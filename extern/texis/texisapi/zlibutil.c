#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "zlib.h"
#include "cgi.h"
#include "texint.h"


/* Wrappers and utils for zlib */

struct TXZLIB_tag
{
  z_stream      zlibStream;
  TXPMBUF       *pmbuf;
  TXZLIBFORMAT  format;
  TXbool        forDecode;
  TXbool        isVirgin;       /* true: no translate calls (thus no DoInit) */
  int           traceEncoding;  /* just some bits (0x8) */
  byte          *orgInBuf, *orgOutBuf;
  TXCTEHF       flags;
};

static const char       BufSzOverflow[] = "Buffer size overflow";
static const char       FormatAnyOnlyForDecode[] =
  "TXZLIBFORMAT_ANY format only legal for decode";

typedef enum
  {
    TXZR_Noun,
    TXZR_Verb,
    TXZR_Info
  }
  TXZR;

static int TXzlibReportError(const char *fn, const char *pfx,
                             TXZR how, TXZLIB *zlib, int zRet);

static void
TXzlibEnd(TXZLIB *zlib)
/* Close zlib part of `zlib', without freeing struct.
 */
{
  z_stream      *zs = &zlib->zlibStream;
  int           res;

  /* Clear buffer pointers just in case it tries to use them: */
  zs->next_in = (Bytef *)NULL;
  zs->avail_in = 0;
  zs->next_out = (Bytef *)NULL;
  zs->avail_out = 0;

  if (zlib->isVirgin) return;                   /* noop if not inited */

  /* Free internal bufs etc.: */
  if (zlib->forDecode)
    res = inflateEnd(zs);
  else
    res = deflateEnd(zs);
  zlib->isVirgin = TXbool_True;
  switch (res)
    {
    case Z_OK:
    case Z_DATA_ERROR:                          /* possible for deflate? */
      break;
    default:
      TXzlibReportError(__FUNCTION__, "Cannot end/close", TXZR_Noun, zlib,
                        res);
      break;
    }
}

TXZLIB *
TXzlibClose(zlib)
TXZLIB  *zlib;          /* (in, opt.) handle to close */
/* Closes an open handle to a handler for the `gzip' or
 * `deflate' encoding.
 * Returns NULL.
 */
{
  if (zlib != TXZLIBPN)
    {
      TXzlibEnd(zlib);
      zlib->pmbuf = txpmbuf_close(zlib->pmbuf);
      zlib = TXfree(zlib);
    }
  return(TXZLIBPN);
}

static int
TXzlibReportError(fn, pfx, how, zlib, zRet)
const char      *fn;    /* (in) C function */
const char      *pfx;   /* (in) error message prefix e.g. "Cannot translate" */
TXZR            how;    /* (in) how to report */
TXZLIB          *zlib;  /* (in) object */
int             zRet;   /* (in) Z_... return value */
/* Returns txpmbuf_putmsg() return value (0 on error, 1 if ok).
 */
{
  z_stream      *zs = &zlib->zlibStream;
  char          *msg, *code, *action, *compType;
  int           ret;
  char          codeMsgBuf[128], flagsBuf[128], *d, *e;
  TXCTEHF       flagsLeft;

  switch (zRet)
    {
    case Z_OK:                                  /* success */
      code = "Z_OK";
      msg = "Ok";
      break;
    case Z_BUF_ERROR:                           /* buffers full, try again */
      code = "Z_BUF_ERROR";
      msg = "I/O buffers full; empty and try again";
      break;
    case Z_STREAM_END:                          /* success and EOF */
      code = "Z_STREAM_END";
      msg = "End of output stream";
      break;
    case Z_NEED_DICT:
      code = "Z_NEED_DICT";
      msg = "Dictionary needed";
      break;
    case Z_ERRNO:
      code = "Z_ERRNO";
      htsnpf(codeMsgBuf, sizeof(codeMsgBuf), "errno %d: %s",
             (int)errno, strerror(errno));
      msg = codeMsgBuf;
      break;
    case Z_STREAM_ERROR:
      code = "Z_STREAM_ERROR";
      msg = "Inconsistent z_stream structure";
      break;
    case Z_DATA_ERROR:
      code = "Z_DATA_ERROR";
      msg = "Input data corrupt";
      break;
    case Z_MEM_ERROR:
      code = "Z_MEM_ERROR";
      msg = "Out of memory";
      break;
    case Z_VERSION_ERROR:
      code = "Z_VERSION_ERROR";
      msg = "Incompatible zlib library version";
      break;
    default:
      htsnpf(codeMsgBuf, sizeof(codeMsgBuf), "code %d", zRet);
      code = codeMsgBuf;
      msg = "Unknown value";
      break;
    }
  if (zs->msg != CHARPN) msg = zs->msg;
  switch (zlib->format)
    {
    case TXZLIBFORMAT_RAWDEFLATE:       compType = "raw deflate";       break;
    case TXZLIBFORMAT_ZLIBDEFLATE:      compType = "zlib deflate";      break;
    case TXZLIBFORMAT_GZIP:             compType = "gzip";              break;
    case TXZLIBFORMAT_ANY:          compType = "raw/zlib/gzip deflate"; break;
    default:                            compType = "unknown type";      break;
    }
  action = (zlib->forDecode ? "decode" : "encode");
  switch (how)
    {
    case TXZR_Verb:
      ret = txpmbuf_putmsg(zlib->pmbuf, MERR, fn,
                           "%s %s %s data: zlib returned %s: %s",
                           pfx, action, compType, code, msg);
      break;
    case TXZR_Noun:
      ret = txpmbuf_putmsg(zlib->pmbuf, MERR, fn,
                           "%s for %s %s: zlib returned %s: %s",
                           pfx, compType, action, code, msg);
      break;
    case TXZR_Info:
      d = flagsBuf;
      e = flagsBuf + sizeof(flagsBuf);
      *d = '\0';
      flagsLeft = zlib->flags;
      if (flagsLeft & TXCTEHF_INPUT_EOF)
        {
          strcpy(d, " INPUT_EOF");
          d += strlen(d);
          flagsLeft &= ~TXCTEHF_INPUT_EOF;
        }
      if (flagsLeft || !zlib->flags)
        {
          htsnpf(d, e - d, " %d", (int)flagsLeft);
          d += strlen(d);
        }
      ret = txpmbuf_putmsg(zlib->pmbuf, TXTRACEENCODING, fn,
                           "After %s for %s data with flags%s zlib consumed 0x%wx=%wd bytes input 0x%wx=%wd bytes output and returned 0x%wx=%wd bytes input avail 0x%wx=%wd bytes output avail and code %s: %s",
                           action, compType, flagsBuf,
                           (EPI_HUGEINT)(zs->next_in - zlib->orgInBuf),
                           (EPI_HUGEINT)(zs->next_in - zlib->orgInBuf),
                           (EPI_HUGEINT)(zs->next_out - zlib->orgOutBuf),
                           (EPI_HUGEINT)(zs->next_out - zlib->orgOutBuf),
                           (EPI_HUGEINT)zs->avail_in,
                           (EPI_HUGEINT)zs->avail_in,
                           (EPI_HUGEINT)zs->avail_out,
                           (EPI_HUGEINT)zs->avail_out, code, msg);
      break;
    default:
      ret = txpmbuf_putmsg(zlib->pmbuf, MERR, __FUNCTION__,
                           "Unknown TXZR type %d when called from %s",
                           (int)how, fn);
      break;
    }
  return(ret);
}

static int TXzlibDoInit ARGS((TXZLIB *zlib));
static int
TXzlibDoInit(zlib)
TXZLIB  *zlib;
/* Internal use: perform actual zlib init.
 * Returns 0 on error.
 */
{
  int           windowBits, res;
  z_stream      *zs;

  zs = &zlib->zlibStream;

  /* 2nd arg to inflateInit2() is window bits: 15 is default of
   * DEF_WBITS; add 16 to support gzip format instead of zlib
   * (deflate) format.  Negative for raw deflate (not zlib deflate):
   */
  switch (zlib->format)
    {
    case TXZLIBFORMAT_RAWDEFLATE:       windowBits = -15;       break;
    case TXZLIBFORMAT_ZLIBDEFLATE:      windowBits = 15;        break;
    case TXZLIBFORMAT_GZIP:             windowBits = 31;        break;
    case TXZLIBFORMAT_ANY:
      /* Should have delayed init until specific format known by
       * TXzlibTranslate():
       */
      txpmbuf_putmsg(zlib->pmbuf, MERR+ UGE, __FUNCTION__,
      "Internal error: TXZLIBFORMAT_ANY should have been clarified by caller");
      goto err;
    default:
      txpmbuf_putmsg(zlib->pmbuf, MERR + UGE, __FUNCTION__,
                     "Internal error: Unknown TXZLIBFORMAT %d",
                     (int)zlib->format);
      goto err;
    }

  if (zlib->forDecode)
    res = inflateInit2(zs, windowBits);
  else
    res = deflateInit2(zs,                      /* stream */
                       Z_DEFAULT_COMPRESSION,   /* wtf pass in from param */
                       Z_DEFLATED,              /* method */
                       windowBits,              /* windowBits */
                       8,                       /* default mem level 8 */
                       Z_DEFAULT_STRATEGY);     /* wtf from param */
  if (res != Z_OK)
    {
      TXzlibReportError(__FUNCTION__, "Cannot init", TXZR_Noun, zlib, res);
      goto err;
    }
  return(1);                                    /* success */
err:
  return(0);
}

TXZLIB *
TXzlibOpen(format, flags, traceEncoding, pmbuf)
TXZLIBFORMAT    format;         /* (in) format */
TXFILTERFLAG    flags;          /* (in) decode vs. encode */
int             traceEncoding;  /* (in) trace bits */
TXPMBUF         *pmbuf;         /* (in) handle for errors (will clone) */
/* Opens a handler for `gzip' or `deflate' compression.
 * ...Open_gzip() and ...Open_deflate() wrap and use this.
 * Returns an open handle, or NULL on error.
 */
{
  TXZLIB        *zlib = TXZLIBPN;

  if (!(zlib = TX_NEW(pmbuf, TXZLIB))) goto err;
  zlib->pmbuf = txpmbuf_open(pmbuf);
  zlib->format = format;
  zlib->forDecode = ((flags & TXFILTERFLAG_DECODE) ? TXbool_True :
                     TXbool_False);
  zlib->isVirgin = TXbool_True;
  zlib->traceEncoding = traceEncoding;
  /* Rest cleared by calloc().  Needed fields (e.g. buffers) will be
   * set per ...Translate... call.  We leave `zalloc' etc. NULL so zlib
   * uses its defaults.
   */

  /* We could pick an encode format for ..._ANY, but that leaves the
   * encode behavior nebulous (which format does ..._ANY mean?).
   * Clarify by simply not supporting ..._ANY for encode; caller must
   * give a specific format:
   */
  if (format == TXZLIBFORMAT_ANY &&
      !(flags & TXFILTERFLAG_DECODE))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__, FormatAnyOnlyForDecode);
      goto err;
    }

  /* Delay TXzlibDoInit() until first data seen in TXzlibTranslate(),
   * when we can resolve TXZLIBFORMAT_ANY
   */
  goto done;

err:
  zlib = TXzlibClose(zlib);
done:
  return(zlib);
}       

int
TXzlibReset(zlib)
TXZLIB  *zlib;  /* (in/out) object to reset */
/* Resets `zlib', i.e. starts over for new encode/decode.
 * Returns 0 on error.
 */
{
  z_stream      *zs = &zlib->zlibStream;
  int           res;

  if (zlib->isVirgin) return(1);                /* noop if no init yet */

  res = (zlib->forDecode ? inflateReset(zs) : deflateReset(zs));
  if (res != Z_OK)
    {
      TXzlibReportError(__FUNCTION__, "Cannot reset", TXZR_Noun, zlib, res);
      return(0);                                /* error */
    }
  return(1);                                    /* success */
}

int
TXzlibTranslate(zlib, flags, inBuf, inBufSz, outBuf, outBufSz)
TXZLIB  *zlib;          /* (in/out) handle to encoding handler */
TXCTEHF flags;          /* (in) flags */
byte    **inBuf;        /* (in/out) input buffer with encoded data */
size_t  inBufSz;        /* (in) input buffer size */
byte    **outBuf;       /* (out) output buffer for decoded data */
size_t  outBufSz;       /* (in) output buffer size */
/* Translates data at `*inBuf' to `*outBuf'.  Advances either/both past
 * data consumed/generated, if any.  `flags' are bit flags:
 *   TXCTEHF_INPUT_EOF  At EOF: `*inBuf' is the final chunk of input data
 *                      (Should call again if all of `*outBuf' used)
 * Returns 0 on error, 1 if ok, 2 if ok and output EOF reached.
 */
{
  z_stream      *zs = &zlib->zlibStream;
  int           res, flushParam;

  zlib->orgInBuf = *inBuf;
  zlib->orgOutBuf = *outBuf;
  zlib->flags = flags;

  if (zlib->isVirgin)                           /* no init yet */
    {
      if (zlib->format == TXZLIBFORMAT_ANY)     /* must map to specific fmt */
        {
          /* ..._ANY only valid with decode (sanity; was checked at open): */
          if (!zlib->forDecode)
            {
              txpmbuf_putmsg(zlib->pmbuf, MERR, __FUNCTION__,
                             FormatAnyOnlyForDecode);
              goto err;
            }
          /* Need to look at first input byte to detect format: */
          if (inBufSz < 1)                      /* no input data yet */
            {
              if (flags & TXCTEHF_INPUT_EOF)    /* no more input data */
                {
                  txpmbuf_putmsg(zlib->pmbuf, MERR + FRE, __FUNCTION__,
                   "Cannot init raw/zlib/gzip deflate decode: No input data");
                  goto err;
                }
              return(1);                        /* await data */
            }
          /* Determine format from first input byte: */
          switch (*zlib->orgInBuf)
            {
            case 0x1f:                          /* 1st byte of gzip header */
              zlib->format = TXZLIBFORMAT_GZIP;
              break;
            case 0x78:                          /* 1st byte of zlib header */
              zlib->format = TXZLIBFORMAT_ZLIBDEFLATE;
              break;
            default:
              /* There is no header for raw deflate data.  But per
               * interwebs, low nibble of first byte of raw deflate
               * data cannot be 6, 7, 8, 9, e, nor f, so gzip and
               * zlib header bytes above exclude raw deflate.
               */
              zlib->format = TXZLIBFORMAT_RAWDEFLATE;
              break;
            }
        }
      /* Format known.  Init: */
      if (!TXzlibDoInit(zlib)) return(0);
      zlib->isVirgin = TXbool_False;            /* now init'd */
    }

  /* Pass buffers to our object: */
  zs->next_in = (Bytef *)zlib->orgInBuf;
  zs->avail_in = (uInt)inBufSz;
  zs->next_out = (Bytef *)zlib->orgOutBuf;
  zs->avail_out = (uInt)outBufSz;
  /* Check for size overflow: */
  if ((size_t)zs->avail_in != inBufSz || (size_t)zs->avail_out != outBufSz)
    {
      txpmbuf_putmsg(zlib->pmbuf, MERR + MAE, __FUNCTION__, BufSzOverflow);
    err:
      return(0);
    }

  /* Z_FINISH must be passed for deflate() to know that input EOF was hit.
   * It is optional for inflate() (since it is marked in the compressed data)
   * but probably polite anyway:
   *
   * Bug 7614 but passing Z_FINISH for inflate() could mask a Z_OK
   * return on some short (all decoded in one call) raw deflate
   * streams apparently (see check below); would get Z_BUF_ERROR
   * instead.  And zlib docs say Z_FINISH is not needed for inflate().
   * (But then how do we indicate EOF to zlib on a truncated stream,
   * so that it can return error to us instead of gimme-more-data?):
   */
  if ((flags & TXCTEHF_INPUT_EOF) && !zlib->forDecode)
    flushParam = Z_FINISH;
  else
    flushParam = Z_NO_FLUSH;

  if (zlib->forDecode)
    res = inflate(zs, flushParam);
  else
    res = deflate(zs, flushParam);
  *inBuf = zs->next_in;                         /* return progress to caller*/
  *outBuf = zs->next_out;

  if (zlib->traceEncoding & 0x8)
    TXzlibReportError(__FUNCTION__, NULL, TXZR_Info, zlib, res);

  switch (res)
    {
    case Z_OK:                                  /* success */
      /* Bug 7614 some short raw deflate streams could return Z_OK
       * when done (iff we avoid Z_FINISH above), instead of
       * Z_STREAM_END.  Check for this by seeing if all input consumed
       * *and* EOF *and* room for more output, which seems like a good
       * indication of all-done:
       */
      if (zlib->forDecode &&
          (flags & TXCTEHF_INPUT_EOF) &&
          zs->avail_in == 0 &&
          zs->avail_out > 0)
        return(2);                              /*should be success and EOF?*/
      else
        return(1);                              /* success */
    case Z_BUF_ERROR:                           /* buffers full, try again */
      return(1);
    case Z_STREAM_END:                          /* success and EOF */
      return(2);
    case Z_DATA_ERROR:
      /* Bug 3200: Z_DATA_ERROR can be caused by opening a raw-deflate
       * stream in zlib-deflate mode: some HTTP servers send
       * raw-deflate data (incorrectly) for Content-Encoding: deflate
       * instead of zlib-deflate (as per the HTTP/1.1 spec).  If we
       * are TXZLIBFORMAT_ZLIBRAWFALLBACK, fall back to raw-deflate to
       * handle this:
       *
       * KNG 20200123 now use TXZLIBFORMAT_ANY, and detect format above
       */
      /* fall through to reporting error: */
    default:
      TXzlibReportError(__FUNCTION__, "Cannot", TXZR_Verb, zlib, res);
      return(0);                                /* error */
    }
}
