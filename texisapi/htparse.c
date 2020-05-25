#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include "http.h"
#include "httpi.h"
#include "texint.h"
#include "unicode.h"

#ifdef RCS_ID
static CONST char RcsId[] = "$Id$";
#endif

#ifdef __APPLE__
#  pragma CC_NON_WRITABLE_STRINGS
#endif

#define BITS_PER_BYTE   8

/* ------------------------------- Config stuff ---------------------------- */
/* Define to clean up URLS in hturlabsolute():
 * scheme, host made lowercase.  Required:
 */
#define HT_CLEAN_URLS
/* -------------------------------- End config ----------------------------- */

static CONST char       Localhost[] = "localhost";
static CONST char       Whitespace[] = " \t\r\n\v\f";
static CONST char       CommaWhitespace[] = ", \t\r\n\v\f";
static CONST char       Utf8[] = "UTF-8";
static CONST char       Utf16[] = "UTF-16";
static CONST char       Utf16Le[] = "UTF-16LE";
static CONST char       Utf16Be[] = "UTF-16BE";
static CONST char       Iso[] = "ISO-8859-1";
static CONST char       UsAscii[] = "US-ASCII";
static CONST char       TruncChar[] = "Truncated character sequence";
static CONST char       InvalidChar[] = "Invalid character sequence";
static CONST char       RangeChar[] = "Out-of-range character sequence";
static CONST char       RangeEsc[] = "Out-of-range HTML escape sequence";
static CONST char       InvalidUnicode[] = "Invalid Unicode value";
static CONST char       InvalidXmlChar[] = "Invalid XML character";
static const char       WhiteSpace[] = " \t\r\n\v\f";

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

  length = (s ? strlen(s) : 0);
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
closeurl(url)
URL	*url;
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

static void
htcleanhost(char *host)
/* Cleans up `host' by making all-lower case.
 */
{
  for ( ; *host != '\0'; host++) {
#if 0   /* don't strip trailing dot, could be FQDN   KNG 000327 */
    if (host[0] == '.' && host[1] == '\0') {    /* strip trailing dot */
      host[0] = '\0';
      break;
    }
#endif /* 0 */
    *host = TX_TOLOWER(*host);
  }
}

static void
TXfetchCleanScheme(char *scheme)
{
  for ( ; *scheme; scheme++)
    *scheme = TX_TOLOWER(*scheme);
}

static void
htcleanpath(char *path, const byte *htsfFlags)
/* Replaces sequences of "/abc/../" with "/", "/./" with "/",
 * and "//" with "/" (if HTSF_UrlCollapseSlashes) in URL path `path'.
 */
{
  char  *p, *q, *s, *d;

  for (p = path; *p != '\0'; p++) {
    if (p[0] != '/') continue;

    if (p[1] == '/') {		/* i.e. "//"; should be error but clean it */
      if ((p > path && p[-1] == ':') ||	/* i.e. proxy; don't zap */
          /* Bug 5849: do not map `//' to `/' by default; can cause error
           * (e.g. redirect loop) if server wants the double slash:
           */
          !HTSF_GET_FLAG(htsfFlags, HTSF_UrlCollapseSlashes))
      {
	p++;
	continue;
      }
      for (d = p, s = p + 1; *s != '\0'; ) *(d++) = *(s++);
      *d = '\0';
      p--;			/* continue at p */
    } else if (p[1] == '.') {
      if ((p[2] == '.') && (p[3] == '/' || p[3] == '\0')) {  /* i.e. "/.." */
	/* Zap the "/.." and its previous dir, even if at root
	 * (e.g. "/..") because root is its own prev dir.  This differs
	 * from CERN lib, which leaves "/.." at root; we remove it to
	 * prevent dumb robot loops.
	 */
	if (p == path) {
	  q = path;
	} else {
	  for (q = p - 1; (q > path) && (*q != '/'); q--);  /* prev slash */
	}
	/* Only zap if not relative path: */
	if (q[0] == '/') {
          for (d = q, s = p + 3; *s != '\0'; ) *(d++) = *(s++);
          *d = '\0';
	  /* If the path was "/..", it's now empty; make "/": */
	  if (q[0] == '\0') strcpy(q, "/");
	  p = q - 1;			/* Start again with prev slash 	*/
	}
      } else if (p[2] == '/') {		/* i.e. "/./" */
        for (d = p, s = p + 2; *s != '\0'; ) *(d++) = *(s++);
        *d = '\0';
	p--;				/* continue at this slash */
      } else if (p[2] == '\0') {	/* i.e. "/." at end of path */
	p[1] = '\0';		/* Remove dot but not slash; still a dir */
      }
    }
  }
}

char *
hturlclean(url, htsfFlags)
char	        *url;
CONST byte      *htsfFlags;     /* (in) HTSF flags */
/* Cleans host and path of `url'.  Returns malloc'd string.
 * Any combo of these flags are respsected in `htsfFlags':
 * HTSF_STRIPNONPRINTURL  HTSF_ENCNONPRINTURL  HTSF_URLCANONSLASH
 */
{
  URL           *parts = NULL;
  char          *newUrl = NULL, *dup = NULL;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  if (url == CHARPN)                                    /* someone does this */
    {
      txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
                     "Internal error: NULL URL");
      goto err;
    }
  if (HTSF_GET_FLAG(htsfFlags, HTSF_ENCNONPRINTURL))
    {
      if ((dup = hturlencbad(url)) == CHARPN) goto err;
    }
  else
    {
      if ((dup = TXstrdup(pmbuf, __FUNCTION__, url)) == CHARPN)
        goto err;
      if (HTSF_GET_FLAG(htsfFlags, HTSF_STRIPNONPRINTURL)) hturlzapbad(dup);
    }
  if (HTSF_GET_FLAG(htsfFlags, HTSF_URLCANONSLASH)) hturlcanonslash(dup);
  parts = openurl(dup);
  if (!parts) goto err;
  if (parts->scheme) TXfetchCleanScheme(parts->scheme);
  if (parts->host) htcleanhost(parts->host);
  if (TXfetchSchemeHasFilePaths(parts->scheme, -1))
    {
      if (parts->path && *parts->path != '\0')
        htcleanpath(parts->path, htsfFlags);
      else
        {
          parts->path = TXfree(parts->path);
          if (!(parts->path = TXstrdup(pmbuf, __FUNCTION__, "/")))
            goto err;
        }
    }
  newUrl = hturlmerge(parts);
  goto finally;

err:
  newUrl = TXfree(newUrl);
finally:
  parts = closeurl(parts);
  dup = TXfree(dup);
  return(newUrl);
}

int
hturlisabsolute(url)
char	*url;
/* Returns 1 if `url' is an absolute URL (e.g. scheme and host given),
 * 0 if not.
 */
{
  char	*s;

  for (s = url;
       *s != '\0' && ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'));
       s++);
  if (s == url || *s != ':') return(0);
  if (!TXfetchSchemeHasFilePaths(url, s - url))
    return(1);                                  /* non-file; assume abs */
  if (strncmp(s, "://", 3) != 0) return(0);	/* no host */
  s += 3;
  return(*s == '\0' || *s == '/' ? 0 : 1);
}

static char *truncport ARGS((char *scheme, char *port));
static char *
truncport(scheme, port)
char    *scheme, *port;
/* Returns `port', or NULL if `port' is default port for `scheme'.
 */
{
  HTPROT        prot;
  char          tmp[EPI_OS_INT_BITS/3+4];

  if (scheme == CHARPN ||
      port == CHARPN ||
      (prot = htstr2protocol(scheme, CHARPN)) == HTPROT_UNKNOWN)
    return(port);
  htsnpf(tmp, sizeof(tmp), "%u", htstdport(prot));
  return(strcmpi(port, tmp) == 0 ? CHARPN : port);
}

char *
hturlabsolute(url, relatedurl, htsfFlags)
char	        *url;
char	        *relatedurl;    /* (in, opt.) */
CONST byte      *htsfFlags;     /* (in) HTSF flags */
/* Cleans and creates a full URL, from `url' which is relative to
 * previous URL `relatedurl' (which is absolute and clean, or may be
 * NULL).  Returns malloc'd string.  If `relatedUrl' is NULL, returns
 * copy of `url' as-is (but HTSF_URLCANONSLASH applied).
 * Any combo of these flags is respected in `htsfFlags':
 * HTSF_STRIPNONPRINTURL  HTSF_ENCNONPRINTURL  HTSF_URLCANONSLASH
 */
{
  int           n, len;
  char          *newp = NULL, *result = NULL;
  char          *urldup = NULL;
  URL           *given = NULL, abs, *related = NULL;
  HTPROT        prot;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  if (HTSF_GET_FLAG(htsfFlags, HTSF_ENCNONPRINTURL))
    {
      if ((urldup = hturlencbad(url)) == CHARPN) goto err;
    }
  else
    {
      if ((urldup = TXstrdup(pmbuf, __FUNCTION__, url)) == CHARPN)
        goto err;
      if (HTSF_GET_FLAG(htsfFlags, HTSF_STRIPNONPRINTURL))
        hturlzapbad(urldup);
    }
  if (HTSF_GET_FLAG(htsfFlags, HTSF_URLCANONSLASH)) hturlcanonslash(urldup);

  /* If no `relatedurl', leave URL as-is.  For htmakepage() when no URL: */
  if (!relatedurl)
    {
      result = urldup;
      urldup = NULL;
      goto done;
    }

  given = openurl(urldup);
  if (!given) goto err;
  hturlreset(&abs);

  if ((related = openurl(relatedurl)) == URLPN)
    goto err;

  /* Set path to "/" if not set: */
  if (related->host &&
      !related->path &&
      TXfetchSchemeHasFilePaths(related->scheme, -1))
    {
      related->path = TXfree(related->path);
      if (!(related->path = TXstrdup(pmbuf, __FUNCTION__, "/"))) goto err;
    }

  /* Grab the scheme: */
  if (given->scheme != CHARPN && *given->scheme != '\0') {
    abs.scheme = given->scheme;
#ifdef HT_CLEAN_URLS
    TXfetchCleanScheme(abs.scheme);
#endif
    /* If different methods, inherit nothing from related URL: */
    if (related->scheme == CHARPN || *related->scheme == '\0' ||
        strcmpi(given->scheme, related->scheme) != 0)
      {
        related = closeurl(related);
        if (!(related = openurl(NULL))) goto err;
      }
  } else {
    abs.scheme = related->scheme;
    if (abs.scheme == CHARPN || *abs.scheme == '\0')
      abs.scheme = "http";      /* wtf assume default */
#ifdef HT_CLEAN_URLS
    else
      TXfetchCleanScheme(abs.scheme);
#endif
  }

  /* Grab host and port: */
  if (given->host) {
    abs.host = given->host;
    abs.hostIsIPv6 = given->hostIsIPv6;
    abs.port = given->port;
    /* If host/port differs, inherit no path: */
#if 0   /* KNG 020619 if *any* host, inherit no path: */
    if (strcmpi(*given->host != '\0' ? given->host : Localhost,
		related->host != CHARPN && *related->host != '\0' ?
		related->host : Localhost) != 0 ||
	/* wtf check default ports: "80" == "" for http: */
	strcmp(given->port ? given->port : "",
	       related->port ? related->port : "") != 0)
#endif
      {
        related->path = TXfree(related->path);
        related->type = TXfree(related->type);
        related->anchor = TXfree(related->anchor);
        related->query = TXfree(related->query);
      }
  } else {
    abs.host = related->host;
    abs.hostIsIPv6 = related->hostIsIPv6;
    abs.port = related->port;
  }

#ifdef HT_CLEAN_URLS
  if (abs.host && *abs.host)
    htcleanhost(abs.host);
  else if (TXfetchSchemeHasFilePaths(abs.scheme, -1))   /* wtf assumption */
    {
      abs.host = (char *)Localhost;
      abs.hostIsIPv6 = TXbool_False;
    }
  if (!HTSF_GET_FLAG(htsfFlags, HTSF_PreserveCanonPortCleanUrl) &&
      abs.port &&
      (!*abs.port ||
       (abs.scheme &&
        (prot = htstr2protocol(abs.scheme, NULL)) != HTPROT_UNKNOWN &&
        (unsigned)atoi(abs.port) == htstdport(prot))))
    abs.port = NULL;                            /* ignore port */
#endif	/* HT_CLEAN_URLS */

  /* Path/query/anchor */
  if (!TXfetchSchemeHasFilePaths(abs.scheme, -1)) {
    char        *at;

    /* don't touch if not a file path: */
    abs.path = given->path;
    abs.query = given->query;
    abs.anchor = given->anchor;
#ifdef HT_CLEAN_URLS
    /* Lowercase host part of mailto paths, for consistency with http etc.: */
    if (abs.scheme &&
        abs.path &&
        strcmpi(abs.scheme, "mailto") == 0 &&
        (at = strchr(abs.path, '@')) != NULL)
      htcleanhost(at + 1);
#endif /* HT_CLEAN_URLS */
    goto pathdone;
  }
  if (given->path != CHARPN && given->path[0] == '/') {	/* absolute given */
    abs.path = given->path;
    abs.query = given->query;
    abs.anchor = given->anchor;
  } else if (related->path != CHARPN && related->path[0] == '/') {
#if 0
    if (given->path == CHARPN) {
      abs.path = related->path;
      if (given->anchor == CHARPN && given->query == CHARPN) {
	abs.anchor = related->anchor;
	abs.query = related->query;
      }
    }
#endif	/* 0 */
    /* Copy the path, but replace the final name with given relative path: */
    if (given->path != CHARPN && *given->path != '\0') {
      /* leave room for given relative path: */
      n = (strrchr(related->path, '/') + 1) - related->path;
      len = strlen(given->path);
      abs.query = given->query;
      abs.anchor = given->anchor;
    } else {
      n = strlen(related->path);
      len = 0;
      /* see also below: */
      if (given->query != CHARPN)
        {
          abs.query = given->query;
          abs.anchor = given->anchor;
        }
      else
        {
          abs.query = related->query;
          if (given->anchor != CHARPN)
            abs.anchor = given->anchor;
          else
            abs.anchor = related->anchor;
        }
    }
    if (!(newp = (char *)TXmalloc(pmbuf, __FUNCTION__, n + len + 1)))
      goto err;
    memcpy(newp, related->path, n);
    if (len) memcpy(newp + n, given->path, len);
    newp[n + len] = '\0';
    abs.path = newp;
  } else if (given->path != CHARPN) {
    /* Force it absolute, since we need '/' after host: */
    n = strlen(given->path) + 2;
    if (!(newp = (char *)TXmalloc(pmbuf, __FUNCTION__, n)))
      goto err;
    *newp = '/';
    strcpy(newp + 1, given->path);
    abs.path = newp;
    abs.query = given->query;
    abs.anchor = given->anchor;
  } else if (related->path != CHARPN) {
    /* Force it absolute: */
    n = strlen(related->path) + 2;
    if (!(newp = (char *)TXmalloc(pmbuf, __FUNCTION__, n)))
      goto err;
    *newp = '/';
    strcpy(newp + 1, related->path);
    abs.path = newp;
    goto qa;
  } else {					/* No inheritance */
    abs.path = "/";
  qa:
    /* see also above: */
    if (given->query != CHARPN)
      {
        abs.query = given->query;
        abs.anchor = given->anchor;
      }
    else
      {
        abs.query = related->query;
        if (given->anchor != CHARPN)
          abs.anchor = given->anchor;
        else
          abs.anchor = related->anchor;
      }
  }
#ifdef HT_CLEAN_URLS
  htcleanpath(abs.path, htsfFlags);
#endif
pathdone:
  result = hturlmerge(&abs);
  goto done;
err:
  result = TXfree(result);
done:
  urldup = TXfree(urldup);
  newp = TXfree(newp);
  related = closeurl(related);
  given = closeurl(given);
  hturlreset(&abs);
  return(result);
}

char *
hturlrelative(url, relatedurl, htsfFlags)
char            *url, *relatedurl;
CONST byte      *htsfFlags;     /* (in) HTSF flags */
/* Generates a URL for `url' as relative to `relatedurl'; if no
 * relation, a copy of `url' is returned.  Both should (but not
 * required to) be full paths.  Returns malloc'd string, or NULL and
 * error set.  Any combo of these flags is respected in `htsfFlags':
 * HTSF_STRIPNONPRINTURL  HTSF_ENCNONPRINTURL  HTSF_URLCANONSLASH
 */
{
  static CONST char     fn[] = "hturlrelative";
  char                  *result = CHARPN, *last, *p, *q, *dup = CHARPN;
  char                  *urldup = CHARPN, *up, *rp;
  URL                   *u = NULL, *r = URLPN;
  int                   levels, n;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  if (HTSF_GET_FLAG(htsfFlags, HTSF_ENCNONPRINTURL))
    {
      if ((urldup = hturlencbad(url)) == CHARPN) goto err;
    }
  else
    {
      if ((urldup = TXstrdup(pmbuf, fn, url)) == CHARPN)
        goto err;
      if (HTSF_GET_FLAG(htsfFlags, HTSF_STRIPNONPRINTURL))
        hturlzapbad(urldup);
    }
  if (HTSF_GET_FLAG(htsfFlags, HTSF_URLCANONSLASH)) hturlcanonslash(urldup);
  u = openurl(urldup);
  if (!u) goto err;
  if ((r = openurl(relatedurl)) == URLPN)
    goto err;
  /* always clean path; needed for path relativizer to work: */
  if (u->path != CHARPN && u->path[0] != '\0')
    htcleanpath(u->path, htsfFlags);
  if (r->path != CHARPN && r->path[0] != '\0')
    htcleanpath(r->path, htsfFlags);

  if (!TXfetchSchemeHasFilePaths(u->scheme, -1) ||      /* no true path */
      !TXfetchSchemeHasFilePaths(r->scheme, -1) ||      /* no true path */
      (u->scheme != CHARPN && u->scheme[0] != '\0' &&
       (r->scheme == CHARPN || strcmpi(u->scheme, r->scheme) != 0)))
    goto merge;                                 /* schemes differ */
  if (u->host && *u->host &&
      (!r->host || strcmpi(u->host, r->host) != 0))
    goto merge;                                 /* hosts differ */
  up = truncport(r->scheme, u->port);
  rp = truncport(r->scheme, r->port);
  if (up != CHARPN && up[0] != '\0' &&
      (rp == CHARPN || strcmpi(up, rp) != 0))
    goto merge;                                 /* ports differ */
  u->scheme = TXfree(u->scheme);
  u->host = TXfree(u->host);
  u->hostIsIPv6 = TXbool_False;
  u->port = TXfree(u->port);

  if (u->path == CHARPN || r->path == CHARPN ||
      u->path[0] != '/' || r->path[0] != '/')
    goto merge;
  last = u->path;
  for (p = u->path, q = r->path; *p != '\0' && *p == *q; p++, q++)
    {
      if (*p == '/') last = p;
    }
  /* if last == u->path, paths completely differ.  Probably best to leave
   * as-is, but we really want a relative path:  -KNG 960806
   */
  /* if (last == u->path) goto merge; */

  /* Some path in common: */
  for (levels = 0; *q != '\0'; q++)
    {
      if (*q == '/') levels++;
    }
  if (!(dup = (char *)TXmalloc(pmbuf, fn, n = 3*levels + strlen(last))))
    goto err;
  for (up = dup; levels > 0; levels--)
    {
      *(up++) = '.';
      *(up++) = '.';
      *(up++) = '/';
    }
  strcpy(up, last + 1);         /* skip first '/' in `last' */
  u->path = TXfree(u->path);
  u->path = dup;
  dup = NULL;
merge:
  result = hturlmerge(u);
  goto done;

err:
  result = CHARPN;
done:
  urldup = TXfree(urldup);
  r = closeurl(r);
  dup = TXfree(dup);
  u = closeurl(u);
  return(result);
}

void
hturlzapbad(url)
char	*url;
/* Deletes invalid chars from `url': those outside 21-7E ASCII.  Munges
 * string.
 */
{
  char	*s, *d;

  for (s = d = url; *s != '\0'; s++)
    if (*s >= '!' && *s <= '~') *d++ = *s;
  *d = '\0';
}

void
hturlcanonslash(url)
char    *url;
/* Canonicalizes backslashes to forward slashes in `url'.
 * Typically must be called before splitting so it's parsed correctly,
 * so we stop at query/anchor.
 */
{
  for ( ; *url != '\0' && *url != '?' && *url != '#'; url++)
    if (*url == '\\') *url = '/';
}

char *
hturlencbad(url)
char    *url;
/* Returns malloc'd copy of `url', URL-encoding chars outside 21-7E ASCII.
 */
{
  static const char     fn[] = "hturlencbad";
  char                  *s, *d, *e, *ret;
  size_t                n;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  n = 1;
  for (s = url; *s != '\0'; s++)                /* compute size needed */
    {
      if (*s >= '!' && *s <= '~') n++;
      else n += 3;
    }

  if ((ret = (char *)TXmalloc(pmbuf, fn, n)) == CHARPN)
    return(CHARPN);
  for (s = url, d = ret, e = ret + n; *s != '\0'; s++)
    {
      if (*s >= '!' && *s <= '~') *(d++) = *s;
      else d += htsnpf(d, e - d, "%.1pU", s);
    }
  *d = '\0';
  return(ret);
}


#define DO_8BIT_NEWLINE(flags, s, se, buf, i, bufsz, e, stop, assign)   \
  if (*s == '\r' && (flags & (UTF_CRNL | UTF_LFNL)))                    \
    {                                                                   \
      e = s + 1;                                                        \
      if (e >= se)                      /* short src buffer */          \
        {                                                               \
          if (!(flags & UTF_FINAL)) stop;  /* need more data */         \
        }                                                               \
      else if (*e == '\n') e++;         /* skip LF of CRLF */           \
      goto donewline;                                                   \
    }                                                                   \
  else if (*s == '\n' && (flags & (UTF_CRNL | UTF_LFNL)))               \
    {                                                                   \
      e = s + 1;                                                        \
    donewline:                                                          \
      if (flags & UTF_CRNL)                                             \
        {                                                               \
          if ((i) < (bufsz)) (buf)[(i)] = '\015';                       \
          else if (flags & UTF_BUFSTOP) stop;                           \
          (i)++;                                                        \
        }                                                               \
      if (flags & UTF_LFNL)                                             \
        {                                                               \
          if ((i) < (bufsz)) (buf)[(i)] = '\012';                       \
          else if (flags & UTF_BUFSTOP) stop;                           \
          (i)++;                                                        \
        }                                                               \
      assign                                                            \
    }

/* ------------------------------------------------------------------------- */
static size_t TXmakeEncodedWordSequence ARGS((char *d, size_t dlen,
      CONST char **sp, size_t slen, UTF flags, size_t maxWidth,
      HTOBJ *htobj, TXPMBUF *pmbuf));
static size_t
TXmakeEncodedWordSequence(d, dlen, sp, slen, flags, maxWidth, htobj, pmbuf)
char            *d;             /* (out) output buffer */
size_t          dlen;           /* (in) size of `d' buffer */
CONST char      **sp;           /* (in) valid-UTF-8 text input */
size_t          slen;           /* (in) length of `*sp' */
UTF             flags;
size_t          maxWidth;       /* (in) max enc-word len (0 == no limit) */
HTOBJ           *htobj;         /* (in/out, opt.) HTOBJ */
TXPMBUF         *pmbuf;         /* (out, opt.) for messages */
/* Encodes `src' to `dest' as one or more RFC 2047 encoded words.
 * Advances `*sp' past consumed data.  Assumes `*sp' is already valid UTF-8.
 * NOTE: UTF_FINAL should be set in `flags', as caller should only
 * call this for a known atomic source-word (i.e. whitespace-separated
 * in the source).
 * Returns would-be length of `dest' used.
 */
{
  static CONST char     fn[] = "TXmakeEncodedWordSequence";
  static CONST char     qPreamble[] = "=?UTF-8?Q?";
  static CONST char     bPreamble[] = "=?UTF-8?B?"; /* must be same len */
  size_t                qLen, bLen, prevQLen, prevBLen, subDestTot;
  size_t                destLeftSz, outSz, curMaxWidth, encLen;
  CONST char            *src, *srcEnd, *srcWordEnd, *s, *e, *prevS;
  CONST char            *subSrcPtr, *srcOrg, *preamble;
  char                  *dest, *destEnd, *destTmp;
  TXUNICHAR             uniChar;
  HTCHARSETFUNC         *transFunc;
  UTF                   transFlags, subState;
  /* Would-be byte length of `s' to `e', encoded to base64: */
#define BASE64LEN(s, e) (((((e) - (s)) + 2) / 3)*4)
#define PREPOSTLEN      ((sizeof(qPreamble) - 1) + 2)

  dest = d;
  destEnd = d + dlen;
  src = srcOrg = *sp;
  srcEnd = src + slen;
  if (maxWidth <= (size_t)0) maxWidth = (size_t)EPI_OS_SIZE_T_MAX;
  if (!(flags & (UTF_CRNL | UTF_LFNL))) flags |= UTF_LFNL;

  while (src < srcEnd)                          /* input still available */
    {
      /* Put linear whitespace between encoded-words, per RFC and UTF_...: */
      if (src > srcOrg)
        {
          if (flags & UTF_CRNL)
            {
              if (dest < destEnd) *dest = '\015';
              else if (flags & UTF_BUFSTOP) break;
              dest++;
            }
          if (flags & UTF_LFNL)
            {
              if (dest < destEnd) *dest = '\012';
              else if (flags & UTF_BUFSTOP) break;
              dest++;
            }
          if (dest < destEnd) *dest = ' ';
          else if (flags & UTF_BUFSTOP) break;
          dest++;
        }

      /* PREPOSTLEN + 3*TX_MAX_UTF8_BYTE_LEN is the longest encoded
       * length needed for at least 1 atomic source character
       * (e.g. 4-byte UTF-8 sequence Q-encoded).  If `maxWidth' is
       * less than that, there is a chance we will not make forward
       * progress (consume some `src') on this pass, so perhaps
       * increase it.  We do this for every encoded word, because the
       * minimum size needed to make forward progress varies according
       * to the leading character: an ASCII char takes less space than
       * a 4-byte UTF-8 sequence:
       */
      curMaxWidth = maxWidth;
      if (curMaxWidth < PREPOSTLEN + TX_MAX_UTF8_BYTE_LEN*3)
        {
          /* Compute Q and base64 length of leading atomic UTF-8 char: */
          e = src;
          uniChar = TXunicodeDecodeUtf8Char(&e, srcEnd,
                                            (flags & UTF_BADENCASISO));
          if (uniChar == TXUNICHAR_SHORT_BUFFER) e = srcEnd;
          qLen = bLen = PREPOSTLEN;
          for (s = src; s < e; s++)
            {
              if (*s == ' ' || ISQPSAFE(*(byte *)s, 2))
                qLen++;                         /* safe as-is */
              else
                qLen += 3;                      /* hex escaped */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
          encLen = (qLen <= bLen || (flags & UTF_QENCONLY)) ? qLen : bLen;
          if (curMaxWidth < encLen) curMaxWidth = encLen;
        }

      /* Find the longest prefix of `src' that we can atomicly
       * transform into an encoded word of not more than `curMaxWidth'
       * bytes.  Use Q encoding or base64, whichever is shorter
       * (or required per `flags'):
       */
      qLen = bLen = prevQLen = prevBLen = PREPOSTLEN;
      for (s = prevS = src;
           s < srcEnd && (qLen <= curMaxWidth ||
                          (bLen <= curMaxWidth && !(flags & UTF_QENCONLY)));
           s = e)
        {
          prevS = s;
          prevQLen = qLen;
          prevBLen = bLen;
          /* Do not split UTF-8 characters mid-sequence: bad form, and
           * illegal per RFC 2047 as each encoded word must be legal
           * standalone:
           */
          if (*(byte *)s & 0x80)                /* potential UTF-8 char */
            {
              e = s;
              uniChar = TXunicodeDecodeUtf8Char(&e, srcEnd,
                                                (flags & UTF_BADENCASISO));
              if (uniChar == TXUNICHAR_SHORT_BUFFER)
                {
                  if (flags & UTF_FINAL) goto oneByte;
                  /* This should not happen; we were supposed to be
                   * called with UTF_FINAL (see comments at top).
                   */
                  break;
                }
              qLen += (e - s)*3;                /* Q would hex escape them */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
          else                                  /* 7-bit ASCII char */
            {
            oneByte:
              e = s + 1;
              if (*s == ' ' || ISQPSAFE(*(byte *)s, 2))
                qLen++;                         /* safe as-is */
              else
                qLen += 3;                      /* hex escaped */
              bLen = PREPOSTLEN + BASE64LEN(src, e);
            }
        }
      /* Choose Q or base64 encoding for this word: */
      if (qLen <= curMaxWidth &&                   /* Q encoding fits and */
          (qLen <= bLen || (flags & UTF_QENCONLY)))   /* is shorter than B */
        {
          srcWordEnd = s;
          transFunc = htiso88591_to_quotedprintable;
          /* UTF_SAVEBOM for Q encoding; UTF_START|UTF_FINAL because
           * the word is self-contained.  Turn off UTF_BUFSTOP for
           * true `qLen'/`bLen' comparision:
           */
          transFlags = ((flags & ~UTF_BUFSTOP) |
                        UTF_SAVEBOM | UTF_START | UTF_FINAL);
          preamble = qPreamble;
        }
      else if (bLen <= curMaxWidth && !(flags & UTF_QENCONLY))
        {
          srcWordEnd = s;
          transFunc = htencodebase64;
          transFlags = (flags | UTF_START | UTF_FINAL);
          preamble = bPreamble;
        }
      else if (prevQLen <= curMaxWidth &&
               (prevQLen <= prevBLen || (flags & UTF_QENCONLY)))
        {
          srcWordEnd = prevS;
          transFunc = htiso88591_to_quotedprintable;
          transFlags = ((flags & ~UTF_BUFSTOP) |
                        UTF_SAVEBOM | UTF_START | UTF_FINAL);
          preamble = qPreamble;
        }
      else      /* prevBLen <= curMaxWidth && !(flags & UTF_QENCONLY) */
        {
          srcWordEnd = prevS;
          transFunc = htencodebase64;
          transFlags = (flags | UTF_START | UTF_FINAL);
          preamble = bPreamble;
        }
      /* Sanity: if no forward progress (`curMaxWidth' too small), yap.
       * Should not happen, due to `curMaxWidth' increment above:
       */
      if (srcWordEnd <= src && src < srcEnd)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Width too small");
          srcWordEnd = src + 1;                 /* just force it */
        }

      /* Now encode `src'-to-`srcWordEnd', to `dest': */
      /* Need to advance `src' and `dest' together atomically if
       * UTF_BUFSTOP, and an encoded word is at atom, so first alias
       * `dest' to `destTmp':
       */
      destTmp = dest;
      /* First output the preamble: */
      for (s = preamble; *s != '\0'; s++)
        {
          if (destTmp < destEnd) *destTmp = *s;
          else if (flags & UTF_BUFSTOP) break;
          destTmp++;
        }
      /* Then the encoded word data: */
      subDestTot = 0;
      subState = (UTF)0;
      subSrcPtr = src;
      destLeftSz = (destTmp <= destEnd ? destEnd - destTmp : 0);
      outSz = transFunc(destTmp, destLeftSz, &subDestTot, &subSrcPtr,
                        srcWordEnd - subSrcPtr, transFlags, &subState,
                        0, htobj, pmbuf);
      if (subSrcPtr < srcWordEnd && (flags & UTF_BUFSTOP))
        break;                                  /* not all consumed */
      destTmp += outSz;
      /* Finally the `?=' suffix: */
      if (destTmp < destEnd) *destTmp = '?';
      else if (flags & UTF_BUFSTOP) break;
      destTmp++;
      if (destTmp < destEnd) *destTmp = '=';
      else if (flags & UTF_BUFSTOP) break;
      destTmp++;

      /* Now we can atomically increment `src' and `dest': */
      dest = destTmp;
      src = srcWordEnd;                         /* should be == subSrcPtr? */
    }

  *sp = src;
  return(dest - d);
#undef BASE64LEN
#undef PREPOSTLEN
}

int
htparsehdrs(desc, what, buf, hdrs)
CONST char      *desc;          /* description for msgs, e.g. `CGI program' */
CONST char      *what;          /* CGI/object name for msgs */
HTBUF           *buf;           /* the buffer to parse */
CGISL           *hdrs;          /* destination for headers */
/* Parses CGI output buffer `buf' for headers, deleting them.
 * Headers are placed in `sl'.  NOTE: `buf' cannot be split.
 * Returns 0 on error, 1 if ok, 2 if past headers.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "htparsecgihdrs";
  char                  *s1, *hdr, *val, *eol, *eob, *nexthdr, *e;
  size_t                bufsz;
  int                   ret = 1;
#define SKIPHORZWHITE(s, e)     \
  for ( ; (s) < (e) && (*(s) == ' ' || *(s) == '\t'); (s)++)

  bufsz = htbuf_getdata(buf, &s1, 0);
  for (hdr = s1, eob = s1 + bufsz; hdr < eob; hdr = nexthdr)
    {                                           /* parse 1st buffer */
      for (eol = hdr; eol < eob && *eol != '\r' && *eol != '\n'; eol++);
      nexthdr = eol;
      if (htskipeol(&nexthdr, eob) != 1) break; /* not EOL: incomplete hdr */
      if (hdr == eol)                           /* blank line */
        {
          hdr = nexthdr;                        /* skip the blank line */
          goto endhdrs;
        }
      for (e = hdr; e < eol && *e != ':'; e++);
      if (e >= eol)                             /* bad header */
        {
          putmsg(MWARN + UGE, fn, "Malformed header from %s%s%s: `%.*s'%s",
                 (desc != CHARPN ? desc : (what != CHARPN ? what : "")),
                 (desc != CHARPN && what != CHARPN ? " " : ""),
                 (desc != CHARPN && what != CHARPN ? what : ""),
                 (int)(eol - hdr > 32 ? 32 : eol - hdr),
                 hdr, (eol - hdr > 32 ? "..." : ""));
        endhdrs:
          ret = 2;                              /* past headers */
          break;
        }
      *(e++) = '\0';                            /* terminate header name */
      val = e;
      SKIPHORZWHITE(val, eol);
      *eol = '\0';                              /* terminate header value */
      if (!cgisladdvarsz(hdrs, hdr, val, eol - val)) goto err;
    }
  /* WTF avoid memmove(); process split buffer?: */
  htbuf_delused(buf, hdr - s1, 0);              /* delete processed data */
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

int
htFindCsvHdrToken(hdrs, hdr, token)
CGISL           *hdrs;  /* (in) header list to examine */
CONST char      *hdr;   /* (in) header to check */
CONST char      *token; /* (in) token to look for */
/* Looks for `token' in `header', which is a CSV list of tokens.
 * Returns 1 if found, 0 if not.
 */
{
  char          **sp, *s, *e;
  size_t        tokenSz;

  tokenSz = strlen(token);
  for (sp = getcgisl(hdrs, (char *)hdr);
       sp != CHARPPN && *sp != CHARPN && **sp != '\0';
       sp++)                                    /* for each header */
    {
      for (s = *sp; *s != '\0'; s = e)          /* for each token */
        {
          s += strspn(s, CommaWhitespace);
          e = s + strcspn(s, CommaWhitespace);
          if ((size_t)(e - s) == tokenSz && strnicmp(s, token, tokenSz) == 0)
            return(1);                          /* found desired token */
        }
    }
  return(0);                                    /* not found */
}
