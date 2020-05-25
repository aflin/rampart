#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "http.h"

/* SQL URL functions. */

int
TXsqlFunc_urlcanonicalize(FLD *urlFld, FLD *flagsFld)
/* SQL function urlcanonicalize(url[, flags]).  Flags are zero or more of:
 *   lowerProtocol      `HTTP://foo/' -> `http://foo/`
 *   lowerHost          `http://FOO/' -> `http://foo/`
 *   removeTrailingDot  `http://foo.bar.gov./' -> `http://foo.bar.gov/'
 *   reverseHost        `http://foo.bar.gov/' -> `http://gov.bar.foo/'
 *   removeStandardPort `http://foo:80/' -> `http://foo/'
 *   decodeSafeBytes URL-decode safe chars, where semantics unlikely to change
 *                   E.g. `%41' -> `A' but `%2F' (`/') remains encoded
 *   upperEncoded    Upper-case hex characters in encoded bytes
 *   lowerPath       `http://foo/Dir%4A/' -> `http://foo/dir%4A/' e.g. Windows
 *                   (does not lower encoded bytes)
 *   addTrailingSlash `http://foo.example.com' -> `http://foo.example.com/'
 *                    (for path-like protocols)
 * Default (if not given, NULL or empty) is all but reverseHost, lowerPath.
 * +/-/= may be used as in textsearchmode.  Args must be varchar.
 * Returns FOP_EOK on success, else FOP_E... error.
 */
{
  static const char     fn[] = "TXsqlFunc_urlcanonicalize";
  static const char     flagSeps[] = ", \t\r\n\v\f";
  int                   ret;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  URL                   *url = NULL;
  const char            *urlStr;
  size_t                urlLen, flagLen;
  char                  *d, *newUrl;
  const char            *flag, *flagsStr;
  byte                  lowerProtocol = 1, lowerHost = 1;
  byte                  removeTrailingDot = 1, reverseHost = 0;
  byte                  removeStandardPort = 1, decodeSafeBytes = 1;
  byte                  upperEncoded = 1, lowerPath = 0, addTrailingSlash = 1;
  byte                  curFlagVal = 1;

  /* Validate args: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!urlFld ||
      TXfldbasetype(urlFld) != FTN_CHAR ||
      (flagsFld && TXfldbasetype(flagsFld) != FTN_CHAR))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                "Wrong argument type(s) or missing arg: expected [var]char");
      ret = FOP_EINVAL;
      goto finally;
    }

  /* Get the URL arg: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  urlStr = (char *)getfld(urlFld, &urlLen);
  if (urlStr == CHARPN)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL string argument");
      ret = FOP_EINVAL;
      goto finally;
    }

  /* Get and parse flags: - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (flagsFld &&
      (flagsStr = (char *)getfld(flagsFld, NULL)) != NULL &&
      *flagsStr)
    {
      for (flag = flagsStr; *flag; flag += flagLen)
        {
          flag += strspn(flag, flagSeps);
          switch (*flag)
            {
            case '+':
              curFlagVal = 1;
              flagLen = 1;
              continue;
            case '-':
              curFlagVal = 0;
              flagLen = 1;
              continue;
            case '=':
              lowerProtocol = lowerHost = removeTrailingDot = reverseHost =
                removeStandardPort = lowerPath = decodeSafeBytes =
                upperEncoded = addTrailingSlash = 0;
              curFlagVal = 1;
              flagLen = 1;
              continue;
            }
          flagLen = strcspn(flag, flagSeps);
          if (flagLen == 0) break;
#define CHK_FLAG(s)                                     \
          if (flagLen == sizeof(#s) - 1 &&              \
              strnicmp(flag, #s, sizeof(#s) - 1) == 0)  \
            s = curFlagVal
          CHK_FLAG(lowerProtocol);
          else
            CHK_FLAG(lowerHost);
          else
            CHK_FLAG(removeTrailingDot);
          else
            CHK_FLAG(reverseHost);
          else
            CHK_FLAG(removeStandardPort);
          else
            CHK_FLAG(lowerPath);
          else
            CHK_FLAG(addTrailingSlash);
          else
            CHK_FLAG(decodeSafeBytes);
          else
            CHK_FLAG(upperEncoded);
          else
            txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                           "Unknown flag `%.*s' ignored",
                           (int)flagLen, flag);
#undef CHK_FLAG
        }          
    }    

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!(url = openurl(urlStr))) goto err;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Decode before other flags (e.g. lowerPath): */
  if (decodeSafeBytes)
    {
      if (url->path)
        {
          char          *newPath;
          size_t        newLen, pathLen;

          pathLen = strlen(url->path);
          if (!(newPath = TXmalloc(pmbuf, fn, pathLen + 1))) goto err;
          newLen = urlstrncpy(newPath, pathLen, url->path, pathLen, USF_SAFE);
          if (newLen > pathLen)                 /* should not happen */
            {
              txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                             "Internal error: decoded str is longer");
              newPath = TXfree(newPath);
              goto err;
            }
          newPath[newLen] = '\0';
          url->path = TXfree(url->path);
          url->path = newPath;
          newPath = NULL;
        }
      if (url->query)
        {
          char          *newQuery;
          size_t        newLen, queryLen;

          queryLen = strlen(url->query);
          if (!(newQuery = TXmalloc(pmbuf, fn, queryLen + 1))) goto err;
          newLen = urlstrncpy(newQuery, queryLen, url->query, queryLen,
                              USF_SAFE);
          if (newLen > queryLen)                /* should not happen */
            {
              txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                             "Internal error: decoded str is longer");
              newQuery = TXfree(newQuery);
              goto err;
            }
          newQuery[newLen] = '\0';
          url->query = TXfree(url->query);
          url->query = newQuery;
          newQuery = NULL;
        }
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (upperEncoded)
    {
      if (url->path)
        {
          char  *newPath, *s;

          if (!(newPath = TXstrdup(pmbuf, fn, url->path))) goto err;
          for (s = newPath; *s; s++)
            if (*s == '%')
              {
                if (s[1]) s[1] = TX_TOUPPER(s[1]);
                if (s[2]) s[2] = TX_TOUPPER(s[2]);
              }
          url->path = TXfree(url->path);
          url->path = newPath;
          newPath = NULL;
        }
      if (url->query)
        {
          char  *newQuery, *s;

          if (!(newQuery = TXstrdup(pmbuf, fn, url->query))) goto err;
          for (s = newQuery; *s; s++)
            if (*s == '%')
              {
                if (s[1]) s[1] = TX_TOUPPER(s[1]);
                if (s[2]) s[2] = TX_TOUPPER(s[2]);
              }
          url->query = TXfree(url->query);
          url->query = newQuery;
          newQuery = NULL;
        }
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (lowerProtocol && url->scheme)
    {
      char      *newProtocol = NULL;
      size_t    newProtocolSz, res, protocolLen;

      protocolLen = strlen(url->scheme);
      newProtocolSz = protocolLen + 1;
      do
        {
          newProtocol = TXfree(newProtocol);
          if (!(newProtocol = (char *)TXmalloc(pmbuf, fn, newProtocolSz)))
            goto err;
          res = TXunicodeStrFold(newProtocol, newProtocolSz, url->scheme,
                                 protocolLen,
                      (TXCFF_CASEMODE_UNICODEMULTI | TXCFF_CASESTYLE_IGNORE));
          if (res == (size_t)(-1)) newProtocolSz += (newProtocolSz >> 2) + 16;
        }
      while (res == (size_t)(-1));
      url->scheme = TXfree(url->scheme);
      url->scheme = newProtocol;
      newProtocol = NULL;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (lowerHost && url->host)
    {
      char      *newHost = NULL;
      size_t    newHostSz, res, hostLen;

      hostLen = strlen(url->host);
      newHostSz = hostLen + 1;
      do
        {
          newHost = TXfree(newHost);
          if (!(newHost = (char *)TXmalloc(pmbuf, fn, newHostSz))) goto err;
          res = TXunicodeStrFold(newHost, newHostSz, url->host, hostLen,
                      (TXCFF_CASEMODE_UNICODEMULTI | TXCFF_CASESTYLE_IGNORE));
          if (res == (size_t)(-1)) newHostSz += (newHostSz >> 2) + 16;
        }
      while (res == (size_t)(-1));
      url->host = TXfree(url->host);
      url->host = newHost;
      newHost = NULL;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (removeTrailingDot && url->host)
    {
      char      *newHost, *last;

      for (last = url->host + strlen(url->host);
           last > url->host && last[-1] == '.';
           last--)
        ;
      if (*last == '.')
        {
          if (!(newHost = TXstrdup(pmbuf, fn, url->host))) goto err;
          newHost[last - url->host] = '\0';
          url->host = TXfree(url->host);
          url->host = newHost;
          newHost = NULL;
        }
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (reverseHost && url->host)
    {
      char              *newHost;
      const char        *dom, *domEnd;
      size_t            hostLen;

      hostLen = strlen(url->host);
      if (!(newHost = (char *)TXmalloc(pmbuf, fn, hostLen + 1))) goto err;
      d = newHost + hostLen;
      *d = '\0';
      for (dom = url->host; *dom; dom = domEnd)
        {                                       /* for each domain in host */
          for ( ; *dom && *dom == '.'; dom++) *(--d) = *dom;
          for (domEnd = dom; *domEnd && *domEnd != '.'; domEnd++);
          d -= domEnd - dom;
          memcpy(d, dom, domEnd - dom);
        }
      url->host = TXfree(url->host);
      url->host = newHost;
      newHost = NULL;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (removeStandardPort && url->port && url->scheme)
    {
      HTPROT    protocol;
      unsigned  stdPortNum, portNum;

      portNum = atoi(url->port);
      protocol = htstr2protocol(url->scheme, NULL);
      stdPortNum = htstdport(protocol);
      if (protocol != HTPROT_UNKNOWN && portNum == stdPortNum)
        url->port = NULL;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (addTrailingSlash &&
      (!url->path || !*url->path) &&
      TXfetchSchemeHasFilePaths(url->scheme, -1))
    {
      char      *newPath;

      if (!(newPath = TXstrdup(pmbuf, fn, "/"))) goto err;
      url->path = TXfree(url->path);
      url->path = newPath;
      newPath = NULL;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (lowerPath && url->path)
    {
      char      *newPath, *s;

      /* WTF would like to use TXunicodeStrFold() for UTF-8, but we
       * need to skip encoded bytes; just do ASCII for now:
       */
      if (!(newPath = TXstrdup(pmbuf, fn, url->path))) goto err;
      for (s = newPath; *s; s++)
        if (*s == '%')
          {
            if (TX_ISXDIGIT(s[1])) s++;
            if (TX_ISXDIGIT(s[1])) s++;
          }
        else
          *s = TX_TOLOWER(*s);
      url->path = TXfree(url->path);
      url->path = newPath;
      newPath = NULL;
    }

  /* Set return value: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!(newUrl = hturlmerge(url))) goto err;
  releasefld(urlFld);                           /* before changing type */
  urlFld->type = (FTN_CHAR | DDVARBIT);
  urlFld->elsz = sizeof(ft_char);
  setfldandsize(urlFld, newUrl, strlen(newUrl) + 1, FLD_FORCE_NORMAL);
  newUrl = NULL;                                /* `urlFld' owns it now */
  ret = FOP_EOK;
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  url = closeurl(url);
  return(ret);
}
