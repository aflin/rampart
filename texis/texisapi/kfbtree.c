/* Contains replacements for some fbtree.c functions, for kdbf internal use.
 */

/* Magic needed before dbquery.h:  -KNG 951115 */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "kfbtree.h"
#include "kdbf.h"
#include "kdbfi.h"
#include "texint.h"                             /* for DBF_KAI */


BTREE *
kdbf_closefbtree(b)
BTREE *b;
{
  static const char     fn[] = "kdbf_closefbtree";
  int	                i, sp;
  KDBF	                *df;

  if (b == (BTREE *)NULL) return(NULL);

  df = (KDBF *)b->dbf->obj;
  if (!(b->cachesize == KDBF_BT_CACHE_SZ && b->cache))
    txpmbuf_putmsg(df->pmbuf, MERR, fn,
"Internal error: Wrong cache size or missing B-tree cache for KDBF file `%s'",
                   df->fn);

  /* Flush the pages, and save them for next time (save some mallocs).
   * We also save pre/postbufsz that pages were allocated with,
   * for proper eventual btfrpage() calls:
   */
  df->btree_cache_prebufsz = df->btree_prebufsz;
  df->btree_cache_postbufsz = df->btree_postbufsz;
  sp = 0;
  for (i = 0; i < b->cachesize; i++) {
    df->btree_cache_pages[i] = NULL;
    if (b->cache[i].dirty)
      btwritepage(b, b->cache[i].pid, b->cache[i].page);
    if (b->cache[i].page != (BPAGE *)NULL) {
      /* Save buffer for next time, to avoid re-mallocing: */
      df->btree_cache_pages[sp++] = b->cache[i].page;
    }
  }
  /* don't free cache; saved by KDBF */
  /* don't free b->his */
  if (b->iamdirty) {
    /* Only variable is the root page pointer: */
    kdbf_put_freetree_root(df, b->root);
  }

  kdbf_pseudo_closedbf(b->dbf);
  /* don't free b */
  return(NULL);
}

#ifdef KDBF_FIX_BTCMP
static int cmp_func ARGS((void *, size_t, void *, size_t, void *));
static int
cmp_func(ap, alen, bp, blen, usr)
void	*ap, *bp, *usr;
size_t	alen, blen;
/* Comparision function for searches in the block B-tree, that doesn't
 * assume sizeof(int) == sizeof(long).
 */
{
  EPI_OFF_T	cmp;

  cmp = *((EPI_OFF_T *)ap) - *((EPI_OFF_T *)bp);
  if (cmp < (EPI_OFF_T)0L)
    return(-1);
  else if (cmp > (EPI_OFF_T)0L)
    return(1);
  else
    return(0);
}
#endif	/* KDBF_FIX_BTCMP */

#ifdef KDBF_FIX_RECIDCMP
int
_recidcmp(r1, r2)
PRECID	r1, r2;
{
  EPI_OFF_T	cmp;

  cmp = (r1->off - r2->off);
  if (cmp < (EPI_OFF_T)0L)
    return(-1);
  else if (cmp > (EPI_OFF_T)0L)
    return(1);
  else
    return(0);
}
#endif	/* KDBF_FIX_RECIDCMP */

BTREE *
kdbf_openfbtree(kdf, flags, root)
KDBF	*kdf;
int	flags;
EPI_OFF_T   root;
{
  static CONST char     fn[] = "kdbf_openfbtree";
  BTREE	*b;
  DBF	*dbf;
  int	i;

  if (root < (EPI_OFF_T)0L) return((BTREE *)NULL);

  dbf = kdbf_pseudo_opendbf(kdf);	/* must be done first */
  if (dbf == (DBF *)NULL) return(NULL);
  b = (BTREE *)kdbf_pseudo_calloc(kdf, &kdf->btree_btree, sizeof(BTREE));
  if (b == NULL) goto err;

  /* Initialize structure; most fields are constant per kdbf: */
  b->magic = KDBF_BTREE_MAGIC;
  b->npages = 0;		/* never used? */
  b->root = root;
  b->hdroff = BTREE_DEFAULT_HDROFF;     /* not used by KDBF */
  b->order = KDBF_BTORDER;	/* must agree with pagesize */
  /* Page size is constant, since order is constant.  Note that
   * if this changes, kdbfchk needs to change as well since it
   * assumes a page size and fields on this formula:
   */
  b->pagesize = KDBF_BT_PGSZ;
  if (flags & BT_EXCLUSIVEACCESS)
    {
      /* wtf investigate ramifications of allowing this; might need
       * kdbf_ioctl() mods?  is it possible?
       */
      putmsg(MERR + UGE, fn, "Internal error: BT_EXCLUSIVEACCESS flag failed for KDBF file %s: Not supported within internal B-tree",
             kdf->fn);
      goto err;
    }
  b->flags = (char)flags;
  b->iamdirty = 0;
  (void)ddsettype(b->datad, 0);
  b->szread = 0;
  b->dbf = dbf;
  /* Cache size must be at least big enough to hold 1 page from
   * each level of the tree:
   */
  b->cachesize = KDBF_BT_CACHE_SZ;

  /* Note that kdbf_pseudo_calloc() must have same size allocs per pointer: */
  if ((b->cache = (BCACHE *)kdbf_pseudo_calloc(kdf, &kdf->btree_cache,
		KDBF_BT_CACHE_SZ*sizeof(BCACHE))) == (BCACHE *)NULL ||
      (b->his = (BTRL *)kdbf_pseudo_calloc(kdf, &kdf->btree_his,
		KDBF_BT_CACHE_SZ*sizeof(BTRL))) == (BTRL *)NULL) {
    /* don't free b->cache */
    return(kdbf_closefbtree(b));
  }

  /* KNG 20070419 Set KDBF pre/postbufsz for efficiency.  B-tree page
   * alloc routines are aware of and use `b->prebufsz'/`b->postbufsz'.
   * Since `kdf->in_btree' is now > 0 (via kdbf_pseudo_opendbf()),
   * these will apply only to B-tree ops, not high-level KDBF user.
   * Delay propagating bufsz to B-tree until we've fixed the cache.
   *   Align to larger of TX_ALIGN_BYTES and EPI_OFF_T, since former
   * may not be enough for safe EPI_OFF_T access on some platforms.
   * This must be enough to align BTREES/BPAGE.  See also open[fv]btree():
   */
  /* TX_ALIGN_BYTES may not be enough alignment, but we cannot change
   * it since that affects .tbl back-compatibility.  Bump it up:
   */
#define ASZ     \
  (TX_ALIGN_BYTES > EPI_OFF_T_BITS/8 ? TX_ALIGN_BYTES : EPI_OFF_T_BITS/8)
#define ALIGN_UP(n)     ((((n) + ASZ - 1) / ASZ) * ASZ)
  if (kdbf_getoptimize() & 0x2)
    {
      kdbf_ioctl(kdf, (DBF_KAI | KDBF_IOCTL_PREBUFSZ),
                 (void *)ALIGN_UP(KDBF_PREBUFSZ_WANT));
      /* Note that we boost postbufsz by sizeof(KDBF_CHKSUM) because
       * internal B-tree pages are checksummed.  This is not included
       * in KDBF_POSTBUFSZ_WANT because that is a user-level value:
       */
      kdbf_ioctl(kdf, (DBF_KAI | KDBF_IOCTL_POSTBUFSZ),
                 (void*)ALIGN_UP(KDBF_POSTBUFSZ_WANT + sizeof(KDBF_CHKSUM)));
    }

  /* Pre-alloc cache pages from saved buffers (if any) and fix cache.
   * Note that pre/postbufsz MUST agree with when they were allocated,
   * or a corrupt free() will happen in btfrpage().  So if they differ,
   * free pages with old values:
   */
  if (kdf->btree_prebufsz != (size_t)kdf->btree_cache_prebufsz ||
      kdf->btree_postbufsz != (size_t)kdf->btree_cache_postbufsz)
    {                                           /* invalid; free cache */
      b->prebufsz = kdf->btree_cache_prebufsz;
      b->postbufsz = kdf->btree_cache_postbufsz;
      for (i = 0; i < KDBF_BT_CACHE_SZ; i++)
        kdf->btree_cache_pages[i] = btfrpage(b, kdf->btree_cache_pages[i]);
      kdf->btree_cache_prebufsz = kdf->btree_cache_postbufsz = 0;
    }
  else                                          /* valid; re-use */
    {
      for (i = 0; i < KDBF_BT_CACHE_SZ; i++)
        b->cache[i].page = kdf->btree_cache_pages[i];
    }
  /* Tell B-tree to use current values: */
  b->prebufsz = kdf->btree_prebufsz;
  b->postbufsz = kdf->btree_postbufsz;

  b->sdepth = 0;
  b->cdepth = 0;
  b->cacheused = 0;
#ifdef KDBF_FIX_BTCMP
  btsetcmp(b, cmp_func);
#else
  btsetdefcmp(b);
#endif

  /* rest of `b' cleared by kdbf_pseudo_calloc() */

  return(b);

 err:
  kdbf_pseudo_closedbf(dbf);
  return(NULL);
#undef ASZ
#undef ALIGN_UP
}

void
kdbf_freebtreefields(df)
KDBF    *df;
{
  int   i, didinit = 0;
  BTREE t;
#define FREE(df, p)	((df)->frees++, free(p))
#define MEMSET(df, s, c, n)   \
  ((df)->memsets++, (df)->memsetbytes += (EPI_HUGEUINT)(n), memset((s), (c), (n)))

  if (df->btree_btree) FREE(df, df->btree_btree);
  if (df->btree_cache) FREE(df, df->btree_cache);
  if (df->btree_his) FREE(df, df->btree_his);

  for (i = 0; i < KDBF_BT_CACHE_SZ; i++)
    if (df->btree_cache_pages[i] != BPAGEPN)
      {
        if (!didinit)
          {
            /* Cached pages must be freed with the same pre/postbufsz
             * as they were created with, so fake up a BTREE with the
             * proper setting:
             */
            MEMSET(df, &t, 0, sizeof(BTREE));
            t.prebufsz = df->btree_cache_prebufsz;
            t.postbufsz = df->btree_cache_postbufsz;
            didinit = 1;
          }
         df->btree_cache_pages[i] = btfrpage(&t, df->btree_cache_pages[i]);
      }

#undef FREE
#undef MEMSET
}

void
kdbf_traverse_tree(bt, root, depth, cb, data)
BTREE		*bt;
EPI_OFF_T		root;
int		depth;
kdbf_item_cb	*cb;
void		*data;
/* Calls cb(key, loc, data) for each item in tree, in order.
 */
{
  BPAGE	*p;
  int	i;

  if (root == (EPI_OFF_T)0L) return;	/* null page */

  if ((p = btgetpage(bt, root)) == BPAGEPN)
    {
      btcantgetpage("kdbf_traverse_tree", bt, root, (EPI_OFF_T)(-1), -1);
      return;
    }
  kdbf_traverse_tree(bt, p->lpage, depth + 1, cb, data);
  for (i = 0; i < p->count; i++) {
    cb(p->items[i].vf.key, p->items[i].locn, data);
    kdbf_traverse_tree(bt, p->items[i].hpage, depth + 1, cb, data);
  }
  p = btreleasepage(bt, root, p);
}

#if defined(KDBF_PEDANTIC) || defined(KDBF_TEST)
void
kdbf_preorder_tree(bt, root, depth, icb, idata, pcb, pdata)
BTREE		*bt;
EPI_OFF_T		root;
int		depth;
kdbf_item_cb	*icb;
kdbf_page_cb	*pcb;
void		*idata, *pdata;
/* Calls icb(key, loc, idata) for each item in tree (if non-NULL),
 * and pcb(page, pdata) for each page (if non-NULL), in preorder.
 */
{
  BPAGE	*p;
  int	i;

  if (root == (EPI_OFF_T)0L) return;	/* null page */

  if ((p = btgetpage(bt, root)) == BPAGEPN)
    {
      btcantgetpage("kdbf_preorder_tree", bt, root, (EPI_OFF_T)(-1), -1);
      return;
    }
  if (pcb) pcb((EPI_OFF_T)root, p, pdata);
  for (i = 0; i < p->count; i++) {
    if (icb) icb(p->items[i].vf.key, p->items[i].locn, idata);
  }
  kdbf_preorder_tree(bt, p->lpage, depth + 1, icb, idata, pcb, pdata);
  for (i = 0; i < p->count; i++) {
    kdbf_preorder_tree(bt, p->items[i].hpage, depth + 1, icb, idata,
		       pcb, pdata);
  }
  p = btreleasepage(bt, root, p);
}
#endif	/* KDBF_PEDANTIC || KDBF_TEST */
