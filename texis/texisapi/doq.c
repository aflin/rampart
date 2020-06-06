/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"
#include "queue.h"

#undef NO_DEL_TMP_JOIN

static int	verbose = 0;
/* static char	tempbuf[8192]; */

#define BTCSIZE TXbtreecache

/******************************************************************/

int
TXshowactions(v)
int	v;
{
	int	ov;

	ov = verbose;
	verbose = v;
	return ov;
}

/******************************************************************/
/*	Process the query.
 */

int
TXprocessquery(query, fo)
QNODE	*query;	/* (in/out) update row count stats */
FLDOP	*fo;
{
	DBTBL	*tup;
	DBTBL 	*tb1, *tb2;
	int	rc=0;
        QUERY	*q = query->q;

	switch (q->op)
	{
		case Q_SELECT:
#ifndef OLD_ORDERBY
#else
			if(!tup_write(q->out, q->in1, fo, 0))
				return -1;
			dostats(q->out, fo);
#endif
			break;
		case Q_PROJECT:
			if(q->proj->p_type == PROJ_SINGLE_END)
			{
				rc = -1;
				break;
			}
			tb1 = q->in1;
			tb2 = q->out;
			tup = tb1;
			if(tup)
				rc = tup_project(tup, tb2, q->proj, fo);
			else if (q->proj->p_type == PROJ_SINGLE)
			{
				q->proj->p_type = PROJ_AGG_CALCED;
				rc = tup_project(tb1, tb2, q->proj, fo);
				q->proj->p_type = PROJ_SINGLE_END;
			}
			else
				rc = -1;
			break;
		case Q_CPRODUCT:
			tup_product(query, q->in1, q->in2, q->out, q->pred,
				    q->proj, fo);
			break;
		case Q_UNION:
			tb1 = q->in1;
			tb2 = q->in2;
			tup_union(tb1, tb2, q->out, fo);
			break;
		case Q_APPEND:
			tb2 = q->in2;
			if (q->tr_before)
				trigexec(q->tr_before, tb2, fo);
			if(q->out!=NULL)
				rc = tup_append(tb2, q->out, fo);
			if (rc == 0 && q->tr_after)
				trigexec(q->tr_after, tb2, fo);
			break;
		case Q_DELETE:
			tb1 = q->in1;
#ifdef NEVER
			if((rc=getindexes(tb1))==-1)
				break;
#endif
			tup = tb1;
			if (q->tr_before)
				trigexec(q->tr_before, tb1, fo);
			if (tup)
			{
				rc = tup_delete(tup, tb1);
			}
#ifdef NEVER
			else
			{
				DebugBreak();
			}
#endif
			if(rc == 0)
			{
				if (q->tr_after)
					trigexec(q->tr_after, NULL, fo);
			}
#ifdef NEVER
			else
			{
				DebugBreak();
			}
#endif
			break;
		case Q_UPDATE:
#ifndef NEVER
			rc = procupd(q, fo);
#else /* NEVER */
                        error; /* KNG 971111 deleted */
#endif /* NEVER */
			break;
		default:
			break;
	}
	q->state = QS_PCOMMIT;
	return rc;
}

/******************************************************************/

QNODE *
openqnode(op)
QNODE_OP	op;
/* Creates new QNODE with operand `op'.
 * Returns NULL on error.
 */
{
	static CONST char	fn[] = "openqnode";
	QNODE			*query;

#ifdef RETURN_NULL_FOR_UNKNOWN
	if(op == QNODE_OP_UNKNOWN)
	{
		return(QNODEPN);
	}
#endif
	if (!(query = (QNODE *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(QNODE))))
		return(QNODEPN);
	query->op = op;
	TX_CLEAR_COUNTINFO(&query->countInfo);
	/* rest cleared by calloc() */
	return(query);
}

QNODE *
closeqnode(query)
QNODE *query;
{
	FLD	*fld;
	int	rc;

	if (query == (QNODE *)NULL)
		return query;
	if (query->op == UPD_SEL_OP && query->left->op == SELECT_OP)
		rc = 0;
	else
		rc = 1;
	if (query->right != (QNODE *)NULL)
	{
		if (query->op != TABLE_AS_OP)
			query->right = closeqnode(query->right);
		else
			free(query->right);
	}
	if (query->left != (QNODE *)NULL)
	{
		if (query->op == TABLE_OP)
			closedd((DD *)query->left);
		else if(query->op != PARAM_OP)
			query->left = closeqnode(query->left);
	}
	if (query->org != QNODEPN)
		query->org = closeqnode(query->org);
#if 1
	if (query->tname != (char *)NULL)
	{
		if (query->op == FIELD_OP)
		{
			fld = (FLD *)query->tname;
			freeflddata(fld);
			closefld(fld);
		}
		else if (query->op == UPD_SEL_OP)
		{
			closeqnode(query->tname);
		}
		else if (query->op == PARAM_OP)
		{
			PARAM	*p;

			p = query->tname;
#ifdef NEVER
			if (p->fld)
			{
				freeflddata(p->fld);
				closefld(p->fld);
			}
			free(p);
#endif /* NEVER */
		}
		else
			free(query->tname);
	}
#endif /* 1 */
	if (query->q != (QUERY *)NULL)
		closequery(query->q);
#ifdef JMT_TEST
	_freelst(query->fldlist);
	_freelst(query->afldlist);
#endif
#ifndef NO_ANALYZE
	if(query->fldlist) slclose(query->fldlist);
	if(query->afldlist) slclose(query->afldlist);
#endif
	free(query);
	return (QNODE *)NULL;
}

/******************************************************************/

int TXquitqnode ARGS((QNODE *));

int
TXquitqnode(query)
QNODE *query;
{
	if (query == (QNODE *)NULL)
		return -1;
	if (query->right != (QNODE *)NULL && query->op != TABLE_AS_OP)
			TXquitqnode(query->right);
	if (query->left != (QNODE *)NULL && query->op != TABLE_OP)
			TXquitqnode(query->left);
	if (query->tname != (char *)NULL && query->op == UPD_SEL_OP)
			TXquitqnode(query->tname);
	return 0;
}

/******************************************************************/

int
TXunpreparetree(query)
QNODE *query;
/* Returns non-zero if this sub-tree contains any parameters whose
 * set/unset status (eg. param-o-rama) is known to have changed
 * since the last execute.
 */
{
	int	rc = 0;
	PARAM	*p;
	QNODE	*qn;

	if (query == (QNODE *)NULL)
		return 0;
	if (query->right != (QNODE *)NULL)
		if (query->op != TABLE_AS_OP)
		{
			/* KNG 20071107 Bug 1961 fixup; restore original
			 * node so convqnodetovarfld() can re-convert
			 * with new parameters.  wtf better way?
			 */
			if (query->right->org != QNODEPN)
			{
				qn = query->right->org;	/* save before close */
				query->right->org = QNODEPN;
				closeqnode(query->right);
				query->right = qn;
			}

			if (TXunpreparetree(query->right)) rc = 1;
		}
	if (query->left != (QNODE *)NULL)
		if (query->op != TABLE_OP && query->op != PARAM_OP)
		{
			/* KNG 20071107 Bug 1961 fixup; wtf better way: */
			if (query->left->org != QNODEPN)
			{
				qn = query->left->org;	/* save before close */
				query->left->org = QNODEPN;
				closeqnode(query->left);
				query->left = qn;
			}

			if (TXunpreparetree(query->left)) rc = 1;
		}
	if (query->q != (QUERY *)NULL)
	{
		closequery(query->q);
		query->q = (QUERY *)NULL;
	}
	query->state = -1;
	query->ordered = 0;
	if (query->op == PARAM_OP)			/* KNG 011210 */
	{
		p = (PARAM *)query->tname;
		/* KNG `prevneeddata' may be inaccurate the first time,
		 * but doesn't matter for `analyzed' reset below, since
		 * `analyzed' will be 0 anyway:
		 */
		if (!p || (p->prevneeddata ^ p->needdata)) rc = 1;
	}
	/* KNG 011210 need to (re-)analyze if parameters have changed
	 * since last execute (with no preptx).  Used to just check
	 * TXgetDiscardUnsetParameterClauses() for this, since params
	 * can't change otherwise.  But params might still be the same
	 * even if TXgetDiscardUnsetParameterClauses() is true, so
	 * optimize and only clear `analyzed' if we *know* params
	 * changed:
	 */
	if (rc)
		query->analyzed = 0;
	/* WTF someday don't just propagate `rc' all the way up: */
	return rc;
}

/******************************************************************/
/*	WTF - This needs to look at the predicates, and determine
 *	if they are the same
 *
 *	Returns 1 if the same, 0 if different
 */

int
TXpredcmp(a, b)
PRED	*a;
PRED	*b;
{
	char	*sa, *sb;
	int	rc;

	if (!a || !b)
		return 0;
	if (a == b)				/* KNG 20100820 optimize */
		return(1);
	sa = TXdisppred(a, 0, 0, 0);
	sb = TXdisppred(b, 0, 0, 0);

	rc = strcmp(sa, sb);

	if (sa)	free(sa);
	if (sb) free(sb);

	return rc?0:1;
}

/******************************************************************/
/*
	Should check if b is at least as good as a.
*/

int projcmp ARGS((PROJ *, PROJ *));

int
projcmp(a, b)
PROJ	*a;
PROJ	*b;
{
	int	i;

	if (!a || !b)
		return 0;
	if (a->n > b->n) /* Must be different if a is more exacting */
		return 0;
	for (i=0; i < a->n; i++)
		if (!TXpredcmp(a->preds[i], b->preds[i]))
			return 0;
	return 1;
}

/******************************************************************/

int
TXdeltmprow(tbl)
DBTBL	*tbl;
{
	PRECID	curloc;
	int	rc, i;
	FLD	*fin;
	size_t	alloced;

	curloc = telltbl(tbl->tbl);
	if(TXgetoff(curloc) && !tbl->rname)
	{
#ifdef DEBUG
		DBGMSG(9,(999, NULL, "Deleting temp row"));
#endif
		if(TXverbosity > 1)
			putmsg(MINFO, NULL, "Deleting temp row");
		rc = deltblrow(tbl->tbl, curloc);
		for(i=0; ((fin = getfldn(tbl->tbl, i, NULL)) != NULL); i++)
		{
			if(fin->v == fin->shadow)
			{
				alloced = fin->alloced;	/* save before free */
				TXfreefldshadow(fin);
				if (alloced > (size_t)0)
					setfld(fin, calloc(1, alloced), alloced);
			}
		}
		return rc;
	}
	return 0;
}

/******************************************************************/

static int havegroup ARGS((QNODE *));
static int
havegroup(query)
QNODE	*query;
{
	static CONST char Fn[] = "havegroup";

	if(!query)
		return 0;
	switch(query->op)
	{
		case NAME_OP:
		case PRODUCT_OP:
			return 0;
		case GROUP_BY_OP:
			return 1;
		case PROJECT_OP:
		case SELECT_OP:
		case FOP_PROXIM:
		case RENAME_OP:
		case HINT_OP:
			return havegroup(query->left);
		case ORDER_OP:
			return havegroup(query->right);
		case SUBQUERY_OP:
			return 0;
		default:
			putmsg(MERR, Fn, "Unknown op %s",
			       TXqnodeOpToStr(query->op, NULL, 0));
			return 0;
	}
}

/******************************************************************/
/* Perform the operations requested in query */

int
TXdotree(query, fo, direction, offset)
QNODE *query;
FLDOP *fo;
int direction;
int offset;
/* Returns -1 at EOF (?), else?
 */
{
	static CONST char Fn[]="TXdotree";
	QUERY *q=query->q;
	DBTBL *rc;
	int	r, res;
	int	readanother;
	int	toskip = 0, skipped;
	TXCOUNTINFO	*countInfo = &query->countInfo;
	static  int found = 0;

	if(query->quitnow)
		return -2;

	/* if query has a ddic, use it as the global default.  It's ok if we
	 * can't find one (action on autocreate that doesn't exist yet, etc.) */
	if(q && q->out && q->out->ddic)
	{
		if(TXusestddic(q->out->ddic)!=0)
			putmsg(MERR, Fn, "TXusestddic failed for q->out->ddic=%i", q->out->ddic);
	}
	else
	{
		if(q && q->in1 && q->in1->ddic)
		{
			if(TXusestddic(q->in1->ddic)!=0)
				putmsg(MERR, Fn, "TXusestddic failed for q->in1->ddic=%i", q->in1->ddic);
		}
		else
		{
			if(q && q->in2 && q->in2->ddic)
			{
				if(TXusestddic(q->in2->ddic)!=0)
					putmsg(MERR, Fn, "TXusestddic failed for q->in2->ddic=%i", q->in2->ddic);
			}
		}
	}

	switch(direction)
	{
		case SQL_FETCH_RELATIVE:
			if(offset == 0)	return -1;
			if(offset > 0)	toskip = offset - 1;
			else		return -1;
	}

	switch (query->op)
	{

/*
 *	We are reading straight from the table.  No further operations.
 */
		case NAME_OP :
			return TXnode_table_exec(query, fo, direction, offset, verbose);
/*
 *	We are renaming the underlying table.
 */
		case RENAME_OP :
			return TXnode_rename_exec(query, fo, direction, offset, verbose);
/*
 *	Joining two tables.
 */
		case PRODUCT_OP :
			return TXnode_join_exec(query, fo, direction, offset, verbose);

/*
 *	We have a hinted table.
 */
		case HINT_OP :
			return TXnode_hint_exec(query, fo, direction, offset, verbose);
		case TABLE_AS_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table create");
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
			do
			{
				r = TXdotree(query->left,fo,SQL_FETCH_NEXT,1);
				if(!r && (q->in1 != q->out))
					tup_write(q->out, q->in1, fo, 0);
			} while (!r && (toskip-- > 0));
			q->nrows = query->left->q->nrows;
			return r;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * We are ordering the rows coming to us.
 * Order the right QNODE rows according to the left QNODE ordering.
 * `q->proj' should also be the ORDER BY ordering?
 */
		case ORDER_OP :
		case ORDERNUM_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling an order");
		{
			RECID *where ;
			int   rc;

			/* If the right side's rows are already in the
			 * desired order, we're ordered:
			 */
			if (query->state != QS_ACTIVE)
			{			/* still prepping */
				if (query->right->op == FOP_PROXIM &&
				    projcmp(q->proj,
					    query->right->q->in1->order))
					query->ordered ++;
				if (query->right->op == SELECT_OP &&
				    projcmp(q->proj,
					    query->right->q->in1->order))
					query->ordered ++;
				if (query->right->op == NAME_OP &&
				    projcmp(q->proj,
					    query->right->q->out->order))
					query->ordered ++;
			}

			/* If it's ordered (e.g. SELECT ... ORDER BY
			 * an indexed field), we're done; just copy
			 * next ordered row to output `q->out':
			 */
			if (query->ordered)	/* Don't do much work */
			{
				int xrc;

				if(query->right->op != GROUP_BY_OP &&
				   query->right->op != DISTINCT_OP)
				TXdeltmprow(q->in1);
				res = TXdotree(query->right, fo, direction,
					       offset);
				/* KNG 20081211 propagate row counts up: */
				query->countInfo = query->right->countInfo;
				if (res == -1)
				{
					q->nrows = query->right->q->nrows;
					return -1;
				}
				q->nrows += toskip + 1;
				query->state = QS_ACTIVE;
				xrc = tup_write(q->out, q->in1, fo, 0)?1:0;
				/* ??? Why needed? Never read, just written: */
				TXaddstatrow(q->out->nfldstat, q->out, fo);
				return xrc;
			}

			/* It's not already in order.  If still prepping,
			 * we haven't done the sort, so do it:
			 */
			if (query->state != QS_ACTIVE)
			{			/* still prepping */
				FLD	*tf=NULL;
				int	i;

				while (TXdotree(query->right, fo,
						SQL_FETCH_NEXT, 1) == 0)
				{	/* while reading a row to `q->in1' */
                                  RECID *index;
                                  RECID index2;

                                  index = &index2;
                                  if(q->in1->rname)
                                    index2 = q->in1->recid;
                                  else
                                    TXsetrecid(index,telldbf(q->in1->tbl->df));
                                  if (!TXrecidvalid(index))
                                    break;
				  /* Put `q->in1' tuple into `q->in2'
				   * (ORDER BY) index.  Also sets
				   * `q->in2' $rank = `q->in1->rank':
				   */
                                  tup_index(q->in1, q->in2, q->proj,
                                            fo, index);
				  /* Check for oversize RAM DBF: */
                                  if (ioctldbtbl(q->in1, RDBF_TOOBIG, NULL)>0)
                                    {
                                      BTREE	*bt2;
                                      BTREE	*bt;

                                      ioctldbtbl(q->in1, DBF_MAKE_FILE, NULL);
                                      bt = q->in2->index.btree;
                                      bt2 = openbtree(NULL,
					 BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
                                      ioctldbf(bt2->dbf, DBF_MAKE_FILE, NULL);
                                      btreesetdd(bt2, btreegetdd(bt));
                                      /* KNG 20100706 Bug 3210: Linear
                                       * ORDER BY was not using
                                       * stringcomparemode:
                                       */
                                      BTPARAM_INIT_TO_PROCESS_DEFAULTS(
						  &bt2->params, q->in1->ddic);

                                      bt2->usr = bt->usr;
                                      bt2->cmp = bt->cmp;
                                      closebtree(bt);
                                      q->in2->index.btree = bt2;
                                      for(i=0; i < q->proj->n; i++)
                                        pred_rmalts(q->proj->preds[i]);
                                      TXrewinddbtbl(q->in1);
                                      while ((index = getdbtblrow(q->in1)),
                                             TXrecidvalid(index))
                                        tup_index(q->in1, q->in2, q->proj, fo,
                                                  index);
                                    }
                                  if(ioctldbtbl(q->in2, RDBF_TOOBIG, NULL) > 0)
                                    {
                                    }
				}

				/* We read all rows (and sorted w/tup_index).
				 * Clean up:
				 */
				/* KNG 20081211 propagate row counts up: */
				query->countInfo = query->right->countInfo;
				for(i=0,tf=TXgetrfldn(q->in2->tbl, i, NULL);
					tf; i++)
				{
					freeflddata(tf);
					tf = TXgetrfldn(q->in2->tbl, i, NULL);
				}
				/* We are now active (returning rows): */
				query->state = QS_ACTIVE;
				q->state = QS_ACTIVE;
				TXrewinddbtbl(q->out);
				TXrewinddbtbl(q->in2);

				if (TXtraceIndexBits & 0x30000)
				{
				  DBTBL	*dbtbl = q->in2;
				  DD	*dd;
				  BTREE	*bt = NULL;
                                  char  *indexSchema = NULL;

				  if (dbtbl->type == 'B' &&
				      (bt = dbtbl->index.btree) != NULL)
					  dd = btreegetdd(bt);
				  else
					  dd = dbtbl->tbl->dd;
				  indexSchema = TXddSchemaToStr(dd, 2);
				  if (TXtraceIndexBits & 0x30000)
					  putmsg(MINFO, __FUNCTION__,
			     "Sorted records into B-tree for ORDER BY %s%s%s",
					     indexSchema,
					     (bt ? "" : " (B-tree missing?)"),
					     ((TXtraceIndexBits & 0x20000) ?
					      ":" : ""));
				  indexSchema = TXfree(indexSchema);
				  if ((TXtraceIndexBits & 0x20000) && bt)
					  TXbtreeDump(TXPMBUFPN, bt, 2, 1);
                                }
			}

			/* We're QS_ACTIVE (past prep, returning rows).
			 * Get next in-order recid `where' to return,
			 * skipping first if needed:
			 */
			do where = getdbtblrow(q->in2); /*from ORDER BY idx*/
			while (TXrecidvalid(where) &&
                               (toskip-- > 0) && ++(q->nrows));
			if (!TXrecidvalid(where))
				return -1;

			/* Get row `where' from `q->in1' (source table): */
			if(TXlocktable(q->in1, R_LCK) == -1)
			{
                          if (texispeekerr(q->in1->ddic) ==
                              MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
					return -2;
				return -1;
			}
			/* was ASSERT_DEL_BLOCK code here */
			rc = gettblrow(q->in1->tbl, where)?1:-1;

			/* Copy `q->in1' row to `q->out', including
			 * rank from `q->in2':
			 */
			{
				RECID *index;
				FLD   *indexRankFld, *outRankFld;

				indexRankFld = dbnametofld(q->in2,
						  (char *)TXrankColumnName);
				/* Write `q->in1' tuple to `q->out': */
				index = tup_write(q->out, q->in1, fo, 0);
				rc = gettblrow(q->out->tbl, index)?1:-1;/*?*/
				/* Copy $rank from `q->in2' too, if there: */
				if (indexRankFld)
				{
					q->out->rank = *(TXrankColumnType *)
						getfld(indexRankFld, NULL);
					outRankFld = dbnametofld(q->out,
						    (char *)TXrankColumnName);
					if (outRankFld)
						_fldcopy(indexRankFld, NULL,
							 outRankFld,NULL,fo);
				}
				if(rc == 1)
					dostats(q->out, fo);
			}
			TXunlocktable(q->in1, R_LCK);
			if (rc != -1)
				q->nrows += 1;
			return rc;
		}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *	We are grouping the rows coming to us.
 */
		case GROUP_BY_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a group by");
		{
			int rc, lrc = -1;

			rc = groupbysetup(query, fo);
			if (rc == -1)
				return -1;
			do
			{
groupagain:
				rc = groupby(query, fo);
				if(rc >= 0 && (toskip > 0))
				{
					lrc = rc;
					continue;
				}
				if(rc < 0 && lrc >= 0 && (toskip-- > 0))
				{
					lrc = rc;
					rc = 0;
					goto groupagain;
				}
			}
			while (rc >= 0 && toskip > 0);
			return rc;
		}
		case DISTINCT_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a distinct");
		{
			int rc;

			if(TXdistinctsetup(query, fo) == -1)
				return -1;
			do rc = TXdistinct(query, fo);
			while (rc == 0 && (toskip -- > 0));
			return rc;
		}

/*
 *	Union two sets of results.
 *	WTF!!  This just does one query, and then the next.
 *	       Supposed to remove dupes.
 */
		case UNION_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table union");
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
			do r = TXdotree(query->left, fo, SQL_FETCH_NEXT, 1);
			while (r == 0 && (toskip-- > 0));
			if(r < 0)
			{
				do r=TXdotree(query->right,fo,SQL_FETCH_NEXT,1);
				while (r == 0 && (toskip-- > 0));
				if(r >= 0)
					tup_write(q->out, q->in2, fo, 0);
			}
			else
			{
				tup_write(q->out, q->in1, fo, 0);
			}
			query->state = q->state;
			if (query->state == QS_PCOMMIT)
				TXrewinddbtbl(q->out);
			return r;

/*
 *	Select a subset of the columns in the result set
 *	to be passed up to the next level.
 */
		case PROJECT_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table project");
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
			if (q->proj->p_type == PROJ_SINGLE_END) /* Perform cleanup */
			{
				if(havegroup(query))
					q->proj->p_type = PROJ_AGG_END;
				else
					q->proj->p_type = PROJ_CLEANUP;
			}
projcleanup:
			if (q->proj->p_type == PROJ_CLEANUP) /* Perform cleanup */
			{
				FLD *fout;
				unsigned i;

				for (i=0;i<q->out->tbl->n;i++)
				{
					fout = q->out->tbl->field[i];
/*
					freeflddata(fout);
*/
				}
				return -1;
			}
			do {
				if(query->left->op != GROUP_BY_OP &&
				   query->left->op != DISTINCT_OP)
					TXdeltmprow(q->in1);
				r = TXdotree(query->left,fo,direction,offset);
				/* KNG 20081211 propagate row counts up: */
				query->countInfo = query->left->countInfo;
				if (r == -1 && q->proj->p_type == PROJ_AGG)
					break;
				if (r == -1 && q->proj->p_type == PROJ_AGG_END)
				{
					q->proj->p_type = PROJ_CLEANUP;
					goto projcleanup;
				}
				if(q->proj->p_type == PROJ_AGG_END)
					q->proj->p_type = PROJ_AGG_DONE;
				if (r == -1 && (q->proj->p_type == PROJ_AGG_DONE || q->proj->p_type == PROJ_SINGLE))
				{
#ifdef NEVER
					if(query->left->q->nrows > 0 || q->proj->p_type == PROJ_SINGLE)
#else
					if(1)
#endif
						q->proj->p_type = PROJ_AGG_CALCED;
					else
						break;
				}
				r = TXprocessquery(query, fo);
				if (q->proj->p_type == PROJ_AGG_CALCED)
					q->proj->p_type = PROJ_SINGLE_END;
				if (q->proj->p_type == PROJ_AGG_DONE)
					query->left->readanother = 1;
			} while ((q->proj->p_type == PROJ_SINGLE) || (q->proj->p_type == PROJ_AGG_DONE));
			query->state = q->state;
			if (r == -1)
			{
				FLD *fout;
				unsigned i;

				for (i=0;i<q->out->tbl->n;i++)
				{
					fout = q->out->tbl->field[i];
					freeflddata(fout);
				}
				if(query->left && query->left->q)
					q->nrows = query->left->q->nrows;
			}
			else
				q->nrows += (toskip + 1);
			return r;
		case SELECT_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table select");
			if (query->left->op == PRODUCT_OP)
			{
				if(TXproductsetup(query->left, q, fo) == -1)
					return -1;
				do
				{
					r = TXproduct(query->left, q, fo);
					if (r == 0)
					{
						++q->nrows;
						if (!TX_ISVALIDCOUNT(countInfo->rowsMatchedMin))
							countInfo->rowsMatchedMin = 0;
						countInfo->rowsMatchedMin++;
						if (!TX_ISVALIDCOUNT(countInfo->rowsReturnedMin))
							countInfo->rowsReturnedMin = 0;
						countInfo->rowsReturnedMin++;
						countInfo->indexCount =
							query->left->countInfo.indexCount;
					}
					else if (r == -1)	/* EOF? */
					{
						/* At EOF; now know max: */
						countInfo->rowsMatchedMax =
							countInfo->rowsMatchedMin;
						countInfo->rowsReturnedMax =
							countInfo->rowsReturnedMin;
					}
				}
				while (r == 0 && (toskip-- > 0));
				if(r == -1)
					return -1;
				query->state = q->state;
				query->left->state = q->state;
				return r;
			}
			else
			{
				query->state = QS_ACTIVE;
				q->state = QS_ACTIVE;
			}
			do {
#ifdef NEVER /* WTF */
				if(query->left->op != GROUP_BY_OP &&
				   query->left->op != DISTINCT_OP)
				TXdeltmprow(q->in1);
#endif
#ifdef NEVER
				if(query->left->op != DISTINCT_OP)
					TXdeltmprow(q->in1);
#endif
				if(query->left->op != NAME_OP && q->in1->pred)
				{
					r = TXdotree(query->left, fo,
						SQL_FETCH_NEXT, 1);
				}
				else
				{
					/* KNG 20081211
					 * SELECT node has the index count
					 * (if any); copy to NAME node (left)
					 * so it can update rowsMatchMin/Max
					 * (eg. during post-proc LIKE):
					 */
					query->left->countInfo = query->countInfo;
					r = TXdotree(query->left, fo,
						direction, offset);
					q->nrows = query->left->q->nrows - (r ? 0 : 1);
					/* Propagate counts back to SELECT: */
					query->countInfo = query->left->countInfo;
					toskip = 0;
				}
				if (r == -1)
					return r;
				readanother = query->readanother;
				if(query->left->op != NAME_OP && q->in1->pred)
				{
					DBGMSG(9, (999, NULL, "NEWCODE"));
					if(!tup_match(q->in1, q->in1->pred, fo))
						readanother = 1;
				}
			} while (readanother || ((toskip-- > 0) && (++q->nrows)));
			if (r == -1)
				return r;
			r = TXprocessquery(query, fo);
			query->state = q->state;
			if (r != -1)
				q->nrows += + 1;
			return r;
		case INSERT_OP :
startinsert:
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table insert");
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
/*
			TXdotree(query->left, fo, direction, offset);
*/
/*
	WTF: Maybe we need to do some more figgering out here what we want
	to do. If we assume the lack of NULLs then the question becomes are
	we doing a union from a table, or from values in the tree here. If
	we are doing it from values in the tree then the toughest part is
	creating the correct tuple to add in, and then also doing the writes,
	and getting the index updated.
*/
			switch (query->right->op)
			{
				case LIST_OP:
				case COLUMN_OP:
				case VALUE_OP:
					rc = tup_read(q->in2, fo, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN);
					if (rc == NULL)
						return -1;
					break;
				default:
				if(query->right->op != GROUP_BY_OP &&
				   query->right->op != DISTINCT_OP)
					TXdeltmprow(q->in2);
					r = TXdotree(query->right, fo, direction, offset);
					if (r == -1)
						return -1;
			}
			r = TXprocessquery(query, fo);
			if (r != -1)
				q->nrows += 1;
			else
			{
#ifdef DEBUG
				DBGMSG(1,(999, NULL, "ERROR INSERT"));
#endif
				if(texispeekerr(q->out->ddic) == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
					r = -2;
				else
					switch (query->right->op)
					{
						case LIST_OP:
						case COLUMN_OP:
						case VALUE_OP:
							r = -2;
							break;
						default:
							r = 0;
							goto startinsert;
							break;
					}
			}
			query->state = q->state;
#ifdef OLD_UPDATE_METHOD /* Don't rewind.  We have just read the inserted row */
			if (query->state == QS_PCOMMIT)
				TXrewinddbtbl(q->out);
#endif
			return r;
		case DEL_SEL_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a table delete");
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
			/* TXlocktable(q->in1, R_LCK); */
			if (TXlockandload(q->in1, PM_DELETE, NULL) == -1)
				return(-1);
			do {
				do {
					r = TXdotree(query->left, fo, SQL_FETCH_NEXT, 1);
					if (r == -1)
					{
						if(found==0)
						{
							/* TXunlocktable(q->in1, R_LCK); */
							TXunlockindex(q->in1, INDEX_WRITE, NULL);
							return r;
						}
#ifndef NO_DUP_DELETES
						found = 0;
						TXrewinddbtbl(q->out);
						r = TXdotree(query->left, fo, SQL_FETCH_NEXT, 1);
#endif
						if(r == -1)
						{
							/* TXunlocktable(q->in1, R_LCK); */
							TXunlockindex(q->in1, INDEX_WRITE, NULL);
							return r;
						}
					}
				}while((r = tup_match(q->in1, q->pred, fo))==0);
				if (r == -1)
				{
					/* TXunlocktable(q->in1, R_LCK); */
					TXunlockindex(q->in1, INDEX_WRITE, NULL);
					return r;
				}
			}while((r = TXprocessquery(query, fo)) == -1 ||
			       (toskip--> 0 && ++q->nrows));
			/* TXunlocktable(q->in1, R_LCK); */
			TXunlockindex(q->in1, INDEX_WRITE, NULL);
			if (r == -1)
				return r;
			found++;
			query->state = q->state;
			q->nrows += 1;
			return r;
		case UPD_SEL_OP :
			if(verbose)
				putmsg(MINFO, Fn, "Handling a record update");

#ifndef OLD_UPDATE_METHOD
			if(query->state != QS_ACTIVE)
			{
				q->usr = NULL;
#endif
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
#ifdef SLOW_UPDATE
			while (TXdotree(query->tname, fo, direction, offset) != -1);
#endif
			TXrewinddbtbl(q->in1);
#ifndef OLD_UPDATE_METHOD
			}
			do r =  TXprocessquery(query, fo);
			while ((r == 0 && toskip-- > 0 && ++q->nrows) ||
				(r == -2));
			if(r == 0)
				q->nrows++;
			return r;
#else
			TXprocessquery(query, fo);
			query->state = q->state;
			return -1;
#endif

		case FOP_PROXIM:
			if(query->state != QS_ACTIVE)
			{
				query->state = QS_ACTIVE;
				q->state = QS_ACTIVE;
				dolikep(query, fo);
			}
			if(q->in1->lname[0] == '\0' &&	/* RAM table? */
			   /* Bug 6172: A join, ANDed with a param-o-rama'd
			    * LIKEP, might have no RAM results in `q->in1':
			    * dolikep() did nothing because there is no
			    * LIKEP in the `query->left->q->pred' tree.
			    * Normally that means the LIKEP was replaced with
			    * a LIKER (from index search?), whose RAM results
			    * we read here.  But in this case, it was removed
			    * completely, due to param-o-rama, and there are
			    * no RAM results yet; need to continue with
			    * TXdotree() below:
			    */
			   (q->flags & TXqueryFlag_In1Populated))
			{
				if(!tup_read(q->in1, fo, direction, offset, &skipped, TXCOUNTINFOPN))
					return -1;
				return 0;
			}
			res = TXdotree(query->left, fo, direction, offset);
			/*   Most of the time, dolikep() has already done
			 * the complete LIKEP (e.g. for post-proc query),
			 * or does nothing (e.g. indexable query); either way
			 * `query->countInfo' is already set correctly.
			 *   But for `X LIKEP 'indexable-query' AND Y = 3',
			 * where X is indexed but Y is not, dolikep() does
			 * nothing (LIKEP already handled), and the above
			 * TXdotree(query->left) does the AND: so we need
			 * to propagate `query->left->countInfo' up.  Try to
			 * detect this by looking for unhandled PRED on
			 * `query->left' (i.e. the AND).  But also make sure
			 * dolikep() did not do the query (e.g. linear LIKEP):
			 * `query->q->in1->indguar' is set by dolikep()
			 * when it actually does the LIKEP.
			 *   (WTF is there another way, e.g. can we just
			 * check if dolikep() already did TXdotree() on
			 * `query->left'?)
			 */
			if (query->left->q &&
			    query->left->q->pred &&
			    !query->left->q->pred->handled &&
			    /* make sure dolikep() did not already do it: */
			    (!query->q || !query->q->in1 ||
			     !query->q->in1->indguar))
				query->countInfo = query->left->countInfo;
			return(res);
		case TABLE_OP :
		case 0 :
			query->state = QS_ACTIVE;
			q->state = QS_ACTIVE;
			return -1;
		case DEMUX_OP:
			/* multivaluetomultirow: split strlst and return
			 * multiple rows eg. for GROUP BY, DISTINCT on strlst:
			 */
			if (query->state != QS_ACTIVE)
			{
				query->state = QS_ACTIVE;
				q->state = QS_ACTIVE;
			}
			/* Start counting rowsReturnedMin, if not already: */
			if (!TX_ISVALIDCOUNT(countInfo->rowsReturnedMin))
				countInfo->rowsReturnedMin = 0;
			res = TXdemuxGetNextRow((TXDEMUX *)q->usr,
						query->right, fo);
			/* Propagate counts up: rowsMatchedMin/Max/indexCount
			 * are for real (original) table and thus pass up,
			 * but rowsReturnedMin/Max are determined here and
			 * may increase:
			 */
			countInfo->rowsMatchedMin =
				query->right->countInfo.rowsMatchedMin;
			countInfo->rowsMatchedMax =
				query->right->countInfo.rowsMatchedMax;
			countInfo->indexCount =
				query->right->countInfo.indexCount;
			if (!res)		/* EOF */
			{
				countInfo->rowsReturnedMax =
					countInfo->rowsReturnedMin;
				return(-1);
			}
			countInfo->rowsReturnedMin++;
			return(0);		/* got a row */
		default :
			putmsg(MERR+UGE,Fn,
				"Huh?! I appear to have gotten confused (QNODE_OP=0x%x)",
				(int)query->op);
			return -1;
	}
}
