/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 * Reader for zip archives.
 *
 * Two layers:
 *   - Handle-based: rp_zip_open()/close() open any zip file from disk and
 *     return an opaque handle.  rp_zip_h_* functions operate on handles.
 *   - Singleton:    rp_zip_init()/lookup()/etc. operate on a built-in
 *     handle backed by the SFX-style zip appended to rampart_exec, used by
 *     the bundled-rampart features.  These are thin wrappers around the
 *     handle-based functions.
 *
 * Format support: zip64 markers are detected and rejected with an error.
 * Compression methods supported: 0 (stored) and 8 (deflate).  Encryption
 * is not supported.  General-purpose bit 3 (data-descriptor / streaming
 * sizes) is not supported -- the central directory entry must carry
 * accurate sizes, which is the case for all non-streaming writers.
 */

#include "rampart.h"
#include "rp_zip.h"
#include "libdeflate.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

struct rp_zip_s {
    unsigned char *zmap;
    size_t         zmap_len;
    off_t          sfx_shift;
    rp_zip_entry  *entries;
    size_t         n_entries;
};

static inline uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0]      |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] <<16) |
           ((uint32_t)p[3] <<24);
}

static int name_cmp(const char *a, size_t alen, const char *b, size_t blen)
{
    size_t min_len = alen < blen ? alen : blen;
    int c = memcmp(a, b, min_len);
    if (c) return c;
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static int cmp_entry(const void *a, const void *b)
{
    const rp_zip_entry *ea = (const rp_zip_entry *)a;
    const rp_zip_entry *eb = (const rp_zip_entry *)b;
    return name_cmp(ea->name, ea->name_len, eb->name, eb->name_len);
}

static int cmp_lookup(const void *key, const void *elem)
{
    const char *q = (const char *)key;
    const rp_zip_entry *e = (const rp_zip_entry *)elem;
    return name_cmp(q, strlen(q), e->name, e->name_len);
}

/* Scan the tail of a buffer for the End-of-Central-Directory signature.
   Returns the offset within zmap on success, or (off_t)-1 on miss. */
static off_t find_eocd(const unsigned char *zmap, size_t zmap_len)
{
    if (zmap_len < 22) return (off_t)-1;
    off_t scan_start = (zmap_len > 65557) ? (off_t)(zmap_len - 65557) : 0;
    for (off_t i = (off_t)zmap_len - 22; i >= scan_start; i--)
    {
        if (zmap[i]==0x50 && zmap[i+1]==0x4b && zmap[i+2]==0x05 && zmap[i+3]==0x06)
        {
            uint16_t comment_len = rd16(zmap + i + 20);
            if ((off_t)comment_len + 22 + i == (off_t)zmap_len)
                return i;
        }
    }
    return (off_t)-1;
}

/* Convert DOS time+date pair to time_t (local time, like unzip does). */
static time_t dos_to_time_t(uint16_t dos_time, uint16_t dos_date)
{
    if (dos_date == 0 && dos_time == 0) return 0;
    struct tm tm = {0};
    tm.tm_sec  = (dos_time & 0x1f) * 2;
    tm.tm_min  = (dos_time >> 5) & 0x3f;
    tm.tm_hour = (dos_time >> 11) & 0x1f;
    tm.tm_mday = (dos_date & 0x1f);
    tm.tm_mon  = ((dos_date >> 5) & 0x0f) - 1;
    tm.tm_year = ((dos_date >> 9) & 0x7f) + 80;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    return (t < 0) ? 0 : t;
}

/* Parse central directory into z->entries given a known EOCD offset.
   Sets z->sfx_shift, z->entries, z->n_entries.  Returns 0 on success. */
static int parse_central_directory(rp_zip_t *z, off_t eocd_off)
{
    if (eocd_off + 22 > (off_t)z->zmap_len) return -1;
    const unsigned char *eocd = z->zmap + eocd_off;

    uint16_t n_records = rd16(eocd + 10);
    uint32_t cd_size   = rd32(eocd + 12);
    uint32_t cd_offset = rd32(eocd + 16);

    if (n_records == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu)
    {
        fprintf(stderr, "rp_zip: zip64 archives are not supported\n");
        return -1;
    }

    z->sfx_shift = eocd_off - (off_t)cd_offset - (off_t)cd_size;
    if (z->sfx_shift < 0)
    {
        fprintf(stderr, "rp_zip: negative SFX shift -- archive layout rejected\n");
        return -1;
    }

    off_t cd_start = z->sfx_shift + (off_t)cd_offset;
    if (cd_start < 0 || cd_start + (off_t)cd_size > (off_t)z->zmap_len)
    {
        fprintf(stderr, "rp_zip: central directory falls outside file\n");
        return -1;
    }

    z->entries = (rp_zip_entry *)malloc(sizeof(rp_zip_entry) * (size_t)n_records);
    if (!z->entries) return -1;

    const unsigned char *p      = z->zmap + cd_start;
    const unsigned char *cd_end = p + cd_size;
    size_t i = 0;
    while (i < n_records && p + 46 <= cd_end)
    {
        if (p[0] != 0x50 || p[1] != 0x4b || p[2] != 0x01 || p[3] != 0x02)
        {
            fprintf(stderr, "rp_zip: bad central directory entry signature\n");
            return -1;
        }

        uint16_t version_made_by = rd16(p + 4);
        uint16_t gp_flag   = rd16(p + 8);
        uint16_t method    = rd16(p + 10);
        uint16_t dos_time  = rd16(p + 12);
        uint16_t dos_date  = rd16(p + 14);
        uint32_t crc32     = rd32(p + 16);
        uint32_t comp_sz   = rd32(p + 20);
        uint32_t unc_sz    = rd32(p + 24);
        uint16_t name_len  = rd16(p + 28);
        uint16_t extra_len = rd16(p + 30);
        uint16_t cmt_len   = rd16(p + 32);
        uint32_t ext_attrs = rd32(p + 38);
        uint32_t lfh_off   = rd32(p + 42);

        if (comp_sz == 0xFFFFFFFFu || unc_sz == 0xFFFFFFFFu || lfh_off == 0xFFFFFFFFu)
        {
            fprintf(stderr, "rp_zip: zip64 extension required for entry, not supported\n");
            return -1;
        }
        if (gp_flag & 0x0001)
        {
            fprintf(stderr, "rp_zip: encrypted entries are not supported\n");
            return -1;
        }
        if (p + 46 + name_len + extra_len + cmt_len > cd_end)
        {
            fprintf(stderr, "rp_zip: central directory entry overruns CD region\n");
            return -1;
        }

        z->entries[i].name        = (const char *)(p + 46);
        z->entries[i].name_len    = name_len;
        z->entries[i].method      = method;
        z->entries[i].comp_size   = comp_sz;
        z->entries[i].uncomp_size = unc_sz;
        z->entries[i].crc32       = crc32;
        z->entries[i].lfh_offset  = (uint64_t)lfh_off;

        /* Mode: trust external_attrs only when version-made-by signals Unix
           (high byte = 3).  Otherwise pick a sensible default based on
           whether this is a directory marker. */
        int is_dir = (name_len > 0 && p[46 + name_len - 1] == '/');
        if ((version_made_by >> 8) == 3)
            z->entries[i].mode = ext_attrs >> 16;
        else
            z->entries[i].mode = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);

        z->entries[i].mtime = dos_to_time_t(dos_time, dos_date);

        p += 46 + name_len + extra_len + cmt_len;
        i++;
    }
    z->n_entries = i;
    qsort(z->entries, z->n_entries, sizeof(rp_zip_entry), cmp_entry);
    return 0;
}

rp_zip_t *rp_zip_open(const char *path)
{
    if (!path) return NULL;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    if (st.st_size < 22) { close(fd); return NULL; }

    rp_zip_t *z = (rp_zip_t *)calloc(1, sizeof(*z));
    if (!z) { close(fd); return NULL; }

    z->zmap_len = (size_t)st.st_size;
    z->zmap = mmap(NULL, z->zmap_len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (z->zmap == MAP_FAILED) { z->zmap = NULL; free(z); return NULL; }

    off_t eocd = find_eocd(z->zmap, z->zmap_len);
    if (eocd < 0)
    {
        munmap(z->zmap, z->zmap_len);
        free(z);
        return NULL;
    }

    if (parse_central_directory(z, eocd) != 0)
    {
        if (z->entries) free(z->entries);
        munmap(z->zmap, z->zmap_len);
        free(z);
        return NULL;
    }

    return z;
}

void rp_zip_close(rp_zip_t *z)
{
    if (!z) return;
    if (z->entries) free(z->entries);
    if (z->zmap) munmap(z->zmap, z->zmap_len);
    free(z);
}

const rp_zip_entry *rp_zip_h_lookup(rp_zip_t *z, const char *name)
{
    if (!z || !name) return NULL;
    return (const rp_zip_entry *)bsearch(name, z->entries, z->n_entries,
                                         sizeof(rp_zip_entry), cmp_lookup);
}

size_t rp_zip_h_count(rp_zip_t *z)
{
    return z ? z->n_entries : 0;
}

const rp_zip_entry *rp_zip_h_at(rp_zip_t *z, size_t i)
{
    if (!z || i >= z->n_entries) return NULL;
    return &z->entries[i];
}

/* Locate the start of an entry's stored bytes within the handle's mmap by
   parsing its local file header (name+extra lengths there can differ from
   the central directory's, so we always read the LFH). */
static const unsigned char *
rp_zip_data_ptr_h(rp_zip_t *z, const rp_zip_entry *e)
{
    off_t lfh_abs = z->sfx_shift + (off_t)e->lfh_offset;
    if (lfh_abs < 0 || lfh_abs + 30 > (off_t)z->zmap_len) return NULL;

    const unsigned char *lfh = z->zmap + lfh_abs;
    if (lfh[0] != 0x50 || lfh[1] != 0x4b || lfh[2] != 0x03 || lfh[3] != 0x04)
        return NULL;

    uint16_t lfh_name_len  = rd16(lfh + 26);
    uint16_t lfh_extra_len = rd16(lfh + 28);

    off_t data_off = lfh_abs + 30 + (off_t)lfh_name_len + (off_t)lfh_extra_len;
    if (data_off + (off_t)e->comp_size > (off_t)z->zmap_len) return NULL;
    return z->zmap + data_off;
}

int rp_zip_h_raw_ptr(rp_zip_t *z, const rp_zip_entry *e,
                     const unsigned char **out, size_t *out_size)
{
    if (!z || !e || !out || !out_size) return -1;
    const unsigned char *data = rp_zip_data_ptr_h(z, e);
    if (!data) return -1;
    *out = data;
    *out_size = (size_t)e->comp_size;
    return 0;
}

/* Normalize "<base>/<rel>" with simple . / .. collapsing; refuse absolute
   targets and refuse any path that would escape the zip root.  Output is
   NUL-terminated.  Returns 0 on success, -1 on reject. */
int rp_zip_normalize(const char *base, const char *rel,
                     char *out, size_t outsz)
{
    if (rel[0] == '/') return -1;  /* absolute disk path -- not in zip */

    char buf[PATH_MAX];
    int n;
    if (base && *base)
        n = snprintf(buf, sizeof(buf), "%s/%s", base, rel);
    else
        n = snprintf(buf, sizeof(buf), "%s", rel);
    if (n < 0 || n >= (int)sizeof(buf)) return -1;

    /* Split on '/', collapse '.' / '..'.  Rebuild into out[]. */
    char *parts[256];
    int n_parts = 0;
    char *p = buf;
    while (*p)
    {
        char *seg = p;
        while (*p && *p != '/') p++;
        size_t sl = (size_t)(p - seg);
        if (*p) { *p = '\0'; p++; }

        if (sl == 0) continue;                                  /* empty */
        if (sl == 1 && seg[0] == '.') continue;                 /* . */
        if (sl == 2 && seg[0] == '.' && seg[1] == '.')          /* .. */
        {
            if (n_parts > 0) n_parts--;
            else return -1;  /* would escape root */
            continue;
        }
        if (n_parts >= (int)(sizeof(parts)/sizeof(parts[0]))) return -1;
        parts[n_parts++] = seg;
    }

    /* Preserve a trailing '/' if the input had one (directory-marker
       entries like "html/" need this to remain distinguishable from the
       file "html").  buf has at least one byte at this point because we
       stripped to make the segment list. */
    int trailing_slash = 0;
    {
        size_t bl = strlen(rel);
        if (bl > 0 && rel[bl - 1] == '/') trailing_slash = 1;
    }

    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < n_parts; i++)
    {
        size_t pl = strlen(parts[i]);
        size_t need = pos + (pos > 0 ? 1 : 0) + pl + 1;
        if (need > outsz) return -1;
        if (pos > 0) out[pos++] = '/';
        memcpy(out + pos, parts[i], pl);
        pos += pl;
    }
    if (trailing_slash && n_parts > 0)
    {
        if (pos + 2 > outsz) return -1;
        out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

#define RP_ZIP_MAX_LINK_HOPS 8

const rp_zip_entry *rp_zip_h_resolve(rp_zip_t *z, const char *name)
{
    if (!z || !name) return NULL;
    /* Normalize the input first so callers can pass paths containing
       "." / ".." segments (e.g. ":zip:/dir/../foo.js" stripped to
       "dir/../foo.js") and have them collapse to the actual entry name. */
    char cur[PATH_MAX];
    if (rp_zip_normalize("", name, cur, sizeof(cur)) != 0) return NULL;

    for (int hops = 0; hops < RP_ZIP_MAX_LINK_HOPS; hops++)
    {
        const rp_zip_entry *e = rp_zip_h_lookup(z, cur);
        if (!e) return NULL;
        if (!S_ISLNK(e->mode)) return e;

        /* Read the link target string (entry's stored content). */
        unsigned char *target = NULL;
        size_t tlen = 0;
        if (rp_zip_h_read(z, e, &target, &tlen) != 0) return NULL;

        /* Determine the symlink's parent directory for relative resolution. */
        char base[PATH_MAX];
        const char *slash = strrchr(cur, '/');
        if (slash)
        {
            size_t bl = (size_t)(slash - cur);
            if (bl >= sizeof(base)) { free(target); return NULL; }
            memcpy(base, cur, bl);
            base[bl] = '\0';
        }
        else
        {
            base[0] = '\0';
        }

        char next[PATH_MAX];
        int rc = rp_zip_normalize(base, (const char *)target, next, sizeof(next));
        free(target);
        if (rc != 0) return NULL;
        memcpy(cur, next, strlen(next) + 1);
    }
    fprintf(stderr, "rp_zip: symlink chain exceeds %d hops at '%s'\n",
            RP_ZIP_MAX_LINK_HOPS, name);
    return NULL;
}

int rp_zip_h_read(rp_zip_t *z, const rp_zip_entry *e,
                  unsigned char **out, size_t *out_len)
{
    if (!z || !e || !out || !out_len) return -1;

    const unsigned char *data = rp_zip_data_ptr_h(z, e);
    if (!data) return -1;

    if (e->method == 0)
    {
        if (e->comp_size != e->uncomp_size) return -1;
        unsigned char *buf = (unsigned char *)malloc((size_t)e->uncomp_size + 1);
        if (!buf) return -1;
        memcpy(buf, data, e->uncomp_size);
        buf[e->uncomp_size] = 0;
        *out = buf;
        *out_len = (size_t)e->uncomp_size;
        return 0;
    }

    if (e->method == 8)
    {
        unsigned char *buf = (unsigned char *)malloc((size_t)e->uncomp_size + 1);
        if (!buf) return -1;

        struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
        if (!d) { free(buf); return -1; }

        size_t actual = 0;
        enum libdeflate_result r = libdeflate_deflate_decompress(
            d, data, (size_t)e->comp_size,
            buf,  (size_t)e->uncomp_size, &actual);
        libdeflate_free_decompressor(d);

        if (r != LIBDEFLATE_SUCCESS || actual != (size_t)e->uncomp_size)
        {
            free(buf);
            return -1;
        }
        buf[e->uncomp_size] = 0;
        *out = buf;
        *out_len = (size_t)e->uncomp_size;
        return 0;
    }

    fprintf(stderr, "rp_zip: unsupported compression method %u\n", (unsigned)e->method);
    return -1;
}

/* ============================================================
 * Singleton wrappers backed by the SFX-style zip in rampart_exec.
 * ============================================================ */

static rp_zip_t *g_payload = NULL;

int rp_zip_init(void)
{
    if (g_payload) return 0;
    if (!rp_has_zip_payload) return -1;
    g_payload = rp_zip_open(rampart_exec);
    return g_payload ? 0 : -1;
}

const rp_zip_entry *rp_zip_lookup(const char *name)
{
    return rp_zip_h_lookup(g_payload, name);
}

const rp_zip_entry *rp_zip_resolve(const char *name)
{
    return rp_zip_h_resolve(g_payload, name);
}

size_t rp_zip_count(void)
{
    return rp_zip_h_count(g_payload);
}

/* Byte offset within rampart_exec where the appended zip begins, equal to
   the size of the bare (zip-free) executable.  Useful for installers that
   want to slice off the original rampart binary. */
off_t rp_zip_payload_offset(void)
{
    return g_payload ? g_payload->sfx_shift : (off_t)-1;
}

const rp_zip_entry *rp_zip_at(size_t i)
{
    return rp_zip_h_at(g_payload, i);
}

int rp_zip_read(const rp_zip_entry *e, unsigned char **out, size_t *out_len)
{
    return rp_zip_h_read(g_payload, e, out, out_len);
}

int rp_zip_raw_ptr(const rp_zip_entry *e, const unsigned char **out, size_t *out_size)
{
    return rp_zip_h_raw_ptr(g_payload, e, out, out_size);
}

/* ============================================================
 * Filesystem shims: zip-aware drop-ins for fopen/stat/lstat/access.
 * Plus rp_fclose that releases the fmemopen backing buffer for
 * FILE*'s opened by rp_fopen on a :zip: path.
 * ============================================================ */

typedef struct rp_zfile_s {
    FILE    *f;
    void    *buf;
    size_t   size;
    mode_t   mode;
    time_t   mtime;
    struct rp_zfile_s *next;
} rp_zfile_t;

static rp_zfile_t      *rp_zfile_head = NULL;
static pthread_mutex_t  rp_zfile_lock = PTHREAD_MUTEX_INITIALIZER;

static void rp_zfile_track(FILE *f, void *buf, const rp_zip_entry *e)
{
    rp_zfile_t *t = (rp_zfile_t *)malloc(sizeof(*t));
    if (!t) return;  /* extremely unlikely; buffer leaks rather than crash */
    t->f = f;
    t->buf = buf;
    t->size = e ? e->uncomp_size : 0;
    t->mode = e ? (mode_t)e->mode : (S_IFREG | 0644);
    t->mtime = e ? e->mtime : 0;
    pthread_mutex_lock(&rp_zfile_lock);
    t->next = rp_zfile_head;
    rp_zfile_head = t;
    pthread_mutex_unlock(&rp_zfile_lock);
}

/* Look up a tracked FILE* without removing it.  Caller holds no lock; the
   returned pointer is only valid while the caller knows the FILE* hasn't
   been closed.  Returns NULL if not tracked. */
static const rp_zfile_t *rp_zfile_peek(FILE *f)
{
    const rp_zfile_t *r = NULL;
    pthread_mutex_lock(&rp_zfile_lock);
    for (rp_zfile_t *p = rp_zfile_head; p; p = p->next)
        if (p->f == f) { r = p; break; }
    pthread_mutex_unlock(&rp_zfile_lock);
    return r;
}

static void *rp_zfile_untrack(FILE *f)
{
    void *buf = NULL;
    pthread_mutex_lock(&rp_zfile_lock);
    rp_zfile_t **p = &rp_zfile_head;
    while (*p)
    {
        if ((*p)->f == f)
        {
            rp_zfile_t *dead = *p;
            buf = dead->buf;
            *p = dead->next;
            free(dead);
            break;
        }
        p = &(*p)->next;
    }
    pthread_mutex_unlock(&rp_zfile_lock);
    return buf;
}

static int is_zip_path(const char *p)
{
    return p && p[0] == ':' && strncmp(p, ":zip:", 5) == 0;
}

/* Strip ":zip:" or ":zip:/" prefix; returns pointer into path (may be ""). */
static const char *zip_strip(const char *path)
{
    const char *p = path + 5;       /* past ":zip:" */
    if (*p == '/') p++;
    return p;
}

static int is_pure_read_mode(const char *mode)
{
    if (!mode || mode[0] != 'r') return 0;
    for (const char *p = mode + 1; *p; p++)
        if (*p == '+' || *p == 'w' || *p == 'a') return 0;
    return 1;
}

FILE *rp_fopen(const char *path, const char *mode)
{
    if (!is_zip_path(path)) return fopen(path, mode);

    if (!rp_has_zip_payload) { errno = ENOENT; return NULL; }
    if (rp_zip_init() != 0) { errno = EIO; return NULL; }
    if (!is_pure_read_mode(mode)) { errno = EROFS; return NULL; }

    const rp_zip_entry *e = rp_zip_resolve(zip_strip(path));
    if (!e) { errno = ENOENT; return NULL; }
    if (S_ISDIR(e->mode)) { errno = EISDIR; return NULL; }

    unsigned char *buf = NULL;
    size_t len = 0;
    if (rp_zip_h_read(g_payload, e, &buf, &len) != 0) { errno = EIO; return NULL; }

    FILE *f = fmemopen(buf, len, "rb");
    if (!f) { int saved = errno; free(buf); errno = saved ? saved : ENOMEM; return NULL; }

    rp_zfile_track(f, buf, e);
    return f;
}

int rp_fstat_is_zip(FILE *f)
{
    return rp_zfile_peek(f) != NULL;
}

int rp_fstat(FILE *f, struct stat *out)
{
    if (!f || !out) { errno = EINVAL; return -1; }
    const rp_zfile_t *t = rp_zfile_peek(f);
    if (t)
    {
        memset(out, 0, sizeof(*out));
        out->st_mode  = t->mode;
        out->st_size  = (off_t)t->size;
        out->st_mtime = t->mtime;
        out->st_nlink = 1;
        return 0;
    }
    int fd = fileno(f);
    if (fd < 0) { errno = EBADF; return -1; }
    return fstat(fd, out);
}

int rp_fclose(FILE *f)
{
    if (!f) { errno = EINVAL; return EOF; }
    void *buf = rp_zfile_untrack(f);
    int rc = fclose(f);
    free(buf);
    return rc;
}

static void zip_fill_stat(const rp_zip_entry *e, struct stat *out)
{
    memset(out, 0, sizeof(*out));
    out->st_mode  = (mode_t)e->mode;
    out->st_size  = (off_t)e->uncomp_size;
    out->st_mtime = e->mtime;
    out->st_nlink = 1;
}

int rp_stat(const char *path, struct stat *out)
{
    if (!is_zip_path(path)) return stat(path, out);
    if (!rp_has_zip_payload) { errno = ENOENT; return -1; }
    if (rp_zip_init() != 0) { errno = EIO; return -1; }

    const rp_zip_entry *e = rp_zip_resolve(zip_strip(path));
    if (!e) { errno = ENOENT; return -1; }
    zip_fill_stat(e, out);
    return 0;
}

int rp_lstat(const char *path, struct stat *out)
{
    if (!is_zip_path(path)) return lstat(path, out);
    if (!rp_has_zip_payload) { errno = ENOENT; return -1; }
    if (rp_zip_init() != 0) { errno = EIO; return -1; }

    /* Normalize but do not follow symlinks. */
    char norm[PATH_MAX];
    if (rp_zip_normalize("", zip_strip(path), norm, sizeof(norm)) != 0) {
        errno = ENOENT; return -1;
    }
    const rp_zip_entry *e = rp_zip_lookup(norm);
    if (!e) { errno = ENOENT; return -1; }
    zip_fill_stat(e, out);
    return 0;
}

int rp_access(const char *path, int mode)
{
    if (!is_zip_path(path)) return access(path, mode);
    if (!rp_has_zip_payload) { errno = ENOENT; return -1; }
    if (rp_zip_init() != 0) { errno = EIO; return -1; }

    if (mode & W_OK) { errno = EROFS; return -1; }

    const rp_zip_entry *e = rp_zip_resolve(zip_strip(path));
    if (!e) { errno = ENOENT; return -1; }

    if (mode & X_OK) {
        if (!(e->mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            errno = EACCES; return -1;
        }
    }
    return 0;  /* F_OK and R_OK both succeed if entry exists */
}
