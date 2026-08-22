/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
 * You may use, distribute this code under the
 * terms of the Rampart Source Available License.
 * see rsal.txt for details
 */
#include "txcoreconfig.h"
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include <ctype.h>
#include <float.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dbquery.h"
#include "texint.h"
#include "vecvalue.h"   /* self-describing chunked-vector values */
#include "texisapi.h"
#include "vecindex.h"
#include "cgi.h"
#include "rampart.h"
#include "rp_zip.h"
#include "api3.h"
#include "../globals/csv_parser.h"
#include "event.h"

static pthread_mutex_t tx_handle_lock;

static int defnoise=1, defsuffix=1, defsuffixeq=1, defprefix=1;

#define RESMAX_DEFAULT 10 /* default number of sql rows returned for select statements if max is not set */

#define QUERY_STRUCT struct rp_query_struct

#define QS_ERROR_DB 1
#define QS_ERROR_PARAM 2
#define QS_SUCCESS 0

#define QFLAG_GETCOUNTS 1 /* whether to include metamorph counts in return */
#define QFLAG_RAWVEC 2    /* whether to return raw vectors rather than new rampart.vector() */

QUERY_STRUCT
{
    const char *sql;    /* the sql statement (allocated by duk and on its stack) */
    duk_idx_t arr_idx;  /* location of array of parameters in ctx, or -1 */
    duk_idx_t obj_idx;  /* location of ?named parameters if not using array above, or -1 */
    duk_idx_t str_idx;
    duk_idx_t arg_idx;  /* location of extra argument for callback */
    duk_idx_t callback; /* location of callback in ctx, or -1 */
    int skip;           /* number of results to skip */
    int64_t max;        /* maximum number of results to return */
    signed char rettype;/* 0 for return object with key as column names,
                           1 for array
                           2 for novars                                           */
    char err;
    uint8_t flags;         /* right now, just QFLAG_GETCOUNTS and QFLAG_RAWVEC */
};

static duk_ret_t rp_sql_close(duk_context *ctx);

extern int TXunneededRexEscapeWarning;
int texis_resetparams(TEXIS *tx);
int texis_cancel(TEXIS *tx);
/*
   info for shared memory
   which is used BOTH for thread and fork versions
   in order to keep it simple
*/
#define FORKMAPSIZE 1048576
//#define FORKMAPSIZE 2 -- for testing - note that for sql_set in fork, 2048 bytes might be used for errors
#define FMINFO struct sql_map_info_s
FMINFO
{
    void *mem;
    void *pos;
};

#define mmap_used ((size_t)(mapinfo->pos - mapinfo->mem))
#define mmap_rem (finfo->mapsize - mmap_used)

/* Default shared-segment reservation.
 *
 * The reservation itself is virtual: pages are allocated on FIRST TOUCH,
 * verified on Linux and on macOS 11 and 15 (a 4GB reservation costs 0 kB
 * RSS until written).  So reserving generously is free -- right up until
 * something touches it.
 *
 * That matters because on macOS and FreeBSD touched pages can NEVER be
 * returned: there is no MADV_REMOVE or hole-punch for POSIX shm, and
 * ftruncate cannot shrink such an object.  The reservation is therefore
 * also the worst-case PERMANENT footprint of a long-lived helper, so it
 * is deliberately modest rather than generous.  8MB keeps a Doc plus a
 * k*dim*4 Vec row on the zero-copy path; 7 helpers on an 8-core box is
 * 56MB worst case.  32-bit keeps the classic 1MB: address space is the
 * scarce resource there and every thread holds its own segment.
 *
 * MUST be a multiple of 8: cwrite_aligned() aligns within the map while
 * the receiver aligns within the reassembled stream, so the two agree
 * only if every chunk boundary is itself aligned.  setMapSize() rounds
 * to whole pages, which guarantees it. */
#if defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ >= 8
#define RP_FORKMAP_DEFAULT (8u * 1024u * 1024u)
#else
#define RP_FORKMAP_DEFAULT (1024u * 1024u)
#endif

/* Smallest segment Sql.setMapSize() will accept: one page.  Small sizes
 * are genuinely useful -- they force nearly every message to chunk, so
 * the multi-chunk paths get exercised by the ordinary test suites -- but
 * the size must stay a multiple of 8 for the alignment reason above, and
 * a page is the natural floor that guarantees it. */
#define RP_FORKMAP_MIN 4096u
#define mmap_reset do{ mapinfo->pos = mapinfo->mem;} while (0)

/* Sanity ceiling for counts that arrive over the pipe (vector dim, chunk
 * count).  Payloads themselves are unbounded -- they chunk -- so this is
 * not a transfer limit: it exists so a desynced pipe delivering garbage
 * cannot make us compute an absurd size or wrap a multiply on 32-bit. */
#define RP_MSG_SANE_FLOATS (64u * 1024u * 1024u)

int thisfork =0; //set after forking.

/* ============================================================
 * Embed integration with rampart-llamacpp.
 *
 * Symbols are looked up lazily on the first sql.set({llamaEmbed:...}).
 * `require("rampart-llamacpp")` triggers a dlopen with RTLD_GLOBAL
 * (core/module.c), so dlsym(RTLD_DEFAULT, "rp_embed_load") finds them.
 *
 * One model per process; the rp_embed_load cache is keyed by path.
 *
 * In main and in any RPTHR_FLAG_THR_SAFE thread we register a callback
 * that calls rp_embed_text directly.  In a helper child, the callback
 * writes 'B' + text up the pipe and reads back the vec (Step 4).
 * ============================================================ */

static void  *(*g_rp_embed_load)(const char *, char *, size_t)            = NULL;
static size_t (*g_rp_embed_text)(void *, const char *, size_t, float **)  = NULL;
static int    (*g_rp_embed_dim)(void *)                                   = NULL;
static void   (*g_rp_embed_set_per_thread)(int)                           = NULL;
static void  *g_embed_handle = NULL;   /* one per process; first sql.set wins */

/* Chunk-level doc embed + chunk spans (2026-07 langtools rework).
 * Span struct is layout-identical across both engines. */
typedef struct { size_t start, end, n_tokens; } rp_embed_span_t;
static size_t (*g_rp_embed_doc)(void *, const char *, size_t,
                                const char *, size_t,
                                float **, size_t *, float **, float *,
                                rp_embed_span_t **)                       = NULL;
static size_t (*g_rp_embed_spans)(void *, const char *, size_t,
                                  rp_embed_span_t **)                     = NULL;
static void   (*g_rp_embed_set_cache_cap)(void *, size_t)                = NULL;

/* --- ONNX embed backend (dlsym'd from rampart-onnx.so on first use) ------
 *
 * Same shape as the llamacpp integration above: `rp_onnx_embed_*` C exports
 * are looked up lazily on the first sql.set({onnxEmbed:...}).  This struct
 * MUST match rampart-langtools/rampart-onnx.c's declaration field for
 * field; the RP_ONNX_EMBED_ABI sentinel makes accidental drift a loud
 * load-time error instead of silent corruption (a shorter struct here
 * feeds the langtools side stack garbage -- and since the opts are the
 * model-handle dedup key, a non-deterministic key that splits the
 * per-model doc cache).  ANY change to the struct must bump the define
 * in BOTH files. */
#define RP_ONNX_EMBED_ABI 4   /* v4: + sentence_split */
typedef struct {
    int         abi_version;      /* must be RP_ONNX_EMBED_ABI */
    const char *tokenizer_path;   /* file-mode only: tokenizer path (a
                                   * *vocab.txt or a dir with tokenizer.json);
                                   * NULL/empty in directory mode, which
                                   * self-discovers the tokenizer */
    int         bos_id;
    int         eos_id;
    int         id_offset;
    int         pad_id;
    int         max_tokens;
    int         pooling;          /* 0 = auto, 1 = mean, 2 = cls */
    int         normalize;
    const char *query_prefix;
    const char *passage_prefix;
    int         max_chunk_batch;  /* 0 = default 64 */
    int         split_mode;       /* 0 auto / 1 window / 2 para */
    int         min_split_tokens; /* 0 = default 32, -1 = off */
    int         pack_paragraphs;  /* 1 = pack paragraphs to the window */
    int         sentence_split;   /* 1 = sentence-pack oversized pieces */
} rp_onnx_embed_opts;

static void  *(*g_rp_onnx_embed_load)(const char *,
                                      const rp_onnx_embed_opts *,
                                      char *, size_t)                    = NULL;
static size_t (*g_rp_onnx_embed_text)(void *, const char *, size_t,
                                      float **)                          = NULL;
static int    (*g_rp_onnx_embed_dim)(void *)                             = NULL;
static void   (*g_rp_onnx_embed_release)(void *)                         = NULL;
static size_t (*g_rp_onnx_embed_doc)(void *, const char *, size_t,
                                     const char *, size_t,
                                     float **, size_t *, float **, float *,
                                     rp_embed_span_t **)                 = NULL;
static size_t (*g_rp_onnx_embed_spans)(void *, const char *, size_t,
                                       rp_embed_span_t **)               = NULL;
static void   (*g_rp_onnx_embed_set_cache_cap)(void *, size_t)          = NULL;

/* --- rampart-clip (CLIP: image + text encoders in ONE shared space) ---
 * Same C-ABI shape as the onnx pointers above (allocate *out_vec, return
 * the dim, 0 on failure).  There is no _doc / _spans pair: CLIP has no
 * chunking -- an image is one vector and its text tower is ~77 tokens --
 * so chunkembed()/abstract-with-Vec are rejected for a clip connection
 * rather than silently doing something else. */
static int    (*g_rp_clip_iface_v1)(void)                                = NULL;
static void  *(*g_rp_clip_embed_load)(const char *, char *, size_t)      = NULL;
static size_t (*g_rp_clip_embed_text)(void *, const char *, size_t,
                                      float **)                          = NULL;
static size_t (*g_rp_clip_embed_image_path)(void *, const char *,
                                            float **)                    = NULL;
/* image BYTES -- used only by the parameter substitution below, which
 * embeds in the parent where the bytes already are (they never cross to
 * the helper and back). */
static size_t (*g_rp_clip_embed_image)(void *, const void *, size_t,
                                       float **)                         = NULL;
static int    (*g_rp_clip_embed_dim)(void *)                             = NULL;
static int    (*g_rp_clip_has_text)(void *)                              = NULL;
static int    (*g_rp_clip_has_vision)(void *)                            = NULL;
static void   (*g_rp_clip_embed_release)(void *)                         = NULL;

/* likevCache: requested doc-result cache capacity for the connection's
 * embed handle (−1 = not set this session -> leave the default of 10).
 * Applied to the active handle when both it and this value are known,
 * regardless of the order the sql.set keys are processed. */
__thread int g_doccache_cap_pending = -1;

/* Which backend the currently-executing query on this thread should
 * route embed() calls to.  Set from h->embed_engine per-exec, alongside
 * g_active_embed_handle. */
typedef enum {
    EMBED_ENGINE_NONE     = 0,
    EMBED_ENGINE_LLAMACPP = 1,
    EMBED_ENGINE_ONNX     = 2,
    EMBED_ENGINE_CLIP     = 3,
} embed_engine_t;

/* Name for error messages, so a failure says which engine refused. */
static const char *embed_engine_name(embed_engine_t e)
{
    switch (e) {
        case EMBED_ENGINE_LLAMACPP: return "llamacpp";
        case EMBED_ENGINE_ONNX:     return "onnx";
        case EMBED_ENGINE_CLIP:     return "clip";
        default:                    return "none";
    }
}

/* --- LRU embed cache (Step 5) ---------------------------------------
 *
 * Process-global, byte-exact key (no normalization).  Sized via
 * sql.set({llamaCache:N}).  N=0 disables.
 *
 * Threading: each parent worker thread has its own thread-local mmap
 * (`finfo->mapinfo->mem`), so concurrent writes to the mmap do NOT
 * contend.  Only the cache data structure itself is shared — guarded
 * by g_lru.mtx.  Critical section is sub-μs (lookup + memcpy of vec
 * into the thread-local mmap; the long-poles — model call, pipe
 * write — happen outside the lock). */

typedef struct embed_lru_node_s {
    char     *text;
    size_t    text_len;
    float    *vec;
    int       dim;
    void     *model;          /* which embed model produced vec (per-connection) */
    uint64_t  text_hash;
    /* hash chain — singly linked, pointer-to-pointer prev for O(1) unlink */
    struct embed_lru_node_s  *hash_next;
    struct embed_lru_node_s **hash_pprev;
    /* recency list — doubly linked */
    struct embed_lru_node_s  *lru_prev;
    struct embed_lru_node_s  *lru_next;
} embed_lru_node_t;

static struct {
    embed_lru_node_t **buckets;
    size_t             n_buckets;     /* power of 2 */
    embed_lru_node_t  *lru_head;      /* most recently used */
    embed_lru_node_t  *lru_tail;      /* eviction candidate */
    size_t             n_entries;
    size_t             capacity;      /* 0 → disabled */
    pthread_mutex_t    mtx;
    int                initialized;
} g_lru = {0};
static pthread_mutex_t g_lru_init_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Key folds the model pointer in with the text, so the same query string
 * embedded by two different models (e.g. MiniLM vs bge-m3 serving different
 * language DBs in one process) gets distinct cache entries. */
static uint64_t embed_lru_key(const void *model, const char *data, size_t len)
{
    uint64_t  h = 14695981039346656037ULL;  /* FNV offset basis */
    uintptr_t m = (uintptr_t)model;
    size_t    i;
    for (i = 0; i < sizeof m; i++)
        h = (h ^ (uint8_t)(m >> (i * 8))) * 1099511628211ULL;
    for (i = 0; i < len; i++)
        h = (h ^ (uint8_t)data[i]) * 1099511628211ULL;  /* FNV prime */
    return h;
}

/* must be called under g_lru.mtx */
static embed_lru_node_t *
embed_lru_find_locked(uint64_t h, const void *model, const char *text, size_t tlen)
{
    embed_lru_node_t *n = g_lru.buckets[h & (g_lru.n_buckets - 1)];
    while (n) {
        if (n->text_hash == h && n->model == model && n->text_len == tlen &&
            memcmp(n->text, text, tlen) == 0)
            return n;
        n = n->hash_next;
    }
    return NULL;
}

/* must be called under g_lru.mtx */
static void embed_lru_list_unlink_locked(embed_lru_node_t *n)
{
    if (n->lru_prev) n->lru_prev->lru_next = n->lru_next;
    else             g_lru.lru_head = n->lru_next;
    if (n->lru_next) n->lru_next->lru_prev = n->lru_prev;
    else             g_lru.lru_tail = n->lru_prev;
}

static void embed_lru_list_push_head_locked(embed_lru_node_t *n)
{
    n->lru_prev = NULL;
    n->lru_next = g_lru.lru_head;
    if (g_lru.lru_head) g_lru.lru_head->lru_prev = n;
    else                g_lru.lru_tail = n;
    g_lru.lru_head = n;
}

static void embed_lru_promote_locked(embed_lru_node_t *n)
{
    if (g_lru.lru_head == n) return;
    embed_lru_list_unlink_locked(n);
    embed_lru_list_push_head_locked(n);
}

static void embed_lru_evict_one_locked(void)
{
    embed_lru_node_t *e = g_lru.lru_tail;
    if (!e) return;
    embed_lru_list_unlink_locked(e);
    /* unlink from hash chain */
    if (e->hash_next) e->hash_next->hash_pprev = e->hash_pprev;
    *(e->hash_pprev) = e->hash_next;
    free(e->text);
    free(e->vec);
    free(e);
    g_lru.n_entries--;
}

/* must be called under g_lru.mtx; copies text + vec into a new node */
static void
embed_lru_put_locked(uint64_t h, const void *model, const char *text, size_t tlen,
                     const float *vec, int dim)
{
    /* Dedup: two threads might miss in parallel and both call put.
     * If already present, just promote and return. */
    embed_lru_node_t *existing = embed_lru_find_locked(h, model, text, tlen);
    if (existing) {
        embed_lru_promote_locked(existing);
        return;
    }
    if (g_lru.n_entries >= g_lru.capacity)
        embed_lru_evict_one_locked();

    embed_lru_node_t *n = (embed_lru_node_t *)calloc(1, sizeof *n);
    if (!n) return;
    n->text = (char *)malloc(tlen);
    if (!n->text) { free(n); return; }
    memcpy(n->text, text, tlen);
    n->text_len  = tlen;
    n->text_hash = h;
    n->dim       = dim;
    n->model     = (void *)model;
    n->vec       = (float *)malloc((size_t)dim * sizeof(float));
    if (!n->vec) { free(n->text); free(n); return; }
    memcpy(n->vec, vec, (size_t)dim * sizeof(float));

    size_t bk = h & (g_lru.n_buckets - 1);
    n->hash_next  = g_lru.buckets[bk];
    n->hash_pprev = &g_lru.buckets[bk];
    if (n->hash_next) n->hash_next->hash_pprev = &n->hash_next;
    g_lru.buckets[bk] = n;

    embed_lru_list_push_head_locked(n);
    g_lru.n_entries++;
}

/* Set capacity (creates structure on first call, no-op on resize for v1). */
static int embed_lru_set_capacity(size_t capacity)
{
    pthread_mutex_lock(&g_lru_init_mtx);
    if (g_lru.initialized) {
        /* For v1, ignore capacity changes after first set.  Future:
         * could grow/shrink in place under g_lru.mtx. */
        pthread_mutex_unlock(&g_lru_init_mtx);
        return 0;
    }
    if (capacity == 0) {
        g_lru.capacity = 0;  /* disabled */
        pthread_mutex_unlock(&g_lru_init_mtx);
        return 0;
    }
    size_t nb = 1;
    while (nb < capacity * 2) nb <<= 1;  /* power of 2, ~2× capacity */
    g_lru.buckets = (embed_lru_node_t **)calloc(nb, sizeof(embed_lru_node_t *));
    if (!g_lru.buckets) {
        pthread_mutex_unlock(&g_lru_init_mtx);
        return -1;
    }
    g_lru.n_buckets = nb;
    g_lru.capacity  = capacity;
    g_lru.n_entries = 0;
    g_lru.lru_head  = g_lru.lru_tail = NULL;
    pthread_mutex_init(&g_lru.mtx, NULL);
    g_lru.initialized = 1;
    pthread_mutex_unlock(&g_lru_init_mtx);
    return 0;
}

/* Forward decls so fork_exec / h_set / sql_set can call these before
 * their definitions later in the file.  DB_HANDLE-using decls move
 * down after the struct definition. */
static void   parent_service_embed(void);
static void   parent_service_embed_doc(void);
static void   parent_service_chunk_spans(void);
static size_t main_embed_doc_callback(void *ud, const char *text,
                                      size_t tlen, const char *prefix,
                                      size_t plen, float **out_vecs,
                                      size_t *out_k, float **out_avg,
                                      float *out_coh,
                                      TXchunkSpan **out_spans);
static size_t main_chunk_spans_callback(void *ud, const char *text,
                                        size_t tlen, TXchunkSpan **out_spans);
static void   setup_llamacpp_callback(void);
static int    setup_llamacpp_main(duk_context *ctx, const char *path,
                                  char *errbuf, size_t errbuflen);
static int    peek_llamaembed_setting(duk_context *ctx, const char **path);
static int    peek_llamacache_setting(duk_context *ctx, int *cap);
static int    peek_likevcache_setting(duk_context *ctx, int *cap);
static int    peek_llamaembed_perthread_setting(duk_context *ctx, int *on);
static int    peek_onnxembed_setting(duk_context *ctx,
                                     const char **model_path_out,
                                     rp_onnx_embed_opts *out);
static int    setup_onnx_main(duk_context *ctx, const char *model_path,
                              const rp_onnx_embed_opts *opts,
                              char *errbuf, size_t errbuflen);
static int    peek_clipembed_setting(duk_context *ctx,
                                     const char **model_path_out);
static int    setup_clip_main(duk_context *ctx, const char *model_path,
                              char *errbuf, size_t errbuflen);
static int    embed_lru_set_capacity(size_t capacity);
static int    fork_drain_embed_callbacks(void);

/* shared mem for logging errors */
//char **errmap=NULL;

/* one SFI struct for each thread in rampart-threads.
   Some threads can run without a fork (main thread and one thread in rampart-server)
   while the rest will fork in order to avoid locking
   around texis_* calls.

   If not in the server, or if not threading, then
   all operations will be non forking.
*/

/* Reassembly buffer for a payload too large for the shared map (see
 * get_chunks()). */
typedef struct { char *b; size_t sz; } XFERBUF;

#define SFI struct sql_fork_info_s
SFI
{
    int reader;         // pipe to read from, in parent or child
    int writer;         // pipe to write to, in parent or child
    pid_t childpid;     // process id of the child if in parent (return from fork)
    FMINFO *mapinfo;    // the shared mmap for reading and writing in both parent and child
    char *errmap;       // the shared mmap for errors
    int mapfd;          // file descriptor of mapinfo->map mmem
    int errfd;          // file descriptor of error map mmem
    void *aux;          // if data is larger than mapsize, we need to copy it in chunks into here
    void *auxpos;
    size_t auxsz;
    FLDLST *fl;         // modified FLDLST for parent pointing to areas in mapinfo
    int mapwrapped;     // set when a message chunked, i.e. touched the WHOLE
                        // segment: the trigger for handing pages back
    size_t mapsize;     // ACTUAL bytes of mapinfo->mem for THIS helper.  Not a
                        // constant: the reservation is best-effort (it can fall
                        // back) and Sql.setMapSize() can change it for helpers
                        // launched later, so helpers may differ.  The child
                        // learns it by fstat()ing the inherited fd.
};

// info for thread/fork pairing as a thread local
__thread SFI *finfo = NULL;

/* Requested shared-segment size for helpers launched from here on.
 * PROCESS-WIDE, deliberately not __thread: Sql.setMapSize() is called
 * once (typically in a server's conf, on the main thread) and must apply
 * to worker threads that fork their helpers later.  Each helper records
 * what it actually got in finfo->mapsize. */
static size_t g_forkmap_size = RP_FORKMAP_DEFAULT;

// address from duk_get_heapptr of the last sql to have its settings applied
__thread void *last_sql_set = NULL;

/* Per-connection embed model support.
 *  g_last_loaded_embed_handle: the model handle just loaded by setup_llamacpp_main
 *      on THIS thread; captured by h_set() onto the connection's DB_HANDLE.
 *      Thread-local so concurrent sql.set({llamaEmbed}) on different threads with
 *      different models don't race (the old process-global g_embed_handle did).
 *  g_active_embed_handle: the model for the query currently executing on this
 *      thread; set per-exec from the handle, read by main_embed_callback (the
 *      in-process embed() path) and parent_service_embed (the helper-parent path).
 *      This is what makes embed() resolve per SQL connection instead of "first
 *      sql.set wins". */
__thread void *g_last_loaded_embed_handle = NULL;
__thread void *g_active_embed_handle      = NULL;

/* Which engine owns g_active_embed_handle for this query.  Set alongside
 * g_active_embed_handle in rp_sql_exec_query so main_embed_callback and
 * parent_service_embed know whether to call rp_embed_text (llamacpp) or
 * rp_onnx_embed_text (onnx). */
__thread embed_engine_t g_active_embed_engine = EMBED_ENGINE_NONE;

/* Thread-local mirror of the just-loaded onnx model, same role as
 * g_last_loaded_embed_handle for llamacpp: h_set() reads this to
 * attach the model to THIS connection's DB_HANDLE. */
__thread void *g_last_loaded_onnx_handle = NULL;

/* Same, for a CLIP model bound by setup_clip_main. */
__thread void *g_last_loaded_clip_handle = NULL;

/* --- embed failure latch ---------------------------------------------
 * An embed() / chunkembed() that FAILS makes texis substitute a NULL
 * field and carry on, so a bare SELECT would hand back a null vector
 * with only sql.errMsg to say why.  A long ingest run driven by a script
 * that never reads errMsg would silently produce vector-less rows.  So
 * the callbacks latch a real failure here, and rp_sql_exec_query turns
 * it into a throw -- for exec() only; query() keeps its documented
 * never-throw behaviour.
 *
 * IMPORTANT: only set for a GENUINE failure -- an empty input legitimately
 * yields a null vector (chunkembed('') is null by design, and the wiki
 * builders rely on it to keep empty articles searchable by keyword), so
 * callers must not latch when there was nothing to embed. */
__thread int  g_embed_failed = 0;
__thread char g_embed_failmsg[256] = {0};

static void embed_fail_latch(const char *fmt, ...)
{
    va_list ap;
    if (g_embed_failed) return;          /* keep the FIRST failure */
    g_embed_failed = 1;
    va_start(ap, fmt);
    vsnprintf(g_embed_failmsg, sizeof g_embed_failmsg, fmt, ap);
    va_end(ap);
}

/* --- Retrieval prompt prefixes (asymmetric embed models) --------------
 *
 * Malloc'd strings resolved when an embed model is bound to a connection
 * (sidecar file next to the model, or explicit sql.set keys) and owned by
 * the DB_HANDLE.  The g_last_loaded_* trio is the same handoff pattern as
 * g_last_loaded_embed_handle: filled by resolve_embed_prompts() on this
 * thread, transferred to the handle by attach_prompts_to_handle().  The
 * g_active_* trio is the per-exec mirror read by embed_compose_text()
 * (borrowed pointers into the executing connection's handle; valid for
 * the duration of the exec, like g_active_embed_handle). */
__thread char *g_last_loaded_prompt_query     = NULL;
__thread char *g_last_loaded_prompt_document  = NULL;
__thread char *g_last_loaded_prompt_doc_title = NULL;
__thread const char *g_active_prompt_query     = NULL;
__thread const char *g_active_prompt_document  = NULL;
__thread const char *g_active_prompt_doc_title = NULL;

/* Compose the active model's DOCUMENT prefix, folding in an optional
 * per-document title:
 *   - doc-title template + title:  template with "{title}" replaced
 *   - document prompt (+ title):   prompt, then the title
 *   - no prompts, title only:      the title (nl_after_title callers only)
 * nl_after_title: append '\n' after an appended title -- used when the
 * result is string-prepended to the text (embed()); the chunkembed path
 * passes 0 because the engine token-injects the prefix per window and
 * needs no separator (title-only there returns NULL = pass through
 * unchanged).  Returns NULL when nothing applies (caller keeps its
 * original prefix); else a malloc'd string, *outlen = length. */
static char *embed_compose_doc_prefix(const char *title, size_t title_len,
                                      int nl_after_title, size_t *outlen)
{
    const char *tp = g_active_prompt_doc_title;
    const char *dp = g_active_prompt_document;
    char *out = NULL;
    size_t n = 0;

    if (title && title_len == 0) title = NULL;
    if (title && tp) {
        const char *slot = strstr(tp, "{title}");
        size_t tplen = strlen(tp);
        if (slot) {
            size_t pre = (size_t)(slot - tp), post = tplen - pre - 7;
            n = tplen - 7 + title_len;
            out = malloc(n + 1);
            if (!out) return NULL;
            memcpy(out, tp, pre);
            memcpy(out + pre, title, title_len);
            memcpy(out + pre + title_len, slot + 7, post);
        } else {           /* slotless template: use as prefix, title after */
            n = tplen + title_len + (nl_after_title ? 1 : 0);
            out = malloc(n + 1);
            if (!out) return NULL;
            memcpy(out, tp, tplen);
            memcpy(out + tplen, title, title_len);
            if (nl_after_title) out[tplen + title_len] = '\n';
        }
        out[n] = '\0';
        if (outlen) *outlen = n;
        return out;
    }
    if (dp) {
        size_t dplen = strlen(dp);
        n = dplen + (title ? title_len : 0) +
            ((title && nl_after_title) ? 1 : 0);
        out = malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, dp, dplen);
        if (title) {
            memcpy(out + dplen, title, title_len);
            if (nl_after_title) out[dplen + title_len] = '\n';
        }
        out[n] = '\0';
        if (outlen) *outlen = n;
        return out;
    }
    if (title && nl_after_title) {
        out = malloc(title_len + 2);
        if (!out) return NULL;
        memcpy(out, title, title_len);
        out[title_len] = '\n';
        out[title_len + 1] = '\0';
        if (outlen) *outlen = title_len + 1;
        return out;
    }
    return NULL;
}

/* Compose the active model's retrieval prompt around `text` per the
 * embed kind (TXEMBED_*).  Returns NULL when no composition applies
 * (caller embeds text verbatim -- the no-prompts fast path); else a
 * malloc'd NUL-terminated buffer, *outlen = length.  Composition
 * happens HERE, before any cache, so every cache along the way
 * (parent LRU, engine text/doc caches) keys on the exact bytes the
 * model saw. */
static char *embed_compose_text(int kind, const char *title, size_t title_len,
                                const char *text, size_t tlen, size_t *outlen)
{
    const char *pfxp = NULL;
    char *pfx = NULL;      /* malloc'd document prefix, when composed */
    size_t pfxlen = 0;
    char *out;

    switch (kind) {
    case TXEMBED_QUERY:
        pfxp = g_active_prompt_query;
        if (!pfxp) return NULL;
        pfxlen = strlen(pfxp);
        break;
    case TXEMBED_DOCUMENT:
        pfx = embed_compose_doc_prefix(title, title_len, 1, &pfxlen);
        if (!pfx) return NULL;
        pfxp = pfx;
        break;
    default:               /* TXEMBED_RAW */
        return NULL;
    }
    out = malloc(pfxlen + tlen + 1);
    if (!out) { free(pfx); return NULL; }
    memcpy(out, pfxp, pfxlen);
    memcpy(out + pfxlen, text, tlen);
    out[pfxlen + tlen] = '\0';
    free(pfx);
    if (outlen) *outlen = pfxlen + tlen;
    return out;
}

// some string functions don't fork.  We need an error map for them
/* errmap0 removed: per-thread capture buffer rp_errmap (see
 * rp_msg_init()) replaced the single process-global buffer. */

extern int RP_TX_isforked;

// lock handle list while operating from multiple threads
#define HLOCK RP_PTLOCK(&tx_handle_lock);
#define HUNLOCK RP_PTUNLOCK(&tx_handle_lock);

#define DB_HANDLE struct db_handle_s_list
DB_HANDLE
{
    TEXIS *tx;                  // a texis handle, NULL if forking
    char *db;                   // the db path.
    char *user;                 // db user
    char *pass;                 // db pass
    DB_HANDLE *next;            // linked list
    DB_HANDLE *prev;            // doublylinked list
    uint16_t forknum;           // convenience. Same as the threadnum from rampart-threads. So forknum == threadnum
    void *embed_handle;         // per-connection embed model (rp_embed_load handle or rp_onnx_embed_load handle), or NULL
    embed_engine_t embed_engine;// which backend owns embed_handle; 0 (NONE) if none
    /* Retrieval prompt strings for embed_handle's model (malloc'd, owned by
     * the handle; NULL = none).  From the model's .prompts.json sidecar /
     * config_sentence_transformers.json, or explicit sql.set keys.  Applied
     * around query/document text before the model runs (see
     * embed_compose_text). */
    char *embed_prompt_query;      // query prompt (plain prefix)
    char *embed_prompt_document;   // document prompt, no-title form
    char *embed_prompt_doc_title;  // document template with a {title} slot
    char flags;                 // bit 0 - if the texis handle is in the corresponding fork
                                // bit 1 - if handle is available (not in use);
                                // bit 2 - embed enabled (sql.set({llamaEmbed:...} OR {onnxEmbed:...}))
                                // bit 3 - embed callback registered in helper child
};

// if the handle corresponds to a *tx handle in a forked child
#define DB_FLAG_FORK 1
// if the handle is currently in use (on the main db_handle_head list)
// if not in use, it is on the thread local db_handle_available_head list.
#define DB_FLAG_IN_USE 2
// embed enabled (sql.set({llamaEmbed:...}) succeeded; rp_embed_handle valid)
#define DB_FLAG_EMBED_ENABLED 4
// embed callback registered in this connection's helper child
#define DB_FLAG_EMBED_CHILD_REGISTERED 8

/* Forward decl for the V-registration helper; needs DB_HANDLE. */
static int fork_maybe_register_embed(DB_HANDLE *h);

#define DB_HANDLE_SET(h,flag)   do { (h)->flags |=  (flag);  } while (0)
#define DB_HANDLE_CLEAR(h,flag) do { (h)->flags &= ~(flag); } while (0)
#define DB_HANDLE_IS(h,flag) ( (h)->flags & flag )


__thread DB_HANDLE
    *db_handle_available_head=NULL,
    *db_handle_available_tail=NULL;

// max handles in available list, per thread/fork
// if more, old handles will be closed and freed.
#define MAX_HANDLES 16
__thread int nhandles=0;

// list of handles in use
DB_HANDLE *db_handle_head=NULL;

static int h_close(DB_HANDLE *h);

static DB_HANDLE *new_handle(const char *db, const char *user, const char *pass)
{
    RPTHR *thr = get_current_thread();
    DB_HANDLE *h = NULL;

    REMALLOC(h, sizeof(DB_HANDLE));

    if(RPTHR_TEST(thr, RPTHR_FLAG_THR_SAFE))
        h->flags=0; // not using forked process, not in_use
    else
        h->flags=1; // using forked process, not in_use

    h->db=strdup(db);
    h->tx=NULL;
    h->embed_handle=NULL;
    h->embed_engine=EMBED_ENGINE_NONE;
    h->embed_prompt_query=NULL;
    h->embed_prompt_document=NULL;
    h->embed_prompt_doc_title=NULL;
    h->forknum = (uint16_t)get_thread_num();
    h->next = h->prev = NULL;
    h->user=strdup(user);
    h->pass=strdup(pass);

    return h;
}

// add handle to main list
static void add_handle(DB_HANDLE *h)
{
    HLOCK

    if(db_handle_head != NULL)
        db_handle_head->prev=h;

    h->next=db_handle_head;
    db_handle_head=h;
    h->prev=NULL;
    DB_HANDLE_SET(h, DB_FLAG_IN_USE);

    HUNLOCK
}

// remove handle from whichever list it is on.
// locking must be done if on main list
static void remove_handle(DB_HANDLE *h)
{
    DB_HANDLE *n=h->next, *p=h->prev;

    if(p) p->next = n;  //if h=available_tail, n==NULL
    if(n) n->prev = p;  //if h==head, p==NULL

    if(h==db_handle_head)
         db_handle_head=n; // n->prev set to null above
    else if(h==db_handle_available_head)
         db_handle_available_head=n;  //n->prev set to null above

    if(h==db_handle_available_tail)
        db_handle_available_tail=p;  //p->next set to null above

    //if it was in the available list, decrement nhandles
    if(!DB_HANDLE_IS(h, DB_FLAG_IN_USE))
        nhandles--;

    h->prev = h->next = NULL;
}

//insert at beginning of available list
static void mark_handle_available(DB_HANDLE *h)
{
    if(!h)
        return;

    // remove from main list
    HLOCK
    remove_handle(h);
    HUNLOCK

    // add to beginning of available list
    if(db_handle_available_head != NULL)
        db_handle_available_head->prev=h; // set old head->prev to new head
    else
        db_handle_available_tail=h;  //if null, both beginning and end of list

    h->next=db_handle_available_head; //set new head->next to old head
    db_handle_available_head=h; // h is now new head
    h->prev=NULL; // head always has prev==NULL
    DB_HANDLE_CLEAR(h, DB_FLAG_IN_USE);

    // limit list size:
    nhandles++;

    /* sanity check:
    int nh=0;
    DB_HANDLE *x=db_handle_available_head;
    while(x) {
        nh++;
        x=x->next;
    }
    printf("NHANDLES = %d vs %d\n",nhandles, nh);
    */

    while (nhandles > MAX_HANDLES)
    {
        //assuming a max > 1. i.e. there will always be a tail->prev
        h=db_handle_available_tail;
        db_handle_available_tail=h->prev;
        db_handle_available_tail->next=NULL;

        h_close(h);
        nhandles--;
    }
}

//remove from available list
static void mark_handle_in_use(DB_HANDLE *h)
{
    // remove from available list, no locking necessary (thread local)
    remove_handle(h);

    // add to main list
    add_handle(h); //this does DB_HANDLE_SET(h, DB_FLAG_IN_USE)
}

#define DBH_MARK_AVAILABLE 0
#define DBH_MARK_IN_USE    1

// find unused handle already open with given db
static DB_HANDLE *find_available_handle(const char *db, const char *user, const char *pass, int in_use)
{
    DB_HANDLE *h=db_handle_available_head;

    /* h->tx == NULL means the helper that owned it died (see
     * invalidate_thread_handles): the pointer belonged to that process,
     * so this handle can never be used again -- skip it and let the
     * caller open a fresh one against the replacement helper.  Handing
     * it back would send a stale TEXIS* to the new child, which
     * dereferences it and dies with SIGSEGV. */
    while(h && ( !h->tx || strcmp(h->db, db)!=0 || strcmp(h->user, user)!=0 || strcmp(h->pass, pass)!=0) )
        h=h->next;

    if(h && in_use)
        mark_handle_in_use(h);

    return h;
}


static DB_HANDLE * free_handle(DB_HANDLE *h)
{
    remove_handle(h);
    if(h->db)
        free(h->db);
    if(h->user)
        free(h->user);
    if(h->pass)
        free(h->pass);
    free(h->embed_prompt_query);
    free(h->embed_prompt_document);
    free(h->embed_prompt_doc_title);
    free(h);
    return NULL;
}

// for yosemite:
#ifdef __APPLE__
#include <Availability.h>
#  if __MAC_OS_X_VERSION_MIN_REQUIRED < 101300
#    include "fmemopen.h"
#    include "fmemopen.c"
#  endif
#endif


static int sql_set(duk_context *ctx, TEXIS *tx, char *errbuf);

#define TXLOCK /* currently unused */
#define TXUNLOCK /* currently unused */


int db_is_init = 0;
int tx_rp_cancelled = 0;
/* Dead code: tx_rp_cancelled is set by die_nicely() (SIGUSR2 handler),
   but nothing in rampart currently sends SIGUSR2 to sql_helper processes.
   If revived, guard with thisfork to avoid exit() in the parent process:
#define EXIT_IF_CANCELLED \
    if (tx_rp_cancelled && thisfork)  \
        exit(0);
*/

#define EXIT_IF_CANCELLED

#ifdef DEBUG_TX_CALLS

#define xprintf(...)                 \
    printf("(%d): ", (int)getpid()); \
    printf(__VA_ARGS__);

#else

#define xprintf(...) /* niente */

#endif

#define TEXIS_OPEN(tdb) ({                        \
    xprintf("Open\n");                            \
    TXLOCK                                        \
    TEXIS *rtx = texis_open((char *)(tdb), "PUBLIC", ""); \
    TXUNLOCK                                      \
    EXIT_IF_CANCELLED                             \
    rtx;                                          \
})

#define TEXIS_CLOSE(rtx) ({     \
    xprintf("Close\n");         \
    TXLOCK                      \
    (rtx) = texis_close((rtx)); \
    TXUNLOCK                    \
    EXIT_IF_CANCELLED           \
    rtx;                        \
})

#define TEXIS_PREP(a, b) ({           \
    xprintf("Prep\n");                \
    TXLOCK                            \
    int r = texis_prepare((a), (b));  \
    TXUNLOCK                          \
    EXIT_IF_CANCELLED                 \
    r;                                \
})

#define TEXIS_EXEC(a) ({        \
    xprintf("Exec\n");          \
    TXLOCK                      \
    int r = texis_execute((a)); \
    TXUNLOCK                    \
    EXIT_IF_CANCELLED           \
    r;                          \
})

#define TEXIS_FETCH(a, b) ({           \
    xprintf("Fetch\n");                \
    TXLOCK                             \
    FLDLST *r = texis_fetch((a), (b)); \
    TXUNLOCK                           \
    EXIT_IF_CANCELLED                  \
    r;                                 \
})

#define TEXIS_SKIP(a, b) ({               \
    xprintf("skip\n");                    \
    TXLOCK                                \
    int r = texis_flush_scroll((a), (b)); \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_GETCOUNTINFO(a, b) ({       \
    xprintf("getCountInfo\n");            \
    TXLOCK                                \
    int r = texis_getCountInfo((a), (b)); \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_FLUSH(a) ({                 \
    xprintf("skip\n");                    \
    TXLOCK                                \
    int r = texis_flush((a));             \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_RESETPARAMS(a) ({           \
    xprintf("resetparams\n");             \
    TXLOCK                                \
    int r = texis_resetparams((a));       \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_PARAM(a, b, c, d, e, f) ({               \
    xprintf("Param\n");                                \
    TXLOCK                                             \
    int r = texis_param((a), (b), (c), (d), (e), (f)); \
    TXUNLOCK                                           \
    EXIT_IF_CANCELLED                                  \
    r;                                                 \
})

// bug fix: added h = h->next and NULL check to prevent infinite loop in signal handler - 2026-02-27
static void die_nicely(int sig)
{
    DB_HANDLE *h = db_handle_head;
    while(h)
    {
        if(h->tx)
            texis_cancel(h->tx);

        h = h->next;
    }
    tx_rp_cancelled = 1;
}


pid_t parent_pid = 0;

#define msgbufsz 4096

/* Per-thread texis message capture.  mmsgfh (texis mmsg.c) is
 * thread-local, so each thread that runs texis in-process gets its own
 * capture buffer here: rp_msg_init() allocates the buffer and points
 * this thread's mmsgfh at it via fmemopen.  finfo (also __thread)
 * carries the same pointer in finfo->errmap, so every existing error-
 * reading path works per-thread.  Without this, a thread that never
 * called rp_msg_init() would have mmsgfh==NULL and texis would fall
 * back to stderr.  Helper children overwrite their (single) thread's
 * mmsgfh with an fmemopen over the shared memfd map (do_child_loop),
 * exactly as before -- the parent reads that map as plain memory, so
 * the parent/child error protocol is unaffected.  One buffer per
 * thread, retained for the thread's life. */
__thread char *rp_errmap = NULL;

char *rp_msg_init(void)
{
    if (rp_errmap == NULL)
    {
        REMALLOC(rp_errmap, msgbufsz);
        rp_errmap[0] = '\0';
        mmsgfh = fmemopen(rp_errmap, msgbufsz, "w+");
    }
    return rp_errmap;
}

static void rp_msg_finalize(void);   /* defined below, used by the macros */

/* rp_msg_finalize() first in each of these: the Error text is taken
 * straight from finfo->errmap, and must be the same bytes rp_log_error()
 * puts in this.errMsg. */
#define throw_tx_error(ctx,pref) do{\
    rp_msg_finalize();\
    duk_push_string(ctx, finfo->errmap);\
    rp_log_error(ctx);\
    RP_THROW(ctx, "%s error: %s",pref, duk_get_string(ctx,-1));\
}while(0)

#define throw_tx_error_close(ctx,pref,h) do{\
    rp_msg_finalize();\
    duk_push_string(ctx, finfo->errmap);\
    rp_log_error(ctx);\
    h_close(h);\
    RP_THROW(ctx, "%s error: %s",pref, duk_get_string(ctx,-1));\
}while(0)


/* True when this thread's mmsgfh is the stream that writes the buffer we
 * are about to read.  For a forked connection finfo->errmap is instead the
 * shared map the helper child writes, and mmsgfh refers to an unrelated
 * per-thread buffer, so its position says nothing about the map. */
#define rp_msgbuf_is_ours() \
    (mmsgfh != NULL && finfo != NULL && finfo->errmap != NULL && finfo->errmap == rp_errmap)

/* Reset the capture buffer to a known-empty state.
 *
 * The WHOLE buffer is zeroed, not just the first byte.  texis writes
 * through mmsgfh, an fmemopen() stream, and fmemopen only writes its own
 * NUL when a write extends past the buffer's previous high-water mark.
 * A message shorter than the one before it therefore leaves the older
 * message's tail in place -- and every reader here uses strlen(), so it
 * would report the new message with the old one's tail still attached.
 * Zeroing makes any subsequent write self-terminating, identically on the
 * in-process and helper-child paths. */
#define clearmsgbuf() do {                \
    if(mmsgfh == NULL) rp_msg_init();     \
    if(mmsgfh) {                          \
        fflush(mmsgfh);                   \
        fseek(mmsgfh, 0, SEEK_SET);       \
    }                                     \
    if(finfo && finfo->errmap)            \
        memset(finfo->errmap, 0, msgbufsz);\
    else if(rp_errmap)                    \
        memset(rp_errmap, 0, msgbufsz);   \
} while(0)

/* Move whatever is already in the buffer to this.errMsg, THEN start
 * clean.  Use this wherever the buffer is being emptied to isolate the
 * next operation's message -- a bare clearmsgbuf() there silently drops
 * whatever the previous operation had to say.  sql.set() applies many
 * properties in a loop and clears before each setprop(), so without this
 * only the last property's warning could ever reach errMsg.
 *
 * rp_log_error() alone is not enough: in the helper child it finalizes
 * the shared map for the parent and returns without clearing, so the
 * clearmsgbuf() must follow it on both sides. */
#define logandclearmsgbuf(ctx) do {       \
    rp_log_error(ctx);                    \
    clearmsgbuf();                        \
} while(0)


/* **************************************************
     store an error string in this.errMsg
   **************************************************   */
static void rp_log_copy_to_errMsg(duk_context *ctx, char *msg)
{
    /* Nothing to add.  Without this, appending an empty message to an
     * existing errMsg still runs the "%s\n%s" join below and tacks on a
     * bare newline -- and rp_log_error() is called several times per
     * statement (after params, after exec, at end/end_query), so a
     * message logged early picked up a trailing newline from every
     * later no-op call.  That is what made query() and exec() report
     * different text for one identical failure. */
    if(!msg || !*msg)
        return;

    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, "errMsg"))
    {
        const char *s = duk_get_string(ctx, -1);
        if(s && *s)
        {
            duk_push_sprintf(ctx, "%s\n%s", s,msg);
        }
        else
            duk_push_string(ctx, msg);
        duk_remove(ctx, -2);
    }
    else
    {
        duk_pop(ctx);
        duk_push_string(ctx, msg);
    }
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
}

/* Terminate the capture buffer at the stream's write position.
 *
 * ftell() is the authority on where the current message ends; strlen() is
 * not, because fmemopen leaves the previous (longer) message's tail in
 * place behind a shorter one.  Only ever terminates when the stream has
 * actually been written to this round: pos == 0 means nothing came through
 * mmsgfh, and the buffer may hold a message written directly with
 * snprintf() (rp_add_named_parameters does exactly that), which must not
 * be truncated away.  Terminating at a position past a directly-written
 * message is harmless -- strlen() stops at its NUL first. */
static void rp_msg_terminate(void)
{
    long pos;

    if(!mmsgfh || !finfo || !finfo->errmap)
        return;

    fflush(mmsgfh);
    pos = ftell(mmsgfh);
    if(pos <= 0)
        return;
    if(pos > msgbufsz - 1)
        pos = msgbufsz - 1;
    finfo->errmap[pos] = '\0';
}

/* Bring the buffer to its final, readable form: terminated at the end of
 * the current message, with one trailing newline removed.  Idempotent.
 *
 * Everything that reads the buffer must go through this, including the
 * throw macros -- they format the Error text straight out of
 * finfo->errmap, so if only rp_log_error() finalized it the thrown
 * message and this.errMsg would differ by a trailing newline for the
 * same failure. */
static void rp_msg_finalize(void)
{
    int pos;

    if(!finfo || !finfo->errmap)
        return;

    /* In the helper child the buffer is always the one our own mmsgfh
     * wrote; in the parent only when the connection is in-process. */
    if(RP_TX_isforked || rp_msgbuf_is_ours())
        rp_msg_terminate();

    pos = (int) strlen(finfo->errmap);
    if(pos && finfo->errmap[pos-1]=='\n')
        finfo->errmap[pos-1]='\0';
}

static int rp_log_error(duk_context *ctx)
{
    if(!finfo || !finfo->errmap)
        return 0;

    rp_msg_finalize();

    /* Helper child: no JS `this` to log to.  Finalizing the shared map is
     * the whole job -- the parent reads it with strlen(). */
    if(RP_TX_isforked)
        return 0;

    int ret = (int) !!(finfo->errmap[0]); //simple !!strlen()

    rp_log_copy_to_errMsg(ctx, finfo->errmap);

    /* Full clear + rewind, not just errmap[0]='\0': a later, shorter
     * message in this same statement would otherwise be read with this
     * one's tail attached.  See clearmsgbuf(). */
    memset(finfo->errmap, 0, msgbufsz);
    if(rp_msgbuf_is_ours())
        rewind(mmsgfh);

    return ret;
}

/* get the expression from a /pattern/ or a "string" */
static const char *get_exp(duk_context *ctx, duk_idx_t idx)
{
    const char *ret=NULL;

    if(duk_is_object(ctx,idx) && duk_has_prop_string(ctx,idx,"source") )
    {
        // its a /pattern/, raw text is in property "source"
        duk_get_prop_string(ctx,idx,"source");
        ret=duk_get_string(ctx,-1);
        duk_pop(ctx);
    }
    else if ( duk_is_string(ctx,idx) )
        ret=duk_get_string(ctx,idx);

    return ret;
}

/* Parent<->helper pipe I/O.
 *
 * These MUST NOT report a partial transfer as anything a caller can
 * mistake for success: callers test `== -1' or `!= sizeof(x)', so a
 * short count that is neither would be read as "fine" and the caller
 * would act on a half-filled object -- which desynchronizes the
 * protocol, the one failure that is silent and unrecoverable.  So the
 * rule here is all-or-nothing: the full count, or -1.
 *
 * Also: retry on EINTR (a signal arriving mid-transfer is not an
 * error), and walk the buffer as char * -- the previous macros did
 * `(b)+r' on the caller's own pointer type, so a resumed transfer of,
 * say, an int * would continue at b + r*sizeof(int), past the object.
 *
 * A failure in the helper child is fatal, as before: it has no one to
 * report to but the pipe that just died. */
static int fork_write_all(const void *buf, size_t sz, int line)
{
    const char *p = (const char *)buf;
    size_t off = 0;

    if (sz > (size_t)INT_MAX) return -1;
    while (off < sz)
    {
        ssize_t r = write(finfo->writer, p + off, sz - off);

        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        fprintf(stderr,
                "rampart-sql helper: write failed: '%s' at %d, fd:%d\n",
                strerror(errno), line, finfo->writer);
        if (thisfork) { fprintf(stderr, "child proc exiting\n"); exit(0); }
        return -1;
    }
    return (int)sz;
}

static int fork_read_all(void *buf, size_t sz, int line)
{
    char *p = (char *)buf;
    size_t off = 0;

    if (sz > (size_t)INT_MAX) return -1;
    while (off < sz)
    {
        ssize_t r = read(finfo->reader, p + off, sz - off);

        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r == 0)                     /* EOF: the peer is gone */
            fprintf(stderr,
                    "rampart-sql helper: read got EOF from %d at %d (peer exited)\n",
                    finfo->reader, line);
        else
            fprintf(stderr,
                    "rampart-sql helper: read failed from %d: '%s' at %d\n",
                    finfo->reader, strerror(errno), line);
        if (thisfork) { fprintf(stderr, "child proc exiting\n"); exit(0); }
        return -1;
    }
    return (int)sz;
}

#define forkwrite(b,c) fork_write_all((b),(c),__LINE__)
#define forkread(b,c)  fork_read_all((b),(c),__LINE__)


/*
static size_t mmwrite(FMINFO *mapinfo, void *data, size_t size)
{
    if(size < mmap_rem)
    {
        memcpy(mapinfo->pos, data, size);
        mapinfo->pos += size;
        return size;
    }
    return 0;
}

static size_t mmread(FMINFO *mapinfo, void *data, size_t size)
{
    if(size < mmap_rem)
    {
        memcpy(data, mapinfo->pos, size);
        mapinfo->pos += size;
        return size;
    }
    return 0;
}
*/

static void free_thread_handles();

static void clean_thread(void *arg)
{
    pid_t *kpid = (pid_t*)arg;

    free_thread_handles();

    if(finfo)
    {
        forkwrite("X", sizeof(char));

        if(finfo->reader != -1)
        {
            close(finfo->reader);
            finfo->reader=-1;
        }
        if(finfo->writer != -1)
        {
            close(finfo->writer);
            finfo->writer=-1;
        }
        if(finfo->mapfd != -1)
        {
            close(finfo->mapfd);
            finfo->mapfd=-1;
        }
        if(finfo->errfd != -1)
        {
            close(finfo->errfd);
            finfo->errfd=-1;
        }
        if(finfo->mapinfo)
        {
            if(finfo->mapinfo->mem)
            {
                if(munmap(finfo->mapinfo->mem, finfo->mapsize) != 0)
                    fprintf(stderr, "error unmapping mapinfo->mem at %s:%d - %s\n", __FILE__,__LINE__,strerror(errno));
            }
            free(finfo->mapinfo);
        }

        if(finfo->errmap)
        {
            if(munmap(finfo->errmap, msgbufsz) != 0)
                fprintf(stderr, "error unmapping errmap at %s:%d - %s\n", __FILE__,__LINE__,strerror(errno));
        }

        if(finfo->aux)
            free(finfo->aux);
        if(finfo->fl)
            free(finfo->fl);

        free(finfo);
        finfo=NULL;
    }

    kill(*kpid,SIGTERM);
}

static int rp_memfd_create(size_t size, int type) {
    char shm_name[NAME_MAX];
    int fd;
    int id=get_thread_num();

    // Generate a unique shared memory object name rpmem-pid-thrno-type
    snprintf(shm_name, NAME_MAX, "/rpmem-%d-%d-%d", getpid(), id, type);

    // Create the shared memory object
    fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        perror("shm_open");
        return -1;
    }

    // Unlink the shared memory object to make it anonymous
    if (shm_unlink(shm_name) == -1) {
        perror("shm_unlink");
        close(fd);
        return -1;
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        close(fd);
        return -1;
    }

    // Clear the FD_CLOEXEC flag
    int flags_fd = fcntl(fd, F_GETFD);
    if (flags_fd == -1) {
        perror("fcntl F_GETFD");
        close(fd);
        return -1;
    }

    flags_fd &= ~FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags_fd) == -1) {
        perror("fcntl F_SETFD");
        close(fd);
        return -1;
    }

    return fd;
}

static void do_child_loop(SFI *finfo);
static int fork_setmem();
static int fork_seterr();

/* payload messaging (defined below, next to cwrite) */
static void  msg_begin(SFI *finfo);
static int   msg_append(SFI *finfo, const void *data, size_t sz);
static int   msg_append_aligned(SFI *finfo, const void *data, size_t sz, size_t tsz);
static int   msg_end(SFI *finfo);
static void *msg_recv(SFI *finfo, int *sizep);
static void  msg_release_above(SFI *finfo, size_t keep);

#define Create 1
#define NoCreate 0

#define MEMMAP 0
#define ERRMAP 1

static char *scr_txt = "var S=require('rampart-sql.so');S.__helper(%d,%d,%d);\n";

/* A helper died: every TEXIS* we cached for it points into a process
 * that no longer exists.  Mark them all unusable so nothing hands one to
 * the replacement, and drop the settings fast-path cache so sql.set() is
 * re-applied to the new child (h_reset_tx_default skips re-applying when
 * `last_sql_set' still matches the connection object -- against a fresh
 * helper that would silently run with default settings). */
static void invalidate_thread_handles(uint16_t forknum)
{
    DB_HANDLE *p;

    HLOCK
    for (p = db_handle_head; p; p = p->next)
        if (p->forknum == forknum)
        {
            p->tx = NULL;
            DB_HANDLE_CLEAR(p, DB_FLAG_EMBED_CHILD_REGISTERED);
        }
    for (p = db_handle_available_head; p; p = p->next)
        if (p->forknum == forknum)
        {
            p->tx = NULL;
            DB_HANDLE_CLEAR(p, DB_FLAG_EMBED_CHILD_REGISTERED);
        }
    HUNLOCK

    last_sql_set = NULL;
}

static SFI *check_fork(DB_HANDLE *h, int create)
{
    //int pidstatus;
    if(finfo == NULL)
    {
        if(!create)
        {
            fprintf(stderr, "Unexpected Error: previously opened pipe info no longer exists for forknum %d\n",h->forknum);
            exit(1);
        }
        else
        {
            REMALLOC(finfo, sizeof(SFI));
            memset(finfo, 0, sizeof(SFI));
            finfo->reader=-1;
            finfo->writer=-1;
            finfo->childpid=0;
            /* the field list for any fetch calls*/
            finfo->fl=NULL;
            /* the shared mmap */
            finfo->mapinfo=NULL;
            /* the aux buffer */
            finfo->aux=NULL;
            finfo->auxsz=0;
            finfo->auxpos=NULL;
            REMALLOC(finfo->mapinfo, sizeof(FMINFO));

            /* Best-effort reservation: ask for the configured size, and
             * if the kernel refuses (an older/stricter POSIX shm limit,
             * a tight sandbox) quietly fall back to the classic 1MB.  A
             * refusal then costs performance -- more messages chunk --
             * never correctness, because every payload chunks anyway. */
            {
                size_t want = g_forkmap_size;

                if (want < RP_FORKMAP_MIN) want = RP_FORKMAP_MIN;
                finfo->mapfd = rp_memfd_create(want, MEMMAP);
                if (finfo->mapfd == -1 && want > FORKMAPSIZE)
                {
                    want = FORKMAPSIZE;
                    finfo->mapfd = rp_memfd_create(want, MEMMAP);
                }
                if(finfo->mapfd == -1)
                {
                    fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                    exit(1);
                }

                finfo->mapinfo->mem = mmap(NULL, want, PROT_READ|PROT_WRITE,
                                           MAP_SHARED, finfo->mapfd, 0);
                if(finfo->mapinfo->mem == MAP_FAILED && want > FORKMAPSIZE)
                {
                    close(finfo->mapfd);
                    want = FORKMAPSIZE;
                    finfo->mapfd = rp_memfd_create(want, MEMMAP);
                    if(finfo->mapfd != -1)
                        finfo->mapinfo->mem = mmap(NULL, want,
                                                   PROT_READ|PROT_WRITE,
                                                   MAP_SHARED, finfo->mapfd, 0);
                }
                if(finfo->mapfd == -1 || finfo->mapinfo->mem == MAP_FAILED)
                {
                    fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                    exit(1);
                }
                finfo->mapsize = want;
            }
            finfo->mapinfo->pos = finfo->mapinfo->mem;

            finfo->errfd=rp_memfd_create(msgbufsz, ERRMAP);
            if(finfo->errfd == -1)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                exit(1);
            }

            finfo->errmap = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->errfd, 0);
            if(finfo->errmap == MAP_FAILED)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                exit(1);
            }
        }

    }

    parent_pid=getpid();

    /* waitpid doesn't work here because SIGCHLD is SIG_IGN (auto-reap).
       kill(pid,0) reliably detects whether the child is still alive. */
    //if (!finfo->childpid || waitpid(finfo->childpid, &pidstatus, WNOHANG))
    if (!finfo->childpid || kill(finfo->childpid, 0) != 0)
    {
        if (!create)
            return NULL;

        /* Respawning: anything cached from the previous helper is stale. */
        if (finfo->childpid)
            invalidate_thread_handles(h->forknum);

        int child2par[2], par2child[2];
        //signal(SIGPIPE, SIG_IGN); //macos
        //signal(SIGCHLD, SIG_IGN);
        /* our creation run.  create pipes and setup for fork */
        if (rp_pipe(child2par) == -1)
        {
            fprintf(stderr, "child2par pipe failed\n");
            return NULL;
        }

        if (rp_pipe(par2child) == -1)
        {
            fprintf(stderr, "par2child pipe failed\n");
            return NULL;
        }

        /* if child died, close old pipes */
        if (finfo->writer > 0)
        {
            close(finfo->writer);
            finfo->writer = -1;
        }
        if (finfo->reader > 0)
        {
            close(finfo->reader);
            finfo->reader = -1;
        }


        /***** fork ******/
        /* NOTE — alternative design (saved in case the SIG_IGN below ever
         * needs to go away):  DOUBLE-FORK the helper so it gets reparented
         * to init at birth.  init reaps it when it exits.  Then the
         * `signal(SIGCHLD, SIG_IGN)` line below is no longer necessary —
         * which removes the global side effect that breaks
         * `rampart.utils.exec`'s waitpid in unrelated threads.
         *
         * Sketch:
         *   pid_t first = fork();                                 // (1) parent forks first child
         *   if (first == 0) {
         *       pid_t gc = fork();                                // (2) first child forks grandchild
         *       if (gc < 0) _exit(1);
         *       if (gc > 0) {                                     // (3) first child reports gc pid to parent and exits
         *           write(child2par[1], &gc, sizeof(gc));
         *           _exit(0);
         *       }
         *       // grandchild — same exec path as the existing child branch,
         *       // but pass the ORIGINAL parent pid as an extra script arg
         *       // (use parent_pid captured before fork(); getppid() will be 1
         *       // by the time we get here because the first child exited).
         *       sprintf(script, scr_txt, par2child[0], child2par[1],
         *               h->forknum, parent_pid);                  // 4 args, not 3
         *       execl(rampart_exec, rampart_exec, "-c", script, NULL);
         *       _exit(0);
         *   }
         *   waitpid(first, NULL, 0);                              // (4) reap the first child
         *   read(child2par[0], &finfo->childpid, sizeof(pid_t));  // (5) grandchild pid
         *
         * Then in fork_helper() (the helper-side entry), read the new arg
         * and pass it explicitly to rp_watch_pid() instead of getppid()
         * (which would be 1 = init).
         *
         * Reason this is NOT the chosen fix: significant restructuring of
         * a fork lifecycle that took non-trivial effort to get right (no
         * orphan helpers, no zombies). The chosen fix lives in
         * rampart-utils.c's exec() — it briefly toggles SIGCHLD to SIG_DFL
         * around its own fork+waitpid window, leaving sql_helper's lifecycle
         * code completely untouched.
         */
        finfo->childpid = fork();

        if (finfo->childpid < 0)
        {
            fprintf(stderr, "fork failed");
            finfo->childpid = 0;
            return NULL;
        }

        if(finfo->childpid == 0)
        { /* child is forked once then talks over pipes. */
            char script[1024];

            close(child2par[0]);
            close(par2child[1]);

            /* rp_pipe() makes both ends close-on-exec so they never leak
             * into unrelated subprocesses.  These two are handed to the
             * helper BY NUMBER in the script text below, so they must
             * survive our execl -- clear the flag here, in the child
             * only: doing it in the parent would leak them again. */
            rp_fd_keep_on_exec(par2child[0]);
            rp_fd_keep_on_exec(child2par[1]);

            sprintf(script, scr_txt, par2child[0], child2par[1], h->forknum);
            execl(rampart_exec, rampart_exec, "-c", script, NULL);
            exit(0);
        }
        else
        {
            pid_t *pidarg=NULL;

            //parent
            signal(SIGPIPE, SIG_IGN); //macos
            signal(SIGCHLD, SIG_IGN);
            rp_pipe_close(child2par,1);
            rp_pipe_close(par2child,0);
            finfo->reader = child2par[0];
            finfo->writer = par2child[1];
            fcntl(finfo->reader, F_SETFL, 0);

            // a callback to kill child
            REMALLOC(pidarg, sizeof(pid_t));
            *pidarg = finfo->childpid;
            set_thread_fin_cb(rpthread[h->forknum], clean_thread, pidarg);

            if(!fork_setmem())
            {
                free(finfo);
                return NULL;
            }

            if(!fork_seterr())
            {
                free(finfo);
                return NULL;
            }
        }
    }
    return finfo;
}

static void free_thread_handles()
{
    DB_HANDLE *h=db_handle_head, *n;
    uint16_t forknum = get_thread_num();

    // remove and free any in in-use list
    HLOCK
    while(h)
    {
        n=h->next;
        if(forknum == h->forknum)
            h_close(h);
        h=n;
    }
    HUNLOCK

    // remove and free all in thread local available list
    h=db_handle_available_head;
    while(h)
    {
        n=h->next;
        h_close(h);
        h=n;
    }

}

static void free_all_handles(void *unused)
{
    DB_HANDLE *n, *h=db_handle_head;

    /* Backstop flush.  Most handles will already have flushed via their
     * own h_close() path while their DDIC was live; this catches any
     * vec_handle_cache entries that weren't tied to a connection (or
     * that survived).  ddic may be null here — in that case we still
     * save the .vec files but skip SYSINDEX dirty-bit clearing
     * (next open will reconcile). */
    TXvecFlushAll(TXvecGetExitHookDDIC());

    while(h)
    {
        n=h->next;
        h_close(h);
        h=n;
    }
}

static int fork_open(DB_HANDLE *h, const char *user, const char *pass);

/* find first unused handle, create as necessary */
static DB_HANDLE *h_open(const char *db, const char *user, const char *pass)
{
    DB_HANDLE *h = NULL;

    h=find_available_handle(db, user, pass, DBH_MARK_IN_USE);

    if(!h)
    {
        h=new_handle(db, user, pass);
        add_handle(h); //handle added to main list
        if( DB_HANDLE_IS(h, DB_FLAG_FORK) )
        {
            //  if pipe error
            if( !fork_open(h,user,pass) )
            {
                HLOCK
                h=free_handle(h); //h==NULL
                HUNLOCK
            }
        }
        else
        {
            rp_msg_init(); /* per-thread capture buffer (mmsgfh is
                              thread-local) BEFORE texis_open can putmsg */
            h->tx=texis_open((char *)(db), (char*)user, (char*)pass);
            // if not using forked child, make sure we have a place to log errors in this proc
            if(!finfo)
            {
                REMALLOC(finfo, sizeof(SFI));
                memset(finfo, 0, sizeof(SFI));
                finfo->reader=-1;
                finfo->writer=-1;
                finfo->errmap=rp_errmap;
            }
        }

        // error opening tx handle
        if(h && !h->tx)
        {
            HLOCK
            h=free_handle(h); //h==NULL
            HUNLOCK
        }
    }

    return h;
}

/*********** CHILD/PARENT FUNCTION PAIRS ************/

static int fork_create(const char *db)
{
    int ret=0;

    if(strlen(db) > PATH_MAX)
        return ret;

    strncpy(finfo->mapinfo->mem, db, PATH_MAX+1);

    if(forkwrite("C", sizeof(char)) == -1)
        return ret;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_create()
{
    int ret=0;

    ret=createdb(finfo->mapinfo->mem);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int get_chunks(int size)
{
    int pos=0;
    finfo->mapwrapped = 1;
    size *= -1;

    if(finfo->auxsz < finfo->mapsize * 2)
    {
        finfo->auxsz = finfo->mapsize * 2;
        REMALLOC(finfo->aux, finfo->auxsz);
    }

    while(1)
    {
        finfo->auxpos = finfo->aux + pos;
        memcpy(finfo->auxpos, finfo->mapinfo->mem, size);
        pos += size; // for next round
        if(forkwrite("C",sizeof(char))==-1) //ask for more
            return 0;
        if(forkread(&size,sizeof(int))==-1) //get the next chunk size
            return 0;
        if(size > -1) //we are done, get remaining data
        {
            if(size + pos > finfo->auxsz)
            {
                finfo->auxsz += size;
                REMALLOC(finfo->aux, finfo->auxsz);
            }
            finfo->auxpos = finfo->aux + pos;
            memcpy(finfo->auxpos, finfo->mapinfo->mem, size);

            /* TOTAL reassembled bytes, not just this last chunk: callers
             * that must know the payload length (e.g. sizing a CBOR
             * buffer) otherwise get the tail only and silently decode a
             * truncated message. */
            return pos + (int)size;
        }
        size *=-1;
        if (size + pos > finfo->auxsz)
        {
            finfo->auxsz *=2;
            REMALLOC(finfo->aux, finfo->auxsz);
        }
    }
    return 0; // no nag
}

static FLDLST * fork_fetch(DB_HANDLE *h,  int stringsFrom)
{
    FLDLST *ret=NULL;
    int i=0, retsize=0;
    int *ilst=NULL;
    FMINFO *mapinfo;
    size_t eos=0;
    void *buf;

    check_fork(h, NoCreate);

    mapinfo = finfo->mapinfo;
    buf=mapinfo->mem;

    if(!finfo)
        return NULL;

    if (fork_maybe_register_embed(h) != 0) return NULL;

    if(forkwrite("f", sizeof(char)) == -1)
        return NULL;

    if(forkwrite(&(h->tx), sizeof(TEXIS*)) == -1)
        return NULL;

    if(forkwrite(&(stringsFrom), sizeof(int)) == -1)
        return NULL;

    /* 'B' callbacks may interleave (scalar embed() in SELECT list
     * fires during texis_fetch).  Drain them before reading retsize. */
    if (fork_drain_embed_callbacks() != 0) return NULL;

    /* NULL here means the pipe failed OR the helper sent its -1 "no more
     * rows / error" sentinel; both end the fetch. */
    buf = msg_recv(finfo, &retsize);
    if(!buf)
    {
        if(finfo->aux)
        {
            free(finfo->aux);
            finfo->aux=NULL;
            finfo->auxsz=0;
            finfo->auxpos=NULL;
        }
        return NULL;
    }

    /* unserialize results and make a new fieldlist */
    if (finfo->fl == NULL)
    {
        REMALLOC(finfo->fl, sizeof(FLDLST));
        finfo->fl->n=0;
        memset(finfo->fl, 0, sizeof(FLDLST));
    }
    ret = finfo->fl;

    /* first int is fl->n */
    ilst = buf;
    ret->n = ilst[0];
    eos += sizeof(int);

    /* types is an array of ints at beginning */
    ilst=(buf) + eos;
    for (i=0;i<ret->n;i++)
        ret->type[i]=ilst[i];
    eos += sizeof(int) * ret->n;

    /* ndata is an array of ints following type ints */
    ilst=(buf) + eos;
    for (i=0;i<ret->n;i++)
        ret->ndata[i] = ilst[i];
    eos += sizeof(int) * ret->n;

    /* next an array of null terminated strings for names */
    for (i=0;i<ret->n;i++)
    {
        ret->name[i]= (buf) + eos;
        eos += strlen(ret->name[i]) + 1;
    }

    /* last is the data itself.  Each field is not necessarily NULL terminated
       and strings may be shorter than field width */
    for (i=0;i<ret->n;i++)
    {
        char type = ret->type[i] & 0x3f;
        size_t type_size = ddftsize(type),
               size = type_size * (size_t)ret->ndata[i];
        if(size==0)
            ret->data[i]=NULL;
        else
        {
            size_t size_mod = eos % type_size;

            //align to type
            if (size_mod)
                eos += (type_size - size_mod);

            ret->data[i]= (buf) + eos;
            eos += size;
        }
    }
    return ret;
}

static int cwrite(SFI *finfo, void *data, size_t sz)
{
    FMINFO *mapinfo = finfo->mapinfo;
    size_t rem = mmap_rem; //space available in mmap
    char c;
    int used = -1 * (int)finfo->mapsize;

    while (rem < sz)
    {
        finfo->mapwrapped = 1;
        memcpy(mapinfo->pos, data, rem);
        /* send negative to signal chunk */
        if( forkwrite(&used,sizeof(int)) == -1)
            return 0;
        /* wait for parent to be ready for next */
        if( forkread(&c,sizeof(char)) == -1)
            return 0;
        /* reset to beginning of map mem */
        mapinfo->pos=mapinfo->mem;
        data += rem;
        sz -= rem;
        rem = finfo->mapsize;
    }

    memcpy(mapinfo->pos, data, sz);
    mapinfo->pos += sz;

    return 1;
}

static int cwrite_aligned(SFI *finfo, void *data, size_t sz, size_t tsz)
{
    FMINFO *mapinfo = finfo->mapinfo;

    //align to type
    if(tsz>1)
    {
        size_t sz_mod = (size_t)mapinfo->pos % tsz;
        if(sz_mod)
            mapinfo->pos += (tsz - sz_mod);
    }

    return cwrite(finfo, data, sz);

}

/* ---------------- payload messaging ----------------
 *
 * Thin wrappers over cwrite()/get_chunks() -- the SAME wire protocol,
 * not a new one.  They exist so that every message body is sent and
 * received the same way, instead of each call site open-coding
 * mmap_reset, the trailing size write and the negative-chunk decode:
 *
 *   sender      msg_begin();  msg_append(...)...;  msg_end();
 *   receiver    p = msg_recv(&sz);
 *
 * A payload of any size works: cwrite() fills the map, signals a full
 * map with a NEGATIVE int, waits for the peer's "C", and repeats;
 * msg_end() writes the final (positive) byte count.  msg_recv()
 * reassembles into finfo->aux when it sees a negative first size.
 *
 * ORDERING TRAP: a message's tag and any fixed header values must be
 * fully exchanged BEFORE msg_begin(), because a chunk signal is
 * indistinguishable from a tag byte to a peer sitting in a different
 * read loop.  (That is exactly the bug fixed in child_fetch(): its 'A'
 * tag was written after serialize_fl(), so the parent's callback-drain
 * loop consumed the chunk signal's low byte as a tag.)
 */
static void msg_begin(SFI *finfo)
{
    FMINFO *mapinfo = finfo->mapinfo;
    finfo->mapwrapped = 0;
    mmap_reset;
}

static int msg_append(SFI *finfo, const void *data, size_t sz)
{
    return cwrite(finfo, (void *)data, sz);
}

/* For data the receiver reads IN PLACE out of the map: keeps each datum
 * on its natural boundary so ARM does not fault on an unaligned load. */
static int msg_append_aligned(SFI *finfo, const void *data, size_t sz,
                              size_t tsz)
{
    return cwrite_aligned(finfo, (void *)data, sz, tsz);
}

/* Close the message: the final chunk's byte count, always >= 0. */
static int msg_end(SFI *finfo)
{
    FMINFO *mapinfo = finfo->mapinfo;
    int used = (int)mmap_used;
    int rc = forkwrite(&used, sizeof(int)) == -1 ? -1 : 0;

    /* This message spilled past the segment, so every page of it is now
     * resident.  Give back everything above the classic 1MB: a one-off
     * huge row should not inflate a long-lived helper for good.  Only
     * fires for chunked messages, so a steady stream of ordinary ones
     * never pays the re-fault cost. */
    if (finfo->mapwrapped)
    {
        msg_release_above(finfo, FORKMAPSIZE);
        finfo->mapwrapped = 0;
    }
    return rc;
}

/* Hand back the pages an unusually large message touched.
 *
 * Only Linux can do this: MADV_REMOVE / FALLOC_FL_PUNCH_HOLE actually
 * free a shm object's backing store.  macOS and FreeBSD have neither --
 * MADV_DONTNEED is a no-op for a shared mapping and ftruncate cannot
 * shrink a POSIX shm object -- so there the high-water mark is simply
 * permanent for the life of the helper, which is why the reservation is
 * kept modest.  A no-op elsewhere, never an error. */
static void msg_release_above(SFI *finfo, size_t keep)
{
#if defined(__linux__) && defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
    if (finfo->mapfd >= 0 && finfo->mapsize > keep)
        (void)fallocate(finfo->mapfd,
                        FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                        (off_t)keep, (off_t)(finfo->mapsize - keep));
#else
    (void)finfo; (void)keep;
#endif
}

/* Receive a payload.  Returns the data (the map itself when it arrived
 * in one piece -- no copy -- or finfo->aux when it was reassembled),
 * with *sizep set to the TOTAL payload length.  Returns NULL and
 * *sizep == -1 for a pipe error or the sender's "nothing here"
 * sentinel; callers that use -1 as a sentinel (fork_fetch: no more
 * rows) check *sizep themselves. */
static void *msg_recv(SFI *finfo, int *sizep)
{
    int size = 0;

    if (forkread(&size, sizeof(int)) == -1) { *sizep = -1; return NULL; }
    *sizep = size;
    if (size == -1) return NULL;                /* sender's sentinel */
    if (size < -1)                              /* multi-chunk */
    {
        *sizep = size = get_chunks(size);
        if (finfo->mapwrapped)
        {
            msg_release_above(finfo, FORKMAPSIZE);
            finfo->mapwrapped = 0;
        }
        return finfo->aux;
    }
    return finfo->mapinfo->mem;
}

/* Serialize ONE row.  Caller must have completed the tag/header exchange
 * first (see msg_begin): chunk signals emitted from here would otherwise
 * be read as tags by a peer in another loop. */
static int serialize_fl(SFI *finfo, FLDLST *fl)
{
    FMINFO *mapinfo = finfo->mapinfo;
    int i=0;
    msg_begin(finfo);

    /* single int = fl->n */
    if(!msg_append(finfo, &(fl->n), sizeof(int)))
        return -1;

    /* array of ints for type*/
    for (i = 0; i < fl->n; i++)
    {
        int type =  fl->type[i];
        if(!msg_append(finfo, &type, sizeof(int)))
            return -1;
    }

    /* array of ints for ndata */
    for (i = 0; i < fl->n; i++)
    {
        int ndata =  fl->ndata[i];

        // TODO: I think this is always the case, but check with TS
        // before removing the next two lines.
        if(fl->data[i] == NULL)
            ndata=0;

        if(!msg_append(finfo, &ndata, sizeof(int)))
            return -1;
    }

    /* array of names -> [col_name1, \0, col_name2, \0 ...] */
    for (i = 0; i < fl->n; i++)
    {
        char *name =  fl->name[i];
        size_t l = strlen(name)+1; //include the \0
        if(!msg_append(finfo, name, l))
            return -1;
    }
    /* data in seq - length of each determined by sizeof(type) * ndata */
    for (i = 0; i < fl->n; i++)
    {
        char type = fl->type[i] & 0x3f;
        size_t type_size = ddftsize(type),
               size = type_size * (size_t)fl->ndata[i];
        if(size !=0 && fl->data[i] != NULL)
        {
            // align ints, etc for arm
            if(!msg_append_aligned(finfo, fl->data[i], size, type_size))
                return -1;
        }
    }

    return (int) mmap_used;
}


static int child_fetch()
{
    int stringFrom=-9999;
    int ret=-1;
    FLDLST *fl;
    TEXIS *tx=NULL;
    char atag = 'A';

    /* idx */
    if (forkread(&tx, sizeof(TEXIS *) )  == -1)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(!tx)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    /* stringFrom */
    if (forkread(&stringFrom, sizeof(int))  == -1)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(stringFrom == -9999)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    /* texis_fetch may invoke the registered TXembedFunc
     * (child_embed_callback) for SELECT-list embed() calls; those
     * emit 'B' interleaved with this response.  Final tag is 'A'. */
    fl = texis_fetch(tx, stringFrom);

    /* 'A' must be written HERE -- after the last callback texis_fetch
     * could emit, but BEFORE serialize_fl().
     *
     * The parent sits in fork_drain_embed_callbacks() reading one byte
     * at a time until it sees 'A'.  serialize_fl() sends a row larger
     * than the map in chunks, and cwrite() signals each one with a
     * NEGATIVE int -- whose first byte, little-endian, is 0x00
     * (-1048576 = 00 00 f0 ff).  With 'A' written afterwards the drain
     * loop consumed that 0x00 as a tag and the pipe desynchronized, so
     * ANY query returning a row bigger than the map failed in a forked
     * helper.  Writing 'A' first hands the chunk signals to
     * fork_fetch's retsize/get_chunks logic, which is what expects
     * them. */
    if (forkwrite(&atag, 1) == -1) return 0;

    if(fl)
        ret = serialize_fl(finfo,fl);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    if(ret<0)
        return 0;

    return ret;
}

static int fork_param(
    DB_HANDLE *h,
    int ipar,
    void *buf,
    long *len,
    int ctype,
    int sqltype
)
{
    int ret=0;
    FMINFO *mapinfo;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    mapinfo = finfo->mapinfo;
    (void)mapinfo;

    /* tag + fixed header FIRST, then the payload (see msg_begin) */
    if(forkwrite("P", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    msg_begin(finfo);
    if(!msg_append(finfo, &ipar, sizeof(int)))
        return 0;

    if(!msg_append(finfo, &ctype, sizeof(int)))
        return 0;

    if(!msg_append(finfo, &sqltype, sizeof(int)))
        return 0;

    if(!msg_append(finfo, len, sizeof(long)))
        return 0;

    if(!msg_append(finfo, buf, (size_t)*len))
        return 0;

    if(msg_end(finfo) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_param()
{
    int ret=0, retsize=0;
    void *buf = finfo->mapinfo->mem;
    int ipar, ctype, sqltype;
    long *len;
    int *ip;
    void *data;
    size_t pos=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(!tx)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    buf = msg_recv(finfo, &retsize);
    if (!buf)
        return 0;

    ip = buf;
    ipar = *ip;
    pos += sizeof(int);

    ip = pos + buf;
    ctype = *ip;
    pos += sizeof(int);

    ip = pos + buf;
    sqltype = *ip;
    pos += sizeof(int);

    len = pos + buf;
    pos += sizeof(long);

    data = pos + buf;

    ret = texis_param(tx, ipar, data, len, ctype, sqltype);

    if(finfo->aux)
    {
        free(finfo->aux);
        finfo->aux=NULL;
        finfo->auxsz=0;
        finfo->auxpos=NULL;
    }

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}



/* Maybe-send 'V' to register the embed callback in the helper.
 * Idempotent: only fires if the handle is embed-enabled but the
 * helper hasn't been told yet (i.e. sql.set arrived after open).
 * Returns 0 on success/skip, -1 on pipe error. */
static int fork_maybe_register_embed(DB_HANDLE *h)
{
    if (!DB_HANDLE_IS(h, DB_FLAG_EMBED_ENABLED) ||
         DB_HANDLE_IS(h, DB_FLAG_EMBED_CHILD_REGISTERED))
        return 0;
    char ack = 0;
    if (forkwrite("V", 1) == -1) return -1;
    if (forkread(&ack, 1) == -1) return -1;
    if (ack == 'K')
        DB_HANDLE_SET(h, DB_FLAG_EMBED_CHILD_REGISTERED);
    return 0;
}

static int fork_exec(DB_HANDLE *h)
{
    int ret=0;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    if (fork_maybe_register_embed(h) != 0) return 0;

    if(forkwrite("e", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return ret;

    /* 'B' callbacks may interleave before the final 'A' + int. */
    if (fork_drain_embed_callbacks() != 0) return 0;
    if (forkread(&ret, sizeof(int)) == -1) return 0;
    return ret;
}

static int child_exec()
{
    TEXIS *tx=NULL;
    int ret=0;
    char atag = 'A';

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(!tx)
    {
        forkwrite(&atag, 1);
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    /* texis_execute may invoke the registered TXembedFunc
     * (LIKEV with string RHS), emitting 'B' callbacks before 'A'. */
    ret = texis_execute(tx);

    if (forkwrite(&atag, 1) == -1) return 0;
    if (forkwrite(&ret, sizeof(int)) == -1) return 0;

    return ret;
}

static int fork_prep(DB_HANDLE *h, char *sql)
{
    int ret=0;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    if(forkwrite("p", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    /* The statement used to be snprintf'd straight into the map, which
     * silently TRUNCATED anything past FORKMAPSIZE and then executed the
     * fragment.  Sent as a payload it can be any length. */
    msg_begin(finfo);
    if(!msg_append(finfo, sql, strlen(sql) + 1))    /* include the NUL */
        return 0;
    if(msg_end(finfo) == -1)
        return 0;

    // get the result back
    if(forkread(&ret, sizeof(int)) == -1)
    {
        return 0;
    }

    return ret;
}

static int child_prep()
{
    TEXIS *tx=NULL;
    int ret=0, sqlsz=0;
    char *sql;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    sql = (char *)msg_recv(finfo, &sqlsz);
    if(!sql)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(!tx)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    //printf("%d: texis_prepare(%p, \"%s\")\n", (int)getpid(),tx, sql);
    ret = texis_prepare(tx, sql);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

/* ============================================================
 * Embed callback wire protocol (Step 4)
 *
 * Helper child → parent:   'B' + size_t tlen + bytes[tlen] text
 * Parent → helper:         size_t veclen_bytes + bytes[veclen_bytes] vec
 *                          (veclen_bytes == 0 signals failure)
 *
 * The 'B' tag arrives interleaved with the helper's final response
 * to fork_exec.  The parent's fork_exec response loop sees 'B' →
 * services the callback → reads next tag.  'A' tag means the final
 * int result is next on the wire.
 *
 * The fork_exec / child_exec protocol is the only command that ever
 * sees embed callbacks (embeds fire inside texis_execute, see plan).
 *
 * Pre-Step-4 fork_exec response was a bare int.  Now it's '<tag>'
 * with 'A' meaning "int result follows".  Other fork_/child_ pairs
 * are unchanged.
 * ============================================================ */

/* Helper-side: writes 'B' tag + text-length + prompt kind + title-length
 * over the pipe; the actual text (and title, for document-kind embeds)
 * goes through the thread-local shared mmap, as does the vector reply.
 * The kind travels with each call because the PARENT owns the model's
 * prompt strings (read once at sql.set) -- the helper never sees them.
 * Mmap is unused during exec/fetch's embed-callback window (child_exec /
 * child_fetch don't touch mmap until after the texis call returns), so
 * reusing it for the bulk-data transfer is safe and avoids a pipe
 * round-trip per kilobyte. */
static size_t child_embed_callback(void *ud,
                                   const char *text, size_t tlen,
                                   int kind, const char *title, size_t title_len,
                                   float **out_vec)
{
    (void)ud;
    *out_vec = NULL;
    if (!finfo) return 0;
    if (!title) title_len = 0;
    if (tlen == 0) return 0;

    /* tag + fixed header first, then [text][title] as one payload */
    char tag = 'B';
    if (forkwrite(&tag, 1) == -1) return 0;
    if (forkwrite((char *)&tlen, sizeof tlen) == -1) return 0;
    if (forkwrite((char *)&kind, sizeof kind) == -1) return 0;
    if (forkwrite((char *)&title_len, sizeof title_len) == -1) return 0;

    msg_begin(finfo);
    if (!msg_append(finfo, text, tlen)) return 0;
    if (title_len && !msg_append(finfo, title, title_len)) return 0;
    if (msg_end(finfo) == -1) return 0;

    size_t veclen_bytes = 0;
    if (forkread(&veclen_bytes, sizeof veclen_bytes) == -1) return 0;
    if (veclen_bytes == 0) return 0;
    if (veclen_bytes % sizeof(float) != 0) return 0;
    if (veclen_bytes / sizeof(float) > RP_MSG_SANE_FLOATS) return 0;

    int   rsz = 0;
    void *rbuf = msg_recv(finfo, &rsz);
    if (!rbuf || rsz < 0 || (size_t)rsz < veclen_bytes) return 0;

    float *vec = (float *)malloc(veclen_bytes);
    if (!vec) return 0;
    memcpy(vec, rbuf, veclen_bytes);
    *out_vec = vec;
    return veclen_bytes / sizeof(float);
}

/* Helper-side doc (chunked) embed: 'D' tag.  Same mmap discipline as
 * 'B'.  Reply: total byte length down the pipe (0 = failure), then
 * (size_t k) followed by k*dim floats in the mmap. */
static size_t child_embed_doc_callback(void *ud,
                                       const char *text, size_t tlen,
                                       const char *prefix, size_t plen,
                                       float **out_vecs, size_t *out_k,
                                       float **out_avg, float *out_coh,
                                       TXchunkSpan **out_spans)
{
    (void)ud;
    if (out_vecs) *out_vecs = NULL;
    if (out_k) *out_k = 0;
    if (out_avg) *out_avg = NULL;
    if (out_coh) *out_coh = 0.0f;
    if (out_spans) *out_spans = NULL;
    if (!finfo) return 0;
    if (!prefix) plen = 0;
    if (tlen == 0) return 0;

    /* Only chunkembed() wants spans.  Tell the parent, so chunkavg() and
     * chunkcoherence() do not inherit chunkembed's "no spans = hard
     * failure" rule. */
    char wantSpans = (out_spans != NULL);

    /* tag + fixed header first, then [text][prefix] as one payload */
    char tag = 'D';
    if (forkwrite(&tag, 1) == -1) return 0;
    if (forkwrite((char *)&tlen, sizeof tlen) == -1) return 0;
    if (forkwrite((char *)&plen, sizeof plen) == -1) return 0;
    if (forkwrite(&wantSpans, 1) == -1) return 0;

    msg_begin(finfo);
    if (!msg_append(finfo, text, tlen)) return 0;
    if (plen && !msg_append(finfo, prefix, plen)) return 0;
    if (msg_end(finfo) == -1) return 0;

    /* Parent replies: dim (0 = fail), k, coh; then the mmap holds
     * [vecs: k*dim floats][avg: dim floats][spans: k TXchunkSpans, only
     * when asked] — the full result, so the child can serve chunkembed /
     * chunkavg / chunkcoherence from one round-trip. */
    int    dim = 0;
    size_t k = 0;
    float  coh = 0.0f;
    if (forkread(&dim, sizeof dim) == -1) return 0;
    if (dim <= 0) return 0;
    if (forkread(&k, sizeof k) == -1) return 0;
    if (forkread(&coh, sizeof coh) == -1) return 0;
    if (k == 0) return 0;
    /* Bound by DIVIDING, never by multiplying: a desynced/corrupt pipe
     * could deliver a (k, dim) pair whose product wraps size_t -- which
     * on a 32-bit build slips past a product-based check.  The payload
     * itself is unbounded (it chunks), so this is purely a sanity gate
     * on values that arrived over the wire. */
    if ((size_t)dim > RP_MSG_SANE_FLOATS) return 0;
    if (k > RP_MSG_SANE_FLOATS / (size_t)dim) return 0;
    size_t vecs_floats = k * (size_t)dim;
    size_t total_bytes = (vecs_floats + (size_t)dim) * sizeof(float)
                         + (wantSpans ? k * sizeof(TXchunkSpan) : 0);

    int    rsz = 0;
    void  *rbuf = msg_recv(finfo, &rsz);
    if (!rbuf || rsz < 0 || (size_t)rsz < total_bytes) return 0;

    const float *mem = (const float *)rbuf;
    if (out_vecs) {
        float *v = (float *)malloc(vecs_floats * sizeof(float));
        if (!v) return 0;
        memcpy(v, mem, vecs_floats * sizeof(float));
        *out_vecs = v;
    }
    if (out_avg) {
        float *a = (float *)malloc((size_t)dim * sizeof(float));
        if (!a) { if (out_vecs) { free(*out_vecs); *out_vecs = NULL; } return 0; }
        memcpy(a, mem + vecs_floats, (size_t)dim * sizeof(float));
        *out_avg = a;
    }
    if (out_spans) {
        TXchunkSpan *s = (TXchunkSpan *)malloc(k * sizeof(TXchunkSpan));
        if (!s) {
            if (out_vecs) { free(*out_vecs); *out_vecs = NULL; }
            if (out_avg)  { free(*out_avg);  *out_avg  = NULL; }
            return 0;
        }
        memcpy(s, (const char *)rbuf
                  + (vecs_floats + (size_t)dim) * sizeof(float),
               k * sizeof(TXchunkSpan));
        *out_spans = s;
    }
    if (out_k) *out_k = k;
    if (out_coh) *out_coh = coh;
    return (size_t)dim;
}

/* Helper-side chunk spans: 'S' tag.  Reply: span count k down the pipe
 * (0 = failure), then k TXchunkSpan structs in the mmap. */
static size_t child_chunk_spans_callback(void *ud,
                                         const char *text, size_t tlen,
                                         TXchunkSpan **out_spans)
{
    (void)ud;
    if (out_spans) *out_spans = NULL;
    if (!finfo || !out_spans) return 0;
    if (tlen == 0) return 0;

    /* tag + fixed header, THEN the payload (see msg_begin): a chunk
     * signal emitted before the header would be read as a tag. */
    char tag = 'S';
    if (forkwrite(&tag, 1) == -1) return 0;
    if (forkwrite((char *)&tlen, sizeof tlen) == -1) return 0;

    msg_begin(finfo);
    if (!msg_append(finfo, text, tlen)) return 0;
    if (msg_end(finfo) == -1) return 0;

    size_t k = 0;
    int    sz = 0;
    void  *sp;

    if (forkread(&k, sizeof k) == -1) return 0;
    sp = msg_recv(finfo, &sz);          /* always sent, empty when k == 0 */
    if (k == 0 || !sp) return 0;
    if (sz < 0 || (size_t)sz < k * sizeof(TXchunkSpan)) return 0;

    TXchunkSpan *spans = (TXchunkSpan *)malloc(k * sizeof(TXchunkSpan));
    if (!spans) return 0;
    memcpy(spans, sp, k * sizeof(TXchunkSpan));
    *out_spans = spans;
    return k;
}

/* Registered once by child when it learns embed is enabled — either
 * via 'O' from fork_open or 'V' from fork_exec.  Idempotent. */
static void setup_llamacpp_callback(void)
{
    TXregisterEmbedFunc(child_embed_callback, NULL);
    TXregisterEmbedDocFunc(child_embed_doc_callback, NULL);
    TXregisterChunkSpansFunc(child_chunk_spans_callback, NULL);
}

/* Parent-side helper: consume any 'B' (embed) / 'D' (doc embed) /
 * 'S' (chunk spans) callbacks, return when the helper sends 'A'
 * (= "real response payload follows").  Returns 0 on success, -1 on
 * pipe error. */
static int fork_drain_embed_callbacks(void)
{
    for (;;) {
        char tag = 0;
        /* != 1 (not == -1): EOF returns 0, and a dead helper must end
         * this loop -- otherwise the stale tag is re-dispatched forever
         * (the Ctrl-C "thousands of Broken pipe messages" spin). */
        if (forkread(&tag, 1) != 1) return -1;
        if (tag == 'B') {
            parent_service_embed();
            continue;
        }
        if (tag == 'D') {
            parent_service_embed_doc();
            continue;
        }
        if (tag == 'S') {
            parent_service_chunk_spans();
            continue;
        }
        if (tag == 'A') return 0;
        fprintf(stderr,
                "rampart-sql: protocol error, unknown tag '%c' (0x%02x)\n",
                tag, (unsigned char)tag);
        return -1;
    }
}

/* Parent-side: called by fork_exec when it sees 'B' on the wire.
 * Reads (tlen, text), embeds via main's rp_embed_text, writes
 * (veclen_bytes, vec).  On any failure writes veclen_bytes=0. */
/* Parent-side: called when the response-loop sees 'B'.  Reads tlen
 * from the pipe; text is already in mmap.  Tries the LRU first, then
 * embeds on miss.  Result goes into the (thread-local) mmap; size goes
 * down the pipe.  No locks held during the model call or pipe write. */
static void parent_service_embed(void)
{
    size_t tlen = 0, title_len = 0, fail = 0;
    int    kind = 0;
    size_t veclen_bytes = 0;
    char  *composed = NULL;

    if (forkread(&tlen, sizeof tlen) != (int)sizeof(tlen)) return;
    if (forkread(&kind, sizeof kind) != (int)sizeof(kind)) return;
    if (forkread(&title_len, sizeof title_len) != (int)sizeof(title_len)) return;
    int   insz = 0;
    void *inbuf = msg_recv(finfo, &insz);
    if (tlen == 0 || !inbuf || insz < 0 || (size_t)insz < tlen + title_len) {
        forkwrite((char *)&fail, sizeof fail);
        return;
    }
    const char *text  = (const char *)inbuf;
    const char *title = title_len ? text + tlen : NULL;

    /* Compose the model's retrieval prompt around the text FIRST (it
     * copies out of the mmap), so the LRU and the engine both key on
     * the exact bytes the model sees. */
    {
        size_t clen = 0;
        composed = embed_compose_text(kind, title, title_len, text, tlen, &clen);
        if (composed) { text = composed; tlen = clen; }
    }

    /* --- LRU lookup -----------------------------------------------
     * Skipped for TXEMBED_IMAGE: the key would be the image's PATH, so
     * a file edited in place would serve its old vector forever.  Images
     * are normally embedded once at insert time, so there is little to
     * cache anyway. */
    int    hit_dim = 0;
    float *hitvec  = NULL;
    if (kind != TXEMBED_IMAGE && g_lru.initialized && g_lru.capacity > 0) {
        uint64_t h = embed_lru_key(g_active_embed_handle, text, tlen);
        pthread_mutex_lock(&g_lru.mtx);
        embed_lru_node_t *n = embed_lru_find_locked(h, g_active_embed_handle, text, tlen);
        if (n) {
            hit_dim = n->dim;
            /* memcpy from cache → mmap while holding lock.  We read
             * n->vec (cache memory, not mmap) and write mmap.  The
             * write overwrites text, but we're done with text. */
            hitvec = (float *)malloc((size_t)n->dim * sizeof(float));
            if (hitvec)
                memcpy(hitvec, n->vec, (size_t)n->dim * sizeof(float));
            else
                hit_dim = 0;
            embed_lru_promote_locked(n);
        }
        pthread_mutex_unlock(&g_lru.mtx);
    }

    if (hit_dim > 0) {
        /* Test-only instrumentation: presence of /tmp/.embed_trace_on
         * enables hit/miss logging to /tmp/embed_trace.log.  Used by
         * smoke_embed_cache.js; harmless to leave in (one access() per
         * embed call; the file rarely exists in production). */
        if (access("/tmp/.embed_trace_on", F_OK) == 0) {
            FILE *f = fopen("/tmp/embed_trace.log", "a");
            if (f) { fputs("hit\n", f); fclose(f); }
        }
        veclen_bytes = (size_t)hit_dim * sizeof(float);
        forkwrite((char *)&veclen_bytes, sizeof veclen_bytes);
        msg_begin(finfo);
        msg_append(finfo, hitvec, veclen_bytes);
        msg_end(finfo);
        free(hitvec);
        free(composed);
        return;
    }

    /* --- MISS: embed (no lock) ----------------------------------- */
    if (access("/tmp/.embed_trace_on", F_OK) == 0) {
        FILE *f = fopen("/tmp/embed_trace.log", "a");
        if (f) { fputs("miss\n", f); fclose(f); }
    }
    float *vec = NULL;
    int    dim = 0;
    /* Use the model of the connection whose query we're servicing on this thread
     * (set by rp_sql_exec_query before fork_exec drove us here), so the forked
     * path is per-connection too -- not the old "first sql.set wins" global.
     * Dispatch by engine tag so onnxEmbed and llamaEmbed connections coexist. */
    if (g_active_embed_handle) {
        if (kind == TXEMBED_IMAGE) {
            /* image path -> image encoder; see main_embed_callback for why
             * a non-image engine must fail rather than embed the path. */
            if (g_active_embed_engine == EMBED_ENGINE_CLIP &&
                g_rp_clip_embed_image_path)
            {
                /* `text` may point into the mmap and is not guaranteed
                 * NUL-terminated there; embed_compose_text() has already
                 * copied it out when it composed, but for IMAGE nothing
                 * composes -- so make a bounded copy for the path ABI. */
                char *p = (char *)malloc(tlen + 1);
                if (p) {
                    memcpy(p, text, tlen);
                    p[tlen] = '\0';
                    dim = g_rp_clip_embed_image_path(g_active_embed_handle, p, &vec);
                    free(p);
                }
            } else {
                putmsg(MERR + UGE, "embed",
                       "embed(..., 'image') needs an image encoder, but this "
                       "connection's embed engine is %s -- use sql.set({clipEmbed:...})",
                       embed_engine_name(g_active_embed_engine));
                embed_fail_latch("embed(..., 'image') needs an image encoder, but "
                                 "this connection's embed engine is %s -- use "
                                 "sql.set({clipEmbed:...})",
                                 embed_engine_name(g_active_embed_engine));
            }
        } else
        switch (g_active_embed_engine) {
        case EMBED_ENGINE_LLAMACPP:
            if (g_rp_embed_text)
                dim = g_rp_embed_text(g_active_embed_handle, text, tlen, &vec);
            break;
        case EMBED_ENGINE_ONNX:
            if (g_rp_onnx_embed_text)
                dim = g_rp_onnx_embed_text(g_active_embed_handle, text, tlen, &vec);
            break;
        case EMBED_ENGINE_CLIP:
            if (g_rp_clip_embed_text)
                dim = g_rp_clip_embed_text(g_active_embed_handle, text, tlen, &vec);
            break;
        default: break;
        }
    }
    if (dim == 0 || !vec) {
        if (vec) free(vec);
        /* same rule as the in-process path: only a non-empty input that
         * produced nothing is a failure */
        if (tlen)
            embed_fail_latch("embed(): the %s embed model returned no vector",
                             embed_engine_name(g_active_embed_engine));
        forkwrite((char *)&fail, sizeof fail);
        free(composed);
        return;
    }

    /* Cache it before overwriting mmap (lru_put copies text + vec into
     * a fresh node; lock briefly).  Images are not cached -- see the
     * lookup side above. */
    if (kind != TXEMBED_IMAGE && g_lru.initialized && g_lru.capacity > 0) {
        uint64_t h = embed_lru_key(g_active_embed_handle, text, tlen);
        pthread_mutex_lock(&g_lru.mtx);
        embed_lru_put_locked(h, g_active_embed_handle, text, tlen, vec, dim);
        pthread_mutex_unlock(&g_lru.mtx);
    }

    veclen_bytes = (size_t)dim * sizeof(float);
    forkwrite((char *)&veclen_bytes, sizeof veclen_bytes);
    msg_begin(finfo);
    msg_append(finfo, vec, veclen_bytes);
    msg_end(finfo);
    free(vec);
    free(composed);
}

/* Parent-side 'D' (doc embed) service: reads tlen (text in mmap), runs
 * the doc embed requesting the FULL result (vecs + avg + coh + spans)
 * so the child can serve any of chunkembed/chunkavg/chunkcoherence from
 * one round-trip, and lays [vecs: k*dim][avg: dim][spans: k] in the
 * mmap.  Reply: int dim (0 = failure), size_t k, float coh.  The
 * parent's langtools doc cache means repeated 'D' for the same text
 * runs the model once. */
static void parent_service_embed_doc(void)
{
    size_t tlen = 0, plen = 0;
    int    faildim = 0;
    char   wantSpans = 0;

    if (forkread(&tlen, sizeof tlen) != (int)sizeof(tlen)) return;
    if (forkread(&plen, sizeof plen) != (int)sizeof(plen)) return;
    if (forkread(&wantSpans, 1) != 1) return;

    int   insz = 0;
    void *inbuf = msg_recv(finfo, &insz);
    if (tlen == 0 || !inbuf || insz < 0 || (size_t)insz < tlen + plen) {
        forkwrite((char *)&faildim, sizeof faildim);
        return;
    }
    const char *text = (const char *)inbuf;
    const char *pfx  = plen ? text + tlen : NULL;

    float *vecs = NULL, *avg = NULL, coh = 0.0f;
    TXchunkSpan *spans = NULL;
    size_t k = 0;
    size_t dim = main_embed_doc_callback(NULL, text, tlen, pfx, plen,
                                         &vecs, &k, &avg, &coh,
                                         wantSpans ? &spans : NULL);
    size_t vecs_floats = k * dim;
    if (dim == 0 || k == 0 || !vecs || !avg || (wantSpans && !spans)) {
        free(vecs); free(avg); free(spans);
        forkwrite((char *)&faildim, sizeof faildim);
        return;
    }

    int idim = (int)dim;
    forkwrite((char *)&idim, sizeof idim);
    forkwrite((char *)&k, sizeof k);
    forkwrite((char *)&coh, sizeof coh);

    /* [vecs][avg][spans] as one payload -- any size, chunked as needed */
    msg_begin(finfo);
    if (!msg_append(finfo, vecs, vecs_floats * sizeof(float)) ||
        !msg_append(finfo, avg, dim * sizeof(float)) ||
        (wantSpans && !msg_append(finfo, spans, k * sizeof(TXchunkSpan))))
    {
        free(vecs); free(avg); free(spans);
        return;
    }
    free(vecs); free(avg); free(spans);
    msg_end(finfo);
}

/* Parent-side 'S' (chunk spans) service: reads tlen (text in mmap),
 * computes the spans (tokenize + chunk, no model), replies k down the
 * pipe with k TXchunkSpan structs in the mmap.  k = 0 = failure. */
static void parent_service_chunk_spans(void)
{
    size_t tlen = 0, fail = 0;
    int    insz = 0;
    void  *text;

    if (forkread(&tlen, sizeof tlen) != (int)sizeof(tlen)) return;
    text = msg_recv(finfo, &insz);
    if (tlen == 0 || !text) {
        forkwrite((char *)&fail, sizeof fail);
        msg_begin(finfo); msg_end(finfo);       /* keep the shapes paired */
        return;
    }

    TXchunkSpan *spans = NULL;
    size_t k = main_chunk_spans_callback(NULL, (const char *)text, tlen, &spans);
    if (k == 0 || !spans) {
        if (spans) free(spans);
        forkwrite((char *)&fail, sizeof fail);
        msg_begin(finfo); msg_end(finfo);
        return;
    }
    forkwrite((char *)&k, sizeof k);
    msg_begin(finfo);
    if (!msg_append(finfo, spans, k * sizeof(TXchunkSpan))) { free(spans); return; }
    free(spans);
    msg_end(finfo);
}

// return 0 on pipe/fork error
// h->tx set otherwise.
static int fork_open(DB_HANDLE *h, const char *user, const char *pass)
{
    check_fork(h, Create);

    if(finfo->childpid)
    {
        /* 'O' = open + register embed callback (fall-through to 'o').
         * 'o' = plain open.  Mirror the flag set on parent's handle so
         * subsequent fork_exec's skip the lazy registration step. */
        char open_cmd = DB_HANDLE_IS(h, DB_FLAG_EMBED_ENABLED) ? 'O' : 'o';
        size_t dlen = strlen(h->db) + 1, ulen = strlen(user) + 1,
               plen = strlen(pass) + 1;

        if(forkwrite(&open_cmd, sizeof(char)) == -1)
            return 0;

        /* db\0user\0pass as a payload -- no length gate needed */
        msg_begin(finfo);
        if(!msg_append(finfo, h->db, dlen) ||
           !msg_append(finfo, user, ulen)  ||
           !msg_append(finfo, pass, plen))
            return 0;
        if(msg_end(finfo) == -1)
            return 0;

        if(forkread(&(h->tx), sizeof(TEXIS *)) == -1)
        {
            return 0;
        }
        if (open_cmd == 'O')
            DB_HANDLE_SET(h, DB_FLAG_EMBED_CHILD_REGISTERED);
    }

    return 1;
}

static int child_open()
{
    char *db, *user, *pass;
    TEXIS *tx=NULL;
    int sz=0;

    db = (char *)msg_recv(finfo, &sz);
    if(!db)
    {
        forkwrite(&tx, sizeof(TEXIS *));    /* NULL: open failed */
        return 0;
    }

    user = db + strlen(db) + 1;
    pass = user + strlen(user) + 1;

    tx = texis_open((char *)(db), user, pass);


    if(forkwrite(&tx, sizeof(TEXIS *)) == -1)
        return 0;

    return 1;
}

static int fork_setmem()
{
    if(finfo->childpid)
    {
        if(forkwrite("M", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&(finfo->mapfd), sizeof(int)) == -1)
            return 0;

    }
    else
        return 0;

    return 1;
}

static int child_setmem()
{
    struct stat st;

    if(forkread(&(finfo->mapfd), sizeof(int)) ==-1)
        return 0;

    /* The fd is the ONLY authority on the size: the parent's reservation
     * is best-effort, so asking the fd is the one way parent and child
     * cannot disagree (a mismatch would mean one side chunking at a
     * different boundary than the other). */
    if(fstat(finfo->mapfd, &st) == 0 && st.st_size > 0)
        finfo->mapsize = (size_t)st.st_size;
    else
        finfo->mapsize = FORKMAPSIZE;

    finfo->mapinfo->mem = mmap(NULL, finfo->mapsize, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->mapfd, 0);
    if(finfo->mapinfo->mem == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed: %s\n",strerror(errno));
        return 0;
    }
    finfo->mapinfo->pos = finfo->mapinfo->mem;

    return 1;
}


static int fork_seterr()
{
    if(finfo->childpid)
    {
        if(forkwrite("E", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&(finfo->errfd), sizeof(int)) == -1)
            return 0;

    }
    else
        return 0;

    return 1;
}

static int child_seterr()
{
    if(forkread(&(finfo->errfd), sizeof(int)) ==-1)
        return 0;

    finfo->errmap = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->errfd, 0);
    if(finfo->errmap == MAP_FAILED)
    {
        fprintf(stderr, "errmsg mmap failed: %s\n",strerror(errno));
        exit(1);
    }

    mmsgfh = fmemopen(finfo->errmap, msgbufsz, "w+");

    return 1;
}

static int fork_close(DB_HANDLE *h)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("c", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    h->tx = NULL;

    return ret;
}

static int child_close()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        tx=TEXIS_CLOSE(tx);
        ret=1;
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_flush(DB_HANDLE *h)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("F", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS*)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_flush()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS*))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret = texis_flush(tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("g", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    if(ret)
    {
        memcpy(countInfo, finfo->mapinfo->mem, sizeof(TXCOUNTINFO));
    }

    return ret;
}

static int child_getCountInfo()
{
    int ret=0;
    TEXIS *tx=NULL;
    TXCOUNTINFO *countInfo = finfo->mapinfo->mem;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret = texis_getCountInfo(tx, countInfo);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_skip(DB_HANDLE *h, int nrows)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("s", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkwrite(&nrows, sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_skip()
{
    int ret=0, nrows=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if (forkread(&nrows, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
        ret = texis_flush_scroll(tx, nrows);
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_resetparams(DB_HANDLE *h)
{
    check_fork(h, NoCreate);
    int ret=1;

    if(!finfo)
        return 0;

    if(forkwrite("r", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_resetparams()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret=texis_resetparams(tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_sql_set(duk_context *ctx, DB_HANDLE *h, char *errbuf)
{
    check_fork(h, Create);
    duk_size_t sz;
    int size;
    void *p;

    int ret=0;

    /* Same likevCache scoping as sql_set: the peek below re-arms it if
     * this (merged) settings object carries the key. */
    g_doccache_cap_pending = -1;

    if(!finfo)
        return 0;

    /* If llamaEmbed is in the settings, load the model in main BEFORE
     * shipping; the helper only knows how to register the wire callback. */
    const char *embed_path = NULL;
    int wants_embed = peek_llamaembed_setting(ctx, &embed_path);
    if (wants_embed) {
        char eerr[256] = {0};
        if (setup_llamacpp_main(ctx, embed_path, eerr, sizeof eerr) != 0) {
            snprintf(errbuf, msgbufsz, "%s", eerr);
            return -1;
        }
    }

    /* Same drill for onnxEmbed.  Only one backend per set() call:
     * llamaEmbed wins if both are present (matches h_set). */
    const char *onnx_model = NULL;
    rp_onnx_embed_opts onnx_opts;
    int wants_onnx = wants_embed ? 0 : peek_onnxembed_setting(ctx, &onnx_model, &onnx_opts);
    if (wants_onnx) {
        char eerr[256] = {0};
        if (setup_onnx_main(ctx, onnx_model, &onnx_opts, eerr, sizeof eerr) != 0) {
            snprintf(errbuf, msgbufsz, "%s", eerr);
            return -1;
        }
    }

    /* llamaCache lives in main only (parent_service_embed is where the
     * LRU lookup happens).  Apply here in the parent. */
    int cache_cap = 0;
    if (peek_llamacache_setting(ctx, &cache_cap)) {
        if (cache_cap < 0) cache_cap = 0;
        if (embed_lru_set_capacity((size_t)cache_cap) != 0) {
            snprintf(errbuf, msgbufsz,
                     "sql.set: llamaCache: failed to allocate cache");
            return -1;
        }
    }

    /* llamaEmbedPerThread also lives in main (rp_embed_text checks it).
     * Apply in parent so the helper never wastes time forwarding. */
    int per_thread = 1;
    if (peek_llamaembed_perthread_setting(ctx, &per_thread)) {
        if (g_rp_embed_set_per_thread)
            g_rp_embed_set_per_thread(per_thread);
    }

    /* likevCache: size the shared per-model doc cache.  The parent owns the
     * cache (it services the child's 'D' doc-embed requests), and the
     * fork path never runs sql_set's property loop -- so peek it here and
     * arm the thread-local.  Our caller applies it after we return --
     * h_set to THIS connection's h->embed_handle, or rp_texis_set (the
     * sql.set path) to the engine named in the same merged settings;
     * applying to the g_last_loaded_* thread-locals HERE would risk
     * capping a DIFFERENT connection's model when likevCache is set
     * without an embed key. */
    int likev_cap = -1;
    if (peek_likevcache_setting(ctx, &likev_cap)) {
        if (likev_cap < 0) likev_cap = 0;
        g_doccache_cap_pending = likev_cap;
    }

    duk_cbor_encode(ctx, -1, 0);
    p=duk_get_buffer_data(ctx, -1, &sz);

    if(forkwrite("S", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    /* The CBOR of the caller's settings object was memcpy'd into the map
     * with NO bounds check -- sql.set({anything: <a big string>}) wrote
     * straight past the mapping.  As a payload it is both bounded and
     * unlimited: any size, chunked as needed. */
    msg_begin(finfo);
    if(!msg_append(finfo, p, (size_t)sz))
        return 0;
    if(msg_end(finfo) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    /* The helper always answers with a payload: the CBOR result when
     * ret > 0, the error text when ret < 0, empty otherwise. */
    {
        void *rbuf = msg_recv(finfo, &size);

        if(ret > 0)
        {
            if(!rbuf) return 0;
            duk_push_external_buffer(ctx);
            duk_config_buffer(ctx, -1, rbuf, (duk_size_t)size);
            duk_cbor_decode(ctx, -1, 0);
        }
        else if (ret < 0 && rbuf)
        {
            strncpy(errbuf, (char *)rbuf, msgbufsz);
            errbuf[msgbufsz-1] = '\0';
        }
    }

    if (ret >= 0 && (wants_embed || wants_onnx)) {
        DB_HANDLE_SET(h, DB_FLAG_EMBED_ENABLED);
        DB_HANDLE_CLEAR(h, DB_FLAG_EMBED_CHILD_REGISTERED);
    }

    return ret;
}

static int child_set()
{
    int ret=0, bufsz=0;
    RPTHR *thr=get_current_thread();
    duk_context *ctx=thr->ctx;
    TEXIS *tx=NULL;
    char errbuf[msgbufsz];
    void *cbor=NULL;
    duk_size_t cborsz=0;
    void *inbuf;

    errbuf[0]='\0';

    if (forkread(&tx, sizeof(TEXIS*))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    /* the caller's settings object, CBOR encoded -- any size */
    inbuf = msg_recv(finfo, &bufsz);
    if (!inbuf)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        /* get js object needed for sql_set to do its stuff */
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, inbuf, (duk_size_t)bufsz);
        duk_cbor_decode(ctx, -1, 0);

        /* do the set in this child */
        ret = sql_set(ctx, tx, errbuf);

        /* ret > 0: JS data to send back, CBOR encoded */
        if (ret > 0)
        {
            duk_cbor_encode(ctx, -1, 0);
            cbor = duk_get_buffer_data(ctx, -1, &cborsz);
        }
    }
    else
        ret=-1;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    /* Always answer with a payload so the parent has one shape to read:
     * the CBOR result (ret > 0), the error text (ret < 0), or nothing. */
    msg_begin(finfo);
    if (ret > 0 && cbor)
    {
        if(!msg_append(finfo, cbor, (size_t)cborsz)) return 0;
    }
    else if (ret < 0)
    {
        if(!msg_append(finfo, errbuf, strlen(errbuf) + 1)) return 0;
    }
    if (msg_end(finfo) == -1)
        return 0;

    return 1; // close will always happen
}

/* in child process, loop and await commands */
static void do_child_loop(SFI *finfo)
{
    while(1)
    {
        char command='\0';
        int ret;// = kill(parent_pid,0);

        //if( ret )
        //    exit(0);

        ret = forkread(&command, sizeof(char));
        if (ret == 0)
        {
            /* read of 0 on a pipe = write end closed. Parent
               either died or shut us down. No recovery.       */
            exit(0);
        }

        /* this is in fork read now
        else if (ret == -1)
            exit(0);
        */

        clearmsgbuf(); // reset the position of the errmap buffer.

        switch(command)
        {
            case 'O':
                /* open + register embed callback */
                setup_llamacpp_callback();
                /* fall through */
            case 'o':
                ret = child_open();
                break;
            case 'V':
                /* lazy embed-callback registration (sql.set arrived
                 * after the connection was opened with 'o') */
                setup_llamacpp_callback();
                {
                    char ack = 'K';  /* arbitrary ack byte; not in tag stream */
                    forkwrite(&ack, 1);
                }
                break;
            case 'c':
                ret = child_close();
                break;
            case 'p':
                ret = child_prep();
                break;
            case 'e':
                ret = child_exec();
                break;
            case 'f':
                ret = 1;
                child_fetch();
                break;
            case 'r':
                ret = child_resetparams();
                break;
            case 'P':
                ret = child_param();
                break;
            case 'F':
                ret = child_flush();
                break;
            case 's':
                ret = child_skip();
                break;
            case 'g':
                ret = child_getCountInfo();
                break;
            case 'S':
                ret = child_set();
                break;
            case 'C':
                ret = child_create();
                break;
            case 'M':
                ret = child_setmem();
                break;
            case 'E':
                ret = child_seterr();
                break;
            case 'X':
                exit(0);
        }
        // do something with ret?
    }
}

static int h_create(const char *db)
{
    RPTHR *thr = get_current_thread();

    if(RPTHR_TEST(thr, RPTHR_FLAG_THR_SAFE))
        return createdb(db);
    return fork_create(db);
}

static int h_end_transaction(DB_HANDLE *h)
{
    mark_handle_available(h);
    return 1;
}

/* Peek the settings object at top-of-stack for the llamaembed key
 * (keys are lowercased by sql_normalize_prop, so use lowercase here).
 * Returns 1 if present, 0 otherwise; sets *path on success. */
static int peek_llamaembed_setting(duk_context *ctx, const char **path)
{
    int found = 0;
    if (duk_get_prop_string(ctx, -1, "llamaembed")) {
        if (duk_is_string(ctx, -1)) {
            found = 1;
            if (path) *path = duk_get_string(ctx, -1);
        }
        else if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) &&
                 !duk_is_function(ctx, -1)) {
            /* object form: { model: '/path.gguf', queryPrompt: ...,
             * documentPrompt: ..., documentTitlePrompt: ..., prompts: false }
             * -- the prompt keys are read by resolve_embed_prompts(). */
            if (duk_get_prop_string(ctx, -1, "model") && duk_is_string(ctx, -1)) {
                found = 1;
                if (path) *path = duk_get_string(ctx, -1);
            }
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);
    return found;
}

/* Likewise for llamaCache.  Returns 1 if present, sets *cap. */
static int peek_llamacache_setting(duk_context *ctx, int *cap)
{
    int found = 0;
    if (duk_get_prop_string(ctx, -1, "llamacache")) {
        if (duk_is_number(ctx, -1)) {
            found = 1; if (cap) *cap = duk_get_int(ctx, -1);
        } else if (duk_is_string(ctx, -1)) {
            found = 1; if (cap) *cap = atoi(duk_get_string(ctx, -1));
        }
    }
    duk_pop(ctx);
    return found;
}

/* likevCache / likevCacheSize: doc-result cache capacity.  Stored keys are
 * lowercased, so match both spellings in lowercase.  Returns 1 if present. */
static int peek_likevcache_setting(duk_context *ctx, int *cap)
{
    int found = 0;
    const char *keys[2] = { "likevcache", "likevcachesize" };
    for (int i = 0; i < 2 && !found; i++) {
        if (duk_get_prop_string(ctx, -1, keys[i])) {
            if (duk_is_number(ctx, -1)) {
                found = 1; if (cap) *cap = duk_get_int(ctx, -1);
            } else if (duk_is_string(ctx, -1)) {
                found = 1; if (cap) *cap = atoi(duk_get_string(ctx, -1));
            }
        }
        duk_pop(ctx);
    }
    return found;
}

/* And llamaEmbedPerThread (bool).  Returns 1 if present, sets *on. */
static int peek_llamaembed_perthread_setting(duk_context *ctx, int *on)
{
    int found = 0;
    if (duk_get_prop_string(ctx, -1, "llamaembedperthread")) {
        if (duk_is_boolean(ctx, -1)) {
            found = 1; if (on) *on = duk_get_boolean(ctx, -1) ? 1 : 0;
        } else if (duk_is_number(ctx, -1)) {
            found = 1; if (on) *on = duk_get_int(ctx, -1) ? 1 : 0;
        }
    }
    duk_pop(ctx);
    return found;
}

/* Parse the onnxEmbed object at top-of-stack settings.  Fills *out with
 * pointers borrowed from the settings object (caller must keep the
 * settings alive until rp_onnx_embed_load returns; string owners are
 * strdup'd inside rp_onnx_embed_load itself).  Returns 1 if the setting
 * is present and valid, 0 otherwise; *model_path_out points at the
 * borrowed model path.
 *
 * As of the 2026-07 rampart-onnx rework, `model` is normally a model
 * directory (HuggingFace/sentence-transformers layout: `onnx/model.onnx`
 * + `tokenizer.json` or `vocab.txt` + optional `1_Pooling/config.json`).
 * In that "directory mode" the tokenizer, bos/eos, id_offset, pooling
 * (if present) and normalize are auto-discovered -- only `pooling`,
 * `maxTokens`, and `passagePrefix` are honored as overrides.
 *
 * "Legacy file mode" is still supported: pass a bare `.onnx` for
 * `model` and a `*vocab.txt` (WordPiece) or directory containing
 * `tokenizer.json` (SentencePiece/BPE) for `tokenizer`, and set
 * `bosId` / `eosId` / `idOffset` / `pooling` yourself.
 *
 * Accepted shape:
 *   sql.set({onnxEmbed:{
 *       model:         '/home/u/.rampart/models/embed/bge-m3',  // required
 *       tokenizer:     '/path/vocab.txt' | '/dir/with/tokenizer.json',  // optional (file mode)
 *       // overrides honored in BOTH modes:
 *       pooling:       'auto' | 'mean' | 'cls',
 *       maxTokens:     512,
 *       passagePrefix: 'passage: ',
 *       // overrides honored ONLY in file mode (ignored in directory mode):
 *       bosId: 0, eosId: 2, idOffset: 0, padId: 1, normalize: true,
 *       queryPrefix: 'query: ',    // unused on the SQL path (SQL is passage-side)
 *   }})
 */
static int peek_onnxembed_setting(duk_context *ctx,
                                  const char **model_path_out,
                                  rp_onnx_embed_opts *out)
{
    int found = 0;
    if (!duk_get_prop_string(ctx, -1, "onnxembed")) {
        duk_pop(ctx);
        return 0;
    }
    if (!duk_is_object(ctx, -1) || duk_is_array(ctx, -1)) {
        duk_pop(ctx);
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->abi_version = RP_ONNX_EMBED_ABI;
    out->bos_id      = 0;
    out->eos_id      = 2;
    out->id_offset   = 0;
    out->pad_id      = 1;
    out->max_tokens  = 0;    /* 0 => use model's n_ctx */
    out->pooling     = 0;    /* auto */
    out->normalize   = 1;    /* default true */

    /* model is required.  tokenizer is optional (only used in file mode). */
    const char *model = NULL, *tok = NULL;
    if (duk_get_prop_string(ctx, -1, "model") && duk_is_string(ctx, -1))
        model = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, -1, "tokenizer") && duk_is_string(ctx, -1))
        tok = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (!model || !model[0]) {
        duk_pop(ctx);
        return 0;
    }
    /* Empty string tokenizer -> NULL (i.e., pure directory mode). */
    out->tokenizer_path = (tok && tok[0]) ? tok : NULL;
    *model_path_out     = model;
    found = 1;

    /* Optional numeric knobs.  The inner object's keys are stored
     * verbatim (sql_normalize_prop only lowercases the OUTER
     * `onnxembed` key), so we look up the exact camelCase names the
     * user typed -- e.g. `bosId`, not `bosid`.  This matches the JS
     * `initEmbed` opts surface documented in rampart-onnx.md. */
    #define _NUM(prop, field) do {                                           \
        if (duk_get_prop_string(ctx, -1, prop) && duk_is_number(ctx, -1))    \
            out->field = duk_get_int(ctx, -1);                               \
        duk_pop(ctx);                                                        \
    } while (0)
    _NUM("bosId",      bos_id);
    _NUM("eosId",      eos_id);
    _NUM("idOffset",   id_offset);
    _NUM("padId",      pad_id);
    _NUM("maxTokens",  max_tokens);
    _NUM("maxChunkBatch", max_chunk_batch);   /* chunks per model run (default 64);
                                               * lower = smaller GPU memory peak */
    #undef _NUM

    /* pooling: string 'auto'|'mean'|'cls'. */
    if (duk_get_prop_string(ctx, -1, "pooling") && duk_is_string(ctx, -1)) {
        const char *s = duk_get_string(ctx, -1);
        if      (!strcasecmp(s, "mean")) out->pooling = 1;
        else if (!strcasecmp(s, "cls"))  out->pooling = 2;
        else                             out->pooling = 0; /* auto */
    }
    duk_pop(ctx);

    /* normalize: boolean. */
    if (duk_get_prop_string(ctx, -1, "normalize")) {
        if (duk_is_boolean(ctx, -1))
            out->normalize = duk_get_boolean(ctx, -1) ? 1 : 0;
        else if (duk_is_number(ctx, -1))
            out->normalize = duk_get_int(ctx, -1) ? 1 : 0;
    }
    duk_pop(ctx);

    /* Optional prefix strings.  camelCase per above. */
    if (duk_get_prop_string(ctx, -1, "queryPrefix") && duk_is_string(ctx, -1))
        out->query_prefix = duk_get_string(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, -1, "passagePrefix") && duk_is_string(ctx, -1))
        out->passage_prefix = duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* Chunker options, mirroring initEmbed: split 'auto'|'window',
     * minTokens (fragment floor; -1 disables merging), packParagraphs,
     * sentenceSplit (sentence-pack oversized pieces).  NOTE for chunked
     * tables: these change chunk boundaries -- build and any HEADERLESS
     * serving must agree (header-bearing values carry their own spans). */
    if (duk_get_prop_string(ctx, -1, "split") && duk_is_string(ctx, -1)) {
        if (!strcasecmp(duk_get_string(ctx, -1), "window"))
            out->split_mode = 1;
    }
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, -1, "minTokens") && duk_is_number(ctx, -1))
        out->min_split_tokens = duk_get_int(ctx, -1);
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, -1, "packParagraphs"))
        out->pack_paragraphs = duk_to_boolean(ctx, -1) ? 1 : 0;
    duk_pop(ctx);
    if (duk_get_prop_string(ctx, -1, "sentenceSplit"))
        out->sentence_split = duk_to_boolean(ctx, -1) ? 1 : 0;
    duk_pop(ctx);

    duk_pop(ctx);   /* the onnxembed object itself */
    return found;
}

/* ---- Retrieval prompt resolution ------------------------------------
 *
 * Asymmetric embed models (nomic, bge, e5, ...) need a prompt prefixed
 * to queries and/or documents.  rampart-models.js writes them as a
 * sidecar next to downloaded model files; resolve_embed_prompts() reads
 * that ONCE when a model is bound to a connection and the strings live
 * in memory from then on (composition per embed call is a strcat --
 * never file I/O on the query path). */

static void clear_last_loaded_prompts(void)
{
    free(g_last_loaded_prompt_query);     g_last_loaded_prompt_query = NULL;
    free(g_last_loaded_prompt_document);  g_last_loaded_prompt_document = NULL;
    free(g_last_loaded_prompt_doc_title); g_last_loaded_prompt_doc_title = NULL;
}

static duk_ret_t prompts_json_decode_raw(duk_context *ctx, void *udata)
{
    (void)udata;
    duk_json_decode(ctx, -1);
    return 1;
}

/* Parse {"prompts":{query, document|passage, documentWithTitle}} into the
 * g_last_loaded_prompt_* trio (first non-empty value per slot wins; the
 * same format as the .prompts.json sidecar AND HuggingFace's
 * config_sentence_transformers.json).  Returns 1 if any prompt loaded. */
static int parse_prompts_json(duk_context *ctx, const char *json, size_t jlen)
{
    int found = 0;
    static const struct { const char *key; int slot; } K[] = {
        { "query", 0 }, { "document", 1 }, { "passage", 1 },
        { "documentWithTitle", 2 }
    };
    char **slots[3] = { &g_last_loaded_prompt_query,
                        &g_last_loaded_prompt_document,
                        &g_last_loaded_prompt_doc_title };

    duk_push_lstring(ctx, json, jlen);
    if (duk_safe_call(ctx, prompts_json_decode_raw, NULL, 1, 1) != DUK_EXEC_SUCCESS ||
        !duk_is_object(ctx, -1)) {
        duk_pop(ctx);
        return 0;
    }
    if (duk_get_prop_string(ctx, -1, "prompts") && duk_is_object(ctx, -1)) {
        for (size_t i = 0; i < sizeof(K) / sizeof(K[0]); i++) {
            if (duk_get_prop_string(ctx, -1, K[i].key) && duk_is_string(ctx, -1)) {
                const char *v = duk_get_string(ctx, -1);
                if (v && v[0] && !*slots[K[i].slot]) {
                    *slots[K[i].slot] = strdup(v);
                    found = 1;
                }
            }
            duk_pop(ctx);
        }
    }
    duk_pop_2(ctx);   /* prompts (or non-object), decoded root */
    return found;
}

/* Read + parse one prompts file.  Returns 1 if prompts were loaded. */
static int load_prompts_file(duk_context *ctx, const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t got;
    int found = 0;

    if (!f) return 0;
    buf = malloc(65536);
    if (!buf) { fclose(f); return 0; }
    got = fread(buf, 1, 65535, f);
    fclose(f);
    if (got > 0) found = parse_prompts_json(ctx, buf, got);
    free(buf);
    return found;
}

/* Locate the model's prompts on disk:
 *   1. <model_path>.prompts.json                      (rampart-models sidecar)
 *   2. model dir:  <dir>/config_sentence_transformers.json   (HF standard)
 *   3. model file: same, in its directory, then its parent (onnx/model.onnx
 *      layouts keep the config one level up). */
static void load_prompt_sidecar(duk_context *ctx, const char *model_path)
{
    char pbuf[PATH_MAX];
    struct stat st;

    if (!model_path || !model_path[0]) return;
    if (snprintf(pbuf, sizeof pbuf, "%s.prompts.json", model_path) >= (int)sizeof pbuf)
        return;
    if (load_prompts_file(ctx, pbuf)) return;

    if (stat(model_path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (snprintf(pbuf, sizeof pbuf, "%s/config_sentence_transformers.json",
                     model_path) < (int)sizeof pbuf)
            load_prompts_file(ctx, pbuf);
        return;
    }
    /* plain file: its dir, then the parent dir */
    snprintf(pbuf, sizeof pbuf, "%s", model_path);
    for (int up = 0; up < 2; up++) {
        char *slash = strrchr(pbuf, '/');
        if (!slash || slash == pbuf) return;
        *slash = '\0';
        char cbuf[PATH_MAX];
        if (snprintf(cbuf, sizeof cbuf, "%s/config_sentence_transformers.json",
                     pbuf) >= (int)sizeof cbuf)
            return;
        if (load_prompts_file(ctx, cbuf)) return;
    }
}

/* Fill g_last_loaded_prompt_* for the embed model being bound.  Expects
 * the (lowercased-key) settings object at stack top.  Explicit keys in
 * the engine's object win WHOLESALE over the sidecar:
 *   queryPrompt / documentPrompt / documentTitlePrompt  (both engines)
 *   prompts: false                          (disable, embed verbatim)
 *   queryPrefix / passagePrefix              (onnx legacy, engine-side:
 *                                            their presence skips the
 *                                            sidecar so prompts never
 *                                            stack on top of them) */
static void resolve_embed_prompts(duk_context *ctx, const char *engine_key,
                                  const char *model_path)
{
    int explicit_keys = 0;

    clear_last_loaded_prompts();
    if (duk_get_prop_string(ctx, -1, engine_key) &&
        duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) &&
        !duk_is_function(ctx, -1)) {
        if (duk_get_prop_string(ctx, -1, "prompts") &&
            duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1)) {
            duk_pop_2(ctx);   /* prompts, engine object */
            return;           /* prompts:false -- embed verbatim */
        }
        duk_pop(ctx);

        static const char *pkeys[3] =
            { "queryPrompt", "documentPrompt", "documentTitlePrompt" };
        char **slots[3] = { &g_last_loaded_prompt_query,
                            &g_last_loaded_prompt_document,
                            &g_last_loaded_prompt_doc_title };
        for (int i = 0; i < 3; i++) {
            if (duk_get_prop_string(ctx, -1, pkeys[i]) && duk_is_string(ctx, -1)) {
                const char *v = duk_get_string(ctx, -1);
                explicit_keys = 1;
                if (v && v[0]) *slots[i] = strdup(v);
            }
            duk_pop(ctx);
        }
        if (!explicit_keys) {
            static const char *ekeys[2] = { "queryPrefix", "passagePrefix" };
            for (int i = 0; i < 2 && !explicit_keys; i++) {
                if (duk_get_prop_string(ctx, -1, ekeys[i]) &&
                    duk_is_string(ctx, -1) && duk_get_string(ctx, -1)[0])
                    explicit_keys = 1;
                duk_pop(ctx);
            }
        }
    }
    duk_pop(ctx);   /* engine key value (or undefined) */

    if (!explicit_keys)
        load_prompt_sidecar(ctx, model_path);
}

/* Transfer the just-resolved prompts to the connection's handle (the
 * same handoff moment as h->embed_handle = g_last_loaded_*_handle). */
static void attach_prompts_to_handle(DB_HANDLE *h)
{
    free(h->embed_prompt_query);
    free(h->embed_prompt_document);
    free(h->embed_prompt_doc_title);
    h->embed_prompt_query     = g_last_loaded_prompt_query;
    h->embed_prompt_document  = g_last_loaded_prompt_document;
    h->embed_prompt_doc_title = g_last_loaded_prompt_doc_title;
    g_last_loaded_prompt_query = NULL;
    g_last_loaded_prompt_document = NULL;
    g_last_loaded_prompt_doc_title = NULL;
}

/* Apply the pending likevCache size (if any) to `handle` — the actual
 * per-connection embed handle.  Called wherever h->embed_handle is
 * established (h_set + the per-exec lazy-load), because that handle isn't
 * necessarily the one setup_*_main loaded (the lazy-load can produce a
 * distinct handle for the same model).  Cap == 0 disables; the setter is
 * idempotent, so calling this per-connection is cheap. */
static void apply_pending_doccache_cap(void *handle, embed_engine_t engine)
{
    if (g_doccache_cap_pending < 0 || !handle) return;
    size_t cap = (size_t)g_doccache_cap_pending;
    if (engine == EMBED_ENGINE_ONNX && g_rp_onnx_embed_set_cache_cap)
        g_rp_onnx_embed_set_cache_cap(handle, cap);
    else if (engine == EMBED_ENGINE_LLAMACPP && g_rp_embed_set_cache_cap)
        g_rp_embed_set_cache_cap(handle, cap);
}

static int h_set(duk_context *ctx, DB_HANDLE *h, char *errbuf)
{
    const char *embed_path = NULL;
    int wants_embed = peek_llamaembed_setting(ctx, &embed_path);

    /* Only one embed backend per sql.set() call.  If both llamaEmbed and
     * onnxEmbed appear in the same object, llamaEmbed wins for backwards
     * compatibility; if only onnxEmbed appears we route through it. */
    const char *onnx_model = NULL;
    rp_onnx_embed_opts onnx_opts;
    int wants_onnx = wants_embed ? 0 : peek_onnxembed_setting(ctx, &onnx_model, &onnx_opts);

    /* clipEmbed is third in the same precedence chain. */
    const char *clip_model = NULL;
    int wants_clip = (wants_embed || wants_onnx)
                   ? 0 : peek_clipembed_setting(ctx, &clip_model);

    /* Resolve the model's retrieval prompts NOW, while the settings object
     * is at stack top (fork_sql_set / sql_set may leave a response there).
     * CLIP participates too: its text tower can carry a query prompt
     * (zero-shot CLIP conventionally wraps queries as "a photo of a ..."). */
    if (wants_embed)
        resolve_embed_prompts(ctx, "llamaembed", embed_path);
    else if (wants_onnx)
        resolve_embed_prompts(ctx, "onnxembed", onnx_model);
    else if (wants_clip)
        resolve_embed_prompts(ctx, "clipembed", clip_model);

    if (wants_embed && DB_HANDLE_IS(h, DB_FLAG_FORK)) {
        /* Forked path: load model in main before shipping settings.
         * setup_llamacpp_main is idempotent (cached by path). */
        char eerr[256] = {0};
        if (setup_llamacpp_main(ctx, embed_path, eerr, sizeof eerr) != 0) {
            snprintf(errbuf, msgbufsz, "%s", eerr);
            return -1;
        }
    }
    if (wants_onnx && DB_HANDLE_IS(h, DB_FLAG_FORK)) {
        char eerr[256] = {0};
        if (setup_onnx_main(ctx, onnx_model, &onnx_opts, eerr, sizeof eerr) != 0) {
            snprintf(errbuf, msgbufsz, "%s", eerr);
            return -1;
        }
    }
    if (wants_clip && DB_HANDLE_IS(h, DB_FLAG_FORK)) {
        char eerr[256] = {0};
        if (setup_clip_main(ctx, clip_model, eerr, sizeof eerr) != 0) {
            snprintf(errbuf, msgbufsz, "%s", eerr);
            return -1;
        }
    }

    int ret;
    if(DB_HANDLE_IS(h,DB_FLAG_FORK))
        ret = fork_sql_set(ctx, h, errbuf);
    else
        ret = sql_set(ctx, h->tx, errbuf);

    if (ret >= 0 && (wants_embed || wants_onnx || wants_clip)) {
        DB_HANDLE_SET(h, DB_FLAG_EMBED_ENABLED);
        /* Attach the just-loaded model to THIS connection's handle (the model
         * is process-global / refcounted-by-path; we only store the pointer).
         * setup_*_main ran for this thread on both the fork path (above)
         * and the non-fork path (inside sql_set), so g_last_loaded_*_handle
         * holds this connection's model. */
        if (wants_embed) {
            h->embed_handle = g_last_loaded_embed_handle;
            h->embed_engine = EMBED_ENGINE_LLAMACPP;
        } else if (wants_onnx) {
            h->embed_handle = g_last_loaded_onnx_handle;
            h->embed_engine = EMBED_ENGINE_ONNX;
        } else {
            h->embed_handle = g_last_loaded_clip_handle;
            h->embed_engine = EMBED_ENGINE_CLIP;
        }
        attach_prompts_to_handle(h);
        apply_pending_doccache_cap(h->embed_handle, h->embed_engine);
        /* Clear CHILD_REGISTERED so the next fork_exec triggers 'V'
         * even if the connection was opened with 'o'. */
        DB_HANDLE_CLEAR(h, DB_FLAG_EMBED_CHILD_REGISTERED);
    }
    else if (ret >= 0 && h->embed_handle) {
        /* sql.set({likevCache:N}) without an embed key, on a connection
         * whose model is already bound: apply to that model now.  (The
         * per-connection h->embed_handle is authoritative -- NOT the
         * g_last_loaded_* thread-locals, which may point at a different
         * connection's model.) */
        apply_pending_doccache_cap(h->embed_handle, h->embed_engine);
    }

    return ret;
}

static int h_flush(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h,DB_FLAG_FORK))
        return fork_flush(h);
    return TEXIS_FLUSH(h->tx);
}

static int h_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_getCountInfo(h, countInfo);
    return TEXIS_GETCOUNTINFO(h->tx, countInfo);
}

static int h_resetparams(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_resetparams(h);
    return TEXIS_RESETPARAMS(h->tx);
}

static int h_param(DB_HANDLE *h, int pn, void *d, long *dl, int t, int st)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_param(h, pn, d, dl, t, st);
    return TEXIS_PARAM(h->tx, pn, d, dl, t, st);
}

static int h_skip(DB_HANDLE *h, int n)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_skip(h,n);
    return TEXIS_SKIP(h->tx, n);
}

static int h_prep(DB_HANDLE *h, char *sql)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_prep(h, sql);
    return TEXIS_PREP(h->tx, sql);
}

static int h_close(DB_HANDLE *h)
{
    int ret=1;

    if(!h) {
        return 1;
    }

    /* Flush any vec indexes mutated through this handle's DDIC, while
     * the DDIC is still live.  In auto-flush mode this is a no-op (no
     * dirty handles).  Skip for forked-helper connections — the helper
     * child runs its own h_close path and flushes there. */
    if(!DB_HANDLE_IS(h, DB_FLAG_FORK) && h->tx) {
        extern DDIC *texis_getddic(TEXIS *tx);
        DDIC *ddic = texis_getddic(h->tx);
        if (ddic) TXvecFlushAll(ddic);
    }

    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        ret=fork_close(h);
    else
        h->tx = TEXIS_CLOSE(h->tx);

    h=free_handle(h);

    return ret;
}

static int h_exec(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_exec(h);
    return TEXIS_EXEC(h->tx);
}

static FLDLST *h_fetch(DB_HANDLE *h,  int stringsFrom)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_fetch(h, stringsFrom);
    return TEXIS_FETCH(h->tx, stringsFrom);
}


/* **************************************************
   Sql.prototype.close
   ************************************************** */
static duk_ret_t rp_sql_close(duk_context *ctx)
{
    // Since handles are cached, hard to know what to do here
    // We will just close the first unused handle with same db
    // in order to free up some resources
    const char *user="PUBLIC",
               *pass="",
               *db=NULL;

    DB_HANDLE *h=NULL;

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        RP_THROW(ctx, "no database has been opened");
    }

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    db = duk_get_string(ctx, -1);
    h=find_available_handle(db, user, pass, 0);
    h_close(h);

    return 0;
}



/* defined below, after db_misc.c is included; used there to check the
 * search expressions given to Sql.sandr() */
static int expr_compiles(const char *expr);

/* Texis reports *why* an expression is bad through putmsg(), which
 * rp_msg_init() has pointed at this thread's capture buffer.  The
 * connection methods read that buffer through finfo->errmap; the
 * standalone functions in db_misc.c (Sql.rex(), Sql.rexFile(),
 * Sql.sandr(), ...) have no connection, so they read the thread buffer
 * directly.  Without this the caller only learns *that* an expression
 * failed to compile, and has to reproduce it in the `rex' CLI to find
 * out what was wrong with it.
 */
static void rp_msgbuf_reset(void)
{
    if(mmsgfh == NULL)
        rp_msg_init();
    if(mmsgfh)
    {
        fflush(mmsgfh);
        fseek(mmsgfh, 0, SEEK_SET);
    }
    if(rp_errmap)
        memset(rp_errmap, 0, msgbufsz);
}

/* Text captured since the last rp_msgbuf_reset(), or NULL if texis had
 * nothing to say.  Trailing newlines are trimmed: the text is going into
 * a one-line Error message. */
static char *rp_msgbuf_text(void)
{
    long pos;

    if(mmsgfh == NULL || rp_errmap == NULL)
        return NULL;
    fflush(mmsgfh);
    pos = ftell(mmsgfh);
    if(pos <= 0)
        return NULL;
    if(pos > msgbufsz - 1)
        pos = msgbufsz - 1;
    rp_errmap[pos] = '\0';
    while(pos > 0 && (rp_errmap[pos-1] == '\n' || rp_errmap[pos-1] == '\r'))
        rp_errmap[--pos] = '\0';
    return(*rp_errmap ? rp_errmap : NULL);
}

#include "db_misc.c" /* copied and altered thunderstone code for stringformat and abstract */

/* **************************************************
    initialize query struct
   ************************************************** */
static void rp_init_qstruct(QUERY_STRUCT *q)
{
    q->sql = (char *)NULL;
    q->arr_idx = -1;
    q->str_idx = -1;
    q->obj_idx=-1;
    q->arg_idx=-1;
    q->callback = -1;
    q->skip = 0;
    q->max = -432100000; //-1 means unlimit, -0.4321 billion means not set.
    q->rettype = -1;
    q->flags = 0;
    q->err = QS_SUCCESS;
}

/* **************************************************
   get up to 4 parameters in any order.
   object=settings, string=sql,
   array=params to sql, function=callback
   example:
   sql.exec(
     "select * from SYSTABLES where NAME=?",
     ["mytable"],
     {max:1,skip:0,returnType:"array:},
     function (row) {console.log(row);}
   );
   //except maybe 6 params: see https://rampart.dev/docs/rampart-sql.html#exec under "Caveats for Options, maxRows and skipRows"
   ************************************************** */
/* TODO: leave stack as you found it */

static QUERY_STRUCT rp_get_query(duk_context *ctx)
{
    duk_idx_t i = 0;
    int gotsettings=0, maxset=0, selectmax=-432100000;
    QUERY_STRUCT q_st;
    QUERY_STRUCT *q = &q_st;

    rp_init_qstruct(q);

    for (i = 0; i < 6; i++)
    {
        int vtype = duk_get_type(ctx, i);
        switch (vtype)
        {
            case DUK_TYPE_NUMBER:
            {
                if(maxset==1)
                    q->skip=duk_get_int(ctx, i);
                else if(!maxset)
                    q->max=duk_get_int(ctx, i);
                else
                    RP_THROW(ctx, "too many Numbers in parameters to sql.exec()");

                maxset++;
                break;
            }
            case DUK_TYPE_STRING:
            {
                //int l;
                if (q->sql != (char *)NULL)
                {
                    RP_THROW(ctx, "Only one string may be passed as a parameter and must be a sql statement.\n");
                    //duk_push_int(ctx, -1);
                    //q->sql = (char *)NULL;
                    //q->err = QS_ERROR_PARAM;
                    //return (q_st);
                }
                q->sql = duk_get_string(ctx, i);
                q->str_idx=i;
                /*  Done in parse_sql_params now -- ajf 2025-07-27
                l = strlen(q->sql) - 1;
                while (*(q->sql + l) == ' ' && l > 0)
                    l--;
                if (*(q->sql + l) != ';')
                {
                    duk_dup(ctx, i);
                    duk_push_string(ctx, ";");
                    duk_concat(ctx, 2);
                    duk_replace(ctx, i);
                    q->sql = (char *)duk_get_string(ctx, i);
                }
                */
                /* it hasn't been set yet. we don't want to overwrite returnRows or returnType */
                if(q->rettype == -1)
                {
                    if(strncasecmp(q->sql, "select", 6))
                        q->rettype=2;
                    else
                        q->rettype=0;
                }

                /* selectMaxRows from this */
                if(!strncasecmp(q->sql, "select", 6))
                {
                    duk_push_this(ctx);
                    duk_get_prop_string(ctx, -1, "selectMaxRows");
                    selectmax=duk_get_int_default(ctx, -1, RESMAX_DEFAULT);
                    duk_pop_2(ctx);
                }
                break;
            }
            case DUK_TYPE_OBJECT:
            {
                /* array of parameters*/

                if (duk_is_array(ctx, i) && q->arr_idx == -1)
                    q->arr_idx = i;

                /* argument is a function, save where it is on the stack */
                else if (duk_is_function(ctx, i))
                {
                    q->callback = i;
                }

                /* object of settings or parameters*/
                else
                {

                    /* the first object with these properties is our settings object */
                    if(!gotsettings)
                    {
                        if (duk_get_prop_string(ctx, i, "includeCounts"))
                        {
                            if (REQUIRE_BOOL(ctx, -1, "sql: includeCounts must be a Boolean"))
                                q->flags |= QFLAG_GETCOUNTS;
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "rawVectors"))
                        {
                            if (REQUIRE_BOOL(ctx, -1, "sql: rawVectors must be a Boolean"))
                                q->flags |= QFLAG_RAWVEC;
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "argument"))
                        {
                            q->arg_idx = duk_get_top_index(ctx);
                            gotsettings=1;
                        }
                        /* leave it on the stack for use in callback */
                        else
                        {
                            duk_pop(ctx);
                            /* alternative */
                            if (duk_get_prop_string(ctx, i, "arg"))
                            {
                                q->arg_idx = duk_get_top_index(ctx);
                                gotsettings=1;
                            }
                            /* leave it on the stack for use in callback */
                            else
                                duk_pop(ctx);
                        }

                        if (duk_get_prop_string(ctx, i, "skipRows"))
                        {
                            q->skip = REQUIRE_INT(ctx, -1, "skipRows must be a Number");
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "maxRows"))
                        {
                            q->max = REQUIRE_INT(ctx, -1, "sql: maxRows must be a Number");
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "returnRows"))
                        {
                            if (REQUIRE_BOOL(ctx, -1, "sql: returnRows must be a Boolean"))
                                q->rettype = 0;
                            else
                                q->rettype = 2;
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "returnType"))
                        {
                            const char *rt = REQUIRE_STRING(ctx, -1, "sql: returnType must be a String");

                            if (!strcasecmp("array", rt))
                            {
                                q->rettype = 1;
                            }
                            else if (!strcasecmp("novars", rt))
                            {
                                q->rettype = 2;
                            }
                            else if (!strcasecmp("object", rt))
                                q->rettype=0;
                            else
                                RP_THROW(ctx, "sql: returnType '%s' is not valid", rt);
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if(gotsettings)
                            break;
                    }

                    if ( q->arr_idx == -1 && q->obj_idx == -1)
                    {
                        // we have ?named parameters
                        q->obj_idx = i;
                        break;
                    }
                }
                break;
            } /* case */
        } /* switch */
    }     /* for */

    /* if qmax is not set and we are in a select, set to this.selectMaxRows, or RESMAX_DEFAULT */
    if( q->max == -432100000 && selectmax != -432100000)
        q->max = selectmax;

    if (q->max < 0)
        q->max = INT64_MAX;

    if (q->skip < 0)
        q->skip = 0;

    if (q->sql == (char *)NULL)
    {
        //q->err = QS_ERROR_PARAM;
        RP_THROW(ctx, "sql - No sql statement present.\n");
    }

    // we are only doing objects now -ajf 2025-07-27
    if(q->arr_idx >-1 && q->obj_idx==-1)
    {
        duk_idx_t idx=q->arr_idx;
        duk_uarridx_t i=0, len=duk_get_length(ctx, idx);

        duk_push_object(ctx);
        for (; i<len; i++) {
            duk_get_prop_index(ctx, idx, i);
            duk_put_prop_index(ctx, -2, i);
        }
        duk_replace(ctx, idx);
        q->arr_idx=-1;
        q->obj_idx=idx;
    }
    return (q_st);
}

/* ------------------------------------------------------------------
 * Sql.list(arr) — explicit wrapper marking a JS array as a SQL list
 * (i.e. for `WHERE col IN (?)`).  Returns an opaque object carrying
 * the array under DUK_HIDDEN_SYMBOL("sql_list") and the inferred
 * element type ("number" or "string") under DUK_HIDDEN_SYMBOL("sql_list_type").
 * Hidden symbols are not settable from JS, so the bind path can
 * trust that any object carrying them came from this constructor.
 *
 * Validation happens here (non-empty, homogeneous, no NaN/Infinity)
 * so the bind path is decision-free.
 *
 * Numbers are emitted as SQL_DOUBLE arrays (texis promotes to the
 * column type at compare time).  Strings are emitted as SQL_STRLST
 * (single ft_strlst parameter); texis 7+ promotes a varchar LHS to a
 * single-element strlst for `varchcol IN (strlst_param)` semantics
 * with inMode=subset (rampart default).
 *
 * Values outside JS-safe-integer range have already lost precision in
 * JS; pass an array of strings if you need exact >2^53 integers.
 */
static duk_ret_t rp_sql_list_create(duk_context *ctx)
{
    duk_uarridx_t i, len;
    int firsttype, type;
    const char *type_str;

    if (!duk_is_array(ctx, 0))
        RP_THROW(ctx, "Sql.list: argument must be an array");

    len = (duk_uarridx_t)duk_get_length(ctx, 0);
    if (len == 0)
        RP_THROW(ctx, "Sql.list: array must not be empty");

    duk_get_prop_index(ctx, 0, 0);
    firsttype = rp_gettype(ctx, -1);
    duk_pop(ctx);

    if (firsttype != RP_TYPE_NUMBER && firsttype != RP_TYPE_STRING)
        RP_THROW(ctx, "Sql.list: array elements must be all numbers or all strings");

    for (i = 0; i < len; i++)
    {
        duk_get_prop_index(ctx, 0, i);
        type = rp_gettype(ctx, -1);
        if (type != firsttype)
        {
            duk_pop(ctx);
            RP_THROW(ctx, "Sql.list: array must be homogeneous; element at index %u differs from first", (unsigned)i);
        }
        if (firsttype == RP_TYPE_NUMBER)
        {
            double d = duk_get_number(ctx, -1);
            if (isnan(d) || isinf(d))
            {
                duk_pop(ctx);
                RP_THROW(ctx, "Sql.list: array contains NaN/Infinity at index %u", (unsigned)i);
            }
        }
        duk_pop(ctx);
    }

    type_str = (firsttype == RP_TYPE_NUMBER) ? "number" : "string";

    /* wrapper: { [HIDDEN("sql_list")]: arr, [HIDDEN("sql_list_type")]: type_str } */
    duk_push_object(ctx);
    duk_dup(ctx, 0);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sql_list"));
    duk_push_string(ctx, type_str);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sql_list_type"));

    return 1;
}

/* Build an ft_strlst from a JS array of strings sitting at stack
 * index `arr_idx`.  Returns malloc'd buffer (caller frees) and
 * writes total byte size to *out_size.  Caller has already verified
 * homogeneity / non-empty.
 */
static void *build_strlst_from_jsarray(duk_context *ctx, duk_idx_t arr_idx, size_t *out_size)
{
    duk_uarridx_t i, len;
    size_t payload_sz, total_sz;
    ft_strlst *p;
    char *q;
    byte byteUsed[256];
    int j;

    len = (duk_uarridx_t)duk_get_length(ctx, arr_idx);

    /* First pass: compute payload size = sum(strlen(s)+1) + 1 terminator */
    payload_sz = 1; /* the final terminator nul */
    for (i = 0; i < len; i++)
    {
        duk_size_t slen;
        duk_get_prop_index(ctx, arr_idx, i);
        (void)duk_get_lstring(ctx, -1, &slen);
        payload_sz += (size_t)slen + 1;
        duk_pop(ctx);
    }

    total_sz = (size_t)TX_STRLST_MINSZ + payload_sz;
    if (total_sz < sizeof(ft_strlst))
        total_sz = sizeof(ft_strlst);

    p = NULL;
    REMALLOC(p, total_sz);
    memset(p, 0, total_sz);
    p->nb = payload_sz;

    /* Copy strings + nuls into buf */
    q = p->buf;
    memset(byteUsed, 0, sizeof(byteUsed));
    for (i = 0; i < len; i++)
    {
        duk_size_t slen;
        const char *s;
        duk_get_prop_index(ctx, arr_idx, i);
        s = duk_get_lstring(ctx, -1, &slen);
        if (slen > 0)
        {
            memcpy(q, s, slen);
            for (j = 0; j < (int)slen; j++)
                byteUsed[(byte)s[j]] = 1;
            q += slen;
        }
        *q++ = '\0';
        duk_pop(ctx);
    }
    *q = '\0'; /* terminator */

    /* Pick a delimiter byte not used in any string (display only) */
    p->delim = '\0';
    for (j = 0; j < 256; j++)
    {
        if (!byteUsed[(byte)TxPrefStrlstDelims[j]])
        {
            p->delim = TxPrefStrlstDelims[j];
            break;
        }
    }

    *out_size = total_sz;
    return (void *)p;
}

/* If the value at the top of the stack is a Sql.list wrapper, alloc
 * the appropriate buffer and fill out (*in,*out,*olen).  Returns
 * the buffer on success (caller must free), or NULL when the value
 * is not a Sql.list wrapper.
 */
static void *check_sql_list(duk_context *ctx, long *olen, int *in, int *out)
{
    duk_uarridx_t i, len;
    const char *type_str;
    duk_idx_t arr_idx;

    *olen = 0;

    if (!duk_is_object(ctx, -1) || duk_is_array(ctx, -1) || duk_is_function(ctx, -1))
        return NULL;

    if (!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_list")))
    {
        duk_pop(ctx);
        return NULL;
    }
    /* stack: ..., wrapper, arr */
    arr_idx = duk_get_top_index(ctx);

    if (!duk_get_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sql_list_type")))
    {
        duk_pop_2(ctx);
        return NULL;
    }
    type_str = duk_get_string(ctx, -1);
    duk_pop(ctx); /* pop type_str */
    /* stack: ..., wrapper, arr */

    if (type_str && !strcmp(type_str, "number"))
    {
        double *dret = NULL;
        len = (duk_uarridx_t)duk_get_length(ctx, arr_idx);
        REMALLOC(dret, sizeof(double) * (size_t)len);
        for (i = 0; i < len; i++)
        {
            duk_get_prop_index(ctx, arr_idx, i);
            dret[i] = duk_get_number(ctx, -1);
            duk_pop(ctx);
        }
        duk_pop(ctx); /* pop arr */
        *in   = SQL_C_DOUBLE;
        *out  = SQL_DOUBLE;
        *olen = (long)len * (long)sizeof(double);
        return (void *)dret;
    }
    else if (type_str && !strcmp(type_str, "string"))
    {
        size_t total_sz = 0;
        void *buf = build_strlst_from_jsarray(ctx, arr_idx, &total_sz);
        duk_pop(ctx); /* pop arr */
        *in   = SQL_C_STRLST;
        *out  = SQL_STRLST;
        *olen = (long)total_sz;
        return buf;
    }

    duk_pop(ctx); /* pop arr; unknown type — fall back to JSON path */
    return NULL;
}

/* ------------------------------------------------------------------
 * DEPRECATED: bare-array auto-detection of homogeneous numeric arrays.
 * The replacement is Sql.list(arr) (above), which is explicit, supports
 * strings via strlst, and is forgery-proof via hidden symbols.
 * Bare-array support is preserved for backward compatibility and will
 * be removed in a future release.
 *
 * Differences from Sql.list:
 *  - Splits int64 vs double based on whether all values are integral
 *    and within INT64 range.  Sql.list always uses double — JS source
 *    is a double regardless, and texis promotes to the column type.
 *  - Only handles number arrays.  Sql.list handles string arrays too.
 *  - Silently does nothing for non-number / non-homogeneous arrays
 *    (caller falls through to JSON stringification).  Sql.list throws
 *    a clear error instead.
 */
void *check_array_params(duk_context *ctx, long *olen, int *in, int *out)
{
    void *ret=NULL;

    *olen=0;

    if(!duk_is_array(ctx, -1))
        return ret;

    duk_uarridx_t i=1, len=duk_get_length(ctx, -1);

    duk_get_prop_index(ctx, -1, 0);
    int type, firsttype = rp_gettype(ctx, -1);
    duk_pop(ctx);

    for(;i<len;i++)
    {
        duk_get_prop_index(ctx, -1, i);
        type = rp_gettype(ctx, -1);
        duk_pop(ctx);
        if(type != firsttype)
            return ret;
    }
    int dofloat=0;
    if(firsttype == RP_TYPE_NUMBER)
    {
        for(i=0;i<len;i++)
        {
            double d, floord;
            duk_get_prop_index(ctx, -1, i);
            d = duk_get_number(ctx, -1);
            duk_pop(ctx);
            floord = floor(d);
            if( (d - floord) > 0.0 || (d - floord) < 0.0 ||
                floord < (double)INT64_MIN || floord > (double)INT64_MAX
            )
            {
                dofloat=1;
                break;
            }
        }
        if(dofloat)
        {
            double *dret=NULL;
            REMALLOC(dret, sizeof(double) * (size_t) len);
            for(i=0;i<len;i++)
            {
                duk_get_prop_index(ctx, -1, i);
                dret[i] = duk_get_number(ctx, -1);
                duk_pop(ctx);
            }
            ret=(void*)dret;
            *in = SQL_C_DOUBLE;
            *out = SQL_DOUBLE;
            *olen = (long)len * (long)sizeof(double);
        }
        else
        {
            int64_t *iret=NULL;
            REMALLOC(iret, sizeof(int64_t) * (size_t) len);
            for(i=0;i<len;i++)
            {
                duk_get_prop_index(ctx, -1, i);
                iret[i] = (int64_t)duk_get_number(ctx, -1);
                duk_pop(ctx);
            }
            ret=(void*)iret;
            *in = SQL_C_SBIGINT;
            *out = SQL_BIGINT;
            *olen = (long)len * (long)sizeof(double);
        }
    }

    return ret;
}

void *check_for_vector_type(duk_context *ctx, long *olen, int *in, int *out)
{
    rp_vec_type type = rp_vec_unknown;
    int vartype = rp_gettype(ctx, -1);
    duk_size_t sz;
    void *v;

    if(vartype != RP_TYPE_VECTOR)
        return NULL;

    if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("rpvec")))
    {
        duk_pop(ctx);
        return NULL;
    }
    v=duk_get_buffer_data(ctx, -1, &sz);
    duk_pop(ctx);
    if(!v)
        return v;

    if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("vectype")))
    {
        duk_pop(ctx);
        return NULL;
    }
    type = duk_get_int(ctx, -1);
    duk_pop(ctx);


    switch(type)
    {
        case rp_vec_f64:  *in=SQL_VEC_F64;  break;
        case rp_vec_f32:  *in=SQL_VEC_F32;  break;
        case rp_vec_f16:  *in=SQL_VEC_F16;  break;
        case rp_vec_bf16: *in=SQL_VEC_BF16; break;
        case rp_vec_i8:   *in=SQL_VEC_I8;   break;
        case rp_vec_u8:   *in=SQL_VEC_U8;   break;
        default:     	                return NULL;
    }
    *out = *in;
    *olen = (long) sz;
    return v;
}

#define push_sql_param do{\
    switch (duk_get_type(ctx, -1))\
    {\
        /* JS null -> empty varchar.  Historically the DEFAULT branch\
           below coerced null to the string "null" via duk_to_string,\
           which surprised callers expecting SQL-shaped semantics.  Two\
           options considered: (a) bind with plen=SQL_NULL_DATA so\
           texis's prepare.c takes its NULL path, and (b) bind as an\
           empty string.  (a) requires texis-side plumbing to actually\
           mark the FLD as NULL (prepare.c currently does not) and\
           patching that broke parameter-FLD reuse across prepared-stmt\
           re-executions.  (b) works entirely inside rampart-sql:\
           TXsqlFunc_embed's `slen==0 -> TXfldSetNull` short-circuit\
           returns SQL NULL for `select embed(?)`, and rp_pushfield\
           already reads an empty varchar column back to JS null.  So\
           null round-trips as null through embed() and through varchar\
           column INSERT/SELECT with zero texis-core edits. */\
        case DUK_TYPE_NULL:\
        {\
            v = (char *)"";\
            plen = 0;\
            in = SQL_C_CHAR;\
            out = SQL_VARCHAR;\
            break;\
        }\
        case DUK_TYPE_NUMBER:\
        {\
            double floord;\
            d = duk_get_number(ctx, -1);\
            floord = floor(d);\
            if( (d - floord) > 0.0 || (d - floord) < 0.0 || \
                floord < (double)INT64_MIN || floord > (double)INT64_MAX)\
            {\
                v = (double *)&d;\
                plen = sizeof(double);\
                in = SQL_C_DOUBLE;\
                out = SQL_DOUBLE;\
            }\
            else\
            {\
                lval = (int64_t) floord;\
                v = (int64_t *)&lval;\
                plen = sizeof(int64_t);\
                in = SQL_C_SBIGINT;\
                out = SQL_BIGINT;\
            }\
            break;\
        }\
        /* all objects are converted to json string\
           this works (or will work) for several datatypes (varchar,int(x),strlst,json varchar) */\
        case DUK_TYPE_OBJECT:\
        {\
            /* Sql.list(arr) — explicit list parameter (preferred). */\
            vfree=v=check_sql_list(ctx, &plen, &in, &out);\
            if(v) break;\
            /* DEPRECATED: bare array auto-detection. Use Sql.list(arr). */\
            vfree=v=check_array_params(ctx, &plen, &in, &out);\
            if(v) break;\
            v=check_for_vector_type(ctx, &plen, &in, &out);\
            if(v) break;\
            char *e;\
            char *r = str_rp_to_json_safe(ctx, -1, NULL, 0);\
            duk_push_string(ctx, r);\
            duk_replace(ctx,-2);\
            free(r);\
            char *s = v = (char*)duk_get_string(ctx, -1);\
            plen = strlen(v);\
            e = s + plen - 1;\
            /* a date (and presumably other single values returned from an object which returns a string)\
             will end up in quotes upon conversion, we need to remove them */\
            if (*s == '"' && *e == '"' && plen > 1)\
            {\
                /* duk functions return const char* */\
                v = s + 1;\
                plen -= 2;\
            }\
            in = SQL_C_CHAR;\
            out = SQL_VARCHAR;\
            break;\
        }\
        /* insert binary data from a buffer */\
        case DUK_TYPE_BUFFER:\
        {\
            duk_size_t sz;\
            v = duk_get_buffer_data(ctx, -1, &sz);\
            plen = (long)sz;\
            in = SQL_BINARY;\
            out = SQL_BINARY;\
            break;\
        }\
        /* default for strings and booleans/undefined (converted to\
           true/false and "undefined"); null has its own case above\
           (bound as empty varchar).  Duktape hands astral-plane\
           characters to C as CESU-8 surrogate pairs -- invalid UTF-8\
           that would be stored verbatim and mangled by fulltext term\
           folding; recombine to standard UTF-8 at the boundary (the\
           object/JSON case above already does, via\
           str_rp_to_json_safe) */\
        default:\
        {\
            size_t ulen;\
            char *u;\
            v = (char *)duk_to_string(ctx, -1);\
            plen = strlen(v);\
            u = duk_rp_cesu8_to_utf8((const char *)v, (size_t)plen, &ulen);\
            if (u)\
            {\
                vfree = v = u;\
                plen = (long)ulen;\
            }\
            in = SQL_C_CHAR;\
            out = SQL_VARCHAR;\
        }\
    }\
} while(0)

#define LIKEP_MOD_CHAR '_'
static int rp_add_named_parameters(
    duk_context *ctx,
    DB_HANDLE *h,
    duk_idx_t obj_loc,
    char **namedSqlParams,
    int nParams
)
{
    int rc=0, i=0;

    for(i=0;i<nParams;i++)
    {
        char *key = namedSqlParams[i];
        void *v;   /* value to be passed to db */
        long plen; /* lenght of value */
        double d;  /* for numbers */
        int64_t lval;
        int in, out;

        duk_get_prop_string(ctx, obj_loc, key);

        //duk_dup(ctx, -1);
        //printf("pushing param '%s'\n", duk_safe_to_string(ctx, -1));
        //duk_pop(ctx);

        if(!duk_is_undefined(ctx, -1))
        {
            void *vfree=NULL;
            push_sql_param;

            /* texis_params is indexed starting at 1 */
            rc = h_param(h, i+1, v, &plen, in, out);
            if(vfree)
                free(vfree);
            if (!rc)
            {
                duk_pop(ctx);
                return 0;
            }
        }
        else
        {
            /* sql.set({paramChk:false}) maps to
               TXsetDiscardUnsetParameterClauses(true) (setprop.c:1049), which
               tells texis to DROP the clause whose parameter was never
               supplied -- the documented "give one complex query, supply only
               the parameters whose clauses should take effect" behaviour
               (sql-set.rst paramChk).  Leaving the parameter unbound here is
               precisely what lets texis do that.  Erroring unconditionally
               made the setting unreachable from rampart JS, because this check
               runs long before texis ever sees the statement.

               Skipping is positionally safe: h_param() above indexes by i+1
               from the loop counter, not by a running count of bound
               parameters, so an unbound slot does not shift the others. */
            if(TXgetDiscardUnsetParameterClauses())
            {
                duk_pop(ctx);
                continue;
            }

            /* TODO: get rid of this and the rest of the LIKEP_PARAM_SUBSTITUTIONS stuff.
            if(*key==LIKEP_MOD_CHAR &&
                 (
                    isdigit(*(key+1)) ||
                    ( *(key+1)==LIKEP_MOD_CHAR && isdigit(*(key+2)) )
                 )
              )
                snprintf(finfo->errmap, msgbufsz-1, "internal error processing likep parameter");
            else
            */
                snprintf(finfo->errmap, msgbufsz-1, "parameter '%s' not found in Object.", key);

            duk_pop(ctx);
            return 0;
        }
        duk_pop(ctx);

    }
    return 1;
}

#define pushcounts do{\
    duk_push_object(ctx);\
    duk_push_number(ctx,(double)cinfo.indexCount );\
    duk_put_prop_string(ctx,-2,"indexCount");\
    duk_push_number(ctx,(double)cinfo.rowsMatchedMin );\
    duk_put_prop_string(ctx,-2,"rowsMatchedMin");\
    duk_push_number(ctx,(double)cinfo.rowsMatchedMax );\
    duk_put_prop_string(ctx,-2,"rowsMatchedMax");\
    duk_push_number(ctx,(double)cinfo.rowsReturnedMin );\
    duk_put_prop_string(ctx,-2,"rowsReturnedMin");\
    duk_push_number(ctx,(double)cinfo.rowsReturnedMax );\
    duk_put_prop_string(ctx,-2,"rowsReturnedMax");\
}while(0);


/* **************************************************
  push a single field from a row of the sql results
   ************************************************** */
/* chunkembed() values may carry a self-describing header (vecvalue.h):
 * strip it for the JS-facing cells and surface its spans (when present)
 * as a .chunkSpans property on the rampart.vector object. */
static void *rp_vec_col_cells(void *data, size_t *cells, size_t elsz,
                              const EPI_UINT32 **spans, size_t *nspans)
{
    TXvecValInfo vvi;

    *spans = NULL;
    *nspans = 0;
    if (data && TXvecValDecode(data, *cells * elsz, elsz, &vvi))
    {
        *cells -= vvi.hdrBytes / elsz;
        if (vvi.spans) { *spans = vvi.spans; *nspans = vvi.k; }
        return (void *)vvi.cells;
    }
    return data;
}

static void rp_vec_attach_spans(duk_context *ctx, const EPI_UINT32 *spans,
                                size_t k)
{
    size_t i;

    if (!spans) return;
    duk_push_array(ctx);
    for (i = 0; i < k; i++)
    {
        /* memcpy, don't dereference: the span array starts at header
           byte 24, and the field data itself is only aligned to the
           column's element size (2 for the default f16), so a direct
           EPI_UINT32 load can be unaligned -- UB, and a fault on
           strict-alignment targets.  TXvecValDecode reads its own
           header fields the same way. */
        EPI_UINT32 s, e;

        memcpy(&s, &spans[i * 2],     sizeof(s));
        memcpy(&e, &spans[i * 2 + 1], sizeof(e));
        duk_push_object(ctx);
        duk_push_uint(ctx, (duk_uint_t)s);
        duk_put_prop_string(ctx, -2, "start");
        duk_push_uint(ctx, (duk_uint_t)e);
        duk_put_prop_string(ctx, -2, "end");
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    duk_put_prop_string(ctx, -2, "chunkSpans");
}

static void rp_pushfield(duk_context *ctx, FLDLST *fl, int i, int rawvec)
{
    char type = fl->type[i] & 0x3f;

    if( !fl->data[i]  || !fl->ndata[i])
    {
        duk_push_null(ctx);
        return;
    }

    switch (type)
    {
    case FTN_CHAR:
    {
        char *v=fl->data[i];
        /* SECURITY (F5): the typed-value-in-varchar encoding reads v[1] and,
           for some subtypes, more bytes; require ndata>=2 before touching v[1]
           so a corrupt 1-byte field can't be over-read, and so the ndata-2
           length below cannot underflow the size_t. */
        if(fl->ndata[i] >= 2 && v[0]=='\xff' && v[1] > '\xf9' )
        {
            switch(v[1])
            {
                case '\xff':
                    /* field text is foreign bytes: sanitize so no JS
                       string op can throw on invalid sequences */
                    duk_rp_push_lstring_safe(ctx, v+2, (duk_size_t)fl->ndata[i] -2);
                    break;
                case '\xfe':
                    /* F5: reads the double at byte offset 8 -> needs 16 bytes */
                    if(fl->ndata[i] >= 2 * sizeof(double))
                        duk_push_number(ctx, ((double*)v)[1]);
                    else
                        duk_push_null(ctx);
                    break;
                case '\xfd':
                    duk_push_true(ctx);
                    break;
                case '\xfc':
                    duk_push_false(ctx);
                    break;
                case '\xfb':
                    duk_push_null(ctx);
                    break;
                case '\xfa':
                    duk_rp_push_lstring_safe(ctx, v+2, strlen(v+2));
                    duk_json_decode(ctx, -1);
                    break;
                default:
                    duk_rp_push_lstring_safe(ctx, v, strlen(v));
                    break;
            }

            break;
        }
        //else fallthrough
    }
    case FTN_INDIRECT:
    {
        duk_size_t  sz = (duk_size_t) strlen(fl->data[i]);

        if(sz > fl->ndata[i])
            sz = fl->ndata[i];
        duk_rp_push_lstring_safe(ctx, (char *)fl->data[i], sz );
        break;
    }
    case FTN_STRLST:
    {
        ft_strlst *p = (ft_strlst *)fl->data[i];
        char *s = p->buf;
        size_t l = strlen(s);
        char *end = s + (p->nb);
        int j = 0;

        duk_push_array(ctx);
        while (s < end)
        {
            duk_rp_push_lstring_safe(ctx, s, l);
            duk_put_prop_index(ctx, -2, j++);
            s += l;
            while (s < end && *s == '\0')
                s++;
            l = strlen(s);
        }
        break;
    }
    case FTN_INT:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_int *)fl->data[i]));
        break;
    }
    /*        push_number with (duk_t_double) cast,
              53bits of double is the best you can
              do for exact whole numbers in javascript anyway */
    case FTN_INT64:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_int64 *)fl->data[i]));
        break;
    }
    case FTN_UINT64:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_uint64 *)fl->data[i]));
        break;
    }
    case FTN_INTEGER:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_integer *)fl->data[i]));
        break;
    }
    case FTN_LONG:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_long *)fl->data[i]));
        break;
    }
    case FTN_SMALLINT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_smallint *)fl->data[i]));
        break;
    }
    case FTN_SHORT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_short *)fl->data[i]));
        break;
    }
    case FTN_DWORD:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_dword *)fl->data[i]));
        break;
    }
    case FTN_WORD:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_word *)fl->data[i]));
        break;
    }
    case FTN_DOUBLE:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_double *)fl->data[i]));
        break;
    }
    case FTN_FLOAT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_float *)fl->data[i]));
        break;
    }
    case FTN_DATE:
    {
        /* equiv to js "new Date(seconds*1000)" */
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) * ((ft_date *)fl->data[i]));
        duk_new(ctx, 1);
        break;
    }
    case FTN_UDATE:
    {
        /* udate is microseconds; a JS Date holds integral MILLISECONDS,
         * so this is lossy by design -- no warning, no rounding-up.
         * floor() rather than C truncation so that pre-1970 (negative)
         * values round toward the past like modern ones do; ECMAScript
         * TimeClip truncates toward zero, which would split the
         * behaviour either side of the epoch.
         * For exact microseconds use convert(col,'int64').
         */
        ft_udate us = *((ft_udate *)fl->data[i]);
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, floor((duk_double_t)us / 1000.0));
        duk_new(ctx, 1);
        break;
    }
    case FTN_COUNTER:
    {
        char s[33];
        //void *v=NULL;
        ft_counter *acounter = fl->data[i];

        //duk_push_object(ctx);
        snprintf(s, 33, "%lx%lx", acounter->date, acounter->seq);
        duk_push_string(ctx, s);
        /*
        duk_put_prop_string(ctx, -2, "counterString");

        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) acounter->date);
        duk_new(ctx, 1);
        duk_put_prop_string(ctx, -2, "counterDate");

        duk_push_number(ctx, (duk_double_t) acounter->seq);
        duk_put_prop_string(ctx, -2, "counterSequence");

        */

        break;
    }
    case FTN_VEC_F64:
    {
        double *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(double), &spans_, &nspans_);

        p = (double *) duk_push_fixed_buffer(ctx, cells_ * sizeof(double));
        memcpy(p, src_, cells_ * sizeof(double));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_f64, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_VEC_F32:
    {
        float *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(float), &spans_, &nspans_);

        p = (float *) duk_push_fixed_buffer(ctx, cells_ * sizeof(float));
        memcpy(p, src_, cells_ * sizeof(float));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_f32, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_VEC_F16:
    {
        uint16_t *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(uint16_t), &spans_, &nspans_);

        p = (uint16_t *) duk_push_fixed_buffer(ctx, cells_ * sizeof(uint16_t));
        memcpy(p, src_, cells_ * sizeof(uint16_t));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_f16, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_VEC_BF16:
    {
        uint16_t *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(uint16_t), &spans_, &nspans_);

        p = (uint16_t *) duk_push_fixed_buffer(ctx, cells_ * sizeof(uint16_t));
        memcpy(p, src_, cells_ * sizeof(uint16_t));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_bf16, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_VEC_I8:
    {
        int8_t *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(int8_t), &spans_, &nspans_);

        p = (int8_t *) duk_push_fixed_buffer(ctx, cells_ * sizeof(int8_t));
        memcpy(p, src_, cells_ * sizeof(int8_t));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_i8, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_VEC_U8:
    {
        uint8_t *p;
        size_t cells_ = (size_t)fl->ndata[i], nspans_;
        const EPI_UINT32 *spans_;
        void *src_ = rp_vec_col_cells(fl->data[i], &cells_,
                                      sizeof(uint8_t), &spans_, &nspans_);

        p = (uint8_t *) duk_push_fixed_buffer(ctx, cells_ * sizeof(uint8_t));
        memcpy(p, src_, cells_ * sizeof(uint8_t));
        if(!rawvec)
        {
            rp_push_new_vector(ctx, rp_vec_u8, cells_, -1);
            rp_vec_attach_spans(ctx, spans_, nspans_);
        }
        break;
    }
    case FTN_BYTE:
    {
        unsigned char *p;

        /* create backing buffer and copy data into it */
        p = (unsigned char *)duk_push_fixed_buffer(ctx, fl->ndata[i] /*size*/);
        memcpy(p, fl->data[i], fl->ndata[i]);
        break;
    }
    default:
        duk_push_int(ctx, (int)type);
    }
}

/* **************************************************
   This is called when sql.exec() has no callback.
   fetch rows and push results to array
   return number of rows
   ************************************************** */
static int rp_fetch(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
{
    int i       = 0,
        rettype = q->rettype,
        rawvec = q->flags & QFLAG_RAWVEC;
    uint64_t rown   = 0,
             resmax = q->max;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->flags & QFLAG_GETCOUNTS)
        h_getCountInfo(h, &cinfo);


    /* create return object */
    duk_push_object(ctx);

    /* create results array (outer array if rettype>0) */
    duk_push_array(ctx);

    /* array of arrays or novars requested */
    if (rettype)
    {

        /* still fill columns if rexmax == 0 */
        /* WTF: return columns if table is empty */
        if (resmax < 1)
        {
            if((fl = h_fetch(h, -1)))
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }

        } else
        /* push values into subarrays and add to outer array */
        while (rown < resmax && (fl = h_fetch(h, -1)))
        {
            /* novars, we need to get each row (for del and return count value) but not return any vars */
            if (rettype == 2)
            {
                rown++;
                continue;
            }
            /* we want first row to be column names */
            if (!rown)
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }

            /* push values into array */
            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                rp_pushfield(ctx, fl, i, rawvec);
                duk_put_prop_index(ctx, -2, i);
            }
            duk_put_prop_index(ctx, -2, rown++);

        } /* while */
    }
    else
    /* array of objects requested
     push object of {name:value,name:value,...} into return array */
    {
        while (rown < resmax && (fl = h_fetch(h, -1)))
        {
            if (!rown)
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }

            duk_push_object(ctx);
            for (i = 0; i < fl->n; i++)
            {
                rp_pushfield(ctx, fl, i, rawvec);
                //duk_dup_top(ctx);
                //printf("%s -> %s\n",fl->name[i],duk_to_string(ctx,-1));
                //duk_pop(ctx);
                duk_put_prop_string(ctx, -2, (const char *)fl->name[i]);
            }
            duk_put_prop_index(ctx, -2, rown++);
        }
    }
    /* added "rows", "results" to be removed
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx,-3,"results");
    */
    duk_put_prop_string(ctx,-2,"rows");
    if(q->flags & QFLAG_GETCOUNTS)
    {
        pushcounts;
        duk_put_prop_string(ctx,-2,"countInfo");
    }
    duk_push_int(ctx, rown);
    duk_put_prop_string(ctx,-2,"rowCount");

    return (rown);
}

/* **************************************************
   This is called when sql.exec() has a callback function
   Fetch rows and execute JS callback function with
   results.
   Return number of rows
   ************************************************** */
static int rp_fetchWCallback(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
{
    int i       = 0,
        rettype = q->rettype,
        rawvec = q->flags & QFLAG_RAWVEC;
    uint64_t rown   = 0,
             resmax = q->max;
    duk_idx_t callback_idx = q->callback,
              colnames_idx = 0,
              count_idx    =-1;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->flags & QFLAG_GETCOUNTS)
    {
        h_getCountInfo(h, &cinfo);
        pushcounts;             /* countInfo */
    }
    else
    {
        duk_push_object(ctx);
    }
    count_idx=duk_get_top_index(ctx);

#define docallback do {\
    duk_dup(ctx, count_idx);\
    if(q->arg_idx > -1){\
        duk_dup(ctx, q->arg_idx);\
        duk_call_method(ctx, 5);\
    } else duk_call_method(ctx, 4);\
} while(0)

    while (rown < resmax && (fl = h_fetch(h, -1)))
    {

        if (!rown)
        {
            /* an array of column names */
            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_push_string(ctx, fl->name[i]);
                duk_put_prop_index(ctx, -2, i);
            }
            colnames_idx=duk_get_top_index(ctx);
        }

        duk_dup(ctx, callback_idx); /* the function */
        duk_push_this(ctx);         /* calling with this */

        switch (rettype)
        {
            /* object requested */
            case 0:
            {
                duk_push_object(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    rp_pushfield(ctx, fl, i, rawvec);
                    duk_put_prop_string(ctx, -2, (const char *)fl->name[i]);
                }
                duk_push_int(ctx, q->skip + rown++ );
                duk_dup(ctx, colnames_idx);
                docallback;
                break;
            }
            /* array */
            case 1:
            {
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    rp_pushfield(ctx, fl, i, rawvec);
                    duk_put_prop_index(ctx, -2, i);
                }

                duk_push_int(ctx, q->skip + rown++ );
                duk_dup(ctx, colnames_idx);
                docallback;
                break;
            }
            /* novars */
            case 2:
            {
                duk_push_object(ctx);       /*empty object */
                duk_push_int(ctx, q->skip + rown++ ); /* index */
                duk_dup(ctx, colnames_idx);
                docallback;
                break;
            }
        } /* switch */
        /* if function returns false, exit while loop, return number of rows so far */
        if (duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
        {
            duk_pop(ctx);
            return (rown);
        }
        /* get rid of ret value from callback*/
        duk_pop(ctx);
    } /* while fetch */

    return (rown);
}
#define IF_CHANGED  0
#define FORCE_RESET 1

static void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx, int force);

#undef pushcounts

/* **************************************************
   Sql.prototype.import
   ************************************************** */
static duk_ret_t rp_sql_import(duk_context *ctx, int isfile)
{
    /* currently only csv
       but eventually add option for others
       by checking options object for
       "type":"filetype" - with default "csv"
    */
    const char *user="PUBLIC",
               *pass="",
               *func_name = isfile?"sql.importCsvFile":"sql.importCsv";
    DCSV dcsv=duk_rp_parse_csv(ctx, isfile, 1, func_name);
    int ncols=dcsv.csv->cols, i=0;
    int tbcols=0, start=0;
    DB_HANDLE *h = NULL;
    TEXIS *tx=NULL;
    char **field_names=NULL;
    uint8_t *field_type=NULL;
    int field_type_size=0;
    duk_idx_t this_idx;

#define closecsv do {\
    int col;\
    for(col=0;col<dcsv.csv->cols;col++) \
        free(dcsv.hnames[col]); \
    free(dcsv.hnames); \
    closeCSV(dcsv.csv); \
} while(0)


    if(strlen(dcsv.tbname)<1)
        RP_THROW(ctx, "%s(): option tableName is required", func_name);

    /* SECURITY (F14): tableName is interpolated into SQL (the SYSCOLUMNS lookup
       and the INSERT statement) and an identifier cannot be bound as a
       parameter, so restrict it to a safe identifier charset.  This prevents
       SQL injection when tableName is derived from request data. */
    {
        const unsigned char *tn = (const unsigned char *)dcsv.tbname;
        for(; *tn; tn++)
            if(!(isalnum(*tn) || *tn=='_'))
                RP_THROW(ctx, "%s(): tableName must contain only letters, digits and underscore", func_name);
    }

    const char *db;
    struct sigaction sa = { {0} };

    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    //  signal(SIGUSR2, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    /* ...and the capture buffer behind it, as exec() does at its entry.
     * Without this an unread message left by an earlier statement is
     * still in the buffer, and the first rp_log_error() here reports it
     * as though this import had produced it. */
    clearmsgbuf();

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        closecsv;
        RP_THROW(ctx, "no database has been opened");
    }
    db = duk_get_string(ctx, -1);

    duk_pop(ctx);

    h = h_open(db,user,pass);
    if(!h)
        throw_tx_error(ctx, "sql open");

    h_reset_tx_default(ctx, h, this_idx, IF_CHANGED);
    duk_pop(ctx);//this

    tx = h->tx;

    {
        FLDLST *fl;
        char sql[256];

        snprintf(sql, 256, "select NAME, TYPE from SYSCOLUMNS where TBNAME='%s' order by ORDINAL_POSITION;", dcsv.tbname);

        if (!h_prep(h, sql))
        {
            closecsv;
            throw_tx_error_close(ctx, "sql prep", h);
        }

        if (!h_exec(h))
        {
            closecsv;
            throw_tx_error_close(ctx, "sql exec", h);
        }

        while((fl = h_fetch(h, -1)))
        {
            /* an array of column names */
            REMALLOC(field_names, (tbcols+2) * sizeof(char*) );

            /* a uint8_t array of column types */
            if (tbcols + 1 > field_type_size)
            {
                field_type_size +=256;
                REMALLOC(field_type, field_type_size * sizeof(uint8_t));
            }

            field_names[tbcols] = strdup(fl->data[0]);

            /* keep track of which fields are counter */
            if(!strncmp(fl->data[1],"counter",7))
                field_type[tbcols] = 1;
            else
                field_type[tbcols] = 0;

            tbcols++;
        }
        /* table doesn't exist? */
        if(!tbcols)
        {
            closecsv;
            h_close(h);
            RP_THROW(ctx, "Table '%s' does not exist.  Table must exist before importing CSV", dcsv.tbname);
        }

        field_names[tbcols] = NULL;

    }

#define fn_cleanup(exc) do { \
    int j=0; \
    if(tbcols){ \
      while(field_names[j]!=NULL) \
          free(field_names[j++]); \
    }\
    if(field_names)free(field_names); \
    if(field_type)free(field_type); \
    if(exc) h_close(h);\
    else h_end_transaction(h);\
    closecsv; \
} while(0);

    {
        /* ncols = number of columns in the csv
           tbcols = number of columns in the table   */
        int col_order[tbcols]; /* one per table column and value is 0 -> ncols-1 */

        if(dcsv.arr_idx>-1)
        {
            duk_idx_t idx=dcsv.arr_idx;
            int aval=0, len= (int)duk_get_length(ctx, idx);
            char **hn=dcsv.hnames;

            for(i=0;i<len;i++)
            {
                duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
                if( duk_is_string(ctx, -1))
                {
                    int j=0;
                    const char *s=duk_get_string(ctx, -1);
                    if (strlen(s)==0)
                        aval=-1;
                    else
                    {
                        while (hn[j]!=NULL)
                        {
                            if(strcmp(hn[j],s)==0)
                            {
                                aval=j;
                                break;
                            }
                            j++;
                        }
                        if (hn[j]==NULL)
                        {
                            fn_cleanup(1);
                            RP_THROW(ctx, "%s(): array contains '%s', which is not a known column name", func_name,s);
                        }
                    }
                }
                else if(! duk_is_number(ctx, -1))
                {
                    fn_cleanup(1);
                    RP_THROW(ctx, "%s(): array requires an array of Integers/Strings (column numbers/names)", func_name);
                }
                else
                    aval=duk_get_int(ctx, -1);

                duk_pop(ctx);
                if( aval>=ncols )
                {
                    fn_cleanup(1);
                    RP_THROW(ctx, "%s(): array contains column number %d. There are %d columns in the csv (numbered 0-%d)",
                        func_name, aval, ncols, ncols-1);
                }
                col_order[i]=aval;
                //printf("order[%d]=%d\n",i,aval);
            }
            /* fill rest, if any, with -1 */
            for (i=len; i<tbcols; i++)
                col_order[i]=-1;
        }
        else
        {
            /* insert order is col order */
            for(i=0;i<tbcols;i++)
                col_order[i]=i;
        }

        {
            int slen = 24 + strlen(dcsv.tbname) + (2*tbcols) -1;
            char sql[slen];
            CSV *csv = dcsv.csv;
            void *v=NULL;   /* value to be passed to db */
            long plen, datelong;
            int in=0, out=0, row=0, col=0, intzero=0;
            DDIC *ddic=NULL;
            ft_counter *ctr = NULL;
            LPSTMT lpstmt;

            lpstmt = tx->hstmt;
            if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
                ddic = lpstmt->dbc->ddic;
            else
            {
                throw_tx_error(ctx, "sql open");;
            }

            snprintf(sql, slen, "insert into %s values (", dcsv.tbname);
            for (i=0;i<tbcols-1;i++)
                strcat(sql,"?,");

            strcat(sql,"?);");

            //printf("%s, %d, %d\n", sql, slen, (int)strlen(sql) );

            if (!h_prep(h, sql))
            {
                fn_cleanup(0);
                throw_tx_error_close(ctx, "sql prep", h);
            }

            if(dcsv.hasHeader) start=1;
            for(row=start;row<csv->rows;row++)      // iterate through the CSVITEMS contained in each row and column
            {
                for(  col=0; /* col<csv->cols && */col<tbcols; col++)
                {

                    if(col_order[col]>-1)
                    {
                    //printf("doing col_order[%d] = %d\n", col, col_order[col]);
                        CSVITEM item=csv->item[row][col_order[col]];
                        switch(item.type)
                        {
                            case integer:
                                in=SQL_C_SBIGINT;
                                out=SQL_BIGINT;
                                v=(int64_t*)&item.integer;
                                plen=sizeof(int64_t);
                                break;
                            case floatingPoint:
                                in=SQL_C_DOUBLE;
                                out=SQL_DOUBLE;
                                v=(double *)&item.integer;
                                plen=sizeof(double);
                                break;
                            case string:
                                v = (char *)item.string;
                                plen = strlen(v);
                                in = SQL_C_CHAR;
                                out = SQL_VARCHAR;
                                break;
                            case dateTime:
                            {
                                struct tm *t=&item.dateTime;
                                in=SQL_C_LONG;
                                out=SQL_DATE;
                                datelong=(long) mktime(t);
                                v = (long*)&datelong;
                                plen=sizeof(long);
                                break;
                            }
                            case nil:
                                in=SQL_C_INTEGER;
                                out=SQL_INTEGER;
                                v=(int*)&intzero;
                                plen=sizeof(int);
                                break;
                        }
                    }
                    else
                    {
                        /* insert texis counter if field is a counter type */
                        if (field_type[col]==1)
                        {
                            ctr = getcounter(ddic);
                            v=ctr;
                            plen=sizeof(ft_counter);
                            in=SQL_C_COUNTER;
                            out=SQL_COUNTER;
                        }
                        else
                        {
                            v=&intzero;
                            plen=sizeof(int);
                            in=SQL_C_INTEGER;
                            out=SQL_INTEGER;
                        }
                    }
                    if( !h_param(h, col+1, v, &plen, in, out))
                    {
                        if(ctr) free(ctr);
                        ctr=NULL;
                        fn_cleanup(0);
                        throw_tx_error_close(ctx, "sql add parameters", h);
                    }
                    if(ctr) free(ctr);
                    ctr=NULL;
                }
                if (col<tbcols)
                {
                    v=(long*)&intzero;
                    plen=sizeof(int);
                    in=SQL_C_INTEGER;
                    out=SQL_INTEGER;
                    for(; col<tbcols; col++)
                    {
                        if( !h_param(h, col+1, v, &plen, in, out))
                        {
                            fn_cleanup(0);
                            throw_tx_error_close(ctx, "sql add parameters", h);
                        }
                    }
                }

                if (!h_exec(h))
                {
                    fn_cleanup(0);
                    throw_tx_error_close(ctx, "sql exec",h);
                }
                if (!h_flush(h))
                {
                    fn_cleanup(0);
                    throw_tx_error_close(ctx, "sql flush", h);
                }

                h_resetparams(h);

                if (dcsv.func_idx > -1 && !( (row-start) % dcsv.cbstep ) )
                {
                    duk_dup(ctx, dcsv.func_idx);
                    duk_push_int(ctx, row-start);
                    duk_call(ctx, 1);
                    if(duk_is_boolean(ctx, -1) && ! duk_get_boolean(ctx, -1) )
                        goto funcend;
                    duk_pop(ctx);
                }
            }
        }
    }

    funcend:

    duk_push_int(ctx, dcsv.csv->rows - start);
    fn_cleanup(0);
    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
    return 1;
}

static duk_ret_t rp_sql_import_csv_file(duk_context *ctx)
{
    return rp_sql_import(ctx, 1);
}

static duk_ret_t rp_sql_import_csv_str(duk_context *ctx)
{
    return rp_sql_import(ctx, 0);
}

/*
   Finds the closing char c. Used for finding single and double quotes
   Tries to deal with escapements
   Returns a pointer to the end of string or the matching character
   *pN is stuffed withe the number of characters it skipped
*/
static char * skip_until_c(char *s,int c,int *pN)
{
   int n=0;
   while(*s)
   {
      if(*s=='\\' && *(s+1)==c)  // deal with escapement
      {
         ++s;
         ++n;
      }
      else
      if(*s==c)
      {
          if(pN)*pN=n;
          return(s);
      }
      ++s;
      ++n;
   }
  if(pN)
     *pN=n;
  return(s);
}

// counts the number of ?'s in the sql string
static int count_sql_parameters(char *s)
{
   int n_params=0;
   while(*s)
   {
      switch(*s)
      {
         case '"' :
         case '\'':
         {
             s=skip_until_c(s+1,*s,NULL);
             break;
         }
         case '\\': ++s; break;
         case '?' : ++n_params;break;
      }
      if(!*s) break;
      ++s;
   }
   return (n_params);
}

static char ** freenames(char **names, int len)
{
    if(!names)
        return names;
    int i=0;
    for (;i<len;i++)
    {
        if(names[i])
            free(names[i]);
    }
    free(names);
    return NULL;
}

// like strndup, but allocates extra_space more bytes
// if len < 0, acts like strdup but allocates extra_space more bytes
static char * strndupx (char *s, ssize_t len, size_t extra_space)
{
    if(!s)
        return NULL;

    char *ret=NULL;

    if(len<0) //get len from *s
    {
        if(extra_space > 0)
        {
            size_t olen = strlen(s) + extra_space + 1;
            CALLOC(ret, olen);
            strcpy(ret, s);
        }
        else
            return strdup(s);
    }
    else
    {
        size_t olen = len + extra_space + 1;
        CALLOC(ret, olen);
        if(len)
            strncpy(ret, s, len);
    }
    return ret;
}

/*
   This parses parameter names out of SQL statements.

   Parameter names must be int the form of:
      ?legal_SQL_variable_name   where legal is (\alpha || \digit || _)+
      or
      ?" almost anything "
      or
      ?' almost anything '

   Returns the number of parameters or -1 if there's syntax error involving parameters
   It removes the names from the SQL and places the result in *new_sql
   It places an array of pointers to the paramater names in names[] ( in order found )
   it places a pointer to a buffer it uses for the name space in *free_me.

   Both names[] and free_me must be freed by the caller BUT ONLY IF return is >0

   LIKEP additions: -ajf 2024-07-27
       - likeppos must be freed if not NULL.
       - free_me removed, names[] must be freed with freenames.
       - processing of sql is done even if no '?' params found.
       - semi-colon added if not in string
*/
static int parse_sql_parameters(
    char *old_sql,
    char **new_sql,
    char **names[]
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    ,
    int **likeppos,
    duk_context *ctx,
    duk_idx_t *obj_idx,
    int likepproc
#endif
)
{
    int    n_params=count_sql_parameters(old_sql);
    char * sql      =NULL;
    char **my_names =NULL;
    char * out_p    =NULL;
    char * s        =NULL;
    char idx_s[32];

    int    name_index=0;
    int    quote_len=0;
    int    qm_index=0;
    int gotsemi=0;

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int nlikep=0, likep_index=0;
    char *parsechars="?'\"\\;";
#endif
    //REMALLOC(sql, strlen(old_sql)+1); // the new_sql cant be bigger than the old
    REMALLOC(sql, strlen(old_sql)*2);   // now it can, because of extra
    *new_sql=sql;                 // give the caller the new SQL
    out_p=sql;                    // init the sql output pointer

    if(n_params)
        CALLOC(my_names, n_params*sizeof(char *));

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    if(likeppos)
        *likeppos=NULL;
#endif
    s=old_sql;
    while(*s)
    {
       switch(*s)
       {

          case ';' :
             gotsemi=1;
             break;
#ifdef LIKEP_PARAM_SUBSTITUTIONS
          // find the like?
          case ',' :
          case '(' :
          case ' ' :
             //skip all this if not doing useSuffixPreset, or whatever future thing triggers this
             if(!likepproc)
                 break;
             {
                // we are looking for likep, liker, like3, like, abstract or stingformat('%m..',...),
                // to replace the ? parameter or quoted string if useSuffixPreset == true
                int   vlen=5;
                char *likev=NULL;
                int   mfunc=0;
#define RP_IS_ABST   1
#define RP_IS_STRFMT 2
                int nparan=1;
                int nq=0;
                //copy the char and advance over spaces
                *out_p++=*s++;
                while(isspace(*s))
                {
                    *out_p++=*s++;
                }

                if(!*s)
                    goto noquery;
                else if (strchr(parsechars, *s)) //got one of '"\?;
                    continue;  //continue with the parse
                else if(!strncasecmp("likep", s, 5))
                    likev="likep ?";
                else if(!strncasecmp("liker", s, 5))
                    likev="liker ?";
                else if(!strncasecmp("like3", s, 5))
                    likev="like3 ?";
                else if(!strncasecmp("like", s, 4))
                {
                    likev="like ?";
                    vlen=4;
                }
                else if(!strncasecmp("abstract", s, 8))
                {
                    mfunc=RP_IS_ABST;
                    vlen=8;
                }
                else if(!strncasecmp("stringformat", s, 12))
                {
                    mfunc=RP_IS_STRFMT;
                    vlen=12;
                }

                if(likev || mfunc)
                {
                    char *p = s+vlen;
                    //printf("START = '%s'\n", p);
                    while(isspace(*p))
                        p++;

                    if(mfunc){
                        // abstract( or stringformat(
                        if (*p!='(')
                            goto noquery;
                        p++;
                        //nparan=1;
                    }

                    if(mfunc==RP_IS_ABST) // abstract(text, max, style, query)
                    {
                        //go to first non whitespace char after third comma(not in quotes), if exists
                        int breakpoint=0;
#define RP_ABST_BREAKAFTER 2
                        while(*p)
                        {
                            switch(*p) {
                                case '\'' :
                                case '"'  :
                                    p=skip_until_c(p+1, *p, NULL);
                                    break;
                                case ','  :
                                    if(nparan==1) //only count commas separating own parameters
                                        breakpoint++;
                                    break;
                                case '('  :
                                    nparan++;
                                    break;
                                case ')'  :
                                    nparan--;
                                    if(nparan<1)
                                        breakpoint=100000;
                                    break;
                                case '?'  :
                                    nq++;
                                    break;
                            }
                            if(breakpoint>RP_ABST_BREAKAFTER)
                                break;
                            p++;
                        }
                        if( *p == ',')
                        {
                            p++;
                            while(isspace(*p))
                                p++;
                                //printf("ABSTRACT at '%s'\n", p);
                            // check for ' or ? below
                        }
                        else
                            goto noquery;
                    }
                    else if(mfunc==RP_IS_STRFMT) // stringformat('%something %mxxx', something, ?query, text)
                    {
                        char qc;
                        int paramno=0, mparam=0, breakpoint=0;
                        while(isspace(*p))
                            p++;

                        if(*p == '\'' || *p == '"')
                            qc=*p;
                        else
                            goto noquery; //its something we can't handle

                        // find %m position
                        p++;
                        while (*p && *p != qc)
                        {
                            if(*p=='%') {
                                paramno++;
                                p++;
                                while(*p =='.' || isdigit(*p)) // %10.12mbH is legal
                                    p++;
                                if(*p=='m')
                                    mparam=paramno;
                            }
                            p++;
                        }
                        if(*p != qc || !mparam)
                            goto noquery;
                        p++;
                        // find param after the mparamth ,
                        while(*p)
                        {
                            switch(*p) {
                                case '\'' :
                                case '"'  :
                                    p=skip_until_c(p+1, *p, NULL);
                                    break;
                                case ','  :
                                    if(nparan==1)
                                        breakpoint++;
                                    break;
                                case '('  :
                                    nparan++;
                                    break;
                                case ')'  :
                                    nparan--;
                                    if(nparan<1)
                                        breakpoint=100000;
                                    break;
                                case '?'  :
                                    nq++;
                                    break;
                            }
                            if(breakpoint>=mparam)
                                break;
                            p++;
                        }

                        if( *p == ',')
                        {
                            p++;
                            while(isspace(*p))
                                p++;
                            // check for ' or ? below
                        }
                        else
                            goto noquery;
                    }

                    //printf("checking for ' or ? at >>%.20s<<\n",p);

                    if (*p =='\'' || *p == '"')
                    {
                        // copy the likep phrase to obj at obj_idx
                        // replace with '?' in query
                        // make room for another parameter name and create name as LIKEP_MOD_CHAR+likep_index
                        char endq=*p;

                        n_params++;
                        REMALLOC(my_names, n_params*sizeof(char *));
                        my_names[n_params-1]=NULL;
                        *names=my_names;

                        sprintf(idx_s, "%c%d", LIKEP_MOD_CHAR, likep_index);
                        my_names[name_index++] = strndupx(idx_s, -1, 1);

                        if(mfunc)
                        {
                            size_t clen = p-s;
                            strncpy(out_p, s, clen);
                            out_p+=clen;
                            strcpy(out_p, " ?");
                            out_p+=2;
                        }
                        else
                        {
                            strcpy(out_p, likev);
                            out_p+=vlen+2; //room for ?
                        }

                        // copy the "query terms" in likep 'query terms' to duktape stack
                        p++;
                        s=p;
                        while(*s && *s != endq)
                            s++;

                        if(!*s)
                            goto error_return;

                        if(*obj_idx == -1) // we don't have a q->obj_idx object, so make one
                            *obj_idx = duk_push_object(ctx);

                        // the name for duk_put_prop()
                        duk_push_sprintf(ctx, "%c%d", LIKEP_MOD_CHAR, likep_index++);
                        // the value
                        duk_push_lstring(ctx, p, (duk_size_t)(s-p));
                        duk_put_prop(ctx, *obj_idx);
                        s++; // '\''

                        REMALLOC(*likeppos, sizeof(int) * (nlikep +2));  //last will be -1;
                        (*likeppos)[nlikep++]=name_index-1;              //the corresponding index in my_names
                        (*likeppos)[nlikep]=-1;                          //terminate list
                        continue;
                    }
                    else if (*p == '?')
                    {
                        REMALLOC(*likeppos, sizeof(int) * (nlikep +2));  //last will be -1;
                        (*likeppos)[nlikep++]=name_index+nq;             //the corresponding index in my_names
                        (*likeppos)[nlikep]=-1;                          //terminate list
                        //printf("Post add like s='%s'\n", s);
                        continue;
                    }
                }
                //printf("AT END s='%s'\n", s);
                break;
             }
#endif
          case '"' :
          case '\'':
             {
                char *t=s;
                s=skip_until_c(s+1,*s,&quote_len);
                memcpy(out_p,t,quote_len+1);     // the plus 1 is for the quote character
                out_p+=quote_len+1;
                break;
             }
          case '\\': break;
          case '?' :
             {
                //printf("DOING ? at '%s'\n", s);
                ++s;

                if(!(isalnum(*s) || *s=='_' || *s=='"' || *s=='\'')) // check for legal 1st char
                {
                    sprintf(idx_s, "%d", qm_index++);
                    my_names[name_index++] = strndupx(idx_s, -1, 1);
                    *out_p='?';
                    ++out_p;
                    if(*s == ';')
                        gotsemi=1;
                    break;
                }

                if(*s=='"' || *s=='\'')          // handle ?"my var"
                {
                   int quote_type=*s;
                   char *p=++s;

                   s=skip_until_c(s,quote_type,&quote_len);
                   if(!*s)           // we hit a null without an ending "
                      goto error_return;

                   my_names[name_index++] = strndupx(p, (ssize_t)(s-p), 1);
                   *out_p='?';
                   ++out_p;
                   ++s;
                   continue;
                }
                else
                {
                   char *p=s;

                   while(*s && (isalnum(*s) || *s=='_'))
                      ++s;
                   my_names[name_index++] = strndupx(p, (ssize_t)(s-p), 1);
                   *out_p='?';
                   ++out_p;
                   if(*s == ';')
                      gotsemi=1;
                   *out_p++=*s;

                   if(!*s)           //  terminated at the end of the sql we're done
                      goto end_return;
                   ++s;
                 continue;
                }
             }
             break;
       }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
       noquery:
#endif
       *out_p++=*s;
       if(!*s)
          break;
       ++s;
    }

   end_return:

   // terminate string
   *out_p='\0';

   //set names
   *names=my_names; // NULL if not doing names

   // terminate sql if necessary
   if(!gotsemi)
       strcat(sql,";");

   return(n_params);

   error_return:
   // free everything and set passed in *pointer to NULL
   if(my_names)
   {
     *names=freenames(my_names, n_params);
     *names=NULL;
   }
   if(sql)
   {
     free(sql);
     *new_sql=NULL;
   }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
   if(*likeppos)
   {
       free(*likeppos);
       *likeppos=NULL;
   }
#endif
   return(-1);
}

/*
void check_parse(char *sql,char *new_sql,char **names,int n_names)
{
   int i;
   printf("IN :%s\nOUT:%s\n%d names\n",sql,new_sql,n_names);
   for(i=0;i<n_names;i++)
      printf("%5d %s\n",i,names[i]);
   printf("\n\n");
}
*/

/* Note the ordering: the error object is FORMATTED first (it reads msg,
 * which is normally finfo->errmap), then rp_log_error() copies the same
 * text to this.errMsg and clears the buffer, and only then do we throw.
 * RP_THROW() does not return, so doing it first -- as this macro used to
 * -- left rp_log_error() unreachable for exec(), and exec() failures here
 * silently never reached errMsg while the identical failure through
 * throw_tx_or_log_error_close() did.  Same shape as that macro now. */
#define throw_tx_or_log_error(ctx,pref,msg) do{\
    rp_msg_finalize();\
    if(!isquery) \
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s error: %s",pref, msg);\
    else\
        duk_push_null(ctx);\
    rp_log_error(ctx);\
    if(!isquery) (void) duk_throw(ctx);\
    goto end_query;\
}while(0)

// close resets finfo->errmap
#define throw_tx_or_log_error_close(ctx,pref,msg,h) do{\
    rp_msg_finalize();\
    if(!isquery) \
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s error: %s",pref, msg);\
    else\
        duk_push_null(ctx);\
    rp_log_error(ctx);\
    h_close(h);\
    h=NULL;\
    if(!isquery) (void) duk_throw(ctx);\
    goto end_query;\
}while(0)

#define throw_or_log_error_old(msg) do{\
    if(!isquery) RP_THROW(ctx, "%s",msg);\
    else if(q && q->callback > -1){\
        rp_log_copy_to_errMsg(ctx, msg);\
        duk_push_number(ctx, -1);\
    } else {\
        duk_push_object(ctx);\
        duk_push_sprintf(ctx, "%s", msg);\
        duk_put_prop_string(ctx, -2, "error");\
    }\
    goto end_query;\
}while(0)

#define throw_or_log_error(msg) do{\
    rp_log_copy_to_errMsg(ctx, msg);\
    if(!isquery)\
        RP_THROW(ctx, "%s",msg);\
    else\
        duk_push_null(ctx);\
    goto end_query;\
}while(0)

// from rampart.utils - put this in rampart.h
duk_ret_t word_replace_new(duk_context *ctx);

#ifdef LIKEP_PARAM_SUBSTITUTIONS

#include "english_short_nouns.h"

/* load short noun equivs into rampart.utils.replace
   and stash it for future use                         */
static duk_idx_t load_list(duk_context *ctx)
{
    duk_push_global_stash(ctx);
    if(!duk_get_prop_string(ctx, -1, "shortnouns"))
    {
        duk_pop(ctx);
        duk_push_c_function(ctx, word_replace_new, 2);
        duk_push_string(ctx, english_short_nouns);
        duk_push_undefined(ctx);
        duk_new(ctx, 2);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "shortnouns");
    }
    duk_remove(ctx, -2); //stash
    return duk_normalize_index(ctx, -1);
}
#endif // LIKEP_PARAM_SUBSTITUTIONS

/* ---- clipEmbed: image-BUFFER parameter substitution -----------------
 *
 *   insert into t values (?, embed(?, 'image'));   -- ? is a Buffer
 *
 * A buffer parameter is already in THIS process, and this process owns
 * the model -- but SQL runs in the helper, so the plain path would ship
 * the bytes to the helper and straight back again.  (The wire itself no
 * longer caps this -- payloads chunk -- but a pointless round trip of
 * the whole image is still worth avoiding.)  Instead: embed here, rewrite that
 * `embed(...)` span to a bare `?`, and bind the resulting VECTOR in
 * place of the bytes.  Same statement, same stored vector, but the
 * image never crosses a process boundary.  A String parameter (a path)
 * is left completely alone -- it flows through the normal path.
 *
 * Rewriting `embed(?, 'image')` -> `?` keeps the number of `?` in the
 * statement identical, so every parameter index is unchanged.
 *
 * Deliberately conservative: the span must match exactly
 * `embed ( ? [, '<dtype>'] , 'image' )` (whitespace anywhere, any case).
 * ANY deviation -- a named param, a non-buffer value, extra arguments,
 * no clip engine bound -- leaves the statement byte-for-byte untouched
 * and the old path handles it.  A miss therefore degrades to current
 * behaviour, never to a wrong vector.
 */
/* dtype word -> bytes per cell + the texis type name for the cast the
 * substitution emits.  The cast is what keeps the rewritten expression
 * the SAME TYPE as embed() would have produced -- without it, `select
 * embed(?,'image')` would come back as a raw byte buffer rather than a
 * vector.  NOTE the type names are case-SENSITIVE in texis ('varvecf16'
 * is rejected as an unknown type; 'varvecF16' is right). */
static int clip_dtype_word(const char *s, size_t n, int *cellbytes,
                           const char **vtype)
{
    if      (n == 3 && !strncasecmp(s, "f16",  3)) { *cellbytes = 2; *vtype = "varvecF16"; return 1; }
    else if (n == 3 && !strncasecmp(s, "f32",  3)) { *cellbytes = 4; *vtype = "varvecF32"; return 1; }
    else if (n == 3 && !strncasecmp(s, "f64",  3)) { *cellbytes = 8; *vtype = "varvecF64"; return 1; }
    else if (n == 4 && !strncasecmp(s, "auto", 4)) { *cellbytes = 2; *vtype = "varvecF16"; return 1; }
    return 0;    /* bf16 (and anything else): leave the statement alone */
}

/* Push a JS buffer holding `vec` in the dtype the statement asked for.
 * cellbytes: 2=f16 (default), 4=f32, 8=f64.  bf16 is not produced here
 * -- callers requesting it fall back to leaving the span alone. */
static void clip_push_vec_buffer(duk_context *ctx, const float *vec,
                                 size_t dim, int cellbytes)
{
    if (cellbytes == 4) {
        void *b = duk_push_fixed_buffer(ctx, dim * sizeof(float));
        memcpy(b, vec, dim * sizeof(float));
    } else if (cellbytes == 8) {
        double *b = (double *)duk_push_fixed_buffer(ctx, dim * sizeof(double));
        for (size_t i = 0; i < dim; i++) b[i] = (double)vec[i];
    } else {
        void *b = duk_push_fixed_buffer(ctx, dim * sizeof(uint16_t));
        rpvec_f32_to_f16((float *)vec, (uint16_t *)b, dim);
    }
}

/* Returns a malloc'd rewritten statement, or NULL when nothing matched
 * (in which case the caller keeps its original sql untouched). */
#define CLIP_SUB_MAX 8            /* image params substituted per statement */
#define CLIP_SUB_NAMELEN 24
static char *clip_sub_image_params(duk_context *ctx, DB_HANDLE *h,
                                   const char *sql, duk_idx_t obj_idx,
                                   char **pnames, int npnames,
                                   char subnames[][CLIP_SUB_NAMELEN], int *nsub,
                                   char *errbuf, size_t errlen)
{
    const char *s = sql;
    char *out = NULL, *o;
    int   qidx = 0, changed = 0;

    if (!h || h->embed_engine != EMBED_ENGINE_CLIP || !h->embed_handle ||
        !g_rp_clip_embed_image || obj_idx < 0 || !pnames)
        return NULL;
    /* cheap pre-test: no 'image' anywhere means nothing to do */
    {
        const char *p; int found = 0;
        for (p = sql; *p; p++)
            if ((*p=='i'||*p=='I') && !strncasecmp(p, "image", 5)) { found = 1; break; }
        if (!found) return NULL;
    }

    /* the replacement `convert(?, 'varvecF16')` can be longer than the
     * span it replaces (e.g. `embed(?,'image')`), so allow room */
    out = (char *)malloc(strlen(sql) + 64);
    if (!out) return NULL;
    o = out;

    while (*s) {
        /* copy quoted strings verbatim so a '?' inside one is not counted */
        if (*s == '\'' || *s == '"') {
            char q = *s;
            *o++ = *s++;
            while (*s && *s != q) {
                if (*s == '\\' && s[1]) *o++ = *s++;
                *o++ = *s++;
            }
            if (*s) *o++ = *s++;
            continue;
        }
        if (*s == '?') { qidx++; *o++ = *s++; continue; }

        /* `embed` as a whole word, not preceded by an identifier char */
        if ((*s == 'e' || *s == 'E') && !strncasecmp(s, "embed", 5) &&
            (s == sql || (!isalnum((unsigned char)s[-1]) && s[-1] != '_')) &&
            !isalnum((unsigned char)s[5]) && s[5] != '_')
        {
            const char *p = s + 5;
            while (isspace((unsigned char)*p)) p++;
            if (*p == '(') {
                const char *span = s;          /* for verbatim fallback */
                int    cellbytes = 2;          /* f16 default */
                const char *vtype = "varvecF16";
                int    sawImage = 0, ok = 1, nargs = 0;
                p++;                           /* past '(' */
                while (isspace((unsigned char)*p)) p++;
                if (*p != '?') ok = 0;         /* first arg must be a bare ? */
                else {
                    p++;
                    nargs = 1;
                    while (ok) {
                        while (isspace((unsigned char)*p)) p++;
                        if (*p == ')') { p++; break; }
                        if (*p != ',') { ok = 0; break; }
                        p++;
                        while (isspace((unsigned char)*p)) p++;
                        if (*p != '\'' && *p != '"') { ok = 0; break; }
                        {   /* a quoted arg: dtype word or kind word */
                            char q = *p++;
                            const char *a = p;
                            while (*p && *p != q) p++;
                            if (!*p) { ok = 0; break; }
                            size_t alen = (size_t)(p - a);
                            p++;               /* past closing quote */
                            nargs++;
                            if (alen == 5 && !strncasecmp(a, "image", 5))
                                sawImage = 1;
                            else if (!clip_dtype_word(a, alen, &cellbytes, &vtype))
                                ok = 0;        /* title/bf16/other: leave alone */
                        }
                        if (nargs > 3) { ok = 0; break; }
                    }
                }
                if (ok && sawImage && qidx < npnames && pnames[qidx] &&
                    *nsub < CLIP_SUB_MAX)
                {
                    /* The parameter this `?` refers to.  parse_sql_parameters
                     * has already turned both `?` and `?name` into positional
                     * `?` with pnames[] holding the key each one reads, so a
                     * single lookup covers both forms. */
                    duk_get_prop_string(ctx, obj_idx, pnames[qidx]);
                    if (duk_is_buffer_data(ctx, -1)) {
                        duk_size_t blen = 0;
                        void *b = duk_get_buffer_data(ctx, -1, &blen);
                        float *vec = NULL;
                        size_t dim = blen ? g_rp_clip_embed_image(h->embed_handle,
                                                                  b, (size_t)blen, &vec)
                                          : 0;
                        if (dim && vec) {
                            /* Bind the vector under a FRESH key rather than
                             * overwriting pnames[qidx]: the same name may be
                             * used by another `?` in the statement -- e.g.
                             *   insert into t values (?p, embed(?p,'image'))
                             * -- and that one must still see the caller's
                             * original value.  Skip any name already present
                             * so a caller's own property is never shadowed. */
                            char nm[CLIP_SUB_NAMELEN];
                            int  k = 0;
                            do {
                                snprintf(nm, sizeof nm, "__clipvec%d", k++);
                            } while (k < 1000 && duk_has_prop_string(ctx, obj_idx, nm));
                            clip_push_vec_buffer(ctx, vec, dim, cellbytes);
                            duk_put_prop_string(ctx, obj_idx, nm);
                            /* point THIS `?` at the new key */
                            free(pnames[qidx]);
                            pnames[qidx] = strdup(nm);
                            /* remember it so the caller can delete it again */
                            snprintf(subnames[*nsub], CLIP_SUB_NAMELEN, "%s", nm);
                            (*nsub)++;
                            free(vec);
                            duk_pop(ctx);              /* the original buffer */
                            /* cast keeps the expression's TYPE identical to
                             * what embed() would have returned */
                            o += sprintf(o, "convert(?, '%s')", vtype);
                            qidx++;
                            s = p;                      /* past the whole span */
                            changed = 1;
                            continue;
                        }
                        free(vec);
                        duk_pop(ctx);
                        snprintf(errbuf, errlen,
                                 "embed(?, 'image'): could not embed the image "
                                 "buffer given for parameter %d", qidx);
                        free(out);
                        return NULL;            /* caller throws errbuf */
                    }
                    duk_pop(ctx);               /* not a buffer: leave alone */
                }
                (void)span;
            }
            /* no match: fall through and copy 'embed' verbatim */
        }
        *o++ = *s++;
    }
    *o = '\0';
    if (!changed) { free(out); return NULL; }
    return out;
}

/* **************************************************
   Sql.prototype.exec
   ************************************************** */
static duk_ret_t rp_sql_exec_query(duk_context *ctx, int isquery)
{
    TEXIS *tx;
    QUERY_STRUCT *q=NULL, q_st;
    DB_HANDLE *h = NULL;
    const char *db, *user="PUBLIC", *pass="";
    duk_idx_t this_idx;
    struct sigaction sa = { {0} };
    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    clearmsgbuf();

    int nParams=0;
    char *newSql=NULL, **namedSqlParams=NULL;
    /* temporary parameter keys added by clip_sub_image_params(); removed
     * again as soon as they are bound, so the caller's parameter object
     * is never observably changed (it may be an object the script is
     * using elsewhere, and exec's row callback can see it). */
    char clipSubNames[CLIP_SUB_MAX][CLIP_SUB_NAMELEN];
    int  nClipSub = 0;
    //  signal(SIGUSR2, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int do_suffix=0;
    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings")))
    {
        if(duk_get_prop_string(ctx, -1, "usesuffixpreset")) {
            do_suffix=duk_get_boolean_default(ctx, -1, 0);
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);
#endif

    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, this_idx, "db"))
        throw_or_log_error("no database has been opened");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx); //db

    q_st = rp_get_query(ctx);
    q = &q_st;

    /* call parameters error, message is already pushed */
    if (q->err == QS_ERROR_PARAM)
    {
        goto end;
    }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int *likeppos = NULL;
    nParams = parse_sql_parameters((char*)q->sql, &newSql, &namedSqlParams, &likeppos, ctx, &(q->obj_idx), do_suffix);
    /*
    printf("newSql='%s' likeppos=%p\n", newSql, likeppos);
    if(likeppos)
    {
        int i=0, *lpp = likeppos;
        while( lpp[i] > -1 )
        {
            printf("likep found in pos %d: '%s'\n", lpp[i], namedSqlParams[lpp[i]]);
            i++;
        }
    } */
#else
    nParams = parse_sql_parameters((char*)q->sql, &newSql, &namedSqlParams);
#endif

    if (nParams > -1) // even if zero, we will get a newSql - ajf 2025-07-27
    {
        duk_push_string(ctx, newSql);
        duk_replace(ctx, q->str_idx);
        q->sql = duk_get_string(ctx, q->str_idx);
        free(newSql);
    }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    /* fix parameters or query */
    if(do_suffix && likeppos) {
        int i=0, *lpp = likeppos;

        duk_idx_t listf_idx = load_list(ctx); // load the list and push wordReplace at listf_idx

        // replace each likep parameter with wordReplace(parameter)
        while( lpp[i] > -1 )
        {
            char *param = namedSqlParams[lpp[i]];

            duk_push_string(ctx,"exec");
            duk_get_prop_string(ctx, q->obj_idx, param);

            // bail if not a string
            if(!duk_is_string(ctx, -1))
            {
                duk_pop_2(ctx);
                i++;
                continue;
            }

            //don't alter object beyond adding hidden properties
            memmove(param+1, param, strlen(param));  //make room for appending LIKEP_MOD_CHAR
            *param=LIKEP_MOD_CHAR; //same name, but altered

            // check if we've done this already
            if(duk_has_prop_string(ctx, q->obj_idx, param))
            {
                //printf("repeat of %s\n", param);
                duk_pop_2(ctx);
                i++;
                continue;
            }

            duk_call_prop(ctx, listf_idx, 1);

            //printf("substituted param %s -> likep = '%s'\n", param, duk_get_string(ctx,-1));

            duk_put_prop_string(ctx, q->obj_idx, param);
            i++;
        }
    }

    if(likeppos)
    {
        free(likeppos);
        likeppos=NULL;
    }
#endif // LIKEP_PARAM_SUBSTITUTIONS
    /* OPEN */
    h = h_open(db,user,pass);
    if(!h)
    {
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error(ctx, "sql open", finfo->errmap);
    }
    h_reset_tx_default(ctx, h, this_idx, IF_CHANGED);

    /* Activate THIS connection's embed model for the prep+exec below.  embed()
     * (in-process: main_embed_callback; forked: parent_service_embed) reads this
     * thread-local, so the model resolves per SQL connection.  Normally h_set has
     * cached the model on the handle; but the last_sql_set fast-path in
     * h_reset_tx_default can skip re-applying settings to a freshly-opened handle
     * (notably the very first query right after sql.set), leaving embed_handle
     * NULL.  In that case resolve it from the connection's saved settings and
     * cache it on the handle (g_rp_embed_load is cached by path, so this loads at
     * most once per handle). */
    if (!h->embed_handle) {
        duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings"));
        if (duk_is_object(ctx, -1)) {
            const char *epath = NULL;
            int lcap = -1;   /* likevCache from THIS connection's stored
                              * settings (on stack top) -- not the
                              * thread-local, which may hold another
                              * connection's cap at exec time. */
            if (peek_likevcache_setting(ctx, &lcap) && lcap < 0) lcap = 0;
            if (g_rp_embed_load && peek_llamaembed_setting(ctx, &epath) && epath) {
                char eerr[256] = {0};
                void *mh = g_rp_embed_load(epath, eerr, sizeof eerr);
                if (mh) {
                    h->embed_handle = mh;
                    h->embed_engine = EMBED_ENGINE_LLAMACPP;
                    resolve_embed_prompts(ctx, "llamaembed", epath);
                    attach_prompts_to_handle(h);
                    if (lcap >= 0 && g_rp_embed_set_cache_cap)
                        g_rp_embed_set_cache_cap(mh, (size_t)lcap);
                    DB_HANDLE_SET(h, DB_FLAG_EMBED_ENABLED);
                }
            } else if (g_rp_onnx_embed_load) {
                const char *omodel = NULL;
                rp_onnx_embed_opts oopts;
                if (peek_onnxembed_setting(ctx, &omodel, &oopts) && omodel) {
                    char eerr[256] = {0};
                    void *mh = g_rp_onnx_embed_load(omodel, &oopts, eerr, sizeof eerr);
                    if (mh) {
                        h->embed_handle = mh;
                        h->embed_engine = EMBED_ENGINE_ONNX;
                        resolve_embed_prompts(ctx, "onnxembed", omodel);
                        attach_prompts_to_handle(h);
                        if (lcap >= 0 && g_rp_onnx_embed_set_cache_cap)
                            g_rp_onnx_embed_set_cache_cap(mh, (size_t)lcap);
                        DB_HANDLE_SET(h, DB_FLAG_EMBED_ENABLED);
                    }
                }
            } else if (g_rp_clip_embed_load) {
                const char *cmodel = NULL;
                if (peek_clipembed_setting(ctx, &cmodel) && cmodel) {
                    char eerr[256] = {0};
                    void *mh = g_rp_clip_embed_load(cmodel, eerr, sizeof eerr);
                    if (mh) {
                        h->embed_handle = mh;
                        h->embed_engine = EMBED_ENGINE_CLIP;
                        resolve_embed_prompts(ctx, "clipembed", cmodel);
                        attach_prompts_to_handle(h);
                        /* no doc cache: CLIP has no chunked doc path */
                        DB_HANDLE_SET(h, DB_FLAG_EMBED_ENABLED);
                    }
                }
            }
        }
        duk_pop(ctx);
    }
    g_active_embed_handle = h->embed_handle;
    g_active_embed_engine = h->embed_engine;
    g_active_prompt_query     = h->embed_prompt_query;
    g_active_prompt_document  = h->embed_prompt_document;
    g_active_prompt_doc_title = h->embed_prompt_doc_title;

    /* arm the embed failure latch for THIS statement (see g_embed_failed) */
    g_embed_failed = 0;
    g_embed_failmsg[0] = '\0';

//  messes up the count for arg_idx, so just leave it
//    duk_remove(ctx, this_idx); //no longer needed

    tx = h->tx;
    if (!tx)
    {
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error(ctx, "open sql", finfo->errmap);
    }

    /* clipEmbed: turn `embed(?, 'image')` over a BUFFER parameter into a
     * bare `?` bound to the vector, embedded here in the parent.  Done
     * after the model is active (just above) and before PREP so the
     * helper only ever sees the rewritten statement.  Returns NULL when
     * nothing matched -- the overwhelmingly common case, and cheap. */
    {
        char cerr[256] = {0};
        char *csql = clip_sub_image_params(ctx, h, q->sql, q->obj_idx,
                                           namedSqlParams, nParams,
                                           clipSubNames, &nClipSub,
                                           cerr, sizeof cerr);
        if (csql) {
            /* park it on the stack like newSql above, so its lifetime is
             * the duktape value stack's problem, not every exit path's */
            duk_push_string(ctx, csql);
            free(csql);
            duk_replace(ctx, q->str_idx);
            q->sql = duk_get_string(ctx, q->str_idx);
        } else if (cerr[0]) {
            /* an earlier image in the same statement may already have been
             * substituted -- take those keys back off before throwing */
            while (nClipSub > 0)
                duk_del_prop_string(ctx, q->obj_idx, clipSubNames[--nClipSub]);
            namedSqlParams=freenames(namedSqlParams, nParams);
            h_close(h);
            RP_THROW(ctx, "sql exec: %s", cerr);
        }
    }

    /* PREP */
    if (!h_prep(h, (char *)q->sql))
    {
        while (nClipSub > 0)
            duk_del_prop_string(ctx, q->obj_idx, clipSubNames[--nClipSub]);
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error_close(ctx, "sql prep", finfo->errmap, h);
    }

    /* PARAMS
       sql parameters are the parameters corresponding to "?key" in a sql statement
       and are provide by passing an object in JS call parameters */
    if(namedSqlParams)
    {
        if(q->obj_idx == -1)
        {
            h_close(h);
            h=NULL;
            namedSqlParams=freenames(namedSqlParams, nParams);
            throw_or_log_error("sql.exec - parameters specified in sql statement, but no corresponding object or array");
        }

        int addok = rp_add_named_parameters(ctx, h, q->obj_idx, namedSqlParams, nParams);

        /* Remove the temporary __clipvecN keys the moment they are bound --
         * before the failure path below, and before h_exec can call back
         * into JS -- so the caller's parameter object is byte-for-byte what
         * it was.  texis already holds its own copy of each bound value. */
        while (nClipSub > 0)
            duk_del_prop_string(ctx, q->obj_idx, clipSubNames[--nClipSub]);

        if (!addok)
        {
            namedSqlParams=freenames(namedSqlParams, nParams);
            throw_tx_or_log_error_close(ctx, "sql add parameters", finfo->errmap, h);
        }

        namedSqlParams=freenames(namedSqlParams, nParams);
    }


    /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters
     TODO: check that this is indeed dead code given that parse_sql_parameters now
           turns "?, ?" into "?0, ?1"
     *
    else if (q->arr_idx != -1)
    {
        if (!rp_add_parameters(ctx, h, q->arr_idx))
            throw_tx_or_log_error_close(ctx, "sql add parameters", finfo->errmap, h);
    }
    */
    else
    {
        h_resetparams(h);
    }

    rp_log_error(ctx);

    /* EXEC */
    if (!h_exec(h))
        throw_tx_or_log_error_close(ctx, "sql exec", finfo->errmap, h);

    rp_log_error(ctx);

    /* skip rows using texisapi */
    if (q->skip)
        h_skip(h, q->skip);

    /* callback - return one row per callback */
    if (q->callback > -1)
    {
        int rows = rp_fetchWCallback(ctx, h, q);
        duk_push_int(ctx, rows);
        goto end; /* done with exec() */
    }

    /*  No callback, return all rows in array of objects */
    (void)rp_fetch(ctx, h, q);

    end:
    rp_log_error(ctx);
    h_end_transaction(h);

    /* An embed()/chunkembed() that failed mid-statement leaves a NULL
     * field behind rather than stopping texis, so raise it here where it
     * cannot be missed.  exec() throws; query() keeps its documented
     * never-throw contract and the reason stays in errMsg. */
    if (g_embed_failed) {
        g_embed_failed = 0;
        if (!isquery)
            RP_THROW(ctx, "sql exec error: %s", g_embed_failmsg);
    }

    return 1; /* returning outer array */

    end_query:
    rp_log_error(ctx);
    if(h) h_end_transaction(h);
    return 1; /* returning outer array or error*/

}

static duk_ret_t rp_sql_exec(duk_context *ctx)
{
  return rp_sql_exec_query(ctx, 0);
}

static duk_ret_t rp_sql_query(duk_context *ctx)
{
  return rp_sql_exec_query(ctx, 1);
}

/* **************************************************
   Sql.prototype.eval
   ************************************************** */
static duk_ret_t rp_sql_eval(duk_context *ctx)
{
    char *stmt = (char *)NULL;
    duk_idx_t str_idx = -1;
    duk_idx_t i = 0, top=duk_get_top(ctx);;

    /* find the argument that is a string */
    for (i = 0; i < top; i++)
    {
        if ( duk_is_string(ctx, i) )
        {
            stmt = (char *)duk_get_string(ctx, i);
            str_idx = i;
        }
        else if( duk_is_object(ctx, i) && !duk_is_array(ctx, i) )
        {
            /* remove returnType:'arrayh' as only one row will be returned */
            if(duk_get_prop_string(ctx, i, "returnType"))
            {
                if(! strcmp(duk_get_string(ctx, -1), "arrayh") )
                    duk_del_prop_string(ctx, i, "returnType");
            }
            duk_pop(ctx);
        }
    }

    if (str_idx == -1)
    {
        rp_log_copy_to_errMsg(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_sprintf(ctx, "select %s;", stmt);
    duk_replace(ctx, str_idx);
    rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "rows");
    duk_get_prop_index(ctx, -1, 0);
    return (1);
}

static duk_ret_t rp_sql_one(duk_context *ctx)
{
    duk_idx_t str_idx = -1, i = 0, obj_idx = -1;

    for (i = 0; i < 2; i++)
    {
        if ( duk_is_string(ctx, i) )
            str_idx = i;
        else if( duk_is_object(ctx, i) && !duk_is_array(ctx, i) )
            obj_idx=i;
    }

    if (str_idx == -1)
    {
        RP_THROW(ctx, "sql.one: No sql statement provided");
    }

    duk_push_object(ctx);
    duk_push_number(ctx, 1.0);
    duk_put_prop_string(ctx, -2, "maxRows");
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "returnRows");

    if( obj_idx != -1)
        duk_pull(ctx, obj_idx);

    rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "rows");
    duk_get_prop_index(ctx, -1, 0);
    //if(duk_is_undefined(ctx, -1))
    //    duk_push_object(ctx);
    return (1);
}


static void free_list(char **nl)
{
    int i=0;
    char *f;

    if(nl==NULL)
        return;

    f=nl[i];

    while(1)
    {
        if (*f=='\0')
        {
            free(f);
            break;
        }
        free(f);
        i++;
        f=nl[i];
    }
    free(nl);
//    *needs_free=0;
}


/*
    Get the heapptr of last sql object used to do sql.set in this thread
    if different from current, reapply settings
*/

static void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx, int force)
{
    void *cur = duk_get_heapptr(ctx, this_idx);

    // if this was the last one to reapply settings, we don't need to do it again
    if(!force && cur == last_sql_set)
        return;

     // reset settings if any
     {
         char errbuf[msgbufsz];
         int ret;

         if (!duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings")) )
         {
             //we have no old settings
             duk_pop(ctx);//undefined
             duk_push_object(ctx); //empty object, reset anyway
         }

         ret = h_set(ctx, h, errbuf);

         duk_pop(ctx);

         if(ret == -1)
         {
             h_close(h);
             RP_THROW(ctx, "%s", errbuf);
         }
         else if (ret ==-2)
             throw_tx_error_close(ctx, errbuf, h);
        else
             last_sql_set=cur;
     }
}


static duk_ret_t rp_texis_reset(duk_context *ctx)
{
    const char *db, *user="PUBLIC", *pass="";
    DB_HANDLE *h = NULL;

    duk_push_this(ctx); //idx == 0

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    //remove saved settings
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("indlist"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist"));

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx);

    h = h_open(db,user,pass);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }

    h_reset_tx_default(ctx, h, -1, FORCE_RESET);

    h_end_transaction(h);
    return 0;
}

static void sql_normalize_prop(char *prop, const char *dprop)
{
    int i=0;

    strcpy(prop, dprop);

    for(i = 0; prop[i]; i++)
        prop[i] = tolower(prop[i]);

    /* a few aliases */
    if(!strcmp("listexp", prop) || !strcmp("listexpressions", prop))
        strcpy(prop, "lstexp");
    else if (!strcmp("listindextmp", prop) || !strcmp("listindextemp", prop) || !strcmp("lstindextemp", prop))
        strcpy(prop, "lstindextmp");
    else if (!strcmp("deleteindextmp", prop) || !strcmp("deleteindextemp", prop) || !strcmp("delindextemp", prop))
        strcpy(prop, "delindextmp");
    else if (!strcmp("addindextemp", prop))
        strcpy(prop, "addindextmp");
    else if (!strcmp("addexpressions", prop))
        strcpy(prop, "addexp");
    else if (!strcmp("delexpressions", prop) || !strcmp("deleteexpressions", prop))
        strcpy(prop, "delexp");
    else if (!strcmp("keepequivs", prop) || !strcmp("useequivs", prop) || !strcmp("keepeqvs", prop))
        strcpy(prop, "useequiv");
    else if (!strcmp("equivsfile", prop))
        strcpy(prop, "eqprefix");
    else if (!strcmp("userequivsfile", prop))
        strcpy(prop, "ueqprefix");
    else if (!strcmp ("listnoise",prop))
        strcpy (prop, "lstnoise");
    else if (!strcmp ("listsuffix",prop))
        strcpy (prop, "lstsuffix");
    else if (!strcmp ("listsuffixequivs",prop))
        strcpy (prop, "lstsuffixeqivs");
    else if (!strcmp ("listprefix",prop))
        strcpy (prop, "lstprefix");
    else if (!strcmp ("noiselist",prop))
        strcpy (prop, "noiselst");
    else if (!strcmp ("suffixlist",prop))
        strcpy (prop, "suffixlst");
    else if (!strcmp ("suffixequivslist",prop))
        strcpy (prop, "suffixeqivslst");
    else if (!strcmp ("suffixeqlist",prop))
        strcpy (prop, "suffixeqlst");
    else if (!strcmp ("prefixlist",prop))
        strcpy (prop, "prefixlst");
}


TEXIS *setprop_tx=NULL;

static char *prop_defaults[][2] = {
   {"defaultLike", "like"},
   {"matchMode", "0"},
   {"pRedoPtType", "0"},
   {"textSearchMode", "unicodemulti, ignorecase, ignorewidth, ignorediacritics, expandligatures"},
   {"stringCompareMode", "unicodemulti, respectcase"},
   {"btreeCacheSize", "20"},
   {"ramRows", "0"},
   {"ramLimit", "0"},
   {"bubble", "1"},
   {"ignoreNewList", "0"},
   {"indexWithin", "0xf"},
   {"wildOneWord", "1"},
   {"wildSufMatch", "1"},
   {"alLinearDict", "0"},
   {"alLinear", "0"},
   {"indexMinSublen", "2"},
   {"dropWordMode", "0"},
   {"metamorphStrlstMode", "equivlist"},
   /*{"groupbymem", "1"}, produces an error */
   {"minWordLen", "255"},
   {"suffixProc", "1"},
   {"rebuild", "0"},
   {"intersects", "-1"},
   {"hyphenPhrase", "1"},
   {"wordc", "[\\alpha\\']"},
   {"langc", "[\\alpha\\'\\-]"},
   {"withinMode", "word span"},
   {"phrasewordproc", "last"},
   {"defSuffRm", "1"},
   {"eqPrefix", "builtin"},
   {"exactPhrase", "0"},
   /* {"withinProc", "1"}, produces error */
   {"likepProximity", "500"},
   {"likepLeadBias", "500"},
   {"likepOrder", "500"},
   {"likepDocFreq", "500"},
   {"likepTblFreq", "500"},
   {"likepRows", "100"},
   /* INDEX_VEC: cap on the candidate pool returned per LIKEV before
    * SQL-side filtering / vecdist re-ranking.  Default 1000. */
   {"likevRows", "1000"},
   /* INDEX_VEC: per-query HNSW expansion factor (recall/latency knob).
    * 0 = inherit the index's ef_construction (the build-time setting). */
   {"likevEf", "0"},
   /* INDEX_VEC ivfpq: per-query nprobe (number of inverted lists scanned).
    * Higher = more recall, more time.  0 = auto (compute from nlist:
    * max(8, nlist/128)); >0 = explicit override. */
   {"likevPqNprobe", "0"},
   /* INDEX_VEC ivfpq: hard cap on training-sample count at CREATE INDEX.
    * Larger needs more disk for the temp .train.tmp file but improves
    * codebook quality on large data. */
   {"vecPqMaxTrainSamples", "1000000"},
   /* INDEX_VEC ivfpq: fractional over-fetch on LIKEV k to absorb live-
    * mask exclusions.  0.10 = 10%; bump for delete-heavy indexes. */
   {"vecPqOverFetchPad", "0.10"},
   {"likepMode", "1"},
   {"likepAllMatch", "0"},
   {"likepObeyIntersects", "0"},
   {"likepInfThresh", "0"},
   /* {"likepIndexThresh", "-1"}, ??? */
   /*{"indexSpace", ""},
   {"indexBlock", ""}, */
   {"meter", "on"},
   {"indexmeter", "off"},
/*   {"addExp", ""},
   {"delExp", ""},
   {"addIndexTmp", ""}
   {"delIndexTmp", ""}, */
   {"indexValues", "splitStrlst"},
   {"btreeThreshold", "50"},
   {"maxLinearRows", "1000"},
   {"likerRows", "1000"},
   {"indexAccess", "0"},
   {"indexMmap", "1"},
   {"indexReadBufSz", "64KB"},
   {"indexWriteBufSz", "128KB"},
   {"indexMmapBufSz", "0"},
   {"indexSlurp", "1"},
   {"indexAppend", "1"},
   {"indexWriteSplit", "1"},
   {"indexBtreeExclusive", "1"},
   {"indexVersion", "2"},
   {"mergeFlush", "1"},
   {"tableReadBufSz", "16KB"},
   /*{"tableSpace", ""},*/
   {"dateFmt", ""},
   /*{"timeZone", ""},
   {"locale", ""}, */
   /*{"indirectSpace", ""},*/
   {"triggerMode", "0"},
   {"paramChk", "1"},
   /* {"message", "1"}, segfault */
   {"varcharToStrlstMode", "json"},
   {"strlstToVarcharMode", "json"},
   {"multiValueToMultiRow", "0"},
   {"inMode", "subset"},
   {"hexifyBytes", "0"},
   {"unalignedBufferWarning", "1"},
   {"nullOutputString", "NULL"},
   /* {"validateBtrees", ""}, no idea */
   {"querySettings", "defaults"},
   {"qMaxWords", "1000"},
   {NULL, NULL}
};
int nnoiseList=181;
char *noiseList[] = {
    "a","about","after","again","ago","all","almost","also","always","am",
    "an","and","another","any","anybody","anyhow","anyone","anything","anyway","are",
    "as","at","away","back","be","became","because","been","before","being",
    "between","but","by","came","can","cannot","come","could","did","do",
    "does","doing","done","down","each","else","even","ever","every","everyone",
    "everything","for","from","front","get","getting","go","goes","going","gone",
    "got","gotten","had","has","have","having","he","her","here","him",
    "his","how","i","if","in","into","is","isn't","it","just",
    "last","least","left","less","let","like","make","many","may","maybe",
    "me","mine","more","most","much","my","myself","never","no","none",
    "not","now","of","off","on","one","onto","or","our","ourselves",
    "out","over","per","put","putting","same","saw","see","seen","shall",
    "she","should","so","some","somebody","someone","something","stand","such","sure",
    "take","than","that","the","their","them","then","there","these","they",
    "this","those","through","till","to","too","two","unless","until","up",
    "upon","us","very","was","we","went","were","what","what's","whatever",
    "when","where","whether","which","while","who","whoever","whom","whose","why",
    "will","with","within","without","won't","would","wouldn't","yet","you","your", ""
};
//todo revisit "ul" and capitulating -> capitalism
int nsuffixList=91;
char *suffixList[] = {
    "'","anced","ancer","ances","atery","enced","encer","ences","ibler","ment",
    "ness","tion","able","less","sion","ance","ious","ible","ence","ship",
    "ical","ward","ally","atic","aged","ager","ages","ated","ater","ates",
    "iced","icer","ices","ided","ider","ides","ised","ises","ived","ives",
    "ized","izer","izes","ncy","ing","ion","ity","ous","ful","tic",
    "ish","ial","ory","ism","age","ist","ate","ary","ual","ize",
    "ide","ive","ier","ess","ant","ise","ily","ice","ery","ent",
    "end","ics","est","ed","red","res","ly","er","al","at",
    "ic","ty","ry","en","nt","re","th","es","ul","s", ""
};

int nsuffixEquivsList=4;
char *suffixEquivsList[] = {
    "'","s","ies", ""
};

int nprefixList=29;
char *prefixList[] = {
    "ante","anti","arch","auto","be","bi","counter","de","dis","em",
    "en","ex","extra","fore","hyper","in","inter","mis","non","post",
    "pre","pro","re","semi","sub","super","ultra","un", ""
};

char **copylist(char **list, int len){
    int i=0;

    char **nl=NULL; /* the list to be populated */

    REMALLOC(nl, sizeof(char*) * len);

    while (i<len)
    {
        nl[i]=strdup(list[i]);
        i++;
    }

    return nl;
}

static int sql_defaults(duk_context *ctx, TEXIS *tx, char *errbuf)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;

    int i=0;
    char **props;

    logandclearmsgbuf(ctx);

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        sprintf(errbuf,"sql open");
        return -1;
    }

    props = prop_defaults[i];

    while (props[0])
    {
        if(setprop(ddic, props[0], props[1] )==-1)
        {
            sprintf(errbuf, "sql reset");
            return -2;
        }
        i++;
        props = prop_defaults[i];
    }

    if(!defnoise)
    {
        globalcp->noise=(byte**)copylist(noiseList, nnoiseList);
        defnoise=1;
    }
    if(!defsuffix)
    {
        globalcp->suffix=(byte**)copylist(suffixList, nsuffixList);
        defsuffix=1;
    }
    if(!defsuffixeq)
    {
        globalcp->suffixeq=(byte**)copylist(suffixEquivsList, nsuffixEquivsList);
        defsuffixeq=1;
    }
    if(!defprefix)
    {
        globalcp->prefix=(byte**)copylist(prefixList, nprefixList);
        defprefix=1;
    }

    return 0;
}

/* ============================================================
 * Embed plumbing (Step 3 v1: main-side fast path only)
 *
 * sql.set({llamaEmbed:"/path/..."}) flow:
 *   1. require("rampart-llamacpp")     — load the .so via rampart's
 *                                        module resolver (RTLD_GLOBAL)
 *   2. dlsym the rp_embed_* C exports (once per process)
 *   3. rp_embed_load(path)              — gives opaque handle; cached
 *                                         by path inside rampart-llamacpp
 *   4. TXregisterEmbedFunc(main_embed_callback, g_embed_handle)
 *      so the SQL `embed()` builtin will call into rampart-llamacpp.
 *   5. Attach the JS embed module to the Sql object as a hidden symbol
 *      so it stays GC-rooted for the connection's lifetime.
 *
 * The "main fast path" is everything that runs texis in-process (no
 * helper).  Step 4 will add the helper round-trip protocol.
 * ============================================================ */

#include <dlfcn.h>

static size_t main_embed_callback(void *ud,
                                  const char *text, size_t tlen,
                                  int kind, const char *title, size_t title_len,
                                  float **out_vec)
{
    size_t r = 0;
    size_t clen = 0;
    char *composed;

    (void)ud;   /* model is the per-connection active handle, not the reg-time ud */
    if (!g_active_embed_handle) return 0;

    /* Apply the model's retrieval prompt per the caller's kind (query /
     * document / raw).  NULL = nothing to apply, embed verbatim -- so a
     * model without prompts is byte-identical to older releases.  The
     * engine's text cache keys on the composed bytes. */
    /* TXEMBED_IMAGE is a MODALITY, not a prompt: `text` is an image file
     * path for an image encoder.  Only CLIP has one -- every other engine
     * must fail here, because embedding the path STRING would store a
     * perfectly plausible, meaningless vector and silently poison the
     * column. */
    if (kind == TXEMBED_IMAGE) {
        if (g_active_embed_engine != EMBED_ENGINE_CLIP) {
            putmsg(MERR + UGE, "embed",
                   "embed(..., 'image') needs an image encoder, but this "
                   "connection's embed engine is %s -- use sql.set({clipEmbed:...})",
                   embed_engine_name(g_active_embed_engine));
            embed_fail_latch("embed(..., 'image') needs an image encoder, but "
                             "this connection's embed engine is %s -- use "
                             "sql.set({clipEmbed:...})",
                             embed_engine_name(g_active_embed_engine));
            return 0;
        }
        if (!g_rp_clip_embed_image_path) return 0;
        /* text is NUL-terminated here (texis hands a C string); the ABI
         * takes a path, not a length. */
        r = g_rp_clip_embed_image_path(g_active_embed_handle, text, out_vec);
        if (!r && tlen)
            embed_fail_latch("embed(..., 'image'): could not read or decode "
                             "the image '%.*s'", (int)(tlen > 120 ? 120 : tlen), text);
        return r;
    }

    composed = embed_compose_text(kind, title, title_len, text, tlen, &clen);
    if (composed) { text = composed; tlen = clen; }

    switch (g_active_embed_engine) {
    case EMBED_ENGINE_LLAMACPP:
        if (g_rp_embed_text)
            r = g_rp_embed_text(g_active_embed_handle, text, tlen, out_vec);
        break;
    case EMBED_ENGINE_ONNX:
        if (g_rp_onnx_embed_text)
            r = g_rp_onnx_embed_text(g_active_embed_handle, text, tlen, out_vec);
        break;
    case EMBED_ENGINE_CLIP:
        /* text tower -- shares the vector space with the image tower */
        if (g_rp_clip_embed_text)
            r = g_rp_clip_embed_text(g_active_embed_handle, text, tlen, out_vec);
        break;
    default:
        break;
    }
    free(composed);
    /* Latch only when there WAS something to embed: an empty input
     * legitimately produces a null vector (see g_embed_failed). */
    if (!r && tlen)
        embed_fail_latch("embed(): the %s embed model returned no vector",
                         embed_engine_name(g_active_embed_engine));
    return r;
}

/* Doc-level (chunked) variant: powers texis's chunkembed() / chunkavg()
 * / chunkcoherence() scalars.  Requests whichever of vecs / avg / coh /
 * spans the scalar wants; the langtools embedder computes them in one
 * run and caches by text, so different scalars on the same text share
 * it.  The engine's {start,end,n_tokens} spans convert to texis's
 * {start,end}; a spans request that can't be satisfied fails the whole
 * call (chunkembed() must never silently store without spans). */
static size_t main_embed_doc_callback(void *ud,
                                      const char *text, size_t tlen,
                                      const char *prefix, size_t plen,
                                      float **out_vecs, size_t *out_k,
                                      float **out_avg, float *out_coh,
                                      TXchunkSpan **out_spans)
{
    size_t r = 0;
    size_t k = 0;
    size_t cplen = 0;
    char *cpfx;
    rp_embed_span_t *es = NULL;

    (void)ud;
    if (out_spans) *out_spans = NULL;
    if (!g_active_embed_handle) return 0;

    /* CLIP has no chunking: an image is one vector and its text tower is
     * ~77 tokens, so there is nothing to split.  Fail loudly instead of
     * letting chunkembed()/chunkavg()/chunkcoherence() appear to work. */
    if (g_active_embed_engine == EMBED_ENGINE_CLIP) {
        putmsg(MERR + UGE, "chunkembed",
               "chunkembed()/chunkavg()/chunkcoherence() are not supported "
               "with clipEmbed: CLIP produces one vector per image or short "
               "text and has no chunking.  Use embed(...) instead");
        embed_fail_latch("chunkembed()/chunkavg()/chunkcoherence() are not "
                         "supported with clipEmbed: CLIP has no chunking.  "
                         "Use embed(...) instead");
        return 0;
    }

    /* chunkembed()'s prefix arg is the per-document TITLE.  Fold the
     * model's document prompt around it (template {title} slot, or
     * prompt-then-title); NULL = no prompts, the title passes through
     * exactly as before.  The engine token-injects the result into each
     * chunk window, and its doc cache keys on (text, composed prefix). */
    cpfx = embed_compose_doc_prefix(prefix, plen, 0, &cplen);
    if (cpfx) { prefix = cpfx; plen = cplen; }

    switch (g_active_embed_engine) {
    case EMBED_ENGINE_LLAMACPP:
        if (g_rp_embed_doc)
            r = g_rp_embed_doc(g_active_embed_handle, text, tlen, prefix, plen,
                               out_vecs, &k, out_avg, out_coh,
                               out_spans ? &es : NULL);
        break;
    case EMBED_ENGINE_ONNX:
        if (g_rp_onnx_embed_doc)
            r = g_rp_onnx_embed_doc(g_active_embed_handle, text, tlen, prefix, plen,
                                    out_vecs, &k, out_avg, out_coh,
                                    out_spans ? &es : NULL);
        break;
    default:
        break;
    }
    free(cpfx);
    if (out_k) *out_k = k;
    if (r && out_spans) {
        TXchunkSpan *ts = NULL;
        size_t i;
        if (es && k && (ts = (TXchunkSpan *)malloc(k * sizeof(TXchunkSpan)))) {
            for (i = 0; i < k; i++) {
                ts[i].start = es[i].start;
                ts[i].end   = es[i].end;
            }
            *out_spans = ts;
        } else {
            /* engine gave no spans, or oom converting: fail the whole
             * call rather than let the caller store a spanless value.
             * Latch it -- texis turns a failed scalar into a NULL field
             * and carries on, so without this an ingest run would store
             * vector-less rows and never throw (see the latch above). */
            const char *why = (es && k) ? "out of memory converting chunk spans"
                                        : "embed engine returned vectors but "
                                          "no chunk spans";
            putmsg(MERR + FRE, "chunkembed", "%s", why);
            embed_fail_latch("chunkembed(): %s", why);
            if (out_vecs && *out_vecs) { free(*out_vecs); *out_vecs = NULL; }
            if (out_avg  && *out_avg)  { free(*out_avg);  *out_avg  = NULL; }
            if (out_k) *out_k = 0;
            r = 0;
        }
    }
    free(es);
    return r;
}

/* Chunk-spans variant: powers abstract()'s vec-seeded snippet mode.
 * Tokenize + chunk only (no model run) — µs-cheap per row.  Converts
 * the engine's {start,end,n_tokens} spans to texis's {start,end}. */
static size_t main_chunk_spans_callback(void *ud,
                                        const char *text, size_t tlen,
                                        TXchunkSpan **out_spans)
{
    rp_embed_span_t *es = NULL;
    size_t k = 0;
    (void)ud;
    if (out_spans) *out_spans = NULL;
    if (!g_active_embed_handle || !out_spans) return 0;
    switch (g_active_embed_engine) {
    case EMBED_ENGINE_LLAMACPP:
        if (!g_rp_embed_spans) return 0;
        k = g_rp_embed_spans(g_active_embed_handle, text, tlen, &es);
        break;
    case EMBED_ENGINE_ONNX:
        if (!g_rp_onnx_embed_spans) return 0;
        k = g_rp_onnx_embed_spans(g_active_embed_handle, text, tlen, &es);
        break;
    default:
        return 0;
    }
    if (k == 0 || !es) { free(es); return 0; }
    TXchunkSpan *ts = (TXchunkSpan *)malloc(k * sizeof(TXchunkSpan));
    if (!ts) { free(es); return 0; }
    for (size_t i = 0; i < k; i++) {
        ts[i].start = es[i].start;
        ts[i].end   = es[i].end;
    }
    free(es);
    *out_spans = ts;
    return k;
}

/* require() a langtools engine module by its CANONICAL name --
 * "rampart-llamacpp", "rampart-onnx", "rampart-clip".  Only the plain
 * name is ever tried: the packager/installer symlinks it to whichever
 * variant is installed (cpu / cu11 / cu12 / cu13), so guessing at
 * variant-suffixed names here would just be a stale second source of
 * truth.  require() handles path resolution; no absolute paths needed.
 * Leaves the module object on the stack on success. */
static int try_require_engine(duk_context *ctx, const char *name)
{
    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "require");
    duk_remove(ctx, -2);
    duk_push_string(ctx, name);
    if (duk_pcall(ctx, 1) == 0) return 0;     /* module on stack */
    duk_pop(ctx);                              /* drop the error */
    return -1;
}

/* Returns 0 on success, fills errbuf and returns -1 on failure.
 * Resolves rp_embed_* via dlsym, attempting require() if not yet loaded. */
static int setup_llamacpp_main(duk_context *ctx, const char *path,
                               char *errbuf, size_t errbuflen)
{
    /* Resolve symbols once per process. */
    if (!g_rp_embed_load) {
        /* If the user (or a prior sql.set) already loaded the module,
         * the symbols are already in RTLD_DEFAULT — skip require(). */
        g_rp_embed_load = dlsym(RTLD_DEFAULT, "rp_embed_load");

        if (!g_rp_embed_load) {
            if (try_require_engine(ctx, "rampart-llamacpp") != 0) {
                snprintf(errbuf, errbuflen,
                         "sql.set llamaEmbed: cannot load rampart-llamacpp "
                         "(require(\"rampart-llamacpp\") failed -- is "
                         "rampart-langtools installed?)");
                return -1;
            }
            /* Stash the module object on the Sql `this` so its .so
             * refcount stays alive for the Sql's lifetime. */
            duk_push_this(ctx);
            duk_dup(ctx, -2);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("llamacpp_module"));
            duk_pop_2(ctx);  /* this, module */

            g_rp_embed_load = dlsym(RTLD_DEFAULT, "rp_embed_load");
        }

        g_rp_embed_text = dlsym(RTLD_DEFAULT, "rp_embed_text");
        g_rp_embed_dim  = dlsym(RTLD_DEFAULT, "rp_embed_dim");
        g_rp_embed_set_per_thread = dlsym(RTLD_DEFAULT, "rp_embed_set_per_thread");
        g_rp_embed_doc   = dlsym(RTLD_DEFAULT, "rp_embed_doc");
        g_rp_embed_spans = dlsym(RTLD_DEFAULT, "rp_embed_spans");
        g_rp_embed_set_cache_cap = dlsym(RTLD_DEFAULT, "rp_embed_set_cache_cap");
        /* iface marker: present only in modules whose rp_embed_doc has
         * the v3 (per-doc prefix) signature -- a stale .so must fail
         * HERE, loudly, not crash on a signature mismatch. */
        void *iface_v3 = dlsym(RTLD_DEFAULT, "rp_embed_iface_v3");
        if (!g_rp_embed_load || !g_rp_embed_text || !g_rp_embed_dim ||
            !g_rp_embed_set_per_thread || !g_rp_embed_doc ||
            !g_rp_embed_spans || !g_rp_embed_set_cache_cap || !iface_v3) {
            snprintf(errbuf, errbuflen,
                     "sql.set llamaEmbed: rampart-llamacpp loaded but "
                     "rp_embed_* C symbols are missing (version mismatch?)");
            /* NULL every pointer: a partial set would let later calls
             * (e.g. sql.set({llamaEmbedPerThread}) gating on
             * g_rp_embed_set_per_thread) reach into the rejected .so */
            g_rp_embed_load = NULL;
            g_rp_embed_text = NULL;
            g_rp_embed_dim  = NULL;
            g_rp_embed_set_per_thread = NULL;
            g_rp_embed_doc   = NULL;
            g_rp_embed_spans = NULL;
            g_rp_embed_set_cache_cap = NULL;
            return -1;
        }
    }

    /* Load model.  rp_embed_load caches by path; repeat with same path
     * just bumps refcount. */
    char loaderr[256] = {0};
    void *h = g_rp_embed_load(path, loaderr, sizeof loaderr);
    if (!h) {
        snprintf(errbuf, errbuflen,
                 "sql.set llamaEmbed: model load failed: %s",
                 loaderr[0] ? loaderr : "unknown");
        return -1;
    }
    g_embed_handle = h;
    /* Thread-local capture of the just-loaded model, so h_set() can attach it to
     * THIS connection's handle without racing other threads' sql.set calls. */
    g_last_loaded_embed_handle = h;

    /* Register the callback so texis's embed() builtin will fire.  The ud is
     * ignored now (main_embed_callback reads the per-connection g_active_embed_handle);
     * registration just installs the parent-process callback once. */
    TXregisterEmbedFunc(main_embed_callback, g_embed_handle);
    TXregisterEmbedDocFunc(main_embed_doc_callback, NULL);
    TXregisterChunkSpansFunc(main_chunk_spans_callback, NULL);
    return 0;
}

/* ============================================================
 * ONNX embed plumbing — parallel to setup_llamacpp_main.
 * ============================================================ */

/* rampart-onnx's rp_onnx_embed_load takes a rich opts struct, unlike
 * llamacpp which takes just a path.  Callers pass model path + fully-
 * populated opts (parsed from the sql.set({onnxEmbed:{...}}) object).
 * Returns 0 on success, fills errbuf and returns -1 on failure. */
static int setup_onnx_main(duk_context *ctx,
                           const char *model_path,
                           const rp_onnx_embed_opts *opts,
                           char *errbuf, size_t errbuflen)
{
    /* Resolve symbols once per process. */
    if (!g_rp_onnx_embed_load) {
        g_rp_onnx_embed_load = dlsym(RTLD_DEFAULT, "rp_onnx_embed_load");
        if (!g_rp_onnx_embed_load) {
            if (try_require_engine(ctx, "rampart-onnx") != 0) {
                snprintf(errbuf, errbuflen,
                         "sql.set onnxEmbed: cannot load rampart-onnx "
                         "(require(\"rampart-onnx\") failed -- is "
                         "rampart-langtools installed?)");
                return -1;
            }
            /* Stash the JS module handle on `this` so the .so stays refcounted. */
            duk_push_this(ctx);
            duk_dup(ctx, -2);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("onnx_module"));
            duk_pop_2(ctx);  /* this, module */
            g_rp_onnx_embed_load = dlsym(RTLD_DEFAULT, "rp_onnx_embed_load");
        }
        g_rp_onnx_embed_text    = dlsym(RTLD_DEFAULT, "rp_onnx_embed_text");
        /* _dim and _release are resolved and REQUIRED below but never
         * called from this file: they complete the version probe (a module
         * missing any of the set is stale) -- _dim mirrors the unused
         * llama g_rp_embed_dim; onnx handles are process-lifetime so
         * _release has no call site by design. */
        g_rp_onnx_embed_dim     = dlsym(RTLD_DEFAULT, "rp_onnx_embed_dim");
        g_rp_onnx_embed_release = dlsym(RTLD_DEFAULT, "rp_onnx_embed_release");
        g_rp_onnx_embed_doc     = dlsym(RTLD_DEFAULT, "rp_onnx_embed_doc");
        g_rp_onnx_embed_spans   = dlsym(RTLD_DEFAULT, "rp_onnx_embed_spans");
        g_rp_onnx_embed_set_cache_cap = dlsym(RTLD_DEFAULT, "rp_onnx_embed_set_cache_cap");
        if (!g_rp_onnx_embed_load || !g_rp_onnx_embed_text
            || !g_rp_onnx_embed_dim  || !g_rp_onnx_embed_release
            || !g_rp_onnx_embed_doc  || !g_rp_onnx_embed_spans
            || !g_rp_onnx_embed_set_cache_cap) {
            snprintf(errbuf, errbuflen,
                     "sql.set onnxEmbed: rampart-onnx loaded but "
                     "rp_onnx_embed_* C symbols are missing (version mismatch?)");
            /* NULL every pointer (see the llama twin above) */
            g_rp_onnx_embed_load    = NULL;
            g_rp_onnx_embed_text    = NULL;
            g_rp_onnx_embed_dim     = NULL;
            g_rp_onnx_embed_release = NULL;
            g_rp_onnx_embed_doc     = NULL;
            g_rp_onnx_embed_spans   = NULL;
            g_rp_onnx_embed_set_cache_cap = NULL;
            return -1;
        }
    }

    /* rampart-onnx used to dlsym rampart-sentencepiece for the C
     * embed path; that dependency was removed in the 2026-07 rework
     * (tokenizers now come from bundled onnxruntime-extensions), so
     * we don't require SP here anymore. */

    char loaderr[256] = {0};
    void *h = g_rp_onnx_embed_load(model_path, opts, loaderr, sizeof loaderr);
    if (!h) {
        snprintf(errbuf, errbuflen,
                 "sql.set onnxEmbed: load failed: %s",
                 loaderr[0] ? loaderr : "unknown");
        return -1;
    }
    g_last_loaded_onnx_handle = h;

    /* Same callback registration as llamacpp; the dispatcher inside
     * main_embed_callback picks the engine based on g_active_embed_engine. */
    TXregisterEmbedFunc(main_embed_callback, NULL);
    TXregisterEmbedDocFunc(main_embed_doc_callback, NULL);
    TXregisterChunkSpansFunc(main_chunk_spans_callback, NULL);
    return 0;
}

/* ------------------------- clipEmbed --------------------------------
 * sql.set({clipEmbed: "/path/model.gguf"}) or {clipEmbed:{model:"..."}}.
 * CLIP puts images and text in ONE vector space, so a table of image
 * vectors is searched with a text query:
 *
 *   insert into images values (?, ?, embed(?, 'image'));  -- ? = a PATH
 *   select Path from images where Vec likev 'a dog on a beach';
 *
 * Only the string forms cross the SQL/helper boundary (a query, or a
 * path) -- both tiny.  Image BYTES are still not accepted here, but the
 * reason is no longer the wire: payloads now chunk, so size is not a
 * limit.  What remains is that the bytes would travel parent->helper as
 * a parameter and then straight back helper->parent to reach the model,
 * for no gain.  rp_clip_embed_image() is ready on the langtools side if
 * that round trip is ever worth accepting; until then embed the bytes
 * in JS and bind the vector directly. */
static int peek_clipembed_setting(duk_context *ctx, const char **model_path_out)
{
    const char *p = NULL;

    if (!duk_get_prop_string(ctx, -1, "clipembed")) {
        duk_pop(ctx);
        return 0;
    }
    if (duk_is_string(ctx, -1)) {
        p = duk_get_string(ctx, -1);
    } else if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) &&
               !duk_is_function(ctx, -1)) {
        /* object form, for parity with llamaEmbed/onnxEmbed */
        if (duk_get_prop_string(ctx, -1, "model") && duk_is_string(ctx, -1))
            p = duk_get_string(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);
    if (!p || !p[0]) return 0;
    *model_path_out = p;
    return 1;
}

static int setup_clip_main(duk_context *ctx, const char *model_path,
                           char *errbuf, size_t errbuflen)
{
    /* Resolve symbols once per process (mirrors setup_onnx_main). */
    if (!g_rp_clip_embed_load) {
        g_rp_clip_embed_load = dlsym(RTLD_DEFAULT, "rp_clip_embed_load");
        if (!g_rp_clip_embed_load) {
            if (try_require_engine(ctx, "rampart-clip") != 0) {
                snprintf(errbuf, errbuflen,
                         "sql.set clipEmbed: cannot load rampart-clip "
                         "(require(\"rampart-clip\") failed -- is "
                         "rampart-langtools installed?)");
                return -1;
            }
            /* Stash the JS module handle on `this` so the .so stays refcounted. */
            duk_push_this(ctx);
            duk_dup(ctx, -2);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("clip_module"));
            duk_pop_2(ctx);  /* this, module */
            g_rp_clip_embed_load = dlsym(RTLD_DEFAULT, "rp_clip_embed_load");
        }
        g_rp_clip_iface_v1         = dlsym(RTLD_DEFAULT, "rp_clip_iface_v1");
        g_rp_clip_embed_text       = dlsym(RTLD_DEFAULT, "rp_clip_embed_text");
        g_rp_clip_embed_image_path = dlsym(RTLD_DEFAULT, "rp_clip_embed_image_path");
        g_rp_clip_embed_image      = dlsym(RTLD_DEFAULT, "rp_clip_embed_image");
        g_rp_clip_embed_dim        = dlsym(RTLD_DEFAULT, "rp_clip_embed_dim");
        g_rp_clip_has_text         = dlsym(RTLD_DEFAULT, "rp_clip_has_text");
        g_rp_clip_has_vision       = dlsym(RTLD_DEFAULT, "rp_clip_has_vision");
        g_rp_clip_embed_release    = dlsym(RTLD_DEFAULT, "rp_clip_embed_release");
        if (!g_rp_clip_embed_load  || !g_rp_clip_iface_v1
            || !g_rp_clip_embed_text || !g_rp_clip_embed_image_path
            || !g_rp_clip_embed_image
            || !g_rp_clip_embed_dim  || !g_rp_clip_has_text
            || !g_rp_clip_has_vision || !g_rp_clip_embed_release) {
            snprintf(errbuf, errbuflen,
                     "sql.set clipEmbed: rampart-clip loaded but "
                     "rp_clip_* C symbols are missing (version mismatch?)");
            g_rp_clip_embed_load       = NULL;
            g_rp_clip_iface_v1         = NULL;
            g_rp_clip_embed_text       = NULL;
            g_rp_clip_embed_image_path = NULL;
            g_rp_clip_embed_dim        = NULL;
            g_rp_clip_has_text         = NULL;
            g_rp_clip_has_vision       = NULL;
            g_rp_clip_embed_release    = NULL;
            return -1;
        }
        if (g_rp_clip_iface_v1() != 1) {
            snprintf(errbuf, errbuflen,
                     "sql.set clipEmbed: rampart-clip ABI version %d, "
                     "expected 1 (rebuild rampart-langtools)",
                     g_rp_clip_iface_v1());
            g_rp_clip_embed_load = NULL;
            return -1;
        }
    }

    char loaderr[256] = {0};
    void *h = g_rp_clip_embed_load(model_path, loaderr, sizeof loaderr);
    if (!h) {
        snprintf(errbuf, errbuflen, "sql.set clipEmbed: %s",
                 loaderr[0] ? loaderr : "unknown");
        return -1;
    }
    /* A vision-only model (a llava-style mmproj) cannot serve
     * `likev '<text>'` at all -- say so now rather than at first query. */
    if (!g_rp_clip_has_text(h)) {
        snprintf(errbuf, errbuflen,
                 "sql.set clipEmbed: '%s' has no text encoder, so text "
                 "queries (likev '<string>') cannot work with it",
                 model_path);
        g_rp_clip_embed_release(h);
        return -1;
    }
    if (!g_rp_clip_has_vision(h))
        snprintf(errbuf, errbuflen,        /* warning only: text still works */
                 "sql.set clipEmbed: note: '%s' has no image encoder; "
                 "embed(?, 'image') will fail", model_path);
    g_last_loaded_clip_handle = h;

    TXregisterEmbedFunc(main_embed_callback, NULL);
    /* Deliberately NOT registering the doc/spans callbacks: CLIP has no
     * chunking, so chunkembed()/chunkavg()/abstract(...,Vec) must fail
     * rather than silently produce something.  If another engine on this
     * process registered them, main_embed_doc_callback rejects a clip
     * connection explicitly. */
    return 0;
}

// returns -1 for bad option, -2 for setprop error, 0 for ok, 1 for ok with return value
/* Compile an index word expression the way the indexer will, so a bad one
 * is rejected by sql.set() instead of being stored and only failing (or
 * silently never matching) when an index is later built.  setprop()
 * stores the expression list verbatim; it is openrlex()/rlex_addexp() that
 * compiles it, with TXrexSyntax_Rex -- which is what openrex() is given
 * here, so this accepts and rejects exactly what the indexer would.
 *
 * Two prefixed forms are passed through unchecked:
 *   \<re2\>...   an RE2 expression.  Rare in practice, and whether it can
 *                be compiled at all depends on whether RE2 was built in,
 *                so judging it is deliberately left to texis.
 *   \<nomatch\>  meaningful only alongside other expressions, i.e. exactly
 *                the rexlex context these end up in; openrex() rejects it
 *                standalone by design, so compiling it here would reject a
 *                legal value.
 *
 * Returns 1 if the expression is usable, 0 if not. */
static int expr_compiles(const char *expr)
{
    FFS *fs;

    if(!expr)
        return 0;

    if(expr[0]=='\\' && expr[1]=='<')
    {
        if(!strncmp(expr+2, "re2\\>",     5)) return 1;
        if(!strncmp(expr+2, "nomatch\\>", 9)) return 1;
    }

    if( !(fs = openrex((byte *)expr, TXrexSyntax_Rex)) )
        return 0;

    closerex(fs);
    return 1;
}

/* setprop() properties whose value texis converts with atoi(), atol(),
 * atof(), strtol() or strtod() without checking that anything was
 * consumed -- so a non-numeric string silently becomes 0 and the setting
 * is quietly wrong (`sql.set({qmaxsets:"ten"})` set it to zero and
 * reported success).  Swept from extern/texis/texisapi/setprop.c.
 *
 * Deliberately NOT listed, because each also accepts a keyword or boolean
 * form that a numeric test would wrongly reject -- setprop() tests them
 * with strcmpi(value,...) or TXgetBooleanOrInt():
 *
 *     exactphrase          also "ignorewordposition", true/false
 *     likepobeyintersects  also boolean
 *     phrasewordproc       also boolean
 *     querysettings        also "defaults" / "texis5defaults"
 *
 * MUST remain sorted: searched with bsearch(). */
static const char * const numeric_props[] = {
    "allineardict", "btreecachesize", "btreedump", "btreeoptimizeoff",
    "btreeoptimizeon", "btreethreshold", "cleanupwait", "dbcleanupverbose",
    "debugmalloc", "dedupmultiitemresults", "dropwordmode", "eastpositive",
    "enablesubsetintersect", "findselloopcheck", "fldmathverbosemaxvaluesize",
    "indexappend", "indexblock", "indexbtreeexclusive", "indexchunk",
    "indexdump", "indexmaxsingle", "indexminsublen", "indexmmap",
    "indexslurp", "indextrace", "indexversion", "indexwritesplit",
    "infpercent", "infthresh", "kdbfiostats", "kdbfoptimizeoff",
    "kdbfoptimizeon", "kdbfverify", "likepmode", "likeprows", "likeptime",
    "likerpercent", "likerrows", "likevef", "likevpqnprobe", "likevrows",
    "lockbatchrows", "lockbatchtime", "locksleepdecrement",
    "locksleepincrement", "locksleepmaxtime", "locksleepmethod",
    "locksleeptime", "matchmode", "maxindextext", "maxlinearrows",
    "maxrows", "mdparmodifyterms", "mergeflush", "minwordlen",
    "predopttype", "qmaxsets", "qmaxsetwords", "qmaxterms", "qmaxwords",
    "qminprelen", "qminwordlen", "ramlimit", "ramrows",
    "strlstrelopvarcharpromoteviacreate", "traceddcache", "traceidx",
    "traceindex", "tracekdbf", "tracemetamorph", "tracerppm",
    "triggermode", "uniqnewlist", "usestringcomparemodeforstrlst",
    "vecpqmaxtrainsamples", "vecpqoverfetchpad", "verbose", "verifysingle",
    "wildoneword", "wildsingle", "wildsufmatch"
};

static int cmp_str_ptr(const void *a, const void *b)
{
    return strcmp(*(const char * const *)a, *(const char * const *)b);
}

static int prop_is_numeric(const char *prop)
{
    if(!prop) return 0;
    return bsearch(&prop, numeric_props,
                   sizeof(numeric_props)/sizeof(numeric_props[0]),
                   sizeof(numeric_props[0]), cmp_str_ptr) != NULL;
}

/* Whether `s' is entirely a number.  JS Numbers and Booleans have already
 * been stringified ("10", "10.5", "1", "0") by the time we get here, so
 * this only ever rejects a string the script actually wrote. */
static int is_numeric_string(const char *s)
{
    char *end;

    if(!s || !*s)
        return 0;

    (void) strtod(s, &end);
    if(end == s)
        return 0;
    while(*end && isspace((unsigned char)*end))
        end++;

    return *end == '\0';
}

static int sql_set(duk_context *ctx, TEXIS *tx, char *errbuf)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    const char *val="";
    int added_ret_obj=0, ret=0;
    char *rlsts[]={"noiseList","suffixList","suffixEquivsList","prefixList"};

    /* likevCache is scoped to one settings-processing run: the object we
     * are about to walk is the connection's MERGED settings, so if it
     * carries likevCache the property handler below re-arms this; if not,
     * a stale cap from another connection's set() on this thread must not
     * leak into this one.  Applied after we return (rp_texis_set /
     * h_set), against the engine key present in the same merged object. */
    g_doccache_cap_pending = -1;

    logandclearmsgbuf(ctx);

    if(!tx)
    {
        rp_log_error(ctx);
        snprintf(errbuf, msgbufsz, "Texis setprop failed\n%s", finfo->errmap);
        goto return_neg_two;
    }

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        snprintf(errbuf, msgbufsz, "sql.set open: %s", finfo->errmap); /* F13 */
        goto return_neg_two;
    }

    if((ret=sql_defaults(ctx, tx, errbuf)))
        return ret;

    /**** Reapply indextmplst and explst if it was modified.
     *
     * The texis expression/indextmp lists are PROCESS-GLOBAL, so the
     * saved list must REPLACE the global list, not append to it: clear
     * the global list first, then re-add the saved entries.  (The old
     * append-without-clear here compounded per set() call -- each call
     * re-added the whole list on top of itself, geometrically -- and
     * the corrupted global list was then snapshotted back as the new
     * saved list.  See also rp_texis_set(): add/del ops are stripped
     * from the persistent settings after application, since list STATE
     * lives in these snapshots, not in replayed operations.) */
    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("indlist")))
    {
        int i=0, len = duk_get_length(ctx, -1);
        const char *val;
        char **glst;

        /* clear the global indextmp list (delete index 0 until empty;
           exp_del warns+fails on an empty list, so check first) */
        while( (glst=TXgetglobalindextmp()) != NULL &&
               glst[0] && strlen(glst[0]) )
        {
            logandclearmsgbuf(ctx);
            if(setprop(ddic, "delindextmp", "0")==-1)
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13 */
                goto return_neg_two;
            }
        }
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            logandclearmsgbuf(ctx);
            if(setprop(ddic, "addindextmp", (char*)val )==-1)
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);//list or undef

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist")))
    {
        int i=0, len = duk_get_length(ctx, -1);
        const char *val;
        char **glst;

        /* clear the global expression list (the old single delexp "0"
           removed exactly one entry, leaving the rest to duplicate) */
        while( (glst=TXgetglobalexp()) != NULL &&
               glst[0] && strlen(glst[0]) )
        {
            logandclearmsgbuf(ctx);
            if(setprop(ddic, "delexp", "0" )==-1)
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13 */
                goto return_neg_two;
            }
        }
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            logandclearmsgbuf(ctx);
            if(setprop(ddic, "addexp", (char*)val )==-1)
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
    }
    duk_pop_2(ctx);// list and this

    duk_idx_t setobj_idx=duk_normalize_index(ctx, -1);

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int do_suffix=0;
    if(duk_get_prop_string(ctx, setobj_idx, "usesuffixpreset")) {
        do_suffix=duk_get_boolean_default(ctx, -1, 0);
    }
    duk_pop(ctx);
#endif


    // apply all settings in object
    duk_enum(ctx, setobj_idx, 0);
    while (duk_next(ctx, -1, 1))
    {
        /*
        int retlisttype=-1, setlisttype=-1, i=0;
        char propa[64], *prop=&propa[0];
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
        {
            sprintf(errbuf, "sql.set - '%s' - unknown/invalid property", dprop);
            goto return_neg_one;
        }

        sql_normalize_prop(prop, dprop);
        */
        const char *prop=duk_get_string(ctx, -2);
        int retlisttype=-1, setlisttype=-1;

        /* useDerivations, set eqprefix and related */
        if( strcmp(prop,"usederivations")==0 )
        {
            RPPATH rp={{0}};
            char eqfile[PATH_MAX], eqpath[PATH_MAX], eqpath2[PATH_MAX];
            const char *dlang=NULL;

            // if false, turn it off
            if(duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1) )
            {
                if(setprop(ddic, "eqprefix", "builtin" )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    goto return_neg_two;
                }
                if(setprop(ddic, "alequivs", "0" )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    goto return_neg_two;
                }
                if(setprop(ddic, "keepeqvs", "0" )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    goto return_neg_two;
                }
                goto propnext;
            }
            //if true
            if(duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) )
                dlang="en";
            //if a string
            else if (!duk_is_string(ctx, -1))
            {
                snprintf(errbuf, msgbufsz, "sql.set: useDerivations must be a Boolean or a String (lang code)");
                goto return_neg_one;
            }
            else
                dlang = duk_get_string(ctx, -1);

            /* unnecessary and sometimes wrong
            if (strlen(dlang) > 2)
            {
                snprintf(errbuf, msgbufsz, "sql.set: useDerivations String must be 2 characters (lang code)");
                goto return_neg_one;
            }
            */
            snprintf(eqfile, PATH_MAX, "%s-deriv", dlang);
            snprintf(eqpath, PATH_MAX, "derivations/%s", dlang);
            snprintf(eqpath2, PATH_MAX, "share/rampart/derivations/%s", dlang);
            rp=rp_find_path(eqfile, eqpath, eqpath2);
            if(strlen(rp.path))
            {
                //printf("setting equiv file: '%s'\n", rp.path);
                if(setprop(ddic, "eqprefix", rp.path )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    setprop(ddic, "eqprefix", "" ); //reset if fail
                    goto return_neg_two;
                }

                if(setprop(ddic, "alequivs", "1" )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    setprop(ddic, "eqprefix", "" ); //reset if fail
                    goto return_neg_two;
                }
                if(setprop(ddic, "keepeqvs", "1" )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    setprop(ddic, "eqprefix", "" ); //reset if fail
                    goto return_neg_two;
                }

            }
            else
            {
                snprintf(errbuf, msgbufsz, "sql.set: couldn't find %.1024s in %.1024s", eqfile, eqpath); /* F13 */
                goto return_neg_two;
            }
            goto propnext;
        }

        /* llamaEmbedPerThread — process-global flag.  Default = true.
         * When true, each calling parent worker thread gets its own
         * llama_context lazily; when false, one shared context with a
         * mutex.  Helper-child ignores. */
        if (!strcasecmp(prop, "llamaEmbedPerThread"))
        {
            if (thisfork) goto propnext;
            int on = 1;
            if (duk_is_boolean(ctx, -1)) on = duk_get_boolean(ctx, -1) ? 1 : 0;
            else if (duk_is_number(ctx, -1)) on = duk_get_int(ctx, -1) ? 1 : 0;
            else {
                snprintf(errbuf, msgbufsz,
                         "sql.set: llamaEmbedPerThread must be a Boolean");
                goto return_neg_one;
            }
            if (g_rp_embed_set_per_thread)
                g_rp_embed_set_per_thread(on);
            goto propnext;
        }

        /* llamaCache — number of entries in the process-wide embed
         * LRU cache.  0 (default) disables.  Helper-child ignores
         * this — the cache lives in main only.  Idempotent for v1
         * (subsequent calls with a different N are silently no-op
         * after the first). */
        if (!strcasecmp(prop, "llamaCache"))
        {
            if (thisfork) goto propnext;  /* helper: nothing to do */
            int n = 0;
            if (duk_is_number(ctx, -1))      n = duk_get_int(ctx, -1);
            else if (duk_is_string(ctx, -1)) n = atoi(duk_get_string(ctx, -1));
            else {
                snprintf(errbuf, msgbufsz,
                         "sql.set: llamaCache must be a Number");
                goto return_neg_one;
            }
            if (n < 0) n = 0;
            if (embed_lru_set_capacity((size_t)n) != 0) {
                snprintf(errbuf, msgbufsz,
                         "sql.set: llamaCache: failed to allocate cache");
                goto return_neg_one;
            }
            goto propnext;
        }

        /* likevCache / likevCacheSize — number of documents in the
         * per-model doc-result cache (the one that lets repeated
         * `likev ?` searches, and chunkembed()+chunkavg()+embed() of the
         * same text, share one model run).  Default 10; 0 disables.
         * Applies to this connection's embed model handle; recorded in a
         * thread-local so it works whether it appears before or after
         * onnxEmbed/llamaEmbed in the same sql.set object, and applied
         * immediately if the handle is already loaded.  Helper-child
         * ignores it — the cache lives in the parent where the model
         * runs. */
        if (!strcasecmp(prop, "likevCache") ||
            !strcasecmp(prop, "likevCacheSize"))
        {
            if (thisfork) goto propnext;
            int n = 0;
            if (duk_is_number(ctx, -1))      n = duk_get_int(ctx, -1);
            else if (duk_is_string(ctx, -1)) n = atoi(duk_get_string(ctx, -1));
            else {
                snprintf(errbuf, msgbufsz,
                         "sql.set: likevCache must be a Number");
                goto return_neg_one;
            }
            if (n < 0) n = 0;
            /* Record it; the cap is applied to the connection's actual
             * embed handle wherever h->embed_handle is established
             * (h_set + the per-exec lazy-load), via
             * apply_pending_doccache_cap().  Storing rather than
             * applying here is deliberate: the handle used for embeds
             * isn't necessarily the one setup_*_main loaded. */
            g_doccache_cap_pending = n;
            goto propnext;
        }

        /* llamaEmbed — main loads the model + registers main callback,
         * helper-child only registers the wire callback (writes 'B'
         * up the pipe).  thisfork tells us which side we're on. */
        if (!strcasecmp(prop, "llamaEmbed"))
        {
            const char *path = NULL;
            if (duk_is_string(ctx, -1))
                path = duk_get_string(ctx, -1);
            else if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) &&
                     !duk_is_function(ctx, -1)) {
                /* object form: { model:'/path.gguf', queryPrompt: ...,
                 * documentPrompt: ..., documentTitlePrompt: ...,
                 * prompts:false } -- prompt keys are handled by
                 * resolve_embed_prompts() at the h_set attach point. */
                if (duk_get_prop_string(ctx, -1, "model") && duk_is_string(ctx, -1))
                    path = duk_get_string(ctx, -1);
                duk_pop(ctx);
            }
            if (!path || !path[0]) {
                snprintf(errbuf, msgbufsz,
                         "sql.set: llamaEmbed must be a string path or "
                         "{model:'<path.gguf>', ...prompt opts}");
                goto return_neg_one;
            }
            if (thisfork) {
                /* Helper child: just register the wire callback. */
                setup_llamacpp_callback();
                goto propnext;
            }
            char eerr[256] = {0};
            if (setup_llamacpp_main(ctx, path, eerr, sizeof eerr) != 0) {
                snprintf(errbuf, msgbufsz, "%s", eerr);
                goto return_neg_one;
            }
            /* DB_HANDLE flag setting happens in h_set / fork_sql_set
             * after this returns successfully. */
            goto propnext;
        }

        /* onnxEmbed — ONNX-backed embed via rampart-onnx.  Accepts an
         * object with at minimum `{model:'<dir>'}`; the model directory
         * self-configures (tokenizer, pooling, bos/eos, normalize) via
         * rampart-onnx's directory mode.  Legacy file mode also works
         * -- pass a bare .onnx as `model` plus a `tokenizer` string.
         * Parsed by peek_onnxembed_setting.  Same fork-vs-main split as
         * llamaEmbed: child registers the wire callback, main loads +
         * registers the dispatcher. */
        if (!strcasecmp(prop, "onnxEmbed"))
        {
            if (!duk_is_object(ctx, -1) || duk_is_array(ctx, -1)) {
                snprintf(errbuf, msgbufsz,
                         "sql.set: onnxEmbed must be an object "
                         "{model:'<dir-or-.onnx>', ...opts}");
                goto return_neg_one;
            }
            if (thisfork) {
                setup_llamacpp_callback();   /* wire callback is engine-agnostic */
                goto propnext;
            }
            /* peek_onnxembed_setting expects the settings object at
             * top-of-stack; feed it the property-value object directly by
             * wrapping it in a temp object with prop "onnxembed". */
            duk_push_object(ctx);
            duk_dup(ctx, -2);
            duk_put_prop_string(ctx, -2, "onnxembed");
            const char *model = NULL;
            rp_onnx_embed_opts opts;
            int ok = peek_onnxembed_setting(ctx, &model, &opts);
            duk_pop(ctx);    /* temp wrapper */
            if (!ok) {
                snprintf(errbuf, msgbufsz,
                         "sql.set: onnxEmbed requires .model (a model directory, "
                         "or a bare .onnx with a .tokenizer path)");
                goto return_neg_one;
            }
            char eerr[256] = {0};
            if (setup_onnx_main(ctx, model, &opts, eerr, sizeof eerr) != 0) {
                snprintf(errbuf, msgbufsz, "%s", eerr);
                goto return_neg_one;
            }
            goto propnext;
        }

        /* clipEmbed — CLIP (image + text in one vector space) via
         * rampart-clip.  Accepts a bare path String or {model:'...'}.
         * Same fork-vs-main split as the two above. */
        if (!strcasecmp(prop, "clipEmbed"))
        {
            const char *cpath = NULL;
            if (duk_is_string(ctx, -1)) {
                cpath = duk_get_string(ctx, -1);
            } else if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1)) {
                if (duk_get_prop_string(ctx, -1, "model") && duk_is_string(ctx, -1))
                    cpath = duk_get_string(ctx, -1);
                duk_pop(ctx);
            }
            if (!cpath || !cpath[0]) {
                snprintf(errbuf, msgbufsz,
                         "sql.set: clipEmbed must be a String path to a CLIP "
                         ".gguf, or an object {model:'<path>'}");
                goto return_neg_one;
            }
            if (thisfork) {
                setup_llamacpp_callback();   /* wire callback is engine-agnostic */
                goto propnext;
            }
            char eerr[256] = {0};
            if (setup_clip_main(ctx, cpath, eerr, sizeof eerr) != 0) {
                snprintf(errbuf, msgbufsz, "%s", eerr);
                goto return_neg_one;
            }
            goto propnext;
        }

        if (!strcmp ("lstnoise", prop))
            retlisttype=0;
        else if (!strcmp ("lstsuffix", prop))
            retlisttype=1;
        else if (!strcmp ("lstsuffixeqivs", prop))
            retlisttype=2;
        else if (!strcmp ("lstprefix", prop))
            retlisttype=3;
        else if (!strcmp ("noiselst", prop))
            setlisttype=0;
        else if (!strcmp ("suffixlst", prop))
            setlisttype=1;
        else if (!strcmp ("suffixeqivslst", prop))
            setlisttype=2;
        else if (!strcmp ("suffixeqlst", prop))
            setlisttype=2;
        else if (!strcmp ("prefixlst", prop))
            setlisttype=3;

        if( (!strcmp(prop, "lstexp")||!strcmp(prop, "lstindextmp")))
        {
            char **lst;
            int arryi=0;
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    goto propnext;
            }
            else
                RP_THROW(ctx, "sql.set - property '%s' requires a Boolean", prop);

            if(!added_ret_obj)
            {
                duk_push_object(ctx);
                duk_insert(ctx, 0);
                added_ret_obj=1;
            }

            duk_push_array(ctx);

            if (!strcmp(prop, "lstexp"))
                lst=TXgetglobalexp();
            else
                lst=TXgetglobalindextmp();

            while (lst[arryi] && strlen(lst[arryi]))
            {
                duk_push_string(ctx, lst[arryi]);
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)arryi);
                arryi++;
            }

            duk_put_prop_string(ctx, 0,
                (
                    strcmp(prop, "lstindextmp")?"expressionsList":"indexTempList"
                )
            );

            goto propnext;
        }

        if(retlisttype>-1)
        {
            byte *nw;
            byte **lsts[]={globalcp->noise,globalcp->suffix,globalcp->suffixeq,globalcp->prefix};
            char *rprop=rlsts[retlisttype];
            byte **lst=lsts[retlisttype];
            int i=0;

            /* skip if false */
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    goto propnext;
            }
            else
                RP_THROW(ctx, "sql.set - property '%s' requires a Boolean", prop);

            if(!added_ret_obj)
            {
                duk_push_object(ctx);
                duk_insert(ctx, 0);
                added_ret_obj=1;
            }

            duk_push_array(ctx);
            while ( (nw=lst[i]) && *nw != '\0' )
            {
                duk_push_string(ctx, (const char *) nw);
                duk_put_prop_index(ctx, -2, i++);
            }

            duk_put_prop_string(ctx, 0, rprop);

            goto propnext;
        }

        if(setlisttype>-1)
        {
            char **nl=NULL; /* the list to be populated */
            /* set the new list up, then free and replace current list *
             * should be null or an array of strings ONLY              */
            if(duk_is_null(ctx, -1))
            {
                REMALLOC(nl, sizeof(char*) * 1);
                nl[0]=strdup("");
            }
            else if(duk_is_array(ctx, -1))
            {
                int len=duk_get_length(ctx, -1), i=0;

                REMALLOC(nl, sizeof(char*) * (len + 1));

                while (i<len)
                {
                    duk_get_prop_index(ctx, -1, i);
                    if(!(duk_is_string(ctx, -1)))
                    {
                        /* note that the RP_THROW below might be caught in js, so we need to clean up *
                         * terminate what we have so far, then free it                                */
                        nl[i]=strdup("");
                        free_list((char**)nl);
                        snprintf(errbuf, msgbufsz, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                        goto return_neg_one;
                    }
                    nl[i]=strdup(duk_get_string(ctx, -1));
                    duk_pop(ctx);
                    i++;
                }
                nl[i]=strdup("");
            }
            else
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                goto return_neg_one;
            }

            switch(setlisttype)
            {
                case 0: free_list((char**)globalcp->noise);
                        globalcp->noise=(byte**)nl;
                        defnoise=0;
                        break;

                case 1: free_list((char**)globalcp->suffix);
                        globalcp->suffix=(byte**)nl;
                        defsuffix=0;
                        break;

                case 2: free_list((char**)globalcp->suffixeq);
                        globalcp->suffixeq=(byte**)nl;
                        defsuffixeq=0;
                        break;

                case 3: free_list((char**)globalcp->prefix);
                        globalcp->prefix=(byte**)nl;
                        defprefix=0;
                        break;
            }
            goto propnext;
        }

        /* addexp, delexp and addindextmp take one at a time, but may take multiple
           so handle arrays here
        */
        if
        (
            duk_is_array(ctx, -1) &&
            (
                !strcmp(prop,"addexp") ||
                !strcmp(prop,"delexp") ||
                !strcmp(prop,"delindextmp") ||
                !strcmp(prop,"addindextmp")
            )
        )
        {
            int ptype=0;

            if(!strcmp(prop,"delexp"))
                ptype=1;
            else if (!strcmp(prop,"addindextmp"))
                ptype=2;
            else if (!strcmp(prop,"delindextmp"))
                ptype=3;

            duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
            while(duk_next(ctx, -1, 1))
            {
                const char *aval=NULL;

                if (ptype==1)
                {
                    if(duk_is_number(ctx, -1))
                    {
                        duk_to_string(ctx, -1);
                        aval=duk_get_string(ctx, -1);
                    }
                    else
                    {
                        aval=get_exp(ctx, -1);

                        if(!aval)
                        {
                            sprintf(errbuf, "sql.set: deleteExpressions - array must be an array of strings, expressions or numbers (expressions or expression index)\n");
                            goto return_neg_one;
                        }
                    }
                }
                else if (ptype==3)
                {
                    if(duk_is_number(ctx, -1))
                    {
                        duk_to_string(ctx, -1);
                        aval=duk_get_string(ctx, -1);
                    }
                    else if (duk_is_string(ctx, -1))
                    {
                        aval=duk_get_string(ctx, -1);
                    }
                    else
                    {
                        sprintf(errbuf, "sql.set: deleteIndexTemp - array must be an array of strings or numbers\n");
                        goto return_neg_one;
                    }
                }
                else if (ptype==0)
                {
                    aval=get_exp(ctx, -1);

                    if(!aval)
                    {
                        sprintf(errbuf, "sql.set: addExpressions - array must be an array of strings or expressions\n");
                        goto return_neg_one;
                    }

                    if(!expr_compiles(aval))
                    {
                        rp_msg_finalize();
                        snprintf(errbuf, msgbufsz,
                                 "sql.set: addExpressions - invalid expression '%s'%s%s",
                                 aval, finfo->errmap[0] ? ": " : "", finfo->errmap);
                        goto return_neg_one;
                    }
                }
                else
                {
                    if(!duk_is_string(ctx, -1))
                    {
                        sprintf(errbuf, "sql.set: addIndexTemp - array must be an array of strings\n");
                        goto return_neg_one;
                    }
                    aval=duk_get_string(ctx, -1);
                }

                logandclearmsgbuf(ctx);
                if(setprop(ddic, (char*)prop, (char*)aval )==-1)
                {
                    snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                    goto return_neg_two;
                }
                duk_pop_2(ctx);
            }
            duk_pop(ctx);
        }
        else
        {
#ifdef LIKEP_PARAM_SUBSTITUTIONS
            if(!strcasecmp(prop, "usesuffixpreset"))
                goto propnext;
#endif
            if(duk_is_number(ctx, -1))
                duk_to_string(ctx, -1);
            if(duk_is_boolean(ctx, -1))
            {
                if(duk_get_boolean(ctx, -1))
                    val="1";
                else
                    val="0";
            }
            else
            {
                if(!(duk_is_string(ctx, -1)))
                {
                    snprintf(errbuf, msgbufsz, "invalid value '%s'", duk_safe_to_string(ctx, -1));
                    goto return_neg_one;
                }
                val=duk_get_string(ctx, -1);
            }

            /*
            if(!strcmp(prop,"querydefaults") || !strcmp(prop,"querydefault"))
            {
                if(!duk_get_boolean_default(ctx, -1, 1))
                    goto propnext;
                prop="querysettings";
                val="defaults";
            }
           */

            /* texis converts these with atoi()/strtod() and never checks,
             * so a typo would silently set the property to 0. */
            if(prop_is_numeric(prop) && !is_numeric_string(val))
            {
                snprintf(errbuf, msgbufsz,
                         "sql.set: %s must be a Number, got '%s'", prop, val);
                goto return_neg_one;
            }

            /* the single-value form of addExpressions; the array form is
             * checked per element above */
            if(!strcmp(prop, "addexp"))
            {
                logandclearmsgbuf(ctx);
                if(!expr_compiles(val))
                {
                    rp_msg_finalize();
                    snprintf(errbuf, msgbufsz,
                             "sql.set: addExpressions - invalid expression '%s'%s%s",
                             val, finfo->errmap[0] ? ": " : "", finfo->errmap);
                    goto return_neg_one;
                }
            }

            logandclearmsgbuf(ctx);
            //printf("setprop(%s, %s)\n", prop, val);
            if(setprop(ddic, (char*)prop, (char*)val )==-1)
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
                goto return_neg_two;
            }
        }

        /* save the altered list for reapplication after reset */
        if( !strcmp(prop, "addexp")||!strcmp(prop, "addindextmp") ||
            !strcmp(prop, "delexp")||!strcmp(prop, "delindextmp")
          )
        {
            char **lst;
            int arryi=0;
            char type = prop[3]; // i for index, e for expression

            duk_push_this(ctx);
            duk_push_array(ctx);
            if (type == 'e')
                lst=TXgetglobalexp();
            else
                lst=TXgetglobalindextmp();

            while (lst[arryi] && strlen(lst[arryi]))
            {
                duk_push_string(ctx, lst[arryi]);
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)arryi);
                arryi++;
            }
            if (type == 'e')
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("explist"));
            else
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("indlist"));

            duk_pop(ctx);//this
        }

        propnext:
        duk_pop_2(ctx);
    }
    duk_pop(ctx);//enum

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    // shortcut setting, overriding anything set above
    if(do_suffix)
    {
        if(setprop(ddic, "useequiv", "1" )==-1)
        {
            snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
            goto return_neg_two;
        }
        if(setprop(ddic, "minwordlen", "5" )==-1)
        {
            snprintf(errbuf, msgbufsz, "sql.set: %s", finfo->errmap); /* F13: errmap can be ~4095 bytes */
            goto return_neg_two;
        }
    }
#endif

    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */

    //tx=texis_close(tx);
    if(added_ret_obj)
    {
        duk_pull(ctx, 0);
        return 1;
    }
    return 0;

    return_neg_two:
        //tx=texis_close(tx);
        return -2;

    return_neg_one:
        //tx=texis_close(tx);
        return -1;
}

/*
static char *stringLower(const char *str)
{
    size_t len = strlen(str);
    char *lower = NULL;
    int i=0;

    REMALLOC(lower, len+1);
    for (; i < len; ++i) {
        lower[i] = tolower((unsigned char)str[i]);
    }
    lower[i] = '\0';
    return lower;
}
*/

// certain settings like lstexp and addexp should not remain in saved settings
static void clean_settings(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings"));
    duk_remove(ctx, -2);
    duk_del_prop_string(ctx, -1, "lstexp");
    duk_del_prop_string(ctx, -1, "delexp");
    duk_del_prop_string(ctx, -1, "addexp");
    duk_del_prop_string(ctx, -1, "lstindextmp");
    duk_del_prop_string(ctx, -1, "delindextmp");
    duk_del_prop_string(ctx, -1, "addindextmp");
    duk_del_prop_string(ctx, -1, "lstnoise");
    duk_del_prop_string(ctx, -1, "lstsuffix");
    duk_del_prop_string(ctx, -1, "lstsuffixeqivs");
    duk_del_prop_string(ctx, -1, "lstprefix");
    duk_pop(ctx);//the settings list object
}

/* Normalize the expression settings in the object on top of the stack:
 * a RegExp becomes its .source, whether it is an array element or the
 * bare single value.  Validates nothing and never throws.
 *
 * This used to live inside an `if (DB_HANDLE_IS(h, DB_FLAG_FORK))` block.
 * The CBOR rationale for it is fork-only -- a RegExp does not survive the
 * trip to the helper -- but the conversion itself is needed on both
 * paths: in-process a single RegExp reached setprop() stringified as
 * "/pat/flags" and came back "invalid value".
 *
 * It also REQUIRED an array and threw otherwise, which rejected the
 * documented single-value form ("a single additional, or an array of
 * additional REX expression", and likewise for delExp) -- but only on the
 * forked path, so `sql.set({addexp: "[\\alnum]+"})` worked in-process and
 * failed through a helper.  Worse, it threw AFTER the new settings had
 * been merged into sql_settings and BEFORE h_set, i.e. past the only
 * place that rolls them back, so the rejected value stayed in the stored
 * settings and was replayed -- and re-thrown -- by every later set() on
 * that connection.
 *
 * Nothing is validated here on purpose.  Bad values are rejected by
 * sql_set()/setprop() below, which is the same code in-process and in the
 * helper child, so both paths reject exactly the same things. */
static void normalize_exp_settings(duk_context *ctx)
{
    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        const char *k = duk_get_string(ctx, -2);

        if( k &&
            (
                !strcasecmp("addexp",k)           ||
                !strcasecmp("addexpressions",k)   ||
                !strcmp("delexpressions", k)      ||
                !strcmp("deleteexpressions", k)   ||
                !strcasecmp("delexp",k)
            )
        )
        {
            if(duk_is_array(ctx, -1))
            {
                duk_uarridx_t ix=0, len=duk_get_length(ctx, -1);

                while (ix < len)
                {
                    duk_get_prop_index(ctx, -1, ix);
                    if(duk_is_object(ctx,-1) && duk_has_prop_string(ctx,-1,"source") )
                    {
                        duk_get_prop_string(ctx, -1,"source");
                        duk_put_prop_index(ctx, -3, ix);
                    }
                    duk_pop(ctx);
                    ix++;
                }
            }
            else if(duk_is_object(ctx,-1) && duk_has_prop_string(ctx,-1,"source"))
            {
                /* the single-value form, given as a RegExp.  stack is
                   [ settings, enum, key, value ]; settings is at -5 once
                   the source string is pushed. */
                duk_get_prop_string(ctx, -1, "source");
                duk_put_prop_string(ctx, -5, k);
            }
        }
        duk_pop_2(ctx);
    }
    duk_pop(ctx);
}

static duk_ret_t rp_texis_set(duk_context *ctx)
{
    const char *db, *user="PUBLIC", *pass="";
    DB_HANDLE *h = NULL;
    int ret = 0;
    char errbuf[msgbufsz];
    char propa[64], *prop=&propa[0];

    /* Same entry sequence as rp_sql_exec_query() and rp_sql_import():
     * empty the capture buffer FIRST, so nothing this call reports can be
     * a leftover from an earlier one -- and, just as important, so that
     * everything from here on (h_open, h_reset_tx_default, and the
     * per-property work below) is reported rather than discarded.  This
     * used to sit after h_open/h_reset_tx_default, which threw away any
     * message they produced. */
    clearmsgbuf();

    duk_push_this(ctx); //idx == 1

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* clear the sql.errMsg string, as exec() and the importers do.
     * rp_log_copy_to_errMsg() APPENDS, so without this a warning raised
     * by sql.set() is joined onto whatever error the previous statement
     * left behind, and errMsg grows across unrelated calls. */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx);

    h = h_open(db,user,pass);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }

    h_reset_tx_default(ctx, h, -1, FORCE_RESET);

    if(!duk_is_object(ctx, 0) || duk_is_array(ctx, 0) || duk_is_function(ctx, 0) )
        RP_THROW(ctx, "sql.set() - object with {prop:value} expected as parameter - got '%s'",duk_safe_to_string(ctx, 0));

    //stack = [ settings_obj, this ]
    //get any previous settings from this
    if(! duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("sql_settings")) )
    {
        duk_pop(ctx);   // pop undefined,
        duk_push_object(ctx); // [ settings_obj, this, new_empty_old_settings ]
    }
                                              // [ settings_obj, this, old_settings ]

    /* Snapshot the saved settings before merging, so a failed apply can be
       rolled back.  The merge below writes every incoming key into the SAME
       stored object BEFORE anything is applied, so without this a key that
       h_set rejects stays behind and is replayed by every subsequent set() --
       which then re-throws the original error even for a perfectly valid
       call, until reset() clears the object.  (Compare the add/del list
       operations stripped after a successful apply further down: same
       "stale entry replayed forever" hazard.) */
    {
        duk_idx_t bidx;

        duk_push_object(ctx);
        bidx = duk_get_top_index(ctx);
        duk_enum(ctx, 2, 0);
        while (duk_next(ctx, -1, 1))
        {
            const char *bk = duk_get_string(ctx, -2);
            duk_put_prop_string(ctx, bidx, bk);   /* consumes the value */
            duk_pop(ctx);                         /* pop the key         */
        }
        duk_pop(ctx);                             /* pop the enum        */
        duk_put_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("sql_settings_backup"));
    }
                                              // [ settings_obj, this, old_settings ]
    /* copy properties, renamed as lowercase, into saved old settings */
    duk_enum(ctx, 0, 0);                      // [ settings_obj, this, old_settings, enum_obj ]
    while (duk_next(ctx, -1, 1))
    {
        //  inside loop -                        [ settings_obj, this , old_settings, enum_obj, key, val ]
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
            RP_THROW(ctx, "sql.set - '%s' - unknown/invalid property", dprop);

        sql_normalize_prop(prop, dprop);

        //check for "useequiv", set "alequiv" to true.
        if(strcmp("useequiv", prop)==0 &&
            (
                ( duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) ) ||
                ( duk_is_number(ctx, -1) && duk_get_number(ctx, -1) != 0 ) ||
                ( duk_is_string(ctx, -1) && strcmp(duk_get_string(ctx,-1), "1")==0) )
            )
        {
            duk_push_true(ctx);
            duk_put_prop_string(ctx, 2, "alequivs");
        }
        // put val into old settings, overwriting old if exists
        duk_put_prop_string(ctx, 2, prop);    // [ settings_obj, this , old_settings, enum_obj, key ]
        duk_pop(ctx);                         // [ settings_obj, this , old_settings, enum_obj ]
    }
    duk_pop(ctx);                             // [ settings_obj, this, combined_settings ]
    duk_remove(ctx, 0);                       // [ this , combined_settings ]

    duk_dup(ctx, -1);                         // [ this , combined_settings, combined_settings ]
    duk_put_prop_string(ctx, 0,
      DUK_HIDDEN_SYMBOL("sql_settings"));     // [ this, combined_settings ]
    duk_remove(ctx, 0);                       // [ combined_settings ]

    /* Replace any RegExp in the expression settings with its source text,
     * on BOTH paths -- see normalize_exp_settings(). */
    normalize_exp_settings(ctx);

    /* Apply through h_set -- the same path exec-time re-application
     * uses -- so an embed key attaches the model AND its retrieval
     * prompts to the handle on EVERY set() that carries one.  (set()
     * merges into the SAME stored settings object, so the exec-time
     * last_sql_set fast-path never re-applies on a re-set; the old
     * direct fork_sql_set/sql_set calls here left a live handle's
     * prompts -- e.g. a prompts:false toggle -- and embed handle
     * stale.)  h_set also applies a pending likevCache cap, with or
     * without an embed key in this set(). */
    ret = h_set(ctx, h, errbuf);

    if(ret == -1 || ret == -2)
    {
        /* Roll the saved settings back to their pre-call state so the key
           that just failed is not replayed by every later set(). */
        duk_push_this(ctx);
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings_backup")))
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sql_settings"));
        else
            duk_pop(ctx);
        duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings_backup"));
        duk_pop(ctx);

        h_close(h);

        /* Report identically however the failure got here.
         *
         * errbuf is the text to report in BOTH cases: in-process sql_set()
         * built it as "sql.set: <errmap>", and through a helper the child
         * built the same string and sent it up the wire.  It was not being
         * logged on the forked path -- the message lives in errbuf, not in
         * the parent's view of the error map -- so an identical failure set
         * errMsg in-process and left it undefined through a helper.
         *
         * ret == -2 also used to go through throw_tx_error(ctx, errbuf),
         * which formats "%s error: %s" with errbuf as the PREFIX and the
         * error map as the suffix -- and since errbuf already embedded the
         * map's text, every -2 failure was thrown with its message printed
         * twice.  One RP_THROW of errbuf covers both returns. */
        rp_msg_finalize();
        rp_log_copy_to_errMsg(ctx, errbuf);
        clearmsgbuf();

        RP_THROW(ctx, "%s", errbuf);
    }

    h_end_transaction(h);

    /* add/del list OPERATIONS must not persist in the saved settings:
     * the resulting list STATE was just snapshotted into the hidden
     * indlist/explist arrays by h_set, and those are what the replay
     * path applies.  Leaving e.g. addexp in sql_settings made every
     * later set() (or handle-switch replay) re-apply the op on top of
     * the replayed list -- the "delete in a separate set() call
     * corrupts the list" bug. */
    duk_push_this(ctx);
    /* applied cleanly -- the rollback snapshot is no longer needed */
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings_backup"));
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings")))
    {
        duk_del_prop_string(ctx, -1, "addexp");
        duk_del_prop_string(ctx, -1, "delexp");
        duk_del_prop_string(ctx, -1, "addindextmp");
        duk_del_prop_string(ctx, -1, "delindextmp");
    }
    duk_pop_2(ctx);

    clean_settings(ctx);
    return (duk_ret_t) ret;
}
int largc;
char **largv;

static void addtbl(duk_context *ctx, char *func, const char *db, char *tbl)
{
    char *err=NULL;

    if(access(tbl,W_OK) != 0)
    {
        err=strerror(errno);
        errno=0;
    }
    else switch ( TXaddtable((char*)db, tbl, NULL, NULL, NULL, NULL, 0) )
    {
        case -2:	err="permission denied";break;
        case -1:	err="unknown error";break;
        case 0:		err=NULL;break;
        default:	err="unknown error";break;
    }
    if(err)
    {
        RP_THROW(ctx, "%s: error importing table %s - %s", func, tbl, err);
    }
}

static duk_ret_t rp_sql_addtable(duk_context *ctx)
{
    const char *db, *tbl = REQUIRE_STRING(ctx, 0, "argument must be a string (/path/to/importTable.tbl)");

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        RP_THROW(ctx, "no database has been opened");
    }
    db=duk_get_string(ctx, -1);
    addtbl(ctx, "addTable()", db, (char*)tbl);
    return 0;
}

// the updater daemon script.  Checks that it is needed, then forks
// to monitor text indexes.

static char *updater_js =
"function(npsql) { \n"
    "var su;\n"
    "try {su=require('rampart-sqlUpdate.js');}catch(e){}\n"
    "if(su){su.launchUpdater(npsql);return true;}\n"
    "else return false;\n"
"}";


/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   ************************************************** */
static duk_ret_t rp_sql_constructor(duk_context *ctx)
{
    const char *db = NULL, *user="PUBLIC", *pass="";
    DB_HANDLE *h;
    int force=0, addtables=0, create=0, no_updater=0;
    char *default_db_files[]={
        "SYSCOLUMNS.tbl",  "SYSINDEX.tbl",  "SYSMETAINDEX.tbl",  "SYSPERMS.tbl",
        "SYSSTATS.tbl",  "SYSTABLES.tbl",  "SYSTRIG.tbl",  "SYSUSERS.tbl", NULL
    };

    /* allow call to Sql() with "new Sql.connect()" only */
    if (!duk_is_constructor_call(ctx))
    {
        RP_THROW(ctx, "Sql.connection():  Must be called with 'new Sql.connection()");
    }

    if(duk_is_string(ctx, 0))
    {
        db = duk_get_string(ctx, 0);
    }

    if(duk_is_boolean(ctx, 1))
        create = (int) duk_get_boolean(ctx, 1);
    else if (!duk_is_undefined(ctx, 1))
        RP_THROW(ctx, "new Sql.connection(path,create) - create must be a boolean");

    if(duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "path"))
        {
            db = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "user"))
        {
            user = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "pass"))
        {
            pass = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "force"))
        {
            force = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.force must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "noUpdater"))
        {
            no_updater = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.noUpdater must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "addtables"))
        {
            addtables = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.addtables must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "addTables"))
        {
            addtables = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.addTables must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "create"))
        {
            create = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params. must be a boolean");
        }
        duk_pop(ctx);

        if(addtables||force)
            create=1;
    }


    if(!db || !strlen(db))
        RP_THROW(ctx,"new Sql.connection() - empty or missing database name");

    clearmsgbuf();

    /* check for db first */
    h = h_open(db, user, pass);
    /* if h, then just open that.  Ignore all other options except addtables */

    if (!h)// otherwise check options
    {
        /*
         if sql=new Sql("/db/path",true), we will
         create the db if it does not exist
        */
        if (create)
        {
            DIR *dir = NULL;

            clearmsgbuf();

            if (rmdir(db) != 0)
            {
#ifdef EEXIST
                if(errno==EEXIST || errno==ENOTEMPTY)
#else
                if(errno==ENOTEMPTY)
#endif
                {
                    dir = opendir(db); //let's have a look inside this dir below
                }
                else if (errno!=ENOENT) //some other error than EEXIST, ENOTEMPTY or ENOENT
                    RP_THROW(ctx, "sql.connection(): cannot create database at '%s' - %s", db, strerror(errno));
                //else the dir doesn't exist, which is just fine.
            }

            // if dir exists, and we are using force or addtables, check if texis system files are present
            if(dir && (force||addtables))
            {
                char **s;
                struct dirent *entry=NULL;

                errno = 0;

                while ((entry = readdir(dir)) != NULL)
                {
                    s=default_db_files;
                    while(*s)
                    {
                        if(entry->d_name[0] != '.' && strcmp(*s, entry->d_name)==0)
                        {
                            closedir(dir);
                            RP_THROW(ctx, "sql.connection(): cannot create '%s', directory exists and has at least one SYS* file (%s)", db, *s);
                            break;
                        }
                        s++;
                    }
                }

                closedir(dir);
                dir=NULL;

                if(errno) //we read the dir
                {
                    int er=errno;
                    errno=0;
                    RP_THROW(ctx, "sql.connection(): cannot create '%s' - %s", db, strerror(er));
                }

                if(strlen(db)+20 > PATH_MAX)
                {
                    RP_THROW(ctx, "sql.connection(): cannot create '%s', path too long", db);
                }
                else
                {
                    char tmppath[PATH_MAX];
                    char topath[PATH_MAX];

                    strcpy(tmppath, db);
                    strcat(tmppath, "/.t");

                    if(!h_create(tmppath))
                    {
                        rp_log_error(ctx);
                        RP_THROW(ctx, "sql.connection(): cannot create database at '%s':\n%s", db, finfo->errmap);
                    }

                    s=default_db_files;
                    while(*s)
                    {
                        strcpy(tmppath, db);
                        strcat(tmppath, "/.t/");
                        strcat(tmppath, *s);
                        strcpy(topath, db);
                        strcat(topath, "/");
                        strcat(topath, *s);
                        if (rename(tmppath, topath))
                        {
                            RP_THROW(ctx, "sql.connection(): cannot create database at '%s': error moving files - %s", db, strerror(errno));
                        }
                        s++;
                    }
                    strcpy(tmppath, db);
                    strcat(tmppath, "/.t");

                    if(addtables)
                    {

                        dir = opendir(db);

                        while ((entry = readdir(dir)) != NULL)
                        {
                            char *e = entry->d_name, **s;
                            int len=strlen(e), issys=0;

                            s=default_db_files;

                            while(*s)
                            {
                                if(strcmp(*s, e)==0)
                                {
                                    issys=1;
                                    break;
                                }
                                s++;
                            }

                            if(!issys && e[len-4]=='.' && e[len-3]=='t' && e[len-2]=='b' && e[len-1]=='l')
                            {
                                char p[PATH_MAX];

                                strcpy(p,db);
                                strcat(p,"/");
                                strcat(p,e);
                                //printf("adding %s to %s\n", p, db);
                                addtbl(ctx, "sql.connection()", db, p);
                            }
                        }

                    }

                    if (rmdir(tmppath) != 0)
                    {
                        RP_THROW(ctx, "sql.connection(): cannot create database at '%s': error removing directory - %s", db, strerror(errno));
                    }
                }

            }
            else if(dir) // there's a dir and we don't have force or addtables.
            {
                closedir(dir);
                RP_THROW(ctx, "sql.connection(): cannot create '%s', directory exists and is not empty", db);
            }
            else if (!h_create(db)) //if !dir, try regular create
            {
                rp_log_error(ctx);
                RP_THROW(ctx, "sql.connection(): cannot open or create database at '%s':\n%s", db, finfo->errmap);
            }

            if(dir)
                closedir(dir);
        }
        else
        {
            rp_log_error(ctx);
            RP_THROW(ctx, "sql.connection(): cannot open database at '%s'\n%s", db, finfo->errmap);
        }
        h = h_open(db, user, pass);
        addtables=0;
    }


    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
    h_end_transaction(h);

    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, "db");
    duk_push_number(ctx, RESMAX_DEFAULT);
    duk_put_prop_string(ctx, -2, "selectMaxRows");
    duk_push_string(ctx, user);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("user"));
    duk_push_string(ctx, pass);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pass"));

    //currently unused, probably can be removed:
    SET_THREAD_UNSAFE(ctx);

    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    // addtables for existing db
    if(addtables)
    {
        int l, i=0;
        const char **existing=NULL;
        struct dirent *entry=NULL;
        DIR *dir = NULL;

        duk_push_string(ctx,"exec");
        duk_push_string(ctx, "select stringformat('%s%s', convert(WHAT, 'varchar' ), '.tbl') tbls from SYSTABLES where \
            WHAT!='SYSCOLUMNS' and  WHAT!='SYSINDEX' and  WHAT!='SYSMETAINDEX' and  WHAT!='SYSPERMS' and \
            WHAT!='SYSSTATS' and  WHAT!='SYSTABLES' and  WHAT!='SYSTRIG' and  WHAT!='SYSUSERS';");
        duk_push_object(ctx);
        duk_push_string(ctx, "array");
        duk_put_prop_string(ctx, -2, "returnType");
        duk_call_prop(ctx, -4, 2);
        duk_get_prop_string(ctx, -1, "rows");
        duk_remove(ctx, -2);

        l=(int)duk_get_length(ctx, -1);

        if(l)
        {
            REMALLOC(existing, l*sizeof(char *));

            for(i=0;i<l;i++)
            {
                duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                existing[i]=duk_to_string(ctx, -1);
                duk_pop(ctx);
            }

        }

        dir = opendir(db);

        while ((entry = readdir(dir)) != NULL)
        {
            char **s, *e = entry->d_name;
            size_t len = strlen(e);

            if(e[len-4]=='.' && e[len-3]=='t' && e[len-2]=='b' && e[len-1]=='l')
            {
                char p[PATH_MAX];
                int skip=0;

                s=default_db_files;

                while(*s)
                {
                    if(strcmp(*s, e)==0)
                    {
                        skip=1;
                        break;
                    }

                    s++;
                }

                if(!skip)
                {
                    for(i=0;i<l;i++)
                    {
                        if(strcmp(e,existing[i])==0)
                        {
                            skip=1;
                            break;
                        }
                    }
                }

                if(!skip)
                {
                    strcpy(p,db);
                    strcat(p,"/");
                    strcat(p,e);

                    addtbl(ctx, "sql.connection()", db, p);
                }
            }

        }
        if(existing)
            free(existing);
        closedir(dir);
        //safeprintstack(ctx);
        duk_pop(ctx); //array of WHATs from SYSTABLES
    }

    if(!no_updater)
    {
        // if there are entries in SYSUPDATE, run the update monitor as a daemon
        duk_push_string(ctx, "rampart-sql.c:indexUpdater()");
        duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, updater_js);
        duk_push_this(ctx);
        duk_call(ctx, 1);
    }
    return 0;
}
/*
#define CALLONE 0
#define CALLEXEC 1
#define NOARGS DUK_INVALID_INDEX
static inline void call_sql(duk_context *ctx, char *sql, int type, duk_idx_t param_idx)
{
    int nargs=1;
    if(param_idx != NOARGS)
    {
        nargs=2;
        param_idx=duk_normalize_index(ctx, param_idx);
    }

    if(type==CALLEXEC)
        duk_push_c_function(ctx, rp_sql_exec, nargs);
    else
        duk_push_c_function(ctx, rp_sql_one, nargs);

    duk_push_this(ctx); //has db name

    duk_push_string(ctx, sql);

    if(param_idx != NOARGS)
        duk_pull(ctx, param_idx);
printf("Call method nargs=%d\n", nargs);
safeprintstack(ctx);
    duk_call_method(ctx, nargs);
}
*/


/* Sql.setMapSize(bytes) -- size of the shared segment used by helpers
 * launched AFTER this call.
 *
 * Process-wide, and it cannot resize an existing helper: a POSIX shm
 * object cannot be shrunk on macOS at all, and re-mapping a live one
 * would invalidate the row pointers the parent hands to JS.  So this is
 * a "set it before the helpers start" knob -- in a server, at the top of
 * the conf, since workers fork their helpers lazily on first use.
 *
 * Returns the value actually adopted (rounded up to whole pages, floored
 * at one page), which may differ from what was asked for.  A helper may
 * still end up smaller if the kernel refuses the reservation; that is
 * per-helper and recorded in finfo->mapsize. */
static duk_ret_t rp_sql_set_map_size(duk_context *ctx)
{
    double  d = duk_get_number_default(ctx, 0, -1.0);
    size_t  pagesz, want;

    if (!(d >= 0.0))
        RP_THROW(ctx, "Sql.setMapSize: a size in bytes is required");

#ifdef _SC_PAGESIZE
    {
        long ps = sysconf(_SC_PAGESIZE);
        pagesz = (ps > 0) ? (size_t)ps : RP_FORKMAP_MIN;
    }
#else
    pagesz = RP_FORKMAP_MIN;
#endif
    if (pagesz < 8) pagesz = RP_FORKMAP_MIN;   /* paranoia: keep 8-aligned */

    want = (size_t)d;
    if (want < RP_FORKMAP_MIN) want = RP_FORKMAP_MIN;
    /* round UP to whole pages: keeps every chunk boundary 8-aligned,
     * which is what lets sender and receiver agree on alignment */
    if (want % pagesz) want += pagesz - (want % pagesz);

    g_forkmap_size = want;
    duk_push_number(ctx, (double)want);
    return 1;
}

static duk_ret_t fork_helper(duk_context *ctx)
{
    SFI finfo_d = {0};

    finfo=&finfo_d;
    REMALLOC(finfo->mapinfo, sizeof(FMINFO));

    setproctitle("rampart sql_helper");

    finfo->reader = REQUIRE_POSINT(ctx,0, "Error, this function is meant to be run upon forking only");
    finfo->writer = REQUIRE_POSINT(ctx,1, "Error, this function is meant to be run upon forking only");
    /* to help with debugging, get parent's thread num */
    thisfork = REQUIRE_POSINT(ctx,2, "Error, this function is meant to be run upon forking only");

    struct sigaction sa = { {0} };

    RP_TX_isforked=1; // mutex locking not necessary in fork
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = 0;
    sa.sa_handler =  die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    setproctitle("rampart sql_helper");

    if(!rp_watch_pid(getppid(), "rampart sql_helper"))
        fprintf(stderr, "Start watcher for sql helper failed\n");

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    do_child_loop(finfo); // loop and never come back;
    // mmap happens in loop

    return 0;
}

static duk_ret_t rp_sql_connect(duk_context *ctx)
{
    duk_push_this(ctx);
    if(!duk_get_prop_string(ctx, -1, "connection"))
        RP_THROW(ctx, "Sql.connect: no this binding");
    duk_insert(ctx, 0);
    duk_pop(ctx); //this
    // [ connection_constructor(), arg0, arg1 ]
    duk_new(ctx, 2);
    return 1;
}

//the sql.scheduleUpdate function
static const char *schupd =
"var su;\n"
"try { su=require('rampart-sqlUpdate.js'); } catch(e){}\n"
"if(su) su.scheduleUpdate\n";

//the sql.scheduleRebuild function (vec REBUILD scheduling)
static const char *schreb =
"var su;\n"
"try { su=require('rampart-sqlUpdate.js'); } catch(e){}\n"
"if(su) su.scheduleRebuild\n";

/* **************************************************
   Initialize Sql module
   ************************************************** */
char install_dir[PATH_MAX+21];
duk_ret_t rp_exec(duk_context *ctx);

extern int TX_is_rampart;
void rp_add_vector_types();

/* Find a zip entry whose basename is `name`.  Prefers an exact root match
   (cheap, O(log N)); on miss, scans for any entry ending in "/name".
   Result is symlink-resolved so a symlink at any of these positions
   pointing at the real binary still works. */
static const rp_zip_entry *rp_zip_find_basename(const char *name)
{
    const rp_zip_entry *e = rp_zip_resolve(name);
    if (e) return e;

    size_t target_len = strlen(name);
    size_t n = rp_zip_count();
    for (size_t i = 0; i < n; i++)
    {
        e = rp_zip_at(i);
        if (!e) break;
        if (e->name_len == 0 || e->name[e->name_len - 1] == '/') continue;
        if (e->name_len > target_len + 1 &&
            e->name[e->name_len - target_len - 1] == '/' &&
            memcmp(e->name + e->name_len - target_len, name, target_len) == 0)
        {
            /* Resolve symlinks at the matched buried path too. */
            char namebuf[PATH_MAX];
            if (e->name_len < sizeof(namebuf))
            {
                memcpy(namebuf, e->name, e->name_len);
                namebuf[e->name_len] = '\0';
                const rp_zip_entry *r = rp_zip_resolve(namebuf);
                if (r) return r;
            }
            return e;
        }
    }
    return NULL;
}

/* Resolver hook for the bundled-rampart case: extract texislockd from the
   appended zip to a tempfile and hand the path back to TXrunlockdaemon.
   The launched daemon, told via -i and -u, idle-exits after 15 minutes
   and unlinks its own image at startup so /tmp stays clean. */
static int rp_lockd_resolver(char *path, size_t pathsz, int *idle_secs)
{
    if (!rp_has_zip_payload) return -1;
    if (rp_zip_init() != 0) return -1;

    const rp_zip_entry *e = rp_zip_find_basename("texislockd");
    if (!e) return -1;

    unsigned char *zbuf = NULL;
    size_t zlen = 0;
    if (rp_zip_read(e, &zbuf, &zlen) != 0) return -1;

    char tp[256];
    snprintf(tp, sizeof(tp), "/tmp/rampart-texislockd-XXXXXX");
    int fd = mkstemp(tp);
    if (fd < 0) { free(zbuf); return -1; }

    ssize_t w = write(fd, zbuf, zlen);
    free(zbuf);
    if (w != (ssize_t)zlen) { close(fd); unlink(tp); return -1; }

    /* Must be executable for execv */
    if (fchmod(fd, 0700) != 0) { close(fd); unlink(tp); return -1; }
    close(fd);

    if (strlen(tp) >= pathsz) { unlink(tp); return -1; }
    strcpy(path, tp);
    if (idle_secs) *idle_secs = 900;
    return 0;
}

duk_ret_t duk_open_module(duk_context *ctx)
{

    TX_is_rampart=1;
    /* Set up locks:
     * this will be run once per new duk_context/thread in server.c
     * but needs to be done only once for all threads
     */
    CTXLOCK;
    if (!db_is_init)
    {
        char *TexisArgv[2];

        RP_PTINIT(&tx_handle_lock);

        /* If this rampart binary is bundled and the bundle includes a
           texislockd entry, route TXrunlockdaemon through our resolver so
           it extracts and launches that bundled binary.  Otherwise leave
           the hook NULL and the standard %EXEDIR%/%BINDIR%/$PATH search
           finds an installed texislockd as before. */
        if (rp_has_zip_payload && rp_zip_init() == 0
            && rp_zip_find_basename("texislockd") != NULL)
        {
            tx_lockd_resolver_hook = rp_lockd_resolver;
        }

        TexisArgv[0]=rampart_exec;

        // To help find texislockd -- which might be only in the same dir
        // as 'rampart' executable.
        strcpy (install_dir, "--install-dir-force=");
        strcat (install_dir, rampart_bin);
        TexisArgv[1]=install_dir;

        if( TXinitapp(NULL, NULL, 2, TexisArgv, &largc, &largv) )
        {
            CTXUNLOCK;
            RP_THROW(ctx, "Failed to initialize rampart-sql in TXinitapp");
        }
        rp_msg_init();  /* this thread's capture buffer; other threads
                           get theirs lazily (mmsgfh is thread-local) */
        db_is_init = 1;
    }
    CTXUNLOCK;

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, rp_sql_constructor, 2 /*nargs*/);

    /* Push proto object that will be Sql.connection.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Sql protoObj ] */

    /* Set Sql.connection.prototype.exec. */
    duk_push_c_function(ctx, rp_sql_exec, 6 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "exec");                    /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* Set Sql.connection.prototype.query. */
    duk_push_c_function(ctx, rp_sql_query, 6 /*nargs*/);  /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "query");                   /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* set Sql.connection.prototype.eval */
    duk_push_c_function(ctx, rp_sql_eval, 4 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "eval");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.connection.prototype.eval */
    duk_push_c_function(ctx, rp_sql_one, 2 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "one");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.connection.prototype.close */
    duk_push_c_function(ctx, rp_sql_close, 0 /*nargs*/); /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_close ] */
    duk_put_prop_string(ctx, -2, "close");                   /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close} ] */

    /* set Sql.connection.prototype.set */
    duk_push_c_function(ctx, rp_texis_set, 1 /*nargs*/);   /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_set ] */
    duk_put_prop_string(ctx, -2, "set");                    /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close,set:fn_set} ] */

    /* set Sql.connection.prototype.reset */
    duk_push_c_function(ctx, rp_texis_reset, 0);
    duk_put_prop_string(ctx, -2, "reset");

    /* set Sql.connection.prototype.importCsvFile */
    duk_push_c_function(ctx, rp_sql_import_csv_file, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsvFile");

    /* set Sql.connection.prototype.importCsv */
    duk_push_c_function(ctx, rp_sql_import_csv_str, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsv");

    duk_push_c_function(ctx, rp_sql_addtable, 1);
    duk_put_prop_string(ctx, -2, "addTable");

    /* set Sql.connection.prototype.scheduleUpdate */
    duk_eval_string(ctx, schupd);
    duk_put_prop_string(ctx, -2, "scheduleUpdate");

    /* set Sql.connection.prototype.scheduleRebuild */
    duk_eval_string(ctx, schreb);
    duk_put_prop_string(ctx, -2, "scheduleRebuild");

    /* Set Sql.connection.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, constructor-->[prototype-->{exe=fn_exe,...}] ] */
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -3, "connection");/* [ {connection()} ] */
    duk_put_prop_string(ctx, -2, "init");      /* depricated: [ {init()} ] */

    /* shortcut for new sql.connection() */
    duk_push_c_function(ctx, rp_sql_connect, 2);
    duk_put_prop_string(ctx, -2, "connect");

    /* Sql.list(arr) — explicit wrapper for `WHERE col IN (?)` lists.
     * Numbers → SQL_DOUBLE array, strings → SQL_STRLST. */
    duk_push_c_function(ctx, rp_sql_list_create, 1);
    duk_put_prop_string(ctx, -2, "list");

    duk_push_c_function(ctx, RPfunc_stringformat, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "stringFormat");

    duk_push_c_function(ctx, RPsqlFuncs_abstract, 5);
    duk_put_prop_string(ctx, -2, "abstract");

    duk_push_c_function(ctx, RPsqlFunc_sandr, 3);
    duk_put_prop_string(ctx, -2, "sandr");

    duk_push_c_function(ctx, RPsqlFunc_sandr2, 3);
    duk_put_prop_string(ctx, -2, "sandr2");

    /* rex|re2(
          expression,                     //string or array of strings
          searchItem,                     //string or buffer
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
            }
        );
   return value is an array of matches.
   If callback is specified, return value is number of matches.
  */
    duk_push_c_function(ctx, RPdbFunc_rex, 4);
    duk_put_prop_string(ctx, -2, "rex");

    duk_push_c_function(ctx, RPdbFunc_re2, 4);
    duk_put_prop_string(ctx, -2, "re2");

    /* rexfile|re2file(
          expression,                     //string or array of strings
          filename,                       //file with text to be searched
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
              delimiter:		  expression to match -- delimiter for end of buffer.  Default is "$" (end of line).  If your pattern crosses lines, specify
                                                                 a delimiter which will not do so and you will be guaranteed to match even if a match crosses internal read buffer boundry
            }
        );

   return value is an array of matches.
   If callback is specified, return value is number of matches.
  */
    duk_push_c_function(ctx, RPdbFunc_rexfile, 4);
    duk_put_prop_string(ctx, -2, "rexFile");

    duk_push_c_function(ctx, RPdbFunc_re2file, 4);
    duk_put_prop_string(ctx, -2, "re2File");

    duk_push_c_function(ctx, searchfile, 3);
    duk_put_prop_string(ctx, -2, "searchFile");

    duk_push_c_function(ctx, searchtext, 3);
    duk_put_prop_string(ctx, -2, "searchText");

    duk_push_c_function(ctx, rp_sql_set_map_size, 1);
    duk_put_prop_string(ctx, -2, "setMapSize");

    /* used when forking/execing */
    duk_push_string(ctx, "__helper");
    duk_push_c_function(ctx, fork_helper, 3);
    duk_def_prop(ctx, -3, DUK_DEFPROP_CLEAR_WEC|DUK_DEFPROP_HAVE_VALUE);

    add_exit_func(free_all_handles, NULL);
    return 1;
}
