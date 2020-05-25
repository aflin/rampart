#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "dbquery.h"
#include "texint.h"
#include "http.h"
#include "httpi.h"	/* for obj->deadline */
#include "scheduler.h"


static const char       TXmonservDefaultTransferLogFormat[] =
  "%h %l %u %t \"%r\" %>s %b \"%{Referer}i\" \"%{User-Agent}i\"";

/* Transfer-log date format: [04/Nov/2002:14:22:30]
 * Assumes no `begin:'/`end:' etc. prefix:
 */
static const char   DefaultTransferLogDateFmt[] = "[%d/%b/%Y:%H:%M:%S %z]";

/* ------------------------------------------------------------------------ */

static size_t
TXprintResourceValue(char *buf, size_t bufSz, const char *resourceName,
                     size_t resourceNameLen, MONITOR_CONN *mc,
                     const TXRESOURCESTATS *stats, double now)
/* Prints value of resource named `resourceName' from `stats', to `buf'.
 * Returns would-be strlen of `buf' (not written past `bufSz';
 * nul-terminated iff room), or -1 on error (unknown `resourceName').
 * Thread-safe.  Signal-safe.
 */
{
  TXRESOURCESTAT        statIdx;
  size_t                ret;
  double                val;

  (void)mc;
  (void)now;

  /* Note that we distinguish between a known `resourceName' that is
   * not set by the OS, and a completely unknown `resourceName':
   * the latter returns an error so we can print e.g. `%{foo}/'
   * literally, the former prints `-' so we know it was a known
   * code but has no value:
   */

  statIdx = TXstrToTxresourcestat(resourceName, resourceNameLen);
  if (statIdx == TXRESOURCESTAT_UNKNOWN)        /* unknown `resourceName' */
    return((size_t)(-1));

  val = stats->values[statIdx];
  if (val < 0.0)                                /* -1 is unset/unobtainable */
    ret = htsnpf(buf, bufSz, "-");
  else if (val == 0.0)
    ret = htsnpf(buf, bufSz, "%g", val);
  else
    {
      static const char units[] = "um\0KMGT";
      const char        *unit;

      unit = units + 2;
      if (val < 1.0)
        {
          while (unit > units && val < 1.0)
            {
              val *= (double)1000.0;
              unit--;
            }
        }
      else
        {
          while (unit < units + sizeof(units) - 2 && val >= 1024.0)
            {
              val /= (double)1024.0;
              unit++;
            }
        }
      if (val < (double)0.001)
        /* Avoid potential scientific notation for small number: */
        ret = htsnpf(buf, bufSz, "%.6lf%.1s", val, unit);
      else if (unit < units + 2)
        ret = htsnpf(buf, bufSz, "%.3lg%.1s", val, unit);
      else if (unit == units + sizeof(units) - 2)
        /* Avoid potential scientific notation for large number: */
        ret = htsnpf(buf, bufSz, "%.0klf%.1s", val, unit);
      else
        ret = htsnpf(buf, bufSz, "%.3lg%.1s", val, unit);
    }
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXmonservLogTransfer(MONITOR_CONN *mc, TXRUSAGE defaultWho, TXbool inSig)
/* Logs transaction to transfer log.
 * NOTE: also called by vhttpd with mocked-up `mc'/`mc->ms'; be careful
 * with field usage as not all may be set.
 * Thread-safe and signal-safe iff `mc->ms->pmbuf' is, and iff all signal
 * handlers use TXentersignal()/TXexitsignal() properly.
 * Returns 0 on error.
 */
{
  HTBUF                 bufObj, *buf = NULL;
  const char            *cSrc, *cSrcEnd;
  int                   ret = 0, negateStatusModifiers, statusMatched;
  size_t                sz;
  char                  *s, *ref, *ua, **sl;
  const char            *fmt, *fmtEnd, *varName;
  size_t                varNameLen, statStrLen, dotPfxLen;
  double                now;
  TXRESOURCESTATS       stats[TXRUSAGE_NUM];
  byte                  gotStats[TXRUSAGE_NUM] = { 0, 0, 0, 0 };
  TXRUSAGE              who;
  char                  statStrBuf[256];
  char                  bufObjBuf[8192];

  /* We use a local-stack HTBUF to avoid malloc during signal handlers: */
  htbuf_init(&bufObj);
  if (inSig)                                    /* cannot use malloc() */
    htbuf_setdata(&bufObj, bufObjBuf, 0, sizeof(bufObjBuf), 1);
  /* else we alloc `bufObj' to grow as needed via malloc() */
  buf = &bufObj;

  if (mc->ms->transferlogfd < 0)                /* log not open */
    {
      /* Should have been opened in TXmonservStartServer(): */
      txpmbuf_putmsg(mc->ms->pmbuf, MERR + UGE +
                     (inSig ? TX_PUTMSG_NUM_IN_SIGNAL : 0), __FUNCTION__,
                     "Internal error: Transfer log not open");
      goto err;
    }
  now = TXgettimeofday();

  if ((sl = getcgisl(mc->reqhdrs, "Referer")) != CHARPPN) ref = *sl;
  else ref = "";
  if ((sl = getcgisl(mc->reqhdrs, "User-Agent")) != CHARPPN) ua = *sl;
  else ua = "";

  /* Print to `buf' according to `mc->ms->transferLogFormat': */
  fmt = mc->ms->transferLogFormat;
  if (!fmt) fmt = TXmonservDefaultTransferLogFormat;
  for ( ; *fmt; fmt = fmtEnd)
    {
      for (fmtEnd = fmt; *fmtEnd && *fmtEnd != '%'; fmtEnd++);
      if (fmtEnd > fmt) goto unkFmtCode;        /* `fmt' is a literal */
      /* `fmt' and `fmtEnd' now point to the `%' of a code: */
      fmtEnd++;                                 /* skip the `%' */

      /* Parse any modifiers: */
      varName = NULL;
      varNameLen = 0;
      negateStatusModifiers = 0;
      statusMatched = -1;       /* -1: no status checks 0: !match 1: match */
      for ( ; *fmtEnd; fmtEnd++)                /* for each modifier */
        {
          switch (*fmtEnd)
            {
            case '{':                           /* `{varName}' present */
              varName = ++fmtEnd;
              for ( ; *fmtEnd && *fmtEnd != '}'; fmtEnd++);
              if (*fmtEnd != '}') goto unkFmtCode;      /* truncated */
              varNameLen = fmtEnd - varName;
              continue;
            case '<':                           /* original request wtf */
            case '>':                           /* final request wtf */
              continue;
            case '!':
              negateStatusModifiers = 1;
              continue;
            case ',':                           /* status code separator */
              continue;
            default:
              if (TX_ISDIGIT(*fmtEnd))          /* CSV status codes */
                {
                  int           status, errnum;
                  char          *e;
                  const char    *statusStr = fmtEnd;

                  for ( ; TX_ISDIGIT(*fmtEnd); fmtEnd++);
                  status = TXstrtoi(statusStr, fmtEnd, &e, 10, &errnum);
                  if (errnum == 0 && mc->status == status) statusMatched = 1;
                  else if (statusMatched < 0) statusMatched = 0;
                  /* else preserve an earlier match */
                  fmtEnd--;                     /* negate later increment */
                  continue;
                }
              break;
            }
          break;                                /* no more modifiers */
        }

      /* Print the format given.  Codes based on Apache httpd 2.4: */
      fmtEnd++;
      if (negateStatusModifiers ? statusMatched == 1 : statusMatched == 0)
        continue;                               /* status list says skip it */
      switch (fmtEnd[-1])
        {
        case '%':                               /* a percent sign */
          if (varName) goto unkFmtCode;
          htbuf_write(buf, fmt + 1, 1);
          break;
        case 'a':                               /* Client IP address */
          /* if (varName)  underlying peer "" wtf */
          htbuf_write(buf, mc->clientIPStr, strlen(mc->clientIPStr));
          break;
        case 'A':                               /* local IP address */
          htbuf_write(buf, mc->localIPStr, strlen(mc->localIPStr));
          break;
        case 'B':                               /* resp. size (!hdrs) */
          htbuf_pf(buf, "%wu", (EPI_HUGEUINT)TX_MAX(mc->respContentBytesSent,
                                                    (EPI_OFF_T)0));
          break;
        case 'b':                               /* resp. size (!hdrs) or `-'*/
          if (mc->respContentBytesSent > (EPI_OFF_T)0)
            htbuf_pf(buf, "%wu", (EPI_HUGEUINT)mc->respContentBytesSent);
          else
            goto doDash;
          break;
        case 'C':                               /* cookie `varName' or `-' */
          if (!varName) goto doDash;
          {
            char        **sl;
            char        tmpVar[1024];

            TXstrncpy(tmpVar, varName, TX_MIN(sizeof(tmpVar), varNameLen + 1));
            if ((sl = getcgisl(mc->requestCookies, tmpVar)) != CHARPPN &&
                *sl != CHARPN)
              htbuf_pf(buf, "%s", *sl);         /* wtf if multiple vals? */
            else
              goto doDash;
          }
          break;
        case 'D':                               /* elapsed time in us */
          htbuf_pf(buf, "%wd", (EPI_HUGEINT)((now - mc->reqStartTime)*
                                             (double)1000000.0));
          break;
        case 'e':                               /* env var named `varName' */
          /* wtf implement; but Apache does not use real env: what to use? */
          goto doDash;
        case 'f':                               /* filename */
          htbuf_pf(buf, "%s", mc->origOrigfilename);
          break;
        case 'h':                               /* remote hostname */
          /* wtf we do not do hostname lookups */
          htbuf_pf(buf, "%s", mc->clientIPStr);
          break;
        case 'H':                               /* request protocol */
          htbuf_pf(buf, "%s", mc->protocol.string);
          break;
        case 'i':                               /* req. hdr `varName', C-esc*/
          if (!varName) goto doDash;
          {
            char        **sl;
            char        tmpVar[1024];

            TXstrncpy(tmpVar, varName, TX_MIN(sizeof(tmpVar), varNameLen + 1));
            if ((sl = getcgisl(mc->reqhdrs, tmpVar)) != CHARPPN &&
                *sl != CHARPN)
              {
                cSrc = *sl;                     /* wtf if multiple vals? */
                cSrcEnd = cSrc + strlen(cSrc);
                goto doCEsc;
              }
            else
              goto doDash;
          }
          break;
        case 'k':                               /* # of Keep-Alives (2nd+) */
          htbuf_pf(buf, "%d", (int)mc->numRequests - 1);
          break;
        case 'l':                               /* remote logname (identd) */
          goto doDash;                          /* wtf */
          break;
        case 'L':                               /* request log ID */
          goto unkFmtCode;                      /* wtf */
        case 'm':                               /* request method */
          htbuf_pf(buf, "%s", htmethod2str(mc->method));
          break;
        case 'n':                               /* module note `varName' */
          goto doDash;                          /* wtf */
        case 'o':                               /*reply hdr `varName', C-esc*/
          /* C-escape it */
          goto doDash;                          /* wtf */
        case 'p':                               /* canonical server port */
          if (varName)
            {
              if (varNameLen == 9 && strnicmp(varName, "canonical", 9) == 0)
                goto canonicalPort;
              else if (varNameLen == 5 && strnicmp(varName, "local", 5) == 0)
                htbuf_pf(buf, "%u", mc->localPort);
              else if (varNameLen == 6 && strnicmp(varName, "remote", 6) == 0)
                htbuf_pf(buf, "%u", mc->clientPort);
              else
                goto unkFmtCode;
            }
          else                                  /* canonical port of server */
            {
            canonicalPort:                      /* wtf Apache differs */
              htbuf_pf(buf, "%d", mc->localPort);
            }
          break;
        case 'P':                               /* PID of child servicer */
          if (varName)
            {
              if (varNameLen == 3 && strnicmp(varName, "pid", 3) == 0)
                goto processId;
              else if (varNameLen == 3 && strnicmp(varName, "tid", 3) == 0)
                htbuf_pf(buf, "%wu", (EPI_HUGEUINT)TXgetcurrentthreadid());
              else if (varNameLen == 6 && strnicmp(varName, "hextid", 6) == 0)
                htbuf_pf(buf, "%wx", (EPI_HUGEUINT)TXgetcurrentthreadid());
              else
                goto unkFmtCode;
            }
          else
            {
            processId:
              htbuf_pf(buf, "%u", (unsigned)TXgetpid(1));
            }
          break;
        case 'q':                               /* query string (w/`?') */
          htbuf_pf(buf, "%s%s", (*mc->query.string ? "?" : ""),
                   mc->query.string);
          break;
        case 'r':                               /* requst line, C-escaped */
          if (!mc->requestLine) goto doDash;
          cSrc = mc->requestLine;
          cSrcEnd = cSrc + strlen(cSrc);
        doCEsc:
          while (cSrc < cSrcEnd)
            {
              char      cEscBuf[1024];
              size_t    cEscLen;

              cEscLen = TXstrToCLiteral(cEscBuf, sizeof(cEscBuf), &cSrc,
                                        cSrcEnd - cSrc);
              if (!htbuf_write(buf, cEscBuf, cEscLen)) goto err;
            }
          break;
        case 'R':                               /* handler generated request */
          goto doDash;                          /* wtf */
        case 's':                               /* status code */
          htbuf_pf(buf, "%u", (int)mc->status);
          break;
        case 't':                               /* request-received time */
          {
            const char  *sFmt;
            size_t      sLen;
            double      tim = mc->reqStartTime;
            char        strftimeFmtBuf[1024];

            if (varName)
              {
                sFmt = varName;
                sLen = varNameLen;
              }
            else
              {
                sFmt = "";
                sLen = 0;
              }
            if (sLen >= 6 && strnicmp(sFmt, "begin:", 6) == 0)
              {
                tim = mc->reqStartTime;
                sFmt += 6;
                sLen -= 6;
              }
            else if (sLen >= 4 && strnicmp(sFmt, "end:", 4) == 0)
              {
                tim = now;
                sFmt += 4;
                sLen -= 4;
              }
            if (sLen == 0)                      /* empty format */
              {
                sFmt = DefaultTransferLogDateFmt;
                sLen = sizeof(DefaultTransferLogDateFmt) - 1;
              }
            /* wtf `sec', `msec', `usec', `msec_frac', `usec_frac' */
            /* Prefix time format with `|' to force use of internal
             * TXstrftime(): thread-safe/signal-safe, and knows `%z':
             */
            *strftimeFmtBuf = '|';
            TXstrncpy(strftimeFmtBuf + 1, sFmt,
                      TX_MIN(sizeof(strftimeFmtBuf) - 1, sLen + 1));
            htbuf_pf(buf, "%at", strftimeFmtBuf, (time_t)tim);
          }
          break;
        case 'T':                               /* #seconds to service req. */
          htbuf_pf(buf, "%wd", (EPI_HUGEINT)(now - mc->reqStartTime));
          break;
        case 'u':                               /* remote user wtf */
          goto doDash;
          break;
        case 'U':                               /* URL path, !query */
          /* Apache gives URL-*de*coded value, so we will too: */
          htbuf_pf(buf, "%s", mc->decodedurl.string);
          break;
        case 'v':                               /* canonical servername */
        case 'V':                               /*"" w/UseCanonicalName wtf */
          htbuf_pf(buf, "%s", mc->ms->servername);
          break;
        case 'X':                               /* connection status */
          htbuf_pf(buf, "%c", ((mc->flags & TXHCF_GOTXFERERR) ? 'X' :
                               ((mc->flags & TXHCF_REUSABLE) ? '+' : '-')));
          break;
        case 'I':                               /* bytes recv'd (+req +hdrs)*/
          goto doDash;                          /* wtf */
        case 'O':                               /* bytes sent (+hdrs) */
          goto doDash;                          /* wtf */
        case 'S':                               /* bytes xferred (I+O) */
        doDash:
          htbuf_pf(buf, "-");                   /* wtf */
          break;
        case '/':                               /* stats (Thunderstone ext.)*/
          if (!varName) goto unkFmtCode;
          who = defaultWho;
          /* `varName' may be prepended with `self' etc.: */
          dotPfxLen = TXstrcspnBuf(varName, varName + varNameLen, ".", 1);
          if (dotPfxLen < varNameLen)           /* `who.' pfx may be present*/
            {
              TXRUSAGE  w;

              w = TXstrToTxrusage(varName, dotPfxLen);
              if (w != TXRUSAGE_UNKNOWN)        /* recognized prefix */
                {
                  who = w;
                  dotPfxLen++;                  /* skip the `.' too */
                  varName += dotPfxLen;         /* skip prefix in `varName' */
                  varNameLen -= dotPfxLen;
                }
            }
          if (!gotStats[who])
            {
              /* TXPMBUF_SUPPRESS: printing errors would be too
               * verbose, maybe signal-unsafe?
               * If TXgetResourceStats() fails, it sets all values -1 as
               * if not set by the OS.  We print those as `-' to
               * indicate no value but the resource name was known; as
               * opposed to an unknown resource name which we print
               * via `unkFmtCode':
               */
              TXgetResourceStats(TXPMBUF_SUPPRESS, who, &stats[who]);
              gotStats[who] = 1;
            }
          statStrLen = TXprintResourceValue(statStrBuf, sizeof(statStrBuf),
                                   varName, varNameLen, mc, &stats[who], now);
          if (statStrLen == (size_t)(-1)) goto unkFmtCode;
          htbuf_write(buf, statStrBuf,
                      TX_MIN(statStrLen, sizeof(statStrBuf) - 1));
          break;
        default:                                /* unknown %-code */
        unkFmtCode:
          if (!htbuf_write(buf, fmt, fmtEnd - fmt)) goto err;
          break;
        }
      if (htbuf_getflags(buf, HTBF_ERROR) & HTBF_ERROR) goto err;
    }
  if (!htbuf_write(buf, TX_OS_EOL_STR, TX_OS_EOL_STR_LEN)) goto err;

  sz = htbuf_getdata(buf, &s, 0);
  ret = (tx_rawwrite(mc->ms->pmbuf, mc->ms->transferlogfd,
              mc->ms->transferlog, TXbool_False, (byte *)s, sz, inSig) == sz);
  goto finally;

err:
  ret = 0;
finally:
  htbuf_release(&bufObj);
  buf = NULL;
  return(ret);
}
