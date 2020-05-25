/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
/*#include <malloc.h>*/
#include "dbquery.h"
#include "texint.h"
#include "fbtree.h"
#include "cgi.h"				/* for htpf() */

static CONST char       OutOfMem[] = "Out of memory";


static void nonlinmsg ARGS((BTREE *t, CONST char *fn));
static void
nonlinmsg(t, fn)
BTREE   *t;
CONST char  *fn;
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
/*	The default comparison function for a fixed btree.
 */

int
TXfcmp(a, al, b, bl, usr)
void *a;
size_t al;
void *b;
size_t bl;
void *usr;
{
#if (EPI_OFF_T_BITS == EPI_OS_INT_BITS)
	return((int)(*(EPI_OFF_T *)a - *(EPI_OFF_T *)b));
#else
	EPI_OFF_T	av, bv;

	av = *(EPI_OFF_T *)a;
	bv = *(EPI_OFF_T *)b;
	return(av > bv ? 1 : (av < bv ? -1 : 0));
#endif
}

/******************************************************************/
/*	The default comparison function for an unsigned fixed btree.
 */

int
TXfucmp(a, al, b, bl, usr)
void *a;
size_t al;
void *b;
size_t bl;
void *usr;
{
#if (EPI_OFF_T_BITS == EPI_OS_UINT_BITS)
#  define FUTYPE        unsigned
#elif (EPI_OFF_T_BITS == EPI_OS_ULONG_BITS)
#  define FUTYPE        unsigned long
#elif defined(HAVE_UNSIGNED_LONG_LONG) && (EPI_OFF_T_BITS == EPI_OS_ULONGLONG_BITS)
#  define FUTYPE        unsigned long long
#elif (EPI_OFF_T_BITS == EPI_HUGEUINT_BITS)
#  define FUTYPE        EPI_HUGEUINT
#else
#  define FUTYPE        u_quad_t                /* hope this works */
#endif
  FUTYPE        av, bv;

  av = (FUTYPE)(*(EPI_OFF_T *)a);
  bv = (FUTYPE)(*(EPI_OFF_T *)b);
  return(av > bv ? 1 : (av < bv ? -1 : 0));
#undef FUTYPE
}

/******************************************************************/

int
TXfixedUnsignedReverseCmp(void *a, size_t al, void *b, size_t bl, void *usr)
{
	return(-TXfucmp(a, al, b, bl, usr));
}

/******************************************************************/

static int fbtcmp ARGS((BTREE *, BPAGE *, int, void *, int));

static int
fbtcmp(bt, p, n, key, keylen)
BTREE *bt;
BPAGE *p;
int n;
void *key;
int keylen;
{
	static CONST char	Fn[] = "fbtcmp";
	EPI_OFF_T *a, *b;
	int rc;

	if ((unsigned)n >= (unsigned)p->count)
	{
		putmsg(MERR, Fn,
 "Attempt to compare non-existent item %d of %d-item page 0x%wx of B-tree %s",
		       n, (int)p->count, (EPI_HUGEUINT)btpg2off(bt, p),
		       getdbffn(bt->dbf));
		return -1;
	}
	a = &p->items[n].vf.key;
	b = (EPI_OFF_T *)key;
	rc = bt->cmp(a, sizeof(*a), b, sizeof(*b), bt->usr);
	if (rc == 0 && bt->search_pos == BT_SEARCH_BEFORE)
		return 1;
	else
		return rc;
}

/*****************************************************************************/

BTREE *
closefbtree(b)
BTREE *b;
{
	int i;

	if (b == (BTREE *)NULL)
		return b;
        if (b->flags & BT_LINEAR)
          btflushappend(b);                     /* KNG 971016 */
	if (b->cache != (BCACHE *)NULL)
	{
		for (i=0; i < b->cachesize; i++)
		{
			if (b->cache[i].dirty)
				btwritepage(b, b->cache[i].pid, b->cache[i].page);
			if (b->cache[i].page != (BPAGE *)NULL)
				b->cache[i].page = btfrpage(b, b->cache[i].page);
		}
		b->cache = TXfree(b->cache);
	}
	b->his = TXfree(b->his);
	if(b->dddirty)
	{
		if(b->datad)
		{
			if(!b->datadoff)
				b->datadoff = -1;
			b->datadoff = btwritebuf(b, b->datadoff, b->datad,
						b->datad->size);
			b->iamdirty = 1;
		}
	}
	if (b->iamdirty)
	{
		if(b->szread)
			btwritebuf(b, b->hdroff, b, b->szread);
		else
			btwritebuf(b, b->hdroff, b, sizeof(BTREES));
	}
	if (!(b->flags & BT_SHAREDBF)) closedbf(b->dbf);
	b = TXfree(b);
	return (BTREE *)NULL;
}

/*****************************************************************************/

void
fbtsetdefcmp(b)
BTREE	*b;
{
	if (b->flags & BT_UNSIGNED)
		b->cmp = TXfucmp;
	else
		b->cmp = TXfcmp;
}

/******************************************************************/


BTREE *
openfbtree(filename, order, cashsize, flags, mode, hdroff, sharedbf)
char *filename;
int order;
int cashsize;
int flags;
int mode;
EPI_OFF_T	hdroff;		/* usually 0; -1 for new B-tree same DBF */
DBF		*sharedbf;	/* non-NULL: use this DBF, shared */
{
	static CONST char fn[] = "openfbtree";
	BTREE	*b = NULL;
	void	*v;
	DBF		*dbf;
	size_t	sz;
	EPI_OFF_T	off;
        extern char *TxBtreeErr, TxBtreeErrBuf[];

        TxBtreeErr = CHARPN;

	/* see also check below: */
	if (TXApp && (TXApp->validateBtrees & 0x01))
	{
		if (!(flags & BT_FIXED))
		{
			putmsg(MERR + FRE, fn,
			       "Internal error: Fixed flag not specified for B-tree `%s' but fixed function called",
			       (b->dbf ? getdbffn(b->dbf) : "?"));
			TxBtreeErr = "Fixed flag not specified in fixed function";
			goto err;
		}
		/* `pagesize' > `order' for fbtree; see also check below: */
		if (order < 0 || (size_t)order > TX_FBTREE_MAX_ORDER)
		{
			if (filename)
				putmsg(MERR + UGE, fn,
	  "Invalid order %d specified for fixed B-tree `%s': Must be 0 to %d",
				       (int)order, filename,
				       (int)TX_FBTREE_MAX_ORDER);
			else
				putmsg(MERR + UGE, fn,
	   "Invalid order %d specified for fixed RAM B-tree: Must be 0 to %d",
				       (int)order,
				       (int)TX_FBTREE_MAX_ORDER);
			TxBtreeErr = "Invalid order specified";
			goto err;
		}
	}

	if (sharedbf != DBFPN)		/* already open, shared */
	{
		flags |= BT_SHAREDBF;
		dbf = sharedbf;
	}
	else
	{
		flags &= ~BT_SHAREDBF;
		errno = 0;
		dbf = opendbf(TXPMBUFPN, filename, mode);
	}
	if (dbf == (DBF *)NULL)                         /* WTF error msg? */
         {                                              /* try to set one */
            if (errno == 0) TxBtreeErr = "Unknown opendbf() error";
            else
              {
                TXstrncpy(TxBtreeErrBuf, strerror(errno), 128);
                TxBtreeErrBuf[128 - 2] = '\0';
                strcat(TxBtreeErrBuf, "?");             /* we're guessing */
                TxBtreeErr = TxBtreeErrBuf;
              }
            return(BTREEPN);
          }

	b = (BTREE *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(BTREE));
	if (b == BTREEPN)
	{
          if (!(flags & BT_SHAREDBF)) dbf = closedbf(dbf);
          TxBtreeErr = (char *)OutOfMem;
          return b;
	}
	b->hdroff = hdroff;
	if (b->hdroff == (EPI_OFF_T)(-1) ||		/*new B-tree same DBF*/
	    (v = (BTREE *)getdbf(dbf, b->hdroff, &sz)) == NULL)
	{
		b->magic = BTREE_MAGIC;
		b->order = order;
		b->npages = 0;
		b->root = 0;
		b->pagesize = TX_FBTREE_PAGESIZE(order);
		b->flags = (char)(flags & BT_SAVE_FLAGS);
		b->iamdirty = 0;
		(void)ddsettype(b->datad, 0);
		b->szread = 0;
		if ((off = b->hdroff) == (EPI_OFF_T)0)
			off = (EPI_OFF_T)(-1);		/* WTF for RAM dbf */
		off = putdbf(dbf, off, b, sizeof(BTREES));
		if (off == (EPI_OFF_T)(-1))
		{
			putmsg(MERR + FRE, fn,
				"Cannot write header of B-tree %s", filename);
			TxBtreeErr = "Cannot write header of B-tree";
			closefbtree(b);
			return(BTREEPN);
		}
		if (b->hdroff == (EPI_OFF_T)(-1))
			b->hdroff = off;
		else if (off != b->hdroff)
		{
			putmsg(MERR + UGE, fn,
		"Header written at unexpected offset 0x%wx (expected 0x%wx)",
				(EPI_HUGEUINT)off, (EPI_HUGEUINT)b->hdroff);
			TxBtreeErr = "Header written at unexpected offset";
			closefbtree(b);
			return(BTREEPN);
		}				
	}
	else
	{
		b->szread = sz;
		b->dbf = dbf;
		if(b->datadoff)
		{
			b->datad = getdbf(b->dbf, b->datadoff, &sz);
			b->datad = convertdd(b->datad, sz);
		}
                b->flags &= BT_SAVE_FLAGS;      /*no BT_LINEAR etc from file*/
                if (flags & BT_LINEAR)
                  {
                    putmsg(MERR + UGE, "openfbtree",
                      "Cannot set linear mode on existent tree %s", filename);
                    closefbtree(b);
                    TxBtreeErr = "Cannot set linear mode on existent tree";
                    return(BTREEPN);
                  }
		memcpy(b, v, sizeof(BTREES));
	}
	(void)ddsettype(b->datad, 0);
	b->dbf = dbf;

	/* Add back BT_LINEAR etc. non-DBF-saved flags.
	 * Since BT_EXCLUSIVEACCESS must always be in-sync with its
	 * corresponding DBF ioctls, leave it off of `b->flags' until
	 * ioctlbtree() sets it (simultaneously with its DBF ioctls):
	 */
        b->flags |= (char)(flags & ~(BT_SAVE_FLAGS | BT_EXCLUSIVEACCESS));

	if (cashsize)
		b->cachesize = cashsize; /* cachesize can be varied */
	if (b->magic != BTREE_MAGIC)
	{
          if (!(flags & BT_SHAREDBF)) closedbf(dbf);
          b = TXfree(b);
          TxBtreeErr = "Bad magic number (corrupt file?)";
          return (BTREE *)NULL;
	}
	/* sanity check; see also check above: */
	if (TXApp && (TXApp->validateBtrees & 0x01))
	{
		if (!(b->flags & BT_FIXED))
		{
			/* This may not be fatal long-term, as we'll
			 * call the right insert/del funcs based on
			 * the loaded B-tree's BT_FIXED flag.  But it
			 * may flummox our `order'/`pagesize' sanity
			 * checks here, and we might not init the rest
			 * of the tree properly here:
			 */
			putmsg(MERR + UGE, fn,
		      "Variable B-tree `%s' cannot be opened as fixed B-tree",
			       (b->dbf ? getdbffn(b->dbf) : "?"));
			TxBtreeErr = "Variable B-tree opened as fixed B-tree";
			goto err;
		}
		if (b->order < 0 || (size_t)b->order > TX_FBTREE_MAX_ORDER)
		{
			putmsg(MERR + FRE, fn,
		    "Corrupt B-tree `%s': Invalid order %d: Must be 0 to %d",
			       (b->dbf ? getdbffn(b->dbf) : "?"),
			       (int)b->order, (int)TX_FBTREE_MAX_ORDER);
			TxBtreeErr = "Invalid order";
			goto err;
		}
		if ((size_t)b->pagesize != TX_FBTREE_PAGESIZE(b->order))
		{
			/* Maybe this could be less stringent; allow
			 * pagesize small enough to just fit `order'
			 * items, not ~2*order as per TX_FBTREE_PAGESIZE()?
			 */
			putmsg(MERR + FRE, fn,
"Corrupt B-tree `%s': Pagesize %d invalid for order %d (expected pagesize %d)",
			       (b->dbf ? getdbffn(b->dbf) : "?"),
			       (int)b->pagesize, (int)b->order,
			       (int)TX_FBTREE_PAGESIZE(b->order));
			TxBtreeErr = "Pagesize invalid for order";
			goto err;
		}
	}

	if(
	   (b->cache = (BCACHE *)TXcalloc(TXPMBUFPN, fn, b->cachesize, sizeof(BCACHE)))
		     ==(BCACHE *)NULL ||
	   (b->his = (BTRL *)TXcalloc(TXPMBUFPN, fn, b->cachesize, sizeof(BTRL)))
		   ==(BTRL *)NULL
	   )
          {
            b = closefbtree(b);
            TxBtreeErr = (char *)OutOfMem;
            return(BTREEPN);
          }

	/* Use pre/postbufsz for efficiency.  See also [kdbf_]open{f,v}btree.
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
	if (TXbtgetoptimize() & 0x1)
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
	b->cacheused = 0;
	btsetdefcmp(b);
	return b;
err:
	return(closefbtree(b));
#undef ALIGN_UP
#undef ASZ
}

/*****************************************************************************/

static BTLOC search ARGS((BTREE *, char *, int, EPI_OFF_T, BTLOC *));

static BTLOC
search(bt, x, len, root, locn)
BTREE *bt;
char *x;
int len;
EPI_OFF_T root;
BTLOC *locn;
{
	static CONST char	Fn[] = "[fbt]search";
	BPAGE	*p;
	int	l, r, i, c, tv = 1;
	BTLOC	rc;

	if (root == bt->root)
		bt->cdepth = 0;
	else
		bt->cdepth++;
	bt->sdepth = bt->cdepth;		/* see vbtree.c comment */

	if (root == (EPI_OFF_T)0)		/* empty tree (iff root) */
	{
		TXsetrecid(&rc, (EPI_OFF_T)(-1));
	}
	else if (bt->cdepth >= bt->cachesize)	/* real bad */
	{
		putmsg(MERR, Fn,
		 "History exceeded for B-tree %s (cdepth %d >= cachesize %d)",
		       getdbffn(bt->dbf), bt->cdepth, bt->cachesize);
		TXsetrecid(&rc, (EPI_OFF_T)(-1));
	}
	else if ((p = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root,
			      (EPI_OFF_T)(root == bt->root ? -2 : -1), -1);
		TXsetrecid(&rc, (EPI_OFF_T)(-1));
	}
	else
	{
		l = 0;
		r = p->count;
		while (l < r)			/* binary search the page */
		{
			i = (l+r)/2;
			c = fbtcmp(bt, p, i, x, len);
			if (c == 0)		/* item `i' matches our key */
			{
				if (TXrecidvalid(locn))
				{		/* secondary key is `locn' */
					c = _recidcmp(&p->items[i].locn, locn);
				}
				else if ((r-l) > 1)
					c = 1;	/* 1st if multiple possible */
			}
			if (c > 0)		/* item `i' > key(+locn) */
				r = i;
			else			/* item `i' <= key(+locn) */
				l = i+1;
		}
		if (!TXrecidvalid(locn))	/* Find first matching */
			while (r > 1&&((tv = fbtcmp(bt, p, r-2, x, len)) == 0))
				r--;
		bt->his[bt->cdepth].page = root;
		bt->his[bt->cdepth].index = r-(tv?1:0);

		/* We've found the key(+locn), or the spot it should be,
		 * on this page.  Either way, search the sub-tree (if any)
		 * for key(+locn):
		 */
		if (r == 0 || r == 1)		/* [r-1].hpage is p->lpage */
		{
			if (p->lpage != (EPI_OFF_T)0 &&
			    (r == 0 || fbtcmp(bt, p, 0, x, len) == 0))
			{
				bt->his[bt->cdepth].index = -1;
				rc = search(bt, x, len, p->lpage, locn);
			}
			else
				TXsetrecid(&rc, (EPI_OFF_T)(-1));
		}
		else
			if (p->items[r-2].hpage != (EPI_OFF_T)0 && tv == 0)
				rc = search(bt, x, len, p->items[r-2].hpage, locn);
			else
				TXsetrecid(&rc, (EPI_OFF_T)(-1));
		if (TXrecidvalid(&rc))		/* found on a deeper page */
		{
			p = btreleasepage(bt, root, p);
			if (bt->cdepth > 0) bt->cdepth--;
			return rc;
		}
		if (r==0) r++;
		while (r <= p->count && (tv = fbtcmp(bt, p, r-1, x, len)) <= 0)
		{
			if (tv == 0)
			{
				BTLOC	ret;

				bt->his[bt->cdepth].index = r-1;
				ret = p->items[r-1].locn;
				p = btreleasepage(bt, root, p);
				bt->sdepth = bt->cdepth;
				if (bt->cdepth > 0) bt->cdepth--;
				return(ret);
			}
			if (p->items[r-1].hpage != (EPI_OFF_T)0)
			{
				if(r == p->count)
					bt->his[bt->cdepth].index = r;
				else
					bt->his[bt->cdepth].index = r-1;
				rc = search(bt, x, len, p->items[r-1].hpage, locn);
				if (TXrecidvalid(&rc))
				{
					p = btreleasepage(bt, root, p);
					if (bt->cdepth > 0) bt->cdepth--;
					return rc;
				}
			}
			else
				TXsetrecid(&rc, (EPI_OFF_T)-1);
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
BTREE	*bt;
char	*x;	/* (in/out) buffer; key copied to it */
size_t	*len;	/* (in/out) length of `*x' */
EPI_OFF_T	root;
/* Gets the last item from tree.  Called by fbtgetlast().
 * Returns recid, or -1 if not found (empty tree) or error
 * (TX_BTREE_GOT_ERROR() will be true on error).
 */
{
	static CONST char Fn[] = "[fbt]getlast";
	BPAGE	*p = NULL;
	int	r;
	BTLOC	rc;
	BITEM	*item;

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
		bt->his[bt->cdepth].page = root;	/* KNG set page too */
		bt->his[bt->cdepth].index = p->count - 1;
		if ((r = p->count) <= 0)	/* sanity check */
		{
			putmsg(MERR, Fn,
			       "Encountered %d-item page 0x%wx in B-tree %s",
			       r, (EPI_HUGEUINT)root, getdbffn(bt->dbf));
			goto err;
		}
		else if (p->items[r-1].hpage != (EPI_OFF_T)0)
		{
			bt->his[bt->cdepth].index = r;
			rc = getlast(bt, x, len, p->items[r-1].hpage);
		}
		else
		{
			item = &p->items[r-1];
#define KEY_SZ	sizeof(item->vf.key)
			if (*len < KEY_SZ)
			{
				putmsg(MERR + MAE, Fn,
				       "Buffer size %d too small for %d", 
				       (int)(*len), (int)KEY_SZ);
				goto err;
			}
			memcpy(x, &item->vf.key, KEY_SZ);
			*len = KEY_SZ;
#undef KEY_SZ
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
fbtgetnext(bt, len, x, pdataptr)
BTREE	*bt;		/* (in/out) the B-tree */
size_t	*len;		/* (in/out, opt.; req. if `x') length of `x' buffer */
void	*x;		/* (in/out, opt.) buffer to copy key to */
byte	**pdataptr;	/* (out, opt.) pointer to actual key data on page */
/* Returns invalid recid on EOF or error; TX_BTREE_GOT_ERROR() true on error.
 */
{
	static CONST char Fn[] = "fbtgetnext";
	BPAGE *p = NULL;
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
	do {   				 /* Figure out which page to look at */
		page = xpage;
		if ((p = btgetpage(bt, page)) == BPAGEPN)
		{
			btcantgetpage(Fn, bt, page, (EPI_OFF_T)(-1), -1);
			goto err;
		}
		if (index == -1)		/* `index' is p->lpage */
		{
			xpage = p->lpage;
			if (xpage != (EPI_OFF_T)0)
			{			/* go left and deeper */
				index = -1;
				level ++;
				bt->his[level].page = xpage;
				bt->his[level].index = -1;
			}
			else			/* go right on this page */
			{
				bt->his[level].index++;
				index++;
			}
		}
		else if (index < p->count)	/* `index' is on the page */
		{
			item = &p->items[index];
			if (f)			/* stop, return this item */
				xpage = (EPI_OFF_T)0;
			else			/* keep looking */
			{
				xpage = item->hpage;
				if (xpage != (EPI_OFF_T)0)
				{		/* go right and deeper */
					level ++;
					index = -1;
					bt->his[level].page = xpage;
					bt->his[level].index = -1;
				}		/*else no deeper pg to right*/
				else if (index < p->count-1) /*not last item*/
				{		/* go right on this page */
					index ++;
					bt->his[level].index++;
				}		/* else last item */
				else if (level > 0)	/* not root page */
				{		/* back up a page, go right */
					level --;
					xpage = bt->his[level].page;
					index = ++bt->his[level].index;
					f = 1;	/* return next item */
				}
				else		/* last item, root page */
				/* KNG 011113 used to be:
				 *  xpage = bt->his[level].page;
				 *  index = ++bt->his[level].index;
				 *  f = 1;
				 */
					goto atEof;	/* nothing more */
			}
		}
		else				/* `index' is off the page */
		{
			if (level > 0)		/* not root page */
			{			/* back up a page, go right */
				level --;
#ifdef NONDEF
				fprintf(stderr, "Level: %d Index: %d\n", level, bt->his[level].index);
#endif
				xpage = bt->his[level].page;
				index = ++bt->his[level].index;
				f = 1;		/* return next item */
			}
			else			/* root page: all done */
				goto atEof;
		}
		p = btreleasepage (bt, page, p);
	} while (xpage != (EPI_OFF_T)0);
	bt->sdepth = level;			/* save for next call */
	if ((p = btgetpage(bt, page = bt->his[level].page)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, page, (EPI_OFF_T)(-1), -1);
		goto err;
	}
	item = &p->items[i = bt->his[level].index];
#define KEY_SZ	sizeof(item->vf.key)
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
		memcpy(x, &item->vf.key, KEY_SZ);
	if (pdataptr)
		*pdataptr = (byte *)&item->vf.key;
	if (len)
		*len = (size_t)KEY_SZ;
#undef KEY_SZ
	btloc = item->locn;
	goto finally;

err:
	TX_BTREE_SET_ERROR(bt, 1);
atEof:
	TXsetrecid(&btloc, RECID_INVALID);	/* EOF or error */
	if (len) *len = 0;
	/* do not clear/terminate `x': wtf some callers assume untouched? */
finally:
	p = btreleasepage(bt, page, p);
	return btloc;
}

/*****************************************************************************/

static int insert ARGS((BTREE *bt, char *x, int len, EPI_OFF_T root, int *h,
			BITEM *item, BTLOC locn));

static int
insert (bt, x, len, root, h, item, locn)
BTREE *bt;
char *x;
int len;
EPI_OFF_T root;
int *h;
BITEM *item;
BTLOC locn;
/* Returns -1 if already exists in unique index, -2 on severe error,
 * 0 if ok.
 */
{
	static CONST char Fn[] = "[fbt]insert";
	BPAGE	*p, *b;
	BITEM	item1;
	int	l, r, i, c, rc;
	EPI_OFF_T	bn;
	size_t	tomove;

	if (root == (EPI_OFF_T)0)		/* empty (sub)tree */
	{
		*h = 1;
		item->vf.key = *(EPI_OFF_T *)x;
		item->locn = locn;
		item->hpage = (EPI_OFF_T)0;
	}
	else
	{
		if ((p = btgetpage(bt, root)) == BPAGEPN)
		{
			btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
			rc = -2;
			goto bailnopg;
		}
		l = 0;
		r = p->count;
		while (l<r)			/* binary search this page */
		{
			i = (l+r)/2;

			if ((c = fbtcmp(bt, p, i, x, len)) == 0)
				if(!(bt->flags & BT_UNIQUE))
					c = _recidcmp(&p->items[i].locn, &locn);
			if (c > 0)		/* item `i' > key `x' */
				r = i;
			else			/* item `i' <= key `x' */
				l = i+1;
		}
		if (r > 0 && (fbtcmp(bt, p, r-1, x, len) == 0))
			/* Item Already Exists */
		{
			if (bt->flags & BT_UNIQUE)
			{
				rc = -1;	/* already exists */
			bail:
				p = btreleasepage(bt, root, p);
			bailnopg:
				*h = 0;
				return rc;
			}
			else
			{
				if ((rc = insert(bt, x, len, p->items[r-1].hpage, h, &item1, locn)) < 0)
					goto bail;
			}
		}
		else
		{
			if (r == 0)
			{
				if ((rc = insert(bt, x, len, p->lpage, h, &item1, locn)) < 0)
					goto bail;
			}
			else
				if ((rc = insert(bt, x, len, p->items[r-1].hpage, h, &item1, locn)) < 0)
					goto bail;
		}
		if (*h == 1)
		{
			btdirtypage(bt, root);
			if (p->count < 2 * bt->order)
			{
				*h = 0;
				tomove = (p->count++ - r) * sizeof(BITEM);
				if (tomove > 0)
					memmove(&p->items[r+1], &p->items[r], tomove);
				p->items[r] = item1;
			}
			else
			{
				if ((bn = btgetnewpage(bt)) == (EPI_OFF_T)(-1))
				{
					putmsg(MERR + FWE, Fn,
					"Cannot create new page in B-tree %s",
					getdbffn(bt->dbf));
					rc = -2;
					goto bail;
				}
				if ((b = btgetpage(bt, bn)) == BPAGEPN)
				{
					btcantgetpage(Fn, bt, bn, (EPI_OFF_T)(-1), -1);
					rc = -2;
					goto bail;
				}
				btdirtypage(bt, bn);
				if (r <= bt->order)
				{
					if (r == bt->order)
						*item = item1;
					else
					{
						*item = p->items[bt->order-1];
						for (i = bt->order-1; i > r; i--)
							p->items[i] = p->items[i-1];
						p->items[r] = item1;
					}
					for (i = 0; i < bt->order; i++)
						b->items[i] = p->items[i+bt->order];
				}
				else
				{
					r -= (bt->order + 1);
					*item = p->items[bt->order];
					for (i = 0; i < r; i++)
						b->items[i] = p->items[i+1+bt->order];
					b->items[r] = item1;
					for (i = r+1; i < bt->order; i++)
						b->items[i] = p->items[i+bt->order];
				}
				p->count = b->count = bt->order;
				b->lpage = item->hpage;
				item->hpage = bn;
				b = btreleasepage(bt, bn, b);
			}
		}
		p = btreleasepage(bt, root, p);
	}
	return 0;
}

/*****************************************************************************/

static int underflow ARGS((BTREE *bt, BPAGE *c, EPI_OFF_T ap, int s, int *h));

static int
underflow(bt, c, ap, s, h)
BTREE *bt;
BPAGE *c;
EPI_OFF_T ap;
int s;
int *h;
/* Returns -1 on error, 0 if ok.
 */
{
	static CONST char Fn[] = "[fbt]underflow";
	BPAGE *a = NULL, *b = NULL;
	int i, k, mb, mc, n;
	EPI_OFF_T bp;

	if ((a = btgetpage(bt, ap)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, ap, (EPI_OFF_T)(-1), -1);
		return(-1);
	}
	n = bt->order;
	mc = c->count;
	if (s < (mc - 1))			/* `s' not last item on `c' */
	{
		s++;
		bp = c->items[s].hpage;
		if ((b = btgetpage(bt, bp)) == BPAGEPN)
		{
		bail1:
			btcantgetpage(Fn, bt, bp, btpg2off(bt, c), s);
			a = btreleasepage(bt, ap, a);
			return(-1);
		}
		btdirtypage(bt, ap);
		/* WTF dirty page `c' */
		mb = b->count;
		k = (mb-n+1)/2;
		a->items[n-1] = c->items[s];
		a->items[n-1].hpage = b->lpage;
		if (k > 0)
		{
			btdirtypage(bt, bp);
			for (i = 0; i < k-1; i++)
				a->items[i+n] = b->items[i];
			c->items[s] = b->items[k-1];
			c->items[s].hpage = bp;
			b->lpage = b->items[k-1].hpage;
			mb -= k;
			for (i = 0; i < mb; i++)
				b->items[i] = b->items[k+i];
			b->count = mb;
			a->count = n - 1 + k;
			*h = 0;
		}
		else
		{
			for (i = 0; i < n; i ++)
				a->items[i+n] = b->items[i+k];
			for (i = s; i < mc - 1; i++)
				c->items[i] = c->items[i + 1];
			a->count = 2 * n;
			c->count = mc -1;
			*h = mc <= n;
			b = btfreepage(bt, bp, b);
		}
	}
	else					/* `s' is last item on `c' */
	{
		if (s == 0)
			bp = c->lpage;
		else
			bp = c->items[s-1].hpage;
		if ((b = btgetpage(bt, bp)) == BPAGEPN)
		{
			s--;
			goto bail1;
		}
		btdirtypage(bt, bp);
		/* WTF dirty page `c' */
		mb = b->count + 1;
		k = (mb - n)/2;
		if (k > 0)
		{
			btdirtypage(bt, ap);
#ifdef NEVER
			for(i=n-1; i >= 0; i--)
				a->items[i+k] = a->items[i];
			a->items[k] = c->items[s];
			a->items[k].hpage = a->lpage;
			mb -= k;
			for (i = k-1; i >= 0; i--)
				a->items[i] = b->items[i+mb];
			a->lpage = b->items[mb].hpage;
			c->items[s] = b->items[mb];
#else
			for(i=n-2; i >= 0; i--)
				a->items[i+k] = a->items[i];
			a->items[k-1] = c->items[s];
			a->items[k-1].hpage = a->lpage;
			mb -= k;
			for (i = k-2; i >= 0; i--)
				a->items[i] = b->items[i+mb];
			a->lpage = b->items[mb-1].hpage;
			c->items[s] = b->items[mb-1];
#endif
			c->items[s].hpage = ap;
			b->count = mb - 1;
			a->count = n - 1 + k;
			*h = 0;
		}
		else
		{
			b->items[mb-1] = c->items[s];
			b->items[mb-1].hpage = a->lpage;
			for (i=0; i < n-1; i++)
				b->items[i+mb] = a->items[i];
			b->count = 2 * n;
			c->count = mc -1;
			*h = mc <= n;
			a = btfreepage(bt, ap, a);
		}
	}
	a = btreleasepage(bt, ap, a);
	b = btreleasepage(bt, bp, b);
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
/* Returns -1 on error, 0 if ok.
 */
{
	static CONST char	Fn[] = "[fbt]del";
	BPAGE *p;
	EPI_OFF_T q;
	int n, rc = 0;

	n = bt->order;
	if ((p = btgetpage(bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
		return(-1);
	}
	btdirtypage(bt, root);			/* WTF underflow() won't */
	q = p->items[p->count-1].hpage;
	if (q != (EPI_OFF_T)0)
	{
		if (del(bt, q, h, a, r) < 0)	/* error */
			rc = -1;
		else if (*h)
			rc = underflow(bt, p, q, p->count-1, h);
	}
	else
	{
		p->count --;
		p->items[p->count].hpage = a->items[r].hpage;
		/* WTF dirty page `a' */
		a->items[r] = p->items[p->count];
		*h = p->count < n;
	}
	p = btreleasepage(bt, root, p);
	return(rc);
}

/*****************************************************************************/

static int delete ARGS((BTREE *bt, BTLOC btloc, char *x, int len,
			EPI_OFF_T root, int *h));

static int
delete(bt, btloc, x, len, root, h)
BTREE *bt;
BTLOC btloc;
char *x;
int len;
EPI_OFF_T root;
int *h;
/* Returns 1 if deleted, 0 if not found, -1 on error.
 */
{
	static CONST char	Fn[] = "[fbt]delete";
	EPI_OFF_T q;
	int c, i, l, n, r, rc = 0;
	BPAGE *a;

	n = bt->order;
	if (root == (EPI_OFF_T)0)			/* empty (sub)tree */
		*h = 0;
	else if ((a = btgetpage (bt, root)) == BPAGEPN)
	{
		btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
		return(-1);
	}
	else
	{
		l = 0;
		r = a->count;
		while (l < r)
		{
			i = (l+r)/2;
			c = fbtcmp(bt, a, i, x, len);
			if(c == 0 && TXrecidvalid(&btloc))
				c = _recidcmp(&a->items[i].locn, &btloc);
			if (c >= 0)
				r = i;
			else
				l = i+1;
		}
		if (r==0)
			q = a->lpage;
		else
			q = a->items[r-1].hpage;
		if ((r < a->count) && (fbtcmp(bt, a, r, x, len) == 0) &&
			(!_recidcmp(&a->items[r].locn, &btloc) || !TXrecidvalid(&btloc)))
		{
                        rc = 1;                         /* KNG found it */
			if (q == 0)
			{
				btdirtypage(bt, root);
				a->count --;
				*h = a->count<n;
#ifdef NEVER
				putmsg(999, NULL, "Deleting Recid %d",TXgetoff(&a->items[r].locn));
#endif
				for (i = r; i < a->count; i++)
					a->items[i] = a->items[i+1];
			}
			else
			{
#ifdef NEVER
				putmsg(999, NULL, "Deleting Recid %d",TXgetoff(&a->items[r].locn));
#endif
				btdirtypage(bt, root); /* WTF del() & underflow() won't */
				if (del(bt, q, h, a, r) < 0)	/* error */
					rc = -1;
				else if (*h)
				{
					if (underflow(bt, a, q, r-1, h) < 0)
						rc = -1;
				}
			}
		}
		else
		{
                        rc = delete(bt, btloc, x, len, q, h);
			if (rc >= 0 && *h)
			{
				btdirtypage(bt, root); /* WTF underflow won't */
				underflow(bt, a, q, r-1, h);
			}
		}
		a = btreleasepage(bt, root, a);
	}
        return(rc);
}

/*****************************************************************************/

#ifdef BTPRINT
extern int TxPrintTreeIndentFactor;

static void printtree ARGS((BTREE *, EPI_OFF_T, int));

static void
printtree(bt, root, depth)
BTREE *bt;
EPI_OFF_T root;
int depth;
{
	static CONST char	Fn[] = "[fbt]printtree";
	BPAGE *p;
	int i, j;

	if (root != (EPI_OFF_T)0)
	{
		if ((p = btgetpage(bt, root)) == BPAGEPN)
		{
			btcantgetpage(Fn, bt, root, (EPI_OFF_T)(-1), -1);
			return;
		}
		printtree(bt, p->lpage, depth + 1);
		for (i=0; i < p->count; i++)
		{
			if (TxPrintTreeIndentFactor > 0)
			{
				for (j = 1; j <= depth; j++)
					htpf("%*s", TxPrintTreeIndentFactor,
					     "");
			}
			htpf("%.3wd:%.3wd ",(EPI_HUGEINT)p->items[i].vf.key,
				(EPI_HUGEINT)TXgetoff(&p->items[i].locn));
			htpf("\n");
			printtree(bt, p->items[i].hpage, depth+1);
		}
		p = btreleasepage(bt, root, p);
	}
}
#endif /* BTPRINT */

/*****************************************************************************/

int
fbtappenditeml(t, p, bi, sp)
BTREE   *t;
BPAGE   *p;
BITEMI  *bi;
int     sp;
/* Internal function (used by btappend()).  Adds item `bi' to end of
 * page `p' if room, setting previous item's hpage to bi->hpage
 * (i.e. we treat bi->hpage like an lpage).  `sp' is split point;
 * ignored (used in vbtree).  Returns 1 if ok, 0 if no more room.
 * KNG 971015
 */
{
  BITEM *it;

  if (p->count >= (t->order << 1)) return(0);           /* no room in page */

  it = p->items + p->count;
  it->hpage = 0;
  it->locn = bi->locn;
  memcpy(&it->vf.key, bi->string, sizeof(EPI_OFF_T));   /* fbtinsert checks len */
  p->count += 1;
  if (p->count == 1)
    p->lpage = bi->hpage;
  else
    it[-1].hpage = bi->hpage;
  return(1);
}

/*****************************************************************************/

int
fbtinsert(t, locn, keysize, key)
BTREE *t;
BTLOC locn;
int keysize;
void *key;
/* Returns -1 on error (already exists in unique index, or other error),
 * 0 if ok.
 */
{
	static CONST char Fn[] = "fbtinsert";
	EPI_OFF_T	x;
	int	h, rc;
	BITEM	item;
	BPAGE	*p;

	if (keysize != sizeof(EPI_OFF_T))
	{
		putmsg(MERR, "fbtinsert", "Unexpected keysize %d", keysize);
		return -1;
	}
        if (t->flags & BT_LINEAR)
          return(btappend(t, &locn, keysize, key, 100, BTBMPN));
	rc = insert(t, key, keysize, t->root, &h, &item, locn);
	if (rc < 0)
		return rc;
	/* WTF invalidate cache if severe error? (vs. unique index insert) */
	if (h)		/* make new root page, with lpage -> old root */
	{
		x = t->root;
		if ((t->root = btgetnewpage(t)) == (EPI_OFF_T)(-1))
		{
			putmsg(MERR + FWE, Fn,
			       "Cannot create new root page for B-tree %s",
			       getdbffn(t->dbf));
			t->root = x;
			return(-1);
		}
		if ((p = btgetpage(t, t->root)) == BPAGEPN)
		{
			btcantgetpage(Fn, t, t->root, (EPI_OFF_T)(-2), -1);
			t->root = x;
			return(-1);
		}
		t->iamdirty = 1;
		btdirtypage(t, t->root);
		p->count = 1;
		p->lpage = x;
		p->items[0] = item;
		p = btreleasepage(t, t->root, p);
	}
	t->numItemsDelta++;
	return 0;
}

/*****************************************************************************/

#ifdef BTPRINT
void
fbtprint(t)
BTREE *t;
{
        LINEAR_CHK(t, return, "fbtprint");
	printtree(t, t->root, 0);
}
#endif

/*****************************************************************************/

BTLOC
fbtsearch(t, keysize, key, locn)
BTREE *t;
int keysize;
void *key;
BTLOC *locn;
{
        LINEAR_CHK(t, return(*locn), "fbtsearch");
	return search(t, key, keysize, t->root, locn);
}

/* ------------------------------------------------------------------------ */

int
TXfbtreeChangeLocOrKey(bt, key, keySize, oldLoc, newLoc, newKey)
BTREE	*bt;		/* (in/out) B-tree to update */
void	*key;		/* (in) key to search for */
int	keySize;	/* (in) size of key */
BTLOC	oldLoc;		/* (in) old (existing) location */
BTLOC	newLoc;		/* (in) new location to set */
byte	*newKey;	/* (in) new key to set instead */
/* Changes BTLOC of `key'/`oldLoc' item to `newLoc'.
 * >>> NOTE: `newLoc'/`newKey' must have same ordering as `oldLoc',  <<<
 * >>> i.e. would have the same Metamorph token number as `oldLoc',  <<<
 * >>> and/or same sort order as `key' (if `newKey'), once all       <<<
 * >>> recids in table are changed.                                  <<<
 * >>> Otherwise B-tree may end up out of order and invalid/corrupt. <<<
 * Returns 2 if found and updated, 1 if not found, 0 on error.
 */
{
	static CONST char	fn[] = "TXfbtreeChangeLoc";
	BTLOC			res;
	BTREE_SEARCH_MODE	saveSearchMode;
	int			ret, itemIndex;
	BPAGE			*page = BPAGEPN;
	EPI_OFF_T		pageOffset = (EPI_OFF_T)(-1);
	BITEM			*item;

	LINEAR_CHK(bt, return(0), fn);
	saveSearchMode = bt->search_pos;
	bt->search_pos = BT_SEARCH_FIND;	/* we look for exact recid */

	res = search(bt, key, keySize, bt->root, &oldLoc);
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
	if (TXrecidcmp(&item->locn, &oldLoc) != 0)
	{
		putmsg(MERR, fn, "Internal error: item %d is recid 0x%wx, expected 0x%wx on page 0x%wx of B-tree %s",
			itemIndex, (EPI_HUGEINT)TXgetoff2(&item->locn),
			(EPI_HUGEINT)TXgetoff2(&oldLoc), (EPI_HUGEINT)pageOffset,
			getdbffn(bt->dbf));
		goto err;
	}
	if (newKey != BYTEPN)
		memcpy(&item->vf.key, newKey, sizeof(EPI_OFF_T));
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

BTLOC
fbtgetlast(t, keysize, key)
BTREE	*t;
size_t	*keysize;	/* (in/out) length of `*x' */
void	*key;		/* (in/out) buffer; key copied to it */
/* Returns invalid recid on EOF or error (TX_BTREE_GOT_ERROR() true on error).
 */
{
        if (t->flags & BT_LINEAR)
          {
            BTLOC       loc;

            nonlinmsg(t, "fbtgetlast");
            TXsetrecid(&loc, RECID_INVALID);
	    TX_BTREE_SET_ERROR(t, 1);
            return(loc);
          }
	return getlast(t, key, keysize, t->root);
}

/*****************************************************************************/

int
fbtdelete(t, btloc, keysize, key)
BTREE *t;
BTLOC btloc;
int keysize;
void *key;
/* Returns 1 if deleted, 0 if not found, -1 on error.
 */
{
	static CONST char Fn[] = "fbtdelete";
	EPI_OFF_T	x;
	BPAGE	*p = NULL;
	int	h, rc;

        LINEAR_CHK(t, return(0), "fbtdelete");
	x = *((EPI_OFF_T *)key);
	rc = delete(t, btloc, key, keysize, t->root, &h);
	if (rc >= 0 && h)			/* need to delete root page */
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
				t->root = p->lpage;	/* new root */
				t->iamdirty = 1;
				p = btfreepage(t, x, p);
			}
			p = btreleasepage(t, x, p);
		}
	}
	if (rc == 1) t->numItemsDelta--;
        return(rc);
}

/*****************************************************************************/

#ifdef TEST

main(argc, argv)
{
	BTREE *b;
	BITEM item;
	BPAGE *p;
	int quit = 0;
	int h;
	EPI_OFF_T x;
	EPI_OFF_T c = 0;
	char *i;
	char s[80];

	b = openfbtree("fixed.btree", 2, 20, BT_UNIQUE | BT_FIXED);
	while (!quit)
	{
		if (fgets(s, sizeof(s), stdin) == NULL)
		{
			quit = 1;
			break;
		}
		c++;
		switch (s[0])
		{
		case 'd' :
		case 'D' :
			i = strdup(&s[1]);
			if (!i)
			{
				putmsg(MWARN+MAE, NULL, OutOfMem);
				break;
			}
			i[strlen(i)-1] = '\0';
			x=atol(i);
			fbtdelete(b, sizeof(x), (void *)x);
			i = TXfree(i);
			break;
		case 'i' :
		case 'I' :
			i = strdup(&s[1]);
			if (!i)
			{
				putmsg(MWARN+MAE, NULL, OutOfMem);
				break;
			}
			i[strlen(i)-1] = '\0';
			x = atol(i);
			fbtinsert(b, c, sizeof(x), (void *)x);
			i = TXfree(i);
			break;
		case 'p' :
		case 'P' :
			fbtprint(b);
			break;
		case 'q' :
		case 'Q' :
			quit = 1;
			break;
		case 's' :
		case 'S' :
			i = strdup(&s[1]);
			if (!i)
			{
				putmsg(MWARN+MAE, NULL, OutOfMem);
				break;
			}
			i[strlen(i)-1] = '\0';
			fbtsearch(b, strlen(i), i);
			i = TXfree(i);
			break;
		}
	}
	closefbtree(b);
}

#endif /* TEST */
