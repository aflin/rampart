#include "texint.h"
#include <stdlib.h>

static int
slen(const char *s)
{
  return(s != CHARPN ? strlen(s) : 0);
}


size_t
urlstrncpy(char *d, size_t dlen, const char *s, size_t slen, USF flags)
/* URL-decodes `s' (`slen' bytes) to `d' (`dlen' bytes).  Returns would-be
 * length of `d' (not nul-terminated); if > `dlen', not written past.
 * If `slen' == -1, strlen(s) is assumed.  `d' may be same as `s'.
 * Safe for binary in & out.  If `dlen' == -1, strlen(d) assumed.
 * `flags' is a mix of:
 *   USF_IGNPLUS        Ignore (don't decode) '+'
 *   USF_SAFE           Only decode semantically-safe chars
 */
{
  CONST char    *se, *sOrg;
  char          *orgd, *de, ch, ch2, chOrg;
  int           i;

  orgd = d;
  if (slen == (size_t)(-1))
    {
      slen = strlen(s);
      if (dlen == (size_t)(-1) && d == s) dlen = slen;
    }
  if (dlen == (size_t)(-1))
    dlen = strlen(d);
  se = s + slen;
  de = d + dlen;
  /* NOTE: see also htpfengine() */
  for ( ; s < se; d++)
    {
      switch (ch = *(s++))
        {
          case '%':             /* %XX hex escape */
            sOrg = s;
            chOrg = ch;
            for (i = 0; i < 2; i++)
              {
                /* Bug 6061: if truncated, no decode: */
                if (s >= se) goto restore;      /* truncated; no decode */
                if ((ch2 = *(s++)) >= '0' && ch2 <= '9')
                  ch2 -= '0';
                else if (ch2 >= 'A' && ch2 <= 'F')
                  ch2 -= ('A' - 10);
                else if (ch2 >= 'a' && ch2 <= 'f')
                  ch2 -= ('a' - 10);
                else                            /* illegal escape; no decode*/
                  /* Bug 6061: if illegal, no decode: */
                  goto restore;
                ch = (i ? (char)(((byte)ch << 4) | (byte)ch2) : ch2);
              }
            /* Bug 6053: if `decodeSafe' and not safe, leave as-is: */
            if ((flags & USF_SAFE) &&
                /* Unreserved chars are allowed in a URI, per RFC 2396 2.3: */
                !(TX_ISALNUM((byte)ch) || ch == '-' || ch == '_' ||
                  ch == '.' || ch == '!' || ch == '~' || ch == '*' ||
                  ch == '\'' || ch == '(' || ch == ')'))
              {
              restore:
                s = sOrg;
                ch = chOrg;
              }
            break;
          case '+':
            if (flags & (USF_IGNPLUS | USF_SAFE)) break;
            ch = ' ';
            break;
          default:
            break;
        }
      if (d < de) *d = ch;
    }
  return((size_t)(d - orgd));
}
/* ------------------------------------------------------------------------- */

void
hturlreset(URL *url)
/* Clears `url', but does not free anything.  Used for declared instead of
 * openurl()-created URLs; do not use on latter (memleak).
 */
{
  memset(url, 0, sizeof(URL));
}

URL *
TXurlDup(TXPMBUF *pmbuf, const URL *url)
{
  URL   *newUrl;

  if (!(newUrl = TX_NEW(pmbuf, URL))) goto err;
  hturlreset(newUrl);
#define DUPIT(fld)                                              \
  if (url->fld &&                                               \
      !(newUrl->fld = TXstrdup(pmbuf, __FUNCTION__, url->fld))) \
    goto err
  DUPIT(scheme);
  DUPIT(host);
  newUrl->hostIsIPv6 = url->hostIsIPv6;
  DUPIT(port);
  DUPIT(path);
  DUPIT(type);
  DUPIT(query);
  DUPIT(anchor);
  goto finally;

err:
  newUrl = closeurl(newUrl);
finally:
  return(newUrl);
#undef DUPIT
}

URL *
openurl(const char *s)
/* Returns malloc'd URL with copy of `s' (optional) split into components.
 */
{
  URL           *url = NULL;
  TXPMBUF       *pmbuf = TXPMBUFPN;
  const char    *after_scheme, *p, *curEos, *newEos;
  size_t        length;

  if (!(url = TX_NEW(pmbuf, URL))) goto err;
  hturlreset(url);

  length = slen(s);
  if (!length) goto finally;

  /* Note: if parsing changes, see also SQL URL functions, e.g.
   * urlreversehost():
   */
  after_scheme = s;
  for (p = s; *p != '\0'; p++) {
    if (*p == ':') {                            /* scheme given */
      if (!(url->scheme = TXstrndup(pmbuf, __FUNCTION__, s, p - s)))
        goto err;
      after_scheme = p + 1;
      break;
    }
    if (*p == '/' || *p == '#' || *p == '?') break;
  }

  /* Check for anchor (terminate before query check KNG 981009): */
  curEos = after_scheme + strcspn(after_scheme, "#");
  if (*curEos == '#')                           /* anchor given */
    {
      if (!(url->anchor = TXstrdup(pmbuf, __FUNCTION__, curEos + 1)))
        goto err;
    }

  /* Check for query: */
  newEos = after_scheme + TXstrcspnBuf(after_scheme, curEos, "?", 1);
  if (*newEos == '?')                           /* query given */
    {
      if (!(url->query = TXstrndup(pmbuf, __FUNCTION__, newEos + 1,
                                   curEos - (newEos + 1))))
        goto err;
      curEos = newEos;
    }

  p = after_scheme;
  if (*p == '/' && p[1] == '/' && p + 1 < curEos)       /* authority given */
  {
    const char        *port, *auth, *authEnd, *host, *hostEnd;

    /* http://host:port/dir/file.html?query#anchor
     *     p^                        ^curEos
     */

    /* Per RFC 3986 3.2, authority is text after `//' up to next `/',
     * `?' or `#'; `curEos' already ends at next `#' or `?':
     */
    auth = p + 2;                               /* after `//' */
    for (authEnd = auth; authEnd < curEos && *authEnd != '/'; authEnd++);
    url->path = TXstrndup(pmbuf, __FUNCTION__, authEnd, curEos - authEnd);
    if (!url->path) goto err;

    /* Get host and optional port: */
    host = hostEnd = auth;
    port = NULL;
    /* IPv6 addresses in URLs are square-bracketed to disambiguate
     * address from port; e.g. http://[1234::5678:91]:80/dir/file.html.
     * Check for brackets:
     */
    url->hostIsIPv6 = TXbool_False;
    if (TX_IPv6_ENABLED(TXApp) && host < authEnd && *host == '[')
      {
        hostEnd = host + TXstrcspnBuf(host, authEnd, "]", 1);
        /* Only accept brackets if complete (so urlmerge() will not
         * add closing bracket if missing), and address is IPv6 (per
         * RFC 3986 3.2.2):
         */
        if (*hostEnd == ']' && memchr(host, ':', hostEnd - host))
          {
            url->hostIsIPv6 = TXbool_True;
            host++;                             /* after `[' */
            port = hostEnd + 1;                 /* after `]' */
          }
      }
    if (!url->hostIsIPv6)
      hostEnd = port = host + TXstrcspnBuf(host, authEnd, ":", 1);
    if (!(url->host = TXstrndup(pmbuf, __FUNCTION__, host, hostEnd - host)))
      goto err;
    if (port && port < authEnd && *port == ':')
      {
        port++;
        url->port = TXstrndup(pmbuf, __FUNCTION__, port, authEnd - port);
        if (!url->port) goto err;
      }
  }
  else if (!(url->path = TXstrndup(pmbuf, __FUNCTION__, after_scheme,
                                   curEos - after_scheme)))
    goto err;

  if (url->path[0] == '\0')
    url->path = TXfree(url->path);              /* no path */

  /* Parse optional `;type=' if FTP: */
  if (url->path && url->scheme && strcmpi(url->scheme, "ftp") == 0)
    {
      char      *semicolon;

      semicolon = strchr(url->path, ';');
      if (semicolon && strnicmp(semicolon, ";type=", 6) == 0)
        {
          *semicolon = '\0';                    /* end path at `;' */
          if (!(url->type = TXstrdup(pmbuf, __FUNCTION__, semicolon + 6)))
            goto err;
        }
    }

  /* Scheme specified but no host: the anchor was not really one
   * e.g. news:j462#36487@foo.bar -- JFG 10/7/92, from bug report
   */
  if (url->scheme &&
      !url->host &&
      url->anchor &&
      strcmpi(url->scheme, "news") == 0)        /* KNG 960220 */
    {
      char      *newPath;

      newPath = TXstrcatN(pmbuf, __FUNCTION__,
                          (url->path ? url->path : ""),
                          (url->query ? "?" : ""),
                          (url->query ? url->query : ""),
                          "#", url->anchor, NULL);
      if (!newPath) goto err;
      url->path = TXfree(url->path);
      url->path = newPath;
      newPath = NULL;
      url->query = TXfree(url->query);
      url->anchor = TXfree(url->anchor);
    }
  goto finally;

err:
  url = closeurl(url);
finally:
  return(url);
}

URL *
closeurl(URL *url)
{
  if (url == URLPN) return(URLPN);

  url->scheme = TXfree(url->scheme);
  url->host = TXfree(url->host);
  url->port = TXfree(url->port);
  url->path = TXfree(url->path);
  url->type = TXfree(url->type);
  url->anchor = TXfree(url->anchor);
  url->query = TXfree(url->query);
  url = TXfree(url);
  return(URLPN);
}

char *
hturlmerge(URL *url)
/* Merges `url' into complete string, returning malloc'd string.
 */
{
  char          *result, *curend;
  int           len;
  TXPMBUF       *pmbuf = TXPMBUFPN;
#define ADD(s)	(strcpy(curend, (s)), curend += strlen(curend))

  len = slen(url->scheme) + slen(url->host) + slen(url->port) +
        slen(url->path) + slen(url->anchor) + slen(url->query);
  if (url->hostIsIPv6) len += 2;                /* 2 for `[' `]' */
  if (url->type) len += 6 + strlen(url->type);  /* 6 for `;type=' */
  if (!(result = (char *)TXmalloc(pmbuf, __FUNCTION__, len + 10)))
    return(NULL);
  curend = result;
  *result = '\0';
  if (url->scheme != CHARPN) {
    ADD(url->scheme);
    ADD(":");
  }
  if (url->host != CHARPN) {
    if (curend > result &&
        !(url->scheme && strcmpi(url->scheme, "javascript") == 0))
      ADD("//");
    if (url->hostIsIPv6) ADD("[");
    ADD(url->host);
    if (url->hostIsIPv6) ADD("]");
  }
  if (url->port != CHARPN) {
    ADD(":");
    ADD(url->port);
  }
  if (url->path != CHARPN) {
    ADD(url->path);
  }
  if (url->type)
    {
      ADD(";type=");
      ADD(url->type);
    }
  if (url->query != CHARPN) {
    if (curend > result) ADD("?");
    ADD(url->query);
  }
  if (url->anchor != CHARPN) {                  /* _after_ query KNG 981009 */
    ADD("#");
    ADD(url->anchor);
  }
  return(result);
#undef ADD
}

void
hturlzapendspace(char *url)
/* Zaps leading/trailing space.
 */
{
  char  *d, *s;

  for (s = d = url; *s != '\0' && strchr(TXWhitespace, *s) != CHARPN; s++);
  while (*s != '\0') *(d++) = *(s++);
  for ( ; d > url && strchr(TXWhitespace, d[-1]) != CHARPN; d--);
  *d = '\0';
}
