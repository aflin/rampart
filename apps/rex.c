/*
#define AUTHOR_COPY 1
#define TEST 1
#define UNIX  1
#define PRIME 1
#define MSDOS 1
*/

/* pbr 9/14/90 chgd TESTREX to TEST for uniformity  */
/* pbr 9/14/90 added fs->root  */
/* pbr 9/14/90 deleted fs->fastest  */
/* pbr 9/14/90 added >> operation */
/* pbr 9/14/90 deleted and rewrote getrex() to improve readability */
/* pbr 9/14/90 deleted read sector and synsearch stuff */
/* pbr 9/14/90 moved setting of fs->backwards from getrex() to openrex()  */
/* pbr 9/14/90 moved initskiptab() from openfpm() to openrex()  */
/* pbr 9/14/90 made initskiptab() direction aware to improve open speed */
/* pbr 9/14/90 added firstexp() and lastexp()  */
/* pbr 9/14/90 added notlines  */
/* pbr 12/02/90 fixed offlenrex -O to report correct offsets  */
/* PBR 01-02-91 added support for NULL expression matching */
/* PBR 04-24-91 added exclusion operators */
/* MAW 04-13-93 fix obob in backwards repeated search */
/* MAW 06-09-93 alloc srchbuf after exprs in main() */
/* JMT 04-17-95 changed define for SMALL_MEM from MSDOS */
/* MAW/PBR/JMT 09-09-96 added subexpression replacement to -R */
/* MAW/PBR/JMT 09-09-96 added subexpression hex print to -R */
/* MAW/PBR/JMT 09-09-96 deleted single char hex print from -R */
/* PBR 5/14/97 added buffer anchors */

/**********************************************************************/
#include "txcoreconfig.h"
/**********************************************************************/


#if !defined(MVS) && !defined(_WIN32)
#  define UNIX  1
#endif

#if defined(_WIN32)
#  include <fcntl.h>                                 /* for setmode() */
#  include <io.h>                                    /* for setmode() */
#  define EPI_FREAD "rb"
#  define EPI_FWRITE "wb"
#else
#define EPI_FREAD "r"
#define EPI_FWRITE "w"
#endif /* _WIN32 */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#ifdef unix
#  ifdef UMALLOC3X
#     include <malloc.h>
#  endif
#endif
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#ifdef EPI_HAVE_LOCALE_H
#  include <locale.h>
#endif /* EPI_HAVE_LOCALE_H */
#ifdef TEST
#  include <signal.h>
#  ifdef EPI_HAVE_UNISTD_H
#    include <unistd.h>                         /* for getpid() */
#  endif /* EPI_HAVE_UNISTD_H */
#  ifdef _WIN32
#    include <process.h>                        /* for getpid() */
#  endif /* _WIN32 */
#endif
#include "sizes.h"
#include "os.h"
#include "txtypes.h"
#include "pm.h"
#include "mmsg.h"
#ifdef USELICENSE
#include "license.h"
#endif
#ifdef EPI_BUILD_RE2
#  include "cre2.h"
#endif /* EPI_BUILD_RE2 */

#ifdef _WIN32
#  define _huge
#endif

/************************************************************************/
                  /* 2/2/90 PBR ADDED SUPPORT FOR MVS */
/************************************************************************/


#define EESC '\\'
#define OSET '['                           /* open-set */
#define CSET ']'                           /* close-set */
#define EBEG '{'
#define EEND '}'
#define ENOT '^'
#define EBOL '^'
#define OPEN_TOKEN         '<'
#define CLOSE_TOKEN        '>'

typedef struct TXrexParseState_tag
{
  const char    *continuedSubExprStart;         /* too-long subexpr start */
  size_t        continuedSubExprStartOffset;
  size_t        subExprIndex;                   /* subexpression #, from 0 */
  byte          respectCase;                    /* after `\R' */
  byte          literal;                        /* inside `\L...\L' */
}
TXrexParseState;

#define TXrexParseState_INIT(s) memset((s), 0, sizeof(TXrexParseState))

static const char       OutOfMem[] = "Out of memory";
static const char       Ellipsis[] = "...";
static const char       ProgramName[] = "rex";
static const char       Re2Token[] = "re2";
#define RE2_TOKEN_LEN   (sizeof(Re2Token) - 1)
static const char       RexToken[] = "rex";
#define REX_TOKEN_LEN   (sizeof(RexToken) - 1)
static const char       NotSupportedForRe2[] =
  "REX: Function not supported for RE2 expressions";
#define ERR_IF_RE2(fs, action) { if ((fs)->re2) { \
      putmsg(MERR + UGE, __FUNCTION__, NotSupportedForRe2); action; } }

static int      RexSaveMem = 0; /* 1: save mem at expense of speed */

#ifdef EPI_ENABLE_RE2
#  define Re2Enabled    1
#else /* !EPI_ENABLE_RE2 */
static int      Re2Enabled = -1;
#endif /* !EPI_ENABLE_RE2 */

int     TXunneededRexEscapeWarning = 1;

/************************************************************************/
#ifdef DEBUG
#  define DEBUGFFS(a) debugffs(a)
void
debugffs(fs)
FFS *fs;
{
 int i, j;
 byte *s;

 ERR_IF_RE2(fs, return);
 s=fs->exp;
 for(i=0;*s;s++,i++)
    {
     printf("%c:%d  %c:%d %p\n",tolower(*s),fs->skiptab[tolower(*s)],
                                toupper(*s),fs->skiptab[toupper(*s)],
                                fs->setlist[i]);
    }
 s=fs->exp;
 for(i=0;i<DYNABYTE;i++)
    {
     if(strchr((char *)s,toupper(i))==(char *)NULL &&
        strchr((char *)s,tolower(i))==(char *)NULL &&
        fs->skiptab[i]!=fs->patsize)
         printf("0x%02x==%d\n",i,fs->skiptab[i]);
    }
}
#else                                                        /* DEBUG */
#  define DEBUGFFS(a)
#endif                                                       /* DEBUG */
/************************************************************************/

#ifdef TEST
/* wtf we do not link in texis lib: */

#  define TXentersignal() (void)TX_ATOMIC_INC(&TxSignalDepthVar)
#  define TXexitsignal()  (void)TX_ATOMIC_DEC(&TxSignalDepthVar)
#  define TXinsignal()    (TxSignalDepthVar > 0)

VOLATILE TXATOMINT      TxSignalDepthVar = 0;
static const char       TxSigProgNameDef[] = "Process";
static char             *TxSigProgName = (char *)TxSigProgNameDef;
typedef struct SIGNAME_tag
{
  int   val;
  char  *name;
}
SIGNAME;
static const SIGNAME	Sigs[] =
{
#ifdef _WIN32
  /* These codes are from _exception_code() in an __except() block: */
  { EXCEPTION_ACCESS_VIOLATION,	        "ACCESS_VIOLATION" },
  { EXCEPTION_ARRAY_BOUNDS_EXCEEDED,    "ARRAY_BOUNDS_EXCEEDED" },
  { EXCEPTION_BREAKPOINT,               "BREAKPOINT" },
  { EXCEPTION_DATATYPE_MISALIGNMENT,    "DATATYPE_MISALIGNMENT" },
  { EXCEPTION_FLT_DENORMAL_OPERAND,     "FLT_DENORMAL_OPERAND" },
  { EXCEPTION_FLT_DIVIDE_BY_ZERO,       "FLT_DIVIDE_BY_ZERO" },
  { EXCEPTION_FLT_INEXACT_RESULT,       "FLT_INEXACT_RESULT" },
  { EXCEPTION_FLT_INVALID_OPERATION,    "FLT_INVALID_OPERATION" },
  { EXCEPTION_FLT_OVERFLOW,             "FLT_OVERFLOW" },
  { EXCEPTION_FLT_STACK_CHECK,          "FLT_STACK_CHECK" },
  { EXCEPTION_FLT_UNDERFLOW,            "FLT_UNDERFLOW" },
  { EXCEPTION_ILLEGAL_INSTRUCTION,      "ILLEGAL_INSTRUCTION" },
  { EXCEPTION_IN_PAGE_ERROR,            "IN_PAGE_ERROR" },
  { EXCEPTION_INT_DIVIDE_BY_ZERO,       "INT_DIVIDE_BY_ZERO" },
  { EXCEPTION_INT_OVERFLOW,             "INT_OVERFLOW" },
  { EXCEPTION_INVALID_DISPOSITION,      "INVALID_DISPOSITION" },
  { EXCEPTION_NONCONTINUABLE_EXCEPTION, "NONCONTINUABLE_EXCEPTION" },
  { EXCEPTION_PRIV_INSTRUCTION,         "PRIV_INSTRUCTION" },
  { EXCEPTION_SINGLE_STEP,              "SINGLE_STEP" },
  { EXCEPTION_STACK_OVERFLOW,           "STACK_OVERFLOW" },
#endif /* _WIN32 */
  { EPI_SIGHUP,         "SIGHUP" },
  { EPI_SIGINT,         "SIGINT" },
  { EPI_SIGQUIT,        "SIGQUIT" },
  { EPI_SIGILL,         "SIGILL" },
  { EPI_SIGTRAP,        "SIGTRAP" },
  { EPI_SIGABRT,        "SIGABRT" },
  { EPI_SIGIOT,         "SIGIOT" },
  { EPI_SIGBUS,         "SIGBUS" },
  { EPI_SIGFPE,         "SIGFPE" },
  { EPI_SIGKILL,        "SIGKILL" },
  { EPI_SIGUSR1,        "SIGUSR1" },
  { EPI_SIGSEGV,        "SIGSEGV" },
  { EPI_SIGUSR2,        "SIGUSR2" },
  { EPI_SIGPIPE,        "SIGPIPE" },
  { EPI_SIGALRM,        "SIGALRM" },
  { EPI_SIGTERM,        "SIGTERM" },
  { EPI_SIGSTKFLT,      "SIGSTKFLT" },
  { EPI_SIGCLD,         "SIGCLD" },
  { EPI_SIGCHLD,        "SIGCHLD" },
  { EPI_SIGCONT,        "SIGCONT" },
  { EPI_SIGSTOP,        "SIGSTOP" },
  { EPI_SIGTSTP,        "SIGTSTP" },
  { EPI_SIGTTIN,        "SIGTTIN" },
  { EPI_SIGTTOU,        "SIGTTOU" },
  { EPI_SIGURG,         "SIGURG" },
  { EPI_SIGXCPU,        "SIGXCPU" },
  { EPI_SIGXFSZ,        "SIGXFSZ" },
  { EPI_SIGVTALRM,      "SIGVTALRM" },
  { EPI_SIGPROF,        "SIGPROF" },
  { EPI_SIGWINCH,       "SIGWINCH" },
  { EPI_SIGIO,          "SIGIO" },
  { EPI_SIGPOLL,        "SIGPOLL" },
  { EPI_SIGPWR,         "SIGPWR" },
  { EPI_SIGUNUSED,      "SIGUNUSED" },
  { EPI_SIGBREAK,       "SIGBREAK" },
  /* These may not exist on some systems, and/or their numbers may
   * overlap a signal name that *does* exist; e.g. SIGEMT does not
   * exist on x86 but has the same number 7 as SIGBUS that does.  List
   * them last to avoid confusion:
   */
  { EPI_SIGEMT,         "SIGEMT" },
  { EPI_SIGSYS,         "SIGSYS" },
  { EPI_SIGINFO,        "SIGINFO" },
  { 0,                  CHARPN }
};


static PID_T
TXgetpid(int force)
{
  (void)force;
  return(getpid());
}

static char *
TXsignalname(int sigval)
/* Returns string name of given signal, or "Unknown signal" if unknown.
 */
{
  const SIGNAME	*sig;

  for (sig = Sigs; sig->name != CHARPN; sig++)
    if (sig->val == sigval) return(sig->name);
  return("Unknown signal");
}

static void tx_catchgenericsig ARGS((int sig));

static SIGTYPE CDECL
tx_genericsighandler TX_SIG_HANDLER_ARGS
/* Handles SIGTERM, SIGINT, etc. for programs that don't want to.
 */
{
  int           in, xitCode = TXEXIT_ABEND;
  int           msgNumAddend = TX_PUTMSG_NUM_IN_SIGNAL;
  char          *d;
  const char    *from, *by;
  char          pidUidBuf[2048];
  char          buf[1024];

#ifndef _WIN32
  (void)context;
#endif
#if defined(_WIN32) && defined(_MSC_VER)
__try
{
#endif /* _WIN32 && _MSC_VER */
  /* Make sure we don't recurse, especially into TXcallabendcbs().
   * A second signal could be much later because we are hanging,
   * or immediately because we got group-SIGINT and parent-SIGTERM together.
   * We should exit on the former, and ignore the latter.  However,
   * we cannot reliably know the difference, so return (ignore) instead
   * of exit on the second signal to allow TXcallabendcbs() to finish.
   * This means we ignore later soft kills if we're hanging, but a
   * hard kill can always be used.  However, for bad signals (SIGSEGV),
   * always exit immediately.  Most duplicate signals should be avoided
   * by sigaction() mask anyway:
   */
  in = TXinsignal();
  TXentersignal();
  if (in)                       /* previous signal is being handled */
    {
      switch (sigNum)           /* note parallel in tx_catchgenericsig() */
        {
#ifdef SIGSEGV
        case SIGSEGV:
#endif /* SIGSEGV */
#ifdef SIGBUS
        case SIGBUS:
#endif /* SIGBUS */
#ifdef SIGFPE
        case SIGFPE:
#endif /* SIGFPE */
#ifdef SIGILL
        case SIGILL:
#endif /* SIGILL */
#ifdef SIGABRT
        case SIGABRT:
#endif /* SIGABRT */
          _exit(TXEXIT_ABEND);  /* _exit() avoids stdio/malloc issues */
          break;
        }
      goto done;
    }

  tx_catchgenericsig(sigNum);                   /* might SEGV again */

#if defined(EPI_HAVE_SA_SIGACTION) && defined(SA_SIGINFO)
  if (sigInfo && sigInfo->si_code <= 0)
    {
      char      *d = pidUidBuf, *e = pidUidBuf + sizeof(pidUidBuf);

      d += snprintf(d, e - d, " UID %d PID %d", (int)sigInfo->si_uid,
                    (int)sigInfo->si_pid);
      if (d >= e)
      {
        char *TruncationPoint = pidUidBuf + sizeof(pidUidBuf) - 4;
        strcpy(TruncationPoint, "...");
      }
      from = " from";
      by = " by";
    }
  else
#endif /* EPI_HAVE_SA_SIGACTION && SA_SIGINFO */
    {
      *pidUidBuf = '\0';
      from = by = pidUidBuf;
    }

  switch (sigNum)
    {
#ifdef SIGTERM
    case SIGTERM:
      putmsg(MERR + msgNumAddend, CHARPN,
             "%s (%u) terminated (signal %d)%s%s; exiting",
             TxSigProgName, (unsigned)TXgetpid(1), (int)sigNum,
             by, pidUidBuf);
      xitCode = TXEXIT_TERMINATED;
      break;
#endif /* SIGTERM */
#if defined(SIGQUIT) || defined(SIGINT)
#  ifdef SIGQUIT
    case SIGQUIT:
#  endif /* SIGQUIT */
#  ifdef SIGINT
    case SIGINT:
#  endif /* SIGINT */
      /* Note that in Windows, we now catch Ctrl-C etc. events directly
       * (TXgenericConsoleCtrlHandler()), so unlikely to get SIGINT:
       */
      putmsg(MERR + msgNumAddend, CHARPN,
             "%s (%u) caught user interrupt (signal %d)%s%s; exiting",
             TxSigProgName, (unsigned)TXgetpid(1), (int)sigNum,
             from, pidUidBuf);
      xitCode = TXEXIT_USERINTERRUPT;
      break;
#endif  /* SIGQUIT || SIGINT */
    default:
      xitCode = TXEXIT_ABEND;
      d = buf;
      *d = '\0';
      putmsg(MERR + msgNumAddend, CHARPN,
#ifdef _WIN32
             "%s (%u) ABEND: exception 0x%X (%s)%s%s; exiting%s",
#else /* !_WIN32 */
             "%s (%u) ABEND: signal %d (%s)%s%s; exiting%s",
#endif /* !_WIN32 */
             TxSigProgName, (unsigned)TXgetpid(1), (int)sigNum,
             TXsignalname(sigNum), from, pidUidBuf, buf);
      break;
    }
  _exit(xitCode);               /* TXEXIT_... code */
done:
  TXexitsignal();
#if defined(_WIN32) && defined(_MSC_VER)
}
__except(EXCEPTION_EXECUTE_HANDLER)
{
   _exit(TXEXIT_ABEND);         /* _exit avoids stdio/malloc problems */
}
#endif /* _WIN32 && _MSC_VER */
  SIGRETURN;
}

static void
tx_catchgenericsig(sig)
int     sig;
{
#ifdef EPI_HAVE_SIGACTION
  struct sigaction      act, prev;

  memset(&act, 0, sizeof(struct sigaction));
  TX_SIG_HANDLER_SET(&act, tx_genericsighandler);
  /* We want to block all signals during the signal handler, to avoid
   * recursing into TXcallabendcbs().  However, allow bad signals so
   * we can exit (note parallel check in tx_genericsighandler()):
   */
  sigfillset(&act.sa_mask);
#  ifdef SIGSEGV
  sigdelset(&act.sa_mask, SIGSEGV);
#  endif /* SIGSEGV */
#  ifdef SIGBUS
  sigdelset(&act.sa_mask, SIGBUS);
#  endif /* SIGBUS */
#  ifdef SIGFPE
  sigdelset(&act.sa_mask, SIGFPE);
#  endif /* SIGFPE */
#  ifdef SIGILL
  sigdelset(&act.sa_mask, SIGILL);
#  endif /* SIGILL */
#  ifdef SIGABRT
  sigdelset(&act.sa_mask, SIGABRT);
#  endif /* SIGABRT */
  sigaction(sig, &act, &prev);
#  ifdef SIGHUP
  if (sig == SIGHUP && (void *)TX_SIG_HANDLER_GET(&prev) != (void *)SIG_DFL)
    sigaction(sig, &prev, &act);        /* e.g. nohup used */
#  endif /* SIGHUP */
#else /* !EPI_HAVE_SIGACTION */
#  ifdef SIGHUP
  SIGTYPE       (CDECL *prev) SIGARGS;

  prev =
#  endif /* SIGHUP */
  signal(sig, tx_genericsighandler);
#  ifdef SIGHUP
  if (sig == SIGHUP && prev != SIG_DFL)
    signal(SIGHUP, prev);               /* e.g. nohup used */
#  endif /* SIGHUP */
#endif /* !EPI_HAVE_SIGACTION */
}

#ifdef _WIN32
BOOL WINAPI
TXgenericConsoleCtrlHandler(DWORD type)
/* Console control handler, sort of like a signal handler, for Windows.
 * Handles Ctrl-C, Ctrl-Break, Close, Logoff, Shutdown events.
 * Note that Windows creates a separate thread for this function when called.
 */
{
  int           in;
  TXEXIT        exitCode = TXEXIT_TERMINATED;

#  if defined(_MSC_VER)
  __try
    {
#  endif /* _MSC_VER */
      /* Make sure we don't recurse, especially into TXcallabendcbs(): */
      in = TXinsignal();
      TXentersignal();
      /* If we're already in a previous signal handler, ignore this message;
       * since all possible types are less severe than SIGSEGV-type issues,
       * we do not exit but let previous signal handler finish:
       */
      if (in) goto done;

      switch (type)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
          putmsg(MERR, CHARPN,
                 "%s (%u) caught user interrupt (%s); exiting",
                 TxSigProgName, (unsigned)TXgetpid(1),
                (type == CTRL_C_EVENT ? "Ctrl-C event" : "Ctrl-Break event"));
          exitCode = TXEXIT_USERINTERRUPT;
          break;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        default:
          putmsg(MERR, CHARPN, "%s (%u) terminated (%s); exiting",
                 TxSigProgName, (unsigned)TXgetpid(1),
                 (type == CTRL_CLOSE_EVENT ? "Window close event" :
                  (type == CTRL_LOGOFF_EVENT ? "Log off event" :
                   "Shutdown event")));
          exitCode = TXEXIT_TERMINATED;
          break;
        }
      _exit(exitCode);                          /* TXEXIT_... code */
    done:
      TXexitsignal();
#  if defined(_MSC_VER)
    }
  __except(EXCEPTION_EXECUTE_HANDLER)
    {
      _exit(TXEXIT_ABEND);                      /* _exit() not exit() */
    }
#  endif /* _MSC_VER */
  return(TRUE);                                 /* we handled this event */
}
#endif /* _WIN32 */

static void
tx_setgenericsigs(const char *prog)
/* Sets generic signal handlers.  Used by kdbfchk, addtable, etc.
 */
{
  char                  *progDup, *old;

  if (prog != CHARPN && (progDup = strdup(prog)) != CHARPN)
    {
      old = TxSigProgName;
      TxSigProgName = progDup;
      if (old != CHARPN && old != TxSigProgNameDef) free(old);
    }
#ifdef SIGTERM
  tx_catchgenericsig(SIGTERM);
#endif /* SIGTERM */
#ifdef _WIN32
  /* Use TXgenericConsoleCtrlHandler() instead of signal() (which sets its
   * own control handler) for SIGINT, so we also get shutdown etc. events:
   */
  SetConsoleCtrlHandler(TXgenericConsoleCtrlHandler, TRUE);
#else /* !_WIN32 */
#  ifdef SIGINT
  tx_catchgenericsig(SIGINT);
#  endif /* SIGINT */
#  ifdef SIGQUIT
  tx_catchgenericsig(SIGQUIT);
#  endif /* SIGQUIT */
#endif /* !_WIN32 */
#ifdef SIGSEGV
  tx_catchgenericsig(SIGSEGV);
#endif /* SIGSEGV */
#ifdef SIGBUS
  tx_catchgenericsig(SIGBUS);
#endif /* SIGBUS */
#ifdef SIGFPE
  tx_catchgenericsig(SIGFPE);
#endif /* SIGFPE */
#ifdef SIGILL
  tx_catchgenericsig(SIGILL);
#endif /* SIGILL */
#ifdef SIGABRT
  tx_catchgenericsig(SIGABRT);
#endif /* SIGABRT */
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif /* SIGPIPE */
#ifdef SIGXFSZ
  signal(SIGXFSZ, SIG_IGN);
#endif /* SIGXFSZ */
#ifdef SIGXCPU
  signal(SIGXCPU, SIG_IGN);
#endif /* SIGXCPU */
#ifdef SIGHUP
  tx_catchgenericsig(SIGHUP);
#endif /* SIGHUP */
}

#  ifdef _WIN32
/* wtf we do not link in texis lib, so need to provide TXstrerror() */
char *
TXstrerror(err)
int	err;
/* WTF thread-unsafe
 * Note: see also rex.c standalone implementation
 */
{
	static char	MsgBuffer[4][256];
	static volatile TXATOMINT	bufn = 0;
        int             curBufIdx;
	char		*s, *buf, *end;

        curBufIdx = TX_ATOMIC_INC(&bufn);
	buf = MsgBuffer[curBufIdx & 3];
	end = buf + sizeof(MsgBuffer[0]);
	*buf = '\0';
	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, (DWORD)err,
                           0, buf, (DWORD)(end - buf), NULL) ||
	    *buf == '\0')
	{				/* FormatMessage() fails w/err 317 */
                sprintf(buf, "ERROR_%d", err);
	}
	/* Trim trailing CRLF and period: */
	for (s = buf + strlen(buf) - 1; s >= buf; s--)
		if (*s == '\r' || *s == '\n') *s = '\0';
		else break;
	if (s > buf && *s == '.' && s[-1] >= 'a' && s[-1] <= 'z') *s = '\0';
	return buf;
}
#  endif /* _WIN32 */

char *nullptr=(char *)NULL;
void
instructem()
{
static char *msg[]={
"\n",
"             REGULAR EXPRESSION PATTERN MATCHER V2.1 DESCRIPTION\n",
"\n",
"       REX locates and prints lines containing occurrences of regular\n",
"       expressions.  If files are not specified, standard input is used.\n",
"       If files are specified, the filename will be printed before the\n",
"       line containing the expression if the \"-n\" option is not used.\n",
"\n",
"SYNTAX\n",
"\n",
"       rex [options] expression [files]\n",
"\n",
"OPTIONS\n",
"       -c       Do not print control characters; replace with space.\n",
"       -C       Count the number of times the expression occurs.\n",
"       -l       List file names that contain the expression.\n",
"       -E\"EX\"   Specify and print the ending delimiting expression.\n",
"       -e\"EX\"   Specify the ending delimiting expression.\n",
"       -S\"EX\"   Specify and print the starting delimiting expression.\n",
"       -s\"EX\"   Specify the starting delimiting expression.\n",
"       -p       Begin printing at the start of the expression.\n",
"       -P       Stop printing at the end of the expression.\n",
"       -r\"STR\"  Replace the expression with \"STR\" to standard output.\n",
"       -R\"STR\"  Replace the expression with \"STR\" to original file.\n",
"       -t\"Fn\"   Use \"Fn\" as the temporary file (default: \"rextmp\").\n",
"       -f\"Fn\"   Read the expression(s) from the file \"Fn\".\n",
"       -n       Do not print the file name.\n",
"       -O       Generate \"FNAME@OFFSET,LEN\" entries for mm3 subfile list.\n",
"       -x       Translate the expression into pseudo-code (debug).\n",
"       -v       Print lines (or delimiters) not containing the expression.\n",
"\n",
"    o  Each option must be placed individually on the command line.\n",
"\n",
"    o  \"EX\"  is a REX expression.\n",
"\n",
"    o  \"Fn\"  is a file name.\n",
"\n",
"    o  \"STR\" is a replacement string.\n",
"\n",
"EXPRESSIONS\n",
"\n",
"    o  Expressions are composed of characters and operators.  Operators\n",
"       are characters with special meaning to REX.  The following\n",
"       characters have special meaning: \"\\=?+*{},[]^$.-!\" and must\n",
"       be escaped with a \'\\\' if they are meant to be taken literally.\n",
"       The string \">>\" is also special and if it is to be matched,\n",
"       it should be written \"\\>>\".  Not all of these characters are\n",
"       special all the time; if an entire string is to be escaped so it\n",
"       will be interpreted literally, only the characters \"\\=?+*{[^$.!>\"\n",
"       need be escaped.\n",
"\n",
"    o  A \'\\\' followed by an \'R\' or an \'I\' means to begin respecting\n",
"       or ignoring alphabetic case distinction, until the end of the\n",
"       sub-expression.  (Ignoring case is the default, and will re-apply\n",
"       at the next sub-expression.)  These switches DO NOT apply to\n",
"       characters inside range brackets.\n",
"\n",
"    o  A \'\\\' followed by an \'L\' indicates that the characters following\n",
"       are to be taken literally up to the next \'\\L\'.  The purpose of\n",
"       this operation is to remove the special meanings from characters.\n",
"\n",
"    o  A sub-expression following '\\F' (followed by) or '\\P' (preceded by)\n",
"       can be used to root the rest of an expression to which it is tied.\n",
"       It means to look for the rest of the expression \"as long as followed\n",
"       by ...\" or \" as long as preceded by ...\" the sub-expression\n",
"       following the \\F or \\P, but the designated sub-expression will be\n",
"       considered excluded from the located expression itself.\n",
"\n",
"    o  A \'\\\' followed by one of the following \'C\' language character\n",
"       classes matches any character in that class: alpha, upper, lower,\n",
"       digit, xdigit, alnum, space, punct, print, graph, cntrl, ascii.\n",
"       Note that the definition of these classes may be affected by\n"
"       the current locale.\n",
"\n",
"    o  A \'\\\' followed by one of the following special characters\n",
"       will assume the following meaning: n=newline, t=tab,\n",
"       v=vertical tab, b=backspace, r=carriage return,\n",
"       f=form feed, 0= the null character.\n",
"\n",
"    o  A \'\\\' followed by  Xn or Xnn where n is a hexadecimal digit\n",
"       will match that character.\n",
"\n",
"    o  A \'\\\' followed by any single character (not one of the above\n",
"       special escape characters/tokens) matches that character.  Escaping\n",
"       a character that is not a special escape is not recommended, as the\n",
"       expression could change meaning if the character becomes an escape\n",
"       in a future release.\n",
"\n",
"    o  The character \'^\' placed anywhere in an expression (except after a\n",
"       \'[\') matches the beginning of a line (same as \\x0A in Unix or\n",
"       \\x0D\\x0A in Windows).\n",
"\n",
"    o  The character \'$\' placed anywhere in an expression\n",
"       matches the end of a line (\\x0A in Unix, \\x0D\\x0A in Windows).\n",
"\n",
"    o  The character \'.\' matches any character.\n",
"\n",
"    o  A single character not having special meaning matches that\n",
"       character.\n",
"\n",
"    o  A string enclosed in brackets ('[]') is a set, and matches any\n",
"       single character from the string.  Ranges of ASCII character codes\n",
"       may be abbreviated with a dash, as in '[a-z]' or '[0-9]'.\n",
"       A \'^\' occurring as the first character of the set will invert\n",
"       the meaning of the set, i.e. any character NOT in the set will\n",
"       match instead.  A literal \'-\' must be preceded by a \'\\\'.\n",
"       The case of alphabetic characters is always respected within brackets.\n",
#ifdef EPI_REX_SET_SUBTRACT
"\n",
"       A double-dash ('--') may be used inside a bracketed set to subtract\n",
"       characters from the set; e.g. '[\\alpha--x]' for all alphabetic\n",
"       characters except 'x'.  The left-hand side of a set subtraction\n",
"       must be a range, character class, or another set subtraction.\n",
"       The right-hand side of a set subtraction must be a range, character\n",
"       class, or a single character.  Set subtraction groups left-to-right.\n",
"       The range operator '-' has precedence over set subtraction.\n",
#endif /* EPI_REX_SET_SUBTRACT */
"\n",
"    o  The \'>>\' operator in the first position of a fixed expression\n",
"       will force REX to use that expression as the \"root\" expression\n",
"       off which the other fixed expressions are matched.  This operator\n",
"       overrides one of the optimizers in REX.  This operator can\n",
"       be quite handy if you are trying to match an expression\n",
"       with a \'!\' operator or if you are matching an item that\n",
"       is surrounded by other items.  For example: \"x+>>y+z+\"\n",
"       would force REX to find the \"y\'s\' first then go backwards\n",
"       and forwards for the leading \"x\'s\" and trailing \"z\'s\".\n",
"\n",
"    o  The \'!\' character in the first position of an expression means\n",
"       that it is NOT to match the following fixed expression.\n",
"       For example: \"start=!finish+\" would match the word \"start\"\n",
"       and anything past it up to (but not including the word \"finish\".\n",
"       Usually operations involving the NOT operator involve knowing\n",
"       what direction the pattern is being matched in.  In these cases\n",
"       the \'>>\' operator comes in handy.  If the \'>>\' operator is used,\n",
"       it comes before the \'!\'.  For example: \">>start=!finish+finish\"\n",
"       would match anything that began with \"start\" and ended with\n",
"       \"finish\".  THE NOT OPERATOR CANNOT BE USED BY ITSELF in an\n",
"       expression, or as the root expression in a compound expression.\n",
"\n",
"       Note that '!' expressions match a character at a time, so their\n",
"       repetition operators count characters, not expression-lengths\n",
"       as with normal expressions.  E.g. '!finish{2,4}' matches 2 to 4\n",
"       characters, whereas 'finish{2,4}' matches 2 to 4 times the length\n",
"       of 'finish'.\n",
"\n",
"REPETITION OPERATORS\n",
"\n",
"    o  A regular expression may be followed by a repetition operator in\n",
"       order to indicate the number of times it may be repeated.\n",
"\n",
"       NOTE: Under Windows the operation \"{X,Y}\" has the syntax \"{X-Y}\"\n",
"       because Windows will not accept the comma on a command line.  Also, N\n",
"       occurrences of an expression implies infinite repetitions but in\n",
"       this program N represents the quantity 32768 which should be a more\n",
"       than adequate substitute in real-world text.\n",
"\n",
"    o  An expression followed by the operator \"{X,Y}\" indicates that\n",
"       from X to Y occurrences of the expression are to be located.  This\n",
"       notation may take on several forms: \"{X}\" means X occurrences of\n",
"       the expression, \"{X,}\" means X or more occurrences of the\n",
"       expression, and \"{,Y}\" means from 0 (no occurrences) to Y\n",
"       occurrences of the expression.\n",
"\n",
"    o  The \'?\' operator is a synonym for the operation \"{0,1}\".\n",
"       Read as: \"Zero or one occurrence.\"\n",
"\n",
"    o  The \'*\' operator is a synonym for the operation \"{0,}\".\n",
"       Read as: \"Zero or more occurrences.\"\n",
"\n",
"    o  The \'+\' operator is a synonym for the operation \"{1,}\".\n",
"       Read as: \"One or more occurrences.\"\n",
"\n",
"    o  The \'=\' operator is a synonym for the operation \"{1}\".\n",
"       Read as: \"One occurrence.\"\n",
#ifdef EPI_ENABLE_RE2
"\n",
"RE2 SYNTAX\n",
"\n",
"    o  The entire search expression may be interpreted as an RE2\n",
"       regular expression instead of REX, by prefixing it with \"\\<re2\\>\".\n",
#  ifdef EPI_BUILD_RE2
"       RE2 syntax is supported on this platform, but not on some others.\n",
#  else /* !EPI_BUILD_RE2 */
"       RE2 syntax is not supported on this platform, but is on some others.\n",
#  endif /* !EPI_BUILD_RE2 */
"\n",
"    o  To use REX syntax, prefix the expression with \"\\<rex\\>\".\n",
"       This is only needed if the default syntax has been changed to RE2\n",
"       (e.g. via an API parameter).\n",
#endif /* EPI_ENABLE_RE2 */
"\n",
"REPLACEMENT STRINGS\n",
"\n",
"       NOTE: Any expression may be searched for in conjunction with\n",
"       a replace operation but the replacement string will always\n",
"       be of fixed size.\n",
"\n",
"    o  Replacement strings may just be a literal string or they may\n",
"       include the \"ditto\" character \"?\".  The ditto character will\n",
"       copy the character in the position specified in the\n",
"       replace-string from the same position in the located expression.\n",
"\n",
"    o  A decimal digit placed within curly-braces (e.g.: {5}) will place\n",
"       that character of the located expression to the output.\n",
"\n",
"    o  A \"\\\" followed by a number will place that subexpression to the\n",
"       output.  Subexpressions are numbered starting at 1.\n",
#ifdef EPI_ENABLE_RE2
"\n",
"    o  \"\\&\" will place the entire expression match to the output\n",
"       (without \\P nor \\F subexpressions, if any).\n",
#endif /* EPI_ENABLE_RE2 */
"\n",
"    o  A plus-character \"+\" will place an incrementing decimal number to\n",
"       the output.  One purpose of this operator is to number lines.\n",
"\n",
#ifdef NEVER
"    o  A \"#\" in the replace-string will cause the character in that\n",
"       position to be printed in hexadecimal form.\n",
"\n",
#else
"    o  A \"#\" followed by a number will cause the numbered subexpression\n",
"       to be printed in hexadecimal form.\n",
"\n",
#endif
"    o  To replace with the literal characters \"?#{}+\\\" precede them with\n",
"       the escapement character \"\\\".\n",
"\n",
"    o  Any character in the replace-string may be represented by the\n",
"       hexadecimal value of that character using the following syntax:\n",
"       \\Xdd where \"dd\" is the hexadecimal value.\n",
"\n",
"CAVEATS AND COMMENTARY\n",
"\n",
"REX is a highly optimized pattern recognition tool that has been modeled\n",
"after the Unix family of tools: GREP, EGREP, FGREP, and LEX.  Wherever\n",
"possible REX\'s syntax has been held consistent with these tools, but\n",
"there are several major departures that may bite those who are used to\n",
"using the GREP family.\n",
"\n",
"REX uses a combination of techniques that allow it to surpass the speed of\n",
"anything similar to it by a very wide margin.\n",
"\n",
"The technique that provides the largest advantage is called\n",
"\"state-anticipation or state-skipping\" which works as follows:\n",
"\n",
"if we were looking for the pattern:\n",
"\n",
"                       ABCDE\n",
"in the text:\n",
"\n",
"                       AAAAABCDEAAAAAAA\n",
"\n",
"a normal pattern matcher would do the following:\n",
"\n",
"                       ABCDE\n",
"                        ABCDE\n",
"                         ABCDE\n",
"                          ABCDE\n",
"                           ABCDE\n",
"                       AAAAABCDEAAAAAAA\n",
"\n",
"The state-anticipation scheme would do the following:\n",
"\n",
"                       ABCDE\n",
"                           ABCDE\n",
"                       AAAAABCDEAAAAAAA\n",
"\n",
"The normal algorithm moves one character at time through the text,\n",
"comparing the leading character of the pattern to the current text\n",
"character of text, and if they match, it compares the leading pattern\n",
"character +1 to the current text character +1 , and so on...\n",
"\n",
"The state anticipation pattern matcher is aware of the length of the\n",
"pattern to be matched, and compares the last character of the pattern to\n",
"the corresponding text character.  If the two are not equal, it moves\n",
"over by an amount that would allow it to match the next potential hit.\n",
"\n",
"If one were to count the number of comparison cycles for each pattern\n",
"matching scheme using the example above, the normal pattern matcher would\n",
"have to perform 13 compare operations before locating the first occurrence\n",
"vs. 6 compare operations for the state-anticipation pattern matcher.\n",
"\n",
"One concept to grasp here is that: \"The longer the pattern to be found,\n",
"the faster the state-anticipation pattern matcher will be.\"  While a\n",
"normal pattern matcher will slow down as the pattern gets longer.\n",
"\n",
"Herein lies the first major syntax departure: REX always applies\n",
"repetition operators to the longest preceding expression.  It does\n",
"this so that it can maximize the benefits of using the state-skipping\n",
"pattern matcher.\n",
"\n",
"If you were to give GREP the expression : ab*de+\n",
"It would interpret it as:\n",
" an \"a\" then 0 or more \"b\"\'s then a \"d\" then 1 or more \"e\"\'s.\n",
"\n",
"REX will interpret this as:\n",
" 0 or more occurrences of \"ab\" followed by 1 or more occurrences of \"de\".\n",
"\n",
"\n",
"The second technique that provides REX with a speed advantage is ability\n",
"to locate patterns both forwards and backwards indiscriminately.\n",
"\n",
"Given the expression: \"abc*def\", the pattern matcher is looking for\n",
"\"Zero to N occurrences of \'abc\' followed by a \'def\'\".\n",
"\n",
"The following text examples would be matched by this expression:\n",
"\n",
"     abcabcabcabcdef\n",
"     def\n",
"     abcdef\n",
"\n",
"But consider these patterns if they were embedded within a body of text:\n",
"\n",
"     My country \'tis of abcabcabcabcdef sweet land of def, abcdef.\n",
"\n",
"A normal pattern matching scheme would begin looking for \'abc*\' .  Since\n",
"\'abc*\' is matched by every position within the text, the normal pattern\n",
"matcher would plod along checking for \'abc*\' and then whether it\'s there\n",
"or not it would try to match \"def\".  REX examines the expression\n",
"in search of the the most efficient fixed length sub-pattern and uses it\n",
"as the root of search rather than the first sub-expression.  So, in the\n",
"example above, REX would not begin searching for \"abc*\" until it has located\n",
"a \"def\".\n",
"\n",
"There are many other techniques used in REX to improve the rate at which\n",
"it searches for patterns, but these should have no effect on the way in\n",
"which you specify an expression.\n",
"\n",
"The three rules that will cause the most problems to experienced GREP users\n",
"are:\n",
"\n",
"1: Repetition operators are always applied to the longest expression.\n",
"2: There must be at least one sub-expression that has one or more repetitions.\n",
"3: No matched sub-expression will be located as part of another.\n",
"\n",
"Rule 1 example:\n",
"\n",
"abc=def*  Means one \"abc\" followed by 0 or more \"def\"\'s .\n",
"\n",
"Rule 2 example:\n",
"\n",
"abc*def*  CAN NOT be located because it matches every position within the text.\n",
"\n",
"Rule 3 example:\n",
"\n",
"a+ab  Is idiosyncratic because \"a+\" is a subpart of \"ab\".\n",
"\n",
"          ===========================================================\n",
"          |               Copyright (C) 1988,1989,1990              |\n",
"          |          Expansion Programs International, Inc.         |\n",
"          |              Written by  P. Barton Richards             |\n",
"          ===========================================================\n",
""
};
 char **p;
#ifdef MVS
 char *s;
 for(p=msg;**p;p++)
    for(s= *p;*s!='\0';s++)
         switch(*s)
              {
               case '\\' : *s=EESC;break;
               case '['  : *s=OSET;break;
               case ']'  : *s=CSET;break;
               case '{'  : *s=EBEG;break;
               case '}'  : *s=EEND;break;
               case '^'  : *s=ENOT;break;
              }
#endif
 for(p=msg;**p;p++)
     fputs(*p,stdout);
}
#endif

/************************************************************************/

int
strn1cmp(a,b)
register byte *a,*b;
{
  register size_t len=strlen((char *)a);
 if(strncmp((char *)a,(char *)b,len)==0)
   return((int)len);
 return(0);
}

/************************************************************************/
        /* MAW 07-02-91 - check for long seq. first to avoid aliasing */
int
dobslash(s,a)               /* leaves the pointer s past the escapement */
byte **s;       /* (in/out) `*s' points to escape (e.g. `\alpha') */
byte *a;        /* (out) corresponding setlist[i] */
/* Parses char-set escape sequence (`\alpha', `\x0D', '\r' etc.; not `\L',
 * `\P', `\F', `\I' nor `\R').  Increments `*s' past escape.
 * Sets corresponding setlist true-values in `a'.
 * Returns: -2 on error (`*s' still advanced; no msg)
 *          -1 for char classes (e.g. `\alpha')
 *          hex value for single char: hex escape or `\n' etc.
 *          char value for unrecognized escape (e.g. `\a' taken as `a')
 * KNG 20070121 was returning classes and single-char `\x00' both as 0
 *              and single-char `\n' etc. as class (0)
 */
{
 register byte *p= *s;
 register int i,len;

 ++p;                                           /* point past the slash */
 if((len=strn1cmp((byte *)"alpha",p)))
     {for(i=0;i<DYNABYTE;i++) if(isalpha(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"upper",p)))
     {for(i=0;i<DYNABYTE;i++) if(isupper(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"lower",p)))
     {for(i=0;i<DYNABYTE;i++) if(islower(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"digit",p)))
     {for(i=0;i<DYNABYTE;i++) if(isdigit(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"xdigit",p)))
     {for(i=0;i<DYNABYTE;i++) if(isxdigit(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"alnum",p)))
     {for(i=0;i<DYNABYTE;i++) if(isalnum(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"space",p)))
     {for(i=0;i<DYNABYTE;i++) if(isspace(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"punct",p)))
     {for(i=0;i<DYNABYTE;i++) if(ispunct(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"print",p)))
     {for(i=0;i<DYNABYTE;i++) if(isprint(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"graph",p)))
     {for(i=0;i<DYNABYTE;i++) if(isgraph(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"cntrl",p)))
     {for(i=0;i<DYNABYTE;i++) if(iscntrl(i)) *(a+i)=1;}
 else if((len=strn1cmp((byte *)"ascii",p)))
     {for(i=0;i<DYNABYTE;i++) if(isascii(i)) *(a+i)=1;}
 else if(tolower(*p)=='x')                               /* hex check */
    {
     static const byte hex[]="0123456789abcdef";
     int hi,lo;
     byte hic,loc;
     int val;
     ++p;                                           /* point past the x */
     if(*p=='\0')                                    /* unexpected null */
       {
         *s = p;
         return(-2);                            /* error */
       }
     hic=(char)tolower(*p);                /* point past the first byte */
     loc=(char)tolower(*(p+1));       /* point past the second hex byte */
     for(hi=0;hex[hi] && hic!=hex[hi];hi++);              /* cvt to hex */
     for(lo=0;hex[lo] && loc!=hex[lo];lo++);
     if(hi<16)
         {
          if(lo<16)                                       /* 2 byte hex */
             {
              *s=p+2;
              val= ((hi<<4)|lo)&0xff;
              *(a+val)=1;
              return(val);
             }
          else                                            /* 1 byte hex */
             {
              *s=p+1;
              *(a+hi)=1;
              return(hi);
             }
         }
     *s = p + 1;                                /* include bad byte */
     return(-2);                                /* error: not hex at all */
    }
 else if(*p=='n') { (a['\n'])=1; *s = p + 1; return('\n'); }
 else if(*p=='t') { (a['\t'])=1; *s = p + 1; return('\t'); }
 else if(*p=='v') { (a['\v'])=1; *s = p + 1; return('\v'); }
 else if(*p=='b') { (a['\b'])=1; *s = p + 1; return('\b'); }
 else if(*p=='r') { (a['\r'])=1; *s = p + 1; return('\r'); }
 else if(*p=='f') { (a['\f'])=1; *s = p + 1; return('\f'); }
 else if(*p=='0') { (a['\0'])=1; *s = p + 1; return('\0'); }
 /* `\<' only valid (as part of token escape) at expression start (for now);
  * was parsed by openrex():
  */
 else if (Re2Enabled && *p == OPEN_TOKEN)
   {
     *s = p + 1;
     return(-2);
   }
 /* `\>' only valid (as part of token escape) at expression start (for now);
  * was parsed by openrex().  But do not confuse mid-stream `\>' with `\>>';
  * latter is legal.  See also similar code in openrex():
  * Bug 6301: allow `\>' mid-stream; could be legacy `>>' escape
  * attempt with single <sandr> that escapes `>' not `>>'.  Treat as
  * literal `>', but do not yap as with unneeded escapes, due to
  * possible legacy escape:
  */
 else if (Re2Enabled && *p == CLOSE_TOKEN)
   goto asisSilent;
 else                                       /* it's not a special escape */
    {
      /* Bug 6301: warn about unneeded escapes -- to reserve namespace
       * for future escape syntax expansion -- but allow them, per
       * legacy behavior.  Some escapes are needed in some contexts
       * but not others; e.g. literal `-' must be escaped inside
       * `[...]' character ranges but not outside, while literal `.'
       * must be escaped outside character ranges but not inside.  To
       * avoid single-<sandr>-escapement issues we allow both silently:
       */
      switch (*p)
        {
        case EESC:      break;                          /* `\\' */
        case '=' :      break;
        case '?' :      break;
        case '+' :      break;
        case '*' :      break;
        case EBEG :     break;                          /* `{' */
        case EEND :     break;                          /* `}' */
        case ',':       break;
        case OSET:      break;                          /* `[' */
        case CSET:      break;                          /* `]' */
        case '-':       break;
        case ENOT:      break;                          /* `^' */
        case '$':       break;
        case '.':       break;
        case '!':       break;
        default:
          /* Note that scripts may look for this message text: */
          if (TXunneededRexEscapeWarning)
            putmsg(MWARN + UGE, __FUNCTION__,
              "REX: Unneeded escape sequence `%.2s': treated as plain%s `%c'",
                   *s, (TX_ISALPHA(*p) ? " respect-case" : ""), (int)*p);
        }
    asisSilent:
     *(a+*p)=1;                                     /* set that byte up */
     *s=p+1;
     return(*p);
    }
 *s=p+len;          /* it was special, so move the pointer past the str */
 return(-1);                                    /* ok: class */
}

/************************************************************************/
int
dorange(s,a)
byte **s;       /* (in/out) `*s' points to set range ("[...]") */
byte *a;        /* (out) corresponding setlist[i] for expression */
/* Parses set range ("[...]") at `*s'.  Increments `*s' past the set.
 * Sets corresponding setlist true-values in `a'.
 * Returns -1 on error (`*s' not advanced), 1 on success.
 */
{
 byte *p= *s;
 int invert=0, c;
 /* `prevch' is previous standalone char, or -1 if previous thing was not a
  * single char (e.g. a class like `\alpha', or nothing) or was RHS of range:
  */
 int    prevch = -1;
#ifdef EPI_REX_SET_SUBTRACT
 /* Given:
  *   A is a single char e.g. `a' or `\x00'
  *   C is a class escape e.g. `\alnum'
  *   R is a range:          R = A `-' A
  *   S is a set:            S = {R|C|D}
  *   D is set difference:   D = S `--' {R|C|A}
  * We do not support pointless A `--' ... for clarity
  * D is not on RHS of subtraction because subtraction groups to the left:
  *   S1 `--' S2 `--' S3 == (S1 `--' S2) `--' S3
  * Range operator has precedence over subtraction, e.g.:
  *   C `--' A `-' A == C `--' (A `-' A)
  */
 /* `sublist' is temp setlist for subtraction: right-side of subtract: */
 byte   sublist[DYNABYTE];
 /* `isprevaset' is nonzero if previous item is a set (definition S above): */
 int    isprevaset = 0;
#endif /* EPI_REX_SET_SUBTRACT */

 ++p;                                           /* pass the OSET `[' */
 if(*p==ENOT)                                   /* check for `^' */
    {
     ++p;
     invert=1;
    }
 for(;*p;)
    {
     switch(*p)
         {
          case EESC :                           /* '\\' */
              if((c=dobslash(&p,a))== -2)       /* invalid escape */
                   return(-1);                  /* error */
              prevch = c;
#ifdef EPI_REX_SET_SUBTRACT
              isprevaset = (c == -1);           /* class escape is a set */
#endif /* EPI_REX_SET_SUBTRACT */
              break;
          case CSET  :                          /* ']' i.e. close-set */
              {
               *s= ++p;                            /* rset s past the ] */
               if(invert)         /* i reuse p for a different job here */
                 {
                   for(c=0,p=a;c<DYNABYTE;c++,p++)    /* [^ ] inversion */
                        if(*p) *p=(byte)0;
                        else   *p=(byte)1;
                 }
               return(1);                       /* success */
              }
          case '-'  :                           /* range or subtraction */
              {
               int to;
               ++p;
#ifdef EPI_REX_SET_SUBTRACT
               if (*p == '-')                   /* set subtraction operator */
                 {
                   if (!isprevaset) return(-1); /* LHS must be a set */
                   ++p;                         /* skip subtract operator */
                   memset(sublist, 0, DYNABYTE);/* init RHS of subtract */
                   /* The RHS of a subtract may be a range, class-escape
                    * or single character.  To resolve precedence issues,
                    * we re-code range/class op here; it's easier than
                    * trying to maintain state through the outer loop:
                    */
                   if (*p == CSET) return(-1);  /* ']' i.e. close-set: error */
                   if (*p == EESC)              /* '\\' */
                     switch (c = dobslash(&p, sublist))
                       {
                       case -2:                 /* invalid escape */
                         return(-1);
                       case -1:                 /* class */
                         /* Class definitely binds to the subtraction;
                          * apply it and we're done:
                          */
                         for (c = 0; c < DYNABYTE; c++)
                           if (sublist[c]) a[c] = 0;
                         prevch = -1;           /* sub. result is not a char*/
                         isprevaset = 1;        /* sub. result is a set */
                         continue;
                       default:                 /* single char */
                         prevch = c;
                         break;                 /* cont. w/single-char below*/
                       }
                   else if (*p == '-')          /* `-' after `--': illegal */
                     return(-1);                /* error */
                   else
                     prevch = *(p++);           /* plain char */
                   /* If we get here, `prevch' is a single char (either
                    * literal or an escape from above).  It could be LHS
                    * of a range, which has precedence over subtraction,
                    * so drill on to resolve it.  Note re-coding of range:
                    */
                   if (*p == '-' && p[1] != '-')/* range op follows */
                     {
                       if (*(++p) == CSET) return(-1);  /* ']' err */
                       /* No need to re-clear `sublist'; not using it: */
                       if (*p == EESC)          /* '\\' */
                         to = dobslash(&p, sublist);
                       else
                         to = *(p++);
                       if (to <= prevch ||      /* 2nd char before first */
                           to < 0 ||            /* 2nd char invalid or class*/
                           prevch < 0)          /* 1st char invalid or class*/
                         return(-1);            /* error */
                       for (c = prevch; c <= to; c++)
                         a[c] = (byte)0;
                     }
                   else
                     /* If another subtraction follows, parse it next loop,
                      * since subtraction binds to the left.
                      */
                     a[prevch] = (byte)0;       /* single char `prevch' */
                   prevch = -1;                 /* sub. result is not a char*/
                   isprevaset = 1;              /* sub. result is a class */
                   break;
                 }
               /* else it's a range op, process below: */
#endif /* EPI_REX_SET_SUBTRACT */
               if (*p == CSET) return(-1);      /* ']' i.e. close-set: error */
               /* KNG 20070122 no longer allow unescaped `-' on RHS of range;
                * could be confused with range op, and/or subtraction op:
                */
               if (*p == '-') return(-1);
               if(*p==EESC)                     /* '\\' */
                   to=dobslash(&p,a);
               else { to= *p; p++; }  /* PBR 10-19-90 fixes hex range bug */
               if (to <= prevch ||              /* 2nd char before first */
                   to < 0 ||                    /* 2nd char invalid or class*/
                   prevch < 0)                  /* 1st char invalid or class*/
                 return(-1);                    /* error */
               for(c=prevch;c<=to;c++)          /* set the span to true */
                   *(a+c)=(byte)1;
               prevch = -1;                     /* `to' part of this range */
#ifdef EPI_REX_SET_SUBTRACT
               isprevaset = 1;                  /* range is a set */
#endif /* EPI_REX_SET_SUBTRACT */
              }
              break;
          default   :
               prevch= *p;
               *(a+prevch)=(byte)1;             /* set table to true */
               ++p;
#ifdef EPI_REX_SET_SUBTRACT
               isprevaset = 0;                  /* single char is not a set */
#endif /* EPI_REX_SET_SUBTRACT */
               break;
         }
    }
 return(-1);                  /* the for loop should never see a null */
}


/************************************************************************/

void
eatspace(sp)                                        /* eats white space */
byte **sp;
{
 for(;**sp && isspace(**sp);(*sp)++);
}

/************************************************************************/

void
eatdigit(sp)                                        /* eats white space */
byte **sp;
{
 for(;**sp && isdigit(**sp);(*sp)++);
}

/************************************************************************/

int
reppar(sOff, s, fs)               /* repetition operation parser for f3par  */
size_t  sOff;   /* (in) `*s' offset in overall expression, for messages */
byte **s;       /* (in/out) repetition clause to parse */
FFS *fs;        /* (in/out) REX object */
/* Parses repetition clause, e.g. `=', `+', `{...}' etc.
 * Assumes `*s' points to start of clause, and advances it past the clause.
 * Returns 0 on error, 1 on success.
 */
{
 static const char      Fn[] = "reppar";
 byte                   *sOrg = *s, *e;

 switch(**s)
    {
     case '=' :  fs->from=1;fs->to=1      ;++(*s);break;    /* from 1 to 1 */
     case '?' :  fs->from=0;fs->to=1      ;++(*s);break;    /* from 0 to 1 */
     case '+' :  fs->from=1;fs->to=MAXFREP;++(*s);break;    /* from 1 to n */
     case '*' :  fs->from=0;fs->to=MAXFREP;++(*s);break;    /* from 0 to n */
     case EBEG :                                /* { */
           /* the possible perms. are : {,} {x} {x,} {,y} {x,y} */
          ++(*s);
          eatspace(s);
                   /* PBR 05-31-91 added * operator */
          if(isdigit(**s) || **s=='*')               /* {x} {x,} {x,y}  */
              {
               if(**s=='*')                             /* PBR 05-31-91 */
                   {
                    ++(*s);
                    fs->from= -1;
                    fs->to=MAXFREP;
                   }
               else
                   {
                    fs->from=fs->to=atoi((char *)*s);            /* {x} */
                    eatdigit(s);
                   }
               if(**s==',' || **s=='-')                   /* {x,} {x,y} */
                   {
                    ++(*s);
                    eatspace(s);
                    if(isdigit(**s))                           /* {x,y} */
                        {
                         fs->to=atoi((char *)*s);
                         eatdigit(s);
                        }
                    else                                        /* {x,} */
                        {
                         fs->to=MAXFREP;
                         eatspace(s);
                        }
                   }
               else eatspace(s);
              }
          else if(**s==',' || **s=='-')                    /* {,y}  {,} */
              {
               fs->from=0;
               ++(*s);
               eatspace(s);
               if(isdigit(**s))                                 /* {,y} */
                   {
                    fs->to=atoi((char *)*s);
                    eatdigit(s);
                   }
               else                                              /* {,} */
                   {
                    fs->to=MAXFREP;
                    eatspace(s);
                   }
              }
          if(**s==EEND) ++(*s);
          else
              {
                /* Try to find end of `{...}' clause for error message: */
               for (e = *s; *e && *e != EEND; e++);
               if (*e == EEND) e++;
               putmsg(MERR, Fn,
               "REX: Syntax error in repetition operator `%.*s' at offset %d",
                      (int)(e - sOrg), sOrg, (int)sOff);
               return(0);
              }
          break;
    }
 if(fs->from>fs->to || fs->to <0 /*|| fs->from<0 */)    /* PBR 05-31-91 */
    {
      putmsg(MERR + UGE,Fn,
             "REX: Invalid values in repetition operator `%.*s' at offset %d",
             (int)(*s - sOrg), sOrg, (int)sOff);
     return(0);
    }
 return(1);
}

/************************************************************************/

int
rexsavemem(yes)
int     yes;
{
  int   prev;

  prev = RexSaveMem;
  RexSaveMem = (yes ? 1 : 0);
  return(prev);
}

/************************************************************************/

static int f3par ARGS((TXrexParseState *state, size_t sOff, byte **sp,
                       FFS *fs));
static int
f3par(state, sOff, sp,fs)
TXrexParseState *state; /* (in/out) parse state */
size_t  sOff;   /* (in) `*sp' offset in overall expression, for messages */
byte **sp;                      /* advances as far as parsed (1 subexp) */
FFS *fs;
/* Parses REX expression at `*sp' for one subexpression, advancing `*sp'.
 * Returns subexpression pattern length, or -1 on error.
 */
{
 static const char Fn[]="f3par";
 byte   *ta[FFS_MAX_PAT_LEN];
 int am, i;
 byte   *s = *sp, *sOrg = *sp, *sSave, *e;

 fs->exclude=0;
 fs->root=0;
 fs->is_not=0;
 fs->exp=s;
 fs->subExprIndex = state->subExprIndex;

  /* `ta' is now a temp full-size array: we dup it at parse end
   * when we know its true size, to save mem:  KNG 040413
   */
  memset(ta, 0, FFS_MAX_PAT_LEN*sizeof(byte *));

  if (!state->continuedSubExprStart)
    {
      /* We are not continuing a too-long subexpression, so we are
       * truly at the start of a subexpression; thus symbols which are
       * significant at start of a subpexression are valid:
       */
      if (*s == '>' && s[1] == '>')             /* start here; `>>' */
        {
          fs->root = 1;
          s += 2;
        }
      if (*s == '!')
        {
          fs->is_not = 1;
          s++;
        }
      /* We should also reset some state: */
      state->respectCase = state->literal = 0;
    }

 fs->from=1;fs->to=1;fs->n=0;                      /* set repeat values */
 for(am=0,ta[am]=(byte *)calloc(DYNABYTE,sizeof(byte));
      ta[am]!=BPNULL;
      ta[++am]=(byte *)calloc(DYNABYTE,sizeof(byte))
    )
    {
     /* KNG 970910  check that we do not waltz past end of setlist: */
     if (am >= (FFS_MAX_PAT_LEN - 2))
         {
           /* Bug 6234: instead of bailing with subexpression-too-long
            * error, stop one byte early and try to split into multiple
            * subexpressions.
            * E.g. turn this:   foo(251chars)bar=
            * into this:        foo(251chars)=bar=
            * Only works if actual repetition is 1 to 1, which we do not
            * know yet.  But that is often the case with (too-)long
            * subexpressions:
            */
           if (!state->continuedSubExprStart)
             {
               state->continuedSubExprStart = (const char *)sOrg;
               state->continuedSubExprStartOffset = sOff;
             }
           goto continueSubExpr;
         }
     NOALLOC:
     if (state->literal)
         {
          if(*s=='\0') goto ENDOFPARSE;
          else if(*s==(byte)EESC && *(s+1)==(byte)'L')
              {                                 /* `\L'  end of literal */
               s+=2;
               state->literal=0;
               goto NOALLOC;
              }
          *(ta[am]+ *s)=(byte)1;
          ++s;
          continue;
         }
     switch(*s)
         {
          case '\0' :
          ENDOFPARSE:
            /* Explicit end of subexpression; no (more) continuing: */
            state->continuedSubExprStart = NULL;
            state->continuedSubExprStartOffset = 0;
            state->subExprIndex++;
          continueSubExpr:
              {
               free(ta[am]);                    /* get rid of remainder */
               ta[am]=BPNULL;
               /* KNG 040413 dup the list now that we know its size: */
               fs->setlist = (byte **)calloc(am + 1, sizeof(byte *));
               if (fs->setlist == (byte **)NULL)
                 {
                   putmsg(MERR + MAE, Fn, OutOfMem);
                   goto err;
                 }
               memcpy(fs->setlist, ta, (am + 1)*sizeof(byte *));
               *sp = fs->expEnd = s;
               return(am);            /* end of pattern return the size */
              }
          case EESC :                           /* '\\'  escape char */
              {
               switch(*(s+1))
                   {
                    case 'P' : fs->exclude= -1;s+=2; goto NOALLOC;/* PBR 04-24-91 */
                    case 'F' : fs->exclude=1 ;s+=2; goto NOALLOC;/* PBR 04-24-91 */
                    case 'R' : state->respectCase = 1; s += 2; goto NOALLOC;
                    case 'I' : state->respectCase = 0; s += 2; goto NOALLOC;
                    case 'L' : state->literal = 1;     s += 2; goto NOALLOC;
                    default  : sSave = s;
                               if(dobslash(&s,ta[am])== -2)
                                  {
                                    putmsg(MERR + UGE, Fn,
                       "REX: Invalid escape sequence `%.*s' at offset %d",
                                           (int)(s - sSave), sSave,
                                           (int)sOff + (int)(sSave - sOrg));
                                    goto err;
                                  }; break;
                   }

              }
              break;
         case OSET :                           /* '['  i.e. open-set */
              sSave = s;
              if(dorange(&s,ta[am])== -1)
                  {
                    /* Attempt to find the end of the range, for message: */
                    for (e = sSave; *e && *e != CSET; e++)
                      if (*e == EESC && e[1] == CSET) e++;
                    if (*e == CSET) e++;
                    putmsg(MERR + UGE, Fn,
                          "REX: Invalid range sequence `%.*s%s' at offset %d",
                           (int)(e - sSave < 50 ? e - sSave : 50), sSave,
                           (e - sSave > 50 ? Ellipsis :""),
                           (int)sOff + (int)(sSave - sOrg));
                    goto err;
                  }
              break;
          case '.' :
              {
               int i;                      /* dot operator is all bytes */
               for(i=0;i<DYNABYTE;i++)
                   *(ta[am]+i)=(byte)1;
               ++s;
              }
              break;
#ifdef _WIN32
          case EBOL :                           /* `^' */
          case '$'  :
              {
               ta[am+1]=(byte *)calloc(DYNABYTE,sizeof(byte));
               if(ta[am+1]==BPNULL) break;
               *(ta[am]+0x0d)=(byte)1;
               *(ta[am+1]+0x0a)=(byte)1;
               ++am;
               ++s;
              } break;
#else
          case EBOL :                           /* `^' */
          case '$' :
#             ifdef macintosh                         /* MAW 07-20-92 */
                 *(ta[am]+(int)'\r')=(byte)1;
#             else
                 *(ta[am]+(int)'\n')=(byte)1;
#             endif
              ++s;
              break;
#endif
          case '=' :                                     /* from 1 to 1 */
          case '?' :                                     /* from 0 to 1 */
          case '+' :                                     /* from 1 to n */
          case '*' :                                     /* from 0 to n */
          case EBEG :                                    /* `{' from x to y */
              {
               if(!reppar(sOff + (s - sOrg), &s,fs))
                   goto err;                             /* PBR 01-02-91 */

               /* Bug 6234: now we pay the piper for expanding too-long
                * subexpression into multiples: repetition must be 1x:
                */
               if (state->continuedSubExprStart &&
                   (fs->from != 1 || fs->to != 1))
                 {
                   putmsg(MERR + UGE, Fn,
                 "REX: Search subexpression `%.50s...' too long at offset %d",
                          state->continuedSubExprStart,
                          (int)state->continuedSubExprStartOffset);
                   goto err;
                 }

               if(fs->from>1     /* can turn this into a fixed length ? */
                  && fs->to==fs->from
                  /* KNG 040413 arbitrary limit to save mem
                   * if FFS_MAX_PAT_LEN is large:
                   */
                  && (am*fs->from)<250
                  && !RexSaveMem
                  /* and make sure it fits in array: */
                  && (am*fs->from)<FFS_MAX_PAT_LEN
                 )
                   {
                    int i,j,k;
                    byte *tmpp=ta[am];              /* save the pointer */
                    fs->nralloced=(byte)am;      /* set number really allocated */
                    for(i=1;i<fs->to;i++)      /* copy template i times */
                        for(k=am*i,j=0;j<am;j++)
                              ta[k+j]=ta[j];
                    am*=fs->to;
                    ta[am]=tmpp;                   /* put it at the end */
                    fs->from=fs->to=1;     /* now it's one long pattern */
                   }
               goto ENDOFPARSE;         /* possibly chain another rex */
              };
          default  :
             {
              if (!state->respectCase)
                   {
                    if(isupper(*s))
                        *(ta[am] + tolower(*s))=(byte)1;
                    if(islower(*s))
                        *(ta[am] + toupper(*s))=(byte)1;
                   }
              *(ta[am]+ *s++)=(byte)1;
              break;
             }
         }
    }
 putmsg(MERR,Fn,OutOfMem);
err:
 /* NOTE: see also closefpm(): */
 if(fs->nralloced)           /* special case repeat copies the pointers */
   {
     for(i=0;i<fs->nralloced;i++)
       if(ta[i]!=BPNULL)
         free(ta[i]);
   }
 else
   {
     for (i = 0; i <= am && ta[i]; i++)         /* Bug 6232: was OBOB */
       if (ta[i])
         {
           free(ta[i]);
           ta[i] = NULL;
         }
   }
 fs->expEnd = BYTEPN;
 return(-1);                                            /* PBR 01-02-91 */
}

/************************************************************************/

/* this gets the FFS all ready to roll except for setting up the buffer
pointers (which is left to the caller) */

void
initskiptab(fs)
register FFS *fs;
{
  /* static const char copyright[]="Copyright 1985,1986,1987,1988 P. Barton Richards"; */ /* see copyright below */
 register int i,j,k;
 register int patsz=fs->patsize;


                /* init set skip table to max jump len */
 if(patsz<=1)                                   /* no skip table needed */
    return;

 if(fs->backwards || fs->root )
     memset((char *)fs->bskiptab,patsz,(unsigned)DYNABYTE);
 if(!fs->backwards || fs->root )
     memset((char *)fs->skiptab,patsz,(unsigned)DYNABYTE);


 --patsz;   /* decrement patsz in order to ignore last byte int pattern */

 if(!fs->backwards || fs->root )
    {
     for(i=0,j=patsz;i<patsz;i++,j--)                       /* forwards */
          for(k=0;k<DYNABYTE;k++)
              if(fs->setlist[i][k])
                   fs->skiptab[k]=(byte)j;
    }
 if(fs->backwards || fs->root )
    {
     for(i=patsz;i;i--)                                    /* backwards */
         for(k=0;k<DYNABYTE;k++)
              if(fs->setlist[i][k])
                   fs->bskiptab[k]=(byte)i;
    }
/*    skip table notes:
fwd   bkwd
3214  4123
bart  bart
                        bart
this start is barts text art bar bart block
*/

}

/************************************************************************/

FFS *                                        /* always returns NULL ptr */
closefpm(fs)
FFS *fs;
{
 unsigned int i;
 if(fs==NULL)
    return(fs);

 if (fs->setlist != (byte **)NULL)
   {
     /* NOTE: see also f3par() error free: */
     if(fs->nralloced)           /* special case repeat copies the pointers */
        {
         for(i=0;i<fs->nralloced;i++)
             if(fs->setlist[i]!=BPNULL)
                  free(fs->setlist[i]);
        }
     else
        {
          for(i=0;i<fs->patsize && fs->setlist[i]!=BPNULL;i++)
            free(fs->setlist[i]);
        }
     free(fs->setlist);
   }
 if (fs->skiptab != BPNULL) free(fs->skiptab);
 if (fs->bskiptab != BPNULL) free(fs->bskiptab);
#ifdef EPI_BUILD_RE2
 if (fs->re2)
   {
     cre2_delete(fs->re2);
     fs->re2 = NULL;
   }
 if (fs->re2CaptureHits)
   {
     free((void *)fs->re2CaptureHits);
     fs->re2CaptureHits = NULL;
   }
 if (fs->re2CaptureHitSizes)
   {
     free(fs->re2CaptureHitSizes);
     fs->re2CaptureHitSizes = NULL;
   }
#endif /* EPI_BUILD_RE2 */
 free((char *)fs);
 return((FFS *)NULL);
}

/************************************************************************/


FFS *
openfpm(sOff, s)
size_t  sOff;   /* (in) `*s' offset in overall expression, for messages */
char *s;
{
 static const char     Fn[] = "openfpm";
 FFS *fs, *first = (FFS *)NULL, *prev = (FFS *)NULL, *last;
 int ps;
 byte   *p;
 TXrexParseState        state;

 TXrexParseState_INIT(&state);

 /* We moved the f3par() recursion on subexpressions to a loop here,
  * to save stack (especially new f3par() `ta' var):  KNG 040413
  */
 p = (byte *)s;
 do
   {
     fs=(FFS *)calloc(1,sizeof(FFS));
     if(fs==NULL||
        (fs->skiptab = (byte *)calloc(DYNABYTE, sizeof(byte))) == BPNULL ||
        (fs->bskiptab = (byte *)calloc(DYNABYTE, sizeof(byte))) == BPNULL)
       {
         putmsg(MERR + MAE, Fn, OutOfMem);
         goto err;
       }

/* PBR 01-02-91 f3par used to return 0 for error but I chgd it to -1 so I
could have a NULL expression be valid */
/* MAW 04-09-92 - store f3par() return in int so can compare to <0 before
casting to byte */
     if ((ps = f3par(&state, sOff + (size_t)(p - (byte *)s), &p, fs)) < 0)
       {
       err:
         first = closerex(first);
         return(closefpm(fs));
       }
     fs->patsize=(byte)ps;
     fs->prev = prev;
     if (prev != (FFS *)NULL) prev->next = fs;
     else first = fs;
     fs->first = first;                         /* Bug 6232 */
     prev = fs;
   }
 while (*p);

 /* KNG 20120807 add quick-access `first'/`last' links: */
 last = fs;
 for (fs = first; fs; fs = fs->next)
   {
     fs->first = first;
     fs->last = last;
   }

 return(first);
}


/************************************************************************/

int
notpm(fs)
FFS *fs;
{
 byte *buf,*end,*bp,*p;
 byte **sstr=fs->setlist;
 byte sz=fs->patsize;
 unsigned int n;

 ERR_IF_RE2(fs, return(-1));

 if(fs->backwards)
    {
     fs->hit=fs->end;
     buf=fs->start;
     bp=end=fs->end-sz;
     for ( ; ; fs->n++, bp--)
         {
           if (fs->n >= fs->to)                 /* at max-repetition */
             {
               /* Bug 3902: if max-repetition reached, hit is that many
                * bytes, just like if forwards:
                */
               fs->hit = bp + sz;               /* hit after would-be pat. */
               break;
             }
           if (bp < buf)                        /* no room left for pattern */
             {
               /* Bug 3902: if pattern not found, match to start of buffer.
                * (Also encompasses Bug 2314 for search-backwards):
                */
               fs->hit = buf;
               break;
             }
          /* KNG 20110920 optimization: can stop at first byte-match fail
           * as we do below for `!fs->backwards'; not just end of pattern:
           * (added during, but not part of, Bug 3902)
           */
          for (n = 0, p = bp; n < sz && sstr[n][*p]; n++, p++);
          if (n >= sz)                          /* whole pat. match so fail */
              {
               bp+=sz;       /* all of em matched so it failed */
               fs->hit=bp;                      /* hit starts after pat. */
               break;
              }
         }
     fs->hitsize=fs->end-fs->hit;
    }
 else
    {
     buf=fs->start;
     end=fs->end;
     fs->hit=bp=p=buf;
     for(;fs->n<fs->to;fs->n++,bp++)            /* advance 1 byte per rep */
        {
         if (end - bp < sz)                     /* no room left for pattern */
           {
             /* KNG 20080822 NOT expression should match to end of buffer
              * if not found, e.g. `>>abc=!xyz+' should match all of
              * `abcdefghi' not just `abcdef':  Bug 2314
              */
             if (bp < end) continue;            /* continue advance */
             break;
           }
         for (n = 0, p = bp; n < sz && sstr[n][*p]; n++, p++);
         if (n >= sz) break;                    /* whole pat. match so fail */
        }
     fs->hitsize=bp-buf;
    }
 return(fs->n);
}

/************************************************************************/

int
repeatpm(fs)                      /* gets repeated pattern count from p */
register FFS *fs;
{
#ifdef _WIN32     /* MAW 07-24-92 - windows far ptrs roll over on dec */
 register byte _huge *p, _huge *q;        /* pointer to curr location */
 byte _huge *s;
#else
 register byte *p, *q;                      /* pointer to curr location */
#endif
 register byte **sstr=fs->setlist;
 register unsigned int i;

 ERR_IF_RE2(fs, return(0));

 if(fs->backwards)
   {
    if(fs->from<0)                                      /* PBR 05-31-91 */
         {
          byte *start=fs->start;
          byte *end=fs->end;
          int  to=fs->to;
          int  rc;
          fs->to=1;
          if(fs->end-to>fs->start)  fs->start=fs->end-to;
          rc=fastpm(fs);
          fs->start=start;
          fs->end=end;
          fs->to=to;
          if(!rc) return(-2);
          fs->hitsize=fs->end-fs->hit;
          fs->hit=start;
          return(1);
         }
    fs->hit=fs->end; /* added  11/15/89 */
    if(fs->patsize==0) /* pbr 5/14/97 added buffer anchors */
      return(fs->start==fs->end ? fs->from : -2);

#ifdef _WIN32     /* MAW 07-24-92 - windows far ptrs roll over on dec */
    for(p=fs->end,p-=fs->patsize,s=fs->start;
        fs->n<fs->to && p>=s;
        p-=fs->patsize)
#else
    for(p=fs->end-fs->patsize;
        fs->n<fs->to && p>=fs->start;
        p-=fs->patsize)
#endif
       {
        for(i=0,q=p;i<fs->patsize && sstr[i][*q];i++,q++);
        if(i<fs->patsize)
          break;
        ++fs->n;
        fs->hitsize+=fs->patsize;
        fs->hit=p;                           /* added this line 9/26/89 */
       }
    return(fs->n);
   }
 else
   {
    if(fs->from<0)                                      /* PBR 05-31-91 */
         {
          byte *start=fs->start;
          byte *end=fs->end;
          int  to=fs->to;
          int  rc;
          fs->to=1;
          if(fs->start+to<fs->end) fs->end=fs->start+to;
          rc=fastpm(fs);
          fs->start=start;
          fs->end=end;
          fs->to=to;
          if(!rc) return(-2);
          fs->hitsize=fs->hit+fs->patsize-fs->start;
          fs->hit=start;
          return(1);
         }
    fs->hit=fs->start;
    if(fs->patsize==0)/* pbr 5/14/97 added buffer anchors */
      return(fs->start==fs->end ? fs->from : -2);
    for(p=fs->start;
        fs->n<fs->to && p+fs->patsize<=fs->end;
        p+=fs->patsize)
       {
        for(i=0,q=p;i<fs->patsize && sstr[i][*q];i++,q++);
        if(i<fs->patsize) return(fs->n);
        fs->n++;
        fs->hitsize+=fs->patsize;
       }
   }
 return(fs->n);
}

/************************************************************************/

int
backnpm(fs,beg)                         /* backwards next pattern match */
register FFS *fs;
byte *beg;
{
 register FFS *pfs;

 ERR_IF_RE2(fs, return(0));

 for(pfs=fs->prev;pfs!=(FFS *)NULL;pfs=pfs->prev) /* <-ex */
    {
     pfs->n=pfs->hitsize=0;
     pfs->end=pfs->next->hit;
     pfs->start=beg;
     if(pfs->is_not)
         {
          if(notpm(pfs)<pfs->from)
              return(0);
         }
     else
         {
          if(repeatpm(pfs)<pfs->from)
              return(0);
         }
    }
 return(1);
}

/************************************************************************/

int
forwnpm(fs,end)                          /* forwards next pattern match */
register FFS *fs;
byte *end;
{
 register FFS *pfs;

 ERR_IF_RE2(fs, return(0));

 for(pfs=fs->next;pfs!=(FFS *)NULL;pfs=pfs->next) /* <-ex */
    {
     pfs->n=pfs->hitsize=0;
     pfs->start=pfs->prev->hit+pfs->prev->hitsize;
     pfs->end=end;
     if(pfs->is_not)
         {
          if(notpm(pfs)<pfs->from)
              return(0);
         }
     else
         {
          if(repeatpm(pfs)<pfs->from)
              return(0);
         }
    }
 return(1);
}

/************************************************************************/

byte *    /* goes to the first pattern in the chain and returns the hit */
rexhit(fs)
register FFS *fs;
{
  fs = fs->first;
 for(;fs!=(FFS *)NULL &&
      fs->next!=(FFS *)NULL &&                        /* MAW 01-08-97 */
      fs->exclude<0;fs=fs->next);                     /* PBR 04-24-91 */
 return(fs->hit);
}

/************************************************************************/

int                        /* returns number of patterns in the chain */
rexscnt(fs)                                           /* MAW 12-03-97 */
register FFS *fs;
{
  size_t        n = 0;

  if (!fs) return(0);

  if (fs->re2) return(fs->re2NumCaptureGroups);

  for (fs = fs->first; fs; fs = fs->next)
    if (fs->subExprIndex > n) n = fs->subExprIndex;
  return((int)(n + 1));
}
/************************************************************************/

static FFS *
TXrexGetSubExpr(FFS *fs, int n)
/* Returns Nth (counting from 0) subexpression of `fs', or NULL if none.
 * Bug 6234: Accounts for continued (too-long) subexpressions.
 */
{
  if (n < 0 || fs->re2) return(NULL);
  for (fs = fs->first; fs && fs->subExprIndex != (size_t)n; fs = fs->next);
  return(fs);
}

FFS *                               /* returns pattern n in the chain */
rexsexpr(fs,n)                                        /* MAW 12-03-97 */
register FFS *fs;
int n;
{
  return(TXrexGetSubExpr(fs, n));
}
/************************************************************************/

byte *          /* goes to pattern n in the chain and returns the hit */
rexshit(fs,n)
register FFS *fs;
int n;
{
  if (fs->re2)
    {
      if (n >= 0 && n < fs->re2NumCaptureGroups)
        return((byte *)fs->re2CaptureHits[n]);
      return(NULL);
    }

  fs = TXrexGetSubExpr(fs, n);
  if (!fs) return(NULL);
  return(fs->hit);
}

/************************************************************************/



int
rexsize(fs)                                /* returns the size of a hit */
register FFS *fs;
{
 register int size=0;
 fs = fs->first;
 for(;fs!=(FFS *)NULL;fs=fs->next)
   {
    if(fs->exclude<0) continue;         /* PBR 04-24-91 handle excludes */
    else
    if(fs->exclude>0) break;
    size+=fs->hitsize;
   }
 return(size);
}

/************************************************************************/



int
rexfsize(fs)                             /* returns the size of a hit */
register FFS *fs;
{
 register int size=0;
 fs = fs->first;
 for(;fs!=(FFS *)NULL;fs=fs->next)
    size+=fs->hitsize;
 return(size);
}

/************************************************************************/

int             /* goes to pattern n in the chain and returns the len */
rexssize(fs,n)
register FFS *fs;
int n;
{
  size_t        sz;

  if (fs->re2)
    {
      if (n >= 0 && n < fs->re2NumCaptureGroups)
        return(fs->re2CaptureHitSizes[n]);
      return(0);
    }

  fs = TXrexGetSubExpr(fs, n);
  /* Bug 6234: A continued (too-long) subexpression silently subtends
   * multiple subexpressions:
   */
  for (sz = 0; fs && fs->subExprIndex == (size_t)n; fs = fs->next)
    sz += fs->hitsize;
  return(sz);
}

/* ======================================================================== */

#ifdef EPI_BUILD_RE2
static byte *
TXrexGetRe2(FFS *fs)
/* Guts of search for RE2.
 */
{
  static const char     fn[] = "TXrexGetRe2";
#  if EPI_RE2_DATE > 2011-01-18
  typedef cre2_string_t         TXcre2String;
#  else /* EPI_RE2_DATE <= 2011-01-18 */
  typedef struct string_piece   TXcre2String;
#  endif /* EPI_RE2_DATE <= 2011-01-18 */
  TXcre2String          matchesTmp[16], *matches = matchesTmp;
  int                   i, re2BufLen;

  /* cre2_match() takes search buffer size of int, not size_t: */
  if ((size_t)(fs->end - fs->start) > (size_t)EPI_OS_INT_MAX)
    {
      putmsg(MERR + MAE, fn, "REX: Search buffer too large for RE2");
      goto err;
    }

  /* Alloc `matches' array if needed: */
  if ((size_t)(1 + fs->re2NumCaptureGroups) > TX_ARRAY_LEN(matchesTmp))
    {
      matches = (TXcre2String *)calloc(1 + fs->re2NumCaptureGroups,
                                       sizeof(TXcre2String));
      if (!matches)
        {
          putmsg(MERR + MAE, __FUNCTION__, OutOfMem);
          goto err;
        }
    }

  /* Find the match: */
  if (fs->end - fs->start > (EPI_SSIZE_T)EPI_OS_INT_MAX)
    {
      putmsg(MERR + UGE, fn, "RE2: Search buffer too large (%wd > max %d)",
             (EPI_HUGEINT)(fs->end - fs->start), (int)EPI_OS_INT_MAX);
      re2BufLen = EPI_OS_INT_MAX;
    }
  else
    re2BufLen = (int)(fs->end - fs->start);
  if (cre2_match(fs->re2, (const char *)fs->start, re2BufLen, 0,
                 re2BufLen, CRE2_UNANCHORED, matches,
                 1 + fs->re2NumCaptureGroups))
    {                                           /* found a match */
      fs->hit = (byte *)matches[0].data;
      fs->hitsize = matches[0].length;
      for (i = 0; i < fs->re2NumCaptureGroups; i++)
        {
          fs->re2CaptureHits[i] = matches[1 + i].data;
          fs->re2CaptureHitSizes[i] = matches[1 + i].length;
        }
    }
  else                                          /* no match */
    {
    err:
      fs->hit = NULL;
      fs->hitsize = 0;
      memset((void *)fs->re2CaptureHits, 0,
             fs->re2NumCaptureGroups*sizeof(const char *));
      memset(fs->re2CaptureHitSizes, 0,
             fs->re2NumCaptureGroups*sizeof(size_t));
    }
  goto finally;

finally:
  if (matches != NULL && matches != matchesTmp)
    {
      free(matches);
      matches = NULL;
    }
  return(fs->hit);
}
#endif /* EPI_BUILD_RE2 */

/************************************************************************/

           /* pbr 7/14/90 MAJOR changes  (complete rewrite) */
byte *
getrex(fs,buf,end,operation)
FFS *fs;
byte *buf,*end;
TXPMOP operation;
{
 static const char      fn[] = "getrex";
 register FFS *ffs=fs;
 byte *lasthit=BPNULL, *lastMatchBegin = NULL, *lastMatchEnd = NULL;
 int backwards=0;
#ifdef TX_NO_REDUNDANT_EMPTY_REX_HITS
#  define noEmpties     1
#else /* !TX_NO_REDUNDANT_EMPTY_REX_HITS */
 static int noEmpties = -1;

 if (noEmpties < 0)
   noEmpties = (getenv("TX_NO_REDUNDANT_EMPTY_REX_HITS") != NULL);
#endif /* !TX_NO_REDUNDANT_EMPTY_REX_HITS */

 if (ffs->patsize == 0 && !ffs->re2)    /* pbr 5/14/97 added buffer anchors */
    {
     if(operation==CONTINUESEARCH || operation==BCONTINUESEARCH)
       return(BPNULL);
     if(ffs->next!=NULL)
        {
         ffs->hitsize=0;
         backwards=ffs->backwards=0;
         ffs->hit=ffs->start=buf;
         ffs->end=end;
         lasthit = lastMatchBegin = lastMatchEnd = BPNULL;
         goto ANCHORED;
        }
     else
     if (ffs->prev!=NULL)
        {
         ffs->hitsize=0;
         backwards=ffs->backwards=1;
         ffs->hit=end;
         ffs->start=buf;
         ffs->end=end-1;
         lasthit = lastMatchBegin = lastMatchEnd = BPNULL;
         goto ANCHORED;
        }
      else
        return(BPNULL);
    }
 else
 switch(operation)
    {
     case SEARCHNEWBUF    :
         {
          ffs->start=buf;
          ffs->end=end;
          backwards=ffs->backwards=0;
          lasthit = lastMatchBegin = lastMatchEnd = BPNULL;/* MAW 01-09-97 - do not set on newbuf */
         };break;
     case BSEARCHNEWBUF   :
         {
           if (fs->re2)
             {
               putmsg(MERR + UGE, fn, "RE2: Cannot search backwards");
               goto err;
             }
          ffs->start=buf;
          ffs->end=end-1;/* pbr aug 29 90 fixed obob , added -1 */
          backwards=ffs->backwards=1;
          lasthit = lastMatchBegin = lastMatchEnd = BPNULL;/* MAW 01-09-97 - do not set on newbuf */
         };break;
     case CONTINUESEARCH  :
         {
          ffs->start=rexhit(ffs)+rexsize(ffs);/* MAW 09-08-92 rexhit() instead ofr ffs->hit */
          ffs->end=end;
          backwards=ffs->backwards=0;
          lasthit = ffs->hit;                         /* MAW 01-09-97 */
          lastMatchBegin = ffs->first->hit;
          lastMatchEnd = ffs->last->hit + ffs->last->hitsize;
         };break;
     case BCONTINUESEARCH :
         {
           if (fs->re2)
             {
               putmsg(MERR + UGE, fn, "RE2: Cannot search backwards");
             err:
               ffs->hit = NULL;
               ffs->hitsize = 0;
               return(NULL);
             }
          ffs->start=buf;
          ffs->end=rexhit(ffs)-rexsize(ffs);/* MAW 09-08-92 rexhit instead of ffs->hit */
          backwards=ffs->backwards=1;
          lasthit = ffs->hit;                         /* MAW 01-09-97 */
          lastMatchBegin = ffs->first->hit;
          lastMatchEnd = ffs->last->hit + ffs->last->hitsize;
         };break;
    }

#ifdef EPI_BUILD_RE2
 if (fs->re2) return(TXrexGetRe2(fs));
#endif /* EPI_BUILD_RE2 */

           /* this is a check for null expressions */
 if(ffs->patsize==0)                                    /* PBR 01-02-91 */
    {
     ffs->hitsize=0;
     if(ffs->backwards) ffs->hit=buf;
     else               ffs->hit=end;
     return(ffs->hit);
    }

 if(ffs->start<buf || ffs->end>end)                     /* bounds check */
    return(BPNULL);

 for(fastpm(ffs);ffs->hit!=BPNULL;fastpm(ffs))
     {
      ANCHORED:
                   /* JMT 12-23-96 - check lasthit to prevent looping */
      if(backnpm(ffs,buf) && forwnpm(ffs,end) &&
         /* KNG 20120829 Bug 4386 check for consistent progress, not just
          * not-equal:
          */
         (!lasthit ||
          (noEmpties ? (backwards ? ffs->hit < lasthit : ffs->hit > lasthit) :
           ffs->hit != lasthit)))
        {
          /* Bug 4352: Do not return redundant empty hits, e.g. `a*'
           * will return both `a' and `' when searching against `a':
           * second hit is redundant, as it is empty and entirely
           * mergeable with previous hit.  Must be careful not to
           * reject valid empty hits, e.g. if \P \F subexpressions
           * make a non-empty *match* into an empty *returned* hit.
           * Rule we use is to reject hit if entire match (including
           * \P \F) is empty and adjacent to entire previous match.
           * We use entire match (including \P \F) instead of just
           * returned hit, because the presence of \P \F does not
           * alter the *number* of hits (and location of each subexpr's
           * hit), just the length of each hit: therefore the presence
           * of \P \F should not alter the number of hits when this
           * feature (skip redundant empty hits) is enabled either:
           */
          if (!noEmpties) return(rexhit(ffs));
          if (ffs->first->hit != ffs->last->hit + ffs->last->hitsize ||/*!mt*/
              ffs->first->hit != lastMatchEnd)  /*not adjacent to last match*/
            return(rexhit(ffs));
        }
      if(ffs->patsize==0)                         /* anchor not found */
         return(BPNULL);
      if(backwards)
          {
           ffs->end  = ffs->patsize>1  ?
               ffs->hit-ffs->bskiptab[*ffs->hit]:
               ffs->hit-1;
          }
      else
          {
           ffs->start= ffs->patsize>1  ?
               ffs->hit+ffs->skiptab[*(ffs->hit+ffs->patsize-1)] :
               ffs->hit+1;
          }
       if(ffs->start<buf || ffs->start>end)               /* bounds check */
           return(BPNULL);
       if(ffs->end<buf || ffs->end>end)                   /* bounds check */
           return(BPNULL);
     }
 return(BPNULL); /* NULL */
#undef noEmpties
}

/************************************************************************/

byte *
getfpm(fs,buf,end,operation)     /* NOW an alias for the same operation */
register FFS *fs;
register byte *buf,*end;
register TXPMOP operation;
{
 return(getrex(fs,buf,end,operation));
}

/************************************************************************/

FFS *
closerex(fs)
FFS *fs;
{
 FFS *nxt;
 for(fs=firstexp(fs);fs!=(FFS *)NULL;fs=nxt)
   {
    nxt=fs->next;    /* MAW 09-27-90 - fs is invalid after closefpm() */
    closefpm(fs);
   }
 return((FFS *)NULL);
}

/* ------------------------------------------------------------------------ */

static FFS *
TXrexOpenRe2(const char *expr)
{
  static const char     fn[] = "TXrexOpenRe2";
  FFS                   *fs = NULL;
#ifdef EPI_BUILD_RE2
#  if EPI_RE2_DATE > 2011-01-18
  cre2_options_t        *opt = NULL;
#  else /* EPI_RE2_DATE <= 2011-01-18 */
  cre2_options          *opt = NULL;
#  endif /* EPI_RE2_DATE <= 2011-01-18 */

  opt = cre2_opt_new();
  if (!opt)
    {
      putmsg(MERR, fn, "RE2: Cannot create options object");
      goto err;
    }
#  if EPI_RE2_DATE > 2011-01-18
  cre2_opt_set_log_errors(opt, 0);              /* no fprintf(stderr) */
#  else /* EPI_RE2_DATE <= 2011-01-18 */
  cre2_opt_log_errors(opt, 0);                  /* no fprintf(stderr) */
#  endif /* EPI_RE2_DATE <= 2011-01-18 */
  fs = (FFS *)calloc(1, sizeof(FFS));
  if (!fs)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      goto err;
    }
  /* Set up expected REX management fields: */
  fs->exp = (byte *)expr;
  fs->expEnd = (byte *)expr + strlen(expr);
  fs->first = fs->last = fs;
  fs->root = 1;
  /* Open the RE2 expression: */
  if (fs->expEnd - fs->exp > (EPI_SSIZE_T)EPI_OS_INT_MAX ||
      !(fs->re2 = cre2_new((char *)fs->exp, (int)(fs->expEnd - fs->exp), opt)))
    {
      putmsg(MERR, fn, "RE2: Cannot open expression `%s'", expr);
      goto err;
    }
  if (cre2_error_code(fs->re2))
    {
      putmsg(MERR + UGE, fn, "RE2: Invalid expression `%s': %s",
             expr, cre2_error_string(fs->re2));
      goto err;
    }
  fs->re2NumCaptureGroups = cre2_num_capturing_groups(fs->re2);
  if (fs->re2NumCaptureGroups < 0) fs->re2NumCaptureGroups = 0;
  if (fs->re2NumCaptureGroups > 0)
    {
      fs->re2CaptureHits = (const char **)calloc(fs->re2NumCaptureGroups,
                                                 sizeof(const char *));
      fs->re2CaptureHitSizes = (size_t *)calloc(fs->re2NumCaptureGroups,
                                                sizeof(size_t));
      if (!fs->re2CaptureHits || !fs->re2CaptureHitSizes)
        {
          putmsg(MERR + MAE, fn, OutOfMem);
          goto err;
        }
    }
  goto finally;
#else /* !EPI_BUILD_RE2 */
  putmsg(MERR, fn, "REX: RE2 not supported on this platform");
  goto err;
#endif /* !EPI_BUILD_RE2 */

err:
  fs = closefpm(fs);
#ifdef EPI_BUILD_RE2
finally:
  if (opt)
    {
      cre2_opt_delete(opt);
      opt = NULL;
    }
#endif /* EPI_BUILD_RE2 */
  return(fs);
}

/************************************************************************/

FFS *
openrex(s, syntax)
byte *s;
TXrexSyntax     syntax;
{
 static const char Fn[] = "openrex";
 FFS *tfs, *fs = NULL, *best;
 float hival=(float)0.0;
 int exclude;                                           /* PBR 04-24-91 */
 byte   *sOrg = s;

#ifndef EPI_ENABLE_RE2
 if (Re2Enabled == -1) Re2Enabled = (getenv("EPI_ENABLE_RE2") != NULL);
#endif /* !EPI_ENABLE_RE2 */

 /* Determine mode (REX or RE2): */
 if (Re2Enabled)
   {
     if (*s == EESC && s[1] == OPEN_TOKEN)      /* `\<'... */
       {
         byte   *e;

         /* See also similar parse in rlex_addexp(): */
         s += 2;                                /* skip `\<' */
         for (e = s; *e; e++)                   /* find `\>' */
           if (*e == EESC && e[1] == CLOSE_TOKEN &&
               /* Ignore `\>>' when looking for `\>'; former has
                * precedence since it pre-dates `\>' syntax.
                * See also similar code in dobslash() and rlex_addexp():
                */
               (CLOSE_TOKEN != '>' || e[2] != '>'))
             break;
         if (!*e)                               /* no ending `\>' */
           {
             putmsg(MERR + UGE, Fn,
   "REX: Expression `%s' missing closing `%c%c' in `%c%c' escape at offset 0",
                    sOrg, EESC, CLOSE_TOKEN, EESC, OPEN_TOKEN);
             goto err;
           }
         if (e - s == RE2_TOKEN_LEN &&
             strncmp((char *)s, Re2Token, RE2_TOKEN_LEN) == 0)
           {                                    /* `\<re2\>' */
             s += RE2_TOKEN_LEN + 2;            /* skip `re2\>' */
             goto openRe2;
           }
         if (e - s == REX_TOKEN_LEN &&
             strncmp((char *)s, RexToken, REX_TOKEN_LEN) == 0)
           {                                    /* `\<rex\>' */
             s += REX_TOKEN_LEN + 2;            /* skip `rex\>' */
             goto openRex;                      /* open as REX */
           }
         if (e - s == TX_REX_TOKEN_NO_MATCH_LEN &&
             strncmp((char *)s, TX_REX_TOKEN_NO_MATCH,
                     TX_REX_TOKEN_NO_MATCH_LEN) == 0)
           {
             /* `\<nomatch\>' is only valid in a multi-expression
              * context, i.e. rexlex, which checks for and handles it
              * without ever calling REX.
              * Same error message in TXrlexDoneAdding():
              */
             putmsg(MERR + UGE, __FUNCTION__,
                   "REX: `%c%c%s%c%c' is only valid with other expressions"
#ifdef TEST
                   " (i.e. REXLEX)"
#endif /* TEST */
                    ,
                    EESC, OPEN_TOKEN, TX_REX_TOKEN_NO_MATCH,
                    EESC, CLOSE_TOKEN);
             goto err;
           }
         putmsg(MERR + UGE, Fn,
                "REX: Unknown `%c%c' escape token `%.*s' at offset 0",
                EESC, OPEN_TOKEN, (int)(e - s), s);
         goto err;
       }
     /* no `\<...\>' escape; open in specified default syntax: */
     if (syntax == TXrexSyntax_Rex)
       ;
     else if (syntax == TXrexSyntax_Re2)
       {
       openRe2:
         fs = TXrexOpenRe2((const char *)s);
         goto finally;
       }
     else
       {
         putmsg(MERR, Fn, "REX: Invalid syntax argument %d", (int)syntax);
         goto err;
       }
   }

openRex:
 tfs = best = fs = openfpm(s - sOrg, (char *)s);

 if(fs==(FFS *)NULL)
   goto err;

 for(tfs=fs;tfs!=(FFS *)NULL;tfs=tfs->next)            /* calculate eff */
    {
     float val;
     if(tfs->from<0)                                    /* PBR 05-31-91 */
         continue;
     if(tfs->root)              /* the user specified this one as first */
        {
         best=tfs;
         break;
        }
     if(tfs->from==0)                       /* calculate pattern effec. */
          val= -(float)tfs->patsize;
     else if(tfs->from==1 && tfs->to==1)
          val=(float)tfs->patsize*(float)2.0;
     else val=(float)tfs->patsize;

     if(val>=hival)              /* reassign best to the fastest so far */
        {                             /* if two are == use the last one */
         best=tfs;
         hival=val;
        }
    }

 /* set the patterns leading up to the "fastest" as backwards
 and the ones after it to forwards */
 if(best!=(FFS *)NULL)
       best->root=1;
 if(best->is_not)
    {
      putmsg(MERR,(char *)NULL,"REX: Root expression `%.*s' cannot have a NOT operator at offset %d", (int)(best->expEnd - best->exp), best->exp,
             (int)(best->exp - sOrg));
      goto err;
    }

 /* up to fastest are backwards */
 for(tfs=fs;tfs!=(FFS *)NULL && tfs!=best;tfs=tfs->next)
    tfs->backwards=1;

 /* past and including the fastest are forwards */
 for(;tfs!=(FFS *)NULL;tfs=tfs->next)
    tfs->backwards=0;

 for(tfs=fs;tfs!=(FFS *)NULL;tfs=tfs->next)      /* set the skip tables */
   {
    if(tfs->from<0 || tfs==best)                        /* PBR 05-31-91 */
         initskiptab(tfs);
   }
                     /* PBR 04-24-91 set excludes */
 exclude=0;
 for(tfs=fs;tfs!=(FFS *)NULL;tfs=tfs->next)
    {
     FFS *efs;
     if(tfs->exclude>0)                        /* chek for fwd excludes */
     {
        if(tfs==fs || tfs->prev->exclude < 0) goto rexerr;/* JMT 1999-12-15 */
        exclude=1;
     }
     else
     if(tfs->exclude<0)                        /* do backwards excludes */
     {
     	  if(tfs->next == NULL) goto rexerr;/* JMT 1999-12-15 */
          for(efs=tfs;efs!=(FFS *)NULL;efs=efs->prev)
              efs->exclude= -1;
     }
     else tfs->exclude=exclude;
    }

 fs = best;                                     /* return the root pattern */
 goto finally;

rexerr:
 putmsg(MWARN + UGE, Fn,
        "REX: Expression `%s' will not match anything (all %cP or %cF)",
        s, EESC, EESC);
err:
 fs = closerex(fs);
finally:
 return(fs);
}

/************************************************************************/

 static const char copyright[]=
/************************************************************************/
           "Copyright 1985,1986,1987,1988 P. Barton Richards";
/************************************************************************/

int
fastpm(fs)
FFS *fs;
{
                   /* in order of register priority */
#ifdef _WIN32     /* MAW 05-20-92 - windows far ptrs roll over on dec */
 register byte _huge *bufptr;                 /* the byte being tested */
#else
 register byte *bufptr;                        /* the byte being tested */
#endif
 register byte *hitc = BPNULL,              /* last byte in the pattern */
                slen,          /* this is the ending byte in the string */
               *endptr;                                   /* end of buf */

 register byte *jmptab;                                   /* jump table */

 byte           **sstr = (byte **)NULL;                /* search string */


 ERR_IF_RE2(fs, return(0));

 if(fs->from==0)                       /* with 0 reps it's always a hit */
    {
     fs->hitsize=0;
     fs->n=0;
     if(fs->backwards)  fs->end+=1;                   /* MAW 04-13-93 */
     /*if(fs->backwards)  fs->hit=fs->end;*/          /* MAW 04-13-93 */
     /*else               fs->hit=fs->start;*/        /* MAW 04-13-93 */
     repeatpm(fs);
     return(1);
    }

 fs->hitsize=fs->patsize;                             /* init the sizes */
 fs->n=1;

 if(fs->backwards)
    {
      jmptab=fs->bskiptab;
      slen=(byte)(fs->patsize-1);
      sstr=fs->setlist;
      hitc=(byte *)fs->setlist[0];
      endptr=fs->start;
      bufptr=fs->end - slen;

      if(!slen)                             /* faster single byte match */
         {
          for(;bufptr>=endptr;bufptr--)
              {
               if(*(hitc + *bufptr))
                   {
                    if(fs->to==1)
                        {
                         fs->hit=bufptr;
                         return(1);
                        }
                    else
                        {
                         /*fs->end=bufptr-1;*/        /* MAW 04-13-93 */
                         fs->end=bufptr;              /* MAW 04-13-93 */
                         if(repeatpm(fs)>=fs->from)
                             return(1);
                         else
                             {
                              fs->n=1;
                              fs->hitsize=1;
                             }
                        }
                   }
              }
         }
      while(bufptr>=endptr)               /* longer than one byte match */
         {
          if(*(hitc+ *bufptr))                                    /* == */
              {               /*  match them until != or complete match */
               register byte *hsc;                      /* hit str byte */
               register byte **ss;
               for(hsc=bufptr+1,ss=sstr+1;
                   *ss!=BPNULL && *(*ss + *hsc);
                   hsc++,ss++);
               if(*ss==BPNULL)
                   {
                    if(fs->to==1)
                        {
                         fs->hit=bufptr;
                         return(1);
                        }
                    else
                        {
                         /*fs->end=bufptr-1;*/        /* MAW 04-13-93 */
                         fs->end=bufptr;              /* MAW 04-13-93 */
                         if(repeatpm(fs)>=fs->from)
                              return(1);
                         else
                             {
                              fs->n=1;
                              fs->hitsize=fs->patsize;
                             }
                        }
                   }
              }
          bufptr-= *(jmptab + *bufptr);                              /* skip */
         }
    }
 else
    {
      jmptab=fs->skiptab;
      slen=(byte)(fs->patsize-1);
      sstr=fs->setlist;
      bufptr=fs->start+slen;
      hitc=(byte *)fs->setlist[slen];
      endptr=fs->end;

      if(!slen)                             /* faster single byte match */
         {
          for(;bufptr<endptr;bufptr++)/* wtf? maw 08-04-92 "<=" to "<" */
             {
              if(*(hitc + *bufptr))
                   {
                    if(fs->to==1)
                        {
                         fs->hit=bufptr;
                         return(1);
                        }
                    else
                        {
                         fs->start=bufptr+1;
                         if(repeatpm(fs)>=fs->from)
                             {
                              fs->hit=bufptr;
                              return(1);
                             }
                         else
                             {
                              fs->n=1;
                              fs->hitsize=1;
                             }
                        }
                   }
              }
         }
      while(bufptr<endptr)/* longer than one byte match *//* wtf? maw 08-04-92 "<=" to "<" */
         {
          if(*(hitc + *bufptr))                                       /* == */
              {               /*  match them until != or complete match */
               register byte *hsc;                      /* hit str byte */
               register byte **ss;
/* NOTE: MAW 10-25-93
HP800 totally screwed up "x=bufptr-slen;" statements. it behaved as if
"x=bufptr+slen" at each occurrence. only tried where slen==8, but also appeared
to be a problem with slen>=8. Occurred with and without -O optimization.
*/
               hsc=bufptr;
               hsc-=slen;
               for(ss=sstr;
                   *ss!=BPNULL && *(*ss + *hsc);
                   hsc++,ss++);
               if(*ss==BPNULL)
                   {
                    fs->hit=bufptr;
                    fs->hit-=slen;
                    fs->start=fs->hit;
                    if(fs->to==1)
                        {
                         return(1);
                        }
                    else
                        {
                         fs->start=bufptr+1;
                         if(repeatpm(fs)>=fs->from)
                             {
                              fs->hit=bufptr;
                              fs->hit-=slen;
                              return(1);
                             }
                         else
                             {
                              fs->n=1;
                              fs->hitsize=fs->patsize;
                             }
                        }
                   }
              }
          bufptr+= *(jmptab + *bufptr);                              /* skip */
         }
    }
 fs->hit=BPNULL;                                              /* no hit */
 fs->n=0;
 fs->hitsize=0;
 return(0);
}

/************************************************************************/

FFS *
mknegexp(ex) /* makes an expression that is exclusive of the c's in ex */
FFS *ex;
{
 unsigned int i,j;
 byte tab[DYNABYTE];
 char s[2];

 ERR_IF_RE2(ex, return(NULL));

 ex=firstexp(ex);
 for(i=0;i<DYNABYTE;i++)
    tab[i]=1;

 for(ex=firstexp(ex);ex!=(FFS *)NULL;ex=ex->next)
    {
     if(ex->is_not) continue;
     for(i=0;i<ex->patsize;i++)
         for(j=0;j<DYNABYTE;j++)
              if(ex->setlist[i][j]) tab[j]=0;
    }

 strcpy(s,"a"); /* make a fake exp */
 ex=openrex((byte *)s, TXrexSyntax_Rex);
 if(ex==(FFS *)NULL) return(ex);
 for(i=0;i<DYNABYTE;i++)
    ex->setlist[0][i]=tab[i];
 return(ex);
}


/************************************************************************/
/************************************************************************/
/************************************************************************/
/************************************************************************/
/************************************************************************/
#ifdef TEST
/************************************************************************/
/************************************************************************/
/************************************************************************/
/************************************************************************/

#ifdef UNIX
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifdef PRIME
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif




byte nocntrl=0;             /* tells it not to print control characters */
byte includesdx=0;             /* tells it not to print the starting ex */
byte includeedx=0;               /* tells it not to print the ending ex */
byte begishit=0;                 /* use the begin &&|| end as the delim */
byte endishit=0;
byte notlines=0;  /* print lines ( or other things ) not containing exp */
byte printfname=1;                               /* print the file name */


/************************************************************************/

byte *                /* returns ptr to byte following last one printed */
bindex(buf,bufend,hitex,begex,endex)       /* bind expression to delims */
byte **buf,**bufend;      /* i change these to the start and end of hit */
FFS *hitex;
FFS *begex;
FFS *endex;
{
 byte *beg,*end;

            /* look for start and end of expression bounds */
 beg=rexhit(hitex);                                     /* PBR 04-24-91 */
 end=beg+rexsize(hitex);                                /* PBR 04-24-91 */

 if(!begishit)
   {
    if((beg=getrex(begex,*buf,beg,BSEARCHNEWBUF))==BPNULL)
         beg= *buf;
    if(beg!= *buf && !includesdx)
         beg+=rexsize(begex);
   }

 if(!endishit)
   {
    if((end=getrex(endex,end,*bufend,SEARCHNEWBUF))==BPNULL)
         end= *bufend;
    if(end!= *bufend && includeedx)
         end+=rexsize(endex);
   }
 *buf=beg;
 *bufend=end;
 return(end);
}

/************************************************************************/
                   /* prints the located expression */


byte *                /* returns ptr to byte following last one printed */
printex(beg,end,hitex,begex,endex)
byte *beg,*end;
FFS *hitex;
FFS *begex;
FFS *endex;
{
 bindex(&beg,&end,hitex,begex,endex);
 if(nocntrl)
    {
      for(;beg<end;beg++)
         putchar(iscntrl(*beg) ? ' ' : *beg);
#     ifdef _WIN32                  /* MAW 03-15-90 - prevent "\r\r\n" */
         putchar(0x0d);
#     endif
    }
 else
    {
      for(;beg<end;beg++)
         putchar(*beg);
#     ifdef _WIN32                  /* MAW 03-15-90 - prevent "\r\r\n" */
         if(*--beg!=0x0d)
            putchar(0x0d);
#     endif
    }
 putchar('\n');                                       /* ensure newline */
 return(end); /* the last one printed */
}


/************************************************************************/

#ifdef SMALL_MEM
#   define SRCHBUFSZ 32000
#else
#   define SRCHBUFSZ 128000
#endif

#define MINSRCHBUF   10000
#define SRCHBUFDEC   1000

byte *srchbuf=BPNULL;                   /* the main serach buffer */
int srchbufsz;                               /* the size of this buffer */

FILE *ipfh;
#define FILEOPEN(a,b) fopen((a), (b))
#define FILECLOSE(a)  fclose((a))
#define STDIN_HANDLE stdin


/************************************************************************/

void
allocsrchbuf()
{
 for(srchbufsz=SRCHBUFSZ,srchbuf=BPNULL;
     srchbufsz>MINSRCHBUF  && (srchbuf=(byte *)malloc(srchbufsz))==BPNULL ;
     srchbufsz-=SRCHBUFDEC);
}

/************************************************************************/


int
findlines(fname,ex,begex,endex)
byte *fname;
FFS *ex;
FFS *begex;
FFS *endex;
{
 register byte *loc;
 register byte *end;
 register int nread;
 byte *lasthit;

 if(fname==NULL)
   ipfh=STDIN_HANDLE;
 else
    {
     ipfh=FILEOPEN((char *)fname,EPI_FREAD);
     if(ipfh==(FILE *)NULL)
         {
          fprintf(stderr,"%s: Cannot open file `%s' for reading: %s\n",
                  ProgramName, fname, TXstrerror(TXgeterror()));
          return(-1);
         }
     }

#ifdef NEVER_MSDOS   /* 11/15/89  caused prob in large mod under dos ?? */
 setbuf(ipfh,(char *)NULL);   /* no BUFFERING */
#endif
#ifdef _WIN32                       /* MAW 03-15-90 - prevent "\r\r\n" */
 setmode(fileno(stdout),O_BINARY);     /* use unxlated mode for msdos */
 setmode(fileno(ipfh),O_BINARY);      /* MAW 04-04-90 - in case stdin */
#else
#endif

while((nread=freadex(ipfh,srchbuf,srchbufsz,endex))>0)
    {
     end=srchbuf+nread;
     lasthit=srchbuf;
     for(loc=getrex(ex,srchbuf,end,SEARCHNEWBUF);loc;)
         {
          if(ipfh!=STDIN_HANDLE && printfname)
              printf("%s:",fname);
          if(notlines)
              {
               byte *a=lasthit,*b=end;
               loc=bindex(&a,&b,ex,begex,endex);
               fwrite((char *)lasthit,(unsigned)(a-lasthit),sizeof(char),stdout);
               lasthit=loc;
               if(loc>=end) break;
              }
          else
              {
               if((loc=printex(srchbuf,end,ex,begex,endex) ) >=end)
                   break;
              }
          loc=getrex(ex,loc,end,SEARCHNEWBUF);
         }
     if(notlines) fwrite((char *)lasthit,(unsigned)(end-lasthit),sizeof(char),stdout);
    }

if(ipfh!=STDIN_HANDLE)/* MAW 10-12-99 - buggy glibc 2 does not let you close stdin */
 FILECLOSE(ipfh);
 return(0);
}

/************************************************************************/

int
offlenrex(fname,ex,begex,endex)
byte *fname;
FFS *ex;
FFS *begex;
FFS *endex;
{
 long lasthit= -1L;
 long bufoff=0L;
 byte *eoh;
 register byte *loc;
 register byte *end;
 register int nread;

 (void)begex;
 if(fname==NULL)
   ipfh=STDIN_HANDLE;
 else
    {
     ipfh=FILEOPEN((char *)fname,EPI_FREAD);
     if(ipfh==(FILE *)NULL)
         {
          fprintf(stderr,"%s: Cannot open file `%s' for reading: %s\n",
                  ProgramName, fname, TXstrerror(TXgeterror()));
          return(-1);
         }
     }

#ifdef NEVER_MSDOS   /* 11/15/89  caused prob in large mod under dos ?? */
 setbuf(ipfh,(char *)NULL);   /* no BUFFERING */
#endif
#ifdef _WIN32                       /* MAW 03-15-90 - prevent "\r\r\n" */
 setmode(fileno(stdout),O_BINARY);     /* use unxlated mode for msdos */
 setmode(fileno(ipfh),O_BINARY);      /* MAW 04-04-90 - in case stdin */
#endif

while((nread=freadex(ipfh,srchbuf,srchbufsz,endex))>0)
    {
     end=srchbuf+nread;
     for(loc=getrex(ex,srchbuf,end,SEARCHNEWBUF);
         loc;
         loc=getrex(ex,loc,end,SEARCHNEWBUF)
        )
         {
          if(lasthit!= -1L)          /* print the end of the last line */
               printf(",%ld>%s",(bufoff+(long)(loc-srchbuf))-lasthit,TX_OS_EOL_STR);
          lasthit=bufoff+(long)(loc-srchbuf);
          for(eoh=loc+rexsize(ex);loc<eoh;loc++)
              putchar(*loc);
          printf(" <%s@%ld",fname,lasthit);
         }
     bufoff+=nread;
    }

 if(lasthit!= -1L)               /* print the last end of the last line */
#ifdef _WIN32
     puts(">\r");                           /* MAW 09-08-92 binary io */
#else
     puts(">");
#endif
if(ipfh!=STDIN_HANDLE)/* MAW 10-12-99 - buggy glibc 2 does not let you close stdin */
 FILECLOSE(ipfh);
 return(0);
}
/************************************************************************/

long
countoccs(fname,ex,begex,endex)
byte *fname;
FFS *ex;
FFS *begex;
FFS *endex;
{
 long count=0L;
 register byte *loc;
 register byte *end;
 register int nread;

 (void)begex;
 if(fname==NULL)
   ipfh=STDIN_HANDLE;
 else
    {
     ipfh=FILEOPEN((char *)fname,EPI_FREAD);
     if(ipfh==(FILE *)NULL)
         {
          fprintf(stderr,"%s: Cannot open file `%s' for reading: %s\n",
                  ProgramName, fname, TXstrerror(TXgeterror()));
          return(-1L);
         }
     }

#ifdef NEVER_MSDOS   /* 11/15/89  caused prob in large mod under dos ?? */
 setbuf(ipfh,(char *)NULL);   /* no BUFFERING */
#endif
#ifdef _WIN32                       /* MAW 03-15-90 - prevent "\r\r\n" */
 setmode(fileno(stdout),O_BINARY);     /* use unxlated mode for msdos */
 setmode(fileno(ipfh),O_BINARY);      /* MAW 04-04-90 - in case stdin */
#endif


while((nread=freadex(ipfh,srchbuf,srchbufsz,endex))>0)
    {
     end=srchbuf+nread;
     for(loc=getrex(ex,srchbuf,end,SEARCHNEWBUF);
         loc;
         loc=getrex(ex,loc,end,CONTINUESEARCH)
        ) ++count;
    }
if(ipfh!=STDIN_HANDLE)/* MAW 10-12-99 - buggy glibc 2 does not let you close stdin */
 FILECLOSE(ipfh);
 return(count);
}


/************************************************************************/

int
filefind(fname,ex,begex,endex)            /* find files that contain ex */
byte *fname;
FFS *ex;
FFS *begex;
FFS *endex;
{
 register byte *end;
 register int nread;

 (void)begex;
 if(!*fname)
   ipfh=STDIN_HANDLE;
 else
    {
     ipfh=FILEOPEN((char *)fname,EPI_FREAD);
     if(ipfh==(FILE *)NULL)
         {
          fprintf(stderr,"%s: Cannot open file `%s' for reading: %s\n",
                  ProgramName, fname, TXstrerror(TXgeterror()));
          return(-1);
         }
     }
#ifdef _WIN32
 setmode(fileno(ipfh),O_BINARY);         /* use unxlated mode for msdos */
#endif

while((nread=freadex(ipfh,srchbuf,srchbufsz,endex))>0)
    {
     end=srchbuf+nread;
     if(getrex(ex,srchbuf,end,SEARCHNEWBUF)!=BPNULL)
        {
         puts((char *)fname);
if(ipfh!=STDIN_HANDLE)/* MAW 10-12-99 - buggy glibc 2 does not let you close stdin */
         FILECLOSE(ipfh);
         return(1);
        }
    }
if(ipfh!=STDIN_HANDLE)/* MAW 10-12-99 - buggy glibc 2 does not let you close stdin */
 FILECLOSE(ipfh);
 return(0);
}
/************************************************************************/

#define EORS  -255                             /* end of replace string */
#define DITTO -256                       /* dup the input to the output */
#define PHEX  -257                           /* print the output in hex */
#define NUMB  -258                                   /* number the hits */
#define SUBCP -259         /* MAW/PBR/JMT 09-09-96 - copy subexpression */
#define ENTIRE_EXPR     -260

int
parserepl(s, a, aLen)
char *s;
int *a;
size_t  aLen;
/* Parses replace string `s' into codes `a'.
 * Returns nonzero on success.
 */
{
  int   *aOrg = a;

#ifndef EPI_ENABLE_RE2
  if (Re2Enabled == -1) Re2Enabled = (getenv("EPI_ENABLE_RE2") != NULL);
#endif /* !EPI_ENABLE_RE2 */

 for(;*s;s++,a++)
    {
      if ((size_t)(a - aOrg) >= aLen) return(0);
     if(*s==EBEG)                               /* `{' */
         {
          char *t;
          for(t=s;*t && *t!=EEND;t++);          /* look for `}' */
          if(*t!=EEND)
              return(0);
          *a= -(atoi(++s));
          if(*a<=EORS)                          /* overlaps special codes */
              return(0);
          s=t;
         }
     else if(*s=='+')
         *a= NUMB;
     else if(*s=='#')
     {
         *(a++)= PHEX;
         if ((size_t)(a - aOrg) >= aLen) return(0);
         if(isdigit((int)*((byte *)s+1)))               /* MAW 09-09-96 */
         {
            s++;
            *a=atoi(s);
            if(*a==0)
               return(0);
            for(;*s && isdigit((int)*((byte *)s+1));s++) ;
         }
         else
            *a=0;
     }
     else if(*s=='?')
         *a= DITTO;
     else if(*s==EESC)                          /* `\' */
         {
          ++s;
          if(*s=='\0')
              return(0);
          else
          if(isdigit((int)*(byte *)s))                  /* MAW 09-09-96 */
          {
             *(a++) = SUBCP;
             if ((size_t)(a - aOrg) >= aLen) return(0);
             *a=atoi(s);
             if(*a==0)
                return(0);
             for(;*s && isdigit((int)*((byte *)s+1));s++) ;
          }
          else
          if(*s=='x' || *s=='X')
              {
               static const byte hex[]="0123456789abcdef";
               int hi,lo;
               byte hic,loc;
               ++s;                                 /* point past the x */
               if(*s=='\0')                          /* unexpected null */
                   return(0);
               hic=tolower((int)*(byte *)s);   /* point past the first byte */
               for(hi=0;hex[hi] && hic!=hex[hi];hi++);    /* cvt to hex */
               if(hi>=16)
                   return(0);
               ++s;
               if(*s=='\0')                          /* unexpected null */
                   return(0);
               loc=tolower((int)*(byte *)s); /* point past the 2nd hex byte */
               for(lo=0;hex[lo] && loc!=hex[lo];lo++);
               if(lo>=16)
                   return(0);
               *a=((hi<<4)|lo)&0xff;              /* convert it into a */
              }
          else if (Re2Enabled && *s == '&')
            *a = ENTIRE_EXPR;
          else *a=(int)*s;                         /* literal character */
         }
     else *a=(int)*s;                        /* just a normal character */
    }
 *a= EORS;
 return(1);
}

/************************************************************************/

int
rentmp(tfn,ofn)                              /* renames tmp to original */
char *tfn,*ofn;
{
static const char Fn[]="rentmp";
#ifdef PRIME
 delete(ofn);
 move(tfn,ofn);
#endif
#ifdef _WIN32
 if(unlink(ofn) || rename(tfn,ofn))
    return(0);
#endif
#ifdef UNIX
int rc;
char *buf;
static const char fmt[]="mv %s %s";

      buf=malloc(strlen(fmt)+strlen(tfn)+strlen(ofn)+1);
      if(buf!=NULL){
         sprintf(buf,fmt,tfn,ofn);
         system(buf);
         free(buf);
         rc=1;
      }
      else
      {
         putmsg(MERR + MAE, Fn, OutOfMem);
         rc=0;
      }
      return(rc);
#endif
#ifdef MVS
int rc=1;
char *buf;
static const char fmt[]="SELECT CMD(RENAME %s %s)";

   if(unlink(ofn)==0){
      buf=malloc(strlen(fmt)+strlen(tfn)+strlen(ofn)+1);
      if(buf!=NULL){
         sprintf(buf,fmt,tfn,ofn);
         rc=ispexec(strlen(buf),buf);
         free(buf);
      }
   }
   return(rc);
#endif                                                         /* MVS */
 return(1);
}

/************************************************************************/

int                                                  /* retns ok or !ok */
sandr(ex,rs,fn,tfn,se,ee)                         /* search and replace */
FFS *ex;                                           /* search expression */
char *rs;                                             /* replace string */
char *fn,*tfn;                                    /* file name ,temp fn */
FFS *se;                          /* starting expression (not used now) */
FFS *ee;                        /* ending expression (used for freadex) */
{
 register byte *loc;
 register byte *end;
 register int nread;
 register byte *lastwrite;
 register int i;
 byte *subhit, *esubhit;
 FILE *fh;
 FILE *ofh;
 long hitcount=0;
 int retcd=0;                                            /* return fail */
 int ra[DYNABYTE];                                     /* replace array */

 (void)se;
#ifndef EPI_ENABLE_RE2
 if (Re2Enabled == -1) Re2Enabled = (getenv("EPI_ENABLE_RE2") != NULL);
#endif /* !EPI_ENABLE_RE2 */

 if(fn==(char *)NULL)                                     /* open input */
    fh=STDIN_HANDLE;
 else if((fh=fopen(fn,EPI_FREAD))==(FILE *)NULL)
   {
     fprintf(stderr,"%s: Cannot open input file `%s': %s\n",
             ProgramName, fn, TXstrerror(TXgeterror()));
    return(0);
   }

 if(tfn==(char *)NULL)                                   /* open output */
    ofh=stdout;
 else if((ofh=fopen(tfn,EPI_FWRITE))==(FILE *)NULL)
   {
    fclose(fh);
    fprintf(stderr,"%s: Cannot open output file `%s': %s\n",
            ProgramName, tfn, TXstrerror(TXgeterror()));
    return(0);
   }

 if(!parserepl(rs, ra, TX_ARRAY_LEN(ra)))       /* parse replace array */
    {
      fprintf(stderr, "%s: Invalid replace string `%s'\n", ProgramName, rs);
     goto END;
    }

#ifdef _WIN32
 setmode(fileno(fh),O_BINARY);                   /* use unxlated mode for msdos */
 setmode(fileno(ofh),O_BINARY);                  /* use unxlated mode for msdos */
#endif

while((nread=freadex(fh,srchbuf,srchbufsz,ee))>0)
    {
     end=srchbuf+nread;
     lastwrite=srchbuf;
     for(loc=getrex(ex,srchbuf,end,SEARCHNEWBUF);
         loc;
         loc=getrex(ex,loc+rexsize(ex),end,SEARCHNEWBUF)
        )
         {
          if(fwrite(lastwrite,sizeof(char),(size_t)(loc-lastwrite),ofh)
             != (size_t)(loc - lastwrite))
              {
                fprintf(stderr, "%s: Cannot write to output file `%s': %s\n",
                        ProgramName, tfn, TXstrerror(TXgeterror()));
               goto END;
              }
          lastwrite=loc+rexsize(ex);             /* mv ptr past pattern */
          for(i=0;ra[i]!=EORS;i++)
              {
               if(ra[i]>EORS && ra[i]<0)
                   {
                    putc((loc[ -( ra[i] + 1) ] ),ofh);
                    continue;
                   }
               switch(ra[i])
                   {
                    case PHEX : i++;
                                if(ra[i]==0)
                                   fprintf(ofh,"\\X%02X",loc[i]);
                                else                  /* MAW 09-09-96 */
                                {
                                   subhit=rexshit(ex,ra[i]-1);
                                   if(subhit!=BPNULL)
                                   {
                                      esubhit=subhit+rexssize(ex,ra[i]-1);
                                      for(;subhit<esubhit;subhit++)
                                         fprintf(ofh,"\\X%02X",*subhit);
				   }
                                }
                                break;
                    case DITTO: putc(loc[i],ofh);break;
                    case NUMB : fprintf(ofh,"%ld",++hitcount);break;
                    case SUBCP: i++;
                                subhit=rexshit(ex,ra[i]-1);
                                if(subhit!=BPNULL)
                                   fwrite(subhit,1,rexssize(ex,ra[i]-1),ofh);
                                break;
                   case ENTIRE_EXPR:
                     if (Re2Enabled)
                       fwrite(rexhit(ex), 1, rexsize(ex), ofh);
                     else
                       fwrite("?", 1, 1, ofh);
                     break;
                    default   : putc(ra[i],ofh);
                   }
              }
         }
                        /* flush end of buffer */
     if (end > lastwrite &&
        fwrite(lastwrite, sizeof(char), (size_t)(end - lastwrite),ofh) !=
         (size_t)(end - lastwrite))
         {
           fprintf(stderr, "%s: Cannot write to output file `%s': %s\n",
                   ProgramName, tfn, TXstrerror(TXgeterror()));
          goto END;
         }
    }
 retcd=1;
 END:
 if(fh!=STDIN_HANDLE)   fclose(fh);
 if(ofh!=stdout) fclose(ofh);
 if(retcd && tfn!=(char *)NULL && fn!=(char *)NULL)
    if(!rentmp(tfn,fn))
       {
        fprintf(stderr,"%s: Could not move `%s' to `%s': %s\n",
                ProgramName, tfn, fn, TXstrerror(TXgeterror()));
        return(0);
       }
 return(retcd);
}

/************************************************************************/

void
xlate_exp(ex)
char *ex;
{
  FFS *fs=openrex((byte *)ex, TXrexSyntax_Rex);
 FFS *tfs, *ffs;
 int i,j,k;

 if(fs==(FFS *)NULL)
    {
     puts("The expression could not be parsed.");
     return;
    }

 ERR_IF_RE2(fs, return);

 for(ffs=firstexp(fs),tfs=ffs;tfs!=(FFS *)NULL;tfs=tfs->next)
   {
    if(tfs==ffs)
         printf("            ");
    else
         printf("\nfollowed by ");
    if(tfs==fs)        printf(">> ");
    else if(tfs->root) printf(" > ");
    else               printf("   ");
    if (tfs->patsize == 0)
      {
        if (!tfs->prev)
          {
            printf("start of buffer");
            continue;
          }
        else if (!tfs->next)
          {
            printf("end of buffer");
            continue;
          }
      }
    if (tfs->from == tfs->to)
      printf("%d occurrence%s of: ", tfs->from, (tfs->from == 1 ? " " : "s"));
    else
      printf("from %d to %d occurrences of: ", tfs->from, tfs->to);
    if(tfs->is_not) printf("NOT ");
    for(i=0;i<tfs->patsize;i++)
        {
         putchar(OSET);                         /* '['  i.e. open-set */
         for(j=0;j<DYNABYTE;j++)
             {
              if(tfs->setlist[i][j]!='\0')
                  {
                    if (j == OSET || j == CSET || j == EESC)
                      printf("%c%c", EESC, j);
                    else if (TX_ISALNUM(j) || TX_ISPUNCT(j))
                        putchar(j);
                   else printf("\\X%02X",j);
                   for(k= j+1<DYNABYTE ? j+1:j ;
                       k<DYNABYTE && tfs->setlist[i][k]!='\0';
                       k++);
                   if(--k>j+1)
                     {
                       if (k == OSET || k == CSET || k == EESC)
                         printf("%c%c", EESC, k);
                       else if (TX_ISALNUM(k) || TX_ISPUNCT(k))
                           printf("-%c",k);
                      else printf("-\\X%02X",k);
                      j=k;
                     }
                  }
             }
         putchar(CSET);                         /* ']'  i.e. close-set */
        }
   }
 putchar('\n');
 closerex(fs);
}

/************************************************************************/

#if defined(PROTECT_IT)
#  include "box.h"
#endif
int
main(int argc,char *argv[])
{
 int i=1;
 int countem=0;                                 /* count occs of an exp */
 int begdef=0;
 int enddef=0;
 int replace=0;      /* says which arg is a replace and if there is one */
 int offsetlen=0;
 int overwrite=0;                   /* says overwrite the original file */
 int findfiles=0;             /* find files that contain the expression */
 int xlate=0;                  /* translate the expression into english */
 int retcd = TXEXIT_OK;                                  /* return code */
 extern byte *srchbuf;
 char *tname="rextmp";                    /* temp file name for Replace */
 char *exfname="";                     /* file name for read expression */
 FFS *ex;                                        /* straight expression */
 FFS *begex = (FFS *)NULL;
 FFS *endex = (FFS *)NULL;
 byte   *sOrg = BYTEPN;
 static const byte def_sexp[2]={ EBOL,'\0' };   /* MAW 02-13-90 */
 static const byte def_eexp[2]={ '$' ,'\0' };   /* MAW 02-13-90 */

 trap24();                                            /* MAW 01-10-91 */
 tx_setgenericsigs(ProgramName);
#ifdef EPI_HAVE_SETLOCALE
 /* normally handled by TXsetlocale(): */
 setlocale(LC_ALL, "");
#endif /* EPI_HAVE_SETLOCALE */
#if defined(_WIN32) && defined(PROTECT_IT)
                 /* MAW 05-23-90 - for networks that cannot use dongle */
 if(argc>1 && strnicmp(argv[i]+1,"+pas",4)==0 && *(argv[i]+5)!='\0')
    {
     extern char pass3fn[], pass3file[];
     pass3fn[0]=pass3file[0]= *(argv[i]+5);
     ++i;
    }
#endif
#ifdef PROTECT_IT
 isok2run(PROT_MM3);  /* check for protection key and exit if missing */
#endif

#ifndef USELICENSE
 msgconfig(0,0,-1);
#endif
 if(argc<2)
    {
     instructem();
     retcd = TXEXIT_OK; goto EXIT;
    }
#ifdef MVS                                            /* MAW 03-13-90 */
 if(expargs(&argc,&argv)<1){
   fprintf(stderr, "%s: Cannot expand wildcards\n", ProgramName);
    retcd = TXEXIT_UNKNOWNERROR;goto EXIT;
 }
#endif

#ifdef USELICENSE
   if(!licensecheck(ProgramName,"1.0",(char *)NULL,argc,argv,ProgramName,0)) exit(255);
#endif
 for(;i<argc && *argv[i]=='-';i++)
         {
          switch(*(argv[i]+1))
              {
#ifdef MEMDEBUG
               case  'M' : mac_off(); break;
#endif
               case  'l' : findfiles=1;break;
               case  'n' : printfname=0;break;
               case  'O' : offsetlen=1;break;
               case  'p' : begishit=1;break;
               case  'P' : endishit=1;break;
               case  'S' : includesdx=1;goto SDX;
               case  's' : includesdx=0;
                           SDX:
                           if(*(argv[i]+2))
                             {
                              begdef=1;
                              begex=openrex((byte *)(argv[i]+2),
                                            TXrexSyntax_Rex);
                             }
                           break;
               case  'E' : includeedx=1;goto EDX;
               case  'e' : includeedx=0;
                           EDX:
                           if(*(argv[i]+2))
                             {
                              enddef=1;
                              endex=openrex((byte *)(argv[i]+2),
                                            TXrexSyntax_Rex);
                             }
                           break;
               case  'c' : nocntrl=1; break;
               case  'C' : countem=1; break;
               case  'R' : overwrite=1;
               case  'r' : replace=i;break;
               case  't' : tname=argv[i]+2;break;
               case  'f' : exfname=argv[i]+2;break;
               case  'x' : xlate=1;break;
               case  'v' : notlines=1;printfname=0;includesdx=0;includeedx=1;break;
               default   : fprintf(stderr,"%s: Invalid option `%s'\n",
                                   ProgramName, argv[i]);
                           retcd = TXEXIT_OK; goto EXIT;
              }
         }

 /* Bug 6105: do not assume expression was given: */
 if (i >= argc)                                 /* no expression given */
   {
     fprintf(stderr, "%s: No expression given (run without args for help)\n",
             ProgramName);
     retcd = TXEXIT_INCORRECTUSAGE; goto EXIT;
   }

if(xlate)                                        /* only do a translate */
   {
    xlate_exp(argv[i]);
    retcd = TXEXIT_OK;goto EXIT;
   }


                          /* open delimiters */

  if(!begdef) begex=openrex((byte *)def_sexp, TXrexSyntax_Rex);   /* MAW 02-13-90 */
  if(!enddef) endex=openrex((byte *)def_eexp, TXrexSyntax_Rex);   /* MAW 02-13-90 */


 if(*exfname!='\0')
    {
     FILE *exf=fopen(exfname,"r");
     byte expr[512];
     if(exf==(FILE *)NULL)
         {
          fprintf(stderr,"%s: Cannot open expression file `%s': %s\n",
                  ProgramName, exfname, TXstrerror(TXgeterror()));
          retcd = TXEXIT_CANNOTOPENINPUTFILE; goto EXIT;
         }
     fgets((char *)expr,511,exf);
     if(*expr) *(expr+strlen((char *)expr)-1)='\0';
     if((ex=openrex(sOrg = expr, TXrexSyntax_Rex))==(FFS *)NULL)
         {
          fprintf(stderr,"%s: Invalid expression file `%s'\n",
                  ProgramName, exfname);
          fclose(exf);
          retcd = TXEXIT_INCORRECTUSAGE; goto EXIT;
         }
     fclose(exf);
    }
 else if((ex=openrex(sOrg = (byte *)argv[i++], TXrexSyntax_Rex))==(FFS *)NULL)
    { retcd = TXEXIT_INCORRECTUSAGE; goto EXIT; }
 DEBUGFFS(ex);
#ifdef NOREXANCHORS /* PBR 5/15/97 */
 if(ex->patsize==0)/* MAW 11-08-96 *//* MAW 11-15-96 - move here from openrex() */
    {
     putmsg(MERR, CHARPN,
            "%s: Root expression `%.*s' cannot be empty at offset %d",
            ProgramName,
            (int)(ex->expEnd - ex->exp), ex->exp, (int)(ex->exp - sOrg));
     ex=closerex(ex);
     retcd = TXEXIT_INCORRECTUSAGE; goto EXIT;
    }
#endif
 allocsrchbuf(); /* added this for shrinking memory. MAW 06-09-93 moved it to after openrex() */

 if(srchbuf==BPNULL)
   {
     fprintf(stderr, "%s: Not enough memory for search buffer\n",
             ProgramName);
    retcd = TXEXIT_OUTOFMEMORY; goto EXIT;
   }

 if(offsetlen)
    {
     if(i<argc && *argv[i])
        {
         for(;i<argc;i++)
              offlenrex((byte *)argv[i],ex,begex,endex);
         closerex(ex);
         closerex(begex);
         closerex(endex);
        }
    }
 else
 if(countem)
    {
     long total=0L;
     long occs=0L;
     if(i<argc && *argv[i])
        {
         for(;i<argc && occs!= -1L ;i++)
             {
              occs=countoccs((byte *)argv[i],ex,begex,endex);
              printf("%s %ld%s",argv[i],occs,TX_OS_EOL_STR);
              total+=occs;
             }
         printf("total %ld%s",total,TX_OS_EOL_STR);
         closerex(ex);
         closerex(begex);
         closerex(endex);
        }
    }
 else
 if(findfiles)
    {
     if(i<argc && *argv[i])
        {
         for(;i<argc;i++)
              filefind((byte *)argv[i],ex,begex,endex);
         closerex(ex);
         closerex(begex);
         closerex(endex);
        }
    }
 else
 if(replace)
    {
     if(!overwrite)
         tname=(char *)NULL;
     if(i>=argc)
          sandr(ex,argv[replace]+2,(char *)NULL,tname,begex,endex);
     else {
           for(;i<argc;i++)
               if(sandr(ex,argv[replace]+2,argv[i],tname,begex,endex)<1)
                 {retcd=1;goto EXIT;}
          }
      closerex(ex);
      closerex(begex);
      closerex(endex);
    }
 else
    {
     if(i<argc && *argv[i])
        {
         for(;i<argc;i++)
              findlines((byte *)argv[i],ex,begex,endex);
        }
     else
        {
         findlines(NULL,ex,begex,endex);
        }
     closerex(ex);
     closerex(begex);
     closerex(endex);
    }
 retcd=0;
 EXIT:
 if(srchbuf!=NULL)
    free(srchbuf);
#ifdef MVS                                            /* MAW 03-13-90 */
 freeargs();
#endif
 mac_check(1);                                        /* MAW 07-16-90 */
 return(retcd);
}

/************************************************************************/
#endif /* TEST */
/************************************************************************/
