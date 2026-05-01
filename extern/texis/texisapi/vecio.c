/* vecio.c — read/write a vec_graph to disk.
 *
 * Format version 1: full vectors stored in the file alongside graph
 * topology.  A future format revision can add a quantized-codes block and remove
 * full-vector duplication (vectors live in the table at that point).
 *
 * Atomicity: writes to "<path>.new", fsyncs, then renames over <path>.
 * On any error, the temp file is left in place for inspection — we don't
 * silently delete partial state.
 *
 * Endianness: host-order (little-endian on all targets we care about).
 * If we ever need cross-platform compatibility we add byte-swapping.
 */

#include "vecio.h"
#include "vecgraphi.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* ----- Error reporting ---------------------------------------------- */

/* texis is single-threaded by design (concurrency is across processes via
 * texislockd), so plain statics are sufficient.
 */
static vecio_err_t  vecio_err  = VECIO_OK;
static char         vecio_msg[256];

static void
vecio_set_error(vecio_err_t code, const char *fmt, ...)
{
    vecio_err = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vecio_msg, sizeof(vecio_msg), fmt, ap);
    va_end(ap);
}

vecio_err_t
vecio_last_error_code(void) { return vecio_err; }

const char *
vecio_last_error_msg(void) { return vecio_msg; }

/* ----- File header --------------------------------------------------- */

#define VECIO_MAGIC      0x30454356u   /* "VCE0" little-endian */
#define VECIO_VERSION    1
#define VECIO_HEADER_SZ  4096

#define VECIO_FLAG_CLEAN_SHUTDOWN  0x0001u

/* On-disk header.  Manually laid out — not packing-attribute-dependent. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t dim;
    uint32_t M;
    uint32_t ef_construction;
    float    alpha;
    uint32_t metric;
    uint32_t seed;
    uint64_t num_nodes;
    uint64_t medoid;
    uint64_t vectors_offset;
    uint64_t neighbors_offset;
    uint64_t degree_offset;
    uint64_t ids_offset;
    uint64_t tombstones_offset;
} vecio_header_t;

/* ----- I/O helpers --------------------------------------------------- */

static int
write_all(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w; left -= (size_t)w;
    }
    return 0;
}

static int
read_all(int fd, void *buf, size_t n)
{
    char *p = (char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -2;   /* EOF */
        p += r; left -= (size_t)r;
    }
    return 0;
}

static int
write_zero_pad(int fd, size_t n)
{
    static const char zeros[512];
    while (n > 0) {
        size_t chunk = (n > sizeof(zeros)) ? sizeof(zeros) : n;
        if (write_all(fd, zeros, chunk) < 0) return -1;
        n -= chunk;
    }
    return 0;
}

/* ----- Write --------------------------------------------------------- */

int
vecio_write(const char *path, const vec_graph_t *g)
{
    vecio_err = VECIO_OK; vecio_msg[0] = '\0';

    if (!path || !g) {
        vecio_set_error(VECIO_ERR_OPEN, "null path or graph");
        return VECIO_ERR_OPEN;
    }

    /* Build temp path: "<path>.new". */
    size_t plen = strlen(path);
    char *tmp = (char *)malloc(plen + 5);
    if (!tmp) { vecio_set_error(VECIO_ERR_NOMEM, "alloc tmp path"); return VECIO_ERR_NOMEM; }
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".new", 5);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        vecio_set_error(VECIO_ERR_OPEN, "open(%s): %s", tmp, strerror(errno));
        free(tmp);
        return VECIO_ERR_OPEN;
    }

    int dim = g->params.dim;
    int M = g->params.M;
    size_t n = g->n;

    /* Compute offsets (header is fixed at VECIO_HEADER_SZ). */
    vecio_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = VECIO_MAGIC;
    h.version = VECIO_VERSION;
    h.flags = 0;   /* CLEAN_SHUTDOWN set after fsync, in a re-write below */
    h.dim = (uint32_t)dim;
    h.M = (uint32_t)M;
    h.ef_construction = (uint32_t)g->params.ef_construction;
    h.alpha = g->params.alpha;
    h.metric = (uint32_t)g->params.metric;
    h.seed = g->params.seed;
    h.num_nodes = (uint64_t)n;
    h.medoid = (uint64_t)g->medoid;

    uint64_t off = VECIO_HEADER_SZ;
    h.vectors_offset = off;     off += (uint64_t)n * dim * sizeof(float);
    h.neighbors_offset = off;   off += (uint64_t)n * M * sizeof(vec_idx_t);
    h.degree_offset = off;      off += (uint64_t)n * sizeof(uint16_t);
    h.ids_offset = off;         off += (uint64_t)n * sizeof(vec_id_t);
    h.tombstones_offset = off;  off += (uint64_t)((n + 7) / 8);

    /* Write header (no CLEAN_SHUTDOWN flag yet). */
    if (write_all(fd, &h, sizeof(h)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write header: %s", strerror(errno));
        goto io_err;
    }
    if (write_zero_pad(fd, VECIO_HEADER_SZ - sizeof(h)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "pad header: %s", strerror(errno));
        goto io_err;
    }

    /* Vectors. */
    if (n > 0 && write_all(fd, g->vectors, (size_t)n * dim * sizeof(float)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write vectors: %s", strerror(errno));
        goto io_err;
    }

    /* Neighbors. */
    if (n > 0 && write_all(fd, g->neighbors, (size_t)n * M * sizeof(vec_idx_t)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write neighbors: %s", strerror(errno));
        goto io_err;
    }

    /* Degrees. */
    if (n > 0 && write_all(fd, g->degree, (size_t)n * sizeof(uint16_t)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write degrees: %s", strerror(errno));
        goto io_err;
    }

    /* External ids. */
    if (n > 0 && write_all(fd, g->external_ids, (size_t)n * sizeof(vec_id_t)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write ids: %s", strerror(errno));
        goto io_err;
    }

    /* Tombstones. */
    if (n > 0 && write_all(fd, g->tombstones, (n + 7) / 8) < 0) {
        vecio_set_error(VECIO_ERR_IO, "write tombstones: %s", strerror(errno));
        goto io_err;
    }

    /* fsync the data, then re-write header with CLEAN_SHUTDOWN flag set. */
    if (fsync(fd) < 0) {
        vecio_set_error(VECIO_ERR_IO, "fsync data: %s", strerror(errno));
        goto io_err;
    }

    h.flags |= VECIO_FLAG_CLEAN_SHUTDOWN;
    if (lseek(fd, 0, SEEK_SET) < 0) {
        vecio_set_error(VECIO_ERR_IO, "lseek: %s", strerror(errno));
        goto io_err;
    }
    if (write_all(fd, &h, sizeof(h)) < 0) {
        vecio_set_error(VECIO_ERR_IO, "rewrite header: %s", strerror(errno));
        goto io_err;
    }
    if (fsync(fd) < 0) {
        vecio_set_error(VECIO_ERR_IO, "fsync header: %s", strerror(errno));
        goto io_err;
    }
    close(fd);
    fd = -1;

    /* Atomic rename. */
    if (rename(tmp, path) < 0) {
        vecio_set_error(VECIO_ERR_RENAME, "rename(%s,%s): %s",
                        tmp, path, strerror(errno));
        free(tmp);
        return VECIO_ERR_RENAME;
    }

    free(tmp);
    return VECIO_OK;

io_err:
    if (fd >= 0) close(fd);
    /* Leave the .new file for diagnosis; caller can clean up. */
    free(tmp);
    return vecio_err;
}

/* ----- Read ---------------------------------------------------------- */

vec_graph_t *
vecio_read(const char *path)
{
    vecio_err = VECIO_OK; vecio_msg[0] = '\0';

    if (!path) {
        vecio_set_error(VECIO_ERR_OPEN, "null path");
        return NULL;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        vecio_set_error(VECIO_ERR_OPEN, "open(%s): %s", path, strerror(errno));
        return NULL;
    }

    vecio_header_t h;
    int rc = read_all(fd, &h, sizeof(h));
    if (rc < 0) {
        vecio_set_error(VECIO_ERR_IO, "read header: %s", strerror(errno));
        close(fd); return NULL;
    }
    if (rc == -2) {
        vecio_set_error(VECIO_ERR_TRUNCATED, "header truncated");
        close(fd); return NULL;
    }

    if (h.magic != VECIO_MAGIC) {
        vecio_set_error(VECIO_ERR_MAGIC,
            "bad magic 0x%08x (expected 0x%08x)", h.magic, VECIO_MAGIC);
        close(fd); return NULL;
    }
    if (h.version != VECIO_VERSION) {
        vecio_set_error(VECIO_ERR_VERSION,
            "version %u not supported (expected %u)",
            (unsigned)h.version, VECIO_VERSION);
        close(fd); return NULL;
    }
    if (!(h.flags & VECIO_FLAG_CLEAN_SHUTDOWN)) {
        vecio_set_error(VECIO_ERR_DIRTY,
            "FLAG_CLEAN_SHUTDOWN not set; file may be incomplete");
        close(fd); return NULL;
    }
    if (h.dim == 0 || h.M == 0) {
        vecio_set_error(VECIO_ERR_PARAMS,
            "invalid params: dim=%u M=%u", h.dim, h.M);
        close(fd); return NULL;
    }

    /* Allocate graph and copy params. */
    vec_graph_params_t params;
    memset(&params, 0, sizeof(params));
    params.dim = (int)h.dim;
    params.M = (int)h.M;
    params.ef_construction = (int)h.ef_construction;
    params.alpha = h.alpha;
    params.metric = (vec_metric_t)h.metric;
    params.seed = h.seed;
    params.max_build_memory = 0;

    size_t n = (size_t)h.num_nodes;

    vec_graph_t *g = (vec_graph_t *)calloc(1, sizeof(*g));
    if (!g) {
        vecio_set_error(VECIO_ERR_NOMEM, "alloc graph");
        close(fd); return NULL;
    }
    g->params = params;
    g->n = n;
    g->cap = n;
    g->medoid = (vec_idx_t)h.medoid;

    if (n == 0) {
        /* Empty graph — valid. */
        if (vec_id_map_init(&g->id_map, 0) < 0) {
            vecio_set_error(VECIO_ERR_NOMEM, "id_map init");
            vec_graph_free(g); close(fd); return NULL;
        }
        close(fd);
        return g;
    }

    g->vectors      = (float *)    malloc((size_t)n * h.dim * sizeof(float));
    g->neighbors    = (vec_idx_t *)malloc((size_t)n * h.M * sizeof(vec_idx_t));
    g->degree       = (uint16_t *) malloc((size_t)n * sizeof(uint16_t));
    g->external_ids = (vec_id_t *) malloc((size_t)n * sizeof(vec_id_t));
    g->tombstones   = (uint8_t *)  malloc((n + 7) / 8);

    if (!g->vectors || !g->neighbors || !g->degree ||
        !g->external_ids || !g->tombstones) {
        vecio_set_error(VECIO_ERR_NOMEM, "alloc node arrays");
        vec_graph_free(g); close(fd); return NULL;
    }
    if (vec_id_map_init(&g->id_map, n) < 0) {
        vecio_set_error(VECIO_ERR_NOMEM, "id_map init");
        vec_graph_free(g); close(fd); return NULL;
    }

    /* Seek + read each block. */
    struct { off_t off; void *buf; size_t sz; const char *what; } blocks[] = {
        { (off_t)h.vectors_offset,    g->vectors,      (size_t)n * h.dim * sizeof(float),   "vectors" },
        { (off_t)h.neighbors_offset,  g->neighbors,    (size_t)n * h.M   * sizeof(vec_idx_t),"neighbors" },
        { (off_t)h.degree_offset,     g->degree,       (size_t)n * sizeof(uint16_t),         "degrees" },
        { (off_t)h.ids_offset,        g->external_ids, (size_t)n * sizeof(vec_id_t),         "ids" },
        { (off_t)h.tombstones_offset, g->tombstones,   (n + 7) / 8,                          "tombstones" },
    };
    for (size_t i = 0; i < sizeof(blocks)/sizeof(blocks[0]); i++) {
        if (lseek(fd, blocks[i].off, SEEK_SET) < 0) {
            vecio_set_error(VECIO_ERR_IO, "lseek %s: %s",
                            blocks[i].what, strerror(errno));
            vec_graph_free(g); close(fd); return NULL;
        }
        rc = read_all(fd, blocks[i].buf, blocks[i].sz);
        if (rc < 0) {
            vecio_set_error(VECIO_ERR_IO, "read %s: %s",
                            blocks[i].what, strerror(errno));
            vec_graph_free(g); close(fd); return NULL;
        }
        if (rc == -2) {
            vecio_set_error(VECIO_ERR_TRUNCATED, "%s block truncated",
                            blocks[i].what);
            vec_graph_free(g); close(fd); return NULL;
        }
    }
    close(fd);

    /* Rebuild id_map from external_ids[]. */
    for (size_t i = 0; i < n; i++) {
        if (vec_id_map_put(&g->id_map, g->external_ids[i], (vec_idx_t)i) < 0) {
            vecio_set_error(VECIO_ERR_NOMEM, "rebuild id_map");
            vec_graph_free(g); return NULL;
        }
    }

    return g;
}
