/* vecindex_internal.h — private types shared between vecindex.c (HNSW
 * backend) and vecindex_ivfpq.cpp (IVFPQ backend).
 *
 * Public callers see only the opaque `TXvecHandle` typedef from
 * vecindex.h.  This header is for the two backend translation units
 * that need to know the polymorphic-base shape and the dispatch vtable.
 */

#ifndef VECINDEX_INTERNAL_H
#define VECINDEX_INTERNAL_H

#include <sys/types.h>          /* dev_t, ino_t */
#include "dbquery.h"            /* DDIC, EPI_OFF_T */
#include "vecindex.h"           /* TXvecHandle, TXvecParams, etc. */

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Backend selector --------------------------------------------- */

enum {
    VEC_BACKEND_HNSW  = 0,      /* default; PARAMS without backend= → HNSW */
    VEC_BACKEND_IVFPQ = 1,      /* PARAMS backend=ivfpq */
};

/* ----- Polymorphic handle base -------------------------------------- */

/* Both struct TXvecHnswHandle (vecindex.c) and struct TXvecIvfpqHandle
 * (vecindex_ivfpq.cpp) embed this as their first field at offset 0.
 * The cache walks via base.next; the dispatcher reads base.backend.
 *
 * The public typedef `TXvecHandle` (in vecindex.h) is an alias for
 * `struct TXvecHandleBase`, so public callers exchange opaque pointers.
 */
struct TXvecHandleBase {
    int                       backend;     /* VEC_BACKEND_* */
    struct TXvecHandleBase   *next;        /* per-process handle cache list */
    char                     *path;        /* SYSINDEX path (no extension); cache key */
    DDIC                     *ddic;        /* DDIC observed at last open */
    /* Geometric / metric metadata — meaningful for both backends.
     * dim is the vector dimensionality, metric is the configured
     * distance, dtype is the column-byte interpretation
     * (FTN_VEC_F32 / F16 / BF16 / I8 / U8) used by query-side f32
     * conversion before the engine is consulted.  Cached from PARAMS
     * at open time; stable for the life of the handle. */
    int                       dim;
    vec_metric_t              metric;
    int                       dtype;
    /* File-identity for cross-process staleness detection.  Captured at
     * open time on the *primary* on-disk artifact (HNSW: .vec; IVFPQ:
     * _H.idxpq).  Cache hit re-stats and evicts on mismatch. */
    dev_t                     file_dev;
    ino_t                     file_ino;
    EPI_OFF_T                 file_mtime;
    EPI_OFF_T                 file_size;
};

/* ----- Backend vtable ----------------------------------------------- */

/* All slots take/return `TXvecHandle *` (= struct TXvecHandleBase *)
 * for public-API symmetry; each backend's implementation casts to its
 * own concrete handle type at function entry. */
typedef struct TXvecBackend {
    int          (*create)   (DDIC *ddic, DBTBL *dbtbl,
                              const char *field, const char *indname,
                              const char *indfile, TXindOpts *options,
                              TXvecParams *outParams);
    /* The dispatcher parses PARAMS once and hands the parsed struct
     * to open; this saves the backend from re-parsing and lets the
     * dispatch decision flow naturally from the parsed `backend` field. */
    TXvecHandle *(*open)     (DDIC *ddic, const char *indfile,
                              const TXvecParams *params);
    void         (*close)    (TXvecHandle *h);
    /* dbtbl + field are needed by IVFPQ to fetch newrec rows for the
     * linear-scan delta during search.  HNSW ignores them.  Either may
     * be NULL — backends must tolerate that (degraded operation). */
    size_t       (*search)   (TXvecHandle *h, DBTBL *dbtbl,
                              const char *field, const float *query,
                              size_t k, size_t ef,
                              vec_search_result_t *results);
    /* Per-row hooks operate on an already-opened handle.  The
     * dispatcher (TXvecAddRow / TXvecDelRow) calls TXvecOpen once and
     * passes the result here — saves the impl from re-opening, and
     * lets the backend assume the handle is valid for h->backend. */
    int          (*add_row)  (DDIC *ddic, TXvecHandle *h, DBTBL *dbtbl,
                              const char *field, RECID *recid);
    int          (*del_row)  (DDIC *ddic, TXvecHandle *h, DBTBL *dbtbl,
                              const char *field, RECID *recid);
    int          (*flush)    (DDIC *ddic, TXvecHandle *h);
    void         (*drop_aux) (DDIC *ddic, const char *indfile);
    /* Cache staleness check — each backend stats its own primary
     * on-disk artifact (HNSW: .vec; IVFPQ: _H.idxpq) and compares
     * against the base.file_* fields captured at open time.
     * Returns nonzero if the file has been replaced since open. */
    int          (*is_stale) (TXvecHandle *h);
    /* ALTER INDEX OPTIMIZE.  Both backends produce new artifacts under
     * `tempBase` (a Tnnnn-style basename in the same directory as the
     * live index); the dispatcher does the atomic-swap rename and
     * SYSINDEX bookkeeping.  HNSW: in-RAM `usearch_add` of newrec
     * recids, save_atomic to tempBase.vec, fresh empty Tnnnn_T.btr,
     * non-absorbed tombstones copied to Tnnnn_del.btr.  IVFPQ:
     * byte-copy live _H/_I.idxpq → temp, encode delta into temp
     * invlists, advance max_recid_at_create, fresh empty Tnnnn_T.btr,
     * non-absorbed tombstones copied to Tnnnn_del.btr.
     *
     * On success the backend returns the sorted int64 array of recids
     * absorbed in `*out_absorbed` (caller-frees, may be NULL/0 if no
     * delta was absorbed); the dispatcher uses it during commit to
     * carry forward concurrent INSERTs/DELETEs that arrived against
     * the live aux btrees during the build phase.  Returns 0 on
     * success, -1 on error. */
    int          (*optimize) (DDIC *ddic, TXvecHandle *h,
                              DBTBL *dbtbl, const char *field,
                              const char *tempBase,
                              TXindOpts *options,
                              int64_t **out_absorbed,
                              size_t *out_n_absorbed);
    /* ALTER INDEX REBUILD.  Both backends rebuild from scratch into
     * `tempBase`.  HNSW: walks live table, usearch_add each row,
     * save_atomic to tempBase.vec, empty Tnnnn_T/_del.btr.  IVFPQ:
     * re-trains codebooks from the live table, encodes all rows into
     * tempBase invlists, empty Tnnnn_T/_del.btr.  Caller-supplied `vp`
     * carries the parsed PARAMS.  `out_absorbed` is the set of recids
     * encoded into sealed during the rebuild — concurrent post-walk
     * inserts (recid > rebuild's max-seen) won't be in the set and
     * the dispatcher carries them forward.  Returns 0 on success, -1
     * on error. */
    int          (*rebuild)  (DDIC *ddic, TXvecHandle *h,
                              DBTBL *dbtbl, const char *field,
                              const TXvecParams *vp,
                              const char *tempBase,
                              TXindOpts *options,
                              int64_t **out_absorbed,
                              size_t *out_n_absorbed);
} TXvecBackend;

extern const TXvecBackend TXvecHnswBackend;     /* defined in vecindex.c */
#ifndef RP_NO_FAISS
extern const TXvecBackend TXvecIvfpqBackend;    /* defined in vecindex_ivfpq.cpp */
#endif

/* Pick the right backend by parsed-params tag.  On builds without FAISS
 * (RP_NO_FAISS — 32-bit ARM) the IVFPQ vtable doesn't exist; callers
 * are expected to gate IVFPQ requests with a clear error before
 * dispatching, but we fall back to HNSW here as a safety net.
 * Unknown values fall back to HNSW (back-compat for PARAMS strings
 * predating this change). */
static inline const TXvecBackend *
vec_backend_for(int backend_id)
{
#ifdef RP_NO_FAISS
    (void)backend_id;
    return &TXvecHnswBackend;
#else
    return (backend_id == VEC_BACKEND_IVFPQ)
            ? &TXvecIvfpqBackend
            : &TXvecHnswBackend;
#endif
}

/* One-time process init that pins OpenBLAS to 1 thread on Linux/FreeBSD.
 * Idempotent; safe to call from any backend's create/open path.  See
 * vecindex_ivfpq.cpp for rationale.  Defined there. */
void TXvecFaissInitOnce(void);

/* Helpers shared between vecindex.c (HNSW) and vecindex_ivfpq.cpp.
 * `vec_convert_to_f32` reads `n_elems` cells of column dtype `t` from
 * `raw` and writes `dim` floats to `dst`.  For i8/u8 sources, `scale`
 * and `zp` apply (value = (q - zp) * scale).  Returns 0 on success,
 * -1 on dtype mismatch / unsupported. */
int  vec_convert_to_f32(int t, const void *raw, size_t n_elems, int dim,
                        float scale, int zp, float *dst);
size_t vec_dtype_elsz(int dtype);

/* Auto-pick IVFPQ params (nlist, M, nbits) from row count + target hint
 * for any field the user didn't supply explicitly.  See plan §4.
 * Defined in vecindex.c so the heuristic is shared. */
void vec_params_apply_pq_defaults(TXvecParams *p, int dim, size_t current_rows);

/* Once `p->dtype` is locked in, fill in default quantization parameters
 * for i8/u8 if the caller didn't supply them.  HNSW + IVFPQ both need
 * this for column-level quantization on varbyte/byte columns. */
void vec_params_apply_quant_defaults(TXvecParams *p);

/* Splice a freshly-opened handle into the per-process cache list.
 * Cache list lives in vecindex.c (private static); the helper is the
 * only way for vecindex_ivfpq.cpp to insert into it. */
void vec_handle_cache_push(struct TXvecHandleBase *hb);

/* ----- Auxiliary-btree helpers (vecindex_aux_btree.c) -------------------
 *
 * Both the HNSW and IVFPQ backends keep two auxiliary btrees alongside
 * their main index file: `<base>_T.btr` (newrec — recids inserted
 * post-CREATE that haven't been folded into the main index yet) and
 * `<base>_del.btr` (tombstone — recids whose main-index entry should
 * be filtered out at SEARCH).  All access is open-and-close-per-op;
 * no cached BTREE* on the handle (which would let btree handles
 * outlive the connection-scoped dbf and produce use-after-close on
 * cross-process workloads).  See texis-internals.md §8.5 for the
 * fulltext analogue.
 *
 * `base_path` is the file stem WITHOUT the `.btr` suffix; the btree
 * subsystem appends `.btr` via openbtree's auto-suffixing.  Construct
 * via `TXvecMakeBtreeBasePath(indfile, "_T")` etc. */

typedef void (*TXvecRecidCallback)(int64_t recid_off, void *user);

char *TXvecMakeBtreeBasePath(const char *indfile, const char *suffix);
int   TXvecBtreeCreateEmpty (const char *base_path);
int   TXvecBtreeInsertRecid (const char *base_path, int64_t recid_off);
int   TXvecBtreeDeleteRecid (const char *base_path, int64_t recid_off);
void  TXvecBtreeWalkRecids  (const char *base_path,
                             TXvecRecidCallback cb, void *user);
int   TXvecBtreeTruncate    (const char *base_path);
void  TXvecBtreeUnlink      (const char *base_path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  /* VECINDEX_INTERNAL_H */
