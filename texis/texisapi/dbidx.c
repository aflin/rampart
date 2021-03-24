/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"

#ifndef NO_BUBBLE_INDEX
/******************************************************************/
/*	Add a regular index to the table.
 */

#define verbose 0

/******************************************************************/

int
TXsetfldcmp(btree)
BTREE *btree;
/* Creates and attaches a FLDCMP object for the fields of `btree'.
 * Returns 0 on success, -1 on error.
 */
{
	FLDCMP *fc;

	fc = TXopenfldcmp(btree, TXOPENFLDCMP_INTERNAL_FLDOP);
	if (!fc)
	{
		return -1;
	}
	btree->usr = fc;
	btsetcmp(btree, (btcmptype)fldcmp);
	return 0;
}

/******************************************************************/

DBIDX *
createdbidx()
/* All DBIDX structs should be created by this function.
 */
{
	static CONST char	fn[] = "createdbidx";
	DBIDX			*dbidx;

	if (!(dbidx = (DBIDX *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(DBIDX))))
		return(DBIDXPN);
	cleardbidx(dbidx);
	return(dbidx);
}

void
cleardbidx(dbidx)
DBIDX	*dbidx;
/* Initializes an already-alloced DBIDX struct.
 * NOTE: see also closedbidx().
 */
{
	memset(dbidx, 0, sizeof(DBIDX));
	dbidx->rowsReturned = -1;
	dbidx->deDupRecids = 1;
	TXsetrecid(&dbidx->lrecid, -1);		/* Bug 4645 insurance */
}

/******************************************************************/

#ifdef NEW_I

#include "keyrec.h"

BTLOC
dbidxsearch(dbidx, sz, key)
DBIDX *dbidx;
size_t sz;
void *key;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxsearch");
		break;
	case DBIDX_BTREE:
	default:
		return btsearch(dbidx->btree, sz, key);
	}
}

/******************************************************************/

BTREE_SEARCH_MODE
dbidxsetsearch(dbidx, searchmode)
DBIDX *dbidx;
BTREE_SEARCH_MODE searchmode;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxsetsearch");
		break;
	case DBIDX_BTREE:
	default:
		return btsetsearch(dbidx->btree, searchmode);
	}
}

/******************************************************************/

int
dbidxinsert(dbidx, btloc, sz, key)
DBIDX *dbidx;
BTLOC *btloc;
size_t sz;
void *key;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
	{
		off_t	keyval;
		if(sz != sizeof(off_t))
			return -1;
		keyval = *(off_t *)key;
		return keyrecappend(dbidx->keyrec, btloc, NULL, keyval, NULL);
	}
	case DBIDX_BTREE:
	default:
		return btinsert(dbidx->btree, btloc, sz, key);
	}
}

/******************************************************************/

int
dbidxspinsert(dbidx, btloc, sz, key, split)
DBIDX *dbidx;
BTLOC *btloc;
size_t sz;
void *key;
int split;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
	{
		off_t	keyval;
		if(sz != sizeof(off_t))
			return -1;
		keyval = *(off_t *)key;
		return keyrecappend(dbidx->keyrec, btloc, NULL, keyval, NULL);
	}
	case DBIDX_BTREE:
	default:
		return btspinsert(dbidx->btree, btloc, sz, key, split);
	}
}

/******************************************************************/

int
dbidxappend(dbidx, btloc, sz, key, split, btbm)
DBIDX *dbidx;
BTLOC *btloc;
size_t sz;
void *key;
int split;
BTBM *btbm;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
	{
		off_t	keyval;
		if(sz != sizeof(off_t))
			return -1;
		keyval = *(off_t *)key;
		return keyrecappend(dbidx->keyrec, btloc, NULL, keyval, NULL);
	}
	case DBIDX_BTREE:
	default:
		return btappend(dbidx->btree, btloc, sz, key, split, btbm);
	}
}

/******************************************************************/

int
dbidxkeytype(dbidx, dtype)
DBIDX *dbidx;
int dtype;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxkeytype");
		break;
	case DBIDX_BTREE:
	default:
		if((dbidx->btree->flags & dtype) == dtype)
			return 1;
		return 0;
	}
}

/******************************************************************/

int
dbidxsetcmp(dbidx, cmpf)
DBIDX *dbidx;
btcmptype cmpf;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxsetcmp");
		break;
	case DBIDX_BTREE:
	default:
		return btsetcmp(dbidx->btree, cmpf);
	}
}

/******************************************************************/

int
dbidxrewind(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		return keyrecrewind(dbidx->keyrec);
	case DBIDX_BTREE:
	default:
		rewindbtree(dbidx->btree);
		break;
	}
	TXsetrecid(&dbidx->lrecid, -1);
	dbidx->nrecs = 0;
	return 0;
}

/******************************************************************/

int
dbidxflush(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		break;
	case DBIDX_BTREE:
	default:
		btflush(dbidx->btree);
	}
	return 0;
}

/******************************************************************/

int
dbidxsort(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		return keyrecsort(dbidx->keyrec);
	case DBIDX_BTREE:
	default:
		break;
	}
	return 0;
}
/******************************************************************/

int
dbidxsetmarker(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxsetmarker");
		break;
	case DBIDX_BTREE:
	default:
		btreesetmarker(dbidx->btree);
	}
	return 0;
}

/******************************************************************/

DD *
dbidxgetdd(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxgetdd");
		break;
	case DBIDX_BTREE:
	default:
		return btreegetdd(dbidx->btree);
	}
}

/******************************************************************/

int
dbidxgetpercentage(dbidx)
DBIDX *dbidx;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		putmsg(999, NULL, "dbidxgetpercentage");
		break;
	case DBIDX_BTREE:
	default:
		btgetpercentage(dbidx->btree);
	}
	return 0;
}

/******************************************************************/

DBIDX *
dbidxclose(dbidx)
DBIDX *dbidx;
{
	if(!dbidx)
		return dbidx;
	if(dbidx->btree)
	{
		switch(dbidx->itype)
		{
		case DBIDX_KEYREC:
			dbidx->keyrec = keyrecclose(dbidx->keyrec);
			break;
		case DBIDX_BTREE:
		default:
			return closedbidx(dbidx);
		}
	}
	if(dbidx->alloced)
	{
		dbidx = TXfree(dbidx);
		return NULL;
	}
	return dbidx;
}

/******************************************************************/

DBIDX *
dbidxdup(dbidx)
DBIDX *dbidx;
{
	static CONST char Fn[]="dbidxdup";
	DBIDX *rc;

	if(!dbidx->btree)
		return NULL;
	rc = createdbidx();
	if(!rc) return(DBIDXPN);
	*rc = *dbidx;
	rc->alloced = 1;
	return rc;
}

/******************************************************************/

DBIDX *
dbidxfrombtree(btree, type)
BTREE *btree;
int type;	/* DBIDX_MEMT */
{
	static CONST char Fn[]="dbidxfrombtree";
	DBIDX *rc;

	rc = createdbidx();
	if(!rc) return(DBIDXPN);
	rc->btree = btree;
	rc->type = type;
	rc->itype = DBIDX_BTREE;
	rc->alloced = 1;
	return rc;
}

/******************************************************************/

DBIDX *
dbidxopen(name, type, arg1, arg2, arg3, arg4)
char *name;
int type;	/* DBIDX_IDXT */
int arg1;
int arg2;
int arg3;
{
	static CONST char Fn[]="dbidxopen";
	DBIDX *rc;

	if (TXverbosity > 0)
		putmsg(MINFO, Fn, "Opening index %s", name);

	rc = createdbidx();
	if(!rc) return(DBIDXPN);
	switch(type)
	{
	case DBIDX_BTREE:
		rc->btree=openbtree(name, arg1, arg2, arg3, arg4);
		if(!rc->btree)
			return dbidxclose(rc);
		break;
	case DBIDX_KEYREC:
		rc->keyrec=keyrecopen(name, arg1, arg2, 0, arg3, arg4);
		break;
	}
	if(name)
		rc->type = DBIDX_NATIVE;
	else
		rc->type = DBIDX_MEMORY;
	rc->itype = type;
	rc->alloced = 1;
	return rc;
}

/******************************************************************/

BTLOC
TXdbidxgetnext(dbidx, outbufsz, outbuf, outbufp)
DBIDX	*dbidx;
size_t	*outbufsz;
void	*outbuf;
byte	**outbufp;
{
	switch(dbidx->itype)
	{
	case DBIDX_KEYREC:
		return keyrecgetnext(dbidx->keyrec, outbuf, outbufsz);
	case DBIDX_BTREE:
	default:
		return getdbidx(dbidx, outbuf, outbufsz, outbufp);
	}
}

#endif /* NEW_I */
/******************************************************************/

int
TXresetdontcare(FLDCMP *fc, int n, TXOF orderFlags)
/* For tables in `fc', clears `orderFlags' for (user-order) fields
 * less than `n', then applies `orderFlags' to field `n' and up.
 * Returns 0 on success.
 */
{
	unsigned int i;

/* We currently only care about the first field in the index.
  JMT 1999-05-19, fixes coredump if generated a unique index on
  multiple fields, and then search on one of them. */

	for(i=0; i < fc->tbl1->n; i++)
	{
		if(fc->tbl1->dd->fd[i].num < n)
			fc->tbl1->dd->fd[i].order &= ~orderFlags;
	}
	for(i=0; i < fc->tbl2->n; i++)
	{
		if(fc->tbl2->dd->fd[i].num < n)
			fc->tbl2->dd->fd[i].order &= ~orderFlags;
	}
	return TXsetdontcare(fc, n, 1, orderFlags);
}

/******************************************************************/

int
TXsetdontcare(FLDCMP *fc, int n, int up, TXOF orderFlags)
/* For tables in `fc', sets `orderFlags' for (user-order) field `n'.
 * If `up', also applies them to all fields greater than `n'.
 * Returns 0 on success.
 */
{
	unsigned int i;

/* We currently only care about the first field in the index.
  JMT 1999-05-19, fixes coredump if generated a unique index on
  multiple fields, and then search on one of them. */

	for(i=0; i < fc->tbl1->n; i++)
	{
		if((fc->tbl1->dd->fd[i].num == n) ||
			(up && fc->tbl1->dd->fd[i].num > n))
			fc->tbl1->dd->fd[i].order |= orderFlags;
	}
	for(i=0; i < fc->tbl2->n; i++)
	{
		if((fc->tbl2->dd->fd[i].num == n) ||
			(up && fc->tbl2->dd->fd[i].num > n))
			fc->tbl2->dd->fd[i].order |= orderFlags;
	}
	return 0;
}

/******************************************************************/

static int *
TXfcsavedontcare(FLDCMP *fc)
{
	static CONST char Fn[] = "TXfcsavedontcare";
	int *rc;
	size_t	i;

	rc = (int *)TXcalloc(TXPMBUFPN, Fn, fc->tbl1->n, sizeof(int));
	if(!rc) return rc;
	for(i=0; i < fc->tbl1->n; i++)
	{
		if(fc->tbl1->dd->fd[i].order != fc->tbl2->dd->fd[i].order)
		{
			putmsg(MWARN, Fn, "tbl1/tbl2 different %d", (int)i);
			return TXfree(rc);
		}
		rc[i] = fc->tbl1->dd->fd[i].order;
		fc->tbl1->dd->fd[i].order &= ~OF_DONT_CARE;
		fc->tbl2->dd->fd[i].order &= ~OF_DONT_CARE;
	}
	return rc;
}

/******************************************************************/

static void
TXfcrestoredontcare(FLDCMP *fc, int *flags)
{
	size_t	i;

	if(flags) for(i=0; i < fc->tbl1->n; i++)
	{
		fc->tbl1->dd->fd[i].order = flags[i];
		fc->tbl2->dd->fd[i].order = flags[i];
	}
	TXfree(flags);
}

/******************************************************************/

int
TXdbidxUnlock(dbidx)
DBIDX	*dbidx;
/* Unlock `dbidx'.  Should only be called if still locked from
 * opendbidx() call.
 */
{
	return(TXunlockindex(dbidx->dbtbl, INDEX_READ, &dbidx->lread));
}

/******************************************************************/

DBIDX *
opendbidx(itype, iname, sysindexFields, sysindexParams, dbtbl, flags)
int		itype;	/* Type of index */
char		*iname; /* Name of index */
CONST char	*sysindexFields;	/* (in) SYSINDEX.FIELDS text */
CONST char	*sysindexParams;	/* (in) SYSINDEX.PARAMS text */
DBTBL		*dbtbl;	/* The table the index belongs to */
int		flags;	/* (in) */
/* `flags':
 * 0x1: leave index locked (if ok)
 * 0x2: split-values index acceptable
 * 0x4: split-values index required iff multi-item type
 * 0x8: no de-dup
 */
{
	static CONST char Fn[]="opendbidx";
	FLDCMP *fc;
	DBIDX  *dbidx;
	BTPARAM	btParams;
	int	indexHasSplitValues;

        (void)itype;

#ifdef MEMDEBUG
	mac_ovchk();
#endif

	/* Bug 4064: using a splitstrlst index during bubble-up may
	 * return duplicate rows; cannot always de-dup in bubble-up,
	 * maybe fail here and hope non-bubble-up can handle it later:
	 */
	if (TXApp->deDupMultiItemResults &&	/* Bug 4064 fix active */
	    (!(flags & 0x2) || (flags & 0x4)))	/*care about split-values */
	{
		BTPARAM_INIT(&btParams);
		if (TXtextParamsToBtparam(&btParams, sysindexParams, iname,
					  0x0) != 0)
			return(NULL);		/* parse failed */
		indexHasSplitValues = TXbtreeHasSplitValues(sysindexFields,
						dbtbl, btParams.indexValues);
		if (!(flags & 0x2) && indexHasSplitValues)
			return(NULL);		/* split-values unacceptable*/
		if ((flags & 0x4) && !indexHasSplitValues &&
		    TXbtreeIsOnMultipleItemType(sysindexFields, dbtbl))
			return(NULL);		/* split-values required */
	}

	if (TXverbosity > 0)
		putmsg(MINFO, Fn, "Opening index %s", iname);

	if (!existsbtree(iname))
	{
		putmsg(MERR, Fn, "Index %s reported to exist, but does not.",
			iname);
		return NULL;
	}
	dbidx = &dbtbl->index;
	TXsetrecid(&dbidx->lrecid, -1);		/* Bug 4645 insurance */
	dbidx->deDupRecids = (TXApp->deDupMultiItemResults && !(flags & 0x8));
	dbidx->lsz = 0;
	*(int *)dbidx->lbuf = 0xDeadBeef;   /* KNG 010430 uninit; force err */
	dbidx->btree=TXbtcacheopen(dbtbl, iname, INDEX_BTREE, INDEX_READ,
				   sysindexParams);
	if (!dbidx->btree)
	{
		putmsg(MERR, Fn, "Index %s reported to exist, but does not.",
			iname);
		return NULL;
	}
	rewindbtree(dbidx->btree);
	btsetsearch(dbidx->btree, BT_SEARCH_BEFORE);

	fc = TXopenfldcmp(dbidx->btree, TXOPENFLDCMP_INTERNAL_FLDOP);
	if (!fc)
	{
		dbidx->btree = TXbtcacheclose(dbtbl, iname, INDEX_BTREE, INDEX_READ, dbidx->btree);
		return NULL;
	}
	TXsetdontcare(fc, 1, 1, OF_DONT_CARE);
	dbidx->btree->usr = fc;
	btsetcmp(dbidx->btree, (btcmptype)fldcmp);
	dbidx->dbtbl = dbtbl;
	dbidx->iname = TXstrdup(TXPMBUFPN, Fn, iname);
        dbidx->sysindexParams = TXstrdup(TXPMBUFPN, Fn, sysindexParams);
	dbidx->type = DBIDX_CACHE;
	if (!(flags & 0x1)) TXdbidxUnlock(dbidx);
	return dbidx;
}

/******************************************************************/

int
setdbidx(dbidx, fld, fname, fld2, inclo, inchi)
DBIDX	*dbidx; /* Name of index */
FLD	*fld;	/* Field containing value to search for */
char	*fname;	/* Name of the field */
FLD	*fld2;  /* Field containing last value to match */
int	inclo;	/* Is the low value inclusive? */
int	inchi;	/* Is the high value inclusive? */
/* Returns 0 on error.
 */
{
	static CONST char Fn[]="setdbidx";
	size_t	sz;
	FLD    *fld1;
	FLDCMP *fc;
	BTLOC	btloc;

	if(verbose)
		putmsg(MINFO, Fn, "fname %s, inclo %d, inchi %d", fname, inclo, inchi);
	fc = dbidx->btree->usr;
	btreeunsetmarker(dbidx->btree);
	dbidx->hibuf = TXfree(dbidx->hibuf);
	dbidx->lobuf = TXfree(dbidx->lobuf);
	fld1 = nametofld(fc->tbl1, fname);
	if(!fld1)
	{
		putmsg(MWARN, Fn, "Could not find field %s", fname);
		goto err;
	}
	if(fld2)
	{
		void	*v;

		v = getfld(fld2, &sz);
		if(verbose)
			putmsg(MINFO, Fn, "V %x, SZ %d, fld1 %x, fld1->type %d, fld2->type %d", v, sz, fld1, fld1->type, fld2->type);

		/* Bug 3677: Check for type mismatch; known cases now
		 * handled by earlier TXfixupMultiItemRelopSingleItem()
		 * and/or _fldcopy():
		 */
		if ((fld2->type & DDTYPEBITS) !=
		    (fld1->type & DDTYPEBITS))
		{
			/* non-bubble-up, e.g. ixbtindex() etc., may be able
			 * to handle this later; but yap anyway; error:
			 */
			putmsg(MWARN, Fn,
   "Internal error: Type mismatch (%s value for %s index field) for index %s; will not use index in bubble-up mode",
				TXfldtypestr(fld2), TXfldtypestr(fld1),
				dbidx->iname);
			goto err;
		}

		putfld(fld1, v, sz);

		sz = fldtobuf(fc->tbl1);
#ifndef NEVER
		if(inchi)
			btsetsearch(dbidx->btree, BT_SEARCH_AFTER);
		else
#endif
			btsetsearch(dbidx->btree, BT_SEARCH_FIND);
		btloc = btsearch(dbidx->btree, sz, fc->tbl1->orec);
#ifndef NEVER
		if(inchi || !TXrecidvalid(&btloc))
		{
#endif
			btloc = btgetnext(dbidx->btree, NULL, NULL, NULL);
#ifndef NEVER
		}
#endif
		dbidx->gotit = 0;
		if(TXrecidvalid(&btloc))
		{
			btreesetmarker(dbidx->btree);
		}
		dbidx->gotit = 1;
		btsetsearch(dbidx->btree, BT_SEARCH_BEFORE);
		dbidx->himark = btgetpercentage(dbidx->btree);
		if (!fld)
			rewindbtree(dbidx->btree);
		dbidx->hibuf = (char *)TXmalloc(TXPMBUFPN, Fn, sz);
		memcpy(dbidx->hibuf, fc->tbl1->orec, sz);
		dbidx->hisz = sz;
	}
	else
		dbidx->himark = 100;
	if(verbose)
		putmsg(MINFO, Fn, "High Mark at %d", dbidx->himark);
	if(fld)
	{
		void	*v;

		v = getfld(fld, &sz);
		if(verbose)
			putmsg(MINFO, Fn, "v %x, sz %d", v, sz);
		putfld(fld1, v, sz);
		sz = fldtobuf(fc->tbl1);
#ifdef DEBUG
		DBGMSG(4,(999, NULL, "Searching for low (%s)", fldtostr(fld1)));
#endif
		btsearch(dbidx->btree, sz, fc->tbl1->orec);
		dbidx->lomark = btgetpercentage(dbidx->btree);
		dbidx->lobuf = (char *)TXmalloc(TXPMBUFPN, Fn, sz);
		memcpy(dbidx->lobuf, fc->tbl1->orec, sz);
		dbidx->losz = sz;
	}
	else
		dbidx->lomark = 0;
	if(verbose)
		putmsg(MINFO, Fn, "Low Mark at %d", dbidx->lomark);
	dbidx->inclo = inclo;
	dbidx->inchi = inchi;
	dbidx->nrecs = 0;
	dbidx->abvlo = 0;
	dbidx->lsz = 0;
	*(int *)dbidx->lbuf = 0xDeadBeef;   /* KNG 010430 uninit; force err */
	return 1;
err:
	return(0);
}

/******************************************************************/

static DBIDX *isetdbidx ARGS((DBIDX *dbidx, BTLOC *lrecid));
static DBIDX *
isetdbidx(dbidx, lrecid)
DBIDX	*dbidx; /* Name of index */
BTLOC	*lrecid;  /* Field containing last value to match */
{
	static CONST char Fn[]="isetdbidx";
	BTLOC	btloc;

        (void)lrecid;
	if(verbose)
		putmsg(MINFO, Fn, "inclo %d, inchi %d", dbidx->inclo, dbidx->inchi);
	btreeunsetmarker(dbidx->btree);
	if(dbidx->hibuf)
	{
#ifndef NEVER
		if(dbidx->inchi)
			btsetsearch(dbidx->btree, BT_SEARCH_AFTER);
		else
#endif
			btsetsearch(dbidx->btree, BT_SEARCH_FIND);
		btloc = btsearch(dbidx->btree, dbidx->hisz, dbidx->hibuf);
#ifndef NEVER
		if(dbidx->inchi || !TXrecidvalid(&btloc))
		{
#endif
			btloc = btgetnext(dbidx->btree, NULL, NULL, NULL);
#ifndef NEVER
		}
#endif
		dbidx->gotit = 0;
		if(TXrecidvalid(&btloc))
		{
			btreesetmarker(dbidx->btree);
		}
		dbidx->gotit = 1;
		btsetsearch(dbidx->btree, BT_SEARCH_BEFORE);
		dbidx->himark = btgetpercentage(dbidx->btree);
		if (!dbidx->lobuf)
			rewindbtree(dbidx->btree);
	}
	else
		dbidx->himark = 100;
	if(verbose)
		putmsg(MINFO, Fn, "High Mark at %d", dbidx->himark);
	if(dbidx->lobuf)
	{
		btsearch(dbidx->btree, dbidx->losz, dbidx->lobuf);
		dbidx->lomark = btgetpercentage(dbidx->btree);
	}
	else
		dbidx->lomark = 0;
	if(verbose)
		putmsg(MINFO, Fn, "Low Mark at %d", dbidx->lomark);
	return dbidx;
}

/******************************************************************/

int
infodbidx(dbidx)
DBIDX	*dbidx;
{
	static CONST char Fn[]="infodbidx";

	if(verbose)
		putmsg(MINFO, Fn, "Expect to read %d%% of the index",
			(dbidx->himark - dbidx->lomark));
	return dbidx->himark - dbidx->lomark;
#ifdef NEVER /* This should be in the calling stuff */
	if((himark - lomark) > TXbtreemaxpercent)
	{
		ix->orig = TXbtcacheclose(dbtbl, iname, INDEX_BTREE, INDEX_READ, ix->orig);
		ix = TXfree(ix);
		fc = TXclosefldcmp(fc);
		btree->usr = TXclosefldcmp(btree->usr);
		btree = closebtree(btree);
		hibuf = TXfree(hibuf);
		lobuf = TXfree(lobuf);
		return NULL;
	}
#endif
}

/******************************************************************/

int
TXdbidxrewind(dbidx)
DBIDX *dbidx;
{
	rewindbtree(dbidx->btree);
	TXsetrecid(&dbidx->lrecid, -1);
	dbidx->nrecs = 0;
	return 0;
}

/* ------------------------------------------------------------------------ */

const char *
TXdbidxGetName(DBIDX *dbidx)
/* Returns human-readable name for `dbidx', for messages.
 */
{
	return(dbidx->btree ? TX_BTREE_NAME(dbidx->btree) : "NULL-B-tree");
}

/******************************************************************/

BTLOC
getdbidx(dbidx, outbuf, outbufsz, outbufp)
DBIDX	*dbidx;
void	*outbuf;
size_t	*outbufsz;
byte	**outbufp;
{
	static CONST char Fn[]="getdbidx";
	size_t	sz2;
	FLDCMP *fc;
	BTLOC	btloc;
	int	rc;
	byte	*indexbuf;

	if(verbose)
		putmsg(MINFO, Fn, "Type = %d", dbidx->type);
	TXsetrecid(&btloc, -1);
	if(dbidx->type != DBIDX_CACHE)
	{
		if(dbidx->type == DBIDX_NATIVE || dbidx->type == DBIDX_MEMORY)
		{
			return(btgetnext(dbidx->btree, outbufsz, outbuf, outbufp));
		}
		putmsg(MWARN, Fn, "Uninitialized type");
		return btloc;
	}
	if(dbidx->nrecs > 0 && !TXrecidvalid(&dbidx->lrecid))
		dbidx->nrecs = 0;
	{
		rc = TXlockindex(dbidx->dbtbl, INDEX_VERIFY | INDEX_READ,
			&dbidx->lread);
		if(rc == -1)
			return btloc;
		if(rc == -2)
		{
			int ts;

#ifdef REPORT_REOPEN
			putmsg(999, NULL, "Reinit index");
#endif
			btreinit(dbidx->btree);
			isetdbidx(dbidx, &dbidx->lrecid);
			ts = btsetsearch(dbidx->btree, BT_SEARCH_FIND);
			if(dbidx->lsz) /* Only search if got something */
			{
/* save and restore flags which cancel out the don't care so we search
   on all fields in the index *//* JMT 2001-11-15 */
				int *savedflags;
				FLDCMP *fc = (FLDCMP *)dbidx->btree->usr;
				savedflags = TXfcsavedontcare(fc);
				btsearch2(dbidx->btree, dbidx->lsz,
					&dbidx->lbuf, &dbidx->lrecid);
				TXfcrestoredontcare(fc, savedflags);
			}
			btsetsearch(dbidx->btree, ts);
#ifdef NEVER
			sz2 = sizeof(tempbuf);
			btloc = btgetnext(dbidx->btree, &sz2, tempbuf);
			if(!TXrecidvalid(&btloc))
			{
				dbidx->lrecid = btloc;
				rc = TXunlockindex(dbidx->dbtbl, INDEX_READ, &dbidx->lread);
				return btloc;
			}
#endif
		}
	}
	if(dbidx->nrecs == 0)
	{
		isetdbidx(dbidx, NULL);
	}
	fc = dbidx->btree->usr;
	while((sz2 = BT_REALMAXPGSZ), (btloc = btgetnext(dbidx->btree, &sz2, NULL, &indexbuf)), TXrecidvalid(&btloc))
	{
		rc = 1;

		/* Bug 4064: de-dup splitstrlst index; now used w/bubble-up
		 * iff param is single-value:
		 */
		if (dbidx->deDupRecids &&
		    TXrecidcmp(&btloc, &dbidx->lrecid) == 0)
			continue;

#ifdef DEBUG
		DBGMSG(9,(999, NULL, "Just read btloc %d", TXgetoff(&btloc)));
#endif
		if (dbidx->hibuf)
		{
			if(dbidx->gotit)
				rc = 1;
			else
				rc = fldcmp(dbidx->hibuf, dbidx->hisz, indexbuf, sz2, fc);
#ifdef NEVER
			if(gotit && (rc < 0 || (rc == 0 && !inchi)))
				putmsg(999, NULL, "My end marker failed. Got %d %d for field %s", rc, inchi, fname);
#endif
#ifdef DEBUG
			DBGMSG(9,(999, NULL, "(Hi)Current record scores a %d", rc));
#endif
			if ((!dbidx->inchi && !rc) || rc < 0)
				break;
		}
#ifdef NEVER
		else
		{
#endif
			if(dbidx->lobuf && !dbidx->abvlo && !dbidx->inclo)
			{
				rc = fldcmp(dbidx->lobuf, dbidx->losz, indexbuf, sz2, fc);
#ifdef DEBUG
				DBGMSG(9,(999, NULL, "(Lo)Current record scores a %d", rc));
#endif
				if (!rc)
					continue;
				else
					dbidx->abvlo++;
			}
#ifdef DEBUG
			else
				DBGMSG(9,(999, NULL, "Current record needs no check", rc));
#endif
#ifdef NEVER
		}
#endif
#ifdef DEBUG
		DBGMSG(9,(999, NULL, "Inserting %d into temp index", *(long *)indexbuf));
#endif
		if(sz2 <= *outbufsz)
		{
			memcpy(outbuf, indexbuf, sz2);
		}
		else
		{
			memcpy(outbuf, indexbuf, *outbufsz);
		}
		*outbufsz = sz2;
		dbidx->nrecs++;

		dbidx->lsz = sz2;
		memcpy(dbidx->lbuf, indexbuf, sz2);
		dbidx->lrecid = btloc;

		rc = TXunlockindex(dbidx->dbtbl, INDEX_READ, &dbidx->lread);
		return btloc;
	}
	dbidx->lrecid = btloc;
	rc = TXunlockindex(dbidx->dbtbl, INDEX_READ, &dbidx->lread);
	return btloc;
}

/******************************************************************/

DBIDX *
closedbidx(dbidx)
DBIDX	*dbidx;
/* NOTE: see also cleardbidx().
 */
{
	DBTBL	*saveBtreeLogDbTbl;

        if (!dbidx) return(NULL);

	saveBtreeLogDbTbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = dbidx->dbtbl;	/* for btreelog debug */

#ifdef NEVER /* TX_DEBUG */
	if(dbidx->itype == 3)
	{
		dbidx = dbidxclose(dbidx);
		goto done;
	}
	if (!dbidx) return(NULL);

#endif
	TXsetrecid(&dbidx->lrecid, -1);		/* Bug 4645 */
	if(!dbidx->btree) goto done;

	dbidx->btree->usr = TXclosefldcmp(dbidx->btree->usr);
	if(dbidx->type == DBIDX_CACHE)
		dbidx->btree = TXbtcacheclose(dbidx->dbtbl, dbidx->iname,
				    INDEX_BTREE, INDEX_READ, dbidx->btree);
	else
		dbidx->btree = closebtree(dbidx->btree);
	dbidx->lobuf = TXfree(dbidx->lobuf);
	dbidx->hibuf = TXfree(dbidx->hibuf);
	dbidx->iname = TXfree(dbidx->iname);
	dbidx->sysindexParams = TXfree(dbidx->sysindexParams);
	dbidx->lobuf = NULL;
	dbidx->hibuf = NULL;
	dbidx->iname = NULL;
	dbidx->keepcached = 0;
	if(dbidx->indexdbtbl)
		dbidx->indexdbtbl = closedbtbl(dbidx->indexdbtbl);
	dbidx->indexdataonly = 0;
done:
	/* WTF should really call cleardbidx(), but not sure if some
	 * DBIDX fields are intended to survive past closedbidx().
	 * Bug 4645
	 */
	TXbtreelog_dbtbl = saveBtreeLogDbTbl;	/* for btreelog debug */
	return dbidx;
}
#endif /* NO_BUBBLE_INDEX */
