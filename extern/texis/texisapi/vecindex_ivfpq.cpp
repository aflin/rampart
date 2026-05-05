/*
 * vecindex_ivfpq.cpp — FAISS IVFPQ backend for INDEX_VEC.
 *
 * Sealed segment of the segmented architecture (vec-ivfpq-integration-plan
 * §1).  CREATE trains codebooks + encodes all rows; SEARCH scans the
 * sealed PQ codes.  Per-row INSERT/DELETE are stubs here — they will be
 * routed to the HNSW delta + tombstone btree in subsequent phases.
 *
 * On-disk layout per index:
 *   <basename>_H.idxpq   codebooks + invlist metadata (this file's serializer)
 *   <basename>_I.idxpq   mmap'd OnDiskInvertedLists (PQ codes + ids)
 *
 * The HEAD file uses FAISS's IOWriter abstraction (impl/io.h) to write
 * a small fixed-shape blob.  We deliberately do NOT include FAISS's
 * impl/index_write.cpp because that pulls in every index type.  Our
 * serializer covers only what IndexIVFPQ needs.
 */

#include <faiss/IndexIVFPQ.h>
#include <faiss/IndexFlat.h>
#include <faiss/Clustering.h>
#include <faiss/invlists/OnDiskInvertedLists.h>
#include <faiss/impl/io.h>
#include <faiss/impl/io_macros.h>
#include <faiss/impl/FaissException.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

extern "C" {
#include "txcoreconfig.h"
#include "dbquery.h"
#include "texint.h"
#include "meter.h"
#include "vecindex.h"
#include "vecindex_internal.h"
#include "sysupdate.h"
}

/* ====================================================================
 * OpenBLAS threading control (kept from phase 1).
 * ==================================================================== */

#if defined(__linux__) || defined(__FreeBSD__)
extern "C" void openblas_set_num_threads(int) __attribute__((weak));
#endif

/* qsort comparator defined in vecindex.c — used by both backends to
 * sort the absorbed-recid array we hand back to the dispatcher. */
extern "C" int vec_int64_cmp_(const void *a, const void *b);

namespace {
void faiss_init_once_impl()
{
#if defined(__linux__) || defined(__FreeBSD__)
    if (::openblas_set_num_threads) ::openblas_set_num_threads(1);
#endif
}
} /* anonymous namespace */

extern "C" void TXvecFaissInitOnce(void)
{
    static std::once_flag flag;
    std::call_once(flag, faiss_init_once_impl);
}

/* ====================================================================
 * Concrete IVFPQ handle.  Embeds TXvecHandleBase; cache-compatible.
 * ==================================================================== */

namespace {
struct TXvecIvfpqHandle {
    TXvecHandleBase    base;
    char              *head_path;       /* "<base>_H.idxpq" */
    char              *invl_path;       /* "<base>_I.idxpq" */
    char              *tomb_base;       /* "<base>_del"  (openbtree adds .btr) */
    char              *newrec_base;     /* "<base>_T"    (openbtree adds .btr) */
    faiss::IndexIVFPQ *idx;             /* owns coarse quantizer + invlists */
    int64_t            max_recid_at_create;
                                        /* boundary persisted in _H.idxpq;
                                         * any insert with recid->off > this
                                         * is fresh, can't collide with sealed */
    int                dirty_meta;      /* head file needs rewrite */
};

/* Auto-scale nprobe with nlist when the user hasn't set an explicit
 * value.  At default nlist=32768 (46M-row indexes) the FAISS default
 * nprobe of 1, or our previous global default of 8, both produced
 * abysmal recall (~0.024% of cells visited). The 1/128 ratio
 * (≈0.78% of cells) maps to nprobe=256 at nlist=32768, which is
 * empirically the right sweet spot. Floor at 8 so small indexes
 * still get reasonable coverage. */
inline size_t auto_nprobe(size_t nlist) {
    size_t n = nlist >> 7;          /* nlist / 128 */
    return n < 8 ? 8 : n;
}

/* C++ wrapper around the shared C `TXvecBtreeWalkRecids` callback API
 * that fills a std::vector — convenient for the IVFPQ search loop. */
void cb_push_recid(int64_t recid_off, void *user) {
    auto *out = static_cast<std::vector<int64_t> *>(user);
    out->push_back(recid_off);
}
void aux_btree_walk_recids(const char *base_path, std::vector<int64_t> *out)
{
    TXvecBtreeWalkRecids(base_path, cb_push_recid, out);
}

/* ====================================================================
 * Path helpers.
 * ==================================================================== */

char *make_pq_path_suffix(const char *indfile, char suffix_letter)
{
    /* Output: "<indfile>_X.idxpq" — texis fulltext-style naming. */
    static const char SFX[] = ".idxpq";
    size_t base_len = std::strlen(indfile);
    char *p = (char *)std::malloc(base_len + 2 + sizeof(SFX));
    if (!p) return nullptr;
    std::memcpy(p, indfile, base_len);
    p[base_len]     = '_';
    p[base_len + 1] = suffix_letter;
    std::memcpy(p + base_len + 2, SFX, sizeof(SFX));
    return p;
}

/* ====================================================================
 * Codebook serialization — head file format.
 *
 * Layout (network-irrelevant, host endianness; matches FAISS internals):
 *   fourcc("ipq2")        4 B   magic + version
 *   d                     int32
 *   metric_type           int32
 *   nlist                 int64
 *   M                     int64
 *   nbits                 int32
 *   by_residual           int32  (always true for IVFPQ; recorded for
 *                                  forward-compat)
 *   nprobe_default        int64
 *   ntotal                int64  (Index base; informational)
 *   is_trained            int32
 *   max_recid_at_create   int64  (largest recid->off observed at CREATE;
 *                                  the segment-architecture keystone —
 *                                  rows whose recid->off exceeds this
 *                                  value were not in the sealed segment)
 *   coarse_quant_codes    vec<uint8_t>  (IndexFlatL2 raw bytes:
 *                                        nlist * d * sizeof(float))
 *   pq.d                  int64
 *   pq.M                  int64
 *   pq.nbits              int32
 *   pq.dsub               int64
 *   pq.ksub               int64
 *   pq.code_size          int64
 *   pq.centroids          vec<float>    (M * ksub * dsub floats)
 *   <invlist metadata>    OnDiskInvertedListsIOHook payload (fourcc
 *                                        "ilod" + slot/list/filename arrays)
 *
 * The invlist metadata's recorded `filename` is what FAISS will mmap
 * on read.  We pass IO_FLAG_ONDISK_SAME_DIR at read time so the .invl
 * is resolved relative to the head file's directory, surviving moves.
 *
 * Format changes are signaled by bumping the magic; pre-release indexes
 * built with an older magic must be REBUILD-ed.
 * ==================================================================== */

const uint32_t IPQ_MAGIC = faiss::fourcc("ipq2");

int save_ivfpq_head(const faiss::IndexIVFPQ *idx, faiss::IOWriter *f,
                    int64_t max_recid_at_create)
{
    try {
        uint32_t magic = IPQ_MAGIC;
        WRITE1(magic);

        int32_t d = idx->d;
        int32_t metric = (int32_t)idx->metric_type;
        int64_t nlist = (int64_t)idx->nlist;
        int64_t M = (int64_t)idx->pq.M;
        int32_t nbits = (int32_t)idx->pq.nbits;
        int32_t by_residual = idx->by_residual ? 1 : 0;
        int64_t nprobe = (int64_t)idx->nprobe;
        int64_t ntotal = (int64_t)idx->ntotal;
        int32_t is_trained = idx->is_trained ? 1 : 0;
        WRITE1(d);
        WRITE1(metric);
        WRITE1(nlist);
        WRITE1(M);
        WRITE1(nbits);
        WRITE1(by_residual);
        WRITE1(nprobe);
        WRITE1(ntotal);
        WRITE1(is_trained);
        WRITE1(max_recid_at_create);

        /* Coarse quantizer — IndexFlatL2.  We store the centroid bytes
         * directly; on load we'll reconstruct an IndexFlatL2 of size d
         * and copy the codes back in.  We use static_cast (not
         * dynamic_cast) because (a) we always construct the coarse
         * quantizer as IndexFlatL2 ourselves, so the type is known by
         * construction; (b) the FAISS subset is built with hidden
         * visibility, so dynamic_cast would fail to locate typeinfo
         * across the libfaiss_ivfpq.a / libtexisapi.a boundary. */
        const auto *flat = static_cast<const faiss::IndexFlat *>(idx->quantizer);
        WRITEVECTOR(flat->codes);

        /* PQ codebook. */
        int64_t pq_d        = (int64_t)idx->pq.d;
        int64_t pq_M        = (int64_t)idx->pq.M;
        int32_t pq_nbits    = (int32_t)idx->pq.nbits;
        int64_t pq_dsub     = (int64_t)idx->pq.dsub;
        int64_t pq_ksub     = (int64_t)idx->pq.ksub;
        int64_t pq_code_sz  = (int64_t)idx->pq.code_size;
        WRITE1(pq_d);
        WRITE1(pq_M);
        WRITE1(pq_nbits);
        WRITE1(pq_dsub);
        WRITE1(pq_ksub);
        WRITE1(pq_code_sz);
        WRITEVECTOR(idx->pq.centroids);

        /* InvList metadata — append the ilod block FAISS expects. */
        faiss::OnDiskInvertedListsIOHook hook;
        hook.write(idx->invlists, f);

        return 0;
    } catch (const faiss::FaissException &e) {
        std::fprintf(stderr, "save_ivfpq_head: %s\n", e.what());
        return -1;
    }
}

/* Reads the head, attaches a freshly mmapped OnDiskInvertedLists.
 * `invl_path_for_open` is the path the caller actually wants used for
 * the mmap (overrides the one stored in the head).  `max_recid_out`
 * receives the persisted max_recid_at_create boundary.  Returns owning
 * pointer or nullptr on error. */
faiss::IndexIVFPQ *load_ivfpq_head(faiss::IOReader *f,
                                   const char *invl_path_for_open,
                                   int64_t *max_recid_out)
{
    try {
        uint32_t magic = 0;
        READ1(magic);
        if (magic != IPQ_MAGIC) {
            std::fprintf(stderr, "load_ivfpq_head: bad magic 0x%08x "
                         "(expected ipq2 0x%08x — index needs REBUILD)\n",
                         (unsigned)magic, (unsigned)IPQ_MAGIC);
            return nullptr;
        }

        int32_t d, metric, nbits, by_residual, is_trained;
        int64_t nlist, M, nprobe, ntotal, max_recid_at_create;
        READ1(d);
        READ1(metric);
        READ1(nlist);
        READ1(M);
        READ1(nbits);
        READ1(by_residual);
        READ1(nprobe);
        READ1(ntotal);
        READ1(is_trained);
        READ1(max_recid_at_create);
        if (max_recid_out) *max_recid_out = max_recid_at_create;

        /* Reconstruct the coarse quantizer + codes. */
        auto *flat = new faiss::IndexFlat(d, (faiss::MetricType)metric);
        flat->ntotal = (faiss::idx_t)nlist;
        READVECTOR(flat->codes);
        if (flat->codes.size() != (size_t)nlist * (size_t)d * sizeof(float)) {
            std::fprintf(stderr,
                "load_ivfpq_head: coarse codes size mismatch "
                "(%zu vs %zu * %d * 4)\n",
                flat->codes.size(), (size_t)nlist, d);
            delete flat;
            return nullptr;
        }

        /* IVFPQ ctor allocates default invlists and PQ; we override
         * both immediately after. */
        auto *idx = new faiss::IndexIVFPQ(flat, d, (size_t)nlist,
                                          (size_t)M, (size_t)nbits,
                                          (faiss::MetricType)metric);
        idx->own_fields  = true;             /* destructor owns flat */
        idx->by_residual = by_residual ? true : false;
        idx->nprobe      = (size_t)nprobe;
        idx->ntotal      = (faiss::idx_t)ntotal;
        idx->is_trained  = is_trained ? true : false;

        /* PQ codebook. */
        int64_t pq_d, pq_M, pq_dsub, pq_ksub, pq_code_sz;
        int32_t pq_nbits;
        READ1(pq_d);
        READ1(pq_M);
        READ1(pq_nbits);
        READ1(pq_dsub);
        READ1(pq_ksub);
        READ1(pq_code_sz);
        idx->pq.d        = (size_t)pq_d;
        idx->pq.M        = (size_t)pq_M;
        idx->pq.nbits    = (size_t)pq_nbits;
        idx->pq.dsub     = (size_t)pq_dsub;
        idx->pq.ksub     = (size_t)pq_ksub;
        idx->pq.code_size = (size_t)pq_code_sz;
        READVECTOR(idx->pq.centroids);
        idx->code_size   = (size_t)pq_code_sz;

        /* Refresh PQ-derived caches (e.g., decompositions used by ADC). */
        idx->precompute_table();

        /* Inverted lists — peel the "ilod" fourcc that the hook's read
         * doesn't peel (the upstream dispatcher in read_InvertedLists
         * consumes it before delegating; we delegate directly). */
        uint32_t inv_h = 0;
        READ1(inv_h);
        if (inv_h != faiss::fourcc("ilod")) {
            std::fprintf(stderr,
                "load_ivfpq_head: invlist fourcc mismatch 0x%08x\n",
                (unsigned)inv_h);
            delete idx;
            return nullptr;
        }
        faiss::OnDiskInvertedListsIOHook hook;
        /* IO_FLAG_ONDISK_SAME_DIR makes the hook resolve the .invl
         * filename relative to our head file's directory.  Requires the
         * IOReader be a FileIOReader (ours is). */
        const int io_flags = faiss::IO_FLAG_ONDISK_SAME_DIR;
        faiss::InvertedLists *ils = hook.read(f, io_flags);
        if (!ils) { delete idx; return nullptr; }
        (void)invl_path_for_open;            /* SAME_DIR derives from f */

        idx->replace_invlists(ils, /*own=*/true);
        return idx;
    } catch (const faiss::FaissException &e) {
        std::fprintf(stderr, "load_ivfpq_head: %s\n", e.what());
        return nullptr;
    }
}

} /* anonymous namespace */

/* ====================================================================
 * Phase-1 stub probe — kept until phase 2 search wiring (task #12)
 * because some build paths reference it.  Real probe could be removed
 * once the dispatcher-side coverage is sufficient. */

extern "C" int TXvecIvfpqAvailable(void)
{
    TXvecFaissInitOnce();
    return 1;
}

/* ====================================================================
 * Backend slot implementations.
 * ==================================================================== */

namespace {

/* Drives texis METERs from FAISS k-means iteration callbacks.  We use
 * separate meters for the two phases of IVFPQ training because they
 * have wildly different per-iter costs (coarse: minutes/iter for
 * nlist=32k; PQ: sub-ms/iter for 8-dim slices) — a single combined
 * meter looks broken (crawls then jumps).  Coarse runs first; on the
 * first PQ callback we force the coarse meter to 100% (it may have
 * early-terminated below cp.niter), close it, and lazily open the PQ
 * meter so the new bar lands on a fresh line. */
struct CoarseProgressCallback : public faiss::ClusteringIterationCallback {
    METER       *meter;
    EPI_HUGEUINT total;
    EPI_HUGEUINT done;
    CoarseProgressCallback(METER *m, EPI_HUGEUINT n) : meter(m), total(n), done(0) {}
    void on_iteration(int iter, int n_iter, int redo, int n_redo,
                      const faiss::ClusteringIterationStats& stats) override {
        (void)iter; (void)n_iter; (void)redo; (void)n_redo; (void)stats;
        ++done;
        if (meter) meter_updatedone(meter, (EPI_HUGEINT)done);
        if (total > 0)
            TXsysupdateProgress(TXsysupdateGetCurrent(),
                (double)done / (double)total);
    }
};

struct PqProgressCallback : public faiss::ClusteringIterationCallback {
    CoarseProgressCallback *coarse_cb;   /* borrowed; we end its meter
                                          * at first PQ tick */
    METER       *meter;                  /* lazily opened on first tick */
    TXMDT        meter_type;             /* TXMDT_NONE → never open */
    EPI_HUGEUINT total;
    EPI_HUGEUINT done;
    const char  *label;
    PqProgressCallback(CoarseProgressCallback *c, TXMDT t, EPI_HUGEUINT n,
                       const char *lbl)
        : coarse_cb(c), meter(NULL), meter_type(t), total(n), done(0),
          label(lbl) {}
    void on_iteration(int iter, int n_iter, int redo, int n_redo,
                      const faiss::ClusteringIterationStats& stats) override {
        (void)iter; (void)n_iter; (void)redo; (void)n_redo; (void)stats;
        if (!meter && total > 0) {
            /* Hand off from coarse: force its meter to 100% (in case
             * coarse early-terminated) before closing, then open ours
             * on a fresh line. */
            if (coarse_cb && coarse_cb->meter) {
                meter_updatedone(coarse_cb->meter,
                                 coarse_cb->meter->totalsz);
                meter_end(coarse_cb->meter);
                closemeter(coarse_cb->meter);
                coarse_cb->meter = NULL;
            }
            if (meter_type != TXMDT_NONE)
                meter = openmeter((char *)label, meter_type,
                                  MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                                  (EPI_HUGEINT)total);
            /* SYSUPDATE: advance from "coarse k-means" (stage 3) to
             * "PQ subquantizers" (stage 4).  This fires the first
             * time PQ training ticks. */
            TXsysupdateAdvanceStage(TXsysupdateGetCurrent(), 4,
                                    "training PQ subquantizers");
        }
        ++done;
        if (meter) meter_updatedone(meter, (EPI_HUGEINT)done);
        if (total > 0)
            TXsysupdateProgress(TXsysupdateGetCurrent(),
                (double)done / (double)total);
    }
};

/* Capture (dev, ino, mtime, size) on _H.idxpq into the base.  Mirror of
 * vec_capture_file_id in vecindex.c, but on our head file. */
void ivfpq_capture_file_id(TXvecIvfpqHandle *h)
{
    struct stat st;
    if (h->head_path && ::stat(h->head_path, &st) == 0) {
        h->base.file_dev   = st.st_dev;
        h->base.file_ino   = st.st_ino;
        h->base.file_mtime = (EPI_OFF_T)st.st_mtime;
        h->base.file_size  = (EPI_OFF_T)st.st_size;
    } else {
        h->base.file_dev = 0;
        h->base.file_ino = 0;
        h->base.file_mtime = 0;
        h->base.file_size = 0;
    }
}

void ivfpq_handle_free(TXvecIvfpqHandle *h)
{
    if (!h) return;
    delete h->idx;     /* destructor owns flat quantizer + invlists */
    std::free(h->head_path);
    std::free(h->invl_path);
    std::free(h->tomb_base);
    std::free(h->newrec_base);
    std::free(h->base.path);
    std::free(h);
}

/* Byte-copy `src` to `dst` (creates/truncates `dst`).  Plain
 * read/write loop — no fs-level CoW shortcut so behavior is uniform
 * across filesystems.  If `meter` is non-NULL, ticks per chunk
 * written.  Returns 0 on success, -1 on error. */
static int
vec_copy_file_bytes(const char *src, const char *dst, METER *meter)
{
    static const char fn[] = "vec_copy_file_bytes";
    int sfd = ::open(src, O_RDONLY);
    if (sfd < 0) {
        putmsg(MERR + UGE, fn, "open `%s' for read: %s",
               src, std::strerror(errno));
        return -1;
    }
    int dfd = ::open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) {
        putmsg(MERR + UGE, fn, "open `%s' for write: %s",
               dst, std::strerror(errno));
        ::close(sfd);
        return -1;
    }
    /* Source size for SYSUPDATE Progress fraction.  Drive Progress
     * regardless of indexmeter setting — the byte-copy of a multi-GB
     * sealed invlist is the dominant cost of IVFPQ OPTIMIZE stage 1
     * and an observer with indexmeter='none' should still see the
     * stage-1 progress bar move. */
    EPI_OFF_T copy_total_bytes = 0;
    {
        struct stat sst;
        if (::fstat(sfd, &sst) == 0) copy_total_bytes = (EPI_OFF_T)sst.st_size;
    }
    char buf[64 * 1024];
    ssize_t r;
    EPI_HUGEINT done = 0;
    while ((r = ::read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = ::write(dfd, buf + off, (size_t)(r - off));
            if (w <= 0) {
                putmsg(MERR + UGE, fn, "write `%s': %s",
                       dst, std::strerror(errno));
                ::close(sfd); ::close(dfd);
                return -1;
            }
            off += w;
        }
        done += (EPI_HUGEINT)r;
        if (meter) meter_updatedone(meter, done);
        if (copy_total_bytes > 0)
            TXsysupdateProgress(TXsysupdateGetCurrent(),
                (double)done / (double)copy_total_bytes);
    }
    if (r < 0) {
        putmsg(MERR + UGE, fn, "read `%s': %s",
               src, std::strerror(errno));
        ::close(sfd); ::close(dfd);
        return -1;
    }
    ::close(sfd);
    if (::close(dfd) != 0) {
        putmsg(MERR + UGE, fn, "close `%s': %s",
               dst, std::strerror(errno));
        return -1;
    }
    return 0;
}

/* File-backed reservoir-sample of up to `k_max` row vectors from
 * `dbtbl` (column `fld`), converting each to f32 and writing into the
 * file at `train_path` row-major.  Returns the number of samples
 * actually written, or 0 on error.
 *
 * The file approach (mirroring rampart-langtools/rampart-faiss.c)
 * avoids holding `k_max × dim × sizeof(float)` bytes in RAM during
 * CREATE, which at 1M × 768 is 3 GB.  The training pass below mmaps
 * the file PROT_READ and hands the pointer to FAISS — kernel
 * page-cache management replaces explicit RAM ownership.
 *
 * Reservoir mechanics (Algorithm R): first `k_max` rows go to slots
 * 0..k_max-1 sequentially; subsequent rows replace a random slot
 * with probability k_max/(seen+1).  Random replacement uses fseek;
 * file size stays bounded at `min(seen, k_max) * dim * 4` bytes.
 */
size_t reservoir_sample_to_file(DBTBL *dbtbl, FLD *fld, int column_dtype,
                                float quant_scale, int quant_zp,
                                int dim, size_t k_max,
                                const char *train_path,
                                uint32_t seed,
                                METER *meter /* may be NULL */)
{
    FILE *fp = std::fopen(train_path, "w+b");
    if (!fp) return 0;

    size_t kept = 0;
    size_t seen = 0;
    std::mt19937 rng(seed);
    int t = fld->type & DDTYPEBITS;
    const size_t row_bytes = (size_t)dim * sizeof(float);
    std::vector<float> qbuf((size_t)dim);
    EPI_HUGEINT meter_done = 0;

    /* SYSUPDATE: stage-1 sampling can take many minutes on a large
     * table; report fractional progress so observers don't see a
     * 0%-stuck row.  Sized by table file bytes regardless of whether
     * indexmeter was set on the index. */
    EPI_OFF_T total_bytes = 0;
    {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            total_bytes = (EPI_OFF_T)st.st_size;
    }

    RECID *recid;
    TXrewinddbtbl(dbtbl);
    while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
        meter_done += (EPI_HUGEINT)dbtbl->tbl->irecsz;
        if (meter) METER_UPDATEDONE(meter, meter_done);
        if (total_bytes > 0)
            TXsysupdateProgress(TXsysupdateGetCurrent(),
                (double)meter_done / (double)total_bytes);

        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) continue;

        size_t cell_count = n_elems;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(column_dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) continue;
            cell_count = n_elems / elsz;
        }
        if ((int)cell_count != dim) continue;

        if (vec_convert_to_f32(column_dtype, raw, cell_count, dim,
                               quant_scale, quant_zp, qbuf.data()) != 0)
            continue;

        size_t slot = (size_t)-1;
        if (kept < k_max) {
            slot = kept++;
        } else {
            std::uniform_int_distribution<size_t> d(0, seen);
            size_t r = d(rng);
            if (r < k_max) slot = r;
        }
        if (slot != (size_t)-1) {
            if (std::fseek(fp, (long)(slot * row_bytes), SEEK_SET) != 0 ||
                std::fwrite(qbuf.data(), row_bytes, 1, fp) != 1) {
                std::fclose(fp);
                ::unlink(train_path);
                return 0;
            }
        }
        seen++;
    }
    std::fflush(fp);
    std::fclose(fp);
    return kept;
}

int ivfpq_create_impl(DDIC *ddic, DBTBL *dbtbl,
                      const char *field, const char *indname,
                      const char *indfile, TXindOpts *options,
                      TXvecParams *outParams)
{
    static const char fn[] = "TXvecCreateIndex(ivfpq)";
    (void)ddic; (void)indname;

    TXvecFaissInitOnce();

    /* 1. Resolve column field + dtype + dim.  Same logic as the HNSW
     *    create — use the first row to lock dim. */
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

    /* 2. Parse params.  TXvecParamsFromOptions has already validated
     *    `flush 'manual'` against backend=ivfpq for us (rejected). */
    TXvecParams vp;
    if (TXvecParamsFromOptions(&vp, options) < 0) return -1;
    if (vp.backend != VEC_BACKEND_IVFPQ) {
        /* Shouldn't happen — dispatcher routes here only on backend match. */
        putmsg(MERR + UGE, fn, "ivfpq_create called with backend=%d", vp.backend);
        return -1;
    }

    /* dtype resolution: typed varvec → use column's element type by
     * default; varbyte → require explicit vec_dtype.  Same semantics
     * as HNSW. */
    if (FTN_IS_VEC(t)) {
        if (vp.dtype == 0) vp.dtype = t;
    } else {
        if (vp.dtype == 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC on a byte/varbyte column requires `with "
                "vec_dtype '...'`");
            return -1;
        }
    }
    vec_params_apply_quant_defaults(&vp);

    /* 3. Pre-pass: count rows + lock dim (read first non-null row) +
     *    record the largest recid->off seen.  That largest value
     *    becomes max_recid_at_create — the boundary that lets later
     *    INSERTs know whether a recid could possibly collide with the
     *    sealed segment (≤ boundary) or is definitely fresh
     *    (> boundary).  Walk every row, including ones with NULL/
     *    dim-mismatched vectors, because the storage layer can
     *    reuse those recids on later UPDATEs.
     *
     *    The pre-pass only needs `recid->off` (and one `getfld()` for
     *    dim detection on the first non-null row); blob preload by
     *    gettblrow() would otherwise drag the full vector blob through
     *    TXblobiGetPayload() for every row.  On a 60GB+ vector table
     *    that's the bulk of the rebuild's I/O.  Suppress for the loop. */
    int dim = 0;
    size_t row_count = 0;
    int64_t max_recid_at_create = 0;
    EPI_OFF_T prepass_total = 0;
    {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            prepass_total = (EPI_OFF_T)st.st_size;
    }
    /* SYSUPDATE: stage 1 of 5 (scan table). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "scanning table");

    /* 3a. Tiny first-row walk to detect dim.  preLoadBlobs is
     * required (gettblrow checks the flag at row-fetch time, before
     * returning), so we keep the user's setting and bail out as soon
     * as we have a non-null vector. */
    {
        RECID *recid;
        TXrewinddbtbl(dbtbl);
        while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
            size_t n_elems = 0;
            void *raw = getfld(fld, &n_elems);
            if (!raw || n_elems == 0) continue;
            size_t cells = n_elems;
            if (t == FTN_BYTE) {
                size_t elsz = vec_dtype_elsz(vp.dtype);
                if (elsz == 0 || (n_elems % elsz) != 0) continue;
                cells = n_elems / elsz;
            }
            dim = (int)cells;
            break;
        }
    }

    /* 3b. Count + max_recid pass.  Suppress blob preload — the loop
     * only reads recid->off, but gettblrow() would otherwise drag
     * every row's vector blob through TXblobiGetPayload().  On a
     * 60GB+ vector table that's the bulk of the rebuild's I/O. */
    METER *prepass_meter = NULL;
    if (options && options->indexmeter != TXMDT_NONE && prepass_total > 0)
        prepass_meter = openmeter(
            (char *)"INDEX_VEC ivfpq stage 1/5 (scanning):",
            options->indexmeter,
            MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
            (EPI_HUGEINT)prepass_total);
    int saved_preLoad = (TXApp != NULL) ? TXApp->preLoadBlobs : 0;
    if (TXApp) TXApp->preLoadBlobs = 0;
    {
        RECID *recid;
        TXrewinddbtbl(dbtbl);
        while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
            EPI_OFF_T off = TXgetoff(recid);
            row_count++;
            if ((int64_t)(uint64_t)off > max_recid_at_create)
                max_recid_at_create = (int64_t)(uint64_t)off;
            if (prepass_meter)
                METER_UPDATEDONE(prepass_meter, (EPI_HUGEINT)off);
            if (prepass_total > 0)
                TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                    (double)off / (double)prepass_total);
        }
    }
    if (TXApp) TXApp->preLoadBlobs = saved_preLoad;
    if (prepass_meter) { meter_end(prepass_meter); closemeter(prepass_meter); }
    if (dim <= 0) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: cannot determine vector dimension "
            "(table has no usable rows)");
        return -1;
    }

    /* 4. Auto-tune nlist / M / nbits if user didn't supply them. */
    vec_params_apply_pq_defaults(&vp, dim, row_count);

    /* 5. Training-row check.  Floor matches FAISS's own internal
     *    "no-warning" threshold: 39 × max(nlist, ksub), where ksub =
     *    2^nbits = 256 for nbits=8.  Below this both the coarse
     *    k-means and the PQ sub-quantizer k-means produce
     *    poorly-distributed centroids.  See plan §4. */
    const size_t ksub = (size_t)1 << vp.pq_nbits;       /* 256 for nbits=8 */
    size_t k_for_train = (size_t)vp.pq_nlist;
    if (k_for_train < ksub) k_for_train = ksub;
    int min_ppc = (vp.pq_min_points_per_centroid > 0)
                ? vp.pq_min_points_per_centroid : 39;
    size_t need_train = (size_t)min_ppc * k_for_train;
    if (row_count < need_train) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC backend=ivfpq with nlist=%d (ksub=%zu, "
            "min_ppc=%d) requires at least %zu training rows; table has "
            "%zu.  Insert more data, lower vec_pq_min_points_per_centroid, "
            "lower vec_pq_nlist / vec_pq_target_rows, or use backend=hnsw.",
            vp.pq_nlist, ksub, min_ppc, need_train, row_count);
        return -1;
    }

    /* 6. Build skeleton: IndexIVFPQ + OnDiskInvertedLists from start. */
    char *head_path = make_pq_path_suffix(indfile, 'H');
    char *invl_path = make_pq_path_suffix(indfile, 'I');
    char *tomb_base   = TXvecMakeBtreeBasePath(indfile, "_del");
    char *newrec_base = TXvecMakeBtreeBasePath(indfile, "_T");
    if (!head_path || !invl_path || !tomb_base || !newrec_base) {
        std::free(head_path); std::free(invl_path);
        std::free(tomb_base); std::free(newrec_base);
        putmsg(MERR + MAE, fn, "alloc path"); return -1;
    }
    /* Erase any prior leftovers — DROP INDEX should have done this but
     * be defensive. */
    ::unlink(head_path); ::unlink(invl_path);
    {
        std::string tomb_btr   = std::string(tomb_base) + ".btr";
        std::string newrec_btr = std::string(newrec_base) + ".btr";
        ::unlink(tomb_btr.c_str());
        ::unlink(newrec_btr.c_str());
    }

    faiss::IndexIVFPQ *idx = nullptr;
    faiss::IndexFlat   *coarse = nullptr;
    faiss::OnDiskInvertedLists *invlists = nullptr;
    int rc_overall = -1;

    try {
        coarse = new faiss::IndexFlatL2(dim);
        idx = new faiss::IndexIVFPQ(coarse, dim,
                                    (size_t)vp.pq_nlist,
                                    (size_t)vp.pq_m,
                                    (size_t)vp.pq_nbits);
        idx->own_fields = true;     /* delete idx → delete coarse */
        idx->verbose = false;

        size_t code_size = idx->pq.code_size;     /* = pq_m for nbits=8 */
        invlists = new faiss::OnDiskInvertedLists(
            (size_t)vp.pq_nlist, code_size, invl_path);
        idx->replace_invlists(invlists, /*own=*/true);

        /* === STAGE 2/5: file-backed reservoir-sample =====================
         * Walks the table once, writing up to n_train f32 vectors into
         * a temp file.  Mirrors rampart-langtools/rampart-faiss.c —
         * avoids holding `n_train × dim × 4` bytes in RAM.
         *
         * n_train = need_train (FAISS's exact min_ppc × max(nlist,ksub)
         * threshold) + 2% headroom to absorb any NULL/dim-mismatched
         * rows the reservoir skips.  TXvecPqMaxTrainSamples acts as a
         * soft *floor*, not a ceiling: when nlist is small we
         * oversample to it for centroid quality, but when nlist
         * demands more we honor that.  Capped at row_count.
         */
        size_t n_train = need_train + need_train / 50;     /* +2% */
        const size_t train_floor = (TXvecPqMaxTrainSamples > 0)
                                   ? (size_t)TXvecPqMaxTrainSamples
                                   : 1000000u;
        if (n_train < train_floor) n_train = train_floor;
        if (n_train > row_count) n_train = row_count;

        std::string train_path_s = std::string(indfile) + ".train.tmp";
        const char *train_path = train_path_s.c_str();

        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq stage 2/5: sampling %zu of %zu rows "
            "(nlist=%d, M=%d, nbits=%d)",
            n_train, row_count, vp.pq_nlist, vp.pq_m, vp.pq_nbits);

        /* SYSUPDATE: stage 2 of 5 (sample/reservoir). */
        TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                                "sampling");

        METER *meter1 = NULL;
        if (options && options->indexmeter != TXMDT_NONE) {
            EPI_STAT_S st;
            EPI_OFF_T total = 0;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                total = (EPI_OFF_T)st.st_size;
            if (total > 0)
                meter1 = openmeter(
                    (char *)"INDEX_VEC ivfpq stage 2/5 (sampling):",
                    options->indexmeter,
                    MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                    (EPI_HUGEINT)total);
        }
        size_t got = reservoir_sample_to_file(dbtbl, fld,
            FTN_IS_VEC(t) ? t : vp.dtype,
            vp.quant_scale, vp.quant_zp, dim, n_train, train_path,
            /*seed=*/0xfa155eedu, meter1);
        if (meter1) { meter_end(meter1); closemeter(meter1); meter1 = NULL; }

        /* Safety net: first pass underdelivered (e.g. unexpectedly many
         * NULL/dim-mismatched rows).  Retry once with extra headroom,
         * up to row_count.  Reservoir reopen truncates the file so
         * this is a fresh pass, not an append. */
        if (got < need_train && n_train < row_count) {
            size_t bumped = need_train + need_train / 5;   /* +20% */
            if (bumped > row_count) bumped = row_count;
            if (bumped > n_train) {
                putmsg(MINFO, fn,
                    "INDEX_VEC: first pass yielded %zu valid rows < %zu "
                    "needed; retrying with target %zu",
                    got, need_train, bumped);
                n_train = bumped;
                if (options && options->indexmeter != TXMDT_NONE) {
                    EPI_STAT_S st;
                    EPI_OFF_T total = 0;
                    if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                        total = (EPI_OFF_T)st.st_size;
                    if (total > 0)
                        meter1 = openmeter(
                            (char *)"INDEX_VEC ivfpq stage 2/5 (sampling, retry):",
                            options->indexmeter,
                            MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                            (EPI_HUGEINT)total);
                }
                got = reservoir_sample_to_file(dbtbl, fld,
                    FTN_IS_VEC(t) ? t : vp.dtype,
                    vp.quant_scale, vp.quant_zp, dim, n_train, train_path,
                    /*seed=*/0xfa155eedu, meter1);
                if (meter1) { meter_end(meter1); closemeter(meter1); meter1 = NULL; }
            }
        }
        if (got < need_train) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: training sample produced %zu valid vectors; "
                "FAISS needs %zu (nlist=%d, min_ppc=%d).  Table has %zu "
                "rows total — many may be NULL or dim-mismatched.  "
                "Insert more data, lower vec_pq_min_points_per_centroid, "
                "or lower vec_pq_nlist.",
                got, need_train, vp.pq_nlist, min_ppc, row_count);
            ::unlink(train_path);
            goto build_err;
        }

        /* === STAGE 3-4/5: train ===========================================
         * Mmap the training file PROT_READ + MAP_PRIVATE.  FAISS does
         * multiple k-means iterations (default 25 each for the coarse
         * quantizer and for each of the M PQ subquantizers); the kernel
         * pages the file in on demand and evicts under memory pressure.
         *
         * No texis METER here — FAISS doesn't expose per-iteration
         * callbacks (yet).  Instead we enable FAISS's own verbose mode,
         * which prints to stderr per iteration:
         *   "  Iteration N (Xs, search Ys): obj=Z imbalance=I"
         * Combined with the wall-clock stage timer below, the user sees
         * real progress during the long part of training.
         */
        const size_t train_bytes = got * (size_t)dim * sizeof(float);
        int train_fd = ::open(train_path, O_RDONLY);
        if (train_fd < 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: open training file `%s': %s",
                train_path, std::strerror(errno));
            ::unlink(train_path);
            goto build_err;
        }
        void *train_addr = ::mmap(nullptr, train_bytes, PROT_READ,
                                  MAP_PRIVATE, train_fd, 0);
        ::close(train_fd);
        if (train_addr == MAP_FAILED) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: mmap training file `%s': %s",
                train_path, std::strerror(errno));
            ::unlink(train_path);
            goto build_err;
        }

        /* FAISS verbose stays off — its stderr/stdout iteration spam
         * was pre-_T.btr era and only useful when there was no other
         * progress signal.  We drive a texis METER from a per-iter
         * callback hook patched into faiss/Clustering.cpp instead. */
        idx->verbose    = false;
        idx->pq.verbose = false;

        /* Apply the user-tunable min-points-per-centroid (default 39
         * matches FAISS).  Lowering it lets tests train an IVFPQ index
         * from a few hundred rows rather than 10k+. */
        idx->cp.min_points_per_centroid    = min_ppc;
        idx->pq.cp.min_points_per_centroid = min_ppc;

        EPI_HUGEUINT coarse_iters =
            (EPI_HUGEUINT)idx->cp.niter * idx->cp.nredo;
        EPI_HUGEUINT pq_iters_per_slice =
            (EPI_HUGEUINT)idx->pq.cp.niter * idx->pq.cp.nredo;
        EPI_HUGEUINT pq_iters = (EPI_HUGEUINT)idx->pq.M * pq_iters_per_slice;
        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq stage 3/5: training "
            "(coarse: %llu iters; PQ: %d slices x %llu iters)",
            (unsigned long long)coarse_iters,
            (int)idx->pq.M, (unsigned long long)pq_iters_per_slice);

        /* SYSUPDATE: stage 3 of 5 (training, coarse k-means).
         * Stage 4 of 5 (PQ subquantizers) advances from inside the
         * PqProgressCallback's first on_iteration tick. */
        TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 3,
                                "training coarse k-means");

        TXMDT mtype = (options && options->indexmeter != TXMDT_NONE)
                      ? options->indexmeter : TXMDT_NONE;

        METER *coarse_meter = NULL;
        if (mtype != TXMDT_NONE && coarse_iters > 0) {
            coarse_meter = openmeter(
                (char *)"INDEX_VEC ivfpq stage 3/5 (coarse k-means):",
                mtype, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                (EPI_HUGEINT)coarse_iters);
        }
        CoarseProgressCallback coarse_cb(coarse_meter, coarse_iters);
        PqProgressCallback     pq_cb(&coarse_cb, mtype, pq_iters,
            "INDEX_VEC ivfpq stage 4/5 (PQ subquantizers):");
        idx->cp.iter_cb    = &coarse_cb;
        idx->pq.cp.iter_cb = &pq_cb;

        struct timespec t_train_start, t_train_end;
        clock_gettime(CLOCK_MONOTONIC, &t_train_start);
        try {
            idx->train(got, (const float *)train_addr);
        } catch (const faiss::FaissException &e) {
            idx->cp.iter_cb    = nullptr;
            idx->pq.cp.iter_cb = nullptr;
            if (coarse_cb.meter) { meter_end(coarse_cb.meter); closemeter(coarse_cb.meter); }
            if (pq_cb.meter)     { meter_end(pq_cb.meter);     closemeter(pq_cb.meter); }
            putmsg(MERR + UGE, fn, "FAISS train: %s", e.what());
            ::munmap(train_addr, train_bytes);
            ::unlink(train_path);
            goto build_err;
        }
        idx->cp.iter_cb    = nullptr;
        idx->pq.cp.iter_cb = nullptr;
        /* Force any still-open meter to 100% before close (covers
         * early-terminated coarse, and the normal PQ-finished case). */
        if (coarse_cb.meter) {
            meter_updatedone(coarse_cb.meter, coarse_cb.meter->totalsz);
            meter_end(coarse_cb.meter);
            closemeter(coarse_cb.meter);
        }
        if (pq_cb.meter) {
            meter_updatedone(pq_cb.meter, pq_cb.meter->totalsz);
            meter_end(pq_cb.meter);
            closemeter(pq_cb.meter);
        }

        clock_gettime(CLOCK_MONOTONIC, &t_train_end);
        double train_secs = (t_train_end.tv_sec - t_train_start.tv_sec) +
            (t_train_end.tv_nsec - t_train_start.tv_nsec) / 1e9;
        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq stage 3/5: training done in %.1fs", train_secs);
        ::munmap(train_addr, train_bytes);
        ::unlink(train_path);

        /* === STAGE 5/5: encode all rows ================================
         * Stream the table through `add_with_ids`; this is where the
         * PQ codes get written into the mmap'd OnDiskInvertedLists.
         * Sized by table file bytes for the meter.
         */
        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq stage 5/5: encoding %zu rows", row_count);

        /* SYSUPDATE: stage 5 of 5 (encoding). */
        TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 5,
                                "encoding");

        METER *meter3 = NULL;
        EPI_OFF_T encode3_total_bytes = 0;
        if (options && options->indexmeter != TXMDT_NONE) {
            EPI_STAT_S st;
            EPI_OFF_T total = 0;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                total = (EPI_OFF_T)st.st_size;
            encode3_total_bytes = total;
            if (total > 0)
                meter3 = openmeter(
                    (char *)"INDEX_VEC ivfpq stage 5/5 (encoding):",
                    options->indexmeter,
                    MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                    (EPI_HUGEINT)total);
        } else {
            EPI_STAT_S st;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                encode3_total_bytes = (EPI_OFF_T)st.st_size;
        }
        {
            std::vector<float> qbuf((size_t)dim);
            EPI_HUGEINT meter3_done = 0;
            RECID *recid;
            int column_dtype = FTN_IS_VEC(t) ? t : vp.dtype;
            TXrewinddbtbl(dbtbl);
            while ((recid = getdbtblrow(dbtbl)) != RECIDPN &&
                   TXrecidvalid(recid)) {
                /* gettblrow returns a pointer to a process-static RECID;
                 * any internal SQL (e.g. TXsysupdateProgress' UPDATE on
                 * SYSUPDATE) can stomp it.  Snapshot the offset before
                 * any such call. */
                EPI_OFF_T row_off = TXgetoff(recid);
                if (meter3) {
                    meter3_done += (EPI_HUGEINT)dbtbl->tbl->irecsz;
                    METER_UPDATEDONE(meter3, meter3_done);
                }
                if (encode3_total_bytes > 0)
                    TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                        (double)row_off / (double)encode3_total_bytes);
                size_t n_elems = 0;
                void *raw = getfld(fld, &n_elems);
                if (!raw || n_elems == 0) continue;
                size_t cells = n_elems;
                if (t == FTN_BYTE) {
                    size_t elsz = vec_dtype_elsz(column_dtype);
                    if (elsz == 0 || (n_elems % elsz) != 0) continue;
                    cells = n_elems / elsz;
                }
                if ((int)cells != dim) continue;
                if (vec_convert_to_f32(column_dtype, raw, cells, dim,
                                       vp.quant_scale, vp.quant_zp,
                                       qbuf.data()) != 0) continue;
                faiss::idx_t fid = (faiss::idx_t)(uint64_t)row_off;
                idx->add_with_ids(1, qbuf.data(), &fid);
            }
        }
        if (meter3) { meter_end(meter3); closemeter(meter3); }

        /* 9. Save head file (codebooks + max_recid_at_create boundary).
         * Bake the auto-scaled nprobe into the saved index, so loaders
         * that don't override get the right default for this nlist. */
        idx->nprobe = auto_nprobe((size_t)idx->nlist);
        {
            faiss::FileIOWriter w(head_path);
            if (save_ivfpq_head(idx, &w, max_recid_at_create) != 0) {
                putmsg(MERR + UGE, fn, "save_ivfpq_head failed for `%s'",
                       head_path);
                goto build_err;
            }
        }

        /* 10. Create empty `_T.btr` and `_del.btr`.  All post-CREATE
         *     INSERTs accumulate as recids in `_T.btr`; DELETEs of
         *     sealed-resident rows accumulate as recids in `_del.btr`;
         *     SEARCH unions sealed + delta + applies tombstone filter;
         *     ALTER INDEX OPTIMIZE folds them back into sealed.
         *     Mirrors texis fulltext's pattern from `index.c:1180,1187`. */
        if (TXvecBtreeCreateEmpty(tomb_base) != 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: failed to create tombstone btree `%s.btr'",
                tomb_base);
            goto build_err;
        }
        if (TXvecBtreeCreateEmpty(newrec_base) != 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC: failed to create newrec btree `%s.btr'",
                newrec_base);
            goto build_err;
        }

        /* 11. Hand parsed params back to caller (for SYSINDEX.PARAMS
         *     emission via TXvecParamsToText). */
        if (outParams) {
            *outParams = vp;
            outParams->graph.dim = dim;
            outParams->dtype     = vp.dtype;
        }

        rc_overall = (idx->ntotal == (faiss::idx_t)row_count) ? 1 : 0;
        goto cleanup;
    } catch (const faiss::FaissException &e) {
        putmsg(MERR + UGE, fn, "FAISS error during build: %s", e.what());
    } catch (const std::exception &e) {
        putmsg(MERR + UGE, fn, "build error: %s", e.what());
    }

build_err:
    rc_overall = -1;
    ::unlink(head_path);
    ::unlink(invl_path);
    {
        std::string tomb_btr   = std::string(tomb_base) + ".btr";
        std::string newrec_btr = std::string(newrec_base) + ".btr";
        ::unlink(tomb_btr.c_str());
        ::unlink(newrec_btr.c_str());
    }

cleanup:
    delete idx;     /* owns coarse + invlists */
    std::free(head_path);
    std::free(invl_path);
    std::free(tomb_base);
    std::free(newrec_base);
    return rc_overall;
}

TXvecHandle *ivfpq_open_impl(DDIC *ddic, const char *indfile,
                             const TXvecParams *vp)
{
    static const char fn[] = "TXvecOpen(ivfpq)";
    TXvecFaissInitOnce();

    char *head_path = make_pq_path_suffix(indfile, 'H');
    char *invl_path = make_pq_path_suffix(indfile, 'I');
    if (!head_path || !invl_path) {
        std::free(head_path); std::free(invl_path);
        return nullptr;
    }

    faiss::IndexIVFPQ *idx = nullptr;
    int64_t max_recid_at_create = 0;
    try {
        faiss::FileIOReader r(head_path);
        idx = load_ivfpq_head(&r, invl_path, &max_recid_at_create);
    } catch (const faiss::FaissException &e) {
        putmsg(MERR + UGE, fn, "load `%s': %s", head_path, e.what());
    }
    if (!idx) {
        std::free(head_path); std::free(invl_path);
        return nullptr;
    }

    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)std::calloc(1, sizeof(*h));
    if (!h) {
        delete idx;
        std::free(head_path); std::free(invl_path);
        return nullptr;
    }
    h->base.backend = VEC_BACKEND_IVFPQ;
    h->base.path    = strdup(indfile);
    h->base.ddic    = ddic;
    h->base.dim     = (int)idx->d;
    h->base.metric  = (idx->metric_type == faiss::METRIC_L2)
                      ? VEC_METRIC_L2 : VEC_METRIC_DOT;
    h->base.dtype   = vp->dtype ? vp->dtype : FTN_VEC_F32;
    h->head_path    = head_path;
    h->invl_path    = invl_path;
    h->tomb_base    = TXvecMakeBtreeBasePath(indfile, "_del");
    h->newrec_base  = TXvecMakeBtreeBasePath(indfile, "_T");
    h->idx          = idx;
    h->max_recid_at_create = max_recid_at_create;
    h->dirty_meta   = 0;

    ivfpq_capture_file_id(h);

    /* Splice into the shared cache (vecindex.c's private list).
     * Auxiliary btrees are NOT cached on the handle — each
     * INSERT/DELETE/SEARCH op opens them fresh and closes when
     * done, which avoids holding btree handles across SQL connection
     * close (the underlying dbf is connection-scoped, so a cached
     * BTREE* would get use-after-close on the next op). */
    vec_handle_cache_push(&h->base);

    return (TXvecHandle *)h;
}

void ivfpq_close_impl(TXvecHandle *h_)
{
    ivfpq_handle_free((TXvecIvfpqHandle *)h_);
}

int ivfpq_flush_impl(DDIC *ddic, TXvecHandle *h_)
{
    (void)ddic;
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    /* Auxiliary btrees (tomb_bt, newrec_bt) are managed by the texis
     * btree layer; their durability is per-btinsert (no in-memory
     * pending state to flush).  Nothing to do here for them.
     *
     * Rewrite head if the in-memory metadata changed.  max_recid_at_create
     * is immutable for the life of the sealed segment — only OPTIMIZE/
     * REBUILD recompute it — so the cached value is always right. */
    if (h->dirty_meta && h->idx) {
        try {
            faiss::FileIOWriter w(h->head_path);
            if (save_ivfpq_head(h->idx, &w, h->max_recid_at_create) != 0)
                return -1;
            h->dirty_meta = 0;
        } catch (const faiss::FaissException &e) {
            std::fprintf(stderr, "ivfpq_flush_impl: %s\n", e.what());
            return -1;
        }
    }
    return 0;
}

void ivfpq_drop_aux_impl(DDIC *ddic, const char *indfile)
{
    (void)ddic;
    char *hp = make_pq_path_suffix(indfile, 'H');
    char *ip = make_pq_path_suffix(indfile, 'I');
    char *tb = TXvecMakeBtreeBasePath(indfile, "_del");
    char *nr = TXvecMakeBtreeBasePath(indfile, "_T");
    if (hp) ::unlink(hp);
    if (ip) ::unlink(ip);
    if (tb) {
        std::string tbtr = std::string(tb) + ".btr";
        ::unlink(tbtr.c_str());
    }
    if (nr) {
        std::string nrbtr = std::string(nr) + ".btr";
        ::unlink(nrbtr.c_str());
    }
    /* Also unlink legacy `_d.vec` from the previous HNSW-delta design,
     * in case we're dropping an index built before the swap. */
    {
        std::string dvec = std::string(indfile) + "_d.vec";
        ::unlink(dvec.c_str());
    }
    std::free(hp); std::free(ip); std::free(tb); std::free(nr);
}

int ivfpq_is_stale_impl(TXvecHandle *h_)
{
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    struct stat st;
    if (::stat(h->head_path, &st) != 0) return 1;
    if (st.st_dev != h->base.file_dev || st.st_ino != h->base.file_ino) return 1;
    if ((EPI_OFF_T)st.st_mtime != h->base.file_mtime) return 1;
    if ((EPI_OFF_T)st.st_size  != h->base.file_size)  return 1;
    return 0;
}

/* IVFPQ vtable slot: search — unions sealed (PQ) + delta (HNSW).
 *
 * Each segment returns up to k_over hits; results are concatenated,
 * deduped (delta wins on id collision), sorted by metric-natural
 * score, and trimmed to k.  Delta scores are exact distances; sealed
 * scores are PQ-ADC approximations.  They live on the same numeric
 * scale (both squared-L2 for L2, both inner-product for IP), so a
 * direct comparison is sound for candidate generation — LIKEV's
 * caller is expected to vecdist re-rank for final ordering.
 *
 * Tombstone post-filter against sealed hits arrives in Phase D.  Until
 * then the storage layer's deleted-row check at SELECT time covers
 * the simple case (DELETE without recid reuse); recid-reuse can
 * surface stale sealed entries.
 *
 * `ef` parameter, if non-zero, is interpreted as nprobe on the sealed
 * side (IVFPQ's analogue of HNSW's expansion_search).  It's also
 * forwarded to the delta's HNSW search slot for symmetry.
 */
size_t ivfpq_search_impl(TXvecHandle *h_, DBTBL *dbtbl, const char *field,
                         const float *query, size_t k, size_t ef,
                         vec_search_result_t *results)
{
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    if (!h || !h->idx || !query || !results || k == 0) return SIZE_MAX;
    TXvecFaissInitOnce();

    float pad = TXvecPqOverFetchPad;
    if (pad < 0.0f) pad = 0.0f;
    size_t k_over = k + (size_t)((double)k * pad) + 1;

    /* Walk the auxiliary btrees fresh.  Open-and-close-per-op gives
     * cross-process correctness (each open reads fresh root pages from
     * disk).  Tiny btrees + cheap opens make this comfortable on the
     * search hot path. */
    std::unordered_set<uint64_t> tomb_set;
    std::vector<int64_t>         newrec_recids;
    {
        std::vector<int64_t> tomb_list;
        aux_btree_walk_recids(h->tomb_base, &tomb_list);
        tomb_set.reserve(tomb_list.size() * 2 + 1);
        for (int64_t r : tomb_list) tomb_set.insert((uint64_t)r);
        aux_btree_walk_recids(h->newrec_base, &newrec_recids);
    }
    std::unordered_set<uint64_t> newrec_set;
    newrec_set.reserve(newrec_recids.size() * 2 + 1);
    for (int64_t r : newrec_recids) newrec_set.insert((uint64_t)r);

    /* === Sealed (IVFPQ) — PQ-ADC search ============================== */
    size_t k_over_sealed = k_over;
    if (h->idx->ntotal > 0 && k_over_sealed > (size_t)h->idx->ntotal)
        k_over_sealed = (size_t)h->idx->ntotal;

    std::vector<vec_search_result_t> sealed_hits;
    if (k_over_sealed > 0) {
        faiss::SearchParametersIVF params;
        if (ef > 0)
            params.nprobe = ef;
        else if (TXlikevPqNprobe > 0)
            params.nprobe = (size_t)TXlikevPqNprobe;
        else
            params.nprobe = auto_nprobe(h->idx->nlist);

        std::vector<float>        dists(k_over_sealed);
        std::vector<faiss::idx_t> ids(k_over_sealed, -1);
        try {
            h->idx->search(1, query, k_over_sealed,
                           dists.data(), ids.data(), &params);
        } catch (const faiss::FaissException &e) {
            std::fprintf(stderr, "ivfpq_search (sealed): %s\n", e.what());
            return SIZE_MAX;
        }
        sealed_hits.reserve(k_over_sealed);
        for (size_t i = 0; i < k_over_sealed; i++) {
            if (ids[i] < 0) continue;
            uint64_t id = (uint64_t)ids[i];
            /* Filter sealed candidates against tombstone (deleted from
             * sealed) and against newrec (storage-reused: newrec wins
             * with the current vector). */
            if (tomb_set.count(id) != 0)   continue;
            if (newrec_set.count(id) != 0) continue;
            vec_search_result_t r;
            r.id    = (vec_id_t)id;
            r.score = dists[i];
            sealed_hits.push_back(r);
        }
    }

    /* === Delta — linear scan over `_T.btr` recids ==================== */
    std::vector<vec_search_result_t> delta_hits;
    if (!newrec_recids.empty() && dbtbl && field) {
        FLD *fld = dbnametofld(dbtbl, (char *)field);
        if (fld) {
            int t = fld->type & DDTYPEBITS;
            int column_dtype = (t == FTN_BYTE) ? h->base.dtype : t;
            std::vector<float> qbuf((size_t)h->base.dim);
            delta_hits.reserve(newrec_recids.size());
            /* Compute distances backend-natively: FAISS METRIC_L2 returns
             * squared L2 (lower=better); METRIC_INNER_PRODUCT returns
             * dot (higher=better).  Match those conventions here so the
             * merged sort works on a uniform scale. */
            bool use_dot = (h->idx->metric_type == faiss::METRIC_INNER_PRODUCT);
            for (int64_t recid_off : newrec_recids) {
                BTLOC bl;
                std::memset(&bl, 0, sizeof(bl));
                bl.off = (EPI_OFF_T)recid_off;
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
                                       /*scale*/0.0f, /*zp*/0,
                                       qbuf.data()) != 0) continue;
                /* Newrec entries are authoritative: if the recid is in
                 * newrec, the row at that recid is the current state
                 * (fetched fresh from storage above).  Even if the same
                 * recid is also in tomb_bt (UPDATE on a sealed-resident
                 * recid: DELETE adds tomb, INSERT adds newrec), newrec
                 * wins.  The tomb filter only applies to sealed-side
                 * candidates, which carry stale PQ codes. */
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
                vec_search_result_t r;
                r.id    = (vec_id_t)(uint64_t)recid_off;
                r.score = score;
                delta_hits.push_back(r);
            }
        }
    }

    /* === Merge sealed + delta, sort, trim to k ======================= */
    std::vector<vec_search_result_t> merged;
    merged.reserve(sealed_hits.size() + delta_hits.size());
    for (const auto &r : sealed_hits) merged.push_back(r);
    for (const auto &r : delta_hits)  merged.push_back(r);

    bool ascending = (h->base.metric == VEC_METRIC_L2);
    std::sort(merged.begin(), merged.end(),
              [ascending](const vec_search_result_t &a,
                          const vec_search_result_t &b) {
                  return ascending ? (a.score < b.score) : (a.score > b.score);
              });

    size_t got = merged.size() < k ? merged.size() : k;
    for (size_t i = 0; i < got; i++) results[i] = merged[i];
    return got;
}

/* IVFPQ vtable slot: per-row INSERT.
 *
 * Just record the recid in `_T.btr`.  No vector encoding, no graph
 * mutation, no full-file rewrite — pure btree insert.  At search
 * time the linear-scan path will fetch the row via gettblrow and
 * compute vecdist inline.  ALTER INDEX OPTIMIZE folds these back
 * into the sealed segment when the user asks.
 *
 * This is the texis-fulltext-style design — see texis-internals.md
 * §8.5/§8.7 for the full rationale. */
int ivfpq_add_row_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                       const char *field, RECID *recid)
{
    static const char fn[] = "TXvecAddRow(ivfpq)";
    (void)ddic; (void)dbtbl; (void)field;
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    if (!h || !recid || !h->newrec_base) return -1;

    int64_t r = (int64_t)(uint64_t)recid->off;
    if (TXvecBtreeInsertRecid(h->newrec_base, r) != 0) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC: btinsert into `_T.btr' failed for recid %lld",
            (long long)r);
        return -1;
    }
    return 0;
}

/* IVFPQ vtable slot: per-row DELETE.
 *
 * Three cases by recid:
 *   - recid > max_recid_at_create: row was inserted post-CREATE; its
 *     entry lives in `_T.btr`.  Remove it from there.  No tombstone
 *     needed (sealed never had this recid).
 *   - recid ≤ max_recid_at_create AND in `_T.btr`: storage-reused recid;
 *     remove from newrec AND tombstone the sealed entry.
 *   - recid ≤ max_recid_at_create AND NOT in `_T.btr`: pure sealed
 *     delete; just tombstone.
 *
 * In practice we always record a tombstone if recid ≤ boundary and
 * always try to remove from newrec — both are cheap and idempotent. */
int ivfpq_del_row_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                       const char *field, RECID *recid)
{
    static const char fn[] = "TXvecDelRow(ivfpq)";
    (void)ddic; (void)dbtbl; (void)field;
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    if (!h || !recid) return 0;

    int64_t r = (int64_t)(uint64_t)recid->off;

    /* Best-effort newrec removal — no-op if the recid isn't present. */
    if (h->newrec_base)
        TXvecBtreeDeleteRecid(h->newrec_base, r);

    /* Tombstone the sealed entry if this recid could be in sealed. */
    if (r <= h->max_recid_at_create && h->tomb_base) {
        if (TXvecBtreeInsertRecid(h->tomb_base, r) != 0) {
            putmsg(MWARN, fn,
                "INDEX_VEC: tombstone insert failed for recid %lld; "
                "subsequent SEARCH may return a stale sealed entry",
                (long long)r);
        }
    }
    return 0;
}

/* IVFPQ vtable slot: ALTER INDEX OPTIMIZE.
 *
 * Walks `_T.btr` (the newrec set) — these are the recids that have
 * been INSERTed since the last CREATE/REBUILD.  For each, fetch the
 * row from storage, encode its vector, add to sealed via add_with_ids.
 * After absorbing everything, truncate `_T.btr` and advance
 * max_recid_at_create.  Tombstones are kept — they still cover deletes
 * of rows that were sealed-resident before this OPTIMIZE.
 *
 * Mirrors the texis fulltext OPTIMIZE pattern (`updindex.c:wtix_getnewlist`):
 * walk newrec, fold into the main index, then empty newrec.
 */
int ivfpq_optimize_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                        const char *field, const char *tempBase,
                        TXindOpts *options,
                        int64_t **out_absorbed, size_t *out_n_absorbed)
{
    static const char fn[] = "TXvecOptimize(ivfpq)";
    (void)ddic;
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    if (!h || !h->idx || !dbtbl || !field || !tempBase) return -1;
    *out_absorbed = nullptr; *out_n_absorbed = 0;
    TXvecFaissInitOnce();

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

    /* Construct Tnnnn paths.  IVFPQ keeps two on-disk artifacts: _H
     * (head: codebooks + metadata) and _I (on-disk inverted lists). */
    char *temp_head = make_pq_path_suffix(tempBase, 'H');
    char *temp_invl = make_pq_path_suffix(tempBase, 'I');
    if (!temp_head || !temp_invl) {
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }
    char tempT[PATH_MAX], tempD[PATH_MAX];
    if (snprintf(tempT, sizeof(tempT), "%s_T",   tempBase) >= (int)sizeof(tempT) ||
        snprintf(tempD, sizeof(tempD), "%s_del", tempBase) >= (int)sizeof(tempD)) {
        putmsg(MERR + UGE, fn, "tempBase path too long");
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    /* Snapshot newrec recids from live `_T.btr'. */
    std::unordered_set<uint64_t> newrec_set;
    std::vector<int64_t> newrec_recids;
    aux_btree_walk_recids(h->newrec_base, &newrec_recids);
    newrec_set.reserve(newrec_recids.size() * 2 + 1);
    for (int64_t r : newrec_recids) newrec_set.insert((uint64_t)r);

    /* SYSUPDATE: stage 1 of 2 (copy sealed).  Independent of meter. */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "copy sealed");

    /* Byte-copy live invlist → temp.  This is the OPTIMIZE cost knob:
     * it scales with sealed (`_I.idxpq`) size, not delta size.  Drive
     * a meter from the connection's indexmeter setting so the user
     * sees progress on a large index. */
    METER *copy_meter = NULL;
    if (options && options->indexmeter != TXMDT_NONE) {
        EPI_STAT_S st;
        if (::stat(h->invl_path, &st) == 0 && st.st_size > 0)
            copy_meter = openmeter(
                (char *)"INDEX_VEC ivfpq OPTIMIZE stage 1/2 (copy sealed):",
                options->indexmeter,
                MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                (EPI_HUGEINT)st.st_size);
    }
    int copy_rc = vec_copy_file_bytes(h->invl_path, temp_invl, copy_meter);
    if (copy_meter) { meter_end(copy_meter); closemeter(copy_meter); }
    /* SYSUPDATE: copy stage finished — flush 100% before next stage. */
    TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink, 1.0);
    if (copy_rc != 0) {
        ::unlink(temp_invl);
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    /* The head file embeds the invlist filename which SAME_DIR uses
     * (head_dir + basename(filename)) to mmap.  To make a temp_idx
     * that mmaps temp_invl (not live_invl), we patch the temp_head:
     * load the live head, override the recorded filename to temp_invl,
     * save to temp_head.  Now load_ivfpq_head(temp_head, ..., SAME_DIR)
     * resolves to temp_invl. */
    {
        int64_t dummy_max = 0;
        faiss::IndexIVFPQ *patcher = nullptr;
        try {
            faiss::FileIOReader r(h->head_path);
            patcher = load_ivfpq_head(&r, h->invl_path, &dummy_max);
        } catch (const faiss::FaissException &e) {
            putmsg(MERR + UGE, fn, "load live for patch: %s", e.what());
        }
        if (!patcher) {
            ::unlink(temp_invl);
            std::free(temp_head); std::free(temp_invl);
            return -1;
        }
        auto *od = static_cast<faiss::OnDiskInvertedLists *>(patcher->invlists);
        od->filename = temp_invl;        /* override for SAME_DIR resolution */
        try {
            faiss::FileIOWriter w(temp_head);
            (void)save_ivfpq_head(patcher, &w, dummy_max);
        } catch (const faiss::FaissException &e) {
            putmsg(MERR + UGE, fn, "patch temp_head: %s", e.what());
            delete patcher;
            ::unlink(temp_head); ::unlink(temp_invl);
            std::free(temp_head); std::free(temp_invl);
            return -1;
        }
        delete patcher;     /* unmaps live_invl; safe — no writes happened */
    }

    /* Open a separate IVFPQ on the temp files.  SAME_DIR resolves the
     * recorded filename's basename against temp_head's directory; the
     * patched filename above is `temp_invl` itself, so we mmap temp.
     * Mutations land here, not on the live files. */
    faiss::IndexIVFPQ *temp_idx = nullptr;
    int64_t temp_max = 0;
    try {
        faiss::FileIOReader r(temp_head);
        temp_idx = load_ivfpq_head(&r, temp_invl, &temp_max);
    } catch (const faiss::FaissException &e) {
        putmsg(MERR + UGE, fn, "load temp `%s': %s", temp_head, e.what());
    }
    if (!temp_idx) {
        ::unlink(temp_head); ::unlink(temp_invl);
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    const int dim = h->base.dim;
    const int column_dtype = (t == FTN_BYTE) ? h->base.dtype : t;
    int64_t  new_max  = temp_max;
    std::vector<int64_t> absorbed;
    absorbed.reserve(newrec_recids.size());
    std::vector<float> qbuf((size_t)dim);

    /* Walk the table sequentially.  For recids in newrec_set, encode
     * the row vector and add to temp_idx.  Track new_max for
     * max_recid_at_create.
     *
     * Meter is sized to table file bytes and ticks on every visited
     * row (its recid offset).  This matches the CREATE-time encoding
     * meter so progress advances smoothly through the unmatched
     * leading rows — sizing to delta count would leave it at 0% until
     * the walk reaches the new INSERTs at the tail of the table. */
    /* SYSUPDATE: stage 2 of 2 (encode delta).  Independent of meter. */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                            "encode delta");

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
                (char *)"INDEX_VEC ivfpq OPTIMIZE stage 2/2 (encode delta):",
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
        if (r > new_max) new_max = r;
        if (encode_meter)
            METER_UPDATEDONE(encode_meter, (EPI_HUGEINT)r);
        /* SYSUPDATE: report fractional progress through encode stage,
         * independent of indexmeter.  Rate-limited inside the helper. */
        if (encode_total_bytes > 0)
            TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                (double)r / (double)encode_total_bytes);
        if (newrec_set.count((uint64_t)r) == 0) continue;

        size_t n_elems = 0;
        void *raw = getfld(fld, &n_elems);
        if (!raw || n_elems == 0) continue;
        size_t cells = n_elems;
        if (t == FTN_BYTE) {
            size_t elsz = vec_dtype_elsz(column_dtype);
            if (elsz == 0 || (n_elems % elsz) != 0) continue;
            cells = n_elems / elsz;
        }
        if ((int)cells != dim) continue;
        if (vec_convert_to_f32(column_dtype, raw, cells, dim,
                               /*scale*/0.0f, /*zp*/0, qbuf.data()) != 0)
            continue;
        faiss::idx_t fid = (faiss::idx_t)r;
        try {
            temp_idx->add_with_ids(1, qbuf.data(), &fid);
        } catch (const faiss::FaissException &e) {
            if (encode_meter) { meter_end(encode_meter); closemeter(encode_meter); }
            putmsg(MERR + UGE, fn,
                "FAISS add_with_ids for recid %lld: %s",
                (long long)r, e.what());
            delete temp_idx;
            ::unlink(temp_head); ::unlink(temp_invl);
            std::free(temp_head); std::free(temp_invl);
            return -1;
        }
        absorbed.push_back(r);
    }
    if (encode_meter) { meter_end(encode_meter); closemeter(encode_meter); }

    /* Save the temp head with the advanced boundary.
     *
     * Subtle: save_ivfpq_head emits an OnDiskInvertedListsIOHook block
     * that records the invlist filename used by the FAISS index
     * object.  When loaded with IO_FLAG_ONDISK_SAME_DIR, FAISS resolves
     * `head_dir + basename(recorded_filename)` for the mmap.  Our
     * temp_idx points at temp_invl, so the recorded basename would be
     * "Tnnnn_I.idxpq" — but after the dispatcher's atomic rename the
     * live invl will be at <indfile>_I.idxpq.  Override the recorded
     * filename to the live path before save so the basename matches
     * what's on disk after commit. */
    {
        auto *od = static_cast<faiss::OnDiskInvertedLists *>(temp_idx->invlists);
        std::string saved_fname = od->filename;
        od->filename = h->invl_path;
        /* Bake auto-scaled nprobe before save (mirrors CREATE path). */
        temp_idx->nprobe = auto_nprobe((size_t)temp_idx->nlist);
        try {
            faiss::FileIOWriter w(temp_head);
            if (save_ivfpq_head(temp_idx, &w, new_max) != 0) {
                putmsg(MERR + UGE, fn, "save_ivfpq_head failed");
                od->filename = saved_fname;
                delete temp_idx;
                ::unlink(temp_head); ::unlink(temp_invl);
                std::free(temp_head); std::free(temp_invl);
                return -1;
            }
        } catch (const faiss::FaissException &e) {
            putmsg(MERR + UGE, fn, "save_ivfpq_head: %s", e.what());
            od->filename = saved_fname;
            delete temp_idx;
            ::unlink(temp_head); ::unlink(temp_invl);
            std::free(temp_head); std::free(temp_invl);
            return -1;
        }
        od->filename = saved_fname;
    }
    delete temp_idx;
    std::free(temp_head); std::free(temp_invl);

    /* Empty Tnnnn aux btrees.  Dispatcher's carry-forward populates
     * them at commit from live aux btrees, filtered by the absorbed
     * set we return.  Pre-create fresh empty files so the rename loop
     * has something to swap. */
    (void)TXvecBtreeUnlink(tempT);
    (void)TXvecBtreeUnlink(tempD);
    if (TXvecBtreeCreateEmpty(tempT) != 0 ||
        TXvecBtreeCreateEmpty(tempD) != 0) {
        putmsg(MERR + UGE, fn, "create empty Tnnnn aux btrees failed");
        return -1;
    }

    /* Sort and hand off the absorbed set. */
    int64_t *abs_arr = nullptr;
    size_t   abs_n   = absorbed.size();
    if (abs_n > 0) {
        abs_arr = (int64_t *)std::malloc(abs_n * sizeof(int64_t));
        if (!abs_arr) { putmsg(MERR + MAE, fn, "alloc absorbed"); return -1; }
        std::memcpy(abs_arr, absorbed.data(), abs_n * sizeof(int64_t));
        std::qsort(abs_arr, abs_n, sizeof(int64_t), vec_int64_cmp_);
    }
    *out_absorbed = abs_arr;
    *out_n_absorbed = abs_n;

    putmsg(MINFO, fn,
        "INDEX_VEC ivfpq OPTIMIZE: absorbed %zu rows into temp; "
        "max_recid_at_create %lld → %lld",
        abs_n, (long long)temp_max, (long long)new_max);
    return 0;
}

/* IVFPQ vtable slot: ALTER INDEX REBUILD.
 *
 * Atomic-swap design: build all new artifacts under the dispatcher-
 * supplied Tnnnn temp basename; live files stay untouched until the
 * dispatcher's commit phase (under brief table W_LCK).  Pipeline
 * mirrors ivfpq_create_impl's body, just writing to Tnnnn paths:
 *   - reservoir-sample training rows from the table.
 *   - train fresh IVFPQ.
 *   - encode all rows into Tnnnn_I.idxpq via add_with_ids.
 *   - save Tnnnn_H.idxpq.
 *   - empty Tnnnn_T.btr / Tnnnn_del.btr.
 * Returns the absorbed-recid list so the dispatcher's carry-forward
 * picks up post-walk INSERTs/DELETEs against the live aux btrees. */
int ivfpq_rebuild_impl(DDIC *ddic, TXvecHandle *h_, DBTBL *dbtbl,
                       const char *field, const TXvecParams *vp_in,
                       const char *tempBase, TXindOpts *options,
                       int64_t **out_absorbed, size_t *out_n_absorbed)
{
    static const char fn[] = "TXvecRebuild(ivfpq)";
    TXvecIvfpqHandle *h = (TXvecIvfpqHandle *)h_;
    if (!h || !dbtbl || !field || !vp_in || !tempBase) return -1;
    *out_absorbed = nullptr; *out_n_absorbed = 0;
    TXvecFaissInitOnce();
    (void)ddic;

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

    TXvecParams vp = *vp_in;
    if (vp.dtype == 0) vp.dtype = h->base.dtype;
    vec_params_apply_quant_defaults(&vp);

    const int dim = h->base.dim;        /* preserved across REBUILD */
    const char *indfile = h->base.path;
    if (!indfile) return -1;

    /* Free the existing IVFPQ idx + invlists mmap before the pre-pass.
     * REBUILD overwrites the index from scratch — we only needed the
     * handle for the metadata above (dim, path, dtype).  Holding the
     * 8.6 GB invlists mmap + ~1-2 GB precompute_table allocation
     * during the table walk causes a >10x slowdown on macOS for
     * reasons not fully understood (page-cache competition, VM
     * accounting, or both — `sudo purge` between runs doesn't fix
     * it).  Cached handle entry remains; vec_commit_temp_swap's
     * is_stale check will detect the post-rename inode and trigger
     * a fresh open from the rebuilt files.  Search-server processes
     * are unaffected — they have their own per-process handle and
     * mmap of the live files. */
    if (h->idx) {
        delete h->idx;
        h->idx = nullptr;
    }

    /* Tnnnn artifact paths.  Live files (h->head_path / h->invl_path)
     * are NOT touched; they keep serving searches until the
     * dispatcher commits the rename. */
    char *temp_head = make_pq_path_suffix(tempBase, 'H');
    char *temp_invl = make_pq_path_suffix(tempBase, 'I');
    if (!temp_head || !temp_invl) {
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }
    char tempT[PATH_MAX], tempD[PATH_MAX];
    if (snprintf(tempT, sizeof(tempT), "%s_T",   tempBase) >= (int)sizeof(tempT) ||
        snprintf(tempD, sizeof(tempD), "%s_del", tempBase) >= (int)sizeof(tempD)) {
        putmsg(MERR + UGE, fn, "tempBase path too long");
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    /* Pre-pass: count rows + record max recid.  Suppress blob preload
     * — the loop only reads recid->off, but gettblrow() would otherwise
     * drag every row's vector blob through TXblobiGetPayload().  On a
     * 60GB+ vector table that's the bulk of the rebuild's I/O. */
    size_t row_count = 0;
    int64_t new_max  = 0;
    EPI_OFF_T prepass_total = 0;
    {
        EPI_STAT_S st;
        if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
            prepass_total = (EPI_OFF_T)st.st_size;
    }
    /* SYSUPDATE: stage 1 of 5 (scan table). */
    TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 1,
                            "scanning table");
    METER *prepass_meter = NULL;
    if (options && options->indexmeter != TXMDT_NONE && prepass_total > 0)
        prepass_meter = openmeter(
            (char *)"INDEX_VEC ivfpq REBUILD stage 1/5 (scanning):",
            options->indexmeter,
            MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
            (EPI_HUGEINT)prepass_total);
    int saved_preLoad = (TXApp != NULL) ? TXApp->preLoadBlobs : 0;
    if (TXApp) TXApp->preLoadBlobs = 0;
    {
        RECID *recid;
        TXrewinddbtbl(dbtbl);
        while ((recid = getdbtblrow(dbtbl)) != RECIDPN && TXrecidvalid(recid)) {
            EPI_OFF_T off = TXgetoff(recid);
            row_count++;
            if ((int64_t)(uint64_t)off > new_max)
                new_max = (int64_t)(uint64_t)off;
            if (prepass_meter)
                METER_UPDATEDONE(prepass_meter, (EPI_HUGEINT)off);
            if (prepass_total > 0)
                TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                    (double)off / (double)prepass_total);
        }
    }
    if (TXApp) TXApp->preLoadBlobs = saved_preLoad;
    if (prepass_meter) { meter_end(prepass_meter); closemeter(prepass_meter); }
    if (row_count == 0) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC REBUILD: table is empty; nothing to rebuild");
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    vec_params_apply_pq_defaults(&vp, dim, row_count);

    /* Training-row floor. */
    const size_t ksub = (size_t)1 << vp.pq_nbits;
    size_t k_for_train = (size_t)vp.pq_nlist;
    if (k_for_train < ksub) k_for_train = ksub;
    int min_ppc = (vp.pq_min_points_per_centroid > 0)
                ? vp.pq_min_points_per_centroid : 39;
    size_t need_train = (size_t)min_ppc * k_for_train;
    if (row_count < need_train) {
        putmsg(MERR + UGE, fn,
            "INDEX_VEC REBUILD requires at least %zu training rows; "
            "table has %zu", need_train, row_count);
        std::free(temp_head); std::free(temp_invl);
        return -1;
    }

    faiss::IndexIVFPQ *idx = nullptr;
    int64_t *abs_arr = nullptr;
    size_t abs_n = 0;
    int rc_overall = -1;
    try {
        auto *coarse = new faiss::IndexFlatL2(dim);
        idx = new faiss::IndexIVFPQ(coarse, dim,
                                    (size_t)vp.pq_nlist,
                                    (size_t)vp.pq_m,
                                    (size_t)vp.pq_nbits);
        idx->own_fields = true;
        idx->verbose    = false;

        auto *invlists = new faiss::OnDiskInvertedLists(
            (size_t)vp.pq_nlist, idx->pq.code_size, temp_invl);
        idx->replace_invlists(invlists, /*own=*/true);

        /* === STAGE 2/5: reservoir-sample training rows =====================
         * See ivfpq_create_impl for the n_train rationale (FAISS exact
         * threshold + 2% headroom; floor not ceiling on TXvecPqMaxTrain-
         * Samples; one-shot retry on under-delivery).
         */
        size_t n_train = need_train + need_train / 50;     /* +2% */
        const size_t train_floor = (TXvecPqMaxTrainSamples > 0)
                                   ? (size_t)TXvecPqMaxTrainSamples
                                   : 1000000u;
        if (n_train < train_floor) n_train = train_floor;
        if (n_train > row_count) n_train = row_count;

        std::string train_path_s = std::string(tempBase) + ".train.tmp";
        const char *train_path = train_path_s.c_str();

        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq REBUILD stage 2/5: sampling %zu of %zu rows "
            "(nlist=%d, M=%d, nbits=%d)",
            n_train, row_count, vp.pq_nlist, vp.pq_m, vp.pq_nbits);

        /* SYSUPDATE: stage 2 of 5 (sampling). */
        TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 2,
                                "sampling");

        METER *meter1 = NULL;
        if (options && options->indexmeter != TXMDT_NONE) {
            EPI_STAT_S st;
            EPI_OFF_T total = 0;
            if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                total = (EPI_OFF_T)st.st_size;
            if (total > 0)
                meter1 = openmeter(
                    (char *)"INDEX_VEC ivfpq REBUILD stage 2/5 (sampling):",
                    options->indexmeter,
                    MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                    (EPI_HUGEINT)total);
        }
        size_t got = reservoir_sample_to_file(dbtbl, fld,
            FTN_IS_VEC(t) ? t : vp.dtype,
            vp.quant_scale, vp.quant_zp, dim, n_train, train_path,
            /*seed=*/0xfa155eedu, meter1);
        if (meter1) { meter_end(meter1); closemeter(meter1); meter1 = NULL; }

        if (got < need_train && n_train < row_count) {
            size_t bumped = need_train + need_train / 5;   /* +20% */
            if (bumped > row_count) bumped = row_count;
            if (bumped > n_train) {
                putmsg(MINFO, fn,
                    "INDEX_VEC REBUILD: first pass yielded %zu valid rows "
                    "< %zu needed; retrying with target %zu",
                    got, need_train, bumped);
                n_train = bumped;
                if (options && options->indexmeter != TXMDT_NONE) {
                    EPI_STAT_S st;
                    EPI_OFF_T total = 0;
                    if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                        total = (EPI_OFF_T)st.st_size;
                    if (total > 0)
                        meter1 = openmeter(
                            (char *)"INDEX_VEC ivfpq REBUILD stage 2/5 (sampling, retry):",
                            options->indexmeter,
                            MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                            (EPI_HUGEINT)total);
                }
                got = reservoir_sample_to_file(dbtbl, fld,
                    FTN_IS_VEC(t) ? t : vp.dtype,
                    vp.quant_scale, vp.quant_zp, dim, n_train, train_path,
                    /*seed=*/0xfa155eedu, meter1);
                if (meter1) { meter_end(meter1); closemeter(meter1); meter1 = NULL; }
            }
        }
        if (got < need_train) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC REBUILD: training sample produced %zu valid "
                "vectors; FAISS needs %zu (nlist=%d, min_ppc=%d).  Table "
                "has %zu rows — many may be NULL or dim-mismatched.  "
                "Insert more data, lower vec_pq_min_points_per_centroid, "
                "or lower vec_pq_nlist.",
                got, need_train, vp.pq_nlist, min_ppc, row_count);
            ::unlink(train_path);
            goto rebuild_err;
        }

        /* === STAGE 3-4/5: train =============================================== */
        {
            const size_t train_bytes = got * (size_t)dim * sizeof(float);
            int train_fd = ::open(train_path, O_RDONLY);
            if (train_fd < 0) {
                putmsg(MERR + UGE, fn, "open train file: %s",
                       std::strerror(errno));
                ::unlink(train_path);
                goto rebuild_err;
            }
            void *train_addr = ::mmap(nullptr, train_bytes, PROT_READ,
                                      MAP_PRIVATE, train_fd, 0);
            ::close(train_fd);
            if (train_addr == MAP_FAILED) {
                putmsg(MERR + UGE, fn, "mmap train file: %s",
                       std::strerror(errno));
                ::unlink(train_path);
                goto rebuild_err;
            }

            EPI_HUGEUINT coarse_iters =
                (EPI_HUGEUINT)idx->cp.niter * idx->cp.nredo;
            EPI_HUGEUINT pq_iters_per_slice =
                (EPI_HUGEUINT)idx->pq.cp.niter * idx->pq.cp.nredo;
            EPI_HUGEUINT pq_iters =
                (EPI_HUGEUINT)idx->pq.M * pq_iters_per_slice;
            putmsg(MINFO, fn,
                "INDEX_VEC ivfpq REBUILD stage 3/5: training "
                "(coarse: %llu iters; PQ: %d slices x %llu iters)",
                (unsigned long long)coarse_iters,
                (int)idx->pq.M, (unsigned long long)pq_iters_per_slice);
            idx->verbose    = false;
            idx->pq.verbose = false;
            idx->cp.min_points_per_centroid    = min_ppc;
            idx->pq.cp.min_points_per_centroid = min_ppc;

            /* SYSUPDATE: stage 3 of 5 (training, coarse k-means).
             * Stage 4 (PQ subquantizers) advances inside the
             * PqProgressCallback's first on_iteration tick. */
            TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 3,
                                    "training coarse k-means");

            TXMDT mtype = (options && options->indexmeter != TXMDT_NONE)
                          ? options->indexmeter : TXMDT_NONE;
            METER *coarse_meter = NULL;
            if (mtype != TXMDT_NONE && coarse_iters > 0) {
                coarse_meter = openmeter(
                    (char *)"INDEX_VEC ivfpq REBUILD stage 3/5 (coarse k-means):",
                    mtype, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                    (EPI_HUGEINT)coarse_iters);
            }
            CoarseProgressCallback coarse_cb(coarse_meter, coarse_iters);
            PqProgressCallback     pq_cb(&coarse_cb, mtype, pq_iters,
                "INDEX_VEC ivfpq REBUILD stage 4/5 (PQ subquantizers):");
            idx->cp.iter_cb    = &coarse_cb;
            idx->pq.cp.iter_cb = &pq_cb;
            try {
                idx->train(got, (const float *)train_addr);
            } catch (const faiss::FaissException &e) {
                idx->cp.iter_cb    = nullptr;
                idx->pq.cp.iter_cb = nullptr;
                if (coarse_cb.meter) { meter_end(coarse_cb.meter); closemeter(coarse_cb.meter); }
                if (pq_cb.meter)     { meter_end(pq_cb.meter);     closemeter(pq_cb.meter); }
                putmsg(MERR + UGE, fn, "FAISS train: %s", e.what());
                ::munmap(train_addr, train_bytes);
                ::unlink(train_path);
                goto rebuild_err;
            }
            idx->cp.iter_cb    = nullptr;
            idx->pq.cp.iter_cb = nullptr;
            if (coarse_cb.meter) {
                meter_updatedone(coarse_cb.meter, coarse_cb.meter->totalsz);
                meter_end(coarse_cb.meter);
                closemeter(coarse_cb.meter);
            }
            if (pq_cb.meter) {
                meter_updatedone(pq_cb.meter, pq_cb.meter->totalsz);
                meter_end(pq_cb.meter);
                closemeter(pq_cb.meter);
            }
            ::munmap(train_addr, train_bytes);
            ::unlink(train_path);
        }

        /* === STAGE 5/5: encode all rows ===================================== */
        putmsg(MINFO, fn,
            "INDEX_VEC ivfpq REBUILD stage 5/5: encoding %zu rows", row_count);

        /* SYSUPDATE: stage 5 of 5 (encoding). */
        TXsysupdateAdvanceStage((TXsysupdateSink *)ddic->sysupdSink, 5,
                                "encoding");

        abs_arr = (int64_t *)std::malloc(row_count * sizeof(int64_t));
        if (!abs_arr) {
            putmsg(MERR + MAE, fn, "alloc absorbed");
            goto rebuild_err;
        }
        {
            METER *meter3 = NULL;
            EPI_OFF_T encode3_total_bytes = 0;
            if (options && options->indexmeter != TXMDT_NONE) {
                EPI_STAT_S st;
                EPI_OFF_T total = 0;
                if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                    total = (EPI_OFF_T)st.st_size;
                encode3_total_bytes = total;
                if (total > 0)
                    meter3 = openmeter(
                        (char *)"INDEX_VEC ivfpq REBUILD stage 5/5 (encoding):",
                        options->indexmeter,
                        MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                        (EPI_HUGEINT)total);
            } else {
                EPI_STAT_S st;
                if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
                    encode3_total_bytes = (EPI_OFF_T)st.st_size;
            }
            std::vector<float> qbuf((size_t)dim);
            RECID *recid;
            int column_dtype = FTN_IS_VEC(t) ? t : vp.dtype;
            TXrewinddbtbl(dbtbl);
            while ((recid = getdbtblrow(dbtbl)) != RECIDPN &&
                   TXrecidvalid(recid)) {
                /* Snapshot recid->off once (process-static; SQL stomps it). */
                EPI_OFF_T off = TXgetoff(recid);
                size_t n_elems = 0;
                void *raw = getfld(fld, &n_elems);
                if (!raw || n_elems == 0) continue;
                size_t cells = n_elems;
                if (t == FTN_BYTE) {
                    size_t elsz = vec_dtype_elsz(column_dtype);
                    if (elsz == 0 || (n_elems % elsz) != 0) continue;
                    cells = n_elems / elsz;
                }
                if ((int)cells != dim) continue;
                if (vec_convert_to_f32(column_dtype, raw, cells, dim,
                                       vp.quant_scale, vp.quant_zp,
                                       qbuf.data()) != 0) continue;
                faiss::idx_t fid = (faiss::idx_t)(uint64_t)off;
                idx->add_with_ids(1, qbuf.data(), &fid);
                abs_arr[abs_n++] = (int64_t)fid;
                if (meter3)
                    METER_UPDATEDONE(meter3, (EPI_HUGEINT)off);
                if (encode3_total_bytes > 0)
                    TXsysupdateProgress((TXsysupdateSink *)ddic->sysupdSink,
                        (double)off / (double)encode3_total_bytes);
            }
            if (meter3) { meter_end(meter3); closemeter(meter3); }
        }

        /* Save head to Tnnnn_H.idxpq.  As in OPTIMIZE, override the
         * recorded invlist filename to the live path so SAME_DIR
         * resolution works after the atomic rename. */
        {
            auto *od = static_cast<faiss::OnDiskInvertedLists *>(idx->invlists);
            std::string saved_fname = od->filename;
            od->filename = h->invl_path;
            /* Bake auto-scaled nprobe before save (mirrors CREATE/OPTIMIZE). */
            idx->nprobe = auto_nprobe((size_t)idx->nlist);
            try {
                faiss::FileIOWriter w(temp_head);
                if (save_ivfpq_head(idx, &w, new_max) != 0) {
                    putmsg(MERR + UGE, fn, "save_ivfpq_head failed");
                    od->filename = saved_fname;
                    goto rebuild_err;
                }
            } catch (...) {
                od->filename = saved_fname;
                throw;
            }
            od->filename = saved_fname;
        }

        /* Empty Tnnnn aux btrees. */
        (void)TXvecBtreeUnlink(tempT);
        (void)TXvecBtreeUnlink(tempD);
        if (TXvecBtreeCreateEmpty(tempT) != 0 ||
            TXvecBtreeCreateEmpty(tempD) != 0) {
            putmsg(MERR + UGE, fn,
                "INDEX_VEC REBUILD: could not create empty Tnnnn aux btrees");
            goto rebuild_err;
        }

        rc_overall = 0;
        goto rebuild_done;
    } catch (const faiss::FaissException &e) {
        putmsg(MERR + UGE, fn, "FAISS error during rebuild: %s", e.what());
    } catch (const std::exception &e) {
        putmsg(MERR + UGE, fn, "rebuild error: %s", e.what());
    }

rebuild_err:
    delete idx;
    std::free(abs_arr);
    ::unlink(temp_head); ::unlink(temp_invl);
    std::free(temp_head); std::free(temp_invl);
    putmsg(MERR + UGE, fn,
        "INDEX_VEC REBUILD failed; live `%s' is unchanged.", indfile);
    return -1;

rebuild_done:
    delete idx;
    std::free(temp_head); std::free(temp_invl);

    if (abs_n > 0)
        std::qsort(abs_arr, abs_n, sizeof(int64_t), vec_int64_cmp_);
    *out_absorbed = abs_arr;
    *out_n_absorbed = abs_n;

    putmsg(MINFO, fn,
        "INDEX_VEC ivfpq REBUILD: re-encoded %zu rows into temp; "
        "max_recid_at_create=%lld",
        abs_n, (long long)new_max);
    (void)rc_overall;
    return 0;
}

} /* anonymous namespace */

/* ====================================================================
 * Backend table — exported.
 * ==================================================================== */

extern "C" const TXvecBackend TXvecIvfpqBackend = {
    /* .create   = */ ivfpq_create_impl,
    /* .open     = */ ivfpq_open_impl,
    /* .close    = */ ivfpq_close_impl,
    /* .search   = */ ivfpq_search_impl,
    /* .add_row  = */ ivfpq_add_row_impl,
    /* .del_row  = */ ivfpq_del_row_impl,
    /* .flush    = */ ivfpq_flush_impl,
    /* .drop_aux = */ ivfpq_drop_aux_impl,
    /* .is_stale = */ ivfpq_is_stale_impl,
    /* .optimize = */ ivfpq_optimize_impl,
    /* .rebuild  = */ ivfpq_rebuild_impl,
};
