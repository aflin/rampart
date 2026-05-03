/* vecindex_aux_btree.c — shared auxiliary btree helpers for INDEX_VEC.
 *
 * Both the HNSW backend (vecindex.c) and the IVFPQ backend
 * (vecindex_ivfpq.cpp) keep two auxiliary btrees alongside the main
 * index file:
 *
 *   <base>_T.btr   — newrec: recids inserted post-CREATE that haven't
 *                    yet been folded into the main index.  Walked at
 *                    SEARCH time as a linear scan with row fetches
 *                    and vecdist computation.  Mirrors texis fulltext's
 *                    `_T.btr` (see texis-internals.md §8.5).
 *   <base>_del.btr — tombstone: recids whose main-index entry is stale
 *                    (deleted) and should be filtered out at SEARCH.
 *
 * Open-and-close-per-op throughout.  Each helper opens the .btr, does
 * its work, closes.  No cached BTREE* — that pattern would let btree
 * handles outlive the connection-scoped dbf that owns them, leading
 * to use-after-close on cross-process workloads.  Cost is ~50 µs per
 * op for the open/close, dominated by the surrounding work in either
 * backend.
 *
 * Cross-process correctness is provided by texislockd serializing
 * INSERT/DELETE under the parent table's PW lock and SEARCH under
 * its PR lock; within each lock window we have exclusive use of the
 * freshly-opened BTREE*.
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "texint.h"
#include "btree.h"
#include "vecindex.h"
#include "vecindex_internal.h"

/* Build "<indfile><suffix>" for the auxiliary-btree base path the
 * btree subsystem turns into "<indfile><suffix>.btr" via openbtree's
 * auto-suffixing.  Caller frees with free(). */
char *
TXvecMakeBtreeBasePath(const char *indfile, const char *suffix)
{
    if (!indfile || !suffix) return NULL;
    size_t ilen = strlen(indfile);
    size_t slen = strlen(suffix);
    char *p = (char *)malloc(ilen + slen + 1);
    if (!p) return NULL;
    memcpy(p, indfile, ilen);
    memcpy(p + ilen, suffix, slen + 1);
    return p;
}

/* Create an empty .btr file.  Used at index-CREATE time so both the
 * tombstone and the newrec btrees exist alongside the main index.
 * Mirrors texis fulltext's pattern from `index.c:1180,1187`.
 * Returns 0 on success, -1 on error. */
int
TXvecBtreeCreateEmpty(const char *base_path)
{
    if (!base_path) return -1;
    BTREE *bt = openbtree((char *)base_path, BTFSIZE, TXbtreecache,
                          BT_UNIQUE | BT_FIXED,
                          O_RDWR | O_CREAT | O_EXCL);
    if (!bt) return -1;
    closebtree(bt);
    return 0;
}

/* Insert one int64 recid into the named btree.  Open, btinsert, close.
 * btinsert with BT_UNIQUE silently skips duplicate keys, so a successful
 * return covers both "newly inserted" and "already present".
 * Returns 0 on success, -1 on btree-open or btinsert error. */
int
TXvecBtreeInsertRecid(const char *base_path, int64_t recid_off)
{
    if (!base_path) return -1;
    BTREE *bt = openbtree((char *)base_path, BTFSIZE, TXbtreecache,
                          BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT);
    if (!bt) return -1;
    BTLOC loc;
    memset(&loc, 0, sizeof(loc));
    loc.off = (EPI_OFF_T)recid_off;
    int rc = btinsert(bt, &loc, sizeof(int64_t), &recid_off);
    closebtree(bt);
    return (rc < 0) ? -1 : 0;
}

/* Delete one int64 recid from the named btree.  Open, btdelete, close.
 * No-op (returns 0) if the .btr file is missing or the recid isn't
 * present. */
int
TXvecBtreeDeleteRecid(const char *base_path, int64_t recid_off)
{
    if (!base_path) return -1;
    /* Build full path with .btr to test existence. */
    size_t blen = strlen(base_path);
    char buf[PATH_MAX];
    if (blen + 5 >= sizeof(buf)) return -1;
    memcpy(buf, base_path, blen);
    memcpy(buf + blen, ".btr", 5);
    EPI_STAT_S st;
    if (EPI_STAT(buf, &st) != 0) return 0;
    BTREE *bt = openbtree((char *)base_path, BTFSIZE, TXbtreecache,
                          BT_UNIQUE | BT_FIXED, O_RDWR);
    if (!bt) return -1;
    BTLOC loc;
    memset(&loc, 0, sizeof(loc));
    loc.off = (EPI_OFF_T)recid_off;
    btdelete(bt, &loc, sizeof(int64_t), &recid_off);
    closebtree(bt);
    return 0;
}

/* Walk the named btree, calling `cb(recid_off, user)` for each entry.
 * Open, walk, close.  No-op if the .btr file is missing.  cb is invoked
 * once per recid in btree order. */
void
TXvecBtreeWalkRecids(const char *base_path, TXvecRecidCallback cb, void *user)
{
    if (!base_path || !cb) return;
    size_t blen = strlen(base_path);
    char buf[PATH_MAX];
    if (blen + 5 >= sizeof(buf)) return;
    memcpy(buf, base_path, blen);
    memcpy(buf + blen, ".btr", 5);
    EPI_STAT_S st;
    if (EPI_STAT(buf, &st) != 0) return;
    BTREE *bt = openbtree((char *)base_path, BTFSIZE, TXbtreecache,
                          BT_UNIQUE | BT_FIXED, O_RDONLY);
    if (!bt) return;
    rewindbtree(bt);
    BTLOC loc;
    int64_t key = 0;
    size_t  ks  = sizeof(int64_t);
    while (loc = btgetnext(bt, &ks, &key, NULL), TXrecidvalid(&loc)) {
        cb(key, user);
        ks = sizeof(int64_t);
    }
    closebtree(bt);
}

/* Truncate the named btree by unlinking the file + recreating empty.
 * Used at OPTIMIZE time after newrec entries have been folded into
 * the main index.  Returns 0 on success, -1 on error. */
int
TXvecBtreeTruncate(const char *base_path)
{
    if (!base_path) return -1;
    size_t blen = strlen(base_path);
    char buf[PATH_MAX];
    if (blen + 5 >= sizeof(buf)) return -1;
    memcpy(buf, base_path, blen);
    memcpy(buf + blen, ".btr", 5);
    unlink(buf);
    return TXvecBtreeCreateEmpty(base_path);
}

/* Unlink the .btr file for `base_path`.  Used at DROP INDEX time. */
void
TXvecBtreeUnlink(const char *base_path)
{
    if (!base_path) return;
    size_t blen = strlen(base_path);
    char buf[PATH_MAX];
    if (blen + 5 >= sizeof(buf)) return;
    memcpy(buf, base_path, blen);
    memcpy(buf + blen, ".btr", 5);
    unlink(buf);
}
