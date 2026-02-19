#ifndef HTDNS_H
#define HTDNS_H

#ifndef TX_CONFIG_H
#  include "txcoreconfig.h"
#endif /* !TX_CONFIG_H */
#ifndef MMSG_H
#  include "mmsg.h"                             /* for TXPMBUF */
#endif
#include "ezsock.h"
#ifdef unix
#  include <netdb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* NOTE: see also source/resolv/inetprivate.h: */
#if (defined(EPI_OS_MAJOR) && defined(EPI_OS_MINOR) && ((defined(__SVR4) && defined(sun) && EPI_OS_MAJOR == 2 && EPI_OS_MINOR >= 5) || (defined(sgi) && EPI_OS_MAJOR == 4))) || defined(_WIN32)
                                     /* Solaris 5.5.1, Irix 4.0 */
/* in net/resinit.c: */
#  ifdef __MINGW32__
#    ifndef EPI_INET_ATON_RETURN_TYPE
#      define EPI_INET_ATON_RETURN_TYPE int
#    endif
#  endif
extern EPI_INET_ATON_RETURN_TYPE inet_aton(const char *s, struct in_addr *a);

#  ifndef EPI_HAVE_GETHOSTNAME_PROTOTYPE
extern int gethostname ARGS((char *name, int namelen));
#  endif /* !EPI_HAVE_GETHOSTNAME_PROTOTYPE */
#endif

#ifndef HTDNSPN
typedef struct HTDNS_tag        HTDNS;
#  define HTDNSPN         ((HTDNS *)NULL)
#  define HTDNSPPN        ((HTDNS **)NULL)
#endif /* !HTDNSPN */

typedef enum HTDNSERR_tag                       /* error codes */
{
  HTDNSERR_OK,
  HTDNSERR_NO_RECOVERY,
  HTDNSERR_TRY_AGAIN,
  HTDNSERR_CANT_OPEN_HOSTS,
  HTDNSERR_CANT_CONNECT,
  HTDNSERR_CONN_REFUSED,
  HTDNSERR_TIMEOUT,
  HTDNSERR_HOST_NOT_FOUND,
  HTDNSERR_BAD_IP,
  HTDNSERR_BAD_USAGE,
  HTDNSERR_NO_MEM,
  HTDNSERR_BAD_SERVICE,
  HTDNSERR_NO_DATA,
  HTDNSERR_INTERNAL,
  HTDNSERR_UNKNOWN,
  HTDNSERR_NUM
}
HTDNSERR;
#define HTDNSERRPN      ((HTDNSERR *)NULL)

typedef enum HTDF_tag                           /* flags */
{
  HTDF_NISWARN          = (1 << 0),             /* warn about NIS */
  HTDF_SYSTEM           = (1 << 1),             /* use blocking system calls*/
  HTDF_IGNTC            = (1 << 2),             /* mapped to RES_IGNTC */
  HTDF_RECURSE          = (1 << 3),             /* mapped to RES_RECURSE */
  HTDF_SYSTEMFALLBACK   = (1 << 4),             /* fall back to HTDF_SYSTEM */
  HTDF_INITIALIZED      = (1 << 5),             /* internal use only */
  HTDF_LAST             = (1 << 6)              /* must be last/largest */
}
HTDF;
#define HTDFPN  ((HTDF *)NULL)
#define TX_HTDF_INTERNAL_FLAGS  (HTDF_INITIALIZED)
#define TX_HTDF_ALL_FLAGS       (HTDF_LAST - (HTDF)1)

typedef enum HTDNS_SERVICE_tag                  /* host lookup services */
{
  HTDNS_SERVICE_NONE,                           /* must be first/0 */
  HTDNS_SERVICE_BIND,
  HTDNS_SERVICE_HOSTS,
  HTDNS_SERVICE_NIS,                            /* not used currently */
  HTDNS_SERVICE_NUM                             /* must be last */
  /* see TXhtdnsServiceTokenToString() if list changes */
}
HTDNS_SERVICE;
#define HTDNS_SERVICEPN ((HTDNS_SERVICE *)NULL)

typedef enum HTDNSQ_tag                         /* top-level query types */
{
  HTDNSQ_NONE,
  HTDNSQ_BYNAME,
  HTDNSQ_BYADDR,
  HTDNSQ_CPHOSTENT
}
HTDNSQ;
#define HTDNSQPN        ((HTDNSQ *)NULL)

#define HTDNS_MAXNS     5                       /* must fit in `badns' bits */

typedef enum HTDNSHF_tag                        /* parsed header flags */
{
  HTDNSHF_RESPONSE      = (1 << 0),             /* is a response */
  HTDNSHF_AUTHANSWER    = (1 << 1),             /* authoritative answer */
  HTDNSHF_TRUNC         = (1 << 2),             /* truncated message */
  HTDNSHF_RECURSEDESIRE = (1 << 3),             /* recursion desired */
  HTDNSHF_RECURSEAVAIL  = (1 << 4),             /* recursion available */
  HTDNSHF_CHECKDISABLED = (1 << 5),             /* checking disabled */
  HTDNSHF_AUTHDATA      = (1 << 6),             /* authentic named data */
  HTDNSHF_UNUSED        = (1 << 7)              /* unused flag */
}
HTDNSHF;
#define HTDNSHFPN       ((HTDNSHF *)NULL)

typedef enum HTDNSOP_tag                        /* opcodes FROM SPEC */
{
  HTDNSOP_QUERY         = 0,
  HTDNSOP_IQUERY        = 1,
  HTDNSOP_STATUS        = 2,
  HTDNSOP_RESERVED1     = 3,
  HTDNSOP_NOTIFY        = 4,
  HTDNSOP_NUM                                   /* must be last */
}
HTDNSOP;
#define HTDNSOPPN       ((HTDNSOP *)NULL)

typedef enum HTDNSRESP_tag                      /* respcodes FROM SPEC */
{
  HTDNSRESP_NOERROR     = 0,
  HTDNSRESP_FORMERR     = 1,
  HTDNSRESP_SERVFAIL    = 2,
  HTDNSRESP_NXDOMAIN    = 3,
  HTDNSRESP_NOTIMP      = 4,
  HTDNSRESP_REFUSED     = 5,
  HTDNSRESP_NUM                                 /* must be last */
}
HTDNSRESP;
#define HTDNSRESPPN     ((HTDNSRESP *)NULL)

typedef enum HTDNSTYPE_tag                      /* RR/query types FROM SPEC */
{
  HTDNSTYPE_A           = 1,    /* IPv4 address */
  HTDNSTYPE_NS          = 2,    /* authoritative server */
  HTDNSTYPE_MD          = 3,    /* mail destination */
  HTDNSTYPE_MF          = 4,    /* mail forwarder */
  HTDNSTYPE_CNAME       = 5,    /* canonical name */
  HTDNSTYPE_SOA         = 6,    /* start of authority zone */
  HTDNSTYPE_MB          = 7,    /* mailbox domain name */
  HTDNSTYPE_MG          = 8,    /* mail group member */
  HTDNSTYPE_MR          = 9,    /* mail rename name */
  HTDNSTYPE_NULL        = 10,   /* null resource record */
  HTDNSTYPE_WKS         = 11,   /* well known service */
  HTDNSTYPE_PTR         = 12,   /* domain name pointer */
  HTDNSTYPE_HINFO       = 13,   /* host information */
  HTDNSTYPE_MINFO       = 14,   /* mailbox information */
  HTDNSTYPE_MX          = 15,   /* mail routing information */
  HTDNSTYPE_TXT         = 16,   /* text strings */
  HTDNSTYPE_RP          = 17,   /* responsible person */
  HTDNSTYPE_AFSDB       = 18,   /* AFS cell database */
  HTDNSTYPE_X25         = 19,   /* X_25 calling address */
  HTDNSTYPE_ISDN        = 20,   /* ISDN calling address */
  HTDNSTYPE_RT          = 21,   /* router */
  HTDNSTYPE_NSAP        = 22,   /* NSAP address */
  HTDNSTYPE_NSAP_PTR    = 23,   /* reverse NSAP lookup (deprecated) */
  HTDNSTYPE_SIG         = 24,   /* security signature */
  HTDNSTYPE_KEY         = 25,   /* security key */
  HTDNSTYPE_PX          = 26,   /* X.400 mail mapping */
  HTDNSTYPE_GPOS        = 27,   /* geographical position (withdrawn) */
  HTDNSTYPE_AAAA        = 28,   /* IPv6 Address */
  HTDNSTYPE_LOC         = 29,   /* Location Information */
  HTDNSTYPE_NXT         = 30,   /* Next Valid Name in Zone */
  HTDNSTYPE_EID         = 31,   /* Endpoint identifier */
  HTDNSTYPE_NIMLOC      = 32,   /* Nimrod locator */
  HTDNSTYPE_SRV         = 33,   /* Server selection */
  HTDNSTYPE_ATMA        = 34,   /* ATM Address */
  HTDNSTYPE_NAPTR       = 35,   /* Naming Authority PoinTeR */
  /* non standard */    
  HTDNSTYPE_UINFO       = 100,  /* user (finger) information */
  HTDNSTYPE_UID         = 101,  /* user ID */
  HTDNSTYPE_GID         = 102,  /* group ID */
  HTDNSTYPE_UNSPEC      = 103,  /* Unspecified format (binary data) */
  /* Query type values which do not appear in resource records */
  HTDNSTYPE_IXFR        = 251,  /* incremental zone transfer */
  HTDNSTYPE_AXFR        = 252,  /* transfer zone of authority */
  HTDNSTYPE_MAILB       = 253,  /* transfer mailbox records */
  HTDNSTYPE_MAILA       = 254,  /* transfer mail agent records */
  HTDNSTYPE_ANY         = 255,  /* wildcard match */
  HTDNSTYPE_NUM                 /* must be last */
}
HTDNSTYPE;
#define HTDNSTYPEPN     ((HTDNSTYPE *)NULL)

typedef enum HTDNSCLASS_tag     	        /* query class FROM SPEC */
{                               /* <arpa/nameser_compat.h> C_... values */
  HTDNSCLASS_IN         = 1,    /* C_IN: the arpa internet */
  HTDNSCLASS_CHAOS      = 3,    /* C_CHAOS: for chaos net (MIT) */
  HTDNSCLASS_HS         = 4,    /* C_HS: for Hesiod name server (MIT) */
  /* Query class values which do not appear in resource records */
  HTDNSCLASS_ANY        = 255,  /* C_ANY: wildcard match */
  HTDNSCLASS_NUM                /* must be last */
}
HTDNSCLASS;
#define HTDNSCLASSPN    ((HTDNSCLASS *)NULL)

typedef struct HTDNSRR_tag                      /* a parsed resource record */
{
  char          *name;
  HTDNSTYPE     type;
  HTDNSCLASS    theclass;                       /* avoid C++ conflict */
  long          ttl;
  size_t        datalen;
  char          *data;
}
HTDNSRR;
#define HTDNSRRPN       ((HTDNSRR *)NULL)

typedef enum HTDNSSEC_tag                       /* message sections */
{
  HTDNSSEC_QUERY,
  HTDNSSEC_ANSWER,
  HTDNSSEC_AUTH,
  HTDNSSEC_ADD,
  HTDNSSEC_NUM                                  /* must be last */
}
HTDNSSEC;
#define HTDNSSECPN      ((HTDNSSEC *)NULL)

typedef struct HTDNSMSG_tag                     /* an entire parsed message */
{
  int           id;                             /* transaction id */
  HTDNSHF       flags;
  HTDNSOP       opcode;
  HTDNSRESP     respcode;
  int           count[HTDNSSEC_NUM];
  HTDNSRR       *list[HTDNSSEC_NUM];
}
HTDNSMSG;
#define HTDNSMSGPN      ((HTDNSMSG *)NULL)

const char *TXhtdnsServiceTokenToString(HTDNS_SERVICE service);

HTDNS   *openhtdns ARGS((TXPMBUF *pmbufclone));
TXbool  TXhtdnsInit(HTDNS *dns);
HTDNS   *closehtdns ARGS((HTDNS *dns));
HTDNS   *duphtdns ARGS((HTDNS *dns));
int     htdns_setpmbuf ARGS((HTDNS *dns, TXPMBUF *pmbufclone, int flags));
TXPMBUF *htdns_getpmbuf ARGS((HTDNS *dns));
int     TXhtdnsSetTraceDns(HTDNS *dns, TXtraceDns traceDns);
int     TXhtdnsGetTraceDns(HTDNS *dns, TXtraceDns *traceDns);

/* non-blocking routines: */

int     htdns_startgethostbyname(HTDNS *dns, const char *name);
int     htdns_startgethostbyaddr(HTDNS *dns, const TXsockaddr *sockaddr);
int     htdns_islookupdone ARGS((HTDNS *dns, struct hostent **hpp));
int     htdns_contlookup ARGS((HTDNS **objs, int num, int timeout));
int     htdns_stoplookup ARGS((HTDNS *dns));

/* blocking routines: */

struct hostent  *htdns_gethostbyname(HTDNS *dns, const char *name);
struct hostent  *htdns_gethostbyaddr(HTDNS *dns, const TXsockaddr *sockaddr);

/* util functions: */

TXbool  TXhostentToSockaddr(TXPMBUF *pmbuf, const struct hostent *hostent,
                            size_t addrIdx, TXsockaddr *sockaddr);

HTDNSQ  htdns_qtype ARGS((HTDNS *dns));
size_t  htdns_getans ARGS((HTDNS *dns, epi_byte **bp));
HTDNSMSG *htdns_parsemsg ARGS((HTDNS *dns));
HTDNSMSG *closehtdnsmsg ARGS((HTDNSMSG *msg));
char    *htdns_getorgname ARGS((HTDNS *dns));
int        htdns_errnum ARGS((HTDNS *dns));
CONST char *htdns_strerror ARGS((int err));
const char *TXhtdnsOpcodeToString(HTDNSOP opcode);
const char *TXhtdnsResponseCodeToString(HTDNSRESP respcode);
const char *TXhtdnsTypeToString(HTDNSTYPE type);
const char *TXhtdnsClassToString(HTDNSCLASS theclass);
int        htdns_settimeout ARGS((HTDNS *dns, int timeout));
int        htdns_gettimeout(HTDNS *dns);
int        htdns_setflags ARGS((HTDNS *dns, HTDF flags, int on));
int        htdns_getflags ARGS((HTDNS *dns, HTDF *flags));
int     htdns_setretrans ARGS((HTDNS *dns, int retrans));
int     htdns_getretrans(HTDNS *dns, int *retrans);
int     htdns_setretry ARGS((HTDNS *dns, int retry));
int     htdns_getretry(HTDNS *dns, int *retry);
int     htdns_setservices ARGS((HTDNS *dns, HTDNS_SERVICE *svc));
char    **TXhtdnsGetNameServers(HTDNS *dns);
TXbool  TXhtdnsSetNameServers(HTDNS *dns, char **svrs);
const char *TXhtdnsGetResolvConfFile(HTDNS *dns);
TXbool      TXhtdnsSetResolvConfFile(HTDNS *dns, const char *file);
const char *TXhtdnsGetNsSwitchConfFile(HTDNS *dns);
TXbool      TXhtdnsSetNsSwitchConfFile(HTDNS *dns, const char *file);
const char *TXhtdnsGetHostConfFile(HTDNS *dns);
TXbool      TXhtdnsSetHostConfFile(HTDNS *dns, const char *file);
const char *TXhtdnsGetHostsFile(HTDNS *dns);
TXbool      TXhtdnsSetHostsFile(HTDNS *dns, const char *file);
int     htdns_setdomains ARGS((HTDNS *dns, char **doms));
TXbool  TXhtdnsSetIPProtocols(HTDNS *dns,
                              TXbool queryForIPv4, TXbool queryForIPv6);
TXbool  TXhtdnsGetIPProtocols(HTDNS *dns,
                              TXbool *queryForIPv4, TXbool *queryForIPv6);

extern CONST char * CONST       HtDnsHf[];

/* semi-private functions: */

#ifndef HTSKTPN                                 /* avoid httpi.h include */
typedef struct HTSKT_tag HTSKT;
#  define HTSKTPN ((HTSKT *)NULL)
#endif /* !HTSKTPN */

int     htdns_worktillwait ARGS((HTDNS *dns));
HTSKT   *htdns_skt ARGS((HTDNS *dns));
time_t  htdns_deadline ARGS((HTDNS *dns));
TXbool  TXhtdnsCopyHostAndSockaddr(HTDNS *dns, const char *host,
                                   const TXsockaddr *sockaddr);

#define TX_TRACESKT_FOR_DATAGRAM(traceSkt)      \
  (((traceSkt) & HTS_DATAGRAM) ? (traceSkt) : (TXtraceSkt)0)
TXPMBUF *TXopenTraceErrsPmbuf(TXPMBUF *mainPmbuf, int errMapNum,
                              const char *prefix);
TXbool TXhtdnsAppendPrefix(TXPMBUF *pmbuf, const char *prefix, size_t line,
                           char **prevPrefix);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !HTDNS_H */
