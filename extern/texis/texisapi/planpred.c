/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "os.h"
#ifdef NEED_GETOPT_H
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#endif
#if defined(sparc) && !defined(__SVR4)
extern int getopt ARGS((int argc,char **argv,char *opts));
extern char *optarg;
extern int optind, opterr, optopt;
#endif
#include "dbquery.h"
#include "texint.h"

/******************************************************************/
/*
	This structure contains the information about each index
	which is used in making the determination as to how and
	when to use it.
*/

#define MAX_PREDS	100

typedef struct PLPRED
{
	char	*iname;		/* File name to index */
	char	*sysindexParams;/* SYSINDEX.PARAMS for index */
	int	order;		/* How this index can order data for query */
	int	pct;		/* Estimate to pctage of rows returned */
	int	required;	/* Is this index required, i.e. must use */
	DBIDX	*index;		/* A pointer to the index */
	PRED	*pred;		/* The predicate that this index "does" */
	PROJ	*ord;		/* How it is ordered */
	int	op;		/* The operation at this point */
	int	this;		/* Number of this node */
	int	left, right;    /* Make a tree */
} PLPRED;

static PLPRED	plan[MAX_PREDS];

/******************************************************************/

PLPRED *
closeplpred(a)
PLPRED	*a;
{
	if(!a)
		return NULL;
	if(a->iname)
		free(a->iname);
	a->iname = NULL;
	if (a->sysindexParams != CHARPN)
	{
		free(a->sysindexParams);
		a->sysindexParams = CHARPN;
	}
	if(a->index)
	{
		closedbidx(a->index);
		free(a->index);
	}
	a->index = NULL;
	if(a->pred)
	{
#ifdef NEVER
		nullmms(a->pred);
#endif
		a->pred = closepred(a->pred);
	}
	if(a->ord)
		a->ord = closeproj(a->ord);
	return NULL;
}

/******************************************************************/
/*
	Comparison function to order the possible index choices.
*/

static int
plcmp(a, b)
PLPRED	*a;
PLPRED	*b;
{
	/* Get those that exist to the top */

	if(a->iname && !b->iname)
		return -1;
	if(!a->iname && b->iname)
		return 1;
	if(!a->iname && !b->iname)
		return 0;

	/* The required fields should be at the top */

	if(a->required && !b->required)
		return -1;
	if(!a->required && b->required)
		return 1;

	/* Get those which get the data in the right order up */

	if(a->order == 2 && b->order == 2)
		return a->pct - b->pct;
	if(a->order == 2 && b->order != 2)
		return -1;

	if(a->order == 0 && b->order)
		return -1;
	if(a->order && b->order == 0)
		return 1;


	/* Get those which will return the least highest */

	return (a->pct - b->pct);
}

/******************************************************************/
/*
	Do we show extra info as we do the work.
*/

static int	verbose = 0;

/******************************************************************/
/*
 *	Add an index to the list of possibly useful indices
 */
static int planaddindex ARGS((char *, CONST char *sysindexParams, int, int,
			      DBIDX *, int, PRED *, PROJ *));

static int
planaddindex(name, sysindexParams, order, pct, index, req, pred, ord)
char	*name;
CONST char	*sysindexParams;
int	order;
int	pct;
DBIDX	*index;
int	req;
PRED	*pred;
PROJ	*ord;
{
	int	i;

	for(i=0; i < 30 && plan[i].iname; i++)
	{
		if(name && !strcmp(plan[i].iname, name))
		{
			if(order > 0)
			{
				plan[i].order |= order;
				order |= plan[i].order;
			}
			else
				order = plan[i].order;
			if(req)
				plan[i].required = req;

		}
	}
	if(i < 30)
	{
		if(name)
			plan[i].iname = strdup(name);
		else
			plan[i].iname = strdup("");
		plan[i].sysindexParams = strdup(sysindexParams ?
						sysindexParams : "");
		if(order > 0)
		{
			plan[i].order = order;
			if(ord)
				plan[i].ord = dupproj(ord);
		}
		if(pct >= 0)
		{
			plan[i].pct = pct;
			plan[i].pred = duppred(pred);
		}
		else
			plan[i].pct = 100;
		if(index)
		{
			plan[i].index=createdbidx();
			memcpy(plan[i].index, index, sizeof(DBIDX));
		}
		plan[i].required = req;
		plan[i].this = i;
		return i;
	}
	return -1;
}

/******************************************************************/

static	int	indcombine ARGS((int, int, PRED *));

static int
indcombine(l, r, pred)
int	l, r;
PRED	*pred;
{
	int	lc, rc;
	int	op = pred->op;
	int	c;

	if(op == FOP_AND)
	{
		if(l < 0)
			return r;
		lc = plan[l].pct;
		if(r < 0)
			return r;
		rc = plan[r].pct;
		c = plcmp(&plan[l], &plan[r]);
		if(c <= 0)
		{
			return l;
		}
		else
		{
			PRED	*tp;

			if(verbose)
				putmsg(MINFO, NULL, "Re-order");
			tp = pred->left;
			pred->left = pred->right;
			pred->right = tp;
			return r;
		}
	}
	if(op == FOP_OR)
	{
		if(l < 0 || r < 0)
			return -1;
		rc = plan[l].pct + plan[r].pct;
		lc = planaddindex(NULL, CHARPN, -1, rc, NULL, 0, pred, NULL);
		c = plcmp(&plan[l], &plan[r]);
		if(c > 0)
		{
			PRED	*tp;

			if(verbose)
				putmsg(MINFO, NULL, "Re-order");
			tp = pred->left;
			pred->left = pred->right;
			pred->right = tp;
			return r;
		}
		return lc;
	}
	return -1;
}
/******************************************************************/
/*
	Determine which indexes can help us how to get into the
	else
		order = plan[i].order;
	correct order.
*/
/******************************************************************/

static int planorder ARGS((DBTBL *, PROJ *, FLDOP *));

static int
planorder(tb, order, fo)
DBTBL	*tb;
PROJ	*order;
FLDOP	*fo;
{
	int	ni, i, rev = 0;
	char	*fname=NULL;
	char	*itype, **iname = CHARPPN, **sysindexParamsVals = CHARPPN;
	PRED	*pred;
	int	rc = 0;

	if (!order)  /* Order not specced, so ... */
		return rc;
	if (!tb->rname)
		return rc;
/* Try and find an index which matches the order.  Initially look for
   one index matching a single order by */

	if (order->n != 1)
	{
		if(verbose)
			putmsg(MINFO, NULL, "Can't use index to help multi-field ORDER BY");
		return rc;
	}

	pred = order->preds[0];
	if (pred->op != 0 || pred->lt != NAME_OP)
		return 0;
	fname = strdup(pred->left);

/* Try to find an index ordered correctly. */
	ni = ddgetindex(tb->ddic, tb->rname, fname, &itype, &iname, NULL,
			&sysindexParamsVals);
	for (i=0; i < ni; i++)
		if (itype[i] == 'v')  /* WTF - JMT added nob */
		{
			if(verbose)
				putmsg(MINFO, NULL, "Found inverted index %s",
					iname[i]);
			planaddindex(iname[i], sysindexParamsVals[i], 1, -1,
				     NULL, 0, NULL, order);
			rc |= 1;
		}
	for (i=0; i < ni; i++)
		if (itype[i] == 'B')
		{
			if(verbose)
				putmsg(MINFO, NULL, "Found index %s", iname[i]);
			planaddindex(iname[i], sysindexParamsVals[i], 2, -1,
				     NULL, 0, NULL, order);
			rc |= 2;
		}
/* Try to find an index in the wrong order */
	if (fname && fname[strlen(fname)-1] == '-')
	{
		rev++;
		fname[strlen(fname)-1] = '\0';
	}
	else if(fname)
	{
		rev++;
		free(fname);
		fname = malloc(strlen(pred->left)+2);
		strcpy(fname, pred->left);
		strcat(fname, "-");
	}
	iname = TXfreeStrList(iname, ni);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, ni);
	if(itype)
		free(itype);
	ni = ddgetindex(tb->ddic, tb->rname, fname, &itype, &iname, NULL,
			&sysindexParamsVals);
	for (i=0; i < ni; i++)
		if (itype[i] == 'v')
		{
			if(verbose)
				putmsg(MINFO, NULL, "Found reversed inverted index %s",
					iname[i]);
			planaddindex(iname[i], sysindexParamsVals[i], 5, -1,
				     NULL, 0, NULL, order);
			rc |= 5;
		}
	for (i=0; i < ni; i++)
		if (itype[i] == 'B')
		{
			if(verbose)
				putmsg(MINFO, NULL, "Found reversed index %s",
					iname[i]);
			planaddindex(iname[i], sysindexParamsVals[i], 10,
				     -1, NULL, 0, NULL, order);
			rc |= 10;
		}
	if (fname)
	{
		free(fname);
		fname = NULL;
	}
	iname = TXfreeStrList(iname, ni);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, ni);
	if(itype)
		free(itype);
	return rc;
}

/******************************************************************/

/***********************************************

	What metamorph index can we use, and
	estimate of effort?

*************************************************/

#define EL(x) mmapi->mme->el[x]
#define NELS  mmapi->mme->nels

static int mmcost ARGS((FLD *));

#define	LONGWORD	15

static int
mmcost(mmfld)
FLD	*mmfld;
{
        DDMMAPI	*ddmmapi;
        MMAPI	*mmapi;
	int	cost = 0;
	int	indg, i, j, dkpm = 0, kpm = 0;

	ddmmapi = getfld(mmfld, NULL);
	if(ddmmapi)
		ddmmapi = ddmmapi->self;
	if (!ddmmapi) return 100;
        if (ddmmapi->query == (char *)NULL)
                return 100;
        mmapi = ddmmapi->mmapi;
	indg = 1;
	for(i=0; i < NELS; i++)
	{
		char **lst = (char **)(EL(i)->lst);
		size_t	sl;

		switch((EL(i)->pmtype))
		{
			case PMISPPM :
				kpm++;
				cost -= 5;
				for(j=0;j<EL(i)->lstsz;j++)
				{
					sl = strlen(lst[j]);
					if(sl >= LONGWORD)
						cost += 1;
					else
						cost += 2;
					if(strchr(lst[j], ' '))
						indg = 0;
				}
				break;
			case PMISSPM :
				kpm++;
				cost -= 5;
				sl = strlen(lst[0]);
				if(sl >= LONGWORD)
					cost += 1;
				else
					cost += 2;
				if(strchr(lst[0], ' '))
					indg = 0;
				break;
			default:
				dkpm ++;
				break;
		}
	}
	if(dkpm)
	{
		indg = 0;
		cost += 3 * dkpm;
	}
	if(indg == 0)
		cost += 10;
	if(cost < 1)
		cost = 1;
	if(cost > 100)
		cost = 100;
	if(kpm == 0)	/* Nothing to use index for */
		cost = 100;
	return cost;
}

/******************************************************************/

static char *mmindex ARGS((DBTBL *, PRED *, FLDOP *, int *,
			   char **sysindexParamsP));

static char	*
mmindex(table, pred, fo, cost, sysindexParamsP)
DBTBL	*table;
PRED	*pred;
FLDOP	*fo;
int	*cost;
char	**sysindexParamsP;	/* (out, opt.) SYSINDEX.PARAMS value */
{
	int	j;
	int	numindex;
	char	*dname;
	char	**iname = CHARPPN, *itype, **sysindexParamsVals = CHARPPN;

	if (sysindexParamsP) *sysindexParamsP = CHARPN;
	if(!pred)
		return NULL;
	if(pred->lt != NAME_OP)
		return NULL;

	dname = dbnametoname(table, pred->left, FTNPN, INTPN);
	if (!dname)
		return NULL;
	numindex=ddgetindex(table->ddic, table->rname, dname, &itype, &iname,
			    NULL, &sysindexParamsVals);
	for(j = 0; j < numindex; j++)
	{
		switch(itype[j])
		{
			case INDEX_3DB:
			case INDEX_FULL:
			case INDEX_MM:
			{
				char *rname;

				*cost = mmcost(pred->right);
				rname = iname[j];
				iname[j] = CHARPN;
				if (sysindexParamsP)
				{
					*sysindexParamsP=sysindexParamsVals[j];
					sysindexParamsVals[j] = CHARPN;
				}
				iname = TXfreeStrList(iname, numindex);
				sysindexParamsVals = TXfreeStrList(
						sysindexParamsVals, numindex);
				if (itype)
					free(itype);
				return rname;
			}
		}
	}
	iname = TXfreeStrList(iname, numindex);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numindex);
	if (itype) free(itype);
	return NULL;
}

/************************************************

	Try to figure out what we can do with
	these predicates and order

*************************************************/

int
TXplantablepred(tb, p, order, fo)
DBTBL	*tb;
PRED	*p;
PROJ	*order;
FLDOP	*fo;
{
	int	ordermask;
	int	rc;
	int	required = 0;
	char	*iname, *sysindexParams = CHARPN;

	if(!tb->rname)
		return -1;
	if(tb->type != 'T' && tb->type != 'S')
		return -1;
	ordermask = planorder(tb, order, fo);
	rc = TXtrybubble(tb, p, order, fo, NULL);
	if(rc != -1)
	{
		int	pct;

		pct = infodbidx(&tb->index);
		rc = planaddindex(tb->index.iname, tb->index.sysindexParams, -1, pct, &tb->index, 0, p, NULL);
		/* cleardbidx(&tb->index); */	/* memleak of FLDCMP */
		closedbidx(&tb->index);
		if(verbose)
		{
			putmsg(MINFO, NULL, "Found index to retrieve %d percent of index", pct);
		}
	}
	else if(p)
	{
		int	left, right;

		switch(p->op)
		{
			case FOP_OR:
			case FOP_AND:
			{
				left = TXplantablepred(tb, p->left, NULL, fo);
				right= TXplantablepred(tb, p->right, NULL, fo);
				rc = indcombine(left, right, p);
				break;
			}
			case FOP_NMM:
			case FOP_RELEV:
				required = 1;
			case FOP_PROXIM:
			case FOP_MM:
				iname = mmindex(tb, p, fo, &rc,
						&sysindexParams);
				if(iname)
				{
					if(verbose)
						putmsg(MINFO, NULL,
						    "Found metamorph index %s",
						    iname);
					rc = planaddindex(iname,
							  sysindexParams, -1,
							  rc, NULL,
							  required, p, NULL);
					free(iname);
					free(sysindexParams);
				}
				break;
			default:
				break;
		}
	}
	return rc;
}

void
TXcleanupplan()
{
	int	i;

	for(i=0; i < 30; i++)
		closeplpred(&plan[i]);
}

/******************************************************************/

void
TXshowplan()
{
	int i;

	qsort(plan, 30, sizeof(PLPRED), plcmp);
	if(plan[0].iname)
	{
		putmsg(MINFO, NULL, "Query Plan");
		putmsg(MINFO, NULL, "----------");
	}
	for(i=0; i < 30; i++)
	{
		if(plan[i].iname)
		{
			putmsg(MINFO, NULL, "%s %3d %4d %d %s",
			       plan[i].iname, plan[i].order, plan[i].pct,
			       plan[i].required, TXdisppred(plan[i].pred, 1, 0, 240));
			plan[i].iname = NULL;
		}
		if(plan[i].order == 2)
		{
			if(plan[i+1].iname)
			{
				putmsg(MINFO, NULL, "");
				putmsg(MINFO, NULL, "Alternate");
				putmsg(MINFO, NULL, "---------");
			}
		}
	}
}

#ifdef TEST

/******************************************************************/

static int planquery ARGS((QNODE *));

static int
planquery(query)
QNODE	*query;
{
	FLDOP	*fo;

	fo = dbgetfo();
	if(!query)
		return;
	planquery(query->left);
	planquery(query->right);
	if(query->q && query->q->in1)
		TXplantablepred(query->q->in1, query->q->pred, query->q->order, fo);
	return 0;
}

/******************************************************************/

main(argc, argv)
int	argc;
char	*argv[];
{
	APICP	*globalcp;
	char	*datasource = "";
	char	*sql = "";
	char	*passwd = "";
	char	*uid = "";
	int	c, i, ndpass = 0;
	void	*henv, *hdbc, *hstmt;

        while ((c = getopt(argc, argv, "a:cD:d:f:hi:l:mnP:p:qR:ru:Vvw:x?")) != -1)
        {
                switch(c)
		{
#ifdef DEBUG
			case 'D' :
				TXDebugLevel = atoi(optarg);
				break;
#endif
                        case 'p' :
                                ndpass--;
                                passwd = optarg;
                                break;
                        case 'u' :
                                ndpass++;
                                uid = optarg;
                                break;
                        case 'd' :
                                datasource = optarg;
                                break;
			default:
				break;
		}
	}
	TXget_globalcp();
        if (SQLAllocEnv(&henv) != SQL_SUCCESS)
                exit(1);
        if (SQLAllocConnect(henv, &hdbc) != SQL_SUCCESS)
        {
                SQLFreeEnv(henv);
		henv = NULL;
		globalcp = closeapicp(globalcp);
                exit(1);
        }
        if (SQLAllocStmt(hdbc, &hstmt) != SQL_SUCCESS)
        {
                SQLFreeConnect(hdbc);
                SQLFreeEnv(henv);
		hdbc = NULL;
		henv = NULL;
		globalcp = closeapicp(globalcp);
                exit(1);
        }
        if (SQLConnect(hdbc,
                        (UCHAR *)datasource, strlen(datasource),
                        (UCHAR *)uid, strlen(uid),
                        (UCHAR *)passwd, strlen(passwd)) != SQL_SUCCESS)
        {
                putmsg(MERR,"SQLConnect","Couldn't connect to %s",datasource);
                SQLFreeStmt(hstmt, SQL_DROP);
                SQLFreeConnect(hdbc);
                SQLFreeEnv(henv);
		hstmt = NULL;
		hdbc = NULL;
		henv = NULL;
		globalcp = closeapicp(globalcp);
                exit(1);
        }
	SQLPrepare(hstmt, (byte *)argv[optind], SQL_NTS);
	SQLFreeStmt(hstmt, SQL_DROP);
	SQLFreeConnect(hdbc);
	SQLFreeEnv(henv);
	hstmt = NULL;
	hdbc = NULL;
	henv = NULL;
	globalcp = closeapicp(globalcp);
	return 0;
}

#endif
