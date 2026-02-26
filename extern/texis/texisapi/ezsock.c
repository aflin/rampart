#ifdef _WIN32
#  include <ws2tcpip.h>                 /* wtf must be before windows.h */
#endif /* _WIN32 */
#include "txcoreconfig.h"
#include <time.h>
#include <sys/types.h>
#ifdef EPI_HAVE_IFADDRS_H
#  include <ifaddrs.h>
#  include <net/if.h>
#endif /* EPI_HAVE_IFADDRS_H */
#ifdef _WIN32
#  include <errno.h>
#  include <winsock.h>
#  include <io.h>                               /* for read(), write() */
/*#  include "fmsws.h"*/
#  ifndef ENOTEMPTY
#     define ENOTEMPTY               WSAENOTEMPTY
#  endif
#  ifndef EWOULDBLOCK
#     define EWOULDBLOCK             WSAEWOULDBLOCK
#  endif
#  ifndef EINPROGRESS
#     define EINPROGRESS             WSAEINPROGRESS
#  endif
#  ifndef ETIMEDOUT
#     define ETIMEDOUT               WSAETIMEDOUT
#  endif
#else /* !_WIN32 */
#  define ioctlsocket ioctl
#  ifndef macintosh
#    include <sys/param.h>
#    include <sys/time.h>
#  endif /* !macintosh */
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  ifndef linux
#    if !defined(hpux) && !defined(__bsdi__) && !defined(macintosh) && !defined(__FreeBSD__) && !defined(__MACH__) && !defined(__CYGWIN__)
#      include <sys/stropts.h>/* wtf */
#    endif
#  endif /* !linux */
#  ifdef linux
#    include <termios.h>
#  else /* !linux */
#    if defined(i386) || defined(__SVR4)
#      ifndef _SCO_C_DIALECT
#        include <sys/filio.h>                          /* for FIONBIO */
#      endif
#    endif
#  endif /* !linux */
#  ifdef u3b2
#    include <sys/in.h>
#    include <sys/inet.h>
#  else /* !u3b2 */
#    include <netinet/in.h>
#    include <sys/un.h>
#    ifndef macintosh
#      include <arpa/inet.h>
#    endif /* !macintosh */
#  endif /* !u3b2 */
#  ifdef _AIX
#    include <sys/select.h>
#  endif
#endif /* !_WIN32 */
#ifdef EPI_HAVE_NETDB_H
#  include <netdb.h>
#endif
#ifdef _SCO_C_DIALECT
   extern int h_errno;
#endif
#ifdef macintosh
#  include <GUSI.h>
#  include <sys/errno.h>
#endif /* macintosh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#  include <errno.h>
#endif /* !_WIN32 */
#include <signal.h>
#include "os.h"
#ifdef MEMDEBUG
#  undef getcwd         /* futzes Solaris unistd.h   KNG 970926 */
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifndef _WIN32
#  include <sys/wait.h>
#endif
#include "sizes.h"
#include "mmsg.h"
extern int epilocmsg ARGS((int f));
#include "ezsock.h"
#include "txtypes.h"
#include "texint.h"     /* for TXfork() */
#include "cgi.h"        /* for htsnpf() */
#include "http.h"       /* for HT_INET_... */
#include "txthreads.h"


#ifdef DEBUGSELECT
#define select(a,b,c,d,e) myselect(a,b,c,d,e,__FILE__,__LINE__)
#endif
#ifdef DEBUG
#  define DEBUGA(a) a
#else
#  define DEBUGA(a)
#endif
#ifndef ENAMETOOLONG                        /* MAW 05-31-94 for win16 */
#  define ENAMETOOLONG ENOENT
#endif
#ifndef HOSTENTPN
#  define HOSTENTPN	((struct hostent *)NULL)	/* KNG 960409 */
#endif

#define BITS_PER_BYTE   8

TXtraceSkt      HtTraceSkt = 0;
static const char       WhiteSpace[] = " \t\r\n\v\f";

/* Call TX_EZSOCK_CLEAR_ERROR() at start of every ez...() function,
 * so that TXezStrerror() afterwards does not inadvertently report
 * h_errno from an earlier call:
 */
#if defined(EPI_HAVE_H_ERRNO) && !defined(_WIN32)
#  define TX_EZSOCK_CLEAR_ERROR()       (TXclearError(), h_errno = 0)
#else /* !EPI_HAVE_H_ERRNO || _WIN32 */
/* Windows has h_errno too, but it is a macro for WSAGetLastError(),
 * which is an alias/wrapper for GetLastError(), so TXclearError()
 * covers it.  (And `h_errno = 0' would cause a compile error.)
 */
#  define TX_EZSOCK_CLEAR_ERROR()       TXclearError()
#endif /* !EPI_HAVE_H_ERRNO || _WIN32 */

static const char       Ques[] = "?";
static int childdied=0;           /* death of child status for server */

#ifdef _WIN32
static int ezsonnt=0;            /* are we running on real Windows NT */

static HWND ezswindow=NULL;
/**********************************************************************/
void
ezssetwindow(HWND w)
{
   ezswindow=w;
}
/**********************************************************************/
#endif                                                    /* _WIN32 */

#ifdef _WIN32
#  define BADSOCK(a) ((a)==(-1)||(a)==0xffff)
#  define BADRET(a)  ((a)==(-1)||(a)==0xffff)
#  define FORWINDOWS(a) a
/**********************************************************************/
#ifdef WINSOCKMANUAL          /* MAW 04-25-94 - load winsock manually */
#define NWSPROC 47
static union {
   FARPROC proc[NWSPROC];
   struct {
      SOCKET                (*PASCAL FAR paccept)(SOCKET s, struct sockaddr FAR *addr, int FAR *addrlen);
      int                   (*PASCAL FAR pbind)(SOCKET s, const struct sockaddr FAR *addr, int namelen);
      int                   (*PASCAL FAR pclosesocket)(SOCKET s);
      int                   (*PASCAL FAR pconnect)(SOCKET s, const struct sockaddr FAR *name, int namelen);
      int                   (*PASCAL FAR pioctlsocket)(SOCKET s, long cmd, u_long FAR *argp);
      int                   (*PASCAL FAR pgetpeername)(SOCKET s, struct sockaddr FAR *name, int FAR * namelen);
      int                   (*PASCAL FAR pgetsockname)(SOCKET s, struct sockaddr FAR *name, int FAR * namelen);
      int                   (*PASCAL FAR pgetsockopt)(SOCKET s, int level, int optname, char FAR * optval, int FAR *optlen);
      u_long                (*PASCAL FAR phtonl)(u_long hostlong);
      u_short               (*PASCAL FAR phtons)(u_short hostshort);
      unsigned long         (*PASCAL FAR pinet_addr)(const char FAR * cp);
      char FAR *            (*PASCAL FAR pinet_ntoa)(struct in_addr in);
      int                   (*PASCAL FAR plisten)(SOCKET s, int backlog);
      u_long                (*PASCAL FAR pntohl)(u_long netlong);
      u_short               (*PASCAL FAR pntohs)(u_short netshort);
      int                   (*PASCAL FAR precv)(SOCKET s, char FAR * buf, int len, int flags);
      int                   (*PASCAL FAR precvfrom)(SOCKET s, char FAR * buf, int len, int flags, struct sockaddr FAR *from, int FAR * fromlen);
      int                   (*PASCAL FAR pselect)(int nfds, fd_set FAR *readfds, fd_set FAR *writefds, fd_set FAR *exceptfds, const struct timeval FAR *timeout);
      int                   (*PASCAL FAR psend)(SOCKET s, const char FAR * buf, int len, int flags);
      int                   (*PASCAL FAR psendto)(SOCKET s, const char FAR * buf, int len, int flags, const struct sockaddr FAR *to, int tolen);
      int                   (*PASCAL FAR psetsockopt)(SOCKET s, int level, int optname, const char FAR * optval, int optlen);
      int                   (*PASCAL FAR pshutdown)(SOCKET s, int how);
      SOCKET                (*PASCAL FAR psocket)(int af, int type, int protocol);
      struct hostent  FAR * (*PASCAL FAR pgethostbyaddr)(const char FAR * addr, int len, int type);
      struct hostent  FAR * (*PASCAL FAR pgethostbyname)(const char FAR * name);
      int                   (*PASCAL FAR pgethostname)(char FAR * name, int namelen);
      struct servent  FAR * (*PASCAL FAR pgetservbyport)(int port, const char FAR * proto);
      struct servent  FAR * (*PASCAL FAR pgetservbyname)(const char FAR * name, const char FAR * proto);
      struct protoent FAR * (*PASCAL FAR pgetprotobynumber)(int proto);
      struct protoent FAR * (*PASCAL FAR pgetprotobyname)(const char FAR * name);
      int                   (*PASCAL FAR pWSAStartup)(WORD wVersionRequired, LPWSADATA lpWSAData);
      int                   (*PASCAL FAR pWSACleanup)(void);
      void                  (*PASCAL FAR pWSASetLastError)(int iError);
      int                   (*PASCAL FAR pWSAGetLastError)(void);
      BOOL                  (*PASCAL FAR pWSAIsBlocking)(void);
      int                   (*PASCAL FAR pWSAUnhookBlockingHook)(void);
      FARPROC               (*PASCAL FAR pWSASetBlockingHook)(FARPROC lpBlockFunc);
      int                   (*PASCAL FAR pWSACancelBlockingCall)(void);
      HANDLE                (*PASCAL FAR pWSAAsyncGetServByName)(HWND hWnd, u_int wMsg, const char FAR * name, const char FAR * proto, char FAR * buf, int buflen);
      HANDLE                (*PASCAL FAR pWSAAsyncGetServByPort)(HWND hWnd, u_int wMsg, int port, const char FAR * proto, char FAR * buf, int buflen);
      HANDLE                (*PASCAL FAR pWSAAsyncGetProtoByName)(HWND hWnd, u_int wMsg, const char FAR * name, char FAR * buf, int buflen);
      HANDLE                (*PASCAL FAR pWSAAsyncGetProtoByNumber)(HWND hWnd, u_int wMsg, int number, char FAR * buf, int buflen);
      HANDLE                (*PASCAL FAR pWSAAsyncGetHostByName)(HWND hWnd, u_int wMsg, const char FAR * name, char FAR * buf, int buflen);
      HANDLE                (*PASCAL FAR pWSAAsyncGetHostByAddr)(HWND hWnd, u_int wMsg, const char FAR * addr, int len, int type, const char FAR * buf, int buflen);
      int                   (*PASCAL FAR pWSACancelAsyncRequest)(HANDLE hAsyncTaskHandle);
      int                   (*PASCAL FAR pWSAAsyncSelect)(SOCKET s, HWND hWnd, u_int wMsg, long lEvent);
      int                   (*PASCAL FAR p__WSAFDIsSet)(SOCKET, fd_set FAR *);
   } wsproc;
} wsun;

#define accept                   (*wsun.wsproc.paccept                  )
#define bind                     (*wsun.wsproc.pbind                    )
#define closesocket              (*wsun.wsproc.pclosesocket             )
#define connect                  (*wsun.wsproc.pconnect                 )
#define ioctlsocket              (*wsun.wsproc.pioctlsocket             )
#define getpeername              (*wsun.wsproc.pgetpeername             )
#define getsockname              (*wsun.wsproc.pgetsockname             )
#define getsockopt               (*wsun.wsproc.pgetsockopt              )
#define htonl                    (*wsun.wsproc.phtonl                   )
#define htons                    (*wsun.wsproc.phtons                   )
#define inet_addr                (*wsun.wsproc.pinet_addr               )
#define inet_ntoa                (*wsun.wsproc.pinet_ntoa               )
#define listen                   (*wsun.wsproc.plisten                  )
#define ntohl                    (*wsun.wsproc.pntohl                   )
#define ntohs                    (*wsun.wsproc.pntohs                   )
#define recv                     (*wsun.wsproc.precv                    )
#define recvfrom                 (*wsun.wsproc.precvfrom                )
#define select                   (*wsun.wsproc.pselect                  )
#define send                     (*wsun.wsproc.psend                    )
#define sendto                   (*wsun.wsproc.psendto                  )
#define setsockopt               (*wsun.wsproc.psetsockopt              )
#define shutdown                 (*wsun.wsproc.pshutdown                )
#define socket                   (*wsun.wsproc.psocket                  )
#define gethostbyaddr            (*wsun.wsproc.pgethostbyaddr           )
#define gethostbyname            (*wsun.wsproc.pgethostbyname           )
#define gethostname              (*wsun.wsproc.pgethostname             )
#define getservbyport            (*wsun.wsproc.pgetservbyport           )
#define getservbyname            (*wsun.wsproc.pgetservbyname           )
#define getprotobynumber         (*wsun.wsproc.pgetprotobynumber        )
#define getprotobyname           (*wsun.wsproc.pgetprotobyname          )
#define WSAStartup               (*wsun.wsproc.pWSAStartup              )
#define WSACleanup               (*wsun.wsproc.pWSACleanup              )
#define WSASetLastError          (*wsun.wsproc.pWSASetLastError         )
#define WSAGetLastError          (*wsun.wsproc.pWSAGetLastError         )
#define WSAIsBlocking            (*wsun.wsproc.pWSAIsBlocking           )
#define WSAUnhookBlockingHook    (*wsun.wsproc.pWSAUnhookBlockingHook   )
#define WSASetBlockingHook       (*wsun.wsproc.pWSASetBlockingHook      )
#define WSACancelBlockingCall    (*wsun.wsproc.pWSACancelBlockingCall   )
#define WSAAsyncGetServByName    (*wsun.wsproc.pWSAAsyncGetServByName   )
#define WSAAsyncGetServByPort    (*wsun.wsproc.pWSAAsyncGetServByPort   )
#define WSAAsyncGetProtoByName   (*wsun.wsproc.pWSAAsyncGetProtoByName  )
#define WSAAsyncGetProtoByNumber (*wsun.wsproc.pWSAAsyncGetProtoByNumber)
#define WSAAsyncGetHostByName    (*wsun.wsproc.pWSAAsyncGetHostByName   )
#define WSAAsyncGetHostByAddr    (*wsun.wsproc.pWSAAsyncGetHostByAddr   )
#define WSACancelAsyncRequest    (*wsun.wsproc.pWSACancelAsyncRequest   )
#define WSAAsyncSelect           (*wsun.wsproc.pWSAAsyncSelect          )
#define __WSAFDIsSet             (*wsun.wsproc.p__WSAFDIsSet            )

static CONST char * CONST wsnam[NWSPROC]={
   (char *)  1 /*"ACCEPT"                  */,
   (char *)  2 /*"BIND"                    */,
   (char *)  3 /*"CLOSESOCKET"             */,
   (char *)  4 /*"CONNECT"                 */,
   (char *) 12 /*"IOCTLSOCKET"             */,
   (char *)  5 /*"GETPEERNAME"             */,
   (char *)  6 /*"GETSOCKNAME"             */,
   (char *)  7 /*"GETSOCKOPT"              */,
   (char *)  8 /*"HTONL"                   */,
   (char *)  9 /*"HTONS"                   */,
   (char *) 10 /*"INET_ADDR"               */,
   (char *) 11 /*"INET_NTOA"               */,
   (char *) 13 /*"LISTEN"                  */,
   (char *) 14 /*"NTOHL"                   */,
   (char *) 15 /*"NTOHS"                   */,
   (char *) 16 /*"RECV"                    */,
   (char *) 17 /*"RECVFROM"                */,
   (char *) 18 /*"SELECT"                  */,
   (char *) 19 /*"SEND"                    */,
   (char *) 20 /*"SENDTO"                  */,
   (char *) 21 /*"SETSOCKOPT"              */,
   (char *) 22 /*"SHUTDOWN"                */,
   (char *) 23 /*"SOCKET"                  */,
   (char *) 51 /*"GETHOSTBYADDR"           */,
   (char *) 52 /*"GETHOSTBYNAME"           */,
   (char *) 57 /*"GETHOSTNAME"             */,
   (char *) 56 /*"GETSERVBYPORT"           */,
   (char *) 55 /*"GETSERVBYNAME"           */,
   (char *) 54 /*"GETPROTOBYNUMBER"        */,
   (char *) 53 /*"GETPROTOBYNAME"          */,
   (char *)115 /*"WSASTARTUP"              */,
   (char *)116 /*"WSACLEANUP"              */,
   (char *)112 /*"WSASETLASTERROR"         */,
   (char *)111 /*"WSAGETLASTERROR"         */,
   (char *)114 /*"WSAISBLOCKING"           */,
   (char *)110 /*"WSAUNHOOKBLOCKINGHOOK"   */,
   (char *)109 /*"WSASETBLOCKINGHOOK"      */,
   (char *)113 /*"WSACANCELBLOCKINGCALL"   */,
   (char *)107 /*"WSAASYNCGETSERVBYNAME"   */,
   (char *)106 /*"WSAASYNCGETSERVBYPORT"   */,
   (char *)105 /*"WSAASYNCGETPROTOBYNAME"  */,
   (char *)104 /*"WSAASYNCGETPROTOBYNUMBER"*/,
   (char *)103 /*"WSAASYNCGETHOSTBYNAME"   */,
   (char *)102 /*"WSAASYNCGETHOSTBYADDR"   */,
   (char *)108 /*"WSACANCELASYNCREQUEST"   */,
   (char *)101 /*"WSAASYNCSELECT"          */,
   (char *)151 /*"__WSAFDISSET"            */,
};

static HINSTANCE wslib=(HINSTANCE)NULL;
/**********************************************************************/
static int
wsload(void)                  /* MAW 04-25-94 - load winsock manually */
{
static CONST char Fn[]="wsload";
int i;

   if(wslib!=(HINSTANCE)NULL) return(0);
   wslib=LoadLibrary("winsock.dll");
#ifdef _WIN32
   if(wslib==(HINSTANCE)NULL)
#else
   if(wslib<=HINSTANCE_ERROR)
#endif
   {
      putmsg(MERR+FOE,Fn,"Cannot load winsock.dll");
      wslib=(HINSTANCE)NULL;
      return(-1);
   }
   for(i=0;i<NWSPROC;i++){
      wsun.proc[i]=GetProcAddress(wslib,wsnam[i]);
      if(wsun.proc[i]==(FARPROC)NULL){
         putmsg(MERR+UGE,Fn,"Function %s missing from winsock.dll",wsnam[i]);
         FreeLibrary(wslib);
         wslib=(HINSTANCE)NULL;
         return(-1);
      }
   }
   return(0);
}
/**********************************************************************/

/**********************************************************************/
static void
wsunload(void)
{
   if(wslib!=(HINSTANCE)NULL){
      FreeLibrary(wslib);
      wslib=(HINSTANCE)NULL;
   }
}
/**********************************************************************/
#else                                                /* WINSOCKMANUAL */
#   define wsload() 0
#   define wsunload()
#endif                                               /* WINSOCKMANUAL */

/**********************************************************************/
struct {
   int n;                                     /* winsock error number */
   int en;                                /* roughly equivalent errno */
   char *s;                              /* winsock error description */
} wserr[]={  /* compiler will also xlate macros in strings to numbers */
   WSAEACCES         ,EACCES      ,"WSAEACCES - the requested address is a broadcast",
   WSAEADDRINUSE     ,EBUSY       ,"WSAEADDRINUSE - the specified address is already in use",
   WSAEADDRNOTAVAIL  ,ENODEV      ,"WSAEADDRNOTAVAIL - the specified address is not available",
   WSAEAFNOSUPPORT   ,ENODEV      ,"WSAEAFNOSUPPORT - the specified address family is not supported",
   WSAEALREADY       ,EINVAL      ,"WSAEALREADY - the asynchronous routine being canceled has already been completed",
   WSAEBADF          ,EBADF       ,"WSAEBADF - bad file number",
   WSAECONNABORTED   ,EPIPE       ,"WSAECONNABORTED - the connection was aborted",
   WSAECONNREFUSED   ,EPERM       ,"WSAECONNREFUSED - the attempt to connect was forcefully rejected",
   WSAECONNRESET     ,EPIPE       ,"WSAECONNRESET - the connection was reset by the remote side",
   WSAEDESTADDRREQ   ,EBADF       ,"WSAEDESTADDRREQ - a destination address is required",
   WSAEDQUOT         ,0           ,"WSAEDQUOT - WSAEDQUOT_",
   WSAEFAULT         ,EFAULT      ,"WSAEFAULT - invalid argument",
   WSAEHOSTDOWN      ,EAGAIN      ,"WSAEHOSTDOWN - host is down",
   WSAEHOSTUNREACH   ,ENOENT      ,"WSAEHOSTUNREAC - host is unreachable",
   WSAEINPROGRESS    ,EBUSY       ,"WSAEINPROGRESS - a blocking Windows Sockets operation is in progress",
   WSAEINTR          ,EINTR       ,"WSAEINTR - the (blocking) call was canceled via WSACancelBlockingCall()",
   WSAEINVAL         ,EINVAL      ,"WSAEINVAL - bad argument/usage",
   WSAEISCONN        ,EEXIST      ,"WSAEISCONN - the socket is already connected",
   WSAELOOP          ,EINVAL      ,"WSAELOOP - link loop",
   WSAEMFILE         ,EMFILE      ,"WSAEMFILE - no more file descriptors are available",
   WSAEMSGSIZE       ,ENOMEM      ,"WSAEMSGSIZE - the datagram was too large to fit into the specified buffer and was truncated",
   WSAENAMETOOLONG   ,ENAMETOOLONG,"WSAENAMETOOLONG - name is too long",
   WSAENETDOWN       ,EIO         ,"WSAENETDOWN - the Windows Sockets implementation has detected that the network subsystem has failed",
   WSAENETRESET      ,EPIPE       ,"WSAENETRESET - the Windows Sockets implementation has dropped the connection",
   WSAENETUNREACH    ,ENXIO       ,"WSAENETUNREACH - the network cannot be reached from this host at this time",
   WSAENOBUFS        ,ENOMEM      ,"WSAENOBUFS - no buffer space is available",
   WSAENOPROTOOPT    ,EINVAL      ,"WSAENOPROTOOPT - the option is unknown or unsupported",
   WSAENOTCONN       ,EBADF       ,"WSAENOTCONN - the socket is not connected",
   WSAENOTEMPTY      ,ENOTEMPTY   ,"WSAENOTEMPTY - directory not empty",
   WSAENOTSOCK       ,EBADF       ,"WSAENOTSOCK - the descriptor is not a socket",
   WSAEOPNOTSUPP     ,EBADF       ,"WSAEOPNOTSUPP - the referenced socket is not a type that supports connection-oriented service",
   WSAEPFNOSUPPORT   ,ENODEV      ,"WSAEPFNOSUPPORT - protocol family not supported",
   WSAEPROCLIM       ,0           ,"WSAEPROCLIM - WSAEPROCLIM_",
   WSAEPROTONOSUPPORT,ENODEV      ,"WSAEPROTONOSUPPORT - the specified protocol is not supported",
   WSAEPROTOTYPE     ,EINVAL      ,"WSAEPROTOTYPE - the specified protocol is the wrong type for this socket",
   WSAEREMOTE        ,0           ,"WSAEREMOTE - WSAEREMOTE_",
   WSAESHUTDOWN      ,EBADF       ,"WSAESHUTDOWN - the socket has been shutdown",
   WSAESOCKTNOSUPPORT,ENODEV      ,"WSAESOCKTNOSUPPORT - the specified socket type is not supported in this address family",
   WSAESTALE         ,EBADF       ,"WSAESTALE - stale handle",
   WSAETIMEDOUT      ,EAGAIN      ,"WSAETIMEDOUT - attempt to connect timed out without establishing a connection",
   WSAETOOMANYREFS   ,EMFILE      ,"WSAETOOMANYREFS - too many references",
   WSAEUSERS         ,EMFILE      ,"WSAEUSERS - too many users",
   WSAEWOULDBLOCK    ,EWOULDBLOCK ,"WSAEWOULDBLOCK - operation would block",
   WSANOTINITIALISED ,EINVAL      ,"WSANOTINITIALISED - a successful WSAStartup() must occur before using this API",
   WSANO_DATA        ,ENOENT      ,"WSANO_DATA - No Data of requested type",
};
#define nwserr (sizeof(wserr)/sizeof(wserr[0]))

#ifdef _WIN32
#ifndef ETIMEDOUT
#  define ETIMEDOUT WSAETIMEDOUT
#endif
#ifndef ENETRESET
#  define ENETRESET WSAENETRESET
#endif
#ifndef ECONNRESET
#  define ECONNRESET WSAECONNRESET
#endif
#ifndef ECONNABORTED
#  define ECONNABORTED WSAECONNABORTED
#endif
#ifndef ECONNREFUSED
#  define ECONNREFUSED WSAECONNREFUSED
#endif
#endif

/**********************************************************************/
char *
ezsockerr(int n)
{
int i;
static CONST char unkfmt[]="%d - unknown winsock error";
static char unk[]="099999 - unknown winsock error";

   if(n<0) n=WSAGetLastError();
   for(i=0;i<nwserr;i++){
      if(wserr[i].n==n) return(wserr[i].s);
   }
   sprintf(unk,unkfmt,n);
   return(unk);
}
/**********************************************************************/
int
ezsockerrno(int n)
{
int i;

   if(n<0) n=WSAGetLastError();
   for(i=0;i<nwserr;i++){
      if(wserr[i].n==n) return(wserr[i].en);
   }
   return(0);
}
#define perror(a) {int e=errno;int o=epilocmsg(1);epiputmsg(MERR,__FUNCTION__,"%s: %s",a,ezsockerr(-1));epilocmsg(o);errno=e;}
static TXATOMINT nopen = 0;
/**********************************************************************/
#ifdef _WIN32
static BOOL
defaultblockinghook(void) /* MAW 03-08-94 - from winsock.txt - for nt */
{
MSG msg;
BOOL ret;
                                       /* get the next message if any */
   ret=(BOOL)PeekMessage(&msg,NULL,0,0,PM_REMOVE);
   if(ret){                              /* if we got one, process it */
      TranslateMessage(&msg);
      DispatchMessage(&msg);
   }
   return(ret);                           /* TRUE if we got a message */
}
#endif                                                   /* _WIN32 */
/**********************************************************************/

static TXbool initsock(TXPMBUF *pmbuf);

TXbool TXinitsock(TXPMBUF *pmbuf){return initsock(pmbuf);}

static TXbool
initsock(TXPMBUF *pmbuf)        /* stolen from winsock.txt */
{
 WORD wVersionRequested;
 WSADATA wsaData;
 int err;
#ifndef MAKEWORD
#  define MAKEWORD(a,b) (((a)<<8)|(b))
#endif
 static TXATOMINT       didInit = 0;            /* Bug 5251 */

 if (TX_ATOMIC_COMPARE_AND_SWAP(&didInit, 0, 1) == 1)
     {                                          /* already init'd */
       (void)TX_ATOMIC_INC(&nopen);
       return(TXbool_True);
     }
#ifdef _WIN32
     if(HIWORD(GetVersion())&0x8000) ezsonnt=0;
     else                            ezsonnt=1;
#else
     ezsonnt=0;
#endif
 if(wsload()!=0)
   {
     txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "wsload() failed");
     return(TXbool_False);/* MAW 04-25-94 - load winsock */
   }
 wVersionRequested = MAKEWORD( 1, 1 );
 err = WSAStartup( wVersionRequested, &wsaData );
 if ( err != 0 ) {

        /* Tell the user that we couldn't find a useable winsock.dll. */
   txpmbuf_putmsg(pmbuf, MERR+UGE, __FUNCTION__,
               "No valid winsock.dll - WSAStartup()=%s",
               ezsockerr(err));
     return(TXbool_False);
 }
 /* Confirm that the Windows Sockets DLL supports 1.1.*/
 /* Note that if the DLL supports versions greater    */
 /* than 1.1 in addition to 1.1, it will still return */
 /* 1.1 in wVersion since that is the version we      */
 /* requested.                                        */
 if ( LOBYTE( wsaData.wVersion ) != 1 ||
      HIBYTE( wsaData.wVersion ) != 1 ) {
        /* Tell the user that we couldn't find a useable winsock.dll. */
     WSACleanup();
     txpmbuf_putmsg(pmbuf, MERR+UGE, __FUNCTION__,
                    "Cannot use WinSock: Wrong version 0x%x",
               (unsigned)wsaData.wVersion);
     return(TXbool_False);
 }
#ifndef NEVER
 WSASetBlockingHook((FARPROC)defaultblockinghook);/* MAW 03-08-94 for nt */
#endif
 (void)TX_ATOMIC_INC(&nopen);
#ifdef DEBUG
    fprintf(stderr,"WSAStartup() ok\n");
    fprintf(stderr,"   wVersion      =%u\n"  ,(unsigned int)wsaData.wVersion);
    fprintf(stderr,"   wHighVersion  =%u\n"  ,(unsigned int)wsaData.wHighVersion);
    fprintf(stderr,"   szDescription =%.*s\n",WSADESCRIPTION_LEN+1,wsaData.szDescription);
    fprintf(stderr,"   szSystemStatus=%.*s\n",WSASYS_STATUS_LEN+1 ,wsaData.szSystemStatus);
    fprintf(stderr,"   iMaxSockets   =%u\n"  ,wsaData.iMaxSockets);
    fprintf(stderr,"   iMaxUdpDg     =%u\n"  ,wsaData.iMaxUdpDg);
    fprintf(stderr,"   lpVendorInfo  =%p\n"  ,wsaData.lpVendorInfo);
#endif
 return(TXbool_True);   /* The Windows Sockets DLL is acceptable. */
}
/**********************************************************************/
static void
deinitsock(void)
{
  TXATOMINT     rc;

  rc = TX_ATOMIC_DEC(&nopen);
  if (rc <= 0)                                  /* unpaired deinit */
    (void)TX_ATOMIC_INC(&nopen);
  else if (rc == 1)                             /* last one out */
    {
# if 0
        /* Bug 5251: getting the number of open sockets wrong causes
         * premature calling of WSACleanup()/wsunload()/etc. here,
         * which can cause later socket calls to fail.  In modern
         * times there should be no cost to keeping WinSock loaded,
         * so do not unload, in case other sockets open unknown to us.
         * See also shortcircuit in initsock().
         */
#        ifdef _WIN32
            WSAUnhookBlockingHook();           /* MAW 03-08-94 for nt */
#        endif
         WSACleanup();
         wsunload();                 /* MAW 04-25-94 - unload winsock */
#endif /* 0 */
   }
}
/**********************************************************************/

/**********************************************************************/
static SOCKET PASCAL FAR
TXacceptWrapper(SOCKET s, struct sockaddr FAR *addr, EPI_ACCEPT_LEN_TYPE FAR *addrlen)
{
SOCKET sd;

   sd=accept(s,addr,addrlen);
   if(BADSOCK(sd)){
      errno=ezsockerrno(-1);
   }else{
     (void)TX_ATOMIC_INC(&nopen);
   }
   return(sd);
}
/**********************************************************************/
#ifdef accept
#  undef accept
#endif
#define accept(a,b,c) TXacceptWrapper(a,b,c)

static int usepoll=0;
/**********************************************************************/
int
ezsockpoll(f)
int f;
{
int of=usepoll;

   if(f) usepoll++;
   else if(usepoll>0) usepoll--;
   return(of);
}
/**********************************************************************/
void
ezsintr(void)                       /* interrupt whatever is going on */
{
   WSACancelBlockingCall();
}
/**********************************************************************/
#  define FIXERRNO() (errno=ezsockerrno(-1))
#else                                                     /* !_WIN32 */
#  define BADSOCK(a) ((a) == TXSOCKET_INVALID)
#  define BADRET(a)  ((a)==(-1))
#  define FORWINDOWS(a)
#  define perror(a) {int e=errno;int o=epilocmsg(1);epiputmsg(MERR,__FUNCTION__,"%s: %s",a,strerror(errno));epilocmsg(o);errno=e;}
#  ifdef macintosh
static TXbool
initsock(TXPMBUF *pmbuf)        /* stolen from winsock.txt */
{
	 static int	done = 0;
	 if (done)
		return 1;
     GUSISetup(GUSIwithInternetSockets);
     done++;
     return 1;
}
#  else
#    define initsock(pmbuf)     TXbool_True
#  endif
#  define initnb() 1
#  define deinitsock()
#  define usepoll 0
#  define FIXERRNO()
#endif                                                    /* !_WIN32 */

#ifndef MAXHOSTNAMELEN
#  define MAXHOSTNAMELEN 256
#endif
#ifndef NOSY
#  define NOSY 0
#endif
#ifndef PROTOCOL
#  define PROTOCOL "tcp"
#endif
#ifndef LINGERTIME
#  define LINGERTIME 60
#endif

#ifndef bzero
#define bzero(a,b) memset(a,0,b) /* JMT - 96-04-05 Fixed order */
#endif
#ifndef bcopy
#define bcopy(a,b,c) memcpy(b,a,c)
#endif

#ifndef FD_SET    /* for 4.2BSD */
#define FD_SETSIZE      (sizeof(fd_set) * 8)
#define FD_SET(n, p)    (((fd_set *) (p))->fds_bits[0] |= (1 << ((n) % 32)))
#define FD_CLR(n, p)    (((fd_set *) (p))->fds_bits[0] &= ~(1 << ((n) % 32)))
#define FD_ISSET(n, p)  (((fd_set *) (p))->fds_bits[0] & (1 << ((n) % 32)))
#define FD_ZERO(p)      bzero((char *)(p), sizeof(*(p)))
#endif
#define ZEROSTRUCT(s)   memset((char *)&(s), 0, sizeof(s))

static const char       IPv6Unsupported[] = "IPv6 currently unsupported";
static const char       UnknownTXfamilyFmt[] = "Unknown TXaddrFamily %d";
static const char       BufTooSmallFmt[] =
  "Buffer size %d too small: Expected at least %d bytes";

static TXbool
TXezMergeFuncs(const char *func1,
               const char *func2,       /* opt. */
              char *mergeBuf, size_t mergeBufSz)
/* Merges two C function names `func1' and `func2' into `mergeBuf',
 * suitable for reporting in putmsg() as the function.
 * Returns true if `mergeBuf' is large enough, false if not
 * (but still merges, with `...' appended if possible).
 */
{
  size_t        totLen;

  /* The phraseology here should work for stock putmsg() handlers that
   * say `<msg> in the function <func>', as well as some scripts that
   * do `(<func>) <msg>'.  It should also work when compounded,
   * i.e. this return value may get appended to another __FUNCTION__.
   */
  if (func2)
    totLen = htsnpf(mergeBuf, mergeBufSz, "%s for %s", func1, func2);
  else
    totLen = htsnpf(mergeBuf, mergeBufSz, "%s", func1);
  if (totLen >= mergeBufSz)
    {
      if (mergeBufSz >= 4)
        TXstrncpy(mergeBuf + mergeBufSz - 4, "...", mergeBufSz - 4);
      return(TXbool_False);
    }
  return(TXbool_True);
}

#define MERGE_FUNC_VARS char    mergeFuncBuf[256]
#define MERGE_FUNC(func)                                                \
  ((func) ? (TXezMergeFuncs(__FUNCTION__, (func), mergeFuncBuf,         \
                          sizeof(mergeFuncBuf)), mergeFuncBuf) : __FUNCTION__)

static TXbool
TXezOkSockToInt(TXPMBUF *pmbuf, const char *func, TXSOCKET skt)
/* Returns true if `skt' may safely be cast to int, false if not (and
 * yaps, and sets ERANGE).  Only a concern in Windows, where TXSOCKET
 * != int.
 */
{
  int   intSkt = (int)skt;

  if ((TXSOCKET)intSkt != skt)
    {
      txpmbuf_putmsg(pmbuf, MERR, func,
                     "Cannot use socket value %wd: Out-of-range for integer",
                     (EPI_HUGEINT)skt);
      TXclearError();
      errno = ERANGE;
#ifdef _WIN32
      TXseterror(ERROR_INVALID_HANDLE);  /* wtf WAG; what is win32 ERANGE? */
#endif /* _WIN32 */
      return(TXbool_False);
    }
  return(TXbool_True);
}

/**********************************************************************/
TXbool
TXezCloseSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                int fd)
{
  int rc;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

#ifdef _WIN32
  /* close() of a socket in Windows is undefined behavior; use closesocket() */
#  define CLOSE_SOCKET_FUNC     closesocket
#else /* !_WIN32 */
#  define CLOSE_SOCKET_FUNC     close
#endif /* !_WIN32 */

  if (fd < 0) return(TXbool_True);

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
            TX_STRINGIZE_VALUE_OF(CLOSE_SOCKET_FUNC) "(skt %d) starting", fd);
  HTS_TRACE_BEFORE_END()
 rc = CLOSE_SOCKET_FUNC(fd);
 FIXERRNO();
 HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
   txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                  TX_STRINGIZE_VALUE_OF(CLOSE_SOCKET_FUNC) "(skt %d): %1.3lf sec returned %d=%s err %d=%s",
                  fd, HTS_TRACE_TIME(), rc, (rc == 0 ? "ok" : "failed"),
                  (int)saveErr, TXgetOsErrName(saveErr, Ques));
 HTS_TRACE_AFTER_END()

 deinitsock();
 return(rc == 0);
#undef CLOSE_SOCKET_FUNC
}
/**********************************************************************/

const char *
TXAFFamilyToString(int afFamily)
{
  switch (afFamily)
    {
#ifdef AF_UNSPEC
    case AF_UNSPEC: return("AF_UNSPEC");
#endif /* AF_UNSPEC */
#ifdef AF_UNIX
    case AF_UNIX: return("AF_UNIX");
#endif /* AF_UNIX */
#ifdef AF_INET
    case AF_INET: return("AF_INET");
#endif /* AF_INET */
#ifdef AF_AX25
    case AF_AX25: return("AF_AX25");
#endif /* AF_AX25 */
#ifdef AF_IPX
    case AF_IPX: return("AF_IPX");
#endif /* AF_IPX */
#ifdef AF_APPLETALK
    case AF_APPLETALK: return("AF_APPLETALK");
#endif /* AF_APPLETALK */
#ifdef AF_NETROM
    case AF_NETROM: return("AF_NETROM");
#endif /* AF_NETROM */
#ifdef AF_BRIDGE
    case AF_BRIDGE: return("AF_BRIDGE");
#endif /* AF_BRIDGE */
#ifdef AF_ATMPVC
    case AF_ATMPVC: return("AF_ATMPVC");
#endif /* AF_ATMPVC */
#ifdef AF_X25
    case AF_X25: return("AF_X25");
#endif /* AF_X25 */
#ifdef AF_INET6
    case AF_INET6: return("AF_INET6");
#endif /* AF_INET6 */
#ifdef AF_ROSE
    case AF_ROSE: return("AF_ROSE");
#endif /* AF_ROSE */
#ifdef AF_DECnet
    case AF_DECnet: return("AF_DECnet");
#endif /* AF_DECnet */
#ifdef AF_NETBEUI
    case AF_NETBEUI: return("AF_NETBEUI");
#endif /* AF_NETBEUI */
#ifdef AF_SECURITY
    case AF_SECURITY: return("AF_SECURITY");
#endif /* AF_SECURITY */
#ifdef AF_KEY
    case AF_KEY: return("AF_KEY");
#endif /* AF_KEY */
#ifdef AF_ROUTE
    case AF_ROUTE: return("AF_ROUTE");
#endif /* AF_ROUTE */
#ifdef AF_PACKET
    case AF_PACKET: return("AF_PACKET");
#endif /* AF_PACKET */
#ifdef AF_ASH
    case AF_ASH: return("AF_ASH");
#endif /* AF_ASH */
#ifdef AF_ECONET
    case AF_ECONET: return("AF_ECONET");
#endif /* AF_ECONET */
#ifdef AF_ATMSVC
    case AF_ATMSVC: return("AF_ATMSVC");
#endif /* AF_ATMSVC */
#ifdef AF_RDS
    case AF_RDS: return("AF_RDS");
#endif /* AF_RDS */
#ifdef AF_SNA
    case AF_SNA: return("AF_SNA");
#endif /* AF_SNA */
#ifdef AF_IRDA
    case AF_IRDA: return("AF_IRDA");
#endif /* AF_IRDA */
#ifdef AF_PPPOX
    case AF_PPPOX: return("AF_PPPOX");
#endif /* AF_PPPOX */
#ifdef AF_WANPIPE
    case AF_WANPIPE: return("AF_WANPIPE");
#endif /* AF_WANPIPE */
#ifdef AF_LLC
    case AF_LLC: return("AF_LLC");
#endif /* AF_LLC */
#ifdef AF_CAN
    case AF_CAN: return("AF_CAN");
#endif /* AF_CAN */
#ifdef AF_TIPC
    case AF_TIPC: return("AF_TIPC");
#endif /* AF_TIPC */
#ifdef AF_BLUETOOTH
    case AF_BLUETOOTH: return("AF_BLUETOOTH");
#endif /* AF_BLUETOOTH */
#ifdef AF_IUCV
    case AF_IUCV: return("AF_IUCV");
#endif /* AF_IUCV */
#ifdef AF_RXRPC
    case AF_RXRPC: return("AF_RXRPC");
#endif /* AF_RXRPC */
#ifdef AF_ISDN
    case AF_ISDN: return("AF_ISDN");
#endif /* AF_ISDN */
#ifdef AF_PHONET
    case AF_PHONET: return("AF_PHONET");
#endif /* AF_PHONET */
#ifdef AF_IEEE802154
    case AF_IEEE802154: return("AF_IEEE802154");
#endif /* AF_IEEE802154 */
#ifdef AF_MAX
    case AF_MAX: return("AF_MAX");
#endif /* AF_MAX */
    default:
      return("?");
    }
}

int
TXaddrFamilyToAFFamily(TXPMBUF *pmbuf, TXaddrFamily addrFamily)
/* Returns AF_... value for `addrFamily', or -1 on error.
 */
{
  int   af;

  if (!TX_IPv6_ENABLED(TXApp))
    {
      switch (addrFamily)
        {
        case TXaddrFamily_IPv6:
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__, IPv6Unsupported);
          goto err;
        case TXaddrFamily_Unspecified:
          /* Map ...Unspecified to IPv4, to avoid inadvertent backdoor
           * to IPv6 while we do not support it:
           */
          addrFamily = TXaddrFamily_IPv4;
          break;
        default:
          break;
        }
    }

  switch (addrFamily)
    {
    case TXaddrFamily_Unspecified:      af = AF_UNSPEC; break;
    case TXaddrFamily_IPv4:             af = AF_INET;   break;
    case TXaddrFamily_IPv6:             af = AF_INET6;  break;
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__, UnknownTXfamilyFmt,
                     (int)addrFamily);
      goto err;
    }
  goto finally;

err:
  af = -1;
finally:
  return(af);
}

static void
TXreportBadAFFamily(TXPMBUF *pmbuf, const char *func, int afFamily)
{
  if (afFamily == AF_UNSPEC)
    {
      /* Common occurence; typically an error from earlier function.
       * Clarify:
       */
      txpmbuf_putmsg(pmbuf, MERR + UGE, func,
                     "Unspecified AF family in IP address");
    }
  else
    txpmbuf_putmsg(pmbuf, MERR + UGE, func,
             "Unknown or unsupported AF address family %d (%s) in IP address",
                   afFamily, TXAFFamilyToString(afFamily));
}

TXaddrFamily
TXAFFamilyToTXaddrFamily(TXPMBUF *pmbuf, int afFamily)
{
  TXaddrFamily  ret;

  switch (afFamily)
    {
    case AF_UNSPEC:     ret = TXaddrFamily_Unspecified; break;
    case AF_INET:       ret = TXaddrFamily_IPv4;        break;
    case AF_INET6:      ret = TXaddrFamily_IPv6;        break;
    default:
      TXreportBadAFFamily(pmbuf, __FUNCTION__, afFamily);
      goto err;
    }
  goto finally;

err:
  ret = TXaddrFamily_Unknown;
finally:
  return(ret);
}

TXaddrFamily
TXstringToTXaddrFamily(TXPMBUF *pmbuf, const char *s, const char *e)
{
  if (!e) e = s + strlen(s);

  if (e - s == 11 && strnicmp(s, "unspecified", 11) == 0)
    return(TXaddrFamily_Unspecified);
  if (e - s == 4 && strnicmp(s, "IPv4", 4) == 0)
    return(TXaddrFamily_IPv4);
  if (e - s == 4 && strnicmp(s, "IPv6", 4) == 0)
    return(TXaddrFamily_IPv6);

  txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                 /* We say `IP protocol' not `IP address family' here:
                  * since we are parsing a string, it is likely to
                  * come from user via ipprotocols setting, so
                  * `protocol' is more expected than `address family':
                  */
                 "Unknown IP protocol `%.*s'", (int)(e - s), s);
  return(TXaddrFamily_Unknown);
}

const char *
TXaddrFamilyToString(TXaddrFamily addrFamily)
{
  switch (addrFamily)
    {
    case TXaddrFamily_Unspecified:      return("unspecified");
    case TXaddrFamily_IPv4:             return("IPv4");
    case TXaddrFamily_IPv6:             return("IPv6");
    default:                            return("unknown");
    }
}

int
TXsockaddrGetAFFamily(const TXsockaddr *sockaddr)
{
  return(sockaddr->storage.ss_family);
}

TXaddrFamily
TXsockaddrGetTXaddrFamily(const TXsockaddr *sockaddr)
{
  return(TXAFFamilyToTXaddrFamily(TXPMBUF_SUPPRESS,
                                  TXsockaddrGetAFFamily(sockaddr)));
}

static EPI_SOCKLEN_T
TXsockaddrGetSockaddrSize(const TXsockaddr *sockaddr)
/* Returns size of sockaddr->storage to use for connect() etc.
 * For Bug 7945.
 */
{
  EPI_SOCKLEN_T sz;

  switch (sockaddr->storage.ss_family)
    {
    case AF_INET:
      sz = sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      sz = sizeof(struct sockaddr_in6);
      break;
#ifndef _WIN32
      /* Windows does not seem to have sockaddr_un.  But maybe soon:
       * https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/
       */
    case AF_UNIX:
      sz = sizeof(struct sockaddr_un);
      break;
#endif /* !_WIN32 */
    default:
      /* Fallback.  sizeof(struct sockaddr_storage) works for
       * AF_INET[6] on Linux/Unix, so try it for unknown here.
       * May still fail at connect() etc., but that is probably no
       * worse than failing here, and maybe it will work.
       */
      sz = sizeof(struct sockaddr_storage);
      break;
    }
  return(sz);
}

TXbool
TXsockaddrToHost(TXPMBUF *pmbuf, TXbool suppressErrs, TXtraceDns traceDns,
                 const char *func, const TXsockaddr *sockaddr, char *hostBuf,
                 size_t hostBufSz, TXbool dnsLookup)
/* Thread-safe and IPv4/IPv6-compatible version of inet_ntoa() and
 * gethostbyaddr().  Writes hostname for `*sockaddr', or string
 * representation of IP address of `*sockaddr' if `dnsLookup' false,
 * to `hostBuf'; or `?' on error.  `hostBufSz' should be at least
 * TX_SOCKADDR_MAX_STR_SZ for IP addresses.
 * `suppressErrs' provides a way to suppress errors without also
 * suppressing trace messages (which also contain system errors).
 * Returns false on error (with `hostBuf' set to `?' if room).
 */
{
  int           res;
  TXbool        ret;
  TX_TRACEDNS_VARS;
  MERGE_FUNC_VARS;
  char          ipBuf[TX_SOCKADDR_MAX_STR_SZ];

  TX_TRACEDNS_BEFORE_BEGIN(traceDns, TXtraceDns_Syscalls)
    /* Turn off tracing to avoid infinite recursion: */
    TXsockaddrToHost(TXPMBUF_SUPPRESS, TXbool_True, TXtraceDns_None,
                     MERGE_FUNC(func), sockaddr, ipBuf, sizeof(ipBuf),
                     TXbool_False);
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   "getnameinfo(%s, ..., s) starting",
                   ipBuf, (dnsLookup ? "(NI)0" : "NI_NUMERICHOST"));
  TX_TRACEDNS_BEFORE_END()
  /* Yay for getnameinfo(): */
  res = getnameinfo((const struct sockaddr *)&sockaddr->storage,
                    (EPI_SOCKLEN_T)sizeof(sockaddr->storage), hostBuf,
#ifdef _WIN32
                    (DWORD)hostBufSz,           /* wtf check bounds */
#else /* !_WIN32 */
                    hostBufSz,
#endif /* !_WIN32 */
                    NULL, 0, (dnsLookup ? 0 : NI_NUMERICHOST));
  TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
    TX_TRACEDNS_AFTER_COMPUTE_TIME();
    /* Turn off tracing to avoid infinite recursion: */
    TXsockaddrToHost(TXPMBUF_SUPPRESS, TXbool_True, TXtraceDns_None,
                     MERGE_FUNC(func), sockaddr, ipBuf, sizeof(ipBuf),
                     TXbool_False);
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
            "getnameinfo(%s, ..., %s): %1.3lf sec returned %d=%s host `%s'%s",
                   ipBuf, (dnsLookup ? "(NI)0" : "NI_NUMERICHOST"),
                   TX_TRACEDNS_TIME(), res,
                   (res != 0 ? gai_strerror(res) : "Ok"),
                   (res != 0 ? Ques : hostBuf),
                   /* same rules as below: */
                   (res == 0 && sockaddr->okIPv4WithIPv6Any &&
                    hostBufSz > 2 && strcmp(hostBuf, "::") == 0 ?
                    "; mapped to `*'" : ""));
  TX_TRACEDNS_AFTER_END()

  if (res != 0 && !suppressErrs)
    {
      /* Clarify bad-family errors, e.g. AF_UNSPEC can happen due to
       * earlier function failure:
       */
      if (sockaddr->storage.ss_family == AF_UNSPEC ||
          res == EAI_FAMILY)
        TXreportBadAFFamily(pmbuf, MERGE_FUNC(func),
                            sockaddr->storage.ss_family);
      else
        txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                       (dnsLookup ? "Cannot resolve IP address to host: %s" :
                        "Cannot convert IP address to string: %s"),
                       gai_strerror(res));
      goto err;
    }

  /* Check for special-case IPv4+IPv6 `*' (see trace message above too): */
  if (sockaddr->okIPv4WithIPv6Any &&
      hostBufSz > 2 &&
      strcmp(hostBuf, "::") == 0)
    strcpy(hostBuf, "*");

  ret = TXbool_True;                            /* success */
  goto finally;

err:
  TXstrncpy(hostBuf, Ques, hostBufSz);
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXsockaddrToStringIP(TXPMBUF *pmbuf, const TXsockaddr *sockaddr, char *ipBuf,
                     size_t ipBufSz)
{
  return(TXsockaddrToHost(pmbuf, TXbool_False, TXtraceDns_None, __FUNCTION__,
                          sockaddr, ipBuf, ipBufSz, TXbool_False));
}

int
TXsockaddrGetPort(TXPMBUF *pmbuf, const TXsockaddr *sockaddr)
/* IPv4/IPv6-compatible.
 * Returns port (in hardware order) from `sockaddr' (network order),
 * or -1 on error.
 */
{
  int   port;

  switch (sockaddr->storage.ss_family)
    {
    case AF_INET:
      port =
        ntohs(((const struct sockaddr_in *)&sockaddr->storage)->sin_port);
      break;
    case AF_INET6:
      port =
        ntohs(((const struct sockaddr_in6 *)&sockaddr->storage)->sin6_port);
      break;
    default:
      TXreportBadAFFamily(pmbuf, __FUNCTION__, sockaddr->storage.ss_family);
      goto err;
    }
  goto finally;

err:
  port = -1;
finally:
  return(port);
}

size_t
TXsockaddrGetIPBytesAndLength(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                              byte **bytes)
/* Sets `*bytes' (if non-NULL) to IP address bytes of `sockaddr'.
 * Returns IP address size of `sockaddr' (in bytes), or 0 on error.
 */
{
  switch (sockaddr->storage.ss_family)
    {
    case AF_INET:
      if (bytes)
        *bytes = (byte *)&((struct sockaddr_in *)&sockaddr->storage)->sin_addr;
      return(sizeof(struct in_addr));
    case AF_INET6:
      if (TX_IPv6_ENABLED(TXApp))
        {
          if (bytes)
            *bytes =
              ((struct sockaddr_in6 *)&sockaddr->storage)->sin6_addr.s6_addr;
          return(sizeof(struct in6_addr));
        }
      /* fall through */
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Unknown/unsupported IP address family %d (%s)",
                     sockaddr->storage.ss_family,
                     TXAFFamilyToString(sockaddr->storage.ss_family));
      if (bytes) *bytes = NULL;
      return(0);
    }
}

TXbool
TXsockaddrSetFamilyAndIPBytes(TXPMBUF *pmbuf, TXsockaddr *sockaddr,
                              TXaddrFamily addrFamily,
                              const byte *bytes, size_t bytesSz)
/* Sets `*sockaddr' to family and network-order IP bytes `bytes'.
 * Returns false on error.
 */
{
  TXbool        ret;

  /* Existing port, scope id etc. would be garbage once we change
   * family, so init first:
   */
  TXsockaddrInit(sockaddr);

  switch (addrFamily)
    {
    case TXaddrFamily_IPv4:
      if (bytesSz != sizeof(struct in_addr)) goto badSz;
      sockaddr->storage.ss_family = AF_INET;
      memcpy(&((struct sockaddr_in *)&sockaddr->storage)->sin_addr,
             bytes, bytesSz);
      break;
    case TXaddrFamily_IPv6:
      if (TX_IPv6_ENABLED(TXApp))
        {
          if (bytesSz != sizeof(struct in6_addr))
            {
            badSz:
              txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
                             "Wrong byte size %d for %s address",
                             (int)bytesSz, TXaddrFamilyToString(addrFamily));
              goto err;
            }
          sockaddr->storage.ss_family = AF_INET6;
          memcpy(((struct sockaddr_in6*)&sockaddr->storage)->sin6_addr.s6_addr,
                 bytes, bytesSz);
          break;
        }
      /* fall through */
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Unknown/unsupported IP address family %d (%s)",
                     (int)addrFamily, TXaddrFamilyToString(addrFamily));
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXsockaddrSetPort(TXPMBUF *pmbuf, TXsockaddr *sockaddr, unsigned port)
/* Returns false on error.
 */
{
  TXbool        ret;

  if (port > 65535)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Port %u out of range", port);
      goto err;
    }
  switch (sockaddr->storage.ss_family)
    {
    case AF_INET:
      ((struct sockaddr_in *)&sockaddr->storage)->sin_port = htons(port);
      break;
    case AF_INET6:
      ((struct sockaddr_in6 *)&sockaddr->storage)->sin6_port = htons(port);
      break;
    default:
      TXreportBadAFFamily(pmbuf, __FUNCTION__, sockaddr->storage.ss_family);
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXsockaddrSetNetmask(TXPMBUF *pmbuf, TXsockaddr *sockaddr, size_t netBits)
/* Sets upper `netBits' bits of `sockaddr' to 1 (all if -1), and rest to 0.
 * Returns 0 on error.
 */
{
  byte          *bytes;
  size_t        numBytes, maxBits, bitsDone;
  TXbool        ret;

  numBytes = TXsockaddrGetIPBytesAndLength(pmbuf, sockaddr, &bytes);
  if (numBytes <= 0) goto err;
  maxBits = BITS_PER_BYTE*numBytes;

  if (netBits == (size_t)(-1))
    netBits = maxBits;
  else if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(netBits) ||
           netBits > maxBits)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Netmask %khd is out of range for %s address",
                     (EPI_HUGEINT)netBits,
                   TXaddrFamilyToString(TXsockaddrGetTXaddrFamily(sockaddr)));
      goto err;
    }

  for (bitsDone = 0; bitsDone < netBits; bitsDone += BITS_PER_BYTE)
    bytes[bitsDone/BITS_PER_BYTE] =
      (netBits - bitsDone >= BITS_PER_BYTE ? 0xff :
       ~((1 << (BITS_PER_BYTE - (netBits - bitsDone))) - 1));
  for ( ; bitsDone < maxBits; bitsDone += BITS_PER_BYTE)
    bytes[bitsDone/BITS_PER_BYTE] = 0;

  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXsockaddrToString(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                   char *addrBuf, size_t addrBufSz)
/* Writes `sockaddr' as string IP and port, to `addrBuf'.  Encloses
 * IPv6 addresses in square brackets to disambiguate port,
 * i.e. suitable for URL.  Writes `?' on error.  `addrBufSz' should be
 * at least TX_SOCKADDR_MAX_STR_SZ.
 * Returns false on error (with `addrBuf' set to `?' if room).
 */
{
  char          *d, *e;
  TXbool        ret;
  int           port;

  d = addrBuf;
  e = addrBuf + addrBufSz;

  if (sockaddr->storage.ss_family == AF_INET6)
    {
      if (e - d < 3) goto tooSmall;             /* 3: `[' `]' and nul */
      *(d++) = '[';
      if (!TXsockaddrToStringIP(pmbuf, sockaddr, d, e - d)) goto err;
      if (d[0] == '*' && d[1] == '\0')
        d[-1] = '*';                            /* `[*' -> `*' */
      else
        {
          d += strlen(d);
          if (e - d < 2) goto tooSmall;         /* 2: `]' and nul */
          *(d++) = ']';
        }
    }
  else                                          /* IPv4 etc. */
    {
      if (!TXsockaddrToStringIP(pmbuf, sockaddr, d, e - d)) goto err;
      d += strlen(d);
    }
  port = TXsockaddrGetPort(pmbuf, sockaddr);
  if (port == -1)
    d += htsnpf(d, e - d, ":?");
  else
    d += htsnpf(d, e - d, ":%u", port);
  if (d >= e)
    {
    tooSmall:
      txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__, BufTooSmallFmt,
                     (int)addrBufSz, (int)TX_SOCKADDR_MAX_STR_SZ);
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
  TXstrncpy(addrBuf, Ques, addrBufSz);
finally:
  return(ret);
}

size_t
TXhostAndPortToSockaddrs(TXPMBUF *pmbuf, TXbool suppressErrs,
                         TXtraceDns traceDns, const char *func,
                         TXaddrFamily addrFamily,
                         const char *host,      /* `*' = any */
                         unsigned port, TXbool dnsLookup,
                         TXbool okIPv4WithIPv6Any,
                         TXsockaddr *sockaddrs, size_t numSockaddrs)
/* Converts string-IP or hostname `host' and `port' to `sockaddrs'.
 * IPv4/IPv6-compatible.  Does (system, blocking) DNS lookup for
 * hostnames iff `dnsLookup' true.  Does not support IPv6 `[::abcd]'
 * bracketed IP adddress syntax for `host' (see comment below).
 * Supports IPv6 `::abcd%2' percent syntax for IPv6 scope id (but `%'
 * must already be URL-decoded by caller, if from URL).  Note that
 * TXaddrFamily_Unspecified, while supported, just passes AF_UNSPEC to
 * system routines; it does not implement Happy Eyeballs.
 *
 * Note that `*' with TXaddrFamily_Unspecified will map to IPv6
 * any-addr address (iff IPv6 supported by Texis, otherwise IPv4-any
 * silently) -- but with `TXsockAddr.okIPv4WithIPv6Any' true iff
 * `okIPv4WithIPv6Any' true, else false and separate IPv4 and IPv6
 * addresses.  `TXsockaddr.okIPv4WithIPv6Any' flag is so caller (and
 * TXezServerSocket() etc.)  can know IPv4 is included.  Note also
 * that `*' with IPv4-only or IPv6-only `addrFamily' is silently ok --
 * even though `*' typically means IPv4+IPv6 -- so that boxes that do
 * not support IPv6 can still use `*' w/o error (if IPv4 `addrFamily'
 * given, or getaddrinfo(AF_UNSPEC) returns AF_INET).  Note also that
 * if IPv6 is disabled by <urlcp> or not supported by OS sockets,, it
 * is up to caller to note this by specifying appropriate
 * TXaddrFamily.  (We do not reject IPv6 addresses here if OS sockets
 * do not support IPv6, because caller might still want to *parse*
 * IPv6 in such cases, e.g. inet...() functions.)
 *
 * Note that IPV6_V6ONLY socket option is also used with IPv4-mapped
 * addresses; see TXezClientSocketNonBlocking().  `okIPv4WithIPv6Any'
 * is ignored for those addresses.  TKaddrFamily_IPv4 with an
 * IPv4-mapped string will return an IPv4 (not IPv4-mapped) address
 * here, so callers should generally use TXaddrFamily_Unspecified to
 * avoid losing AF_INET6 info -- and check returned addresses against
 * ipprotocols, rather than pre-setting `addrFamily' based on
 * iprotocols.  (Also should do that because an TXaddrFamily_IPv4
 * parse of a generic non-IPv4-mapped IPv6 address will give an error
 * here that is not easily seen as related to ipprotocols.)
 *
 * `suppressErrs' provides a way to suppress errors without also
 * suppressing trace messages (which also contain system errors).
 *
 * Returns number of addresses found, or 0 on error (reported and preserved).
 */
{
  struct addrinfo       hints, *result = NULL, *resFirst = NULL, *resIter;
  size_t                numSockaddrsToReturn = 0, earlierIdx;
  int                   res = -1, afFamily;
  TXbool                gotIPv6ResultWhileDisabled = TXbool_False;
  const char            *errStr, *errSfx = "", *nonGaiErrStr = NULL;
  char                  familyBuf[256];
  char                  portBuf[EPI_OS_INT_BITS/3 + 128];
  const char            *portArg;
  TX_TRACEDNS_VARS;
  MERGE_FUNC_VARS;
  const char            *flag0 = Ques, *flag1 = "", *flag2 = "";
  const char            *openQuote = "", *closeQuote = "";
  const char            *protMsg;
  TXbool                doTraceAfter = TXbool_False;
  TXbool                sawDualAny;
  int                   dualAnyPort;
  TXPMBUF               *pmbufNonTrace;

  /* TXPMBUF for functions that have no tracing (and no `suppressErrs'): */
  pmbufNonTrace = (suppressErrs ? TXPMBUF_SUPPRESS : pmbuf);

  /* Do not ask for IPv6 if not enabled; see also main check at end: */
  if (addrFamily == TXaddrFamily_Unspecified &&
      !TX_IPv6_ENABLED(TXApp))
    addrFamily = TXaddrFamily_IPv4;

  switch (addrFamily)
    {
    case TXaddrFamily_Unspecified:      protMsg = "IPv4/IPv6";  break;
    case TXaddrFamily_IPv4:             protMsg = "IPv4";       break;
    case TXaddrFamily_IPv6:             protMsg = "IPv6";       break;
    default:                            protMsg = "IPvUnknown"; break;
    }

  /* For some reason, getaddrinfo("*") returns the loopback
   * interface(s), not IN[6]ADDR_ANY, so handle manually.  Also makes
   * later any-addr checks easier; can just check for NULL:
   */
  if (strcmp(host, "*") == 0) host = NULL;

  openQuote = (host ? "`" : "");
  closeQuote = (host ? "'" : "");
  afFamily = TXaddrFamilyToAFFamily(pmbufNonTrace, addrFamily);
  /* ok to `goto err|reportErr' now: `openQuote' etc. init'd */
  if (afFamily < 0) goto err;

  /* getaddrinfo() allows IP addresses to end with space -- and then
   * be followed by more junk.  This partially changes in Liunx ~3.10
   * (no longer allows trailing space).  We used to allow some space
   * (lead/trail) for flexibility in e.g. <nslookup> parsing user
   * data, but this can lead to issues in e.g. fetching URLs like
   * `http:// 1.2.3.4/', where we'd "resolve" that IP and then pass a
   * `Host: %201.2.3.4' header and would get an HTTP error.  Plus,
   * resolving hostnames w/lead/trail space already gives error; plus
   * if we return error on IP w/lead/trail space then caller can be
   * sure on non-error return that the data is solely an IP and does
   * not contain other characters.  So return error if spaces present:
   */
  if (host)
    {
      if (host[strcspn(host, WhiteSpace)])      /* has any whitespace */
        {
          errStr = nonGaiErrStr = "Contains whitespace";
          goto reportErr;
        }
    }

  if (!TXinitsock(pmbufNonTrace)) goto err;

  /* Room for at least one makes sense, and is assumed below: */
  if (numSockaddrs < 1)
    {
      errStr = nonGaiErrStr = "Must ask with room for at least one address";
      goto reportErr;
    }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = afFamily;
  if (!dnsLookup || !host)
    {
      hints.ai_flags |= AI_NUMERICHOST;         /* no DNS lookup */
      flag1 = " | AI_NUMERICHOST";
    }
  if (port >= 65536)
    {
      errStr = nonGaiErrStr = "Port out of range";
      TXseterror(ERANGE);
      goto reportErr;
    }
#ifdef AI_NUMERICSERV
  /* Might as well let getaddrinfo() set the port too: */
  hints.ai_flags |= AI_NUMERICSERV;             /* we give numeric port */
  htsnpf(portBuf, sizeof(portBuf), "%u", (unsigned)port);
  portArg = portBuf;
  flag0 = "AI_NUMERICSERV";
#else /* !AI_NUMERICSERV */
  /* wtf Linux 2.4.9 getaddrinfo() returns -8 `Servname not supported
   * for ai_socktype' for numeric port (though it supports named services
   * in /etc/services); do it manually below:
   */
  htsnpf(portBuf, sizeof(portBuf), "NULL for port %u", (unsigned)port);
  portArg = NULL;
  flag0 = "(AI)0";
#endif /* !AI_NUMERICSERV */
  if (!host)
    {
      hints.ai_flags |= AI_PASSIVE;             /* get IN[6]ADDR_ANY[_INIT] */
      flag2 = " | AI_PASSIVE";
    }
  /* Do not support bracketed `[abced:1234:...]' format: we are passed
   * port separately, so brackets are not needed here to disambiguate
   * port.  Caller should have stripped brackets; doing it here might
   * erroneously strip them twice.
   */
  TX_TRACEDNS_BEFORE_BEGIN(traceDns, TXtraceDns_Syscalls)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
      "getaddrinfo(%s%s%s, %s, {%s, (%s%s%s)}, ...) starting",
       openQuote, (host ? host : "NULL"), closeQuote, portBuf,
                   TXAFFamilyToString(hints.ai_family), flag0, flag1, flag2);
  TX_TRACEDNS_BEFORE_END()
  res = getaddrinfo(host, portArg, &hints, &result);
  doTraceAfter = TXbool_True;

  /* For AI_PASSIVE and AF_UNSPEC, we may get IPv4 before/after IPv6.
   * If `okIPv4WithIPv6Any', prefer IPv6 first, as it can listen to
   * both IPv4 and IPv6; this IPv6 preference is also assumed below
   * when we copy addresses (i.e. `sawDualAny' should be hit before
   * IPv4-any).  But if not `okIPv4WithIPv6Any', prefer IPv4 first for
   * cross-platform consistency (e.g. in opening/reporting separate
   * IPv4+IPv6 sockets for `*'):
   */
  if (res == 0 &&                               /* getaddrinfo() success */
      addrFamily == TXaddrFamily_Unspecified &&
      !host &&
      TX_IPv6_ENABLED(TXApp))
    {
      for (resFirst = result;
           resFirst && resFirst->ai_family !=
             (okIPv4WithIPv6Any ? AF_INET6 : AF_INET);
           resFirst = resFirst->ai_next)
        ;
    }

  /* For AF_INET IPv4-mapped address (e.g. ::ffff:102:304), Linux
   * 2.6.32 returns AF_INET address, whereas Linux 2.4.9 returns
   * EAI_ADDRFAMILY.  (For AF_{UNSPEC,INET6} IPv4-mapped address,
   * Linux 2.6.32 returns IPv4-mapped address e.g. ::ffff:102:304.)
   * Always return EAI_ADDRFAMILY for any AF_INET IPv6-syntax: 1) we'd
   * like to not lose the knowledge that we parsed an IPv6 address, 2)
   * it is consistent across platforms, 3) it is consistent with the
   * converse: for AF_INET6 IPv4-addr (e.g. 1.2.3.4) Linux 2.6.32
   * returns EAI_ADDRFAMILY (instead of IPv4-mapped address).
   */
  if (res == 0 &&                               /* getaddrinfo() success */
      result &&
      addrFamily == TXaddrFamily_IPv4 &&
      host &&
      strchr(host, ':'))                        /* was given IPv6 syntax */
    {
      TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                       "getaddrinfo(`%s', %s, {AF_INET, ...}) returned ok but source address looks like IPv6; mapping to error EAI_ADDRFAMILY",
                       host, portBuf);
      TX_TRACEDNS_AFTER_END()
#ifdef EAI_ADDRFAMILY
      res = EAI_ADDRFAMILY;
#else /* !EAI_ADDRFAMILY */
      res = -9;
#endif /* !EAI_ADDRFAMILY */
      if (result)
        {
          freeaddrinfo(result);
          result = NULL;
        }
    }

#ifndef EPI_GETADDRINFO_SUPPORTS_IPv4_CLASSES
  {
    struct sockaddr_in  *sa;
    struct sockaddr_in6 *sa6;
    struct in_addr      inaddr;

    sa = (struct sockaddr_in *)&sockaddrs->storage;
    sa6 = (struct sockaddr_in6 *)&sockaddrs->storage;

    /* Linux 2.4.9 getaddrinfo() returns error for `1.2.3'; workaround
     * using inet_aton() which does:
     */
    if (res != 0 && !dnsLookup && host &&
        (addrFamily == TXaddrFamily_Unspecified ||
         addrFamily == TXaddrFamily_IPv4) &&
        inet_aton(host, &inaddr))
      {
        TXsockaddrInit(sockaddrs);
        sa->sin_family = AF_INET;
        sa->sin_addr = inaddr;
        sa->sin_port = htons(port);
        TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
          {
            char        addrBuf[TX_SOCKADDR_MAX_STR_SZ];

            TXsockaddrToString(TXPMBUF_SUPPRESS, sockaddrs,
                               addrBuf, sizeof(addrBuf));
            txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                        "inet_aton(%s%s%s) + manual port returned address %s",
                           openQuote, host, closeQuote, addrBuf);
          }
        TX_TRACEDNS_AFTER_END()
        numSockaddrsToReturn = 1;
        goto finally;
      }

    /* Same platform (Linux 2.4.9) does not support NULL host + AI_PASSIVE: */
    if (res != 0 && !dnsLookup && !host)
      {
        char    useAddrBuf[TX_SOCKADDR_MAX_STR_SZ];

        switch (addrFamily)
          {
          case TXaddrFamily_Unspecified:
            /* This platform probably only supports IPv4; fall through: */
          case TXaddrFamily_IPv4:
            TXsockaddrInit(sockaddrs);
            sa->sin_family = AF_INET;
            sa->sin_addr.s_addr = htonl(INADDR_ANY);
            sa->sin_port = htons(port);
            TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
              TXsockaddrToString(TXPMBUF_SUPPRESS, sockaddrs,
                                 useAddrBuf, sizeof(useAddrBuf));
              txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                          "manually assigned INADDR_ANY + port to address %s",
                             useAddrBuf);
            TX_TRACEDNS_AFTER_END()
            numSockaddrsToReturn = 1;
            goto finally;
          case TXaddrFamily_IPv6:
            TXsockaddrInit(sockaddrs);
            sa6->sin6_family = AF_INET6;
            memcpy(&sa6->sin6_addr, &in6addr_any, sizeof(sa6->sin6_addr));
            sa6->sin6_port = htons(port);
            TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
              TXsockaddrToString(TXPMBUF_SUPPRESS, sockaddrs,
                                 useAddrBuf, sizeof(useAddrBuf));
              txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                         "manually assigned IN6ADDR_ANY + port to address %s",
                             useAddrBuf);
            TX_TRACEDNS_AFTER_END()
            numSockaddrsToReturn = 1;
            goto finally;
          default:
            break;
          }
      }
  }
#endif /* !EPI_GETADDRINFO_SUPPORTS_IPv4_CLASSES */

  if (res != 0)                                 /* failed */
    {
      errStr = gai_strerror(res);
      if (0
#ifdef EAI_ADDRFAMILY
          || res == EAI_ADDRFAMILY
#endif /* EAI_ADDRFAMILY */
#ifdef EAI_FAMILY
          || res == EAI_FAMILY
#endif /* EAI_FAMILY */
          )
        {
          TXaddrFamily      askedTxFam;

          /* hints.ai_family may differ from `addrFamily',
           * i.e. if !TX_IPv6_ENABLED():
           */
          askedTxFam = TXAFFamilyToTXaddrFamily(TXPMBUF_SUPPRESS,
                                                hints.ai_family);
          htsnpf(familyBuf, sizeof(familyBuf), " (asked for %s)",
                 (askedTxFam != TXaddrFamily_Unknown ?
                  TXaddrFamilyToString(askedTxFam) :
                  TXAFFamilyToString(hints.ai_family)));
          errSfx = familyBuf;
        }
    reportErr:
      if (!suppressErrs)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         (dnsLookup ?
                         "Cannot parse/resolve %s address/host %s%s%s: %s%s" :
                          "Cannot parse %s address %s%s%s: %s%s"),
                         protMsg, openQuote,
                         (host ? host : (addrFamily == TXaddrFamily_IPv6 ?
                                         "IN6ADDR_ANY_INIT" :
                                         (addrFamily == TXaddrFamily_IPv4 ?
                                          "INADDR_ANY" : "*"))),
                         closeQuote, errStr, errSfx);
          TX_POPERROR();
        }
      goto err;
    }
  if (!result)
    {
      errStr = nonGaiErrStr = "NULL result returned by getaddrinfo()";
      goto reportErr;
    }

  /* Copy unique addresses from`result' list to `sockaddrs': */
  sawDualAny = TXbool_False;
  dualAnyPort = -1;
  for (resIter = result, numSockaddrsToReturn = 0;
       resIter && numSockaddrsToReturn < numSockaddrs;
       resIter = resIter->ai_next)
    {
      struct addrinfo   *resToCopy;
      TXsockaddr        *curSockaddr = &sockaddrs[numSockaddrsToReturn];

      resToCopy = resIter;
      /* Swap copy order of `result' and `resFirst', if latter set: */
      if (resFirst)
        {
          if (resIter == result)
            resToCopy = resFirst;
          else if (resIter == resFirst)
            resToCopy = result;
        }

      if (resToCopy->ai_family == AF_INET6 && !TX_IPv6_ENABLED(TXApp))
        {
          /* Since IPv6 is not enabled, we mapped Unspecified to IPv4
           * above, so we should not be getting any IPv6 addresses
           * for Unspecified.  This is probably due to explicit IPv6
           * request; error later if no other IPv4 addresses:
           */
          gotIPv6ResultWhileDisabled = TXbool_True;
          continue;
        }
      if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(resToCopy->ai_addrlen) ||
          resToCopy->ai_addrlen > sizeof(curSockaddr->storage))
        {
          errStr = nonGaiErrStr =
            "Address returned by getaddrinfo() too large";
          goto reportErr;
        }
      TXsockaddrInit(curSockaddr);
      memcpy(&curSockaddr->storage, resToCopy->ai_addr,
             resToCopy->ai_addrlen);
#ifndef AI_NUMERICSERV
      /* Fill in port; lack of AI_NUMERICSERV means getaddrinfo() did not: */
      switch (TXsockaddrGetAFFamily(curSockaddr))
        {
        case AF_INET:
          ((struct sockaddr_in *)&curSockaddr->storage)->sin_port =
            htons(port);
          break;
        case AF_INET6:
          ((struct sockaddr_in6 *)&curSockaddr->storage)->sin6_port =
            htons(port);
          break;
        default:
          errStr = nonGaiErrStr =
            "Cannot set port: Unknown address family returned";
          goto reportErr;
        }
#endif /* !AI_NUMERICSERV */
      /* Set `okIPv4WithIPv6Any' if appropriate: */
      if (addrFamily == TXaddrFamily_Unspecified &&
          !host &&
          okIPv4WithIPv6Any)
        curSockaddr->okIPv4WithIPv6Any = TXbool_True;
      /* Avoid dups: */
      for (earlierIdx = 0;
           earlierIdx < numSockaddrsToReturn &&
             !(TXsockaddrIPsAreEqual(pmbufNonTrace, &sockaddrs[earlierIdx],
                                     curSockaddr, TXbool_False) &&
               TXsockaddrGetPort(pmbufNonTrace, &sockaddrs[earlierIdx]) ==
               TXsockaddrGetPort(pmbufNonTrace, curSockaddr));
           earlierIdx++)
        ;
      if (earlierIdx >= numSockaddrsToReturn)   /* not a dup */
        {
          /* If we already have an IPv6-any-addr+IPv4, skip
           * IPv4-any-addr, as it is not only redundant but will fail
           * to open at the same time as the former.  This assumes
           * `resFirst' reording done above:
           */
          if (sawDualAny &&
              curSockaddr->storage.ss_family == AF_INET &&
              ntohl(((const struct sockaddr_in *)&curSockaddr->storage)
                    ->sin_addr.s_addr) == INADDR_ANY &&
              ntohs(((const struct sockaddr_in *)&curSockaddr->storage)
                    ->sin_port) == dualAnyPort)
            continue;                           /* skip; redundant/conflict */
          if (curSockaddr->storage.ss_family == AF_INET6 &&
              IN6_ARE_ADDR_EQUAL(&in6addr_any,
          &((const struct sockaddr_in6 *)&curSockaddr->storage)->sin6_addr) &&
              curSockaddr->okIPv4WithIPv6Any)
            {
              sawDualAny = TXbool_True;
              dualAnyPort = ntohs(((const struct sockaddr_in6 *)&curSockaddr
                                   ->storage)->sin6_port);
            }
          numSockaddrsToReturn++;
        }
    }

  /* Sanity check: */
  if (numSockaddrsToReturn == 0)
    {
      if (gotIPv6ResultWhileDisabled)
        {
          TXseterror(TXERR_INVALID_PARAMETER);
          errStr = nonGaiErrStr = IPv6Unsupported;
          goto reportErr;
        }
      else                                      /* should not happen */
        {
          errStr = nonGaiErrStr = "No unique addresses found";
          goto reportErr;
        }
    }

  goto finally;

err:
  for (earlierIdx = 0; earlierIdx < numSockaddrs; earlierIdx++)
    TXsockaddrInit(&sockaddrs[earlierIdx]);
  numSockaddrsToReturn = 0;
finally:
  if (doTraceAfter)
    {
      TXsockaddr        traceSockaddr;
      char              addrsBuf[TX_MAX(TX_SOCKADDR_MAX_STR_SZ, 2048)];
      char              *d, *e;
      size_t            numResults, retIdx;
      const char        *tok;
      char              tokBuf[128];
      char              retAddrsBuf[TX_MAX(TX_SOCKADDR_MAX_STR_SZ, 2048)];
      char              askedForBuf[128];

      TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
      TX_TRACEDNS_AFTER_COMPUTE_TIME();
      /* Print getaddrinfo() addrs to `addrsBuf': */
      e = addrsBuf + sizeof(addrsBuf);
      for (resIter = result, numResults = 0, d = addrsBuf;
           resIter && res == 0;
           resIter = resIter->ai_next, numResults++)
        {
          if (d > addrsBuf && d < e) *(d++) = ' ';
          if (d >= e) continue;
          switch (resIter->ai_socktype)
            {
            case SOCK_STREAM:   tok = "STREAM";    break;
            case SOCK_DGRAM:    tok = "DGRAM";     break;
            case SOCK_RAW:      tok = "RAW";       break;
            case SOCK_RDM:      tok = "RDM";       break;
            case SOCK_SEQPACKET:tok = "SEQPACKET"; break;
#ifdef SOCK_DCCP
            case SOCK_DCCP:     tok = "DCCP";      break;
#endif
#ifdef SOCK_PACKET
            case SOCK_PACKET:   tok = "PACKET";    break;
#endif
            default:
              htsnpf(tokBuf, sizeof(tokBuf), "SOCK_%d",
                     (int)resIter->ai_socktype);
              tok = tokBuf;
              break;
            }
          TXstrncpy(d, tok, e - d);
          d += strlen(d);
          if (d > addrsBuf && d < e) *(d++) = '-';
          if (d >= e) continue;
          if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(resIter->ai_addrlen) ||
              resIter->ai_addrlen > sizeof(traceSockaddr.storage))
            htsnpf(d, e - d, "[Result size %wd too large]",
                   (EPI_HUGEINT)resIter->ai_addrlen);
          else
            {
              TXsockaddrInit(&traceSockaddr);
              memcpy(&traceSockaddr.storage, resIter->ai_addr,
                     resIter->ai_addrlen);
              if (addrFamily == TXaddrFamily_Unspecified && !host)
                traceSockaddr.okIPv4WithIPv6Any = TXbool_True;
              TXsockaddrToString(TXPMBUF_SUPPRESS, &traceSockaddr, d, e - d);
            }
          d += strlen(d);
        }
      if (result)
        {
          if (d < e)
          {
            *d = '\0';
          } else {
            char *TruncationPoint = addrsBuf + sizeof(addrsBuf) - 4;
            strcpy(TruncationPoint, "...");
          }
        }
      else
        strcpy(addrsBuf, "NULL");

      /* Print returned addrs to `retAddrsBuf': */
      d = retAddrsBuf;
      e = retAddrsBuf + sizeof(retAddrsBuf);
      for (retIdx = 0; retIdx < numSockaddrsToReturn; retIdx++)
        {
          if (retIdx > 0 && d < e) *(d++) = ' ';
          if (d < e)
            {
              TXsockaddrToString(TXPMBUF_SUPPRESS, &sockaddrs[retIdx],
                                 d, e - d);
              d += strlen(d);
            }
        }
      if (d < e)
      {
        *d = '\0';
      } else {
        char *TruncationPoint = retAddrsBuf + sizeof(retAddrsBuf) - 4;
        strcpy(TruncationPoint, "...");
      }

      if (numSockaddrs != numSockaddrsToReturn)
        htsnpf(askedForBuf, sizeof(askedForBuf), " (asked for up to %d)",
               (int)numSockaddrs);
      else
        *askedForBuf = '\0';

      /* Issue message: */
      txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                     "getaddrinfo(%s%s%s, %s, {%s, (%s%s%s)}, ...): %1.3lf sec returned %d=%s %d address%s %s; okIPv4WithIPv6Any=%s; returning %d%s address%s %s%s%s%s",
                     openQuote, (host ? host : "NULL"),
                     closeQuote, portBuf,
                     TXAFFamilyToString(hints.ai_family), flag0, flag1, flag2,
                     TX_TRACEDNS_TIME(), res, (res ? gai_strerror(res) : "Ok"),
                     (int)numResults, (numResults != 1 ? "es" : ""),
                     addrsBuf, (okIPv4WithIPv6Any ? "Y" : "N"),
                     (int)numSockaddrsToReturn,
                     (numSockaddrs == numSockaddrsToReturn ? " asked-for":""),
                     (numSockaddrsToReturn != 1 ? "es" : ""),
                     retAddrsBuf, askedForBuf, (nonGaiErrStr ? "; " : ""),
                     (nonGaiErrStr ? nonGaiErrStr : ""));
      TX_TRACEDNS_AFTER_END()
    }
  else if (nonGaiErrStr)
    {
      TX_TRACEDNS_AFTER_BEGIN(traceDns, TXtraceDns_Syscalls)
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
       "getaddrinfo(%s%s%s, ...) not called: %s (error suppressed by caller)",
                       openQuote, (host ? host : "NULL"),
                       closeQuote, nonGaiErrStr);
      TX_TRACEDNS_AFTER_END()
    }

  if (result)
    {
      freeaddrinfo(result);
      result = NULL;
    }
  return(numSockaddrsToReturn);
}

TXsockaddr *
TXsockaddrNew(TXPMBUF *pmbuf)
{
  TXsockaddr    *ret;

  if (!(ret = TX_NEW(pmbuf, TXsockaddr))) return(NULL);
  TXsockaddrInit(ret);
  return(ret);
}

TXsockaddr *
TXsockaddrDup(TXPMBUF *pmbuf, const TXsockaddr *sockaddr)
{
  TXsockaddr    *ret;

  if (!(ret = TX_NEW(pmbuf, TXsockaddr))) return(NULL);
  *ret = *sockaddr;
  return(ret);
}

TXsockaddr *
TXsockaddrFree(TXsockaddr *sockaddr)
{
  sockaddr = TXfree(sockaddr);
  return(NULL);
}

int
TXsockaddrToIPv4(TXPMBUF *pmbuf, const TXsockaddr *sockaddrIn,
                 TXsockaddr *sockaddrOut)
/* Translates `sockaddrIn' to IPv4 `sockaddrOut' iff IPv4-mapped, else
 * copies.  Returns 0 on error (giving an IPv4 or non-IPv4-mapped IPv6
 * address is not an error.), 1 if unchanged, 2 if modified.
 */
{
  int           ret;
  byte          *inBytes;
  size_t        inBytesLen;
  byte          ipv4Bytes[TX_IPv4_BYTE_SZ];

  if (TXsockaddrIsIPv4Mapped(sockaddrIn))
    {
      TXsockaddrInit(sockaddrOut);
      inBytesLen = TXsockaddrGetIPBytesAndLength(pmbuf, sockaddrIn, &inBytes);
      if (inBytesLen <= 0) goto err;
      if (inBytesLen != TX_IPv6_BYTE_SZ)
        {
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Internal error: Unexpected IP byte size");
          goto err;
        }
      memcpy(ipv4Bytes, inBytes + TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ,
             TX_IPv4_BYTE_SZ);
      if (!TXsockaddrSetFamilyAndIPBytes(pmbuf, sockaddrOut, TXaddrFamily_IPv4,
                                         ipv4Bytes, TX_IPv4_BYTE_SZ) ||
          !TXsockaddrSetPort(pmbuf, sockaddrOut,
                             TXsockaddrGetPort(pmbuf, sockaddrIn)))
        goto err;
      ret = 2;
    }
  else
    {
      *sockaddrOut = *sockaddrIn;
      ret = 1;
    }
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

int
TXsockaddrToIPv6(TXPMBUF *pmbuf, const TXsockaddr *sockaddrIn,
                 TXsockaddr *sockaddrOut)
/* Translates `sockaddrIn' to IPv4-mapped `sockaddrOut' iff IPv4, else
 * copies.  Returns 0 on error (giving a non-IPv4 address is not
 * an error.), 1 if unchanged, 2 if modified.
 */
{
  int           ret;
  byte          *inBytes;
  size_t        inBytesLen;
  byte          ipv6Bytes[TX_IPv6_BYTE_SZ];

  if (TXsockaddrGetTXaddrFamily(sockaddrIn) == TXaddrFamily_IPv4)
    {
      TXsockaddrInit(sockaddrOut);
      inBytesLen = TXsockaddrGetIPBytesAndLength(pmbuf, sockaddrIn, &inBytes);
      if (inBytesLen <= 0) goto err;
      if (inBytesLen != TX_IPv4_BYTE_SZ)
        {
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Internal error: Unexpected IP byte size");
          goto err;
        }
      memset(ipv6Bytes, 0, sizeof(ipv6Bytes));
      ipv6Bytes[10] = 0xff;
      ipv6Bytes[11] = 0xff;
      memcpy(ipv6Bytes + TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ,
             inBytes, TX_IPv4_BYTE_SZ);
      if (!TXsockaddrSetFamilyAndIPBytes(pmbuf, sockaddrOut, TXaddrFamily_IPv6,
                                         ipv6Bytes, TX_IPv6_BYTE_SZ) ||
          !TXsockaddrSetPort(pmbuf, sockaddrOut,
                             TXsockaddrGetPort(pmbuf, sockaddrIn)))
        goto err;
      ret = 2;
    }
  else
    {
      *sockaddrOut = *sockaddrIn;
      ret = 1;
    }
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

TXbool
TXsockaddrToInaddr(TXPMBUF *pmbuf, const TXsockaddr *sockaddr,
                   struct in_addr *inaddr)
{
  TXbool        ret;

  if (sockaddr->storage.ss_family != AF_INET)
    {
      TXreportBadAFFamily(pmbuf, __FUNCTION__, sockaddr->storage.ss_family);
      goto err;
    }
  memcpy(inaddr, &((struct sockaddr_in *)&sockaddr->storage)->sin_addr,
         sizeof(*inaddr));
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
  memset(inaddr, 0, sizeof(*inaddr));
finally:
  return(ret);
}

void
TXinaddrAndPortToSockaddr(struct in_addr inaddr, unsigned port,
                          TXsockaddr *sockaddr)
/* Legacy IPv4 helper function.
 */
{
  struct sockaddr_in    *sockaddrIn;

  TXsockaddrInit(sockaddr);

  sockaddrIn = (struct sockaddr_in *)&sockaddr->storage;
  sockaddrIn->sin_family = AF_INET;
  sockaddrIn->sin_addr = inaddr;
  sockaddrIn->sin_port = htons(port);
}

TXbool
TXsockaddrHardwareToNetworkOrder(TXPMBUF *pmbuf, TXsockaddr *sockaddr)
{
#ifdef EPI_LITTLE_ENDIAN
  struct sockaddr_in    *sa4;
  struct sockaddr_in6   *sa6;
  TXbool                ret;
  byte                  *bytes, tmp;
  size_t                byteIdx;
#endif /* EPI_LITTLE_ENDIAN */
  size_t                addrLen;

  switch (sockaddr->storage.ss_family)
    {
    case AF_INET:
#ifdef EPI_LITTLE_ENDIAN
      sa4 = (struct sockaddr_in *)&sockaddr->storage;
      sa4->sin_port = htons(sa4->sin_port);
      sa4->sin_addr.s_addr = htonl(sa4->sin_addr.s_addr);
#elif defined(EPI_BIG_ENDIAN)
      /* no-op */
#else /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
#  error Unknown byte order
#endif /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
      break;
    case AF_INET6:
#ifdef EPI_LITTLE_ENDIAN
      addrLen = TXsockaddrGetIPBytesAndLength(pmbuf, sockaddr, &bytes);
      if (addrLen <= 0) goto err;
      sa6 = (struct sockaddr_in6 *)bytes;
      sa6->sin6_port = htons(sa6->sin6_port);
      for (byteIdx = 0; byteIdx < addrLen/2; byteIdx++)
        {
          tmp = bytes[byteIdx];
          bytes[byteIdx] = bytes[(addrLen - 1) - byteIdx];
          bytes[(addrLen - 1) - byteIdx] = tmp;
        }
      /* ignore flowinfo, scope id? */
#elif defined(EPI_BIG_ENDIAN)
      /* no-op */
#else /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
#  error Unknown byte order
#endif /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
      break;
    default:
      TXreportBadAFFamily(pmbuf, __FUNCTION__, sockaddr->storage.ss_family);
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXsockaddrNetContainsSockaddrNet(TXPMBUF *pmbuf,
                                 const TXsockaddr *sockaddrA, int netbitsA,
                                 const TXsockaddr *sockaddrB, int netbitsB,
                                 TXbool mapIPv4)
/* Returns true if `sockaddrA' with netmask bits `netbitsA' contains
 * `sockaddrB' with netmask bits `netbitsB'.  Network byte order.
 * Not commutative.  If `mapIPv4', maps IPv4 addresses to IPv6,
 * so e.g. 1.2.3.4 is the same as ::ffff:1.2.3.4.
 * A netbits value of -1 means length of associated sockaddr.
 */
{
  int                   maxNetbits;
  TXsockaddr            sockaddrAHw, sockaddrBHw;
  TXsockaddr            sockaddrAToIPv6, sockaddrBToIPv6;
  struct in_addr        inaddrAHw, inaddrBHw;
  EPI_UINT64            *uint64Addr;
  /* Network-order array of hardware-order bits: ...Hw[0] contains
   * most-significant 64 bits (in hardware order); ...Hw[1] least-significant:
   */
  EPI_UINT64            addrAHw[2], maskAHw[2], addrBHw[2];
  /* HW_MASK_64: 64-bit version of TX_HARDWARE_ORDER_INET_NETMASK(): */
#define HW_MASK_64(bits)   \
  ((bits) ? ~(((EPI_UINT64)1 << (64-(bits))) - (EPI_UINT64)1) : (EPI_UINT64)0)

  /* Just nip this common case in the bud, and report from top-level
   * function, not e.g. TXsockaddrNetworkToHardwareOrder().
   * Also fully validate family, for netbits fixup assumptions:
   */
  if (sockaddrA->storage.ss_family != AF_INET &&
      sockaddrA->storage.ss_family != AF_INET6)
    {
      TXreportBadAFFamily(pmbuf, __FUNCTION__,
                          sockaddrA->storage.ss_family);
      return(TXbool_False);
    }
  if (sockaddrB->storage.ss_family != AF_INET &&
      sockaddrB->storage.ss_family != AF_INET6)
    {
      TXreportBadAFFamily(pmbuf, __FUNCTION__,
                          sockaddrB->storage.ss_family);
      return(TXbool_False);
    }

  /* Map to IPv6 before checking family or netbits: */
  if (mapIPv4 && TX_IPv6_ENABLED(TXApp))
    {
      if (sockaddrA->storage.ss_family == AF_INET)
        {
          if (!TXsockaddrToIPv6(pmbuf, sockaddrA, &sockaddrAToIPv6))
            return(TXbool_False);
          if (netbitsA > (maxNetbits = TX_IPv4_BYTE_SZ*BITS_PER_BYTE))
            goto reportNetbitsA;
          if (netbitsA >= 0)
            netbitsA += (TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ)*BITS_PER_BYTE;
          sockaddrA = &sockaddrAToIPv6;
        }
      if (sockaddrB->storage.ss_family == AF_INET)
        {
          if (!TXsockaddrToIPv6(pmbuf, sockaddrB, &sockaddrBToIPv6))
            return(TXbool_False);
          if (netbitsB > (maxNetbits = TX_IPv4_BYTE_SZ*BITS_PER_BYTE))
            goto reportNetbitsB;
          if (netbitsB >= 0)
            netbitsB += (TX_IPv6_BYTE_SZ - TX_IPv4_BYTE_SZ)*BITS_PER_BYTE;
          sockaddrB = &sockaddrBToIPv6;
        }
    }

  /* If families still different, they do not contain each other: */
  if (sockaddrA->storage.ss_family != sockaddrB->storage.ss_family)
    return(TXbool_False);

  /* Fix up and/or validate netbits: */
  maxNetbits = (int)TXsockaddrGetIPBytesAndLength(pmbuf, sockaddrA, NULL)*8;
  if (maxNetbits <= 0) return(TXbool_False);
  if (netbitsA < 0)
    netbitsA = maxNetbits;
  else if (netbitsA > maxNetbits)
    {
    reportNetbitsA:
      netbitsB = netbitsA;
      sockaddrB = sockaddrA;
      goto reportNetbitsB;
    }
  if (netbitsB < 0)
    netbitsB = maxNetbits;
  else if (netbitsB > maxNetbits)
    {
    reportNetbitsB:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
           "Network bits /%d too large (expected at most /%d for %s address)",
                     netbitsB, maxNetbits,
               TXaddrFamilyToString(TXAFFamilyToTXaddrFamily(TXPMBUF_SUPPRESS,
                                              sockaddrB->storage.ss_family)));
      return(TXbool_False);
    }

  /* Some easy cases assumed checked below: */
  if (netbitsA == 0)                            /* net A is whole addr space*/
    return(TXbool_True);
  if (netbitsA > netbitsB)                      /* net A smaller than net B */
    return(TXbool_False);

  /* Get hardware-order versions: */
  sockaddrAHw = *sockaddrA;
  sockaddrBHw = *sockaddrB;
  if (!TXsockaddrNetworkToHardwareOrder(pmbuf, &sockaddrAHw) ||
      !TXsockaddrNetworkToHardwareOrder(pmbuf, &sockaddrBHw))
    return(TXbool_False);

  switch (sockaddrAHw.storage.ss_family)
    {
    case AF_INET:
      /* memcpy() to avoid gcc `break strict-aliasing rules' errors: */
      memcpy(&inaddrAHw,
             &((const struct sockaddr_in *)&sockaddrAHw.storage)->sin_addr,
             sizeof(inaddrAHw));
      memcpy(&inaddrBHw,
             &((const struct sockaddr_in *)&sockaddrBHw.storage)->sin_addr,
             sizeof(inaddrBHw));
      return(TX_HARDWARE_ORDER_INET_CONTAINS_INET(inaddrAHw.s_addr, netbitsA,
                                                  inaddrBHw.s_addr, netbitsB));
    case AF_INET6:
      if (!TX_IPv6_ENABLED(TXApp)) return(TXbool_False);

      uint64Addr = (EPI_UINT64 *)
        &((const struct sockaddr_in6 *)&sockaddrAHw.storage)->sin6_addr;
#ifdef EPI_LITTLE_ENDIAN
#  define UPPER_Hw_SRC_IDX      1
#  define LOWER_Hw_SRC_IDX      0
#elif defined(EPI_BIG_ENDIAN)
#  define UPPER_Hw_SRC_IDX      0
#  define LOWER_Hw_SRC_IDX      1
#else /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
#  error Unknown endianness
#endif /* !EPI_LITTLE_ENDIAN && !EPI_BIG_ENDIAN */
      addrAHw[0] = uint64Addr[UPPER_Hw_SRC_IDX];
      addrAHw[1] = uint64Addr[LOWER_Hw_SRC_IDX];

      uint64Addr = (EPI_UINT64 *)
        &((const struct sockaddr_in6 *)&sockaddrBHw.storage)->sin6_addr;
      addrBHw[0] = uint64Addr[UPPER_Hw_SRC_IDX];
      addrBHw[1] = uint64Addr[LOWER_Hw_SRC_IDX];

      if (netbitsA <= 64)
        {
          maskAHw[0] = HW_MASK_64(netbitsA);
          maskAHw[1] = (EPI_UINT64)0;
        }
      else                                      /* netbitsA > 64 */
        {
          maskAHw[0] = ~(EPI_UINT64)0;
          maskAHw[1] = HW_MASK_64(netbitsA - 64);
        }

      return((addrAHw[0] & maskAHw[0]) == (addrBHw[0] & maskAHw[0]) &&
             (addrAHw[1] & maskAHw[1]) == (addrBHw[1] & maskAHw[1]));
    default:
      return(TXbool_False);
    }
#undef HW_MASK_64
#undef UPPER_Hw_SRC_IDX
#undef LOWER_Hw_SRC_IDX
}

TXbool
TXsockaddrIsIPv4Mapped(const TXsockaddr *sockaddr)
{
  struct sockaddr_in6   *sa6;
  TXbool                ret = TXbool_False;

  if (sockaddr->storage.ss_family == AF_INET6)
    {
      sa6 = ((struct sockaddr_in6 *)&sockaddr->storage);
      if (IN6_IS_ADDR_V4MAPPED(&sa6->sin6_addr))
        ret = TXbool_True;
    }
  return(ret);
}

static TXbool
TXezGetSocketFlags(TXSOCKET skt, char *buf, size_t bufsz)
/* Prints some handle flags to `buf' for `skt'.  For tracing.
 * Returns false on error (`buf' still set sanely).
 */
{
  TXbool        ret;
#ifdef _WIN32
  DWORD getFlags;

  if (!GetHandleInformation((HANDLE)skt, &getFlags)) goto err;
  htsnpf(buf, bufsz, "%s%s",
         ((getFlags & HANDLE_FLAG_INHERIT) ? "HANDLE_FLAG_INHERIT" :
          "!HANDLE_FLAG_INHERIT"),
         ((getFlags & HANDLE_FLAG_PROTECT_FROM_CLOSE) ?
          " HANDLE_FLAG_PROTECT_FROM_CLOSE" : ""));
#else /* !_WIN32 */
  int   flags;

  flags = fcntl(skt, F_GETFD);
  if (flags == -1) goto err;
  htsnpf(buf, bufsz, "%s",
         ((flags & FD_CLOEXEC) ? "FD_CLOEXEC" : "!FD_CLOEXEC"));
#endif /* !_WIN32 */
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
  htsnpf(buf, bufsz, "unknown-flags");
finally:
  return(ret);
}

/* We prefer setting CLOEXEC or its equivalent atomically with
 * [WSA]socket() via TX_SOCKET_FLAGS, rather than later with fcntl()
 * (TX_SOCKET_FLAGS_POST_STR): another thread could fork() + exec()
 * between our [WSA]socket() and fcntl().  Bug 7570.
 */
#ifdef _WIN32
#  define TX_SOCKET_FUNC_STR    "WSASocket"
/* WSA_FLAG_OVERLAPPED because MSDN docs say most sockets should have it
 * WSA_FLAG_NO_HANDLE_INHERIT because we do not want child processes
 *   to inherit the socket and cause problems (e.g. hanging web connection
 *   after data sent, if Vortex starts a daemon like task monitor; Bug 7570)
 */
#  define TX_SOCKET_FLAGS       (WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT)
#  define TX_SOCKET_FLAGS_STR " (WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT)"
#  define TX_SOCKET_FLAGS_POST_STR      ""
#else /* !_WIN32 */
#  define TX_SOCKET_FUNC_STR    "socket"
/* NOTEL if TX_SOCKET_FLAGS changes, update TXezPostFixSocketFlags(): */
/* {SOCK,FD}_CLOEXEC for same reasons as WSA_FLAG_NO_HANDLE_INHERIT above: */
#  ifdef SOCK_CLOEXEC
#    define TX_SOCKET_FLAGS       SOCK_CLOEXEC
#    define TX_SOCKET_FLAGS_STR " SOCK_CLOEXEC"
#  define TX_SOCKET_FLAGS_POST_STR      ""
#  else /* !SOCK_CLOEXEC */
#    define TX_SOCKET_FLAGS     0
#    define TX_SOCKET_FLAGS_STR ""
#  define TX_SOCKET_FLAGS_POST_STR      " FD_CLOEXEC"
#  endif /* !SOCK_CLOEXEC */
#endif /* !_WIN32 */

static TXbool
TXezPostFixSocketFlags(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                       TXSOCKET skt)
/* Applies TX_SOCKET_FLAGS equivalents (TX_SOCKET_FLAGS_POST_STR) when
 * platform does not support anything useful in former.
 * Returns false on error (should continue).
 */
{
  TXbool        ret;
  MERGE_FUNC_VARS;

  if (TX_SOCKET_FLAGS) goto ok;                 /* already applied */

#ifdef _WIN32
  /* no fcntl(FD_CLOEXEC)? */
  TX_PUSHERROR();
  txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
"Internal warning: No after-socket() remedy for TX_SOCKET_FLAGS; continuing");
  TX_POPERROR();
  goto err;
#else /* !_WIN32 */
  {
    int flags, res;
    HTS_TRACE_VARS;

    HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
      txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                     "fcntl(skt %d, F_SETFD, +FD_CLOEXEC) starting",
                     (int)skt);
    HTS_TRACE_BEFORE_END()
    flags = fcntl(skt, F_GETFD);
    if (flags == -1) flags = 0;                 /* work around error */
    flags |= FD_CLOEXEC;
    res = fcntl(skt, F_SETFD, (long)flags);
    HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
      txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
      "fcntl(skt %d, F_SETFD, +FD_CLOEXEC): %1.3lf sec returned %d err %d=%s",
                     (int)skt, HTS_TRACE_TIME(), res, (int)saveErr,
                     TXgetOsErrName(saveErr, Ques));
    HTS_TRACE_AFTER_END()
    if (res == -1)
      {
        TX_PUSHERROR();
        txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                 "Could not set close-on-exec flag for socket %d; continuing",
                       (int)skt);
        TX_POPERROR();
        goto err;
      }
  }
#endif /* !_WIN32 */
ok:
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

/**********************************************************************/
/*
** Creates an internet socket, binds it to an address, and prepares it for
** subsequent accept() calls by calling listen().  IPv4/IPv6-compatible.
**
** Input: port number desired, or 0 for a random one
** Output: file descriptor of socket, or a negative error
*/
int
TXezServerSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                 const TXsockaddr *sockaddr, int backlog)
{
  TXSOCKET      sock = TXSOCKET_INVALID;
  int           one, ipv6OnlyVal = -1, linger = LINGERTIME;
  TXbool        keepalive = TXbool_True;
  struct linger lin;
  char          addrBuf[TX_SOCKADDR_MAX_STR_SZ];
  TXbool        doTraceAfter = TXbool_False;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  char          sktFlagsBuf[256];

  if (!initsock(pmbuf)) goto err;

  if (sockaddr->storage.ss_family == AF_INET6 &&
      IN6_ARE_ADDR_EQUAL(&in6addr_any,
               &((const struct sockaddr_in6 *)&sockaddr->storage)->sin6_addr))
    ipv6OnlyVal = !sockaddr->okIPv4WithIPv6Any;

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    TXsockaddrToString(pmbuf, sockaddr, addrBuf, sizeof(addrBuf));
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "() for %s linger %d backlog %d keepalive %s IPV6_V6ONLY %s" TX_SOCKET_FLAGS_STR TX_SOCKET_FLAGS_POST_STR " starting",
                   addrBuf, linger, backlog, (keepalive ? "yes" : "no"),
                   (ipv6OnlyVal > 0 ? "on" :
                    (ipv6OnlyVal == 0 ? "off" : "unchanged")));
  HTS_TRACE_BEFORE_END()
  doTraceAfter = TXbool_True;

#ifdef _WIN32
  /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Bug 7570: */
  sock = WSASocket(sockaddr->storage.ss_family, SOCK_STREAM, 0,
                   NULL /*  WSAPROTOCOL_INFO*/,
                   0 /* group */,
                   TX_SOCKET_FLAGS);
#else /* !_WIN32 */
  sock = socket(sockaddr->storage.ss_family, (SOCK_STREAM | TX_SOCKET_FLAGS),
                0);
#endif /* !_WIN32 */
  if (BADSOCK(sock))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + FOE, MERGE_FUNC(func),
                     "Cannot create %s socket: %s",
                    TXaddrFamilyToString(TXsockaddrGetTXaddrFamily(sockaddr)),
                     TXstrerror(TXgeterror()));
      TX_POPERROR();
      goto err;
    }

  TXezPostFixSocketFlags(pmbuf, HTS_NONE /* subsumed above */,
                         MERGE_FUNC(func), sock);

  /* Check for int overflow early, e.g. before connect()/bind(): */
  if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), sock)) goto err;

                       /* MAW 04-26-94 - add SO_LINGER to all sockets */
  if (linger >= 0)
    {
      lin.l_onoff = 1;
      lin.l_linger = linger;
    }
  else
    {
      lin.l_onoff = 0;
      lin.l_linger = 0;
    }
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
                     "Cannot set linger %d on socket %wd: %s; continuing",
                     linger, (EPI_HUGEINT)sock, TXstrerror(saveErr));
      TX_POPERROR();
    }

  one = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&one,sizeof(one)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
                     "Cannot set SO_REUSEADDR on socket %wd: %s; continuing",
                     (EPI_HUGEINT)sock, TXstrerror(saveErr));
      TX_POPERROR();
    }

  if (keepalive)
    {
      one = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&one,sizeof(one))
          != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
                       "Cannot set SO_KEEPALIVE on socket %wd: %s; continuing",
                         (EPI_HUGEINT)sock, TXstrerror(saveErr));
          TX_POPERROR();
        }
    }

  if (ipv6OnlyVal >= 0)
    {
#ifdef IPV6_V6ONLY
      if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&ipv6OnlyVal,
                     sizeof(ipv6OnlyVal)) != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot %s IPV6_V6ONLY on socket %wd: %s",
                         (ipv6OnlyVal > 0 ? "set" : "clear"),
                         (EPI_HUGEINT)sock, TXstrerror(saveErr));
          TX_POPERROR();
          goto err;
        }
#else /* !IPV6_V6ONLY */
      /* This error is unlikely to be reached, as all known platforms
       * that do not support IPV6_V6ONLY also do not support AF_INET6
       * sockets anyway (i.e. Linux 2.4.9):
       */
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "IPV6_V6ONLY unsupported on this platform");
      TXseterror(TXERR_INVALID_PARAMETER);
      goto err;
#endif /* !IPV6_V6ONLY */
    }

  if (bind(sock, (const struct sockaddr *)&sockaddr->storage,
           TXsockaddrGetSockaddrSize(sockaddr)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot bind server socket to %s: %s",
                     (TXsockaddrToString(pmbuf, sockaddr, addrBuf,
                                         sizeof(addrBuf)), addrBuf),
                     TXstrerror(TXgeterror()));
      TX_POPERROR();
      goto err;
    }

  if (backlog > 0 && listen(sock, backlog) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot listen() on %s server socket: %s",
                     (TXsockaddrToString(pmbuf, sockaddr, addrBuf,
                                         sizeof(addrBuf)), addrBuf),
                     TXstrerror(TXgeterror()));
      TX_POPERROR();
      goto err;
   }

  goto finally;

err:
  TX_PUSHERROR();
  TXezCloseSocket(pmbuf, (TXtraceSkt)0, __FUNCTION__, sock);
  sock = TXSOCKET_INVALID;
  TX_POPERROR();
finally:
  if (doTraceAfter)
    {
      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
        /* Reporting actual socket flags (via TXezGetSocketFlags())
         * even though we requested {SOCK,FD}_CLOEXEC ensures we got
         * them; could be using SOCK_CLOEXEC on a pre-2.6.27 kernel
         * that does not support it -- and might silently ignore it.
         */
        TXezGetSocketFlags(sock, sktFlagsBuf, sizeof(sktFlagsBuf));
        TXsockaddrToString(pmbuf, sockaddr, addrBuf, sizeof(addrBuf));
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                       TX_SOCKET_FUNC_STR "() for %s linger %d backlog %d keepalive %s IPV6_V6ONLY %s" TX_SOCKET_FLAGS_STR TX_SOCKET_FLAGS_POST_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                       addrBuf, linger, backlog, (keepalive ? "yes" : "no"),
                       (ipv6OnlyVal > 0 ? "on" :
                        (ipv6OnlyVal == 0 ? "off" : "unchanged")),
                       HTS_TRACE_TIME(), (EPI_HUGEINT)sock, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
      HTS_TRACE_AFTER_END()
    }
  return(sock);
}

/**********************************************************************/

int
TXezServerSocketNonBlocking(
        TXPMBUF *pmbuf,                 /* for putmsgs */
        TXtraceSkt traceSkt,
        const char *func,               /* C function for `traceSkt' msgs */
        const TXsockaddr *sockaddr,     /* host, port optional */
        const char *hostForMsgs,        /* (opt.) host to print for msgs */
        int linger,                     /* >= 0: set linger seconds */
        int backlog,                    /* listen() backlog (<=0: no listen)*/
        TXbool keepalive)               /* set SO_KEEPALIVE */
/* Returns -1 on error.  IPv4/IPv6-compatible.
 */
{
  TXSOCKET      sock = TXSOCKET_INVALID;
  int           on, ipv6OnlyVal = -1;
  struct linger lin;
#ifdef macintosh
  long          ioarg;
#else /* !macintosh */
  ulong         ioarg;
#endif /* !macintosh */
  char          *d;
  char          hostBuf[TX_SOCKADDR_MAX_STR_SZ];
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  TXbool        doTraceAfter = TXbool_False;
  char          sktFlagsBuf[256];

  if (!initsock(pmbuf)) goto err;
  if (!hostForMsgs)
    {
      /* We will ultimately be printing port too, so use TXsockaddrToString()
       * not TXsockaddrToStringIP() so we get brackets for IPv6:
       */
      TXsockaddrToString(TXPMBUF_SUPPRESS, sockaddr, hostBuf, sizeof(hostBuf));
      /* But since we print port ourselves (`hostForMsgs' may be used),
       * chop off port:
       */
      d = strrchr(hostBuf, ':');
      if (d) *d = '\0';
      hostForMsgs = hostBuf;
    }

  if (sockaddr->storage.ss_family == AF_INET6 &&
      IN6_ARE_ADDR_EQUAL(&in6addr_any,
               &((const struct sockaddr_in6 *)&sockaddr->storage)->sin6_addr))
    ipv6OnlyVal = !sockaddr->okIPv4WithIPv6Any;

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   /* Print port as int, in case -1 (error).
                    * Suppress ...GetPort() message; will likely be
                    * redundant with other messages if bad `sockaddr':
                    */
                  TX_SOCKET_FUNC_STR "() for %s:%d linger %d backlog %d keepalive %s IPV6_V6ONLY %s" TX_SOCKET_FLAGS_STR TX_SOCKET_FLAGS_POST_STR " starting",
                   hostForMsgs,
                   (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                   linger, backlog, (keepalive ? "yes" : "no"),
                   (ipv6OnlyVal > 0 ? "on" :
                    (ipv6OnlyVal == 0 ? "off" : "unchanged")));
  HTS_TRACE_BEFORE_END()
  doTraceAfter = TXbool_True;

  /* Create socket: */
#ifdef _WIN32
  /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Bug 7570: */
  sock = WSASocket(sockaddr->storage.ss_family, SOCK_STREAM, IPPROTO_TCP,
                   NULL /*  WSAPROTOCOL_INFO*/,
                   0 /* group */,
                   TX_SOCKET_FLAGS);
#else /* !_WIN32 */
  sock = socket(sockaddr->storage.ss_family, (SOCK_STREAM | TX_SOCKET_FLAGS),
                IPPROTO_TCP);
#endif /* !_WIN32 */
  if (BADSOCK(sock))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot create %s server socket for %s:%d: %s",
                    TXaddrFamilyToString(TXsockaddrGetTXaddrFamily(sockaddr)),
                     hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  TXezPostFixSocketFlags(pmbuf, HTS_NONE /* subsumed above */,
                         MERGE_FUNC(func), sock);

  /* Check for int overflow early, before e.g. connect()/bind(): */
  if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), sock)) goto err;

  /* Set non-blocking: */
  ioarg = 1;
  if (ioctlsocket(sock, FIONBIO, &ioarg) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                    "Cannot set server socket %wd for %s:%d non-blocking: %s",
                     (EPI_HUGEINT)sock, hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  /* Set SO_REUSEADDR: */
  on = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
     "Cannot set SO_REUSEADDR on server socket %wd for %s:%d: %s; continuing",
                     (EPI_HUGEINT)sock, hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
    }

  /* Set or clear SO_LINGER as requested: */
  if (linger >= 0)
    {
      lin.l_onoff = 1;
      lin.l_linger = linger;
    }
  else
    {
      lin.l_onoff = 0;
      lin.l_linger = 0;
    }
  if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&lin, sizeof(lin)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
         "Cannot %s SO_LINGER on server socket %wd for %s:%d: %s; continuing",
                     (linger >= 0 ? "set" : "clear"), (EPI_HUGEINT)sock,
                     hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
    }

  /* Set SO_KEEPALIVE if requested: */
  if (keepalive)
    {
      on = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char *)&on, sizeof(on))
          != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
     "Cannot set SO_KEEPALIVE on server socket %wd for %s:%d: %s; continuing",
                         (EPI_HUGEINT)sock, hostForMsgs,
                         (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                         TXstrerror(saveErr));
          TX_POPERROR();
        }
    }

  /* Set IPV6_V6ONLY: */
  if (ipv6OnlyVal >= 0)
    {
#ifdef IPV6_V6ONLY
      if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&ipv6OnlyVal,
                     sizeof(ipv6OnlyVal)) != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot %s IPV6_V6ONLY on socket %wd: %s",
                         (ipv6OnlyVal > 0 ? "set" : "clear"),
                         (EPI_HUGEINT)sock, TXstrerror(saveErr));
          TX_POPERROR();
          goto err;
        }
#else /* !IPV6_V6ONLY */
      /* This error is unlikely to be reached, as all known platforms
       * that do not support IPV6_V6ONLY also do not support AF_INET6
       * sockets anyway (i.e. Linux 2.4.9):
       */
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "IPV6_V6ONLY unsupported on this platform");
      TXseterror(TXERR_INVALID_PARAMETER);
      goto err;
#endif /* !IPV6_V6ONLY */
    }

  /* Bind address to socket: */
  if (bind(sock, (const struct sockaddr *)&sockaddr->storage,
           TXsockaddrGetSockaddrSize(sockaddr)) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot bind server socket %wd to %s:%d: %s",
                     (EPI_HUGEINT)sock, hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  /* listen() on socket: */
  if (backlog > 0 && listen(sock, backlog) != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot listen on server socket %wd for %s:%d: %s",
                     (EPI_HUGEINT)sock, hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  goto finally;

err:
  TX_PUSHERROR();
  TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), sock);
  sock = TXSOCKET_INVALID;
  TX_POPERROR();
finally:
  if (doTraceAfter)
    {
      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
        TXezGetSocketFlags(sock, sktFlagsBuf, sizeof(sktFlagsBuf));
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                       TX_SOCKET_FUNC_STR "() for %s:%d linger %d backlog %d keepalive %s IPV6_V6ONLY %s" TX_SOCKET_FLAGS_STR TX_SOCKET_FLAGS_POST_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                       hostForMsgs,
                       (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                       linger, backlog, (keepalive ? "yes" : "no"),
                       (ipv6OnlyVal > 0 ? "on" :
                        (ipv6OnlyVal == 0 ? "off" : "unchanged")),
                       HTS_TRACE_TIME(), (EPI_HUGEINT)sock, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
      HTS_TRACE_AFTER_END()
    }
  return(sock);
}

TXbool
TXezGetLocalSockaddr(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                     int fd, TXsockaddr *sockaddr)
/* Copies local IP address and port of `fd' to `*sockaddr'.
 * Thread-safe.  IPv4/IPv6-compatible.
 * Returns false on error (with `*sockaddr' init'd).
 */
{
  EPI_GETSOCKNAME_LEN_TYPE      sockaddrLen;
  int                           ret, res;
  char                          addrBuf[TX_SOCKADDR_MAX_STR_SZ];
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

  if (!initsock(pmbuf)) goto err;

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   "getsockname(%d) starting", fd);
  HTS_TRACE_BEFORE_END()
  TXsockaddrInit(sockaddr);
  TX_EZSOCK_CLEAR_ERROR();
  sockaddrLen = sizeof(sockaddr->storage);
  res = getsockname(fd, (struct sockaddr *)&sockaddr->storage, &sockaddrLen);
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "getsockname(%d): %1.3lf sec returned %d err %d=%s %s",
                   fd, HTS_TRACE_TIME(), res, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques),
                   (TXsockaddrToString(pmbuf, sockaddr, addrBuf,
                                       sizeof(addrBuf)), addrBuf));
  HTS_TRACE_AFTER_END()
  if (res != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot get local socket address: %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  TXsockaddrInit(sockaddr);
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXezGetRemoteSockaddr(TXPMBUF *pmbuf, int fd, TXsockaddr *sockaddr)
/* Copies remote IP address and port of `fd' to `*sockaddr'.
 * Thread-safe.  IPv4/IPv6-compatible.
 * Returns false on error.
 */
{
  EPI_GETSOCKNAME_LEN_TYPE      sockaddrLen;
  TXbool                        ret;

  if (!initsock(pmbuf)) goto err;
  TXsockaddrInit(sockaddr);
  TX_EZSOCK_CLEAR_ERROR();
  sockaddrLen = sizeof(sockaddr->storage);
  if (getpeername(fd, (struct sockaddr *)&sockaddr->storage, &sockaddrLen)
      != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot get remote socket address: %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  ret = TXbool_True;
  goto finally;

err:
  TXsockaddrInit(sockaddr);
  ret = TXbool_False;
finally:
  return(ret);
}

TXbool
TXezGetIPProtocolsAvailable(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                            const char *func, TXbool *okIPv4, TXbool *okIPv6)
/* Returns false on error (unable to determine IPvN status).
 */
{
  static TXATOMINT      supportIPv4 = -1, supportIPv6 = -1;
  TXSOCKET              skt = TXSOCKET_INVALID;
  TXbool                ret;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  char                  sktFlagsBuf[256];

  /* While getaddrinfo() may support IPv6, the kernel may not be able
   * to talk IPv6 so verify with an actual socket (e.g. Linux 2.4.9
   * gives `Address family not supported').  This still does not
   * guarantee connecting, routing, DNS etc. work; e.g.  Linux
   * 2.6.9-42 can open an IPv6 socket but fails to connect to a
   * non-loopback, non-local IPv6 address (`Cannot assign requested
   * address'; but *can* connect to ::1 or its own address).
   */

  /* IPv4: */
  if (supportIPv4 >= 0)                         /* already determined */
    *okIPv4 = supportIPv4;
  else                                          /* not determined yet */
    {
      if (!initsock(pmbuf)) goto err;
      HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
        txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                       TX_SOCKET_FUNC_STR "(AF_INET, SOCK_STREAM, 0)"
                       TX_SOCKET_FLAGS_STR " starting");
      HTS_TRACE_BEFORE_END()
      TX_EZSOCK_CLEAR_ERROR();
#ifdef _WIN32
      /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Bug 7570: */
      skt = WSASocket(AF_INET, SOCK_STREAM, 0,
                      NULL /*  WSAPROTOCOL_INFO*/,
                      0 /* group */,
                      TX_SOCKET_FLAGS);
#else /* !_WIN32 */
      skt = socket(AF_INET, (SOCK_STREAM | TX_SOCKET_FLAGS), 0);
#endif /* !_WIN32 */
      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
        TXezGetSocketFlags(skt, sktFlagsBuf, sizeof(sktFlagsBuf));
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                       TX_SOCKET_FUNC_STR "(AF_INET, SOCK_STREAM, 0)"
             TX_SOCKET_FLAGS_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                       HTS_TRACE_TIME(), (EPI_HUGEINT)skt, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
      HTS_TRACE_AFTER_END()
      *okIPv4 = supportIPv4 = !BADSOCK(skt);
      /* closing now, so no point in TXezPostFixSocketFlags() */
      TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
      skt = TXSOCKET_INVALID;
    }

  /* IPv6: */
  if (TX_IPv6_ENABLED(TXApp))
    {
      if (supportIPv6 >= 0)                     /* already determined */
        *okIPv6 = supportIPv6;
      else                                      /* not determined yet */
        {
          if (!initsock(pmbuf)) goto err;       /* parallels deinit @ close */
          HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
            txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                           TX_SOCKET_FUNC_STR "(AF_INET6, SOCK_STREAM, 0)"
                           TX_SOCKET_FLAGS_STR " starting");
          HTS_TRACE_BEFORE_END()
          TX_EZSOCK_CLEAR_ERROR();
#ifdef _WIN32
          /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Bug 7570: */
          skt = WSASocket(AF_INET6, SOCK_STREAM, 0,
                          NULL /*  WSAPROTOCOL_INFO*/,
                          0 /* group */,
                          TX_SOCKET_FLAGS);
#else /* !_WIN32 */
          skt = socket(AF_INET6, (SOCK_STREAM | TX_SOCKET_FLAGS), 0);
#endif /* !_WIN32 */
          HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
            TXezGetSocketFlags(skt, sktFlagsBuf, sizeof(sktFlagsBuf));
            txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                           TX_SOCKET_FUNC_STR "(AF_INET6, SOCK_STREAM, 0)"
             TX_SOCKET_FLAGS_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                           HTS_TRACE_TIME(), (EPI_HUGEINT)skt, (int)saveErr,
                           TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
          HTS_TRACE_AFTER_END()
          *okIPv6 = supportIPv6 = !BADSOCK(skt);
          /* closing now, so no point in TXezPostFixSocketFlags() */
          TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
          skt = TXSOCKET_INVALID;
        }
    }
  else
    *okIPv6 = TXbool_False;

  ret = TXbool_True;
  goto finally;

err:
  ret = *okIPv4 = *okIPv6 = TXbool_False;
finally:
  TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
  skt = TXSOCKET_INVALID;
  return(ret);
}

/**********************************************************************/
/*
** Returns a connected client socket, and waits timeout seconds during
** connect.
**
** Input: host name and port number to connect to
** Output: file descriptor of CONNECTED socket, or a negative error
** (-2 on connection refused, else -1)
** Note that returned socket will be non-blocking if timeout given
*/
int
TXezClientSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                 const TXsockaddr *sockaddr, TXbool noConnRefusedMsg,
                 double timeout)
{
  TXSOCKET      sock = TXSOCKET_INVALID;
  int           rc, res;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  TXbool        gotConnRefused = TXbool_False;
  struct linger lin, orgLin;
  char          sockBuf[TX_SOCKADDR_MAX_STR_SZ];
  char          sktFlagsBuf[256];

  if (!initsock(pmbuf)) goto err;
  if (timeout >= 0.0 && usepoll) timeout = -1.0;

  if (!TX_IPv6_ENABLED(TXApp) &&
      sockaddr->storage.ss_family == AF_INET6)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func), IPv6Unsupported);
      goto err;
    }

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_STREAM, 0)"
                   TX_SOCKET_FLAGS_STR " starting",
                   TXAFFamilyToString(sockaddr->storage.ss_family));
  HTS_TRACE_BEFORE_END()
  TX_EZSOCK_CLEAR_ERROR();
#ifdef _WIN32
  /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Buf 75: */
  sock = WSASocket(sockaddr->storage.ss_family, SOCK_STREAM, 0,
                   NULL /*  WSAPROTOCOL_INFO*/,
                   0 /* group */,
                   TX_SOCKET_FLAGS);
#else /* !_WIN32 */
  sock = socket(sockaddr->storage.ss_family, (SOCK_STREAM | TX_SOCKET_FLAGS),
                0);
#endif /* !_WIN32 */
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
    TXezGetSocketFlags(sock, sktFlagsBuf, sizeof(sktFlagsBuf));
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_STREAM, 0)"
             TX_SOCKET_FLAGS_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                   TXAFFamilyToString(sockaddr->storage.ss_family),
                   HTS_TRACE_TIME(), (EPI_HUGEINT)sock, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
  HTS_TRACE_AFTER_END()
  if (BADSOCK(sock))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot create %s socket to connect to %s: %s",
                    TXaddrFamilyToString(TXsockaddrGetTXaddrFamily(sockaddr)),
                     (TXsockaddrToString(pmbuf, sockaddr, sockBuf,
                                         sizeof(sockBuf)), sockBuf),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  TXezPostFixSocketFlags(pmbuf, traceSkt, MERGE_FUNC(func), sock);

  /* Check for int overflow early, e.g. before connect(): */
  if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), sock)) goto err;

                      /* MAW 04-26-94 - add SO_LINGER to all sockets */
  lin.l_onoff = 1;
  lin.l_linger = LINGERTIME;
  orgLin = lin;
  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
         "setsockopt(skt %wd, SOL_SOCKET, SO_LINGER, %s linger=%ld) starting",
                   (EPI_HUGEINT)sock, (lin.l_onoff ? "on" : "off"),
                   (long)lin.l_linger);
  HTS_TRACE_BEFORE_END()
  res = setsockopt(sock, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "setsockopt(skt %wd, SOL_SOCKET, SO_LINGER, %s linger=%ld): %1.3lf sec returned %d=%s err %d=%s",
                   (EPI_HUGEINT)sock, (orgLin.l_onoff ? "on" : "off"),
                   (long)orgLin.l_linger, HTS_TRACE_TIME(), res,
                   (res == 0 ? "Ok" : "failed"),
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()

  if (timeout >= 0.0)
    {
#ifdef macintosh
      long              ioarg = 1, orgIoarg;
#else
      unsigned long     ioarg = 1, orgIoarg;
#endif

      orgIoarg = ioarg;
      HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
        txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                       "ioctlsocket(skt %wd, FIONBIO, &%ld) starting",
                       (EPI_HUGEINT)sock, (long)ioarg);
      HTS_TRACE_BEFORE_END()
      TX_EZSOCK_CLEAR_ERROR();
      res = ioctlsocket(sock, FIONBIO, &ioarg);
      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
   "ioctlsocket(skt %wd, FIONBIO, &%ld): %1.3lf sec returned %d=%s err %d=%s",
                       (EPI_HUGEINT)sock, (long)orgIoarg, HTS_TRACE_TIME(),
                       res, (res == 0 ? "Ok" : "failed"),
                       (int)saveErr, TXgetOsErrName(saveErr, Ques));
      HTS_TRACE_AFTER_END()
      if (res != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot set socket non-blocking: %s",
                         TXstrerror(TXgeterror()));
          TX_POPERROR();
          goto err;
        }
    }

  rc = TXezConnectSocket(pmbuf, traceSkt, MERGE_FUNC(func), sock, sockaddr,
                         NULL, noConnRefusedMsg);
  switch (rc)
    {
    case -2:                                    /* connection refused */
      gotConnRefused = TXbool_True;
      /* fall through */
    case -1:                                    /* error */
      goto err;
    case 0:                                     /* ok */
      break;
    default:
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "Unknown return %d from TXezConnectSocket()", rc);
      TX_POPERROR();
      goto err;
    }

  if (timeout >= 0.0)
    {
      int       waitWriteRes;

      /* Socket is non-blocking due to timeout, so wait for connect: */
      waitWriteRes = TXezWaitForSocketWritability(pmbuf, traceSkt,
                                             MERGE_FUNC(func), sock, timeout);
      if (waitWriteRes == 1)                    /* `sock' writable */
        {
          int           sockErr;
          socklen_t     len;

          /* If connection refused (especially loopback/same-host), we
           * may get socket-writable-ok back from
           * TXezWaitForSocketWritability().  Check for error:
           * wtf make this an ezsock function?  part of ...TXezWaitFor...?
           */
          sockErr = 0;
          len = sizeof(sockErr);
          HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
            txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
               "getsockopt(%wd, SOL_SOCKET, SO_ERROR, &sockErr, ...) starting",
                           (EPI_HUGEINT)sock);
          HTS_TRACE_BEFORE_END()
          TX_EZSOCK_CLEAR_ERROR();
          res = getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&sockErr,&len);
          HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
            txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                           "getsockopt(%wd, SOL_SOCKET, SO_ERROR, &sockErr, ...): %1.3lf sec returned %d err %d=%s sockErr %d=%s",
                           (EPI_HUGEINT)sock, HTS_TRACE_TIME(), res,
                           (int)saveErr, TXgetOsErrName(saveErr, Ques),
                           (int)sockErr, TXgetOsErrName(sockErr, Ques));
          HTS_TRACE_AFTER_END()
          if (res != 0 && sockErr == 0) sockErr = TXgeterror(); /* Sun/SysV */
          /* Mainly check for ECONNREFUSED (e.g. loopback/same-host),
           * but bail on any error?
           */
          if (sockErr == 0 || sockErr == TXERR_EWOULDBLOCK) goto finally;
          TXseterror(sockErr);
        }
      if (TXgeterror() == TXERR_ECONNREFUSED)
        gotConnRefused = TXbool_True;
      /* Timeout, connection refused etc.: */
      if (!noConnRefusedMsg || !gotConnRefused)
        {
          TX_PUSHERROR();
          TXsockaddrToString(pmbuf, sockaddr, sockBuf, sizeof(sockBuf));
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot connect to %s: %s", sockBuf,
                         (waitWriteRes == 0 ? "Timeout":TXstrerror(saveErr)));
          TX_POPERROR();
        }
      goto err;
    }
  goto finally;

err:
  if (!BADSOCK(sock))
    {
      TX_PUSHERROR();
      TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), sock);
      sock = TXSOCKET_INVALID;
      TX_POPERROR();
    }
finally:
  if (BADSOCK(sock) && gotConnRefused) sock = -2;
  return(sock);
}

int
TXezClientSocketNonBlocking(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                            const char *func, const TXsockaddr *sockaddr,
                            const char *hostForMsgs, TXbool noConnRefusedMsg,
                            int linger)
/* Opens and connects a socket in non-blocking mode and returns it, or
 * -1 on error, or -2 on connection-refused.
 * NOTE that socket may not yet be finished connecting;
 * must do select() for read/write (and exception if Windows).
 * If `noconnmsg' is non-zero, will not report ECONNREFUSED errors.
 * If `linger' is >= 0, SO_LINGER option set to that time in seconds.
 * Re-tries interrupted system calls.
 */
{
  TXSOCKET      skt = TXSOCKET_INVALID;
  int           res, ipv6OnlyVal = -1;
  TXbool        gotConnRefused = TXbool_False;
  struct linger lin, orgLin;
#ifdef macintosh
  long          ioarg = 1, orgIoarg;
#else
  ulong         ioarg = 1, orgIoarg;
#endif
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  char          *d;
  char          hostBuf[TX_SOCKADDR_MAX_STR_SZ];
  char          sktFlagsBuf[256];

  if (!initsock(pmbuf)) goto err;

  if (!hostForMsgs)
    {
      /* We will ultimately be printing port too, so use TXsockaddrToString()
       * not TXsockaddrToStringIP() so we get brackets for IPv6:
       */
      TXsockaddrToString(pmbuf, sockaddr, hostBuf, sizeof(hostBuf));
      /* But since we print port ourselves (`hostForMsgs' may be used),
       * chop off port:
       */
      d = strrchr(hostBuf, ':');
      if (d) *d = '\0';
      hostForMsgs = hostBuf;
    }

  /* IPv4-mapped addresses require IPV6_V6ONLY to be off, or they will
   * result in `Network is unreachable' errors at connect() (even if
   * there is an IPv6 route for IPv4-mapped addresses?  thus it is
   * impossible to send an over-the-wire IPv6 packet to an IPv4-mapped
   * address?).  Because such addresses use IPv4 over the wire but still
   * use IPv6 sockets, we require both IPv4 and IPv6 to be enabled.
   * This should have been checked in TXfetchCheckIPProtocol() for
   * IPv4-mapped addresses; we assume so here.
   *
   * wtf could also set `ipv6OnlyVal' if `okIPv4WithIPv6Any' applies?
   * probably will not matter since we connect() not bind() here?
   */
  if (TXsockaddrIsIPv4Mapped(sockaddr))
    ipv6OnlyVal = 0;                            /* allow IPv4 too */

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_STREAM, 0)"
                   TX_SOCKET_FLAGS_STR " starting",
                   TXAFFamilyToString(sockaddr->storage.ss_family));
  HTS_TRACE_BEFORE_END()
  TX_EZSOCK_CLEAR_ERROR();
#ifdef _WIN32
  /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Buf 75: */
  skt = WSASocket(sockaddr->storage.ss_family, SOCK_STREAM, 0,
                  NULL /*  WSAPROTOCOL_INFO*/,
                  0 /* group */,
                  TX_SOCKET_FLAGS);
#else /* !_WIN32 */
  skt = socket(sockaddr->storage.ss_family, (SOCK_STREAM | TX_SOCKET_FLAGS),
               0);
#endif /* !_WIN32 */
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
    TXezGetSocketFlags(skt, sktFlagsBuf, sizeof(sktFlagsBuf));
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_STREAM, 0)"
             TX_SOCKET_FLAGS_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                   TXAFFamilyToString(sockaddr->storage.ss_family),
                   HTS_TRACE_TIME(), (EPI_HUGEINT)skt, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
  HTS_TRACE_AFTER_END()
  if (BADSOCK(skt))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                     "Cannot create %s socket to connect to %s:%d: %s",
                    TXaddrFamilyToString(TXsockaddrGetTXaddrFamily(sockaddr)),
                     hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  TXezPostFixSocketFlags(pmbuf, traceSkt, MERGE_FUNC(func), skt);

  /* Check for int overflow early, e.g. before connect(): */
  if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), skt)) goto err;

  /* Set IPV6_V6ONLY if requested: */
  if (ipv6OnlyVal >= 0)
    {
#ifdef IPV6_V6ONLY
      if (setsockopt(skt, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&ipv6OnlyVal,
                     sizeof(ipv6OnlyVal)) != 0)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot %s IPV6_V6ONLY on socket %wd: %s",
                         (ipv6OnlyVal > 0 ? "set" : "clear"),
                         (EPI_HUGEINT)skt, TXstrerror(saveErr));
          TX_POPERROR();
          goto err;
        }
#else /* !IPV6_V6ONLY */
      /* This error is unlikely to be reached, as all known platforms
       * that do not support IPV6_V6ONLY also do not support AF_INET6
       * sockets anyway (i.e. Linux 2.4.9):
       */
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "IPV6_V6ONLY unsupported on this platform");
      TXseterror(TXERR_INVALID_PARAMETER);
      goto err;
#endif /* !IPV6_V6ONLY */
    }

  if (linger >= 0)                              /* set linger */
    {
      lin.l_onoff = 1;
      lin.l_linger = linger;
    }
  else
    {
      lin.l_onoff = 0;
      lin.l_linger = 0;
    }
  orgLin = lin;
  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
         "setsockopt(skt %wd, SOL_SOCKET, SO_LINGER, %s linger=%ld) starting",
                   (EPI_HUGEINT)skt, (lin.l_onoff ? "on" : "off"),
                   (long)lin.l_linger);
  HTS_TRACE_BEFORE_END()
  res = setsockopt(skt, SOL_SOCKET, SO_LINGER, (char *)&lin, sizeof(lin));
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "setsockopt(skt %wd, SOL_SOCKET, SO_LINGER, %s linger=%ld): %1.3lf sec returned %d err %d=%s",
                   (EPI_HUGEINT)skt, (orgLin.l_onoff ? "on" : "off"),
                   (long)orgLin.l_linger, HTS_TRACE_TIME(), res, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()
  if (res != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MWARN, MERGE_FUNC(func),
"Cannot %s SO_LINGER on client socket %wd to connect to %s:%d: %s; continuing",
                     (linger >= 0 ? "set" : "clear"), (EPI_HUGEINT)skt,
                     hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
    }

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   "ioctlsocket(skt %wd, FIONBIO, &%ld) starting",
                   (EPI_HUGEINT)skt, (long)ioarg);
  HTS_TRACE_BEFORE_END()
  TX_EZSOCK_CLEAR_ERROR();
  orgIoarg = ioarg;
  res = ioctlsocket(skt, FIONBIO, &ioarg);      /* set non-blocking mode */
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
      "ioctlsocket(skt %wd, FIONBIO, &%ld): %1.3lf sec returned %d err %d=%s",
                   (EPI_HUGEINT)skt, (long)orgIoarg, HTS_TRACE_TIME(), res,
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()
  if (res != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                 "Cannot set socket %wd non-blocking to connect to %s:%d: %s",
                     (EPI_HUGEINT)skt, hostForMsgs,
                     (int)TXsockaddrGetPort(TXPMBUF_SUPPRESS, sockaddr),
                     TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  res = TXezConnectSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt, sockaddr,
                          hostForMsgs, noConnRefusedMsg);
  switch (res)
    {
    case -2:                                    /* connection refused */
      gotConnRefused = TXbool_True;
      /* fall through */
    case -1:                                    /* error */
      goto err;
    case 0:                                     /* success */
      break;
    default:
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "Unknown return %d from TXezConnectSocket()", res);
      TX_POPERROR();
      goto err;
    }

  goto finally;

err:
  TX_PUSHERROR();
  TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
  TX_POPERROR();
  skt = (gotConnRefused ? -2 : TXSOCKET_INVALID);
finally:
  return(skt);
}
/**********************************************************************/

int
TXezClientSocketDatagramNonBlocking(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                                    const char *func, TXaddrFamily addrFamily)
/* Opens (but does not connect) a datagram socket in non-blocking mode
 * and returns it, or -1 on error.  NOTE that socket may not yet be
 * finished connecting; must do select() for read/write (and exception
 * if Windows).  Re-tries interrupted system calls.
 * Respects HTS_DATAGRAM.
 */
{
  TXSOCKET              skt = TXSOCKET_INVALID;
  int                   res;
#ifdef macintosh
  long                  ioarg = 1;
#else
  ulong                 ioarg = 1;
#endif
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  int                   afFamily;
  char                  sktFlagsBuf[256];

  if (!(traceSkt & HTS_DATAGRAM)) traceSkt = HTS_NONE;

  if (!initsock(pmbuf)) goto err;

  afFamily = TXaddrFamilyToAFFamily(pmbuf, addrFamily);
  if (afFamily < 0) goto err;

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_DGRAM, 0)"
                   TX_SOCKET_FLAGS_STR " starting",
                   TXAFFamilyToString(afFamily));
  HTS_TRACE_BEFORE_END()
  TX_EZSOCK_CLEAR_ERROR();
#ifdef _WIN32
  /* Use WSASocket() so we can set TX_SOCKET_FLAGS; Buf 75: */
  skt = WSASocket(afFamily, SOCK_DGRAM, 0,
                  NULL /*  WSAPROTOCOL_INFO*/,
                  0 /* group */,
                  TX_SOCKET_FLAGS);
#else /* !_WIN32 */
  skt = socket(afFamily, (SOCK_DGRAM | TX_SOCKET_FLAGS), 0);
#endif /* !_WIN32 */
  FIXERRNO();
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
    TXezGetSocketFlags(skt, sktFlagsBuf, sizeof(sktFlagsBuf));
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   TX_SOCKET_FUNC_STR "(%s, SOCK_DGRAM, 0)"
             TX_SOCKET_FLAGS_STR ": %1.3lf sec returned skt %wd err %d=%s %s",
                   TXAFFamilyToString(afFamily),
                   HTS_TRACE_TIME(), (EPI_HUGEINT)skt, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques), sktFlagsBuf);
  HTS_TRACE_AFTER_END()
  if (BADSOCK(skt))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + FOE, MERGE_FUNC(func),
                     "Cannot create %s datagram socket: %s",
                     TXaddrFamilyToString(addrFamily), TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }

  TXezPostFixSocketFlags(pmbuf, traceSkt, MERGE_FUNC(func), skt);

  /* Check for int overflow early, before any potential connect()/bind(): */
  if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), skt)) goto err;

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   "ioctlsocket(skt %wd, FIONBIO, &%ld) starting",
                   (EPI_HUGEINT)skt, ioarg);
  HTS_TRACE_BEFORE_END()
  TXseterror(0);
  res = ioctlsocket(skt, FIONBIO, &ioarg);
  FIXERRNO();
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_IOCTL)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
      "ioctlsocket(skt %wd, FIONBIO, &%ld): %1.3lf sec returned %d err %d=%s",
                   (EPI_HUGEINT)skt, ioarg, HTS_TRACE_TIME(), res,
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()
  if (res != 0)
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                     "Cannot set datagram socket %wd non-blocking: %s",
                     (EPI_HUGEINT)skt, TXstrerror(saveErr));
      TX_POPERROR();
      goto err;
    }
  goto finally;

err:
  TX_PUSHERROR();
  TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
  skt = TXSOCKET_INVALID;
  TX_POPERROR();
finally:
  return(skt);
}

/* ------------------------------------------------------------------------ */

int
TXezConnectSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                  int skt, const TXsockaddr *sockaddr, const char *hostForMsgs,
                  TXbool noConnRefusedMsg)
/* Connects socket `skt' to `sockaddr' (or disconnects if NULL).
 * `hostForMsgs' is optional host string for messages.  `sockaddr', `pmbuf',
 * `func' optional.
 * Returns 0 if ok, -1 on error, -2 on connection-refused.
 */
{
  int           tries = 0, rc, ret;
  TXERRTYPE     errnum;
  char          addrBuf[TX_SOCKADDR_MAX_STR_SZ];
  TXsockaddr    sockaddrActual;
  EPI_SOCKLEN_T sa_size;
  unsigned      port;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

  if (!initsock(pmbuf)) goto err;
  if (sockaddr)
    {
      TXsockaddrToString(pmbuf, sockaddr, addrBuf, sizeof(addrBuf));
      sockaddrActual = *sockaddr;
    }
  else
    {
      TXsockaddrInit(&sockaddrActual);
      TXstrncpy(addrBuf, "disconnect", sizeof(addrBuf));
    }
  port = TXsockaddrGetPort(pmbuf, &sockaddrActual);

  TX_EZSOCK_CLEAR_ERROR();

#ifndef _WIN32
again:
#endif /* _WIN32 */
  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    if (hostForMsgs)
      txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                     "connect(skt %d, %s:%u = %s) starting",
                     skt, hostForMsgs, port, addrBuf);
    else
      txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                     "connect(skt %d, %s) starting",
                     skt, addrBuf);
  HTS_TRACE_BEFORE_END()
  TXclearError();
  rc = connect(skt, (const struct sockaddr *)&sockaddrActual.storage,
               TXsockaddrGetSockaddrSize(&sockaddrActual));
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
  {
    TXsockaddr  localAddr;
    char        localAddrBuf[TX_SOCKADDR_MAX_STR_SZ];
    double      theTime;

    theTime = HTS_TRACE_TIME();                 /* immediately */
    if (rc == 0)                                /* success */
      {
        TXezGetLocalSockaddr(pmbuf, (TXtraceSkt)0 /* we are already tracing*/,
                             NULL, skt, &localAddr);
        TXsockaddrToString(pmbuf, &localAddr, localAddrBuf,
                           sizeof(localAddrBuf));
      }
    else
      TXstrncpy(localAddrBuf, Ques, sizeof(localAddrBuf));
    if (hostForMsgs)
      txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                     "connect(skt %d, %s:%u = %s): %1.3lf sec returned %d=%s local addr %s err %d=%s",
                     skt, hostForMsgs,port, addrBuf, theTime, rc,
                     (rc == 0 ? "Ok" : "failed"), localAddrBuf,
                     (int)saveErr, TXgetOsErrName(saveErr, Ques));
    else
      txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
     "connect(skt %d, %s): %1.3lf sec returned %d=%s local addr %s err %d=%s",
                     skt, addrBuf, theTime, rc,
                     (rc == 0 ? "Ok" : "failed"), localAddrBuf,
                     (int)saveErr, TXgetOsErrName(saveErr, Ques));
  }
  HTS_TRACE_AFTER_END()

  FORWINDOWS(errno = ezsockerrno(-1));
  errnum = TXgeterror();
  if (rc != 0 &&
#ifdef _WIN32
      errnum != WSAEINPROGRESS && errnum != WSAEWOULDBLOCK
#else /* !_WIN32 */
      errnum != EINPROGRESS
#  ifdef EWOULDBLOCK
      && errnum != EWOULDBLOCK
#  endif
#endif /* !_WIN32 */
      )
    {
#ifndef _WIN32
      /* KNG 20040713 Solaris 2.8 sometimes gets EINTR: */
      if (errnum == EINTR && ++tries < 5) goto again;
      /* KNG 20040921 But re-trying connect() may then fail as already
       * connected: then return as if success.  Since the re-connect
       * may still be needed on some non-BSD OS (UNP p. 413) we
       * re-tried it anyway.
       */
      if (tries && (0
#  ifdef EISCONN
                    || errnum == EISCONN
#  endif /* EISCONN */
#  ifdef EALREADY
                    || errnum == EALREADY
#  endif /* EALREADY */
            ))
        goto ok;
#endif /* !_WIN32 */
      rc = (errnum == TXERR_ECONNREFUSED);
      if (!noConnRefusedMsg || !rc)
        {
          const char    *errSfx = "";

          TX_PUSHERROR();
          if (sockaddr)
            {
              struct sockaddr_in6       *sa6;

              /* Add hint for common problem: */
              sa6 = (sockaddr->storage.ss_family == AF_INET6 ?
                     ((struct sockaddr_in6 *)&sockaddr->storage) : NULL);
              if (sa6 &&
                  IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr) &&
                  sa6->sin6_scope_id == 0 &&
                  errnum == EINVAL)
                errSfx = " (link-local address may need scope identifier)";
              /* Report error: */
              if (hostForMsgs)
                txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                               "Cannot connect to %s:%u: %s%s",
                               hostForMsgs, port, TXstrerror(errnum),
                               errSfx);
              else
                txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                               "Cannot connect to %s: %s%s",
                               addrBuf, TXstrerror(errnum), errSfx);
            }
          else
            txpmbuf_putmsg(pmbuf, MERR + FWE, MERGE_FUNC(func),
                           "Cannot disconnect socket: %s%s",
                           TXstrerror(errnum), errSfx);
          TX_POPERROR();
        }
      if (rc)                                   /* connection refused */
        {
          ret = -2;
          goto finally;
        }
      goto err;
    }
#ifndef _WIN32
ok:
#endif /* !_WIN32 */
  ret = 0;
  goto finally;

err:
  ret = -1;
finally:
  return(ret);
}

/* ------------------------------------------------------------------------ */

TXbool
TXezShutdownSocket(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                   int skt, int how)
/* how: SHUT_RD, SHUT_WR, or SHUT_RDWR
 * Returns false on error.
 */
{
  int           rc;
  const char    *howStr;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  TXERR         errNum;

  switch (how)
    {
    case SHUT_RD:       howStr = "SHUT_RD";     break;
    case SHUT_WR:       howStr = "SHUT_WR";     break;
    case SHUT_RDWR:     howStr = "SHUT_RDWR";   break;
    default:            howStr = "?";           break;
    }

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   "shutdown(skt %d, %s) starting", skt, howStr);
  HTS_TRACE_BEFORE_END()
  TX_EZSOCK_CLEAR_ERROR();
  rc = shutdown(skt, how);
  FIXERRNO();
  HTS_TRACE_AFTER_START(HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
             "shutdown(skt %d, %s): %1.3lf sec returned %d=%s err %d=%s",
                   skt, howStr, HTS_TRACE_TIME(), rc,
                   (rc == 0 ? "Ok" : "failed"),
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()

  errNum = TXgeterror();
  if (rc != 0
#ifdef _WIN32
      && errNum != WSAEWOULDBLOCK
#else /* !_WIN32 */
#  ifdef EAGAIN
      && errNum != EAGAIN
#  endif /* EAGAIN */
#endif /* !_WIN32 */
      /* We should not be calling shutdown() on an unconnected socket,
       * but we may not know that it is not connected (e.g. remote host
       * disconnected and we have not found out yet), so do not yap:
       */
      && !TX_ERROR_IS_ENOTCONN(errNum))
    {
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "Cannot shutdown(%s) socket %d: %s",
                     howStr, skt, TXstrerror(saveErr));
      TX_POPERROR();
    }
  return(rc == 0);
}

int
TXezWaitForSocketReadability(
        TXPMBUF *pmbuf,         /* (out, opt.) buffer for putmsgs */
        TXtraceSkt traceSkt,    /* (in, opt.) HTS_... traceskt bits */
        const char *func,       /* (in, opt.) function for traceskt msgs */
        int fd,                 /* (in) file descriptor */
        double timeout)         /* (in) timeout in seconds (-1 == infinite) */
/* Waits for `fd' (may be non-blocking) to become readable.
 * Does not re-try after interrupt.
 * wtf - This should really be the replacement for ezswaitread().
 * Returns 1 if data is available, 0 if timeout, -1 on error
 */
{
  EWM   stats;
  int   res;
  MERGE_FUNC_VARS;

  stats = (EWM_READ
#ifdef _WIN32
           /* Exception bit gets connection-refused under Windows;
            * under Unix it gets OOB data:
            */
           | EWM_EXCEPTION
#endif /* _WIN32 */
           );
  res = TXezWaitForMultipleSockets(pmbuf, traceSkt, MERGE_FUNC(func), &fd,
                                   &stats, 1, NULL, timeout, TXbool_False);
  switch (res)
    {
    case 0:                                     /* ok */
      if (stats & EWM_READ) return(1);
#ifdef _WIN32
      if (stats & EWM_EXCEPTION)
        {
          if (TXgeterror() == 0)
            {
              if (traceSkt & HTS_SELECT)
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "Treating socket exception bit set as connection refused");
                  TX_POPERROR();
                }
              TXseterror(WSAECONNREFUSED);      /* assumption */
            }
          return(-1);
        }
#endif /* _WIN32 */
      return(0);                                /* assume timeout */
    case -1:                                    /* error */
      return(-1);
    default:                                    /* -2 etc. unexpected */
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
               "Unexpected return value %d from TXezWaitForMultipleSockets()",
                     res);
      return(-1);
    }
}
/**********************************************************************/

int
TXezWaitForSocketWritability(
    TXPMBUF *pmbuf,             /* (out, opt.) buffer for putmsgs */
    TXtraceSkt traceSkt,        /* (in, opt.) HTS_... traceskt bits */
    const char *func,           /* (in, opt.) function for traceskt msgs */
    int fd,                     /* (in) file descriptor */
    double timeout)             /* (in) timeout in seconds (-1 == infinite) */
/* Waits for `fd' (may be non-blocking) to become writable.
 * Does not re-try after interrupt.
 * Returns 1 if data is available, 0 if timeout, -1 on error.
 */
{
  EWM   stats;
  int   res;
  MERGE_FUNC_VARS;

  stats = (EWM_WRITE
#ifdef _WIN32
           /* Exception bit gets connection-refused under Windows;
            * under Unix it gets OOB data:
            */
           | EWM_EXCEPTION
#endif /* _WIN32 */
           );
  res = TXezWaitForMultipleSockets(pmbuf, traceSkt, MERGE_FUNC(func), &fd,
                                   &stats, 1, NULL, timeout, TXbool_False);
  switch (res)
    {
    case 0:                                     /* ok */
      if (stats & EWM_WRITE) return(1);
#ifdef _WIN32
      if (stats & EWM_EXCEPTION)
        {
          if (TXgeterror() == 0)
            {
              if (traceSkt & HTS_SELECT)
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "Treating socket exception bit set as connection refused");
                  TX_POPERROR();
                }
              TXseterror(WSAECONNREFUSED);      /* assumption */
            }
          return(-1);
        }
#endif /* _WIN32 */
      return(0);                                /* assume timeout */
    case -1:                                    /* error */
      return(-1);
    default:                                    /* -2 etc. unexpected */
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
               "Unexpected return value %d from TXezWaitForMultipleSockets()",
                     res);
      return(-1);
    }
}
/**********************************************************************/

int
TXezWaitForMultipleSockets(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                           const char *func,    /* (in, opt.) C func; msgs */
                           const int *fds, EWM *stats, int num,
                           const char **sktNames, /* (in, opt.) for msgs */
                           double timeout,    /* < 0: infinite */
                           TXbool okIntr)
/* Like ezswait[read|write]err(), but for multiple sockets.  Waits
 * up to `timeout' seconds for any of the `num' `fds' sockets to change
 * status according to the parallel `stats' array: EWM_READ/WRITE/EXCEPTION.
 * Sets `stats' array on return, including EWM_ERR flag if error.
 * `sktNames' is optional parallel-to-`fds' array of socket names, for msgs.
 * Returns 0 if ok (check `stats' array), -1 if overall error (reported),
 * -2 (silently) iff `okIntr' and interrupted.  -1 sockets silently ignored.
 * Ignores HTS_DATAGRAM, as datagram/stream nature of each `fds' not known.
 */
{
  fd_set                readbits, writebits, exceptbits;
  fd_set                sreadbits, swritebits, sexceptbits, *rbp, *wbp, *ebp;
  struct timeval        timer, *tim;
  int                   ret, i, fd, maxfd;
  double                deadline = -1.0, now;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  char                  *prePtr, *preBufEnd, preBuf[256];
  char                  *bufPtr, *bufEnd, buf[256];
  char                  preTimeBuf[100];

  /* Check fd before using FD_SET(), because it's a macro and
   * could waltz off into a stack fault on bad values.
   */
#ifdef _WIN32
  /* Bad assumptions for decent implementations which don't
     implement as array of bits. */
#  define IS_BAD_DESC(fd)       0
#else /* !_WIN32 */
#  define IS_BAD_DESC(fd)       \
  ((unsigned)(fd) >= (unsigned)(sizeof(fd_set)*EPI_OS_CHAR_BITS))
#endif /* !_WIN32 */

  if (!initsock(pmbuf)) return(-1);
  *preTimeBuf = '\0';
  TX_EZSOCK_CLEAR_ERROR();
  FD_ZERO(&readbits);
  FD_ZERO(&writebits);
  FD_ZERO(&exceptbits);
  rbp = wbp = ebp = (fd_set *)NULL;
  maxfd = -1;
  *(prePtr = preBuf) = '\0';
  preBufEnd = preBuf + sizeof(preBuf) - 4;      /* -4 for `...' */
  for (i = 0; i < num; i++)
    {
      fd = fds[i];
      if (fd == -1) continue;                   /* see same check below */
      if (IS_BAD_DESC(fd))                      /* "" */
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, MERGE_FUNC(func),
                         "Invalid file descriptor %d: ignored", fd);
          continue;
        }
      if ((traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT))) &&
          prePtr < preBufEnd)
        prePtr += htsnpf(prePtr, preBufEnd - prePtr, " skt %s%s%d=",
                         (sktNames && sktNames[i] ? sktNames[i] : ""),
                         (sktNames && sktNames[i] ? "=" : ""), fd);
      if (stats[i] & EWM_READ)
        {
          FD_SET(fd, &readbits);
          rbp = &readbits;
          if ((traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT))) &&
              prePtr < preBufEnd)
            *(prePtr++) = 'R';
        }
      if (stats[i] & EWM_WRITE)
        {
          FD_SET(fd, &writebits);
          wbp = &writebits;
          if ((traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT))) &&
              prePtr < preBufEnd)
            *(prePtr++) = 'W';
        }
      if (stats[i] & EWM_EXCEPTION)             /* NBLK Windows connect */
        {
          FD_SET(fd, &exceptbits);
          ebp = &exceptbits;
          if ((traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT))) &&
              prePtr < preBufEnd)
            *(prePtr++) = 'X';
        }
      if (stats[i] & EWM_ERR)
        {
          if ((traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT))) &&
              prePtr < preBufEnd)
            *(prePtr++) = 'E';
        }
      if (fd > maxfd) maxfd = fd;
    }
  maxfd++;
  if (traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT)))
    {
      if (prePtr < preBufEnd)
        *prePtr = '\0';
      else                                      /* `preBuf' overflow */
        strcpy(prePtr, "...");
    }

  timerclear(&timer);
  if (timeout < 0.0)
    tim = (struct timeval *)NULL;               /* MAW 07-29-93 wait forever */
  else
    {
      timer.tv_sec = (long)timeout;
      timer.tv_usec = (long)((timeout - (double)timer.tv_sec)*1000000.0);
      tim = &timer;
      /* We might need to re-start the select(); if so we'll need to correct
       * the timeout, so get the current time now:  wtf another system call
       */
      deadline = TXgettimeofday() + timeout;
    }
  sreadbits = readbits;
  swritebits = writebits;
  sexceptbits = exceptbits;
  /* KNG 021016 used to give up after 25 tries, but something is interrupting
   * this over 25 times (SIGIO in Vortex?), so keep re-trying:
   */
  if (usepoll)                                  /* ? untested */
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "Internal error: usepoll unimplemented");
      ret = -1;
    }
  else for (;;)                                 /* !usepoll */
    {
      TXbool    wasInterrupted = TXbool_False, retrying = TXbool_False;

      if (traceSkt & (HTS_SELECT | HTS_BEFORE_SELECT))
        {
          if (tim)
            htsnpf(preTimeBuf, sizeof(preTimeBuf), "%1.3klf",
                   (double)tim->tv_sec + (double)tim->tv_usec / 1000000.0);
          else
            htsnpf(preTimeBuf, sizeof(preTimeBuf), "NULL=infinite");
        }
      HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_SELECT)
        txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                       "select(%s sec okIntr=%s%s) starting", preTimeBuf,
                       (okIntr ? "yes" : "no"), preBuf);
      HTS_TRACE_BEFORE_END()

      TX_EZSOCK_CLEAR_ERROR();
      ret = select(maxfd, rbp, wbp, ebp, tim);

      wasInterrupted = (BADRET(ret) && TXgeterror() == TXERR_EINTR);
      retrying = (wasInterrupted && !okIntr);
      /* Update `stats' array (Bug 7355: only if not retrying; otherwise
       * we still need original values to check against after next select()).
       * Preserve err for tracing below:
       */
      TX_PUSHERROR();
      *(bufPtr = buf) = '\0';
      bufEnd = buf + sizeof(buf) - 4;           /* -4 for `...' */
      for (i = 0; i < num; i++)
        {
          fd = fds[i];
          if ((traceSkt & HTS_SELECT) && bufPtr < bufEnd)
            bufPtr += htsnpf(bufPtr, bufEnd - bufPtr, " skt %s%s%d=",
                             (sktNames && sktNames[i] ? sktNames[i] : ""),
                             (sktNames && sktNames[i] ? "=" : ""), fd);
          if (fd == -1)                         /* unused socket entry */
            {
              if (!retrying) stats[i] = (EWM)0;
            }
          else if (IS_BAD_DESC(fd) || BADRET(ret))
            {
              if (!retrying) stats[i] = EWM_ERR;
              if ((traceSkt & HTS_SELECT) && bufPtr < bufEnd)
                *(bufPtr++) = 'E';
            }
          else
            {
              int       orgStat = stats[i];

              /* Bug 7355: do not modify stats[] if retrying: */
              if (!retrying) stats[i] = EWM_NONE;
              if (ret > 0)
                {
                  if ((orgStat & EWM_READ) && FD_ISSET(fd, &readbits))
                    {
                      if (!retrying) stats[i] |= EWM_READ;
                      if ((traceSkt & HTS_SELECT) && bufPtr < bufEnd)
                        *(bufPtr++) = 'R';
                    }
                  if ((orgStat & EWM_WRITE) && FD_ISSET(fd, &writebits))
                    {
                      if (!retrying) stats[i] |= EWM_WRITE;
                      if ((traceSkt & HTS_SELECT) && bufPtr < bufEnd)
                        *(bufPtr++) = 'W';
                    }
                  if ((orgStat & EWM_EXCEPTION) && FD_ISSET(fd, &exceptbits))
                    {
                      if (!retrying) stats[i] |= EWM_EXCEPTION;
                      if ((traceSkt & HTS_SELECT) && bufPtr < bufEnd)
                        *(bufPtr++) = 'X';
                    }
                }
            }
        }
      TX_POPERROR();

      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_SELECT)
        if (bufPtr < bufEnd)
          *bufPtr = '\0';
        else                                    /* `buf' overflow */
          strcpy(bufPtr, "...");
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
        "select(%s sec okIntr=%s%s): %1.3lf sec returned %d=%s err %d=%s%s%s",
                       preTimeBuf,
                       (okIntr ? "yes" : "no"), preBuf, HTS_TRACE_TIME(), ret,
                    (BADRET(ret) || ret < 0 ? "Err" : (0 ? "Timeout" : "Ok")),
                       (int)saveErr, TXgetOsErrName(saveErr, Ques), buf,
                       (wasInterrupted && okIntr ? ", returning -2" :
                        (retrying ? ", retrying" : "")));
      HTS_TRACE_AFTER_END()

      if (wasInterrupted && okIntr) return(-2);
      if (!retrying) break;                     /* success or severe error */

      /* Set up for retry: */
      readbits = sreadbits;
      writebits = swritebits;
      exceptbits = sexceptbits;
      timerclear(&timer);
      if (timeout < 0.0)
        tim = (struct timeval *)NULL;
      else                                      /* correct timeout */
        {
          now = TXgettimeofday();
          if (deadline >= now)
            {
              double    n = deadline - now;

              timer.tv_sec = (long)n;
              timer.tv_usec = (long)((n - (double)timer.tv_sec)*1000000.0);
            }
          else
            timer.tv_sec = timer.tv_usec = 0L;
          tim = &timer;
        }
    }
  if (BADRET(ret))
    {
      char      sktBuf[128];

      if (num == 1)
        htsnpf(sktBuf, sizeof(sktBuf), "skt %s%s%d",
               (sktNames && sktNames[0] ? sktNames[0] : ""),
               (sktNames && sktNames[0] ? "=" : ""), (int)fds[0]);
      else
        *sktBuf = '\0';
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func), "select(%s) failed: %s",
                     sktBuf, TXstrerror(TXgeterror()));
      return(-1);
    }
  return(0);
}
/**********************************************************************/

/* ------------------------------------------------------------------------ */

#ifdef _WIN32
int
TXezWaitForMultipleObjects(TXPMBUF *pmbuf, TXtraceSkt traceSkt,
                           const char *func,
         const HANDLE *handles, size_t numHandles, const char **handleNames,
         double timeout, TXbool alertable)
/* WaitForMultipleObjectsEx() wrapper, with tracing.  `handleNames' is
 * parallel to `handles' and optional, for trace messages.
 * Returns WaitForMultipleObjectsEx() return value; if WAIT_FAILED, reported
 * (there are non-WaitForMultipleObjectsEx() causes of WAIT_FAILED too).
 * Thread-safe.
 */
{
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;
  char          *h, *handleBufEnd, handleBuf[1024], retTraceBuf[128];
  int           ret;
  size_t        i;

  *(h = handleBuf) = '\0';
  handleBufEnd = handleBuf + sizeof(handleBuf) - 4;     /* -4 for `...' */

  if (traceSkt & (HTS_SELECT | HTS_BEFORE(HTS_SELECT)))
    {
      h += htsnpf(h, handleBufEnd - h, " handle%s",
                  (numHandles == 1 ? "" : "s"));
      for (i = 0; i < numHandles; i++)
        if (h < handleBufEnd)
          h += htsnpf(h, handleBufEnd - h,
                      " %s%s%p",
                      (handleNames && handleNames[i] ? handleNames[i] : ""),
                      (handleNames && handleNames[i] ? "=" : ""),
                      (void *)handles[i]);
      if (h < handleBufEnd)
        *h = '\0';
      else                                      /* `preBuf' overflow */
        strcpy(h, "...");
    }

  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_SELECT)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
         "WaitForMultipleObjectsEx(%1.3lf sec all=0 alertable=%s%s) starting",
                   timeout, (alertable ? "Y" : "N"), handleBuf);
  HTS_TRACE_BEFORE_END()

  TX_EZSOCK_CLEAR_ERROR();
  if (timeout >= (double)(((EPI_HUGEUINT)1 << sizeof(DWORD)*8) / 1000))
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, MERGE_FUNC(func),
                     "Timeout %g too large for WaitForMultipleObjectsEx()",
                     timeout);
      TXseterror(ERROR_INVALID_PARAMETER);
      goto err;
    }
  ret = (int)WaitForMultipleObjectsEx((DWORD)numHandles, handles,
                                      FALSE,    /* wait for any not all */
                                      (timeout < 0.0 ? INFINITE :
                                       (DWORD)(timeout*1000.0)), alertable);

  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_SELECT)
    if (ret >= WAIT_OBJECT_0 && ret < WAIT_OBJECT_0 + numHandles)
      htsnpf(retTraceBuf, sizeof(retTraceBuf),
             "WAIT_OBJECT_0+%d = handle %s%s%p", ret - (int)WAIT_OBJECT_0,
             (handleNames && handleNames[ret - (int)WAIT_OBJECT_0] ?
              handleNames[ret - (int)WAIT_OBJECT_0] : ""),
          (handleNames && handleNames[ret - (int)WAIT_OBJECT_0] ? "=" : ""),
             (void *)handles[ret - (int)WAIT_OBJECT_0]);
    else if (ret >= WAIT_ABANDONED_0 && ret < WAIT_ABANDONED_0 + numHandles)
      htsnpf(retTraceBuf, sizeof(retTraceBuf),
             "WAIT_ABANDONED_0+%d = handle %s%s%p",
             ret - (int)WAIT_ABANDONED_0,
             (handleNames && handleNames[ret - (int)WAIT_OBJECT_0] ?
              handleNames[ret - (int)WAIT_OBJECT_0] : ""),
          (handleNames && handleNames[ret - (int)WAIT_OBJECT_0] ? "=" : ""),
             (void *)handles[ret - (int)WAIT_ABANDONED_0]);
    else if (ret == WAIT_IO_COMPLETION)
      strcpy(retTraceBuf, "WAIT_IO_COMPLETION");
    else if (ret == WAIT_TIMEOUT)
      strcpy(retTraceBuf, "WAIT_TIMEOUT");
    else if (ret == WAIT_FAILED)
      strcpy(retTraceBuf, "WAIT_FAILED");
    else
      strcpy(retTraceBuf, "?");
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   "WaitForMultipleObjectsEx(%1.3lf sec all=0 alertable=%s%s): %1.3lf sec returned %d=%s err %d=%s",
                   timeout, (alertable ? "Y" : "N"), handleBuf,
                   HTS_TRACE_TIME(), ret, retTraceBuf, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  HTS_TRACE_AFTER_END()

  if (ret == WAIT_FAILED)
    {
      txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                     "WaitForMultipleObjectsEx() failed: %s",
                     /* WTF our TXstrerror() thread-unsafe in Windows: */
                     TXgetOsErrName(TXgeterror(), Ques));
      goto err;
    }
  goto finally;

err:
  ret = WAIT_FAILED;
finally:
  return(ret);
}
#endif /* _WIN32 */

#if NOSY
/**********************************************************************/
static void
pidgeon(sd)                                          /* stool pidgeon */
int sd;
{
struct sockaddr_in s;
int len, i;
unsigned short sa;
struct in_addr na;

   if(!initsock(pmbuf)) return;
#  error Update for IPv6
   len=sizeof(s);
   if(getpeername(sd,&s,&len)==0){
   int o=epilocmsg(1);
   epiputmsg(MINFO,(char *)NULL,"connected from %s:%u",inet_ntoa(s.sin_addr),(unsigned)ntohs(s.sin_port));
      epilocmsg(o);
   }
}
/**********************************************************************/
#else
#  define pidgeon(a)
#endif

unsigned
TXezStringToPort(TXPMBUF *pmbuf, const char *service)
/* Returns -1 on error, else port number for `service'.
 */
{
  unsigned      sno;

   TX_EZSOCK_CLEAR_ERROR();
   if(isdigit((int)(*(byte *)service))) sno = (unsigned)atoi(service);
   else{
   struct servent *sep;
      sep=getservbyname(service,PROTOCOL);
      if(sep==NULL){
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                   "Unknown service `%s' for protocol `%s'",service,PROTOCOL);
        return((unsigned)(-1));
      }else{
        sno = (unsigned)ntohs(sep->s_port);
      }
   }
   return(sno);
}
/**********************************************************************/

/* We want an accept() function that can atomically do TX_SOCKET_FLAGS
 * as well, if possible, to avoid race condition with post-application
 * of those flags: Bug 7570
 */
#ifdef _WIN32
/* wtf this cannot apply TX_SOCKET_FLAGS ala WSASocket() (nor do we
 * fix in TXezPostFixSocketFlags()); should be ok since
 * accept()-returned handles seem to have same flags
 * (i.e. (no-)inherit) as the accept() server handle:
 */
#  define TX_ACCEPT_FUNC                TXacceptWrapper
#  define TX_SOCKET_FLAGS_STR_ACCEPT    ""
#elif defined(EPI_HAVE_ACCEPT4)
#  define TX_ACCEPT_FUNC                accept4
#  define TX_SOCKET_FLAGS_STR_ACCEPT    TX_SOCKET_FLAGS_STR
#else /* !EPI_HAVE_ACCEPT4 */
#  define TX_ACCEPT_FUNC                accept
#  define TX_SOCKET_FLAGS_STR_ACCEPT    ""
#endif /* !EPI_HAVE_ACCEPT4 */

/**********************************************************************/
int
TXezWaitForCall(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                int sd,
                TXsockaddr *remoteAddr /* opt. */)
{
EPI_ACCEPT_LEN_TYPE rsalen=0;
TXsockaddr rsa;
TXSOCKET        csd;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

   if (!remoteAddr) remoteAddr = &rsa;
   do{
     TXsockaddrInit(remoteAddr);
     rsalen = sizeof(remoteAddr->storage);
     TX_EZSOCK_CLEAR_ERROR();
      childdied=0;                      /* clear death signal flag */
      HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
        txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                       TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                       "(skt %d, ...)" TX_SOCKET_FLAGS_STR_ACCEPT
                       " starting", sd);
      HTS_TRACE_BEFORE_END()
        /* WTF Windows: do TX_SOCKET_FLAGS via WSAAccept() somehow,
         * like we do with WSASocket()? fix TX_SOCKET_FLAGS_STR_ACCEPT
         * if/when this fixed.  should be ok as-is, see #define
         * TX_ACCEPT_FUNC comment
         */
      csd = TX_ACCEPT_FUNC(sd, (struct sockaddr *)&remoteAddr->storage,
                           &rsalen
#ifdef EPI_HAVE_ACCEPT4
                           , TX_SOCKET_FLAGS
#endif /* EPI_HAVE_ACCEPT4 */
                           );
                      /* restart accept() on death of child signal */
      HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
      {
        double          sec;
        TXsockaddr      locSockaddr;
        char            remoteAddrBuf[TX_SOCKADDR_MAX_STR_SZ];
        char            localAddrBuf[TX_SOCKADDR_MAX_STR_SZ];
        char            sktFlagsBuf[256];

        sec = HTS_TRACE_TIME();                 /* immediately */
        if (!BADSOCK(csd))                           /* success */
          {
            TXsockaddrToString(pmbuf, remoteAddr, remoteAddrBuf,
                               sizeof(remoteAddrBuf));
            if (TXezGetLocalSockaddr(pmbuf, (TXtraceSkt)0, MERGE_FUNC(func), csd,
                                     &locSockaddr))
              TXsockaddrToString(pmbuf, &locSockaddr, localAddrBuf,
                                 sizeof(localAddrBuf));
            else
              TXstrncpy(localAddrBuf, Ques, sizeof(localAddrBuf));
          }
        else
          {
            TXstrncpy(localAddrBuf, Ques, sizeof(localAddrBuf));
            TXstrncpy(remoteAddrBuf, Ques, sizeof(remoteAddrBuf));
          }
        TXezGetSocketFlags(csd, sktFlagsBuf, sizeof(sktFlagsBuf));
        txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                       TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                       "(skt %d, ...)" TX_SOCKET_FLAGS_STR_ACCEPT
              ": %1.3lf sec returned skt %wd remote %s local %s err %d=%s %s",
                       sd, sec, (EPI_HUGEINT)csd, remoteAddrBuf, localAddrBuf,
                       (int)saveErr, TXgetOsErrName(saveErr, Ques),
                       sktFlagsBuf);
      }
      HTS_TRACE_AFTER_END()
   }while(BADSOCK(csd) &&
          ((errno==EINTR && childdied) ||
/* MAW 08-04-95 - add handling for various client errors that can break an accept() in progress */
            errno==ETIMEDOUT ||
            errno==ENETRESET ||
            errno==ECONNRESET ||
            errno==ECONNABORTED
         ));
   if(BADSOCK(csd)){
     txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                    TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                    "(skt %wd, ...)" TX_SOCKET_FLAGS_STR_ACCEPT " failed: %s",
                    (EPI_HUGEINT)sd, TXstrerror(TXgeterror()));
     csd = TXSOCKET_INVALID;
   }else{
     if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), csd))
       {
         TX_PUSHERROR();
         TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), csd);
         csd = TXSOCKET_INVALID;
         TX_POPERROR();
       }
     else
       {
         TXezPostFixSocketFlags(pmbuf, traceSkt, MERGE_FUNC(func), csd);
         pidgeon(csd);
       }
   }
   return(csd);
}
/**********************************************************************/

int
ezsacceptnblk(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func, int fd,
              TXsockaddr *remoteSockaddr,  /* (out, opt) */
              TXsockaddr *localSockaddr)   /* (out, opt.) */
/* Accepts new connection on non-blocking server socket `fd' (select()
 * should have just returned writable).  Sets accepted socket to
 * non-blocking, and `*remoteSockaddr'/`*localSockaddr' to remote/local IP
 * and port.
 * Returns new socket, or -1 on benign error (retry e.g. EINTR, not
 * reported), or -2 on severe error (reported).
 */
{
  TXsockaddr            remSockaddr, locSockaddr;
  TXSOCKET              skt = TXSOCKET_INVALID;
  TXbool                gotLocSockaddr = TXbool_False;
  EPI_ACCEPT_LEN_TYPE   sockaddrLen;
  TXERRTYPE             errnum;
#ifdef macintosh
  long                  ioarg;
#else /* !macintosh */
  ulong                 ioarg;
#endif /* !macintosh */
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

  TX_EZSOCK_CLEAR_ERROR();
  TXsockaddrInit(&remSockaddr);
  TXsockaddrInit(&locSockaddr);
  HTS_TRACE_BEFORE_START_ARG(traceSkt, HTS_OPEN)
    txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                   TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                   "(skt %d, ...)" TX_SOCKET_FLAGS_STR_ACCEPT " starting", fd);
  HTS_TRACE_BEFORE_END()
    /* WTF Windows: do TX_SOCKET_FLAGS via WSAAccept() somehow, like
     * we do with WSASocket()? at least post-fix in
     * TXezPostFixSocketFlags().  should be ok as-is; see #define
     * TX_ACCEPT_FUNC comment
     */
  sockaddrLen = sizeof(remSockaddr.storage);
  skt = TX_ACCEPT_FUNC(fd, (struct sockaddr*)&remSockaddr.storage,
                       &sockaddrLen
#ifdef EPI_HAVE_ACCEPT4
                       , TX_SOCKET_FLAGS
#endif /* EPI_HAVE_ACCEPT4 */
                       );
  HTS_TRACE_AFTER_START_ARG(traceSkt, HTS_OPEN)
  {
    double      sec;
    char        remoteAddrBuf[TX_SOCKADDR_MAX_STR_SZ];
    char        localAddrBuf[TX_SOCKADDR_MAX_STR_SZ];
    char        sktFlagsBuf[256];

    sec = HTS_TRACE_TIME();                     /* immediately */
    if (!BADSOCK(skt))                               /* success */
      {
        TXsockaddrToString(pmbuf, &remSockaddr, remoteAddrBuf,
                           sizeof(remoteAddrBuf));
        gotLocSockaddr = TXezGetLocalSockaddr(pmbuf, (TXtraceSkt)0,
                                         MERGE_FUNC(func), skt, &locSockaddr);
        if (gotLocSockaddr)
          TXsockaddrToString(pmbuf, &locSockaddr, localAddrBuf,
                             sizeof(localAddrBuf));
        else
          TXstrncpy(localAddrBuf, Ques, sizeof(localAddrBuf));
      }
    else
      {
        TXstrncpy(localAddrBuf, Ques, sizeof(localAddrBuf));
        TXstrncpy(remoteAddrBuf, Ques, sizeof(remoteAddrBuf));
      }
    TXezGetSocketFlags(skt, sktFlagsBuf, sizeof(sktFlagsBuf));
    txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                   TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                   "(skt %d, ...)" TX_SOCKET_FLAGS_STR_ACCEPT
              ": %1.3lf sec returned skt %wd remote %s local %s err %d=%s %s",
                   fd, sec, (EPI_HUGEINT)skt, remoteAddrBuf, localAddrBuf,
                   (int)saveErr, TXgetOsErrName(saveErr, Ques),
                   sktFlagsBuf);
  }
  HTS_TRACE_AFTER_END()

  if (skt >= 0)                                 /* success */
    {
      if (remoteSockaddr) *remoteSockaddr = remSockaddr;

      if (!TXezOkSockToInt(pmbuf, MERGE_FUNC(func), skt))
        {
          TX_PUSHERROR();
          TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
          skt = TXSOCKET_INVALID;
          TX_POPERROR();
        }
      else
        {
          TXezPostFixSocketFlags(pmbuf, traceSkt, MERGE_FUNC(func), skt);
        }

      ioarg = 1;
      if (ioctlsocket(skt, FIONBIO, &ioarg) != 0)
        {                                       /* failed */
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                         "Cannot set socket %wd non-blocking: %s",
                         (EPI_HUGEINT)skt, TXstrerror(TXgeterror()));
          TXezCloseSocket(pmbuf, traceSkt, MERGE_FUNC(func), skt);
          skt = TXSOCKET_INVALID;
          TX_POPERROR();
          if (remoteSockaddr) TXsockaddrInit(remoteSockaddr);
          if (localSockaddr) TXsockaddrInit(localSockaddr);
          return(-2);                           /* severe error */
        }
      if (localSockaddr)
        {
          if (!gotLocSockaddr)                  /* untried */
            gotLocSockaddr = TXezGetLocalSockaddr(pmbuf, (TXtraceSkt)0,
                                         MERGE_FUNC(func), skt, &locSockaddr);
          *localSockaddr = locSockaddr;
        }
      return(skt);                              /* success */
    }

  /* accept() error.  Check for interrupted system call, or Stevens
   * UNP p. 423 race condition (see also ezserversocknblk() elsewhere):
   */
  errnum = TXgeterror();
  if (remoteSockaddr) TXsockaddrInit(remoteSockaddr);
  if (localSockaddr) TXsockaddrInit(localSockaddr);
  if (0
#ifdef _WIN32
#  ifdef WSAEWOULDBLOCK
      || errnum == WSAEWOULDBLOCK
#  endif /* WSAEWOULDBLOCK */
#  ifdef WSAECONNABORTED
      || errnum == WSAECONNABORTED
#  endif /* WSAECONNABORTED */
#  ifdef WSAEPROTO
      || errnum == WSAEPROTO                    /* SVR4: reset by client */
#  endif /* WSAEPROTO */
#  ifdef WSAEINTR
      || errnum == WSAEINTR                     /* signal interrupt */
#  endif /* WSAEINTR */
#  ifdef WSAECONNRESET
      || errnum == WSAECONNRESET
#  endif /* WSAECONNRESET */
#else /* !_WIN32 */
#  ifdef EWOULDBLOCK
      || errnum == EWOULDBLOCK                  /* BSD: reset by client */
#  endif /* EWOULDBLOCK */
#  ifdef ECONNABORTED
      || errnum == ECONNABORTED                 /* Posix: reset by client */
#  endif /* ECONNABORTED */
#  ifdef EPROTO
      || errnum == EPROTO                       /* SVR4: reset by client */
#  endif /* EPROTO */
#  ifdef EINTR
      || errnum == EINTR                        /* signal interrupt */
#  endif /* EINTR */
#  ifdef ECONNRESET
      || errnum == ECONNRESET
#  endif /* ECONNRESET */
#  ifdef ENOBUFS
      || errnum == ENOBUFS                      /* HPUX 11 */
#  endif /* ENOBUFS */
#endif /* !_WIN32 */
      )
    return(-1);                                 /* benign error */
  txpmbuf_putmsg(pmbuf, MERR, MERGE_FUNC(func),
                 TX_STRINGIZE_VALUE_OF(TX_ACCEPT_FUNC)
                 "(skt %d, ...)" TX_SOCKET_FLAGS_STR_ACCEPT " failed: %s",
                 fd, TXstrerror(errnum));
  return(-2);                                   /* severe error */
}

/* ------------------------------------------------------------------------ */

char *
TXezSocketStrerror(void)
{
  TXERRTYPE     errNum;

#if defined(EPI_HAVE_H_ERRNO) && defined(EPI_HAVE_HSTRERROR)
  if (h_errno != 0) return((char *)hstrerror(h_errno));
#endif /* EPI_HAVE_H_ERRNO && EPI_HAVE_HSTRERROR */
  errNum = TXgeterror();
  if (errNum != 0) return(TXstrerror(errNum));
#ifdef _WIN32
  if (errno != 0) return(strerror(errno));
#endif /* _WIN32 */
  return("Ok");
}

/**********************************************************************/
char *
ezshostname()                  /* MAW 03-09-94 rename from ezhostname */
{
  static CONST char	Fn[] = "ezshostname";
  TXPMBUF               *pmbuf = TXPMBUFPN;
char *buf;
struct hostent *he;

   if(!initsock(pmbuf)) return((char *)NULL);
   TX_EZSOCK_CLEAR_ERROR();
   if((buf=(char *)TXmalloc(pmbuf, Fn, MAXHOSTNAMELEN+1))==(char *)NULL){
     ;
   }else{
      TX_EZSOCK_CLEAR_ERROR();
      if(gethostname(buf,MAXHOSTNAMELEN)!=0){
	perror("gethostname");		/* KNG 960410 */
        buf = TXfree(buf);
      }else{
        TX_EZSOCK_CLEAR_ERROR();
         if((he=gethostbyname(buf))== HOSTENTPN){
            buf = TXfree(buf);
         }else{
           if((buf=(char *)TXrealloc(pmbuf, Fn, buf,strlen(he->h_name)+1))!=(char *)NULL){
               strcpy(buf,he->h_name);
	    }
         }
      }
   }
   DEBUGA(fprintf(stderr,"ezshostname()=%s\n",buf);)
   return(buf);
}
/**********************************************************************/

#ifdef _WIN32
#  ifndef VER_PLATFORM_WIN32_WINDOWS
#    define VER_PLATFORM_WIN32_WINDOWS 1
#  endif

static TXbool
try95hack(char *buf, size_t bufSz)
{
	DWORD type, sz;
	LONG  rc;
	HKEY hkey;
	OSVERSIONINFO os;

	memset(&os, 0, sizeof(OSVERSIONINFO));
	os.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os);
	if(os.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS)
          return(TXbool_False);
	sz = (DWORD)bufSz;
	buf[0] = '\0';
	type = 0;
	rc = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\Class\\NetTrans\\0005", 0, KEY_QUERY_VALUE, &hkey);
	if(rc == 0)
	{
		rc = RegQueryValueEx(hkey,
			"IPAddress",
			NULL,
			&type,
			buf,
			&sz);
		RegCloseKey(hkey);
		if(rc == 0 && type == REG_SZ) return(TXbool_True);
	}
	return(TXbool_False);
}
#endif /* _WIN32 */

#ifdef EPI_HAVE_GETIFADDRS
static TXbool
TXezGetMyIpFromInterface(TXPMBUF *pmbuf, TXaddrFamily addrFamily,
                         TXsockaddr *sockaddr,  /* opt. */
                         char *ipBuf,           /* opt. */
                         size_t ipBufSz)
/* Gets IP address string for this host.  `ipBufSz' should be at least
 * TX_SOCKADDR_MAX_STR_SZ.  Supports TXaddrFamily_Unspecified to mean either.
 * May return link-local or loopback address, but only if a non-global addr
 * is not found.
 * Returns false on error (with `ipBuf' set to `?' if room).
 */
{
  struct ifaddrs        *ifaList = NULL, *ifa, *ifaBest = NULL;
  int                   rankBest = -1, rank;
  TXbool                ret = TXbool_False;
  TXsockaddr            sockaddrTmp;

  if (getifaddrs(&ifaList) != 0) goto err;

  for (ifa = ifaList; ifa; ifa = ifa->ifa_next)
    {
      if (!ifa->ifa_addr) continue;
      rank = 0;
      /* we rank best-first: !loopback, !linklocal, inet6, inet */
      switch (addrFamily)
        {
        case TXaddrFamily_Unspecified:
          if (ifa->ifa_addr->sa_family == AF_INET) rank += 0x1;
          else if (ifa->ifa_addr->sa_family == AF_INET6) rank += 0x2;
          else continue;                        /* wrong family */
          break;
        case TXaddrFamily_IPv4:
          if (ifa->ifa_addr->sa_family == AF_INET) rank += 0x1;
          else continue;                        /* wrong family */
          break;
        case TXaddrFamily_IPv6:
          if (ifa->ifa_addr->sa_family == AF_INET6) rank += 0x2;
          else continue;                        /* wrong family */
          break;
        default:                                /* dunno what they ask for */
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         UnknownTXfamilyFmt, (int)addrFamily);
          goto err;
        }
      if (ifa->ifa_addr->sa_family == AF_INET6 &&
          !IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr))
        rank += 0x4;
      if (!(ifa->ifa_flags & IFF_LOOPBACK)) rank += 0x8;
      if (rank > rankBest)
        {
          ifaBest = ifa;
          rankBest = rank;
        }
    }
  if (ifaBest)
    {
      TXsockaddrInit(&sockaddrTmp);
      memcpy(&sockaddrTmp.storage, ifaBest->ifa_addr,
             (ifaBest->ifa_addr->sa_family == AF_INET ?
              sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)));
      if (sockaddr) *sockaddr = sockaddrTmp;
      if (ipBuf && !TXsockaddrToStringIP(pmbuf, &sockaddrTmp, ipBuf, ipBufSz))
        goto err;
      ret = TXbool_True;
      goto finally;
    }

err:
  ret = TXbool_False;
  if (sockaddr) TXsockaddrInit(sockaddr);
  if (ipBuf) TXstrncpy(ipBuf, Ques, ipBufSz);
finally:
  if (ifaList)
    {
      freeifaddrs(ifaList);
      ifaList = NULL;
    }
  return(ret);
}
#endif /* EPI_HAVE_GETIFADDRS */

/**********************************************************************/
TXbool
TXezGetMyIP(TXPMBUF *pmbuf, TXtraceDns traceDns, TXaddrFamily addrFamily,
            TXsockaddr *sockaddr,       /* opt. */
            char *ipBuf,                /* opt. */
            size_t ipBufSz)
/* Gets local host's IP (or `?' on error) into `sockaddr' and/or `ipBuf'
 * (both optional).  `ipBufSz' should be at least TX_SOCKADDR_MAX_STR_SZ.
 * Supports TXaddrFamily_Unspecified to mean either.
 * Returns false on error (with `ipBuf' set to `?' if room).
 */
{
  TXsockaddr    sockaddrTmp;
  TXbool        ret;
  char          hostBuf[TX_MAX(MAXHOSTNAMELEN + 1, TX_SOCKADDR_MAX_STR_SZ)];

  if(!initsock(pmbuf)) goto err;
  TX_EZSOCK_CLEAR_ERROR();
#ifdef EPI_HAVE_GETIFADDRS
  if (TXezGetMyIpFromInterface(pmbuf, addrFamily, sockaddr, ipBuf, ipBufSz))
    {
      ret = TXbool_True;
      goto finally;
    }
#endif /* EPI_HAVE_GETIFADDRS */
#ifdef _WIN32
  if ((addrFamily == TXaddrFamily_IPv4 ||       /* probably IPv4 only */
       addrFamily == TXaddrFamily_Unspecified) &&
      try95hack(hostBuf, sizeof(hostBuf)))
    ;
  else
#endif /* __WIN32 */
  if (gethostname(hostBuf, sizeof(hostBuf)) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Cannot gethostname(): %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  if (!TXhostAndPortToSockaddrs(pmbuf,
                                TXbool_False,   /* do not suppress errors */
                                traceDns, __FUNCTION__, addrFamily, hostBuf,
                                0, TXbool_True, TXbool_True, &sockaddrTmp, 1))
    goto err;
  if (sockaddr) *sockaddr = sockaddrTmp;
  if (ipBuf && !TXsockaddrToStringIP(pmbuf, &sockaddrTmp, ipBuf, ipBufSz))
    goto err;
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
  if (sockaddr) TXsockaddrInit(sockaddr);
  if (ipBuf) TXstrncpy(ipBuf, Ques, ipBufSz);
finally:
  return(ret);
}

/**********************************************************************/
int
ezspeek(fh, buf, n)
int     fh;
void    *buf;
int     n;
/* Peek at data from `fh'.
 * Re-tries interrupted system call.
 */
{
  int   rc;
  int   loops = 100;

  do
    {
      TX_EZSOCK_CLEAR_ERROR();
      rc = recv(fh, buf, n, MSG_PEEK);
      FIXERRNO();
    }
  while (rc == -1 && errno == EINTR && loops--);
  return(rc);
}
/**********************************************************************/

/**********************************************************************/

EPI_SSIZE_T
TXezSocketRead(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
               int skt, const char *hostForMsgs, void *buf, size_t bufSz,
               TXbool flushIt, TXsockaddr *peer, TXezSR ignore)
/* This should replace ezsread().
 * Also fills in `peer' if given, with peer address.
 * Returns number of bytes read, -1 on error, or -2 iff `ignore' is
 * TXezSR_IgnoreConnReset/AllErrs and connection reset.
 */
{
  int           tries;
  EPI_SSIZE_T   bufRd, attemptThisTry, readThisTry;
  TXbool        gotOkRead;
  TXERRTYPE     errNum;
  EPI_SOCKLEN_T socklen;
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

  bufRd = 0;
  gotOkRead = TXbool_False;
  do
    {
      tries = 0;
      do
        {
          attemptThisTry = bufSz - bufRd;
#ifdef _WIN32
          /* Windows read() takes an unsigned int.  And it fails if
           * the count is > INT_MAX, not UINT_MAX (see tx_rawread()):
           */
          if (attemptThisTry > (size_t)EPI_OS_INT_MAX)
            attemptThisTry = (size_t)EPI_OS_INT_MAX;
          /* NOTE: reduce to ~16KB if `skt' is a tty; see tx_rawread() */
#  define CAST  (unsigned int)
#else /* !_WIN32 */
#  define CAST
#endif /* !_WIN32 */
          /* Windows requires recv() on a socket; read() triggers
           * an `Invalid parameter given to a CRT system call' fault:
           */
          HTS_TRACE_BEFORE_START_ARG(traceSkt, (HTS_READ | HTS_READ_DATA))
            if (traceSkt & HTS_BEFORE(HTS_READ))
              txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                            "recvfrom(skt %d%s%s, %wd bytes, 0, %s) starting",
                             skt, (hostForMsgs ? "=" : ""),
                             (hostForMsgs ? hostForMsgs : ""),
                             (EPI_HUGEINT)attemptThisTry,
                             (peer ? "&peer" : "NULL"));
            if ((traceSkt & HTS_BEFORE(HTS_READ_DATA)) && attemptThisTry > 0)
              tx_hexdumpmsg(pmbuf, HTS_MSG_BEFORE + HTS_MSG_READ_DATA,
                            NULL, (byte *)buf + bufRd, attemptThisTry, 1);
          HTS_TRACE_BEFORE_END()
          if (peer) TXsockaddrInit(peer);
          TX_EZSOCK_CLEAR_ERROR();
          socklen = (peer ? sizeof(peer->storage) : 0);
          readThisTry = recvfrom(skt, (byte*)buf + bufRd, CAST attemptThisTry,
                                 0, (peer ? (struct sockaddr *)&peer->storage :
                                     NULL), (peer ? &socklen : NULL));
          FIXERRNO();
          HTS_TRACE_AFTER_START_ARG(traceSkt, (HTS_READ | HTS_READ_DATA))
            if (traceSkt & HTS_READ)
              txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
                             "recvfrom(skt %d%s%s, %wd bytes, 0, %s): %1.3lf sec returned %wd bytes err %d=%s",
                             skt, (hostForMsgs ? "=" : ""),
                             (hostForMsgs ? hostForMsgs : ""),
                             (EPI_HUGEINT)attemptThisTry,
                             (peer ? "&peer" : "NULL"), HTS_TRACE_TIME(),
                             (EPI_HUGEINT)readThisTry, (int)saveErr,
                             TXgetOsErrName(saveErr, Ques));
            if ((traceSkt & HTS_READ_DATA) && readThisTry > 0)
              tx_hexdumpmsg(pmbuf, HTS_MSG_AFTER + HTS_MSG_READ_DATA,
                            NULL, (byte *)buf + bufRd, readThisTry, 1);
          HTS_TRACE_AFTER_END()
        }
      while (++tries < 25 && readThisTry == -1 && errno == EINTR);
      if (readThisTry >= 0)                     /* got somewhere */
        {
          gotOkRead = TXbool_True;
          bufRd += readThisTry;
        }
    }
  while (flushIt && (size_t)bufRd < bufSz && readThisTry > 0);
  if (!gotOkRead) bufRd = readThisTry;          /* return recv()'s -1 */

  errNum = TXgeterror();
  if (bufRd < 0 &&
      (ignore == TXezSR_IgnoreConnReset || ignore == TXezSR_IgnoreAllErrs) &&
      TX_ERROR_IS_CONNECTION_RESET(errNum))
    return(-2);                                 /* connection aborted */

  if ((bufRd < 0 || (flushIt && (size_t)bufRd < bufSz)) &&
      ignore != TXezSR_IgnoreAllErrs)
    {
      char              buf[100];
      const char        *label;

      TX_PUSHERROR();
      if (!hostForMsgs)
        {
          htsnpf(buf, sizeof(buf), "socket %d", skt);
          label = buf;
        }
      else
        label = hostForMsgs;
      txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                     "Could not read%s %wd bytes from %s: %s",
                     (bufRd < 0 ? " any of" : ""),
                     (EPI_HUGEINT)bufSz, label, TXstrerror(saveErr));
      TX_POPERROR();
    }
  /* Do not report partial read as error: may need to select() again */

  return(bufRd);
#undef CAST
}

/**********************************************************************/
int
ezswrite(fh,buf,n)                /* MAW 03-09-94 rename from ezwrite */
int fh;
void *buf;
int n;
/* Write to `fh'.
 * Re-tries interrupted system call.
 */
{
  int   rc, loops = 25;

   do
     {
       TX_EZSOCK_CLEAR_ERROR();
       rc=send(fh,buf,n,0);
       FIXERRNO();
     }
   while (rc == -1 && errno == EINTR && loops--);
   DEBUGA(fprintf(stderr,"ezswrite(%d,%p,%d)=%d\n",fh,buf,n,rc);)
   return(rc);
}

/**********************************************************************/

EPI_SSIZE_T
TXezSocketWrite(TXPMBUF *pmbuf, TXtraceSkt traceSkt, const char *func,
                int skt, const char *hostForMsgs, void *buf, size_t bufSz,
                TXbool flushIt, const TXsockaddr *dest)
/* This should replace ezswrite().  `dest' is optional.
 * Returns number of bytes written (could be < 0).
 */
{
  int           tries;
  EPI_SSIZE_T   bufWritten, attemptThisTry, writtenThisTry;
  TXbool        gotOkWrite;
  EPI_SOCKLEN_T destSockaddrSz;
  char          destAddrBuf[TX_SOCKADDR_MAX_STR_SZ + 4 /* for `...' */];
  HTS_TRACE_VARS;
  MERGE_FUNC_VARS;

  if (traceSkt)
    {
      if (dest)
        {
          TXsockaddrToStringIP(pmbuf,  dest, destAddrBuf, sizeof(destAddrBuf));
          htsnpf(destAddrBuf + strlen(destAddrBuf),
                 sizeof(destAddrBuf) - strlen(destAddrBuf), ":%u",
                 (unsigned)TXsockaddrGetPort(TXPMBUF_SUPPRESS, dest));
          strcpy(destAddrBuf + sizeof(destAddrBuf) - 4, "...");
        }
      else
        strcpy(destAddrBuf, "NULL");
    }
  else
    *destAddrBuf = '\0';
  destSockaddrSz = (dest ? TXsockaddrGetSockaddrSize(dest) : 0);

  bufWritten = 0;
  gotOkWrite = TXbool_False;
  do
    {
      tries = 0;
      do
        {
          attemptThisTry = bufSz - bufWritten;
#ifdef _WIN32
          /* Windows send() takes a signed int.  See also tx_rawwrite(): */
          if (attemptThisTry > (size_t)EPI_OS_INT_MAX)
            attemptThisTry = (size_t)EPI_OS_INT_MAX;
          /* NOTE: reduce to ~16KB if `skt' is a tty?; see tx_rawwrite() */
#  define CAST  (int)
#else /* !_WIN32 */
#  define CAST
#endif /* !_WIN32 */
          /* Windows requires send() on a socket; write() triggers an
           * `Invalid parameter given to a CRT system call' fault:
           */
          HTS_TRACE_BEFORE_START_ARG(traceSkt, (HTS_WRITE | HTS_WRITE_DATA))
            if (traceSkt & HTS_BEFORE(HTS_WRITE))
              txpmbuf_putmsg(pmbuf, HTS_MSG_BEFORE, MERGE_FUNC(func),
                             "sendto(skt %d%s%s, %wd bytes, 0, %s) starting",
                             skt, (hostForMsgs ? "=" : ""),
                             (hostForMsgs ? hostForMsgs : ""),
                             (EPI_HUGEINT)attemptThisTry, destAddrBuf);
            if ((traceSkt & HTS_BEFORE(HTS_WRITE_DATA)) && attemptThisTry > 0)
              tx_hexdumpmsg(pmbuf, HTS_MSG_BEFORE + HTS_MSG_WRITE_DATA,
                            NULL, (byte*)buf + bufWritten, attemptThisTry, 1);
          HTS_TRACE_BEFORE_END()
          TX_EZSOCK_CLEAR_ERROR();
          writtenThisTry = sendto(skt, (byte *)buf + bufWritten,
                                  CAST attemptThisTry, 0,
                                  (dest ?
                                   (const struct sockaddr *)&dest->storage :
                                   NULL), destSockaddrSz);
          FIXERRNO();
          HTS_TRACE_AFTER_START_ARG(traceSkt, (HTS_WRITE | HTS_WRITE_DATA))
            if (traceSkt & HTS_WRITE)
              txpmbuf_putmsg(pmbuf, HTS_MSG_AFTER, MERGE_FUNC(func),
 "sendto(skt %d%s%s, %wd bytes, 0, %s): %1.3lf sec wrote %wd bytes err %d=%s",
                             skt, (hostForMsgs ? "=" : ""),
                             (hostForMsgs ? hostForMsgs : ""),
                             (EPI_HUGEINT)attemptThisTry, destAddrBuf,
                             HTS_TRACE_TIME(), (EPI_HUGEINT)writtenThisTry,
                             (int)saveErr, TXgetOsErrName(saveErr, Ques));
            if ((traceSkt & HTS_WRITE_DATA) && writtenThisTry > 0)
              tx_hexdumpmsg(pmbuf, HTS_MSG_AFTER + HTS_MSG_WRITE_DATA,
                            NULL, (byte*)buf + bufWritten, writtenThisTry, 1);
          HTS_TRACE_AFTER_END()
        }
      while (++tries < 25 && writtenThisTry == -1 && errno == EINTR);
      if (writtenThisTry >= 0)                  /* got somewhere */
        {
          gotOkWrite = TXbool_True;
          bufWritten += writtenThisTry;
        }
    }
  while (flushIt && (size_t)bufWritten < bufSz && writtenThisTry > 0);
  if (!gotOkWrite) bufWritten = writtenThisTry; /* return send()'s -1 */

  if (bufWritten < 0 || (flushIt && (size_t)bufWritten < bufSz))
    {
      char              buf[100];
      const char        *label;

      TX_PUSHERROR();
      if (!hostForMsgs)
        {
          htsnpf(buf, sizeof(buf), "socket %d", skt);
          label = buf;
        }
      else
        label = hostForMsgs;
      txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                     "Could not write%s %wd bytes to %s: %s",
                     (bufWritten < 0 ? " any of" : ""),
                     (EPI_HUGEINT)bufSz, label, TXstrerror(saveErr));
      TX_POPERROR();
    }
  /* Do not report partial write as error: may need to select() again */

  return(bufWritten);
}

/***********************************************************************
** almost generic server
***********************************************************************/
#ifdef macintosh
#  define TRAPDEATH 0
#endif
#ifdef _WIN32
#  define TRAPKILL 0
#  define TRAPDEATH 0
#  ifdef _WIN32
#     define SOCKINHERITED ((int)GetStdHandle(STD_INPUT_HANDLE))
#  else
#     define SOCKINHERITED 0
#  endif
#else                                                     /* _WIN32 */
#  define SOCKINHERITED 0
#endif                                                    /* _WIN32 */

#ifndef TRAPKILL
#  define TRAPKILL 1
#endif
#ifndef TRAPDEATH
#  define TRAPDEATH 1
#endif

#if TRAPKILL
static TX_SIG_HANDLER   *oldterm;
static TX_SIG_HANDLER   *oldhup;
static int g_sock=(-1);
static TXbool   IsServerChild = TXbool_False;

/**********************************************************************/
static SIGTYPE CDECL
bye TX_SIG_HANDLER_ARGS
{
  char          infoBuf[8192];
  char          *infoCur, *infoEnd, *infoPrev;
  size_t        lenPrinted;
  TXtrap        trap = (TXApp ? TXApp->trap : TXtrap_Default);
  const char    *procName = (IsServerChild ? "Server child" : "Server");
  PID_T         myPid;
int o=epilocmsg(1);

  (void)TX_SIG_HANDLER_CONTEXT_ARG;

  infoCur = infoBuf;
  infoEnd = infoBuf + sizeof(infoBuf);
  *infoCur = '\0';
#define INFO_BUF_LEFT   (infoCur < infoEnd ? infoEnd - infoCur : 0)

  myPid = TXgetpid(1);

  /* Get UID/PID that killed us, and their ancestors maybe: */
  infoPrev = infoCur;
  infoCur += htsnpf(infoCur, INFO_BUF_LEFT, " by");
  lenPrinted = TXprintUidAndAncestors(infoCur, INFO_BUF_LEFT,
                                      TX_SIG_HANDLER_SIGINFO_ARG, trap);
  infoCur += lenPrinted;
  if (lenPrinted == 0) *(infoCur = infoPrev) = '\0';

#  ifdef _WIN32
  epiputmsg(MINFO + PROCEND, NULL, "%s PID %u exiting: Killed%s",
            procName, (unsigned)myPid, infoBuf);
      epilocmsg(o);
#  else
#ifdef SIGHUP
      epiputmsg(MINFO + PROCEND, NULL, "%s PID %u exiting: %s (signal %d)%s",
                procName, (unsigned)myPid,
                (sigNum == SIGHUP ? "Hangup" : "Killed"), sigNum, infoBuf);
#else
      epiputmsg(MINFO + PROCEND, NULL, "%s PID %u exiting: %s (signal %d)%s",
                procName, (unsigned)myPid, "Killed", sigNum, infoBuf);
#endif
      epilocmsg(o);
    TXezCloseSocket(TXPMBUF_SUPPRESS, (TXtraceSkt)0, __FUNCTION__, g_sock);
    g_sock=(-1);
    _exit(TXEXIT_TERMINATED);
#  endif
   SIGRETURN;
#undef INFO_BUF_LEFT
}
/**********************************************************************/
static void trapkill ARGS((int sock));
static void
trapkill(sock)
int sock;
{
   g_sock=sock;
   oldterm = TXcatchSignal(SIGTERM, bye);
#  ifdef SIGHUP
   oldhup = TXcatchSignal(SIGHUP, bye);
#  endif
}
/**********************************************************************/
static void untrapkill ARGS((void));
static void
untrapkill()
{
   g_sock=(-1);
   TXcatchSignal(SIGTERM, oldterm);
#ifdef SIGHUP
   TXcatchSignal(SIGHUP, oldhup);
#endif
}
/**********************************************************************/
#else                                                     /* TRAPKILL */
#  define trapkill(a)
#  define untrapkill()
#endif                                                    /* TRAPKILL */
#if TRAPDEATH
static SIGTYPE (CDECL *oldcld)ARGS((int));
static SIGTYPE CDECL deathtrap ARGS((int sig));
/**********************************************************************/
static SIGTYPE CDECL
deathtrap(sig)
int sig;
{
#ifdef WNOHANG /* MAW 01-07-98 - catch any missed ones, avoid zombies */
   while(waitpid((pid_t)-1,(int *)NULL,WNOHANG)>0)
      childdied++;/* remember that it occurred so i can restart accept() */
#else                                                      /* WNOHANG */
   childdied++;/* remember that it occurred so i can restart accept() */
   wait((int *)NULL);
#endif                                                     /* WNOHANG */
   signal(sig,deathtrap);
   SIGRETURN;
}
/**********************************************************************/

void eztrapdeath ARGS((void));
/**********************************************************************/
void
eztrapdeath()
{
/* MAW 11-09-93
   oldcld=signal(SIGCLD,SIG_IGN);
we don't want zombies. many systems are smart enough to not create zombies
if you're ignoring death of child, but others are not.
the most portable way to prevent them is to trap death of child and do a
wait() in the signal handler.
waitpid() or wait3() could be used without the signal handler, but would
require ifdef'ing till you drop.
*/
   oldcld=signal(SIGCLD,deathtrap);
}
/**********************************************************************/

void ezuntrapdeath ARGS((void));
/**********************************************************************/
void
ezuntrapdeath()
{
   signal(SIGCLD,oldcld);
}
/**********************************************************************/
#  define trapdeath()    eztrapdeath()
#  define untrapdeath()  ezuntrapdeath()
#else                                                    /* TRAPDEATH */
#  define trapdeath()
#  define untrapdeath()
#endif                                                   /* TRAPDEATH */

static int runit ARGS((char *cmd,int fh));
/**********************************************************************/
static int
runit(cmd,fh)
char *cmd;
int fh;
{
#ifdef _WIN32
STARTUPINFO si;
PROCESS_INFORMATION pi;
int rc;

   si.cb             =sizeof(STARTUPINFO);
   si.lpReserved     =NULL;
   si.lpDesktop      =NULL;
   si.lpTitle        =NULL;
   si.dwX            =CW_USEDEFAULT;
   si.dwY            =CW_USEDEFAULT;
   si.dwXSize        =CW_USEDEFAULT;
   si.dwYSize        =CW_USEDEFAULT;
   si.dwXCountChars  =0;
   si.dwYCountChars  =0;
   si.dwFillAttribute=0;
   si.dwFlags        =STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
   si.wShowWindow    =SW_SHOW;
   si.cbReserved2    =0;
   si.lpReserved2    =NULL;
   si.hStdInput      =(HANDLE)fh;      /* pass socket handle to child */
   si.hStdOutput     =(HANDLE)fh;
   si.hStdError      =(HANDLE)fh;
   if(!CreateProcess((LPCTSTR)NULL,
                     (LPSTR)cmd,
                     (LPSECURITY_ATTRIBUTES)NULL,
                     (LPSECURITY_ATTRIBUTES)NULL,
                     (BOOL)1,                      /* inherit handles */
                     (DWORD)0,
                     (LPVOID)NULL,
                     (LPCTSTR)NULL,
                     &si,
                     &pi))
   {
      epiputmsg(MERR,(char *)NULL,
                "CreateProcess(%s) failed (err=0x%lx)",
                cmd,(unsigned long)GetLastError());
      return(1);
   }
   rc = (pi.hProcess==INVALID_HANDLE_VALUE)?1:0;
   CloseHandle(pi.hThread);
   CloseHandle(pi.hProcess);
   return rc;
#else /* !_WIN32 */
  /* MAW 03-07-94 wtf - not correctly implemented on unix, not needed */
  /* supposed to redirect stdio to fh */
   (void)cmd;
   (void)fh;
   return(1/*system(cmd)*/);
#endif /* !_WIN32 */
}
/**********************************************************************/

static int ezsdofork=1;                               /* MAW 03-02-95 */
/**********************************************************************/
#if !defined(_WIN32) && !defined(macintosh) /* WTF JMT 03-13-95 */
int
ezsfserve(dofork)                                     /* MAW 03-02-95 */
int dofork;
{
int of=ezsdofork;

   if(dofork!=(-1))
      ezsdofork=dofork;
   return(of);
}
#endif
/**********************************************************************/

#ifdef TX_DEBUG
static int validate_connection(int, TCP_ALLOW *);

static int
validate_connection(int sock, TCP_ALLOW *allows)
{
	struct sockaddr_in sin;
	size_t len;

	if(!allows)
		return VALID_CONNECTION;
	len = sizeof(sin);
	if(getpeername(sock,(struct sockaddr *)&sin,&len)!=0)
	{
		return INVALID_CONNECTION;
	}
	while(allows)
	{
		if((sin.sin_addr.s_addr & allows->mask.sin_addr.s_addr) ==
			allows->address.sin_addr.s_addr)
		{
			switch(allows->mode)
			{
			case TCP_MODE_ALLOW: return VALID_CONNECTION;
			case TCP_MODE_DENY: return INVALID_CONNECTION;
			}
		}
		allows = allows->next;
	}
	return INVALID_CONNECTION;
}
#endif /* TX_DEBUG */

/**********************************************************************/
int
ezsxserve(pmbuf, addrFamily, sname,svr,cmd,allows)
TXPMBUF *pmbuf;
TXaddrFamily    addrFamily;
char *sname;
int (*svr)ARGS((int));
char *cmd;                 /* MAW 03-07-94 - add external cmd support */
void *allows;
{
int csd=0;
int sd=0;
unsigned sno=0;
int rc;
TXsockaddr      sockaddr;

#ifndef TX_DEBUG
   (void)allows;
#endif /* !TX_DEBUG */
   DEBUGA(fprintf(stderr,"ezsxserve(%s,%p,%s)\n",sname,svr,cmd==NULL?"NULL":cmd);)
   if(sname==(char *)NULL || *sname=='\0'){/* MAW 11-09-93 - running from inetd */
      trapkill(SOCKINHERITED);
      if(!initsock(pmbuf)) return(-1);
      TX_EZSOCK_CLEAR_ERROR();
      if(svr==(int (*)ARGS((int)))NULL){
         if((rc=runit(cmd,SOCKINHERITED))!=0){
           TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, SOCKINHERITED);
         }
      }else{
         if((rc=(*svr)(-1))==0){
            rc=(*svr)(SOCKINHERITED);
         }
         TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, SOCKINHERITED);
      }
      deinitsock();
      untrapkill();
      return(rc);
   }
   if ((sno = TXezStringToPort(pmbuf, sname)) == (unsigned)(-1)) {
      return(-1);
   }
   if (!TXhostAndPortToSockaddrs(pmbuf,
                                 TXbool_False,  /* do not suppress errors */
                                 TXtraceDns_None, __FUNCTION__, addrFamily,
                                 "*", sno, TXbool_False, TXbool_True,
                                 &sockaddr, 1))
     return(-1);
   sd = TXezServerSocket(pmbuf, HtTraceSkt, __FUNCTION__, &sockaddr, TXbool_True);/* will use default protocol for specified port */
   if(sd<0){
      return(-1);
   }
   trapkill(sd);
   trapdeath();
   if(svr!=(int (*)ARGS((int)))NULL){
      if((rc=(*svr)(-1))!=0){
         TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, sd);
         untrapdeath();
         untrapkill();
         return(-1);
      }
   }
   for(;;){
      if(svr!=(int (*)ARGS((int)))NULL){
         if((rc=(*svr)(-2))!=0){/* MAW 08-11-94 - let know each time around */
            TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, sd);
            untrapdeath();
            untrapkill();
            return(-1);
         }
      }
      csd = TXezWaitForCall(pmbuf, HtTraceSkt, __FUNCTION__, sd, NULL);
      if(csd==(-1)){
         TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, sd);
         untrapdeath();
         untrapkill();
         return(-1);
      }
#ifdef TX_DEBUG
      if(validate_connection(csd, allows) != VALID_CONNECTION)
      {
         TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
	 /* WTF - logit */
	 continue;
      }
#endif
#     if defined(_WIN32) || defined(macintosh)             /* MAW 03-08-94 - multi conn support */
         if(svr==(int (*)ARGS((int)))NULL){
            if((rc=runit(cmd,csd))!=0){
               TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
            }
	    else
	    {
               TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
	    }
         }else{
            rc=(*svr)(csd);
            TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
         }
#     else
         if(ezsdofork)
         {
           switch (TXfork(pmbuf, "ezsxserve process", NULL, 0x0)) {
            case -1: TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd); break;
            case 0:                                          /* child */
               IsServerChild = TXbool_True;
               trapdeath();  /* KNG 000120 was untrapdeath: monitor zombies */
               TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, sd);
               if(svr==(int (*)ARGS((int)))NULL){
                  rc=runit(cmd,csd);
               }else{
                  rc=(*svr)(csd);
               }
               TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
               untrapkill();
               exit(rc);
               break;
            default:                                        /* parent */
               TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
               break;
            }
         }
         else                            /* MAW 03-02-95 - no forking */
         {
            if(svr==(int (*)ARGS((int)))NULL){
               rc=runit(cmd,csd);
            }else{
               rc=(*svr)(csd);
            }
            TXezCloseSocket(pmbuf, HtTraceSkt, __FUNCTION__, csd);
         }
#     endif
   }
   /*unreached*/
}
/**********************************************************************/

#ifdef DEBUGSELECT
/**********************************************************************/
#undef select
int
myselect(n,r,w,e,t,file,line)
int n;
fd_set *r;
fd_set *w;
fd_set *e;
struct timeval *t;
char *file;
int line;
{
int rc;
int o=epilocmsg(1), i, fd;
fd_set readbits;
static char buf[128];

/*
   for(fd=i=0;fd<n;fd++){
      buf[i++]=FD_ISSET(fd,r)?'r':'-';
      buf[i++]=FD_ISSET(fd,w)?'w':'-';
      buf[i++]=FD_ISSET(fd,e)?'e':'-';
      buf[i++]=' ';
   }
   putmsg(999,"select()","high fd=%d, bits=%s, size=%u",n-1,buf,sizeof(*(&readbits)));
*/
   rc=select(n,r,w,e,t);
   if(rc<0){
      putmsg(999,"select()","%s:%d: %s",file,line,strerror(errno));
      chdir("/usr1/acct/src/net/iris");
      abort();
   }
   epilocmsg(o);
   return(rc);
}
/**********************************************************************/
#endif /* DEBUGSELECT */
