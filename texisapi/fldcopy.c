/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"


#undef shadow /* shadow */

/*	Copy the contents of a field
 *	This function copies the contents of src to dst.  It should also
 *	perform any required conversions between types.
 */

extern EPI_OFF_T bitob ARGS((void *, TBL *));

int
_fldcopy(src, srct, dst, dstt, fo)
FLD	*src;	/* The source field */
TBL	*srct;	/* The table the source field came from */
FLD	*dst;	/* The destination field */
TBL	*dstt;	/* The table for the destination field */
FLDOP	*fo;
/* Returns 0 on success, -1 on error.
 */
{
	TXPMBUF	*pmbuf = TXPMBUFPN;
	void	*v;
	size_t	sz;
	FLD	*fld;
	int	rc = 0;

#ifdef NEVER
	if ((src->type == dst->type) && (dst->alloced > src->n) && (src->elsz == 1))
	{
		DBGMSG(0, (999, NULL, "Quick copy"));
		memcpy(dst->shadow, src->v, src->n + 1);
		setfldv(dst);
		dst->n = src->n;
		dst->size = src->size;
		return rc;
	}
#endif
	if ((TXisblob(src->type)) && ((dst->type & DDTYPEBITS) == FTN_BLOBI))
	{
		v = getfld(src, &sz);
		switch(src->type & DDTYPEBITS)
		{
		case FTN_BLOBZ:
			v = bztobi(*(ft_blob *)v, srct);
			break;
		case FTN_BLOB:
		default: /* Unknown BLOB Type */
			v = btobi(*(ft_blob *)v, srct);
			break;
		}
		setfld(dst, v, sz);
		dst->n = src->n;
		return rc;
	}
	else
	if (((src->type & DDTYPEBITS) == FTN_BLOBI) &&
	    (TXisblob(dst->type)))
	{
		EPI_OFF_T *where;

		where = (EPI_OFF_T *)TXcalloc(pmbuf, __FUNCTION__,
					      1, sizeof(EPI_OFF_T));
		v = getfld(src, &sz);
		switch(dst->type & DDTYPEBITS)
		{
		case FTN_BLOBZ:
			*where = bitobz(v, dstt);
			break;
		case FTN_BLOB:
		default:
			*where = bitob(v, dstt);
		}
		setfld(dst, where, sz);
		dst->n = src->n;
		return rc;
	}
	if (((src->type & DDTYPEBITS) == FTN_BLOBI) &&
	    dst->storage &&
	    !isramtbl(dstt) &&
	    (TXisblob(dst->storage->type)))
	{
		EPI_OFF_T *where;
		FTN	bitype = (FTN)0;

		where = (EPI_OFF_T *)TXcalloc(pmbuf, __FUNCTION__,
					      1, sizeof(EPI_OFF_T));
		v = getfld(src, &sz);
		switch(dst->storage->type & DDTYPEBITS)
		{
		case FTN_BLOBZ:
			*where = bitobz(v, dstt);
			bitype = FTN_BLOBZ;
			break;
		case FTN_BLOB:
		default:
			*where = bitob(v, dstt);
			bitype = FTN_BLOB;
		}
		setfld(dst->storage, where, sizeof(EPI_OFF_T));
		dst->storage->size = sizeof(EPI_OFF_T);
		dst->storage->n = src->n;
		if(!dst->v && ((dst->type & DDTYPEBITS) == FTN_BLOBI))
		{
			ft_blobi	*v;

			v = (ft_blobi *)dst->shadow;
			if (v)
				TXblobiFreeMem(v);
			else
			{
				dst->alloced = sizeof(ft_blobi) + 1;
				dst->shadow = v = TXcalloc(pmbuf, __FUNCTION__,
							   1, dst->alloced);
				if (!v) goto err;
				dst->frees = FREESHADOW;
			}
			v->dbf = dstt->bf;
			v->off = *where;
			v->otype = bitype;
			setfldv(dst);
		}
		else
		{
			ft_blobi	*v;

			v = dst->v;
			if(v)
			{
				TXblobiFreeMem(v);
				v->dbf = dstt->bf;
				v->off = *where;
				v->otype = bitype;
			}
		}
		return rc;
	}
	else
	{
		if(!fldisset(dst))
		{
			if(fldisvar(dst))
			{
				/* Put some fldmath-safe values in.
				 * WTF statics.
				 * NOTE:  See also retopttype():
				 */
				static ft_double	db = 1.0;
				static ft_float		fl = 1.0;
				static ft_strlst	sl = { 0, ',', "" };
				static ft_date		da = (ft_date)3;
				static ft_datetime	dt={2000,1,1,0,0,0,0};

				switch (dst->type & DDTYPEBITS)
				{
				case FTN_DOUBLE:
					putfld(dst, &db, sizeof(ft_double));
					break;
				case FTN_FLOAT:
					putfld(dst, &fl, sizeof(ft_float));
					break;
				case FTN_STRLST:
					putfld(dst, &sl, sizeof(ft_strlst));
					break;
				case FTN_DATE:
					putfld(dst, &da, sizeof(ft_date));
					break;
				case FTN_DATETIME:
					putfld(dst, &dt, sizeof(ft_datetime));
					break;
				default:
					putfld(dst, "", 0);
					break;
				}
			}
			else
			{
				if(dst->n ==0)
					dst->n = dst->size/dst->elsz;
				setfldv(dst);
			}
		}
		/* Can we speed things up ? */
#ifdef NEVER
		if ((src->type == dst->type) && (dst->alloced > src->size))
		{
			DBGMSG(0, (999, NULL, "Quick copy"));
			memcpy(dst->shadow, src->v, src->size + 1);
			setfldv(dst);
			dst->n = src->n;
			dst->size = src->size;
			return rc;
		}
#endif
		fopush(fo, dst);
		fopush(fo, src);
		if(foop(fo, FOP_ASN)<0)
		{
			txpmbuf_putmsg(pmbuf, MERR, "fldcopy",
			       "Could not assign type %s to %s (FTN %d to %d)",
                               TXfldtypestr(src), TXfldtypestr(dst),
			       src->type, dst->type);
			fodisc(fo);
			rc = -1;
			memset(dst->v, 0, dst->alloced);
		}
		else
		{
			FLD *strg=FLDPN;

			fld = fopop(fo);
			TXfreefldshadow(dst);
			if(dst->fldlist)
				dst->fldlist = TXfree(dst->fldlist);
			if(isramtbl(dstt))
				dst->storage = closefld(dst->storage);
			else
				strg=dst->storage;
			memcpy(dst, fld, sizeof(FLD));
			fld = TXfree(fld); /* Just copied contents */
			if(!isramtbl(dstt))
				dst->storage=strg;
			if (((dst->type & DDTYPEBITS) == FTN_BLOBI) &&
			    dst->storage &&
			    !isramtbl(dstt) &&
			    (TXisblob(dst->storage->type)))
			{
				EPI_OFF_T *where;

				where = (EPI_OFF_T *)TXcalloc(pmbuf,
					   __FUNCTION__,1, sizeof(EPI_OFF_T));
				v = getfld(dst, &sz);
				switch(dst->storage->type & DDTYPEBITS)
				{
				case FTN_BLOBZ:
					*where = bitobz(v, dstt);
					break;
				case FTN_BLOB:
				default:
					*where = bitob(v, dstt);
				}
				setfld(dst->storage, where, sizeof(EPI_OFF_T));
				dst->storage->size = sizeof(EPI_OFF_T);
				dst->storage->n = dst->n;
			}
		}
	}
	goto finally;

err:
	rc = -1;
finally:
	return rc;
}

/*	Copy the contents of a field
 *	This function copies the contents of src to dst.  It should also
 *	perform any required conversions between types.
 */

int
fldcopy(src, srct, dst, dstt, fo)
FLD	*src;	/* The source field */
TBL	*srct;	/* The table the source field came from */
FLD	*dst;	/* The destination field */
TBL	*dstt;	/* The table for the destination field */
FLDOP	*fo;
{
	void	*v;
	size_t	sz;

	(void)dstt;
	(void)fo;
	v = getfld(src, &sz);
	if (((src->type & DDTYPEBITS) == FTN_BLOB) &&
	    ((dst->type & DDTYPEBITS) == FTN_BLOBI))
		v = btobi(*(ft_blob *)v, srct);
	if (((src->type & DDTYPEBITS) == FTN_BLOBZ) &&
	    ((dst->type & DDTYPEBITS) == FTN_BLOBI))
		v = bztobi(*(ft_blob *)v, srct);
	putfld(dst, v, sz);
	dst->n = src->n;
	return 0;
}
