/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef MSDOS
#include <io.h>			/* for access() */
#endif
#include <sys/types.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

static CONST char	MissingDD[] = "Internal error: Missing DD for DBF %s";

/*  Where do memory access need to be aligned to.  sizeof(void *) is
    a good estimate for most machines. */

#define ALIGN_BYTES     TX_ALIGN_BYTES	/* moved to texint.h KNG */

/************************************************************************/

static int tbfinit ARGS((TBL * tb));
static TBL *newtbl ARGS((TXPMBUF *pmbuf));

/************************************************************************/

int
TXclosetblvirtualfields(TBL *tb)
/* Returns 0 on error.
 */
{
	int i;
#ifndef NO_TRY_FAST_CONV
	for (i = 0; i < tb->nvfield; i++)	/* free virtual fields */
#else
	for (i = 0; i < DDFIELDS; i++)	/* free virtual fields */
#endif
	{
		if (tb->vfield[i] != (FLDPN))
			tb->vfield[i] = closefld(tb->vfield[i]);
		if (tb->vfname[i] != (CHARPN))
			tb->vfname[i] = TXfree(tb->vfname[i]);
	}
#ifndef NO_TRY_FAST_CONV
	tb->nvfield = 0;
#endif
	return(1);				/* success */
}

/************************************************************************/
		    /* throws away a table handle */

#ifdef TX_DEBUG
static int vhit = 0;
static int vmiss = 0;
static int shit = 0;
static int smiss = 0;
#endif

/* ------------------------------------------------------------------------ */

int
TXtblReleaseFlds(tbl)
TBL	*tbl;
/* Releases data of `tbl' fields, preserving types/sizes.
 * Returns 0 on error.
 */
{
	unsigned int	i;
	TX_FLD_KIND	saveKind;

	if (tbl->field)
		for (i = 0; i < tbl->n; i++)
		{
			saveKind = tbl->field[i]->kind;
			setfld(tbl->field[i], NULL, 0);
			tbl->field[i]->kind = saveKind;
		}
	return(1);
}

/* ------------------------------------------------------------------------ */

size_t
TXtblGetRowSize(tbl)
TBL	*tbl;
/* Returns size of current row.
 */
{
	return(tbl->irecsz);
}

/* ------------------------------------------------------------------------ */

int
TXtblReleaseRow(tbl)
TBL	*tbl;
/* Releases memory associated with current row of `tbl'.
 * Any fields pointing directly into its data will be cleared
 * and should not be used until next gettblrow().
 * Returns 2 on success, 1 if ok (nothing freed), 0 on error.
 */
{
	RECID	recid;

	TXsetrecid(&recid, 0);		/* just in case */
	if (ioctldbf(TXgetdbf(tbl, &recid),
		     (DBF_KAI | KDBF_IOCTL_FREECURBUF), NULL) == 0)
		/* Row data freed; now clear the fields that point into it: */
		return(TXtblReleaseFlds(tbl) ? 2 : 0);
	return(1);
}

/* ------------------------------------------------------------------------ */

TBL *
closetbl(tb)
TBL *tb;
{
	unsigned int i;

	if (tb != TBLPN)
	{
		if (tb->dd != DDPN)
			closedd(tb->dd);
		if (tb->df != DBFPN)
			closedbf(tb->df);
		if (tb->bf != DBFPN)
			closedbf(tb->bf);
		tb->orec = TXfree(tb->orec);
		if (tb->field)
		{
			for (i = 0; i < tb->n; i++)	/* free allocated fields */
			{
				if (tb->field[i] != (FLDPN))
					tb->field[i] = closefld(tb->field[i]);
			}
			tb->field = TXfree(tb->field);
		}
		TXclosetblvirtualfields(tb);
		tb->rdd = TXfree(tb->rdd);
		tb = TXfree(tb);
	}
#if defined(TX_DEBUG) && defined(NEVER)
	putmsg(999, NULL, "vHit = %d vMiss = %d sHit = %d sMiss = %d",
	       vhit, vmiss, shit, smiss);
#endif
	return (TBLPN);
}

/************************************************************************/

	     /* makes a squeaky clean new table structure */

static TBL *
newtbl(TXPMBUF *pmbuf)
{
	static CONST char	fn[] = "newtbl";
	TBL *tb = (TBL *) TXcalloc(pmbuf, fn, 1, sizeof(TBL));

	if (tb != TBLPN)
	{
		tb->dd = DDPN;
		tb->df = DBFPN;
		tb->bf = DBFPN;
		tb->n = 0;
		tb->orec = (byte *) NULL;
		tb->orecsz = 0;
		tb->orecdatasz = 0;
		tb->prebufsz = 0;
		tb->postbufsz = 0;
	}
	return (tb);
}

/************************************************************************/

static int
tbfinit(tb)			/* allocate memory for the fields and init them (DD reqd!) */
TBL *tb;
{
	static CONST char	fn[] = "tbfinit";
	int i;
	int nfields;

	nfields = ddgetnfields(tb->dd);
	if (nfields == 0)	/* ie. SYSDUMMY KNG 000525 */
	{
		tb->field = FLDPPN;
		goto done;
	}
	tb->field = (FLD **) TXcalloc(TXPMBUFPN, fn, nfields, sizeof(FLD *));
	if (tb->field == (FLD **) NULL)
		return 0;
	for (i = tb->n = 0; i < nfields; i++, tb->n++)	/* make room for incoming fields */
	{
		if ((tb->field[i] = openfld(ddgetfd(tb->dd, i))) == FLDPN)
			return (0);
		if (ddgetblobs(tb->dd))
			tb->field[i]->storage = openstfld(ddgetfd(tb->dd, i));
		if (tb->field[i]->storage)
			tb->field[i]->storage->memory = tb->field[i];
	}
      done:
	return (1);
}

/******************************************************************/

TBL *
opentbl_dbf(DBF * dbf, char *tn)
{
	TBL *tbl = NULL;
	DD *dd;
	size_t sz;
	char fname[PATH_MAX];
	char *fn = fname;

	dd = (DD *) getdbf(dbf, 0, &sz);
	if (!dd)
	{
		return tbl;
	}
	tbl = newtbl(dbf->pmbuf);
	if (!tbl)
	{
		return tbl;
	}
	tbl->dd = convertdd(dd, sz);
	dd = tbl->dd;
	if (!dd)
	{
		return closetbl(tbl);
	}
	tbl->df = dbf;
	if (ddgetblobs(dd))	/* does this table have an associated blob dbf ? */
	{
		if (tn != (char *) NULL && *tn)
		{
			TXstrncpy(fn, tn, PATH_MAX-4);
			strcat(fn, ".blb");	/* WTF I dont check this length */
		}
		else
		{
			fn = (char *) NULL;
		}
		if ((tbl->bf = opendbf(dbf->pmbuf, fn, O_RDWR)) == DBFPN)
		{
			return (closetbl(tbl));
		}
	}
	tbl->tbltype = ddgettype(tbl->dd);
	if (ioctldbf
	    (tbl->df, DBF_KAI | KDBF_IOCTL_PREBUFSZ,
	     (void *) KDBF_PREBUFSZ_WANT) != -1)
		tbl->prebufsz = KDBF_PREBUFSZ_WANT;
	if (ioctldbf
	    (tbl->df, DBF_KAI | KDBF_IOCTL_POSTBUFSZ,
	     (void *) KDBF_POSTBUFSZ_WANT) != -1)
		tbl->postbufsz = KDBF_POSTBUFSZ_WANT;

	if (!tbfinit(tbl))	/* make room for incoming fields */
		return (closetbl(tbl));
	return (tbl);
}

/************************************************************************/

      /* opens and inits an existing table when given a file name */

TBL *
opentbl(pmbuf, tn)
TXPMBUF	*pmbuf;	/* (in, opt.) buffer to clone and attach to; for messages */
char *tn;
{
	static const char	fn[] = "opentbl";
	TBL *tb;
	DBF *df;
	char fname[PATH_MAX];
	char *fnPtr = fname;

	if (tn != (char *) NULL && *tn)
	{
		fname[sizeof(fname) - 5 /* sizeof(".tbl") + nul */] = 'x';
		TXstrncpy(fnPtr, tn, PATH_MAX - 5);
		if (fname[sizeof(fname) - 5] != 'x')
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
				       "Path too long");
			return(NULL);
		}
#ifdef NEVER
		fnPtr[PATH_MAX - 5] = '\0';
#endif
		strcat(fnPtr, ".tbl");
	}
	else
		fnPtr = (char *) NULL;

	if (access(fnPtr, 0) < 0 ||
	    (df = opendbf(pmbuf, fnPtr, O_RDWR)) == DBFPN)
		return (NULL);

	tb = opentbl_dbf(df, tn);
	return (tb);
}

/************************************************************************/

/* Createtbl creates a clean new empty table that has been initialized
to process records of the type defined in the DD that was passed to
it. Createtbl() will return a (TBL *)NULL if error */

TBL *
TXcreatetbl_dbf(DD *dd, DBF *dbf, DBF *bf)
{
	static CONST char	fn[] = "TXcreatetbl_dbf";
	TBL			*tb;
	int			tbltype;

	if (DDPN == dd)				/* KNG 2005-11-23 sanity */
	{
		putmsg(MERR, fn, MissingDD, (dbf ? getdbffn(dbf) : "?"));
		return NULL;
	}

	tbltype = ddgettype(dd);

	if(DBFPN == dbf)
		return NULL;

	if (tbltype == DBASE_TABLE)
		tbltype = TEXIS_FAST_TABLE;	/* Cannot create dbf files */

	if ((tb = newtbl(dbf->pmbuf)) != TBLPN)
	{
		tb->df = dbf;
		tb->tbltype = tbltype;
		if (putdbf(tb->df, -1L, (void *) dd, dd->size) != 0L)
			return (closetbl(tb));
		tb->dd = convertdd(dd, sizeof(DD));
		tb->bf = bf;
		if (!tbfinit(tb))
			return (closetbl(tb));
	}
	return (tb);
}

/******************************************************************/

TBL *
createtbl(dd, tn)
DD *dd;
char *tn;    /* (in, opt.) table path, sans `.tbl'; or NULL/TXNOOPDBF_PATH */
{
	static CONST char	fn[] = "createtbl";
	TBL *tb = NULL;
	DBF *df = NULL, *bf = NULL;
	TXPMBUF	*pmbuf = TXPMBUFPN;
	char fname[PATH_MAX];
	char bname[PATH_MAX];

	if (tn != (char *) NULL && tn != TXNOOPDBF_PATH)
	{
		fname[sizeof(fname) - 1] = 'x';
		TXstrncpy(fname, tn, PATH_MAX - 4);
		strcat(fname, ".tbl");
		if (fname[sizeof(fname) - 1] != 'x')
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Path too long");
			return(TBLPN);
		}
	}
	else
		fname[0] = '\0';

	if (DDPN == dd)				/* KNG 2005-11-23 sanity */
	{
		putmsg(MERR, fn, MissingDD, fname);
		return NULL;
	}

	if ((df = opendbf(TXPMBUFPN, (tn == TXNOOPDBF_PATH ? tn : fname),
			  (O_RDWR | O_CREAT | O_EXCL))) == DBFPN)
		return NULL;
	if (ddgetblobs(dd))
	{
		if (tn == (char *) NULL || tn == TXNOOPDBF_PATH)
		{
			bf = opendbf(TXPMBUFPN, tn, O_RDWR | O_CREAT | O_EXCL);
		}
		else
		{
			TXstrncpy(bname, tn, PATH_MAX - 4);
			strcat(bname, ".blb");/* WTF I dont check this length */
			bf = opendbf(TXPMBUFPN, bname, O_RDWR | O_CREAT | O_EXCL);
		}
		if(!bf)
		{
			df = closedbf(df);
			if (tn != (char *) NULL && tn != TXNOOPDBF_PATH)
				deldbf(fname);
			return NULL;
		}
	}
	tb = TXcreatetbl_dbf(dd, df, bf);
	return (tb);
}

/* ------------------------------------------------------------------------ */

char *
TXtblTupleToStr(TBL *tbl)
/* Prints current `tbl' tuple (row), as `(col, col, ...)'.
 * Returns alloced string.
 */
{
	HTBUF		*buf = NULL;
	char		*ret;
	CONST DDFD	*fdFixed, *fdFixedEnd, *fdVar, *fdVarEnd, *fd;
	int		colCreatedIdx;
	FLD		*fld;
	DD		*dd = tbl->dd;
	const char	*quote;

	if (!(buf = openhtbuf())) goto err;
	htbuf_pf(buf, "(");
	fdFixed = dd->fd;			/* fixed-size fields first */
	fdFixedEnd = fdVar = dd->fd + dd->ivar;	/* followed by variable */
	fdVarEnd = dd->fd + dd->n;
	for (colCreatedIdx = 0; colCreatedIdx < dd->n; colCreatedIdx++)
	{					/* iterate in created order */
		if (fdFixed < fdFixedEnd &&	/* have fixed fields left & */
		    (fdVar >= fdVarEnd ||	/* (no more var fields or */
		     fdFixed->num < fdVar->num))/*  fixed field is next) */
			fd = fdFixed++;
		else
			fd = fdVar++;
		fld = tbl->field[fd - dd->fd];
		if (colCreatedIdx > 0) htbuf_pf(buf, ", ");
		if (TXfldIsNull(fld))
			quote = "";
		else switch (fld->type & DDTYPEBITS)
		{
		case FTN_CHAR:
		case FTN_INDIRECT:
		case FTN_STRLST:
		case FTN_BLOB:
		case FTN_BLOBI:
			quote = "'";
			break;
		default:
			quote = "";
			break;
		}
		htbuf_pf(buf, "%s%s%s", quote, fldtostr(fld), quote);
	}
	htbuf_pf(buf, ")");
	htbuf_getdata(buf, &ret, 0x3);
	goto finally;

err:
	ret = NULL;
finally:
	buf = closehtbuf(buf);
	return(ret);
}

/************************************************************************/

/*
VSL stands for Variable Sized Long

This is for storing and retrieving unsigned long values in the minimum
number of bytes required to hold the given value.

The highest value that may be stored in one of these is
2^30 or about 1 billion ( I hope thats big enough )

*/

#define VSL1MAX 0x3F
#define VSL2MAX 0x3FFF
#define VSL3MAX 0x3FFFFF
#define VSL4MAX 0x3FFFFFFF

/************************************************************************/

byte *
ivsl(bp, pn)
byte *bp;
ulong *pn;
{
	register byte *obp, ob;	/* Place to hold first byte */
	short nbytes;
	byte shift = 0;

	obp = bp;
	ob = *obp;		/* keep track of the first byte */
	*pn = 0L;		/* init the long */
	nbytes = (*bp >> 6);	/* find out how big it is */
	*bp &= 0x3F;		/* get rid of the type bits */

	shift = (byte) (nbytes << 3);

	do
	{
		*pn += ((ulong) * bp++) << shift;
		shift -= 8;
	}
	while (nbytes--);

	*obp = ob;		/* restore the first byte */
	return (bp);
}

/************************************************************************/

int
TXoutputVariableSizeLong(pmbuf, bp, n, desc)
TXPMBUF	*pmbuf;
byte **bp;
ulong n;
const char	*desc;	/* (in, opt.) description of `n' */
/* Outputs `n' to `*bp' as a variable-size-long, advancing `*bp'.
 * Returns 0 on error (`n' too large; `*bp' not advanced; yaps).
 */
{
	static const char	fn[] = "TXoutputVariableSizeLong";
	byte size;
	short i;

	if (n < VSL1MAX)
		size = 0;	/* get the size type */
	else if (n < VSL2MAX)
		size = 1;
	else if (n < VSL3MAX)
		size = 2;
	else if (n < VSL4MAX)
		size = 3;
	else
	{
		txpmbuf_putmsg(pmbuf, MERR, fn, "%s %wku too large for VSL",
			       (desc ? desc : "Value"), (EPI_HUGEUINT)n);
		return(0);	/* too big store */
	}

	for (i = size; i >= 0; i--, n >>= 8)	/* put out the qty in msb->lsb order */
		(*bp)[i] = (byte) (n & 0xff);

	**bp |= size << 6;	/* or the type into the first byte */

	*bp += size + 1;
	return(1);				/* success */
}

/************************************************************************/
/*	Get values from a buffer into a table.  This routine will
 *	extract values from a buffer, and place them into the fields
 *	in tb.  The format of the buffer is consistent with that stored
 *	by puttblrow, and also with fldtobuf.
 */

static int pbuftofld ARGS((byte *, TBL *, byte *));

static int
pbuftofld(rec, tb, end)
byte *rec;			/* The buffer with the data */
TBL *tb;			/* The table to put the values in */
byte *end;			/* End of the buffer */
{
	static CONST char fn[] = "pbuftofld";
	int i;
	int ivar;
	ulong fsz;
	byte *srec = rec, *fend;
	void	*p;

	ivar = ddgetivar(tb->dd);
	for (i = 0; i < (int)tb->n; i++)
	{
		FLD *f = tb->field[i];	/* alias the field */

		if (i >= ivar)
		{
			if (rec >= end)
				goto trunc;
			rec = ivsl(rec, &fsz);
			fend = rec + fsz;	/* check size before alloc KNG */
			if (fend > end || fend < rec)
				goto badsize;
			f->size = (size_t) fsz;
			f->n = f->size / f->elsz;
			if (fsz + 1 > f->alloced)
			{
				TXfreefldshadow(f);
				if ((p = TXmalloc(TXPMBUFPN, fn, (size_t) fsz + 1)) ==
				    (void *) NULL)
					return (-1L);
				setfld(f, p, (size_t)fsz + 1);
			}

/* all the plus 1 stuff above is to enable a 0 byte terminator
 * at the end of all fields but not stored in the file
 */
		}
		else
		{
			fsz = (ulong) f->size;
			fend = rec + fsz;	/* check size before alloc KNG */
			if (fend > end || fend < rec)
				goto trunc;
		}
		setfldv(f);
		memcpy(f->v, (void *) rec, (size_t) fsz);
		*((byte *) f->v + fsz) = '\0';
		rec = fend;
	}
	return 0;
      badsize:
	putmsg(MERR + FRE, fn,
       "Bad size %wd for column %s before offset 0x%wx in recid 0x%wx%s of %s",
	       (EPI_HUGEINT)fsz, ddgetname(tb->dd, i),
	       (EPI_HUGEUINT) (rec - srec),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
	return (-1);
      trunc:
	putmsg(MERR + FRE, fn,
	       "Truncated data for column %s in recid 0x%wx%s of %s",
	       ddgetname(tb->dd, i),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
	return (-1);
}

/******************************************************************/

static int fbuftofld ARGS((byte *rec, TBL *tb, byte *end));

static int
fbuftofld(rec, tb, end)
byte *rec;			/* the data: must be ALIGN_BYTES-aligned */
TBL *tb;			/* The table to put the values in */
byte *end;			/* Size of the buffer */
{
	static CONST char fn[] = "fbuftofld";
	int i, dupData = 0;
	unsigned long m;        /* must be unsigned */

#ifdef FAST_BUFFLD
	unsigned int nstart = 0;
#endif
	int ivar;
	int nflds = tb->n;
	byte *srec = rec, *fend;
	ulong fsz;

	/* Bug 4720: we point tb->field[] data directly into `rec' data,
	 * so we align those FLD pointers -- but we also assume `rec' is
	 * initially aligned.  Lack of initial alignment can cause unneeded
	 * re-alignment of `rec' (below) between fields, which was not
	 * present in the original ffldtobuf() mapping, causing corruption
	 * and `Bad size N for column X ...' errors:
	 */
	if (((EPI_VOIDPTR_UINT)rec % (EPI_VOIDPTR_UINT)ALIGN_BYTES) !=
	    (EPI_VOIDPTR_UINT)0)
	{
		/* We can work around this by duplicating data, so that
		 * FLD data alignment is independent of `rec' alignment.
		 * But this should not happen, so yap:
		 */
		if (TXApp->unalignedBufferWarning)
		{
			putmsg(MWARN + MAE, fn,
	     "Unaligned buffer %p for table `%s': fixing by duplicating data",
			       rec, (tb->df ? getdbffn(tb->df) : "?"));
		}
		dupData = 1;
		/* Since we have dissociated `rec' and FLD alignment,
		 * we can now align `rec' below based on offset-in-buffer
		 * (instead of absolute pointer value), which should
		 * correspond with its original ffldtobuf() alignment.
		 * When the original `rec' start is properly aligned
		 * (the normal case), offset-in-buffer alignment is
		 * the same as absolute-pointer alignment; thus we now use
		 * offset-in-buffer alignment always, for convenience.
		 * Bug 4720
		 */
	}

	ivar = ddgetivar(tb->dd);
#ifdef FAST_BUFFLD
error check rec alignment assumptions, e.g. Bug 4720: should we
  set srec = rec here?;
	if (rec == tb->irec)
	{
		nstart = ivar;
		rec = tb->ivarpos;
	}
	else
		tb->irec = rec;
	for (i = nstart; i < nflds; i++)
#else
	for (i = 0; i < nflds; i++)
#endif
	{
		FLD *f = tb->field[i];	/* alias the field */

		if (f->storage)
			f = f->storage;
		if (i >= ivar)			/* var-size field */
		{
			if (rec + sizeof(ulong) > end)
				goto trunc;	/* KNG */
#ifdef FAST_BUFFLD
			if (i == ivar)
				tb->ivarpos = rec;
#endif /* FAST_BUFFLD */
			fsz = *(ulong *) rec;
			rec += sizeof(ulong);
			if (f->elsz == 1)
				f->size = (size_t) fsz - 1;
			else
				f->size = (size_t) fsz;
			f->n = f->size / f->elsz;
			/* KNG 20060213 cleanliness: let setfld() etc.
			 * set `alloced' when setting `shadow':
			 */
			/* if (fsz + 1 > f->alloced)
				f->alloced = fsz; */

/* all the plus 1 stuff above is to enable a 0 byte terminator
 * at the end of all fields but not stored in the file
 */
		}
		else				/* fixed-size field */
			fsz = (ulong) f->size;
		fend = rec + f->size;
		if (fend < rec)
			goto badsize;	/* KNG sanity */
		if (f->elsz == 1)
			fend++;
		if (fend > end)
			goto badsize;	/* KNG sanity */
		if ((f->type & DDTYPEBITS) == FTN_BLOBI)
		{
			memcpy(f->shadow, rec, f->size);
			setfldv(f);
		}
		else
		{
#ifdef TX_DEBUG
			if (f->shadow == rec)
				shit++;
			else
				smiss++;
			if (f->v == rec)
				vhit++;
			else
				vmiss++;
#endif /* TX_DEBUG */
			TXfreefldshadow(f);
			if (dupData)
			{
				f->shadow = TXmalloc(TXPMBUFPN, fn, f->size+1);
				if (!f->shadow) goto err;
				memcpy(f->shadow, rec, f->size);
				((char *)f->shadow)[f->size] = '\0';
				TXsetshadowalloc(f);
			}
			else
			{
				f->shadow = rec;
				TXsetshadownonalloc(f);
			}
			setfldv(f);
		}
		rec = fend;
		if (f->memory)
		{
			if (TXisblob(f->type))
			{
				FLD *fm = f->memory;
				ft_blobi *bi;

				if (!fm->v)
				{
					/*KNG 20050213 setfldandsize for OO:*/
					setfldandsize(fm,
						TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_blobi)+1),
						sizeof(ft_blobi)+1, FLD_FORCE_NORMAL);
				}
				bi = fm->v;
				if (bi)
				{
#ifdef NEVER /* Are the following needed?  Doesn't the last memcpy overwrite? */
#endif
					bi->dbf = tb->bf;
					bi->off = *(ft_blob *)f->v;
					TXblobiFreeMem(bi);
					/* should be blob or blobz: */
					bi->otype = (f->type & DDTYPEBITS);
				}
			}
		}
                /* Make sure we use an unsigned type for the mod; the
                 * pointer could cast to a negative number with
                 * undefined results after a mod.  Note that size
                 * isn't really an issue, since we only care about the
                 * lowest few bits:    KNG 020208
		 *   Bug 4720 see `dupData' comments above; we now align
		 * based on offset-in-buffer, not absolute-pointer:
                 */
		m = (EPI_VOIDPTR_UINT)(rec - srec) %
			(EPI_VOIDPTR_UINT)ALIGN_BYTES;
		if (m != (EPI_VOIDPTR_UINT)0)
			rec += ALIGN_BYTES - (int)m;
	}
	return 0;
      badsize:
	putmsg(MERR + FRE, fn,
       "Bad size %wd for column %s%s offset 0x%wx in recid 0x%wx%s of %s",
	       (EPI_HUGEINT)fsz, ddgetname(tb->dd, i),
	       (i >= ivar ? " at" : "; truncated data at"),
	       (EPI_HUGEUINT)(rec-srec) - (EPI_HUGEUINT)(i >= ivar ? sizeof(ulong):0),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
	return (-1);
      trunc:
	putmsg(MERR + FRE, fn,
	       "Truncated data for column %s in recid 0x%wx%s of %s",
	       ddgetname(tb->dd, i),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
err:
	return (-1);
}

/******************************************************************/
#ifndef NO_HAVE_DDBF

static int dbuftofld ARGS((byte *, TBL *, byte *));

static int
dbuftofld(rec, tb, end)
byte *rec;			/* The buffer with the data */
TBL *tb;			/* The table to put the values in */
byte *end;			/* Size of the buffer */
{
	static CONST char	fn[] = "dbuftofld";
	unsigned int i;
	int ivar;
	size_t szm;
	char *v;

	(void)end;
	ivar = ddgetivar(tb->dd);
	rec++;
	for (i = 0; i < tb->n; i++)
	{
		int j = ddgetorign(tb->dd, i);
		FLD *f = tb->field[j];	/* alias the field */
		ulong fsz;

		if (f->storage)
			f = f->storage;
		if (j >= ivar)
		{
			/*
			   This is where we need to handle memos
			   Uh-oh.  Need to handle varchars here too!
			 */

			FLD *fm;
			off_t blocknum;
			int k;

			/* First behave as normal.  f->v should now
			   have character representation of block num */
			fsz = (ulong) f->size;
			setfldv(f);
			memcpy(f->v, (void *) rec, (size_t) fsz);
			*((byte *) f->v + fsz) = '\0';
			rec += fsz;
			fm = f->memory;

#ifndef NEVER
			if (f->wasmemo)
#else
			if (fm->size > 255)
#endif
			{
				blocknum = atoi(f->v);
				if (blocknum == 0)
				{
					v = TXstrdup(TXPMBUFPN, fn, "");
					szm = 0;
				}
				else if (tb->bf)
					v = agetdbf(tb->bf, blocknum, &szm);
				else
					return 0;
				if (!v)
				{
					putmsg(MINFO, NULL, "Bad block %d",
					       blocknum);
					v = TXstrdup(TXPMBUFPN, fn, "");
					szm = 0;
				}
#ifdef NEVER
				if (szm > 10000 || szm < 0)
				{
					putmsg(999, NULL, "%lx %d", v, szm);
					szm = 0;
				}
#endif
#ifndef NEVER
				for (k = szm; k && v; k--)
					if (v[k] == 11)
					{
						v[k] = 0;
						k = 1;
					}
#endif
				setfld(fm, v, szm);
				fm->size = fm->n = szm;
#ifdef NEVER
				if (fm->size < 256)
					fm->size = 256;
#endif
			}
			else
			{
				if (fm)
					setfld(fm, NULL, 0);
				v = TXmalloc(TXPMBUFPN, fn, fsz + 1);
				if (!v) return(-1);
				memcpy(v, f->v, (size_t) fsz);
				for (szm = fsz - 1;
				     (int) szm >= 0 && (*(v + szm) == '\0'
							|| isspace((int)
								   *(
								     (unsigned
								      char *)
								     v +
								     szm)));
				     szm--)
					*(v + szm) = '\0';
				/* Handle char */
				if (fm)
					setfld(fm, v, szm + 1);
			}
		}
		else
		{
			fsz = (ulong) f->size;
			setfldv(f);
			memcpy(f->v, (void *) rec, (size_t) fsz);
			v = f->v;
			for (szm = fsz - 1;
			     (int) szm >= 0 && (*(v + szm) == '\0'
						|| isspace((int)
							   *((unsigned char *)
							     v + szm)));
			     szm--)
				*(v + szm) = '\0';
			*((byte *) f->v + fsz) = '\0';
			rec += fsz;
		}
	}
	return 0;
}
#endif

/******************************************************************/
#ifdef NULLABLE_TEXIS_TABLE
static int nbuftofld(byte * rec, TBL * tb, byte * end);
static size_t nfldtobuf(TBL * tb);
#endif /* NULLABLE_TEXIS_TABLE */

int
buftofld(rec, tb, recsz)
byte *rec;			/* The buffer with the data */
TBL *tb;			/* The table to put the values in */
size_t recsz;			/* Size of the buffer */
{
#if defined(NEVER) && defined(TX_DEBUG)
	putmsg(999, "buftofld", "type=%d, rec=%lx, sz=%lx, tb=%s",
	       tb->tbltype, rec, recsz, getdbffn(tb->df));
#endif
	switch (tb->tbltype)
	{
	case TEXIS_OLD_TABLE:
		return pbuftofld(rec, tb, rec + recsz);
	case TEXIS_FAST_TABLE:
		return fbuftofld(rec, tb, rec + recsz);
#ifdef NULLABLE_TEXIS_TABLE
	case TEXIS_NULL1_TABLE:
		return nbuftofld(rec, tb, rec + recsz);
#endif
	case TEXIS_TMP_TABLE:
		return 0;
#ifndef NO_HAVE_DDBF
	case DBASE_TABLE:
		return dbuftofld(rec, tb, rec + recsz);
	default:
		putmsg(999, NULL, "Don't know table type %d", tb->tbltype);
#endif
	}
	return -1;
}

/******************************************************************/
/*	Takes a table, encodes the fields.  This routine takes the
 *	values stored in the fields of tb, and generates a buffer
 *	that contains an encoding of the fields.  The buffer is
 *	pointed to by tb->orec upon return.
 *
 *	returns the size of the buffer.
 */

static size_t pfldtobuf ARGS((TBL *));

static size_t
pfldtobuf(tb)
TBL *tb;			/* The table to encode */
{
	static CONST char	fn[] = "pfldtobuf";
	int i;
	size_t needed;
	byte *rec;
	int ivar;

	ivar = ddgetivar(tb->dd);
	for (needed = i = 0; i < (int)tb->n; i++)
		needed += tb->field[i]->size + sizeof(ulong);

	needed += (tb->prebufsz + tb->postbufsz);
	if (needed > tb->orecsz)
	{
		tb->orec = TXfree(tb->orec);
		if ((tb->orec = (byte *)TXmalloc(TXPMBUFPN, fn, needed)) == (byte *) NULL)
			return (-1L);
		tb->orecsz = needed;
	}

	rec = tb->orec;
	rec += tb->prebufsz;
	for (i = 0; i < (int)tb->n; i++)
	{
		FLD *f = tb->field[i];
		void *v;
		size_t	sizeToUse;

		sizeToUse = f->size;
		if (i >= ivar)
		{
			sizeToUse = f->size = f->n * f->elsz;
			if (sizeToUse < f->n || sizeToUse < f->elsz ||
#if EPI_OS_SIZE_T_BITS > EPI_OS_ULONG_BITS
			    sizeToUse > (size_t)EPI_OS_ULONG_MAX ||
#endif
			    !TXoutputVariableSizeLong(TXPMBUFPN, &rec,
					       (ulong) f->size, "Field size"))
			{
				/* truncate it; wtf return error instead: */
				sizeToUse = TX_MIN(sizeToUse, VSL4MAX - 1);
				TXoutputVariableSizeLong(TXPMBUFPN, &rec,
							 (ulong)sizeToUse,
						      "Truncated field size");
			}
		}
		v = getfld(f, NULL);
		if (v)
			memcpy((void *) rec, v, sizeToUse);
		else
			memset((void *) rec, 0, sizeToUse);
		rec += sizeToUse;
	}
	return (rec - (tb->orec + tb->prebufsz));
}

/******************************************************************/

static size_t ffldtobuf ARGS((TBL *));

static size_t
ffldtobuf(tb)
TBL *tb;			/* The table to encode */
/* Returns size of data written to `tb->orec', or -1 on error.
 */
{
	static CONST char	fn[] = "ffldtobuf";
	int i;
	size_t needed;
	byte *rec, *recDataStart;
	int ivar;
	ulong	ulSz;
	void	*fldData;

	ivar = ddgetivar(tb->dd);
	for (needed = i = 0; i < (int)tb->n; i++)
	{
		needed += tb->field[i]->size + sizeof(ulong);
		if (tb->field[i]->elsz == 1)
			needed++;
		if (needed % ALIGN_BYTES)
			needed += ALIGN_BYTES - (needed % ALIGN_BYTES);
	}

	needed += (tb->prebufsz + tb->postbufsz);
	if (needed > tb->orecsz)
	{
		tb->orec = TXfree(tb->orec);
		if ((tb->orec = (byte *) TXmalloc(TXPMBUFPN, fn, needed)) == (byte *) NULL)
			goto err;
		tb->orecsz = needed;
	}

	rec = recDataStart = tb->orec + tb->prebufsz;
	for (i = 0; i < (int)tb->n; i++)
	{
		FLD *f = tb->field[i];

		if (f->storage)
			f = f->storage;
		if (i >= ivar)			/* this is a var FLD */
		{
			f->size = f->n * f->elsz;
			if (f->elsz == 1)
				ulSz = (ulong)(f->size + 1);
			else
				ulSz = (ulong)f->size;
			if ((size_t)ulSz < f->size ||
			    TX_SIZE_T_VALUE_LESS_THAN_ZERO(f->size))
			{			/* overflow */
				putmsg(MERR + MAE, fn,
			     "Data size %wd out of range for column %s of %s",
				       (EPI_HUGEINT)f->size,
				       ddgetname(tb->dd, i),
				       (tb->df ? getdbffn(tb->df) : "?"));
				goto err;
			}
			/* KNG 20130419 Bug 4720 do not assume `prebufsz'
			 * is ALIGN_BYTES- or ulong-aligned:
			 */
			memcpy(rec, &ulSz, sizeof(ulong));
			rec += sizeof(ulong);
		}
		if (TXfldIsNull(f))
		{
			/* Bug 5395: convert NULL to empty for now, so that
			 * we can use NULL in fldmath at least: that still
			 * involves a putdbtblrow(), even if it is to RAM DBF.
			 * WTF this would let NULLs become empty in tables,
			 * but ok for now since we do not yet support NULL
			 * in disk tables.
			 */
			static const ft_double	db = 0.0;
			static const ft_float	fl = 0.0;
			static const ft_strlst	sl = { 0, ',', "" };
			static const ft_date	da = (ft_date)0;
			static const ft_datetime dt = { 1970, 1, 1, 0, 0,0,0};
			size_t			fldDataSz, n;

			switch (TXfldType(f) & DDTYPEBITS)
			{
			case FTN_DOUBLE:
				fldDataSz = sizeof(ft_double);
				fldData = (void *)&db;
				break;
			case FTN_FLOAT:
				fldDataSz = sizeof(ft_float);
				fldData = (void *)&fl;
				break;
			case FTN_STRLST:
				fldDataSz = sizeof(ft_strlst);
				fldData = (void *)&sl;
				break;
			case FTN_DATE:
				fldDataSz = sizeof(ft_date);
				fldData = (void *)&da;
				break;
			case FTN_DATETIME:
				fldDataSz = sizeof(ft_datetime);
				fldData = (void *)&dt;
				break;
			default:
				fldDataSz = 1;
				fldData = "";
				break;
			}
			for (n = 0; n < f->size; n += fldDataSz)
				memcpy((char *)rec + n, fldData,
				       TX_MIN(fldDataSz, f->size - n));
		}
		else
		{
			fldData = getfld(f, NULL);
			if (!fldData)
			{
				putmsg(MERR + MAE, fn,
				       "NULL field data for column %s of %s",
				       ddgetname(tb->dd, i),
				       (tb->df ? getdbffn(tb->df) : "?"));
				goto err;
			}
			memcpy((void *) rec, fldData, f->size);
		}
		rec += f->size;
		if (f->elsz == 1)
		{
			*rec = '\0';
			rec++;
		}
		/* KNG 20130419 Bug 4720 do not assume malloc() returns
		 * ALIGN_BYTES-aligned buffer, nor that `prebufsz' is
		 * ALIGN_BYTES-aligned: align from buffer-start not abs ptr:
		 */
		while (((EPI_VOIDPTR_UINT)(rec - recDataStart) %
			(EPI_VOIDPTR_UINT)ALIGN_BYTES) != (EPI_VOIDPTR_UINT)0)
			*(rec++) = '\0';
	}
	return (rec - recDataStart);
err:
	return((size_t)(-1));
}

/******************************************************************/

#ifdef NULLABLE_TEXIS_TABLE

static int
nbuftofld(byte * rec, TBL * tb, byte * end)
{
	static CONST char Fn[] = "nbuftofld";
	int i = 0;
	int orpos;
	int ivar;
	int ntblflds = tb->n;
	int nrecflds = 0;
	int nnullbytes = tb->nnb;
	ulong fsz;
	unsigned long m;
	DD *dd = tb->dd;
	FLD *f;
	byte *fend, *srec = rec, *nulli = NULL;
	int nullb = 0, isnull;
	byte nullm = 128;

	if ((end - rec) > sizeof(int))
	{
		nrecflds = *(int *) rec;
		rec += sizeof(int);
	}

	if (nnullbytes)
	{
		nulli = rec;
		rec += nnullbytes;
		if (rec > end)
			goto trunc;
	}
	ivar = ddgetivar(dd);

	for (; i < ntblflds; i++)
	{
		f = tb->field[i];
		if (f->storage)
			f = f->storage;
		orpos = dd->fd[i].num;
		isnull = 0;
#error check FTN_NotNullableFlag usage: iff set: field is *not* allowed to contain a NULL
		if (dd->fd[i].type & FTN_NotNullableFlag)
		{
			if (nulli[nullb] & nullm)
			{
				isnull = 1;
			}
			if (nullm > 1)
			{
				nullm >>= 1;
			}
			else
			{
				nullm = 128;
				nullb++;
			}
		}
		if (isnull || (orpos > nrecflds))
		{
			f->size = 0;
			f->n = 0;
			TXfreefldshadow(f);
			f->shadow = NULL;
			f->frees = 0;
			continue;
		}
		if (i >= ivar)
		{
			if (rec + sizeof(ulong) > end)
				goto trunc;	/* KNG */
			fsz = *(ulong *) rec;
			rec += sizeof(ulong);
			if (f->elsz == 1)
				f->size = (size_t) fsz - 1;
			else
				f->size = (size_t) fsz;
			f->n = f->size / f->elsz;
			if (fsz + 1 > f->alloced)
				f->alloced = fsz;

/* all the plus 1 stuff above is to enable a 0 byte terminator
 * at the end of all fields but not stored in the file
 */
		}
		else
			fsz = (ulong) f->size;
		fend = rec + f->size;
		if (fend < rec)
			goto badsize;	/* KNG sanity */
		if (f->elsz == 1)
			fend++;
		if (fend > end)
			goto badsize;	/* KNG sanity */
		if ((f->type & DDTYPEBITS) == FTN_BLOBI)
		{
			memcpy(f->shadow, rec, f->size);
			setfldv(f);
		}
		else
		{
			TXfreefldshadow(f);
			f->shadow = rec;
			setfldv(f);
			f->frees = 0;
		}
		rec = fend;
		if (f->memory)
		{
			if (TXisblob(f->type))
			{
				FLD *fm = f->memory;
				ft_blobi *bi;

				if (!fm->v)
				{
					fm->v = TXcalloc(TXPMBUFPN, Fn, 1, sizeof(ft_blobi));
					if (!fm->v) return(-1);
					fm->alloced = sizeof(ft_blobi);
					fm->size = sizeof(ft_blobi);
					fm->n = 1;
					TXfreefldshadow(fm);
					fm->shadow = fm->v;
					fm->frees = FREESHADOW;
				}
				bi = fm->v;
				if (bi)
				{
					bi->dbf = tb->df;
					bi->off = *(ft_blob *)f->v;
					TXblobiFreeMem(bi);
					switch(f->type & DDTYPEBITS)
					{
					case FTN_BLOB:
						bi->otype = BLOB_ASIS;
						break;
					case FTN_BLOBZ:
						bi->otype = BLOB_GZIP;
						break;
					default:
						bi->otype = BLOB_UNKNOWN;
						break;
					}
				}
			}
		}
		if ((m = (unsigned long) rec % (unsigned long)ALIGN_BYTES) != 0L)
			rec += ALIGN_BYTES - (int) m;
	}
	return 0;
      badsize:
	putmsg(MERR + FRE, Fn,
	"Bad size %wd for column %s%s offset 0x%wx in recid 0x%wx%s of %s",
	       (EPI_HUGEINT)fsz, ddgetname(tb->dd, i),
	       (i >= ivar ? " at" : "; truncated data at"),
	       (EPI_HUGEUINT)(rec-srec) - (EPI_HUGEUINT)(i>=ivar ? sizeof(ulong) : 0),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
	return (-1);
      trunc:
	putmsg(MERR + FRE, Fn,
	       "Truncated data for column %s in recid 0x%wx%s of %s",
	       ddgetname(tb->dd, i),
	       (tb->df ? (EPI_HUGEUINT)telldbf(tb->df) : (EPI_HUGEUINT)0),
	       (tb->df ? "" : "?"),
	       (tb->df ? getdbffn(tb->df) : "?"));
	return (-1);
}

static size_t
nfldtobuf(TBL * tb)
{
	static CONST char	fn[] = "nfldtobuf";
	int i;
	size_t needed;
	byte *rec;
	int ivar;
	int nnullbytes = tb->nnb;
	int nullb = 0;
	byte nullm = 128, *nulli;
	DD *dd = tb->dd;

	ivar = ddgetivar(tb->dd);
	for (needed = i = 0; i < (int)tb->n; i++)
	{
		needed += tb->field[i]->size + sizeof(ulong) + sizeof(int)
			/* + sizeofnulldata */ ;

		if (tb->field[i]->elsz == 1)
			needed++;
		if (needed % ALIGN_BYTES)
			needed += ALIGN_BYTES - (needed % ALIGN_BYTES);
	}

	needed += (tb->prebufsz + tb->postbufsz);
	if (needed > tb->orecsz)
	{
		tb->orec = TXfree(tb->orec);
		if ((tb->orec = (byte *) TXmalloc(TXPMBUFPN, fn, needed)) == (byte *) NULL)
			return (-1L);
		tb->orecsz = needed;
	}

	rec = tb->orec + tb->prebufsz;

	*(int *) rec = tb->n;
	rec += sizeof(int);

	if (nnullbytes)
	{
		nulli = rec;
		rec += nnullbytes;
		memset(nulli, 0, nnullbytes);
	}

	for (i = 0; i < (int)tb->n; i++)
	{
		FLD *f = tb->field[i];

		if (f->storage)
			f = f->storage;
#error check FTN_NotNullableFlag usage: iff set: field is *not* allowed to contain a NULL
		if (dd->fd[i].type & FTN_NotNullableFlag && (f->v == NULL))
		{
			nulli[nullb] |= nullm;
			if (nullm > 1)
			{
				nullm >>= 1;
			}
			else
			{
				nullm = 128;
				nullb++;
			}
			continue;
		}
		if (i >= ivar)
		{
			f->size = f->n * f->elsz;
			if (f->elsz == 1)
				*(ulong *) rec = (f->size + 1);
			else
				*(ulong *) rec = f->size;
			rec += sizeof(ulong);
		}
		memcpy((void *) rec, getfld(f, NULL), f->size);
		rec += f->size;
		if (f->elsz == 1)
		{
			*rec = '\0';
			rec++;
		}
		while ((unsigned long)rec % (unsigned long)ALIGN_BYTES)
			*(rec++) = '\0';
	}
	return (rec - (tb->orec + tb->prebufsz));
}
#endif

size_t fldtobuf(tb)
TBL *tb;			/* The table to encode */
/* Returns size of data written to` tb->orec', or -1 on error.
 */
{
	size_t rc = (size_t)(-1);

	switch (tb->tbltype)
	{
	case TEXIS_OLD_TABLE:
		rc = pfldtobuf(tb);
		break;
	case TEXIS_FAST_TABLE:
		rc = ffldtobuf(tb);
		break;
#ifdef NULLABLE_TEXIS_TABLE
	case TEXIS_NULL1_TABLE:
		rc = nfldtobuf(tb);
		break;
#endif
	case TEXIS_TMP_TABLE:
		rc = 0;
		break;
	}
#if defined(NEVER) && defined(TX_DEBUG)
	putmsg(999, "fldtobuf", "type=%d, rec=%lx, sz=%lx, tb=%s",
	       tb->tbltype, tb->orec, rc, getdbffn(tb->df));
#endif
	tb->orecdatasz = rc;
	return rc;
}

/************************************************************************/

int
validrow(tb, handle)
TBL *tb;
RECID *handle;
{
	return validdbf(TXgetdbf(tb, handle), TXgetoff(handle));
}

/******************************************************************/
/**
 * Clears computed values cached in the table, for example virtual
 * fields or computed JSON fields
 *
 * @param tb The table to clear the value from
 *
**/

static void
tblReleaseComputedValues(TBL *tb)
{
	unsigned int i;

	for(i = 0; i < tb->nvfield; i++)
	{
		if(tb->vfield[i] && FLD_IS_COMPUTED(tb->vfield[i]))
		{
			TXfldSetNull(tb->vfield[i]);
		}
	}
}
/******************************************************************/

/* gets a row/record from a table and initializes pointers to the fields
as well as the sizes.  If gettbl is called with -1L as a handle, it will
read the next record within the file.
returns handle if OK, -1L on error or EOF .
*/
/*WTF Call fldtobuf and friends */

RECID *
gettblrow(tb, handle)
TBL *tb;
RECID *handle;
{
	byte *rec;
	static RECID rc;
	char *f;

	tblReleaseComputedValues(tb);
  gettblrow:
	if (
	    (rec =
	     (byte *) getdbf(TXgetdbf(tb, handle), TXgetoff(handle),
			     &tb->irecsz)) == (byte *) NULL)
		return (RECID *) NULL;

	if (buftofld(rec, tb, tb->irecsz) == -1)
	{
		f = getdbffn(tb->df);
		if (f == CHARPN)
			f = "RAM DBF";
		putmsg(MWARN, NULL, "Error in data, file %s, offset 0x%wx",
		       f, (EPI_HUGEUINT)telldbf(tb->df));
		if (TXgetoff(handle) == -1)
			goto gettblrow;
		return NULL;
	}
	/* Set `tb->irec' for potential use by TXcompactTable().
	 * But set it *after* buftofld() call, which expects it
	 * to be the *previous* getdbf() buffer:
	 */
	tb->irec = rec;
#ifdef FAST_BUFFLD
	wtf probably want another field in `tb' (e.g. `prevIrec')
	for fbuftofld() use, to distinguish from current-DBF-buffer use
	by TXcompactTable()
#endif /* FAST_BUFFLD */

	/* Pre-load blobs if requested, now that we have locks and
	 * know that blob DBF handle(s) should still be valid.
	 * Ensures that blobi has data in mem, in case we close
	 * the DBF out from under it, or it reads after locks, etc.
	 * Should be safe to do for multiple blobs in a row,
	 * now that TXblobiGetPayload() always allocs instead of
	 * returning native getdbf() buffer (which could get overwritten
	 * by next blob in a row):
	 */
	if (TXApp && TXApp->preLoadBlobs && tb->bf)
	{
		size_t	i;
		FLD	*f;
		void	*v;
		size_t	sz;

		for (i = 0; i < tb->n; i++)
		{
			f = tb->field[i];	/* alias the field */
			if (TXfldbasetype(f) == FTN_BLOBI &&
			    (v = getfld(f, &sz)) != NULL &&
			    sz >= sizeof(ft_blobi))
				TXblobiGetPayload((ft_blobi *)v, NULL);
		}
	}

	TXsetrecid(&rc, telldbf(tb->df));
	return &rc;
}

/************************************************************************/

/* position the table to the first row in the file.
returns 1 if ok, 0 if not, 2 if by sea */

int
rewindtbl(tb)
TBL *tb;
{
	size_t sz;

/*
 if(telldbf(tb->df)==0)
	return 1;
*/
#ifdef FAST_BUFFLD
	tb->irec = NULL;
	tb->ivarpos = NULL;
#endif
	if (ioctldbf(tb->df, DBF_KAI + KDBF_IOCTL_SEEKSTART, NULL) == 0)
		return 1;
	if (ioctldbf(tb->df, DBF_NOOP + TXNOOPDBF_IOCTL_SEEKSTART, NULL) == 0)
		return 1;
	/* Seek past the .tbl header (DD) by reading it: */
	if (getdbf(tb->df, 0L, &sz) == (void *) NULL)
		return (0);
	return (1);
}

/************************************************************************/

/* This function collects the fields present in the record to be written
together and then sends the record out as one large unit.  Puttbl()
returns a handle to the record which may be different than the one passed
to it if the record is a different size.  It will return -1L on error.  */

RECID *
puttblrow(tb, handle)
TBL *tb;
RECID *handle;
{
	size_t needed;
	static RECID rc;
	DBF	*df;

	df = TXgetdbf(tb, handle);
	/* Bug 4770 optimization: skip fldtobuf() if not needed
	 * (DBF_NOOP), e.g. QNODE-to-QNODE transfer for GROUP BY:
	 */
	if (df->dbftype == DBF_NOOP)
		needed = 0;
	else if ((needed = fldtobuf(tb)) == (size_t)(-1))
		return (RECID *) NULL;
	TXsetrecid(&rc, putdbf(df, TXgetoff(handle), tb->orec, needed));
	return &rc;
}

/************************************************************************/

 /* given a field name, it returns the field pointer or NULL on error */

FLD *
nametofld(tb, s)
TBL *tb;
char *s;
{
	static CONST char	fn[] = "nametofld";
	FLD *fld;
        char *jp;

	int i;

	if (!s)
		return NULL;
	i = ddfindname(tb->dd, s);
	if (i != -1)
		return tb->field[i];
	if (strchr(s, '\\'))
	{
		char *c = s;
		char *e, tc = '\0';
		int fldcount = 1;
		int j;
		FLD *fld1;

#ifndef NO_TRY_FAST_CONV
		for (j = 0; j < tb->nvfield; j++)
#else
		for (j = 0; j < DDFIELDS; j++)
#endif
			if (tb->vfname[j] && !strcmp(s, tb->vfname[j]))
			{
				if (FLD_IS_VIRTUAL(tb->vfield[j]))
				{
					return tb->vfield[j];
					break;
				}
				else
				{
					tb->vfield[j] =
						closefld(tb->vfield[j]);
					tb->vfname[j] = TXfree(tb->vfname[j]);
				}
			}
		fld = (FLD *) TXcalloc(TXPMBUFPN, fn, 1, sizeof(FLD));
		if (!fld) return(FLDPN);
		fld->kind = TX_FLD_VIRTUAL;
		for (e = c; *e; e++)
			fldcount += (*e == '\\') ? 1 : 0;
		fld->vfc = fldcount;
		fld->fldlist = (FLD **) TXcalloc(TXPMBUFPN, fn, fldcount, sizeof(FLD *));
		if (!fld->fldlist)
		{
			fld = closefld(fld);
			return(FLDPN);
		}
		fldcount = 0;
		while (c && *c)
		{
			e = strchr(c, '\\');
			if (e)
			{
				tc = *e;
				*e = '\0';
			}
			if (strlen(c))
				fld1 = nametofld(tb, c);
			else
				fld1 = fld;
			if (!fld1)
			{
				if (e)
					*e = tc;
				return closefld(fld);
			}
			else
			{
				if (fldcount == 0)	/* 1st field */
				{
					/* KNG 20080319 A virtual field's
					 * type should always be varchar
					 * (see mkvirtual()), unless all
					 * sub-fields are [var]byte:
					 */
					if ((fld1->type & DDTYPEBITS) ==
					    FTN_BYTE)
					{
						fld->type =
							FTN_BYTE | DDVARBIT;
						fld->elsz = 1;
					}
					else
					{
						fld->type =
							FTN_CHAR | DDVARBIT;
						fld->elsz = 1;
					}
				}
				else if ((fld1->type & DDTYPEBITS) !=
					    FTN_BYTE &&
					 (fld->type & DDTYPEBITS) ==
					    FTN_BYTE)
				{
					/* A later field was not [var]byte;
					 * revert to varchar:
					 */
					fld->type = FTN_CHAR | DDVARBIT;
					fld->elsz = 1;
				}
				fld->fldlist[fldcount++] = fld1;
			}
			/* Add field */
			if (e)
			{
				*e = tc;
				c = e + 1;
			}
			else
				c = NULL;
		}
		for (j = 0; j < DDFIELDS; j++)
			if (!tb->vfield[j])
			{
				tb->vfield[j] = fld;
				tb->vfname[j] = TXstrdup(TXPMBUFPN, fn, s);
#ifndef NO_TRY_FAST_CONV
				tb->nvfield = j + 1;
#endif
				break;
			}
		if (j == DDFIELDS)
			putmsg(MWARN, NULL, "Too many virtual fields");
		return fld;
	} else if ((TXApp->betafeatures[BETA_JSON]) &&
                  (jp = strstr(s, ".$")) && (jp[2] == '.' || jp[2] == '[')) {
            char *parentFldName = NULL;
            char *jsonPath;
            FLD *ofld;
		int j;

#ifndef NO_TRY_FAST_CONV
		for (j = 0; j < tb->nvfield; j++)
#else
		for (j = 0; j < DDFIELDS; j++)
#endif
			if (tb->vfname[j] && !strcmp(s, tb->vfname[j]))
			{
				if (FLD_IS_COMPUTED(tb->vfield[j]))
				{
					return tb->vfield[j];
					break;
				}
				else
				{
					tb->vfield[j] =
						closefld(tb->vfield[j]);
					tb->vfname[j] = TXfree(tb->vfname[j]);
				}
			}
            fld = FLDPN;
            parentFldName = TXcalloc(NULL, fn, 1, (jp-s)+1);
            if(!parentFldName)
               goto jsonflddone;
            TXstrncpy(parentFldName, s, (jp-s)+1);
            fld = (FLD *) TXcalloc(NULL, fn, 1, sizeof(FLD));
            if (!fld)
               goto jsonflddone;
            fld->kind = TX_FLD_COMPUTED_JSON;
            fld->vfc = 2;
            fld->fldlist = (FLD **) TXcalloc(NULL, fn, 2, sizeof(FLD *));
            if (!fld->fldlist)
            {
               goto jsonflddone;
            }
            if (!(fld->fldlist[0] = nametofld(tb, parentFldName)))
            {
               goto jsonflddone;
            }
            if (!(fld->fldlist[1] = createfld("varchar", 1, 1)))
            {
               goto jsonflddone;
            }
            parentFldName = TXfree(parentFldName);
            jsonPath = strdup(jp+1);
            setfld(fld->fldlist[1], jsonPath, strlen(jsonPath));
            if(strstr(jsonPath, "[*]")) {
               fld->type = (FTN_STRLST | DDVARBIT);
               fld->elsz = TX_STRLST_ELSZ;
            }
            else {
               fld->type = (FTN_CHAR | DDVARBIT);
               fld->elsz = 1;
            }
		for (j = 0; j < DDFIELDS; j++)
			if (!tb->vfield[j])
			{
				tb->vfield[j] = fld;
				tb->vfname[j] = TXstrdup(TXPMBUFPN, fn, s);
#ifndef NO_TRY_FAST_CONV
				tb->nvfield = j + 1;
#endif
				break;
			}
            return fld;
jsonflddone:
            TXfree(parentFldName);
            fld = closefld(fld);
            return fld;

        }
	return (FLDPN);
}

/************************************************************************/

/* returns the name of field[n] or null if no field */

DDFD *
getflddesc(tb, n)
TBL *tb;
int n;
{
	return ddgetfd(tb->dd, ddgetorign(tb->dd, n));
}

/************************************************************************/

int
fldnum(tb, f)
TBL *tb;
FLD *f;
{
	unsigned int i;

	for (i = 0; i < tb->n; i++)
	{
		if (tb->field[i] == f)
			return tb->dd->fd[i].num;
	}
	return -1;
}

/******************************************************************/

/* returns the name of field[n] or null if no field */

char *
getfldname(tb, n)
TBL *tb;
int n;
{
	return ddgetname(tb->dd, ddgetorign(tb->dd, n));
}

/************************************************************************/


/* tells how many fields there are in a table */


int
ntblflds(tb)
TBL *tb;
{
	return (tb->n);
}

/******************************************************************/

int
tbgetorign(tb, n)
TBL *tb;
int	n;	/* field number, in user-added (not internal) order */
/* Like ddgetorign() but for a TBL: return internal field index for
 * user-added field index `n', or -1 if out of range.
 */
{
	static CONST char	fn[] = "tbgetorign";
	int i;

	if (!tb)
		return -1;
	if (!tb->rdd)
	{
		tb->rdd = (int *) TXcalloc(TXPMBUFPN, fn, tb->dd->n + 1, sizeof(int));

		if (tb->rdd)
		{
			for (i = 0; i < tb->dd->n + 1; i++)
			{
				tb->rdd[i] = ddgetorign(tb->dd, i);
			}
		}
	}
	if (tb->rdd)
	{
		i = (n >= 0 && n < tb->dd->n + 1 ? tb->rdd[n] : -1);
	}
	else
	{
		i = ddgetorign(tb->dd, n);
	}
	return i;
}

/************************************************************************/

#ifdef DDTEST			/* just test making a dd */

#include "ctype.h"

char *
next(s)
char *s;
{
	for (; isalnum(*s) || *s == ')'; s++);
	if (*s == '(')
	{
		*s++ = '\0';
		return (s);
	}
	for (*s++ = '\0'; *s && *s == ' '; s++);
	return (s);
}

int
main(argc, argv)
int argc;
char *argv[];
{
	int i;
	DD *dd;
	char ln[80];
	char *p;
	char *name;
	char *type;
	int size;

	if ((dd = opendd()) != DDPN)
	{
		while (gets(ln))
		{
			DDFD *lst = dd->fd;

			name = ln;
			type = next(name);
			size = atoi(next(type));
			putdd(dd, name, type, size, 0);
			for (i = 0; i < dd->n; i++)
				printf("%10s type=%02XX pos=%d size=%d\n",
				       lst[i].name, lst[i].type, lst[i].pos,
				       lst[i].size);
		}
		closedd(dd);
	}

	exit(TXEXIT_OK);
}

#endif /* ddtest */
/************************************************************************/
#ifdef TEST
#include "sys/types.h"
#include "sys/stat.h"


#define TBLNM "tmp"
#define STAT struct stat

#define LNSZ 512

#ifndef JOHN
char ln[LNSZ];
#else
char *ln;
#endif

int
main(argc, argv)
int argc;
char *argv[];
{
	static CONST char	fn[] = "main";
	TBL *tb = opentbl(TBLNM);
	int i;
	FLD *fnamef, *sizef, *mtimef, *linef;
	char fname[255];

	if (tb == TBLPN)
	{
		DD *dd;

		if ((dd = opendd()) == DDPN ||
		    !putdd(dd, "fname", "varchar", 65, 0) ||
		    !putdd(dd, "line", "varchar", 65, 0) ||
		    !putdd(dd, "size", "long", 1, 0) ||
		    !putdd(dd, "mtime", "long", 1, 0) ||
		    (tb = createtbl(dd, TBLNM)) == TBLPN)
			exit(TXEXIT_UNKNOWNERROR);
	}

	if ((fnamef = nametofld(tb, "fname")) == FLDPN ||
	    (mtimef = nametofld(tb, "mtime")) == FLDPN ||
	    (sizef = nametofld(tb, "size")) == FLDPN ||
	    (linef = nametofld(tb, "line")) == FLDPN)
		exit(TXEXIT_UNKNOWNERROR);

#ifndef JOHN
	for (i = 1; i < argc; i++)
#else
	while (fgets(fname, 255, stdin))
#endif
	{
          EPI_STAT_S info;

#ifndef JOHN
		if (!EPI_STAT(argv[i], &info))
#else
		int fh;

		fname[strlen(fname) - 1] = '\0';
		if (!EPI_STAT(fname, &info))
#endif
		{
#ifndef JOHNDATA
			ln = strdup("TESTDATA");
#else
			ln = (char *) TXmalloc(TXPMBUFPN, fn, info.st_size + 1);
			fh = open(fname, O_RDONLY);
			read(fh, ln, info.st_size);
			close(fh);
			ln[info.st_size] = '\0';
			if (ln[info.st_size - 1] == '\n')
				ln[info.st_size - 1] = '\0';
#endif
#ifndef JOHN
			putfld(fnamef, (void *) argv[i], strlen(argv[i]));
#else
			putfld(fnamef, (void *) fname, strlen(fname));
#endif
#ifndef JOHNDATA
			putfld(linef, (void *) ln, strlen(ln));
#else
			putfld(linef, (void *) ln, info.st_size);
#endif
			putfld(mtimef, (void *) &info.st_mtime, 1);
			putfld(sizef, (void *) &info.st_size, 1);
			if (puttblrow(tb, -1L) < 0L)
				exit(TXEXIT_UNKNOWNERROR);
#ifdef JOHN
			ln = TXfree(ln);
#endif
		}
	}

#ifndef JOHN

	if (rewindtbl(tb))
	{
		while (gettblrow(tb, -1L) > 0L)
		{
			size_t sz;
			long mtime = *((long *) getfld(mtimef, &sz));
			long size = *((long *) getfld(sizef, &sz));
			char *name = (char *) getfld(fnamef, &sz);
			char *line = (char *) getfld(linef, &sz);

			printf("%10ld %10ld %s %s\n", size, mtime, name,
			       line);
		}
	}
#endif
	if (tb != TBLPN)
		closetbl(tb);
}

#endif /* test */
