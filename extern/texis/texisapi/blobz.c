/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"
#include "httpi.h"


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
	EPI_HUGEUINT	hu;
	size_t		ret;
	char		verBuf[128];

	/* NOTE: see also TXblobzDoCompressOrUncompress() */
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
	case TX_BLOBZ_TYPE_EXTERNAL:
		buf++;				/* skip TX_BLOBZ_TYPE */
		bufSz--;
		/* Decode our VSH original-size header: */
		INVSH(buf, hu, goto invshErr);
		ret = (size_t)hu;
		break;
	invshErr:
		txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
			       CorruptBlobzFmt, file, (EPI_HUGEINT)offset,
			       "Bad VSH size encoding");
		goto err;
	default:			/* unknown TX_BLOBZ_TYPE... */
		TXgetTexisVersionNumString(verBuf, sizeof(verBuf),
					   TXbool_False /* !vortexStyle */,
					   TXbool_False /* !forHtml */);
		txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
			       CorruptBlobzFmt, file, (EPI_HUGEINT)offset,
   "Unknown blobz type; data possibly created by version newer than Texis %s",
			       verBuf);
		goto err;
	}
	goto finally;

err:
	ret = (size_t)(-1);
finally:
	return(ret);
}

static byte *
TXblobzDoExternalCompressOrUncompress(TXPMBUF *pmbuf, const char *file,
				      EPI_OFF_T offset, const byte *inBuf,
				      size_t inBufSz, TXFILTERFLAG flags,
				      size_t *outBufSz)
/* Internal use.  Does [Texis] Blobz External [Un]compress Exe compression
 * or uncompression (per `flags') of `inBuf', which is raw (un)compressed
 * data (i.e. sans blobz header).  Sets `*outBufSz' to output size (not
 * including nul terminator).  `file' and `offset' (-1 if unknown) are
 * for messages.
 * Returns alloced, nul-terminated output buffer, with blobz header if
 * compressing.
 */
{
	char		*cmd;
	TXPOPENARGS	po;
	TXPIPEARGS	pa;
	int		exitCode, isSig, getExitCodeStatus = -1;
	byte		prefix[TX_BLOBZ_MAX_HDR_SZ], *d, *outBuf;
	char		offsetBuf[40 + EPI_HUGEUINT_BITS];

	TXPOPENARGS_INIT(&po);
	TXPIPEARGS_INIT(&pa);

	/* Get the executable to use: */
	cmd = (TXApp ? ((flags & TXFILTERFLAG_DECODE) ?
			TXApp->blobzExternalUncompressExe :
			TXApp->blobzExternalCompressExe) :
	       NULL);
	if (!cmd || !*cmd)
	{
		if (offset == (EPI_OFF_T)(-1))
			*offsetBuf = '\0';
		else
			htsnpf(offsetBuf, sizeof(offsetBuf),
			       " at offset 0x%wu", (EPI_HUGEUINT)offset);
		txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
			       ((flags & TXFILTERFLAG_DECODE) ?
				"Cannot uncompress external-compressor blobz data from file `%s'%s: [Texis] Blobz External Uncompress Exe is undefined" :
				"Cannot compress external-compressor blobz data to file `%s'%s: [Texis] Blobz External Compress Exe is undefined"),
			       file, offsetBuf);
		goto err;
	}

	/* Set up and start external command: */
	po.desc = ((flags & TXFILTERFLAG_DECODE) ?
		   "Blobz External Uncompress Exe" :
		   "Blobz External Compress Exe");
	po.argv = tx_dos2cargv(cmd, 1 /* remove quotes */);
	if (!po.argv) goto err;
	po.cmd = po.argv[0];
	po.flags = (TXPDF_QUOTEARGS | TXPDF_REAP | TXPDF_SAVE |
		    TXPDF_SEARCHPATH);
	po.fh[STDIN_FILENO] = TXFHANDLE_CREATE_PIPE_VALUE;
	po.fh[STDOUT_FILENO] = TXFHANDLE_CREATE_PIPE_VALUE;
	po.fh[STDERR_FILENO] = TXFHANDLE_STDERR;	/* wtf read it too */
	/* wtf set childpostfork, childerrexit? */
	if (!TXpopenduplex(&po, &pa)) goto err;
	/* stdin from our `uncompressedData': */
	htbuf_setdata(pa.pipe[STDIN_FILENO].buf, (char *)inBuf, inBufSz,
		      inBufSz + 1 /* wtf assumed nul-term. */,
		      0 /* const data */);

	if (!(flags & TXFILTERFLAG_DECODE))
	{
		/* Prepend blobz header to stdout, to avoid dup bloat: */
		d = prefix;
		*(d++) = TX_BLOBZ_TYPE_EXTERNAL;
		d = outvsh(d, inBufSz);
		if (!htbuf_write(pa.pipe[STDOUT_FILENO].buf,
				 (char *)prefix, d - prefix))
			goto err;
	}

	/* Run the command: */
	while (TXFHANDLE_IS_VALID(pa.pipe[STDIN_FILENO].fh) ||
	       TXFHANDLE_IS_VALID(pa.pipe[STDOUT_FILENO].fh))
	{					/* while cmd runs */
		if (TXFHANDLE_IS_VALID(pa.pipe[STDIN_FILENO].fh) &&
		    htbuf_getsendsz(pa.pipe[STDIN_FILENO].buf) <= 0 /* EOF */)
			TXpendio(&pa, 0); 	/* send EOF to child stdin */
		TXpreadwrite(&pa, -1 /* no timeout */);
	}

	/* End cmd and report non-zero exit: */
	TXpendio(&pa, 1 /* close all handles */);
	exitCode = TXEXIT_OK;
	isSig = 0;
	getExitCodeStatus = !!TXpgetexitcode(&pa, 1, &exitCode, &isSig);
	if (!getExitCodeStatus)
		/* already reported by TXpgetexitcode() */;
	else if ((exitCode != TXEXIT_OK && exitCode!=TXEXIT_TIMEOUT) || isSig)
		TXreportProcessExit(pmbuf, __FUNCTION__, po.desc,
				    po.cmd, pa.pid, exitCode, isSig, NULL);

	/* Take the (un)compressed data: */
	*outBufSz = htbuf_getdata(pa.pipe[STDOUT_FILENO].buf,
				  (char **)&outBuf, 1);
	goto finally;

err:
	if (pa.pid != (PID_T)0) TXpkill(&pa, 1);
	outBuf = NULL;
	*outBufSz = 0;
finally:
	/* Last-ditch effort at reporting exit code, if not reported: */
	if (pa.pid && getExitCodeStatus == -1)
	{
		/* do not wait: we are done here */
		getExitCodeStatus = !!TXpgetexitcode(&pa, 0x3, &exitCode,
						     &isSig);
		if (!getExitCodeStatus)
			/* already reported by TXpgetexitcode() */;
		else if ((exitCode != TXEXIT_OK && exitCode != TXEXIT_TIMEOUT)
			 || isSig)
			TXreportProcessExit(pmbuf, __FUNCTION__, po.desc,
					    po.cmd, pa.pid, exitCode, isSig,
					    NULL);
	}
	TXpcloseduplex(&pa, 1);
	po.argv = TXfreeStrList(po.argv, -1);
	return(outBuf);
}

static byte *
TXblobzDoCompressOrUncompress(TXPMBUF *pmbuf, const char *file,
			      EPI_OFF_T offset, byte *inBuf, size_t inBufSz,
			      TXFILTERFLAG flags, size_t *outBufSz)
/* Internal use.  Does internal (ZLIB gzip) or external (Blobz
 * External [Un]Compress Exe) compression/uncompression (per `flags')
 * of `inBuf'.
 * `inBuf' is uncompressed data iff TXFILTERFLAG_ENCODE,
 * compressed data with TX_BLOBZ_TYPE + VSH header if TXFILTERFLAG_DECODE.
 * Sets `*outBufSz' to output size (not including nul terminator).
 * `file' and `offset' (-1 if unknown) are for messages.
 * Returns alloced, nul-terminated output buffer.
 */
{
	TXZLIB		*zlibHandle = NULL;
	size_t		outBufUsedSz = 0, curOutLen;
	size_t		outBufAllocedSz = 0, outBufGuessSz = -1;
	size_t		outBufGzipOffset = -1;
	TXCTEHF		cteFlags;
	int		gzipTransRes = 0, noProgressNum = 0;
	int		badGzip = 0;
	byte		*curInBuf, *prevInBuf, *inBufEnd, *newOutBuf;
	byte		*curOutBuf, *outBuf = NULL, *p;
	EPI_HUGEUINT	hu;
#define MIN_OUTBUF_SZ	65536
	TX_BLOBZ_TYPE	blobzType;

	inBufEnd = inBuf + inBufSz;

	/* Check data type if decoding: */
	if (flags & TXFILTERFLAG_DECODE)
	{
		/* NOTE: see also TXblobzGetUncompressedSize(): */
		if (inBufSz < 1)		/* zero length: no ...TYPE */
		{
			outBufUsedSz = 0;
			outBufAllocedSz = 1;
			outBuf = (byte *)TXstrdup(pmbuf, __FUNCTION__, "");
			if (!outBuf) goto err;
			goto finally;
		}
		blobzType = (TX_BLOBZ_TYPE)(inBuf[0]);
		switch (blobzType)
		{
		case TX_BLOBZ_TYPE_ASIS:	/* just copy the data */
			outBufUsedSz = inBufSz - 1;    /* -1: TYPE stripped */
			outBufAllocedSz = outBufUsedSz + 1;    /* +1 for nul*/
			outBuf = (byte *)TXmalloc(pmbuf, __FUNCTION__,
						  outBufAllocedSz);
			if (!outBuf) goto err;
			memcpy(outBuf, inBuf + 1, outBufUsedSz);
			outBuf[outBufUsedSz] = '\0';
			goto finally;
		case TX_BLOBZ_TYPE_GZIP:
		case TX_BLOBZ_TYPE_EXTERNAL:
			inBuf++;		/* skip TX_BLOBZ_TYPE */
			inBufSz--;
			/* Skip our VSH original-size header: */
			INVSH(inBuf, hu, goto invshErr);
			inBufSz = inBufEnd - inBuf;	/* `inBuf' advanced */
			if (hu < 256*1024*1024)	/* sanity */
				outBufGuessSz = (size_t)hu;
			if (blobzType == TX_BLOBZ_TYPE_EXTERNAL)
			{
				outBuf = TXblobzDoExternalCompressOrUncompress(
					pmbuf, file, offset, inBuf, inBufSz,
					flags, &outBufUsedSz);
				goto finally;
			}
			break;			/* gunzip below */
		invshErr:
			/* Bad VSH; try to recover.  Look for gzip start: */
			p = memchr(inBuf, TX_GZIP_START_BYTE, inBufSz);
			if (!p)			/* not gzip header either */
			{
				txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
					       CorruptBlobzFmt,
					       file, (EPI_HUGEINT)offset,
				  "Bad VSH size encoding and no gzip header");
				goto err;
			}
			inBuf = p;
			inBufSz = inBufEnd - inBuf;
			txpmbuf_putmsg(pmbuf, MWARN + FRE, __FUNCTION__,
				       CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
		     "Bad VSH size encoding for gzip data; will work around");
			break;
		default:			/* unknown TX_BLOBZ_TYPE... */
			txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
				       CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
	"Unknown blobz type %d; data possibly created by newer Texis version",
				       (int)blobzType);
			goto err;
		}
	}
	else					/* compressing */
	{
		byte	*d;

		/* Use external compression, if defined and over threshold: */
		if (TXApp &&
		    TXApp->blobzExternalCompressExe &&
		    *TXApp->blobzExternalCompressExe &&
		    inBufSz >= TXApp->blobzExternalCompressMinSize)
		{
			outBuf = TXblobzDoExternalCompressOrUncompress(pmbuf,
				  file, offset, inBuf, inBufSz, flags,
				  &outBufUsedSz);
			goto finally;
		}

		outBufAllocedSz = MIN_OUTBUF_SZ +
			TX_MAX(TX_BLOBZ_MAX_HDR_SZ, 64);
		if (!(outBuf = (byte *)TXmalloc(pmbuf, __FUNCTION__,
						outBufAllocedSz)))
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
			newOutBuf = (byte *)TXrealloc(pmbuf, __FUNCTION__,
						      outBuf,
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
			txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
				       CorruptBlobzFmt,
				       file, (EPI_HUGEINT)offset,
				     "No forward progress decoding gzip data");
		else
			txpmbuf_putmsg(pmbuf, MWARN + FWE, __FUNCTION__,
				       "Internal error with blobz file `%s': No forward progress gzipping data; will store as-is",
				       file /* offset likely -1 */);
		badGzip = 1;
	}
	/* Sanity check: encoded data must start with TX_GZIP_START_BYTE,
	 * so zlib can recognize it on read:
	 */
	if (outBufGzipOffset != (size_t)(-1) &&
	    (outBufGzipOffset > outBufUsedSz ||
	     outBuf[outBufGzipOffset] != TX_GZIP_START_BYTE))
	{
		txpmbuf_putmsg(pmbuf, MWARN + FWE, __FUNCTION__,
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
		outBuf = (byte *)TXmalloc(pmbuf, __FUNCTION__,
					  outBufAllocedSz);
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
		newOutBuf = (byte *)TXrealloc(pmbuf, __FUNCTION__, outBuf,
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
	char *buf;
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
	buf = (char *)TXblobzDoCompressOrUncompress(pmbuf,
					   getdbffn((DBF *)v->dbf),
					   v->off, rc, *sz,
					   TXFILTERFLAG_DECODE, sz);
	return(buf);
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
		compressedBuf = TXblobzDoCompressOrUncompress(pmbuf,
					getdbffn(outtbl->bf), -1L,
					(byte *)buf, sz, TXFILTERFLAG_ENCODE,
					&compressedBufSz);
		buf = (char *)compressedBuf;
		sz = compressedBufSz;
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

