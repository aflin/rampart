/* vecindex.c — texis engine integration for the ANN vector index.
 *
 * This translation unit owns:
 *   1. The HNSW backend (uses usearch under extern/texis/thirdparty/usearch).
 *   2. The dispatcher for backend-polymorphic public entry points
 *      (TXvecCreateIndex, TXvecOpen, TXvecSearch, etc.).
 *   3. The shared per-process handle cache (polymorphic; stores both
 *      HNSW and IVFPQ handles; backend tag in the base struct).
 *
 * The IVFPQ backend lives in vecindex_ivfpq.cpp.  The two backends share
 * the vtable / base-struct types defined in vecindex_internal.h.
 *
 * Engine code (index.c, predopt.c, idxinfo.c, procupd.c, droptbl.c)
 * calls into the TXvec* functions here when handling INDEX_VEC.
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <math.h>

#include "dbquery.h"
#include "texint.h"
#include "btree.h"
#include "meter.h"

#include "vecindex.h"
#include "vecindex_internal.h" /* TXvecHandleBase, TXvecBackend, vec_backend_for */
#include "vec_blas_probe.h"    /* texis_vec_blas_probe — runtime BLAS check */
#include "sysupdate.h"

#include "usearch.h"           /* usearch C API */


/* Vector dtype conversion helpers — defined in
 * src/duktape/globals/vector-distance.c (linked into the texis engine).
 * Declarations also exist in src/include/rampart.h, but that header
 * isn't on the texis include path; forward-declaring here keeps the
 * dependency local and explicit.
 */
extern void rpvec_f16_to_f32(const uint16_t *src, float *dst, size_t n);
extern void rpvec_bf16_to_f32(const uint16_t *src, float *dst, size_t n);
extern void rpvec_f64_to_f32(const double *src, float *dst, size_t n);
/* Quantized → f32 dequantization (value = (q - zp) * scale) */
extern void rpvec_i8_to_f32(const int8_t *src, float *dst, size_t n,
                            float scale, int zp);
extern void rpvec_u8_to_f32(const uint8_t *src, float *dst, size_t n,
                            float scale, int zp);
/* f32 → quantized quantization (q = round(v / scale + zp), clamped) */
extern void rpvec_f32_to_i8(const float *src, int8_t *dst, size_t n,
                            float scale, int zp);
extern void rpvec_f32_to_u8(const float *src, uint8_t *dst, size_t n,
                            float scale, int zp);
/* f32 → reduced-precision float */
extern void rpvec_f32_to_f16(const float *src, uint16_t *dst, size_t n);
extern void rpvec_f32_to_bf16(const float *src, uint16_t *dst, size_t n);

/* Suffix used for the on-disk usearch file.  Distinct from the previous
 * Vamana backend's `.vec' so the two backends never confuse each other's
 * files; if a user is migrating, they ALTER INDEX REBUILD once.
 */
#define VECIDX_FILE_SUFFIX ".vec"

/* Forward decls — definitions are after the handle/SYSINDEX helpers
 * section since they need TXvecHandle and vec_sysindex_lookup_*. */

/* ----- TXvecParams: defaults / parse / from-options / to-text ------- */

/* Sentinel used in TXvecParams.quant_zp to distinguish "user didn't set
 * it, apply the dtype default" from a deliberate zero (which is the
 * correct symmetric default for i8 but a deliberate choice for u8).
 * Never appears in persisted PARAMS — apply_quant_defaults() always
 * resolves it to a real value before the index is built or saved.
 */
#define TX_VEC_ZP_UNSET INT_MIN

static void
vec_params_init(TXvecParams *p)
{
    static const vec_graph_params_t graph_defaults = VEC_GRAPH_PARAMS_DEFAULT;
    memset(p, 0, sizeof(*p));
    p->backend = VEC_BACKEND_HNSW;     /* legacy SYSINDEX.PARAMS strings
                                        * have no `backend=' key and were
                                        * always HNSW; TXvecParamsFromOptions
                                        * overrides this to IVFPQ for new
                                        * CREATE statements that don't say
                                        * WITH backend explicitly */
    p->graph = graph_defaults;
    p->threshold_t = 10000;
    p->threshold_d = 1000;
    /* sentinels: scale<=0 → "use dtype default"; zp == TX_VEC_ZP_UNSET → "use dtype default" */
    p->quant_scale = 0.0f;
    p->quant_zp = TX_VEC_ZP_UNSET;
    /* IVFPQ fields default to 0 = "auto-pick from target_rows / row count
     * at CREATE time".  Filled in by vec_params_apply_pq_defaults(). */
    p->pq_m = 0;
    p->pq_nlist = 0;
    p->pq_nbits = 0;
    p->pq_target_rows = 0;
    p->pq_min_points_per_centroid = 0;   /* 0 = use FAISS default (39) */
}

/* Round x up to the next power of 2.  `x` must be > 0. */
static uint64_t
round_pow2_u64(uint64_t x)
{
    uint64_t r = 1;
    while (r < x) r <<= 1;
    return r;
}

/* Round x up to the next multiple of m.  `m` must be > 0. */
static int
round_to_mult(int x, int m)
{
    return ((x + m - 1) / m) * m;
}

/* Auto-tune IVFPQ params from `pq_target_rows` (or current row count)
 * for any field the user didn't supply explicitly.  Called from the
 * IVFPQ create path.  Idempotent — running it twice with the same
 * inputs is a no-op.  See plan §4 for the heuristics.
 *
 * `dim` is the column dimensionality (must be > 0); used to pick `M`
 * such that the per-subquantizer subspace dimension `dim/M` lands
 * in [8, 16].  Caller passes dim from the column type.
 */
void
vec_params_apply_pq_defaults(TXvecParams *p, int dim, size_t current_rows)
{
    size_t n_target = (size_t)p->pq_target_rows;
    if (n_target < current_rows) n_target = current_rows;
    if (n_target == 0) n_target = 1;

    if (p->pq_nlist == 0) {
        /* nlist ≈ 4 * sqrt(N_target), clamped to [64, 65536], rounded
         * up to a power of two for cleaner partitioning. */
        double s = 4.0 * sqrt((double)n_target);
        uint64_t nl = (s < 64.0) ? 64 : (uint64_t)s;
        nl = round_pow2_u64(nl);
        if (nl < 64)    nl = 64;
        if (nl > 65536) nl = 65536;
        p->pq_nlist = (int)nl;
    }
    if (p->pq_m == 0 && dim > 0) {
        /* FAISS IVFPQ requires dim % M == 0, so M MUST divide dim.  Aim for a
         * per-subquantizer subspace dsub = dim/M ~= 8 (prefer 8..16), with M a
         * multiple of 4 (FastScan-friendly).  Search the divisors of dim and
         * pick the one closest to that target.  This works for any embedding
         * width (384, 768, 1024, ...) instead of clamping to a value that may
         * not divide dim (e.g. old code picked 96 for dim=1024, and 1024 % 96
         * != 0 -> FAISS set_derived_values() aborts). */
        int best = 0, best_score = 0x7fffffff, M;
        for (M = 4; M <= dim && M <= 256; M += 4) {
            int dsub, score;
            if (dim % M != 0) continue;          /* must divide dim */
            dsub = dim / M;
            if (dsub < 4) break;                 /* too fine */
            score = (dsub > 8 ? dsub - 8 : 8 - dsub)
                  + (dsub > 16 ? 1000 : 0);      /* prefer dsub in [8,16], ~8 */
            if (score < best_score) { best_score = score; best = M; }
        }
        if (best == 0) {
            /* Odd/prime-ish dim with no multiple-of-4 divisor: fall back to the
             * largest plain divisor giving dsub >= 4 (PQ works; FastScan won't). */
            for (M = (dim < 256 ? dim : 256); M >= 1; M--)
                if (dim % M == 0 && dim / M >= 4) { best = M; break; }
            if (best == 0) best = 1;
        }
        p->pq_m = best;
    }
    if (p->pq_nbits == 0) {
        p->pq_nbits = 8;            /* v1: hardcoded; FastScan = 4 in phase 4 */
    }
}

/* Once `p->dtype` is locked in, fill in default quantization parameters
 * for i8/u8 if the caller didn't supply them.  Called from
 * TXvecCreateIndex after the dtype-resolution block; idempotent.
 */
void
vec_params_apply_quant_defaults(TXvecParams *p)
{
    int zp_user_set = (p->quant_zp != TX_VEC_ZP_UNSET);
    switch (p->dtype) {
    case FTN_VEC_I8:
        if (p->quant_scale <= 0.0f) p->quant_scale = 1.0f / 127.0f;
        if (!zp_user_set)            p->quant_zp = 0;
        break;
    case FTN_VEC_U8:
        if (p->quant_scale <= 0.0f) p->quant_scale = 1.0f / 127.0f;
        if (!zp_user_set)            p->quant_zp = 128;
        break;
    default:
        /* float dtypes — zero out unused fields for tidiness */
        if (!zp_user_set) p->quant_zp = 0;
        break;
    }
}

static const char *
vec_opt_get(TXindOpts *options, TXindOpt opt)
{
    int i;
    if (!options) return NULL;
    for (i = 0; i < options->numOptions; i++)
        if (options->option[i] == opt)
            return options->values[i] ? options->values[i][0] : NULL;
    return NULL;
}

/* Public helper: peek at the backend option (default IVFPQ) and
 * return the SYSUPDATE.KIND label that would apply.  Used by
 * createindex (in index.c) at CREATE time, before SYSINDEX.PARAMS
 * exists, so the SYSUPDATE row can be tagged with the specific
 * backend from the start. */
const char *
TXvecKindFromOptions(TXindOpts *options)
{
    const char *s = vec_opt_get(options, TXindOpt_backend);
    if (s) {
        if (!strcasecmp(s, "hnsw") || !strcasecmp(s, "usearch"))
            return "vec-hnsw";
    }
    /* Default backend (no `WITH backend ...`) is IVFPQ — same as
     * TXvecParamsFromOptions.  On 32-bit ARM FAISS is not built and
     * the default flips to HNSW to match. */
#ifdef RP_NO_FAISS
    return "vec-hnsw";
#else
    return "vec-ivfpq";
#endif
}

int
TXvecParamsFromOptions(TXvecParams *out, TXindOpts *options)
{
    static const char fn[] = "TXvecParamsFromOptions";
    const char *s;
    char *e;
    long li;
    double d;

    vec_params_init(out);
    if (!options) return 0;

    /* Backend selector.  CREATE-time default is IVFPQ (smaller on-disk
     * footprint, faster optimize, comparable search latency, recall=1
     * via the delta + sealed merge).  HNSW remains available via
     * `WITH backend 'hnsw'` for tables below the IVFPQ training floor
     * (~10k rows) or for ultra-high-dim corpora where PQ approximation
     * degrades. */
    if ((s = vec_opt_get(options, TXindOpt_backend)) != NULL) {
        if      (!strcasecmp(s, "hnsw")  || !strcasecmp(s, "usearch"))
            out->backend = VEC_BACKEND_HNSW;
        else if (!strcasecmp(s, "ivfpq") || !strcasecmp(s, "faiss"))
            out->backend = VEC_BACKEND_IVFPQ;
        else {
            putmsg(MERR + UGE, fn,
                "backend must be `hnsw' or `ivfpq'; got `%s'", s);
            return -1;
        }
    } else {
#ifdef RP_NO_FAISS
        /* On 32-bit ARM FAISS is not built — default to HNSW so users
         * without an explicit `with backend` clause still get a working
         * CREATE.  Explicit `backend ivfpq' still errors at dispatch
         * with a clear message (see TXvecCreateIndex). */
        out->backend = VEC_BACKEND_HNSW;
#else
        out->backend = VEC_BACKEND_IVFPQ;
#endif
    }

    if ((s = vec_opt_get(options, TXindOpt_vec_m)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < 4 || li > 1024) {
            putmsg(MERR + UGE, fn,
                "vec_m must be an integer in [4, 1024]; got `%s'", s);
            return -1;
        }
        out->graph.M = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_efc)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < 8 || li > 4096) {
            putmsg(MERR + UGE, fn,
                "vec_efc must be an integer in [8, 4096]; got `%s'", s);
            return -1;
        }
        out->graph.ef_construction = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_alpha)) != NULL) {
        d = strtod(s, &e);
        if (e == s || *e || d < 1.0 || d > 2.0) {
            putmsg(MERR + UGE, fn,
                "vec_alpha must be a number in [1.0, 2.0]; got `%s'", s);
            return -1;
        }
        out->graph.alpha = (float)d;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_metric)) != NULL) {
        if (!strcasecmp(s, "dot") || !strcasecmp(s, "ip") ||
            !strcasecmp(s, "inner")) {
            out->graph.metric = VEC_METRIC_DOT;
        } else if (!strcasecmp(s, "cos") || !strcasecmp(s, "cosine")) {
            putmsg(MERR + UGE, fn,
                "vec_metric `%s' is not yet supported; for cosine "
                "semantics L2-normalize your vectors and use `dot'", s);
            return -1;
        } else if (!strcasecmp(s, "l2") || !strcasecmp(s, "l2sq") ||
                   !strcasecmp(s, "euclidean")) {
            putmsg(MERR + UGE, fn,
                "vec_metric `%s' is not yet supported; no SQL operator "
                "currently consumes this ranking", s);
            return -1;
        } else {
            putmsg(MERR + UGE, fn,
                "vec_metric must be `dot' (or alias `ip'/`inner'); got `%s'",
                s);
            return -1;
        }
    }
    /* ----- IVFPQ-specific options ----------------------------------- */

    if ((s = vec_opt_get(options, TXindOpt_vec_pq_m)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < 4 || li > 256) {
            putmsg(MERR + UGE, fn,
                "vec_pq_m must be an integer in [4, 256]; got `%s'", s);
            return -1;
        }
        out->pq_m = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_pq_nlist)) != NULL) {
        errno = 0;
        li = strtol(s, &e, 10);
        if (e == s || *e || errno == ERANGE || li < 4 || li > INT_MAX) {
            putmsg(MERR + UGE, fn,
                "vec_pq_nlist must be an integer >= 4; got `%s'", s);
            return -1;
        }
        out->pq_nlist = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_pq_nbits)) != NULL) {
        li = strtol(s, &e, 10);
        /* v1 supports nbits=8 only; FastScan's 4-bit path comes in
         * phase 4.  Accept the value to allow forward-compat PARAMS
         * strings, but warn loudly if it isn't 8. */
        if (e == s || *e || li < 4 || li > 8) {
            putmsg(MERR + UGE, fn,
                "vec_pq_nbits must be an integer in [4, 8]; got `%s'", s);
            return -1;
        }
        if (li != 8) {
            putmsg(MERR + UGE, fn,
                "vec_pq_nbits=%ld not yet supported (FastScan is phase 4); "
                "use vec_pq_nbits=8", li);
            return -1;
        }
        out->pq_nbits = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_pq_target_rows)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < 1) {
            putmsg(MERR + UGE, fn,
                "vec_pq_target_rows must be a positive integer; got `%s'", s);
            return -1;
        }
        if (li > INT_MAX) li = INT_MAX;
        out->pq_target_rows = (int)li;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_pq_min_points_per_centroid)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < 1 || li > 39) {
            putmsg(MERR + UGE, fn,
                "vec_pq_min_points_per_centroid must be an integer in [1, 39]; "
                "got `%s'", s);
            return -1;
        }
        out->pq_min_points_per_centroid = (int)li;
    }

    if ((s = vec_opt_get(options, TXindOpt_vec_dtype)) != NULL) {
        if      (!strcasecmp(s, "f64"))  out->dtype = FTN_VEC_F64;
        else if (!strcasecmp(s, "f32"))  out->dtype = FTN_VEC_F32;
        else if (!strcasecmp(s, "f16"))  out->dtype = FTN_VEC_F16;
        else if (!strcasecmp(s, "bf16")) out->dtype = FTN_VEC_BF16;
        else if (!strcasecmp(s, "i8"))   out->dtype = FTN_VEC_I8;
        else if (!strcasecmp(s, "u8"))   out->dtype = FTN_VEC_U8;
        else {
            putmsg(MERR + UGE, fn,
                "vec_dtype must be one of `f64',`f32',`f16',`bf16',`i8',`u8'; got `%s'",
                s);
            return -1;
        }
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_scale)) != NULL) {
        d = strtod(s, &e);
        if (e == s || *e || d <= 0.0) {
            putmsg(MERR + UGE, fn,
                "vec_scale must be a positive number; got `%s'", s);
            return -1;
        }
        out->quant_scale = (float)d;
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_zero_point)) != NULL) {
        li = strtol(s, &e, 10);
        if (e == s || *e || li < -128 || li > 255) {
            putmsg(MERR + UGE, fn,
                "vec_zero_point must be an integer in [-128, 255]; got `%s'", s);
            return -1;
        }
        out->quant_zp = (int)li;     /* clears the TX_VEC_ZP_UNSET sentinel */
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_calibrate)) != NULL) {
        if      (!strcasecmp(s, "none"))                       out->calibrate_mode = 0;
        else if (!strcasecmp(s, "auto") ||
                 !strcasecmp(s, "asymmetric"))                  out->calibrate_mode = 1;
        else {
            putmsg(MERR + UGE, fn,
                "vec_calibrate must be `none' or `auto'; got `%s'", s);
            return -1;
        }
    }
    return 0;
}

/* Map dtype FTN tag → string for PARAMS serialization, NULL on unknown. */
static const char *
vec_dtype_name(int dtype)
{
    switch (dtype) {
    case FTN_VEC_F64:  return "f64";
    case FTN_VEC_F32:  return "f32";
    case FTN_VEC_F16:  return "f16";
    case FTN_VEC_BF16: return "bf16";
    case FTN_VEC_I8:   return "i8";
    case FTN_VEC_U8:   return "u8";
    default:           return NULL;
    }
}

/* Map dtype FTN tag → element size in bytes, 0 on unknown. */
size_t
vec_dtype_elsz(int dtype)
{
    switch (dtype) {
    case FTN_VEC_F64:  return 8;
    case FTN_VEC_F32:  return 4;
    case FTN_VEC_F16:  return 2;
    case FTN_VEC_BF16: return 2;
    case FTN_VEC_I8:   return 1;
    case FTN_VEC_U8:   return 1;
    default:           return 0;
    }
}

int
TXvecParamsParse(TXvecParams *out, const char *params)
{
    vec_params_init(out);
    if (!params || !*params) return 0;

    char *dup = strdup(params);
    if (!dup) return -1;
    char *p = dup;
    while (*p) {
        while (*p == ';' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *eq = strchr(p, '=');
        char *end = strchr(p, ';');
        if (!end) end = p + strlen(p);
        if (!eq || eq >= end) { p = end; continue; }
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        char saved = *end;
        *end = '\0';

        if      (!strcmp(key, "backend")) {
            if      (!strcmp(val, "hnsw") ||
                     !strcmp(val, "usearch")) out->backend = VEC_BACKEND_HNSW;
            else if (!strcmp(val, "ivfpq") ||
                     !strcmp(val, "faiss"))   out->backend = VEC_BACKEND_IVFPQ;
            /* unknown backend value: leave as init default (HNSW) */
        }
        else if (!strcmp(key, "dim"))    out->graph.dim = atoi(val);
        else if (!strcmp(key, "M"))      out->graph.M = atoi(val);
        else if (!strcmp(key, "efc"))    out->graph.ef_construction = atoi(val);
        else if (!strcmp(key, "alpha"))  out->graph.alpha = (float)atof(val);
        else if (!strcmp(key, "metric")) {
            if (!strcmp(val, "l2"))       out->graph.metric = VEC_METRIC_L2;
            else if (!strcmp(val, "dot")) out->graph.metric = VEC_METRIC_DOT;
        }
        else if (!strcmp(key, "dtype")) {
            if      (!strcmp(val, "f64"))  out->dtype = FTN_VEC_F64;
            else if (!strcmp(val, "f32"))  out->dtype = FTN_VEC_F32;
            else if (!strcmp(val, "f16"))  out->dtype = FTN_VEC_F16;
            else if (!strcmp(val, "bf16")) out->dtype = FTN_VEC_BF16;
            else if (!strcmp(val, "i8"))   out->dtype = FTN_VEC_I8;
            else if (!strcmp(val, "u8"))   out->dtype = FTN_VEC_U8;
        }
        else if (!strcmp(key, "quant_scale")) {
            out->quant_scale = (float)atof(val);
        }
        else if (!strcmp(key, "quant_zp")) {
            out->quant_zp = atoi(val);
        }
        /* IVFPQ-specific (only present when backend=ivfpq).  HNSW
         * indexes will never see these keys; the parser silently
         * accepts them on any backend so the values round-trip
         * through ALTER INDEX paths that don't touch backend. */
        else if (!strcmp(key, "pq_m"))            out->pq_m = atoi(val);
        else if (!strcmp(key, "pq_nlist"))        out->pq_nlist = atoi(val);
        else if (!strcmp(key, "pq_nbits"))        out->pq_nbits = atoi(val);
        else if (!strcmp(key, "pq_target_rows"))  out->pq_target_rows = atoi(val);
        else if (!strcmp(key, "pq_min_ppc"))      out->pq_min_points_per_centroid = atoi(val);
        *end = saved;
        p = (saved ? end + 1 : end);
    }
    free(dup);
    return 0;
}

int
TXvecParamsToText(char *buf, size_t bufSz, const TXvecParams *p)
{
    /* HNSW path emits flush= and state= so reading code never has to
     * guess the default — and so a state transition (dirty→clean and
     * back) is just a fixed-length substring rewrite of the existing
     * line.  IVFPQ omits those fields entirely; see plan §3 for why
     * IVFPQ doesn't have HNSW's auto/manual + dirty-bit machinery.
     *
     * dtype is emitted when known so a varbyte-backed index can be
     * reopened without consulting the column.  HNSW quant fields are
     * appended only for i8/u8 indexes; floats leave them off the wire.
     */
    const char *dtypeStr = vec_dtype_name(p->dtype);
    int n;

    if (p->backend == VEC_BACKEND_IVFPQ) {
        /* IVFPQ form.  No flush= / state= (HNSW-only).  No graph.M /
         * efc / alpha (HNSW-only).  Quant fields only emitted when
         * the column dtype is i8/u8 (column-level scalar quantization,
         * orthogonal to the PQ codes that IVFPQ stores internally). */
        int is_quant_col = (p->dtype == FTN_VEC_I8 || p->dtype == FTN_VEC_U8);
        /* `pq_min_ppc` (min training points per centroid) is emitted only
         * when explicitly set; the read-back parser falls back to the
         * FAISS default (39) when absent. */
        char min_ppc_clause[32] = "";
        if (p->pq_min_points_per_centroid > 0)
            snprintf(min_ppc_clause, sizeof(min_ppc_clause),
                     ";pq_min_ppc=%d", p->pq_min_points_per_centroid);
        if (is_quant_col) {
            n = snprintf(buf, bufSz,
                "type=vec;backend=ivfpq;dim=%d;dtype=%s;metric=%s"
                ";pq_m=%d;pq_nlist=%d;pq_nbits=%d;pq_target_rows=%d%s"
                ";quant_scale=%.6f;quant_zp=%d",
                p->graph.dim,
                dtypeStr ? dtypeStr : "f32",
                (p->graph.metric == VEC_METRIC_L2) ? "l2" : "dot",
                p->pq_m, p->pq_nlist, p->pq_nbits, p->pq_target_rows,
                min_ppc_clause,
                (double)p->quant_scale, p->quant_zp);
        } else {
            n = snprintf(buf, bufSz,
                "type=vec;backend=ivfpq;dim=%d;dtype=%s;metric=%s"
                ";pq_m=%d;pq_nlist=%d;pq_nbits=%d;pq_target_rows=%d%s",
                p->graph.dim,
                dtypeStr ? dtypeStr : "f32",
                (p->graph.metric == VEC_METRIC_L2) ? "l2" : "dot",
                p->pq_m, p->pq_nlist, p->pq_nbits, p->pq_target_rows,
                min_ppc_clause);
        }
    } else {
        /* HNSW form (existing).  backend=usearch is the literal value
         * we've always emitted; kept for back-compat with PARAMS strings
         * already on disk.  An equivalent `backend=hnsw` is also accepted
         * by the parser. */
        int is_quantized = (p->dtype == FTN_VEC_I8 || p->dtype == FTN_VEC_U8);
        if (is_quantized) {
            n = snprintf(buf, bufSz,
                "type=vec;backend=usearch;dim=%d;dtype=%s;M=%d;efc=%d;alpha=%.3f;metric=%s"
                ";quant_scale=%.6f;quant_zp=%d",
                p->graph.dim,
                dtypeStr ? dtypeStr : "f32",
                p->graph.M, p->graph.ef_construction, p->graph.alpha,
                (p->graph.metric == VEC_METRIC_L2) ? "l2" : "dot",
                (double)p->quant_scale, p->quant_zp);
        } else {
            n = snprintf(buf, bufSz,
                "type=vec;backend=usearch;dim=%d;dtype=%s;M=%d;efc=%d;alpha=%.3f;metric=%s",
                p->graph.dim,
                dtypeStr ? dtypeStr : "f32",
                p->graph.M, p->graph.ef_construction, p->graph.alpha,
                (p->graph.metric == VEC_METRIC_L2) ? "l2" : "dot");
        }
    }
    return (n < 0 || (size_t)n >= bufSz) ? -1 : n;
}

/* ----- helpers ------------------------------------------------------- */

static char *
make_usearch_path(const char *indfile)
{
    /* sizeof(literal) includes the trailing NUL, so it already covers
     * the byte we need to terminate `path`.  No `+ 1`. */
    size_t plen = strlen(indfile);
    char *path = (char *)malloc(plen + sizeof(VECIDX_FILE_SUFFIX));
    if (!path) return NULL;
    memcpy(path, indfile, plen);
    memcpy(path + plen, VECIDX_FILE_SUFFIX, sizeof(VECIDX_FILE_SUFFIX));
    return path;
}

static usearch_metric_kind_t
metric_to_usearch(vec_metric_t m)
{
    return (m == VEC_METRIC_L2) ? usearch_metric_l2sq_k : usearch_metric_ip_k;
}

/* Map an FTN_VEC_* tag to the corresponding usearch_scalar_*_k.  Returns
 * usearch_scalar_unknown_k for unsupported types — caller treats that
 * as a hard error.
 */
static usearch_scalar_kind_t
dtype_to_usearch_scalar(int dtype)
{
    switch (dtype) {
    case FTN_VEC_F64:  return usearch_scalar_f64_k;
    case FTN_VEC_F32:  return usearch_scalar_f32_k;
    case FTN_VEC_F16:  return usearch_scalar_f16_k;
    case FTN_VEC_BF16: return usearch_scalar_bf16_k;
    case FTN_VEC_I8:   return usearch_scalar_i8_k;
    case FTN_VEC_U8:   return usearch_scalar_u8_k;
    default:           return usearch_scalar_unknown_k;
    }
}

/* Convert one row's vector to f32 in `dst[0..dim)`.  For i8/u8 sources,
 * `scale` and `zp` are the dequantization parameters from the index's
 * PARAMS (the column carries no inherent calibration).  Returns 0 on
 * success, -1 on unsupported/empty.
 */
int
vec_convert_to_f32(int t, const void *raw, size_t n_elems, int dim,
                   float scale, int zp, float *dst)
{
    if ((int)n_elems != dim) return -1;
    switch (t) {
    case FTN_VEC_F32:
        memcpy(dst, raw, (size_t)dim * sizeof(float));
        return 0;
    case FTN_VEC_F64:
        rpvec_f64_to_f32((const double *)raw, dst, (size_t)dim);
        return 0;
    case FTN_VEC_F16:
        rpvec_f16_to_f32((const uint16_t *)raw, dst, (size_t)dim);
        return 0;
    case FTN_VEC_BF16:
        rpvec_bf16_to_f32((const uint16_t *)raw, dst, (size_t)dim);
        return 0;
    case FTN_VEC_I8:
        rpvec_i8_to_f32((const int8_t *)raw, dst, (size_t)dim, scale, zp);
        return 0;
    case FTN_VEC_U8:
        rpvec_u8_to_f32((const uint8_t *)raw, dst, (size_t)dim, scale, zp);
        return 0;
    default:
        return -1;
    }
}

/* Convert the f32 vector in `src[0..dim)` to the index's storage dtype,
 * writing into `dst` (caller-allocated, dim * vec_dtype_elsz(dst_dtype)
 * bytes).  For i8/u8 destinations, `scale` and `zp` apply.  Returns 0
 * on success, -1 on unsupported.
 */
static int
quantize_from_f32(int dst_dtype, const float *src, int dim,
                  float scale, int zp, void *dst)
{
    switch (dst_dtype) {
    case FTN_VEC_F32:
        memcpy(dst, src, (size_t)dim * sizeof(float));
        return 0;
    case FTN_VEC_F64: {
        double *d = (double *)dst;
        for (int i = 0; i < dim; i++) d[i] = (double)src[i];
        return 0;
    }
    case FTN_VEC_F16:
        /* No vectorized rpvec_f32_to_f16 in the texis-side externs; use
         * the round-trip via the existing f16 helper if needed.  In
         * practice the vec-index doesn't quantize-to-f16 outputs — see
         * the dtype-passthrough fast paths in callers. */
        return -1;
    case FTN_VEC_BF16:
        return -1;
    case FTN_VEC_I8:
        rpvec_f32_to_i8(src, (int8_t *)dst, (size_t)dim, scale, zp);
        return 0;
    case FTN_VEC_U8:
        rpvec_f32_to_u8(src, (uint8_t *)dst, (size_t)dim, scale, zp);
        return 0;
    default:
        return -1;
    }
}

/* End-to-end "feed one row to usearch" pipeline:
 *   raw column bytes -> f32 (qbuf_f32) -> [optional quantize to qbuf_idx]
 *   -> usearch_add with the matching scalar kind.
 *
 * For float-storage indexes (f32) qbuf_idx may be NULL; the f32 buffer
 * is fed directly.  For i8/u8 storage qbuf_idx must be pre-allocated to
 * dim * vec_dtype_elsz(index_dtype) bytes.  Returns 0 on success, -1 on
 * any conversion or usearch error (with `*uerr_out` populated by usearch).
 */
static int
vec_add_one(usearch_index_t idx, usearch_key_t key, int dim,
            int index_dtype, float scale, int zp,
            int column_dtype, const void *raw, size_t cells,
            float *qbuf_f32, void *qbuf_idx,
            const char **uerr_out)
{
    if (vec_convert_to_f32(column_dtype, raw, cells, dim, scale, zp, qbuf_f32) < 0)
        return -1;
    const void *uvec = qbuf_f32;
    usearch_scalar_kind_t ukind = usearch_scalar_f32_k;
    if (index_dtype == FTN_VEC_I8 || index_dtype == FTN_VEC_U8) {
        if (!qbuf_idx) return -1;
        if (quantize_from_f32(index_dtype, qbuf_f32, dim, scale, zp, qbuf_idx) < 0)
            return -1;
        uvec = qbuf_idx;
        ukind = dtype_to_usearch_scalar(index_dtype);
    }
    *uerr_out = NULL;
    usearch_add(idx, key, uvec, ukind, uerr_out);
    return *uerr_out ? -1 : 0;
}

/* ----- Index creation ----------------------------------------------- */

/* Public dispatcher: parse options to learn which backend the user
 * asked for, then delegate to that backend's create slot. */
int
TXvecCreateIndex(DDIC *ddic, DBTBL *dbtbl,
                 const char *field, const char *indname,
                 const char *indfile, TXindOpts *options,
                 TXvecParams *outParams)
{
    static const char fn[] = "TXvecCreateIndex";
    TXvecParams vp;
    if (TXvecParamsFromOptions(&vp, options) < 0) return -1;
#ifdef RP_NO_FAISS
    /* FAISS is not built on this platform (32-bit ARM) — see
     * extern/extern.cmake.  IVFPQ requests must error cleanly rather
     * than silently fall through to HNSW. */
    if (vp.backend == VEC_BACKEND_IVFPQ) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC backend=ivfpq is not supported on 32-bit ARM "
            "(FAISS upstream does not support this platform); use "
            "backend=hnsw");
        return -1;
    }
#elif defined(__linux__) || defined(__FreeBSD__)
    /* IVFPQ uses faiss which uses BLAS + OpenMP + (on Linux) gfortran.
     * Linux: libopenblas / libgomp / libgfortran / libquadmath are NOT
     *   Priority: required on Debian/Ubuntu; bundle ships without DT_NEEDED
     *   on them, dlopen on first IVFPQ use.
     * FreeBSD: libomp is base, but libopenblas + (transitively) libgfortran
     *   come from the openblas package and aren't present on a vanilla install.
     *   Same probe pattern, single dlopen target.
     * Not needed on macOS — FAISS uses Accelerate.framework (always present). */
    if (vp.backend == VEC_BACKEND_IVFPQ) {
        const char *blas_err = NULL;
        if (!texis_vec_blas_probe(&blas_err)) {
            putmsg(MERR + UGE, fn, "%s", blas_err);
            return -1;
        }
    }
#endif
    return vec_backend_for(vp.backend)->create(ddic, dbtbl, field, indname,
                                               indfile, options, outParams);
}

/* HNSW slot for create.  Existing body, unchanged. */
static int
hnsw_create_impl(DDIC *ddic, DBTBL *dbtbl,
                 const char *field, const char *indname,
                 const char *indfile, TXindOpts *options,
                 TXvecParams *outParams)
{
    static const char fn[] = "TXvecCreateIndex";
    int rc = -1;
    int dim = 0;
    size_t n_added = 0, skipped = 0;
    float *qbuf = NULL;        /* per-row f32 work buffer */
    void  *qbuf_idx = NULL;    /* per-row index-dtype buffer (i8/u8) when needed */
    char *vecpath = NULL;
    METER *meter = NULL;
    EPI_HUGEINT meterDone = 0;
    EPI_OFF_T meterTotal = 0;
    usearch_index_t idx = NULL;
    const char *uerr = NULL;
    RECID *recid;

    (void)indname;

    /* SYSUPDATE: stage 1 of 3 (init + scan).  Stage 2 (encode) takes
     * over once the index is initialized on the first row; stage 3
     * (save) at the end. */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "scan rows");

    FLD *fld = dbnametofld(dbtbl, (char *)field);
    if (!fld) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: field `%s' not found in table", field);
        return -1;
    }
    int t = fld->type & DDTYPEBITS;
    if (!FTN_IS_VEC_OR_BYTE(t)) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC requires a vector or varbyte column; `%s' has type 0x%x",
            field, (unsigned)fld->type);
        return -1;
    }

    /* Parse params. */
    TXvecParams vp;
    if (TXvecParamsFromOptions(&vp, options) < 0) goto err;

    /* dtype resolution.
     *   - typed varvec column (incl. varvecI8/U8): the index dtype is
     *     the column's element type by default; an explicit vec_dtype
     *     option may override (cross-conversion at the index boundary).
     *   - varbyte/byte column: vec_dtype is required (we have no other
     *     way to know how to interpret cell bytes). */
    if (FTN_IS_VEC(t)) {
        if (vp.dtype == 0) vp.dtype = t;     /* no override → match column */
    } else {        /* FTN_BYTE */
        if (vp.dtype == 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC on a byte/varbyte column requires `with "
                "vec_dtype '...'` (f64/f32/f16/bf16/i8/u8) so the index "
                "knows how to interpret cell bytes");
            goto err;
        }
    }
    /* dtype now reflects the index's storage; fill in default scale/zp
     * for i8/u8 if the user didn't supply them. */
    vec_params_apply_quant_defaults(&vp);

    /* Pre-pass: count live rows so we can reserve usearch's capacity
     * (and pre-allocate per-thread context buffers).  usearch_reserve
     * doesn't auto-grow, so we need an upper bound up front.  Iterating
     * the table once without reading vectors is cheap if blob preload
     * is suppressed; otherwise gettblrow drags the vector blob through
     * TXblobiGetPayload for every row, which is the bulk of the I/O.
     */
    size_t row_estimate = 0;
    EPI_OFF_T scan_total_bytes = 0;
    {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            scan_total_bytes = (EPI_OFF_T)st.st_size;
    }
    int saved_preLoad_scan = (TXApp != NULL) ? TXApp->preLoadBlobs : 0;
    if (TXApp) TXApp->preLoadBlobs = 0;
    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        EPI_OFF_T off = TXgetoff(recid);
        row_estimate++;
        if (scan_total_bytes > 0)
            TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                (double)off / (double)scan_total_bytes);
    }
    if (TXApp) TXApp->preLoadBlobs = saved_preLoad_scan;

    /* Optional second pre-pass: vec_calibrate 'auto' on a quantized
     * index runs through the table once to find global min/max, then
     * derives scale + zp asymmetrically.  Skipped for float dtypes and
     * when the user passed `none` (the default).  Has its own progress
     * meter when indexmeter is enabled, mirroring fulltext's per-phase
     * meter convention (see fdbim.c). */
    if (vp.calibrate_mode == 1 &&
        (vp.dtype == FTN_VEC_I8 || vp.dtype == FTN_VEC_U8)) {
        float gmin =  FLT_MAX, gmax = -FLT_MAX;
        int   calib_dim = 0;
        float *calib_buf = NULL;
        METER *cmeter = NULL;
        EPI_HUGEINT cmeterDone = 0;
        if (options && options->indexmeter != TXMDT_NONE) {
            EPI_STAT_S st;
            EPI_OFF_T total = 0;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                total = (EPI_OFF_T)st.st_size;
            if (total > 0)
                cmeter = openmeter("INDEX_VEC: calibrating quantization:",
                                   options->indexmeter,
                                   MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                                   (EPI_HUGEINT)total);
        }
        EPI_OFF_T calib_total_bytes = 0;
        {
            EPI_STAT_S st2;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st2) == 0)
                calib_total_bytes = (EPI_OFF_T)st2.st_size;
        }
        TXrewinddbtbl(dbtbl);
        while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
            EPI_OFF_T calib_off = TXgetoff(recid);
            if (cmeter) {
                cmeterDone += (EPI_HUGEINT)dbtbl->tbl->irecsz;
                METER_UPDATEDONE(cmeter, cmeterDone);
            }
            if (calib_total_bytes > 0)
                TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                    (double)calib_off / (double)calib_total_bytes);
            size_t cn = 0;
            void *crow = getfld(fld, &cn);
            if (!crow || cn == 0) continue;
            int cdtype = (t == FTN_BYTE) ? vp.dtype : t;
            size_t cells = cn;
            if (t == FTN_BYTE) {
                size_t elsz = vec_dtype_elsz(cdtype);
                if (elsz == 0 || (cn % elsz) != 0) continue;
                cells = cn / elsz;
            }
            if (calib_dim == 0) {
                calib_dim = (int)cells;
                calib_buf = (float *)malloc((size_t)calib_dim * sizeof(float));
                if (!calib_buf) {
                    putmsg(MERR + MAE, fn, "alloc calib_buf");
                    if (cmeter) cmeter = closemeter(cmeter);
                    goto err;
                }
            } else if ((int)cells != calib_dim) {
                continue;       /* mismatch: skip; main build will warn */
            }
            /* For calibration we only need native column values —
             * pass scale=1, zp=0 (i8/u8 native sources rarely matter
             * here since calibrating them is unusual). */
            if (vec_convert_to_f32(cdtype, crow, cells, calib_dim,
                               1.0f, 0, calib_buf) < 0)
                continue;
            for (int i = 0; i < calib_dim; i++) {
                float v = calib_buf[i];
                if (v < gmin) gmin = v;
                if (v > gmax) gmax = v;
            }
        }
        free(calib_buf);
        if (cmeter) {
            EPI_STAT_S st;
            EPI_OFF_T total = 0;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                total = (EPI_OFF_T)st.st_size;
            meter_updatedone(cmeter, (EPI_HUGEINT)total);
            meter_end(cmeter);
            cmeter = closemeter(cmeter);
        }
        if (gmin > gmax) {
            putmsg(MWARN, fn,
                "vec_calibrate 'auto': no usable rows; falling back to defaults");
        } else if (gmax - gmin < 1e-12f) {
            putmsg(MWARN, fn,
                "vec_calibrate 'auto': zero data range; falling back to defaults");
        } else {
            float new_scale = (gmax - gmin) / 255.0f;
            long  new_zp;
            if (vp.dtype == FTN_VEC_I8)
                new_zp = (long)lrintf(-128.0f - gmin / new_scale);
            else
                new_zp = (long)lrintf(-gmin / new_scale);
            if (new_zp < (vp.dtype == FTN_VEC_I8 ? -128 : 0))
                new_zp = (vp.dtype == FTN_VEC_I8 ? -128 : 0);
            else if (new_zp > (vp.dtype == FTN_VEC_I8 ? 127 : 255))
                new_zp = (vp.dtype == FTN_VEC_I8 ? 127 : 255);
            vp.quant_scale = new_scale;
            vp.quant_zp = (int)new_zp;
        }
    }

    /* SYSUPDATE: stage 2 of 3 (encode rows). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                            "encode rows");

    /* Open progress meter sized by table file bytes. */
    if (options && options->indexmeter != TXMDT_NONE) {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            meterTotal = (EPI_OFF_T)st.st_size;
        if (meterTotal > 0)
            meter = openmeter("INDEX_VEC: scanning table:",
                              options->indexmeter,
                              MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                              (EPI_HUGEINT)meterTotal);
    } else {
        /* Need byte total for SYSUPDATE progress fraction even if no
         * indexmeter requested. */
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            meterTotal = (EPI_OFF_T)st.st_size;
    }

    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        /* gettblrow returns a pointer to a process-static RECID; any
         * call that runs internal SQL (e.g. TXsysupdateProgress' UPDATE
         * on SYSUPDATE) walks that table and stomps the static.  Snapshot
         * the offset NOW before we run any such call. */
        EPI_OFF_T row_off = TXgetoff(recid);
        if (meter) {
            meterDone += (EPI_HUGEINT)dbtbl->tbl->irecsz;
            METER_UPDATEDONE(meter, meterDone);
        }
        if (meterTotal > 0)
            TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                (double)row_off / (double)meterTotal);

        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) { skipped++; continue; }

        /* Resolve column dtype: how to interpret raw bytes from getfld().
         *   - typed varvec column: column_dtype is the column's element type
         *   - byte/varbyte column: column_dtype = vp.dtype (user-specified
         *     interpretation; the bytes ARE values of that type) */
        int column_dtype = (t == FTN_BYTE) ? vp.dtype : t;

        /* For varbyte columns getfld() returns byte count, not cells.
         * Typed varvec columns return cell count directly. */
        size_t cell_count = n_elems;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(column_dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) {
                putmsg(MWARN, fn,
                    "INDEX_VEC: row byte length %lu not a multiple of "
                    "dtype element size %lu; skipping",
                    (unsigned long)n_elems, (unsigned long)elsz);
                skipped++;
                continue;
            }
            cell_count = n_elems / elsz;
        }

        if (dim == 0) {
            /* First row: lock dim and initialize the index. */
            dim = (int)cell_count;

            /* Index storage is f32 by default; for i8/u8 indexes use the
             * matching usearch scalar kind so on-disk size shrinks 4x. */
            usearch_scalar_kind_t store_kind = usearch_scalar_f32_k;
            if (vp.dtype == FTN_VEC_I8) store_kind = usearch_scalar_i8_k;
            else if (vp.dtype == FTN_VEC_U8) store_kind = usearch_scalar_u8_k;

            usearch_init_options_t uo;
            memset(&uo, 0, sizeof(uo));
            uo.metric_kind   = metric_to_usearch(vp.graph.metric);
            uo.metric        = NULL;
            uo.quantization  = store_kind;
            uo.dimensions    = (size_t)dim;
            uo.connectivity  = (size_t)vp.graph.M;
            uo.expansion_add = (size_t)vp.graph.ef_construction;
            uo.expansion_search = (size_t)vp.graph.ef_construction;
            uo.multi         = false;

            idx = usearch_init(&uo, &uerr);
            if (!idx || uerr) {
                putmsg(MERR + UGE, fn,
                    "usearch_init failed: %s", uerr ? uerr : "(null)");
                goto err;
            }

            /* Reserve enough capacity for all rows up front.  usearch_reserve
             * doesn't auto-grow on add(); we counted the rows in the pre-pass.
             * Reserve also allocates the per-thread context pool that
             * usearch_add() requires.
             */
            size_t reserve_n = row_estimate > 0 ? row_estimate : 16;
            usearch_reserve(idx, reserve_n, &uerr);
            if (uerr) {
                putmsg(MERR + UGE, fn, "usearch_reserve: %s", uerr);
                goto err;
            }

            qbuf = (float *)malloc((size_t)dim * sizeof(float));
            if (!qbuf) { putmsg(MERR + MAE, fn, "alloc qbuf"); goto err; }

            /* Allocate the index-dtype output buffer for i8/u8 indexes. */
            if (vp.dtype == FTN_VEC_I8 || vp.dtype == FTN_VEC_U8) {
                qbuf_idx = malloc((size_t)dim * vec_dtype_elsz(vp.dtype));
                if (!qbuf_idx) { putmsg(MERR + MAE, fn, "alloc qbuf_idx"); goto err; }
            }
        }

        if ((int)cell_count != dim) {
            putmsg(MWARN, fn,
                "INDEX_VEC: skipping row: vector dim %lu != index dim %d",
                (unsigned long)cell_count, dim);
            skipped++;
            continue;
        }
        if (vec_add_one(idx, (usearch_key_t)(uint64_t)row_off, dim,
                        vp.dtype, vp.quant_scale, vp.quant_zp,
                        column_dtype, raw, cell_count,
                        qbuf, qbuf_idx, &uerr) < 0) {
            if (uerr) {
                putmsg(MERR + UGE, fn, "usearch_add: %s", uerr);
                goto err;
            }
            skipped++;
            continue;
        }
        n_added++;
    }

    if (meter) {
        meter_updatedone(meter, (EPI_HUGEINT)meterTotal);
        meter_end(meter);
    }

    if (n_added == 0 || !idx) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: no usable vectors in table (skipped=%lu)",
            (unsigned long)skipped);
        goto err;
    }

    /* SYSUPDATE: stage 3 of 3 (save). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 3,
                            "save");

    /* Persist. */
    vecpath = make_usearch_path(indfile);
    if (!vecpath) { putmsg(MERR + MAE, fn, "alloc path"); goto err; }
    usearch_save(idx, vecpath, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_save(%s): %s", vecpath, uerr);
        goto err;
    }

    if (outParams) {
        *outParams = vp;
        outParams->graph.dim = dim;
        outParams->dtype = vp.dtype;
    }

    /* Create empty `_T.btr` (newrec) and `_del.btr` (tombstone)
     * alongside the .vec.  All post-CREATE INSERTs accumulate in
     * `_T.btr`; deletes accumulate in `_del.btr`; SEARCH unions
     * sealed (.vec) + linear-scanned newrec rows + applies the
     * tombstone filter; ALTER INDEX OPTIMIZE folds them back into
     * the .vec via usearch_add + a single save_atomic.  Mirrors
     * texis fulltext's pattern (texis-internals.md §8.5). */
    {
        char *tomb_base   = TXvecMakeBtreeBasePath(indfile, "_del");
        char *newrec_base = TXvecMakeBtreeBasePath(indfile, "_T");
        if (!tomb_base || !newrec_base) {
            free(tomb_base); free(newrec_base);
            putmsg(MERR + MAE, fn, "alloc aux btree paths");
            goto err;
        }
        /* Defensive: erase prior leftovers from a re-created index. */
        TXvecBtreeUnlink(tomb_base);
        TXvecBtreeUnlink(newrec_base);
        int rc_t = TXvecBtreeCreateEmpty(tomb_base);
        int rc_n = TXvecBtreeCreateEmpty(newrec_base);
        free(tomb_base); free(newrec_base);
        if (rc_t != 0 || rc_n != 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: could not create auxiliary btrees");
            goto err;
        }
    }

    rc = (skipped > 0) ? 0 : 1;
    goto cleanup;

err:
    rc = -1;
cleanup:
    if (meter) meter = closemeter(meter);
    if (idx) usearch_free(idx, &uerr);
    free(qbuf);
    free(qbuf_idx);
    free(vecpath);
    return rc;
}

/* ----- Search-side handle cache ------------------------------------- */

/* HNSW-backend concrete handle.  First field is `base` (the polymorphic
 * TXvecHandleBase from vecindex_internal.h, carrying backend tag, cache
 * list pointer, path string, ddic, file identity).  HNSW-specific state
 * follows.  IVFPQ has its own struct in vecindex_ivfpq.cpp that embeds
 * the same base; both go into the same cache list. */
struct TXvecHnswHandle {
    struct TXvecHandleBase  base;        /* MUST be first; dim/metric/dtype live here */
    char            *fpath;       /* base.path + .vec suffix */
    char            *tomb_base;   /* "<base.path>_del" — _del.btr stem */
    char            *newrec_base; /* "<base.path>_T"   — _T.btr stem */
    usearch_index_t  index;       /* opaque usearch handle */
    /* HNSW-specific quantization parameters — only meaningful when
     * base.dtype is i8/u8; cached from PARAMS for use by per-row hooks
     * and LIKEV queries without re-parsing on each call. */
    float            quant_scale;
    int              quant_zp;
};

/* Cache stores the polymorphic base; both HNSW and IVFPQ handles
 * embed it as their first field.  Per-handle teardown dispatches via
 * base.backend → the right backend's close slot.
 *
 * The cache itself is private to this translation unit — the only
 * way for the IVFPQ backend (vecindex_ivfpq.cpp) to insert is via
 * vec_handle_cache_push() below. */
static struct TXvecHandleBase *vec_handle_cache = NULL;

void
vec_handle_cache_push(struct TXvecHandleBase *hb)
{
    if (!hb) return;
    hb->next = vec_handle_cache;
    vec_handle_cache = hb;
}

static int save_atomic(usearch_index_t idx, const char *fpath, const char *fn);
static int save_atomic_h(struct TXvecHnswHandle *h, const char *fn);

/* Capture (dev, ino, mtime, size) for `fpath`.  Sets all fields to 0
 * on stat() failure (file missing, permissions); the resulting record
 * compares unequal to any later stat with the file present, which is
 * what we want — a missing → present transition forces reload. */
static void
vec_capture_file_id(const char *fpath, struct TXvecHnswHandle *h)
{
    EPI_STAT_S st;
    if (EPI_STAT(fpath, &st) == 0) {
        h->base.file_dev   = st.st_dev;
        h->base.file_ino   = st.st_ino;
        h->base.file_mtime = (EPI_OFF_T)st.st_mtime;
        h->base.file_size  = (EPI_OFF_T)st.st_size;
    } else {
        h->base.file_dev   = 0;
        h->base.file_ino   = 0;
        h->base.file_mtime = 0;
        h->base.file_size  = 0;
    }
}

/* Return non-zero if the on-disk file for `h->fpath` no longer
 * matches what we recorded at load.  A mismatch means another process
 * has rewritten the file (save_atomic's rename gives a new inode);
 * our cached usearch state is now stale and must be discarded.
 */
static int
vec_handle_is_stale(const struct TXvecHnswHandle *h)
{
    EPI_STAT_S st;
    if (EPI_STAT(h->fpath, &st) != 0) {
        /* File disappeared (DROP from another process between our
         * load and now): treat as stale; subsequent open will fail
         * cleanly via usearch_metadata. */
        return 1;
    }
    if (st.st_dev != h->base.file_dev || st.st_ino != h->base.file_ino) return 1;
    if ((EPI_OFF_T)st.st_mtime != h->base.file_mtime) return 1;
    if ((EPI_OFF_T)st.st_size != h->base.file_size) return 1;
    return 0;
}

/* Internal: free `h` (frees usearch state and string fields).  Does
 * NOT touch the cache list — caller is responsible for unlinking. */
static void
vec_handle_free(struct TXvecHnswHandle *h)
{
    const char *uerr = NULL;
    if (!h) return;
    if (h->index) usearch_free(h->index, &uerr);
    free(h->base.path);
    free(h->fpath);
    free(h->tomb_base);
    free(h->newrec_base);
    free(h);
}

/* ----- SYSINDEX PARAMS update (in-place) ---------------------------- */

/* Match an SYSINDEX.FNAME entry against `indfile` (an absolute path the
 * caller passed in).  SYSINDEX stores either the basename (when the
 * file lives under ddic->pname) or the full path.  Either form should
 * match an indfile that ends with the SYSINDEX value.
 */
static int
vec_fname_matches(const char *sysFname, const char *indfile)
{
    size_t flen = strlen(sysFname);
    size_t ilen = strlen(indfile);
    if (flen > ilen) return 0;
    /* tail match */
    return strcmp(indfile + (ilen - flen), sysFname) == 0;
}

/* Read the current PARAMS for `indfile` into `outParams` (NULL on miss).
 * Caller must free.  Returns 0 if found, -1 if not found / on error.
 */
static int
vec_sysindex_get_params(DDIC *ddic, const char *indfile, char **outParams)
{
    TBL  *tb;
    FLD  *fnameFld, *paramsFld, *typeFld;
    RECID *at;
    int   rc = -1;

    *outParams = NULL;
    if (!ddic) return -1;
    tb = ddic->indextbl;
    if (!tb) return -1;
    fnameFld  = nametofld(tb, "FNAME");
    paramsFld = nametofld(tb, "PARAMS");
    typeFld   = nametofld(tb, "TYPE");
    if (!fnameFld || !paramsFld || !typeFld) return -1;

    if (TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL) == -1) return -1;
    rewindtbl(tb);
    while (TXrecidvalid(at = gettblrow(tb, NULL))) {
        const char *fn_ = (const char *)getfld(fnameFld, NULL);
        int t = fn_ ? (int)(*(char *)getfld(typeFld, NULL)) : 0;
        if (fn_ && t == INDEX_VEC && vec_fname_matches(fn_, indfile)) {
            const char *p = (const char *)getfld(paramsFld, NULL);
            *outParams = p ? strdup(p) : strdup("");
            rc = 0;
            break;
        }
    }
    TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
    return rc;
}

/* Atomically replace the on-disk index file with the in-memory state.
 * Writes to <fpath>.new, then renames over <fpath>.  Crash mid-write
 * leaves the original intact.
 *
 * NOTE: callers that hold a TXvecHandle should use save_atomic_h()
 * below so the handle's cached file identity is refreshed; otherwise
 * the next operation will see the rename's new inode and assume some
 * OTHER process wrote the file, evicting our cache unnecessarily.
 */
static int
save_atomic(usearch_index_t idx, const char *fpath, const char *fn)
{
    const char *uerr = NULL;
    size_t plen = strlen(fpath);
    char *tmp = (char *)malloc(plen + 5);
    if (!tmp) {
        putmsg(MERR + MAE, fn, "alloc tmp save path");
        return -1;
    }
    memcpy(tmp, fpath, plen);
    memcpy(tmp + plen, ".new", 5);

    usearch_save(idx, tmp, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_save(%s): %s", tmp, uerr);
        unlink(tmp);
        free(tmp);
        return -1;
    }
    if (rename(tmp, fpath) != 0) {
        putmsg(MERR + UGE, fn, "rename(%s, %s): %s",
               tmp, fpath, strerror(errno));
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

/* Same as save_atomic but also refreshes the handle's cached file
 * identity so vec_handle_is_stale doesn't fire on our own write. */
static int
save_atomic_h(struct TXvecHnswHandle *h, const char *fn)
{
    if (save_atomic(h->index, h->fpath, fn) != 0) return -1;
    vec_capture_file_id(h->fpath, h);
    return 0;
}

/* (Write-Ahead Log infrastructure was removed when the `_T.btr`
 * newrec design landed — every INSERT is now durable via the btree,
 * with no deferred state to log.  See vec-ivfpq-integration-plan.md
 * and texis-internals.md §8.5/§8.7.)
 */


/* HNSW vtable slot: open.  Dispatcher has already done cache lookup
 * and PARAMS parse; this function is on the cache-miss path only. */
static TXvecHandle *
hnsw_open_impl(DDIC *ddic, const char *indfile, const TXvecParams *vp)
{
    static const char fn[] = "TXvecOpen";
    const char *uerr = NULL;

    char *fpath = make_usearch_path(indfile);
    if (!fpath) { putmsg(MERR + MAE, fn, "alloc path"); return NULL; }

    /* Read header first to know dim/metric. */
    usearch_init_options_t opts;
    memset(&opts, 0, sizeof(opts));
    usearch_metadata(fpath, &opts, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_metadata(%s): %s", fpath, uerr);
        free(fpath);
        return NULL;
    }

    usearch_index_t idx = usearch_init(&opts, &uerr);
    if (!idx || uerr) {
        putmsg(MERR + UGE, fn, "usearch_init: %s", uerr ? uerr : "(null)");
        if (idx) usearch_free(idx, &uerr);
        free(fpath);
        return NULL;
    }

    /* Use load (read into RAM, mutable) so per-row insert/delete can
     * mutate the cached state.  Mmap'd `view` would be faster for read-
     * only loads but can't mutate; fine for very large indexes if we
     * ever distinguish read-only from mutable handles.
     */
    usearch_load(idx, fpath, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_load(%s): %s", fpath, uerr);
        usearch_free(idx, &uerr);
        free(fpath);
        return NULL;
    }

    struct TXvecHnswHandle *h =
        (struct TXvecHnswHandle *)calloc(1, sizeof(*h));
    if (!h) { usearch_free(idx, &uerr); free(fpath); return NULL; }
    h->base.backend = VEC_BACKEND_HNSW;
    h->base.path = strdup(indfile);
    if (!h->base.path) { free(h); usearch_free(idx, &uerr); free(fpath); return NULL; }
    h->fpath       = fpath;            /* h takes ownership */
    h->tomb_base   = TXvecMakeBtreeBasePath(indfile, "_del");
    h->newrec_base = TXvecMakeBtreeBasePath(indfile, "_T");
    h->index  = idx;
    h->base.dim    = (int)opts.dimensions;
    h->base.metric = (opts.metric_kind == usearch_metric_l2sq_k)
                ? VEC_METRIC_L2 : VEC_METRIC_DOT;
    h->base.ddic       = ddic;         /* may be NULL; refreshed on cache hit */

    /* Take dtype + quant params from the parsed PARAMS (already supplied
     * by the dispatcher).  Pre-i8 / pre-quant indexes leave vp->dtype
     * zero — fall back to F32. */
    h->base.dtype       = vp->dtype ? vp->dtype : FTN_VEC_F32;
    h->quant_scale = vp->quant_scale;
    h->quant_zp    = vp->quant_zp;

    /* Capture file identity for cross-process change detection.  Done
     * AFTER usearch_load returns so the inode/mtime reflect what we
     * actually loaded. */
    vec_capture_file_id(fpath, h);

    h->base.next     = vec_handle_cache;
    vec_handle_cache = &h->base;

    return (TXvecHandle *)h;
}

/* Public dispatcher.  Cache lookup is shared across backends; the
 * cache-miss path peels PARAMS, parses it, and dispatches by backend. */
TXvecHandle *
TXvecOpen(DDIC *ddic, const char *indfile, const char *params_in)
{
    /* Cache lookup.  On hit, validate that the on-disk file hasn't
     * been replaced by another process; if it has, evict and fall
     * through to a fresh load.  Refresh ddic on hit so the most
     * recent caller's DDIC is what an exit-time flush will use.
     *
     * Pointer-to-pointer walk so we can unlink in place if stale.  */
    {
        struct TXvecHandleBase **pp = &vec_handle_cache;
        while (*pp) {
            struct TXvecHandleBase *hb = *pp;
            if (strcmp(hb->path, indfile) == 0) {
                if (vec_backend_for(hb->backend)->is_stale((TXvecHandle *)hb)) {
                    /* Another process rewrote the file. */
                    *pp = hb->next;
                    vec_backend_for(hb->backend)->close((TXvecHandle *)hb);
                    break;          /* fall through to reload */
                }
                if (ddic) hb->ddic = ddic;
                return (TXvecHandle *)hb;
            }
            pp = &hb->next;
        }
    }

    /* Get PARAMS — caller may have passed it (predopt.c does); else
     * look it up.  This is the only SYSINDEX read on the cache-miss
     * path; the parsed result drives backend dispatch and seeds the
     * fresh handle so the backend impl never re-reads PARAMS. */
    char *owned_params = NULL;
    const char *params = params_in;
    if (!params) {
        if (vec_sysindex_get_params(ddic, indfile, &owned_params) != 0)
            return NULL;
        params = owned_params;
    }

    TXvecParams vp;
    int rc = TXvecParamsParse(&vp, params ? params : "");
    free(owned_params);
    if (rc != 0) return NULL;

#ifdef RP_NO_FAISS
    /* FAISS is not built on this platform (32-bit ARM).  Any existing
     * IVFPQ index file (e.g. copied in from another machine) cannot
     * be opened — the backend impl is absent.  Refuse cleanly. */
    if (vp.backend == VEC_BACKEND_IVFPQ) {
        putmsg(MERR + UGE, "TXvecOpen",
            "INDEX_VEC `%s' is backend=ivfpq, which is not supported on "
            "32-bit ARM (FAISS unavailable on this platform)", indfile);
        return NULL;
    }
#elif defined(__linux__) || defined(__FreeBSD__)
    /* BLAS probe — see parallel comment in TXvecCreateIndex. */
    if (vp.backend == VEC_BACKEND_IVFPQ) {
        const char *blas_err = NULL;
        if (!texis_vec_blas_probe(&blas_err)) {
            putmsg(MERR + UGE, "TXvecOpen", "%s", blas_err);
            return NULL;
        }
    }
#endif

    return vec_backend_for(vp.backend)->open(ddic, indfile, &vp);
}

/* ----- Per-row hooks ------------------------------------------------ */

/* HNSW vtable slot: add_row.
 *
 * The recid is recorded in `<base>_T.btr` (newrec).  No usearch_add,
 * no save_atomic — those happen at OPTIMIZE time when newrec entries
 * are folded into the .vec.  Cost: one btree insert (~µs) instead of
 * a full-file rewrite of the .vec (~hundreds of ms at scale).
 *
 * Mirrors texis fulltext's `addto3dbi` (3dbindex.c:4400). */
static int
hnsw_add_row_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                  const char *field, RECID *recid)
{
    static const char fn[] = "TXvecAddRow";
    (void)ddic; (void)dbtbl; (void)field;
    if (!recid) return -1;
    struct TXvecHnswHandle *h = (struct TXvecHnswHandle *)h_;
    if (!h || !h->newrec_base) return -1;
    int64_t r = (int64_t)(uint64_t)recid->off;
    if (TXvecBtreeInsertRecid(h->newrec_base, r) != 0) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: btinsert into `_T.btr' failed for recid %lld",
            (long long)r);
        return -1;
    }
    return 0;
}

/* HNSW vtable slot: del_row.
 *
 * Try to remove from `_T.btr` (no-op if recid wasn't a post-CREATE
 * insert).  Always tombstone via `_del.btr` so SEARCH filters out
 * stale .vec hits.  No usearch_remove on the cached index, no
 * save_atomic — those happen at OPTIMIZE time. */
static int
hnsw_del_row_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                  const char *field, RECID *recid)
{
    static const char fn[] = "TXvecDelRow";
    (void)ddic; (void)dbtbl; (void)field;
    if (!recid) return 0;
    struct TXvecHnswHandle *h = (struct TXvecHnswHandle *)h_;
    if (!h) return 0;
    int64_t r = (int64_t)(uint64_t)recid->off;
    if (h->newrec_base) TXvecBtreeDeleteRecid(h->newrec_base, r);
    if (h->tomb_base && TXvecBtreeInsertRecid(h->tomb_base, r) != 0) {
        putmsg(MWARN, fn,
            "INDEX_VEC: tombstone insert failed for recid %lld; "
            "subsequent SEARCH may return a stale entry", (long long)r);
    }
    return 0;
}

/* HNSW vtable slot: search.  Public dispatcher TXvecSearch passes
 * the handle through after walking the cache. */
static size_t
hnsw_search_impl(TXvecHandle *h_, DBTBL *dbtbl, const char *field,
                 const float *query, size_t k, size_t ef,
                 vec_search_result_t *results)
{
    static const char fn[] = "TXvecSearch";
    const char *uerr = NULL;
    void *qbuf_idx = NULL;
    (void)dbtbl; (void)field;     /* HNSW doesn't need a delta-scan path */
    struct TXvecHnswHandle *h = (struct TXvecHnswHandle *)h_;
    if (!h || !h->index) return SIZE_MAX;
    /* Per-query ef.  Caller's `ef` arg wins if non-zero; otherwise fall
     * back to the global TXlikevef set via sql.set/SET; 0 means leave
     * the index's existing expansion_search alone. */
    {
        size_t want_ef = (ef > 0) ? ef
                       : (TXlikevef > 0 ? (size_t)TXlikevef : 0);
        if (want_ef > 0) {
            const char *uerr2 = NULL;
            usearch_change_expansion_search(h->index, want_ef, &uerr2);
            if (uerr2) {
                putmsg(MWARN, fn, "usearch_change_expansion_search: %s",
                       uerr2);
            }
        }
    }

    usearch_key_t *keys = (usearch_key_t *)
        malloc(k * sizeof(usearch_key_t));
    usearch_distance_t *dists = (usearch_distance_t *)
        malloc(k * sizeof(usearch_distance_t));
    if (!keys || !dists) {
        free(keys); free(dists);
        return SIZE_MAX;
    }

    /* For i8/u8 indexes, quantize the query before searching so it lands
     * in the same coordinate space as the stored vectors. */
    const void *uquery = query;
    usearch_scalar_kind_t ukind = usearch_scalar_f32_k;
    if (h->base.dtype == FTN_VEC_I8 || h->base.dtype == FTN_VEC_U8) {
        qbuf_idx = malloc((size_t)h->base.dim * vec_dtype_elsz(h->base.dtype));
        if (!qbuf_idx) { free(keys); free(dists); return SIZE_MAX; }
        if (quantize_from_f32(h->base.dtype, query, h->base.dim,
                              h->quant_scale, h->quant_zp, qbuf_idx) < 0) {
            free(keys); free(dists); free(qbuf_idx); return SIZE_MAX;
        }
        uquery = qbuf_idx;
        ukind = dtype_to_usearch_scalar(h->base.dtype);
    }

    size_t got = usearch_search(h->index, uquery, ukind,
                                k, keys, dists, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_search: %s", uerr);
        free(keys); free(dists); free(qbuf_idx);
        return SIZE_MAX;
    }

    /* Walk auxiliary btrees: tombstones (recids whose .vec entry is
     * stale) and newrec (recids inserted post-CREATE that haven't
     * been folded yet).  Each is a sorted int64 array we bsearch
     * for the per-candidate filter. */
    struct vec_recid_vec { int64_t *data; size_t len; size_t cap; };
    struct vec_recid_vec tomb_v   = {NULL, 0, 0};
    struct vec_recid_vec newrec_v = {NULL, 0, 0};
    /* Local closure: append to a vec_recid_vec. */
    /* (TXvecBtreeWalkRecids takes a C function pointer; use a static
     * helper at file scope — see vec_recid_vec_push above.) */
    extern void vec_recid_vec_push_(int64_t r, void *user);
    TXvecBtreeWalkRecids(h->tomb_base,   vec_recid_vec_push_, &tomb_v);
    TXvecBtreeWalkRecids(h->newrec_base, vec_recid_vec_push_, &newrec_v);

    extern int vec_int64_cmp_(const void *a, const void *b);
    if (tomb_v.len)   qsort(tomb_v.data,   tomb_v.len,   sizeof(int64_t), vec_int64_cmp_);
    if (newrec_v.len) qsort(newrec_v.data, newrec_v.len, sizeof(int64_t), vec_int64_cmp_);

    /* Build the merged candidate set: sealed hits (.vec) minus
     * tombstones and newrec-overrides, plus a full linear scan of
     * newrec.  Sort by metric-natural score and trim to k.  Mirrors
     * the IVFPQ search path so HNSW recall is also unbiased by
     * delta-set fraction.  See vecindex_ivfpq.cpp's search_impl. */
    int ascending = (h->base.metric == VEC_METRIC_L2);
    size_t cap = (size_t)got + newrec_v.len;
    vec_search_result_t *merged = (cap > 0)
        ? (vec_search_result_t *)malloc(cap * sizeof(vec_search_result_t))
        : NULL;
    size_t mlen = 0;
    if (cap > 0 && !merged) {
        free(tomb_v.data); free(newrec_v.data);
        free(keys); free(dists); free(qbuf_idx);
        return SIZE_MAX;
    }

    /* Pack sealed (.vec) hits, dropping any whose recid is tombstoned
     * or has a newrec override (in which case the newrec scan below
     * carries the current vector). */
    for (size_t i = 0; i < got; i++) {
        int64_t id = (int64_t)(uint64_t)keys[i];
        if (tomb_v.len &&
            bsearch(&id, tomb_v.data, tomb_v.len, sizeof(int64_t), vec_int64_cmp_))
            continue;
        if (newrec_v.len &&
            bsearch(&id, newrec_v.data, newrec_v.len, sizeof(int64_t), vec_int64_cmp_))
            continue;
        merged[mlen].id = (vec_id_t)keys[i];
        merged[mlen].score = (h->base.metric == VEC_METRIC_L2)
                             ? (float)dists[i]
                             : 1.0f - (float)dists[i];
        mlen++;
    }

    /* Linear scan newrec: fetch each row, compute exact distance.
     * Always scan the whole delta (not capped at k) so the merged
     * top-k is correct regardless of delta size. */
    if (newrec_v.len > 0 && dbtbl && field) {
        FLD *fld = dbnametofld(dbtbl, (char *)field);
        if (fld) {
            int t = fld->type & DDTYPEBITS;
            int column_dtype = (t == FTN_BYTE) ? h->base.dtype : t;
            float *qbuf = (float *)malloc((size_t)h->base.dim * sizeof(float));
            if (qbuf) {
                int use_dot = (h->base.metric != VEC_METRIC_L2);
                for (size_t i = 0; i < newrec_v.len; i++) {
                    int64_t r = newrec_v.data[i];
                    BTLOC bl;
                    memset(&bl, 0, sizeof(bl));
                    bl.off = (EPI_OFF_T)r;
                    RECID *res = gettblrow(dbtbl->tbl, &bl);
                    if (!res || !TXrecidvalid(res)) continue;
                    size_t n_elems = 0;
                    void *raw = getfld(fld, &n_elems);
                    if (!raw || n_elems == 0) continue;
                    size_t cells = n_elems;
                    if (t == FTN_BYTE) {
                        size_t elsz = vec_dtype_elsz(column_dtype);
                        if (elsz == 0 || (n_elems % elsz) != 0) continue;
                        cells = n_elems / elsz;
                    }
                    if ((int)cells != h->base.dim) continue;
                    if (vec_convert_to_f32(column_dtype, raw, cells, h->base.dim,
                                           /*scale*/0.0f, /*zp*/0, qbuf) != 0)
                        continue;
                    float score = 0.0f;
                    if (use_dot) {
                        for (int j = 0; j < h->base.dim; j++)
                            score += query[j] * qbuf[j];
                    } else {
                        for (int j = 0; j < h->base.dim; j++) {
                            float d = query[j] - qbuf[j];
                            score += d * d;
                        }
                    }
                    merged[mlen].id = (vec_id_t)(uint64_t)r;
                    merged[mlen].score = score;
                    mlen++;
                }
                free(qbuf);
            }
        }
    }

    /* Sort merged by metric-natural score, then copy first k into
     * the caller's results buffer. */
    if (mlen > 1) {
        extern int vec_search_cmp_asc_(const void *, const void *);
        extern int vec_search_cmp_desc_(const void *, const void *);
        qsort(merged, mlen, sizeof(*merged),
              ascending ? vec_search_cmp_asc_ : vec_search_cmp_desc_);
    }

    size_t out = mlen < k ? mlen : k;
    for (size_t i = 0; i < out; i++) results[i] = merged[i];

    free(merged);
    free(tomb_v.data);
    free(newrec_v.data);
    free(keys); free(dists); free(qbuf_idx);
    return out;
}

/* Helpers used by hnsw_search_impl's btree walk + sort.  Defined at
 * file scope so they can be `extern` declared inline above (avoiding
 * forward decls littering the top of the file). */
void vec_recid_vec_push_(int64_t r, void *user)
{
    struct { int64_t *data; size_t len; size_t cap; } *v = user;
    if (v->len == v->cap) {
        size_t nc = v->cap ? v->cap * 2 : 16;
        int64_t *nd = (int64_t *)realloc(v->data, nc * sizeof(int64_t));
        if (!nd) return;     /* on failure, drop the entry */
        v->data = nd; v->cap = nc;
    }
    v->data[v->len++] = r;
}
int vec_int64_cmp_(const void *a, const void *b)
{
    int64_t la = *(const int64_t *)a, lb = *(const int64_t *)b;
    return (la > lb) - (la < lb);
}
int vec_search_cmp_asc_(const void *a, const void *b)
{
    float fa = ((const vec_search_result_t *)a)->score;
    float fb = ((const vec_search_result_t *)b)->score;
    return (fa > fb) - (fa < fb);
}
int vec_search_cmp_desc_(const void *a, const void *b)
{
    float fa = ((const vec_search_result_t *)a)->score;
    float fb = ((const vec_search_result_t *)b)->score;
    return (fa < fb) - (fa > fb);
}

void
TXvecCloseAll(void)
{
    struct TXvecHandleBase *hb = vec_handle_cache;
    while (hb) {
        struct TXvecHandleBase *next = hb->next;
        vec_backend_for(hb->backend)->close((TXvecHandle *)hb);
        hb = next;
    }
    vec_handle_cache = NULL;
}

void
TXvecInvalidateHandle(const char *indfile)
{
    struct TXvecHandleBase **pp = &vec_handle_cache;
    if (!indfile) return;
    while (*pp) {
        struct TXvecHandleBase *hb = *pp;
        if (strcmp(hb->path, indfile) == 0) {
            *pp = hb->next;
            vec_backend_for(hb->backend)->close((TXvecHandle *)hb);
            break;
        }
        pp = &hb->next;
    }
    /* The WAL TABLE drop is wired separately via TXvecDropAux,
     * called from droptbl.c::TXdropdindex while the DDIC is still
     * available. */
}

/* ----- Flush API ---------------------------------------------------- */

/* HNSW writes are durable on every INSERT (via `_T.btr` newrec +
 * usearch_save_atomic on OPTIMIZE), so flush is a no-op. */
static int
hnsw_flush_impl(DDIC *ddic, TXvecHandle *h_)
{
    (void)ddic; (void)h_;
    return 0;
}

int
TXvecFlush(DDIC *ddic, const char *indfile)
{
    if (!indfile) return -1;
    for (struct TXvecHandleBase *hb = vec_handle_cache; hb; hb = hb->next) {
        if (strcmp(hb->path, indfile) == 0)
            return vec_backend_for(hb->backend)->flush(ddic, (TXvecHandle *)hb);
    }
    return 0;       /* not in cache → nothing to flush */
}

int
TXvecFlushAll(DDIC *ddic)
{
    int rc = 0;
    for (struct TXvecHandleBase *hb = vec_handle_cache; hb; hb = hb->next) {
        /* Prefer the caller's DDIC, fall back to the one captured at
         * the handle's last open.  If both are NULL, the backend's
         * flush handler is expected to skip SYSINDEX-side housekeeping
         * but still persist the on-disk artifact (next open will
         * reconcile to verify and clear). */
        DDIC *useDdic = ddic ? ddic : hb->ddic;
        if (vec_backend_for(hb->backend)->flush(useDdic, (TXvecHandle *)hb) != 0)
            rc = -1;
    }
    return rc;
}

/* Exit-time DDIC pointer.  rampart-sql arranges to call TXvecFlushAll
 * with this DDIC via its own cmdline.c add_exit_func registration; we
 * don't reference cmdline.c symbols from libtexisapi because the same
 * library is linked into tsql/texislockd which don't have those.
 */
static DDIC *vec_exit_hook_ddic = NULL;

void
TXvecRegisterExitHook(DDIC *ddic)
{
    /* Idempotent: the embedder may call this multiple times (e.g. once
     * per Sql.connect).  We just remember the most recently-registered
     * DDIC for the eventual flush.  In multi-process mode each child
     * has its own static and registers independently. */
    vec_exit_hook_ddic = ddic;
}

/* ---- Embed callback registry --------------------------------------
 *
 * Process-globals; see vecindex_internal.h for contract.  Each forked
 * helper process has its own copy.  Not thread-safe by design: the SQL
 * layer ensures sql.set({llamaEmbed:...}) is the only registrar and
 * helper-child registration happens once at open or first exec. */

static TXembedFunc g_embed_fn = NULL;
static void       *g_embed_ud = NULL;

void
TXregisterEmbedFunc(TXembedFunc fn, void *user_data)
{
    g_embed_fn = fn;
    g_embed_ud = user_data;
}

TXembedFunc
TXgetEmbedFunc(void **user_data_out)
{
    if (user_data_out) *user_data_out = g_embed_ud;
    return g_embed_fn;
}

void
TXclearEmbedFunc(void)
{
    g_embed_fn = NULL;
    g_embed_ud = NULL;
}

/* ---- embed() SQL scalar function -------------------------------------
 *
 *   embed(text)              -> varbyte (f16 bytes, default)
 *   embed(text, 'f16')       -> varbyte with f16 bytes
 *   embed(text, 'f32')       -> varbyte with f32 bytes
 *   embed(text, 'bf16')      -> varbyte with bf16 bytes
 *   embed(text, 'f64')       -> varbyte with f64 bytes
 *
 * Returns a varbyte FLD (declared FTN_BYTE | DDVARBIT) so it works with
 * any vec column type via fobyby's byte→vec assignment: fobyby reads
 * the column's elsz, recomputes n = bytes/elsz, and stores the bytes
 * unmodified.  This means the caller must pick an embed() dtype that
 * matches the destination column dtype (e.g., use 'f32' for varvecF32,
 * leave default for varvecF16 or varbyte storage).
 *
 * f16 default chosen for storage efficiency — 2× smaller than f32 with
 * negligible cosine-similarity loss on L2-normalized embedding vectors.
 *
 * No callback registered → MERR + error return.
 */
int
TXsqlFunc_embed(FLD *f1, FLD *f2)
{
    void        *ud = NULL;
    TXembedFunc  fn = TXgetEmbedFunc(&ud);
    float       *vec_f32 = NULL;
    size_t       dim;
    char        *src;
    size_t       slen;
    int          dtype = FTN_VEC_F16;   /* default */

    if (!fn)
    {
        putmsg(MERR + UGE, "embed",
               "No embed function registered; set llamaEmbed first");
        return FOP_EINVAL;
    }

    /* Optional 2nd arg: dtype string. */
    if (f2 != FLDPN && !TXfldIsNull(f2))
    {
        const char *dt = (const char *)getfld(f2, NULL);
        if (dt)
        {
            if      (!strcasecmp(dt, "f16"))  dtype = FTN_VEC_F16;
            else if (!strcasecmp(dt, "f32"))  dtype = FTN_VEC_F32;
            else if (!strcasecmp(dt, "f64"))  dtype = FTN_VEC_F64;
            else if (!strcasecmp(dt, "bf16")) dtype = FTN_VEC_BF16;
            else {
                putmsg(MERR + UGE, "embed",
                       "Unknown dtype `%s' — expected one of "
                       "'f16', 'f32', 'f64', 'bf16'", dt);
                return FOP_EINVAL;
            }
        }
    }

    if (TXfldIsNull(f1))
    {
        TXfldSetNull(f1);
        return FOP_EOK;
    }
    src = (char *)getfld(f1, &slen);
    if (!src || slen == 0)
    {
        TXfldSetNull(f1);
        return FOP_EOK;
    }

    dim = fn(ud, src, slen, &vec_f32);
    if (dim == 0 || !vec_f32)
    {
        putmsg(MERR + FRE, "embed", "embed callback returned no vector");
        if (vec_f32) free(vec_f32);
        return FOP_EINVAL;
    }

    /* Convert f32 callback output to the requested dtype's bytes.  Output
     * goes into a fresh malloc — vec_f32 is freed after the convert. */
    size_t out_elsz =
        (dtype == FTN_VEC_F16  || dtype == FTN_VEC_BF16) ? 2 :
        (dtype == FTN_VEC_F32) ? 4 :
        (dtype == FTN_VEC_F64) ? 8 : 4;
    size_t out_bytes = dim * out_elsz;
    void *bytes = malloc(out_bytes);
    if (!bytes) {
        free(vec_f32);
        putmsg(MERR + MAE, "embed", "out of memory");
        return FOP_ENOMEM;
    }

    switch (dtype) {
    case FTN_VEC_F32:
        memcpy(bytes, vec_f32, out_bytes);
        break;
    case FTN_VEC_F16:
        rpvec_f32_to_f16(vec_f32, (uint16_t *)bytes, dim);
        break;
    case FTN_VEC_BF16:
        rpvec_f32_to_bf16(vec_f32, (uint16_t *)bytes, dim);
        break;
    case FTN_VEC_F64: {
        double *d = (double *)bytes;
        for (size_t i = 0; i < dim; i++) d[i] = (double)vec_f32[i];
        break;
    }
    default:
        free(vec_f32); free(bytes);
        return FOP_EINVAL;
    }
    free(vec_f32);

    /* Emit as varvec*<dtype> so `select embed(?)` returns a typed vec
     * with .dim and .toNumbers().  Assignment to a same-dtype column
     * is a no-op identity copy; mismatched-dtype column assignments
     * fail fast (texis's vec→vec ASN only handles matching dtypes).
     * Callers wanting cross-dtype storage should pick the right dtype
     * arg up-front. */
    freeflddata(f1);
    f1->type = dtype | DDVARBIT;
    f1->elsz = out_elsz;
    setfldandsize(f1, bytes, out_bytes + 1, FLD_FORCE_NORMAL);
    return FOP_EOK;
}

DDIC *
TXvecGetExitHookDDIC(void)
{
    return vec_exit_hook_ddic;
}

/* ----- Planner integration ------------------------------------------ */

int
TXvecScoreIndex(const char *sysindexFields, const char *sysindexParams,
                DBTBL *dbtbl, int fldOp, FLD *param, int paramIsRHS)
{
    (void)sysindexFields; (void)sysindexParams; (void)dbtbl;
    (void)param; (void)paramIsRHS;
    if (fldOp == FOP_MMV) return 100;
    return 0;
}

/* ----- Access function: ixvecindex --------------------------------- */

IINDEX *
TXvecIxVecIndex(const char *iname, const char *sysindexParams,
                FLD *infld, const char *fname, DBTBL *dbtbl,
                int op, int *cop)
{
    static const char fn[] = "TXvecIxVecIndex";
    IINDEX *ix = NULL;
    BTREE *bt = NULL;
    float *qbuf = NULL;
    void  *qbuf_idx = NULL;             /* HNSW internal use only — kept
                                         * for cleanup symmetry; backend
                                         * does its own quantization */
    vec_search_result_t *res = NULL;
    EPI_HUGEUINT cnt = 0;

    (void)sysindexParams; (void)fname;
    if (cop) *cop = 0;

    if (op != FOP_MMV) goto err;
    if (!infld || !iname) goto err;
    if (TXfldIsNull(infld)) goto err;

    /* Open the vec handle first — we need hb->dim for both the
     * normal vec/byte path AND the auto-embed path below. */
    TXvecHandle *h_ = TXvecOpen(dbtbl ? dbtbl->ddic : NULL, iname,
                                sysindexParams);
    if (!h_) goto err;
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h_;

    int t = infld->type & DDTYPEBITS;

    /* If we get a string RHS here it means predopt's auto-embed
     * pre-rewrite didn't fire (e.g. embed callback was registered
     * after prep).  Fall back: embed inline.  Note this only fixes
     * the index path — per-row post-process will still see the
     * original string and reject everything, so the result will be
     * empty.  The proper fix is predopt's pre-rewrite (below). */
    if (!FTN_IS_VEC_OR_BYTE(t)) {
        if (t == FTN_CHAR) {
            void *embed_ud = NULL;
            TXembedFunc embed_fn = TXgetEmbedFunc(&embed_ud);
            if (embed_fn) {
                size_t text_len = 0;
                const char *text = (const char *)getfld(infld, &text_len);
                if (text && text_len > 0) {
                    float *evec = NULL;
                    size_t edim = embed_fn(embed_ud, text, text_len, &evec);
                    if (edim == (size_t)hb->dim && evec) {
                        qbuf = evec;
                        goto have_qbuf;
                    }
                    if (evec) free(evec);
                }
            }
        }
        goto err;
    }

    /* Extract + convert query to f32.  Backend-agnostic: TXvecSearch
     * dispatches to the right backend, and each backend's search slot
     * handles its own internal quantization (HNSW: query → i8/u8 if
     * index is quantized; IVFPQ: f32 throughout). */
    {
        size_t qn = 0;
        void *qraw = getfld(infld, &qn);
        if (!qraw || qn == 0) goto err;

        int qDtype = FTN_IS_VEC(t) ? t : hb->dtype;
        size_t qCells = qn;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(qDtype);
            if (elsz == 0 || (qn % elsz) != 0) goto err;
            qCells = qn / elsz;
        }
        if ((int)qCells != hb->dim) {
            if (TXverbosity > 0)
                putmsg(MINFO, fn,
                    "INDEX_VEC dim=%d but query dim=%lu; falling back to brute force",
                    hb->dim, (unsigned long)qCells);
            goto err;
        }

        qbuf = (float *)malloc((size_t)hb->dim * sizeof(float));
        if (!qbuf) goto err;
        /* For HNSW i8/u8 indexes the search slot quantizes internally
         * with its own quant_scale/quant_zp; pass 0/0 here since the
         * caller side doesn't have those (and shouldn't need to). */
        if (vec_convert_to_f32(qDtype, qraw, qCells, hb->dim,
                               0.0f, 0, qbuf) < 0) goto err;
    }

have_qbuf: ;

    /* Top-K search via the dispatcher.  Pool size from likevRows;
     * per-query expansion (ef for HNSW, nprobe for IVFPQ) from
     * likevEf — backend interprets in its own units.
     *
     * The empty statement after the label is required: this TU compiles
     * under -std=c89-ish strictness on some hosts, where a label must
     * be followed by a statement (not a declaration). */
    size_t k = (TXnlikevhits > 0) ? (size_t)TXnlikevhits : 1000;
    size_t ef = (TXlikevef > 0) ? (size_t)TXlikevef : 0;
    size_t got;
    res = (vec_search_result_t *)
        malloc(k * sizeof(vec_search_result_t));
    if (!res) goto err;
    got = TXvecSearch(h_, dbtbl, fname, qbuf, k, ef, res);
    if (got == SIZE_MAX) { free(res); goto err; }

    /* Materialize into in-memory btree, recid as btloc, exact-rank
     * key so btree iteration order = ORDER BY $rank DESC.
     *
     * For each candidate we fetch the stored vec, compute the dot
     * product against the query at the column's stored precision
     * (matching FOP_MMV's per-row scoring exactly), scale to
     * [-100000, 100000] like FOP_MMV does, and use
     * (100000 - scaled) as the btree key — so the BT_UNSIGNED
     * ASC iteration delivers best-first.  Same shape as LIKEP's
     * rank-keyed btree (3dbindex.c:2034).
     *
     * Cost: one row fetch + one dot product per candidate at
     * dispatcher time — same work FOP_MMV does later during WHERE
     * eval, so it's a duplicated computation (a few ms at most for
     * the default likevRows=1000 candidate pool).  In exchange:
     * row order matches `ORDER BY $rank DESC` without the user
     * having to write it.
     */
    bt = openbtree(NULL, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED,
                   O_RDWR | O_CREAT);
    if (!bt) { free(res); goto err; }

    {
        FLD *colFld = (dbtbl && fname) ? dbnametofld(dbtbl, (char *)fname) : NULL;
        int colT = colFld ? (colFld->type & DDTYPEBITS) : 0;
        int column_dtype = (colT == FTN_BYTE) ? hb->dtype : colT;
        size_t col_elsz = vec_dtype_elsz(column_dtype);
        /* Scratch buffer for the converted-to-f32 stored vec.  Pattern
         * matches the newrec scan loop above (line ~1691). */
        float *sbuf = NULL;
        if (colFld && col_elsz > 0)
            sbuf = (float *)malloc((size_t)hb->dim * sizeof(float));

        for (size_t i = 0; i < got; i++) {
            /* DOT metric: filter zero-or-negative correlation
             * (matches the legacy backend-score filter — keeps
             * cnt accounting unchanged for callers that rely on it). */
            if (hb->metric == VEC_METRIC_DOT && res[i].score <= 0.0f)
                continue;

            /* Default to the backend's score if anything goes wrong
             * fetching/converting the stored vec. */
            double exact_score = (double)res[i].score;
            if (sbuf) {
                BTLOC bl_fetch;
                memset(&bl_fetch, 0, sizeof(bl_fetch));
                bl_fetch.off = (EPI_OFF_T)(uint64_t)res[i].id;
                RECID *rrc = gettblrow(dbtbl->tbl, &bl_fetch);
                if (rrc && TXrecidvalid(rrc)) {
                    size_t n_bytes = 0;
                    void *raw = getfld(colFld, &n_bytes);
                    size_t cells = n_bytes;
                    if (raw && n_bytes > 0 && colT == FTN_BYTE) {
                        if (col_elsz > 0 && (n_bytes % col_elsz) == 0)
                            cells = n_bytes / col_elsz;
                        else
                            raw = NULL;
                    }
                    if (raw && (int)cells == hb->dim &&
                        vec_convert_to_f32(column_dtype, raw, cells,
                                           hb->dim, 0.0f, 0, sbuf) == 0) {
                        double s = 0.0;
                        for (int j = 0; j < hb->dim; j++)
                            s += (double)qbuf[j] * (double)sbuf[j];
                        exact_score = s;
                    }
                }
            }
            /* Clamp + scale exactly like FOP_MMV (fldops.c:799-801). */
            if (exact_score >  1.0) exact_score =  1.0;
            if (exact_score < -1.0) exact_score = -1.0;
            int32_t scaled = (int32_t)(exact_score * 100000.0);

            BTLOC bl;
            TXsetrecid(&bl, (EPI_OFF_T)(uint64_t)res[i].id);
            /* ASC-iteration ⇒ DESC-by-score: smaller key = higher
             * score.  Map [-100000, 100000] → [0, 200000] by
             * inverting: key = 100000 - scaled. */
            EPI_OFF_T key = (EPI_OFF_T)(100000 - scaled);
            btinsert(bt, &bl, sizeof(key), &key);
            cnt++;
        }
        if (sbuf) free(sbuf);
    }

    rewindbtree(bt);

    ix = openiindex();
    if (!ix) goto err;
#ifdef NEW_I
    ix->orig = dbidxfrombtree(bt, DBIDX_MEMORY);
    if (!ix->orig) goto err;
#else
    ix->orig = bt;
#endif
    bt = NULL;
    ix->cntorig = cnt;
    goto cleanup;

err:
    if (ix) ix = closeiindex(ix);
cleanup:
    if (bt) bt = closebtree(bt);
    free(qbuf);
    free(qbuf_idx);
    free(res);
    return ix;
}

/* ====================================================================
 * Public dispatchers + HNSW backend vtable
 * ==================================================================== */

/* Slot wrappers — public-API signature, delegate to HNSW impls. */

static void
hnsw_close_impl(TXvecHandle *h_)
{
    vec_handle_free((struct TXvecHnswHandle *)h_);
}

static int
hnsw_is_stale_impl(TXvecHandle *h_)
{
    return vec_handle_is_stale((const struct TXvecHnswHandle *)h_);
}

static void
hnsw_drop_aux_impl(DDIC *ddic, const char *indfile)
{
    (void)ddic;
    if (!indfile) return;
    char *tomb_base   = TXvecMakeBtreeBasePath(indfile, "_del");
    char *newrec_base = TXvecMakeBtreeBasePath(indfile, "_T");
    if (tomb_base)   { TXvecBtreeUnlink(tomb_base);   free(tomb_base); }
    if (newrec_base) { TXvecBtreeUnlink(newrec_base); free(newrec_base); }
}

/* HNSW vtable slot: optimize.
 *
 * Fold `_T.btr` newrec entries into the .vec via usearch_add, then
 * one save_atomic at the end (avoids rewriting the .vec for every
 * absorbed row).  Tombstones are also applied: each newrec recid that
 * was simultaneously in tomb_bt (UPDATE-in-place) gets removed via
 * usearch_remove and dropped from the tombstone btree.  After absorb:
 * truncate `_T.btr`.  Mirrors texis fulltext's `wtix_getnewlist` path. */
/* HNSW vtable slot: optimize.
 *
 * Atomic-swap design: the dispatcher allocates a Tnnnn temp basename
 * and we build new artifacts at Tnnnn_*.  Caller atomic-swaps Tnnnn
 * over the live files at commit (under brief table W_LCK) and bumps
 * the cross-process index version counter.
 *
 *   1. Snapshot live `_T.btr' newrec recids.
 *   2. Load a FRESH usearch index from live .vec (don't pollute
 *      h->index — if we fail the cached handle is still consistent
 *      with the on-disk live state).
 *   3. Reserve capacity, walk the table sequentially, absorb each
 *      recid that's in the snapshot (UPDATE-in-place: usearch_remove
 *      then add).
 *   4. save_atomic the absorbed graph to Tnnnn.vec.
 *   5. Create empty Tnnnn_T.btr and Tnnnn_del.btr — the dispatcher's
 *      carry-forward populates them at commit time from live aux
 *      btrees (filtered by the absorbed set we return). */
static int
hnsw_optimize_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                   const char *field, const char *tempBase,
                   TXindOpts *options,
                   int64_t **out_absorbed, size_t *out_n_absorbed)
{
    static const char fn[] = "TXvecOptimize(hnsw)";
    (void)ddic;
    struct TXvecHnswHandle *h = (struct TXvecHnswHandle *)h_;
    if (!h || !dbtbl || !field || !tempBase) return -1;
    *out_absorbed = NULL; *out_n_absorbed = 0;

    FLD *fld = dbnametofld(dbtbl, (char *)field);
    if (!fld) {
        putmsg(MERR + UGE, fn, "field `%s' not found", field);
        return -1;
    }
    int t = fld->type & DDTYPEBITS;
    if (!FTN_IS_VEC_OR_BYTE(t)) {
        putmsg(MERR + UGE, fn,
            "field `%s' is not a vector or varbyte type", field);
        return -1;
    }

    /* Snapshot the live newrec recids — these are the rows the build
     * phase will absorb into sealed. */
    struct { int64_t *data; size_t len; size_t cap; } newrec_v = {NULL, 0, 0};
    extern void vec_recid_vec_push_(int64_t r, void *user);
    extern int  vec_int64_cmp_(const void *a, const void *b);
    TXvecBtreeWalkRecids(h->newrec_base, vec_recid_vec_push_, &newrec_v);
    if (newrec_v.len > 0)
        qsort(newrec_v.data, newrec_v.len, sizeof(int64_t), vec_int64_cmp_);

    char tempVec[PATH_MAX], tempT[PATH_MAX], tempD[PATH_MAX];
    if (snprintf(tempVec, sizeof(tempVec), "%s.vec", tempBase) >= (int)sizeof(tempVec) ||
        snprintf(tempT,   sizeof(tempT),   "%s_T",   tempBase) >= (int)sizeof(tempT) ||
        snprintf(tempD,   sizeof(tempD),   "%s_del", tempBase) >= (int)sizeof(tempD)) {
        putmsg(MERR + UGE, fn, "tempBase path too long");
        free(newrec_v.data);
        return -1;
    }

    /* SYSUPDATE: stage 1 of 3 (load fresh graph from disk). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "load fresh graph");

    /* Load a fresh usearch graph from live .vec.  Don't mutate h->index;
     * leave the cached handle consistent with what's still on disk
     * until commit. */
    const char *uerr = NULL;
    usearch_init_options_t opts;
    memset(&opts, 0, sizeof(opts));
    usearch_metadata(h->fpath, &opts, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_metadata: %s", uerr);
        free(newrec_v.data);
        return -1;
    }
    usearch_index_t fresh = usearch_init(&opts, &uerr);
    if (!fresh || uerr) {
        putmsg(MERR + UGE, fn, "usearch_init: %s", uerr ? uerr : "(null)");
        if (fresh) usearch_free(fresh, &uerr);
        free(newrec_v.data);
        return -1;
    }
    usearch_load(fresh, h->fpath, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_load: %s", uerr);
        usearch_free(fresh, &uerr);
        free(newrec_v.data);
        return -1;
    }

    /* Reserve capacity for the delta inserts. */
    if (newrec_v.len > 0) {
        size_t cur = usearch_size(fresh, &uerr);     uerr = NULL;
        size_t cap = usearch_capacity(fresh, &uerr); uerr = NULL;
        if (cur + newrec_v.len > cap) {
            size_t want = cap * 2;
            if (want < cur + newrec_v.len) want = cur + newrec_v.len + 16;
            usearch_reserve(fresh, want, &uerr);
            if (uerr) {
                putmsg(MERR + UGE, fn, "usearch_reserve: %s", uerr);
                usearch_free(fresh, &uerr);
                free(newrec_v.data);
                return -1;
            }
        }
    }

    int column_dtype = (t == FTN_BYTE) ? h->base.dtype : t;
    float *qbuf = (float *)malloc((size_t)h->base.dim * sizeof(float));
    void  *qbuf_idx = NULL;
    if (h->base.dtype == FTN_VEC_I8 || h->base.dtype == FTN_VEC_U8)
        qbuf_idx = malloc((size_t)h->base.dim * vec_dtype_elsz(h->base.dtype));
    int64_t *absorbed = (newrec_v.len > 0)
        ? (int64_t *)malloc(newrec_v.len * sizeof(int64_t)) : NULL;
    if (!qbuf || ((h->base.dtype == FTN_VEC_I8 || h->base.dtype == FTN_VEC_U8)
                  && !qbuf_idx) || (newrec_v.len > 0 && !absorbed)) {
        putmsg(MERR + MAE, fn, "alloc qbuf/absorbed");
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx); free(newrec_v.data); free(absorbed);
        return -1;
    }
    size_t absorbed_n = 0;

    /* SYSUPDATE: stage 2 of 3 (encode delta). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                            "encode delta");

    /* Walk the table sequentially, absorbing rows in the snapshot.
     * Meter sized to table file bytes; ticks per visited recid offset.
     * Same shape as the IVFPQ OPTIMIZE encode meter. */
    METER *encode_meter = NULL;
    EPI_OFF_T encode_total_bytes = 0;
    if (newrec_v.len > 0 && options
        && options->indexmeter != TXMDT_NONE) {
        EPI_STAT_S st;
        EPI_OFF_T total = 0;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            total = (EPI_OFF_T)st.st_size;
        encode_total_bytes = total;
        if (total > 0)
            encode_meter = openmeter(
                "INDEX_VEC hnsw OPTIMIZE (encode delta):",
                options->indexmeter,
                MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                (EPI_HUGEINT)total);
    } else {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            encode_total_bytes = (EPI_OFF_T)st.st_size;
    }
    if (newrec_v.len > 0) {
        RECID *recid;
        TXrewinddbtbl(dbtbl);
        while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
            /* Snapshot recid->off once.  recid points to a process-static
             * RECID; any internal SQL call (TXsysupdateProgress) stomps it.
             * See project_texis_recid_static memo. */
            int64_t r = (int64_t)(uint64_t)recid->off;
            if (encode_meter)
                METER_UPDATEDONE(encode_meter, (EPI_HUGEINT)r);
            if (encode_total_bytes > 0)
                TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                    (double)r / (double)encode_total_bytes);
            if (!bsearch(&r, newrec_v.data, newrec_v.len,
                         sizeof(int64_t), vec_int64_cmp_))
                continue;
            size_t n_elems = 0;
            void *raw = getfld(fld, &n_elems);
            if (!raw || n_elems == 0) continue;
            size_t cells = n_elems;
            if (t == FTN_BYTE) {
                size_t elsz = vec_dtype_elsz(column_dtype);
                if (elsz == 0 || (n_elems % elsz) != 0) continue;
                cells = n_elems / elsz;
            }
            if ((int)cells != h->base.dim) continue;
            if (usearch_contains(fresh, (usearch_key_t)(uint64_t)r, &uerr)) {
                usearch_remove(fresh, (usearch_key_t)(uint64_t)r, &uerr);
                uerr = NULL;
            }
            if (vec_add_one(fresh, (usearch_key_t)(uint64_t)r, h->base.dim,
                            h->base.dtype, h->quant_scale, h->quant_zp,
                            column_dtype, raw, cells,
                            qbuf, qbuf_idx, &uerr) < 0) {
                if (uerr) {
                    putmsg(MWARN, fn, "usearch_add for recid %lld: %s",
                           (long long)r, uerr);
                    uerr = NULL;
                }
                continue;
            }
            absorbed[absorbed_n++] = r;
        }
    }
    if (encode_meter) { meter_end(encode_meter); closemeter(encode_meter); }

    /* SYSUPDATE: stage 3 of 3 (save). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 3,
                            "save");

    /* Save the absorbed graph to Tnnnn.vec. */
    if (save_atomic(fresh, tempVec, fn) != 0) {
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx); free(newrec_v.data); free(absorbed);
        return -1;
    }
    usearch_free(fresh, &uerr);

    /* Empty Tnnnn aux btrees — dispatcher carry-forward fills them in
     * at commit from live aux btrees, filtered by the absorbed set. */
    TXvecBtreeUnlink(tempT);
    TXvecBtreeUnlink(tempD);
    if (TXvecBtreeCreateEmpty(tempT) != 0 ||
        TXvecBtreeCreateEmpty(tempD) != 0) {
        putmsg(MERR + UGE, fn, "create empty Tnnnn aux btrees failed");
        free(qbuf); free(qbuf_idx); free(newrec_v.data); free(absorbed);
        return -1;
    }

    free(qbuf); free(qbuf_idx); free(newrec_v.data);

    if (absorbed_n > 0)
        qsort(absorbed, absorbed_n, sizeof(int64_t), vec_int64_cmp_);
    *out_absorbed = absorbed;
    *out_n_absorbed = absorbed_n;

    putmsg(MINFO, fn,
        "INDEX_VEC hnsw OPTIMIZE: absorbed %lu rows into temp `%s.vec'",
        (unsigned long)absorbed_n, tempBase);
    return 0;
}

/* HNSW vtable slot: rebuild.
 *
 * Build a fresh HNSW graph from the current table contents into the
 * dispatcher-supplied Tnnnn temp basename:
 *   1. Initialize a new usearch index with the same params as the
 *      existing handle (dim, M, ef_construction, metric, dtype).
 *   2. Walk the table, encode each row's vector, usearch_add.
 *   3. save_atomic to Tnnnn.vec.
 *   4. Empty Tnnnn_T.btr and Tnnnn_del.btr (the dispatcher's
 *      carry-forward at commit will pick up any concurrent
 *      INSERTs/DELETEs whose recids weren't in our walk's absorbed
 *      set). */
static int
hnsw_rebuild_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                  const char *field, const TXvecParams *vp,
                  const char *tempBase, TXindOpts *options,
                  int64_t **out_absorbed, size_t *out_n_absorbed)
{
    static const char fn[] = "TXvecRebuild(hnsw)";
    (void)vp;
    struct TXvecHnswHandle *h = (struct TXvecHnswHandle *)h_;
    if (!h || !dbtbl || !field || !tempBase) return -1;
    *out_absorbed = NULL; *out_n_absorbed = 0;

    /* SYSUPDATE: stage 1 of 3 (init fresh graph). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "init fresh graph");

    FLD *fld = dbnametofld(dbtbl, (char *)field);
    if (!fld) {
        putmsg(MERR + UGE, fn, "field `%s' not found", field);
        return -1;
    }
    int t = fld->type & DDTYPEBITS;
    if (!FTN_IS_VEC_OR_BYTE(t)) {
        putmsg(MERR + UGE, fn,
            "field `%s' is not a vector or varbyte type", field);
        return -1;
    }

    char tempVec[PATH_MAX], tempT[PATH_MAX], tempD[PATH_MAX];
    if (snprintf(tempVec, sizeof(tempVec), "%s.vec", tempBase) >= (int)sizeof(tempVec) ||
        snprintf(tempT,   sizeof(tempT),   "%s_T",   tempBase) >= (int)sizeof(tempT) ||
        snprintf(tempD,   sizeof(tempD),   "%s_del", tempBase) >= (int)sizeof(tempD)) {
        putmsg(MERR + UGE, fn, "tempBase path too long");
        return -1;
    }

    /* Mirror the existing handle's usearch params for the fresh
     * index.  Pull metadata from the live .vec; that's the simplest
     * way to preserve dim/M/efc/metric without re-deriving from PARAMS. */
    const char *uerr = NULL;
    usearch_init_options_t opts;
    memset(&opts, 0, sizeof(opts));
    usearch_metadata(h->fpath, &opts, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_metadata: %s", uerr);
        return -1;
    }
    usearch_index_t fresh = usearch_init(&opts, &uerr);
    if (!fresh || uerr) {
        putmsg(MERR + UGE, fn, "usearch_init: %s", uerr ? uerr : "(null)");
        if (fresh) usearch_free(fresh, &uerr);
        return -1;
    }

    int column_dtype = (t == FTN_BYTE) ? h->base.dtype : t;
    float *qbuf = (float *)malloc((size_t)h->base.dim * sizeof(float));
    void  *qbuf_idx = NULL;
    if (h->base.dtype == FTN_VEC_I8 || h->base.dtype == FTN_VEC_U8)
        qbuf_idx = malloc((size_t)h->base.dim * vec_dtype_elsz(h->base.dtype));
    if (!qbuf || ((h->base.dtype == FTN_VEC_I8 || h->base.dtype == FTN_VEC_U8)
                  && !qbuf_idx)) {
        putmsg(MERR + MAE, fn, "alloc qbuf");
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx);
        return -1;
    }

    /* usearch needs an explicit reservation before adds; without it
     * the first add aborts.  Start at a reasonable default and grow
     * 2x when we hit the cap. */
    size_t reserved = 1024;
    usearch_reserve(fresh, reserved, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_reserve(initial): %s", uerr);
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx);
        return -1;
    }

    /* Allocate absorbed buffer; grow as we encode.  No cheap row-count
     * available at this layer, so start small and realloc as we go. */
    size_t absorbed_cap = 64;
    int64_t *absorbed = (int64_t *)malloc(absorbed_cap * sizeof(int64_t));
    if (!absorbed) {
        putmsg(MERR + MAE, fn, "alloc absorbed");
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx);
        return -1;
    }
    size_t absorbed_n = 0;

    /* SYSUPDATE: stage 2 of 3 (encode rows). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                            "encode rows");

    /* Meter sized to table file bytes; ticks per visited recid offset.
     * Mirrors the HNSW OPTIMIZE encode meter. */
    METER *encode_meter = NULL;
    EPI_OFF_T encode_total_bytes = 0;
    if (options && options->indexmeter != TXMDT_NONE) {
        EPI_STAT_S st;
        EPI_OFF_T total = 0;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            total = (EPI_OFF_T)st.st_size;
        encode_total_bytes = total;
        if (total > 0)
            encode_meter = openmeter(
                "INDEX_VEC hnsw REBUILD (encode rows):",
                options->indexmeter,
                MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                (EPI_HUGEINT)total);
    } else {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            encode_total_bytes = (EPI_OFF_T)st.st_size;
    }
    RECID *recid;
    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        /* Snapshot recid->off once (process-static; SQL calls stomp it). */
        int64_t r = (int64_t)(uint64_t)recid->off;
        if (encode_meter)
            METER_UPDATEDONE(encode_meter, (EPI_HUGEINT)r);
        if (encode_total_bytes > 0)
            TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                (double)r / (double)encode_total_bytes);
        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) continue;
        size_t cells = n_elems;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(column_dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) continue;
            cells = n_elems / elsz;
        }
        if ((int)cells != h->base.dim) continue;
        /* Double reservation if we're about to exceed it. */
        if (absorbed_n + 1 >= reserved) {
            reserved *= 2;
            usearch_reserve(fresh, reserved, &uerr);
            if (uerr) {
                putmsg(MWARN, fn, "usearch_reserve(grow %zu): %s",
                       reserved, uerr);
                uerr = NULL;
            }
        }
        if (vec_add_one(fresh, (usearch_key_t)(uint64_t)r, h->base.dim,
                        h->base.dtype, h->quant_scale, h->quant_zp,
                        column_dtype, raw, cells,
                        qbuf, qbuf_idx, &uerr) < 0) {
            if (uerr) {
                putmsg(MWARN, fn, "usearch_add for recid %lld: %s",
                       (long long)r, uerr);
                uerr = NULL;
            }
            continue;
        }
        if (absorbed_n == absorbed_cap) {
            size_t newcap = absorbed_cap * 2;
            int64_t *p = (int64_t *)realloc(absorbed, newcap * sizeof(int64_t));
            if (!p) { putmsg(MERR + MAE, fn, "realloc absorbed"); break; }
            absorbed = p; absorbed_cap = newcap;
        }
        absorbed[absorbed_n++] = r;
    }
    if (encode_meter) { meter_end(encode_meter); closemeter(encode_meter); }

    /* SYSUPDATE: stage 3 of 3 (save). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 3, "save");

    if (save_atomic(fresh, tempVec, fn) != 0) {
        usearch_free(fresh, &uerr);
        free(qbuf); free(qbuf_idx); free(absorbed);
        return -1;
    }
    usearch_free(fresh, &uerr);

    /* Empty Tnnnn aux btrees. */
    (void)TXvecBtreeUnlink(tempT);
    (void)TXvecBtreeUnlink(tempD);
    if (TXvecBtreeCreateEmpty(tempT) != 0 ||
        TXvecBtreeCreateEmpty(tempD) != 0) {
        putmsg(MERR + UGE, fn, "create empty Tnnnn aux btrees failed");
        free(qbuf); free(qbuf_idx); free(absorbed);
        return -1;
    }

    free(qbuf); free(qbuf_idx);

    extern int vec_int64_cmp_(const void *a, const void *b);
    if (absorbed_n > 0)
        qsort(absorbed, absorbed_n, sizeof(int64_t), vec_int64_cmp_);
    *out_absorbed = absorbed;
    *out_n_absorbed = absorbed_n;

    putmsg(MINFO, fn,
        "INDEX_VEC hnsw REBUILD: encoded %lu rows into temp `%s.vec'",
        (unsigned long)absorbed_n, tempBase);
    return 0;
}

/* The HNSW backend table — populated entirely with hnsw_*_impl static
 * funcs.  Picked up by vec_backend_for(VEC_BACKEND_HNSW). */
const TXvecBackend TXvecHnswBackend = {
    .create   = hnsw_create_impl,
    .open     = hnsw_open_impl,
    .close    = hnsw_close_impl,
    .search   = hnsw_search_impl,
    .add_row  = hnsw_add_row_impl,
    .del_row  = hnsw_del_row_impl,
    .flush    = hnsw_flush_impl,
    .drop_aux = hnsw_drop_aux_impl,
    .is_stale = hnsw_is_stale_impl,
    .optimize = hnsw_optimize_impl,
    .rebuild  = hnsw_rebuild_impl,
};

/* Public dispatchers.  Each opens the right backend's handle (via
 * TXvecOpen which dispatches at the cache-miss point) and forwards. */

int
TXvecAddRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field, RECID *recid)
{
    if (!dbtbl || !indfile || !field || !recid) return -1;
    TXvecHandle *h = TXvecOpen(ddic, indfile, NULL);
    if (!h) return -1;
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h;
    return vec_backend_for(hb->backend)->add_row(ddic, h, dbtbl, field, recid);
}

int
TXvecDelRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field, RECID *recid)
{
    if (!indfile || !recid) return -1;
    TXvecHandle *h = TXvecOpen(ddic, indfile, NULL);
    if (!h) return -1;
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h;
    return vec_backend_for(hb->backend)->del_row(ddic, h, dbtbl, field, recid);
}

size_t
TXvecSearch(TXvecHandle *h, DBTBL *dbtbl, const char *field,
            const float *query, size_t k, size_t ef,
            vec_search_result_t *results)
{
    if (!h) return SIZE_MAX;
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h;
    return vec_backend_for(hb->backend)
            ->search(h, dbtbl, field, query, k, ef, results);
}

void
TXvecDropAux(DDIC *ddic, const char *indfile)
{
    if (!ddic || !indfile) return;
    /* DROP INDEX may race with backend identification — the SYSINDEX
     * entry's PARAMS string is generally still readable here, but the
     * cleanup operations are idempotent in either backend.  Try both;
     * each no-ops on missing artifacts. */
    TXvecHnswBackend.drop_aux(ddic, indfile);
#ifndef RP_NO_FAISS
    TXvecIvfpqBackend.drop_aux(ddic, indfile);
#endif
}

/* ====================================================================
 * Atomic ALTER INDEX (OPTIMIZE / REBUILD) plumbing
 *
 * Mirrors the fulltext (updindex.c) flow:
 *   Phase 1.  Allocate a unique Tnnnn basename in the live index's
 *             directory.  Insert an INDEX_TEMP SYSINDEX row pointing
 *             at it via TXcreateTempIndexOrTableEntry; that row +
 *             Tnnnn.PID lockfile let TXdocleanup reap orphan files
 *             after a crashed build.  No exclusive lock held during
 *             the lockfile creation other than the brief SYSINDEX
 *             write lock TXcreateTempIndexOrTableEntry takes itself.
 *   Phase 2.  Backend builds vec artifacts at Tnnnn_*ext.  No
 *             exclusive lock held; live searches keep hitting the
 *             original (untouched) files.
 *   Phase 3.  Brief commit under table W_LCK:
 *               - Invalidate this process's vec_handle_cache entry
 *                 (so subsequent opens pick up the renamed files).
 *               - rename(2) Tnnnn_*ext → live_*ext for each backend
 *                 extension that exists at Tnnnn (ENOENT-tolerant).
 *               - Delete the INDEX_TEMP SYSINDEX row.
 *               - TXtouchindexfile bumps the cross-process version
 *                 counter, prompting other processes to reopen.
 *
 * On error during Phase 2, vec_abort_temp_build() unlinks the Tnnnn
 * artifacts and removes the INDEX_TEMP row.
 *
 * The five vec extensions:
 *   .vec       (HNSW only)
 *   _H.idxpq   (IVFPQ only)
 *   _I.idxpq   (IVFPQ only)
 *   _T.btr     (both backends — newrec)
 *   _del.btr   (both backends — tombstones)
 * ==================================================================== */

static const char * const VEC_RENAME_EXTS[] = {
    ".vec",
    "_H.idxpq",
    "_I.idxpq",
    "_T.btr",
    "_del.btr",
    NULL
};

/* Allocate a unique Tnnnn temp basename in the same directory as
 * `liveBase`, and insert an INDEX_TEMP SYSINDEX row pointing at it.
 * `*out_temp_base` holds the temp basename on success (caller frees).
 * `*out_temp_recid` is the SYSINDEX row's recid, used by the commit
 * helper to remove the temp row after the swap. */
static int
vec_alloc_temp_base(DDIC *ddic, const char *indname,
                    const char *tableName, const char *field,
                    const char *params, const char *liveBase,
                    char **out_temp_base, RECID *out_temp_recid)
{
    static const char fn[] = "vec_alloc_temp_base";

    /* Strip the basename to get the directory.  TXbasename returns a
     * pointer into the input; the prefix length is the directory. */
    char dir[PATH_MAX];
    const char *bn = TXbasename(liveBase);
    size_t dlen = (size_t)(bn - liveBase);
    if (dlen == 0) {
        dir[0] = '.';
        dir[1] = '\0';
    } else {
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, liveBase, dlen);
        /* Strip trailing slash unless dir is just "/". */
        while (dlen > 1 && dir[dlen - 1] == '/') dlen--;
        dir[dlen] = '\0';
    }

    /* TXcreateTempIndexOrTableEntry flags:
     *   0x2: rebuild — required on Unix to insert the INDEX_TEMP row
     *                  (we always want it inserted for crash recovery).
     *   0x8: yap and fail if a temp/CR entry already exists for this name. */
    int flags = 0x2 | 0x8;
    char *tempBase = NULL;
    RECID tempRow;
    TXsetrecid(&tempRow, RECID_INVALID);
    if (!TXcreateTempIndexOrTableEntry(ddic, dir, indname, tableName,
                                       field, /*numTblFlds*/0, flags,
                                       /*remark*/NULL,
                                       params ? params : "",
                                       &tempBase, &tempRow)) {
        putmsg(MERR + UGE, fn,
            "could not allocate temp basename for ALTER INDEX `%s'", indname);
        return -1;
    }
    *out_temp_base = tempBase;
    if (out_temp_recid) *out_temp_recid = tempRow;
    return 0;
}

/* Walk a btree at `liveBase` and copy each recid that's NOT in the
 * sorted `absorbed` array into the btree at `tempBase`.  Used during
 * commit to carry forward concurrent INSERTs (live `_T.btr`) and
 * concurrent DELETEs (live `_del.btr`) that arrived during the build
 * phase, while filtering out recids the backend already absorbed into
 * sealed (so we don't carry them as delta).  Returns 0 on success. */
static int
vec_carry_forward_recids(const char *liveBase, const char *tempBase,
                         const int64_t *absorbed, size_t n_absorbed)
{
    /* Snapshot live recids first so we can iterate and insert into
     * tempBase without conflicting btree open semantics. */
    extern int vec_int64_cmp_(const void *a, const void *b);
    extern void vec_recid_vec_push_(int64_t r, void *user);
    struct { int64_t *data; size_t len; size_t cap; } live_v = {NULL, 0, 0};
    TXvecBtreeWalkRecids(liveBase, vec_recid_vec_push_, &live_v);
    int rc = 0;
    for (size_t i = 0; i < live_v.len; i++) {
        int64_t r = live_v.data[i];
        if (n_absorbed > 0 &&
            bsearch(&r, absorbed, n_absorbed, sizeof(int64_t),
                    vec_int64_cmp_))
            continue;       /* absorbed → don't carry as delta */
        if (TXvecBtreeInsertRecid(tempBase, r) != 0) {
            rc = -1;
            break;
        }
    }
    free(live_v.data);
    return rc;
}

/* Atomic-ish swap of vec artifacts.  Caller holds no locks; this
 * acquires the table W_LCK briefly to fence concurrent searchers and
 * writers from the rename window.  `absorbed` (sorted) lists recids
 * the backend folded into sealed; the carry-forward step copies live
 * `_T.btr` / `_del.btr` entries that aren't in the absorbed set into
 * the new `Tnnnn_T.btr` / `Tnnnn_del.btr` so concurrent INSERTs and
 * DELETEs against the live index during the build phase aren't lost. */
static int
vec_commit_temp_swap(DDIC *ddic, DBTBL *dbtbl, const char *indfile,
                     const char *tempBase, RECID tempRow,
                     const int64_t *absorbed, size_t n_absorbed)
{
    static const char fn[] = "vec_commit_temp_swap";
    int rc = -1;
    int locked = 0;

    if (TXlocktable(dbtbl, W_LCK) != 0) {
        putmsg(MERR + UGE, fn, "could not lock table for atomic swap");
        return -1;
    }
    locked = 1;

    /* Drop our cached vec handle so subsequent opens fault in the
     * post-rename files.  Cross-process invalidation is via
     * TXtouchindexfile below. */
    TXvecInvalidateHandle(indfile);

    /* Carry-forward live aux btrees into Tnnnn aux btrees.  Backend
     * already pre-populated Tnnnn_*.btr (e.g. with non-absorbed live
     * tombstones for OPTIMIZE, or empty for REBUILD); we append the
     * post-snapshot live entries that weren't in the absorbed set. */
    {
        char liveT[PATH_MAX], tempT[PATH_MAX];
        char liveD[PATH_MAX], tempD[PATH_MAX];
        if (snprintf(liveT, sizeof(liveT), "%s_T", indfile) < (int)sizeof(liveT) &&
            snprintf(tempT, sizeof(tempT), "%s_T", tempBase) < (int)sizeof(tempT)) {
            if (vec_carry_forward_recids(liveT, tempT,
                                         absorbed, n_absorbed) != 0) {
                putmsg(MWARN, fn, "carry-forward `_T.btr' failed");
            }
        }
        if (snprintf(liveD, sizeof(liveD), "%s_del", indfile) < (int)sizeof(liveD) &&
            snprintf(tempD, sizeof(tempD), "%s_del", tempBase) < (int)sizeof(tempD)) {
            if (vec_carry_forward_recids(liveD, tempD,
                                         absorbed, n_absorbed) != 0) {
                putmsg(MWARN, fn, "carry-forward `_del.btr' failed");
            }
        }
    }

    for (size_t i = 0; VEC_RENAME_EXTS[i]; i++) {
        char src[PATH_MAX], dst[PATH_MAX];
        int s_len = snprintf(src, sizeof(src), "%s%s",
                             tempBase, VEC_RENAME_EXTS[i]);
        int d_len = snprintf(dst, sizeof(dst), "%s%s",
                             indfile, VEC_RENAME_EXTS[i]);
        if (s_len < 0 || s_len >= (int)sizeof(src) ||
            d_len < 0 || d_len >= (int)sizeof(dst)) {
            putmsg(MERR + UGE, fn, "path too long for vec artifact swap");
            goto unlock;
        }
        EPI_STAT_S st;
        if (EPI_STAT(src, &st) != 0) continue;     /* backend skipped this ext */
        if (rename(src, dst) != 0) {
            putmsg(MERR + UGE, fn, "rename `%s' → `%s': %s",
                   src, dst, strerror(errno));
            goto unlock;
        }
    }

    /* Live row's FNAME still points at `indfile`; the renames have
     * replaced that file's content.  No SYSINDEX TYPE flip needed.
     * The INDEX_TEMP row was just for crash-cleanup tracking — drop it. */
    if (TXrecidvalid(&tempRow))
        (void)TXdelindexrec(ddic, tempRow);

    /* Remove the .PID lockfile that TXcreateTempIndexOrTableEntry
     * created (also for crash-cleanup tracking; no longer needed). */
    {
        char pidPath[PATH_MAX];
        if (snprintf(pidPath, sizeof(pidPath), "%s%s",
                     tempBase, TXtempPidExt) < (int)sizeof(pidPath))
            (void)unlink(pidPath);
    }

    /* Bump the cross-process index version counter.  Other rampart
     * processes re-stat their cached handles on next op and reopen. */
    TXtouchindexfile(ddic);

    rc = 0;

unlock:
    if (locked) TXunlocktable(dbtbl, W_LCK);
    return rc;
}

/* Roll back a failed Phase-2 build: unlink any Tnnnn artifacts and
 * remove the INDEX_TEMP SYSINDEX row.  Best-effort; logs nothing on
 * already-missing files. */
static void
vec_abort_temp_build(DDIC *ddic, const char *tempBase, RECID tempRow)
{
    if (!tempBase) return;
    for (size_t i = 0; VEC_RENAME_EXTS[i]; i++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s%s",
                     tempBase, VEC_RENAME_EXTS[i]) >= (int)sizeof(path))
            continue;
        (void)unlink(path);
    }
    /* Also drop the .PID lockfile that TXcreateTempIndexOrTableEntry
     * created for crash-tracking. */
    {
        char pidPath[PATH_MAX];
        if (snprintf(pidPath, sizeof(pidPath), "%s%s",
                     tempBase, TXtempPidExt) < (int)sizeof(pidPath))
            (void)unlink(pidPath);
    }
    if (ddic && TXrecidvalid(&tempRow))
        (void)TXdelindexrec(ddic, tempRow);
}

/* ALTER INDEX OPTIMIZE entry point.  Allocates Tnnnn, dispatches to
 * the backend's optimize slot (which writes new artifacts at Tnnnn_*),
 * and atomic-swaps on success.  Returns 0 on success, -1 on error. */
int
TXvecOptimize(DDIC *ddic, const char *indname, const char *indfile,
              const char *tableName, const char *field, const char *params,
              TXindOpts *options)
{
    static const char fn[] = "TXvecOptimize";
    if (!ddic || !indname || !indfile || !tableName || !field) return -1;

    /* Short-circuit when there's nothing to absorb.  Walk `_T.btr` to
     * count newrec entries; if zero, OPTIMIZE has no work to do (no
     * delta to fold into sealed) and skipping avoids the byte-copy of
     * `_I.idxpq` which can be many seconds on a large index. */
    {
        char *newrec_base = TXvecMakeBtreeBasePath(indfile, "_T");
        if (newrec_base) {
            extern void vec_recid_vec_push_(int64_t r, void *user);
            struct { int64_t *data; size_t len; size_t cap; }
                v = {NULL, 0, 0};
            TXvecBtreeWalkRecids(newrec_base, vec_recid_vec_push_, &v);
            free(newrec_base);
            size_t n = v.len;
            free(v.data);
            if (n == 0) return 0;
        }
    }

    /* Pull the WITH clause (e.g. `with indexmeter 'on'`) into the
     * options struct.  Mirrors updindex() — fulltext's re-CREATE/ALTER
     * processes options before consuming them, so the WITH clause
     * affects this run even though PARAMS-changing options are
     * ignored at the backend layer. */
    if (options) {
        int afterIndexType = INDEX_VEC;
        if (!TXindOptsProcessRawOptions(options, &afterIndexType, 1))
            return -1;
    }

    DBTBL *dbtbl = opendbtbl(ddic, (char *)tableName);
    if (!dbtbl) {
        putmsg(MERR + UGE, fn, "could not open table `%s'", tableName);
        return -1;
    }

    /* Hold R_LCK on the table for the duration of OPTIMIZE.  Mirrors
     * CREATE INDEX's uberlock at index.c:2333.  Without this,
     * getdbtblrow's per-row R_LCK acquire/release goes to texislockd
     * over a socket on every one of millions of rows — many minutes
     * of pure lock chatter on a large table. */
    if (TXlocktable(dbtbl, R_LCK) != 0) {
        putmsg(MERR + UGE, fn, "could not R_LCK table `%s'", tableName);
        closedbtbl(dbtbl);
        return -1;
    }

    TXvecHandle *h = TXvecOpen(ddic, indfile, params);
    if (!h) {
        TXunlocktable(dbtbl, R_LCK);
        closedbtbl(dbtbl);
        return -1;
    }
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h;

    char *tempBase = NULL;
    RECID tempRow;
    TXsetrecid(&tempRow, RECID_INVALID);
    if (vec_alloc_temp_base(ddic, indname, tableName, field, params,
                            indfile, &tempBase, &tempRow) != 0) {
        TXunlocktable(dbtbl, R_LCK);
        closedbtbl(dbtbl);
        return -1;
    }

    int64_t *absorbed = NULL;
    size_t n_absorbed = 0;
    int rc = vec_backend_for(hb->backend)->optimize(
        ddic, h, dbtbl, field, tempBase, options, &absorbed, &n_absorbed);
    /* Release R_LCK before commit acquires W_LCK on the same dbtbl. */
    TXunlocktable(dbtbl, R_LCK);
    if (rc != 0) {
        free(absorbed);
        vec_abort_temp_build(ddic, tempBase, tempRow);
        free(tempBase);
        closedbtbl(dbtbl);
        return -1;
    }

    rc = vec_commit_temp_swap(ddic, dbtbl, indfile, tempBase, tempRow,
                              absorbed, n_absorbed);
    if (rc != 0)
        vec_abort_temp_build(ddic, tempBase, tempRow);

    free(absorbed);
    free(tempBase);
    closedbtbl(dbtbl);
    return rc;
}

/* ALTER INDEX REBUILD entry point.  Same shape as TXvecOptimize but
 * the backend's rebuild slot does a from-scratch build into Tnnnn. */
int
TXvecRebuild(DDIC *ddic, const char *indname, const char *indfile,
             const char *tableName, const char *field, const char *params,
             TXindOpts *options)
{
    static const char fn[] = "TXvecRebuild";
    if (!ddic || !indname || !indfile || !tableName || !field) return -1;

    /* Process WITH clause; mirrors updindex(). */
    if (options) {
        int afterIndexType = INDEX_VEC;
        if (!TXindOptsProcessRawOptions(options, &afterIndexType, 1))
            return -1;
    }

    DBTBL *dbtbl = opendbtbl(ddic, (char *)tableName);
    if (!dbtbl) {
        putmsg(MERR + UGE, fn, "could not open table `%s'", tableName);
        return -1;
    }

    /* Hold R_LCK on the table for the duration of REBUILD.  Mirrors
     * CREATE INDEX's uberlock at index.c:2333.  Without this,
     * getdbtblrow's per-row R_LCK acquire/release goes to texislockd
     * over a socket on every one of millions of rows — many minutes
     * of pure lock chatter on a large table. */
    if (TXlocktable(dbtbl, R_LCK) != 0) {
        putmsg(MERR + UGE, fn, "could not R_LCK table `%s'", tableName);
        closedbtbl(dbtbl);
        return -1;
    }

    TXvecHandle *h = TXvecOpen(ddic, indfile, params);
    if (!h) {
        TXunlocktable(dbtbl, R_LCK);
        closedbtbl(dbtbl);
        return -1;
    }
    struct TXvecHandleBase *hb = (struct TXvecHandleBase *)h;

    TXvecParams vp;
    memset(&vp, 0, sizeof(vp));
    if (TXvecParamsParse(&vp, params ? params : "") != 0) {
        putmsg(MERR + UGE, fn,
            "could not parse SYSINDEX.PARAMS for `%s'", indfile);
        TXunlocktable(dbtbl, R_LCK);
        closedbtbl(dbtbl);
        return -1;
    }

    char *tempBase = NULL;
    RECID tempRow;
    TXsetrecid(&tempRow, RECID_INVALID);
    if (vec_alloc_temp_base(ddic, indname, tableName, field, params,
                            indfile, &tempBase, &tempRow) != 0) {
        TXunlocktable(dbtbl, R_LCK);
        closedbtbl(dbtbl);
        return -1;
    }

    int64_t *absorbed = NULL;
    size_t n_absorbed = 0;
    int rc = vec_backend_for(hb->backend)->rebuild(
        ddic, h, dbtbl, field, &vp, tempBase, options, &absorbed, &n_absorbed);
    /* Release R_LCK before commit acquires W_LCK on the same dbtbl. */
    TXunlocktable(dbtbl, R_LCK);
    if (rc != 0) {
        free(absorbed);
        vec_abort_temp_build(ddic, tempBase, tempRow);
        free(tempBase);
        closedbtbl(dbtbl);
        return -1;
    }

    rc = vec_commit_temp_swap(ddic, dbtbl, indfile, tempBase, tempRow,
                              absorbed, n_absorbed);
    if (rc != 0)
        vec_abort_temp_build(ddic, tempBase, tempRow);

    free(absorbed);
    free(tempBase);
    closedbtbl(dbtbl);
    return rc;
}
