/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/
/*
	Given a FTN_BLOBI pointer, returns pointer to data, and
	size in *sz;
*/

void *
TXblobiGetPayload(ft_blobi *v, size_t *sz)
/* `v'          (in) ft_blobi to get value of
 * `sz'         (out, opt.) byte size of value
 * Returns pointer to payload data of blob, or NULL on error.
 * Sets `*sz' to its size (w/o nul).  Data is nul-terminated, and either
 * owned by `v' or constant.  Fetches from DBF if needed.
 * NOTE: WTF like all blob-reading functions, no locks are used, nor do we
 *       know if someone closed our DBF handle as part of SQL handle close...
 */
{
	static CONST char	fn[] = "TXblobiGetPayload";
	static CONST char	emptyStr[] = "";
	void			*ret;

	if(!v)	/* No pointer given */
		goto err;
	if (TXblobiIsInMem(v))			/* already alloced and term.*/
		goto ok;
	switch(v->otype)
	{
	default:
		putmsg(MERR, fn, "Unknown blobi FTN type %d", (int)v->otype);
		goto err;
	case FTN_BLOB:
		/* KNG 991019 offset could be -1 from failed putdbf(); that
		 * would fetch next record here, which is unpredictable:
		 */
		if (v->off < (EPI_OFF_T)0L)
		{
			/* WTF until Bug 4037 implemented, offset -1
			 * may explicitly mean empty; cannot
			 * distinguish from error:
			 */
			if (v->off == (EPI_OFF_T)(-1))
			{
				if (sz) *sz = 0;
				ret = (void *)emptyStr;
				goto finally;
			}
			else
			{
				putmsg(MWARN + FRE, fn, "Missing blob offset");
				goto err;
			}
		}
		TXblobiFreeMem(v);
		/* KNG 20160311 always alloc and own the buffer;
		 * do not rely on DBF buffer sticking around:
		 */
		v->memdata = agetdbf((DBF *)v->dbf, v->off, &v->len);
		if (!v->memdata) goto err;
		v->ndfree = 1;
		break;
	case FTN_BLOBZ:
		v->memdata = TXagetblobz(v, &v->len);
		if (!v->memdata) goto err;
		v->ndfree = 1;
		break;
	}
ok:
	if (sz) *sz = v->len;
	ret = v->memdata;
	goto finally;

err:
	if (sz) *sz = 0;
	ret = NULL;
finally:
	return(ret);
}

int
TXblobiFreeMem(ft_blobi *bi)
/* Frees `bi' data, if in mem.
 * Returns 0 on error.
 */
{
	if (bi->ndfree)
	{
		bi->memdata = TXfree(bi->memdata);
		bi->ndfree = 0;
	}
	else
		bi->memdata = NULL;
	return(1);
}

void *
TXblobiGetMem(ft_blobi *bi, size_t *sz)
/* Sets `*sz' (opt.) to in-mem size, and returns pointer to mem.
 * Caller does not own data.  Does not load from DBF.
 */
{
	if (!bi->dbf || bi->memdata)
	{
		if (sz) *sz = bi->len;
		return(bi->memdata);
	}
	else
	{
		if (sz) *sz = 0;
		return(NULL);
	}
}

int
TXblobiSetMem(ft_blobi *bi, void *mem, size_t sz, int isAlloced)
/* Sets `bi' mem to `mem'.  `isAlloced' is nonzero if `mem' is alloced;
 * `bi' will take ownership.  `mem' must be nul-terminated.
 * Returns nonzero on success, zero on error (`mem' freed iff `isAlloced').
 */
{
	TXblobiFreeMem(bi);
	bi->memdata = mem;
	bi->len = sz;
	bi->ndfree = !!isAlloced;
	return(1);
}

void *
TXblobiRelinquishMem(ft_blobi *bi, size_t *sz)
/* Relinquishes in-memory data to caller: caller owns it and must free.
 * Sets `*sz' (opt.) to its size.  All "stealing" of ft_blobi.memdata
 * should go through this wrapper.
 * Returns pointer to data, or NULL if no data or not alloced.
 */
{
	void	*ret = NULL;
	size_t	len = 0;

	if (bi->memdata && bi->ndfree)
	{
		ret = bi->memdata;
		bi->memdata = NULL;
		bi->ndfree = 0;
		len = bi->len;
		bi->len = 0;
	}
	if (sz) *sz = len;
	return(ret);
}

int
TXblobiIsInMem(ft_blobi *bi)
/* Returns nonzero if data for `bi' is already in memory.
 */
{
	return(!bi->dbf || bi->memdata);
}

DBF *
TXblobiGetDbf(ft_blobi *bi)
{
	return(bi->dbf);
}

int
TXblobiSetDbf(ft_blobi *bi, DBF *dbf)
{
	bi->dbf = dbf;
	return(1);
}

FTN
TXblobiGetStorageType(ft_blobi *bi)
{
	return(bi->otype);
}

/******************************************************************/

/*	Converts an EPI_OFF_T (BLOB handle) and table to a FTN_BLOBI
	struct.  Basically just copies the args into struct.
*/

void *
btobi(b, intbl)
EPI_OFF_T	b;
TBL	*intbl;
{
	static char Fn[]="btobi";
	ft_blobi	*rc = NULL;

	if(!intbl->bf)	/* No blob file, so no blob */
		return (void *)NULL;
	rc = (ft_blobi *)calloc(1, sizeof(ft_blobi));
	if (rc == (ft_blobi *)NULL)
	{
		putmsg(MWARN+MAE, Fn, "Out of Memory");
		return rc;
	}
	rc->off = b;
	rc->dbf = intbl->bf;
	return rc;
}

/******************************************************************/

/*	This does the actual work of adding a blob to a table, and
	returns the offset that it was added.
*/

EPI_OFF_T
bitob(bi, outtbl)
void	*bi;
TBL	*outtbl;
{
	ft_blobi	*rc=bi;
	DBF		*df;
	char		*buf;
	EPI_OFF_T		r = -1;
	size_t		sz;

	df = rc->dbf;	/* Blob DBF */
	if(df && !rc->memdata)
	{
		/* If the blobi already points to return offset */
		if(df == outtbl->bf)
			return rc->off;
		buf = getdbf(df, rc->off, &sz);
		if(!buf)
			return -1;
	}
	else
	{
		/* It is in memory.  Assume string WTF */
		buf=(char *)rc->memdata;
		sz=rc->len;
	}
	if(sz == 0) /* KDBF Does not like empty blobs, will cause it to
			store the NULL.  Should not be a problem. */
			/* KNG Bug 4030 it is a problem -- mapping empty
			 * data to 1-byte nul via `sz++' is erroneous.
			 * Just use offset -1 since that is treated as
			 * error and thus empty-blob elsewhere, to avoid
			 * KDBF error from 0-length write here.
			 * WTF use offset 0 once Bug 4037 implemented,
			 * to distinguish error (-1) from empty (0):
			 */
		r = -1;
	else
		r = putdbf(outtbl->bf, -1L, buf, sz);
	return r;
}

/******************************************************************/

size_t
TXblobiGetPayloadSize(ft_blobi *v)
/* Returns payload size of blob `v', or -1 on error.
 * Tries not to read whole blob into mem if possible, but might, and might
 * save it to `v->memdata'.
 */
{
	static const char	fn[] = "TXblobiGetPayloadSize";
	byte buf[5 + TX_BLOBZ_MAX_HDR_SZ];
	size_t	fullSz, ret;
	DBF *df;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	df = v->dbf;
	if (!df || v->memdata)			/* Points to memory segment */
	{
		ret = (size_t)v->len;
		goto finally;
	}

	switch(v->otype)
	{
	default:
		txpmbuf_putmsg(pmbuf, MERR, fn, "Unknown blob type %d",
			       (int)v->otype);
		goto err;
	case FTN_BLOB:
		if(df->dbftype == DBF_KAI)
		{
		/* Just read a byte or two; readdbf() returns *total* size: */
			ret = readdbf(df, v->off, buf, 2);
			if (ret == 0) goto err;
		}
		else
		{
			if (!getdbf(df, v->off, &ret)) goto err;
		}
		break;
	case FTN_BLOBZ:
		if (df->dbftype == DBF_KAI)
		{
			/* Just read the blobz header; get size from it: */
			fullSz = readdbf(df, v->off, buf, TX_BLOBZ_MAX_HDR_SZ);
			if (fullSz == 0) goto err;
			ret = TXblobzGetUncompressedSize(pmbuf, getdbffn(df),
					v->off, buf,
					TX_MIN(fullSz, TX_BLOBZ_MAX_HDR_SZ),
					fullSz);
		}
		else
		{
			/* Might as well save the data for future use: */
			TXblobiFreeMem(v);
			v->memdata = TXagetblobz(v, &v->len);
			if (!v->memdata) goto err;
			ret = v->len;
		}
		break;
	}
	goto finally;

err:
	ret = (size_t)(-1);
finally:
	return(ret);
}

/******************************************************************/

