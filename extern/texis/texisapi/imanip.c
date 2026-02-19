/* -=- kai-mode: John -=- */
#ifdef NEVER /* NEW_I */
#include "imanip2.c"
#else
#include "txcoreconfig.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(MSDOS) && !defined(__MINGW32__)
#include <unistd.h>
#include <pwd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "ddic.h"
#include "mmtbl.h"
#include "fldcmp.h"
#include "dbtable.h"
#include "cgi.h"

/********************************************************************/
/*
 * This file implements the index manipulation functions required
 * by TexIS
 */

static long		dummy; /* Force align */
static byte		buf[BT_MAXPGSZ];

/******************************************************************/

IINDEX *
closeiindex(index)
IINDEX *index;
{
	(void)dummy;

	if (index)
	{
#ifdef NEW_I
		if(index->orig)
		{
			if(index->ko)
				index->orig = TXfree(index->orig);
			else
				dbidxclose(index->orig);
		}
		if (!index->ki && index->inv)
			dbidxclose(index->inv);
		if (!index->kr && index->revinv)
			dbidxclose(index->revinv);
		if (!index->km && index->mirror)
			dbidxclose(index->mirror);
		if (!index->ks && index->ordered)
			dbidxclose(index->ordered);
		if (!index->kv && index->rev)
			dbidxclose(index->rev);
#else
		if (!index->ko && index->orig)
		{
			if(index->orig->usr)
				index->orig->usr =
					TXclosefldcmp(index->orig->usr);
			closebtree(index->orig);
		}
		if (!index->ki && index->inv)
			closebtree(index->inv);
		if (!index->kr && index->revinv)
			closebtree(index->revinv);
		if (!index->km && index->mirror)
			closebtree(index->mirror);
		if (!index->ks && index->ordered)
			closebtree(index->ordered);
		if (!index->kv && index->rev)
			closebtree(index->rev);
#endif
		free(index);
	}
	return (IINDEX *)NULL;
}

/******************************************************************/

IINDEX *
openiindex()
/* All IINDEX structs should be created by this function.
 */
{
	static CONST char	fn[] = "openiindex";
	IINDEX			*index;

	index = (IINDEX *)calloc(1, sizeof(IINDEX));
	if (index == IINDEXPN)
	{
		TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, 1, sizeof(IINDEX));
		return(IINDEXPN);
	}
	index->rowsReturned = -1;
	/* rest cleared by calloc? */
	return index;
}

/******************************************************************/
/*
 * Make an inverted index.
 *
 * WTF: Use array, qsort, and append
 *
 */

int
TXindexinv(index)
IINDEX *index;
{
	static char	Fn[] = "indexinv";
	BTLOC		btloc;
	size_t		len;
	unsigned long	cnt=0;
	EPI_OFF_T		key;

	if (index->inv)	/* Inverse already exists */
		return 0;
	if (!index->orig) /* No original  WTF - support partial */
		return -1;
#ifdef NEW_I
	if (index->orig->itype != DBIDX_BTREE)
		return -1;
	if ((!(index->orig->btree->flags & BT_FIXED)) ||  /* Can only invert fixed */
	    (!(index->orig->btree->flags & BT_UNSIGNED))) /* unsigned for now */
#else
	if ((!(index->orig->flags & BT_FIXED)) ||  /* Can only invert fixed */
	    (!(index->orig->flags & BT_UNSIGNED))) /* unsigned for now */
#endif
		cnt++;
#ifdef NEW_I
	index->inv = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
#else
	index->inv = openbtree(NULL, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
	if (index->inv)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&index->inv->params, DDICPN/*wtf*/);
#endif
	if (!index->inv)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
#ifdef NEW_I
	if (!cnt)
		index->inv->nrank = index->orig->nrank;
	TXdbidxrewind(index->orig);
#else
	rewindbtree(index->orig);
#endif
	if (cnt)
	{
#ifdef NEW_I
		btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
		btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	}
	else
	{
		/* With positive ranks, we get rank (*descending*) ordering
		 * via TXfixedUnsignedReverseCmp(), not via negative ranks:
		 */
		if (!(TXApp && TXApp->legacyVersion7OrderByRank) &&
		    index->orank)
		    btsetcmp(index->inv, (btcmptype)TXfixedUnsignedReverseCmp);

		len = sizeof(key);
#ifdef NEW_I
		btloc = TXdbidxgetnext(index->orig, &len, &key, NULL);
#else
		btloc = btgetnext(index->orig, &len, &key, NULL);
#endif
	}
	while (TXrecidvalid(&btloc))
	{
		BTLOC	newLoc, newKey;

		newKey = btloc;			/* flipping key and loc */
		if (!cnt)
			TXsetrecid(&newLoc, (EPI_OFF_T)key);
		else
			TXsetrecid(&newLoc, (EPI_OFF_T)cnt++);
#ifdef NEW_I
		dbidxspinsert(index->inv, &newLoc, sizeof(BTLOC), &newKey, 95);
#else
		btspinsert(index->inv, &newLoc, sizeof(BTLOC), &newKey, 95);
#endif
		if (cnt)
		{
#ifdef NEW_I
			btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
			btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
		}
		else
		{
			len = sizeof(key);
#ifdef NEW_I
			btloc = TXdbidxgetnext(index->orig, &len, &key, NULL);
#else
			btloc = btgetnext(index->orig, &len, &key, NULL);
#endif
		}
	}

	if (TXtraceIndexBits & 0x30000)
	{
		BTREE	*saveInv;

		/* Squirrel away `inv' to make `index' back to original: */
		saveInv = index->inv;
		index->inv = NULL;
		/* Yap about it: */
		if (TXtraceIndexBits & 0x10000)
			putmsg(MINFO, __FUNCTION__,
		       "Created inverted B-tree for %s IINDEX %p (%wkd rows)",
			       TXiindexTypeName(index), index,
			       (EPI_HUGEINT)index->cntorig);
		if (TXtraceIndexBits & 0x20000)
		{
			BTREE	*saveOrig;

			TXdumpIindex(NULL, 2, index);	/* dump orig */
			saveOrig = index->orig;
			index->orig = NULL;
			index->inv = saveInv;
			TXdumpIindex(NULL, 2, index);	/* dump inv */
			index->orig = saveOrig;
		}
		index->inv = saveInv;
	}

	return 0;
}

/******************************************************************/
/*
 * Make an reverse index.
 *
 * WTF: Use array, qsort, and append
 *
 */

int
_indrev(index)
IINDEX *index;
{
	static char	Fn[] = "indexrev";
	BTLOC		btloc, btl;
	unsigned long	cnt;

	if (index->rev)	/* Inverse already exists */
		return 0;
	if (!index->orig) /* No original  WTF - support partial */
		return -1;
	cnt=EPI_OS_ULONG_MAX;
#ifdef NEW_I
	index->rev = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
#else
	index->rev = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	if (index->rev)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&index->rev->params, DDICPN/*wtf*/);
#endif
	if (!index->rev)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
#ifdef NEW_I
	TXdbidxrewind(index->orig);
	btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
	rewindbtree(index->orig);
	btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	while (TXrecidvalid(&btloc))
	{
		TXsetrecid(&btl, (EPI_OFF_T)cnt--);
#ifdef NEW_I
		dbidxinsert(index->rev, &btloc, sizeof(btl), &btl);
		btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
		btinsert(index->rev, &btloc, sizeof(btl), &btl);
		btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	}
	return 0;
}

/******************************************************************/
/*
 * Make an reverse inverted index.
 *
 * WTF: Use array, qsort, and append
 *
 */

static int indexrevinv ARGS((IINDEX *));

static int
indexrevinv(index)
IINDEX *index;
{
	static char	Fn[] = "indexinv";
	BTLOC		btloc, btl;
	unsigned long	cnt;

	if (index->revinv)	/* Inverse already exists */
		return 0;
	if (!index->orig) /* No original  WTF - support partial */
		return -1;
	cnt=EPI_OS_ULONG_MAX;
#ifdef NEW_I
	index->revinv = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
#else
	index->revinv = openbtree(NULL, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
	if (index->revinv)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&index->revinv->params, DDICPN/*wtf*/);
#endif
	if (!index->revinv)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
#ifdef NEW_I
	TXdbidxrewind(index->orig);
	btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
	rewindbtree(index->orig);
	btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	while (TXrecidvalid(&btloc))
	{
		TXsetrecid(&btl, (EPI_OFF_T)cnt--);
#ifdef NEW_I
		dbidxinsert(index->revinv, &btl, sizeof(btl), &btloc);
		btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
		btinsert(index->revinv, &btl, sizeof(btl), &btloc);
		btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	}
	return 0;
}

/******************************************************************/
/*
 * Make a "mirrored" index.  This throws away the key altogether.
 *
 * WTF: Use array, qsort, and append
 *
 */

int
indexmirror(index)
IINDEX *index;
{
	static char	Fn[] = "indexmirror";
	BTLOC		btloc;

	if (index->mirror)	/* Inverse already exists */
		return 0;
	if (!index->orig) /* No original  WTF - support partial */
		return -1;
#ifdef NEW_I
	if (!(index->orig->itype == DBIDX_BTREE &&
	      index->orig->btree->flags & BT_FIXED)) /* Can only invert fixed now */
		return -1;
	index->mirror = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
#else
	if (!(index->orig->flags & BT_FIXED)) /* Can only invert fixed now */
		return -1;
	index->mirror = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	if (index->mirror)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&index->mirror->params, DDICPN/*wtf*/);
#endif
	if (!index->mirror)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
#ifdef NEW_I
	TXdbidxrewind(index->orig);
	btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
	rewindbtree(index->orig);
	btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	while (TXrecidvalid(&btloc))
	{
#ifdef NEW_I
		dbidxinsert(index->mirror, &btloc, sizeof(BTLOC), &btloc);
		btloc = TXdbidxgetnext(index->orig, NULL, NULL, NULL);
#else
		btinsert(index->mirror, &btloc, sizeof(BTLOC), &btloc);
		btloc = btgetnext(index->orig, NULL, NULL, NULL);
#endif
	}
	return 0;
}

/******************************************************************/
/*
 * And two indexes.  Takes two indexes, and outputs the items in
 * a that are also in b.
 */

static IINDEX *
indexandavv(IINDEX *a, IINDEX *b, IINDEX *result, int linearmerge)
{
	static char Fn[] = "indexandavv";
#ifdef NEW_I
	DBIDX *aIdx = a->inv, *bIdx, *resultIdx;
#else
	BTREE *aIdx = a->inv, *bIdx, *resultIdx;
#endif
	BTLOC aLoc, bLoc, aInvKey, bKey, crank;
	size_t len;
	int rc = 0;

#ifdef NEW_I
	resultIdx = result->inv = dbidxopen(NULL, DBIDX_BTREE, BT_MAXPGSZ, 20,
				BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
	resultIdx->nrank = result->nrank;
#else
	resultIdx = result->inv = openbtree(NULL, BT_MAXPGSZ, 20,
				BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
	if (resultIdx)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&resultIdx->params, DDICPN/*wtf*/);
#endif
	if (!resultIdx)
	{
		putmsg(MERR + FOE, Fn, "Could not create index file");
		return closeiindex(result);
	}
	if (b->mirror)
		bIdx = b->mirror;
	else
		bIdx = b->inv;	/* We'll look up in the inverse tree */
#ifdef NEW_I
	TXdbidxrewind(aIdx);
	TXdbidxrewind(bIdx);
	TXdbidxrewind(resultIdx);
#else
	rewindbtree(aIdx);
	rewindbtree(bIdx);
	rewindbtree(resultIdx);
#endif
	TXsetrecid(&bLoc, -1);
	len = sizeof(aInvKey);
#ifdef NEW_I
	for(aLoc = TXdbidxgetnext(aIdx, &len, &aInvKey, NULL);
	    TXrecidvalid(&aLoc);
	     aLoc = TXdbidxgetnext(aIdx, &len, &aInvKey, NULL))
#else
	for(aLoc = btgetnext(aIdx, &len, &aInvKey, NULL);
	    TXrecidvalid(&aLoc);
	     aLoc = btgetnext(aIdx, &len, &aInvKey, NULL))
#endif
	{
		EPI_OFF_T rank;

#ifdef DEBUG
		DBGMSG(9, (999, NULL, "Read a length %d key", len));
#endif
		if (linearmerge)
		{
			if (!TXrecidvalid(&bLoc))
			{
				len = sizeof(bKey);
#ifdef NEW_I
				bLoc = TXdbidxgetnext(bIdx, &len, &bKey, NULL);
#else
				bLoc = btgetnext(bIdx, &len, &bKey, NULL);
#endif
			}
			while (TXrecidvalid(&aLoc) && TXrecidvalid(&bLoc) &&
			       (rc = TXrecidcmp(&aInvKey, &bKey)) != 0)
			{
				if (rc > 0)
				{
					len = sizeof(bKey);
#ifdef NEW_I
					bLoc = TXdbidxgetnext(bIdx, &len, &bKey, NULL);
#else
					bLoc = btgetnext(bIdx, &len, &bKey, NULL);
#endif
				}
				else
				{
					len = sizeof(aInvKey);
#ifdef NEW_I
					aLoc = TXdbidxgetnext(aIdx, &len, &aInvKey, NULL);
#else
					aLoc = btgetnext(aIdx, &len, &aInvKey, NULL);
#endif
				}
			}
			if (!TXrecidvalid(&aLoc))
				break;
		}
		else
		{
#ifdef NEW_I
			bLoc = dbidxsearch(bIdx, sizeof(aInvKey), &aInvKey);/* WTF WRONG */
#else
			bLoc = btsearch(bIdx, sizeof(aInvKey), &aInvKey);	/* WTF WRONG */
#endif
			if(!TXrecidvalid(&bLoc))
				continue;
		}
		/* Or is it.  The indexinv above will take care of
		   most issues. */
		rank = 0;
		if (a->nrank)
		{
			rank = TXgetoff(&aLoc);
		}
		DBGMSG(1, (999, NULL, "A Rank %d", rank));
		if (b->nrank)
			rank += TXgetoff(&bLoc);
		DBGMSG(1, (999, NULL, "B Rank %d", TXgetoff(&bLoc)));
		DBGMSG(1, (999, NULL, "RESULT Rank %d", rank));
		if (result->nrank)
		{
			TXsetrecid(&crank, rank);
			len = sizeof(crank);
		}
		if (TXrecidvalid(&bLoc))		/* Does it exist ? */
		{
#ifdef NEW_I
			dbidxspinsert(resultIdx, &crank, len, &aInvKey, 90);
#else
			btspinsert(resultIdx, &crank, len, &aInvKey, 90);
#endif
			result->cntorig++;
		}
		len = sizeof(buf);
		len = sizeof(aInvKey);
		if(linearmerge)
#ifdef NEW_I
			bLoc = TXdbidxgetnext(bIdx, &len, &bKey, NULL);
#else
			bLoc = btgetnext(bIdx, &len, &bKey, NULL);
#endif
	}
#ifdef NEW_I
	TXdbidxrewind(resultIdx);
#else
	rewindbtree(resultIdx);
#endif
	return(result);
}

CONST char *
TXiindexTypeName(iin)
IINDEX	*iin;
{
	if (iin->orig) return("original");
	if (iin->inv) return("inverted");
	if (iin->revinv) return("reversed-inverted");
	if (iin->mirror) return("mirror");
	if (iin->ordered) return("ordered");
	if (iin->rev) return("reversed-ordered");
	return("unknown");
}

void
TXdumpIindex(pmbuf, indent, iin)
TXPMBUF		*pmbuf;
int		indent;	/* (in) number of spaces to indent message */
IINDEX		*iin;
/* Dumps type and recids of `iin' to a single putmsg().  For debug/tracing.
 */
{
	BTREE	*bt;
	char	btFlagsBuf[256], schemaStr[1024];

	if (iin->orig)
		bt = iin->orig;
	else if (iin->inv)
		bt = iin->inv;
	else if (iin->mirror)			/* recid -> recid */
		bt = iin->mirror;
	else
	{
		txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
			       "Cannot handle %s IINDEX %p",
			       TXiindexTypeName(iin), iin);
		goto err;
	}

	TXbtreePrFlags(bt, btFlagsBuf, sizeof(btFlagsBuf));
	if (btreegetdd(bt))
	{
		char	*sStr = NULL;

		sStr = TXddSchemaToStr(btreegetdd(bt), 2);
		htsnpf(schemaStr, sizeof(schemaStr), " schema %s", sStr);
		sStr = TXfree(sStr);
	}
	else if (bt->flags & BT_FIXED)
		*schemaStr = '\0';
	else					/* should have a DD? */
		strcpy(schemaStr, " no DD");
	txpmbuf_putmsg(pmbuf, MINFO, NULL,
		       "%*s%s IINDEX %p nrank %d orank %d B-tree %p flags %s%s with cmp %s %wkd locs:",
		       indent, "", TXiindexTypeName(iin), iin,
		       (int)iin->nrank, (int)iin->orank, bt, btFlagsBuf,
		       schemaStr, TXbtreeCmpFuncToStr(bt),
		       (EPI_HUGEINT)TXbtreeGetNumItemsDelta(bt));
	TXbtreeDump(pmbuf, bt, indent + 2, 1);
	return;
err:
	return;
}

/******************************************************************/
/*
 * And two indexes.  Takes two indexes, and outputs the items in
 * a that are also in b.
 */

IINDEX *
indexand(a, b, inv)
IINDEX *a;
IINDEX *b;
int inv;
{
	static char Fn[] = "indexand";
	IINDEX *result = NULL, *t = NULL, *orgA = a, *orgB = b;
#ifdef NEW_I
	DBIDX *aIdx = NULL, *bIdx, *resultIdx;
#  define GETNEXT(idx, keyLenPtr, keyBuf)	\
	TXdbidxgetnext(idx, keyLenPtr, keyBuf, NULL)
#else
	BTREE *aIdx = BTREEPN, *bIdx, *resultIdx;
#  define GETNEXT(idx, keyLenPtr, keyBuf)	\
	btgetnext(idx, keyLenPtr, keyBuf, NULL)
#endif
	BTLOC aLoc, bLoc, aInvKey, bKey;
	size_t len;
	int dolinear = 0, aKeyIsRecid = 0, bKeyIsRecid = 0;
	int aMirrored = 0, aIsInverted = 0, rc = 0;
	int linearmerge = 0;

	TXsetrecid(&aInvKey, -1);
	TXsetrecid(&bKey, -1);

	result = openiindex();
	if (!result) goto err;
	if (a->mirror || a->inv)
	{
		aKeyIsRecid = 1;		/* Can key off a->recid */
	}
	if (b->mirror || b->inv)
	{
		bKeyIsRecid = 1;		/* Can key off b->recid */
	}
	if (aKeyIsRecid && (!bKeyIsRecid || (a->cntorig > b->cntorig)))
	{
		/* A MUST be ready to switch, and we switch either
		   if b is not ready, or a has more items than b. */
		t = a;
		a = b;
		b = t;
		t = NULL;
		aKeyIsRecid = (a->mirror || a->inv);
		bKeyIsRecid = (b->mirror || b->inv);
	}
	if (!b->mirror && TXindexinv(b) == -1)
	{
		/* Need some data to search or merge from in b */
		goto err;
	}
	if (a->cntorig &&
	    b->cntorig &&
	    (a->cntorig/b->cntorig<(EPI_HUGEUINT)5) &&
	   (b->cntorig/a->cntorig < (EPI_HUGEUINT)5) &&
	    !a->orig)
	{
		linearmerge++;
	}
	if (!a->orig)
	{
		if (a->mirror)
		{
			aIdx = a->mirror;
			aMirrored = 1;
		}
		else if (a->inv)
		{
			aIdx = a->inv;
			aIsInverted = 1;
		}
	}
	else
		aIdx = a->orig;	/* Look at a's original tree */

	result->nrank = a->nrank + b->nrank;
	result->orank = a->orank + b->orank;
	if (aIsInverted && inv)
	{
		result = indexandavv(a, b, result, linearmerge);
		goto done;
	}
	if (result->nrank)
	{
		if (result->orank)
		{		/* Ordered by rank */
#ifdef NEW_I
			result->orig = dbidxopen(NULL,
						 DBIDX_BTREE, BT_MAXPGSZ, 20,
						 (BT_UNSIGNED | BT_FIXED),
						 (O_RDWR | O_CREAT));
#else
			result->orig = openbtree(NULL,
						 BT_MAXPGSZ, 20,
						 (BT_UNSIGNED | BT_FIXED),
						 (O_RDWR | O_CREAT));
			if (!result->orig) goto err;
		     /* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
			BTPARAM_INIT_TO_PROCESS_DEFAULTS(&result->orig->params, DDICPN/*wtf*/);
			/* With positive ranks, we get
			 * rank-*descending* ordering via
			 * TXfixedUnsignedReverseCmp(), not negative ranks:
			 */
			if (!(TXApp && TXApp->legacyVersion7OrderByRank))
				btsetcmp(result->orig,
					 (btcmptype)TXfixedUnsignedReverseCmp);
#endif
		}
		else				/* !orank */
		{		/* No order, but store rank */
			if (aMirrored)
			{
				dolinear++;
#ifdef NEW_I
				result->orig = dbidxopen(NULL,
					 DBIDX_BTREE, BT_MAXPGSZ, 20,
					 (BT_UNSIGNED | BT_FIXED | BT_LINEAR),
					 (O_RDWR | O_CREAT));
#else
				result->orig = openbtree(NULL,
						      BT_MAXPGSZ, 20,
					 (BT_UNSIGNED | BT_FIXED | BT_LINEAR),
					 (O_RDWR | O_CREAT));
				if (result->orig)
					/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
					BTPARAM_INIT_TO_PROCESS_DEFAULTS(&result->orig->params, DDICPN/*wtf*/);
#endif
			}
			else
#ifdef NEW_I
				result->orig = dbidxopen(NULL,
					 DBIDX_BTREE, BT_MAXPGSZ, 20, 0,
					 (O_RDWR | O_CREAT));
#else
				result->orig = openbtree(NULL,
						      BT_MAXPGSZ, 20, 0,
					 (O_RDWR | O_CREAT));
				if (result->orig)
					/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
					BTPARAM_INIT_TO_PROCESS_DEFAULTS(&result->orig->params, DDICPN/*wtf*/);
#endif
		}
	}
	else					/* nrank 0 */
	{
		if (aMirrored) /* Just recids.  Store as they come in order */
		{
			dolinear++;
#ifdef NEW_I
			result->orig = dbidxopen(NULL, DBIDX_BTREE,
					BT_MAXPGSZ, 20,
					(BT_UNSIGNED | BT_FIXED | BT_LINEAR),
					(O_RDWR | O_CREAT));
#else
			result->orig = openbtree(NULL, BT_MAXPGSZ, 20,
					(BT_UNSIGNED | BT_FIXED | BT_LINEAR),
					(O_RDWR | O_CREAT));
			if (result->orig)
				/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
				BTPARAM_INIT_TO_PROCESS_DEFAULTS(&result->orig->params, DDICPN/*wtf*/);
#endif
		}
		else
#ifdef NEW_I
			result->orig = dbidxopen(NULL, DBIDX_BTREE, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
#else
			result->orig = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
			if (result->orig)
				/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
				BTPARAM_INIT_TO_PROCESS_DEFAULTS(&result->orig->params, DDICPN/*wtf*/);
#endif
	}
	if (!result->orig)
	{
		putmsg(MERR + FOE, Fn, "Could not create index file");
		goto err;
	}
	if (b->mirror)
		bIdx = b->mirror;
	else
		bIdx = b->inv;	/* We'll look up in the inverse tree */
	resultIdx = result->orig;		/* And store in result's original */
#ifdef NEW_I
	resultIdx->nrank = result->nrank;
	TXdbidxrewind(aIdx);
	TXdbidxrewind(bIdx);
	TXdbidxrewind(resultIdx);
#else
	rewindbtree(aIdx);
	rewindbtree(bIdx);
	rewindbtree(resultIdx);
#endif
	len = sizeof(buf);
	TXsetrecid(&bLoc, -1);
	if (aIsInverted)
	{
		len = sizeof(aInvKey);
		aLoc = GETNEXT(aIdx, &len, &aInvKey);
	}
	else
		aLoc = GETNEXT(aIdx, &len, buf);
	while (TXrecidvalid(&aLoc))
	{
		EPI_OFF_T rank;

#ifdef DEBUG
		DBGMSG(9, (999, NULL, "Read a length %d key", len));
#endif
		if (linearmerge)
		{
			if (!TXrecidvalid(&bLoc))
			{
				len = sizeof(bKey);
				bLoc = GETNEXT(bIdx, &len, &bKey);
			}
			if (aIsInverted)
			{
				while (TXrecidvalid(&aLoc) &&
				       TXrecidvalid(&bLoc) &&
				       (rc = TXrecidcmp(&aInvKey, &bKey)) != 0)
				{
					if (rc > 0)	/* a > b */
					{
						len = sizeof(bKey);
						bLoc = GETNEXT(bIdx, &len,
							       &bKey);
					}
					else		/* a < b */
					{
						len = sizeof(aInvKey);
						aLoc = GETNEXT(aIdx, &len,
							       &aInvKey);
					}
				}
				if (!TXrecidvalid(&aLoc))
					break;
			}
			else			/* !aIsInverted */
			{
				while (TXrecidvalid(&aLoc) &&
				       TXrecidvalid(&bLoc) &&
				       (rc = TXrecidcmp(&aLoc, &bKey)) != 0)
				{
					if (rc > 0)	/* a > b */
					{
						len = sizeof(bKey);
						bLoc = GETNEXT(bIdx, &len,
							       &bKey);
					}
					else		/* a < b */
					{
						len = sizeof(buf);
						aLoc = GETNEXT(aIdx, &len,
							       buf);
					}
				}
				if (!TXrecidvalid(&aLoc))
					break;
			}
		}
		else
		{
			if (aIsInverted)
			{
#ifdef NEW_I
				bLoc = dbidxsearch(bIdx, sizeof(aInvKey),
						   &aInvKey);	/* WTF WRONG */
#else
				bLoc = btsearch(bIdx, sizeof(aInvKey),
						&aInvKey);	/* WTF WRONG */
#endif
			}
			else
				/* WTF WRONG */
#ifdef NEW_I
				bLoc = dbidxsearch(bIdx, sizeof(aLoc), &aLoc);
#else
				bLoc = btsearch(bIdx, sizeof(aLoc), &aLoc);
#endif
		}
		/* Or is it.  The indexinv above will take care of
		   most issues. */
		rank = 0;
		if (a->nrank)
		{
			if (aIsInverted)
				rank = TXgetoff(&aLoc);
			else
				rank = *(int *) buf;
		}
		DBGMSG(1, (999, NULL, "A Rank %d", rank));
		if (b->nrank)
			rank += TXgetoff(&bLoc);
		DBGMSG(1, (999, NULL, "B Rank %d", TXgetoff(&bLoc)));
		DBGMSG(1, (999, NULL, "RESULTIINDEX Rank %d", rank));
		if (result->nrank)
		{
			*(EPI_OFF_T *) buf = rank;
			len = sizeof(EPI_OFF_T);
		}
		if (TXrecidvalid(&bLoc))		/* Does it exist ? */
		{
			if (aIsInverted)
			{
				if (dolinear)
#ifdef NEW_I
					dbidxinsert(resultIdx, &aInvKey, len,
						    buf);
#else
					btappend(resultIdx, &aInvKey, len,
						 buf, 90, NULL);
#endif
				else
#ifdef NEW_I
					dbidxspinsert(resultIdx, &aInvKey,
						      len, buf, 90);
#else
					btspinsert(resultIdx, &aInvKey, len,
						   buf, 90);
#endif
			}
			else
			{
				if (dolinear)
#ifdef NEW_I
					dbidxinsert(resultIdx, &aLoc, len,
						    buf);
#else
					btappend(resultIdx, &aLoc, len,
						 buf, 90, NULL);
#endif
				else
#ifdef NEW_I
					dbidxspinsert(resultIdx, &aLoc, len,
						      buf, 90);
#else
					btspinsert(resultIdx, &aLoc, len,
						   buf, 90);
#endif
			}
			result->cntorig++;
		}
		len = sizeof(buf);
		if (aIsInverted)
		{
			len = sizeof(aInvKey);
			aLoc = GETNEXT(aIdx, &len, &aInvKey);
		}
		else
			aLoc = GETNEXT(aIdx, &len, buf);
	}
#ifdef NEW_I
	/* WTF: BTFLUSH */
	if (dolinear)
		btflush(resultIdx->btree);
	TXdbidxrewind(resultIdx);
#else
	if (dolinear)
		btflush(resultIdx);
	rewindbtree(resultIdx);
#endif
	goto done;

err:
	result = closeiindex(result);
done:
	if (TXtraceIndexBits & 0x10000)
		putmsg(MINFO, Fn,
		       "ANDed %s IINDEX %p (%wd rows) and %s IINDEX %p (%wd rows) creating %s IINDEX %p (%wd rows)",
		       TXiindexTypeName(orgA), orgA,
		       (EPI_HUGEINT)orgA->cntorig,
		       TXiindexTypeName(orgB), orgB,
		       (EPI_HUGEINT)orgB->cntorig,
		       (result ? TXiindexTypeName(result) : "failed"), result,
		       (result ? (EPI_HUGEINT)result->cntorig : (EPI_HUGEINT)0));
	if (TXtraceIndexBits & 0x20000)
	{
		TXdumpIindex(NULL, 2, orgA);
		TXdumpIindex(NULL, 2, orgB);
		if (result) TXdumpIindex(NULL, 2, result);
	}
	return(result);
#undef GETNEXT
}

/******************************************************************/
/*
 *
 * WTF: Use array, qsort, and append
 *
 */

#ifdef NEW_I
#define GETFROMA() {\
	len1 = sizeof(key1);\
	bl1 = TXdbidxgetnext(ab, &len1, &key1, NULL);\
	TXsetrecid(&b1, (EPI_OFF_T)key1);\
	if(a->nrank)\
		rank1 = TXgetoff(&bl1)/a->nrank;\
	else\
		rank1 = 0;\
}

#define GETFROMB() {\
	len2 = sizeof(key2);\
	bl2 = TXdbidxgetnext(bb, &len2, &key2, NULL);\
	TXsetrecid(&b2, (EPI_OFF_T)key2);\
	if(b->nrank)\
		rank2 = TXgetoff(&bl2)/b->nrank;\
	else\
		rank2 = 0;\
}
#else
#define GETFROMA() {\
	len1 = sizeof(key1);\
	bl1 = btgetnext(ab, &len1, &key1, NULL);\
	TXsetrecid(&b1, (EPI_OFF_T)key1);\
	if(a->nrank)\
		rank1 = TXgetoff(&bl1)/a->nrank;\
	else\
		rank1 = 0;\
}

#define GETFROMB() {\
	len2 = sizeof(key2);\
	bl2 = btgetnext(bb, &len2, &key2, NULL);\
	TXsetrecid(&b2, (EPI_OFF_T)key2);\
	if(b->nrank)\
		rank2 = TXgetoff(&bl2)/b->nrank;\
	else\
		rank2 = 0;\
}
#endif

IINDEX *
indexor(a, b, inv)
IINDEX *a;
IINDEX *b;
int	inv;
{
	static char Fn[] = "indexor";
	IINDEX	*c = NULL, *orgA = a, *orgB = b;
#ifdef NEW_I
	DBIDX	*ab, *bb, *cb;
#else
	BTREE	*ab, *bb, *cb;
#endif
	BTLOC	bl1, bl2, b1, b2;
	size_t	len1, len2;
	EPI_OFF_T	key1=(EPI_OFF_T)-1, key2=(EPI_OFF_T)-1;
	EPI_OFF_T	rank1, rank2;

	c = openiindex();
	if (!c) goto err;
	if (TXindexinv(a) == -1)
	{
		if(!a->mirror)
			goto err;
	}
	if (TXindexinv(b) == -1)
	{
		if(!b->mirror)
			goto err;
	}
#ifdef NEW_I
	if(inv)
		c->orig = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
	else
		c->orig = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
#else
	if(inv)
		c->orig = openbtree(NULL, BTFSIZE, 20, BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
	else
		c->orig = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	if (c->orig)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&c->orig->params, DDICPN/*wtf*/);
#endif
	if (!c->orig)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		goto err;
	}
	ab = a->inv;		/* Look at a's inverse tree */
	if(!ab)
		ab = a->mirror; /* Look at mirror.  If no inv, no rank */
	bb = b->inv;		/* We'll look up in the inverse tree */
	if(!bb)
		bb = b->mirror; /* Look at mirror.  If no inv, no rank */
	cb = c->orig;		/* And store in c's original */
#ifdef NEW_I
	TXdbidxrewind(ab);
	TXdbidxrewind(bb);
	TXdbidxrewind(cb);
#else
	rewindbtree(ab);
	rewindbtree(bb);
	rewindbtree(cb);
#endif
	GETFROMA();
	GETFROMB();
	while(TXrecidvalid(&bl1) || TXrecidvalid(&bl2))
	{
		if (((key1 < key2) || (!TXrecidvalid(&bl2))) && (TXrecidvalid(&bl1)))
		{
			if(inv)
			{
				TXsetrecid(&b1, (EPI_OFF_T)rank1);
#ifdef NEW_I
				dbidxspinsert(cb, &b1, sizeof(key1), &key1, 90);
#else
				btspinsert(cb, &b1, sizeof(key1), &key1, 90);
#endif
			}
			else
			{
				key1 = rank1;
#ifdef NEW_I
				dbidxspinsert(cb, &b1, sizeof(key1), &key1, 90);
#else
				btspinsert(cb, &b1, sizeof(key1), &key1, 90);
#endif
			}
			GETFROMA();
			c->cntorig++;
		} else
		if (((key2 < key1) || (!TXrecidvalid(&bl1))) && (TXrecidvalid(&bl2)))
		{
			if(inv)
			{
				TXsetrecid(&b2, (EPI_OFF_T)rank2);
#ifdef NEW_I
				dbidxspinsert(cb, &b2, sizeof(key2), &key2, 90);
#else
				btspinsert(cb, &b2, sizeof(key2), &key2, 90);
#endif
			}
			else
			{
				key2 = rank2;
#ifdef NEW_I
				dbidxspinsert(cb, &b2, sizeof(key2), &key2, 90);
#else
				btspinsert(cb, &b2, sizeof(key2), &key2, 90);
#endif
			}
			GETFROMB();
			c->cntorig++;
		} else
		if (key1 == key2)
		{
			if(inv)
			{
				if(rank1 > rank2)
					TXsetrecid(&b2, (EPI_OFF_T)rank2);
				else
					TXsetrecid(&b2, (EPI_OFF_T)rank1);
#ifdef NEW_I
				dbidxspinsert(cb, &b2, sizeof(key2), &key2, 90);
#else
				btspinsert(cb, &b2, sizeof(key2), &key2, 90);
#endif
			}
			else
			{
#ifndef NO_NEW_RANK
				if(rank1 > rank2)
					key2 = rank2;
				else
					key2 = rank1;
#endif
#ifdef NEW_I
				dbidxspinsert(cb, &b2, sizeof(key2), &key2, 90);
#else
				btspinsert(cb, &b2, sizeof(key2), &key2, 90);
#endif
			}
			GETFROMA();
			GETFROMB();
			c->cntorig++;
		}
	}
/* Let's normalize real quick */
#ifdef NO_NEW_RANK /* What does this do again? */
	indexmirror(c);
	c->inv = c->orig;
	c->orig = c->mirror ;
	c->mirror = c->inv ;
	c->inv = NULL;
#else
	c->nrank = 1;
	if(inv)
	{
		c->inv = c->orig;
		c->orig = NULL;
	}
#endif
	goto done;

err:
	c = closeiindex(c);
done:
	if (TXtraceIndexBits & 0x10000)
		putmsg(MINFO, Fn,
		       "ORed %s IINDEX %p (%wd rows) and %s IINDEX %p (%wd rows) creating %s IINDEX %p (%wd rows)",
		       TXiindexTypeName(orgA), orgA,
		       (EPI_HUGEINT)orgA->cntorig,
		       TXiindexTypeName(orgB), orgB,
		       (EPI_HUGEINT)orgB->cntorig,
		       (c ? TXiindexTypeName(c) : "failed"), c,
		       (c ? (EPI_HUGEINT)c->cntorig : (EPI_HUGEINT)0));
	if (TXtraceIndexBits & 0x20000)
	{
		TXdumpIindex(NULL, 2, orgA);
		TXdumpIindex(NULL, 2, orgB);
		if (c) TXdumpIindex(NULL, 2, c);
	}
	return c;
}

/******************************************************************/
/*
 * This will sort a by b.
 *
 * a contains key1->recid.
 * b contains key2->recid.
 *
 * All recid's in a are also in b.  a will be "converted" to
 * key2->recid, for those key2 which have a corresponding
 * key1.
 *
 * WTF: Use array, qsort, and append
 *
 */

int
indsort(a, b, rev)
IINDEX *a;
IINDEX *b;
int	rev;
{
	static char Fn[] = "indsort";
#ifdef NEW_I
	DBIDX	*ab, *bb, *cb;
#else
	BTREE	*ab, *bb, *cb;
#endif
	BTLOC	bl1, bl2;

	ab = a->orig;
	if (rev)
	{
		if (indexrevinv(b) == -1)
			return -1;
		bb = b->revinv;
	}
	else
	{
		if (TXindexinv(b) == -1)
			return -1;
		bb = b->inv;
	}
#ifdef NEW_I
	a->ordered = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
#else
	a->ordered = openbtree(NULL, BTFSIZE, 20, BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
	if (a->ordered)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&a->ordered->params, DDICPN/*wtf*/);
#endif
	if (!a->ordered)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
	cb = a->ordered;
#ifdef NEW_I
	TXdbidxrewind(ab);
	TXdbidxrewind(bb);
	TXdbidxrewind(cb);
	bl1 = TXdbidxgetnext(ab, NULL, NULL, NULL);
#else
	rewindbtree(ab);
	rewindbtree(bb);
	rewindbtree(cb);
	bl1 = btgetnext(ab, NULL, NULL, NULL);	/* What's the recid */
#endif
	while (TXrecidvalid(&bl1))
	{
#ifdef NEW_I
		bl2 = dbidxsearch(bb, sizeof(bl1), &bl1); /* The key is */
#else
		bl2 = btsearch(bb, sizeof(bl1), &bl1); /* The key is */
#endif
		if (!TXrecidvalid(&bl2))
		{
#ifdef NEVER
			putmsg(MWARN, Fn, "Index Inconsistencies");
#endif
			/* Bad news */
		}
		else
		{
#ifdef NEW_I
			dbidxinsert(cb, &bl1, sizeof(bl2), &bl2);
#else
			btinsert(cb, &bl1, sizeof(bl2), &bl2);
#endif
		}
#ifdef NEW_I
		bl1 = TXdbidxgetnext(ab, NULL, NULL, NULL);
#else
		bl1 = btgetnext(ab, NULL, NULL, NULL);
#endif
	}
	return 0;
}
/******************************************************************/
/*
 * This will sort a by b.
 *
 * a contains key1->recid.
 * b contains key2->recid.
 *
 * All recid's in a are also in b.  a will be "converted" to
 * key2->recid, for those key2 which have a corresponding
 * key1.
 *
 * WTF: Use array, qsort, and append
 *
 */

int
TXindsort2(a, b, rev, dbidx)
IINDEX *a;
IINDEX *b;
int	rev;
DBIDX *dbidx;
{
	static char Fn[] = "indsort";
#ifdef NEW_I
	DBIDX	*bb, *cb;
#else
	BTREE	*bb, *cb;
#endif
	BTLOC	bl1, bl2;
	size_t	len;

	if (rev)
	{
		if (indexrevinv(b) == -1)
			return -1;
		bb = b->revinv;
	}
	else
	{
		if (TXindexinv(b) == -1)
			return -1;
		bb = b->inv;
	}
#ifdef NEW_I
	a->ordered = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 20, BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
#else
	a->ordered = openbtree(NULL, BTFSIZE, 20, BT_UNSIGNED | BT_FIXED, O_RDWR | O_CREAT);
	if (a->ordered)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&a->ordered->params, DDICPN/*wtf*/);
#endif
	if (!a->ordered)
	{
		putmsg(MERR+FOE, Fn, "Could not create index file");
		return -1;
	}
	cb = a->ordered;
#ifdef NEW_I
	TXdbidxrewind(bb);
	TXdbidxrewind(cb);
#else
	rewindbtree(bb);
	rewindbtree(cb);
#endif
	len = sizeof(buf);
	bl1 = getdbidx(dbidx, buf, &len, NULL);	/* What's the recid */
	while (TXrecidvalid(&bl1))
	{
#ifdef NEW_I
		bl2 = dbidxsearch(bb, sizeof(bl1), &bl1); /* The key is */
#else
		bl2 = btsearch(bb, sizeof(bl1), &bl1); /* The key is */
#endif
		if (!TXrecidvalid(&bl2))
		{
#ifdef NEVER
			putmsg(MWARN, Fn, "Index Inconsistencies");
#endif
			/* Bad news */
		}
		else
#ifdef NEW_I
			dbidxinsert(cb, &bl1, sizeof(bl2), &bl2);
#else
			btinsert(cb, &bl1, sizeof(bl2), &bl2);
#endif
		len = sizeof(buf);
		bl1 = getdbidx(dbidx, buf, &len, NULL);
	}
	return 0;
}

/******************************************************************/

#ifdef TEST

typedef struct tagInData {
	long	key;
	long	loc;
} InData;

InData  adata[] = {1, 10, 2, 20, 3, 40, 4, 30, 5, 25, -1, -1};
InData  bdata[] = {2, 10, 4, 15, 5, 30, 7, 22, 9, 45, -1, -1};
InData  cdata[] = {1, 25, 2, 40, 3, 22, 4, 45, 5, 30, 6, 20, 7, 10, 8, 15, -1, -1};

main()
{
	BTREE	*a, *b, *c;
	BTLOC	l;
	int	i;
	IINDEX	ia, ib, ic, id, ie, *ig, *ih, *ii;

	a = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	b = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	c = openbtree(NULL, BTFSIZE, 20, BT_FIXED, O_RDWR | O_CREAT);
	for (i=0; adata[i].key != -1; i++)
	{
		l.loc = adata[i].loc;
		btinsert(a, l, sizeof(long), &adata[i].key);
	}
	for (i=0; bdata[i].key != -1; i++)
	{
		l.loc = bdata[i].loc;
		btinsert(b, l, sizeof(long), &bdata[i].key);
	}
	for (i=0; cdata[i].key != -1; i++)
	{
		l.loc = cdata[i].loc;
		btinsert(c, l, sizeof(long), &cdata[i].key);
	}
	btprint(a);
	puts("");
	btprint(b);
	puts("");
	btprint(c);
	puts("");
	ia.orig = a;
	ib.orig = b;
	ic.orig = c;
	ig = indexand(&ia, &ib);
	if (ig)
	{
		btprint(ig->orig);
		puts("");
	}
	ih = indexor(&ia, &ib);
	if (ih)
	{
		btprint(ih->mirror);
		puts("");
	}
	indsort(&ia, &ic);
	if (ia.ordered)
	{
		btprint(ia.ordered);
		puts("");
	}
}

#endif
#endif
