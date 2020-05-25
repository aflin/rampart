#ifndef URLUTILS_H
#define URLUTILS_H

typedef enum USF_tag
{
  USF_IGNPLUS =         (1 << 0),       /* ignore (don't decode) '+' */
  USF_SAFE =            (1 << 1)        /* only semantically-safe chars */
}
USF;
#define USFPN   ((USF *)NULL)

size_t  urlstrncpy ARGS((char *d, size_t dlen, CONST char *s, size_t slen,
                         USF flags));

typedef struct URL_tag
{
  /* all public and alloced; NULL if not present: */
  char	        *scheme;	/* protocol ("http", "ftp", etc.) */

  /* NOTE: `authority' in RFC 3986 and hence <urlutil split> is defined
   * to be everything between `//' and next `/', `?' or `#'.  If we
   * add user/pass etc. parsing from authority, need to update urlutil etc.
   * references to `authority':
   */
  char          *host;          /* host/IP, sans IPv6 brackets */
  TXbool        hostIsIPv6;
  char          *port;          /* port, sans `:' */

  char	        *path;          /* path; starts with '/' for absolute */
  char          *type;          /* type code (for ftp) */
  char	        *anchor;        /* anchor after '#' */
  char	        *query;         /* query string after '?' */
} URL;
#define URLPN	        ((URL *)NULL)

URL *TXurlDup(TXPMBUF *pmbuf, const URL *url);
URL *openurl(const char *s);
URL *closeurl ARGS((URL *url));
char *hturlmerge ARGS((URL *url));
void hturlreset(URL *url);

#endif /* URLUTILS_H */
