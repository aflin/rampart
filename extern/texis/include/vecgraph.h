/* vecgraph.h — Vamana ANN graph: build, search, mutate.
 *
 * Pure C, no texis dependencies.  IDs are opaque 64-bit integers; vector
 * data is flat float32 arrays in row-major (vec_i = data[i*dim..i*dim+dim]).
 * Internally, distances are computed via simsimd's `dot` kernel; vectors are
 * assumed L2-normalized so dot product equals cosine similarity.
 *
 * The algorithm is Vamana from Subramanya et al. (NeurIPS 2019, "DiskANN").
 * Implemented from the paper; see vector-index-design.md §7 for the spec.
 *
 * Threading: not thread-safe.  Caller serializes.
 */

#ifndef VECGRAPH_H
#define VECGRAPH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Types --------------------------------------------------------- */

/* Opaque external id.  In texis usage this is a packed RECID; in standalone
 * tests it's just a row index.  vecgraph treats it as an uninterpreted key.
 */
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
    /* Hard cap on in-memory build size, in bytes.  vec_graph_build() refuses
     * to start if `n*dim*4 + n*M*4` exceeds this.  0 = unlimited.
     * Until a streaming-insert build path lands, this cap is the only knob.
     */
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

/* Opaque graph handle. */
typedef struct vec_graph vec_graph_t;

/* SIMD distance-kernel function pointer.  Signature matches simsimd's
 * dense punned kernels (size_t and double on relevant platforms), so a
 * simsimd kernel pointer can be cast directly to this type without a
 * trampoline.  When set on a graph (via vec_graph_set_dist_kernel),
 * inner-loop distance computations bypass the inline scalar fallback.
 */
typedef void (*vec_dist_kernel_t)(const void *a, const void *b,
                                   size_t dim, double *out);

/* Install (or clear with NULL) a distance kernel on `g`.  The kernel must
 * compute the same metric the graph was built with — caller is responsible
 * for matching it to `vec_graph_params(g)->metric`.
 */
void vec_graph_set_dist_kernel(vec_graph_t *g, vec_dist_kernel_t kernel);

/* ----- Build --------------------------------------------------------- */

/* Build a graph from `n` vectors.  Vectors are passed as a flat row-major
 * array `vectors[n*dim]`; the i-th vector is `vectors[i*dim..i*dim+dim]`.
 * `ids[n]` provides the external id for each vector.  All ids must be
 * distinct and != VEC_ID_INVALID.
 *
 * `params->dim` must be set; other fields fall back to defaults if 0.
 *
 * `kernel` (optional, may be NULL) installs a SIMD distance kernel for the
 * inner loop of build and search.  Pass NULL to use the inline scalar
 * fallback — that's what the standalone test does.
 *
 * Returns a graph handle on success, NULL on error (out of memory, invalid
 * params, duplicate ids).  The caller must free with vec_graph_free().
 *
 * The graph takes its own internal copy of the vector data; the caller may
 * free `vectors` and `ids` after this call returns.
 */
vec_graph_t *vec_graph_build(const float *vectors,
                              const vec_id_t *ids,
                              size_t n,
                              const vec_graph_params_t *params,
                              vec_dist_kernel_t kernel);

/* Free a graph and all associated memory. */
void vec_graph_free(vec_graph_t *g);

/* ----- Search -------------------------------------------------------- */

/* Search the graph for the top-k nearest neighbors of `query`.
 * `query` is a flat dim-element float32 array.
 * `ef` is the beam search width; must be >= k.  Larger ef → higher recall,
 *      slower query.  ef == 0 means "use ef_construction from build params".
 * `results[k]` is filled with the nearest neighbors, sorted best-first
 *      per the graph's metric (descending dot, ascending L2).
 *
 * Returns the number of results actually written (<= k).  Returns SIZE_MAX
 * on error (NULL inputs, etc.).
 */
size_t vec_graph_search(const vec_graph_t *g,
                        const float *query,
                        size_t k,
                        size_t ef,
                        vec_search_result_t *results);

/* ----- Mutation (declarations only; not yet implemented) ------------ */

/* Insert a single vector into an existing graph.  RobustPrune is run on the
 * new node's neighborhood and on each affected reverse neighbor.
 * Returns 0 on success, -1 on error (duplicate id, allocation failure).
 */
int vec_graph_insert(vec_graph_t *g, const float *vec, vec_id_t id);

/* Mark a node as deleted.  The node remains in the graph but is filtered
 * from search results.  Deletes accumulate; periodic compaction (a future
 * merge pass) removes them physically.
 * Returns 0 if found and marked, -1 if id wasn't present.
 */
int vec_graph_delete(vec_graph_t *g, vec_id_t id);

/* ----- Introspection (for tests + diagnostics) ----------------------- */

/* Number of live (non-deleted) nodes. */
size_t vec_graph_size(const vec_graph_t *g);

/* Returns the params used at build time.  The returned pointer is owned by
 * the graph and remains valid until vec_graph_free.
 */
const vec_graph_params_t *vec_graph_params(const vec_graph_t *g);

/* ----- Brute-force baseline (for tests + small-table fallback) ------- */

/* Exact top-k nearest neighbors by exhaustive scan.  Same I/O contract as
 * vec_graph_search but operates on raw vector arrays without building a
 * graph.  O(n * dim) per query.  Used by:
 *  - vecgraph-test.c to compute ground truth for recall@k
 *  - the texis fallback path for tables small enough that ANN build cost
 *    exceeds brute-force query cost
 */
size_t vec_brute_search(const float *vectors,
                        const vec_id_t *ids,
                        size_t n, int dim,
                        const float *query,
                        size_t k,
                        vec_metric_t metric,
                        vec_search_result_t *results);

#ifdef __cplusplus
}
#endif

#endif  /* VECGRAPH_H */
