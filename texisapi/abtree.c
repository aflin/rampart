/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef MSDOS
# include <stddef.h>
# include <io.h>
#else
# include <unistd.h>
# include <signal.h>
# include <sys/wait.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>				/* for floor() */
#include "dbquery.h"
#include "texint.h"
#ifdef NEVER
#include "os.h"
#include "mmsg.h"
#include "fld.h"
#include "dbtable.h"
#include "btree.h"
#endif
#include "fbtree.h"
#include "vbtree.h"
#include "cgi.h"				/* for htsnpf() */

#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#endif

#ifndef BPAGEPN
#  define BPAGEPN       ((BPAGE *)NULL)
#endif

int     TxPrintTreeVerbose = 0, TxPrintTreeMaxItemLen = MAXINT;
int     TxPrintTreeIndentFactor = 4;
char    *TxBtreeErr = CHARPN;           /* error from last openbtree() */
char    TxBtreeErrBuf[128];

static CONST char       BtreeExt[] = ".btr";
#define BTREE_EXT_SZ    4

/* btreeoptimize:
 * bit 0:  use pre/postbufsz in fbtree
 * bit 1:  use pre/postbufsz in vbtree
 * Set/get with TXbtsetoptimize()/TXbtgetoptmize()
 * See also kdbf_setoptimize()/kdbf_getoptmize()
 */
#define BTREE_OPTIMIZE_ALL      0x3
#define BTREE_OPTIMIZE_DEFAULT  BTREE_OPTIMIZE_ALL
static int      TXbtreeOptimize = BTREE_OPTIMIZE_DEFAULT;

static CONST char	stringCompareModeStr[] = "stringcomparemode";
#define STRINGCOMPAREMODESTR_SZ	(sizeof(stringCompareModeStr) - 1)
static CONST char	indexValuesStr[] = "indexvalues";
#define INDEXVALUESSTR_SZ	(sizeof(indexValuesStr) - 1)
static CONST char	WhiteSpace[] = " \t\r\n\v\f";
static CONST char	IndexFieldSep[] = " ,";

/************************************************************************/

int
TXbtgetoptimize()
{
  return(TXbtreeOptimize);
}

/************************************************************************/

int
TXbtsetoptimize(flags, set)
int     flags;  /* (in) bit flags to set or clear */
int     set;    /* (in) 2: set all defaults  1: set flags  0:  clear flags */
/* Texis function to set/clear global B-tree optimization flags.
 * Returns 0 on error.
 */
{
  switch (set)
    {
    case 2:                                     /* set defaults */
      TXbtreeOptimize = BTREE_OPTIMIZE_DEFAULT;
      break;
    case 1:                                     /* set flags */
      if (flags & ~BTREE_OPTIMIZE_ALL) return(0);/* illegal flag */
      TXbtreeOptimize |= flags;
      break;
    case 0:                                     /* clear flags */
      if (flags & ~BTREE_OPTIMIZE_ALL) return(0);/* illegal flag */
      TXbtreeOptimize &= ~flags;
      break;
    default:                                    /* illegal action */
      return(0);
    }
  return(1);                                    /* success */
}

TXindexValues
TXstrToIndexValues(s, e)
CONST char	*s;
CONST char	*e;
/* Returns -1 on error.
 */
{
	if (!e) e = s + strlen(s);
	while (s < e && strchr(WhiteSpace, *s)) s++;
	while (e > s && strchr(WhiteSpace, e[-1])) e--;
	if (e - s == 11 && strnicmp(s, "splitstrlst", 11) == 0)
		return(TXindexValues_SplitStrlst);
	if (e - s == 3 && strnicmp(s, "all", 3) == 0)
		return(TXindexValues_All);
	return((TXindexValues)(-1));
}

CONST char *
TXindexValuesToStr(indexValues)
TXindexValues	indexValues;
{
	switch (indexValues)
	{
	case TXindexValues_SplitStrlst:	return("splitstrlst");
	case TXindexValues_All:		return("all");
	default:			return("unknown");
	}
}

/************************************************************************/

#ifdef KAI_ABTREE_DEBUG
static char     Dbuf[128];
static void vbtdumppage ARGS((BPAGE *p));
static void
vbtdumppage(p)
BPAGE   *p;
{
  int   i;
  BITEM *it;

  printf("(lpage = %08lX):\n", (ulong)p->lpage);
  fflush(stdout);
  for (i = 0; i < p->count; i++)
    {
      it = p->items + i;
      printf("  %-10s  %3ld %08lX\n",
             (char *)p + it->vf.var.key, (long)it->locn.off,(ulong)it->hpage);
      fflush(stdout);
    }
}

static void fbtdumppage ARGS((BPAGE *p));
static void
fbtdumppage(p)
BPAGE   *p;
{
  int   i;
  BITEM *it;

  printf("(lpage = %08lX):\n", (ulong)p->lpage);
  fflush(stdout);
  for (i = 0; i < p->count; i++)
    {
      it = p->items + i;
      printf("  %08lX  %3ld %08lX\n",
             it->vf.key, (long)it->locn.off, (ulong)it->hpage);
      fflush(stdout);
    }
}
#  define PF(a)                 (printf a, fflush(stdout))
#  define DUMPPAGE(t, p)        \
  if ((t)->flags & BT_FIXED) fbtdumppage(p); else vbtdumppage(p)
#else   /* !KAI_ABTREE_DEBUG */
#  define PF(a)
#  define DUMPPAGE(t, p)
#endif  /* !KAI_ABTREE_DEBUG */

/************************************************************************/

static int getroot ARGS((BTREE *));

static int
getroot(t)
BTREE	*t;
{
	/* Need pre/postbufsz in buffer too.  They are derived from
	 * KDBF_...BUFSZ_WANT, but aligned up:
	 */
	union
	{
		byte	buf[KDBF_PREBUFSZ_WANT + TX_ALIGN_BYTES +
				sizeof(BTREES) + KDBF_POSTBUFSZ_WANT];
		BTREES	btrees;			/* try to force alignment */
	}
	buf;
	BTLOC	loc;
	int	ret, res;

	/* DBF ioctls set by BT_EXCLUSIVEACCESS are
	 * incompatible with readdbf(); temp clear them for DBF call:
	 */
	if (t->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(t, 0);
	res = readdbf(t->dbf, t->hdroff, buf.buf,sizeof(BTREES));
	if (t->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(t, 1);

	if (res != sizeof(BTREES))
		ret = -1;
	else
	{
		/* This is safe, because t->prebufsz is aligned: */
		t->root = ((BTREES *)(buf.buf + t->prebufsz))->root;
#ifdef DEBUG
		if(t->root != btrees.root)
			DBGMSG(9,(999, NULL, "They're different"));
#endif
		ret = 0;
	}
	if (t->flags & BT_LOGOPS)
	{
		TXsetrecid(&loc, t->root);	/* log root: more useful */
		btlogop(t, 0, NULL, &loc, "RDroot",(ret < 0 ? "fail" : "ok"));
	}
	return 0;
}

/******************************************************************/

int
btreinit(t)
BTREE	*t;
{
	int	i;

	btflush(t);
	for(i=0; i < t->cachesize; i++)
	{
		t->cache[i].pid = (EPI_OFF_T)0;
		/* KNG 011116 invalidate the history too,
		 * in case a new search is not done.
		 * Pick a real bogus value (other than -1==invalid).
		 */
		t->his[i].page = (EPI_OFF_T)(-559038737);  /* 0xdeadbeef */
		t->his[i].index = -1;
	}

	/* Do a rewindbtree() too, since the sequence
	 * rewindbtree(), btreinit(), btgetnext() is supposed to be valid:
	 */
	t->sdepth = 0;
	t->cdepth = 0;
	t->his[0].page = (EPI_OFF_T)0;
	t->his[0].index = -1;

	return(getroot(t));
}

/******************************************************************/

EPI_OFF_T
btwritebuf(t, off, buf, sz)
BTREE		*t;
EPI_OFF_T	off;
void		*buf;
size_t		sz;
/* Helper function to write DD or BTREES with pre/postbufsz.
 * Should only be used with small `sz' to minimize malloc()/memcpy().
 */
{
	size_t			na;
	EPI_OFF_T		retoff;
	char			sflags;
	char			*prebuf, tmp[512];

	/* DBF ioctls set by BT_EXCLUSIVEACCESS are incompatible with
	 * putdbf() to a specific offset; temp clear them for DBF call:
	 */
	if (off != (EPI_OFF_T)(-1) && (t->flags & BT_EXCLUSIVEACCESS))
		TXbtsetexclusiveioctls(t, 0);

	/* In case `buf' is the tree itself, do not save non-BT_SAVE_FLAGS.
	 * Caller (e.g. btsetroot()) used to clear these itself, but that
	 * would inadvertently clear BT_EXCLUSIVEACCESS which we need here:
	 */
	sflags = t->flags;
	t->flags &= BT_SAVE_FLAGS;		/* do not save BT_LINEAR etc*/

	if (t->prebufsz || t->postbufsz)
	{					/* alloc buf with pre/post */
		na = t->prebufsz + sz + t->postbufsz;
		prebuf = tmp;
		if (na > sizeof(tmp) &&
		    !(prebuf = (char *)TXmalloc(TXPMBUFPN, __FUNCTION__, na)))
		{
			TxBtreeErr = "Cannot alloc memory";
			retoff = (EPI_OFF_T)(-1);
		}
		else
		{
			memcpy(prebuf + t->prebufsz, buf, sz);
			retoff = putdbf(t->dbf, off, prebuf, sz);
			if (prebuf != tmp) prebuf = TXfree(prebuf);
		}
	}
	else					/* can write `buf' direct */
		retoff = putdbf(t->dbf, off, buf, sz);

	t->flags = sflags;			/* restore flags */

	if (off != (EPI_OFF_T)(-1) && (t->flags & BT_EXCLUSIVEACCESS))
		TXbtsetexclusiveioctls(t, 1);

	return(retoff);
}

int
btsetroot(b)
BTREE	*b;
/* Writes data dictionary and header, if dirty.  Internal use.
 * Returns 0 on success, -1 on error (e.g. DBF write error).
 */
{
	static CONST char	fn[] = "btsetroot";
	EPI_OFF_T		off;
	int			ret = 0;
	BTLOC			loc;

	if (b->dddirty && b->datad)		/* DD needs to be saved */
	{
		if(!b->datadoff)
			b->datadoff = (EPI_OFF_T)(-1); /* wtf for RAM dbf */
		off = btwritebuf(b, b->datadoff, b->datad, b->datad->size);
		if (off == (EPI_OFF_T)(-1))	/* could not write to DBF */
		{
			putmsg(MERR + FWE, fn,
				"Cannot write data dictionary of B-tree %s",
				getdbffn(b->dbf));
			TxBtreeErr = "Cannot write data dictionary of B-tree";
			ret = -1;		/* error */
		}
		else				/* successful write */
		{
			b->dddirty = 0;
			if (off != b->datadoff)	/* DD offset changed */
			{
				b->datadoff = off;
				b->iamdirty = 1;
			}
		}
		if (b->flags & BT_LOGOPS)
		{
			TXsetrecid(&loc, off);
			btlogop(b, 0, NULL, &loc, "WRdd",
				(off == (EPI_OFF_T)(-1) ? "fail" : "ok"));
		}
	}
	if(b->iamdirty)				/* header needs to be saved */
	{
		/* btwritebuf() will only save BT_SAVE_FLAGS to DBF;
		 * we no longer temp-clear the others here so that
		 * btwritebuf() knows about BT_EXCLUSIVEACCESS. KNG 20070425
		 */
		/* b->iamdirty == 2 means this is a newly-created tree;
		 * we only flip hdroff 0 to -1 then (needed for RAM dbf?):
		 */
		if ((off = b->hdroff) == (EPI_OFF_T)0 && b->iamdirty == 2)
			off = (EPI_OFF_T)(-1);	/* WTF for RAM dbf */
		/* WTF we may not have alloc'd `b->szread' bytes? */
		if(b->szread)
			off = btwritebuf(b, off, b, b->szread);
		else
			off = btwritebuf(b, off, b, sizeof(BTREES));
		if (off == (EPI_OFF_T)(-1))	/* could not write to DBF */
		{
			putmsg(MERR + FRE, fn,
				"Cannot write header of B-tree %s",
				getdbffn(b->dbf));
			TxBtreeErr = "Cannot write header of B-tree";
			ret = -1;
		}
		else				/* successful write */
		{
			b->iamdirty = 0;
			if (b->hdroff == (EPI_OFF_T)(-1))/*new tree same DBF*/
				b->hdroff = off;
			else if (off != b->hdroff)
			{
				putmsg(MERR + UGE, fn,
		"Header written at unexpected offset 0x%wx (expected 0x%wx)",
				(EPI_HUGEUINT)off, (EPI_HUGEUINT)b->hdroff);
				TxBtreeErr = "Header written at unexpected offset";
				ret = -1;
			}
		}
		if (b->flags & BT_LOGOPS)
		{
			TXsetrecid(&loc, b->root);/* log root: more useful */
			btlogop(b, 0, NULL, &loc, "WRhdr",
				(off == (EPI_OFF_T)(-1) ? "fail" : "ok"));
		}
	}
	return(ret);
}
/******************************************************************/


int
existsbtree(filename)
char	*filename;
{
  size_t        sz;
  char          buf[PATH_MAX];

  if (filename == CHARPN || filename[0] == '\0')
    return(0);
  sz = strlen(filename);
  if (sz < BTREE_EXT_SZ || strcmp(filename + sz - BTREE_EXT_SZ, BtreeExt))
    {
      if (!TXcatpath(buf, filename, BtreeExt))
        return(0);
      filename = buf;
    }
  return((access(filename, F_OK) == 0) ? 1 : 0);
}

/******************************************************************/

BTREE *
openbtree(filename, psize, csize, flags, mode)
char *filename;
int psize;
int csize;
int flags;
int mode;	/* (in) O_... flags e.g. O_RDWR | O_CREAT */
{
  char          *p;
  BTREE         *rc;
  size_t        sz;
  char          buf[PATH_MAX];

  TxBtreeErr = CHARPN;
  if (filename != CHARPN && filename[0] != '\0')
    {
      sz = strlen(filename);
      if ((sz < BTREE_EXT_SZ || strcmp(filename + sz - BTREE_EXT_SZ, BtreeExt))
#ifdef DEVEL
          && strcmp(".", filename)
#endif
          )
        {
          if (!TXcatpath(buf, filename, BtreeExt))
            {
              TxBtreeErr = "Path too long";
              return(BTREEPN);
            }
          p = buf;
        }
      else
        p = filename;
    }
  else
    p = CHARPN;
  if (flags & BT_FIXED)
    rc = openfbtree(p, psize, csize, flags, mode, BTREE_DEFAULT_HDROFF,DBFPN);
  else
    rc = openvbtree(p, psize, csize, flags, mode, BTREE_DEFAULT_HDROFF,DBFPN);
  if (rc == BTREEPN) goto done;                 /* failed to open */
  rc->openMode = mode;                          /* *before* bttexttoparam() */
  if (rc->dbf)
    ioctldbf(rc->dbf, RDBF_SETOVER, NULL);

  /* Defaults; may be overridden by SYSINDEX.PARAMS in bttexttoparam(): */
  BTPARAM_INIT(&rc->params);
done:
  return(rc);
}

/************************************************************************/

BTREE *
closebtree(t)
BTREE *t;
{
	static CONST char	fn[] = "closebtree";

	if(!t)
		return t;
	if(t->magic != BTREE_MAGIC)
	{
		putmsg(MERR, fn, "B-tree %s corrupt: Bad magic number",
			(t->dbf ? getdbffn(t->dbf) : "?"));
	}
	if (t->flags & BT_FIXED)
		return closefbtree(t);
	else
		return closevbtree(t);
}

/************************************************************************/

int
btspinsert(t, index, keysize, key, sp)
BTREE *t;
BTLOC *index;
size_t keysize;
void *key;
int sp;		/* (in) percentage page fill */
/* Like btinsert(), but allows up to `sp' percent of page to be filled,
 * rather than 50%.  Typically for inserting (in order) into a RAM B-tree
 * being used as a list, i.e. no future random deletes/inserts; saves space.
 */
{
	int	rc;
	char	tmp[256];

	BTPARAM_ACTIVATE(&t->params);

	if (t->flags & BT_FIXED)
		rc = fbtinsert(t, *index, keysize, key);
	else
	{
		if(keysize > (t->pagesize - sizeof(BPAGE)))
		{
			putmsg(MERR + MAE, __FUNCTION__,
      "Cannot insert %wu-byte value `%s' into B-tree `%s': Value too large",
			       (EPI_HUGEUINT)keysize,
			       btkey2str(tmp, sizeof(tmp), key, keysize),
			       TX_BTREE_NAME(t));
			goto err;
		}
		rc = vbtinsert(t, *index, keysize, key, sp);
	}
	btsetroot(t);
	goto done;

err:
	rc = -1;
done:
	BTPARAM_DEACTIVATE(&t->params);
	return rc;
}

/************************************************************************/

char *
btkey2str(buf, bufsz, key, keysize)
char		*buf;
size_t		bufsz;
CONST void	*key;
size_t		keysize;
/* Creates printable version of `key' in `buf'.
 */
{
#define CVT(a)	((a) >= ' ' && (a) <= '~' ? (a) : '.')
	char		*d;
	CONST char	*s, *e;

	if (bufsz < 1) return("");		/* sanity check */
	bufsz--;
	if (keysize <= bufsz)			/* all fits */
	{
		for (s = (CONST char *)key, e = s + keysize, d = buf;
		     s < e;
		     s++, d++)
			*d = CVT(*s);
	}
	else if (bufsz < 3)			/* sanity check */
	{
		for (d = buf; d < buf + bufsz; d++) *d = '.';
	}
	else					/* skip some in middle */
	{
		for (s = (CONST char *)key, e = s + (bufsz - 3)/2, d = buf;
		     s < e;
		     s++, d++)
			*d = CVT(*s);
		*(d++) = '.';
		*(d++) = '.';
		*(d++) = '.';
		s += (keysize - (bufsz - 3));
		for (e = (CONST char *)key + keysize; s < e; s++, d++)
			*d = CVT(*s);
	}
	*d = '\0';
	return(buf);
#undef CVT
}

/************************************************************************/

int
btinsert(t, index, keysize, key)
BTREE *t;
BTLOC *index;
size_t keysize;
void *key;
/* Returns -1 on error.
 */
{
	static CONST char Fn[] = "btinsert";
	int		rc;
	char		tmp[64];

	BTPARAM_ACTIVATE(&t->params);

	if (t->flags & BT_FIXED)
		rc = fbtinsert(t, *index, keysize, key);
	else
	{
		if(keysize > ((t->pagesize - sizeof(BPAGE))/4))
		{
			putmsg(MERR + MAE, Fn,
			      "Cannot insert %wu-byte value `%s' into B-tree %s: Value too large",
			       (EPI_HUGEUINT)keysize, btkey2str(tmp, sizeof(tmp), key, keysize), getdbffn(t->dbf));
			goto err;
		}
		rc = vbtinsert(t, *index, keysize, key, 50);
	}
	btsetroot(t);
	goto done;

err:
	rc = -1;
done:
	BTPARAM_DEACTIVATE(&t->params);
	return rc;
}

/************************************************************************/

#ifdef BTPRINT
void
btprint(t)
BTREE *t;
{
	if (t->flags & BT_FIXED)
		fbtprint(t);
	else
		vbtprint(t);
}
#endif

/************************************************************************/

BTLOC
btsearch(t, keysize, key)
BTREE *t;
int keysize;
void *key;
{
	BTLOC	rc;

	BTPARAM_ACTIVATE(&t->params);
	if (t->flags & BT_FIXED)
		rc = fbtsearch(t, keysize, key, NULL);
	else
		rc = vbtsearch(t, keysize, key, NULL);
	BTPARAM_DEACTIVATE(&t->params);
	return(rc);
}

/************************************************************************/

BTLOC
btsearch2(t, keysize, key, recid)
BTREE	*t;
int	keysize;
void	*key;
BTLOC	*recid;
{
	BTLOC	rc;

	BTPARAM_ACTIVATE(&t->params);
	if (t->flags & BT_FIXED)
		rc = fbtsearch(t, keysize, key, recid);
	else
		rc = vbtsearch(t, keysize, key, recid);
	BTPARAM_DEACTIVATE(&t->params);
	return(rc);
}

/************************************************************************/

BTLOC
btgetlast(t, keysize, key)
BTREE *t;
size_t *keysize;
void *key;
{
	BTLOC	rc;

	BTPARAM_ACTIVATE(&t->params);
	if (t->flags & BT_FIXED)
		rc = fbtgetlast(t, keysize, key);
	else
		rc = vbtgetlast(t, keysize, key);
	BTPARAM_DEACTIVATE(&t->params);
	return(rc);
}

/******************************************************************/

EPI_OFF_T
btpg2off(bt, pg)
BTREE	*bt;
BPAGE	*pg;
/* Returns offset of `pg'.
 */
{
	int	i;

	for (i = 0; i < bt->cachesize; i++)
		if (bt->cache[i].page == pg && bt->cache[i].inuse)
			return(bt->cache[i].pid);
	return((EPI_OFF_T)(-1));
}

/******************************************************************/

void
btcantgetpage(fn, bt, off, refoff, refitem)
CONST char	*fn;
BTREE		*bt;
EPI_OFF_T	off;		/* page we cannot get */
EPI_OFF_T	refoff;		/* referring page (-1: ?, -2: off is root) */
int		refitem;	/* referring item (-1: lpage) */
{
	char	xtra[EPI_OFF_T_BITS/4+EPI_OS_INT_BITS/3+40];

	if (refoff >= (EPI_OFF_T)0)	/* referring page is known */
	{
		htsnpf(xtra, sizeof(xtra), " referenced by page 0x%wx",
			(EPI_HUGEUINT)refoff);
		if (refitem == -1) strcat(xtra, " lpage");
		else sprintf(xtra + strlen(xtra), " item %d", refitem);
	}
	else
		*xtra = '\0';
	putmsg(MERR + FRE, fn, "Cannot get %spage 0x%wx of B-tree %s%s",
	       (refoff == (EPI_OFF_T)(-2) ? "root " : ""),
	       (EPI_HUGEUINT)off, getdbffn(bt->dbf), xtra);
}

/************************************************************************/

void
btupdate(bt, btloc)
BTREE *bt;
BTLOC btloc;
{
	BPAGE	*p;
	int	level = bt->sdepth;
	EPI_OFF_T	page = bt->his[level].page;

	if ((p = btgetpage(bt, page)) == BPAGEPN)
	{
		btcantgetpage("btupdate", bt, page, (EPI_OFF_T)(-1), -1);
		return;
	}
	if(_recidcmp(&p->items[bt->his[level].index].locn, &btloc)!=0)
	{
		p->items[bt->his[level].index].locn = btloc;
		btdirtypage(bt, page);
	}
	p = btreleasepage(bt, page, p);
}

/************************************************************************/

BTLOC
btgetnext(t, len, x, pdataptr)
BTREE	*t;		/* (in/out) the B-tree */
size_t	*len;		/* (in/out) length of `x' buffer */
void	*x;		/* (in/out) buffer to copy key to */
byte	**pdataptr;	/* (out, opt.) pointer to actual key data on page */
/* Returns invalid recid on EOF or error; TX_BTREE_GOT_ERROR() true on error.
 */
{
	BTLOC rc;
	size_t olen = 0;

	if(len)
		olen = *len;
	do {
		if(len)
			*len = olen;
		if (t->flags & BT_FIXED)
			rc = fbtgetnext(t, len, x, pdataptr);
		else
			rc = vbtgetnext(t, len, x, pdataptr);
	} while (TXgetoff(&rc) == RECID_DELETED);
	return rc;
}

/************************************************************************/
/*
	Deletes an item from the Btree.

	This will delete the item referenced by key.  Btloc should hold
	the location the key references if known, or -1 otherwise.
        Returns 1 if deleted, 0 if not found.
*/

int
btdelete(t, btloc, keysize, key)
BTREE *t;			/* Tree to delete from */
BTLOC *btloc;			/* Optional location */
int keysize;			/* Size of the key */
void *key;			/* The key */
{
  int   rc;

	BTPARAM_ACTIVATE(&t->params);
	if (t->flags & BT_FIXED)
		rc = fbtdelete(t, *btloc, keysize, key);
	else
		rc = vbtdelete(t, *btloc, keysize, key);
	btsetroot(t);
	BTPARAM_DEACTIVATE(&t->params);
        return(rc);
}

/***********************************************************************/

int
btreeclear(bt)
BTREE *bt;
/* Clears `bt'.
 * NOTE: should not be used if shared DBF (i.e. multiple B-trees).
 * WTF change to walk the tree instead of DBF, for shared DBF.
 */
{
	size_t sz;
	EPI_OFF_T dd, c;
	void *v;
	int rc = 0, res;
	BTLOC	loc;

	/* DBF ioctls set by BT_EXCLUSIVEACCESS are incompatible with
	 * getdbf(); temp clear them:
	 */
	if (bt->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(bt, 0);

	if (bt->flags & BT_SHAREDBF)
		putmsg(MWARN + UGE, "btreeclear",
			"Clearing shared-DBF B-tree %s", getdbffn(bt->dbf));
	getdbf(bt->dbf, 0, &sz);
	dd = bt->datadoff;
	while((v = getdbf(bt->dbf, -1, &sz)) != NULL)
	{
		c = telldbf(bt->dbf);
		if(c && c != dd)
		{
			res = freedbf(bt->dbf, c);
			if (bt->flags & BT_LOGOPS)
			{
				TXsetrecid(&loc, c);
				btlogop(bt, 0, NULL, &loc, "FRdbf",
					(res ? "ok" : "fail"));
			}
			rc ++;
		}
	}
	bt->root = (EPI_OFF_T)0;
	bt->npages = 0;
	bt->numItemsDelta = 0;

	if (bt->flags & BT_EXCLUSIVEACCESS) TXbtsetexclusiveioctls(bt, 1);
	return rc;
}

/************************************************************************/
/*
	Return to default comparison function.
*/

void
btsetdefcmp(t)
BTREE *t;			/* Tree to set comparison function */
{
	if (t->flags & BT_FIXED)
		fbtsetdefcmp(t);
	else
		vbtsetdefcmp(t);
}

/************************************************************************/

void
rewindbtree(bt)
BTREE *bt;
{
	bt->sdepth = 0;
	bt->cdepth = 0;
	bt->his[0].page = (EPI_OFF_T)0;
	bt->his[0].index = -1;
}

/******************************************************************/

int
btreesetmarker(bt)
BTREE	*bt;
{
	BPAGE	*p;

	bt->rangeend = bt->his[bt->sdepth];
	if (bt->root == bt->rangeend.page)
	{
		if ((p = btgetpage(bt, bt->rangeend.page)) == BPAGEPN)
		{
			btcantgetpage("btreesetmarker", bt, bt->rangeend.page,
				      (EPI_OFF_T)(-1), -1);
			return(0);		/* WTF correct for error? */
		}
		if (p->count == bt->rangeend.index) bt->rangeend.index--;
		p = btreleasepage(bt, bt->rangeend.page, p);
	}
	bt->stopatrange = 1;
	if(bt->rangeend.page == bt->root && bt->rangeend.index == -1)
		return 1;
	return 0;
}

/******************************************************************/

int
btreeunsetmarker(bt)
BTREE	*bt;
{
	bt->stopatrange = 0;
	return 0;
}

/******************************************************************/

int
btreesetdd(bt, dd)
BTREE	*bt;
DD	*dd;
{
	if (dd)
		bt->datad = convertdd(dd, 0);
/*
	ddsettype(&bt->datad, TEXIS_FAST_TABLE);
*/
	bt->dddirty = 1;
	return 0;
}

/******************************************************************/

DD *
btreegetdd(bt)
BTREE	*bt;
{
	return bt->datad;
}

/******************************************************************/

int
btgetpercentage(bt)
BTREE	*bt;
{
	static CONST char Fn[] = "btgetpercentage";
	int		low, high, nl, nh, range;
	int		depth, count, index;
	EPI_OFF_T	page;
	BPAGE		*p;

	low = 0;
	high = 100;
	depth = 0;
	range = high - low;

	while(range)
	{
		page = bt->his[depth].page;
		index = bt->his[depth].index+1;

		if (page == (EPI_OFF_T)0) break;	/* wtf check sdepth?*/
		if ((p = btgetpage(bt, page)) == BPAGEPN)
		{
			btcantgetpage(Fn, bt, page, (EPI_OFF_T)(-1), -1);
			break;
		}
		count = p->count + 1;
		nl = (index * 100) / count;
		nh = ((index + 1) * 100) / count;
		if(nl < 0) nl = 0;
		if(nh > 100) nh = 100;
#ifdef NEVER
		putmsg(999, NULL, "Low = %d, High = %d, nl = %d, nh = %d", low, high, nl, nh);
#endif
		high = low + ((range * nh) / 100);
		low = low + ((range * nl) / 100);
#ifdef NEVER
		putmsg(999, NULL, "Low = %d, High = %d", low, high);
#endif
		range = high - low;
		p = btreleasepage(bt, page, p);
		depth ++;
		if(depth > bt->sdepth)
			break;
	}
	return low;
}

/*****************************************************************************/

int
TXbtsetexclusiveioctls(bt, set)
BTREE	*bt;
int	set;
/* Internal B-tree use; sets or clears DBF ioctls for BT_EXCLUSIVEACCESS.
 * Returns 0 if all ioctls failed, 1 if partial success, 2 if total success.
 */
{
	int	fails = 0, oks = 0;

        /* WTF support KDBF_IOCTL_NOREADERS *without* KDBF_IOCTL_APPENDONLY
	 * in kdbf, so that we do not have to temp-turn-off these ioctls in
	 * btreadpage() etc.:
	 */
	if (ioctldbf(bt->dbf, (DBF_KAI | KDBF_IOCTL_APPENDONLY),
		     (void *)(EPI_VOIDPTR_INT)set) == 0)
		oks++;
	else
		fails++;
	if (ioctldbf(bt->dbf, (DBF_KAI | KDBF_IOCTL_NOREADERS),
		     (void *)(EPI_VOIDPTR_INT)set) == 0)
		oks++;
	else
		fails++;
	return(oks ? (fails ? 1 : 2) : 0);
}

/*****************************************************************************/

int
ioctlbtree(t, ioctl, data)
BTREE   *t;
int     ioctl;
void    *data;
/* Returns -1 on error, 0 if ok.
 */
{
  static CONST char   fn[] = "ioctlbtree";

  if (t->magic != BTREE_MAGIC)
    {
      putmsg(MERR, fn, "B-tree %s corrupt: Bad magic number",
             (t->dbf ? getdbffn(t->dbf) : "?"));
      return(-1);
    }
  switch (ioctl)
    {
    case BTREE_IOCTL_LINEAR:
      if (!(t->flags & BT_LINEAR) && data != NULL &&
          (t->root != (EPI_OFF_T)0 || t->cacheused > 0))
        {
          putmsg(MERR+UGE, fn, "Linear mode not permitted on existent tree %s",
                 getdbffn(t->dbf));
          return(-1);
        }
      if ((t->flags & BT_LINEAR) && data == NULL)
        {
          putmsg(MERR+UGE, fn,"Cannot change linear flag on existent tree %s",
                 getdbffn(t->dbf));
          return(-1);
        }
      if (data != NULL)
        t->flags |= BT_LINEAR;
      else
        t->flags &= ~BT_LINEAR;
      return(0);
    case BTREE_IOCTL_EXCLUSIVEACCESS:
      /* Note that this can be set either indirectly at open[fv]btree()
       * with BT_EXCLUSIVEACCESS flag, or directly with ioctlbtree().
       */
      if ((data ? 1 : 0) == ((t->flags & BT_EXCLUSIVEACCESS) ? 1 : 0))
        return(0);                              /* nothing to do */
      if (t->flags & BT_SHAREDBF)
        {
          /* May be bad interactions with shared-dbf; wtf see if possible? */
          putmsg(MERR + UGE, fn,
                 "Cannot set BT_EXCLUSIVEACCESS on BT_SHAREDBF tree %s",
                 getdbffn(t->dbf));
          return(-1);                           /* failed */
        }
      /* BT_EXCLUSIVEACCESS should always be in sync with current state
       * of corresponding DBF ioctls, so we know their state.  These
       * ioctls will fail if our DBF is not KDBF, so if failure, do
       * not set BT_EXCLUSIVEACCESS (so we save useless ioctl-toggle
       * in btwritepage() etc.), but do not report/return error if setting,
       * because this could be RAM DBF etc. which does not support it
       * (and failure of DBF to support it is merely a potential performance
       * penalty, not a corruption issue):
       */
      if (data)                                 /* set flag */
        {
          /* If either ioctl succeeds, set BT_EXCLUSIVEACCESS so that we
           * know to clear these ioctls later:
           */
          if (TXbtsetexclusiveioctls(t, 1))
            t->flags |= BT_EXCLUSIVEACCESS;
        }
      else                                      /* clear flag */
        {
          /* We know BT_EXCLUSIVEACCESS is currently set, thus at least
           * one ioctl succeeded and thus `t->dbf' is KDBF.  So any ioctl
           * failure is a true failure (not RAM-DBF-no-support):
           */
          if (TXbtsetexclusiveioctls(t, 0) != 2)
            {
              putmsg(MERR, fn,
               "Could not clear DBF ioctls for BT_EXCLUSIVEACCESS on tree %s",
                     getdbffn(t->dbf));
              return(-1);
            }
          t->flags &= ~BT_EXCLUSIVEACCESS;
        }
      return(0);                                /* success */
    default:
      return(-1);
    }
}

int
btappend(t, loc, keysize, key, sp, bm)
BTREE   *t;
BTLOC   *loc;
int     keysize;
void    *key;
int     sp;
BTBM    *bm;
/* Appends an item to end of fixed/variable BTREE `t'.  Item must be
 * greater than rest of tree, tree must be BT_LINEAR, and no other
 * operations on tree may take place, before or after.  `sp' is
 * percentage split point.  If `bm' is non-NULL, will set bm->thisrec
 * to this item's location, and update bm->bookmark if moved (i.e. from
 * cache to disk).  Returns -1 on error, 0 if ok.  KNG 971015
 */
{
  static CONST char     fn[] = "btappend";
  int                   (*apitem) ARGS((BTREE *t,BPAGE *p,BITEMI *bi,int sp));
  int                   i, ret;
  BPAGE                 *p;
  BITEMI                bi;
  char                  tmp[64];

  BTPARAM_ACTIVATE(&t->params);
  PF(("Adding `%s' %d: ", (t->flags & BT_FIXED) ?
      (sprintf(Dbuf,"%08lX",*(EPI_OFF_T*)key), Dbuf) : (char*)key,(int)loc->off));
  if (!(t->flags & BT_LINEAR))
    {
      putmsg(MERR + UGE, fn, "Linear op attempted in tree %s",
             getdbffn(t->dbf));
      goto err;
    }
  apitem = (t->flags & BT_FIXED) ? fbtappenditeml : vbtappenditeml;
  bi.hpage = 0;                                         /* really lpage */
  bi.locn = *loc;
  if (keysize > t->pagesize)
    {
      putmsg(MERR + MAE, fn, "%wu-byte key `%s' too large for B-tree %s",
             (EPI_HUGEUINT)keysize, btkey2str(tmp, sizeof(tmp), key, keysize),
             getdbffn(t->dbf));
      goto err;
    }
  bi.len = keysize;
  bi.alloced = 0;
  bi.string = (char *)key;
  for (i = 0; i < t->cacheused; i++)
    {
      p = t->cache[i].page;
      if (apitem(t, p, &bi, sp))                        /* was room in page */
        {
          PF((" added at level %d\n", i));              /* don't flag dirty; */
          goto ok;                                      /*  will flush later */
        }
      p->items[p->count - 1].hpage = bi.hpage;          /* link higher page */
      bi.hpage = btwritepage(t, (EPI_OFF_T)(-1), p);
      if (bi.hpage == (EPI_OFF_T)(-1)) goto err;
      if (bm != BTBMPN && bm->bookmark.page == -i)      /* writing bookmark */
        bm->bookmark.page = bi.hpage;
      PF(("wrote level %d page %08lX ", i, (ulong)bi.hpage));
      DUMPPAGE(t, p);
      btinitpage(t, p);                                 /* "new" page */
    }
  /* No room; must create a new page, and thus depth increases by 1.
   * This will be the new root page, but we won't know its dbf offset
   * until it's written, so don't update t->root (would also prevent
   * KDBF_IOCTL_APPENDONLY mode on dbf):
   */
  if (t->cacheused >= t->cachesize)                     /* sanity check */
    {
      putmsg(MERR + MAE, fn, "Cache size exceeded");
      goto err;
    }
  i = t->cacheused;
  t->cache[i].page = p = btmkpage(t);
  if (p == BPAGEPN) goto err;
  t->cacheused += 1;
  if (!apitem(t, p, &bi, sp)) goto err;                 /* must fit */
  PF((" added at new level %d\n", i));
ok:
  if (bm != BTBMPN)                                     /* record our loc */
    {
      bm->thisrec.page = -i;                            /* <= 0 means cache */
      bm->thisrec.index = p->count - 1;
    }
  t->numItemsDelta++;
  ret = 0;
  goto done;

err:
  ret = -1;
done:
  BTPARAM_DEACTIVATE(&t->params);
  return(ret);
}

int
btupdatebm(t, bm, keysize, key)
BTREE   *t;
BTBM    *bm;
int     keysize;
void    *key;
/* Replaces key at bm->bookmark with `key'/`keysize'.  New key must
 * sort the same, and fit on the page.  Only for use with btappend().
 * Returns -1 on error.
 */
{
  static CONST char     fn[] = "btupdatebm";
  BPAGE                 *p;
  BITEMI                bi;
  BITEM                 *it;
  int                   idx, modified = 0;
  EPI_OFF_T             rc;

  if (bm->bookmark.page <= 0)                           /* still in cache */
    p = t->cache[-bm->bookmark.page].page;
  else
    {
      if ((p = btmkpage(t)) == BPAGEPN) return(-1);     /* wtf use cache */
      if (btreadpage(t, bm->bookmark.page, p, &modified) != 0)
        {                                               /* failed */
          p = btfrpage(t, p);
          return(-1);
        }
      /* page written below */
    }
  idx = bm->bookmark.index;
  if (t->flags & BT_FIXED)
    {
      putmsg(MERR + UGE, fn, "Not yet implemented");
      return(-1);
    }
  else
    {
      it = p->items + idx;
      bi.hpage = it->hpage;
      bi.locn = it->locn;
      bi.len = keysize;
      bi.string = (char *)key;
      if (!TXvbtreeReplaceItem(t, p, idx, &bi))
        return(-1);
    }
  if (bm->bookmark.page <= 0)                           /* still in cache */
    {
      return(0);
    }
  else
    {
      rc = btwritepage(t, bm->bookmark.page, p);
      p = btfrpage(t, p);
      return(rc == (EPI_OFF_T)(-1) ? -1 : 0);
    }
}

int
btflushappend(t)
BTREE   *t;
/* Internal function: flush cache after [vf]btappend() calls, at tree
 * close.  Called by btflush(), since each page still needs to be
 * linked, none have a dbf offset yet, and some might be empty.
 * Returns -1 on error, 0 if ok.
 */
{
  BPAGE *p;
  int   i;
  EPI_OFF_T hpage;

  PF(("Flushing cache\n"));
  hpage = 0;                            /* wtf merge small pages, esp. root? */
  for (i = 0; i < t->cacheused; i++)
    {
      t->cache[i].dirty = 0;                            /* no normal flush */
      t->cache[i].pid = (EPI_OFF_T)0;
      p = t->cache[i].page;
      if (p->count == 0) continue;                      /* drop empty pages */
      p->items[p->count - 1].hpage = hpage;             /* link higher page */
      if ((hpage = btwritepage(t, (EPI_OFF_T)(-1), p)) == (EPI_OFF_T)(-1))
        goto err;
      PF(("wrote level %d page %08lX ", i, (ulong)hpage));
      DUMPPAGE(t, p);
    }
  t->root = hpage;
  PF(("Setting root to %08lX\n", (ulong)t->root));
  t->cacheused = 0;
  t->flags &= ~BT_LINEAR;                               /* don't flag file */
  t->iamdirty = 1;                                      /* since we set root */
  return(0);

err:
  return(-1);
}

BTHIST *
openbthist(bt)
BTREE   *bt;
/* Opens new history list for btree `bt'.  Returns NULL on error.
 * Tree should not be modified, before or after this call.
 * KNG 971024
 */
{
  BTHIST                *bh;

  if (!(bh = (BTHIST *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(BTHIST))))
    goto err;
  bh->bt = bt;
  /* rest clear by calloc() */
  if (!(bh->his = (BTRL *)TXcalloc(TXPMBUFPN, __FUNCTION__, bt->cachesize,
                                   sizeof(BTRL))))
    goto err;
  goto done;

err:
  bh = closebthist(bh);
done:
  return(bh);
}

BTHIST *
closebthist(bh)
BTHIST  *bh;
{
  if (bh == BTHISTPN) goto done;
  bh->his = TXfree(bh->his);
  bh = TXfree(bh);
done:
  return(BTHISTPN);
}

BTHIST *
dupbthist(bt)
BTREE   *bt;
/* Opens new history, cloning existing history state of `bt'.
 * Returns NULL on error.
 */
{
  BTHIST        *bh;

  if ((bh = openbthist(bt)) == BTHISTPN) goto done;
  memcpy(bh->his, bt->his, bt->cachesize*sizeof(BTRL));
  bh->sdepth = bt->sdepth;
  bh->cdepth = bt->cdepth;
done:
  return(bh);
}

int
btsethist(bh)
BTHIST  *bh;
/* Sets history list of tree to `bh'.  btsearch() and/or btgetnext()
 * may be called after this, and this history list will be modified.
 * btunsethist() must be called when done (before closing tree, etc.).
 */
{
  BTREE *bt = bh->bt;

  bh->ohis = bt->his;                   /* save for btunsethist() */
  bh->osdepth = bt->sdepth;
  bh->ocdepth = bt->cdepth;
  bt->his = bh->his;
  bt->sdepth = bh->sdepth;
  bt->cdepth = bh->cdepth;
  return(1);
}

int
btunsethist(bh)
BTHIST  *bh;
{
  BTREE *bt = bh->bt;

  bh->sdepth = bt->sdepth;
  bh->cdepth = bt->cdepth;
  bt->his = bh->ohis;
  bt->sdepth = bh->osdepth;
  bt->cdepth = bh->ocdepth;
  return(1);
}

FLD *
TXgetNextIndexFldFromTable(indexFields, dbtbl, curField)
CONST char      *indexFields;   /* (in) B-tree fields (SYSINDEX.FIELDS) */
DBTBL           *dbtbl;         /* (in) table that B-tree index is on */
CONST char      **curField;     /* (in/out) current field in `indexFields' */
/* Returns FLD * from `dbtbl' for next field in `indexFields',
 * after `*curField' (NULL: get first); advances `*curField'; or NULL
 * if not found.  List ends when NULL returned *and* `**curField' set to nul.
 */
{
  static CONST char     fn[] = "TXgetNextIndexFldFromTable";
  FLD                   *dbtblFld;
  CONST char            *fldNameEnd;
  size_t                n;
  char                  tmpFldNameBuf[256], *tmpFldName = tmpFldNameBuf;

  /* Get the next index field name: */
  if (*curField)                                /* get next field */
    *curField += strcspn(*curField, IndexFieldSep);     /* advance over fld */
  else                                          /* get first field */
    *curField = indexFields;
  *curField += strspn(*curField, IndexFieldSep);
  if (!**curField) return(FLDPN);               /* no more fields */
  fldNameEnd = *curField + strcspn(*curField, IndexFieldSep);

  /* Copy name to `tmpFldName' so we can nul-terminate: */
  n = fldNameEnd - *curField;
  if (n < sizeof(tmpFldNameBuf))
    {
      memcpy(tmpFldName, *curField, n);
      tmpFldName[n] = '\0';
    }
  else if (!(tmpFldName = TXstrndup(TXPMBUFPN, fn, *curField, n)))
    goto err;

  /* Get the table field: */
  dbtblFld = dbnametofld(dbtbl, tmpFldName);
  goto done;

err:
  dbtblFld = FLDPN;
done:
  if (tmpFldName && tmpFldName != tmpFldNameBuf)
    tmpFldName = TXfree(tmpFldName);
  return(dbtblFld);
}

int
TXbtreeHasSplitValues(indexFields, dbtbl, indexValues)
CONST char      *indexFields;   /* (in) B-tree fields (SYSINDEX.FIELDS) */
DBTBL           *dbtbl;         /* (in) table that B-tree index is on */
TXindexValues   indexValues;    /* (in) TXindexValues... value */
/* Returns 1 if B-tree index on `indexFields' would have multi-item
 * type for first field *and* its values would be split, according to
 * its `indexFields', `dbtbl' schema and `indexValues'; 0 if not or
 * unknown.
 */
{
  static CONST char     fn[] = "TXbtreeHasSplitValues";
  FLD                   *dbtblFld;
  CONST char            *fldName;
  size_t                fldNameLen;
  int                   ret;

  switch (indexValues)
    {
    case TXindexValues_SplitStrlst:             /* split strlst iff only fld*/
      /* splitstrlst: index has split multi-item items iff index is
       * one field and it is strlst in the DBTBL.  Must check field
       * type in DBTBL, because if actually split, its type was
       * changed to varchar in the index; would not be able to
       * distinguish from "ordinary" varchar DBTBL field otherwise:
       */

      /* Get the first index field's FLD -- from the table: */
      fldName = NULL;
      dbtblFld = TXgetNextIndexFldFromTable(indexFields, dbtbl, &fldName);
      fldNameLen = strcspn(fldName, IndexFieldSep);
      if (!dbtblFld)
        {
          putmsg(MERR, fn,
                 "Internal error: Cannot find index field `%.*s' in DBTBL",
                 (int)fldNameLen, fldName);
          goto err;                             /* unknown */
        }

      /* See if there is a next field -- if there is, splitstrlst will
       * not split:
       */
      fldName += fldNameLen;
      fldName += strspn(fldName, IndexFieldSep);
      ret = ((dbtblFld->type & DDTYPEBITS) == FTN_STRLST &&
             !*fldName);
      break;

    case TXindexValues_All:                     /* nothing ever split */
      ret = 0;                                  /* false */
      break;

      /* For future type TXindexValues_SplitMulti (split all multi-item
       * types), may have to return 1 iff any `btreeDd' type was
       * *originally* (in `dbtbl') a multi-item type (per
       * TXfldGetNextItem() type logic?)  or will SplitMulti only
       * split first field?
       */

    default:
      putmsg(MERR, fn, "Unknown indexvalues value %d for B-tree",
             (int)indexValues);
      goto err;
    }
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

int
TXbtreeIsOnMultipleItemType(indexFields, dbtbl)
CONST char      *indexFields;   /* (in) B-tree fields (SYSINDEX.FIELDS) */
DBTBL           *dbtbl;         /* (in) table that B-tree index is on */
/* Returns 1 if B-tree index on `indexFields' would be on a multi-item
 * type for first field (of table, regardless of whether split in index
 * or not); or 0 if not or unknown.  WTF do we care about later fields
 * being multi-item someday?
 */
{
  static CONST char     fn[] = "TXbtreeIsOnMultipleItemType";
  FLD                   *dbtblFld;
  CONST char            *fldName;
  size_t                fldNameLen;

  /* Must check field type in DBTBL, because if actually split,
   * type was changed in the index, e.g. DBTBL strlst changed
   * to index varchar:
   */

  /* Get DBTBL FLD of first index field: */
  fldName = NULL;
  dbtblFld = TXgetNextIndexFldFromTable(indexFields, dbtbl, &fldName);
  if (!dbtblFld)
    {
      fldNameLen = strcspn(fldName, IndexFieldSep);
      putmsg(MERR, fn,
             "Internal error: Cannot find index field `%.*s' in DBTBL",
             (int)fldNameLen, fldName);
      return(0);      /* unknown */
  }
  return(TXfldIsMultipleItemType(dbtblFld, NULL, NULL));
}

/******************************************************************/

int
btparamtotext(BTREE *btree, CONST char *indexFields, DBTBL *dbtbl,
              int *textsz, char *buffer)
/* Returns 0 if ok, -1 on severe error.  If `*textsz' increases,
 * there was not enough room.  `dbtbl' is the table the index is on.
 * See also BTPARAM_ACTIVATE(), BTPARAM_DEACTIVATE().
 */
{
	static CONST char	fn[] = "btparamtotext";
	int maxsz, remainsz, needsz = 0, sz;
	char tempbuf[256];

	if(!buffer)
		return -1;
	if(!textsz)
		return -1;
	if(!btree)
		return -1;
	maxsz = *textsz;
	remainsz = maxsz;
	if(maxsz > 0)
	{
		buffer[0] = '\0';
		remainsz --;
		needsz ++;                      /* for terminating `\0' */
	}
	if(btree->params.max_index_text != 0)
	{
		sz = htsnpf(tempbuf, sizeof(tempbuf), "max_index_text=%d;",
				(int)btree->params.max_index_text);
		needsz += sz;
		if(sz <= remainsz)
		{
			strcat(buffer, tempbuf);
			remainsz -= sz;
		}
	}

	/* KNG 20080402 Previous Texis versions do not have stringcomparemode,
	 * and it effectively defaulted to ...DEFAULT_OLD, so if we do not
	 * see stringcomparemode in SYSINDEX.PARAMS we know to use the old
	 * default.  So for brevity, only need to store it here if it has
	 * changed from that old default:
	 */
	if (btree->params.stringcomparemode != TXCFF_STRINGCOMPAREMODE_DEFAULT_OLD)
	{
		strcpy(tempbuf, stringCompareModeStr);
		tempbuf[STRINGCOMPAREMODESTR_SZ] = '=';
		sz = TXtxcffToStr(tempbuf + STRINGCOMPAREMODESTR_SZ + 1,
				  sizeof(tempbuf) - (STRINGCOMPAREMODESTR_SZ + 1),
				  btree->params.stringcomparemode) +
					STRINGCOMPAREMODESTR_SZ + 1;
		if (sz < (int)sizeof(tempbuf) - 1)
		{
			tempbuf[sz] = ';';
			tempbuf[sz+1] = '\0';
		}
		sz++;				/* for `;' */
		needsz += sz;
		if (sz <= remainsz)
		{
			strcat(buffer, tempbuf);
			remainsz -= sz;
		}
	}

	switch (btree->params.indexValues)
	{
	case TX_INDEX_VALUES_DEFAULT_OLD:
		/* Legacy default for no-`indexvalues=...'-in-SYSINDEX.PARAMS;
		 * do not need to print it:
		 */
		break;
	case TXindexValues_All:
		/* No split values in index.  Optimization: we can
		 * skip printing `indexvalues=all' to SYSINDEX.PARAMS
		 * if we know the index would also have no split
		 * values with TX_INDEX_VALUES_DEFAULT_OLD.  This
		 * technically changes an `all' index to
		 * `splitstrlst', but if the actual splitting does not
		 * change (not done either way), it saves us some
		 * SYSINDEX.PARAMS room here:
		 */
		if (!TXbtreeHasSplitValues(indexFields, dbtbl,
					   TX_INDEX_VALUES_DEFAULT_OLD))
			break;			/* can skip printing */
		/* else fall through and print: */
	default:
		if ((unsigned)btree->params.indexValues >=
		    (unsigned)TXindexValues_NUM)
		{
			putmsg(MERR + UGE, fn,
				"Unknown indexvalues value %d for B-tree %s",
				(int)btree->params.indexValues,
				getdbffn(btree->dbf));
			return(-1);
		}
		sz = htsnpf(tempbuf, sizeof(tempbuf), "%s=%s;",
			    indexValuesStr,
			    TXindexValuesToStr(btree->params.indexValues));
		needsz += sz;
		if (sz <= remainsz)
		{
			strcat(buffer, tempbuf);
			remainsz -= sz;
		}
	}

	*textsz = needsz;
	return 0;
}

/******************************************************************/

int
TXtextParamsToBtparam(p, sysindexParams, indexPath, flags)
BTPARAM		*p;
CONST char	*sysindexParams;
CONST char	*indexPath;		/* (in) index path for messages */
int		flags; /* bit 0: yap  bit 1: recoverable errors */
/* Returns -2 on severe error, -1 on recoverable error (if `flags & 0x2'),
 * 0 if ok.
 * Note: see also TXindOptsProcessOptions(), which parses the
 * `WITH ...' index options versions of these options.
 * See also BTPARAM_ACTIVATE(), BTPARAM_DEACTIVATE().
 */
{
	static CONST char	fn[] = "TXtextParamsToBtparam";
	static CONST char	willTryToUse[] = "; will try to use index but results may be incorrect";
	char *param;
	char *cparams = CHARPN;
	CONST char	*paramName = CHARPN;
	size_t	n;
	int	ret = 0, res, yap = (flags & 0x1), recover = (flags & 0x2);
	TXCFF	stringCompareMode;

	if(!p)
		goto err;
	if(!sysindexParams)
		goto err;
	cparams = TXstrdup(TXPMBUFPN, __FUNCTION__, sysindexParams);
	if(!cparams)
		goto err;
	for(param = strtok(cparams, ";"); param; param = strtok(NULL, ";"))
	{
		n = strcspn(param, "=;");
		if (n == 14 && strncmp(param, "max_index_text", 14) == 0)
		{
			sscanf(param, "max_index_text=%d;", &p->max_index_text);
		}
		else if (n == STRINGCOMPAREMODESTR_SZ &&
			 strncmp(param, stringCompareModeStr, STRINGCOMPAREMODESTR_SZ) == 0)
		{
			paramName = stringCompareModeStr;
			param += STRINGCOMPAREMODESTR_SZ + 1;  /* +1 for `=' */
			/* SYSINDEX.PARAMS stringcomparemode is absolute,
			 * because we do not know the current/default
			 * setting in effect when the index was created:
			 */
			res = TXstrToTxcff(param, CHARPN, (TXCFF)(-1),
					  (TXCFF)(-1), (TXCFF)(-1), 1,
					  (TXCFF)(-1), &stringCompareMode);
			p->stringcomparemode = (int)stringCompareMode;
			if (!res)
			{
			badParamValue:
				if (yap)
					putmsg((recover ? MWARN : MERR) + UGE, fn, "Index %s uses unknown %s value `%s': Incompatible, use later Texis release%s",
						indexPath, paramName, param,
						(recover ? willTryToUse : ""));
				ret = (recover ? -1 : -2);	/* error */
			}
		}
		else if (n == INDEXVALUESSTR_SZ &&
			 strnicmp(param, indexValuesStr, INDEXVALUESSTR_SZ)==0)
		{
			TXindexValues	iv;
			paramName = indexValuesStr;
			param += INDEXVALUESSTR_SZ + 1;	/* +1 for `=' */
			iv = TXstrToIndexValues(param, CHARPN);
			if (iv == (TXindexValues)(-1)) goto badParamValue;
			p->indexValues = iv;
		}
		else				/* unknown setting */
		{
			/* An unknown setting is an error, because we cannot
			 * know if we are compatible with the index or not:
			 */
			if (yap)
				putmsg((recover ? MWARN : MERR) + UGE, fn, "Index %s has unknown SYSINDEX.PARAMS setting `%s': Incompatible, use later Texis release%s",
					indexPath, param,
					(recover ? willTryToUse : ""));
			ret = (recover ? -1 : -2);	/* error */
		}
	}
	goto done;

err:
	ret = -2;				/* severe error */
done:
	TXfree(cparams);
	return ret;
}

int
bttexttoparam(BTREE *btree, char *params)
{
	if (!btree) return(-1);
	/* Allow SYSINDEX.PARAMS parse errors if we are only SELECTing
	 * from the index, on the theory that it is probably still
	 * largely usable, and using it is probably better than a linear
	 * search.  But if we are modifying the index, do not allow errors,
	 * or we might corrupt the index.  Report errors regardless:
	 */
	return(TXtextParamsToBtparam(&btree->params, params,
                                     getdbffn(btree->dbf),
		(((btree->openMode & (O_WRONLY | O_RDWR)) ? 0 : 0x2) | 0x1))
		< -1 ? -1 : 0);
}

int
TXbtreeScoreIndex(sysindexFields, sysindexParams, ddicOptions, indexPath,
		  dbtbl, fldOp, param, paramIsRHS)
CONST char	*sysindexFields;	/* (in) SYSINDEX.FIELDS text */
CONST char	*sysindexParams;	/* (in) SYSINDEX.PARAMS text */
CONST int	*ddicOptions;		/* (in) DDIC.options */
CONST char	*indexPath;		/* (in) index path for messages */
DBTBL		*dbtbl;			/* (in) table the index is on */
int		fldOp;			/* (in) FOP_... of search */
FLD		*param;			/* (in) parameter */
int		paramIsRHS;		/* (in) nonzero: param is right-side*/
/* Returns an index score for SYSINDEX.PARAMS text `sysindexParams' for
 * B-tree index `indexPath', based on comparing it to current settings.
 * 0 means index cannot be used at all.  Higher numbers indicate greater
 * preference.  Value returned must be in range 0-TX_INDEX_SUBSCORE_MAX.
 */
{
	int	score, paramRes, doSubset;
	int   	btreeHasSplitValues;
	BTPARAM	params;
	TXCFF	modeCur, modeIdx, caseStyleCur, caseStyleIdx;
	size_t	paramLen, paramNumItems;

	BTPARAM_INIT(&params);			/* get defaults */
	/* Assume we are scoring indexes for read-only (PM_SELECT)
	 * usage, and try to recover from SYSINDEX.PARAMS errors: maybe
	 * try to use such an index for reading later (but see score
	 * deduction below).  Also suppress messages, since any
	 * failure may be for an index we are not going to choose
	 * anyway, and the index we do choose will be opened (and
	 * errors reported) separately elsewhere:
	 */
	paramRes = TXtextParamsToBtparam(&params, sysindexParams, indexPath,
                                         0x2);
	if (paramRes < -1) return(0);		/* severe error parsing */
	score = 1;				/* at least non-zero */
	if (globalcp == APICPPN) globalcp = TXopenapicp();
	/* We prefer an index that agrees with current settings,
	 * so that we search as expected (i.e. if case-sensitive and
	 * ignore-case indexes are both present, select appropriate),
	 * but use it even if it does not agree (probably better than
	 * linear search).  The more it agrees, the better:
	 * NOTE: see also TX3dbiScoreIndex():
	 */
	if (params.max_index_text == ddicOptions[DDIC_OPTIONS_MAX_INDEX_TEXT])
		score += 200;
	else if (params.max_index_text > ddicOptions[DDIC_OPTIONS_MAX_INDEX_TEXT])
		score += 100;

	modeCur = TXCFF_GET_CASEMODE(globalcp->stringcomparemode);
	modeIdx = TXCFF_GET_CASEMODE(params.stringcomparemode);
	if (modeCur == modeIdx)
		score += 10;
	/* unicodemono and unicodemulti mode are more similar than
	 * either are to ctype, so if both modes are unicode,
	 * give partial credit:
	 */
	else if (modeCur != TXCFF_CASEMODE_CTYPE &&
		 modeIdx != TXCFF_CASEMODE_CTYPE)
		score += 5;

	/* ISO-8859-1 vs. UTF-8 is a major issue; higher score: */
	score += ((params.stringcomparemode & TXCFF_ISO88591) ==
		  (globalcp->stringcomparemode & TXCFF_ISO88591) ? 50 : 0);

	caseStyleCur = TXCFF_GET_CASESTYLE(globalcp->stringcomparemode);
	caseStyleIdx = TXCFF_GET_CASESTYLE(params.stringcomparemode);
	if (caseStyleCur == caseStyleIdx)
		score += 10;
	/* lowercase is similar to ignorecase, and uppercase is similar
	 * to titlecase:
	 */
	else if ((caseStyleCur == TXCFF_CASESTYLE_IGNORE ||
		  caseStyleCur == TXCFF_CASESTYLE_LOWER) &&
		 (caseStyleIdx == TXCFF_CASESTYLE_IGNORE ||
		  caseStyleIdx == TXCFF_CASESTYLE_LOWER))
		score += 8;
	else if ((caseStyleCur == TXCFF_CASESTYLE_UPPER ||
		  caseStyleCur == TXCFF_CASESTYLE_TITLE) &&
		 (caseStyleIdx == TXCFF_CASESTYLE_UPPER ||
		  caseStyleIdx == TXCFF_CASESTYLE_TITLE))
		score += 8;

	/* Bug 4070, Bug 3677: */
	/* N = multi-item type            intersect = intersect-type operator
	 * 1 = single-item type           subset = subset-type operator
	 * UR = unique result recids      PT = post-proc needed against table
	 * UP = unique param list first   PI = post-proc needed against index
	 * B = can do bubble-up too (if UR: need de-dup based on prev recid)
	 *
	 * param IN col   Can use split-val index   Can use all-val index
	 *   1       1    yes B                             yes B
	 *   1       N    yes UR B                          no
	 *   N       1    yes (intersect: UP)               yes (intersect: UP)
	 *   N       N    yes (intersect: UP; subset: PT)   no
	 *
	 * col IN param  Can use split-val idx   Can use all-val index
	 *  1       1    yes B                          yes B
	 *  1       N    yes UP                         yes UP
	 *  N       1    yes (subset: PT) intersect:B   intersect:no subset:yes
	 *  N       N    yes (UR; subset: PT)        intersect:no subset:yes PI
	 *
	 * Note that for param (as opposed to column), we could
	 * check if the actual *value* is multi- vs. single-item, in
	 * case that makes a difference for index usability (i.e. a
	 * multi-item-type param that contains one value).  Does not
	 * make a difference?: above tables show param-1 and param-N
	 * yes/no results are the same for the same col-{1|N} value,
	 * though actual usage of index (e.g. post-proc) may vary.
	 * So no need to check multi-ness of param value here?
         * wrong: see below for empty-set parameter check
	 */

	doSubset = (fldOp == FOP_IS_SUBSET ||
		    (fldOp == FOP_IN && TXApp->inModeIsSubset));
	switch (fldOp)
	{
	case FOP_IN:
	case FOP_IS_SUBSET:
	/* FOP_INTERSECT returns a set, not boolean; cannot use index */
	/* FOP_INTERSECT_IS_EMPTY is negation; cannot use index */
	case FOP_INTERSECT_IS_NOT_EMPTY:
	case FOP_MAT:
		/*   IN/SUBSET/INTERSECT looks at individual values of
		 * multi-item types, regardless of whether `inmode' is
		 * `subset' or `intersect'.  So iff a multi-item type
		 * is indexed, it must be split if `param IN column';
		 * for `column IN param', an all-val index might
		 * sometimes be usable (see chart above).  See
		 * parallel check in ixbteqindex(), ixbtindex().
		 * Similar logic for MATCHES.
		 *   Exception for subset ops with an empty-set
		 * parameter: if param is RHS, ixbteqindex() will need
		 * to look up just empty sets -- which are missing
		 * from split-values indexes Bug 4143; if param is
		 * LHS, ixbteqindex() will need to return all rows --
		 * but empty-set rows are missing from split-values index.
		 * In either case, subset must use *non*-split-values.
		 */
		if (!TXbtreeIsOnMultipleItemType(sysindexFields, dbtbl))
			break;
		btreeHasSplitValues = TXbtreeHasSplitValues(sysindexFields,
						    dbtbl, params.indexValues);
		paramNumItems = TXfldNumItems(param);
		switch (param->type & DDTYPEBITS)
		{
		case FTN_CHAR:
		case FTN_BYTE:
			/* Bug 3677 #12 empty str = empty set:
			 * Bug 3677 #13 not for FOP_IN:
			 */
			getfld(param, &paramLen);
			if (fldOp != FOP_IN && paramLen == 0)
				paramNumItems = 0;
			break;
		}
		/*   For `anyColumn SUBSET|IN-subset $oneItemParam',
		 * we prefer indexvalues=all index, since ixbteqindex()
		 * will only need to look up empty-strlst and
		 * single-string-strlst values -- both are possble in
		 * an indexvalues=all index.  A split-values index would
		 * need post-processing for the former, and cannot do
		 * the latter at all (Bug 4143), though we use it anyway
		 * (empty-strlst values may not be relevant to user)
		 * at a lower preference:
		 */
		if (doSubset && paramIsRHS && paramNumItems == 1)
		{
			/* all-values index preferred over split-value,
			 * but still allow the latter:
			 */
			if (btreeHasSplitValues)
			{
				score -= 100;
				if (score <= 0) score = 1;
			}
		}
		else if (doSubset && paramNumItems == 0)
		{				/* must be non-split-values */
			if (btreeHasSplitValues) return(0);
		}
		else
		{				/* must be split-values */
			if (!btreeHasSplitValues) return(0);
		}
		break;
	default:
		if (TXApp->strlstRelopVarcharPromoteViaCreate)
		{
		/* `=' now compares whole strlst, not individual elements;
		 * needs an index where values are not split.  Same for
		 * `<', `>' etc.:
		 */
		if ((fldOp & FOP_CMP) &&	/* FOP_EQ, FOP_LT etc. */
		    TXbtreeHasSplitValues(sysindexFields, dbtbl,
					  params.indexValues))
			return(0);		/* probably unusable index */
		/* WTF non-FOP_CMP ops? */
		}
		break;
	}

	/* Take off points if there was an error parsing params: */
	if (paramRes != 0) score -= 50;		/* WAG */

	if (score < 1) score = 1;		/* min. openable-index score*/

	return(score);
}

/******************************************************************/

BTREE_SEARCH_MODE
btsetsearch(BTREE *btree, BTREE_SEARCH_MODE searchmode)
{
	BTREE_SEARCH_MODE old_searchmode;

	old_searchmode = btree->search_pos;
	btree->search_pos = searchmode;
	return old_searchmode;
}

/* ------------------------------------------------------------------------ */

int
TXbtreeChangeLocOrKey(bt, key, keySize, oldLoc, newLoc, newKey)
BTREE	*bt;		/* (in/out) B-tree to update */
void	*key;		/* (in) key to search for */
int	keySize;	/* (in) size of key */
BTLOC	oldLoc;		/* (in) old (existing) location with `key' */
BTLOC	newLoc;		/* (in) new location to set */
byte	*newKey;	/* (in) new key to set instead */
/* Changes BTLOC of `key'/`oldLoc' item to `newLoc'.
 * >>> NOTE: `newLoc' must have same relative ordering as `oldLoc',  <<<
 * >>> i.e. would have the same Metamorph token number as `oldLoc',  <<<
 * >>> and/or same sort order as `key' (if `newKey'), once all       <<<
 * >>> recids in table are changed.                                  <<<
 * >>> Otherwise B-tree may end up out of order and invalid/corrupt. <<<
 * Returns 2 if found and updated, 1 if not found, 0 on error.
 */
{
	int	ret;

	BTPARAM_ACTIVATE(&bt->params);
	if (bt->flags & BT_FIXED)
		ret = TXfbtreeChangeLocOrKey(bt, key, keySize, oldLoc, newLoc,
						newKey);
	else
		ret = TXvbtreeChangeLocOrKey(bt, key, keySize, oldLoc, newLoc,
						newKey);
	BTPARAM_DEACTIVATE(&bt->params);
	return(ret);
}

/****************************************************************************/

int
btdump(t, flags)
BTREE   *t;     /* (in) tree to dump */
int     flags;  /* (in) see vbtdump() */
/* Dumps `t' according to `flags' to "...-NNNNa.*" files: (see texglob.c:):
 *   bit  0: issue putmsg about where dump file(s) are
 *   bit  1: .btree:   copy of in-mem BTREE struct
 *   bit  2: .btr:     copy of .btr file
 *   bit  3: .cache:   page cache: {BCACHE BPAGE} x t->cachesize
 *   bit  4: .his:     history: {BTRL} x t->cachesize
 *   bit  5: .core:    fork and dump core
 * For human-readable list of B-tree items, see TXbtreeDump().
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "btdump";
  static CONST char     cannotopendump[] =
    "Cannot open B-tree dump file %s: %s";
  int                   srcfh = -1, destfh = -1, ret, tries, i;
  size_t                rd, wr;
  char                  *frend, *root, *dirend;
  PID_T                 pid;
  BTLOC                 loc;
#ifndef _WIN32
  SIGTYPE (CDECL *prevhandler) ARGS((int sig));
#endif /* !_WIN32 */
  char                  file[PATH_MAX];
  char                  buf[PATH_MAX];

  BTPARAM_ACTIVATE(&t->params);
  if (t->dbf->dbftype != DBF_KAI)               /* need real filename */
    {
      putmsg(MERR, fn, "Cannot dump B-tree %s: Unsupported DBF type %d",
             getdbffn(t->dbf), (int)t->dbf->dbftype);
      goto err;
    }
  TXstrncpy(file, getdbffn(t->dbf), sizeof(file) - 1);
  frend = file + strlen(file);
  if (frend - file >= 4 && strcmpi(frend - 4, ".btr") == 0) frend -= 4;
  *(frend++) = '-';
  *frend = '\0';
  while (frend > file && *frend != PATH_SEP) frend--;
  if (*frend != PATH_SEP)
    {
      putmsg(MERR, fn, "Cannot find dir separator in B-tree path %s", file);
      goto err;
    }
  dirend = frend;
  *(frend++) = '\0';                            /* split into dir + file */
  if ((root = TXtempnam(file, frend, CHARPN)) == CHARPN) goto err;
  TXstrncpy(file, root, sizeof(file));
  root = TXfree(root);
  frend = file + strlen(file);
  if ((file + sizeof(file)) - frend < 10)       /* we append to it */
    {
      putmsg(MERR + MAE, fn, "Out of buffer space");
      goto err;
    }

  if (flags & (1 << 0))                         /* issue message */
    putmsg(MINFO, fn, "Dumping B-tree %s to %s", getdbffn(t->dbf), file);

  if (flags & (1 << 1))                         /* .btree file */
    {
      TXstrncpy(frend, ".btree", (file + sizeof(file)) - frend);
      destfh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "B-tree dump file",
			 file, TXrawOpenFlag_None,
			 (TX_O_BINARY | O_WRONLY | O_CREAT | O_TRUNC), 0666);
      if (destfh < 0) goto err;
      if (tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False, (byte *)t,
		      sizeof(BTREE), TXbool_False) != sizeof(BTREE))
        goto err;
      close(destfh);
      destfh = -1;
    }

  if (flags & (1 << 2))                         /* .btr file */
    {
      TXstrncpy(frend, ".btr", (file + sizeof(file)) - frend);
      destfh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "B-tree dump file",
			 file, TXrawOpenFlag_None,
			 (TX_O_BINARY | O_WRONLY | O_CREAT | O_TRUNC), 0666);
      if (destfh < 0) goto err;
      srcfh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "B-tree",
			getdbffn(t->dbf), TXrawOpenFlag_None,
			(TX_O_BINARY | O_RDONLY), 0666);
      if (srcfh < 0) goto err;
      for (;;)
        {
          tries = 0;
          do
            {
              TXseterror(0);
              rd = (size_t)read(srcfh, buf, sizeof(buf));
              tries++;
            }
          while (rd == (size_t)(-1) && tries < 25 &&
                 TXgeterror() == TXERR_EINTR);
          if (rd == (size_t)(-1))               /* error */
            {
              putmsg(MERR + FRE, fn, "Cannot read from B-tree %s: %s",
                     getdbffn(t->dbf), TXstrerror(TXgeterror()));
              goto err;
            }
          if (rd == 0) break;                   /* EOF */
          if (tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False, (byte *)buf,
			  rd, TXbool_False) != rd)
            goto err;
        }
      close(srcfh);
      srcfh = -1;
      close(destfh);
      destfh = -1;
    }

  if (flags & (1 << 3))                         /* .cache file */
    {
      TXstrncpy(frend, ".cache", (file + sizeof(file)) - frend);
      destfh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "B-tree dump file",
			 file, TXrawOpenFlag_None,
			 (TX_O_BINARY | O_WRONLY | O_CREAT | O_TRUNC), 0666);
      if (destfh < 0) goto err;
      for (i = 0; i < t->cachesize; i++)
        {
          if (tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False,
			  (byte*)&t->cache[i], sizeof(BCACHE), TXbool_False)
	      != sizeof(BCACHE))
            goto err;
          if (t->cache[i].page != BPAGEPN)
            wr = tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False,
                         (byte *)t->cache[i].page, t->pagesize, TXbool_False);
          else                                  /* write blank placeholder */
            {
              memset(buf, 0, sizeof(buf));
              for (wr = 0; wr < (size_t)t->pagesize; wr += rd)
                {
                  rd = t->pagesize - wr;
                  if (rd > sizeof(buf)) rd = sizeof(buf);
                  if (tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False,
				  (byte *)buf, rd, TXbool_False) !=
                      rd)
                    goto err;
                }
            }
        }
      close(destfh);
      destfh = -1;
    }

  if (flags & (1 << 4))                         /* .his file */
    {
      TXstrncpy(frend, ".his", (file + sizeof(file)) - frend);
      destfh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "B-tree dump file",
			 file, TXrawOpenFlag_None,
			 (TX_O_BINARY | O_WRONLY | O_CREAT | O_TRUNC), 0666);
      if (destfh < 0) goto err;
      for (i = 0; i < t->cachesize; i++)
        {
          if (tx_rawwrite(TXPMBUFPN, destfh, file, TXbool_False,
			  (byte *)&t->his[i], sizeof(BTRL), TXbool_False) !=
	      sizeof(BTRL))
            goto err;
        }
      close(destfh);
      destfh = -1;
    }

  if (flags & (1 << 5))                         /* core file */
    {
#ifdef _WIN32
      putmsg(MERR + UGE, fn, "Core file unsupported on this platform");
      goto err;
#else /* !_WIN32 */
      prevhandler = signal(SIGCLD, SIG_IGN);    /* ignore SIGCLD for now */
      switch (pid = TXfork(TXPMBUFPN, "btdump core file process", NULL, 0x0))
        {
        case -1:                                /* failed */
          signal(SIGCLD, prevhandler);
          goto err;
        case 0:                                 /* child process */
          signal(SIGSEGV, SIG_DFL);
          *dirend = '\0';                       /* chop `file' to just dir */
          chdir(file);
          *((volatile char *)0) = '\0';
          _exit(TXEXIT_COREDUMPREQUESTED);
        default:                                /* parent */
          if (waitpid(pid, &i, 0) == pid)
            {
              strcpy(buf, file);
              TXstrncpy(buf + (frend - file), ".core",
                        sizeof(buf) - (frend - file));
              TXstrncpy(dirend + 1, "core",     /* WTF stomping `file' */
                        (file + sizeof(file)) - (dirend + 1));
              rename(file, buf);
            }
          signal(SIGCLD, prevhandler);          /* restore previous handler */
          break;
        }
#endif /* !_WIN32 */
    }

  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (srcfh != -1) close(srcfh);
  if (destfh != -1) close(destfh);
  if (t->flags & BT_LOGOPS)
    {
      TXsetrecid(&loc, t->root);                /* log root: more useful */
      btlogop(t, 0, NULL, &loc, "dump", (ret ? "ok" : "fail"));
    }
  BTPARAM_DEACTIVATE(&t->params);
  return(ret);
}

/******************************************************************/

int
btlogop(BTREE *bt, int n, void *data, BTLOC *locn, CONST char *op,
	CONST char *result)
/* Logs operation `op' to B-tree log file named after `bt'.
 * Note that bt->usr->tbl1 is modified, which is ok if bt->cmp == fldcmp
 * and caller is about to use bt->cmp anyway (i.e. log *before* action).
 * Internal use.
 * Returns -1 on error, 0 on success.
 */
{
	static CONST char	fn[] = "btlogop";
	FLDCMP *fc;
	FLD	*fld;
	TXOF	orderFlags;
	STRBUF *strbuf = STRBUFPN;
	int i, fh = -1, res, ret;
	double	tim;
	char	*d, *s, tmp[PATH_MAX], ntmp[EPI_OS_INT_BITS/3 + 3], ltmp[EPI_OS_INT_BITS/3+3];
	char	dbtbltmp[EPI_OS_LONG_BITS/4+3];
	char	rltmp[EPI_OS_INT_BITS/3+3], wltmp[EPI_OS_INT_BITS/3+3];
	char	locnTmp[EPI_HUGEUINT_BITS/4 + 10];
#define PR(buf, fmt, type, ptr)					\
	if (ptr)						\
		htsnpf(buf, sizeof(buf), fmt, (type)*(ptr));	\
	else							\
	{							\
		buf[0] = '-';					\
		buf[1] = '\0';					\
	}

	if (!(bt->flags & BT_LOGOPS))		/* logging not enabled */
		goto ok;
	if (bt->dbf->dbftype != DBF_KAI)	/* no filename to log to */
		goto err;
	fc = bt->usr;
	if (!fc || !data || bt->cmp != (btcmptype)fldcmp)
		s = (data ? "? ?" : "- -");	/* no FLDCMP available */
	else
	{
		strbuf = openstrbuf();
		buftofld(data, fc->tbl1, n);
		for (i = 0; (size_t)i < fc->tbl1->n; i++)
		{
			if (i > 0) addstrbuf(strbuf, ",", 1);
			fld = getfldn(fc->tbl1, i, &orderFlags);
			if (orderFlags & OF_DESCENDING)
				addstrbuf(strbuf, "D", -1);
			if (orderFlags & OF_IGN_CASE)
				addstrbuf(strbuf, "I", -1);
			if (orderFlags & OF_DONT_CARE)
				addstrbuf(strbuf, "X", -1);
			if (orderFlags & OF_PREFER_END)
				addstrbuf(strbuf, "E", -1);
			if (orderFlags & OF_PREFER_START)
				addstrbuf(strbuf, "S", -1);
		}
		addstrbuf(strbuf, " ", -1);
		for (i = 0; (size_t)i < fc->tbl1->n; i++)
		{
			if (i > 0) addstrbuf(strbuf, ",", 1);
			fld = getfldn(fc->tbl1, i, &orderFlags);
			addstrbuf(strbuf, fldtostr(fld), -1);
		}
		s = strbuf->data;
	}

	tim = TXgettimeofday();
	TXstrncpy(tmp, getdbffn(bt->dbf), sizeof(tmp) - 4);
	d = tmp + strlen(tmp);
	if (d - tmp >= BTREE_EXT_SZ &&
	    strcmp(d - BTREE_EXT_SZ, BtreeExt) == 0)
		d -= BTREE_EXT_SZ;
	strcpy(d, ".log");
	if ((fh = open(tmp, (O_WRONLY | O_APPEND | O_CREAT), 0666)) == -1)
	{
		putmsg(MERR + FOE, fn, "Cannot open B-tree log file %s: %s",
			tmp, TXstrerror(TXgeterror()));
		goto err;
	}
	if (data)
		htsnpf(ntmp, sizeof(ntmp), "%d", n);
	else
	{
		ntmp[0] = '-';
		ntmp[1] = '\0';
	}
	PR(ltmp, "%d", int, TXbtreelog_srcline)
	if (TXbtreelog_dbtbl)
	{
		htsnpf(rltmp, sizeof(rltmp), "%d", TXbtreelog_dbtbl->nireadl);
		htsnpf(wltmp, sizeof(wltmp), "%d", TXbtreelog_dbtbl->niwrite);
		htsnpf(dbtbltmp, sizeof(dbtbltmp), "%p", TXbtreelog_dbtbl);
	}
	else
	{
		dbtbltmp[0] = rltmp[0] = wltmp[0] = '-';
		dbtbltmp[1] = rltmp[1] = wltmp[1] = '\0';
	}
	if (locn)
		htsnpf(locnTmp, sizeof(locnTmp), "0x%wx",
			(EPI_HUGEUINT)TXgetoff2(locn));
	else
	{
		locnTmp[0] = '-';
		locnTmp[1] = '\0';
	}
	/* Log file format:
 * date time script:line PID dbtbl rl wl Btree act res S I T recid keysz key
	 *
	 * dbtbl  DBTBL pointer
	 * rl     number of index read locks (DBTBL.nireadl)
	 * wl     number of index write locks (DBTBL.niwrite)
	 * Btree  B-tree pointer
	 * act    Action:
	 *     open create close RDroot dump   recid is root page offset
	 *     WRhdr              write header: recid is root page offset
	 *     WRdd               write DD: recid is DD offset
	 *                        (read DD @ open is not logged)
	 *     delete append insert search  recid is for given key
	 *     RDpage WRpage CRpage FRpage  read/write/create/free page
	 *     FRdbf              free DBF block (unknown if page/header/DD)
	 * res    ok=success, fail=failure, dup=duplicate, miss=not-found
	 * S      B-tree search mode: B=before, F=find, A=after
	 * I      DBTBL->indguar flag 1=completely indexable query
	 * T      DBTBL->index.type   N=DBIDX_NATIVE (bubble-up)
	 *                            M=DBIDX_MEMORY (RAM B-tree)
	 *                            C=DBIDX_CACHE (RAM cache?)
	 * recid  See notes under `act' field
	 * Unavailable/not-applicable fields logged as `-'
	 */
	i = htsnpf(tmp, sizeof(tmp),
"%at.%06d %4s:%-4s %5u %-10s %s %s %-#10lx %-9s %-4s %c %c %c %6s %3s %s\n",
		"%Y-%m-%d %H:%M:%S", (time_t)tim,
		(int)(((double)1000000.0)*(tim - floor(tim))),
		(TXbtreelog_srcfile != CHARPPN && *TXbtreelog_srcfile!=CHARPN
			? TXbasename(*TXbtreelog_srcfile) : "-"),
		ltmp, (unsigned)TXgetpid(0), dbtbltmp, rltmp, wltmp,
		(long)bt, op, result,
		(bt->search_pos == BT_SEARCH_FIND ? 'F' :
		 (bt->search_pos == BT_SEARCH_BEFORE ? 'B' :
                  (bt->search_pos == BT_SEARCH_AFTER ? 'A' : '?'))),
		(TXbtreelog_dbtbl ? (TXbtreelog_dbtbl->indguar >= 0 &&
		 TXbtreelog_dbtbl->indguar <= 9 ?
		 '0' + TXbtreelog_dbtbl->indguar : '?') : '-'),
		(TXbtreelog_dbtbl &&
		 TXbtreelog_dbtbl->index.type != DBIDX_MEMUNK ?
		 (TXbtreelog_dbtbl->index.type == DBIDX_NATIVE ? 'N' :
		  (TXbtreelog_dbtbl->index.type == DBIDX_MEMORY ? 'M' :
		   (TXbtreelog_dbtbl->index.type == DBIDX_CACHE ? 'C' : '?')))
		 : '-'),
		locnTmp, ntmp, s);
	if (i > (int)sizeof(tmp) - 1)		/* msg too long */
	{
		strcpy(tmp + sizeof(tmp) - 5, "...\n");
		i = sizeof(tmp) - 1;
	}
	res = tx_rawwrite(TXPMBUFPN, fh, getdbffn(bt->dbf) /* WTF */,
			  TXbool_False, (byte *)tmp, i, TXbool_False);
	if (res != i) goto err;
ok:
	ret = 0;
	goto done;

err:
	ret = -1;
done:
	if (fh >= 0) close(fh);
	closestrbuf(strbuf);
	return(ret);
#undef PR
}

size_t
TXbtreePrFlags(BTREE *bt, char *buf, size_t bufSz)
/* Prints BT_... flags of `bt' to `buf', for putmsgs.
 * Returns would-be size of `buf' (if over `bufSz', ellipsis used).
 */
{
	char		*d, *e;
	const char	*s;
	unsigned	flags = bt->flags;
	int		i;
	char		intBuf[EPI_OS_UINT_BITS/4 + 10];

	if (!buf || bufSz <= 0) bufSz = 0;
	e = buf + bufSz;
	d = buf;
#define DO_FLAG(flag)				\
	if (flags & (flag))			\
	{					\
		if (d > buf)			\
		{				\
			if (d < e) *d = ' ';	\
			d++;			\
		}				\
		for (s = #flag; *s; s++, d++)	\
			if (d < e) *d = *s;	\
		flags &= ~(flag);		\
	}
	DO_FLAG(BT_FIXED);
	DO_FLAG(BT_UNSIGNED);
	DO_FLAG(BT_LINEAR);
	DO_FLAG(BT_LOGOPS);
	DO_FLAG(BT_NOCOMP);
	DO_FLAG(BT_SHAREDBF);
	DO_FLAG(BT_EXCLUSIVEACCESS);
	if (flags)
	{
		if (d > buf)
		{
			if (d < e) *d = ' ';
			d++;
		}
		htsnpf(intBuf, sizeof(intBuf), "0x%x", flags);
		for (s = intBuf; *s; s++, d++)
			if (d < e) *d = *s;
	}
	if (d < e)
		*d = '\0';
	else					/* add ellipsis at end */
	{
		for (i = 0; i < 4 && e > buf; i++, e--)
			e[-1] = (i ? '.' : '\0');
	}
	return(d - buf);
#undef DO_FLAG
}

EPI_SSIZE_T
TXbtreeGetNumItemsDelta(BTREE *bt)
/* Returns change in number of items since B-tree was opened.
 */
{
	return(bt->numItemsDelta);
}

/* ------------------------------------------------------------------------ */

char *
TXbtreeTupleToStr(TXPMBUF *pmbuf, BTREE *bt, TBL **scratchTbl,
		  void *recBuf, size_t recBufSz)
/* Prints buftofld() data `recBuf', e.g. as `(123, 5)', to a string.
 * `scratchTbl' is optional; `*scratchTbl' must be NULL on first call,
 * and will be set to a scratch table to pass in on future calls with
 * same `bt'; closetbl() when all done.
 * For debug/trace.
 * Returns alloc'd string, or NULL on error.
 */
{
	char	*tupleBuf = NULL, *newBuf, *d, *e;
	size_t	tupleBufSz = 0, tupleBufUsedLen = 0;
	TBL	*tbl = NULL;
	DD	*dd = btreegetdd(bt);

	if (!(bt->flags & BT_FIXED))
	{
		if (scratchTbl && *scratchTbl)
			tbl = *scratchTbl;
		else if (dd)
		{
			tbl = createtbl(dd, TXNOOPDBF_PATH);
			if (!tbl) goto err;
		}
		/* else missing DD */
	}

	do
	{
		int		didTuple = 0;
		EPI_OFF_T	key;

		/* (re)alloc buffer: */
		tupleBufSz = (tupleBufSz << 1) + 1024;
		newBuf = (char *)TXrealloc(pmbuf, __FUNCTION__,
					   tupleBuf, tupleBufSz);
		if (!newBuf)
		{
#ifndef EPI_REALLOC_FAIL_SAFE
			tupleBuf = NULL;	/* realloc() freed it */
#endif /* EPI_REALLOC_FAIL_SAFE */
			goto err;
		}
		tupleBuf = newBuf;

		/* Print tuple: */
		d = tupleBuf;
		e = tupleBuf + tupleBufSz;
		if (bt->flags & BT_FIXED)
		{
			/* Check for proper fbtree key size: */
			if (recBufSz == sizeof(EPI_OFF_T)) goto doOff_t;
		}
		else				/* vbtree */
		{
			if (dd && buftofld(recBuf, tbl, recBufSz) == 0)
			{
				char	*tblTupleStr = NULL;

				/* Rank may be stored as ft_handle;
				 * print in base 10 for clarity:
				 */
				TXfldtostrHandleBase10++;
				tblTupleStr = TXtblTupleToStr(tbl);
				TXfldtostrHandleBase10--;
				if (!tblTupleStr) goto err;
				if (d < e) TXstrncpy(d, tblTupleStr, e - d);
				d += strlen(tblTupleStr);
				tblTupleStr = TXfree(tblTupleStr);
				didTuple = 1;
			}
		}
		if (!didTuple)
		{
			byte	*rec, *recEnd;
			char	*startD = d;

			/* Missing DD, or buftofld() failed, etc. */
			if (recBufSz == sizeof(EPI_OFF_T))
			{			/* assume off_t */
			doOff_t:
				memcpy(&key, recBuf, sizeof(EPI_OFF_T));
				d += htsnpf(d, e - d, "(0x%08wx = %6wd)",
					    (EPI_HUGEINT)key,
					    (EPI_HUGEINT)key);
				didTuple = 1;
			}
			else			/* hex dump */
			{
				rec = (byte *)recBuf;
				recEnd = rec + recBufSz;
				for ( ; rec < recEnd; rec++)
					d += htsnpf(d, (d < e ? e - d : 0),
						    " %02X", (unsigned)*rec);
				if (startD < e) *startD = '(';
				d += htsnpf(d, (d < e ? e - d : 0), ")");
			}
		}
		tupleBufUsedLen = (size_t)(d - tupleBuf);
	}
	while (tupleBufUsedLen >= tupleBufSz);

	goto finally;

err:
	tupleBuf = TXfree(tupleBuf);
finally:
	if (scratchTbl)
		*scratchTbl = tbl;
	else
		closetbl(tbl);
	tbl = NULL;
	if (tupleBuf && tupleBufUsedLen < tupleBufSz)	/* save mem */
		tupleBuf = (char *)TXrealloc(pmbuf, __FUNCTION__,
					     tupleBuf, tupleBufUsedLen + 1);
	return(tupleBuf);
}

/* ------------------------------------------------------------------------ */

int
TXbtreeDump(TXPMBUF *pmbuf, BTREE *bt, int indent, int recidsDecimalToo)
/* Dumps `bt' to `pmbuf', e.g. as `loc 0x... key (123, 5)'.
 * Preserves `bt' current position and history (page cache may change).
 * Prints recids in decimal too if `recidsDecimalToo'.
 * Returns 0 on error.
 */
{
	BTLOC	loc;
	size_t	scratchBufLen;
	char	*tupleStr = NULL;
	TBL	*scratchTbl = NULL;
	int	ret;
	BTHIST	*scratchHist = NULL;
	byte	scratchBuf[8192];		/* wtf alloc if small */

	if (!(scratchHist = dupbthist(bt))) goto err;
	/* Save current history/state, and use `scratchHist': */
	btsethist(scratchHist);

	rewindbtree(bt);
	while (scratchBufLen = sizeof(scratchBuf),
	       loc = btgetnext(bt, &scratchBufLen, scratchBuf, NULL),
	       TXrecidvalid(&loc))
	{
		tupleStr = TXbtreeTupleToStr(pmbuf, bt, &scratchTbl,
					     scratchBuf, scratchBufLen);
		if (!tupleStr) goto err;
		if (recidsDecimalToo)
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
				       "%*sloc 0x%08wx = %6wd key %s",
				       (int)indent, "",
				       (EPI_HUGEINT)TXgetoff2(&loc),
				       (EPI_HUGEINT)TXgetoff2(&loc),
				       tupleStr);
		else
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN,
				       "%*sloc 0x%08wx key %s",
				       (int)indent, "",
				       (EPI_HUGEINT)TXgetoff2(&loc),
				       tupleStr);
		tupleStr = TXfree(tupleStr);
	}

	ret = 1;
	goto finally;

err:
	ret = 0;
finally:
	tupleStr = TXfree(tupleStr);
	scratchTbl = closetbl(scratchTbl);
	if (scratchHist)
	{
		btunsethist(scratchHist);
		scratchHist = closebthist(scratchHist);
	}
	return(ret);
}

/* ------------------------------------------------------------------------ */

const char *
TXbtreeCmpFuncToStr(BTREE *bt)
{
	const char	*cmpFuncStr;

	if (bt->cmp == (btcmptype)fldcmp)
		cmpFuncStr = "fldcmp";
	else if (bt->cmp == TXfucmp)
		cmpFuncStr = "TXfucmp";
	else if (bt->cmp == TXfixedUnsignedReverseCmp)
		cmpFuncStr = "TXfixedUnsignedReverseCmp";
	else if (bt->cmp == TXfcmp)
		cmpFuncStr = "TXfcmp";
	else
		cmpFuncStr = "?";
	return(cmpFuncStr);
}

/*****************************************************************************/

#ifdef LTEST
typedef struct IT_tag {
  char  *vkey;
  EPI_OFF_T fkey;
  int   loc;
} IT;
#define ITPN    ((IT *)NULL)
#define ITPPN   ((IT **)NULL)

typedef int (ITCB) ARGS((void *usr, byte *key, size_t keysize, BTLOC loc,
                         EPI_OFF_T hpage));

static int fcmp ARGS((CONST void *a, CONST void *b));
static int
fcmp(a, b)
CONST   void *a, *b;
{
  return(((IT *)a)->fkey -  ((IT *)b)->fkey);
}

static int vcmp ARGS((CONST void *a, CONST void *b));
static int
vcmp(a, b)
CONST   void *a, *b;
{
  return(strcmp(((IT *)a)->vkey, ((IT *)b)->vkey));
}

static int fwalk_cb ARGS((void *usr, byte *key, size_t keysize, BTLOC loc,
                          EPI_OFF_T hpage));
static int
fwalk_cb(usr, key, keysize, loc, hpage)
void    *usr;
byte    *key;
size_t  keysize;
BTLOC   loc;
EPI_OFF_T   hpage;
{
  EPI_OFF_T off;

  (*(int *)usr)++;
  off = TXgetoff(&loc);
  printf("%08lX %3ld %08lX\n", (ulong)(*(EPI_OFF_T *)key),(long)off, (ulong)hpage);
  return(1);
}

static int vwalk_cb ARGS((void *usr, byte *key, size_t keysize, BTLOC loc,
                          EPI_OFF_T hpage));
static int
vwalk_cb(usr, key, keysize, loc, hpage)
void    *usr;
byte    *key;
size_t  keysize;
BTLOC   loc;
EPI_OFF_T   hpage;
{
  EPI_OFF_T off;

  (*(int *)usr)++;
  off = TXgetoff(&loc);
  printf("%-10s %3ld %08lX\n", (char *)key, (long)off, (ulong)hpage);
  if (strlen((char *)key) + 1 != keysize)
    printf("Error: key size mismatch\n");
  return(1);
}

static void walk_tree ARGS((BTREE *t, EPI_OFF_T root, int depth, ITCB *cb,
                            void *usr));
static void
walk_tree(t, root, depth, cb, usr)
BTREE   *t;
EPI_OFF_T   root;
int     depth;
ITCB    *cb;
void    *usr;
/* Calls cb(usr, key, loc) for each item in tree, in order.
 */
{
  BPAGE	*p;
  BITEM *it;
  int	i;

  if (root == (EPI_OFF_T)0L) return;                /* null page */

  if ((p = btgetpage(t, root)) == BPAGEPN)
    {
      btcantgetpage("walk_tree", t, root, (EPI_OFF_T)(-1), -1);
      return;
    }
  walk_tree(t, p->lpage, depth + 1, cb, usr);
  printf("----- page %08lX (lpage = %08lX) -----\n",
         (ulong)root, (ulong)p->lpage);
  for (i = 0; i < p->count; i++)
    {
      it = p->items + i;
      if (t->flags & BT_FIXED)
        cb(usr, (byte *)&it->vf.key, sizeof(it->vf.key), it->locn, it->hpage);
      else
        cb(usr, (char*)p+it->vf.var.key, it->vf.var.len, it->locn, it->hpage);
      walk_tree(t, it->hpage, depth + 1, cb, usr);
    }
  p = btreleasepage(t, root, p);
}

static int dumptree ARGS((BTREE *t));
static int
dumptree(t)
BTREE   *t;
{
  int   n;

  n = 0;
  printf("----- root is %08lX -----\n", t->root);
  walk_tree(t, t->root, 0, (t->flags & BT_FIXED) ? fwalk_cb : vwalk_cb, &n);
  printf("%d items\n", n);
  return(1);
}

void
main(argc, argv)
int     argc;
char    *argv[];
{
  static IT     items[] = {
    { "the",            0x100,    1 },
    { "ramble",         0x101,    2 },
    { "twins",          0x102,    3 },
    { "crab",           0x103,    4 },
    { "liverish",       0x104,    5 },
    { "scaly",          0x105,    6 },
    { "scorpions",      0x106,    7 },
    { "are",            0x107,    8 },
    { "good",           0x108,    9 },
    { "water",          0x109,   10 },
    { "fish",           0x110,   11 },
  };
#define NITEMS  (sizeof(items)/sizeof(items[0]))
  static char   file[] = "test";
  BTREE *t;
  IT    *it, *end;
  BTLOC loc;
  int   flags = 0, dump = 0;
  int   ret, i, psize;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "-d") == 0) dump = 1;
      else if (strcmp(argv[i], "-f") == 0) flags |= BT_FIXED;
      else
        {
          printf("unknown option %s\n", argv[i]);
          goto err;
        }
    }
  qsort(items, NITEMS, sizeof(items[0]), (flags & BT_FIXED) ? fcmp : vcmp);
  psize = (flags & BT_FIXED) ? 2 : 64;
  if ((t = openbtree(file, psize, 10, flags)) == BTREEPN) goto err;
  if (!dump && ioctlbtree(t, BTREE_IOCTL_LINEAR, NULL) < 0)
    goto err;
  if (dump)
    {
      dumptree(t);
      goto done;
    }
  for (end = items + NITEMS, it = items; it < end; it++)
    {
      loc.off = (EPI_OFF_T)it->loc;
      if (flags & BT_FIXED)
        ret = btspinsert(t, &loc, sizeof(it->fkey), &it->fkey, 100);
      else
        ret = btspinsert(t, &loc, strlen(it->vkey) + 1, it->vkey, 100);
      if (ret < 0) goto err;
    }

done:
err:
  t = closebtree(t);
}
#endif  /* LTEST */
