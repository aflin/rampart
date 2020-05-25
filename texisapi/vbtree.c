/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
/*#include <malloc.h>*/
#include "dbquery.h"
#include "texint.h"
#include "vbtree.h"
#include "cgi.h"				/* for htpf() */

#if defined(MEMDEBUG) || defined(TX_DEBUG)
#  define EXTRA_PAGE_CHECK(bt, p)                                       \
  { if (TXApp && (TXApp->validateBtrees & 0x0040))                      \
      {                                                                 \
  if (TXbtreeIsValidPage(TXPMBUFPN, __FUNCTION__, (bt), -1, (p), NULL) != 2) \
    putmsg(MINFO,__FUNCTION__,"Previous B-tree corruption detected at %s:%d",\
           __FILE__, __LINE__);                                         \
      }                                                                 \
  } /* wtf return error to caller? */
#else /* !(MEMDEBUG || TX_DEBUG) */
#  define EXTRA_PAGE_CHECK(bt, p)                                       \
  { if (TXApp && (TXApp->validateBtrees & 0x0040))                      \
      TXbtreeIsValidPage(TXPMBUFPN, __FUNCTION__, (bt), -1, (p), NULL); \
  } /* wtf return error to caller? */
#endif /* !(MEMDEBUG || TX_DEBUG) */

static CONST char OutOfMem[] = "Out of memory";
static CONST char	BtreeExt[] = ".btr";
#define BTREE_EXT_SZ	(sizeof(BtreeExt) - 1)

static int	GotBtCmpEq = 0;


static void nonlinmsg ARGS((BTREE * t, CONST char *fn));
static void
nonlinmsg(t, fn)
BTREE *t;
CONST char *fn;
{
	putmsg(MERR + UGE, fn, "Non-linear op attempted in tree %s",
	       getdbffn((t)->dbf));
}

#  define LINEAR_CHK(t, e, f)   \
  if ((t)->flags & BT_LINEAR)   \
    {                           \
      nonlinmsg(t, f);          \
      e;                        \
    }

/******************************************************************/

static int cmp ARGS((void *, size_t, void *, size_t, void *));

static int
cmp(a, al, b, bl, usr)
void *a;
size_t al;
void *b;
size_t bl;
void *usr;
{
	int rc, tocmp;

	tocmp = al < bl ? al : bl;
	rc = memcmp(a, b, tocmp);
	if (rc == 0)
		rc = al - bl;
	return rc;
}

/******************************************************************/

static int vbtcmp ARGS((BTREE *bt, BPAGE *p, int n, void *key, int keylen));

static int
vbtcmp(bt, p, n, key, keylen)
BTREE *bt;
BPAGE *p;
int n;
void *key;
int keylen;
{
	static CONST char	Fn[] = "vbtcmp";
	int rc;
	struct bkl *x;
	CONST char	*itemKey, *endOfPage;

	if ((unsigned)n >= (unsigned)p->count)
	{
		putmsg(MERR, Fn,
  "Attempt to compare nonexistent item %d of %d-item page 0x%wx of B-tree %s",
		       n, (int)p->count, (EPI_HUGEUINT)btpg2off(bt, p),
		       getdbffn(bt->dbf));
		return -1;
	}
	x = &p->items[n].vf.var;
#ifdef NEVER
	putmsg(999, "vbtcmp", "Page: %lx, Index %d", p, n);
#endif
	itemKey = (CONST char *)p + x->key;

	/* KNG 20090928 sanity check: */
	endOfPage = (CONST char *)p + bt->pagesize;
	if (itemKey >= endOfPage || itemKey + x->len > endOfPage ||
	    itemKey < (CONST char *)p || x->len < 0)
	{
		putmsg(MERR + MAE, Fn,
			"Item %d out of bounds of page 0x%wx of B-tree %s",
			n, (EPI_HUGEUINT)btpg2off(bt, p), getdbffn(bt->dbf));
		return(-1);
	}

	rc =
		bt->cmp((void *)itemKey, x->len, key, keylen, bt->usr);
	if (rc == 0)				/* item == `key' */
	{
		GotBtCmpEq = 1;			/* for btlogop() */
		/* If bt->search is nonzero (i.e. BT_SEARCH_BEFORE or
		 * BT_SEARCH_AFTER), return it; this is used to bias search()
		 * towards the first (BT_SEARCH_BEFORE == 1) or last
		 * (BT_SEARCH_AFTER == -1) item in tree that matches `key'.
		 * This also means that if bt->search is nonzero, our return
		 * is always nonzero:
		 * >>> NOTE: see KNG 011115 comments elsewhere if changing <<<
		 */
		if (BT_SEARCH_FIND == bt->search_pos)
		{
			if (TXgetoff(&p->items[n].locn) == RECID_DELETED)
				return -1;	/* bias search to next item */
			else
				return 0;
		}
		else
		{
			return bt->search_pos;
		}
	}
	else
	{
		return rc;
	}
}

/*****************************************************************************/

BTREE *
closevbtree(b)
BTREE *b;
{
	int i;
	BTLOC	loc;

	if (b == (BTREE *) NULL)
		return b;
	btflush(b);				/* WTF return code? */
	if (b->cache != (BCACHE *) NULL)
	{
		for (i = 0; i < b->cachesize; i++)
		{
			if (b->cache[i].page != (BPAGE *) NULL)
				b->cache[i].page =
					btfrpage(b, b->cache[i].page);
		}
		b->cache = TXfree(b->cache);
	}
	b->his = TXfree(b->his);
	if (b->flags & BT_LOGOPS)
	{
		TXsetrecid(&loc, b->root);	/* log root: more useful */
		btlogop(b, 0, NULL, &loc, "close", "ok");
	}
	b->datad = closedd(b->datad);
	if (!(b->flags & BT_SHAREDBF))
		b->dbf = closedbf(b->dbf);
	b = TXfree(b);
	return (BTREE *) NULL;
}

/*****************************************************************************/

void
vbtsetdefcmp(b)
BTREE *b;
{
	b->cmp = cmp;
}

/*****************************************************************************/

BTREE *
openvbtree(filename, order, cashsize, flags, mode, hdroff, sharedbf)
char *filename;
int order;
int cashsize;
int flags;
int mode;
EPI_OFF_T	hdroff;		/* usually 0; -1 for new shared-DBF B-tree */
DBF		*sharedbf;	/* non-NULL: use this DBF, shared */
{
	static CONST char fn[] = "openvbtree";
	BTREE *b = NULL;
	void *v;
	DBF *dbf;
	size_t sz, fsz, ssz;
	char		*s;
	CONST char	*f;
	int		saveflagson = 0, saveflagsoff = 0, isnew = 0;
	BTLOC		loc;
	extern char *TxBtreeErr, TxBtreeErrBuf[];

	TxBtreeErr = CHARPN;

	/* see also check below: */
	if (TXApp && (TXApp->validateBtrees & 0x01))
	{
		if (flags & BT_FIXED)
		{
			putmsg(MERR + FRE, fn,
			       "Internal error: Fixed flag specified for B-tree `%s' but variable function called",
			       (b->dbf ? getdbffn(b->dbf) : "?"));
			TxBtreeErr="Fixed flag specified in variable function";
			goto err;
		}
		/* see also check below: */
		if (order < (int)sizeof(BPAGE) || order > EPI_OS_SHORT_MAX)
		{
				putmsg(MERR + UGE, fn,
		"Invalid order %d specified for RAM B-tree: Must be %d to %d",
				       (int)order, (int)sizeof(BPAGE),
				       (int)EPI_OS_SHORT_MAX);
			TxBtreeErr = "Invalid order specified";
			goto err;
		}
	}

	if (sharedbf != DBFPN)		/* already open, shared */
	{
		flags |= BT_SHAREDBF;
		/* KNG 20070423 WTF check that pre/postbufsz is consistent
		 * between `sharedbf' and B-tree at all times, e.g. btsetroot()
		 * etc. below; in the meantime disallow BT_SHAREDBF.
		 * also check openfbtree(), kdbf_openfbtree()?
		 */
		putmsg(MERR + UGE, fn,
			"Internal error: BT_SHAREDBF not fully supported yet");
		dbf = sharedbf;
	}
	else
	{
		flags &= ~BT_SHAREDBF;
		TXseterror(0);
		dbf = opendbf(TXPMBUFPN, filename, mode);
	}

	if (dbf == (DBF *) NULL)	/* WTF error msg? */
	{			/* try to set one */
		if (TXgeterror() == 0)
			TxBtreeErr = "Unknown opendbf() error";
		else
		{
			TXstrncpy(TxBtreeErrBuf,TXstrerror(TXgeterror()),128);
			TxBtreeErrBuf[128 - 2] = '\0';
			strcat(TxBtreeErrBuf, "?");	/* we're guessing */
			TxBtreeErr = TxBtreeErrBuf;
		}
		return (BTREEPN);
	}

	if (TXbtreelog != CHARPN && filename != CHARPN)
	{
		s = TXbtreelog;
		if (strncmp(s, "off=", 4) == 0) s += 4;
		else if (strncmp(s, "on=", 3) == 0) s += 3;
		ssz = strlen(s);
		if (ssz >= BTREE_EXT_SZ &&
		    strcmp(s + ssz - BTREE_EXT_SZ, BtreeExt) == 0)
			ssz -= BTREE_EXT_SZ;	/* ignore trailing ".btr" */
		if (TX_ISABSPATH(s))
			f = filename;
		else				/* relative path */
			f = TXbasename(filename);
		fsz = strlen(f);
		if (fsz >= BTREE_EXT_SZ &&
		    strcmp(f + fsz - BTREE_EXT_SZ, BtreeExt) == 0)
			fsz -= BTREE_EXT_SZ;	/* ignore trailing ".btr" */
		if (fsz == ssz && strncmp(f, s, fsz) == 0)
		{				/* matches this B-tree */
			if (s == TXbtreelog + 4) saveflagsoff |= BT_LOGOPS;
			else saveflagson |= BT_LOGOPS;
		}
	}

	flags |= saveflagson;
	flags &= ~saveflagsoff;

	b = (BTREE *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(BTREE));
	if (b == (BTREE *) NULL)
	{
		if (!(flags & BT_SHAREDBF)) dbf = closedbf(dbf);
		TxBtreeErr = (char *)OutOfMem;
		return b;
	}
	b->hdroff = hdroff;
	if (b->hdroff == (EPI_OFF_T)(-1) ||	/* new B-tree same DBF */
	    (v = (void *) getdbf(dbf, b->hdroff, &sz)) == NULL)
	{
		isnew = 1;
		b->magic = BTREE_MAGIC;
		b->order = order;
		b->npages = 0;
		b->root = 0;
		b->pagesize = order;
		b->flags = (char) (flags & BT_SAVE_FLAGS);
		b->iamdirty = 2;		/* 2 == new tree */
		(void)ddsettype(b->datad, 0);
		b->szread = 0;
		b->dbf = dbf;
		if (btsetroot(b) < 0) goto err;	/* could not write */
	}
	else					/* file already exists */
	{
		memcpy(b, v, sizeof(BTREES));
		b->szread = sz;
		b->dbf = dbf;
		if (sz > sizeof(BTREES))
		{
			NDD *odd;

			odd = &((BTREEO *) v)->datad;
			b->datad =
				convertdd(odd,
					  sz - ((char *) odd - (char *) v));
		}
		else if (b->datadoff)
		{
			b->datad = getdbf(b->dbf, b->datadoff, &sz);
			/* if BT_LOGOPS, could log RDdd here, but we know
			 * it always happens at open, which hasn't logged yet
			 */
			b->datad = convertdd(b->datad, sz);
		}
		b->flags &= BT_SAVE_FLAGS;	/*no BT_LINEAR etc from file*/
		if (flags & BT_LINEAR)
		{
			putmsg(MERR + UGE, fn,
			       "Cannot set linear mode on existent tree %s",
			       filename);
			TxBtreeErr = "Cannot set linear mode on existent tree";
			goto err;
		}
	}
#ifdef NEVER			/* WTF JMT 970804 */
	ddsettype(b->datad, 0);
#endif

	/* Add back BT_LINEAR etc. non-DBF-saved flags.
	 * Since BT_EXCLUSIVEACCESS must always be in-sync with its
	 * corresponding DBF ioctls, leave it off of `b->flags' until
	 * ioctlbtree() sets it (simultaneously with its DBF ioctls):
	 */
	b->flags |= (char) (flags & ~(BT_SAVE_FLAGS | BT_EXCLUSIVEACCESS));

	if (cashsize)
		b->cachesize = cashsize;	/* cachesize can be varied */
	if (b->magic != BTREE_MAGIC)
	{
		if (!(flags & BT_SHAREDBF)) dbf = closedbf(dbf);
		b = TXfree(b);
		TxBtreeErr = "Bad magic number (corrupt file?)";
		return (BTREEPN);
	}
	/* sanity check; see also check above: */
	if (TXApp && (TXApp->validateBtrees & 0x01))
	{
		if (b->flags & BT_FIXED)
		{
			/* This may not be fatal long-term, as we'll
			 * call the right insert/del funcs based on
			 * the loaded B-tree's BT_FIXED flag.  But it
			 * may flummox our `order'/`pagesize' sanity
			 * checks here, and caller might expect a
			 * fixed B-tree:
			 */
			putmsg(MERR + UGE, fn,
		     "Fixed B-tree `%s' may not be opened as variable B-tree",
			       (b->dbf ? getdbffn(b->dbf) : "?"));
			TxBtreeErr = "Fixed B-tree opened as variable B-tree";
			goto err;
		}
		/* `order' is same as `pagesize' for vbtree, and
		 * `freesp'/`stacktop' are pagesize-related but shorts:
		 */
		if (b->order < (int)sizeof(BPAGE) ||
		    b->order > EPI_OS_SHORT_MAX)
		{
			putmsg(MERR + FRE, fn,
		   "Corrupt B-tree `%s': Invalid order %d: Must be %d to %d",
			       (b->dbf ? getdbffn(b->dbf) : "?"),
			       (int)b->order, (int)sizeof(BPAGE),
			       (int)EPI_OS_SHORT_MAX);
			TxBtreeErr = "Invalid order";
			goto err;
		}
		if (b->pagesize != b->order)
		{
			putmsg(MERR + FRE, fn,
		   "Corrupt B-tree `%s': Pagesize %d does not equal order %d",
			       (b->dbf ? getdbffn(b->dbf) : "?"),
			       (int)b->pagesize, (int)b->order);
			TxBtreeErr = "Pagesize invalid for order";
			goto err;
		}
	}

	if (
	    (b->cache =
	     (BCACHE *)TXcalloc(TXPMBUFPN, fn, b->cachesize,
			       sizeof(BCACHE))) == (BCACHE *) NULL
	    || (b->his =
		(BTRL *)TXcalloc(TXPMBUFPN, fn, b->cachesize, sizeof(BTRL))) == (BTRL *) NULL)
	{
		TxBtreeErr = (char *)OutOfMem;
		goto err;
	}

	/* Use pre/postbufsz for efficiency.  See also [kdbf_]openfbtree().
	 * Align prebufsz for BTREES/BPAGE.  Note that if BT_SHAREDBF,
	 * `b->dbf' will probably already have pre/postbufsz set from
	 * previous open.  Should be ok as long as TXbtgetoptimize() is
	 * constant through all calls with the same shared DBF, so that
	 * `b->dbf' pre/postbufsz is consistent with `b' pre/postbufsz:
	 */
	/* TX_ALIGN_BYTES may not be enough alignment, but we cannot change
	 * it since that affects .tbl back-compatibility.  Bump it up:
	 */
#define ASZ     \
  (TX_ALIGN_BYTES > EPI_OFF_T_BITS/8 ? TX_ALIGN_BYTES : EPI_OFF_T_BITS/8)
#define ALIGN_UP(n)     ((((n) + ASZ - 1) / ASZ) * ASZ)
	if (TXbtgetoptimize() & 0x2)
	{
		if (ioctldbf(b->dbf, (DBF_KAI | KDBF_IOCTL_PREBUFSZ),
				(void *)ALIGN_UP(KDBF_PREBUFSZ_WANT)) == 0)
			b->prebufsz = ALIGN_UP(KDBF_PREBUFSZ_WANT);
		if (ioctldbf(b->dbf, (DBF_KAI | KDBF_IOCTL_POSTBUFSZ),
				(void *)ALIGN_UP(KDBF_POSTBUFSZ_WANT)) == 0)
			b->postbufsz = ALIGN_UP(KDBF_POSTBUFSZ_WANT);
	}

	/* Add in BT_EXCLUSIVEACCESS: */
	if ((flags & BT_EXCLUSIVEACCESS) &&
	    ioctlbtree(b, BTREE_IOCTL_EXCLUSIVEACCESS, (void *)1) != 0)
		goto err;

	b->sdepth = 0;
	b->cdepth = 0;
	b->iamdirty = 0;
	b->cacheused = 0;
	b->lcode = 0;
	b->hcode = 0;
	if ((b->flags & saveflagson) != saveflagson)
	{
		b->flags |= saveflagson;
		/* We want to save the new flags, but we can only
		 * write to the DBF if it was opened in write mode with
		 * write locks, so only flag it dirty if so:
		 */
		if (mode & (O_WRONLY | O_RDWR)) b->iamdirty = 1;
	}
	if ((b->flags & saveflagsoff) != 0)
	{
		b->flags &= ~saveflagsoff;
		if (mode & (O_WRONLY | O_RDWR)) b->iamdirty = 1;
	}
	DBGMSG(9,
	       (999, NULL, "Opened btree %s.  Root = %d", filename, b->root));
	btsetdefcmp(b);
	if (b->flags & BT_LOGOPS)
	{
		TXsetrecid(&loc, b->root);	/* log root: more useful */
		btlogop(b, 0, NULL, &loc, (isnew ? "create" : "open"), "ok");
	}
	goto done;

err:
	b = closevbtree(b);
done:
	return(b);
#undef ALIGN_UP
#undef ASZ
}

/*****************************************************************************/

static BTLOC search ARGS((BTREE *bt, char *x, int len, EPI_OFF_T root,
			BTLOC *locn));

static BTLOC
search(bt, x, len, root, locn)
BTREE *bt;		/* (in/out) tree to search */
char *x;		/* (in) key to search for */
int len;		/* (in) key length */
EPI_OFF_T root;		/* (in) root page of (sub)tree to start searching */
BTLOC *locn;		/* (in, opt) secondary key */
{
	static CONST char	Fn[] = "[vbt]search";
	BPAGE *p;
	int l, r, i, c, tv = 1;
	BTLOC rc;

	if (root == bt->root)			/* first call */
		bt->cdepth = 0;
	else
		bt->cdepth++;
	/* bt->cdepth is our current depth in the tree.     KNG 011115
	 * bt->sdepth is ultimately either the level we found the key(+locn),
	 * or a "low water mark" in the tree where it should be.
	 * When exiting a level, we always decrement bt->cdepth, but
	 * leave bt->sdepth alone.  However, when *entering* a level
	 * (here), bt->sdepth should be set equal bt->cdepth: we're
	 * searching a new sub-tree; the key(+locn) is assumed to be in it.
	 * (This helps protect against a dbidx.c:1.13-style bug, where we'd
	 * erroneously hop up and down the tree looking for a recid,
	 * but kept incrementing bt->sdepth beyond the end of bt->his.
	 * WTF is this logic valid given the while-loop at bottom,
	 * which may search a sub-tree but return later on this level?):
	 */
	bt->sdepth = bt->cdepth;

	if (root == (EPI_OFF_T)0)		/* empty tree (iff root) */
	{
	mtorbad:
		if ((unsigned)bt->sdepth < (unsigned)bt->cachesize)
		{
			bt->his[bt->sdepth].page = (EPI_OFF_T)0;
			bt->his[bt->sdepth].index = -1;	/* KNG 011113 */
		}
		TXsetrecid(&rc, (EPI_OFF_T)(-1));
	}
	else if (bt->cdepth >= bt->cachesize)	/* real bad */
	{
		putmsg(MERR, Fn,
		 "History exceeded for B-tree %s (cdepth %d >= cachesize %d)",
		       getdbffn(bt->dbf), bt->cdepth, bt->cachesize);
		goto mtorbad;
	}
	else if ((p = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root,
			      (EPI_OFF_T)(root == bt->root ? -2 : -1), -1);
		goto mtorbad;
	}
	else
	{
		l = i = 0;
		r = p->count;
		while (l < r)			/* binary search the page */
		{
			i = (l + r) / 2;
			c = vbtcmp(bt, p, i, x, len);
			if (c == 0)		/* item `i' matches our key */
			{
				if (TXrecidvalid(locn))
				{		/* secondary key is `locn' */
					c =
						_recidcmp(&p->items[i].locn,
							  locn);
				}
				else if ((r - l) > 1)
					c = 1;	/* 1st if multiple possible */
			}
			if (c > 0)		/* item `i' > key(+locn) */
				r = i;
			else			/* item `i' <= key(+locn) */
				l = i + 1;
		}
		/* At this point item is at r-1 if it matches
		 * (-1 because `if (c > 0)' instead of `if (c >= 0)'?),
		 * or may be in subtree at [r-1].hpage if it doesn't.
		 */
		/* If not looking for an exact recid, find first matching
		 * key on the page.  (WTF could remove this loop because
		 * ((r - l) > 1) check above takes care of it? KNG)
		 * KNG 011115 optimization: if bt->search is nonzero,
		 * vbtcmp() always returns nonzero, so skip the loop:
		 * >>> NOTE: see other KNG 011115 comments, and vbtcmp() <<<
		 */
		if (!TXrecidvalid(locn) && bt->search_pos == BT_SEARCH_FIND)  /* KNG 011115 */
			while (r > 1
			       && ((tv = vbtcmp(bt, p, r - 2, x, len)) == 0))
				r--;
		bt->his[bt->cdepth].page = root;
#ifdef NEVER
		error error error; tv may not be set due to KNG opt. above;
		putmsg(999, NULL, "tv = %d, r = %d", tv, r);
		bt->his[bt->cdepth].index = r - (tv ? 1 : 0);
#else
/* This is probably broken in some case, but for now */
		bt->his[bt->cdepth].index = r - 1;
#endif

		/* We've found the key(+locn), or the spot it should be,
		 * on this page.  Either way, search the sub-tree (if any)
		 * for key(+locn):
		 */
		if (r == 0 || r == 1)		/* [r-1].hpage is p->lpage */
		{
			if (p->lpage != (EPI_OFF_T)0 &&
			    (r == 0 || (bt->search_pos == BT_SEARCH_FIND &&    /* KNG 011115 */
					vbtcmp(bt, p, 0, x, len) == 0)))
			{
				bt->his[bt->cdepth].index = -1;
				rc = search(bt, x, len, p->lpage, locn);
			}
			else
				TXsetrecid(&rc, (EPI_OFF_T)(-1));
		}
		else
		{
			if (p->items[r - 2].hpage != (EPI_OFF_T)0 &&
			    (bt->search_pos == BT_SEARCH_FIND &&	/* KNG 011115 optimize */
			     vbtcmp(bt, p, r - 1, x, len) == 0))
				rc =
					search(bt, x, len,
					       p->items[r - 2].hpage, locn);
			else
				TXsetrecid(&rc, (EPI_OFF_T)(-1));
		}
		if (TXrecidvalid(&rc))	/* it was found on a deeper page */
		{
			p = btreleasepage(bt, root, p);
			if (bt->cdepth > 0) bt->cdepth--;
			return rc;
		}
		if (r == 0)
			r++;
		while (r <= p->count
		       && (tv = vbtcmp(bt, p, r - 1, x, len)) <= 0)
		{
			if (tv == 0)	/* item r-1 matches key */
			{
				if (!TXrecidvalid(locn) ||
				    _recidcmp(&p->items[r - 1].locn,
					      locn) == 0)
				{	/* found key (and locn if given) */
					BTLOC	ret;

					bt->his[bt->cdepth].index = r - 1;
					ret = p->items[r - 1].locn;
					p = btreleasepage(bt, root, p);
					bt->sdepth = bt->cdepth;
					if (bt->cdepth > 0) bt->cdepth--;
					return(ret);
				}
				if (TXrecidvalid(locn) &&
				    _recidcmp(&p->items[r - 1].locn,
					      locn) > 0)
				{	/* past possible locn on this page */
					TXsetrecid(&rc, (EPI_OFF_T)(-1));
					break;
				}
			}
			if (p->items[r - 1].hpage != (EPI_OFF_T)0)
			{
				if (r == p->count)
					bt->his[bt->cdepth].index = r;
				else
					bt->his[bt->cdepth].index = r - 1;
				rc =
					search(bt, x, len,
					       p->items[r - 1].hpage, locn);
				if (TXrecidvalid(&rc))
				{		/* found on deeper page */
					p = btreleasepage(bt, root, p);
					if (bt->cdepth > 0) bt->cdepth--;
					return rc;
				}
			}
			else
				TXsetrecid(&rc, (EPI_OFF_T) - 1);
			r++;
		}
		p = btreleasepage(bt, root, p);
	}
	if (bt->cdepth > 0) bt->cdepth--;
	return rc;
}

/******************************************************************/

static BTLOC getlast ARGS((BTREE *, char *, size_t *, EPI_OFF_T));

static BTLOC
getlast(bt, x, len, root)
BTREE *bt;
char *x;	/* (in/out) buffer; key copied to it */
size_t *len;	/* (in/out) length of `*x' */
EPI_OFF_T root;
/* Gets the last item from the tree.  Called by vbtgetlast().
 * Returns recid, or -1 if not found (empty tree) or error
 * (TX_BTREE_GOT_ERROR() will be true on error).
 */
{
	static CONST char Fn[] = "[vbt]getlast";
	BPAGE *p = BPAGEPN;
	int r;
	BTLOC rc;
	BITEM *item;
	int nlen = *len;

	if (root == bt->root)
	{
		bt->cdepth = 0;
		bt->sdepth = 0;
	}
	else
	{
		bt->cdepth++;
		bt->sdepth++;
	}
	if (root == (EPI_OFF_T)0)		/* empty tree (iff root) */
	{
		goto atEof;
	}
	else if (bt->cdepth >= bt->cachesize)	/* real bad */
	{
		putmsg(MERR, Fn,
		 "History exceeded for B-tree %s (cdepth %d >= cachesize %d)",
		       getdbffn(bt->dbf), bt->cdepth, bt->cachesize);
		goto err;
	}
	else if ((p = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root,
			      (EPI_OFF_T)(root == bt->root ? -2 : -1), -1);
		goto err;
	}
	else
	{
		r = p->count;
		if (r <= 0)
		{
			putmsg(MERR, Fn,
			       "Encountered %d-item page 0x%wx in B-tree %s",
			       r, (EPI_HUGEUINT)root, getdbffn(bt->dbf));
			goto err;
		}
		bt->his[bt->cdepth].page = root;     /* KNG 011112 */
		if (p->items[r - 1].hpage != (EPI_OFF_T)0)
		{				/* go deeper to the right */
			bt->his[bt->cdepth].index = r;
			rc = getlast(bt, x, len, p->items[r - 1].hpage);
		}
		else				/* bottom level, return item*/
		{
			bt->his[bt->cdepth].index = r - 1;	/* KNG */
			item = &p->items[r - 1];
			if (nlen > item->vf.var.len)
				nlen = item->vf.var.len;
			else if (nlen < item->vf.var.len)
			{
				putmsg(MERR + MAE, Fn,
    "Buffer size %d too small for %d-byte item %d of page 0x%wx of B-tree %s",
				       nlen, (int)item->vf.var.len, r-1,
				       (EPI_HUGEUINT)root, getdbffn(bt->dbf));
				goto err;
			}
			memcpy(x, (char *) p + item->vf.var.key, nlen);
			if (*len > (size_t) nlen)
				((char *) x)[nlen] = '\0';
			*len = (size_t) nlen;
			rc = item->locn;
		}
	}
        goto finally;

err:
	TX_BTREE_SET_ERROR(bt, 1);
atEof:
	TXsetrecid(&rc, RECID_INVALID);		/* EOF or error */
	if (len) *len = 0;
	/* do not clear/terminate `x': wtf some callers assume untouched? */
finally:
	p = btreleasepage(bt, root, p);
	if (bt->cdepth > 0) bt->cdepth--;
	return rc;
}

/*****************************************************************************/

BTLOC
vbtgetnext(bt, len, x, pdataptr)
BTREE	*bt;		/* (in/out) the B-tree */
size_t	*len;		/* (in/out, opt.; req. if `x') length of `x' buffer */
void	*x;		/* (in/out, opt.) buffer to copy key to */
byte	**pdataptr;	/* (out, opt.) pointer to actual key data on page */
/* Returns invalid recid on EOF or error; TX_BTREE_GOT_ERROR() true on error.
 * NOTE: `*len' may not be updated on EOF.
 */
{
	static CONST char Fn[] = "vbtgetnext";
	BPAGE *p = BPAGEPN;
	BITEM *item;
	BTLOC btloc;
	int level = bt->sdepth;
	int index;
	int f, i;
	EPI_OFF_T page = (EPI_OFF_T)RECID_INVALID, xpage = bt->his[level].page;

	TXsetrecid(&btloc, RECID_INVALID);
	if (x && !len)
	{
		putmsg(MERR, Fn,
		       "Must supply length if you supply a buffer.");
		goto err;
	}
	LINEAR_CHK(bt, goto err, Fn);
	index = bt->his[level].index;
	if (xpage == (EPI_OFF_T)0)		/* e.g. rewindbtree() */
	{
		xpage = bt->root;
		if (xpage == (EPI_OFF_T)0)	/* empty (sub?)tree */
			goto atEof;
		index = -1;
		bt->his[0].index = -1;
		bt->his[0].page = xpage;
	}
	f = 0;
	do
	{
		if (level >= bt->cachesize)
		{
			putmsg(MERR, Fn,
		 "History exceeded for B-tree %s (cdepth %d >= cachesize %d)",
			       getdbffn(bt->dbf), level, bt->cachesize);
			goto err;
		}
		if (page != xpage || p == BPAGEPN)
		{
			if (p != BPAGEPN)
				p = btreleasepage(bt, page, p);
			page = xpage;
			if ((p = btgetpage(bt, page)) == BPAGEPN)
			{
			pagefail:
				btcantgetpage(Fn, bt,page,(EPI_OFF_T)(-1),-1);
				goto err;
			}
		}
		if (index == -1)		/* `index' is p->lpage */
		{
			xpage = p->lpage;
			if (xpage != (EPI_OFF_T)0)
			{			/* go left and deeper */
				index = -1;
				level++;
				bt->his[level].page = xpage;
				bt->his[level].index = -1;
			}
			else			/* go right on this page */
			{
				/* xpage == 0, so will fall through */
				bt->his[level].index++;
				index++;
			}
		}
		else if ((0 < p->count) && (index < p->count))	/* `index' is on the page */
		{
			item = &p->items[index];
			if (f)			/* stop, return this item */
				xpage = (EPI_OFF_T)0;
			else			/* keep looking */
			{
				xpage = item->hpage;
				if (xpage != (EPI_OFF_T)0)
				{		/* go right and deeper */
/* Starting a new page.  If we are doing geo search will want to check if
   any location covered by this page could possibly be in our target region */
					long lcode, hcode;

					/* Are we doing geographic search */
					if (bt->hcode > 0)
					{
						/*Do we have a bounded region? */
						if (index < p->count - 1)
						{
							lcode =
								*(long
								  *) ((char *)
								      p +
								      item[0].
								      vf.var.
								      key);
							hcode =
								*(long
								  *) ((char *)
								      p +
								      item[1].
								      vf.var.
								      key);
							if (!TXcodesintersect
							    (lcode, hcode,
							     bt->lcode,
							     bt->hcode))
							{
/* The page below here can not possibly contain any of the desired matching
   points, therefore we don't descend, but move to the next item in this
   page. */
								xpage =
									bt->
									his
									[level].
									page;
								index =
									++bt->
									his
									[level].
									index;
								continue;
							}
						}
					}
					level++;
					index = -1;
					bt->his[level].page = xpage;
					bt->his[level].index = -1;
				}		/*else no deeper pg to right*/
				else if (index < p->count-1) /*not last item*/
				{		/* go right on this page */
					/* xpage == 0, so will fall through */
					index++;
					bt->his[level].index++;
				}		/* else last item */
				else if (level > 0)	/* not root page */
				{		/* back up a page, go right */
					level--;
					xpage = bt->his[level].page;
					index = ++bt->his[level].index;
					f = 1;	/* return next item */
				}
				else		/* last item, root page */
					goto atEof;
			}
		}
		else				/* `index' is off the page */
		{
			if (level > 0)		/* not root page */
			{
				level--;	/* back up a page, go right */
				DBGMSG(9,
				       (999, Fn, "Level: %d Index: %d\n",
					level, bt->his[level].index));
				xpage = bt->his[level].page;
				index = ++bt->his[level].index;
				f = 1;		/* return next item */
			}
			else			/* root page */
			{
				item = &p->items[p->count - 1];
				xpage = item->hpage;
				if (xpage != (EPI_OFF_T)0 && index == p->count)
				{
/* Starting a new page.  If we are doing geo search will want to check if
   any location covered by this page could possibly be in our target region */
/* Difficult to do here.  This is the page of everything greater than this
   page, and since we'd be doing a stopatrange search, there must be some
   matching points with code greater than us.  */
					level++;
					index = -1;
					bt->his[level].page = xpage;
					bt->his[level].index = -1;
				}
				else
					goto atEof;
			}
		}
/* btreleasepage (bt, page, p); *//* done at top KNG */
	}
	while (xpage != 0);
	bt->sdepth = level;
	if (bt->stopatrange && (bt->his[level].page == bt->rangeend.page &&
				bt->his[level].index == bt->rangeend.index))
		goto atEof;
	xpage = bt->his[level].page;
	if (page != xpage)	/* often same  KNG 980217 */
	{
		p = btreleasepage(bt, page, p);
		page = xpage;
		p = btgetpage(bt, page);
		if (p == BPAGEPN)
			goto pagefail;
	}
	item = &p->items[i = bt->his[level].index];
	if ((level == 0) && (i == (p->count - 1)))
		bt->his[0].index++;
#define KEY_SZ	(item->vf.var.len)
	if (len && *len < (size_t)KEY_SZ)
	{
		/* we still yap if no buffer: still wrong size, might
		 * be a problem if buffer added later:
		 */
		putmsg((x ? MERR : MWARN) + MAE, Fn,
		       "Buffer size %wd too small (need %wd) for item %d of page 0x%wx of B-tree %s%s",
		       (EPI_HUGEINT)(*len), (EPI_HUGEINT)KEY_SZ, i,
		       (EPI_HUGEUINT)page, getdbffn(bt->dbf),
		       (x ? "" : ": ignored, no buffer supplied"));
		goto err;
	}
	if (x)
	{
		memcpy(x, (char *) p + item->vf.var.key, KEY_SZ);
		if (*len > (size_t)KEY_SZ)
			((char *) x)[KEY_SZ] = '\0';
	}
	if (pdataptr)
	{
		*pdataptr = (byte *) p + item->vf.var.key;
	}
	if (len)
		*len = (size_t)KEY_SZ;
#undef KEY_SZ
	btloc = item->locn;
	goto finally;


err:
	TX_BTREE_SET_ERROR(bt, 1);
atEof:
	TXsetrecid(&btloc, RECID_INVALID);	/* EOF or error */
	/* do not clear/terminate `x': wtf some callers assume untouched? */
	/* do not set *len = 0: wtf some callers assume untouched? */
finally:
	p = btreleasepage(bt, page, p);
	return btloc;
}

/*****************************************************************************/

static int getitem ARGS((BTREE *, BPAGE *, int, BITEMI *));

static int
getitem(bt, p, n, bi)
BTREE *bt;	/* (in) B-tree */
BPAGE *p;	/* (in) page to get item from */
int n;		/* (in) array index of item to get */
BITEMI *bi;	/* (out) item */
/* Sets `bi' to malloc'd copy of `n'th item on page `p'.
 * Returns 0 on error.
 */
{
	static CONST char	fn[] = "[vbt]getitem";

	bi->locn = p->items[n].locn;
	bi->hpage = p->items[n].hpage;
	bi->len = p->items[n].vf.var.len;
	bi->string = (char *) TXmalloc(TXPMBUFPN, fn, bi->len);
	if (bi->string == CHARPN)
	{
		bi->alloced = 0;
		return(0);
	}
	else
	{
		bi->alloced = 1;
		memcpy(bi->string, (char*)p +p->items[n].vf.var.key, bi->len);
		return(1);
	}
}

/*****************************************************************************/

int
TXvbtreeSetFreespaceAndStacktop(TXPMBUF *pmbuf, BTREE *bt, BPAGE *p,
				EPI_OFF_T pageOffset, int fixOther)
/* Computes `p->freesp' and `p->stacktop'.  `pageOffset' is for
 * messages; may be -1 if unknown (will look up in cache).
 * If `fixOther', applies fixes to other values (e.g. keys) as needed;
 * else just yaps.
 * Returns 0 on error (with best guess for freesp/stacktop) and yaps.
 */
{
	static const char	truncKey[] = "; truncating key";
	int i, ret = 1;
	int stackUsed, freeSpace, stackTop, heapTop;

	if (bt->flags & BT_FIXED)
	{
		txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
	"Attempt to compute freespace/stacktop on fixed B-tree `%s': Ignored",
		       TX_BTREE_NAME(bt));
		/* default from btinitpage(): */
		freeSpace = bt->order - (short)sizeof(BPAGE);
		stackTop = bt->pagesize;
		ret = 0;
		goto finally;
	}

	heapTop = (int)((byte *)&p->items[p->count] - (byte *)p);
	stackTop = bt->pagesize;
	stackUsed = 0;
	for (i = 0; i < p->count; i++)
	{
		int	len = p->items[i].vf.var.len;
		int	key = p->items[i].vf.var.key;

		if (len < 0 || heapTop + stackUsed + len > bt->pagesize)
		{
			if (pageOffset == (EPI_OFF_T)(-1))
				pageOffset = btpg2off(bt, p);
			txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
	   "Corrupt B-tree `%s': Page 0x%wd item #%d has invalid length %d%s",
				       TX_BTREE_NAME(bt),
				       (EPI_HUGEINT)pageOffset, i, len,
				       (fixOther ? truncKey : ""));
			len = 0;		/* wtf */
			if (fixOther) p->items[i].vf.var.len = len;
			ret = 0;
		}
		if (key < heapTop || key > bt->pagesize - len)
		{
			if (pageOffset == (EPI_OFF_T)(-1))
				pageOffset = btpg2off(bt, p);
			txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
       "Corrupt B-tree `%s': Page 0x%wd item #%d has invalid key offset %d%s",
				       TX_BTREE_NAME(bt),
				       (EPI_HUGEINT)pageOffset, i, key,
				       (fixOther ? truncKey : ""));
			len = 0;		/* wtf */
			if (fixOther) p->items[i].vf.var.len = len;
			key = bt->pagesize; 	/* wtf */
			if (fixOther) p->items[i].vf.var.key = key;
			ret = 0;
		}
		stackUsed += len;
		if (key < stackTop) stackTop = key;
	}
	if (heapTop + stackUsed > bt->pagesize)
	{
		if (pageOffset == (EPI_OFF_T)(-1))
			pageOffset = btpg2off(bt, p);
		txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
		       "Corrupt B-tree `%s': Page 0x%wd has invalid stack used length %d (exceeds max %d); attempting to work around",
		       TX_BTREE_NAME(bt), (EPI_HUGEINT)pageOffset,
		       stackUsed, (bt->pagesize - heapTop));
		stackUsed = bt->pagesize - heapTop;
		ret = 0;
	}
	freeSpace = bt->pagesize - (heapTop + stackUsed);
	/* note: this is actually sizeof(BPAGE.items) == 1*sizeof(BITEM)
	 * *less* than true free space; unknown reason (forgot to subtract
	 * sizeof(BPAGE.items) from sizeof(BPAGE) in original computation?):
	 * See also btinitpage()'s setting of BPAGE.freesp:
	 */
	freeSpace -= sizeof(p->items);

finally:
	p->freesp = freeSpace;
	p->stacktop = stackTop;
	return(ret);
}

/*****************************************************************************/

static int compactpage ARGS((BTREE *, BPAGE *, int, int));

static int
compactpage(t, p, need, location)
BTREE *t;
BPAGE *p;
int need;		/* (in) key size needed */
int location;		/* (in) item index where we want to insert? */
/* If not enough *contiguous* free space for a new item with key length
 * `need', will try to compact page `p' to get more contiguous space,
 * by removing slack space (gaps) in items' keys in the heap, and
 * removing any non-payload (RECID_DELETED and no children) items.
 * Returns number of items before index `location' that were removed,
 * so caller can adjust `location'.
 */
{
	char *heapTop, *stackt, *key;
	long	contiguousFreeSpace;
	BPAGE *tp;
	int i;
	int removed = 0;

/*
	setfreesp(t, p);
	if (p->freesp < need)
		printf("There ain't enough room on the page: %d %d\n", p->freesp, need);
*/
	/* `p->freesp' includes possible orphaned slack space amongst
	 * keys, but we want *contiguous* free space, so compute it:
	 */
	heapTop = (char *) &p->items[p->count];
	stackt = (char *) p + (int) p->stacktop;
	contiguousFreeSpace = stackt - heapTop;
	if (contiguousFreeSpace < (long)sizeof(BITEM) + need)
	{				/* not enough for new BITEM + key */
		DBGMSG(1,
		       (999, NULL, "Compacting page.  Need %d + %d, have %ld",
			(int)sizeof(BITEM), need, contiguousFreeSpace));
		tp = btmkpage(t);		/* make temp new page `tp' */
		if (tp == BPAGEPN) return 0;

		/* Copy all payload (non-RECID_DELETED or non-leaf) items
		 * from `p' to `tp', noting useful removals:
		 */
		tp->lpage = p->lpage;
		for (i = 0; i < p->count; i++)
		{
			if (TXgetoff(&p->items[i].locn) != RECID_DELETED ||
			    p->items[i].hpage != 0)
			{			/* payload item */
				/* Copy `p' item[i] to `tp': */
				tp->items[tp->count] = p->items[i];
				key = (char *) p + p->items[i].vf.var.key;
				tp->stacktop -= p->items[i].vf.var.len;
				memcpy((char *) tp + (int) tp->stacktop, key,
				       p->items[i].vf.var.len);
				tp->items[tp->count].vf.var.key =
					tp->stacktop;
				tp->freesp -=
					(p->items[i].vf.var.len +
					 sizeof(BITEM));
				tp->count++;
                                EXTRA_PAGE_CHECK(t, tp);
			}
			else if (i < location)
			{
				/* Item `i' was removed from (not copied to)
				 * `tp'.  It is also before our `location',
				 * thus its removal frees up useful space:
				 */
				removed++;
			}
		}

		/* Try to ensure new page is non-empty: if empty and
		 * original is not, original must be all non-payload;
		 * copy its first item to `tp' so it is non-empty:
		 */
		if ((tp->count == 0) && (p->count > 0))
		{
			/* Need to fix `removed', as we are putting back
			 * one of the removed items:
			 */
			if (removed && location > 0)
				removed--;
			/* Copy `p' item[0] to `tp' item[0]: */
			tp->count++;
			tp->items[0] = p->items[0];
			key = (char *) p + p->items[0].vf.var.key;
			tp->stacktop -= p->items[0].vf.var.len;
			memcpy((char *) tp + (int) tp->stacktop, key,
			       p->items[0].vf.var.len);
			tp->items[0].vf.var.key = tp->stacktop;
			tp->freesp -=
				(p->items[0].vf.var.len + sizeof(BITEM));
		}

		/* Copy new compact page back to original and free new: */
		memcpy(p, tp, t->pagesize);
		tp = btfrpage(t, tp);		/* free `tp' */
	}
        EXTRA_PAGE_CHECK(t, p);
	return removed;
}

/*****************************************************************************/

static void nospacemsg ARGS((CONST char *fn, BTREE *bt, int loc,
                             CONST void *key, size_t keysize));
static void
nospacemsg(fn, bt, loc, key, keysize)
CONST char      *fn;
BTREE           *bt;
int             loc;
CONST void      *key;
size_t          keysize;
/* KNG 990315 say what filename the tree is, for this error
 */
{
  char  tmp[128];

  putmsg(MWARN, fn,
  "Not enough space on page at item #%d for %wd-byte key `%s' in B-tree `%s'",
         loc, (EPI_HUGEINT)keysize, btkey2str(tmp, sizeof(tmp), key, keysize),
         TX_BTREE_NAME(bt));
}

/*****************************************************************************/

int
TXvbtreeReplaceItem(bt, p, n, bi)
BTREE *bt;
BPAGE *p;
int n;
BITEMI *bi;
/* Replaces existing item `n' with `bi', on page `p'.  Note that item index
 * might change.  Renamed from `vbtputitem()'.
 * Returns 0 on error (WTF old key may be zeroed out).
 */
{
	static CONST char fn[] = "TXvbtreeReplaceItem";
	int newKeyLoc, heaptop, ret;

#ifdef JMT_TEST
	int t;

	t = p->items[n].vf.var.len;
#endif
	DBGMSG(1, (999, NULL, "Replacing item of length %d with one %d long",
		   p->items[n].vf.var.len, bi->len));
	/* Since we are replacing item[n], zero out its key so it can
	 * merge with contiguous free space if compactpage() compacts.
	 * WTF how to undo this if error below?
	 * Bug 6141 but update `p->freesp' too:
	 */
	p->freesp += p->items[n].vf.var.len;
	p->items[n].vf.var.len = 0;
	/* Compact page to make room if needed: */
	n -= compactpage(bt, p, bi->len, n);
	/* Store the new key: */
	newKeyLoc = (int) p->stacktop - bi->len;
	heaptop = (char *) &p->items[p->count] - (char *) p;
	if (newKeyLoc < heaptop)		/* stack/heap collision */
	{
		DBGMSG(1, (999, NULL, "newKeyLoc = %d,  heaptop = %d",
			  newKeyLoc, heaptop));
		nospacemsg(fn, bt, newKeyLoc, bi->string, (size_t)bi->len);
		goto err;
	}
	if (newKeyLoc < 0)			/* overflow */
	{
		nospacemsg(fn, bt, newKeyLoc, bi->string, (size_t)bi->len);
		goto err;
	}
	p->items[n].vf.var.key = newKeyLoc;
	p->items[n].vf.var.len = bi->len;
	p->items[n].locn = bi->locn;
	p->items[n].hpage = bi->hpage;
	/* Bug 6141 we are replacing an item, not adding, so `p->freesp'
	 * only changes by key-size delta, not sizeof(BITEM) too.
	 * We added old key size above, so just subtract new key size:
	 */
	p->freesp -= bi->len;
	DBGMSG(1, (999, NULL, "Freespace set to %d", p->freesp));
	p->stacktop = newKeyLoc;
	memcpy((char *) p + newKeyLoc, bi->string, bi->len);
	ret = 1;				/* success */
	goto finally;

err:
	ret = 0;
finally:
        EXTRA_PAGE_CHECK(bt, p);
	return(ret);
}

/*****************************************************************************/

static int additem ARGS((BTREE *, BPAGE *, int, BITEMI *));

static int
additem(bt, p, n, bi)
BTREE *bt;
BPAGE *p;
int n;
BITEMI *bi;
/* Adds a new item `bi' to `p', inserted as item index `n'.
 * Returns 0 on error (page might be modified by compaction, but
 * item not added).
 */
{
	static CONST char fn[] = "additem";
	int newKeyLoc, heaptop, ret;
	size_t tomove;

	/* Compact page to make room, if needed: */
	n -= compactpage(bt, p, bi->len, n);
	/* Get new item location, verify there is room: */
	newKeyLoc = (int) p->stacktop - bi->len;
	heaptop = (char *) &p->items[p->count + 1] - (char *) p;
	if (newKeyLoc < heaptop)		/* stack/heap collision */
	{
		DBGMSG(1, (999, NULL, "newKeyLoc = %d,  heaptop = %d",
			   newKeyLoc, heaptop));
		nospacemsg(fn, bt, newKeyLoc, bi->string, (size_t)bi->len);
		goto err;
	}
	if (newKeyLoc < 0)			/* overflow */
	{
		nospacemsg(fn, bt, newKeyLoc, bi->string, (size_t)bi->len);
		goto err;
	}
	/* Make a gap in items array: */
	tomove = (p->count++ - n) * sizeof(BITEM);
	if (tomove > 0)
		memmove(&p->items[n + 1], &p->items[n], tomove);
	/* Set the new item: */
	p->items[n].vf.var.key = newKeyLoc;
	p->items[n].vf.var.len = bi->len;
	p->items[n].locn = bi->locn;
	p->items[n].hpage = bi->hpage;
	p->freesp -= (bi->len + sizeof(BITEM));
	DBGMSG(1, (999, NULL, "Freespace set to %d", p->freesp));
	p->stacktop = newKeyLoc;
	memcpy((char *) p + newKeyLoc, bi->string, bi->len);
	ret = 1;				/* success */
	goto finally;

err:
	ret = 0;
finally:
        EXTRA_PAGE_CHECK(bt, p);
	return(ret);
}

/*****************************************************************************/

static int copyAndAddItem ARGS((BTREE *, BPAGE *, int, BPAGE *, int));

static int
copyAndAddItem(bt, s, si, d, di)
BTREE *bt;
BPAGE *s;
int si;
BPAGE *d;
int di;
/* Adds (inserts) page `s' item `si' to page `d' item `di'.  `s' is
 * unmodified.  Renamed from `transferitem()'.
 * Returns 0 on error.
 */
{
	static CONST char Fn[] = "copyAndAddItem";
	BITEMI bi;
	int	ret;

	if (si < 0 || di < 0)
	{
		putmsg(MERR, Fn, "si=%d di=%d for B-tree `%s'",
		       (int)si, (int)di, getdbffn(bt->dbf));
		goto err;
	}
	bi.alloced = 0;
	bi.locn = s->items[si].locn;
	bi.hpage = s->items[si].hpage;
	bi.len = s->items[si].vf.var.len;
	bi.string = (char *) s + s->items[si].vf.var.key;

#ifdef DEBUG
	if (d->freesp < bi.len)
		DBGMSG(1, (999, Fn, "Freesp = %d, need %d string = %.*s",
			   d->freesp, bi.len, bi.len, bi.string));
#endif
	ret = additem(bt, d, di, &bi);
	if (bi.alloced)
		bi.string = TXfree(bi.string);
	goto finally;

err:
	ret = 0;
finally:
	return(ret);
}

/*****************************************************************************/

static int insert
ARGS((BTREE *bt, char *x, int len, EPI_OFF_T root, int *h, BITEMI *item,
	BTLOC locn, int splitpoint, int depth));

static int
insert(bt, x, len, root, h, item, locn, splitpoint, depth)
BTREE *bt;		/* (in/out) B-tree to insert into */
char *x;		/* (in) key to insert */
int len;		/* (in) key length */
EPI_OFF_T root;		/* (in) root page of (sub)tree to insert into */
int *h;			/* (out) 1: item is higher than all `root' items */
BITEMI *item;		/* (out) */
BTLOC locn;		/* (in) location to associate with key */
int splitpoint;
int depth;
/* Returns -1 if already exists in unique index, -2 on severe error,
 * 0 if ok.
 */
{
  static const char     Fn[] = "[vbt]insert";
  BPAGE                 *p = NULL, *b = NULL;
  BITEMI                item1;
  int                   c, l, r, i, middle, rc = 0, lc;
  EPI_OFF_T             bn;
  long                  space;

  item1.alloced = 0;
  item1.string = NULL;
  depth++;
  if (root == (EPI_OFF_T)0)                     /* empty (sub)tree */
    {
    emptyOrBad:
      *h = 1;
      item->key = 0;
      item->len = len;
      item->locn = locn;
      item->hpage = 0;
      item->string = x;
#ifdef MEMDEBUG
      item->alloced = 0;
#endif
    }
  else if (depth > (bt->cachesize + 10))        /* real badness */
    {
      putmsg(MERR, Fn, "depth=%d > cachesize=%d + 10 for B-tree %s",
             depth, (int)bt->cachesize, getdbffn(bt->dbf));
      rc = -2;                                  /* severe error */
      goto emptyOrBad;
    }
  else if ((p = btgetpage(bt, root)) == BPAGEPN)
    {
      btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
      rc = -2;                                  /* severe error */
      goto emptyOrBad;
    }
  else
    {
      l = 0;
      r = p->count;
      while (l < r)                             /* binary search this page */
        {
          i = (l + r) / 2;
          if ((c = vbtcmp(bt, p, i, x, len)) == 0)
            if (!(bt->flags & BT_UNIQUE))
              c = _recidcmp(&p->items[i].locn, &locn);
          if (c > 0)
            r = i;
          else
            l = i + 1;
        }
      if ((bt->flags & BT_UNIQUE) && r > 0 &&
          (vbtcmp(bt, p, r - 1, x, len) == 0))
        {                                       /* Item Already Exists */
          if (bt->flags & BT_UNIQUE)
            {
              lc = -1;                          /* already exists */
            bail:
              *h = 0;
              p = btreleasepage(bt, root, p);
              return(lc);
            }
          else
            {
              if ((lc = insert(bt, x, len, p->items[r - 1].hpage, h,
                               &item1, locn, splitpoint, depth)) < 0)
                goto bail;
            }
        }
      else
        {
          if (r == 0)
            {
              if ((lc = insert(bt, x, len, p->lpage, h, &item1, locn,
                               splitpoint, depth)) < 0)
                goto bail;
            }
          else
            if ((lc = insert(bt, x, len, p->items[r - 1].hpage, h,
                             &item1, locn, splitpoint, depth)) < 0)
              goto bail;
        }
      if (*h == 1)
        {
          btdirtypage(bt, root);
          if (p->freesp > item1.len)            /* `item1' fits on `p' */
            {
              *h = 0;
              if (!additem(bt, p, r, &item1)) goto err;
            }
          else                                  /* split `p' into two pages */
            {
              bn = btgetnewpage(bt);
              if (bn == (EPI_OFF_T)(-1))
                {
                  putmsg(MERR, Fn, "Cannot create new page for B-tree %s",
                         getdbffn(bt->dbf));
                err:
                  lc = -2;                      /* severe error */
                  goto bail;
                }
              if ((b = btgetpage(bt, bn)) == BPAGEPN)
                {
                  btcantgetpage(Fn, bt, bn, (EPI_OFF_T)(-1), -1);
                  goto err;
                }
              btdirtypage(bt, bn);
              /* Find `middle' item of `p': item that is `splitpoint' percent
               * of the way through its used space:
               */
              for (space = 0L - sizeof(BITEM), middle = 0;
                   space <
                     (long) (bt->pagesize - sizeof(BPAGE)) * splitpoint / 100
                     && middle < p->count;
                   middle++)
                {
                  space += (p->items[middle].vf.var.len + sizeof(BITEM));
                  if (r == middle)
                    {
                      space += (item1.len + sizeof(BITEM));
                      middle++;
                    }
                }
              middle--;
              if (middle >= p->count)
                middle = p->count - 1;
              /* Split off some items from `p' into new page `b': */
              if (r <= middle)
                {
                  if (r == middle)
                    {
                      *item = item1;
#ifdef MEMDEBUG
                      if (item1.alloced)
                        item1.alloced = 0;
#endif
                      /* Move upper half of `p' to `b', i.e. items
                       * `middle' through `p->count - 1':
                       */
                      for (i = 0; i < (p->count - middle); i++)
                        if (!copyAndAddItem(bt, p, i + middle, b, i))
                          {
                          delBAndErr:
                            b = btfreepage(bt, bn, b);
                            goto err;
                          }
                      p->count -= b->count;
                      TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt, p,root,1);
                    }
                  else                          /* r < middle */
                    {
                      if (!getitem(bt, p, middle - 1, item)) goto delBAndErr;
                      for (i = 0; i < (p->count - middle); i++)
                        if (!copyAndAddItem(bt, p, i + middle, b, i))
                          goto delBAndErr;
                      p->count -= b->count;
                      p->count--;
                      /* Bug 6141: update freesp/stacktop before additem(): */
                      TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt, p,root,1);
                      /* wtf stumble on if this additem() fails; we
                       * have already moved stuff to `b' so cannot bail:
                       */
                      additem(bt, p, r, &item1);
                    }
                }
              else                              /* r > middle */
                {
                  r -= (middle + 1);
                  if (!getitem(bt, p, middle, item)) goto delBAndErr;
                  for (i = 0; i < r; i++)
                    if (!copyAndAddItem(bt, p, 1 + i + middle, b, i))
                      goto delBAndErr;
                  if (!additem(bt, b, r, &item1)) goto delBAndErr;
                  for (i = r + 1; i < (p->count - middle); i++)
                    if (!copyAndAddItem(bt, p, i + middle, b, i))
                      goto delBAndErr;
                  p->count -= b->count;
                  TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt, p, root, 1);
                }
              b->lpage = item->hpage;
              item->hpage = bn;
              b = btreleasepage(bt, bn, b);
            }
        }
      p = btreleasepage(bt, root, p);
    }
  if (item1.alloced)
    item1.string = TXfree(item1.string);
  return rc;
}

/*****************************************************************************/
/*
 *	Find how many items in `b' can be moved to `a' while maintaining 50%
 *	Population. (Or as close as possible)
 *	For VBTREE only; returns -1, 0, +1
 */

static int moveable ARGS((BTREE *bt, BPAGE *a, BPAGE *b, BPAGE *c, int s));

static int
moveable(bt, a, b, c, s)
BTREE *bt;
BPAGE *a;
BPAGE *b;
BPAGE *c;
int s;
{
	int available;

	if (b != BPAGEPN)
	{
		available = a->freesp + b->freesp;
		available += sizeof(BPAGE);
	}
	else
	{
		available = a->freesp + bt->order;
	}
	available -= c->items[s].vf.var.len;
	if (available > bt->pagesize)
	{
		return 0;
	}
	else
	{
		if (c->items[s].vf.var.len > a->freesp)
			return -1;
		else
			return 1;
	}
}

/*****************************************************************************/
/*
 *	c  - underflowing page
 *	ap - page number of the ancestor page
 *	s  - index of deleted item in c
 */

static int underflow ARGS((BTREE *bt, BPAGE *c, EPI_OFF_T ap, int s, int *h));

static int
underflow(bt, c, ap, s, h)
BTREE *bt;
BPAGE *c;
EPI_OFF_T ap;
int s;
int *h;
/* Returns -1 on severe error, 0 if ok.
 * WTF don't know how benign 0 or missing pages are...
 */
{
	static CONST char Fn[] = "[vbt]underflow";
	BPAGE *a = NULL, *b = NULL;
	BITEMI bi;
	int i, k, mb = 0, mc, n;
	EPI_OFF_T bp;

	DBGMSG(1,
	       (999, NULL, "Delete caused an underflow at %lx", time(NULL)));
	bi.alloced = 0;
	if (ap == (EPI_OFF_T)0) return(0);	/* WTF is this benign? KNG */
	if ((a = btgetpage(bt, ap)) == BPAGEPN)
	{	/* Page doesn't exist.  Underflowing isn't so important */
		/* but a missing page might be...?  KNG 011113 */
		btcantgetpage(Fn, bt, ap, (EPI_OFF_T)(-1), -1);
		return(-1);
	}
	compactpage(bt, a, bt->pagesize, 0);
	n = a->count + 1;
	mc = c->count;
#ifdef NEVER
	if (mc == 0)
	{
		*h = 0;
		return(0);
	}
#endif
	DBGMSG(1, (999, NULL, "S = %d, MC = %d", s, mc));
	if (s < (mc - 1))	/* Use page to right of a if there is one */
	{
		DBGMSG(1, (999, NULL, "Going right"));
		btdirtypage(bt, ap);
		s++;
		bp = c->items[s].hpage;
		if (bp == (EPI_OFF_T)0)			/* no page */
			b = BPAGEPN;
		else if ((b = btgetpage(bt, bp)) == BPAGEPN)
			btcantgetpage(Fn, bt, bp, btpg2off(bt, c), s);
		if (b != BPAGEPN)
		{
			btdirtypage(bt, bp);
			compactpage(bt, b, bt->pagesize, 0);
			mb = b->count;
		}
		k = moveable(bt, a, b, c, s);	/* Number of available items on page b */
		DBGMSG(1, (999, NULL, "Moveable = %d", k));
		if (b != BPAGEPN && k > 0)
		{
			DBGMSG(1, (999, NULL, "Not enough to remove page"));

			k--;
			mb--;
			bi.locn = b->items[k].locn;
			bi.len = b->items[k].vf.var.len;
			bi.string = (char *) b + b->items[k].vf.var.key;
			if (bi.len <= c->items[s].vf.var.len ||
			    c->freesp >= (bi.len - c->items[s].vf.var.len))
			{
				/* a[n-1] = c[s] (insertion): */
				copyAndAddItem(bt, c, s, a, n - 1);
				/* ... */
				a->items[n - 1].hpage = b->lpage;
				b->lpage = b->items[k].hpage;
				bi.hpage = bp;
				TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
								c, -1, 1);
				/* c[s] = bi: */
				TXvbtreeReplaceItem(bt, c, s, &bi);
				/* ... */
				memmove(b->items, &b->items[k + 1],
					mb * sizeof(BITEM));
				b->count = mb;
				/* Bug 6141: set stacktop too: */
				TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
								b, -1, 1);
			}
			*h = 0;
		}
		else if (k == 0)
		{
			DBGMSG(1, (999, NULL, "Enough to remove page"));
			/* a[n-1] = c[s] (insertion): */
			copyAndAddItem(bt, c, s, a, n - 1);
			/* ... */
			if (b != BPAGEPN)
			{
				a->items[n - 1].hpage = b->lpage;
				for (i = 0; i < b->count; i++)
					copyAndAddItem(bt, b, i + k, a, i + n);
                                EXTRA_PAGE_CHECK(bt, a);
			}
			else
				a->items[n - 1].hpage = (EPI_OFF_T)0;
			for (i = s; i < mc - 1; i++)
				c->items[i] = c->items[i + 1];
			c->count = mc - 1;
			/* Bug 6141: set stacktop too: */
			TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt, c,-1,1);
			*h = (unsigned) c->freesp >
			     (bt->pagesize - sizeof(BPAGE)) / 2;
			b = btfreepage(bt, bp, b);
		}
	}
	else
	{
		DBGMSG(1, (999, NULL, "Going left"));
		if (s <= 0)
			bp = c->lpage;
		else
			bp = c->items[s - 1].hpage;
		if (bp == (EPI_OFF_T)0)			/* no page */
			b = BPAGEPN;
		else if ((b = btgetpage(bt, bp)) == BPAGEPN)
			btcantgetpage(Fn, bt, bp, btpg2off(bt, c), s - 1);
		if (b != BPAGEPN)
		{
			btdirtypage(bt, bp);
			compactpage(bt, b, bt->pagesize, 0);
			mb = b->count;
		}
		else
		{
			putmsg(MERR, Fn, "Left Page Missing");
		}
		k = moveable(bt, a, b, c, s);
		if (b != BPAGEPN && k > 0)		/* Must = 1 */
		{
			k--;
			mb--;
			bi.locn = b->items[mb].locn;
			bi.len = b->items[mb].vf.var.len;
			bi.string = (char *) b + b->items[mb].vf.var.key;
			if (bi.len <= c->items[s].vf.var.len ||
			    c->freesp >= (bi.len - c->items[s].vf.var.len))
			{
				btdirtypage(bt, ap);
				copyAndAddItem(bt, c, s, a, k);
				a->items[k].hpage = a->lpage;
				a->lpage = b->items[mb].hpage;
				bi.hpage = ap;
				/* Bug 6141: set stacktop too: */
				TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
								c, -1, 1);
				TXvbtreeReplaceItem(bt, c, s, &bi);
				b->count = mb;
				/* Bug 6141: set stacktop too: */
				TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
								b, -1, 1);
			}
			*h = 0;
		}
		else if (b != BPAGEPN && k == 0)
		{
			copyAndAddItem(bt, c, s, b, mb);
			b->items[mb].hpage = a->lpage;
			for (i = 0; i < n - 1; i++)
				copyAndAddItem(bt, a, i, b, i + mb + 1);
			c->count = mc - 1;
			/* Bug 6141: set stacktop too: */
			TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
							c, -1, 1);
			*h =
				(unsigned) c->freesp >
				(bt->pagesize - sizeof(BPAGE)) / 2;
			a = btfreepage(bt, ap, a);
		}
	}
	a = btreleasepage(bt, ap, a);
	b = btreleasepage(bt, bp, b);
	if (bi.alloced)
		bi.string = TXfree(bi.string);
	return(0);
}

/*****************************************************************************/

static int del ARGS((BTREE *bt, EPI_OFF_T root, int *h, BPAGE *a, int r));

static int
del(bt, root, h, a, r)
BTREE *bt;
EPI_OFF_T root;
int *h;
BPAGE *a;
int r;
/* Returns -1 on severe error, 0 if ok.
 */
{
	static CONST char Fn[] = "[vbt]del";
	BPAGE *p;
	BITEMI bi;
	EPI_OFF_T q;
	int	rc = 0;

	DBGMSG(1,
	       (999, NULL, "Del (root %d, item %d) called at %lx", root, r,
		time(NULL)));
	if ((p = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
		return(-1);
	}
	if (p->count == 0)
	{
		/* Should never reach here.  We will corrupt a page if
		   we do, so get out.  Should maybe try and be more
		   intelligent */
		p = btreleasepage(bt, root, p);
		return(0);
	}
	q = p->items[p->count - 1].hpage;
	btdirtypage(bt, root);
	if (q != (EPI_OFF_T)0)
	{
		if (del(bt, q, h, a, r) < 0)
			rc = -1;
		else if (*h && underflow(bt, p, q, p->count - 1, h) < 0)
			rc = -1;
	}
	else
	{
		bi.len = p->items[p->count - 1].vf.var.len;
		if (bi.len <= a->items[r].vf.var.len ||
		    a->freesp >= (bi.len - a->items[r].vf.var.len))
		{
			p->count--;
			p->items[p->count].hpage = a->items[r].hpage;
			bi.locn = p->items[p->count].locn;
			bi.hpage = p->items[p->count].hpage;
			bi.string =
				(char *) p + p->items[p->count].vf.var.key;
			DBGMSG(1,
			       (999, Fn, "Freesp = %d, need %d", a->freesp,
				bi.len));

			if (bi.len <= a->items[r].vf.var.len ||
			    a->freesp >= (bi.len - a->items[r].vf.var.len))
			{
				TXvbtreeReplaceItem(bt, a, r, &bi);
			}
			else
			{
				putmsg(MERR, Fn,
				       "Could not drag up item in B-tree `%s'",
				       TX_BTREE_NAME(bt));
			}
			/* Bug 6141: set stacktop too: */
			TXvbtreeSetFreespaceAndStacktop(TXPMBUFPN, bt,
							p, root, 1);
			*h =
				p->freesp >
				(int) (bt->pagesize - sizeof(BPAGE)) / 2;
		}
		else
		{
			TXsetrecid(&(a->items[r].locn), RECID_DELETED);
		}
	}
	p = btreleasepage(bt, root, p);
	return(rc);
}

/*****************************************************************************/

static int delete ARGS((BTREE *, BTLOC, char *, int, EPI_OFF_T, int *, int));

static int
delete(bt, btloc, x, len, root, h, depth)
BTREE *bt;
BTLOC btloc;
char *x;
int len;
EPI_OFF_T root;
int *h;
int depth;
/* Returns 1 if item deleted, 0 if not found, -1 on severe error.
 */
{
	static CONST char	Fn[] = "[vbt]delete";
	EPI_OFF_T q;
	int c, i, l, r, rc = 0;
	BPAGE *a = BPAGEPN;

	depth++;
	DBGMSG(1, (999, NULL, "Delete called at %lx", time(NULL)));
	if (root == (EPI_OFF_T)0)			/* empty tree */
		*h = 0;
	else if (depth > (bt->cachesize + 10))
	{
		putmsg(MERR, Fn, "depth=%d > cachesize=%d+10 for B-tree %s",
		       depth,  (int)bt->cachesize, getdbffn(bt->dbf));
		*h = 0;
		return(-1);
	}
	else if ((a = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
		*h = 0;
		return(-1);
	}
	else
	{
		l = 0;
		r = a->count;
		while (l < r)				/* binary search */
		{
			i = (l + r) / 2;
			c = vbtcmp(bt, a, i, x, len);
			if (c == 0 && TXrecidvalid(&btloc))
				c = _recidcmp(&a->items[i].locn, &btloc);
			if (c >= 0)			/* item `i' >= `x' */
				r = i;
			else				/* item `i' < `x' */
				l = i + 1;
		}
		if (r == 0)
			q = a->lpage;
		else
			q = a->items[r - 1].hpage;
		if ((r < a->count) && (vbtcmp(bt, a, r, x, len) == 0) &&
		    (!_recidcmp(&a->items[r].locn, &btloc)
		     || !TXrecidvalid(&btloc)))
		{
			rc = 1;	/* KNG we found it */
			if (q == (EPI_OFF_T)0)
			{
				/* Delete a[r]: */
				btdirtypage(bt, root);
				DBGMSG(1,
				       (999, NULL, "Deleting Recid %d",
					TXgetoff(&a->items[r].locn)));
				a->freesp +=
					a->items[r].vf.var.len +
					sizeof(BITEM);
				/* Bug 6141: update stacktop too: */
				if (a->items[r].vf.var.key == a->stacktop)
				{
					int	i, stackTop = bt->pagesize;
					/* Cannot just add a[r] key len;
					 * might be slack space above.
					 * Fully recompute:
					 */
					for (i = 0; i < a->count; i++)
						if (a->items[i].vf.var.key <
						    stackTop && i != r)
							stackTop = a->items[i].vf.var.key;
					a->stacktop = stackTop;
				}
				a->count--;
				DBGMSG(1,
				       (999, NULL, "Freespace set to %d",
					a->freesp));
				*h =
					a->freesp >
					(int) (bt->pagesize -
					       sizeof(BPAGE)) / 2;
				memmove(&a->items[r], &a->items[r + 1],
					(a->count - r) * sizeof(BITEM));
                                EXTRA_PAGE_CHECK(bt, a);
			}
			else
			{
				DBGMSG(1,
				       (999, NULL, "Deleting Recid %d",
					TXgetoff(&a->items[r].locn)));
				if (del(bt, q, h, a, r) < 0) rc = -1;
				btdirtypage(bt, root);
				if (*h && rc >= 0)
				{
					if (underflow(bt, a, q, r - 1, h) < 0)
						rc = -1;
				}
                                EXTRA_PAGE_CHECK(bt, a);
			}
		}
		else
		{
			rc = delete(bt, btloc, x, len, q, h, depth);
			if (*h && rc >= 0)
			{
				btdirtypage(bt, root);
				if (underflow(bt, a, q, r - 1, h) < 0)
					rc = -1;
			}
                        EXTRA_PAGE_CHECK(bt, a);
		}
	}
	a = btreleasepage(bt, root, a);
	return (rc);
}

/*****************************************************************************/

#ifdef BTPRINT
extern int TxPrintTreeVerbose, TxPrintTreeMaxItemLen, TxPrintTreeIndentFactor;

static void printtree ARGS((BTREE *, EPI_OFF_T, int));

static void
printtree(bt, root, depth)
BTREE *bt;
EPI_OFF_T root;
int depth;
{
	static CONST char Fn[] = "[vbt]printtree";
	BPAGE *p;
	int i, j;
	char *data, *xtra;
	unsigned long len;

	if (root != (EPI_OFF_T)0)
	{
		if ((p = btgetpage(bt, root)) == BPAGEPN)
		{
			btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
			return;
		}
                if (TxPrintTreeVerbose > 0)
                  {
                    htpf("%*sPage 0x%wx: count=%d freesp=%d stacktop=%u lpage=0x%wx\n",
                           depth,"", (EPI_HUGEUINT)root, p->count,(int)p->freesp,
                           (unsigned)p->stacktop, (EPI_HUGEUINT)p->lpage);
                  }
		if (p->lpage == root)
		{
			putmsg(MERR, Fn,
			"Page 0x%wx of B-tree %s has lpage equal to itself",
				(EPI_HUGEINT)root,
				(bt->dbf ? getdbffn(bt->dbf) : "?"));
			return;
		}
		printtree(bt, p->lpage, depth + 1);
		for (i = 0; i < p->count; i++)
		{
			data = (char *) p + p->items[i].vf.var.key;
			len = p->items[i].vf.var.len;
			if (TxPrintTreeIndentFactor > 0)
			{
				for (j = 1; j <= depth; j++)
					htpf("%*s", TxPrintTreeIndentFactor,
					     "");
			}
			if (!bt->datad)		/* assume inverted index */
			{
				RECID	dataRecid;

				if (len == sizeof(RECID)) /* correct */
				{
					memcpy(&dataRecid,data,sizeof(RECID));
					htpf("0x%0*wx", sizeof(RECID)*2,
					    (EPI_HUGEUINT)TXgetoff2(&dataRecid));
				}
				else		/* bogus size for inv. idx */
					htpf("%.*s", len, data);
			}
			else			/* assume regular index */
			{
				data = (char *) ivsl((byte *) data, &len);
				xtra = "";
				if (len > (unsigned long)TxPrintTreeMaxItemLen)
				{
					len = TxPrintTreeMaxItemLen;
					xtra = "...";
				}
				printf("%.*s%s", (int)len, data, xtra);
			}
                        if (TxPrintTreeVerbose > 0)
                          htpf(" hpage=0x%wx locn=0x%wx (pg 0x%wx item %d)",
                                 (EPI_HUGEUINT)p->items[i].hpage,
                                 (EPI_HUGEUINT)TXgetoff2(&p->items[i].locn),
                                 (EPI_HUGEUINT)root, i);
			printf("\n");
			if (p->items[i].hpage == root)
			{
				putmsg(MERR, Fn,
		"Page 0x%wx of B-tree %s has item %d hpage equal to itself",
					(EPI_HUGEINT)root,
					(bt->dbf ? getdbffn(bt->dbf) : "?"),
					(int)i);
				return;
			}
			printtree(bt, p->items[i].hpage, depth + 1);
		}
		p = btreleasepage(bt, root, p);
	}
}
#endif /* BTPRINT */

/*****************************************************************************/

int
vbtappenditeml(t, p, bi, sp)
BTREE *t;
BPAGE *p;
BITEMI *bi;
int sp;

/* Internal function (used by btappend()).  Adds item `bi' to end of
 * page `p' if room, setting previous item's hpage to bi->hpage
 * (i.e. we treat bi->hpage like an lpage).  `sp' is split point
 * (what % full a page should be before going to next).  Assumes item
 * is not larger than max item size for a page.  Returns 1 if ok, 0 if
 * no more room.  KNG 971015
 */
{
	EPI_OFF_T lpage;

	/* Check for free space first.  No need to attempt compactpage() since
	 * we haven't modified any item's sizes (all calls are btappend() or
	 * btupdatebm()):
	 */
	if (((int) p->freesp - (int) bi->len) <	/* free space to be too low */
	    (int) (t->pagesize - sizeof(BPAGE)) * (100 - sp) / 100 &&
	    p->count > 0)	/* at least 1 item per page */
		return (0);	/* no room; at split point */
	lpage = bi->hpage;
	bi->hpage = 0;
	additem(t, p, p->count, bi);	/* wtf did it work? */
	if (p->count == 1)
		p->lpage = lpage;
	else
		p->items[p->count - 2].hpage = lpage;
	bi->hpage = lpage;
	if (t->flags & BT_LOGOPS)
		btlogop(t, bi->len, bi->string, &bi->locn, "append", "ok");
	return (1);
}

/*****************************************************************************/

int
vbtinsert(t, locn, keysize, key, sp)
BTREE *t;	/* (in/out) B-tree to insert into */
BTLOC locn;	/* (in) location value to associate with key */
int keysize;	/* (in) size of key */
void *key;	/* (in) key to insert */
int sp;
/* Returns -1 if already exists in unique index, -2 on other/severe error,
 * 0 if ok.
 */
{
	static CONST char Fn[] = "vbtinsert";
	EPI_OFF_T x;
	int h, rc;
	BITEMI item;
	BPAGE *p;

	if (t->flags & BT_LINEAR)
		return (btappend(t, &locn, keysize, key, sp, BTBMPN));

	item.alloced = 0;
	rc=insert(t, key, keysize, t->root, &h, &item, locn, sp, 0);
	if (rc < 0) goto done;
	/* WTF invalidate cache if severe error? (vs. unique index insert) */
	if (h)					/* item belongs on higher pg*/
	{
		x = t->root;
		t->root = btgetnewpage(t);
		if (t->root == (EPI_OFF_T)(-1))
		{
			putmsg(MERR, Fn,
			       "Cannot create new root page for B-tree %s",
			       getdbffn(t->dbf));
			t->root = x;			/* restore previous */
			goto err;
		}
		t->iamdirty = 1;
		if (btsetroot(t) < 0)		/* write failed */
		{
			t->root = x;
			goto err;
		}
		if ((p = btgetpage(t, t->root)) == BPAGEPN)
		{
			btcantgetpage(Fn, t, t->root, (EPI_OFF_T)(-2), -1);
			t->root = x;
			goto err;
		}
		btdirtypage(t, t->root);
		p->count = 1;
		p->lpage = x;
		memcpy(&p->items[0], &item, sizeof(BITEM));
		p->stacktop -= item.len;
		p->freesp -= item.len;
		p->freesp -= sizeof(BITEM);
		DBGMSG(1, (999, NULL, "Freespace set to %d", p->freesp));
		memcpy((char *) p + (int) p->stacktop, item.string, item.len);
		p->items[0].vf.var.key = p->stacktop;
		p = btreleasepage(t, t->root, p);
	}
	if (item.alloced)
		item.string = TXfree(item.string);
	rc = 0;
	t->numItemsDelta++;
	goto done;

err:
	rc = -2;
done:
	if (t->flags & BT_LOGOPS)
		btlogop(t, keysize, key, &locn, "insert", (rc == 0 ? "ok" :
			(rc == -1 ? "dup" : "fail")));
	return (rc);
}

/*****************************************************************************/

#ifdef BTPRINT			/* uses printf */
void
vbtprint(t)
BTREE *t;
{
	printtree(t, t->root, 0);
}
#endif

/*****************************************************************************/

BTLOC vbtsearch(t, keysize, key, locn)
BTREE *
	t;
int
	keysize;
void *
	key;
BTLOC *
	locn;
{
	BTLOC	res;

	LINEAR_CHK(t, return (*locn), "vbtsearch");
	GotBtCmpEq = 0;
	res = search(t, key, keysize, t->root, locn);
	if (t->flags & BT_LOGOPS)
		btlogop(t, keysize, key, locn, "search",
			(TXrecidvalid2(&res) ? "ok" :
			/* BT_SEARCH_BEFORE/AFTER will always cause `res'
			 * to be invalid (failure).  Need to check if key
			 * was really found before t->search_pos said no:
			 */
			 (GotBtCmpEq ? "hit" : "miss")));
	return(res);
}

/* ------------------------------------------------------------------------ */

int
TXvbtreeChangeLocOrKey(bt, key, keySize, oldLoc, newLoc, newKey)
BTREE	*bt;		/* (in/out) B-tree to update */
void	*key;		/* (in) key to search for */
int	keySize;	/* (in) size of key */
BTLOC	oldLoc;		/* (in) old (existing) location */
BTLOC	newLoc;		/* (in) new location to set */
byte	*newKey;	/* (in, opt.) new key to set instead */
/* Changes BTLOC of `key'/`oldLoc' item to `newLoc'.
 * >>> NOTE: `newLoc'/`newKey' must have same ordering as `oldLoc',  <<<
 * >>> i.e. would have the same Metamorph token number as `oldLoc',  <<<
 * >>> and/or same sort order as `key' (if `newKey'), once all       <<<
 * >>> recids in table are changed.                                  <<<
 * >>> Otherwise B-tree may end up out of order and invalid/corrupt. <<<
 * Returns 2 if found and updated, 1 if not found, 0 on error.
 */
{
	static CONST char	fn[] = "TXvbtreeChangeLocOrKey";
	BTLOC			res;
	BTREE_SEARCH_MODE	saveSearchMode;
	int			ret, itemIndex;
	BPAGE			*page = BPAGEPN;
	EPI_OFF_T		pageOffset = -1;
	BITEM			*item;

	LINEAR_CHK(bt, return(0), fn);
	saveSearchMode = bt->search_pos;
	bt->search_pos = BT_SEARCH_FIND;
	GotBtCmpEq = 0;

	res = search(bt, key, keySize, bt->root, &oldLoc);
	if (bt->flags & BT_LOGOPS)
		btlogop(bt, keySize, key, &oldLoc, "change-loc",
			(TXrecidvalid2(&res) ? "ok" :
			/* BT_SEARCH_BEFORE/AFTER will always cause `res'
			 * to be invalid (failure).  Need to check if key
			 * was really found before t->search_pos said no:
			 */
			 (GotBtCmpEq ? "hit" : "miss")));
	if (!TXrecidvalid2(&res))		/* not found */
	{
		ret = 1;			/* ok but not found */
		goto done;
	}

	/* Get the found item and change its BTLOC.  search() should have
	 * set history at `bt->sdepth' to the found item:
	 */
	pageOffset = bt->his[bt->sdepth].page;
	itemIndex = bt->his[bt->sdepth].index;
	page = btgetpage(bt, pageOffset);
	if (page == BPAGEPN)
	{
		btcantgetpage(fn, bt, pageOffset, (EPI_OFF_T)(-1), -1);
		goto err;
	}
	if (itemIndex < 0 || itemIndex >= page->count)
	{
		putmsg(MERR, fn,
			"Internal error: out-of-bounds item %d when looking for recid 0x%wx on page 0x%wx of B-tree %s",
			itemIndex, (EPI_HUGEINT)TXgetoff2(&oldLoc),
			(EPI_HUGEINT)pageOffset, getdbffn(bt->dbf));
		goto err;
	}
	item = &page->items[itemIndex];
	/* `item->locn' should be `oldLoc', because we searched for it.
	 * Verify it though:
	 */
	if (TXrecidcmp(&item->locn, &oldLoc) != 0)
	{					/* sanity check failed */
		putmsg(MERR, fn, "Internal error: item %d is recid 0x%wx, expected 0x%wx on page 0x%wx of B-tree %s",
			itemIndex, (EPI_HUGEINT)TXgetoff2(&item->locn),
			(EPI_HUGEINT)TXgetoff2(&oldLoc), (EPI_HUGEINT)pageOffset,
			getdbffn(bt->dbf));
		goto err;
	}
	if (newKey != BYTEPN)
		memcpy(page + item->vf.var.key, newKey, keySize);
	else
		item->locn = newLoc;		/* set the new recid */
	btdirtypage(bt, pageOffset);		/* we changed the page */
	ret = 2;
	goto done;

err:
	ret = 0;
done:
	page = btreleasepage(bt, pageOffset, page);
	bt->search_pos = saveSearchMode;
	return(ret);
}

/*****************************************************************************/

BTLOC vbtgetlast(t, keysize, key)
BTREE *
	t;
size_t	*keysize;	/* (in/out) length of `*x' */
void	*key;		/* (in/out) buffer; key copied to it */
/* Returns invalid recid on EOF or error (TX_BTREE_GOT_ERROR() true on error).
 */
{
	if (t->flags & BT_LINEAR)
	{
		BTLOC loc;

		nonlinmsg(t, "vbtgetlast");
		TXsetrecid(&loc, RECID_INVALID);
		TX_BTREE_SET_ERROR(t, 1);
		return (loc);
	}
	return getlast(t, key, keysize, t->root);
}

/*****************************************************************************/

int
vbtdelete(t, btloc, keysize, key)
BTREE *t;
BTLOC btloc;
int keysize;
void *key;
/* Returns 1 if item deleted, 0 if not found, -1 on error.
 */
{
	static CONST char Fn[] = "vbtdelete";
	EPI_OFF_T x;
	BPAGE *p = NULL;
	int h, rc;

	LINEAR_CHK(t, return (0), Fn);
	x = *((EPI_OFF_T *) key);
	rc = delete(t, btloc, key, keysize, t->root, &h, 0);
	if (h && rc >= 0)			/* need to delete root page */
	{
		x = t->root;
		if ((p = btgetpage(t, x)) == BPAGEPN)
		{
			btcantgetpage(Fn, t, x, (EPI_OFF_T)(-2), -1);
			rc = -1;
		}
		else
		{
			if (p->count == 0)
			{
				t->root = p->lpage;
				t->iamdirty = 1;
				if (btsetroot(t) < 0) rc = -1;
				p = btfreepage(t, x, p);
			}
			p = btreleasepage(t, x, p);
		}
	}
	if (t->flags & BT_LOGOPS)
		btlogop(t, keysize, key, &btloc, "delete", (rc == 1 ? "ok" :
			(rc == 0 ? "miss" : "fail")));
	if (rc == 1) t->numItemsDelta--;
	return (rc);
}

/*****************************************************************************/

#ifdef TEST

main(int argc, char *argv[])
{
	BTREE *b;
	int quit = 0;
	long c = 0;
	int csize = 20;
	int psize = BT_MAXPGSZ;
	char *i;
	char s[80];
	char *filename = (char *) NULL;
	extern char *optarg;
	extern int optind;

	while ((c = getopt(argc, argv, "f:c:p:")) != -1)
	{
		switch (c)
		{
		case 'f':
			filename = optarg;
			break;
		case 'c':
			csize = atoi(optarg);
			break;
		case 'p':
			psize = atoi(optarg);
			break;
		}
	}
	c = 0;
	b = openvbtree(filename, psize, csize, BT_UNIQUE);
	if (b == (BTREE *) NULL)
	{
		putmsg(MERR + FOE, "", "Error opening btree");
		exit(TXEXIT_CANNOTOPENINPUTFILE);
	}
	while (!quit)
	{
		if (fgets(s, sizeof(s), stdin) == NULL)
		{
			quit = 1;
			break;
		}
		switch (s[0])
		{
		case 'c':
		case 'C':
			printf("Cache: hits %d misses %d ratio %d\n",
			       ch, cm, (100 * ch) / (cm + ch));
			break;
		case 'd':
		case 'D':
			i = strdup(&s[1]);
			i[strlen(i) - 1] = '\0';
			vbtdelete(b, strlen(i), i);
			i = TXfree(i);
			break;
		case 'i':
		case 'I':
			i = strdup(&s[1]);
			i[strlen(i) - 1] = '\0';
			vbtinsert(b, c++, strlen(i), i);
			i = TXfree(i);
			break;
		case 'p':
		case 'P':
			vbtprint(b);
			break;
		case 'q':
		case 'Q':
			quit = 1;
			break;
		case 's':
		case 'S':
			i = strdup(&s[1]);
			i[strlen(i) - 1] = '\0';
			printf("%d\n", vbtsearch(b, strlen(i), i));
			i = TXfree(i);
			break;
		}
	}
	b = closevbtree(b);
	return (0);
}

#endif
