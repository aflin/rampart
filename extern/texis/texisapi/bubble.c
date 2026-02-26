/* -=- kai-mode: John -=- */
#ifndef NO_BUBBLE_INDEX
#include "txcoreconfig.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(MSDOS)
#include <unistd.h>
#include <pwd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"
#include "sregex.h"

static CONST char	TXcannotUseIndexInBubbleUpModeFmt[] =
"Cannot use index `%s' in bubble-up mode for current query: has split values";

#ifndef NO_ANALYZE
#define BUBBLE_GPRED
#endif
/******************************************************************/

static int getcomp ARGS((DBTBL *, PRED *, PRED *, FLDOP *));

static int
getcomp(tb, l, r, fo)
DBTBL	*tb;
PRED	*l;
PRED	*r;
FLDOP	*fo;
{
	static CONST char	fn[] = "getcomp";
	int	rev, res;
	FLD	*lfld, *rfld;
	PRED	*t;
	DBIDX	*dbidx;
	char	*dname;
	char	*itype = NULL, **iname = NULL;
	char	**sysindexFieldsVals = CHARPPN;
	char	**sysindexParamsVals = CHARPPN;
	int	numindex = 0;
	int	j = 0, ret;

	if (!(l->lt == NAME_OP && l->rt == FIELD_OP &&
	    r->lt == NAME_OP && r->rt == FIELD_OP &&
	    !strcmp(l->left, r->left)))
		goto err;

	if(l->op > r->op) /* Order the predicates */
	{
		t = l;
		l = r;
		r = t;
	}
	switch (l->op) /* Should mirror switch lower down */
	{
		case FOP_EQ :
		case FOP_GT :
		case FOP_GTE :
		case FOP_LT :
		case FOP_LTE :
			break;
		default :
			goto err;
	}
	dname = dbnametoname(tb, l->left, FTNPN, INTPN);
	if (!dname)
		goto err;
	lfld = dbnametofld(tb, dname);
	numindex=ddgetindex(tb->ddic, tb->rname, dname, &itype,
			    &iname, &sysindexFieldsVals, &sysindexParamsVals);
	rev = 0;
	if (numindex <= 0)
	{
		char	*rname;

		iname = TXfreeStrList(iname, numindex);
		sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals,
						   numindex);
		sysindexParamsVals = TXfreeStrList(sysindexParamsVals,
						   numindex);
		itype = TXfree(itype);
		/* Now check for reversed indexes. */
		rname = TXmalloc(TXPMBUFPN, fn, strlen(dname)+2);
		if(!rname)
			goto err;
		strcpy(rname, dname);
		strcat(rname, "-");
		numindex=ddgetindex(tb->ddic, tb->rname, rname,
				    &itype, &iname, &sysindexFieldsVals,
				    &sysindexParamsVals);
		rname = TXfree(rname);
		if (numindex <= 0)
		{
			/* Now check for reversed indexes. */
			goto err;
		}
		rev = 1;
#ifdef DEBUG
		DBGMSG(9, (999,NULL,"We found ourselves a reversed index"));
#endif
	}
	rfld = newfld(lfld);
	lfld = newfld(lfld);
	dbidx = NULL;
	_fldcopy((FLD *)l->right,NULL,lfld,NULL, fo);
	_fldcopy((FLD *)r->right,NULL,rfld,NULL, fo);
#ifdef DEBUG
	DBGMSG(9,(999, NULL, "l->op = %d, r->op = %d", l->op, r->op));
	DBGMSG(9,(999, NULL, "l = %d, r = %d", *(long *)lfld->v, *(long *)rfld->v));
#endif
	switch (l->op) /* Should mirror switch higher up */
	{
		case FOP_EQ : /* Don't really need to check r? */
			rfld = closefld(rfld);
			for(j=0; j < numindex; j++)
				if (itype[j] == 'B')
				{
					dbidx=opendbidx(itype[j], iname[j],
							sysindexFieldsVals[j],
							sysindexParamsVals[j],
							tb, 1);
					if(dbidx)
					{
						res = setdbidx(dbidx, lfld, dname, lfld, 1, 1);
						TXdbidxUnlock(dbidx);	/* Bug 2542 */
						if (!res)	/* failed */
							dbidx = closedbidx(dbidx);
						else if(dbidx != &tb->index)
							tb->index = *dbidx;
					}
					if(dbidx)
					{
#if defined (BUBBLE_GPRED) && defined(NEVER) /* What about rfld? */
						tb->indguar = 1;
#endif
						break;
					}
				}
			lfld = closefld(lfld);
			ret = (dbidx != NULL ? 1 : -1);
			break;
		case FOP_GT :
		case FOP_GTE :
			rfld = closefld(rfld);
			for(j=0; j < numindex; j++)
				if (itype[j] == 'B')
				{
				dbidx=opendbidx(itype[j], iname[j],
						sysindexFieldsVals[j],
						sysindexParamsVals[j], tb, 1);
				if(dbidx)
				{
					if(!rev)
						res = setdbidx(dbidx, lfld, dname, NULL, (l->op == FLDMATH_GTE), 0);
					else
						res = setdbidx(dbidx, NULL, dname, lfld, 0, (l->op == FLDMATH_GTE));
					TXdbidxUnlock(dbidx);	/* Bug 2542 */
					if (!res)	/* failed */
						dbidx = closedbidx(dbidx);
					else if(dbidx != &tb->index)
						tb->index = *dbidx;
				}
					if(dbidx)
#ifdef BUBBLE_GPRED
					{
						tb->indguar=1;
						break;
					}
#else
						break;
#endif
				}
			lfld = closefld(lfld);
			ret = (dbidx != NULL ? 1 : -1);
			break;
		case FOP_LT :
		case FOP_LTE :
			if (r->op != FLDMATH_GT && r->op != FLDMATH_GTE)
				rfld = closefld(rfld);
#ifdef DEBUG
			else
				DBGMSG(9,(999, NULL, "We can do something with %s", l->left));
#endif
			for(j=0; j < numindex; j++)
				if (itype[j] == 'B')
				{
					dbidx=opendbidx(itype[j], iname[j],
							sysindexFieldsVals[j],
							sysindexParamsVals[j],
							tb, 1);
					if(dbidx)
					{
						if(!rev)
							res = setdbidx(dbidx, rfld, dname, lfld, (r->op == FLDMATH_GTE), (l->op == FLDMATH_LTE));
						else
							res = setdbidx(dbidx, lfld, dname, rfld, (l->op == FLDMATH_LTE), (r->op == FLDMATH_GTE));
						TXdbidxUnlock(dbidx);	/* Bug 2542 */
						if (!res)	/* failed */
							dbidx = closedbidx(dbidx);
						else if(dbidx != &tb->index)
							tb->index = *dbidx;
					}
					if(dbidx)
#ifdef BUBBLE_GPRED
					{
						tb->indguar=1;
						break;
					}
#else
						break;
#endif
				}
			rfld = closefld(rfld);
			lfld = closefld(lfld);
			ret = (dbidx != NULL ? 1 : -1);
			break;
		default :
			rfld = closefld(rfld);
			lfld = closefld(lfld);
			goto err;
	}
        goto done;

err:
        ret = -1;
done:
	itype = TXfree(itype);
	iname = TXfreeStrList(iname, numindex);
	sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals, numindex);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numindex);
        return(ret);
}

/******************************************************************/

int
TXtrybubble(tb, p, order, fo, tbspec)
DBTBL	*tb;
PRED	*p;
PROJ	*order;
FLDOP	*fo;
TBSPEC  *tbspec;
{
	static CONST char Fn[] = "TXtrybubble";
	char	*dname, *fname = CHARPN;
	char	*rname = NULL;
	int	j = 0, lookright = 0, ret, dbidxFlags, doSubset;
	size_t	paramNumItems, infldLen;
	int	rev, res, returnAllRows = 0, paramIsEmptySet;
	FLD	*fld, *infld=NULL;
	FTN	indexFldType;
	int	indexFldN;
	/* dupIndexFld: a duplicate field of same type as index field: */
	FLD	*dupIndexFld = NULL;
	/* promotedParamFld: a copy of parameter/literal `infld',
	 * promoted as needed:
	 */
	FLD	*promotedParamFld = NULL;
	DBIDX	*dbidx=NULL;
	int	shortcircuit = 0;
	INDEXINFO indexinfo;

#ifdef NEVER
	if(order)
		goto err;
#endif /* NEVER */
	if(!p)
		goto err;
	if(!tb->rname)
		goto err;
	if(tb->ddic->no_bubble)
		goto err;
	if (p->op == FLDMATH_AND)
	{
		ret = getcomp(tb, p->left, p->right, fo);
		goto done;
	}
	if (p->op == FLDMATH_OR)
		goto err;
	if(p->op == NOT_OP)
		goto err;
	DBGMSG(1, (999, NULL, "In bubble"));
	infld = TXpredGetColumnAndField(p, &lookright, &fname);
	if(!infld)
		goto err;
	dname = dbnametoname(tb, fname, FTNPN, INTPN);
	if (!dname)
		goto err;
	fld = dbnametofld(tb, dname);
	resetindexinfo(&indexinfo);
	indexinfo.tbspec = tbspec;
	if(p->iname && p->op == FLDMATH_EQ)
	{
		indexinfo.paths = p->iname;
		indexinfo.itypes = p->itype;
		rev = p->rev;
		shortcircuit = 1;
		indexinfo.numIndexes = p->indexcnt;
		goto letsgo;
	}
	indexinfo.numIndexes = ddgetindex(tb->ddic, tb->rname, dname, &indexinfo.itypes, &indexinfo.paths, &indexinfo.fields, &indexinfo.sysindexParamsVals);
	rev = 0;
	if (indexinfo.numIndexes <= 0)
	{
		if(!shortcircuit)
			closeindexinfo(&indexinfo);
		/* Now check for reversed indexes. */
		rname = TXmalloc(TXPMBUFPN, Fn, strlen(dname)+2);
		if(!rname)
			goto err;
		strcpy(rname, dname);
		strcat(rname, "-");
		indexinfo.numIndexes = ddgetindex(tb->ddic, tb->rname, rname, &indexinfo.itypes, &indexinfo.paths, &indexinfo.fields, &indexinfo.sysindexParamsVals);
		if (indexinfo.numIndexes <= 0)
		{
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			/* Make LIKE3 do LIKE !!! WTF */
			if(p->op == FLDMATH_NMM)
				p->op = FLDMATH_MM;
			goto err;
		}
		rev = 1;
#ifdef DEBUG
		DBGMSG(9,(999,NULL,"We found ourselves a reversed index"));
#endif
	}
	if(!lookright)
		rev = 1 - rev;
#ifdef NEVER
	ix = NULL;
#endif /* NEVER */
letsgo:

	/* Maybe promote `infld' for Bug 3677: */
	if (p->op != FLDMATH_NEQ &&		/* cannot use index anyway */
	    !TXfixupMultiItemRelopSingleItem(fld, dname, p->op, &infld,
					     &promotedParamFld, fo))
		goto err;

	switch (p->op)
	{
		case FOP_IN:
		case FOP_IS_SUBSET:
		/* FOP_INTERSECT returns set not boolean; cannot use index */
		/* FOP_INTERSECT_IS_EMPTY is negation; cannot use index */
		case FOP_INTERSECT_IS_NOT_EMPTY:

			/* Bug 3677 et al.:  We can bubble-up
			 * IN/SUBSET/INTERSECT_IS_NOT_EMPTY iff:
			 * 1)  both param and column are single-item, or
			 * 2)  LHS is single-item param, or
			 * 3)  RHS is single-item param and op is intersect
			 * 4)  LHS is empty-set param and op is subset
			 * (see TXbtreeScoreIndex() comments).  2 and 3
			 * may need de-dup in getdbidx() (if multi-item col),
			 * and must use splitstrlst index.  4 must use
			 * indexvalues=all index (Bug 4143 no empty-set rows,
			 * and cannot be de-duped in bubble-up anyway):
			 */
			if (!TXApp->deDupMultiItemResults) /* optimization unused */
				goto err;
			dbidxFlags = 0x3;	/* keep locks; splitval-ok */
			doSubset = (p->op == FLDMATH_IS_SUBSET ||
				  (p->op == FLDMATH_IN && TXApp->inModeIsSubset));
			paramNumItems = TXfldNumItems(infld);
			/* Set `paramIsEmptySet': */
			paramIsEmptySet = 0;
			if (paramNumItems == 0)
				paramIsEmptySet = 1;
			else switch (infld->type & DDTYPEBITS)
			{
			case FTN_CHAR:
			case FTN_BYTE:
				/* Bug 3677 #12: Empty string is empty set;
				 * Bug 3677 #13: not for FOP_IN:
				 */
				if (p->op == FLDMATH_IN) break;
				getfld(infld, &infldLen);
				paramIsEmptySet = (infldLen == 0);
				break;
			}
			/* Check for conditions 1-4 above: */
			if (!lookright && doSubset && paramIsEmptySet)
			{			/* #4 above is true */
				returnAllRows = 1;
				/* Cannot de-dup whole index + need empties:*/
				dbidxFlags &= ~0x6;
			}
			else if (paramNumItems == 1 &&
				 !TXfldIsMultipleItemType(fld, NULL, NULL))
			{			/* #1 above is true */
				;
			}
			else if (!lookright &&
				 paramNumItems == 1)
				/* #2 above is true */
				dbidxFlags |= 0x4;  /* split-val required */
			else if (lookright &&
				 paramNumItems == 1 &&
				 (p->op == FLDMATH_INTERSECT_IS_NOT_EMPTY ||
				  (p->op == FLDMATH_IN && !TXApp->inModeIsSubset)))
				/* #3 above is true */
				dbidxFlags |= 0x4;  /* split-val required */
			else
				goto err;	/* cannot bubble-up */

			for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright);
			     j >= 0;
			     j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
			{
				if (shortcircuit && tb->index.keepcached)
				{
					dbidx = &tb->index;
				}
				else
				dbidx = opendbidx(indexinfo.itypes[j],
						  indexinfo.paths[j],
						  indexinfo.fields[j],
				      indexinfo.sysindexParamsVals[j], tb,
						  dbidxFlags);
				if (!dbidx) continue;

				/* Index field type may vary depending on
				 * index, even though all are same column
				 * (e.g. splitstrlst is varchar,
				 * `indexvalues=all' is strlst).  See
				 * comment below on why dup is needed even
				 * if index and table field are same type:
				 */
				dupIndexFld = closefld(dupIndexFld);
				if (TXbtreeHasSplitValues(indexinfo.fields[j],
					 tb, dbidx->btree->params.indexValues))
				{
					TXfldIsMultipleItemType(fld, NULL,
								&indexFldType);
					indexFldN = 1;
				}
				else		/* index type same as table */
				{
					indexFldType = fld->type;
					/* Bug 6280: preserve size, especially
					 * if non-var:
					 */
					indexFldN = fld->n;
				}

				/* Create `dupIndexFld' as copy of `infld': */
				if ((indexFldType & DDTYPEBITS) == FTN_CHAR &&
				    (infld->type & DDTYPEBITS) == FTN_STRLST &&
				    paramNumItems == 1)
				{
					dupIndexFld = TXdemoteSingleStrlstToVarchar(infld);
					if (!dupIndexFld) goto err;
				}
				else
				{
					dupIndexFld = emptyfld(indexFldType,
							       indexFldN);
					if (!dupIndexFld) goto err;
					_fldcopy((FLD *)infld, NULL,
						 dupIndexFld, NULL, fo);
				}

				if (returnAllRows)
					dupIndexFld = closefld(dupIndexFld);
				res = setdbidx(dbidx, dupIndexFld, dname,
					       dupIndexFld, 1, 1);
				TXdbidxUnlock(dbidx); /* Bug 2542 */
				if (res && (!order ||
				    infodbidx(dbidx)<TXbtreemaxpercent))
				{
					if(!(shortcircuit &&
					  tb->index.keepcached))
					if(dbidx != &tb->index)
						tb->index = *dbidx;
				}
				else
				{
					dbidx=closedbidx(dbidx);
					dbidx = NULL;
				}
				if(dbidx)
#ifdef BUBBLE_GPRED
				{
					tb->indguar=1;
					break;
				}
#else /* BUBBLE_GPRED */
					break;
#endif /* BUBBLE_GPRED */
			}
			if(dbidx)
				dbidx->keepcached = shortcircuit;
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			else if(indexinfo.iscores)
				indexinfo.iscores = TXfree(indexinfo.iscores);
			ret = (dbidx != NULL ? 1 : -1);
			goto done;
		case FOP_EQ :
			/* Make a dup of `fld' before copying `infld'
			 * to it: if `fld' is a virtual field,
			 * _fldcopy() would destroy `fld->isvirtual',
			 * causing later nametofld() to close and
			 * re-create the `tb->tbl->vfield[n]' FLD that
			 * `fld' is.  Shows up as result of Bug 4064
			 * fix in opendbidx():
			 */
			dupIndexFld = newfld(fld);
			if (!dupIndexFld) goto err;
			_fldcopy((FLD *)infld, NULL, dupIndexFld, NULL, fo);
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
				if(shortcircuit && tb->index.keepcached)
				{
					dbidx = &tb->index;
				}
				else
				/* KNG 20090313 Bug 2542 The setdbidx() call
				 * below was occurring without a read lock,
				 * because opendbidx() normally unlocks the
				 * index.  Possible fixes:
				 * 1) Bracket setdbidx() with lock/unlock.
				 *    Con: Adds another dblock() call.
				 * 2) Bracket opendbidx() + setdbidx() with
				 *    lock/unlock.
				 *    Pro: Just 1 dblock() call; opendbidx()
				 *         will find it already locked.
				 *    Con: Would have to pass `fc' counter
				 *         obtained from TXlockindex() here
				 *         down to TXbtcacheopen().
				 * 3) Tell opendbidx() not to unlock until
				 *    after setdbidx() call.
				 *    Pro: Just 1 dblock() call.
				 * 4) Let btcache code do all locking and
				 *    re-init, ie. setdbidx() should re-open
				 *    from btcache?
				 *    Con: not sure if possible.
				 * Use method 3.
				 */
				dbidx=opendbidx(indexinfo.itypes[j],
						indexinfo.paths[j],
						indexinfo.fields[j],
					indexinfo.sysindexParamsVals[j],tb,1);
				if(dbidx)
				{
					TXbool	hasSplitValues;

					/* A split-value index is probably not
					 * usable in bubble-up mode, and will
					 * probably give type-mismatch errors
					 * in setdbidx():
					 */
					hasSplitValues = TXbtreeHasSplitValues(indexinfo.fields[j], tb, dbidx->btree->params.indexValues);
					res = (!hasSplitValues &&
					       setdbidx(dbidx, dupIndexFld,
						dname, dupIndexFld, 1, 1));
					TXdbidxUnlock(dbidx); /* Bug 2542 */
					if(res && (!order ||
					    infodbidx(dbidx)<TXbtreemaxpercent))
					{
						if(!(shortcircuit &&
						  tb->index.keepcached))
						if(dbidx != &tb->index)
							tb->index = *dbidx;
					}
					else
					{
						if (hasSplitValues &&
						    TXverbosity > 0)
							txpmbuf_putmsg(TXPMBUFPN, MINFO, Fn, TXcannotUseIndexInBubbleUpModeFmt, dbidx->iname);
						dbidx=closedbidx(dbidx);
						dbidx = NULL;
					}
				}
				if(dbidx)
#ifdef BUBBLE_GPRED
				{
					tb->indguar=1;
					break;
				}
#else /* BUBBLE_GPRED */
					break;
#endif /* BUBBLE_GPRED */
			}
			if(dbidx)
				dbidx->keepcached = shortcircuit;
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			else if(indexinfo.iscores)
				indexinfo.iscores = TXfree(indexinfo.iscores);
			ret = (dbidx != NULL ? 1 : -1);
			goto done;
		case FOP_GT :
		case FOP_GTE :
			/* See Bug 4064 dup comment above: */
			dupIndexFld = newfld(fld);
			if (!dupIndexFld) goto err;
			_fldcopy((FLD *)infld, NULL, dupIndexFld, NULL, fo);
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
				if(shortcircuit && tb->index.keepcached)
				{
					dbidx = &tb->index;
				}
				else
				dbidx=opendbidx(indexinfo.itypes[j],
						indexinfo.paths[j],
						indexinfo.fields[j],
					indexinfo.sysindexParamsVals[j],tb,1);
				if(dbidx)
				{
					TXbool	hasSplitValues;

					/* See FOP_EQ split-values comment: */
					hasSplitValues = TXbtreeHasSplitValues(indexinfo.fields[j], tb, dbidx->btree->params.indexValues);
					res = !hasSplitValues;
					if(!rev)
						res = res && setdbidx(dbidx, dupIndexFld,
							dname, NULL,
							(p->op == FLDMATH_GTE), 0);
					else
						res = res && setdbidx(dbidx, NULL, dname,
							dupIndexFld, 0,
							(p->op == FLDMATH_GTE));
					TXdbidxUnlock(dbidx);	/* Bug 2542 */
					if (res && (!order ||
					    infodbidx(dbidx)<TXbtreemaxpercent))
					{
						if(!(shortcircuit &&
						  tb->index.keepcached))
						if(dbidx != &tb->index)
							tb->index = *dbidx;
					}
					else
					{
						if (hasSplitValues &&
						    TXverbosity > 0)
							txpmbuf_putmsg(TXPMBUFPN, MINFO, Fn, TXcannotUseIndexInBubbleUpModeFmt, dbidx->iname);
						dbidx=closedbidx(dbidx);
						dbidx=NULL;
					}
				}
				if(dbidx)
				{
#ifdef BUBBLE_GPRED
					tb->indguar = 1;
#endif /* BUBBLE_GPRED */
#ifdef NEVER /* Was JMT_TEST */
					PROJ *op;
					PRED *opred;

					opred = (PRED *)TXcalloc(TXPMBUFPN, Fn, 1, sizeof(PRED));
					if(opred)
					{
						opred->lt = NAME_OP;
						if(rname)
							opred->left = TXstrdup(TXPMBUFPN, fn, rname);
						else
							opred->left = TXstrdup(TXPMBUFPN, fn, dname);
						putmsg(999, NULL,
						"We might be ordered by %s, and may want to be ordered by %s", TXdisppred(opred, 0, 0, 80), TXdisppred(order->preds[0], 0, 0, 80));
						op = (PROJ *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(PROJ));
						if(op)
						{
						op->n = 1;
						op->preds = (PRED **)TXcalloc(TXPMBUFPN, fn, 1, sizeof(PRED *));
						if(op->preds)
						{
							op->preds[0] = opred;
							tb->order=op;
						}
						}
					}

/*Want to set an order by here that matches what we are thinking*/
#endif /* NEVER */
					break;
				}
			}
			tb->index.keepcached = shortcircuit;
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			ret = (dbidx != NULL ? 1 : -1);
			goto done;
		case FOP_LT :
		case FOP_LTE :
			/* See Bug 4064 dup comment above: */
			dupIndexFld = newfld(fld);
			if (!dupIndexFld) goto err;
			_fldcopy((FLD *)infld, NULL, dupIndexFld, NULL, fo);
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
				if(shortcircuit && tb->index.keepcached)
				{
					dbidx = &tb->index;
				}
				else
				dbidx=opendbidx(indexinfo.itypes[j],
						indexinfo.paths[j],
						indexinfo.fields[j],
					indexinfo.sysindexParamsVals[j],tb,1);
				if(dbidx)
				{
					TXbool	hasSplitValues;

					/* See FOP_EQ split-values comment: */
					hasSplitValues = TXbtreeHasSplitValues(indexinfo.fields[j], tb, dbidx->btree->params.indexValues);
					res = !hasSplitValues;
					if(!rev)
						res = res && setdbidx(dbidx, NULL, dname,
							dupIndexFld, 0,
							(p->op == FLDMATH_LTE));
					else
						res = res && setdbidx(dbidx, dupIndexFld,
							dname, NULL,
							(p->op == FLDMATH_LTE), 0);
					TXdbidxUnlock(dbidx);	/* Bug 2542 */
					if (res && (!order ||
					    infodbidx(dbidx)<TXbtreemaxpercent))
					{
						if(!(shortcircuit &&
						  tb->index.keepcached))
						if(dbidx != &tb->index)
							tb->index = *dbidx;
					}
					else
					{
						if (hasSplitValues &&
						    TXverbosity > 0)
							txpmbuf_putmsg(TXPMBUFPN, MINFO, Fn, TXcannotUseIndexInBubbleUpModeFmt, dbidx->iname);
						dbidx=closedbidx(dbidx);
						dbidx=NULL;
					}
				}
				if(dbidx)
#ifdef BUBBLE_GPRED
				{
					tb->indguar = 1;
					break;
				}
#else /* BUBBLE_GPRED */
					break;
#endif /* BUBBLE_GPRED */
			}
			tb->index.keepcached = shortcircuit;
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			ret = (dbidx != NULL ? 1 : -1);
			goto done;
#ifdef NO_BUBBLE_INDEX
		case FOP_MM :
			fld = createfld("varchar", 255, 0);
			v = getfld((FLD *)infld, &sz);
			putfld(fld, v, sz);
			fld->n = ((FLD *)infld)->n;
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
				int	cop;

				ix = ixmmindex(indexinfo.itype[j], indexinfo.iname[j], fld, dname, tb, p->op, &cop);
				if(cop && asc)
					p->op = FLDMATH_NMM;
				if (!ix)
					continue;
				else
				{
/* Can't guarantee anything at this point
					in->gpred = p;
*/
					break;
				}
			}
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			fld = closefld(fld);
			ret = ix;
			goto done;
		case FOP_NMM :
		case FOP_RELEV :
		case FOP_PROXIM :
			fld = createfld("varchar", 255, 0);
			v = getfld((FLD *)infld, &sz);
			putfld(fld, v, sz);
			fld->n = ((FLD *)infld)->n;
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
				int	cop;

				ix = ixmmindex(indexinfo.itype[j], indexinfo.iname[j], fld, dname, tb, p->op, &cop);
				if (!ix)
					continue;
				else
				{
					in->gpred = p;
					break;
				}
			}
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			fld = closefld(fld);
			ret = ix;
			goto done;
#endif
#ifndef NO_BUBBLE_MATCH
		case FOP_MAT:
		{
			char	prefixbuf[256];
			char	prefixbuf1[256];
			size_t	sz, szq;
			int	rc, inchi;
			FLD	*fld2;
			char	*s, *prefix = prefixbuf, *prefix1 = prefixbuf1;

			/* Cannot use index for `$param MATCHES column': */
			if (!lookright) break;
			/* Probably cannot use reversed index (wtf?): */
			if (rev) break;
			/* If no data, MATCHES open probably failed earlier:*/
			if (getfld(infld, SIZE_TPN) == NULL) goto fopmatbad;
			s = TXmatchgetr(infld, &szq);
			if (s == CHARPN) goto fopmatbad;
			if (szq + 5 > sizeof(prefixbuf))
			  {
			    errno = 0;
			    if ((prefix = (char *)TXmalloc(TXPMBUFPN, Fn, szq + 5)) == CHARPN ||
				(prefix1 = (char *)TXmalloc(TXPMBUFPN, Fn, szq + 5)) == CHARPN)
				goto fopmatbad;
			  }
			TXget_globalcp();
			rc = sregprefix(s, prefix, szq+5, &sz,
			    (TXCFF_GET_CASESTYLE(globalcp->stringcomparemode)
				== TXCFF_CASESTYLE_IGNORE) ? 1 : 0);
			DBGMSG(1, (999, NULL, "Prefix = %s(%d) %d", prefix,sz, rc));
			/* If neither looking for exact-match nor a prefix
			 * followed by anything (wildcard), we either cannot
			 * use an index, or would need post-processing;
			 * therefore no bubble-up.  KNG 20120220 optimization:
			 * bail now, no need to setdbidx() an index we will
			 * not use:
			 */
			if ((sz + 1 < szq) &&	/* ? */
			    (rc == 0))/* neither exact-match nor prefix+wild */
				break;

			if (!sz)		/* no prefix: use all of idx*/
			{			/* Bug 4227 */
				fld = fld2 = NULL;
				rc = 1;		/* avoid fld2 create below */
			}
			else
			{
				fld = createfld("varchar", szq + 5, 0);
				putfld(fld, prefix, sz);
			}
			if(rc == 1)		/* looking for exact-match */
			{
				fld2 = fld;
				inchi = 1;
			}
			else
			{
				memcpy(prefix1, prefix, sz+1);
				prefix1[sz-1]++;
				DBGMSG(1, (999, NULL, "Prefix1 = %s(%d)", prefix1,sz));
				fld2 = createfld("varchar", 255, 0);
				putfld(fld2, prefix1, sz);
				inchi = 0;
			}
			for(j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright);
			    j >= 0;
			    j=TXchooseindex(&indexinfo, tb, p->op, infld,
					    lookright))
			{
#ifndef NO_BUBBLE_MATCH
				if(shortcircuit && tb->index.keepcached)
				{
					dbidx = &tb->index;
				}
				else
				dbidx=opendbidx(indexinfo.itypes[j],
						indexinfo.paths[j],
						indexinfo.fields[j],
					/* MATCHES needs splitstrlst iff
					 * multi-item index type, since
					 * it looks at items; see also
					 * TXbtreeScoreIndex():
					 */
					indexinfo.sysindexParamsVals[j],tb,7);
				if(dbidx)
				{
					if(!rev)
						res = setdbidx(dbidx, fld, dname, fld2, 1, inchi);
					else
						res = setdbidx(dbidx, fld2, dname, fld, inchi, 1);
					TXdbidxUnlock(dbidx);	/* Bug 2542 */
					if (!res)
					{
						dbidx = closedbidx(dbidx);
						continue;
					}
					if(!(shortcircuit &&
					  tb->index.keepcached))
					if(dbidx != &tb->index)
						tb->index = *dbidx;
				}
				/* KNG 20120220 `rc' etc. now checked above;
				 * already know we can use `dbidx' & guarantee:
				 */
#  ifdef BUBBLE_GPRED
				tb->indguar = 1;
#  endif /* BUBBLE_GPRED */
				break;
#else /* NO_BUBBLE_MATCH */
				ix = ixbtindex(indexinfo.itype[j], indexinfo.iname[j], fld, dname, fld2, 1, inchi, fo, tb);
				if (!ix)
					continue;
				else
				{
					in->gpred = p;
					break;
				}
#endif /* NO_BUBBLE_MATCH */
			}
			tb->index.keepcached = shortcircuit;
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			if(fld!=fld2)
				fld2 = closefld(fld2);
			fld=closefld(fld);
		fopmatbad:
			if (prefix != prefixbuf && prefix != CHARPN)
			  prefix = TXfree(prefix);
			if (prefix1 != prefixbuf1 && prefix1 != CHARPN)
			  prefix1 = TXfree(prefix1);
			ret = (dbidx != NULL ? 1 : -1);
			goto done;
		}
#endif /* BUBBLE_MATCH */
		default :
#ifdef DEBUG
/* WTF: != */
			DBGMSG(1,(999, NULL, "Don't know how to handle op %d", p->op));
#endif
			if(!shortcircuit)
				closeindexinfo(&indexinfo);
			goto err;
	}
	if(!shortcircuit)
		closeindexinfo(&indexinfo);
	goto err;

err:
	ret = -1;
done:
	rname = TXfree(rname);
	if (dupIndexFld) dupIndexFld = closefld(dupIndexFld);
	if (promotedParamFld) promotedParamFld = closefld(promotedParamFld);
	return(ret);
}

#endif /* NO_BUBBLE_INDEX */
