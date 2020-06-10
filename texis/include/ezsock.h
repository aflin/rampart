#if !defined(EZSOCK_H) || defined(EZSPROTOONLY)
#ifndef EZSOCK_H
#  define EZSOCK_H
#endif

#include "txcoreconfig.h"
#ifndef _WIN32
#  include <netinet/in.h>
#endif /* !_WIN32 */
#ifdef EPI_HAVE_SYS_SOCKET_H
#  include <sys/socket.h>                       /* SHUT_RDWR etc. */
#endif /* EPI_HAVE_SYS_SOCKET_H */
#ifndef SHUT_RD
#  define SHUT_RD       0
#endif /* !SHUT_RD */
#ifndef SHUT_WR
#  define SHUT_WR       1
#endif /* !SHUT_WR */
#ifndef SHUT_RDWR
#  define SHUT_RDWR     2
#endif /* !SHUT_RDWR */
#ifndef OS_H
#  include "os.h"       /* for CONST */
#endif /* !OS_H */
#ifndef MMSG_H
#  include "mmsg.h"                             /* for TXPMBUF * */
#endif /* !MMSG_H */
#include "txtypes.h"                            /* for TXbool */

/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/

/* HtTraceSkt: socket tracing messages.  Moved to ezsock.h from httpi.h
 * since some ezsock.c functions now do their own tracing.
 * Documented:
 * 0x00000001: after open()/close()/accept()/connect()
 * 0x00000002: after select()
 * 0x00000004: after read()
 * 0x00000008: after write()
 * 0x00000010: after ioctl()/getsockopt()
 * 0x00000020:
 * 0x00000040: after read() data
 * 0x00000080: after write()/ioctl() data
 * 0x00000100:       SSL certificate verification data (do not ask for cert)
 * 0x00010000: before open()/close()/accept()/connect()
 * 0x00020000: before select()
 * 0x00040000: before read()
 * 0x00080000: before write()
 * 0x00100000: before ioctl()/getsockopt()
 * 0x00200000:
 * 0x00400000: before read() data
 * 0x00800000: before write()/ioctl() data
 * 0x01000000:        ask for SSL peer cert (if not already)
 * 0x10000000:        datagram sockets too
 */
/* I(tok, afterVal) */
#define HTS_BEFOREABLE_SYMBOLS_LIST     \
  I(OPEN,           0x0001)             \
  I(SELECT,         0x0002)             \
  I(READ,           0x0004)             \
  I(WRITE,          0x0008)             \
  I(IOCTL,          0x0010)             \
  I(unused,         0x0020)             \
  I(READ_DATA,      0x0040)             \
  I(WRITE_DATA,     0x0080)

#define HTS_BEFORE(bits)        ((bits) << 16)

typedef enum TXtraceSkt_tag
  {
    HTS_NONE            = 0,
#undef I
#define I(tok, afterVal)        HTS_##tok = afterVal,
    HTS_BEFOREABLE_SYMBOLS_LIST
#undef I
#define I(tok, afterVal)        HTS_BEFORE_##tok = HTS_BEFORE(afterVal),
    HTS_BEFOREABLE_SYMBOLS_LIST
#undef I
    HTS_SSL_VERIFY      =     0x0100,
    HTS_DATAGRAM        = 0x10000000
  }
  TXtraceSkt;

#define HTS_MSG_READ_DATA       4
#define HTS_MSG_WRITE_DATA      8
#define HTS_TRACE_BEFORE_START_ARG(traceskt, bits)      \
  if ((traceskt) & ((bits) | HTS_BEFORE(bits)))         \
    {                                                   \
      if ((traceskt) & HTS_BEFORE(bits))                \
        {
#define HTS_TRACE_BEFORE_START(bits)                    \
  HTS_TRACE_BEFORE_START_ARG(HtTraceSkt, bits)
#define HTS_TRACE_BEFORE_END()                          \
        }                                               \
      htsTraceStart = TXgetTimeContinuousFixedRateOrOfDay();    \
      TXclearError();                                   \
    }
#define HTS_TRACE_AFTER_START_ARG(traceskt, bits)       \
  if ((traceskt) & (bits))                              \
    {                                                   \
      TX_PUSHERROR();                                   \
      htsTraceFinish = TXgetTimeContinuousFixedRateOrOfDay();   \
      htsTraceTime = htsTraceFinish - htsTraceStart;    \
      if (htsTraceTime < 0.0 && htsTraceTime > -0.001)  \
        htsTraceTime = 0.0;
/* ^-- Make sure diff is non-negative, i.e. in case finish > start slightly
 * but floating-point diff is very slightly negative:
 * KNG 20170824 that would hide clock skew; don't:
 * KNG 20170828 but such a float err happens often; compromise and hide if
 * negative and under 1ms (probably float err), since that is likely trace
 * message display resolution but still allows significant clock skew to show:
 * KNG 20171117 should no longer have clock skew on modern OSes due to
 * use of TXgetTimeContinuousFixedRate(), but still may have float err
 * NOTE: see also _traceDnsTime below
 */
#define HTS_TRACE_AFTER_END()   TX_POPERROR(); }
#define HTS_TRACE_AFTER_START(bits)                     \
  HTS_TRACE_AFTER_START_ARG(HtTraceSkt, bits)
#define HTS_TRACE_VARS  double htsTraceStart = -1.0,    \
    htsTraceFinish = -1.0, htsTraceTime = -1.0
#define HTS_TRACE_TIME()        htsTraceTime

extern double TXgetTimeContinuousFixedRateOrOfDay(void); /*wtf from texint.h*/
extern TXtraceSkt       HtTraceSkt;

#define TX_TRACEPIPE_TO_TRACESKT(tracepipe)                             \
  ((((tracepipe) & TPF_SELECT) ? HTS_SELECT : 0) |                      \
   (((tracepipe) & TPF_BEFORE(TPF_SELECT)) ? HTS_BEFORE(HTS_SELECT) : 0))

/* ------------------------------------------------------------------------ */

#define TX_TRACEDNS_BEFORE(bits)        ((bits) << 16)

typedef enum TXtraceDns_tag
  {
    TXtraceDns_None           = 0x0000,
    TXtraceDns_FileOpens      = 0x0001,         /* and environment vars */
    TXtraceDns_LinesRead      = 0x0002,         /* conf, hosts lines read */
    TXtraceDns_APICalls       = 0x0004,     /* Texis and internal API calls */
    /* Note that ..._Syscalls does not include sockets; covered by traceskt */
    TXtraceDns_Syscalls       = 0x0008,         /* get{addr,name}info() */
    TXtraceDns_DNSMessages    = 0x0010,
    TXtraceDns_Cache          = 0x0020,         /* e.g. in http.c */
                            /* 0x80000: */
    TXtraceDns_BeforeSyscalls = TX_TRACEDNS_BEFORE(TXtraceDns_Syscalls)
  }
  TXtraceDns;

#define TX_TRACEDNS_VARS                                \
  double _traceDnsStart = -1.0, _traceDnsFinish = -1.0, \
    _traceDnsTime = -1.0
#define TX_TRACEDNS_TIME()      _traceDnsTime

#define TX_TRACEDNS_BEFORE_BEGIN(traceDns, bits)        \
  if ((traceDns) & ((bits) | TX_TRACEDNS_BEFORE(bits))) \
    {                                                   \
      if ((traceDns) & TX_TRACEDNS_BEFORE(bits))        \
        {
#define TX_TRACEDNS_BEFORE_END()                        \
        }                                               \
      _traceDnsStart = TXgetTimeContinuousFixedRateOrOfDay();   \
      TXclearError();                                   \
    }
#define TX_TRACEDNS_AFTER_BEGIN(traceDns, bits)         \
  if ((traceDns) & (bits))                              \
    {                                                   \
      TX_PUSHERROR();
      /* call TX_TRACEDNS_AFTER_COMPUTE_TIME() if needed, here */
#define TX_TRACEDNS_AFTER_COMPUTE_TIME()                        \
      _traceDnsFinish = TXgetTimeContinuousFixedRateOrOfDay();  \
      _traceDnsTime = _traceDnsFinish - _traceDnsStart;         \
      if (_traceDnsTime < 0.0 && _traceDnsTime > -0.001)        \
        _traceDnsTime = 0.0;
/* ^-- See htsTraceTime comments above */
#define TX_TRACEDNS_AFTER_END() \
      TX_POPERROR();            \
    }

/* ------------------------------------------------------------------------ */

#ifdef _WIN32
TXbool TXinitsock(TXPMBUF *pmbuf);
typedef SOCKET  TXSOCKET;
#  define TXSOCKET_INVALID      INVALID_SOCKET
#else /* !_WIN32 */
#  define TXinitsock(pmbuf)     TXbool_True
typedef int     TXSOCKET;
#  define TXSOCKET_INVALID      (-1)
#endif /* !_WIN32 */

typedef enum TXaddrFamily_tag
  {
    TXaddrFamily_Unknown,                       /* should be first/0 */
    /* Giveing `Unspecified' is generally an error, except in a few
     * limited places like TXhostAndPortToSockaddrs() (e.g. where we
     * may not know the family of a string IP address):
     */
    TXaddrFamily_Unspecified,
    TXaddrFamily_IPv4,
    TXaddrFamily_IPv6,
    /* see TXaddrFamilyTo...() functions if this list changes */
  }
TXaddrFamily;

/* TX_OK_IPvN_DEFAULT are the default values for <urlcp ipprotocols>
 * etc., i.e. at a higher (potentially Happy Eyeballs) level than
 * TXaddrFamily.  We were going to leave IPv6 default off until Happy
 * Eyeballs implemented, to avoid getting IPv6 addrs when unreliable.
 * However: 1) even w/o Happy Eyeballs, our DNS looks up IPv4 first;
 * 2) a connectivity problem that Happy Eyeballs could work around
 * would be the network's fault, and could still be worked around by
 * disabling either IPv4 or IPv6; and 3) we do not wish to change the
 * default from IPv4 only in 8.0 to IPv4+IPv6 in 8.1 (w/Happy
 * Eyeballs) so soon after, especially as this would be a profile
 * default change too.  Thus, we enable IPv4+IPv6 by default:
 *
 * NOTE: if defaults change, update <urlcp ipprotocols> docs,
 * texis.ini.src [Scheduler] Listen (and monitor.c default):
 */
#define TX_OK_IPv4_DEFAULT      TXbool_True
#define TX_OK_IPv6_DEFAULT      TX_IPv6_ENABLED(TXApp)

#define TX_OK_IP_TO_ADDRFAMILY(okIPv4, okIPv6)                            \
  ((okIPv4) ? ((okIPv6) ? TXaddrFamily_Unspecified : TXaddrFamily_IPv4) : \
   ((okIPv6) ? TXaddrFamily_IPv6 : TXaddrFamily_Unknown))

/* TXsockaddr: Object for an IP address, or socket address (IP and
 * port).  Both use same object: 1) no IP-only <-> IP+port conversion
 * functions needed, and 2) there is already a Unix-standard
 * poly-AF-family socket address struct (sockaddr[_storage]) we can
 * use internally but no standard poly-AF-family IP-only struct.
 *
 * We use sockaddr_storage internally because it is defined to be big
 * enough (unlike sockaddr), and contains the AF family.  We wrap it
 * instead of using it directly because: 1) sockaddr_storage might not
 * be big enough in future; 2) might want our object opaque inside to
 * avoid dragging in socket.h etc. in other code; 3) might want to add
 * more info, like size, in future.
 *
 * Note that we always use TXsockaddr.storage in network-order
 * (unless legacy ...{Hardware|Network}Order() functions used).
 */
typedef struct TXsockaddr_tag
{
  struct sockaddr_storage       storage;
  /* An AF_INET6 in6addr_any address could also listen for IPv4, or
   * not (e.g. use a separate AF_INET inaddr_any socket); cannot tell
   * from AF alone, so add another field.  This maps to IPV6_V6ONLY
   * setsockopt().  Only relevant for in6addr_any.  Note that this is
   * typically false even for in6addr_any, as we typically set this
   * false for TXhostAndPortToSockaddrs() calls.
   *
   * Note that IPV6_V6ONLY (but not `okIPv4WithIPv6Any') is also used
   * for IPv4-mapped addresses; see TXezClientSocketNonBlocking():
   */
  TXbool                        okIPv4WithIPv6Any;
}
TXsockaddr;

/* We may parse `*:80' as AF_UNSPEC port 80 (to mean IPv4+IPv6),
 * so leave initialized but unparsed address as not AF_{UNSPEC,INET,INET6}:
 * using an initialized but otherwise unset address should be an
 * error.  If INADDR_ANY desired, it should be set explicitly
 * (e.g. via TYhostAndPortToSockaddrs()):
 */
#define TX_AF_UNKNOWN   (sa_family_t)65535 /* not AF_{UNSPEC,INET,INET6} */
#define TXsockaddrInit(sockaddr)                        \
  { memset((sockaddr), 0, sizeof(TXsockaddr));          \
    (sockaddr)->storage.ss_family = TX_AF_UNKNOWN;      \
    (sockaddr)->okIPv4WithIPv6Any = TXbool_False;       \
  }

#define TX_IPv4_MAX_STR_SZ      16              /* 255.255.255.255 */
/* ABCD:ABCD:ABCD:ABCD:ABCD:ABCD:192.168.158.190 */
#define TX_IPv6_MAX_STR_SZ      46
#define TX_IP_MAX_STR_SZ        TX_MAX(TX_IPv4_MAX_STR_SZ, TX_IPv6_MAX_STR_SZ)
/* +3 for `[' `]' `:', then +5 for port: */
#define TX_IPPORT_MAX_STR_SZ    (TX_IP_MAX_STR_SZ + 3 + 5)
/* enough for IP or sockaddr (IP + port): */
/* NOTE: if TX_SOCKADDR_MAX_STR_SZ changes update JDOM_VERSION */
#define TX_SOCKADDR_MAX_STR_SZ  TX_MAX(TX_IP_MAX_STR_SZ, TX_IPPORT_MAX_STR_SZ)

#define TX_IPv4_BYTE_SZ 4
#define TX_IPv6_BYTE_SZ 16
#define TX_IP_MAX_BYTE_SZ       TX_MAX(TX_IPv4_BYTE_SZ, TX_IPv6_BYTE_SZ)

/* Family/address/port translation functions: */
const char *TXAFFamilyToString(int afFamily);
int TXaddrFamilyToAFFamily(TXPMBUF *pmbuf, TXaddrFamily addrFamily);
TXaddrFamily TXAFFamilyToTXaddrFamily(TXPMBUF *pmbuf, int afFamily);
TXaddrFamily TXstringToTXaddrFamily(TXPMBUF *pmbuf, const char *s,
                                    const char *e);
const char *TXaddrFamilyToString(TXaddrFamily addrFamily);

int    TXsockaddrGetAFFamily(const TXsockaddr *sockaddr);
TXaddrFamily TXsockaddrGetTXaddrFamily(const TXsockaddr *sockaddr);
TXbool TXsockaddrToHost(TXPMBUF *pmbuf, TXbool suppressErrs,
                        TXtraceDns traceDns, const char *func,
                        const TXsockaddr *sockaddr, char *hostBuf,
                        size_t hostBufSz, TXbool dnsLookup);
TXbool TXsockaddrToStringIP(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                            char *ipBuf, size_t ipBufSz);
int    TXsockaddrGetPort(TXPMBUF *pmbuf, const TXsockaddr *sockaddr);
size_t TXsockaddrGetIPBytesAndLength(TXPMBUF *pmbuf,
                                    const TXsockaddr *sockaddr, byte **bytes);
TXbool TXsockaddrSetFamilyAndIPBytes(TXPMBUF *pmbuf, TXsockaddr *sockaddr,
                                     TXaddrFamily addrFamily,
                                     const byte *bytes, size_t bytesSz);
TXbool TXsockaddrSetPort(TXPMBUF *pmbuf, TXsockaddr *sockaddr, unsigned port);
TXbool TXsockaddrSetNetmask(TXPMBUF *pmbuf, TXsockaddr *sockaddr,
                            size_t netBits);
TXbool TXsockaddrToString(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                          char *addrBuf, size_t addrBufSz);
size_t TXhostAndPortToSockaddrs(TXPMBUF *pmbuf, TXbool suppressErrs,
        TXtraceDns traceDns, const char *func, TXaddrFamily addrFamily,
        const char *host, unsigned port, TXbool dnsLookup,
        TXbool okIPv4WithIPv6Any, TXsockaddr *sockaddrs, size_t numSockaddrs);
/* TXstringIPToSockaddr(): String IP to sockaddr should not block, so
 * no need to trace.  Still take family though; may want to require IPvN:
  */
#define TXstringIPToSockaddr(pmbuf, suppressErrs, traceDns, addrFamily, ip, okIPv4WithIPv6Any, sockaddr) \
  (TXhostAndPortToSockaddrs((pmbuf), (suppressErrs), (traceDns),        \
                            __FUNCTION__, (addrFamily), (ip), 0,        \
                TXbool_False, (okIPv4WithIPv6Any), (sockaddr), 1) == 1 ?  \
   TXbool_True : TXbool_False)
TXsockaddr *TXsockaddrNew(TXPMBUF *pmbuf);
TXsockaddr *TXsockaddrDup(TXPMBUF *pmbuf, const TXsockaddr *sockaddr);
TXsockaddr *TXsockaddrFree(TXsockaddr *sockaddr);

int TXsockaddrToIPv4(TXPMBUF *pmbuf, const TXsockaddr *sockaddrIn,
                     TXsockaddr *sockaddrOut);
int TXsockaddrToIPv6(TXPMBUF *pmbuf, const TXsockaddr *sockaddrIn,
                     TXsockaddr *sockaddrOut);

/* Legacy IPv4-only glue functions.  Code that uses these should be
 * upgraded to use TXsockaddr:
 */
TXbool TXsockaddrToInaddr(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                          struct in_addr *inaddr);
void TXinaddrAndPortToSockaddr(struct in_addr inaddr, unsigned port,
                               TXsockaddr *sockaddr);
TXbool TXsockaddrHardwareToNetworkOrder(TXPMBUF *pmbuf, TXsockaddr *sockaddr);
#define TXsockaddrNetworkToHardwareOrder TXsockaddrHardwareToNetworkOrder

/* Network mask functions: */
TXbool TXsockaddrNetContainsSockaddrNet(TXPMBUF *pmbuf,
                                   const TXsockaddr *sockaddrA, int netbitsA,
                                   const TXsockaddr *sockaddrB, int netbitsB,
                                   TXbool mapIPv4);
#define TXsockaddrIPsAreEqual(pmbuf, sockaddrA, sockaddrB, mappedIsIPv4) \
  TXsockaddrNetContainsSockaddrNet((pmbuf), (sockaddrA), -1, (sockaddrB), -1,\
                                   (mappedIsIPv4))
TXbool TXsockaddrIsIPv4Mapped(const TXsockaddr *sockaddr);

/* Server/client socket functions: */
int   TXezServerSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                       const TXsockaddr *sockaddr, int backlog);
#define TX_EZ_BACKLOG_DEFAULT   5
int   TXezServerSocketNonBlocking(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                                  const char *func, const TXsockaddr *sockaddr,
                                  const char *hostForMsgs, int linger,
                                  int backlog, TXbool keepalive);
TXbool TXezGetLocalSockaddr(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                            const char *func, int fd, TXsockaddr *sockaddr);
TXbool TXezGetRemoteSockaddr(TXPMBUF *pmbuf, int fd, TXsockaddr *sockaddr);
TXbool TXezGetIPProtocolsAvailable(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                            const char *func, TXbool *okIPv4, TXbool *okIPv6);
int    TXezClientSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                        const TXsockaddr *sockaddr, TXbool noConnRefusedMsg,
                        double timeout);
int    TXezClientSocketNonBlocking(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
               const char *func, const TXsockaddr *sockaddr,
               const char *hostForMsgs, TXbool noConnRefusedMsg, int linger);
int    TXezClientSocketDatagramNonBlocking(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                              const char *func, TXaddrFamily addrFamily);
int    TXezConnectSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                         int skt, const TXsockaddr *sockaddr,
                         const char *hostForMsgs, TXbool noConnRefusedMsg);
TXbool TXezShutdownSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                          const char *func, int skt, int how);
TXbool TXezCloseSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                       int fd);
int    TXezWaitForSocketReadability(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                                    const char *func, int fd, double timeout);
int    TXezWaitForSocketWritability(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                                    const char *func, int fd, double timeout);

char    *TXezSocketStrerror(void);

/* NOTE: if this enum changes see also HTFW in httpi.h: */
typedef enum EWM_tag            /* `stats' flags per socket */
{
  EWM_NONE      = 0,
  EWM_READ      = (1 << 0),
  EWM_WRITE     = (1 << 1),
  EWM_EXCEPTION = (1 << 2),
  EWM_ERR       = (1 << 3)
}
EWM;
#define EWMPN           ((EWM *)NULL)
#define EWM_RETMASK     (EWM_READ | EWM_WRITE | EWM_EXCEPTION | EWM_ERR)
int   TXezWaitForMultipleSockets(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                                 const char *func, const int *fds, EWM *stats,
                                 int num, double timeout, TXbool okIntr);
#ifdef _WIN32
int
TXezWaitForMultipleObjects(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                           const char *func,
          const HANDLE *handles, size_t numHandles, const char **handleNames,
          double timeout, TXbool alertable);
#endif /* _WIN32 */
extern int   ezsacceptnblk(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                           const char *func, int fd,
                           TXsockaddr *remoteSockaddr,
                           TXsockaddr *localSockaddr);
extern char *ezshostname  ARGS((void));
TXbool TXezGetMyIP(TXPMBUF *pmbuf, TXtraceDns traceDns,
                   TXaddrFamily addrFamily, TXsockaddr *sockaddr,
                   char *ipBuf, size_t ipBufSz);
extern int   ezspeek      ARGS((int fd,void *buf,int n));

typedef enum
  {
    TXezSR_IgnoreNoErrs,
    TXezSR_IgnoreConnReset,
    TXezSR_IgnoreAllErrs
  }
TXezSR;
EPI_SSIZE_T TXezSocketRead(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                           const char *func, int skt, const char *hostForMsgs,
                           void *buf, size_t bufSz, TXbool flushIt,
                           TXsockaddr *peer, TXezSR ignore);
extern int   ezswrite     ARGS((int fd,void *buf,int n));
EPI_SSIZE_T TXezSocketWrite(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                            const char *func, int skt, const char *hostForMsgs,
                            void *buf, size_t bufSz, TXbool flushIt,
                            const TXsockaddr *dest);
unsigned        TXezStringToPort(TXPMBUF *pmbuf, const char *service);
int TXezWaitForCall(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                    int sd, TXsockaddr *remoteAddr);
extern void ezuntrapdeath ARGS((void));
extern void eztrapdeath ARGS((void));
#ifdef _WIN32
extern int   ezsockpoll   ARGS((int onoff));
extern void  ezsintr      ARGS((void));
extern char *ezsockerr ARGS((int n));
extern int   ezsockerrno ARGS((int n));
#  ifndef EZSPROTOONLY
#    define      ezsfserve(a) (a)
#  endif /* EZSPROTOONLY */

#else /* !_WIN32 */
extern int   ezsfserve    ARGS((int dofork));
#  ifndef EZSPROTOONLY
#    define      ezsockpoll(f)   0
#    define      ezsintr()
#  endif /* EZSPROTOONLY */
#endif /* !_WIN32 */

#ifndef EZSPROTOONLY

#define INVALID_CONNECTION -1
#define VALID_CONNECTION 0

#define TCP_MODE_ALLOW	1
#define TCP_MODE_DENY	2

typedef struct TCP_ALLOW
{
	int	mode;	/* Allow or deny */
	struct	sockaddr_in	address;
	struct	sockaddr_in	mask;
	struct	TCP_ALLOW	*next;
} TCP_ALLOW;

/* wtf-Happy-Eyeballs: use sockaddr, rename to TX...?  use a Happy
 * Eyeballs wrapper?  only TX_DEBUG cpdb server in monitor, and
 * ncgserver, use these:
 */
int   ezsxserve(TXPMBUF *pmbuf, TXaddrFamily addrFamily, char *sname,
                int (*server)(int), char *cmd, void *allows);
#define ezsserve(pmbuf, fam, a,b)    ezsxserve((pmbuf), (fam), (a), (b), NULL, NULL)
#define ezscmdserve(pmbuf, fam, a,b) ezsxserve((pmbuf), (fam), (a), NULL, (b), NULL)


#define EZS_TCPIP   0
#define EZS_NETBIOS 1
#if defined(MSDOS) && defined(WANT_NETBIOS_JUNK)
extern int   ezsprotocol  ARGS((int proto));
extern int   ezsfndproto  ARGS((char *nm));
extern char *ezsnmproto   ARGS((int p));
#else                                                        /* MSDOS */
#define ezsprotocol(a) EZS_TCPIP
#define ezsfndproto(a) EZS_TCPIP
#define ezsnmproto(a)  "TCP/IP"
#endif                                                       /* MSDOS */
#endif                                                /* EZSPROTOONLY */
/**********************************************************************/
#endif                                                    /* EZSOCK_H */
