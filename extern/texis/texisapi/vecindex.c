/* vecindex.c — texis engine integration for the ANN vector index.
 *
 * Backend: usearch (HNSW), embedded under extern/texis/thirdparty/usearch.
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

#include "dbquery.h"
#include "texint.h"
#include "btree.h"
#include "meter.h"

#include "vecindex.h"

#include "usearch.h"           /* usearch C API */


/* Vector dtype conversion helpers — defined in
 * src/duktape/globals/vector-distance.c (linked into the texis engine).
 */
extern void rpvec_f16_to_f32(const uint16_t *src, float *dst, size_t n);
extern void rpvec_bf16_to_f32(const uint16_t *src, float *dst, size_t n);
extern void rpvec_f64_to_f32(const double *src, float *dst, size_t n);

/* Suffix used for the on-disk usearch file.  Distinct from the previous
 * Vamana backend's `.vec' so the two backends never confuse each other's
 * files; if a user is migrating, they ALTER INDEX REBUILD once.
 */
#define VECIDX_FILE_SUFFIX ".vec"

/* Suffix appended to the index name to form the WAL table's name.
 * E.g. CREATE VEC INDEX wv3_vec → WAL table named wv3_vec_wal.  Used
 * only in defer (manual flush) mode; harmless and empty otherwise. */
#define VECIDX_WAL_SUFFIX  "_wal"

/* Forward decls — definitions are after the handle/SYSINDEX helpers
 * section since they need TXvecHandle and vec_sysindex_lookup_*. */
static int  vec_wal_create_table(DDIC *ddic, const char *indfile);

/* ----- TXvecParams: defaults / parse / from-options / to-text ------- */

static void
vec_params_init(TXvecParams *p)
{
    static const vec_graph_params_t graph_defaults = VEC_GRAPH_PARAMS_DEFAULT;
    memset(p, 0, sizeof(*p));
    p->graph = graph_defaults;
    p->threshold_t = 10000;
    p->threshold_d = 1000;
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
    if ((s = vec_opt_get(options, TXindOpt_flush)) != NULL) {
        if (!strcasecmp(s, "auto"))
            out->flush_mode = 0;
        else if (!strcasecmp(s, "manual"))
            out->flush_mode = 1;
        else {
            putmsg(MERR + UGE, fn,
                "flush must be `auto' or `manual'; got `%s'", s);
            return -1;
        }
    }
    if ((s = vec_opt_get(options, TXindOpt_vec_dtype)) != NULL) {
        if      (!strcasecmp(s, "f64"))  out->dtype = FTN_VEC_F64;
        else if (!strcasecmp(s, "f32"))  out->dtype = FTN_VEC_F32;
        else if (!strcasecmp(s, "f16"))  out->dtype = FTN_VEC_F16;
        else if (!strcasecmp(s, "bf16")) out->dtype = FTN_VEC_BF16;
        else {
            putmsg(MERR + UGE, fn,
                "vec_dtype must be one of `f64',`f32',`f16',`bf16'; got `%s'",
                s);
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
static size_t
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

        if      (!strcmp(key, "dim"))    out->graph.dim = atoi(val);
        else if (!strcmp(key, "M"))      out->graph.M = atoi(val);
        else if (!strcmp(key, "efc"))    out->graph.ef_construction = atoi(val);
        else if (!strcmp(key, "alpha"))  out->graph.alpha = (float)atof(val);
        else if (!strcmp(key, "metric")) {
            if (!strcmp(val, "l2"))       out->graph.metric = VEC_METRIC_L2;
            else if (!strcmp(val, "dot")) out->graph.metric = VEC_METRIC_DOT;
        }
        else if (!strcmp(key, "flush")) {
            out->flush_mode = !strcmp(val, "manual") ? 1 : 0;
        }
        else if (!strcmp(key, "state")) {
            out->dirty = !strcmp(val, "dirty") ? 1 : 0;
        }
        else if (!strcmp(key, "dtype")) {
            if      (!strcmp(val, "f64"))  out->dtype = FTN_VEC_F64;
            else if (!strcmp(val, "f32"))  out->dtype = FTN_VEC_F32;
            else if (!strcmp(val, "f16"))  out->dtype = FTN_VEC_F16;
            else if (!strcmp(val, "bf16")) out->dtype = FTN_VEC_BF16;
            else if (!strcmp(val, "i8"))   out->dtype = FTN_VEC_I8;
            else if (!strcmp(val, "u8"))   out->dtype = FTN_VEC_U8;
        }
        *end = saved;
        p = (saved ? end + 1 : end);
    }
    free(dup);
    return 0;
}

int
TXvecParamsToText(char *buf, size_t bufSz, const TXvecParams *p)
{
    /* Always emit flush=... and state=... so reading code never has to
     * guess the default — and so a state transition is just a substring
     * rewrite of the existing line.  dtype is emitted when known so a
     * varbyte-backed index can be reopened without consulting the
     * column. */
    const char *dtypeStr = vec_dtype_name(p->dtype);
    int n = snprintf(buf, bufSz,
        "type=vec;backend=usearch;dim=%d;dtype=%s;M=%d;efc=%d;alpha=%.3f;metric=%s"
        ";flush=%s;state=%s",
        p->graph.dim,
        dtypeStr ? dtypeStr : "f32",
        p->graph.M, p->graph.ef_construction, p->graph.alpha,
        (p->graph.metric == VEC_METRIC_L2) ? "l2" : "dot",
        p->flush_mode ? "manual" : "auto",
        p->dirty ? "dirty" : "clean");
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

/* Convert one row's vector to f32 in `dst[0..dim)`.  Returns 0 on success,
 * -1 on unsupported/empty.
 */
static int
convert_to_f32(int t, const void *raw, size_t n_elems, int dim, float *dst)
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
    default:
        return -1;
    }
}

/* ----- Index creation ----------------------------------------------- */

int
TXvecCreateIndex(DDIC *ddic, DBTBL *dbtbl,
                 const char *field, const char *indname,
                 const char *indfile, TXindOpts *options,
                 TXvecParams *outParams)
{
    static const char fn[] = "TXvecCreateIndex";
    int rc = -1;
    int dim = 0;
    size_t n_added = 0, skipped = 0;
    float *qbuf = NULL;
    char *vecpath = NULL;
    METER *meter = NULL;
    EPI_HUGEINT meterDone = 0;
    EPI_OFF_T meterTotal = 0;
    usearch_index_t idx = NULL;
    const char *uerr = NULL;
    RECID *recid;

    (void)ddic;
    (void)indname;

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
    if (t == FTN_VEC_I8 || t == FTN_VEC_U8) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: i8/u8 vector columns not yet supported");
        return -1;
    }

    /* Parse params. */
    TXvecParams vp;
    if (TXvecParamsFromOptions(&vp, options) < 0) goto err;

    /* dtype resolution.
     *   - typed varvec column: dtype is the column's element type;
     *     vec_dtype option (if given) must agree.
     *   - varbyte column: vec_dtype is required (we have no other way
     *     to know how to interpret cell bytes). */
    if (FTN_IS_VEC(t)) {
        if (vp.dtype != 0 && vp.dtype != t) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: vec_dtype `%s' conflicts with column type",
                vec_dtype_name(vp.dtype) ? vec_dtype_name(vp.dtype) : "?");
            goto err;
        }
        vp.dtype = t;
    } else {        /* FTN_BYTE */
        if (vp.dtype == 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC on a varbyte column requires `with vec_dtype "
                "'f16'` (or f32/f64/bf16) so the index knows how to "
                "interpret cell bytes");
            goto err;
        }
        if (vp.dtype == FTN_VEC_I8 || vp.dtype == FTN_VEC_U8) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: i8/u8 dtype not yet supported");
            goto err;
        }
    }

    /* Pre-pass: count live rows so we can reserve usearch's capacity
     * (and pre-allocate per-thread context buffers).  usearch_reserve
     * doesn't auto-grow, so we need an upper bound up front.  Iterating
     * the table once without reading vectors is cheap.
     */
    size_t row_estimate = 0;
    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid))
        row_estimate++;

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
    }

    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        if (meter) {
            meterDone += (EPI_HUGEINT)dbtbl->tbl->irecsz;
            METER_UPDATEDONE(meter, meterDone);
        }

        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) { skipped++; continue; }

        /* For varbyte columns getfld() returns byte count, not cells.
         * Translate so dim/convert logic below sees cell count. */
        size_t cell_count = n_elems;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(vp.dtype);
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

            usearch_init_options_t uo;
            memset(&uo, 0, sizeof(uo));
            uo.metric_kind   = metric_to_usearch(vp.graph.metric);
            uo.metric        = NULL;
            uo.quantization  = usearch_scalar_f32_k;
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
        }

        if (convert_to_f32(vp.dtype, raw, cell_count, dim, qbuf) < 0) {
            if ((int)cell_count != dim) {
                putmsg(MWARN, fn,
                    "INDEX_VEC: skipping row: vector dim %lu != index dim %d",
                    (unsigned long)cell_count, dim);
            }
            skipped++;
            continue;
        }

        usearch_add(idx, (usearch_key_t)(uint64_t)recid->off,
                    qbuf, usearch_scalar_f32_k, &uerr);
        if (uerr) {
            putmsg(MERR + UGE, fn, "usearch_add: %s", uerr);
            goto err;
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

    /* Create the per-index WAL table.  Lives for the lifetime of the
     * index; dropped at DROP INDEX.  Failing here is fatal so a name
     * collision is surfaced at create time rather than silently
     * disabling defer mode. */
    if (vec_wal_create_table(ddic, indfile) != 0) goto err;

    rc = (skipped > 0) ? 0 : 1;
    goto cleanup;

err:
    rc = -1;
cleanup:
    if (meter) meter = closemeter(meter);
    if (idx) usearch_free(idx, &uerr);
    free(qbuf);
    free(vecpath);
    return rc;
}

/* ----- Search-side handle cache ------------------------------------- */

struct TXvecHandle {
    char            *path;        /* SYSINDEX path (no extension) */
    char            *fpath;       /* path + .vec suffix */
    usearch_index_t  index;       /* opaque usearch handle */
    int              dim;
    vec_metric_t     metric;
    int              dtype;       /* FTN_VEC_F* — interpretation of cell
                                   * bytes for byte-backed indexes. */
    int              flush_mode;  /* 0=auto, 1=manual (cached from PARAMS) */
    int              dirty;       /* in-memory state diverges from disk */
    DDIC            *ddic;        /* DDIC observed at last open; used by
                                   * the exit-time flush to update SYSINDEX.
                                   * May go stale if connection closes
                                   * while the process keeps the cache;
                                   * stale-check happens via best-effort
                                   * try (lock failure → skip SYSINDEX). */
    /* Filesystem identity captured at load time — used to detect that
     * another process has rewritten the file (save_atomic uses rename,
     * so the inode changes).  Compared on cache hit; mismatch evicts
     * the handle and reloads.  The same check covers same-inode
     * truncate-and-rewrite via mtime, which save_atomic doesn't do but
     * a hand-edit might. */
    dev_t            file_dev;
    ino_t            file_ino;
    EPI_OFF_T        file_mtime;
    EPI_OFF_T        file_size;
    struct TXvecHandle *next;
};

static struct TXvecHandle *vec_handle_cache = NULL;
static int vec_force_defer = 0;       /* set by TXvecSetForceDefer */

static int save_atomic(usearch_index_t idx, const char *fpath, const char *fn);
static int save_atomic_h(struct TXvecHandle *h, const char *fn);

/* Capture (dev, ino, mtime, size) for `fpath`.  Sets all fields to 0
 * on stat() failure (file missing, permissions); the resulting record
 * compares unequal to any later stat with the file present, which is
 * what we want — a missing → present transition forces reload. */
static void
vec_capture_file_id(const char *fpath, struct TXvecHandle *h)
{
    EPI_STAT_S st;
    if (EPI_STAT(fpath, &st) == 0) {
        h->file_dev   = st.st_dev;
        h->file_ino   = st.st_ino;
        h->file_mtime = (EPI_OFF_T)st.st_mtime;
        h->file_size  = (EPI_OFF_T)st.st_size;
    } else {
        h->file_dev   = 0;
        h->file_ino   = 0;
        h->file_mtime = 0;
        h->file_size  = 0;
    }
}

/* Return non-zero if the on-disk file for `h->fpath` no longer
 * matches what we recorded at load.  A mismatch means another process
 * has rewritten the file (save_atomic's rename gives a new inode);
 * our cached usearch state is now stale and must be discarded.
 *
 * Always treated as "stale" if our handle has unflushed writes
 * (h->dirty == 1) — we shouldn't reload over our own pending state.
 * In manual-flush mode the same-process cache is the source of truth
 * until flush; cross-process visibility in defer mode is documented
 * as not provided.
 */
static int
vec_handle_is_stale(const struct TXvecHandle *h)
{
    EPI_STAT_S st;
    if (h->dirty) return 0;
    if (EPI_STAT(h->fpath, &st) != 0) {
        /* File disappeared (DROP from another process between our
         * load and now): treat as stale; subsequent open will fail
         * cleanly via usearch_metadata. */
        return 1;
    }
    if (st.st_dev != h->file_dev || st.st_ino != h->file_ino) return 1;
    if ((EPI_OFF_T)st.st_mtime != h->file_mtime) return 1;
    if ((EPI_OFF_T)st.st_size != h->file_size) return 1;
    return 0;
}

/* Internal: free `h` (frees usearch state and string fields).  Does
 * NOT touch the cache list — caller is responsible for unlinking. */
static void
vec_handle_free(struct TXvecHandle *h)
{
    const char *uerr = NULL;
    if (!h) return;
    if (h->index) usearch_free(h->index, &uerr);
    free(h->path);
    free(h->fpath);
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

/* Locate the SYSINDEX row for the vec index file `indfile`, replace
 * its PARAMS field with `newParams`, and write back at the same RECID.
 * Returns 0 on success, -1 on failure.
 *
 * `newParams` must be the same byte length as the existing value when
 * we want to guarantee true in-place rewrite (the texis dbf layer will
 * relocate if size differs).  Our toText output is fixed-length per
 * (mode,state) tuple, so callers meet this naturally.
 */
static int
vec_sysindex_update_params(DDIC *ddic, const char *indfile,
                           const char *newParams)
{
    static const char fn[] = "vec_sysindex_update_params";
    TBL  *tb;
    FLD  *fnameFld, *paramsFld, *typeFld;
    RECID *at, foundRecid;
    int   rc = -1;

    if (!ddic || !indfile || !newParams) return -1;
    tb = ddic->indextbl;
    if (!tb) {
        putmsg(MERR, fn, "no index TBL");
        return -1;
    }
    fnameFld  = nametofld(tb, "FNAME");
    paramsFld = nametofld(tb, "PARAMS");
    typeFld   = nametofld(tb, "TYPE");
    if (!fnameFld || !paramsFld || !typeFld) {
        putmsg(MERR, fn, "SYSINDEX missing FNAME/PARAMS/TYPE");
        return -1;
    }

    if (TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL) == -1) return -1;

    rewindtbl(tb);
    TXsetrecid(&foundRecid, -1);
    while (TXrecidvalid(at = gettblrow(tb, NULL))) {
        const char *fn_ = (const char *)getfld(fnameFld, NULL);
        int t = fn_ ? (int)(*(char *)getfld(typeFld, NULL)) : 0;
        if (fn_ && t == INDEX_VEC && vec_fname_matches(fn_, indfile)) {
            foundRecid = *at;
            putfld(paramsFld, (void *)newParams, strlen(newParams));
            if (puttblrow(tb, &foundRecid) == NULL) {
                putmsg(MERR + UGE, fn,
                    "SYSINDEX puttblrow for %s failed", indfile);
                goto unlock;
            }
            rc = 0;
            break;
        }
    }
    if (rc != 0)
        putmsg(MWARN, fn, "SYSINDEX entry for `%s' not found", indfile);

    /* Force SYSINDEX cache refresh so subsequent reads see the update. */
    if (!ddic->dblock && ddic->indtblcache)
        ddic->indtblcache->tbl = closetbl(ddic->indtblcache->tbl);

unlock:
    TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
    return rc;
}

/* Rewrite SYSINDEX.PARAMS for `h` to reflect `dirty`.  Reads the
 * current PARAMS to preserve build-time fields (M/efc/alpha) we don't
 * carry on the handle, then writes back with updated state.
 */
static int
vec_persist_state(DDIC *ddic, TXvecHandle *h, int dirty)
{
    static const char fn[] = "vec_persist_state";
    TXvecParams vp;
    char        buf[TX_VEC_PARAMS_TEXT_MAX];
    char       *currParams = NULL;

    if (!ddic || !h) return -1;
    if (vec_sysindex_get_params(ddic, h->path, &currParams) != 0 ||
        !currParams) {
        putmsg(MWARN, fn, "could not read current PARAMS for %s", h->path);
        return -1;
    }
    if (TXvecParamsParse(&vp, currParams) != 0) {
        free(currParams);
        return -1;
    }
    free(currParams);
    /* Override mutable bits from the in-memory handle. */
    vp.flush_mode = h->flush_mode;
    vp.dirty      = dirty ? 1 : 0;
    if (TXvecParamsToText(buf, sizeof(buf), &vp) < 0) {
        putmsg(MERR, fn, "PARAMS too long");
        return -1;
    }
    return vec_sysindex_update_params(ddic, h->path, buf);
}

/* Mark the index dirty and lazily persist the clean→dirty transition.
 * Subsequent calls within the same dirty window are no-ops, so the
 * typical defer-burst path triggers exactly one SYSINDEX write per
 * clean→dirty edge.
 */
static int
vec_mark_dirty(DDIC *ddic, TXvecHandle *h)
{
    if (!h || h->dirty) return 0;
    h->dirty = 1;
    if (!ddic) return 0;        /* best effort if no DDIC available */
    return vec_persist_state(ddic, h, 1);
}

/* Read SYSINDEX for `indfile` to learn TBNAME + FIELDS.  The two output
 * strings are alloc'd; caller frees.  Returns 0 on success, -1 on miss.
 */
static int
vec_sysindex_lookup_table_field(DDIC *ddic, const char *indfile,
                                char **outTbName, char **outFieldName)
{
    TBL *tb;
    FLD *fnameFld, *typeFld, *tbnameFld, *fieldsFld;
    RECID *at;
    int rc = -1;
    *outTbName = NULL;
    *outFieldName = NULL;
    if (!ddic || !indfile) return -1;
    tb = ddic->indextbl;
    if (!tb) return -1;
    fnameFld  = nametofld(tb, "FNAME");
    typeFld   = nametofld(tb, "TYPE");
    tbnameFld = nametofld(tb, "TBNAME");
    fieldsFld = nametofld(tb, "FIELDS");
    if (!fnameFld || !typeFld || !tbnameFld || !fieldsFld) return -1;
    if (TXlocksystbl(ddic, SYSTBL_INDEX, R_LCK, NULL) == -1) return -1;
    rewindtbl(tb);
    while (TXrecidvalid(at = gettblrow(tb, NULL))) {
        const char *fn_ = (const char *)getfld(fnameFld, NULL);
        int t = fn_ ? (int)(*(char *)getfld(typeFld, NULL)) : 0;
        if (fn_ && t == INDEX_VEC && vec_fname_matches(fn_, indfile)) {
            const char *tbName = (const char *)getfld(tbnameFld, NULL);
            const char *fName  = (const char *)getfld(fieldsFld, NULL);
            *outTbName     = tbName ? strdup(tbName) : strdup("");
            *outFieldName  = fName  ? strdup(fName)  : strdup("");
            rc = 0;
            break;
        }
    }
    TXunlocksystbl(ddic, SYSTBL_INDEX, R_LCK);
    return rc;
}

/* Reconcile the loaded index against the current table contents:
 * for each table row, if its RECID isn't already in the index, read
 * its vector and add it.  This recovers writes the previous process
 * deferred and didn't flush before crashing.
 *
 * Forward-only: orphans (keys in usearch for since-deleted rows) are
 * tolerated.  They get returned by the ANN search but the LIKEV
 * post-filter (which iterates a btree of the candidate RECIDs) drops
 * any candidate whose row isn't found in the table.  Periodic REBUILD
 * collapses these.
 *
 * Returns 0 on success (and saves + clears dirty bit), -1 on hard error
 * (handle remains dirty; caller can retry next open).
 */
static int
vec_reconcile(DDIC *ddic, TXvecHandle *h)
{
    static const char fn[] = "vec_reconcile";
    char *tbName = NULL, *fieldName = NULL;
    DBTBL *dbtbl = NULL;
    TBL *tb = NULL;
    FLD *fld;
    RECID *recid;
    float *qbuf = NULL;
    size_t added = 0, scanned = 0;
    int rc = -1;
    const char *uerr = NULL;

    if (vec_sysindex_lookup_table_field(ddic, h->path,
                                        &tbName, &fieldName) != 0 ||
        !tbName || !fieldName) {
        putmsg(MWARN, fn, "could not find SYSINDEX entry for %s", h->path);
        goto cleanup;
    }

    /* Open the table for read.  We need a DBTBL (not raw TBL) so
     * dbnametofld() resolves the field. */
    dbtbl = opendbtbl(ddic, tbName);
    if (!dbtbl) {
        putmsg(MERR + UGE, fn, "could not open table `%s'", tbName);
        goto cleanup;
    }
    tb = dbtbl->tbl;
    fld = dbnametofld(dbtbl, fieldName);
    if (!fld) {
        putmsg(MERR + UGE, fn, "field `%s' not in table `%s'",
               fieldName, tbName);
        goto cleanup;
    }

    qbuf = (float *)malloc((size_t)h->dim * sizeof(float));
    if (!qbuf) { putmsg(MERR + MAE, fn, "alloc qbuf"); goto cleanup; }

    int colType = fld->type & DDTYPEBITS;

    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        scanned++;
        usearch_key_t key = (usearch_key_t)(uint64_t)recid->off;
        uerr = NULL;
        if (usearch_contains(h->index, key, &uerr)) continue;

        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) continue;

        size_t cells = n_elems;
        if (colType == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(h->dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) continue;
            cells = n_elems / elsz;
        }
        if ((int)cells != h->dim) continue;
        if (convert_to_f32(h->dtype, raw, cells, h->dim, qbuf) < 0) continue;

        size_t sz = usearch_size(h->index, &uerr);     uerr = NULL;
        size_t cap = usearch_capacity(h->index, &uerr); uerr = NULL;
        if (sz >= cap) {
            size_t want = cap * 2 < cap + 16 ? cap + 16 : cap * 2;
            usearch_reserve(h->index, want, &uerr);
            if (uerr) { putmsg(MERR + UGE, fn, "reserve: %s", uerr);
                        goto cleanup; }
        }
        usearch_add(h->index, key, qbuf, usearch_scalar_f32_k, &uerr);
        if (uerr) { putmsg(MERR + UGE, fn, "add: %s", uerr); goto cleanup; }
        added++;
    }

    if (added > 0) {
        if (save_atomic_h(h, fn) != 0) goto cleanup;
    }
    h->dirty = 0;
    (void)vec_persist_state(ddic, h, 0);
    putmsg(MINFO, fn, "INDEX_VEC: reconcile of `%s' added %lu rows (scanned %lu)",
           h->path, (unsigned long)added, (unsigned long)scanned);
    rc = 0;

cleanup:
    free(qbuf);
    if (dbtbl) closedbtbl(dbtbl);
    free(tbName);
    free(fieldName);
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
save_atomic_h(struct TXvecHandle *h, const char *fn)
{
    if (save_atomic(h->index, h->fpath, fn) != 0) return -1;
    vec_capture_file_id(h->fpath, h);
    return 0;
}

/* ----- Write-Ahead Log (WAL) ----------------------------------------
 *
 * In defer (manual-flush) mode, per-row INSERT/DELETE writes a record
 * to a per-index texis SQL TABLE named `<indname>_wal` instead of
 * save_atomic'ing the .vec.  Using a regular texis table (not a
 * standalone btree) gives us cross-process locking, atomic single-
 * row insert, and cache invalidation through texislockd's existing
 * machinery — none of which we can replicate cleanly outside texis.
 *
 * Schema:  rid uint64    (recid offset of the indexed table row)
 *          op  char(1)   ('A' for add, 'D' for delete)
 *
 * Flush protocol — vec_wal_replay_and_clear():
 *   1. Iterate WAL via getdbtblrow under per-row R_LCK; build a local
 *      list of (wal_recid, rid, op) triples.  This snapshots what the
 *      WAL contained at iteration time.
 *   2. Reload .vec from disk; apply every list entry to the in-memory
 *      state.  ADDs read the row's vector from the indexed table;
 *      DELs just usearch_remove.  All ops are idempotent
 *      (contains+remove+add for ADD, no-op-on-missing for DEL).
 *   3. save_atomic_h to commit.
 *   4. freedbf each wal_recid we processed; rids that arrived during
 *      step 2 are left alone for the next flush.
 *
 * Concurrent flushers see overlapping snapshots — both apply, both
 * delete the rids they applied — re-applies are idempotent so the
 * final state is correct.  Crash anywhere preserves the WAL contents
 * up to that point; next flush re-applies.
 */

#define VEC_WAL_OP_ADD  ((uint8_t)'A')
#define VEC_WAL_OP_DEL  ((uint8_t)'D')

/* WAL table name = basename(indfile) + "_wal".  E.g. for indfile
 * "/db/wv3_vec" the table is "wv3_vec_wal".  Caller frees. */
static char *
vec_wal_table_name(const char *indfile)
{
    const char *slash = strrchr(indfile, '/');
    const char *base  = slash ? slash + 1 : indfile;
    size_t blen = strlen(base);
    char *name = (char *)malloc(blen + 5);  /* base + "_wal" + NUL */
    if (!name) return NULL;
    memcpy(name, base, blen);
    memcpy(name + blen, "_wal", 5);
    return name;
}

/* The on-disk file for the WAL table — texis appends ".tbl". */
static char *
vec_wal_table_fname(const char *indfile)
{
    /* Same naming as the indname suffix, since opendbtbl resolves a
     * relative fname under ddic->pname.  The indfile path may be
     * absolute under the db dir; in that case use just the basename. */
    return vec_wal_table_name(indfile);
}

/* Create the WAL table for `indfile`.  Called exactly once, from
 * TXvecCreateIndex; the table lives for the lifetime of the index
 * and is dropped at DROP INDEX time.  A name collision (e.g. a user
 * table happens to be called <indname>_wal) fails the index create
 * loudly so the conflict is caught immediately.
 *
 * Returns 0 on success, -1 on error.
 */
static int
vec_wal_create_table(DDIC *ddic, const char *indfile)
{
    static const char fn[] = "vec_wal_create_table";
    DD     *dd      = NULL;
    DBTBL  *dbtbl   = NULL;
    char   *tbname  = NULL;
    char   *fname   = NULL;
    int     rc      = -1;

    if (!ddic || !indfile) return -1;
    tbname = vec_wal_table_name(indfile);
    fname  = vec_wal_table_fname(indfile);
    if (!tbname || !fname) goto cleanup;

    /* Refuse if a table with this name already exists.  Two reasons:
     *  - prevents silently colliding with a user's table.
     *  - makes "CREATE VEC INDEX twice with same name" fail in a
     *    clear place, vs. quietly reusing stale WAL state. */
    dbtbl = opendbtbl(ddic, tbname);
    if (dbtbl) {
        putmsg(MERR + UGE, fn,
            "table `%s' already exists; rename or drop it before "
            "creating the vec index", tbname);
        goto cleanup;
    }

    dd = opendd();
    if (!dd) { putmsg(MERR + UGE, fn, "opendd"); goto cleanup; }
    ddsettype(dd, 1);
    if (putdd(dd, "rid", "uint64", 1, 0) < 0) {
        putmsg(MERR + UGE, fn, "putdd rid"); goto cleanup;
    }
    if (putdd(dd, "op",  "char",   1, 0) < 0) {
        putmsg(MERR + UGE, fn, "putdd op"); goto cleanup;
    }
    dbtbl = createdbtbl(ddic, dd, fname, tbname,
                        "INDEX_VEC WAL", 'T');
    if (!dbtbl) {
        putmsg(MERR + UGE, fn, "createdbtbl(%s)", tbname);
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (dbtbl) closedbtbl(dbtbl);
    if (dd)    closedd(dd);
    free(tbname);
    free(fname);
    return rc;
}

/* Drop the WAL table.  Best-effort — silently no-ops if missing. */
static void
vec_wal_drop_table(DDIC *ddic, const char *indfile)
{
    char *tbname = vec_wal_table_name(indfile);
    if (!tbname) return;
    /* TXdropdtable handles SYSTABLES/SYSCOLUMNS row removal + the
     * .tbl file unlink + index cleanup. */
    extern int TXdropdtable(DDIC *, char *);
    (void)TXdropdtable(ddic, tbname);
    free(tbname);
}

void
TXvecDropAux(DDIC *ddic, const char *indfile)
{
    if (!ddic || !indfile) return;
    vec_wal_drop_table(ddic, indfile);
}

/* Append (recid, op) to the WAL table.  Each call is its own DBTBL
 * open/insert/close — texis's cross-process locking around puttblrow
 * gives us the cache invalidation we need.  Higher-frequency callers
 * already pay the per-row INSERT lock dance for their main table; the
 * extra WAL row is the same cost.
 *
 * Returns 0 on success, -1 on error.
 */
static int
vec_wal_append(DDIC *ddic, const char *indfile, RECID *recid, uint8_t op)
{
    static const char fn[] = "vec_wal_append";
    DBTBL *wal    = NULL;
    char  *tbname = NULL;
    FLD   *ridFld = NULL, *opFld = NULL;
    EPI_UINT64 rid_u64;
    char   op_c;
    int    rc = -1;

    if (!ddic) return -1;
    tbname = vec_wal_table_name(indfile);
    if (!tbname) return -1;

    wal = opendbtbl(ddic, tbname);
    if (!wal) {
        putmsg(MERR + UGE, fn, "opendbtbl(%s)", tbname);
        goto cleanup;
    }
    ridFld = dbnametofld(wal, "rid");
    opFld  = dbnametofld(wal, "op");
    if (!ridFld || !opFld) {
        putmsg(MERR + UGE, fn, "missing rid/op field");
        goto cleanup;
    }

    if (TXlocktable(wal, W_LCK) != 0) {
        putmsg(MERR + UGE, fn, "lock %s", tbname);
        goto cleanup;
    }
    rid_u64 = (EPI_UINT64)(uint64_t)recid->off;
    op_c    = (char)op;
    putfld(ridFld, &rid_u64, 1);
    putfld(opFld,  &op_c,    1);
    if (puttblrow(wal->tbl, NULL) == NULL) {
        putmsg(MERR + UGE, fn, "puttblrow(%s)", tbname);
        TXunlocktable(wal, W_LCK);
        goto cleanup;
    }
    TXunlocktable(wal, W_LCK);
    rc = 0;

cleanup:
    if (wal) closedbtbl(wal);
    free(tbname);
    return rc;
}

/* Returns 1 if the WAL table has any rows, 0 if empty/missing, -1 on
 * error.  Used by TXvecOpen to decide whether to invoke replay. */
static int
vec_wal_has_entries(DDIC *ddic, const char *indfile)
{
    DBTBL *wal = NULL;
    char  *tbname = vec_wal_table_name(indfile);
    int    has = 0;

    if (!tbname || !ddic) { free(tbname); return 0; }
    wal = opendbtbl(ddic, tbname);
    if (!wal) { free(tbname); return 0; }   /* missing = empty */

    if (TXlocktable(wal, R_LCK) == 0) {
        TXrewinddbtbl(wal);
        RECID *r = getdbtblrow(wal);
        if (r != RECIDPN && TXrecidvalid(r)) has = 1;
        TXunlocktable(wal, R_LCK);
    }
    closedbtbl(wal);
    free(tbname);
    return has;
}

/* Replay all WAL entries onto h's index, save .vec, clear the WAL.
 *
 * Multi-process safety: the WAL btree has no built-in cross-process
 * lock (texis btrees rely on a parent table's R/W_LCK for that).  We
 * serialize WAL access by taking W_LCK on the indexed table for the
 * duration of replay.  Per-row vec_wal_append happens inside
 * TXvecAddRow which is already under the table's W_LCK (held by
 * procupd around per-row index updates), so its access is naturally
 * serialized against this flush.
 *
 * Returns 0 on success, -1 on hard error.
 */
/* One pending entry from the WAL table. */
typedef struct {
    RECID    walRecid;     /* RECID within the WAL table — used for delete-after-apply */
    EPI_OFF_T rid;         /* offset into the indexed table */
    uint8_t  op;           /* 'A' or 'D' */
} vec_wal_entry_t;

static int
vec_wal_replay_and_clear(struct TXvecHandle *h, DDIC *ddic)
{
    static const char fn[] = "vec_wal_replay_and_clear";
    char  *tbName    = NULL, *fieldName = NULL;
    char  *walTbName = NULL;
    DBTBL *dbtbl     = NULL;
    DBTBL *wal       = NULL;
    FLD   *fld       = NULL;
    FLD   *walRid    = NULL, *walOp = NULL;
    float *qbuf      = NULL;
    vec_wal_entry_t *entries = NULL;
    size_t  nentries = 0, capentries = 0;
    const char *uerr = NULL;
    size_t  applied_add = 0, applied_del = 0;
    int     rc = -1;
    int     colType = 0;
    int     hold_table_lock = 0;

    if (!ddic || !h) return -1;

    if (vec_sysindex_lookup_table_field(ddic, h->path,
                                        &tbName, &fieldName) != 0 ||
        !tbName || !fieldName) {
        putmsg(MWARN, fn, "no SYSINDEX entry for %s", h->path);
        goto cleanup;
    }
    walTbName = vec_wal_table_name(h->path);
    if (!walTbName) goto cleanup;

    dbtbl = opendbtbl(ddic, tbName);
    if (!dbtbl) {
        putmsg(MERR + UGE, fn, "could not open table `%s'", tbName);
        goto cleanup;
    }
    /* Take W_LCK on the indexed table for the duration of flush.
     * This serializes against:
     *   - other processes' INSERT/DELETE on the indexed table
     *     (their per-row vec_wal_append takes the same W_LCK), and
     *   - other processes' concurrent flush attempts (only one
     *     flush in flight at a time).
     * The latter is critical: without it, two processes can each
     * SELECT the WAL, reload .vec, save_atomic, and the second
     * rename() races and clobbers the first's writes. */
    if (TXlocktable(dbtbl, W_LCK) != 0) {
        putmsg(MERR + UGE, fn, "lock %s for flush", tbName);
        goto cleanup;
    }
    hold_table_lock = 1;

    fld = dbnametofld(dbtbl, fieldName);
    if (!fld) {
        putmsg(MERR + UGE, fn, "field `%s' not in table", fieldName);
        goto cleanup;
    }
    colType = fld->type & DDTYPEBITS;

    wal = opendbtbl(ddic, walTbName);
    if (!wal) {
        /* No WAL table → nothing to replay (probably a stale dirty
         * flag from a pre-WAL build).  Caller falls back to reconcile. */
        rc = 0;
        goto cleanup;
    }
    walRid = dbnametofld(wal, "rid");
    walOp  = dbnametofld(wal, "op");
    if (!walRid || !walOp) {
        putmsg(MERR + UGE, fn, "WAL table missing rid/op");
        goto cleanup;
    }

    /* Step 1: snapshot the WAL into a local list.  We hold W_LCK on
     * the indexed table for the entire flush, so other processes'
     * vec_wal_append calls block until we finish — the WAL contents
     * are stable during this iteration. */
    TXrewinddbtbl(wal);
    {
        RECID *r;
        while ((r = getdbtblrow(wal)) != RECIDPN && TXrecidvalid(r)) {
            if (nentries >= capentries) {
                size_t newcap = capentries ? capentries * 2 : 64;
                vec_wal_entry_t *p = (vec_wal_entry_t *)realloc(
                    entries, newcap * sizeof(*p));
                if (!p) {
                    putmsg(MERR + MAE, fn, "alloc entries");
                    goto cleanup;
                }
                entries = p;
                capentries = newcap;
            }
            entries[nentries].walRecid = *r;
            entries[nentries].rid = (EPI_OFF_T)*(EPI_UINT64 *)getfld(walRid, NULL);
            entries[nentries].op  = (uint8_t)*(char *)getfld(walOp, NULL);
            nentries++;
        }
    }

    if (nentries == 0) {
        /* Empty WAL — another process already applied any pending
         * writes (or we never had any).  Reload .vec from disk so
         * our in-memory state reflects the merged result, then
         * clear the dirty bit.  Without the reload, our cached
         * usearch state could miss the sibling's flushed adds and
         * subsequent same-process queries would be stale. */
        const char *uerr2 = NULL;
        usearch_init_options_t opts;
        usearch_index_t fresh = NULL;
        memset(&opts, 0, sizeof(opts));
        usearch_metadata(h->fpath, &opts, &uerr2);
        if (!uerr2) fresh = usearch_init(&opts, &uerr2);
        if (!uerr2 && fresh) usearch_load(fresh, h->fpath, &uerr2);
        if (!uerr2 && fresh) {
            usearch_free(h->index, &uerr2);
            h->index = fresh;
            vec_capture_file_id(h->fpath, h);
        } else if (fresh) {
            usearch_free(fresh, &uerr2);
        }
        h->dirty = 0;
        (void)vec_persist_state(ddic, h, 0);
        rc = 0;
        goto cleanup;
    }

    /* Step 2: reload .vec from disk into a fresh usearch state, so we
     * pick up any other process's flushes since our load. */
    qbuf = (float *)malloc((size_t)h->dim * sizeof(float));
    if (!qbuf) { putmsg(MERR + MAE, fn, "alloc qbuf"); goto cleanup; }

    {
        usearch_index_t fresh = NULL;
        usearch_init_options_t opts;
        memset(&opts, 0, sizeof(opts));
        usearch_metadata(h->fpath, &opts, &uerr);
        if (uerr) {
            putmsg(MERR + UGE, fn, "usearch_metadata: %s", uerr);
            goto cleanup;
        }
        fresh = usearch_init(&opts, &uerr);
        if (!fresh || uerr) {
            putmsg(MERR + UGE, fn, "usearch_init: %s",
                   uerr ? uerr : "(null)");
            if (fresh) usearch_free(fresh, &uerr);
            goto cleanup;
        }
        usearch_load(fresh, h->fpath, &uerr);
        if (uerr) {
            putmsg(MERR + UGE, fn, "usearch_load: %s", uerr);
            usearch_free(fresh, &uerr);
            goto cleanup;
        }
        usearch_free(h->index, &uerr);
        h->index = fresh;
    }

    /* Step 3: apply each WAL entry to the in-memory state. */
    for (size_t i = 0; i < nentries; i++) {
        EPI_OFF_T ridOff = entries[i].rid;
        uint8_t   op     = entries[i].op;
        usearch_key_t key = (usearch_key_t)(uint64_t)ridOff;

        if (op == VEC_WAL_OP_DEL) {
            uerr = NULL;
            usearch_remove(h->index, key, &uerr);
            uerr = NULL;
            applied_del++;
            continue;
        }
        /* ADD: read row's vector from indexed table.  We already hold
         * W_LCK on dbtbl, so no per-row sub-lock needed. */
        RECID r;
        TXsetrecid(&r, ridOff);
        if (gettblrow(dbtbl->tbl, &r) == NULL) {
            continue;       /* row gone — fine, skip. */
        }
        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        int   ok = 1;
        size_t cells = n_elems;
        if (!raw || n_elems == 0) ok = 0;
        else if (colType == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(h->dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) ok = 0;
            else cells = n_elems / elsz;
        }
        if (ok && (int)cells != h->dim) ok = 0;
        if (ok && convert_to_f32(h->dtype, raw, cells, h->dim, qbuf) < 0) ok = 0;
        if (!ok) continue;

        size_t sz  = usearch_size(h->index, &uerr);     uerr = NULL;
        size_t cap = usearch_capacity(h->index, &uerr); uerr = NULL;
        if (sz >= cap) {
            size_t want = cap * 2 < cap + 16 ? cap + 16 : cap * 2;
            usearch_reserve(h->index, want, &uerr);
            if (uerr) { putmsg(MERR + UGE, fn, "reserve: %s", uerr); goto cleanup; }
        }
        if (usearch_contains(h->index, key, &uerr)) {
            usearch_remove(h->index, key, &uerr);
            uerr = NULL;
        }
        usearch_add(h->index, key, qbuf, usearch_scalar_f32_k, &uerr);
        if (uerr) { putmsg(MERR + UGE, fn, "add: %s", uerr); goto cleanup; }
        applied_add++;
    }

    /* Step 4: save the merged state. */
    if (save_atomic_h(h, fn) != 0) goto cleanup;

    /* Step 5: delete each WAL row we processed.  We hold W_LCK on
     * the indexed table; a separate brief W_LCK on the WAL table per
     * row is fine.  (We can't keep the WAL locked for all deletions
     * because we might already hold the indexed table's lock on
     * conflicting hierarchies.) */
    for (size_t i = 0; i < nentries; i++) {
        if (TXlocktable(wal, W_LCK) != 0) continue;
        (void)freedbf(wal->tbl->df, TXgetoff(&entries[i].walRecid));
        TXunlocktable(wal, W_LCK);
    }

    /* Step 6: clear the SYSINDEX dirty bit so the next opener can
     * skip the recovery path. */
    h->dirty = 0;
    (void)vec_persist_state(ddic, h, 0);
    rc = 0;
    if (TXverbosity > 0)
        putmsg(MINFO, fn, "WAL replay: %lu ADDs, %lu DELs (of %lu entries)",
               (unsigned long)applied_add, (unsigned long)applied_del,
               (unsigned long)nentries);

cleanup:
    free(qbuf);
    free(entries);
    if (wal) closedbtbl(wal);
    if (hold_table_lock && dbtbl) TXunlocktable(dbtbl, W_LCK);
    if (dbtbl) closedbtbl(dbtbl);
    free(tbName);
    free(fieldName);
    free(walTbName);
    return rc;
}

TXvecHandle *
TXvecOpen(DDIC *ddic, const char *indfile)
{
    static const char fn[] = "TXvecOpen";
    const char *uerr = NULL;

    /* Cache lookup.  On hit, validate that the on-disk file hasn't
     * been replaced by another process; if it has, evict and fall
     * through to a fresh load.  Refresh ddic on hit so the most
     * recent caller's DDIC is what an exit-time flush will use.
     *
     * Pointer-to-pointer walk so we can unlink in place if stale.  */
    {
        struct TXvecHandle **pp = &vec_handle_cache;
        while (*pp) {
            struct TXvecHandle *h = *pp;
            if (strcmp(h->path, indfile) == 0) {
                if (vec_handle_is_stale(h)) {
                    /* Another process rewrote the file. */
                    *pp = h->next;
                    vec_handle_free(h);
                    break;          /* fall through to reload */
                }
                if (ddic) h->ddic = ddic;
                return h;
            }
            pp = &h->next;
        }
    }

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

    struct TXvecHandle *h =
        (struct TXvecHandle *)calloc(1, sizeof(*h));
    if (!h) { usearch_free(idx, &uerr); free(fpath); return NULL; }
    h->path = strdup(indfile);
    if (!h->path) { free(h); usearch_free(idx, &uerr); free(fpath); return NULL; }
    h->fpath  = fpath;            /* h takes ownership */
    h->index  = idx;
    h->dim    = (int)opts.dimensions;
    h->metric = (opts.metric_kind == usearch_metric_l2sq_k)
                ? VEC_METRIC_L2 : VEC_METRIC_DOT;
    h->flush_mode = 0;            /* default; PARAMS lookup may override */
    h->dirty      = 0;
    h->dtype      = FTN_VEC_F32;  /* default; overridden from PARAMS */
    h->ddic       = ddic;         /* may be NULL; refreshed on cache hit */

    /* Capture file identity for cross-process change detection.  Done
     * AFTER usearch_load returns so the inode/mtime reflect what we
     * actually loaded. */
    vec_capture_file_id(fpath, h);

    /* Read flush_mode + dirty from SYSINDEX.PARAMS so subsequent writes
     * know whether to defer.  Best-effort: if DDIC is missing or the
     * SYSINDEX row can't be located, we keep the auto/clean defaults. */
    if (ddic) {
        char *currParams = NULL;
        if (vec_sysindex_get_params(ddic, indfile, &currParams) == 0 &&
            currParams) {
            TXvecParams vp;
            if (TXvecParamsParse(&vp, currParams) == 0) {
                h->flush_mode = vp.flush_mode;
                h->dirty      = vp.dirty;
                if (vp.dtype) h->dtype = vp.dtype;
            }
            free(currParams);
        }
    }

    h->next   = vec_handle_cache;
    vec_handle_cache = h;

    /* Recovery path.  When PARAMS state=dirty (set on the first defer
     * write per burst, cleared after a successful flush), there's
     * pending work to apply.  Try the WAL first — it's the cheap path
     * (replays only the recorded ops) and covers the common case of
     * a clean shutdown that didn't get a chance to flush, plus crash-
     * mid-burst.  If the WAL replay fails, fall back to the table-
     * scan reconcile so missing rows are still recovered. */
    if (h->dirty && ddic) {
        if (vec_wal_replay_and_clear(h, ddic) != 0) {
            putmsg(MWARN, fn,
                "INDEX_VEC: WAL replay for `%s' failed; falling back to "
                "table-scan reconcile", indfile);
            if (vec_reconcile(ddic, h) != 0)
                putmsg(MWARN, fn,
                    "INDEX_VEC: reconcile of `%s' failed; index may be "
                    "missing rows until manually rebuilt", indfile);
        }
    }
    return h;
}

/* ----- Per-row hooks ------------------------------------------------ */

int
TXvecAddRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field, RECID *recid)
{
    static const char fn[] = "TXvecAddRow";
    if (!dbtbl || !indfile || !field || !recid) return -1;

    TXvecHandle *h = TXvecOpen(ddic, indfile);
    if (!h) return -1;

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

    size_t n_elems = 0;
    void *raw = getfld(fld, &n_elems);
    if (!raw || n_elems == 0) {
        /* NULL or empty vector: nothing to add to the index. */
        return 0;
    }

    /* Compute cell count from byte length when the column is varbyte. */
    size_t cell_count = n_elems;
    if (t == FTN_BYTE) {
        size_t elsz = vec_dtype_elsz(h->dtype);
        if (elsz == 0 || (n_elems % elsz) != 0) {
            putmsg(MWARN, fn,
                "INDEX_VEC: row byte length %lu not a multiple of dtype "
                "element size %lu; skipping",
                (unsigned long)n_elems, (unsigned long)elsz);
            return 0;
        }
        cell_count = n_elems / elsz;
    }
    if ((int)cell_count != h->dim) {
        putmsg(MWARN, fn,
            "INDEX_VEC: row vector dim %lu != index dim %d; skipping",
            (unsigned long)cell_count, h->dim);
        return 0;
    }

    float *qbuf = (float *)malloc((size_t)h->dim * sizeof(float));
    if (!qbuf) { putmsg(MERR + MAE, fn, "alloc qbuf"); return -1; }

    int rc = 0;
    if (convert_to_f32(h->dtype, raw, cell_count, h->dim, qbuf) < 0) {
        putmsg(MWARN, fn, "INDEX_VEC: skipping row with unsupported dtype");
        rc = 0;
        goto cleanup;
    }

    const char *uerr = NULL;
    /* If the recid is already present (UPDATE path can call us after
     * TXdelfromindices, but a stray duplicate shouldn't crash us),
     * remove first then add.  Surface remove errors as a warning so
     * the subsequent failed add doesn't look like a duplicate-key
     * mystery.
     */
    if (usearch_contains(h->index, (usearch_key_t)(uint64_t)recid->off, &uerr)) {
        usearch_remove(h->index, (usearch_key_t)(uint64_t)recid->off, &uerr);
        if (uerr) {
            putmsg(MWARN, fn, "usearch_remove (pre-add cleanup): %s", uerr);
            uerr = NULL;
        }
    }

    /* usearch_reserve doesn't auto-grow on add(); ensure capacity
     * before we hit it.  After load(), capacity == size, so the very
     * first add would fail with "Reserve capacity ahead of insertions".
     */
    {
        size_t sz = usearch_size(h->index, &uerr);     uerr = NULL;
        size_t cap = usearch_capacity(h->index, &uerr); uerr = NULL;
        if (sz >= cap) {
            size_t want = cap * 2;
            if (want < cap + 16) want = cap + 16;
            usearch_reserve(h->index, want, &uerr);
            if (uerr) {
                putmsg(MERR + UGE, fn, "usearch_reserve: %s", uerr);
                rc = -1; goto cleanup;
            }
        }
    }

    usearch_add(h->index, (usearch_key_t)(uint64_t)recid->off,
                qbuf, usearch_scalar_f32_k, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_add: %s", uerr);
        rc = -1; goto cleanup;
    }

    /* Defer when the index is configured for manual flush, OR when
     * the connection has set vecAutoFlush=false.  In defer mode we
     * record the op in the per-index WAL btree (cross-process durable,
     * locked) and skip save_atomic; flush replays the WAL onto a
     * fresh disk load, merging all processes' pending writes. */
    if (getenv("TX_VEC_TRACE"))
        fprintf(stderr, "[AddRow] flush_mode=%d force_defer=%d\n",
            h->flush_mode, vec_force_defer);
    if (h->flush_mode || vec_force_defer) {
        if (vec_wal_append(ddic, indfile, recid, VEC_WAL_OP_ADD) != 0) {
            putmsg(MWARN, fn,
                "WAL append failed; falling back to eager save");
            if (save_atomic_h(h, fn) != 0) { rc = -1; goto cleanup; }
        } else {
            (void)vec_mark_dirty(ddic, h);
        }
    } else {
        if (save_atomic_h(h, fn) != 0) { rc = -1; goto cleanup; }
    }

cleanup:
    free(qbuf);
    return rc;
}

int
TXvecDelRow(DDIC *ddic, DBTBL *dbtbl,
            const char *indfile, const char *field, RECID *recid)
{
    static const char fn[] = "TXvecDelRow";
    (void)dbtbl; (void)field;
    if (!indfile || !recid) return -1;

    TXvecHandle *h = TXvecOpen(ddic, indfile);
    if (!h) return -1;

    const char *uerr = NULL;
    size_t removed = usearch_remove(h->index,
                                    (usearch_key_t)(uint64_t)recid->off,
                                    &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_remove: %s", uerr);
        return -1;
    }
    if (removed == 0) {
        /* Recid wasn't in the index — nothing to persist.  This happens
         * when the row was inserted with a NULL or invalid vector and
         * thus never added; not an error.
         */
        return 0;
    }

    if (h->flush_mode || vec_force_defer) {
        if (vec_wal_append(ddic, indfile, recid, VEC_WAL_OP_DEL) != 0) {
            putmsg(MWARN, fn,
                "WAL append failed; falling back to eager save");
            if (save_atomic_h(h, fn) != 0) return -1;
        } else {
            (void)vec_mark_dirty(ddic, h);
        }
    } else {
        if (save_atomic_h(h, fn) != 0) return -1;
    }
    return 0;
}

size_t
TXvecSearch(TXvecHandle *h, const float *query, size_t k, size_t ef,
            vec_search_result_t *results)
{
    static const char fn[] = "TXvecSearch";
    const char *uerr = NULL;
    (void)ef;   /* usearch's expansion_search was set at index creation;
                 * dynamic adjustment per query isn't exposed. */
    if (!h || !h->index) return SIZE_MAX;

    usearch_key_t *keys = (usearch_key_t *)
        malloc(k * sizeof(usearch_key_t));
    usearch_distance_t *dists = (usearch_distance_t *)
        malloc(k * sizeof(usearch_distance_t));
    if (!keys || !dists) {
        free(keys); free(dists);
        return SIZE_MAX;
    }

    size_t got = usearch_search(h->index, query, usearch_scalar_f32_k,
                                k, keys, dists, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_search: %s", uerr);
        free(keys); free(dists);
        return SIZE_MAX;
    }

    /* Translate.  usearch returns distances (lower = better) for both
     * inner-product (1 - dot) and L2sq.  vec_search_result_t.score
     * historically holds the metric-natural value (higher = better for
     * dot, lower = better for L2).  The LIKEV path doesn't actually use
     * this score (it re-evaluates per row); set a reasonable value for
     * any caller that does inspect it.
     */
    for (size_t i = 0; i < got; i++) {
        results[i].id = (vec_id_t)keys[i];
        if (h->metric == VEC_METRIC_L2)
            results[i].score = (float)dists[i];
        else
            results[i].score = 1.0f - (float)dists[i];   /* dot ~= 1 - returned */
    }

    free(keys); free(dists);
    return got;
}

void
TXvecCloseAll(void)
{
    struct TXvecHandle *h = vec_handle_cache;
    while (h) {
        struct TXvecHandle *next = h->next;
        vec_handle_free(h);
        h = next;
    }
    vec_handle_cache = NULL;
}

void
TXvecInvalidateHandle(const char *indfile)
{
    struct TXvecHandle **pp = &vec_handle_cache;
    if (!indfile) return;
    while (*pp) {
        struct TXvecHandle *h = *pp;
        if (strcmp(h->path, indfile) == 0) {
            *pp = h->next;
            vec_handle_free(h);
            break;
        }
        pp = &h->next;
    }
    /* The WAL TABLE drop is wired separately via TXvecDropAux,
     * called from droptbl.c::TXdropdindex while the DDIC is still
     * available. */
}

/* ----- Flush API ---------------------------------------------------- */

/* Reload h->index from h->fpath, replacing the in-memory state.  Used
 * after flush so the cached handle reflects the freshly-saved disk
 * state (including any sibling-process writes that got merged in). */
static int
vec_handle_reload_from_disk(struct TXvecHandle *h, const char *fn)
{
    const char *uerr = NULL;
    usearch_init_options_t opts;
    usearch_index_t fresh = NULL;

    memset(&opts, 0, sizeof(opts));
    usearch_metadata(h->fpath, &opts, &uerr);
    if (uerr) {
        putmsg(MWARN, fn, "reload metadata: %s", uerr);
        return -1;
    }
    fresh = usearch_init(&opts, &uerr);
    if (!fresh || uerr) {
        putmsg(MWARN, fn, "reload init: %s", uerr ? uerr : "(null)");
        if (fresh) usearch_free(fresh, &uerr);
        return -1;
    }
    usearch_load(fresh, h->fpath, &uerr);
    if (uerr) {
        putmsg(MWARN, fn, "reload load: %s", uerr);
        usearch_free(fresh, &uerr);
        return -1;
    }
    usearch_free(h->index, &uerr);
    h->index = fresh;
    vec_capture_file_id(h->fpath, h);
    return 0;
}

static int
vec_flush_handle(DDIC *ddic, TXvecHandle *h)
{
    static const char fn[] = "TXvecFlush";
    (void)fn;

    if (!h->dirty) return 0;

    /* Always replay.  vec_wal_replay_and_clear takes W_LCK on the
     * indexed table for its duration (serializing against concurrent
     * INSERTs and other flushes), reloads .vec from disk fresh
     * (picking up any sibling flushes that happened since our load),
     * applies the WAL atomically, saves, and deletes the entries it
     * processed.  Empty-WAL is a fast no-op; non-empty handles all
     * cross-process merge cases via the per-row idempotent ops. */
    if (vec_wal_replay_and_clear(h, ddic) != 0) return -1;
    h->dirty = 0;
    return 0;
}

int
TXvecFlush(DDIC *ddic, const char *indfile)
{
    if (!indfile) return -1;
    for (struct TXvecHandle *h = vec_handle_cache; h; h = h->next)
        if (strcmp(h->path, indfile) == 0)
            return vec_flush_handle(ddic, h);
    return 0;       /* not in cache → nothing to flush */
}

int
TXvecFlushAll(DDIC *ddic)
{
    int rc = 0;
    for (struct TXvecHandle *h = vec_handle_cache; h; h = h->next) {
        if (!h->dirty) continue;
        /* Prefer the caller's DDIC, fall back to the one captured at
         * the handle's last open.  If both are NULL, we still save the
         * .vec but skip the SYSINDEX clean bit (next open will
         * reconcile to verify and clear). */
        DDIC *useDdic = ddic ? ddic : h->ddic;
        if (vec_flush_handle(useDdic, h) != 0) rc = -1;
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

DDIC *
TXvecGetExitHookDDIC(void)
{
    return vec_exit_hook_ddic;
}

/* Connection-scoped force-defer flag (see vecindex.h).  Storage is
 * defined near the top of the file; setter/getter live here. */

void
TXvecSetForceDefer(int on)
{
    vec_force_defer = on ? 1 : 0;
}

int
TXvecGetForceDefer(void)
{
    return vec_force_defer;
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
    usearch_key_t *keys = NULL;
    usearch_distance_t *dists = NULL;
    EPI_HUGEUINT cnt = 0;

    (void)sysindexParams; (void)fname;
    if (cop) *cop = 0;

    if (op != FOP_MMV) goto err;
    if (!infld || !iname) goto err;
    if (TXfldIsNull(infld)) goto err;

    int t = infld->type & DDTYPEBITS;
    if (!FTN_IS_VEC_OR_BYTE(t)) goto err;

    TXvecHandle *h = TXvecOpen(dbtbl ? dbtbl->ddic : NULL, iname);
    if (!h) goto err;

    /* Extract + convert query to f32.  For varbyte queries we use the
     * index's persisted dtype to translate cell bytes; for typed varvec
     * queries the column type already discriminates. */
    size_t qn = 0;
    void *qraw = getfld(infld, &qn);
    if (!qraw || qn == 0) goto err;

    int qDtype = FTN_IS_VEC(t) ? t : h->dtype;
    size_t qCells = qn;
    if (t == FTN_BYTE) {
        size_t elsz = vec_dtype_elsz(h->dtype);
        if (elsz == 0 || (qn % elsz) != 0) goto err;
        qCells = qn / elsz;
    }
    if ((int)qCells != h->dim) {
        if (TXverbosity > 0)
            putmsg(MINFO, fn,
                "INDEX_VEC dim=%d but query dim=%lu; falling back to brute force",
                h->dim, (unsigned long)qCells);
        goto err;
    }

    qbuf = (float *)malloc((size_t)h->dim * sizeof(float));
    if (!qbuf) goto err;
    if (convert_to_f32(qDtype, qraw, qCells, h->dim, qbuf) < 0) goto err;

    /* Top-K search. */
    size_t k = 1000;
    keys  = (usearch_key_t *)     malloc(k * sizeof(usearch_key_t));
    dists = (usearch_distance_t *)malloc(k * sizeof(usearch_distance_t));
    if (!keys || !dists) goto err;

    const char *uerr = NULL;
    size_t got = usearch_search(h->index, qbuf, usearch_scalar_f32_k,
                                k, keys, dists, &uerr);
    if (uerr) {
        putmsg(MERR + UGE, fn, "usearch_search: %s", uerr);
        goto err;
    }

    /* Materialize into in-memory btree, recid as btloc, sequence number
     * as key (best-first iteration since usearch returns sorted).
     */
    bt = openbtree(NULL, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED,
                   O_RDWR | O_CREAT);
    if (!bt) goto err;

    for (size_t i = 0; i < got; i++) {
        /* Filter for dot metric: distance >= 1 means dot <= 0 (no match). */
        if (h->metric == VEC_METRIC_DOT && dists[i] >= 1.0f) continue;
        BTLOC bl;
        TXsetrecid(&bl, (EPI_OFF_T)(uint64_t)keys[i]);
        EPI_OFF_T key = (EPI_OFF_T)i;
        btinsert(bt, &bl, sizeof(key), &key);
        cnt++;
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
    free(keys);
    free(dists);
    return ix;
}
