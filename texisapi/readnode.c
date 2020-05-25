/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_EPI
#  include "os.h"
#endif /* USE_EPI */
#include "dbquery.h"
#include "texint.h"


#if DEBUG
static int dq = 0;
#endif
#ifndef OBJECT_READTOKEN
extern char *zztext;
#define readtoken(a) readtoken()
#define ZZTEXT zztext
#else
#define ZZTEXT toke->zztext
#endif

/******************************************************************/

static void countfieldtype(FLD *f, long *nChar, long *nLong, long *nDouble, long *nOther)
{
	int basetype = TXfldbasetype(f);
	switch(basetype)
	{
		case FTN_CHAR: *nChar=*nChar+1; break;
		case FTN_LONG: *nLong=*nLong+1; break;
		case FTN_DOUBLE: *nDouble=*nDouble+1; break;
		default: *nOther=*nOther+1; break;
	}
}

static int counttypes(QNODE *q, long *nChar, long *nLong, long *nDouble, long *nOther)
{
	int ntypes = 0;

	while(q->op == LIST_OP &&
	      q->left->op == LIST_OP &&
	      q->right->op == FIELD_OP)
	{
		countfieldtype((FLD *)(q->right->tname), nChar, nLong, nDouble, nOther);
		q = q->left;
	}
	if(q->op == FIELD_OP)
	{
		countfieldtype((FLD *)(q->tname), nChar, nLong, nDouble, nOther);
	}
	if(q->op == LIST_OP)
	{
		counttypes(q->left, nChar, nLong, nDouble, nOther);
		counttypes(q->right, nChar, nLong, nDouble, nOther);
	}
	if(*nChar > 0) ntypes++;
	if(*nLong > 0) ntypes++;
	if(*nDouble > 0) ntypes++;
	if(*nOther > 0) ntypes++;
	return(ntypes);
}

static int convertfield(QNODE *q, FLDOP *fo)
{
	FLD *f1, *f2, *f3;

	if(q->op != FIELD_OP)
		return 0;
	f1 = (FLD *)q->tname;
	if(TXfldbasetype(f1) == FTN_CHAR)
		return 0;
	f2 = createfld("varchar", 1, 0);
	fopush(fo, f1);
	fopush(fo, f2);
	foop(fo, FOP_CNV);
	f2 = closefld(f2);
	f2 = fopop(fo);
	f1 = closefld(f1);
	q->tname = f2;
}
static int convertfields(QNODE * q, FLDOP *fo)
{
	while(q->op == LIST_OP &&
	      q->left->op == LIST_OP &&
	      q->right->op == FIELD_OP)
	{
		convertfield(q->right, fo);
		q = q->left;
	}
	if(q->op == FIELD_OP)
		convertfield(q, fo);
	if(q->op == LIST_OP)
	{
		convertfields(q->left, fo);
		convertfields(q->right, fo);
	}
	return 0;
}
/******************************************************************/

static int countfields ARGS((QNODE *));

static int
countfields(q)
QNODE	*q;
{
	int count = 0;

	while(q->op == LIST_OP &&
	      q->left->op == LIST_OP &&
	      q->right->op == FIELD_OP)
	{
		count++;
		q = q->left;
	}
	if(q->op == FIELD_OP)
		count++;
	if(q->op == LIST_OP)
	{
		count += countfields(q->left);
		count += countfields(q->right);
	}
	return count;
}

/******************************************************************/

static int countlengths ARGS((QNODE *));

static int
countlengths(q)
QNODE	*q;
{
	int length = 0;

	while(q->op == LIST_OP &&
	      q->left->op == LIST_OP &&
	      q->right->op == FIELD_OP)
	{
		length += ((FLD *)q->right->tname)->size;
		q = q->left;
	}
	if(q->op == FIELD_OP)
		length += ((FLD *)q->tname)->size;
	if(q->op == LIST_OP)
	{
		length += countlengths(q->left);
		length += countlengths(q->right);
	}
	return length;
}

/******************************************************************/

static byte *walknadd ARGS((QNODE *, byte *, size_t));

static byte *
walknadd(q,v,n)
QNODE	*q;
byte	*v;
size_t	n;
{
	if(q->op == FIELD_OP)
	{
		memcpy(v, getfld(q->tname, NULL), n);
		v += n;
		return v;
	}
	if(q->op == LIST_OP)
	{
		v = walknadd(q->left, v, n);
		v = walknadd(q->right, v, n);
		return v;
	}
	return v;
}

/******************************************************************/

static size_t walknaddlong(QNODE *q,ft_long *v, FLD *f, size_t n)
{
	if(q->op == FIELD_OP)
	{
		v[n] = *(ft_long *)getfld(q->tname, NULL);
		if(f->issorted && n > 0)
		{
			if(v[n-1] > v[n])
				f->issorted = 0;
		}
		n++;
		return n;
	}
	if(q->op == LIST_OP)
	{
		n = walknaddlong(q->left, v, f, n);
		n = walknaddlong(q->right, v, f, n);
		return n;
	}
	return n;
}

/******************************************************************/

static char *walknaddstr ARGS((QNODE *q, char *v, FLD *f, byte *byteUsed));

static char *
walknaddstr(QNODE *q, char *v, FLD *f, byte *byteUsed)
{
	char	*s, *d;

	if(q->op == FIELD_OP)
	{
		size_t	n;

		/* Copy field `q->tname' to `v', noting in `byteUsed'
		 * which bytes occur, for later strlst-sep determination:
		 */
		for (s = getfld(q->tname, &n), d = v; *s; s++, d++)
			byteUsed[(byte)(*d = *s)] = 1;
		*d = '\0';
		if(f && (f->dsc.dptrs.strings))
		{
			if(f->issorted && f->dsc.ptrsused > 0)
			{
				int inorder;

				inorder = strcmp(v, f->dsc.dptrs.strings[f->dsc.ptrsused-1].v);
				if(inorder < 0)
					f->issorted = 0;
			}
			if(f->dsc.ptrsused < f->dsc.ptrsalloced)
			{
				f->dsc.dptrs.strings[f->dsc.ptrsused].v = v;
				f->dsc.dptrs.strings[f->dsc.ptrsused].len = strlen(v);
				f->dsc.ptrsused++;
			}
		}
		v += n;
		v++;
		return v;
	}
	if(q->op == LIST_OP)
	{
		v = walknaddstr(q->left, v, f, byteUsed);
		v = walknaddstr(q->right, v, f, byteUsed);
		return v;
	}
	return v;
}

/******************************************************************/

static QNODE *
convlisttovarfld(QNODE *q, DDIC *ddic, FLDOP *fo)
{
	static const char	fn[] = "convlisttovarfld";
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);
	int	n, i;
	FLD	*nf;
	void	*v;
	QNODE	*nq;
	byte	byteUsed[256];
	long nChar = 0, nLong = 0, nDouble = 0, nOther = 0;

	if(q->op != LIST_OP)
		return q;
	n = countfields(q);
	if(counttypes(q, &nChar, &nLong, &nDouble, &nOther) > 1)
	{
		convertfields(q, fo);
	}
	if(q->right->op == FIELD_OP)
		nf = newfld(q->right->tname);
	else /* Potential for disaster */
		return q;
	/* ---------- NOTE: see also convqnodetovarfld() ---------- */
	nf->type |= DDVARBIT;
	if(nf->elsz != 1)
	{
		switch(nf->type & DDTYPEBITS)
		{
		case FTN_LONG:
			nf->issorted=ddic->optimizations[OPTIMIZE_SORTED_VARFLDS];
			v = (void *)TXmalloc(pmbuf, fn, n * nf->elsz);
			walknaddlong(q, (ft_long *)v, nf, 0);
			putfld(nf, v, n);
			break;
		default:
			v = (void *)TXmalloc(pmbuf, fn, n * nf->elsz);
			walknadd(q, v, nf->elsz);
			putfld(nf, v, n);
		}
	}
	else
	{
		size_t	tsz, usedLen;
		char	*v1;
		ft_strlst	*sl;

		if(ddic->optimizations[OPTIMIZE_PTRS_TO_STRLSTS])
			nf->dsc.dptrs.strings = TXcalloc(pmbuf, fn, n, sizeof(TX_String));
		else
			nf->dsc.dptrs.strings = TXfree(nf->dsc.dptrs.strings);
		if(nf->dsc.dptrs.strings)
		{
			nf->dsc.allocedby = nf;
			nf->dsc.ptrsalloced=n;
			nf->issorted=ddic->optimizations[OPTIMIZE_SORTED_VARFLDS];
		}
		tsz = countlengths(q);		/* sum of items' lengths */
		tsz += n;			/* nul-term. each item */
		tsz ++;				/* nul-term. whole strlst */
		n = tsz + sizeof(ft_strlst);
		sl = (ft_strlst *)TXmalloc(pmbuf, fn, n + 1);/* +1 for fldmath nul-term. */
		((char *)sl)[n] = '\0';		/* for fldmath */
		memset(byteUsed, 0, sizeof(byteUsed));
		v = &sl->buf;
		v1 = walknaddstr(q, v, nf, byteUsed);  /* copy items to `v' */
		/* KNG 20071108 was not including final nul in `nb': */
		*(v1++) = '\0';			/* nul-term. whole strlst */
		sl->nb = v1 - (char *)v;	/* `nb' includes strlst nul */
		/* KNG 20160930 clear slack space for Valgrind, in case
		 * this field gets written to .vtx file:
		 */
		usedLen = v1 - (char *)sl;
		if (usedLen < (size_t)n)
			memset(v1, 0, n - usedLen);
		else				/* wtf should not happen */
			putmsg(MERR + MAE, __FUNCTION__, "strlst overflow");
		/* KNG 20120227 use printable delim if possible: */
		for (i=0; i<256 && byteUsed[(byte)TxPrefStrlstDelims[i]]; i++);
		sl->delim = (i < 256 ? TxPrefStrlstDelims[i] : '\0');
		v = sl;
		nf->type = FTN_STRLST;
		setfldandsize(nf, v, n + 1, FLD_FORCE_NORMAL);	/* +1 for fldmath nul-term. */
	}
	if ((nq = openqnode(FIELD_OP)) == QNODEPN) return(QNODEPN);
	nq->tname = nf;
	q = closeqnode(q);
	return nq;
}

/******************************************************************/

static int
TXhaslikep(QNODE *q)
{
	int rc;

	switch(q->op)
	{
		case FOP_PROXIM:
			return 1;
		case FOP_AND:
		case FOP_OR:
			rc = TXhaslikep(q->right);
			if(rc)
				return rc;
		case NOT_OP:
			return TXhaslikep(q->left);
		default:
			break;
	}
	return 0;
}

/******************************************************************/

static int
allnamenum(QNODE *q)
{
	int rc;

	switch(q->op)
	{
		case FOP_AND:
		case FOP_OR:
		case LIST_OP:
			rc = allnamenum(q->right);
			if(!rc)
				return rc;
		case NOT_OP:
#ifdef TX_USE_ORDERING_SPEC_NODE
		case ORDERING_SPEC_OP:
#endif /* TX_USE_ORDERING_SPEC_NODE */
			return allnamenum(q->left);
		case NAMENUM_OP:
			return 1;
		default:
			break;
	}
	return 0;
}

/******************************************************************/
/*	This is a hand coded parser for my internal language.  It
 *	builds the tree of actions that will be required to perform
 *	the query
 */

static QNODE *
ireadnode(DDIC *ddic, TX_READ_TOKEN *toke, int depth, QNODE *pq, QTOKEN x, FLDOP *fo);

/******************************************************************/
/*	Hand unrolled loop for LIST_OP.  Original code:
		case LIST_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ", ");
#endif
			this->right = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
*/
static int
ireadlstnode(DDIC *ddic, TX_READ_TOKEN *toke, int depth, QNODE *pq, FLDOP *fo)
{
	int x;
	QNODE *this, *next;

	this = pq;
	x = readtoken(toke);
#if DEBUG
	if (dq)
		fprintf(stderr, "(");
#endif
	while(x == LIST_OP)
	{
		next = openqnode(x);
		next->op = x;
		next->parentqn = this;
		this->left = next;
		this = next;
		x = readtoken(toke);
	}
	while(this)
	{
		next = ireadnode(ddic, toke, depth, this, x, fo);
		if(this->left == NULL)
			this->left = next;
		else if(this->right == NULL)
		{
#if DEBUG
			if (dq)
				fprintf(stderr, ", ");
#endif
			this->right = next;
			if(this == pq)
				return 0;
			this = this->parentqn;
		}
		x = readtoken(toke);
	}
#if DEBUG
	if (dq)
		fprintf(stderr, ")");
#endif
	return 0;
}

/******************************************************************/

#define READNEXTNODE ireadnode(ddic, toke, depth + 1, this, 0, fo)

static QNODE *
ireadnode(DDIC *ddic, TX_READ_TOKEN *toke, int depth, QNODE *pq, QTOKEN x, FLDOP *fo)
{
	static CONST char Fn[]="readnode";
	TXPMBUF	*pmbuf = (ddic ? ddic->pmbuf : TXPMBUFPN);
	QNODE	*this;
	int	size, notnull;
	unsigned long	t;
	long	t2;
	double	d;
	char	*name, *type;
	DD	*dd;
	FLD	*f;

	if(x == 0)
		x = readtoken(toke);
	this = openqnode(x);
	if(!this)
		return this;
	this->parentqn = pq;
	switch (x)
	{
		case SELECT_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " SELECT ");
#endif
			this->right = READNEXTNODE;
			if(!this->right)
			{
			errClose:
				this = closeqnode(this);
				return NULL;
			}
			this->op = x;
			if(TXlikepmode && TXhaslikep(this->right))
			{
				QNODE *this2;

				this2 = openqnode(FLDMATH_PROXIM);
				if (!this2) goto errClose;
				this2->left = this;
				this = this2;
				/* KNG maintain `parentqn' for dolikep(): */
				this->parentqn = pq;
				this->left->parentqn = this;
			}
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case HAVING_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " HAVING ");
#endif
			this->left = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case PROJECT_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " PROJECT ");
#endif
			this->right = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			if((this->left == NULL) || (this->right == NULL))
				this = closeqnode(this);
			break;
		case PRODUCT_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " JOIN ");
#endif
			this->right = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case UNION_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " UNION ");
#endif
			this->right = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case LIST_OP :
			ireadlstnode(ddic, toke, depth, this, fo);
			break;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ", ");
#endif
			this->right = READNEXTNODE;
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case NOT_OP:
#if DEBUG
			if (dq)
				fprintf(stderr, "NOT (");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			this->op = NOT_OP;
			break;
		case EXISTS_OP:
#if DEBUG
			if (dq)
				fprintf(stderr, "EXISTS (");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			this->op = EXISTS_OP;
			break;
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
		case FOP_AND :
		case FOP_OR :
		case FOP_NEQ :
		case FOP_MM :
		case FOP_NMM :
		case FOP_RELEV :
		case FOP_PROXIM :
		case FOP_MMIN :
		case FOP_IN :
		case FOP_IS_SUBSET :
		case FOP_INTERSECT :
		case FOP_INTERSECT_IS_EMPTY :
		case FOP_INTERSECT_IS_NOT_EMPTY :
		case FOP_MAT :
		case FOP_TWIXT :
#if DEBUG
		{	char temp;
			if (dq)
				fprintf(stderr, "(");
			temp = ZZTEXT[0];
#endif
			this->op = x;
			this->left = READNEXTNODE;
			if(!this->left)
			{
				this = closeqnode(this);
				break;
			}
#if DEBUG
			if (dq)
				switch (temp)
				{
					case '|' :
						fprintf(stderr, " OR ");
						break;
					case '&' :
						fprintf(stderr, " AND ");
						break;
					case '~' :
						fprintf(stderr, " LIKE ");
						break;
					case '!' :
						fprintf(stderr, " <> ");
						break;
					default :
						fprintf(stderr, " %c ", temp);
						break;
				}
		}
#endif /* DEBUG */
			this->right = READNEXTNODE;
			if(!this->right)
			{
				this = closeqnode(this);
				break;
			}
			switch (this->op)
			{
			case FOP_IN:
			case FOP_IS_SUBSET:
			case FOP_INTERSECT:
			case FOP_INTERSECT_IS_EMPTY:
			case FOP_INTERSECT_IS_NOT_EMPTY:
			case FOP_TWIXT:
				/* NOTE: see also ctreetopred(): */
				this->right = convlisttovarfld(this->right, ddic, fo);
				/* KNG SUBSET/INTERSECT may have a LHS list: */
				this->left = convlisttovarfld(this->left, ddic, fo);
				break;
			default:
				break;
			}
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case ARRAY_OP:
			this->left = READNEXTNODE;
			this->left = convlisttovarfld(this->left, ddic, fo);
			break;
		case RENAME_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " RENAMED ");
#endif
			x = readtoken(toke);
			if(!this->left)
			{
				this = closeqnode(this);
				return NULL;
			}
			if (x != NAME_OP)
			{
				char	buf[128];
				putmsg(MERR + UGE, Fn,
	       "Parse error: Expected NAME_OP after RENAME_OP, but got %s",
				       TXqnodeOpToStr(x, buf, sizeof(buf)));
				this = closeqnode(this);
				return ((QNODE *)NULL);
			}
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
#if DEBUG
			if (dq)
				fprintf(stderr, "%s)", ZZTEXT);
#endif
			this->op = RENAME_OP;
			break;
		case NAME_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "%s", ZZTEXT);
#endif
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			this->op = NAME_OP;
			break;
                case HINT_OP :
#ifndef TX_SQL_WITH_HINTS
				putmsg(MERR + UGE, Fn, "Parse error: WITH (HINTS) unsupported");
#endif
                        this->op = HINT_OP;
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, "WITH ");
#endif
			this->right = READNEXTNODE;
                        break;
#ifdef TX_USE_ORDERING_SPEC_NODE
                case ORDERING_SPEC_OP:
                        this->op = ORDERING_SPEC_OP;
			/* For now, we just put the `-' `^' flags in
			 * `tname'.  Eventually might want to hang
			 * a LIST_OP of options off this node, ala
			 * index WITH options, e.g. for stringcomparemodes:
			 */
			this->tname = TXstrndup(pmbuf, __FUNCTION__,
					  ZZTEXT, strcspn(ZZTEXT, " \t\r\n"));
			this->left = READNEXTNODE;
                        break;
#endif /* TX_USE_ORDERING_SPEC_NODE */
		case NAMENUM_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "%s", ZZTEXT);
#endif
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			this->op = NAMENUM_OP;
			break;
		case ALL_OP :
			this->op = ALL_OP;
			break;
		case PARAM_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "?");
#endif
#ifdef NEVER
			this->tname = TXcalloc(pmbuf, Fn, 1, sizeof(PARAM));
#ifndef OLDPARAM
			((PARAM *)this->tname)->num=strtol(ZZTEXT+1, NULL, 10);
#endif /* OLDPARAM */
#else /* NEVER */
			this->left = (void *)strtol(ZZTEXT+1, NULL, 10);
#endif
			this->op = PARAM_OP;
			break;
		case STRING_OP :
			this->tname = createfld("varchar", 1, 0);
#if DEBUG
			if (dq)
				fprintf(stderr, "\"%s\"", ZZTEXT);
#endif
			if (!strstr(ZZTEXT, "''"))
				putfld(this->tname, (void *)TXstrdup(pmbuf, Fn, ZZTEXT),
					strlen(ZZTEXT));
			else			/* copy, condense "''" */
			{
				char *t, *p, *q;

				t = TXcalloc(pmbuf, Fn, strlen(ZZTEXT), 1);
				for (p=ZZTEXT,q=t; *p; p++, q++)
				{
					*q = *p;
					if (*p == '\'')
						p++;
				}
				putfld(this->tname, t, strlen(t));
			}
			this->op = FIELD_OP;
			break;
		case COUNTER_OP :
			this->tname = createfld("counteri", 1, 0);
#if DEBUG
			if (dq)
				fprintf(stderr, "COUNTER", ZZTEXT);
#endif
			this->op = FIELD_OP;
			break;
		case TX_QNODE_NUMBER:
		      {
			int	errNum;

			t = TXstrtoul(ZZTEXT, NULL, NULL,
				      (0 | TXstrtointFlag_NoLeadZeroOctal |
				       TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
				      &errNum);
			this->tname = createfld("long", 1, 0);
			if (!this->tname) goto err;
#if DEBUG
			if (dq)
				fprintf(stderr, "%s", ZZTEXT);
#endif
			setfldv((FLD *)this->tname);
			if (errNum)
				TXfldSetNull((FLD *)this->tname);
			else
			{
				ft_long	*v;

				if (!(v = TX_NEW_ARRAY(pmbuf, 2, ft_long)))
					goto err;
				*v = t;
				if (setfldandsize((FLD *)this->tname, v,
						  sizeof(ft_long) + 1, FLD_FORCE_NORMAL) < 0)
					goto err;
			}
		      }
			this->op = FIELD_OP;
			break;
		case NNUMBER :
		      {
			int	errNum;

			t2 = TXstrtol(ZZTEXT, NULL, NULL,
				      (0 | TXstrtointFlag_NoLeadZeroOctal |
				       TXstrtointFlag_ConsumeTrailingSpace |
				       TXstrtointFlag_TrailingSourceIsError),
				      &errNum);
			this->tname = createfld("long", 1, 0);
			if (!this->tname) goto err;
#if DEBUG
			if (dq)
				fprintf(stderr, "%s", ZZTEXT);
#endif
			setfldv((FLD *)this->tname);
			if (errNum)
				TXfldSetNull((FLD *)this->tname);
			else
			{
				ft_long	*v;

				if (!(v = TX_NEW_ARRAY(pmbuf, 2, ft_long)))
					goto err;
				*v = t2;
				if (setfldandsize((FLD *)this->tname, v,
						  sizeof(ft_long) + 1, FLD_FORCE_NORMAL) < 0)
					goto err;
			}
		      }
			this->op = FIELD_OP;
			break;
		case FLOAT_OP :
			d = atof(ZZTEXT);
			this->tname = createfld("double", 1, 0);
#if DEBUG
			if (dq)
				fprintf(stderr, "%s", ZZTEXT);
#endif
			setfldv((FLD *)this->tname);
			*(double *)((FLD *)this->tname)->v = d;
			this->op = FIELD_OP;
			break;
		case TABLE_AS_OP :
			this->op = TABLE_AS_OP;
			readtoken(toke);
			this->right = TXmalloc(pmbuf, Fn, 1);
			((char *)this->right)[0] = ZZTEXT[0];
			this->left = READNEXTNODE;
			x = readtoken(toke);
			if(x == NAME_OP)
				this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			break;
		case TABLE_OP :
			dd = opendd();
			readtoken(toke);
			switch(ZZTEXT[0])
			{
				case 'F':
					(void)ddsettype(dd, TEXIS_FAST_TABLE);
					break;
				case 'C':
					(void)ddsettype(dd, TEXIS_OLD_TABLE);
					break;
				case 'R':
					(void)ddsettype(dd, TEXIS_RAM_TABLE);
					break;
				case 'B':
					(void)ddsettype(dd, TEXIS_BTREE_TABLE);
					break;
				case 'X':
					(void)ddsettype(dd, TEXIS_RAM_BTREE_TABLE);
					break;
				default:
#ifdef NULLABLE_TEXIS_TABLE
					(void)ddsettype(dd, TEXIS_NULL1_TABLE);
#else
					(void)ddsettype(dd, TEXIS_FAST_TABLE);
#endif
					break;
			}
			while ((x = readtoken(toke)) == COLUMN_OP)
			{
				readtoken(toke);
				name = TXstrdup(pmbuf, Fn, ZZTEXT);
				readtoken(toke);
				type = TXstrdup(pmbuf, Fn, ZZTEXT);
				size = 1;
#ifdef NULLABLE_TEXIS_TABLE
				notnull=1;
#else
				notnull=0;
#endif
				x = readtoken(toke);
				if(x == TX_QNODE_NUMBER)
				{
					size = atoi(ZZTEXT);
					x = readtoken(toke);
				}
				while (x != COLUMN_OP)
				{
#ifdef NULLABLE_TEXIS_TABLE
					if(x == NAME_OP)
					{
						if(*ZZTEXT == 'N')
							notnull = 0;
					}
#endif
					x = readtoken(toke);
				}
				if(!putdd(dd, name, type, size, notnull))
				{
					DD *ndd;

					ndd = TXexpanddd(dd, 10);
					if(ndd)
					{
						closedd(dd);
						dd = ndd;
						ndd = NULL;
					}
					if(!putdd(dd, name, type, size, notnull))
					{
						putmsg(MERR, NULL,
			       "Could not create field `%s' of type %s(%d)",
						       name, type, (int)size);
						name = TXfree(name);
						type = TXfree(type);
						goto errdd;
					}
				}
				name = TXfree(name);
				type = TXfree(type);
			}
			/* KNG 20060228 do not allow FTN_INTERNAL, at least
			 * for now, as it is OO and not flat mem, and would
			 * fail in buftofld() etc.  Check here in SQL parse
			 * rather than putdd(), so that internal/RAM/B-tree
			 * return tables for SELECT rows support FTN_INTERNAL
			 * for fldmath etc.
			 */
			if (!TXddOkForTable(TXPMBUFPN, dd))
			{
			errdd:
				dd = closedd(dd);
				this = closeqnode(this);
				return(this);
			}
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			this->left = (void *)dd;
			this->op = TABLE_OP;
			break;
		case INSERT_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, "INSERT INTO ");
#endif
			this->op = INSERT_OP;
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " ");
#endif
			this->right = READNEXTNODE;
			break;
		case VALUE_OP :
#if DEBUG
			if (dq)
				fprintf(stderr, " (");
#endif
			this->op = VALUE_OP;
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case COLUMN_OP :
			this->op = COLUMN_OP;
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, "=");
#endif
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " ");
#endif
			break;
		case DEL_SEL_OP :
			this->op = DEL_SEL_OP;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " DELETE ");
#endif
#if 1
			this->right = READNEXTNODE;
#endif
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case DEL_ALL_OP :
			this->op = DEL_SEL_OP;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " DELETE ");
#endif
			this->right = NULL;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case UPD_SEL_OP :
			this->op = UPD_SEL_OP;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			/* Read the table name */
			this->tname = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " UPDATE ");
#endif
			/* Read the Update spec */
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, " WHERE ");
#endif
			/* Read the condition */
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case UPD_ALL_OP :
			this->op = UPD_SEL_OP;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
#if 1
			this->tname = READNEXTNODE;
#endif
#if DEBUG
			if (dq)
				fprintf(stderr, " UPDATE ALL ");
#endif
			this->left = READNEXTNODE;
#if 1
			this->right = (QNODE *)NULL;
#else
			this->right = READNEXTNODE;
#endif
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case REG_FUN_OP :
		case AGG_FUN_OP :
			this->op = x;
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, "(");
#endif
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case ORDER_OP :
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, "(ORDER BY (");
#endif
			this->left = READNEXTNODE;
			if(allnamenum(this->left))
				this->op = ORDERNUM_OP;
#if DEBUG
			if (dq)
				fprintf(stderr, ") ");
#endif
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case GRANT_OP :
		case REVOKE_OP :
			this->op = x;
			this->left = READNEXTNODE;
			this->right = READNEXTNODE;
			x = readtoken(toke);
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			break;
		case CONVERT_OP :
			this->op = FLDMATH_CNV;
			this->left = READNEXTNODE;	/* expr to convert */
			if(!this->left)
			{
				this = closeqnode(this);
				break;
			}
			this->right = openqnode(FIELD_OP);
			x = readtoken(toke);		/* name of type */
			this->right->tname = f = createfld(ZZTEXT, 1, 0);
			if (f)
			{
				/* KNG 20060217 use tx_fti_... calls later: */
				if ((f->type & DDTYPEBITS) != FTN_INTERNAL)
					setfld(f, TXcalloc(pmbuf, __FUNCTION__,
							   1, f->size + 1),
					       f->size + 1);
			}
			else
			{
				putmsg(MWARN+UGE, "convert",
				"Unknown type `%s'", ZZTEXT);
				this = closeqnode(this);
			}
			break;
		case DISTINCT_OP :
			this->op = x;
			this->right = READNEXTNODE;
			break;
		case GROUP_BY_OP :
			this->op = x;
#if DEBUG
			if (dq)
				fprintf(stderr, "(GROUP BY (");
#endif
			this->left = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ") ");
#endif
			this->right = READNEXTNODE;
#if DEBUG
			if (dq)
				fprintf(stderr, ")");
#endif
			break;
		case PROP_OP:				/* JMT 97-11-24 */
			this->op = x;
			this->right = READNEXTNODE;
			break;
		case VIEW_OP :
			this->op = VIEW_OP;
/*
			readtoken(toke);
			this->right = TXmalloc(pmbuf, Fn, 1);
			((char *)this->right)[0] = ZZTEXT[0];
*/
			x = readtoken(toke);
			if(x == NAME_OP)
				this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			this->left = READNEXTNODE;
			break;
		case CREATE_OP :
		case ALTER_OP:
			this->op = x;
			readtoken(toke);	/* "table"/"index"/"user" */
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			if (this->op == ALTER_OP &&
			    strcmpi(this->tname, "index") == 0)
			{			/* index table action ... */
				this->left = openqnode(LIST_OP);
				if (!this->left) goto err;
				this->left->left = READNEXTNODE; /* index */
				this->left->right = READNEXTNODE; /* table */
				this->right = READNEXTNODE;	/* actions */
			}
			else			/* CREATE, or ALTER !INDEX */
			{
				/* indexName, table, indexType: */
				this->left = READNEXTNODE;
				this->right = READNEXTNODE;	/* fields */
			}
			break;
		case DROP_OP :
		{
			this->op = DROP_OP;
			readtoken(toke); /* "table"/"index"/"trigger"/"user" */
			this->tname = TXstrdup(pmbuf, Fn, ZZTEXT);
			this->left=READNEXTNODE;	/* index etc. name */
			this->right = READNEXTNODE;	/* opt. "ifexists" */
			if (this->right && this->right->op == 0)
				/* there was no "ifexists" */
				this->right = closeqnode(this->right);
			break;
		}
		case SUBQUERY_OP :
		{
			this->op = SUBQUERY_OP;
			this->left=READNEXTNODE;
			break;
		}
		case NULL_OP:
			/* Technically a NULL token is untyped at this point,
			 * but should be ok to make it varchar:
			 */
			this->tname = createfld("varchar", 1, 0);
			this->op = FIELD_OP;
			break;
		case 0 :
			break;
		default :
			putmsg(MERR+UGE, Fn, "Unknown Symbol %d (%s?)",
				(int)x, TXqnodeOpToStr(x, NULL, 0));
			this = closeqnode(this);
			return (QNODE *)NULL;
	}
#if DEBUG
	if (dq && depth == 0)
		fprintf(stderr, "\n");
#endif
	return this;
err:
	this = TXfree(this);
	return(this);
}

/******************************************************************/

#ifdef OBJECT_READTOKEN
QNODE *
readnode(DDIC *ddic, FLDOP *fo, TX_READ_TOKEN *toke, int depth)
{
	return ireadnode(ddic, toke, depth, NULL, 0, fo);
}
#else
QNODE *
readnode(DDIC *ddic, FLDOP *fo, int depth)
{
	return ireadnode(ddic, NULL, depth, NULL, 0, fo);
}
#endif

/******************************************************************/
