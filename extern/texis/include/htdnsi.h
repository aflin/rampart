#ifndef HTDNSI_H
#define HTDNSI_H


#ifdef unix
#  include <sys/uio.h>
#  include <sys/param.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <sys/time.h>
#  include <arpa/inet.h>
#  include <net/if.h>
#  include <arpa/nameser.h>
#  undef TMP_BIG_ENDIAN                 /* avoid SysV i386 compile warning */
#  ifdef BIG_ENDIAN
#    define TMP_BIG_ENDIAN
#  endif
#  undef BIG_ENDIAN
#  include <resolv.h>
#  if defined(TMP_BIG_ENDIAN) && !defined(BIG_ENDIAN)
#    define BIG_ENDIAN
#  endif
#  undef TMP_BIG_ENDIAN
#else
#  include <winsock.h>
#  undef h_errno
#  include "nameser.h"
#  include "epiresolv.h"
#  define MAXHOSTNAMELEN        255
#endif
#include <errno.h>
#ifndef SIZES_H
#  include "sizes.h"
#endif
#ifndef ARGS
#  include "os.h"
#endif
#ifndef HTDNS_H
#  include "htdns.h"
#endif
#ifndef HTTPI_H
#  include "httpi.h"
#endif
#include "ezsock.h"                             /* for TXtraceDns */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef HFIXEDSZ
#  define HFIXEDSZ    12
#endif
#ifndef INT16SZ
#  define INT16SZ       2
#endif
#ifndef INT32SZ
#  define INT32SZ       4
#endif

#define RES_NOENV       0x01000000              /* no environment overrides */

typedef enum HTDSL_tag                          /* overall lookup states */
{
  HTDSL_IDLE,                                   /* not doin' nothin' */
  HTDSL_BYNAME,                                 /* gethostbyname() lookup */
  HTDSL_BYNAME_OTHER_IPv,                       /* "" for other IPv family */
  HTDSL_BYADDR,                                 /* gethostbyaddr() lookup */
  HTDSL_DONE                                    /* done with lookup */
}
HTDSL;
#define HTDSLPN         ((HTDSL *)NULL)

typedef enum HTDSS_tag                          /* search sub-states for */
{                                               /*   htdns_searchtillwait() */
  HTDSS_IDLE,                                   /* not doing a search */
  HTDSS_START,                                  /* start */
  HTDSS_ALIAS,                                  /* HOSTALIASES query */
  HTDSS_DOTS,                                   /* dots-in-name query */
  HTDSS_AFTERDOTS,
  HTDSS_DOMAINSTART,                            /* check a domain */
  HTDSS_DOMAINDONE,                             /* a domain check is done */
  HTDSS_ASISSTART,                              /* as-is query start */
  HTDSS_ASISDONE,                               /*   "" done */
  HTDSS_FALLOUT,                                /* misc. end stuff */
  HTDSS_DONE                                    /* search done */
}
HTDSS;
#define HTDSSPN         ((HTDSS *)NULL)

typedef enum HTDSQ_tag                          /* query sub-states for */
{                                               /*   htdns_querytillwait() */
  HTDSQ_IDLE,                                   /* not doing a query */
  HTDSQ_START,                                  /* start */
  HTDSQ_SEND,                                   /* send() */
  HTDSQ_SENDTO,                                 /* sendto() */
  HTDSQ_RECV,                                   /* recv() */
  HTDSQ_DONE                                    /* query done */
}
HTDSQ;
#define HTDSQPN         ((HTDSQ *)NULL)

#if PACKETSZ > 1024
#  define MAXPACKET     PACKETSZ
#else
#  define MAXPACKET     1024
#endif

typedef union HTDNSQUERY_tag
{
  HEADER        hdr;
  u_char        buf[MAXPACKET];
}
HTDNSQUERY;
#define HTDNSQUERYPN    ((HTDNSQUERY *)NULL)
#undef MAXPACKET

#define HTDNS_MAXALIASES        25
#define HTDNS_MAXADDRS          25
#define HTDNS_MAXTRIMDOMAINS    4

typedef struct HTDSSI_tag                       /* additional settings */
{
  HTDNS_SERVICE         services[HTDNS_SERVICE_NUM];
  int                   hosts_multiple_addrs;
  int                   spoof, spoofalert;
  int                   reorder;
  char                  *trimdomain[HTDNS_MAXTRIMDOMAINS];
  char                  trimdomainbuf[256];
  int                   numtrimdomains;
  char                  *nsSwitchConfFile;      /* alloced, opt. */
  char                  *hostConfFile;          /* alloced, opt. */
  char                  *hostsFile;             /* alloced */
  TXbool                hostsFileSetByUser;
  char                  *defaultHostsFile;      /* alloced */
}
HTDSSI;

struct HTDNS_tag
{
  char                  *resolvConfFile;        /* alloced, opt. */
  struct __res_state    res;                    /* state info */
  char                  **userDomains;          /* alloced; set by user */
  int                   userRetrans;            /* (>= 0: set by user) */
  int                   userRetry;              /* (>= 0: set by user) */
  TXsockaddr            nameservers[HTDNS_MAXNS];
  TXbool                nameserversAreUserSet;
  HTDF                  flags;
  int                   timeout;
  HTDNSERR              errnum;                 /* last error */
  HTSKT                 *skt;

  /* settings filled out by htdns_initservices(): */
  HTDSSI                si;
  HTDNS_SERVICE         userServices[HTDNS_SERVICE_NUM];
  TXbool                haveUserServices;

  TXbool                queryForIPv4;
  TXbool                queryForIPv6;

  FILE                  *hostf;
  size_t                hostfLineNum;
  int                   stayopen;
  char                  *hostbuf;               /* scratch buf */
  size_t                hostbufsz;              /* currently alloced size */
  struct hostent        host;                   /* return value */

  /* htdns_gethtent() buffers (part of hostent return): */
  /* also htdns_startgethostbyname() */
  char                  *host_aliases[HTDNS_MAXALIASES];  /* part of `host' */
  char                  *host_addrs[2];                   /* "" */
  char                  *h_addr_ptrs[HTDNS_MAXADDRS + 1]; /* "" */
  TXsockaddr            hostAddr;

  /* htdns_gethtbyname() buffers (part of `host'): */
  char                  namebuf[MAXHOSTNAMELEN];
  char                  *ht_addr_ptrs[HTDNS_MAXADDRS + 1];
  char                  *loc_addr_ptrs[HTDNS_MAXADDRS + 1];
  char                  *aliases[HTDNS_MAXALIASES];

  /* htdns_search() buffers: */
  HTDSS                 sstate;                 /* search sub-state */
  int                   searchClass;
  HTDNSTYPE             searchType;
  u_int                 dots;
  int                   trailing_dot, saved_herrno;
  int                   got_nodata, got_servfail, tried_as_is;
  CONST char *CONST     *domain;

  /* htdns_querytillwait() buffers: */
  HTDSQ                 qstate;                 /* query sub-sub-state */
  HTDNSQUERY            *qbuf;                  /* packet query buffer */
  size_t                qbufsz;                 /*   alloced size */
  size_t                qbuflen;                /*   in-use size */
  HTDNSQUERY            *ansbuf;                /* packet reply buffer */
  size_t                ansbuflen;              /*   cooked received length */
  size_t                ansbuflenraw;           /*   raw received length */
  int                   try, ns, gotsomewhere, connreset, terrno;
  int                   v_circuit;
  u_int                 badns;
  int                   vc;                     /* "static" across sends */
  TXbool                connected;
  time_t                qdeadline;              /* timeout if blocking */

  /* htdns_startgethostbyaddr() buffers: */
  TXsockaddr            byAddr;

  HTDSL                 lookupState;            /* overall lookup state */
  int                   svcidx;                 /* current service index */
  char                  *orgnamebuf;            /* original name to look up */
  size_t                orgnamebufsz;           /* buffer size */
  time_t                overallDeadline;        /* overall timeout */
  /* Track *overall*-deadline-reached separate from HTSKT HTFW_TIMEOUT,
   * because latter may be reached multiple times (qdeadline/multiple tries):
   */
  TXbool                overallDeadlineReached;
  HTDNSQ                qtype;
  TXaddrFamily          queryAddrFamily;

  /* htdns_mkquery() state: */
  int                   qid;                    /* query id */

  TXPMBUF               *pmbuf;                 /* optional putmsg buffer */
  TXtraceDns            traceDns;               /* tracedns setting */
};

void    htdns_putlong ARGS((dword l, u_char *msgp));
dword   htdns_getlong ARGS((CONST u_char *msgp));
epi_word  htdns_getshort ARGS((CONST u_char *msgp));
void    htdns_putshort ARGS((unsigned s, u_char *msgp));
int     htdns_incbuf ARGS((char **buf, size_t *sz, size_t inc,
                           TXPMBUF *pmbuf));
int     htdns_setbuf ARGS((char **buf, size_t *sz, size_t nsz,
                           TXPMBUF *pmbuf));

int  htdns_mkquery(HTDNS *dns, HTDNSOP op, const char *dname,
      HTDNSCLASS theclass, HTDNSTYPE type, const u_char *data, size_t datalen,
      const u_char *newrr_in, u_char *buf, size_t buflen, int qid);

void htdns_closeconn ARGS((HTDNS *dns));
int  htdns_startquery(HTDNS *dns, const char *name, HTDNSCLASS theclass,
                      HTDNSTYPE type);
const char *TXhtdnsReasonToGiveUp(HTDNS *dns);
int  htdns_querytillwait ARGS((HTDNS *dns));

TXbool htdns_startsearch(HTDNS *dns, HTDNSCLASS theclass);
int  htdns_searchtillwait ARGS((HTDNS *dns));

TXbool  TXhtdnsInitRes(TXPMBUF *pmbuf, TXtraceDns traceDns,
                       const char *resolvConfFile, struct __res_state *res,
                       TXsockaddr nameservers[HTDNS_MAXNS], HTDSSI *si);
void htdns_initservices(HTDSSI *si, int noenv, TXPMBUF *pmbuf,
                        TXtraceDns traceDns);

int  htdns_dn_comp ARGS((CONST char *exp_dn, u_char *comp_dn, size_t length,
                         u_char **dnptrs, u_char **lastdnptr));
int  htdns_dn_expand ARGS((CONST u_char *msg, CONST u_char *eomorig,
                         CONST u_char *comp_dn, char *exp_dn, size_t length));

extern char             DnsHostDb[];
extern HTDNS_SERVICE    DnsServices[];

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !HTDNSI_H */
