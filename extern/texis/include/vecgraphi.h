/* vecgraphi.h — internal header shared between vecgraph.c and vecio.c.
 *
 * Not for public consumption.  Public API is vecgraph.h.  This header is
 * named with the `i` suffix following the btree.h / btreei.h convention.
 */

#ifndef VECGRAPHI_H
#define VECGRAPHI_H

#include "vecgraph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal node index — caps at 4 billion nodes per index, plenty. */
typedef uint32_t vec_idx_t;
#define VEC_IDX_INVALID ((vec_idx_t)-1)

/* External-id → internal-idx hash table.
 * Open addressing, linear probing, power-of-two capacity, load factor ≤ 0.7.
 * No deletes from the table — we tombstone via vec_graph.tombstones[].
 */
typedef struct {
    vec_id_t  *keys;     /* keys[i] == VEC_ID_INVALID means empty slot */
    vec_idx_t *vals;
    size_t     cap;      /* power of two */
    size_t     used;
} vec_id_map_t;

struct vec_graph {
    vec_graph_params_t params;

    size_t       n;            /* number of nodes (live + tombstoned) */
    size_t       cap;          /* slots allocated in vectors/neighbors/etc. */

    float       *vectors;      /* n × dim, row-major */
    vec_idx_t   *neighbors;    /* n × M, packed; degree[i] tells the live len */
    float       *neighbor_dist;/* n × M, parallel to neighbors; cached d(i, neighbor) */
    uint16_t    *degree;       /* n entries; #valid neighbors per node */
    vec_id_t    *external_ids; /* internal_idx → external id */
    uint8_t     *tombstones;   /* (n+7)/8 bytes, bit i = deleted */

    vec_idx_t    medoid;       /* entry point for search */
    vec_id_map_t id_map;       /* external_id → internal_idx */

    /* Visit tracking — generation counter, lazily allocated. */
    uint64_t    *visited_gen;
    uint64_t     cur_gen;

    /* Scratch buffer for greedy_search and robust_prune. */
    void        *scratch;
    size_t       scratch_cap;

    /* Optional SIMD distance kernel.  When non-NULL, vec_dist() in the
     * inner loop calls this instead of the inline scalar fallback.
     * Installed by vec_graph_set_dist_kernel.
     */
    vec_dist_kernel_t dist_kernel;
};

/* Tombstone bitmap helpers (used by vecio for the tombstones block). */
static inline int
vec_is_tombstoned(const struct vec_graph *g, vec_idx_t i)
{
    return (g->tombstones[i >> 3] >> (i & 7)) & 1;
}

static inline void
vec_set_tombstoned(struct vec_graph *g, vec_idx_t i)
{
    g->tombstones[i >> 3] |= (uint8_t)(1u << (i & 7));
}

/* id_map operations — defined in vecgraph.c. */
int       vec_id_map_init(vec_id_map_t *m, size_t hint);
void      vec_id_map_free(vec_id_map_t *m);
int       vec_id_map_put(vec_id_map_t *m, vec_id_t id, vec_idx_t idx);
vec_idx_t vec_id_map_get(const vec_id_map_t *m, vec_id_t id);

#ifdef __cplusplus
}
#endif

#endif  /* VECGRAPHI_H */
