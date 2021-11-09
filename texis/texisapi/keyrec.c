/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "keyrec.h"
#include "fldcmp.h"
#include "fdbi.h"
#include "cgi.h"

#define OPTIMIZE_KEYREC_SORT	/* saves an unneeded re-sort */

static int fldcmp2(byte *, size_t, byte *, size_t, FLDCMP *);
static int fldcmp3(byte *, size_t, byte *, size_t, FLDCMP *, KEYREC *);

/******************************************************************/

KEYREC *
keyrecclose(keyrec)
KEYREC *keyrec;
{
	if(!keyrec)
		return keyrec;
	if(keyrec->fname)
		keyrec->fname = TXfree(keyrec->fname);
	if(keyrec->items)
	{
		keyrec->items = TXfree(keyrec->items);
	}
	keyrec->toprows = closefheap(keyrec->toprows);
	keyrec->cmporder = TXfree(keyrec->cmporder);
	keyrec->dd = closedd(keyrec->dd);
	keyrec = TXfree(keyrec);
	fldcmp2(0,0,0,0,0); /* Reset fldcmp stored info */
	fldcmp3(0,0,0,0,0,0); /* Reset fldcmp stored info */
	return NULL;
}

/******************************************************************/

static int
keyreccmp(const void *a, const void *b, void *usr)
/* Sort function for sorting by rank, recid. */
{
        int     rc;
	CONST byte *ap=a, *bp=b;

	(void)usr;
	/* Compare ranks: */
	rc = *(EPI_OFF_T *)(ap + TX_KEYREC_ITEM_DATA_OFFSET) -
	     *(EPI_OFF_T *)(bp + TX_KEYREC_ITEM_DATA_OFFSET);
	/* If same, compare recids: */
	if (rc == 0)
		rc = TXrecidcmp((BTLOC *)(ap + TX_KEYREC_ITEM_BTLOC_OFFSET),
				(BTLOC *)(bp + TX_KEYREC_ITEM_BTLOC_OFFSET));
	return rc;
}

/******************************************************************/

static int
TXkeyrecCmpRankDescRecid(const void *a, const void *b, void *usr)
/* Sort function for sorting by rank desc, recid. */
{
        int     rc;
	const byte *ap = a, *bp = b;

	(void)usr;
	/* Compare ranks: */
	rc = *(EPI_OFF_T *)(bp + TX_KEYREC_ITEM_DATA_OFFSET) -
	     *(EPI_OFF_T *)(ap + TX_KEYREC_ITEM_DATA_OFFSET);
	/* If same, compare recids: */
	if (rc == 0)
		rc = TXrecidcmp((BTLOC *)(ap + TX_KEYREC_ITEM_BTLOC_OFFSET),
				(BTLOC *)(bp + TX_KEYREC_ITEM_BTLOC_OFFSET));
	return(rc);
}

int
TXkeyrecSetCmpRankDescRecid(KEYREC *keyrec)
{
	keyrec->cmpf = TXkeyrecCmpRankDescRecid;
	return(1);
}

/******************************************************************/

KEYREC *
keyrecopen(fname, keysz, ranksize, extrasize, flags, inc, dd)
char *fname;
size_t keysz;
size_t ranksize;
size_t extrasize;
long flags;
int inc;
DD	*dd;		/* (in) dup'd */
{
	static const char	fn[] = "keyrecopen";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	KEYREC *rc;

	rc = (KEYREC *)TXcalloc(pmbuf, fn, 1, sizeof(KEYREC));
	if(!rc)
		return rc;

	if(fname)
		rc->fname = TXstrdup(pmbuf, fn, fname);
	else
		rc->fname = NULL;
	rc->keysz = TX_ALIGN_UP_SIZE_T(keysz);
	rc->ranksz = ranksize;
	rc->extrasz = extrasize;
	rc->totalsz = rc->keysz + ranksize + extrasize;
	rc->flags = flags;
	rc->increment = inc;
	rc->sorted = 1;
	rc->alloced = 0;
	rc->used = 0;
	rc->cloc = 0;
	rc->cmpf = keyreccmp;
	rc->dd = TXdupDd(dd);
	return rc;
}

/******************************************************************/

static int
keyrecheapcmp(void *a, void *b, void *usr)
{
	KEYREC	*keyrec = (KEYREC *)usr;

	/* Negate `cmpf', because we want top of heap to be the *last*
	 * item of the top `maxsortrows', not first:
	 */
	return(-keyrec->cmpf(a, b, usr));
}

/******************************************************************/

int
keyrecsetmaxsortrows(keyrec, maxsortrows)
KEYREC *keyrec;
int maxsortrows;
{
	if(keyrec)
	{
		if(maxsortrows > 0)
		{
			if(keyrec->used > 0)
				return -1;
			keyrec->toprows = closefheap(keyrec->toprows);
			keyrec->toprows = openfheap(keyrecheapcmp, keyrec, 0);
			fheap_alloc(keyrec->toprows, maxsortrows);
		}
		else
		{
			keyrec->toprows = closefheap(keyrec->toprows);
		}
		keyrec->maxsortrows = maxsortrows;
		return 0;
	}
	return -1;
}

#if 0
static int
keyrecsetcmp(KEYREC *keyrec, keyreccmptype cmpf)
{
	if(keyrec)
	{
		keyrec->cmpf = cmpf;
		return 0;
	}
	return -1;
}
#endif /* 0 */

/******************************************************************/

static int
adjust_fheap(FHEAP *fh, int delta)
{
	size_t i;

	if(delta == 0)
		return 0;
	for(i = 0; i < fheap_num(fh); i++)
	{
		fheap_elem(fh, i) = (char *)fheap_elem(fh, i) + delta;
	}
	return i;
}

/******************************************************************/

int
keyrecappend(keyrec, btloc, key, ranktok, extrad)
KEYREC *keyrec;
BTLOC *btloc;
void *key;
EPI_OFF_T ranktok;
void *extrad;
{
	static const char Fn[] = "keyrecappend";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	size_t kisz;
	byte	*newbuf, *d;
	char	*xtraMsg = NULL;

	kisz = TX_KEYREC_ITEM_SZ(keyrec);
	if(keyrec->used >= keyrec->alloced)
	{
		/* If we realloc, and we have a heap, we may need
		   to adjust all the pointers in the heap */
		int adjustment = 0;
		int pointer_delta = 0;
		byte *old_pointer = NULL;

		keyrec->alloced += keyrec->increment;
		if(keyrec->toprows && keyrec->items)
		{
			old_pointer = keyrec->items;
			adjustment++;
		}
		newbuf = (keyrec->items == BYTEPN ?
			  (byte *)TXmalloc(pmbuf, Fn, keyrec->alloced*kisz):
			  (byte *)TXrealloc(pmbuf, Fn, keyrec->items,
					    keyrec->alloced*kisz));
		if (!newbuf)
		{
#ifdef EPI_REALLOC_FAIL_SAFE
			keyrec->items = TXfree(keyrec->items);
#endif /* EPI_REALLOC_FAIL_SAFE */
		}
		keyrec->items = newbuf;
		if(adjustment)
		{
			pointer_delta = keyrec->items - old_pointer;
			adjust_fheap(keyrec->toprows, pointer_delta);
		}
	}
	if(!keyrec->items)
		return -1;
	d = TX_KEYREC_ITEM_DATA(keyrec, keyrec->used);
#if defined(EPI_TXALIGN_OFF_T_COPY_SAFE)
	*(EPI_OFF_T *)d = ranktok;
#else
	memcpy(d, &ranktok, sizeof(ranktok));
#endif
	d += keyrec->ranksz;
	memcpy(d, key, keyrec->keysz);
	d += keyrec->keysz;
	if(keyrec->extrasz)
	{
		memcpy(d, extrad, keyrec->extrasz);
	}
#if defined(EPI_TXALIGN_OFF_T_COPY_SAFE)
	*(BTLOC *)TX_KEYREC_ITEM_BTLOC(keyrec, keyrec->used) = *btloc;
#else
	memcpy(TX_KEYREC_ITEM_BTLOC(keyrec, keyrec->used), btloc,
	       sizeof(BTLOC));
#endif
	keyrec->used++;
	keyrec->sorted = 0;

	/* If maxsortrows in effect, update heap, and maybe discard item: */
	if(keyrec->toprows)
	{
		FHEAP *heap = keyrec->toprows;
		void *heapTopItem, *item;

		item = TX_KEYREC_ITEM(keyrec, keyrec->used - 1);
		if(fheap_num(heap) >= (size_t)keyrec->maxsortrows)
		{				/* heap is full */
			heapTopItem = fheap_top(heap);
			/* Heap sorts opposite of cmpf, so
			 * `heapTopItem' is the *largest* (hence last)
			 * sorted item we will eventually return in
			 * getnext.  So if `item' >= `heapTopItem',
			 * `item' is later and does not make it into heap:
			 */
			if (keyrec->cmpf(item, heapTopItem, keyrec) >= 0)
			{
				if (TXtraceIndexBits & 0x400000)
				{
					char	*discardTuple, *heapTuple;

					discardTuple=TXkeyrecTupleToStr(keyrec,
							    keyrec->used - 1);
					heapTuple = TXkeyrecTupleToStr(keyrec,
						 TX_KEYREC_ITEM_INDEX(keyrec,
								heapTopItem));
					putmsg(MINFO, __FUNCTION__,
		  "Discarded rank %wd %s: Not better than keyrec heap top %s",
					       (EPI_HUGEINT)ranktok,
					       discardTuple, heapTuple);
					discardTuple = TXfree(discardTuple);
					heapTuple = TXfree(heapTuple);
				}
				/* Save mem and discard `item'; not in
				 * the heap, and we only return heap
				 * items in keyrecgetnext():
				 */
				keyrec->used--;
				return 0;
			}
			/* `item' < top of heap; replace top with `item': */
			if (TXtraceIndexBits & 0x400000)
			{
				char	*discardTuple;

				discardTuple = TXkeyrecTupleToStr(keyrec,
				   TX_KEYREC_ITEM_INDEX(keyrec, heapTopItem));
				xtraMsg = TXstrcatN(TXPMBUFPN, __FUNCTION__,
						    "; deleted earlier ",
						    discardTuple,
						    " from keyrec heap",
						    NULL);
				discardTuple = TXfree(discardTuple);
			}
			fheap_deletetop(heap);
		}
		fheap_insert(heap, item);
		if (TXtraceIndexBits & 0x400000)
		{
			char	*newXtra;

			newXtra = TXstrcatN(TXPMBUFPN, __FUNCTION__,
					    " and inserted into keyrec heap",
					    (xtraMsg ? xtraMsg : ""), NULL);
			xtraMsg = TXfree(xtraMsg);
			xtraMsg = newXtra;
			newXtra = NULL;
		}
	}

	if (TXtraceIndexBits & 0x400000)
	{
		char	*tuple;

		tuple = TXkeyrecTupleToStr(keyrec, keyrec->used - 1);
		putmsg(MINFO, __FUNCTION__,
		       "Appended rank %wd %s to keyrec%s",
		       (EPI_HUGEINT)ranktok, tuple, (xtraMsg ? xtraMsg : ""));
		tuple = TXfree(tuple);
		xtraMsg = TXfree(xtraMsg);
	}

	return 0;
}

/******************************************************************/

BTLOC
keyrecgetnext(keyrec, data, sz)
KEYREC *keyrec;
void *data;
size_t *sz;
{
	BTLOC	rc;

	TXsetrecid(&rc, -1);
	if(!keyrec)
		return rc;

	/* If we have a maxsortrows heap, return just its items, in order: */
	if(keyrec->toprows)
	{
		void *item;
		/* NOTE: see also TXkeyrecDump() if this changes: */
#ifdef OPTIMIZE_KEYREC_SORT
		/* keyrecsort() put heap items onto stack: */
		if (fheap_stknum(keyrec->toprows) <= 0)
			return(rc);
		item = fheap_stktop(keyrec->toprows);
		keyrec->cloc = TX_KEYREC_ITEM_INDEX(keyrec, item);
		fheap_stkpop(keyrec->toprows);
#else /* !OPTIMIZE_KEYREC_SORT */
		/* Note that this is proper (ascending `cmpf') order
		 * -- despite negation in keyrecheapcmp() -- because
		 * keyrecsort() reset heap compare func to `cmpf' and
		 * re-sorted:
		 */
		if(fheap_num(keyrec->toprows) <=0 )
			return rc;
		item = fheap_top(keyrec->toprows);
		keyrec->cloc = TX_KEYREC_ITEM_INDEX(keyrec, item);
		fheap_deletetop(keyrec->toprows);
#endif /* !OPTIMIZE_KEYREC_SORT */
	}

	/* Now get item `cloc': */
	if((keyrec->cloc >= keyrec->used) ||
	   TX_SIZE_T_VALUE_LESS_THAN_ZERO(keyrec->cloc))
		return rc;
	if(data)
		if(*sz >= keyrec->totalsz)
			memcpy(data,TX_KEYREC_ITEM_DATA(keyrec, keyrec->cloc),
			       keyrec->totalsz);
	if(sz)
		*sz = keyrec->totalsz;
#if defined(EPI_TXALIGN_OFF_T_COPY_SAFE)
	rc = *(BTLOC *)TX_KEYREC_ITEM_BTLOC(keyrec, keyrec->cloc);
#else
	memcpy(&rc, TX_KEYREC_ITEM_BTLOC(keyrec, keyrec->cloc),
	       sizeof(rc));
#endif
	keyrec->cloc++;
	return rc;
}

/******************************************************************/

int
keyrecrewind(keyrec)
KEYREC *keyrec;
{
	if(!keyrec)
		return -1;
	keyrec->cloc = 0;
	return 0;
}

/* ------------------------------------------------------------------------ */

char *
TXkeyrecTupleToStr(KEYREC *keyrec, size_t itemIdx)
/* Prints tuple (e.g. `recid 0xNNN data (0x0000123, 5)' at index `itemIdx'
 * to an alloc'd string and returns it, or NULL on error.
 */
{
	char	*tupleBuf = NULL, *newBuf, *d, *e;
	size_t	tupleBufSz = 0, tupleBufUsedLen = 0;
	byte	*data;
	TBL	*tbl = NULL;
	RECID	recid;

	data = TX_KEYREC_ITEM_DATA(keyrec, itemIdx);
	tbl = createtbl(keyrec->dd, TXNOOPDBF_PATH);

	do
	{
		/* (re)alloc buffer: */
		tupleBufSz = (tupleBufSz << 1) + 1024;
		newBuf = (char *)TXrealloc(TXPMBUFPN, __FUNCTION__,
					   tupleBuf, tupleBufSz);
		if (!newBuf)
		{
#ifndef EPI_REALLOC_FAIL_SAFE
			tupleBuf = NULL;	/* realloc() freed it */
#endif /* EPI_REALLOC_FAIL_SAFE */
			goto err;
		}
		tupleBuf = newBuf;

		/* Print `recid (tuple)': */
		d = tupleBuf;
		e = tupleBuf + tupleBufSz;
		memcpy(&recid, TX_KEYREC_ITEM_BTLOC(keyrec, itemIdx),
		       sizeof(RECID));
		d += htsnpf(d, e - d, "recid 0x%08wx data ",
			    (EPI_HUGEINT)TXgetoff2(&recid));
		if (buftofld(data, tbl, keyrec->totalsz) == 0)
		{
			char	*tupleStr = NULL;

			/* Rank stored as ft_handle; print in base 10
			 * for clarity:
			 */
			TXfldtostrHandleBase10++;
			tupleStr = TXtblTupleToStr(tbl);
			TXfldtostrHandleBase10--;
			if (d < e) TXstrncpy(d, tupleStr, e - d);
			d += strlen(tupleStr);
			tupleStr = TXfree(tupleStr);
		}
		else
		{
			TXstrncpy(tupleBuf, "(?)", tupleBufSz);
			d += 3;
		}
		tupleBufUsedLen = (size_t)(d - tupleBuf);
	}
	while (tupleBufUsedLen >= tupleBufSz);

	goto finally;

err:
	tupleBuf = TXfree(tupleBuf);
finally:
	tbl = closetbl(tbl);
	if (tupleBuf && tupleBufUsedLen < tupleBufSz)	/* save mem */
		tupleBuf = (char *)TXrealloc(TXPMBUFPN, __FUNCTION__,
					     tupleBuf, tupleBufUsedLen + 1);
	return(tupleBuf);
}

/* ------------------------------------------------------------------------ */

int
TXkeyrecDump(TXPMBUF *pmbuf, KEYREC *keyrec, int indent)
/* Dumps `keyrec' values to `pmbuf', with no `keyrec' side effects.
 * Assumed to be called *after* keyrecsort().
 */
{
	size_t	itemIdx;
	char	*tupleStr;

	if (keyrec->toprows)
	{
		FHEAP	*tmpHeap;
		void	*item;

		tmpHeap = TXfheapDup(keyrec->toprows);
#ifdef OPTIMIZE_KEYREC_SORT
		while (fheap_stknum(tmpHeap) > 0)
#else /* !OPTIMIZE_KEYREC_SORT */
		while (fheap_num(tmpHeap) > 0)
#endif /* !OPTIMIZE_KEYREC_SORT */
		{
#ifdef OPTIMIZE_KEYREC_SORT
			item = fheap_stktop(tmpHeap);
#else /* !OPTIMIZE_KEYREC_SORT */
			item = fheap_top(tmpHeap);
#endif /* !OPTIMIZE_KEYREC_SORT */
			itemIdx = TX_KEYREC_ITEM_INDEX(keyrec, item);
			tupleStr = TXkeyrecTupleToStr(keyrec, itemIdx);
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN, "%*s%s",
				       (int)indent, "", tupleStr);
			tupleStr = TXfree(tupleStr);
#ifdef OPTIMIZE_KEYREC_SORT
			fheap_stkpop(tmpHeap);
#else /* !OPTIMIZE_KEYREC_SORT */
			fheap_deletetop(tmpHeap);
#endif /* !OPTIMIZE_KEYREC_SORT */
		}
		tmpHeap = closefheap(tmpHeap);
	}
	else
	{
		for (itemIdx = 0; itemIdx < keyrec->used; itemIdx++)
		{
			tupleStr = TXkeyrecTupleToStr(keyrec, itemIdx);
			txpmbuf_putmsg(pmbuf, MINFO, CHARPN, "%*s%s",
				       (int)indent, "", tupleStr);
			tupleStr = TXfree(tupleStr);
		}
	}
	return(1);
#undef STRBUFSZ
}

/******************************************************************/

int
keyrecsort(keyrec)
KEYREC *keyrec;
{
	TXPMBUF	*pmbuf = TXPMBUFPN;

	if(!keyrec)
		return -1;
	if(keyrec->sorted)
		return 0;
	if(keyrec->toprows)
	{
		/* Heap sort func is currently keyrecheapcmp(), which
		 * sorts by `keyrec->cmpf()' but in reverse.  We want
		 * records in `keyrec->cmpf()' order:
		 */
#ifdef OPTIMIZE_KEYREC_SORT
		if (TXtraceIndexBits & 0x200000)/* Bug 6796 tracing */
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
	     "Sorting %wkd (of %wkd) keyrec results via heap delete to stack",
				       (EPI_HUGEINT)fheap_num(keyrec->toprows),
				       (EPI_HUGEINT)keyrec->used);
		/* Pulling the records off will still get them in
		 * order, just reverse order.  Ok; just put them on
		 * the heap's stack; keyrecgetnext() will return them
		 * in reverse-reverse (correct) order.  This saves an
		 * fheap_reheap() re-sort, and does not cost any mem:
		 */
		fheap_stkreset(keyrec->toprows);
		while (fheap_num(keyrec->toprows) > 0)
		{
			void	*item;

			item = fheap_top(keyrec->toprows);
			fheap_deletetop(keyrec->toprows);
			fheap_stkpush(keyrec->toprows, item);
		}
#else /* !OPTIMIZE_KEYREC_SORT */
		if (TXtraceIndexBits & 0x200000)/* Bug 6796 tracing */
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
			  "Sorting %wkd (of %wkd) keyrec results via re-heap",
				       (EPI_HUGEINT)fheap_num(keyrec->toprows),
				       (EPI_HUGEINT)keyrec->used);
		if (keyrec->used < 2) return(0);	/* optimization */
		/* Re-sort the heap with correct-order `keyrec->cmpf()': */
		fheap_setcmp(keyrec->toprows, keyrec->cmpf);
		fheap_reheap(keyrec->toprows);
#endif /* !OPTIMIZE_KEYREC_SORT */
		if (TXtraceIndexBits & 0x800000)/* Bug 6796 tracing */
		{
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "%wkd keyrec records after sorting:",
			     (EPI_HUGEINT)fheap_stknum(keyrec->toprows));
			TXkeyrecDump(pmbuf, keyrec, 2);
		}
	}
	else
	{
		if (TXtraceIndexBits & 0x200000)/* Bug 6796 tracing */
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				      "Sorting %wkd keyrec results via qsort",
				       (EPI_HUGEINT)keyrec->used);
		if (keyrec->used < 2) return(0);	/* optimization */
		TXqsort_r(keyrec->items, keyrec->used,
			  TX_KEYREC_ITEM_SZ(keyrec), keyrec->cmpf, keyrec);
		keyrec->sorted = 1;
		if (TXtraceIndexBits & 0x800000)/* Bug 6796 tracing */
		{
			txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				       "%wkd keyrec records after sorting:",
			     (EPI_HUGEINT)keyrec->used);
			TXkeyrecDump(pmbuf, keyrec, 2);
		}
	}
	fldcmp2(0,0,0,0,0); /* Reset fldcmp stored info */
	fldcmp3(0,0,0,0,0,0); /* Reset fldcmp stored info */
	return 0;
}

/******************************************************************/

#if 0
static BTLOC
keyrecsearch(KEYREC *keyrec, size_t sz, void *key)
{
	BTLOC rc;
	byte keyitem[BT_MAXPGSZ];
	int l, r, c, i;

	(void)sz;
	TXsetrecid(&rc, -1);
	if(!keyrec)
		return rc;
	keyrecsort(keyrec);
	memcpy(keyitem + TX_KEYREC_ITEM_DATA_OFFSET, key, keyrec->totalsz);
	TXsetrecid((BTLOC *)keyitem, 0);
	l = 0;
	r = keyrec->used - 1;
	while(l < r)
	{
		i = (l+r)/2;
		c = keyrec->cmpf(TX_KEYREC_ITEM(keyrec, i), keyitem, keyrec);
		if (c > 0)
			r = i;
		else
			l = i + 1;
	}
	c = keyrec->cmpf(TX_KEYREC_ITEM(keyrec, r), keyitem, keyrec);
	if(c == 0)
#if defined(EPI_TXALIGN_OFF_T_COPY_SAFE)
		rc = *(BTLOC *)TX_KEYREC_ITEM_BTLOC(keyrec, r);
#else
		memcpy(&rc, TX_KEYREC_ITEM_BTLOC(keyrec, r),
		       sizeof(BTLOC));
#endif
	return rc;
}
#endif /* 0 */

/******************************************************************/

static int
keyrecfldcmp(const void *a, const void *b, void *usr)
/* qsort_r() compare function
 */
{
        int     rc;
	CONST byte *ap=a, *bp=b;
	KEYREC	*keyrec = (KEYREC *)usr;

	rc = fldcmp3((byte *)ap + TX_KEYREC_ITEM_DATA_OFFSET, keyrec->totalsz,
	            (byte *)bp + TX_KEYREC_ITEM_DATA_OFFSET, keyrec->totalsz,
		     keyrec->fldcmp, keyrec);
	if (rc == 0)
		rc = TXrecidcmp((BTLOC *)ap, (BTLOC *)bp);
	return rc;
}

/* ------------------------------------------------------------------------ */

const char *
TXkeyrecCmpFuncToStr(KEYREC *keyrec)
{
	if (keyrec->cmpf == keyreccmp) return("keyreccmp");
	if (keyrec->cmpf == keyrecfldcmp) return("keyrecfldcmp");
	if (keyrec->cmpf == TXkeyrecCmpRankDescRecid)
		return("TXkeyrecCmpRankDescRecid");
	return("?");
}

/******************************************************************/

int
keyrecsetfldcmp(keyrec, fldcmp)
KEYREC *keyrec;
FLDCMP *fldcmp;
{
	if(!keyrec)
		return -1;
	keyrec->fldcmp = fldcmp;
	keyrec->cmpf = keyrecfldcmp;
	fldcmp2(0,0,0,0,0); /* Reset fldcmp stored info */
	fldcmp3(0,0,0,0,0,0); /* Reset fldcmp stored info */
	return 0;
}

/******************************************************************/
/*	Compare two fields using the field math stuff.  Takes a
 *	buffer as would be stored in BTREE.
 *
 *	The usr structure is intended to have a FLDCMP structure
 *	which will contain a table to decode the fields.
 *
 *	Returns -1, 0, 1 if buf1 is less than, equal to or greater
 *	than buf2.
 *
 *	Assumes no var.
 */

/******************************************************************/

typedef struct FLDCMP_INFO
{
	int	type;
	EPI_SSIZE_T	off;
	int	rev;
	FLD	fld1;
	FLD	fld2;
} FLDCMP_INFO;

/******************************************************************/

static int
locfldcmp2(void *vp1, void *vp2, int type, int *status, int rev)
/* Compares FLD.data `vp1' and `vp2', of FTN `type'.  Sets `*status'
 * to 0 on success, -1 on error (e.g. unknown type).  `rev' is OF_... flags;
 * only OF_IGN_CASE respected, caller must handled others e.g. OF_DESCENDING.
 * Returns < 0 if `vp1' less than `vp2', 0 if equal, > 0 if `vp1' greater.
 */
{
	int	r;
/* Bug 4753: compare values without integer overflow, ala locflcmp(): */
#define CMPVALS(a, b)	((a) < (b) ? -1 : ((a) > (b) ? 1 : 0))

	*status = 0;

	switch (type & FTN_VarBaseTypeMask)
	{
		case FTN_INT:
		{
			ft_int	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_INTEGER:
		{
			ft_integer	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_LONG:
		{
			ft_long	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_SHORT:
		{
			ft_short	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_SMALLINT:
		{
			ft_smallint	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_COUNTER:
		{
			ft_counter	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			CTRCMP(v1, v2, r);
			return r;
		}
#ifndef NEVER/* WTF HANDLE Case Insensitive */
		case FTN_CHAR:
		case FTN_CHAR | DDVARBIT:
		{
			CONST ft_char	*v1, *v2;
			TXCFF	mode;

			v1 = vp1;
			v2 = vp2;
			TXget_globalcp();
			mode = globalcp->stringcomparemode;
			if (rev & OF_IGN_CASE)
				mode = TXCFF_SUBST_CASESTYLE(mode,
						TXCFF_CASESTYLE_IGNORE);
			return(TXunicodeStrFoldCmp(&v1, -1, &v2, -1, mode));
		}
#endif
		case FTN_DWORD:
		{
			ft_dword	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_WORD:
		{
			ft_word	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_INT64:
		{
			ft_int64	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_UINT64:
		{
			ft_uint64	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_DATE:
		{
			ft_date	*v1, *v2;

			v1 = vp1;
			v2 = vp2;
			return(CMPVALS(*v1, *v2));
		}
		case FTN_FLOAT:
		{
			ft_float	v1, v2;

			v1 = *(ft_float *)vp1;
			v2 = *(ft_float *)vp2;
			return(TXFLOAT_CMP(v1, v2));
		}
		case FTN_DOUBLE:
		{
			ft_double	v1, v2;

			v1 = *(ft_double *)vp1;
			v2 = *(ft_double *)vp2;
			return(TXDOUBLE_CMP(v1, v2));
		}
		case FTN_BYTE:
		case FTN_DECIMAL:
		case FTN_BLOB:
		case FTN_HANDLE:
		case FTN_INDIRECT:
		case FTN_BLOBI:
		case FTN_STRLST:
		case FTN_DATESTAMP:
		case FTN_TIMESTAMP:
		default:
#ifdef TX_PEDANTIC
			putmsg(900, "locfldcmp2", "Unknown type %d", type);
#endif /* TX_PEDANTIC */
			*status = -1;
			return 0;
	}
#undef CMPVALS
}

/******************************************************************/

static int
fldcmp2(buf1, fld1sz, buf2, fld2sz, usr)
byte *buf1, *buf2;
size_t fld1sz, fld2sz;
FLDCMP *usr;
{
	static const char	fn[] = "fldcmp2";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	FLD *r, *fld1, *fld2;
	FLDOP *fo;
	size_t n, i;
	int rc, rev;
	TXCFF	oign = (TXCFF)0;
	static	FLDCMP *lastusr = NULL;
	static	FLDCMP_INFO *lastinfo = NULL;
	static  size_t ncmps = 0;

	if(!usr)
	{
		lastusr = usr;
		lastinfo = TXfree(lastinfo);
		return 0;
	}
	fo = usr->fo;
	if(!lastusr)
	{
		buftofld(buf1, usr->tbl1, fld1sz);
		buftofld(buf2, usr->tbl2, fld2sz);
		ncmps=0;
		for (i=0; i < usr->tbl1->n; i++)
		{
			rev = usr->tbl2->dd->fd[i].order;
			if(rev & OF_DONT_CARE) continue;
			ncmps++;
		}
		lastinfo = (FLDCMP_INFO *)TXcalloc(pmbuf, fn, ncmps,
						   sizeof(FLDCMP_INFO));
		ncmps=0;
		for (i=0; i < usr->tbl1->n; i++)
		{
			rev = usr->tbl2->dd->fd[i].order;
			if(rev & OF_DONT_CARE) continue;
			fld1 = usr->tbl1->field[i];
			/* wtf `fld2' should probably be usr->tbl2->field[i]
			 * not tbl1; but later on we assume fld1/fld2
			 * are the same type anyway? KNG 20130604
			 */
			fld2 = usr->tbl1->field[i];
			lastinfo[ncmps].rev = rev;
			lastinfo[ncmps].type = fld1->type;
			lastinfo[ncmps].off = (byte *)fld1->v - buf1;
			initfld(&lastinfo[ncmps].fld1, fld1->type, fld1->n);
			initfld(&lastinfo[ncmps].fld2, fld2->type, fld2->n);
			ncmps++;
		}
		lastusr = usr;
	}
	for(i=0; i < ncmps; i++)
	{
		int	status;

		fld1 = &lastinfo[i].fld1;
		fld1->shadow = buf1 + lastinfo[i].off;
		fld2 = &lastinfo[i].fld2;
		fld2->shadow = buf2 + lastinfo[i].off;
		rev = lastinfo[i].rev;
		setfldv(fld1);
		setfldv(fld2);
		rc = locfldcmp2(fld1->v, fld2->v, fld1->type, &status, rev);
		if(status == 0 && rc != 0)
		{
			if(rev & OF_DESCENDING)
				rc = rc * -1;
			return rc;
		}
		if(status == 0)
			continue;
		fopush(fo, fld1);
		fopush(fo, fld2);
		if(rev & OF_IGN_CASE)
		{
			TXget_globalcp();
			oign = globalcp->stringcomparemode;
			globalcp->stringcomparemode = TXCFF_SUBST_CASESTYLE(
			  globalcp->stringcomparemode, TXCFF_CASESTYLE_IGNORE);
		}
		rc = foop(fo, FOP_COM);
		if(rev & OF_IGN_CASE)
		{
			globalcp->stringcomparemode = oign;
		}
		r = fopeek(fo);
		if (r != (FLD *)NULL)
		{
			rc = (int)*((ft_int *) getfld(r, &n));
			fodisc(fo);
			if(rev & OF_DESCENDING)
				rc = rc * -1;
			if(rc)
				return rc;
		}
	}
	return 0;
}

/******************************************************************/

static int
fldcmp3(byte *buf1, size_t fld1sz, byte *buf2, size_t fld2sz,
	FLDCMP *usr, KEYREC *keyrec)
{
	static const char	fn[] = "fldcmp3";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	FLD *r, *fld1, *fld2;
	void	*data;
	FLDOP *fo;
	size_t n;
	int rc, i, rev;
	TXCFF	oign = (TXCFF)0;
	static	FLDCMP *lastusr = NULL;
	static	FLDCMP_INFO *lastinfo = NULL;
	static  int ncmps = 0;

	if(!usr)
	{
		lastusr = usr;
		lastinfo = TXfree(lastinfo);
		return 0;
	}
	fo = usr->fo;
	if(!lastusr)
	{
		buftofld(buf1, usr->tbl1, fld1sz);
		buftofld(buf2, usr->tbl2, fld2sz);
		ncmps=keyrec->auxalloced;
		lastinfo = (FLDCMP_INFO *)TXcalloc(pmbuf, fn, ncmps,
						   sizeof(FLDCMP_INFO));
		for (i=0; i < ncmps; i++)
		{
			rev = usr->tbl2->dd->fd[keyrec->cmporder[i]].order;
			fld1 = usr->tbl1->field[keyrec->cmporder[i]];
			/* wtf `fld2' should probably be usr->tbl2->...
			 * not tbl1; but later on we assume fld1/fld2
			 * are the same type anyway? KNG 20130604
			 */
			fld2 = usr->tbl1->field[keyrec->cmporder[i]];
			lastinfo[i].rev = rev;
			lastinfo[i].type = fld1->type;
			lastinfo[i].off = (byte *)fld1->v - buf1;
			initfld(&lastinfo[i].fld1, fld1->type, fld1->n);
			initfld(&lastinfo[i].fld2, fld2->type, fld2->n);
		}
		lastusr = usr;
	}
	for(i=0; i < ncmps; i++)
	{
		int	status;

		fld1 = &lastinfo[i].fld1;
		fld1->shadow = buf1 + lastinfo[i].off;
		fld2 = &lastinfo[i].fld2;
		fld2->shadow = buf2 + lastinfo[i].off;
		rev = lastinfo[i].rev;
		setfldv(fld1);
		setfldv(fld2);
		rc = locfldcmp2(fld1->v, fld2->v, fld1->type, &status, rev);
		if(status == 0 && rc != 0)
		{
			if(rev & OF_DESCENDING)
				rc = rc * -1;
			return rc;
		}
		if(status == 0)
			continue;
		fopush(fo, fld1);
		fopush(fo, fld2);
		if(rev & OF_IGN_CASE)
		{
			TXget_globalcp();
			oign = globalcp->stringcomparemode;
			globalcp->stringcomparemode = TXCFF_SUBST_CASESTYLE(
			  globalcp->stringcomparemode, TXCFF_CASESTYLE_IGNORE);
		}
		rc = foop(fo, FOP_COM);
		if(rev & OF_IGN_CASE)
		{
			globalcp->stringcomparemode = oign;
		}
		r = fopeek(fo);
		if (rc == -1)
		{
#ifdef TX_PEDANTIC
			putmsg(900, "fldcmp3", "foop(FOP_COM) failed");
#endif /* TX_PEDANTIC */
			return 0;
		}
		if (r != (FLD *)NULL)
		{
			data = getfld(r, &n);
			if (data == NULL)
			{
#ifdef TX_PEDANTIC
				putmsg(900, "fldcmp3", "getfld() is NULL");
#endif /* TX_PEDANTIC */
				return 0;
			}
			if ((r->type & DDTYPEBITS) != FTN_INT)
			{
#ifdef TX_PEDANTIC
				putmsg(900, "fldcmp3", "r is not FTN_INT");
#endif /* TX_PEDANTIC */
				return 0;
			}
			rc = (int)*((ft_int *)data);
			fodisc(fo);
			if(rev & OF_DESCENDING)
				rc = rc * -1;
			if(rc)
				return rc;
		}
	}
	return 0;
}
