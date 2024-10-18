/* -=- kai-mode: John -=- */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <sys/stat.h>
#ifndef _WIN32
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#endif /* !_WIN32 */
#include "texint.h"
#include "cgi.h"
#include "ezsock.h"
#include "fcgi.h"
#include "pool.h"
#include "httpi.h"
#include "txlic.h"


/* ------------------------ monitor internal scheduler --------------------- */

typedef struct SCHEDULER
{
	int	maxtasks;
	int	go;
	const char	*stopReason;
	void	*tasks;
	int	hightask, curtask;
        int     *fds, *idx;
	EWM	*stats;
} SCHEDULER;
#define SCHEDULERPN     ((SCHEDULER *)NULL)

typedef enum SCF_tag
{
  SCF_ALIGN     = (1 << 0),
  SCF_RUNNING   = (1 << 1),
  SCF_REQSTOP   = (1 << 2)
}
SCF;
#define SCFPN   ((SCF *)NULL)

SCHEDULER *tx_closescheduler ARGS((SCHEDULER *sched));
SCHEDULER *initscheduler ARGS((int));
int addjob (SCHEDULER *sched, int (*func)(void *usr, time_t now), void *usr,
	    double delay, int aligndelay, int skt, const char *name);
int deljob(SCHEDULER *sched, int jobno, int verbose, const char *reason);
int delayjob ARGS((SCHEDULER *sched, int jobno, double delay));
int runsched ARGS((SCHEDULER *sched, int verbose));
char *txsched_getflag ARGS((SCHEDULER *sched, SCF flag));
int txsched_signaltask ARGS((SCHEDULER *sched, int task));
void txsched_procreap ARGS((DDIC *ddic, TXMUTEX *ddicMutex, int verbose));

extern void (*TXsysSchedKickReaperFunc) ARGS((void));

/* ----------------------------- monsock.c --------------------------------- */

/* http-like monitor server.
 * Used as Vortex <SCHEDULE> server, and temp Windows web server.
 */

/* FastCGI client: */
typedef struct MONITOR_FCGI_tag
{
	struct MONITOR_FCGI_tag	*next;
	char		*urlprefix;	/* const: URL prefix */
	TXcriticalSection	*cs;	/* var: mux access to `numinuse' */
	int		numinuse;	/* var: mux access to `fcgi' */
	FCGI		*fcgi;		/* var: obj to talk to server */
	int		prestart;	/* const */
}
MONITOR_FCGI;
#define MONITOR_FCGIPN	((MONITOR_FCGI *)NULL)

typedef enum MONITOR_SERVER_TYPE_tag
{
	MONITOR_SERVER_NONE,
	MONITOR_SERVER_SCHEDULE,
	MONITOR_SERVER_WEB,
	MONITOR_SERVER_DISTRIBDBF
}
MONITOR_SERVER_TYPE;

/* <SCHEDULE>/web/JDBF server object: */
/* `const' means does not change for duration of server: */
typedef struct MONITOR_SERVER_tag
{
	TXPMBUF		*pmbuf;		/* const/opt., for msgs */
	char		*what;		/* const: "schedule" or "web" */
	char		*texisIniSectionName; /* const, for msgs */

	int		numconn;	/* dynamic: #current connections */
	TXcriticalSection	*conncsect;	/* access control */

	int		*goPtr;		/* dynamic: may be set elsewhere */
	const char	**stopReasonPtr;/* dynamic: may be set elsewhere */
	int		stopOnFail;	/* const: set *go = 0 on failure */
	int		maxbacklog;	/* const */

	TXsockaddr	*addrs;		/* local to server only */
	size_t		numAddrs;
	HTSKT		**skts;		/* local to server only */

	double		timeout;	/* -1: infinite */
	MONITOR_SERVER_TYPE	servertype;
	char		*cwd;		/* const: DOCUMENT_ROOT + / */
	size_t		cwdlen;		/* const: strlen(cwd) */
	DDIC		**ddic, *myddic;
	TXMUTEX		*ddicMutex;	/* const: mutex for either DDIC */
	int		maxclients;	/* const: max ok concurrent conn. */
	size_t		maxhdrsz;	/* const */
	int		liveoutput;	/* const */
	char		*user;		/* const */
	char		*pass;		/* const */
	int		sslMode;	/* const */
	void		*sslCtx;	/* const (but OpenSSL mods w/locks) */
	byte		sslVerifyErrMask[TXsslVerifyErrMaskByteSz]; /* const*/
	int		sslVerifyDepth;	/* const (<0 == no limit) */
	HTSSLPROT	sslProtocols;	/* const HTSSLPROT_... bits */
	char		*sslCiphers[TX_SSL_CIPHER_GROUP_NUM];	/* const */
	int		fastlogon;	/* const */
	int		statusEnabled;	/* const */
	int		maxConnRequests;/* const */
	time_t		maxConnLifetime;/* const */
	time_t		maxConnIdleTime;/* const */
	TXfetchVerbose	traceRequests;	/* const */
	int		traceAuth;	/* const */
	int		schedulerVerbose; /* const */
	byte		schedulerEnabled;	/* const */
	/* POST /license.upd service controlled by licUpdUser */
	byte		createLocksEnabled;	/* const */
	byte		licenseInfoEnabled;	/* const */
	byte		logEmptyRequests;	/* const */

	/* [License Update] settings: */
	char		*licUpdUser;	/* const */
	int		(*licUpdCb) ARGS((void *usr, time_t now)); /* const */
	TXMUTEX		*licUpdMutex;	/* const */
	int		licUpdRequirePassword;	/* const */
	int		licUpdRequireSecure;	/* const */
	int		licUpdTerseMessages;	/* const */
	TXsockaddr	licUpdRequireRemoteNetwork;  /* const */
	int		licUpdRequireRemoteNetworkBits;	/* const */

	char	*(*licGetExtraInfoCb)(void *usr);	/* const */
	void	*licGetExtraInfoUsr;			/* const */

	/* web-server specific: */
	char		*vortexpath;	/* const: URL for Vortex scripts */
	char		*vortexByExtPath;/* const: URL for by-ext. scripts */
	char		**texisargv;	/* const: path to texis exe args */
	int		texisargc;	/* const: length of `texisargv' */
	char		**indexfiles;	/* const: list of index.html names */
	char		*servername;	/* const: server name (if isweb) */
	char		*serversoftware;/* const: server software */
	int		dodirindex;
	int		dirRobotsIndex;
	int		dirRobotsFollow;
	int		transferlogfd;	/* const: != -1: transfer log fd */
	char		*transferlog;	/* const */
	char		*transferLogFormat;	/* const (NULL: default) */
	CGISL		*extraenv;	/* const: (optional) */
	int		numextraenv;	/* const */
	MONITOR_FCGI	*mfcgi;		/* const */
	unsigned	allowfilemask;	/* const */
	unsigned	allowdirmask;	/* const */
	int		badcontentlengthworkaround;	/* const */
	int		doMultiViews;	/* const */
	LVXF		vxFeatureFlags;	/* const */
	/* These function pointers avoid netex1 etc. having to link against
	 * Vortex lib for vx_openglob() etc.; these functions should migrate
	 * to Texis lib someday WTF.  See also TXFindNegotiatedFileServerInfo,
	 * MONITOR_SERVER:
	 */
	VSTKSTAT *(*globOpenFunc) ARGS((CONST char *path,
					CONST char *fixedEnd, int flags));
	VSTKSTAT *(*globCloseFunc) ARGS((VSTKSTAT *vss));
	char     *(*globNextFunc) ARGS((VSTKSTAT *vss, EPI_STAT_S *st,
					int *depth));
	/* web-server `memPool'-alloced structures: */
	POOL		*memPool;	/* const; optional */
	VHEXT		**mimeTypes;	/* const; sorted by extension */
	int		numMimeTypes;	/* const */
	VHEXT		**mimeEncodings;/* const; sorted by extension */
	int		numMimeEncodings;/* const */
}
MONITOR_SERVER;
#define MONITOR_SERVERPN        ((MONITOR_SERVER *)NULL)

/* structs to pass args to server at open: */

typedef struct TXCONN_FCGI_tag
{
	struct TXCONN_FCGI_tag	*next;
	char		*urlprefix;	/* required */
	char		*commandline;	/* optional */
	char		*bindpath;	/* optional */
	size_t		bufsz;		/* optional (0==default) */
	FCGIFLAG	flags;		/* optional */
	int		prestart;	/* nonzero: start server before req */
}
TXCONN_FCGI;
#define TXCONN_FCGIPN	((TXCONN_FCGI *)NULL)

/* struct to pass to tx_openmonserv() to configure a web server: */
typedef struct TXCONN_INFO_tag
{
	MONITOR_SERVER_TYPE	servertype;
	TXPMBUF	*pmbuf;		/* opt.; will clone */
	char	*what;		/* opt.; e.g. "web" or "schedule" */
	CONST char	*texisIniSectionName;	/* opt.; for msgs */
	char *documentroot;	/* required */
	int	stopOnFail;	/* opt. */
	int maxbacklog;		/* opt. */
	int timeout;		/* opt. */
	TXsockaddr	*addrs;	/* opt. */
	size_t		numAddrs;
	int  maxclients;	/* opt.; max concurrent connections allowed */
	size_t	maxhdrsz;	/* opt. */
	int	liveoutput;	/* opt. non-zero: TXPDF_ANYDATARD */
	char	*user;		/* opt. Windows user to run sub-process as */
	char	*pass;		/* opt. Windows pass for `user' */
	int	sslMode;	/* 0: SSL off  1: optional (Upgrade)  2: on */
	int	sslPassPhraseDialog;	/* 0: none  1: builtin */
	char	*sslCertificateChainFile;/* opt. server CA chain file */
	char	*sslCertificateFile;	/* opt. server cert file (opt w/key)*/
	char	*sslCertificateKeyFile;	/* opt. server private key file */
	char	*sslCACertificateFile;	/* opt. client-auth-ok CA certs file*/
	/* wtf	*sslCACertificatePath */
	char	*sslCADNRequestFile;	/* opt. client-auth-request CA file */
	/* wtf	*sslCADNRequestPath */
	byte	sslVerifyErrMask[TXsslVerifyErrMaskByteSz];	/* opt. */
	int	sslVerifyDepth;	/* default 1; <0 == no limit */
	HTSSLPROT	sslProtocols;	/* opt. HTSSLPROT_... bits */
	char	*sslCiphers[TX_SSL_CIPHER_GROUP_NUM];	/* opt. */
	int	fastlogon;	/* opt. non-zero: CreateProcessWithLogonW() */
	int	statusEnabled;	/* !0: support GET / requests (watchdog) */
	int	maxConnRequests;/* 0: default  -1: unlimited */
	int	maxConnLifetime;/* 0: default  -1: unlimited */
	int	maxConnIdleTime;/* 0: default  -1: unlimited */
	TXfetchVerbose	traceRequests;	/* opt. flags (ala http verbose) */
	int	traceAuth;	/* opt. ala HTOBJ.traceauth (limit support) */
	int	schedulerVerbose; /* opt. [Scheduler] Verbose */
	byte	schedulerEnabled;
	/* POST /license.upd service controlled by licUpdUser */
	byte	createLocksEnabled;
	byte	licenseInfoEnabled;
	byte	logEmptyRequests;

	/* [License Update] settings: */
	char	*licUpdUser;	/* opt. license update user */
	int	(*licUpdCb) ARGS((void *usr, time_t now)); /* const */
	TXMUTEX	*licUpdMutex;	/* opt. (caller owns) */
	int	licUpdRequirePassword;	/* non-zero: require password */
	int	licUpdRequireSecure;	/* non-zero: require secure conn. */
	int	licUpdTerseMessages;	/* non-zero: short msgs for security*/
	TXsockaddr	licUpdRequireRemoteNetwork;  /* only from this net */
	int	licUpdRequireRemoteNetworkBits;	/* netmask bits (-1==off) */

	char	*(*licGetExtraInfoCb)(void *usr);	/* opt. */
	void	*licGetExtraInfoUsr;			/* opt. */

	/* web-server specific: */
	char *vortexpath;	/* opt. URL prefix for Vortex scripts*/
	char *vortexByExtPath;	/* opt. URL prefix for by-extension scripts */
	char *texisexe;		/* opt. path to texis executable and args */
	char *indexfiles;	/* opt. whitespace-separated index.html names*/
	int	dodirindex;	/* opt. */
	int	dirRobotsIndex;	/* opt. */
	int	dirRobotsFollow;/* opt. */
	char	*transferlog;	/* opt. */
	const char	*transferLogFormat;	/* opt. */
	CGISL	*extraenv;	/* opt. */
	TXCONN_FCGI	*ifcgi;	/* opt. */
	unsigned	allowfilemask;	/* const */
	unsigned	allowdirmask;	/* const */
	int		badcontentlengthworkaround;	/* const */
	int		doMultiViews;
	/* These function pointers avoid netex1 etc. having to link against
	 * Vortex lib for vx_openglob() etc.; these functions should migrate
	 * to Texis lib someday WTF.  See also TXFindNegotiatedFileServerInfo,
	 * MONITOR_SERVER:
	 */
	VSTKSTAT *(*globOpenFunc) ARGS((CONST char *path,
					CONST char *fixedEnd, int flags));
	VSTKSTAT *(*globCloseFunc) ARGS((VSTKSTAT *vss));
	char     *(*globNextFunc) ARGS((VSTKSTAT *vss, EPI_STAT_S *st,
					int *depth));
	/* web-server `memPool'-alloced structures: */
	POOL		*memPool;	/* opt.; server will own it */
	VHEXT		**mimeTypes;	/* sorted by extension */
	int		numMimeTypes;
	VHEXT		**mimeEncodings;/* sorted by extension */
	int		numMimeEncodings;
}
TXCONN_INFO;
#define TXCONN_INFOPN	((TXCONN_INFO *)NULL)
#define	TXCONN_INFOPPN	((TXCONN_INFO **)NULL)

typedef struct TEXIS_STRING
{
	TXPMBUF	*pmbuf;				/* not cloned wtf */
	size_t maxlength;
	char *string;
} TEXIS_STRING;

typedef enum TXHCF_tag
{
	TXHCF_PASTCGIHDRS	= (1 << 0),	/* past CGI response hdrs */
	TXHCF_PASTRESPHDRS	= (1 << 1),	/* past server response hdrs*/
	TXHCF_PASTREQHDRS	= (1 << 2),	/* past request headers */
	TXHCF_ISBOLTON		= (1 << 3),	/* request is bolton Vortex */
	TXHCF_ISCGI		= (1 << 4),	/* request is CGI program */
	TXHCF_GOTREQCONTENT	= (1 << 5),	/* have read request content*/
	TXHCF_REQCONNUPGRADE	= (1 << 6),	/* got Connection: Upgrade */
	TXHCF_REQCONNKEEPALIVE	= (1 << 7),	/*got Connection: Keep-Alive*/
	TXHCF_REUSABLE		= (1 << 8),	/* connection is reusable */
	TXHCF_REQWASSECURE	= (1 << 9),	/* request was over SSL */
	TXHCF_GOTXFERERR	= (1 << 10)	/* got transfer error */
}
TXHCF;
/* per-request flags: */
#define TXHCF_REQFLAGS	(TXHCF_PASTCGIHDRS | TXHCF_PASTRESPHDRS | \
			TXHCF_PASTREQHDRS | TXHCF_ISBOLTON | TXHCF_ISCGI | \
			TXHCF_GOTREQCONTENT | \
			TXHCF_REQCONNUPGRADE | TXHCF_REQCONNKEEPALIVE | \
			TXHCF_REQWASSECURE | TXHCF_GOTXFERERR)

/* Per-connection object: */
typedef struct MONITOR_CONN_tag
{
	MONITOR_SERVER	*ms;		/* shared parent server */
	HTBUF *reqContent;			/* request content */
	char	*requestLine;			/* request line */
	HTSKT *skt;			/* client conn. skt */
	HTSSLPROT	sslProtocols;	/* bits */
	double		deadline;		/* -1: no limit */
	HTMETH method;
	TXsockaddr	clientAddr;	/* network byte order */
	char		clientIPStr[TX_SOCKADDR_MAX_STR_SZ];	/* IP only */
	unsigned	clientPort;	/* hardware order */
	TXsockaddr	localAddr;	/* network byte order */
	char		localIPStr[TX_SOCKADDR_MAX_STR_SZ];	/* IP only */
	unsigned	localPort;	/* hardware order */
	int mime_flag;
	TEXIS_STRING encodedurl, decodedurl, protocol, query;
	TEXIS_STRING origfilename;	/* ms->cwd + [path] */
	char		*origOrigfilename;
	TEXIS_STRING pathinfo, pathtranslated;
	TEXIS_STRING encodings;
	TEXIS_STRING x_schedule, x_comments, x_schedule_options;
	TEXIS_STRING x_schedule_start, x_schedule_urlend;
	TEXIS_STRING contenttype;
	TEXIS_STRING	acceptEncodingReqHdr;
	int		gotAcceptEncoding;
	int x_schedule_pid;
	char *method_str;
	time_t	if_modified_since;
	int		numRequests;		/* number of requests seen */
	EPI_OFF_T	respContentLength;	/* response Content-Length */
	EPI_OFF_T	respContentBytesSent;	/* actual content sent */
	time_t		respLastModified;	/* != -1: Last-Modified */
	EPI_OFF_T	docoff, reqContentLength;
	HTSSLPROT	reqUpgradeSslProtocols;	/* request Upgrade protocols*/
	TXHCF flags;
	int status;
	char		*statusline;	/* text after HTTP code */
	CGISL		*reqhdrs;	/* request headers from client */
	CGISL		*requestCookies;
	char		*respContentType;	/* response Content-Type */
	char		*respContentLocation;	/* response Content-Location*/
	char		*locationurl;	/* Location: URL */
	CGISL		*outhdrs;	/* headers to output (e.g. from CGI)*/
	EPI_STAT_S	sb;
	int		sberrno;
	time_t		connStartTime;	/* when connection started */
	double		reqStartTime;	/* when current request started */
	time_t		idleStartTime;	/* when idle started */
#ifdef HAVE_JDBF
	EPI_OFF_T	ddbf_range;
	long		ddbf_command;
#endif
}
MONITOR_CONN;
#define MONITOR_CONNPN	((MONITOR_CONN *)NULL)

MONITOR_SERVER *tx_closemonserv ARGS((MONITOR_SERVER *monserv));
MONITOR_SERVER *tx_openmonserv ARGS((TXCONN_INFO *conninfo, int userPromptOk));
int   TXmonservStartServer(MONITOR_SERVER *ms, DDIC **ddicPtr,
                           TXMUTEX *ddicMutex, CONST char *db, int *goPtr,
			   const char **stopReasonPtr);
int	tx_monitorhandleconn(MONITOR_SERVER *ms, size_t addrIdx);
TXTHREADRET TXTHREADPFX tx_runmonserv ARGS((void *ms));

int TXmonservLogTransfer(MONITOR_CONN *mc, TXRUSAGE defaultWho, TXbool inSig);

void txmon_kickreaper ARGS((void));

#endif /* !SCHEDULER_H */
