/* vecindex.h — texis-engine integration for the Vamana ANN vector index.
 *
 * This is the only header in the vec-index module that depends on texis
 * engine types (DDIC, DBTBL, IINODE, PRED, etc.).  vecgraph.h and vecio.h
 * are pure C with no engine deps and can be tested standalone.
 *
 * The integration glue lives in vecindex.c.  Engine code (index.c,
 * predopt.c, idxinfo.c, addtoind.c) calls into the TXvec* functions
 * here when handling INDEX_VEC.
 */

#ifndef VECINDEX_H
#define VECINDEX_H

#include "dbquery.h"          /* DDIC, DBTBL, etc. */
#include "vecgraph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- SYSINDEX.PARAMS handling ------------------------------------- */

/* Parsed parameters from SYSINDEX.PARAMS for a vec index.  Mirrors
 * vec_graph_params_t plus texis-side merge thresholds plus persistent
 * flush state.
 *
 * `flush_mode` controls per-row save behavior:
 *   0 = auto   — save_atomic after every row mutation (current default,
 *                always-coherent on disk)
 *   1 = manual — defer saves until explicit flush, connection close,
 *                or process exit; faster bulk loads, requires reconcile
 *                on next open if process crashes mid-burst
 * `dirty` is the persisted bit indicating in-memory state has unflushed
 * mutations; reconcile-on-open consults it to skip the table scan when
 * disk and memory match.  Only set in manual-flush mode.
 */
typedef struct {
    vec_graph_params_t graph;     /* dim, M, ef_construction, alpha, metric, ... */
    int                threshold_t;   /* delta-tier insert count → trigger merge */
    int                threshold_d;   /* tombstone count → trigger merge */
    int                flush_mode;    /* 0=auto, 1=manual */
    int                dirty;         /* 1 iff unflushed writes exist on disk */
    /* Element dtype — an FTN_VEC_* tag indicating how each cell of the
     * indexed column should be interpreted.  For typed varvec columns
     * this matches the column type and is set automatically.  For
     * varbyte columns the user must supply `vec_dtype 'f16'` (etc.) at
     * CREATE INDEX time; without it the create is rejected.
     * Persisted in PARAMS so the open path can pick the right
     * convert_to_f32 routine without consulting the column. */
    int                dtype;
} TXvecParams;

/* Parse a SYSINDEX.PARAMS string of the form
 *   "type=vec;metric=dot;dim=768;M=64;efc=200;alpha=1.2;..."
 * into `out`.  Returns 0 on success, -1 on unrecognized param or parse error.
 */
int  TXvecParamsParse(TXvecParams *out, const char *params);

/* Initialize `out` from defaults, then overlay any vec_* WITH-clause
 * options found in `options`.  Returns 0 on success, -1 if any value
 * is out of range (a putmsg has been issued).  Both index.c (for
 * SYSINDEX.PARAMS) and TXvecCreateIndex (for the actual graph build)
 * call this so they see identical params.
 */
int  TXvecParamsFromOptions(TXvecParams *out, TXindOpts *options);

/* Stringify TXvecParams into a SYSINDEX.PARAMS line.  Buffer must be at
 * least TX_VEC_PARAMS_TEXT_MAX bytes.  Returns the number of bytes written
 * (excluding null), or -1 on overflow.
 */
#define TX_VEC_PARAMS_TEXT_MAX 256
int  TXvecParamsToText(char *buf, size_t bufSz, const TXvecParams *p);

/* ----- Index creation ----------------------------------------------- */

/* Build a vector index from a source table.  Scans the table, gathers
 * (RECID, vector) pairs, builds the Vamana graph in memory, persists
 * via vecio_write to <indfile>.vec, writes the SYSINDEX row.
 *
 * Called from index.c:createindex when itype == INDEX_VEC.
 *
 * Returns:
 *    1 — full success
 *    0 — partial success (some rows skipped due to dim/dtype mismatch
 *         or NULL vectors; index is usable but incomplete)
 *   -1 — hard error (allocation, I/O, options parse, etc.)
 */
int  TXvecCreateIndex(DDIC *ddic, DBTBL *dbtbl,
                      const char *field, const char *indname,
                      const char *indfile, TXindOpts *options,
                      TXvecParams *outParams);

/* ----- Per-row maintenance hooks ------------------------------------ */

/* Called from procupd.c TXaddtoindices when the index is INDEX_VEC.
 * Reads the indexed field's vector from the current row and adds it to
 * the index, then persists.  Returns 0 on success, -1 on hard error.
 *
 * `field` is the column name (from `dbtbl->vecIndexFldNames[i]`); the
 * row is wherever `dbtbl` is currently positioned.
 */
int  TXvecAddRow(DDIC *ddic, DBTBL *dbtbl,
                 const char *indfile, const char *field,
                 RECID *recid);

/* Counterpart to TXvecAddRow for DELETE — removes the recid from the
 * index and persists.
 */
int  TXvecDelRow(DDIC *ddic, DBTBL *dbtbl,
                 const char *indfile, const char *field,
                 RECID *recid);

/* ----- Search-side --------------------------------------------------- */

/* Per-process cached graph handle.  Opaque to callers; currently a flat
 * map keyed by index file path.
 */
typedef struct TXvecHandle TXvecHandle;

/* Open (or fetch from cache) a graph handle for the given index file.
 * Returns NULL on error.
 */
TXvecHandle *TXvecOpen(DDIC *ddic, const char *indfile);

/* Search: top-k nearest to `query`, with beam width `ef` (0 = use
 * ef_construction from index params).  `results` must be size k.
 * Returns number of results filled (≤ k), or SIZE_MAX on error.
 */
size_t       TXvecSearch(TXvecHandle *h, const float *query, size_t k,
                         size_t ef, vec_search_result_t *results);

/* Close all cached handles (called on engine shutdown). */
void         TXvecCloseAll(void);

/* Invalidate (and free) any cached handle for `indfile`.  Called from
 * the DROP INDEX path so a subsequent CREATE INDEX with the same name
 * re-loads from the freshly-written .vec file rather than handing back
 * a stale pointer to the old usearch_index_t.
 */
void         TXvecInvalidateHandle(const char *indfile);

/* Drop the WAL table associated with the vec index at `indfile`.
 * Called from the DROP INDEX path so a stale WAL doesn't survive the
 * index that owned it. Best-effort; missing-table is treated as
 * already-dropped. */
void         TXvecDropAux(DDIC *ddic, const char *indfile);

/* Flush a specific index: save the in-memory state to disk and clear
 * the SYSINDEX dirty bit.  No-op (returns 0) if the cached handle for
 * `indfile` is already clean or no handle exists.  Returns 0 on success,
 * -1 on I/O error.
 */
int          TXvecFlush(DDIC *ddic, const char *indfile);

/* Flush every dirty handle in the per-process cache.  Used by the exit
 * hook (clean shutdown) and by the connection-close path (when the
 * connection had vecAutoFlush=false).  Returns 0 on success, -1 if
 * any handle failed (other handles are still attempted).
 */
int          TXvecFlushAll(DDIC *ddic);

/* Remember `ddic` for later use by the embedder's exit-time flush.
 * Embedders (rampart-sql) wire add_exit_func(handler, NULL) where
 * `handler` calls TXvecFlushAll(TXvecGetExitHookDDIC()).
 */
void         TXvecRegisterExitHook(DDIC *ddic);
DDIC        *TXvecGetExitHookDDIC(void);

/* Connection-scoped defer override.  When the embedder sets this to
 * non-zero (e.g. via JS `sql.set({vecAutoFlush:false})`), per-row
 * INSERT/DELETE skip save_atomic regardless of the per-index PARAMS
 * `flush=auto|manual` setting — the index is treated as if it were in
 * manual mode for the duration.  Set back to zero to return to per-
 * index policy; the embedder typically calls TXvecFlushAll() right
 * after that to make the write visible on disk.
 *
 * The flag is process-static.  In rampart-sql's forked-helper mode
 * each connection has its own process, so this is naturally per-
 * connection.  In single-process embeddings with multiple concurrent
 * connections (rare for texis given its single-threaded engine), the
 * flag is shared — last writer wins.
 */
void         TXvecSetForceDefer(int on);
int          TXvecGetForceDefer(void);

/* ----- Planner integration ------------------------------------------ */

/* Score function for idxinfo.c:indexscore.  Mirrors TXbtreeScoreIndex /
 * TX3dbiScoreIndex.  Returns 0 if the index can't serve the predicate,
 * else a positive score in [1, TX_INDEX_SUBSCORE_MAX].
 */
int  TXvecScoreIndex(const char *sysindexFields,
                     const char *sysindexParams,
                     DBTBL *dbtbl, int fldOp,
                     FLD *param, int paramIsRHS);

/* Access function for predopt.c:realwork's FOP_MMV branch.
 * Loads the .vec graph (cached after first open), runs an ANN search,
 * and materializes the top-K candidates into an in-memory btree wrapped
 * as IINDEX.orig (DBIDX_MEMORY).  The executor then iterates that btree
 * and re-evaluates the LIKEV predicate per candidate row to compute
 * `$rank` — `*cop` is always set to 0 (post-process required).
 *
 * Returns NULL on any failure, including unsupported query dtypes or
 * dim-mismatch — caller falls back to brute-force LIKEV.
 */
IINDEX *TXvecIxVecIndex(const char *iname,
                        const char *sysindexParams,
                        FLD *infld, const char *fname,
                        DBTBL *dbtbl, int op, int *cop);

#ifdef __cplusplus
}
#endif

#endif  /* VECINDEX_H */
