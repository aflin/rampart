#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else /* !EPI_HAVE_STDARG */
#  include <varargs.h>
#endif /* !EPI_HAVE_STDARG */
#include "texint.h"
#include "cgi.h"                /* for htvsnpf() */


/* putmsg() buffering object */

struct TXPMBUF_tag
{
  size_t        refcnt;         /* number of concurrent users */
  TXPMBUFF      flags;
  TXPM          *msgs;                          /* list of messages */
  TXPM          *lastMsg;                       /* last message in list */
  size_t        nmsgs;                          /* number of messages */
  char          *prefix;                        /* (opt.) prefix for msgs */
  TXPMBUF       *chainedPmbuf;                  /* (opt.) buffer to copy to */
  int           errMapNum;                      /* >= 0: map err to this */
};

#define TXPMBUF_IS_SPECIAL(p)   \
  ((p) == TXPMBUFPN || (p) == TXPMBUF_NEW || (p) == TXPMBUF_SUPPRESS)

static char     *TXpmbufNullPrefix = CHARPN;
#define NO_TIME_SET     (-1e+30)
static double   TXpmbufCurrentPutmsgTime = NO_TIME_SET;


static const char *
TXpmbufSpecialName(TXPMBUF *pmbuf)
{
  if (pmbuf == TXPMBUFPN) return("TXPMBUFPN");
  if (pmbuf == TXPMBUF_SUPPRESS) return("TXPMBUF_SUPPRESS");
  if (pmbuf == TXPMBUF_NEW) return("TXPMBUF_NEW");
  return("(allocated TXPMBUF)");
}

static void
TXpmbufInvalidBufParameterMsg(TXPMBUF *pmbuf, const char *fn)
{
  txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "Invalid TXPMBUF parameter %s",
                 TXpmbufSpecialName(pmbuf));
}

double
TXpmbufGetCurrentPutmsgTime(void)
/* Called from putmsg() function to get timestamp of current message,
 * which may differ from "now".  Should only be called once per
 * putmsg(): subsequent calls will return "now" as they are assumed to
 * be different (nested) putmsgs.  wtf ideally this would be a
 * parameter passed to putmsg().
 * Returns putmsg timestamp (typically "now").
 * Thread-unsafe?
 */
{
  double        ret;

  if (TXpmbufCurrentPutmsgTime != NO_TIME_SET)
    {
      ret = TXpmbufCurrentPutmsgTime;
      TXpmbufCurrentPutmsgTime = NO_TIME_SET;   /* do not re-use time */
      return(ret);
    }
  return(TXgettimeofday());
}

TXPMBUF *
txpmbuf_open(pmbufclone)
TXPMBUF *pmbufclone;    /* (in) optional object to clone */
/* If `clone' is TXPMBUF_NEW, creates and returns a TXPMBUF object.
 * If `clone' is TXPMBUFPN or TXPMBUF_SUPPRESS, returns it.
 * If `clone' is any other value, attaches to it and returns it.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "txpmbuf_open";
  TXPMBUF               *pmbuf;

  if ((pmbuf = pmbufclone) != TXPMBUFPN && pmbuf != TXPMBUF_SUPPRESS)
    {
      if (pmbufclone == TXPMBUF_NEW)
        {
          pmbuf = (TXPMBUF *)TXcalloc(pmbufclone, fn, 1, sizeof(TXPMBUF));
          if (pmbuf == TXPMBUFPN)
            return(TXPMBUFPN);
          pmbuf->flags = TXPMBUFF_DEFAULT;
          pmbuf->chainedPmbuf = TXPMBUF_SUPPRESS;
          pmbuf->errMapNum = -1;
          /* rest cleared by calloc() */
        }
      pmbuf->refcnt++;
    }
  return(pmbuf);
}

TXPMBUF *
txpmbuf_close(pmbuf)
TXPMBUF *pmbuf;
/* Decrements reference count of `pmbuf', and closes it if 0.
 * Thread-safe.
 */
{
  if (!TXPMBUF_IS_SPECIAL(pmbuf) && --pmbuf->refcnt <= (size_t)0)
    {
      txpmbuf_clrmsgs(pmbuf);
      if (!TXPMBUF_IS_SPECIAL(pmbuf->chainedPmbuf) &&
          pmbuf->chainedPmbuf != pmbuf)         /* sanity check */
        {
          txpmbuf_close(pmbuf->chainedPmbuf);
          pmbuf->chainedPmbuf = TXPMBUF_SUPPRESS;
        }
      pmbuf = TXfree(pmbuf);
    }
  return(TXPMBUFPN);
}

int
txpmbuf_setflags(pmbuf, flags, on)
TXPMBUF         *pmbuf; /* (in/out) */
TXPMBUFF        flags;  /* (in) */
int             on;     /* (in) nonzero: set `flags' on (else off) */
/* Sets `flags' on if `on', else off.
 * Returns 0 on error.
 * Thread-safe.
 */
{
  static const char     fn[] = "txpmbuf_setflags";

  flags &= TXPMBUFF_ALL;
  if (TXPMBUF_IS_SPECIAL(pmbuf))
    {
      TXpmbufInvalidBufParameterMsg(pmbuf, fn);
      return(0);
    }
  if (on) pmbuf->flags |= flags;
  else pmbuf->flags &= ~flags;
  return(1);
}

TXPMBUFF
txpmbuf_getflags(pmbuf)
TXPMBUF *pmbuf;
/* Returns current flags of `pmbuf'.
 * Thread-safe.
 */
{
  if (pmbuf == TXPMBUF_SUPPRESS)
    return(TXPMBUFF_DEFAULT & ~(TXPMBUFF_PASS | TXPMBUFF_SAVE));
  if (pmbuf == TXPMBUFPN || pmbuf == TXPMBUF_NEW)
    return(TXPMBUFF_DEFAULT);
  return(pmbuf->flags);
}

size_t
txpmbuf_nmsgs(pmbuf)
TXPMBUF *pmbuf;
/* Returns number of messages in buffer.
 * Thread-safe.
 */
{
  static const char     fn[] = "txpmbuf_nmsgs";

  if (TXPMBUF_IS_SPECIAL(pmbuf))
    {
      TXpmbufInvalidBufParameterMsg(pmbuf, fn);
      return(0);
    }
  return(pmbuf->nmsgs);
}

TXPM *
TXpmbufGetMessageList(pmbuf)
TXPMBUF *pmbuf;         /* (in) */
/* Returns pointer to message list in `pmbuf', or NULL if no messages.
 * `pmbuf' still owns returned list.
 * Thread-safe.
 */
{
  if (TXPMBUF_IS_SPECIAL(pmbuf)) return(TXPMPN);
  return(pmbuf->msgs);
}

int
txpmbuf_clrmsgs(pmbuf)
TXPMBUF *pmbuf;
/* Clears all messages in `pmbuf'.
 * Returns 0 on error, 1 if ok.
 * Thread-safe.
 */
{
  TXPM  *nextMsg;

  if (TXPMBUF_IS_SPECIAL(pmbuf)) return(1);     /* nothing to do */
  for ( ; pmbuf->msgs != TXPMPN; pmbuf->msgs = nextMsg)
    {
      nextMsg = pmbuf->msgs->next;
      pmbuf->msgs = TXfree(pmbuf->msgs);
    }
  pmbuf->nmsgs = 0;
  pmbuf->lastMsg = pmbuf->msgs = TXPMPN;
  return(1);
}  

int
TXpmbufSetChainedPmbuf(TXPMBUF *pmbuf, TXPMBUF *chainedPmbuf)
/* Attaches to `chainedPmbuf' and sets it as an additional buffer to
 * copy all messages that are sent to `pmbuf' to.  Set `chainedPmbuf'
 * to TXPMBUF_SUPPRESS to close any currently-set chained buffer.
 * Chained buffer will also be closed on close of `pmbuf'.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXpmbufSetChainedPmbuf";

  if (TXPMBUF_IS_SPECIAL(pmbuf) || chainedPmbuf == TXPMBUF_NEW)
    {
      TXpmbufInvalidBufParameterMsg(pmbuf, fn);
      return(0);
    }

  /* First close existing chained buffer, if any: */
  if (!TXPMBUF_IS_SPECIAL(pmbuf->chainedPmbuf) &&
      pmbuf->chainedPmbuf != pmbuf)         /* sanity check */
    {
      txpmbuf_close(pmbuf->chainedPmbuf);
      pmbuf->chainedPmbuf = TXPMBUF_SUPPRESS;
    }

  /* Now attach to new one: */
  pmbuf->chainedPmbuf = txpmbuf_open(chainedPmbuf);

  return(1);
}

static va_list               vaNULL;
static int
TXpmbufPutmsgActual(TXPMBUF *pmbuf, const TXPM *srcMsg, const char *srcMsgStr,
                    int num, const char *fn, const char *fmt, va_list argp)
/* Handles message from `srcMsg' (if non-NULL) or `num'/`fn'/`fmt'.
 * Returns 0 on error, 1 if ok.
 * Thread-safe iff !TXPMBUF_PASS (TXpmbufCurrentPutmsgTime is set unsafely).
 * Async-signal-safe iff (`pmbuf' is NULL or !TXPMBUFF_SAVE) and msg < 8KB.
 */
{
  static CONST char     thisfn[] = "TXpmbufPutmsgActual";
  static const char     mallocNeededFmt[] =
    "Internal error: malloc() needed during signal; skipped";
#ifdef va_copy
  va_list               argpcopy;
#  define VC()          va_copy(argpcopy, argp)
#  define VE()          va_end(argpcopy)
#else /* !va_copy */
#  define argpcopy      argp
#  define VC()
#  define VE()
#endif /* !va_copy */
  size_t                msgLen, n;
  TXPMBUFF              flags;
  char                  tmp[8192];
  char                  *tmpbuf = tmp, *dest;
  size_t                tmpsz = sizeof(tmp), destSz, srcMsgLen = 0;
  int                   badallocs = 0, numWithSig;
  TXPM                  *newMsg = TXPMPN;
  const char            *prefix = CHARPN;
  double                when;
  TXPMBUF               *chainedPmbuf = TXPMBUF_SUPPRESS;
  TXbool                inSig;

#ifndef EPI_HAVE_STDARG
  error unimplemented;
#endif /* !EPI_HAVE_STDARG */

  /* We want to know if *this* thread (not just any thread in the
   * whole process) is in a signal, for safety. Since __thread vars
   * and pthread functions are not async-signal-safe, and we cannot
   * easily add a new parameter to thousands of [txpmbuf_]putmsg() calls, we
   * overload `num' for the (relatively few) in-signal [txpmbuf_]putmsg()s:
   */
  if (srcMsg) num = srcMsg->num;
  numWithSig = num;
  if (num >= TX_PUTMSG_NUM_IN_SIGNAL)
    {
      num -= TX_PUTMSG_NUM_IN_SIGNAL;
      inSig = TXbool_True;
    }
  else
    inSig = TXbool_False;

  if (pmbuf == TXPMBUFPN)
    {
      flags = TXPMBUFF_DEFAULT;
      prefix = TXpmbufNullPrefix;
    }
  else if (pmbuf == TXPMBUF_NEW)
    flags = TXPMBUFF_DEFAULT;
  else if (pmbuf == TXPMBUF_SUPPRESS)
    flags = (TXPMBUFF_DEFAULT & ~(TXPMBUFF_PASS | TXPMBUFF_SAVE));
  else
    {
      flags = pmbuf->flags;
      prefix = pmbuf->prefix;
      chainedPmbuf = pmbuf->chainedPmbuf;
    }

  if ((flags & (TXPMBUFF_PASS | TXPMBUFF_SAVE)) == (TXPMBUFF)0 &&
      chainedPmbuf == TXPMBUF_SUPPRESS)
    goto done;                                  /* nothing to do */

  if (srcMsg)
    {
      when = srcMsg->when;
      if (!srcMsgStr) srcMsgStr = srcMsg->msg;
    }
  else
    {
      when = TXgettimeofday();
      srcMsgStr = CHARPN;
    }

  /* Convert `srcMsg' or `fmt' + argp to a string: */
  if (prefix)
    msgLen = (size_t)htsnpf(tmpbuf, tmpsz, "%s: ", prefix);
  else
    msgLen = 0;
  dest = tmpbuf + msgLen;
  destSz = (tmpsz > msgLen ? tmpsz - msgLen : (size_t)0);
  if (srcMsg)                                   /* use `srcMsgStr' */
    {
      srcMsgLen = strlen(srcMsgStr);
      n = TX_MIN(destSz, srcMsgLen);
      if (n > (size_t)0) memcpy(dest, srcMsgStr, n);
      if (destSz > (size_t)0) dest[TX_MIN(destSz - 1, srcMsgLen)] = '\0';
      msgLen += srcMsgLen;
    }
  else                                          /* use `fmt'/`argp' etc. */
    {
      VC();                                     /* init `argpcopy' */
      msgLen += (size_t)htvsnpf(dest, destSz, fmt, (HTPFF)0, TXFMTCPPN,
                                TXFMTSTATEPN, argpcopy, HTPFARGPN, SIZE_TPN,
                                TXPMBUF_SUPPRESS /* wtf? */);
      VE();                                     /* done with `argpcopy' */
    }
  if (msgLen >= tmpsz)                          /* too large for buffer */
    {
      /* We have to alloc a temp buffer.  Since we might need to alloc
       * again if saving the message below, avoid a double alloc and
       * allocate the TXPM buffer now, and use it:
       */
      n = TXPM_MIN_SZ + msgLen + 1;             /* +1 for nul */
      if (srcMsg ? srcMsg->func : fn)           /* add room for `fn' + nul */
        n += strlen(srcMsg ? srcMsg->func : fn) + 1;
      /* Use TXPMBUFPN when reporting error, to avoid recursion: */
      if (!inSig &&
          (newMsg = (TXPM *)TXmalloc(TXPMBUFPN, thisfn, n)) != TXPMPN)
        {                                       /* successful alloc */
          tmpbuf = newMsg->msg;
          tmpsz =  msgLen + 1;
          if (prefix)
            msgLen = (size_t)htsnpf(tmpbuf, tmpsz, "%s: ", prefix);
          else
            msgLen = 0;
          dest = tmpbuf + msgLen;
          destSz = (tmpsz > msgLen ? tmpsz - msgLen : (size_t)0);
          if (srcMsg)                           /* use `srcMsgStr' */
            {
              n = TX_MIN(destSz, srcMsgLen);
              if (n > (size_t)0) memcpy(dest, srcMsgStr, n);
              if (destSz > (size_t)0) dest[TX_MIN(destSz-1, srcMsgLen)] ='\0';
              msgLen += srcMsgLen;
            }
          else                                  /* use `fmt'/`argp' etc. */
            {
              msgLen += (size_t)htvsnpf(dest, destSz, fmt, (HTPFF)0,
                                        TXFMTCPPN, TXFMTSTATEPN, argp,
                                        HTPFARGPN, SIZE_TPN,
                                        TXPMBUF_SUPPRESS /* wtf? */);
            }
          if (msgLen >= tmpsz) goto trunc;      /* still too large */
        }
      else                                      /* alloc failed */
        {
          badallocs++;
          tmpbuf = tmp;                         /* back to original buf */
          tmpsz = sizeof(tmp);
        trunc:
          msgLen = tmpsz - 1;                   /* truncate msg (-1 for nul)*/
          strcpy(tmpbuf + msgLen - 3, "...");
          if (inSig)
            /* putmsg() not txpmbuf_putmsg(); avoid recursion: */
            putmsg(MERR + UGE + TX_PUTMSG_NUM_IN_SIGNAL, __FUNCTION__,
                   mallocNeededFmt);
        }
    }

  /* Pass message on to putmsg() if needed: */
  if (flags & TXPMBUFF_PASS)
    {
      TXpmbufCurrentPutmsgTime = when;          /* wtf thread-unsafe */
      putmsg(numWithSig, (srcMsg ? srcMsg->func : fn), "%s", tmpbuf);
      TXpmbufCurrentPutmsgTime = NO_TIME_SET;
    }

  /* Pass on to `chainedPmbuf': */
  if (chainedPmbuf != TXPMBUF_SUPPRESS)
    {
      if (srcMsg)
        TXpmbufPutmsgActual(chainedPmbuf, srcMsg, CHARPN, 0, CHARPN,
                            CHARPN, vaNULL);
      else
        {
          TXPM  tmpSrcMsg;
      
          memset(&tmpSrcMsg, 0, sizeof(TXPM));
          tmpSrcMsg.when = when;
          tmpSrcMsg.num = numWithSig;
          tmpSrcMsg.func = (char *)fn;
          TXpmbufPutmsgActual(chainedPmbuf, &tmpSrcMsg, tmpbuf, 0, CHARPN,
                              CHARPN, vaNULL);
        }
    }

  /* Add message to `pmbuf': */
  if ((flags & TXPMBUFF_SAVE) && !badallocs)
    {
      if (newMsg == TXPMPN)                     /* need to alloc `newMsg' */
        {
          n = TXPM_MIN_SZ + msgLen + 1;         /* +1 for nul */
          if (srcMsg ? srcMsg->func : fn)
            n += strlen(srcMsg ? srcMsg->func : fn) + 1;
          /* Use TXPMBUFPN when reporting error, to avoid recursion: */
          if (inSig)
            {
              putmsg(MERR + UGE + TX_PUTMSG_NUM_IN_SIGNAL, __FUNCTION__,
                     mallocNeededFmt);
              badallocs++;
              goto done;
            }
          if ((newMsg = (TXPM *)TXmalloc(TXPMBUFPN, thisfn, n)) == TXPMPN)
            {
              badallocs++;
              goto done;
            }
          memcpy(newMsg->msg, tmpbuf, msgLen + 1);
        }
      newMsg->when = when;
      newMsg->num = num;
      if (srcMsg ? srcMsg->func : fn)
        {
          newMsg->func = newMsg->msg + msgLen + 1;
          strcpy(newMsg->func, (srcMsg ? srcMsg->func : fn));
        }
      else
        newMsg->func = CHARPN;
      /* Add to list: */
      newMsg->prev = pmbuf->lastMsg;
      newMsg->next = TXPMPN;
      if (pmbuf->lastMsg != TXPMPN)
        pmbuf->lastMsg->next = newMsg;
      else
        pmbuf->msgs = newMsg;
      pmbuf->lastMsg = newMsg;
      pmbuf->nmsgs++;
      newMsg = TXPMPN;                          /* `pmbuf' owns it now */
    }

done:
  if (!inSig) newMsg = TXfree(newMsg);
  return(badallocs ? 0 : 1);
}

int
TXpmbufCopyMsgs(destPmbuf, srcPmbuf, minErrNum, minMsgIdx)
TXPMBUF *destPmbuf;     /* (out) destination buffer */
TXPMBUF *srcPmbuf;      /* (in) source buffer */
int     minErrNum;      /* (in) minimum putmsg() err number to copy (e.g. 0)*/
size_t  minMsgIdx;      /* (in) minimum message index to copy (e.g. 0) */
/* Copies messages from `srcPmbuf' to `destPmbuf'.
 * Returns 0 on error.
 */
{
  TXPM          *msg;
  int           ret = 1;
  size_t        msgIdx;

  if (TXPMBUF_IS_SPECIAL(srcPmbuf)) return(1);  /* nothing to do */
  for (msg = srcPmbuf->msgs, msgIdx = 0; msg; msg = msg->next, msgIdx++)
    if (msg->num >= minErrNum &&
        msgIdx >= minMsgIdx &&
        !TXpmbufPutmsgActual(destPmbuf, msg, CHARPN, 0, CHARPN, CHARPN, vaNULL))
      ret = 0;
  return(ret);
}

const char *
TXpmbufGetPrefix(TXPMBUF *pmbuf)
{
  if (pmbuf == TXPMBUFPN)
    return(TXpmbufNullPrefix);
  else if (TXPMBUF_IS_SPECIAL(pmbuf))
    return(NULL);
  else
    return(pmbuf->prefix);
}

int
TXpmbufSetPrefix(TXPMBUF *pmbuf, const char *prefix)
/* Sets prefix to prepend to all messages sent to `pmbuf'.
 * `prefix' may be NULL to cancel.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXpmbufSetPrefix";
  char                  *newPrefix;

  if (pmbuf == TXPMBUFPN)
    {
      if (prefix)
        {
          if (!(newPrefix = TXstrdup(pmbuf, fn, prefix))) goto err;
        }
      else
        newPrefix = CHARPN;
      TXpmbufNullPrefix = TXfree(TXpmbufNullPrefix);
      TXpmbufNullPrefix = newPrefix;
    }
  else if (pmbuf == TXPMBUF_SUPPRESS)
    /* everything suppressed, no prefix ever used */;
  else if (pmbuf == TXPMBUF_NEW)
    {
      TXpmbufInvalidBufParameterMsg(pmbuf, fn);
      return(0);
    }
  else                                          /* allocated TXPMBUF */
    {
      if (prefix)
        {
          if (!(newPrefix = TXstrdup(pmbuf, fn, prefix))) goto err;
        }
      else
        newPrefix = CHARPN;
      pmbuf->prefix = TXfree(pmbuf->prefix);
      pmbuf->prefix = newPrefix;
    }
  return(1);
err:
  return(0);
}

int
TXpmbufGetErrMapNum(TXPMBUF *pmbuf)
/* Ok to call for special TXPMBUFs too
 */
{
  if (TXPMBUF_IS_SPECIAL(pmbuf)) return(-1);
  return(pmbuf->errMapNum);
}

TXbool
TXpmbufSetErrMapNum(TXPMBUF *pmbuf, int errMapNum)
/* Sets putmsg number to map errors reported to `pmbuf' to `errMap'
 * (-1: do not map).
 * Returns false on error.
 */
{
  if (TXPMBUF_IS_SPECIAL(pmbuf))
    {
      TXpmbufInvalidBufParameterMsg(pmbuf, __FUNCTION__);
      return(TXbool_False);
    }
  pmbuf->errMapNum = errMapNum;
  return(TXbool_True);
}

/* putmsg() replacement.  `pmbuf' may be NULL, in which case message is just
 * passed on to putmsg().
 * Returns 0 on error, 1 if ok.
 * Thread-safe iff putmsg() is or TXPMBUF_PASS unset.
 * Async-signal-safe iff (`pmbuf' is NULL or !TXPMBUFF_SAVE) and msg < 2KB.
 * Preserves system error(s) present on entry, even on error return:
 * caller unlikely to report a txpmbuf_putmsg()-caused error, and probably
 * wants to preserve existing error regardless.
 */
int CDECL
txpmbuf_putmsg(TXPMBUF *pmbuf, int num, CONST char *fn, CONST char *fmt, ...)
{
  va_list       argp;
  int           ret;
  TXbool        inSig;

#ifndef EPI_HAVE_STDARG
  error unimplemented for non-stdarg;
#endif /* !EPI_HAVE_STDARG */

  /* We are sometimes called when reporting a system error, but *before*
   * a decision is made based on that error; preserve it:
   */
  TX_PUSHERROR();

  /* Dissect, modify, re-merge `num': */
  inSig = (num >= TX_PUTMSG_NUM_IN_SIGNAL);
  if (inSig) num -= TX_PUTMSG_NUM_IN_SIGNAL;
  if (!TXPMBUF_IS_SPECIAL(pmbuf) && pmbuf->errMapNum >= 0 && num < MINFO)
    {
      if (pmbuf->errMapNum == TX_PUTMSG_NUM_SUPPRESS_ERRS)
        {
          ret = 1;
          goto finally;
        }
      num = pmbuf->errMapNum;
    }
  if (inSig) num += TX_PUTMSG_NUM_IN_SIGNAL;

  va_start(argp, fmt);
  ret = TXpmbufPutmsgActual(pmbuf, TXPMPN, CHARPN, num, fn, fmt, argp);
  va_end(argp);

finally:
  TX_POPERROR();
  return(ret);
}
