/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef EPI_HAVE_PWD_H
#  include <pwd.h>
#endif /* EPI_HAVE_PWD_H */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"
#include "sregex.h"
#include "fdbi.h"
#include "cgi.h"

#undef NO_SAFE_BTREE

#define MAX_FIELDS	10
typedef struct IOS_tag
{
	int op;
	PRED *preds[MAX_FIELDS];
	char *names[MAX_FIELDS];
}
IOS;

static char tempbuf[BT_MAXPGSZ];

static CONST char	IndexDoesNotExistFmt[] =
	"Index %s reported to exist, but does not";
static CONST char	CouldNotFindFieldInIndexFmt[] =
	"Could not find field %s in index %s";
static CONST char	WillNotUseIndexBtreeThresholdFmt[] =
  "Will not use index %s: match estimate %d%% exceeds btreethreshold %d";
static CONST char	CannotUseIndexNotSplitFmt[] =
	"Cannot use index %s for current query: indexvalues not split";
static CONST char	OpeningIndexFmt[] = "Opening index %s%s%s";
static CONST char	ToCreateIindexFmt[] = "%s to create IINDEX %p";
static CONST char       AndAndingFmt[] = " and ANDing with IINDEX %p";
static CONST char	MtStr[] = "";
#define INDEX_TRACE_VARS	\
  char	iiCreatingBuf[128], iiAndBuf[128], forBuf[65536]
#define II_CREATING_ARG(iin, tbspec)	((TXtraceIndexBits & 0x2000) ?	\
    (htsnpf(iiCreatingBuf, sizeof(iiCreatingBuf), ToCreateIindexFmt,	\
   ((tbspec) && (tbspec)->pind ? (htsnpf(iiAndBuf, sizeof(iiAndBuf),    \
    AndAndingFmt, (void *)(tbspec)->pind), iiAndBuf) : MtStr),          \
	    (void *)(iin)), iiCreatingBuf) : MtStr)
static CONST char	ForColOpParamFmt[] = " for `%s %s %s'";
#define FOR_OP_ARG(colName, op, paramStr, paramIsRhs)	\
  ((TXtraceIndexBits & 0x4000) ? (htsnpf(forBuf, sizeof(forBuf), \
   ForColOpParamFmt, ((paramIsRhs) ? (colName) : (paramStr)), \
   TXfldopname(op), ((paramIsRhs) ? (paramStr) : (colName))), \
   forBuf) : MtStr)

static CONST char	FieldNonexistentFmt[] = "Field %s non-existent";
static const char	FieldNonexistentInOrgFmt[] =
	"Field `%s' non-existent in original table";
static const char	TypeMismatchFmt[] =
	"Field `%s' has different types in original table and index";
static const char	ReturningIindexFmt[] =
	"Returning %s IINDEX %p after searching index `%s':";

/******************************************************************/

static int verbose = 0;

EPI_HUGEUINT TXindcnt = 0;		/* Last count of index records */

/******************************************************************/

int
TXshowindexes(v)
int v;
{
	int ov;

	ov = verbose;
	verbose = v;
	return ov;
}

/********************************************************************/
/*	Is this predicate good to use as an index expression?
 *	WTF - Only will use single comparisons against a constant.
 *
 *	Worse it expects the field on the left and the constant on
 *	the right (although this is the "normal" case.)
 *	KNG 20090311 fixed; can handle reverse case too
 *
 *	It also takes the constant and shoves it in fld.
 */

static int goodpred ARGS((DBTBL *tb, PRED *p, char *fname, FLD *fld,
			  FLDOP *fo, BTREE_SEARCH_MODE *searchMode));

static int
goodpred(tb, p, fname, fld, fo, searchMode)
DBTBL *tb;
PRED *p;
char *fname;	/* (in) name of first field in `tb' */
FLD *fld;	/* (in, out) first field in `tb': gets constant from `p' */
FLDOP *fo;
BTREE_SEARCH_MODE	*searchMode;	/* (out) mode to use for btsearch() */
{
	char *dname;

	*searchMode = BT_SEARCH_UNKNOWN;

	if (p->lt == NAME_OP && p->rt == FIELD_OP)
	{
		dname = dbnametoname(tb, p->left, FTNPN, INTPN);
		if (!dname)
			return 0;
		if (strcmp(dname, fname) != 0)	/* `p->left' is not `fname' */
			return 0;
		switch (p->op)
		{
		case FOP_EQ:			/* x = 2 */
			/* Copy `p->right' to `fld' for caller to seek to: */
			_fldcopy((FLD *) p->right, NULL, fld, NULL, fo);
			tb->ipred = p;		/* EOF when this is false */
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_GT:			/* x > 2 */
			_fldcopy((FLD *) p->right, NULL, fld, NULL, fo);
			/* Do not set `tb->ipred': no stopping point */
			*searchMode = BT_SEARCH_AFTER;
			return 1;		/* seek to `fld' */
		case FOP_GTE:			/* x >= 2 */
			_fldcopy((FLD *) p->right, NULL, fld, NULL, fo);
			/* Do not set `tb->ipred': no stopping point */
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_LT:			/* x < 2 */
		case FOP_LTE:			/* x <= 2 */
			tb->ipred = p;		/* EOF when this is false */
			return 0;		/* do not seek to `fld' */
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			_fldcopy((FLD *) p->right, NULL, fld, NULL, fo);
			return 1;
		default:
			return 0;
		}
	}
	if (p->lt == NAME_OP && p->rt == PARAM_OP)
	{
		PARAM *pa;

		pa = p->right;
		if (!pa->fld)
			return 0;
		dname = dbnametoname(tb, p->left, FTNPN, INTPN);
		if (!dname)
			return 0;
		if (strcmp(dname, fname) != 0)	/* `p->left' is not `fname' */
			return 0;
		switch (p->op)
		{
		case FOP_EQ:			/* x = $val */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			tb->ipred = p;
			*searchMode = BT_SEARCH_BEFORE;
			return 1;
		case FOP_GT:			/* x > $val */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			*searchMode = BT_SEARCH_AFTER;
			return 1;
		case FOP_GTE:			/* x >= $val */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			*searchMode = BT_SEARCH_BEFORE;
			return 1;
		case FOP_LT:			/* x < $val */
		case FOP_LTE:			/* x <= $val */
			tb->ipred = p;
			return 0;		/* do not seek to `fld' */
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			return 1;
		default:
			return 0;
		}
	}
	if (p->lt == FIELD_OP && p->rt == NAME_OP)
	{
		dname = dbnametoname(tb, p->right, FTNPN, INTPN);
		if (!dname)
			return 0;
		if (strcmp(dname, fname) != 0)	/* `p->right' is not `fname' */
			return 0;
		switch (p->op)
		{
		case FOP_EQ:			/* 2 = x */
			/* Copy `p->left' to `fld' for caller to seek to: */
			_fldcopy((FLD *) p->left, NULL, fld, NULL, fo);
			tb->ipred = p;		/* EOF when this is false */
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_GT:			/* 2 > x */
		case FOP_GTE:			/* 2 >= x */
			tb->ipred = p;		/* EOF when this is false */
			return 0;		/* do not seek to `fld' */
		case FOP_LT:			/* 2 < x */
			_fldcopy((FLD *) p->left, NULL, fld, NULL, fo);
			/* Do not set `tb->ipred': no stopping point */
			*searchMode = BT_SEARCH_AFTER;
			return 1;		/* seek to `fld' */
		case FOP_LTE:			/* 2 <= x */
			_fldcopy((FLD *) p->left, NULL, fld, NULL, fo);
			/* Do not set `tb->ipred': no stopping point */
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			_fldcopy((FLD *) p->left, NULL, fld, NULL, fo);
			return 1;
		default:
			return 0;
		}
	}
	if (p->lt == PARAM_OP && p->rt == NAME_OP)
	{
		PARAM *pa;

		pa = p->left;
		if (!pa->fld)
			return 0;
		dname = dbnametoname(tb, p->right, FTNPN, INTPN);
		if (!dname)
			return 0;
		if (strcmp(dname, fname) != 0)	/* `p->right' is not `fname'*/
			return 0;
		switch (p->op)
		{
		case FOP_EQ:			/* $val = x */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			tb->ipred = p;		/* EOF when this is false */
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_GT:			/* $val > x */
		case FOP_GTE:			/* $val >= x */
			tb->ipred = p;
			return 0;		/* do not seek to `fld' */
		case FOP_LT:			/* $val < x */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			*searchMode = BT_SEARCH_AFTER;
			return 1;		/* seek to `fld' */
		case FOP_LTE:			/* $val <= x */
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			*searchMode = BT_SEARCH_BEFORE;
			return 1;		/* seek to `fld' */
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			_fldcopy(pa->fld, NULL, fld, NULL, fo);
			return 1;
		default:
			return 0;
		}
	}
	return 0;
}

/******************************************************************/

int
TXpredgetindx(p, t1, t2)
PRED *p;
DBTBL *t1, *t2;
{
	char *x;

	if (!p)
		return 1;
	p->refc |= 2;
	switch (p->lt)
	{
	case 'P':
		TXpredgetindx(p->left, t1, t2);
		break;
	case NAME_OP:
		x = dbnametoname(t2, p->left, FTNPN, INTPN);
		if (x)		/* Field we're looking for */
		{
			p->indexcnt = ddgetindex(t2->ddic, t2->rname, x,
						 &p->itype, &p->iname, NULL,
						 CHARPPPN);
		}
		break;
	default:
		break;
	}
	switch (p->rt)
	{
	case 'P':
		TXpredgetindx(p->right, t1, t2);
		break;
	case NAME_OP:
		x = dbnametoname(t2, p->right, FTNPN, INTPN);
		if (x)		/* Field we're looking for */
		{
			p->indexcnt = ddgetindex(t2->ddic, t2->rname, x,
						 &p->itype, &p->iname, NULL,
						 CHARPPPN);
		}
		break;
	default:
		break;
	}
	return 0;
}

/******************************************************************/
/*
 *	Check names of predicate for the table.  This will walk
 *	through the predicate and verify that all the fields used
 *	exist within the given table.
 */

int
TXsetpredalts(p, t, add, v, alrank)
PRED *p;
DBTBL *t;
int add;
int v;
int alrank;
{
	FLD *f;

	if (!p)
		return 1;
	switch (p->lt)
	{
	case 'P':
		TXsetpredalts(p->left, t, add, v, alrank);
		break;
	case FIELD_OP:
		break;
	case NAME_OP:
		if (p->op == REG_FUN_OP)
			break;
		if (p->lvt == t)
			break;
		if (p->lnvt == t)
			break;
		f = dbnametofld(t, p->left);
		if (!f)
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			p->lnvt = t;
			break;
		}
		else if (!alrank && !strcmp(p->left, TXrankColumnName))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			break;
		}
		else
		{
			p->lvt = t;
			p->altleft = f;
			p->lat = FIELD_OP;
		}
		break;
	default:
		putmsg(MERR, "setpredalts",
		       "Strange value in p->lt: %d", (int) p->lt);
		return 1;
	}
	switch (p->rt)
	{
	case 'P':
		TXsetpredalts(p->right, t, add, v, alrank);
		break;
	case FIELD_OP:
		switch (p->op)
		{
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			{
				DDMMAPI *ddmmapi;
				size_t sz;

				if (!add)
					break;
				ddmmapi = getfld((FLD *) p->right, &sz);
				if (ddmmapi && ddmmapi->qtype == NAME_OP)
				{
#ifdef DEBUG
					DBGMSG(9,
					       (999, NULL, "Ruling on %s",
						ddmmapi->qdata));
#endif
					if (!dbnametoname(t, ddmmapi->qdata,
							  FTNPN, INTPN))
						break;
				}
			}
			break;
		default:
			break;
		}
		break;
	case NAME_OP:
		if (p->rvt == t)
			break;
		if (p->rnvt == t)
			break;
		f = dbnametofld(t, p->right);
		if (!f)
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->right);
			p->rnvt = t;
			break;
		}
		else if (!alrank && !strcmp(p->right, TXrankColumnName))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			break;
		}
		else
		{
			p->rvt = t;
			p->rat = FIELD_OP;
			p->altright = f;
		}
		break;
	default:
		if (p->op != NOT_OP)
		{
			putmsg(MERR, "setpredalts",
			       "Strange value in p->rt: %d", (int) p->rt);
			break;
		}
		break;
	}
	return 1;
}

/******************************************************************/
/*
 *	Check names of predicate for the table.  This will walk
 *	through the predicate and verify that all the fields used
 *	exist within the given table.
 */

int
TXsetprednames(p, t, add, v, alrank)
PRED *p;
DBTBL *t;
int add;
int v;
int alrank;
{
	if (!p)
		return 1;
	switch (p->lt)
	{
	case 'P':
		TXsetprednames(p->left, t, add, v, alrank);
		break;
	case FIELD_OP:
	case SUBQUERY_OP:
		break;
	case NAME_OP:
		if (p->op == REG_FUN_OP)
			break;
		if (p->lvt == t)
			break;
		if (p->lnvt == t)
			break;
		if (!dbnametoname(t, p->left, FTNPN, INTPN))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			p->lnvt = t;
			break;
		}
		else if (!alrank && !strcmp(p->left, TXrankColumnName))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			break;
		}
		else
			p->lvt = t;
		break;
	default:
		putmsg(MERR, "setprednames",
		       "Strange value in p->lt: %d", (int) p->lt);
		return 1;
	}
	switch (p->rt)
	{
	case 'P':
		TXsetprednames(p->right, t, add, v, alrank);
		break;
	case FIELD_OP:
		switch (p->op)
		{
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			{
				DDMMAPI *ddmmapi;
				size_t sz;

				if (!add)
					break;
				ddmmapi = getfld((FLD *) p->right, &sz);
				if (ddmmapi && ddmmapi->qtype == NAME_OP)
				{
#ifdef DEBUG
					DBGMSG(9,
					       (999, NULL, "Ruling on %s",
						ddmmapi->qdata));
#endif
					if (!dbnametoname(t, ddmmapi->qdata,
							  FTNPN, INTPN))
						break;
				}
			}
			break;
		default:
			break;
		}
		break;
	case NAME_OP:
		if (p->rvt == t)
			break;
		if (p->rnvt == t)
			break;
		if (!dbnametoname(t, p->right, FTNPN, INTPN))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->right);
			p->rnvt = t;
			break;
		}
		else if (!alrank && !strcmp(p->right, TXrankColumnName))
		{
			if (v)
				putmsg(MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			break;
		}
		else
			p->rvt = t;
		break;
	case SUBQUERY_OP:
		break;
	default:
		if (p->op != NOT_OP)
		{
			putmsg(MERR, "setprednames",
			       "Strange value in p->rt: %x", (int) p->rt);
			break;
		}
		break;
	}
	return 1;
}

/******************************************************************/

int
TXclearpredvalid(p)
PRED *p;
{
	if (!p)
		return 1;
	p->lvt = NULL;
	p->lnvt = NULL;
	p->rvt = NULL;
	p->rnvt = NULL;
	switch (p->lt)
	{
	case 'P':
		TXclearpredvalid(p->left);
	default:
		break;
	}
	switch (p->rt)
	{
	case 'P':
		TXclearpredvalid(p->right);
	default:
		break;
	}
	return 1;
}

static int
txColInOrgDbtbl(TXPMBUF *pmbuf, FTN dbtblFldType, DBTBL *orgDbtbl,
		char *colName)
{
	FTN	orgDbtblFldType;

	if (!dbnametoname(orgDbtbl, colName, &orgDbtblFldType, INTPN))
	{
		txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
			       FieldNonexistentInOrgFmt, colName);
		return(0);
	}
	/* Used to require FTN_VarBaseTypeMask (unknown reason),
	 * now just DDTYPEBITS for Bug 7207 fix
	 * (TXpredicateIsResolvableWithAltTable() call in TXsetupauxorder()):
	 */
	if ((dbtblFldType & DDTYPEBITS) != (orgDbtblFldType & DDTYPEBITS))
	{
		txpmbuf_putmsg(pmbuf, MWARN, NULL, TypeMismatchFmt, colName);
		return(0);
	}
	return(1);
}

static int
TXaddDdIdx(int *colsUsed, size_t n, int ddIdx)
/* Adds `ddIdx' (>= 0) to -1-terminated `colsUsed', keeping list unique.
 * Returns 0 on error.
 */
{
	size_t	i;

	if (ddIdx < 0) return(0);
	for (i = 0; i < n && colsUsed[i] != -1 && colsUsed[i] != ddIdx; i++);
	if (i + 1 < n)
	{
		if (colsUsed[i] == ddIdx) return(1);	/* already in list */
		colsUsed[i++] = ddIdx;
		colsUsed[i] = -1;		/* re-terminate list */
		return(1);
	}
	return(0);				/* no more room */
}

/******************************************************************/
/*
 *	Check validity of predicate for the table.  This will walk
 *	through the predicate and verify that all the fields used
 *	exist within the given table (returns 1 if so, 0 if not).
 */

static TXbool
TXispredvalidActual(
	TXPMBUF *pmbuf,	/* (out, opt.) buffer for messages */
	PRED *p,	/* (in) the predicate to validate */
	DBTBL *t,	/* (in) the table with source columns */
	int flags,	/* (in) 0x01: check Metamorph ops too (?)
			 *      0x02: allow $rank column
			 *	0x04: do not use/set `{l,r}[n]vt' fields
			 */
	DBTBL *orgDbtbl,/* (in, opt.) columns must exist here too and
			 * have same type
			 */
	int  *colsUsed)	/* (out, opt.) DD.fd[] indexes used from `t'
			 * (-1 terminated); must be `t->tbl->dd->n + 1'
			 * in length, -1-term. initially
			 */
{
	static const char	fn[] = "TXispredvalidActual";
	FTN			dbtblFldType;
	int			noVt = (flags & 0x4), ddIdx;

	if (!p)
		return(TXbool_True);
	if (!TXverbosepredvalid)
		pmbuf = TXPMBUF_SUPPRESS;
	switch (p->lt)
	{
	case 'P':
		if (!TXispredvalidActual(pmbuf, p->left, t, flags, orgDbtbl,
					 colsUsed))
			return(TXbool_False);
		break;
	case FIELD_OP:
		break;
	case NAME_OP:
		if (p->op == REG_FUN_OP || p->op == AGG_FUN_OP)
			break;
		if (!noVt && p->lvt == t)
			break;
		if (!noVt && p->lnvt == t)
			return(TXbool_False);
		if (!dbnametoname(t, p->left, &dbtblFldType, &ddIdx))
		{				/* `p->left' not in `t' */
			txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			if (!noVt) p->lnvt = t;
			return(TXbool_False);
		}
		if (!(flags & 0x2) && strcmp(p->left, TXrankColumnName) == 0)
		{
			txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			return(TXbool_False);
		}
		if (orgDbtbl &&
		    !txColInOrgDbtbl(pmbuf, dbtblFldType, orgDbtbl, p->left))

		{
			if (!noVt) p->lnvt = t;
			return(TXbool_False);
		}
		if (!noVt)
			p->lvt = t;
		if (colsUsed && ddIdx >= 0)
			TXaddDdIdx(colsUsed, t->tbl->dd->n, ddIdx);
		break;
	case SUBQUERY_OP:
		return(TXbool_True);
	default:
		putmsg(MERR, fn, "Strange value in p->lt: %d", (int)p->lt);
		return(TXbool_True);
	}
	switch (p->rt)
	{
	case 'P':
		if (!TXispredvalidActual(pmbuf, p->right, t, flags, orgDbtbl,
					 colsUsed))
			return(TXbool_False);
		break;
	case FIELD_OP:
		switch (p->op)
		{
		case FOP_MM:
		case FOP_NMM:
		case FOP_RELEV:
		case FOP_PROXIM:
		case FOP_MMIN:
			{
				DDMMAPI *ddmmapi;
				size_t sz;

				if (!(flags & 0x1))
					break;
				ddmmapi = getfld((FLD *) p->right, &sz);
				if (ddmmapi && ddmmapi->qtype == NAME_OP)
				{
#ifdef DEBUG
					DBGMSG(9,
					       (999, NULL, "Ruling on %s",
						ddmmapi->qdata));
#endif
					if (!dbnametoname(t, ddmmapi->qdata,
							&dbtblFldType, &ddIdx))
						return(TXbool_False);
					if (orgDbtbl &&
					    !txColInOrgDbtbl(pmbuf,
							     dbtblFldType,
						    orgDbtbl, ddmmapi->qdata))
						return(TXbool_False);
					if (colsUsed && ddIdx >= 0)
						TXaddDdIdx(colsUsed,
							t->tbl->dd->n, ddIdx);
				}
			}
			break;
		default:
			break;
		}
		break;
	case NAME_OP:
		if (!noVt && p->rvt == t)
			break;
		if (!noVt && p->rnvt == t)
			return(TXbool_False);
		if (!dbnametoname(t, p->right, &dbtblFldType, &ddIdx))
		{				/* `p->right' not in `t' */
			txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->right);
			if (!noVt) p->rnvt = t;
			return(TXbool_False);
		}
		if (!(flags & 0x2) && strcmp(p->right, TXrankColumnName) == 0)
		{
			txpmbuf_putmsg(pmbuf, MWARN + UGE, NULL,
				       FieldNonexistentFmt, p->left);
			return(TXbool_False);
		}
		if (orgDbtbl &&
		    !txColInOrgDbtbl(pmbuf, dbtblFldType, orgDbtbl, p->right))

		{
			if (!noVt) p->rnvt = t;
			return(TXbool_False);
		}
		if (!noVt)
			p->rvt = t;
		if (colsUsed && ddIdx >= 0)
			TXaddDdIdx(colsUsed, t->tbl->dd->n, ddIdx);
		break;
	case SUBQUERY_OP:
		return(TXbool_True);
	default:
		switch (p->op)
		{
		case NOT_OP:
		case RENAME_OP:
			break;
		default:
			/* wtf seem to benignly end up with op 0 when
			 * called from Bug 4768 fix; pred_eval() seems
			 * to ignore as well?
			 */
			if (orgDbtbl) return(TXbool_True);

			putmsg(MERR, fn, "Strange value in p->rt: %d",
				 (int)p->rt);
			return(TXbool_False);
		}
		break;
	}
	return(TXbool_True);
}

TXbool
TXispredvalid(TXPMBUF *pmbuf, PRED *p, DBTBL *t, int flags, DBTBL *orgDbtbl,
	      int *colsUsed)
{
	if (colsUsed)
	{
		memset(colsUsed, 0, (t->tbl->dd->n + 1)*sizeof(int));
		colsUsed[0] = -1;
	}
	return(TXispredvalidActual(pmbuf, p, t, flags, orgDbtbl, colsUsed));
}

/******************************************************************/
/*
 *	Make a valid predicate for the table.  This expects an
 *	"optimized" predicate, as would be given to settablepred
 *	and checks each term to see if it is valid.  It builds a
 *	new predicate by duplicating the valid predicates.
 *
 *	Bugs:
 *	This is currently a half-assed implementation, in that it
 *	will either return the whole predicate, or none at all. It
 *	makes no attempt to sort out which part of the predicate
 *	should be used.  Maybe if there is an invalid part of the
 *	predicate it should "choose" a single valid part?  This will
 *	make a good first step, and if an optimizer exists, then it
 *	can be used successively to order the query for even greater
 *	optimizations.
 *
 *	Currently an invalid predicate means that some fields do
 *	not exist within the specified table.  Most likely due to
 *	a join.
 */

PRED *
TXmakepredvalid(p, t, add, v, alrank)
PRED *p;
DBTBL *t;
int add;
int v;
int alrank;
{
	PRED *tp;
	PRED *np = p;
	int rc, flags;

	if (!p)
		return p;
	flags = 0;
	if (add) flags |= 0x1;
	if (alrank) flags |= 0x2;
#ifndef ALLOW_MISSING_FIELDS
	rc = TXispredvalid((v ? t->ddic->pmbuf : TXPMBUF_SUPPRESS), p, t,
			   flags, DBTBLPN, INTPN);
#else
	rc = TXispredvalid(TXPMBUF_SUPPRESS, p, t, flags, DBTBLPN, INTPN);
#endif
	if (!rc)
	{
#ifdef DEBUG
		DBGMSG(5, (MINFO, "ispredvalid", "Failure"));
#endif
/* WTF - Let's actually try and make it valid */
		switch (p->op)
		{
		case FOP_AND:
			tp = TXmakepredvalid(p->left, t, add, v, alrank);
			if (tp)
				return tp;
			tp = TXmakepredvalid(p->right, t, add, v, alrank);
			if (tp)
				return tp;
			break;
		default:
			break;
		}
		return (PRED *) NULL;
	}
	return np;
}

/******************************************************************/

PRED *
TXduppredvalid(p, t, add, v, alrank)
PRED *p;
DBTBL *t;
int add;
int v;
int alrank;
{
	static CONST char	fn[] = "TXduppredvalid";
	PRED *lp, *rp;
	PRED *np = p;
	int rc, flags;

	if (!p)
		return p;
	flags = 0;
	if (add) flags |= 0x1;
	if (alrank) flags |= 0x2;
#ifndef ALLOW_MISSING_FIELDS
	rc = TXispredvalid((v ? t->ddic->pmbuf : TXPMBUF_SUPPRESS), p, t,
			   flags, DBTBLPN, INTPN);
#else
	rc = TXispredvalid(TXPMBUF_SUPPRESS, p, t, flags, DBTBLPN, INTPN);
#endif
	if (!rc)
	{
#ifdef DEBUG
		DBGMSG(5, (MINFO, "ispredvalid", "Failure"));
#endif
/* WTF - Let's actually try and make it valid */
		switch (p->op)
		{
		case FOP_AND:
			lp = TXduppredvalid(p->left, t, add, v, alrank);
			rp = TXduppredvalid(p->right, t, add, v, alrank);
			if (!lp)
				return rp;
			if (!rp)
				return lp;
			np = (PRED *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PRED));
			if (!np) return(NULL);	/* wtf */
			np->op = FLDMATH_AND;
			np->lt = p->lt;
			np->rt = p->rt;
			np->left = lp;
			np->right = rp;
			return np;
		default:
			break;
		}
		return (PRED *) NULL;
	}
	else
	{
		return duppred(np);
	}
}

/******************************************************************/

PRED *
TXclosepredvalid2(p)
PRED *p;
{
	if (p)
	{
		if (p->lt == 'P')
			TXclosepredvalid2(p->left);
		if (p->rt == 'P')
			TXclosepredvalid2(p->right);
		if (p->refc == 1)
		{
			p->itype = TXfree(p->itype);
			if (p->iname)
			{
				int i;

				for (i = 0; i < p->indexcnt; i++)
					p->iname[i] = TXfree(p->iname[i]);
				p->iname = TXfree(p->iname);
			}
			p = TXfree(p);
		}
	}
	return NULL;
}

/******************************************************************/

PRED *
TXduppredvalid2(p, t, add, v, alrank)
PRED *p;
DBTBL *t;
int add;
int v;
int alrank;
{
	static CONST char	fn[] = "TXduppredvalid2";
	PRED *lp, *rp;
	PRED *np = p;
	int rc, flags;

	if (!p)
		return p;
	flags = 0;
	if (add) flags |= 0x1;
	if (alrank) flags |= 0x2;
#ifndef ALLOW_MISSING_FIELDS
	rc = TXispredvalid((v ? t->ddic->pmbuf : TXPMBUF_SUPPRESS), p, t,
			   flags, DBTBLPN, INTPN);
#else
	rc = TXispredvalid(TXPMBUF_SUPPRESS, p, t, add, 0, alrank, DBTBLPN,
			   INTPN);
#endif
	if (!rc)
	{
#ifdef DEBUG
		DBGMSG(5, (MINFO, "ispredvalid", "Failure"));
#endif
/* WTF - Let's actually try and make it valid */
		switch (p->op)
		{
		case FOP_AND:
			lp = TXduppredvalid2(p->left, t, add, v, alrank);
			rp = TXduppredvalid2(p->right, t, add, v, alrank);
			if (!lp)
				return rp;
			if (!rp)
				return lp;
			np = (PRED *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PRED));
			if (!np) return(NULL);	/* wtf */
			np->op = FLDMATH_AND;
			np->lt = p->lt;
			np->rt = p->rt;
			np->left = lp;
			np->right = rp;
			np->refc = 1;
			return np;
		default:
			break;
		}
		return (PRED *) NULL;
	}
	else
	{
		return np;
	}
}

/******************************************************************/
/*	If the table is a btree index, and part of the predicate
 *	matches the key of the table then the table is positioned
 *	at the first matching record
 */

static void dobtindx ARGS((DBTBL *, PRED *, PROJ *, FLDOP *));

static void
dobtindx(tb, rp, order, fo)
DBTBL *tb;
PRED *rp;
PROJ *order;
FLDOP *fo;
{
	size_t i;
	char *fname;
	FLD *fld;
	byte buf[BT_MAXPGSZ];
	PRED *cp = rp;
	size_t sz;
	BTREE_SEARCH_MODE	searchMode = BT_SEARCH_UNKNOWN;
	DBTBL	*saveBtreeLogDbTbl;

	saveBtreeLogDbTbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = tb;			/* for btreelog debug */

	(void)order;
	if (!rp) goto done;

	/* Get the first field of `tb', since that is the primary key: */
	fname = getfldname(tb->tbl, 0);
	fld = dbnametofld(tb, fname);

	i = BT_MAXPGSZ;
	btgetnext(tb->index.btree, &i, buf, NULL);
	buftofld(buf, tb->tbl, i);	/* Get a row from the table */
	TXrewinddbtbl(tb);
	while (cp->op == FOP_AND)
	{
		if (goodpred(tb, cp->right, fname, fld, fo, &searchMode))
		{
			/* Seek the B-tree to the value in `fld': */
			/* KNG 20090311 Bug 2537 but first set OF_DONT_CARE
			 * on fields beyond `fname', so that btsearch() does
			 * not look for them in addition to `fname' (which
			 * is the only field we've validated via goodpred());
			 * later fields have junk from buftofld() above:
			 */
			TXsetdontcare((FLDCMP *)tb->index.btree->usr,
					1, 1, OF_DONT_CARE);
			sz = fldtobuf(tb->tbl);
			btsetsearch(tb->index.btree, searchMode);
			btsearch(tb->index.btree, sz, tb->tbl->orec);
		}
		cp = cp->left;
	}
	if (goodpred(tb, cp, fname, fld, fo, &searchMode))
	{
		/* Seek the B-tree to the value in `fld': */
		/* KNG 20090311 Bug 2537 set OF_DONT_CARE first: */
		TXsetdontcare((FLDCMP *)tb->index.btree->usr,
				1, 1, OF_DONT_CARE);
		sz = fldtobuf(tb->tbl);
		btsetsearch(tb->index.btree, searchMode);
		btsearch(tb->index.btree, sz, tb->tbl->orec);
	}

done:
	TXbtreelog_dbtbl = saveBtreeLogDbTbl;	/* for btreelog debug */
	return;
	/* If we found a good pred, we should be correctly positioned. */
}

/******************************************************************/
/*	Remove the association between a table and index
 */

static int rmindex ARGS((DBTBL *));

static int
rmindex(tb)
DBTBL *tb;
{
	if (tb->index.btree)
	{
#ifndef NO_BUBBLE_INDEX
		closedbidx(&tb->index);
#else
		if (tb->index.btree->usr)
			tb->index.btree->usr =
				closefldcmp(tb->index.btree->usr);
		tb->index.btree = closebtree(tb->index.btree);
#endif
	}
	return 0;
}

/******************************************************************/
/*	Free an IINODE structure, and all contents.
 */

static IINODE *closeiinode ARGS((IINODE *, int));

static IINODE *
closeiinode(in, rmc)
IINODE *in;
int rmc;
{
	if (in)
	{
#ifdef CACHE_IINODE
		if (!rmc && in->cached)
			return NULL;
#endif
		if (in->left)
			in->left = closeiinode(in->left, rmc);
		if (in->right)
			in->right = closeiinode(in->right, rmc);
		if (in->index)
		{
			FLDCMP *fc;

			if (in->index->orig)
			{
#ifdef NEW_I
#else
				fc = in->index->orig->usr;
				in->index->orig->usr = TXclosefldcmp(fc);
#endif
			}
			else
				fc = NULL;
			in->index = closeiindex(in->index);
		}
		if (in->fgpred && in->gpred)
			in->gpred = TXfree(in->gpred);
		in = TXfree(in);
	}
	return NULL;
}

/******************************************************************/

IINODE *
TXcloseiinode(in)
IINODE *in;
{
	return closeiinode(in, 1);
}

/******************************************************************/

static IINDEX *ixbttwindex ARGS((int itype, char *iname,
		CONST char *sysindexParams, FLD *fld, char *fname,
		FLDOP *fo, DBTBL *dbtbl));

static IINDEX *
ixbttwindex(itype, iname, sysindexParams, fld, fname, fo, dbtbl)
int itype;			/* Type of index */
char *iname;			/* Name of index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS for index */
FLD *fld;			/* Field containing value to search for */
char *fname;			/* Name of the field */
FLDOP *fo;			/* A fldop structure to use */
DBTBL *dbtbl;			/* The table the index belongs to */
/* Returns a RAM B-tree with items from index `iname' that match `fld'.
 * Optimized for the BETWIXT condition.
 */
{
	static CONST char Fn[] = "ixbttwindex";
	size_t sz;
	size_t sz2;
	FLD *fld1, *fld2;
	FTN	itemType;
	FLDCMP *fc;
	IINDEX *ix = NULL;
	BTLOC btloc;
	BTREE *bt = NULL, *rc = NULL;
	EPI_HUGEUINT nrecs;
	long llat, clat, hlat;
	long llon, clon, hlon;
	long code;
	void	*v;
	INDEX_TRACE_VARS;

	(void)itype;
#ifdef MEMDEBUG
	mac_ovchk();
#endif
	switch (dbtbl->type)
	{
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		goto err;
	}
	if (!fld) goto err;			/* should always have `fld' */
	ix = openiindex();
	if (!ix) goto err;
	if (verbose)
		putmsg(MINFO, Fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, TBSPECPN),
		       FOR_OP_ARG(fname, FOP_TWIXT, fldtostr(fld), 1));
	if (!existsbtree(iname)) goto missingIndex;
	rc = TXbtcacheopen(dbtbl, iname, INDEX_BTREE, INDEX_READ,
			   sysindexParams);
	if (!rc)
	{
	missingIndex:
		putmsg(MWARN, Fn, IndexDoesNotExistFmt, iname);
		goto err;
	}
	bt = openbtree(NULL, BT_MAXPGSZ, 20, BT_FIXED, O_RDWR | O_CREAT);
	if (!bt) goto err;
	/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
	BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt->params, dbtbl->ddic);

	fc = TXopenfldcmp(rc, TXOPENFLDCMP_INTERNAL_FLDOP);
	if (!fc) goto err;
	TXsetdontcare(fc, 1, 1, OF_DONT_CARE);	/* ignore all but 1st field */
	rc->usr = fc;
	btsetcmp(rc, (btcmptype) fldcmp);

	fld1 = nametofld(fc->tbl1, fname);
	if (!fld1)
	{
		putmsg(MWARN, Fn, CouldNotFindFieldInIndexFmt, fname, iname);
		goto err;
	}
	nrecs = 0;

	/* Create `fld2', with the same type as the return of
	 * `TXfldGetNextItem(fld, ...)', e.g. strlst `fld' comes back
	 * as varchar, multiple-item items come back singly:
	 */
	TXfldIsMultipleItemType(fld, NULL, &itemType);
	fld2 = emptyfld(itemType, 1);
	if (!fld2) goto err;
	/* We want exactly `itemType'; FTN_NotNullableFlag may defeat
	 * locfldcmp():
	 */
	if (!(itemType & FTN_NotNullableFlag))
		fld2->type &= ~FTN_NotNullableFlag;

	v = TXfldGetNextItem(fld, NULL, 0, &sz);    /* low BETWIXT limit */
	if ((v = TXfldGetNextItem(fld, v, sz, &sz)) != NULL)
	{					/* high BETWIXT limit */
		code = *(long *) v;
		TXcode2ll(code, &hlat, &hlon);
		putfld(fld2, v, sz);
		_fldcopy(fld2, NULL, fld1, NULL, fo); /* fld1 = fld2*/
		sz = fldtobuf(fc->tbl1);
		btsetsearch(rc, BT_SEARCH_AFTER);
		btloc = btsearch(rc, sz, fc->tbl1->orec);
		sz2 = sizeof(tempbuf);
		btloc = btgetnext(rc, &sz2, tempbuf, NULL);
		if (TXrecidvalid(&btloc))
		{
			btreesetmarker(rc);
		}
		rc->hcode = code;
		v = TXfldGetNextItem(fld, NULL, 0, &sz); /*low BETWIXT limit*/
		code = *(long *) v;
		rc->lcode = code;
		TXcode2ll(code, &llat, &llon);
		btsetsearch(rc, BT_SEARCH_BEFORE);
		putfld(fld2, v, sz);
		_fldcopy(fld2, NULL, fld1, NULL, fo);	/* fld1 = fld2 */
		sz = fldtobuf(fc->tbl1);
		if (verbose)
			putmsg(MINFO, NULL, "Betwixt %ld and %ld",
			       (long)rc->lcode, (long)rc->hcode);
		btloc = btsearch(rc, sz, fc->tbl1->orec);
		while ((sz2 = sizeof(tempbuf)),
		       (btloc = btgetnext(rc, &sz2, tempbuf, NULL)),
		        TXrecidvalid(&btloc))
		{
			code = *(long *) tempbuf;
			TXcode2ll(code, &clat, &clon);
			if (clat >= llat && clat <= hlat &&
			    clon >= llon && clon <= hlon)
			{
				btinsert(bt, &btloc, sizeof(btloc), &btloc);
				nrecs++;
			}
		}
	}
	fld2 = closefld(fld2);

	rewindbtree(bt);
#ifdef NEW_I
	ix->mirror = dbidxfrombtree(bt, DBIDX_BTREE);
#else
	ix->mirror = bt;
#endif
	bt = NULL;				/* owned by `ix' now */
	ix->cntorig = nrecs;
	goto done;

err:
	ix = closeiindex(ix);
done:
	if (rc)
	{
		rc->usr = TXclosefldcmp(rc->usr);
		rc = TXbtcacheclose(dbtbl, iname, INDEX_BTREE, INDEX_READ, rc);
	}
	if (bt)
	{
		bt->usr = TXclosefldcmp(bt->usr);
		bt = closebtree(bt);
	}
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return ix;
}

/* ----------------------------------------------------------------------- */

typedef struct	FLDITEM_tag
{
	void	*data;				/* item data */
	size_t	sz;				/* item size */
	size_t	count;				/* count of item in `fld' */
	byte	type;				/* qsort() user data */
	byte	indexIsDesc;			/* "" */
}
FLDITEM;

static int TXfldItemCmp ARGS((CONST void *a, CONST void *b));
static int
TXfldItemCmp(a, b)
CONST void	*a;
CONST void	*b;
/* qsort() callback for sorting FLDITEMs.
 */
{
	static CONST char	fn[] = "TXfldItemCmp";
	FLDITEM			*itemA = (FLDITEM *)a, *itemB = (FLDITEM *)b;
	int			ret;

	/* All items are assumed to be the same type: */
	switch (itemA->type & DDTYPEBITS)
	{
	case FTN_BYTE:
		ret = memcmp(itemA->data, itemB->data,
			     TX_MIN(itemA->sz, itemB->sz));
		if (ret) break;
		if (itemA->sz < itemB->sz) ret =-1;
		else if (itemA->sz > itemB->sz) ret = 1;
		else ret = 0;
		break;
	case FTN_CHAR:
		ret = TXstringcompare(itemA->data, itemB->data,
				      itemA->sz, itemB->sz);
		break;
	case FTN_DOUBLE:
		/* from fldops.c COM() for fododo(): */
#define COM(a, b)					\
  (TXDOUBLE_IS_NaN(a) ? (TXDOUBLE_IS_NaN(b) ? 0 : 1) :	\
                         (TXDOUBLE_IS_NaN(b) ? -1 :	\
  (TXDOUBLE_ISGT(a, b) ? 1 : (TXDOUBLE_ISLT(a, b) ? -1 : 0))))
		ret = COM(*(ft_double *)itemA->data,
			  *(ft_double *)itemB->data);
		break;
#undef COM

	case FTN_FLOAT:
		/* from fldops.c COM() for foflfl(): */
#define COM(a, b)					\
  (TXFLOAT_IS_NaN(a) ? (TXFLOAT_IS_NaN(b) ? 0 : 1) :	\
                         (TXFLOAT_IS_NaN(b) ? -1 :	\
  (TXFLOAT_ISGT(a, b) ? 1 : (TXFLOAT_ISLT(a, b) ? -1 : 0))))
		ret = COM(*(ft_float *)itemA->data,
			  *(ft_float *)itemB->data);
		break;
#undef COM

#define VAL(i, type)	(*(type *)(i)->data)
#define CMP(a, b, type)	{				\
  if (VAL(a, type) < VAL(b, type)) ret = -1;		\
  else if (VAL(a, type) > VAL(b, type)) ret = 1;	\
  else ret = 0;	}
	case FTN_DATE:		CMP(itemA, itemB, ft_date);	break;
	case FTN_INT:		CMP(itemA, itemB, ft_int);	break;
	case FTN_INTEGER:	CMP(itemA, itemB, ft_integer);	break;
	case FTN_LONG:		CMP(itemA, itemB, ft_long);	break;
	case FTN_SHORT:		CMP(itemA, itemB, ft_short);	break;
	case FTN_SMALLINT:	CMP(itemA, itemB, ft_smallint);	break;
	case FTN_WORD:		CMP(itemA, itemB, ft_word);	break;
	case FTN_DWORD:		CMP(itemA, itemB, ft_dword);	break;
	case FTN_COUNTER:
		CTRCMP(&VAL(itemA, ft_counter), &VAL(itemB, ft_counter), ret);
		break;
	case FTN_INT64:		CMP(itemA, itemB, ft_int64);	break;
	case FTN_UINT64:	CMP(itemA, itemB, ft_uint64);	break;

	case FTN_DECIMAL:
	case FTN_BLOB:
	case FTN_HANDLE:
	case FTN_INDIRECT:
	case FTN_BLOBI:
	case FTN_STRLST:			/* should not happen */
	case FTN_DATESTAMP:
	case FTN_TIMESTAMP:
	case FTN_DATETIME:
	case FTN_COUNTERI:
	case FTN_RECID:
	case FTN_INTERNAL:
	case FTN_BLOBZ:
	default:
		putmsg(MWARN, fn, "Unhandled type %s",
			ddfttypename(itemA->type));
		ret = 0;
	}
	if (itemA->indexIsDesc) ret = -ret;
	return(ret);
#undef VAL
#undef CMP
}

static EPI_HUGEINT TXgetIndexRowsForItem ARGS((BTREE *results,
	BTREE *indexBtree,
        FLD *indexFld, FLDITEM *item, FLD *itemFld, FLDCMP *fc, FLDOP *fo,
        int doSubset, int deDupIndexResults, int useStrlstCreate));
static EPI_HUGEINT
TXgetIndexRowsForItem(results, indexBtree, indexFld, item, itemFld, fc, fo,
		      doSubset, deDupIndexResults, useStrlstCreate)
BTREE	*results;	/* (out) RAM B-tree for results */
BTREE	*indexBtree;	/* (in) B-tree index */
FLD	*indexFld;  	/* (in) field from `indexBtree' */
FLDITEM	*item;		/* (in, opt.) item to search for (NULL: all rows) */
FLD	*itemFld;	/* (in) field to put `item' data in for searching */
FLDCMP	*fc;		/* (in) FLDCMP for comparisons */
FLDOP	*fo;		/* (in) FLDOP for fldmath ops */
int	doSubset;
int	deDupIndexResults;
int	useStrlstCreate;
/* Looks for rows in `indexBtree' that match `item' (all rows if NULL),
 * and adds to `results'.
 * Returns number of rows found, or -1 on error or exceeding btreethreshold
 * (abandon index usage).
 */
{
	static CONST char	fn[] = "TXgetIndexRowsForItem";
	BTLOC			curLoc, prevLoc, cntLoc;
	EPI_HUGEINT		itemCountThisRow, totalRecs;
	int			res, loMark, hiMark;
	char			*indexFldBuf = NULL;
	size_t			curKeySz, indexFldBufSz = 0;
	char			*curKeyBuf = NULL;
	TXstrlstCharConfig	saveSep, sepToUse;

	if (item)				/* looking for `item' */
	{
		putfld(itemFld, item->data, item->sz);
		/* Use create mode for `strlstCol subset $oneItem' when
		 * there is a non-empty item and index is non-split-value;
		 * use lastchar otherwise (current "user" TXApp setting
		 * may not be appropriate?), e.g. empty `item' with
		 * non-split-values index and looking for empty sets:
		 */
		sepToUse = saveSep = TXApp->charStrlstConfig;
		if (useStrlstCreate)
		{
		 /* Bug 4162 #2 changes TXVSSEP_CREATE: empty-string becomes
		  * empty-strlst, but here we want one-empty-string strlst;
		  * use most-preferred delim (',') to achieve it:
		  */
			if (TXVSSEP_CREATE_EMPTY_STR_TO_EMPTY_STRLST(TXApp) &&
			    ((item->type & DDTYPEBITS) == FTN_CHAR ||
			     (item->type & DDTYPEBITS) == FTN_BYTE) &&
			    item->sz == 0)
			{
				sepToUse.toStrlst = TXc2s_defined_delimiter;
				sepToUse.delimiter = TxPrefStrlstDelims[0];
			}
			else
			{
				sepToUse.toStrlst = TXc2s_create_delimiter;
			}
		}
		else
		{
			sepToUse.toStrlst = TXc2s_trailing_delimiter;
		}
		TXApp->charStrlstConfig = sepToUse;
		_fldcopy(itemFld, NULL, indexFld, NULL, fo); /* item->index */
		indexFldBufSz = fldtobuf(fc->tbl1);
		TXApp->charStrlstConfig = saveSep;
		/* Whether `indexBtree' is ASC or DESC should not matter:
		 * we are looking for value equality only (not </>),
		 * and recid order asc/desc does not matter as long
		 * as dup recids (for a given value) are adjacent.
		 */
		btsetsearch(indexBtree, BT_SEARCH_AFTER);
		btsearch(indexBtree, indexFldBufSz, fc->tbl1->orec);
		hiMark = btgetpercentage(indexBtree);
		btsetsearch(indexBtree, BT_SEARCH_BEFORE);
		btsearch(indexBtree, indexFldBufSz, fc->tbl1->orec);
		loMark = btgetpercentage(indexBtree);
		if ((hiMark - loMark) > TXbtreemaxpercent)
			goto btreeThresholdExceeded;
		indexFldBuf = (char *)TXmalloc(TXPMBUFPN, fn, indexFldBufSz);
		if (!indexFldBuf) goto err;
		memcpy(indexFldBuf, fc->tbl1->orec, indexFldBufSz);
	}
	else					/* looking for all rows */
	{
		loMark = 0;
		hiMark = 100;
		if ((hiMark - loMark) > TXbtreemaxpercent)
		{				/* item is too "noisy" */
		btreeThresholdExceeded:
			if (verbose)
				putmsg(MINFO, fn,
				       WillNotUseIndexBtreeThresholdFmt,
				       getdbffn(indexBtree->dbf),
				       (int)(hiMark - loMark),
				       (int)TXbtreemaxpercent);
			goto err;
		}
		rewindbtree(indexBtree);
	}

	if (!(curKeyBuf = (char *)TXmalloc(TXPMBUFPN, fn, BT_MAXPGSZ)))
		goto err;
	TXsetrecid(&prevLoc, -1);
	itemCountThisRow = totalRecs = 0;
	while ((curKeySz = BT_MAXPGSZ),
	       (curLoc = btgetnext(indexBtree, &curKeySz, curKeyBuf, NULL)),
	       (TXrecidvalid(&curLoc) &&
		(!indexFldBuf ||		/* all rows requested */
		 fldcmp(indexFldBuf, indexFldBufSz, curKeyBuf, curKeySz, fc)
		 == 0)))
	{
		/* If `!deDupIndexResults', `bt' is not set up for
		 * per-recid counting of index results, which we
		 * usually need for subset.  But thanks to
		 * subset-multi-LHS-param-single-RHS-col check
		 * earlier, we know overall param is at most one item;
		 * combined with `!deDupIndexResults' we can assume no
		 * dup recids from index either, so no counts needed:
		 */
		if (doSubset && deDupIndexResults && item)
		{
			res = 1;		/* no result added yet */
			/* All rows come back in recid order from
			 * index, for a given `item'.  So we can
			 * detect dup recids (per-`item') merely by
			 * checking adjacent recids:
			 */
			if (TXrecidcmp(&prevLoc, &curLoc) == 0)
				itemCountThisRow++;
			else			/* new row for this item */
			{
				itemCountThisRow = 1;
				prevLoc = curLoc;
			}
			if ((EPI_HUGEUINT)itemCountThisRow <=
			    (EPI_HUGEUINT)item->count)
			{
				cntLoc = btsearch(results, sizeof(BTLOC),
						  &curLoc);
				if (TXrecidvalid2(&cntLoc))
				{		/* already seen: inc count */
					TXsetrecid(&cntLoc, cntLoc.off + 1);
					btupdate(results, cntLoc);
				}
				else		/* first time for `curLoc' */
				{
					TXsetrecid(&cntLoc, 1);
					res = btinsert(results, &cntLoc,
						       sizeof(BTLOC),&curLoc);
				}
			}
			/* else we have enough values for `item' from
			 * this `curLoc': any more and we will
			 * over-count, maybe miss other `item's
			 */
		}
		else		/* intersect, not de-dup, or all-rows */
		{
			if (deDupIndexResults)
				res = btinsert(results, &curLoc,
					       sizeof(BTLOC), &curLoc);
			else
				/* wtf can we use btappend() instead;
				 * do we care about ordering?  should
				 * already be in order anyway?
				 */
				res = btinsert(results, &curLoc, curKeySz,
					       curKeyBuf);
		}
		if (res == 0) totalRecs++;
	}
	goto done;

err:
	totalRecs = -1;				/* error */
done:
	indexFldBuf = TXfree(indexFldBuf);
	curKeyBuf = TXfree(curKeyBuf);
	return(totalRecs);
}

/******************************************************************/

static IINDEX *ixbteqindex ARGS((int itype, char *iname,
	CONST char *sysindexFileds, CONST char *sysindexParams, FLD *fld,
	char *fname, FLDOP *fo, DBTBL *dbtbl, int fop,
	int paramIsRHS, int indexIsDesc, int *needPostProc));

static IINDEX *
ixbteqindex(itype, iname, sysindexFields, sysindexParams, fld, fname,
	    fo, dbtbl, fop, paramIsRHS, indexIsDesc, needPostProc)
int itype;			/* Type of index */
char *iname;			/* Name of index */
CONST char *sysindexFields;	/* (in) SYSINDEX.FIELDS of index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS of index */
FLD *fld;			/* Field containing value to search for */
char *fname;			/* Name of the field */
FLDOP *fo;			/* A fldop structure to use */
DBTBL *dbtbl;			/* The table the index belongs to */
int	fop;			/* (in) FOP operator */
int	paramIsRHS;		/* (in) nonzero: param is on right-hand side*/
int	indexIsDesc;		/* (in) nonzero: `iname' is a DESC index */
int	*needPostProc;		/* (out) nonzero: results need post-proc */
/* Returns a RAM B-tree with items from index `iname' that match
 * (potentially multi-item) `fld' for IN, SUBSET, or INTERSECT_IS_NOT_EMPTY.
 */
{
	static CONST char Fn[] = "ixbteqindex";
	size_t sz;
	FLD *fld1, *fld2, *emptyFld = NULL;
	FTN	itemType;
	FLDCMP *fc;
	IINDEX *ix = NULL;
	BTLOC btloc, cntLoc;
	BTREE *bt = BTREEPN, *rc = BTREEPN;
	BTREE	*finalBtree = NULL;
	EPI_HUGEINT nrecs, curRecs;
	int doSubset = 0, btreeIsOnMultiItemType = 0, btreeHasSplitValues = 0;
	int	deDupIndexResults, returnEmptyIndexRowsToo = 0;
	int	haveEmptyItem = 0, tableColIsCharOrByte, returnAllRows = 0;
	int	emptyStringIsEmptySet, useStrlstCreateAndSeparateEmpty = 0;
	void	*v;
	FLDITEM	*uniqFldItems = NULL, *itemSrc, *itemDest, *itemEnd;
	size_t	numUniqFldItems = 0, numAllocedUniqFldItems = 0;
	size_t	numTotalFldItems = 0;
	CONST char	*fldName;
	FLD		*dbtblFld;
	FTN		tableColType;
	INDEX_TRACE_VARS;

	(void)itype;
#ifdef MEMDEBUG
	mac_ovchk();
#endif
	*needPostProc = 0;
	emptyStringIsEmptySet = 1;
	switch (fop)
	{
	case FOP_IN:
		doSubset = TXApp->inModeIsSubset;
		emptyStringIsEmptySet = 0;
		break;
	case FOP_IS_SUBSET:
		doSubset = 1;
		break;
	case FOP_INTERSECT:			/* not a boolean/predicate */
		goto err;
	case FOP_INTERSECT_IS_EMPTY:		/* negation cannot use index */
		goto err;
	case FOP_INTERSECT_IS_NOT_EMPTY:
		doSubset = 0;
		break;
	}
	switch (dbtbl->type)
	{
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		goto err;
	}
	if (!fld) goto err;			/* should always have `fld' */
	ix = openiindex();
	if (!ix) goto err;
	if (verbose)
		putmsg(MINFO, Fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, TBSPECPN),
		       FOR_OP_ARG(fname, fop, fldtostr(fld), paramIsRHS));
	if (!existsbtree(iname)) goto indexMissing;
	rc = TXbtcacheopen(dbtbl, iname, INDEX_BTREE, INDEX_READ,
			   sysindexParams);
	if (!rc)
	{
	indexMissing:
		putmsg(MWARN, Fn, IndexDoesNotExistFmt, iname);
		ix = closeiindex(ix);
		goto err;
	}
	btsetsearch(rc, BT_SEARCH_BEFORE);

	btreeIsOnMultiItemType = TXbtreeIsOnMultipleItemType(sysindexFields,
							     dbtbl);
	btreeHasSplitValues = TXbtreeHasSplitValues(sysindexFields, dbtbl,
						    rc->params.indexValues);
	deDupIndexResults = (TXApp->deDupMultiItemResults &&
			     btreeHasSplitValues);

	if (deDupIndexResults)
	{
		/* Bug 4064: Use a unique B-tree (with key set to
		 * table recid, not fld value) to de-dup results: may
		 * get the same row back from `rc' for multiple `IN'
		 * etc. values (LHS or RHS).  Also, might store
		 * per-recid counts in `bt' recid (instead of table
		 * recid), for subset:
		 */
		bt = openbtree(NULL, BT_MAXPGSZ, 20, BT_FIXED | BT_UNIQUE,
				O_RDWR | O_CREAT);
		if (!bt) goto err;
	}
	else
	{
		bt = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
		if (!bt) goto err;
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt->params, dbtbl->ddic);
		btreesetdd(bt, btreegetdd(rc));

		fc = TXopenfldcmp(bt, TXOPENFLDCMP_CREATE_FLDOP);
		if (!fc) goto err;
		bt->usr = fc;
		TXsetdontcare(fc, 1, 1, OF_DONT_CARE);  /* ignore 2nd+ flds */
		btsetcmp(bt, (btcmptype) fldcmp);
	}

	fc = TXopenfldcmp(rc, TXOPENFLDCMP_CREATE_FLDOP);
	if (!fc) goto err;
	rc->usr = fc;
	TXsetdontcare(fc, 1, 1, OF_DONT_CARE);	/* ignore all but 1st field */
	btsetcmp(rc, (btcmptype) fldcmp);

	fld1 = nametofld(fc->tbl1, fname);
	if (!fld1)
	{
		putmsg(MWARN, Fn, CouldNotFindFieldInIndexFmt, fname, iname);
		goto err;
	}
	nrecs = 0;

	/* Create `fld2', with the same type as the return of
	 * `TXfldGetNextItem(fld, ...)', e.g. strlst `fld' comes back
	 * as varchar, multiple-item items come back singly:
	 */
	TXfldIsMultipleItemType(fld, NULL, &itemType);
	fld2 = emptyfld(itemType, 1);
	if (!fld2) goto err;
	/* We want exactly `itemType'; FTN_NotNullableFlag may defeat
	 * locfldcmp():
	 */
	if (!(itemType & FTN_NotNullableFlag))
		fld2->type &= ~FTN_NotNullableFlag;

	/* Get type of table column, which may differ from index type
	 * if split-values:
	 */
	fldName = NULL;
	dbtblFld = TXgetNextIndexFldFromTable(sysindexFields, dbtbl,&fldName);
	if (dbtblFld)
		tableColType = (FTN)dbtblFld->type;
	else					/* should not happen */
		tableColType = (FTN)fld1->type;	/* true iff !split-values */
	switch (tableColType & DDTYPEBITS)
	{
	case FTN_CHAR:
	case FTN_BYTE:
		tableColIsCharOrByte = 1;
		break;
	default:
		tableColIsCharOrByte = 0;
		break;
	}

	/* Get unique, sorted list of `fld' items, with counts of dups.
	 * Counts let us handle duplicate-item `fld' (and `rc' rows)
	 * without a table post-process; uniqueness saves looking up
	 * dups more than once; sorting may improve `rc' lookup speed:
	 */
	v = NULL;
	sz = 0;
	while ((v = TXfldGetNextItem(fld, v, sz, &sz)) != NULL)
	{
		if (!TX_INC_ARRAY(TXPMBUFPN, &uniqFldItems,
				  numUniqFldItems, &numAllocedUniqFldItems))
			goto err;
		itemDest = uniqFldItems + numUniqFldItems;
		itemDest->data = v;
		itemDest->sz = sz;
		itemDest->count = 1;		/* to be fixed later */
		itemDest->type = (byte)fld2->type;
		/* Sort the same (ASC/DESC) as `iname', just to help
		 * lookup speed.  Probably does not matter:
		 */
		itemDest->indexIsDesc = (byte)indexIsDesc;
		numUniqFldItems++;
		if (sz == 0 &&
		    (itemDest->type & FTN_VarBaseTypeMask) == (DDVARBIT | FTN_CHAR))
			haveEmptyItem = 1;
	}

	/* Bug 3677 #12: empty string is taken as empty strlst for set ops;
	 * Bug 3677 #13: except for FOP_IN:
	 */
	if (numUniqFldItems == 1 &&
	    ((fld->type & DDTYPEBITS) == FTN_CHAR ||
	     (fld->type & DDTYPEBITS) == FTN_BYTE) &&
	    uniqFldItems[0].sz == 0 &&
	    emptyStringIsEmptySet)
	{
		numUniqFldItems = 0;		/* change to empty set */
		haveEmptyItem = 0;
	}

	/* Sort and unique-with-counts the item list: */
	numTotalFldItems = numUniqFldItems;	/* save # before de-duping */
	if (numTotalFldItems > 1 && TXApp->deDupMultiItemResults)
	{
		BTPARAM_ACTIVATE(&rc->params);
		qsort(uniqFldItems, numUniqFldItems, sizeof(FLDITEM),
		      TXfldItemCmp);
		/* Unique the sorted list, counting dups: */
		itemEnd = uniqFldItems + numUniqFldItems;
		for (itemDest = uniqFldItems, itemSrc = itemDest + 1;
		     itemSrc < itemEnd;
		     itemSrc++)
		{
			if (TXfldItemCmp(itemDest, itemSrc) == 0)
				itemDest->count++;
			else			/* different item */
				*(++itemDest) = *itemSrc;
		}
		numUniqFldItems = (itemDest + 1) - uniqFldItems;
		BTPARAM_DEACTIVATE(&rc->params);
	}

	/* Optimization: if LHS (param or column) of subset op has one
	 * item, subset and intersect behave the same, so we can avoid
	 * extra subset work:
	 */
	if (doSubset)
	{
		if (!paramIsRHS && numTotalFldItems == 1)/* 1-item LHS param */
			doSubset = 0;
		else if (paramIsRHS && !btreeIsOnMultiItemType)
		{
			/* LHS is single-item type.  However, if
			 * varchar, empty string is taken as empty
			 * set, which is subset of anything: need to
			 * return empty index rows too (but not for
			 * FOP_IN, per Bug 3677 #13):
			 */
			doSubset = 0;
			if (tableColIsCharOrByte && emptyStringIsEmptySet)
				/* No Bug 4143 issue: LHS col is
				 * single-item and thus not split.
				 */
				returnEmptyIndexRowsToo = 1;
		}
	}

	/* Optimization: a multi-item param can never be a subset of
	 * a single-value RHS type, so iff LHS is multi-item param and
	 * RHS is single-item type, no rows can be returned for subset.
	 * (This subset-multi-LHS-param-single-RHS-col check here
	 * also allows subset to work with non-split indexes, below.)
	 */
	if (doSubset &&
	    !paramIsRHS &&			/* param is left-hand-side */
	    numTotalFldItems > 1 &&		/*   and multi-item */
	    !btreeIsOnMultiItemType)
		goto returnBt;

	/* Empty-set param:
	 *   Intersect (param-LHS or -RHS):
	 *     No rows ever match, as the result can never be non-empty.
	 *   Subset:
	 *     Param LHS:
	 *     	 All rows match, as empty-set is a subset of every set.
	 *     	 But since empty sets are not present in split-value
	 *       indexes (Bug 4143), we will miss empty-set table rows
	 *       if we try to iterate such indexes, so we cannot use them;
	 *       plus we would have to de-dup a lot of rows.
	 *       TXbtreeScoreIndex() works around this by preferring
	 *       an indexvalues=all index which we *can* use here.
	 *     Param RHS:
	 *       Only empty-set rows would match, but since empty sets
	 *       are not present in split-value indexes (Bug 4143), we
	 *       cannot use such an index.  TXbtreeScoreIndex() works
	 *       around this by preferring an indexvalues=all index which
	 *       we *can* use here.
	 */
	if (numTotalFldItems == 0)		/* empty-set parameter */
	{
		if (doSubset)			/* subset */
		{
			if (btreeHasSplitValues)
			{
			splitValuesEmptyParamSubset:
				/* Bug 4143: empty-set rows unindexed
				 * in split-values indexes; cannot find
				 * them either specifically or as part of
				 * all-table-rows.  Try another index or
				 * go linear.  TXbtreeScoreIndex() knows
				 * to give us an indexvalues=all index
				 * if available:
				 */
				if (verbose)
					putmsg(MINFO, Fn, "Cannot use index %s for current query: indexvalues split, empty-set parameter and subset op",
					       iname);
				goto err;
			}
			if (paramIsRHS)
			{	/* Want to return only empty-set table rows */
				if (btreeIsOnMultiItemType)
					returnEmptyIndexRowsToo = 1;
				else if (tableColIsCharOrByte &&
					 emptyStringIsEmptySet)
					returnEmptyIndexRowsToo = 1;
				else
					/* single-item type: cannot ever be
					 * any empty-set rows, return nothing:
					 */
					goto returnBt;
			}
			else			/* param is LHS */
			{	/* Want to return all table rows */
				returnAllRows = 1;
			}
		}
		else				/* intersect */
		{
			/* intersect of empty-set never returns anything --
			 * unless it was subset-optimized-into-intersect
			 * and we still want empty index rows too:
			 */
			if (!returnEmptyIndexRowsToo)
				goto returnBt;	/* return no rows */
		}
	}
	else					/* non-empty-set param */
	{
		/* Empty-set table rows may be present for some types:
		 *   Intersect:
		 *     No extra work needed (intersect w/empty is empty)
		 *   Subset:
		 *     Param LHS:
		 *       Add empty-set table rows iff param is empty;
		 *       already covered above w/empty-set parameter check.
		 *     Param RHS:
		 *       Add empty-set table rows (regardless of param value).
		 *       If parameter is empty-set, this was already covered
		 *       above under empty-set parameter check.  Otherwise
		 *       (non-empty parameter), cannot look up empty-set
		 *       rows in split-value index (wtf Bug 4143), but cannot
		 *       look up individual param values in non-split-value
		 * 	 index either (iff multi-item-type column).
		 *         Probably better to miss empty rows in the result
		 *       set than not use an index at all, so proceed; user
		 *       can always add "OR col = ''" (if there is an
		 *       indexvalues=all index) to get the empty rows too:
		 */
		if (doSubset && paramIsRHS && numTotalFldItems != 0 &&
		    btreeHasSplitValues)
		{
			if (verbose)
				putmsg(MWARN, Fn,
		  "Results from index %s will be missing empty rows (if any)",
				       iname);
		}
	}

	/*   IN/SUBSET/INTERSECT look at individual values of multi-item
	 * types, regardless of whether `inmode' is `subset' or `intersect'.
	 * So iff a multi-item type is in the index, it must be split.
	 *   Exception is empty-set param and subset ops: must have
	 * non-split-values index then (need to look for empty-set rows,
	 * which are missing from split-values index).
	 *   Another exception is `anyColumn subset $oneItemParam':
	 * can use either, but non-split index preferred.
	 *   NOTE: see parallel check and reasoning in TXbtreeScoreIndex():
	 */
	if (btreeIsOnMultiItemType)
	{
		if (doSubset && paramIsRHS && numTotalFldItems == 1 &&
		    !btreeHasSplitValues)
		{
			/* For `col subset $oneItemParam' we prefer
			 * non-split index, as it does not require
			 * post-processing.  And we can look up empties:
			 */
			returnEmptyIndexRowsToo = 1;
			useStrlstCreateAndSeparateEmpty = 1;
		}
		else if (doSubset && numTotalFldItems == 0)
		{
			/* was checked above in empty-set-param checks too */
			if (btreeHasSplitValues)
				goto splitValuesEmptyParamSubset;
		}
		else if (!btreeHasSplitValues)
		{
			if (verbose)
				putmsg(MINFO, Fn, CannotUseIndexNotSplitFmt,
				       iname);
			goto err;
		}
	}

	/* Intersect:
	 *   Copy all matching rows for each `fld' item to RAM B-tree `bt'.
	 *   BT_UNIQUE `bt' flag will prevent dup final results (Bug 4064).
	 *   Intersect is commutative, so `paramIsRHS' does not matter.
	 * Subset:
	 *   param on left:
	 *     For each matching row for each unique `fld' item,
	 *     increment its `bt' count (up to count for that `fld' item).
	 *     At end, `bt' items whose count equals `numTotalFldItems'
	 *     have all `fld' items (including dups).
	 *     WTF Bug 4085 2-way ANDed arrays may improve time/mem needs?
	 *   param on right:
	 *     split-values index:
	 *       treat as intersect, and flag for post-proc
	 *     indexvalues=all index:
	 *       usable (and preferred) if # param items <= 1
	 */
	if (doSubset && paramIsRHS && numTotalFldItems != 0)
	{					/* column subset $param */
		/* Turn mode to intersect, because not all `fld' items
		 * are required if `fld' is RHS.  More importantly,
		 * if index is split-values, when we find a match
		 * for a `fld' item, we cannot find out whether that
		 * row also has any other items *not* in `fld', which
		 * would make the row not a match:  must post-process
		 * to handle that.  (One exception would be if LHS
		 * column is single-value type: no post-proc, we know
		 * it has no other values.  But we already checked
		 * for that via LHS-is-1-item optimization above.)
		 */
		doSubset = 0;			/* intersect mode */
		if (numTotalFldItems <= 1 && !btreeHasSplitValues)
		{
			/* see `col subset $oneItemParam' above */
		}
		else
		{
			*needPostProc = 1;	/* post-process needed */
			if (verbose)
				putmsg(MINFO, Fn,
				       "Results from index %s will require post-processing: subset op, right-side parameter and multi/zero-item left-side column",
				       iname);
		}
	}

	/* Now look up stuff in the index: */
	if (returnAllRows)
	{
		/* Bug 4143: empty-set rows unindexed in split-values indexes;
		 * would be missing.  TXbtreeScoreIndex() knows to give us
		 * an indexvalues=all index where usable:
		 */
		if (btreeHasSplitValues) goto splitValuesEmptyParamSubset;
		nrecs = TXgetIndexRowsForItem(bt, rc, fld1, NULL, fld2, fc,
					      fo, doSubset, deDupIndexResults,
					      0);
		if (nrecs < (EPI_HUGEINT)0) goto err;
	}
	else
	{
		itemEnd = uniqFldItems + numUniqFldItems;
		for (itemSrc = uniqFldItems; itemSrc < itemEnd; itemSrc++)
		{				/* each unique `fld' item */
			curRecs = TXgetIndexRowsForItem(bt, rc, fld1, itemSrc,
							fld2, fc, fo,
							doSubset,
							deDupIndexResults,
					     useStrlstCreateAndSeparateEmpty);
			if (curRecs < (EPI_HUGEINT)0) goto err;
			nrecs += curRecs;
		}
		if (returnEmptyIndexRowsToo &&
		    (!haveEmptyItem || useStrlstCreateAndSeparateEmpty))
		{
			/* Get rows from a fake empty item too.  Just re-use
			 * first `uniqFldItems', as we are done with them:
			 */
			if (numUniqFldItems == 0 &&
			    !TX_INC_ARRAY(TXPMBUFPN, &uniqFldItems,
					  numUniqFldItems,
					  &numAllocedUniqFldItems))
				goto err;
			itemDest = uniqFldItems + 0;
			itemDest->data = "";
			itemDest->sz = 0;
			itemDest->count = 1;
			itemDest->type = (byte)fld2->type;
			itemDest->indexIsDesc = (byte)indexIsDesc;
			/* Empty item must be varchar, so it can be truly
			 * empty (e.g. for `strlstCol subset $longParam'
			 * we want to look up empty-set strlst, not `0'):
			 */
			emptyFld = emptyfld((DDVARBIT | FTN_CHAR), 1);
			if (!emptyFld) goto err;
			curRecs = TXgetIndexRowsForItem(bt, rc, fld1,itemDest,
							emptyFld, fc, fo,
							doSubset,
							deDupIndexResults, 0);
			if (curRecs < (EPI_HUGEINT)0) goto err;
			nrecs += curRecs;
		}
	}
	fld2 = closefld(fld2);

	rewindbtree(bt);

	if (doSubset && deDupIndexResults)
	{
		/* Need to remove items from `bt' that do not have enough
		 * item matches, and change "recids" (counts) to true recids:
		 */
		nrecs = 0;
		finalBtree = openbtree(NULL, BT_MAXPGSZ, 20,
				       BT_FIXED | BT_LINEAR, O_RDWR | O_CREAT);
		if (!finalBtree) goto err;
		while (sz = sizeof(btloc),
		       cntLoc = btgetnext(bt, &sz, &btloc, NULL),
		       TXrecidvalid2(&cntLoc))
		{
			if (cntLoc.off != (EPI_OFF_T)numTotalFldItems)
				continue;
			if (btappend(finalBtree, &btloc, sizeof(BTLOC),
				     &btloc, 90, NULL) != 0)
				goto err;
			nrecs++;
		}
		btflush(finalBtree);
		rewindbtree(finalBtree);
		bt = closebtree(bt);
		bt = finalBtree;
		finalBtree = NULL;
	}

returnBt:
#ifdef NEW_I
	if (deDupIndexResults)
		ix->mirror = dbidxfrombtree(bt, DBIDX_BTREE);
	else
		ix->orig = dbidxfrombtree(bt, DBIDX_MEMORY);
	ix->cntorig = nrecs;
#else
	if (deDupIndexResults)
		ix->mirror = bt;
	else
		ix->orig = bt;
	ix->cntorig = nrecs;
#endif
	bt = NULL;				/* owned by `ix' now */
	goto done;

err:
	ix = closeiindex(ix);
done:
	if (rc)
	{
		rc->usr = TXclosefldcmp(rc->usr);
		rc = TXbtcacheclose(dbtbl, iname, INDEX_BTREE, INDEX_READ, rc);
	}
	if (bt)
	{
		bt->usr = TXclosefldcmp(bt->usr);
		bt = closebtree(bt);
	}
	if (finalBtree) finalBtree = closebtree(finalBtree);
	uniqFldItems = TXfree(uniqFldItems);
	if (emptyFld) emptyFld = closefld(emptyFld);
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return ix;
}

static IINDEX *ixbtmmindex
ARGS((int, char *, CONST char *sysindexParams, FLD *, char *, FLD *, int,
      int, FLDOP *, DBTBL *));

#ifndef BTLOCPN
#  define BTLOCPN       ((BTLOC *)NULL)
#endif

#define How many record ids to allocate for, at a shot.
#define RECINC 50000

/******************************************************************/
/*      Add a regular index to the table.
 */

static IINDEX *ixbtindex ARGS((int itype, char *iname,
	CONST char *sysindexFields, CONST char *sysindexParams, FLD **fld,
	char **fname, FLD **fld2, int *inclo, int *inchi, FLDOP *fo,
	DBTBL *dbtbl, TBSPEC *tbspec, int nflds, int fop));

static IINDEX *
ixbtindex(itype, iname, sysindexFields, sysindexParams, fld, fname, fld2,
	  inclo, inchi, fo, dbtbl, tbspec, nflds, fop)
int itype;			/* Type of index */
char *iname;			/* Name of index */
CONST char *sysindexFields;	/* (in) SYSINDEX.FIELDS of index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS of index */
FLD **fld;			/* Field containing value to search for */
char **fname;			/* Name of the field */
FLD **fld2;			/* Field containing last value to match */
int *inclo;			/* Is the low value inclusive? */
int *inchi;			/* Is the high value inclusive? */
FLDOP *fo;			/* A fldop structure to use */
DBTBL *dbtbl;			/* The table the index belongs to */
TBSPEC *tbspec;
int nflds;			/* How many fields */
int	fop;			/* (in) FOP operator */
{
	static CONST char Fn[] = "ixbtindex";
	size_t sz, losz = 0, hisz = 0;
	size_t sz2;
	FLD **fld1 = NULL;
	FLDCMP *fc;
	IINDEX *ix = NULL;
	BTLOC btloc;
	BTLOC *recids = NULL;	/* Number of recids */
	size_t si, recsalloced = 0, recsused = 0;	/* Recids alloced/used */
	size_t	finalRecCount=0;
	int i, btreeHasSplitValues = 0;
	BTREE *bt = NULL, *rc = NULL;
	int abvlo = 0;
	EPI_HUGEUINT nrecs;
	int himark, lomark;
	int gotit = 0;
	int needtbuf;
	int foundfield = 0;
	int inclast, incfirst;
	char *lobuf = NULL, *hibuf = NULL;
	byte *indexbuf;
	INDEX_TRACE_VARS;
#ifndef NO_USE_EXTRA
	void *extra = NULL;
#endif

	switch (dbtbl->type)
	{
	case INDEX_3DB:
	case INDEX_MM:		/* wtf */
	case INDEX_FULL:	/* wtf */
		{
			if (!fld && !fld2)
				goto err;
			if (!fld)
				return ixbtmmindex(itype, iname,
						   sysindexParams, NULL,
						   *fname,
						   *fld2, *inclo, *inchi, fo,
						   dbtbl);
			if (!fld2)
				return ixbtmmindex(itype, iname,
						   sysindexParams, *fld,
						   *fname,
						   NULL, *inclo, *inchi, fo,
						   dbtbl);
			return ixbtmmindex(itype, iname, sysindexParams,
					   *fld, *fname, *fld2,
					   *inclo, *inchi, fo, dbtbl);

		}
	}
	ix = openiindex();
	if (!ix) goto err;
	if (verbose)
	{
		if (!(TXtraceIndexBits & 0x4000))
			*forBuf = '\0';
		else if (nflds == 1)
			htsnpf(forBuf, sizeof(forBuf)," for values `%s' `%s'",
			       (fld && fld[0] ? fldtostr(fld[0]) : "?"),
			       (fld2 && fld2[0] ? fldtostr(fld2[0]) : "?"));
		else
			htsnpf(forBuf, sizeof(forBuf), " for %d values",
			       (int)nflds);
		putmsg(MINFO, Fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, tbspec), forBuf);
	}
	if (!existsbtree(iname)) goto missingIndex;
	rc = TXbtcacheopen(dbtbl, iname, INDEX_BTREE, INDEX_READ,
			   sysindexParams);
	if (!rc)
	{
	missingIndex:
		putmsg(MERR, Fn, IndexDoesNotExistFmt, iname);
		goto err;
	}
	btsetsearch(rc, BT_SEARCH_BEFORE);

	btreeHasSplitValues = TXbtreeHasSplitValues(sysindexFields, dbtbl,
						    rc->params.indexValues);

	/* MATCHES looks at individual values of multi-item types.
	 * So iff a multi-item type is present, it must be split.
	 * See parallel check and reasoning in TXbtreeScoreIndex()
	 * (which should not have let us try to use the index here).
	 * Exception is if we are returning all rows (MATCHES '%')
	 * and de-duping results: prefer indexvalues=all index then
	 * (wtf could de-dup here like we do in ixbteqindex()):
	 */
	switch (fop)
	{
	case FOP_MAT:
		if (!fld && !fld2 &&		/* want all rows */
		    TXApp->deDupMultiItemResults &&	/* Bug 4064: de-dup results */
		    btreeHasSplitValues)
		{
			if (verbose)
				putmsg(MINFO, Fn, "Will not use index %s for current query: indexvalues split and all rows needed; prefer non-split index",
				       iname);
			goto err;
		}
		if (TXbtreeIsOnMultipleItemType(sysindexFields, dbtbl) &&
		    !btreeHasSplitValues)
		{
			if (verbose)
				putmsg(MINFO, Fn, CannotUseIndexNotSplitFmt,
					iname);
			goto err;
		}
		break;
	}

	fld1 = (FLD **) TXcalloc(TXPMBUFPN, Fn, nflds, sizeof(FLD *));
	if (!fld1) goto err;
	bt =
		openbtree(NULL, BT_MAXPGSZ, 20, BT_FIXED | BT_LINEAR,
			  O_RDWR | O_CREAT);
	if (bt)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt->params, dbtbl->ddic);

	if(TXsetfldcmp(rc) == -1) goto err;
	fc = rc->usr;
	/* Ignore all but the first `nflds' fields: */
	TXsetdontcare(fc, nflds, 1, OF_DONT_CARE);

	for (i = 0; i < nflds; i++)
	{
		fld1[i] = nametofld(fc->tbl1, fname[i]);
		if (!fld1[i])
		{
			putmsg(MWARN, Fn, CouldNotFindFieldInIndexFmt,
				fname, iname);
			goto err;
		}
	}
	foundfield = 0;
	inclast = inchi[0];
	if (fld2)
	{
		void *v;

		for (i = 0; i < nflds; i++)
		{
			if (fld2[i])
			{
				v = getfld(fld2[i], &sz);
				putfld(fld1[i], v, sz);
				foundfield++;
				if (inchi[i])
					btsetsearch(rc, BT_SEARCH_AFTER);
				else
					btsetsearch(rc, BT_SEARCH_FIND);
				inclast = inchi[i];
			}
			else
				TXsetdontcare(fc, i, 0, OF_DONT_CARE);

		}
	}
	if (foundfield)
	{
		sz = fldtobuf(fc->tbl1);
		btloc = btsearch(rc, sz, fc->tbl1->orec);
		if (inclast || !TXrecidvalid(&btloc))
		{
			btloc = btgetnext(rc, NULL, NULL, NULL);
		}
		gotit = 0;
		if (TXrecidvalid(&btloc))
		{
			btreesetmarker(rc);
		}
		gotit = 1;
		btsetsearch(rc, BT_SEARCH_BEFORE);
		himark = btgetpercentage(rc);
		hibuf = (char *) TXmalloc(TXPMBUFPN, Fn, sz);
		if (!hibuf) goto err;
		memcpy(hibuf, fc->tbl1->orec, sz);
		hisz = sz;
	}
	else
		himark = 100;

	TXresetdontcare(fc, nflds,
			OF_DONT_CARE | OF_PREFER_END | OF_PREFER_START);
	foundfield = 0;
	incfirst = inclo[0];
	if (fld)
	{
		void *v;

		for (i = 0; i < nflds; i++)
		{
			if (fld[i])
			{
				v = getfld(fld[i], &sz);
				putfld(fld1[i], v, sz);
				foundfield++;
				if (!inclo[i])
				{
					TXsetdontcare(fc, i, 0,
						      OF_PREFER_END);
					incfirst = 0;
				}
			}
			else
				TXsetdontcare(fc, i, 0, OF_DONT_CARE);
		}
	}
	if (foundfield)
	{
		sz = fldtobuf(fc->tbl1);
		btsearch(rc, sz, fc->tbl1->orec);
		lomark = btgetpercentage(rc);
		lobuf = (char *) TXmalloc(TXPMBUFPN, Fn, sz);
		if (!lobuf) goto err;
		memcpy(lobuf, fc->tbl1->orec, sz);
		losz = sz;
	}
	else
	{
		rewindbtree(rc);
		lomark = 0;
	}
	if (verbose)
		putmsg(MINFO, Fn, "Expect to read %d%% of the index",
		       (himark - lomark));
	if ((himark - lomark) > TXbtreemaxpercent)
	{
		if (verbose)
			putmsg(MINFO, Fn, WillNotUseIndexBtreeThresholdFmt,
				iname, (int)(himark - lomark),
				(int)TXbtreemaxpercent);
		goto err;
	}
	nrecs = 0;
/* Conditions we might look at tempbuf:
   fld2 && !gotit
   fld && !abvlo && !inclo
 */
	needtbuf = 0;
	if (fld2 && fld2[0] && !gotit)
		needtbuf++;
	if (fld && fld[0] && !abvlo && !incfirst)
		needtbuf++;
#ifndef NO_USE_EXTRA
	if (ddgetnfields(btreegetdd(rc)) > 1)
	{
		int x = 0;	/* init `x' so Valgrind does not complain */

		extra = iextra(tbspec, btreegetdd(rc), 0, &x, dbtbl, 0);
		if (extra)
			needtbuf++;
	}
#endif
	while ((sz2 = sizeof(tempbuf)),
	       (btloc =
		btgetnext(rc, &sz2, NULL, needtbuf ? &indexbuf : NULL)),
	       TXrecidvalid(&btloc))
	{
		int res = 1;

		if (fld2 && fld2[0])
		{
			if (gotit)
				res = 1;
			else
				res = fldcmp(hibuf, hisz, indexbuf, sz2, fc);
			if ((!inclast && !res) || res < 0)
				break;
		}
		if (fld && fld[0] && !abvlo && !incfirst)
		{
			res = fldcmp(lobuf, losz, indexbuf, sz2, fc);
			if (!res)
				continue;
			else
			{
				abvlo++;
				needtbuf--;
			}
		}
/* WTF - may be able to do some extra checks here */
#ifndef NO_USE_EXTRA
		if (extra)
		{
			int isok;

			isok = iextraok(extra, btloc, indexbuf, sz2);
			if (!isok)
				continue;
		}
#endif
		if (recsalloced == recsused)
		{
			recsalloced += RECINC;
			recids = (recids == BTLOCPN ?
				  (BTLOC *) TXmalloc(TXPMBUFPN, Fn,
				recsalloced * sizeof(RECID)) : (BTLOC *)
				  TXrealloc(TXPMBUFPN, Fn, recids,
					  recsalloced * sizeof(RECID)));
			if (!recids) goto err;
		}
		recids[recsused++] = btloc;
	}
#ifndef NO_USE_EXTRA
	if (extra)
	{
		extra = closeextra(extra, 1);
	}
#endif
	if (recids)
	{
		qsort(recids, recsused, sizeof(RECID),
		      (int (*)ARGS((CONST void *, CONST void *))) _recidcmp);
		finalRecCount = 0;
		for (si = 0; si < recsused; si++)
		{
			/* Bug 4064: de-dup results (e.g. strlst MATCHES): */
			if (TXApp->deDupMultiItemResults &&
			    si > 0 &&
			    TXrecidcmp(&recids[si], &recids[si-1]) == 0)
				continue;	/* dup; skip */
			btappend(bt, &recids[si], sizeof(RECID), &recids[si],
				 90, NULL);
			finalRecCount++;
		}
		recids = TXfree(recids);
	}
	nrecs = (EPI_HUGEUINT) finalRecCount;
	btflush(bt);
	rewindbtree(bt);
#ifdef NEW_I
	ix->mirror = dbidxfrombtree(bt, DBIDX_BTREE);
#else
	ix->mirror = bt;
#endif
	bt = NULL;				/* `ix' owns it now */
	ix->cntorig = nrecs;
	goto done;

err:
	ix = closeiindex(ix);
done:
	lobuf = TXfree(lobuf);
	hibuf = TXfree(hibuf);
	fld1 = TXfree(fld1);
	recids = TXfree(recids);
	if (rc)
	{
		rc->usr = TXclosefldcmp(rc->usr);
		rc = TXbtcacheclose(dbtbl, iname, INDEX_BTREE, INDEX_READ, rc);
	}
	if (bt)
	{
		bt->usr = TXclosefldcmp(bt->usr);
		bt = closebtree(bt);
	}
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return ix;
}

/******************************************************************/
/*	Add a regular index to the table.
 */

static IINDEX *
ixbtmmindex(itype, iname, sysindexParams, fld, fname, fld2, inclo, inchi,
	    fo, dbtbl)
int itype;			/* Type of index */
char *iname;			/* Name of index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS value of index */
FLD *fld;			/* Field containing value to search for */
char *fname;			/* Name of the field */
FLD *fld2;			/* Field containing last value to match */
int inclo;			/* Is the low value inclusive? */
int inchi;			/* Is the high value inclusive? */
FLDOP *fo;			/* A fldop structure to use */
DBTBL *dbtbl;			/* The table the index belongs to */
{
	static CONST char Fn[] = "ixbtmmindex";
	size_t sz, losz, hisz;
	size_t sz2, wdSz, occSz;
	IINDEX *ix = IINDEXPN;
	BTLOC btloc;
	BTREE *bt = BTREEPN;
	int abvlo = 0;
	EPI_HUGEUINT nrecs;
	int himark, lomark;
	int gotit = 0;
	char *lobuf = CHARPN, *hibuf = CHARPN;
	int (*exists) ARGS((char *name));
	union
	{
		void *obj;
		BTREE *bt;
		FDBI *fi;
	}
	i;
	CONST char	*wd, *w;
	ft_int64	rowCount, occurrenceCount = 0;
	INDEX_TRACE_VARS;
	char		occStrBuf[EPI_HUGEINT_BITS/3 + 3];
	char		mergeBuf[BT_REALMAXPGSZ];

	(void)fo;
	i.obj = NULL;
#ifdef MEMDEBUG
	mac_ovchk();
#endif
	/* Cannot search a Metamorph index-as-table by Count, RowCount
	 * or OccurrenceCount; only by Word:
	 */
	if (!strcmp(fname, "Count") ||
	    strcmp(fname, "RowCount") == 0 ||
	    strcmp(fname, "OccurrenceCount") == 0)
	{
		goto err;
	}
	switch (dbtbl->type)
	{
	case INDEX_MM:
	case INDEX_FULL:
		exists = fdbi_exists;
		break;
	case INDEX_3DB:
	default:
		exists = existsbtree;
		break;
	}
	ix = openiindex();
	if (!ix) goto err;
	if (verbose)
		putmsg(MINFO, Fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, TBSPECPN),
		       (*forBuf = '\0', forBuf)/* wtf */);
	if (!exists(iname)) goto missingIndex;
	if (dbtbl->type == INDEX_MM || dbtbl->type == INDEX_FULL)
		i.fi = dbtbl->fi;
	else
		i.obj = TXbtcacheopen(dbtbl, iname, itype, INDEX_READ,
				      sysindexParams);
	if (i.obj == NULL)
	{
	missingIndex:
		putmsg(MERR, Fn, IndexDoesNotExistFmt, iname);
		goto err;
	}
	bt = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
	if (bt == BTREEPN) goto err;
	if (bt)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt->params, dbtbl->ddic);

	/* Can only search a Metamorph index-as-table by Word: */
	if (strcmp(fname, "Word"))
	{
		putmsg(MWARN, Fn, CouldNotFindFieldInIndexFmt, fname, iname);
		goto err;
	}
	if (fld2)
	{
		void *v;

		v = getfld(fld2, &sz);

		switch (dbtbl->type)
		{
		case INDEX_MM:
		case INDEX_FULL:
			btloc =
				fdbi_search(i.fi, (char *) v, inchi ? -1 : 0,
					    &himark);
			break;
		case INDEX_3DB:
		default:
			if (inchi)
				btsetsearch(i.bt, BT_SEARCH_AFTER);
			else
				btsetsearch(i.bt, BT_SEARCH_FIND);
			btloc = btsearch(i.bt, sz, v);
			himark = btgetpercentage(i.bt);
			break;
		}
		if (inchi || !TXrecidvalid(&btloc))
		{
			switch (dbtbl->type)
			{
			case INDEX_MM:
			case INDEX_FULL:
				btloc = fdbi_getnextrow(i.fi, &wd,
							&rowCount,
							&occurrenceCount);
				break;
			case INDEX_3DB:
			default:
				sz2 = sizeof(tempbuf);
				btloc = btgetnext(i.bt, &sz2, tempbuf, NULL);
				break;
			}
		}
		if (TXrecidvalid(&btloc))
		{
			switch (dbtbl->type)
			{
			case INDEX_MM:
			case INDEX_FULL:
				fdbi_setmarker(i.fi);
				break;
			case INDEX_3DB:
			default:
				btreesetmarker(i.bt);
				btsetsearch(i.bt, BT_SEARCH_BEFORE);
				break;
			}
		}
		gotit = 1;

		if (!fld)
			switch (dbtbl->type)
			{
			case INDEX_MM:
			case INDEX_FULL:
				fdbi_rewind(i.fi);
				break;
			case INDEX_3DB:
			default:
				rewindbtree(i.bt);
			}
		hibuf = (char *) TXmalloc(TXPMBUFPN, Fn, sz);
		if (!hibuf) goto err;
		memcpy(hibuf, v, sz);
		hisz = sz;
	}
	else
		himark = 100;
	if (fld)
	{
		void *v;

		v = getfld(fld, &sz);
#ifdef DEBUG
		DBGMSG(4,
		       (999, NULL, "Searching for low (%s)", fldtostr(fld)));
#endif
		switch (dbtbl->type)
		{
		case INDEX_MM:
		case INDEX_FULL:
			fdbi_search(i.fi, (char *) v, 1, &lomark);
			break;
		case INDEX_3DB:
		default:
			btsearch(i.bt, sz, v);
			lomark = btgetpercentage(i.bt);
			break;
		}
		lobuf = (char *) TXmalloc(TXPMBUFPN, Fn, sz);
		if (!lobuf) goto err;
		memcpy(lobuf, v, sz);
		losz = sz;
	}
	else
		lomark = 0;
	if (verbose)
		putmsg(MINFO, Fn, "Expect to read %d%% of the index",
		       (himark - lomark));
	if ((himark - lomark) > TXbtreemaxpercent)
	{
		if (verbose)
			putmsg(MINFO, Fn, WillNotUseIndexBtreeThresholdFmt,
				iname, (int)(himark - lomark),
				(int)TXbtreemaxpercent);
		goto err;
	}
	nrecs = 0;
	while ((dbtbl->type == INDEX_MM || dbtbl->type == INDEX_FULL ?
		btloc = fdbi_getnextrow(i.fi, &wd, &rowCount, &occurrenceCount) :
		(sz2 = sizeof(tempbuf),
		 btloc = btgetnext(i.bt, &sz2, tempbuf, NULL))),
	       TXrecidvalid(&btloc))
	{
		int rc = 1;

#ifdef DEBUG
		DBGMSG(9,
		       (999, NULL, "Just read btloc %d", TXgetoff(&btloc)));
#endif
		if (fld2)
		{
			if (gotit)
				rc = 1;
			else
				rc =
					strcmp(hibuf,
					       (dbtbl->type == INDEX_MM
						|| dbtbl->type ==
						INDEX_FULL ? wd : tempbuf));
#ifdef DEBUG
			DBGMSG(9,
			       (999, NULL, "(Hi)Current record scores a %d",
				rc));
#endif
			if ((!inchi && !rc) || rc < 0)
				break;
		}
		if (fld && !abvlo && !inclo)
		{
			rc =
				strcmp(lobuf,
				       (dbtbl->type == INDEX_MM
					|| dbtbl->type ==
					INDEX_FULL ? wd : tempbuf));
#ifdef DEBUG
			DBGMSG(9,
			       (999, NULL,
				"(Lo)Current record scores a %d", rc));
#endif
			if (!rc)
				continue;
			else
				abvlo++;
		}
#ifdef DEBUG
		else
			DBGMSG(9,
			       (999, NULL,
				"Current record needs no check", rc));
#endif
#ifdef DEBUG
		DBGMSG(9,
		       (999, NULL, "Inserting %d into temp index",
			*(long *) tempbuf));
#endif
		if (dbtbl->type == INDEX_MM || dbtbl->type == INDEX_FULL)
		{
			/* Append `occurrenceCount' to `wd', for later
			 * recovery in tup_read_fromnewmmindex():
			 */
			wdSz = strlen(wd);
			occSz = htsnpf(occStrBuf, sizeof(occStrBuf), "%wd",
					(EPI_HUGEINT)occurrenceCount);
			if (wdSz + 1 + occSz + 1 > sizeof(mergeBuf))
			{			/* too large; do not append */
				putmsg(MERR + MAE, Fn,
				  "Row too large: OccurrenceCount truncated");
				w = wd;
				sz2 = wdSz;
			}
			else
			{
				memcpy(mergeBuf, wd, wdSz + 1);
				memcpy(mergeBuf + wdSz + 1, occStrBuf,
					occSz + 1);
				w = mergeBuf;
				sz2 = wdSz + 1 + occSz;
			}
			TXsetrecid(&btloc, rowCount);
		}
		else
		{
			w = tempbuf;
		}
		btinsert(bt, &btloc, sz2, (char *)w);
		nrecs++;
	}
	rewindbtree(bt);
#ifdef NEW_I
	ix->orig = dbidxfrombtree(bt, DBIDX_MEMORY);
#else
	ix->orig = bt;
#endif
	bt = BTREEPN;
	ix->cntorig = nrecs;
	goto done;

err:
	ix = closeiindex(ix);
done:
	bt = closebtree(bt);
	if (dbtbl->type != INDEX_MM && dbtbl->type != INDEX_FULL)
	{
		i.obj =
			TXbtcacheclose(dbtbl, iname, itype, INDEX_READ,
				       i.obj);
	}
	lobuf = TXfree(lobuf);
	hibuf = TXfree(hibuf);
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return (ix);
}

/******************************************************************/
/*	Add a Metamorph index to the table.
 */

static
	IINDEX *ixmmindex
ARGS((int, char *, CONST char *sysindexParams, FLD *, char *, DBTBL *,
      int, int *, int, TBSPEC *));
static IINDEX *ixfmmindex
ARGS((int, char *, CONST char *sysindexParams, FLD *, char *, DBTBL *,
      int, int *, int, TBSPEC *));

static IINDEX *
ixmmindex(itype, iname, sysindexParams, fld, fname, tb, op, cop, inv, tbspec)
int itype;			/* Type of index: INDEX_FULL, INDEX_MM etc. */
char *iname;			/* File path to index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS value of index */
FLD *fld;			/* Field containing Metamorph expression */
char *fname;			/* Name of table field to Metamorph search */
DBTBL *tb;			/* Table containing `fname' */
int op;				/* The operation we are doing */
int *cop;			/* Convert operation (i.e. if no post-proc) */
int inv;			/* Create inverted version of index. */
TBSPEC *tbspec;
{
	static CONST char Fn[] = "ixmmindex";
	A3DBI *dbi;
	IINDEX *ix;
	EPI_HUGEUINT nrecs;
	INDEX_TRACE_VARS;

	*cop = 0;
	if (itype == INDEX_MM || itype == INDEX_FULL)
		return ixfmmindex(itype, iname, sysindexParams, fld, fname,
				  tb, op, cop, inv, tbspec);
	ix = openiindex();
	if (!ix) return(IINDEXPN);
	if (verbose)
		putmsg(MINFO, Fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, tbspec), FOR_OP_ARG(fname, op,
				  ((DDMMAPI *)getfld(fld, NULL))->query, 1));
	if ((dbi = TXbtcacheopen(tb, iname, INDEX_3DB, INDEX_READ,
				 sysindexParams)) != NULL)
	{
		switch (op)
		{
		case FOP_RELEV:
#ifdef NEW_I
			ix->orig = dbidxfrombtree(setr3dbi(dbi, fld, fname,
					tb, &nrecs), DBIDX_MEMORY);
			ix->orig->nrank = 1;
#else
			ix->orig = setr3dbi(dbi, fld, fname, tb, &nrecs);
#endif
			ix->nrank = 1;
			ix->orank = 1;
			break;
		case FOP_PROXIM:
			switch (TXlikepmode)
			{
			case 0:
#ifdef NEW_I
				ix->orig = dbidxfrombtree(setp3dbi(dbi, fld, fname,
						    tb, &nrecs), DBIDX_MEMORY);
				ix->orig->nrank = 1;
#else
				ix->orig = setp3dbi(dbi, fld, fname,
						    tb, &nrecs);
#endif
				break;
			case 1:
#ifdef NEW_I
				ix->orig = dbidxfrombtree(setp3dbi2(dbi, fld, fname,
						     tb, &nrecs), DBIDX_MEMORY);
				ix->orig->nrank = 1;
#else
				ix->orig = setp3dbi2(dbi, fld, fname,
						     tb, &nrecs);
#endif
				break;
			}
			ix->nrank = 1;
			ix->orank = 1;
			break;
		case FOP_MM:
#ifdef NEW_I
			ix->orig = dbidxfrombtree(TXset3dbi(dbi, fld, fname, tb,
					     1, &nrecs, cop, NULL, op), DBIDX_MEMORY);
#else
			ix->orig = TXset3dbi(dbi, fld, fname, tb,
					     1, &nrecs, cop, NULL, op);
#endif
			break;
		case FOP_NMM:
		case FOP_MMIN:
#ifdef NEW_I
			ix->orig = dbidxfrombtree(TXset3dbi(dbi, fld, fname, tb,
					     0, &nrecs, cop, NULL, op), DBIDX_MEMORY);
#else
			ix->orig = TXset3dbi(dbi, fld, fname, tb,
					     0, &nrecs, cop, NULL, op);
#endif
			break;
		default:
#ifdef NEW_I
			ix->orig = dbidxfrombtree(TXset3dbi(dbi, fld, fname, tb,
					     1, &nrecs, cop, NULL, op), DBIDX_MEMORY);
#else
			ix->orig = TXset3dbi(dbi, fld, fname, tb,
					     1, &nrecs, cop, NULL, op);
#endif
			break;
		}
		TXrewinddbtbl(tb);
		TXbtcacheclose(tb, iname, INDEX_3DB, INDEX_READ, dbi);
	}
	else
	{
		*cop = 0;
		putmsg(MWARN, NULL, "Could not open index %s", iname);
		ix = closeiindex(ix);
		return NULL;
	}
	if (!ix->orig)
	{
		ix = closeiindex(ix);
		*cop = 0;
		return NULL;
	}
	ix->cntorig = nrecs;
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return ix;
}

static IINDEX *
ixfmmindex(itype, iname, sysindexParams, fld, fname, tb, op, cop, inv, tbspec)
int itype;			/* Type of index.  Must be INDEX_MM or INDEX_FULL */
char *iname;			/* Name of index */
CONST char *sysindexParams;	/* (in) SYSINDEX.PARAMS value of index */
FLD *fld;			/* Field containing Metamorph expression */
char *fname;
DBTBL *tb;
int op;				/* The operation we are doing */
int *cop;			/* Convert operation */
int inv;			/* Create inverted version of index */
TBSPEC *tbspec;

/* Add a Full Inversion Metamorph index to the table.
 */
{
	static CONST char fn[] = "ixfmmindex";
	IINDEX *ix;
	DBI_SEARCH *dbisearch = NULL;
	int rc;
	INDEX_TRACE_VARS;

	if(tb->ddic->optimizations[OPTIMIZE_LIKE_AND_NOINV])
		inv = 0;		/* WTF WTF WTF WTF WTF */
	*cop = 0;		/* just for safety */
	if ((ix = openiindex()) == IINDEXPN) goto err;
	dbisearch = (DBI_SEARCH *) TXcalloc(TXPMBUFPN, fn, 1,
						sizeof(DBI_SEARCH));
	if (!dbisearch) goto err;
	if (verbose)
		putmsg(MINFO, fn, OpeningIndexFmt, iname,
		       II_CREATING_ARG(ix, tbspec), FOR_OP_ARG(fname, op,
				  ((DDMMAPI *)getfld(fld, NULL))->query, 1));
	dbisearch->imode = INDEX_READ;
	if ((dbisearch->fip = TXbtcacheopen(tb, iname, itype,
				dbisearch->imode, sysindexParams)) == NULL)
	{
		putmsg(MWARN, fn, "Could not open index %s", iname);
		goto err;
	}
	dbisearch->iindex = ix;
	dbisearch->infld = fld;
	dbisearch->fname = fname;
	dbisearch->dbtbl = tb;
	dbisearch->nopost = 0; /* cop */
	dbisearch->op = op;
	dbisearch->inv = inv;
	dbisearch->tbspec = tbspec;

	switch (op)
	{
	case FOP_PROXIM:	/* likep */
	case FOP_RELEV:	/* liker */
		dbisearch->nopre = 0;
		rc = setf3dbi(dbisearch);
		break;
	case FOP_NMM:		/* like3 */
		dbisearch->nopre = 0;
		rc = setf3dbi(dbisearch);
		break;
	case FOP_MMIN:		/* likein */
		dbisearch->nopre = 0;
		rc = setf3dbi(dbisearch);
		break;
	case FOP_MM:		/* like */
		/* Turn on no-pre for LIKE: we're (probably) doing a
		 * post-search later, so no-pre will just toss all new
		 * records to it: setf3dbi will turn it off if it's OK
		 * to do so.
		 */
		dbisearch->nopre = 1;
		rc = setf3dbi(dbisearch);
		break;
	default:
		putmsg(MWARN, fn, "Internal error: unknown field op %d", op);
		goto err;
	}
	if (rc != 0)
		goto err;
#ifdef NEW_I /* WTF */
	if(dbisearch->inv)
	{
		if(ix->inv) ix->inv->nrank = ix->nrank;
	}
	else
	{
		if(ix->orig) ix->orig->nrank = ix->nrank;
	}
#else
	if (dbisearch->inv)
	{
		BTLOC bl1, bl2;
		size_t l1;
		BTREE *bt;

		bt = ix->inv;
		if (bt && (bt->flags & BT_FIXED))
		{
			rewindbtree(bt);
			l1 = sizeof(bl2);
			bl1 = btgetnext(bt, &l1, &bl2, NULL);
			if (TXrecidvalid(&bl1))
			{
				if (TXApp && TXApp->legacyVersion7OrderByRank)
				{
					if (TXgetoff(&bl1) > 0)
						putmsg(MERR, __FUNCTION__,
				 "Internal error: Expecting a negative rank");
				}
				else
				{
					if (TXgetoff(&bl1) < 0)
						putmsg(MERR, __FUNCTION__,
				 "Internal error: Expecting a positive rank");
				}
				if (TXgetoff(&bl2) < 0)
					putmsg(MERR, __FUNCTION__,
				  "Internal error: Expecting a positive key");
			}
			rewindbtree(bt);
		}
	}
	else
	{
		BTLOC bl1, bl2;
		size_t l1;
		BTREE *bt;

		bt = ix->orig;
		if (bt && (bt->flags & BT_FIXED))
		{
			rewindbtree(bt);
			l1 = sizeof(bl2);
			bl1 = btgetnext(bt, &l1, &bl2, NULL);
			if (TXrecidvalid(&bl1))
			{
				if (TXApp && TXApp->legacyVersion7OrderByRank)
				{
					if ((bt->flags & BT_FIXED) &&
					    (TXgetoff(&bl2) > 0))
						putmsg(MERR, __FUNCTION__,
				 "Internal error: Expecting a negative rank");
				}
				else
				{
					if ((bt->flags & BT_FIXED) &&
					    (TXgetoff(&bl2) < 0))
						putmsg(MERR, __FUNCTION__,
				 "Internal error: Expecting a positive rank");
				}
				if (TXgetoff(&bl1) < 0)
					putmsg(MERR, __FUNCTION__,
				 "Internal error: Expecting a positive key");
			}
			rewindbtree(bt);
		}
	}
#endif /* NEW_I */
	ix->cntorig = dbisearch->nhits;
	*cop = dbisearch->nopost;
	goto done;

      err:
	*cop = 0;
	ix = TXfree(ix);
      done:
	TXrewinddbtbl(tb);
	TXbtcacheclose(tb, iname, itype, dbisearch->imode, dbisearch->fip);
	dbisearch = TXfree(dbisearch);
	if ((TXtraceIndexBits & 0x8000) && ix)
	{
		putmsg(MINFO, __FUNCTION__, ReturningIindexFmt,
		       TXiindexTypeName(ix), ix, iname);
		TXdumpIindex(NULL, 2, ix);
	}
	return (ix);
}

PROXBTREE *
TXmkprox(mm, fld, fop)
MMAPI *mm;
FLD *fld;
int fop;			/* FOP_... field op (likep, like, etc.) */

/* Creates a minimal PROXBTREE object for ranking, when there's
 * no index: basically just open an RPPM, so dolikep() etc. can
 * use it later on.  WTF should be combined with setf3dbi()'s init.
 */
{
	static CONST char fn[] = "TXmkprox";
	PROXBTREE *pbt;
	MMQL *mq = MMQLPN;

	if (fop == FOP_MMIN)
		return NULL;
	pbt = (PROXBTREE *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PROXBTREE));
	if (!pbt) goto err;
	if ((mq = mmrip(mm, 0)) == MMQLPN)
		goto err;
	if ((pbt->r = openrppm(mm, mq, fop, FDBIPN, RPF_LOGICHECK)) == RPPMPN)
		goto err;

	/* Since not just ranking but row *matching* will be done in essence
	 * by rppm_rankbuf() (because TXcalcrank() et al. just return the
	 * top likeprows rows with rank > 0), make sure any needed getmmapi()
	 * post-processing (eg. for w/) gets done.  See similar call
	 * during index search in fdbi_get():  KNG 20060606
	 */
	if (RPPM_WITHIN_POSTMM_REASON(pbt->r, mm->mme))
		rppm_setflags(pbt->r, RPF_MMCHECK, 1);

	pbt->flags |= PBF_SETWEIGHTS;	/* not really, but we have none */
	pbt->f = fld;
	goto done;

      err:
	pbt = TXfree(pbt);
      done:
	TXclosemmql(mq, 0);
	return (pbt);
}

/******************************************************************/

static IINDEX *andindices ARGS((DBTBL *, IINODE *, int));

static IINDEX *
andindices(tb, in, inv)
DBTBL *tb;
IINODE *in;
int inv;
{
	static CONST char	fn[] = "andindices";
	IINDEX *ix = NULL;

	if (in->left && in->left->ipred && in->right && in->right->ipred)
		in->ipred = NULL;
	else if (in->left && in->left->ipred && in->left->index)
		in->ipred = in->left->ipred;
	else if (in->right && in->right->ipred && in->right->index)
		in->ipred = in->right->ipred;

/* Is this smarter? */
	if (in->left && in->left->gpred && in->right && in->right->gpred)
	{
		in->gpred = (PRED *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PRED));
		if (!in->gpred) return(NULL);	/* wtf */
		in->gpred->lt = 'P';
		in->gpred->rt = 'P';
		in->gpred->left = in->left->gpred;
		in->gpred->right = in->right->gpred;
		in->gpred->op = FLDMATH_AND;
		in->fgpred = 1;
	}
	else
	{
		if (in->left && in->left->gpred)
			in->gpred = in->left->gpred;
		else if (in->right && in->right->gpred)
			in->gpred = in->right->gpred;
	}

	if (!in->left || in->left->index == (IINDEX *) NULL)
	{
		if (in->right)
		{
			ix = in->right->index;
			in->right->index = NULL;
		}
		return ix;
	}
	if (!in->right || in->right->index == (IINDEX *) NULL)
	{
		if (in->left)
		{
			ix = in->left->index;
			in->left->index = NULL;
		}
		return ix;
	}
	if(tb->ddic->optimizations[OPTIMIZE_LIKE_AND])
		if (in->right && in->right->index && in->left &&
		    in->right->index->piand == in->left->index)
		{
			ix = in->right->index;
			in->right->index = NULL;
			DBGMSG(1, (999, CHARPN, "Using AND merge"));
			return ix;
		}
	ix = indexand(in->left->index, in->right->index, inv);
	tb->order = closeproj(tb->order);
	return ix;
}

/******************************************************************/

static IINDEX *orindices ARGS((DBTBL *, IINODE *, int));

static IINDEX *
orindices(tb, in, inv)
DBTBL *tb;
IINODE *in;
int inv;
{
	static CONST char	fn[] = "orindices";
	IINDEX *ix;

	if (in->left && in->right && in->left->gpred && in->right->gpred)
	{
		in->gpred = (PRED *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(PRED));
		if (!in->gpred) return(NULL);	/* wtf */
		in->gpred->lt = 'P';
		in->gpred->rt = 'P';
		in->gpred->left = in->left->gpred;
		in->gpred->right = in->right->gpred;
		in->gpred->op = FLDMATH_OR;
		in->fgpred = 1;
	}
	else if (in->left && in->left->gpred)
		in->gpred = in->left->gpred;
	else if (in->right && in->right->gpred)
		in->gpred = in->right->gpred;

	if ((in->left == NULL) || in->left->index == (IINDEX *) NULL)
		return NULL;
	if ((in->right == NULL) || in->right->index == (IINDEX *) NULL)
		return NULL;
	ix = indexor(in->left->index, in->right->index, inv);
	tb->order = closeproj(tb->order);
	return ix;
}

/* ------------------------------------------------------------------------ */

static IINDEX *txMakeEmptyIindex ARGS((void));
static IINDEX *
txMakeEmptyIindex()
/* Creates an empty (0 recids) IINDEX, eg. for <apicp denymode error>.
 * Used in circumstances where we cannot return error, so we force 0 results.
 */
{
	IINDEX	*ix;

	ix = openiindex();
	if (ix == IINDEXPN) return(IINDEXPN);
#ifdef NEW_I
	ix->orig = dbidxopen(NULL, DBIDX_BTREE, BTFSIZE, 5,
#  ifndef NO_NEW_RANK
			     (BT_FIXED | BT_UNSIGNED)
#  else
			     BT_FIXED
#  endif
			     ,
			     O_RDWR | O_CREAT);
#else
	ix->orig = openbtree(NULL, BTFSIZE, 5,
#  ifndef NO_NEW_RANK
			     (BT_FIXED | BT_UNSIGNED)
#  else
			     BT_FIXED
#  endif
			     ,
			     O_RDWR | O_CREAT);
	if (ix->orig)
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&ix->orig->params, DDICPN/*wtf*/);
#endif /* NEW_I */
	return(ix);
}

/******************************************************************/

static IINDEX *realwork(DBTBL *tb, IINODE *in, PRED *p, FLDOP *fo, int asc,
			int inv, TBSPEC *tbspec);
static IINDEX *getiinindex(DBTBL *tb, IINODE *in, PRED *p, FLDOP *fo, int asc,
			   int inv, TBSPEC *tbspec);

static IINDEX *
getiinindex(tb, in, p, fo, asc, inv, tbspec)
DBTBL *tb;
IINODE *in;
PRED *p;
FLDOP *fo;
int asc;	/* Allow short-cut, e.g. parent of `p' (if any) is AND */
int inv;
TBSPEC *tbspec;
{
	IINDEX *rc;

/* Somewhere around here we need to handle multiple FOP_MMIN terms, and
   and their relative indices together.  Basically we just collect the
   matching recids from the various indexes.  With no intermediate
   anding.
*/
	if (p->op == FOP_AND)
		return andindices(tb, in, inv);
	if (p->op == FOP_OR)
		return orindices(tb, in, inv);
	if (p->op == NOT_OP)	/* Indexes useless here. */
	{
		if (in->left && in->left->index)
			in->left->index = closeiindex(in->left->index);
		if (in->right && in->right && in->right->index)
			in->right->index = closeiindex(in->right->index);
		return NULL;
	}
	p->assumetrue++;
	rc = realwork(tb, in, p, fo, asc, inv, tbspec);
	p->assumetrue--;
	return rc;
}

/******************************************************************/

FLD *
TXpredGetColumnAndField(p, lookrightp, fnamep)
PRED *p;		/* (in) predicate to look at */
int *lookrightp;	/* (out, opt.) nonzero: param is right-side */
char **fnamep;		/* (out, opt.) name of column */
/* Returns param/literal field of `p' iff `p' is a predicate of the form
 * `column OP {$param|literal}' or `{$param|literal} OP column',
 * and sets `*lookrightp' to nonzero if RHS is the param/literal,
 * and sets `*fnamep' to the column name.
 * Returns NULL if `p' is any other form of predicate.
 */
{
	FLD *infld = FLDPN;
	int lookright = 0;

	if (fnamep != CHARPPN)
		*fnamep = CHARPN;
	if (p->lt == NAME_OP)			/* left side is a column */
	{
		if (fnamep != CHARPPN)
			*fnamep = p->left;
		if (p->rt == FIELD_OP)
			infld = (FLD *) p->right;
		else if (p->rt == PARAM_OP)
			infld = ((PARAM *) p->right)->fld;
		else if (p->rat == FIELD_OP)
			infld = (FLD *) p->altright;
		lookright = 1;
	}
	if (p->rt == NAME_OP)			/* right side is a column */
	{
		if (lookright)			/* left side also a column */
		{
			infld = FLDPN;		/*   so fail */
			goto done;
		}
		if (fnamep != CHARPPN)
			*fnamep = p->right;
		if (p->lt == FIELD_OP)
			infld = (FLD *) p->left;
		else if (p->lt == PARAM_OP)
			infld = ((PARAM *) p->left)->fld;
		else if (p->lat == FIELD_OP)
			infld = (FLD *) p->altleft;
		lookright = 0;
	}
      done:
	if (lookrightp != INTPN)
		*lookrightp = lookright;
	return (infld);
}

FLD *
TXdemoteSingleStrlstToVarchar(fld)
FLD     *fld;   /* (in) single-item strlst */
/* Returns new varchar field that is a copy of single-item strlst field `fld'.
 * Returns NULL on error, or if `fld' is multi-item.
 */
{
  static CONST char     fn[] = "TXdemoteSingleStrlstToVarchar";
  ft_strlst             sl;
  char                  *s, *e, *slEnd, *buf = NULL;
  FLD                   *ret = NULL;
  size_t                n;

  if ((fld->type & DDTYPEBITS) != FTN_STRLST) goto err;
  s = TXgetStrlst(fld, &sl);
  if (!s) goto err;
  slEnd = s + sl.nb;
  if (slEnd > s && !slEnd[-1]) slEnd--;         /* -1 for strlst-term. nul */
  for (e = s; e < slEnd && *e; e++);            /* end of str */
  if (e >= slEnd) goto err;			/* `fld' has 0 items */
  if (e + 1 < slEnd) goto err;                  /* `fld' has >1 item */
  n = e - s;
  buf = (char *)TXmalloc(NULL, fn, n + 1);
  if (!buf) goto err;
  memcpy(buf, s, n);
  buf[n] = '\0';
  ret = emptyfld((DDVARBIT | FTN_CHAR), 1);
  if (!ret) goto err;
  /* We want exactly `varchar'; do not add FTN_NotNullableFlag, may
   * defeat locfldcmp:
   */
  ret->type &= ~FTN_NotNullableFlag;
  setfldandsize(ret, buf, n + 1, FLD_FORCE_NORMAL);
  buf = NULL;                                   /* owned by `fld' now */
  goto done;

err:
  ret = closefld(ret);
done:
  buf = TXfree(buf);
  return(ret);
}

int
TXfixupMultiItemRelopSingleItem(tblColFld, colName, op, paramFld,
                                promotedParamFld, fo)
FLD             *tblColFld;     /* (in) table column field */
CONST char      *colName;       /* (in) column name */
int             op;             /* (in) FOP operator */
FLD             **paramFld;     /* (in/out) parameter/literal field */
FLD             **promotedParamFld;     /* (out) new FLD, to be freed */
FLDOP           *fo;            /* (in/out) FLDOP for scratch */
/* Bug 3677: see foslch()/fochsl() comments: for strlst RELOP varchar
 * and vice versa, promote varchar to strlst via TXVSSEP_CREATE and treat
 * as strlst RELOP strlst (for arrayconvert consistency); for non-strlst
 * non-varchar multi-item types, behavior not defined at all yet (similar?).
 * Returns 0 on error (index usage should be abandoned), 1 if ok.
 * May create `*promotedParamFld', and set `*paramFld' to it; free
 * `*promotedParamFld' when done.
 */
{
  static CONST char     fn[] = "TXfixupMultiItemRelopSingeItem";
  int                   ret, isSetOp;
  TXbool		tblColIsMultipleItemType;
  char                  *val;

  *promotedParamFld = NULL;
  if (!TXApp->strlstRelopVarcharPromoteViaCreate ||
      !(op & FOP_CMP) ||                        /* not =, !=, <, >, IN etc. */
      op == FOP_TWIXT ||  /* allow `intCol BETWEEN (9, 13)' texis test296? */
      (tblColIsMultipleItemType =
       TXfldIsMultipleItemType(tblColFld, NULL, NULL)) ==
      TXfldIsMultipleItemType(*paramFld, NULL, NULL))
    goto ok;                                    /* nothing to do */

  isSetOp = (op == FOP_IN ||
             op == FOP_IS_SUBSET ||
             op == FOP_INTERSECT ||
             op == FOP_INTERSECT_IS_EMPTY ||
             op == FOP_INTERSECT_IS_NOT_EMPTY);

  if ((tblColFld->type & DDTYPEBITS) == FTN_STRLST &&
      (((*paramFld)->type & DDTYPEBITS) == FTN_CHAR ||
       ((*paramFld)->type & DDTYPEBITS) == FTN_BYTE))
    {
      /* Promote `*paramFld' to strlst, via TXVSSEP_CREATE: */
      TXstrlstCharConfig   saveSep = TXApp->charStrlstConfig;
      ft_strlst *sl;
      size_t    n;

      *promotedParamFld = newfld(tblColFld);
      if (!*promotedParamFld) goto err;
      /* Bug 4162 #2: `create' mode in version 7+ will now convert
       * empty-string to empty-strlst (to align with Bug 3677 #12),
       * not one-empty-string strlst as in v6-.  But here we want
       * one-empty-string strlst (e.g. exception for IN, Bug 3677 #13):
       */
      if (TXVSSEP_CREATE_EMPTY_STR_TO_EMPTY_STRLST(TXApp) &&
          (val = (char *)getfld(*paramFld, &n)) != CHARPN &&
          n == 0)
			{
				TXApp->charStrlstConfig.toStrlst = TXc2s_defined_delimiter;
				TXApp->charStrlstConfig.delimiter = TxPrefStrlstDelims[0];
			}
      else
			{
				TXApp->charStrlstConfig.toStrlst = TXc2s_create_delimiter;
			}
      _fldcopy(*paramFld, NULL, *promotedParamFld, NULL, fo);
      TXApp->charStrlstConfig = saveSep;
      /* Bug 3677 #12: empty-string becomes empty-strlst, not
       * one-empty-string strlst:
       */
      if (op != FOP_IN)			/* Bug 3677 #13 but not for FOP_IN */
        {
          getfld(*paramFld, &n);
          if (n == 0)                           /* empty string */
            {
              sl = (ft_strlst *)getfld(*promotedParamFld, NULL);
              sl->nb = 1;                       /* just strlst nul */
            }
        }
      *paramFld = *promotedParamFld;
    }
  else if (isSetOp)
    goto ok;
  else if ((tblColFld->type & DDTYPEBITS) == FTN_CHAR &&
           ((*paramFld)->type & DDTYPEBITS) == FTN_STRLST)
    {
      /* Cannot promote `tblColFld' to strlst, as it is (fixed) table column.
       * Demote `*paramFld' to varchar if possible:
       */
      *promotedParamFld = TXdemoteSingleStrlstToVarchar(*paramFld);
      if (!*promotedParamFld)                   /* failed */
        {
          if (TXverbosity > 0 &&
              TXfldNumItems(*paramFld) != 1)
            {
              /* WTF we would have to embed nuls in a varchar here (to
               * hold multiple items), yet expect them *not* to terminate
               * the field; will not work in fldcmp(), _fldcopy() etc.
               * Bail on index usage: (WTF could return 0 rows immediately
               * for FOP_EQ: no varchar value is multi-item):
               */
              putmsg(MINFO, fn, "Will not look for index on column `%s': Cannot promote multi-/zero-item value `%s' to index type varchar properly for index search",
                     colName, fldtostr(*paramFld));
            }
          goto err;
        }
      else
        *paramFld = *promotedParamFld;
    }
  else if (tblColIsMultipleItemType)
    putmsg(MWARN + UGE, fn,
           "Multi-item-field (`%s') %s single-item-field behavior is undefined for other than strlst/varchar",
           colName, TXqnodeOpToStr(op, NULL, 0));
    /* allow it to proceed anyway; was caveated */
  else
    putmsg(MWARN + UGE, fn,
           "Multi-item-field %s single-item-field (`%s') behavior is undefined for other than strlst/varchar",
           TXqnodeOpToStr(op, NULL, 0), colName);
    /* allow it to proceed anyway; was caveated */
ok:
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

/******************************************************************/
/*
Basic aim -
	Find the field (either left or right)
	Get a value (from the opposite side)
	Index lookup.
*/
static IINDEX *
realwork(tb, in, p, fo, asc, inv, tbspec)
DBTBL *tb;
IINODE *in;
PRED *p;
FLDOP *fo;
int asc;	/* Allow short-cut, e.g. parent of `p' (if any) is AND */
int inv;
TBSPEC *tbspec;
{
	static CONST char Fn[] = "realwork";
	char *dname, *fname = NULL;
	int j = 0, lookright = 0, needPostProc;
	FLD *fld = NULL, *infld = NULL, *fld2 = NULL;
	/* promotedParamFld: a copy of parameter/literal `infld',
	 * promoted as needed:
	 */
	FLD	*promotedParamFld = NULL;
	IINDEX *ix = NULL;
	int rev, inclo, inchi, indexesAreDescending;
	size_t sz;
	void *v;
	DDMMAPI *ddmmapi;
	INDEXINFO indexinfo;

	DBGMSG(1, (999, NULL, "In realwork"));
	resetindexinfo(&indexinfo);
	infld = TXpredGetColumnAndField(p, &lookright, &fname);
	if (!infld)
		goto err;
	in->gpred = NULL;
	dname = dbnametoname(tb, fname, FTNPN, INTPN);
	if (!dname)
		goto err;
	fld = dbnametofld(tb, dname);
	indexinfo.tbspec = tbspec;
	switch (tb->type)
	{
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		/* It's a Metamorph index being accessed as a table
		 * (indexaccess == 1).  The "table" itself (dictionary)
		 * is its own B-tree index:
		 */
		indexinfo.itypes = (char *) TXmalloc(TXPMBUFPN, Fn, 2);
		if (!indexinfo.itypes) goto err;	/* wtf */
		indexinfo.itypes[0] = 'B';
		indexinfo.itypes[1] = '\0';
		indexinfo.paths = (char **) TXcalloc(TXPMBUFPN, Fn, 2,
						     sizeof(char *));
		if (!indexinfo.paths) goto err;	/* wtf */
		indexinfo.paths[0] = TXmalloc(TXPMBUFPN, Fn, strlen(tb->dbi->name) + 4);
		if (!indexinfo.paths[0]) goto err;	/* wtf */

		indexinfo.fields = (char **) TXcalloc(TXPMBUFPN, Fn, 2,
							sizeof(char *));
		if (!indexinfo.fields) goto err;	/* wtf */
		indexinfo.fields[0] = TXstrdup(TXPMBUFPN, Fn,
	(tb->tbl->dd->n >= 1 ? tb->tbl->dd->fd[0].name : "?" /*wtf err*/));
		if (!indexinfo.fields[0]) goto err;	/* wtf */

		indexinfo.sysindexParamsVals = (char **)TXcalloc(TXPMBUFPN, Fn, 2, sizeof(char *));
		if (indexinfo.sysindexParamsVals == CHARPPN)
			goto err;
		indexinfo.sysindexParamsVals[0] = TXstrdup(TXPMBUFPN, Fn, tb->indexAsTableSysindexParams != CHARPN ? tb->indexAsTableSysindexParams : "");
		strcpy(indexinfo.paths[0], tb->dbi->name);
		if (tb->type != INDEX_MM && tb->type != INDEX_FULL)
			strcat(indexinfo.paths[0], "_M");
		indexinfo.numIndexes = 1;
		break;
	default:
		indexinfo.numIndexes =
			ddgetindex(tb->ddic, tb->rname, dname,
				   &indexinfo.itypes, &indexinfo.paths,
				   &indexinfo.fields,
				   &indexinfo.sysindexParamsVals);
	}
	rev = indexesAreDescending = 0;
	if (indexinfo.numIndexes <= 0)
	{
		char *rname;

		closeindexinfo(&indexinfo);
		/* Now check for reversed indexes. */
		rname = TXmalloc(TXPMBUFPN, Fn, strlen(dname) + 2);
		if (!rname)
			goto err;
		strcpy(rname, dname);
		strcat(rname, "-");
		indexinfo.numIndexes =
			ddgetindex(tb->ddic, tb->rname, rname,
				   &indexinfo.itypes, &indexinfo.paths,
				   &indexinfo.fields,
				   &indexinfo.sysindexParamsVals);
		rname = TXfree(rname);
		if (indexinfo.numIndexes <= 0)
		{
			closeindexinfo(&indexinfo);
			/* Make LIKE3 do LIKE !!! WTF */
			if (p->op == FLDMATH_NMM)
				p->op = FLDMATH_MM;
		      makepbt:
			if (!TXismmop(p->op, NULL))
				goto err;
			/* Create a PROXBTREE, so post-search later can
			 * generate ranks:   KNG 980630
			 */
			fld = dbnametofld(tb, dname);
			ddmmapi = (DDMMAPI *) getfld((FLD *) infld, &sz);
			if (ddmmapi != DDMMAPIPN && fld != FLDPN)
			{
				ddmmapi->bt =
					TXmkprox(ddmmapi->mmapi, fld, p->op);
				ddmmapi->self->bt = ddmmapi->bt;
			}
			goto err;
		}
		rev = indexesAreDescending = 1;
#ifdef DEBUG
		DBGMSG(9, (999, NULL, "We found ourselves a reversed index"));
#endif
	}
	ix = NULL;

	/* If predicate is `{$param|literal} OP column' (!lookright)
	 * instead of `column OP {$param|literal}' (lookright),
	 * then negate whether we have reversed (descending) indexes.
	 * E.g. a descending index can be used with `$x < column'
	 * as if it were ascending and `column < $x'.  Note that this
	 * negation is incorrect for non-commutative operators (e.g. IN);
	 * Bug 4087 WTF:
	 */
	if (!lookright)
		rev = 1 - rev;

	/* Maybe promote `infld' for Bug 3677: */
	if (p->op != FOP_NEQ &&                 /* cannot use index anyway */
            !TXfixupMultiItemRelopSingleItem(fld, dname, p->op, &infld,
					     &promotedParamFld, fo))
		goto err;

	switch (p->op)
	{
	case FOP_TWIXT:
		if (rev != 0)
			goto err;
		fld2 = newfld(fld);

		/* KNG 20060707 due to fldop1.c:1.11 fix, in _fldcopy() an
		 * int(1) FOP_ASN [var]int(2) now gives correct int(1), not
		 * [var]int(2) as ixbttwindex() expects (but which breaks
		 * table insert elsewhere).  So make target field var to allow
		 * 2 values.  Affects "... where LongField in (5, 10)":
		 */
		if (infld->n > fld2->n) fld2->type |= DDVARBIT;

		_fldcopy((FLD *) infld, NULL, fld2, NULL, fo);
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			ix =
				ixbttwindex(indexinfo.itypes[j],
					    indexinfo.paths[j],
					    indexinfo.sysindexParamsVals[j],
					    fld2, dname, fo, tb);
			if (!ix)
				continue;
			else
			{
				in->gpred = p;
				in->ipred = p;
				break;
			}
		}
		if (fld2)
			fld2 = closefld(fld2);
		goto done;
	case FOP_IN:
	case FOP_IS_SUBSET:
	/* FOP_INTERSECT returns a set, not boolean; cannot use index */
	/* FOP_INTERSECT_IS_EMPTY is negation; cannot use index */
	case FOP_INTERSECT_IS_NOT_EMPTY:
		/* Bug 4087: can use DESC index for IN, since we only search
		 * for equality; therefore no `rev' check needed here
		 * (ixbteqindex() can check `indexesAreDescending' if needed).
		 * But still need to check `lookright', as IN/SUBSET/INTERSECT
		 * is not always commutative; handled in ixbteqindex().
		 */
		/* WTF Need a better fldcopy for FOP_IN or do we?  We
		   use fldcmp, which already accounts for differences
		   in type except we need the right type when searching
		   the tree */
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			ix =
				ixbteqindex(indexinfo.itypes[j],
					    indexinfo.paths[j],
					    indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
					    infld, dname, fo, tb,
					    p->op, lookright,
					    indexesAreDescending,
					    &needPostProc);
			if (!ix)
				continue;
			else
			{
				if (needPostProc)
					in->gpred = in->ipred = NULL;
				else
				{
					in->gpred = p;
					in->ipred = p;
				}
				break;
			}
		}
		goto done;
	case FOP_EQ:
		fld2 = newfld(fld);
		_fldcopy((FLD *) infld, NULL, fld2, NULL, fo);
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			inclo = inchi = 1;
			ix =
				ixbtindex(indexinfo.itypes[j],
					  indexinfo.paths[j],
					  indexinfo.fields[j],
					  indexinfo.sysindexParamsVals[j],
					  &fld2, &dname,
					  &fld2, &inclo, &inchi, fo, tb,
					  tbspec, 1, p->op);
			if (!ix)
				continue;
			else
			{
				in->gpred = p;
				in->ipred = p;
				break;
			}
		}
		if (fld2)
			fld2 = closefld(fld2);
		goto done;
	case FOP_GT:
	case FOP_GTE:
		fld2 = newfld(fld);
		_fldcopy((FLD *) infld, NULL, fld2, NULL, fo);
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			if (!rev)
			{
				inclo = p->op == FOP_GTE;
				inchi = 0;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  &fld2,
						  &dname, NULL, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
			}
			else
			{
				inclo = 0;
				inchi = p->op == FOP_GTE;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  NULL,
						  &dname, &fld2, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
			}
			if (!ix)
				continue;
			else
			{
				in->gpred = p;
				break;
			}
		}
		if (fld2)
			fld2 = closefld(fld2);
		goto done;
	case FOP_LT:
	case FOP_LTE:
		fld2 = newfld(fld);
		_fldcopy((FLD *) infld, NULL, fld2, NULL, fo);
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			if (!rev)
			{
				inclo = 0;
				inchi = p->op == FOP_LTE;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  NULL,
						  &dname, &fld2, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
			}
			else
			{
				inclo = p->op == FOP_LTE;
				inchi = 0;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  &fld2,
						  &dname, NULL, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
			}
			if (!ix)
				continue;
			else
			{
				in->gpred = p;
				break;
			}
		}
		if (fld2)
			fld2 = closefld(fld2);
		goto done;
	case FOP_MM:				/* LIKE */
	case FOP_MMIN:				/* LIKEIN */
		if (rev != 0)
			goto err;
		fld = createfld("varchar", 255, 0);
		v = getfld((FLD *) infld, &sz);
		putfld(fld, v, sz);
		fld->n = ((FLD *) infld)->n;
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			int cop;

			p->handled = 1;
			ix =
				ixmmindex(indexinfo.itypes[j],
					  indexinfo.paths[j],
					  indexinfo.sysindexParamsVals[j],
					  fld, dname, tb,
					  p->op, &cop, inv, tbspec);
			if (cop) /* Fully handled (no post-process) */
			{
				if(asc) /* Allow short-cut, e.g.
					 * parent of `p' (if any) is AND
					 */
					/* Set op to LIKE3, which is a
					 * no-op (returns 1) in
					 * n_fchch() etc.; LIKE3 is
					 * defined to be index-only:
					 */
					p->op = FLDMATH_NMM; /* Skip post-proc */
				else if (!tb->ddic->optimizations[OPTIMIZE_SHORTCUTS])
					p->handled = 0;
			}
			else			/* needs post-process */
			{
				p->handled = 0;
			}
			if (!ix)
			{
				continue;
			}
			else
			{
/* Can't guarantee anything at this point
					in->gpred = p;
*/
				break;
			}
		}
		closeindexinfo(&indexinfo);
		fld = closefld(fld);
		if (ix == IINDEXPN)
			goto makepbt;
		goto done;
	case FOP_NMM:				/* LIKE3 */
	case FOP_RELEV:				/* LIKER */
	case FOP_PROXIM:			/* LIKEP */
		if (rev != 0)
			goto err;
		fld = createfld("varchar", 255, 0);
		v = getfld((FLD *) infld, &sz);
		putfld(fld, v, sz);
		fld->n = ((FLD *) infld)->n;
		for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
				       lookright);
		     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright))
		{
			int cop;

			p->handled = 1;
			ix =
				ixmmindex(indexinfo.itypes[j],
					  indexinfo.paths[j],
					  indexinfo.sysindexParamsVals[j],
					  fld, dname, tb,
					  p->op, &cop, inv, tbspec);
			if (cop &&		/* convert op (no post-proc)*/
			    asc &&		/* allow short cut; parent
						 * (if any) is AND */
			    p->op == FLDMATH_PROXIM)
				/* Set op to LIKER, which is a no-op
				 * (returns 1) in n_fchch() etc.;
				 * LIKER is defined to be index-only:
				 */
				p->op = FLDMATH_RELEV;
			if (!cop ||			/* needs post-proc */
			    !asc)   /* no shortcuts, e.g. parent is not AND */
				p->handled = 0;
			if (!ix)
				continue;
			else
			{
				/* Bug 6796 comment #1: cannot
				 * guarantee `p' unless handled; see
				 * similar post-proc check above for
				 * `gpred' for FOP_IN/FOP_IS_SUBSET
				 * and FOP_MM/FOP_MMIN.  Used to set
				 * `in->gpred' here even if post-proc
				 * needed; that made end of
				 * predtoiinode() set `indguar' which
				 * prevented table read and post-proc
				 * in tup_read_indexed():
				 */
				if (p->handled) in->gpred = p;
				break;
			}
		}
		closeindexinfo(&indexinfo);
		fld = closefld(fld);
		if (ix == IINDEXPN)
			goto makepbt;
		goto done;
	case FOP_MAT:
		{
			char prefixbuf[256];
			char prefixbuf1[256];
			size_t szq;
			int rc;
			char *s, *prefix = prefixbuf, *prefix1 = prefixbuf1;

			/* Cannot use index for `$param MATCHES column': */
			if (!lookright) goto err;
			/* Probably cannot use reversed index (wtf?): */
			if (rev != 0)
				goto err;
			/* If no data, MATCHES open probably failed earlier:*/
			if (getfld(infld, SIZE_TPN) == NULL) goto fopMatBad;
			s = TXmatchgetr(infld, &szq);
			if (s == CHARPN) goto fopMatBad;
			if (szq + 5 > sizeof(prefixbuf))
			{
				errno = 0;
				if ((prefix = (char *) TXmalloc(TXPMBUFPN, Fn, szq + 5)) ==
				    CHARPN
				    || (prefix1 =
					(char *) TXmalloc(TXPMBUFPN, Fn, szq + 5)) == CHARPN)
				{
				fopMatBad:
					/* KNG 20090415 Bug 2565 Would like to
					 * bail here and return no records,
					 * but this function "cannot fail".
					 * So make a fake empty IINDEX:
					 */
					ix = txMakeEmptyIindex();
					goto fopMatDone;
				}
			}
			if (globalcp == APICPPN) globalcp = TXopenapicp();
			rc = sregprefix(s, prefix, szq + 5, &sz,
			    (TXCFF_GET_CASESTYLE(globalcp->stringcomparemode)
				 == TXCFF_CASESTYLE_IGNORE) ? 1 : 0);
			DBGMSG(1,
			       (999, NULL, "Prefix = %s(%d) %d", prefix, sz,
				rc));
			if (!sz)
			{
				/* Bug 4257 no prefix: cannot search index.
				 * But if any-suffix is allowed (i.e. expr
				 * was `*'), we *can* use entire index:
				 */
				if (rc == 2)	/*any suffix: use entire idx*/
					fld = NULL;
				else		/* Bug 4257 cannot use idx */
					goto fopMatDone;
			}
			else
			{
				fld = createfld("varchar", szq + 4, 0);
				putfld(fld, prefix, sz);
			}
			if (rc == 1 ||		/* looking for exact match */
			    (sz == 0 && rc == 2))	/* `*' */
			{
				fld2 = fld;
				inchi = 1;
			}
			else			/* looking for prefix */
			{
				memcpy(prefix1, prefix, sz + 1);
				prefix1[sz - 1]++;
				DBGMSG(1,
				       (999, NULL, "Prefix1 = %s(%d)",
					prefix1, sz));
				fld2 = createfld("varchar", 255, 0);
				putfld(fld2, prefix1, sz);
				inchi = 0;
			}
			for (j = TXchooseindex(&indexinfo, tb, p->op, infld,
					       lookright);
			     j >= 0; j = TXchooseindex(&indexinfo, tb, p->op,
						       infld, lookright))
			{
				inclo = 1;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  &fld,
						  &dname, &fld2, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
				if (!ix)
					continue;
				else
				{
					/* WTF Not always */
					in->ipred = p;
					if ((sz + 1 >= szq) ||
					    (rc == 2) || /* prefix+anything */
					    /* KNG 20120223 guarantee
					     * results if exact-match, too:
					     */
					    (rc == 1))   /* exact match */
					{
						/* Index guarantees results;
						 * no post-proc needed (?):
						 */
						in->gpred = p;
					}
					else
						in->ipred = NULL;
					break;
				}
			}
			closeindexinfo(&indexinfo);
			if (fld != fld2)
				fld2 = closefld(fld2);
			fld = closefld(fld);
		      fopMatDone:
			if (prefix != prefixbuf && prefix != CHARPN)
				prefix = TXfree(prefix);
			if (prefix1 != prefixbuf1 && prefix1 != CHARPN)
				prefix1 = TXfree(prefix1);
			goto done;
		}
	default:
#ifdef DEBUG
/* WTF: != */
		DBGMSG(1,
		       (999, NULL, "Don't know how to handle op %d", p->op));
#endif
		goto err;
	}
	/* fall through to error */

err:
	ix = closeiindex(ix);
done:
	if (promotedParamFld) promotedParamFld = closefld(promotedParamFld);
	closeindexinfo(&indexinfo);		/* Bug 6229 */
	return(ix);
}

/******************************************************************/

static IINDEX *getcomp ARGS((DBTBL *, IINODE *, PRED *, FLDOP *, TBSPEC *));

static IINDEX *
getcomp(tb, in, p, fo, tbspec)
DBTBL *tb;
IINODE *in;
PRED *p;
FLDOP *fo;
TBSPEC *tbspec;
{
	static CONST char	fn[] = "getcomp";
	int rev, inclo, inchi;
	FLD *lfld, *rfld;
	PRED *t;
	PRED *l;
	PRED *r;
	IINDEX *ix = (IINDEX *) NULL;
	INDEXINFO indexinfo;

	l = p->left;
	r = p->right;
	if (l->lt == NAME_OP && l->rt == FIELD_OP &&
	    r->lt == NAME_OP && r->rt == FIELD_OP &&
	    !strcmp(l->left, r->left))
	{
                /* Predicate `p' is (both `tblCol' table columns the same):
                 *                AND
                 *              /     \
                 *       some-op       some-op
                 *       /     \       /     \
                 *   tblCol  field  tblCol  field
                 */
		char *dname;
		int j = 0, lookright = 1;
		in->gpred = NULL;
		if (l->op > r->op)	/* Order the predicates */
		{
			t = l;
			l = r;
			r = t;
		}
		switch (l->op) /* Should match switch below */
		{
		case FOP_EQ:	/* Don't really need to check r? */
		case FOP_GT:
		case FOP_GTE:
		case FOP_LT:
		case FOP_LTE:
			break;
		default:
			return NULL;
		}
		dname = dbnametoname(tb, l->left, FTNPN, INTPN);
		if (!dname)
			return NULL;
		resetindexinfo(&indexinfo);
		indexinfo.tbspec = tbspec;
		lfld = dbnametofld(tb, dname);
		indexinfo.numIndexes =
			ddgetindex(tb->ddic, tb->rname, dname,
				   &indexinfo.itypes, &indexinfo.paths,
				   &indexinfo.fields,
				   &indexinfo.sysindexParamsVals);
		rev = 0;
		if (indexinfo.numIndexes <= 0)
		{
			char *rname;

			closeindexinfo(&indexinfo);
			/* Now check for reversed indexes. */
			rname = TXmalloc(TXPMBUFPN, fn, strlen(dname) + 2);
			if (!rname)
				return NULL;
			strcpy(rname, dname);
			strcat(rname, "-");
			indexinfo.numIndexes =
				ddgetindex(tb->ddic, tb->rname, rname,
					   &indexinfo.itypes, &indexinfo.paths,
					   &indexinfo.fields,
					   &indexinfo.sysindexParamsVals);
			rname = TXfree(rname);
			if (indexinfo.numIndexes <= 0)
			{
				closeindexinfo(&indexinfo);
				/* Now check for reversed indexes. */
				return NULL;
			}
			rev = 1;
#ifdef DEBUG
			DBGMSG(9,
			       (999, NULL,
				"We found ourselves a reversed index"));
#endif
		}
		rfld = newfld(lfld);
		lfld = newfld(lfld);
		ix = NULL;
		_fldcopy((FLD *) l->right, NULL, lfld, NULL, fo);
		_fldcopy((FLD *) r->right, NULL, rfld, NULL, fo);
#ifdef DEBUG
		DBGMSG(9,
		       (999, NULL, "l->op = %d, r->op = %d", l->op, r->op));
		DBGMSG(9,
		       (999, NULL, "l = %d, r = %d", *(long *) lfld->v,
			*(long *) rfld->v));
#endif
		switch (l->op) /* Should match switch above */
		{
		case FOP_EQ:	/* Don't really need to check r? */
			rfld = closefld(rfld);
			for (j = TXchooseindex(&indexinfo, tb, l->op, lfld,
					       lookright);
			     j >= 0; j = TXchooseindex(&indexinfo, tb, l->op,
						       lfld, lookright))
			{
				inclo = 1;
				inchi = 1;
				ix =
					ixbtindex(indexinfo.itypes[j],
						  indexinfo.paths[j],
					          indexinfo.fields[j],
					    indexinfo.sysindexParamsVals[j],
						  &lfld,
						  &dname, &lfld, &inclo,
						  &inchi, fo, tb, tbspec, 1,
						  p->op);
				if (!ix)
					continue;
				else
				{
					in->gpred = l;
					in->ipred = l;
					break;
				}
			}
			lfld = closefld(lfld);
			closeindexinfo(&indexinfo);
			return ix;
		case FOP_GT:
		case FOP_GTE:
			rfld = closefld(rfld);
			for (j = TXchooseindex(&indexinfo, tb, l->op, lfld,
					       lookright);
			     j >= 0; j = TXchooseindex(&indexinfo, tb, l->op,
						       lfld, lookright))
			{
				if (!rev)
				{
					inclo = l->op == FOP_GTE;
					inchi = 0;
					ix =
						ixbtindex(indexinfo.itypes[j],
							  indexinfo.paths[j],
						          indexinfo.fields[j],
					      indexinfo.sysindexParamsVals[j],
							  &lfld, &dname, NULL,
							  &inclo, &inchi, fo,
							  tb, tbspec, 1,p->op);
				}
				else
				{
					inclo = 0;
					inchi = l->op == FOP_GTE;
					ix =
						ixbtindex(indexinfo.itypes[j],
							  indexinfo.paths[j],
						          indexinfo.fields[j],
					      indexinfo.sysindexParamsVals[j],
							  NULL, &dname, &lfld,
							  &inclo, &inchi, fo,
							  tb, tbspec, 1,p->op);
				}
				if (!ix)
					continue;
				else
				{
					in->gpred = l;
					break;
				}
			}
			lfld = closefld(lfld);
			closeindexinfo(&indexinfo);
			return ix;
		case FOP_LT:
		case FOP_LTE:
			if (r->op != FOP_GT && r->op != FOP_GTE)
				rfld = closefld(rfld);
#ifdef DEBUG
			else
				DBGMSG(9,
				       (999, NULL,
					"We can do something with %s",
					l->left));
#endif
			for (j = TXchooseindex(&indexinfo, tb, l->op, lfld,
					       lookright);
			     j >= 0; j = TXchooseindex(&indexinfo, tb, l->op,
						       lfld, lookright))
			{
				if (!rev)
				{
					inclo = r->op == FOP_GTE;
					inchi = l->op == FOP_LTE;
					ix =
						ixbtindex(indexinfo.itypes[j],
							  indexinfo.paths[j],
						          indexinfo.fields[j],
					      indexinfo.sysindexParamsVals[j],
							  &rfld, &dname,
							  &lfld, &inclo,
							  &inchi, fo, tb,
							  tbspec, 1, p->op);
				}
				else
				{
					inclo = l->op == FOP_LTE;
					inchi = r->op == FOP_GTE;
					ix =
						ixbtindex(indexinfo.itypes[j],
							  indexinfo.paths[j],
						          indexinfo.fields[j],
					      indexinfo.sysindexParamsVals[j],
							  &lfld, &dname,
							  &rfld, &inclo,
							  &inchi, fo, tb,
							  tbspec, 1, p->op);
				}
				if (!ix)
					continue;
				else
				{
					if (rfld)
						in->gpred = p;
					else
						in->gpred = l;
					break;
				}
			}
			rfld = closefld(rfld);
			lfld = closefld(lfld);
			closeindexinfo(&indexinfo);
			return ix;
		default:
			rfld = closefld(rfld);
			lfld = closefld(lfld);
			closeindexinfo(&indexinfo);
			return NULL;
		}
	}
	return 0;
}

/******************************************************************/

static IINDEX *getcomp2 ARGS((DBTBL *, IINODE *, PRED *, FLDOP *, TBSPEC *));

static IINDEX *
getcomp2(tb, in, p, fo, tbspec)
DBTBL *tb;
IINODE *in;
PRED *p;
FLDOP *fo;
TBSPEC *tbspec;
{
	int rev;
	FLD *lfld, *rfld;
	FLD *sfld[2], *efld[2];
	char *names[2];
	int incs[2], ince[2];
	int rightisgood = 1;
	IINDEX *ix = (IINDEX *) NULL;
	PRED *l = p->left;
	PRED *r = p->right;

	if (!tb->ddic->optimizations[OPTIMIZE_COMPOUND_INDEX])
		return NULL;
	if (l->lt == NAME_OP && l->rt == FIELD_OP &&
	    r->lt == NAME_OP && r->rt == FIELD_OP &&
	    strcmp(l->left, r->left) && l->op == FOP_EQ)
	{
		char *dname3;
		char *itype = NULL, **iname = NULL;
		char **sysindexFieldsVals = CHARPPN;
		char **sysindexParamsVals = CHARPPN;
		int numindex;
		int j = 0;

		in->gpred = NULL;
		names[0] = dbnametoname(tb, l->left, FTNPN, INTPN);
		if (!names[0])
			return NULL;
		names[1] = dbnametoname(tb, r->left, FTNPN, INTPN);
		if (!names[1])
			return NULL;
		lfld = dbnametofld(tb, names[0]);
		rfld = dbnametofld(tb, names[1]);
		sfld[0] = newfld(lfld);
		sfld[1] = newfld(rfld);
		efld[0] = newfld(lfld);
		efld[1] = newfld(rfld);
		incs[0] = incs[1] = ince[0] = ince[1] = 1;
		dname3 = TXstrcat4(",", names[0], " ", names[1]);
		if (dname3)
		{
			numindex =
				ddgetindex(tb->ddic, tb->rname, dname3,
					   &itype, &iname, &sysindexFieldsVals,
					   &sysindexParamsVals);
			dname3 = TXfree(dname3);
		}
		else
			numindex = 0;
		rev = 0;
		if (numindex <= 0)
		{
			closefld(sfld[0]);
			closefld(sfld[1]);
			closefld(efld[0]);
			closefld(efld[1]);
			return NULL;
		}
		ix = NULL;
		_fldcopy((FLD *) l->right, NULL, sfld[0], NULL, fo);
		_fldcopy((FLD *) r->right, NULL, sfld[1], NULL, fo);
		_fldcopy((FLD *) l->right, NULL, efld[0], NULL, fo);
		_fldcopy((FLD *) r->right, NULL, efld[1], NULL, fo);
#ifdef DEBUG
		DBGMSG(9,
		       (999, NULL, "l->op = %d, r->op = %d", l->op, r->op));
		DBGMSG(9,
		       (999, NULL, "l = %d, r = %d", *(long *) lfld->v,
			*(long *) rfld->v));
#endif
		switch (l->op)
		{
		case FOP_EQ:	/* Don't really need to check r? */
			switch (r->op)
			{
			case FOP_EQ:
				break;
			case FOP_LT:
				ince[1] = 0;
				sfld[1] = closefld(sfld[1]);
				/* efld[1] = closefld(efld[1]); */
				break;
			case FOP_LTE:
				ince[1] = 1;
				sfld[1] = closefld(sfld[1]);
				/* efld[1] = closefld(efld[1]); */
				break;
			case FOP_GT:
				incs[1] = 0;
				efld[1] = closefld(efld[1]);
				/* efld[1] = closefld(efld[1]); */
				break;
			case FOP_GTE:
				incs[1] = 1;
				efld[1] = closefld(efld[1]);
				/* efld[1] = closefld(efld[1]); */
				break;
			default:
				sfld[1] = closefld(sfld[1]);
				efld[1] = closefld(efld[1]);
				rightisgood = 0;
			}
			for (j = 0; j < numindex; j++)
				if (itype[j] == INDEX_BTREE)
				{
					ix =
						ixbtindex(itype[j], iname[j],
							sysindexFieldsVals[j],
							sysindexParamsVals[j],
							  sfld, names, efld,
							  incs, ince, fo, tb,
							  tbspec, 2, l->op);
					if (!ix)
						continue;
					else
					{
						if (rightisgood)
							in->gpred = p;
						else
							in->gpred = l;
						in->ipred = l;
						break;
					}
				}
			iname = TXfreeStrList(iname, numindex);
			sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals,
							   numindex);
			sysindexParamsVals = TXfreeStrList(sysindexParamsVals,
							   numindex);
			itype = TXfree(itype);
			closefld(sfld[0]);
			closefld(sfld[1]);
			closefld(efld[0]);
			closefld(efld[1]);
			return ix;
		default:
			rfld = closefld(rfld);
			iname = TXfreeStrList(iname, numindex);
			sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals,
							   numindex);
			sysindexParamsVals = TXfreeStrList(sysindexParamsVals,
							   numindex);
			itype = TXfree(itype);
			closefld(sfld[0]);
			closefld(sfld[1]);
			closefld(efld[0]);
			closefld(efld[1]);
			return NULL;
		}
	}
	return 0;
}

/******************************************************************/

static int TXneedindex ARGS((PRED *));

static int
TXneedindex(p)
PRED *p;
{
	int rc = 0;

	switch (p->op)
	{
	case FOP_NMM:
	case FOP_RELEV:
#ifdef LIKEP_REQ_INDEX
	case FOP_PROXIM:
#endif
		return 1;
	default:
		break;
	}
	if (p->lt == 'P')
		rc = TXneedindex(p->left);
	if (rc == 1)
		return rc;
	if (p->rt == 'P')
		rc = TXneedindex(p->right);
	if (rc == 1)
		return rc;
	return 0;
}

/******************************************************************/

static IINODE *predtoiinode(DBTBL *tb, PRED *p, TBSPEC *order, FLDOP *fo,
			    int asc, int inv);

static IINODE *
predtoiinode(tb, p, order, fo, asc, inv)
DBTBL *tb;
PRED *p;
TBSPEC *order;
FLDOP *fo;
int asc;	/* Allow short-cut, e.g. parent of `p' (if any) is AND */
int inv;			/* Get index ready for AND */
{
	static CONST char	fn[] = "predtoiinode";
	static const char linearQueryFmt[] =
		"Query `%s' would require linear search%s%s";
	IINODE *iin;
	PRED *ogpred = (PRED *) NULL;

	if (!p)
		return NULL;
	if (pred_allhandled(p))	/* Someone else has already handled this */
		return NULL;
	if (!TXkeepgoing(tb->ddic))
		return NULL;
	if (p->op == NOT_OP)	/* Can't handle NOT ... yet */
		return NULL;
#ifdef CACHE_IINODE
	iin = TXgetcachediinode(tb, p, fo, asc, inv);
	if (iin)
		return iin;
#endif /* CACHE_IINODE */
	iin = (IINODE *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(IINODE));
	if (!iin)
		return iin;
	if (p->lt == 'P' && p->rt == 'P' && p->op == FOP_AND)
	{
/* What we want to do here is sortout what we can do with this predicate */
		p->assumetrue++;
		iin->index = getcomp(tb, iin, p, fo, order);
		if (iin->index)
		{
#ifdef DEBUG
			DBGMSG(9,
			       (999, NULL, "We did some amazing stuff here"));
#endif
			iin->op = FOP_AND;
		}
		else
			iin->index = getcomp2(tb, iin, p, fo, order);
		if (iin->index)
		{
#ifdef DEBUG
			DBGMSG(9,
			       (999, NULL, "We did some amazing stuff here"));
#endif
			iin->op = FOP_AND;
		}
		p->assumetrue--;
	}
	if (!iin->index)
	{
		if (p->lt == 'P')
		{
			if (order)
			{
				ogpred = order->gpred;
				if (p->op == FOP_OR)
					order->gpred = p->left;
			}
			iin->left =
				predtoiinode(tb, p->left, order, fo,
					     (p->op == FOP_AND) && asc, 1);
			if (order)
				order->gpred = ogpred;
		}
		if (iin->left &&	/* There was a left hand side */
		    iin->left->index &&	/* With an index */
		    iin->left->index->cntorig < (EPI_HUGEUINT) TXmaxlinearrows &&	/* Less than TXmaxlinearrows */
		    p->op == FOP_AND &&	/* Only if AND (or needs other */
		    p->rt == 'P' &&	/* It is a predicate */
		    asc && /*allow shortcuts; parent (if any) of `p' is AND*/
#ifndef NO_NEW_RANK
		    !TXneedindex(p->right) &&
#endif
		    ((PRED *) p->right)->op != FOP_NMM)	/* Force LIKE3 */
		{
#ifndef LIKEP_REQ_INDEX
			if (((PRED *) p->right)->op == FOP_PROXIM)
			{
				DDMMAPI *ddmmapi;
				MMAPI *mm;
				FLD *fld;

				fld =
					dbnametofld(tb,
						    ((PRED *) p->
						     right)->left);
				if (fld)
				{
				  ddmmapi =
					getfld(((PRED *) p->right)->right,
					       NULL);
				  if (ddmmapi)
				  {
				    mm = ddmmapi->mmapi;
				    ddmmapi->bt = TXmkprox(mm, fld,FOP_PROXIM);
				  }
				}
			}
#endif
			iin->right = NULL;	/* Do nothing real here */
		}
		else if (p->rt == 'P')
		{
			if (order)
			{
				if (p->op == FOP_AND && iin->left)
					order->pind = iin->left->index;
				else
					order->pind = NULL;
				ogpred = order->gpred;
				if (p->op == FOP_OR)
					order->gpred = p->right;
			}
			iin->right =
				predtoiinode(tb, p->right, order, fo,
					     (p->op == FOP_AND) && asc, 1);
			if (order)
				order->gpred = ogpred;
		}		/* WTF - Check for small right side?  Might save time */
		iin->op = p->op;
		iin->index = getiinindex(tb, iin, p, fo, asc, inv, order);
#ifndef OLD_ALLINEAR
		if (iin->index == (IINDEX *) NULL && TXismmop(iin->op, NULL))
		{
			FLD *fld;
			DDMMAPI *dd;
			IINDEX *ix;

			/* No index hits for a Metamorph search, either
			 * because the index couldn't resolve the query or
			 * there was no index.  If allinear is off, then
			 * we can't allow this linear search.  Johannes
			 * says it's too hard to bail out here, so just
			 * yap and fake 0 records from the "index":
			 * KNG 980916
			 */
			/* KNG 000111 could also be qmaxsetwords.  wtf
			 * indicate this with a non-global var:
			 */
			if (FdbiBonusError)
			{
				FdbiBonusError = 0;
				goto makeEmptyIndex;
			}
			if ((fld = TXpredGetColumnAndField(p, INTPN, CHARPPN))
				!= FLDPN
			    && (dd =
				(DDMMAPI *) getfld(fld, NULL)) != DDMMAPIPN
			    && dd->mmapi != MMAPIPN
			    && dd->mmapi->acp != APICPPN
			    && !dd->mmapi->acp->allinear)
			{
				char *q;
				const char	*reason, *pfx;

				q = (dd->mmapi->mme == (MM3S *) NULL ? "?" : dd->mmapi->mme->query);	/* KNG 990304 core */
				/* wtf would like to get reason direct
				 * from FDBI object for this search,
				 * not a global; could be unrelated:
				 */
				reason = TXfdbiGetLinearReason();
				if (reason)
					pfx = ": ";
				else
					reason = pfx = "";
				switch (dd->mmapi->acp->denymode)
				{
				case API3DENYWARNING:
					putmsg(MWARN + UGE, CHARPN,
					       linearQueryFmt, q, pfx, reason);
					/* fall through */
				case API3DENYSILENT:
					break;
				case API3DENYERROR:
					putmsg(MERR + UGE, CHARPN,
					       linearQueryFmt, q, pfx, reason);
					break;	/* WTF cause real error? */
				}
				/* Avoid potentially yapping again later,
				 * if ORed with an unindexed clause:
				 */
				p->didPostProcLinearMsg = 1;
			      makeEmptyIndex:
				ix = txMakeEmptyIindex();
				if (ix != IINDEXPN) iin->index = ix;
			}
		}
#endif /* !OLD_ALLINEAR */
	}
	if (pred_allhandled(p))
		tb->indguar = 1;
	else
		tb->indguar = TXpredcmp(p, iin->gpred);
	return iin;
}

/******************************************************************/


#ifdef CACHE_IINODE

IINODE *
TXpredtoiinode(tb, p, order, fo, asc, inv)
DBTBL *tb;
PRED *p;
TBSPEC *order;
FLDOP *fo;
int asc;	/* Allow short-cut, e.g. parent of `p' (if any) is AND */
int inv;			/* Get index ready for AND */
{
	return predtoiinode(tb, p, order, fo, asc, inv);
}

#endif

/******************************************************************/
/*	Hunt down an index that we can use.  This goes column by
 *	column, and checks each column for its usefullness.  If
 *	all regular columns fail the psuedo column recid is tried
 *
 *	Rather than going column by column, this should probably
 *	go index by index, and build up a matching index.
 *
 *	Currently there are no return codes.  It does the best it
 *	can.  There is no "REAL BAD" failure, as TEXIS can
 *	continue without indexes, albeit more slowly (usually).
 *
 *	Currently quits after the first match
 */

static int donoindx ARGS((DBTBL *, TBSPEC *, FLDOP *, int));

static int
donoindx(DBTBL * tb, TBSPEC * tbspec, FLDOP * fo, int allowbubble)
/* Returns 1 if bubble-up index used, 0 otherwise (no index, or MM,
 * or bubble == 0 index).
 */
{
	IINODE *iinode;
        DBTBL   *savtbl;
	int	ret = 0;

        savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = tb;			/* for btreelog debug */

	if (!tbspec->pred)	/* Remove index */
	{
		rmindex(tb);
		goto done;
	}
	if (!tb->rname &&
	    !(tb->type == INDEX_3DB || tb->type == INDEX_MM
	      || tb->type == INDEX_FULL))
		goto done;
#ifndef NO_BUBBLE_INDEX
	if (allowbubble
	    && TXtrybubble(tb, /*WTF*/ tbspec->pred, /*WTF*/ tbspec->proj, fo,
			   tbspec) == 1)
	{
		ret = 1;			/* bubble-up index used */
		goto done;
	}
#endif
	iinode = predtoiinode(tb, tbspec->pred, tbspec, fo, 1, 0);
	if (iinode && iinode->index)
	{
#ifdef DEBUG
		if (tb->index.btree)
			DBGMSG(9, (999, NULL, "Index already exists"));
#endif
		DBGMSG(1, (999, NULL, "NRANK = %d", iinode->index->nrank));
#ifndef NO_NEW_RANK
		tb->index.nrank = iinode->index->nrank;
#endif
		if (iinode->index->orig)
		{
#ifdef NEW_I
			tb->index = *(iinode->index->orig);
#else
			tb->index.btree = iinode->index->orig;
#endif
		}
		else if (iinode->index->mirror)
		{
#ifdef NEW_I
			tb->index = *(iinode->index->mirror);
#else
			tb->index.btree = iinode->index->mirror;
#endif
			iinode->index->km = 1;
		}
		else if (iinode->index->inv)
		{
			iinode->index->orig = iinode->index->inv;
			iinode->index->inv = NULL;
			TXindexinv(iinode->index);
#ifdef NEW_I
			dbidxclose(iinode->index->orig);
#else
			closebtree(iinode->index->orig);
#endif
			iinode->index->orig = iinode->index->inv;
			iinode->index->inv = NULL;
#ifdef NEW_I
			tb->index = *(iinode->index->orig);
#else
			tb->index.btree = iinode->index->orig;
#endif
		}
#ifndef NO_BUBBLE_INDEX
		tb->index.type = DBIDX_MEMORY;
#endif
		TXindcnt = iinode->index->cntorig;
		tb->indcnt = iinode->index->cntorig;
		tb->index.nrecs = iinode->index->cntorig;
		/* KNG 20081211 pass on likeprows-reduced count too: */
		tb->index.rowsReturned = iinode->index->rowsReturned;
#ifdef DEBUG
		DBGMSG(9,
		       (999, NULL, "Index has %wd rows",
			(EPI_HUGEUINT) TXindcnt));
#endif
		tb->ipred = iinode->ipred;
		iinode->index->ko = 1;
	}
	iinode = closeiinode(iinode, 0);
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return(ret);
}

/******************************************************************/
/*	Try and attach an order to the recids in the index of
 *	the table.  The ordering is specified in the projection.
 */

static int doorder ARGS((DBTBL *tb, PROJ *order, FLDOP *fo, QNODE_OP qnop));

static int
doorder(tb, order, fo, qnop)
DBTBL *tb;
PROJ *order;
FLDOP *fo;
QNODE_OP	qnop;	/* (in) op being performed (GROUP_BY_OP/ORDER_BY_OP) */
{
	static CONST char	Fn[] = "doorder";
	int ni, i = 0, found = 0, rev = 0;
	int qryindex = 1;
	char *fname = NULL;
	char	**sysindexFieldsVals = NULL;
	char *itype, **iname = CHARPPN, **sysindexParamsVals = CHARPPN;
	PRED *pred;
	IINDEX *ia, *ib;
	BTREE *bttemp;
	DBIDX *tidx = NULL;
	int	dbidxFlags;
	INDEX_TRACE_VARS;

	(void)fo;
	if (!order)		/* Order not specced, so ... */
		return 0;
	if (!tb->rname)		/* Not a real table */
		return 0;
#ifdef NEVER			/* WTF */
	if (tb->index.type == DBIDX_CACHE)
		closedbidx(&tb->index);
#endif
	if (!tb->index.btree)	/* If there's no index allow use of BTREE */
	{
		qryindex = 0;
	}
/* Try and find an index which matches the order.  Initially look for
   one index matching a single order by */
	if (tb->order)		/* Already ordered */
		return 0;

	if (order->n != 1)
		return 0;

	ia = openiindex();
	if (!ia)
		return 0;
	ib = openiindex();
	if (!ib)
	{
		ia = closeiindex(ia);
		return 0;
	}

	pred = order->preds[0];
	if (!pred || pred->op != 0 || pred->lt != NAME_OP)
	{
	bail1:
		closeiindex(ia);
		closeiindex(ib);
		return 0;
	}
#ifdef NEW_I
	ia->orig = createdbidx();
	*ia->orig = tb->index;
#else
	ia->orig = tb->index.btree;
	ia->ko = 1;
#endif
	fname = TXpredToFieldOrderSpec(pred);
	if (!fname) goto bail1;

/* Try to find an index ordered correctly. */
	ni = ddgetindex(tb->ddic, tb->rname, fname, &itype, &iname,
			&sysindexFieldsVals, &sysindexParamsVals);
	if(qryindex)
		for (i = 0; i < ni && !found; i++)
			if (itype[i] == INDEX_INV)
				found++;
	if (!found)
	{
		if(!qryindex)
			for (i = 0; i < ni && !found; i++)
				if (itype[i] == INDEX_BTREE)
					found++;
		if (!found)
		{
			/* Try to find an index in the wrong order */
			if (fname && fname[strlen(fname) - 1] == '-')
			{
				rev++;
				fname[strlen(fname) - 1] = '\0';
			}
			else if (fname)
			{
				char	*newFname;

				rev++;
				newFname = TXstrcatN(TXPMBUFPN, __FUNCTION__,
						     fname, "-", NULL);
				fname = TXfree(fname);
				fname = newFname;
				if (!fname) return(0);	/* wtf */
			}
			iname = TXfreeStrList(iname, ni);
			sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals,
							   ni);
			sysindexParamsVals = TXfreeStrList(sysindexParamsVals,
							   ni);
			itype = TXfree(itype);
			ni = ddgetindex(tb->ddic, tb->rname, fname, &itype,
					&iname, &sysindexFieldsVals,
					&sysindexParamsVals);
			if(qryindex)
			{
				for (i = 0; i < ni && !found; i++)
					if (itype[i] == INDEX_INV)
						found++;
			}
			if (!found)
			{
				if(!qryindex)
					for (i = 0; i < ni && !found; i++)
						if (itype[i] == INDEX_BTREE)
							found++;
			}
			fname = TXfree(fname);
			if (!found)
				goto end;
		}
		else
		{
			fname = TXfree(fname);
		}
		i--;		/* Compensate for the last increment */
		if (itype[i] == INDEX_BTREE)
		{
#ifndef NO_SAFE_BTREE
			if (!rev)
			{
				if (qnop == ORDER_OP ||
				    qnop == ORDERNUM_OP)
					/* ORDER BY: whole value; no split: */
					dbidxFlags = 0x0;
				else		/* GROUP_BY_OP */
				if (TXApp->multiValueToMultiRow)
				/* If multivaluetomultirow active, Bug 2397
				 * still depends on non-de-duped split-val
				 * index results for GROUP BY strlst:
				 */
					/*no de-dup, split needed iff multi:*/
					dbidxFlags = 0xe;
				else
					/* If not multivaluetomultirow,
					 * we need a non-split index:
					 */
					dbidxFlags = 0x0;  /* split not ok */
				tidx = opendbidx(itype[i], iname[i],
						sysindexFieldsVals[i],
						sysindexParamsVals[i], tb,
						dbidxFlags);
			}
			else
#endif
#ifdef NEW_I
				ib->orig =
					dbidxopen(iname[i], DBIDX_BTREE, BT_MAXPGSZ, 20, 0,
						  O_RDONLY);
#else
			{
				if (TXverbosity > 0)
					putmsg(MINFO, Fn, OpeningIndexFmt,
					       iname[i], II_CREATING_ARG(ib, TBSPECPN),
					       (*forBuf = '\0', forBuf) /* wtf */);
				ib->orig =
					openbtree(iname[i], BT_MAXPGSZ, 20, 0,
						  O_RDONLY);
				if (ib->orig &&
				    bttexttoparam(ib->orig,
						  sysindexParamsVals[i]) < 0)
					ib->orig = closebtree(ib->orig);
			}
#endif
		}
		else
#ifdef NEW_I
			ib->inv =
				dbidxopen(iname[i], DBIDX_BTREE, BT_MAXPGSZ, 20, 0,
					  O_RDONLY);
#else
		{
			if (TXverbosity > 0)
				putmsg(MINFO, Fn, OpeningIndexFmt,
				       iname[i], II_CREATING_ARG(ib, TBSPECPN),
				       (*forBuf = '\0', forBuf) /* wtf */);
			ib->inv =
				openbtree(iname[i], BT_MAXPGSZ, 20, 0,
					  O_RDONLY);
			if (ib->inv &&
			    bttexttoparam(ib->inv, sysindexParamsVals[i]) < 0)
				ib->inv = closebtree(ib->inv);
		}
#endif
	}
	else
	{
		i--;		/* Compensate for the last increment */
#ifdef NEW_I
		ib->inv = dbidxopen(iname[i], DBIDX_BTREE, BT_MAXPGSZ,
					20, 0, O_RDONLY);
#else
		if (TXverbosity > 0)
			putmsg(MINFO, Fn, OpeningIndexFmt, iname[i],
			       II_CREATING_ARG(ib, TBSPECPN), (*forBuf = '\0', forBuf) /* wtf */);
		ib->inv = openbtree(iname[i], BT_MAXPGSZ, 20, 0, O_RDONLY);
		if (ib->inv &&
		    bttexttoparam(ib->inv, sysindexParamsVals[i]) < 0)
			ib->inv = closebtree(ib->inv);
#endif
	}
	bttemp = NULL;
#ifdef NEW_I
	if (qryindex && ia->orig->btree)
#else
	if (ia->orig)
#endif
	{
		if (tb->index.type == DBIDX_CACHE)
		{
			if (TXindsort2(ia, ib, rev, &tb->index) == -1)
				goto end;
		}
		else
		{
			if (indsort(ia, ib, rev) == -1)
				goto end;
		}
/*
	WTF = Do we need an inverted index for $rank
*/
		if (tb->index.nrank)
		{
			TXindexinv(ia);
			ia->ki = 1;
#ifdef NEW_I
			if(ia->inv)
				tb->rankindex = *(ia->inv);
#else
			tb->rankindex.btree = ia->inv;
#endif
			tb->rankindex.nrank = tb->index.nrank;
			tb->index.nrank = 0;
		}
#ifndef NO_BUBBLE_INDEX
		closedbidx(&tb->index);
#ifdef NEW_I
		tb->index = *ia->ordered;
#else
		tb->index.btree = ia->ordered;
		tb->index.type = DBIDX_MEMORY;
#endif
		ia->ko = 1;
#else
		tb->index.btree = ia->ordered;
		ia->ko = 0;
#endif
		ia->ks = 1;
	}
	else if (ib->orig)
	{
		if (!rev)
		{
#ifndef NO_SAFE_BTREE
			if (tidx)
			{
				tb->index = *tidx;
				tidx = NULL;
			}
#else /* SAFE_BTREE */
			tb->index.btree = ib->orig;
#ifndef NO_BUBBLE_INDEX
			tb->index.type = DBIDX_MEMORY;
#endif
			ib->ko = 1;
#endif /* SAFE_BTREE */
		}
		else
		{
			_indrev(ib);
#ifdef NEW_I
			if(ib->rev)
			{
				tb->index = *(ib->rev);
			}
#else
			tb->index.btree = ib->rev;
#ifndef NO_BUBBLE_INDEX
			tb->index.type = DBIDX_MEMORY;
#endif
#endif /* NEW_I */
			ib->kv = 1;
		}
	}
	if (tb->index.btree)
		tb->order = dupproj(order);
	else
		tb->order = NULL;
      end:
	iname = TXfreeStrList(iname, ni);
	sysindexFieldsVals = TXfreeStrList(sysindexFieldsVals, ni);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, ni);
	itype = TXfree(itype);
	if (ia)
		ia = closeiindex(ia);
	if (ib)
		ib = closeiindex(ib);
	fname = TXfree(fname);
#if !defined(NO_SAFE_BTREE) && defined(NEVER)
	if (tidx)
		closedbidx(tidx);
#endif
	return 0;
}

/* ------------------------------------------------------------------------ */

static PROJ *
TXmakeOrderByRankProj(TXPMBUF *pmbuf)
/* Creates a PROJ for `ORDER BY $rank DESC', the default ordering for LIKEP.
 */
{
  PROJ  *proj = NULL;
  PRED  *pred = NULL;

  pred = TX_NEW(pmbuf, PRED);
  if (!pred) goto err;
#ifdef TX_USE_ORDERING_SPEC_NODE
  pred->left = TXstrdup(pmbuf, __FUNCTION__, TXrankColumnName);
  if (!(TXApp && TXApp->legacyVersion7OrderByRank))
	  /* With positive ranks internally, we must ORDER BY $rank *DESC*: */
	  pred->orderFlags |= OF_DESCENDING;
#else /* !TX_USE_ORDERING_SPEC_NODE */
  if (TXApp && TXApp->legacyVersion7OrderByRank)
	  pred->left = TXstrdup(pmbuf, __FUNCTION__, TXrankColumnName);
  else
	  /* With positive ranks internally, we must ORDER BY $rank *DESC*: */
	  pred->left = TXstrcatN(pmbuf, __FUNCTION__, TXrankColumnName, "-",
				 NULL);
#endif /* !TX_USE_ORDERING_SPEC_NODE */
  if (!pred->left) goto err;
  pred->lt = NAME_OP;
  /* rest cleared by calloc() */

  proj = TX_NEW(pmbuf, PROJ);
  if (!proj) goto err;
  /* rest cleared by calloc() */
  proj->p_type = PROJ_AGG;
  proj->n = 1;                                  /* just ordering by $rank */
  proj->preds = (PRED **)TXcalloc(pmbuf, __FUNCTION__, proj->n,
                                  sizeof(QNODE *));
  if (!proj->preds) goto err;
  proj->preds[0] = pred;
  pred = NULL;                                  /* owned by `proj' now */

  goto finally;

err:
  pred = closepred(pred);
  proj = closeproj(proj);
finally:
  return(proj);
}

/********************************************************************/
/*
 *	This will need to look at the predicate and the data
 *	dictionary to determine how to use indexes.
 *
 *	The idea of this code is to examine the query and tables and
 *	determine the best index to use, and what predicates the
 *	main read needs to check.
 *
 *	This needs a lot of work.
 */

int TXshowiplan = 0;

void
TXsettablepred(QNODE *qnode, DBTBL * tb, PRED * p, PROJ * order, FLDOP * fo,
	       int allowbubble, SLIST * flist, SLIST * plist)
{
	static CONST char Fn[] = "settablepred";
	PRED *cp = p;
	PRED *rp = p;
	int		res, allHandled;
	TXCOUNTINFO	*countInfo = &qnode->countInfo;

/* Close any existing index */

	TXindcnt = 0;
	tb->indcnt = 0;
	TX_CLEAR_COUNTINFO(countInfo);
	if (TXshowiplan)
	{
		TXplantablepred(tb, p, order, fo);
		TXshowplan();
	}
	if (tb->index.btree && tb->type != 'B' && tb->type != 'b')
	{
#if 0
		if (tb->index.btree->usr)
			tb->index.btree->usr =
				closefldcmp(tb->index.btree->usr);
#endif
#ifndef NO_BUBBLE_INDEX
		if (tb->index.keepcached == 0)
			closedbidx(&tb->index);
#else
		tb->index.btree = closebtree(tb->index.btree);
#endif
	}
/* First what we try and do is extract the portions of the query which
 * apply to the table.  Currently this is not well implemented */
	if (p)
	{
		rp = TXmakepredvalid(p, tb, 0, 1, 1);
		if (rp != p)
		{
			char *op, *np;

			if (verbose)
			{
				op = TXdisppred(p, 0, 0, 240);
				np = TXdisppred(rp, 0, 0, 240);
				putmsg(MINFO, Fn,
				       "Had to reduce %s to %s for table %s",
				       op, np, tb->lname);
				op = TXfree(op);
				np = TXfree(np);
			}
		}
		tb->pred = rp;
		cp = rp;

		if (verbose)
		{
			char *dp = TXdisppred(rp, 0, 0, 240);

			putmsg(MINFO, Fn, "Setting pred %s on table %s", dp,
			       tb->lname);
			dp = TXfree(dp);
		}
/* Now look at what type of table it is */
		if (tb->type == 'B')
			dobtindx(tb, rp, order, fo);
		else
		{
			TBSPEC *tbspec;
			int freetbspecflist = 0;
			PROJ	*orderByRankProj = NULL;

			tbspec = (TBSPEC *) TXcalloc(TXPMBUFPN, Fn, 1, sizeof(TBSPEC));
			if (tbspec)
			{
                          tbspec->pred = rp;
                          tbspec->proj = order;
			  /* `ORDER BY num' does not give us a
			   * `tbspec->proj' here, because reasons.
			   * But setf3dbi() needs to know about them
			   * too, to turn off likeprows etc.
			   * Make a note.  WTF would like to pass on
			   * schema here, but cannot because reasons:
			   */
			  tbspec->haveOrderByNum =
				  (qnode->parentqn &&	/* LIKEP */
				   /* wtf true parent of LIKEP is PROJECT;
				    * `parentqn' linkage skips it?
				    */
				   qnode->parentqn->parentqn &&
				   qnode->parentqn->parentqn->op==ORDERNUM_OP);
                          /* Bug 6796 comment #0 fix: `SELECT aux
                           * ... LIKEP' was ordering by recid not
                           * rank, because TXsetupauxorder() did a
                           * KEYREC but with no rank ordering.  Force
                           * an ORDER BY $rank here; this may also
                           * help setf3dbi() in other areas:
                           */
                          if (!tbspec->proj &&  /* no ORDER BY expr and */
			      !tbspec->haveOrderByNum &&
                              (p->op == FOP_PROXIM ||   /* LIKEP or */
                               p->op == FOP_RELEV))     /* LIKER */
			  {
				  orderByRankProj = tbspec->proj =
					  TXmakeOrderByRankProj(TXPMBUFPN);
				  if (TXtraceIndexBits & 0x200000)
					  putmsg(MINFO, __FUNCTION__,
	      "Added `ORDER BY $rank%s' to TBSPEC to ensure %s rank ordering",
		   (TXApp && TXApp->legacyVersion7OrderByRank ? "" : " DESC"),
					       TXqnodeOpToStr((QNODE_OP)p->op,
							      NULL, 0));
			  }
				tbspec->pind = NULL;
				tbspec->pflist = plist;
				if (flist)
				{
					tbspec->flist = flist;
				}
				else if (rp)
				{
					char *s, *ts;

					tbspec->flist = slopen();
					freetbspecflist++;
					s = TXpredflds(rp);
					ts = strtok(s, ", ");
					while (ts)
					{
						if (!slfind
						    (tbspec->flist, ts))
							sladd(tbspec->flist,
							      ts);
						ts = strtok(NULL, ", ");
					}
					s = TXfree(s);
				}

			}
			res = donoindx(tb, tbspec, fo, allowbubble);
                        /* wtf unsure of consequences of leaving our
                         * fake ORDER BY attached longer, so close it:
                         */
                        if (orderByRankProj)
                          tbspec->proj = orderByRankProj =
                            closeproj(orderByRankProj);

			/* KNG 20081210 save stats from index (obtained
			 * during donoindx()) to QNODE.  But if the index
			 * is bubble-up, there are no counts since the
			 * index records were not fully read yet:
			 */
			if (tb->index.btree != BTREEPN && /* got index */
			    res != 1)			/* not bubble-up */
			{
				/* KNG 20081212 WTF `tb->indguar'
				 * unexpectedly set for post-proc LIKEP
				 * (because top-level FLDMATH_PROXIM QNODE
				 * will handle post-proc?), and
				 * `pred_allhandled(p)' unexpectedly unset
				 * for bubble == 0 + btthreshold == 100
				 * regular index search, so distrust one
				 * or the other accordingly:
				 */
				allHandled = (TXpred_haslikep(p) ?
					      pred_allhandled(p) :
					      tb->indguar);
				/* If `allHandled', then row count is exact
				 * (no post-proc):
				 */
				countInfo->rowsMatchedMin =
				  (allHandled ? tb->index.nrecs : 0);
				countInfo->rowsMatchedMax = tb->index.nrecs;
				countInfo->indexCount = tb->index.nrecs;
				/* Set rowsMatchedMin/Max, using likeprows
				 * limit count (if available):
				 */
				if (tb->index.rowsReturned != -1)
				{		/* eg. likeprows limited */
					countInfo->rowsReturnedMin =
					 (allHandled?tb->index.rowsReturned:0);
					countInfo->rowsReturnedMax =
						tb->index.rowsReturned;
				}
				else		/* likeprows not set/used? */
				{
					countInfo->rowsReturnedMin =
						countInfo->rowsMatchedMin;
					countInfo->rowsReturnedMax =
						countInfo->rowsMatchedMax;
				}
			}

			if (freetbspecflist)
			{
				slclose(tbspec->flist);
			}
			tbspec = TXfree(tbspec);
		}
	}
	else
	{
		tb->pred = p;
		tb->ipred = p;
	}

/*	Somewhere in here should probably be the order code.  We have the
	list of (potentially) matching recid's.  Now just need to order
	them */

	doorder(tb, order, fo, (qnode->parentqn ? qnode->parentqn->op : 0));
#ifdef DEBUG
	if (tb->index.btree)
		DBGMSG(9,
		       (999, Fn, "This pred has an index.  Indguar = %d",
			tb->indguar));
#endif
	if (tb->indguar)
	{
		if (tb->ddic->optimizations[OPTIMIZE_INDEXONLY] == 0)
			tb->indguar = 0;
	}
	return;
}
