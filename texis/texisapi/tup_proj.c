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
#include "texint.h"
#include "fldcmp.h"

#define NEW_AGG_TYPE

/******************************************************************/
static byte buf[BT_MAXPGSZ];
static char type[40] ;
static char *retoptype ARGS((char *, char *, int, int, int, int, int, FLDOP *, int *, int *));

static char *
retoptype(ltype, rtype, lsz, rsz, lnn, rnn, op, fo, sz, nn)
char	*ltype;
char	*rtype;
int	lsz;
int	rsz;
int	lnn;
int	rnn;
int	op;
FLDOP	*fo;
int	*sz;
int	*nn;
{
	FLD	*f1, *f2, *f;
	int	savmsg;
	size_t		n;

	if (strcmp(ltype, "counteri") != 0)	/* not counteri */
		f1 = createfld(ltype, lsz?lsz:1, lnn);
	else					/* counteri */
	{
		DDFT	*ft;

		ft = getddft(rtype);
		switch (ft->type & FTN_VarBaseTypeMask)
		{
		case FTN_BYTE:
		case FTN_CHAR:
		case FTN_COUNTER:
		case FTN_DATE:
		case FTN_STRLST:
			/* We have fldmath handlers for these types */
			f1 = createfld(ltype, TX_MAX(lsz, 1), lnn);
			break;
		default:
			f1 = createfld("counter", lsz, lnn);
			break;
		}
	}
	if(!f1)
		return NULL;
	if (strcmp(rtype, "counteri") != 0)	/* not counteri */
		f2 = createfld(rtype, rsz?rsz:1, rnn);
	else					/* counteri */
	{
		DDFT	*ft;

		ft = getddft(ltype);
		switch (ft->type & FTN_VarBaseTypeMask)
		{
		case FTN_BYTE:
		case FTN_CHAR:
		case FTN_COUNTER:
		case FTN_DATE:
		case FTN_STRLST:
			/* We have fldmath handlers for these types */
			f2 = createfld(rtype, TX_MAX(rsz, 1), rnn);
			break;
		default:
			f2 = createfld("counter", TX_MAX(lsz, 1), lnn);
			break;
		}
	}
	if(!f2)
	{
		closefld(f1);
		return NULL;
	}
/* wtf JMT */ /* Let's see how well fldmath survives */
	setfldv(f1);
	setfldv(f2);
	{
		void		*vlist[2];
		FLD		*flist[2];
		int		i;

		flist[0] = f1;	flist[1] = f2;
		for (i = 0; i < 2; i++)
		{
			vlist[i] = getfld(flist[i], &n);
			if (!TXftnInitDummyData(TXPMBUFPN, flist[i]->type,
						vlist[i], flist[i]->size, 1))
				goto bail;
			/* KNG 20090122 Bug 2485: no nuls for
			 * varchartostrlstsep = create.  WTF use
			 * putfld() for OO-ness and to set n/size:
			 */
			if (i == 0 &&
			    (flist[0]->type & DDTYPEBITS) == FTN_CHAR &&
			    (flist[1]->type & DDTYPEBITS) == FTN_STRLST)
				memset(vlist[i], '0', n);
			/* else leave TXftnInitDummyData()-set value */
		}
	}
	fopush(fo, f1);
	fopush(fo, f2);
	savmsg = TXsetparsetimemesg(0);	/* Turn off parsetime putmsgs */
	if(foop(fo, op) == -1)
	{
	bail:
		closefld(f1);
		closefld(f2);
		return NULL;
	}
	TXsetparsetimemesg(savmsg);	/* Restore previous setting */
	f = fopop(fo);
	closefld(f1);
	closefld(f2);
	if (f != (FLD *)NULL)
	{
		strcpy(type, ddfttypename(f->type));
		*nn = TXftnIsNotNullable(f->type);
		*sz = f->n;
		closefld(f);
		return type;
	}
	return "long";
}

/******************************************************************/
/*
	Promote a type for doing aggregate functions avg and sum.
*/

static char *
aggpromtype(char *typeStr, char *func)
{
#ifndef NEVER
	if(!strcmp(func, "avg"))
	{
		if(!strcmp(typeStr, "long")) return "long";
		if(!strcmp(typeStr, "int")) return "long";
		if(!strcmp(typeStr, "integer")) return "long";
		if(!strcmp(typeStr, "short")) return "long";
		if(!strcmp(typeStr, "smallint")) return "long";
		if(!strcmp(typeStr, "word")) return "long";
		if(!strcmp(typeStr, "dword")) return "long";
	}
#endif
#ifdef NEVER
	if(!strcmp(func, "sum"))
	{
		if(!strcmp(typeStr, "long")) return "long";
		if(!strcmp(typeStr, "float")) return "double";
		if(!strcmp(typeStr, "int")) return "long";
		if(!strcmp(typeStr, "integer")) return "long";
		if(!strcmp(typeStr, "short")) return "long";
		if(!strcmp(typeStr, "smallint")) return "long";
		if(!strcmp(typeStr, "word")) return "long";
		if(!strcmp(typeStr, "dword"))
			if(sizeof(ft_dword) < sizeof(ft_long))
				return "long";
			else
				return "dword";
	}
#endif
	return typeStr;
}

/* ------------------------------------------------------------------------ */

static FTN
TXpredChildType(DBTBL *dbtbl, FLDOP *fo, QNODE_OP childType, void *child,
		size_t *fldLen, int *isNotNullable)
/* Returns type of PRED `child', setting optional `*fldLen'/`*isNotNullable'.
 * Returns 0 on error.
 */
{
	static const char	fn[] = "TXpredChildType";
	char			*typeStr;
	DDFD			fd;
	FLD			*fld;
	int			fLen = 1, isnn = 0;
	TXPMBUF			*pmbuf;

	pmbuf = (dbtbl->ddic ? dbtbl->ddic->pmbuf : NULL);
	switch (childType)
	{
	case (QNODE_OP)'P':
		typeStr = predtype((PRED *)child, dbtbl, fo, &fLen, &isnn);
		if (!typeStr) goto err;
		if (getddfd(typeStr, fLen, isnn, "", &fd) != 0)
		{
			txpmbuf_putmsg(pmbuf, MERR, fn,
				       "Cannot convert type `%s' to FTN",
				       typeStr);
			goto err;
		}
		if (fldLen) *fldLen = (size_t)fLen;
		if (isNotNullable) *isNotNullable = isnn;
		return(fd.type);
	case FIELD_OP:
		fld = (FLD *)child;
		goto getFld;
	case NAME_OP:
		fld = dbnametofld(dbtbl, child);
		if (!fld)
		{
			txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
				      "Field `%s' non-existent in table `%s'",
				       (char *)child, (dbtbl->lname ?
						dbtbl->lname : dbtbl->rname));
			goto err;
		}
	getFld:
		if (fldLen) getfld(fld, fldLen);
		if (isNotNullable)
			*isNotNullable = TXftnIsNotNullable(fld->type);
		return(fld->type);
	default:
		txpmbuf_putmsg(pmbuf, MERR, fn, "Unknown PRED type %d",
			       (int)childType);
		goto err;
	}

err:
	if (fldLen) *fldLen = 0;
	if (isNotNullable) *isNotNullable = 0;
	return((FTN)0);				/* failure */
}

static int
TXgetLookupFuncReturnType(DBTBL *dbtbl, FLDOP *fo, PRED *pred, FTN *retType,
			  size_t *fldLen, int *isNotNullable)
/* Sets `*retType' to function return type if `pred' is a lookup()
 * function call, setting optional `*fldLen'/`*isNotNullable'.
 * Returns 2 if lookup() call, 1 if not, 0 on error.
 */
{
	PRED	*rightPred, *rightLeft;
	FTN	valArgType, lookupArgType, resultArgType;
	size_t	valArgLen, lookupArgLen, resultArgLen;
	int	ret;

	/*  lookup(1, 2, 'foo') parse tree:
	 *
         *  <pred op="function">
         *    <name>lookup</name>
	 *    <pred op="list">        <-- rightPred
         *      <pred op="list">      <-- rightLeft
         *        <field side="left" type="long">1</field>
         *        <field side="right" type="long">2</field>
         *      </pred>
         *      <field side="right" type="varchar">foo</field>
         *    </pred>
         *  </pred>
	 */

	*retType = (FTN)0;
	if (pred->op != REG_FUN_OP ||
	    TXfldopFuncNameCompare((char *)pred->left, "lookup") != 0 ||
	    pred->rt != 'P' ||
	    !(rightPred = (PRED *)pred->right) ||
	    rightPred->op != LIST_OP)
	{					/* not a lookup() call */
		ret = 1;
		goto finally;
	}

	/* It's a lookup() call.  See if it has 2 or 3 args: */
	if (rightPred->lt == 'P' &&
	    (rightLeft = (PRED *)rightPred->left) != NULL &&
	    rightLeft->op == LIST_OP)
	{					/* 3 args */
		valArgType = TXpredChildType(dbtbl, fo, rightLeft->lt,
					   rightLeft->left, &valArgLen, NULL);
		lookupArgType = TXpredChildType(dbtbl, fo, rightLeft->rt,
				       rightLeft->right, &lookupArgLen, NULL);
		resultArgType = TXpredChildType(dbtbl, fo, rightPred->rt,
				       rightPred->right, &resultArgLen, NULL);
		if (!valArgType || !lookupArgType || !resultArgType) goto err;
	}
	else					/* 2 args */
	{
		valArgType = TXpredChildType(dbtbl, fo, rightPred->lt,
					   rightPred->left, &valArgLen, NULL);
		lookupArgType = TXpredChildType(dbtbl, fo, rightPred->rt,
				       rightPred->right, &lookupArgLen, NULL);
		resultArgType = (FTN)0;
		resultArgLen = 0;
		if (!valArgType || !lookupArgType) goto err;
	}
	*retType = TXsqlFuncLookup_GetReturnType(valArgType, valArgLen,
						 lookupArgType, lookupArgLen,
						 resultArgType, resultArgLen);
	if (fldLen) *fldLen = 1;		/* may vary, if var type */
	if (isNotNullable) *isNotNullable = 0;
	ret = 2;				/* success, and is lookup() */
	goto finally;

err:
	if (fldLen) *fldLen = 0;
	if (isNotNullable) *isNotNullable = 0;
	ret = 0;
finally:
	return(ret);
}

static int
TXgetIfNullFuncReturnType(DBTBL *dbtbl, FLDOP *fo, PRED *pred, FTN *retType,
			  size_t *fldLen, int *isNotNullable)
/* Sets `*retType' to function return type if `pred' is an ifNull()
 * function call, setting optional `*fldLen'/`*isNotNullable'.
 * NOTE: must agree with TXsqlFuncIfNull() SQL return type.
 * Returns 2 if ifNull() call, 1 if not, 0 on error.
 */
{
	PRED	*rightPred;
	int	ret;

	/*  ifNull(1, 2) PRED parse tree:
	 *
	 * <pred op="function">
	 *   <name>ifnull</name>
	 *   <pred op="list">
	 *     <field side="left" type="long">1</field>
	 *     <field side="right" type="long">2</field>
	 *   </pred>
	 * </pred>
	 */

	*retType = (FTN)0;
	if (pred->op != REG_FUN_OP ||
	    TXfldopFuncNameCompare((char *)pred->left, "ifNull") != 0 ||
	    pred->rt != 'P' ||
	    !(rightPred = (PRED *)pred->right) ||
	    rightPred->op != LIST_OP)
	{					/* not an ifNull() call */
		ret = 1;
		goto finally;
	}

	/* It's an ifNull() call.  Type is always the type of first arg: */
	*retType = TXpredChildType(dbtbl, fo, rightPred->lt,
				   rightPred->left, NULL, NULL);
	if (fldLen) *fldLen = 1;		/* may vary, if var type */
	if (isNotNullable) isNotNullable = 0;
	ret = 2;				/* success, and is ifNull() */

finally:
	return(ret);
}


/****************************************************************************/
/* WTF - this does not handle anything except single fields very well.      */

char *
predtype(p, t, fo, sz, nn)
PRED	*p;
DBTBL	*t;
FLDOP	*fo;
int	*sz;
int	*nn;
/* Returns Texis type (e.g. "long") of predicate `p', or NULL on error.
 * Sets `*sz' to FLD.n (length) of field type, and `*nn' nonzero if
 * type is not nullable.
 */
{
#ifdef DEBUG
	static	char Fn[] = "predtype";
#endif
	FLD *f=NULL;
	int rsz, rnn;
	int lsz, lnn;
	char ltype[64];
	char rtype[64];

	*sz = 1;
	*nn = 0;
	type[0] = '\0';
	ltype[0] = '\0';
	rtype[0] = '\0';
	DBGMSG(1, (999, Fn, "Pred = %s", TXdisppred(p, 0, 0, 240)));
	if(!p)
		return NULL;
	if(p->op == RENAME_OP && p->lt == 'P')
	{
		return predtype(p->left, t, fo, sz, nn);
	}
	if(p->op == 0 || p->op == RENAME_OP)
	{
		if (p->lt == FIELD_OP)
			f = p->left;
		if (p->lt == NAME_OP)
			f = dbnametofld(t, p->left);
		if (f != (FLD *)NULL)
		{
			strcpy(type, TXfldtypestr(f));
			*nn = TXftnIsNotNullable(f->type);
			*sz = f->n;
			if (!strcmp(type, "counteri"))
				type[strlen(type)-1] = '\0';
			return type;
		}
		else
		{
#ifndef NO_RANK_FIELD
			if (TXisRankName(p->left))
				return((char *)TXrankColumnTypeStr);
#endif
			putmsg(MWARN+UGE,NULL,"Field %s non-existent",p->left);
			return (char *)NULL;
		}
	}
	if(p->op == REG_FUN_OP)
	{
		int funtype ;
		funtype = fofuncret(fo, p->left);
		if(funtype)
		{
			FLD	*typeFld;
			char	*typeVal;
			size_t	typeN;
			PRED	*rightPred = (PRED *)p->right;
			FTN	funcRetType;
			size_t	fldLen;
			int	isNotNullable;

			/* Three-arg convert() function has a dynamic
			 * return type, named in its second arg:
			 */
			if (strcmp((char *)p->left, "convert") == 0 &&
			    p->rt == 'P' &&
			    rightPred &&
			    rightPred->op == LIST_OP &&
			    rightPred->lt == 'P' &&
			    rightPred->left &&
			    ((PRED *)rightPred->left)->op == LIST_OP &&
			    ((PRED *)rightPred->left)->rt == FIELD_OP &&
			    (typeFld = (FLD *)((PRED *)rightPred->left)->right) != FLDPN &&
			    (typeFld->type & DDTYPEBITS) == FTN_CHAR &&
			    (typeVal = getfld(typeFld, &typeN)) != CHARPN)
			{
				TXstrncpy(type, typeVal, sizeof(type));
				return(type);
			}
			/* lookup() has a dynamic return type: */
			switch (TXgetLookupFuncReturnType(t, fo, p,
				       &funcRetType, &fldLen, &isNotNullable))
			{
			case 2:			/* is lookup() */
				TXstrncpy(type, ddfttypename(funcRetType),
					  sizeof(type));
				*sz = (int)fldLen;
				*nn = isNotNullable;
				return(type);
				break;
			case 1:			/* is not lookup() */
				break;
			case 0:			/* error */
			default:
				return(NULL);
			}
			/* ifNull() has a dynamic return type: */
			switch (TXgetIfNullFuncReturnType(t, fo, p,
				       &funcRetType, &fldLen, &isNotNullable))
			{
			case 2:			/* is ifNull() */
				TXstrncpy(type, ddfttypename(funcRetType),
					  sizeof(type));
				*sz = (int)fldLen;
				*nn = isNotNullable;
				return(type);
			case 1:			/* is not ifNull() */
				break;
			case 0:			/* error */
			default:
				return(NULL);
			}
			/* some other function: */
			strcpy(type, ddfttypename(funtype));
			return type;
		}
		else
		{
			putmsg(MWARN+UGE,NULL,"Function %s non-existent",p->left);
			return NULL;
		}
	}
	if(p->op == AGG_FUN_OP)
	{
		if (!strcmp(p->left, "count"))
		{
			return "long";
		}
		else
		{
			if (p->rt == 'P')
			{
				char	*pType;

#ifdef NEW_AGG_TYPE
				pType = predtype(p->right, t, fo, sz, nn);
				if (!pType) return(NULL);
				return(aggpromtype(pType, p->left));
#else
				return(pType);
#endif
			}
			if (p->rt == FIELD_OP)
				f = p->right;
			if (p->rt == NAME_OP)
				f = dbnametofld(t, p->right);
			if (f != (FLD *)NULL)
			{
				strcpy(type, TXfldtypestr(f));
				*nn = TXftnIsNotNullable(f->type);
				*sz = f->n;
#ifdef NEW_AGG_TYPE
				return aggpromtype(type, p->left);
#else
				return type;
#endif
			}
			else
			{
				putmsg(MWARN+UGE,NULL,"Field %s non-existent",p->left);
				return (char *)NULL;
			}
		}
	}
	/* WTF - Need to calculate some real value here */
	if (p->lt == 'P')
	{
		char *ts = predtype(p->left, t, fo, &lsz, &lnn);
		if(ts)
			strcpy(ltype, ts);
		else
			return(NULL);
	}
	else if(p->left)
	{
		if (p->lt == FIELD_OP)
			f = p->left;
		if (p->lt == NAME_OP)
			f = dbnametofld(t, p->left);
		if (f != (FLD *)NULL)
		{
			strcpy(ltype, TXfldtypestr(f));
			lnn = TXftnIsNotNullable(f->type);
			lsz = f->n;
		}
		else
		{
#ifndef NO_RANK_FIELD
			if (TXisRankName(p->left))
			{
				strcpy(ltype, TXrankColumnTypeStr);
				lsz = 1;
				lnn = 1;
			}
			else
#endif
			{
				putmsg(MWARN+UGE,NULL,"Field %s non-existent",p->left);
				return(NULL);
			}
		}
	}
	else
	{
		putmsg(MWARN+UGE,NULL,"Unexpected predicate");
		strcpy(ltype, "");
	}
	if (p->rt == 'P')
	{
		char *ts = predtype(p->right, t, fo, &rsz, &rnn);
		if(ts)
			strcpy(rtype, ts);
		else
			return(NULL);
	}
	else if(p->right)
	{
		if (p->rt == FIELD_OP)
			f = p->right;
		if (p->rt == NAME_OP)
			f = dbnametofld(t, p->right);
		if (f != (FLD *)NULL)
		{
			strcpy(rtype, TXfldtypestr(f));
			rnn = TXftnIsNotNullable(f->type);
			rsz = f->n;
		}
		else
		{
#ifndef NO_RANK_FIELD
			if (TXisRankName(p->right))
			{
				strcpy(rtype, TXrankColumnTypeStr);
				rsz = 1;
				rnn = 1;
			}
			else
#endif
			{
				putmsg(MWARN+UGE,NULL,"Field %s non-existent",p->right);
				return(NULL);
			}
		}
	}
	else
	{
		putmsg(MWARN+UGE,NULL,"Unexpected predicate");
		return(NULL);
	}
	return retoptype(ltype, rtype, lsz, rsz, lnn, rnn, p->op, fo, sz, nn);
}

/******************************************************************/

static int openstats ARGS((DBTBL *, PROJ *, FLDOP *));

static int
openstats(tin, proj, fo)
DBTBL	*tin;	/* Original table */
PROJ	*proj;	/* The projection */
FLDOP	*fo;	/* A field op for any calculations */
{
	return TXopennewstats(tin, proj, fo, &tin->nfldstat);
}

TXbool
TXpredicateIsResolvableWithAltTable(PRED *pred, DBTBL *orgDbtbl,
				    DBTBL *altDbtbl, TXbool checkPred)
/* Returns true if predicate `pred' -- based on `orgDbtbl' (e.g. source
 * table) -- is also resolvable with just the columns in `altDbtbl'
 * (e.g. Metamorph compound index fields); false if not.  If `checkPred'
 * is true, actually checks predicate tree, instead of just its
 * output column name.
 */
{
	TXbool	ret;

	if (checkPred)
	{
		/* Bug 4768: check the source fields of the predicate,
		 * not the output column name.  E.g. if predicate is
		 * a RENAME_OP of a column in `altDbtbl', do not reject
		 * it just because the output name is not in the table:
		 */
		/* KNG 20130523 not sure if we should use/modify
		 * `{l,r}[n]vt' fields with our Bug 4768 usage of
		 * TXispredvalid(), so do not (flag 0x4):
		 */
		ret = TXispredvalid(TXPMBUF_SUPPRESS, pred, altDbtbl, 0x7,
				    orgDbtbl, INTPN);
	}
	else					/* just check output name */
	{
		char	*predOutColName;
		DD	*orgDd = orgDbtbl->tbl->dd, *altDd = altDbtbl->tbl->dd;
		int	orgDdIdx, altDdIdx;

#ifndef NO_TRY_FAST_CONV
		predOutColName = TXdisppred(pred, 1, 1, 0);
#else
		predOutColName = TXdisppred(pred, 1, 0, 0);
#endif
		/* KNG 20120305 types must also agree, e.g. if we are
		 * projecting table strlst field `predOutColName',
		 * cannot source it from index varchar field `predOutColName'
		 * (i.e. from indexvalues=splitstrlst index):
		 */
		ret = ((orgDdIdx = ddfindname(orgDd, predOutColName)) >= 0 &&
		       (altDdIdx = ddfindname(altDd, predOutColName)) >= 0 &&
		       (orgDd->fd[orgDdIdx].type & FTN_VarBaseTypeMask) ==
		       (altDd->fd[altDdIdx].type & FTN_VarBaseTypeMask));
#ifdef NO_TRY_FAST_CONV
		predOutColName = TXfree(predOutColName);
#endif
	}
	return(ret);
}

/******************************************************************/
/*
 *	Setup a table to hold the result of a projection.
 *	Returns a table matching the projection.
 */

DBTBL *
TXtup_project_setup(tin, proj, fo, flags)
DBTBL	*tin;	/* Original table */
PROJ	*proj;	/* The projection */
FLDOP	*fo;	/* A field op for any calculations */
int	flags;	/* 0x1: allow fast copy  0x2: no-op DBF for returned table */
{
	static CONST char	fn[] = "TXtup_project_setup";
	DD *dd;
	DBTBL *dbt;
	int  i;
	char *fname = NULL;
	char *nname;

	dd = opennewdd(proj->n + 1);
        if(!dd)
         return (DBTBL *)NULL;
	dbt = (DBTBL *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(DBTBL));
	(void)ddsettype(dd, 1);
	for (i = 0; i < proj->n; i++)
	{
		int sz, nn;

#ifdef NEVER
		ltype[0] = '\0';
		rtype[0] = '\0';
#endif
#ifndef NO_TRY_FAST_CONV
		fname = TXdisppred(proj->preds[i], 1, 1, DDNAMESZ);
#else
		fname = TXdisppred(proj->preds[i], 1, 0, DDNAMESZ);
#endif
		if(!fname)
			return NULL;
		nname = predtype(proj->preds[i], tin, fo, &sz, &nn);
		DBGMSG(1, (999, NULL, "ProjS %s", fname));
		if (nname == (char *)NULL)
		{
			putmsg(MWARN+UGE,NULL,"Field non-existent or type error in %s",fname);
#ifdef NO_TRY_FAST_CONV
			fname = TXfree(fname);
#endif
			dd = closedd(dd);
			dbt = TXfree(dbt);
			return NULL;
		}
		else
		{
			if((strlen(fname)>=DDNAMESZ) ||
			   (!putdd(dd, fname, nname, sz, nn)))
			{ /* Assume it's duplicate name or too long */
#ifdef NO_TRY_FAST_CONV
				if(fname)
					fname = TXfree(fname);
#endif
				fname = TXmalloc(TXPMBUFPN, fn, DDNAMESZ + 1);
				if(fname)
					sprintf(fname, "#TEMP%d", i);
				if(!putdd(dd, fname, nname, sz, nn))
				{
					fname = TXfree(fname);
					fname = TXdisppred(proj->preds[i], 1, 1, 0);
					putmsg(MWARN, NULL, "Could not create field %s");
				}
				else
				{
					proj->preds[i]->edisplay=TXstrdup(TXPMBUFPN, fn, fname);
#ifndef NEVER
					fname = TXfree(fname);
					fname = TXdisppred(proj->preds[i], 0, 1, 0);
					proj->preds[i]->idisplay=TXstrdup(TXPMBUFPN, fn, fname);
#else
					proj->preds[i]->idisplay=TXstrdup(TXPMBUFPN, fn, fname);
#endif
				}
			}
		}
#ifdef NO_TRY_FAST_CONV
		fname = TXfree(fname);
#endif
	}
	dbt->lname = TXstrdup(TXPMBUFPN, fn, tin->lname);
	dbt->tbl = createtbl(dd, ((flags & 0x2) ? TXNOOPDBF_PATH : NULL));
	dbt->type = 'T';
	dbt->ddic = tin->ddic;
	dbt->frecid = createfld("recid", 1, 0);
	dbt->tblid = -1;
	putfld(dbt->frecid, &dbt->recid, 1);
	dd = closedd(dd);
	if((proj->p_type == PROJ_SINGLE) || (proj->p_type == PROJ_AGG_DONE))
	{
		tin->needstats = 1;
		TXrewinddbtbl(tin);
		/* Bug 4770: if `tin' is the output of a GROUP BY it
		 * might not have a real DBF attached (e.g. DBF_NOOP):
		 */
		if (tin->tbl->df->dbftype != DBF_NOOP)
			getdbtblrow(tin);
		openstats(tin, proj, fo);
		if(TXisprojcountonly(tin, proj, fo) &&
		   !TXpred_haslikep(tin->pred))
			tin->needstats = 2;
		TXrewinddbtbl(tin);
	}
	else if ((flags & 0x1) && tin->indguar && tin->index.btree &&
			tin->index.btree->datad)
	{
		int indexonly = 1, madeIndexdbtbl = 0;

		/* We would normally create `.indexdbtbl' only *after*
		 * we verified `indexonly', but now (Bug 4768) we also
		 * need it *during* `indexonly' verification:
		 */
		if (!tin->index.indexdbtbl)
		{
			tin->index.indexdbtbl = TXopentmpdbtbl(NULL, NULL,
				   NULL, tin->index.btree->datad, tin->ddic);
			madeIndexdbtbl = 1;
		}
		/* Validate the projection's predicates: */
		for (i = 0; i < proj->n; i++)
		{
		       if (!TXpredicateIsResolvableWithAltTable(proj->preds[i],
						   tin, tin->index.indexdbtbl,
	 tin->ddic->optimizations[OPTIMIZE_INDEX_DATA_ONLY_CHECK_PREDICATES]))
			{
				indexonly = 0;
				break;
			}
		}
		if(indexonly)
		{
			tin->index.indexdataonly=1;
		}
		else if (madeIndexdbtbl)
		{
			/* Close `.indexdbtbl' created above; not usable: */
			tin->index.indexdbtbl =
				closedbtbl(tin->index.indexdbtbl);
		}
	}
	return dbt;
}

/******************************************************************/

int
TXfldnamecmp(t, n1, n2)
DBTBL	*t;
char	*n1;
char	*n2;
{
	int	rc;
	int	fq1=0, fq2=0;
	char	*fn1, *fn2;

	(void)t;
	fn1 = strchr(n1, '.');
	fn2 = strchr(n2, '.');
	if(fn1 == NULL)
	{
		fq1 = 1;
		fn1 = n1;
	}
	if(fn2 == NULL)
	{
		fq1 = 1;
		fn2 = n2;
	}

	if(!fq1 && !fq2)
	{
		rc = strcmp(n1, n2);
		return rc;
	}

	rc = strcmp(fn1, fn2);
	if(rc)
		return rc;
	if(!strcmp(n1, n2))
		return 0;
	if(fq1 && fq2)
		return 1;
	return rc;
}

/******************************************************************/

typedef	struct tagAGGIDX {
	char	*iname;
	char	*ifname;
	char	*fname;
	char	*sysindexParams;
	int	op[DDFIELDS];
	int	fnum[DDFIELDS];
	int	dir;
	int	fullscan;
	BTREE	*bt;
} AGGIDX;

/******************************************************************/

static int findindex ARGS((DBTBL *, PRED *, AGGIDX *, int));

static int
findindex(tbl, pred, aggidx, fnum)
DBTBL	*tbl;
PRED	*pred;
AGGIDX	*aggidx;
int	fnum;
{
	static CONST char	fn[] = "findindex";
	char	*nname, *f, *s;
	char	*itype, *iunique, **iname, **ifile, **ifields;
	char	**sysindexParamsVals = CHARPPN;
	int	x = 0, y = 0;
	int	i, numindex;
	int	btidx;
#ifdef AUTO_MK_INDEX
	int	beenhere = 0;
#endif

	if(!tbl->rname)
		return 0;
	if(pred->op != AGG_FUN_OP)
	{
		if(pred->op == RENAME_OP)
			return findindex(tbl, pred->left, aggidx, fnum);
		return 0;
	}
	if(TXpredrtdist(pred)) /* Can't do DISTINCT WTF */
		return 0;

	nname = TXdisppred(pred, 0, 0, 0);
	f = strchr(nname, '(');
	if(f != (char *)NULL)
	{
		f[0] = '\0';
		f++;
	}
	else
		return 0;
	s = nname;
	*strchr(f, ')') = '\0';
	btidx = 0;

	/* Look for forward indexes */

#ifdef AUTO_MK_INDEX
findind:
#endif
	if(tbl->ddic->optimizations[OPTIMIZE_COUNT_STAR] && !strcmp(f, "*"))
	{
		numindex = TXddgetindexinfo(tbl->ddic, tbl->rname, NULL,
			&itype, &iunique, &iname, &ifile, &ifields,
			&sysindexParamsVals, NULL);
	}
	else
	{
		numindex = TXddgetindexinfo(tbl->ddic, tbl->rname, f,
			&itype, &iunique, &iname, &ifile, &ifields,
			&sysindexParamsVals, NULL);
	}
	if(numindex > 0)
	{
		for(x=0; aggidx[x].fname; x++)
		{
			if(!strcmp(aggidx[x].fname, f))
			{
				if(aggidx[x].fname)
					btidx++;
				for(y=0; aggidx[x].op[y]; y++);
				break;
			}
		}
		if(!strcmp(s, "min"))
			aggidx[x].op[y] = AGG_MIN;
		else if(!strcmp(s, "max"))
			aggidx[x].op[y] = AGG_MAX;
		else if(!strcmp(s, "avg"))
			aggidx[x].op[y] = AGG_AVG;
		else if(!strcmp(s, "sum"))
			aggidx[x].op[y] = AGG_SUM;
		else if(!strcmp(s, "count"))
			aggidx[x].op[y] = AGG_COUNT;
		aggidx[x].fnum[y] = fnum;
		if(y==0)
		{
			aggidx[x].fname = TXstrdup(TXPMBUFPN, fn, f);
			for(i=0;i<numindex;i++)
			{
				if(itype[i] == INDEX_BTREE)
				{
					aggidx[x].iname = TXstrdup(TXPMBUFPN, fn, iname[i]);
					aggidx[x].ifname = TXstrdup(TXPMBUFPN, fn, ifile[i]);
					aggidx[x].sysindexParams =
						TXstrdup(TXPMBUFPN, fn, sysindexParamsVals[i]);
					aggidx[x].dir = ifields[i][strlen(ifields[i])-1]
							== '-';
					btidx ++;
					break;
				}
			}
			if(!aggidx[x].iname)
			{
				aggidx[x].fname = TXfree(aggidx[x].fname);
			}
		}
	}
#ifdef AUTO_MK_INDEX
	else
	{
		char newiname[40];
		int j;

		putmsg(999, NULL, "Wish I had an index for %s - %s",
				tbl->rname, f);
		if(!beenhere)
		{
			strcpy(newiname, "AUTOIX");
			for(j=0; j < 20; j++)
			{
				newiname[j+6] = rand() % 26 + 'a';
			}
			newiname[j+6] = '\0';
			createindex(tbl->ddic, newiname, newiname, tbl->rname,
					f, 0, INDEX_BTREE);
			beenhere++;
			goto findind;

		}
	}
#endif
	itype = TXfree(itype);
	iunique = TXfree(iunique);
	iname = TXfreeStrList(iname, numindex);
	ifile = TXfreeStrList(ifile, numindex);
	ifields = TXfreeStrList(ifields, numindex);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numindex);
	iname = TXfree(iname);
	ifile = TXfree(ifile);
	ifields = TXfree(ifields);

	/* Look for reverse indexes */

	strcat(f, "-");
	numindex = TXddgetindexinfo(tbl->ddic, tbl->rname, f,
		&itype, &iunique, &iname, &ifile, &ifields,
		&sysindexParamsVals, NULL);
	if(numindex > 0)
	{
		for(x=0; aggidx[x].fname; x++)
		{
			if(!strcmp(aggidx[x].fname, f))
			{
				if(aggidx[x].fname)
					btidx++;
				for(y=0; aggidx[x].op[y]; y++);
				break;
			}
		}
		if(!strcmp(s, "min"))
			aggidx[x].op[y] = AGG_MIN;
		else if(!strcmp(s, "max"))
			aggidx[x].op[y] = AGG_MAX;
		else if(!strcmp(s, "avg"))
			aggidx[x].op[y] = AGG_AVG;
		else if(!strcmp(s, "sum"))
			aggidx[x].op[y] = AGG_SUM;
		else if(!strcmp(s, "count"))
			aggidx[x].op[y] = AGG_COUNT;
		aggidx[x].fnum[y] = fnum;
		if(y==0)
		{
			aggidx[x].fname = TXstrdup(TXPMBUFPN, fn, f);
			for(i=0;i<numindex;i++)
			{
				if(itype[i] == INDEX_BTREE)
				{
					aggidx[x].iname = TXstrdup(TXPMBUFPN, fn, iname[i]);
					aggidx[x].ifname = TXstrdup(TXPMBUFPN, fn, ifile[i]);
					aggidx[x].sysindexParams =
						TXstrdup(TXPMBUFPN, fn, sysindexParamsVals[i]);
					aggidx[x].dir = ifields[i][strlen(ifields[i])-1]
							== '-';
					btidx ++;
					break;
				}
			}
		}
	}
	itype = TXfree(itype);
	iunique = TXfree(iunique);
	iname = TXfreeStrList(iname, numindex);
	ifile = TXfreeStrList(ifile, numindex);
	ifields = TXfreeStrList(ifields, numindex);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numindex);
	iname = TXfree(iname);
	ifile = TXfree(ifile);
	ifields = TXfree(ifields);
	nname = TXfree(nname);
	return btidx;
}

/******************************************************************/

static AGGIDX *closeaggidx ARGS((AGGIDX *));

static AGGIDX *
closeaggidx(aggidx)
AGGIDX	*aggidx;
{
	if(aggidx->iname)
		aggidx->iname = TXfree(aggidx->iname);
	if(aggidx->ifname)
		aggidx->ifname = TXfree(aggidx->ifname);
	if(aggidx->fname)
		aggidx->fname = TXfree(aggidx->fname);
	if (aggidx->sysindexParams != CHARPN)
		aggidx->sysindexParams = TXfree(aggidx->sysindexParams);
	if(aggidx->bt)
		aggidx->bt = closebtree(aggidx->bt);
	return NULL;
}

/******************************************************************/
/*
	This is called in the case there is an index assigned to
	the table, which guarantees the desired result.  If so,
	see if it can evaluate the stats.
*/

static int checkindexforstats ARGS((DBTBL *, DBTBL *, PROJ *, FLDOP *));

static int 
checkindexforstats(tin, tout, proj, fo)
DBTBL *tin;
DBTBL *tout;
PROJ *proj;
FLDOP *fo;
{
	static CONST char	fn[] = "checkindexforstats";
	int i, j;
	size_t sz;
	int rc = 1;
	DBTBL *dbtbl;
	BTLOC btloc;
	byte tempbuf[8192];

	if(!tin->index.btree || !tin->index.btree->usr)
	{
		proj->p_type = PROJ_AGG_DONE;
		return 0;
	}
	dbtbl = TXcalloc(TXPMBUFPN, fn, 1, sizeof(DBTBL));
	if(dbtbl)
	{
		dbtbl->tbl = ((FLDCMP *)tin->index.btree->usr)->tbl1;
		dbtbl->lname = tin->lname;
		dbtbl->rname = tin->rname;
		for(i=0; rc == 1 && i < proj->n; i++)
		{
			/* Can we resolve from index? */
			rc = TXispredvalid(TXPMBUF_SUPPRESS, proj->preds[i],
					   dbtbl, 0x1, DBTBLPN, INTPN);
		}
		if (rc==1)
		{
/*
First cut, just read through index, calculating stats
*/
			TXopennewstats(dbtbl, proj, fo, &dbtbl->nfldstat);
/* Already read a row?, so copy stats already calculated */
			TXcopystats(dbtbl->nfldstat, tin->nfldstat);
			sz = sizeof(tempbuf);
			btloc = dbidxgetnext(&tin->index, &sz, tempbuf, NULL);
			while(TXrecidvalid(&btloc))
			{
#ifdef TX_DEBUG
				if (-1 == buftofld(tempbuf, dbtbl->tbl, sz))
					debugbreak();
#else
				buftofld(tempbuf, dbtbl->tbl, sz);
#endif
				TXaddstatrow(dbtbl->nfldstat, dbtbl, fo);
				sz = sizeof(tempbuf);
				btloc = dbidxgetnext(&tin->index, &sz, tempbuf, NULL);
			}
			for(j=0;j<proj->n;j++)
			{
				FLD *fld;
				FLD	*tfld;
				void	*v, *p;
				size_t	sz1;

				fld = TXgetstatfld(dbtbl, proj->preds[j]);
				v = fld?getfld(fld, &sz):NULL;
				if(v)
				{
					sz1 = sz * fld->elsz;
					p = TXmalloc(TXPMBUFPN, fn, sz1);
					memcpy(p, v, sz1);
					tfld = getfldn(tout->tbl, j, NULL);
					freeflddata(tfld);
					setfld(tfld, p, sz1);
					putfld(tfld, p, sz);
				}
				else
				{
					proj->p_type = PROJ_AGG_DONE;
					return rc;
				}
			}
			proj->p_type = PROJ_AGG_CALCED;
			return 0;
		}
		dbtbl = TXfree(dbtbl);
	/* Don't know yet */
	}
	proj->p_type = PROJ_AGG_DONE;
	return rc;
}

/******************************************************************/

static int faststats ARGS((DBTBL *, DBTBL *, PROJ *, FLDOP *));

static int 
faststats(tin, tout, proj, fo)
DBTBL *tin;
DBTBL *tout;
PROJ *proj;
FLDOP *fo;
{
/*******************************************************************/
/* The idea behind this portion is to see if we can use indices to */
/* calculate all the aggregate functions.  If there is and index   */
/* on all the fields involved then we can use them without using   */
/* the actual table.  Min() and/or Max() can be a straight lookup  */
/* depending on the direction of the indices, the others would     */
/* require walking the index, unless I added a getlast to BTREE.   */
/* Count, sum and avg require walking through the index, which     */
/* should be cheaper than reading from the table.                  */
/*******************************************************************/
	static CONST char	fn[] = "faststats";
	AGGIDX	*aggidx = (AGGIDX *)NULL;
	int	j, op, noindex = 0, ret;
	int	haveIndexLock = 0;
	FLD	**fld;
	TBL	*tbl = NULL;
	size_t	i, aggsiz = 0;
        DBTBL   *savtbl;

        savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = tin;			/* for btreelog debug */

	/* Prevent leaping ahead if there is a predicate on the table */
/* WTF:

	If we can resolve some aggs here, do so, and then leave
	the rest to be resolved later.  Should save some fldmath
	time.

	Right now if we are looking for a subset of the table then
	we ignore faststats, and look at all the records which match
	the predicate.  We shoud be able to do better than that.  If
	we can guarantee the predicate by index, and the index also
	contains the data we want, we can just use the index.  Examples:

	create index Index1 on Table1(Field1, Field2);

	1: select min(Field1) from Table1 where Field1 > 5;

		Search Index1 for Field1 > 5.  First value
		is a match.

	2: select max(Field1) from Table1 where Field1 < 5;

		Need to do a getprev from btree after
		searching for 5 in Index.

	3: select max(Field1) from Table1 where Field1 > 5;

		Get the last entry in Index, if it's > 5
		that's it, otherwise no answer.

	4: select min(Field2) from Table1 where Field1 = 5;

		Search for Field1=5, look at first entry
		in index for Field2.

	5: select min(Field2) from Table1 where Field1 > 5;

		????

	6: select max(Field2) from Table1 where Field1 = 5;

		???

	Need to know our state when we get here.  Do we already
	have an index open on the where clause, and what kind
	of index is it?

	Currently when we get here we have a random index assigned
	to the table, that may or may not have anything to do with
	the aggregate we want.  Need smarter choice of index for
	case 4.  

	Assumption: Not descending index.
	If agg = min
		if field is first field, get first from index.
		else
			if field is second field, and field1 op is = or <
				get first from index.
*/

	if(tin->pred)				      /* JMT 98-07-02 */
	{
		if(tin->indguar && tin->ddic->optimizations[OPTIMIZE_FASTSTATS])
		{
			ret = checkindexforstats(tin, tout, proj, fo);
			goto done;
		}
		proj->p_type = PROJ_AGG_DONE;
		goto ok;
	}
	aggsiz = proj->n * 2;
	aggidx = (AGGIDX *)TXcalloc(TXPMBUFPN, fn, aggsiz+1, sizeof(AGGIDX));
	if (aggidx == (AGGIDX *)NULL)
		goto err;

	/* KNG 200806 Bug 2279 get index lock.  We delayed as long as
	 * possible for speed (i.e. in case checkindexforstats() handled it?).
	 */
	if (TXlockindex(tin, INDEX_READ, NULL) == -1)
	{
		if (!TXsqlWasCancelled(tin->ddic))
			putmsg(MWARN + FWE, fn,
			       "Could not lock indexes for table %s",
			       tin->lname);
		/* stumble on: we are read-only so only we get corrupted */
	}
	else
		haveIndexLock = 1;

	for(j=0; j < proj->n; j++)
	{
		if (!findindex(tin, proj->preds[j], aggidx, j))
		{
#ifdef DEBUG
			DBGMSG(9,(999, NULL, "No index for %s", TXdisppred(proj->preds[j], 0, 0, 240)));
#endif
			noindex++;
		}
	}
	if(noindex)
	{
		proj->p_type = PROJ_AGG_DONE;
		goto ok;
	}
	else
	{
		fld = (FLD **)TXcalloc(TXPMBUFPN, fn, proj->n, sizeof(FLD *));
		if (fld == (FLD **)NULL)
			goto err;
		/* Analysis, Mr Spock */
		for (i = 0; i < aggsiz && aggidx[i].iname; i++)
		{
			for(j=0;(op=aggidx[i].op[j]);j++)
				if(op == AGG_AVG || op == AGG_SUM || op == AGG_COUNT)	
					aggidx[i].fullscan = 1;
#ifdef DEBUG
			if(aggidx[i].fullscan)
				DBGMSG(9, (999, NULL, "Index %s needs readthru",
					aggidx[i].iname));
			else
				DBGMSG (9, (999, NULL, "Index %s needs lookup",
					aggidx[i].iname));
#endif
			aggidx[i].bt = openbtree(aggidx[i].ifname, 8192, 20, 0, O_RDONLY);
			if (aggidx[i].bt != BTREEPN &&
			    bttexttoparam(aggidx[i].bt, aggidx[i].sysindexParams) < 0)
				aggidx[i].bt = closebtree(aggidx[i].bt);
		}
	}
	for(j=0;j<proj->n;j++)
	{
		fld[j] = newfld(getfldn(tout->tbl, j, TXOFPN));
	}
	for (i = 0; i < aggsiz && aggidx[i].iname; i++)
	{
		int	firsttime;
		int	fnum;
		RECID	recid;
		size_t	sz;
		unsigned long count;
		FLD	*tfld;

		for(j=0;(op=aggidx[i].op[j]);j++)
		{
			firsttime = 1;
			count = 0;
			fnum = aggidx[i].fnum[j];
			if (!aggidx[i].bt)	/* sanity check */
			{
				putmsg(MERR, fn, "Missing B-tree");
				goto err;
			}
			tbl = createtbl(btreegetdd(aggidx[i].bt), NULL);
			rewindbtree(aggidx[i].bt);
			while(sz = sizeof(buf),
			      recid = btgetnext(aggidx[i].bt, &sz, buf, NULL),
			      TXrecidvalid(&recid))
			{
				switch(op)
				{
					case AGG_MIN:
						if(firsttime && aggidx[i].dir == 0)
						{
							if(-1 == buftofld(buf, tbl, sz))
							{
#ifdef TX_DEBUG
								debugbreak() ;
#endif
								rewindbtree(aggidx[i].bt);
								recid = btgetnext(aggidx[i].bt, &sz, buf, NULL);

							}
							fopush(fo, fld[fnum]);
							fopush(fo, tbl->field[0]);
							foop(fo, FOP_ASN);
							if(fld[fnum])
								closefld(fld[fnum]);
							fld[fnum] = fopop(fo);
						}
						break;
					case AGG_MAX:
						if(firsttime && aggidx[i].dir != 0)
						{
#ifdef TX_DEBUG
							if (-1 == buftofld(buf, tbl, sz)) debugbreak();
#else
							buftofld(buf, tbl, sz);
#endif
							fopush(fo, fld[fnum]);
							fopush(fo, tbl->field[0]);
							foop(fo, FOP_ASN);
							if(fld[fnum])
								closefld(fld[fnum]);
							fld[fnum] = fopop(fo);
						}
						break;
					case AGG_SUM:
					case AGG_AVG:
#ifdef TX_DEBUG
						if (-1 == buftofld(buf, tbl, sz)) debugbreak();
#else
						buftofld(buf, tbl, sz);
#endif
						fopush(fo, fld[fnum]);
						fopush(fo, tbl->field[0]);
						if(firsttime)
							foop(fo, FOP_ASN);
						else
							foop(fo, FOP_ADD);
						if(fld[fnum])
							closefld(fld[fnum]);
						fld[fnum] = fopop(fo);
					case AGG_COUNT:
						count++;
						break;
					default:
						putmsg(999, NULL, "Unknown function %d", (int)op);
						break;
				}
				firsttime = 0;
				if (!aggidx[i].fullscan)
					break;
			}
			if (!aggidx[i].fullscan)
			{
				sz = sizeof(buf);
#ifdef DEBUG
				DBGMSG(9, (999, NULL, "Getting Last"));
#endif
				recid = btgetlast(aggidx[i].bt, &sz, buf);
			}
			switch(op)
			{
				case AGG_MIN:
					if(aggidx[i].dir != 0)
					{
#ifdef TX_DEBUG
						if (-1 == buftofld(buf, tbl, sz)) debugbreak();
#else
						buftofld(buf, tbl, sz);
#endif
						fopush(fo, fld[fnum]);
						fopush(fo, tbl->field[0]);
						foop(fo, FOP_ASN);
						if(fld[fnum])
							closefld(fld[fnum]);
						fld[fnum] = fopop(fo);
					}
#ifdef DEBUG
					DBGMSG(9, (999, NULL, "The min is %s",
						fldtostr(fld[fnum])));
#endif
					break;
				case AGG_MAX:
					if(aggidx[i].dir == 0)
					{
#ifdef TX_DEBUG
						if (-1 == buftofld(buf, tbl, sz)) debugbreak();
#else
						buftofld(buf, tbl, sz);
#endif
						fopush(fo, fld[fnum]);
						fopush(fo, tbl->field[0]);
						foop(fo, FOP_ASN);
						if(fld[fnum])
							closefld(fld[fnum]);
						fld[fnum] = fopop(fo);
					}
#ifdef DEBUG
					DBGMSG(9, (999, NULL, "The max is %s",
						fldtostr(fld[fnum])));
#endif
					break;
				case AGG_SUM:
#ifdef DEBUG
					DBGMSG(9, (999, NULL, "The sum is %s",
						fldtostr(fld[fnum])));
#endif
					break;
				case AGG_AVG:
				case AGG_COUNT:
					tfld = createfld("long", 1, 1);
					putfld(tfld, &count, 1);
					fopush(fo, fld[fnum]);
					fopush(fo, tfld);
					if(op == AGG_COUNT)
						foop(fo, FOP_ASN);
					else
						foop(fo, FOP_DIV);
					if(fld[fnum])
						closefld(fld[fnum]);
					closefld(tfld);
					fld[fnum] = fopop(fo);
#ifdef DEBUG
					if(op == AGG_AVG)
					DBGMSG(9, (999, NULL, "The avg is %s",
						fldtostr(fld[fnum])));
					else
					DBGMSG(9, (999, NULL, "The count is %s",
						fldtostr(fld[fnum])));
#endif
					break;
			}
			if(tbl)
				closetbl(tbl);
		}
	}
	for(j=0;j<proj->n;j++)
	{
		FLD	*tfld;
		void	*v, *p;
		size_t	sz, sz1;

		v = getfld(fld[j], &sz);
		sz1 = sz * fld[j]->elsz;
		if ((p = TXmalloc(TXPMBUFPN, fn, sz1)) == NULL)
			goto err;
		memcpy(p, v, sz1);
		tfld = getfldn(tout->tbl, j, TXOFPN);
		freeflddata(tfld);
		setfld(tfld, p, sz1);
		putfld(tfld, p, sz);
		fld[j] = closefld(fld[j]);
	}
	fld = TXfree(fld);
	proj->p_type = PROJ_AGG_CALCED;
ok:
err:						/* wtf what is error return */
	ret = 0;
done:
	if (aggidx)
	{
		for (i = 0; i < aggsiz; i++)
			closeaggidx(&aggidx[i]);
		aggidx = TXfree(aggidx);
	}
	if (haveIndexLock)
		TXunlockindex(tin, INDEX_READ, NULL);
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return(ret);
}

/****************************************************************************/
/*
 *	Perform a projection on a tuple, and write to the result table
 */

int
tup_project(tin, tout, proj, fo)
DBTBL *tin;
DBTBL *tout;
PROJ *proj;
FLDOP *fo;
{
	static CONST char	fn[] = "tup_project";
	FLD  *fout;
	int  i;
	size_t sz;
	void *v;
	FTN	vType;
	char *nname;
	int usecachedfout = 0;

	if(proj->p_type == PROJ_SINGLE)
		faststats(tin, tout, proj, fo);
	else
	{
		if(proj->p_type < PROJ_UNSET)
			return 0;
		if(tout->cachedproj == proj && tout->projfldcache)
		{
			usecachedfout = 1;
		}
		else
		{
			tout->projfldcache = TXfree(tout->projfldcache);
			tout->projfldcache = TXcalloc(TXPMBUFPN, fn, proj->n, sizeof(FLD *));
			tout->cachedproj = proj;
		}
		for (i = 0; i < proj->n; i++)
		{
			/* WTF - cache fields, where?  In tout? */
			if(usecachedfout)
			{
				fout = tout->projfldcache[i];
			}
			else
			{
				nname = TXdisppred(proj->preds[i], 1, 0, 0);
				if(!nname)
					return -1;
				fout = dbnametofld(tout, nname);
				if(!fout)
				{
					tout->cachedproj = NULL;
					nname = TXfree(nname);
					return -1;
				}
				nname = TXfree(nname);
				tout->projfldcache[i] = fout;
			}
			v = (void *)NULL;
			vType = (FTN)0;
			switch(proj->p_type)
			{
				case PROJ_AGG_CALCED:
				case PROJ_AGG:
					v = evalpred(tin, proj->preds[i], fo,
						     &sz, &vType);
					/* Bug 5395: used to return -1 on
					 * NULL `v'; now try to support NULL,
					 * which should have a type, unlike
					 * no-rows, which should be type 0
					 * (except for some no-results SELECTs
					 * which we detect as PROJ_AGG_CALCED?)
					 */
					if (v == (void *)NULL &&
					    (vType == (FTN)0 ||
					     proj->p_type == PROJ_AGG_CALCED))
						return -1;
					break;
				default:
					break;
			}
			if (proj->p_type > PROJ_SINGLE && fout != (FLD *)NULL)
			{
				freeflddata(fout);
				if ((FTN)(vType & DDTYPEBITS) !=
				    TXfldbasetype(fout))
					putmsg(MERR, fn,
		   "Result column #%d result type %s is not expected type %s",
					       (i + 1), ddfttypename(vType),
					       TXfldtypestr(fout));
#ifndef NEVER /* WTF - Need to get size right */
				setfldandsize(fout, v, sz * fout->elsz + 1, FLD_FORCE_NORMAL);
#else
				putfld(fout, v, sz);
#endif
			}
		}
	}

	/* Bug 6809: copy DBTBL.rank too: */
	tout->rank = tin->rank;

	if (proj->p_type > PROJ_SINGLE)
	{
		putdbtblrow(tout, NULL);
	}
	return 0;
}

/****************************************************************************/

