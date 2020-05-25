/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"

static ft_int mone = -1;
static ft_int pone = 1;
static ft_int pzero = 0;

/****************************************************************************/

static int forev ARGS((FLDOP *));

static int
forev(fo)
FLDOP *fo;
{
	FLD	f[MAXFLDARGS];
	int	mine[MAXFLDARGS];
	int	nstack, i;

	nstack=fonmark(fo);        /* find how many pushes since last mark */
	if(nstack>MAXFLDARGS) return(FOP_EDOMAIN);/* too many args */
	for(i=0;i<nstack;i++)
	{
		f[i]=*fopeekn(fo,i+1);
		mine[i] = fo->fs->mine[fo->fs->numUsed - (i+1)];
	}
	for(i=0;i<nstack;i++)
	{
		fo->fs->f[fo->fs->numUsed - (nstack - i)] = f[i];
		fo->fs->mine[fo->fs->numUsed - (nstack - i)] = mine[i];
	}
	return 0;
}

/******************************************************************/

void
TXpredClear(p, full)
PRED	*p;
int	full;	/* (in) nonzero: full clear (all cached info) */
{
	if(!p)
		return;
	p->value = 0;
	if (full)
	{
		/* Bug 4242: clear *all* PRED.fldmathfunc: */
		p->fldmathfunc = NULL;
		if (p->rat == FIELD_OP &&
		    p->altright &&
		    !(p->dff & 0x8))
			closefld(p->altright);
		p->rat = 0;
		p->altright = NULL;
		if (p->lat == FIELD_OP &&
		    p->altleft &&
		    !(p->dff & 0x4))
			closefld(p->altleft);
		p->lat = 0;
		p->altleft = NULL;
	}
	if(p->lt == 'P')
		TXpredClear(p->left, full);
	if(p->rt == 'P')
		TXpredClear(p->right, full);
}

/******************************************************************/

void
pred_rmalts(p)
PRED	*p;
{
	if(!p)
		return;
	p->value = 0;
	if(p->lt == 'P')
		pred_rmalts(p->left);
	if(p->rt == 'P')
		pred_rmalts(p->right);
	if(p->lt == NAME_OP)
		p->lat = 0;
	if(p->rt == NAME_OP)
		p->rat = 0;
}

/******************************************************************/
/*
	dbtbl is going away.  Remove our memory of it.   If
	dbtbl is NULL remove all memories.
*/

void
pred_rmfieldcache(PRED *p, DBTBL *dbtbl)
{
	if(!p)
		return;
	if(dbtbl)
	{
		if(p->lvt == dbtbl)
			p->lvt = NULL;
		if(p->lnvt == dbtbl)
			p->lnvt = NULL;
		if(p->rvt == dbtbl)
			p->rvt = NULL;
		if(p->rnvt == dbtbl)
			p->rnvt = NULL;
	}
	else
	{
		p->lvt = NULL;
		p->lnvt = NULL;
		p->rvt = NULL;
		p->rnvt = NULL;
	}
	if(p->lt == 'P')
		pred_rmfieldcache(p->left, dbtbl);
	if(p->rt == 'P')
		pred_rmfieldcache(p->right, dbtbl);
	return;
}

/******************************************************************/

void
pred_sethandled(p)
PRED	*p;
/* Recursively sets `handled' flag true on `p'.
 */
{
	if(!p)
		return;
	p->handled = 1;
	if(p->lt == 'P')
		pred_sethandled(p->left);
	if(p->rt == 'P')
		pred_sethandled(p->right);
}

/******************************************************************/

int
pred_allhandled(p)
PRED	*p;
/* Returns nonzero if `p' tree is entirely `handled'-true, zero otherwise.
 * Updates it along the way.
 */
{
	int rc;

	if(!p)
		return 1;
	if(p->lt == 'P')
	{
		rc = pred_allhandled(p->left);
		if(!rc)
			return rc;
	}
	if(p->rt == 'P')
	{
		rc = pred_allhandled(p->right);
		if(!rc)
			return rc;
		if(p->lt == 'P')
			p->handled = 1;
	}
	return p->handled;
}

/******************************************************************/

int
TXpred_haslikep(p)
PRED	*p;
{
	int rc = 0;

	if(!p)
		return 0;
	if(p->op == FLDMATH_PROXIM)
		return 1;
	if(p->lt == 'P')
	{
		rc = TXpred_haslikep(p->left);
		if(rc)
			return rc;
	}
	if(p->rt == 'P')
	{
		return TXpred_haslikep(p->right);
	}
	return rc;
}

/******************************************************************/

int
TXpred_countnames(p)
PRED	*p;
{
	int rc = 0;

	if(!p)
		return 0;
	if(p->op == FLDMATH_PROXIM)
		return 1;
	switch(p->lt)
	{
		case 'P':
			rc += TXpred_countnames(p->left);
			break;
		case NAME_OP:
			rc ++;
			break;
		default:
			break;
	}
	switch(p->rt)
	{
		case 'P':
			rc += TXpred_countnames(p->right);
			break;
		case NAME_OP:
			rc ++;
			break;
		default:
			break;
	}
	return rc;
}

/* ----------------------------------------------------------------------- */

int
TXprojIsRankDescOnly(PROJ *proj)
/* Returns nonzero if `proj' (e.g. ORDER BY) is exactly `$rank desc'
 * (with `desc' interpreted numerically), 0 if not.
 */
{
	int	ret;
	char	*fname = NULL;
	PRED	*pred;

	if (proj->n != 1) goto not;		/* multiple predicates */

	pred = proj->preds[0];
#ifdef TX_USE_ORDERING_SPEC_NODE
	if (pred->op != QNODE_OP_UNKNOWN || pred->lt != NAME_OP) goto not;
	fname = TXpredToFieldOrderSpec(pred);
#else /* !TX_USE_ORDERING_SPEC_NODE */
	fname = TXdisppred(pred, 0, 0, 0);
#endif /* !TX_USE_ORDERING_SPEC_NODE */
	if (!fname) goto err;
	if (strncmp(fname, TXrankColumnName, TX_RANK_COLUMN_NAME_LEN) != 0)
		goto not;			/* not `$rank' */
	if (TXApp && TXApp->legacyVersion7OrderByRank)
	{			/* negative internal ranks */
		if (fname[TX_RANK_COLUMN_NAME_LEN])	/* not numeric desc */
			goto not;
	}
	else
	{
		if (fname[TX_RANK_COLUMN_NAME_LEN] != '-' ||	/* not DESC */
		    fname[TX_RANK_COLUMN_NAME_LEN + 1])
			goto not;
	}
	ret = 1;
	goto finally;

err:
not:
	ret = 0;
finally:
	fname = TXfree(fname);
	return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXpredHasRank(PRED *p)
/* Returns 1 if `p' contains a reference to $rank, 0 if not.
 */
{
	int rc = 0;

	if (!p) goto finally;

	switch (p->lt)
	{
	case NAME_OP:
		rc = (rc || TXisRankName(p->left));
		break;
	case 'P':
		rc = (rc || TXpredHasRank(p->left));
		break;
	default:
		break;
	}
	switch (p->rt)
	{
	case NAME_OP:
		rc = (rc || TXisRankName(p->right));
		break;
	case 'P':
		rc = (rc || TXpredHasRank(p->right));
		break;
	default:
		break;
	}

finally:
	return(rc);
}

/* ------------------------------------------------------------------------ */

int
TXprojHasRank(PROJ *proj)
/* Returns nonzero if `proj' contains an expression that uses $rank.
 */
{
	int	i;

	for (i = 0; i < proj->n; i++)
		if (TXpredHasRank(proj->preds[i]))
			return(1);
	return(0);
}

/******************************************************************/

int TXfldnamecmp ARGS((DBTBL *, char *, char *));
static int pred_evalstats ARGS((DBTBL *, PRED *, FLDOP *));

static int
pred_evalstats(t, p, fo)
DBTBL	*t;
PRED	*p;
FLDOP	*fo;
{
	FLD	*fld;

/*
	WTF:
		Error checking.
		Make sure we push good values.
*/
	if(!strcmp(p->left, "avg"))
	{
		PRED *p2;

		p2 = duppred(p);
		free(p2->left);
		p2->left = strdup("sum");
		fld = TXgetstatfld(t, p2);
		if(!fld)
		{
			p2 = closepred(p2);
			putmsg(MERR, NULL, "Could not evaluate avg()");
			return -1;
		}
		fopush(fo, fld);
		free(p2->left);
		if(!getfld(fld, NULL))
		{
			p2 = closepred(p2);
			putmsg(MERR, NULL, "Could not evaluate avg()");
			fodisc(fo);
			return -1;
		}
		p2->left = strdup("count");
		fld = TXgetstatfld(t, p2);
		if(!fld)
		{
			p2 = closepred(p2);
			putmsg(MERR, NULL, "Could not evaluate avg()");
			fodisc(fo);
			return -1;
		}
		fopush(fo, fld);
		foop(fo, FOP_DIV);
		closepred(p2);
		return 1;
	}
	else
	{
		long *zero;

		fld = TXgetstatfld(t, p);
		if(!fld)
		{
			return -1;
		}
		if(!getfld(fld, NULL))
		{
			if(!strcmp(p->left, "count"))
			{
				zero = (long *)calloc(1, sizeof(long));
				if(zero)
				{
					*zero = 0;
					setfld(fld, zero, 1);
				}
			}
			else
			{
				return -1;
			}
		}
		fopush(fo, fld);
		return 1;
	}
}

/******************************************************************/

static int TXcacheconv ARGS((PRED *p, FLDOP *fo));
static int
TXcacheconv(p, fo)
PRED *p;
FLDOP *fo;
{
	static CONST char	fn[] = "TXcacheconv";

	if(p->lt == FIELD_OP && p->lat == 0 &&
	   fo->hadtf1 && fo->owntf1 && fo->tf1)
	{
		if (TXfldmathverb >= 3)
			putmsg(MINFO, fn,
"Caching arg1 promotion result into %s predicate altleft: type=%s=%d%s%s%s",
				TXqnodeOpToStr(p->op, CHARPN, 0),
				TXfldtypestr(fo->tf1), (int)fo->tf1->type,
				(TXfldmathverb >= 4 ? " value=[" : ""),
				(TXfldmathverb >= 4 ? fldtostr(fo->tf1) : ""),
				(TXfldmathverb >= 4 ? "]" : ""));
		p->lat = FIELD_OP;
		p->altleft = fo->tf1;
		fo->owntf1 = 0;
		fo->tf1 = NULL;
		p->fldmathfunc = NULL;
	}
	if(p->rt == FIELD_OP && p->rat == 0 &&
	   fo->hadtf2 && fo->owntf2 && fo->tf2)
	{
		if (TXfldmathverb >= 3)
			putmsg(MINFO, fn,
"Caching arg2 promotion result into %s predicate altright: type=%s=%d%s%s%s",
				TXqnodeOpToStr(p->op, CHARPN, 0),
				TXfldtypestr(fo->tf2), (int)fo->tf2->type,
				(TXfldmathverb >= 4 ? " value=[" : ""),
				(TXfldmathverb >= 4 ? fldtostr(fo->tf2) : ""),
				(TXfldmathverb >= 4 ? "]" : ""));
		p->rat = FIELD_OP;
		p->altright = fo->tf2;
		fo->owntf2 = 0;
		fo->tf2 = NULL;
		p->fldmathfunc = NULL;
	}
	return 0;
}

static int TXpredNumFunctionArgsList ARGS((PRED *pred));
static int
TXpredNumFunctionArgsList(pred)
PRED	*pred;
{
	int	numArgs = 0;

	if (!pred || pred->op != LIST_OP) return(0);
	switch (pred->lt)
	{
		case FIELD_OP:
			numArgs++;
			break;
		case 'P':
			numArgs += TXpredNumFunctionArgsList((PRED *)pred->left);
			break;
		default:
			break;
	}
	switch (pred->rt)
	{
		case FIELD_OP:
			numArgs++;
			break;
		case 'P':
			numArgs += TXpredNumFunctionArgsList((PRED *)pred->right);
			break;
		default:
			break;
	}
	return(numArgs);
}

int
TXpredNumFunctionArgs(pred)
PRED	*pred;	/* (in) REG_FUN_OP predicate tree */
/* Returns number of arguments passed to REG_FUN_OP `pred', or -1 on error.
 */
{
	int	numArgs;

	if (pred->op != REG_FUN_OP) return(-1);
	/* right-side is the args: */
	switch (pred->rt)
	{
		case FIELD_OP:
			numArgs = 1;
			break;
		case 'P':
			numArgs = TXpredNumFunctionArgsList((PRED *)pred->right);
			break;
		default:
			numArgs = 0;
			break;
	}
	return(numArgs);
}

/* ------------------------------------------------------------------------ */

char *
TXpredGetFirstUsedColumnName(PRED *pred)
/* Returns name of first column referenced in `pred'.
 */
{
	char	*name;

	switch (pred->op)
	{
	case REG_FUN_OP:
	case AGG_FUN_OP:
		/* Left child of function is a NAME_OP, but it is the
		 * function name, not a table column (which we would
		 * otherwise return):
		 */
		break;
		/* wtf `pred->op' will never be NAME_OP? */
	default:				/* look left first */
		switch (pred->lt)
		{
		case 'P':
			name=TXpredGetFirstUsedColumnName((PRED *)pred->left);
			if (name) return(name);
			break;
		case NAME_OP:
			return((char *)pred->left);
		default:
			break;
		}
		break;
	}

	switch (pred->rt)			/* look right second */
	{
	case 'P':
		return(TXpredGetFirstUsedColumnName((PRED *)pred->right));
	case NAME_OP:
		return((char *)pred->left);
	default:
		return(NULL);
	}
}		

/******************************************************************/

int
pred_eval(tup, p, fo)
DBTBL *tup;	/* (in) optional if called from Vortex */
PRED *p;
FLDOP *fo;
/* NOTE: see also source/vortex/expr.c TXvxIsPredCompilable() etc.
 * if anything changes here, especially need for `tup'.
 * Note that if called from Vortex, `tup' may be NULL.
 */
{
	FLD *f, *r, *r2;
	FLD	*tf1 = NULL, *tf2 = NULL;
        FOP fldOp;
	int rc, rc2 = 0;
	size_t	n, n2;
	int retval = 1;
	void	*v;
	ft_int	*intVal, *intVal2;

#define IS_LINEAR(dbtbl)        (!(dbtbl)->index.btree)

#ifdef LIKE_OPTIMIZE
	if (p == (PRED *)NULL || p->op == FOP_NMM || p->op == FOP_RELEV || p->op == FOP_PROXIM || p->assumetrue)
#else
	if (p == (PRED *)NULL || p->assumetrue ||
	    /* Bug 6404: A fully-indexable LIKE ORed with a
	     * fully-indexed non-LIKE clause(*), was post-proc'ing the
	     * LIKE, because the OR prevents the LIKE from being
	     * replaced with LIKER in realwork() (which would avoid
	     * post-proc: LIKER is no-op true in fldmath n_fchch()).
	     * But this post-proc was not reported/checked (see other
	     * OPTIMIZE_LIKE_HANDLED use), because it is outside of
	     * fdbi_get()'s check.
	     *
	     * Although no LIKER was set, `p->handled' is still left
	     * true (unless !OPTIMIZE_SHORTCUTS; defaults true),
	     * indicating no post-proc needed.  So here we can assume
	     * a `p->handled' LIKE is true, *if* we are not linear
	     * (still have the index results RAM B-tree
	     * `tup->index.btree').  Otherwise (linear) we are being
	     * called for every row in the table, not just the LIKE
	     * index results (other side of OR is linear e.g. `1 =
	     * 0'), and we must do the linear LIKE because this might
	     * be a non-LIKE-match row:
	     *
	     *  *Non-LIKE because a fully-indexed LIKE would be set
	     * `handled', which (with the other fully-indexed LIKE
	     * marked `handled') could propagate up as all-handled and
	     * avoid pred eval altogether (thanks to OPTIMIZE_SHORTCUTS)?
	     */
	    ((p->op == FLDMATH_MM || p->op == FLDMATH_MMIN) &&
             p->handled && !IS_LINEAR(tup) &&
	     tup->ddic && tup->ddic->optimizations[OPTIMIZE_LIKE_HANDLED]))
#endif
	{
		f = createfld("int", 1, 0);
		putfld(f, &pone, 1);
		fopush(fo, f);
		closefld(f);
		if(p)
			p->value = 1;
		retval = 1;
		goto done;
	}
	if (p->op == AGG_FUN_OP)
	{
		if (tup == DBTBLPN) goto missingDbtbl;
		retval = pred_evalstats(tup, p, fo);
		goto done;
	}
	if (p->op == REG_FUN_OP)
	{
		fomark(fo);
	}
	else if (p->op == SUBQUERY_OP)
	{
		int subqt;

		subqt = SUBQUERY_SINGLEVAL;
		if (tup == DBTBLPN) goto missingDbtbl;
		tf1 = TXqueryfld(tup->ddic,tup,p->left,fo,subqt, 1);
		if(tf1)
		{
			fopush(fo, tf1);
			TXsetshadownonalloc(tf1);/* Need Data a while longer*/
			tf1 = closefld(tf1);
			retval = 1;
			goto done;
		}
		else
		{
			retval = -1;
			goto done;
		}
	}
	else
	{
		switch (p->lt)
		{
			case FIELD_OP :
				if(p->lat == FIELD_OP && p->altleft)
					fopush(fo, p->altleft);
				else
					fopush(fo, p->left);
				break;
			case 'P' :
				if (pred_eval(tup, p->left, fo) == -1)
				{
					retval = -1;
					goto done;
				}
				break;
			case NAME_OP :
				if (p->lat == FIELD_OP)
				{
					fopush(fo, p->altleft);
					break;
				}
				if (tup == DBTBLPN) goto missingDbtbl;
				f = dbnametofld(tup, p->left);
#ifdef NEVER
				putmsg(MINFO, "pred_eval", "Looking up %s in %lx", p->left, tup);
#endif
				if (f != NULL)
				{
					fopush(fo, f);
					p->lat = FIELD_OP;
					p->altleft = f;
				}
				else
				{
					const char	*tblMsgName;
#ifndef NO_RANK_FIELD
					if (TXisRankName(p->left))
					{
#ifndef NEVER
						/* NOTE: see TXcalcrank()
						 * comments on how we avoid
						 * infinite recursion with
						 * this TXcalcrank() call
						 * (only call here if
						 * `tup->rank' is 0):
						 */
						if(tup->rank == 0)
						{
							int nrank = 0;

							tup->rank = TXcalcrank(
							    tup,tup->pred,
							    &nrank,fo);

							if(nrank)
							tup->rank /= nrank;
						}
#endif
						tf1 = createfld((char *)TXrankColumnTypeStr, 1, 0);
						putfld(tf1, &tup->rank, 1);
						fopush(fo, tf1);
						break;
					}
#ifdef NEVER
					if(!strcmp(p->left, "$recid"))
					{
						fopush(fo, tup->frecid);
						break;
					}
#endif
#endif
					tblMsgName = tup->lname;
					if (tblMsgName && !*tblMsgName)
						tblMsgName = NULL;
					putmsg(MWARN, __FUNCTION__,
					       "Could not find field `%s'%s%s%s",
					       p->left, (tblMsgName ? " in `" : ""),
					       (tblMsgName ? tblMsgName : ""),
					       (tblMsgName ? "'": ""));
					f = createfld("int", 1, 0);
					putfld(f, &mone, 1);
					fopush(fo, f);
					closefld(f);
					p->value = -1;
					retval = -1;
					goto done;
				}
				break;
		case SUBQUERY_OP:
			{
				int subqt;

				if (p->lat == FIELD_OP)
				{
					fopush(fo, p->altleft);
					break;
				}
				switch(p->op)
				{
					case EXISTS_OP:
						subqt = SUBQUERY_EXISTSVAL;
						break;
					case FOP_IN:
					case FOP_IS_SUBSET:
					case FOP_INTERSECT:
					case FOP_INTERSECT_IS_EMPTY:
					case FOP_INTERSECT_IS_NOT_EMPTY:
						subqt = SUBQUERY_MULTIVAL;
						break;
					default:
						subqt = SUBQUERY_SINGLEVAL;
						break;
				}
				if (tup == DBTBLPN) goto missingDbtbl;
				tf1=TXqueryfld(tup->ddic,tup,p->left,fo,subqt,1);
				if(tf1)
				{
					fopush2(fo, tf1, 1);
					TXsetshadownonalloc(tf1);
				}
				else
				{
					switch(p->op)
					{
						case FOP_IN:
						case FOP_IS_SUBSET:
						case FOP_INTERSECT_IS_EMPTY:
						case FOP_INTERSECT_IS_NOT_EMPTY:
						case FOP_EQ:
							fodisc(fo);
							f = createfld("int",
									1, 0);
							putfld(f,
		(p->op == FLDMATH_INTERSECT_IS_EMPTY ? &pone : &pzero), 1);
							fopush(fo, f);
							closefld(f);
							rc = (p->op == FLDMATH_INTERSECT_IS_EMPTY);
							p->value = rc;
							retval = 1;
							goto done;
						default:
#ifdef TX_DEBUG
							putmsg(200, "Evaluate", "(%d) op on NULL subquery", p->op);
#endif
							break;
					}
				}
			}
			break;
			default:
#ifdef PEDANTIC
				putmsg(MWARN, NULL, "Unexpected case 1"):
#endif
			break;
		}
	}
	switch(p->op)
	{
		case NOT_OP :
		{
			ft_int *ti = NULL;

			r = fopeek(fo);
			if (r != NULL)
			{
				ti = getfld(r, &n);
				if(ti)
					rc = *ti;
				else
					rc = 0;
			}
			else
				rc = 0;
			rc = rc?0:1;
			if (ti != NULL)
				*ti = rc;
			p->value = rc;
			if (rc == 0)
				retval = 1;
			else
				retval = 0;
			goto done;
		}
		case EXISTS_OP :
		{
			ft_int *ti = NULL;

			r = fopeek(fo);
			if (r != NULL && (v = getfld(r, &n)) != NULL)
			{
				ti = (ft_int *)v;
				rc = *ti;
			}
			else
				rc = 0;
			rc = rc?1:0;
			if (ti != NULL)
				*ti = rc;
			p->value = rc;
			if(tf1)
				TXsetshadownonalloc(tf1);
			if (rc == 0)
				retval = 1;
			else 
				retval = 0;
			goto done;
		}
		case FOP_AND :
			r = fopeek(fo);
			if (r != NULL && (v = getfld(r, &n)) != NULL)
				rc = (int)*((ft_int *)v);
			else
				rc = 0;
			p->value = rc;
			if (rc == 0)
			{
				retval = 1;
				goto done;
			}
			break;
		case FOP_OR :
			r = fopeek(fo);
			if (r != NULL && (v = getfld(r, &n)) != NULL)
				rc = (int)*((ft_int *)v);
			else
				rc = 0;
			p->value = rc;
			if (rc != 0)
			{
				retval = 1;
				goto done;
			}
			break;
		default:
			break;
	}
	switch (p->rt)
	{
		case FIELD_OP :
			if (TXismmop(p->op, &fldOp))
			{
				DDMMAPI *ddmmapi = getfld(p->right, NULL);
				if (ddmmapi == (DDMMAPI *)NULL)
				{
					fodisc(fo);
					f = createfld("int", 1, 0);
					putfld(f, &mone, 1);
					fopush(fo, f);
					closefld(f);
					p->value = -1;
					retval = -1;
					goto done;
				}
				if (ddmmapi->qtype != FIELD_OP)
				{
					if (tup == DBTBLPN) goto missingDbtbl;
					setddmmapi(tup, ddmmapi->self, fldOp);
				}
			}
			if(p->rat == FIELD_OP && p->altright)
				fopush(fo, p->altright);
			else
				fopush(fo, p->right);
			break;
		case 'P' :
			if(pred_eval(tup, p->right, fo)==-1)
			{
				retval = -1;
				goto done;
			}
			break;
		case NAME_OP :
			if (p->rat == FIELD_OP)
			{
				fopush(fo, p->altright);
				break;
			}
			if (tup == DBTBLPN) goto missingDbtbl;
			f = dbnametofld(tup, p->right);
			if (f != NULL)
			{
				fopush(fo, f);
				p->altright = f;
				p->rat = FIELD_OP;
			}
			else
			{
				const char	*tblMsgName;
#ifndef NO_RANK_FIELD
				if (TXisRankName(p->right))
				{
					tf2 = createfld((char *)TXrankColumnTypeStr, 1, 0);
					putfld(tf2, &tup->rank, 1);
					fopush(fo, tf2);
					break;
				}
#endif
				tblMsgName = tup->lname;
				if (tblMsgName && !*tblMsgName)
					tblMsgName = NULL;
				putmsg(MWARN, __FUNCTION__,
				       "Could not find field `%s'%s%s%s",
				       p->right, (tblMsgName ? " in `" : ""),
				       (tblMsgName ? tblMsgName : ""),
				       (tblMsgName ? "'" : ""));
				fodisc(fo);
				f = createfld("int", 1, 0);
				putfld(f, &mone, 1);
				fopush(fo, f);
				closefld(f);
				p->value = -1;
				retval = -1;
				goto done;
			}
			break;
		case SUBQUERY_OP:
			{
				int subqt;
				if (p->rat == FIELD_OP)
				{
					fopush(fo, p->altright);
					break;
				}
				switch(p->op)
				{
					case EXISTS_OP:
						subqt = SUBQUERY_EXISTSVAL;
						break;
					case FOP_IN:
					case FOP_IS_SUBSET:
					case FOP_INTERSECT:
					case FOP_INTERSECT_IS_EMPTY:
					case FOP_INTERSECT_IS_NOT_EMPTY:
						subqt = SUBQUERY_MULTIVAL;
						break;
					default:
						subqt = SUBQUERY_SINGLEVAL;
						break;
				}
				if (tup == DBTBLPN) goto missingDbtbl;
				tf2=TXqueryfld(tup->ddic,tup,p->right,fo,subqt,1);
				if(tf2)
					fopush(fo, tf2);
				else
				{
					switch(p->op)
					{
						case FOP_EQ:
						case FOP_IN:
						case FOP_IS_SUBSET:
						case FOP_INTERSECT_IS_EMPTY:
						case FOP_INTERSECT_IS_NOT_EMPTY:
							fodisc(fo);
							f = createfld("int",
									1, 0);
							putfld(f,
		(p->op == FLDMATH_INTERSECT_IS_EMPTY ? &pone : &pzero), 1);
							fopush(fo, f);
							closefld(f);
							rc = (p->op == FLDMATH_INTERSECT_IS_EMPTY);
							p->value = rc;
							retval = 1;
							goto done;
						default:
#ifdef TX_DEBUG
							putmsg(200, "Evaluate", "(%d) op on NULL subquery", p->op);
#endif
							break;
					}
				}
			}
			break;
		default:
			break;
	}
	switch(p->op)
	{
		case FOP_MM :
		case FOP_MMIN :
                  /* Linear search (if IS_LINEAR()).  If LIKE is the
                   * only thing in the WHERE clause, we would not get
                   * here due to predtoiinode() setting 0 results if
                   * !allinear (if allinear is 1, we do get here, but
                   * then our check below is silent).  Since we did
                   * get here, we know that predtoiinode() approved
                   * the LIKE as indexed, but then an OR above it with
                   * an unindexed clause must have dropped the index
                   * results.  Thus this linear search is due to that
                   * other clause (Bug 6404), and we must check
                   * allinear.
                   *
                   * Could also be post-proc (if !IS_LINEAR()).  If
                   * WHERE is just a LIKE needing post-proc, then
                   * alpostproc must be true (and our check here will
                   * be silent); otherwise (!alpostproc) fdbi_get()
                   * would have yapped but then marked the index
                   * results as needing no post-processing.  But could
                   * also be a fully-indexed LIKE ANDed with a
                   * fully-indexable LIKE: second LIKE may not have
                   * used index due to first LIKE's index results
                   * being under maxlinearrows, and thus second LIKE
                   * is being run here against first LIKE's index
                   * results (post-proc): must check alpostproc here.
                   *
                   * Thus, in addition to historical linear/post-proc
                   * checks at index usage time (predtoiinode() /
                   * fdbi_get()), we also check here -- where the
                   * linear/post-proc LIKE is actually performed -- to
                   * catch any linear/post-proc done for unknown
                   * reasons (e.g. Bug 6404 reasons above), rather
                   * that assuming we'll always catch them beforehand
                   * (in predtoiinode()/fdbi_get()).
                   */
                  if (globalcp &&
                      !(IS_LINEAR(tup) ? globalcp->allinear :
                        globalcp->alpostproc) &&
                      /* Linear/post-proc LIKE ok for non-real-table
                       * use, e.g. Vortex <if> which uses SYSDUMMY:
                       */
                      tup->lname &&
                      strcmp(tup->lname, "SYSDUMMY") != 0 &&
                      tup->ddic->optimizations[OPTIMIZE_LIKE_HANDLED])
                    {
                      FLD       *queryFld;
                      DDMMAPI   *ddmmapi;
                      size_t    n;
                      char      *query = "?";

                      if ((queryFld = fopeek(fo)) != NULL &&
                          TXfldbasetype(queryFld) == FTN_CHAR &&
                          (ddmmapi = (DDMMAPI*)getfld(queryFld, &n)) != NULL &&
                          n == sizeof(DDMMAPI))
                        query = ddmmapi->query;
                      if (globalcp->denymode != API3DENYSILENT &&
                          !p->didPostProcLinearMsg)
                        {
                          putmsg((globalcp->denymode == API3DENYWARNING ?
                                  MWARN : MERR) + UGE, NULL,
		     "Query `%s' with current WHERE clauses would require %s",
                                 query, (IS_LINEAR(tup) ? "linear search" :
                                         "post-processing"));
                          /* Yap only once, not every row: */
                          p->didPostProcLinearMsg = 1;
                        }
                      /* Fail the op: */
                      closefld(fopop(fo));
		      /* Bug 7373: due to earlier fopush() copying the
		       * PRED.left FLD struct wholesale, the fopeek()
		       * FLD shadow here will also point to PRED.left's
		       * shadow, which fld2finv() may free, which
		       * may result in a double free when the LIKE
		       * QNODE's left side is closed and hence the
		       * original FLD shadow is freed (because
		       * PRED.left points to LIKE QNODE left side).
		       * So use a separate field:
		       */
                      /* if ((resultFld = fopeek(fo)) != NULL) */
                        /* fld2finv(resultFld, 0); */
		      fodisc(fo);		/* pop off top and discard */
		      f = createfld("int", 1, 0);
		      putfld(f, &pzero, 1);
		      fopush(fo, f);
		      f = closefld(f);
                      /* WTF would like to stop entire query *without*
                       * indicating error (as retval == -1 does?).
                       * predtoiinode() does via txMakeEmptyIindex();
                       * we cannot change index mid-stream here?
                       */
                      retval = -1;
                      break;
                    }
                  /* fall through and do the Metamorph op */
		case FOP_ADD :
		case FOP_SUB :
		case FOP_MUL :
		case FOP_DIV :
		case FOP_MOD :
		case FOP_EQ :
		case FOP_LT :
		case FOP_LTE :
		case FOP_GT :
		case FOP_GTE :
		case FOP_NEQ :
		case FOP_NMM :
		case FOP_RELEV :
		case FOP_PROXIM :
		case FOP_CNV :
		case FOP_IN :
		case FOP_IS_SUBSET :
		case FOP_INTERSECT :
		case FOP_INTERSECT_IS_EMPTY :
		case FOP_INTERSECT_IS_NOT_EMPTY :
		case FOP_MAT :
		case FOP_TWIXT :
			rc2 = foop2(fo, p->op, p->resultfld, &p->fldmathfunc);
#ifdef DEBUG
			if(rc2 != 0)
				DBGMSG(2,(999, "foop", "Error %d, Op %d", rc2, p->op));
#endif
			break;
		case FOP_AND :
			/* Bug 4292: value of some AND children (e.g.
			 * indexed LIKE post-process via
			 * metamorphop()) can be rank values instead
			 * of 1/0: used to use FOP_MUL for AND here,
			 * but repeat multiplication can overflow,
			 * causing erroneous error (negative) value
			 * return.  Emulate MIN(a, b) instead, to
			 * avoid overflow yet retain a rank value for
			 * potential `tup->rank' use below:
			 */
			if ((r = fopeek2(fo)) != NULL &&
			    (r2 = fopeek(fo)) != NULL &&
			    ((r->type & DDTYPEBITS) == FTN_INT) &&
			    ((r2->type & DDTYPEBITS) == FTN_INT) &&
			    (intVal = (ft_int *)getfld(r, &n)) != NULL &&
			    (intVal2 = (ft_int *)getfld(r2, &n2)) != NULL &&
			    n > (size_t)0 &&
			    n2 > (size_t)0)
			{
				if (!*intVal || !*intVal2)
					rc2 = 0;
				else
					rc2 = TX_MIN(*intVal, *intVal2);
				n = 1;
			}
			else
				n = 0;
			foop(fo, FOP_MUL);
			if (n && (r = fopeek(fo)) != NULL)
				fld2finv(r, rc2);
			break;
		case FOP_OR :
			/* Bug 4292: emulate MAX(a, b) instead of
			 * potentially-overflowing FOP_ADD, for OR:
			 */
			if ((r = fopeek2(fo)) != NULL &&
			    (r2 = fopeek(fo)) != NULL &&
			    ((r->type & DDTYPEBITS) == FTN_INT) &&
			    ((r2->type & DDTYPEBITS) == FTN_INT) &&
			    (intVal = (ft_int *)getfld(r, &n)) != NULL &&
			    (intVal2 = (ft_int *)getfld(r2, &n2)) != NULL &&
			    n > (size_t)0 &&
			    n2 > (size_t)0)
			{
				rc2 = TX_MAX(*intVal, *intVal2);
				n = 1;
			}
			else
				n = 0;
			/* Bug 5395: NULL OR TRUE is TRUE, not NULL; but
			 * NULL + 1 gives us NULL, which is taken as FALSE.
			 * (TRUE OR NULL squeaks by due to short-circuit,
			 * so we never get here).  Treat that NULL as 0:
			 */
			if (r && r2 &&
			    TXfldIsNull(r))	/* LHS is NULL */
			{
				FLD	*popFld;

				/* Since 0 + N == N, just discard the NULL
				 * and skip the FOP_ADD:
				 */
				/* WTF `select 1 x where ('all'
				 * matches convert('foo,bar',
				 * 'strlst', ',') or 1 = 1)' will leak
				 * the popped FLD here, but `r2->v'
				 * shares the same `v', so closefld(popFld)
				 * would free `r2->v' flummoxing later
				 * access in `fo' stack after
				 * `fopush(fo, r2)'?  Texis test658 Valgrind.
				 * Compromise: just free FLD struct; WTF
				 */
				popFld = fopop(fo);  /* pop off `r2' (RHS) */
				if (popFld != r2) TXfree(popFld);
				popFld = NULL;
				fodisc(fo);	/* discard `r' (LHS, NULL) */
				r = NULL;
				fopush(fo, r2);
			}
			else
				foop(fo, FOP_ADD);
			if (n && (r = fopeek(fo)) != NULL)
				fld2finv(r, rc2);
			break;
		case REG_FUN_OP:
		case AGG_FUN_OP:
			if (p->lt == NAME_OP)
			{
				/* NOTE: see also TXvxIsPredicateCompilable:*/
				if(!strcmp(p->left, "abstract") &&
				   fonmark(fo) == 4)
				{
					FLD *ft;
					if (tup == DBTBLPN)
					{
					missingDbtbl:
						putmsg(MERR + UGE, __FUNCTION__,
					    "Internal error: Missing DBTBL");
						goto err;
					}
					ft = createfld("varbyte", 1, 0);
					putfld(ft, tup, sizeof(DBTBL));
					fopush(fo, ft);
					closefld(ft);
				}
				if (forev(fo)< 0)
				{
					putmsg(MWARN, __FUNCTION__,
		"Could not prepare stack for call to SQL function `%s'",
						p->left);
					goto errRet;
				}
				if (focall(fo, p->left, TXPMBUFPN) < 0)
				{
					/* focall() already yapped */
				errRet:
					f = createfld("int", 1, 0);
					putfld(f, &mone, 1);
					fopush(fo, f);
					closefld(f);
				err:
					p->value = -1;
					retval = -1;
					goto done;
				}
			}
			break;
		case LIST_OP:
		case RENAME_OP:
			break;
		default: /* How would this happen? */
			if (p->op == QNODE_OP_UNKNOWN) break;  /* wtf SET value */
			putmsg(MERR, __FUNCTION__,
			       "Internal error: Unknown FOP %d (%s?) ignored",
			       (int)p->op, TXqnodeOpToStr(p->op, NULL, 0));
			break;
	}
#ifndef NO_CACHE_CONSTANT_CONVERSION
	TXcacheconv(p, fo);
#endif
	r = fopeek(fo);
#ifdef NEVER
#endif
	if (r != NULL
#ifndef NO_EVAL_INT_ONLY
		&& r->type == FTN_INT
#endif
	)
	{
		ft_int	*ip;
		ip = (ft_int *)getfld(r, &n);
		if (ip)
		{
			rc = *ip;
			if((p->op & FOP_CMP) && !p->resultfld)
				p->resultfld = dupfld(r);
			if (globalcp == APICPPN) globalcp = TXopenapicp();
			if(p->handled &&
			   (globalcp->stringcomparemode & TXCFF_PREFIX) &&
			   (p->op & FOP_CMP) && rc == TX_STRFOLDCMP_ISPREFIX)
			{
				rc = 1;
				p->handled = 0;
			}
		}
		else
			rc = 0;
	}
	else
		rc = 0;
	p->value = rc;

        /* Save rank from metamorphop(), if available and we're doing
         * LIKE/LIKEIN.  This will possibly replace a rank value from
         * the index (since we're doing a post-search here): -KNG 980701
         */
        if ((p->op == FLDMATH_MM || p->op == FLDMATH_MMIN) &&
	    rc > 1 && tup != DBTBLPN)
          tup->rank = rc;

done:
	if(tf1)
		closefld(tf1);
	if(tf2)
		closefld(tf2);
#ifdef DEBUG
	DBGMSG(9,(999, NULL, "%s Result = %lx", disppred(p, 0, 0), rc));
#endif
	return retval;
#undef IS_LINEAR
}

/****************************************************************************/

int
tup_match(tup, p, fo)
DBTBL *tup;
PRED *p;
FLDOP *fo;
{
	static CONST char	Fn[] = "tup_match";
	FLD *r;
	size_t n;
	int rc = -1;

	if (p == (PRED *)NULL)
	{
		rc = 1;
		goto done;
	}
#ifdef NEVER
	putmsg(999, Fn, "Predicate is %s", disppred(p, 0, 0));
#endif
#ifdef DEBUG
	DBGMSG(9,(999, Fn, "Predicate is %s", disppred(p, 0, 0)));
#endif
	TXpredClear(p, 0);
	if(pred_eval(tup, p, fo) == -1)
	{
		rc = -1;
		goto done;
	}
	r = fopeek(fo);
	if (r != (FLD *)NULL)
	{
		ft_int	*ip;
#ifdef RECID_HACK
		if(getfld(r, NULL) == NULL)
		{
			fodisc(fo);
			rc = 1;
			goto done;
		}
#endif
		ip = (ft_int *)getfld(r, &n);
		if (ip)
		{
			rc = *ip;
		}
		else
			rc = 0;
		fodisc(fo);
	}
done:
#if defined(DEBUG)
	DBGMSG(9,(999, Fn, "Predicate is %s, Result is %d", TXdisppred(p, 0, 0, 128), rc));
#endif
	if (TXverbosity > 2)
		putmsg(MINFO, Fn, "Table `%s' recid 0x%wx %s predicate %p",
		       tup->lname, (EPI_HUGEINT)TXgetoff(&tup->recid),
		       (rc > 0 ? "matches" : (rc == 0 ? "does not match" :
					       "does not match (error)")),
		       p);
	return rc;

}

/****************************************************************************/

void *
evalpred(t, p, fo, sz, type)
DBTBL	*t;
PRED	*p;
FLDOP	*fo;
size_t	*sz;	/* (out) number of elements in returned data */
FTN	*type;	/* (out, opt.) type of returned data */
/* Evaluates predicate `p', returning result as alloc'd FLD data,
 * or NULL if NULL result (Bug 5395).
 * Sets `*sz' to number of elements in returned data, and `*type' to FTN type.
 */
{
	FLD *r = NULL;
	void *rc = (void *)NULL, *v;
	int pe;
	int needdisc = 0;

	if(p->op == 0 && p->rt == 0)
	{
		if(p->lat == FIELD_OP)
		{
			r = p->altleft;
		}
		if(!r && p->lt == FIELD_OP)
		{
			r = p->left;
		}
	}
	if(!r)
	{
		if((pe=pred_eval(t, p, fo))==-1)
		{
			fodisc(fo);
			if (type) *type = (FTN)0;
			return rc;
		}
		needdisc = 1;
		r = fopeek(fo);
	}
	if (r != (FLD *)NULL)
	{
		if (r->type == FTN_COUNTERI)
		{
			v = getcounter(t->ddic);
			if (type) *type = FTN_COUNTER;
			*sz = 1;
			/* Bug 4646: `v' is already alloc'd: */
			rc = v;
		}
		else
		{
			v = getfld(r, sz);
			if (type) *type = r->type;
			if(!v)
				return rc;
			rc = TXftnDupData(v, *sz, r->type, *sz*r->elsz, NULL);
		}
		if(needdisc)
			fodisc(fo);
#ifdef NEVER
		*sz = n;
#endif
	}
	else if (type) *type = (FTN)0;
	return rc;
}

