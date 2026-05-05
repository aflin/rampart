/* vecindex.h — texis-engine integration for the usearch ANN vector index.
 *
 * This is the only header in the vec-index module that depends on texis
 * engine types (DDIC, DBTBL, IINODE, PRED, etc.).
 *
 * The integration glue lives in vecindex.c.  Engine code (index.c,
 * predopt.c, idxinfo.c, addtoind.c) calls into the TXvec* functions
 * here when handling INDEX_VEC.
 */

#ifndef VECINDEX_H
#define VECINDEX_H

#include <stddef.h>
#include <stdint.h>
#include "dbquery.h"          /* DDIC, DBTBL, etc. */

/* Forward decl for IINDEX (defined in texint.h as `struct tagIINDEX`)
 * so this header doesn't have to pull in texint.h transitively.
 * Callers of TXvecIxVecIndex should include texint.h to use the
 * returned pointer. */
struct tagIINDEX;
#ifndef IINDEX
#define IINDEX struct tagIINDEX
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Index parameters (formerly in vecgraph.h) -------------------- */

typedef uint64_t vec_id_t;
#define VEC_ID_INVALID ((vec_id_t)-1)

typedef enum {
    VEC_METRIC_DOT = 1,    /* inner product; on normalized vectors == cosine */
    VEC_METRIC_L2  = 2,    /* squared Euclidean */
} vec_metric_t;

typedef struct {
    int           dim;             /* vector dimensionality (required) */
    int           M;               /* max neighbors per node (typical 64-96) */
    int           ef_construction; /* search width during build (typical 200) */
    float         alpha;           /* RobustPrune relaxation (typical 1.2) */
    vec_metric_t  metric;
    uint32_t      seed;            /* RNG seed; 0 = use a default */
    /* Hard cap on in-memory build size, in bytes.  0 = unlimited. */
    size_t        max_build_memory;
} vec_graph_params_t;

/* Sensible defaults for general use. */
#define VEC_GRAPH_PARAMS_DEFAULT { \
    .dim = 0,                     \
    .M = 64,                      \
    .ef_construction = 128,       \
    .alpha = 1.2f,                \
    .metric = VEC_METRIC_DOT,     \
    .seed = 0,                    \
    .max_build_memory = 0,        \
}

typedef struct {
    vec_id_t  id;
    float     score;   /* metric-natural value: dot ↑, L2 ↓.  Caller sorts
                        * appropriately.  Returned arrays are pre-sorted by
                        * "best first" for the metric (descending for dot,
                        * ascending for L2). */
} vec_search_result_t;

/* ----- SYSINDEX.PARAMS handling ------------------------------------- */

/* Parsed parameters from SYSINDEX.PARAMS for a vec index.  Mirrors
 * vec_graph_params_t plus texis-side merge thresholds.
 */
typedef struct {
    /* Backend selector: 0 = HNSW, 1 = IVFPQ.  PARAMS without a backend=
     * key (legacy SYSINDEX rows from before IVFPQ landed) parse as
     * HNSW for back-compat; new CREATE INDEX statements without `WITH
     * backend` get IVFPQ as the default (set by TXvecParamsFromOptions).
     * Defined values live in vecindex_internal.h's anonymous enum
     * (VEC_BACKEND_HNSW / VEC_BACKEND_IVFPQ).  Held as a plain int
     * here so vecindex.h doesn't have to expose internal types to
     * public callers. */
    int                backend;
    vec_graph_params_t graph;     /* dim, M, ef_construction, alpha, metric, ... */
    int                threshold_t;   /* delta-tier insert count → trigger merge */
    int                threshold_d;   /* tombstone count → trigger merge */
    /* Element dtype — an FTN_VEC_* tag indicating how each cell of the
     * indexed column should be interpreted.  For typed varvec columns
     * this matches the column type and is set automatically.  For
     * varbyte columns the user must supply `vec_dtype 'f16'` (etc.) at
     * CREATE INDEX time; without it the create is rejected.
     * Persisted in PARAMS so the open path can pick the right
     * convert_to_f32 routine without consulting the column. */
    int                dtype;
    /* For i8/u8 indexes (and any future quantized type): dequantization
     * parameters such that  value = (q - quant_zp) * quant_scale  and
     * quantization is  q = round(value / quant_scale + quant_zp).
     * Defaults applied when dtype is locked in:
     *   i8 → scale = 1/127, zp = 0   (symmetric, unit-norm friendly)
     *   u8 → scale = 1/127, zp = 128 (asymmetric, unit-norm friendly)
     * Floats leave both fields at 0; they are unused on the float path.
     * Persisted in PARAMS so subsequent INSERTs and queries quantize
     * with the same calibration as the build. */
    float              quant_scale;
    int                quant_zp;
    /* Transient: only meaningful between TXvecParamsFromOptions() and
     * TXvecCreateIndex().  Drives the pre-scan calibration; the result
     * (scale, zp) is what gets persisted in PARAMS.  Not parsed back. */
    int                calibrate_mode;  /* 0=none, 1=auto (asymmetric) */

    /* IVFPQ-specific (only meaningful when backend == VEC_BACKEND_IVFPQ).
     * 0 means "not set" → CREATE-time auto-tune from pq_target_rows.
     * Persisted in PARAMS so subsequent INSERT/SEARCH see the same shape. */
    int                pq_m;            /* number of subquantizers */
    int                pq_nlist;        /* number of inverted lists / coarse centroids */
    int                pq_nbits;        /* bits per PQ code (8 only for v1) */
    int                pq_target_rows;  /* asymptotic-size hint, drives auto-pick */
    /* Minimum training points per centroid.  Default 39 (FAISS's own
     * "no-warning" threshold).  Lower values are accepted by FAISS
     * (with quality warnings); we expose the knob so the regression
     * tests can train an IVFPQ index from a few hundred rows instead
     * of the 9984+ that the default floor demands.  Wired to both
     * idx->cp.min_points_per_centroid and idx->pq.cp.min_points_per_centroid
     * so coarse and PQ k-means use the same threshold. */
    int                pq_min_points_per_centroid;
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
 *
 * Sized to fit the IVFPQ-backend variant comfortably (which has more
 * fields than HNSW): backend=ivfpq;dim=NNNN;dtype=XXXX;metric=XXX;
 * pq_m=NNN;pq_nlist=NNNNN;pq_nbits=N;pq_target_rows=NNNNNNNNN; ≈ 100 B.
 */
#define TX_VEC_PARAMS_TEXT_MAX 384
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

/* Per-process cached index handle.  Opaque to callers; the concrete
 * type is `struct TXvecHandleBase` (defined in vecindex_internal.h) and
 * each backend's handle type embeds that as its first field.  Public
 * callers exchange this opaque pointer through TXvecOpen / TXvecSearch /
 * etc.; only the backend impl modules cast it down to their concrete
 * struct.
 */
typedef struct TXvecHandleBase TXvecHandle;

/* Open (or fetch from cache) an index handle for the given index file.
 * Returns NULL on error.
 *
 * `params` is the SYSINDEX.PARAMS string for this index, if the caller
 * already has it (e.g., predopt.c handing it through from the planner).
 * Pass NULL to have TXvecOpen look it up from SYSINDEX itself —
 * callers without ready access to PARAMS pay one SYSINDEX read per
 * cache miss (zero per cache hit).  The parsed `backend` field of the
 * params determines which backend's open implementation handles the
 * request.
 */
TXvecHandle *TXvecOpen(DDIC *ddic, const char *indfile, const char *params);

/* Search: top-k nearest to `query`, with beam width `ef` (0 = use
 * ef_construction from index params).  `results` must be size k.
 * Returns number of results filled (≤ k), or SIZE_MAX on error.
 */
size_t       TXvecSearch(TXvecHandle *h, DBTBL *dbtbl, const char *field,
                         const float *query, size_t k, size_t ef,
                         vec_search_result_t *results);

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

/* ALTER INDEX OPTIMIZE entry point for INDEX_VEC.  Used by alterIndex.c
 * when the SYSINDEX type is INDEX_VEC.  Both backends produce the new
 * artifacts at a Tnnnn temp basename and atomic-swap them in place;
 * concurrent searches see the old index until the swap completes.
 * `params` may be NULL — dispatcher reads it from SYSINDEX.  `options`
 * carries the connection-scoped knobs (notably `indexmeter`) so the
 * backend can drive a progress meter for long phases. */
int          TXvecOptimize(DDIC *ddic, const char *indname,
                           const char *indfile, const char *tableName,
                           const char *field, const char *params,
                           TXindOpts *options);

/* ALTER INDEX REBUILD entry point for INDEX_VEC.  Both backends rebuild
 * from scratch into a Tnnnn temp basename and atomic-swap; concurrent
 * searches keep hitting the old index until the swap.  `options` is
 * the connection-scoped TXindOpts (drives indexmeter). */
int          TXvecRebuild(DDIC *ddic, const char *indname,
                          const char *indfile, const char *tableName,
                          const char *field, const char *params,
                          TXindOpts *options);

/* Count rows in the `_T.btr` newrec auxiliary btree for an INDEX_VEC.
 * Used by alterIndex.c to evaluate `HAVING COUNT(NewRows) > N` on
 * ALTER INDEX OPTIMIZE / REBUILD, mirroring the fulltext path's
 * predicate-eval at updindex.c.  Returns 0 if the file is missing
 * or unreadable. */
size_t       TXvecCountNewRows(const char *indfile);

/* Inspect a TXindOpts to determine which vec backend would be used
 * for a CREATE INDEX with these options.  Used by createindex (in
 * index.c) to write a refined SYSUPDATE.KIND label at CREATE time.
 * Returns "vec-hnsw" or "vec-ivfpq" (string literal — do not free).
 * Defaults to "vec-ivfpq" if no explicit backend is set. */
const char  *TXvecKindFromOptions(TXindOpts *options);

/* Flush a specific index: save the in-memory state to disk and clear
 * the SYSINDEX dirty bit.  No-op (returns 0) if the cached handle for
 * `indfile` is already clean or no handle exists.  Returns 0 on success,
 * -1 on I/O error.
 */
int          TXvecFlush(DDIC *ddic, const char *indfile);

/* Flush every cached handle in the per-process cache.  Used by the
 * exit hook (clean shutdown).  Under the `_T.btr` design every INSERT
 * is already durable, so this is a near no-op — but it still gives
 * each backend a chance to round-trip any in-memory metadata
 * (e.g. IVFPQ's max_recid_at_create) before exit.  Returns 0 on
 * success, -1 if any handle failed.
 */
int          TXvecFlushAll(DDIC *ddic);

/* Remember `ddic` for later use by the embedder's exit-time flush.
 * Embedders (rampart-sql) wire add_exit_func(handler, NULL) where
 * `handler` calls TXvecFlushAll(TXvecGetExitHookDDIC()).
 */
void         TXvecRegisterExitHook(DDIC *ddic);
DDIC        *TXvecGetExitHookDDIC(void);

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
