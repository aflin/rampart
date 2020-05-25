/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <errno.h>
#include <stdio.h>
#if defined(unix) || defined(__unix)	/* MAW 02-15-94 wtf? groups on nt? */
#include <grp.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#  include <winsock.h>
#else /* !_WIN32 */
#  include <netinet/in.h>
#endif /* !_WIN32 */
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"
#include "cgi.h"

#if defined(unix) && !defined(_AIX)
int fchmod ARGS((int, mode_t));
#endif

static int join_optimize ARGS((DDIC *ddic, QNODE *query, FLDOP *fo));

#ifndef HAVE_STRERROR
#define strerror(a)  sys_errlist[a]
#endif

QUERY *
TXopenQuery(op)
QUERY_OP	op;
/* Creates and returns a QUERY struct for operation `op'.
 * Returns NULL on error.
 */
{
	static CONST char	fn[] = "TXopenQuery";
	QUERY			*q;

	if (!(q = (QUERY *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(QUERY))))
		return(QUERYPN);
	q->op = op;
	/* rest cleared by calloc() */
	return(q);
}

QUERY *
closequery(q)
QUERY *q;
{
	if (q == (QUERY *) NULL)
		return q;
	if (q->in1 != (DBTBL *) NULL &&
	    q->op != Q_RENAME &&
#ifdef OLD_UPDATE_METHOD
	    q->op != Q_UPDATE &&
#endif
#ifndef OLD_ORDERBY
	    q->op != Q_SELECT &&
#endif
	    q->op != Q_DELETE &&
	    q->op != Q_PROXIM &&
	    q->op != Q_APPEND)
		q->in1 = closedbtbl(q->in1);
	if (q->in2 != (DBTBL *) NULL)
#ifndef OLD_UPDATE_METHOD
		if(q->op != Q_UPDATE)
#endif
		q->in2 = closedbtbl(q->in2);
	if (q->order != (PROJ *) NULL)
		q->order = closeproj(q->order);
	if (q->proj != (PROJ *) NULL)
		q->proj = closeproj(q->proj);
	if (q->pred != (PRED *) NULL)
		q->pred = closepred(q->pred);
	if (q->pred1 != (PRED *) NULL)
		q->pred1 = closepred(q->pred1);
	q->pr1 = closepred(q->pr1);		/* Bug 6253 */
	if (q->prod != (PROD *) NULL)
		q->prod = closeprod(q->prod);
	if (q->update != (UPDATE *) NULL)
		q->update = closeupdate(q->update);
	if (q->op == Q_UPDATE && q->usr)
		q->usr = closebtree(q->usr);
	if ((q->op == Q_GROUP || q->op == Q_DISTINCT) && q->usr)
	{
		q->usr = TXcloseginfo(q->usr);
	}
	if (q->op == Q_DEMUX)
		q->usr = TXdemuxClose((TXDEMUX *)q->usr);
	free(q);
	return (QUERY *) NULL;
}

/******************************************************************/

PROJ *
TXddToProj(dbtbl)
DBTBL *dbtbl;
{
	PROJ *rc;
	DD *dd;
	int i;

	dd = dbtbl->tbl->dd;
	rc = (PROJ *) calloc(1, sizeof(PROJ));
	if (rc)
	{
		rc->preds = (PRED **) calloc(ddgetnfields(dd), sizeof(PRED *));
		if (rc->preds)
		{
			rc->n = ddgetnfields(dd);
			for (i = 0; i < rc->n; i++)
			{
				rc->preds[i] = (PRED *) calloc(1, sizeof(PRED));
				rc->preds[i]->left = strdup(ddgetname(dd, i));
				rc->preds[i]->lt = NAME_OP;
			}
		}
		rc->p_type = PROJ_AGG;
	}
	return rc;
}

/******************************************************************/

static int propagatepred ARGS((QNODE *query, PRED *pred, FLDOP *fo));

static int
propagatepred(query, pred, fo)
QNODE *query;
PRED *pred;
FLDOP *fo;
{
#if defined(CACHE_IINODE)
	IINODE *temp;
	PRED *vp;
	char *x;

	switch(query->op)
	{
	case PRODUCT_OP:
		x = disppred(pred, 0, 0);
		putmsg(999, NULL, "Given %s", x);
		if(x) free(x);
		break;
	case NAME_OP:
		x = disppred(pred, 0, 0);
		putmsg(999, NULL, "Given %s", x);
		if(x) free(x);
		vp = TXmakepredvalid(pred, query->q->out, 0, 0, 1);
		if(query->q && query->q->out)
		{
			x = disppred(vp, 0, 0);
			putmsg(999, NULL, "Valid is %s", x);
			if(x) free(x);
		}
		if(!vp)
			return 0;
		temp = TXpredtoiinode(query->q->out, vp, NULL, fo, 1, 1);
		if(!temp->index)
		{
			putmsg(999, NULL, "Denied");
			temp = TXcloseiinode(temp);
		}
		if(temp && temp != query->q->out->cacheinode)
		{
			if(query->q->out->cacheinode)
			{
				closepred(query->q->out->cacheinode->gpred);
				query->q->out->cacheinode =
				    TXcloseiinode(query->q->out->cacheinode);
			}
			temp->gpred = duppred(temp->gpred);
			temp->cached++;
			query->q->out->cacheinode = temp;
		}
	default:
		return 0;
	}
#else
	if (!query || query->op != PRODUCT_OP)
		return 0;
#endif
	if (query->q && query->q->pred == NULL)
	{
		query->q->pred = TXduppredvalid(pred, query->q->out, 0, 0, 1);
#if !defined(NO_PRE_FIND_INDEX)
		TXpredgetindx(query->q->pred, query->q->in1, query->q->in2);
#endif
	}
	propagatepred(query->left, pred, fo);
	propagatepred(query->right, pred, fo);
	return 0;
}

/******************************************************************/

#ifdef HAVE_VIEWS
static char *showqnop  ARGS((DDIC *ddic, QNODE *query, FLDOP *fo));
static char *treetosql ARGS((DDIC *ddic, QNODE *query, FLDOP *fo));

static char *
showqnop(ddic, query, fo)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
{
	putmsg(999, NULL, "query->op = %d", query->op);
	switch(query->op)
	{
	case NAME_OP:
		putmsg(999, NULL, "%s", query->tname);
		break;
	case PROJECT_OP:
		putmsg(999, NULL, "select ");
		showqnop(ddic, query->left, fo);
	case SELECT_OP:
		putmsg(999, NULL, "select ");
		showqnop(ddic, query->left, fo);
		putmsg(999, NULL, "where ");
		putmsg(999, NULL, "%s", disppred(TXtreetopred(ddic, query->right, 0, fo, NULL), 0, 0));
		break;
	}
	return NULL;
}

/******************************************************************/

static char *
treetosql(ddic, query, fo)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
{
	showqnop(ddic, query, fo);
	return strdup("SELECT * FROM test1 union SELECT * from test2;");
}
#endif

/******************************************************************/

static char *querytotext ARGS((STRBUF *sb, QNODE *query));

static char *
querytotext(sb, query)
STRBUF  *sb;    /* buffer to append to */
QNODE *query;   /* query tree */
/* Returns NULL on error, else string data of `sb'.
 */
{
	int	len;

	switch(query->op)
	{
	case LIST_OP:
		if (addstrbuf(sb, ",", 1) == -1) return(CHARPN);
		if (!querytotext(sb, query->left)) return(CHARPN);
		if (!querytotext(sb, query->right)) return(CHARPN);
		break;
	case NAME_OP:
		if((len = lenstrbuf(sb)) > 0 &&
                        sb->data[len-1] != ',' &&
                        sb->data[len-1] != '(' &&
			addstrbuf(sb, " ", 1) == -1) return(CHARPN);
		if (addstrbuf(sb, query->tname, -1) == -1) return(CHARPN);
		break;
	case REG_FUN_OP:
		if (!querytotext(sb, query->left)) return(CHARPN);
		if (addstrbuf(sb, "(", 1) == -1) return(CHARPN);
		if (!querytotext(sb, query->right)) return(CHARPN);
		if (addstrbuf(sb, ")", 1) == -1) return(CHARPN);
		break;
#ifdef TX_USE_ORDERING_SPEC_NODE
	case ORDERING_SPEC_OP:
		/* This field list will be used externally in SYSINDEX,
		 * not internally in a PRED etc., so put back the
		 * `^' `-' ASC/DESC/IGNCASE flags:
		 */
		if (!querytotext(sb, query->left)) return(NULL);
		if (addstrbuf(sb, query->tname, -1) == -1) return(NULL);
		break;
#endif /* TX_USE_ORDERING_SPEC_NODE */
	default:
		break;
	}
	return sb->data;
}

/******************************************************************/

static int numtrans ARGS((QNODE *query, DBTBL *t));


static int
numtrans(query, t)
QNODE *query;
DBTBL *t;
/* Returns 0 on success, -1 on error.
 */
{
	long fno;
	char *tail = NULL, *newVal = NULL, *numStr, *fldName;
	int	ret;

	if (!query) goto ok;
	switch(query->op)
	{
		case NAMENUM_OP:
			numStr = query->tname;
			if (*numStr == '_') numStr++;
			fno = strtol(numStr, &tail, 0);
			fldName = ddgetname(t->tbl->dd,
					    ddgetorign(t->tbl->dd, fno - 1));
			if (!fldName)		/* Bug 4427 */
			{
				putmsg(MERR + UGE, NULL,
				       "No such ORDER BY field number %d",
				       (int)fno);
				goto err;
			}
			newVal = TXstrcat2(fldName, tail);
			if (!newVal) goto err;
			query->op = NAME_OP;
			query->tname = TXfree(query->tname);
			query->tname = newVal;
			newVal = NULL;
			break;
		case LIST_OP:
			if (numtrans(query->left, t) < 0) goto err;
			if (numtrans(query->right, t) < 0) goto err;
			break;
#ifdef TX_USE_ORDERING_SPEC_NODE
		case ORDERING_SPEC_OP:
			if (numtrans(query->left, t) < 0) goto err;
			break;
#endif /* TX_USE_ORDERING_SPEC_NODE */
		default:
			break;
	}
ok:
	ret = 0;
	goto finally;

err:
	ret = -1;
finally:
	newVal = TXfree(newVal);
	return(ret);
}

/******************************************************************/

static int
TXprojcountonly(PROJ *proj, FLDOP *fo)
{
	int i;
	PRED *p;

	(void)fo;
	for(i=0; i < proj->n; i++)
	{
		p = proj->preds[i];
		if(p->op == RENAME_OP && p->lt == 'P')
			p = p->left;
		if(p->op != AGG_FUN_OP)
			return 0;
		if(strcmp(p->left, "count") || TXpredrtdist(p))
			return 0;
	}
	return 1;
}

/******************************************************************/

static int
sladdpred(SLIST *sl, PRED *pred)
{
	char *s, *ts;
	size_t itemlen;

	s = TXpredflds(pred);
	if(!s)
		return 0;
	ts = s;
	while((itemlen = strcspn(ts, "-^, ")) != 0)
	{
		ts[itemlen] = '\0';
		if(!slfind(sl, ts))
			sladd(sl, ts);
		ts += itemlen + 1;
		ts += strspn(ts, "-^, ");
	}
	TXfree(s);
	return 0;
}

static char *
TXgetStringValFromQnode(DDIC *ddic, FLDOP *fldop, DBTBL *dbtbl, QNODE *qnode)
/* Returns alloc'd string.
 */
{
	char	*val = NULL, *ret;
	FLD	*fld = NULL, *fld2 = NULL, *cnvResFld = NULL;
	PRED	*pred = NULL;

	pred = TXtreetopred(ddic, qnode, 0, fldop, dbtbl);
	if (!pred) goto err;
	if (pred_eval(NULL, pred, fldop) < 0)	/* wtf does 1 or 0 ret mean?*/
		goto err;
	fld = fopop(fldop);
	pred = closepred(pred);
	if (!fld) goto err;
	if (TXfldbasetype(fld) == FTN_CHAR)	/* already char */
		val = getfld(fld, NULL);
	else
	{
		fld2 = createfld("varchar", 20, 0);
		if (!fld2) goto err;
		putfld(fld2, "", 0);		/* for verbose msgs */
		if (fopush(fldop, fld) != 0 ||
		    fopush(fldop, fld2) != 0 ||
		    foop(fldop, FOP_CNV) != 0)
			goto err;
		fld = closefld(fld);
		fld2 = closefld(fld2);
		fld = fopop(fldop);
		if (!fld) goto err;
		if (TXfldbasetype(fld) != FTN_CHAR) goto err;
		val = getfld(fld, NULL);
	}
	if (!(ret = TXstrdup(TXPMBUFPN, __FUNCTION__, val))) goto err;
	goto finally;

err:
	ret = NULL;
finally:
	pred = closepred(pred);
	fld = closefld(fld);
	fld2 = closefld(fld2);
	cnvResFld = closefld(cnvResFld);
	return(ret);
}

/******************************************************************/

DBTBL *
ipreparetree(prepinfo, query, parentquery, success)
IPREPTREEINFO *prepinfo;
QNODE *query;		/* Query we are preparing */
QNODE *parentquery;	/* Parent node, if any */
int *success;		/* Where to store a success code */
{
	static CONST char Fn[] = "ipreparetree";
	QUERY *q;
	PRED *pred = NULL;
	int i;
	int op;
	DDIC *ddic;	/* Database */
	FLDOP *fo;	/* A fldmath struct for luck */
	int prepq;	/* When to prepare the query */
	int allowbubble;/* Allowbubble */
	int countonly;/* Allowbubble */
	int analyze, res;
	DBTBL *dbtbl;
	STRBUF	*strBuf = STRBUFPN;
	char	*args;

	if(!prepinfo)
		return NULL;

	ddic = prepinfo->ddic;
	fo = prepinfo->fo;
	prepq = prepinfo->prepq;
	allowbubble = prepinfo->allowbubble;
	countonly = prepinfo->countonly;
	analyze = prepinfo->analyze;
	dbtbl = prepinfo->dbtbl;
#ifdef MEMDEBUG
	mac_ovchk();
#endif
	q = TXopenQuery((QUERY_OP)0 /* wtf query->op? */);
	query->q = q;
	q->nrows = 0;
	query->state = QS_PREPED;
	q->state = QS_PREPED;
	*success = 0;
	if(parentquery)
		q->priorproj = parentquery->q->priorproj;
	else
		q->priorproj = 0;
#ifdef NEVER
	putmsg(999, NULL, "Processing %d", query->op);
#endif
	switch (query->op)
	{
	case NAME_OP:
		return TXnode_table_prep(prepinfo, query, parentquery, success);
	case RENAME_OP:
		return TXnode_rename_prep(prepinfo, query, parentquery, success);
	case HINT_OP:
		return TXnode_hint_prep(prepinfo, query, parentquery, success);
	case PRODUCT_OP:
		return TXnode_join_prep(prepinfo, query, parentquery, success);
	case UNION_OP:
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze && !query->fldlist)
		{
			if(parentquery && parentquery->fldlist)
			{
				query->fldlist =
					sldup(parentquery->fldlist);
			}
		}
		prepinfo->prepq = 1;
		q->in1 = ipreparetree(prepinfo, query->left, query, success);
		if (q->in1 == NULL)
			return (DBTBL *) NULL;
		if(analyze && query->left)
		{
			if(query->left->afldlist)
			{
				if(!query->afldlist)
					query->afldlist =
						sldup(query->left->afldlist);
				else
					sladdslst(query->afldlist,
						query->left->afldlist, 1);
			}
		}
		q->in2 = ipreparetree(prepinfo, query->right, query, success);
		if (q->in2 == NULL)
			return (DBTBL *) NULL;
		if(analyze && query->right)
		{
			if(query->right->afldlist)
			{
				if(!query->afldlist)
					query->afldlist =
						sldup(query->right->afldlist);
				else
					sladdslst(query->afldlist,
						query->right->afldlist, 1);
			}
		}
		q->pred = (PRED *) NULL;
		q->proj = (PROJ *) NULL;
		q->op = Q_UNION;
		preparequery(query, fo, prepinfo->allowbubble);
		if (q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case PROJECT_OP:
		prepinfo->preq |= PM_SELECT;
		q->pred = (PRED *) NULL;
		q->proj = treetoproj(ddic, query->right, fo);
		if(!q->proj)
			return NULL;
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze) for(i=0; i < q->proj->n; i++)
		{
			if(!query->fldlist)
			{
				if(parentquery && parentquery->fldlist)
				{
					query->fldlist =
						sldup(parentquery->fldlist);
				}
				else
				{
					query->fldlist = slopen();
				}
			}
			sladdpred(query->fldlist, q->proj->preds[i]);
		}
		q->priorproj = 1;
		if (query->left->op == SELECT_OP && q->proj->p_type == PROJ_SINGLE)
			prepinfo->allowbubble = 0;
		if(TXprojcountonly(q->proj, fo))
			prepinfo->countonly = 1;
		q->in1 = ipreparetree(prepinfo, query->left, query, success);

		/* KNG 20081210 propagate stats up: */
		query->countInfo = query->left->countInfo;

		prepinfo->allowbubble = allowbubble;
		prepinfo->countonly = countonly;
		if (q->in1 == NULL)
			return (DBTBL *) NULL;
		if(analyze && query->left)
		{
			if(query->left->afldlist)
			{
				if(!query->afldlist)
					query->afldlist =
						sldup(query->left->afldlist);
				else
					sladdslst(query->afldlist,
						query->left->afldlist, 1);
			}
		}
		q->in2 = (DBTBL *) NULL;
		op = Q_PROJECT;
#ifndef NO_ORDERED_GROUP
		if (query->left->op == GROUP_BY_OP && q->proj->p_type == PROJ_AGG)
		{
#ifdef NEVER
			putmsg(MINFO, NULL, "Unexpected use of GROUP BY.  No aggregate functions");
#endif
			q->proj->p_type = PROJ_SINGLE;
		}
#endif
		q->op = op;
		preparequery(query, fo, prepinfo->allowbubble);
		if (q->out == NULL || q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case SELECT_OP:
		prepinfo->preq |= PM_SELECT;
		pred=TXtreetopred(ddic, query->right,
				  TXgetDiscardUnsetParameterClauses(), fo,
				  dbtbl);
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze)
		{
		if(!query->fldlist)
		{
			if(parentquery && parentquery->fldlist)
				query->fldlist = sldup(parentquery->fldlist);
		}
		if(query->fldlist)
		{
			sladdpred(query->fldlist, pred);
		}
		}
/*
		else I need them all;
*/
		if (query->left->op == PRODUCT_OP)
		{
			q->in1 = ipreparetree(prepinfo, query->left->left, query, success);
			if (q->in1 == NULL)
			{
				if(pred)
					pred = closepred(pred);
				return (DBTBL *) NULL;
			}
			q->in2 = ipreparetree(prepinfo, query->left->right, query, success);
			if (q->in2 == NULL)
			{
				if(pred)
					pred = closepred(pred);
				return (DBTBL *) NULL;
			}
			op = Q_CPRODUCT;
		}
		else
		{
			q->in1 = ipreparetree(prepinfo, query->left, query, success);
			if (q->in1 == NULL)
			{
				if(pred)
					pred = closepred(pred);
				return (DBTBL *) NULL;
			}
			q->in2 = (DBTBL *) NULL;
			op = Q_SELECT;
		}
		if(analyze && query->left)
		{
			if(query->left->afldlist)
			{
				if(!query->afldlist)
					query->afldlist = sldup(query->left->afldlist);
				else
					sladdslst(query->afldlist,query->left->afldlist, 1);
			}
		}
		if (prepq)
		{
			q->pred = pred;
			if (op == Q_CPRODUCT)
				propagatepred(query->left, q->pred, fo);
		}
		q->proj = (PROJ *) NULL;
		q->op = op;
		preparequery(query, fo, prepinfo->allowbubble);
		if(!q->out)
		{
			if(pred && !prepq)
				pred = closepred(pred);
			return(q->out);
		}
		/* WTF!!!  Rewinds setup bubble index */
		/* WTF!!!  Forces extra isetdbidx if NO_PRE_FIND_INDEX */
		/* WTF!!!  in dbidx.c */
		TXrewinddbtblifnoindex(q->out);
		if (!prepq)
		{
			q->pred = pred;
			if (op == Q_CPRODUCT)
				propagatepred(query->left, q->pred, fo);
		}
#ifndef NO_TEST_JOIN_OPTIMIZE
		join_optimize(ddic, query, fo);
#endif
#ifndef ALLOW_MISSING_FIELDS
		if(TXmakepredvalid(q->pred, q->out, 1, 1, 1) != q->pred)
		{
			q->out = closedbtbl(q->out);
			return NULL;
		}
#endif
		if (q->out->tbl == NULL)
			return (DBTBL *) NULL;
		return q->out;
	case TABLE_AS_OP:
		{
			char *fn;
			DD *tdd;
			DBTBL	*rc;

			if(((char *)query->right)[0] != 'R')
			{
				fn = (char *) malloc(PATH_MAX);
				strcpy(fn, ddic->tbspc);
				strcat(fn, query->tname);
			}
			else
				fn = NULL;
			rc = ipreparetree(prepinfo, query->left, query, success);
			if (!rc)
				return rc;
			/* Bug 4390: convert blobi back to blob (via
			 * TXbiddc()) when creating a table with
			 * `CREATE TABLE dest AS SELECT * FROM src':
			 */
			tdd = TXbiddc(tbldd(rc->tbl));
			if (!tdd)
				return closedbtbl(rc);
			q->out = createdbtbl(ddic, tdd, fn,
					     query->tname, "", TEXIS_TABLE);
			tdd = closedd(tdd);
			if (query->left->op != NAME_OP &&
			    query->left->op != SELECT_OP &&
			    query->left->op != FLDMATH_PROXIM)
			{
				q->in1 = q->out;
				query->left->q->out = q->out;
/* WTF - hack to not double delete */ q->op = Q_RENAME;
				closedbtbl(rc);
			}
			else
			{
				q->in1 = rc;
			}
			rc = q->out;
			free(fn);
			return rc;
		}
	case TABLE_OP:
		{
			char *fn;
			char ttype;
			DD *dd = (DD *)query->left;
			DBTBL	*rc;

			ttype = TEXIS_TABLE;
			switch(dd->tbltype)
			{
				case TEXIS_RAM_TABLE:
					fn = NULL;
					dd->tbltype=1;
					break;
				case TEXIS_RAM_BTREE_TABLE:
					fn = NULL;
				case TEXIS_BTREE_TABLE:
					ttype = 'b';
					dd->tbltype = 0;
				default:
					fn = (char *) malloc(PATH_MAX);
					strcpy(fn, ddic->tbspc);
					strcat(fn, query->tname);
			}
			rc = createdbtbl(ddic, dd, fn, query->tname, "", ttype);
			if(fn) free(fn);
			return rc;
		}
	case INSERT_OP:
		prepinfo->preq |= PM_INSERT;
		q->in1 = ipreparetree(prepinfo, query->left, query, success);
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
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
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				q->in2->ddic = ddic;
				q->in2->type = TEXIS_TABLE;
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				listtotbl(query->right, q->in2, fo);
				break;
			}
		case COLUMN_OP:
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				q->in2->ddic = ddic;
				q->in2->type = TEXIS_TABLE;
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				columntotbl(query->right, q->in2, fo);
				break;
			}
		case VALUE_OP:
			{
				DD *dd;

				q->in2 = (DBTBL *) calloc(1, sizeof(DBTBL));
				if(!q->in2)
					return NULL;
				q->in2->ddic = ddic;
				q->in2->type = TEXIS_TABLE;
				q->in2->tblid = -1;
				dd = TXbddc(tbldd(q->in1->tbl));
				q->in2->tbl = createtbl(dd, NULL);
				closedd(dd);
				if (valuestotbl(query->right, q->in2, fo) == -1)
					return NULL;
				break;
			}
		default:
			{
				int tmppreq = prepinfo->preq;
				prepinfo->preq = 0;
				q->in2 = ipreparetree(prepinfo, query->right, query, success);
				prepinfo->preq = tmppreq;
			/* Set preq back to 0, as different subquery */
			break;
			}
		}
		q->op = Q_APPEND;
		if ((q->in1 == NULL) || (q->in2 == NULL))
			return (DBTBL *) NULL;
		if (q->in1->lname && q->in2->lname &&
		    !strcmp(q->in1->lname, q->in2->lname))
		{
			putmsg(MERR + UGE, (char *) NULL,
			       "Can't insert a table into itself.");
			return (DBTBL *) NULL;
		}
		preparequery(query, fo, prepinfo->allowbubble);
		if (q->out == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		if (q->out->tbl == (TBL *) NULL)
			return (DBTBL *) NULL;
		TXrewinddbtbl(q->out);
		return q->out;
	case COLUMN_OP:
#if 0
		fprintf(stderr, "\nTime for some funky assignments\n");
#endif
		putmsg(MWARN, Fn, "This function not supported yet");
		q->in1 = ipreparetree(prepinfo, query->left, query, success);
		q->in2 = ipreparetree(prepinfo, query->right, query, success);
#if 0
		disptable(q->in2, fo);
#endif
		return (DBTBL *) NULL;
	case DEL_SEL_OP:
		prepinfo->preq |= PM_DELETE;
		q->in1 = ipreparetree(prepinfo, query->left, query, success);
		if (q->in1 == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		q->in2 = (DBTBL *) NULL;
		if (query->right)
			q->pred = TXtreetopred(ddic, query->right,
					TXgetDiscardUnsetParameterClauses(),
					       fo, dbtbl);
		else
			q->pred = (PRED *) NULL;
		q->proj = (PROJ *) NULL;
		q->op = Q_DELETE;
		preparequery(query, fo, prepinfo->allowbubble);
		if (q->out != (DBTBL *) NULL)
		{
			if(q->out->type != INDEX_BTREE)
			{
				TXrewinddbtbl(q->out);
			}
			if (q->out->tbl == (TBL *) NULL)
			{
				return (DBTBL *) NULL;
			}
		}
		return q->out;
	case UPD_SEL_OP:
		q->update = treetoupd(ddic, query->left, fo);
		if (q->update == (UPDATE *) NULL)	/*if(!q->update) */
			return (DBTBL *) NULL;
		prepinfo->preq |= PM_UPDATE;
		q->in1 = ipreparetree(prepinfo, query->tname, query, success);
		if (q->in1 == (DBTBL *) NULL)
			return (DBTBL *) NULL;
		q->in2 = (DBTBL *) NULL;
		if (query->right)
			q->pred = TXtreetopred(ddic, query->right,
					TXgetDiscardUnsetParameterClauses(),
					       fo, dbtbl);
		else
			q->pred = (PRED *) NULL;
		q->op = Q_UPDATE;
		preparequery(query, fo, prepinfo->allowbubble);
		if (q->out != (DBTBL *) NULL)
		{
			if (q->out->tbl == (TBL *) NULL)
				return (DBTBL *) NULL;
			TXrewinddbtbl(q->out);
		}
		return q->out;
	case ORDERNUM_OP:
		q->op = Q_ORDER;
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
#ifdef NEVER
		prepinfo->prepq = 0;
#endif
		q->in1 = ipreparetree(prepinfo, query->right, query, success);
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
		if (numtrans(query->left, q->in1) < 0) return(NULL);
		q->proj = treetoproj(ddic, query->left, fo);
		if(!q->proj)
			return NULL;
		goto orderproj;
	case ORDER_OP:
		q->op = Q_ORDER;
		q->proj = treetoproj(ddic, query->left, fo);
		if(!q->proj)
			return NULL;
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze) for(i=0; i < q->proj->n; i++)
		{
			if(!query->fldlist)
			{
				if(parentquery && parentquery->fldlist)
				{
					query->fldlist =
						sldup(parentquery->fldlist);
				}
				else
				{
					query->fldlist = slopen();
				}
			}
			sladdpred(query->fldlist, q->proj->preds[i]);
		}
		prepinfo->prepq = 0;
		q->in1 = ipreparetree(prepinfo, query->right, query, success);
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
#ifdef NEVER /* Was NO_OPTIMIZE ORDER - fails with query */
		if (parentquery && parentquery->q && (parentquery->q->pred == NULL) && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
#endif
orderproj:
		switch (query->right->op)
		{
		case SELECT_OP:
		case GROUP_BY_OP:
			{
				QUERY *q1 = query->right->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(query->right, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(query->right, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
				}
				break;
			}
		case FOP_PROXIM:
			{
				QNODE	*qnode1 = query->right->left;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
					/* propagate stats up;
					 * `qnode1' is SELECT;
					 * `query->right' could be LIKEP?
					 */
					query->right->countInfo =
						qnode1->countInfo;
					query->countInfo = qnode1->countInfo;
				}
				break;
			}
		case NAME_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(qnode1, q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL, NULL);
				TXsettablepred(qnode1, q1->out, q1->pred,
					       q->order, fo, 1, NULL, NULL);
				break;
			}
		case PROJECT_OP:
		case DISTINCT_OP:
		case UNION_OP:
		case RENAME_OP:
			break;	/* Can happen.  Not bad? */
		default:
			putmsg(MERR, Fn, "Operation %d was unexpected while ordering",
			       query->right->op);
			break;
		}
		preparequery(query, fo, prepinfo->allowbubble);
		return q->out;

	case GROUP_BY_OP:
		q->proj = treetoproj(ddic, query->left, fo);
		if(!q->proj)
			return NULL;
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze) for(i=0; i < q->proj->n; i++)
		{
			if(!query->fldlist)
			{
				if(parentquery && parentquery->fldlist)
				{
					query->fldlist =
						sldup(parentquery->fldlist);
				}
				else
				{
					query->fldlist = slopen();
				}
			}
			sladdpred(query->fldlist, q->proj->preds[i]);
		}
		q->op = Q_GROUP;
		switch(query->right->op)
		{
			case SELECT_OP:
			case ORDER_OP:
			case NAME_OP:
/* Don't setup indices yet, as we will do it ourselves */
				prepinfo->prepq = 0;
				q->in1 = ipreparetree(prepinfo, query->right,
				             query, success);
				break;
			default:
/* Setup indices yet, as we can't do it ourselves yet (too deep in tree) */
				prepinfo->prepq = 1;
				q->in1 = ipreparetree(prepinfo, query->right,
					     query, success);
				break;
		}
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
		if(analyze && query->right)
		{
			if(query->right->afldlist)
			{
				if(!query->afldlist)
					query->afldlist = sldup(query->right->afldlist);
				else
					sladdslst(query->afldlist,query->right->afldlist, 1);
			}
		}
#ifdef NEVER			/* Not appropriate for group by ? */
		if (parentquery && parentquery->q && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
#endif
		switch (query->right->op)
		{
		case SELECT_OP:
		case ORDER_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
				}
				break;
			}
		case FOP_PROXIM:
			{
				QNODE	*qnode1 = query->right->left;
				QUERY	*q1 = qnode1->q;

#ifndef NEVER
				break;
#endif
				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
				}
				break;
			}
		case NAME_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(qnode1, q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL, NULL);
				TXsettablepred(qnode1, q1->out, q1->pred,
					       q->order, fo, 1, NULL, NULL);
				break;
			}
		case RENAME_OP:
			break;
		default:
			putmsg(MERR, Fn, "Operation %d was unexpected while grouping",
			       query->right->op);
			break;
		}
		preparequery(query, fo, prepinfo->allowbubble);
		return q->out;
	case DISTINCT_OP:
		q->op = Q_DISTINCT;
		switch(query->right->op)
		{
			case SELECT_OP:
			case ORDER_OP:
			case NAME_OP:
/* Don't setup indices yet, as we will do it ourselves */
				prepinfo->prepq = 0;
				q->in1 = ipreparetree(prepinfo, query->right,
				             query, success);
			default:
/* Setup indices yet, as we can't do it ourselves yet (too deep in tree) */
				prepinfo->prepq = 1;
				q->in1 = ipreparetree(prepinfo, query->right,
					     query, success);
		}
		if (q->in1 == (DBTBL *) NULL)
			return q->in1;
#ifdef NEVER			/* Not appropriate for group by ? */
		if (parentquery && parentquery->q && parentquery->q->proj && parentquery->q->proj->type == 1)
		{
			q->out = q->in1;
			q->in1 = NULL;
			return q->out;
		}
#endif
		q->proj = TXddToProj(q->in1);
		switch (query->right->op)
		{
		case SELECT_OP:
		case ORDER_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				if (q1->op == Q_SELECT)
				{
					TXsettablepred(qnode1, q1->in1, (PRED *) NULL,
						       (PROJ *) NULL, fo, 1, NULL, NULL);
					TXsettablepred(qnode1, q1->in1, q1->pred,
						       q->order, fo, 1, NULL, NULL);
				}
				break;
			}
		case NAME_OP:
			{
				QNODE	*qnode1 = query->right;
				QUERY	*q1 = qnode1->q;

				q->order = dupproj(q->proj);
				q1->order = dupproj(q->proj);
				TXsettablepred(qnode1, q1->out, (PRED *) NULL,
					       (PROJ *) NULL, fo, 1, NULL, NULL);
				TXsettablepred(qnode1, q1->out, q1->pred,
					       q->order, fo, 1, NULL, NULL);
				break;
			}
		default:
/*
   putmsg(MERR, Fn, "Operation %d was unexpected in distinct",
   query->right->op);
 */
			break;
		}
		preparequery(query, fo, prepinfo->allowbubble);
		return q->out;

	case GRANT_OP:
		{
			long pf;

			pf = strtoperms(query->left->tname);
			prepinfo->preq |= pf;
			prepinfo->preq |= PM_GRANT;
			q->in1 = ipreparetree(prepinfo, query->right, query, success);
			if (q->in1)
			{
				if (permgrant(ddic, q->in1, query->tname, pf) == -1)
				{
					return NULL;
				}
				setindexperms(q->in1);
			}
			*success = 1;
			return (DBTBL *) NULL;
		}
	case REVOKE_OP:
		{
			long pf;

			pf = strtoperms(query->left->tname);
			prepinfo->preq |= pf;
			prepinfo->preq |= PM_GRANT;

			q->in1 = ipreparetree(prepinfo, query->right, query, success);
			if (q->in1 == (DBTBL *) NULL ||
			    q->in1->tbl == (TBL *) NULL)
				return (DBTBL *) NULL;
			if (permrevoke(ddic, q->in1, query->tname, pf) == -1)
				return (DBTBL *) NULL;
			setindexperms(q->in1);
			*success = 1;
			return (DBTBL *) NULL;
		}
	case FOP_PROXIM:
	{
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(analyze && !query->fldlist)
		{
			if(parentquery && parentquery->fldlist)
				query->fldlist = sldup(parentquery->fldlist);
		}
		q->in1 = ipreparetree(prepinfo, query->left, query, success);
		if (q->in1 == (DBTBL *) NULL || q->in1->tbl == (TBL *) NULL)
			return (DBTBL *) NULL;
		if(analyze && query->left && query->left->afldlist)
		{
			if(!query->afldlist)
				query->afldlist = sldup(query->left->afldlist);
			else
				sladdslst(query->afldlist,query->left->afldlist, 1);
		}
		q->out = q->in1;
		q->op = Q_PROXIM;

		/* KNG 20081210 propagate stats up: */
		query->countInfo = query->left->countInfo;

		return q->out;
	}
	case 0:
		return (DBTBL *) NULL;
	case PROP_OP:		/* SET property=value */  /* JMT 97-11-24 */
	{
		int rc2;
		char *prop = NULL;
		char *val = NULL;

		if(query->right && query->right->left &&
		   query->right->left->op == NAME_OP)
			prop = query->right->left->tname;
		if(!prop)
			return NULL;
		val = TXgetStringValFromQnode(ddic, fo, dbtbl,
					      query->right->right);
		if (!val) return(NULL);
		rc2 = setprop(ddic, prop, val);
		val = TXfree(val);
		*success = rc2 >= 0 ? 1 : 0;
		return NULL;
	}
#ifdef HAVE_VIEWS
	case VIEW_OP:/* JMT 98-02-16 */
		{
			char *fn=NULL;
			DD *tdd;

			rc = ipreparetree(prepinfo, query->left, query, success);
			if (!rc)
				return rc;
			fn = treetosql(ddic, query->left, fo);
			tdd = rc->tbl->dd;
			if (!tdd)
				return closedbtbl(rc);
			q->out = createdbtbl(ddic, tdd, fn,
					     query->tname, "", 'V');
			if (query->left->op != NAME_OP &&
			    query->left->op != SELECT_OP &&
			    query->left->op != FOP_PROXIM)
			{
				q->in1 = q->out;
				query->left->q->out = q->out;
/* WTF - hack to not double delete */ q->op = Q_RENAME;
				closedbtbl(rc);
			}
			else
			{
				q->in1 = rc;
			}
			rc = q->out;
			*success=1;
			free(fn);
			return rc;
		}
		return q->out;
#endif
#ifndef NO_KEEP_STATS
	case CREATE_OP:
		if(!strcmp(query->tname, "index"))
		{
			char *fields, *indexType, *indexName, *tableName;
			int rc;
			STRBUF	*sb = NULL;
			QNODE	*optionsSubList;
			TXindOpts	*options = NULL;

			/* NOTE: see also irepreparetree() */
			/* <CREATE what="index">
			 *   <list>
			 *     indexName, tableName, indexType[, option ...]
			 *   </list>
			 *   <list> field[, field ...] </list>
			 * </CREATE>
                         */
			indexName = TXqnodeListGetItem(query->left, 0);
			tableName = TXqnodeListGetItem(query->left, 1);
			indexType = TXqnodeListGetItem(query->left, 2);
			if ((sb = openstrbuf()) == STRBUFPN ||
			    (fields = querytotext(sb,query->right))==CHARPN ||
			    !indexName || !tableName || !indexType)
			{				/* failed */
				*success = 0;
				sb = closestrbuf(sb);
				return(NULL);
			}
			options = TXindOptsOpen(ddic);	/* global defaults */
			if (!options) goto err1;
			optionsSubList = TXqnodeListGetSubList(query->left, 3);
			if (optionsSubList &&	/* has WITH options */
			    !TXindOptsGetRawOptions(options, optionsSubList))
			{
			err1:
				rc = -1;
				goto afterCreateIndex;
			}
			rc = createindex(ddic,
					 indexName,	/* index file */
					 indexName,
					 tableName,
					 fields,
					 (indexType[0] == INDEX_UNIQUE),
					 indexType[0], options);
		afterCreateIndex:
			options = TXindOptsClose(options);
			if(rc != -1) 
				*success=1;
			else
				*success=0;
			sb = closestrbuf(sb);
		}
#ifdef HAVE_LINK_TABLES
		else if (!strcmp(query->tname, "link"))
		{
			int rc;
			FLD *f;
			char *connect = NULL;

			if(query->right->right->op == FIELD_OP)
			{
				f = query->right->right->tname;
				connect = getfld(f, NULL);
			}
			if(connect)
				rc = TXcreatelink(ddic, query->left->tname, query->right->left->tname, connect);
			else
				rc = -1;
			if (rc == 0)
				*success = 1;
			else
				*success = 0;
		}
#endif /* HAVE_LINK_TABLES */
		else if (strcmp(query->tname, "user") == 0)
		{
			int	rc2;
			char	*user = NULL;
			char	*pass = NULL;

			/* Note that we only get here when the CREATE USER
			 * password is a parameter/expression; for literals
			 * it was already handled in sql1.y
			 */
			if (query->left && query->left->op == NAME_OP)
				user = query->left->tname;
			if (!user) return(NULL);
			pass = TXgetStringValFromQnode(ddic, fo, dbtbl,
						       query->right);
			if (!pass) return(NULL);
			rc2 = createuser(ddic, user, pass);
			pass = TXfree(pass);
			*success = (rc2 == 0 ? 1 : 0);
		}
		else
		{
			putmsg(MERR + UGE, __FUNCTION__,
			       "Unknown CREATE target `%s'", query->tname);
			goto alterErr;
		}
		return NULL;
#endif /* !NO_KEEP_STATS */
	case DROP_OP:
		if(!strcmp(query->tname, "table") && query->left)
		{
			TXdropdtable(ddic, query->left->tname);
			*success=1;
		}
		if(!strcmp(query->tname, "index") && query->left)
		{
			TXdropdindex(ddic, query->left->tname);
			*success=1;
		}
		if(!strcmp(query->tname, "trigger") && query->left)
		{
			TXdroptrigger(ddic, query->left->tname);
			*success=1;
		}
		if(!strcmp(query->tname, "user") && query->left)
		{
			TXdropuser(ddic, query->left->tname);
			*success=1;
		}
#ifdef HAVE_LINK_TABLES
		if(!strcmp(query->tname, "link") && query->left)
		{
			int rc;

			rc = TXdroplink(ddic, query->left->tname);
			if (rc == 0)
				*success = 1;
			else
				*success = 0;
		}
#endif
		return NULL;
	case ALTER_OP:
		/* NOTE: see also repreptree.c */
		if (strcmpi(query->tname, "table") == 0)
		{				/* ALTER TABLE */
			/* We somewhat arbitrarily decided ALTER TABLE
			 * requires SELECT and UPDATE perms -- even though
			 * it never returns nor modifies logical data --
			 * because it does do significant read/writes,
			 * and should have *some* perms required.
			 * See also TXgetindexes() perms in TXcompactTable():
			 */
			prepinfo->preq |= (PM_ALTER | PM_SELECT | PM_UPDATE);
			if (!query->left ||
			    query->left->op != NAME_OP ||
			    query->left->tname == CHARPN)
			{
				putmsg(MERR, Fn,
					"Cannot get ALTER TABLE table name");
				goto alterErr;
			}
			/* Continue prep on the left NAME_OP (table);
			 * let prep open the table so it can check perms:
			 */
			q->in1 = ipreparetree(prepinfo, query->left, query,
					      success);
			if (q->in1 == DBTBLPN) goto alterErr;
			/* `query->right' is args e.g. "compact": */
			if ((strBuf = openstrbuf()) == STRBUFPN ||
			    (args = querytotext(strBuf,query->right))==CHARPN)
				goto alterErr;
			if (strcmpi(args, "compact") == 0)
				res = TXcompactTable(ddic, q, 0);
			else
			{
				putmsg(MERR + UGE, CHARPN,
					"Unknown ALTER TABLE argument `%s'",
					args);
				goto alterErr;
			}
			*success = (res ? 1 : 0);
		}
		else if (strcmpi(query->tname, "index") == 0)
		{				/* ALTER INDEX */
			CONST char	*indexName, *tableName, *actionOptions;
			STRBUF		*sb = NULL;

			/* wtf no perm check, as per CREATE INDEX */
			if (!query->left ||
			    query->left->op != LIST_OP ||
			    !query->left->left ||
			    !query->left->right ||
			    !query->right ||
			    (query->right->op != NAME_OP &&
			     query->right->op != LIST_OP))
			{
				putmsg(MERR, Fn,
     "Internal error: Cannot get ALTER INDEX index, table or action/options");
				goto alterErr;
			}
			indexName = (query->left->left->op == NAME_OP ?
				     query->left->left->tname : CHARPN);
			tableName = (query->left->right->op == NAME_OP ?
				     query->left->right->tname : CHARPN);
			if (!(sb = openstrbuf()) ||
			    !(actionOptions = querytotext(sb, query->right)))
				*success = 0;
			else
				*success = TXalterIndexes(ddic, indexName,
							  tableName,
							  actionOptions);
			sb = closestrbuf(sb);
		}
		else if (strcmpi(query->tname, "user") == 0)
		{
			int	rc2;
			char	*user = NULL;
			char	*pass = NULL;

			/* Note that we only get here when the ALTER USER
			 * password is a parameter/expression; for literals
			 * it was already handled in sql1.y
			 */
			if (query->left && query->left->op == NAME_OP)
				user = query->left->tname;
			if (!user) return(NULL);
			pass = TXgetStringValFromQnode(ddic, fo, dbtbl,
						       query->right);
			if (!pass) return(NULL);
			rc2 = changeuser(ddic, user, pass);
			pass = TXfree(pass);
			*success = (rc2 == 0 ? 1 : 0);
		}
		else
		{
			putmsg(MERR+UGE, CHARPN, "Unknown ALTER target `%s'",
				query->tname);
			goto alterErr;
		}
		goto alterDone;
		/* ALTER USER handled directly in sql1.y; no QNODE created */
	alterErr:
		*success = 0;
	alterDone:
		strBuf = closestrbuf(strBuf);
		return(DBTBLPN);
	default:
		putmsg(MERR + UGE, Fn,
		       "Unimplemented feature QNODE_OP #%#x", (int)query->op);
		return (DBTBL *) NULL;
	}
}

/******************************************************************/

DBTBL *
TXpreparetree(ddic, query, fo, success, dbtbl)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
int *success;
DBTBL *dbtbl;
{
	IPREPTREEINFO *prepinfo;
	DBTBL *rc;
	static CONST char Fn[] = "TXpreparetree";
	char	msg[1024];

#ifdef ALLOW_EXPIRE_TIME
	static char ed[] = "@(#)wxyzFFFFFFFF";
	char *np;

	np = ed + 8;
	sscanf(np, "%08lx", &et);
	if ((unsigned long) time(NULL) > et)
	{
		putmsg(MWARN, NULL, "This Texis has expired");
		*success = 0;
		return NULL;
	}
#endif
	if (TXchecklicense(LIC_EXPIRE_TIME, 0, msg, sizeof(msg)) < 0)
	{
		putmsg(MWARN, NULL, "License violation: %s", msg);
		*success = 0;
		return NULL;
	}
	if(ddic)
		if(TXusestddic(ddic)!=0)
			putmsg(MERR, Fn, "TXusestddic failed for ddic=%i", ddic);

	prepinfo = (IPREPTREEINFO *)calloc(1, sizeof(IPREPTREEINFO));
	if(!prepinfo)
		return NULL;
	prepinfo->prepq = 1;
	prepinfo->preq = 0;
	prepinfo->fo = fo;
	prepinfo->dbtbl = dbtbl;
	prepinfo->ddic = ddic;
	prepinfo->allowbubble = 1;
	prepinfo->countonly = 0;
	prepinfo->stmthits = 0;
	prepinfo->analyze = ddic->optimizations[OPTIMIZE_ANALYZE];
	if(query->analyzed)
		prepinfo->analyze = 0;

	rc = ipreparetree(prepinfo, query, NULL, success);
	query->analyzed = 1;
	if (*success)				/* successfully executed */
	{
		/* Statement was already executed by ipreparetree(),
		 * eg. a SET, CREATE INDEX etc.  No results to be returned:
		 */
		query->countInfo.rowsMatchedMin = 
			query->countInfo.rowsMatchedMax = 0;
		query->countInfo.rowsReturnedMin = 
			query->countInfo.rowsReturnedMax = 0;
	}

	if(ddic->messages[MESSAGES_DUMP_QNODE])
	{
		HTBUF	*buf = openhtbuf();
		char	*s;

		if (ddic->messages[MESSAGES_DUMP_QNODE] == 2)
		{
			/* Do not print this first time; this is still
			 * the "set message='dumpqnode'" statement,
			 * user probably wants the next statement reported.
			 */
			ddic->messages[MESSAGES_DUMP_QNODE]--;
		}
		else
		{
			TXdumpqnode(buf, ddic, query, NULL, fo, 0);
			htbuf_getdata(buf, &s, 0);
			putmsg(MINFO, CHARPN, "QNODE tree after prep:\n%s", s);
			buf = closehtbuf(buf);
		}
	}

	free(prepinfo);
	return rc;
}



/******************************************************************/

static int dumpavail ARGS((QNODE *query, int depth));

static int
dumpavail(query, depth)
QNODE *query;
int	depth;
{
#ifdef ANALYZE_DEBUG
	int i;

	if(query->afldlist)
	{
		for(i=0; i < depth; i++)
			printf(" ");
		printf("available: ");
		if(query->afldlist->cnt > 1)
		{
			if(query->fldlist &&
			   slfind(query->fldlist, query->afldlist->s[0]))
				printf("*");
			printf("%s", query->afldlist->s[0]);
		}
		for(i=1; i < query->afldlist->cnt - 1; i++)
		{
			if(query->fldlist &&
			   slfind(query->fldlist, query->afldlist->s[i]))
				printf(", *%s", query->afldlist->s[i]);
			else
				printf(", %s", query->afldlist->s[i]);
		}
		printf("\n");
	}
#else
	(void)query;
	(void)depth;
#endif
	return 0;
#ifdef NEVER
			for(i=0; i < q->out->tbl->n; i++)
			{
				char *nm;

				nm = q->out->tbl->dd->fd[i].name;
				if(slfind(query->fldlist, nm))
				{
					printf("*");
					printf("%s ", nm);
				}
				else if (q->out->rname)
				{
					nm = TXstrcat3(q->out->rname,
						".", nm);
					if(slfind(query->fldlist, nm))
					{
						printf("*");
					}
					printf("%s ", nm);
/*
					if(nm)
						free(nm);
*/
				}
				else
					printf("%s ", nm);
				sladd(query->afldlist, nm);
			}
#endif
}

/******************************************************************/

int analyzetree ARGS((DDIC *ddic, QNODE *query, QNODE *parentquery, FLDOP *fo,
		      int depth));

int
analyzetree(ddic, query, parentquery, fo, depth)
DDIC *ddic;
QNODE *query;
QNODE *parentquery;
FLDOP *fo;
int depth;
{
#ifdef ANALYZE_DEBUG
	QUERY *q;
	char *s;
	int i;
#else

	(void)parentquery;
#endif

	if(!query)
		return 0;

#ifdef ANALYZE_DEBUG
	q = query->q;
	for(i=0; i < depth; i++)
		printf(" ");
#endif
	switch(query->op)
	{
		case PROJECT_OP:
#ifdef ANALYZE_DEBUG
			printf("PROJECT ");
			if(query->fldlist->cnt > 1)
				printf("%s", query->fldlist->s[0]);
			for(i=1; i < query->fldlist->cnt - 1; i++)
				printf(", %s", query->fldlist->s[i]);
			printf("\n");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case SELECT_OP:
#ifdef ANALYZE_DEBUG
			printf("SELECT ");
			if(query->fldlist)
			{
				if(query->fldlist->cnt > 1)
					printf("%s", query->fldlist->s[0]);
				for(i=1; i < query->fldlist->cnt - 1; i++)
					printf(", %s", query->fldlist->s[i]);
			}
			else
				printf("*");
			printf("\n");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case RENAME_OP:
#ifdef ANALYZE_DEBUG
			puts("RENAME");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case FOP_PROXIM:
#ifdef ANALYZE_DEBUG
			puts("LIKEP");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case NAME_OP:
#ifdef ANALYZE_DEBUG
			printf("TABLE %s ", query->tname);
			printf("require: ");
			if(parentquery && parentquery->fldlist &&
			   parentquery->fldlist->cnt > 1)
			{
				printf("%s", parentquery->fldlist->s[0]);
				for(i=1; i < parentquery->fldlist->cnt - 1; i++)
					printf(", %s", parentquery->fldlist->s[i]);
			}
			printf("\n");
			dumpavail(query, depth);
			if(q->out->index.btree && q->out->index.btree->datad)
			{
				printf(" index:[");
				for(i=0; i < q->out->index.btree->datad->n; i++)
					printf("%s ", q->out->index.btree->datad->fd[i].name);
				printf("]");
			}
			printf("\n");
#endif
			break;
		case ORDER_OP:
		case ORDERNUM_OP:
#ifdef ANALYZE_DEBUG
			printf("ORDER BY");
			for(i=0; q->order && i < q->order->n; i++)
			{
				char *x;

				x = TXpredflds(q->proj->preds[i]);
				printf(" %s", x);
				if(x)
					free(x);
			}
			printf("\n");
#endif
			analyzetree(ddic, query->right, query, fo, depth+1);
			break;
		case GROUP_BY_OP:
#ifdef ANALYZE_DEBUG
			printf("GROUP BY");
			for(i=0; i < q->proj->n; i++)
				printf(" %s", TXpredflds(q->proj->preds[i]));
			printf("\n");
#endif
			analyzetree(ddic, query->right, query, fo, depth+1);
			break;
		case DISTINCT_OP:
#ifdef ANALYZE_DEBUG
			printf("DISTINCT\n");
#endif
			analyzetree(ddic, query->right, query, fo, depth+1);
			break;
		case INSERT_OP:
			switch(query->right->op)
			{
			case COLUMN_OP:
			case LIST_OP:
			case VALUE_OP:
				break;
			default:
#ifdef ANALYZE_DEBUG
				printf("INSERT INTO ");
#endif
				analyzetree(ddic,query->left,query,fo,depth+1);
				analyzetree(ddic,query->right,query,fo,depth+1);
			}
			break;
		case PRODUCT_OP:
#ifdef ANALYZE_DEBUG
			printf("JOIN\n");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			analyzetree(ddic, query->right, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case UNION_OP:
#ifdef ANALYZE_DEBUG
			printf("UNION\n");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			analyzetree(ddic, query->right, query, fo, depth+1);
			dumpavail(query, depth);
			break;
		case TABLE_AS_OP:
#ifdef ANALYZE_DEBUG
			printf("TABLE AS\n");
#endif
			analyzetree(ddic, query->left, query, fo, depth+1);
			break;
		case PROP_OP:
			/* SET x = ... */
		case CREATE_OP:
			/* CREATE INDEX|USER .... */
		case DROP_OP:
			/* DROP ... */
		case TABLE_OP:
			/* CREATE TABLE ... (NOT AS SELECT) */
		case UPD_SEL_OP:
			/* UPDATE TABLE ... */
		case DEL_SEL_OP:
			/* DELETE FROM  TABLE ... */
			/* Not very interesting ops. */
			break;
		default:
			printf("op=%lx\n", (long)query->op);
	}
	if(query->afldlist)
		query->afldlist = slclose(query->afldlist);
	if(query->fldlist)
		query->fldlist = slclose(query->fldlist);
	return 0;
}


/******************************************************************/

#ifndef NO_TEST_JOIN_OPTIMIZE

static int counttables ARGS((DDIC *ddic, QNODE *query));

static int
counttables(ddic, query)
DDIC *ddic;
QNODE *query;
{
	int ntables = 0;

	if(!query)
		return 0;
	if(query->op == SELECT_OP)
	{
		ntables += counttables(ddic, query->left);
	}
	if(query->op == PRODUCT_OP)
	{
		ntables += counttables(ddic, query->left);
		ntables += counttables(ddic, query->right);
	}
	if(query->op == NAME_OP)
	{
		ntables ++;
	}
	return ntables;
}

/******************************************************************/

typedef struct JOITEM {
	char	*field;	/* Field of interest */
	char	*dep;	/* Table we depend on for value.  NULL for const */
	int	index;	/* Do we have an index to resolve. */
} JOITEM;

typedef struct JOTBINFO {
	char	*tname;	/* Name of the table */
	DBTBL	*tbl;
	int	nitems;	/* Number of items stored */
	int	aitems; /* Number of items alloced */
	JOITEM	*items;	/* Array of item info */
	int	ncindex;	/* Number of constant index */
	int	ndindex;	/* Number of dependent index */
	int	orpos;	/* Original position */
	int	ntables;
} JOTBINFO;

/******************************************************************/

static int poptables ARGS((DDIC *ddic, QNODE *query, JOTBINFO *jinfo,
			   int depth));

static int
poptables(ddic, query, jinfo, depth)
DDIC *ddic;
QNODE *query;
JOTBINFO *jinfo;
int depth;
{
	QUERY *q;
	int ntables = 0;

	if(!query)
		return 0;
	q = query->q;
	if(query->op == SELECT_OP)
	{
		ntables += poptables(ddic, query->left, jinfo, depth);
		depth=ntables;
	}
	if(query->op == PRODUCT_OP)
	{
		ntables += poptables(ddic, query->left, jinfo, depth);
		depth=ntables;
		ntables += poptables(ddic, query->right, jinfo, depth);
		depth=ntables;
	}
	if(query->op == NAME_OP)
	{
		jinfo[depth].tbl = q->out;
		jinfo[depth].tname = strdup(q->out->rname);
		depth++;
		ntables ++;
	}
	return ntables;
}

/******************************************************************/

static int reassigntables ARGS((DDIC *ddic, QNODE *query, JOTBINFO *jinfo,
				int depth, FLDOP *fo));

static int
reassigntables(ddic, query, jinfo, depth, fo)
DDIC *ddic;
QNODE *query;
JOTBINFO *jinfo;
int depth;
FLDOP *fo;
{
	QUERY *q;
	int ntables = 0;

	if(!query)
		return 0;
	q = query->q;
	if(query->op == SELECT_OP)
	{
		ntables += reassigntables(ddic, query->left, jinfo, depth, fo);
		depth=ntables;
		if(q->op == Q_CPRODUCT)
		{
			q->in1 = query->left->left->q->out;
			q->in2 = query->left->right->q->out;
			if(q->out)
				q->out = closedbtbl(q->out);
			preparequery(query, fo, 1);
		}
	}
	if(query->op == PRODUCT_OP)
	{
		ntables += reassigntables(ddic, query->left, jinfo, depth, fo);
		depth=ntables;
		ntables += reassigntables(ddic, query->right, jinfo, depth, fo);
		depth=ntables;
		if(q)
		{
			q->in1 = query->left->q->out;
			q->in2 = query->right->q->out;
			q->pred = closepred(q->pred);
			if(q->out)
				q->out = closedbtbl(q->out);
			preparequery(query, fo, 1);
		}
	}
	if(query->op == NAME_OP)
	{
		q->out = jinfo[depth].tbl;
		q->pred = closepred(q->pred);
		depth++;
		ntables ++;
	}
	return ntables;
}

/******************************************************************/

static int getpreds ARGS((DDIC *ddic, QNODE *query, PRED *pred,
			  JOTBINFO *jinfo));

static int
getpreds(ddic, query, pred, jinfo)
DDIC *ddic;
QNODE *query;
PRED *pred;
JOTBINFO *jinfo;
{
	int found = 0;
	int isconst = 0;
	char *fname, *t;

	if(!pred)
		return 0;
	switch(pred->lt)
	{
	case 'P':
		getpreds(ddic, query, pred->left, jinfo);
		break;
	case NAME_OP:
		if(dbnametofld(jinfo->tbl, pred->left))
		{
			found++;
		}
		break;
	case FIELD_OP:
		isconst++;
		break;
	default:
		break;
	}
	switch(pred->rt)
	{
	case (QNODE_OP)'P':
		getpreds(ddic, query, pred->right, jinfo);
		break;
	case NAME_OP:
		if(dbnametofld(jinfo->tbl, pred->right))
		{
			if (!found)
			{
			jinfo->items[jinfo->nitems].field = strdup(pred->right);
			if(pred->lt == NAME_OP)
			{
				jinfo->items[jinfo->nitems].dep =
					strdup(pred->left);
				t=strchr(jinfo->items[jinfo->nitems].dep, '.');
				if(t)
					*t = '\0';
				else
				{
					if(TXverbosity > 2)
						putmsg(MWARN, NULL, "Optimizer doesn't know table name for %s yet.", jinfo->items[jinfo->nitems].dep);
					*jinfo->items[jinfo->nitems].dep='\0';
				}
			}
			fname = dbnametoname(jinfo->tbl, pred->right, FTNPN,
					     INTPN);
			if(fname)
			{
				jinfo->items[jinfo->nitems].index =
				  ddgetindex(ddic,jinfo->tname,fname,NULL,NULL, NULL, CHARPPPN);
			}
			jinfo->nitems++;
			}
		}
		else if(found)
		{
			jinfo->items[jinfo->nitems].field = strdup(pred->left);
			jinfo->items[jinfo->nitems].dep = strdup(pred->right);
			t=strchr(jinfo->items[jinfo->nitems].dep, '.');
			if(t)
				*t = '\0';
			else
			{
				if(TXverbosity > 2)
					putmsg(MWARN, NULL, "Optimizer doesn't know table name for %s yet.", jinfo->items[jinfo->nitems].dep);
				*jinfo->items[jinfo->nitems].dep='\0';
			}
			fname = dbnametoname(jinfo->tbl, pred->left, FTNPN,
					     INTPN);
			if(fname)
			{
				jinfo->items[jinfo->nitems].index =
				  ddgetindex(ddic,jinfo->tname,fname,NULL,NULL, NULL, CHARPPPN);
			}
			jinfo->nitems++;
		}
		break;
	case FIELD_OP:
		if(found)
		{
			jinfo->items[jinfo->nitems].field = strdup(pred->left);
			fname = dbnametoname(jinfo->tbl, pred->left, FTNPN,
					     INTPN);
			if(fname)
			{
				jinfo->items[jinfo->nitems].index =
				  ddgetindex(ddic,jinfo->tname,fname,NULL,NULL, NULL, CHARPPPN);
			}
			jinfo->nitems++;
		}
	default:
		break;
	}
	return 0;
}

/******************************************************************/

static int joinfcmp ARGS((CONST void *, CONST void *));

static int
joinfcmp(av, bv)
CONST void *av, *bv;
{
	CONST JOTBINFO *a, *b;
	char *t;
	int i, cmp = 0;

	a = av; b = bv;
#ifdef NEVER
	putmsg(MINFO, "joinfcmp", "(%s)%d - (%s)%d", a->tname, a->ndindex, b->tname, b->ndindex);
#endif
	if(a->ncindex != b->ncindex)
		return b->ncindex - a->ncindex;
	if(b->ndindex != a->ndindex)
		return a->ndindex - b->ndindex;
	t = a->tname;
	/* KNG 20081217 Given a join `WHERE a.x = b.y' and both `x' and `y'
	 * are indexed, `a' is no "better" than `b' since they are mirror
	 * images.  Yet this loop would say `a < b', as well as `b < a'
	 * if called in the opposite argument order, making us violate
	 * trichotomy and the result of our qsort() be platform-dependent.
	 * Hold off returning until we do the mirror-image check in the
	 * next loop; if that says `a > b' then fall back to next check:
	 */
	for (i = 0; i < b->nitems; i++)
	{
		if(b->items[i].index > 0 && b->items[i].dep &&
		   !strcmp(b->items[i].dep, t))
		{
			cmp = -1;		/* a < b, but wait... */
			break;
		}
	}
	t = b->tname;
	for (i = 0; i < a->nitems; i++)
	{
		if(a->items[i].index > 0 && a->items[i].dep &&
		   !strcmp(a->items[i].dep, t) &&
		   cmp == 0)			/* ie !(a < b) from above */
			return 1;
	}
	return a->orpos - b->orpos;
}

/******************************************************************/

static JOTBINFO *closejotbinfo ARGS((JOTBINFO *));

static JOTBINFO *
closejotbinfo(jb)
JOTBINFO *jb;
{
	int i, j;

	if(jb)
	{
		for(i = 0; i < jb[0].ntables; i++)
		{
			if(jb[i].tname)
				free(jb[i].tname);
			if(jb[i].items)
			{
				for(j = 0; j < jb[i].aitems; j++)
				{
					if(jb[i].items[j].field)
						free(jb[i].items[j].field);
					if(jb[i].items[j].dep)
						free(jb[i].items[j].dep);
				}
				free(jb[i].items);
			}
		}
		free(jb);
	}
	return NULL;
}

/******************************************************************/

static JOTBINFO *getjoinfo ARGS((DDIC *ddic, QNODE *query));

static JOTBINFO *
getjoinfo(ddic, query)
DDIC *ddic;
QNODE *query;
{
	static CONST char Fn[] = "join_optimize_1";
	int nname, ntables, i, j, nmv = 0;
	PRED *p;
	QUERY *q;
	JOTBINFO *rc = NULL;

	if(!query)
		return rc;
	q = query->q;
	if(!q)
		return rc;

	p = q->pred;
	nname = TXpred_countnames(p);
	nname = nname << 1;
	ntables = counttables(ddic, query);
	if(ntables > 1)
	{
		rc = (JOTBINFO *)calloc(ntables, sizeof(JOTBINFO));
		if(!rc)
			return rc;
		rc[0].ntables = ntables;
		poptables(ddic, query, rc, 0);
		for(i=0; i < ntables; i++)
		{
#ifdef NEVER
			putmsg(MINFO, Fn, "Testing - %d: %s", i, rc[i].tname);
#endif
			rc[i].ntables = ntables;
			rc[i].items = (JOITEM *)calloc(nname, sizeof(JOITEM));
			rc[i].orpos = i;
			if(rc[i].items)
				rc[i].aitems = nname;
			rc[i].nitems = 0;
			getpreds(ddic, query, p, &rc[i]);
			for(j = 0; j < rc[i].nitems; j++)
			{
#ifdef NEVER
				putmsg(MINFO,Fn,"Testing - %d/%d: %s => %s %d",
					i,j,rc[i].items[j].field,
					    rc[i].items[j].dep,
					    rc[i].items[j].index);
#endif
				if(rc[i].items[j].dep)
					rc[i].ndindex += rc[i].items[j].index;
				else
					rc[i].ncindex += rc[i].items[j].index;
			}
		}
		qsort(rc, ntables, sizeof(JOTBINFO), joinfcmp);
		for(i=0; i < ntables; i++)
		{
			if(TXverbosity > 1)
				putmsg(MINFO, Fn, "Testing - %d(%d): %s", i,
					rc[i].orpos, rc[i].tname);
			for(j = 0; j < rc[i].nitems; j++)
			{
#ifdef NEVER
				putmsg(MINFO,Fn,"Testing - %d/%d: %s => %s %d",
					i,j,rc[i].items[j].field,
					    rc[i].items[j].dep,
					    rc[i].items[j].index);
#endif
				if(rc[i].orpos != i)
					nmv++;
			}
		}
#if defined(NEW_STUFF) && defined(NEVER)
/* Decide and assign indexes here ? */
		assindexpred();
		p->rev = rev;
		p->iname = strdup(iname[j]);
		p->itype = malloc(2);
		p->itype[0] = itype[j];
		p->itype[1] = '\0';
#endif
	}
	if(!nmv)
		return closejotbinfo(rc);
	return rc;
}

/******************************************************************/

static int
join_optimize(ddic, query, fo)
DDIC *ddic;
QNODE *query;
FLDOP *fo;
{
	static CONST char Fn[] = "join_optimize";
	QUERY *q;
	JOTBINFO *jotbinfo;

	if(!query)
		return 0;
	if(!ddic->optimizations[OPTIMIZE_JOIN])
		return 0;
	q = query->q;
	if(!q)
		return 0;
	if(q->op != Q_CPRODUCT)
		return 0;
	jotbinfo = getjoinfo(ddic, query);
	if(jotbinfo)
	{
		if(TXverbosity > 1)
			putmsg(MINFO, Fn, "Need rearrange");
		reassigntables(ddic, query, jotbinfo, 0, fo);
		if(query->op == SELECT_OP && q->pred)
		{
			TXclearpredvalid(q->pred);
			propagatepred(query->left, q->pred, fo);
		}
		closejotbinfo(jotbinfo);
	}
#ifndef NO_PRE_FIND_INDEX
	TXpredgetindx(q->pred, q->in1, q->in2);
#endif
	return 0;
}

#endif /* TEST_JOIN_OPTIMIZE */
