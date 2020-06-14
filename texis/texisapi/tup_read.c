/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "txcoreconfig.h"
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <ctype.h>
#include <time.h>
#include "sizes.h"
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"


static int tup_copy3 ARGS((DBTBL *, DBTBL *, FLDOP *));

static byte tempbuf[BT_REALMAXPGSZ];  /* Somewhere to put rows from BTREE */
#ifdef OLD_STATS
static ft_dword one = 1;
#endif
/******************************************************************/

DBTBL *
dostats(t, fo)
DBTBL *t;
FLDOP *fo;
{
#ifndef OLD_STATS
	TXaddstatrow(t->nfldstat, t, fo);
	return t;
#else /* OLD_STATS */
	int i;
	double v;
	FLD *fld, *f1;
	FLDSTAT *fs;
	size_t	sz;

	if (!t->needstats)
	{
		return t;
	}
	for (
		i=0, fld = getfldn(t->tbl, i, NULL);
		fld;
		i++, fld = getfldn(t->tbl, i, NULL))
	{
		FLD	*f;

		fs = &t->fldstats[i];

		if(fs->count)
		{
			f = createfld("dword", 1, 1);
			putfld(f, &one, 1);
			fopush(fo, f);
			if (getfld(fs->count, &sz))
			{
				fopush(fo, fs->count);
				foop(fo, FOP_ADD);
			}
			closefld(fs->count);
			fs->count = fopop(fo);
			f = closefld(f);
		}

		if(fs->sum)
		{
			fopush(fo, fld);
			if(getfld(fs->sum, &sz))
			{
				fopush(fo, fs->sum);
				foop(fo, FOP_ADD);
			}
			closefld(fs->sum);
			fs->sum = fopop(fo);
		}

		if(fs->min)
		{
			fopush(fo, fld);
			if(getfld(fs->min, &sz))
			{
				fopush(fo, fs->min);
				foop(fo, FOP_LT);
				f1 = fopop(fo);
				if (*(ft_int *)getfld(f1, &sz))
				{
					closefld(fs->min);
					fs->min = dupfld(fld);
				}
				closefld(f1);
			}
			else
			{
				closefld(fs->min);
				fs->min = dupfld(fld);
			}
		}

		if(fs->max)
		{
			fopush(fo, fld);
			if(getfld(fs->max, &sz))
			{
				fopush(fo, fs->max);
				foop(fo, FOP_GT);
				f1 = fopop(fo);
				if (*(ft_int *)getfld(f1, &sz))
				{
					closefld(fs->max);
					fs->max = dupfld(fld);
				}
				closefld(f1);
			}
			else
			{
				closefld(fs->max);
				fs->max = dupfld(fld);
			}
		}
	}
	return t;
#endif /* OLD_STATS */
}

/******************************************************************/

RECID
TXmygettblrow(dbtbl, recid)
DBTBL	*dbtbl;
RECID	*recid;
{
	static const char	fn[] = "TXmygettblrow";
	RECID	rc, *t;
	TBL	*tbl;

	tbl = dbtbl->tbl;
	t = NULL;
	if(dbtbl->ddic->optimizations[OPTIMIZE_MINIMAL_LOCKING])
	{
		t = gettblrow(tbl, recid);
	}
	else
	{
		if(TXlocktable(dbtbl, R_LCK) == 0)
		{
#ifdef ASSERT_DEL_BLOCK
			if(validrow(tbl, recid))
				t = gettblrow(tbl, recid);
			else
			{
				texispusherror(dbtbl->ddic,
					       MAKEERROR(MOD_TUP,TUP_READ));
				t = NULL;
			}
#else
			t = gettblrow(tbl, recid);
#endif
			TXunlocktable(dbtbl, R_LCK);
		}
	}
	if (t)
	{
		rc = *t;
		if (TXApp && TXApp->traceRowFieldsTables &&
		    TXApp->traceRowFieldsFields)
			TXdbtblTraceRowFieldsMsg(fn, dbtbl, rc,
						 TXApp->traceRowFieldsTables,
						 TXApp->traceRowFieldsFields);
	}
	else
		TXsetrecid(&rc, (EPI_OFF_T)-1);
	return rc;
}

/* ------------------------------------------------------------------------ */

static void
TXtupReportDbidxRead(const char *fn, DBTBL *tup, BTLOC btloc)
{
	putmsg(MINFO, fn,
	       "Table `%s' recid 0x%wx read from index `%s'",
	       tup->lname, (EPI_HUGEINT)TXgetoff(&btloc),
	       TXdbidxGetName(&tup->index));
}

/* ------------------------------------------------------------------------ */

static void
TXtupReportTableRead(const char *fn, DBTBL *tup, BTLOC btloc)
{
	putmsg(MINFO, fn,
	       "Table `%s' recid 0x%wx read",
	       tup->lname, (EPI_HUGEINT)TXgetoff(&btloc));
}

/******************************************************************/

static DBTBL *tup_read_frombtree ARGS((DBTBL *, FLDOP *, int, int *,
				       TXCOUNTINFO *countInfo));

static DBTBL *
tup_read_frombtree(t, fo, toskip, skipped, countInfo)
DBTBL *t;	/* The table to read from */
FLDOP *fo;	/* A fldmath stack to evaluate predicates on */
int toskip;  /* Which direction do we want to retrieve records in */
int *skipped;	/* How many records were skipped */
TXCOUNTINFO	*countInfo;	/* (in/out, opt.) row count stats */
{
	size_t i;
	int doneskip = 0;
	int trc;
	PRED *p = t->pred;
	BTLOC btloc;
	DBTBL *rc = NULL;

	do
	{
		i = BT_MAXPGSZ;
		if(TXlocktable(t, R_LCK) == 0)
		{
			btloc = dbidxgetnext(&t->index, &i, tempbuf, NULL);
			TXunlocktable(t, R_LCK);
		}
		else
		{
			break;
		}
		if (!TXrecidvalid(&btloc))
		{
			break;
		}

		if (TXverbosity >= 3)
			TXtupReportDbidxRead(__FUNCTION__, t, btloc);

		t->recid = btloc;
		buftofld(tempbuf, t->tbl, i);
		if ((trc = tup_match(t, p, fo)) > 0)
		{				/* predicate matched */
			/* We want this one */
			rc = dostats(t, fo);
			if (countInfo != TXCOUNTINFOPN)
			{
				countInfo->rowsMatchedMin++;
				countInfo->rowsReturnedMin++;
			}
			if(toskip-- > 0)
			{
				++doneskip;
				continue;
			}
			if(skipped)
				*skipped = doneskip;
			return rc;
		}
		else if (countInfo != TXCOUNTINFOPN)
		{
			/* Pred did not match, so one less potential hit.
			 * (`rowsMatchedMax' is probably -1 eg. if bubble?)
			 */
			if (countInfo->rowsMatchedMax > 0)
				countInfo->rowsMatchedMax--;
			if (countInfo->rowsReturnedMax > 0)
				countInfo->rowsReturnedMax--;
		}
		if (trc < 0 || tup_match(t, t->ipred, fo) == 0)
		{
			/* Matched the end condition */
			break;
		}
	} while(1);

	/* At EOF */
	if (countInfo != TXCOUNTINFOPN)
	{
		/* No more rows, so if we did not know the max rows,
		 * (eg. post-proc or bubble == 1), we know now,
		 * since we counted them in `rowsMatchedMin':
		 */
		if (TX_ISVALIDCOUNT(countInfo->rowsMatchedMin) &&
		    !TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
			countInfo->rowsMatchedMax = countInfo->rowsMatchedMin;
		if (TX_ISVALIDCOUNT(countInfo->rowsReturnedMin) &&
		    !TX_ISVALIDCOUNT(countInfo->rowsReturnedMax))
			countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;
	}
	if(skipped)
		*skipped = doneskip;
	return (DBTBL *)NULL;
}

/******************************************************************/

static DBTBL *tup_read_fromoldmmindex ARGS((DBTBL *, FLDOP *, int, int *));

static DBTBL *
tup_read_fromoldmmindex(t, fo, toskip, skipped)
DBTBL *t;	/* The table to read from */
FLDOP *fo;	/* A fldmath stack to evaluate predicates on */
int toskip;  /* Which direction do we want to retrieve records in */
int *skipped;	/* How many records were skipped */
{
	size_t i;
	int doneskip = 0;
	BTLOC btloc;
	TTL *ttl;
	FLD *fword, *fcount;

	do{
		i = sizeof(tempbuf);
		btloc = dbidxgetnext(&t->index, &i, tempbuf, NULL);
		if (TXrecidvalid(&btloc) && TXverbosity >= 3)
			TXtupReportDbidxRead(__FUNCTION__, t, btloc);
	} while(TXrecidvalid(&btloc) && !strcmp((char *)tempbuf, LASTTOKEN) &&
		(toskip-- > 0) && ++doneskip);
	if(skipped)
		*skipped = doneskip;
	if (!TXrecidvalid(&btloc))
		return NULL;
	fword = nametofld(t->tbl, "Word");
	fcount = nametofld(t->tbl, "Count");
	ttl = getdbfttl(t->dbi->mm->bdbf, TXgetoff(&btloc));
	i = countttl(ttl);
	ttl = closettl(ttl);
	putfld(fword, tempbuf, strlen((char *)tempbuf));
	putfld(fcount, &i, 1);
	gettblrow(t->tbl, puttblrow(t->tbl, NULL));
	return dostats(t, fo);
}

/******************************************************************/

static DBTBL *tup_read_fromnewmmindex ARGS((DBTBL *, FLDOP *, int, int *,
					    TXCOUNTINFO *countInfo));

static DBTBL *
tup_read_fromnewmmindex(t, fo, toskip, skipped, countInfo)
DBTBL *t;	/* The table to read from */
FLDOP *fo;	/* A fldmath stack to evaluate predicates on */
int toskip;  /* Which direction do we want to retrieve records in */
int *skipped;	/* How many records were skipped */
TXCOUNTINFO	*countInfo;	/* (in/out, opt.) row count stats */
{
	size_t i, wdLen;
	int doneskip = 0;
	BTLOC btloc;
	FLD     *fword, *fcount, *rowCountFld, *occurrenceCountFld;
	ft_int64	occurrenceCount, rowCount;

	do
	{
		i = sizeof(tempbuf);
		btloc = dbidxgetnext(&t->index, &i, tempbuf, NULL);
		if (TXrecidvalid(&btloc) && TXverbosity >= 3)
			TXtupReportDbidxRead(__FUNCTION__, t, btloc);
		if (TXrecidvalid2(&btloc) &&
		    countInfo != TXCOUNTINFOPN)
		{
		    /* if `rowsMatchedMax' is set already, then we
		     * probably have all the rows, ie. a RAM B-tree
		     * result set from "... where Word >= 'something'":
		     * `rowsMatchedMin' is already the full count,
		     * do not increment it:
		     */
			if (!TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
				countInfo->rowsMatchedMin++;
			if (!TX_ISVALIDCOUNT(countInfo->rowsReturnedMax))
				countInfo->rowsReturnedMin++;
		}
	} while(TXrecidvalid2(&btloc) && (toskip-- > 0) && ++doneskip);
	if(skipped)
		*skipped = doneskip;
	if (!TXrecidvalid2(&btloc))		/* at EOF */
	{
		if (countInfo != TXCOUNTINFOPN)
		{
			/* No more rows, so we now know the max rows,
			 * since we counted the min rows along the way:
			 */
			if (TX_ISVALIDCOUNT(countInfo->rowsMatchedMin) &&
			    !TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
				countInfo->rowsMatchedMax =
					countInfo->rowsMatchedMin;
			if (TX_ISVALIDCOUNT(countInfo->rowsReturnedMin) &&
			    !TX_ISVALIDCOUNT(countInfo->rowsReturnedMax))
				countInfo->rowsReturnedMax =
					countInfo->rowsReturnedMin;
		}
		return(NULL);
	}
	fword = nametofld(t->tbl, "Word");
	fcount = nametofld(t->tbl, "Count");
	rowCountFld = nametofld(t->tbl, "RowCount");
	occurrenceCountFld = nametofld(t->tbl, "OccurrenceCount");
	wdLen = strlen((char *)tempbuf);
	/* Recover occurrenceCount from tempbuf; was appended in
	 * ixbtmmindex():
	 */
	if (wdLen + 1 < i)			/* occurrenceCount appended */
		occurrenceCount = TXstrtoi64((char *)tempbuf + wdLen + 1,
					     CHARPN, CHARPPN, 0, NULL);
	else					/* was lost/no room */
		occurrenceCount = 0;
	if (fword && (fword->type & DDTYPEBITS) == FTN_CHAR)
		putfld(fword, tempbuf, wdLen);
	rowCount = (ft_int64)TXgetoff(&btloc);
	if (fcount && (fcount->type & DDTYPEBITS) == FTN_INT64)
		putfld(fcount, &rowCount, 1);
	if (rowCountFld && (rowCountFld->type&DDTYPEBITS) ==FTN_INT64)
		putfld(rowCountFld, &rowCount, 1);
	if (occurrenceCountFld &&		/* only for INDEX_FULL */
	    (occurrenceCountFld->type & DDTYPEBITS) == FTN_INT64)
		putfld(occurrenceCountFld, &occurrenceCount, 1);
	gettblrow(t->tbl, puttblrow(t->tbl, NULL));
	return(dostats(t, fo));
}

/******************************************************************/

static DBTBL *tup_read_indexed ARGS((DBTBL *, FLDOP *, int, int *,
				     TXCOUNTINFO *countInfo));

static DBTBL *
tup_read_indexed(t, fo, toskip, skipped, countInfo)
DBTBL *t;	/* The table to read from */
FLDOP *fo;	/* A fldmath stack to evaluate predicates on */
int toskip;  /* Which direction do we want to retrieve records in */
int *skipped;	/* How many records were skipped */
TXCOUNTINFO	*countInfo;	/* (in/out, opt.) row count stats */
{
	size_t i;
	int doneskip = 0;
	PRED *p = t->pred;
	BTLOC btloc;
	DBTBL *rc;

	/* If TX_ISVALIDCOUNT(countInfo->rowsMatchedMax), it is expected to be
	 * the full index count (if known, eg. bubble == 0 or Metamorph);
	 * rowsMatchedMin may be similar (eg. indguar == 1) or may be
	 * a running count incremented here (eg. indguar == 0).
	 */

	do
	{
		i = sizeof(tempbuf);
		btloc = dbidxgetnext(&t->index, &i, tempbuf, NULL);
		if (TXrecidvalid(&btloc) && TXverbosity >= 3)
			TXtupReportDbidxRead(__FUNCTION__, t, btloc);
		/* Since this loop asserts t->indguar == 1,
		 * there is no post-process, and we should not
		 * have to update countInfo->rowsMatchMin/Max.
		 */
	} while(TXrecidvalid(&btloc) &&
		t->indguar == 1 && t->needstats == 0 &&
		(toskip-- > 0) && ++doneskip);
	if(skipped)
		*skipped = doneskip;
	for(;TXrecidvalid(&btloc); i = sizeof(tempbuf),
		    btloc = dbidxgetnext(&t->index, &i, tempbuf, NULL),
		    (TXrecidvalid(&btloc) && TXverbosity >= 3 ?
		     TXtupReportDbidxRead(__FUNCTION__, t, btloc) : (void)0))
	{
		int trc;

		if(t->indguar == 0 || t->needstats != 2 || !t->rname)
		{
			if(t->index.indexdataonly)
			{
				t->recid = btloc;
				buftofld(tempbuf, t->index.indexdbtbl->tbl, i);
				tup_copy3(t, t->index.indexdbtbl, fo);
			}
			else
			{
				t->recid = TXmygettblrow(t, &btloc);
			}
		}
		else
		{
			t->recid = btloc;
			if((t->index.type != DBIDX_CACHE) &&
			   (TXsetcountstat(t->nfldstat, t->index.nrecs)>0))
			{ /* Set the count in stats, return end of results */
				/* wtf update `countInfo'? */
				return NULL;
			}
		}
		TXrowsread++;
		if(!TXrecidvalid(&t->recid))
		{
			/* Row may have been deleted after we got it from
			 * the index.
			 */
			if (countInfo != TXCOUNTINFOPN)
			{
				if (countInfo->rowsMatchedMin > 0)
					countInfo->rowsMatchedMin--;
				if (countInfo->rowsMatchedMax > 0)
					countInfo->rowsMatchedMax--;
				if (countInfo->rowsReturnedMin > 0)
					countInfo->rowsReturnedMin--;
				if (countInfo->rowsReturnedMax > 0)
					countInfo->rowsReturnedMax--;
			}
			continue;
		}

#ifndef NO_NEW_RANK
		if(t->index.nrank)
			t->rank = TX_RANK_INTERNAL_TO_USER(TXApp, *(EPI_OFF_T *)tempbuf/t->index.nrank);
		else if (t->rankindex.nrank && t->rankindex.btree)
			{
				BTLOC btloc1;

				btloc1 = btsearch(t->rankindex.
				 btree, sizeof(BTLOC), &btloc);
				t->rank = TX_RANK_INTERNAL_TO_USER(TXApp, TXgetoff(&btloc1)/t->rankindex.nrank);
			}
#endif /* !NO_NEW_RANK */
		if (t->indguar)			/* no post-proc or predicate*/
		{
#ifdef NO_NEW_RANK
#ifndef NO_RANK_FIELD
			if(p->op == FOP_RELEV || p->op == FOP_PROXIM)
				t->rank = TX_RANK_INTERNAL_TO_USER(TXApp, *(EPI_OFF_T *)tempbuf);
			else
				t->rank=0;
#endif
#endif /* NO_NEW_RANK */
			rc = dostats(t, fo);
			/* `indguar' set: no `countInfo' update needed;
			 * unless overall count is not known, eg. bubble == 1
			 * search `where Num >= 3'.  WTF better method
			 * to check that count is unknown that looking
			 * if `rowsMatchedMax == -1': TXCOUNTINFO should
			 * ideally be "output-only":
			 */
			if (countInfo != TXCOUNTINFOPN &&
			    !TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
			{
				countInfo->rowsMatchedMin++;
				countInfo->rowsReturnedMin++;
			}
			if(toskip-- > 0)
				continue;
			return rc;
		}
		if ((trc = tup_match(t, p, fo)) > 0)
		{				/* predicate matched */
					/* We want this one */
			t->recid = btloc;
			rc = dostats(t, fo);
			if (countInfo != TXCOUNTINFOPN)
			{
				countInfo->rowsMatchedMin++;
				countInfo->rowsReturnedMin++;
			}
			if(toskip-- > 0)
				continue;
			return rc;
		}
		else if (countInfo != TXCOUNTINFOPN)
		{
			/* Pred did not match, so one less potential hit: */
			if (countInfo->rowsMatchedMax > 0)
				countInfo->rowsMatchedMax--;
			if (countInfo->rowsReturnedMax > 0)
				countInfo->rowsReturnedMax--;
		}
		if (trc < 0 || tup_match(t, t->ipred, fo) == 0)
				/* Matched the end condition */
			break;
		/* Didn't match anything, so try again */
	}

	/* At EOF */
	if (countInfo != TXCOUNTINFOPN)
	{
		/* No more rows, so if we did not know the max rows,
		 * (eg. post-proc or bubble == 1), we know now,
		 * since we counted them in `rowsMatchedMin':
		 */
		if (TX_ISVALIDCOUNT(countInfo->rowsMatchedMin) &&
		    !TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
			countInfo->rowsMatchedMax = countInfo->rowsMatchedMin;
		if (TX_ISVALIDCOUNT(countInfo->rowsReturnedMin) &&
		    !TX_ISVALIDCOUNT(countInfo->rowsReturnedMax))
			countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;
	}
	return (DBTBL *)NULL;
}

/****************************************************************************/
/*
 *	Read a Tuple matching a predicate from a table
 *
 *      Needs to be improved to take care of indexes etc...
 */

DBTBL *
tup_read(t, fo, direction, offset, skipped, countInfo)
DBTBL *t;	/* The table to read from */
FLDOP *fo;	/* A fldmath stack to evaluate predicates on */
int direction;  /* Which direction do we want to retrieve records in */
int offset;	/* Offset of record */
int *skipped;	/* How many records were skipped */
TXCOUNTINFO	*countInfo;	/* (in/out, opt.) row count stats */
{
	int toskip = 0, doneskip = 0;
	DBTBL	*ret;
	PRED *p = t->pred;
        DBTBL   *savtbl;

        savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = t;			/* for btreelog debug */

#ifndef NO_RANK_FIELD
	t->rank=0;
#endif
	if(skipped)
		*skipped = 0;
	switch(direction)
	{
		case SQL_FETCH_RELATIVE:
			if(offset == 0)
			{
				ret = t;
				goto done;
			}
			if(offset > 0)	toskip = offset - 1;
			else		goto mt;
	}

	if (countInfo != TXCOUNTINFOPN)
	{
		if (!TX_ISVALIDCOUNT(countInfo->rowsMatchedMin))
			countInfo->rowsMatchedMin = 0;
		if (!TX_ISVALIDCOUNT(countInfo->rowsReturnedMin))
			countInfo->rowsReturnedMin = 0;
	}

	if ((t->index.btree == NULL) &&
	    (p  == (PRED *)NULL || fo == (FLDOP *)NULL))
	/*
	 *	There is no predicate to match, or no match stack
	 *	to use for evaluation.
	*/
	{
		PRECID	tmp;

		do
		{
			tmp = getdbtblrow(t);
			if (TXrecidvalid(tmp) && TXverbosity >= 3)
				TXtupReportTableRead(__FUNCTION__, t, *tmp);
			if (TXrecidvalid(tmp) && countInfo != TXCOUNTINFOPN)
			{
				countInfo->rowsMatchedMin++;
				countInfo->rowsReturnedMin++;
			}
		} while (tmp && (toskip-- > 0) && ++doneskip && dostats(t,fo));
		if (!tmp)
			TXsetrecid(&t->recid, (EPI_OFF_T)-1);
		else
			t->recid = *tmp;
		if(skipped)
			*skipped = doneskip;
		if (TXrecidvalid(&t->recid))
			ret = dostats(t, fo);
		else
			goto linearEofCounts;
		goto done;
	}
	if (t->index.btree == (BTREE *)NULL)
	/*
	 *	There is a predicate to match, but no index on it.
	*/
	{
		int trc;
		PRECID	xx;

#ifdef RECID_HACK
		if(p->op == FOP_GT && p->lt == NAME_OP &&
		   p->rt == FIELD_OP && !strcmp(p->left, "$recid"))
		{
			EPI_OFF_T want, amat;
			RECID wrecid;

			want = *(long *)getfld(p->right, NULL);
			amat = telldbf(t->tbl->df);
			if(amat <= want)
			{
				TXsetrecid(&wrecid, want);
				gettblrow(t->tbl, &wrecid);
			}
		}
#endif

		while (xx = getdbtblrow(t), TXrecidvalid(xx))
		{
			if (TXverbosity >= 3)
				TXtupReportTableRead(__FUNCTION__, t, *xx);
			t->recid = *xx;
			if ((trc = tup_match(t, p, fo)) > 0)
			{			/* predicate matched */
				if (countInfo != TXCOUNTINFOPN)
				{
					countInfo->rowsMatchedMin++;
					countInfo->rowsReturnedMin++;
				}

				if(toskip-- > 0)
				{
					++doneskip;
					dostats(t, fo);
					continue;
				}
				if(skipped)
					*skipped = doneskip;
				ret = dostats(t, fo);
				goto done;
			}
			else			/* predicate did not match */
			{
				/* KNG No need to reduce `rowsMatchedMax':
				 * it should be -1 anyway since no index
				 */
				if (trc < 0) break;
			}
		}
		/* No (more) rows found; EOF: */
		TXsetrecid(&t->recid, (EPI_OFF_T)-1);
		/* We can set max = min now that EOF reached; no more rows: */
	linearEofCounts:
		if (countInfo != TXCOUNTINFOPN)
		{
			if (!TX_ISVALIDCOUNT(countInfo->rowsMatchedMax))
				countInfo->rowsMatchedMax =
					countInfo->rowsMatchedMin;
			if (!TX_ISVALIDCOUNT(countInfo->rowsReturnedMax))
				countInfo->rowsReturnedMax =
					countInfo->rowsReturnedMin;
		}
	}
	else
	/*
	 *	There is a predicate to match, and an index on it.
	*/
	{
		switch(t->type)
		{
		case INDEX_BTREE:
			ret = tup_read_frombtree(t, fo, toskip, skipped,
						 countInfo);
			break;
		case INDEX_3DB:
			ret = tup_read_fromoldmmindex(t, fo, toskip, skipped);
			break;
		case INDEX_MM:
		case INDEX_FULL:
			ret = tup_read_fromnewmmindex(t, fo, toskip, skipped,
						      countInfo);
			break;
		default:
			ret = tup_read_indexed(t, fo, toskip, skipped,
						countInfo);
			break;
		}
		goto done;
	}
mt:
	ret = (DBTBL *)NULL;
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return(ret);
}

int
tup_copy(t, tup, fo)
DBTBL *t;	/* Destination table */
DBTBL *tup;	/* Source table */
FLDOP *fo;	/* Fldmath */
{
	TBL	*tin, *tout;
	FLD *fin, *fout;
	int i;

/* Copy the fields to the output table */

	tin = tup->tbl;
	tout= t->tbl;
	if(tout->bf)
	{
		if(TXlocktable(t, W_LCK) < 0)
			return -1;
	}
	for (i=0;
	    ((fin = getfldn(tin, i, NULL))!= NULL) &&
	    ((fout = getfldn(tout, i, NULL))!=NULL);
	     i++)
	{
		_fldcopy(fin, tin, fout, tout, fo);
	}
	t->rank = tup->rank;
	if(tout->bf)
	{
		TXunlocktable(t, W_LCK);
	}
	return 0;
}

/******************************************************************/

static int tup_copy2 ARGS((DBTBL *, DBTBL *, FLDOP *));

static int
tup_copy2(t, tup, fo)
DBTBL *t;	/* Destination table */
DBTBL *tup;	/* Source table */
FLDOP *fo;	/* Fldmath */
{
	TBL	*tin, *tout;
	FLD *fin, *fout;
	int i;

/* Copy the fields to the output table */

	tin = tup->tbl;
	tout= t->tbl;
	for (i=0;
	    ((fin = getfldn(tin, i, NULL))!= NULL) &&
	    ((fout = getfldn(tout, i, NULL))!=NULL);
	     i++)
	{
		if(fin->type == fout->type)
		{
			TXfreefldshadow(fout);
			/* KNG 20060215 keep things OO */
			setfldandsize(fout, fin->v, fin->size + 1, FLD_FORCE_NORMAL);
			TXsetshadownonalloc(fout);
		}
		else
			_fldcopy(fin, tin, fout, tout, fo);
	}
	t->rank = tup->rank;
	return 0;
}

/******************************************************************/
/*
	Yet another copy function.  This will copy those fields
	that exist in source to destination, and leave the
	remainder NULL.
*/

static int
tup_copy3(t, tup, fo)
DBTBL *t;	/* Destination table */
DBTBL *tup;	/* Source table */
FLDOP *fo;	/* Fldmath */
{
	TBL	*tin, *tout;
	FLD *fin, *fout;
	char *fldname;
	int i;
	TXstrlstCharConfig orgSep;
	size_t	n;

/* Copy the fields to the output table */

	tin = tup->tbl;
	tout= t->tbl;
	/* Can't handle NULLs yet
	for(i = 0;(fout = getfldn(tout, i, NULL)) != NULL; i++)
	{
		fout->v = NULL;
	}
	*/
	for (i=0;
	    ((fin = getfldn(tin, i, NULL))!= NULL) &&
	    ((fldname = getfldname(tin, i))!=NULL);
	     i++)
	{
		fout = nametofld(tout, fldname);
		if (!fout) continue;		/* dest does not have field */
		if (fin->type == fout->type)
		{
			TXfreefldshadow(fout);
			/* KNG 20060215 keep things OO */
			setfldandsize(fout, fin->v, fin->size + 1, FLD_FORCE_NORMAL);
			TXsetshadownonalloc(fout);
		}
		/* KNG 20090424 Bug 2598: if types do not match, use
		 * _fldcopy(); was just leaving `fout' unset/previous-value.
		 * Also set varcharToStrlstSep to TXVSSEP_CREATE, since we
		 * know any varchar data being converted to strlst here
		 * is a single value from the strlst-splitup at index insert.
		 * Ideally though, `fout' type should already have been
		 * changed to varchar (Bug 2397) and this conversion unneeded:
		 */
		else
		{
			orgSep = TXApp->charStrlstConfig;
			/* Avoid Bug 4162 comment #2 change
			 * (empty-string converted via TXVSSEP_CREATE
			 * becomes empty-strlst), and force
			 * empty-string to become one-empty-string
			 * strlst, because if the varchar came from a
			 * strlst, it was (probably?) a non-empty
			 * strlst originally:
			 */
			getfld(fin, &n);
			if (TXVSSEP_CREATE_EMPTY_STR_TO_EMPTY_STRLST(TXApp) &&
			    n == 0)		/* empty-str iff varchar */
			{
				TXApp->charStrlstConfig.toStrlst = TXc2s_defined_delimiter;
				TXApp->charStrlstConfig.delimiter = TxPrefStrlstDelims[0];
			}
			else
			{
				TXApp->charStrlstConfig.toStrlst = TXc2s_create_delimiter;
			}
			_fldcopy(fin, tin, fout, tout, fo);
			TXApp->charStrlstConfig = orgSep;
		}
	}
	t->rank = tup->rank;
	return 0;
}

/****************************************************************************/
/*
 *	Write a tuple to a table.
 *
 *	Returns offset of new row.
 *
 *	1999-06-09 - JMT - Added forcecopy flag.  This forces a copy as
		occasionally a record is written, and it is assumed that
		the data being written will hang around for a while, which
		is not always the case.  E.g. group by where it will read
		the next row before exiting.  In that case the data must
		be copied, or maybe a read needs to be forced afterward or
		something, however there are a bunch of other conditions
		currently forcing a copy due to various weirdnesses, old
		tables, as you don't want to mess with those fields, and
		blobs.
 */

RECID *
tup_write(t, tup, fo, forcecopy)
DBTBL *t;	/* Destination table */
DBTBL *tup;	/* Source table */
FLDOP *fo;	/* Fldmath */
int   forcecopy;
{
	static const char	fn[] = "tup_write";
#ifndef NO_PROP_RANK
	FLD *fld, *fld2;
#endif
	RECID *rc;
	int	locked = 0;

/* Copy the fields to the output table */

	if(t->ddic && t->ddic->optimizations[OPTIMIZE_COPY] == 0)
		forcecopy++;
	if((!forcecopy) &&
	   (t->tbl->tbltype != TEXIS_OLD_TABLE) &&
	   (t->tbl->bf == NULL))
		tup_copy2(t, tup, fo);
	else
		if(tup_copy(t, tup, fo) != 0)
			return NULL;
#ifndef NO_RANK_FIELD
	t->rank = tup->rank;
#endif
#ifndef NO_PROP_RANK
	fld  = dbnametofld(t, (char *)TXrankColumnName);
	fld2 = dbnametofld(t, (char *)TXrankColumnName);
	if(fld && !fld2)
		putfld(fld, &t->rank, 1);
	if(fld && !getfld(fld, NULL))
		putfld(fld, &t->rank, 1);
#endif
	rc = putdbtblrow(t, NULL);
	if(rc == NULL)
	{
		TBL	*tbl = t->tbl;
		FLD	*f;
		unsigned int j;

		locked = (TXlocktable(t, W_LCK) != -1);
		/* Proceed even if not locked, so we can yap only if needed: */
		for (j=0; j < tbl->n; ++j)
		{
			f = TXgetrfldn(tbl, j, NULL);
			if(!f)
				continue;
			if ((f->type & DDTYPEBITS) == FTN_INDIRECT)
			{
				void	*v;

				v = getfld(f, NULL);
				if(TXisindirect(v))
				{
					if (locked)
						unlink(v);
					else
						txpmbuf_putmsg(t->ddic->pmbuf,
							      MWARN, fn,
							      "Will not remove indirect file `%s' after table `%s' write failure: Cannot obtain lock%s",
							       v, t->lname, (TXsqlWasCancelled(t->ddic) ? ": SQL transaction cancelled" : ""));
				}
#ifdef DEBUG
				else
					DBGMSG(9, (999, "Delete", "strstr(%s, \"/.turn\") = (%lx) %s", v, strstr(v, "/.turl"), strstr(v, "/.turl")));
#endif
			}
			if ((f->type & DDTYPEBITS) == FTN_BLOBI)
			{
				ft_blobi	*v;

				v = getfld(f, NULL);
				/* Bug 4026: -1 invalid, do not freedbf(-1)
				 * as that will delete current block
				 * which is incorrect:
				 */
				/* WTF when Bug 4037 implemented, maybe
				 * also do not delete offset-0 blobs?
				 */
				if (v && v->off != (EPI_OFF_T)(-1))
				{
					if (locked)
						freedbf(((DBF *)v->dbf), v->off);
					else
						txpmbuf_putmsg(t->ddic->pmbuf,
							      MWARN, fn,
							      "Will not delete blob at offset 0x%wx in `%s' after table `%s' write failure: Cannot obtain lock%s",
						     (EPI_HUGEINT)v->off,
						     getdbffn((DBF *)v->dbf),
						     t->lname,
	   (TXsqlWasCancelled(t->ddic) ? ": SQL transaction cancelled" : ""));
				}
			}
		}
		if (locked) TXunlocktable(t, W_LCK);
	}
	return rc;
}

int
tup_delete(tup, tb)
DBTBL *tup;
DBTBL *tb;
{
	(void)tup;
/*
	Need to delete this record from the indexes
*/
	if(TXlockandload(tb, PM_DELETE, NULL) == -1)
		return -1;
	if (TXlocktable(tb, W_LCK) == -1)
	{
		if ((tb->type == 'T' || tb->type == 'S') && tb->rname)
			TXunlockindex(tb, INDEX_WRITE, NULL);
		return -1;
	}
	if(!validrow(tb->tbl, &tb->recid))	/* Already deleted */
	{
		flushindexes(tb);
		TXunlocktable(tb, W_LCK);
		if ((tb->type == 'T' || tb->type == 'S') && tb->rname)
			TXunlockindex(tb, INDEX_WRITE, NULL);
		return -1;
	}

        TXdelfromindices(tb);

	if (tb->type != 'B')
	{
		TBL	*tbl = tb->tbl;
		FLD	*f;
		unsigned int j;

		for (j=0; j < tbl->n; ++j)
		{
			f = TXgetrfldn(tbl, j, NULL);
			if(!f)
				continue;
			if ((f->type & DDTYPEBITS) == FTN_INDIRECT)
			{
				void	*v;

				v = getfld(f, NULL);
				if(TXisindirect(v))
				{
					unlink(v);
				}
#ifdef DEBUG
				else
					DBGMSG(9, (999, "Delete", "strstr(%s, \"/.turn\") = (%lx) %s", v, strstr(v, "/.turl"), strstr(v, "/.turl")));
#endif
			}
			if ((f->type & DDTYPEBITS) == FTN_BLOBI)
			{
				ft_blobi	*v;

				v = getfld(f, NULL);
				/* Bug 4026: -1 invalid, do not freedbf(-1)
				 * as that will delete current block
				 * which is incorrect:
				 */
				/* WTF when Bug 4037 implemented, maybe
				 * also do not delete offset-0 blobs?
				 */
				if (v && v->off != (EPI_OFF_T)(-1))
					freedbf(((DBF *)v->dbf), v->off);
			}
		}
		freedbf(tb->tbl->df, TXgetoff(&tb->recid));
		flushindexes(tb);
		TXunlocktable(tb, W_LCK);
		if ((tb->type == 'T' || tb->type == 'S') && tb->rname)
			TXunlockindex(tb, INDEX_WRITE, NULL);
	}
#ifdef BTREE_TABLE_DELETES
	else
	{
		BTLOC btloc;
		int tbs;

		TXsetrecid(&btloc, -1);
		fldtobuf(tb->tbl);
		tbs = btsetsearch(tb->index.btree, BT_SEARCH_FIND);
		btdelete(tb->index.btree, &tb->recid, tb->tbl->orecsz, tb->tbl->orec);
		btsetsearch(tb->index.btree, tbs);
	}
#endif
	return 0;
}
