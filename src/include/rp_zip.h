/* Copyright (C) 2026 Aaron Flin - All Rights Reserved
 *
 * Two-layer zip reader:
 *
 *   - Handle-based API (rp_zip_open / rp_zip_close / rp_zip_h_*) reads any
 *     zip file from disk via mmap.  Returns an opaque rp_zip_t * handle.
 *
 *   - Singleton API (rp_zip_init / rp_zip_lookup / rp_zip_read / etc.) is a
 *     thin wrapper around an internal handle that points at the SFX-style
 *     zip appended to rampart_exec.  Used by the bundled-rampart features
 *     (require() integration, fileserver, payload* utils, etc.).
 *
 * Backed by mmap; entries are looked up via bsearch on a name-sorted array.
 * No content cache -- callers (Duktape module loader, fileserver, JS
 * utilities) handle their own caching.
 *
 * Format support: zip64 markers are detected and rejected.  Compression
 * methods supported: 0 (stored) and 8 (deflate).  Encryption is not
 * supported.
 */
#ifndef RP_ZIP_H
#define RP_ZIP_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    const char *name;       /* not NUL-terminated; pointer into the mmap */
    uint16_t name_len;
    uint16_t method;        /* 0=stored, 8=deflate; others rejected */
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t crc32;
    uint64_t lfh_offset;    /* relative to start of zip data (pre-SFX-shift) */
    uint32_t mode;          /* Unix file mode; S_IFREG|0644 default if unknown */
    time_t   mtime;         /* converted from DOS time+date fields, 0 if unknown */
} rp_zip_entry;

/* ============== Handle-based API (any zip on disk) ============== */

typedef struct rp_zip_s rp_zip_t;

/* Open a zip on disk.  Returns NULL on failure (file missing, not a zip,
   zip64, encrypted, etc.). */
rp_zip_t *rp_zip_open(const char *path);

/* Release a handle.  Unmaps the file and frees the index. */
void rp_zip_close(rp_zip_t *z);

/* Look up an entry by NUL-terminated name.  Does NOT follow symlinks --
   if the entry is S_ISLNK, returns the symlink itself. */
const rp_zip_entry *rp_zip_h_lookup(rp_zip_t *z, const char *name);

/* Like rp_zip_h_lookup, but follows symlinks: if the entry is S_ISLNK,
   reads its target string, normalizes it relative to the symlink's parent
   (rejecting absolute disk paths and any normalization that escapes the
   zip root), and looks up the target.  Repeats up to 8 hops then bails.
   Returns the final non-symlink entry on success, or NULL.

   Use this when you want the bytes of the file the user meant (auto-run,
   require, fileserver, payloadGet/zipGet).  Use rp_zip_h_lookup when you
   want to preserve the symlink itself (list, extract, raw_ptr). */
const rp_zip_entry *rp_zip_h_resolve(rp_zip_t *z, const char *name);

/* Total number of entries (including 0-byte directory markers). */
size_t rp_zip_h_count(rp_zip_t *z);

/* Iterate entries by index. */
const rp_zip_entry *rp_zip_h_at(rp_zip_t *z, size_t i);

/* Read and (if compressed) decompress an entry.  *out is malloc'd with one
   trailing NUL byte and the caller must free.  *out_len is uncompressed
   size.  Returns 0 on success, -1 on error. */
int rp_zip_h_read(rp_zip_t *z, const rp_zip_entry *e,
                  unsigned char **out, size_t *out_len);

/* Zero-copy access to an entry's stored (compressed-or-not) bytes.
   *out points into the handle's mmap and remains valid until rp_zip_close.
   *out_size = e->comp_size.  Returns 0 on success. */
int rp_zip_h_raw_ptr(rp_zip_t *z, const rp_zip_entry *e,
                     const unsigned char **out, size_t *out_size);

/* ============== Singleton API (appended-to-rampart_exec zip) ==============
 * Equivalent to the handle API above using an internal handle backed by
 * the executable's appended payload.  Requires rp_has_zip_payload != 0.
 */

int                  rp_zip_init(void);
const rp_zip_entry  *rp_zip_lookup(const char *name);
const rp_zip_entry  *rp_zip_resolve(const char *name); /* follows symlinks */
size_t               rp_zip_count(void);
const rp_zip_entry  *rp_zip_at(size_t i);
int                  rp_zip_read(const rp_zip_entry *e,
                                 unsigned char **out, size_t *out_len);
int                  rp_zip_raw_ptr(const rp_zip_entry *e,
                                    const unsigned char **out, size_t *out_size);
/* Byte offset at which the appended zip begins (= size of the bare rampart
   binary).  Returns -1 if the singleton handle is not initialized. */
off_t                rp_zip_payload_offset(void);

/* ============== Filesystem shims (zip-aware drop-in wrappers) ==============
 *
 * These wrappers transparently handle paths beginning with ":zip:".  For
 * any other path they call straight through to the libc equivalent.  Use
 * them in place of fopen/stat/lstat/access/fclose where the caller may
 * receive a :zip: path from a script.
 *
 * rp_fopen:  read modes ("r","rb","r+" without write) on :zip: -> FILE* via
 *            fmemopen over decompressed bytes; the malloc'd backing buffer
 *            is freed automatically when the FILE* is closed via rp_fclose.
 *            Write modes on :zip: -> NULL with errno = EROFS.
 *
 * rp_fclose: required only if you opened with rp_fopen (a regular fopen'd
 *            FILE* is also handled correctly).  Frees any zip backing buf.
 *
 * rp_stat / rp_lstat:  on :zip:, fill struct stat from the zip entry
 *            (st_size, st_mode, st_mtime, st_nlink=1).  st_dev/st_ino are
 *            zeroed.  rp_lstat does not follow symlinks.
 *
 * rp_access: on :zip:, F_OK is "exists", R_OK always succeeds for entries,
 *            X_OK consults entry mode bits, W_OK always fails (EROFS).
 */
FILE *rp_fopen (const char *path, const char *mode);
int   rp_fclose(FILE *f);
int   rp_stat  (const char *path, struct stat *out);
int   rp_lstat (const char *path, struct stat *out);
int   rp_access(const char *path, int mode);
int   rp_fstat (FILE *f, struct stat *out);    /* succeeds on rp_fopen'd zip FILE*'s too */
int   rp_fstat_is_zip(FILE *f);                /* 1 if FILE* came from rp_fopen on :zip: path */

/* Path normalizer used internally and by zip-aware readdir.
   Joins base/rel (or just rel if base is empty), collapses "." and "..",
   rejects absolute disk paths and any escape above the root.
   Preserves a trailing '/' if rel had one. */
int rp_zip_normalize(const char *base, const char *rel,
                     char *out, size_t outsz);

#endif /* RP_ZIP_H */
