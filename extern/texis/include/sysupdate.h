/* sysupdate.h — texis-side maintenance for the SYSUPDATE table.
 *
 * SYSUPDATE is a per-database system table that tracks index-update
 * progress and (via rampart-sqlUpdate.js) per-index OPTIMIZE schedules.
 * Texis automatically creates the table on first CREATE/ALTER INDEX
 * call against any database that doesn't yet have one.  Progress
 * writes happen during long index operations so an external observer
 * can do `SELECT * FROM SYSUPDATE` to see what's running.
 *
 * Schema (16 columns, fixed forever — texis SQL has no
 * ALTER TABLE ADD COLUMN):
 *
 *   ID         counter
 *   NAME       varchar(16)    -- index name
 *   TBNAME     varchar(16)    -- indexed table name
 *   KIND       varchar(16)    -- 'fulltext', 'vec-hnsw', 'vec-ivfpq'
 *   PREVIOUS   int            -- unix ts of last completed run, -1 = never
 *   NEXT       int            -- unix ts of next scheduled run, -1 = unscheduled
 *   INTV       int            -- schedule interval seconds, -1 = unscheduled
 *   THRESH     int            -- HAVING COUNT(NewRows) threshold, -1 = unscheduled
 *   ACTION     varchar(16)    -- '' (idle), 'optimize', 'rebuild', 'create'
 *   STAGE      int            -- 0 (idle), 1..NSTAGES while running
 *   NSTAGES    int            -- 0 (idle), total stages while running
 *   STAGENAME  varchar(64)    -- '' (idle), current stage label
 *   PROGRESS   double         -- 0.0..1.0 fraction within STAGE
 *   STARTED    int            -- 0 (idle), unix ts of current run start
 *   COMMENTS   varchar(255)   -- free-form: error message, info, etc.
 *   PARAMS     varchar(64)    -- JSON-encoded future per-row knobs
 *
 * Writes are rate-limited: at most once per second under normal
 * progress, plus an unconditional final-tick on every operation
 * boundary so the row always lands in a clean final state.
 *
 * Failure tolerance: if SYSUPDATE doesn't exist and can't be created,
 * if the schema differs from the 16-column expected form, or if any
 * SYSUPDATE write fails, texis warns (putmsg MWARN) and proceeds
 * with the actual index operation.  SYSUPDATE writes never fail an
 * index operation.
 */

#ifndef SYSUPDATE_H
#define SYSUPDATE_H

#include <stddef.h>
#include "dbquery.h"     /* DDIC */

#ifdef __cplusplus
extern "C" {
#endif

/* The schema-version "magic" — number of columns the v1 table has.
 * If a SYSUPDATE table exists but has a different number of columns,
 * we treat it as a stale schema and skip all SYSUPDATE writes for
 * that database. */
#define SYSUPDATE_SCHEMA_NCOLS 16

/* Progress-sink state.  One per active CREATE/ALTER operation.
 * Zero-initialize before TXsysupdateBegin, then pass to subsequent
 * Stage / Progress / End calls.  A NULL ddic in this struct after
 * Begin means "skipped, no SYSUPDATE writes" (table missing,
 * schema mismatch, etc.) — subsequent calls become no-ops. */
typedef struct TXsysupdateSink {
    DDIC       *ddic;            /* connection handle, NULL = skip writes */
    char        name[256];       /* index name (NUL-terminated) */
    int         nstages;         /* total stages declared at Begin */
    int         curStage;        /* 1..nstages, 0 = no stage active */
    char        stageName[64];
    double      lastProgress;    /* last PROGRESS value written */
    double      lastWriteTime;   /* unix ts (seconds, double) of last write */
    int         dirty;           /* set when stage/progress changed since
                                  * last write — forces a final flush */
} TXsysupdateSink;

/* Ensure SYSUPDATE exists and is the expected schema for this database.
 * Returns 1 if SYSUPDATE is usable for writes (present and 16-col),
 * 0 if it isn't (missing-and-couldn't-create, or schema mismatch).
 * Idempotent — caches the result on the DDIC so repeated calls are
 * cheap.  Logs a one-time warning via putmsg MWARN on any
 * skip-causing condition. */
int TXsysupdateEnsure(DDIC *ddic);

/* Mark the start of an index operation in SYSUPDATE.  Inserts a row
 * if no row exists for `indname`, or updates the existing one
 * (preserving schedule columns NEXT/INTV/THRESH/PARAMS).
 *
 * `kind`       — 'fulltext', 'vec-hnsw', 'vec-ivfpq'
 * `action`     — 'create', 'optimize', 'rebuild'
 * `nstages`    — total stages this operation will go through (>= 1)
 * `firstStage` — name to record for STAGE=1 (e.g. "copy sealed")
 *
 * Initializes `sink` for subsequent calls.  On failure to write
 * SYSUPDATE, sets sink->ddic = NULL so later calls become no-ops. */
void TXsysupdateBegin(TXsysupdateSink *sink, DDIC *ddic,
                      const char *indname, const char *tbname,
                      const char *kind, const char *action,
                      int nstages, const char *firstStage);

/* Advance to a new stage.  Resets PROGRESS to 0.0 and updates STAGE
 * and STAGENAME.  Always writes (not rate-limited — stage transitions
 * are rare and important). */
void TXsysupdateAdvanceStage(TXsysupdateSink *sink, int stage,
                             const char *stageName);

/* Report progress within the current stage.  `frac` is 0.0..1.0.
 * Rate-limited: writes only when (a) `frac` differs from the last
 * written value by >= 0.001, AND (b) at least 1.0 seconds have passed
 * since the last write.  No-op if sink->ddic is NULL. */
void TXsysupdateProgress(TXsysupdateSink *sink, double frac);

/* Mark the operation complete.  On success (errMsg == NULL): clears
 * STAGE/NSTAGES/STAGENAME/ACTION/STARTED/PROGRESS to idle values,
 * sets PREVIOUS to current time, sets COMMENTS to a brief
 * "completed at ..." string.  On failure (errMsg != NULL): clears
 * STAGE/NSTAGES/STAGENAME/ACTION/STARTED/PROGRESS to idle, leaves
 * PREVIOUS unchanged, sets COMMENTS to errMsg.  Always writes
 * (final-tick guarantee). */
void TXsysupdateEnd(TXsysupdateSink *sink, const char *errMsg);

/* On CREATE INDEX failure: delete the SYSUPDATE row entirely so an
 * orphan row doesn't reference an index that never existed.
 * (For OPTIMIZE/REBUILD failures, prefer TXsysupdateEnd with
 * errMsg.) */
void TXsysupdateOnCreateFailure(TXsysupdateSink *sink);

/* Delete all SYSUPDATE rows for this index.  Called from
 * TXdropdindex (covers both DROP INDEX directly and DROP TABLE
 * cascade through TXdropdtable).  No-op if SYSUPDATE doesn't exist. */
void TXsysupdateOnDrop(DDIC *ddic, const char *indname);

/* Invalidate the per-process schema-check cache.  Call when SYSUPDATE
 * itself is dropped or its schema may have changed out-of-band, so the
 * next operation re-checks the table state. */
void TXsysupdateInvalidateCache(void);

/* Write a free-form note to SYSUPDATE.COMMENTS for an index without
 * touching the run-state columns.  Used by alterIndex when an
 * operation is short-circuited by HAVING (no Begin/End cycle, but we
 * still want observers to see "why nothing happened").  No-op if
 * SYSUPDATE doesn't exist or no row exists for `indname`. */
void TXsysupdateNote(DDIC *ddic, const char *indname, const char *comment);

/* Process-static "current operation sink" so meter.c and other code
 * that doesn't have a DDIC handy can still advance the SYSUPDATE
 * stage.  Set/cleared by the dispatch layer (alterIndex.c,
 * createindex.c) around the operation.  Only one operation at a time
 * per process — guaranteed by texislockd's table write lock plus
 * rampart's single-ALTER-per-helper model. */
void              TXsysupdateSetCurrent(TXsysupdateSink *sink);
TXsysupdateSink  *TXsysupdateGetCurrent(void);

/* Auto-advance the current sink's stage based on a meter label.
 * Called from meter.c openmeter() so any operation that opens a
 * meter with a recognized label automatically reports the right
 * SYSUPDATE.STAGE / STAGENAME, without each callsite needing
 * explicit AdvanceStage calls.
 *
 * Recognized fulltext labels (case-sensitive substring match):
 *   "Creating new token", "Reading original token" → stage 1
 *   "Indexing"                                     → stage 2
 *   "Final merge"                                  → stage 3
 *
 * Vec labels are explicitly handled by their operation code (which
 * calls TXsysupdateAdvanceStage directly) and aren't matched here. */
void TXsysupdateAdvanceStageByLabel(const char *label);

#ifdef __cplusplus
}
#endif

#endif /* SYSUPDATE_H */
