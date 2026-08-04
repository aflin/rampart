/* txdummyver.c — provides TxSeconds + TXtexisver() AND stubs for the
 * vec-dispatch entry points referenced from BUILD_SRCS files.
 *
 * Build setup:
 *   - texisbuild (lean): includes this file.  No vec sources linked,
 *     so the stubs below are the only definitions of `TXvec*` and
 *     `TXsqlFunc_embed` referenced from non-vec BUILD_SRCS files.  Tools that
 *     link texisbuild (rex, metamorph, backref, texislockd) only ever
 *     reach the stubs through SQL plan/execute paths — and those tools
 *     don't run SQL, so the stubs are dead code.
 *   - texisapi (full): does NOT include this file.  Uses the real
 *     texver.c (generated each build) and the real vec implementations
 *     from vecindex.c / vecindex_ivfpq.cpp / vec_blas_probe.c.
 *
 * The stubs let BUILD_SRCS files (idxinfo.c, droptbl.c, index.c,
 * procupd.c, alterIndex.c, dbtbl.c, predopt.c, ...) link against
 * texisbuild without dragging vecindex.o (and transitively faiss +
 * libopenblas/libgomp/libgfortran) into the lean tools' DT_NEEDED.
 *
 * Since rampart's vendored texis tree is frozen at a fixed version,
 * the version constants below are hardcoded to the snapshot's actual
 * values so tools linking `texisbuild` report a real version rather
 * than the literal "Dummy version" the original stub returned.  When
 * the vendored texis snapshot is bumped, refresh the constants to
 * match the new `texver.c` the build generates.
 */
#include "txcoreconfig.h"
#include <stdio.h>          /* FILE — transitively required by vecindex.h */
#include "vecindex.h"

/* ----- Version metadata ------------------------------------------- */

const long TxSeconds = 1780864275;

char *
TXtexisver()
{
    static char txver[] = "8.0.1780864275-dev";
    return txver;
}

/* ----- Vec dispatch stubs -----------------------------------------
 *
 * No-op implementations of every vec entry point referenced from
 * BUILD_SRCS files (the ones we found via "undefined reference" errors
 * during the texisbuild link).  All return error / NULL / 0 so callers
 * downstream of an unintended hit see a clean failure rather than a
 * crash.
 *
 * If a future BUILD_SRCS change references a new TXvec* / TXsqlFunc_*
 * symbol, the build will fail at the texisbuild link with a fresh
 * "undefined reference"; add a stub here mirroring the signature.
 */

int
TXvecScoreIndex(const char *sysindexFields, const char *sysindexParams,
                DBTBL *dbtbl, int fldOp, FLD *param, int paramIsRHS)
{
    (void)sysindexFields; (void)sysindexParams;
    (void)dbtbl; (void)fldOp; (void)param; (void)paramIsRHS;
    return 0;
}

int
TXvecCreateIndex(DDIC *ddic, DBTBL *dbtbl,
                 const char *field, const char *indname,
                 const char *indfile, TXindOpts *options,
                 TXvecParams *outParams)
{
    (void)ddic; (void)dbtbl; (void)field; (void)indname;
    (void)indfile; (void)options; (void)outParams;
    return -1;
}

int
TXvecAddRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field,
            RECID *recid)
{
    (void)ddic; (void)dbtbl; (void)indfile; (void)field; (void)recid;
    return -1;
}

int
TXvecDelRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field,
            RECID *recid)
{
    (void)ddic; (void)dbtbl; (void)indfile; (void)field; (void)recid;
    return -1;
}

int
TXvecAddRowDelta(const char *indfile, DBTBL *dbtbl,
                 const char *field, RECID *recid)
{
    (void)indfile; (void)dbtbl; (void)field; (void)recid;
    return -1;
}

int
TXvecDelRowDelta(const char *indfile, RECID *recid)
{
    (void)indfile; (void)recid;
    return -1;
}

int
TXvecCreateDeltaBtrees(const char *indfile)
{
    (void)indfile;
    return -1;
}

int
TXvecAbstractBestChunk(const char *query, void *vecData, size_t vecBytes,
                       int colType, int *cixOut, int *ccntOut)
{
    (void)query; (void)vecData; (void)vecBytes; (void)colType;
    (void)cixOut; (void)ccntOut;
    return -1;
}

int
TXvecExcerptChunkScores(const char *query, void *vecData, size_t vecBytes,
                        int colType, double **scoresOut, size_t *kOut)
{
    (void)query; (void)vecData; (void)vecBytes; (void)colType;
    (void)scoresOut; (void)kOut;
    return -1;
}

void
TXvecDropAux(DDIC *ddic, const char *indfile)
{
    (void)ddic; (void)indfile;
}

void
TXvecInvalidateHandle(const char *indfile)
{
    (void)indfile;
}

size_t
TXvecCountNewRows(const char *indfile)
{
    (void)indfile;
    return 0;
}

const char *
TXvecKindFromOptions(TXindOpts *options)
{
    (void)options;
    return "vec-hnsw";   /* harmless default; this branch is dead in lean tools */
}

int
TXvecParamsParse(TXvecParams *out, const char *params)
{
    (void)out; (void)params;
    return -1;
}

int
TXvecParamsFromOptions(TXvecParams *out, TXindOpts *options)
{
    (void)out; (void)options;
    return -1;
}

int
TXvecParamsToText(char *buf, size_t bufSz, const TXvecParams *p)
{
    (void)buf; (void)bufSz; (void)p;
    return -1;
}

int
TXvecOptimize(DDIC *ddic, const char *indname,
              const char *indfile, const char *tableName,
              const char *field, const char *params,
              TXindOpts *options)
{
    (void)ddic; (void)indname; (void)indfile; (void)tableName;
    (void)field; (void)params; (void)options;
    return -1;
}

int
TXvecRebuild(DDIC *ddic, const char *indname,
             const char *indfile, const char *tableName,
             const char *field, const char *params,
             TXindOpts *options)
{
    (void)ddic; (void)indname; (void)indfile; (void)tableName;
    (void)field; (void)params; (void)options;
    return -1;
}

IINDEX *
TXvecIxVecIndex(const char *iname, const char *sysindexParams,
                FLD *infld, const char *fname,
                DBTBL *dbtbl, int op, int *cop)
{
    (void)iname; (void)sysindexParams; (void)infld; (void)fname;
    (void)dbtbl; (void)op;
    if (cop) *cop = 0;
    return NULL;
}

IINDEX *
TXvecLinearVecIndex(DBTBL *dbtbl, const char *fname, FLD *infld)
{
    (void)dbtbl; (void)fname; (void)infld;
    return NULL;            /* lean tools: plain per-row LIKEV path */
}

/* Embed callback registry — lean tools don't run the embed() SQL
 * function, but dbtbl.c's SQL-func table has a slot referencing it. */

TXembedFunc
TXgetEmbedFunc(void **user_data_out)
{
    if (user_data_out) *user_data_out = NULL;
    return NULL;
}

int
TXsqlFunc_embed(FLD *f1, FLD *f2, FLD *f3, FLD *f4)
{
    (void)f1; (void)f2; (void)f3; (void)f4;
    return -1;
}

TXembedDocFunc
TXgetEmbedDocFunc(void **user_data_out)
{
    if (user_data_out) *user_data_out = NULL;
    return NULL;
}

int
TXsqlFunc_chunkembed(FLD *f1, FLD *f2, FLD *f3, FLD *f4)
{
    (void)f1; (void)f2; (void)f3; (void)f4;
    return -1;
}

int
TXsqlFunc_chunkavg(FLD *f1, FLD *f2, FLD *f3)
{
    (void)f1; (void)f2; (void)f3;
    return -1;
}

int
TXsqlFunc_chunkcoherence(FLD *f1, FLD *f2)
{
    (void)f1; (void)f2;
    return -1;
}

TXchunkSpansFunc
TXgetChunkSpansFunc(void **user_data_out)
{
    if (user_data_out) *user_data_out = NULL;
    return NULL;
}

/* LIKEV chunk scratch — fldops.c (shared with lean tools) writes it;
 * dbtbl.c's abstract() reads it, but the read is inert in the lean
 * tools: the chunk-spans callback stub below returns none, so the
 * vec-snippet mode always falls back to a plain abstract. */
static __thread int tx_likev_last_chunk_ix_dummy  = -1;
static __thread int tx_likev_last_chunk_cnt_dummy = 0;

void
TXlikevSetLastChunk(int ix, int cnt)
{
    tx_likev_last_chunk_ix_dummy  = ix;
    tx_likev_last_chunk_cnt_dummy = cnt;
}

void
TXlikevGetLastChunk(int *ix, int *cnt)
{
    if (ix)  *ix  = tx_likev_last_chunk_ix_dummy;
    if (cnt) *cnt = tx_likev_last_chunk_cnt_dummy;
}
