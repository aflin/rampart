#ifndef HTTP_H
#define HTTP_H
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef CGI_H
#  include "cgi.h"
#endif
#ifndef TX_REFINFO_H
#  include "refInfo.h"
#endif /* TX_REFINFO_H */
#ifndef TXTYPES_H
#  include "txtypes.h"
#endif /* TXTYPES_H */
#include "htmll.h"
#include "ezsock.h"
#include "urlprotocols.h"
#include "urlutils.h"

#ifndef TX_VERSION_NUM
error: TX_VERSION_NUM undefined;
#endif /* !TX_VERSION_NUM */


/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >> If any structs, etc. change here or elsewhere, update JDOM_VERSION <<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

/* may also be defined in cgi.h: */
#ifndef HTOBJPN
typedef struct HTOBJ_tag        HTOBJ;  /* defined in httpi.h */
#  define HTOBJPN       ((HTOBJ *)NULL)
#  define HTOBJPPN      ((HTOBJ **)NULL)
#endif /* !HTOBJPN */

#ifndef HTDNSPN                         /* WTF avoid htdns.h include */
typedef struct HTDNS_tag        HTDNS;
#  define HTDNSPN         ((HTDNS *)NULL)
#  define HTDNSPPN        ((HTDNS **)NULL)
#endif /* !HTDNSPN */

typedef enum REPARENT_tag       /* reparenting mode */
{
  REPARENT_UNKNOWN = -1,/* unknown mode */
  REPARENT_NONE,        /* no reparenting */
  REPARENT_ABS,         /* links made absolute */
  REPARENT_REROOT,      /* links made relative to new root */
  REPARENT_MIRROR,      /* links made absolute, appended to mirror URL */
  REPARENT_RELATEDFILES,/* fix up multipart/related using file refs */
  REPARENT_HIDEEXTERNAL /* hide external links: prefix `thismessage:/' */
}
REPARENT;

REPARENT TXfetchReparentStrToToken ARGS((CONST char *s, CONST char *e));
CONST char *TXfetchReparentTokenToStr ARGS((REPARENT mode));

#define TX_HTSF_SYMBOLS_LIST                                                \
I(DO8BIT)                           /* do 8-bit chars when formatting */    \
I(REDIRMSGS)                        /* issue redirect info messages */      \
I(IGNOREALT)                        /* ignore ALT text in images */         \
I(REPARENTIMG)                      /* re-parent image URLs too */          \
I(NOSTRIKE)                         /* don't include <STRIKE> text */       \
I(OFFSITEOK)                        /* off-site references are ok */        \
I(ALARMCLOSE)                       /* alarm close() in case it blocks */   \
I(NODEL)                            /* don't include <DEL> text */          \
I(EATLINKSPACE)                     /* eat lead/trail space in links */     \
I(UrlCollapseSlashes)               /* `//' -> `/' in URLs */               \
I(CLIENTERRMSGS)                    /* yap about 4xx errors */              \
I(NOSELECT)                         /* no <SELECT> (<OPTIONS>) text */      \
I(NOINPUT)                          /* no <INPUT> values in text */         \
I(NOTEXTAREA)                       /* no <TEXTAREA> ... </TEXTAREA> */     \
I(STRICTCOMMENT)                    /* no <!text> comments, only <!-- */    \
I(NESTCOMMENT)                      /* allow nested comments */             \
I(NOCONNMSG)                        /* be silent on connection refused */   \
I(DELAYSAVE)                        /* delay savefile open until data */    \
I(ALLOWPUNCT)                       /* set HF_ALLOWPUNCT when parsing */    \
I(STRIPNONPRINTURL)                 /* strip non-(space to ~) in URLs */    \
I(ENCNONPRINTURL)                   /* encode non-(space to ~) in URLs */   \
I(JAVASCRIPT)                       /* enable JavaScript language */        \
I(COOKIEMSGS)                       /* issue cookie info messages */        \
I(ACCEPTNEWCOOKIES)                 /* whether to accept new cookies */     \
I(SENDCOOKIES)                      /* whether to send cookies */           \
I(SCRIPTLINKS)                      /* run javascript: links on page */     \
I(SCRIPTMSGS)                       /* whether to print JavaScript msgs */  \
I(GETSCRIPTS)                       /* fetch and run <SCRIPT SRC=url> */    \
I(SCRIPTURLDECODE)                  /* URL-decode javascript: URLs */       \
I(SCRIPTVARYEVENTS)                 /* modify page, fire script events */   \
I(URLCANONSLASH)                    /* flip backslash to forward slash */   \
I(LINKSUSEPROT)                     /* use protocolmask for links too */    \
I(SCRIPTREALTIMERS)                 /* real-time JavaScript timers */       \
I(NETMODESYS)                       /* use system net API, not internal */  \
I(STRLINKPROT)                      /* get protocol URLs from Strings */    \
I(STRLINKFILE)                      /* get file-like URLs from Strings */   \
I(STRLINKALL)                       /* get all Strings */                   \
I(STRLINKABS)                       /* absolute <urlinfo strlinks> */       \
I(CHARSETMSGS)                      /* issue charset convert msgs */        \
I(SENDEMPTYCONTENT)                 /* send content even if empty */        \
I(EMPTYHTTP09OK)                    /* accept empty HTTP 0.9 responses */   \
I(CHARSETPARTIALCONVOK)             /* accept partial charset conversion*/  \
I(BADHDRMIDOK)                      /* accept malformed middle header */    \
I(BADHDRMSGS)                       /* issue bad header msgs */             \
I(SORTFILEDIRS)                     /* sort file:// dir lists */            \
I(LINGER)                           /* set SO_LINGER */                     \
I(SHUTDOWNWR)                       /* use shutdown(SHUT_WR) if possible*/  \
I(XMLTAGS)                          /* skip XML <?...?> <![CDATA[...]]> */  \
I(CHECKIDLECONNEOF)                 /* check idle connection for EOF */     \
I(FTPCONTENTLENGTH)                 /* parse FTP RETR response for size */  \
I(FTPPASSIVE)                       /* use FTP passive mode first */        \
I(COOKIEWILDCARDS)                  /* respect domain/path wildcards */     \
I(MSIECOOKIEDOMAIN)                 /* assume MSIE cookies are `.domain'*/  \
I(FTPRELATIVEPATHS)                 /* RFC 1738: strip leading `/' */       \
I(FORMATXMLASHTML)                  /* format XML as HTML */                \
I(SENDLMRESPONSE)                   /* send LM responses in NTLM auth */    \
I(SENDNTLMRESPONSE)                 /* send NTLM responses in NTLM auth */  \
I(ACCEPTCOOKIEMODS)                 /* accept cookie modifications */       \
I(ACCEPTCOOKIEDELS)                 /* accept cookie deletes */             \
                                                                            \
/* Public Ntlm[v1|v2]{Ignore|Accept|Request|Require}... settings are */     \
/* mapped to 2 actual `Offer'/`Require...IfOffered' flags as follows: */    \
                                                                            \
/*               Offer...    Require...IfOffered        */                  \
/* Ignore...  =    0                 0                  */                  \
/* Accept...  =    0                 1                  */                  \
/* Request... =    1                 0                  */                  \
/* Require... =    1                 1                  */                  \
                                                                            \
I(Ntlmv1OfferSigning)               /* offer signing during NTLMv1 */       \
I(Ntlmv1RequireSigningIfOffered)    /* require "" if either side offers */  \
I(Ntlmv1OfferSealing)               /* offer sealing during NTLMv1 */       \
I(Ntlmv1RequireSealingIfOffered)    /* require "" if either side offers */  \
I(Ntlmv1OfferNtlmv2SessionSecurity) /* offer NTLMv2 sess. sec. during v1*/  \
I(Ntlmv1RequireNtlmv2SessionSecurityIfOffered) /* require "" if offered*/   \
I(Ntlmv1Offer128bitEncryption)      /* offer 128-bit encryption for v1*/    \
I(Ntlmv1Require128bitEncryptionIfOffered) /* require "" if offered */       \
                                                                            \
I(Ntlmv2OfferSigning)               /* offer signing during NTLMv2 */       \
I(Ntlmv2RequireSigningIfOffered)    /* require "" if either side offers */  \
I(Ntlmv2OfferSealing)               /* offer sealing during NTLMv2 */       \
I(Ntlmv2RequireSealingIfOffered)    /* require "" if either side offers */  \
I(Ntlmv2OfferNtlmv2SessionSecurity) /* offer NTLMv2 sess. sec. during v2*/  \
I(Ntlmv2RequireNtlmv2SessionSecurityIfOffered) /* require "" if offered*/   \
I(Ntlmv2Offer128bitEncryption)      /* offer 128-bit encryption for v2*/    \
I(Ntlmv2Require128bitEncryptionIfOffered) /* require "" if offered */       \
                                                                            \
I(CookieDomainMatchSelf)            /* `x.y' domain-matches `.x.y' */       \
I(FileDirRobotsIndex)               /* <meta robots> index for file:// */   \
I(FileDirRobotsFollow)              /* <meta robots> follow for file:// */  \
I(FtpDirRobotsIndex)                /* <meta robots> index for ftp:// */    \
I(FtpDirRobotsFollow)               /* <meta robots> follow for ftp:// */   \
I(Utf8BadEncAsIso88591)             /* take bad UTF-8 bytes as ISO */       \
/* See UTF_BADENCASISOERR comments.  Turning this off also makes */         \
/* bad-UTF-8-as-ISO no longer an error: */                                  \
I(Utf8BadEncAsIso88591Err)          /* yap/err if bad UTF-8 as ISO */       \
I(SaveDownloadDoc)                  /* save download doc if distinct */     \
I(ContentLocationAsBaseUrl)         /* take Content-Location as Base URL*/  \
I(IgnoreAnchorFrames)               /* ignore src="#" frames/iframes */     \
I(FtpSendRelativePathsAsAbsolute)   /* get home dir w/PWD and prefix it */  \
I(FtpActivePassiveFallback)         /* fallback to other mode if failure*/  \
I(AllowBadChunkedInfo)              /* bad Chunked info: pass data as-is*/  \
I(AllowInputFileDefault)            /* allow <input type="file"> default*/  \
I(AllowAnyCookiePath)               /* allow any non-wild Cookie Path */    \
I(AllowAnyCookieDomain)             /* allow any non-wild Cookie Domain */  \
I(NonClientErrResponseMsgs)         /* report non-client-err responses */   \
I(ScriptAsserts)                    /* report JavsScript assert failures*/  \
I(SslUseSni)                        /* use Server Name Indication */        \
I(AuthenticateServerMutually)       /* experimental/subject to change */    \
I(PacMsgs)                                                                  \
I(IPv6ScopeIdInHostHeader)          /* leave IPv6 scope id in Host: hdrs */ \
I(PreserveCanonPortCleanUrl)        /* save canon port when cleaning URLs */\
I(MetaCookies)                      /* accept <meta http-equiv> cookies */  \
I(OffSiteFetchMsgsAndErr)           /* `Not fetching off-site' msgs+err */  \
I(UserDataFetchMsgsAndErr)          /* `... User-data fetch ...' msgs+err */\
I(SslLegacyServerConnect)           /* set SSL_OP_LEGACY_SERVER_CONNECT */
/* >>>>> NOTE: if adding more flags, must update JDOM_VERSION, <<<<<
 * >>>>> as HTOBJ struct size will change (`flags' increases)  <<<<<
 */

typedef enum HTSF_tag
{
#undef I
#define I(tok)  HTSF_##tok,
TX_HTSF_SYMBOLS_LIST
#undef I
  HTSF_NUM                              /* must be last */
}
HTSF;
#define HTSFPN          ((HTSF *)NULL)

#define HTSF_BYTE_ARRAY_SZ              TX_BIT_ARRAY_BYTE_SZ(HTSF_NUM)
#define HTSF_SET_FLAG(array, flag)      TX_BIT_ARRAY_SET_BIT(array, flag)
#define HTSF_CLR_FLAG(array, flag)      TX_BIT_ARRAY_CLR_BIT(array, flag)
#define HTSF_GET_FLAG(array, flag)      TX_BIT_ARRAY_GET_BIT(array, flag)

#ifndef HTSKTPN
typedef struct HTSKT_tag HTSKT;
#  define HTSKTPN ((HTSKT *)NULL)
#endif /* !HTSKTPN */

/* Fetch methods we know of (not all supported).
 * Note that HTTP methods must be first, through HTMETH_LAST_HTTP:
#ifdef FULL_WEBDAV
I(COPY)
I(MOVE)
I(LOCK)
I(UNLOCK)
#endif
 */
#define HTMETH_SYMBOLS_LIST     \
I(OPTIONS)                      \
I(GET)                          \
I(HEAD)                         \
I(POST)                         \
I(PUT)                          \
I(DELETE)                       \
I(TRACE)                        \
I(PROPFIND)                     \
I(PROPPATCH)                    \
I(MKCOL)                        \
I(SEARCH)                       \
I(CONNECT)                      \
I(MKDIR)                        \
I(RENAME)                       \
I(SCHEDULE)                     \
I(COMPILE)                      \
I(RUN)                          \
I(CREATELOCKS)

#define HTMETH_LAST_HTTP        HTMETH_CONNECT

typedef enum HTMETH_tag
{
  HTMETH_UNKNOWN = -1,          /* must be first */
#undef I
#define I(tok)  HTMETH_##tok,
HTMETH_SYMBOLS_LIST
#undef I
  HTMETH_NUM                    /* must be last */
}
HTMETH;
#define HTMETHPN        ((HTMETH *)NULL)

/* Error codes (see hterr.c / etc. if changed).  Preserve order/values:
 * I(enumToken, userTokenStr, errMsg)
 * HTERR_OK must be first (0):
 */
#define TX_HTERR_SYMBOLS_LIST   \
I(OK,                           "Ok", \
  "Ok") \
I(CLIENT_ERR,                   "ClientErr", \
  "Unknown client error") \
I(SERVER_ERR,                   "ServerErr", \
  "Server error") \
I(UNK_CODE,                     "UnkResponseCode", \
  "Unrecognized response code") \
I(UNK_VERS,                     "UnkProtocolVersion", \
  "Unrecognized protocol version") \
I(CONN_TIMEOUT,                 "ConnTimeout", \
  "Connection timeout") \
I(UNK_HOST,                     "UnkHost", \
  "Unknown host") \
I(CANT_CONNECT,                 "CannotConn", \
  "Cannot connect to host") \
I(NOT_CONNECT,                  "NotConn", \
  "Not connected") \
I(CANT_CLOSE,                   "CannotCloseConn", \
  "Cannot close connection") \
I(CANT_WRITE,                   "CannotWriteConn", \
  "Cannot write to connection") \
I(CANT_READ,                    "CannotReadConn", \
  "Cannot read from connection") \
I(CANT_WRITE_FILE,              "CannotWriteFile", \
  "Cannot write to file") \
I(NO_MEM,                       "OutOfMem", \
  "Out of memory") \
I(PAGE_TRUNC,                   "PageTrunc", \
  "Page not expected size, possibly truncated") \
I(PAGE_TOO_BIG,                 "MaxPageSizeExceeded", \
  "Max page size exceeded, truncated") \
I(TOO_MANY_REDIRS,              "TooManyRedirs", \
  "Too many redirects") \
I(OFFSITE_REF,                  "OffsiteRef", \
  "Off-site or unapproved redirect or frame") \
I(UNK_ACCESS,                   "UnkProtocol", \
  "Unknown/unimplemented access protocol") \
I(BAD_PARAM,                    "BadParam", \
  "Bad parameter") \
I(UNK,                          "UnkErr", \
  "Unknown error") \
I(BAD_REDIR,                    "BadRedir", \
  "Bad redirect") \
I(UNAUTHORIZED,                 "DocUnauth", \
  "Document access unauthorized") \
I(FORBIDDEN,                    "DocForbidden", \
  "Document access forbidden") \
I(NOT_FOUND,                    "DocNotFound", \
  "Document not found") \
I(SERVER_NOT_IMPLEMENTED,       "ServerNotImplemented", \
  "Server did not recognize request (unimplemented)") \
I(SERVICE_UNAVAIL,              "ServiceUnavailable", \
  "Service unavailable") \
I(UNK_METHOD,                   "UnkMethod", \
  "Unknown request method") \
I(CANT_READ_FILE,               "CannotReadFile", \
  "Cannot read from file") \
I(CANT_LOAD_LIB,                "CannotLoadLib", \
  "Cannot load dynamic library") \
I(SCRIPT_ERR,                   "ScriptErr", \
  "Script error") \
I(SCRIPT_TIMEOUT,               "ScriptTimeout", \
  "Script timeout") \
I(SCRIPT_MEM,                   "ScriptMemExceeded", \
  "Script memory limit exceeded") \
I(DISALLOWED_PROTOCOL,          "DisallowedProtocol", \
  "Disallowed protocol") \
I(SSL_ERR,                      "SslErr", \
  "SSL error") \
I(PROXY_UNAUTHORIZED,           "ProxyUnauth", \
  "Proxy access unauthorized") \
I(EMBED_SECURITY_CHANGE,        "EmbeddedSecurityChange", \
  "Embedded object security change") \
I(DISALLOWED_FILE_PREFIX,       "DisallowedFilePrefix", \
  "Disallowed file prefix") \
I(DISALLOWED_FILE_TYPE,         "DisallowedFileType", \
  "Disallowed file type") \
I(DISALLOWED_FILE_NONLOCAL,     "DisallowedNonlocalFileUrl", \
  "Disallowed non-local file URL") \
I(CANT_CONVERT_CHARSET,         "CannotConvertCharset", \
  "Cannot convert character set") \
I(DISALLOWED_AUTH_SCHEME,       "DisallowedAuthScheme", \
  "Disallowed authentication scheme") \
I(SECURE_NOT_POSSIBLE,          "SecureTransNotPossible", \
  "Secure transaction not possible") \
I(UNEXPECTED_RESPONSE,          "UnexpectedResponseCode", \
  "Unexpected server response") \
I(DISALLOWED_METHOD,            "DisallowedMethod", \
  "Disallowed request method") \
I(UPGRADE_TO_SSL_REQUIRED,      "ConnUpgradeToSslRequired", \
  "Connection upgrade to SSL required") \
I(NOT_PERMITTED_BY_LICENSE,     "FetchNotPermittedByLicense", \
  "Fetch not permitted by license")     \
I(UNKNOWN_CONTENT_ENCODING,     "UnknownContentEncoding", \
  "Unknown Content- or Transfer-Encoding")      \
I(DISALLOWED_CONTENT_ENCODING,  "DisallowedContentEncoding", \
  "Disallowed Content- or Transfer-Encoding")   \
I(CANNOT_DECODE_CONTENT_ENCODING,  "CannotDecodeContentEncoding", \
  "Cannot decode Content- or Transfer-Encoding")\
I(NOT_ACCEPTABLE,               "NotAcceptable",        \
  "Client-acceptable version not found")        \
I(CANNOT_VERIFY_SERVER_CERTIFICATE,     "CannotVerifyServerCertificate",   \
  "Cannot verify server certificate")   \
I(CONNECTION_NOT_REUSABLE,      "ConnectionNotReusable",  \
  "Connection not reusable")            \
I(CANNOT_TUNNEL_PROTOCOL,       "CannotTunnelProtocol", \
  "Cannot tunnel protocol")             \
I(PacError,                     "PacError", \
  "Proxy auto-config error")            \
I(UserDataFetchNeedsMoreData,   "UserDataFetchNeedsMoreData",   \
  "User-data fetch needs more data")    \
I(ExternalComponentError,       "ExternalComponentError",       \
  "External page component (frame/iframe/script src etc.) error")

typedef enum HTERR_tag
{
#undef I
#define I(enumToken, userTokenStr, errMsg) HTERR_##enumToken,
  TX_HTERR_SYMBOLS_LIST
#undef I
  HTERR_NUM			/* number of possible errors (must be last) */
}
HTERR;
#define HTERRPN         ((HTERR *)NULL)

/* ----------------------------- proxy.c: --------------------------------- */

/* Default is first; should be `auto': */
#define TXproxyMode_Default     TXproxyMode_auto
#define TX_PROXY_MODE_SYMBOLS_LIST      \
  I(auto)       \
  I(proxy)      \
  I(tunnel)

typedef enum TXproxyMode_tag
{
  TXproxyMode_Unknown = -1,                     /* must be first and -1 */
#undef I
#define I(tok)  TXproxyMode_##tok,
  TX_PROXY_MODE_SYMBOLS_LIST
#undef I
  TXproxyMode_NUM                               /* must be last */
}
TXproxyMode;

/* A single proxy: */
typedef struct TXproxy_tag
{
  HTPROT        protocol;                       /* HTPROT_UNKNOWN: direct */
  char          *host;                          /* alloced */
  unsigned      port;
}
TXproxy;

TXproxy *TXproxyOpen(TXPMBUF *pmbuf, HTERR *hterrno, HTPROT protocol,
                     const char *hostEncoded, const char *hostEncodedEnd,
                     unsigned port);
TXproxy *TXproxyOpenFromUrl(TXPMBUF *pmbuf, HTERR *hterrno, const char *url);
TXproxy *TXproxyClose(TXproxy *proxy);
TXproxy *TXproxyDup(TXPMBUF *pmbuf, TXproxy *proxy);
TXproxy **TXproxyListDup(TXPMBUF *pmbuf, TXproxy **list, size_t listNum);
TXproxy **TXproxyListClose(TXproxy **list, size_t listNum);
TXproxy **TXproxyListCreateDirect(TXPMBUF *pmbuf);

/* ------------------------------------------------------------------------ */

typedef struct HTPAGE_tag HTPAGE;
#define HTPAGEPN	((HTPAGE *)NULL)
#define HTPAGEPPN       ((HTPAGE **)NULL)

typedef struct HTATTR_tag               /* Attr object for JavaScript */
{
  unsigned              namenum;        /* HATTR_... value (if known) else */
  char                  *namestr;       /*   upper-case name (if unknown) */
  char                  *value;         /* (optional) string value */
}
HTATTR;
#define HTATTRPN        ((HTATTR *)NULL)

typedef enum HTELEMENTF_tag
{
  HTELEMENTF_ALL        = -1,
  HTELEMENTF_ENDTAG     = (1 << 0),     /* this is an end tag */
  HTELEMENTF_ALONE      = (1 << 1)      /* not inherited by another object */
}
HTELEMENTF;
#define HTELEMENTFPN    ((HTELEMENTF *)NULL)

typedef struct HTELEMENT_tag            /* Element object for JavaScript */
{                                       /* NOTE: inherited by other objects */
  HTPAGE                *pg;            /* parent page */
  unsigned              tagnum;         /* HTAG_... value (if known) else */
  char                  *tagstr;        /*   upper-case name (if unknown) */
  char                  *id;            /* id attribute (opt., case-sens.) */
  char                  *name;          /* name attribute (opt., case-sens) */
  HTELEMENTF            flags;
  void                  *jsobj;         /* internal JavaScript JSObject */
  HTATTR                *attrs;         /* (optional) attributes */
  size_t                nattrs;
  byte                  jsobjRootedByElement;   /* boolean */
}
HTELEMENT;
#define HTELEMENTPN     ((HTELEMENT *)NULL)
#define HTELEMENTPPN    ((HTELEMENT **)NULL)

/* event types; must be in ascending alphanumeric order: */
#define HTEVTYPE_LIST   \
I(Abort)                \
I(AfterPrint)           \
I(BeforePrint)          \
I(BeforeUnload)         \
I(Blur)                 \
I(Change)               \
I(Click)                \
I(Close)                \
I(DblClick)             \
I(DragDrop)             \
I(Error)                \
I(Focus)                \
I(Help)                 \
I(KeyDown)              \
I(KeyPress)             \
I(KeyUp)                \
I(Load)                 \
I(MouseDown)            \
I(MouseMove)            \
I(MouseOut)             \
I(MouseOver)            \
I(MouseUp)              \
I(Move)                 \
I(Reset)                \
I(Resize)               \
I(Scroll)               \
I(Select)               \
I(Submit)               \
I(Unload)

typedef enum HTEVTYPE_tag
{
  HTEVTYPE_UNKNOWN      = -1,
#undef I
#define I(event)        HTEVTYPE_##event,
  HTEVTYPE_LIST
#undef I
  HTEVTYPE_NUM                  /* must be last */
}
HTEVTYPE;
#define HTEVTYPEPN      ((HTEVTYPE *)NULL)

CONST char * CONST *hteventhandlernames ARGS((void));

#ifdef HTMLL_H
extern CONST HATTR      HtEventHandlerAttrs[HTEVTYPE_NUM];
#endif

typedef struct HTEVENT_tag              /* Event object (JavaScript) */
{
  HTPAGE                *pg;            /* parent page */
  HTEVTYPE              type;
  char                  *target;        /* may be NULL */
}
HTEVENT;
#define HTEVENTPN       ((HTEVENT *)NULL)

typedef struct HTANCHOR_tag             /* an <A NAME=...> tag */
{
  struct HTANCHOR_tag   *next;          /* next in list */
  HTPAGE                *pg;            /* our parent page */
  char                  *name;          /* <A NAME> of anchor */
  char                  *text;          /* formatted <A>...</A> text */
  size_t                fmtoff;         /* `text' offset from start of doc */
}
HTANCHOR;
#define HTANCHORPN      ((HTANCHOR *)NULL)

typedef struct HTCOOKIE_tag             /* header/<META>/JavaScript cookie */
{
  struct HTCOOKIE_tag   *next;          /* next in list */
  char                  *domain;
  char                  *path;
  byte                  secure;         /* secure flag */
  byte                  httpOnly;       /* nonzero: not for JavaScript */
  time_t                expires;        /* may be 0 */
  char                  *name;
  char                  *value;
}
HTCOOKIE;
#define HTCOOKIEPN      ((HTCOOKIE *)NULL)

#define TXfetchInputTypeSymbolsList             \
  I(BUTTON,             "button")               \
  I(CHECKBOX,           "checkbox")             \
  I(FILEUPLOAD,         "file")                 \
  I(HIDDEN,             "hidden")               \
  I(IMAGE,              "image")                \
  I(PASSWORD,           "password")             \
  I(RADIO,              "radio")                \
  I(RESET,              "reset")                \
  I(SELECT_ONE,         "select-one")           \
  I(SELECT_MULTIPLE,    "select-multiple")      \
  I(SUBMIT,             "submit")               \
  I(TEXT,               "text")                 \
  I(TEXTAREA,           "textarea")

typedef enum HTINTYPE_tag               /* type of <FORM> input elements */
{
  HTINTYPE_UNKNOWN      = -1,           /* must be first */
#undef I
#define I(tok, string)  HTINTYPE_##tok,
TXfetchInputTypeSymbolsList
#undef I
  HTINTYPE_NUM                          /* must be last */
}
HTINTYPE;
#define HTINTYPEPN      ((HTINTYPE *)NULL)

typedef struct HTOPTION_tag             /* an <OPTION> */
{
  struct HTOPTION_tag   *next;          /* next in list */
  struct HTINPUT_tag    *select;        /* our parent <SELECT> */
  char                  *text;          /* js-mod; display text */
  char                  *value;         /* js-mod; VALUE attribute */
  char                  defaultSelected;/* js-mod */
  char                  selected;       /* js-mod */
  char                  disabled;       /* js-mod */
}
HTOPTION;
#define HTOPTIONPN      ((HTOPTION *)NULL)

/* JavaScript puts same-named input objects into an array, so we
 * group our inputs by name as well:
 *
 *
 *                   +---------+--nextdiff-->+---------+--nextdiff-->NULL
 *                   | NAME=x  |             | NAME=y  |
 *  NULL<--prevdiff--|idxsame=0|<--prevdiff--|idxsame=0|
 *                   +---------+             +---------+
 *                        |                 ^     |
 *                    nextsame             /   nextsame
 *                        |               /       |
 *                        V              /        V
 *                   +---------+--nextdiff       NULL
 *                   | NAME=x  |
 *  NULL<--prevdiff--|idxsame=1|
 *                   +---------+
 *                        |
 *                    nextsame
 *                        |
 *                        V
 *                      NULL
 */

typedef struct HTINPUT_tag              /* a <FORM> element (<INPUT> etc.) */
{
  HTELEMENT             element;        /* must be first */
  struct HTINPUT_tag    *next;          /* next element in <FORM> */
  struct HTINPUT_tag    *nextdiff;      /* next different-name element */
  struct HTINPUT_tag    *prevdiff;      /* previous different-name element */
  struct HTINPUT_tag    *nextsame;      /* next same-name element */
  struct HTFORM_tag     *form;          /* our parent <FORM> */
  char                  *name;          /* NAME attribute */
  HTINTYPE              type;
  char                  *value;         /* js-mod; NULL for <SELECT> */
  char                  *defaultValue;  /* js-mod; pass/text/textarea only */
  HTOPTION              *options;       /* <SELECT> only */
  size_t                noptions;       /* <SELECT> only */
  char                  checked;        /* js-mod; checkbox/radio only */
  char                  defaultChecked; /* js-mod; checkbox/radio only */
  char                  disabled;       /* js-mod */
}
HTINPUT;
#define HTINPUTPN       ((HTINPUT *)NULL)

typedef struct HTFORM_tag               /* a <FORM> */
{
  HTELEMENT             element;        /* must be first */
  struct HTFORM_tag     *next;          /* next in list */
  HTPAGE                *pg;            /* our parent page */
  char                  *action;        /* js-mod */
  HTINPUT               *inputs;        /* <INPUT>/<SELECT> etc. elements */
  size_t                ninputs;        /* total number of elements */
  char                  *enctype;       /* js-mod: as given */
  CONST char            *encoding;      /* js-mod: canonical */
  HTMETH                method;         /* js-mod */
  char                  *name;
  char                  *target;        /* js-mod */
  char                  *mimeBoundary;
}
HTFORM;
#define HTFORMPN        ((HTFORM *)NULL)

typedef struct HTLOCATION_tag           /* a Location object */
{
  struct HTWINDOW_tag   *window;        /* our parent window */
  URL                   *url;           /* js-mod? our URL */
  void                  *jsobj;         /* associated JavaScript object */
}
HTLOCATION;
#define HTLOCATIONPN    ((HTLOCATION *)NULL)

typedef struct HTLINK_tag               /* a Link object */
{
  struct HTLINK_tag     *next;          /* next in list */
  HTPAGE                *pg;            /* our parent page */
  URL                   *url;           /* js-mod our URL */
  void                  *jsobj;         /* internal JavaScript object */
  char                  *target;        /* js-mod; may be NULL */
  char                  *text;          /* js-mod; may be temp. NULL */
  size_t                fmtoff;         /* `text' offset from start of doc */
}
HTLINK;
#define HTLINKPN    ((HTLINK *)NULL)

typedef struct HTIMAGE_tag              /* an Image object */
{
  HTELEMENT             element;        /* must be first */
  struct HTIMAGE_tag    *next;          /* next in list */
  HTPAGE                *pg;            /* our parent page */
  int                   border;
  int                   width;
  int                   height;
  int                   hspace;
  int                   vspace;
  char                  *lowsrc;        /* as-is URL (or empty) */
  char                  *src;           /* FQ URL */
  char                  *name;
}
HTIMAGE;
#define HTIMAGEPN       ((HTIMAGE *)NULL)

typedef enum HTBAR_tag
{
  HTBAR_LOCATION,
  HTBAR_MENU,
  HTBAR_PERSONAL,
  HTBAR_SCROLL,
  HTBAR_STATUS,
  HTBAR_TOOL,
  HTBAR_NUM                             /* must be last */
}
HTBAR;
#define HTBARPN         ((HTBAR *)NULL)

typedef enum HTTIMERF_tag               /* HTTIMER flags */
{
  HTTIMERF_ALL          = -1,
  HTTIMERF_REPEATING    = (1 << 0),     /* repeating timer (setInterval) */
  HTTIMERF_INUSE        = (1 << 1),     /* timer being executed */
  HTTIMERF_ZOMBIE       = (1 << 2)      /* timer cleared while running */
}
HTTIMERF;
#define HTTIMERFPN      ((HTTIMERF *)NULL)

typedef struct HTTIMER_tag              /* interval/timeout for JavaScript */
{
  struct HTTIMER_tag    *prev, *next;   /* previous/next timer this window */
  struct HTWINDOW_tag   *window;        /* parent window */
  double                when;           /* time_t to fire this timer */
  int                   intervalmsec;   /* interval in msec */
  int                   id;             /* user-visible identifier */
  HTTIMERF              flags;
  char                  *file;          /* URL/file containing `code' */
  int                   linenum;        /* line number of `file' */
  char                  *code;          /* JavaScript code to execute */
}
HTTIMER;
#define HTTIMERPN       ((HTTIMER *)NULL)

typedef struct HTWINDOW_tag             /* a Window object (for JavaScript) */
{
  HTPAGE                *pg;            /* our page */
  void                  *jsobj;         /* internal JavaScript JSObject * */
  int                   innerWidth;     /* js-mod; window size less chrome */
  int                   innerHeight;    /* js-mod */
  int                   screenX;        /* js-mod; screen location */
  int                   screenY;        /* js-mod */
  int                   pageXOffset;    /* js-mod; scrolled offset of view */
  int                   pageYOffset;    /* js-mod */
  char                  *status;        /* js-mod (may be NULL) */
  char                  *defaultStatus; /* js-mod (may be NULL) */
  char                  *name;          /* js-mod (may be NULL) */
  void                  *opener;        /* js-mod; JavaScript object */
  HTLOCATION            *location;      /* js-mod */
  void                  *historyjsobj;  /* js-mod? */
  void                  *screenjsobj;   /* js-mod? */
  size_t                nframes;
  HTTIMER               *timers;        /* js-mod; setInterval/setTimeout */
  char                  bar[HTBAR_NUM]; /* js-mod */
  char                  offscreenBuffering;     /* js-mod; auto/false/true */
}
HTWINDOW;
#define HTWINDOWPN      ((HTWINDOW *)NULL)

/* I(objtype) */
#define HTDOMOBJTYPE_LIST       \
I(options)                      \
I(input)                        \
I(inputs)                       \
I(elements)                     \
I(form)                         \
I(forms)                        \
I(image)                        \
I(images)                       \
I(document)                     \
I(page)

typedef enum HTDOMOBJTYPE_tag
{
#undef I
#define I(objtype)      HTDOMOBJTYPE_##objtype,
  HTDOMOBJTYPE_LIST
#undef I
  HTDOMOBJTYPE_NUM                              /* must be last */
}
HTDOMOBJTYPE;
#define HTDOMOBJTYPEPN  ((HTDOMOBJTYPE *)NULL)

typedef enum UTF_tag
{
  UTF_ONECHAR       = (1 << 0), /* only decode 1 character */
  UTF_ESCRANGE      = (1 << 1), /* HTML-escape out-of-range characters */
  UTF_ESCLOW        = (1 << 2), /* HTML-escape applicable 7-bit chars */
  UTF_BUFSTOP       = (1 << 3), /* stop decoding at dest buf limit */
  UTF_FINAL         = (1 << 4), /* this is the final buffer (input EOF) */
  UTF_BINARY        = (1 << 5), /* (quoted-printable) don't delimit lines */
  UTF_BIGENDIAN     = (1 << 6), /* (UTF-16) assume source is big-endian */
  UTF_LITTLEENDIAN  = (1 << 7), /* (UTF-16) assume source is little-endian */
  UTF_SAVEBOM       = (1 << 8), /* (UTF-16/quoted-printable) save BOM/sp->_ */
  UTF_START         = (1 << 9), /* this is the first buffer */
  UTF_CRNL          = (1 <<10), /* map output newlines to CR */
  UTF_LFNL          = (1 <<11), /* map output newlines to LF */
  UTF_DO8BIT        = (1 <<12), /* (ISO-8859-1 -> ISO-8859-1) 8-bit esc rep */
  UTF_HTMLDECODE    = (1 <<13), /* HTML-decode HTML sequences */
  UTF_BADCHARMSG    = (1 <<14), /* Report bad/out-of-range characters */
  UTF_XMLSAFE       = (1 <<15), /* Output XML-safe chars only */
  UTF_BADENCASISO   = (1 <<16), /* Interpret bad UTF-8 seq. as ISO-8859-1 */
  /* Error for UTF_BADENCASISO has its own flag, because it could occur
   * often if given data that is *known* to be mixed UTF-8/ISO-8859-1,
   * so may want to turn off those messages but still yap for other errs.
   * Message only issued if UTF_BADCHARMSG set too, so turning off that
   * still turns off all messages (including UTF_BADENCASISO converts):
   */
  UTF_BADENCASISOERR= (1 <<17), /* Error/msg if "" */
  UTF_QENCONLY      = (1 <<18), /* (encoded-word format) Use Q enc only */
  UTF_NOCHARTRANSLATE = (1 << 19), /* do not do charset translation */
}
UTF;
#define UTFPN   ((UTF *)NULL)

typedef size_t (HTCHARSETFUNC) ARGS((char *d, size_t dlen, size_t *dtot,
             CONST char **sp, size_t slen, UTF flags, UTF *state, int width,
             HTPFOBJ *htobj, TXPMBUF *pmbuf));
#define HTCHARSETFUNCPN ((HTCHARSETFUNC *)NULL)

#define HtUnicodeMap_UNKNOWN            ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_UNKNOWN          ((CONST byte *)NULL)
#define NUM_UNKNOWN_CODES               0
#define HtUnicodeMap_US_ASCII           ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_US_ASCII         ((CONST byte *)NULL)
#define NUM_US_ASCII_CODES              0
#define HtUnicodeMap_ISO_8859_1         ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_ISO_8859_1       ((CONST byte *)NULL)
#define NUM_ISO_8859_1_CODES            0
#define HtUnicodeMap_UTF_8              ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_UTF_8            ((CONST byte *)NULL)
#define NUM_UTF_8_CODES                 0
#define HtUnicodeMap_UTF_16             ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_UTF_16           ((CONST byte *)NULL)
#define NUM_UTF_16_CODES                0
#define HtUnicodeMap_UTF_16BE           ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_UTF_16BE         ((CONST byte *)NULL)
#define NUM_UTF_16BE_CODES              0
#define HtUnicodeMap_UTF_16LE           ((CONST EPI_UINT16 *)NULL)
#define HtUnicodeIndex_UTF_16LE         ((CONST byte *)NULL)
#define NUM_UTF_16LE_CODES              0

/* Values are token, canonical-string-name, to-utf8-func,
 * to-utf8-flags, from-utf8-func, from-utf8-flags.
 * Both functions must support UTF_HTMLDECODE.  See charsets.c for aliases.
 * HTCHARSET_UNKNOWN must be first.
 * NOTE: Charsets must be in ascending sorted order, by string name,
 * ignoring (but retaining) case, `-', `_', `:', '.'.  See htstr2charset():
 */
#define HTCHARSET_LIST                                                  \
I(UNKNOWN,      "Unknown", HTCHARSETFUNCPN,   (UTF)0,           \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_1,   "ISO-8859-1", htiso88591_to_utf8, (UTF)0,       \
  htutf8_to_iso88591,    (UTF)0)                                        \
I(ISO_8859_10,  "ISO-8859-10",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_11,  "ISO-8859-11",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_13,  "ISO-8859-13",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_14,  "ISO-8859-14",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_15,  "ISO-8859-15",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_16,  "ISO-8859-16",  HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_2,   "ISO-8859-2", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_3,   "ISO-8859-3", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_4,   "ISO-8859-4", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_5,   "ISO-8859-5", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_6,   "ISO-8859-6", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_7,   "ISO-8859-7", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_8,   "ISO-8859-8", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(ISO_8859_9,   "ISO-8859-9", HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(MACINTOSH,    "MACINTOSH",  HTCHARSETFUNCPN, (UTF)0,          \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(US_ASCII,     "US-ASCII",  htusascii_to_utf8, (UTF)0,         \
  htutf8_to_usascii,    (UTF)0)                                         \
I(UTF_16,       "UTF-16", htutf16_to_utf8,   (UTF)0,            \
  htutf8_to_utf16,      (UTF)0)                                         \
I(UTF_16BE,     "UTF-16BE", htutf16_to_utf8,   UTF_BIGENDIAN,   \
  htutf8_to_utf16,      UTF_BIGENDIAN)                                  \
I(UTF_16LE,     "UTF-16LE", htutf16_to_utf8,   UTF_LITTLEENDIAN,\
  htutf8_to_utf16,      UTF_LITTLEENDIAN)                               \
I(UTF_8,        "UTF-8", htutf8_to_utf8,    (UTF)0,             \
  htutf8_to_utf8,       (UTF)0)                                         \
I(WINDOWS_1250, "WINDOWS-1250", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1251, "WINDOWS-1251", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1252, "WINDOWS-1252", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1253, "WINDOWS-1253", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1254, "WINDOWS-1254", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1255, "WINDOWS-1255", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1256, "WINDOWS-1256", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1257, "WINDOWS-1257", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_1258, "WINDOWS-1258", HTCHARSETFUNCPN, (UTF)0,        \
  HTCHARSETFUNCPN,      (UTF)0)                                         \
I(WINDOWS_874,  "WINDOWS-874", HTCHARSETFUNCPN, (UTF)0,         \
  HTCHARSETFUNCPN,      (UTF)0)

typedef enum HTCHARSET_tag              /* known character sets/encodings */
{
#undef I
#define I(tok, cn, tf, tl, ff, fl)  HTCHARSET_##tok,
  HTCHARSET_LIST
#undef I
  HTCHARSET_NUM                         /* must be last */
}
HTCHARSET;
#define HTCHARSETPN     ((HTCHARSET *)NULL)

/* Default charsettext: */
#ifdef HT_UNICODE
#  define HTCHARSET_TEXT_DEFAULT        HTCHARSET_UTF_8
#else /* !HT_UNICODE */
#  define HTCHARSET_TEXT_DEFAULT        HTCHARSET_ISO_8859_1
#endif /* !HT_UNICODE */

typedef struct HTCSINFO_tag                     /* charset info */
{
  HTCHARSET     tok;                            /* if != HTCHARSET_UNKNOWN */
  char          *buf;                           /* if != CHARPN */
}
HTCSINFO;
#define HTCSINFOPN      ((HTCSINFO *)NULL)

#ifndef HTJDPN
typedef struct HTJD_tag HTJD;
#  define HTJDPN        ((HTJD *)NULL)
#endif

/* Content-Type source */
#define HTCTS_SYMBOLS_LIST      \
I(generated)                    \
I(header)                       \
I(doctype)                      \
I(metaheader)                   \
I(urlpath)                      \
I(contentscan)

typedef enum HTCTS_tag
{
  HTCTS_UNKNOWN = -1,                   /* unknown */
#undef I
#define I(tok)  HTCTS_##tok,
  HTCTS_SYMBOLS_LIST
#undef I
  HTCTS_NUM                             /* must be last */
}
HTCTS;
#define HTCTSPN ((HTCTS *)NULL)

/* pg->textFormatter; `unknown' must be first (0): */
#define TXfetchTextFormatter_SYMBOLS_LIST       \
  I(unknown)                                    \
  I(rawdoc)                                     \
  I(text)                                       \
  I(gopher)                                     \
  I(html)                                       \
  I(rss)                                        \
  I(frame)

typedef enum TXfetchTextFormatter__tag
{
#undef I
#define I(tok)  TXfetchTextFormatter_##tok,
  TXfetchTextFormatter_SYMBOLS_LIST
#undef I
  TXfetchTextFormatter_NUM                               /* must be last */
}
TXfetchTextFormatter;
#define TXfetchTextFormatterPN  ((TXfetchTextFormatter *)NULL)

CONST char *TXfetchTextFormatterToStr ARGS((TXfetchTextFormatter tf));

/* avoid include of htmll.h: */
#ifndef HTLPN
typedef struct HTL_tag  HTL;
#  define HTLPN ((HTL *)NULL)
#endif /* !HTLPN */

/* I(token, string, hdrval) where `token' is HTSSLPROT_... enum and
 * SSL_OP_NO_... token, `string' is string value and `hdrval' is
 * Upgrade: header token.  Listed in increasing priority.
 * Note: used as bitfields, do not exceed 8*sizeof(int).
 * WTF what are the proper header tokens for SSLv2 and SSLv3?
 * Note: TLSv1.1, TLSv1.2 only supported in OpenSSL 1.0.1+,
 * but OPENSSL_VERSION_NUMBER may not be defined here for us to
 * check, yet we may need a fully defined HTSSLPROT enum.  So always
 * include TLSv1.1, TLSv1.2; breaks OpenSSL 0.9.7 but that is dead to us:
 * Similar for TLSv1.3; only in OpenSSL 1.1.1+:
 */
#define HTSSLPROT_SYMBOLS_LIST          \
  I(SSLv2, "SSLv2", "SSL/2.0")          \
  I(SSLv3, "SSLv3", "SSL/3.0")          \
  I(TLSv1, "TLSv1", "TLS/1.0")          \
  I(TLSv1_1, "TLSv1.1", "TLS/1.1")      \
  I(TLSv1_2, "TLSv1.2", "TLS/1.2")      \
  I(TLSv1_3, "TLSv1.3", "TLS/1.3")

typedef enum HTSSLPROT_tag      /* SSL protocols */
{
  HTSSLPROT_UNKNOWN = -1,       /* must be first */
#undef I
#define I(tok, string, hdrval)  HTSSLPROT_##tok,
HTSSLPROT_SYMBOLS_LIST
#undef I
  HTSSLPROT_NUM                 /* must be last */
}
HTSSLPROT;
#define HTSSLPROTPN     ((HTSSLPROT *)NULL)

/* Turn off SSLv2; known vulnerable.
 * Turn off SSLv3 due to CVE-2014-3566 vulnerability:
 */
#define TX_DEF_SSLPROTOCOL_MASK (((1 << HTSSLPROT_NUM) - 1) &   \
        ~((1 << HTSSLPROT_SSLv2) | (1 << HTSSLPROT_SSLv3)))


/* I(token, string)
 * Note that string names correspond to Apache SSLCipherSuite setting's
 * `protocol' (first, optional) argument:
 */
#define TX_SSL_CIPHER_GROUP_SYMBOLS_LIST        \
  I(SSL,        "SSL")                          \
  I(TLSv1_3,    "TLSv1.3")

typedef enum TX_SSL_CIPHER_GROUP_tag
  {
    TX_SSL_CIPHER_GROUP_UNKNOWN = -1,           /* must be -1 and first */
#undef I
#define I(token, string)        TX_SSL_CIPHER_GROUP_##token,
    TX_SSL_CIPHER_GROUP_SYMBOLS_LIST
#undef I
    TX_SSL_CIPHER_GROUP_NUM,
    TX_SSL_CIPHER_GROUP_DEFAULT = TX_SSL_CIPHER_GROUP_SSL
  }
  TX_SSL_CIPHER_GROUP;
TX_SSL_CIPHER_GROUP TXsslStringToCipherGroup(const char *s, const char *e);
const char *TXsslCipherGroupToString(TX_SSL_CIPHER_GROUP group);

/* - - - - - - - - - - - - - - - Connections - - - - - - - - - - - - - - - */

int    htsetmaxconnrequests ARGS((HTOBJ *obj, size_t n));
size_t htgetmaxconnrequests ARGS((HTOBJ *obj));
int    htgetnextconnserial ARGS((HTOBJ *obj));
int    htsetmaxidleconn ARGS((HTOBJ *obj, size_t n));
size_t htgetmaxidleconn ARGS((HTOBJ *obj));
int    htsetmaxconnidletime ARGS((HTOBJ *obj, double sec));
double htgetmaxconnidletime ARGS((HTOBJ *obj));
int    htsetmaxconnlifetime ARGS((HTOBJ *obj, double sec));
double htgetmaxconnlifetime ARGS((HTOBJ *obj));
void  *htgetnextidleconn ARGS((HTOBJ *obj, void *conn, HTPROT *prot,
                               CONST char **host, unsigned *port));
double htexpireconncache ARGS((HTOBJ *obj, int all, int serialAndNewer));

/* - - - - - - - - - - - - - - - Authorization - - - - - - - - - - - - - - */

/* These must be in order of increasing trustworthiness.
 *   I(name, WWW-Authenticate-token, default-on, okhttp, supportsExtCred)
 * name: internal HTAUTH_... token name and public user string name
 * okhttp: 1 if ok to use for HTTP/HTTPs
 * supportsExtCred: 1 if support for external (forwarded/kinit) credentials
 * NOTE: UNKNOWN must be first, then anonymous == 0.
 * NOTE: HTAUTH_anonymous != HTAUTH_FTP w/user "anonymous"
 * See auth.c for required functions.  See also htauth_cachegetpspace() usage.
 * NTLMv1 and NTLMv2 are listed separately (even though the distinction is
 * below this level) so that the user can enable/disable them separately.
 * NTLMv1 is disabled by default due to being less-secure.
 */
#ifdef EPI_AUTH_DIGEST
#  define DIGEST_SYM    I(Digest,       "Digest",       1,      1, 0)
#else /* !EPI_AUTH_DIGEST */
#  define DIGEST_SYM
#endif /* !EPI_AUTH_DIGEST */
#ifdef EPI_AUTH_NEGOTIATE
/* WTF we use SSPI under Windows, but it is broken currently, so do not
 * define TX_AUTH_NEGOTIATE_SUPPORTED 1 under Windows yet, so that
 * Negotiate Auth is off (and not enable-able):
 */
#  if defined(EPI_BUILD_SASL)
#    define TX_AUTH_NEGOTIATE_SUPPORTED 1
#  else
#    define TX_AUTH_NEGOTIATE_SUPPORTED 0
#  endif
#  define EPI_NEGOTIATE_SYMBOLS_LIST    \
  I(Negotiate, "Negotiate", TX_AUTH_NEGOTIATE_SUPPORTED, 1, 1)
#else /* !EPI_AUTH_NEGOTIATE */
#  define EPI_NEGOTIATE_SYMBOLS_LIST
#endif /* !EPI_AUTH_NEGOTIATE */
/* WTF we list Negotiate before NTLM -- even though we consider
 * Negotiate more secure -- so that when NTLM and Negotiate are both
 * offered (IIS), NTLM is chosen.  If we chose Negotiate instead, then
 * SASL would choose GSSAPI, we would need Kerberos creds, which would
 * likely fail to be obtained, or would be rejected by what is likely
 * an NTLM peer.
 */
#define HTAUTHSYMBOLS_LIST                              \
I(anonymous,    "",             1,      1,      0)      \
I(FTP,          "FTP",          1,      0,      0)      \
I(Basic,        "Basic",        1,      1,      0)      \
DIGEST_SYM                                              \
I(file,         "file",         1,      0,      0)      \
EPI_NEGOTIATE_SYMBOLS_LIST                              \
I(NTLMv1,       "NTLM",         0,      1,      0)      \
I(NTLMv2,       "NTLM",         1,      1,      0)

typedef enum HTAUTH_tag         /* Authentication types */
{
  HTAUTH_UNKNOWN        = -1,   /* must be first: rest start at 0 */
#undef I
#define I(name, token, def, okhttp, supportsExtCred)    HTAUTH_##name,
HTAUTHSYMBOLS_LIST
#undef I
  HTAUTH_NUM                    /* must be last */
}
HTAUTH;
#define HTAUTHPN        ((HTAUTH *)NULL)

HTAUTH  htstr2auth ARGS((CONST char *s, CONST char *e, int internal));
CONST char *htauth2str ARGS((HTAUTH prot, int internal));
int   htsetmaxprotspacecachesz ARGS((HTOBJ *obj, size_t sz));
size_t htgetmaxprotspacecachesz ARGS((HTOBJ *obj));
int   htsetmaxprotspacelifetime ARGS((HTOBJ *obj, double sec));
double htgetmaxprotspacelifetime ARGS((HTOBJ *obj));
int   htsetmaxprotspaceidletime(HTOBJ *obj, double sec);
double htgetmaxprotspaceidletime(HTOBJ *obj);
int   htsettraceauth ARGS((HTOBJ *obj, int n));
int   htgettraceauth ARGS((HTOBJ *obj));
TXtraceHtmlParse TXfetchGetTraceHtmlParse(HTOBJ *obj);
TXbool TXfetchSetTraceHtmlParse(HTOBJ *obj, TXtraceHtmlParse traceHtmlParse);
int   htsetauthschemes ARGS((HTOBJ *obj, HTAUTH *schemes));
HTAUTH *htgetauthschemes ARGS((HTOBJ *obj));
int   htsetusername ARGS((HTOBJ *obj, char *user));
int   htsetpassword ARGS((HTOBJ *obj, char *pass));

#define TX_TRACEAUTH_VARS double traceAuthStart = -1.0, traceAuthTime = -1.0,\
    traceAuthFinish = -1.0
#define TX_TRACEAUTH_BEFORE(authObj, flags)                             \
  if ((authObj) && ((authObj)->traceAuth & (flags)))                    \
    {                                                                   \
      traceAuthStart = TXgetTimeContinuousFixedRateOrOfDay();           \
    }
#define TX_TRACEAUTH_AFTER_BEGIN(authObj, flags)                          \
  if ((authObj) && ((authObj)->traceAuth & (flags)))                      \
    {                                                                     \
      TX_PUSHERROR();                                                     \
      /* Make sure time diff is non-negative, i.e. in case start/end */   \
      /* times are identical but floating-point diff is slightly neg.: */ \
      if (traceAuthStart >= (double)0.0)                                  \
        {                                                                 \
          traceAuthFinish = TXgetTimeContinuousFixedRateOrOfDay();        \
          traceAuthTime = traceAuthFinish - traceAuthStart;               \
          if (traceAuthTime < 0.0 && traceAuthTime > -0.001)              \
            traceAuthTime = 0.0;                                          \
        } {
#define TX_TRACEAUTH_AFTER_END()   } TX_POPERROR(); }

/* ----------------------------- encodings.c: ----------------------------- */

/*   Content-Encoding and Transfer-Encoding tokens.  `chunked' is valid
 * for Transfer-Encoding only, and is handled with special-case code;
 * the rest we assume (NOTE) are valid for *both* Content-Encoding and
 * Transfer-Encoding, and are handled identically for both.
 * `x-' prefixes are automagically recognized/stripped by
 * TXfetchStrToEncodingEnum().
 *   Macro format: I(httpToken, fileExt, desc, bitFlags)
 *   Bit flags: 0x1: default on  0x2: is-identity
 * `httpToken' must also be safe for part of C function name
 * NOTE: list must be sorted ascending case-insensitve by extension,
 * for TXhtDefaultMimeEncodingsArray[].
 * Bug 5807: `7bit', `8bit', `binary' are really Content-Transfer-Encoding
 * values (for MIME, not HTTP), but Apache may set Content-Encoding: 7bit
 * for e.g. message/rfc822 files (.mht), so we accept them for HTTP.
 */
#define TX_CONTENT_OR_TRANSFER_ENCODING_SYMBOLS_LIST    \
  I(7bit,         "",     "No transformation",  0x3)    \
  I(8bit,         "",     "No transformation",  0x3)    \
  I(binary,       "",     "No transformation",  0x3)    \
  I(identity,     "",     "No transformation",  0x3)    \
  I(chunked,      "",     "Chunked",            0x1)    \
  I(deflate,      "",     "zlib deflate",       0x1)    \
  I(gzip,         "gz",   "GNU zip",            0x1)    \
  I(compress,     "Z",    "UNIX compress",      0x1)
typedef enum TXCTE_tag                          /*Content-/Transfer-Encoding*/
{
  TXCTE_UNKNOWN = -1,                           /* must be first */
#undef I
#define I(httpToken, fileExt, desc, flags)      TXCTE_##httpToken,
TX_CONTENT_OR_TRANSFER_ENCODING_SYMBOLS_LIST
#undef I
  TXCTE_NUM                                     /* must be last */
}
TXCTE;
#define TXCTEPN ((TXCTE *)NULL)

/* Filter object that manages a Content-/Transfer-Encoding decoder: */
typedef struct TXCTEFILTER_tag  TXCTEFILTER;
#define TXCTEFILTERPN   ((TXCTEFILTER *)NULL)

TXCTE   TXfetchStrToEncodingEnum ARGS((CONST char *s, CONST char *e));
CONST char *TXfetchEncodingEnumToStr ARGS((TXCTE cte));
int     TXfetchSetAllowedEncodings ARGS((HTOBJ *obj, CONST TXCTE *encodings));
TXCTE  *TXfetchGetAllowedEncodings ARGS((HTOBJ *obj));

/* internal access functions: */
TXCTE   TXfetchCteFilterGetEncoding ARGS((TXCTEFILTER *filter));
int     TXfetchCteFilterIsTransferEncoding ARGS((TXCTEFILTER *filter));

extern CONST char      TXfetchDefEncodingMask[TXCTE_NUM];

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* I(X509_V_tok)
 * Most of these are X509_V_ERR_... values:
 */
#define TX_SSL_VERIFY_ERR_SYMBOLS_LIST          \
I(X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT)         \
I(X509_V_ERR_UNABLE_TO_GET_CRL)                 \
I(X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE)  \
I(X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE)   \
I(X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY)\
I(X509_V_ERR_CERT_SIGNATURE_FAILURE)            \
I(X509_V_ERR_CRL_SIGNATURE_FAILURE)             \
I(X509_V_ERR_CERT_NOT_YET_VALID)                \
I(X509_V_ERR_CERT_HAS_EXPIRED)                  \
I(X509_V_ERR_CRL_NOT_YET_VALID)                 \
I(X509_V_ERR_CRL_HAS_EXPIRED)                   \
I(X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD)    \
I(X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD)     \
I(X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD)    \
I(X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD)    \
I(X509_V_ERR_OUT_OF_MEM)                        \
I(X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)       \
I(X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)         \
I(X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) \
I(X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE)   \
I(X509_V_ERR_CERT_CHAIN_TOO_LONG)               \
I(X509_V_ERR_CERT_REVOKED)                      \
I(X509_V_ERR_INVALID_CA)                        \
I(X509_V_ERR_PATH_LENGTH_EXCEEDED)              \
I(X509_V_ERR_INVALID_PURPOSE)                   \
I(X509_V_ERR_CERT_UNTRUSTED)                    \
I(X509_V_ERR_CERT_REJECTED)                     \
I(X509_V_ERR_SUBJECT_ISSUER_MISMATCH)           \
I(X509_V_ERR_AKID_SKID_MISMATCH)                \
I(X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH)       \
I(X509_V_ERR_KEYUSAGE_NO_CERTSIGN)              \
I(X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER)          \
I(X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION)      \
I(X509_V_ERR_KEYUSAGE_NO_CRL_SIGN)              \
I(X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION)  \
I(X509_V_ERR_INVALID_NON_CA)                    \
I(X509_V_ERR_PROXY_PATH_LENGTH_EXCEEDED)        \
I(X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE)     \
I(X509_V_ERR_PROXY_CERTIFICATES_NOT_ALLOWED)    \
I(X509_V_ERR_INVALID_EXTENSION)                 \
I(X509_V_ERR_INVALID_POLICY_EXTENSION)          \
I(X509_V_ERR_NO_EXPLICIT_POLICY)                /* last OpenSSL err */  \
I(No_Peer_Certificate)                          \
I(CommonName_Hostname_Mismatch)                 /* client setting only */ \
I(Other_Error)                                  /* other X509 error */

/* Last/highest TXsslVerifyErr value that is an OpenSSL X509 error: */
#define TXsslVerifyErr_Last_Openssl_Err \
  TXsslVerifyErr_X509_V_ERR_NO_EXPLICIT_POLICY

typedef enum TXsslVerifyErr_tag
{
  TXsslVerifyErr_Unknown = -1,                  /* must be first, and -1 */
  TXsslVerifyErr_Ok     = 0,                    /* must be index 0 */
#undef I
#define I(tok)  TXsslVerifyErr_##tok,
  TX_SSL_VERIFY_ERR_SYMBOLS_LIST
#undef I
  TXsslVerifyErr_Num                            /* must be last */
}
TXsslVerifyErr;
#define TXsslVerifyErrPN        ((TXsslVerifyErr *)NULL)

#define TXsslVerifyErrMaskByteSz        \
  (TX_BIT_ARRAY_BYTE_SZ(TXsslVerifyErr_Num))

#define TX_SSL_VERIFY_ERR_MASK_SET_DEFAULT(mask)        \
  memset((mask), 0, TXsslVerifyErrMaskByteSz)

#define TX_SSL_VERIFY_DEPTH_DEFAULT     1

/* ------------------------------ refInfo: -------------------------------- */

TXrefInfo *TXrefInfoOpen(TXPMBUF *pmbuf, TXrefTypeFlag types, const char *url);
TXrefInfo *TXrefInfoDup(TXrefInfo *refInfo, int readOnly);
TXrefInfo *TXrefInfoClose(TXrefInfo *refInfo);

TXrefTypeFlag  TXrefInfoGetTypes(TXrefInfo *refInfo);
TXrefFlag      TXrefInfoGetFlags(TXrefInfo *refInfo);
TXrefFlag      TXrefInfoSetFlags(TXrefInfo *refInfo, TXrefFlag flags, int set);
#ifdef HTMLL_H
HTAG    TXrefInfoGetTag(TXrefInfo *refInfo);
int     TXrefInfoSetTag(TXrefInfo *refInfo, HTAG tag);
HATTR   TXrefInfoGetSourceAttribute(TXrefInfo *refInfo);
int     TXrefInfoSetSourceAttribute(TXrefInfo *refInfo, HATTR attr);
#endif /* HTMLL_H */
char   *TXrefInfoGetAttribute(TXrefInfo *refInfo, const char *attr);
int     TXrefInfoSetAttribute(TXrefInfo *refInfo, const char *attr,
                              const char *value);
char  **TXrefInfoGetAttributes(TXrefInfo *refInfo);

#undef I
#define I(fld)                                                          \
  const char *TXrefInfoGet##fld(TXrefInfo *refInfo);                    \
  int TXrefInfoSet##fld(TXrefInfo *refInfo, const char *fld, size_t fldLen);
TX_REFINFO_STRING_SYMBOLS
#undef I

#undef I
#define I(fld)                                                          \
  EPI_SSIZE_T TXrefInfoGet##fld(TXrefInfo *rerInfo);                    \
  int         TXrefInfoSet##fld(TXrefInfo *refInfo, EPI_SSIZE_T fld);
TX_REFINFO_SSIZE_T_SYMBOLS
#undef I

/* - - - - - - - - - - - - - - - - - Utils: - - - - - - - - - - - - - - - - */

TXrefInfo *TXrefInfoListAdd(TXPMBUF *pmbuf, TXrefInfo ***refInfos,
                            size_t *numRefInfos, size_t *numAllocedRefInfos,
                            TXrefTypeFlag types, const char *url);
void TXrefInfoListRemoveDisallowed(HTOBJ *obj, TXrefInfo **refInfos,
                                   size_t *numRefInfos, TXrefTypeFlag types);
int  TXrefInfoListTake(TXPMBUF *pmbuf, TXrefInfo ***refInfosDest,
                      size_t *numRefInfosDest, size_t *numAllocedRefInfosDest,
                       TXrefInfo **refInfosSrc, size_t *numRefInfosSrc,
                       TXrefTypeFlag types, int alwaysDel);
size_t TXrefInfoListCount(TXrefInfo **refInfos, TXrefTypeFlag types);
char **TXrefInfoListGetUrls(TXPMBUF *pmbuf, TXrefInfo **refInfos,
                            TXrefTypeFlag types);

/* ------------------------------------------------------------------------ */

struct HTPAGE_tag
{
  HTELEMENT     element;                /* must be first; for Body */
  HTJD          *jd;                    /* (internal) JavaScript/DOM state */
  int           javascriptLibDepth;     /* (internal) in JavaScript lib func*/
  const char    *javascriptLibFunc;     /* (internal) */
  char          **intermediateUrls;     /* (opt.) redirects from */
  char		*url;			/* (clean) URL for this page */
  int           majver, minver;         /* major/minor HTTP version number */
  int		respcode;		/* HTTP response code (0=not set) */
  char		*respmsg;		/*  ""     ""    message (if !NULL) */
  int		redirs;			/* number of redirects to reach page */
  int           optUpRedirs;            /* # of OPTIONS-Upgrade `redirs' */
  TXproxy       *lastProxyUsed;
  char          *src;                   /* source doc, maybe with headers */
  size_t	srcsz;			/* "" size */
  char		*rawdoc;		/* document start (in src) */
  size_t	rawdocsz;
  CGISL		*hdrs;			/* headers */
  char          *downloadDoc;           /* downloaded document (may==rawdoc)*/
  size_t        downloadDocSz;
  char		*contype;		/* content type, from header or guess*/
  CGISL         *contypeparams;         /*   ""     ""  params */
  HTCTS         contypesrc;             /*   ""     ""  source */
  double        dnstime, transfertime;  /* seconds to DNS & transfer page */
  HTAUTH        authUsed;               /* auth scheme used */
  CGISL         *authparamsuser;        /* modified auth. params from resp. */
  HTAUTH        authUsedHighest;        /* highest auth used (over redirs) */
  /* Encodings, in server-applied order.  NULL if no encodings:
   */
  char          **encodings;                    /* NULL-terminated */
  size_t        numEncodings;
  int           refcnt;                 /* number of pointers to this page */
  void          *sslServerCertificate;  /* X509 cert from server, if any */
  /* `sslClientCAList' is a TX_STACK_OF_X509_NAME * of the server's
   * list of acceptable signing CAs for the client cert:
   */
  void          *sslClientCAList;
  TXsslVerifyErr sslVerifyServerErr;
  /* internal HTOBJ cache fields: */
  HTPAGE        *cachenext;             /* next page in cache */
  HTPAGE        *cacheprev;             /* previous page in cache */
  unsigned      cachehash;              /* hash of URL */
  /* internal formatter use: */
  TXrefInfo     *curScriptRef;          /*running script's `javascript:' ref*/
  int		flags;			/* flags (see HTF below) */
   /* these are only set by htformatpage(): */
  char		*title, *txt;		/* formatted title and document text */
  size_t	titlesz, txtsz;		/* "" size  (`txt' may == `rawdoc') */
  TXrefInfo    **refInfos;              /* js-mod */
  size_t        numRefInfos, numAllocedRefInfos;      /* js-mod */
  CGISL         *metahdrs, *metadata;   /* <META HTTP-EQUIV, NAME> tags */
  int           bgclr, fgclr, linkclr;  /* js-mod; -1 == unknown */
  int           alinkclr, vlinkclr;     /* js-mod; -1 == unknown */
  HTANCHOR      *anchors;               /* <A NAME=...>...</A> anchors list */
  size_t        nanchors;               /* number of anchors in list */
  HTFORM        *forms;                 /* <FORM>s */
  size_t        nforms;                 /* number of forms in list */
  int           width, height;          /* js-mod; document.width/height */
  HTWINDOW      *window;                /* Window object for JavaScript */
  HTLINK        *doclinks;              /* <A HREF=...>...</A> links */
  size_t        ndoclinks;
  HTIMAGE       *docimages;             /* <IMG> images */
  size_t        ndocimages;
  HTELEMENT     **elements;             /* array */
  size_t        nelements, aelements;   /* number in array / alloced size */
  URL           *u;                     /* split `url' */
  int           nfetched;               /* # fetched frames/iframes/srcs */
  HTCHARSET     charsettext;            /* formatted text charset */
  char          *charsettextbuf;        /* "" if !NULL and charsettext=unk. */
  HTCHARSET     charsetdetected;        /* guess at src charset */
  HTCHARSET     charsetexplicit;        /* explicitly-set src charset */
  char          *charsetexplicitbuf;    /* "" if !NULL and charsetexp=unk. */
  HTCHARSET     charsetsrc;             /* assumed src charset */
  char          *charsetsrcbuf;         /* "" if set and unk. (may == exp.) */
  HTCHARSETFUNC *unhtmlfunc;            /* temp internal usage */
  UTF           unhtmlFlags;            /* "" */
  HTL           *lexer;                 /* lexer (saved for buffer list) */
  TXfetchTextFormatter textFormatter;   /* `txt' formatter used */
};

/* Optional parent-page info passed to htformatpage().  Hack until true
 * (multi-object) frame support is added:
 */
typedef struct HTFPINFO_tag
{
  TXrefInfo    **refInfos;              /* parent's references (w/frames) */
  size_t        numRefInfos;
}
HTFPINFO;
#define HTFPINFOPN      ((HTFPINFO *)NULL)
#define HTFPINFO_EMPTY_VAL      { NULL, 0 }


/* Flags set (mostly) by formatting.  These are not errors: */
#define HTF_SYMBOLS_LIST                                        \
I(REDIR)        /* page was redirected (see redirs field) */    \
I(FRAME)        /* page has a frame (set by htformatpage() */   \
I(FORM)         /*   "" <FORM> or <ISINDEX> tag "" */           \
I(TABLE)        /*   "" <TABLE>          ""        */           \
I(SCRIPT)       /*   "" <SCRIPT>         ""        */           \
I(JAVA)         /* page has Java stuff */                       \
I(FORMATTED)    /* already formatted */                         \
I(IFRAME)       /* page has an <IFRAME> */                      \
I(REQSECURE)    /* request was secure */                        \
I(RESPSECURE)   /* response was secure */                       \
I(ANCSECURE)    /* ancestors (redirs) exist and were secure */  \
I(DESCSECURE)   /* descendants (frames) exist and were secure */\
I(SavedContent) /* content was saved via TOFILE; rawdoc empty */

typedef enum HTFint_tag
{
#undef I
#define I(tok)  HTFint_##tok,
HTF_SYMBOLS_LIST
#undef I
}
HTFint;

typedef enum HTF_tag                            /* bit flags */
{
#undef I
#define I(tok)  HTF_##tok = (1 << HTFint_##tok),
HTF_SYMBOLS_LIST
#undef I
}
HTF;
#define HTFPN   ((HTF *)NULL)

/* internal use: */
#define HTF_FORMATFLAGS (HTF_FRAME | HTF_FORM | HTF_TABLE | HTF_SCRIPT | \
                HTF_JAVA | HTF_FORMATTED | HTF_IFRAME | HTF_DESCSECURE)
/* HTF_DESCSECURE is a format flag because it can currently only be set
 * by htgetframe()?
 */

#if TX_VERSION_MAJOR >= 6
#  define TX_HTTP_DEFAULT_MAJOR_VERSION 1
#  define TX_HTTP_DEFAULT_MINOR_VERSION 1
#else /* TX_VERSION_MAJOR < 6 */
#  define TX_HTTP_DEFAULT_MAJOR_VERSION 1
#  define TX_HTTP_DEFAULT_MINOR_VERSION 0
#endif /* TX_VERSION_MAJOR < 6 */
#define TX_HTTP_MAX_MAJOR_VERSION       1

/* Merge and un-merge major+minor to a single int; for internal compares: */
#define TX_HTTP_VERSION_TO_MAJOR(n)     ((n) >> 8)
#define TX_HTTP_VERSION_TO_MINOR(n)     ((n) & 0xff)
#define TX_HTTP_MAJOR_MINOR_TO_VERSION(major, minor)    \
  (((major) << 8) + ((minor) & 0xff))

#define TX_HTTP_VERSION_0_9     TX_HTTP_MAJOR_MINOR_TO_VERSION(0, 9)
#define TX_HTTP_VERSION_1_0     TX_HTTP_MAJOR_MINOR_TO_VERSION(1, 0)
#define TX_HTTP_VERSION_1_1     TX_HTTP_MAJOR_MINOR_TO_VERSION(1, 1)

/* HTTP response codes, from the spec: */
#define HTCODE_INFO		100	/* (all 1xx) */
#define HTCODE_CLASS_INFO       HTCODE_INFO
#define HTCODE_SWITCHING_PROTOCOLS 101  /* Switching Protocols (Upgrade hdr)*/
#define HTCODE_OK		200	/* Ok (all 2xx) */
#define HTCODE_CLASS_OK         HTCODE_OK
#define HTCODE_CREATED		201	/* Created */
#define HTCODE_ACCEPTED		202	/* Accepted */
#define HTCODE_NO_CONTENT	204	/* No Content */
#define HTCODE_MULTIPLE_CHOICES	300	/* Multiple Choices */
#define HTCODE_CLASS_REDIRECT   300
#define HTCODE_MOVED_PERM	301	/* Moved permanently */
#define HTCODE_MOVED_TEMP	302	/* Moved temporarily */
#define HTCODE_SEE_OTHER        303     /* See Other */
#define HTCODE_NOT_MODIFIED	304	/* Not modified */
#define HTCODE_BAD_REQUEST	400	/* Bad client request (all 4xx) */
#define HTCODE_CLASS_CLIENT_ERR HTCODE_BAD_REQUEST
#define HTCODE_UNAUTHORIZED	401	/* Unauthorized */
#define HTCODE_FORBIDDEN	403	/* Forbidden */
#define HTCODE_NOT_FOUND	404	/* Not found */
#define HTCODE_METHOD_NOT_ALLOWED 405   /* Method not allowed */
#define HTCODE_NOT_ACCEPTABLE   406     /* Not acceptable */
#define HTCODE_PROXY_UNAUTHORIZED 407   /* Proxy access unauthorized */
#define HTCODE_REQUEST_TIMEOUT  408     /* Request timed out */
#define HTCODE_REQUEST_ENTITY_TOO_LARGE 413 /* Request entity too large */
#define HTCODE_REQUEST_URI_TOO_LARGE 414 /* Request URI too large */
#define HTCODE_UPGRADE_REQUIRED 426     /* Upgrade Required */
#define HTCODE_SERVER_ERR	500	/* internal server error (all 5xx) */
#define HTCODE_CLASS_SERVER_ERR HTCODE_SERVER_ERR
#define HTCODE_NOT_IMPLEMENTED  501     /* Not implemented by server */
#define HTCODE_BAD_GATEWAY	502	/* Bad gateway */
#define HTCODE_SERVICE_UNAVAIL	503	/* Service unavailable */

#define TXfetchResponseCodeClass(code)  (((code)/100)*100)

#define FTPCODE_MAJOR_OKPRELIMINARY     100     /* all 1xx */
#define FTPCODE_OKWILLOPENDATACONN      150
#define FTPCODE_MAJOR_OK                200     /* all 2xx */
#define FTPCODE_MAJOR_OKINTERMEDIATE    300     /* all 3xx */
#define FTPCODE_USER_OK_NEED_PASSWORD   331
#define FTPCODE_CLOSEDATA               226
#define FTPCODE_MAJOR_ERROR_TRANSIENT   400     /* all 4xx */
#define FTPCODE_NOTLOGGED               530     /* not logged in */
#define FTPCODE_FILEUNAVAIL             550
#define FTPCODE_FILENOTALLOWED          553     /* sometimes "perm. denied" */
#define FTPCODE_MINOR_FILESYSERR        FTPCODE_FILEUNAVAIL   /* 55x class */

#define TX_FETCH_DEFAULT_MAX_FRAMES     5
#if TX_VERSION_MAJOR >= 8
#  define TX_FETCH_DEFAULT_MAX_REDIRS   20
#else /* TX_VERSION_MAJOR < 8 */
#  define TX_FETCH_DEFAULT_MAX_REDIRS   5
#endif /* TX_VERSION_MAJOR < 8 */

#define TX_FETCH_DEFAULT_TIMEOUT                30
#define TX_FETCH_DEFAULT_WRITE_BUFFER_SIZE      (32*1024)
#define TX_FETCH_DEFAULT_MAX_HEADER_SIZE        (128*1024)
#if TX_VERSION_MAJOR >= 8
#  define TX_FETCH_DEFAULT_MAX_PAGE_SIZE        (100*1024*1024)
#else /* TX_VERSION_MAJOR < 8 */
#  define TX_FETCH_DEFAULT_MAX_PAGE_SIZE        (512*1024)
#endif /* TX_VERSION_MAJOR < 8 */
/* Would like maxdownloadsize to default the same as maxpagesize, so
 * that network bandwidth is also limited just as memory bandwidth is.
 * But that would mean users need to increase two settings instead of
 * one when they want to increase download *and* page size (which is
 * typical), which would be annoying.  So make maxdownloadsize default
 * to unlimited: in reality, network bandwidth will still be limited
 * by maxpagesize, because we decode Content-Encodings on-the-fly, and
 * wire-transfer size is rarely (if ever) much larger than decoded
 * size: once maxpagesize is reached, we will probably not have
 * consumed more than maxpagesize + writeBufferSize in network
 * bandwidth, and probably less if compression was used:
 */
#define TX_FETCH_DEFAULT_MAX_DOWNLOAD_SIZE      (-1)

/* PAC fetch retry delay is low, because PAC failure halts all fetches: */
#define TX_FETCH_DEFAULT_PAC_FETCH_RETRY_DELAY  10
/* But proxy retry delay can be larger, because this delay is only
 * used if we have another proxy to use.  MSIE 5.5-10 uses 30 minutes?
 */
#define TX_FETCH_DEFAULT_PROXY_RETRY_DELAY      300

typedef int (HTIOFUNC) ARGS((void *data, char *buf, size_t sz));
#define HTIOFUNCPN      ((HTIOFUNC *)NULL)

typedef enum TXfetchType_tag
{
  TXfetchType_Normal,                           /* must be first/0 */
  TXfetchType_PacInitUser,                      /* user PAC init */
  TXfetchType_PacFetchInternal,                 /* internal TXproxyCache use*/
}
TXfetchType;

typedef struct HTFA_tag         /* fetch args */
{
  char          *sendcontype;   /* send Content-Type */
  HTBUF         *sendcontent;   /* send content */
  CGISL         *sendvars;      /*   or vars */
  char          *sendfile;      /*   or from file */
  int           senddesc;       /*   or from descriptor (not closed) */
  char          *savefile;      /* save content to file */
  int           savedesc;       /*   or to descriptor (not closed) */
  HTIOFUNC      *savefunc;      /*   or via func */
  void          *savefuncdata;
  CONST char    *renameto;      /* file path to rename to (HTMETH_RENAME) */
  TXfetchType   fetchType;
}
HTFA;
#define HTFAPN          ((HTFA *)NULL)

typedef enum HTOT_tag           /* HTOUR URL types */
{
  HTOT_REDIR,
  HTOT_FRAME,
  HTOT_IFRAME,
  HTOT_NUM                      /* must be last */
}
HTOT;
#define HTOTPN  ((HTOT *)NULL)

typedef struct HTOUR_tag        /* htokurlreq() struct */
{
  HTOT          type;           /* type of URL */
  char          *url;           /* URL in question */
  URL           *parts;         /* split URL */
  int           depth;          /* depth from original */
}
HTOUR;
#define HTOURPN ((HTOUR *)NULL)

typedef enum HTEMSEC_tag        /* embed security */
{
  HTEMSEC_OFF,                  /* don't care */
  HTEMSEC_NODECREASE,           /* don't load less-secure embedded objects */
  HTEMSEC_NOINCREASE,           /* don't load more-secure embedded objects */
  HTEMSEC_SAMEPROTOCOL,         /* don't load different-protocol objects */
  HTEMSEC_NUM                   /* must be last */
}
HTEMSEC;
#define HTEMSECPN       ((HTEMSEC *)NULL)

typedef enum HTSECURE_tag       /* secure transactions */
{
  HTSECURE_OFF,                 /* must be first; do not care about secure */
  HTSECURE_PREFERRED,           /* prefer secure but accept in-the-clear */
  HTSECURE_REQUIRED,            /* secure only */
  HTSECURE_NUM                  /* must be last */
}
HTSECURE;
#define HTSECUREPN ((HTSECURE *)NULL)

typedef enum HTFTYPE_tag        /* file types for filetype */
{
  HTFTYPE_FILE,                 /* regular file */
  HTFTYPE_DIR,                  /* directory */
  HTFTYPE_DEV,                  /* char or block device */
  HTFTYPE_SYMLINK,              /* symbolic link */
  HTFTYPE_OTHER,                /* pipe, socket, mpc, nwk */
  HTFTYPE_NUM                   /* must be last */
}
HTFTYPE;
#define HTFTYPEPN       ((HTFTYPE *)NULL)

typedef enum HTFNL_tag          /* filenonlocal types */
{
  HTFNL_OFF,                    /* disallow */
  HTFNL_UNC,                    /* allow straight through as UNC path */
  HTFNL_FTP,                    /* punt to FTP */
  HTFNL_NUM                     /* must be last */
}
HTFNL;
#define HTFNLPN         ((HTFNL *)NULL)

/* - - - - - - - - - - - - Refs for format/reparent - - - - - - - - - - - - */

/* Note that these are used twice apiece, for format and reparent: */
#define HTREFFSYMBOLS_LIST      \
I(link)                         \
I(image)                        \
I(frame)                        \
I(iframe)

typedef enum HTREFFN_tag                /* HTREFF as enum */
{
#undef I
#define I(sym)  HTREFFN_##sym,
  HTREFFSYMBOLS_LIST
#undef I
  HTREFFN_NUM                           /* must be last */
}
HTREFFN;
#define HTREFFNPN       ((HTREFFN *)NULL)

typedef enum HTREFF_tag                 /* HTREFF as flags */
{
#undef I
#define I(sym)  HTREFF_##sym##ForFormat = (1 << HTREFFN_##sym),
HTREFFSYMBOLS_LIST
#define HTREFF_FORMAT_FLAGS     ((1 << HTREFFN_NUM) - 1)
#define HTREFFN2FORMAT(n)       (1 << (n))
#undef I
#define I(sym)  HTREFF_##sym##ForReparent =(1 << (HTREFFN_NUM+HTREFFN_##sym)),
HTREFFSYMBOLS_LIST
#define HTREFF_REPARENT_FLAGS   (HTREFF_FORMAT_FLAGS << HTREFFN_NUM)
#define HTREFFN2REPARENT(n)     (1 << (HTREFFN_NUM + (n)))
#undef I
  HTREFF_NUM                            /* avoid trailing comma in enum */
}
HTREFF;
#define HTREFFPN        ((HTREFF *)NULL)

typedef struct HTREF_tag        /* a reference (image/link/frame) */
{
  int           tag;            /* HTAG value */
  int           attr;           /* HATTR value */
  HTREFF        flags;
  int           attr2;          /* optional HATTR value */
  char          *val2;          /* if non-NULL, these must match too */
}
HTREF;
#define HTREFPN         ((HTREF *)NULL)

extern CONST HTREF      HtDefaultHtrefs[];
extern CONST size_t     HtDefaultHtrefsNum;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Function type that does host to IP translation: */
typedef TXbool (HTHOST2IP)(HTOBJ *obj, const char *host, TXbool cacheOnly,
                           TXsockaddr *sockaddr);

HTOBJ *openhtobj ARGS((void));
HTOBJ *closehtobj ARGS((HTOBJ *obj));
HTREF *htfreehtrefs ARGS((HTREF *refs, size_t nrefs));
HTOBJ *duphtobj ARGS((HTOBJ *obj));                     /* KNG 980805 */
int    TXfetchSetHttpVersion ARGS((HTOBJ *obj, int majorVersion,
                                   int minorVersion));
int    TXfetchGetHttpVersion ARGS((HTOBJ *obj, int *majorVersion,
                                   int *minorVersion));
int    TXfetchSetWriteBufferSize ARGS((HTOBJ *obj, size_t sz));
int    TXfetchGetWriteBufferSize ARGS((HTOBJ *obj, size_t *sz));
int    TXfetchSetTraceEncoding ARGS((HTOBJ *obj, int traceEncoding));
int    TXfetchGetTraceEncoding ARGS((HTOBJ *obj, int *traceEncoding));
int    TXfetchSetTraceDns ARGS((HTOBJ *obj, TXtraceDns traceDns));
int    TXfetchGetTraceDns ARGS((HTOBJ *obj, TXtraceDns *traceDns));
int   htsetmaxhdrsize ARGS((HTOBJ *obj, size_t hdrsz));
size_t htgetmaxhdrsize ARGS((HTOBJ *obj));
int   htsetmaxpgsize ARGS((HTOBJ *obj, size_t pgsz));
size_t htgetmaxpgsize ARGS((HTOBJ *obj));
int    TXfetchSetMaxDownloadSize ARGS((HTOBJ *obj, size_t sz));
int    TXfetchGetMaxDownloadSize ARGS((HTOBJ *obj, size_t *sz));
#ifdef METER_H
int    TXfetchGetFetchMeter ARGS((HTOBJ *obj, MDOUTFUNC **outFunc,
                                  MDFLUSHFUNC **flushFunc,
                                  void **userData, TXMDT *type));
int    TXfetchSetFetchMeter ARGS((HTOBJ *obj, MDOUTFUNC *outFunc,
                                  MDFLUSHFUNC *flushFunc,
                                  void *userData, TXMDT type));
#endif /* METER_H */
size_t TXfetchGetDownloadDoc ARGS((HTOBJ *obj, byte **doc));
int   TXfetchGetShowWidgets ARGS((HTOBJ *obj, char mask[HTINTYPE_NUM]));
int   TXfetchSetShowWidgets ARGS((HTOBJ *obj, CONST char *mask));
int   htsettimeout ARGS((HTOBJ *obj, int timeout));
int   htgettimeout ARGS((HTOBJ *obj));
int   htsetagent ARGS((HTOBJ *obj, char *agent, char *host, char *vers,
		       char *comm));
int   htsetanyagent ARGS((HTOBJ *obj, char *agent));    /* MAW 960801 */
int   TXfetchSetBaseUrl(HTOBJ *obj, const char *baseUrl);
int   htclearaccept ARGS((HTOBJ *obj));                 /* KNG 961125 */
int   htaddaccept ARGS((HTOBJ *obj, char *media, int qualpct));  /*KNG 961125*/
int   htsetmaxredirs ARGS((HTOBJ *obj, int redirs));
void  htgetflags ARGS((HTOBJ *obj, byte *htsfFlags));
int   htgetflag ARGS((HTOBJ *obj, HTSF flag));
int   htsetflag ARGS((HTOBJ *obj, HTSF flag, int yes));
int   htcopyflags ARGS((HTOBJ *obj, CONST byte *htsfFlags));
#define htsetredirmsgs(obj, yes)    htsetflag((obj), HTSF_REDIRMSGS,(yes))
#define htsetignorealttxt(obj, yes) htsetflag((obj), HTSF_IGNOREALT,(yes))
#define htsetignorestrike(obj, yes) htsetflag((obj), HTSF_NOSTRIKE,(yes))
#define htsetoffsiteok(obj, yes)    htsetflag((obj), HTSF_OFFSITEOK,(yes))
#define htset8bithtml(obj, yes)     htsetflag((obj), HTSF_DO8BIT,(yes))
#define htsetreparentimg(obj, yes)  htsetflag((obj),HTSF_REPARENTIMG, (yes))
int   htsetmaxframes ARGS((HTOBJ *obj, int frames));
int   TXfetchSetHost2ipFunc(HTOBJ *obj, HTHOST2IP *func);
HTHOST2IP *TXfetchGetHost2ipFunc(HTOBJ *obj);

const char      *TXproxyModeToString(TXproxyMode mode);
TXproxyMode     TXstringToProxyMode(const char *s, const char *e);
int   TXfetchSetProxy(HTOBJ *obj, const char *url);
int   TXfetchSetPacScript(HTOBJ *obj, const char *script);
int   TXfetchSetPacUrl(HTOBJ *obj, const char *url);
int   TXfetchSetPacFetchRetryDelay(HTOBJ *obj, double delay);
int   TXfetchSetProxyRetryDelay(HTOBJ *obj, double delay);
int   htsetproxyusername ARGS((HTOBJ *obj, const char *proxyuser));
int   htsetproxypassword ARGS((HTOBJ *obj, const char *proxypass));
int   TXfetchSetProxyMode(HTOBJ *obj, TXproxyMode mode);
int   TXfetchClearProxyCache(HTOBJ *obj);
HTOBJ *TXfetchGetPacFetchObj(HTOBJ *obj);

typedef int (HTOKURL) ARGS((URL *url, void *data));     /* KNG 970515 */
#define HTOKURLPN       ((HTOKURL *)NULL)
#define HTOKURLREVCB    ((HTOKURL *)1)
int   htsetokurlfunc ARGS((HTOBJ *obj, HTOKURL *func, void *data));
CONST HTOUR *htokurlreq ARGS((HTOBJ *obj));             /* KNG 010601 */
int   htokurlack ARGS((HTOBJ *obj, int ok));            /* KNG 010601 */
REPARENT TXfetchGetReparentMode ARGS((HTOBJ *obj));
REPARENT htsetreparentmode ARGS((HTOBJ *obj, REPARENT mode));
int   htsetlinelen ARGS((HTOBJ *obj, size_t len));
size_t htgetlinelen ARGS((HTOBJ *obj));
int   htsetifmodsince ARGS((HTOBJ *obj, time_t tim, char *timstr));
int   htsetreqhdrs ARGS((HTOBJ *obj, CGISL *hdrs));     /* KNG 971211 */
int htsetreqhdr ARGS((HTOBJ *obj, CONST char *hdr, CONST char * CONST *vals));

  /* NOTE: see also mmsg.h: */
typedef enum TXfetchVerbose_tag
  {
    TXfetchVerbose_ResponseConnection           = 0x000001,
    TXfetchVerbose_RequestConnection            = 0x000002,
    TXfetchVerbose_ResponseLine                 = 0x000004,
    TXfetchVerbose_RequestLine                  = 0x000008,
    TXfetchVerbose_ResponseHeaders              = 0x000010,
    TXfetchVerbose_RequestHeaders               = 0x000020,
    /* do ...IfBinary flags also for text MIMEs: */
    TXfetchVerbose_ResponseBinaryFlagsAlsoIfText= 0x000040,
    TXfetchVerbose_RequestBinaryFlagsAlsoIfText = 0x000080,
    /* Note: these are set for TXfetchVerbose_SMALL_TO_MEDIUM() to work: */
    TXfetchVerbose_ResponseSmallRawdocHexIfBinary=0x000100,/* 16 lines/256B*/
    TXfetchVerbose_ResponseSmallRawdocIfText    = 0x000200, /* 16 lines */
    TXfetchVerbose_ResponseSmallFormattedIfText = 0x000400, /* 16 lines */
                                               /* 0x000800 future anytotx? */
    TXfetchVerbose_ResponseMediumRawdocHexIfBinary=0x001000,/*128 lines/2KB*/
    TXfetchVerbose_ResponseMediumRawdocIfText   = 0x002000, /* 128 lines */
    TXfetchVerbose_ResponseMediumFormattedIfText= 0x004000, /* 128 lines */
                                               /* 0x800000 future anytotx? */
    TXfetchVerbose_RequestSmallRawdocHexIfBinary=0x010000, /* 16 lines/256B*/
    TXfetchVerbose_RequestSmallRawdocIfText     = 0x020000, /* 16 lines */
    TXfetchVerbose_RequestSmallFormattedIfText  = 0x040000, /* 16 lines */
                                               /* 0x080000 future anytotx? */
    TXfetchVerbose_RequestMediumRawdocHexIfBinary=0x100000, /*128 lines/2KB*/
    TXfetchVerbose_RequestMediumRawdocIfText    = 0x200000, /* 128 lines */
    TXfetchVerbose_RequestMediumFormattedIfText = 0x400000, /* 128 lines */
                                               /* 0x800000 future anytotx? */
  }
TXfetchVerbose;
#define TXfetchVerbose_SMALL_TO_MEDIUM(f)       ((f) << 4)

int   htsetverbose(HTOBJ *obj, TXfetchVerbose verbose);
int   htgeterrno ARGS((HTOBJ *obj));                    /* KNG 980806 */
int   htseterrno ARGS((HTOBJ *obj, HTERR err, int iffOk)); /* KNG 021009 */
int   htsetminclrdiff ARGS((HTOBJ *obj, int diff));     /* KNG 980912 */
HTDNS *htgetdns ARGS((HTOBJ *obj));                     /* KNG 000125 */
int   htsetscriptmem ARGS((HTOBJ *obj, size_t sz));
size_t htgetscriptmem ARGS((HTOBJ *obj));
int   TXfetchGetScriptGcThreshold(HTOBJ *obj, size_t *sz, TXbool *isPercent);
int   TXfetchSetScriptGcThreshold(HTOBJ *obj, size_t sz, TXbool isPercent);
int   htsetscripttimeout ARGS((HTOBJ *obj, int timeout));
int   htgetscripttimeout ARGS((HTOBJ *obj));
int   htsetscriptmaxtimer ARGS((HTOBJ *obj, int time));
int   htgetscriptmaxtimer ARGS((HTOBJ *obj));
int   htsetscriptevents ARGS((HTOBJ *obj, HTEVTYPE *events));
HTEVTYPE *htgetscriptevents ARGS((HTOBJ *obj));
int   htsetprotocols ARGS((HTOBJ *obj, HTPROT *protocols));
HTPROT *htgetprotocols ARGS((HTOBJ *obj));
int   htsetlinkprotocols ARGS((HTOBJ *obj, HTPROT *protocols));
HTPROT *htgetlinkprotocols ARGS((HTOBJ *obj));
int   htsetembedsecurity ARGS((HTOBJ *obj, HTEMSEC emsec));
int   htsetsecure ARGS((HTOBJ *obj, HTSECURE sec));
int   htsetmethods ARGS((HTOBJ *obj, HTMETH methods));
HTMETH htgetmethods ARGS((HTOBJ *obj));

TXbool  TXfetchGetIPProtocols(HTOBJ *obj, TXbool *okIPv4, TXbool *okIPv6);
TXbool  TXfetchSetIPProtocols(HTOBJ *obj, TXbool okIPv4, TXbool okIPv6);
TXbool  TXfetchGetIPProtocolsAvailable(HTOBJ *obj, TXbool *okIPv4,
                                       TXbool *okIPv6);

int   htsetsslprotocols ARGS((HTOBJ *obj, HTSSLPROT sslProtMask));
HTSSLPROT htgetsslprotocols ARGS((HTOBJ *obj));
int   TXfetchSetSslCertificate ARGS((HTOBJ *obj, CONST char *certBuf,
                                    size_t certBufLen, CONST char *certFile));
int   TXfetchSetSslCertificateKey ARGS((HTOBJ *obj, CONST char *keyBuf,
            size_t keyBufLen, CONST char *keyFile, CONST char *passwd,
            CONST char *noPwdMsg));
int   TXfetchSetSslCertificateChain ARGS((HTOBJ *obj, CONST char *chainBuf,
            size_t chainBufLen, CONST char *chainFile, int skipFirst));
int   TXfetchSetSslCACertificate ARGS((HTOBJ *obj, CONST char *caCertBuf,
            size_t caCertBufLen, CONST char *caCertFile));
int   TXfetchSetSslVerifyServer ARGS((HTOBJ *obj,
                                      CONST byte *sslVerifyErrMask));
int   TXfetchSetSslVerifyDepth ARGS((HTOBJ *obj, int sslVerifyDepth));
TXbool TXfetchSetSslCiphers(HTOBJ *obj, TX_SSL_CIPHER_GROUP group,
                            const char *sslCiphers);
const char *TXfetchGetSslCiphers(HTOBJ *obj, TX_SSL_CIPHER_GROUP group);

int   htsetfiletypes ARGS((HTOBJ *obj, HTFTYPE *ftypes));
HTFTYPE *htgetfiletypes ARGS((HTOBJ *obj));
int   htsetfileinclude ARGS((HTOBJ *obj, char **ftrees));
char **htgetfileinclude ARGS((HTOBJ *obj));
int   htsetfileexclude ARGS((HTOBJ *obj, char **ftrees));
char **htgetfileexclude ARGS((HTOBJ *obj));
int   htsetfilenonlocal ARGS((HTOBJ *obj, HTFNL val));
HTFNL htgetfilenonlocal ARGS((HTOBJ *obj));
int   htsetfileroot ARGS((HTOBJ *obj, char *root));
char *htgetfileroot ARGS((HTOBJ *obj));

int   TXfetchSetInputFileRoot ARGS((HTOBJ *obj, char *inputFileRoot));
char *TXfetchGetInputFileRoot ARGS((HTOBJ *obj));

int TXfetchSetSaslPluginPath(HTOBJ *obj, const char *saslPluginPath);
char *TXfetchGetSaslPluginPath(HTOBJ *obj);
int TXfetchSetSaslMechanisms(HTOBJ *obj, char **saslMechanisms);
char **TXfetchGetSaslMechanisms(HTOBJ *obj);
char **TXfetchGetSaslMechanismsAvailable(HTOBJ *obj);

int TXfetchSetSspiPackages(HTOBJ *obj, char **sspiPackages);
char **TXfetchGetSspiPackages(HTOBJ *obj);
char **TXfetchGetSspiPackagesAvailable(HTOBJ *obj);

CONST HTCSINFO *htstr2charset ARGS((CONST HTCSCFG *cfg, CONST char *s,
                                    CONST char*e));
CONST char *htcharset2str ARGS((HTCHARSET charset));
int TXcharsetConfigOpenFromFile ARGS((HTCSCFG **cfgp, TXPMBUF *pmbuf,
         HTERR *hterrnop, CONST char *f, int yap));
int TXcharsetConfigOpenFromText ARGS((HTCSCFG **cfgp, TXPMBUF *pmbuf,
         HTERR *hterrnop, CONST char *text, int yap, CONST char *filename));
HTCSCFG *TXcharsetConfigClose ARGS((HTCSCFG *cfg));
HTCSCFG *TXcharsetConfigClone ARGS((HTCSCFG *cfg));
char    *TXcharsetConfigToText ARGS((TXPMBUF *pmbuf, HTCSCFG *cfg));

int   htsetcharsettext ARGS((HTOBJ *obj, CONST char *charset));
HTCHARSET htgetcharsettext ARGS((HTOBJ *obj, CONST char **charset));
int   htsetcharsetsrcdefault ARGS((HTOBJ *obj, CONST char *charset));
HTCHARSET htgetcharsetsrcdefault ARGS((HTOBJ *obj, CONST char **charset));
int   htsetcharsetsrc ARGS((HTOBJ *obj, CONST char *charset));
HTCHARSET htgetcharsetsrc ARGS((HTOBJ *obj, CONST char **charset));
int   htsetcharsetconverter ARGS((HTOBJ *obj, CONST char *charsetconverter));
CONST char *htgetcharsetconverter ARGS((HTOBJ *obj));
int   htsetcharsetconfigfromfile ARGS((HTOBJ *obj,
                                       CONST char *charsetconfigfile));
int   htsetcharsetconfigfromtext ARGS((HTOBJ *obj,
                                       CONST char *charsetconfigtext));
HTCSCFG *htgetcharsetconfigobj ARGS((HTOBJ *obj));
int   htsetscreenwidth ARGS((HTOBJ *obj, int width));
int   htgetscreenwidth ARGS((HTOBJ *obj));
int   htsetscreenheight ARGS((HTOBJ *obj, int height));
int   htgetscreenheight ARGS((HTOBJ *obj));
int   htsettracescript ARGS((HTOBJ *obj, int n));
int   htgettracescript ARGS((HTOBJ *obj));

/* tracefetch flags: page fetching and parsing actions (documented): */
typedef enum HTTRF_tag
{
  HTTRF_All                     = -1,
  HTTRF_TopLevelFetch           = 0x0001,       /* user-started net. fetch */
  HTTRF_SubObjectFetch          = 0x0002,       /* sub-object network fetch */
  HTTRF_SubObjectCacheFetch     = 0x0004,       /* sub-object from cache */
  HTTRF_StaticLinks             = 0x0008,       /* static (HTML) links */
  HTTRF_ScriptLinks             = 0x0010,       /* script links added */
  HTTRF_StaticNonLinks          = 0x0020,       /* static images/frames/etc */
  HTTRF_ScriptNonLinks          = 0x0040,       /* script images/frames/etc */
  HTTRF_TopLevelUserSourceFetch = 0x0080,       /* user-started format ftch */
  HTTRF_ProxyCache              = 0x0100,       /* proxy cache (!script) */
  HTTRF_RedirectFetch           = 0x0200,
  HTTRF_AuthFetch               = 0x0400,
  HTTRF_ProxyRetryFetch         = 0x0800,
  HTTRF_EmptyResponseRetryFetch = 0x1000,
  HTTRF_PacFetch                = 0x2000,
}
HTTRF;
#define HTTRFPN         ((HTTRF *)NULL)

int   htsettracefetch ARGS((HTOBJ *obj, HTTRF flags));
HTTRF htgettracefetch ARGS((HTOBJ *obj));

double htgetlastuse ARGS((HTOBJ *obj));
int   htsetpmbuf ARGS((HTOBJ *obj, TXPMBUF *pmbufclone, int flags));
TXPMBUF *htgetpmbuf ARGS((HTOBJ *obj));
int   htsethtrefs ARGS((HTOBJ *obj, CONST HTREF *htrefs, size_t num,
                        int isalloc));
size_t htgethtrefs ARGS((HTOBJ *obj, CONST HTREF **htrefsp));

int   htisok_token ARGS((CONST char *s, CONST char *e));
void  htmkok_token ARGS((char *d, char *s));
int   htisok_hdrval ARGS((CONST char *s, CONST char *e));
void  htmkok_hdrval ARGS((char *d, char *s));
void  htfa_reset ARGS((HTFA *args));                    /* KNG 990915 */
int   hturlvars ARGS((HTBUF *buf, CGISL *vars));

/* non-blocking calls: */
int     htstartfetch(HTOBJ *obj, const char *url, HTMETH method, HTFA *args);
int     htcontfetch ARGS((HTOBJ **objs, int num, int timeout));
int     htstopfetch ARGS((HTOBJ *obj));
int     TXfetchClearError(HTOBJ *obj);
int     htisfetchdone ARGS((HTOBJ *obj, HTPAGE **pgp));

HTPAGE *htfetchpage(HTOBJ *obj, const char *url, HTMETH method, HTFA *args);
HTPAGE *htgetpage ARGS((HTOBJ *obj, const char *url));
HTPAGE *htgetpageform ARGS((HTOBJ *obj, char *url, CGISL *vars)); /* 960917 */
HTPAGE *htpostpageform ARGS((HTOBJ *obj, char *url, CGISL *vars)); /* 960917 */
HTPAGE *htmakepage(HTOBJ *obj, const char *url, const char *statusLine,
                   CGISL *hdrsCgisl, char **hdrsStrList, char *downloadDoc,
                   size_t downloadDocLen, TXbool ownDownloadDoc,
                   TXbool leaveAttached, const char *errToken);
int     htcopypage ARGS((HTOBJ *obj, char *url, char *file));
HTPAGE *closehtpage ARGS((HTPAGE *pg));

/* util: */
const char *htstrerror(HTERR err);
HTERR TXhterrTokenToNum(const char *errToken);
const char *TXhterrUserTokenStr(HTERR err);
char *htprwr ARGS((HTSKT *skt, int ing));
void  htfreestrlist ARGS((char **list, int num));
char **htfreenlist ARGS((char **list));
size_t htcountnlist ARGS((char **list));
char **htdupnlist ARGS((HTOBJ *obj, char **list));
char *htgethostname(TXPMBUF *pmbuf, int fast);
int   htissamesite ARGS((CONST URL *a, CONST URL *b));
char *htfileurl2path ARGS((HTOBJ *obj, CONST char *url, CONST URL *parts,
                           int flags));
char *TXobjHostForMsgs(HTOBJ *obj);
char *TXobjPortForMsgs(HTOBJ *obj);
char *htcontype ARGS((char *ext));
CONST char *TXfetchMimeTypeToExt ARGS((CONST char *mimeType,
                                       CONST char *mimeTypeEnd));
CONST char *htparseparams ARGS((CGISL *sl, CONST char *s, int flags));
HTMETH  htstr2method ARGS((CONST char *s, CONST char *e));  /* KNG 980714 */
CONST char      *htmethod2str ARGS((HTMETH meth));
HTPROT  htstr2protocol ARGS((CONST char *s, CONST char *e));/* KNG 990920 */
CONST char      *htprotocol2str ARGS((HTPROT prot));
unsigned htstdport ARGS((HTPROT prot));
HTSSLPROT       htstr2sslprotocol ARGS((CONST char *s, CONST char *e));
CONST char      *htsslprotocol2str ARGS((HTSSLPROT prot));
HTSSLPROT       hthdrtok2sslprotocol ARGS((CONST char *s, CONST char *e));
CONST char      *htsslprotocol2hdrtok ARGS((HTSSLPROT prot));
CONST char      *TXsslVerifyErrEnumToString ARGS((TXsslVerifyErr errNum));
TXsslVerifyErr   TXsslVerifyErrStringToEnum ARGS((CONST char*s,CONST char*e));
int              TXsslVerifyErrParseStringToMask ARGS((TXPMBUF *pmbuf,
    byte *mask, CONST char *settingName, CONST char*s, int isVerifyOfClient));
HTFTYPE  htstr2filetype ARGS((CONST char *s, CONST char *e));
CONST char      *htfiletype2str ARGS((HTFTYPE ft));
HTCTS htstr2contenttypesrc ARGS((CONST char *s, CONST char *e));
CONST char *htcontenttypesrc2str ARGS((HTCTS ft));

extern CONST char       HtTextPlain[], HtTextHtml[], HtTextJavascript[];
extern CONST char       HtFormUrlencoded[], HtMultipartFormdata[];
extern CONST char       TXAppXml[], TXTextXml[], TXAppXhtml[];
extern CONST char       TXTextVndWapWml[];
extern CONST char       TXApplicationOctetStream[];
extern CONST char       TXDirHtmlHeaderFmt[], TXDirHtmlFooterFmt[];
extern CONST char       TXDirHtmlTableAllFmt[], TXDirHtmlTableNameFmt[];
extern CONST char       TXDirHtmlItemAllFmt[], TXDirHtmlItemNameFmt[];
extern CONST char       TXDirHtmlTimeLargeFmt[], TXDirHtmlTimeSmallFmt[];
extern CONST char       TXthismessageUrl[];
extern const char       TXrssMimeType[];

#define TX_DIR_HTML_TIME_DIFF_SEC       (60*60*24*182)

/* These must be in ascending sorted order by header string name: */
#define TX_HTTP_HEADER_SYMBOLS_LIST             \
I(ACCEPT,               "Accept")               \
I(ACCEPTENCODING,       "Accept-Encoding")      \
I(AUTHORIZATION,        "Authorization")        \
I(CONNECTION,           "Connection")           \
I(CONTENTENCODING,      "Content-Encoding")     \
I(CONTENTLENGTH,        "Content-Length")       \
I(CONTENTLOCATION,      "Content-Location")     \
I(CONTENTTYPE,          "Content-Type")         \
I(COOKIE,               "Cookie")               \
I(HOST,                 "Host")                 \
I(IFMODIFIEDSINCE,      "If-Modified-Since")    \
I(PROXYAUTHORIZATION,   "Proxy-Authorization")  \
I(TE,                   "TE")                   \
I(UPGRADE,              "Upgrade")              \
I(USERAGENT,            "User-Agent")

typedef enum TXHTHDR_tag
{
  TXHTHDR_UNKNOWN       = -1,                   /* must be first */
#undef I
#define I(tok, hdrVal)  TXHTHDR_##tok,
  TX_HTTP_HEADER_SYMBOLS_LIST
#undef I
  TXHTHDR_NUM                                   /* must be last */
}
TXHTHDR;
#define TXHTHDRPN       ((TXHTHDR *)NULL)

TXHTHDR TXhttpHeaderStrToToken ARGS((CONST char *s, CONST char *e));
CONST char *TXhttpHeaderTokenToStr ARGS((TXHTHDR hdr));

/* ------------------------------ htformat.c: ------------------------------ */

int htstr2clr ARGS((char *s));
int htdomainmatch ARGS((HTOBJ *obj, CONST char *host, CONST char *domain));
TXbool htaddcookietojar(HTOBJ *obj, const HTPAGE *pg, const char *hdrval,
                        TXbool fromScript, TXbool isMeta);
char *htgetcookiejar ARGS((HTOBJ *obj, int all, int netscape4x));
int htsetcookiejar ARGS((HTOBJ *obj, CONST char *s, int append));
int htprcookiehdrval ARGS((HTBUF *buf, HTOBJ *obj, CONST URL *u,
                           int fromScript));
int htsetmaxpagecachesz ARGS((HTOBJ *obj, size_t sz));
size_t htgetmaxpagecachesz ARGS((HTOBJ *obj));
#define TX_CHARSET_DETECT_BUF_SZ        (128*1024)
HTCHARSET htdetectcharset ARGS((CONST char *buf, size_t sz));
int htformatpage(HTOBJ *obj, HTPAGE *pg, const HTFPINFO *parentInfo,
                 TXbool forMakePage);
int htgetframe(HTOBJ *obj, HTPAGE *pg, TXbool getFrames, TXbool getIframes,
               TXbool forMakePage);
int htreparent ARGS((HTOBJ *obj, HTPAGE *pg, REPARENT reparentMode,
                CONST char *newroot, TXmimeId **mimeIds, size_t numMimeIds));
int TXfetchMimeTypeIsRunnableJavaScript(const char *mimeType);

int TXfetchGetContentTypeFlags(const HTOBJ *obj, const char *contentType);

/* ------------------------------- htdom.c: ------------------------------- */

CONST char *TXfetchInputTypeToString ARGS((HTINTYPE inputType));
HTINTYPE    TXfetchStringToInputType ARGS((CONST char *s, CONST char *e));
size_t htmkformdata ARGS((HTOBJ *htObj, HTFORM *form, HTINPUT *submit,
                          char **data, char **boundary));
char *htgetdomvalue ARGS((HTOBJ *htObj, HTPAGE *pg, CONST char *domstr,
                          size_t *retSz));
int htsetdomvalue ARGS((HTOBJ *htObj, HTPAGE *pg, CONST char *domstr,
                        CONST char *val));
int htsetdomvaluebyobj ARGS((HTOBJ *htObj, void *obj, HTDOMOBJTYPE objtype,
                             CONST char*prop, CONST char *val, size_t valsz));

/* ------------------------------- htbuf.c: ------------------------------- */

typedef enum HTBF_tag           /* HTBUF flags */
{
  HTBF_DO8BIT   = (1 << 0),     /* do 8-bit HTML decoding */
  HTBF_ERROR    = (1 << 1),     /* error occurred */
  HTBF_CONST    = (1 << 2),     /* HTBUF.data is read-only and non-alloced */
  HTBF_NOALLOC  = (1 << 3),     /* HTBUF.data is non-alloced */
  HTBF_NOMSG    = (1 << 4),     /* do not yap about errors */
  HTBF_ATOMIC   = (1 << 5),     /* atomic mode */
  HTBF_SRCUTF8  = (1 << 6),     /* source is UTF-8 */
  HTBF_DESTUTF8 = (1 << 7),     /* destination is UTF-8 */
  HTBF_OWNFMTCP = (1 << 8)      /* HTBUF.fmtcp is owned and should be freed */
}
HTBF;
#define HTBFPN          ((HTBF *)NULL)

HTBF  htbuf_setflags ARGS((HTBUF *buf, HTBF flags, int set));
HTBF  htbuf_getflags ARGS((HTBUF *buf, HTBF flags));
char *htbuf_geteol(HTBUF *buf);
int   htbuf_setunhtml ARGS((HTBUF *buf, HTCHARSETFUNC *unhtml, UTF flags));
HTCHARSETFUNC   *htbuf_getunhtml ARGS((HTBUF *buf, UTF *flags));
int   htbuf_setpmbuf ARGS((HTBUF *buf, TXPMBUF *pmbufclone, int flags));
TXPMBUF *htbuf_getpmbuf ARGS((HTBUF *buf));

/* ------------------------------- htbufio.c: ------------------------------ */

HTSKT   *htskt_open ARGS((void));
HTSKT   *htskt_close ARGS((HTSKT *skt));
int     htskt_setbuf ARGS((HTSKT *skt, HTBUF *buf, int sktowned));
HTBUF   *htskt_getbuf ARGS((HTSKT *skt, int *sktowned));
HTERR   htskt_geterrno ARGS((HTSKT *skt));
int     htskt_seterrno ARGS((HTSKT *skt, HTERR hterrno));
char    *TXhtsktGetPayloadDesc ARGS((HTSKT *skt));
int     TXhtsktSetPayloadDesc ARGS((HTSKT *skt, CONST char *payloadDesc));

typedef enum TXHBIF_tag                         /* HTBUF I/O flags */
{
  TXHBIF_ADVANCE_SENDLIMIT              = 0x01, /* advance sendlimit too */
  TXHBIF_EACCES_AS_UNAUTHORIZED         = 0x02, /* 1st-read EACCES issue */
  TXHBIF_SZ_IS_LIMIT                    = 0x04, /* `sz' is limit not request*/
  TXHBIF_VIRGIN_READ_ERR_AS_EOF         = 0x08, /* minor 1st-read err is EOF*/
}
TXHBIF;
#define TXHBIFPN        ((TXHBIF *)NULL)

int htbuf_flushnblk ARGS((HTSKT *skt));
int htbuf_flush ARGS((HTSKT *skt, time_t deadline));
int htbuf_readnblk ARGS((HTSKT *skt, size_t sz, TXHBIF flags, size_t slack));
int htbuf_read ARGS((HTSKT *skt, size_t sz, TXHBIF flags, time_t deadline));
int htbuf_getlinenblk ARGS((HTSKT *skt, char **sp, char **eolp, TXHBIF flags,
                            size_t limit));
int htbuf_getline ARGS((HTSKT *skt, char **sp, char **eolp, TXHBIF flags,
                        size_t limit, time_t deadline));
int htskt_set ARGS((HTSKT *skt, CONST char *host, const char *remoteIp,
                    unsigned port, int desc));

/* ------------------------------- htparse.c: ------------------------------ */

int hturlisabsolute ARGS((char *url));

char *hturlencbad ARGS((char *url));
char *hturlabsolute ARGS((char *url, char *relatedurl, CONST byte *htsfFlags));
char *hturlrelative ARGS((char *url, char *relatedurl, CONST byte *htsfFlags));
char *hturlclean ARGS((char *url, CONST byte *htsfFlags));
TXbool TXfetchSchemeHasFilePaths(const char *scheme, size_t len);

int TXhtsktWaitForMultipleSockets(HTSKT **skts, int num, double deadline,
                                  TXbool okIntr);

/* these munge the string param: */
void hturlzapbad ARGS((char *url));
void hturlcanonslash ARGS((char *url));
void hturlzapendspace (char *url);

#include "htpf.h"
#include "htcharset.h"
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int htparsehdrs ARGS((CONST char *desc, CONST char *what, HTBUF *buf,
                      CGISL *hdrs));
int htFindCsvHdrToken ARGS((CGISL *hdrs, CONST char *hdr, CONST char *token));
int TXinetparse(TXPMBUF *pmbuf, TXtraceDns traceDns, const char *inetStr,
                TXsockaddr *inet);
TXbool TXinetabbrev(TXPMBUF *pmbuf, char *d, size_t dlen,
                    const TXsockaddr *inet, int netBits, TXbool canon);
const char *TXinetclass(TXPMBUF *pmbuf, const TXsockaddr *inet, int netBits);

/* Bitmask such that hi-order `netbits' (0-32) of the lowest 32 bits are 1.
 * (wtf check for 0 added because some machines treat n << 32 as n << 0):
 */
#define TX_HARDWARE_ORDER_INET_NETMASK(netbits)                 \
  ((netbits) ? ~((1U << (32 - (netbits))) - 1U) : 0U)

/* True if ipa/netbitsa contains ipb/netbitsb.  ips are unsigned int IPs,
 * netbits are # of netmask bits (0-32):
 */
#define TX_HARDWARE_ORDER_INET_CONTAINS_INET(ipa, netbitsa, ipb, netbitsb)  \
  ((netbitsa) == 0 ||                                                       \
   ((netbitsa) <= (netbitsb) &&                                             \
    ((unsigned)(ipa) & TX_HARDWARE_ORDER_INET_NETMASK(netbitsa)) ==         \
    ((unsigned)(ipb) & TX_HARDWARE_ORDER_INET_NETMASK(netbitsa))))

#define TX_HARDWARE_ORDER_INET_CONTAINS_IP(ipa, netbitsa, ipb)          \
  TX_HARDWRE_ORDER_INET_CONTAINS_INET(ipa, netbitsa, ipb, 32)

extern CONST char       TxIsValidXmlCodepointIso[256];

/* TX_UNICODE_CODEPOINT_IS_VALID_XML(n): true if `n' is a valid XML codepoint.
 * NOTE: TX_IS_VALID_UNICODE_CODEPOINT(n) is assumed to be true:
 */
#define TX_UNICODE_CODEPOINT_IS_VALID_XML(n)                            \
  ((unsigned)(n) < 256 ? TxIsValidXmlCodepointIso[(unsigned)(n)] :      \
   ((n) != 0xfffe && (n) != 0xffff))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

size_t  TXparseMailAliases ARGS((TXPMBUF *pmbuf, CONST char *buf,
                                 CONST char *bufEnd, int flags,
                                 char ***aliases,
                                 char ****typeLists, char ****targetLists));

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

char *TXfetchStrdup ARGS((HTOBJ *obj, CONST char *fn, CONST char *s));

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif	/* !HTTP_H */
