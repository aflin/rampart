/* sysupdate.c — SYSUPDATE table maintenance for index operations.
 *
 * See sysupdate.h for the design overview.
 *
 * SQL goes through ddic->ihstmt under TXddicBeginInternalStmt /
 * TXddicEndInternalStmt protection (recursion-safe), with TXpushid +
 * stxalcrtbl/stxalgrant elevation for system-table creation.  Pattern
 * mirrors TXsetstatistic in sysstats.c.
 *
 * Failures here never propagate out — every entry point swallows
 * errors and continues so the actual index operation isn't disturbed.
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "texint.h"
#include "sysupdate.h"

/* External privilege gates from texis core (same as sysstats.c). */
extern int stxalcrtbl ARGS((int));
extern int stxalgrant ARGS((int));

/* Per-process schema-check cache.  Avoids repeated SYSCOLUMNS counts;
 * keyed by DDIC pointer.  Single slot is fine since callers usually
 * keep one DDIC per connection. */
static DDIC  *sysupdate_schema_cached_ddic = NULL;
static int    sysupdate_schema_cached_ok   = 0;
static int    sysupdate_schema_warned      = 0;

static double
sysupdate_now(void)
{
    return TXgettimeofday();
}

/* Run a fixed-SQL no-row, no-param statement under the ihstmt.
 * Caller must have already opened the internal statement and pushed
 * the appropriate id/permissions.  Returns 0 on success, -1 on
 * failure. */
static int
sysupdate_run_simple(DDIC *ddic, const char *sql, const char *label)
{
    int rc;
    rc = SQLPrepare(ddic->ihstmt, (byte *)sql, SQL_NTS);
    if (rc != SQL_SUCCESS) {
        putmsg(MWARN, "sysupdate", "SQLPrepare(%s) failed", label);
        return -1;
    }
    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    if (rc != SQL_SUCCESS) {
        putmsg(MWARN, "sysupdate", "SQLExecute(%s) failed", label);
        return -1;
    }
    return 0;
}

/* Check whether SYSUPDATE exists in the data dictionary.  Caller must
 * already hold the internal-stmt + pushid context. */
static int
sysupdate_table_exists_inner(DDIC *ddic)
{
    int rc, present = 0;
    long sz = 0;
    char buf[64];
    static const char *q =
        "select NAME from SYSTABLES where NAME='SYSUPDATE';";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return 0;
    SQLBindCol(ddic->ihstmt, 1, SQL_C_CHAR, buf, sizeof(buf) - 1, &sz);
    rc = SQLExecute(ddic->ihstmt);
    if (rc == SQL_SUCCESS) {
        if (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) present = 1;
        while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    }
    return present;
}

/* Count columns of SYSUPDATE via SYSCOLUMNS.  Returns count >= 0 or
 * -1 on error.  Caller already holds internal-stmt + pushid. */
static int
sysupdate_count_columns_inner(DDIC *ddic)
{
    int rc, n = 0;
    long sz = 0;
    char buf[64];
    static const char *q =
        "select NAME from SYSCOLUMNS where TBNAME='SYSUPDATE';";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;
    SQLBindCol(ddic->ihstmt, 1, SQL_C_CHAR, buf, sizeof(buf) - 1, &sz);
    rc = SQLExecute(ddic->ihstmt);
    if (rc == SQL_SUCCESS) {
        while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) n++;
    }
    return n;
}

/* Create SYSUPDATE with the v1 schema and GRANT SELECT to PUBLIC.
 * Caller already holds internal-stmt; this function pushes id and
 * elevates create/grant permissions. */
static int
sysupdate_create_table_inner(DDIC *ddic)
{
    int svcr, svgr, ret;
    static const char *createSql =
        "create table SYSUPDATE ("
        " ID counter,"
        " NAME varchar(16),"
        " TBNAME varchar(16),"
        " KIND varchar(16),"
        " PREVIOUS int,"
        " NEXT int,"
        " INTV int,"
        " THRESH int,"
        " ACTION varchar(16),"
        " STAGE int,"
        " NSTAGES int,"
        " STAGENAME varchar(64),"
        " PROGRESS double,"
        " STARTED int,"
        " COMMENTS varchar(255),"
        " PARAMS varchar(64));";
    static const char *grantSql = "GRANT SELECT on SYSUPDATE to PUBLIC;";

    TXpushid(ddic, 0, 0);
    svcr = stxalcrtbl(1);
    svgr = stxalgrant(1);

    ret = sysupdate_run_simple(ddic, createSql, "create SYSUPDATE");
    if (ret == 0)
        (void)sysupdate_run_simple(ddic, grantSql, "grant SYSUPDATE");

    stxalcrtbl(svcr);
    stxalgrant(svgr);
    TXpopid(ddic);
    return ret;
}

int
TXsysupdateEnsure(DDIC *ddic)
{
    int present, ncols;

    if (!ddic) return 0;

    /* Cached fast path. */
    if (ddic == sysupdate_schema_cached_ddic)
        return sysupdate_schema_cached_ok;

    if (!TXddicBeginInternalStmt("sysupdate-ensure", ddic)) return 0;

    /* Check existence under su (so we can see SYSTABLES regardless
     * of caller perms — same idiom as TXsetstatistic). */
    TXpushid(ddic, 0, 0);
    present = sysupdate_table_exists_inner(ddic);
    TXpopid(ddic);

    if (!present) {
        if (sysupdate_create_table_inner(ddic) != 0) {
            if (!sysupdate_schema_warned) {
                putmsg(MWARN, "sysupdate",
                    "could not create SYSUPDATE; index progress will "
                    "not be reported in this database");
                sysupdate_schema_warned = 1;
            }
            sysupdate_schema_cached_ddic = ddic;
            sysupdate_schema_cached_ok   = 0;
            TXddicEndInternalStmt(ddic);
            return 0;
        }
    }

    TXpushid(ddic, 0, 0);
    ncols = sysupdate_count_columns_inner(ddic);
    TXpopid(ddic);

    if (ncols != SYSUPDATE_SCHEMA_NCOLS) {
        if (!sysupdate_schema_warned) {
            putmsg(MWARN, "sysupdate",
                "SYSUPDATE schema mismatch (got %d columns, expected %d); "
                "drop SYSUPDATE and let texis recreate it to enable "
                "index progress reporting",
                ncols, SYSUPDATE_SCHEMA_NCOLS);
            sysupdate_schema_warned = 1;
        }
        sysupdate_schema_cached_ddic = ddic;
        sysupdate_schema_cached_ok   = 0;
        TXddicEndInternalStmt(ddic);
        return 0;
    }

    sysupdate_schema_cached_ddic = ddic;
    sysupdate_schema_cached_ok   = 1;
    TXddicEndInternalStmt(ddic);
    return 1;
}

/* Check whether a SYSUPDATE row exists for `indname`.  Returns 1 if
 * yes, 0 if no, -1 on error.  We don't actually need the ID value —
 * all our UPDATEs use NAME=? as the predicate — so we don't bind the
 * column.  (texis SQLBindCol only supports SQL_C_CHAR; binding
 * SQL_C_LONG would print a "Unsupported" putmsg even though the
 * fetch otherwise works.)  `outId` is left untouched; the parameter
 * is kept for ABI stability with prior callers. */
static int
sysupdate_find_id_inner(DDIC *ddic, const char *indname, long *outId)
{
    int rc, found = 0;
    long namesz;
    static const char *q = "select ID from SYSUPDATE where NAME=?;";

    (void)outId;

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;
    namesz = (long)strlen(indname);
    SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0,
                (char *)indname, &namesz);
    rc = SQLExecute(ddic->ihstmt);
    if (rc == SQL_SUCCESS) {
        if (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) found = 1;
        while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    }
    return found;
}

static int
sysupdate_insert_run_inner(DDIC *ddic, const char *indname, const char *tbname,
                           const char *kind, const char *action, int nstages,
                           const char *stagename, long now)
{
    int rc;
    long namesz, tbsz, kindsz, actionsz, stagensz;
    long previous = -1, next_v = -1, intv = -1, thresh = -1;
    long stage = 1, nstages_l = (long)nstages, started = now;
    double progress = 0.0;
    long zerosz = 0;
    long lsz = sizeof(long), dsz = sizeof(double);
    char emptystr[1] = "";
    static const char *q =
        "insert into SYSUPDATE values(counter,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;

    namesz   = (long)strlen(indname);
    tbsz     = (long)strlen(tbname);
    kindsz   = (long)strlen(kind);
    actionsz = (long)strlen(action);
    stagensz = (long)strlen(stagename);

    SQLSetParam(ddic->ihstmt,  1, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)indname,   &namesz);
    SQLSetParam(ddic->ihstmt,  2, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)tbname,    &tbsz);
    SQLSetParam(ddic->ihstmt,  3, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)kind,      &kindsz);
    SQLSetParam(ddic->ihstmt,  4, SQL_C_LONG,   SQL_INTEGER, 0, 0, &previous,         &lsz);
    SQLSetParam(ddic->ihstmt,  5, SQL_C_LONG,   SQL_INTEGER, 0, 0, &next_v,           &lsz);
    SQLSetParam(ddic->ihstmt,  6, SQL_C_LONG,   SQL_INTEGER, 0, 0, &intv,             &lsz);
    SQLSetParam(ddic->ihstmt,  7, SQL_C_LONG,   SQL_INTEGER, 0, 0, &thresh,           &lsz);
    SQLSetParam(ddic->ihstmt,  8, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)action,    &actionsz);
    SQLSetParam(ddic->ihstmt,  9, SQL_C_LONG,   SQL_INTEGER, 0, 0, &stage,            &lsz);
    SQLSetParam(ddic->ihstmt, 10, SQL_C_LONG,   SQL_INTEGER, 0, 0, &nstages_l,        &lsz);
    SQLSetParam(ddic->ihstmt, 11, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)stagename, &stagensz);
    SQLSetParam(ddic->ihstmt, 12, SQL_C_DOUBLE, SQL_DOUBLE,  0, 0, &progress,         &dsz);
    SQLSetParam(ddic->ihstmt, 13, SQL_C_LONG,   SQL_INTEGER, 0, 0, &started,          &lsz);
    SQLSetParam(ddic->ihstmt, 14, SQL_C_CHAR,   SQL_CHAR,    0, 0, emptystr,          &zerosz);
    SQLSetParam(ddic->ihstmt, 15, SQL_C_CHAR,   SQL_CHAR,    0, 0, emptystr,          &zerosz);

    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

static int
sysupdate_update_run_begin_inner(DDIC *ddic, const char *indname,
                                 const char *kind, const char *action,
                                 int nstages, const char *stagename, long now)
{
    int rc;
    long namesz, kindsz, actionsz, stagensz;
    long stage = 1, nstages_l = (long)nstages, started = now;
    double progress = 0.0;
    long lsz = sizeof(long), dsz = sizeof(double);
    static const char *q =
        "update SYSUPDATE set KIND=?, ACTION=?, STAGE=?, NSTAGES=?,"
        " STAGENAME=?, PROGRESS=?, STARTED=?, COMMENTS='' where NAME=?;";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;

    kindsz   = (long)strlen(kind);
    actionsz = (long)strlen(action);
    stagensz = (long)strlen(stagename);
    namesz   = (long)strlen(indname);

    SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)kind,      &kindsz);
    SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)action,    &actionsz);
    SQLSetParam(ddic->ihstmt, 3, SQL_C_LONG,   SQL_INTEGER, 0, 0, &stage,            &lsz);
    SQLSetParam(ddic->ihstmt, 4, SQL_C_LONG,   SQL_INTEGER, 0, 0, &nstages_l,        &lsz);
    SQLSetParam(ddic->ihstmt, 5, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)stagename, &stagensz);
    SQLSetParam(ddic->ihstmt, 6, SQL_C_DOUBLE, SQL_DOUBLE,  0, 0, &progress,         &dsz);
    SQLSetParam(ddic->ihstmt, 7, SQL_C_LONG,   SQL_INTEGER, 0, 0, &started,          &lsz);
    SQLSetParam(ddic->ihstmt, 8, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)indname,   &namesz);

    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

static int
sysupdate_update_progress_inner(DDIC *ddic, const char *indname,
                                int stage, const char *stagename, double progress)
{
    int rc;
    long namesz, stagensz;
    long stage_l = (long)stage;
    long lsz = sizeof(long), dsz = sizeof(double);
    static const char *q =
        "update SYSUPDATE set STAGE=?, STAGENAME=?, PROGRESS=? where NAME=?;";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;

    stagensz = (long)strlen(stagename);
    namesz   = (long)strlen(indname);

    SQLSetParam(ddic->ihstmt, 1, SQL_C_LONG,   SQL_INTEGER, 0, 0, &stage_l,          &lsz);
    SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)stagename, &stagensz);
    SQLSetParam(ddic->ihstmt, 3, SQL_C_DOUBLE, SQL_DOUBLE,  0, 0, &progress,         &dsz);
    SQLSetParam(ddic->ihstmt, 4, SQL_C_CHAR,   SQL_CHAR,    0, 0, (char *)indname,   &namesz);

    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

static int
sysupdate_update_run_end_inner(DDIC *ddic, const char *indname,
                               long now, const char *errMsg, int success)
{
    int rc;
    long namesz, commentsz;
    long stage = 0, nstages_l = 0, started = 0;
    double progress = 0.0;
    char idleStr[1] = "";
    long idleSz = 0;
    long lsz = sizeof(long), dsz = sizeof(double);
    static const char *qSuccess =
        "update SYSUPDATE set STAGE=?, NSTAGES=?, STAGENAME=?, PROGRESS=?,"
        " STARTED=?, ACTION=?, PREVIOUS=?, COMMENTS=? where NAME=?;";
    static const char *qFailure =
        "update SYSUPDATE set STAGE=?, NSTAGES=?, STAGENAME=?, PROGRESS=?,"
        " STARTED=?, ACTION=?, COMMENTS=? where NAME=?;";

    rc = SQLPrepare(ddic->ihstmt,
                    (byte *)(success ? qSuccess : qFailure), SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;

    commentsz = (long)(errMsg ? strlen(errMsg) : 0);
    namesz    = (long)strlen(indname);

    SQLSetParam(ddic->ihstmt, 1, SQL_C_LONG,   SQL_INTEGER, 0, 0, &stage,           &lsz);
    SQLSetParam(ddic->ihstmt, 2, SQL_C_LONG,   SQL_INTEGER, 0, 0, &nstages_l,       &lsz);
    SQLSetParam(ddic->ihstmt, 3, SQL_C_CHAR,   SQL_CHAR,    0, 0, idleStr,          &idleSz);
    SQLSetParam(ddic->ihstmt, 4, SQL_C_DOUBLE, SQL_DOUBLE,  0, 0, &progress,        &dsz);
    SQLSetParam(ddic->ihstmt, 5, SQL_C_LONG,   SQL_INTEGER, 0, 0, &started,         &lsz);
    SQLSetParam(ddic->ihstmt, 6, SQL_C_CHAR,   SQL_CHAR,    0, 0, idleStr,          &idleSz);
    if (success) {
        SQLSetParam(ddic->ihstmt, 7, SQL_C_LONG, SQL_INTEGER, 0, 0, &now,           &lsz);
        SQLSetParam(ddic->ihstmt, 8, SQL_C_CHAR, SQL_CHAR,    0, 0,
                    (char *)(errMsg ? errMsg : ""),                                  &commentsz);
        SQLSetParam(ddic->ihstmt, 9, SQL_C_CHAR, SQL_CHAR,    0, 0, (char *)indname, &namesz);
    } else {
        SQLSetParam(ddic->ihstmt, 7, SQL_C_CHAR, SQL_CHAR, 0, 0,
                    (char *)(errMsg ? errMsg : ""),                                  &commentsz);
        SQLSetParam(ddic->ihstmt, 8, SQL_C_CHAR, SQL_CHAR, 0, 0, (char *)indname,   &namesz);
    }

    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

static int
sysupdate_delete_by_name_inner(DDIC *ddic, const char *indname)
{
    int rc;
    long namesz;
    static const char *q = "delete from SYSUPDATE where NAME=?;";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;
    namesz = (long)strlen(indname);
    SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0, (char *)indname, &namesz);
    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

/* ----- Public API --------------------------------------------------- */

void
TXsysupdateBegin(TXsysupdateSink *sink, DDIC *ddic,
                 const char *indname, const char *tbname,
                 const char *kind, const char *action,
                 int nstages, const char *firstStage)
{
    long id, now;
    int found, rc;

    memset(sink, 0, sizeof(*sink));
    if (!ddic || !indname || !tbname || !kind || !action ||
        nstages < 1 || !firstStage)
        return;

    if (!TXsysupdateEnsure(ddic)) return;
    if (!TXddicBeginInternalStmt("sysupdate-begin", ddic)) return;

    TXpushid(ddic, 0, 0);

    now = (long)time(NULL);
    found = sysupdate_find_id_inner(ddic, indname, &id);
    if (found > 0) {
        rc = sysupdate_update_run_begin_inner(ddic, indname, kind, action,
                                              nstages, firstStage, now);
    } else if (found == 0) {
        rc = sysupdate_insert_run_inner(ddic, indname, tbname, kind, action,
                                        nstages, firstStage, now);
    } else {
        rc = -1;
    }

    TXpopid(ddic);
    TXddicEndInternalStmt(ddic);

    if (rc != 0) return;

    sink->ddic = ddic;
    snprintf(sink->name, sizeof(sink->name), "%s", indname);
    sink->nstages = nstages;
    sink->curStage = 1;
    snprintf(sink->stageName, sizeof(sink->stageName), "%s", firstStage);
    sink->lastProgress = 0.0;
    sink->lastWriteTime = sysupdate_now();
    sink->dirty = 0;
}

void
TXsysupdateAdvanceStage(TXsysupdateSink *sink, int stage,
                        const char *stageName)
{
    int rc;
    if (!sink || !sink->ddic || !stageName) return;
    if (stage < 1 || stage > sink->nstages) return;
    if (!TXddicBeginInternalStmt("sysupdate-advance", sink->ddic)) return;
    TXpushid(sink->ddic, 0, 0);
    rc = sysupdate_update_progress_inner(sink->ddic, sink->name, stage,
                                         stageName, 0.0);
    TXpopid(sink->ddic);
    TXddicEndInternalStmt(sink->ddic);
    if (rc != 0) return;
    sink->curStage = stage;
    snprintf(sink->stageName, sizeof(sink->stageName), "%s", stageName);
    sink->lastProgress = 0.0;
    sink->lastWriteTime = sysupdate_now();
    sink->dirty = 0;
}

void
TXsysupdateProgress(TXsysupdateSink *sink, double frac)
{
    double now;
    int rc;
    if (!sink || !sink->ddic) return;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    if (fabs(frac - sink->lastProgress) > 1e-9) sink->dirty = 1;

    now = sysupdate_now();
    if (fabs(frac - sink->lastProgress) < 0.001) return;
    if (now - sink->lastWriteTime < 1.0)         return;

    if (!TXddicBeginInternalStmt("sysupdate-progress", sink->ddic)) return;
    TXpushid(sink->ddic, 0, 0);
    rc = sysupdate_update_progress_inner(sink->ddic, sink->name,
                                         sink->curStage, sink->stageName,
                                         frac);
    TXpopid(sink->ddic);
    TXddicEndInternalStmt(sink->ddic);
    if (rc == 0) {
        sink->lastProgress  = frac;
        sink->lastWriteTime = now;
        sink->dirty = 0;
    }
}

void
TXsysupdateEnd(TXsysupdateSink *sink, const char *errMsg)
{
    long now;
    int success;
    char comments[256];

    if (!sink || !sink->ddic) {
        if (sink) memset(sink, 0, sizeof(*sink));
        return;
    }

    now = (long)time(NULL);
    success = (errMsg == NULL);
    if (success) {
        struct tm tmv;
        time_t t = (time_t)now;
        localtime_r(&t, &tmv);
        strftime(comments, sizeof(comments),
                 "completed at %Y-%m-%d %H:%M:%S", &tmv);
    } else {
        snprintf(comments, sizeof(comments), "%s", errMsg);
    }

    if (TXddicBeginInternalStmt("sysupdate-end", sink->ddic)) {
        TXpushid(sink->ddic, 0, 0);
        (void)sysupdate_update_run_end_inner(sink->ddic, sink->name, now,
                                             comments, success);
        TXpopid(sink->ddic);
        TXddicEndInternalStmt(sink->ddic);
    }
    memset(sink, 0, sizeof(*sink));
}

void
TXsysupdateOnCreateFailure(TXsysupdateSink *sink)
{
    if (!sink || !sink->ddic) {
        if (sink) memset(sink, 0, sizeof(*sink));
        return;
    }
    if (TXddicBeginInternalStmt("sysupdate-create-fail", sink->ddic)) {
        TXpushid(sink->ddic, 0, 0);
        (void)sysupdate_delete_by_name_inner(sink->ddic, sink->name);
        TXpopid(sink->ddic);
        TXddicEndInternalStmt(sink->ddic);
    }
    memset(sink, 0, sizeof(*sink));
}

void
TXsysupdateOnDrop(DDIC *ddic, const char *indname)
{
    if (!ddic || !indname) return;
    if (!TXsysupdateEnsure(ddic)) return;
    if (!TXddicBeginInternalStmt("sysupdate-drop", ddic)) return;
    TXpushid(ddic, 0, 0);
    (void)sysupdate_delete_by_name_inner(ddic, indname);
    TXpopid(ddic);
    TXddicEndInternalStmt(ddic);
}

void
TXsysupdateInvalidateCache(void)
{
    sysupdate_schema_cached_ddic = NULL;
    sysupdate_schema_cached_ok   = 0;
    /* Don't reset sysupdate_schema_warned — it's a per-process "have
     * we already warned" gate, independent of cache validity. */
}

/* UPDATE SYSUPDATE.COMMENTS for one index (no run-state change). */
static int
sysupdate_update_comment_inner(DDIC *ddic, const char *indname,
                               const char *comment)
{
    int rc;
    long namesz, commentsz;
    static const char *q =
        "update SYSUPDATE set COMMENTS=? where NAME=?;";

    rc = SQLPrepare(ddic->ihstmt, (byte *)q, SQL_NTS);
    if (rc != SQL_SUCCESS) return -1;
    commentsz = (long)(comment ? strlen(comment) : 0);
    namesz    = (long)strlen(indname);
    SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0,
                (char *)(comment ? comment : ""), &commentsz);
    SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0,
                (char *)indname, &namesz);
    rc = SQLExecute(ddic->ihstmt);
    while (SQLFetch(ddic->ihstmt) == SQL_SUCCESS) ;
    return (rc == SQL_SUCCESS) ? 0 : -1;
}

void
TXsysupdateNote(DDIC *ddic, const char *indname, const char *comment)
{
    if (!ddic || !indname || !comment) return;
    if (!TXsysupdateEnsure(ddic)) return;
    if (!TXddicBeginInternalStmt("sysupdate-note", ddic)) return;
    TXpushid(ddic, 0, 0);
    (void)sysupdate_update_comment_inner(ddic, indname, comment);
    TXpopid(ddic);
    TXddicEndInternalStmt(ddic);
}

/* Process-static current-sink — see header for safety rationale. */
static TXsysupdateSink *sysupdate_current = NULL;

void
TXsysupdateSetCurrent(TXsysupdateSink *sink)
{
    sysupdate_current = sink;
}

TXsysupdateSink *
TXsysupdateGetCurrent(void)
{
    return sysupdate_current;
}

void
TXsysupdateAdvanceStageByLabel(const char *label)
{
    TXsysupdateSink *sink = sysupdate_current;
    if (!sink || !sink->ddic || !label) return;

    /* Substring match against the meter label.  The labels are stable
     * — they appear directly in fdbim.c, index.c, etc. — so this is
     * approximately as brittle as the script's old screen-scrape
     * `monitor()` thread, but the matching happens in C right where
     * the meter actually opens, not by parsing buffered stdout. */
    int stage = 0;
    const char *name = NULL;
    if (strstr(label, "Creating new token") ||
        strstr(label, "Reading original token")) {
        stage = 1; name = "creating tokens";
    }
    else if (strstr(label, "Final merge")) {
        /* Check Final merge first since "Final merge to index:" also
         * contains "index" which may overlap with future patterns. */
        stage = 3; name = "final merge";
    }
    else if (strstr(label, "Indexing")) {
        stage = 2; name = "indexing";
    }
    if (stage == 0 || stage > sink->nstages) return;
    TXsysupdateAdvanceStage(sink, stage, name);
}
