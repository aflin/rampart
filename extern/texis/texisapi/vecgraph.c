/* vecgraph.c — Vamana ANN graph implementation.
 *
 * Pure C, no texis dependencies.  Exposed via vecgraph.h.
 *
 * Distance computations dispatch through `g->dist_kernel` when one has
 * been installed via vec_graph_set_dist_kernel (e.g. a simsimd punned
 * kernel from the texis embedder).  Without a kernel the fallback is
 * an inline scalar that the compiler auto-vectorizes; that path is
 * what the standalone vecgraph-test uses.
 *
 * Algorithm reference: Subramanya et al., "DiskANN" (NeurIPS 2019).
 * Implemented from the paper; see vector-index-design.md §7.
 */

#include "vecgraph.h"
#include "vecgraphi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

/* ----- Constants ----------------------------------------------------- */

#define VEC_DEFAULT_SEED       0xC0FFEEU
#define VEC_DEFAULT_M          64
#define VEC_DEFAULT_EFC        128
#define VEC_DEFAULT_ALPHA      1.2f
#define VEC_DEFAULT_METRIC     VEC_METRIC_DOT

/* struct vec_graph and vec_id_map_t are defined in vecgraphi.h so vecio.c
 * can read/write them.  Local helpers below.
 */

/* Tombstone helpers — alias the inline helpers from the internal header
 * so the body of this file reads the same as before.
 */
#define is_tombstoned(g, i)  vec_is_tombstoned((g), (i))
#define set_tombstoned(g, i) vec_set_tombstoned((g), (i))

/* ----- Distance functions -------------------------------------------- */

/* Simple inline implementations; the compiler auto-vectorizes them
 * reasonably well at -O2.  A later revision can wire in simsimd's
 * punned kernels via rp_vector_distance from vector-distance.c.
 */

static inline float
vec_dot(const float *a, const float *b, int dim)
{
    float s = 0.0f;
    int i;
    for (i = 0; i < dim; i++)
        s += a[i] * b[i];
    return s;
}

static inline float
vec_l2sq(const float *a, const float *b, int dim)
{
    float s = 0.0f;
    int i;
    for (i = 0; i < dim; i++) {
        float d = a[i] - b[i];
        s += d * d;
    }
    return s;
}

/* "score" in the sense of vec_search_result_t: metric-natural value.
 * For dot, higher is better; for L2sq, lower is better.
 */
static inline float
vec_score(const float *a, const float *b, int dim, vec_metric_t metric)
{
    return (metric == VEC_METRIC_L2) ? vec_l2sq(a, b, dim)
                                      : vec_dot(a, b, dim);
}

/* Graph-aware variants used in the inner loops.  When `g->dist_kernel'
 * is non-NULL the SIMD kernel does the work; the inline scalar path
 * stays as a fallback for the standalone test build (which links
 * vecgraph.c without simsimd) and for graphs where a kernel hasn't
 * been installed.
 */
static inline float
vec_score_g(const vec_graph_t *g, const float *a, const float *b)
{
    int dim = g->params.dim;
    if (g->dist_kernel) {
        double d;
        g->dist_kernel(a, b, (size_t)dim, &d);
        return (float)d;
    }
    return vec_score(a, b, dim, g->params.metric);
}

/* "is `a` better than `b` for this metric?"  True if a should sort before b
 * in best-first order.
 */
static inline int
vec_score_better(float a, float b, vec_metric_t metric)
{
    return (metric == VEC_METRIC_L2) ? (a < b) : (a > b);
}

/* Worst-possible score for the metric (sentinel for "no result yet"). */
static inline float
vec_score_worst(vec_metric_t metric)
{
    return (metric == VEC_METRIC_L2) ? INFINITY : -INFINITY;
}

/* ----- id_map: external_id → internal_idx ---------------------------- */
/* Externally visible (declared in vecgraphi.h) so vecio.c can use them. */

int
vec_id_map_init(vec_id_map_t *m, size_t hint)
{
    size_t cap = 16;
    while (cap < hint * 2) cap <<= 1;   /* 50% load factor at hint */
    m->cap = cap;
    m->used = 0;
    m->keys = (vec_id_t *)malloc(cap * sizeof(vec_id_t));
    m->vals = (vec_idx_t *)malloc(cap * sizeof(vec_idx_t));
    if (!m->keys || !m->vals) {
        free(m->keys); free(m->vals);
        m->keys = NULL; m->vals = NULL;
        return -1;
    }
    /* Mark all slots empty. */
    for (size_t i = 0; i < cap; i++) m->keys[i] = VEC_ID_INVALID;
    return 0;
}

void
vec_id_map_free(vec_id_map_t *m)
{
    free(m->keys); free(m->vals);
    m->keys = NULL; m->vals = NULL;
    m->cap = m->used = 0;
}

/* xorshift64-style hash; well-spread for our use. */
static inline uint64_t
id_hash(vec_id_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

vec_idx_t
vec_id_map_get(const vec_id_map_t *m, vec_id_t id)
{
    if (m->cap == 0) return VEC_IDX_INVALID;
    size_t mask = m->cap - 1;
    size_t i = id_hash(id) & mask;
    while (m->keys[i] != VEC_ID_INVALID) {
        if (m->keys[i] == id) return m->vals[i];
        i = (i + 1) & mask;
    }
    return VEC_IDX_INVALID;
}

static int vec_id_map_grow(vec_id_map_t *m, size_t new_cap);

/* Insert id → idx.  Returns 0 on success, -1 on alloc failure, 1 if id
 * already present (the existing mapping is preserved).
 */
int
vec_id_map_put(vec_id_map_t *m, vec_id_t id, vec_idx_t idx)
{
    if (m->cap == 0 || m->used * 10 >= m->cap * 7) {
        if (vec_id_map_grow(m, m->cap ? m->cap * 2 : 16) < 0) return -1;
    }
    size_t mask = m->cap - 1;
    size_t i = id_hash(id) & mask;
    while (m->keys[i] != VEC_ID_INVALID) {
        if (m->keys[i] == id) return 1;   /* duplicate */
        i = (i + 1) & mask;
    }
    m->keys[i] = id;
    m->vals[i] = idx;
    m->used++;
    return 0;
}

static int
vec_id_map_grow(vec_id_map_t *m, size_t new_cap)
{
    vec_id_t  *old_keys = m->keys;
    vec_idx_t *old_vals = m->vals;
    size_t     old_cap  = m->cap;

    vec_id_t  *new_keys = (vec_id_t *)malloc(new_cap * sizeof(vec_id_t));
    vec_idx_t *new_vals = (vec_idx_t *)malloc(new_cap * sizeof(vec_idx_t));
    if (!new_keys || !new_vals) { free(new_keys); free(new_vals); return -1; }
    for (size_t i = 0; i < new_cap; i++) new_keys[i] = VEC_ID_INVALID;

    m->keys = new_keys;
    m->vals = new_vals;
    m->cap  = new_cap;
    m->used = 0;

    for (size_t i = 0; i < old_cap; i++) {
        if (old_keys[i] != VEC_ID_INVALID)
            vec_id_map_put(m, old_keys[i], old_vals[i]);
    }

    free(old_keys);
    free(old_vals);
    return 0;
}

/* ----- Allocation / free --------------------------------------------- */

/* Apply defaults to any zero/unset fields. */
static void
fill_defaults(vec_graph_params_t *p)
{
    if (p->M == 0)               p->M = VEC_DEFAULT_M;
    if (p->ef_construction == 0) p->ef_construction = VEC_DEFAULT_EFC;
    if (p->alpha == 0.0f)        p->alpha = VEC_DEFAULT_ALPHA;
    if (p->metric == 0)          p->metric = VEC_DEFAULT_METRIC;
    if (p->seed == 0)            p->seed = VEC_DEFAULT_SEED;
}

/* Allocate a graph capable of holding `cap` nodes with the given params. */
static vec_graph_t *
graph_alloc(const vec_graph_params_t *params, size_t cap)
{
    vec_graph_t *g = (vec_graph_t *)calloc(1, sizeof(*g));
    if (!g) return NULL;

    g->params = *params;
    g->n = 0;
    g->cap = cap;
    g->medoid = VEC_IDX_INVALID;

    if (cap == 0) return g;   /* allowed; build will populate */

    int dim = params->dim;
    int M   = params->M;

    g->vectors       = (float *)    malloc((size_t)cap * dim * sizeof(float));
    g->neighbors     = (vec_idx_t *)malloc((size_t)cap * M   * sizeof(vec_idx_t));
    g->neighbor_dist = (float *)    malloc((size_t)cap * M   * sizeof(float));
    g->degree        = (uint16_t *) calloc(cap, sizeof(uint16_t));
    g->external_ids  = (vec_id_t *) malloc((size_t)cap * sizeof(vec_id_t));
    g->tombstones    = (uint8_t *)  calloc((cap + 7) / 8, sizeof(uint8_t));

    if (!g->vectors || !g->neighbors || !g->neighbor_dist || !g->degree ||
        !g->external_ids || !g->tombstones) {
        vec_graph_free(g);
        return NULL;
    }

    if (vec_id_map_init(&g->id_map, cap) < 0) {
        vec_graph_free(g);
        return NULL;
    }
    return g;
}

void
vec_graph_free(vec_graph_t *g)
{
    if (!g) return;
    free(g->vectors);
    free(g->neighbors);
    free(g->neighbor_dist);
    free(g->degree);
    free(g->external_ids);
    free(g->tombstones);
    free(g->visited_gen);
    free(g->scratch);
    vec_id_map_free(&g->id_map);
    free(g);
}

/* ----- Internal: distance for graph operations (lower-is-better) ----- */

/* Vamana's RobustPrune compares pairwise distances and uses an alpha
 * multiplier; the abstraction needs a "lower is better" distance.
 * For dot/cosine on normalized vectors, distance := 1 - dot ∈ [0, 2].
 * For L2, distance := L2sq.
 * We always return non-negative; FP noise is clamped at 0.
 */
static inline float
vec_dist(const float *a, const float *b, int dim, vec_metric_t metric)
{
    if (metric == VEC_METRIC_L2)
        return vec_l2sq(a, b, dim);
    float d = 1.0f - vec_dot(a, b, dim);
    return d < 0.0f ? 0.0f : d;
}

/* Graph-aware variant — uses the installed SIMD kernel when present.
 * Returns lower-is-better in both metric branches, matching vec_dist().
 */
static inline float
vec_dist_g(const vec_graph_t *g, const float *a, const float *b)
{
    int dim = g->params.dim;
    if (g->dist_kernel) {
        double raw;
        g->dist_kernel(a, b, (size_t)dim, &raw);
        if (g->params.metric == VEC_METRIC_L2) {
            float d = (float)raw;
            return d < 0.0f ? 0.0f : d;
        }
        float d = 1.0f - (float)raw;
        return d < 0.0f ? 0.0f : d;
    }
    return vec_dist(a, b, dim, g->params.metric);
}

/* ----- Internal: candidate set --------------------------------------- */

typedef struct {
    vec_idx_t idx;
    float     dist;        /* always lower-is-better */
    uint8_t   expanded;    /* 1 if greedy_search has visited this node's neighbors */
    uint8_t   _pad[3];     /* explicit padding for predictable layout */
} vec_cand_t;

/* Insert into sorted-ascending array, capped at `cap`.  Returns 1 if
 * inserted, 0 if dropped (full and not better than worst).
 * Linear shift for insertion; cap is small (≤ ef + M).
 */
static int
cand_insert_sorted(vec_cand_t *arr, size_t *size, size_t cap, vec_cand_t v)
{
    if (*size == cap && v.dist >= arr[cap - 1].dist)
        return 0;
    size_t lo = 0, hi = *size;
    while (lo < hi) {
        size_t mid = (lo + hi) >> 1;
        if (arr[mid].dist < v.dist) lo = mid + 1; else hi = mid;
    }
    /* If full, drop the last; otherwise grow. */
    size_t end = (*size < cap) ? *size : cap - 1;
    for (size_t i = end; i > lo; i--) arr[i] = arr[i - 1];
    arr[lo] = v;
    if (*size < cap) (*size)++;
    return 1;
}

/* ----- Internal: visit-generation tracking --------------------------- */

static int
ensure_visited_buf(vec_graph_t *g)
{
    if (g->visited_gen == NULL) {
        g->visited_gen = (uint64_t *)calloc(g->cap, sizeof(uint64_t));
        if (!g->visited_gen) return -1;
        g->cur_gen = 0;
    }
    return 0;
}

static inline int
is_visited(const vec_graph_t *g, vec_idx_t i)
{
    return g->visited_gen[i] == g->cur_gen;
}

static inline void
mark_visited(vec_graph_t *g, vec_idx_t i)
{
    g->visited_gen[i] = g->cur_gen;
}

static inline void
new_visit_generation(vec_graph_t *g)
{
    g->cur_gen++;
    if (g->cur_gen == 0) {
        memset(g->visited_gen, 0, g->cap * sizeof(uint64_t));
        g->cur_gen = 1;
    }
}

/* Scratch buffer of `vec_cand_t` for ef_construction + M candidates. */
static vec_cand_t *
get_scratch_cands(vec_graph_t *g)
{
    size_t need = (size_t)(g->params.ef_construction + g->params.M)
                * sizeof(vec_cand_t);
    if (g->scratch_cap < need) {
        free(g->scratch);
        g->scratch = malloc(need);
        if (!g->scratch) { g->scratch_cap = 0; return NULL; }
        g->scratch_cap = need;
    }
    return (vec_cand_t *)g->scratch;
}

/* ----- Greedy beam search ------------------------------------------- */

/* Standard Vamana greedy_search.  Starts at `entry`, expands neighbors of
 * the best unvisited candidate until no unvisited candidate is closer
 * than the worst kept (i.e., the `ef`-th best).
 *
 * `out` is filled with up to `ef` candidates sorted ascending by distance
 * (closest first).  Returns the number filled (≤ ef), or SIZE_MAX on
 * allocation failure.
 *
 * Tombstoned and out-of-graph nodes are skipped.
 */
static size_t
greedy_search(vec_graph_t *g,
              const float *query,
              size_t ef,
              vec_idx_t entry,
              vec_cand_t *out)
{
    if (ef == 0 || g->n == 0) return 0;
    if (ensure_visited_buf(g) < 0) return SIZE_MAX;
    new_visit_generation(g);

    int dim = g->params.dim;
    int M = g->params.M;

    size_t L_size = 0;

    /* Push entry (or first live node if entry is tombstoned). */
    vec_idx_t start = entry;
    if (start >= g->n || is_tombstoned(g, start)) {
        start = VEC_IDX_INVALID;
        for (size_t i = 0; i < g->n; i++) {
            if (!is_tombstoned(g, (vec_idx_t)i)) { start = (vec_idx_t)i; break; }
        }
        if (start == VEC_IDX_INVALID) return 0;
    }
    {
        vec_cand_t e = {
            .idx = start,
            .dist = vec_dist_g(g, query, g->vectors + (size_t)start * dim),
            .expanded = 0,
        };
        cand_insert_sorted(out, &L_size, ef, e);
        mark_visited(g, start);   /* "in candidate set"; expansion is separate */
    }

    /* Main loop: expand best unexpanded.  "expanded" lives on the candidate
     * struct (the per-graph `visited` bitmap is for "already in candidate
     * set, don't re-push" — a different concept).  Since `out` is sorted
     * ascending by distance, the first un-expanded candidate is also the
     * closest un-expanded candidate.
     */
    while (1) {
        size_t p_pos = SIZE_MAX;
        for (size_t i = 0; i < L_size; i++) {
            if (!out[i].expanded) { p_pos = i; break; }
        }
        if (p_pos == SIZE_MAX) break;

        vec_idx_t p = out[p_pos].idx;
        out[p_pos].expanded = 1;

        const vec_idx_t *nbrs = g->neighbors + (size_t)p * M;
        int deg = (int)g->degree[p];
        /* Prefetch the first neighbor's vector before entering the loop;
         * inside the loop we prefetch one ahead of the current iteration
         * so the random-indexed load is in L1 by the time vec_dist_g
         * needs it.  Big help on hosts where the SIMD distance kernel is
         * faster than an L2 miss.
         */
        if (deg > 0)
            __builtin_prefetch(g->vectors + (size_t)nbrs[0] * dim);
        for (int j = 0; j < deg; j++) {
            vec_idx_t q = nbrs[j];
            if (j + 1 < deg)
                __builtin_prefetch(g->vectors + (size_t)nbrs[j+1] * dim);
            if (is_visited(g, q) || is_tombstoned(g, q)) continue;
            mark_visited(g, q);    /* prevent re-push within this search */
            float d = vec_dist_g(g, query, g->vectors + (size_t)q * dim);
            vec_cand_t c = { .idx = q, .dist = d, .expanded = 0 };
            cand_insert_sorted(out, &L_size, ef, c);
        }
    }

    return L_size;
}

/* ----- RobustPrune --------------------------------------------------- */

/* Select up to M neighbors for node `p` from a candidate list.
 *
 * Algorithm (Vamana, with α-relaxation):
 *   1. Sort `cand` by distance to p, ascending.
 *   2. Repeatedly: take the closest remaining candidate as a new neighbor,
 *      then drop any later candidate `c` for which dist(c, neighbor) ≤
 *      α · dist(c, p).  This eliminates "occluded" candidates whose
 *      direct edge would be redundant via the new neighbor.
 *
 * `cand[0..cand_n)` is the input candidate list with distances to p.
 *   - Mutated in place (sorted, then compacted as candidates are dropped).
 *   - The candidate list may include p itself; we skip it.
 *   - Tombstoned candidates are skipped.
 *
 * `out[0..returned-1]` receives the selected neighbors.  Caller-provided,
 * must be sized at least M.
 *
 * Returns the number of neighbors selected (≤ M).
 */
static int
robust_prune(vec_graph_t *g, vec_idx_t p,
             vec_cand_t *cand, size_t cand_n,
             vec_idx_t *out, float *out_dists)
{
    int M = g->params.M;
    float alpha = g->params.alpha;
    int dim = g->params.dim;

    /* Sort by distance ascending.  cand_n is small; insertion sort. */
    for (size_t i = 1; i < cand_n; i++) {
        vec_cand_t v = cand[i];
        size_t j = i;
        while (j > 0 && cand[j - 1].dist > v.dist) {
            cand[j] = cand[j - 1];
            j--;
        }
        cand[j] = v;
    }

    int selected = 0;
    size_t i = 0;
    while (i < cand_n && selected < M) {
        if (cand[i].idx == p || is_tombstoned(g, cand[i].idx)) { i++; continue; }
        out[selected] = cand[i].idx;
        if (out_dists) out_dists[selected] = cand[i].dist;
        selected++;
        const float *p_star_vec = g->vectors + (size_t)cand[i].idx * dim;

        /* Filter cand[i+1..cand_n): keep only those NOT occluded by p_star.
         * Compact in place.
         */
        size_t r = i + 1;
        size_t w = i + 1;
        if (r < cand_n)
            __builtin_prefetch(g->vectors + (size_t)cand[r].idx * dim);
        while (r < cand_n) {
            if (r + 1 < cand_n)
                __builtin_prefetch(g->vectors + (size_t)cand[r+1].idx * dim);
            if (cand[r].idx == p || is_tombstoned(g, cand[r].idx)) { r++; continue; }
            float d_to_pstar = vec_dist_g(g, p_star_vec,
                g->vectors + (size_t)cand[r].idx * dim);
            /* DiskANN Algorithm 2: drop c if α·d(p*, c) ≤ d(p, c).
             * Equivalently: keep if α·d(p*, c) > d(p, c).
             * cand[r].dist holds d(p, c).
             */
            if (alpha * d_to_pstar > cand[r].dist)
                cand[w++] = cand[r];
            r++;
        }
        cand_n = w;
        i++;
    }

    return selected;
}

/* ----- Medoid computation -------------------------------------------- */

/* Find the index whose vector is closest to the centroid of all vectors.
 * Used as the search entry point.  Approximate is fine.  O(n × dim).
 */
static vec_idx_t
compute_medoid(vec_graph_t *g)
{
    int dim = g->params.dim;
    size_t n = g->n;

    /* Compute centroid. */
    double *centroid = (double *)calloc(dim, sizeof(double));
    if (!centroid) return 0;   /* fall back to node 0 */
    for (size_t i = 0; i < n; i++) {
        const float *v = g->vectors + i * dim;
        for (int d = 0; d < dim; d++) centroid[d] += v[d];
    }
    float *cf = (float *)malloc(dim * sizeof(float));
    if (!cf) { free(centroid); return 0; }
    for (int d = 0; d < dim; d++) cf[d] = (float)(centroid[d] / (double)n);
    free(centroid);

    /* Find closest vector to centroid. */
    vec_idx_t best = 0;
    float best_dist = vec_dist_g(g, cf, g->vectors);
    for (size_t i = 1; i < n; i++) {
        float d = vec_dist_g(g, cf, g->vectors + i * dim);
        if (d < best_dist) { best_dist = d; best = (vec_idx_t)i; }
    }
    free(cf);
    return best;
}

/* ----- Random k-regular graph initialization ------------------------- */

/* xorshift32 RNG (simple, fast, OK for graph init). */
static inline uint32_t
xs32_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* Initialize each node with M random neighbors (excluding self).  This
 * gives the build loop a graph to start refining.
 */
static void
init_random_graph(vec_graph_t *g)
{
    int M = g->params.M;
    size_t n = g->n;
    uint32_t rng = g->params.seed;

    if (n <= 1) {
        for (size_t i = 0; i < n; i++) g->degree[i] = 0;
        return;
    }

    int max_neighbors = (M < (int)(n - 1)) ? M : (int)(n - 1);
    int dim = g->params.dim;

    for (size_t i = 0; i < n; i++) {
        vec_idx_t *nbrs = g->neighbors + i * M;
        float     *dists = g->neighbor_dist + i * M;
        const float *qv = g->vectors + i * dim;
        int filled = 0;
        /* Reservoir-style sampling without replacement: cheap-and-dirty
         * via random probes with rejection.  Good enough for init.
         */
        int attempts = 0;
        while (filled < max_neighbors && attempts < max_neighbors * 4) {
            vec_idx_t cand = (vec_idx_t)(xs32_next(&rng) % n);
            if (cand == (vec_idx_t)i) { attempts++; continue; }
            int dup = 0;
            for (int j = 0; j < filled; j++)
                if (nbrs[j] == cand) { dup = 1; break; }
            if (dup) { attempts++; continue; }
            nbrs[filled] = cand;
            dists[filled] = vec_dist_g(g, qv, g->vectors + (size_t)cand * dim);
            filled++;
            attempts++;
        }
        g->degree[i] = (uint16_t)filled;
    }
}

/* ----- Build pass (vamana_build_pass) -------------------------------- */

/* One refinement pass: for each node, run greedy_search to find candidates,
 * RobustPrune to pick M neighbors, and add reverse-edges to update affected
 * neighborhoods.  Returns 0 on success, -1 on allocation failure.
 *
 * `pass_alpha` overrides the params alpha for this pass — Vamana's two-pass
 * build uses alpha=1.0 for pass 1 and the configured alpha for pass 2.
 */
static int
build_pass(vec_graph_t *g, float pass_alpha, int pass_ef)
{
    int M = g->params.M;
    int efc = pass_ef;
    int dim = g->params.dim;
    float saved_alpha = g->params.alpha;
    g->params.alpha = pass_alpha;

    vec_cand_t *scratch = get_scratch_cands(g);
    if (!scratch) { g->params.alpha = saved_alpha; return -1; }

    /* Two pairs of buffers: i's pruned neighbors stay in new_nbrs/new_dists
     * for the duration of the outer iteration (we read them in the reverse-
     * edge loop); u's re-prune output goes into u_new_nbrs/u_new_dists so it
     * doesn't clobber i's list mid-iteration.  (The pre-cache version of
     * this code reused a single buffer and silently corrupted new_nbrs[j+1]
     * after the first reverse-edge overflow.)
     */
    vec_idx_t *new_nbrs    = (vec_idx_t *)malloc((size_t)M * sizeof(vec_idx_t));
    float     *new_dists   = (float *)    malloc((size_t)M * sizeof(float));
    vec_idx_t *u_new_nbrs  = (vec_idx_t *)malloc((size_t)M * sizeof(vec_idx_t));
    float     *u_new_dists = (float *)    malloc((size_t)M * sizeof(float));
    if (!new_nbrs || !new_dists || !u_new_nbrs || !u_new_dists) {
        free(new_nbrs); free(new_dists);
        free(u_new_nbrs); free(u_new_dists);
        g->params.alpha = saved_alpha;
        return -1;
    }

    size_t n = g->n;
    for (size_t i = 0; i < n; i++) {
        if (is_tombstoned(g, (vec_idx_t)i)) continue;

        const float *qv = g->vectors + i * dim;

        /* greedy_search from medoid finds candidate neighbors. */
        size_t L_size = greedy_search(g, qv, (size_t)efc, g->medoid, scratch);
        if (L_size == SIZE_MAX) {
            free(new_nbrs); free(new_dists);
            g->params.alpha = saved_alpha;
            return -1;
        }

        /* Add the existing neighbors of i to the candidate list, with
         * distances to i (so RobustPrune can re-evaluate them).  Read
         * cached distances to avoid recomputing.
         */
        size_t cand_n = L_size;
        const vec_idx_t *old_nbrs  = g->neighbors    + i * M;
        const float     *old_dists = g->neighbor_dist + i * M;
        int old_deg = g->degree[i];
        size_t cap = g->scratch_cap / sizeof(vec_cand_t);
        for (int j = 0; j < old_deg && cand_n < cap; j++) {
            vec_idx_t q = old_nbrs[j];
            if (is_tombstoned(g, q)) continue;
            /* Skip if already in scratch. */
            int dup = 0;
            for (size_t k = 0; k < L_size; k++)
                if (scratch[k].idx == q) { dup = 1; break; }
            if (dup) continue;
            scratch[cand_n].idx = q;
            scratch[cand_n].dist = old_dists[j];
            cand_n++;
        }

        /* Prune to M neighbors. */
        int new_deg = robust_prune(g, (vec_idx_t)i, scratch, cand_n,
                                   new_nbrs, new_dists);
        memcpy(g->neighbors    + i * M, new_nbrs,  new_deg * sizeof(vec_idx_t));
        memcpy(g->neighbor_dist + i * M, new_dists, new_deg * sizeof(float));
        g->degree[i] = (uint16_t)new_deg;

        /* Reverse-edge updates: for each new neighbor u, add `i` to u's
         * neighborhood.  If u's degree exceeds M, re-prune u.  We have
         * the distance from i to u in `new_dists[j]`; the metric is
         * symmetric so that's also d(u, i).
         */
        for (int j = 0; j < new_deg; j++) {
            vec_idx_t u = new_nbrs[j];
            float     d_iu = new_dists[j];
            vec_idx_t *u_nbrs  = g->neighbors    + (size_t)u * M;
            float     *u_dists = g->neighbor_dist + (size_t)u * M;
            int u_deg = g->degree[u];

            /* Already an edge u→i?  Skip. */
            int has_edge = 0;
            for (int k = 0; k < u_deg; k++)
                if (u_nbrs[k] == (vec_idx_t)i) { has_edge = 1; break; }
            if (has_edge) continue;

            if (u_deg < M) {
                u_nbrs[u_deg] = (vec_idx_t)i;
                u_dists[u_deg] = d_iu;
                g->degree[u]++;
            } else {
                /* Re-prune u's neighborhood with i added as a candidate.
                 * All distances u→u_nbrs[k] are cached from the last
                 * time u was pruned; just read them.
                 */
                size_t u_cand_n = 0;
                if (u_cand_n + (size_t)u_deg + 1 > cap) {
                    /* Insufficient scratch: skip the reverse edge.
                     * Rare; only at extreme M and tiny ef_construction.
                     */
                    continue;
                }
                for (int k = 0; k < u_deg; k++) {
                    if (is_tombstoned(g, u_nbrs[k])) continue;
                    scratch[u_cand_n].idx = u_nbrs[k];
                    scratch[u_cand_n].dist = u_dists[k];
                    u_cand_n++;
                }
                scratch[u_cand_n].idx = (vec_idx_t)i;
                scratch[u_cand_n].dist = d_iu;
                u_cand_n++;
                int u_new_deg = robust_prune(g, u, scratch, u_cand_n,
                                             u_new_nbrs, u_new_dists);
                memcpy(u_nbrs,  u_new_nbrs,  u_new_deg * sizeof(vec_idx_t));
                memcpy(u_dists, u_new_dists, u_new_deg * sizeof(float));
                g->degree[u] = (uint16_t)u_new_deg;
            }
        }
    }

    free(new_nbrs);
    free(new_dists);
    free(u_new_nbrs);
    free(u_new_dists);
    g->params.alpha = saved_alpha;
    return 0;
}

/* ----- Public build entry point ------------------------------------- */

vec_graph_t *
vec_graph_build(const float *vectors,
                const vec_id_t *ids,
                size_t n,
                const vec_graph_params_t *params_in,
                vec_dist_kernel_t kernel)
{
    static const char fn[] = "vec_graph_build";

    if (!vectors || !ids || !params_in) {
        fprintf(stderr, "%s: NULL argument\n", fn);
        return NULL;
    }
    if (n == 0) {
        fprintf(stderr, "%s: n == 0\n", fn);
        return NULL;
    }

    vec_graph_params_t params = *params_in;
    if (params.dim <= 0) {
        fprintf(stderr, "%s: dim must be > 0 (got %d)\n", fn, params.dim);
        return NULL;
    }
    fill_defaults(&params);

    /* Memory cap check: vectors + adjacency dominates. */
    if (params.max_build_memory > 0) {
        size_t need = n * params.dim * sizeof(float)
                    + n * params.M * sizeof(vec_idx_t);
        if (need > params.max_build_memory) {
            fprintf(stderr,
                "%s: projected build size %zu MB exceeds cap %zu MB; "
                "raise max_build_memory or use a streaming build\n",
                fn, need >> 20, params.max_build_memory >> 20);
            return NULL;
        }
    }

    vec_graph_t *g = graph_alloc(&params, n);
    if (!g) {
        fprintf(stderr, "%s: allocation failed for n=%zu dim=%d\n",
                fn, n, params.dim);
        return NULL;
    }
    g->dist_kernel = kernel;

    /* Copy vectors and ids, populate id_map.  Catch duplicate ids. */
    memcpy(g->vectors, vectors, n * params.dim * sizeof(float));
    for (size_t i = 0; i < n; i++) {
        if (ids[i] == VEC_ID_INVALID) {
            fprintf(stderr, "%s: ids[%zu] == VEC_ID_INVALID\n", fn, i);
            vec_graph_free(g);
            return NULL;
        }
        g->external_ids[i] = ids[i];
        int rc = vec_id_map_put(&g->id_map, ids[i], (vec_idx_t)i);
        if (rc == 1) {
            fprintf(stderr, "%s: duplicate id at ids[%zu] = %llu\n",
                    fn, i, (unsigned long long)ids[i]);
            vec_graph_free(g);
            return NULL;
        }
        if (rc < 0) {
            fprintf(stderr, "%s: vec_id_map_put failed at i=%zu\n", fn, i);
            vec_graph_free(g);
            return NULL;
        }
    }
    g->n = n;

    /* Initialize as a random M-regular graph and compute medoid. */
    init_random_graph(g);
    g->medoid = compute_medoid(g);

    /* Two-pass refinement: pass 1 with alpha=1.0 (basic Vamana), pass 2
     * with the configured alpha (the α-relaxed pass that adds long-range
     * edges).  The paper shows this converges to a good graph in two
     * passes for typical embeddings.
     */
    /* Pass 1 uses a smaller search width than pass 2 — its job is
     * only to seed connectivity for the α-relaxed pass; there's no
     * benefit to running it at full ef.  Half of pass 2's ef (floored
     * at M) preserves recall while cutting pass-1 work roughly in half.
     */
    int pass1_ef = params.ef_construction / 2;
    if (pass1_ef < params.M) pass1_ef = params.M;
    if (build_pass(g, 1.0f, pass1_ef) < 0 ||
        build_pass(g, params.alpha, params.ef_construction) < 0) {
        fprintf(stderr, "%s: build pass failed (allocation)\n", fn);
        vec_graph_free(g);
        return NULL;
    }

    return g;
}

/* ----- Public search entry point ------------------------------------ */

size_t
vec_graph_search(const vec_graph_t *g_const,
                 const float *query,
                 size_t k,
                 size_t ef,
                 vec_search_result_t *results)
{
    if (!g_const || !query || !results || k == 0) return SIZE_MAX;
    /* greedy_search needs to mutate visited_gen and scratch; cast away
     * const.  Search is conceptually read-only on the graph topology
     * but mutates per-search bookkeeping.
     */
    vec_graph_t *g = (vec_graph_t *)g_const;

    if (ef == 0) ef = (size_t)g->params.ef_construction;
    if (ef < k) ef = k;

    /* Reuse scratch buffer; size must be ≥ ef. */
    if (g->scratch_cap < ef * sizeof(vec_cand_t)) {
        free(g->scratch);
        g->scratch = malloc(ef * sizeof(vec_cand_t));
        if (!g->scratch) { g->scratch_cap = 0; return SIZE_MAX; }
        g->scratch_cap = ef * sizeof(vec_cand_t);
    }
    vec_cand_t *cands = (vec_cand_t *)g->scratch;

    size_t L_size = greedy_search(g, query, ef, g->medoid, cands);
    if (L_size == SIZE_MAX) return SIZE_MAX;

    /* Convert internal distances to metric-natural scores. */
    int dim = g->params.dim;
    size_t out = 0;
    for (size_t i = 0; i < L_size && out < k; i++) {
        if (is_tombstoned(g, cands[i].idx)) continue;
        const float *v = g->vectors + (size_t)cands[i].idx * dim;
        results[out].id = g->external_ids[cands[i].idx];
        results[out].score = vec_score_g(g, query, v);
        out++;
    }
    return out;
}

/* ----- Mutation (not yet implemented) ------------------------------- */

int
vec_graph_insert(vec_graph_t *g, const float *vec, vec_id_t id)
{
    (void)g; (void)vec; (void)id;
    fprintf(stderr, "vec_graph_insert: not yet implemented\n");
    return -1;
}

int
vec_graph_delete(vec_graph_t *g, vec_id_t id)
{
    (void)g; (void)id;
    fprintf(stderr, "vec_graph_delete: not yet implemented\n");
    return -1;
}

void
vec_graph_set_dist_kernel(vec_graph_t *g, vec_dist_kernel_t kernel)
{
    if (g) g->dist_kernel = kernel;
}

/* ----- Introspection ------------------------------------------------- */

size_t
vec_graph_size(const vec_graph_t *g)
{
    if (!g) return 0;
    /* Subtract tombstoned nodes. */
    size_t live = g->n;
    for (size_t i = 0; i < g->n; i++)
        if (is_tombstoned(g, (vec_idx_t)i)) live--;
    return live;
}

const vec_graph_params_t *
vec_graph_params(const vec_graph_t *g)
{
    return g ? &g->params : NULL;
}

/* Test-only diagnostic: degree of node by external id, or -1 if not present.
 * Not part of the public API (no vecgraph.h declaration); expected to be
 * referenced only by vecgraph-test.c via `extern`.
 */
int vecgraph_degree_for_test(const vec_graph_t *g, vec_id_t id);
int
vecgraph_degree_for_test(const vec_graph_t *g, vec_id_t id)
{
    if (!g) return -1;
    vec_idx_t i = vec_id_map_get(&g->id_map, id);
    if (i == VEC_IDX_INVALID || i >= g->n) return -1;
    return is_tombstoned(g, i) ? -1 : (int)g->degree[i];
}

/* ----- Brute-force reference (used by tests + small-table fallback) -- */

/* Min-heap keyed by score "worse-ness": for dot, lower scores at the top
 * (so we can pop them); for L2, higher scores at the top.  This lets us
 * keep a fixed-size top-k window during the scan: the slot at the top is
 * the worst element currently kept, and we replace it when we find better.
 *
 * Tiny inline heap rather than a generic priority queue — we only need
 * sift-down; insert is "replace top, sift down."
 */
static inline int
heap_worse(const vec_search_result_t *a, const vec_search_result_t *b,
           vec_metric_t metric)
{
    /* "a is worse than b" iff a sorts later in best-first order. */
    return vec_score_better(b->score, a->score, metric);
}

static void
heap_sift_down(vec_search_result_t *h, size_t k, vec_metric_t metric)
{
    /* Top of the heap is the WORST element kept (so it's the next one to
     * be evicted when something better arrives).  Standard binary heap.
     */
    size_t i = 0;
    while (1) {
        size_t l = 2 * i + 1;
        size_t r = 2 * i + 2;
        size_t worst = i;
        if (l < k && heap_worse(&h[l], &h[worst], metric)) worst = l;
        if (r < k && heap_worse(&h[r], &h[worst], metric)) worst = r;
        if (worst == i) break;
        vec_search_result_t t = h[i]; h[i] = h[worst]; h[worst] = t;
        i = worst;
    }
}

static int
result_cmp_better_first(const void *a, const void *b, vec_metric_t metric)
{
    const vec_search_result_t *ra = (const vec_search_result_t *)a;
    const vec_search_result_t *rb = (const vec_search_result_t *)b;
    if (vec_score_better(ra->score, rb->score, metric)) return -1;
    if (vec_score_better(rb->score, ra->score, metric)) return  1;
    return 0;
}

/* qsort comparators for each metric — qsort doesn't take a context arg. */
static int cmp_dot_desc(const void *a, const void *b)
    { return result_cmp_better_first(a, b, VEC_METRIC_DOT); }
static int cmp_l2_asc(const void *a, const void *b)
    { return result_cmp_better_first(a, b, VEC_METRIC_L2); }

size_t
vec_brute_search(const float *vectors,
                 const vec_id_t *ids,
                 size_t n, int dim,
                 const float *query,
                 size_t k,
                 vec_metric_t metric,
                 vec_search_result_t *results)
{
    if (!vectors || !ids || !query || !results || k == 0 || n == 0 || dim <= 0)
        return SIZE_MAX;

    size_t kept = 0;
    float worst = vec_score_worst(metric);

    for (size_t i = 0; i < n; i++) {
        const float *v = vectors + i * dim;
        float s = vec_score(query, v, dim, metric);

        if (kept < k) {
            results[kept].id    = ids[i];
            results[kept].score = s;
            kept++;
            if (kept == k) {
                /* Heapify: build the heap so results[0] is the worst kept. */
                for (size_t j = k / 2; j > 0; j--)
                    heap_sift_down(results + j - 1, k - (j - 1), metric);
                /* Top of heap is now the worst kept. */
                worst = results[0].score;
            }
        } else if (vec_score_better(s, worst, metric)) {
            results[0].id    = ids[i];
            results[0].score = s;
            heap_sift_down(results, k, metric);
            worst = results[0].score;
        }
    }

    /* Sort best-first for return. */
    qsort(results, kept, sizeof(*results),
          metric == VEC_METRIC_L2 ? cmp_l2_asc : cmp_dot_desc);
    return kept;
}
