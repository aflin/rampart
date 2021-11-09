/* -=- kai-mode: John -=- */

#include "txcoreconfig.h"
#include "stdio.h"
#ifdef _WINDLL			/* for msvc 8 */
int __cdecl sscanf(const char *, const char *, ...);
#endif
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"
#ifdef USE_EPI
#  include "sizes.h"
#  include "os.h"
#else
#  ifdef __BORLANDC__
#     define off_t long
#  endif
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldops.h"
#include "cgi.h"		/* for htsnpf() */

/**********************************************************************
implemented functions:
   -- is "don't do"
   ## is made with template #
   cc is made with custom code
   ?? is "should we?"

   11 handles homogeneous types          fldop1.c
   22 handles large arg1 and small arg2  fldop2.c
   33 handles small arg1 and large arg2  fldop3.c
      promotes arg1 to type of arg2 then calls template 11
   44 formats to human readable string   fldop4.c
   55 casts arg1 to arg2 type            fldop5.c

arg1 is down   the table
arg2 is across the table

   ch by sh sm in ir wo dw lo fl do da co sl id de bl ha
ch cc cc 44 44 44 44 44 44 44 44 44 cc cc cc cc -- -- -- char
by    11       55          55                   -- -- -- byte
sh 44    11    33          33 33 33             -- -- -- short
sm 44       11 33          33 33 33             -- -- -- smallint
in 44 55 22 22 11 11       33 33 33             -- -- -- int
ir 44          11 11                            -- -- -- integer
wo 44                11                         -- -- -- word
dw 44                   11                      -- -- -- dword
lo 44 55 22 22 22          11 33 33 11          -- -- -- long
fl 44    22 22 22          22 11 33             -- -- -- float
do 44    22 22 22          22 22 11             -- -- -- double
da cc                      11       11          -- -- -- date
co cc                                  cc       -- -- -- counter
sl cc                                     cc    -- -- -- strlst
id                                              -- -- -- indirect
de -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- decimal
bl -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- ?? -- blob
ha -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- ?? handle


WARNING: remember to add to fosetop() to foopen() for added functions
         and to fldops.h
**********************************************************************/

#ifndef MIN
#  define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#  define MAX(a,b) ((a)>(b)?(a):(b))
#endif

/**********************************************************************/
#ifdef __stdc__
int
fld2finv(FLD * f, ft_int val)
#else
int
fld2finv(f, val)		/* set fld to fixed int type set to val */
FLD *f;
ft_int val;
#endif
{
	size_t elsz;
	void *p;

	if ((f->type & DDTYPEBITS) == FTN_INTERNAL)
		TXfreefldshadow(f);		/* before changing type */
	elsz = sizeof(ft_int);
	p = getfld(f, NULL);
	if (f->alloced < elsz + 1 || !p)
	{
		if ((p = malloc(elsz + 1)) == (void *) NULL)
			return (FOP_ENOMEM);
		*((char *) p + elsz) = '\0';
		setfld(f, p, elsz + 1);
	}
	f->kind = TX_FLD_NORMAL;
	f->type = FTN_INT;
	f->n = 1;
	f->elsz = elsz;
	f->size = elsz;
	*(ft_int *) p = val;
	putfld(f, p, 1);
	return (0);
}				/* end fld2finv() */

/**********************************************************************/

/**********************************************************************/
#ifdef __stdc__
int
fld2flov(FLD * f, ft_long val)
#else
int
fld2flov(f, val)		/* set fld to fixed long type set to val */
FLD *f;
ft_long val;
#endif
{
	size_t elsz;
	void *p;

	if ((f->type & DDTYPEBITS) == FTN_INTERNAL)
		TXfreefldshadow(f);		/* before changing type */
	elsz = sizeof(ft_long);
	if (f->alloced < elsz + 1)
	{
		if ((p = malloc(elsz + 1)) == (void *) NULL)
			return (FOP_ENOMEM);
		*((char *) p + elsz) = '\0';
		setfld(f, p, elsz + 1);
	}
	else
		p = getfld(f, NULL);
	f->kind = TX_FLD_NORMAL;
	f->type = FTN_LONG;
	f->n = 1;
	f->elsz = elsz;
	f->size = elsz;
	*(ft_long *) p = val;
	putfld(f, p, 1);
	return (0);
}				/* end fld2flov() */

/**********************************************************************/

/**********************************************************************/
void
fldswp(f1, f2)
FLD *f1, *f2;
{
	FLDSWP(f1, f2);
}				/* end fldswp() */

/**********************************************************************/

/**********************************************************************/

#ifdef FLDIGNORECASE
/*#define STRCMP(a,b)    strcmpi(a,b)*/
#define STRNCMP(a,b,n) strnicmp(a,b,n)
#define CHR4CMP(a)     tolower(a)
#else
/*#define STRCMP(a,b)    strcmp(a,b)*/
#define STRNCMP(a,b,n) strncmp(a,b,n)
#define CHR4CMP(a)     a
#endif

#ifdef NO_NEW_STRCMP
#define STRCMP(a,b)	(TXigncase?strcmpi((a),(b)):strcmp((a),(b)))
#else

#define STRCMP(a,b, c, d)	TXstringcompare(a, b, c, d)

int
TXstringcompare(a, b, an, bn)
char *a, *b;
size_t an, bn;
{
	int	cmp;
	char	*ae, *be;

	/* KNG 20040810 may be getting NULLs here causing ABEND? */
	/* KNG Bug 5895 support SQL NULL.  All compare ops with a NULL
	 * should return NULL/unknown, but we need a consistent order
	 * for indexes; see TXDOUBLE_CMP() definition comments in txtypes.h,
	 * as this is a similar issue as NaN:
	 */
	if (!a) return(b ? 1 : 0);
	if (!b) return(-1);

	TXget_globalcp();
	/* TXunicodeStrFoldCmp() does not stop at nul if length(s) given
	 * (for binary data support eg. <xtree>), but some char fields
	 * have nul padding (eg. fixed-width SYSMETAINDEX fields),
	 * so force stop-at-nul via -1 lengths here:
	 */
	ae = a + an;
	be = b + bn;
	cmp = TXunicodeStrFoldCmp((CONST char**)&a, -1, (CONST char**)&b, -1,
				  globalcp->stringcomparemode);
	/* Bug 2938: only return ...ISPREFIX if `a' is truly truncated: */
	if (cmp == TX_STRFOLDCMP_ISPREFIX)
	{
		if (a < ae)			/* `a' is not truncated */
		{
			/* We know `*a' is now nul, because we passed -1 for
			 * length of `a' (nul-termination), and we got
			 * ...ISPREFIX, which only happens if `a' compares
			 * equal thru to its EOF.  So we only need to check
			 * if `b' is now at EOF to distinguish equality vs.
			 * a < b:
			 */
			if (b >= be || *b == '\0') cmp = 0;
			else cmp = -1;
		}
	}
	return(cmp);
}

#endif

/******************************************************************/

int
TXmakesimfield(f1, f2)
FLD	*f1;	/* (in) source field */
FLD	*f2;	/* (out) target field */
/* Sets `f2' to `f1's type and size.
 * Returns 0 if ok.
 */
{
	if ((f1->type & DDTYPEBITS) == FTN_INTERNAL ||
	    (f2->type & DDTYPEBITS) == FTN_INTERNAL)
		TXfreefldshadow(f2);		/* before changing f2 type */
	f2->type = f1->type;
	f2->elsz = f1->elsz;
	f2->size = f1->size;
	f2->n = f1->n;
	if ((f2->type & DDTYPEBITS) == FTN_INTERNAL)
		return(0);			/* no direct shadow malloc */
	if ((f1->size + 1) > f2->alloced)
	{
		void *v;

		v = malloc(f1->size + 1);
		setfld(f2, v, f1->size + 1);
	}
	return 0;
}

int
TXfreevirtualdata(f1)
FLD *f1;
/*
 * Originally written to free virtual field data.  Could be used for any computed field
 */
{
	if (FLD_IS_COMPUTED(f1))
	{
		TXfreefldshadownotblob(f1);
		f1->v = NULL;
	}
	return 0;
}

static int stringcmp ARGS((FLD * f1, FLD * f2, FLD * f3, int op));
/**********************************************************************/
static int
stringcmp(f1, f2, f3, op)
FLD *f1, *f2, *f3;
int op;
{
	int rc, cmp;
#ifndef NO_NEW_STRCMP
	size_t sz1, sz2;
	void *a, *b;

	a = getfld(f1, &sz1);
	b = getfld(f2, &sz2);

	/* Bug 5395 SQL NULL: */
	if (TXfldIsNull(f1) || TXfldIsNull(f2))
	{
		if (op == FOP_COM)
			return(fld2finv(f3, TX_FLD_NULL_CMP(a, b,
						    STRCMP(a, b, sz1, sz2))));
		return(TXfldmathReturnNull(f1, f3));
	}

	/* Empty string is empty set (but not for FOP_IN: Bug 3677 #13),
         * which is a subset of anything:
         */
	if (sz1 == 0 && op == FOP_IS_SUBSET)
		return(fld2finv(f3, 1));	/* true */

	cmp = STRCMP(a, b, sz1, sz2);
	TXfreevirtualdata(f1);
	switch (op)
	{
	case FOP_EQ:
		/* Single-string compare to single-string, so subset and
		 * intersect modes behave the same (iff LHS non-empty):
		 */
	case FOP_IN:
	case FOP_IS_SUBSET:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
		switch (cmp)
		{
		case 0:
			return fld2finv(f3, (op != FOP_INTERSECT_IS_EMPTY));
		case TX_STRFOLDCMP_ISPREFIX:
			return fld2finv(f3, TX_STRFOLDCMP_ISPREFIX);
		default:
			return fld2finv(f3, (op == FOP_INTERSECT_IS_EMPTY));
		}
		break;
	}
#else
	cmp = STRCMP(getfld(f1, NULL), getfld(f2, NULL));
#endif
	switch (op)
	{
	case FOP_IN:		/* Same as equal, as there is only one string, otherwise
				   should be using stringlist */
	case FOP_IS_SUBSET:
	case FOP_INTERSECT_IS_EMPTY:
	case FOP_INTERSECT_IS_NOT_EMPTY:
	case FOP_EQ:
		rc = (cmp == 0);
		if (op == FOP_INTERSECT_IS_EMPTY) rc = !rc;
		rc = fld2finv(f3, rc);
		break;
	case FOP_NEQ:
		rc = fld2finv(f3, cmp != 0);
		break;
	case FOP_LT:
		rc = fld2finv(f3, cmp < 0);
		break;
	case FOP_LTE:
		rc = fld2finv(f3, cmp <= 0);
		break;
	case FOP_GT:
		rc = fld2finv(f3, cmp > 0);
		break;
	case FOP_GTE:
		rc = fld2finv(f3, cmp >= 0);
		break;
	case FOP_COM:
		rc = fld2finv(f3, cmp);
		break;
	default:
		rc = FOP_EINVAL;
	}
	return (rc);
}				/* end stringcmp() */

/**********************************************************************/

static int varcat ARGS((FLD * f1, FLD * f2));
/**********************************************************************/
static int
varcat(f1, f2)
FLD *f1, *f2;
{
	unsigned int need;
	ft_char *p;
	void *v1, *v2;

	v1 = getfld(f1, NULL);
	v2 = getfld(f2, NULL);
	need = f1->size + f2->size + 1;
	if ((p = (ft_char *) malloc(need)) == (char *) NULL)
		return (FOP_ENOMEM);
	memcpy(p, v1, f1->size);
	setfld(f1, p, need);
	memcpy(p + f1->size, v2, f2->size);
	p[need - 1] = '\0';
	f1->n += f2->n;
	f1->size += f2->size;
	return (0);
}				/* end varcat() */

/**********************************************************************/

static int stringcut ARGS((FLD * f1, FLD * f2));
/**********************************************************************/
static int
stringcut(f1, f2)
FLD *f1, *f2;
{
	ft_char *p1, *p2, *e, *e2, *d;
	size_t sz1, sz2;

/* JMT 980109 Allow removing empty string */
	p1 = getfld(f1, &sz1);
	p2 = getfld(f2, &sz2);
	if (f2->size <= f1->size)
	{
		e2 = p1 + f1->size;
		e = e2 - f2->size + 1;
		if (!f2->size)
			e--;
		for (d = p1; p1 < e;)
		{
			if (STRNCMP(p1, p2, f2->size) == 0)
			{
				if (f2->size == 0)
				{
					if (*p1 == '\0')
					{
						p1 += 1;
						f1->n -= 1;
						f1->size -= 1;
					}
					else
					{
						*(d++) = *(p1++);
					}
				}
				p1 += f2->size;
				f1->n -= f2->n;
				f1->size -= f2->size;
			}
			else
			{
				*(d++) = *(p1++);
			}
		}
		for (; p1 < e2;)
			*(d++) = *(p1++);
		*d = '\0';
	}
	return (0);
}				/* end stringcut() */

/**********************************************************************/

/**********************************************************************/
/* byte byte */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
/* Included here to do comparisons */
int
fobyby(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_byte *vp1, *vp2, *vp3 = NULL;
	size_t n1, n2, n3, n;
	int var1;
	int rc = 0;

	vp1 = (ft_byte *) getfld(f1, &n1);
	vp2 = (ft_byte *) getfld(f2, &n2);
	var1 = fldisvar(f1);
	if (n1 > 1 || var1)
	{
		switch (op)
		{
		case FOP_ADD:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			copyfld(f3, f1);
			rc = varcat(f3, f2);
			break;	/* wtf - trim if fixed?? */
		case FOP_CNV:
			rc = fobyby(f2, f1, f3, FOP_ASN);
			break;
		case FOP_ASN:			/* f1 data = f2 data */
			if (var1)
			{
				copyfld(f3, f2);	/* f3 = f2 */
				f3->type = f1->type;	/* save f1 var bit */
			}
			else
			{
				byte *v;

				f3->type = f1->type;
				f3->elsz = f1->elsz;
				vp2 = (ft_byte *) getfld(f2, &n2);
				n = MIN(f1->size, n2);
				v = malloc(f1->alloced + 1);
				setfld(f3, v, n);
				f3->size = f1->size;
				f3->n = f1->n;
				memcpy(v, vp2, n * sizeof(ft_char));
				memset(v + n, '\0', f1->size - n);	/* zero pad */
			}
			break;
		case FOP_EQ:
		case FOP_NEQ:
		case FOP_LT:
		case FOP_LTE:
		case FOP_GT:
		case FOP_GTE:
		case FOP_COM:
			rc = memcmp(vp1, vp2, TX_MIN(n1, n2));
			if (rc == 0) rc = (n1 > n2 ? 1 : (n1 < n2 ? -1 : 0));
			switch (op)
			{
			case FOP_EQ:
				rc = fld2finv(f3, rc == 0);
				break;
			case FOP_NEQ:
				rc = fld2finv(f3, rc != 0);
				break;
			case FOP_LT:
				rc = fld2finv(f3, rc < 0);
				break;
			case FOP_LTE:
				rc = fld2finv(f3, rc <= 0);
				break;
			case FOP_GT:
				rc = fld2finv(f3, rc > 0);
				break;
			case FOP_GTE:
				rc = fld2finv(f3, rc >= 0);
				break;
			case FOP_COM:
				rc = fld2finv(f3, rc);
				break;
			}
			break;
		default:
			rc = FOP_EINVAL;
		}
	}
	else					/* !(n1 > 1 || var1) */
	{
		if (!(op & FOP_CMP))
		{
			TXmakesimfield(f1, f3);
			vp3 = (ft_byte *) getfld(f3, &n3);
		}
		switch (op)
		{
		case FOP_ADD:
			*vp3 = *vp1 + *vp2;
			break;
		case FOP_SUB:
			*vp3 = *vp1 - *vp2;
			break;
		case FOP_MUL:
			*vp3 = *vp1 * *vp2;
			break;
		case FOP_DIV:
			if (*vp2 == (ft_byte) 0)
			{
				/* Bug 6914: */
				TXfldSetNull(f3);
				rc = FOP_EDOMAIN;
			}
			else
				*vp3 = *vp1 / *vp2;
			break;
#     ifndef fonomodulo
		case FOP_MOD:
			if (*vp2 == (ft_byte) 0)
			{
				TXfldSetNull(f3);	/* Bug 6914 */
				rc = FOP_EDOMAIN;
			}
			else
				*vp3 = *vp1 % *vp2;
			break;
#     endif
		case FOP_CNV:
			fobyby(f2, f1, f3, FOP_ASN);
			break;
		case FOP_ASN:
			*vp3 = *vp2;
			break;
		case FOP_EQ:
			rc = fld2finv(f3, *vp1 == *vp2);
			break;
		case FOP_NEQ:
			rc = fld2finv(f3, *vp1 != *vp2);
			break;
		case FOP_LT:
			rc = fld2finv(f3, *vp1 < *vp2);
			break;
		case FOP_LTE:
			rc = fld2finv(f3, *vp1 <= *vp2);
			break;
		case FOP_GT:
			rc = fld2finv(f3, *vp1 > *vp2);
			break;
		case FOP_GTE:
			rc = fld2finv(f3, *vp1 >= *vp2);
			break;
		case FOP_COM:
			rc = fld2finv(f3, *vp1 - *vp2);
			break;
		default:
			rc = FOP_EINVAL;
		}
	}
	return (rc);
}				/* end foxxxx() */

/**********************************************************************/

/**********************************************************************/
/* char char */
/**********************************************************************/

/**********************************************************************/
int
fochch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	static CONST char	fn[] = "fochch";
	ft_char *vp1, *vp2, *s, *e;
	ft_strlst	*sl3;
	size_t n1, n2, n;
	int rc = 0, i;
	byte	byteUsed[256];

	if (op & FOP_CMP)
	{
		rc = stringcmp(f1, f2, f3, op);
	}
	else
	{
		switch (op)
		{
		case FOP_ADD:
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				return(TXfldmathReturnNull(f1, f3));
			copyfld(f3, f1);
			rc = varcat(f3, f2);
			break;
		case FOP_SUB:
			copyfld(f3, f1);
			rc = stringcut(f3, f2);
			break;
		case FOP_CNV:
			if (fldisvar(f2))
			{
				copyfld(f3, f1);
				f3->type |= DDVARBIT;
				if (!TXfldIsNull(f3))
				{
					f3->n = strlen(f3->v);
					f3->size = f3->n;
				}
				/* else preserve f3->n if NULL */
			}
			else
			{
				char *v;

				f3->type = f2->type;
				f3->elsz = f2->elsz;
				vp1 = (ft_char *) getfld(f1, &n1);
				n = MIN(f2->size, n1);
				if (TXfldIsNull(f1))
				{
					TXfldSetNull(f3);
					break;
				}
				v = malloc(f2->size + 1);
				setfld(f3, v, n);
				f3->size = f2->size;
				f3->n = f2->n;
				memcpy(v, vp1, n * sizeof(ft_char));
				memset(v + n, '\0', (f2->size + 1) - n);	/* zero pad */
			}
			break;
		case FOP_ASN:
			if (fldisvar(f1))
			{
				copyfld(f3, f2);
				f3->type |= DDVARBIT;
				f3->n = (f3->v ? strlen(f3->v) : 0);
				f3->size = f3->n;
			}
			else
			{
				char *v;

				f3->type = f1->type;
				f3->elsz = f1->elsz;
				vp2 = (ft_char *) getfld(f2, &n2);
				n = MIN(f1->size, n2);
				v = malloc(f1->size + 1);
				setfld(f3, v, f1->size);
				f3->size = f1->size;
				f3->n = f1->n;
				memcpy(v, vp2, n * sizeof(ft_char));
				memset(v + n, '\0', (f1->size + 1) - n);	/* zero pad */
			}
			break;
		case FOP_INTERSECT:
			vp1 = (ft_char *)getfld(f1, &n1);
			vp2 = (ft_char *)getfld(f2, &n2);
			/* If the strings do not match, the intersection
			 * is the empty set -- which we must represent
			 * with strlst (empty set is different from NULL,
			 * and NULL is not supported yet anyway).  But we
			 * must always return the same type (for
			 * column/retoptype() consistency); so always use
			 * strlst, even if strings match.
			 * Note that Bug 3677 #12 issue (empty varchar
			 * treated as empty-strlst, not one-empty-string
			 * strlst) does not need to be checked, as both
			 * sides are varchar: whichever way we define
			 * empty-varchar-in-a-set-context, the other side
			 * has to have the same definition:
			 */
			if (TXstringcompare(vp1, vp2, n1, n2) == 0)
			{			/* strings match */
				/* 1-item strlst (`vp1'; favor LHS): */
				n = TX_STRLST_MINSZ + n1 + 2;
				if (n < sizeof(ft_strlst))
					n = sizeof(ft_strlst);
				sl3 = (ft_strlst *)TXmalloc(NULL, fn, n + 1);
				if (!sl3)
				{
					rc = FOP_ENOMEM;
					break;
				}
				memset(byteUsed, 0, sizeof(byteUsed));
				e = vp1 + n1;
				for (s = vp1; s < e; s++)
					byteUsed[*(byte *)s] = 1;
				for (i = 0; i < 256 &&
				   byteUsed[(byte)TxPrefStrlstDelims[i]]; i++);
				sl3->nb = n1 + 2;
				sl3->delim = (i < 256 ? TxPrefStrlstDelims[i]
					      : '\0');
				memcpy(sl3->buf, vp1, n1);
				sl3->buf[n1] = '\0';	/* item term. */
				sl3->buf[n1 + 1] = '\0';   /* strlst term. */
			}
			else			/* strings do not match */
			{
				/* 0-item strlst: */
				n = TX_STRLST_MINSZ + 1; /* +1: strlst term.*/
				if (n < sizeof(ft_strlst))
					n = sizeof(ft_strlst);
				sl3 = (ft_strlst *)TXmalloc(NULL, fn, n + 1);
				if (!sl3)
				{
					rc = FOP_ENOMEM;
					break;
				}
				sl3->nb = 1;	/* 1: strlst term. */
				sl3->delim = TxPrefStrlstDelims[0];;
				sl3->buf[0] = '\0';	/* strlst term. */
			}
			((char *)sl3)[n] = '\0';	/* FLD term. */
			releasefld(f3);
			f3->type = (DDVARBIT | FTN_STRLST);
			f3->elsz = 1;
			setfldandsize(f3, sl3, n + 1, FLD_FORCE_NORMAL);
			break;
		default:
			rc = FOP_EINVAL;
		}
	}
	return (rc);
}				/* end fochch() */

/**********************************************************************/

/**********************************************************************/
/* double double */
/**********************************************************************/

/* floating-point ops may be unsafe for NaN etc.: */
#undef ISEQ
#define ISEQ(a, b)      TXDOUBLE_ISEQ(a, b)
#undef ISLT
#define ISLT(a, b)      TXDOUBLE_ISLT(a, b)
#undef ISLTE
#define ISLTE(a, b)     TXDOUBLE_ISLTE(a, b)
#undef ISGT
#define ISGT(a, b)      TXDOUBLE_ISGT(a, b)
#undef ISGTE
#define ISGTE(a, b)     TXDOUBLE_ISGTE(a, b)
#undef COM
/* See txtypes.h notes on TXDOUBLE_CMP(): */
#define COM(a, b)       TXDOUBLE_CMP(a, b)

#undef foxxxx
#undef ft_xx
#define foxxxx fododo
#define ft_xx ft_double
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop1.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* float float */
/**********************************************************************/

/* floating-point ops may be unsafe for NaN etc.: */
#undef ISEQ
#define ISEQ(a, b)      TXFLOAT_ISEQ(a, b)
#undef ISLT
#define ISLT(a, b)      TXFLOAT_ISLT(a, b)
#undef ISLTE
#define ISLTE(a, b)     TXFLOAT_ISLTE(a, b)
#undef ISGT
#define ISGT(a, b)      TXFLOAT_ISGT(a, b)
#undef ISGTE
#define ISGTE(a, b)     TXFLOAT_ISGTE(a, b)
/* See txtypes.h notes on TXFLOAT_CMP(): */
#undef COM
#define COM(a, b)       TXFLOAT_CMP(a, b)

#undef foxxxx
#undef ft_xx
#define foxxxx foflfl
#define ft_xx ft_float
#define fonomodulo
#include "fldop1.c"
#undef fonomodulo

/**********************************************************************/
/* back to defaults for most remaining fldop[12].c usage: */
#undef ISEQ
#define ISEQ(a, b)      ((a) == (b))
#undef ISLT
#define ISLT(a, b)      ((a) < (b))
#undef ISLTE
#define ISLTE(a, b)     ((a) <= (b))
#undef ISGT
#define ISGT(a, b)      ((a) > (b))
#undef ISGTE
#define ISGTE(a, b)     ((a) >= (b))
#undef COM
#define COM(a, b)       ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0))

/**********************************************************************/
/* dword dword */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx fodwdw
#define ft_xx ft_dword
#include "fldop1.c"

/**********************************************************************/
/* int int */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx foinin
#define ft_xx ft_int
#include "fldop1.c"

/**********************************************************************/
/* integer integer */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx foirir
#define ft_xx ft_integer
#include "fldop1.c"

/**********************************************************************/
/* long long */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx fololo
#define ft_xx ft_long
#include "fldop1.c"

/**********************************************************************/
/* short short */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx foshsh
#define ft_xx ft_short
#include "fldop1.c"

/**********************************************************************/
/* smallint smallint */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx fosmsm
#define ft_xx ft_smallint
#include "fldop1.c"

/**********************************************************************/
/* word word */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx fowowo
#define ft_xx ft_word
#include "fldop1.c"

/**********************************************************************/
/* int64 int64 */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#undef GCC_ALIGN_FIX
#if !defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE)
#  define GCC_ALIGN_FIX 1
#endif
#define foxxxx foi6i6
#define ft_xx ft_int64
#include "fldop1.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* uint64 uint64 */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
#define foxxxx fou6u6
#define ft_xx ft_uint64
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#undef GCC_ALIGN_FIX
#if !defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE)
#  define GCC_ALIGN_FIX 1
#endif
#include "fldop1.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* date date */
/**********************************************************************/
#ifdef NEVER
#undef foxxxx
#undef ft_xx
#define foxxxx fodada
#define ft_xx ft_date
#include "fldop1.c"
#else
#include "dateops.c"
#endif

/**********************************************************************/
/* handle handle */
/**********************************************************************/
#undef foxxxx
#undef ft_xx
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#undef GCC_ALIGN_FIX
#if TX_FT_HANDLE_BITS >= 64 && (!defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE))
#  define GCC_ALIGN_FIX 1
#endif
#define foxxxx fohaha
#define ft_xx ft_handle
#include "fldop1.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* long int */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foloin
#define ft_xx  ft_long
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2long
#include "fldop2.c"

/**********************************************************************/
/* long integer */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foloir
#define ft_xx  ft_long
#define ft_yy  ft_integer
#define demote fld2integer
#define promote fld2long
#include "fldop2.c"

/**********************************************************************/
/* int short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foinsh
#define ft_xx  ft_int
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2int
#include "fldop2.c"

/**********************************************************************/
/* int smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foinsm
#define ft_xx  ft_int
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2int
#include "fldop2.c"

/**********************************************************************/
/* dword short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodwsh
#define ft_xx  ft_dword
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2dword
#include "fldop2.c"

/**********************************************************************/
/* dword smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodwsm
#define ft_xx  ft_dword
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2dword
#include "fldop2.c"

/**********************************************************************/
/* long short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy folosh
#define ft_xx  ft_long
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2long
#include "fldop2.c"

/**********************************************************************/
/* long smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy folosm
#define ft_xx  ft_long
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2long
#include "fldop2.c"

/**********************************************************************/
/* long word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy folowo
#define ft_xx  ft_long
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2long
#include "fldop2.c"

/**********************************************************************/
/* dword word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodwwo
#define ft_xx  ft_dword
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2dword
#include "fldop2.c"

/**********************************************************************/
/* short int */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshin
#define foxxxx foinin
#define promote fld2int
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint int */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmin
#define foxxxx foinin
#define promote fld2int
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* short long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshlo
#define foxxxx fololo
#define promote fld2long
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmlo
#define foxxxx fololo
#define promote fld2long
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* short dword */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshdw
#define foxxxx fodwdw
#define promote fld2dword
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint dword */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmdw
#define foxxxx fodwdw
#define promote fld2dword
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* short float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshfl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmfl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* short double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshdo
#define foxxxx fododo
#define promote fld2double
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmdo
#define foxxxx fododo
#define promote fld2double
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* int long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foinlo
#define foxxxx fololo
#define promote fld2long
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* integer long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foirlo
#define foxxxx fololo
#define promote fld2long
#define demote fld2integer
#define foyyyy foirir
#include "fldop3.c"

/**********************************************************************/
/* dword long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwlo
#define foxxxx fololo
#define promote fld2long
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* word int */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowoin
#define foxxxx foinin
#define promote fld2int
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* word long */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowolo
#define foxxxx fololo
#define promote fld2long
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* word dword */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowodw
#define foxxxx fodwdw
#define promote fld2dword
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* long dword */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx folodw
#define foxxxx fodwdw
#define promote fld2dword
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* int word */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foinwo
#define foxxxx fowowo
#define promote fld2word
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* dword int */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwin
#define foxxxx foinin
#define promote fld2int
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* int dword */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foindw
#define foxxxx fodwdw
#define promote fld2dword
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* float double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fofldo
#define foxxxx fododo
#define promote fld2double
#define demote fld2float
#define foyyyy foflfl
#include "fldop3.c"

/**********************************************************************/
/* long double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx folodo
#define foxxxx fododo
#define promote fld2double
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* word float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowofl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* word double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowodo
#define foxxxx fododo
#define promote fld2double
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* dword double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwdo
#define foxxxx fododo
#define promote fld2double
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* int double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foindo
#define foxxxx fododo
#define promote fld2double
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* long float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx folofl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* dword float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwfl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* int float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foinfl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/****************************************************************************/
/* floating-point ops may be unsafe for NaN etc.: */
#undef ISEQ
#define ISEQ(a, b)      (TXDOUBLE_IS_NaN(a) ? 0 : (a) == (b))
#undef ISLT
#define ISLT(a, b)      (TXDOUBLE_IS_NaN(a) ? 0 : (a) < (b))
#undef ISLTE
#define ISLTE(a, b)     (TXDOUBLE_IS_NaN(a) ? 0 : (a) <= (b))
#undef ISGT
#define ISGT(a, b)      (TXDOUBLE_IS_NaN(a) ? 0 : (a) > (b))
#undef ISGTE
#define ISGTE(a, b)     (TXDOUBLE_IS_NaN(a) ? 0 : (a) >= (b))
/* see comment above for COM(): */
#undef COM
#define COM(a, b)       (TXDOUBLE_IS_NaN(a) ? 1 :       \
                         ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0)))

/**********************************************************************/
/* double long */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodolo
#define ft_xx  ft_double
#define ft_yy  ft_long
#define demote fld2long
#define promote fld2double
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* double word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodowo
#define ft_xx  ft_double
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* double dword */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fododw
#define ft_xx  ft_double
#define ft_yy  ft_dword
#define demote fld2dword
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* double int  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodoin
#define ft_xx  ft_double
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* double short  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodosh
#define ft_xx  ft_double
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* double smallint  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodosm
#define ft_xx  ft_double
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* double int64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodoi6
#define ft_xx  ft_double
#define ft_yy  ft_int64
#define demote fld2int64
#define promote fld2double
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* double handle */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodoha
#define ft_xx  ft_double
#define ft_yy  ft_handle
#define demote fld2handle
#define promote fld2double
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* double float */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodofl
#define ft_xx  ft_double
#define ft_yy  ft_float
#define demote fld2float
#define promote fld2double
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/****************************************************************************/
/* floating-point ops may be unsafe for NaN etc.: */
#undef ISEQ
#define ISEQ(a, b)      (TXFLOAT_IS_NaN(a) ? 0 : (a) == (b))
#undef ISLT
#define ISLT(a, b)      (TXFLOAT_IS_NaN(a) ? 0 : (a) < (b))
#undef ISLTE
#define ISLTE(a, b)     (TXFLOAT_IS_NaN(a) ? 0 : (a) <= (b))
#undef ISGT
#define ISGT(a, b)      (TXFLOAT_IS_NaN(a) ? 0 : (a) > (b))
#undef ISGTE
#define ISGTE(a, b)     (TXFLOAT_IS_NaN(a) ? 0 : (a) >= (b))
/* see comment above for COM(): */
#undef COM
#define COM(a, b)       (TXFLOAT_IS_NaN(a) ? 1 :        \
                         ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0)))

/**********************************************************************/
/* float long */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fofllo
#define ft_xx  ft_float
#define ft_yy  ft_long
#define demote fld2long
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float dword */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fofldw
#define ft_xx  ft_float
#define ft_yy  ft_dword
#define demote fld2dword
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflwo
#define ft_xx  ft_float
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float short  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflsh
#define ft_xx  ft_float
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float smallint  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflsm
#define ft_xx  ft_float
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float int  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflin
#define ft_xx  ft_float
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2float
#define fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* float int64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fofli6
#define ft_xx  ft_float
#define ft_yy  ft_int64
#define demote fld2int64
#define promote fld2float
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* float handle */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflha
#define ft_xx  ft_float
#define ft_yy  ft_handle
#define demote fld2handle
#define promote fld2float
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* back to defaults for most remaining fldop[12].c usage: */
#undef ISEQ
#define ISEQ(a, b)      ((a) == (b))
#undef ISLT
#define ISLT(a, b)      ((a) < (b))
#undef ISLTE
#define ISLTE(a, b)     ((a) <= (b))
#undef ISGT
#define ISGT(a, b)      ((a) > (b))
#undef ISGTE
#define ISGTE(a, b)     ((a) >= (b))
#undef COM
#define COM(a, b)       ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0))

/**********************************************************************/
/* int64 int  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6in
#define ft_xx  ft_int64
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 long  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6lo
#define ft_xx  ft_int64
#define ft_yy  ft_long
#define demote fld2long
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6wo
#define ft_xx  ft_int64
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 dword */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6dw
#define ft_xx  ft_int64
#define ft_yy  ft_dword
#define demote fld2dword
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6sh
#define ft_xx  ft_int64
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foi6sm
#define ft_xx  ft_int64
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2int64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* int64 double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foi6do
#define foxxxx fododo
#define promote fld2double
#define demote fld2int64
#define foyyyy foi6i6
#include "fldop3.c"

/**********************************************************************/
/* int64 float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foi6fl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2int64
#define foyyyy foi6i6
#include "fldop3.c"

/**********************************************************************/
/* int64 uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foi6u6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2int64
#define foyyyy foi6i6
#include "fldop3.c"

/* ------------------------------------------------------------------------ */

/**********************************************************************/
/* handle int  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fohain
#define ft_xx  ft_handle
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2handle
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* handle long  */
/**********************************************************************/
#if TX_FT_HANDLE_BITS >= EPI_OS_LONG_BITS
#  undef foxxyy
#  undef ft_xx
#  undef ft_yy
#  undef demote
#  undef promote
#  undef fonomodulo
#  undef GCC_ALIGN_FIX
#  define foxxyy fohalo
#  define ft_xx  ft_handle
#  define ft_yy  ft_long
#  define demote fld2long
#  define promote fld2handle
#  include "fldop2.c"
#else /* TX_FT_HANDLE_BITS < EPI_OS_LONG_BITS */
#  error Need handle/long function (fldop3.c)
#endif /* TX_FT_HANDLE_BITS < EPI_OS_LONG_BITS */

/**********************************************************************/
/* handle word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fohawo
#define ft_xx  ft_handle
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2handle
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* handle dword */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fohadw
#define ft_xx  ft_handle
#define ft_yy  ft_dword
#define demote fld2dword
#define promote fld2handle
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* handle short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fohash
#define ft_xx  ft_handle
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2handle
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* handle smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fohasm
#define ft_xx  ft_handle
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2handle
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* handle double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fohado
#define foxxxx fododo
#define promote fld2double
#define demote fld2handle
#define foyyyy fohaha
#include "fldop3.c"

/**********************************************************************/
/* handle float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fohafl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2handle
#define foyyyy fohaha
#include "fldop3.c"

/**********************************************************************/
/* handle int64  */
/**********************************************************************/
#if TX_FT_HANDLE_BITS <= 64
#  undef foyyxx
#  undef foyyyy
#  undef foxxxx
#  undef promote
#  undef demote
#  define foyyxx fohai6
#  define foyyyy fohaha
#  define foxxxx foi6i6
#  define promote fld2int64
#  define demote fld2handle
#  include "fldop3.c"
#else
#  error Need handler (fldop2.c)
#endif

/**********************************************************************/
/* handle uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fohau6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2handle
#define foyyyy fohaha
#include "fldop3.c"

/* ------------------------------------------------------------------------ */

#undef EXTRAVARS
#define EXTRAVARS	int	errnum;

/**********************************************************************/
/* char int64 */
/* int64 char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#undef GCC_ALIGN_FIX
#define fochxx     fochi6
#define foxxch     foi6ch
#define ft_xx      ft_int64
#define bpxx       ((sizeof(ft_int64)*8/3)+2)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%wd",(EPI_HUGEINT)(c)) /* fmt to buf */
/* use TXstrtoi64() for size (== ft_xx) */
#define cvtxx(s,vp) (*(vp) = TXstrtoi64((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		     (e > (char *)(s) && errnum == 0))
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#if !defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE)
#  define GCC_ALIGN_FIX 1
#endif
#include "fldop4.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* char handle */
/* handle char */
/* NOTE: see also fochre(), forech(), fldtostr() */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#undef GCC_ALIGN_FIX
#define fochxx     fochha
#define foxxch     fohach
#define ft_xx      ft_handle
#define bpxx       ((sizeof(ft_handle)*8/3)+10)	/* guess fmt'd bytes per xx */
/* We want the correct number of leading `ffff...'
 * digits for negative off_t, so pick exact-size type for `fmtxx()':
 */
#if TX_FT_HANDLE_BITS == EPI_HUGEINT_BITS
#  define fmtxx(a,b,c) htsnpf((a),(b),"0x%08wx",(EPI_HUGEINT)(c))
#elif TX_FT_HANDLE_BITS == EPI_OS_INT_BITS
#  define fmtxx(a,b,c) htsnpf((a),(b),"0x%08x",(int)(c))
#else
#  error Need htpf code and C type exactly matching ft_handle
#endif
/* use TXstrtoepioff_t for size (== ft_xx): */
#define cvtxx(s,vp) (*(vp) = TXstrtoepioff_t((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		     (e > (char*)(s) && errnum == 0))
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#if TX_FT_HANDLE_BITS >= 64 && (!defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE))
#  define GCC_ALIGN_FIX 1
#endif
#include "fldop4.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* int64 byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy foi6by
#define ft_xx ft_int64
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"

/**********************************************************************/
/* int int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foini6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* long int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foloi6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* word int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowoi6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* dword int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwi6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* short int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshi6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmi6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* byte int64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fobyi6
#define ft_xx ft_byte
#define ft_yy ft_int64
#define mote  fld2int64
#include "fldop5.c"

/**********************************************************************/
/* uint64 int  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6in
#define ft_xx  ft_uint64
#define ft_yy  ft_int
#define demote fld2int
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 long  */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6lo
#define ft_xx  ft_uint64
#define ft_yy  ft_long
#define demote fld2long
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 word */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6wo
#define ft_xx  ft_uint64
#define ft_yy  ft_word
#define demote fld2word
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 dword */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6dw
#define ft_xx  ft_uint64
#define ft_yy  ft_dword
#define demote fld2dword
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 short */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6sh
#define ft_xx  ft_uint64
#define ft_yy  ft_short
#define demote fld2short
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 smallint */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fou6sm
#define ft_xx  ft_uint64
#define ft_yy  ft_smallint
#define demote fld2smallint
#define promote fld2uint64
#undef fonomodulo
#include "fldop2.c"
#undef fonomodulo

/**********************************************************************/
/* uint64 double */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fou6do
#define foxxxx fododo
#define promote fld2double
#define demote fld2uint64
#define foyyyy fou6u6
#include "fldop3.c"

/**********************************************************************/
/* uint64 float */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fou6fl
#define foxxxx foflfl
#define promote fld2float
#define demote fld2uint64
#define foyyyy fou6u6
#include "fldop3.c"

/**********************************************************************/
/* uint64 int64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fou6i6
#define foxxxx foi6i6
#define promote fld2int64
#define demote fld2uint64
#define foyyyy fou6u6
#include "fldop3.c"

/**********************************************************************/
/* char uint64 */
/* uint64 char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#undef GCC_ALIGN_FIX
#define fochxx     fochu6
#define foxxch     fou6ch
#define ft_xx      ft_uint64
#define bpxx       ((sizeof(ft_uint64)*8/3)+2)  /* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%wu",(EPI_HUGEUINT)(c)) /* fmt to buf */
/* use TXstrtoui64() for size (== ft_xx): */
#define cvtxx(s,vp)  (*(vp) = TXstrtoui64((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		      (e > (char *)(s) && errnum == 0))
/* wtf assume fields are alloced at least to TX_ALIGN_BYTES boundary: */
#if !defined(EPI_TXALIGN_INT64_COPY_SAFE) || !defined(EPI_TXALIGN_INT64_CMP_SAFE)
#  define GCC_ALIGN_FIX 1
#endif
#include "fldop4.c"
#undef GCC_ALIGN_FIX

/**********************************************************************/
/* uint64 byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fou6by
#define ft_xx ft_uint64
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"

/**********************************************************************/
/* int uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foinu6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* long uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx folou6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* word uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowou6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* dword uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwu6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* short uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshu6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint uint64 */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmu6
#define foxxxx fou6u6
#define promote fld2uint64
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* double uint64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy fodou6
#define ft_xx  ft_double
#define ft_yy  ft_uint64
#define demote fld2uint64
#define promote fld2double
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* float uint64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef demote
#undef promote
#define foxxyy foflu6
#define ft_xx  ft_float
#define ft_yy  ft_uint64
#define demote fld2uint64
#define promote fld2float
#define fonomodulo
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop2.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif
#undef fonomodulo

/**********************************************************************/
/* byte uint64 */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fobyu6
#define ft_xx ft_byte
#define ft_yy ft_uint64
#define mote  fld2uint64
#include "fldop5.c"

/* ------------------------------------------------------------------------ */

/**********************************************************************/
/* handle byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fohaby
#define ft_xx ft_handle
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"

/**********************************************************************/
/* int handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foinha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2int
#define foyyyy foinin
#include "fldop3.c"

/**********************************************************************/
/* long handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foloha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2long
#define foyyyy fololo
#include "fldop3.c"

/**********************************************************************/
/* word handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fowoha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2word
#define foyyyy fowowo
#include "fldop3.c"

/**********************************************************************/
/* dword handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fodwha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2dword
#define foyyyy fodwdw
#include "fldop3.c"

/**********************************************************************/
/* short handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx foshha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2short
#define foyyyy foshsh
#include "fldop3.c"

/**********************************************************************/
/* smallint handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fosmha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2smallint
#define foyyyy fosmsm
#include "fldop3.c"

/**********************************************************************/
/* uint64 handle */
/**********************************************************************/
#undef foyyxx
#undef foxxxx
#undef promote
#undef demote
#undef foyyyy
#define foyyxx fou6ha
#define foxxxx fohaha
#define promote fld2handle
#define demote fld2uint64
#define foyyyy fou6u6
#include "fldop3.c"

/**********************************************************************/
/* byte handle */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fobyha
#define ft_xx ft_byte
#define ft_yy ft_handle
#define mote  fld2handle
#include "fldop5.c"

/* ------------------------------------------------------------------------ */

/**********************************************************************/
/* char int */
/* int char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochin
#define foxxch     foinch
#define ft_xx      ft_int
#define bpxx       ((sizeof(ft_int)*8/3)+2)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%ld",(long)(c))	/* fmt to buf */
/* use TXstrtoi() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtoi((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char long */
/* long char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochlo
#define foxxch     foloch
#define ft_xx      ft_long
#define bpxx       ((sizeof(ft_long)*8/3)+2)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%ld",(long)(c))	/* fmt to buf */
/* use TXstrtol() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtol((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char dword */
/* dword char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochdw
#define foxxch     fodwch
#define ft_xx      ft_dword
#define bpxx       (sizeof(ft_dword)*8/3+1)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%lu",(unsigned long)(c))/* fmt to buf */
/* use TXstrtodw() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtodw((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char word */
/* word char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochwo
#define foxxch     fowoch
#define ft_xx      ft_word
#define bpxx       (sizeof(ft_word)*8/3+1)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%u",(unsigned)(c))	/* fmt to buf */
/* use TXstrtow() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtow((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char short */
/* short char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochsh
#define foxxch     foshch
#define ft_xx      ft_short
#define bpxx       (sizeof(ft_short)*8/3+2)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%d",(int)(c))	/* fmt to buf */
/* use TXstrtos() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtos((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char smallint */
/* smallint char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochsm
#define foxxch     fosmch
#define ft_xx      ft_smallint
#define bpxx       (sizeof(ft_smallint)*8/3+2)	/* guess bytes per xx */
#define fmtxx(a,b,c) htsnpf((a),(b),"%d",(int)(c))	/* fmt to buf */
/* use TXstrtos() for size (== ft_xx): */
#define cvtxx(s, vp)  (*(vp) = TXstrtos((s), CHARPN, &e, (0 | TXstrtointFlag_NoLeadZeroOctal), &errnum), \
		       (e > (char *)(s) && errnum == 0))
#include "fldop4.c"

/**********************************************************************/
/* char float */
/* float char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochfl
#define foxxch     foflch
#define ft_xx      ft_float
#define bpxx       15		/* guess at max bytes per xx */
#define fmtxx(a,b,c) htsnpf((a), (b), "%g", (float)(c))	/* fmt to buf */
/* use TXstrtod() to handle Inf/-Inf/NaN on some platforms: */
/* use TXdouble2float() to handle FPE on some platforms: */
#undef EXTRAVARS
#define EXTRAVARS       double dV; float fV; int errnum;
#define cvtxx(s, vp)    (dV = TXstrtod((s), CHARPN, &e, &errnum),	\
       TXdouble2float(&dV, &fV), *(vp) = fV, (e > (char *)(s) && errnum == 0))
#include "fldop4.c"
#undef EXTRAVARS
#define EXTRAVARS	int errnum;

/**********************************************************************/
/* char double */
/* double char */
/**********************************************************************/
#undef fochxx
#undef foxxch
#undef ft_xx
#undef bpxx
#undef fmtxx
#undef cvtxx
#define fochxx     fochdo
#define foxxch     fodoch
#define ft_xx      ft_double
#define bpxx       15		/* guess bytes per xx when formatted */
#define fmtxx(a,b,c) htsnpf((a),(b),"%lg",(double)(c))	/* fmt to buf */
/* use TXstrtod() to handle Inf/-Inf/NaN on some platforms: */
#define cvtxx(s, vp)  (*(vp) = TXstrtod((s), CHARPN, &e, &errnum), \
		       (e > (char *)(s) && errnum == 0))
#ifdef GCC_DOUBLE_FIX
#  define GCC_ALIGN_FIX
#endif
#include "fldop4.c"
#ifdef GCC_DOUBLE_FIX
#  undef GCC_ALIGN_FIX
#endif

/**********************************************************************/
/* byte int */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fobyin
#define ft_xx ft_byte
#define ft_yy ft_int
#define mote  fld2int
#include "fldop5.c"

/**********************************************************************/
/* int byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy foinby
#define ft_xx ft_int
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"

/**********************************************************************/
/* byte long */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fobylo
#define ft_xx ft_byte
#define ft_yy ft_long
#define mote  fld2long
#include "fldop5.c"

/**********************************************************************/
/* long byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy foloby
#define ft_xx ft_long
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"

/**********************************************************************/
/* dword byte */
/**********************************************************************/
#undef foxxyy
#undef ft_xx
#undef ft_yy
#undef mote
#define foxxyy fodwby
#define ft_xx ft_dword
#define ft_yy ft_byte
#define mote  fld2byte
#include "fldop5.c"
