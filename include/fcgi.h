#ifndef FCGI_H
#define FCGI_H

/* Prototypes and definitions for Fast CGI.
 */

#ifndef CGI_H
#  include "cgi.h"
#endif
#ifndef HTTP_H
#  include "http.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FCGIPN
typedef struct FCGI_tag FCGI;
#  define FCGIPN  ((FCGI *)NULL)
#endif

#define FCGI_DEF_BUFSZ          (1 << 16)   /* must be > sizeof(FCGI_HDR) */

typedef enum FCGIFLAG_tag
{
  FCGIFLAG_KILLONEXIT   = (1 << 0)      /* kill process(es) on exit */
}
FCGIFLAG;
#define FCGIFLAGPN      ((FCGIFLAG *)NULL)

FCGI    *openfcgi ARGS((CONST char *bindpath, char **argv, CGISL *env));
FCGI    *closefcgi ARGS((FCGI *fcgi));

int     fcgi_setbufsz ARGS((FCGI *fcgi, size_t bufsz));
int     fcgi_setflags ARGS((FCGI *fcgi, FCGIFLAG flags, int on));

int     fcgi_startserver ARGS((FCGI *fcgi, int yap));

int     fcgi_startrequest ARGS((FCGI *fcgi, HTBUF *conbuf, EPI_OFF_T conlen,
                                char **envp, HTSKT **fcgisktp));
int     fcgi_stoprequest ARGS((FCGI *fcgi));
int     fcgi_readskt2stdin ARGS((FCGI *fcgi, HTSKT *skt));
int     fcgi_writestdout2skt ARGS((FCGI *fcgi, HTSKT *skt));
int     fcgi_worktillwait ARGS((FCGI *fcgi));

extern int      FcgiTrace;

#ifdef __cplusplus
}
#endif

#endif /* !FCGI_H */
