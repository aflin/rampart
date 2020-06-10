#ifndef HTTPI_H
#define HTTPI_H

#ifdef _WIN32
#  include <wininet.h>
#endif /* _WIN32 */
#ifndef SIZES_H
#  include "sizes.h"
#endif
#ifndef CGI_H
#  include "cgi.h"
#endif
#ifndef HTDNS_H
#  include "htdns.h"
#endif
#ifndef HTTP_H
#  include "http.h"
#endif
#ifndef POOL_H
#  include "pool.h"
#endif
#ifndef EZSOCK_H
#  include "ezsock.h"
#endif
#ifndef TXTYPES_H
#  include "txtypes.h"   /* for TXATOMINT, TXstrerror(), TXCTEHF */
#endif
#ifndef METER_H
#  include "meter.h"
#endif /* !METER_H */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Internal HTTP lib definitions
 */

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >>> If any structs, etc. change here, update JDOM_VERSION in jdom.h <<<<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

/* I/O buffer: */
struct HTBUF_tag
{
  char          *data;          /* actual buffer */
  size_t        cnt, sent, sz;  /* used, written counts; size of buffer */
  size_t        sendlimit;      /* limit for `sent' */
  char          *eol;           /* current EOL (if reading by line) */
  HTBF          flags;
  TXFMTCP       *fmtcp;         /* owned; NULL for default */
  /* We have our own TXFMTSTATE struct here too, to save an alloc
   * in openhtbuf(), and more importantly, to allow a local-var HTBUF
   * to be initialized by htbuf_init() without any allocs, for in-signal
   * usage by Vortex:
   */
  TXFMTSTATE    privateFs;      /* default state: internal */
  TXFMTSTATE    *fs;            /* owned; may == &privateFs */
  /* see also htbuf.c: */
#if TXATOMINTBITS == EPI_OS_SIZE_T_BITS /* can use cnt/sz */
#  define htbuf_atomcnt cnt
#  define htbuf_atomsz  sz
#  define TX_HTBUF_ATOM_TYPE    size_t
#  define TXhtbufGetDataSz(buf) ((buf)->cnt)
#else /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
#  define TX_HTBUF_ATOM_TYPE    TXATOMINT
  TX_HTBUF_ATOM_TYPE    htbuf_atomcnt, htbuf_atomsz;
#  define TXhtbufGetDataSz(buf) \
  (((buf)->flags & HTBF_ATOMIC) ? (size_t)(buf)->htbuf_atomcnt : (buf)->cnt)
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
  TXATOMINT     refcnt, agetcnt, agetsz;
  size_t        maxsz;          /* max alloced size permitted */
  HTCHARSETFUNC *unhtml;        /* HTML decode function */
  UTF           unhtmlFlags;    /* flags for it */
  TXPMBUF       *pmbuf;         /* (optional) putmsg buffer */
};

#define HTBUF_CHUNK	256	/* min size to inc buf */

typedef enum HTGT_tag           /* Gopher document types */
{
  HTGT_UNKNOWN  = -1,           /* must be first */
  HTGT_TEXTFILE,
  HTGT_MENU,
  HTGT_CSOPHONE,
  HTGT_ERROR,
  HTGT_BINHEX,
  HTGT_PCBINARY,
  HTGT_UUENCODED,
  HTGT_INDEXSEARCH,
  HTGT_TELNET,
  HTGT_BINARY,
  HTGT_DUPSERVER,
  HTGT_GIF,
  HTGT_IMAGE,
  HTGT_TN3270,
  HTGT_NUM
}
HTGT;
#define HTGTPN  ((HTGT *)NULL)

typedef enum HTFW_tag           /* ezswaitmulti()/HTSKT wait flags */
{
  HTFW_NONE             = 0,
  HTFW_READ             = EWM_READ,     /* ezswaitmulti() */
  HTFW_WRITE            = EWM_WRITE,    /* ezswaitmulti() */
  HTFW_EXCEPTION        = EWM_EXCEPTION,/* ezswaitmulti() */
  HTFW_ERR              = EWM_ERR,      /* ezswaitmulti() */
  HTFW_TIMEOUT          = (1 << 4),
  HTFW_WRITEVIRGIN      = (1 << 5),     /* no writes to this socket yet */
  HTFW_NONSKT           = (1 << 6),     /* file descriptor is not a socket */
  HTFW_NOCONNMSG        = (1 << 7),     /* no msg if connection refused */
  HTFW_SSL              = (1 << 8),     /* use SSL */
  HTFW_ALARMCLOSE       = (1 << 9),     /* use alarm() to timeout close() */
  HTFW_NOCLOSE          = (1 <<10),     /* do not close descriptor (stdin) */
  HTFW_READVIRGIN       = (1 <<11),     /* no reads from this socket yet */
  HTFW_NOCLOSEBUF       = (1 <<12),     /* do not close `buf' */
  HTFW_NOCLOSESSLCTX    = (1 <<13),     /* do not close `sslctx' */
  HTFW_DATAGRAM_SKT     = (1 <<14),     /* datagram socket */
  HTFW_DidSslClientOptions = (1 << 15), /* called TXsslSetClientOptions() */
  HTFW_Connected        = (1 <<16)
}
HTFW;
#define HTFWPN          ((HTFW *)NULL)
#define HTFW_SKTBITS    (~(HTFW_NOCONNMSG | HTFW_ALARMCLOSE | HTFW_NOCLOSEBUF))

/* Internal fetch state.
 * I(enumToken, description)
 */
#define HTFS_SYMBOLS_LIST                                       \
I(IDLE,                 "idle")                                 \
I(GetPacScriptViaSubObj,"getting PAC script via sub object")    \
I(STARTFETCH,           "starting fetch")                       \
I(LOOKUPHOST,           "hostname lookup")                      \
I(MAKECONN,             "connecting")                           \
I(SSLHANDSHAKE,         "SSL handshake")                        \
I(CREATEREQ,            "creating request")                     \
I(SENDREQ,              "sending request")                      \
I(READCONTENTDESC,      "reading local content")                \
I(SENDCONTENT,          "sending content")                      \
I(SENDCONTENTLAST,      "sending last content chunk")           \
I(READRESPCODE,         "reading response line")                \
I(READHDRS,             "reading headers")                      \
I(SKIPHDRSEOL,          "reading end-of-headers line")          \
I(READCONTENT,          "reading content")                      \
I(SAVECONTENT,          "writing to local content")             \
I(OKURLREVCB,           "waiting for user callback")            \
I(FETCHDONE,            "done")                                 \
I(FetchFailed,          "failed")

typedef enum HTFS_tag           /* fetch engine states */
{
#undef I
#define I(enumToken, description)       HTFS_##enumToken,
HTFS_SYMBOLS_LIST
#undef I
  HTFS_NUM                                      /* must be last */
}
HTFS;
#define HTFSPN          ((HTFS *)NULL)

extern CONST char * CONST      TXfetchFetchStateDesc[HTFS_NUM];

typedef enum HTFSF_tag                          /* fetch state flags */
{
  HTFSF_SWITCHINGPROTOCOLS      = (1 << 0),     /* switching after 101 code */
  HTFSF_READMAINEOF             = (1 << 1),     /* read EOF on `mainconn' */
  HTFSF_GotLastCteEof           = (1 << 2),     /* CTE. EOF on last filter */
  HTFSF_GENERATEDHTML           = (1 << 3),     /* content is generated HTML*/
  HTFSF_OFFEREDUPGRADE          = (1 << 4),     /* offered Upgrade: */
  HTFSF_NoResponseBodyExpected  = (1 << 5),     /* e.g. HEAD request */
  HTFSF_WireRcvLimitIsShort     = (1 << 6),     /*max...size <Content-Length*/
  HTFSF_OrgEntityRcvLimitIsShort= (1 << 7),
  HTFSF_CteDecodeOpenFailed     = (1 << 8),     /* CTE filter open failed */
  HTFSF_GotFirstCteEof          = (1 << 9),     /* got EOF on first filter */
  HTFSF_GotMaxPageSizeExceeded  = (1 <<10),     /* got (but not reported) */
  HTFSF_DoingConnectMethod      = (1 <<11),     /* doing CONNECT request */
  /* The encoding filters were tried with the current input buffer,
   * and produced no forward progress:
   */
  HTFSF_FilterNoProgressThisInput = (1 << 12),

  /* flags to clear when setting HTFS_STARTFETCH: */
  HTFSF_CLRATSTARTFLAGS         = ~0
}
HTFSF;
#define HTFSFPN ((HTFSF *)NULL)

typedef enum HTOU_tag                           /* OPTIONS/Upgrade state */
{
  HTOU_OFF,                                     /* did not do it */
  HTOU_OPTIONS,                                 /* in OPTIONS request */
  HTOU_MAINREQ                                  /* doing main request */
}
HTOU;
#define HTOUPN  ((HTOU *)NULL)

/* FTP states (often higher level than HTFS):
 *   I(symbol, "while"-string-description)
 */
#define HTFTPSTATESYMBOLS_LIST                  \
I(IDLE,         "idle")                         \
I(CONNECT,      "connecting")                   \
I(LOGIN,        "logging in")                   \
I(STARTMETHOD,  "starting command")             \
I(CWD,          "changing directory")           \
I(CWDHOME,      "changing to home directory")   \
I(CWDHOMETILDE, "changing to home directory via `CWD ~'") \
I(PWDHOME,      "getting home directory")       \
I(TYPE,         "setting TYPE")                 \
I(PASV_EPSV,    "sending PASV or EPSV")         \
I(PORT_EPRT,    "sending PORT or EPRT")         \
I(COMMAND,      "sending command")              \
I(COMMAND2,     "sending second command")       \
I(ACCEPTDATASKT,"accepting data connection")    \
I(CONNECTDATASKT,"connecting to data socket")   \
I(DATATRANSFER, "transferring data")            \
I(READRESPCODE, "reading command response")

typedef enum HTFTPSTATE_tag
{
#undef I
#define I(s, d) HTFTPSTATE_##s,
HTFTPSTATESYMBOLS_LIST
#undef I
  HTFTPSTATE_NUM                                /* must be last */
}
HTFTPSTATE;
#define HTFTPSTATEPN    ((HTFTPSTATE *)NULL)

extern CONST char * CONST       TXfetchFtpStateDesc[HTFTPSTATE_NUM];

typedef enum HTFTPDF_tag        /* FTP dir/file try states */
{
  HTFTPDF_IDLE,
  HTFTPDF_DIR_FIRST,            /* dir before file */
  HTFTPDF_FILE_SECOND,          /* file after dir */
  HTFTPDF_FILE_FIRST,           /* file before dir */
  HTFTPDF_DIR_SECOND            /* dir after file */
}
HTFTPDF;
#define HTFTPDFPN       ((HTFTPDF *)NULL)

typedef struct HTCOOKIEJAR_tag  /* shared cookie jar */
{
  int           refcnt;         /* number of HTOBJs using this jar */
  int           wildcards;      /* nonzero: domain/path wildcards */
  HTCOOKIE      *cookies;       /* list of cookies */
}
HTCOOKIEJAR;
#define HTCOOKIEJARPN   ((HTCOOKIEJAR *)NULL)

typedef struct HTPGCACHE_tag    /* shared page cache (JavaScript/frames) */
{
  size_t        cursz, maxsz;   /* current/maximum byte size */
  int           refcnt;         /* number of HTOBJs using this cache */
  HTPAGE        *pagefirst;     /* list of pages, most-recent-used first */
  HTPAGE        *pagelast;      /* last page in cache */
}
HTPGCACHE;
#define HTPGCACHEPN     ((HTPGCACHE *)NULL)

typedef struct HTHOST_tag
{
  double        insertTimeCFR, lastAccessTimeCFR;
  char          *host;
  TXsockaddr    sockaddr;
}
HTHOST;
#define HTHOSTPN        ((HTHOST *)NULL)
#define HTHOSTPPN       ((HTHOST **)NULL)

typedef struct HTHOSTCACHE_tag  /* shared host -> IP cache */
{
  int           refcnt;         /* number of HTOBJs using this cache */
  HTHOST        **hosts;
  int           num, sz;
  double        firstUseTimeCFR;
  long          hits, misses, unknown, expired;
}
HTHOSTCACHE;
#define HTHOSTCACHEPN   ((HTHOSTCACHE *)NULL)

/* Temporary directory entry for file:// dir lists: */
typedef struct HTDIRENTRY_tag
#ifdef TXSTATBUF_DEFINED
{
  TXSTATBUF     statBuf, lstatBuf;
  char          file[TX_ALIGN_BYTES];
}
#endif /* TXSTATBUF_DEFINED */
HTDIRENTRY;
#define HTDIRENTRYPN    ((HTDIRENTRY *)NULL)
#define HTDIRENTRYPPN   ((HTDIRENTRY **)NULL)

/* ----------------------------- Authorization ---------------------------- */

/* forward declaration: */
typedef struct HTCONN_tag       HTCONN;
#define HTCONNPN        ((HTCONN *)NULL)

/* shared cache amongst HTOBJs: */
typedef struct HTPSPACECACHE_tag        HTPSPACECACHE;
#define HTPSPACECACHEPN ((HTPSPACECACHE *)NULL)

typedef enum HTPSF_tag
{
  HTPSF_DEL     = (1 << 0)      /* deleted entry */
}
HTPSF;
#define HTPSFPN ((HTPSF *)NULL)

typedef struct HTPSPACE_tag     HTPSPACE;
#define HTPSPACEPN              ((HTPSPACE *)NULL)
#define HTPSPACEPPN             ((HTPSPACE **)NULL)

/* - - - - - - - - - - - - - - - - - auth.c - - - - - - - - - - - - - - - - */

/* access functions for conn.c/http.c/etc.: */
HTAUTH         htpspace_getauthscheme ARGS((CONST HTPSPACE *ps));
HTPROT         htpspace_getprotocol(const HTPSPACE *ps);
CONST char    *htpspace_getuser ARGS((CONST HTPSPACE *ps));
CONST char    *htpspace_getpass ARGS((CONST HTPSPACE *ps));
HTPSPACECACHE *htpspace_getcache ARGS((CONST HTPSPACE *ps));
HTPSF          htpspace_getflags ARGS((CONST HTPSPACE *ps));

HTAUTH     htauth_cachegetrespdata ARGS((HTPSPACECACHE *pc, CONST URL *url,
        CGISL *resphdrs, CGISL **authparams, TXPMBUF *pmbuf, HTERR *hterrno));
HTPSPACE  *htauth_cachegetpspace ARGS((HTPSPACECACHE *pc, HTPROT wireProtocol,
                    URL *url, HTAUTH scheme, TXPMBUF *pmbuf, HTERR *hterrno));

HTPSPACECACHE *htauth_cacheopen ARGS((HTPSPACECACHE *clone,
                                      TXPMBUF *pmbufclone));
HTPSPACECACHE *htauth_cacheclose ARGS((HTPSPACECACHE *pc));
int         htauth_cachesetpmbuf ARGS((HTPSPACECACHE *pc, TXPMBUF *pmbufclone,
                                       int flags));
TXPMBUF    *htauth_cachegetpmbuf ARGS((HTPSPACECACHE *pc));
int         htauth_cachesetschemes ARGS((HTPSPACECACHE *pc, HTAUTH *schemes));
HTAUTH     *htauth_cachegetschemes ARGS((HTPSPACECACHE *pc));
CONST char *htauth_cachegetdefuser ARGS((HTPSPACECACHE *pc));
CONST char *htauth_cachegetdefpass ARGS((HTPSPACECACHE *pc));
int         htauth_cacheisokscheme ARGS((HTPSPACECACHE *pc, HTAUTH scheme));
HTPSPACE   *htauth_cachegetanonpspace ARGS((HTPSPACECACHE *pc));
void TXfetchCannotMutuallyAuthenticateServer(TXPMBUF *pmbuf,
                                             const HTPSPACE *ps);

int         htauth_attach ARGS((HTPSPACE *ps));
int         htauth_detach ARGS((HTPSPACE *ps));

extern CONST char       HtAuthorizationFmt[];
extern CONST char       HtDefAuthMask[];

/* shared connection cache amongst HTOBJs: */
typedef struct HTCONNCACHE_tag  HTCONNCACHE;
#define HTCONNCACHEPN   ((HTCONNCACHE *)NULL)

/* Automagically prototype htauth* handlers for each scheme.
 * NOTE: If these change, update AUTHMETH struct in html/conn.c too:
 */
#undef I
#define I(name, token, def, okhttp, supportsExtCred)                     \
void *htauth_##name##_open ARGS((HTCONNCACHE *cc, CONST HTPSPACE *ps,    \
           HTSKT *skt, int flags, TXPMBUF *pmbufclone, HTERR *hterrno)); \
int   htauth_##name##_close ARGS((void *authobj));                       \
void *htauth_##name##_sktclosed ARGS((void *authobj));                   \
int htauth_##name##_handleresp ARGS((void *authobj, CONST HTPSPACE *ps,  \
      HTSKT *skt, CGISL *authparams, int respcode, int flags,            \
      CGISL *authparamsuser, HTERR *hterrno));                           \
int htauth_##name##_printreqhdr ARGS((void *authobj, CONST HTPSPACE *ps, \
    HTSKT *skt, HTBUF *buf, HTMETH method, CONST URL *requrl, int flags, \
    HTERR *hterrno));
HTAUTHSYMBOLS_LIST
#ifndef EPI_AUTH_DIGEST
/* Prototype these, since the digest code is compiled even if not live: */
I(Digest, "Digest", 1, 1, 0)
#endif /* !EPI_AUTH_DIGEST */
#undef I

#define htauth_anonymous_open           NULL
#define htauth_anonymous_close          NULL
#define htauth_anonymous_sktclosed      NULL
#define htauth_anonymous_handleresp     NULL
#define htauth_anonymous_printreqhdr    NULL

/* - - - - - - - - - - - - - - - - - authftp.c - - - - - - - - - - - - - - */

/* - - - - - - - - - - - - - - - - authbasic.c - - - - - - - - - - - - - - */

int htauth_prbasichdrdata ARGS((HTBUF *buf, CONST char *user,
                                CONST char *pass));

/* - - - - - - - - - - - - - - - - authfile.c - - - - - - - - - - - - - - - */

TXHANDLE htauth_file_getlogonuserhandle ARGS((void *authobj));

/* - - - - - - - - - - - - - - - - authntlm.c - - - - - - - - - - - - - - - */

/* ------------------------------ Connections ----------------------------- */

/* User data for htssl_nopwdcb() callback.  Try to keep small; HTSKT field: */
typedef struct HTNOPWDCBDATA_tag
{
  TXPMBUF       *pmbuf;
  CONST char    *path;                          /* optional */
  CONST char    *msg;
  int           flags;
  /* bit 0:  set bit 1 when called */
}
HTNOPWDCBDATA;
#define HTNOPWDCBDATAPN ((HTNOPWDCBDATA *)NULL)

/* User data for htssl_GetPasswordFromUserCb() callback: */
typedef struct HTUSERPWDCBDATA_tag
{
  TXPMBUF       *pmbuf;
  CONST char    *prompt;                        /* e.g. "Password: " */
  CONST char    *path;                          /* path to key or cert */
  int           putmsgAtPrompt;                 /* nonzero: putmsg at prompt*/
  char          prevPassword[128];              /* previously-obtained pwd */
}
HTUSERPWDCBDATA;
#define HTUSERPWDCBDATAPN        ((HTUSERPWDCBDATA *)NULL)

struct HTSKT_tag                /* a client-or-server socket and its fields */
{
  int           desc;           /* the descriptor (-1 == closed) */
  HTBUF         *buf;           /* the associated buffer */
  HTERR         hterrno;        /* recent error */
  HTFW          wantbits;       /* desired rd/wr status */
  HTFW          gotbits;        /* actual rd/wr status via select() */
  char          *host;          /* remote host or file name */
  char          remoteIP[TX_SOCKADDR_MAX_STR_SZ];       /* remote IP */
  unsigned      port;           /* remote port (0 if file) */
  char          portstr[EPI_OS_INT_BITS/3+2];   /* printable version of port */
  byte          sslVerifyErrMask[TXsslVerifyErrMaskByteSz];
  int           sslVerifyDepth;
  TXsslVerifyErr sslVerifyErrRcvd;  /* error received during cert verify */
  void          *sslPeerCertificate;  /* peer cert received during verify */
  void          *sslctx, *ssl;  /* SSL fields */
  TXPMBUF       *pmbuf;         /* optional putmsg buffer */
  HTNOPWDCBDATA noPwdCbData;    /* for htssl_nopwdcb() */
  char          *payloadDesc;   /* (opt.) payload description e.g. "request"*/
};

typedef enum HCCF_tag           /* internal connection flags */
{
  HCCF_REUSABLE    = (1 << 0),  /* (public) may be re-used e.g. Keep-Alive */
  HCCF_REQWITHAUTH = (1 << 1),  /* (internal) request sent Authorization */
  HCCF_USEAUTH     = (1 << 2),  /* (internal) use curpspace (not anon) */
  HCCF_NETMODESYS  = (1 << 3),  /* (internal) use netmode sys this conn */
  HCCF_FTPPASSIVE  = (1 << 4),  /* (public) current FTP mode is passive */
  HCCF_AUTHFAILED  = (1 << 5)   /* (internal) auth call failed */
}
HCCF;
#define HCCFPN  ((HCCF *)NULL)
#define HCCF_PUBLIC_MASK        (HCCF_REUSABLE | HCCF_FTPPASSIVE)

HTCONN *htconn_openfromcache ARGS((HTCONNCACHE *cc, HTPROT prot,
               CONST char *host, unsigned port, HTPROT targetProtocol,
               const char *targetHost, unsigned targetPort, HTSECURE secure,
               HTPSPACE *ps, CONST char *cururl, TXPMBUF *pmbufclone,
               HTERR *hterrno));
HTCONN *htconn_closefromcache ARGS((HTCONN *hc, int force, HTERR *hterrno));
char   *htconn_getip ARGS((HTCONN *hc));
int     htconn_setip ARGS((HTCONN *hc, CONST char *ip, HTERR *hterrno));
int     htconn_openssl ARGS((HTCONN *hc, HTERR *hterrnoP));
int     htconn_openskt ARGS((HTCONN *hc, CONST char *useragent,
                    CONST char *user, CONST char *pass, HTERR *hterrno));
int     htconn_statfiles ARGS((HTCONN *hc, HTDIRENTRY **dirEntries,
                               size_t numDirEntries, HTERR *hterrno));
TXbool  htconn_closesktwrite(HTCONN *hc);
int     htconn_closeskt ARGS((HTCONN *hc, int force, HTERR *hterrno));
HTSKT  *htconn_getskt ARGS((HTCONN *hc));
char   *TXhtconnGetTunneledHost(HTCONN *hc);
unsigned TXhtconnGetTunneledPort(HTCONN *hc);
int     TXhtconnSetTunneled(HTCONN *hc, HTPROT protocol, const char *host,
                            unsigned port);
void    TXhtconnVerboseConnectionMsg(HTCONN *hc, TXfetchVerbose what);
#ifdef _WIN32
HINTERNET htconn_getinetconnhandle ARGS((HTCONN *hc));
#endif /* _WIN32 */
int     htconn_getnextfile ARGS((HTCONN *hc, char **fp, int *isdir));
#ifdef TXSTATBUFPN
TXSTATBUF *htconn_getstatbuf ARGS((HTCONN *hc));
#endif /* TXSTATBUFPN */
HCCF    htconn_getflags ARGS((HTCONN *hc));
int     htconn_setflags ARGS((HTCONN *hc, HCCF flags, int yes,
                              CONST char *reason));
CONST char *TXhtconnGetFtpHomeDir ARGS((HTCONN *hc));
int     TXhtconnSetFtpHomeDir ARGS((HTCONN *hc, HTERR *hterrnop,
                                    CONST char *homeDir));
CONST char *htconn_getftpcwd ARGS((HTCONN *hc));
int     htconn_setftpcwd ARGS((HTCONN *hc, HTERR *hterrnop, CONST char *cwd));
size_t  htconn_getsktrequests ARGS((HTCONN *hc));
CGISL  *htconn_getauthparamsuser ARGS((HTCONN *hc, int take));
int     htconn_ismatch ARGS((HTCONN *hc, HTPROT prot, CONST char *host,
                             unsigned port, HTPROT targetProtocol,
                             const char *targetHost, unsigned targetPort,
                             HTSECURE secure));
int     htconn_issktconn ARGS((HTCONN *hc));
int     htconn_HandleAuthResponse(HTCONN *hc, CGISL *authparams,
                                  int respcode, HTERR *hterrno);
int     htconn_printauthreqhdr ARGS((HTCONN *hc, HTBUF *buf, HTMETH method,
                                     CONST URL *requrl, int ifNotAuthHdr,
                                     HTERR *hterrno));
HTPSPACE *htconn_getcurpspace ARGS((HTCONN *hc));
int     htconn_setcurpspace ARGS((HTCONN *hc, HTPSPACE *ps,
                                  CONST char *cururl, HTERR *hterrno));
HTCONNCACHE *TXhtconnGetHtconncache ARGS((HTCONN *hc));

HTCONNCACHE *htconn_cacheopen ARGS((HTCONNCACHE *clone, TXPMBUF *pmbufclone));
HTCONNCACHE *htconn_cacheclose ARGS((HTCONNCACHE *cc));
double  htconn_cacheexpire ARGS((HTCONNCACHE *cc, int which,
                                 int serialAndNewer));
int     htconn_cachesetpmbuf ARGS((HTCONNCACHE *cc, TXPMBUF *pmbufclone,
                                   int flags));
TXPMBUF *htconn_cachegetpmbuf ARGS((HTCONNCACHE *cc));
TXtraceDns  TXhtconncacheGetTraceDns(HTCONNCACHE *cc);
TXbool      TXhtconncacheSetTraceDns(HTCONNCACHE *cc, TXtraceDns traceDns);

TXbool  TXhtconncacheSetSslCiphers(HTCONNCACHE *cc, TX_SSL_CIPHER_GROUP group,
                                   const char *sslCiphers);
const char *TXhtconncacheGetSslCiphers(HTCONNCACHE *cc,
                                       TX_SSL_CIPHER_GROUP grop);

#ifdef TXSSLSYMS_H
int     TXhtconncacheSetCertificate ARGS((HTCONNCACHE *cc, X509 *x509));
X509   *TXhtconncacheGetCertificate ARGS((HTCONNCACHE *cc));
int     TXhtconncacheSetPrivateKey ARGS((HTCONNCACHE *cc, EVP_PKEY *key));
int     TXhtconncacheSetCertificateChain ARGS((HTCONNCACHE *cc,
                                               TX_STACK_OF_X509 *x509Stack));
int     TXhtconncacheSetCACertificates ARGS((HTCONNCACHE *cc,
                                             TX_STACK_OF_X509 *x509Stack));
#endif /* TXSSLSYMS_H */

int     TXhtconncacheSetSslVerifyErrMask ARGS((HTCONNCACHE *cc,
                                               CONST byte *sslVerifyErrMask));
int     TXhtconncacheSetSslVerifyDepth ARGS((HTCONNCACHE *cc,
                                             int sslVerifyDepth));

void    htconn_cachesettraceauth ARGS((HTCONNCACHE *cc, int n));
int     TXhtconncacheGetTraceAuth(HTCONNCACHE *cc);
int     htconn_cachecopyflags ARGS((HTCONNCACHE *cc, CONST byte *htsfFlags));
byte   *TXhtconncacheGetHtsfFlags(HTCONNCACHE *cc);
HTFTYPE *htconn_cachegetfiletypes ARGS((HTCONNCACHE *cc));
int     htconn_cachesetfiletypes ARGS((HTCONNCACHE *cc,CONST HTFTYPE*ftypes));

/* ------------------------------- FTP ------------------------------------ */

int     htftp_connectdataskt ARGS((HTOBJ *obj));
int     htftp_createreq ARGS((HTOBJ *obj));
int     htftp_parserespline ARGS((HTOBJ *obj, char *line));
int     htftp_afterrespline ARGS((HTOBJ *obj));
int     htftp_fixlinks ARGS((HTOBJ *obj));

/* ----------------------------- proxy.c: --------------------------------- */

/* Object with settings and info about proxies; shared amongst shared HTOBJs.
 * Internal HTOBJ use only:
 */
typedef struct TXproxyCache_tag TXproxyCache;

TXproxyCache *TXproxyCacheOpen(TXPMBUF *pmbuf);
TXproxyCache *TXproxyCacheClone(TXproxyCache *cache);
TXproxyCache *TXproxyCacheClose(TXproxyCache *cache);

HTERR   TXproxyCacheSetManualProxy(TXproxyCache *cache, const char *url);
HTERR TXproxyCacheSetPacScript(TXproxyCache *cache, const char *script,
                               const char *contentType, int isManual);
HTERR   TXproxyCacheSetManualPacUrl(TXproxyCache *cache, const char *url);
HTERR   TXproxyCacheSetPacFetchRetryDelay(TXproxyCache *cache, double delay);
double  TXproxyCacheGetPacFetchRetryDelay(TXproxyCache *cache);
HTERR   TXproxyCacheSetProxyRetryDelay(TXproxyCache *cache, double delay);
double  TXproxyCacheGetProxyRetryDelay(TXproxyCache *cache);
HTERR   TXproxyCacheSetUser(TXproxyCache *cache, const char *user);
char   *TXproxyCacheGetUser(TXproxyCache *cache);
HTERR   TXproxyCacheSetPassword(TXproxyCache *cache, const char *password);
char   *TXproxyCacheGetPassword(TXproxyCache *cache);
HTERR   TXproxyCacheSetMode(TXproxyCache *cache, TXproxyMode mode);
TXproxyMode TXproxyCacheGetMode(TXproxyCache *cache);
HTERR TXproxyCacheSetTraceFetch(TXproxyCache *cache, HTTRF traceFetch);
int     TXproxyCacheSetPmbuf(TXproxyCache *cache, TXPMBUF *pmbuf, int flags);
HTERR   TXproxyCacheGetHterrno(TXproxyCache *cache);

TXproxy **TXproxyCacheFindProxyListForUrl(TXproxyCache *cache, HTOBJ *obj,
                                          const char *url, int fast);
#define TXPROXYLIST_PENDING_FETCH       ((TXproxy **)1)
#define TXPROXYLIST_RUN_JAVASCRIPT      ((TXproxy **)2)
#define TXPROXYLIST_IS_SPECIAL(list)            \
  ((list) == TXPROXYLIST_PENDING_FETCH ||       \
   (list) == TXPROXYLIST_RUN_JAVASCRIPT)
HTOBJ  *TXproxyCacheGetPacFetchObj(TXproxyCache *cache);
int     TXproxyCacheUsedProxy(TXproxyCache *cache, TXproxy *proxy, int good);
int     TXproxyCacheClear(TXproxyCache *cache);

/* ------------------------------------------------------------------------ */

struct HTOBJ_tag
{
  HTCONN        *mainconn;      /* primary/control connection */
  HTSKT         *curskt;        /* currently used socket (`mainconn' etc.) */
  int           cursktIsPacFetch;  /* `curskt' is proxyCache's HTOBJ skt */
  HTSKT         *dataskt;       /* FTP data socket (server or accept'd) */
  HTPROT        wireProtocol;   /* over-the-wire protocol of active fetch */
  HTBUF         *outbuf;        /* output buffer */
  HTBUF         *inbuf;         /* input buffer */
  int           httpVersion;    /* desired HTTP vers: ((major << 8) + minor)*/
  size_t        maxhdrsize;     /* headers max size (-1 == no limit) */
  size_t	maxpgsize;      /* post-CTE-decode max body size (-1 == "") */
  size_t        maxDownloadSize;/* pre-CTE-decode max body size (-1 == "") */
  int		timeout;	/* duration of timeout (seconds) */
  time_t	deadline;	/* deadline for current op (now + timeout) */
  int		maxredirs;	/* maximum # of redirects to follow */
  int		maxframes;	/* maximum # of frames to fetch */
  HTHOST2IP	*host2ip;	/* resolver function */
  char		*useragent;	/* string for User-Agent header */
  HTBUF         *accept;        /* Accept header value */
  TXproxyCache  *proxyCache;    /* proxy settings and cache */
  HTOKURL       *okurlfunc;     /* function to call to validate redirects */
  void          *okurldata;     /*   user data for it */
  char          *ifmodsince;    /* If-Modified-Since value (optional) */
  time_t        ifmodsincedate; /* "" parsed as date (0 if not set) */
  CGISL         *reqhdrs;       /* (shared) opt. request headers from user */
  int           *reqhdrsrefcnt; /* (shared) reqhdrs reference count */

  TXfetchVerbose verbose;       /* verbosity bits */

  byte          flags[HTSF_BYTE_ARRAY_SZ];
  REPARENT      reparentmode;   /* how to reparent */
  int           minclrdiff;     /* minimum fg/bg text color diff */
  size_t        linelen;        /* chars per line to word wrap formatted txt*/
  size_t        scriptmem;      /* max total mem for JavaScript, bytes */
  size_t        scriptGcThreshold; /* garbage-collection threshold, bytes */
  size_t        scriptGcThresholdUser;  /* GC user value, to compute "" */
  TXbool        scriptGcThresholdUserIsPercent;
  int           scripttimeout;  /* max total time for JavaScript */
  int           scriptmaxtimer; /* max timer (in seconds) to call */
  size_t        writeBufferSize;/* block transfer rate to `savefile' */
  HTDNS         *dns;           /* non-blocking DNS lookup object */
  HTCOOKIEJAR   *cookiejar;     /* js-mod; cookies; shared across duphtobj */
  char          eventmask[HTEVTYPE_NUM];        /* which events to call */
  HTPGCACHE     *pgcache;       /* page cache; shared across duphtobj */
  HTHOSTCACHE   *hostcache;     /* shared host -> IP cache */
  HTPSPACECACHE *pspacecache;   /* shared protection space cache */
  HTCONNCACHE   *conncache;     /* shared connection cache */
  char          protocolmask[HTPROT_NUM];       /* fetchable protocols */
  char          linkprotmask[HTPROT_NUM];       /* linkable protocols */
  HTMETH        methods;        /* allowed methods (bitmask) */
  HTEMSEC       embedsecurity;  /* embedded objects security level */
  HTSECURE      secure;         /* secure transaction level */
  int           secureSetByUser;/* nonzero: `secure' set by user */
  HTSSLPROT     sslProtocols;   /* allowed SSL protocols (bitmask) */
  TXbool        okIPv4, okIPv6; /* allowed IP protocols */
  char          **fileinclude;  /* fileinclude prefixes */
  char          **fileexclude;  /* fileexclude prefixes */
  HTFNL         filenonlocal;   /* filenonlocal setting */
  char          *fileroot;      /* fileroot setting */
  HTCHARSET     charsettext;    /* charset text setting */
  char          *charsettextbuf;/* "" if unk. */
  HTCHARSET     charsetsrcdefault; /*if !unk: src charset if !(detect/expl.)*/
  char          *charsetsrcdefaultbuf;/* "" if unk. */
  HTCHARSET     charsetsrc;     /* if !unk: forced src charset assumption */
  char          *charsetsrcbuf; /* "" if unk. */
  char          *charsetconverter;      /* if NULL: check texis.cnf */
  HTCSCFG       *charsetconfig; /* charsets config */
  int           screenwidth, screenheight;      /* js-rdonly */
  char          *baseUrl;       /* (opt.) overrides page URL, <base> etc. */
  /* tracescript bit flags (documented):
   * 0x0001:  inline scripts run
   * 0x0002:  remote scripts run
   * 0x0004:  javascript: links run
   * 0x0008:  timers run
   * 0x0010:  objects checked for events
   * 0x0020:  modify-and-call-event-handler objects run
   * 0x0040:  event handlers run (normal or modify)
   * 0x0080:  check-string-for-URL called
   * 0x0100:  check-string-for-URL adds link
   * 0x0200:  script output inserted (into raw HTML parse buffer)
   * 0x0400:  event handlers added
   * 0x0800:  memory allocated
   * 0x1000:  memory freed
   * 0x2000:  other (e.g. PAC) scripts run
   * todo: elements-add anchors-add inputs-add options-add forms-add links-add
   */
  int           tracescript;
  /* traceauth (documented):
   * 0x0001:  protection spaces made (added/expanded/deleted)
   * 0x0002:  protection spaces used (attached/detached)
   * 0x0004:  auth objects used (stop/start/handle/connclosed/hdr)
   * 0x0008:  authentication protocol-specific msgs
   * 0x0010:  Keep-Alive
   * 0x0020:  WWW-Authenticate parsing
   * 0x0040:  connection open/close/idle/re-use
   * 0x0080:  socket/descriptor/handle open/close
   */
  int           traceauth;
  HTTRF         tracefetch;                     /* documented */
  /* traceEncoding (documented):
   * 0x0001     open/close objects
   * 0x0002     ...TranslateEncoding filter calls
   * 0x0004     decoder object state changes etc.
   * 0x0008     some zlib calls (encode/decode)
   * 0x0010
   * 0x0020
   * 0x0040     data read
   * 0x0080     data written
   */
  int           traceEncoding;
  TXtraceDns    traceDns;                       /* see ezsock.h for values */
  TXtraceHtmlParse      traceHtmlParse;
  HTREF         *htrefs;
  size_t        htrefsnum;      /* number of elements in `htrefs' */
  char          encodingMask[TXCTE_NUM];        /* allowed encodings */
  MDOUTFUNC     *meterOutFunc;
  MDFLUSHFUNC   *meterFlushFunc;
  void          *meterUserData;
  TXMDT         meterType;
  char          showWidgetsMask[HTINTYPE_NUM];  /* widgets to format in text*/
  char          *inputFileRoot;                 /* <urlcp inputfileroot> */
  byte          isSystem;                       /* do not license-count */

  /* State variables: */
  TXfetchType   fetchType;
  HTFS          state;          /* fetch state */
  HTFSF         stateFlags;     /* additional state */
  HTERR         hterrno;        /* last HtErrno value */
  int           syserrno;       /* last errno value */
  HTPAGE        *pg;            /* currently in-progress page */
  char          *cururl;        /* current URL (may == orgurl) */
  char          *orgurl;        /* original URL */
  char          **intermediateUrls;     /* (opt.) redirects from */
  char          *nameurl;       /* what to call the page */

  HTSSLPROT     chosenSslProtocol;      /* from `Upgrade' response header */

  TXproxy       **curProxyList; /* (opt.) proxy list for current URL */
  size_t        curProxyListIdx;/* index into `curProxyList' */

  HTMETH        method;         /* main request method */
  HTMETH        wireMethod;     /* over-the-wire method */
  HTBUF         *sendcontent;   /* content to send */
  EPI_OFF_T     sendconlen;     /*   length of it or `sendskt' data */
  HTSKT         *sendskt;       /*   read socket for HTFA sendfile|senddesc */
  char          *sendcontype;   /*   its type */
  int           sendmerged;     /*   nonzero: `sendcontent' merged into buf */
  int           redirs;         /* number of redirects so far */
  int           optUpRedirs;    /* number of OPTIONS-Upgrade `redirs' */
  int           emptyKeepAliveResponses; /* # empty responses on Keep-Alive */
  int           ancSecure;      /* nonzero: no previous insecure fetches */
  URL           *sendparts;     /* cut-up URL for send request */
  /* Over-the-wire data sent (headers + body): */
  EPI_HUGEINT   wireSendLen;
  size_t        constart;       /* offset of start of content */
  /* Response content length (-1 == unknown/not-specified), e.g. from
   * Content-Length or `NNN bytes' (FTP msg).  Includes length of HTTP
   * response line and response headers, if still in buffer:
   */
  size_t        rdconlen;
  /* Over-the-wire (i.e. before Content-/Transfer-Encoding decode)
   * received-data limit; headers + body.  -1 == unlimited.  Might
   * be `maxhdrsize' during headers read, then (`maxpgsize' or Content-Length)
   * + actual header size later:
   */
  size_t        wireRcvLimit;
  /* Over-the-wire data received: */
  size_t        wireRcvLen;
  /* Container for saving wire data received, if HTSF_SaveDownloadDoc and
   * it is not being accumulated in `obj->inbuf'.  WTF would like to keep
   * accumulating in `obj->inbuf' even if sending to encoding filter or
   * `obj->saveskt', to avoid memcpy/dup here:
   */
  HTBUF         *wireRcvBodyAccum;
  /* Original-entity (i.e. after Content-Encoding decode) received-data limit;
   * headers + body.  -1 == unlimited:
   */
  size_t        orgEntityRcvLimit;
  /* Length of original-entity (i.e. after Content-/Transfer-Encoding decode)
   * data received; headers + body:
   */
  size_t        orgEntityRcvLen;
  HTSKT         *saveskt;       /* write socket for HTFA savefile|savedesc */
  HTIOFUNC      *savefunc;      /* function to save content via */
  void          *savefuncdata;
  char          *renameto;      /* FTP RENAME destination file path */
  double        start;          /* start time of current DNS/fetch */
  double        lastuse;        /* last net activity */
  double        dnstime, transfertime;  /* cumulative DNS and transfer time */
  HTOUR         our;            /* buf for htokurlreq() */
#ifdef _WIN32
  HINTERNET     inetreqhandle;  /* request handle for HTSF_NETMODESYS */
#endif /* !_WIN32 */
  HTDIRENTRY    **direntries;
  int           numdirentries, anumdirentries;
  HTOU          optUp;          /* OPTIONS/Upgrade state */
  HTAUTH        authUsedHighest;/* highest HTAUTH used (e.g. over redirects)*/
  METER         *downloadMeter;
  METER         *uploadMeter;

  /* `tunneled...' is the protocol/host/port we are (or will) tunnel to
   * via CONNECT (if proxy settings indicate tunneling):
   */
  HTPROT        tunneledProtocol;
  char          *tunneledHost;
  TXbool        tunneledHostIsIPv6;
  unsigned      tunneledPort;

  TXCTEFILTER   *cteDecodeFilters; /* Content-/Transfer-Encoding decoders */
  TXCTEFILTER   *cteDecodeLastFilter;   /* last of "" */
  HTBUF         *cteDecodeOutBuf;  /* final output buffer for "" */
  int           cteDecodeNoProgressCount;  /* # stalled calls */
  /* Response encodings, in server-applied order.  NULL if no encodings: */
  char          **encodings;                    /* NULL-terminated */
  size_t        numEncodings;

  /* additional FTP state: */
  HTFTPSTATE    ftpstate;       /* fetch state (often higher than HTFS)*/
  int           ftpexpmajorresp;/* expected response code major class */
  HTERR         ftpsavhterrno;  /* saved hterrno from 1st try */
  int           ftpsavsyserrno; /* saved syserrno from 1st try */
  int           ftpsavrespcode; /* saved pg->respcode from 1st try */
  char          *ftpsavrespmsg; /* saved pg->respmsg from 1st try */
  HTFTPDF       ftpdirfile;     /* dir-vs.-file state */
  HTBUF         *ftprespbuf;    /* multi-line respmsg build buffer */
  char          *ftppath;       /* URL-decoded path buffer, no trailing `/' */
                                /*   (unless `/') and cleaned */
  char          ftpmode;        /* TYPE for main command */
  int           ftpredir;       /* 0: none  1: w/o slash  2: w/slash */
  TXsockaddr    ftpDataRemoteSockaddr;
  TXbool        ftpUsingExtendedActivePassive;
  /* init ftp state after calloc(HTOBJ): */
#define HTOBJ_INITFTPSTATE(obj)
  /* rest cleared by calloc() */
  /* clear ftp state after fetch: */
#define HTOBJ_CLEARFTPSTATE(obj)                        \
  {                                                     \
    (obj)->ftpstate = HTFTPSTATE_IDLE;                  \
    (obj)->ftpexpmajorresp = 0;                         \
    (obj)->ftpsavhterrno = HTERR_OK;                    \
    (obj)->ftpsavsyserrno = 0;                          \
    (obj)->ftpdirfile = HTFTPDF_IDLE;                   \
    (obj)->ftprespbuf = closehtbuf((obj)->ftprespbuf);  \
    if ((obj)->ftppath != CHARPN)                       \
      {                                                 \
        free((obj)->ftppath);                           \
        (obj)->ftppath = CHARPN;                        \
      }                                                 \
    (obj)->ftpmode = '\0';                              \
    (obj)->ftpredir = 0;                                \
    (obj)->ftpsavrespcode = 0;                          \
    if ((obj)->ftpsavrespmsg != CHARPN)                 \
      {                                                 \
        free((obj)->ftpsavrespmsg);                     \
        (obj)->ftpsavrespmsg = CHARPN;                  \
      }                                                 \
    TXsockaddrInit(&(obj)->ftpDataRemoteSockaddr);      \
    (obj)->ftpUsingExtendedActivePassive = TXbool_False;\
  }

  /* Format state: */
  double        timestart;      /* real time_t we started formatting */
  double        timeskip;       /* additional fast-forward time for timers */

  TXPMBUF       *pmbuf;                         /* putmsg() disposition */
  TXPMBUF       *tempPmbuf;                     /* temp buffer */
};

/* MIME type to extension: */
typedef struct HTMIMETYPE_tag
{
  CONST char    *type, *suffixes, *desc;
}
HTMIMETYPE;
#define HTMIMETYPEPN    ((HTMIMETYPE *)NULL)

extern int              xmlapiCheckVersion;
extern int              xsltapiCheckVersion;
extern CONST char       HtWillNotFetch[];

int  htloadjdomsymbols ARGS((TXPMBUF *pmbuf));

/* this is here instead of with the mimeReader stuff in dbquery.h
 * because it needs HTOBJ *:
 */
int  TXmimeReaderSetDefaultHtobj(HTOBJ *htobj, int dupNow);

/* - - - - - - - - - - - - - - - ssl.c: - - - - - - - - - - - - - - - - - - */

int  htssl_nopwdcb ARGS((char *buf, int size, int rw, void *usr));
int  htssl_nopwdcb2 ARGS((char *buf, int size, int rw, void *usr));
int  htssl_GetPasswordFromUserCb ARGS((char *buf, int size, int rw,
                                       void *usr));
int  TXsslGetPasswordFromStringCb ARGS((char *buf, int size, int rw,
                                        void *usr));
int  htssl_addentropy ARGS((CONST void *buf, int sz, double entropy));
int  htssl_loadentropy ARGS((CONST char *path));
int  htssl_getprngdpid ARGS((CONST char *path));
int  htssl_geterror ARGS((void *ssl, int res, char **tracemsg));
char *htssl_errorstr ARGS((char *buf, size_t sz));
void htssl_reporterr ARGS((CONST char *fn, CONST char *prefix, HTSKT *skt,
                           TXPMBUF *pmbuf, int sslerr));
int  TXsslErrorIsConnectionReset ARGS((HTSKT *skt, int sslErr));
int  TXsslInitHtobjSslFields ARGS((HTOBJ *obj));
int  TXsslCloseHtobjSslFields ARGS((HTOBJ *obj));
int  TXsslDupHtobjSslFields ARGS((HTOBJ *oldObj, HTOBJ *newObj));
int  TXsslSetAllowedProtocols ARGS((HTSKT *skt, int sslProtMask));
TXbool  TXsslSetClientOptions(HTSKT *skt, const char *requestHost,
                              int sslProtocolMask, byte htsfFlags[]);
int  TXsslUpdateHtsktBits ARGS((HTSKT *skt, int sslErr));
int  TXsslConnect ARGS((HTSKT *skt, int *sslErr));
#ifdef TXSSLSYMS_H
TXbool TXsslSetCiphers(TXPMBUF *pmbuf, SSL_CTX *sslCtx,
                       TX_SSL_CIPHER_GROUP group, const char *sslCiphers);
X509 *TXsslX509Dup ARGS((TXPMBUF *pmbuf, X509 *x509));
X509 *TXsslX509Free ARGS((X509 *x509));
X509 *TXsslGetPeerCertificate ARGS((HTSKT *skt));
char *TXsslGetCertificateCommonName ARGS((TXPMBUF *pmbuf, X509 *cert));
int  TXsslHostnameCommonNameMatch ARGS((CONST char *hostname,
                                        CONST char *cnPattern));
int  TXsslGetCertBasicConstraints ARGS((TXPMBUF *pmbuf, X509 *cert, int *isCa,
                                        int *pathLen));
TX_STACK_OF_X509_NAME *TXsslGetClientCAList ARGS((HTSKT *skt));
TX_STACK_OF_X509 *TXsslX509StackFree ARGS((TX_STACK_OF_X509 *x509Stack));
size_t TXsslX509NameStackToStrings ARGS((TXPMBUF *pmbuf,
                                         TX_STACK_OF_X509_NAME *nameStack,
                                         char ***nameStrs));
X509_STORE       *TXsslX509StoreFree ARGS((X509_STORE *x509Store));
TX_STACK_OF_X509_NAME *TXsslX509NameStackFree ARGS((TX_STACK_OF_X509_NAME *nameStack));
TX_STACK_OF_X509_NAME *TXsslX509NameStackDup ARGS((TXPMBUF *pmbuf,
                                          TX_STACK_OF_X509_NAME *nameStack));
X509 *TXsslParseCertificate ARGS((TXPMBUF *pmbuf, CONST char *certBuf,
                                  size_t certBufLen, CONST char *certFile));
char *TXsslCertificateToString ARGS((TXPMBUF *pmbuf, X509 *x509, int flags));
char *TXsslX509NameToString ARGS((TXPMBUF *pmbuf, X509_NAME *name));
EVP_PKEY *TXsslParsePrivateKey ARGS((TXPMBUF *pmbuf, CONST char *keyBuf,
                size_t keyBufLen, CONST char *keyFile, CONST char *passwd,
                CONST char *noPwdMsg, int flags));
TX_STACK_OF_X509 *TXsslParseCertificateChain ARGS((TXPMBUF *pmbuf,
            CONST char *chainBuf, size_t chainBufLen, CONST char *chainFile,
            int skipFirst, size_t *numItems));
TX_STACK_OF_X509 *TXsslParseCACertificates ARGS((TXPMBUF *pmbuf,
            CONST char *caCertBuf, size_t caCertBufLen,
            CONST char *caCertFile, size_t *numItems));
int  TXsslUseCertificate ARGS((TXPMBUF *pmbuf, SSL_CTX *sslCtx, X509 *x509,
                               CONST char *fileForMsgs));
int  TXsslUsePrivateKey ARGS((TXPMBUF *pmbuf, SSL_CTX *sslCtx, EVP_PKEY *key,
                              CONST char *fileForMsgs));
int  TXsslUseCertificateChain ARGS((TXPMBUF *pmbuf, SSL_CTX *sslCtx,
                   TX_STACK_OF_X509 *x509Stack, CONST char *fileForMsgs));
int  TXsslUseCACertificates ARGS((TXPMBUF *pmbuf, SSL_CTX *sslCtx,
                       TX_STACK_OF_X509 *x509Stack, CONST char *fileForMsgs));
#endif /* TXSSLSYMS_H */
int  TXsslPkcs7VerifyDoc(TXPMBUF *pmbuf, const char *doc, size_t docSz,
                         const char *pkcs7Sig, size_t pkcs7SigSz,
                         const char *cert, size_t certSz);
int  TXsslGrabStuff(HTPAGE *pg, HTSKT *skt);
int  TXsslWorkTillWaitHandshake(HTOBJ *obj);
int  TXsslReportPostConnectVerifyErr ARGS((HTSKT *skt));
int  TXsslSetVerify ARGS((HTSKT *skt, CONST byte *sslVerifyErrMask,
                          int sslVerifyDepth));

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int htincarray ARGS((TXPMBUF *pmbuf, void **array, size_t num,
                     size_t *anum, size_t sz));

CONST char *htcururlxtramsg ARGS((HTOBJ *obj));
char *htstrlwr ARGS((char *s));
int   htleft ARGS((HTOBJ *obj));
int   htdocontype ARGS((HTOBJ *obj, HTPAGE *pg, CONST char *s, HTCTS src));
int   htopensave ARGS((HTOBJ *obj));
int   htaddstr ARGS((char ***listp, int *nump, int *anump, char *s,
                     int isalloc));
TXrefInfo *htaddurl(HTOBJ *obj, HTPAGE *pg, const char *file, int bufnum,
                    int line, int col, TXrefTypeFlag types,
                    const char *desc, const char *url, int flags);
size_t httransbuf ARGS((HTPFOBJ *obj, HTCHARSET charset,
    CONST char *charsetbuf, HTCHARSETFUNC *func, int flags, UTF utfExtra,
    CONST char *buf, size_t sz, char **ret));

void TXfetchVerboseTextMsgs(HTOBJ *obj, TXfetchVerbose smallFlag,
                            TXbool outBound, const char *text, size_t textSz);
void TXfetchVerboseAllMsgsForTextRequests(HTOBJ *obj, const char *url,
                                      const char *rawText, size_t rawTextSz);
void TXfetchVerboseBinaryMsgs(HTOBJ *obj, TXfetchVerbose smallFlag,
                            TXbool outBound, const void *data, size_t dataSz);

HTGT  htgophertype ARGS((CONST char *path));
CONST char *htgophercontype ARGS((HTGT gt));
void  htclosestate ARGS((HTOBJ *obj, int flags));
void  TXfetchBadParameterError ARGS((HTOBJ *obj, CONST char *fn));
int   htchkmod ARGS((HTOBJ *obj, CONST char *fn));
void TXfetchSetBothRcvLimits ARGS((HTOBJ *obj, size_t wireRcvLimit,
                                   size_t orgEntityRcvLimit));

int TXfetchGetIsSystem ARGS((HTOBJ *obj));
int TXfetchSetIsSystem ARGS((HTOBJ *obj, int isSystem));

/* amount of written data (to be sent + hold): */
#define htbuf_getdatasz(buf)    ((buf)->cnt < (buf)->sent ?     \
  ((buf)->sz - (buf)->sent) + (buf)->cnt : (buf)->cnt - (buf)->sent)
/* max amount of free space (yet to be written to), if alloc to maxsz: */
#define htbuf_getmaxavailsz(buf) ((buf)->maxsz - (htbuf_getdatasz(buf) + 1))
/* amount of hold data (written, not to be sent yet): */
#define htbuf_getholdsz(buf)    ((buf)->cnt < (buf)->sendlimit ?        \
  ((buf)->sz - (buf)->sendlimit) + (buf)->cnt : (buf)->cnt - (buf)->sendlimit)
/* amount of send data (written, to be sent): */
#define htbuf_getsendsz(buf)    ((buf)->sendlimit < (buf)->sent ?       \
((buf)->sz - (buf)->sent) + (buf)->sendlimit : (buf)->sendlimit - (buf)->sent)

int   htbuf_atomicwrite ARGS((HTBUF *buf, CONST char *data, size_t len));
int CDECL htbuf_atomicpf ARGS((HTBUF *buf, CONST char *data, ...));
int   htbuf_atomicgetdata ARGS((VOLATILE HTBUF *buf, char **data));
int   htbuf_atomicgetdatadone ARGS((VOLATILE HTBUF *buf, TXATOMINT sz));

void  htfreeformat ARGS((HTPAGE *pg));
HTCOOKIEJAR *openhtcookiejar ARGS((HTOBJ *obj));
HTCOOKIEJAR *closehtcookiejar ARGS((HTCOOKIEJAR *cj));
HTPGCACHE *openhtpgcache ARGS((HTOBJ *obj));
HTPGCACHE *closehtpgcache ARGS((HTPGCACHE *pc));
HTTIMER *openhttimer ARGS((HTOBJ *obj, HTWINDOW *window, CONST char *file,
                  int linenum, CONST char *code, int msec, HTTIMERF flags));
HTTIMER *closehttimer ARGS((HTTIMER *timer));
/* KNG 20050608 VOLATILE to thwart Windows /Oa optimization: */
int     httimercall ARGS((HTOBJ *obj, VOLATILE HTTIMER *timer));

HTPAGE *htduppage ARGS((HTOBJ *obj, HTPAGE *pg));

char *TXhtobjStrdup(HTOBJ *obj, const char *fn, const char *s);
char *TXfetchResponseCodeDesc(HTPROT protocol, int respCode,
                              const char *ifUnknown);

extern CONST char       HtNoMem[];
CONST HTMIMETYPE *htdefaultmimetypelist ARGS((void));

#define HT_PUTMSG_MAERR_OBJ(obj, fn, n)                                 \
  { txpmbuf_putmsg(obj->pmbuf, MERR + MAE, fn, HtNoMem, (EPI_HUGEUINT)(n), \
                   TXstrerror(TXgeterror()));                           \
    (obj)->hterrno = HTERR_NO_MEM;                                      \
    (obj)->syserrno = errno; }

#define CR      '\015'
#define LF      '\012'

#define HTTP_CRLF_S                             "\015\012"

/* -------------------------------- htbufio.c ----------------------------- */

int     htskt_setpmbuf ARGS((HTSKT *skt, TXPMBUF *pmbufclone, int flags));
TXPMBUF *htskt_getpmbuf ARGS((HTSKT *skt));
int     htskt_setgotbits ARGS((HTSKT *skt, HTFW bits, int set));
HTFW    htskt_getgotbits ARGS((HTSKT *skt));
int     htskt_setwantbits ARGS((HTSKT *skt, HTFW bits, int set));
HTFW    htskt_getwantbits ARGS((HTSKT *skt));
#ifdef TXSSLSYMS_H
SSL_CTX *TXhtsktGetSslctx(HTSKT *skt);
#endif /* TXSSLSYMS_H */
void   *htskt_getssl ARGS((HTSKT *skt));
int     htskt_isopen ARGS((HTSKT *skt));
int     htskt_getdesc ARGS((HTSKT *skt));
int     htskt_closedesc ARGS((HTSKT *skt));
int htskt_checkwritevirginity ARGS((HTSKT *skt));
int htskt_checkeof ARGS((HTSKT *skt));

/* --------------------------------- vhttpd ------------------------------- */

#ifndef VHEXTPN                 /* see also libhttpd.h */
typedef struct VHEXT_tag        VHEXT;
#  define VHEXTPN         ((VHEXT *)NULL)
#  define VHEXTPPN        ((VHEXT **)NULL)
#endif /* !VHEXTPN */

struct VHEXT_tag                /* MIME type/encoding by file ext. */
{
  char          *ext;           /* file extension (sans `.') */
  char          *type;          /* MIME type or encoding */
  char          *desc;          /* description */
  int           isadd;          /* internal override flag */
};

#define TX_HTDEFAULTMIMETYPES_NUM       61
extern CONST VHEXT * CONST TXhtDefaultMimeTypes[];

#define TX_HTDEFAULTMIMEENCODINGS_NUM   TXCTE_NUM
extern CONST VHEXT * CONST TXhtDefaultMimeEncodings[];

int     vh_readline ARGS((FILE *fp, CONST char *fnam, char **text,
                          char **lineBuf, size_t *lineBufSz, int *line));
char    *vh_okext ARGS((CONST char *fnam, int line, char *what, char *ext,
                        int dotreq));
int     vh_lkupext ARGS((VHEXT **list, int num, CONST char *ext,
                         CONST char *extEnd));
int     vh_addext ARGS((CONST char *fnam, int line, POOL *p, VHEXT ***listp,
                   int *nump, char *what, char *ext, char *type, int isadd));
int     vh_parseconfigext ARGS((POOL *p, CONST char *file, CONST char *what,
                                VHEXT ***listp, int *nump, int opt,
                                CONST char *defaultText));

/* - - - - - - - - - - - - - html/encodings.c: - - - - - - - - - - - - - - */

int     TXfetchPrintRequestEncodingHdr ARGS((HTBUF *buf,HTOBJ *obj,int doTe));
int     TXfetchOpenResponseEncodingFilters ARGS((HTOBJ *obj,
                                                 CGISL *responseHdrs));
int     TXfetchCloseResponseEncodingFilters ARGS((HTOBJ *obj,
                                                  int saveEncList));
int     TXfetchTranslateEncodingData ARGS((HTOBJ *obj, TXCTEHF flags));

extern CONST char       TXfetchDefaultMimeEncodingsTxt[];

/* - - - - - - - - - - - vhttpd/contentNegotiation.c: - - - - - - - - - - - */

/* A header token with an associated q preference value; linked list: */
typedef struct TXqPrefItem_tag
{
  struct TXqPrefItem_tag        *next;
  double        qPref;                          /* q value */
  char          tokenStr[8];                    /* token (dynamic size) */
}
TXqPrefItem;
#define TXqPrefItemPN           ((TXqPrefItem *)NULL)

int     TXvhttpdParseQPrefList ARGS((CONST char *hdr, int starSlashStarWild,
                                     TXPMBUF *pmbuf, TXqPrefItem **list));
TXqPrefItem *TXvhttpdCloseTxQPrefItemList ARGS((TXqPrefItem *list));

/* Avoid #include of vortexi.h.  WTF this should migrate to Texis lib: */
#ifndef VSTKSTATPN
typedef struct VSTKSTAT_tag     VSTKSTAT;
#  define VSTKSTATPN    ((VSTKSTAT *)NULL)
#endif /* !VSTKSTATPN */

#ifndef TXFindNegotiatedFileServerInfoPN
typedef struct TXFindNegotiatedFileServerInfo_tag
  TXFindNegotiatedFileServerInfo;
#  define TXFindNegotiatedFileServerInfoPN      \
  ((TXFindNegotiatedFileServerInfo *)NULL)
#endif /* !TXFindNegotiatedFileServerInfoPN */

/* Only define struct TXFindNegotiatedFileServerInfo_tag if EPI_STAT_S
 * and struct stat are defined, to avoid compile warning.  Not
 * strictly necessary:
 */
#ifdef TXSTATBUF_DEFINED
struct TXFindNegotiatedFileServerInfo_tag
{
  VHEXT         **mimeTypes;
  int           numMimeTypes;
  VHEXT         **knownEncodings;
  int           numKnownEncodings;
  int           allowFileMask;                  /* -1: no check */
  int           allowDirMask;                   /* -1: no check */
  /* Returns 1 if file extension `ext' (including ".") is ok, 0 if not.
   * If pointer is NULL, all extensions assumed o:
   */
  int           (*okFileExtCbFunc) ARGS((void *usr, CONST char *ext));
  void          *okFileExtCbData;
  /* These function pointers avoid netex1 etc. having to link against
   * Vortex lib for vx_openglob() etc.; these functions should migrate
   * to Texis lib someday WTF.  See also TXCONN_INFO:
   */
  VSTKSTAT *(*globOpenFunc) ARGS((CONST char *path, CONST char *fixedEnd,
                                  int flags));
  VSTKSTAT *(*globCloseFunc) ARGS((VSTKSTAT *vss));
  char     *(*globNextFunc) ARGS((VSTKSTAT *vss, EPI_STAT_S *st, int *depth));
};
#endif /* EPI_STAT_SPN */

typedef enum TXFNF_tag
{
  /* These must be in increasing-success order: */
  TXFNF_ERR,                    /* severe error; no `*response...' vals */
  TXFNF_NOT_FOUND,              /* not found; no `*response...' values */
  TXFNF_NO_ALLOWFILEMASK,       /* found, not permitted by si->allowFileMask*/
  TXFNF_NO_ALLOWDIRMASK,        /* found, not permitted by si->allowDirMask*/
  TXFNF_NO_ALLOWEXT,            /* found, not permitted by okFileExtCbFunc()*/
  /* future server perms go here */
  TXFNF_NOT_CLIENT_ACCEPTABLE,  /* found, not acceptable per Accept-* hdrs */
#define TXFNF_OK_SERVER_PERMS   TXFNF_NOT_CLIENT_ACCEPTABLE
  TXFNF_FOUND,                  /* found, permitted and acceptable */
}
TXFNF;
#define TXFNFPN ((TXFNF *)NULL)

/* Check TXSTATBUF_DEFINED, because stat will then be defined, which
 * avoids a warning about struct stat not defined.  Not really needed,
 * and means that texint.h should be included before httpi.h if this
 * prototype is needed:
 */
#ifdef TXSTATBUF_DEFINED
TXFNF TXvhttpdFindNegotiatedFile ARGS((CONST char *requestPathname,
                                       int flags, TXPMBUF *pmbuf,
                                     CONST TXFindNegotiatedFileServerInfo *si,
                                       CONST TXqPrefItem *acceptEncodingList,
                                       char **responsePathname,
                                       char *responseHdrs[TXHTHDR_NUM],
                                       EPI_STAT_S *responseStat,
                                       int *responseErrno,
                                       int *responseIsVariant));
#endif /* TXSTATBUF_DEFINED */

extern CONST char      TXvhttpdDefaultAcceptEncoding[];

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif	/* !HTTPI_H */
