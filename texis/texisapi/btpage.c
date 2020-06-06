#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#if !defined(__bsdi__) && !defined(macintosh) && !defined(__MACH__)
#  include <malloc.h>
#endif
#include <fcntl.h>
#include "dbquery.h"
#include "texint.h"
#include "vbtree.h"

/*****************************************************************************/
/*                                                                           */
/* The page handling routines. These handle the page cache for the btree     */
/* routines.                                                                 */
/*                                                                           */
/*****************************************************************************/

/* ------------------------------------------------------------------------- */
#define BTPAGE_LAST_OPT                 /* lookup optimization KNG 980217 */
/* ------------------------------------------------------------------------- */

#ifdef BTPAGE_LAST_OPT
static int      LastGetPage = EPI_OS_INT_MAX;
#endif

static const char       Ques[] = "?";

extern char		*TxBtreeErr;	/* wtf */


int
TXbtreeIsValidPage(TXPMBUF *pmbuf, const char *fn, BTREE *btree,
                   EPI_OFF_T pageOffset, BPAGE *page, int *dirtied)
/* Returns 2 if `pg' is ok; 1 (and yaps) if correctable problem; 0
 * (and yaps) if invalid.  Sets optional `*dirtied' nonzero if page
 * was modified (caller should write it if possible).  `pageOffset' is
 * for messages; looked up if -1.
 */
{
  int           ret = 2, modified = 0, heapTop;
  EPI_SSIZE_T   val, valMin, valMax;
  byte          *byteEndOfPage = (byte *)page + btree->pagesize;
  short         orgFreesp;
  unsigned short orgStacktop;

  /* check count first; we use it for other values' limits: */
  val = (EPI_SSIZE_T)page->count;
  valMin = 0;
  valMax = (byteEndOfPage - (byte *)page->items)/sizeof(BITEM);
  if (val < valMin || val > valMax)
    {
      /* This is fatal, as we may have lost keys, and other checks
       * depend on a sane count.  But try to fix anyway if requested,
       * in case caller ignores us (but do not say so in message):
       */
      if (TXApp && (TXApp->validateBtrees & 0x1000))
        {                                       /* fix it */
          if (val < valMin) page->count = valMin;
          else if (val > valMax) page->count = valMax;
          modified = 1;
        }
      if (pageOffset == (EPI_OFF_T)(-1))
        pageOffset = btpg2off(btree, page);
      txpmbuf_putmsg(pmbuf, MERR + FRE, fn,
 "Corrupt B-tree `%s': Page 0x%wx has invalid count %d (must be %d to %d)",
                     TX_BTREE_NAME(btree), (EPI_HUGEUINT)pageOffset,
                     (int)val, (int)valMin, (int)valMax);
      goto err;
    }

  if (btree->flags & BT_FIXED)                  /* fbtree */
    {
      /* `freesp' and `stacktop' ignored for fbtree */
    }
  else                                          /* vbtree */
    {
      orgFreesp = page->freesp;
      orgStacktop = page->stacktop;
      if (TXApp && (TXApp->validateBtrees & 0x0020))  /* more stringent */
        {
          if (!TXvbtreeSetFreespaceAndStacktop(pmbuf, btree, page, pageOffset,
                                ((TXApp->validateBtrees & 0x1000) ? 1 : 0)) &&
              ret > 1)
            ret = 1;
          if (page->freesp != orgFreesp)        /* we fixed it */
            {
              if (pageOffset == (EPI_OFF_T)(-1))
                pageOffset = btpg2off(btree, page);
              txpmbuf_putmsg(pmbuf, MWARN + FRE, fn,
"Corrupt B-tree `%s': Page 0x%wx has incorrect freespace %d (should be %d)%s",
                             TX_BTREE_NAME(btree), (EPI_HUGEUINT)pageOffset,
                             (int)orgFreesp, (int)page->freesp,
			     /* We can only fix it on disk if write mode: */
			     ((TXApp->validateBtrees & 0x1000) ?
                              ((btree->openMode & (O_WRONLY | O_RDWR)) ?
                          "; will try to fix" : "; working around it") : ""));
              if (TXApp->validateBtrees & 0x1000) modified = 1;
              else page->freesp = orgFreesp;    /* undo fix */
              /* Ok not to err if not fixing; range check below: */
              if (ret > 1) ret = 1;
            }
          if (page->stacktop != orgStacktop)    /* we fixed it */
            {
              if (pageOffset == (EPI_OFF_T)(-1))
                pageOffset = btpg2off(btree, page);
              txpmbuf_putmsg(pmbuf, MWARN + FRE, fn,
 "Corrupt B-tree `%s': Page 0x%wx has incorrect stacktop %d (should be %d)%s",
                             TX_BTREE_NAME(btree), (EPI_HUGEUINT)pageOffset,
                             (int)orgStacktop, (int)page->stacktop,
			     /* We can only fix it on disk if write mode: */
			     ((TXApp->validateBtrees & 0x1000) ?
                              ((btree->openMode & (O_WRONLY | O_RDWR)) ?
                          "; will try to fix" : "; working around it") : ""));
              if (TXApp->validateBtrees & 0x1000) modified = 1;
              else page->stacktop = orgStacktop;        /* undo fix */
              /* Ok not to err if not fixing; range check below: */
              if (ret > 1) ret = 1;
            }
        }

      heapTop = (int)((byte *)&page->items[page->count] - (byte *)page);

      /* Range check stacktop: */
      val = (EPI_SSIZE_T)page->stacktop;
      valMin = heapTop;
      valMax = (EPI_SSIZE_T)btree->pagesize;
      if (val < valMin || val > valMax)         /* bad `stacktop' */
        {
          if (TXApp && (TXApp->validateBtrees & 0x1000))
            {                                   /* fix it */
              if (val < valMin) page->stacktop = valMin;
              else if (val > valMax) page->stacktop = valMax;
              modified = 1;
            }
          if (pageOffset == (EPI_OFF_T)(-1))
            pageOffset = btpg2off(btree, page);
          txpmbuf_putmsg(pmbuf, MWARN + FRE, fn,
                         "Corrupt B-tree `%s': Page 0x%wx has incorrect stacktop %d (must be %d to %d)%s",
                         TX_BTREE_NAME(btree), (EPI_HUGEUINT)pageOffset,
                         (int)val, (int)valMin, (int)valMax,
                         /* We can only fix it on disk if write mode: */
                         ((TXApp->validateBtrees & 0x1000) ?
                          ((btree->openMode & (O_WRONLY | O_RDWR)) ?
                          "; will try to fix" : "; working around it") : ""));
          if (ret > 1) ret = 1;                 /* recoverable */
        }

      /* Range check freespace, after stacktop; former is more flexible: */
      val = (EPI_SSIZE_T)page->freesp;
      /* wtf min `freesp' can be 1 BITEM off: due to sizeof(BPAGE) usage
       * which has 1 BITEM?  additem() seen to cause freesp to go negative;
       * investigate; Vortex test960 can cause freesp < -sizeof(BITEM):
       */
      valMin = -(EPI_SSIZE_T)sizeof(page->items);
      /* could be slack space in keys, so we cannot use `page->stacktop': */
      valMax = btree->pagesize - heapTop;
      /* `valMax' is off by sizeof(BITEM) for same reason as `valMin': */
      valMax += valMin;
      if (val < valMin || val > valMax)
        {
          if (TXApp && (TXApp->validateBtrees & 0x1000))
            {                                   /* fix it */
              if (val < valMin) page->freesp = valMin;
              else if (val > valMax) page->freesp = valMax;
              modified = 1;
            }
          if (pageOffset == (EPI_OFF_T)(-1))
            pageOffset = btpg2off(btree, page);
          txpmbuf_putmsg(pmbuf, MWARN + FRE, fn,
                         "Corrupt B-tree `%s': Page 0x%wx has incorrect freespace %d (must be %d to %d)%s",
                         TX_BTREE_NAME(btree), (EPI_HUGEUINT)pageOffset,
                         (int)val, (int)valMin, (int)valMax,
                         /* We can only fix it on disk if write mode: */
                         ((TXApp->validateBtrees & 0x1000) ?
                          ((btree->openMode & (O_WRONLY | O_RDWR)) ?
                          "; will try to fix" : "; working around it") : ""));
          if (ret > 1) ret = 1;                 /* recoverable */
        }
    }

  goto finally;

err:
  ret = 0;
finally:
  if (dirtied) *dirtied = modified;
  return(ret);
}

BPAGE *
btgetpage(t, n)
BTREE *t;
EPI_OFF_T n;
/* Note: Returned page will have `t->prebufsz' bytes allocated before it,
 * and `t->postbufsz' after, for KDBF optimizations.
 */
{
	static CONST char Fn[] = "btgetpage";
	int i, c = -1, u = -1, modified = 0;
	int	cl, ul;
	int l0 = 0, allocfailed = 0;
	BCACHE *bc;

	if (n == (EPI_OFF_T)0)				/* WTF error? KNG */
		return (BPAGEPN);

	t->pagereads++;
	cl = t->pagereads;
	ul = t->pagereads;

#ifdef BTPAGE_LAST_OPT
	/* Optimization: look at the last page we used and see if a match: */
        if (LastGetPage < t->cacheused &&               /* KNG 980217 */
            (bc = t->cache + LastGetPage)->pid == n)
          goto gotit;
#endif

	/* Look through the page cache for a match: */
#ifndef NEVER /* This is broken - WTF */
	for (i = 0, bc = &t->cache[0]; i < t->cacheused; i++, bc++)
#else
	for (i = 0, bc = &t->cache[0]; i < t->cachesize; i++, bc++)
#endif
	{
		if (bc->pid == n)
		{
#ifdef BTPAGE_LAST_OPT
                  LastGetPage = i;
#endif
                gotit:
#ifdef DEBUG
			if (bc->inuse > 0)
				DBGMSG(9, (999, NULL, "Regetting %ld\n", n));
#endif
			bc->inuse ++;
			bc->lastaccess = t->pagereads;
#ifdef DEBUG
			DBGMSG(8, (999, Fn, "Found %ld in cache", n));
#endif
#ifdef MEMDEBUG
                        /* Some of us use B-trees with > 8k page size. -KNG */
			if(bc->page->count > 1000 &&
                           t->pagesize <= BT_MAXPGSZ)
                          {
                            putmsg(MERR + MAE, Fn, "Page count large for B-tree %s", (t->dbf ? getdbffn(t->dbf) : "?"));
                            abort();            /* abort if MEMDEBUG */
                          }
#endif  /* MEMDEBUG */
			return bc->page;
		}
#ifdef NEVER
		if (l0 == i)
		{
			if (bc->pid == 0)
				l0 = i;
			else
				l0 = i+1;
		}
#endif
	}
#ifdef DEBUG
	DBGMSG(8, (999, Fn, "%ld not in cache", n));
#endif

	/* Not found in cache; must load a new page.
	 * Look for an unused cache item to use:
	 */
	for (i = l0, bc = &t->cache[i]; i < t->cachesize; i++, bc++)
	{
		if (bc->pid == 0)	/* Empty node */
		{
			bc->inuse = 1;
			bc->pid = n;
			if (bc->page == (BPAGE *)NULL)
			  {
			    if (allocfailed) goto clrit;
			    bc->page = btmkpage(t);
			    if (bc->page == BPAGEPN)
			      {
				allocfailed = 1;
			      clrit:
				bc->inuse = 0;
				bc->pid = 0;
				continue;
			      }
			  }
			if (btreadpage(t, n, bc->page, &modified) < 0)
				return NULL;
			bc->dirty = 0;
                        /* Bug 6141: save page if fixed, if possible: */
			if (modified && (t->openMode & (O_WRONLY | O_RDWR)))
				bc->dirty = 1;
			if(t->cacheused < (i+1))
			t->cacheused = i + 1;
#ifdef MEMDEBUG
                        /* Some of us use B-trees with > 8k page size. -KNG */
			if(bc->page->count > 1000 &&
                           t->pagesize <= BT_MAXPGSZ)
                          {
                            putmsg(MERR + MAE, Fn, "Page count large for B-tree %s", (t->dbf ? getdbffn(t->dbf) : "?"));
                            abort();            /* abort if MEMDEBUG */
                          }
#endif  /* MEMDEBUG */
#ifdef BTPAGE_LAST_OPT
                        LastGetPage = i;
#endif
			return bc->page;
		}
		/* Keep track of the oldest clean-and-unused cache slot
		 * (`c'/`cl') and oldest unused slot (`u'/`ul'):
		 */
		if (bc->inuse == 0)	/* Unused node */
		{
			if (bc->dirty == 0)	/* page is clean */
			{
				if(bc->lastaccess < cl)
				{
					c = i;
					cl = bc->lastaccess;
				}
			}
			if(bc->lastaccess < ul)
			{
				u = i;
				ul = bc->lastaccess;
			}
		}
	}
	if (c != -1)				/* have a clean unused slot */
		i = c;
	else
	{
		if (u != -1) i = u;		/* have an unused slot */
		else				/* out of cache space */
		{
			putmsg(MERR + UGE, Fn, "Internal error: Out of cache space trying to obtain page 0x%wx of B-tree %s",
				(EPI_HUGEINT)n, getdbffn(t->dbf));
			return(BPAGEPN);
		}
		if (btwritepage(t, t->cache[i].pid, t->cache[i].page) == (EPI_OFF_T)(-1))
			return(BPAGEPN);
	}
	t->cache[i].inuse = 1;
	t->cache[i].pid = n;
	if (t->cache[i].page == (BPAGE *)NULL)
	{
		if (allocfailed) return(BPAGEPN);
		t->cache[i].page = btmkpage(t);
		if (t->cache[i].page == BPAGEPN) return(BPAGEPN);
	}
	if (btreadpage(t, n, t->cache[i].page, &modified) < 0)
		return (BPAGE *)NULL;
	t->cache[i].dirty = 0;
        /* Bug 6141: save page if fixed, if possible: */
	if (modified && (t->openMode & (O_WRONLY | O_RDWR)))
		t->cache[i].dirty = 1;
#ifdef MEMDEBUG
        if(t->cache[i].page->count > 1000 &&
           t->pagesize <= BT_MAXPGSZ)           /* could be huge -KNG 980307 */
          {
            putmsg(MERR + MAE, Fn, "Item count large for B-tree %s", (t->dbf ? getdbffn(t->dbf) : "?"));
            abort();                            /* abort if MEMDEBUG */
          }
#endif  /* MEMDEBUG */
#ifdef BTPAGE_LAST_OPT
        LastGetPage = i;
#endif
	return t->cache[i].page;
}

/*****************************************************************************/

void
btdirtypage(t, n)
BTREE *t;
EPI_OFF_T n;
{
	static CONST char Fn[] = "btdirtypage";
	int i;
	BCACHE *bc;

	for (i = 0, bc = &t->cache[0]; i < t->cachesize; i++, bc++)
		if (bc->pid == n)
		{
#ifdef MEMDEBUG
                  if(bc->page->count > 1000 &&
                     t->pagesize <= BT_MAXPGSZ)   /* could be huge -KNG */
                    {
                      putmsg(MERR + MAE, Fn, "Item count large for B-tree %s", (t->dbf ? getdbffn(t->dbf) : "?"));
                      abort();                  /* abort if MEMDEBUG */
                    }
#endif  /* MEMDEBUG */
			bc->dirty = 1;
			return;
		}
	putmsg(MERR, Fn,"Cannot dirty page 0x%wx of B-tree `%s': Not in cache",
	       (EPI_HUGEUINT)n, getdbffn(t->dbf));
}

/*****************************************************************************/

void
btinitpage(t, p)
BTREE   *t;
BPAGE   *p;
/* just "clears" a page  KNG 971016
 */
{
  p->count = 0;
  p->lpage = (EPI_OFF_T)0;
  p->freesp = t->order - (short)sizeof(BPAGE);/* `freesp' ignored in fbtree */
  p->stacktop = t->pagesize;
}

int
btreadpage(t, off, p, dirtied)
BTREE   	*t;
EPI_OFF_T	off;
BPAGE   	*p;
int             *dirtied;
/* Reads page `p' from offset `off'.
 * *All* page reads should go through this function, for logging.
 * KNG 20070419 and because of KDBF ioctls.  Assumes `p' has `t->prebufsz'
 * and `t->postbufsz' contiguously allocated with it.  Sets `*dirtied'
 * nonzero if page modified (i.e. repaired); caller should write if possible.
 * Returns 0 on success, -1 on error.
 */
{
	static CONST char	fn[] = "btreadpage";
	size_t			nread;
	int			ret = 0, modified = 0;
	BTLOC			loc;

	/* DBF ioctls set by BT_EXCLUSIVEACCESS are incompatible with
	 * readdbf(); temporarily clear them so readdbf() does not fail.
	 * We should normally only have BT_EXCLUSIVEACCESS set on trees
	 * that are linear-write, so this rarely if ever happens:
	 */
	if (t->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(t, 0);
	nread = readdbf(t->dbf, off, (byte *)p - t->prebufsz, t->pagesize);
	if (t->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(t, 1);

	if (nread != (size_t)t->pagesize)
	{
		putmsg(MERR + FRE, fn,
 "Could not read %kwd-byte page at offset 0x%wx of B-tree %s: got %kwd bytes",
                       (EPI_HUGEINT)t->pagesize, (EPI_HUGEUINT)off,
                       getdbffn(t->dbf), (EPI_HUGEINT)nread);
		TxBtreeErr = "Could not read page";
#if defined(TX_DEBUG) && defined(_WIN32)
		DebugBreak();
#endif
		ret = -1;
	}

	/* Quick sanity check of page; e.g. a bad count could ABEND: */
	if (TXApp && (TXApp->validateBtrees & 0x02) &&
	    !TXbtreeIsValidPage(TXPMBUFPN, fn, t, off, p, &modified))
	{
		TxBtreeErr = "Read invalid page";
		ret = -1;
	}

	if (t->flags & BT_LOGOPS)
	{
		TXsetrecid(&loc, off);
		btlogop(t, 0, NULL, &loc, "RDpage",
                        (ret < 0 ? "fail" :
                         (modified ? "ok-modified" : "ok")));
	}

        if (dirtied) *dirtied = modified;
	return(ret);
}

EPI_OFF_T
btwritepage(t, off, p)
BTREE   	*t;
EPI_OFF_T	off;
BPAGE   	*p;
/* Writes page `p' to offset `off' (new offset if -1).
 * *All* page writes should go through this function, for logging.
 * KNG 20070419 and because of KDBF ioctls.  Assumes `p' has `t->prebufsz'
 * and `t->postbufsz' contiguously allocated with it.
 * Returns offset of page, or -1 on error.
 */
{
	static CONST char	fn[] = "btwritepage";
	EPI_OFF_T		res;
	BTLOC			loc;
        int                     modified = 0;

	/* Quick sanity check of page; e.g. a bad count could ABEND: */
	if (TXApp && (TXApp->validateBtrees & 0x04) &&
	    !TXbtreeIsValidPage(TXPMBUFPN, fn, t, off, p, &modified))
	{
		/* WTF allow the write to proceed for now; otherwise
		 * page is still dirty and more write attempts follow:
		 */
		/* TxBtreeErr = "Attempt to write invalid page"; */
		/* res = (EPI_OFF_T)(-1); */
		/* goto finally; */
	}

	/* DBF ioctls set by BT_EXCLUSIVEACCESS are incompatible with
	 * putdbf() to a specific offset; temporarily clear them:
	 */
	if (off != (EPI_OFF_T)(-1) && (t->flags & BT_EXCLUSIVEACCESS))
		TXbtsetexclusiveioctls(t, 0);
	res = putdbf(t->dbf, off, (byte *)p - t->prebufsz, t->pagesize);
	if (off != (EPI_OFF_T)(-1) && (t->flags & BT_EXCLUSIVEACCESS))
		TXbtsetexclusiveioctls(t, 1);

	if (res == (EPI_OFF_T)(-1))		/* write failed */
	{
		if (off == (EPI_OFF_T)(-1))	/* new page */
			putmsg(MERR + FWE, fn,
		"Could not write %kwd-byte new page to B-tree %s",
				(EPI_HUGEINT)t->pagesize, getdbffn(t->dbf));
		else				/* existing page */
			putmsg(MERR + FWE, fn,
		"Could not write %kwd-byte page at offset 0x%wx of B-tree %s",
				(EPI_HUGEINT)t->pagesize, (EPI_HUGEUINT)off,
				getdbffn(t->dbf));
		TxBtreeErr = "Could not write page";
	}

/* finally: */
	if (t->flags & BT_LOGOPS)
	{
		TXsetrecid(&loc, (off == (EPI_OFF_T)(-1) ? res : off));
		btlogop(t, 0, NULL, &loc,
			(off == (EPI_OFF_T)(-1) ? "CRpage" : "WRpage"),
			(res < (EPI_OFF_T)0 ? "fail" :
                         (modified ? "ok-modified" : "ok")));
	}
	return(res);
}

BPAGE *
btmkpage(t)
BTREE   *t;
/* Creates new page, without writing it or adding to cache.   KNG 971016
 * Will have `t->prebufsz' bytes alloced before returned pointer,
 * and `t->postbufsz' after, for KDBF ioctls.
 * All page allocs should go through here for consistent pre/postbufsz.
 * Page should be freed with btfrpage() (if not put in cache).
 */
{
  static CONST char   fn[] = "btmkpage";
  BPAGE         *p;
  byte		*buf;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  buf = (byte *)TXcalloc(pmbuf, fn, 1,
                         t->prebufsz + t->pagesize + t->postbufsz);
  if (!buf) return(NULL);
  /* Note that `t->prebufsz' must be a multiple of alignment size: */
  p = (BPAGE *)(buf + t->prebufsz);
  btinitpage(t, p);
  return(p);
}

BPAGE *
btfrpage(t, p)
BTREE	*t;
BPAGE	*p;
/* Frees orphaned (not in cache) page `p'.
 * All page frees should eventually go through here, since this deals with
 * pre/postbufsz offsets of `p'.  Note usage in kfbtree.c too.
 * Returns NULL.
 */
{
	if (!p) return(NULL);
	/* Page was alloced with `t->prebufsz' before it, so
	 * back off to original malloc() pointer for free():
	 */
	if (TXApp && (TXApp->validateBtrees & 0x2000))
		memset((byte *)p, 0xfe, t->pagesize);
	TXfree((byte *)p - t->prebufsz);
	return(BPAGEPN);
}

/*****************************************************************************/

EPI_OFF_T
btgetnewpage(t)
BTREE *t;
/* Allocates and writes new empty page to tree `t'.  Returns offset of
 * new page, or -1 on error.
 */
{
	BPAGE *p;
	int i;
	static CONST char Fn[] = "btgetnewpage";

	for (i=0; i < t->cachesize && t->cache[i].pid > (EPI_OFF_T)0; i++);
	if (i == t->cachesize || t->cache[i].pid != (EPI_OFF_T)0)
	{
		for (i=t->cachesize-1; i>=0; i--)
		{
			if (t->cache[i].inuse == 0)
			{
				if(t->cache[i].page)
				{
					if (t->cache[i].dirty)
					{
						if (btwritepage(t,
						       t->cache[i].pid,
						       t->cache[i].page) ==
						       (EPI_OFF_T)(-1))
							return((EPI_OFF_T)(-1));
					}
					memset(t->cache[i].page,0,t->pagesize);
				}
				t->cache[i].pid = (EPI_OFF_T)0;
				t->cache[i].dirty = 0;
				break;
			}
		}
		if (i==-1)
		{
			putmsg(MERR,Fn,"No free slots in the cache");
			return((EPI_OFF_T)(-1));
		}
	}
	p = t->cache[i].page;
	if(p)
		memset(t->cache[i].page, 0, t->pagesize);
	else
	{
		p = btmkpage(t);
		if (!p) return((EPI_OFF_T)(-1));
	}
        btinitpage(t, p);
	t->cache[i].pid = btwritepage(t, (EPI_OFF_T)(-1), p);
	t->cache[i].inuse = 0;
	t->cache[i].dirty = 0;
	t->cache[i].page = p;
	if(t->cacheused < (i + 1))
		t->cacheused = i + 1;
	if(t->cache[i].pid == (EPI_OFF_T)(-1))
	{
		t->cache[i].pid = (EPI_OFF_T)0;
		return((EPI_OFF_T)(-1));
	}
	else
		return t->cache[i].pid;
}

/*****************************************************************************/

BPAGE *
btreleasepage(t, n, b)
BTREE *t;
EPI_OFF_T n;
BPAGE *b;
/* Releases page `b' (at offset `n') from use: cache is free to re-use
 * it for another page (after writing to disk, if dirty).  Caller must
 * no longer use `b' without call btgetpage() again.
 * Returns NULL.
 */
{
	static const char	fn[] = "btreleasepage";
	int i, modified = 0;
        BCACHE  *bc;

	if (n == 0 || b == BPAGEPN)     /* KNG 990316 check NULL */
		return(NULL);

#ifdef BTPAGE_LAST_OPT
        /* Optimization: it's very likely we're releasing the previous
         * btgetpage() page, so check it first:   -KNG 980217
         */
        if (LastGetPage < t->cachesize &&
            (bc = t->cache + LastGetPage)->pid == n)
          goto gotit;
#endif

	for (i = 0, bc = &t->cache[0]; i < t->cachesize; i++, bc++)
	{
		if (bc->pid == n)
		{
                gotit:
#ifdef MEMDEBUG
                  if(b->count > 1000 &&
                     t->pagesize <= BT_MAXPGSZ)   /* could be huge -KNG */
                    {
                      putmsg(MERR + MAE, fn, "Item count large for B-tree %s", (t->dbf ? getdbffn(t->dbf) : "?"));
                      abort();                  /* abort if MEMDEBUG */
                    }
#endif  /* MEMDEBUG */

/*
 *			if (bc->dirty)
 *				putdbf(t->dbf, n, b, t->pagesize);wtf prebufsz/postbufsz
 *			bc->dirty = 0;
 */
			if (TXApp && (TXApp->validateBtrees & 0x08) &&
			    !TXbtreeIsValidPage(TXPMBUFPN, fn, t, n, b,
                                                &modified))
			{
				/* wtf do something? */
			}
			if (modified &&		/* we modified page */
                            /* Bug 6141: save page if fixed, if possible: */
			    /* We can only write the page if we opened the
			     * B-tree with write flag:
			     */
			    (t->openMode & (O_WRONLY | O_RDWR)))
				bc->dirty = 1;

			if (--bc->inuse < 0)
			{
				if (TXApp && (TXApp->validateBtrees & 0x10))
					putmsg(MWARN, fn,
			 "Page 0x%wx of B-tree `%s' released when not in use",
					       (EPI_HUGEINT)n,
                                               TX_BTREE_NAME(t));
				bc->inuse = 0;
			}
			return(NULL);
		}
	}

	/* Page not in cache: `b' might be freed mem by now: */
	if (TXApp && (TXApp->validateBtrees & 0x10))
	{
		putmsg(MERR + MAE, fn,
		       "Page 0x%wx of B-tree `%s' not in cache",
		       (EPI_HUGEINT)n, TX_BTREE_NAME(t));
	}
	return(NULL);
}

/*****************************************************************************/

BPAGE *
btfreepage(t, n, b)
BTREE *t;
EPI_OFF_T n;
BPAGE *b;
/* Releases and removes `b' from cache, deletes it in DBF, and frees it.
 * Returns NULL.
 */ 
{
	int i, res;
	BTLOC	loc;

	if (n == 0 || !b)
          return(NULL);
	b = btreleasepage(t, n, b);
	for (i = 0; i < t->cachesize; i++)
		if (t->cache[i].pid == n)
		{
			t->cache[i].page = btfrpage(t, t->cache[i].page);

			/* DBF ioctls set by BT_EXCLUSIVEACCESS are
			 * incompatible with freedbf(); temp clear them:
			 */
			if (t->flags & BT_EXCLUSIVEACCESS)
				TXbtsetexclusiveioctls(t, 0);
			res = freedbf(t->dbf, t->cache[i].pid);
			if (t->flags & BT_EXCLUSIVEACCESS)
				TXbtsetexclusiveioctls(t, 1);

			if (t->flags & BT_LOGOPS)
			{
				TXsetrecid(&loc, t->cache[i].pid);
				btlogop(t, 0, NULL, &loc, "FRpage",
					(res ? "ok" : "fail"));
			}
			t->cache[i].page = (BPAGE *)NULL;
			t->cache[i].pid = 0;
			t->cache[i].inuse = 0;
			t->cache[i].dirty = 0;
			return(NULL);
		}
        return(NULL);
}

/*****************************************************************************/

int
btflush(b)
BTREE *b;
/* Flushes unwritten data in `b' to disk.
 * Returns 0 on success, -1 on error.
 */
{
	int	i, ret = 0;

	if (b == (BTREE *)NULL)
		return(0);

        if ((b->flags & BT_LINEAR) && btflushappend(b) < 0) ret = -1;
	if (b->cache != (BCACHE *)NULL)
		for (i=0; i < b->cachesize; i++)
		{
			if (b->cache[i].dirty)
			{
				if (btwritepage(b, b->cache[i].pid,
					b->cache[i].page) < 0)
					ret = -1;
				else
					b->cache[i].dirty = 0;
			}
		}
	if (btsetroot(b) < 0) ret = -1;
#ifdef NEVER /* _WIN32 */
	if(getdbffh(b->dbf))
		_commit(getdbffh(b->dbf));
#endif
#ifdef NEVER
	else
		putmsg(999, NULL, "It would appear the root page is still %d", b->root);
#endif
	return(ret);
}

/*****************************************************************************/
/******************************************************************/

