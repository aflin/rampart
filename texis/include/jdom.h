#ifndef JDOM_H
#define JDOM_H

#ifndef TEXINT_H
#  include "texint.h"
#endif
#ifndef HTMLL_H
#  include "htmll.h"
#endif
#ifndef WILD_H
#  include "wild.h"
#endif


/* Public (to html lib internals) JavaScript/DOM function prototypes,
 * used by htformat.c
 *
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >> If any structs, etc. change here or httpi.h etc., update JDOM_VERSION <<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

#define JDOM_VERSION    0x20200211      /* YYYYMMDD lib compat version # */

#ifdef _WIN32
#  define JD_EXTERN_API(__type)  extern _declspec(dllexport) CDECL __type
#  define JD_EXPORT_API(__type)         _declspec(dllexport) CDECL __type
#  define JD_EXTERN_DATA(__type) extern _declspec(dllexport)       __type
#  define JD_EXPORT_DATA(__type)        _declspec(dllexport)       __type
#else /* !_WIN32 */
#  define JD_EXTERN_API(__type)  extern                            __type
#  define JD_EXPORT_API(__type)                                    __type
#  define JD_EXTERN_DATA(__type) extern                            __type
#  define JD_EXPORT_DATA(__type)                                   __type
#endif /* !_WIN32 */

/* Symbols which the lib will need to import manually from the executable: */
#define JDOMIMPORTS_LIST                                        \
I(int,          TXaddabendloccb, (TXABENDLOCCB *func, void *usr)) \
I(void *,       TXcalloc,       (TXPMBUF *pmbuf, CONST char *fn, size_t n, size_t sz TXALLOC_PROTO)) \
I(int,          TXdelabendloccb, (TXABENDLOCCB *func, void *usr)) \
I(CONST char *, TXfetchInputTypeToString, (HTINTYPE inputType)) \
I(int,          TXgetBacktrace, (char *buf, size_t bufSz, int flags)) \
I(void *,       TXfree,         (void *p TXALLOC_PROTO))        \
I(void *,       TXmalloc,       (TXPMBUF *pmbuf, CONST char *fn, size_t sz TXALLOC_PROTO))      \
I(void *,       TXrealloc,      (TXPMBUF *pmbuf, CONST char *fn, void *p, size_t sz TXALLOC_PROTO)) \
I(char *,       TXstrdup,       (TXPMBUF *pmbuf, CONST char *fn, CONST char *s TXALLOC_PROTO))  \
I(CONST char *, TXgetlocale,    (void))                         \
I(double,       TXgettimeofday, (void))                         \
I(time_t,       TXindparsetime, (const char *buf, size_t bufSz, int msg, \
                                 TXPMBUF *pmbuf))               \
I(HTHOST2IP *,  TXfetchGetHost2ipFunc, (HTOBJ *obj))            \
I(int,          TXprintSz, (char *buf, size_t bufSz, EPI_HUGEINT num)) \
I(size_t,       TXrefInfoListCount, (TXrefInfo **refInfos,      \
                                     TXrefTypeFlag types))      \
I(int,          TXrefInfoSetStrBaseUrl, (TXrefInfo *refInfo,    \
                        const char *StrBaseUrl, size_t StrBaseUrlLen))      \
I(int,          TXsetalarm,     (TXALARMFUNC *func, void *usr, double sec, TXbool inSig)) \
I(int,          TXunsetalarm,   (TXALARMFUNC *func, void *usr, double*secp)) \
I(HTBUF *,      closehtbuf,     (HTBUF *buf TXALLOC_PROTO))     \
I(HTTIMER *,    closehttimer,   (HTTIMER *timer))               \
I(URL *,        closeurl,       (URL *url))                     \
I(WILD *,       closewild,      (WILD *w))                      \
I(char **,      getcgisl,       (CGISL *sl, char *name))        \
I(TXbool,       htaddcookietojar, (HTOBJ *obj, const HTPAGE *pg, const char *hdrval, TXbool fromScript, TXbool isMeta)) \
I(TXrefInfo *, htaddurl,        (HTOBJ *obj, HTPAGE *pg, const char *file, \
                       int bufnum, int line, int col, TXrefTypeFlag types, \
                       const char *desc, const char *url, int flags)) \
I(size_t,       htbuf_getdata,  (HTBUF *buf, char **data, int release)) \
I(int,          htbuf_pf,       (HTBUF *buf, CONST char *fmt, ...)) \
I(int,          htbuf_write,    (HTBUF *buf, CONST char *data, size_t len)) \
I(char *,       htcontype,      (char *ext))                    \
I(CONST char * CONST *, hteventhandlernames,    (void))         \
I(TXPMBUF *,    htgetpmbuf,     (HTOBJ *obj))                   \
I(int,          htgettimeout,   (HTOBJ *obj))                  \
I(CONST char *, htint2attr,     (HATTR attr))                   \
I(CONST char *, htint2tag,      (HTAG tag))                     \
I(CONST HTMIMETYPE *,   htdefaultmimetypelist,  (void))         \
I(int,          htprcookiehdrval, (HTBUF *buf, HTOBJ *obj, CONST URL *u, int fromScript)) \
I(int,          htsetdomvaluebyobj, (HTOBJ *htObj, void *obj,   \
     HTDOMOBJTYPE objtype, CONST char *prop, CONST char *val, size_t valsz)) \
I(int,          htsettimeout,   (HTOBJ *obj, int timeout))      \
I(int,          htsnpf,         (char *, size_t, CONST char *, ...)) \
I(int,          htstr2clr,      (char *s))                      \
I(HTMETH,       htstr2method,   (CONST char *s, CONST char *e)) \
I(const char *, htstrerror,     (int err))                      \
I(char *,       hturlabsolute,  (char *url, char *relatedurl, CONST byte *htsfFlags)) \
I(char *,       hturlmerge,     (URL *url))                     \
I(size_t,       TXhostAndPortToSockaddrs, (TXPMBUF *pmbuf,TXbool suppressErrs,\
      TXtraceDns traceDns, const char *func, TXaddrFamily addrFamily,   \
 const char *host, unsigned port, TXbool dnsLookup, TXbool okIPv4WithIPv6Any,\
      TXsockaddr *sockaddrs, size_t numSockaddrs))              \
I(TXbool,       TXsockaddrToStringIP, (TXPMBUF *pmbuf,                  \
  const TXsockaddr *sockaddr, char *ipBuf, size_t ipBufSz))             \
I(char *,       TXunicodeEncodeUtf8Char, (char *d, char *de, TXUNICHAR ch))  \
I(int,          TXunicodeStrFoldCmp, (const char **ap, size_t alen, \
                            const char **bp, size_t blen, TXCFF modeFlags)) \
I(TXbool,       TXezGetMyIP, (TXPMBUF *pmbuf, TXtraceDns traceDns,      \
                              TXaddrFamily addrFamily,                  \
             TXsockaddr *sockaddr, char *ipBuf, size_t ipBufSz)) \
I(HTBUF *,      openhtbuf,      (TXALLOC_PROTO_SOLE))           \
I(HTTIMER *,    openhttimer,    (HTOBJ *, HTWINDOW *, CONST char *, int, CONST char *, int, HTTIMERF)) \
I(URL *,        openurl,        (const char *s))                \
I(WILD *,       openwild2,      (const char *expr, const char *exprEnd, \
                     const char *fixedEnd, char escChar, TXwildFlag flags)) \
I(char *,       strstri,        (CONST char *, CONST char *))   \
I(int,          strnicmp,       (CONST char *, CONST char *, size_t)) \
I(int,          strcmpi,        (CONST char *, CONST char *)) \
I(int,  TXosTime_tToGmtTxtimeinfo, (time_t tim, TXTIMEINFO *timeinfo))   \
I(int,  TXosTime_tToLocalTxtimeinfo, (time_t tim, TXTIMEINFO *timeinfo)) \
I(int,          txpmbuf_putmsg, (TXPMBUF *, int, CONST char *, CONST char *, ...)) \
I(TXPMBUF *,    txpmbuf_open, (TXPMBUF *pmbuf)) \
I(TXPMBUF *,    txpmbuf_close, (TXPMBUF *pmbuf))                \
I(int,    TXtxtimeinfoToTime_t, (const TXTIMEINFO *timeinfo, time_t *timP)) \
I(int,          wildmatch,      (WILD *w, char *filename))

typedef struct JDOMIMPORTS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
  JDOMIMPORTS_LIST
#undef I
}
JDOMIMPORTS;
#define JDOMIMPORTSPN   ((JDOMIMPORTS *)NULL)

#ifndef HTJDPN
typedef struct HTJD_tag HTJD;
#  define HTJDPN        ((HTJD *)NULL)
#endif

/* This macro keeps function names, prototypes, and strings in one place: */
#define JDOMSYMBOLS_LIST                                        \
I(int,          jd_getapiversion, (void))                       \
I(int,          jd_initapi, (CONST JDOMIMPORTS *imp, size_t sz)) \
I(time_t,       jd_getseconds, (void))                          \
I(CONST char *, jd_getplatform, (void))                         \
I(int,          jd_getversion, (int *jsmajor, int *jsminor))    \
I(HTJD *,       jd_open, (HTOBJ *obj, HTPAGE *pg, int forPac))  \
I(HTJD *,       jd_close, (HTJD *jd))                           \
I(HTERR,        jd_gethterrno, (HTJD *jd))                      \
I(TXPMBUF *,    jd_getpmbuf, (HTJD *jd))                        \
I(int,          jd_setpmbuf, (HTJD *jd, TXPMBUF *pmbuf))        \
I(int,          jd_runscript, (HTJD *jd, HTPAGE *pg, char *src, size_t srcSz,\
                               const char *file, int lineo))    \
I(char *,       jd_callfunctionname, (HTJD *jd, const char *urlOrFile, \
                            const char *func,int argc, char **argv))   \
I(int,          jd_addanchor, (HTANCHOR *an))                   \
I(int,          jd_addelement, (HTELEMENT *element))            \
I(int,          jd_elementCloseJavaScript, (HTELEMENT *element))\
I(int,          jd_addform, (HTFORM *form))                     \
I(int,          jd_formCloseJavaScript, (HTFORM *form))         \
I(int,          jd_addinput, (HTINPUT *input))                  \
I(int,          jd_inputCloseJavaScript, (HTINPUT *input))      \
I(int,          jd_addoption, (HTOPTION *option))               \
I(int,          jd_addlink, (HTLINK *link))                     \
I(int,          jd_linkCloseJavaScript, (HTLINK *link))         \
I(int,          jd_addimage, (HTIMAGE *image))                  \
I(int,          jd_imageCloseJavaScript, (HTIMAGE *image))      \
I(int,          jd_addeventhandler, (HTPAGE*,void*,HTEVTYPE,CONST char*,int))\
I(int,          jd_calleventhandler, (HTPAGE *, void*,HTEVTYPE,void*,void*)) \
I(int,          jd_getdocbuf, (HTPAGE *pg, char **bufp, size_t *szp)) \
I(int,          jd_windowCloseJavaScript, (HTWINDOW *window))   \
I(int,          jd_documentCloseJavaScript, (HTPAGE *pg))       \
I(int,          jd_addframe, (HTPAGE *pg, CONST char *name))

typedef struct JDOMSYMBOLS_tag
{
#undef I
#define I(ret, func, args)      ret (CDECL *func) ARGS(args);
  JDOMSYMBOLS_LIST
#undef I
}
JDOMSYMBOLS;
#define JDOMSYMBOLSPN   ((JDOMSYMBOLS *)NULL)

extern JDOMSYMBOLS      TXjdomSymbols;

#undef I
#define I(ret, func, args)      JD_EXTERN_API(ret) func ARGS(args);
JDOMSYMBOLS_LIST
#undef I

/* ------------------------------ jdsec.c: -------------------------------- */

JD_EXTERN_DATA(const long)      JdSeconds;

#endif /* !JDOM_H */
