/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "fldmath.h"
#include "fldcmp.h"
#include "texint.h"


/****************************************************************************/
/*
 *	Setup an index to hold the result of a projection.
 *	Returns an index matching the projection.
 */

DBTBL *
tup_index_setup(tin, proj, fo, rankdir, outputDd)
DBTBL	*tin;	/* Original table */
PROJ	*proj;	/* The projection */
FLDOP	*fo;	/* A field op for any calculations */
TXOF	rankdir;/* How to count the rank field. */
DD	*outputDd;	/* (in/out, opt.) Projection output DD to alter */
{
	DD *dd;
	DBTBL *dbt;
	int  i;
	TXOF	rev;
	char *fname;
	char *nname;
	FLDCMP	*fc;
	int	sz, nn, idx, res, fnameNum;

	dd = opendd();
        if(!dd)
         return (DBTBL *)NULL;
	dbt = (DBTBL *)calloc(1, sizeof(DBTBL));
	(void)ddsettype(dd, 1);
	for (i = 0; i < proj->n; i++)
	{
		fname = TXdisppred(proj->preds[i], 0, 0, 0);
#ifdef TX_USE_ORDERING_SPEC_NODE
		rev = proj->preds[i]->orderFlags;
#else /* !TX_USE_ORDERING_SPEC_NODE */
		rev = (TXOF)0;
		if (fname[strlen(fname)-1] == '^') /* IGNCASE ? */
		{
			fname[strlen(fname)-1] = '\0';
			rev |= OF_IGN_CASE;
		}
		if (fname[strlen(fname)-1] == '-') /* DESC ? */
		{
			fname[strlen(fname)-1] = '\0';
			rev |= OF_DESCENDING;
		}
#endif /* !TX_USE_ORDERING_SPEC_NODE */
#ifdef NEVER
		if(isnumeric(fname))
		{
			int fldnum = atoi(fname);
			DD *tdd = tin->tbl->dd;

			fname = strdup(ddgetname(tdd, ddgetorign(tdd, fldnum)));
			nname = gettypename(tdd[ddgetorign(tdd, fldnum)]);
		}
		else
#endif
		nname = predtype(proj->preds[i], tin, fo, &sz, &nn);
		if (nname == (char *)NULL)
			putmsg(MWARN+UGE,NULL,"Field non-existent in %s",fname);
		else
		{
			/* If GROUP BY/DISTINCT on sole strlst field, change
			 * it to a varchar here.  This tells TXdemuxOpen()
			 * to split on this field.  Bug 2397:
			 */
			if (TXApp->multiValueToMultiRow &&
			    proj->n == 1 &&	/* only one field */
			    outputDd != DDPN &&	/* is GROUP BY or the like */
			    (strcmpi(nname, "varstrlst") == 0 ||
			     strcmpi(nname, "strlst") == 0))
			{
				char	*firstColName;

				nname = "varchar";
				sz = 1;
				/* Also change the same field in output.
				 * See TXgetMultiValueSplitFldIdx()
				 * comments as to how we look up the
				 * column here (Bug 1995):
				 */
				firstColName =
			       TXpredGetFirstUsedColumnName(proj->preds[i]);
				if (firstColName &&
				    (idx=ddfindname(outputDd,firstColName))>=0)
				{		/* wtf DD drill: */
					outputDd->fd[idx].type =
						(DDVARBIT | FTN_CHAR);
					outputDd->fd[idx].size =
						1*sizeof(ft_char);
					outputDd->fd[idx].elsz =
						sizeof(ft_char);
				}
			}
			fnameNum = putdd(dd, fname, nname, sz, nn);
			if (fnameNum < 0)
				putmsg(MERR, __FUNCTION__,
				       "Could not add `%s' to DD", fname);
			else
			/* Bug 4425 comment #8: ddsetordern() may fail
			 * for long expressions:
			 */
			if (!TXddSetOrderFlagsByIndex(dd, fnameNum - 1, rev))
				putmsg(MERR, __FUNCTION__,
				   "Could not set order flags for field `%s'", 
				       fname);
		}
		fname = TXfree(fname);
	}
#ifndef OLD_ORDERBY
	res = putdd(dd, (char *)TXrankColumnName, (char *)TXrankColumnTypeStr,
                    1, 1);
	/* Bugfix: If $rank is already in `dd', do not change its
	 * user-specified dir; only apply `rankdir' as default if we
	 * are adding $rank.  Fixed at same time as legacyVersion7OrderByRank
	 * introduced, so tie it to that just in case expected:
	 */
	if ((TXApp && TXApp->legacyVersion7OrderByRank) ||    /* old way or */
	    res)				/* we added it */
		ddsetordern(dd, TXrankColumnName, rankdir);
#endif
	if(tin->lname)
		dbt->lname = strdup(tin->lname);
	dbt->rname = NULL;
	dbt->tbl = createtbl(dd, NULL);
	dbt->index.btree = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
#ifndef NO_BUBBLE_INDEX
	dbt->index.type = DBIDX_MEMORY;
#endif
	btreesetdd(dbt->index.btree, dd);
	/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
	 * stringcomparemode:
	 */
	BTPARAM_INIT_TO_PROCESS_DEFAULTS(&dbt->index.btree->params, dbt->ddic);

	fc = TXopenfldcmp(dbt->index.btree /*dd*/, TXOPENFLDCMP_CREATE_FLDOP);
	btsetcmp(dbt->index.btree, (btcmptype)fldcmp);
	dbt->index.btree->usr = fc;

	dbt->type = 'B';
	dbt->ddic = tin->ddic;
	dbt->frecid = createfld("recid", 1, 0);
	dbt->tblid = -1;
	putfld(dbt->frecid, &dbt->recid, 1);
	closedd(dd);
	return dbt;
}

/****************************************************************************/
/*
 *	Make an index on a tuple, and write to the result table
 */

RECID *
tup_index(tin, tout, proj, fo, where)
DBTBL	*tin;
DBTBL	*tout;
PROJ	*proj;
FLDOP	*fo;
RECID	*where;
{
	FLD *fout;
	int  i;
	size_t sz;
	void *v = NULL;
	FTN	vType = (FTN)0;
#ifdef JMT_COMP
	char *nname;
#endif

	for (i = 0; i < proj->n; i++)
	{
		if(proj->p_type > PROJ_SINGLE)
		{
#ifndef JMT_COMP
			fout = getfldn(tout->tbl, i, NULL);
#else
			nname = TXdisppred(proj->preds[i], 0, 0, 0);
			fout = dbnametofld(tout, nname);
			free(nname);
#endif
		}
		else
			fout = NULL;

		/* if (fout != (FLD *)NULL) */
			/* v = getfld(fout, &sz); */

		switch(proj->p_type)
		{
		case PROJ_AGG_CALCED:
#ifdef OLD_STATS
			v = evalstats(tin, proj->preds[i], fo, &sz);
			vType = ?;
			break;
#endif
		case PROJ_AGG:
			v = evalpred(tin, proj->preds[i], fo, &sz, &vType);
			if (!v)
				return NULL;
			break;
		default:
			break;
		}
		if (proj->p_type > PROJ_SINGLE && fout != (FLD *)NULL)
		{
			size_t maxsz;

			maxsz =  tout->ddic->options[DDIC_OPTIONS_MAX_INDEX_TEXT];
			if(maxsz > 0 && sz > maxsz)
				sz = maxsz;
			/* Sanity check on `v' type: */
			/* See also TXgetMultiValueSplitFldIdx() Bug 1995 comments: */
			if ((vType & FTN_VarBaseTypeMask) !=
			    (TXfldType(fout) & FTN_VarBaseTypeMask))
                          {
                            /* Yap at least each statement, but not each row:*/
                            if (!TXApp ||
                                !TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_TupIndexPredEvalWrongType])
                              {
                                char    *predDisp;

                                if (TXApp)
                                  TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_TupIndexPredEvalWrongType] = 1;
                                predDisp = TXdisppred(proj->preds[i], 0,0,0);
                                putmsg(MERR, __FUNCTION__,
	   "Pred `%s' evaluated to type %s, but expected type %s: Discarding",
                                       predDisp, ddfttypename(vType),
                                       TXfldtypestr(fout));
                                predDisp = TXfree(predDisp);
                              }
                            TXftnFreeData(v, sz, vType, 1);
                            v = NULL;
                        }
			else
			{
			/* Bug 6249: `fout' should own `v'; it is alloced: */
				setfldandsize(fout, v, sz*ddftsize(vType) + 1, FLD_FORCE_NORMAL);
				v = NULL;
			}
		}
		else
		{
			TXftnFreeData(v, sz, vType, 1);
			v = NULL;
		}
	}
#ifndef OLD_ORDERBY
	if (proj->p_type > PROJ_SINGLE)
	{
		ft_int trank;

		trank = tin->rank;
		fout = getfldn(tout->tbl, i, NULL);
		if(fout)
		{
			setfldv(fout);
			*(ft_int *)fout->v = trank;
		}
	}
#endif
	if (proj->p_type > PROJ_SINGLE)
		return putdbtblrow(tout, where);
	return NULL;
}

/****************************************************************************/
/*
 *	Search an index on a tuple.
 */

RECID *
tup_index_search(tin, tout, proj, fo, where)
DBTBL	*tin;
DBTBL	*tout;
PROJ	*proj;
FLDOP	*fo;
RECID	*where;
{
	FLD *fout;
	RECID *rc;
	int  i;
	size_t sz;
	void *v = NULL;
	FTN	vType = (FTN)0;
#ifdef JMT_COMP
	char *nname;
#endif

	(void)where;
	for (i = 0; i < proj->n; i++)
	{
		if(proj->p_type > PROJ_SINGLE)
		{
#ifndef JMT_COMP
			fout = getfldn(tout->tbl, i, NULL);
#else
			nname = TXdisppred(proj->preds[i], 0, 0, 0);
			fout = dbnametofld(tout, nname);
			free(nname);
#endif
		}
		else
			fout = NULL;

		/* if (fout != (FLD *)NULL) */
			/* v = getfld(fout, &sz); */

		switch(proj->p_type)
		{
		case PROJ_AGG_CALCED:
#ifdef OLD_STATS
			v = evalstats(tin, proj->preds[i], fo, &sz);
			vType = ?;
			break;
#endif
		case PROJ_AGG:
			v = evalpred(tin, proj->preds[i], fo, &sz, &vType);
			if (!v)
				return NULL;
			break;
		default:
			break;
		}
		if (proj->p_type > PROJ_SINGLE && fout != (FLD *)NULL)
		{
			/* Sanity check on `v' type: */
			/* See also TXgetMultiValueSplitFldIdx() Bug 1995 comments: */
			if ((vType & FTN_VarBaseTypeMask) !=
			    (TXfldType(fout) & FTN_VarBaseTypeMask))
			{
                            /* Yap at least each statement, but not each row:*/
                            if (!TXApp ||
				!TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_TupIndexPredEvalWrongType])
                              {
                                char    *predDisp;

				if (TXApp)
                                  TXApp->didOncePerSqlMsg[TXoncePerSqlMsg_TupIndexPredEvalWrongType] = 1;
				predDisp = TXdisppred(proj->preds[i], 0,0,0);
				putmsg(MERR, __FUNCTION__,
	   "Pred `%s' evaluated to type %s, but expected type %s: Discarding",
				       predDisp, ddfttypename(vType),
				       TXfldtypestr(fout));
				predDisp = TXfree(predDisp);
                              }
                            TXftnFreeData(v, sz, vType, 1);
                            v = NULL;
			}
			else
			{
			/* Bug 6249: `fout' should own `v'; it is alloced: */
				setfldandsize(fout, v, sz*ddftsize(vType) + 1, FLD_FORCE_NORMAL);
				v = NULL;
			}
		}
		else
		{
			TXftnFreeData(v, sz, vType, 1);
			v = NULL;
		}
	}
#ifndef OLD_ORDERBY
	if (proj->p_type > PROJ_SINGLE)
	{				/* dup code from above? */
		ft_int trank;

		trank = tin->rank;
		fout = getfldn(tout->tbl, i, NULL);
		if(fout)
		{
			setfldv(fout);
			*(ft_int *)fout->v = trank;
		}
	}
#endif
	sz = fldtobuf(tout->tbl);
	rc = malloc(sizeof(RECID));
	if(rc)
		*rc = btsearch(tout->index.btree, sz, tout->tbl->orec);
	return rc;
}

/****************************************************************************/

