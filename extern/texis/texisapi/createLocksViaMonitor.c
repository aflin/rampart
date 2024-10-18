#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#ifdef EPI_HAVE_IO_H
#  include <io.h>
#endif /* EPI_HAVE_IO_H */
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include "dbquery.h"
#include "texint.h"
#include "http.h"
#include "httpi.h"
#include "htmll.h"
#include "txlic.h"

#ifdef _WIN32
# define HAVE_CREATE_LOCKS_MONITOR
#else
# undef HAVE_CREATE_LOCKS_MONITOR
#endif

const char       TXxCreateLocksOptionsHdrName[] = "X-Createlocks-Options";

static const char       CommaWhiteSpace[] = ", \t\r\n\v\f";


TXCREATELOCKSMETHOD
TXstrToCreateLocksMethod(const char *s, const char *e)
{
  if (!e) e = s + strlen(s);
  if (e - s == 6 && strnicmp(s, "direct", 6) == 0)
    return(TXCREATELOCKSMETHOD_DIRECT);
#ifdef HAVE_CREATE_LOCKS_MONITOR
  if (e - s == 7 && strnicmp(s, "monitor", 7) == 0)
    return(TXCREATELOCKSMETHOD_MONITOR);
#endif
  return(TXCREATELOCKSMETHOD_UNKNOWN);
}

const char *
TXcreateLocksMethodToStr(TXCREATELOCKSMETHOD method)
{
  switch (method)
    {
    case TXCREATELOCKSMETHOD_DIRECT:    return("direct");
#ifdef HAVE_CREATE_LOCKS_MONITOR
    case TXCREATELOCKSMETHOD_MONITOR:   return("monitor");
#endif
    default:                            return("unknown");
    }
}

int
TXsetCreateLocksMethods(TXPMBUF *pmbuf, TXAPP *app, const char *srcDesc,
                        const char *s, size_t sLen)
/* Parses CSV list of methods `s' and applies it to `app'.
 * `srcDesc' is a description of the source of `s', e.g.
 * `[Texis] Createlocks Methods'.
 * Returns 0 on severe error, 1 if recoverable, 2 if ok.
 */
{
  static const char     fn[] = "TXsetCreateLocksMethods";
  TXCREATELOCKSMETHOD       methods[TXCREATELOCKSMETHOD_NUM];
  size_t                i;
  const char            *tokEnd, *orgS = s, *e;
  int                   gotErr = 0;

  if (sLen == (size_t)(-1)) sLen = strlen(s);
  e = s + sLen;
  for (i = 0; i < (size_t)TXCREATELOCKSMETHOD_NUM; i++)
    methods[i] = TXCREATELOCKSMETHOD_UNKNOWN;
  for (i = 0; i < (size_t)TXCREATELOCKSMETHOD_NUM && s < e; i++, s = tokEnd)
    {                                           /* up to ...NUM choices */
      s += TXstrspnBuf(s, e, CommaWhiteSpace, -1);
      if (s >= e) break;                        /* no more choices given */
      tokEnd = s + TXstrcspnBuf(s, e, CommaWhiteSpace, -1);
      methods[i] = TXstrToCreateLocksMethod(s, tokEnd);
      if (methods[i] == TXCREATELOCKSMETHOD_UNKNOWN)
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                         "Invalid %s value `%.*s': ignored", srcDesc,
                         (int)(tokEnd - s), s);
          gotErr = 1;
          i--;
          continue;
        }
    }
  s += TXstrspnBuf(s, e, CommaWhiteSpace, -1);
  if (s < e)                                    /* still more choices given */
    {
      txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                 "Too many %s values: Expected at most %d, remainder ignored",
                     srcDesc, (int)TXCREATELOCKSMETHOD_NUM);
      gotErr = 1;
    }
  /* Must set at least one method: */
  if (methods[0] == TXCREATELOCKSMETHOD_UNKNOWN)
    {
      txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                     "No valid %s values in list `%.*s': setting unchanged",
                     srcDesc, (int)(e - orgS), orgS);
      return(1);
    }
  memcpy(app->createLocksMethods, methods, sizeof(app->createLocksMethods));
  return(gotErr ? 1 : 2);
}

#ifdef HAVE_CREATE_LOCKS_MONITOR

static int
TXcreateLocksPrintResponseMessages(TXPMBUF *pmbuf, const char *xmlBuf,
                                   size_t xmlBufSz)
/* Parses CREATELOCKS response `xmlBuf' from monitor and prints any
 * <Message>s.
 * Returns 0 on error.
 */
{
  static const char     msgTagName[] = "Message";
  HTL                   *htl = HTLPN;
  HTE                   *he;
  HTA                   *ha;
  HTBUF                 *buf = HTBUFPN;
  int                   curMsgNum = 0, ret;
  size_t                msgDepth = 0;
  char                  *curMsgCFunction = CHARPN, *curMsgMsg = CHARPN;
#define CLR_CUR_MSG()   {                       \
  curMsgNum = MERR;                             \
  curMsgCFunction = TXfree(curMsgCFunction);    \
  curMsgMsg = TXfree(curMsgMsg); }

  if (xmlBufSz == (size_t)(-1)) xmlBufSz = strlen(xmlBuf);

  /* Use builtin HTL lexer to parse document: do not want critical
   * operations like createlocks to be dependent on external
   * third-party XML lib:
   */
  if (!(htl = openhtl(pmbuf)) ||
      !sethtl(htl, (char *)xmlBuf, xmlBufSz, 0) ||
      !(buf = openhtbuf()))
    goto err;
  htlsetflags(htl, (HF_UTEXT | HF_RETCOMMENT), 0);
  htlsetflags(htl, (HF_UWHITE | HF_UNWHITE | HF_RETSCRIPT|HF_XML), 1);
  htbuf_setunhtml(buf, htutf8_to_utf8, (UTF_HTMLDECODE | UTF_DO8BIT |
                                        UTF_BADENCASISO));

  CLR_CUR_MSG();
  while ((he = gethtl(htl)) != HTEPN)           /* look for <Message> tags */
    {
      switch (he->type)
        {
        case HTE_TAG:
          if (!he->prefix &&
              strcmpi(he->name, msgTagName) == 0)
            {                                   /* <Message> */
              CLR_CUR_MSG();
              msgDepth++;
              for (ha = he->attr; ha; ha = ha->next)
                if (ha->name && ha->value)
                  {
                    if (strcmpi(ha->name, "code") == 0)
                      curMsgNum = strtol(ha->value, CHARPPN, 0);
                    else if (strcmpi(ha->name, "cFunction") == 0)
                      {
                        htbuf_clear(buf);
                        if (!htbuf_unhtml(buf, ha->value, strlen(ha->value)))
                          goto err;
                        htbuf_getdata(buf, &curMsgCFunction, 1);
                      }
                  }
              htbuf_clear(buf);                 /* begin accum content */
              /* If it is a self-closing tag, print it now.  Should not be: */
              if (he->flags & HEF_RSLASH) goto endMsg;
            }
          break;
        case HTE_ETAG:
          if (!he->prefix &&
              strcmpi(he->name, msgTagName) == 0)
            {                                   /* </Message> */
            endMsg:
              if (msgDepth-- > 0)
                {
                  htbuf_getdata(buf, &curMsgMsg, 1);
                  htbuf_clear(buf);
                  txpmbuf_putmsg(pmbuf, curMsgNum, curMsgCFunction,
                                 /* Prefix these messages, so user knows
                                  * they are from monitor, not local process:
                                  */
                                 "Texis Monitor message: %s", curMsgMsg);
                }
              CLR_CUR_MSG();
            }
          break;
        case HTE_WORD:
        case HTE_PUNCT:
        case HTE_WHITE:
        case HTE_NEWLINE:
        case HTE_SPACE:
        case HTE_TAB:
          /* Add to accumulating <Message> content: */
          if (msgDepth > 0 && !htbuf_unhtml(buf, he->data, he->dlen))
            goto err;
          break;
        case HTE_COMMENT:
        case HTE_SCRIPT:
        case HTE_CDATA:
          break;
        }
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;                                      /* error */
done:
  htl = closehtl(htl);
  buf = closehtbuf(buf);
  CLR_CUR_MSG();
  return(ret);
#undef CLR_CUR_MSG
}

int
TXcreateLocksViaMonitor(TXPMBUF *pmbuf, const char *dbPath, int timeout)
/* Sends a CREATELOCKS request to the Texis Monitor.  Called by Texis
 * clients during ddopen()/createdb(), depending on [Texis] Createlocks
 * Methods.  `dbPath' should be canonical.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXcreateLocksViaMonitor";
  const char            *errMsg;
  HTOBJ                 *obj = HTOBJPN;
  HTPAGE                *pg = HTPAGEPN;
  CGISL                 *requestHdrs = CGISLPN;
  HTFA                  fetchArgs;
  int                   ret, runLevel = 0;
  char                  *monServUrlPfx = CHARPN, *url = CHARPN;
  HTBUF                 *buf = HTBUFPN;
  char                  *dbPathFwdSlashed = CHARPN;
  char                  *d, tmpBuf[1024];
  HTL                   *htl = HTLPN;

  if (!TX_ISABSPATH(dbPath))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                     "Database path `%s' not absolute", dbPath);
      goto err;
    }
  /* Avoid infinite recursion: monitor should not call this: */
  if (TXlicIsLicenseMonitor())
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                     "Internal error: Monitor method called by monitor");
      goto err;
    }

  /* Prep for fetch: */
  if (!(obj = openhtobj())) goto err;
  if (!htsetpmbuf(obj, pmbuf, 0x3)) goto err;   /* 0x3: shared/sub objs too */
  htsettimeout(obj, timeout);
  if (!TX_CREATELOCKS_VERBOSE_IS_SET())
    {
      /* Suppress any response-code message: we will report them
       * below, with more context in the message:
       */
      htsetflag(obj, HTSF_CLIENTERRMSGS, 0);
      htsetflag(obj, HTSF_NonClientErrResponseMsgs, 0);
    }
  htsetmaxredirs(obj, 1);                       /* should not be redirected */
  if (!TXfetchSetIsSystem(obj, 1)) goto err;    /* no license count/limit */
  if (!(requestHdrs = opencgisl())) goto err;
  /* Set X-Client-Pid for logging/debugging help: */
  htsnpf(tmpBuf, sizeof(tmpBuf), "%wu", (EPI_HUGEUINT)TXgetpid(0));
  cgisladdvar(requestHdrs, "X-Client-Pid", tmpBuf);
  if (!htsetreqhdrs(obj, requestHdrs)) goto err;
  htfa_reset(&fetchArgs);
  if (!TXgetMonitorServerUrl(&monServUrlPfx, &runLevel, NULL)) goto err;
  if (!(buf = openhtbuf())) goto err;
  htbuf_setpmbuf(buf, pmbuf, 0x3);              /* 0x3: shared/sub objs too */
  /* Flip `\' to `/' for neatness in URL: */
  if (!(dbPathFwdSlashed = TXstrdup(pmbuf, fn, dbPath))) goto err;
  for (d = dbPathFwdSlashed; *d; d++) if (TX_ISPATHSEP(*d)) *d = '/';
  if (!htbuf_pf(buf, "%s"
#ifdef _WIN32
                "/"
#endif /* _WIN32 */
                "%pU", monServUrlPfx, dbPathFwdSlashed))
    goto err;
  htbuf_getdata(buf, &url, 0);

  /* Do the fetch: */
  pg = htfetchpage(obj, url, HTMETH_CREATELOCKS, &fetchArgs);
  errMsg = CHARPN;
  if (pg == HTPAGEPN)                           /* complete failure */
    errMsg = htstrerror(htgeterrno(obj));
  else
    {
      /* Parse and print any messages from the server -- regardless of
       * success/failure; could be warnings on success.  Note that
       * caller might still be buffering/suppressing our messages, if
       * it is trying multiple methods:
       */
      if (pg->contype &&
          (strcmpi(pg->contype, TXAppXml) == 0 ||
           strcmpi(pg->contype, TXTextXml) == 0) &&
          pg->rawdoc)
        {
          TXcreateLocksPrintResponseMessages(pmbuf, pg->rawdoc, pg->rawdocsz);
        }
      if (TXfetchResponseCodeClass(pg->respcode) != HTCODE_CLASS_OK)
        {                                       /* failed */
          const char    *auxMsg;

          switch (pg->respcode)
            {
            case HTCODE_METHOD_NOT_ALLOWED:
              /* Pre-HTMETH_CREATELOCKS-implementation monitor */
              auxMsg = ": Upgrade Texis Monitor";
              break;
            case HTCODE_SERVER_ERR:
              /* Probably supports HTMETH_CREATELOCKS, but not
               * enabled.  A message returned from Texis Monitor with
               * the request should already be telling user to enable
               * `createlocks' in [Scheduler] Services:
               */
              auxMsg = "";
              break;
            default:
              auxMsg = "";
              break;
            }
          htsnpf(tmpBuf, sizeof(tmpBuf),
                 "Unsuccessful response code %03d (%s)%s", (int)pg->respcode,
                 (pg->respmsg ? pg->respmsg : "?"), auxMsg);
          errMsg = tmpBuf;
        }
      else                                      /* success */
        errMsg = CHARPN;
    }
  if (errMsg)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Could not create locks for database %s via monitor: %s",
                     dbPath, errMsg);
      goto err;
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  pg = closehtpage(pg);
  requestHdrs = closecgisl(requestHdrs);
  obj = closehtobj(obj);
  monServUrlPfx = TXfree(monServUrlPfx);
  buf = closehtbuf(buf);
  dbPathFwdSlashed = TXfree(dbPathFwdSlashed);
  htl = closehtl(htl);
  return(ret);
}

#endif
