/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"

#define NO_QUICK_FLDCMP
#undef NO_QUICK_FLDCMP

/************************************************************************/
/* returns the address of field[n] or null if no field */

FLD *
TXgetrfldn(tb,n,orderFlags)
TBL *tb;	/* The table to look in */
int n;		/* Field number (internal, not user-added, order) */
TXOF	*orderFlags;	/* (out, opt.) Are we looking at a reversed field? */
{
	int nfields;

	nfields=tb->n;
	if(n>=nfields || n<0 )
		return((FLD *)NULL);
	if (orderFlags)
		*orderFlags = (TXOF)ddgetorder(tb->dd, n);
	return(tb->field[n]);
}

/************************************************************************/
/* returns the address of the nth field inserted or null if no field */

FLD *
getfldn(tb,n,orderFlags)
TBL *tb;	/* The table to look in */
int n;		/* Field number (user-added order, not internal order) */
TXOF	*orderFlags;	/* (out, opt.) Are we looking at a reversed field? */
{
	int	i;

	i = tbgetorign(tb, n);		/* map user `n' to internal `i' */
	return TXgetrfldn(tb, i, orderFlags);
}

/******************************************************************/

static FLDOP *tempfo = NULL;

void
closetmpfo()
{
	if(tempfo)
		tempfo = foclose(tempfo);
}

FLDCMP *
TXopenfldcmp(BTREE *bt, FLDOP *fo)
/* Creates a FLDCMP.  Uses and owns `fo' if given (even on error),
 * else creates one if TXOPENFLDCMP_CREATE_FLDOP given, else uses
 * shared internal FLDOP if TXOPENFLDCMP_INTERNAL_FLDOP given,
 * else set to NULL.  Creates TBLs from `bt' if given, else none.
 * Returns FLDCMP, or NULL on error.
 */
{
	FLDCMP *rc = NULL;

	rc = (FLDCMP *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(FLDCMP));
	if (!rc) goto err;
	if (fo == TXOPENFLDCMP_CREATE_FLDOP && !(fo = dbgetfo())) goto err;
	else if (fo == TXOPENFLDCMP_INTERNAL_FLDOP)
	{					/* use our internal FLDOP */
		if (!tempfo) tempfo = dbgetfo();
		if (!tempfo) goto err;
		fo = tempfo;
	}
	rc->fo = fo;
	fo = NULL;				/* `rc' owns it */
	if (bt)
	{
		rc->tbl1 = createtbl(btreegetdd(bt), NULL);
		rc->tbl2 = createtbl(btreegetdd(bt), NULL);
		if (!rc->tbl1 || !rc->tbl2) goto err;
	}
	goto finally;

err:
	rc = TXclosefldcmp(rc);
	if (fo != tempfo) fo = foclose(fo);	/* we own it even on error */
finally:
	return(rc);
}

/******************************************************************/

FLDCMP *
TXclosefldcmp(FLDCMP *fc)
{
	if (fc)
	{
		if (fc->fo && fc->fo != tempfo)
			fc->fo = foclose(fc->fo);
		if (fc->tbl1)
			fc->tbl1 = closetbl(fc->tbl1);
		if (fc->tbl2)
			fc->tbl2 = closetbl(fc->tbl2);
		fc = TXfree(fc);
	}
	return NULL;
}

/******************************************************************/

int
TXfldCmpSameType(f1, f2, status, orderFlags)
FLD	*f1, *f2;
int	*status;	/* (out) 0 on success, -1 on failure */
TXOF	orderFlags;
/* Compares `f1' and `f2', which must be the same type.
 * Returns < 0 if `f1' < `f2'; 0 if equal; -1 if `f1' > `f2'.
 * If fields cannot be handled, sets `*status' to -1, else 0 on success.
 */
{
	int	type, rc;
	size_t	sz;

	*status = 0;

	if (TXfldmathverb >= 1)
		TXfldmathopmsg(f1, f2, FOP_COM, __FUNCTION__);

	if(f1->type != f2->type) goto err;

	type = (f1->type & FTN_VarBaseTypeMask);
	/* Don't care about NULL bit */
	switch(type)
	{
		case FTN_INT:
		{
			ft_int	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_INTEGER:
		{
			ft_integer	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_LONG:
		{
			ft_long	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_SHORT:
		{
			ft_short	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_SMALLINT:
		{
			ft_smallint	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_COUNTER:
		{
			ft_counter	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,TX_CTRCMP(v1, v2));
			else
				rc = TX_CTRCMP(v1, v2);
			goto done;
		}
#ifndef NEVER/* WTF HANDLE Case Insensitive */
		case FTN_CHAR:
		case FTN_CHAR | DDVARBIT:
		{
			CONST ft_char	*v1, *v2;
			TXCFF	mode;

			v1 = getfld(f1, &sz);
			v2 = getfld(f2, &sz);
			TXget_globalcp();
			mode = globalcp->stringcomparemode;
			if (orderFlags & OF_IGN_CASE)
				mode = TXCFF_SUBST_CASESTYLE(mode,
						TXCFF_CASESTYLE_IGNORE);
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				  TXunicodeStrFoldCmp(&v1, -1, &v2, -1, mode));

			else
			     rc = TXunicodeStrFoldCmp(&v1, -1, &v2, -1, mode);
			goto done;
		}
#endif
		case FTN_DWORD:
		{
			ft_dword	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_WORD:
		{
			ft_word	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_INT64:
		{
			ft_int64	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_UINT64:
		{
			ft_uint64	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
			goto done;
		}
		case FTN_FLOAT:
		{
			ft_float	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
						     TXFLOAT_CMP(*v1, *v2));
			else
				rc = TXFLOAT_CMP(*v1, *v2);
			goto done;
		}
		case FTN_DOUBLE:
		{
			ft_double	*v1, *v2, a, b, *ap, *bp;
#ifdef GCC_DOUBLE_FIX
#  ifdef __GNUC__
#    define TMP_DECL(fttype)	\
	fttype	ftTmp; volatile byte *itemSrc, *itemDest;
#  else
#    define TMP_DECL(fttype)	fttype	ftTmp;
#  endif
#  ifdef __GNUC__
	/* gcc tries very hard to optimize this memcpy() into an
	 * inline ft_xx assignment, with possible bus error results on
	 * Sparc.  It appears we need true byte-pointer args to memcpy()
	 * to force a byte-wise copy.  See also FDBI_TXALIGN_RECID_COPY(),
	 * fldop2.c, other GCC_DOUBLE_FIX uses:
	 */
#    define ITEM(fttype, v, i)						\
			(itemSrc = (volatile byte *)&(((fttype *)v)[i]), \
			 itemDest = (volatile byte *)&ftTmp,		\
			 memcpy((byte *)itemDest, (byte *)itemSrc,	\
				sizeof(fttype)), ftTmp)
#  else /* !__GNUC__ */
#    define ITEM(fttype, v, i)						\
		(memcpy(&ftTmp, &(((fttype *)v)[i]), sizeof(fttype)), ftTmp)
#  endif /* !__GNUC__ */
#else /* !GCC_ALIGN_FIX */
#  define TMP_DECL(fttype)
#  define ITEM(fttype, v, i)	(((fttype *)v)[i])
#endif /* !GCC_ALIGN_FIX */
			TMP_DECL(ft_double)

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1))
			{
				a = 0.0;
				ap = NULL;
			}
			else
			{
				a = ITEM(ft_double, v1, 0);
				ap = &a;
			}
			if (TXfldIsNull(f2))
			{
				b = 0.0;
				bp = NULL;
			}
			else
			{
				b = ITEM(ft_double, v2, 0);
				bp = &b;
			}
			rc = TX_FLD_NULL_CMP(ap, bp, TXDOUBLE_CMP(*ap, *bp));
			goto done;
		}
		case FTN_DATE:
		{
			ft_date	*v1, *v2;

			v1 = getfld(f1, &sz);
			if(sz != 1) goto err;
			v2 = getfld(f2, &sz);
			if(sz != 1) goto err;
			if (TXfldIsNull(f1) || TXfldIsNull(f2))
				rc = TX_FLD_NULL_CMP(v1, v2,
				     (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1)));
			else
				rc = (*v1 == *v2 ? 0 : (*v1 > *v2 ? 1 : -1));
		done:
			if (TXfldmathverb >= 2)
				putmsg(MINFO, NULL,
				       "Fldmath op %s=%d result ok value=[%d]",
				       __FUNCTION__, (FOP_COM & 0x7f), rc);
			return(rc);
		}
		case FTN_BYTE:
		case FTN_DECIMAL:
		case FTN_BLOB:
		case FTN_HANDLE:
		case FTN_INDIRECT:
		case FTN_BLOBI:
		case FTN_STRLST:
		case FTN_DATESTAMP:
		case FTN_TIMESTAMP:
		default:
#ifdef NEVER
			putmsg(999, __FUNCTION__, "Unknown type %d", type);
#endif
		err:
			if (TXfldmathverb >= 2)
				putmsg(MINFO, NULL,
			 "Fldmath op %s=%d failed, will probably use FOP_CMP",
				       __FUNCTION__, (FOP_COM & 0x7f));
			*status = -1;
			return 0;
	}
}

/******************************************************************/
/*	Compare two fields using the field math stuff.  Takes a
 *	buffer as would be stored in BTREE.
 *
 *	The usr structure is intended to have a FLDCMP structure
 *	which will contain a table to decode the fields.
 *
 *	Returns -1, 0, 1 if buf1 is less than, equal to or greater
 *	than buf2.
 */

int
fldcmp(buf1, fld1sz, buf2, fld2sz, usr)
void *buf1, *buf2;
size_t fld1sz, fld2sz;
FLDCMP *usr;
{
	FLD *r, *fld1, *fld2;
	FLDOP *fo;
	size_t n;
	int rc, i;
	TXOF	orderFlags;
	TXCFF	oign = (TXCFF)0;
#ifdef NEVER
	char	*y, *x;
#endif

	if(TXverbosity > 1)
		putmsg(MINFO, NULL, "Comparing records");
	fo = usr->fo;
	buftofld(buf1, usr->tbl1, fld1sz);
	buftofld(buf2, usr->tbl2, fld2sz);
	for (i=0; i < (int)usr->tbl1->n; i++)
	{
		fld1 = getfldn(usr->tbl1, i, &orderFlags);
		fld2 = getfldn(usr->tbl2, i, &orderFlags);
		if(orderFlags & OF_DONT_CARE) continue;
#ifndef NO_QUICK_FLDCMP
		if(fld1->type == fld2->type)
		{
			int	status, r;
			r = TXfldCmpSameType(fld1, fld2, &status, orderFlags);
			if(status == 0 && r != 0)
			{
				if(orderFlags & OF_DESCENDING)
					r = r * -1;
				return r;
			}
			if(status == 0 /* && r == 0 */)
			{
				if(orderFlags & OF_PREFER_END)
					return -1;
				if(orderFlags & OF_PREFER_START)
					return 1;
				continue;
			}
		}
#endif
		fopush(fo, fld1);
		fopush(fo, fld2);
#ifndef JMT_COMP
		if(orderFlags & OF_IGN_CASE)
		{
			TXget_globalcp();
			oign = globalcp->stringcomparemode;
			globalcp->stringcomparemode = TXCFF_SUBST_CASESTYLE(
			  globalcp->stringcomparemode, TXCFF_CASESTYLE_IGNORE);
		}
		rc = foop(fo, FOP_COM);
		if(orderFlags & OF_IGN_CASE)
		{
			globalcp->stringcomparemode = oign;
		}
		if(rc == 0)
			r = fopeek(fo);
		else
			r = NULL;
		if (r != (FLD *)NULL)
		{
			rc = (int)*((ft_int *) getfld(r, &n));
			fodisc(fo);
			if(orderFlags & OF_DESCENDING)
				rc = rc * -1;
			if(rc)
				return rc;
			if(orderFlags & OF_PREFER_END)
				return -1;
			if(orderFlags & OF_PREFER_START)
				return 1;
		}
#else
		if (orderFlags & OF_DESCENDING)
			foop(fo, FOP_GT);
		else
			foop(fo, FOP_LT);
		r = fopeek(fo);
		if (r != (FLD *)NULL)
		{
			rc = (int)*((ft_int *) getfld(r, &n));
			fodisc(fo);
			if (rc == 1)
				return -1;
		}
		fopush(fo, fld1);
		fopush(fo, fld2);
		if (orderFlags & OF_DESCENDING)
			foop(fo, FOP_LT);
		else
			foop(fo, FOP_GT);
		r = fopeek(fo);
		if (r != (FLD *)NULL)
		{
			rc = (int)*((ft_int *) getfld(r, &n));
			fodisc(fo);
			if (rc == 1)
				return 1;
		}
#endif
	}
	return 0;
}
