/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"


static const char	CorruptBlobzFmt[] =
	"Corrupt blobz data in file `%s' at offset 0x%wx: %s";


size_t
TXblobzGetUncompressedSize(TXPMBUF *pmbuf, const char *file, EPI_OFF_T offset,
			   const byte *buf, size_t bufSz, size_t fullSz)
/* Gets uncompressed size of blobz data `buf', which need not be the
 * entire block, but must be at least TX_BLOBZ_MAX_HDR_SZ bytes.
 * `fullSz' is full size of data, were it all to be in `buf'.
 * `file'/`offset' for messages.
 * Returns uncompressed size, -1 if unknown.
 */
{
	static const char	fn[] = "TXblobzGetUncompressedSize";
	EPI_HUGEUINT		hu;
	size_t			ret;

	/* NOTE: see also TXblobzDoInternalCompression() */
	if (bufSz < 1)				/* zero lengh: no ...TYPE */
	{
		ret = 0;
		goto finally;
	}

	switch ((TX_BLOBZ_TYPE)(buf[0]))
	{
	case TX_BLOBZ_TYPE_ASIS:
		ret = fullSz - 1;		/* -1: skip TX_BLOBZ_TYPE */
		break;
	case TX_BLOBZ_TYPE_GZIP:
		buf++;				/* skip TX_BLOBZ_TYPE */
		bufSz--;
		/* Decode our VSH original-size header: */
		if (bufSz < VSH_MAXLEN)
		{
			txpmbuf_putmsg(pmbuf, MERR + FRE, fn,
				       CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
				       "Truncated at VSH size");
			goto err;
		}
		INVSH(buf, hu, goto invshErr);
		ret = (size_t)hu;
		break;
	invshErr:
		txpmbuf_putmsg(pmbuf, MERR + FRE, fn, CorruptBlobzFmt,
			       file, (EPI_HUGEINT)offset,
			       "Bad VSH size encoding");
		goto err;
	default:			/* unknown TX_BLOBZ_TYPE... */
		txpmbuf_putmsg(pmbuf, MERR + FRE, fn, CorruptBlobzFmt,
			       file, (EPI_HUGEINT)offset,
	  "Unknown blobz type; data possibly created by newer Texis version");
		goto err;
	}
	goto finally;

err:
	ret = (size_t)(-1);
finally:
	return(ret);
}

static byte *
TXblobzDoInternalCompression(TXPMBUF *pmbuf, const char *file,
			     EPI_OFF_T offset, byte *inBuf, size_t inBufSz,
			     TXFILTERFLAG flags, size_t *outBufSz)
/* Internal use.  Does internal (ZLIB etc.) compress/uncompress
 * (per `flags') of `inBuf'.  Sets `*outBufSz' to output size
 * (not including nul terminator).  `file' and `offset' are for messages.
 * Returns alloced, nul-terminated output buffer.
 */
{
	static const char	fn[] = "TXblobzDoInternalCompression";
	TXZLIB			*zlibHandle = NULL;
	size_t			outBufUsedSz = 0, curOutLen;
	size_t			outBufAllocedSz = 0, outBufGuessSz = -1;
	size_t			outBufGzipOffset = -1;
	TXCTEHF			cteFlags;
	int			gzipTransRes = 0, noProgressNum = 0;
	int			badGzip = 0;
	byte			*curInBuf, *prevInBuf, *inBufEnd, *newOutBuf;
	byte			*curOutBuf, *outBuf = NULL, *p;
	EPI_HUGEUINT		hu;
#define MIN_OUTBUF_SZ		65536

	inBufEnd = inBuf + inBufSz;

	/* Check data type if decoding: */
	if (flags & TXFILTERFLAG_DECODE)
	{
		/* NOTE: see also TXblobzGetUncompressedSize(): */
		if (inBufSz < 1)		/* zero length: no ...TYPE */
		{
			outBufUsedSz = 0;
			outBufAllocedSz = 1;
			outBuf = (byte *)TXstrdup(pmbuf, fn, "");
			if (!outBuf) goto err;
			goto finally;
		}
		switch ((TX_BLOBZ_TYPE)(inBuf[0]))
		{
		case TX_BLOBZ_TYPE_ASIS:	/* just copy the data */
			outBufUsedSz = inBufSz - 1;    /* -1: TYPE stripped */
			outBufAllocedSz = outBufUsedSz + 1;    /* +1 for nul*/
			outBuf = (byte *)TXmalloc(pmbuf, fn, outBufAllocedSz);
			if (!outBuf) goto err;
			memcpy(outBuf, inBuf + 1, outBufUsedSz);
			outBuf[outBufUsedSz] = '\0';
			goto finally;
		case TX_BLOBZ_TYPE_GZIP:
			inBuf++;		/* skip TX_BLOBZ_TYPE_GZIP */
			inBufSz--;
			/* Skip our VSH original-size header: */
			if (inBufSz < VSH_MAXLEN)
			{
				txpmbuf_putmsg(pmbuf, MERR + FRE, fn,
					       CorruptBlobzFmt,
					       file, (EPI_HUGEINT)offset,
					       "Truncated at VSH size");
				goto err;
			}
			INVSH(inBuf, hu, goto invshErr);
			inBufSz = inBufEnd - inBuf;
			if (hu < 256*1024*1024)	/* sanity */
				outBufGuessSz = (size_t)hu;
			break;			/* gunzip below */
		invshErr:
			/* Bad VSH; try to recover.  Look for gzip start: */
			p = memchr(inBuf, TX_GZIP_START_BYTE, inBufSz);
			if (!p)			/* not gzip header either */
			{
				txpmbuf_putmsg(pmbuf, MERR + FRE, fn,
					       CorruptBlobzFmt,
					       file, (EPI_HUGEINT)offset,
				  "Bad VSH size encoding and no gzip header");
				goto err;
			}
			inBuf = p;
			inBufSz = inBufEnd - inBuf;
			txpmbuf_putmsg(pmbuf, MWARN + FRE, fn, CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
				   "Bad VSH size encoding; will work around");
			break;
		default:			/* unknown TX_BLOBZ_TYPE... */
			txpmbuf_putmsg(pmbuf, MERR + FRE, fn, CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
	  "Unknown blobz type; data possibly created by newer Texis version");
			goto err;
		}
	}
	else					/* compressing */
	{
		byte	*d;

		outBufAllocedSz = MIN_OUTBUF_SZ + TX_MAX(1 + VSH_MAXLEN, 64);
		if (!(outBuf = (byte *)TXmalloc(pmbuf, fn, outBufAllocedSz)))
			goto err;
		/* Header is TX_BLOBZ_TYPE_GZIP + VSH(inBufSz);
		 * while gzip does store original data size, it puts
		 * it at the end of the buffer, which would still
		 * incur a large read or another seek and read for us:
		 */
		d = outBuf;
		*(d++) = TX_BLOBZ_TYPE_GZIP;
		d = outvsh(d, inBufSz);
		outBufGzipOffset = outBufUsedSz = d - outBuf;
	}

	zlibHandle = TXzlibOpen(TXZLIBFORMAT_GZIP, flags,
				0 /* traceEncoding */, pmbuf);
	if (!zlibHandle) goto err;
	cteFlags = TXCTEHF_INPUT_EOF;

	curInBuf = inBuf;
	do
	{
		/* Expand output buffer if needed: */
		if (outBufAllocedSz - outBufUsedSz < MIN_OUTBUF_SZ)
		{
			/* Use `outBufGuessSz' hint if available: */
			if (outBufAllocedSz == 0 &&
			    outBufGuessSz != (size_t)(-1))
				outBufAllocedSz = outBufGuessSz + 1;
			else
				outBufAllocedSz += outBufAllocedSz/4 +
					MIN_OUTBUF_SZ;
			newOutBuf = (byte *)TXrealloc(pmbuf, fn, outBuf,
						      outBufAllocedSz);
			if (!newOutBuf)
			{
#ifndef EPI_REALLOC_FAIL_SAFE
				outBuf = NULL;
				outBufAllocedSz = outBufUsedSz = 0;
#endif /* EPI_REALLOC_FAIL_SAFE */
				goto err;
			}
			outBuf = newOutBuf;
		}

		/* Compress/uncompress input: */
		prevInBuf = curInBuf;
		curOutBuf = outBuf + outBufUsedSz;
		gzipTransRes = TXzlibTranslate(zlibHandle, cteFlags, &curInBuf,
					       inBufEnd - curInBuf, &curOutBuf,
					       outBufAllocedSz - outBufUsedSz);

		/* Update output buffer.  Do before checking result
		 * code, as we might consume/get data even on error:
		 */
		curOutLen = curOutBuf - (outBuf + outBufUsedSz);
		outBufUsedSz = curOutBuf - outBuf;

		/* Check for forward progress: */
		if (curInBuf == prevInBuf && curOutLen == 0) noProgressNum++;
	}
	while (gzipTransRes == 1 && noProgressNum <= 5);

	/* Check for errors: */
	switch (gzipTransRes)
	{
	case 0:					/* error */
	default:
		badGzip = 1;
		break;
	case 1:					/* ok */
	case 2:					/* ok, output EOF */
		/* wtf if not input eof, keep reading to consume it? */
		break;
	}
	if (noProgressNum > 5)
	{
		if (flags & TXFILTERFLAG_DECODE)
			txpmbuf_putmsg(pmbuf, MERR + FRE, fn, CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
				     "No forward progress decoding gzip data");
		else
			txpmbuf_putmsg(pmbuf, MWARN + FWE, fn,
				       "Internal error with blobz file `%s': No forward progress gzipping data; will store as-is",
				       file /* offset likely -1 */);
		badGzip = 1;
	}
	/* Sanity check: encoded data must start with TX_BLOBZ_TYPE_GZIP,
	 * so we can recognize it on read:
	 */
	if (outBufGzipOffset != (size_t)(-1) &&
	    (outBufGzipOffset > outBufUsedSz ||
	     outBuf[outBufGzipOffset] != TX_GZIP_START_BYTE))
	{
		txpmbuf_putmsg(pmbuf, MWARN + FWE, fn,
			       "Internal error with blobz file `%s': buffer lacks gzip header byte after gzipping; will store data as-is",
			       file /* offset likely -1 */);
		badGzip = 1;
	}

	/* Act on error, and/or maybe store data as-is instead: */
	if (flags & TXFILTERFLAG_DECODE)
	{
		if (badGzip) goto err;
	}
	else if (badGzip ||
		 /* Optimization: if gzip data is *larger*, store as-is: */
		 outBufUsedSz > 1 + inBufSz)
	{					/* store as-is */
		outBuf = TXfree(outBuf);
		outBufUsedSz = 1 + inBufSz;	/* +1 for ...TYPE */
		outBufAllocedSz = outBufUsedSz + 1;	/* +1 for nul */
		outBuf = (byte *)TXmalloc(pmbuf, fn, outBufAllocedSz);
		if (!outBuf) goto err;
		outBuf[0] = TX_BLOBZ_TYPE_ASIS;
		memcpy(outBuf + 1, inBuf, inBufSz);
		outBuf[outBufUsedSz] = '\0';
		goto finally;
	}

	/* Right-size and nul-terminate `outBuf': */
	if (outBufAllocedSz != outBufUsedSz + 1)
	{
		outBufAllocedSz = outBufUsedSz + 1;
		newOutBuf = (byte *)TXrealloc(pmbuf, fn, outBuf,
					      outBufAllocedSz);
		if (!newOutBuf)
		{
#ifndef EPI_REALLOC_FAIL_SAFE
			outBuf = NULL;
			outBufAllocedSz = outBufUsedSz = 0;
#endif /* EPI_REALLOC_FAIL_SAFE */
			goto err;
		}
		outBuf = newOutBuf;
	}
	outBuf[outBufUsedSz] = '\0';

	goto finally;

err:
	outBuf = TXfree(outBuf);
	outBufAllocedSz = outBufUsedSz = 0;
finally:
	zlibHandle = TXzlibClose(zlibHandle);
	*outBufSz = outBufUsedSz;
	return(outBuf);
#undef MIN_OUTBUF_SZ
}

/******************************************************************/
/*
	Given a FTN_BLOBI pointer, returns pointer to data, and
	size in *sz;
*/

void *
TXagetblobz(v, sz)
ft_blobi *v;
size_t *sz;	/* (out) size of returned data (not including nul) */
/* Returns alloced, nul-terminated uncompressed data from `v'.
 * Should ideally be internal blob use only.
 */
{
	static CONST char	fn[] = "TXagetblobz";
	void *rc;
	FLD *tvcfld = NULL, *tvdfld = NULL;
	char *buf;
	char *unzipcmd;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	if (!v) goto err;			/* No pointer given */
	if(!v->dbf)	/* Points to memory segment */
	{
		*sz = v->len;
		/* `v->off' contains it already, as a pointer in mem.
		 * But caller expects an alloced copy:
		 */
		buf = (char *)TXmalloc(pmbuf, fn, v->len + 1);
		if (!buf) goto err;
		memcpy(buf, (void *)v->off, v->len);
		buf[v->len] = '\0';
		return(buf);
	}
        /* KNG 991019 offset could be -1 from failed putdbf(); that
         * would fetch next record here, which is unpredictable:
         */
        if (v->off < (EPI_OFF_T)0L)
          {
            /* WTF until Bug 4037 implemented, offset -1 may explicitly
             * mean empty; cannot distinguish from error:
             */
            if (v->off == (EPI_OFF_T)(-1))
              {
                *sz = 0;
                /* Caller expects alloced data? */
                return(TXstrdup(pmbuf, fn, ""));
              }
            else
              {
		txpmbuf_putmsg(pmbuf, MWARN + FRE, fn, "Missing blob offset");
		goto err;
              }
          }

	if (v->otype != FTN_BLOBZ)
	{
		txpmbuf_putmsg(pmbuf, MERR +UGE, fn,
		 "Internal error: ft_blobi otype is %s instead of expected %s",
			      ddfttypename(v->otype), ddfttypename(FTN_BLOBZ));
		goto err;
	}

	rc = getdbf((DBF *)v->dbf, v->off, sz);
	if (!rc) goto err;			/* block read failed */
	if(TXApp && TXApp->blobUncompressExe && TXApp->blobUncompressExe[0])
	{					/* external uncompress */
		unzipcmd = TXApp->blobUncompressExe;
		tvcfld = createfld("varchar",15,1);
		setfld(tvcfld, unzipcmd, strlen(unzipcmd));
		TXsetshadownonalloc(tvcfld);
		tvdfld = createfld("varbyte",80,1);
		setfldandsize(tvdfld, rc, *sz + 1, FLD_FORCE_NORMAL);
		dobshell(tvcfld, tvdfld, NULL, NULL, NULL);
		buf = getfld(tvcfld, sz);
		TXsetshadownonalloc(tvcfld);
		TXsetshadownonalloc(tvdfld);
		closefld(tvdfld);
		closefld(tvcfld);
		return buf;
	}
	else					/* internal uncompress */
	{
		buf = (char *)TXblobzDoInternalCompression(pmbuf,
				getdbffn((DBF *)v->dbf), v->off, rc, *sz,
				TXFILTERFLAG_DECODE, sz);
		return(buf);
	}
err:
	*sz = 0;
	return(NULL);
}

/******************************************************************/

/*	Converts an EPI_OFF_T (BLOB handle) and table to a FTN_BLOBI
	struct.  Basically just copies the args into struct.
*/

void *
bztobi(b, intbl)
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
bitobz(bi, outtbl)
void	*bi;
TBL	*outtbl;
{
	ft_blobi	*rc=bi;
	DBF		*df;
	char		*buf;
	EPI_OFF_T		r = -1;
	size_t		sz;
	FLD		*tvcfld = NULL, *tvdfld = NULL;
	TXPMBUF		*pmbuf = TXPMBUFPN;
	byte		*compressedBuf = NULL;
	size_t		compressedBufSz = 0;

	df = rc->dbf;	/* Blob DBF */
	if(df && !rc->memdata)
	{
		/* If the blobi already points to return offset */
		if(df == outtbl->bf)
		{
			r = rc->off;
			goto finally;
		}
		buf = getdbf(df, rc->off, &sz);
		if (!buf) goto err;
	}
	else
	{
		/* It is in memory.  Assume string WTF */
		buf=rc->memdata;
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
	{
		r = -1;
	}
	else
	{
		if(TXApp && TXApp->blobCompressExe && TXApp->blobCompressExe[0])
		{				/* external compress */
			char *zipcmd;

			zipcmd = TXApp->blobCompressExe;
			tvcfld = createfld("varchar",15,1);
			setfld(tvcfld, strdup(zipcmd), strlen(zipcmd));
			tvdfld = createfld("varchar",sz,1);
			setfld(tvdfld, buf, sz);
			TXsetshadownonalloc(tvdfld);
			dobshell(tvcfld, tvdfld, NULL, NULL, NULL);
			buf = getfld(tvcfld, &sz);
		}
		else				/* internal compress */
		{
			compressedBuf = TXblobzDoInternalCompression(pmbuf,
					getdbffn(outtbl->bf), -1L,
					(byte *)buf, sz, TXFILTERFLAG_ENCODE,
					&compressedBufSz);
			buf = (char *)compressedBuf;
			sz = compressedBufSz;
		}
		r = putdbf(outtbl->bf, -1L, buf, sz);
	}
	goto finally;

err:
	r = (EPI_OFF_T)(-1);
finally:
	tvcfld = closefld(tvcfld);
	tvdfld = closefld(tvdfld);
	compressedBuf = TXfree(compressedBuf);
	compressedBufSz = 0;
	return r;
}

/******************************************************************/

