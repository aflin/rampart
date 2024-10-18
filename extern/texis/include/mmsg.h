/**********************************************************************
All MM3 communications use these definitions for sending messages
to the UI. The format of the line is as follows:

XXX msg_text\n

where XXX is the id of the message to a process trying to identify its
contents.
000-010 standard errors (below) that force failure of the process
011-099 messages indicate total failure of the process
100-199 messages indicate potential failure or hazard to the process
200-299 messages are informative messages on the operation of a process
300-399 messages are hit information coming from a mm engine
400-499 messages are non-error messages coming from a mindex engine
500-599 messages about query/hit logic
600-699 query debugging info
700-999 undefined as yet

format for 300 (MHITLOGIC):
XXX filename block_offset block_size [hit_offset1 hit_size1] [...]\n
**********************************************************************/

#ifndef MMSG_H
#define MMSG_H

#include "txtypes.h"                            /* for TXbool */

/**********************************************************************/
#ifndef TX_ONLY_DEFINE_TXEXIT

#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
#ifndef FAR                                        /* MSDOS sillyness */
#  define FAR
#endif
#ifndef CDECL                                      /* MSDOS sillyness */
#  define CDECL
#endif
/**********************************************************************/

/* All [txpmbuf_]putmsg() calls in signals must add TX_PUTMSG_NUM_IN_SIGNAL
 * to the error number, so that putmsg() handlers know the current
 * thread is in a signal handler.  TXinsignal() is not enough, as it
 * is a global (e.g. could indicate a separate thread is in a handler).
 * And setting a global __thread variable may be async-signal-unsafe.
 */
#define TX_PUTMSG_NUM_IN_SIGNAL         0x400   /* > any normal num */

#define TX_PUTMSG_NUM_SUPPRESS_ERRS     0x800

#define MERR  0                                       /* deadly error */
#define MWARN 100                                          /* warning */
#define MINFO 200                                      /* information */
#define MHIT  300                                /* hit begin and end */
#define MMDX  400                      /* mindex create and read msgs */
#define MJUNK 500                      /* spurious msgs about the hit */
#define MREPT 600                                /* parameters report */
#define MTRACEERR       700           /* error, only shown if tracing */
#define MTRACEWARN      800         /* warning, only shown if tracing */
#define MDATA 300                     /* default datamsg() msg number */

                                     /* errors - add to MERR or MWARN */
#define FOE  2                                         /* file open   */
#define FCE  3                                         /*      close  */
#define FME  4                                         /*      create */
#define FRE  5                                         /*      read   */
#define FWE  6                                         /*      write  */
#define FSE  7                                         /*      seek   */
#define FDE  8                                         /*      delete */
#define FNE  9                                         /*      rename */
#define FTE 10                                         /*      stat   */
#define MAE 11                                   /* memory allocation */
#define MDE 12                                 /* memory deallocation */
#define LKE 13                                         /*      lock */
#define UKE 14                                         /*      unlock */
#define UGE 15                                         /* usage error */
#define PRM 16                                         /* permission  */
#define FRK 17                                         /* fork        */
#define EXE 18                                         /* exec        */

#define SCE 60                                  /* [Java]script error */
#define SCW 61                                  /* [Java]script warning */

/* NOTE: see also verbose flags in httpi.h: */
#define HV_MSG_ResponseLine             (MINFO + 25)
#define HV_MSG_RequestLine              (MINFO + 26)
#define HV_MSG_ResponseHeaders          (MINFO + 27)
#define HV_MSG_RequestHeaders           (MINFO + 28)
#define HV_MSG_ResponseConnection       (MINFO + 29)
#define HV_MSG_RequestConnection        (MINFO + 30)
#define HV_MSG_ResponseFormattedIfText  (MINFO + 31)
#define HV_MSG_ResponseRawdocIfText     (MINFO + 32)
#define HV_MSG_ResponseRawdocHexIfBinary (MINFO + 33)
#define HV_MSG_RequestFormattedIfText   (MINFO + 34)
#define HV_MSG_RequestRawdocIfText      (MINFO + 35)
#define HV_MSG_RequestRawdocHexIfBinary (MINFO + 36)

/* future TXfetchVerbose_... values would follow */

#define TPF_MSG_BEFORE          (MINFO + 41)    /* tracepipe (before) */
#define TPF_MSG_AFTER           (MINFO + 42)    /* tracepipe (after) */
#define HTS_MSG_BEFORE          (MINFO + 43)    /* traceskt (before) */
#define HTS_MSG_AFTER           (MINFO + 44)    /* traceskt (after) */
#define HTTRD_MSG_BEFORE        (MINFO + 45)    /* tracedns (before) */
#define HTTRD_MSG_AFTER         (MINFO + 46)    /* tracedns (after) */
#define HTTRF_MSG_BEFORE        (MINFO + 47)    /* tracefetch (before) */
#define HTTRF_MSG_AFTER         (MINFO + 48)    /* tracefetch (after) */
#define HTTRACEAUTH_MSG_BEFORE  (MINFO + 49)    /* traceauth (before) */
#define HTTRACEAUTH_MSG_AFTER   (MINFO + 50)    /* traceauth (after) */
#define TXTRACESQL_MSG_BEFORE   (MINFO + 51)    /* tracesql (before) */
#define TXTRACESQL_MSG_AFTER    (MINFO + 52)    /* tracesql (after) */
#define TXTRACEKDBF_MSG_BEFORE  (MINFO + 53)    /* tracekdbf (before) */
#define TXTRACEKDBF_MSG_AFTER   (MINFO + 54)    /* tracekdbf (after) */
#define TXTRACEINDEX_MSG_BEFORE (MINFO + 55)    /* traceindex (before) */
#define TXTRACEINDEX_MSG_AFTER  (MINFO + 56)    /* traceindex (after) */
#define TXTRACEALARM_MSG_BEFORE (MINFO + 57)    /* tracealarm (before) */
#define TXTRACEALARM_MSG_AFTER  (MINFO + 58)    /* tracealarm (after) */

/*      SCE                     (MINFO + 60)       tracescript (error) */
/*      SCW                     (MINFO + 61)       tracescript (warning) */

#define TXTRACEDUMPTABLE_MSG_BEFORE (MINFO + 63)/* tracedumptable (before) */
#define TXTRACEDUMPTABLE_MSG_AFTER  (MINFO + 64)/* tracedumptable (after) */
#define VXTRACEURLCPCALLS_MSG_BEFORE (MINFO + 65)
#define VXTRACEURLCPCALLS_MSG_AFTER  (MINFO + 66)
#define TXTRACETNEF_CALLS_MSG_BEFORE (MINFO + 67)
#define TXTRACETNEF_CALLS_MSG_AFTER  (MINFO + 68)
#define TXTRACELICENSE_MSG           (MINFO + 69)    /* tracelicense */
#define TXTRACEMUTEX_MSG             (MINFO + 70)    /* traceMutex */
/* (MMINFO + 71) */
#define TXTRACEENCODING              (MINFO + 72)    /* tracencoding */
#define TXTRACEANYTOTX               (MINFO + 73)    /* traceanytotx */

/* Error codes specific for Texis */

#define TNF 75                  /* Table not found in Data Dictionary */
#define TAE 76                  /* Table already exists */
#define UID 77                  /* Non existant user-id */
#define XUI 78                  /* Insert into unique index failed */

#define PROCBEG   1                     /* start and end of a process */
#define PROCEND   2
#define MMPROCBEG   (MINFO+PROCBEG)           /* engine start and end */
#define MMPROCEND   (MINFO+PROCEND)

#define PROCBEG   1                     /* start and end of a process */
#define PROCEND   2
#define MMPROCBEG   (MINFO+PROCBEG)           /* engine start and end */
#define MMPROCEND   (MINFO+PROCEND)
#define MDXPROCBEG  (MMDX +PROCBEG)           /* mindex start and end */
#define MDXPROCEND  (MMDX +PROCEND)
#define TDBPROCBEG  MMPROCBEG                    /* 3db start and end */
#define TDBPROCEND  MMPROCEND

#define MMDXHIT     (MMDX+3)          /* mindex hit if it is printing */
#define MMDXFNAME   (MMDX+4)     /* mindex file that is being indexed */
#define MMDXENDHIT  (MMDX+5)                     /* end of mindex hit */

#define MENDHIT     (MHIT +1)                 /* end of metamorph hit */
#define MFILEINFO   (MINFO+3)                        /* file messages */
#define MTOTALS     (MINFO+4)                        /* search totals */
#define MHITLOGIC   (MJUNK+1)                            /* hit logic */
#define MQUERY      (MJUNK+2)              /* the query from the user */

#define TDBDOING    (MINFO+4)        /* what am i generally doing now */
#define TDBSUMMARY  (MINFO+5)             /* summary of what was done */
#define TDBQUERY    MQUERY                 /* the query from the user */
/**********************************************************************/
/*
** mmsgfh is where to output messages.
** usually set automatically from mmsgfname.
** setting mmsgfh to (FILE *)NULL will force reopen of
**    mmsgfname if it is not (char *)NULL.
** if mmsgfh is (FILE *)NULL and mmsgfname is (char *)NULL
**    or opening mmsgfname fails
**    mmsgfh will be set to stderr.
*/
#ifndef EPI_NO_MMSGFH_EXTERN
extern FILE FAR * FAR mmsgfh;
#endif /* !EPI_NO_MMSGFH_EXTERN */
/*
** set mmsgfname to the filename to send messages to
** or (char *)NULL to prevent mmsgfh from being changed
** do not set it to the empty string ("")
*/
extern char *mmsgfname;
/**********************************************************************/
#ifdef EPI_HAVE_STDARG
#define EPIPUTMSG() \
int CDECL \
epiputmsg(int n, const char *fn, const char *fmt, ...) \
{ \
va_list args; \
   va_start(args,fmt);
#else
#define EPIPUTMSG() \
int CDECL \
epiputmsg(va_alist) \
va_dcl \
{ \
va_list args; \
int n; \
const char *fn, *fmt; \
   va_start(args); \
   n  =va_arg(args,int   ); \
   fn =va_arg(args,const char *); \
   fmt=va_arg(args,const char *);
#endif                                                  /* EPI_HAVE_STDARG */
#define PUTMSG EPIPUTMSG
/**********************************************************************/
#define putmsg epiputmsg
                /* MAW 02-13-95 - no prototype if varargs used at all */
int epilocmsg(int);
#if ( !defined(MMSG_C) || defined(EPI_HAVE_STDARG) ) && !defined(va_dcl)
extern int  FAR CDECL putmsg     ARGS((int num,const char FAR *fn,const char FAR *fmt,...));
#endif
extern void       FAR okdmsg     ARGS((void));
extern int        FAR datamsg    ARGS((int eol,char FAR *buf,int sz,int count));
extern void       FAR fixmmsgfh  ARGS((void));
extern void       FAR closemmsg  ARGS((void));
extern void       FAR msgconfig  ARGS((int shownum,int showfunc,int enableoutput));
#if defined(_WIN32)
extern int  FAR       setmmsgfh    ARGS((int nfh));
extern char FAR * FAR setmmsgfname ARGS((char FAR *nfn));
#  ifdef va_start
extern void FAR       setmmsg      ARGS(( int (FAR *func)(int,char FAR *,char FAR *,va_list) ));
#  else /* !va_start */
extern void FAR       setmmsg      ARGS(( int (FAR *func)() ));
#  endif /* !va_start */
#endif
/**********************************************************************/
#include "txpmbuf.h"
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

typedef struct TXPM_tag                         /* a putmsg */
{
  struct TXPM_tag       *prev;                  /* previous message in list */
  struct TXPM_tag       *next;                  /* next message in list */
  double                when;                   /* time_t when msg reported */
  int                   num;                    /* MERR etc. */
  char                  *func;                  /* C function (optional) */
  char                  msg[8];                 /* message (var size) */
  /* rest of `msg' follows */
}
TXPM;
#define TXPMPN          ((TXPM *)NULL)
#define TXPM_MIN_SZ     (TXPMPN->msg - (char *)TXPMPN)

typedef enum TXPMBUFF_tag
{
  TXPMBUFF_PASS         = (1 << 0),     /* pass msgs through to putmsg() */
  TXPMBUFF_SAVE         = (1 << 1),     /* save msgs */
  TXPMBUFF_ALL          = (1 << 2) - 1  /* (internal) */
}
TXPMBUFF;
#define TXPMBUFFPN      ((TXPMBUFF *)NULL)
#define TXPMBUFF_DEFAULT        (TXPMBUFF_PASS)

TXPMBUF     *txpmbuf_open ARGS((TXPMBUF *pmbufclone));
TXPMBUF     *txpmbuf_close ARGS((TXPMBUF *pmbuf));
int         txpmbuf_setflags ARGS((TXPMBUF *pmbuf, TXPMBUFF flags, int on));
TXPMBUFF    txpmbuf_getflags ARGS((TXPMBUF *pmbuf));
size_t      txpmbuf_nmsgs ARGS((TXPMBUF *pmbuf));
TXPM        *TXpmbufGetMessageList ARGS((TXPMBUF *pmbuf));
int         txpmbuf_clrmsgs ARGS((TXPMBUF *pmbuf));
int         TXpmbufCopyMsgs ARGS((TXPMBUF *destPmbuf, TXPMBUF *srcPmbuf,
                                  int minErrNum, size_t minMsgIdx));
const char *TXpmbufGetPrefix(TXPMBUF *pmbuf);
int         TXpmbufSetPrefix(TXPMBUF *pmbuf, const char *prefix);
int         TXpmbufGetErrMapNum(TXPMBUF *pmbuf);
TXbool      TXpmbufSetErrMapNum(TXPMBUF *pmbuf, int errMapMum);
double      TXpmbufGetCurrentPutmsgTime(void);
int         TXpmbufSetChainedPmbuf(TXPMBUF *pmbuf, TXPMBUF *chainedPmbuf);


/* logs to `pmbuf' and/or putmsg(), according to `pmbuf->flags';
 * NULL `pmbuf' is equivalent to TXPMBUFF_PUTMSG-only:
 */
extern int  FAR CDECL txpmbuf_putmsg ARGS((TXPMBUF *pmbuf, int num,
                               const char FAR *fn, const char FAR *fmt, ...));

#endif /* !TX_ONLY_DEFINE_TXEXIT */

/******************************************************************/

/* Exit codes.  These are semi-standardized, so no inserts/mods and
 * only append to end.  Should eventually be standardized acros all
 * Thunderstone programs.
 * NOTE: see also copy of this in installshield/Script Files/setup.rul
 * NOTE: do not exceed 127 codes (Unix exit() limit)
 * I(tok, desc)
 */
#define TXEXIT_SYMBOLS_LIST                                     \
I(OK,                   "")                                     \
I(1,                    "")                                     \
I(2,                    "")                                     \
I(USERINTERRUPT,        "User interrupt or signal")             \
I(CANNOTOPENMONITORLOG, "Cannot open monitor log")              \
I(ABEND,                "ABEND")                                \
I(CANNOTEXECMON,        "Cannot exec monitor sub-process")      \
I(CANNOTOPENDDIC,       "Cannot re-open DDIC")                  \
I(UNKPROG,              "Unknown program")                      \
I(ALREADYRUN,           "Texis Monitor already running")        \
I(BADLICENSEFILE,       "Invalid license file")                 \
I(SCHEDSERVERFAILED,    "Schedule server init failed")          \
I(NAMEDPIPEFAILED,      "Named pipe failed")                    \
I(DBOPENFAILED,         "Database open failed")                 \
I(PERMSFAILED,          "User/pass failed")                     \
I(WEBSERVERFAILED,      "Web server init failed")               \
I(NOINDEX,              "No such index")                        \
I(TBLOPENFAILED,        "Table open failed")                    \
I(INDEXOPENFAILED,      "Index open failed")                    \
I(LOCKOPENFAILED,       "Lock open failed")                     \
I(INVALIDINSTALLDIR,    "Invalid install dir specified")        \
I(REGSETVALUEFAILED,    "Registry set value failed")            \
I(REGSERVICEFAILED,     "Register service failed")              \
I(INCORRECTUSAGE,       "Incorrect usage")                      \
I(CANNOTOPENINPUTFILE,  "Cannot open input file")               \
I(CANNOTREADINPUTFILE,  "Cannot read input file")               \
I(NONKDBFINPUTFILE,     "Non-KDBF input file")                  \
I(INTERNALERROR,        "Internal error")                       \
I(UNKNOWNERROR,         "Unknown error")                        \
I(CORRUPTINPUTFILE,     "Corrupt input file")                   \
I(TIMEOUT,              "Timeout")                              \
I(COREDUMPREQUESTED,    "Core dump requested")                  \
I(CPDBSERVERFAILED,     "cpdb server init failed")              \
I(OUTOFMEMORY,          "Out of memory")                        \
I(CHECKSUMDIFFERS,      "Computed checksum differs from file checksum")  \
I(CHECKSUMNOTFOUND,     "Checksum not found in file")           \
I(CANNOTEXECSUBPROCESS, "Cannot exec sub-process")              \
I(LICENSEVIOLATIONERROR,"License violation or error")           \
I(ERRORDURINGEXIT,      "Secondary error during exit")          \
I(PERMISSIONDENIED,     "Permission denied")                    \
I(CANNOTCONNECTTOREMOTESERVER, "Cannot connect to remote server")       \
I(CANNOTCONNECTTOLOCALSERVER,  "Cannot connect to local server")        \
I(CANNOTWRITETOTABLE,   "Cannot write to table")                \
I(CANNOTOPENOUTPUTFILE, "Cannot open output file")              \
I(CANNOTOPENERRORLOG,   "Cannot open error log")                \
I(CANNOTWRITETOFILE,    "Cannot write to file")                 \
I(GENSERVERFAILED,      "Genserver init failed")                \
I(TERMINATED,           "Terminated by signal or event")        \
I(FLOATINGPOINTEXCEPTION, "Floating-point exception")           \
I(SQLSTATEMENTFAILED,   "SQL statement failed")                 \
I(SCRIPTCOMPILEFAILED,  "Vortex script compile failed")         \
I(LIBRARYACTIONFAILED,  "Vortex module library action failed")  \
I(CANNOTRENAMEFILE,     "Cannot rename file")                   \
I(CANNOTSCHEDULESCRIPT, "Cannot schedule Vortex script")        \
I(CANNOTGETCONFIGSETTINGS, "Cannot get config settings")        \
I(INCORRECTENVIRONMENT, "Incorrect environment setup")          \
I(CANNOTEXECUTESCRIPT,  "Cannot execute Vortex script")         \
I(NOLICENSE,            "No current license")                   \
I(NEWERVERSIONINSTALLED,"A newer version of Texis is installed")\
I(RUNNINGASROOT,        "Effective user is root")               \
I(CPDBACTIONFAILED,     "cpdb action failed")                   \
I(CANNOTWRITETOSTDOUT,  "Cannot write to standard output")      \
I(NOTHINGFOUNDTOPATCH,  "Nothing found in file to patch")       \
I(CannotLoadSharedLibrary, "Cannot load shared library")

typedef enum TXEXIT_tag
{
#undef I
#define I(tok, desc)    TXEXIT_##tok,
TXEXIT_SYMBOLS_LIST
#undef I
  TXEXIT_NUM                                    /* must be last */
}
TXEXIT;
#define TXEXITPN        ((TXEXIT *)NULL)

#ifndef TX_ONLY_DEFINE_TXEXIT

extern const char * const       TxExitDescList[];

#endif /* !TX_ONLY_DEFINE_TXEXIT */

/******************************************************************/

#endif                                                      /* MMSG_H */
