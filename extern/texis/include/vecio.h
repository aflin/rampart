/* vecio.h — persistence for vec_graph.
 *
 * Format (version 1): header + full-precision vectors + fixed-size
 * adjacency + degrees + external-id map + tombstone bitmap.  Vectors
 * live in the file; a future format revision can add a quantized-codes
 * block and drop the duplicated vectors per vector-index-design.md §14.1.
 *
 * Atomicity: writes go to a temp file `<path>.new`, fsync, then rename
 * over `<path>`.  Crash mid-write leaves the original `<path>` untouched.
 *
 * Crash detection: header carries FLAG_CLEAN_SHUTDOWN.  Set on successful
 * write, checked on read.  A missing flag means the file was not closed
 * cleanly; vecio_read fails with VECIO_ERR_DIRTY and the caller decides
 * whether to rebuild from source (the v1 strategy per design §9).
 *
 * No texis dependencies.
 */

#ifndef VECIO_H
#define VECIO_H

#include "vecgraph.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes returned from vecio_last_error_code(). */
typedef enum {
    VECIO_OK = 0,
    VECIO_ERR_OPEN = -1,        /* could not open file */
    VECIO_ERR_IO = -2,          /* read/write failed */
    VECIO_ERR_MAGIC = -3,       /* file magic doesn't match */
    VECIO_ERR_VERSION = -4,     /* unsupported file format version */
    VECIO_ERR_DIRTY = -5,       /* FLAG_CLEAN_SHUTDOWN missing — caller should rebuild */
    VECIO_ERR_TRUNCATED = -6,   /* file shorter than header expects */
    VECIO_ERR_PARAMS = -7,      /* params (dim, M) out of range */
    VECIO_ERR_NOMEM = -8,       /* allocation failure during load */
    VECIO_ERR_RENAME = -9,      /* atomic rename failed */
} vecio_err_t;

/* Suffix for the .vec file.  Caller is responsible for the leading path. */
#define VECIO_SUFFIX ".vec"

/* Write `g` to `path`.  Returns 0 on success, negative vecio_err_t on error.
 * Implementation: writes to "<path>.new", fsyncs, renames over `path`.
 * Existing `<path>` is preserved on error.
 */
int vecio_write(const char *path, const vec_graph_t *g);

/* Read a graph from `path`.  Returns NULL on error; check
 * vecio_last_error_code() for the reason and vecio_last_error_msg() for
 * a human-readable detail.  On success, the returned graph must be freed
 * with vec_graph_free().
 */
vec_graph_t *vecio_read(const char *path);

/* Most recent error from this thread.  Reset to VECIO_OK at the start of
 * each vecio_* call.
 */
vecio_err_t  vecio_last_error_code(void);
const char  *vecio_last_error_msg(void);

#ifdef __cplusplus
}
#endif

#endif  /* VECIO_H */
