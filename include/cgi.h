#ifndef CGI_H
#define CGI_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* ---------------------------- Config stuff ------------------------------- */
/* Define to make htpf(), etc. real functions instead of macros for printf():
 * -KNG 960116
 */
#define HTPF_STRAIGHT   /* avoid static linking issues */

/* Define to _not_ replace strftime(), ascftime(), etc.:
 *  -KNG 970202
 */
/* #define STRFTIME_STRAIGHT */
#define USECGIPASSWD2           /* use new encrypt, do owner field */

/* see also htprintf.c, cgi.c, etc. */
/* ----------------------------- End config -------------------------------- */

#if defined(HTPF_STRAIGHT) && !defined(va_list)
#  ifdef EPI_HAVE_STDARG
#    include <stdarg.h>
#  else
#    include <varargs.h>
#  endif
#endif
#ifndef _TIME_H
#  include <time.h>     /* for struct tm */
#endif
#ifndef MMAPI_H                /* MAW 05-28-97 - for pdfxml prototype */
#  include "api3.h"
#endif
#ifndef TX_VERSION_NUM
error: TX_VERSION_NUM undefined;
#endif /* !TX_VERSION_NUM */

/* some boxes have sprintf() return char * instead of int:
 * -KNG 960122
 */
#if defined(sun) && !defined(__SVR4)
#  define SPFRET_TYPE	char *
#  define SPFRET(i, s)	return(s)
#else
#  define SPFRET_TYPE	int
#  define SPFRET(i, s)	return(i)
#endif
#include "htpf.h"

/* borrowed from http.h: */
#ifndef HTOBJPN
typedef struct HTOBJ_tag        HTOBJ;
#  define HTOBJPN       ((HTOBJ *)NULL)
#  define HTOBJPPN      ((HTOBJ **)NULL)
#endif /* !HTOBJPN */


/************************************************************************/

typedef struct CGIS_tag
{
 char *tag; /* variable */
 size_t tagLen; /* length of variable (not including nul-term.) */
 char **s;  /* contents of variable */        /* MAW 11-01-94 - array */
 size_t *len;   /* parallel array of value lengths */
 int    n;                             /* MAW 11-01-94 - # in s array */
}
CGIS;
#define CGISPN (CGIS *)NULL


/************************************************************************/

/* Name-comparison function for CGISL lists: */
typedef int (CGISLCMP) ARGS((CONST char *a, CONST char *b, size_t sz));
#define CGISLCMPPN      ((CGISLCMP *)NULL)

typedef struct CGISL_tag
{
  CGIS		*cs;
  char		*buf;
  int		n;
  int		bufsz, bufused, privnum;	/* KNG 960201 */
  CGISLCMP	*cmp;				/* KNG 960321 */
}
CGISL;
#define CGISLPN         ((CGISL *)NULL)
#define CGISLPPN        ((CGISL **)NULL)

/************************************************************************/


/* Internal usage; these must be in order of precedence (see cgivar())
 * (low val == high precedence)   -KNG 960108
 */
typedef enum CGISLN_tag
{
  CGISL_PUT,            /* putcgi() state vars */
  CGISL_PREVPUT,        /* previous (read) state */
  CGISL_ENV,            /* environment vars */  /* KNG 970410 over COOKIE */
  CGISL_COOKIE,         /* cookies */
  CGISL_URL,            /* URL vars */
  CGISL_CONTENT,        /* content vars */
  CGISL_NUM             /* count: must be last */
}
CGISLN;

/* (public) flags to cgivar()/getcgi():  -KNG 960108 */
typedef enum CGIN_tag
{
  CGI_PUT       = (1 << CGISL_PUT),
  CGI_PREVPUT   = (1 << CGISL_PREVPUT),
  CGI_ENV       = (1 << CGISL_ENV),             /* KNG 970410 over COOKIE */
  CGI_COOKIE    = (1 << CGISL_COOKIE),
  CGI_URL       = (1 << CGISL_URL),
  CGI_CONTENT   = (1 << CGISL_CONTENT),
  /* combinations: */
  CGI_STATE     = (CGI_PUT | CGI_PREVPUT),
  CGI_ANY       = (~0)  /* any */
}
CGIN;

#define CGI struct CGI_struct
#define CGIPN ((CGI *)NULL)
CGI
{
 char *  server_software  ;
 char *  server_name      ;
 char *  gateway_interface;
 char *  server_protocol  ;
 char *  server_port      ;
 char *  request_method   ;
 char *  http_connection  ; /* PBR 1-1-96 */
 char *  http_user_agent  ; /* PBR 1-1-96 */
 char *  http_host        ; /* PBR 1-1-96 */
 char *  http_accept      ;
 char *  http_cookie      ; /* PBR 1-1-96 */
 char *  http_x_forwarded_for;
 char *  path_info        ;
 char *  path_translated  ;
 char *  script_name      ;
 char *  query_string     ;
 char *  remote_host      ;
 char *  remote_addr      ;
 char *  remote_user      ;
 char *  auth_type        ;
 char *  content_type     ;
 char *  content_length   ;
 char *  content;
 char *  document_root    ;                           /* MAW 09-12-95 */
 char   *server_root;

 int	flags;                          /* internal CF flags */
 struct CGIPRIV_tag	*priv;		/* other stuff KNG 960131 */
 size_t content_sz;                     /* actual `content' size 961001 */
};

/* Return codes from cgilogin()/cgivalidlogin(): */
#define CGILOG_OK		1	/* ok login */
#define CGILOG_ERROR		0	/* internal error (no mem, etc.) */
#define CGILOG_NOT		-1	/* not logged in (cgivalidlogin()) */
#define CGILOG_TIMEOUT		-2	/* idle timeout */
#define CGILOG_INVALID		-3	/* invalid user/pass */
#define CGILOG_NOPASSTBL	-4	/* can't find passwd table */
#define CGILOG_BADREMHOST	-5	/* rem host changed/not in IP prefix */
#define CGILOG_USEREXISTS	-6	/* user already exists */
#define CGILOG_NOSUCHUSER	-7	/* user doesn't exist */

/* opencgi[pre]() flags: */
typedef enum OCF_tag
{
  OCF_URLDECODECOOKIEVALS       = (1 << 0)
}
OCF;
#define OCFPN   ((OCF *)NULL)

CGI   *closecgi ARGS((CGI *cp));
CGI   *opencgi ARGS((OCF flags));
OCF   cgigetflags ARGS((CGI *cp));
int   cgisetflags ARGS((CGI *cp, OCF flags, int on));
int   TXcgislAddCookiesFromHeader(CGISL *sl, const char *hdrVal,
                                  size_t hdrValLen, int urlDecodeValues);
int   iscgiprog ARGS((void));                           /* KNG 960719 */
char  *cgivar ARGS((CGI *cp, int n, int which, char ***valp));
char  *cgivarsz ARGS((CGI *cp, int n, int which, char ***valp, size_t **szp));
char  **getcgi ARGS((CGI *cp, char *name, int which));
char  **getcgisz ARGS((CGI *cp, char *name, int which, size_t **szp));
void  TXcgislClear ARGS((CGISL *sl));
CGISL *closecgisl ARGS((CGISL *sl));			/* KNG 960321 */
CGISL *opencgisl ARGS((void));				/* KNG 960321 */
int    cgisladdstr ARGS((CGISL *sl, char *s));		/* KNG 960326 */
int    cgisladdvar ARGS((CGISL *cl, CONST char *name, CONST char *val)); /* KNG 960321 */
int    cgisladdvarsz ARGS((CGISL *cl, CONST char *name, CONST char *val, size_t sz));
int    TXcgislAddVarLenSz ARGS((CGISL *cl, CONST char *name, size_t nameLen,
                                CONST char *val, size_t sz));
int    TXcgislAddVarLenSzLower(CGISL *cl, const char *name, size_t nameLen,
                               const char *val, size_t sz);
CGISL *dupcgisl ARGS((CGISL *sl));
size_t cgisl_numvals ARGS((CGISL *cgisl));
char  *cgislvar ARGS((CGISL *sl, int n, char ***valp));	/* KNG 960321 */
char  *cgislvarsz ARGS((CGISL *sl, int n, char ***valp, size_t **szp));
char **getcgisl ARGS((CGISL *sl, char *name));		/* KNG 960321 */
char **TXcgislGetVarAndValues ARGS((CGISL *sl, char **varName));
int    cgislsetcmp ARGS((CGISL *sl, CGISLCMP *cmp));	/* KNG 960321 */

size_t TXcgislNumVars(const CGISL *cgisl);

char *cgiparsehdr(HTPFOBJ *htpfobj, const char *hdr, const char **end,
                  CGISL **parms);

int  directcgi ARGS((int flag));
CGI *opencgipre ARGS((OCF flags, CGI *init, char **envn,
                      char **envv));                    /* KNG 970227 */

int  writecgi ARGS((CGI *cp, FILE *fp));

int  putcgi ARGS((CGI *cp, char *name, char *val));	/* KNG 960103 */
int  cgistarthdrs ARGS((CGI *cp, char *type));		/* KNG 960105 */
int  cgiendhdrs ARGS((CGI *cp));			/* KNG 960105 */
int  cgiputcookie ARGS((CGI *cp, char *name, char *val, char *domain,
			char *path, int secure, long expire));
int  cgigetstate ARGS((CGI *cp, char **name, char **val));  /* KNG 960131 */

int  cgilogtimeout ARGS((CGI *cp, long tim));
int  cgivalidlogin ARGS((CGI *cp));
int  cgilogin ARGS((CGI *cp, char *db, char *tbl, char *user, char *pass));
int  cgilogout ARGS((CGI *cp));

/* optional/internal: */
int   cgiwritestate ARGS((CGI *cp, int tofile));	/* KNG 960131 */
int   cgireadstate ARGS((CGI *cp));			/* KNG 960103 */
int   cgiprocenv ARGS((CGI *cp));			/* KNG 960109 */
/* for backwards compatibility:  -KNG 960108 */
#define cgiurl(c, n, v)		cgivar((c), (n), CGI_URL, (v))
#define cgicontent(c, n, v)	cgivar((c), (n), CGI_CONTENT, (v))
#define findcgi(c, s)		getcgi((c), (s), CGI_ANY)
#define freecgisl(sl)		closecgisl(sl)	/* KNG 960321 */

int getcgich ARGS((char **sp));

/* ------------------------------ htbuf.c: --------------------------------- */
#ifndef HTBUFPN
typedef struct HTBUF_tag        HTBUF;          /* defined in httpi.h */
#  define HTBUFPN ((HTBUF *)NULL)
/* Note: see also txlic.h */
#endif /* !HTBUFPN */

#define HTBUF_STDOUT    ((HTBUF *)1)            /* alias for stdout */

#undef TXALLOC_PROTO_SOLE
#undef TXALLOC_PROTO
#ifdef MEMDEBUG
#  define TXALLOC_PROTO_SOLE    CONST char *file, int line, CONST char *memo
#  define TXALLOC_PROTO         , TXALLOC_PROTO_SOLE
#  undef openhtbuf
#  undef closehtbuf
#  undef TxfmtcpDup
#  undef TxfmtcpClose
#else /* !MEMDEBUG */
#  define TXALLOC_PROTO_SOLE    void
#  define TXALLOC_PROTO
#endif /* !MEMDEBUG */

HTBUF *openhtbuf ARGS((TXALLOC_PROTO_SOLE));
HTBUF *closehtbuf ARGS((HTBUF *buf TXALLOC_PROTO));
TXFMTCP *TxfmtcpDup ARGS((CONST TXFMTCP *src, TXPMBUF *pmbuf TXALLOC_PROTO));
TXFMTCP *TxfmtcpClose ARGS((TXFMTCP *fmtcp TXALLOC_PROTO));

#ifdef MEMDEBUG
#  define openhtbuf()           openhtbuf(__FILE__, __LINE__, CHARPN)
#  define closehtbuf(buf)       closehtbuf((buf), __FILE__, __LINE__, CHARPN)
#  define TxfmtcpDup(src, pmbuf) TxfmtcpDup((src), (pmbuf), __FILE__,__LINE__, CHARPN)
#  define TxfmtcpClose(fmtcp)   TxfmtcpClose((fmtcp), __FILE__, __LINE__, CHARPN)
#endif /* MEMDEBUG */

int TxfmtcpSetQuerySetStyles(TXFMTCP *fmtcp, TXPMBUF *pmbuf,
                             char **querySetStyles, int fmtcpOwns);
int TxfmtcpSetQuerySetClasses(TXFMTCP *fmtcp, TXPMBUF *pmbuf,
                              char **querySetClasses, int fmtcpOwns);
int TxfmtcpCreateStylesheet(HTBUF *buf, CONST TXFMTCP *fmtcp);

void htbuf_init ARGS((HTBUF *buf));
size_t htbuf_getdata ARGS((HTBUF *buf, char **data, int flags));
int    htbuf_setmaxsz ARGS((HTBUF *buf, size_t maxsz));
void   htbuf_setdata ARGS((HTBUF *buf, char *data, size_t cnt, size_t sz,
                           int alloc));
size_t htbuf_getunused ARGS((HTBUF *buf, char **unused));
int    htbuf_addused2 ARGS((HTBUF *buf, int cnt, int syncSendlimit));
#define htbuf_addused(buf, cnt) htbuf_addused2((buf), (cnt), 0)
int    htbuf_delused ARGS((HTBUF *buf, size_t sz, int oksplit));
int    htbuf_cpfromhold ARGS((HTBUF *buf, char *dest, size_t sz));
int    htbuf_delhold ARGS((HTBUF *buf, size_t sz));
size_t htbuf_getsend ARGS((HTBUF *buf, char **data1, size_t *sz1,
                           char **data2, size_t *sz2));
int    htbuf_delsend ARGS((HTBUF *buf, size_t sz));
int    TXhtbufUnSend(HTBUF *buf, size_t sz);
int htbuf_clear ARGS((HTBUF *buf));
int htbuf_release ARGS((HTBUF *buf));
int htbuf_doinc ARGS((HTBUF *buf, size_t sz, int hard));
#define htbuf_inc(buf, sz)      htbuf_doinc(buf, sz, 1)
#define htbuf_softinc(buf, sz)  htbuf_doinc(buf, sz, 0)
size_t htbuf_getavail ARGS((HTBUF *buf, char **data1, size_t *sz1,
                            char **data2, size_t *sz2));
int    htbuf_decavail ARGS((HTBUF *buf, size_t sz, int sendtoo));
size_t htbuf_getdata2 ARGS((HTBUF *buf, char **data1, size_t *sz1,
                            char **data2, size_t *sz2));
int htbuf_write ARGS((HTBUF *buf, CONST char *data, size_t sz));
int htbuf_writehold ARGS((HTBUF *buf, CONST char *data, size_t sz));
int htbuf_rewrite ARGS((HTBUF *buf, CONST char *data, size_t sz));
int htbuf_insert ARGS((HTBUF *buf, size_t insOffset, CONST char *insData,
                       size_t insSz));
#define htbuf_unhold(buf)       htbuf_rewrite(buf, CHARPN, (size_t)(-1))
int htbuf_unhtml ARGS((HTBUF *buf, char *s, size_t len));
/* Callback in print engine to output data:   KNG 980719 */
typedef void (HTPFOUTCB) ARGS((void *outdata, char *s, size_t sz));
#define HTPFOUTCBPN     ((HTPFOUTCB *)NULL)
#ifdef CGI_H_GOT_VA_LIST
int htbuf_vpf ARGS((HTBUF *buf, CONST char *fmt, size_t fmtsz, HTPFF flags,
                    HTPF_SYS_VA_LIST argp, HTPFARG *args, size_t *argnp));
size_t htpfengine ARGS((CONST char *fmt, size_t fmtsz, HTPFF flags,
        CONST TXFMTCP *fmtcp, TXFMTSTATE *fs, HTPF_SYS_VA_LIST argp,
        HTPFCB *getarg, void *getdata, HTPFARG *args, size_t *argnp,
        HTPFOUTCB *out, void *outdata, TXPMBUF *pmbuf));
#endif /* CGI_H_GOT_VA_LIST */
int CDECL htbuf_pf ARGS((HTBUF *buf, CONST char *fmt, ...));
int htbuf_cpf ARGS((HTBUF *buf, CONST char *fmt, size_t fmtsz, HTPFF flags,
                    HTPFCB *cb, void *data));
int htbuf_setfmtstate ARGS((HTBUF *buf, TXFMTSTATE *fs));
TXFMTSTATE *htbuf_getfmtstate ARGS((HTBUF *buf));
int htbuf_setfmtcp ARGS((HTBUF *buf, TXFMTCP *fmtcp, int htbufOwns));
TXFMTCP *htbuf_getfmtcp ARGS((HTBUF *buf));
int htskipeol ARGS((char **sp, char *e));
int TXskipEolBackwards ARGS((CONST char *bufStart, CONST char **curLoc));
/* see also http.h */

/* ------------------------ passwd table stuff: ---------------------------- */

typedef struct CGIPWENT_tag
{
  char	*user, *pass;
  char	*ip;
  char	*txuser, *txpass;
  char	*url;
#ifdef USECGIPASSWD2
  char  *owner;
#endif
}
CGIPWENT;
#define CGIPWENTPN	((CGIPWENT *)NULL)

void freecgipwent ARGS((CGIPWENT *pw));
#ifdef TSQL
int cgigetpwnam ARGS((TSQL *ts, char *tbl, char *user, char *pass,
		      CGIPWENT **pwp));
int cgisetpwent ARGS((TSQL *ts, char *tbl));
int cgiendpwent ARGS((TSQL *ts));
CGIPWENT *cgigetpwent ARGS((TSQL *ts));

int  cgipwupdate ARGS((TSQL *ts, char *tbl, CGIPWENT *pw));
int  cgipwcreate ARGS((TSQL *ts, char *tbl, CGIPWENT *pw));
int  cgipwdelete ARGS((TSQL *ts, char *tbl, char *user));
#endif  /* TSQL */

#include "mime.h"
/* ------------------------------- PDF XML --------------------------------- */

char **pdfxml ARGS((APICP *cp,char *query,char *buf,char *end,char *color,int flags,int startpg,CONST char *charset));
char **freexml ARGS((char **));
#define PDFXML_UNIT_WORDS     1
#define PDFXML_UNIT_CHARS     2
#define PDFXML_MODE_ACTIVE    4
#define PDFXML_MODE_PASSIVE   8
#define PDFXML_SHOWHIT       16

/************************************************************************/
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CGI_H */
