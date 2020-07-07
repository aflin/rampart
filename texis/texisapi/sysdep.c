#include "txcoreconfig.h"
#include <stdio.h>
#undef EPI_AES_SOURCE
#ifdef __alpha
#  define AES_SOURCE                            /* for unsetenv() prototype */
#  define EPI_AES_SOURCE
#endif
#include <stdlib.h>
#ifdef EPI_AES_SOURCE
#  undef AES_SOURCE
#endif
#include <errno.h>
#include <string.h>
#if defined(HAVE_STRINGS_H)
#  include <strings.h>                          /* for bzero() */
#endif /* _AIX */
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#ifdef linux
#  include <sys/user.h>
#  include <a.out.h>
#endif
#ifdef _WIN32
#  include <io.h>
/* this conflicts with winternl.h below: */
/* #  include <ntsecapi.h>                 /\* for NTSTATUS on x86 *\/ */
#  include <direct.h>                           /* for chdir() */
#  include <DbgHelp.h>                          /* for SYMBOL_INFO */
#endif /* _WIN32 */
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <time.h>
#if defined(EPI_HAVE_UTIME) || defined(EPI_HAVE_UTIMES) || defined(EPI_HAVE_FUTIME) || defined(EPI_HAVE_FUTIMES)
#  ifdef _WIN32
#    include <sys/utime.h>
#  else /* !_WIN32 */
#    include <sys/time.h>
#    include <utime.h>
#  endif /* !_WIN32 */
#endif /* EPI_HAVE_[F]UTIME[S] */
#include <signal.h>
#ifdef EPI_HAVE_UCONTEXT_T
#  include <ucontext.h>
#endif /* EPI_HAVE_UCONTEXT_T */
#if defined(EPI_HAVE_STATVFS) && !(defined(EPI_PREFER_STATFS) && defined(EPI_HAVE_STATFS))
#  include <sys/statvfs.h>
#elif defined(EPI_USE_SYS_STATFS_H)
#  include <sys/statfs.h>
#elif defined(EPI_USE_SYS_VFS_H)
#  include <sys/vfs.h>
#elif defined(EPI_USE_SYS_MOUNT_H)
#  include <sys/param.h>
#  include <sys/mount.h>
#endif
#if defined(hpux) || defined(__hpux)
#  include <sys/pstat.h>
#endif
#if defined(__bsdi__) || defined(__FreeBSD__) || defined(__MACH__)
#  include <sys/param.h>
#  include <sys/sysctl.h>
#endif
#if defined(HAVE_KVM_H)
#  include <paths.h>
#  include <kvm.h>
#  include <sys/user.h>
#endif
#ifdef __MACH__
#  include <mach-o/dyld.h>      /* for loading shared libs */
#  include <sys/mman.h>         /* for SHARED_DATA_REGION_SIZE etc. */
#endif /* __MACH__ */
#ifdef HAVE_MACH_TASK_INFO
#  include <mach/task_info.h>
#endif
#ifdef __sgi
#  include <invent.h>           /* Irix hardware inventory stuff */
#  include <sys/sysmp.h>        /* system call for load average etc. */
#  include <sys/syssgi.h>       /* syssgi() */
#  if defined(EPI_OS_MAJOR) && EPI_OS_MAJOR >= 6
#    include <sys/procfs.h>     /* struct prpsinfo */
#  endif
#endif
#ifdef __alpha                  /* Alpha / OSF system info stuff */
#  include <sys/sysinfo.h>
#  include <machine/hal_sysinfo.h>
#  include <sys/file.h>
#  include <paths.h>
#  include <sys/table.h>
#  include <sys/procfs.h>
#  define strcmpi strcasecmp
#endif
#if defined(sun) && defined(__SVR4)     /* Solaris */
#  include <kstat.h>
#  if !defined(_FILE_OFFSET_BITS) || _FILE_OFFSET_BITS != 64
#    include <sys/procfs.h>
#  endif
#endif
#ifdef _AIX
#  include <procinfo.h>
#  include <nlist.h>
#  include <diag/diag.h>        /* for DEFAULTOBJPATH (/etc/objrepos) */
#  undef CP                     /* clashes with api3i.h */
#endif
#ifdef _WIN32
#  ifndef NO_PSAPI
#    include <psapi.h>          /* for GetProcessMemoryInfo() */
#  endif /* !NO_PSAPI */
#  include <winnt.h>            /* for TXaccess() */
#  if defined(_MSC_VER) && _MSC_VER <= 1310
/* wtf svrapi.h is missing under MSVS 2012? */
#    include <svrapi.h>         /* for TXaccess() */
#  endif /* _MSC_VER && _MSC_VER <= 1310 */
#  include <aclapi.h>           /* for TXaccess() */
#  include <lmcons.h>           /* for UNLEN */
#  include <winternl.h>         /* for PROCESS_BASIC_INFORMATION etc. */
#  include <sddl.h>             /* for ConvertSidToStringSid() */
#  define WCHARPN       ((WCHAR *)NULL)
#else /* !_WIN32 */
#  include <sys/ipc.h>
#  include <sys/shm.h>
#  include <sys/resource.h>
#  include <sys/wait.h>
#  include <netinet/in.h>
#  include <pwd.h>
#  include <sys/param.h>                        /* for HZ */
#endif /* !_WIN32 */
#ifdef EPI_HAVE_TIOCGWINSZ
#  include <termios.h>
#  include <sys/ioctl.h>
#endif /* EPI_HAVE_TIOCGWINSZ */
#ifdef EPI_HAVE_LOCALE_H
#  include <locale.h>
#endif /* EPI_HAVE_LOCALE_H */
#if defined(EPI_HAVE_DLOPEN)
#  include <dlfcn.h>
#elif defined(EPI_HAVE_SHL_LOAD)
#  include <dl.h>
#endif
#ifdef EPI_HAVE_EXECINFO_H
#  include <execinfo.h>
#endif /* EPI_HAVE_EXECINFO_H */
#ifdef EPI_HAVE_MCHECK_H
#  include <mcheck.h>                           /* for mcheck_status */
#endif /* EPI_HAVE_MCHECK_H */
/* sparc-sun-solaris2.5.1-32-32 seems to be missing some prototypes: */
#  if defined(__sun__) && EPI_OS_MAJOR == 2 && EPI_OS_MINOR == 5
extern int utimes(char *file, struct timeval *tvp);
extern int getrusage(int who, struct rusage *rusage);
extern int getdtablesize(void);
#  endif /* solaris 2.5 */
#define TX_SYSDEP_C
#include "texint.h"
#ifdef TEST
#  ifdef EPI_HAVE_PROCESS_H
#    include <process.h>
#  endif /* EPI_HAVE_PROCESS_H */
#  include "mmsg.h"
#  ifdef EPI_HAVE_STDARG
#    include <stdarg.h>
#  else
#    include <varargs.h>
#  endif
#  ifndef _WIN32
#    include <sys/socket.h>
#    include <sys/mman.h>
#  endif /* !_WIN32 */
#endif /* TEST */
#include "dirio.h"
#include "cgi.h"                /* for htsnpf() */
#include "httpi.h"              /* wtf for HTBUF drill */
#include "authNegotiate.h"		/* for TXsaslProcessInit() */
#define TXgetTexisVersionNumString(a,b,c,d) (TXtexisver())

/* TX_PIPE_MAX could be MAXINT; pick an upper bound for buffer allocation: */
#if TX_PIPE_MAX > 8192
#  define BUF_INCR      8192
#else
#  define BUF_INCR      TX_PIPE_MAX
#endif

#undef MY_PAGE_SIZE
#if defined(PAGE_SIZE)
#  define MY_PAGE_SIZE  PAGE_SIZE
#elif defined(EPI_HAVE_GETPAGESIZE)
#  define MY_PAGE_SIZE  getpagesize()
#endif

#ifndef DIRPN
#  define DIRPN       ((DIR *)NULL)
#endif /* !DIRPN */


CONST char      TxPlatformDesc[] = EPI_PLATFORM_DESC;

static char     TxLocaleBuf[256] = "";
static size_t   TxLocaleSz = sizeof(TxLocaleBuf);       /* alloced size */
static char     *TxLocale = TxLocaleBuf;
/* WTF TxLocaleSerial moved to eng/langc.c for link resolution */
#define TX_NUM_OLD_LOCALES      4
#define TX_OLD_LOCALE_SZ        256
static char     TxOldLocaleBufs[TX_NUM_OLD_LOCALES][TX_OLD_LOCALE_SZ] =
  { "", "", "", "" };
static int      TxOldLocaleSerials[TX_NUM_OLD_LOCALES] = { 0, 0, 0, 0};
static int      TxOldLocaleNextIdx = 0;
static char     TxDecimalSepBuf[256] = "";
static size_t   TxDecimalSepSz = sizeof(TxDecimalSepBuf);
static char     *TxDecimalSep = TxDecimalSepBuf;

static const char       Ques[] = "?";
static CONST char       BinDir[] = "%BINDIR%";
static const char       SysLibPath[] = "%SYSLIBPATH%";
static CONST char       DefLibPath[] = "%EXEDIR%" PATH_DIV_S "%BINDIR%"
  PATH_DIV_S "%SYSLIBPATH%";
static char     *TxLibPath = (char *)DefLibPath;
int             TxLibPathSerial = 1;
static char	*TxEntropyPipe = CHARPN;
static CONST char       TXsigProcessNameDef[] = "Process";
static char             *TXsigProcessName = (char *)TXsigProcessNameDef;

char	*TXFeatures[] =
  {
    /* NOTE: update <vxinfo {version,release}> too if list changes WRT
     * third-party (non-Thunderstone) features:
     */
#ifdef EPI_BUILD_RE2
    "RE2",
#endif /* EPI_BUILD_RE2 */
    /* These may be removed by TXprocessInit(): */
#ifdef EPI_VORTEX_WATCHPATH
#  ifdef TX_WATCHPATH_SUPPORTED
    "watchpath",
#  endif /* TX_WATCHPATH_SUPPORTED */
#  ifdef TX_WATCHPATH_SUBTREE_SUPPORTED
    "watchpathsubtree",
#  endif /* TX_WATCHPATH_SUBTREE_SUPPORTED */
#endif /* EPI_VORTEX_WATCHPATH */
    NULL
  };

static char	TxTzName[2][64] = { "GMT", "GMT" };
static time_t	TxTzOff[2] = { 0, 0 };	/* Local = GMT + TxTzOff[tm_isdst] */
static int	TxTzDidInit = 0;

static double	TXprocessStartTime = -1.0;

const char              TXunsupportedPlatform[] = "Unsupported platform";
static CONST char               Ok[] = "ok";
static CONST char               Failed[] = "failed";
static CONST char * CONST       StdioName[TX_NUM_STDIO_HANDLES] =
{
  "stdin",
  "stdout",
  "stderr",
};

/* see texint.h; documented: */
int		TxTracePipe = 0;

/* see texint.h; documented: */
int             TxTraceLib = 0;

/* internal actual value for compiled default: */
/* KNG 20060822 changed to 10s from 250ms to try to avoid terminating
 * I/O threads; sometimes I/O thread is given no task but does not
 * have a chance to start before 250ms polite-exit-request timeout,
 * and must be terminated.  Cannot make infinite in case the thread
 * really is stuck?  Used up to 2x (see tx_prw_endiothread());
 * 2x10.0 << Vortex default timeout.
 */
#define TX_POPEN_ENDIOTIMEOUT_DEFAULT_REAL      ((double)10.0)

static double   TxPopenEndIoTimeoutDefault=TX_POPEN_ENDIOTIMEOUT_DEFAULT_REAL;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef _WIN32
/* NOTE: TXterminate... stuff associated with alarms, see below: */
/* Prefix for event that all Texis processes will wait on, to receive
 * <kill $pid SIGTERM> soft kill.  Process ID is appended:
 */
static CONST char	TXterminateEventPrefix[] = "TEXIS_TERMINATE_PID_";
/* Handle to actual event: */
static HANDLE		TXterminateEvent = NULL;
/* Callback when event is received: */
static TXTERMFUNC	*TXterminateEventHandler =
				TXgenericTerminateEventHandler;
#endif /* _WIN32 */

/* ------------------------------------------------------------------------- */

#ifndef SIGARGS
#  define SIGARGS     ARGS((int sig))
#endif  /* !SIGARGS */
#if !defined(SIGCLD) && defined(SIGCHLD)
#  define SIGCLD        SIGCHLD
#endif
#ifndef FD_SETPN
#  define FD_SETPN      ((fd_set *)NULL)
#endif
#ifndef S_ISDIR
#  define S_ISDIR(m)    ((m) & S_IFDIR)
#endif
#ifdef MSDOS
#  define S_ISUID 0
#  define S_ISGID 0
#  define S_ISVTX 0
#endif /* MSDOS */

#ifndef SHM_LOCKED
#  define SHM_LOCKED    0               /* not supported in Irix */
#endif
#ifndef SHM_INIT
#  define SHM_INIT      0               /* not supported in Linux */
#endif
#ifndef SHM_DEST
#  define SHM_DEST      0               /* WTF thunder not supported? */
#endif
#if defined(EPI_HAVE_IPC_PERM___KEY)
#  define KEYFIELD      __key
#elif defined(EPI_HAVE_IPC_PERM__KEY)
#  define KEYFIELD      _key
#elif defined(EPI_HAVE_IPC_PERM_KEY)
#  define KEYFIELD      key
#else
        error No Key Field
#endif

typedef struct TXPROC_tag
{
  struct TXPROC_tag     *next;
  PID_T                 pid;
  TXPMF                 flags;
  int                   sig, xit;
  CONST char * CONST    *xitdesclist;   /* exit code descriptions (opt.) */
  char                  *desc;          /* description */
  char                  *cmd;           /* path of command */
  TXprocExitCallback    *callback;      /* optional exit callback */
  void                  *userData;      /* optional "" user data */
}
TXPROC;
#define TXPROCPN  ((TXPROC *)NULL)

static TXPROC           *TxProcList = TXPROCPN; /* misc. forked processes */
static TXMUTEX          *TxProcMutex = TXMUTEXPN;
void                    (*TxInForkFunc) ARGS((int on)) = NULL;
static TXATOMINT        TXinForkedChild = 0;
static TXATOMINT        TxWaitDepth = 0;
static const char       TxProcNotInited[] =
  "Internal error: Process management initialization failed or not called";
static CONST char       Whitespace[] = " \t\r\n\v\f";
static CONST char       OutOfMem[] = "Out of memory";

CONST char * CONST      TxInstBinVars[TX_INSTBINVARS_NUM + 1] =
{
  "INSTALLDIR",
  "BINDIR",
  "EXEDIR",
  CHARPN
};
CONST char * CONST      TxInstBinVals[TX_INSTBINVARS_NUM + 1] =
{
  TXINSTALLPATH_VAL,
  TXREPLACEVAL_BINDIR,
  TXREPLACEVAL_EXEDIR,
  CHARPN
};

/* ---------------------------- Wrappers for DLLs -------------------------- */

CONST char *
TXgetlocale()
{
  return(TxLocale);
}

CONST char *
TXgetDecimalSep()
/* Could be empty if TXsetlocale() not called yet.
 */
{
  return(TxDecimalSep);
}

/* WTF see eng/langc.c for TXgetlocaleserial() */

/* ----------------------------------------------------------------------- */

int
tx_setenv(const char *name, const char *value)
/* Sets `name' to `value' in environment, replacing old value if present.
 * Returns 0 on error.
 */
{
#ifdef EPI_HAVE_SETENV
  return(setenv(name, value, 1) == 0);
#else /* !EPI_HAVE_SETENV */
  /* putenv()'s arg becomes part of the environment, so we cannot free it;
   * thus this memleaks, and we prefer setenv() above:
   */
  char  *s;

  s = TXstrcat3(name, "=", value);
  if (!s) return(0);
  return(putenv(s) == 0);
#endif /* !EPI_HAVE_SETENV */
}

/* ------------------------------------------------------------------------- */

int
tx_unsetenv(name)
CONST char      *name;
/* Unsets environment variable `name'.  Returns 0 on error.
 */
{
#if defined(EPI_HAVE_UNSETENV)
  unsetenv(name);
  return(1);
#elif defined(_WIN32)                    	/* putenv(VAR=) will unset */
  static CONST char     fn[] = "tx_unsetenv";
  char                  *tmp;
  int                   rc;

  errno = 0;
  if ((tmp = (char *)malloc(strlen(name) + 2)) == CHARPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      return(0);
    }
  strcpy(tmp, name);
  strcat(tmp, "=");
  errno = 0;
  rc = putenv(tmp);                             /* unset the var */
  free(tmp);
  if (rc != 0)
    {
      putmsg(MERR, fn, "Cannot unset environment var %s: %s",
             name, strerror(errno));
      return(0);
    }
  return(1);
#else /* !EPI_HAVE_UNSETENV && !_WIN32 */
  int   s, d, n;                                /* WTF drilling environ */
  char  *e;

  n = strlen(name);
  for (s = d = 0; (e = _environ[s]) != CHARPN; s++)
    if (strncmp(e, name, n) != 0 || e[n] != '=')
      _environ[d++] = e;
  _environ[d] = CHARPN;                         /* copy NULL terminator */
  return(1);
#endif /* !EPI_HAVE_UNSETENV && !_WIN32 */
}

char **
tx_mksafeenv(how)
int     how;    /* 0: char **list  1: char *buf  2: set our env */
/* Creates a "safe" environment list: strip CGI variables in case anything
 * Vortex-like is ever run (texis, scheduled jobs, etc.).  Hack wtf.
 * Can be freed directly: one malloc only.  Returns NULL on error.
 * >>>>> NOTE: if how == 2, do not free return value (it's 1 or 0)    <<<<<
 * >>>>> NOTE: returns strlst-like buf under Windows, array otherwise <<<<<
 * >>>>> NOTE: `bad' list must correspond to iscgiprog()              <<<<<
 */
{
  static CONST char             fn[] = "tx_mksafeenv";
  static CONST char * CONST     bad[] =
  {
    "SCRIPT_NAME",
    "QUERY_STRING",
    "CONTENT_LENGTH",
    "REQUEST_METHOD",
    "REMOTE_HOST",
    "REMOTE_ADDR",
    CHARPN
  };
  size_t        totsz = 0;
  char          **envp;
  size_t        n, d, e, i;
  char          *el;
  CONST char    *s;

  if (how == 2)                                 /* just modify our env */
    {
      for (i = 0; (s = bad[i]) != CHARPN; i++)
        if (!tx_unsetenv(s)) return(CHARPPN);
      return((char **)1);
    }
  if (how != 1) how = 0;

  for (n = 0; (el = _environ[n]) != CHARPN; n++)
    if (how == 1) totsz += strlen(el) + 1;

  if (how == 1)
    envp = (char **)malloc(totsz + 1);
  else
    envp = (char **)malloc((n + 1)*sizeof(char *));
  if (envp == CHARPPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      goto done;
    }
  for (n = d = 0; (el = _environ[n]) != CHARPN; n++)
    {
      e = strcspn(el, "=");
      for (i = 0; (s = bad[i]) != CHARPN; i++)
        if (strncmp(el, s, e) == 0 && s[e] == '\0') break;
      if (s == CHARPN)                          /* not a CGI var */
        {
          if (how == 1)
            {
              strcpy((char *)envp + d, el);
              d += strlen(el) + 1;
            }
          else
            envp[d++] = el;
        }
    }
  if (how == 1) ((char *)envp)[d] = '\0';
  else envp[d] = CHARPN;
done:
  return(envp);
}

char *
TXproff_t(at)
EPI_OFF_T	at;
/* Returns hex string for `at' (i.e. 0x%X printf code).  Safe even if
 * sizeof(EPI_OFF_T) > sizeof(long), or if called multiple (up to 4) times
 * before using returned static string.
 * Thread-unsafe.
 * Signal-unsafe.
 */
{
  static char   buf[(EPI_OFF_T_BITS/4 + 4)*4];
#define EOB     (buf + sizeof(buf))
  static char   *s = buf;
  char          *ret;
  int           sz, rollover = 0;

  if (at == (EPI_OFF_T)(-1)) return("-1");      /* special case */

  TX_PUSHERROR();                               /* htsnpf() may change err */
again:
  sz = htsnpf(s, EOB - s, "0x%wX", (EPI_HUGEUINT)at) + 1;
  ret = s;
  s += sz;                                      /* advance for next call */
  if (s > EOB)                                  /* truncated: try again */
    {
      s = buf;                                  /* roll over to buf start */
      if (rollover)                             /* already re-tried: error */
        {
          *(s++) = '?';
          *(s++) = '\0';
        }
      else
        {
          rollover = 1;
          goto again;
        }
    }
  if (s >= EOB || EOB - s < sz) s = buf;        /* roll over */
  TX_POPERROR();
  return(ret);
#undef EOB
}

char *
TXprkilo(buf, bufsz, sz)
char            *buf;   /* (out) buffer to write to */
size_t          bufsz;  /* (in) size of `buf' */
EPI_HUGEUINT    sz;     /* (in) number to write */
/* Pretty-prints `sz' (e.g. "100M", "10K") to `buf'.  Preserves errno.
 * Returns `buf'.
 * Thread-safe.
 */
{
  TX_PUSHERROR();                               /* may be reporting error */

#define INRANGE(sz, b)                      \
 ((EPI_HUGEUINT)(sz) >= ((EPI_HUGEUINT)1 << (b)) && \
  ((EPI_HUGEUINT)(sz) & (((EPI_HUGEUINT)1 << (b)) - (EPI_HUGEUINT)1)) == (EPI_HUGEUINT)0)

  if (sz == (EPI_HUGEUINT)(-1)) htsnpf(buf, bufsz, "-1");
  else
#if EPI_HUGEUINT_BITS > 32
       if (INRANGE(sz, 60)) htsnpf(buf, bufsz, "%kwuE", (sz >> 60));
  else if (INRANGE(sz, 50)) htsnpf(buf, bufsz, "%kwuP", (sz >> 50));
  else if (INRANGE(sz, 40)) htsnpf(buf, bufsz, "%kwuT", (sz >> 40));
  else
#endif /* EPI_HUGEUINT_BITS > 32 */
       if (INRANGE(sz, 30)) htsnpf(buf, bufsz, "%kwuG", (sz >> 30));
  else if (INRANGE(sz, 20)) htsnpf(buf, bufsz, "%kwuM", (sz >> 20));
  else if (INRANGE(sz, 10)) htsnpf(buf, bufsz, "%kwuK", (sz >> 10));
  else htsnpf(buf, bufsz, "%kwu", sz);

  TX_POPERROR();
  return(buf);
#undef INRANGE
}

int
TXparseCEscape(pmbuf, buf, bufEnd, charVal)
TXPMBUF         *pmbuf;         /* (out, opt.) for msgs */
CONST char      **buf;          /* (in/out) buffer to parse */
CONST char      *bufEnd;        /* (in, opt.) end of `*buf' */
int             *charVal;       /* (out) character value */
/* Sets `*charVal' to character value for single (after-`\') C escape
 * sequence `*buf'.  Advances `*buf' past escape (even on error); may
 * not advance it (e.g. unknown escape).  Always sets `*charVal' to
 * valid or best-guess value (even on error).  Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXparseCEscape";
  CONST char            *s = *buf, *orgS;
  char                  *e;
  int                   i, rollover;
  unsigned              n, newN;
  char                  tmp[8];

  if (bufEnd == CHARPN) bufEnd = s + strlen(s);
  if (s >= bufEnd) goto unkEsc;
  switch (*s)
    {
    case 'n':   (*buf)++;       *charVal = 10;          return(1);
    case 'r':   (*buf)++;       *charVal = 13;          return(1);
    case 't':   (*buf)++;       *charVal = 9;           return(1);
    case 'a':   (*buf)++;       *charVal = 7;           return(1);
    case 'b':   (*buf)++;       *charVal = 8;           return(1);
    case 'e':   (*buf)++;       *charVal = 27;          return(1);
    case 'f':   (*buf)++;       *charVal = 12;          return(1);
    case 'v':   (*buf)++;       *charVal = 11;          return(1);
    case '\\':  (*buf)++;       *charVal = '\\';        return(1);
    case 'x':                                   /* hex escape */
      /* K&R says no limit to number of hex digits in escape;
       * out-of-range behavior is undefined however:
       */
      for (orgS = ++s, n = 0, rollover = 0; s < bufEnd; s++)
        {
          if (*s >= 'A' && *s <= 'F')
            i = 10 + (*s - 'A');
          else if (*s >= 'a' && *s <= 'f')
            i = 10 + (*s - 'a');
          else if (*s >= '0' && *s <= '9')
            i = *s - '0';
          else
            break;
          newN = (n << 4) | (unsigned)i;
          if (newN < n) rollover = 1;
          n = newN;
        }
      if (s <= orgS)
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                         "Invalid hex escape sequence `\\%.*s'",
                         (int)(s - *buf), *buf);
          *buf = s;
          *charVal = '?';
          return(0);                            /* error */
        }
      if (n > 255 || rollover)
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                         "Out of range hex escape sequence `\\%.*s'",
                         (int)(s - *buf), *buf);
          *buf = s;
          *charVal = (n & 0xff);
          return(0);
        }
      *buf = s;
      *charVal = n;
      return(1);
    case '0':                                   /* octal escape */
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      /* K&R says max of 3 octal digits: */
      for (i = 0;
           i < 3 && s < bufEnd && *s >= '0' && *s < '8';
           i++, s++)
        tmp[i] = *s;
      tmp[i] = '\0';
      n = (unsigned)strtol(tmp, &e, 8);
      if (e == tmp || *e != '\0')
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                         "Invalid octal escape sequence `\\%.*s'",
                         (int)(s - *buf), *buf);
          *buf = s;
          *charVal = '?';
          return(0);
        }
      if (n > 255)
        {
          txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                         "Out of range octal escape sequence `\\%.*s'",
                         (int)(s - *buf), *buf);
          *buf = s;
          *charVal = (n & 0xff);
          return(0);
        }
      *buf = s;
      *charVal = n;
      return(1);
    default:
    unkEsc:
      txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                     "Unknown escape sequence `\\%.1s'",
                     (s < bufEnd ? s : ""));
      /* Do not advance `*buf': let caller print `*s' as-is later */
      *charVal = '\\';                          /* leave `\' as-is in output*/
      return(0);
    }
}

char *
TXcesc2str(s, slen, dlen)
CONST char      *s;
size_t          slen;   /* `s' length (strlen(s) if -1) */
size_t          *dlen;  /* set to returned value length */
/* Returns nul-terminated copy of `s' with C escapes translated.
 * `*dlen' set to length of returned string.  free() return value when done.
 */
{
  static CONST char     fn[] = "TXcesc2str";
  CONST char            *se;
  char                  *fmt, *d;
  int                   escVal;

  if (slen == (size_t)(-1)) slen = strlen(s);
  if ((fmt = (char *)malloc(slen + 1)) == CHARPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      return(CHARPN);
    }
  for (d = fmt, se = s + slen; s < se; d++)
    {
      if (*s != '\\')                           /* not an escape sequence */
        {
          *d = *(s++);
          continue;
        }
      s++;                                      /* skip the `\' */
      TXparseCEscape(TXPMBUFPN, &s, se, &escVal);
      *d = escVal;
    }
  *d = '\0';
  if (dlen != (size_t *)NULL) *dlen = (size_t)(d - fmt);
  return(fmt);
}

size_t
TXstrToCLiteral(char *d, size_t dlen, const char **sp, size_t slen)
/* Writes C literal string (i.e. with escapes, not quoted) for `*sp' to `d'.
 * `dlen' is size of `d'.  If `slen' is -1, strlen(*sp) assumed.
 * Returns length of literal string written (not nul-terminated);
 * if > `dlen', not written past.  Advances `*sp' past consumed chars.
 * Thread-safe.  Signal-safe.
 */
{
  const char    *sEnd, *lit;
  char          *dOrg = d, *dEnd = d + dlen;
  const char    *s = *sp;
  size_t        litLen;
  char          octalBuf[16];
  int           n;

  if (slen == (size_t)(-1)) slen = strlen(s);
  sEnd = s + slen;
  for ( ; s < sEnd; s++)
    {
      switch (*s)
        {
        case '\n':  lit = "\\n";        litLen = 2;     break;
        case '\r':  lit = "\\r";        litLen = 2;     break;
        case '\t':  lit = "\\t";        litLen = 2;     break;
        case '\f':  lit = "\\f";        litLen = 2;     break;
        case '\v':  lit = "\\v";        litLen = 2;     break;
        case '\\':  lit = "\\\\";       litLen = 2;     break;
        case '"':   lit = "\\\"";       litLen = 2;     break;
        default:
          if (*s < ' ' || *s > '~')
            {
              n = (int)(*(byte *)s);
              octalBuf[0] = '\\';
              octalBuf[1] = '0' + n/64;
              n -= (n/64)*64;
              octalBuf[2] = '0' + n/8;
              octalBuf[3] = '0' + n % 8;
              lit = octalBuf;
              litLen = 4;
            }
          else
            {
              lit = s;
              litLen = 1;
            }
          break;
        }
      if (d + litLen > dEnd) break;             /* will not fit atomically */
      memcpy(d, lit, litLen);
      d += litLen;
    }
  *sp = s;
  return(d - dOrg);
}

void
tx_hexdumpmsg(pmbuf, errnum, fn, buf, sz, flags)
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
int             errnum;
CONST char      *fn;
CONST byte      *buf;
size_t          sz;
int             flags;  /* bit 0: print offset  bit 1: center pipe */
/* Hex-dumps `sz' bytes of `buf' via txpmbuf_putmsg()s to `pmbuf'
 * (plain putmsg() if `pmbuf' is NULL).
 */
{
  static CONST char     hexch[] = "0123456789ABCDEF";
  size_t                n, off = (size_t)0, i;
#define BYTES_PER_LINE  16
#define CHAROFF         (6 + 3*BYTES_PER_LINE + 2)
  char                  *hexd, *chard;
  char                  line[CHAROFF + BYTES_PER_LINE + 1];

  while (sz > (size_t)0)
    {
      /* Note: Test Fetch in scripts checks for this format in messages:
       *   nnnn: nn ...
       */
      n = (sz < BYTES_PER_LINE ? sz : BYTES_PER_LINE);
      htsnpf(line, sizeof(line), "%04X: ", (unsigned)off);
      sz -= n;
      off += n;
      for (hexd = line + 6, chard = line + CHAROFF, i = 0; i < n; i++, buf++)
        {
          *(hexd++) = hexch[(unsigned)(*buf) >> 4];
          *(hexd++) = hexch[(unsigned)(*buf) & 0xF];
          *(hexd++) = ((flags & 2) && i == BYTES_PER_LINE/2 - 1 ? '|' : ' ');
          *(chard++) = (*buf >= (byte)' ' && *buf <= (byte)'~' ? *buf : '.');
        }
      for ( ; hexd < line + CHAROFF; hexd++) *hexd = ' ';
      *chard = '\0';
      txpmbuf_putmsg(pmbuf, errnum, fn, "%s", line + ((flags & 1) ? 0 : 6));
    }
}

/* ------------------------------------------------------------------------- */

#if (defined(__SVR4) && defined(sun)) || (defined(hpux) || defined(__hpux))
static size_t cmpmemsz ARGS((EPI_HUGEUINT numpg, EPI_HUGEUINT pgsz));
static size_t
cmpmemsz(numpg, pgsz)
EPI_HUGEUINT    numpg, pgsz;
/* Returns mem size in MB given number of pages `numpg' and page size `pgsz',
 * while trying not to cause integer overflow.
 */
{
  int   shift;

  for (shift = 20; shift > 0; shift--)
    {
      if ((numpg & (EPI_HUGEUINT)1) == (EPI_HUGEUINT)0) numpg >>= 1;
      else if ((pgsz & (EPI_HUGEUINT)1) == (EPI_HUGEUINT)0) pgsz >>= 1;
      else break;
    }
  return((size_t)((numpg*pgsz) >> shift));
}
#endif  /* need cmpmemsz() */

static size_t   TxPageSz = (size_t)(-1);
static size_t   TxMemSz = (size_t)(-1);

size_t
TXpagesize()
/* Returns page size of RAM in bytes if known, or 0 if unknown.
 */
{
#ifdef _WIN32
  SYSTEM_INFO   si;
#endif

  if (TxPageSz != (size_t)(-1)) return(TxPageSz);
#if defined(linux) && defined(MY_PAGE_SIZE)     /* Linux ------------------ */
  return(MY_PAGE_SIZE);
#elif defined(__SVR4) && defined(sun)           /* Solaris ---------------- */
  TxPageSz = (size_t)sysconf(_SC_PAGESIZE);
  if (TxPageSz == (size_t)(-1)) TxPageSz = 0;
#elif defined(_WIN32)                         /* Windows ---------------- */
  GetSystemInfo(&si);
  TxPageSz = (size_t)si.dwPageSize;
#elif defined(hpux) || defined(__hpux)          /* HPUX ------------------- */
  TXphysmem();
  if (TxPageSz == (size_t)(-1)) TxPageSz = 0;
#elif defined(EPI_HAVE_GETPAGESIZE)
  TxPageSz = (size_t)getpagesize();             /*Alpha/Irix/FreeBSD/AIX/OSX*/
#else                                           /* unknown ---------------- */
  TxPageSz = 0;
#endif
  return(TxPageSz);
}

static const char *
TXprocInfoFindToken(TXPMBUF *pmbuf, const char *procPath,
   const char *tokenName, int withColon, const char *buf, const char **valEnd)
/* Returns pointer to value of `tokenName' in `buf', or NULL if not found.
 * Sets `*valEnd' to end of value.
 * `procPath' is for messages.
 * Thread-safe.  Signal-safe.
 */
{
  static const char     fn[] = "TXprocInfoFindToken";
  const char            *s, *e;
  size_t                tokenNameLen;

  tokenNameLen = strlen(tokenName);
  for (s = buf; (s = strstri(s, tokenName)) != CHARPN; s++)
    if ((withColon ? s[tokenNameLen] == ':' : 1) &&
        (s == buf || s[-1] == '\r' || s[-1] == '\n'))
      break;
  if (s)
    {
      s += tokenNameLen + 1;                    /* +1 for `:' */
      s += strspn(s, " \t");
      e = s + strcspn(s, "\r\n");
      if (!*e) s = e = NULL;                    /* truncated value */
    }
  else
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot find token `%s' in %s",
                     tokenName, procPath);
      e = NULL;
    }
  if (valEnd) *valEnd = e;
  return(s);
}

size_t
TXgetSystemCpuTimes(TXPMBUF *pmbuf, double **times)
/* Alloc's and sets `*times' to an array of cumulative system CPU times,
 * for each CPU.
 * Returns number of CPUs, or 0 on error.
 * Thread-safe.  Signal-unsafe.
 */
{
  double                *cpus = NULL;
  size_t                numCpus = 0, numAllocedCpus = 0;
#ifdef _WIN32
  FILETIME              idleTime, kernelTime, userTime;

  /* WTF how to get info for each CPU? */
  if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "GetSystemTimes() failed: %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  if (!TX_INC_ARRAY(pmbuf, &cpus, numCpus, &numAllocedCpus))
    goto err;
#  define FILETIME_TO_SEC(ft)   (((double)((((EPI_HUGEINT)(ft).dwHighDateTime) << 32) + (EPI_HUGEINT)(ft).dwLowDateTime)) / 10000000.0)
  /* `kernelTime' includes `idleTime': */
  cpus[numCpus++] = /* FILETIME_TO_SEC(idleTime) + */
    FILETIME_TO_SEC(kernelTime) + FILETIME_TO_SEC(userTime);
#  undef FILETIME_TO_SEC
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  static const char     path[] = PATH_SEP_S "proc" PATH_SEP_S "stat";
  char                  tokenBuf[256];
  int                   fd = -1, readLen, errnum;
  char                  readBuf[8192], *e;
  const char            *val, *valEnd;
  double                dVal;

  fd = TXrawOpen(pmbuf, __FUNCTION__, NULL, path, TXrawOpenFlag_None,
                 (O_RDONLY | TX_O_BINARY), 0666);
  if (fd < 0) goto err;
  readLen = tx_rawread(pmbuf, fd, path, (byte *)readBuf,
                       sizeof(readBuf) - 1, 0x9);
  close(fd);
  fd = -1;
  if (readLen <= 0) goto err;
  readBuf[readLen] = '\0';

  for (numCpus = 0; ; numCpus++)
    {
      htsnpf(tokenBuf, sizeof(tokenBuf), "cpu%u", (int)numCpus);
      /* Do not yap if not found: could be looking too far: */
      val = TXprocInfoFindToken(TXPMBUF_SUPPRESS, path, tokenBuf, 0,
                                readBuf, &valEnd);
      if (!val) break;                          /* no more `cpuN' entries */
      /* Add up all entries on the line: */
      for (dVal = 0.0; val < valEnd; val = e)
        {
          dVal += TXstrtod(val, valEnd, &e, &errnum);
          if (errnum != 0) break;
        }
      if (!TX_INC_ARRAY(pmbuf, &cpus, numCpus, &numAllocedCpus))
        goto err;
#  ifdef HZ
      cpus[numCpus] = dVal/(double)HZ;
#  else /* !HZ */
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "HZ undefined");
      goto err;
#  endif /* !HZ */
    }
  if (numCpus == 0)
    {
      /* No `cpuN' entries.  Maybe a `cpu' entry: */
      val = TXprocInfoFindToken(pmbuf, path, tokenBuf, 0, readBuf, &valEnd);
      if (!val) goto err;
      /* Add up all entries on the line: */
      for (dVal = 0.0; val < valEnd; val = e)
        {
          dVal += TXstrtod(val, valEnd, &e, &errnum);
          if (errnum != 0) break;
        }
      if (!TX_INC_ARRAY(pmbuf, &cpus, numCpus, &numAllocedCpus))
        goto err;
#  ifdef HZ
      cpus[numCpus++] = dVal/(double)HZ;
#  else /* !HZ */
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "HZ undefined");
      goto err;
#  endif /* !HZ */
    }
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  goto finally;

err:
  cpus = TXfree(cpus);
  numCpus = numAllocedCpus = 0;
finally:
  if (times) *times = cpus;
  else TXfree(cpus);
  return(numCpus);
}

double
TXgetSystemBootTime(TXPMBUF *pmbuf)
/* Returns system boot time as time_t, or -1 if unknown.
 * Thread-safe.  Signal-safe.
 */
{
  double        bootTime;

  /* Do not cache boot time between calls: we want it to sync with
   * current system time in case latter is altered.  I.e. always ensure
   * TXgettimeofday() - TXgetSystemBootTime() == uptime.
   */
#ifdef _WIN32
  bootTime = TXgettimeofday() - ((double)GetTickCount64())/1000.0;
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  {
    static const char   path[] = PATH_SEP_S "proc" PATH_SEP_S "stat";
    int                 fd = -1, readLen, errnum;
    char                readBuf[8192], *e;
    const char          *val, *valEnd;
    double              dVal;

    fd = TXrawOpen(pmbuf, __FUNCTION__, NULL, path, TXrawOpenFlag_None,
                   (O_RDONLY | TX_O_BINARY), 0666);
    if (fd < 0) goto err;
    readLen = tx_rawread(pmbuf, fd, path, (byte *)readBuf,
                         sizeof(readBuf) - 1, 0x9);
    close(fd);
    fd = -1;
    if (readLen <= 0) goto err;
    readBuf[readLen] = '\0';
    val = TXprocInfoFindToken(pmbuf, path, "btime", 0, readBuf, &valEnd);
    if (!val) goto err;
    dVal = TXstrtod(val, valEnd, &e, &errnum);
    if (errnum == 0)
      bootTime = dVal;
    else
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Invalid btime value `%.*s' in %s",
                       (int)(valEnd - val), val, path);
        goto err;
      }
  }
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  goto finally;

#ifndef _WIN32
err:
#endif /* !_WIN32 */
  bootTime = -1.0;
finally:
  return(bootTime);
}

TXprocInfo *
TXprocInfoClose(TXprocInfo *procInfo)
/* Thread-safe.
 * Signal-unsafe (uses TXfree()).
 */
{
  if (!procInfo || !procInfo->isAlloced) return(NULL);
  procInfo->argv = TXfreeStrList(procInfo->argv, procInfo->argc);
  procInfo->cmd = TXfree(procInfo->cmd);
  procInfo->exePath = TXfree(procInfo->exePath);
  procInfo->sidUser = TXfree(procInfo->sidUser);
  procInfo = TXfree(procInfo);
  return(NULL);
}

#ifdef _WIN32
char *
TXwideCharToUtf8(TXPMBUF *pmbuf, const wchar_t *wideStr, size_t wideByteLen)
/* Converts `wideByteLen' bytes of `wideStr' to UTF-8.
 * Returns nul-terminated UTF-8 string, alloced.
 */
{
  size_t        allocSz, charLen;
  char          *ret = NULL;

  charLen = (size_t)wideByteLen/sizeof(wchar_t);
  if (charLen == 0)     /* WideCharToMultiByte() may (seem to) fail */
    allocSz = 0;
  else if (charLen >= (size_t)EPI_OS_INT_MAX)
    {
      TXseterror(ERROR_INVALID_PARAMETER);
      goto wcharCnvErr;
    }
  else
    {
      allocSz = WideCharToMultiByte(CP_UTF8, (DWORD)0, wideStr, (int)charLen,
                                    NULL, 0, CHARPN, NULL);
      if (allocSz == (size_t)0) goto wcharCnvErr;
    }
  ret = (char *)TXmalloc(pmbuf, __FUNCTION__, allocSz + 1);
  if (!ret) goto err;
  /* Second call actually converts the data: */
  if (charLen != 0)
    {
      allocSz = WideCharToMultiByte(CP_UTF8, (DWORD)0, wideStr, (int)charLen,
                                    ret, (int)allocSz, CHARPN, NULL);
      if (!allocSz)
        {
        wcharCnvErr:
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Could not convert wide string to UTF-8: %s",
                         TXstrerror(TXgeterror()));
          goto err;
        }
    }
  ret[allocSz] = '\0';
  goto finally;

err:
  ret = TXfree(ret);
finally:
  return(ret);
}

wchar_t *
TXutf8ToWideChar(TXPMBUF *pmbuf, const char *utf8Str, size_t utf8ByteLen)
/* Converts `utf8ByteLen' bytes of `utf8Str' to wide characters.
 * Returns (wide-nul)-terminated wide char string, alloced.
 */
{
  size_t        allocLen;
  wchar_t       *ret = NULL;

  if (utf8ByteLen == 0)     /* MultiByteToWideChar() may (seem to) fail? */
    allocLen = 0;
  else if (utf8ByteLen >= (size_t)EPI_OS_INT_MAX)
    {
      TXseterror(ERROR_INVALID_PARAMETER);
      goto utf8CnvErr;
    }
  else
    {
      allocLen = MultiByteToWideChar(CP_UTF8, (DWORD)0, utf8Str,
                                     (int)utf8ByteLen, NULL, 0);
      if (allocLen == (size_t)0) goto utf8CnvErr;
    }
  ret = (wchar_t *)TXmalloc(pmbuf, __FUNCTION__,
                            (allocLen + 1)*sizeof(wchar_t));
  if (!ret) goto err;
  /* Second call actually converts the data: */
  if (utf8ByteLen != 0)
    {
      allocLen = MultiByteToWideChar(CP_UTF8, (DWORD)0, utf8Str,
                                     (int)utf8ByteLen, ret, (int)allocLen);
      if (!allocLen)
        {
        utf8CnvErr:
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Could not convert UTF-8 to wide string: %s",
                         TXstrerror(TXgeterror()));
          goto err;
        }
    }
  ret[allocLen] = (wchar_t)0;
  goto finally;

err:
  ret = TXfree(ret);
finally:
  return(ret);
}


typedef LONG KPRIORITY;

/* TXSPID: an unofficial expansion of SYSTEM_PROCESS_INFORMATION: */
typedef struct TXSPID_tag
{
  ULONG                 NextEntryOffset;
  ULONG                 NumberOfThreads;
  LARGE_INTEGER         SpareLi1;
  LARGE_INTEGER         SpareLi2;
  LARGE_INTEGER         SpareLi3;
  LARGE_INTEGER         CreateTime;
  LARGE_INTEGER         UserTime;
  LARGE_INTEGER         KernelTime;
  UNICODE_STRING        ImageName;
  KPRIORITY             BasePriority;
  HANDLE                UniqueProcessId;
  ULONG                 InheritedFromUniqueProcessId;
  ULONG                 HandleCount;
  BYTE                  Reserved4[4];           /* SessionId? */
  /* from a TS_SYS_PROCESS_INFORMATION struct from the interwebs, and
   * reverse engineering with GetProcessMemoryInfo():
   */
  ULONG                 SpareUl3;
  SIZE_T                x2;
  SIZE_T                PeakVirtualSize;
  SIZE_T                VirtualSize;
  SIZE_T                PageFaultCount;
  ULONG                 PeakWorkingSetSize;
  ULONG                 x3;
  ULONG                 WorkingSetSize;
  SIZE_T                QuotaPeakPagedPoolUsage;
  SIZE_T                QuotaPagedPoolUsage;
  SIZE_T                QuotaPeakNonPagedPoolUsage;
  SIZE_T                QuotaNonPagedPoolUsage;
  SIZE_T                PagefileUsage;
  SIZE_T                PeakPagefileUsage;
  SIZE_T                PrivateUsage;
  LARGE_INTEGER         Reserved6[6];
}
TXSPID;

#  define STATUS_SUCCESS                ((NTSTATUS)0x00000000L)
#  define STATUS_INFO_LENGTH_MISMATCH   ((NTSTATUS)0xC0000004L)

typedef NTSTATUS (TXquerySysInfoFunc)(SYSTEM_INFORMATION_CLASS infoClass,
                                      PVOID info, ULONG infoLen,
                                      PULONG returnLen);
static const char       TXquerySysInfoFuncName[] = "NtQuerySystemInformation";


typedef NTSTATUS (TXqueryInfoProcFunc)(HANDLE ProcessHandle,
                           /* enum? */ DWORD ProcessInformationClass,
                                       PVOID ProcessInformation,
                                       DWORD ProcessInformationLength,
                                       PDWORD ReturnLength);
static const char   TXqueryInfoProcFuncName[] = "NtQueryInformationProcess";
static TXqueryInfoProcFunc      *TXqueryInfoProcFuncPtr = NULL;


static TXprocInfoList *
TXgetSysProcInfoDetailed(TXPMBUF *pmbuf)
/* Wrapper for NtQuerySystemInformation().
 * Returns alloc'd buffer of (some) process information for all processes.
 */
{
  static const char             fn[] = "TXgetSysProcInfoDetailed";
  static TXquerySysInfoFunc     *sysInfoFunc = NULL;
  byte                          *buf = NULL;
  size_t                        bufSz = 0;
  NTSTATUS                      res;
  ULONG                         returnLen;
  TXSPID                        *proc, *nextProc;

  /* NtQuerySystemInformation() may go away in a future OS release,
   * so look up dynamically to avoid run-time linker error:
   */
  if (!sysInfoFunc)
    {
      sysInfoFunc = (TXquerySysInfoFunc *)
        GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")),
                       TXquerySysInfoFuncName);
      if (!sysInfoFunc)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot get address of %s(): %s",
                         TXquerySysInfoFuncName, TXstrerror(TXgeterror()));
          goto err;
        }
    }

  /* Get all processes' information opaquely into `buf': */
  for (;;)
    {
      bufSz += (1 << 17);                       /* WAG */
      buf = TXfree(buf);
      if (!(buf = (byte *)TXmalloc(pmbuf, fn, bufSz))) goto err;
      res = sysInfoFunc(SystemProcessInformation, buf, (ULONG)bufSz,
                      &returnLen);
      if (res == STATUS_SUCCESS)
        {
          bufSz = (size_t)returnLen;
          break;
        }
      else if (res != STATUS_INFO_LENGTH_MISMATCH)
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "%s() failed: %s",
                         TXquerySysInfoFuncName, TXstrerror(TXgeterror()));
          goto err;
        }
    }

  /* Validate `buf': */
  for (proc = (TXSPID *)buf; proc; proc = nextProc)
    {
      /* Set `nextProc' to next process: */
      if (!proc->NextEntryOffset)               /* official end of list */
        nextProc = NULL;
      else
        {
          nextProc = (TXSPID *)((byte *)proc + proc->NextEntryOffset);
          if ((byte *)nextProc >= buf + bufSz || (byte *)nextProc < buf)
            {
              txpmbuf_putmsg(pmbuf, MWARN + MAE, fn,
                             "Truncated data returned from %s()",
                             TXquerySysInfoFuncName);
              proc->NextEntryOffset = 0;        /* terminate here */
              nextProc = NULL;
            }
        }
      proc = nextProc;
    }

  goto finally;

err:
  buf = TXfree(buf);
  bufSz = 0;
finally:
  return((TXprocInfoList *)buf);
}

int
TXqueryInformationProcess(TXPMBUF *pmbuf, HANDLE handle, int infoClass,
                          void *buf, size_t bufSz)
/* Wrapper for NtQueryInformationProcess().
 * Returns 0 on error.
 */
{
  int           ret;
  NTSTATUS      res;
  ULONG         returnLen;

  /* NtQueryInformationProcess() may go away in a future OS release,
   * so look up dynamically to avoid run-time linker error:
   */
  if (!TXqueryInfoProcFuncPtr)
    {
      TXqueryInfoProcFuncPtr = (TXqueryInfoProcFunc *)
        GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")),
                       TXqueryInfoProcFuncName);
      if (!TXqueryInfoProcFuncPtr)
        {
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Cannot get address of %s(): %s",
                         TXqueryInfoProcFuncName, TXstrerror(TXgeterror()));
          goto err;
        }
    }

  if (bufSz > (size_t)EPI_UINT32_MAX) bufSz = (size_t)EPI_UINT32_MAX;
  res = TXqueryInfoProcFuncPtr(handle, infoClass, buf, (DWORD)bufSz,
                               &returnLen);
  if (res != STATUS_SUCCESS)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
     "Cannot get PROCESS_BASIC_INFORMATION for process: status 0x%x error %s",
                     (int)res, TXstrerror(TXgeterror()));
      goto err;
    }
  if (returnLen > bufSz)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
   "Cannot get PROCESS_BASIC_INFORMATION for process: Buffer size too small");
      goto err;
    }
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

static int
TXgetUserProcParams(TXPMBUF *pmbuf, PID_T pid, char **cmdLine, char **exePath,
                    char **sidUser)
/* Wrapper for NtQueryInformationProcess() and some drilling.
 * Sets `*cmdLine' to alloc'd command line for process `pid',
 * `*exePath' to alloc'd path to executable, `*sidUser' to alloc'd user
 * SID string.
 * Returns 0 on error.
 * Thread-safe?  Signal-unsafe: uses malloc().
 */
{
  static const char             fn[] = "TXgetUserProcParams";
  TXHANDLE                      procHandle = (HANDLE)0;
  PVOID                         uppAddr;        /* in process's memory */
  UNICODE_STRING                uniStr;         /* in process's memory */
  PROCESS_BASIC_INFORMATION     pbi;
  int                           ret;
  char                          *cmd = NULL, *exe = NULL, *user = NULL;
  wchar_t                       *uniWideBuf = NULL;
  TXHANDLE                      tokenHandle = (HANDLE)0;

  procHandle = OpenProcess((PROCESS_QUERY_INFORMATION | PROCESS_VM_READ),
                           FALSE, pid);
  if (!procHandle)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot open process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Get user SID: */
  if (OpenProcessToken(procHandle, TOKEN_QUERY, &tokenHandle))
    {
      TOKEN_USER        *tokenUser;
      DWORD             resLen;
      char              buf[1024], *s;

      resLen = sizeof(buf);
      if (GetTokenInformation(tokenHandle, TokenUser, buf, sizeof(buf),
                              &resLen))
        {
          tokenUser = (TOKEN_USER *)buf;
          if (ConvertSidToStringSid(tokenUser->User.Sid, &s))
            {
              user = TXstrdup(pmbuf, fn, s);
              LocalFree(s);
              s = NULL;
            }
          else
            txpmbuf_putmsg(pmbuf, MERR, fn,
                "Cannot get process user: ConvertSidToStringSid() failed: %s",
                           TXstrerror(TXgeterror()));
        }
      else
        txpmbuf_putmsg(pmbuf, MERR, fn,
                  "Cannot get process user: GetTokenInformation() failed: %s",
                       TXstrerror(TXgeterror()));
    }
  else
    txpmbuf_putmsg(pmbuf, MERR, fn,
                   "Cannot get process user: OpenProcessToken() failed: %s",
                   TXstrerror(TXgeterror()));

  /* Could get mem info with GetProcessMemoryInfo() here, as that is
   * the proper way, but PROCESS_MEMORY_COUNTERS[_EX] lacks virtual
   * size info, so caller will copy info from TXSPID struct instead.
   */

  /* Get PEB address: */
  if (!TXqueryInformationProcess(pmbuf, procHandle, 0, &pbi,
                                 sizeof(PROCESS_BASIC_INFORMATION)))
    goto err;

  /* Read process's RTL_USER_PROCESS_PARAMETERS address,
   * from PEB.ProcessParameters in its memory, into `uppAddr':
   */
  if (!ReadProcessMemory(procHandle, (byte *)pbi.PebBaseAddress +
                         FIELD_OFFSET(PEB, ProcessParameters),
                         &uppAddr, sizeof(PVOID), NULL))
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
          "Cannot get RTL_USER_PROCESS_PARAMETERS address for process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Read process's command line UNICODE_STRING struct,
   * from RTL_USER_PROCESS_PARAMETERS.CommandLine in its memory:
   */
  if (!ReadProcessMemory(procHandle, (byte *)uppAddr +
                       FIELD_OFFSET(RTL_USER_PROCESS_PARAMETERS, CommandLine),
                         &uniStr, sizeof(uniStr), NULL))
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
"Cannot get RTL_USER_PROCESS_PARAMETERS.CommandLine struct for process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Read process's command line, from `uniStr.Buffer' in its memory: */
  uniWideBuf = (wchar_t *)TXmalloc(pmbuf, fn, uniStr.Length);
  if (!uniWideBuf) goto err;
  if (!ReadProcessMemory(procHandle, uniStr.Buffer, uniWideBuf,
                         uniStr.Length, NULL))
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Cannot read command line for process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Convert to UTF-8: */
  cmd = TXwideCharToUtf8(pmbuf, uniWideBuf, uniStr.Length);
  uniWideBuf = TXfree(uniWideBuf);

  /* Read process's image path UNICODE_STRING struct,
   * from RTL_USER_PROCESS_PARAMETERS.ImagePathName in its memory:
   */
  if (!ReadProcessMemory(procHandle, (byte *)uppAddr +
                     FIELD_OFFSET(RTL_USER_PROCESS_PARAMETERS, ImagePathName),
                         &uniStr, sizeof(uniStr), NULL))
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Cannot get RTL_USER_PROCESS_PARAMETERS.ImagePathName struct for process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Read process's image path, from `uniStr.Buffer' in its memory: */
  uniWideBuf = (wchar_t *)TXmalloc(pmbuf, fn, uniStr.Length);
  if (!uniWideBuf) goto err;
  if (!ReadProcessMemory(procHandle, uniStr.Buffer, uniWideBuf,
                         uniStr.Length, NULL))
    {
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "Cannot read image path for process %u: %s",
                     (unsigned)pid, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Convert to UTF-8: */
  exe = TXwideCharToUtf8(pmbuf, uniWideBuf, uniStr.Length);
  uniWideBuf = TXfree(uniWideBuf);

  goto finally;

err:
  cmd = TXfree(cmd);
  exe = TXfree(exe);
  user = TXfree(user);
  ret = 0;
finally:
  if (tokenHandle)
    {
      TXhandleClose(tokenHandle);
      tokenHandle = (HANDLE)0;
    }
  if (procHandle)
    {
      TXhandleClose(procHandle);
      procHandle = (HANDLE)0;
    }
  uniWideBuf = TXfree(uniWideBuf);
  if (cmdLine) *cmdLine = cmd;
  else TXfree(cmd);
  cmd = NULL;
  if (exePath) *exePath = exe;
  else TXfree(exe);
  exe = NULL;
  if (sidUser) *sidUser = user;
  else TXfree(user);
  user = NULL;
  return(ret);
}
#  undef STATUS_SUCCESS
#  undef STATUS_INFO_LENGTH_MISMATCH
#endif /* _WIN32 */

size_t
TXprocInfoListPids(TXPMBUF *pmbuf, PID_T **pids, TXprocInfoList **list)
/* Sets `*pids' to alloc'd array of all current process IDs.  Info on
 * a particular PID may be obtained with TXprocInfoByPid().
 * If `list' is non-NULL, `*list' may be set to an alloc'd opaque copy of
 * all processes' info, closed with TXprocInfoListClose(), which can be
 * passed to TXprocInfoByPid().  This is only used on some platforms.
 * Returns length of `pids', or 0 on error.
 * Thread-safe.  Signal-unsafe (malloc usage).
 */
{
  PID_T                 *pidList = NULL;
  size_t                numPids = 0, numAllocedPids = 0;
#ifdef _WIN32
  TXprocInfoList        *myList = NULL;
  TXquerySysInfoFunc    *sysInfoFunc = NULL;
  TXSPID                *proc, *nextProc;

  myList = TXgetSysProcInfoDetailed(pmbuf);
  if (!myList) goto err;

  /* Rip through `buf' and make PID list: */
  for (proc = (TXSPID *)myList; proc; proc = nextProc)
    {
      if (!TX_INC_ARRAY(pmbuf, &pidList, numPids, &numAllocedPids))
        goto err;
      pidList[numPids++] = (PID_T)proc->UniqueProcessId;
      /* Set `nextProc' to next process: */
      if (!proc->NextEntryOffset)               /* official end of list */
        nextProc = NULL;
      else
        nextProc = (TXSPID *)((byte *)proc + proc->NextEntryOffset);
      proc = nextProc;
    }
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  DIR                   *dir = NULL;
  struct dirent         *de;
  char                  *s;

  dir = opendir(PATH_SEP_S "proc");
  if (!dir) goto err;
  while ((de = readdir(dir)) != NULL)
    {
      for (s = de->d_name; *s && TX_ISDIGIT(*s); s++);
      if (s > de->d_name && !*s)                /* it's integral, thus PID */
        {
          if (!TX_INC_ARRAY(pmbuf, &pidList, numPids, &numAllocedPids))
            goto err;
          pidList[numPids++] = atoi(de->d_name);
        }
    }
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  goto finally;

err:
  pidList = TXfree(pidList);
  numPids = numAllocedPids = 0;
#ifdef _WIN32
  myList = TXfree(myList);
#endif /* _WIN32 */
finally:
#ifdef _WIN32
  if (list)
    *list = myList;
  else
    TXfree(myList);                             /* do not orphan `myList' */
  myList = NULL;
#else /* !_WIN32 */
  if (dir)
    {
      closedir(dir);
      dir = NULL;
    }
  if (list) *list = NULL;
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (pids) *pids = pidList;
  return(numPids);
}

TXprocInfoList *
TXprocInfoListClose(TXprocInfoList *procInfoList)
{
  return(TXfree(procInfoList));
}

TXprocInfo *
TXprocInfoByPid(TXPMBUF *pmbuf, TXprocInfoList *list, PID_T pid, byte *heap,
                size_t heapSz)
/* Returns info about process `pid'.  If `heap' given, object is "alloc'd"
 * from it (e.g. for use within signal handlers) and caller is responsible
 * for freeing, otherwise malloc is used and TXprocInfoClose() may be called.
 * May yap and return partially-filled out object.  If `list' given
 * (from previous TXprocInfoList() call), gets data from that snapshot.
 * Thread-safe.
 * Signal-safe if Unix, if `heap' used, and if `pmbuf' is safe.
 */
{
  static const char     fn[] = "TXprocInfoByPid";
  static const char     ellipsis[] = "...";
  TXprocInfo            *procInfo = NULL;
#ifdef _WIN32
  TXprocInfoList        *myList = NULL;
  TXquerySysInfoFunc    *sysInfoFunc = NULL;
  TXSPID                *proc, *nextProc;
  char                  *cmdLine = NULL;
#else /* !_WIN32 */
  static const char     cannotParse[] = "Cannot parse %s %s value `%.*s': %s";
  char                  procPath[256], readBuf[8192], *e = NULL;
  int                   readLen, intVal;
  long                  lVal;
  double                dVal;
  const char            *val, *valEnd, *s, *lastParen;
  int                   procFd = -1, errnum, trunc;
  size_t                argc, valSz = 0;
#endif /* !_WIN32 */
  size_t                allocSz = 0;
#define ALLOC(ptr, objType, sz)         \
  {                                     \
    allocSz = (sz);                     \
    if (heap)                           \
      {                                 \
        if (heapSz < (size_t)(sz)) goto heapErr; \
        (ptr) = (objType *)heap;        \
        heap += allocSz;                \
        heapSz -= allocSz;              \
      }                                 \
    else                                \
      {                                 \
        (ptr) = (objType *)TXmalloc(pmbuf, fn, allocSz);  \
        if (!(ptr)) goto err;           \
      }                                 \
  }

#ifdef _WIN32
  /* Some subsidiary calls malloc() parts of `procInfo'; wtf: */
  if (heap)
    {
      txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                     "Heap parameter not supported on this platform");
      goto err;
    }
#else /* !_WIN32 */
  (void)list;
#endif /* !_WIN32 */

  ALLOC(procInfo, TXprocInfo, sizeof(TXprocInfo));
  memset(procInfo, 0, sizeof(TXprocInfo));
  procInfo->isAlloced = !heap;
  procInfo->pid = pid;
  procInfo->parentPid = (PID_T)(-1);
  procInfo->uidReal = procInfo->uidEffective = (UID_T)(-1);
  procInfo->uidSaved = procInfo->uidFileSystem = (UID_T)(-1);
  procInfo->gidReal = procInfo->gidEffective = (GID_T)(-1);
  procInfo->gidSaved = procInfo->gidFileSystem = (GID_T)(-1);
  procInfo->vsz = procInfo->vszPeak = -1;
  procInfo->rss = procInfo->rssPeak = -1;
  procInfo->startTime = procInfo->userTime = procInfo->systemTime = -1.0;
  /* rest cleared by memset() */
#ifdef _WIN32 /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!list)
    {
      myList = TXgetSysProcInfoDetailed(pmbuf);
      if (!myList) goto err;
      list = myList;
    }

  /* Rip through `buf' and find `pid': */
  for (proc = (TXSPID *)list; proc; proc = nextProc)
    {
      if ((PID_T)proc->UniqueProcessId == pid) break; /* found `pid' */
      /* Set `nextProc' to next process: */
      if (!proc->NextEntryOffset)               /* official end of list */
        nextProc = NULL;
      else
        nextProc = (TXSPID *)((byte *)proc + proc->NextEntryOffset);
      proc = nextProc;
    }

  /* Get info from `proc' into `procInfo': */
  if (proc)
    {
      /* this may not really be the parent, sez the interwebs;
       * CreateToolhelp32Snapshot() can get us
       * PROCESSENTRY32.th32ParentProcessID but we are not using that API:
       */
      procInfo->parentPid = (PID_T)proc->InheritedFromUniqueProcessId;
#if 0
    wtf:
      UID_T         uidReal, uidEffective, uidSaved, uidFileSystem;
      GID_T         gidReal, gidEffective, gidSaved, gidFileSystem; /*-1 */

      char          state[32];                      /* e.g. `R (running)' */
#endif
      procInfo->cmd = TXwideCharToUtf8(pmbuf, proc->ImageName.Buffer,
                                       proc->ImageName.Length);
      TXgetUserProcParams(pmbuf, pid, &cmdLine, &procInfo->exePath,
                          &procInfo->sidUser);
      if (cmdLine)
        {
          procInfo->argv = tx_dos2cargv(cmdLine, 1);
          if (procInfo->argv)
            procInfo->argc = (int)TXcountStrList(procInfo->argv);
          cmdLine = TXfree(cmdLine);
        }

      /* Proper way to get mem info is GetProcessMemoryInfo(), but
       * that does not get us virtual sizes, so use TXSPID:
       */
      procInfo->vsz = (EPI_HUGEINT)proc->VirtualSize;
      procInfo->vszPeak = (EPI_HUGEINT)proc->PeakVirtualSize;
      procInfo->rss = (EPI_HUGEINT)proc->WorkingSetSize;
      procInfo->rssPeak = (EPI_HUGEINT)proc->PeakWorkingSetSize;

      procInfo->startTime = (double)TXfiletime2time_t(proc->CreateTime.LowPart,
                                                  proc->CreateTime.HighPart);
      procInfo->userTime = ((double)proc->UserTime.QuadPart / 10000000.0);
      procInfo->systemTime = ((double)proc->KernelTime.QuadPart / 10000000.0);
    }
  else
    {
      txpmbuf_putmsg(pmbuf, MWARN, fn, "PID %u not found", (unsigned)pid);
      goto err;
    }
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Get cmd, parent PID etc. from /proc/pid/status: */
  if (htsnpf(procPath, sizeof(procPath),
             PATH_SEP_S "proc" PATH_SEP_S "%u" PATH_SEP_S "status",
             (unsigned)pid) >= (int)sizeof(procPath))  /* should not happen */
    goto pathTooLong;
  procFd = TXrawOpen(pmbuf, __FUNCTION__, NULL, procPath, TXrawOpenFlag_None,
                     (O_RDONLY | TX_O_BINARY), 0666);
  if (procFd < 0) goto err;
  readLen = tx_rawread(pmbuf, procFd, procPath, (byte *)readBuf,
                       sizeof(readBuf) - 1, 0x9);
  if (readLen <= 0) goto cannotRead;
  readBuf[readLen] = '\0';
  close(procFd);
  procFd = -1;
  /* /proc/NNN/status format:
     Name:   bash
     State:  S (sleeping)
     Tgid:   1528
     Pid:    1528
     PPid:   1527
     ...
   */
  val = TXprocInfoFindToken(pmbuf, procPath, "Name", 1, readBuf, &valEnd);
  if (val)
    {
      ALLOC(procInfo->cmd, char, (valEnd - val) + 1);
      memcpy(procInfo->cmd, val, valEnd - val);
      procInfo->cmd[valEnd - val] = '\0';
    }

#  define GET_INT_VAL(pbuf, fld, token)  {                              \
  val = TXprocInfoFindToken(pbuf, procPath, token, 1, readBuf, &valEnd); \
  e = NULL;                                                             \
  if (val)                                                              \
    {                                                                   \
      lVal = TXstrtol(val, valEnd, &e, 0, &errnum);                     \
      if (errnum == 0)                                                  \
        procInfo->fld = lVal;                                           \
      else                                                              \
        txpmbuf_putmsg(pmbuf, MWARN, fn, cannotParse,                   \
                       procPath, token, (int)(valEnd - val), val,       \
                       TXstrerror(errnum));                             \
    }                                                                   \
  }

  GET_INT_VAL(pmbuf, parentPid, "PPid");

  val = TXprocInfoFindToken(pmbuf, procPath, "Uid", 1, readBuf, &valEnd);
  if (val)
    {
      UID_T     *ptrs[4];
      int       i;

      ptrs[0] = &procInfo->uidReal;
      ptrs[1] = &procInfo->uidEffective;
      ptrs[2] = &procInfo->uidSaved;
      ptrs[3] = &procInfo->uidFileSystem;
      for (i = 0; i < 4; i++)
        {
          intVal = TXstrtoi(val, valEnd, &e, 0, &errnum);
          if (errnum == 0)
            *(ptrs[i]) = (PID_T)intVal;
          else
            txpmbuf_putmsg(pmbuf, MWARN, fn,
                           "Cannot parse %s Uid value `%.*s': %s", procPath,
                           (int)(valEnd - val), TXstrerror(errnum));
          val = e;
        }
    }

  val = TXprocInfoFindToken(pmbuf, procPath, "Gid", 1, readBuf, &valEnd);
  if (val)
    {
      GID_T     *ptrs[4];
      int       i;

      ptrs[0] = &procInfo->gidReal;
      ptrs[1] = &procInfo->gidEffective;
      ptrs[2] = &procInfo->gidSaved;
      ptrs[3] = &procInfo->gidFileSystem;
      for (i = 0; i < 4; i++)
        {
          intVal = TXstrtoi(val, valEnd, &e, 0, &errnum);
          if (errnum == 0)
            *(ptrs[i]) = (GID_T)intVal;
          else
            txpmbuf_putmsg(pmbuf, MWARN, fn,
                           "Cannot parse %s Gid value `%.*s': %s", procPath,
                           (int)(valEnd - val), TXstrerror(errnum));
          val = e;
        }
    }

  /* VmSize/VmPeak/VmRSS/VmHWM may not exist, e.g. for threads?
   * Do not report error:
   */
  GET_INT_VAL(TXPMBUF_SUPPRESS, vsz, "VmSize");
  if (e && strnicmp(e, " kB", 3) == 0) procInfo->vsz <<= 10;
  GET_INT_VAL(TXPMBUF_SUPPRESS, vszPeak, "VmPeak");
  if (e && strnicmp(e, " kB", 3) == 0) procInfo->vszPeak <<= 10;
  GET_INT_VAL(TXPMBUF_SUPPRESS, rss, "VmRSS");
  if (e && strnicmp(e, " kB", 3) == 0) procInfo->rss <<= 10;
  GET_INT_VAL(TXPMBUF_SUPPRESS, rssPeak, "VmHWM");
  if (e && strnicmp(e, " kB", 3) == 0) procInfo->rssPeak <<= 10;

  val = TXprocInfoFindToken(pmbuf, procPath, "State", 1, readBuf, &valEnd);
  if (val)
    {
      size_t    n;

      n = TX_MIN((size_t)(valEnd - val), sizeof(procInfo->state));
      memcpy(procInfo->state, val, n);
      procInfo->state[n] = '\0';
    }

  /* Get stuff from /proc/pid/stat: */
  if (htsnpf(procPath, sizeof(procPath),
             PATH_SEP_S "proc" PATH_SEP_S "%u" PATH_SEP_S "stat",
             (unsigned)pid) >= (int)sizeof(procPath))  /* should not happen */
    goto pathTooLong;
  procFd = TXrawOpen(pmbuf, __FUNCTION__, NULL, procPath, TXrawOpenFlag_None,
                     (O_RDONLY | TX_O_BINARY), 0666);
  if (procFd < 0) goto err;
  readLen = tx_rawread(pmbuf, procFd, procPath, (byte *)readBuf,
                       sizeof(readBuf) - 1, 0x9);
  if (readLen <= 0) goto cannotRead;
  readBuf[readLen] = '\0';
  close(procFd);
  procFd = -1;

  /* Process name may contain unbalanced parens, spaces, digits etc.,
   * but rest of buffer should have no parens, so look for last close paren:
   */
  lastParen = NULL;
  for (s = readBuf; *s; s++)
    if (*s == ')') lastParen = s;
  if (lastParen)
    {
      int       i;
      size_t    len;

      s = lastParen + 1;
      for (i = 0; i < 11; i++)                  /* skip 11 fields */
        {
          s += strspn(s, Whitespace);
          len = strcspn(s, Whitespace);
          if (len == 0) break;                  /* no field */
          s += len;
        }
      if (i == 11)                              /* `s' -> utime */
        {
          dVal = TXstrtod(s, NULL, &e, &errnum);
#  ifdef HZ
          if (errnum == 0)
            procInfo->userTime = dVal/(double)HZ;
#  else /* !HZ */
          txpmbuf_putmsg(pmbuf, MERR, fn, "HZ undefined");
#  endif /* !HZ */
          s += strspn(s, Whitespace);
          len = strcspn(s, Whitespace);
          s += len;
          if (len > 0)                          /* `s' -> stime */
            {
              dVal = TXstrtod(s, NULL, &e, &errnum);
#  ifdef HZ
              if (errnum == 0)
                procInfo->systemTime = dVal/(double)HZ;
#  endif /* HZ */
              for (i = 0; i < 7; i++)           /* skip 7 fields */
                {
                  s += strspn(s, Whitespace);
                  len = strcspn(s, Whitespace);
                  if (len == 0) break;          /* no field */
                  s += len;
                }
              if (i == 7)                       /* `s' -> starttime (%llu) */
                {
                  double        bootTime;

                  bootTime = TXgetSystemBootTime(pmbuf);
                  dVal = TXstrtod(s, NULL, &e, &errnum);
#  ifdef HZ
                  if (errnum == 0 && bootTime >= 0.0)
                    procInfo->startTime = bootTime + dVal/(double)HZ;
#  endif /* HZ */
                }
            }
        }
    }

  /* Get exe path from /proc/exe: */
  if (htsnpf(procPath, sizeof(procPath),
             PATH_SEP_S "proc" PATH_SEP_S "%u" PATH_SEP_S "exe",
             (unsigned)pid) >= (int)sizeof(procPath))  /* should not happen */
    goto pathTooLong;
  readLen = readlink(procPath, readBuf, sizeof(readBuf));
  if (readLen < 0)
    txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot get exe path: %s",
                   TXstrerror(TXgeterror()));
  else if (readLen >= (int)sizeof(readBuf))
    goto pathTooLong;
  else
    {
      ALLOC(procInfo->exePath, char, readLen + 1);
      memcpy(procInfo->exePath, readBuf, readLen);
      procInfo->exePath[readLen] = '\0';
    }

  /* Get argc/argv from /proc/pid/cmdline.  Do this last, as it is
   * likely to take the most mem: do not use up `heap' prematurely:
   */
  if (htsnpf(procPath, sizeof(procPath),
             PATH_SEP_S "proc" PATH_SEP_S "%u" PATH_SEP_S "cmdline",
             (unsigned)pid) >= (int)sizeof(procPath))  /* should not happen */
    {
    pathTooLong:
      txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Path too long");
      goto err;
    }
  procFd = TXrawOpen(pmbuf, __FUNCTION__, NULL, procPath, TXrawOpenFlag_None,
                     (O_RDONLY | TX_O_BINARY), 0666);
  if (procFd < 0) goto err;
  readLen = tx_rawread(pmbuf, procFd, procPath, (byte *)readBuf,
                       sizeof(readBuf) - 1, 0x9);
  if (readLen < 0)                              /*0 ok: cmdline may be empty*/
    {
    cannotRead:
      txpmbuf_putmsg(pmbuf, MERR + FRE, fn, "Cannot read %s: %s",
                     procPath, TXstrerror(TXgeterror()));
      goto err;
    }
  readBuf[readLen] = '\0';                      /* insurance */
  close(procFd);
  procFd = -1;
  if ((size_t)readLen >= sizeof(readBuf) - 1)
    {
      txpmbuf_putmsg(pmbuf, MWARN + MAE, fn, "Command line too long");
      strcpy(readBuf + sizeof(readBuf) - sizeof(ellipsis), ellipsis);
    }
  for (argc = 0, val = readBuf; val < readBuf + readLen; val++, argc++)
    val += strlen(val);
  ALLOC(procInfo->argv, char *, (argc + 1)*sizeof(char *));
  memset(procInfo->argv, 0, (argc + 1)*sizeof(char *));
  for (procInfo->argc = 0, val = readBuf, trunc = 0;
       val < readBuf + readLen && (size_t)procInfo->argc < argc && !trunc;
       procInfo->argc++, val += valSz)
    {
      valSz = strlen(val) + 1;

      /* Command line may be long and `heap' small; do not let that
       * completely fail us:
       */
      if (heap && valSz > heapSz)               /* value too large */
        {
          txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                         "Out of heap space; truncated args");
          trunc = 1;
          valSz = heapSz;
        }
      ALLOC(procInfo->argv[procInfo->argc], char, valSz);
      memcpy(procInfo->argv[procInfo->argc], val, valSz);
      if (trunc)
        {
          if (valSz >= sizeof(ellipsis))
            strcpy(procInfo->argv[procInfo->argc] + valSz - sizeof(ellipsis),
                   ellipsis);
          else
            procInfo->argv[procInfo->argc] = (char *)ellipsis;
        }
    }
  procInfo->argv[procInfo->argc] = NULL;
#endif /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  goto finally;

heapErr:
  txpmbuf_putmsg(pmbuf, MERR + MAE, fn, "Out of heap space");
err:
  procInfo = TXprocInfoClose(procInfo);
finally:
#ifdef _WIN32
  myList = TXprocInfoListClose(myList);
  cmdLine = TXfree(cmdLine);
#else /* !_WIN32 */
  if (procFd >= 0)
    {
      close(procFd);
      procFd = -1;
    }
#endif /* !_WIN32 */
  return(procInfo);
#undef ALLOC
#undef GET_INT_VAL
}

size_t
TXprintPidInfo(char *buf, size_t bufSz, PID_T pid, PID_T *ppid)
/* Returns would-be length printed (nul-terminated iff `bufSz > 0').
 * Format:  ` (args)|[cmd] PPID nnn' with `...' suffix if short buf.
 * Sets optional `*ppid' to parent PID, if known.
 * Thread-safe.
 * Signal-safe.
 */
{
  TXprocInfo    *info;
  char          *d = buf, *e;
  int           i;
  byte          heap[8192];
#define BUF_LEFT        (d < e ? e - d : 0)

  if (!buf || bufSz <= 0) bufSz = 0;
  e = buf + bufSz;

  info = TXprocInfoByPid(TXPMBUF_SUPPRESS, NULL, pid, heap, sizeof(heap));
  if (ppid) *ppid = (info ? info->parentPid : -1);
  if (!info) goto finally;

  /* Make sure we print a PPID -- even if bogus -- if we print
   * anything at all, so that caller does not print PPID info adjacent
   * to our nothing, which is adjacent to (earlier) caller PID info:
   * would end up with PPID info next to PID.
   */
  if (info->argv && info->argc > 0)             /* args preferred */
    {
      d += htsnpf(d, BUF_LEFT, " (");
      for (i = 0; i < info->argc; i++)
        d += htsnpf(d, BUF_LEFT, "%s%s", (i > 0 ? " " : ""), info->argv[i]);
      d += htsnpf(d, BUF_LEFT, ")");
    }
  else if (info->cmd)
    d += htsnpf(d, BUF_LEFT, " [%s]", info->cmd);
  else
    d += htsnpf(d, BUF_LEFT, " ?");
  d += htsnpf(d, BUF_LEFT, " PPID %d", (int)info->parentPid);

finally:
  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (bufSz > 0) *(d < e ? d : e - 1) = '\0';   /* ensure nul-termination */
  /* no need to free `info': alloc'd on local `heap' */
  return((size_t)(d - buf));
#undef BUF_LEFT
}

/* ------------------------------------------------------------------------ */

size_t
TXprintUidAndAncestors(char *buf, size_t bufSz,
                       TX_SIG_HANDLER_SIGINFO_TYPE *sigInfo, TXtrap flags)
/* Prints UID, and PID ancestor info to `buf', from optional `sigInfo', as:
 *   ` UID nnn PID nnn (args)|[cmd] PPID nnn [(args)|[cmd] PPID nnn ...]'
 * with `...' suffix if short buf.  Prints empty string if no info to give.
 * Returns would-be length printed (nul-terminated iff `bufSz > 0').
 * Signal-safe.  Thread-safe.
 */
{
#define BUF_LEFT        (d < e ? e - d : 0)
  char  *d = buf, *e;
  int   i;

  if (!buf || bufSz <= 0) bufSz = 0;
  e = buf + bufSz;

#if defined(EPI_HAVE_SA_SIGACTION) && defined(SA_SIGINFO)
  if (sigInfo && sigInfo->si_code <= 0)
    {
      PID_T     ppid;

      d += htsnpf(d, BUF_LEFT, " UID %d PID %d", (int)sigInfo->si_uid,
                  (int)sigInfo->si_pid);
      if ((flags & (TXtrap_InfoPid | TXtrap_InfoPpid)) &&
          sigInfo->si_pid > 0)
        {
          d += TXprintPidInfo(d, BUF_LEFT, sigInfo->si_pid, &ppid);
          while ((flags & TXtrap_InfoPpid) && (int)ppid > 0)
            {
              d += TXprintPidInfo(d, BUF_LEFT, ppid, &ppid);
              if (!(flags & TXtrap_InfoPid)) break;
            }
        }
    }
#endif /* EPI_HAVE_SA_SIGACTION && SA_SIGINFO */

  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (bufSz > 0) *(d < e ? d : e - 1) = '\0';   /* ensure nul-termination */
  return((size_t)(d - buf));
#undef BUF_LEFT
}

/* ------------------------------------------------------------------------ */

size_t
TXprintSigCodeAddr(char *buf, size_t bufSz,
                   TX_SIG_HANDLER_SIGINFO_TYPE *sigInfo)
/* Prints signal code and address from `sigInfo' if possible, e.g. for ABEND.
 * Returns would-be length printed, nul-terminated if room, `...' suffix
 * if `buf' short.  Prints empty string if no info to give.  Format:
 *   `[ CODE][ addr %p]'
 * Signal-safe.  Thread-safe.
 */
{
#define BUF_LEFT        (d < e ? e - d : 0)
  char  *d = buf, *e;
  int   i;

  if (!buf || bufSz <= 0) bufSz = 0;
  e = buf + bufSz;

#if defined(EPI_HAVE_SA_SIGACTION) && defined(EPI_HAVE_SIGINFO_T)
  if (sigInfo)
    {
      const char        *codeName;

      codeName = TXsiginfoCodeName(sigInfo->si_signo, sigInfo->si_code);
      if (codeName)
        d += htsnpf(d, BUF_LEFT, " %s", codeName);
      /* else don't bother? */

      /* Print address, if appropriate: */
      switch (sigInfo->si_signo)
        {
        case SIGFPE:
        case SIGILL:
        case SIGSEGV:
        case SIGBUS:
          d += htsnpf(d, BUF_LEFT, " addr %p", sigInfo->si_addr);
          break;
        }
    }
#endif /* EPI_HAVE_SA_SIGACTION && EPI_HAVE_SIGINFO_T */

  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (bufSz > 0) *(d < e ? d : e - 1) = '\0';   /* ensure nul-termination */
  return((size_t)(d - buf));
#undef BUF_LEFT
}

/* ------------------------------------------------------------------------ */

size_t
TXphysmem()
/* Returns size of physical system memory in megabytes, or 0 if unknown.
 * KNG 971107
 */
{
  size_t        memsz = 0;
#if defined(linux)                              /* Linux ------------------ */
  char          tmp[1024];
  char          *s, *e;
  int           fd, errnum;
  EPI_HUGEUINT  sz;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  if ((fd = open(PATH_SEP_S "proc" PATH_SEP_S "meminfo", O_RDONLY, 0666)) < 0)
    goto done;
  if ((sz = read(fd, tmp, sizeof(tmp) - 1)) == (size_t)(-1)) sz = 0;
  tmp[sz] = '\0';
  s = strstri(tmp, "MemTotal:");
  if (s != CHARPN)                              /* new-style */
    {
      s += strcspn(s, Whitespace);
      s += strspn(s, Whitespace);
      sz = TXstrtouh(s, CHARPN, &e, 0, &errnum);
      if (e == s || sz == (EPI_HUGEUINT)0 || errnum != 0) goto ldone;
      s = e + strspn(e, Whitespace);
      if (*s == 'k' || *s == 'K') memsz = (size_t)(sz >> 10);
      else memsz = (size_t)(sz >> 20);
    }
  else                                          /* old-style */
    {
      s = tmp + strspn(tmp, Whitespace);
      if (strncmp(s, "total:", 6) != 0) goto ldone;
      s += strcspn(s, "0123456789");
      sz = TXstrtouh(s, CHARPN, &e, 0, &errnum);
      if (e == s || sz == (EPI_HUGEUINT)0 || errnum != 0) goto ldone;
      memsz = (size_t)(sz >> 20);
    }
ldone:
  close(fd);
#elif defined(__SVR4) && defined(sun)           /* Solaris ---------------- */
  ulong pgsz, phys;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  if ((phys = (ulong)sysconf(_SC_PHYS_PAGES)) != (ulong)(-1L) &&
      (pgsz = (ulong)(TxPageSz != (size_t)(-1) ? TxPageSz :
               sysconf(_SC_PAGESIZE))) != (ulong)(-1L))
    {
      memsz = cmpmemsz(phys, pgsz);
      TxPageSz = (size_t)phys;
    }
  else
    TxPageSz =  (size_t)0;
#elif defined(_WIN32)                         /* Windows ---------------- */
  MEMORYSTATUS  ms;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  ms.dwLength = sizeof(ms);
  GlobalMemoryStatus(&ms);                      /* wtf return code? */
  memsz = (size_t)(ms.dwTotalPhys >> 20);
#elif defined(hpux) || defined(__hpux)          /* HPUX ------------------- */
  struct pst_static     pst;
  ulong                 pgsz, phys;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  if (pstat_getstatic(&pst, sizeof(pst), (size_t)1, 0) != -1)
    {
      pgsz = (ulong)pst.page_size;
      phys = (ulong)pst.physical_memory;
      TxPageSz = (size_t)phys;
      memsz = cmpmemsz(phys, pgsz);
    }
#elif defined(__bsdi__) || defined(__FreeBSD__) || defined(__MACH__)
  int           mib[2];                /* BSD[I]/FreeBSD/OSX ----- */
  long          physmem;
  size_t        len;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  mib[0] = CTL_HW;
  mib[1] = HW_MEMSIZE;
  len = sizeof(physmem);
  if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), &physmem, &len, NULL, 0) != -1)
    memsz = (physmem >> 20);
#elif defined(__sgi)                            /* Irix ------------------- */
  struct inventory_s    *i;
#  if defined(EPI_OS_MAJOR) && EPI_OS_MAJOR >= 5 /* Irix 5+ */
#    define CLASS       inv_class
#    define TYPE        inv_type
#    define STATE       inv_state
#  else                                         /* Irix 4.0 */
#    define CLASS       class
#    define TYPE        type
#    define STATE       state
#  endif

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  for (setinvent(), i = getinvent();
       i != (struct inventory_s *)NULL;
       i = getinvent())
    {
      if (i->CLASS == INV_MEMORY && i->TYPE == INV_MAIN)
        {
          memsz = (i->STATE >> 20);
          break;
        }
    }
  endinvent();
#elif defined(__alpha)                          /* Alpha ------------------ */
  int   kbytes;

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  if (getsysinfo(GSI_PHYSMEM, (caddr_t)&kbytes, sizeof(kbytes)) == 1)
    memsz = ((size_t)kbytes >> 10);
#elif defined(_AIX)                             /* AIX -------------------- */
  /* This version divined by reverse-engineering lsattr -E -l sys0 -a realmem
   * with sctrace.  WTF OFF is a WAG:
   */
  int   fd;
  char  buf[328];
#  define OFF   26

  if (TxMemSz != (size_t)(-1)) return(TxMemSz);

  if ((fd = open(DEFAULTOBJPATH PATH_SEP_S "CuDv", O_RDONLY, 0666)) >= 0)
    {
      if (read(fd, buf, sizeof(buf)) >= sizeof(buf))
	{
          memsz = (size_t)(*((dword *)(buf + OFF)) >> 10);
	  if (memsz < 0 || memsz > 20000) memsz = 0;  /* sanity check WTF */
	}
      close(fd);
    }
#  undef OFF
#else                                           /* unknown ---------------- */
  /* leave as 0 */
#endif

#ifdef linux
done:
#endif /* linux */
  TxMemSz = memsz;
  return(memsz);
}

/* ------------------------------------------------------------------------- */

int
TXloadavg(avgs)
float   *avgs;
/* Sets 3 elements of `avgs' to 1, 5, and 15-minute load averages
 * of the system.  If partial success, unknown values set to -1.
 * Returns 0 on error.
 */
{
  int           ret;
#if defined(linux)                              /* Linux ------------------ */
  int           fd = -1, res;
  size_t        sz;
  char          tmp[128];

  fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL,
                 PATH_SEP_S "proc" PATH_SEP_S "loadavg", TXrawOpenFlag_None,
                 (O_RDONLY | TX_O_BINARY), 0666);
  if (fd < 0) goto err;
  if ((sz = read(fd, tmp, sizeof(tmp) - 1)) == (size_t)(-1)) sz = 0;
  tmp[sz] = '\0';
  res = sscanf(tmp, "%f %f %f", &avgs[0], &avgs[1], &avgs[2]);
  close(fd);
  fd = -1;
  if (res != 3) goto err;
  ret = 1;
  goto done;
#elif defined(__alpha)                          /* Alpha ------------------ */
  struct tbl_loadavg    load;
  int                   i;

  if (table(TBL_LOADAVG, 0, &load, 1, sizeof(load)) != 1) goto err;
  for (i = 0; i < 3; i++)
    avgs[i] = (load.tl_lscale == 0 ? (float)load.tl_avenrun.d[i] :
               (float)load.tl_avenrun.l[i] / (float)load.tl_lscale);
  ret = 1;
  goto done;
#elif defined(sun) && defined(__SVR4)           /* Solaris ---------------- */
#  define SCALE 256                             /* wtf how to determine? */
  static kstat_ctl_t    *kc = (kstat_ctl_t *)NULL;
  static kstat_t        *ksp = (kstat_t *)NULL;
  kstat_named_t         *kn;
  int                   i;

  if (kc == (kstat_ctl_t *)NULL &&              /* init if not yet */
      (kc = kstat_open()) == (kstat_ctl_t *)NULL)
    goto err;
  if (ksp == (kstat_t *)NULL)                   /* find load avg item */
    {
      for (ksp = kc->kc_chain; ksp != (kstat_t *)NULL; ksp = ksp->ks_next)
        {
          if (ksp->ks_type == KSTAT_TYPE_NAMED &&
              ksp->ks_name[7] == 'm' &&
              strcmp(ksp->ks_name, "system_misc") == 0)
            break;
        }
      if (ksp == (kstat_t *)NULL) goto err;
    }
  if (kstat_read(kc, ksp, NULL) == -1) goto err;
  avgs[0] = avgs[1] = avgs[2] = -1.0;
  for (i = 0, kn = (kstat_named_t *)ksp->ks_data; i < ksp->ks_ndata; i++, kn++)
    {
      if (kn->name[8] == '1' && kn->data_type == KSTAT_DATA_ULONG &&
          strcmp(kn->name, "avenrun_1min") == 0)
        avgs[0] = ((float)kn->value.ul) / (float)SCALE;
      if (kn->name[8] == '5' && kn->data_type == KSTAT_DATA_ULONG &&
          strcmp(kn->name, "avenrun_5min") == 0)
        avgs[1] = ((float)kn->value.ul) / (float)SCALE;
      if (kn->name[9] == '5' && kn->data_type == KSTAT_DATA_ULONG &&
          strcmp(kn->name, "avenrun_15min") == 0)
        avgs[2] = ((float)kn->value.ul) / (float)SCALE;
    }
  ret = (avgs[0] != -1.0 || avgs[1] != -1.0 || avgs[2] != -1.0);
  goto done;
#elif defined(__sgi)                            /* Irix ------------------- */
#  define SCALE 1024.0                          /* wtf how to determine? */
  static int    fd = -1, ldoff = -1;
  int           i;
  long          val[3];

  if (fd < 0)
    {
      fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, "/dev/kmem",
                     TXrawOpenFlag_None, O_RDONLY, 0666);
      if (fd < 0) goto err;
      tx_savefd(fd);                            /* protect at daemonize() */
    }
  if (ldoff == -1)
    {
      if ((ldoff = sysmp(MP_KERNADDR, MPKA_AVENRUN)) == -1) goto err;
      ldoff &= MAXINT;                          /* ? */
    }
  if (lseek(fd, (off_t)ldoff, SEEK_SET) != (off_t)ldoff) goto err;
  if ((i=read(fd, val, sizeof(val))) != sizeof(val)) goto err;
  for (i = 0; i < 3; i++)
    avgs[i] = ((float)val[i])/(float)SCALE;
  ret = 1;
  goto done;
#elif defined(__FreeBSD__) || defined(__MACH__) /* FreeBSD/OSX ------------ */
  double        val[3];
  int           i;

  errno = 0;
  if (getloadavg(val, 3) != 3) goto err;
  for (i = 0; i < 3; i++) avgs[i] = (float)val[i];
  ret = 1;
  goto done;
  /* see also old version in RCS */
#elif defined(hpux) || defined(__hpux)          /* HPUX ------------------- */
  struct pst_dynamic  pdyn;

  if (pstat_getdynamic(&pdyn, sizeof(pdyn), 1, 0) >= 1)
    {
      avgs[0] = (float)pdyn.psd_avg_1_min;
      avgs[1] = (float)pdyn.psd_avg_5_min;
      avgs[2] = (float)pdyn.psd_avg_15_min;
      ret = 1;
      goto done;
    }
  goto err;
#elif defined(_AIX)                             /* AIX -------------------- */
#  define SCALE (64*1024)                       /* wtf how to determine? */
  static int    fd = -1;
  static off_t  off = (off_t)(-1);
  int           i;
  struct nlist  nl[] =
  {
    { { "avenrun" } },
    { { CHARPN    } },
  };
  long          avenrun[3];

  if (fd < 0)                                   /* not opened yet */
    {
      fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, "/dev/kmem",
                     TXrawOpenFlag_None, O_RDONLY, 0666);
      if (fd < 0) goto err;
      tx_savefd(fd);                            /* protect at daemonize() */
    }
  if (off < (off_t)0)                           /* no offset yet */
    {
      if (knlist(nl, 1, sizeof nl[0]) < 0) goto err;
      if (nl[0].n_value == 0) goto err;
      off = (off_t)nl[0].n_value;
    }
  if (lseek(fd, off, SEEK_SET) == (off_t)(-1) ||
      read(fd, avenrun, sizeof(avenrun)) < sizeof(avenrun))
    goto err;
  for (i = 0; i < 3; i++)
    avgs[i] = ((float)avenrun[i])/(float)SCALE;
  ret = 1;
  goto done;
#else                                           /* unknown ---------------- */
  /* unknown OS; fall through to err */
#endif
#ifndef _WIN32
err:
#endif /* !_WIN32 */
  avgs[0] = avgs[1] = avgs[2] = -1.0;
  ret = 0;
#ifndef _WIN32
done:
#endif /* !_WIN32 */
  return(ret);
}

/* ------------------------------------------------------------------------- */

int
TXcatpath(dest, src, ext)
char            *dest;
CONST char      *src, *ext;
/* Concatenates `src' + `ext' to `dest'.  Assumes `dest' is PATH_MAX
 * chars in size.  Returns 0 on error (not enough space).
 * `dest' and `src' may be the same or overlap.
 */
{
  static CONST char     fn[] = "TXcatpath";
  size_t                sz;
  CONST char            *e;

  if ((sz = strlen(src)) + strlen(ext) >= PATH_MAX)
    {
      e = src;
      if (sz > 30) e = src + sz - 30;           /* show reasonable length */
      putmsg(MERR + MAE, fn, "Path ...%s too long", e);
      *dest = '\0';                             /* for safety */
      return(0);
    }
  else
    {
      if (src != dest) memmove(dest, src, sz);
      strcpy(dest + sz, ext);
      return(1);
    }
}

char *
TXtempnam(dir, prefix, ext)
CONST char      *dir;           /* (in) dir to use (NULL: pick one) */
CONST char      *prefix;        /* (in) prefix for name (NULL: pick one) */
CONST char      *ext;           /* (in) extension (w/.) (NULL: none) */
/* Creates a unique filename in `dir' using `prefix' and returns it in an
 * alloc buf.  Filename will have no extension.
 * Thread-safe.
 */
{
  static CONST char             fn[] = "TXtempnam";
  static CONST char * CONST     env[] =
  {
    "TMP", "TMPDIR", "TEMP", "TEMPDIR", CHARPN
  };
  static TXATOMINT              n = 0;
  CONST char * CONST            *sp;
  char                          *tmp, *d, *e;
  EPI_STAT_S                    st;
  int                           i;
#ifdef _WIN32
  char                          buf[PATH_MAX];
#endif /* _WIN32 */

  if (prefix == CHARPN) prefix = "T";
  if (ext == CHARPN) ext = "";

  /* Find an accessible directory if they did not give any dir at all.
   * If they did, assume they know what they're doing and just use it:
   */
  if (dir == CHARPN || *dir == '\0')
    {
      for (sp = env; *sp != CHARPN; sp++)
        {
          dir = getenv(*sp);
#ifdef _WIN32
          /* KNG 20041103 sometimes low-priv user gets $TMP set to
           * system temp dir, which is writable but not readable by
           * user, so user could create a temp file but not be able
           * to read or delete it.  check via TXaccess():
           */
#  define BITS  (R_OK | W_OK)
#else /* !_WIN32 */
#  define BITS  (W_OK | X_OK)
#endif /* !_WIN32 */
          if (dir != CHARPN && *dir != '\0' && TXaccess(dir, BITS) == 0)
            goto cont1;
#undef BITS
        }
      /* Pick fallback dir: */
#ifdef MSDOS
      /* Windows options:
       *   o  GetUserProfileDirectory() + Local Settings\Temp
       *      too hard-coded, and user may want temp elsewhere
       *   o  GetWindowsDirectory() + Temp
       *      too hard-coded, and should not be writing to windows dir
       *   o  c:\tmp
       *      unlikely to exist
       *   o  c:\
       *      don't want apps messing up root dir
       *   o  TXINSTALLPATH_VAL + \tmp
       *      we can ensure it exists and is writable on install.
       *      if user wants it elsewhere, set env or (future) texis.cnf option
       */
      TXstrncpy(buf, TXINSTALLPATH_VAL, sizeof(buf) - 5);
      strcat(buf, PATH_SEP_S "tmp");
      dir = buf;
#else /* !MSDOS */
      dir = "/tmp";
#endif /* !MSDOS */
    }

cont1:
  if ((tmp = (char *)malloc(strlen(dir) + strlen(prefix) + strlen(ext) +
                            EPI_OS_INT_BITS + 2)) == CHARPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      goto done;
    }
  strcpy(tmp, dir);
  d = tmp + strlen(tmp);
  if (d > tmp && d[-1] != PATH_SEP
#ifdef MSDOS
      && d[-1] != '/'
#endif
      )
    *(d++) = PATH_SEP;
  strcpy(d, prefix);
  d += strlen(d);
  sprintf(d, "%05u", (unsigned)TXgetpid(1));
  d += strlen(d);
  do
    {
      i = TX_ATOMIC_INC(&n);
      e = d;
      do
        {
          *(e++) = 'a' + (i % 26);
          i /= ('z' - 'a') + 1;
        }
      while (i > 0);
      strcpy(e, ext);
    }
  while (EPI_STAT(tmp, &st) == 0);

done:
  return(tmp);
}

/* ------------------------------------------------------------------------- */

size_t
TXfuser(pids, maxpids, path)
PID_T           *pids;          /* (out) PID array of users of `path' */
size_t          maxpids;        /* (in) size of `pids' array */
CONST char      *path;          /* (in) path of file to check */
/* Populates `pids' with PIDs of processes that currently have `path' open.
 * Returns number of `pids' elements filled in, or -1 if unable to determine.
 * WTF thread-unsafe.
 */
{
#ifdef EPI_HAVE_PROC_FS
  DIR           *dir = DIRPN, *dir2 = DIRPN;
  size_t        ret;
  EPI_STAT_S    pathst, st;
  struct dirent *de, *de2;
  PID_T         pid;
  char          *d, tmp[PATH_MAX];

  if (EPI_STAT(path, &pathst) != 0) goto err;
  strcpy(tmp, PATH_SEP_S "proc");
  d = tmp + 5;
  if ((dir = opendir(tmp)) == DIRPN) goto err;
  *(d++) = PATH_SEP;
  for (ret = 0; ret < maxpids && (de = readdir(dir)) != NULL; )
    {
      d = tmp + 6;                              /* after `/proc/' */
      TXstrncpy(d, de->d_name, tmp + PATH_MAX - d);
      d += strlen(d);
      strcpy(d, PATH_SEP_S "fd");
      d += 3;
      if ((dir2 = opendir(tmp)) == DIRPN) continue;
      *(d++) = PATH_SEP;
      pid = (PID_T)0;
      while (ret < maxpids && (de2 = readdir(dir2)) != NULL)
        {
          TXstrncpy(d, de2->d_name, tmp + PATH_MAX - d);
          if (EPI_STAT(tmp, &st) == 0 &&
              st.st_dev == pathst.st_dev &&
              st.st_ino == pathst.st_ino)
            {
              if (!pid) pid = atoi(de->d_name);
              pids[ret++] = pid;
            }
        }
      closedir(dir2);
      dir2 = DIRPN;
    }
  goto done;

err:
  ret = (size_t)(-1);
done:
  if (dir2 != DIRPN) closedir(dir2);
  if (dir != DIRPN) closedir(dir);
  return(ret);
#else /* !EPI_HAVE_PROC_FS */
  return((size_t)(-1));
#endif /* !EPI_HAVE_PROC_FS */
}

/* ------------------------------------------------------------------------- */

TXATOMINT       TxSignalDepthVar = 0;

typedef struct SIGNAME_tag
{
  int   val;
  char  *name;
}
SIGNAME;
static CONST SIGNAME	Sigs[] =
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

#ifdef EPI_HAVE_SIGINFO_T
typedef struct TXSIGINFO_tag
{
  int   signal;                                 /* -1 == any signal */
  int   code;                                   /* si_code value */
  char  *name;                                  /* SI_... etc. name */
}
TXSIGINFO;
#define TXSIGINFOPN     ((TXSIGINFO *)NULL)

static CONST TXSIGINFO  TXsiginfoCodes[] =
{
#  ifdef EPI_HAVE_SIGCODE_SI_USER
  { -1,         SI_USER,        "SI_USER"       },
#  endif /* EPI_HAVE_SIGCODE_SI_USER */
#  ifdef EPI_HAVE_SIGCODE_SI_KERNEL
  { -1,         SI_KERNEL,      "SI_KERNEL"     },
#  endif /* EPI_HAVE_SIGCODE_SI_KERNEL */
#  ifdef EPI_HAVE_SIGCODE_SI_QUEUE
  { -1,         SI_QUEUE,       "SI_QUEUE"      },
#  endif /* EPI_HAVE_SIGCODE_SI_QUEUE */
#  ifdef EPI_HAVE_SIGCODE_SI_TIMER
  { -1,         SI_TIMER,       "SI_TIMER"      },
#  endif /* EPI_HAVE_SIGCODE_SI_TIMER */
#  ifdef EPI_HAVE_SIGCODE_SI_MESGQ
  { -1,         SI_MESGQ,       "SI_MESGQ"      },
#  endif /* EPI_HAVE_SIGCODE_SI_MESGQ */
#  ifdef EPI_HAVE_SIGCODE_SI_ASYNCIO
  { -1,         SI_ASYNCIO,     "SI_ASYNCIO"    },
#  endif /* EPI_HAVE_SIGCODE_SI_ASYNCIO */
#  ifdef EPI_HAVE_SIGCODE_SI_SIGIO
  { -1,         SI_SIGIO,       "SI_SIGIO"      },
#  endif /* EPI_HAVE_SIGCODE_SI_SIGIO */
#  ifdef EPI_HAVE_SIGCODE_SI_TKILL
  { -1,         SI_TKILL,       "SI_TKILL"      },
#  endif /* EPI_HAVE_SIGCODE_SI_TKILL */
#  ifdef EPI_HAVE_SIGCODE_SI_LWP
  { -1,         SI_LWP,         "SI_LWP"        },
#  endif /* EPI_HAVE_SIGCODE_SI_LWP */
#  ifdef EPI_HAVE_SIGCODE_SI_NOINFO
  { -1,         SI_NOINFO,      "SI_NOINFO"     },
#  endif /* EPI_HAVE_SIGCODE_SI_NOINFO */
#  ifdef EPI_HAVE_SIGCODE_SI_UNDEFINED
  { -1,         SI_UNDEFINED,   "SI_UNDEFINED"  },
#  endif /* EPI_HAVE_SIGCODE_SI_UNDEFINED */
#  ifdef EPI_HAVE_SIGCODE_ILL_ILLOPC
  { SIGILL,     ILL_ILLOPC,     "ILL_ILLOPC"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_ILLOPC */
#  ifdef EPI_HAVE_SIGCODE_ILL_ILLOPN
  { SIGILL,     ILL_ILLOPN,     "ILL_ILLOPN"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_ILLOPN */
#  ifdef EPI_HAVE_SIGCODE_ILL_ILLADR
  { SIGILL,     ILL_ILLADR,     "ILL_ILLADR"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_ILLADR */
#  ifdef EPI_HAVE_SIGCODE_ILL_ILLTRP
  { SIGILL,     ILL_ILLTRP,     "ILL_ILLTRP"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_ILLTRP */
#  ifdef EPI_HAVE_SIGCODE_ILL_PRVOPC
  { SIGILL,     ILL_PRVOPC,     "ILL_PRVOPC"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_PRVOPC */
#  ifdef EPI_HAVE_SIGCODE_ILL_PRVREG
  { SIGILL,     ILL_PRVREG,     "ILL_PRVREG"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_PRVREG */
#  ifdef EPI_HAVE_SIGCODE_ILL_COPROC
  { SIGILL,     ILL_COPROC,     "ILL_COPROC"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_COPROC */
#  ifdef EPI_HAVE_SIGCODE_ILL_BADSTK
  { SIGILL,     ILL_BADSTK,     "ILL_BADSTK"    },
#  endif /* EPI_HAVE_SIGCODE_ILL_BADSTK */
#  ifdef EPI_HAVE_SIGCODE_FPE_INTDIV
  { SIGFPE,     FPE_INTDIV,     "FPE_INTDIV"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_INTDIV */
#  ifdef EPI_HAVE_SIGCODE_FPE_INTOVF
  { SIGFPE,     FPE_INTOVF,     "FPE_INTOVF"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_INTOVF */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTDIV
  { SIGFPE,     FPE_FLTDIV,     "FPE_FLTDIV"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTDIV */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTOV
  { SIGFPE,     FPE_FLTOV,      "FPE_FLTOV"     },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTOV */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTUN
  { SIGFPE,     FPE_FLTUN,      "FPE_FLTUN"     },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTUN */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTRES
  { SIGFPE,     FPE_FLTRES,     "FPE_FLTRES"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTRES */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTINV
  { SIGFPE,     FPE_FLTINV,     "FPE_FLTINV"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTINV */
#  ifdef EPI_HAVE_SIGCODE_FPE_FLTSUB
  { SIGFPE,     FPE_FLTSUB,     "FPE_FLTSUB"    },
#  endif /* EPI_HAVE_SIGCODE_FPE_FLTSUB */
#  ifdef EPI_HAVE_SIGCODE_SEGV_MAPERR
  { SIGSEGV,    SEGV_MAPERR,    "SEGV_MAPERR"   },
#  endif /* EPI_HAVE_SIGCODE_SEGV_MAPERR */
#  ifdef EPI_HAVE_SIGCODE_SEGV_ACCERR
  { SIGSEGV,    SEGV_ACCERR,    "SEGV_ACCERR"   },
#  endif /* EPI_HAVE_SIGCODE_SEGV_ACCERR */
#  ifdef EPI_HAVE_SIGCODE_BUS_ADRALN
  { SIGBUS,     BUS_ADRALN,     "BUS_ADRALN"    },
#  endif /* EPI_HAVE_SIGCODE_BUS_ADRALN */
#  ifdef EPI_HAVE_SIGCODE_BUS_ADRERR
  { SIGBUS,     BUS_ADRERR,     "BUS_ADRERR"    },
#  endif /* EPI_HAVE_SIGCODE_BUS_ADRERR */
#  ifdef EPI_HAVE_SIGCODE_BUS_OBJERR
  { SIGBUS,     BUS_OBJERR,     "BUS_OBJERR"    },
#  endif /* EPI_HAVE_SIGCODE_BUS_OBJERR */
#  ifdef EPI_HAVE_SIGCODE_TRAP_BRKPT
  { SIGTRAP,    TRAP_BRKPT,     "TRAP_BRKPT"    },
#  endif /* EPI_HAVE_SIGCODE_TRAP_BRKPT */
#  ifdef EPI_HAVE_SIGCODE_TRAP_TRACE
  { SIGTRAP,    TRAP_TRACE,     "TRAP_TRACE"    },
#  endif /* EPI_HAVE_SIGCODE_TRAP_TRACE */
#  ifdef EPI_HAVE_SIGCODE_CLD_EXITED
  { SIGCLD,     CLD_EXITED,     "CLD_EXITED"    },
#  endif /* EPI_HAVE_SIGCODE_CLD_EXITED */
#  ifdef EPI_HAVE_SIGCODE_CLD_KILLED
  { SIGCLD,     CLD_KILLED,     "CLD_KILLED"    },
#  endif /* EPI_HAVE_SIGCODE_CLD_KILLED */
#  ifdef EPI_HAVE_SIGCODE_CLD_DUMPED
  { SIGCLD,     CLD_DUMPED,     "CLD_DUMPED"    },
#  endif /* EPI_HAVE_SIGCODE_CLD_DUMPED */
#  ifdef EPI_HAVE_SIGCODE_CLD_TRAPPED
  { SIGCLD,     CLD_TRAPPED,    "CLD_TRAPPED"   },
#  endif /* EPI_HAVE_SIGCODE_CLD_TRAPPED */
#  ifdef EPI_HAVE_SIGCODE_CLD_STOPPED
  { SIGCLD,     CLD_STOPPED,    "CLD_STOPPED"   },
#  endif /* EPI_HAVE_SIGCODE_CLD_STOPPED */
#  ifdef EPI_HAVE_SIGCODE_CLD_CONTINUED
  { SIGCLD,     CLD_CONTINUED,  "CLD_CONTINUED" },
#  endif /* EPI_HAVE_SIGCODE_CLD_CONTINUED */
#  ifdef EPI_HAVE_SIGCODE_POLL_IN
  { SIGPOLL,    POLL_IN,        "POLL_IN"       },
#  endif /* EPI_HAVE_SIGCODE_POLL_IN */
#  ifdef EPI_HAVE_SIGCODE_POLL_OUT
  { SIGPOLL,    POLL_OUT,       "POLL_OUT"      },
#  endif /* EPI_HAVE_SIGCODE_POLL_OUT */
#  ifdef EPI_HAVE_SIGCODE_POLL_MSG
  { SIGPOLL,    POLL_MSG,       "POLL_MSG"      },
#  endif /* EPI_HAVE_SIGCODE_POLL_MSG */
#  ifdef EPI_HAVE_SIGCODE_POLL_ERR
  { SIGPOLL,    POLL_ERR,       "POLL_ERR"      },
#  endif /* EPI_HAVE_SIGCODE_POLL_ERR */
#  ifdef EPI_HAVE_SIGCODE_POLL_PRI
  { SIGPOLL,    POLL_PRI,       "POLL_PRI"      },
#  endif /* EPI_HAVE_SIGCODE_POLL_PRI */
#  ifdef EPI_HAVE_SIGCODE_POLL_HUP
  { SIGPOLL,    POLL_HUP,       "POLL_HUP"      },
#  endif /* EPI_HAVE_SIGCODE_POLL_HUP */
  { 0,          0,              CHARPN          }
};
#endif /* EPI_HAVE_SIGINFO_T */

#ifdef _WIN32
static CONST SIGNAME    TXctrlEvents[] =
{
  { CTRL_C_EVENT,       "Ctrl-C event"          },
  { CTRL_BREAK_EVENT,   "Ctrl-Break event"      },
  { CTRL_CLOSE_EVENT,   "Window close event"    },
  { CTRL_LOGOFF_EVENT,  "Logoff event"          },
  { CTRL_SHUTDOWN_EVENT,"Shutdown event"        },
  { 0,                  CHARPN                  },
};

CONST char *
TXctrlEventName(type)
int     type;
/* Returns string name of Windows CTRL_..._EVENT `type'.
 */
{
  CONST SIGNAME *event;

  for (event = TXctrlEvents; event->name != CHARPN; event++)
    if (event->val == type) return(event->name);
  return("Unknown event");
}
#endif /* _WIN32 */

char *
TXsignalname(sigval)
int     sigval;
/* Returns string name of given signal, or "Unknown signal" if unknown.
 */
{
  CONST SIGNAME	*sig;

  for (sig = Sigs; sig->name != CHARPN; sig++)
    if (sig->val == sigval) return(sig->name);
  return("Unknown signal");
}

int
TXsignalval(signame)
char    *signame;
/* Returns value for given signal name, or -1 if unknown.
 */
{
  CONST SIGNAME	*sig;

  for (sig = Sigs; sig->name != CHARPN; sig++)
    if (strcmpi(sig->name, signame) == 0 ||
        strcmpi(sig->name + 3, signame) == 0)
      return(sig->val);
  return(-1);
}

CONST char *
TXsiginfoCodeName(sigNum, code)
int     sigNum; /* (in) signal number (-1 if unknown) */
int     code;   /* (in) sinfo_t->si_code */
/* Returns string name of siginfo_t->si_code value `code' for `sigNum',
 * or NULL if unknown.
 * Signal-safe.  Thread-safe.
 */
{
#ifdef EPI_HAVE_SIGINFO_T
  CONST TXSIGINFO       *si;

  for (si = TXsiginfoCodes; si->name != CHARPN; si++)
    if ((si->signal < 0 || (sigNum >= 0 && si->signal == sigNum)) &&
        si->code == code)
      return(si->name);
#endif /* EPI_HAVE_SIGINFO_T */
  return(NULL);
}

size_t
TXprintRegisters(char *buf, size_t bufSz, void *si, void *ctx)
/* Prints registers to `buf', derived from sa_sigaction signal handler args
 * (or Windows exception info).  Format: `ip 0xnnn [esi 0xnnn ...]'
 * Returns would-be length printed, nul-terminated if room, `...' suffix
 * if short.
 * Thread-safe.  Signal-safe.
 */
{
#define BUF_LEFT        (d < e ? e - d : 0)
#undef EPI_REG_IP
  char  *d = buf, *e;
  int   i;

  if (!buf || bufSz <= 0) bufSz = 0;
  e = buf + bufSz;

#if defined(_WIN32)                           /* Windows ---------------- */
  {
    struct exception_pointers   *ex = (struct exception_pointers *)si;

    if (ex && ex->cxt)
      {
#  ifdef _WIN64
        d += htsnpf(d, BUF_LEFT, "ip %p", (void *)ex->cxt->Rip);
        d += htsnpf(d, BUF_LEFT, " rsi %p rdi %p rax %p rbx %p rcx %p rdx %p"
                    " rsp %p rbp %p"
                    " r8 %p r9 %p r10 %p r11 %p r12 %p r13 %p r14 %p r15 %p"
                    " eflags %p",
                    (void *)ex->cxt->Rsi,
                    (void *)ex->cxt->Rdi,
                    (void *)ex->cxt->Rax,
                    (void *)ex->cxt->Rbx,
                    (void *)ex->cxt->Rcx,
                    (void *)ex->cxt->Rdx,
                    (void *)ex->cxt->Rsp,
                    (void *)ex->cxt->Rbp,
                    (void *)ex->cxt->R8,
                    (void *)ex->cxt->R9,
                    (void *)ex->cxt->R10,
                    (void *)ex->cxt->R11,
                    (void *)ex->cxt->R12,
                    (void *)ex->cxt->R13,
                    (void *)ex->cxt->R14,
                    (void *)ex->cxt->R15,
                    (void *)ex->cxt->EFlags);
#  else /* !_WIN64 */
        d += htsnpf(d, BUF_LEFT, "ip %p", (void *)ex->cxt->Eip);
        d += htsnpf(d, BUF_LEFT, " esi %p edi %p eax %p ebx %p ecx %p edx %p"
                    " esp %p ebp %p"
                    " eflags %p",
                    (void *)ex->cxt->Esi,
                    (void *)ex->cxt->Edi,
                    (void *)ex->cxt->Eax,
                    (void *)ex->cxt->Ebx,
                    (void *)ex->cxt->Ecx,
                    (void *)ex->cxt->Edx,
                    (void *)ex->cxt->Esp,
                    (void *)ex->cxt->Ebp,
                    (void *)ex->cxt->EFlags);
#  endif /* !_WIN64 */
      }
  }
#elif defined(EPI_HAVE_UCONTEXT_T)
  {
    ucontext_t  *uc = (ucontext_t *)ctx;

    (void)si;
    /* We used to bail here if no `si' or `si->si_code' is SI_USER etc.,
     * due to perceived bad ip.  But that ip might just be system/glue
     * code, much like stack frames between handler and main code.
     * Print registers anyway; may be useful.
     */

    if (!uc) goto err;

#  if defined(EPI_HAVE_GREGS)
#    if defined(REG_EIP)                        /* x86 (Linux) ------------ */
#      define EPI_REG_IP  REG_EIP
#    elif defined(REG_RIP)                      /* x86_64 (Linux) --------- */
#      define EPI_REG_IP  REG_RIP
#    elif defined(EIP)                          /* x86 (Solaris) ---------- */
#      define EPI_REG_IP  EIP
#    elif defined(REG_PC)                       /* SPARC ------------------ */
#      define EPI_REG_IP  REG_PC
#    elif defined(CTX_EPC)                      /* MIPS (Irix) ------------ */
#      define EPI_REG_IP  CTX_EPC
#    else                                       /* unknown ---------------- */
#      define EPI_REG_IP  0
    goto err;
#    endif /* unknown `gregs' offset */
    d += htsnpf(d, BUF_LEFT, "ip %p",
                (void *)uc->uc_mcontext.gregs[EPI_REG_IP]);
#    if defined(REG_RSI)
    d += htsnpf(d, BUF_LEFT, " rsi %p rdi %p rax %p rbx %p rcx %p rdx %p"
                " rsp %p rbp %p"
#      ifdef REG_UESP
                " uesp %p"
#      endif /* REG_UESP */
                " r8 %p r9 %p r10 %p r11 %p r12 %p r13 %p r14 %p r15 %p"
                " eflags %p trapno %p err %p",
                (void *)uc->uc_mcontext.gregs[REG_RSI],
                (void *)uc->uc_mcontext.gregs[REG_RDI],
                (void *)uc->uc_mcontext.gregs[REG_RAX],
                (void *)uc->uc_mcontext.gregs[REG_RBX],
                (void *)uc->uc_mcontext.gregs[REG_RCX],
                (void *)uc->uc_mcontext.gregs[REG_RDX],
                (void *)uc->uc_mcontext.gregs[REG_RSP],
                (void *)uc->uc_mcontext.gregs[REG_RBP],
#      ifdef REG_UESP
                (void *)uc->uc_mcontext.gregs[REG_UESP],
#      endif /* REG_UESP */
                (void *)uc->uc_mcontext.gregs[REG_R8],
                (void *)uc->uc_mcontext.gregs[REG_R9],
                (void *)uc->uc_mcontext.gregs[REG_R10],
                (void *)uc->uc_mcontext.gregs[REG_R11],
                (void *)uc->uc_mcontext.gregs[REG_R12],
                (void *)uc->uc_mcontext.gregs[REG_R13],
                (void *)uc->uc_mcontext.gregs[REG_R14],
                (void *)uc->uc_mcontext.gregs[REG_R15],
                (void *)uc->uc_mcontext.gregs[REG_EFL],
                (void *)uc->uc_mcontext.gregs[REG_TRAPNO],
                (void *)uc->uc_mcontext.gregs[REG_ERR]);
#    elif defined(REG_ESI)
    d += htsnpf(d, BUF_LEFT, " esi %p edi %p eax %p ebx %p ecx %p edx %p"
                " esp %p ebp %p uesp %p"
                " eflags %p trapno %p err %p",
                (void *)uc->uc_mcontext.gregs[REG_ESI],
                (void *)uc->uc_mcontext.gregs[REG_EDI],
                (void *)uc->uc_mcontext.gregs[REG_EAX],
                (void *)uc->uc_mcontext.gregs[REG_EBX],
                (void *)uc->uc_mcontext.gregs[REG_ECX],
                (void *)uc->uc_mcontext.gregs[REG_EDX],
                (void *)uc->uc_mcontext.gregs[REG_ESP],
                (void *)uc->uc_mcontext.gregs[REG_EBP],
                (void *)uc->uc_mcontext.gregs[REG_UESP],
                (void *)uc->uc_mcontext.gregs[REG_EFL],
                (void *)uc->uc_mcontext.gregs[REG_TRAPNO],
                (void *)uc->uc_mcontext.gregs[REG_ERR]);
#  endif
#  elif defined(__ia64)                         /* Itanium ---------------- */
    ip = (void *)uc->uc_mcontext.sc_ip;
#  elif defined(__FreeBSD__) && defined(__i386) /* i386 (BSD) ------------- */
    ip = (void *)uc->uc_mcontext.mc_eip;
#  elif defined(hpux) || defined(__hpux)        /* HPUX ------------------- */
    /* WTF `ss_pcoq_head' is a SWAG; gdb reports ~3 bytes less: */
    /* WTF only tested on HPUX 10: */
#    ifndef __LP64__
    if (!UseWideRegs(&uc->uc_mcontext))
      ip = (void *)uc->uc_mcontext.ss_narrow.ss_pcoq_head;
    else
#    endif /* !__LP64__ */
      ip = (void *)uc->uc_mcontext.ss_wide.ss_64.ss_pcoq_head;
#  elif defined(__ppc__)                        /* PowerPC (Darwin OS/X) -- */
    if (uc->uc_mcontext)
      {
        ip = (void *)uc->uc_mcontext->ss.srr0;
      }
#  elif defined(_POWER)                         /* PowerPC (AIX 4.3) ------ */
    ip = (void *)uc->uc_mcontext.jmp_context.iar;
#  elif defined(__alpha)                        /* Alpha ------------------ */
    /* WTF uc->uc_mcontext.{sc_fp_trigger_inst,sc_fp_trap_pc} inaccurate */
#  endif
  }

err:
#endif /* EPI_HAVE_UCONTEXT_H */

  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (bufSz > 0) *(d < e ? d : e - 1) = '\0';   /* ensure nul-termination */
  return((size_t)(d - buf));
#undef BUF_LEFT
#undef EPI_REG_IP
}

TX_SIG_HANDLER *
TXcatchSignal(sigNum, handler)
int             sigNum;
TX_SIG_HANDLER  *handler;
/* Returns previous handler, which should have been set with TXcatchSignal()
 * (if set at all).
 */
{
#ifdef EPI_HAVE_SIGACTION
  struct sigaction      act, prev;

  memset(&act, 0, sizeof(struct sigaction));
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, sigNum);
  if ((void *)handler == (void *)SIG_DFL ||
      (void *)handler == (void *)SIG_IGN)
    {
      /* Technically SIG_IGN/SIG_DFL are only valid for sa.sa_handler,
       * not sa.sa_sigaction, even though both are usually union-overlapped:
       */
#  ifdef SA_SIGINFO
      act.sa_flags &= ~SA_SIGINFO;
#  endif /* SA_SIGINFO */
      act.sa_handler = (void (*)(int))handler;
    }
  else
    TX_SIG_HANDLER_SET(&act, handler);
  sigaction(sigNum, &act, &prev);
  return(
#  ifdef SA_SIGINFO
         (prev.sa_flags & SA_SIGINFO) ? prev.sa_sigaction :
#  endif /* SA_SIGINFO */
         (TX_SIG_HANDLER *)prev.sa_handler);
#else /* !EPI_HAVE_SIGACTION */
  return(signal(sigNum, handler));
#endif /* !EPI_HAVE_SIGACTION */
}

static void tx_catchgenericsig ARGS((int sig));
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

void
TXmkabend(void)
/* Intentional ABEND.  Separate function because we often seem to lose
 * the top item off the stack in gdb.
 */
{
  *((volatile char *)NULL) = 0;
}

SIGTYPE CDECL
tx_genericsighandler TX_SIG_HANDLER_ARGS
/* Handles SIGTERM, SIGINT, SIGSEGV etc. for programs that don't want to.
 */
{
  int           xitCode = TXEXIT_ABEND;
  int           msgNumAddend = TX_PUTMSG_NUM_IN_SIGNAL;
  const char    *by = "";
  TXtrap        trap = (TXApp ? TXApp->trap : TXtrap_Default);
  size_t        lenPrinted;
  TXbool        didBacktrace = TXbool_False;
  char          *infoCur, *infoEnd, *infoPrev, *infoSkipFrom, infoBuf[8192];
  char          pidBuf[64], versionNumBuf[128];

#ifndef _WIN32
  (void)context;
#endif /* !_WIN32 */
#if defined(_WIN32) && defined(_MSC_VER)
__try
{
#endif /* _WIN32 && _MSC_VER */
  infoCur = infoSkipFrom = infoBuf;
  infoEnd = infoBuf + sizeof(infoBuf);
  *infoCur = '\0';
#define INFO_BUF_LEFT   (infoCur < infoEnd ? infoEnd - infoCur : 0)

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
  if (TXentersignal())                  /* previous signal is being handled */
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

  if (trap & (TXtrap_CoreViaNull | TXtrap_CoreViaReturn))
    {
      TXcatchSignal(sigNum, (TX_SIG_HANDLER*)SIG_DFL);  /* core next time */
#ifdef SIGSEGV
      if (sigNum != SIGSEGV)                    /* *CHARPN below */
        TXcatchSignal(SIGSEGV, (TX_SIG_HANDLER *)SIG_DFL);
#endif /* SIGSEGV */
    }
  else                                          /* might SEGV again */
    /* Only needed if no sigaction() available, since tx_catchgenericsig()
     * does not set SA_RESETHAND.  But do it just in case:
     */
    tx_catchgenericsig(sigNum);                   /* might SEGV again */

  /* Note: `infoSkipFrom'/`by' assumes this is first part of `infoBuf': */
  infoPrev = infoCur;
  infoCur += htsnpf(infoCur, INFO_BUF_LEFT, " from");
  infoSkipFrom = infoCur;
  by = " by";
  lenPrinted = TXprintUidAndAncestors(infoCur, INFO_BUF_LEFT,
                                      TX_SIG_HANDLER_SIGINFO_ARG, trap);
  infoCur += lenPrinted;
  if (lenPrinted == 0)
    {
      *(infoCur = infoSkipFrom = infoPrev) = '\0';
      by = "";
    }

#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  infoPrev = infoCur;
  infoCur += htsnpf(infoCur, INFO_BUF_LEFT, "; killing thread PIDs");
  lenPrinted = TXprintOtherThreadPids(TXbool_True, infoCur, INFO_BUF_LEFT);
  infoCur += lenPrinted;
  if (lenPrinted == 0)
    *(infoCur = infoPrev) = '\0';
  else
    infoCur += htsnpf(infoCur, INFO_BUF_LEFT, ";");
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

  if (vx_getpmflags() & VXPMF_ShowPid)
    *pidBuf = '\0';
  else
    htsnpf(pidBuf, sizeof(pidBuf), "(%u) ", (unsigned)TXgetpid(1));

  switch (sigNum)
    {
#ifdef SIGTERM
    case SIGTERM:
      putmsg(MERR + msgNumAddend, CHARPN,
             "%s%s terminated (signal %d)%s%s; exiting",
             pidBuf, TXsigProcessName, (int)sigNum,
             by, infoSkipFrom);
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
             "%s%s caught user interrupt (signal %d)%s; exiting",
             pidBuf, TXsigProcessName, (int)sigNum, infoBuf);
      xitCode = TXEXIT_USERINTERRUPT;
      break;
#endif  /* SIGQUIT || SIGINT */
    default:
      xitCode = TXEXIT_ABEND;

      infoCur += TXprintSigCodeAddr(infoCur, INFO_BUF_LEFT,
                                    TX_SIG_HANDLER_SIGINFO_ARG);

      if (trap & TXtrap_InfoBacktrace)
        {
          infoPrev = infoCur;
          infoCur += htsnpf(infoCur, INFO_BUF_LEFT, " with backtrace ");
          if (TXgetBacktrace(infoCur, INFO_BUF_LEFT, 0x4) == 0)
            *(infoCur = infoPrev) = '\0';
          else if (INFO_BUF_LEFT > 0)
            {
              infoCur += strlen(infoCur);
              didBacktrace = TXbool_True;
            }
        }

      if (trap & TXtrap_InfoRegisters)
        {
          infoPrev = infoCur;
          infoCur += htsnpf(infoCur, INFO_BUF_LEFT,
                            (didBacktrace ? " and registers " :
                             " with registers "));
          lenPrinted = TXprintRegisters(infoCur, INFO_BUF_LEFT,
                                        TX_SIG_HANDLER_SIGINFO_ARG,
                                        TX_SIG_HANDLER_CONTEXT_ARG);
          infoCur += lenPrinted;
          if (lenPrinted == 0) *(infoCur = infoPrev) = '\0';
        }

      if (trap & TXtrap_InfoLocationBad)
        infoCur += TXprabendloc(infoCur, INFO_BUF_LEFT);

      TXgetTexisVersionNumString(versionNumBuf, sizeof(versionNumBuf),
                                 TXbool_True /* vortexStyle */,
                                 TXbool_False /* !forHtml */);
      putmsg(MERR + msgNumAddend, CHARPN,
             "%s%s version %s %aT (%s) ABEND:"
#ifdef _WIN32
             " exception 0x%X"
#else /* !_WIN32 */
             " signal %d"
#endif /* !_WIN32 */
             " (%s)%s; exiting",
             pidBuf, TXsigProcessName,
             versionNumBuf, "|%Y%m%d", (time_t)TxSeconds, TxPlatformDesc,
             (int)sigNum, TXsignalname(sigNum), infoBuf);
      break;
    }
  TXcallabendcbs();
#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  TXkillOtherThreads(TXbool_True);
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

  if (xitCode == TXEXIT_ABEND &&
      (trap & (TXtrap_CoreViaNull | TXtrap_CoreViaReturn)))
    {                                           /* core after abend */
      /* See vortex/vsignal.c:sig_handler() for logic here */
      /* First cd to log dir: */
      if (TXApp && TXApp->logDir)
        {
          if (chdir(TXApp->logDir) != 0) {}
        }
      else
        {
          if (chdir(TXINSTALLPATH_VAL) != 0) {}
          if (chdir("logs") != 0) {}
        }
      if (trap & TXtrap_CoreViaNull) TXmkabend();
      if (trap & TXtrap_CoreViaReturn) goto done;       /* let it core */
      /* fall through to exit */
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

#if defined(EPI_HAVE_MCHECK_H) && 0
static void
TXgenericMcheckHandler(enum mcheck_status mstatus)
/* Handler for glibc memory corruption.  WTF thread-unsafe; unused.
 */
{
  int           msgNumAddend = TX_PUTMSG_NUM_IN_SIGNAL;
#  ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  size_t        lenPrinted;
#  endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
  TXtrap        trap;
  TXbool        didBacktrace = TXbool_False;
  const char    *mDesc;
  char          *infoCur, *infoEnd, *infoPrev, infoBuf[8192];
  char          pidBuf[64], versionNumBuf[128];

#  if defined(_WIN32) && defined(_MSC_VER)
 __try
 {
#  endif /* _WIN32 && _MSC_VER */

  infoCur = infoBuf;
  infoEnd = infoBuf + sizeof(infoBuf);
  *infoCur = '\0';
#  define INFO_BUF_LEFT (infoCur < infoEnd ? infoEnd - infoCur : 0)

  if (TXentersignal()) _exit(TXEXIT_ABEND);     /* per Vortex handler */
  trap = (TXApp ? TXApp->trap : TXtrap_Default);

  if (trap & (TXtrap_CoreViaNull | TXtrap_CoreViaReturn))
    {
#ifdef SIGSEGV
      TXcatchSignal(SIGSEGV, (TX_SIG_HANDLER *)SIG_DFL); /* *CHARPN below */
#endif /* SIGSEGV */
    }

  if (trap & TXtrap_InfoBacktrace)
    {
      /* wtf TXgetBacktrace() seems to fail here.  Stock mcheck()
       * handler backtrace seems hosed anyway.
       */
      infoPrev = infoCur;
      infoCur += htsnpf(infoCur, INFO_BUF_LEFT, " with backtrace ");
      if (TXgetBacktrace(infoCur, INFO_BUF_LEFT, 0x4) == 0)
        *(infoCur = infoPrev) = '\0';
      else if (INFO_BUF_LEFT > 0)
        {
          infoCur += strlen(infoCur);
          didBacktrace = TXbool_True;
        }
    }

  if (trap & TXtrap_InfoLocationBad)
    infoCur += TXprabendloc(infoCur, INFO_BUF_LEFT);

#  ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  infoPrev = infoCur;
  infoCur += htsnpf(infoCur, INFO_BUF_LEFT, "; killing thread PIDs");
  lenPrinted = TXprintOtherThreadPids(TXbool_True, infoCur, INFO_BUF_LEFT);
  infoCur += lenPrinted;
  if (lenPrinted == 0) *(infoCur = infoPrev) = '\0';
#  endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

  switch (mstatus)
    {
    case MCHECK_HEAD:
      mDesc = "Memory preceding an allocated block was clobbered";
      break;
    case MCHECK_TAIL:
      mDesc = "Memory following an allocated block was clobbered";
      break;
    case MCHECK_FREE:
      mDesc = "A block of memory was freed twice";
      break;
    default:
      mDesc = "Unknown memory error";
      break;
    }

  if (vx_getpmflags() & VXPMF_ShowPid)
    *pidBuf = '\0';
  else
    htsnpf(pidBuf, sizeof(pidBuf), "(%u) ", (unsigned)TXgetpid(1));

  TXgetTexisVersionNumString(versionNumBuf, sizeof(versionNumBuf),
                             TXbool_True /* vortexStyle */,
                             TXbool_False /* !forHtml */);
  putmsg(MERR + msgNumAddend, NULL,
         "%s%s version %s %aT (%s) ABEND: glibc memory error: %s%s; exiting",
         pidBuf, TXsigProcessName,
         versionNumBuf, "|%Y%m%d", (time_t)TxSeconds, TxPlatformDesc,
         mDesc, infoBuf);
#  ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  TXkillOtherThreads(TXbool_True);
#  endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
  TXcallabendcbs();

  if (trap & (TXtrap_CoreViaNull | TXtrap_CoreViaReturn))
    {
      /* First cd to log dir: */
      if (TXApp && TXApp->logDir)
        chdir(TXApp->logDir);
      else
        {
          chdir(TXINSTALLPATH_VAL);
          chdir("logs");
        }
      if (trap & TXtrap_CoreViaNull) TXmkabend();
      if (trap & TXtrap_CoreViaReturn) goto done; /* hope it cores*/
      /* else fall through to _exit() in case core failed */
    }

  _exit(TXEXIT_ABEND);	/* _exit avoids stdio/malloc problems */
done:
  TXexitsignal();
#  if defined(_WIN32) && defined(_MSC_VER)
 }
 __except(EXCEPTION_EXECUTE_HANDLER)
 {
   _exit(TXEXIT_ABEND);         /* _exit avoids stdio/malloc problems */
 }
#  endif /* _WIN32 && _MSC_VER */
#  undef INFO_BUF_LEFT
}
#endif /* EPI_HAVE_MCHECK_H && 0 */

TXbool
TXcatchMemoryErrors(void)
/* Catches glibc memory errors.  Must be called before any allocs.
 * Returns false on error (silently).
 */
{
#ifdef EPI_HAVE_MCHECK_H
  /* mcheck() is not thread-safe, per interwebs; causes Vortex test382
   * and monitor web server (both of which use threads) to crash:
   */
  return(TXbool_False);
  /* return(mcheck(TXgenericMcheckHandler) == 0); */
#else /* !EPI_HAVE_MCHECK_H */
  return(TXbool_False);
#endif /* !EPI_HAVE_MCHECK_H */
}

#ifdef _WIN32
BOOL WINAPI
TXgenericConsoleCtrlHandler(DWORD type)
/* Console control handler, sort of like a signal handler, for Windows.
 * Handles Ctrl-C, Ctrl-Break, Close, Logoff, Shutdown events.
 * Note that Windows creates a separate thread for this function when called.
 */
{
  TXEXIT        exitCode = TXEXIT_TERMINATED;
  char          pidBuf[64];

#  if defined(_MSC_VER)
  __try
    {
#  endif /* _MSC_VER */
      /* Make sure we don't recurse, especially into TXcallabendcbs(): */
      /* If we're already in a previous signal handler, ignore this message;
       * since all possible types are less severe than SIGSEGV-type issues,
       * we do not exit but let previous signal handler finish:
       */
      if (TXentersignal()) goto done;

      if (vx_getpmflags() & VXPMF_ShowPid)
        *pidBuf = '\0';
      else
        htsnpf(pidBuf, sizeof(pidBuf), "(%u) ", (unsigned)TXgetpid(1));

      switch (type)
        {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
          putmsg(MERR, CHARPN,
                 "%s%s caught user interrupt (%s); exiting",
                 pidBuf, TXsigProcessName, TXctrlEventName(type));
          exitCode = TXEXIT_USERINTERRUPT;
          break;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
        default:
          putmsg(MERR, CHARPN, "%s%s terminated (%s); exiting",
                 pidBuf, TXsigProcessName, TXctrlEventName(type));
          exitCode = TXEXIT_TERMINATED;
          break;
        }
      TXcallabendcbs();
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

int CDECL
TXgenericExceptionHandler(int sigNum,
            TX_SIG_HANDLER_SIGINFO_TYPE *info /* struct exception_pointers */)
/* Called as:
 * __try
 *   {
 *     ... code to protect ...
 *   }
 * __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
 *   {
 *     nothing here, as TXgenericExceptionHandler() will exit if needed
 *   }
 * Returns EXCEPTION_EXECUTE_HANDLER or EXCEPTION_CONTINUE_SEARCH
 */
{
  int           ret = EXCEPTION_EXECUTE_HANDLER;
  int           msgNumAddend = TX_PUTMSG_NUM_IN_SIGNAL;
  TXtrap        trap = (TXApp ? TXApp->trap : TXtrap_Default);
  size_t        lenPrinted;
  TXbool        didBacktrace = TXbool_False;
  char          *infoCur, *infoEnd, *infoPrev, *infoSkipFrom;
  char          infoBuf[8192];
  char          pidBuf[64], versionNumBuf[128];

  __try
    {
      infoCur = infoSkipFrom = infoBuf;
      infoEnd = infoBuf + sizeof(infoBuf);
      *infoCur = '\0';
#  define INFO_BUF_LEFT (infoCur < infoEnd ? infoEnd - infoCur : 0)

      if (TXentersignal()) _exit(TXEXIT_ABEND); /* do not reenter */
      TX_PUSHERROR();                           /* preserve main thread err */

      /* Decide whether to handle the exception, or ignore it.
       * If we ignore it, also tell monitor.c:
       */
      ret = ((trap & TXtrap_CatchBad) ? EXCEPTION_EXECUTE_HANDLER :
             EXCEPTION_CONTINUE_SEARCH);
      TXexceptionbehaviour = ret;
      if (!(trap & TXtrap_CatchBad)) goto done;

      /* wtf check if bolton server here, pass signal? */
      /* exceptions are internal to process, so no TXprintUidAndAncestors() */

      infoCur += TXprintSigCodeAddr(infoCur, INFO_BUF_LEFT, info);

      if (trap & TXtrap_InfoBacktrace)
        {
          infoPrev = infoCur;
          infoCur += htsnpf(infoCur, INFO_BUF_LEFT, " with backtrace ");
          if (TXgetBacktrace(infoCur, INFO_BUF_LEFT, 0x4) == 0)
            *(infoCur = infoPrev) = '\0';
          else if (INFO_BUF_LEFT > 0)
            {
              infoCur += strlen(infoCur);
              didBacktrace = TXbool_True;
            }
        }

      if (trap & TXtrap_InfoRegisters)
        {
          infoPrev = infoCur;
          infoCur += htsnpf(infoCur, INFO_BUF_LEFT,
                            (didBacktrace ? " and registers " :
                             " with registers "));
          lenPrinted = TXprintRegisters(infoCur, INFO_BUF_LEFT, info, NULL);
          infoCur += lenPrinted;
          if (lenPrinted == 0) *(infoCur = infoPrev) = '\0';
        }

      if (trap & TXtrap_InfoLocationBad)
        infoCur += TXprabendloc(infoCur, INFO_BUF_LEFT);

      if (vx_getpmflags() & VXPMF_ShowPid)
        *pidBuf = '\0';
      else
        htsnpf(pidBuf, sizeof(pidBuf), "(%u) ", (unsigned)TXgetpid(1));

      TXgetTexisVersionNumString(versionNumBuf, sizeof(versionNumBuf),
                                 TXbool_True /* vortexStyle */,
                                 TXbool_False /* !forHtml */);
      putmsg(MERR + msgNumAddend, NULL,
             "%s%s version %s %aT (%s) ABEND:"
#ifdef _WIN32
             " exception 0x%X"
#else /* !_WIN32 */
             " signal %d"
#endif /* !_WIN32 */
             " (%s)%s; exiting",
             pidBuf, TXsigProcessName,
             versionNumBuf, "|%Y%m%d", (time_t)TxSeconds, TxPlatformDesc,
             (int)sigNum, TXsignalname(sigNum), infoBuf);

      if (trap & (TXtrap_InfoStack1K | TXtrap_InfoStack16K))
        {                                       /* print stack */
          byte          *stk;
          size_t        sz;

          if (!info)
            putmsg(MERR + msgNumAddend, NULL, "No exception info available");
          else if (info->cxt == NULL)
            putmsg(MERR + msgNumAddend, NULL,
                   "No exception context available");
          else
            {
              /* registers handled above */
              if (trap & (TXtrap_InfoStack1K | TXtrap_InfoStack16K))
                {                               /* print stack */
#  ifdef _WIN64
                  stk = (byte *)info->cxt->Rsp - 16;
                  putmsg(MINFO + msgNumAddend, NULL,
                         "Stack dump at rsp - 16:");
#  else /* !_WIN64 */
                  stk = (byte *)info->cxt->Esp - 16;
                  putmsg(MINFO + msgNumAddend, NULL,
                         "Stack dump at esp - 16:");
#  endif /* !_WIN64 */
                  sz = 0;
                  if (trap & TXtrap_InfoStack1K) sz += 1024;
                  if (trap & TXtrap_InfoStack16K) sz += 16*1024;
                  tx_hexdumpmsg(TXPMBUFPN, MINFO + msgNumAddend, NULL,
                                stk, sz, 0x1);
                }
            }
        }
      /* no cores in Windows, so no TXtrap_CoreViaNull TXtrap_CoreViaReturn */
      TXcallabendcbs();
      _exit(TXEXIT_ABEND);

    done:
      TX_POPERROR();
      TXexitsignal();
    }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
    _exit(TXEXIT_ABEND);
  }
  return(ret);
#  undef INFO_BUF_LEFT
}
#endif /* _WIN32 */

TXbool
TXsetSigProcessName(TXPMBUF *pmbuf, const char *name)
/* Can set NULL to set default of `Process', e.g. to free mem at exit.
 */
{
  char          *dup = NULL, *old;
  TXbool        ret;

  old = TXsigProcessName;
  if (name)
    {
      if ((dup = TXstrdup(pmbuf, __FUNCTION__, name)) != NULL)
        TXsigProcessName = dup;
      ret = (dup != NULL);
    }
  else
    {
      TXsigProcessName = (char *)TXsigProcessNameDef;
      ret = TXbool_True;
    }
  if (old && old != TXsigProcessNameDef) old = TXfree(old);
  return(ret);
}

void
TXsysdepCleanupAtExit()
/* Free mem just before exit.  Only called for MEMDEBUG neatness.
 */
{
  TXsetSigProcessName(TXPMBUFPN, NULL);
}

void
tx_setgenericsigs(void)
/* Sets generic signal handlers.  Used by kdbfchk, addtable, etc.
 */
{
  if (TXApp &&
      (TXApp->trap & (TXtrap_CoreViaNull | TXtrap_CoreViaReturn)))
    TXmaximizeCoreSize();

#ifdef _MSC_VER
#  if _MSC_VER >= 1400 /* handler for Parameter Validation, VS >= 2005 */
  _set_invalid_parameter_handler(txInvalidParameterHandler);
#  endif /* Visual Studio >= 2005 */
#  ifdef _DEBUG
  SetErrorMode(0); /* use  windows exceptions, despite parent (Cygwin) */
#  endif /* _DEBUG */
#endif /* _MSC_VER (visual studio only) */

#ifdef SIGTERM
  tx_catchgenericsig(SIGTERM);
#endif /* SIGTERM */
  /* Start the alarm thread, so it can service TEXIS_TERMINATE_PID_nnn
   * (<kill $pid SIGTERM>) events too under Windows, and catches alarms
   * under Unix:
   */
  TXstartEventThreadAlarmHandler();
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

void
tx_unsetgenericsigs(level)
int     level;  /* 0: unset all sigs  1: unset fatal sigs */
{
   if (level <= 1)
     {
#ifdef SIGTERM
       signal(SIGTERM, SIG_DFL);
#endif /* SIGTERM */
#ifdef SIGINT
       signal(SIGINT, SIG_DFL);
#endif /* SIGINT */
#ifdef SIGQUIT
       signal(SIGQUIT, SIG_DFL);
#endif /* SIGQUIT */
#ifdef SIGSEGV
       signal(SIGSEGV, SIG_DFL);
#endif /* SIGSEGV */
#ifdef SIGBUS
       signal(SIGBUS, SIG_DFL);
#endif /* SIGBUS */
#ifdef SIGFPE
       signal(SIGFPE, SIG_DFL);
#endif /* SIGFPE */
#ifdef SIGILL
       signal(SIGILL, SIG_DFL);
#endif /* SIGILL */
#ifdef SIGABRT
       signal(SIGABRT, SIG_DFL);
#endif /* SIGABRT */
       if (level <= 0)
         {
#ifdef SIGPIPE
           signal(SIGPIPE, SIG_DFL);
#endif /* SIGPIPE */
#ifdef SIGXFSZ
           signal(SIGXFSZ, SIG_DFL);
#endif /* SIGXFSZ */
#ifdef SIGXCPU
           signal(SIGXCPU, SIG_DFL);
#endif /* SIGXCPU */
#ifdef SIGHUP
           signal(SIGHUP, SIG_DFL);
#endif /* SIGHUP */
         }
     }
}

/* ------------------------------------------------------------------------- */

long
TXsleepmsec(msec, ignsig)
long    msec;
int     ignsig;
/* Sleeps for `msec' milliseconds.  Ignores signals if `ignsig'.
 * Returns milliseconds of time left unslept (if interrupted by signal).
 * Does not use SIGALRM or setitimer().  Should be used instead of sleep()
 * to avoid potential interference with TXsetalarm().
 * Will not call putmsg() (assumed by vxPutmsgBuf.c).
 */
{
  do
    {
#if defined(__sgi)
      msec = sginap(msec/(1000L/CLK_TCK));
      if (msec >= 0) msec *= 1000L/CLK_TCK;
      else msec = 0L;                           /* error */
#elif defined(_WIN32)
#  if 1
      /* not interruptable, according to MSDN?  (as opposed to SleepEx()) */
      Sleep(msec);
      msec = 0L;
#  else /* !1 */
      SYSTEMTIME    st;
      long          mdest;

      GetSystemTime(&st);
      mdest = ((st.wHour*60L + st.wMinute)*60L + st.wSecond)*1000L +
        st.wMilliseconds + msec;
      Sleep(msec);
      GetSystemTime(&st);
      msec = mdest - (((st.wHour*60L + st.wMinute)*60L + st.wSecond)*1000L +
                      st.wMilliseconds);
      if (msec < 125L) msec = 0L;               /* close enough */
#  endif /* !1 */
#elif defined(EPI_HAVE_NANOSLEEP)
      /* KNG 20090605 Bug 2678: select(NULL, NULL, NULL, timer) does not
       * return nonzero nor errno non-zero under hppa1.1-hp-hpux11.00-32-32.
       * Use nanosleep() over select() where possible; supposedly does not
       * use signals (or setitimer()?), and returns time remaining and error:
       */
      struct timespec   request, remain;

      request.tv_sec = msec/1000L;
      request.tv_nsec = 1000000L*(msec % 1000L);
      remain.tv_sec = remain.tv_nsec = 0L;
      errno = 0;
      if (nanosleep(&request, &remain) < 0)     /* err, prolly woke early */
        {
          if (errno == EINTR)                   /* a signal was received */
            msec = remain.tv_sec*1000L + remain.tv_nsec / 1000000L;
          else
            msec = 0L;                          /* error */
        }
      else
        msec = 0L;
#else /* !_WIN32 && !__sgi && !EPI_HAVE_NANOSLEEP */
      struct timeval        tv;
#  if !defined(linux) && !defined(__linux)
      long                  mdest;

      gettimeofday(&tv, NULL);
      mdest = tv.tv_sec*1000L + (tv.tv_usec / 1000L) + msec;
#  endif /* !linux && !__linux */
      tv.tv_sec = msec/1000L;
      tv.tv_usec = 1000L*(msec % 1000L);
      errno = 0;
      if (select(0, FD_SETPN, FD_SETPN, FD_SETPN, &tv) < 0)
        {
          if (errno == EINTR)                   /* a signal was received */
            {
#  if defined(linux) || defined(__linux)
              msec = tv.tv_sec*1000L + tv.tv_usec / 1000L;
#  else
              gettimeofday(&tv, NULL);
              msec = mdest - (tv.tv_sec*1000L + tv.tv_usec / 1000L);
              if (msec < 125L) msec = 0L;       /* close enough */
#  endif /* !linux && !__linux */
            }
          else
            msec = 0L;                          /* error */
        }
      else
        msec = 0L;
#endif /* !_WIN32 && !__sgi */
    }
  while (msec > 0L && ignsig);
  return(msec);
}

/* Converts Windows FILETIME to seconds (but not time_t start offset): */
#define TX_FILETIME_TO_SECONDS(lo, hi)  \
  ((((double)(hi))*((double)4294967296.0)+(double)(lo))/(double)10000000.0)

double
TXfiletime2time_t(lo, hi)
EPI_UINT32      lo, hi;         /* lo/hi 32-bit words of file time */
/* Converts Windows FILETIME value `lo'/`hi' to time_t and returns it.
 *   Note that our method is believed more accurate than MSVC stat()
 * method, which is FileTimeToLocalFileTime(), FileTimeToSystemTime(),
 * then __loctotime_t(): but the docs for FileTimeToLocalFileTime()
 * say it uses the *current* DST offset, not the FILETIME's, so if
 * current time is DST and FILETIME is STD (or vice versa) it is
 * off by 1 hour.  This can be seen in MSVC stat.st_mtime, dir and
 * Windows Explorer file times changing when the clock changes from
 * a DST to STD time or vice versa (happens in Win2k/Win2k3; Win2k8
 * appears fixed, at least in Windows Explorer?).  See Bug 3733.
 * Research says use SystemTimeToTzSpecificLocalTime() (first with
 * FileTimeToSystemTime()?) to avoid FileTimeToLocalFileTime() bug.
 * Thread-safe.  Signal-safe.
 */
{
  return(TX_FILETIME_TO_SECONDS(lo, hi) -
         (double)11644473600.0);        /* 1601-01-01 to 1970-01-01 in sec. */
}

int
TXtime_t2filetime(tim, lo, hi)
double          tim;            /* time_t to convert */
EPI_UINT32      *lo, *hi;
/* Converts time_t-scale `tim' to Windows FILETIME and returns in
 * `*lo'/`*hi'.  Returns 1 if ok, 0 on error.
 */
{
  EPI_UINT64    quad;

  quad = (EPI_UINT64)((tim + (double)11644473600.0)*(double)10000000.0);
  *lo = (EPI_UINT32)quad;
  *hi = (EPI_UINT32)(quad >> 32);
  return(1);
}

int
TXsetProcessStartTime(void)
/* Called to timestamp start of process.  Should be called ASAP at
 * start of every process, e.g. in main(), in child of fork().
 * Returns 0 on error.  Silent; does not yap, as it may be called
 * before putmsg() is set up.
 */
{
#ifdef _WIN32
  /* no need to track it; obtained with GetProcessTimes() */
  return(1);
#else /* !_WIN32 */
  if (TXprocessStartTime == -1.0)
    TXprocessStartTime = TXgettimeofday();
  return(TXprocessStartTime != -1.0);
#endif /* !_WIN32 */
}

double
TXgettimeofday()
/* Returns seconds since 00:00 Jan 1 1970 UTC, including fractions of a second.
 * (Same scale as time(), but with fraction.)
 * Returns -1 on error.  Silent; does not yap, as it may be called
 * before putmsg() is set up.
 */
{
#if defined(_WIN32)
  FILETIME              ft;

  GetSystemTimeAsFileTime(&ft);
  return(TXfiletime2time_t(ft.dwLowDateTime, ft.dwHighDateTime));
#else /* !_WIN32 */
  struct timeval        tv;

  if (gettimeofday(&tv, NULL) != 0)
    return((double)(-1.0));
  return((double)tv.tv_sec + (double)tv.tv_usec / (double)1000000.0);
#endif /* !_WIN32 */
}

TXbool
TXgetTimeContinuousFixedRate(double *theTime)
/* Sets `*theTime' to a time_t-scale (i.e. in seconds) time that is
 * continuous (no jumps due to system clock changes) and fixed-rate
 * (no speedup/slowdown) since boot.  Epoch is not 1970-01-01 but is
 * undefined; usually roughly corresponds to system boot.
 * Returns false if unsupported.
 */
{
  TXbool                ret;
#if defined(EPI_HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC_RAW)
  struct timespec       ts;

  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) goto err;
  *theTime = (double)ts.tv_sec + ((double)ts.tv_nsec)/(double)1000000000.0;
#elif defined(_WIN32)
  static EPI_HUGEINT    qpfFreq = 0;
  LARGE_INTEGER         ctr;

  if (!qpfFreq)
    {
      LARGE_INTEGER     freq;
      /* QueryPerformanceFrequency() is fixed and determined at boot */
      if (!QueryPerformanceFrequency(&freq)) goto err;
      qpfFreq = (EPI_HUGEINT)freq.QuadPart;
      if (qpfFreq <= 0) qpfFreq = 1;            /* sanity check */
    }
  if (!QueryPerformanceCounter(&ctr)) goto err;
  *theTime = (double)(ctr.QuadPart/qpfFreq) +
    ((double)(ctr.QuadPart % qpfFreq)) / ((double)qpfFreq);
#else
  goto err;
#endif
  ret = TXbool_True;
  goto finally;

err:
  *theTime = -1.0;
  ret = TXbool_False;
finally:
  return(ret);
}

double
TXgetTimeContinuousFixedRateOrOfDay(void)
/* Returns TXgetTimeContinuousFixedRate(), or TXgettimeofday() if
 * former unsupported.
 */
{
  static TXATOMINT      gotErr = -1;
  double                now;

  if (gotErr > 0)
    now = TXgettimeofday();
  else if (!TXgetTimeContinuousFixedRate(&now))
    {
      gotErr = 1;
      now = TXgettimeofday();
    }
  return(now);
}

TXbool
TXgetTimeContinuousVariableRate(double *theTime)
/* Sets `*theTime' to a time_t-scale (i.e. in seconds) time that is
 * continuous (no jumps due to system clock changes) since boot, but
 * potentially variable-rate (may speedup/slowdown due to NTP etc.).
 * Epoch is not 1970-01-01 but is undefined; usually roughly
 * corresponds to system boot.
 * Returns false on error.
 */
{
  TXbool                ret;
#if defined(EPI_HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  struct timespec       ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) goto err;
  *theTime = (double)ts.tv_sec + ((double)ts.tv_nsec)/(double)1000000000.0;
#else
  goto err;
#endif
  ret = TXbool_True;
  goto finally;

err:
  *theTime = -1.0;
  ret = TXbool_False;
finally:
  return(ret);
}

int
TXsetFileTime(path, fd, creationTime, accessTime, modificationTime, allNow)
CONST char      *path;                  /* (in, opt.) file to modify */
int             fd;                     /* (in, opt.) handle to modify */
CONST double    *creationTime;          /* (in, opt.) creation time_t */
CONST double    *accessTime;            /* (in, opt.) access time_t */
CONST double    *modificationTime;      /* (in, opt.) modification time_t */
int             allNow;                 /* (in) true: set all times to now */
/* Sets file creation/access/modification times.  If `fd >= 0', `fd'
 * is used, else `path'.  A NULL time will not be set (i.e. existing
 * file time left as-is).  Setting `allNow' nonzero sets all
 * (non-NULL) times to "now" regardless of numeric value, which allows
 * Unix [f]utime[s]() to potentially touch files when it has write
 * perms but not ownership.
 * Returns 0 on success, -1 on error, -2 if action not supported (errno set),
 * e.g. set-via-`fd' may not be supported.
 */
{
#ifdef _WIN32
  FILETIME      creationFiletime, accessFiletime, modificationFiletime;
  FILETIME      *cPtr, *aPtr, *mPtr, now;
  HANDLE        myHandle = INVALID_HANDLE_VALUE;
  BOOL          res;

  /* futime() broken under Windows 2k/2k3 at least; uses broken
   * FileTimeToLocalFileTime() (Bug 3733).  Use SetFileTime().
   */
  if (allNow)
    {
      GetSystemTimeAsFileTime(&now);
      creationFiletime = accessFiletime = modificationFiletime = now;
      cPtr = (creationTime ? &creationFiletime : NULL);
      aPtr = (accessTime ? &accessFiletime : NULL);
      mPtr = (modificationTime ? &modificationFiletime : NULL);
    }
  else
    {
      if (creationTime &&
          TXtime_t2filetime(*creationTime, &creationFiletime.dwLowDateTime,
                            &creationFiletime.dwHighDateTime))
        cPtr = &creationFiletime;
      else
        cPtr = NULL;
      if (accessTime &&
          TXtime_t2filetime(*accessTime, &accessFiletime.dwLowDateTime,
                            &accessFiletime.dwHighDateTime))
        aPtr = &accessFiletime;
      else
        aPtr = NULL;
      if (modificationTime &&
          TXtime_t2filetime(*modificationTime,
                            &modificationFiletime.dwLowDateTime,
                            &modificationFiletime.dwHighDateTime))
        mPtr = &modificationFiletime;
      else
        mPtr = NULL;
    }

  /* Apply the times to the file: */
  if (fd < 0)                                   /* no file descriptor given */
    {
      /* We need a handle for SetFileTime(), so open the file.  First
       * try opening with FILE_FLAG_BACKUP_SEMANTICS because that will
       * allow us to open a dir:
       */
      myHandle = CreateFile(path,
                            FILE_WRITE_ATTRIBUTES, /* for SetFileTime() */
                            /* Allow others to play with the file while
                             * we have it open:
                             */
                            (FILE_SHARE_READ | FILE_SHARE_WRITE |
                             FILE_SHARE_DELETE),
                            NULL,               /* non-inheritable handle */
                            OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, /* in case dir */
                            NULL);
      if (myHandle == INVALID_HANDLE_VALUE)     /* failed, try again */
        {
          myHandle = CreateFile(path,
                                FILE_WRITE_ATTRIBUTES, /* for SetFileTime() */
                                (FILE_SHARE_READ | FILE_SHARE_WRITE |
                                 FILE_SHARE_DELETE),
                                NULL,           /* non-inheritable handle */
                                OPEN_EXISTING,
                                0,
                                NULL);
          if (myHandle == INVALID_HANDLE_VALUE) /* still failed */
            {
              errno = TXmapOsErrorToErrno(GetLastError());
              return(-1);                       /* error */
            }
        }
    }
  res = SetFileTime((myHandle != INVALID_HANDLE_VALUE ? myHandle :
                     (HANDLE)_get_osfhandle(fd)), cPtr, aPtr, mPtr);
  if (myHandle != INVALID_HANDLE_VALUE)
    {
      TX_PUSHERROR();                           /* preserve errno */
      CloseHandle(myHandle);
      myHandle = INVALID_HANDLE_VALUE;
      TX_POPERROR();
    }
  if (!res) return(-1);                         /* error */
  return(0);                                    /* success */
#elif defined(EPI_HAVE_UTIME) || defined(EPI_HAVE_UTIMES) || defined(EPI_HAVE_FUTIME) || defined(EPI_HAVE_FUTIMES)
  struct timeval        tv[2], *tvPtr, now = { 0L, 0L };
  EPI_STAT_S            st;
  int                   res, gotNow = 0;

  /* `creationTime' not used/settable under Unix */
  (void)creationTime;

  memset(tv, 0, sizeof(tv));                    /* just in case */
  if (allNow && accessTime && modificationTime)
    /* A NULL time to [f]utime[s]() may allow it to touch to current time
     * even if we only have write perms and do not own the file:
     */
    tvPtr = NULL;
  else
    {
      tvPtr = tv;
      if (!accessTime || !modificationTime)
        {
          /* One or both times to be left as-is; need to stat(): */
          if (!accessTime && !modificationTime) goto ok;  /* nothing to do */
          if (fd >= 0)
            res = EPI_FSTAT(fd, &st);
          else
            res = EPI_STAT(path, &st);
          if (res != 0) return(-1);             /* error */
        }
      if (accessTime)
        {
          if (allNow)
            {
              if (!gotNow && gettimeofday(&now, NULL) != 0) return(-1);
              gotNow = 1;
              tv[0] = now;
            }
          else
            {
              tv[0].tv_sec = (long)*accessTime;
              tv[0].tv_usec = (long)((*accessTime -
                                (double)((EPI_INT64)*accessTime))*1000000.0);
            }
        }
      else                                      /* use stat() time */
        {
          tv[0].tv_sec = (long)st.st_atime;
          /* Try to retain as much precision as possible; copy nanoseconds: */
#  ifdef EPI_HAVE_ST_ATIMENSEC
          tv[0].tv_usec = (long)st.st_atimensec/1000;
#  elif defined(EPI_HAVE_ST_ATIM_TV_NSEC)
          tv[0].tv_usec = (long)st.st_atim.tv_nsec/1000;
#  else
          tv[0].tv_usec = 0L;
#  endif
        }
      if (modificationTime)
        {
          if (allNow)
            {
              if (!gotNow && gettimeofday(&now, NULL) != 0) return(-1);
              gotNow = 1;
              tv[1] = now;
            }
          else
            {
              tv[1].tv_sec = (long)*modificationTime;
              tv[1].tv_usec = (long)((*modificationTime -
                           (double)((EPI_INT64)*modificationTime))*1000000.0);
            }
        }
      else                                      /* use stat() time */
        {
          tv[1].tv_sec = (long)st.st_mtime;
          /* Try to retain as much precision as possible; copy nanoseconds: */
#  ifdef EPI_HAVE_ST_MTIMENSEC
          tv[1].tv_usec = (long)st.st_mtimensec/1000;
#  elif defined(EPI_HAVE_ST_MTIM_TV_NSEC)
          tv[1].tv_usec = (long)st.st_mtim.tv_nsec/1000;
#  else
          tv[1].tv_usec = 0L;
#  endif
        }
    }

  /* Apply the times from `tvPtr': */
  if (fd >= 0)                                  /* set time on `fd' */
    {
#  ifdef EPI_HAVE_FUTIMES
      if (futimes(fd, tvPtr) != 0) return(-1);
#  elif defined(EPI_HAVE_FUTIME)
      struct utimbuf    ut;
      ut.actime = tv[0].tv_sec;                 /* losing tv_usec; oh well */
      ut.modtime = tv[1].tv_sec;                /* "" */
      if (futime(fd, (tvPtr ? &ut : NULL)) != 0) return(-1);
#  else /* !EPI_HAVE_FUTIME[S] */               /* unsupported platform */
      /* wtf */
#  ifdef ENOSYS
      errno = ENOSYS;
#  else /* !ENOSYS */
      errno = EINVAL;
#  endif /* !ENOSYS */
      return(-2);                               /* unsupported action */
#  endif /* !EPI_HAVE_FUTIME[S] */
    }
  else                                          /* set time on `path' */
    {
#  ifdef EPI_HAVE_UTIMES
      if (utimes((char *)path, tvPtr) != 0) return(-1);
#  elif defined(EPI_HAVE_UTIME)
      struct utimbuf    ut;
      ut.actime = tv[0].tv_sec;                 /* losing tv_usec; oh well */
      ut.modtime = tv[1].tv_sec;                /* "" */
      if (utime(path, (tvPtr ? &ut : NULL)) != 0) return(-1);
#  else /* !EPI_HAVE_UTIME[S] */                /* unsupported platform */
      /* wtf */
#  ifdef ENOSYS
      errno = ENOSYS;
#  else /* !ENOSYS */
      errno = EINVAL;
#  endif /* !ENOSYS */
      return(-2);                               /* unsupported action */
#  endif /* !EPI_HAVE_UTIME[S] */
    }
ok:
  return(0);                                    /* success */
#else /* !_WIN32 && !EPI_HAVE_[F]UTIME[S] */
  /* wtf */
  (void)creationTime;
#  ifdef ENOSYS
  errno = ENOSYS;
#  else /* !ENOSYS */
  errno = EINVAL;
#  endif /* !ENOSYS */
  return(-2);                                   /* unsupported action */
#endif /* !_WIN32 && !EPI_HAVE_[F]UTIME[S] */
}

/* ------------------------------------------------------------------------- */

#define FOR_SYSDEP_C
#include "decode.c"
#undef FOR_SYSDEP_C

/******************************************************************/

static const char * const       TXrusageNames[] =
{
#undef I
#define I(tok)  #tok,
  TXRUSAGE_SYMBOLS_LIST
#undef I
  NULL
};

TXRUSAGE
TXstrToTxrusage(const char *s, size_t sLen)
/* Returns TXRUSAGE value for `s', or TXRUSAGE_UNKNOWN if unknown.
 * `sLen' is length of `s'; strlen(s) assumed if -1.
 * Thread-safe.  Signal-safe.
 */
{
  return((TXRUSAGE)TXfindStrInList((char **)TXrusageNames, -1, s, sLen, 0x1));
}

static const char * const       TXresourcestatNames[] =
{
#undef I
#define I(tok)  #tok,
  TXRESOURCESTAT_SYMBOLS_LIST
#undef I
  NULL
};

TXRESOURCESTAT
TXstrToTxresourcestat(const char *s, size_t sLen)
/* Returns TXRESOURCESTAT value for `s', or TXRESOURCESTAT_UNKNOWN if unknown.
 * `sLen' is length of `s'; strlen(s) assumed if -1.
 * Thread-safe.  Signal-safe.
 */
{
  return((TXRESOURCESTAT)TXfindStrInList((char **)TXresourcestatNames, -1,
                                         s, sLen, 0x1));
}

int
TXgetResourceStats(TXPMBUF *pmbuf, TXRUSAGE who, TXRESOURCESTATS *stats)
/* Gets resource usage statistics into `stats' for `who'.  Not all TXRUSAGE
 * values are supported on all platforms (error return if not).
 * Not all TXRESOURCESTAT values are supported on all platforms
 * (unsupported stats set to -1).
 * Returns 0 on error.
 * Thread-safe.  Signal-safe.
 */
{
  static const char     fn[] = "TXgetResourceStats";
  int                   ret;
  TXRESOURCESTAT        statIdx;
#ifdef _WIN32
  FILETIME              creationTime, exitTime, kernelTime, userTime;

  switch (who)
    {
    case TXRUSAGE_SELF:
      if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime,
                           &kernelTime, &userTime))
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot GetProcessTimes(): %s",
                         TXstrerror(TXgeterror()));
          goto err;
        }
      break;
    case TXRUSAGE_CHILDREN:
      /* wtf need a handle to child process(es) */
      /* fall through */
    case TXRUSAGE_BOTH:
      /* wtf need a handle to child process(es) */
      txpmbuf_putmsg(pmbuf, MERR, fn,
                     "TXRUSAGE_%s unsupported on this platform",
                TX_STR_LIST_VAL(TXrusageNames, TXRUSAGE_NUM, who, "UNKNOWN"));
      goto err;
    case TXRUSAGE_THREAD:
      if (!GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime,
                          &kernelTime, &userTime))
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot GetThreadTimes(): %s",
                         TXstrerror(TXgeterror()));
          goto err;
        }
      break;
    default:
      txpmbuf_putmsg(pmbuf, MERR, fn, "Unknown TXRUSAGE value %d", (int)who);
      goto err;
    }
  for (statIdx = (TXRESOURCESTAT)0; statIdx < TXRESOURCESTAT_NUM; statIdx++)
    stats->values[statIdx] = -1.0;              /* for unknown values */
  stats->values[TXRESOURCESTAT_UserTime] =
    TX_FILETIME_TO_SECONDS(userTime.dwLowDateTime, userTime.dwHighDateTime);
  stats->values[TXRESOURCESTAT_SystemTime] =
   TX_FILETIME_TO_SECONDS(kernelTime.dwLowDateTime,kernelTime.dwHighDateTime);
  stats->values[TXRESOURCESTAT_RealTime] = TXgettimeofday() -
   TXfiletime2time_t(creationTime.dwLowDateTime, creationTime.dwHighDateTime);
#else /* !_WIN32 */
  int           whoOsParam;
  struct rusage usage;

  switch (who)
    {
    case TXRUSAGE_SELF:
      whoOsParam = RUSAGE_SELF;
      break;
    case TXRUSAGE_CHILDREN:
      whoOsParam = RUSAGE_CHILDREN;
      break;
#  ifdef RUSAGE_BOTH
    case TXRUSAGE_BOTH:
      whoOsParam = RUSAGE_BOTH;
      break;
#  endif /* RUSAGE_BOTH */
#  ifdef RUSAGE_THREAD
    case TXRUSAGE_THREAD:
      whoOsParam = RUSAGE_THREAD;
      break;
#  endif /* RUSAGE_THREAD */
    default:
      if ((unsigned)who < (unsigned)TXRUSAGE_NUM)
        txpmbuf_putmsg(pmbuf, MERR, fn,
                       "TXRUSAGE_%s unsupported on this platform",
                TX_STR_LIST_VAL(TXrusageNames, TXRUSAGE_NUM, who, "UNKNOWN"));
      else
        txpmbuf_putmsg(pmbuf, MERR, fn, "Unknown TXRUSAGE value %d",
                       (int)who);
      goto err;
    }
  if (getrusage(whoOsParam, &usage) != 0)       /* if failure */
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, "getrusage(%s) failed: %s",
                     TX_STR_LIST_VAL(TXrusageNames, TXRUSAGE_NUM, who,
                                     "UNKNOWN"), TXstrerror(TXgeterror()));
      goto err;
    }
  for (statIdx = (TXRESOURCESTAT)0; statIdx < TXRESOURCESTAT_NUM; statIdx++)
    stats->values[statIdx] = -1.0;              /* for unknown values */
  stats->values[TXRESOURCESTAT_UserTime] = (double)usage.ru_utime.tv_sec +
    (double)usage.ru_utime.tv_usec/(double)1000000.0;
  stats->values[TXRESOURCESTAT_SystemTime] = (double)usage.ru_stime.tv_sec +
    (double)usage.ru_stime.tv_usec/(double)1000000.0;
  if (who == TXRUSAGE_SELF && TXprocessStartTime != -1.0)
    stats->values[TXRESOURCESTAT_RealTime] =
      TXgettimeofday() - TXprocessStartTime;
  else                                          /* wtf thread start time? */
    stats->values[TXRESOURCESTAT_RealTime] = -1.0;
  if (who == TXRUSAGE_SELF)
    {
      static const char path[] =
        PATH_SEP_S "proc" PATH_SEP_S "self" PATH_SEP_S "status";
      int               fd = -1, numRd, errnum;
      double            vmSize;
      char              buf[4096], *s, *e;

      fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, path,
                     TXrawOpenFlag_None, O_RDONLY, 0666);
      if (fd >= 0 &&                            /* have /proc/self/status */
          (numRd = tx_rawread(TXPMBUF_SUPPRESS, fd, path, (byte *)buf,
                              sizeof(buf) - 1, 0x8)) > 0)
        {
          buf[numRd] = '\0';
          s = strstri(buf, "VmPeak:");
          if (s)                                /* found `VmPeak' */
            {
              s += 7;
              s += strspn(s, Whitespace);
              vmSize = TXstrtod(s, NULL, &e, &errnum);
              if (e > s && errnum == 0)         /* parsed VmPeak value */
                {
                  s = e;
                  s += strspn(s, Whitespace);
                  if (strnicmp(s, "kB", 2) == 0)
                    vmSize *= 1024.0;
                  else if (strnicmp(s, "mB", 2) == 0)
                    vmSize *= 1024.0*1024.0;
                  stats->values[TXRESOURCESTAT_MaxVirtualMemorySize] = vmSize;
                }
            }
        }
      if (fd >= 0) { close(fd); fd = -1; }
    }
  stats->values[TXRESOURCESTAT_MaxResidentSetSize] = usage.ru_maxrss
#if defined(__APPLE__) && defined(__MACH__)
#else
    *1024
#endif
    ;
  stats->values[TXRESOURCESTAT_IntegralSharedMemSize] = usage.ru_ixrss;
  stats->values[TXRESOURCESTAT_IntegralUnsharedDataSize] = usage.ru_idrss;
  stats->values[TXRESOURCESTAT_IntegralUnsharedStackSize] = usage.ru_isrss;
  stats->values[TXRESOURCESTAT_MinorPageFaults] = usage.ru_minflt;
  stats->values[TXRESOURCESTAT_MajorPageFaults] = usage.ru_majflt;
  stats->values[TXRESOURCESTAT_Swaps] = usage.ru_nswap;
  stats->values[TXRESOURCESTAT_BlockInputOps] = usage.ru_inblock;
  stats->values[TXRESOURCESTAT_BlockOutputOps] = usage.ru_oublock;
  stats->values[TXRESOURCESTAT_MessagesSent] = usage.ru_msgsnd;
  stats->values[TXRESOURCESTAT_MessagesReceived] = usage.ru_msgrcv;
  stats->values[TXRESOURCESTAT_SignalsReceived] = usage.ru_nsignals;
  stats->values[TXRESOURCESTAT_VoluntaryContextSwitches] = usage.ru_nvcsw;
  stats->values[TXRESOURCESTAT_InvoluntaryContextSwitches] = usage.ru_nivcsw;
#endif /* !_WIN32 */
  ret = 1;
  goto finally;

err:
  ret = 0;
  for (statIdx = (TXRESOURCESTAT)0; statIdx < TXRESOURCESTAT_NUM; statIdx++)
    stats->values[statIdx] = -1.0;
finally:
  return(ret);
}

int
TXshowproctimeinfo(pinfo1, pinfo2)
TXRESOURCESTATS *pinfo1, *pinfo2;
{
	double real_time = 0, system_time = 0, user_time = 0;

	real_time = pinfo2->values[TXRESOURCESTAT_RealTime]
	          - pinfo1->values[TXRESOURCESTAT_RealTime];
	system_time = pinfo2->values[TXRESOURCESTAT_SystemTime]
	            - pinfo1->values[TXRESOURCESTAT_SystemTime];
	user_time = pinfo2->values[TXRESOURCESTAT_UserTime]
	          - pinfo1->values[TXRESOURCESTAT_UserTime];
	printf("%.3fs real %.3fs user %.3fs system\n",
		real_time,  user_time,  system_time);
	return 0;
}

int
TXshowmaxmemusage()
{
#if defined(_WIN32) || defined(__APPLE__)
#else
extern int _end;
  htpf("Max dynamic mem = %wd\n", (EPI_HUGEINT)((char *)sbrk(0) - (char *)&_end));
#endif
	return 0;
}

#if defined(sun) && defined(__SVR4)
/* see syssol.c for TXgetmeminfo() */
#else
int
TXgetmeminfo(mem)
size_t  mem[2];
/* Puts virtual size and resident set size for this process, in
 * bytes, in `mem' array.
 * Returns 1 if ok; 0 if error (unable to get sizes) and sizes set to -1.
 * Thread-safe.  Async-signal-safe.
 */
{
#if defined(linux) && defined(MY_PAGE_SIZE)     /* Linux ------------------ */
  int   fd, n;
  char  *s, *e, tmp[512];

  htsnpf(tmp, sizeof(tmp),
         PATH_SEP_S "proc" PATH_SEP_S "%u" PATH_SEP_S "stat", (int)getpid());
  fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, tmp,
                 TXrawOpenFlag_None, O_RDONLY, 0666);
  if (fd < 0) goto err;
  n = (int)read(fd, tmp, sizeof(tmp) - 1);
  close(fd);
  fd = -1;
  if (n < 40) goto err;                         /* arbitrary min. size */
  tmp[n] = '\0';

  strtol(tmp, &s, 0);                           /* pid */
  if (s == tmp) goto err;
  s += strspn(s, Whitespace);
  s += strcspn(s, Whitespace);                  /* comm */
  s += strspn(s, Whitespace);
  s++;                                          /* state */
  if (s - tmp < 5) goto err;
  for (n = 0; n < 19; n++) strtol(s, &s, 0);
  mem[0] = (size_t)strtol(s, &e, 0);            /* VSZ */
  if (e == s) goto err;
  mem[1] = (size_t)strtol(e, &s, 0)*MY_PAGE_SIZE;       /* RSS in pages */
  if (s == e) goto err;
  return(1);
#elif defined(__alpha)                          /* Alpha ------------------ */
  struct prpsinfo       pi;
  int                   fd, ret;
  char                  tmp[128];

  htsnpf(tmp, sizeof(tmp), PATH_SEP_S "proc" PATH_SEP_S "%d", (int)getpid());
  fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, tmp,
                 TXrawOpenFlag_None, O_RDONLY, 0666);
  if (fd < 0) goto err;
  ret = ioctl(fd, PIOCPSINFO, (void *)&pi);
  close(fd);
  fd = -1;
  if (ret == -1) goto err;
  mem[0] = (size_t)pi.pr_size*TXpagesize();     /* VSZ */
  mem[1] = (size_t)pi.pr_rssize*TXpagesize();   /* RSS */
  return(1);
#elif defined(__sgi)                            /* Irix ------------------- */
  size_t        vsz, rss;

#  if defined(EPI_OS_MAJOR)
#    if EPI_OS_MAJOR >= 6                       /* Irix 6+ */
  struct prpsinfo       pi;
  char                  tmp[32];
  int                   fd, ret;

  htsnpf(tmp, sizeof(tmp),
         PATH_SEP_S "proc" PATH_SEP_S "pinfo" PATH_SEP_S "%010d",
         (int)getpid());
  fd = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__, NULL, tmp,
                 TXrawOpenFlag_None, O_RDONLY, 0666);
  if (fd < 0) goto err;
  ret = ioctl(fd, PIOCPSINFO, (void *)&pi);
  close(fd);
  fd = -1;
  if (ret == -1) goto err;
  vsz = (size_t)pi.pr_size;
  rss = (size_t)pi.pr_rssize;
#    elif EPI_OS_MAJOR == 5                     /* Irix 5 */
  unsigned int  v = 0, r = 0;
  if (syssgi(SGI_PROCSZ, getpid(), &v, &r) == -1) goto err;
  vsz = (size_t)v;
  rss = (size_t)r;
#    else                                       /* Irix 4.0 */
  unsigned int  v = 0, r = 0;
  syssgi(SGI_PROCSZ, getpid(), &v, &r);
  vsz = (size_t)v;
  rss = (size_t)r;
#    endif
#  endif /* EPI_OS_MAJOR */
  mem[0] = vsz*TXpagesize();
  mem[1] = rss*TXpagesize();
  return(1);
#elif defined(__FreeBSD__)                      /* FreeBSD ---------------- */
  kvm_t                 *kd;
  struct kinfo_proc     *ki;
  int                   n;
#  ifndef _POSIX2_LINE_MAX
#    define _POSIX2_LINE_MAX    2048
#  endif
  char                  e[_POSIX2_LINE_MAX];

  kd = kvm_openfiles(_PATH_DEVNULL, _PATH_DEVNULL, _PATH_DEVNULL, O_RDONLY,e);
  if (kd == (kvm_t *)NULL) goto err;
  ki = kvm_getprocs(kd, KERN_PROC_PID, getpid(), &n);
  if (ki == (struct kinfo_proc *)NULL || n != 1)
    {
      kvm_close(kd);
      goto err;
    }
  mem[0] = (size_t)ki[0].kp_eproc.e_vm.vm_map.size;
  mem[1] = (size_t)ki[0].kp_eproc.e_vm.vm_rssize*TXpagesize();
  kvm_close(kd);
  return(1);
#elif defined(HAVE_TASK_INFO)                         /* OS/X ------------------- */
  task_t                        task;
  struct task_basic_info        taskInfo;
  mach_msg_type_number_t        taskInfoCount = TASK_BASIC_INFO_COUNT;
  mach_msg_type_number_t        vmInfoCount = VM_REGION_BASIC_INFO_COUNT_64;
  struct vm_region_basic_info_64 vmInfo;
  vm_address_t                  address = GLOBAL_SHARED_TEXT_SEGMENT;
  vm_size_t                     size;
  mach_port_t                   objName;

  /* Derived from:
   * http://miknight.blogspot.com/2005/11/resident-set-size-in-mac-os-x.html
   * http://dev.alt.textdrive.com/browser/ZOE/Applications/ZOEMenu/AGProcess.m
   */

  if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
    goto err;
  if (task_info(task, TASK_BASIC_INFO, (task_info_t)&taskInfo, &taskInfoCount)
      != KERN_SUCCESS)
    goto err;

  /* RSS appears correct, but VSZ may be too large; correct it: */
  if (vm_region_64(task, &address, &size, VM_REGION_BASIC_INFO,
                   (vm_region_info_t)&vmInfo, &vmInfoCount, &objName) !=
      KERN_SUCCESS)
    goto err;
  mem[0] = taskInfo.virtual_size;
  if (vmInfo.reserved && size == SHARED_TEXT_REGION_SIZE &&
      taskInfo.virtual_size>(SHARED_TEXT_REGION_SIZE+SHARED_DATA_REGION_SIZE))
    mem[0] -= SHARED_TEXT_REGION_SIZE + SHARED_DATA_REGION_SIZE;

  mem[1] = taskInfo.resident_size;
  return(1);
#elif defined(hpux) || defined(__hpux)          /* HPUX ------------------- */
  struct pst_status     pst;

  if (pstat_getproc(&pst, sizeof(pst), (size_t)0, getpid()) < 1) goto err;
  /* ps man page implies we use _SC_PAGE_SIZE
   * (not TxPageSz from pstat_getstatic()) to compute byte size here:
   */
  mem[0]=(size_t)(pst.pst_vtsize+pst.pst_vdsize+pst.pst_vssize)*_SC_PAGE_SIZE;
  mem[1] = (size_t)(pst.pst_tsize+pst.pst_dsize+pst.pst_ssize)*_SC_PAGE_SIZE;
  return(1);
#elif defined(_AIX)                             /* AIX -------------------- */
  struct procsinfo      pst;
  pid_t                 pid;
  EPI_HUGEUINT          sum;

  pid = getpid();
  if (getprocs(&pst, sizeof(pst), NULL, 0, &pid, 1) != 1) goto err;
  /* WTF this VSZ agrees with ps, but the RSS comes out larger than VSZ? */
#  if EPI_OS_SIZE_T_BITS < EPI_HUGEUINT_BITS
  sum = (EPI_HUGEUINT)pst.pi_dvm*(EPI_HUGEUINT)TXpagesize();
  if (sum > (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX) mem[0] = (size_t)EPI_OS_SIZE_T_MAX;
  else mem[0] = (size_t)sum;
  sum = ((EPI_HUGEUINT)(pst.pi_drss + pst.pi_trss))*(EPI_HUGEUINT)TXpagesize();
  if (sum > (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX) mem[1] = (size_t)EPI_OS_SIZE_T_MAX;
  else mem[1] = (size_t)sum;
#  else
  mem[0] = (size_t)(pst.pi_dvm*TXpagesize());
  mem[1] = (size_t)((pst.pi_drss + pst.pi_trss)*TXpagesize());
#  endif
  return(1);
#elif defined(_WIN32)                         /* Windows ---------------- */
  /* The GetProcessMemoryInfo() function is in psapi.dll, which is
   * only standard on Win2k and XP.  Since we don't want to cause an
   * abend by calling it when unavailable, we first check the OS
   * version.  If that fails, we try to manually load psapi.dll in
   * case they've installed it themselves.  We use both checks in case
   * the function moves outside psapi.dll in future releases:
   *
   * KNG 20170113 minimum Windows version for us is now Win2008; skip check.
   */
#  ifndef NO_PSAPI
  PROCESS_MEMORY_COUNTERS       pmc;

  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    goto err;
  mem[0] = (size_t)pmc.PagefileUsage;           /* wtf is this VSZ? */
  mem[1] = (size_t)pmc.WorkingSetSize;          /* wtf is this RSS? */
  return(1);
#  endif /* !NO_PSAPI */
#else                                           /* unknown ---------------- */
  /* fall through to error */
#endif
#if !defined(_WIN32) || !defined(NO_PSAPI)
err:
#endif /* !_WIN32 || !NO_PSAPI */
  mem[0] = mem[1] = (size_t)(-1);
  return(0);
}
#endif /* !(sun && __SVR4) */

/******************************************************************/
/*
	Sets `*diskSpace' to amount of avail/free/total space on filesystem
	containing the file or directory path.  This will be
	trimmed back to a reasonable point.  Bytes.  Returns 0 on error.
	Note that even on success, some `*diskSpace' fields may be -1
	(unknown).  Some may be < 0 legitimately, e.g. `availableBytes'
	when root fills up filesystem.
*/

int
TXgetDiskSpace(const char *path, TXdiskSpace *diskSpace)
{
	char    fname[PATH_MAX];
        int     ret;
        EPI_HUGEINT     denom;
#ifdef _WIN32
	BOOL	rc;
	FARPROC pGetDiskFreeSpaceEx;

	TX_DISKSPACE_INIT(diskSpace);
	TXcatpath(fname, path, "");
	fname[strcspn(fname, TX_PATH_SEP_CHARS_S)] = '\0';

	pGetDiskFreeSpaceEx = GetProcAddress( GetModuleHandle("kernel32.dll"),
						"GetDiskFreeSpaceExA");

	if (pGetDiskFreeSpaceEx)
	{
		__int64 i64FreeBytesToCaller;
		__int64 i64TotalBytes;
		__int64 i64FreeBytes;

		rc = (BOOL)pGetDiskFreeSpaceEx (fname,
				&i64FreeBytesToCaller,
				&i64TotalBytes,
				&i64FreeBytes);
		// Process GetDiskFreeSpaceEx results.
		if(!rc)
			goto err;
		diskSpace->availableBytes = (EPI_HUGEINT)i64FreeBytesToCaller;
		diskSpace->freeBytes = (EPI_HUGEINT)i64FreeBytes;
		diskSpace->totalBytes = (EPI_HUGEINT)i64TotalBytes;
                goto ok;
	}
	else
	{
		DWORD	SectorsPerCluster;
		DWORD	BytesPerSector;
		DWORD	FreeClusters;
		DWORD	Clusters;
		EPI_HUGEINT	BytesPerCluster;

		rc = GetDiskFreeSpace(fname, &SectorsPerCluster, &BytesPerSector,
			&FreeClusters, &Clusters);
		if(!rc)
			goto err;

		BytesPerCluster = (EPI_HUGEINT)BytesPerSector *
			(EPI_HUGEINT)SectorsPerCluster;

		diskSpace->availableBytes = (EPI_HUGEINT)FreeClusters *
			(EPI_HUGEINT)BytesPerCluster;
                goto ok;
	}
#else /* !_WIN32 */
        /* >>>>> NOTE: if this changes see also syssol.c <<<<< */
#  if defined(EPI_HAVE_STATVFS) && !(defined(EPI_PREFER_STATFS) && defined(EPI_HAVE_STATFS))
	struct statvfs sfs;
#  elif defined(EPI_HAVE_STATFS)
	struct statfs sfs;
#  endif
	int rc;

	TX_DISKSPACE_INIT(diskSpace);
	TXcatpath(fname, path, "");
	do
	{
                TXseterror(0);
#  if defined(EPI_HAVE_STATVFS) && !(defined(EPI_PREFER_STATFS) && defined(EPI_HAVE_STATFS))
                rc = statvfs(fname, &sfs);
#  elif defined(EPI_HAVE_STATFS)
		rc = statfs(fname, &sfs EPI_STATFS_EXTRA_ARGS);
#  else
                goto err;                         /* WTF unimplemented */
#  endif
		if(rc == -1)
		{
			char *p;

			p = strrchr(fname, PATH_SEP);
			if(p)
				*p = '\0';
			else
				goto err;
		}
	} while (rc == -1 && (errno == ENOENT || errno == ENOTDIR));
        if (rc == -1) goto err;                   /* error */
#  if defined(EPI_HAVE_STATVFS) && !(defined(EPI_PREFER_STATFS) && defined(EPI_HAVE_STATFS))
        if ((long)sfs.f_frsize <= 0L) goto err;
#    ifdef EPI_HAVE_STATVFS_BAVAIL
	diskSpace->availableBytes = (EPI_HUGEINT)sfs.f_bavail *
		(EPI_HUGEINT)sfs.f_frsize;
#    endif
	diskSpace->freeBytes = (EPI_HUGEINT)sfs.f_bfree *
		(EPI_HUGEINT)sfs.f_frsize;
	diskSpace->totalBytes = (EPI_HUGEINT)sfs.f_blocks *
		(EPI_HUGEINT)sfs.f_frsize;
#  elif defined(EPI_HAVE_STATFS) || defined(EPI_HAVE_STATFS_SZ)
        if ((long)sfs.f_bsize <= 0L) goto err;
#    ifdef EPI_HAVE_STATVFS_BAVAIL
	diskSpace->availableBytes = (EPI_HUGEINT)sfs.f_bavail *
		(EPI_HUGEINT)sfs.f_bsize;
#    endif
	diskSpace->freeBytes = (EPI_HUGEINT)sfs.f_bfree *
		(EPI_HUGEINT)sfs.f_bsize;
	diskSpace->totalBytes = (EPI_HUGEINT)sfs.f_blocks *
		(EPI_HUGEINT)sfs.f_bsize;
#  else
        goto err;
#  endif
#endif /* !_WIN32 */
#ifdef _WIN32
ok:
#endif /* _WIN32 */
	/* %used = (total - free) / (total - (free - avail)) */
	denom = diskSpace->totalBytes -
		(diskSpace->freeBytes - diskSpace->availableBytes);
	if (denom == 0)
		diskSpace->usedPercent = -1.0;
	else
	{
		diskSpace->usedPercent = ((double)(diskSpace->totalBytes -
						   diskSpace->freeBytes)) /
			((double)denom);
		diskSpace->usedPercent *= (double)100.0;
	}
	ret = 1;
	goto finally;
err:
	TX_DISKSPACE_INIT(diskSpace);
	ret = 0;
finally:
	return(ret);
}

/* ------------------------------------------------------------------------- */

TXbool
TXmkdir(TXPMBUF *pmbuf, const char *path, unsigned mode)
/* Returns false on error.
 */
{
  TXbool        ret;

  if (mkdir(path
#ifndef _WIN32
            , mode
#endif /* !_WIN32 */
            ) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + FCE, __FUNCTION__,
                     "Cannot create directory `%s': %s", path,
                     TXstrerror(TXgeterror()));
      goto err;
    }
#ifdef _WIN32
  if (_chmod(path, mode) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + PRM, __FUNCTION__,
                     "Cannot change permissions to %04o on newly created directory `%s': %s; removing",
                     (unsigned)mode, path, TXstrerror(TXgeterror()));
      /* We want to return error on perms failure, so caller knows.
       * So for atomicity, remove the dir too:
       */
      if (_rmdir(path) != 0)
        txpmbuf_putmsg(pmbuf, MERR + FDE, __FUNCTION__,
                       "Cannot remove newly created directory `%s': %s", path,
                       TXstrerror(TXgeterror()));
      goto err;
    }
#endif /* !_WIN32 */
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
}

/* ------------------------------------------------------------------------- */

TXSHMINFO *
TXgetshminfo(key)
int             key;
/* Returns NULL on error, otherwise fills out and returns a static struct.
 */
{
#ifdef _WIN32
  return(TXSHMINFOPN);
#else /* !_WIN32 */
  static TXSHMINFO      si;
  int                   id;
  struct shmid_ds       ds;

  id = shmget(key, 0, 0);
  if (id == -1)                                 /* may be destroyed */
    {
#  if defined(SHM_STAT) && defined(SHM_INFO)
      struct shm_info   info;
      int               nids, i;
      memset(&info, 0, sizeof(info));
      if ((nids = shmctl(0, SHM_INFO, (struct shmid_ds *)(void *)&info)) < 0)
        return(TXSHMINFOPN);                    /* could not list all shms */
      for (i = 0; i <= nids; i++)
        {
          if ((id = shmctl(i, SHM_STAT, &ds)) == -1) continue;
          if (ds.shm_perm.KEYFIELD == key) break;
        }
      if (i > nids)
#  endif /* SHM_STAT && SHM_INFO */
        return(TXSHMINFOPN);
    }
  else if (shmctl(id, IPC_STAT, &ds) != 0)
    return(TXSHMINFOPN);

  memset(&si, 0, sizeof(si));
  si.id = id;
  si.key = key;
  si.perms = 0;
  si.nattach = ds.shm_nattch;
  si.sz = ds.shm_segsz;
  if (ds.shm_perm.mode & SHM_DEST) si.perms |= TXSM_DEST;
  if (ds.shm_perm.mode & SHM_INIT)  si.perms |= TXSM_INIT;
  if (ds.shm_perm.mode & SHM_LOCKED)  si.perms |= TXSM_LOCKED;
  return(&si);
#endif /* !_WIN32 */
}

/* ------------------------------------------------------------------------- */

CONST char *
TXgetExeFileName()
/* Returns file path of currently running executable, looking it up in $PATH
 * if there is no slash in it.
 * Returns NULL on error (e.g. TXsetargv() not called at program start).
 */
{
  CONST char    *argv0;
#ifdef _WIN32
  DWORD         res;
  char          tmp[PATH_MAX];
#endif /* !_WIN32 */

  /* If we already determined the path, return it: */
  if (TxExeFileName != CHARPN) return(TxExeFileName);

  if (!TxOrgArgv || (argv0 = TxOrgArgv[0]) == CHARPN) return(CHARPN);
  /* If argv[0] contains a path separator, then it is absolute or
   * cwd-relative, and no PATH search was used or is needed now:
   */
  if (strchr(argv0, PATH_SEP) != CHARPN) return(argv0);
#ifdef _WIN32
  if (strchr(argv0, '/') != CHARPN) return(argv0);
  /* PATH-determined.  Use GetModuleFileName(); faster than PATH search?: */
  res = GetModuleFileName(NULL, tmp, sizeof(tmp));
  if (res == 0 || res >= sizeof(tmp)) return(CHARPN);
  TxExeFileName = strdup(tmp);
#else /* !_WIN32 */
  /* PATH-determined: */
  TxExeFileName = epipathfindmode((char *)argv0, getenv("PATH"), (X_OK|0x8));
#endif /* !_WIN32 */
  return(TxExeFileName);
}

/* ------------------------------------------------------------------------- */

int
TXgetBooleanOrInt(pmbuf, settingGroup, settingName, val, valEnd, isbool)
TXPMBUF         *pmbuf;         /* (in, opt.) TXPMBUF for messages */
CONST char      *settingGroup;  /* (in, opt.) for msgs ("[Apicp]") */
CONST char      *settingName;   /* (in, opt.) for msgs ("alpostproc") */
CONST char      *val;           /* (in) string to parse */
CONST char      *valEnd;        /* (in, opt.) end of `val' (NULL: strlen) */
int             isbool;         /* (in) flag/control */
/* Returns integer value `val', or 1 if boolean TRUE value, otherwise 0.
 * If `isbool' is 1, returns only non-negative int (< 0 rolled to 0).
 * If `isbool' is 2, returns only boolean (1 or 0).  If `isbool' is 3,
 * same as 2, but returns -1 if not a recognized boolean value or int,
 * and yaps.  If `isbool' is 4, only recognizes string boolean values,
 * and yaps and returns -1 if digits or other text.
 */
{
  static CONST char * CONST     boolstr[] =
  {
    "off",      "on",
    "false",    "true",
    "no",       "yes",
    "n",        "y",
    "disabled", "enabled",
    "disable",  "enable",
    "notok",    "ok",
    "negative", "affirmative",
    "nack",     "ack",
    "nay",      "yea",
    CHARPN
  };
  static CONST char             white[]  = " \t\r\n\v\f";
  int                           n;
  size_t                        len;
  CONST char * CONST            *sp;
  char                          *e;
  CONST char                    *s2;

  if (!valEnd) valEnd = val + strlen(val);
  val += TXstrspnBuf(val, valEnd, white, -1);
  s2 = val;
  if (*s2 == '-') s2++;
  if (*s2 >= '0' && *s2 <= '9')
    {
      int       errnum;

      n = TXstrtoi(val, valEnd, &e, 0, &errnum);
      switch (isbool)
        {
        case 4:  goto maybeYap;
        case 1:  return(n < 0 ? 0 : n);         /* non-negative int */
        case 2:
        case 3:
          /* [TX]strtol() family skips leading but not trailing whitespace: */
          if (e < valEnd) e += TXstrspnBuf(e, valEnd, white, -1);
          if (e < valEnd || errnum) goto maybeYap;
          return(n > 0 ? 1 : 0);
        case 0:                                 /* any int */
        default: return(n);
        }
    }
  len = TXstrcspnBuf(val, valEnd, white, -1);
  for (sp = boolstr; *sp != CHARPN; sp++)
    if (strnicmp(val, *sp, len) == 0 && (*sp)[len] == '\0')
      return((sp - boolstr) & 1);
maybeYap:
  if (isbool >= 3)
    {
      txpmbuf_putmsg(pmbuf, MERR+UGE, CHARPN,
                     "Invalid value `%.*s'%s%s%s%s: Expected boolean%s",
                     (int)(valEnd - val), val,
                     (settingGroup || settingName ? " for " : ""),
                     (settingGroup ? settingGroup : ""),
                     (settingName && settingGroup ? " " : ""),
                     (settingName ? settingName : ""),
                     (isbool <= 3 ? " or int" : ""));
      return(isbool == 2 ? 0 : -1);
    }
  return(0);
}

/* ------------------------------------------------------------------------- */

int
TXgetwinsize(cols, rows)
int     *cols, *rows;
/* Returns current tty window size in `*cols' and `*rows', and 1, or
 * 0 if unknown.  NOTE: should be callable from signal handler.
 */
{
#ifdef EPI_HAVE_TIOCGWINSZ
  struct winsize        ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 ||
      ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0)
    {
      *cols = ws.ws_col;
      *rows = ws.ws_row;
      return(1);
    }
#endif /* EPI_HAVE_TIOCGWINSZ */
  *cols = *rows = -1;
  return(0);
}

/* ------------------------------------------------------------------------- */

char *
TXo_flags2str(buf, bufsz, flags)
char    *buf;   /* (out) buffer to write to */
size_t  bufsz;  /* (in) size of `buf' */
int     flags;  /* (in) flags to write */
/* Writes C-source string for O_... bits in `flags' to `buf'.
 * Returns `buf'.
 * Thread-safe.
 */
{
  static CONST struct
  {
    int         flag;
    char        *s;
  }
  fl[] =
  {                                             /* Typical value: */
    { O_RDONLY,   "O_RDONLY"   },               /* 0x0000 */
    { O_WRONLY,   "O_WRONLY"   },               /* 0x0001 */
    { O_RDWR,     "O_RDWR"     },               /* 0x0002 */
#ifdef O_BINARY
    { O_BINARY,   "O_BINARY"   },               /* 0x8000 Windows */
#endif
#ifdef O_TEXT
    { O_TEXT,     "O_TEXT"     },               /* 0x4000 Windows */
#endif
    { O_CREAT,    "O_CREAT"    },               /* 0x0040 or 0x0100 Windows */
    { O_EXCL,     "O_EXCL"     },               /* 0x0080 or 0x0400 Windows */
#ifdef O_NOCTTY
    { O_NOCTTY,   "O_NOCTTY"   },               /* 0x0100 */
#endif
    { O_TRUNC,    "O_TRUNC"    },               /* 0x0200 */
    { O_APPEND,   "O_APPEND"   },               /* 0x0400 or 0x0008 Windows */
#ifdef O_NONBLOCK
    { O_NONBLOCK, "O_NONBLOCK" },               /* 0x0800 or ? Windows */
#endif
#ifdef O_NDELAY
    { O_NDELAY,   "O_NDELAY"   },               /* O_NONBLOCK */
#endif
#ifdef O_SYNC
    { O_SYNC,     "O_SYNC"     },               /* 0x1000 or ? Windows */
#endif
#ifdef O_FSYNC
    { O_FSYNC,    "O_FSYNC"    },               /* O_SYNC */
#endif
#ifdef O_ASYNC
    { O_ASYNC,    "O_ASYNC"    },               /* 0x2000 or ? Windows */
#endif
#ifdef O_NOINHERIT
    { O_NOINHERIT,"O_NOINHERIT"},               /* 0x0080 Windows */
#endif
#ifdef O_TEMPORARY
    { O_TEMPORARY,"O_TEMPORARY"},               /* 0x0040 Windows */
#endif
  };
#define N       (sizeof(fl)/sizeof(fl[0]))
  char          *d, *e;
  size_t        i;

  d = buf;
  e = buf + bufsz;
  if (O_RDONLY == 0 && !(flags & (O_RDWR|O_WRONLY)))
    {
      if (d < e) d += htsnpf(d, e - d, "O_RDONLY");
    }
  for (i = 0; i < N; i++)
    {
      if (!(fl[i].flag & flags)) continue;
      if (d > buf && d < e) *(d++) = '|';
      if (d < e) d += htsnpf(d, e - d, "%s", fl[i].s);
      flags &= ~fl[i].flag;
      if (flags == 0) break;
    }
  if (flags && d < e)
    {
      if (d > buf && d < e) *(d++) = '|';
      if (d < e) d += htsnpf(d, e - d, "0%o", flags);
    }
  if (d < e) *d = '\0';
  else if (e > d) e[-1] = '\0';
  return(buf);
#undef N
}

int
TXtruncateFile(pmbuf, path, fd, sz)
TXPMBUF         *pmbuf; /* (in) for messages */
CONST char      *path;  /* (in, opt.) for messages */
int             fd;     /* (in) file descriptor */
EPI_OFF_T       sz;     /* (in) size to truncate to */
/* Truncates file corresponding to `fd' to `sz' bytes.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXtruncateFile";
  EPI_STAT_S            st;

  if (path == CHARPN) path = Ques;

#if defined(_WIN32)
#  if EPI_OFF_T_BITS == 64
#    ifdef EPI_HAVE__CHSIZE_S
  /* use MSVS 2005+ builtin _chsize_s(): */
  if (_chsize_s(fd, sz) != 0)
#    else /* !EPI_HAVE__CHSIZE_S */
  /* use our _chsizei64(): */
  if (_chsizei64(fd, sz) != 0)
#    endif /* !EPI_HAVE__CHSIZE_S */
#  else /* EPI_OFF_T_BITS != 64 */
  if (_chsize(fd, (long)(sz)) != 0)
#  endif /* EPI_OFF_T_BITS != 64 */
    {
      txpmbuf_putmsg(pmbuf, MERR + FWE, fn,
                     "Could not truncate file `%s' to 0x%wx bytes: %s",
                     path, (EPI_HUGEUINT)sz, TXstrerror(TXgeterror()));
      return(0);
    }
#elif defined(EPI_FTRUNCATE_BROKEN)
  /* might only be broken on certain kernels, but don't trust it: */
  txpmbuf_putmsg(pmbuf, MERR + FWE, fn,
      "Could not truncate file `%s' to 0x%wx bytes: ftruncate() may be broken",
                 path, (EPI_HUGEUINT)sz);
  return(0);
#else /* !_WIN32 && !EPI_FTRUNCATE_BROKEN */
  if (ftruncate(fd, sz) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR + FWE, fn,
                     "Could not truncate file `%s' to 0x%wx bytes: %s",
                     path, (EPI_HUGEUINT)sz, TXstrerror(TXgeterror()));
      return(0);
    }
#endif /* !_WIN32 && !EPI_FTRUNCATE_BROKEN */
  /* truncate() (even though we use ftruncate()) is broken on some platforms
   * or kernels (e.g. HP-UX), so verify ftruncate()'s action:
   */
  if (EPI_FSTAT(fd, &st) != 0 || (EPI_OFF_T)st.st_size != sz)
    {
      txpmbuf_putmsg(pmbuf, MERR + FTE, fn,
      "Truncate of file `%s' to 0x%wx bytes failed: file is wrong size 0x%wx",
                     path, (EPI_HUGEUINT)sz, (EPI_HUGEUINT)((EPI_OFF_T)st.st_size));
      return(0);
    }
  return(1);                                    /* success */
}

/* ------------------------------------------------------------------------- */

#define talloc(type)    ((type *)malloc(sizeof(type)))
#define isodigit(c)     ((c) >= '0' && (c) <= '7')

static int oatoi ARGS((CONST char *s));
static int
oatoi(s)
CONST char      *s;
{
  int   i;

  if (*s == '\0') return(-1);
  for (i = 0; isodigit(*s); ++s)
    i = i*8 + *s - '0';
  if (*s != '\0') return(-1);
  return(i);
}

TXFMODE *
opentxfmode(s, opmask)
CONST char      *s;
unsigned        opmask;
/* Return a linked list of file mode change operations created from
 * `s', an ASCII mode string that contains either an octal number
 * specifying an absolute mode, or symbolic mode change operations with
 * the form:
 *   [ugoa...][[+-=][rwxXstugo...]...][,...]
 * If no users (ugoa) given, then all bits are affected, but operators
 * in `opmask' (an ORed list of MODE_MASK_... bits for =+-) have the umask
 * applied.  Returns NULL if `s' does not contain a valid representation
 * of file mode change operations, or if there is insufficient memory.
 */
{
  TXFMODE       *head;          /* First element of the linked list. */
  TXFMODE       *change;        /* An element of the linked list. */
  int           i;              /* General purpose temporary. */
  int           umask_value = -1; /* The umask value (surprise). */
  unsigned      affected_bits; /* Which bits in the mode are operated on. */
  unsigned      affected_masked; /* `affected_bits' modified by umask. */
  unsigned      ops_to_mask;    /* Operators to actually use umask on. */
#define ALLMODE (S_ISUID|S_ISGID|S_ISVTX|S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|\
                 S_IWGRP|S_IXGRP|S_IROTH|S_IWOTH|S_IXOTH)

  i = oatoi(s);
  if (i >= 0)
    {
      if (i > 07777) return(TXFMODEPN);
      head = talloc(TXFMODE);
      if (head == TXFMODEPN) return(TXFMODEPN);
      head->next = TXFMODEPN;
      head->op = '=';
      head->flags = 0;
      head->value = i;
      head->mask = 07777;       /* Affect all permissions. */
      return(head);
    }

  change = head = TXFMODEPN;

  /* One loop iteration for each "ugoa...=+-rwxXstugo...[=+-rwxXstugo...]". */
  s--;
  do
    {
      affected_bits = ops_to_mask = 0;
      /* Turn on all the bits in `affected_bits' for each group given. */
      for (s++; ; s++)
        switch (*s)
          {
          case 'u':  affected_bits |= (S_ISUID|S_IRUSR|S_IWUSR|S_IXUSR);break;
          case 'g':  affected_bits |= (S_ISGID|S_IRGRP|S_IWGRP|S_IXGRP);break;
          case 'o':  affected_bits |= (S_ISVTX|S_IROTH|S_IWOTH|S_IXOTH);break;
          case 'a':  affected_bits |= ALLMODE;                          break;
          default:   goto no_more_affected;
          }

    no_more_affected:
      /* If none specified, affect all bits, except perhaps those
       * set in the umask.
       */
      if (affected_bits == 0)
        {
          affected_bits = ALLMODE;
          ops_to_mask = opmask;
        }

      while (*s == '=' || *s == '+' || *s == '-')
        {
          /* Add the element to the tail of the list, so the operations
           * are performed in the correct order.
           */
          if (head == TXFMODEPN)
            {
              head = talloc(TXFMODE);
              if (head == TXFMODEPN) return(TXFMODEPN);
              change = head;
            }
          else
            {
              change->next = talloc(TXFMODE);
              if (change->next == TXFMODEPN) return(closetxfmode(head));
              change = change->next;
            }

          change->next = TXFMODEPN;
          change->op = *s;              /* one of "=+-" */
          affected_masked = affected_bits;
          if (ops_to_mask & (*s == '=' ? MODE_MASK_EQUALS :
                             (*s == '+' ? MODE_MASK_PLUS : MODE_MASK_MINUS)))
            {
              if (umask_value == -1)    /* haven't fetched umask yet */
                {
                  umask_value = umask(0);
                  umask(umask_value);   /* Restore the old value. */
                }
              affected_masked &= ~umask_value;
            }
          change->mask = affected_masked;
          change->value = 0;
          change->flags = 0;

          /* Set `value' according to the bits set in `affected_masked'. */
          for (s++; ; s++)
            switch (*s)
              {
              case 'r':
                change->value |= ((S_IRUSR|S_IRGRP|S_IROTH)&affected_masked);
                break;
              case 'w':
                change->value |= ((S_IWUSR|S_IWGRP|S_IWOTH)&affected_masked);
                break;
              case 'X':
                change->flags |= MODE_X_IF_ANY_X;
                /* Fall through. */
              case 'x':
                change->value |= ((S_IXUSR|S_IXGRP|S_IXOTH)&affected_masked);
                break;
              case 's':
                change->value |= ((S_ISUID|S_ISGID) & affected_masked);
                break;
              case 't':
                change->value |= (S_ISVTX & affected_masked);
                break;
              case 'u':
                if (change->value) goto invalid;
                change->value = (S_IRUSR|S_IWUSR|S_IXUSR);
                change->flags |= MODE_COPY_EXISTING;
                break;
              case 'g':
                if (change->value) goto invalid;
                change->value = (S_IRGRP|S_IWGRP|S_IXGRP);
                change->flags |= MODE_COPY_EXISTING;
                break;
              case 'o':
                if (change->value) goto invalid;
                change->value = (S_IROTH|S_IWOTH|S_IXOTH);
                change->flags |= MODE_COPY_EXISTING;
                break;
              default: goto no_more_values;
              }
        no_more_values:;
        }
    }
  while (*s == ',');
  if (*s == '\0') return(head);
invalid:
  return(closetxfmode(head));
}

unsigned
txfmode_adjust(f, mode)
TXFMODE         *f;
unsigned        mode;
/* Return file permissions `mode', adjusted as indicated by the list
 * of change * operations `f'.  If `mode' is a directory, the type `X'
 * change affects it even if no execute bits were set in `mode'.  The
 * returned value has the S_IFMT bits cleared.
 */
{
  unsigned      newmode;        /* The adjusted mode and one operand. */
  unsigned      value;          /* The other operand. */

  newmode = (mode & 07777);

  for ( ; f != TXFMODEPN; f = f->next)
    {
      if (f->flags & MODE_COPY_EXISTING)
        {
          /* Isolate in `value' the bits in `newmode' to copy, given in
           * the mask `f->value'.
           */
          value = (newmode & f->value);

          if (f->value & (S_IRUSR|S_IWUSR|S_IXUSR))
            /* Copy `u' permissions onto `g' and `o'. */
            value |= (value >> 3) | (value >> 6);
          else if (f->value & (S_IRGRP|S_IWGRP|S_IXGRP))
            /* Copy `g' permissions onto `u' and `o'. */
            value |= (value << 3) | (value >> 3);
          else
            /* Copy `o' permissions onto `u' and `g'. */
            value |= (value << 3) | (value << 6);

          /* In order to change only `u', `g', or `o' permissions,
           * or some combination thereof, clear unselected bits.
           * This can not be done in mode_compile because the value
           * to which the `f->affected' mask is applied depends
           * on the old mode of each file.
           */
          value &= f->mask;
        }
      else
        {
          value = f->value;
          /* If `X', do not affect the execute bits if the file is not a
           * directory and no execute bits are already set.
           */
          if ((f->flags & MODE_X_IF_ANY_X)
              && !S_ISDIR(mode)
              && (newmode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0)
            value &= ~(S_IXUSR|S_IXGRP|S_IXOTH); /* Clear the execute bits. */
        }

      switch (f->op)
        {
        case '=': newmode = ((newmode & ~f->mask) | value); break;
        case '+': newmode |= value;                           break;
        case '-': newmode &= ~value;                          break;
        }
    }
  return(newmode);
}

TXFMODE *
closetxfmode(f)
TXFMODE *f;
{
  TXFMODE       *next;

  while (f != TXFMODEPN)
    {
      next = f->next;
      free(f);
      f = next;
    }
  return(TXFMODEPN);
}

void
txfmode_string(buf, mode, forchmod)
char            *buf;
unsigned        mode;
int             forchmod;
/* Writes a 10-character string, followed by nul, to `buf', that corresponds
 * to the ls-style mode string for st_mode bits `mode'.  If `forchmod' is
 * non-zero, creates a chmod-style string instead; may be up to 20 chars.
 */
{
  char  *s, *d, *e;

  if (forchmod)
    {
      *(buf++) = 'u';
      *(buf++) = '=';
      s = buf;
      if (mode & S_IRUSR) *(buf++) = 'r';
      if (mode & S_IWUSR) *(buf++) = 'w';
      if (mode & S_IXUSR) *(buf++) = ((mode & S_ISUID) ? 's' : 'x');
      else if (mode & S_ISUID) *(buf++) = 'S';
      *(buf++) = ',';
      *(buf++) = 'g';
      *(buf++) = '=';
      e = d = buf;
      if (mode & S_IRGRP) *(buf++) = 'r';
      if (mode & S_IWGRP) *(buf++) = 'w';
      if (mode & S_IXGRP) *(buf++) = ((mode & S_ISGID) ? 's' : 'x');
      else if (mode & S_ISGID) *(buf++) = 'S';
      for ( ; d < buf && *s != ',' && *s == *d; s++, d++);
      if (d == buf && *s == ',')                /* u and g have same perms */
        {
          buf = s + 1;
          for (d = s; *d != '='; d--) *d = d[-1];
          *d = 'g';
          e = d + 2;
        }
      s = e;
      *(buf++) = ',';
      *(buf++) = 'o';
      *(buf++) = '=';
      d = buf;
      if (mode & S_IROTH) *(buf++) = 'r';
      if (mode & S_IWOTH) *(buf++) = 'w';
      if (mode & S_IXOTH) *(buf++) = ((mode & S_ISVTX) ? 't' : 'x');
      else if (mode & S_ISVTX) *(buf++) = 'T';
      for ( ; d < buf && *s != ',' && *s == *d; s++, d++);
      if (d == buf && *s == ',')                /* g and o have same perms */
        {
          if (e[-3] == 'u')                     /* u and g were the same */
            {
              buf = s - 1;
              for (d = e - 2; d < s; d++) *d = d[1];
              e[-3] = 'a';
            }
          else
            {
              buf = s + 1;
              for (d = s; *d != '='; d--) *d = d[-1];
              *d = 'o';
            }
        }
      *buf = '\0';
      return;
    }

  if (S_ISDIR(mode)) *buf = 'd';
#ifdef S_ISBLK
  else if (S_ISBLK(mode)) *buf = 'b';
#endif
#ifdef S_ISCHR
  else if (S_ISCHR(mode)) *buf = 'c';
#endif
#ifdef S_ISREG
  else if (S_ISREG(mode)) *buf = '-';
#endif
#ifdef S_ISFIFO
  else if (S_ISFIFO(mode)) *buf = 'p';
#endif
#ifdef S_ISLNK
  else if (S_ISLNK(mode)) *buf = 'l';
#endif
#ifdef S_ISSOCK
  else if (S_ISSOCK(mode)) *buf = 's';
#endif
#ifdef S_ISMPC
  else if (S_ISMPC(mode)) *buf = 'm';
#endif
#ifdef S_ISNWK
  else if (S_ISNWK(mode)) *buf = 'n';
#endif
  else *buf = '?';
  buf++;
  *(buf++) = ((mode & S_IRUSR) ? 'r' : '-');
  *(buf++) = ((mode & S_IWUSR) ? 'w' : '-');
  *(buf++) = ((mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') :
              ((mode & S_ISUID) ? 'S' : '-'));
  *(buf++) = ((mode & S_IRGRP) ? 'r' : '-');
  *(buf++) = ((mode & S_IWGRP) ? 'w' : '-');
  *(buf++) = ((mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') :
              ((mode & S_ISGID) ? 'S' : '-'));
  *(buf++) = ((mode & S_IROTH) ? 'r' : '-');
  *(buf++) = ((mode & S_IWOTH) ? 'w' : '-');
  *(buf++) = ((mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') :
              ((mode & S_ISVTX) ? 'T' : '-'));
  *buf = '\0';
}

/* ------------------------------------------------------------------------- */

char *
TXfd2file(fd, flags)
int     fd;
int     flags;  /* bit 0: filename must be valid for other processes too */
/* Returns filename corresponding to open file descriptor `fd' if known,
 * else NULL.  Return value is alloc'd.
 */
{
#ifdef _WIN32
  return(CHARPN);
#else
  int   sz;
  char  fil[20+EPI_OS_INT_BITS/3], buf[PATH_MAX];

  htsnpf(fil, sizeof(fil),
         PATH_SEP_S "proc" PATH_SEP_S "self" PATH_SEP_S "fd" PATH_SEP_S "%d",
         fd);
  if ((sz = readlink(fil, buf, sizeof(buf))) < 0 || sz >= PATH_MAX)
    return(CHARPN);
  buf[sz] = '\0';
  if ((flags & 1) &&
      (*buf != PATH_SEP ||
       strnicmp(buf, PATH_SEP_S "proc" PATH_SEP_S, 6) == 0 ||
       strnicmp(buf, PATH_SEP_S "dev" PATH_SEP_S, 5) == 0))
    return(CHARPN);
  return(strdup(buf));
#endif
}

char *
TXbasename(path)
CONST char      *path;
/* Returns pointer to first character after last path or drive separator
 * in `path', or `path' if none.
 * Thread-safe.
 * Signal-safe.
 */
{
  CONST char    *last;

  for (last = path + strlen(path) - 1; last >= path; last--)
    if (*last == PATH_SEP
#ifdef _WIN32
        || *last == '/' || *last == ':'
#endif /* _WIN32 */
        )
      break;
  return((char *)last + 1);
}

size_t
TXdirname(dest, destSz, src)
char            *dest;  /* (out) buffer for directory name */
size_t          destSz; /* (in) size of `dest' */
CONST char      *src;   /* (src) path to parse */
/* Copies directory part of `src' to `dest'.  Will not contain trailing
 * slash, unless needed as leading slash.  May be `.' if no slashes in `src'.
 * Note that return value might NOT be a prefix of `src' (e.g. `.' for `x').
 * Null-terminates `dest'.
 * Returns 0 on error (not enough space) with putmsg, else strlen(dest).
 */
{
  static CONST char     fn[] = "TXdirname";
  CONST char            *bn, *s, *dirSrc;
  size_t                sz;

  bn = TXbasename(src);
  sz = bn - src;
  dirSrc = src;
  if (sz == 0)                                  /* no dir/drive sep in `src'*/
    {
      if (src[0] == '.' && src[1] == '.' && src[2] == '\0')
        sz = 2;
      else                                      /* `.' or just file */
        {
          dirSrc = ".";
          sz = 1;
        }
    }
  else
    {
      /* We can chop the trailing slash if it is not part of the start
       * of the path:
       */
      s = dirSrc = src;
#ifdef _WIN32
      /* Cannot use TX_ISABSPATH(): slash after colon is optional here: */
      if (((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) &&
          s[1] == ':')
        s += 2;
#endif /* _WIN32 */
      if (TX_ISPATHSEP(*s)) s++;
      if (s < bn) sz--;                         /* chop trailing slash */
    }

  /* Copy `sz' bytes of `dirSrc' to `dest': */
  if (sz >= destSz)                             /* dir too large */
    {
      putmsg(MERR + MAE, fn, "Path `%.30s'... too long for %wd-byte buffer",
             src, (EPI_HUGEINT)destSz);
      sz = 0;                                   /* error */
    }
  else
    memcpy(dest, dirSrc, sz);
  if (sz < destSz) dest[sz] = '\0';
  return(sz);
}

char *
TXfileext(path)
CONST char      *path;
/* Returns pointer to trailing file extension in `path' (starting at `.'),
 * or pointer to end of `path' if none.  Note that path `/foo.bar/'
 * (trailing slash) is considered not to have any extension.
 */
{
  CONST char    *p, *last;

  for (p = last = path + strlen(path); p > path; p--)
    if (*p == '.' || *p == PATH_SEP
#ifdef _WIN32
        || *p == '/' || *p == ':'
#endif /* _WIN32 */
        )
      break;
  return(*p == '.' ? (char *)p : (char *)last);
}

char *
TXjoinpath(pmbuf, flags, srcs, numSrcs)
TXPMBUF *pmbuf;         /* (in, opt.) for messages */
int     flags;          /* (in, opt.) flags */
char    **srcs;         /* (in) NULL-terminated array of components to merge*/
size_t  numSrcs;        /* (in) number of items in `srcs' */
/* Concatenates NULL-terminated `srcs' array of molecular path components.
 * Bit `flags':
 *   0x01  Absolute-path components will override previous join, e.g. join of
 *         "foo" "bar" "/baz" yields "/baz", not "foo/bar/baz".
 * Returns alloc'd buffer, or NULL on error.
 */
{
  static CONST char     fn[] = "TXjoinpath";
  char                  *dest = NULL, *newDest;
  CONST char            *src;
  size_t                srcIdx, srcLen, needSz, destUsed = 0, destSz = 0;
  int                   addPathSep;

  for (srcIdx = 0; srcIdx < numSrcs; srcIdx++)
    {
      src = srcs[srcIdx];
      /* See if we need a path separator (`addPathSep'): */
      addPathSep = 0;
      if (TX_ISABSLIKEPATH(src) &&              /* Bug 7136 */
          (flags & 0x1))
        destUsed = 0;                           /* overwrite joined path */
      else if (destUsed > 0 &&
               !TX_ISPATHSEP(dest[destUsed - 1]) &&
               !TX_ISPATHSEP(*src))
        addPathSep = 1;
      else if (destUsed > 0 &&
               TX_ISPATHSEP(dest[destUsed - 1]) &&
               TX_ISPATHSEP(*src))
        src++;                                  /* Bug 7136 second issue */

      /* Compute merge size `needSz': */
      srcLen = strlen(src);
      needSz = destUsed + addPathSep + srcLen + 1;
      if (needSz > destSz)                      /* must realloc */
        {
          destSz += (destSz >> 2) + 128;        /* overalloc */
          if (destSz < needSz) destSz = needSz;
          newDest = TXrealloc(pmbuf, fn, dest, destSz);
          if (!newDest)
            {
#ifndef EPI_REALLOC_FAIL_SAFE
              dest = NULL;
              destSz = 0;
#endif /* !EPI_REALLOC_FAIL_SAFE */
              goto err;
            }
          dest = newDest;
        }

      /* Append the path component: */
      if (addPathSep) dest[destUsed++] = PATH_SEP;
      memcpy(dest + destUsed, src, srcLen + 1);
      destUsed += srcLen;
    }

  /* Condense alloc to just-needed, or expand NULL to empty string: */
  if (dest)
    {
      if (destUsed + 1 < destSz)                /* overalloced */
        {
          destSz = destUsed + 1;
          newDest = (char *)TXrealloc(pmbuf, fn, dest, destSz);
          if (newDest)
            dest = newDest;
#ifndef EPI_REALLOC_FAIL_SAFE
          else                                  /* we lost `dest' */
            {
              dest = NULL;
              destSz = 0;
              goto err;
            }
#endif /* !EPI_REALLOC_FAIL_SAFE */
        }
    }
  else                                          /* return empty, not NULL */
    dest = TXstrdup(pmbuf, fn, "");
  goto done;

err:
  dest = TXfree(dest);
  destSz = 0;
done:
  return(dest);
}

int
TXpathcmpGetDiff(const char     **aPath,
                 size_t         alen,
                 const char     **bPath,
                 size_t         blen)
/* strncmp() for file paths `*aPath' and `*bPath'.  `alen'/`blen' are
 * lengths of `*aPath'/`*bPath' respectively; -1 for strlen().
 * Sets `*aPath'/`*bPath' to first point of difference.
 * Thread-safe.
 * Signal-safe.
 */
{
  const char    *a = *aPath, *b = *bPath, *ao, *bo, *ae, *be, *as, *bs;
  byte          ac = 0, bc = 0;
  int           cmp = 0, aeof = 0, beof = 0;

  if (alen == (size_t)(-1)) ae = (CONST char *)EPI_OS_VOIDPTR_MAX;
  else ae = a + alen;
  if (blen == (size_t)(-1)) be = (CONST char *)EPI_OS_VOIDPTR_MAX;
  else be = b + blen;
  ao = a;                                       /* original `a' and `b' */
  bo = b;

  while (cmp == 0)                              /* while chars the same */
    {
      as = a;
      do
        {
          /* If we reach the end of either string, stop comparing them.
           * But if we hit end of `a' first, keep potentially advancing
           * `b' to EOF below:
           */
          aeof = (a >= ae || *a == '\0');
          if (aeof) break;
          ac = *(byte *)a;
#ifdef EPI_CASE_INSENSITIVE_PATHS               /* fold lower case to upper */
          if (ac >= 'a' && ac <= 'z') ac -= 'a' - 'A';
#endif /* EPI_CASE_INSENSITIVE_PATHS */
#ifdef MSDOS                                    /* canonicalize path sep */
          if (ac == '/') ac = PATH_SEP;
#endif /* MSDOS */
          a++;
        }
      /* Treat consecutive "/" as one, and skip unimportant trailing "/": */
      while (ac == PATH_SEP && ((a >= ae || *a == '\0') ? (as > ao
#ifdef MSDOS
                                                          && as[-1] != ':'
#endif /* MSDOS */
                                                          ) :
                                (*a == PATH_SEP
#ifdef MSDOS
                                 || *a == '/'
#endif /* MSDOS */
                                 )));

      bs = b;
      do
        {
          beof = (b >= be || *b == '\0');
          if (beof) break;
          bc = *(byte *)b;
#ifdef EPI_CASE_INSENSITIVE_PATHS               /* fold lower case to upper */
          if (bc >= 'a' && bc <= 'z') bc -= 'a' - 'A';
#endif /* EPI_CASE_INSENSITIVE_PATHS */
#ifdef MSDOS                                    /* canonicalize path sep */
          if (bc == '/') bc = PATH_SEP;
#endif /* MSDOS */
          b++;
        }
      /* Treat consecutive "/" as one, and skip unimportant trailing "/": */
      while (bc == PATH_SEP && ((b >= be || *b == '\0') ? (bs > bo
#ifdef MSDOS
                                                          && bs[-1] != ':'
#endif /* MSDOS */
                                                          ) :
                                (*b == PATH_SEP
#ifdef MSDOS
                                 || *b == '/'
#endif /* MSDOS */
                                 )));

      if (aeof || beof) break;
      /* Sort "/" before all chars, so that "a" < "a/b" < "a.b"
       * (i.e. "a" sorts *immediately* before its children):
       */
      if (ac == PATH_SEP) ac = 0;
      if (bc == PATH_SEP) bc = 0;
      cmp = ((int)ac - (int)bc);
    }

  if (cmp == 0) cmp = (aeof ? (beof ? 0 : -1) : 1);
  *aPath = (a > ao && !aeof ? a - 1 : a);
  *bPath = (b > bo && !beof ? b - 1 : b);
  return(cmp);
}

int
TXpathcmp(const char    *a,
          size_t        alen,
          const char    *b,
          size_t        blen)
{
  return(TXpathcmpGetDiff(&a, alen, &b, blen));
}

char *
TXcanonpath(path, yap)
CONST char      *path;
int             yap;    /* nonzero: report errors */
/* Returns a malloc'd copy of the full, canonical (no symlinks) `path'.
 * (There really is not a single canonical path under Unix, because any
 * hard link is as valid as another, but at least symlinks are removed.)
 * Used by SYSSCHEDULE table to consistently identify scripts.
 * Thread-unsafe (due to chdir()).
 */
{
  static CONST char     fn[] = "TXcanonpath";
  char                  tpath[PATH_MAX];
  char                  *ret;
#ifndef _WIN32
  int                   i, res;
  CONST char            *p;
  char                  cwd[PATH_MAX];
#endif /* !_WIN32 */

#ifdef EPI_HAVE_REALPATH
  /* realpath() man page says it is especially unsafe if non-allocing,
   * because it might overflow the 2nd arg.  So we prefer alloc version:
   */
  if ((ret = realpath((char *)path,
#  ifdef EPI_HAVE_REALPATH_ALLOC
                      CHARPN
#  else /* !EPI_HAVE_REALPATH_ALLOC */
                      tpath
#  endif /* !EPI_HAVE_REALPATH_ALLOC */
                      )) != CHARPN)             /* success */
    {
#  ifndef EPI_HAVE_REALPATH_ALLOC
      if ((ret = strdup(tpath)) == CHARPN && yap)
        TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, strlen(tpath) + 1, 1);
#  endif /* !EPI_HAVE_REALPATH_ALLOC */
      return(ret);
    }
  /* realpath() may fail if path does not exist; fall back to other methods: */
#endif /* EPI_HAVE_REALPATH */
#if defined(_WIN32)
  _fullpath(tpath, path, sizeof(tpath));
  if ((ret = strdup(tpath)) == CHARPN && yap)
    putmsg(MERR + MAE, fn, OutOfMem);
  return(ret);
#else /* !_WIN32 */
  p = strrchr(path, PATH_SEP);
  if (p == path) goto asis;                     /* 1 leading / e.g. "/file" */

  if (getcwd(cwd, sizeof(cwd)) == CHARPN)       /* getcwd() failed */
    {
      if (yap)
        putmsg(MERR + MAE, fn, "Cannot getcwd(): %s",
               TXstrerror(TXgeterror()));
      goto err;
    }
  if (p == CHARPN) goto cwdcat;                 /* no / e.g. "file" */

  if ((size_t)(p - path) >= sizeof(tpath))
    {
      if (yap) putmsg(MERR + MAE, fn, "Source path too large");
      goto err;
    }
  memcpy(tpath, path, p - path);                /* tpath = dir of `path' */
  tpath[p - path] = '\0';
  res = 0;
  if (chdir(tpath) == 0)                        /* successful cd */
    {
      res++;
      if (getcwd(tpath, sizeof(tpath)) != CHARPN)
        res++;                                  /* `tpath' now canon */
      else if (yap)
        putmsg(MERR + MAE, fn, "Cannot getcwd() in new path: %s",
               TXstrerror(TXgeterror()));
    }
  if (res > 0 && chdir(cwd) != 0 && yap)        /* cd back where we were */
    putmsg(MERR, fn,"Could not return to current dir %s: %s",
           cwd, TXstrerror(TXgeterror()));
  if (res >= 2)                                 /* `tpath' canonicalized ok */
    {
      if (*tpath != PATH_SEP || tpath[1] != '\0')       /* not "/" */
        {
          strcat(tpath, p);                     /* append final file part */
          p = tpath;
        }
    }
  else if (*path != PATH_SEP)                   /* e.g. "dir/file" */
    {
    cwdcat:                                     /* cwd = cwd/path */
      i = strlen(cwd);
      if (i > 0 && cwd[i-1] != PATH_SEP) cwd[i++] = PATH_SEP;
      TXstrncpy(cwd + i, path, sizeof(cwd) - i);
      cwd[sizeof(cwd) - 1] = '\0';
      p = cwd;
    }
  else                                          /* eg "/dir/file" */
    {
    asis:
      p = path;
    }
  /* WTF we have not removed symlinks from final file component */
  if ((ret = strdup(p)) == CHARPN && yap)
    TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, strlen(p) + 1, 1);
  return(ret);
err:
  return(CHARPN);
#endif /* !_WIN32 */
}

char *
TXstrrspn(a, b)
CONST char      *a;
CONST char      *b;
/* Like strspn(a, b), but searches `a' backwards.
 * Returns pointer to trailing part of `a' that is composed entirely of
 * characters in `b' (excluding nuls in both strings).
 */
{
  CONST char    *t;

  for (t = a + strlen(a) - 1; t >= a && strchr(b, *t) != CHARPN; t--);
  return((char *)(t + 1));
}

char *
TXstrrcspn(a, b)
CONST char      *a;
CONST char      *b;
/* Like strcspn(a, b), but searches `a' backwards.
 * Returns pointer to trailing part of `a' that is composed entirely of
 * characters not in `b' (excluding nuls in both strings).
 */
{
  CONST char    *t;

  for (t = a + strlen(a) - 1; t >= a && strchr(b, *t) == CHARPN; t--);
  return((char *)(t + 1));
}

/* ------------------------------------------------------------------------- */

int
TXinitChildProcessManagement(void)
/* Called at process start.
 * Returns 0 on error.
 */
{
  if (TxProcMutex) return(1);                   /* already initialized */
  TxProcMutex = TXmutexOpen(TXPMBUFPN /* must be special */, CHARPN);
  /* Do not sit on `pmbuf' permanently: */
  if (TxProcMutex) TxProcMutex->pmbuf = txpmbuf_close(TxProcMutex->pmbuf);
  return(TxProcMutex != TXMUTEXPN);
}

int
TXsetInProcessWait(int on)
/* Called just before and after a thread calls waitpid() and TXsetprocxit().
 * Thread-safe (iff TX_ATOMIC_THREAD_SAFE).  Signal-safe.
 * Returns 0 on error, i.e. caller is not first.
 */
{
  TXATOMINT     org;
  int           ret;

  if (on)
    {
      org = TX_ATOMIC_INC(&TxWaitDepth);
      ret = (org == 0);                         /* success iff first caller */
    }
  else
    {
      org = TX_ATOMIC_DEC(&TxWaitDepth);
      ret = (org == 1);                         /* success iff outermost */
    }
  return(ret);
}

int
TXaddproc(pid, desc, cmd, flags, xitdesclist, callback, userData)
PID_T                   pid;
char                    *desc, *cmd;
TXPMF                   flags;
CONST char * CONST      *xitdesclist;
TXprocExitCallback      *callback;      /* (in, opt.) exit callback */
void                    *userData;      /* (in, opt.) "" user data */
/* Adds process `pid' with description `desc', path `cmd' to list of
 * misc. forked processes.  Any fork()ed processes that won't be
 * wait()ed for by their creator should be added to this list; a
 * catch-all wait() handler can then look up unknown children in this
 * list with TXgetproc().  `xitdesclist' is optional array of exit code
 * descriptions, NULL-terminated.  Must be constant/permanently alloced.
 * Returns 0 on error.
 * NOTE: processes created with TXpopenduplex() should have TXPDF_REAP
 * set instead of calling this after: avoids race condition.
 * Thread-safe (iff TxInForkFunc set properly).
 */
{
  static const char     fn[] = "TXaddproc";
  TXPROC                *p;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  if (!TxProcMutex)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, TxProcNotInited);
      return(0);
    }
  p = (TXPROC *)TXcalloc(pmbuf, fn, 1,
                         sizeof(TXPROC) + strlen(desc) + strlen(cmd) + 2);
  if (!p) return(0);
  p->pid = pid;
  p->flags = (flags & ~TXPMF_DONE);
  p->sig = p->xit = 0;
  p->xitdesclist = xitdesclist;
  p->desc = (char *)(p + 1);
  strcpy(p->desc, desc);
  p->cmd = p->desc + strlen(p->desc) + 1;
  strcpy(p->cmd, cmd);
  p->callback = callback;
  p->userData = userData;

  if (TXmutexLock(TxProcMutex, -1.0) != 1)
    {                                           /* lock failed */
      p = TXfree(p);
      return(0);
    }
  p->next = TxProcList;
  TxProcList = p;
  TXmutexUnlock(TxProcMutex);

  return(1);
}

int
TXsetprocxit(pid, owner, sig, xit, desc, cmd, xitdesc)
PID_T           pid;
int             owner;          /* non-zero: caller owns process */
int             sig, xit;       /* signal or exit code */
char            **desc;         /* (out, opt.) */
char            **cmd;          /* (out, opt.) */
CONST char      **xitdesc;      /* (out, opt.) */
/* Searches for listed process `pid', setting its signal/exit to `sig'/`xit'.
 * Sets `*desc', `*cmd' and possibly `*xitdesc' on return
 * (saves a call to TXgetprocxit()).  Calls callback if defined.
 * Returns 2 if save process (and not `owner'), 1 if found, 0 if not found.
 * Process marked for deletion; call TXcleanproc() once back in main thread.
 * Signal-safe.  Technically thread-unsafe, but should be ok with threads.
 */
{
  TXPROC                *p;
  CONST char * CONST    *xd;

  for (p = TxProcList; p != TXPROCPN; p = p->next)
    {
      if (p->pid != pid) continue;
      if (owner) p->flags &= ~TXPMF_SAVE;
      p->flags |= TXPMF_DONE;                   /* mark for deletion */
      p->sig = sig;
      p->xit = xit;
      if (desc) *desc = p->desc;
      if (cmd) *cmd = p->cmd;
      if (xitdesc != (CONST char **)NULL)
        {
          if ((xd = p->xitdesclist) != (CONST char * CONST *)NULL &&
              p->xit >= 0 && p->sig == 0)
            {
              for ( ; *xd != CHARPN && (xd - p->xitdesclist) < p->xit; xd++);
              if (*xd != CHARPN && **xd != '\0')
                *xitdesc = *xd;
              else
                *xitdesc = CHARPN;
            }
          else
            *xitdesc = CHARPN;
        }
      if (p->callback) p->callback(p->userData, pid, sig, xit);
      return((p->flags & TXPMF_SAVE) ? 2 : 1);
    }
  if (desc) *desc = CHARPN;
  if (cmd) *cmd = CHARPN;
  if (xitdesc != (CONST char **)NULL) *xitdesc = CHARPN;
  return(0);
}

int
TXgetprocxit(pid, owner, sig, xit, desc, cmd, xitdesc)
PID_T           pid;
int             owner;
int             *sig, *xit;     /* (out, opt.) */
char            **desc;         /* (out, opt.) */
char            **cmd;          /* (out, opt.) */
CONST char      **xitdesc;      /* (out, opt.) */
/* Searches for listed process `pid', setting `*desc', `*cmd', `*sig'/`*xit',
 * `*xitdesc', and unmarking as saved iff `owner'.
 * Returns 2 if exited, 1 if found (still running), 0 if not found.
 * Signal-safe.  Technically thread-unsafe, but should be ok with threads.
 * NOTE: will deadlock if called inside TXsetInProcessWait() block.
 */
{
  TXPROC                *p;
  CONST char * CONST    *xd;

  for (p = TxProcList; p != TXPROCPN; p = p->next)
    {
      if (p->pid != pid) continue;
      if (owner) p->flags &= ~TXPMF_SAVE;
      if (sig) *sig = p->sig;
      if (xit) *xit = p->xit;
      if (desc) *desc = p->desc;
      if (cmd) *cmd = p->cmd;
      if (xitdesc != (CONST char **)NULL)
        {
          if ((xd = p->xitdesclist) != (CONST char * CONST *)NULL &&
              p->xit >= 0 && p->sig == 0)
            {
              for ( ; *xd != CHARPN && (xd - p->xitdesclist) < p->xit; xd++);
              if (*xd != CHARPN && **xd != '\0')
                *xitdesc = *xd;
              else
                *xitdesc = CHARPN;
            }
          else
            *xitdesc = CHARPN;
        }
      return((p->flags & TXPMF_DONE) ? 2 : 1);
    }
  if (sig) *sig = 0;
  if (xit) *xit = 0;
  if (desc) *desc = CHARPN;
  if (cmd) *cmd = CHARPN;
  if (xitdesc != (CONST char **)NULL) *xitdesc = CHARPN;
  return(0);
}

void
TXprocDelete(pid, callback, userData)
PID_T                   pid;            /* (in, opt.) PID to match */
TXprocExitCallback      *callback;      /* (in, opt.) callback to match */
void                    *userData;      /* (in, opt.) user data to match */
/* Deletes processes matching `pid' (if non-zero) or `callback'+`userdata'.
 * Call from main (non-signal-handler) thread.
 * Thread-safe.
 */
{
  static const char     fn[] = "TXprocDelete";
  TXPROC  *p, *prev, *next;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  if (!TxProcMutex)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, TxProcNotInited);
      return;                                   /* error */
    }
  if (TXmutexLock(TxProcMutex, -1.0) != 1)
    return;                                     /* error */

  for (prev = TXPROCPN, p = TxProcList; p != TXPROCPN; prev = p, p = next)
    {
      next = p->next;
      if (pid ? p->pid == pid :
          (p->callback == callback && p->userData == userData))
        {
          /* Matches; delete it: */
          if (prev != TXPROCPN) prev->next = next;
          else TxProcList = next;
          p = TXfree(p);
          p = prev;                             /* so prev doesn't change */
        }
    }
  TXmutexUnlock(TxProcMutex);
}

void
TXcleanproc()
/* Deletes dead processes from list.  Call from main (non-signal-handler)
 * thread.
 * Thread-safe.
 */
{
  static const char     fn[] = "TXcleanproc";
  TXPROC  *p, *prev, *next;
  TXPMBUF       *pmbuf = TXPMBUFPN;

  if (!TxProcMutex)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, TxProcNotInited);
      return;                                   /* error */
    }
  if (TXmutexLock(TxProcMutex, -1.0) != 1)
    return;                                     /* failed */

  for (prev = TXPROCPN, p = TxProcList; p != TXPROCPN; prev = p, p = next)
    {
      next = p->next;
      if ((p->flags & (TXPMF_DONE | TXPMF_SAVE)) != TXPMF_DONE) continue;
      if (prev != TXPROCPN) prev->next = next;
      else TxProcList = next;
      p = TXfree(p);
      p = prev;                                 /* so prev doesn't change */
    }
  TXmutexUnlock(TxProcMutex);
}

void
TXfreeAllProcs()
/* Frees all listed processes.  Called at exit just for MEMDEBUG.
 * Thread-unsafe.  Signal-unsafe.  Call from main thread.
 */
{
  TXPROC  *p, *next;

  p = TxProcList;
  TxProcList = TXPROCPN;                        /* no mucking by signals */
  for ( ; p != TXPROCPN; p = next)
    {
      next = p->next;
      p = TXfree(p);
    }
  TxProcMutex = TXmutexClose(TxProcMutex);
}

int
TXisTexisProg(prog)
CONST char      *prog;  /* (in) path to program */
/* Returns 1 if `prog' is probably a Texis program, 0 if not.
 * Thread-safe.
 * Signal-safe.
 */
{
  /* NOTE: This list must be in sorted order.
   * NOTE: These programs are assumed to return TXEXIT_... exit codes.
   */
  static CONST char * CONST     texisProgs[] =
    {
      /* Uncomment more of these when they use TXEXIT_... consistently: */
      "addtable",
      "anytotx",
      "backref",
      "chkind",
      "copydb",
      "copydbf",
      "cpdb",
      "creatdb",
      /* "dbverify", */
      /* "dumplock", */
      /* "fetch", */
      /* "genserver", */
      /* "geturl", */
      /* "gw", */
      /* "gwpatch", */
      /* "hex", */
      /* "htpf", */
      /* "istxt", */
      "kdbfchk",
      "lockandrun",
      /* "ltest", */
      /* "mdx", */
      /* "mm3e", */
      /* "mmedit", */
      /* "mmhot", */
      /* "mmstrip", */
      /* "mmvec", */
      "monitor",
      /* "monlock", */
      /* "ncg", */
      "odf",
      "pdftotx",
      /* "recon", */
      /* "rex", */
      /* "rmlocks", */
      "tac",
      "texis",
      /* "texisd", */
      "tfchksum",
      /* "timport", */
      /* "timportn", */
      "tsql",
      /* "txtoc", */
      /* "txtocf", */
      /* "vfpatch", */
      /* "vhttpd", */
      /* "wordlist", */
      /* "wsem", */
      /* "xtree", */
    };
#define NUM_PROGS       (sizeof(texisProgs)/sizeof(texisProgs[0]))
  int           l, r, i, cmp;
  CONST char    *baseName;
#ifdef _WIN32
  CONST char    *ext;
#endif /* _WIN32 */
  size_t        baseNameLen;

  baseName = TXbasename(prog);
  baseNameLen = strlen(baseName);
#ifdef _WIN32
  /* Check for `.exe' extension (optional since we can exec without it): */
  ext = TXfileext(baseName);
  if (TXpathcmp(ext, -1, ".exe", 4) == 0) baseNameLen -= 4;
#endif /* _WIN32 */
  l = 0;
  r = NUM_PROGS;
  while (l < r)                                 /* binary search */
    {
      i = ((l + r) >> 1);
      cmp = TXpathcmp(baseName, baseNameLen, texisProgs[i], -1);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else return(1);                           /* found it */
    }
  return(0);                                    /* not found */
#undef NUM_PROGS
}

/******************************************************************/

char *
TXsetlocale(s)
char    *s;
/* Wrapper for setlocale() that saves the current locale, to avoid
 * setlocale() calls to get the locale.  Returns setlocale() return value,
 * which may be NULL if `s' is invalid.  Sets TxLocale to current locale.
 */
{
  char          *rc, *d;
  size_t        n, localeSz = 0;
  int           newSerial = 0;
#ifdef EPI_HAVE_SETLOCALE
  struct lconv  *lc;

  if ((s = rc = setlocale(LC_ALL, s)) == CHARPN &&      /* cannot set */
      (s = setlocale(LC_ALL, CHARPN)) == CHARPN)        /* cannot restore */
    s = "C";                                    /* fallback */
#else /* !EPI_HAVE_SETLOCALE */
  if (*s == '\0' || (*s == 'C' && s[1] == '\0') || strcmp(s, "POSIX") == 0)
    rc = "C";                                   /* recognized */
  else
    rc = CHARPN;                                /* all else unsupported */
  s = "C";                                      /* locale is always "C" */
#endif /* !EPI_HAVE_SETLOCALE */
  if (strcmp(TxLocale, s) != 0)                 /* locale changed */
    {
      localeSz = strlen(s) + 1;
      if (localeSz <= TxLocaleSz)               /* fits existing buffer */
        {
          strcpy(TxLocale, s);
          newSerial = TxLocaleSerial + 1;       /* locale changed */
        }
      else if ((d = strdup(s)) != CHARPN)       /* alloc ok */
        {
          if (TxLocale != TxLocaleBuf) free(TxLocale);
          TxLocale = d;
          TxLocaleSz = localeSz;
          newSerial = TxLocaleSerial + 1;       /* locale changed */
        }
      else                                      /* alloc failed */
        {
#ifdef EPI_HAVE_SETLOCALE
          if (*TxLocale != '\0')                /* if previous locale known */
            setlocale(LC_ALL, TxLocale);        /*   then restore it */
#endif /* EPI_HAVE_SETLOCALE */
          rc = CHARPN;                          /* return failure */
        }
      if (newSerial)                            /* locale changed */
        {
          int   i;

#ifdef EPI_HAVE_SETLOCALE
          lc = localeconv();
          if (lc != (struct lconv *)NULL &&
              (s = lc->decimal_point) != CHARPN)
            {
              n = strlen(s) + 1;
              if (n <= TxDecimalSepSz)          /* fits existing buffer */
                strcpy(TxDecimalSep, s);
              else if ((d = strdup(s)) != CHARPN)       /* alloc ok */
                {
                  if (TxDecimalSep != TxDecimalSepBuf) free(TxDecimalSep);
                  TxDecimalSep = d;
                  TxDecimalSepSz = n;
                }
            }
          else                                  /* call failed */
            *TxDecimalSep = '\0';
#else /* !EPI_HAVE_SETLOCALE */
          TxDecimalSep[0] = '.';                /* assumption */
          TxDecimalSep[1] = '\0';
#endif /* !EPI_HAVE_SETLOCALE */
          /* Bug 5046: roll back to an earlier locale serial if we can
           * match it, so if locale is changed-and-restored between an
           * object's opening and use (e.g. PPM), it might not see the
           * locale (serial) change:
           */
          for (i = 0; i < TX_NUM_OLD_LOCALES; i++)
            {
              if (*(TxOldLocaleBufs[i]) &&
                  strcmp(TxLocale, TxOldLocaleBufs[i]) == 0)
                {                               /* match found */
                  newSerial = TxOldLocaleSerials[i];
                  break;
                }
            }
          if (i >= TX_NUM_OLD_LOCALES &&        /* no match; add to list... */
              localeSz <= TX_OLD_LOCALE_SZ)     /* fits */
            {
              strcpy(TxOldLocaleBufs[TxOldLocaleNextIdx], TxLocale);
              TxOldLocaleSerials[TxOldLocaleNextIdx] = newSerial;
              if (++TxOldLocaleNextIdx >= TX_NUM_OLD_LOCALES)
                TxOldLocaleNextIdx = 0;
            }
        }
    }
  if (newSerial) TxLocaleSerial = newSerial;
  return(rc);
}

/******************************************************************/

char *
TXgetlibpath()
/* Returns unexpanded Lib Path value, e.g. for passing to TXopenlib().
 * Static global: do not free or modify.
 * Thread-unsafe.
 */
{
  return(TxLibPath);
}

int
TXsetlibpath(pmbuf, path)
TXPMBUF         *pmbuf; /* (in, opt.) buffer for putmsgs */
CONST char      *path;
/* Sets Lib Path value.  `path' can be "bin" (deprecated), "sys" (deprecated),
 * or a specific path with optional %-vars, or NULL for the default.
 * Returns 0 on error.
 * Thread-unsafe.
 */
{
  TxLibPathSerial++;                            /* let others know of change*/
  if (TxLibPath != CHARPN && TxLibPath != DefLibPath && TxLibPath != BinDir)
    TXfree(TxLibPath);
  if (path == CHARPN) path = (char *)DefLibPath;
  else if (strcmp(path, "sys") == 0) path = (char *)SysLibPath;
  else if (strcmp(path, "bin") == 0) path = (char *)BinDir;
  if (strcmpi(path, BinDir) == 0) TxLibPath = (char *)BinDir;
  else if (strcmpi(path, DefLibPath) == 0) TxLibPath = (char *)DefLibPath;
  else if ((TxLibPath = strdup(path)) == CHARPN)
    {
      TXputmsgOutOfMem(pmbuf, MERR + MAE, "TXsetlibpath", strlen(path)+1, 1);
      TxLibPath = (char *)DefLibPath;
      return(0);
    }
  return(1);
}

/******************************************************************/

char *
TXgetentropypipe()
{
	char	*d;

	if (TxEntropyPipe != CHARPN) return(TxEntropyPipe);
	if (TxConf != CONFFILEPN &&
	    (d=getconfstring(TxConf,"Texis","Entropy Pipe",CHARPN)) != CHARPN)
		TxEntropyPipe = strdup(d);
	else if ((TxEntropyPipe = (char *)malloc(strlen(TXINSTALLPATH_VAL)+14)) != CHARPN)
	{
		strcpy(TxEntropyPipe, TXINSTALLPATH_VAL);
		strcat(TxEntropyPipe, "/etc/egd-pool");
	}
	if (TxEntropyPipe != CHARPN) return(TxEntropyPipe);
	putmsg(MERR + UGE, "TXgetentropypipe", OutOfMem);
	return(CHARPN);
}

int
TXsetentropypipe(pipePath)
CONST char	*pipePath;
{
	static const char	fn[] = "TXsetentropypipe";
	TXPMBUF			*pmbuf = TXPMBUFPN;

	if (TxEntropyPipe != CHARPN) free(TxEntropyPipe);
	if (pipePath == CHARPN)
		TxEntropyPipe = CHARPN;
	else if ((TxEntropyPipe = TXstrdup(pmbuf, fn, pipePath)) == CHARPN)
		return(0);
	return(1);
}

/******************************************************************/

#ifdef _WIN32

int
TXgetfileinfo(char *filename, LPDWORD ownsize, char *owner, LPDWORD grpsize, char *group)
{
	SECURITY_DESCRIPTOR secdesc[8];
	DWORD copied;
	char domain[128];
	DWORD domsize=127;
	PSID ownpsid, grppsid;
	SID_NAME_USE peuse;
	BOOL defaulted, rc;

	owner[0] = '\0';
	group[0] = '\0';
	InitializeSecurityDescriptor(&secdesc, SECURITY_DESCRIPTOR_REVISION);
	if(!GetFileSecurity(filename, OWNER_SECURITY_INFORMATION |
		GROUP_SECURITY_INFORMATION, &secdesc, sizeof(secdesc), &copied))
		return -1;
	if(copied > sizeof(secdesc))
		return -1;
	rc = GetSecurityDescriptorOwner(&secdesc, &ownpsid, &defaulted);
	rc = GetSecurityDescriptorGroup(&secdesc, &grppsid, &defaulted);
	rc = LookupAccountSid(NULL, ownpsid, owner, ownsize, domain, &domsize, &peuse);
	domsize=127;
	rc = LookupAccountSid(NULL, grppsid, group, grpsize, domain, &domsize, &peuse);
	return 0;
}

/******************************************************************/
/*
	Canonicalize an Object Name

	If Win2K or newer then add "Global\" to the front so that
	Terminal Services etc don't cause problems.
*/

char *
TXGlobalName(TXPMBUF *pmbuf, CONST char *Name)
/* Thread-safe (but check putmsg()).
 */
{
	static CONST char		Fn[] = "TXGlobalName";
	static TXATOMINT                MajorVer = -1;
	OSVERSIONINFO                 osver;
	char                          *rc = (char *)Name;

	if(!Name)
		return NULL;
	if (MajorVer == -1)                                 /* first call */
	{
		osver.dwOSVersionInfoSize = sizeof(osver);
		if(GetVersionEx(&osver))
			MajorVer = osver.dwMajorVersion;
	}
	if(MajorVer >= 5)
	{
		rc = malloc(strlen(Name) + 8);
		if(rc)
		{
			strcpy(rc, "Global\\");
			strcat(rc, Name);
		}
		else
		{
			TX_PUSHERROR();
			txpmbuf_putmsg(pmbuf, MERR + MAE, Fn, OutOfMem);
			TX_POPERROR();
		}
	}
	return rc;
}

int
tx_initsec(pmbuf, sd, sa)
TXPMBUF			*pmbuf;
SECURITY_DESCRIPTOR	*sd;
SECURITY_ATTRIBUTES	*sa;
/* Returns 0 on error.
 * Thread-safe.
 */
{
	static CONST char	Fn[] = "tx_initsec";

	if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION))
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MERR + MAE, Fn,
				"InitializeSecurityDescriptor() failed: %s",
				TXstrerror(saveErr));
		TX_POPERROR();
		return(0);
	}
	/* Initialize security descriptor DACL to allow everyone access: */
	if (!SetSecurityDescriptorDacl(sd, TRUE, NULL, FALSE))
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MERR + MAE, Fn,
				"SetSecurityDescriptorDacl() failed: %s",
				TXstrerror(saveErr));
		TX_POPERROR();
		return(0);
	}
	sa->nLength = sizeof(SECURITY_ATTRIBUTES);
	sa->bInheritHandle = FALSE;
	sa->lpSecurityDescriptor = sd;
	return(1);
}

/******************************************************************/
/*
	Create a Named Pipe with appropriate permissions
	Preserves OS error code.
	Returns INVALID_HANDLE_VALUE on error.
*/

HANDLE
TXCreateNamedPipe(char *name, int yap)
{
	static CONST char Fn[] = "TXCreateNamedPipe";
	SECURITY_ATTRIBUTES secattr;
	SECURITY_DESCRIPTOR secdesc;
	HANDLE	rc;

	if (!tx_initsec((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS),
			&secdesc, &secattr))
		return INVALID_HANDLE_VALUE;
	rc = CreateNamedPipe(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE,
			PIPE_UNLIMITED_INSTANCES, 4096, 4096, 500,
			&secattr);
	if (rc == INVALID_HANDLE_VALUE && yap)
	{
		TX_PUSHERROR();
		putmsg(MERR+MAE, Fn, "CreateNamedPipe(%s) failed: %s",
			(name != CHARPN ? name : "NULL"),
			TXstrerror(saveErr));
		TX_POPERROR();
	}
	return(rc);
}

/******************************************************************/

HANDLE
TXCreateMutex(TXPMBUF *pmbuf, char *name)
/*
	Create (or open if already existing) a Mutex with appropriate
	permissions `name' may be NULL for an unnamed mutex.
	Preserves OS error code.  Returns NULL on error.
*/
{
	SECURITY_ATTRIBUTES secattr;
	SECURITY_DESCRIPTOR secdesc;
	char *oname;
	HANDLE rc;

	if (!tx_initsec(pmbuf, &secdesc, &secattr))
		return(NULL);
	if ((oname = TXGlobalName(pmbuf, name)) == CHARPN && name != CHARPN)
		return(NULL);
	rc = CreateMutex(&secattr, FALSE, oname);
	if (!rc && pmbuf != TXPMBUF_SUPPRESS)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
			"CreateMutex(%s) failed: %s",
			(oname != CHARPN ? oname : "NULL"),
			TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if(oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

HANDLE
TXOpenMutex(TXPMBUF *pmbuf, char *name)
/* Returns NULL on error.
 */
{
	char *oname = NULL;
	HANDLE rc;

	if (!(oname = TXGlobalName(pmbuf, name)) && name)
		return(NULL);
	rc = OpenMutex(SYNCHRONIZE, FALSE /* !inherit */, oname);
	if (!rc && pmbuf != TXPMBUF_SUPPRESS)
	{
		TX_PUSHERROR();
		txpmbuf_putmsg(pmbuf, MERR + MAE, __FUNCTION__,
			"OpenMutex(%s) failed: %s",
			(oname ? oname : "NULL"), TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if(oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

/******************************************************************/
/*
	Create a FileMapping with appropriate permissions
	Preserves OS error code.
	Returns NULL on error.
*/

HANDLE
TXCreateFileMapping(HANDLE hFile, DWORD flProtect, DWORD MaxSizeHi, DWORD MaxSizeLo, char *name, int yap)
{
	static CONST char Fn[] = "TXCreateFileMapping";
	SECURITY_ATTRIBUTES secattr;
	SECURITY_DESCRIPTOR secdesc;
	HANDLE rc;
	char *oname;

	if (!tx_initsec((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS),
			&secdesc, &secattr))
		return(NULL);
	if ((oname = TXGlobalName((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS), name))
			== CHARPN && name != CHARPN)
		return(NULL);
	rc = CreateFileMapping(hFile, &secattr, flProtect,
		MaxSizeHi, MaxSizeLo, oname);
	if (!rc && yap)
	{
		TX_PUSHERROR();
		putmsg(MERR+MAE, Fn,
			"CreateFileMapping(%s) failed: %s",
			(oname != CHARPN ? oname : "NULL"),
			TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if (oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

HANDLE
TXOpenFileMapping(char *name, int yap)
{
	static CONST char	Fn[] = "TXOpenFileMapping";
	HANDLE rc;
	char *oname;

	if ((oname = TXGlobalName((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS), name))
			== CHARPN && name != CHARPN)
		return(NULL);
	rc = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, oname);
	if (!rc && yap)
	{
		TX_PUSHERROR();
		putmsg(MERR+MAE, Fn,
			"OpenFileMapping(%s) failed: %s",
			(oname != CHARPN ? oname : "NULL"),
			TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if (oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

/******************************************************************/
/*
	Create an Event with appropriate permissions
	`name' may be NULL.
	Preserves OS error code.
	Returns NULL on error.
*/

HANDLE
TXCreateEvent(char *name, BOOL ManualReset, int yap)
{
	static CONST char Fn[] = "TXCreateEvent";
	SECURITY_ATTRIBUTES secattr;
	SECURITY_DESCRIPTOR secdesc;
	HANDLE rc;
	char *oname;

	if (!tx_initsec((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS),
			&secdesc, &secattr))
		return(NULL);
	if ((oname = TXGlobalName((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS), name))
			== CHARPN && name != CHARPN)
		return(NULL);
	rc = CreateEvent(&secattr, ManualReset, FALSE, oname);
	if (rc == NULL && yap)
	{
		TX_PUSHERROR();
		putmsg(MERR + MAE, Fn, "CreateEvent(%s) failed: %s",
			(oname != CHARPN ? oname : "NULL"),
			TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if(oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

HANDLE
TXOpenEvent(DWORD Access, BOOL Inherit, char *name, int yap)
/* Thread-safe (but check putmsg()).
 */
{
	static CONST char	Fn[] = "TXOpenEvent";
	HANDLE rc;
	char *oname;

	if ((oname = TXGlobalName((yap ? TXPMBUFPN : TXPMBUF_SUPPRESS), name))
			== CHARPN && name != CHARPN)
		return(NULL);
	rc = OpenEvent(Access, Inherit, oname);
	if (rc == NULL && yap)
	{
		TX_PUSHERROR();
		putmsg(MERR + MAE, Fn, "OpenEvent(%s) failed: %s",
			(oname != CHARPN ? oname : "NULL"),
			TXstrerror(saveErr));
		if (oname != name) oname = TXfree(oname);
		TX_POPERROR();
		return(rc);
	}
	if(oname != name)
	{
		TX_PUSHERROR();
		oname = TXfree(oname);
		TX_POPERROR();
	}
	return rc;
}

/******************************************************************/

char *
TXstrerror(err)
int	err;
/* WTF thread-unsafe
 * Note: see also rex.c, atomicTests.c standalone implementations
 */
{
	static char	MsgBuffer[4][256];
	static TXATOMINT	bufn = 0;
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
		const char	*errName;

		errName = TXgetOsErrName(err, NULL);
		if (errName)
			TXstrncpy(buf, errName, end - buf);
		else
			htsnpf(buf, end - buf, "ERROR_%d", err);
	}
	/* Trim trailing CRLF and period: */
	for (s = buf + strlen(buf) - 1; s >= buf; s--)
		if (*s == '\r' || *s == '\n') *s = '\0';
		else break;
	if (s > buf && *s == '.' && s[-1] >= 'a' && s[-1] <= 'z') *s = '\0';
	return buf;
}
#endif /* _WIN32 */

/******************************************************************/

int
tx_parsesz(pmbuf, s, szp, setting, bitsz, byteSuffixOk)
TXPMBUF         *pmbuf;         /* (in) (opt.) for putmsgs */
CONST char      *s;             /* (in) string to parse */
EPI_HUGEINT     *szp;           /* (out) number */
CONST char      *setting;       /* (in) setting (for putmsgs) */
int             bitsz;          /* (in) bit size of final stored type */
TXbool          byteSuffixOk;   /* (in) `KB' etc. accepted as well as `K' */
/* Converts numeric string `s' to integer `*szp'.  Groks stuff like
 * "1K", "2M", etc. as well as decimal/hex.  Single commas between digits
 * allowed.  Leading zeroes do not indicate octal.  Maps < 0 to -1.
 * `bitsz' is #bits in ultimate stored type, for overflow check.
 * Returns 0 on parse error.
 */
{
  static CONST char white[] = " \t\r\n\v\f";
  char          *e;
  CONST char    *s2;
  EPI_HUGEINT   h, sz;
  int           bits, shift, errnum;

  /* Skip leading 0s to avoid octal interpretation: */
  s2 = s;
  while (*s2 != '\0' && strchr(white, *s2) != CHARPN) s2++;
  while (*s2 == '0') s2++;
  if (s2 > s) s2--;
  h = TXstrtoh(s2, CHARPN, &e, (0 | TXstrtointFlag_Base10CommasOk), &errnum);
  if (s == e || errnum != 0) goto gerr;
  if (h < (EPI_HUGEINT)0) sz = (EPI_HUGEINT)(-1);
  else sz = h;
  shift = 0;
  e += strspn(e, white);
  if (strcmpi(e, "K") == 0 ||
      (byteSuffixOk && strcmpi(e, "KB") == 0))
    shift = 10;
  else if (strcmpi(e, "M") == 0 ||
           (byteSuffixOk && strcmpi(e, "MB") == 0))
    shift = 20;
  else if (strcmpi(e, "G") == 0 ||
           (byteSuffixOk && strcmpi(e, "GB") == 0))
    shift = 30;
  else if (strcmpi(e, "T") == 0 ||
           (byteSuffixOk && strcmpi(e, "TB") == 0))
    shift = 40;
  else if (strcmpi(e, "P") == 0 ||
           (byteSuffixOk && strcmpi(e, "PB") == 0))
    shift = 50;
  else if (strcmpi(e, "E") == 0 ||
           (byteSuffixOk && strcmpi(e, "EB") == 0))
    shift = 60;
  else if (*e != '\0' && strchr(white, *e) == CHARPN)
    {
    gerr:
      txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
                     "Integer value `%s' garbled for `%s'",
                     s, setting);
      return(0);
    }
  if (shift && h > (EPI_HUGEINT)0)
    {
      for (bits = EPI_HUGEINT_BITS-1;
           bits && !(sz & ((EPI_HUGEINT)1 << bits));
           bits--)
        ;
      if (bits + shift >= bitsz)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
                 "Value `%s' would overflow for `%s'", s, setting);
          return(0);
        }
      sz <<= shift;
    }
  *szp = sz;
  return(1);
}

int
TXprintSz(char *buf, size_t bufSz, EPI_HUGEINT num)
/* NOTE: this should correspond with tx_parsesz().
 * Returns 0 on error (`buf' too short).
 */
{
  const char    *suffix = "";

#define MASK(n) (((EPI_HUGEINT)1 << (n)) - 1)
  if ((num & MASK(60)) == (EPI_HUGEINT)0)
    {
      suffix = "E";
      num >>= 60;
    }
  else if ((num & MASK(50)) == (EPI_HUGEINT)0)
    {
      suffix = "P";
      num >>= 50;
    }
  else if ((num & MASK(40)) == (EPI_HUGEINT)0)
    {
      suffix = "T";
      num >>= 40;
    }
  else if ((num & MASK(30)) == (EPI_HUGEINT)0)
    {
      suffix = "G";
      num >>= 30;
    }
  else if ((num & MASK(20)) == (EPI_HUGEINT)0)
    {
      suffix = "M";
      num >>= 20;
    }
  else if ((num & MASK(10)) == (EPI_HUGEINT)0)
    {
      suffix = "K";
      num >>= 10;
    }
  if ((size_t)htsnpf(buf, bufSz, "%wkd%s", num, suffix) >= bufSz)
    return(0);
  else
    return(1);
#undef MASK
}

/****************************************************************************/

char **
TXlib_freepath(list, n)
char    **list;
size_t  n;
/* Frees return value of TXlib_expandpath().
 */
{
  size_t        i;

  if (list == CHARPPN) return(CHARPPN);
  for (i = 0; i < n; i++)
    if (list[i] != CHARPN) TXfree(list[i]);
  TXfree(list);
  return(CHARPPN);
}

static void txlib_zappath ARGS((char *path));
static void
txlib_zappath(path)
char    *path;
/* Removes PATH_DIV (path separator character) from both ends of `path'.
 */
{
  size_t        n, sz;

  sz = strlen(path);
  for (n = 0; path[n] == PATH_DIV; n++);
  sz -= n;
  if (n > 0) memmove(path, path + n, sz + 1);
  while (sz > 0 && path[sz - 1] == PATH_DIV) path[--sz] = '\0';
}

size_t
TXlib_expandpath(path, listp)
CONST char      *path;
char            ***listp;
/* Creates malloc'd list of path(s) derived from `path'.
 * %-vars are expanded.  Each time %SYSLIBPATH% is encountered, a NULL
 * element is entered in the list for it.  Sets `*listp' to alloced list.
 * Returns number of elements.
 */
{
  static CONST char     fn[] = "TXlib_expandpath";
  static CONST char     pct[] = "%";
  HTBUF                 *buf;
  CONST char            *s, *e, *exeFileName;
  size_t                n, nel = 0, sz;
  char                  **list = CHARPPN, *data;

  if (TxTraceLib & 0x1)
    putmsg(MINFO, fn, "Expanding lib path `%s'", path);

  if ((buf = openhtbuf()) == HTBUFPN) goto err;
  if ((list = (char **)TXcalloc(TXPMBUFPN, fn, 1, sizeof(char *))) == CHARPPN)
    goto err;
  for (s = path; *s != '\0'; s++)
    {
      n = strcspn(s, pct);
    thunk:
      if (n > 0 && !htbuf_write(buf, s, n)) goto err;
      s += n;
      if (*s == '\0') break;                    /* end of source string */
      s++;                                      /* skip the `%' */
      n = strcspn(s, pct);
      if (s[n] != '%')                          /* incomplete %-var */
        {
          s--;
          n++;
          goto thunk;
        }
      if (n == 10 && strnicmp(s, "INSTALLDIR", 10) == 0)
        {
          if (!htbuf_pf(buf, "%s", TXINSTALLPATH_VAL)) goto err;
        }
      else if (n == 6 && strnicmp(s, "BINDIR", 6) == 0)
        goto doBinDir;
      else if (n == 6 && strnicmp(s, "EXEDIR", 6) == 0)
        {
          if ((exeFileName = TXgetExeFileName()) != CHARPN)
            {
              e = TXbasename(exeFileName);
              /* Skip last `/', if it is not the first char: */
              if (e > exeFileName + 1 &&
                  (e[-1] == PATH_SEP
#ifdef _WIN32
                   || e[-1] == '/'
#endif /* _WIN32 */
                   ))
                e--;
              if (!htbuf_write(buf, exeFileName, e - exeFileName))
                goto err;
            }
          else                                  /* fallback to %BINDIR% */
            {
            doBinDir:
#ifdef _WIN32
              if (!htbuf_pf(buf, "%s", TXINSTALLPATH_VAL))
                goto err;
#else /* !_WIN32 */
              if (!htbuf_pf(buf, "%s" PATH_SEP_S "bin", TXINSTALLPATH_VAL))
                goto err;
#endif /* !_WIN32 */
            }
        }
      else if (n == 10 && strnicmp(s, "SYSLIBPATH", 10) == 0)
        {
          sz = htbuf_getdata(buf, &data, 1);    /* flush existing path */
          if (sz > 0)
            {
              txlib_zappath(data);
              list[nel++] = data;
            }
          else
            TXfree(data);
          list = (char **)TXrealloc(TXPMBUFPN, fn, list,
                                    (nel + 2)*sizeof(char *));
          if (list == CHARPPN) goto err;
          list[nel++] = CHARPN;                 /* for %SYSLIBPATH% */
          list[nel] = CHARPN;
        }
      else if (n == 0)                          /* %% */
        goto asis;
      else                                      /* unknown %-var */
        {
          s--;                                  /* include starting `%' */
          n++;
        asis:
          if (!htbuf_write(buf, s, n + 1)) goto err;
        }
      s += n;
    }
  sz = htbuf_getdata(buf, &data, 1);
  if (nel == 0 || sz > 0)
    {
      if (data == CHARPN &&
          (sz = 0, data = TXstrdup(TXPMBUFPN, fn, "")) == CHARPN)
        goto err;
      txlib_zappath(data);
      list[nel++] = data;
    }
  goto done;

err:
  list = TXlib_freepath(list, nel);
  nel = 0;
done:
  closehtbuf(buf);
  *listp = list;
  return(nel);
}

TXLIB *
TXopenlib(file, path, flags, pmbuf)
const char      *file;  /* file name to load, e.g. "libc.so" */
const char      *path;  /* required PATH to search for `file' */
int             flags;  /* (in) bit flags */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
/* Loads a dynamic library from `file'.  If `file' is not an absolute path,
 * `path' is searched.  The following %-vars are automatically replaced:
 *   %INSTALLDIR%       Texis install dir
 *   %BINDIR%           Texis binary dir
 *   %EXEDIR%           Running executable's dir
 *   %SYSLIBPATH%       Platform-dependent search path
 * `flags' is a set of bit flags:
 * 0x1: issue error putmsgs
 * 0x2: set RTLD_GLOBAL so library can be used to resolve later dependencies
 * 0x4: issue tracing putmsgs
 * 0x8: set RTLD_MEMBER (AIX) iff `file' syntax is "lib.a(lib.so)"
 * Returns NULL on error.
 */
{
  TXLIB                 *lib = NULL;
  const char            *f = NULL, *err = NULL, *p1 = NULL, *p2 = NULL;
  const char            *mf, *baseName, *curpath = NULL;
  char                  *s, **list = NULL, *xtra1, *xtra2, *xtra3;
  char                  *fileBase = (char*)file;/*`file' copy w/o "(lib.so)"*/
  const char            *fileMember = NULL;     /* "(lib.so)" part of `file'*/
  size_t                listLen = 0, listIdx;
  char                  *lastLoadErrMsg = NULL;         /* alloc'd */
  char                  *lastLoadErrFile = NULL;        /* alloc'd */
  const char            *lastLoadErrPath = NULL;        /* list[] element */
  const char            *reportMsg, *reportFile, *reportPath;
#ifdef EPI_HAVE_DLOPEN
  int                   dlFlags;
#  ifdef RTLD_MEMBER
  size_t                n;
#  endif /* RTLD_MEMBER */
#endif /* EPI_HAVE_DLOPEN */
#define MSGINFOLOAD     if (flags & 4)                                  \
  txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__, "Loading %s%s%s%s",        \
                 f, xtra2, xtra3, xtra1)

  if (TxTraceLib & 0x2) flags |= 0x4;

  if ((listLen = TXlib_expandpath(path, &list)) == 0)
    {
      err = TXstrerror(TXgeterror());
      goto chkerr;
    }
  if (flags & 0x8)                              /* use RTLD_MEMBER iff "()" */
    {
      /* For flexibility, bit 3 only causes RTLD_MEMBER if the platform
       * supports it *and* the "lib.a(lib.so)" syntax was used; this
       * saves the caller from having to potentially check its `file' arg:
       */
#if defined(EPI_HAVE_DLOPEN) && defined(RTLD_MEMBER)
      /* Remove "(lib.so)" part of "lib.a(lib.so)" from `file' for
       * epipathfindmode():
       */
      fileMember = strrchr(file, '(');
      if (fileMember != CHARPN)                 /* "lib.a(lib.so)" syntax */
        {
          n = fileMember - file;
          if (!(fileBase = (char *)TXmalloc(pmbuf, __FUNCTION__, n + 1)))
            {
              err = TXstrerror(TXgeterror());
              goto chkerr;
            }
          memcpy(fileBase, file, n);
          fileBase[n] = '\0';
        }
#endif /* EPI_HAVE_DLOPEN && RTLD_MEMBER */
    }
  for (listIdx = 0; !lib && listIdx < listLen; listIdx++)
    {                                           /* try each sub-path */
      curpath = list[listIdx];
      if (f && f != fileBase && f != file) f = TXfree((char *)f);
      f = file;                                 /* i.e. *with* `fileMember' */
      if (curpath)
        {
          f = epipathfindmode((char *)fileBase, (char *)curpath, 0x8);
          if (!f)                               /* did not find the library */
            {
              /* Use strerror instead of TXstrerror() for consistent
               * cross-platform "No such file or directory" error
               * message for Vortex tests:
               */
              err = strerror(errno);
              if (flags & 0x4)
                txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
                               "Looking for %s in path `%s': Failed: %s",
                               fileBase, curpath, err);
              continue;
            }
          else                                  /* found the library */
            {
              if (flags & 0x4)
                txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
                               "Looking for %s in path `%s': Found %s",
                               fileBase, curpath, f);
              /* Re-attach "(lib.so)" member part: */
              if (fileMember)
                {
                  s = (char *)TXmalloc(pmbuf, __FUNCTION__,
                                       strlen(f) + strlen(fileMember) + 1);
                  if (!s)
                    {
                      err = TXstrerror(TXgeterror());
                      goto chkerr;
                    }
                  strcpy(s, f);
                  strcat(s, fileMember);
                  f = TXfree((char *)f);
                  f = s;
                }
            }
        }
      xtra1 = (curpath ? "" : " using system-dependent path");
#if defined(_WIN32) /* - - - - - - - - - - - - - - - - - - - - - - - - - */
      xtra2 = xtra3 = "";
      MSGINFOLOAD;
      lib = LoadLibrary(f);
      err = TXstrerror(TXgeterror());
#elif defined(EPI_HAVE_DLOPEN) /* - - - - - - - - - - - - - - - - - - - - - */
      dlFlags = RTLD_NOW;                       /* so we don't blow up later*/
      xtra2 = " with flags RTLD_NOW";
      xtra3 = "";
#  ifdef RTLD_GLOBAL
      if (flags & 0x2)
        {
          xtra2 = " with flags RTLD_NOW | RTLD_GLOBAL";
          dlFlags |= RTLD_GLOBAL;
        }
#  endif /* RTLD_GLOBAL */
#  ifdef RTLD_MEMBER
      if (fileMember)
        {
          xtra3 = " | RTLD_MEMBER";
          dlFlags |= RTLD_MEMBER;
        }
#  endif /* RTLD_MEMBER */
      MSGINFOLOAD;
      lib = dlopen(f, dlFlags);
      err = dlerror();
#elif defined(EPI_HAVE_SHL_LOAD) /* - - - - - - - - - - - - - - - - - - - - */
      xtra2 = " with flags BIND_IMMEDIATE";
      xtra3 = "";
      MSGINFOLOAD;
      lib = (TXLIB *)shl_load(f, BIND_IMMEDIATE, 0L);
      err = TXstrerror(TXgeterror());
#elif defined(__MACH__) /* OS/X - - - - - - - - - - - - - - - - - - - - - - */
      {
        NSObjectFileImage   img;
        NSLinkEditErrors    ler;
        int                 lerno;
        CONST char          *fil;

        lib = NULL;
        xtra2 = " with flags NSLINKMODULE_OPTION_BINDNOW | NSLINKMODULE_OPTION_RETURN_ON_ERROR";
        xtra3 = "";
        MSGINFOLOAD;
        if (NSCreateObjectFileImageFromFile(f, &img) !=
            NSObjectFileImageSuccess)
          err = TXstrerror(TXgeterror());
        else
          {
            /* This could terminate the process if it fails... limited API: */
            lib = NSLinkModule(img, f, (NSLINKMODULE_OPTION_BINDNOW |
                                        NSLINKMODULE_OPTION_RETURN_ON_ERROR));
            if (!lib)
              NSLinkEditError(&ler, &lerno, &fil, &err);
            else
              NSDestroyObjectFileImage(img);
          }
      }
#else /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
      xtra2 = xtra3 = "";
      MSGINFOLOAD;
      err = TXunsupportedPlatform;
#endif /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
      if (!lib)                                 /* load failed */
        {
          /* Save the latest error from an actual load: it is more
           * important than a later error from epipathfindmode():
           */
          lastLoadErrMsg = TXfree(lastLoadErrMsg);
          lastLoadErrMsg = TXstrdup(pmbuf, __FUNCTION__,
                                    (err ? err : "Unknown error"));
          lastLoadErrFile = TXfree(lastLoadErrFile);
          lastLoadErrFile = TXstrdup(pmbuf, __FUNCTION__, f);
          lastLoadErrPath = curpath;
          if (flags & 0x4)
            txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__, "Load failed: %s",
                           err);
        }
    }
  if (lib && (flags & 0x4))
    txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
                   "Load successful: lib handle is %p", lib);

chkerr:
  if (!lib && (flags & 0x1))
    {                                           /* report error */
      char              *noEntErrMsg, *d, *e;
      const char        *pathForMsg;
      char              mergedPath[1024];

      /* Merge expanded `list' into one search path for message: */
      d = mergedPath;
      e = mergedPath + sizeof(mergedPath);
      *d = '\0';
      for (listIdx = 0; list && listIdx < listLen && d < e; listIdx++)
        {
          if (d > mergedPath) *(d++) = ':';
          pathForMsg = (list[listIdx] ? list[listIdx] : SysLibPath);
          d += htsnpf(d, e - d, "%s", pathForMsg);
        }
      if (d > e)
      {
        char *TruncationPoint = mergedPath + sizeof(mergedPath) - 4;
        strcpy(TruncationPoint, "...");
      }

      /* A non-file-not-found error from an actual load is more
       * important that file-not-found from a later epipathfindmode():
       */
      if (lastLoadErrMsg &&
          lastLoadErrFile &&
          (noEntErrMsg = strerror(ENOENT)) != NULL &&
          !strstr(lastLoadErrMsg, noEntErrMsg))
        {
          reportMsg = lastLoadErrMsg;
          reportFile = lastLoadErrFile;
          reportPath = lastLoadErrPath;
        }
      else
        {
          reportMsg = (err ? err : "Unknown error");
          reportFile = f;
          reportPath = curpath;
        }
      mf = fileBase;
      p1 = p2 = "";
      /* Use full path to lib in message, if not present and possible: */
      baseName = TXbasename(fileBase);
#ifdef EPI_CASE_INSENSITIVE_PATHS
      s = strstri((char *)reportMsg, (char *)baseName);
#else /* !EPI_CASE_INSENSITIVE_PATHS */
      s = strstr(reportMsg, baseName);
#endif /* !EPI_CASE_INSENSITIVE_PATHS */
      if (!s || s == reportMsg ||
          (!s[-1] || !strchr(TX_PATH_SEP_CHARS_S, s[-1])))
        {                                       /* full path not in err msg */
          if (reportFile &&
              reportFile[strcspn(reportFile, TX_PATH_SEP_CHARS_S)])
            mf = reportFile;                    /* `reportErrFile' has path */
          else if (reportPath && *reportPath)
            {
              if (!strchr(reportPath, PATH_DIV))
                {                               /* `reportPath' single dir */
                  p1 = reportPath;
                  p2 = PATH_SEP_S;
                }
              else
                {
                  p1 = mf;
                  p2 = " from path ";
                  mf = reportPath;
                }
            }
          else                                  /* no path: system-dep. */
            {
              p1 = mf;
              p2 = " from system-dependent path";
              mf = "";
            }
        }
      txpmbuf_putmsg(pmbuf, MERR + FRE, __FUNCTION__,
   "Cannot load dynamic library %s%s%s while searching library path `%s': %s",
                     p1, p2, mf, mergedPath, reportMsg);
    }
  if (f && f != fileBase && f != file) f = TXfree((char *)f);
  TXlib_freepath(list, listLen);
  if (fileBase && fileBase != file) fileBase = TXfree(fileBase);
  lastLoadErrMsg = TXfree(lastLoadErrMsg);
  lastLoadErrFile = TXfree(lastLoadErrFile);
  return(lib);
}

TXLIB *
TXcloselib(lib)
TXLIB   *lib;
/* Frees library `lib'.
 * Returns NULL.
 */
{
  if (lib == TXLIBPN) goto done;
#if defined(_WIN32)
  FreeLibrary(lib);
#elif defined(EPI_HAVE_DLOPEN)
  dlclose(lib);
#elif defined(EPI_HAVE_SHL_LOAD)
  shl_unload((shl_t)lib);
#elif defined(__MACH__)
  /* NSUnLinkModule(lib, NSUNLINKMODULE_OPTION_NONE); */        /* wtf */
#else /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
#endif /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
done:
  return(TXLIBPN);
}

int
TXopenLibs(const char *libs, const char *path, int flags, TXPMBUF *pmbuf)
/* Loads whitespace/CSV list `libs'.  Note that handles are orphaned.
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXopenLibs";
  const char            *s, *e;
  int                   ret = 1;
  char                  libName[1024];

  for (s = libs; *s; s = e)
    {
      s += strspn(s, Whitespace);
      if (!*s) break;
      e = s + strcspn(s, Whitespace);
      if ((size_t)(e - s) >= sizeof(libName))
        {
          txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                         "Lib name `%.*s' too large (%wd bytes), skipped",
                         (int)(e - s), s, (EPI_HUGEINT)(e - s));
          ret = 0;
          continue;
        }
      memcpy(libName, s, e - s);
      libName[e - s] = '\0';
      TXopenlib(libName, path, flags, pmbuf);
    }
  return(ret);
}

void *
TXlib_getaddr(lib, pmbuf, name)
TXLIB           *lib;   /* (in) loaded library to get symbol address from */
TXPMBUF         *pmbuf; /* (out) (optional) putmsg buffer */
CONST char      *name;  /* symbol name to look up */
/* Returns symbol address, or NULL on error.
 */
{
  static CONST char     fn[] = "TXlib_getaddr";
  void                  *addr;
  CONST char            *err;

#if defined(_WIN32)
  addr = (void *)GetProcAddress(lib, name);
  err = TXstrerror(TXgeterror());
#elif defined(EPI_HAVE_DLOPEN)
  addr = dlsym(lib, name);
  err = dlerror();
#elif defined(EPI_HAVE_SHL_LOAD)
  shl_t ret;
  ret = (shl_t)lib;
  addr = NULL;
  if (shl_findsym(&ret, name, TYPE_UNDEFINED, &addr) != 0)
    {
      addr = NULL;
      err = strerror(errno);
    }
#elif defined(__MACH__)
  addr = NSAddressOfSymbol(NSLookupSymbolInModule((NSModule)lib, name));
  if (addr == NULL)
    err = strerror(errno);              /* wtf */
#else /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
  addr = NULL;
  err = TXunsupportedPlatform;
#endif /* !_WIN32 && !EPI_HAVE_DLOPEN && !EPI_HAVE_SHL_LOAD && !__MACH__ */
  if (TxTraceLib & 0x4)
    txpmbuf_putmsg(pmbuf, MINFO, fn, "Lib %p symbol %s: address is %p",
                   lib, name, addr);
  if (!addr)
    txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
                   "Cannot get symbol `%s' from dynamic library: %s",
                   name, err);
  return(addr);
}

size_t
TXlib_getaddrs(lib, pmbuf, names, addrs, num)
TXLIB                   *lib;
TXPMBUF                 *pmbuf; /* (out) (optional) putmsg buffer */
CONST char * CONST      *names; /* array of `num' symbol names to look up */
void                    **addrs;/* parallel array of `num' return addresses */
size_t                  num;
/* Looks up each element of `names' and sets corresponding `addrs' element
 * to that symbol's address.  Returns number of symbols successfully found.
 */
{
  size_t        i, ret;

  for (i = ret = 0; i < num; i++)
    if ((addrs[i] = TXlib_getaddr(lib, pmbuf, names[i])) != NULL)
      ret++;
  return(ret);
}

/****************************************************************************/

typedef struct TXALARM_tag	TXALARM;
#define TXALARMPN		((TXALARM *)NULL)

struct TXALARM_tag
{
	TXALARM		*next;
	double		abstime;		/* absolute time of alarm */
	TXALARMFUNC	*func;
	void		*usr;
};

static TXALARM	*TxAlarms = TXALARMPN;		/* ascending list of alarms */
static TXALARM	*TxAlarmsOld = TXALARMPN;	/* done, to-be-freed alarms */
#ifdef _WIN32
static TXTHREAD	TxAlarmThread = TXTHREAD_NULL;	/* thread to fire alarms */
static HANDLE	TxAlarmCreatorThread = NULL;	/* TxAlarmThread creator */
static HANDLE	TxAlarmAccessMutex = NULL;
static HANDLE	TxAlarmResetEvent = NULL;
static int	TxAlarmThreadShouldExit = 0;
#else /* !_WIN32 */
static SIGTYPE	(*TxAlarmPrevHandler) SIGARGS = SIG_DFL;
static int	TxAlarmGotPrevHandler = 0;
#endif /* !_WIN32 */
static TXATOMINT	TxAlarmGot = 0;
static TXATOMINT	TxAlarmDelay = 0;
static TXATOMINT	TxAlarmIn = 0;

/* tracealarm (documented):
 * 0x0001: TXsetalarm/TXunsetalarm/TXunsetallalarms called
 * 0x0002: system alarm/handler set/unset
 * 0x0004: TX alarms fired
 * 0x0008: system alarms fired
 * 0x0010: TXsetalarm/TXunsetalarm/TXunsetallalarms finished
 * 0x0020: timestamp each msg
 * 0x0040: TXgetalarm() called
 */
int		TxTraceAlarm = 0;

#if defined(EPI_HAVE_SIGACTION) && defined(EPI_HAVE_SIGINFO_T)
#  define TX_ALARM_HANDLER_DECL_ARGS	    (int sig, siginfo_t *si, void *uc)
#  define TX_ALARM_HANDLER_PASS_ARGS(sig)	(sig, NULL, NULL)
#else /* !(EPI_HAVE_SIGACTION && EPI_HAVE_SIGINFO_T) */
#  define TX_ALARM_HANDLER_DECL_ARGS		SIGARGS
#  define TX_ALARM_HANDLER_PASS_ARGS(sig)	(sig)
#endif /* !(EPI_HAVE_SIGACTION && EPI_HAVE_SIGINFO_T) */
static SIGTYPE CDECL tx_alarm_handler TX_ALARM_HANDLER_DECL_ARGS;
#define SIG_ALARM_MAIN_THREAD   666     /* indicates non-signal call */

#define TX_ALARMSTAMP_BUFSZ	24
static char *tx_alarmstamp ARGS((char *buf, size_t sz));
static char *
tx_alarmstamp(buf, sz)
char	*buf;
size_t	sz;
{
	if (TxTraceAlarm & 0x20)
		htsnpf(buf, sz, "%1.6lf: ", TXgettimeofday());
	else if (sz > 0)
		*buf = '\0';
	return(buf);
}

#ifdef _WIN32
static void tx_alarm_accessmutexon ARGS((void));
static void
tx_alarm_accessmutexon()
/* Controls access to TxAlarms/TxAlarmsOld lists under Windows,
 * where multiple threads exist.
 */
{
	HANDLE	mutex = TxAlarmAccessMutex;

	if (mutex == NULL) return;
	switch (WaitForSingleObject(mutex, INFINITE))
	{
	case WAIT_ABANDONED:
	case WAIT_OBJECT_0:	/* we have the ball */
		break;
	default:		/* WTF error */
		break;
	}
}

static void tx_alarm_accessmutexoff ARGS((void));
static void
tx_alarm_accessmutexoff()
{
	HANDLE	mutex = TxAlarmAccessMutex;

	if (mutex == NULL) return;
	ReleaseMutex(mutex);
}

static TXTHREADRET TXTHREADPFX
tx_alarm_thread(void *arg)
/* Separate thread under Windows that waits for and calls alarms.
 * Also handles TEXIS_TERMINATE_PID_nnn events (<kill $pid SIGTERM>).
 */
{
	DWORD	msec;
	int	res, nHandles;
	HANDLE	handles[3];
	void	(*func) ARGS((void));
	char	stampbuf[TX_ALARMSTAMP_BUFSZ];
	char	hBuf[256], *hBufDest;
#define ADD_HANDLE(hn)						\
	{ handles[nHandles++] = (hn);				\
	if (TxTraceAlarm && hBufDest < hBuf + sizeof(hBuf))	\
	{							\
		hBufDest +=					\
		htsnpf(hBufDest, (hBuf + sizeof(hBuf)) - hBufDest,	\
			" %s=%p", #hn, (hn));			\
	}}

	__try
	{

	if (TxTraceAlarm & 0x2)
		putmsg(MINFO, CHARPN,
		  "%sAlarm thread starting",
		tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ));

	while (!TxAlarmThreadShouldExit)
	{
		tx_alarm_accessmutexon();
		if (TxAlarms != TXALARMPN)
		{
			msec = (DWORD)((TxAlarms->abstime - TXgettimeofday())*
					(double)1000.0);
			if (msec <= (DWORD)0) msec = 1;
		}
		else
			msec = INFINITE;
		/* Allow the main thread to signal us again (and us to wait).
		 * Do this inside the access mutex to avoid race conditions:
		 */
		ResetEvent(TxAlarmResetEvent);
		nHandles = 0;
		hBufDest = hBuf;
		*hBufDest = '\0';
		ADD_HANDLE(TxAlarmResetEvent);
		ADD_HANDLE(TxAlarmCreatorThread);
		/* Also wait for TEXIS_TERMINATE_PID_nnn event, if set.
		 * Again, inside mutex to avoid race w/anyone modifying
		 * `TXterminateEvent':
		 */
		if (TXterminateEvent != NULL)
			ADD_HANDLE(TXterminateEvent);

		tx_alarm_accessmutexoff();

		if (TxAlarmThreadShouldExit) goto done;

		/* Now wait for timeout, signal to reset alarm, creator
		 * thread to terminate, or TEXIS_TERMINATE_PID_nnn event.
		 * We wait for creator thread so that
		 * we can exit too when it exits, if we have no more alarms
		 * to service.  Otherwise we might wait indefinitely (if no
		 * alarms) after all other threads exit, which might (?)
		 * cause the parent process to wait indefinitely for us:
		 */
		if (TxTraceAlarm & 0x2)
		{
			if (msec == INFINITE)
				putmsg(MINFO, CHARPN,
				  "%sAlarm thread waiting indefinitely for%s",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				hBuf);
			else
				putmsg(MINFO, CHARPN,
			    "%sAlarm thread waiting for %ld.%03ld sec for%s",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				    ((long)msec)/1000L, ((long)msec) % 1000L,
				    hBuf);
		}
		switch (WaitForMultipleObjects(nHandles, handles, FALSE,msec))
		{
		case WAIT_ABANDONED_0:
		case WAIT_OBJECT_0:		/* main thread signalled us */
			/* If TxAlarmThreadShouldExit is set, main thread
			 * is telling us to exit:
			 */
			if (TxAlarmThreadShouldExit) goto done;
			/* Otherwise, loop around and get new alarm value */
			break;
		case WAIT_FAILED:		/* error */
		default:			/* ? */
			/* try again, but sleep a bit in case repetitive: */
			TXsleepmsec(250, 0);	/* don't burn CPU */
			break;
		case WAIT_ABANDONED_0 + 1:
		case WAIT_OBJECT_0 + 1:		/* creator thread exited */
			if (TxAlarmThreadShouldExit) goto done;
			tx_alarm_accessmutexon();
			res =(TxAlarms==TXALARMPN || TxAlarmResetEvent==NULL);
			if (res) TxAlarmThread = TXTHREAD_NULL;
			tx_alarm_accessmutexoff();
			if (res) goto done;	/* exit this thread too */
			break;
		case WAIT_OBJECT_0 + 2:		/* TEXIS_TERMINATE_PID_nnn */
			if (TXinsignal()) break;/* sanity: no recursion */
			/* Use mutex to access `TXterminateEventHandler'
			 * atomically.  wtf what if mutex blocks? we at least
			 * do the `TXinsignal()' test before this:
			 */
			tx_alarm_accessmutexon();
			func = TXterminateEventHandler;
			tx_alarm_accessmutexoff();
			if (func != TXTERMFUNCPN) func();
			/* If we're still alive for some reason,
			 * might as well reset:
			 */
			if (TXterminateEvent != NULL)
				ResetEvent(TXterminateEvent);
			break;
		case WAIT_TIMEOUT:		/* timeout expired */
			tx_alarm_handler TX_ALARM_HANDLER_PASS_ARGS(0);
			break;
		}
	}
done:
	if (TxTraceAlarm & 0x2)
		putmsg(MINFO, CHARPN,
		  "%sAlarm thread ending",
		tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ));
	/* MS C apparently needs a null statement here */
	;} __except(TXgenericExceptionHandler(_exception_code(),
                                              _exception_info()))
	{
	}
	return(0);
#undef ADD_HANDLE
}

void
TXgenericTerminateEventHandler()
/* Called by tx_alarm_thread() by default when TEXIS_TERMINATE_PID_nnn
 * event signalled (e.g. by external process <kill $pid SIGTERM>).
 * May be changed with TXsetTerminateEventHandler().
 */
{
	char	pidBuf[64];

#  if defined(_MSC_VER)
	__try
	{
#  endif /* _MSC_VER */
		/* do not recurse, especially into TXcallabendcbs(): */
		/* If we're already in a previous signal handler,
		 * ignore this message; since all possible types are
		 * less severe than SIGSEGV-type issues, we do not
		 * exit but let previous signal handler finish:
		 */
		if (TXentersignal()) goto done;
		if (vx_getpmflags() & VXPMF_ShowPid)
			*pidBuf = '\0';
		else
			htsnpf(pidBuf, sizeof(pidBuf), "(%u) ",
			       (unsigned)TXgetpid(1));
		putmsg(MERR, CHARPN,
		       "%s%s terminated (Texis Terminate event); exiting",
		       pidBuf, TXsigProcessName);
		TXcallabendcbs();
		_exit(TXEXIT_TERMINATED);	/* _exit() not exit() */
	done:
		TXexitsignal();
#  if defined(_MSC_VER)
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		_exit(TXEXIT_ABEND);		/* _exit() not exit() */
	}
#  endif /* _MSC_VER */
}

TXTERMFUNC *
TXsetTerminateEventHandler(func)
TXTERMFUNC	*func;
/* Sets terminate-event handler to `func', returning old one.
 * Can be NULL for no handler, or TXgenericTerminateEventHandler for default.
 * Note that old one could still fire after new one is set;
 * see tx_alarm_thread().
 */
{
	TXTERMFUNC	*ret;

	tx_alarm_accessmutexon();
	ret = TXterminateEventHandler;
	TXterminateEventHandler = func;
	tx_alarm_accessmutexoff();
	return(ret);
}
#else /* !_WIN32 */
#  define tx_alarm_accessmutexon()
#  define tx_alarm_accessmutexoff()
#endif /* !_WIN32 */

static int setsysalarm ARGS((int what, double tim));
static int
setsysalarm(what, tim)
int	what;
double	tim;
/* Sets OS alarm.  Returns 0 on error.
 * `what' is what to do, after ensuring threads/handlers/events created:
 *   0  set current alarm (if any), ignoring `tim'
 *   1  "", relative `tim'
 *   2  turn off current alarm (may or may not stop system thread)
 *   3  turn off all system alarms/threads/events completely
 */
{
	static CONST char	fn[] = "setsysalarm";
#ifdef _WIN32
	char			stampbuf[TX_MAX(TX_ALARMSTAMP_BUFSZ,
			sizeof(TXterminateEventPrefix) + EPI_OS_INT_BITS/3 + 3)];
	int			ret;

	/* Note that we don't kill the alarm thread if `what' is 2,
	 * so that we're not killing and re-starting the alarm thread
	 * a lot if many alarms are set and cancelled serially
	 * (this also seems to avoid a bug where alarms fail?).
	 * Also ensures that alarm thread is servicing TXterminateEvent
	 * even if no alarms currently scheduled.
	 * But see caveat in tx_alarm_thread():
	 */
	if (what == 3)			/* turn off OS alarm completely */
	{
		ret = 1;
	closeout:
		/* Do not mutex with tx_alarm_accessmutexon() while
		 * killing alarm thread: might deadlock with alarm
		 * thread if we wait for it to exit inside the mutex
		 * (Bug 2803).  Should be ok w/o mutex?
		 */
		if (!TxAlarmIn)		/* do not kill our own thread */
		{
			if (TxAlarmThread != TXTHREAD_NULL)
			{
				/* Bug 2803: hard-kill of alarm thread seems
				 * to cause main thread's exit() code to be
				 * lost (due to race?), when <exec>ing a
				 * Vortex sub-process.  Use soft kill first:
				 */
				TxAlarmThreadShouldExit = 1;
				if (TxAlarmResetEvent != NULL)
				{
					/* Signal alarm thread to wake up and
					 * exit due to TxAlarmThreadShouldExit
					 */
					if (TxTraceAlarm & 0x2)
						putmsg(MINFO, CHARPN,
					"%sSignaling alarm thread %p to exit",
				    tx_alarmstamp(stampbuf, sizeof(stampbuf)),
						(void *)TxAlarmThread);
					SetEvent(TxAlarmResetEvent);
					switch (WaitForSingleObject(TxAlarmThread,
								1000))
					{
					case WAIT_ABANDONED_0:
					case WAIT_OBJECT_0:
						TxAlarmThread = TXTHREAD_NULL;
						break;
					}
				}
				/* Hard-kill only if soft-kill failed: */
				if (TxAlarmThread != TXTHREAD_NULL)
				{
					if (TxTraceAlarm & 0x2)
						putmsg(MINFO, CHARPN,
					    "%sTerminating alarm thread %p",
				    tx_alarmstamp(stampbuf, sizeof(stampbuf)),
						(void *)TxAlarmThread);
					TXterminatethread(TXPMBUFPN,
							  TxAlarmThread, 0);
				}
				TxAlarmThread = TXTHREAD_NULL;
			}
			if (TxAlarmCreatorThread != NULL)
				CloseHandle(TxAlarmCreatorThread);
			TxAlarmCreatorThread = NULL;
		}
		/* was tx_alarm_accessmutexoff() here before Bug 2803 */
		if (TXterminateEvent != NULL)
			CloseHandle(TXterminateEvent);
		TXterminateEvent = NULL;
		if (TxAlarmAccessMutex != NULL)
			CloseHandle(TxAlarmAccessMutex);
		TxAlarmAccessMutex = NULL;
		if (TxAlarmResetEvent != NULL)
			CloseHandle(TxAlarmResetEvent);
		TxAlarmResetEvent = NULL;
		return(ret);
	}
	if (TxAlarmAccessMutex == NULL)
	{
		TxAlarmAccessMutex = TXCreateMutex(TXPMBUFPN, CHARPN);
		if (TxAlarmAccessMutex == NULL)
			goto closeout1;
	}
	if (TxAlarmResetEvent == NULL)
	{
		TxAlarmResetEvent = TXCreateEvent(CHARPN, TRUE, 1);
		if (TxAlarmResetEvent == NULL)
			goto closeout1;
	}
	if (TXterminateEvent == NULL)
	{
		htsnpf(stampbuf, sizeof(stampbuf), "%s%u",
			TXterminateEventPrefix, TXgetpid(1));
		/* Event should not exist already (since PID in the name),
		 * but not a big deal if it does.  WTF event has perms
		 * for everyone; restrict to admin/local-system (for
		 * Texis Monitor web-server-run scripts)/current-user
		 * someday, via SDDL-set ACL?
		 */
		TXterminateEvent = TXCreateEvent(stampbuf, TRUE, 1);
		if (TXterminateEvent == NULL)
			goto closeout1;
	}
	tx_alarm_accessmutexon();
	if (TxAlarmThread == TXTHREAD_NULL)
	{
		/* Set TxAlarmCreatorThread to let the tx_alarm_thread know
		 * who created it (see tx_alarm_thread() caveat).  Since
		 * GetCurrentThread() is a constant, we must dup it to get
		 * a handle valid for the other thread.  Thus we must
		 * close it sometime too:
		 */
		if (TxAlarmCreatorThread != NULL)
			CloseHandle(TxAlarmCreatorThread);
		TxAlarmCreatorThread = NULL;
		if (!TXgetCurrentThread(1, &TxAlarmCreatorThread))
		{
			tx_alarm_accessmutexoff();
			goto closeout1;
		}
		if (!TXcreatethread(TXPMBUFPN, "alarm", tx_alarm_thread,
				    NULL, (TXTHREADFLAG)0, 0, &TxAlarmThread))
		{
			TX_PUSHERROR();
			tx_alarm_accessmutexoff();
			TX_POPERROR();
		closeout1:
			ret = 0;
			goto closeout;
		}
	}
	else
		SetEvent(TxAlarmResetEvent);	/* kick tx_alarm_thread() */
	tx_alarm_accessmutexoff();
	/* must ignore relative time `tim': tx_alarm_thread() does rest... */
#else /* !_WIN32 */
	char			stampbuf[TX_ALARMSTAMP_BUFSZ];
	struct itimerval	nv, ov;
	long			sec, usec;

	if (TxAlarms == TXALARMPN || what >= 2)	/* not setting an alarm */
		sec = usec = 0L;
	else					/* setting an alarm */
	{
		/* what == 1 is optional/optimization: just saves
		 * gettimeofday call
		 */
		if (what == 0)			/* convert to relative time */
			tim = TxAlarms->abstime - TXgettimeofday();
		sec = (long)tim;
		usec = (tim - (double)sec)*(double)1000000;
		if (sec < 0L || (sec == 0L && usec <= 0L))
		{				/* minimum time */
			sec = 0L;
			usec = 1L;
		}
	}
	/* We don't know fer sure that SA_RESETHAND is not in effect,
	 * so set the handler every time (unless turning off all alarms):
	 */
	if (what < 3)				/* if not at-exit clearing */
	{
		SIGTYPE	(*prevHandler) SIGARGS;
#  ifdef EPI_HAVE_SIGACTION
		struct sigaction	act, prev;
		sigemptyset(&act.sa_mask);
		/* try not to set SA_RESETHAND, but don't assume unset */
		/* do not set SA_RESTART: interrupt any system calls */
		/* do not set SA_NODEFER: try not to re-interrupt handler */
#    ifdef SA_INTERRUPT			/* SunOS? */
		act.sa_flags = SA_INTERRUPT;
#    else /* !SA_INTERRUPT */
		act.sa_flags = 0;
#    endif /* !SA_INTERRUPT */
#    ifdef EPI_HAVE_SIGINFO_T
		act.sa_sigaction = tx_alarm_handler;
		act.sa_flags |= SA_SIGINFO;
#    else /* !EPI_HAVE_SIGINFO_T */
		act.sa_handler = tx_alarm_handler;
#    endif /* !EPI_HAVE_SIGINFO_T */
		errno = 0;
		if (sigaction(SIGALRM, &act, &prev) != 0)
		{
			putmsg(MERR, fn,"Cannot set alarm handler: %s",
				strerror(errno));
			return(0);	/* WTF */
		}
		prevHandler = prev.sa_handler;
#  else /* !EPI_HAVE_SIGACTION */
		prevHandler = signal(SIGALRM, tx_alarm_handler);
#  endif /* !EPI_HAVE_SIGACTION */
		if (!TxAlarmGotPrevHandler)
		{
			TxAlarmPrevHandler = prevHandler;
			TxAlarmGotPrevHandler = 1;
		}
		if (TxTraceAlarm & 0x2)
			putmsg(MINFO, CHARPN,
				"%sSet signal handler for SIGALRM",
				tx_alarmstamp(stampbuf, sizeof(stampbuf)));
	}
	if (what >= 2 || TxAlarms != TXALARMPN)	/* clearing/setting alarm */
	{
		nv.it_interval.tv_sec = 0L;
		nv.it_interval.tv_usec = 0L;
		nv.it_value.tv_sec = (long)sec;
		nv.it_value.tv_usec = (long)usec;
		/* Set the timer, _after_ we've set the handler: */
		if (TxTraceAlarm & 0x2)
			putmsg(MINFO, CHARPN,
				"%sSetting itimer for %ld.%06ld sec",
				tx_alarmstamp(stampbuf, sizeof(stampbuf)),
				(long)nv.it_value.tv_sec,
				(long)nv.it_value.tv_usec);
		errno = 0;
		if (setitimer(ITIMER_REAL, &nv, &ov) != 0)
		{
			putmsg(MERR, fn, "Cannot setitimer(%ld.%06ld): %s",
			       (long)nv.it_value.tv_sec,
			       (long)nv.it_value.tv_usec, strerror(errno));
			return(0);		/* WTF */
		}
	}
#endif /* !_WIN32 */
	return(1);
}

int
TXstartEventThreadAlarmHandler(void)
/* Starts thread that services alarms and TEXIS_TERMINATE_PID_nnn events.
 * Note that the thread may also be started via TXsetalarm().  Under Unix,
 * sets alarm handler, so we catch SIGALRM (even if no alarms set yet:
 * could be signalled from another process for some unknown reason).
 */
{
#ifdef _WIN32
  if (TxAlarmThread != TXTHREAD_NULL) return(1);	/* already did this */
#else /* !_WIN32 */
  if (TxAlarmGotPrevHandler) return(1);		/* already did this */
#endif /* !_WIN32 */
  return(setsysalarm(0, 0.0));
}

static SIGTYPE CDECL
tx_alarm_handler TX_ALARM_HANDLER_DECL_ARGS
/* Signal handler (Unix) or thread callback (Windows) for SIGALRM.
 * Calls appropriate function and sets timer for next alarm.
 */
{
	TXALARM	*a;
        int     msgNumAddend =
#ifdef _WIN32
          0;                                    /* not really a signal */
#else /* !_WIN32 */
        TX_PUTMSG_NUM_IN_SIGNAL;
#endif /* !_WIN32 */
	char	stampbuf[TX_ALARMSTAMP_BUFSZ];

	(void)TXentersignal();
	if ((TxTraceAlarm & 0x8) && sig != SIG_ALARM_MAIN_THREAD)
	{
		CONST char	*reason = Ques;
		char		pidBuf[EPI_OS_UINT_BITS/3 + 3] = "?";
#if defined(EPI_HAVE_SIGACTION) && defined(EPI_HAVE_SIGINFO_T)
		char		codeNameBuf[64];

                (void)uc;
		if (si != (siginfo_t *)NULL)
		{
			reason = TXsiginfoCodeName(sig, si->si_code);
			if (!reason)
			{
				htsnpf(codeNameBuf, sizeof(codeNameBuf),
				       "si_code_%d", (int)si->si_code);
				reason = codeNameBuf;
			}
			htsnpf(pidBuf, sizeof(pidBuf), "%u",
				(unsigned)si->si_pid);
		}
#endif /* EPI_HAVE_SIGACTION && EPI_HAVE_SIGINFO_T */
		putmsg(MINFO + msgNumAddend, CHARPN,
			"%sSystem alarm handler signalled (reason: %s; from PID %s): in=%d delay=%d got=%d alarms=%p",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			reason, pidBuf, (int)TxAlarmIn, (int)TxAlarmDelay,
			(int)TxAlarmGot, TxAlarms);
	}
	/* Check if this is not a valid alarm.  Try to reset alarm handler
         * (in case SA_RESETHAND in effect) unless it is too dangerous:
	 */
	if (TxAlarmIn) goto done;	/* inadvertent recursion? */
	if (TxAlarms == TXALARMPN) goto setSysAlarm;	/* spurious signal? */
	if (TxAlarmDelay)		/* main thread modifying lists */
	{
		(void)TX_ATOMIC_INC(&TxAlarmGot);
		goto setSysAlarm;
	}

	(void)TX_ATOMIC_INC(&TxAlarmIn);	/* we are now in an alarm */

	tx_alarm_accessmutexon();
	/* Bug 2365: Check for NULL again, once inside the mutex: */
	if ((a = TxAlarms) != TXALARMPN)
	{
#if defined(EPI_HAVE_SIGACTION) && defined(EPI_HAVE_SIGINFO_T) && defined(EPI_HAVE_SIGCODE_SI_USER)
		/* Bug 2674: Make sure this is not an external process
		 * signalling us (probably at the wrong time).  Checking
		 * siginfo_t.si_code avoids a TXgettimeofday() call:
		 */
		if (si != (siginfo_t *)NULL && si->si_code == SI_USER)
		{				/* user-generated signal */
			if (TxTraceAlarm & 0x8)
				putmsg(MINFO + msgNumAddend, CHARPN,
				    "%sSystem alarm not from timer, ignoring",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ));
			a = TXALARMPN;
		}
		else
#else /* !(EPI_HAVE_SIGACTION && EPI_HAVE_SIGINFO_T) */
		double	now;
		/* Bug 2674: Make sure it is the appropriate time for
		 * this alarm, in case an external process is throwing
		 * SIGALRMs at us for some reason.  Costs an extra
		 * TXgettimeofday() though:
		 */
		now = TXgettimeofday();
		if (a->abstime - now > (double)0.5)
		{				/* >0.5 sec in future */
			if (TxTraceAlarm & 0x8)
				putmsg(MINFO + msgNumAddend, CHARPN,
	"%sToo early for TX alarm(%p, %p): set for %1.6f (%1.6f from now)",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
					a->func, a->usr, a->abstime,
					a->abstime - now);
			a = TXALARMPN;
		}
		else			/* move from active to old */
#endif /* !(EPI_HAVE_SIGACTION && EPI_HAVE_SIGINFO_T) */
		{
			TxAlarms = TxAlarms->next;
			a->next = TxAlarmsOld;
			TxAlarmsOld = a;
		}
	}
	tx_alarm_accessmutexoff();

	if (a != TXALARMPN)
	{
		if (TxTraceAlarm & 0x4)
			putmsg(MINFO + msgNumAddend, CHARPN,
				"%sCalling TX alarm(%p, %p) from %s",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				a->func, a->usr,
	    (sig == SIG_ALARM_MAIN_THREAD ? "main thread" : "signal thread"));
		a->func(a->usr);		/* this might shuffle lists */
	}

	(void)TX_ATOMIC_DEC(&TxAlarmIn);

	/* Re-set the OS timer for the next alarm.
	 * If the above callback called TXsetalarm(), then we end up
	 * calling gettimeofday() again in setsysalarm() here.  We cannot
	 * safely avoid this double call, because we don't know how much
	 * time elapsed between their TXsetalarm() and our setsysalarm(),
	 * so any saved value may be stale now.  And we cannot let their
	 * TXsetalarm() call setsysalarm() either, because our signal
	 * handler might get re-invoked early...
	 */
setSysAlarm:
#ifndef _WIN32
	/* Set system alarm even if no TxAlarms, so it can grab signal
	 * in case SA_RESETHAND in effect:
	 */
	setsysalarm(0, 0.0);
#endif /* !_WIN32 */
done:
	TXexitsignal();
	SIGRETURN;
}

int
TXsetalarm(func, usr, sec, inSig)
TXALARMFUNC	*func;
void		*usr;
double		sec;
TXbool		inSig;
/* Sets an alarm: `func' will be called with parameter `usr' in
 * `sec' seconds from now.  If an alarm already exists for this
 * func/usr pair it will be re-scheduled as this alarm.
 * Must be called from main (non-signal) thread, except when scheduling
 * an alarm from an alarm callback.  Returns 0 on error.
 */
{
	static CONST char	fn[] = "TXsetalarm";
	TXALARM			*prev, *next, *a, *new;
	int			reset = 0, ret;
	int	msgNumAddend = (inSig ? TX_PUTMSG_NUM_IN_SIGNAL : 0);
	char			stampbuf[TX_ALARMSTAMP_BUFSZ];

	(void)TX_ATOMIC_INC(&TxAlarmDelay);
	tx_alarm_accessmutexon();
	for (prev = TXALARMPN, a = TxAlarms; a != TXALARMPN; prev=a,a=a->next)
	{					/* find old alarm, if any */
		if (a->func == func && a->usr == usr) break;
	}
	if (a == TXALARMPN)			/* no old alarm */
	{
		if (TxTraceAlarm & 0x1)
			putmsg(MINFO + msgNumAddend, CHARPN,
				"%sTXsetalarm(%p, %p, %1.6lf): new",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				func, usr, sec);
		if (TxAlarmsOld != TXALARMPN)	/* recycle... */
		{
			new = TxAlarmsOld;
			TxAlarmsOld = TxAlarmsOld->next;
		}
		else if (TxAlarmIn)		/* cannot malloc */
		{
			putmsg(MERR + UGE + msgNumAddend, fn,
			"Internal error: cannot malloc in alarm");
			goto err;
		}
		else if ((new=(TXALARM*)calloc(1,sizeof(TXALARM)))==TXALARMPN)
		{
			putmsg(MERR + MAE + msgNumAddend, fn, OutOfMem);
			goto err;
		}
		new->func = func;
		new->usr = usr;
	}
	else					/* remove old alarm */
	{
		if (TxTraceAlarm & 0x1)
			putmsg(MINFO + msgNumAddend, CHARPN,
				"%sTXsetalarm(%p, %p, %1.6lf): reset",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				func, usr, sec);
		if (prev == TXALARMPN)		/* first in list */
		{
			TxAlarms = TxAlarms->next;
			reset = 1;		/* note for later */
		}
		else
			prev->next = a->next;
		new = a;
	}
	if (sec < (double)0.0) sec = (double)0.0;/* clean up requested time */
	new->abstime = TXgettimeofday() + sec;
	/* Insert new alarm in proper place in list: */
	for (prev = TXALARMPN, a = TxAlarms; a != TXALARMPN; prev=a,a=a->next)
		if (new->abstime < a->abstime) break;
	new->next = a;
	if (prev == TXALARMPN)			/* first in list */
	{
		TxAlarms = new;
		reset = 1;			/* note for below */
	}
	else
		prev->next = new;
	if (!TxAlarmIn)				/* clean up old alarms */
	{
		while (TxAlarmsOld != TXALARMPN)
		{
			next = TxAlarmsOld->next;
			free(TxAlarmsOld);
			TxAlarmsOld = next;
		}
	}
	if (reset && !TxAlarmIn &&
	    !setsysalarm((new == TxAlarms ? 1 : 0), sec))
		goto err;
	ret = 1;
	goto done;

err:
	ret = 0;
done:
	tx_alarm_accessmutexoff();
	(void)TX_ATOMIC_DEC(&TxAlarmDelay);
	/* Handle a pending signal.  We don't really know if it was for
	 * the first alarm on the queue *before* or *after* we added
	 * this one, but it doesn't really matter, they'll both fire
	 * at the correct time (roughly within clock resolution),
	 * just not necessarily in order:
	 */
	if (TxAlarmGot)
	{
		TxAlarmGot = 0;
		tx_alarm_handler TX_ALARM_HANDLER_PASS_ARGS(
					SIG_ALARM_MAIN_THREAD);
	}
	if (TxTraceAlarm & 0x10)
		putmsg(MINFO + msgNumAddend, CHARPN,
	     "%sTXsetalarm(%p, %p, %1.6lf) done: in=%d delay=%d got=%d",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			func, usr, sec, (int)TxAlarmIn,
			(int)TxAlarmDelay, (int)TxAlarmGot);
	return(ret);
}

double
TXgetalarm(func, usr)
TXALARMFUNC	*func;	/* (in) alarm function to check for */
void		*usr;	/* (in) alarm user data to check for */
/* Returns absolute time that alarm for `func'/`usr' pair is set to fire,
 * or 0 if no such alarm is set.
 * Should be called from main (non-signal/alarm) thread only.
 */
{
	TXALARM	*a;
	double	ret;
	char	stampbuf[TX_ALARMSTAMP_BUFSZ];

	tx_alarm_accessmutexon();
	for (a = TxAlarms; a != TXALARMPN; a = a->next)
		if (a->func == func && a->usr == usr) break;
	if (a == TXALARMPN)			/* no such alarm */
	{
		if (TxTraceAlarm & 0x40)
			putmsg(MINFO, CHARPN,
				"%sTXgetalarm(%p, %p): not set",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				func, usr);
		goto err;
	}
	ret = a->abstime;
	if (TxTraceAlarm & 0x40)
		putmsg(MINFO, CHARPN,
		   "%sTXgetalarm(%p, %p): will fire at %1.6lf",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			func, usr, ret);
	goto done;

err:
	ret = 0.0;
done:
	tx_alarm_accessmutexoff();
	return(ret);
}

int
TXunsetalarm(func, usr, secp)
TXALARMFUNC	*func;
void		*usr;
double		*secp;	/* (optional) fire time */
/* Removes alarm for func/usr.  Sets *secp to absolute time that alarm
 * would have been called.  Returns 0 on error (no such alarm).
 * Should be called from main (non-signal/alarm) thread only.
 */
{
	TXALARM	*prev = TXALARMPN, *next, *a;
	int	ret;
	char	stampbuf[TX_ALARMSTAMP_BUFSZ];

	(void)TX_ATOMIC_INC(&TxAlarmDelay);	/* defer signals */
	tx_alarm_accessmutexon();
	for (prev = TXALARMPN, a = TxAlarms; a != TXALARMPN; prev=a,a=a->next)
		if (a->func == func && a->usr == usr) break;
	if (a == TXALARMPN)			/* no such alarm */
	{
		if (TxTraceAlarm & 0x1)
			putmsg(MINFO, CHARPN,
				"%sTXunsetalarm(%p, %p): not set",
				tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
				func, usr);
		if (secp != (double *)NULL) *secp = (double)0.0;
		prev = TXALARMPN;		/*stop tx_alarm_handler call*/
		goto err;
	}
	if (prev == TXALARMPN) TxAlarms = TxAlarms->next;
	else prev->next = a->next;
	if (secp != (double *)NULL) *secp = a->abstime;
	if (TxTraceAlarm & 0x1)
		putmsg(MINFO, CHARPN,
		   "%sTXunsetalarm(%p, %p): would have fired at %1.6lf",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			func, usr, (double)a->abstime);
	if (TxAlarmIn)				/* cannot free() */
	{
		a->next = TxAlarmsOld;
		TxAlarmsOld = a;
	}
	else
	{
		free(a);
		while (TxAlarmsOld != TXALARMPN)/* clean up old alarms */
		{
			next = TxAlarmsOld->next;
			free(TxAlarmsOld);
			TxAlarmsOld = next;
		}
		if (prev == TXALARMPN &&	/* deleted first alarm */
		    !setsysalarm((TxAlarms == TXALARMPN ? 2 : 0), 0.0))
			goto err;
	}
	ret = 1;
	goto done;

err:
	ret = 0;
done:
	tx_alarm_accessmutexoff();
	(void)TX_ATOMIC_DEC(&TxAlarmDelay);
	/* Check for and handle a pending signal, but only if we did
	 * not delete the first alarm.  If we did delete the first,
	 * it's gone and doesn't need to fire (and firing here would
	 * erroneously fire the next alarm early).  Resetting OS timer
	 * for next alarm in TxAlarms is handled either by setsysalarm()
	 * above or the tx_alarm_handler() calling us right now:
	 */
	if (TxAlarmGot)
	{
		TxAlarmGot = 0;
		if (prev != TXALARMPN)
			tx_alarm_handler TX_ALARM_HANDLER_PASS_ARGS(
						SIG_ALARM_MAIN_THREAD);
	}
	if (TxTraceAlarm & 0x10)
		putmsg(MINFO, CHARPN,
		"%sTXunsetalarm(%p, %p) done: in=%d delay=%d got=%d",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			func, usr, (int)TxAlarmIn,
			(int)TxAlarmDelay, (int)TxAlarmGot);
	return(ret);
}

int
TXunsetallalarms()
/* Removes all pending alarms.  Mainly useful for Windows where this will
 * also terminate the alarm thread, so the process will exit when the
 * main thread terminates.
 */
{
	TXALARM	*next;
	char	stampbuf[TX_ALARMSTAMP_BUFSZ];

	(void)TX_ATOMIC_INC(&TxAlarmDelay);
	tx_alarm_accessmutexon();

	if (TxTraceAlarm & 0x1)
		putmsg(MINFO, CHARPN, "%sTXunsetallalarms() called",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ));

	while (TxAlarms != TXALARMPN)		/* free all alarms */
	{
		next = TxAlarms->next;
		free(TxAlarms);
		TxAlarms = next;
	}
	setsysalarm(3, 0.0);			/* turn off system alarm */
	while (TxAlarmsOld != TXALARMPN)	/* clean up old alarms */
	{
		next = TxAlarmsOld->next;
		free(TxAlarmsOld);
		TxAlarmsOld = next;
	}

	tx_alarm_accessmutexoff();
	(void)TX_ATOMIC_DEC(&TxAlarmDelay);
	if (TxTraceAlarm & 0x10)
		putmsg(MINFO, CHARPN,
			"%sTXunsetallalarms() done: in=%d delay=%d got=%d",
			tx_alarmstamp(stampbuf, TX_ALARMSTAMP_BUFSZ),
			(int)TxAlarmIn, (int)TxAlarmDelay, (int)TxAlarmGot);
	return(1);
}

/* ------------------------------------------------------------------------ */

TXEVENT *
opentxevent(int manualReset)
/* Creates an event for thread synchronization.
 * Thread-safe.
 */
{
	static CONST char	fn[] = "opentxevent";
#ifdef _WIN32
	HANDLE			hn;

	hn = CreateEvent(NULL,		/* not inheritable */
			 manualReset,
			 FALSE,		/* initially unsignalled */
			 NULL);		/* unnamed */
	if (hn == NULL)
	{
		putmsg(MERR, fn, "Cannot CreateEvent(): %s",
			TXstrerror(TXgeterror()));
		return(TXEVENTPN);
	}
	return((TXEVENT *)hn);
#elif defined(EPI_USE_PTHREADS)
	pthread_cond_t	*event;

	if (manualReset)
	{
		putmsg(MERR + UGE, __FUNCTION__,
		       "Internal error: manual-reset events not supported on this platform");
		/* wtf could implement it with wrapper if needed */
		return(NULL);
	}
	event = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
	if (event == (pthread_cond_t *)NULL)
	{
		putmsg(MERR + MAE, fn, OutOfMem);
		return(TXEVENTPN);
	}
	if (pthread_cond_init(event, (pthread_condattr_t *)NULL) != 0)
	{
		putmsg(MERR, __FUNCTION__, "pthread_cond_init() failed: %s",
		       TXstrerror(TXgeterror()));
		event = closetxevent(event);
	}
	return((TXEVENT *)event);
#else /* !_WIN32 && !EPI_USE_PTHREADS */
	putmsg(MERR + UGE, fn, NoThreadsPlatform);
	return(TXEVENTPN);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}

TXEVENT *
closetxevent(event)
TXEVENT	*event;
/* Thread-safe. */
{
#ifdef _WIN32
	if ((HANDLE)event != NULL) CloseHandle((HANDLE)event);
	return(TXEVENTPN);
#elif defined(EPI_USE_PTHREADS)
	static CONST char	fn[] = "closetxevent";
	int			n;

	if (!event) goto finally;

	if (!(n = pthread_cond_destroy((pthread_cond_t *)event)))
	{
		putmsg(MERR, fn, "Cannot pthread_cond_destroy(): %s",
		       strerror(n));
		return(TXEVENTPN);
	}
	event = TXfree((void *)event);
finally:
	return(TXEVENTPN);
#else /* !_WIN32 && !EPI_USE_PTHREADS */
	return(TXEVENTPN);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}

int
TXunlockwaitforevent(event, mutex)
TXEVENT	*event;
TXMUTEX	*mutex;
/* Atomically unlocks `mutex' and blocks current thread waiting for both
 * `event' to be signalled and an exclusive lock on `mutex'.  Current
 * thread must have `mutex' locked.  Only call once at a time per thread.
 * Returns 0 on error, 1 if event signalled and lock obtained.
 * Thread-safe.
 */
{
#ifdef _WIN32
	static CONST char	fn[] = "TXunlockwaitforevent";
	HANDLE			handles[2];

	/* We cannot atomically unlock `mutex' and block here.
	 * But it should be ok because unlike pthreads, an event signal
	 * is not lost if no one is waiting for it (e.g. someone signals
	 * us between our ReleaseMutex() and WaitForMultipleObjects()):
	 */
	handles[0] = (HANDLE)event;
	handles[1] = (HANDLE)mutex;
	if (!ReleaseMutex((HANDLE)mutex))
	{
		putmsg(MERR, fn, "Cannot ReleaseMutex(): %s",
			TXstrerror(TXgeterror()));
		return(0);
	}
	switch (WaitForMultipleObjects(2, handles, TRUE, INFINITE))
	{
	case WAIT_OBJECT_0:		/* we have both event and mutex */
	case WAIT_ABANDONED:
		return(1);
	}
	putmsg(MERR, fn, "Cannot WaitForMultipleObjects(): %s",
			TXstrerror(TXgeterror()));
	return(0);
#elif defined(EPI_USE_PTHREADS)
	static CONST char	fn[] = "TXunlockwaitforevent";
	int			res;

	do
		res = pthread_cond_wait((pthread_cond_t *)event,
					(pthread_mutex_t *)mutex);
	while (res == EINTR);
	if (res != 0)
	{
		putmsg(MERR, fn, "Cannot pthread_cond_wait(): %s",
		       strerror(res));
		return(0);
	}
	return(1);
#else /* !_WIN32 && !EPI_USE_PTHREADS */
	return(0);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}

int
TXsignalevent(event)
TXEVENT	*event;
/* Signals one of the thread(s) waiting on `event' to wake up.
 * NOTE: Current thread should have lock on associated mutex.
 * Returns 0 on error, 1 if ok.
 * Thread-safe.
 */
{
#ifdef _WIN32
	static CONST char	fn[] = "TXsignalevent";

	if (!SetEvent((HANDLE)event))
	{
		putmsg(MERR, fn, "Cannot SetEvent(): %s",
			TXstrerror(TXgeterror()));
		return(0);
	}
	return(1);
#elif defined(EPI_USE_PTHREADS)
	static CONST char	fn[] = "TXsignalevent";
	int			n;

	if ((n = pthread_cond_signal((pthread_cond_t *)event)) != 0)
	{
		putmsg(MERR, fn, "Cannot pthread_cond_signal(): %s",
		       strerror(n));
		return(0);
	}
	return(1);
#else /* !_WIN32 && !EPI_USE_PTHREADS */
	return(0);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}

/****************************************************************************/

#ifdef TX_ATOMIC_FALLBACK_FUNCS
TXATOMINT_NV
TXatomicAdd(val, n)
TXATOMINT      *val;
TXATOMINT      n;
{
  TXATOMINT     ret;

  ret = *val;
  *val += n;
  return(ret);
}

TXATOMINT_NV
TXatomicSub(val, n)
TXATOMINT      *val;
TXATOMINT      n;
{
  TXATOMINT     ret;

  ret = *val;
  *val -= n;
  return(ret);
}

TXATOMINT_NV
TXatomicCompareAndSwap(TXATOMINT *valPtr, TXATOMINT oldVal, TXATOMINT newVal)
{
  TXATOMINT     ret;

  ret = *valPtr;
  if (ret == oldVal) *valPtr = newVal;
  return(ret);
}

TXATOMINT_WIDE_NV
TXatomicCompareAndSwapWide(TXATOMINT_WIDE *valPtr, TXATOMINT_WIDE oldVal,
                           TXATOMINT_WIDE newVal)
{
  TXATOMINT_WIDE        ret;

  ret = *valPtr;
  if (ret == oldVal) *valPtr = newVal;
  return(ret);
}
#endif /* TX_ATOMIC_FALLBACK_FUNCS */

/****************************************************************************/

#ifndef _WIN32
#  if !defined(EPI_HAVE_GETPWUID_R_PASSWD) &&!defined(EPI_HAVE_GETPWUID_R_INT)
static TXcriticalSection        *TXgetpwCsect = NULL;

static void tx_copypwent ARGS((struct passwd *pwbuf, char *buf, size_t bufsz,
                               struct passwd *pw));
static void
tx_copypwent(pwbuf, buf, bufsz, pw)
struct passwd   *pwbuf;
char            *buf;
size_t          bufsz;
struct passwd   *pw;
{
  char          *d, *e;
  size_t        n;
#    define ADDFLD(f)                                           \
  if (pw->f != CHARPN && d + (n = strlen(pw->f) + 1) <= e)      \
    {                                                           \
      memcpy(d, pw->f, n);                                      \
      pwbuf->f = d;                                             \
      d += n;                                                   \
    }

  d = buf;
  e = buf + bufsz;
  ADDFLD(pw_name);
  ADDFLD(pw_passwd);
  pwbuf->pw_uid = pw->pw_uid;
  pwbuf->pw_gid = pw->pw_gid;
  /* age, comment? */
  ADDFLD(pw_gecos);
  ADDFLD(pw_dir);
  ADDFLD(pw_shell);
#    undef ADDFLD
}
#  endif /* !EPI_HAVE_GETPWUID_R_PASSWD) && !EPI_HAVE_GETPWUID_R_INT */

struct passwd *
TXgetpwuid_r(uid, pwbuf, buf, bufsz)
UID_T           uid;
struct passwd   *pwbuf;
char            *buf;
size_t          bufsz;
/* Thread-safe.
 */
{
  struct passwd *pw;

  memset(pwbuf, 0, sizeof(struct passwd));
#  ifdef EPI_HAVE_GETPWUID_R_PASSWD
  pw = getpwuid_r(uid, pwbuf, buf, bufsz);
#  elif defined(EPI_HAVE_GETPWUID_R_INT)
  if (getpwuid_r(uid, pwbuf, buf, bufsz, &pw) != 0)
    pw = (struct passwd *)NULL;
#  else /* !EPI_HAVE_GETPWUID_R_PASSWD && !EPI_HAVE_GETPWUID_R_INT */
  pw = NULL;
  if (TXcriticalSectionEnter(TXgetpwCsect, TXPMBUF_SUPPPRESS /* wtf pmbuf */))
    {
      pw = getpwuid(uid);
      if (pw)
        tx_copypwent(pwbuf, buf, bufsz, pw);
      TXcriticalSectionExit(TXgetpwCsect);
    }
#  endif /* !EPI_HAVE_GETPWUID_R_PASSWD && !EPI_HAVE_GETPWUID_R_INT */
  return(pw);
}

struct passwd *
TXgetpwnam_r(name, pwbuf, buf, bufsz)
CONST char      *name;
struct passwd   *pwbuf;
char            *buf;
size_t          bufsz;
/* Thread-safe.
 */
{
  struct passwd *pw;

  memset(pwbuf, 0, sizeof(struct passwd));
#  ifdef EPI_HAVE_GETPWUID_R_PASSWD
  pw = getpwnam_r(name, pwbuf, buf, bufsz);
#  elif defined(EPI_HAVE_GETPWUID_R_INT)
  if (getpwnam_r(name, pwbuf, buf, bufsz, &pw) != 0)
    pw = (struct passwd *)NULL;
#  else /* !EPI_HAVE_GETPWUID_R_PASSWD && !EPI_HAVE_GETPWUID_R_INT */
  pw = NULL;
  if (TXcriticalSectionEnter(TXgetpwCsect, TXPMBUF_SUPPRESS /* wtf pmbuf */))
    {
      pw = getpwnam(name);
      if (pw)
        tx_copypwent(pwbuf, buf, bufsz, pw);
      TXcriticalSectionExit(TXgetpwCsect);
    }
#  endif /* !EPI_HAVE_GETPWUID_R_PASSWD && !EPI_HAVE_GETPWUID_R_INT */
  return(pw);
}
#endif /* !_WIN32 */

char *
TXgetRealUserName(pmbuf)
TXPMBUF *pmbuf; /* (out) for messages */
/* Returns alloced real user name, or NULL on error.
 */
{
  static CONST char     fn[] = "TXgetRealUserName";
  char                  tmp[1024];
#ifdef _WIN32
  DWORD                 dwSz;

  dwSz = sizeof(tmp);
  if (GetUserName(tmp, &dwSz))
    return(TXstrdup(pmbuf, fn, tmp));
  txpmbuf_putmsg(pmbuf, MERR, fn, "Cannot GetUserName(): %s",
                 TXstrerror(TXgeterror()));
  return(CHARPN);
#else /* !_WIN32 */
  UID_T                 uid;
  struct passwd         *pw, pwbuf;
  char                  *res;

  uid = getuid();
  pw = TXgetpwuid_r(uid, &pwbuf, tmp, sizeof(tmp));
  if (!pw)
    {
      htsnpf(tmp, sizeof(tmp), "%u", (unsigned)uid);
      res = tmp;
    }
  else
    res = pw->pw_name;
  return(TXstrdup(pmbuf, fn, res));
#endif /* !_WIN32 */
}

char *
TXgetEffectiveUserName(pmbuf)
TXPMBUF *pmbuf; /* (out) for messages */
/* Returns alloced effective user name, or NULL on error.
 */
{
#ifdef _WIN32
  return(TXgetRealUserName(pmbuf));
#else /* !_WIN32 */
  static CONST char     fn[] = "TXgetRealUserName";
  char                  tmp[1024];
  UID_T                 uid;
  struct passwd         *pw, pwbuf;
  char                  *res;

  uid = geteuid();
  pw = TXgetpwuid_r(uid, &pwbuf, tmp, sizeof(tmp));
  if (!pw)
    {
      htsnpf(tmp, sizeof(tmp), "%u", (unsigned)uid);
      res = tmp;
    }
  else
    res = pw->pw_name;
  return(TXstrdup(pmbuf, fn, res));
#endif /* !_WIN32 */
}

int
TXisUserAdmin(void)
/* Returns 1 if current effective user is root (Unix) or in local
 * Administrators group (Windows), 0 if not, -1 on error.
 */
{
#ifdef _WIN32
  int                           ret, freeSid = 0;
  BOOL                          b;
  SID_IDENTIFIER_AUTHORITY      ntAuthority = SECURITY_NT_AUTHORITY;
  PSID                          administratorsGroup;

  if (!AllocateAndInitializeSid(&ntAuthority, 2,
                               SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS,
                               0, 0, 0, 0, 0, 0,
                                &administratorsGroup))
    goto err;
  freeSid = 1;
  if (!CheckTokenMembership(NULL, administratorsGroup, &b))
    goto err;
  ret = (b ? 1 : 0);
  goto finally;

err:
  ret = -1;
finally:
  if (freeSid)
    {
      FreeSid(administratorsGroup);
      freeSid = 0;
    }
  return(ret);
#else /* !_WIN32 */
  return(geteuid() == 0);
#endif /* !_WIN32 */
}

/****************************************************************************/

static char *TXreplaceVarsActual ARGS((TXPMBUF *pmbuf, CONST char *val,
    CONST char * CONST *vars, size_t numVars,
    CONST char * CONST *vals, CONST int *valsAreExpanded, int *varsWereUsed,
    CONST char *topVal, size_t topVarOffset, size_t topVarLen));
static char *
TXreplaceVarsActual(pmbuf, val, vars, numVars, vals, valsAreExpanded,
                    varsWereUsed, topVal, topVarOffset, topVarLen)
TXPMBUF                 *pmbuf; /* (in, opt.) buffer for putmsgs */
CONST char              *val;   /* (in) string to expand */
CONST char * CONST      *vars;  /* (in) array of variable names */
size_t                  numVars;/* (in) length of `vars' */
CONST char * CONST      *vals;  /* (in) array of variable values */
CONST int               *valsAreExpanded;       /* (in, opt.) boolean array */
int                     *varsWereUsed;  /* (in) boolean array */
CONST char              *topVal;        /* (in) top-level `val' */
size_t                  topVarOffset;   /* (in) offset in `topVal' of var */
size_t                  topVarLen;      /* (in) "" length */
/* Guts of tx_replacevars(); recursive function.
 * First/top call must have `topVal == val'.
 */
{
  static CONST char     fn[] = "TXreplaceVarsActual";
  CONST char            *s, *e, *bn, *exeFileName = CHARPN;
  CONST char            *replaceStr = CHARPN;
  char                  *d, *newBuf, *replaceAlloc = CHARPN;
  size_t                newSz, replaceSz, totalNeeded, i;
  char                  *expBufAlloc = CHARPN, *expBuf;
  char                  expBufTmp[512];
  size_t                expBufSz;

  /* And init `expBuf' etc.: */
  expBuf = expBufTmp;
  expBufSz = sizeof(expBufTmp);

  d = expBuf;
  for (s = val; *s != '\0'; s = e)              /* for entire `val' string */
    {
      if (replaceAlloc != CHARPN) replaceAlloc = TXfree(replaceAlloc);

      /* Find next var ref, or replace as-is if literals/incomplete/etc.: */
      e = strchr(s, '%');
      if (e == CHARPN) e = s + strlen(s);
      if (e > s) goto asIs;                     /* some plain chars */
      for (e = ++s; *e != '\0' && *e != '%'; e++);      /* get var name end */
      if (*e != '%')                            /* incomplete var reference */
        {
          s--;                                  /* back up to initial '%' */
        asIs:
          replaceStr = s;
          replaceSz = e - s;
          goto replaceIt;
        }
      if (s == e)                               /* `%%' */
        {
          replaceStr = s;
          replaceSz = ++e - s;
          goto replaceIt;
        }

      /* Look up var, determine replacement string (maybe) and size: */
      replaceStr = CHARPN;
      for (i = 0; i < numVars; i++)             /* look up var name */
        {
          if (strncmp(s, vars[i], e - s) == 0 && vars[i][e - s] == '\0')
            break;
        }
      if (i < numVars)                          /* found it */
        {
          if (varsWereUsed[i])                  /* infinite recursion */
            {
              txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                             "Variable reference loop encountered while expanding var `%.*s' at byte %d in value `%s'",
                             /* It is more useful to print the
                              * top-level (i.e. user-visible) var
                              * `topVarOffset' involved in the loop,
                              * rather than the immediate var `s'
                              * encountered, which may differ and be
                              * buried N levels deep in var refs:
                              */
                             (int)topVarLen, topVal + topVarOffset,
                             (int)topVarOffset,
                             topVal);
              goto err;
            }
          if (vals[i] == TXREPLACEVAL_BINDIR)
            goto doBinDir;
          else if (vals[i] == TXREPLACEVAL_EXEDIR)
            {
              if ((exeFileName = TXgetExeFileName()) != CHARPN)
                {
                  bn = TXbasename(exeFileName);
                  /* Skip last `/', if it is not the first char: */
                  if (bn > exeFileName + 1 &&
                      (bn[-1] == PATH_SEP
#ifdef _WIN32
                       || bn[-1] == '/'
#endif /* _WIN32 */
                       ))
                    bn--;
                  replaceStr = exeFileName;
                  replaceSz = bn - exeFileName;
                }
              else                              /* fallback to %BINDIR% */
                {
                doBinDir:
                  replaceStr = TXREPLACEVAL_BINDIR; /* fixed later */
                  replaceSz = strlen(TXINSTALLPATH_VAL)
#ifndef _WIN32
                    + 4
#endif /* !_WIN32 */
                    ;
                }
            }
          else                                  /* plain string val */
            {
              if (valsAreExpanded != INTPN && !valsAreExpanded[i])
                {
                  /* We must recursively expand `vals[i]' if needed: */
                  varsWereUsed[i]++;
                  replaceAlloc = TXreplaceVarsActual(pmbuf, vals[i],
                                                     vars, numVars, vals,
                                                     valsAreExpanded,
                                                     varsWereUsed,
                                                     topVal,
                                                     (topVal == val ?
                                                      (size_t)((s-1) - val) :
                                                      topVarOffset),
                                                     (topVal == val ?
                                                      (size_t)((e-s) + 2) :
                                                      topVarLen));
                  varsWereUsed[i]--;
                  if (replaceAlloc == CHARPN) goto err;
                  replaceStr = replaceAlloc;
                  replaceSz = strlen(replaceStr);
                }
              else                              /* already expanded */
                {
                  replaceStr = vals[i];
                  replaceSz = strlen(vals[i]);
                }
            }
          e++;                                  /* skip trailing `%' */
        }
      else                                      /* unknown var */
        {
          s--;                                  /* back up to initial '%' */
          replaceStr = s;
          replaceSz = ++e - s;
        }

      /* See if `expBuf' needs realloc: */
    replaceIt:
      totalNeeded = (d + replaceSz + 1) - expBuf;
      if (totalNeeded > expBufSz)               /* realloc needed */
        {
          newSz = (expBufSz + (expBufSz >> 1));
          if (newSz < totalNeeded) newSz = totalNeeded;
          newBuf = (char *)TXrealloc(pmbuf, fn, expBufAlloc, newSz);
          if (newBuf == CHARPN)                 /* alloc failed */
            goto err;
          else if (expBufAlloc == CHARPN)
            memcpy(newBuf, expBuf, d - expBuf);
          /* Bug 5033: update `d' too: */
          d = newBuf + (d - expBuf);
          expBuf = newBuf;
          expBufSz = newSz;
        }

      /* Copy the replacement over: */
      if (replaceStr == TXREPLACEVAL_BINDIR)
        {
          strcpy(d, TXINSTALLPATH_VAL);
          d += strlen(TXINSTALLPATH_VAL);
#ifndef _WIN32
          strcpy(d, PATH_SEP_S "bin");
          d += 4;
#endif /* !_WIN32 */
        }
      else
        {
          memcpy(d, replaceStr, replaceSz);
          d += replaceSz;
        }
    }
  *d = '\0';                                    /* terminate */
  if (replaceAlloc != CHARPN) replaceAlloc = TXfree(replaceAlloc);
  /* Dup `expBuf' if not already: */
  if (expBufAlloc == CHARPN)
    {
      if ((expBufAlloc = TXstrdup(pmbuf, fn, expBuf)) == CHARPN)
        goto err;
    }
  goto done;

err:
  if (expBufAlloc != CHARPN) expBufAlloc = TXfree(expBufAlloc);
  expBuf = CHARPN;
done:
  if (replaceAlloc != CHARPN) replaceAlloc = TXfree(replaceAlloc);
  return(expBufAlloc);
}

char *
tx_replacevars(pmbuf, val, yap, vars, numVars, vals, valsAreExpanded)
TXPMBUF                 *pmbuf; /* (in, opt.) buffer for putmsgs */
CONST char              *val;   /* (in) string to expand */
int                     yap;    /* (in) nonzero: issue msgs on errors */
CONST char * CONST      *vars;  /* (in) array of variable names */
size_t                  numVars;/* (in) length of `vars' */
CONST char * CONST      *vals;  /* (in) parallel array of `vars' values */
CONST int               *valsAreExpanded;       /* (in, opt.) boolean array */
/* Returns malloc'd copy of `val' with embedded `vars' replaced, recursing.
 * If `valsAreExpanded' is non-NULL, it is an array of booleans indicating
 * whether each `vals' member is already var-expanded or not.  If
 * `valsAreExpanded' is NULL, all `vals' are assumed to be already expanded.
 */
{
  static CONST char     fn[] = "tx_replacevars";
  int                   varsWereUsedTmp[16], *varsWereUsedAlloc = INTPN;
  int                   *varsWereUsed = INTPN;
  char                  *ret = CHARPN;

  if (!yap) pmbuf = TXPMBUF_SUPPRESS;

  /* Alloc and clear `varsWereUsed': */
  if (numVars <= sizeof(varsWereUsedTmp)/sizeof(varsWereUsedTmp[0]))
    {
      memset(varsWereUsedTmp, 0, sizeof(varsWereUsedTmp));
      varsWereUsed = varsWereUsedTmp;
    }
  else if (!(varsWereUsedAlloc = (int *)TXcalloc(pmbuf, fn, numVars,
                                                 sizeof(int))))
    goto err;
  else
    varsWereUsed = varsWereUsedAlloc;

  ret = TXreplaceVarsActual(pmbuf, val, vars, numVars, vals,
                            valsAreExpanded, varsWereUsed, val, 0, 0);
  goto done;

err:
  ret = TXfree(ret);
done:
  if (varsWereUsedAlloc != INTPN)
    varsWereUsedAlloc = TXfree(varsWereUsedAlloc);
  return(ret);
}

const char *
TXgetCodeDescription(const TXCODEDESC *list, int code, const char *unkCodeDesc)
/* Returns description for `code' if found in NULL-terminated `list',
 * else `unkCodeDesc'.
 * Thread-safe.  Signal-safe.
 */
{
  for ( ; list->description; list++)
    if (list->code == code) return(list->description);
  return(unkCodeDesc);
}

/****************************************************************************/

int
TXsplitdomainuser(pmbuf, domain, user, adomain, auser)
TXPMBUF         *pmbuf;         /* (out, opt.) for messages */
CONST char      *domain;        /* (in) optional domain */
CONST char      *user;          /* (in) user  domain\user  user@domain */
char            **adomain;      /* (out) allocated domain (NULL if none) */
char            **auser;        /* (out) allocated user */
/* Splits domain/user combo strings `domain'/`user' into domain and user.
 * Returns alloc'd strings `*adomain' and `*auser' with just domain
 * (NULL if none) and just user; or NULL and error set on error.
 */
{
  static CONST char     fn[] = "TXsplitdomainuser";
  CONST char            *u;
  size_t                dlen, ulen;

  if (domain != CHARPN)                         /* use `domain'/`user' as-is*/
    {
      dlen = strlen(domain);
      ulen = strlen(user);
    }
  else                                          /* domain part of `user'? */
    {
      if ((u = (CONST char *)strchr(user, '\\')) != CHARPN)
        {                                       /* Domain\User */
          domain = user;
          dlen = u - domain;
          user = ++u;
          ulen = strlen(user);
        }
      /* do not split on forward slash: Windows logon does not */
      else if ((u = (CONST char *)strchr(user, '@')) != CHARPN)
        {                                       /* User@Domain */
          domain = u + 1;
          dlen = strlen(domain);
          ulen = u - user;
        }
      else                                      /* User */
        {
          domain = CHARPN;                      /* i.e. local machine only */
          dlen = 0;
          ulen = strlen(user);
        }
    }
  if (domain != CHARPN)
    {
      if (!(*adomain = (char *)TXmalloc(pmbuf, fn, dlen + 1))) goto perr;
      memcpy(*adomain, domain, dlen);
      (*adomain)[dlen] = '\0';
    }
  else
    *adomain = CHARPN;
  if (!(*auser = (char *)TXmalloc(pmbuf, fn, ulen + 1)))
    {
    perr:
      TX_PUSHERROR();
      *adomain = TXfree(*adomain);
      *adomain = *auser = CHARPN;
      TX_POPERROR();
      return(0);
    }
  memcpy(*auser, user, ulen);
  (*auser)[ulen] = '\0';
  return(1);
}

/****************************************************************************/

char *
tx_c2dosargv(argv, quote)
char    **argv;
int     quote;  /* double-quote args that need it */
/* Free with free().  Sets errno/TXseterror() on error.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "tx_c2dosargv";
  char                  **ap;
  size_t                n;
  char                  *glob, *s;
  int                   quoteIt;                /* quote current arg or not */

  for (n = 0, ap = argv; *ap != CHARPN; ap++)
    n += strlen(*ap) + 3;                       /* inc. possible quotes */
  if ((glob = (char *)TXmalloc(TXPMBUFPN, fn, n)) == CHARPN)
    return(CHARPN);
  for (s = glob, ap = argv; *ap != CHARPN; ap++)
    {
      if (s > glob) *(s++) = ' ';               /* space-separate args */
      n = strlen(*ap);
      quoteIt = (quote &&                       /* quote option set and */
                 (n == 0 ||                     /*   (empty-string arg or */
                  (strcspn(*ap, Whitespace) != n && /* (`*ap' has whitespace*/
                   strchr(*ap, '"') == CHARPN))); /*    and has no quotes)) */
      if (quoteIt) *(s++) = '"';
      memcpy(s, *ap, n);
      s += n;
      if (quoteIt) *(s++) = '"';
    }
  *s = '\0';
  return(glob);
}

/****************************************************************************/

char **
tx_dos2cargv(cmdline, unquote)
CONST char      *cmdline;
int             unquote;        /* remove double quotes from args */
/* Creates alloc'd argv[] array for DOS-style command line `cmdline'.
 * Args are whitespace-separated, or double-quoted.
 * If `unquote', removes double quotes.
 */
{
  static CONST char     fn[] = "tx_dos2cargv";
  CONST char            *s, *e;
  size_t                n = 0;
  char                  qch;
  char                  **ret = CHARPPN, *d;
  int                   mkit;

  for (mkit = 0; mkit < 2; mkit++)              /* 2 passes: count and make */
    {
      if (mkit &&
          (ret = (char **)calloc(n + 1, sizeof(char *))) == CHARPPN)
        goto maerr;
      s = cmdline;
      s += strspn(s, Whitespace);               /* skip inter-arg space */
      for (n = 0; *s != '\0'; n++)              /* for each arg */
        {
          for (e = s, qch = '\0'; *e != '\0'; e++)
            {                                   /* for each char this arg */
              if (qch != '\0')                  /* we're in quotes */
                {
                  if (*e == qch)                /* ending quote */
                    {
                      qch = '\0';
                      continue;
                    }
                }
              else if (*e == '"')               /* starting quote */
                {
                  qch = *e;
                  continue;
                }
              else if (strchr(Whitespace, *e) != CHARPN)
                break;                          /* end of arg */
            }
          if (mkit)                             /* alloc arg */
            {
              if ((ret[n] = (char *)malloc((e - s) + 1)) == CHARPN)
                {
                maerr:
                  putmsg(MERR + MAE, fn, OutOfMem);
                  goto err;
                }
              for (d = ret[n]; s < e; s++)
                {
                  if (unquote && *s == '"') continue;
                  *(d++) = *s;
                }
              *d = '\0';
            }
          s = e;
          s += strspn(s, Whitespace);           /* skip inter-arg space */
        }
    }
  goto done;

err:
  ret = freenlst(ret);
done:
  return(ret);
}

/******************************************************************/

int
TXprocessexists(pid)
PID_T pid;
/* Returns 1 if process `pid' exists, 0 if not.
 * Thread-safe.
 */
{
#if defined(unix) || defined(__unix)
	if (pid > 0)
	{
		if (kill(pid, 0) == -1)
		{
			switch (TXgeterror())
			{
			case ESRCH:
				return 0;
			}
		}
	}
	else
		return 0;
	return 1;
#elif defined(_WIN32)
	HANDLE	handle;
	int	res;

	handle = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
	if (handle != NULL)
	{
		res = TXprocessexists_handle(handle);
		CloseHandle(handle);
		return(res);
	}
	else switch (GetLastError())
	{
		case ERROR_INVALID_PARAMETER:
			return 0;
		default:
			return 1;
	}
	return 0;
#else /* !unix && !__unix && !_WIN32 */
        error error error;
#endif /* !unix && !__unix && !_WIN32 */
}

int
TXkill(pid, sig)
PID_T   pid;
int     sig;    /* -1 == SIGTERM or equivalent; -2 == SIGKILL or equivalent */
/* Kills process `pid' with signal `sig'.  Maps 0/SIGINT/SIGBREAK/SIGTERM
 * to process-exists/Ctrl-Btreak/Texis-Terminate for Windows.
 * Returns 1 success, 0 on error.
 * Thread-safe.
 * Signal-safe under Unix (just maps to kill()).
 */
{
#ifdef _WIN32
  HANDLE        hn;
  BOOL          r = FALSE;
  char		buf[sizeof(TXterminateEventPrefix) + EPI_OS_INT_BITS/3 + 3];

  if (sig == -1)
#  ifdef SIGTERM
    sig = SIGTERM;
#  else /* !SIGTERM */
    sig = 15;
#  endif /* !SIGTERM */
  else if (sig == -2)
#  ifdef SIGKILL
    sig = SIGKILL;
#  else /* !SIGKILL */
    sig = 9;
#  endif /* !SIGKILL */
  switch (sig)
    {
    case 0:
      /* Map to Unix behavior of process-exists: */
      r = TXprocessexists(pid);
      break;
#  ifdef SIGINT
    case SIGINT:
#  endif /* SIGINT */
#  ifdef SIGBREAK
    case SIGBREAK:
#  endif /* SIGBREAK */
      /* Map SIGINT/SIGBREAK to Ctrl-C/Ctrl-Break, for symmetry with
       * Windows signal(SIGINT) which receives Ctrl-C/Ctrl-Break.
       * Note limitations of GenerateConsoleCtrlEvent(): it sends to
       * all processes in the process group `pid'; caller must share
       * the same console as target (else returns failure);
       * CTRL_C_EVENT does nothing (but returns success).
       * So map both SIGINT and SIGBREAK to Ctrl-Break:
       */
#  if defined(SIGINT) || defined(SIGBREAK)
      r = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
      break;
#  endif /* SIGINT || SIGBREAK */
#  ifdef SIGTERM
    case SIGTERM:
#  else /* !SIGTERM */
    case 15:
#  endif /* !SIGTERM */
      /* Map SIGTERM to our TEXIS_TERMINATE_PID_nnn event for soft kill,
       * for Unix symmetry.  Of course this only works on Texis processes:
       */
      htsnpf(buf, sizeof(buf), "%s%u", TXterminateEventPrefix, (int)pid);
      hn = TXOpenEvent(EVENT_ALL_ACCESS, FALSE, buf, 0);
      if (hn == NULL) return(0);
      r = SetEvent(hn);
      CloseHandle(hn);
      break;
#  ifdef SIGKILL
    case SIGKILL:
      /* Map to hard kill, for Unix symmetry: */
#  else /* !SIGKILL */
    case 9:
#  endif /* !SIGKILL */
    default:
      hn = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
      if (hn == NULL) return(0);
      r = TerminateProcess(hn, sig);
      CloseHandle(hn);
      break;
    }
  return(r);
#else /* !_WIN32 */
  if (sig == -1) sig = SIGTERM;
  else if (sig == -2) sig = SIGKILL;
  return(kill(pid, sig) == 0);
#endif /* !_WIN32 */
}

#ifdef _WIN32
int
TXprocessexists_handle(proc)
HANDLE  proc;
/* Returns 1 if process `proc' exists, 0 if not.
 * Thread-safe.
 */
{
  DWORD errcode;

  return(GetExitCodeProcess(proc, &errcode) && errcode == STILL_ACTIVE);
}

static char *tx_c2dosenv ARGS((CONST char * CONST env[]));
static char *
tx_c2dosenv(env)
CONST char * CONST      env[];
/* Returns DOS-style environment list for NULL-terminated environment list
 * `env'.  Should be free()d when done.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "tx_c2dosenv";
  CONST char * CONST    *p;
  size_t                sz;
  char                  *ret, *d;

  for (sz = 1, p = env; *p != CHARPN && **p != '\0'; p++)
    sz += strlen(*p) + 1;
  if ((ret = (char *)malloc(sz)) == CHARPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      return(CHARPN);
    }
  for (d = ret, p = env; *p != CHARPN && **p != '\0'; p++)
    {
      strcpy(d, *p);
      d += strlen(*p) + 1;
    }
  *d = '\0';
  return(ret);
}

static void tx_perr ARGS((int num, CONST char *fn, CONST char *what,
                          PID_T pid));
static void
tx_perr(num, fn, what, pid)
int             num;
CONST char      *fn;
CONST char      *what;
PID_T           pid;
{
  char          *desc, *cmd;
  int           sig, xit;
  char          tmp[EPI_OS_INT_BITS/3+3];

  if (!TXgetprocxit(pid, 0, &sig, &xit, &desc, &cmd, NULL))
    {
      desc = "process";
      htsnpf(tmp, sizeof(tmp), "%d", (int)pid);
      cmd = tmp;
    }
  switch (GetLastError())
    {
    case ERROR_BROKEN_PIPE:         /* process ended? */
    case ERROR_NO_DATA:
      break;
    default:
      putmsg(num, fn, "Cannot %s %s %s: %s", what, desc, cmd,
             TXstrerror(TXgeterror()));
      break;
    }
}

static WCHAR *tx_ansi2unicode ARGS((CONST char *s));
static WCHAR *
tx_ansi2unicode(s)
CONST char      *s;
{
  static CONST char     fn[] = "tx_ansi2unicode";
  int                   len;
  size_t                sSz;
  WCHAR                 *ret;
  WCHAR                 tmp[UNLEN + 1];

  sSz = strlen(s) + 1;
  if (sSz > (size_t)EPI_OS_INT_MAX)
    {
      putmsg(MERR + MAE, fn, "String too long");
      return(WCHARPN);
    }
  if ((len = MultiByteToWideChar(CP_ACP, 0, s, (int)sSz, tmp,
                                 (int)(sizeof(tmp)/sizeof(tmp[0])))) == 0)
    {
      putmsg(MERR, fn, "Cannot translate ANSI string to Unicode: %s",
             TXstrerror(TXgeterror()));
      return(WCHARPN);
    }
  if ((ret = (WCHAR *)_wcsdup(tmp)) == WCHARPN)
    {
      putmsg(MERR + MAE, fn, OutOfMem);
      return(WCHARPN);
    }
  return(ret);
}

static TXTHREADRET TXTHREADPFX tx_prw_iothread ARGS((void *arg));
static TXTHREADRET TXTHREADPFX
tx_prw_iothread(arg)
void    *arg;
/* I/O thread for individual stdio pipe (parent-process end).
 * Waits for parent thread to signal it to read/write data from/to its pipe.
 * Returns (exits) on severe error or if parent thread tells it to.
 * NOTE that creator creates this thread with a small stack.
 * Thread-safe (but note parent-thread usage of TXPIPEOBJ * `arg).
 */
{
  static CONST char     fn[] = "tx_prw_iothread";
  TXPIPEOBJ             *pobj = (TXPIPEOBJ *)arg;
  HANDLE                handles[2];
  const char            *handleNames[2];
  char                  handleNameBufs[2][32];
  DWORD                 res, actlen;
  CONST char            *exitreason = "unknown reason";
  TRACEPIPE_VARS;

  __try
    {

  TRACEPIPE_BEFORE_START(TPF_THREAD_RUN)
    txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_BEFORE, __FUNCTION__,
                   "%sIoThread=%p starting", StdioName[pobj->stdIdx],
                   (void *)pobj->iothread);
  TRACEPIPE_BEFORE_END()

  for (;;)
    {
      /* Wait for parent thread to signal us, or to unexpectedly exit: */
      handles[0] = pobj->iobeginevent;          /* signal for us to start */
      htsnpf(handleNameBufs[0], sizeof(handleNameBufs[0]), "%sIoBeginEvent",
             StdioName[pobj->stdIdx]);
      handleNames[0] = handleNameBufs[0];
      handles[1] = pobj->parentthread;          /* parent exited */
      handleNames[1] = "parentThread";
      switch (TXezWaitForMultipleObjects(pobj->pmbuf,
                                         TX_TRACEPIPE_TO_TRACESKT(TxTracePipe),
                                         __FUNCTION__, handles, 2,
                                         handleNames, -1.0, TXbool_False))
        {
        case WAIT_ABANDONED_0:                  /* cannot happen? */
        case WAIT_OBJECT_0:                     /* we have been signalled */
          TRACEPIPE_BEFORE_START(TPF_EVENT)
            if (TxTracePipe & TPF_BEFORE(TPF_EVENT))
              txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_BEFORE, __FUNCTION__,
                             "ResetEvent(%sIoBeginEvent=%p): starting",
                             StdioName[pobj->stdIdx],
                             (void *)pobj->iobeginevent);
          TRACEPIPE_BEFORE_END()
          res = ResetEvent(pobj->iobeginevent); /* clear for next time */
          TRACEPIPE_AFTER_START(TPF_EVENT)
            txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_AFTER, fn,
                        "ResetEvent(%sIoBeginEvent=%p): %1.3f sec %s err %d=%s",
                           StdioName[pobj->stdIdx],
                           (void *)pobj->iobeginevent,
                           TRACEPIPE_TIME(), (res ? Ok : Failed),
                           (int)saveErr, TXgetOsErrName(saveErr, Ques));
          TRACEPIPE_AFTER_END()
          break;
        case WAIT_ABANDONED_0 + 1:              /* cannot happen? */
          exitreason = "parent thread abandoned";
          goto done;                            /* we exit too */
        case WAIT_OBJECT_0 + 1:                 /* parent thread exited */
          exitreason = "parent thread exited";
          goto done;                            /* we exit too */
        case WAIT_FAILED:                       /* error */
        case WAIT_TIMEOUT:                      /* should not happen */
        default:
          Sleep(100);                           /* do not burn CPU */
          continue;                             /* try again */
        }

      /* Do the task assigned by parent thread: */
      if (pobj->task == TPOTASK_IDLE) goto cont;/* do nothing */
      if (pobj->task == TPOTASK_EXIT)           /* we should exit */
        {
          exitreason = "TPOTASK_EXIT requested";
          goto done;
        }
      if (!TXFHANDLE_IS_VALID(pobj->fh))
        {
          pobj->resulterrnum = ERROR_INVALID_HANDLE;
          pobj->resultlen = (size_t)0;
        }
      else if (pobj->task == TPOTASK_READ)      /* read from pipe */
        {
          if (pobj->iobuf == CHARPN || pobj->iobufsztodo <= (size_t)0)
            {                                   /* sanity check */
              pobj->resulterrnum = ERROR_NOT_ENOUGH_MEMORY;
              pobj->resultlen = (size_t)0;
              goto cont;
            }
          /*   We used to check for process existence, and then did a
           * PeekNamedPipe(), before attempting a read.  Hopefully not
           * needed now that we have separate I/O threads, and can
           * terminate them from the main thread.
           *   We will read to a separate buffer `pboj->iobuf', so that
           * parent thread can return from TXpreadwrite() safely:
           * our read might happen while the parent thread is modifying
           * `pobj->buf'.  Downside is a memcpy() into `pobj->buf' later:
           */
          TRACEPIPE_BEFORE_START(TPF_READ | TPF_READ_DATA)
            if (TxTracePipe & TPF_BEFORE(TPF_READ))
              txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_BEFORE, fn,
                             "ReadFile(%sExecRead=%p) of %wd bytes: starting",
                             StdioName[pobj->stdIdx], (void *)pobj->fh,
                             (EPI_HUGEINT)pobj->iobufsztodo);
            if ((TxTracePipe & TPF_BEFORE(TPF_READ_DATA)) &&
                pobj->iobufsztodo > 0)
              tx_hexdumpmsg(pobj->pmbuf, TPF_MSG_BEFORE + TPF_MSG_READ_DATA,
                            CHARPN, pobj->iobuf, pobj->iobufsztodo, 1);
          TRACEPIPE_BEFORE_END()
          res = (DWORD)ReadFile(pobj->fh, pobj->iobuf,
                                (DWORD)pobj->iobufsztodo, &actlen, NULL);
          TRACEPIPE_AFTER_START(TPF_READ | TPF_READ_DATA)
            if (TxTracePipe & TPF_READ)
              txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_AFTER, fn,
                             "ReadFile(%sExecRead=%p) of %wd bytes: %1.3lf sec %s read %wd bytes err %d=%s",
                             StdioName[pobj->stdIdx], (void *)pobj->fh,
                             (EPI_HUGEINT)pobj->iobufsztodo,
                             TRACEPIPE_TIME(), (res ? Ok : Failed),
                             (EPI_HUGEINT)actlen, (int)saveErr,
                             TXgetOsErrName(saveErr, Ques));
            if ((TxTracePipe & TPF_READ_DATA) && res && actlen > 0)
              tx_hexdumpmsg(pobj->pmbuf, TPF_MSG_AFTER + TPF_MSG_READ_DATA,
                            CHARPN, pobj->iobuf, actlen, 1);
          TRACEPIPE_AFTER_END()
        }
      else if (pobj->task == TPOTASK_WRITE)     /* write to pipe */
        {
          if (pobj->iobuf == CHARPN || pobj->iobufsztodo <= (size_t)0)
            {                                   /* sanity check */
              pobj->resulterrnum = ERROR_NOT_ENOUGH_MEMORY;
              pobj->resultlen = (size_t)0;
              goto cont;
            }
          /* KNG 020628 WriteFile() won't return until the entire
           * buffer is written, and it won't fail if the process at
           * the other end of the pipe has exited.  We used to check
           * for process existence here; now parent thread does it.
           * Still a race condition; WriteFile() could block forever,
           * so we'd like to use async I/O e.g. WriteFileEx().  But
           * that's not supported for anonymous pipes, so we use threads.
           * Parent thread can terminate us if we block (plus parent
           * is servicing reads via other threads, so we shouldn't block):
           */
          TRACEPIPE_BEFORE_START(TPF_WRITE | TPF_WRITE_DATA)
            if (TxTracePipe & TPF_BEFORE(TPF_WRITE))
              txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_BEFORE, fn,
                             "WriteFile(%sWrite=%p) of %wd bytes: starting",
                             StdioName[pobj->stdIdx], (void *)pobj->fh,
                             (EPI_HUGEINT)pobj->iobufsztodo);
            if ((TxTracePipe & TPF_BEFORE(TPF_WRITE_DATA)) &&
                pobj->iobufsztodo > 0)
              tx_hexdumpmsg(pobj->pmbuf, TPF_MSG_BEFORE + TPF_MSG_WRITE_DATA,
                            CHARPN, pobj->iobuf, pobj->iobufsztodo, 1);
          TRACEPIPE_BEFORE_END()
          res = (DWORD)WriteFile(pobj->fh, pobj->iobuf,
                                 (DWORD)pobj->iobufsztodo, &actlen, NULL);
          TRACEPIPE_AFTER_START(TPF_WRITE | TPF_WRITE_DATA)
            if (TxTracePipe & TPF_WRITE)
              txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_AFTER, fn,
"WriteFile(%sWrite=%p) of %wd bytes: %1.3lf sec %s wrote %wd bytes err %d=%s",
                             StdioName[pobj->stdIdx], (void *)pobj->fh,
                             (EPI_HUGEINT)pobj->iobufsztodo,
                             TRACEPIPE_TIME(), (res ? Ok : Failed),
                             (EPI_HUGEINT)actlen, (int)saveErr,
                             TXgetOsErrName(saveErr, Ques));
            if ((TxTracePipe & TPF_WRITE_DATA) && res && actlen > 0)
              tx_hexdumpmsg(pobj->pmbuf, TPF_MSG_AFTER + TPF_MSG_WRITE_DATA,
                            CHARPN, pobj->iobuf, actlen, 1);
          TRACEPIPE_AFTER_END()
        }
      else                                      /* unknown task */
        {
          res = 0;                              /* failure */
          TXseterror(ERROR_INVALID_FUNCTION);
        }
      if (res)                                  /* success */
        {
          pobj->resulterrnum = (TXERRTYPE)0;
          pobj->resultlen = (size_t)actlen;
        }
      else                                      /* failure */
        {
          pobj->resulterrnum = TXgeterror();
          /* Make sure parent thread knows call failed; set nonzero err: */
          if (pobj->resulterrnum == (TXERRTYPE)0)
            pobj->resulterrnum = (TXERRTYPE)(-1);
          pobj->resultlen = (size_t)0;
        }

    cont:
      /* Signal parent thread that we're done with `pobj->task': */
      SetEvent(pobj->ioendevent);
    }

done:
  /* MS C apparently needs a null statement here */
  ;} __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
   {
   }
  TRACEPIPE_AFTER_START(TPF_THREAD_RUN)
    txpmbuf_putmsg(pobj->pmbuf, TPF_MSG_AFTER, fn,
                   "%sIoThread=%p exiting: %s",
                   StdioName[pobj->stdIdx], (void *)pobj->iothread,
                   exitreason);
  TRACEPIPE_AFTER_END()
  return(0);
}

#else /* !_WIN32 ---------------------------------------------------------- */

PID_T
TXfork(TXPMBUF *pmbuf, const char *description, const char *cmdArgs,
       int flags)
/* Wrapper for fork() that does some Texis housekeeping.
 * `description' is optional; process description for non-zero exit message.
 * Valid `flags':
 *   0x01  no reaping (no TxInForkFunc nor TXaddproc calls)
 *   0x02  regroup
 *   0x04  close descriptors > stdio (but not tx_savefd() descriptors)
 *   0x08  close stdio and tx_savefd() descriptors
 * NOTE: see also fork() in TXpopenduplex(), which does not call this.
 */
{
  static const char     fn[] = "TXfork";
  PID_T                 pid;
  TXERRTYPE             saveErr;
  TXTHREADID            parentTid;

  if (!(flags & 0x01) && TxInForkFunc) TxInForkFunc(1);

  parentTid = TXgetcurrentthreadid();
  pid = fork();
  switch (pid)
    {
    case -1:                                    /* failed */
      saveErr = TXgeterror();
      if (!(flags & 0x01) && TxInForkFunc) TxInForkFunc(0);
      txpmbuf_putmsg(pmbuf, MERR + FRK, fn, "Cannot fork(): %s",
                     TXstrerror(saveErr));
      break;
    case 0:                                     /* in child process */
      (void)TX_ATOMIC_INC(&TXinForkedChild);
      TXpid = 0;                                /* invalidate cache */
      TXprocessStartTime = -1.0;                /* "" */
      TXsetProcessStartTime();
      TXthreadFixInfoAfterFork(parentTid);
      /* do not call TxInForkFunc(0): may try to reap? */
      /* call childpostfork here */
      if (flags & 0x02) TXregroup();
      if (flags & 0x0c)
        TXclosedescriptors(((flags & 0x04) ? 0x2 : 0x0) |
                           ((flags & 0x08) ? 0x5 : 0x0));
      break;
    default:                                    /* parent process */
      if (!(flags & 0x01))
        {
          TXaddproc(pid, (char *)(description ? description : "Process"),
                    (char *)(cmdArgs ? cmdArgs : ""),
                    (TXPMF)0 /* wtf TXPMF_SAVE? */, TxExitDescList,
                    NULL, NULL);
          if (TxInForkFunc) TxInForkFunc(0);    /* after TXaddproc */
        }
      break;
    }
  return(pid);
}
#endif /* !_WIN32 */

int
TXgetInForkedChild(void)
{
  return(TXinForkedChild);
}

double
TXpgetendiotimeoutdefault()
{
  return(TxPopenEndIoTimeoutDefault);
}

int
TXpsetendiotimeoutdefault(sec)
double  sec;
/* Returns 0 on error.
 * `sec' is in seconds; can be -1 for infinite or -2 for compiled default.
 */
{
  if (sec == TX_POPEN_ENDIOTIMEOUT_DEFAULT)
    TxPopenEndIoTimeoutDefault =  TX_POPEN_ENDIOTIMEOUT_DEFAULT_REAL;
  else if (sec == TX_POPEN_ENDIOTIMEOUT_INFINITE ||
           sec >= (double)0.0)
    TxPopenEndIoTimeoutDefault = sec;
  else
    {
      putmsg(MERR + UGE, "TXpsetendiotimeoutdefault",
             "Invalid value (%g) for endiotimeout", sec);
      return(0);
    }
  return(1);
}

#ifdef _WIN32
static TXcriticalSection        *TXpopenduplexCsect = NULL;

TXbool
TXpopenSetParentStdioNonInherit(TXPMBUF *pmbuf,
                   TXFHANDLE hParentStd[TX_NUM_STDIO_HANDLES],
                   EPI_UINT32 dwParentStdOrgHandleInfo[TX_NUM_STDIO_HANDLES])
/* Sets parent's (our) stdio handles `hParentStd' to non-inheritable.
 * Sets `dwParentStdOrgHandleInfo' to GetHandleInformation() values
 * (for later restoration), or -1 on error or if already
 * non-inheritable.
 * Might be called from TXinitapp(), so generally be silent.
 * Returns true on success, false on error (continue).
 * Thread-unsafe, since stdio handles are global.
 */
{
  int           stdIdx;
  BOOL          fSuccess;
  TXbool        ret = TXbool_True;
  TRACEPIPE_VARS;

  for (stdIdx = 0; stdIdx < TX_NUM_STDIO_HANDLES; stdIdx++)
    {
      if (!TXFHANDLE_IS_VALID(hParentStd[stdIdx]))
        {
          ret = TXbool_False;
          continue;
        }

      fSuccess = GetHandleInformation(hParentStd[stdIdx],
                                      &dwParentStdOrgHandleInfo[stdIdx]);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(pmbuf, TPF_MSG_AFTER, __FUNCTION__,
                       "GetHandleInformation(%s=%p): %s returned 0x%x =%s%s%s",
                       StdioName[stdIdx], (void *)hParentStd[stdIdx],
                       (fSuccess ? Ok : Failed),
                       (int)dwParentStdOrgHandleInfo[stdIdx],
                       ((dwParentStdOrgHandleInfo[stdIdx] &
                        /* inheritance is important to us, so always
                         * explicitly print its status either way:
                         */
                         HANDLE_FLAG_INHERIT) ? " HANDLE_FLAG_INHERIT" :
                        " !HANDLE_FLAG_INHERIT"),
                       ((dwParentStdOrgHandleInfo[stdIdx] &
                         HANDLE_FLAG_PROTECT_FROM_CLOSE) ?
                        " HANDLE_FLAG_PROTECT_FROM_CLOSE" : ""),
                       ((dwParentStdOrgHandleInfo[stdIdx] &
                         ~(HANDLE_FLAG_INHERIT |
                           HANDLE_FLAG_PROTECT_FROM_CLOSE)) ?
                        " ?" : ""));
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {                                       /* pre-2000 NT stdin console*/
          dwParentStdOrgHandleInfo[stdIdx] = (DWORD)(-1);
          ret = TXbool_False;
          continue;
        }

      /* KNG 20100420 Bug 3117: SetHandleInformation() can fail with
       * `The parameter is incorrect' for unknown reasons; seems to
       * happen when the parent is a Vortex-scheduled script.  Can
       * also happen when the parent is run from command line in
       * Windows 7.  Try to avoid problem by not clearing the flag if
       * it is already off (unknown if that is ever the case).
       * Finally, do not yap if this fails; stumble on:
       */
      if (dwParentStdOrgHandleInfo[stdIdx] & HANDLE_FLAG_INHERIT)
        {                                   /* is inheritable: turn off */
          fSuccess = SetHandleInformation(hParentStd[stdIdx],
                                          HANDLE_FLAG_INHERIT, 0);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(pmbuf, TPF_MSG_AFTER, __FUNCTION__,
                 "SetHandleInformation(%s=%p, HANDLE_FLAG_INHERIT, %d): %s",
                           StdioName[stdIdx],
                           (void *)hParentStd[stdIdx],
                           0, (fSuccess ? Ok : Failed));
          TRACEPIPE_AFTER_END()
          if (!fSuccess)
            {
              /* Bug 3117 wtf cannot find a reason for `The parameter
               * is incorrect' error we get sometimes, so ignore it
               * unless tracing (especially since we may be called
               * by TXinitapp()):
               */
              if (TxTracePipe & TPF_OPEN)
                txpmbuf_putmsg(pmbuf, TPF_MSG_AFTER, __FUNCTION__,
                               "Cannot set %s handle flags: %s",
                               StdioName[stdIdx], TXstrerror(TXgeterror()));
              dwParentStdOrgHandleInfo[stdIdx] = (DWORD)(-1);
              ret = TXbool_False;
            }
        }
      else                                  /* is non-inheritable */
        /* nothing to restore later: */
        dwParentStdOrgHandleInfo[stdIdx] = (DWORD)(-1);
    }

  return(ret);
}
#endif /* _WIN32 */

int
TXpopenduplex(po, pa)
CONST TXPOPENARGS       *po;    /* (read-only) initialization args */
TXPIPEARGS              *pa;    /* (returned) args for TXpreadwrite() */
/* Like popen(), but two-way.  `po->cmd' is execed, with NULL-terminated
 * `po->argv' args and environment `po->envp'.  po->fh[0..2] must
 * be set to:
 *   TXFHANDLE_INVALID_VALUE            for /dev/null
 *   TXFHANDLE_CREATE_PIPE_VALUE        for normal pipe to or from parent
 *   other value                        for that file descriptor/handle
 * On return, pa->pipe[0..2].fh are set to file descriptors to write, read and
 * read from the subprocess, respectively (i.e. stdin/stdout/stderr of child)
 * for PIPE_HANDLE_VALUE input values.  See header for TXPDF flags.
 * `pa->pid' set to subprocess PID, `pa->proc' to process handle (Windows).
 * `pa->pipe[0..2].buf' set to 3 buffers (even if corresponding handle unset).
 * Returns 1 on success, 0 on error (with TXgeterror() set).
 * Unix: calls po->childpostfork() in child, immediately after fork(), if set.
 * Unix: calls po->childerrexit() in child, if exec error, if set (else exit).
 * NOTE: `pa' must not be copied; give same pointer to all TXp...() functions
 * Thread-safe, but check yer SIGCLD and TxInForkFunc handlers.
 * Thread-unsafe under Windows iff [Texis] Restore Stdio Inheritance true;
 * see Inheritance comments below.  (Theoretically thread-unsafe if false
 * as well, but the race then is just both threads setting no-inherit;
 * should be ok.)
 * TXpopenSetParentStdioNonInherit() and associated flag restoration;
 * mutex it?
 */
{
  static CONST char     fn[] = "TXpopenduplex";
  double                timeout;
  TRACEPIPE_VARS;
#ifdef _WIN32
  static CONST char     cf[] = "CreateFile(%s) for %s%s: 0x%x";
  static CONST char     cp[] = "CreatePipe(%s%s): %s rd=0x%x wr=0x%x";
  static CONST char     shi[] =
    "SetHandleInformation(%s%s=0x%x, HANDLE_FLAG_INHERIT, %d): %s";
  static CONST char     nul[] = "NUL";
  SECURITY_ATTRIBUTES   saAttr;
  SECURITY_DESCRIPTOR   sdDesc;
  PROCESS_INFORMATION   piProcInfo;
  STARTUPINFO           siStartupInfo;
  STARTUPINFOW          siwStartupInfo;
  BOOL                  fSuccess;
  DWORD                 CreateFlags;
  DWORD                 dwParentStdOrgHandleInfo[TX_NUM_STDIO_HANDLES];
  TXFHANDLE             hParentStd[TX_NUM_STDIO_HANDLES],
    hExecStdRd[TX_NUM_STDIO_HANDLES], hExecStdWr[TX_NUM_STDIO_HANDLES];
  HANDLE                usertoken = INVALID_HANDLE_VALUE;
  int                   i, incsect = 0, res;
  char                  *cmdline = CHARPN, *envstrlst = CHARPN;
  char                  *domain = CHARPN, *adomain = CHARPN;
  char                  *user = CHARPN, *pass, *s;
  TXTHREAD              parentthread = TXTHREAD_NULL;
  int                   closeparent = 0;
#  if _WINDOWS_WINNT >= 0x0500
#    define NUMUNI        5
  WCHAR                 *tmpuni[NUMUNI];

  for (i = 0; i < NUMUNI; i++) tmpuni[i] = WCHARPN;
#  endif /* _WINDOWS_WINNT >= 0x500 */
  TXPIPEARGS_INIT(pa);
  hParentStd[STDIN_FILENO] = TXFHANDLE_STDIN;
  hParentStd[STDOUT_FILENO] = TXFHANDLE_STDOUT;
  hParentStd[STDERR_FILENO] = TXFHANDLE_STDERR;
  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    {
      hExecStdRd[i] = hExecStdWr[i] = TXFHANDLE_INVALID_VALUE;
      dwParentStdOrgHandleInfo[i] = (DWORD)(-1);        /* i.e. unset */
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, __FUNCTION__,
                       "GetStdHandle(%s): %p", StdioName[i],
                       (void *)hParentStd[i]);
      TRACEPIPE_AFTER_END()
    }
  pass = (po->pass != CHARPN ? po->pass : "");

  /* We were setting our (`parent') stdio handles non-inherit -- but
   * only for exec bkgnd.  Bug 6028: We should do it always, to
   * prevent handle leak to child.  Child will only need our stdio if
   * explicitly passed (by TXpopenduplex() caller) via po->fh[n].  We
   * already make an inheritable dup of po->fh[n] in that case since
   * we don't know if it is inheritable; thus if po->fh[n] is our
   * stdio (which we're making *non-*inheritable here), the dup will
   * (properly) be inheritable:
   *
   * Note that with [Texis] Restore Stdio Inheritance false (default),
   * this should effectively do nothing, because we already set stdio
   * non-inherit at startup in TXinitapp(); this call is just
   * insurance (and needed to set `dwParentStdOrgHandleInfo' for
   * restore below, if ... Inheritance is true instead):
   */
  TXpopenSetParentStdioNonInherit(po->pmbuf, hParentStd,
                                  dwParentStdOrgHandleInfo);

  if (po->user != CHARPN)                       /* su to another user */
    {
      if (!TXsplitdomainuser(((po->flags & TXPDF_QUIET) ? TXPMBUF_SUPPRESS :
                          po->pmbuf), po->domain, po->user, &adomain, &user))
        goto err;
      domain = (adomain != CHARPN ? adomain : ".");
      if (!(po->flags & TXPDF_FASTLOGON))
        {
          TRACEPIPE_BEFORE_START(TPF_OPEN)
            if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
              txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, CHARPN,
                             "LogonUser(%s, %s) starting", user, domain);
          TRACEPIPE_BEFORE_END()
          i = LogonUser(user, domain, pass, LOGON32_LOGON_BATCH,
                        LOGON32_PROVIDER_DEFAULT, &usertoken);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                           "LogonUser(%s, %s): %1.3lf sec %s err %d=%s",
                           user, domain, TRACEPIPE_TIME(),
                           (i ? Ok : Failed), (int)saveErr,
                           TXgetOsErrName(saveErr, Ques));
          TRACEPIPE_AFTER_END()
          if (!i)
            {
              TX_PUSHERROR();
              if (!(po->flags & TXPDF_QUIET))
                txpmbuf_putmsg(po->pmbuf, MERR + PRM, fn,
                               "Cannot logon user `%s\\%s' for exec: %s",
                               domain, user, TXstrerror(TXgeterror()));
              TX_POPERROR();
              goto err;
            }
        }
#  if _WINDOWS_WINNT < 0x0500
      else
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + UGE, fn,
                             "Cannot CreateProcessWithLogonW(): %s",
                             TXunsupportedPlatform);
              TX_POPERROR();
            }
          goto err;
        }
#  endif /* _WINDOWS_WINNT < 0x0500 */
    }

  /* We're manipulating our std handles' flags, so serialize access: */
  if (TXcriticalSectionEnter(TXpopenduplexCsect, po->pmbuf))
    incsect = 1;
  else
    goto err;

  if (po->flags & TXPDF_BKGND)                  /* background process */
    {
      /*  DETACHED_PROCESS does not seem good through web-server.
       * CreateFlags = (DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP);
       */
      CreateFlags = CREATE_NEW_PROCESS_GROUP;
      /* Catch-22: if we let background child process inherit handles,
       * not only might it stomp one of its parent's other handles,
       * but the grandparent will wait for both the parent and the
       * child to exit, instead of just the parent, because the child
       * has a copy of parent's std handle(s) (as non-std handles,
       * because the parent saved his own std handles for restoration
       * after CreateProcess()).  But if we don't let the child inherit,
       * it cannot see its std handles that parent opened for it
       * (pipe/stdout etc.), so a child write to stdout fails.
       *
       * KNG 030307 solution: let child inherit handles so it can see
       * the std handles we opened for it.  But make parent's std
       * handles non-inheritable: since these are probably the only
       * ones the grandparent may also have, the grandparent won't wait
       * for the child since the child doesn't have a copy.  The child
       * will inherit other misc. handles from us, but the grandparent
       * shouldn't know about them (so no wait on child), it's hard for
       * child to stomp them accidentally (handle numbering vs. Unix
       * descriptors), and we prevent most other parent handles from
       * being inherited anyway (O_NOINHERIT in kdbf.c etc.).
       */
      /* Bug 6028: we moved make-parent-stdio-non-inheritable
       * to above TXpopenSetParentStdioNonInherit() call,
       * and we do it for all execs, not just bkgnd
       */
    }
  else                                          /* foreground process */
    {
      CreateFlags = 0;
      if (po->flags & TXPDF_NEWPROCESSGROUP)
        CreateFlags |= CREATE_NEW_PROCESS_GROUP;
    }
  cmdline = tx_c2dosargv(po->argv, (po->flags & TXPDF_QUOTEARGS));
  if (cmdline == CHARPN) goto err;

  if (user != CHARPN)
    {
      /* Need to give access to our handles to the other user (?): */
      if (!tx_initsec(TXPMBUFPN, &sdDesc, &saAttr)) goto err;
    }
  else
    {
      saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
      saAttr.lpSecurityDescriptor = NULL;
      /* no other `saAttr' members except `bInheritHandle' */
    }
  saAttr.bInheritHandle = TRUE;

  /* - - - - - - - - - Set up child-stdin/parent-write  - - - - - - - - - - */
  if (po->fh[STDIN_FILENO] == TXFHANDLE_INVALID_VALUE)  /* open /dev/null */
    {
      hExecStdRd[STDIN_FILENO] = CreateFile(nul,(GENERIC_READ|GENERIC_WRITE),
                                  (FILE_SHARE_READ|FILE_SHARE_WRITE),
                                  &saAttr, OPEN_ALWAYS, 0, NULL);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cf, nul,
                       StdioName[STDIN_FILENO], "ExecRead",
                       (int)hExecStdRd[STDIN_FILENO]);
      TRACEPIPE_AFTER_END()
      if (!TXFHANDLE_IS_VALID(hExecStdRd[STDIN_FILENO]))
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot open stdin nul file: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else if (po->fh[STDIN_FILENO] == TXFHANDLE_CREATE_PIPE_VALUE)
    {                                           /* parent -> child pipe */
      fSuccess = CreatePipe(&hExecStdRd[STDIN_FILENO],
                            &hExecStdWr[STDIN_FILENO], &saAttr, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cp,
                       StdioName[STDIN_FILENO], "Exec",
                     (fSuccess ? Ok : Failed), (int)hExecStdRd[STDIN_FILENO],
                       (int)hExecStdWr[STDIN_FILENO]);
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot create stdin pipe: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
      /* Make the parent's write handle to the pipe non-inheritable.  This
       * ensures the child cannot see, close or wait on the parent's handle:
       */
      fSuccess = SetHandleInformation(hExecStdWr[STDIN_FILENO],
                                      HANDLE_FLAG_INHERIT, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, shi,
                       StdioName[STDIN_FILENO], "ExecWrite",
                       (int)hExecStdWr[STDIN_FILENO], 0,
                       (fSuccess ? Ok : Failed));
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR, fn,
                             "Cannot flag stdin write handle non-inherit: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else                                          /* use given file handle */
    {
      /* We don't know if the po->fh[STDIN_FILENO] handle we're given
       * is inheritable (and in fact it should not be -- all handles
       * should be non-inheritable via TXrawOpen() or the like; Bug
       * 7570); make an inheritable copy so the child can see it (and
       * because we close hExecStdRd[STDIN_FILENO] below):
       */
      fSuccess = DuplicateHandle(GetCurrentProcess(), po->fh[STDIN_FILENO],
                                 GetCurrentProcess(),
                                 &hExecStdRd[STDIN_FILENO], 0,
                                 TRUE,          /* inherited */
                                 DUPLICATE_SAME_ACCESS);
      if (!fSuccess) goto duphnfail;
    }

  /* - - - - - - - - - Set up child-stdout/parent-read  - - - - - - - - - - */
  if (po->fh[STDOUT_FILENO] == TXFHANDLE_INVALID_VALUE) /* open /dev/null */
    {
      hExecStdWr[STDOUT_FILENO] =CreateFile(nul,(GENERIC_READ|GENERIC_WRITE),
                                  (FILE_SHARE_READ|FILE_SHARE_WRITE),
                                  &saAttr, OPEN_ALWAYS, 0, NULL);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cf, nul,
                       StdioName[STDOUT_FILENO], "ExecWrite",
                       (int)hExecStdWr[STDOUT_FILENO]);
      TRACEPIPE_AFTER_END()
      if (!TXFHANDLE_IS_VALID(hExecStdWr[STDOUT_FILENO]))
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot open stdout nul file: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else if (po->fh[STDOUT_FILENO] == TXFHANDLE_CREATE_PIPE_VALUE)
    {                                           /* child -> parent pipe */
      fSuccess = CreatePipe(&hExecStdRd[STDOUT_FILENO],
                            &hExecStdWr[STDOUT_FILENO], &saAttr, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cp,
                       StdioName[STDOUT_FILENO], "Exec",
                    (fSuccess ? Ok : Failed), (int)hExecStdRd[STDOUT_FILENO],
                       (int)hExecStdWr[STDOUT_FILENO]);
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot create stdout pipe: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
      /* Make the parent's read handle to the pipe non-inheritable.  This
       * ensures the child cannot see, close or wait on the parent's handle:
       */
      fSuccess = SetHandleInformation(hExecStdRd[STDOUT_FILENO],
                                      HANDLE_FLAG_INHERIT, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, shi,
                       StdioName[STDOUT_FILENO], "ExecRead",
                       (int)hExecStdRd[STDOUT_FILENO], 0,
                       (fSuccess ? Ok : Failed));
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR, fn,
                             "Cannot flag stdout read handle non-inherit: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else                                          /* use given file handle */
    {
      /* We don't know if the po->fh[STDOUT_FILENO] handle we're given
       * is inheritable; make an inheritable copy so the child can see it
       * (and because we close hExecStdWr[STDOUT_FILENO] below):
       */
      fSuccess = DuplicateHandle(GetCurrentProcess(), po->fh[STDOUT_FILENO],
                                 GetCurrentProcess(),
                                 &hExecStdWr[STDOUT_FILENO], 0,
                                 TRUE,          /* inherited */
                                 DUPLICATE_SAME_ACCESS);
      if (!fSuccess)
#  if TX_VERSION_MAJOR >= 5
        goto duphnfail;
#  else /* TX_VERSION_MAJOR< 5 */
        {
        duphnfail:
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "DuplicateHandle() failed: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
#  endif /* TX_VERSION_MAJOR < 5 */
    }

#  if TX_VERSION_MAJOR >= 5
  /* - - - - - - - - - Set up child-stderr/parent-read  - - - - - - - - - - */
  if (po->fh[STDERR_FILENO] == TXFHANDLE_INVALID_VALUE) /* open /dev/null */
    {
      hExecStdWr[STDERR_FILENO] =CreateFile(nul,(GENERIC_READ|GENERIC_WRITE),
                                  (FILE_SHARE_READ|FILE_SHARE_WRITE),
                                  &saAttr, OPEN_ALWAYS, 0, NULL);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cf, nul,
                       StdioName[STDERR_FILENO], "ExecWrite",
                       (int)hExecStdWr[STDERR_FILENO]);
      TRACEPIPE_AFTER_END();
      if (!TXFHANDLE_IS_VALID(hExecStdWr[STDERR_FILENO]))
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot open stderr nul file: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else if (po->fh[STDERR_FILENO] == TXFHANDLE_CREATE_PIPE_VALUE)
    {                                           /* child -> parent pipe */
      fSuccess = CreatePipe(&hExecStdRd[STDERR_FILENO],
                            &hExecStdWr[STDERR_FILENO], &saAttr, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, cp,
                       StdioName[STDERR_FILENO], "Exec",
                    (fSuccess ? Ok : Failed), (int)hExecStdRd[STDERR_FILENO],
                       (int)hExecStdWr[STDERR_FILENO]);
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "Cannot create stderr pipe: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
      /* Make the parent's read handle to the pipe non-inheritable.  This
       * ensures the child cannot see, close or wait on the parent's handle:
       */
      fSuccess = SetHandleInformation(hExecStdRd[STDERR_FILENO],
                                      HANDLE_FLAG_INHERIT, 0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn, shi,
                       StdioName[STDERR_FILENO], "ExecRead",
                       (int)hExecStdRd[STDERR_FILENO], 0,
                       (fSuccess ? Ok : Failed));
      TRACEPIPE_AFTER_END()
      if (!fSuccess)
        {
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR, fn,
                             "Cannot flag stderr read handle non-inherit: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
  else                                          /* use given file handle */
    {
      /* We don't know if the po->fh[STDERR_FILENO] handle we're given
       * is inheritable; make an inheritable copy so the child can see it
       * (and because we close hExecStdWr[STDERR_FILENO] below):
       */
      fSuccess = DuplicateHandle(GetCurrentProcess(), po->fh[STDERR_FILENO],
                                 GetCurrentProcess(),
                                 &hExecStdWr[STDERR_FILENO], 0,
                                 TRUE,          /* inherited */
                                 DUPLICATE_SAME_ACCESS);
      if (!fSuccess)
        {
        duphnfail:
          if (!(po->flags & TXPDF_QUIET))
            {
              TX_PUSHERROR();
              txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                             "DuplicateHandle() failed: %s",
                             TXstrerror(TXgeterror()));
              TX_POPERROR();
            }
          goto err;
        }
    }
#  endif /* TX_VERSION_MAJOR >= 5 */

  /* - - - - - - - - - - - - Create the child process - - - - - - - - - - - */
  ZeroMemory(&siStartupInfo, sizeof(STARTUPINFO));
  ZeroMemory(&siwStartupInfo, sizeof(STARTUPINFO));
  if (user == CHARPN || !(po->flags & TXPDF_FASTLOGON))
    {                                           /* ANSI-char function */
      siStartupInfo.cb = sizeof(STARTUPINFO);
      siStartupInfo.lpReserved = NULL;
      siStartupInfo.lpReserved2 = NULL;
      siStartupInfo.lpTitle = NULL;
      siStartupInfo.cbReserved2 = 0;
      siStartupInfo.lpDesktop = NULL;
      siStartupInfo.hStdInput = hExecStdRd[STDIN_FILENO];
      siStartupInfo.hStdOutput = hExecStdWr[STDOUT_FILENO];
#  if TX_VERSION_MAJOR >= 5
      siStartupInfo.hStdError = hExecStdWr[STDERR_FILENO];
#  else /* TX_VERSION_MAJOR < 5 */
      siStartupInfo.hStdError = hExecStdWr[STDOUT_FILENO];
#  endif /* TX_VERSION_MAJOR < 5 */
      siStartupInfo.dwFlags = (STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
      siStartupInfo.wShowWindow = SW_HIDE;
    }
  else                                          /* wide-char function */
    {
      siwStartupInfo.cb = sizeof(STARTUPINFOW);
      siwStartupInfo.lpReserved = NULL;
      siwStartupInfo.lpReserved2 = NULL;
      siwStartupInfo.lpTitle = NULL;
      siwStartupInfo.cbReserved2 = 0;
      siwStartupInfo.lpDesktop = NULL;
      siwStartupInfo.hStdInput = hExecStdRd[STDIN_FILENO];
      siwStartupInfo.hStdOutput = hExecStdWr[STDOUT_FILENO];
#  if TX_VERSION_MAJOR >= 5
      siwStartupInfo.hStdError = hExecStdWr[STDERR_FILENO];
#  else /* TX_VERSION_MAJOR < 5 */
      siwStartupInfo.hStdError = hExecStdWr[STDOUT_FILENO];
#  endif /* TX_VERSION_MAJOR < 5 */
      siwStartupInfo.dwFlags = (STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
      siwStartupInfo.wShowWindow = SW_HIDE;
    }

  envstrlst = tx_c2dosenv(po->envp != CHARPPN ? po->envp : _environ);
  if (envstrlst == CHARPN) goto err;

  /* We don't specify the first argument to CreateProcess()
   * (lpApplicationName), because it is taken literally.  Giving NULL
   * makes CreateProcess() parse its second arg (lpCommandLine) for
   * the first white-space delimited arg, and allows double-quotes to
   * escape embedded spaces in the program name, and appends .exe if
   * needed.  (If we gave a double-quoted program to both
   * lpApplicationName and lpCommandLine, it wouldn't be found; if we
   * gave an embedded-space program name with no quotes to both, the
   * child program would mis-parse its own arguments.)
   */
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    s = (user != CHARPN ? ((po->flags & TXPDF_FASTLOGON) ?
        "CreateProcessWithLogonW" : "CreateProcessAsUser") : "CreateProcess");
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      {
        if (user != CHARPN)
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, CHARPN,
                         "%s(%s\\%s, `%s') starting",
                         s, domain, user, cmdline);
        else
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, CHARPN,
                         "%s(`%s') starting", s, cmdline);
      }
  TRACEPIPE_BEFORE_END()
  if (user != CHARPN)                   /* create process as different user */
    {
      if (po->flags & TXPDF_FASTLOGON)
        {
#  if _WINDOWS_WINNT >= 0x0500
          if ((tmpuni[0] = tx_ansi2unicode(user)) == WCHARPN ||
              (domain != CHARPN &&
               (tmpuni[1] = tx_ansi2unicode(domain)) == WCHARPN) ||
              (tmpuni[2] = tx_ansi2unicode(pass)) == WCHARPN ||
              (tmpuni[3] = tx_ansi2unicode(cmdline)) == WCHARPN ||
              (po->cwd != CHARPN &&
               (tmpuni[4] = tx_ansi2unicode(po->cwd)) == WCHARPN))
            goto err;
          i = CreateProcessWithLogonW(tmpuni[0], tmpuni[1], tmpuni[2],
                     (DWORD)0,          /* logon flags */
                     NULL,              /* program to run */
                     tmpuni[3],         /* command line */
                     CreateFlags,       /* creation flags */
                     envstrlst,         /* environment */
                     tmpuni[4],         /* current working directory */
                     &siwStartupInfo,   /* STARTUPINFO pointer */
                     &piProcInfo);      /* receives PROCESS_INFORMATION */
#  else /* _WINDOWS_WINNT < 0x0500 */
          i = 0;                        /* should not happen, checked above */
#  endif /* _WINDOWS_WINNT < 0x0500 */
        }
      else
        i = CreateProcessAsUser(usertoken,
                     NULL,              /* program to run */
                     cmdline,           /* command line */
                     NULL,              /* process security attributes */
                     NULL,              /* primary thread "" */
                     TRUE,              /* inherit handles */
                     CreateFlags,       /* creation flags */
                     envstrlst,
                     po->cwd,
                     &siStartupInfo,    /* STARTUPINFO pointer */
                     &piProcInfo);      /* receives PROCESS_INFORMATION */
    }
  else                                  /* create process as same user */
    i = CreateProcess(NULL,             /* program to run */
                      cmdline,          /* command line */
                      NULL,             /* process security attributes */
                      NULL,             /* primary thread "" */
                      TRUE,             /* inherit handles */
                      CreateFlags,      /* creation flags */
                      envstrlst,        /* environment */
                      po->cwd,          /* current working directory */
                      &siStartupInfo,   /* STARTUPINFO pointer */
                      &piProcInfo);     /* receives PROCESS_INFORMATION */
  TRACEPIPE_AFTER_START(TPF_OPEN)
    if (user != CHARPN)
      txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                 "%s(%s\\%s, `%s'): %1.3lf sec %s err %d=%s handle %p pid %u",
                     s, domain, user, cmdline, TRACEPIPE_TIME(),
                     (i ? Ok : Failed), (int)saveErr,
                     TXgetOsErrName(saveErr, Ques),
                     (void *)piProcInfo.hProcess,
                     (unsigned)piProcInfo.dwProcessId);
    else
      txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                     "%s(`%s'): %1.3lf sec %s err %d=%s handle %p pid %u",
                     s, cmdline, TRACEPIPE_TIME(), (i ? Ok : Failed),
                     (int)saveErr, TXgetOsErrName(saveErr, Ques),
                     (void *)piProcInfo.hProcess,
                     (unsigned)piProcInfo.dwProcessId);
  TRACEPIPE_AFTER_END()
  if (!i)
    {
      if (!(po->flags & TXPDF_QUIET))
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(po->pmbuf, MERR + EXE, fn, "Cannot exec `%s': %s",
                         po->cmd, TXstrerror(TXgeterror()));
          TX_POPERROR();
        }
      goto err;
    }

  /* Save parent handles to return: */
  pa->pipe[STDIN_FILENO].fh = hExecStdWr[STDIN_FILENO];
  hExecStdWr[STDIN_FILENO] = TXFHANDLE_INVALID_VALUE;
  pa->pipe[STDOUT_FILENO].fh = hExecStdRd[STDOUT_FILENO];
  hExecStdRd[STDOUT_FILENO] = TXFHANDLE_INVALID_VALUE;
#  if TX_VERSION_MAJOR >= 5
  pa->pipe[STDERR_FILENO].fh = hExecStdRd[STDERR_FILENO];
  hExecStdRd[STDERR_FILENO] = TXFHANDLE_INVALID_VALUE;
#  else /* TX_VERSION_MAJOR < 5 */
  pa->pipe[STDERR_FILENO].fh = TXFHANDLE_INVALID_VALUE;
#  endif /* TX_VERSION_MAJOR < 5 */

  if (po->flags & TXPDF_BKGND)          /* background process */
    {
      CloseHandle(piProcInfo.hProcess);
      pa->proc = TXHANDLE_NULL;
    }
  else
    pa->proc = piProcInfo.hProcess;
  CloseHandle(piProcInfo.hThread);
  pa->pid = piProcInfo.dwProcessId;

  /* Set up I/O threads:
   * GetCurrentThread() is a constant: must dup it to get a handle
   * valid for the other threads:
   */
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, fn,
                     "TXgetCurrentThread(1=dup, &handle): starting");
  TRACEPIPE_BEFORE_END()
  res = TXgetCurrentThread(1, &parentthread);    /* wtf pass `po->pmbuf' */
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, fn,
                 "TXgetCurrentThread(1=dup, &handle): %1.3f sec %s handle=%p",
                   TRACEPIPE_TIME(),  (res ? Ok : Failed),
                   (void *)parentthread);
  TRACEPIPE_AFTER_END()
  if (!res) goto err;
  closeparent = 1;
  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    {
      char      threadNameBuf[256];

      pa->pipe[i].stdIdx = i;
      pa->pipe[i].parentthread = parentthread;
      pa->pipe[i].haveParentThread = 1;
      closeparent = 0;                  /* pa->pipe[] array owns it now */
      /* do not clear `parentthread': other stdio TXPIPEOBJs will copy too */
      if (!TXFHANDLE_IS_VALID(pa->pipe[i].fh)) continue;/* e.g. /dev/null*/
      /* Use manual-reset events:
       * 1) Multiple events may get signalled at one WaitForMultipleObjects()
       *    call, but we can only detect the first, and the other(s) might
       *    still get auto-reset if the mode were auto-reset (?).
       * 2) We may wait multiple times on the same event before servicing it
       *    (e.g. if TXPDF_ANYDATARD set): auto-reset events would be unset
       *    on subsequent calls and thus we could wait indefinitely.
       */
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, fn,
                         "TXCreateEvent(NULL, TRUE, 1) (%sIoBeginEvent): starting",
                         StdioName[i]);
      TRACEPIPE_BEFORE_END()
      pa->pipe[i].iobeginevent = TXCreateEvent(CHARPN, TRUE, 1);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn,
                       "TXCreateEvent(NULL, TRUE, 1) (%sIoBeginEvent): %1.3lf sec returned %p err %d=%s",
                       StdioName[i], TRACEPIPE_TIME(),
                       (void *)pa->pipe[i].iobeginevent, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      if (pa->pipe[i].iobeginevent == NULL) goto err;
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, fn,
                         "TXCreateEvent(NULL, TRUE, 1) (%sIoEndEvent): starting",
                         StdioName[i]);
      TRACEPIPE_BEFORE_END()
      pa->pipe[i].ioendevent = TXCreateEvent(CHARPN, TRUE, 1);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn,
    "TXCreateEvent(NULL, TRUE, 1) (%sIoEndEvent): %1.3lf sec returned %p err %d=%s",
                       StdioName[i], TRACEPIPE_TIME(),
                       (void *)pa->pipe[i].ioendevent, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      if (pa->pipe[i].ioendevent == NULL) goto err;
      /* pre-set the I/O end event so that TXpreadwrite() does not deadlock:*/
      if (!SetEvent(pa->pipe[i].ioendevent))
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(po->pmbuf, MERR, fn, "SetEvent() failed: %s",
                         TXstrerror(TXgeterror()));
          TX_POPERROR();
          goto err;
        }
      if ((pa->pipe[i].iobuf = (char *)malloc(TPO_IOBUFSZ)) == CHARPN)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(po->pmbuf, MERR + MAE, fn, OutOfMem);
          TX_POPERROR();
          goto err;
        }
      htsnpf(threadNameBuf, sizeof(threadNameBuf), "%sIoThread",
             StdioName[i]);
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, fn,
                         "TXcreatethread(%s): starting", threadNameBuf);
      TRACEPIPE_BEFORE_END()
	res = TXcreatethread(po->pmbuf, threadNameBuf, tx_prw_iothread,
                             pa->pipe + i,
                           /* small stack: shouldn't be doing much, and we
                            * might mem-leak the stack on TerminateThread():
			    * Bug 5139: increase to 128KB for prputmsg():
                            */
                           (TXTHREADFLAG)0, 128*1024, &pa->pipe[i].iothread);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, fn,
                       "TXcreatethread(%s): %1.3lf sec %s returned handle %p id %p err %d=%s",
                       threadNameBuf, TRACEPIPE_TIME(), (res ? Ok : Failed),
                       (void *)pa->pipe[i].iothread,
                       (void *)TXgetThreadId(pa->pipe[i].iothread),
                       (int)saveErr, TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      if (res)
        pa->pipe[i].haveIoThread = 1;
      else
        goto err;
    }

#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  static CONST char     devnull[] = "/dev/null";
  int                   i, res;
  TXFHANDLE             pfh[2*TX_NUM_STDIO_HANDLES];
  TXERRTYPE             serr;
#  define RD    0
#  define WR    1

  TXPIPEARGS_INIT(pa);
  for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
    pfh[i] = TXFHANDLE_INVALID_VALUE;

  if (po->user != CHARPN)
    {
      if (!(po->flags & TXPDF_QUIET))
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(po->pmbuf, MERR + UGE, fn,
                         "Cannot logon user `%s' for exec: %s",
                         po->user, TXunsupportedPlatform);/* WTF su if root?*/
          TX_POPERROR();
        }
      goto err;
    }

  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    if (po->fh[i] == TXFHANDLE_CREATE_PIPE_VALUE)
      {
        TRACEPIPE_BEFORE_START(TPF_OPEN)
          if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
            txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, CHARPN,
                           "pipe(%s): starting", StdioName[i]);
        TRACEPIPE_BEFORE_END()
        res = pipe(pfh + 2*i);
        TRACEPIPE_AFTER_START(TPF_OPEN)
          txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                       "pipe(%s): %1.3lf sec %s ret desc %d=R %d=W err %d=%s",
                         StdioName[i], TRACEPIPE_TIME(),
                         (i >= 0 ? Ok : Failed), pfh[2*i+RD], pfh[2*i+WR],
                         (int)saveErr, TXgetOsErrName(saveErr, Ques));
        TRACEPIPE_AFTER_END()
        if (res < 0)
          {
            if (!(po->flags & TXPDF_QUIET))
              {
                TX_PUSHERROR();
                txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                               "Cannot create pipes: %s",
                               TXstrerror(TXgeterror()));
                TX_POPERROR();
              }
            goto err;
          }
      }

  TRACEPIPE_BEFORE_START(TPF_OPEN)
    /* Log execve() here too, to avoid putmsg() in child process: */
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      {
        char    argBuf[8192], *d, *e, *LastChar, *TruncationPoint;
        size_t  i;

        d = argBuf;
        e = argBuf + sizeof(argBuf);
        LastChar = argBuf + sizeof(argBuf) - 1; /* Leaving Room for NUL */
        TruncationPoint = argBuf + sizeof(argBuf) - 5; /* Leaving Room for ...] */
        if (po->argv)
          {
            if (d < e) d += htsnpf(d, e - d, "[");
            for (i = 0; po->argv[i]; i++)
              if (d < LastChar)
                d += htsnpf(d, e - d, "%s`%s'", (i ? ", " : ""), po->argv[i]);
            if (d < LastChar)
            {
              d += htsnpf(d, e - d, "]");
            } else {
              strcpy(TruncationPoint, "...]");
            }
          }
        else
          strcpy(argBuf, "NULL");
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_BEFORE, CHARPN,
                       "fork()/execve(`%s', %s, env): starting",
                       po->cmd, argBuf);
      }
  TRACEPIPE_BEFORE_END()
  if ((po->flags & TXPDF_REAP) && TxInForkFunc != NULL) TxInForkFunc(1);

  switch (pa->pid = fork())
    {
    case -1:                                    /* failed */
      serr = TXgeterror();
      if ((po->flags & TXPDF_REAP) && TxInForkFunc != NULL) TxInForkFunc(0);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                       "fork(): %1.3lf sec ret -1 err %d",
                       TRACEPIPE_TIME(), (int)serr);
      TRACEPIPE_AFTER_END()
      if (!(po->flags & TXPDF_QUIET))
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(po->pmbuf, MERR + FRK, fn, "Cannot fork(): %s",
                         TXstrerror(TXgeterror()));
          TX_POPERROR();
        }
      goto err;
    case 0:                                     /* in child process */
      /* do not call TxInForkFunc(0): may try to reap? */
      TXpid = 0;                                /* invalidate cache */
      TXprocessStartTime = -1.0;                /* "" */
      if (po->childpostfork != TXPOPENFUNCPN)
        po->childpostfork(po->usr, pa->pid, TXHANDLE_NULL);
      if (po->flags & (TXPDF_BKGND|TXPDF_NEWPROCESSGROUP)) TXregroup();

      /* set up stdin with appropriate pipe: */
      if (po->fh[STDIN_FILENO] == TXFHANDLE_INVALID_VALUE)
        {                                       /* KNG 970430 use /dev/null */
          pfh[2*STDIN_FILENO + RD] = open(devnull, O_RDONLY, 0666);
          if (!TXFHANDLE_IS_VALID(pfh[2*STDIN_FILENO + RD])) goto openfailed;
        }
      else if (po->fh[STDIN_FILENO] != TXFHANDLE_CREATE_PIPE_VALUE)
        /* use given handle */
        pfh[2*STDIN_FILENO + RD] = po->fh[STDIN_FILENO];
      if (pfh[2*STDIN_FILENO + RD] == STDIN_FILENO)
        /* already stdin; don't close: */
        pfh[2*STDIN_FILENO + RD] = TXFHANDLE_INVALID_VALUE;
      else if (dup2(pfh[2*STDIN_FILENO + RD], STDIN_FILENO) < 0)
        goto dupfail;

      /* set up stdout with appropriate descriptor: */
      if (po->fh[STDOUT_FILENO] == TXFHANDLE_INVALID_VALUE)
        {                                       /* KNG 970430 use /dev/null */
          pfh[2*STDOUT_FILENO + WR] = open(devnull, O_WRONLY, 0666);
          if (!TXFHANDLE_IS_VALID(pfh[2*STDOUT_FILENO + WR]))
#  if TX_VERSION_MAJOR >= 5
            goto openfailed;
#  else /* TX_VERSION_MAJOR < 5 */
            {
            openfailed:
              if (!(po->flags & TXPDF_QUIET))
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                                 "open(/dev/null) failed: %s",
                                 TXstrerror(TXgeterror()));
                  TX_POPERROR();
                }
              goto childbail;
            }
#  endif /* TX_VERSION_MAJOR < 5 */
        }
      else if (po->fh[STDOUT_FILENO] != TXFHANDLE_CREATE_PIPE_VALUE)
        /* use given handle: */
        pfh[2*STDOUT_FILENO + WR] = po->fh[STDOUT_FILENO];
      if (pfh[2*STDOUT_FILENO + WR] == STDOUT_FILENO)
        /* already stdout; don't close below: */
        pfh[2*STDOUT_FILENO + WR] = TXFHANDLE_INVALID_VALUE;
      else
        {
          if (dup2(pfh[2*STDOUT_FILENO + WR], STDOUT_FILENO) < 0)
#  if TX_VERSION_MAJOR >= 5
            goto dupfail;
#  else /* TX_VERSION_MAJOR < 5 */
            {
            dupfail:
              if (!(po->flags & TXPDF_QUIET))
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                                 "dup2() failed: %s",
                                 TXstrerror(TXgeterror()));
                  TX_POPERROR();
                }
              goto childbail;
            }
#  endif /* TX_VERSION_MAJOR < 5 */
        }

#  if TX_VERSION_MAJOR >= 5
      /* set up stderr with appropriate descriptor: */
      if (po->fh[STDERR_FILENO] == TXFHANDLE_INVALID_VALUE)    /* /dev/null */
        {
          pfh[2*STDERR_FILENO + WR] = open(devnull, O_WRONLY, 0666);
          if (!TXFHANDLE_IS_VALID(pfh[2*STDERR_FILENO + WR]))
            {
            openfailed:
              if (!(po->flags & TXPDF_QUIET))
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                                 "open(/dev/null) failed: %s",
                                 TXstrerror(TXgeterror()));
                  TX_POPERROR();
                }
              goto childbail;
            }
        }
      else if (po->fh[STDERR_FILENO] != TXFHANDLE_CREATE_PIPE_VALUE)
        /* use given handle */
        pfh[2*STDERR_FILENO + WR] = po->fh[STDERR_FILENO];
      if (pfh[2*STDERR_FILENO + WR] == STDERR_FILENO)
        /* already stderr; don't close below: */
        pfh[2*STDERR_FILENO + WR] = TXFHANDLE_INVALID_VALUE;
      else
        {
          if (dup2(pfh[2*STDERR_FILENO + WR], STDERR_FILENO) < 0)
            {
            dupfail:
              if (!(po->flags & TXPDF_QUIET))
                {
                  TX_PUSHERROR();
                  txpmbuf_putmsg(po->pmbuf, MERR + FOE, fn,
                                 "dup2() failed: %s",
                                 TXstrerror(TXgeterror()));
                  TX_POPERROR();
                }
              goto childbail;
            }
        }
#  endif /* TX_VERSION_MAJOR >= 5 */

      /* close unneeded pipe ends (or given file handles) first: */
      for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
        if (TXFHANDLE_IS_VALID(pfh[i]))
          {
            (void)TXfhandleClose(pfh[i]);
            pfh[i] = TXFHANDLE_INVALID_VALUE;
          }
      TXclosedescriptors(0x6);                  /* clean up Texis, etc. */
      if (po->cwd && chdir(po->cwd) != 0) {}
      /* Now run program: */
      execve(po->cmd, po->argv, (po->envp != CHARPPN ? po->envp : _environ));
      /* if we get here, the exec() failed: */
      if (!(po->flags & TXPDF_QUIET))
        txpmbuf_putmsg(po->pmbuf, MERR + EXE, fn, "Cannot exec `%s': %s",
                       po->cmd, TXstrerror(TXgeterror()));
    childbail:
      if (po->childerrexit != TXPOPENFUNCPN)
        po->childerrexit(po->usr, pa->pid, TXHANDLE_NULL);
      _exit(TXEXIT_CANNOTEXECSUBPROCESS);
    }
  /* back in parent process: */
  if (po->flags & TXPDF_REAP)
    {
      TXaddproc(pa->pid, (po->desc != CHARPN ? po->desc : "Process"),
                po->cmd, ((po->flags & TXPDF_SAVE) ? TXPMF_SAVE : (TXPMF)0),
                (CONST char * CONST *)NULL, po->exitCallback,
                po->exitUserData);
      if (TxInForkFunc != NULL) TxInForkFunc(0);        /* after TXaddproc */
    }
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, CHARPN,
                   "fork(): %1.3lf sec pid %u err %d=%s",
                   TRACEPIPE_TIME(), (unsigned)pa->pid, (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  /* Save parent handles to return: */
  pa->pipe[STDIN_FILENO].fh = pfh[2*STDIN_FILENO + WR];
  pfh[2*STDIN_FILENO + WR] = TXFHANDLE_INVALID_VALUE;
  pa->pipe[STDOUT_FILENO].fh = pfh[2*STDOUT_FILENO + RD];
  pfh[2*STDOUT_FILENO + RD] = TXFHANDLE_INVALID_VALUE;
#  if TX_VERSION_MAJOR >= 5
  pa->pipe[STDERR_FILENO].fh = pfh[2*STDERR_FILENO + RD];
  pfh[2*STDERR_FILENO + RD] = TXFHANDLE_INVALID_VALUE;
#  else /* TX_VERSION_MAJOR < 5 */
  pa->pipe[STDERR_FILENO].fh = TXFHANDLE_INVALID_VALUE;
#  endif /* TX_VERSION_MAJOR < 5 */
#  undef RD
#  undef WR
#endif /* !_WIN32 */

  pa->pmbuf = txpmbuf_open(po->pmbuf);

  /* Open read/write buffers for all handles (even unused ones): */
  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    {
      pa->pipe[i].pmbuf = pa->pmbuf;
      if ((pa->pipe[i].buf = openhtbuf()) == HTBUFPN) goto err;
    }

  /* Set tx_prw_endiotimeout() timeout value: */
  timeout = po->endiotimeout;
  if (timeout == TX_POPEN_ENDIOTIMEOUT_DEFAULT)
    timeout = TxPopenEndIoTimeoutDefault;
  if (timeout < (double)0.0)
#ifdef _WIN32
    pa->endiomsec = (EPI_UINT32)INFINITE;
#else /* !_WIN32 */
    /* INFINITE not known; use any value since endiomsec not used: */
    pa->endiomsec = 0;
#endif /* !_WIN32 */
  else if (timeout >= (double)(((EPI_UINT32)0xffffffffUL)/(EPI_UINT32)1000))
    pa->endiomsec = ((EPI_UINT32)0xffffffffUL)/(EPI_UINT32)1000;
  else
    pa->endiomsec = (EPI_UINT32)(timeout*(double)1000.0);

  goto done;

err:
  TX_PUSHERROR();
  TXpcloseduplex(pa, 1);
  TX_POPERROR();
done:
  TX_PUSHERROR();
#ifdef _WIN32
  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)       /* close unused handles */
    {
      if (TXFHANDLE_IS_VALID(hExecStdRd[i]))
        {
          CloseHandle(hExecStdRd[i]);
          hExecStdRd[i] = TXFHANDLE_INVALID_VALUE;
        }
      if (TXFHANDLE_IS_VALID(hExecStdWr[i]))
        {
          CloseHandle(hExecStdWr[i]);
          hExecStdWr[i] = TXFHANDLE_INVALID_VALUE;
        }
      /* Restore stdio flags.  Note that this is thread-unsafe:
       * another thread might be setting/restoring flags across us.
       * This is why [Texis] Restore Stdio Inheritance defaults false:
       */
	if (TXApp->restoreStdioInheritance &&
            dwParentStdOrgHandleInfo[i] != (DWORD)(-1))
        {                                       /* valid and inheritable */
          fSuccess = SetHandleInformation(hParentStd[i], HANDLE_FLAG_INHERIT,
                                          dwParentStdOrgHandleInfo[i]);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(po->pmbuf, TPF_MSG_AFTER, __FUNCTION__, shi,
                           StdioName[i], "", (int)hParentStd[i],
                           ((dwParentStdOrgHandleInfo[i] &
                             HANDLE_FLAG_INHERIT) ? 1 : 0),
                           (fSuccess ? Ok : Failed));
          TRACEPIPE_AFTER_END()
          if (!fSuccess && !(po->flags & TXPDF_QUIET))
            txpmbuf_putmsg(po->pmbuf, MERR, fn,
                           "Cannot restore %s handle flags: %s",
                           StdioName[i], TXstrerror(TXgeterror()));
        }
    }
  /* exit csect after std handle flags restored */
  if (incsect)
    {
      TXcriticalSectionExit(TXpopenduplexCsect, po->pmbuf);
      incsect = 0;
    }
#  if _WINDOWS_WINNT >= 0x0500
  for (i = 0; i < NUMUNI; i++)
    if (tmpuni[i] != WCHARPN)
      {
        SecureZeroMemory(tmpuni[i], wcslen(tmpuni[i]));
        free(tmpuni[i]);
      }
#  endif /* _WINDOWS_WINNT >= 0x0500 */
  if (envstrlst != CHARPN) free(envstrlst);
  if (cmdline != CHARPN) free(cmdline);
  if (adomain != CHARPN) free(adomain);
  if (user != CHARPN) free(user);
  if (usertoken != INVALID_HANDLE_VALUE) CloseHandle(usertoken);
  if (closeparent)
    {
      CloseHandle(parentthread);
      parentthread = TXTHREAD_NULL;
      closeparent = 0;
    }
#else /* !_WIN32 */
  for (i = 0; i < 2*TX_NUM_STDIO_HANDLES; i++)
    if (TXFHANDLE_IS_VALID(pfh[i]))
      {
        (void)TXfhandleClose(pfh[i]);
        pfh[i] = TXFHANDLE_INVALID_VALUE;
      }
  pa->proc = TXHANDLE_NULL;
#endif /* !_WIN32 */
  TX_POPERROR();
  return(pa->pid != (PID_T)0);
#undef NUMUNI
}

static size_t myread ARGS((int fd, void *buf, size_t count, TXPMBUF *pmbuf));
static size_t
myread(fd, buf, count, pmbuf)
int     fd;
void    *buf;
size_t  count;
TXPMBUF *pmbuf;
{
  int	        n = 0;
  size_t        rd;
  TRACEPIPE_VARS;

  do
    {
#ifdef _WIN32
      unsigned int      countTry;

      /* Windows read() takes an unsigned int, which may be < size_t.
       * And it fails if the count is > INT_MAX, not UINT_MAX:
       */
      countTry = (unsigned int)TX_MIN(count, (size_t)EPI_OS_INT_MAX);
#else /* !_WIN32 */
      size_t            countTry = count;
#endif /* !_WIN32 */
      TRACEPIPE_BEFORE_START(TPF_READ | TPF_READ_DATA)
        if (TxTracePipe & TPF_BEFORE(TPF_READ))
          txpmbuf_putmsg(pmbuf, TPF_MSG_BEFORE, CHARPN,
                         "read() %wd bytes from desc %d: starting",
                         (EPI_HUGEINT)countTry, fd);
        if ((TxTracePipe & TPF_BEFORE(TPF_READ_DATA)) && countTry > 0)
          tx_hexdumpmsg(pmbuf, TPF_MSG_BEFORE + TPF_MSG_READ_DATA, CHARPN,
                        buf, countTry, 1);
      TRACEPIPE_BEFORE_END()
      TXseterror(0);
      rd = (size_t)read(fd, buf, countTry);
      TRACEPIPE_AFTER_START(TPF_READ | TPF_READ_DATA)
        if (TxTracePipe & TPF_READ)
          txpmbuf_putmsg(pmbuf, TPF_MSG_AFTER, CHARPN,
         "read() %wd bytes from desc %d: %1.3lf sec read %wd bytes err %d=%s",
                         (EPI_HUGEINT)countTry, fd, TRACEPIPE_TIME(),
                         (EPI_HUGEINT)rd, (int)saveErr,
                         TXgetOsErrName(saveErr, Ques));
        if ((TxTracePipe & TPF_READ_DATA) && rd != (size_t)(-1) && rd > 0)
          tx_hexdumpmsg(pmbuf, TPF_MSG_AFTER + TPF_MSG_READ_DATA, CHARPN,
                        buf, rd, 1);
      TRACEPIPE_AFTER_END()
    }
  while (rd == (size_t)(-1) && errno == EINTR && ++n < 25);
  return(rd);
}

static void tx_prw_procname ARGS((PID_T pid, char *tmp, size_t tmpsz,
                                  CONST char **desc, CONST char **cmd));
static void
tx_prw_procname(pid, tmp, tmpsz, desc, cmd)
PID_T           pid;            /* (in) process ID to look up */
char            *tmp;           /* (out) temp scratch buf, may be returned */
size_t          tmpsz;          /* (in) size of `tmp' */
CONST char      **desc;         /* (out) description of process `pid' */
CONST char      **cmd;          /* (out) command line of process `pid' */
/* Thread-safe.
 */
{
  int           sig, xit;

  if (!TXgetprocxit(pid, 0, &sig, &xit, (char **)desc, (char **)cmd, NULL))
    {
      *desc = "process";
      htsnpf(tmp, tmpsz, "%u", (unsigned)pid);
      *cmd = tmp;
    }
}

#ifdef _WIN32
CONST char *TXwaitRetToStr(res, delta)
DWORD   res;    /* (in) WaitFor...Object[s]() return value */
int     *delta; /* (out, optional) delta from returned string value */
{
  if (res == WAIT_TIMEOUT)
    {
      if (delta != INTPN) *delta = 0;
      return("WAIT_TIMEOUT");
    }
  if (res == WAIT_FAILED)
    {
      if (delta != INTPN) *delta = 0;
      return("WAIT_FAILED");
    }
  if (res >= WAIT_ABANDONED_0)
    {
      if (delta != INTPN) *delta = res - WAIT_ABANDONED_0;
      return("WAIT_ABANDONED_0");
    }
  if (delta != INTPN) *delta = res - WAIT_OBJECT_0;
  return("WAIT_OBJECT_0");
}

static int tx_prw_waitforiocomplete ARGS((TXPIPEARGS *pa, DWORD msec,
                                          int std));
static int
tx_prw_waitforiocomplete(pa, msec, std)
TXPIPEARGS      *pa;    /* (in/out) */
DWORD           msec;   /* (in) timeout in msec, or INFINITE */
int             std;    /* (in) >= 0: wait for that handle only (closing) */
/* Wait for any of the I/O threads to finish I/O (or die).
 * Returns index of completed handle (e.g. 0..2), -1 on timeout, -2 if none
 * available (e.g. all handles closed), -3 on error.
 * May close I/O thread handle (e.g. if thread dies).
 * Called by parent thread.
 * Thread-safe (but see I/O thread usage of `pa').
 */
{
  static CONST char     fn[] = "tx_prw_waitforiocomplete";
  int                   n, i, r;
  TXPIPEOBJ             *pobj;
  int                   stdnum[TX_NUM_STDIO_HANDLES];
  HANDLE                handles[2*TX_NUM_STDIO_HANDLES];
  const char            *handleNames[2*TX_NUM_STDIO_HANDLES];
  char                  handleNameBufs[2*TX_NUM_STDIO_HANDLES][32];
  DWORD                 res;
  CONST char            *msgfmt, *desc, *cmd;
  char                  tmp[2*TX_NUM_STDIO_HANDLES*(EPI_OS_LONG_BITS/4 + 22) + 32];
  TXERRTYPE             saverrnum;
  TRACEPIPE_VARS;

  for (n = i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    {
      pobj = pa->pipe + i;
      if (std >= 0 ? (i == std && pobj->task != TPOTASK_IDLE) :
          (TXFHANDLE_IS_VALID(pobj->fh) &&
          /* Only wait for threads that we have tasks for, i.e. do not
           * wait for stdin if we have no data for it.  Otherwise it
           * may show up as idle here, tx_prw_startiotask()
           * will ignore it since there's no data to send, we'll see it
           * idle again here, etc.; we may never see other idle threads:
           */
           (i != STDIN_FILENO ||                /* is a read thread */
            htbuf_getsendsz(pobj->buf) > (size_t)0 ||   /* have data for it */
            pobj->task != TPOTASK_IDLE)))       /* thread is in progress */
        {
          stdnum[n >> 1] = i;
          handles[n] = pobj->ioendevent;
          htsnpf(handleNameBufs[n], sizeof(handleNameBufs[n]),
                 "%sIoEndEvent", StdioName[i]);
          handleNames[n] = handleNameBufs[n];
          n++;
          handles[n] = pobj->iothread;
          htsnpf(handleNameBufs[n], sizeof(handleNameBufs[n]),
                 "%sIoThread", StdioName[i]);
          handleNames[n] = handleNameBufs[n];
          n++;
        }
    }
  /* No open handles is probably an error, as TXpreadwrite() callers
   * generally stop when all handles are closed:
   */
  if (n == 0) return(-2);                       /* no open handles */
  res = TXezWaitForMultipleObjects(pa->pmbuf,
                                   TX_TRACEPIPE_TO_TRACESKT(TxTracePipe),
                                   __FUNCTION__, handles, n, handleNames,
                            (msec == INFINITE ? -1.0 : ((double)msec)/1000.0),
                                   TXbool_False);
  switch (res)
    {
    case WAIT_ABANDONED_0 + 1:                  /* I/O thread abandoned (?) */
    case WAIT_ABANDONED_0 + 3:                  /*  ""  */
    case WAIT_ABANDONED_0 + 5:                  /*  ""  */
      /* an "abandoned" thread is not possible? */
      i = stdnum[(((res - WAIT_ABANDONED_0) - 1) >> 1)];
      msgfmt = "I/O thread for %s for %s %s abandoned";
      goto perr1;
    case WAIT_OBJECT_0 + 1:                     /* I/O thread died */
    case WAIT_OBJECT_0 + 3:                     /*  ""  */
    case WAIT_OBJECT_0 + 5:                     /*  ""  */
      i = stdnum[(((res - WAIT_OBJECT_0) - 1) >> 1)];
      pobj = pa->pipe + i;
      /* Close handle now, since we know the thread died, to avoid
       * trying (and waiting) for thread to end in TXpcloseduplex():
       */
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "CloseHandle(%sIoThread=%p) starting",
                         StdioName[i], (void *)pobj->iothread);
      TRACEPIPE_BEFORE_END()
      r = CloseHandle(pobj->iothread);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "CloseHandle(%sIoThread=%p): %1.3lf sec %s err %d=%s",
                       StdioName[i], (void *)pobj->iothread, TRACEPIPE_TIME(),
                       (r ? Ok : Failed), (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      pobj->iothread = TXTHREAD_NULL;
      pobj->haveIoThread = 0;
      msgfmt = "I/O thread for %s for %s %s died";
      goto perr1;
    case WAIT_ABANDONED_0:                      /* ioendevent abandoned (?) */
    case WAIT_ABANDONED_0 + 2:                  /*  ""  */
    case WAIT_ABANDONED_0 + 4:                  /*  ""  */
      /* an abandoned event is not possible? */
      i = stdnum[((res - WAIT_ABANDONED_0) >> 1)];
      msgfmt = "I/O end event for %s for %s %s abandoned";
    perr1:
      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
      txpmbuf_putmsg(pa->pmbuf, MERR, fn, msgfmt, StdioName[i], desc, cmd);
      break;                                    /* error */
    case WAIT_OBJECT_0:                         /* ioendevent signalled */
    case WAIT_OBJECT_0 + 2:                     /*  ""  */
    case WAIT_OBJECT_0 + 4:                     /*  ""  */
      i = stdnum[((res - WAIT_OBJECT_0) >> 1)];
      return(i);                                /* success */
    case WAIT_TIMEOUT:                          /* timeout */
      return(-1);
    case WAIT_FAILED:                           /* error */
      break;                                    /* reported */
    default:                                    /* ? */
      saverrnum = TXgeterror();
      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
      txpmbuf_putmsg(pa->pmbuf, MERR, fn,
         "WaitForMultipleObjectsEx() failed for I/O end events for %s %s: %s",
                     desc, cmd, TXstrerror(saverrnum));
      break;                                    /* error */
    }
  return(-3);                                   /* error */
}

static int tx_prw_finishiotask ARGS((TXPIPEARGS *pa, int std, int closing));
static int
tx_prw_finishiotask(pa, std, closing)
TXPIPEARGS      *pa;            /* (in/out) */
int             std;            /* (in) stdio index of completed thread */
int             closing;        /* (in) nonzero: closing; do not finish */
/* Finishes I/O task after I/O thread `std' completes its part.
 * Called by parent thread.
 * Thread-safe (but see I/O thread usage of `pa').
 * Returns 2 if read some data (ok), 1 if ok, 0 on I/O error, -1 on severe
 * error (internal).
 * Advances `pa->pipe[STDIN_FILENO].buf->sent' or `pa->pipe[1..2].buf->cnt'.
 * May close I/O handle (e.g. EOF).
 */
{
  static CONST char     fn[] = "tx_prw_finishiotask";
  TXPIPEOBJ             *pobj;
  int                   orgtask, gotdata = 0, r, ret = 1;
  char                  *buf;
  size_t                bufsz;
  char                  tmp[32];
  CONST char            *desc, *cmd;
  TRACEPIPE_VARS;

  pobj = pa->pipe + std;
  orgtask = pobj->task;
  /* Clear task, before we might potentially bail: */
  pobj->task = TPOTASK_IDLE;
  TRACEPIPE_AFTER_START(TPF_EVENT)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "Setting %sIoThread=%p idle",
                   StdioName[std], (void *)pobj->iothread);
  TRACEPIPE_AFTER_END()

  if (!closing)
    switch (orgtask)
    {
    case TPOTASK_IDLE:                          /* thread was doing nothing */
      break;
    case TPOTASK_READ:                          /* thread was reading */
      if (std == STDIN_FILENO) goto badtask;    /* we _write_ stdin */
      switch (pobj->resulterrnum)               /* was I/O successful? */
        {
        case 0:                                 /* I/O success */
          if (pobj->resultlen > 0)              /* got some data */
            {
              size_t    remain;

              gotdata = 1;
              /* I/O thread read to private `iobuf' to avoid conflict with
               * possible parent thread caller usage of `buf', so copy it:
               */
              bufsz = htbuf_getunused(pobj->buf, &buf);
              if (bufsz < pobj->resultlen)      /* should not happen */
                {
                  txpmbuf_putmsg(pa->pmbuf, MERR + MAE, fn,
                             "Internal error: Final result buffer too small");
                  goto err;
                }
              memcpy(buf, pobj->iobuf, pobj->resultlen);
              /* wtf htbuf_addused2() should be changed to take EPI_SSIZE_T */
              remain = pobj->resultlen;
              do
                {
                  int   n;

                  n = (int)TX_MIN(remain, (size_t)EPI_OS_INT_MAX);
                  htbuf_addused(pobj->buf, n);
                  remain -= (size_t)n;
                }
              while (remain > (size_t)0);
            }
          else if (pobj->resultlen == 0)        /* got EOF */
            goto closeit;
          break;
          /* According to ReadFile() docs, we get ERROR_BROKEN_PIPE on EOF,
           * instead of resultlen == 0, for anonymous pipes:
           */
        case ERROR_BROKEN_PIPE:                 /* other end closed (EOF) */
        case ERROR_NO_DATA:
          goto closeit;
        default:                                /* I/O failed */
          goto yapcloseit;
        }
      break;
    case TPOTASK_WRITE:                         /* thread was writing */
      if (std != STDIN_FILENO)                  /* we _read_ stdout/stderr */
        {
        badtask:
          tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
          txpmbuf_putmsg(pa->pmbuf, MERR, fn,
                         "Internal error: Invalid task %d for %s for %s %s",
                         orgtask, StdioName[std], desc, cmd);
          goto err;
        }
      switch (pobj->resulterrnum)               /* was I/O successful? */
        {
        case 0:                                 /* I/O success */
          /* Note: this must correspond with what iothread wrote,
           * and what TXpreadwrite() checks:
           */
          if (!htbuf_delsend(pobj->buf, pobj->resultlen)) goto err;
          break;
        default:                                /* I/O failed */
        yapcloseit:
          ret = 0;                              /* indicate I/O error */
          tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
          txpmbuf_putmsg(pa->pmbuf, MERR, fn, "Cannot %s %s for %s %s: %s",
                         (orgtask == TPOTASK_READ ? "read from" : "write to"),
                         StdioName[std], desc, cmd,
                         TXstrerror(pobj->resulterrnum));
          /* fall through: */
        closeit:
          TRACEPIPE_BEFORE_START(TPF_OPEN)
            if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
              txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                             "CloseHandle(%s=%p): starting",
                             StdioName[std], (void *)pobj->fh);
          TRACEPIPE_BEFORE_END()
          r = TXfhandleClose(pobj->fh);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                           "CloseHandle(%s=%p): %1.3lf sec %s err %d=%s",
                           StdioName[std], (void *)pobj->fh, TRACEPIPE_TIME(),
                           (r ? Ok : Failed), (int)saveErr,
                           TXgetOsErrName(saveErr, Ques));
          TRACEPIPE_AFTER_END()
          pobj->fh = TXFHANDLE_INVALID_VALUE;
          break;
        }
      break;
    case TPOTASK_EXIT:
    default:
      break;
    }
  return(ret ? (gotdata ? 2 : 1) : 0);
err:
  return(-1);                                   /* severe error */
}

static int tx_prw_startiotask ARGS((TXPIPEARGS *pa, int std));
static int
tx_prw_startiotask(pa, std)
TXPIPEARGS      *pa;    /* (in/out) */
int             std;
/* Starts idle I/O thread `std' on a read/write task, if appropriate.
 * Returns 2 if task started, 1 if ok (nothing started), 0 on error,
 * -3 if max stdout buffer size exceeded (silent),
 * -2 if max stderr buffer size exceeded (silent).
 * Called by parent thread.
 * Thread-safe (but see I/O thread usage of `pa').
 */
{
  static CONST char     fn[] = "tx_prw_startiotask";
  TXPIPEOBJ             *pobj;
  BOOL                  r;
  int                   task, ret;
  char                  *buf;
  CONST char            *desc, *cmd;
  size_t                ilen;
  char                  tmp[32];
  TRACEPIPE_VARS;

  pobj = pa->pipe + std;
  task = TPOTASK_IDLE;
  if (TXFHANDLE_IS_VALID(pobj->fh))         /* still have a pipe */
    {
      if (std == STDIN_FILENO)                  /* stdin: write if data */
        {
          if (htbuf_getsendsz(pobj->buf) > 0)   /* have data to write */
            {
              htbuf_getsend(pobj->buf, &buf, &ilen, CHARPPN, SIZE_TPN);
              if (ilen > (size_t)TX_PIPE_MAX) ilen = (size_t)TX_PIPE_MAX;
              if (ilen > (size_t)TPO_IOBUFSZ) ilen = (size_t)TPO_IOBUFSZ;
              memcpy(pobj->iobuf, buf, ilen);   /* I/O thread uses iobuf */
              task = TPOTASK_WRITE;
              pobj->iobufsztodo = ilen;
              TRACEPIPE_AFTER_START(TPF_EVENT)
                txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "About to signal %sIoThread=%p to write %ld bytes",
                               StdioName[std], (void *)pobj->iothread,
                               (long)pobj->iobufsztodo);
              TRACEPIPE_AFTER_END()
            }
        }
      else                                      /* start a read */
        {
          /* Increase the input buffer if needed: */
          ilen = pobj->buf->maxsz - (pobj->buf->cnt + 1);
          if (ilen > (size_t)BUF_INCR) ilen = (size_t)BUF_INCR;
          if (ilen <= (size_t)0)                /* no room left */
            {
              ret = std - 4;
              goto done;
            }
          if (!htbuf_inc(pobj->buf, ilen)) goto err;    /* error */
          task = TPOTASK_READ;
          /* Although iothread will read into `pbj->iobuf', we'll
           * eventually copy it into `pobj->buf', so read its size max:
           */
          pobj->iobufsztodo = htbuf_getunused(pobj->buf, &buf);
          if (pobj->iobufsztodo > (size_t)TPO_IOBUFSZ)
            pobj->iobufsztodo = (size_t)TPO_IOBUFSZ;
          TRACEPIPE_AFTER_START(TPF_EVENT)
            txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                        "About to signal %sIoThread=%p to read %ld bytes",
                           StdioName[std], (void *)pobj->iothread,
                           (long)pobj->iobufsztodo);
          TRACEPIPE_AFTER_END()
        }
    }
  else if (std == STDIN_FILENO)
    {
      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
      txpmbuf_putmsg(pa->pmbuf, MERR, fn,
                     "Cannot write %ld bytes to %s for %s %s: Handle closed",
                     (long)htbuf_getsendsz(pobj->buf), StdioName[std], desc,
                     cmd);
      goto err;
    }
  if (task == TPOTASK_IDLE)                     /* ok; nothing to do */
    {
      ret = 1;
      goto done;
    }

  /* Only reset `ioendevent' here, when we're actually going to tell
   * the thread to do something.  Otherwise we leave `ioendevent'
   * signalled, so it will return as idle in a future
   * tx_prw_waitforiocomplete() call.
   */
  TRACEPIPE_BEFORE_START(TPF_EVENT)
    if (TxTracePipe & TPF_BEFORE(TPF_EVENT))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                     "ResetEvent(%sIoEndEvent=%p): starting",
                     StdioName[std], (void *)pobj->ioendevent);
  TRACEPIPE_BEFORE_END()
  r = ResetEvent(pobj->ioendevent);
  TRACEPIPE_AFTER_START(TPF_EVENT)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "ResetEvent(%sIoEndEvent=%p): %1.3lf sec %s err %d=%s",
                   StdioName[std], (void *)pobj->ioendevent, TRACEPIPE_TIME(),
                   (r ? Ok : Failed), (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  if (!r)                                       /* SetEvent() failed */
    {
      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
      txpmbuf_putmsg(pa->pmbuf, MERR, fn,
                     "ResetEvent() for %s I/O end event for %s %s failed: %s",
                     StdioName[std], desc, cmd, TXstrerror(TXgeterror()));
      goto err;
    }

  /* Signal I/O thread to begin: */
  pobj->task = task;
  TRACEPIPE_BEFORE_START(TPF_EVENT)
    if (TxTracePipe & TPF_BEFORE(TPF_EVENT))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                     "SetEvent(%sIoBeginEvent=%p): starting",
                     StdioName[std], (void *)pobj->iobeginevent);
  TRACEPIPE_BEFORE_END()
  r = SetEvent(pobj->iobeginevent);
  TRACEPIPE_AFTER_START(TPF_EVENT)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "SetEvent(%sIoBeginEvent=%p): %1.3lf sec %s err %d=%s",
                   StdioName[std], (void *)pobj->iobeginevent,
                   TRACEPIPE_TIME(), (r ? Ok : Failed), (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  if (!r)                                       /* SetEvent() failed */
    {
      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
      txpmbuf_putmsg(pa->pmbuf, MERR, fn,
                     "SetEvent() for %s I/O begin event for %s %s failed: %s",
                     StdioName[std], desc, cmd, TXstrerror(TXgeterror()));
      pobj->task = TPOTASK_IDLE;                /* just in case? */
      goto err;
    }
  ret = 2;                                      /* ok and task started */
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

static int tx_prw_terminatethread ARGS((CONST char *fn,
                                        TXPIPEARGS *pa, int std));
static int
tx_prw_terminatethread(fn, pa, std)
CONST char      *fn;    /* (in) C function name for tracing */
TXPIPEARGS      *pa;    /* (in/out) */
int             std;    /* (in) which STDIN_... thread to close */
/* Hard-terminate I/O thread `std'.
 * Called by parent thread.
 * Returns 1 if ok, 0 on error.
 */
{
  int           r, ret = 1;
  TXPIPEOBJ     *pobj;
  char          *desc, *cmd;
  char          tmp[32];
  TRACEPIPE_VARS;

  pobj = pa->pipe + std;
  tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
  txpmbuf_putmsg(pa->pmbuf, MWARN, fn, "Terminating %s I/O thread for %s %s",
                 StdioName[std], desc, cmd);
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, CHARPN,
                     "TXterminatethread(%sIoThread=%p, 0): starting",
                     StdioName[std], (void *)pobj->iothread);
  TRACEPIPE_BEFORE_END()
  /* WTF this may mem-leak the stack, and/or corrupt things, sez MS: */
  r = TXterminatethread(pa->pmbuf, pobj->iothread, 0);
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, CHARPN,
              "TXterminatethread(%sIoThread=%p, 0): %1.3lf sec %s err %d=%s",
                   StdioName[std], (void *)pobj->iothread, TRACEPIPE_TIME(),
                   (r ? Ok : Failed), (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  if (!r)
    {
      ret = 0;
      txpmbuf_putmsg(pa->pmbuf, MERR+FCE, fn,
                     "Could not terminate %s I/O thread for %s %s: %s",
                     StdioName[std], desc, cmd, TXstrerror(TXgeterror()));
    }
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, CHARPN,
                     "CloseHandle(%sIoThread=%p): starting",
                     StdioName[std], (void *)pobj->iothread);
  TRACEPIPE_BEFORE_END()
  r = CloseHandle(pobj->iothread);
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, CHARPN,
                   "CloseHandle(%sIoThread=%p): %1.3lf sec %s err %d=%s",
                   StdioName[std], (void *)pobj->iothread, TRACEPIPE_TIME(),
                   (r ? Ok : Failed), (int)saveErr,
                   TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  pobj->iothread = TXTHREAD_NULL;
  pobj->haveIoThread = 0;
  if (!r)
    {
      ret = 0;
      txpmbuf_putmsg(pa->pmbuf, MERR + FCE, fn,
                     "Could not close %s I/O thread handle for %s %s: %s",
                     StdioName[std], desc, cmd, TXstrerror(TXgeterror()));
    }
  return(ret);
}

static int tx_prw_endiothread ARGS((TXPIPEARGS *pa, int std));
static int
tx_prw_endiothread(pa, std)
TXPIPEARGS      *pa;    /* (in/out) */
int             std;    /* (in) stdio thread to end */
/* Ends I/O thread `std', politely if possible, hard if need be.
 * NOTE: thread should be ended before closing I/O handle, to avoid
 * potential race condition where handle is closed, another thread opens a
 * new handle w/same value, and then I/O thread does I/O on that (wrong) one.
 * Called by parent thread.  Uses pa->endiomsec up to 2x.
 * Returns 1 if ok, 0 on error.
 */
{
  static CONST char     fn[] = "tx_prw_endiothread";
  TXPIPEOBJ             *pobj;
  BOOL                  r;
  DWORD                 res;
  int                   ret;
  TRACEPIPE_VARS;

  pobj = pa->pipe + std;
  if (!pobj->haveIoThread) goto ok;

  /* First check for a just-completed task, to increase the chances
   * that the thread will be idle and thus able to be soft-killed
   * instead of hard-terminated.  (E.g. a thread that was I/O blocked
   * at timeout in TXpreadwrite() may now be done, due to an
   * intervening TXpkill().):
   */
  if (tx_prw_waitforiocomplete(pa, pa->endiomsec, std) == std)
    tx_prw_finishiotask(pa, std, 1);

  /* Next try to soft-kill the thread (signal it to exit); avoids
   * potential stack mem-leak and corruption of TerminateThread().
   * All I/O threads should usually be waiting on `iobeginevent';
   * TXpreadwrite()/TXpopenduplex() usually leave them in that state:
   */
  if (pobj->iobeginevent != NULL &&             /* still have event and */
      pobj->task == TPOTASK_IDLE)               /*   thread is idle */
    {
      const char        *handleNames[1];
      char              handleNameBufs[1][32];

      TRACEPIPE_AFTER_START(TPF_EVENT)
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "About to signal %sIoThread=%p to exit",
                       StdioName[std], (void *)pobj->iothread);
      TRACEPIPE_AFTER_END()
      pobj->task = TPOTASK_EXIT;                /* tell thread to exit */
      TRACEPIPE_BEFORE_START(TPF_EVENT)
        if (TxTracePipe & TPF_BEFORE(TPF_EVENT))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "SetEvent(%sIoBeginEvent=%p): starting",
                         StdioName[std], (void *)pobj->iobeginevent);
      TRACEPIPE_BEFORE_END()
      r = SetEvent(pobj->iobeginevent);         /* wake up `iothread' */
      TRACEPIPE_AFTER_START(TPF_EVENT)
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "SetEvent(%sIoBeginEvent=%p): %1.3lf sec %s err %d=%s",
                       StdioName[std], (void *)pobj->iobeginevent,
                       TRACEPIPE_TIME(), (r ? Ok : Failed), (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      htsnpf(handleNameBufs[0], sizeof(handleNameBufs[0]), "%sIoThread",
             StdioName[std]);
      handleNames[0] = handleNameBufs[0];
      res = TXezWaitForMultipleObjects(pa->pmbuf,
                                       TX_TRACEPIPE_TO_TRACESKT(TxTracePipe),
                                       __FUNCTION__, &pobj->iothread, 1,
                                       handleNames,
                                       (pa->endiomsec == INFINITE ? -1.0 :
                                        ((double)pa->endiomsec)/1000.0),
                                       TXbool_False);
      switch (res)
        {
        case WAIT_OBJECT_0:                     /* `iothread' exited */
          TRACEPIPE_BEFORE_START(TPF_OPEN)
            if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
              txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                             "CloseHandle(%sIoThread=%p): starting",
                             StdioName[std], (void *)pobj->iothread);
          TRACEPIPE_BEFORE_END()
          r = CloseHandle(pobj->iothread);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "CloseHandle(%sIoThread=%p): %1.3lf sec %s err %d=%s",
                           StdioName[std], (void *)pobj->iothread,
                           TRACEPIPE_TIME(), (r ? Ok : Failed), (int)saveErr,
                           TXgetOsErrName(saveErr, Ques));
          TRACEPIPE_AFTER_END()
          pobj->iothread = TXTHREAD_NULL;
          pobj->haveIoThread = 0;
          break;
        }
    }

  /* Finally, hard-terminate thread if it's still around: */
  if (pobj->haveIoThread)                       /* thread still exists */
    ret = tx_prw_terminatethread(fn, pa, std);
  else
    {
    ok:
      ret = 1;
    }
  return(ret);
}
#endif /* _WIN32 */

int
TXpreadwrite(pa, timeout)
TXPIPEARGS      *pa;
int             timeout;        /* optional timeout (-1 for infinite) */
/* Writes `pa->pipe[STDIN_FILENO].buf' to `pa->pipe[STDIN_FILENO].fh',
 * and clears it if not const, and reads stdout/stderr data into
 * `pa->pipe[1..2].buf'.
 * Returns:
 *   -3  max stdout buffer size exceeded (silent)
 *   -2  max stderr buffer size exceeded (silent)
 *   -1  timeout (silent)
 *    0  error
 *    1  success
 * `pa->pipe[0..2].fh' is parent end of child stdin/stdout/stderr.
 * Any may be set to TXFHANDLE_INVALID_VALUE by this call, on error.
 * NOTE: Caller should close `pa->pipe[STDIN_FILENO].fh' (sends stdin EOF)
 * via TXpendio(pa, 0), NOT by closing `pa->pipe[STDIN_FILENO].fh' directly.
 * Will not run for more than `timeout' seconds (infinite if -1).
 */
{
  static CONST char     fn[] = "TXpreadwrite";
  time_t                deadline = (time_t)0, now = (time_t)0;
  TRACEPIPE_VARS;
  int                   ret, r, std, wroteAll = 0;
  char                  tmp[32];
  CONST char            *desc, *cmd;
  TXPIPEOBJ             *pobj;
  int                   ioerrs = 0;
#ifdef _WIN32
  DWORD                 msec;
#else /* !_WIN32 */
  fd_set                rb, wb, xb;
  int                   nfd, tries = 0, gotdata = 0;
  char                  *idata;
  size_t                ilen, nw, nr = (size_t)(-1), org;
  struct timeval        tv, *tvp;
  char                  *obuf;
  size_t                olen;
  char                  *e, prebuf[128], postbuf[128];
#endif /* !_WIN32 */

  if (timeout >= 0) deadline = (now = time(TIME_TPN)) + (time_t)timeout;

#ifdef _WIN32
  /*   It's ok to wait here for the write thread to finish, as we're
   * simultaneously servicing read threads, and the caller expects us
   * to block until the write is finished (or timeout).
   *   The I/O threads are safe to let continue after this call returns,
   * since they read/write to `pa->pipe[..].iobuf' which the parent thread
   * caller of this function does not touch.  By using the separate `iobuf',
   * we don't have to block the parent thread waiting for reads (and we
   * don't trust select()/PeekNamedPipe()).  This also lets us delay the
   * I/O thread termination on timeout until after the caller has (probably)
   * killed the process, at which time TXpcloseduplex() may be able to
   * soft-kill the threads instead of a hard-terminate which might be needed
   * here.  Downside to `iobuf' is memory and memcpy() overhead.
   */

  do
    {
      if (timeout >= 0 && now >= deadline) goto timeout;

      /* First wait for an I/O thread to finish and become idle: */
      msec = (timeout >= 0 ? ((DWORD)TX_MIN(deadline - now,
              (time_t)(((DWORD)EPI_UINT32_MAX)/(DWORD)1000)))*(DWORD)1000 :
              (DWORD)INFINITE);
      std = tx_prw_waitforiocomplete(pa, msec, -1);
      if (std == -3) goto err;                  /* error */
      if (std == -2) break;                     /* all closed: cannot cont. */
      if (std == -1)                            /* timeout */
        {
        timeout:
          ret = -1;
          goto done;
        }

      /* I/O thread `std' is idle.  Finish parent end of its previous task: */
      r = tx_prw_finishiotask(pa, std, 0);
      if (r < 0) goto err;                      /* severe error */
      /* If there was an I/O error (e.g. broken pipe), continue servicing
       * other threads.  They may have data, or at least (if they're failing
       * too due to process ending) can be put in an idle state so we
       * can soft-kill (rather than hard terminate) them later.
       * tx_prw_finishiotask() should have closed the I/O handle
       * for this thread, so that we will not wait for it in a later
       * tx_prw_waitforiocomplete():
       */
      if (r == 0)                               /* I/O error */
        {
          ioerrs |= (1 << std);                 /* note for later */
          continue;
        }

      /* Stop if requested: */
      if (r == 2 &&                             /* read data and */
          htbuf_getsendsz(pa->pipe[STDIN_FILENO].buf) <= 0 && /* all written*/
          (pa->flags & TXPDF_ANYDATARD))        /* stop when any data read */
        break;

      /* Start idle I/O thread `std' on an I/O task: */
      switch (r = tx_prw_startiotask(pa, std))
        {
        case 0:  goto err;                      /* error */
        case -3:                                /* max stdout buffer size */
        case -2: ret = r;  goto done;           /* max stderr buffer size */
        case 1:  Sleep(50);  break;             /* ok, but nothing started */
        }

      if (timeout >= 0) now = time(TIME_TPN);
    }
  while (htbuf_getsendsz(pa->pipe[STDIN_FILENO].buf) > 0 ||
         pa->pipe[STDIN_FILENO].task != TPOTASK_IDLE);

#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  do
    {
      nfd = -1;
      for (std = 0; std < TX_NUM_STDIO_HANDLES; std++)
        if (TXFHANDLE_IS_VALID(pa->pipe[std].fh) &&
            pa->pipe[std].fh > nfd)
          nfd = pa->pipe[std].fh;
      if (nfd == -1) break;                     /* no valid handles */
      nfd++;
      FD_ZERO(&rb);
      FD_ZERO(&wb);
      FD_ZERO(&xb);                             /* OOB data */
      if ((TXFHANDLE_IS_VALID(pa->pipe[STDIN_FILENO].fh)) &&
          htbuf_getsendsz(pa->pipe[STDIN_FILENO].buf) > 0)
        FD_SET(pa->pipe[STDIN_FILENO].fh, &wb);
      if (TXFHANDLE_IS_VALID(pa->pipe[STDOUT_FILENO].fh))
        FD_SET(pa->pipe[STDOUT_FILENO].fh, &rb);
      if (TXFHANDLE_IS_VALID(pa->pipe[STDERR_FILENO].fh))
        FD_SET(pa->pipe[STDERR_FILENO].fh, &rb);
      if (timeout >= 0)
        {
          if (now >= deadline) goto timeout;    /* timeout */
          memset(&tv, 0, sizeof(tv));
          tv.tv_sec = deadline - now;
          tvp = &tv;
        }
      else
        tvp = (struct timeval *)NULL;
      tries++;                                  /* select/write try count */
      TRACEPIPE_BEFORE_START(TPF_SELECT)
        *(e = prebuf) = '\0';
        e[1] = '\0';
        if (TXFHANDLE_IS_VALID(pa->pipe[STDIN_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDIN_FILENO].fh, &wb))
          e += htsnpf(e, (prebuf + sizeof(prebuf)) - e,
                      " desc %d=W", pa->pipe[STDIN_FILENO].fh);
        if (TXFHANDLE_IS_VALID(pa->pipe[STDOUT_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDOUT_FILENO].fh, &rb))
          e += htsnpf(e, (prebuf + sizeof(prebuf)) - e,
                      " desc %d=R", pa->pipe[STDOUT_FILENO].fh);
        if (TXFHANDLE_IS_VALID(pa->pipe[STDERR_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDERR_FILENO].fh, &rb))
          e += htsnpf(e, (prebuf + sizeof(prebuf)) - e,
                      " desc %d=R", pa->pipe[STDERR_FILENO].fh);
        if (timeout >= 0)
          e += htsnpf(e, (prebuf + sizeof(prebuf)) - e, ", %d sec",
                      (int)(deadline - now));
        else
          e += htsnpf(e, (prebuf + sizeof(prebuf)) - e, ", inf sec");
        if (TxTracePipe & TPF_BEFORE(TPF_SELECT))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "select(%s): starting", prebuf + 1);
      TRACEPIPE_BEFORE_END()
      r = select(nfd, &rb, &wb, &xb, tvp);
      TRACEPIPE_AFTER_START(TPF_SELECT)
        *(e = postbuf) = '\0';
        if (TXFHANDLE_IS_VALID(pa->pipe[STDIN_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDIN_FILENO].fh, &wb))
          e += htsnpf(e, (postbuf + sizeof(postbuf)) - e,
                      " desc %d=W", pa->pipe[STDIN_FILENO].fh);
        if (TXFHANDLE_IS_VALID(pa->pipe[STDOUT_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDOUT_FILENO].fh, &rb))
          e += htsnpf(e, (postbuf + sizeof(postbuf)) - e,
                      " desc %d=R", pa->pipe[STDOUT_FILENO].fh);
        if (TXFHANDLE_IS_VALID(pa->pipe[STDERR_FILENO].fh) &&
            FD_ISSET(pa->pipe[STDERR_FILENO].fh, &rb))
          e += htsnpf(e, (postbuf + sizeof(postbuf)) - e,
                      " desc %d=R", pa->pipe[STDERR_FILENO].fh);
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "select(%s): %1.3lf sec ret %d%s err %d=%s",
                       prebuf + 1, TRACEPIPE_TIME(), r, postbuf, (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      if (r < 1)                                /* error/none ready */
        {
          TXERRTYPE     errNum;

          errNum = TXgeterror();
          if (r == 0)                           /* timeout */
            {
            timeout:
              ret = -1;
              goto done;
            }
          if (errNum == EINTR && tries < 25) continue; /* try again */
          tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
          txpmbuf_putmsg(pa->pmbuf, MERR, fn,
                         "Cannot select() on stdio for %s %s: %s",
                         desc, cmd, TXstrerror(errNum));
          goto err;
        }

      /* Write data to stdin if needed: */
      pobj = pa->pipe + STDIN_FILENO;
      if (pobj->buf)
        htbuf_getsend(pobj->buf, &obuf, &olen, CHARPPN, SIZE_TPN);
      else
        {
          obuf = CHARPN;
          olen = 0;
        }
      if (TXFHANDLE_IS_VALID(pobj->fh) &&
          FD_ISSET(pobj->fh, &wb))
        {
          nw = (olen > (size_t)TX_PIPE_MAX ? (size_t)TX_PIPE_MAX : olen);
          org = nw;
          TRACEPIPE_BEFORE_START(TPF_WRITE | TPF_WRITE_DATA)
            if (TxTracePipe & TPF_BEFORE(TPF_WRITE))
              txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                             "write() %ld bytes to desc %d: starting",
                             (long)org, pobj->fh);
            if ((TxTracePipe & TPF_BEFORE(TPF_WRITE_DATA)) && org > 0)
              tx_hexdumpmsg(pa->pmbuf, TPF_MSG_BEFORE + TPF_MSG_WRITE_DATA,
                            CHARPN, (CONST byte *)obuf, org, 1);
          TRACEPIPE_BEFORE_END()
          nw = (size_t)write(pobj->fh, obuf, org);
          TRACEPIPE_AFTER_START(TPF_WRITE | TPF_WRITE_DATA)
            if (TxTracePipe & TPF_WRITE)
              txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
         "write() %ld bytes to desc %d: %1.3lf sec wrote %ld bytes err %d=%s",
                             (long)org, pobj->fh, TRACEPIPE_TIME(), (long)nw,
                             (int)saveErr, TXgetOsErrName(saveErr, Ques));
            if ((TxTracePipe & TPF_WRITE_DATA) && nw != (size_t)(-1) && nw >0)
              tx_hexdumpmsg(pa->pmbuf, TPF_MSG_AFTER + TPF_MSG_WRITE_DATA,
                            CHARPN, (CONST byte *)obuf, nw, 1);
          TRACEPIPE_AFTER_END()
          if (nw == (size_t)(-1))               /* error */
            {
              if (TXgeterror() == EINTR && tries < 25) continue;
              tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
              txpmbuf_putmsg(pa->pmbuf, MERR + FWE, fn,
                             "Cannot write to %s for %s %s: %s",
                             StdioName[STDIN_FILENO], desc, cmd,
                             TXstrerror(TXgeterror()));
              nr = (size_t)(-1);                /* propagate error below */
              std = STDIN_FILENO;
              goto closeit;
            }
          if (!htbuf_delsend(pobj->buf, nw)) goto err;
          if (nw >= olen)                        /* wrote everything */
            {
              wroteAll = 1;
              if (gotdata && (pa->flags & TXPDF_ANYDATARD)) break;
            }
        }
      else if (olen == (size_t)0)               /* nothing to write */
        wroteAll = 1;

      tries = 0;                                /* reset select/write tries */

      /* Read data from stdout/stderr if available: */
      for (std = STDOUT_FILENO; std <= STDERR_FILENO; std++)
        {
          pobj = pa->pipe + std;
          if (TXFHANDLE_IS_VALID(pobj->fh) &&
              FD_ISSET(pobj->fh, &rb) &&
              pobj->buf != HTBUFPN)
            {
              ilen = pobj->buf->maxsz - (pobj->buf->cnt + 1);
              if (ilen > (size_t)BUF_INCR) ilen = (size_t)BUF_INCR;
              if (ilen <= (size_t)0)                    /* no room left */
                {
                  ret = std - 4;
                  goto done;
                }
              if (!htbuf_inc(pobj->buf, ilen)) goto err;
              ilen = htbuf_getunused(pobj->buf, &idata);
              nr = myread(pobj->fh, idata, ilen, pa->pmbuf);
              if (nr == (size_t)(-1) || nr == (size_t)0)/* error or EOF */
                {
                  if (nr == (size_t)(-1))       /* error */
                    {
                      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
                      txpmbuf_putmsg(pa->pmbuf, MERR + FWE, fn,
                                     "Cannot read from %s for %s %s: %s",
                                     StdioName[std], desc, cmd,
                                     TXstrerror(TXgeterror()));
                    }
                closeit:
                  TRACEPIPE_BEFORE_START(TPF_OPEN)
                    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
                      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                                     "close(%s desc %d): starting",
                                     StdioName[std], pobj->fh);
                  TRACEPIPE_BEFORE_END()
                  r = TXfhandleClose(pobj->fh);
                  TRACEPIPE_AFTER_START(TPF_OPEN)
                    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                                 "close(%s desc %d): %1.3lf sec %s err %d=%s",
                                   StdioName[std], pobj->fh, TRACEPIPE_TIME(),
                                   (r ? Ok : Failed), (int)saveErr,
                                   TXgetOsErrName(saveErr, Ques));
                  TRACEPIPE_AFTER_END()
                  if (!r)
                    {
                      tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
                      txpmbuf_putmsg(pa->pmbuf, MERR + FCE, fn,
                                "Cannot close parent end of %s for %s %s: %s",
                                     StdioName[std], desc, cmd,
                                     TXstrerror(TXgeterror()));
                    }
                  pobj->fh = TXFHANDLE_INVALID_VALUE;
                  /* Do not bail at I/O error: continue w/other handles.  E.g.
                   * help ensure we get stderr msgs if stdin closes first:
                   */
                  if (nr == (size_t)(-1)) ioerrs |= (1 << std);
                  break;
                }
              htbuf_addused(pobj->buf, (int)nr);
              if (nr > 0)                       /* read something */
                {
                  gotdata = 1;
                  if (wroteAll && (pa->flags & TXPDF_ANYDATARD)) goto stop;
                }
            }
        }
      if (timeout >= 0) now = time(TIME_TPN);
    }
  while (htbuf_getsendsz(pa->pipe[STDIN_FILENO].buf) > 0);

stop:
#endif  /* !_WIN32 */

  /* Caller expects stdin buffer to be completely sent, as well as
   * cleared (i.e. not a ring buffer):
   */
  pobj = pa->pipe + STDIN_FILENO;
  if (htbuf_getsendsz(pobj->buf) > 0)           /* did not fully write stin */
    {
      if (!(ioerrs && (1 << STDIN_FILENO)))     /* only if not reported yet */
        {
          tx_prw_procname(pa->pid, tmp, sizeof(tmp), &desc, &cmd);
          txpmbuf_putmsg(pa->pmbuf, MERR + FWE, fn,
                         "Cannot completely write to %s for %s %s",
                         StdioName[STDIN_FILENO], desc, cmd);
        }
      goto err;
    }
  else                                          /* successfully wrote stdin */
    {
      if (pobj->buf->flags & HTBF_CONST)
        {
          /* htbuf_clear() would generate error */
          htbuf_setdata(pobj->buf, CHARPN, 0, 0, 0);
        }
      else if (!htbuf_clear(pobj->buf))
        goto err;
    }
  if (ioerrs) goto err;
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

int
TXpendio(pa, all)
TXPIPEARGS      *pa;    /* (in/out) */
int             all;    /* (in) nonzero: all handles, not just stdin */
/* Closes I/O to `pa'.  Sets `pa->fh' set to TXFHANDLE_INVALID_VALUE;
 * any `pa->fh' member can be that if already closed.
 * Returns 1 on success, 0 on error.  Call TXpcloseduplex() after.
 * Call with `all' 0 to just close stdin (i.e. send stdin EOF to child).
 * NOTE: do not close any handle manually, use this function to avoid race.
 * Call with `all' 1 to close all.
 */
{
  static CONST char     fn[] = "TXpendio";
  int                   std, r;
  TXPIPEOBJ             *pobj;
  TRACEPIPE_VARS;

  for (std = 0; std < TX_NUM_STDIO_HANDLES; std++)
    {
      pobj = &pa->pipe[std];
#ifdef _WIN32
      /* Make sure I/O thread is dead first, to avoid race condition
       * mentioned in tx_prw_endiothread() (new handle opened w/same val):
       */
      tx_prw_endiothread(pa, std);
#endif /* _WIN32 */
      if (TXFHANDLE_IS_VALID(pobj->fh))
        {
          TRACEPIPE_BEFORE_START(TPF_OPEN)
            if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
              txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
#ifdef _WIN32
                             "CloseHandle(%sExec%s=%p): starting",
                             StdioName[std],
                             (std == STDIN_FILENO ? "Write" : "Read"),
                             (void *)pobj->fh);
#else /* !_WIN32 */
                             "close(%s desc %ld): starting",
                             StdioName[std], (long)pobj->fh);
#endif /* !_WIN32 */
          TRACEPIPE_BEFORE_END()
          r = TXfhandleClose(pobj->fh);
          TRACEPIPE_AFTER_START(TPF_OPEN)
            txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
#ifdef _WIN32
                          "CloseHandle(%sExec%s=%p): %1.3lf sec %s err %d=%s",
                           StdioName[std],
                           (std == STDIN_FILENO ? "Write" : "Read"),
                           (void *)pobj->fh,
#else /* !_WIN32 */
                           "close(%s desc %ld): %1.3lf sec %s err %d=%s",
                           StdioName[std], (long)pobj->fh,
#endif /* !_WIN32 */
                           TRACEPIPE_TIME(), (r ? Ok : Failed), (int)saveErr,
                           TXgetOsErrName(saveErr, Ques));
          TRACEPIPE_AFTER_END()
          pobj->fh = TXFHANDLE_INVALID_VALUE;
        }
      if (!all) break;
    }
  return(1);
}

int
TXpkill(pa, yap)
TXPIPEARGS      *pa;
int             yap;
/* Terminates process early.  Call TXpcloseduplex() after.
 * Signal-safe under Unix if `yap' and TxTracePipe are 0.
 * Returns 0 on error, 1 if ok.
 */
{
  static CONST char     fn[] = "TXpkill";
  int                   res;
  TRACEPIPE_VARS;

#ifdef _WIN32
  if (pa->pid == (PID_T)0 || pa->proc == TXHANDLE_NULL) return(1);

  /* KNG 20070220  Try soft Texis-terminate first: */
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                     "TXkill(%u, -1): starting", (unsigned)pa->pid);
  TRACEPIPE_BEFORE_END()
  res = TXkill(pa->pid, -1);
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "TXkill(%u, -1): %1.3lf sec %s err %d=%s",
                   (unsigned)pa->pid, TRACEPIPE_TIME(), (res ? Ok : Failed),
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()

  if (!res)
    {
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "TerminateProcess(%p, 15): starting",
                         (void *)pa->proc);
      TRACEPIPE_BEFORE_END()
      res = TerminateProcess(pa->proc, 15);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "TerminateProcess(%p, 15): %1.3lf sec %s err %d=%s",
                       (void *)pa->proc, TRACEPIPE_TIME(),
                       (res ? Ok : Failed), (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
    }
#else /* !_WIN32 */
  if (pa->pid == (PID_T)0) return(1);
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                     "kill(%u, SIGTERM): starting", (unsigned)pa->pid);
  TRACEPIPE_BEFORE_END()
  res = (kill(pa->pid, SIGTERM) == 0);
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "kill(%u, SIGTERM): %1.3lf sec %s err %d=%s",
                   (unsigned)pa->pid, TRACEPIPE_TIME(), (res ? Ok : Failed),
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
#endif /* !_WIN32 */
  if (!res && yap)
    txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                   "Cannot terminate process %u: %s",
                   (unsigned)pa->pid, TXstrerror(TXgeterror()));
  return(res);
}

int
TXpgetexitcode(pa, flags, code, issig)
TXPIPEARGS      *pa;
int             flags;  /* 0x1: yap  0x2: wait NOHANG, silent if timeout */
int             *code;    /* (out) exit code or signal */
int             *issig;   /* (out) 1 if signal */
/* Waits for process `pa' to end and gets exit code/signal.
 * TXpendio() generally called before this.  Caller is assumed to be
 * "owner" of the process, i.e. the one that spawned it.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXpgetexitcode";
  int                   exitCodeOrSignal = 0, exitCodeIsSig = 0, ret;
  int                   inWait = 0;
  TRACEPIPE_VARS;
#ifdef _WIN32
  DWORD                 r;
  BOOL                  boolRet;
  const char            *handleNames[1];

  handleNames[0] = "subProcess";
  TXsetInProcessWait(1);
  inWait = 1;
  r = TXezWaitForMultipleObjects(((flags & 0x1) ? pa->pmbuf :
                                  TXPMBUF_SUPPRESS),
                                 TX_TRACEPIPE_TO_TRACESKT(TxTracePipe),
                                 __FUNCTION__, &pa->proc, 1, handleNames,
                                 ((flags & 0x2) ? 0.0 : -1.0),
                                 TXbool_False);
  if (r == WAIT_FAILED) goto err;               /* reported */
  if (r != WAIT_OBJECT_0)
    {
      if ((flags & 0x1) && !(r == WAIT_TIMEOUT && (flags & 0x2)))
        txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                       "WaitForMultipleObjectsEx() failed for PID %u: %s",
                       (unsigned)pa->pid, TXstrerror(TXgeterror()));
      goto err;
    }
  TRACEPIPE_BEFORE_START(TPF_OPEN)
    if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
      txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                     "GetExitCodeProcess(proc=%p) starting",
                     (void *)pa->proc);
  TRACEPIPE_BEFORE_END()
  boolRet = GetExitCodeProcess(pa->proc, &r);
  TRACEPIPE_AFTER_START(TPF_OPEN)
    txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                   "GetExitCodeProcess(proc=%p): %1.3lf sec returned %d exit code %d%s err %d=%s",
                   (void *)pa->proc, TRACEPIPE_TIME(), (int)boolRet,
                   (int)r, (r == STILL_ACTIVE ? "=STILL_ACTIVE" : ""),
                   (int)saveErr, TXgetOsErrName(saveErr, Ques));
  TRACEPIPE_AFTER_END()
  if (!boolRet)
    {
      if (flags & 0x1)
        txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                       "GetExitCodeProcess() failed for PID %u: %s",
                       (unsigned)pa->pid, TXstrerror(TXgeterror()));
      goto err;
    }
  if (r == STILL_ACTIVE)
    {
      if (flags & 0x1)
        txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn, "PID %u still active",
                       (unsigned)pa->pid);
      goto err;
    }
  exitCodeOrSignal = (int)r;
  /* be consistent with Unix version; set exit code: */
  TXsetprocxit(pa->pid, 1, 0, exitCodeOrSignal, NULL, NULL, NULL);
  goto ok;
#else /* !_WIN32 */
  int           i, tries = 0, sigNum;
  PID_T         res = -1;
  TXERRTYPE     errNum;

  TXsetInProcessWait(1);
  inWait = 1;
  do
    {
      int       waitOptions;

      TRACEPIPE_BEFORE_START(TPF_SELECT)
        if (TxTracePipe & TPF_BEFORE(TPF_SELECT))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "waitpid(pid=%u, ..., %s) starting",
                         (unsigned)pa->pid,
                         ((flags & 0x2) ? "WNOHANG" : "0=inf"));
      TRACEPIPE_BEFORE_END()
      waitOptions = 0;
      if (flags & 0x2) waitOptions |= WNOHANG;
      res = waitpid(pa->pid, &i, waitOptions);
      errNum = TXgeterror();
      TRACEPIPE_AFTER_START(TPF_SELECT)
        if (res == (PID_T)(-1) || res == 0)
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                 "waitpid(pid=%u, ..., %s): %1.3lf sec returned %d err %d=%s",
                         (unsigned)pa->pid,
                         ((flags & 0x2) ? "WNOHANG" : "0=inf"),
                         TRACEPIPE_TIME(), (int)res, (int)saveErr,
                         TXgetOsErrName(saveErr, Ques));
        else
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
           "waitpid(pid=%u, ..., %s): %1.3lf sec returned %d %s %d err %d=%s",
                         (unsigned)pa->pid,
                         ((flags & 0x2) ? "WNOHANG" : "0=inf"),
                         TRACEPIPE_TIME(), (int)res,
                         (WIFEXITED(i) ? "exit code" :
                          (WIFSIGNALED(i) ? "got signal" : "unknown status")),
                         (WIFEXITED(i) ? (int)WEXITSTATUS(i) :
                          (WIFSIGNALED(i) ? WTERMSIG(i) : i)),
                         (int)saveErr,
                         TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
    }
  while (res == (PID_T)(-1) && errNum == TXERR_EINTR && ++tries < 25);
  if (res > 0)                                  /* found a PID */
    {
      int       wifFailed = 0;

      if (WIFEXITED(i))
        exitCodeOrSignal = WEXITSTATUS(i);
      else if (WIFSIGNALED(i))
        {
          exitCodeOrSignal = WTERMSIG(i);
          exitCodeIsSig = 1;
        }
      else
        {
          if (flags & 0x1)
            txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                           "Unknown exited-or-signaled status for PID %u",
                           (unsigned)res);
          wifFailed = 1;
        }
      TXsetprocxit(res, 1, (exitCodeIsSig ? exitCodeOrSignal : 0),
                   (exitCodeIsSig ? 0 : exitCodeOrSignal), NULL, NULL, NULL);
      if (wifFailed) goto err;
      if (res == pa->pid) goto ok;              /* waitpid() found our PID */
    }
  /* call TXsetInProcessWait(0) ASAP after TXsetprocxit(),
   * and certainly before TxWaitDepth check:
   */
  if (inWait) { TXsetInProcessWait(0); inWait = 0; }

  /* waitpid() failed to find our PID.  Bug 1821: look in system
   * process list in case it is in there, e.g. monitor.c:sigcld() handler
   * already waitpid()'d for it.  But first wait for other thread
   * that might have caught our PID with waitpid(), but has not
   * called TXsetprocxit() with its exit status yet:
   */
  while (TxWaitDepth > 0) TXsleepmsec(50, 0);   /* wait for other thread */
  switch (TXgetprocxit(pa->pid, 1, &sigNum, &exitCodeOrSignal, NULL, NULL,
                       NULL))
    {
    case 2:                                     /* found, exited */
      if (sigNum)
        {
          exitCodeOrSignal = sigNum;
          exitCodeIsSig = 1;
        }
      goto ok;
    case 1:                                     /* found, still active */
      if ((flags & 0x1) && !(res == 0 && (flags & 0x2)))
        txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                       "PID %u still active", (unsigned)pa->pid);
      goto err;
    case 0:                                     /* not found */
    default:
      break;
    }

  /* waitpid() failed, and our PID is not in the already-exited list: */
  if (flags & 0x1)
    txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                   "waitpid() failed for PID %u: %s",
                   (unsigned)pa->pid, TXstrerror(errNum));
  goto err;
#endif /* !_WIN32 */
ok:
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  if (inWait) { TXsetInProcessWait(0); inWait = 0; }
  if (code) *code = exitCodeOrSignal;
  if (issig) *issig = exitCodeIsSig;
  return(ret);
}

int
TXpcloseduplex(pa, flags)
TXPIPEARGS      *pa;
int             flags;  /* 0x1: buf too  0x2: wait NOHANG  0x4: no un-SAVE */
/* NOTE: httransbuf() may call this with `flags' 0, then again with `flags' 1
 * after processing buffers.
 */
{
  static const char     fn[] = "TXpcloseduplex";
#ifdef _WIN32
  TXPIPEOBJ             *pobj;
  TRACEPIPE_VARS;
#endif /* _WIN32 */
  int                   i, ret = 1;

  TXpendio(pa, 1);
#ifdef _WIN32
  for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
    {
      pobj = pa->pipe + i;
      tx_prw_endiothread(pa, i);
      if (pobj->iobeginevent != NULL)
        {
          CloseHandle(pobj->iobeginevent);
          pobj->iobeginevent = NULL;
        }
      if (pobj->ioendevent != NULL)
        {
          CloseHandle(pobj->ioendevent);
          pobj->ioendevent = NULL;
        }
      if (pobj->iobuf != CHARPN)
        {
          free(pobj->iobuf);
          pobj->iobuf = CHARPN;
        }
    }
  /* all pipes' `parentthread's are the same shared handle: */
  if (pa->pipe[STDIN_FILENO].haveParentThread)
    {
      CloseHandle(pa->pipe[STDIN_FILENO].parentthread);
      pa->pipe[STDIN_FILENO].parentthread =
        pa->pipe[STDOUT_FILENO].parentthread =
        pa->pipe[STDERR_FILENO].parentthread = TXTHREAD_NULL;
      pa->pipe[STDIN_FILENO].haveParentThread =
        pa->pipe[STDOUT_FILENO].haveParentThread =
        pa->pipe[STDERR_FILENO].haveParentThread = 0;
    }

  if (pa->proc != TXHANDLE_NULL)
    {
      TRACEPIPE_BEFORE_START(TPF_OPEN)
        if (TxTracePipe & TPF_BEFORE(TPF_OPEN))
          txpmbuf_putmsg(pa->pmbuf, TPF_MSG_BEFORE, fn,
                         "CloseHandle(proc=%p) starting", (void *)pa->proc);
      TRACEPIPE_BEFORE_END()
      i = CloseHandle(pa->proc);
      TRACEPIPE_AFTER_START(TPF_OPEN)
        txpmbuf_putmsg(pa->pmbuf, TPF_MSG_AFTER, fn,
                       "CloseHandle(proc=%p): %1.3lf sec %s err %d=%s",
                       (void *)pa->proc, TRACEPIPE_TIME(),
                       (i ? Ok : Failed), (int)saveErr,
                       TXgetOsErrName(saveErr, Ques));
      TRACEPIPE_AFTER_END()
      pa->proc = TXHANDLE_NULL;
    }
#else /* !_WIN32 */
  if ((flags & 2) && pa->pid)
    {
      PID_T     pid;
      int       exitCode = 0, sigNum = 0;

      TXsetInProcessWait(1);
      pid = waitpid(pa->pid, &i, WNOHANG);
      if (pid > 0)                              /* found a PID */
        {
          if (WIFEXITED(i))
            exitCode = WEXITSTATUS(i);
          else if (WIFSIGNALED(i))
            sigNum = WTERMSIG(i);
          else
            {
              txpmbuf_putmsg(pa->pmbuf, MERR + EXE, fn,
                             "Unknown exited-or-signaled status for PID %u",
                             (unsigned)pid);
              ret = 0;                          /* error return */
            }
          TXsetprocxit(pid, 1, sigNum, exitCode, NULL, NULL, NULL);
        }
      TXsetInProcessWait(0);
    }
#endif /* !_WIN32 */
  if (!(flags & 0x4) && pa->pid)                /* undo TXPDF_SAVE */
    TXgetprocxit(pa->pid, 1, NULL, NULL, NULL, NULL, NULL);
  pa->pid = (PID_T)0;
  if (flags & 1)
    {
      for (i = 0; i < TX_NUM_STDIO_HANDLES; i++)
        pa->pipe[i].buf = closehtbuf(pa->pipe[i].buf);
    }
  pa->pmbuf = txpmbuf_close(pa->pmbuf);         /* close putmsg buffer last */
  return(ret);
}

int
TXreportProcessExit(TXPMBUF *pmbuf, const char *fn, const char *procDesc,
                    const char *cmd, PID_T pid, int exitCodeOrSig, int isSig,
                    const char *exitDesc)
/* Reports process exit.
 * `procDesc' is optional process description, e.g. `Vortex CGI process'.
 * `cmd' is optional command line.
 * `exitDesc' is optional exit code description, e.g. `Terminated by signal'.
 * Returns 0 on error.
 */
{
  const char    *pfx, *sfx;

  /* If no exit code description, and this is a Texis program, look up desc:*/
  if (!exitDesc && !isSig && cmd && TXisTexisProg(cmd))
    {
      const char * const        *desc;

      for (desc = TxExitDescList;
           *desc != CHARPN && (desc - TxExitDescList) != exitCodeOrSig;
           desc++)
        ;
      if (*desc != CHARPN && **desc != '\0') exitDesc = *desc;
    }

  /* If still no exit code description, look up in system statuses: */
  if (!exitDesc && !isSig)
    exitDesc = TXgetCodeDescription(TXsystemStatuses, exitCodeOrSig, NULL);

  if (exitDesc)
    {
      pfx = " (";
      sfx = "?)";
    }
  else
    pfx = sfx = exitDesc = "";
  if (!procDesc) procDesc = "Process";

  txpmbuf_putmsg(pmbuf, (exitCodeOrSig != 0 ? MWARN : MINFO) + EXE, fn,
                 "%s%s%s%s PID %u %s %d%s%s%s", procDesc,
                 (cmd ? " `" : ""), (cmd ? cmd : ""), (cmd ? "'" : ""),
                 (unsigned)pid,
                 (isSig ? "received signal" : "returned exit code"),
                 (int)exitCodeOrSig, pfx, exitDesc, sfx);
  return(1);
}

/* ------------------------------------------------------------------------ */

int
TXprocessInit(TXPMBUF *pmbuf)
/* Called once at process init, immediately (time-sensitive),
 * before any threads.  `pmbuf' not cloned.
 * Returns 0 on error.
 */
{
  static TXATOMINT      didProcessInit = 0;
  int                   ret = 1;

  if (TX_ATOMIC_COMPARE_AND_SWAP(&didProcessInit, 0, 1) != 0)
    return(1);                                  /* already called */

#ifdef _WIN32
  /* Dodge link issue for tfchksum.exe: */
  {
    extern TXAPP          **TXinvalidParameterHandlerTXApp;
    TXinvalidParameterHandlerTXApp = &TXApp;
    TXinvalidParameterHandlerGetBacktraceFunc = TXgetBacktrace;
  }
#endif /* _WIN32 */

  if (!TXsetProcessStartTime()) ret = 0;
  /* wtf cannot usefully pass `pmbuf' to this: */
  if (!TXinitChildProcessManagement()) ret = 0;
  if (!TXinitAbendSystem(pmbuf)) ret = 0;
  if (!TXinitThreads(pmbuf)) ret = 0;
#ifdef _WIN32
  if (!TXpopenduplexCsect &&
      !(TXpopenduplexCsect =
        TXcriticalSectionOpen(TXcriticalSectionType_Normal, pmbuf)))
    ret = 0;
#else /* !_WIN32 */
#  if !defined(EPI_HAVE_GETPWUID_R_PASSWD) &&!defined(EPI_HAVE_GETPWUID_R_INT)
  if (!TXgetpwCsect &&
      !(TXgetpwCsect =
        TXcriticalSectionOpen(TXcriticalSectionType_Normal, pmbuf)))
    ret = 0;
#  endif
#endif /* !_WIN32 */
/* WTF
  if (!TXsaslProcessInit(pmbuf)) ret = 0;
*/

#if defined(EPI_VORTEX_WATCHPATH) && !defined(EPI_ENABLE_VORTEX_WATCHPATH) && (defined(TX_WATCHPATH_SUPPORTED) || defined(TX_WATCHPATH_SUBTREE_SUPPORTED))
  /* Remove `watchpath' and `watchpathsubtree' if not enabled: */
  if (!getenv("EPI_ENABLE_VORTEX_WATCHPATH"))
    {
      char      **sp, **dp;

      for (sp = dp = TXFeatures; *sp; sp++)
        if (strcmpi(*sp, "watchpath") != 0 &&
            strcmpi(*sp, "watchpathsubtree") != 0)
          *(dp++) = *sp;
      *dp = NULL;
    };
#endif

  return(ret);
}

/****************************************************************************/

int
TXgetmaxdescriptors()
/* Returns max number of open file descriptors possible, or -1 if unknown.
 * Thread-safe.
 */
{
#ifdef _WIN32
  return(-1);
#else /* !_WIN32 */
  static TXCINIT_DECL(got_dtablesz);
  static int    dtablesz = -1;
  EPI_HUGEINT   soft, hard;

  TXentercinit(got_dtablesz);
  dtablesz = getdtablesize();
  TXexitcinit(got_dtablesz, TXCINIT_OK);
#  ifdef RLIMIT_NOFILE
  if (TXgetrlimit(TXPMBUFPN, RLIMIT_NOFILE, &soft, &hard) > 0 &&
      hard < (EPI_HUGEINT)dtablesz)
    return((int)hard);
#  endif /* RLIMIT_NOFILE */
  return(dtablesz);
#endif /* !_WIN32 */
}

int
TXgetopendescriptors()
/* Returns number of open file descriptors, or -1 if unknown.
 * Thread-safe.
 */
{
#ifdef _WIN32
  return(-1);
#else /* !_WIN32 */
  int   fd, cnt, max;

  max = TXgetmaxdescriptors();
  if (max < 0) max = 8193;                      /* WTF guess */
  for (fd = cnt = 0; fd < max; fd++)
    if (fcntl(fd, F_GETFL) != -1) cnt++;
  return(cnt);
#endif /* !_WIN32 */
}

/****************************************************************************/

void
TXtxtimeinfoToStructTm(const TXTIMEINFO *timeinfo, struct tm *tm)
/* Copies `*timeinfo' to `*tm'.
 * NOTE: Not all fields may be copy-able, depending on platform.
 * Thread-safe.  Signal-safe.
 */
{
	memset(tm, 0, sizeof(struct tm));	/* in case we miss fields */
	tm->tm_sec = timeinfo->second;
	tm->tm_min = timeinfo->minute;
	tm->tm_hour = timeinfo->hour;
	tm->tm_mday = timeinfo->dayOfMonth;
	tm->tm_mon = timeinfo->month - 1;
	tm->tm_year = (int)(timeinfo->year - 1900);
	tm->tm_wday = timeinfo->dayOfWeek - 1;
	tm->tm_yday = timeinfo->dayOfYear - 1;
	tm->tm_isdst = timeinfo->isDst;
#ifdef EPI_HAVE_TM_GMTOFF
	tm->tm_gmtoff = timeinfo->gmtOffset;
#endif /* EPI_HAVE_TM_GMT_OFF */
}

void
TXstructTmToTxtimeinfo(const struct tm *tm, TXTIMEINFO *timeinfo)
/* Copies `*tm' to `*timeinfo'.
 * NOTE: Caller may have to set `timeinfo->gmtOffset' depending on platform.
 * Thread-safe.  Signal-safe.
 */
{
	timeinfo->year = tm->tm_year + 1900;
	timeinfo->month = tm->tm_mon + 1;
	timeinfo->dayOfMonth = tm->tm_mday;
	timeinfo->hour = tm->tm_hour;
	timeinfo->minute = tm->tm_min;
	timeinfo->second = tm->tm_sec;
	timeinfo->dayOfWeek = tm->tm_wday + 1;
	timeinfo->dayOfYear = tm->tm_yday + 1;
	timeinfo->isDst = tm->tm_isdst;
#ifdef EPI_HAVE_TM_GMTOFF
	timeinfo->gmtOffset = tm->tm_gmtoff;
#else /* !EPI_HAVE_TM_GMT_OFF */
	timeinfo->gmtOffset = 0;		/* wtf -1 for unknown? */
#endif /* !EPI_HAVE_TM_GMT_OFF */
	timeinfo->isDstStdOverlap = -1;		/* i.e. unknown */
}

size_t
TXosStrftime(char *s, size_t max, const char *format, const TXTIMEINFO *timeinfo)
{
	struct tm	tm;
	size_t		ret;
#ifdef EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES
        static const byte isValidCode[256] =
          EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES;
        const char      *src;
        size_t          numBadCodes = 0, allocSz;
        char            *altFmt = NULL, *dest, altBuf[256];
#endif /* EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES */

	TXtxtimeinfoToStructTm(timeinfo, &tm);
	/* Sanity check the values: MSVS 2012 strftime() will throw an
	 * invalid-parameter exception on e.g. negative tm_sec (even
	 * though its gmtime() may have generated that very struct tm
	 * from e.g. gmtime(-1)):
         */
#ifdef _WIN32
	if (tm.tm_sec < 0 || tm.tm_sec > 59 ||
	    tm.tm_min < 0 || tm.tm_min > 59 ||
	    tm.tm_hour < 0 || tm.tm_hour > 23 ||
	    tm.tm_mday < 1 || tm.tm_mday > 31 ||
	    tm.tm_mon < 0 || tm.tm_mon > 11 ||
	    tm.tm_wday < 0 || tm.tm_wday > 6 ||
	    tm.tm_yday < 0 || tm.tm_yday > 365)
          goto err;
#endif /* _WIN32 */

        /* Also check codes: invalid codes generate invalid-parameter
         * exception too, so escape them to get typical Unix behavior
         * of printing as-as:
	 */
#ifdef EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES
        for (src = format; *src; src++)
          {
            if (*src != '%') continue;
            src++;
            if (!*src)
              {
                numBadCodes++;
                break;
              }
            if (!isValidCode[(byte)*src] &&
                !(*src == '#' && src[1] && isValidCode[(byte)(src[1])]))
              numBadCodes++;
          }
        if (numBadCodes > 0)                    /* escape bad codes */
          {
            allocSz = (src - format) + numBadCodes + 1;
            if (allocSz < sizeof(altBuf))
              altFmt = altBuf;
            else
              {
                altFmt = (char *)TXmalloc(TXPMBUF_SUPPRESS, __FUNCTION__,
                                          allocSz);
                if (!altFmt) goto err;
              }
            dest = altFmt;
            for (src = format; *src; src++)
              {
                if (*src != '%')
                  {
                    *(dest++) = *src;
                    continue;
                  }
                src++;
                if (!*src)
                  {
                    *(dest++) = '%';
                    *(dest++) = '%';
                    break;
                  }
                if (!isValidCode[(byte)*src] &&
                    !(*src == '#' && src[1] && isValidCode[(byte)(src[1])]))
                  *(dest++) = '%';
                *(dest++) = '%';
                *(dest++) = *src;
              }
            *dest = '\0';
            format = altFmt;
          }
#  endif /* EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES */
	ret = strftime(s, max, format, &tm);
        goto finally;

#if defined(_WIN32) || defined(EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES)
err:
        TXstrncpy(s, "(err)", max);
        ret = (max >= 6 ? 5 : 0);
#endif
finally:
#ifdef EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES
        if (altFmt && altFmt != altBuf)
          {
            altFmt = TXfree(altFmt);
            format = NULL;
          }
#endif /* EPI_IS_VALID_STRFTIME_CODE_ARRAY_VALUES */
	return(ret);
}

size_t
TXstrftime(s, max, format, timeinfo)
char			*s;		/* destination buffer */
size_t			max;		/* its size */
CONST char		*format;	/* format string to use */
CONST TXTIMEINFO	*timeinfo;	/* the time to print */
/* Partial implementation of strftime().  For use by signal handlers etc.
 * NOTE: does not support some codes/modifiers; see below.
 * Signal-safe.  Thread-safe.
 */
{
	static CONST char * CONST	wkday[] =	/* WTF locale */
	{
		"Sunday",
		"Monday",
		"Tuesday",
		"Wednesday",
		"Thursday",
		"Friday",
		"Saturday",
	};
	static CONST char * CONST	month[] =	/* WTF locale */
	{
		"January",
		"February",
		"March",
		"April",
		"May",
		"June",
		"July",
		"August",
		"September",
		"October",
		"November",
		"December",
	};
	char		*d, *e, *d2;
	CONST char	*w;
	size_t		sz;
	time_t		t, t2;
#if EPI_OS_TIME_T_BITS == 32
	EPI_UINT32	prNum;
#elif EPI_OS_TIME_T_BITS == 64
	EPI_UINT64	prNum;
#else
#  error Need unsigned prNum same size as time_t
#endif
	int		alt, isNeg, padWidth;
	char		padChar, tmp[EPI_OS_TIME_T_BITS/3 + 4];

	if (s == CHARPN || format == CHARPN || !timeinfo)
	{
		if (s != CHARPN && max > 0) *s = '\0';
		return(0);
	}
	d = s;
	e = d + max;
	for ( ; *format != '\0'; format++)
	{
		if (*format != '%')
		{
			if (d < e) *(d++) = *format;
			else goto shortbuf;
			continue;
		}
		alt = padWidth = 0;
		padChar = '\0';
	again:
		switch (*++format)
		{
			case '#':			/* alt. format */
				alt = 1;
				goto again;
			case 'a':			/* abbr. weekday */
				w = wkday[(unsigned)(timeinfo->dayOfWeek-1)%7];
				goto doshort;
			case 'A':			/* full weekday */
				w = wkday[(unsigned)(timeinfo->dayOfWeek-1)%7];
				goto dostr;
			case 'b':			/* abbr. month */
			case 'h':			/* "" */
				w = month[(unsigned)(timeinfo->month-1) % 12];
			doshort:
				if (d + 2 >= e) goto shortbuf;
				*(d++) = *(w++);
				*(d++) = *(w++);
				*(d++) = *(w++);
				break;
			case 'B':			/* full month */
				w = month[(unsigned)(timeinfo->month-1) % 12];
			dostr:
				while (*w != '\0')
				{
					if (d >= e) goto shortbuf;
					*(d++) = *(w++);
				}
				break;
			case 'c':			/* preferred format */
				w = "%a %b %e %H:%M:%S %Y";  /* WTF locale */
			dosubfmt:	/* WTF WTF WTF */
				sz = TXstrftime(d, (size_t)(e-d), w, timeinfo);
				if (sz == 0) goto shortbuf;
				d += sz;
				if (d > e) goto shortbuf;
				break;
			case 'C':			/* century 01 */
				t = timeinfo->year / (time_t)100;
				t = TX_ABS(t);
				goto twodigit;
			case 'd':			/* day of month 01 */
				t = (time_t)timeinfo->dayOfMonth;
				if (alt) goto nDigit;	/* no leading sp/0 */
			twodigit:	/* WTF WTF WTF */
				padWidth = 2;
				padChar = '0';
				goto nDigit;
			case 'D':			/* %m/%d/%y */
				w = "%m/%d/%y";
				goto dosubfmt;
			case 'e':			/* day of month 1 */
				t = (time_t)timeinfo->dayOfMonth;
			twodigitsp:	/* WTF WTF WTF */
				padWidth = 2;
				padChar = ' ';
				goto nDigit;
			case 'E':			/* modifier */
				goto again;		/* WTF */
			/* case 'G': WTF ISO 8601 year */
			/* case 'g': WTF "" */
			case 'H':			/* 24-hour hour 01 */
				t = (time_t)timeinfo->hour;
				goto twodigit;
			case 'I':			/* 12-hour hour 01 */
				if (timeinfo->hour >= 0)
				{
					t = ((time_t)timeinfo->hour % 12);
					if (t == 0) t = 12;
				}
				else
					t = 12 - (-timeinfo->hour % 12);
				goto twodigit;
			case 'j':			/* day of year 001 */
				t = (time_t)timeinfo->dayOfYear;
				padWidth = 3;
				padChar = '0';
				goto nDigit;
			case 'k':			/* 24-hour hour 1 */
				t = (time_t)timeinfo->hour;
				goto twodigitsp;
			case 'l':			/* 12-hour hour 1 */
				if (timeinfo->hour >= 0)
				{
					t = ((time_t)timeinfo->hour % 12);
					if (t == 0) t = 12;
				}
				else
					t = 12 - (-timeinfo->hour % 12);
				goto twodigitsp;
			case 'm':			/* month number 01 */
				t = (time_t)timeinfo->month;
				goto twodigit;
			case 'M':			/* minute 00 */
				t = (time_t)timeinfo->minute;
				goto twodigit;
			case 'n':			/* newline */
				if (d < e) *(d++) = '\n';
				else goto shortbuf;
				break;
			case 'O':			/* modifier */
				goto again;		/* WTF */
			case 'p':			/* AM/PM */
				if (d + 1 >= e) goto shortbuf;
				if (timeinfo->hour >= 0)
					*(d++) = (timeinfo->hour % 24 >= 12 ?
						  'P' : 'A');
				else
					*(d++) = (24 - (-timeinfo->hour % 24)
						  >= 12 ? 'P' : 'A');
				*(d++) = 'M';
				break;
			case 'P':			/* am/pm */
				if (d + 1 >= e) goto shortbuf;
				if (timeinfo->hour >= 0)
					*(d++) = (timeinfo->hour % 24 >= 12 ?
						  'p' : 'a');
				else
					*(d++) = (24 - (-timeinfo->hour % 24)
						  >= 12 ? 'p' : 'a');
				*(d++) = 'm';
				break;
			case 'r':			/* time in am/pm */
				w = "%I:%M:%S %p";	/* WTF locale? */
				goto dosubfmt;
			case 'R':			/* time in hour:min */
				w = "%H:%M";
				goto dosubfmt;
			case 's':		/* time_t seconds */
				if (!TXtxtimeinfoToTime_t(timeinfo, &t))
				{		/* could not get time_t */
					if (d < e) *(d++) = TX_INVALID_CHAR;
					else goto shortbuf;
					break;
				}
				goto nDigit;
			case 'S':			/* second 00 */
				t = (time_t)timeinfo->second;
				goto twodigit;
			case 't':			/* tab */
				if (d < e) *(d++) = '\t';
				else goto shortbuf;
				break;
			case 'T':			/* 24-hour time */
				w = "%H:%M:%S";
				goto dosubfmt;
			case 'u':			/* day of week 1 */
				t = (((time_t)(timeinfo->dayOfWeek-1)%7)+1);
				goto nDigit;
				break;
			case 'U':			/* week number 00 */
				/* Week 01 starts with first Sunday: */
				t = (time_t)(((timeinfo->dayOfYear -
					       timeinfo->dayOfWeek) + 1) / 7);
				if (timeinfo->dayOfYear >= timeinfo->dayOfWeek)
					t++;
				goto twodigit;
			/* case 'V': WTF week number */
			case 'w':			/* day of week # 0 */
				t = ((time_t)(timeinfo->dayOfWeek - 1) % 7);
				goto nDigit;
			case 'W':			/* week number 00 */
				/* Week 01 starts with first Monday: */
				t2 = (time_t)(timeinfo->dayOfWeek - 1);
				t2 = (t2 == 0 ? 6 : t2 - 1);
				t = (time_t)((((timeinfo->dayOfYear - 1) - t2)
					      + 1) / 7);
				if (timeinfo->dayOfYear - 1 >= t2) t++;
				goto twodigit;
			case 'x':			/* preferred date */
				w = "%m/%d/%y";		/* WTF locale */
				goto dosubfmt;
			case 'X':			/* preferred time */
				w = "%H:%M:%S";		/* WTF locale */
				goto dosubfmt;
			case 'y':			/* 2-digit year 00 */
				t = ((time_t)TX_ABS(timeinfo->year) % 100);
				goto twodigit;
			case 'Y':			/* year w/century */
				t = (time_t)timeinfo->year;
			nDigit:
				d2 = tmp + sizeof(tmp);
				isNeg = (t < (time_t)0);
				/* Use an unsigned to fit -EPI_OS_TIME_T_MAX,
                                 * but same size as time_t to trim overflow:
                                 */
				prNum = (isNeg ? -t : t);
				do
				{
					*--d2 = '0' + (char)(prNum % 10);
					prNum /= 10;
				}
				while (prNum > 0);
				while ((tmp + sizeof(tmp)) - d2 < padWidth)
					*--d2 = padChar;
				if (isNeg) *--d2 = '-';
				sz = (tmp + sizeof(tmp)) - d2;
				if (d + sz > e) goto shortbuf;
				while (d2 < tmp + sizeof(tmp))
					*(d++) = *(d2++);
				break;
			case 'z':			/* GMT offset HHMM */
				if (d + 4 >= e) goto shortbuf;
				t = TX_ABS(timeinfo->gmtOffset);
				if (timeinfo->gmtOffset < 0)
					*(d++) = '-';
				else
					*(d++) = '+';
				t %= 86400;
				t /= 60;
				*(d++) = '0' + (char)(t / 600);
				t %= 600;
				*(d++) = '0' + (char)(t / 60);
				t %= 60;
				goto twodigit;
			case 'Z':			/* time zone */
				if (timeinfo->gmtOffset == 0)
					w = "GMT";
				else
					w = TxTzName[timeinfo->isDst>0];/*WTF*/
				goto dostr;
			case '+':			/* date(1) fmt */
				w = "%a %b %e %H:%M:%S %Z %Y";
				goto dosubfmt;
			case '%':			/* a percent sign */
				if (d < e) *(d++) = '%';
				else goto shortbuf;
				break;
			case '\0':
				if (d < e) *(d++) = '%';
				else goto shortbuf;
				format--;		/* anti-increment */
				break;
			default:
				if (d < e) *(d++) = '%';
				if (d < e ) *(d++) = *format;
				else goto shortbuf;
				break;
		}
	}
	if (d < e) *d = '\0';
	else goto shortbuf;
	return((size_t)(d - s));
shortbuf:
	if (d >= e) d = e - 1;
	if (d >= s) *d = '\0';
	return(0);
}

#define MINUTE		60
#define HOUR		(60*MINUTE)
#define DAY		(24*HOUR)
/* SDIV: We need to divide A by B integrally, and back off 1 if A is negative.
 * E.g. -3 / 5 should be -1, not 0 as usual; -7 / 5 should be -2, not -1.
 * A % B (where A < 0) may vary by platform: usually <= 0, but could be >= 0.
 * If >= 0, then A / B should already be one less than usual, else subtract 1:
 */
#define SDIV(a, b)	((a) / (b) - ((a) % (b) < 0))
/* Number of leap days through and including year `y': */
#define LEAPDAYS(y)	(SDIV(y, 4) - SDIV(y, 100) + SDIV(y, 400))
/* ISLEAPYEAR(y) True if year `y' is a leap year: */
#define ISLEAPYEAR(y)	(((y) & 3) == 0 && ((y) % 100 != 0 || (y) % 400 == 0))
#define DAYS_PER_YEAR(y)	(ISLEAPYEAR(y) ? 366 : 365)

static CONST int	daysnorm[12] =
{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static CONST int	daysleap[12] =
{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

int
TXosTime_tToGmtTxtimeinfo(time_t tim, TXTIMEINFO *timeinfo)
/* gmtime[_r]() wrapper, using OS gmtime[_r]().
 * Returns 0 on error.
 * May be thread-unsafe (if !EPI_HAVE_GMTIME_R), signal-unsafe.
 */
{
	struct tm	*tm;
#ifdef EPI_HAVE_GMTIME_R
	struct tm	tmBuf;

	tm = gmtime_r(&tim, &tmBuf);
#else /* !EPI_HAVE_GMTIME_R */
	tm = gmtime(&tim);
#endif /* !EPI_HAVE_GMTIME_R */
	if (!tm) return(0);
	TXstructTmToTxtimeinfo(tm, timeinfo);
	timeinfo->isDstStdOverlap = 0;		/* GMT */
	return(1);
}

int
TXtime_tToGmtTxtimeinfo(time_t tim, TXTIMEINFO *timeinfo)
/* Converts time `tim' and stores in `*timeinfo'.
 * NOTE: Timezone-related fields of `*timeinfo' will be cleared as if GMT.
 * Returns zero on error.
 * Thread-safe.  Signal-safe.
 */
{
	time_t			d, f, esty, estf, esty2;
	int			m;
	CONST int		*days;

	d = tim / DAY;
	f = (tim % DAY);
	if (f < (time_t)0)			/* get frac >= 0 */
	{
		d--;
		f += DAY;
	}
	timeinfo->hour = (int)f / HOUR;
	f %= HOUR;
	timeinfo->minute = (int)f / MINUTE;
	timeinfo->second = (int)f % MINUTE;
	timeinfo->dayOfWeek = ((d + 4) % 7) + 1;    /* 1970-01-01 was Thu */
	if (timeinfo->dayOfWeek < 1) timeinfo->dayOfWeek += 7;

	esty = 1970;
	while (d < 0 || d >= DAYS_PER_YEAR(esty))
	{
		/* Guess a corrected year, assuming isometric 365 days: */
		esty2 = esty + d / 365 - (d % 365 < 0);
		/* Fix days and year to match the guessed year: */
		d -= (esty2 - esty) * 365 + LEAPDAYS(esty2 - 1) -
			LEAPDAYS(esty - 1);
		esty = esty2;
	}
	estf = d;

	timeinfo->year = esty;          /* no overflow: `year' is time_t */
	timeinfo->dayOfYear = (int)estf + 1;
	days = (ISLEAPYEAR(esty) ? daysleap : daysnorm);
	for (m = 0; m < 12 && estf >= days[m]; estf -= days[m++]);
	timeinfo->month = m + 1;
	timeinfo->dayOfMonth = (int)(estf + 1);

	timeinfo->isDst = 0;
	timeinfo->gmtOffset = 0;
	timeinfo->isDstStdOverlap = 0;

	return(1);
}

int
TXtxtimeinfoToTime_t(const TXTIMEINFO *timeinfo, time_t *timP)
/* Converts time `*timeinfo' and stores in `*timP'.
 * NOTE: Also uses `gmtOffset' field of `*timeinfo'.
 * NOTE: Does not modify `*timeinfo', thus does not set `dayOf{Week,Year}'.
 * Returns zero on error (e.g. out of range).
 * Thread-safe.  Signal-safe.
 */
{
	time_t		tim, res, eRangeLoVal = 0;
	TXTIMEINFO	reqTimeinfo = *timeinfo;
	CONST int	*days;
	int		i, isERangeLo = 0, ret;

	/* Normalize `reqTimeinfo' just as much as needed,
	 * i.e. non-constant units.  Constant-size units will
	 * accumulate in linear time_t properly:
	 */
	reqTimeinfo.year += (reqTimeinfo.month - 1) / 12;
	if (reqTimeinfo.year < timeinfo->year) goto eRangeHi;
	reqTimeinfo.month = ((reqTimeinfo.month - 1) % 12) + 1;
	if (reqTimeinfo.month < 1)
		reqTimeinfo.month = 1;		/* wtf for safe mod below */

	/* Non-leap-day time from 1970 to `tm_year': */
	tim = (time_t)reqTimeinfo.year - (time_t)1970;
	res = tim*((time_t)365*DAY);
	if (res / ((time_t)365*DAY) != tim)	/* out of range */
	{
		if (reqTimeinfo.year >= (time_t)1970) goto eRangeHi;
		/* Do not bail yet for range-low: might get back in range
		 * when later values added:
		 */
		eRangeLoVal = res;
		isERangeLo = 1;
	}
	tim = res;

	/* Leap day time from 1970 to `tm_year': */
	res = tim + (LEAPDAYS((time_t)reqTimeinfo.year - (time_t)1) -
		LEAPDAYS(1970 - 1))*DAY;
	if (tim >= 0 && !isERangeLo)
	{
		if (res < tim) goto eRangeHi;
	}
	else if (tim < 0 && res > tim)
	{
		/* Do not bail yet for range-low: might get back in range
		 * when later values added:
		 */
		eRangeLoVal = res;
		isERangeLo = 1;
	}
	tim = res;

	/* Time in `tm_year' before `tm_mon': */
	days = (ISLEAPYEAR(reqTimeinfo.year) ? daysleap : daysnorm);
	res = tim;
	for (i = 0; i < reqTimeinfo.month - 1; i++)
		res += days[i]*DAY;
	if (res < tim && !isERangeLo) goto eRangeHi;
	tim = res;

	/* Time in `tm_mon' before `tm_mday': */
	res = tim + ((time_t)reqTimeinfo.dayOfMonth - (time_t)1)*DAY;
	if (res < tim && !isERangeLo) goto eRangeHi;
	tim = res;

	/* Hours/minutes/seconds: */
	res = tim + ((time_t)reqTimeinfo.hour)*HOUR;
	if (res < tim && !isERangeLo) goto eRangeHi;
	tim = res;
	res = tim + ((time_t)reqTimeinfo.minute)*MINUTE;
	if (res < tim && !isERangeLo) goto eRangeHi;
	tim = res;
	res = tim + (time_t)reqTimeinfo.second;
	if (res < tim && !isERangeLo) goto eRangeHi;
	tim = res;

	/* If still out of range low, and GMT fixup will not help, error: */
	if (reqTimeinfo.gmtOffset >= 0 && isERangeLo && tim >= eRangeLoVal)
		goto eRangeLo;

	/* Apply (usually-local) GMT offset to`tim': */
	tim -= reqTimeinfo.gmtOffset;		/* GMT = local - GMToff */

	if (reqTimeinfo.gmtOffset < 0 && isERangeLo && tim >= eRangeLoVal)
		goto eRangeLo;

        ret = 1;                                /* success */
        goto finally;

eRangeLo:
	tim = -(time_t)EPI_OS_TIME_T_MAX - (time_t)1;
	ret = 0;
        goto finally;
eRangeHi:
	tim = (time_t)EPI_OS_TIME_T_MAX;
	ret = 0;
finally:
        *timP = tim;
        return(ret);
}

#ifdef EPI_HAVE_TM_GMTOFF
#  define GET_GMT_OFF(tm)       ((int)(tm)->tm_gmtoff)
#elif defined(EPI_HAVE_TIMEZONE_VAR)
/* timezone/altzone are UTC - local; negate: */
#  ifdef EPI_HAVE_ALTZONE_VAR
#    define GET_GMT_OFF(tm)     (-((tm)->tm_isdst > 0 ? altzone : timezone))
#  elif defined(EPI_HAVE__DSTBIAS_VAR)
#    define GET_GMT_OFF(tm) (-((tm)->tm_isdst>0 ? timezone+_dstbias:timezone))
#  else /* !EPI_HAVE_ALTZONE_VAR && !EPI_HAVE__DSTBIAS_VAR */
/* WTF 3600 is WAG: */
#    define GET_GMT_OFF(tm)  (-((tm)->tm_isdst > 0 ? timezone-3600: timezone))
#  endif /* !EPI_HAVE_ALTZONE_VAR && !EPI_HAVE__DSTBIAS_VAR */
#else
#  error Cannot determine GMT offset
#endif

int
TXosTime_tToLocalTxtimeinfo(time_t tim, TXTIMEINFO *timeinfo)
/* localtime[_r]() wrapper, using OS localtime[_r]().
 * Converts `tim' to local time, storing in `*timeinfo'.
 * NOTE: `isDstStdOverlap' will be set to unknown.
 * Returns 0 on error.
 * May be thread-unsafe (if !EPI_HAVE_GMTIME_R), signal-unsafe.
 */
{
	struct tm	*tm;
#ifdef EPI_HAVE_GMTIME_R
	struct tm	tmBuf;

	tm = localtime_r(&tim, &tmBuf);
#else /* !EPI_HAVE_GMTIME_R */
	tm = localtime(&tim);
#endif /* !EPI_HAVE_GMTIME_R */
	if (!tm) return(0);
	TXstructTmToTxtimeinfo(tm, timeinfo);
	/* Get GMT offset again, in case it is set from globals
	 * and not tm.tm_gmtoff:
	 */
	timeinfo->gmtOffset = GET_GMT_OFF(tm);
	timeinfo->isDstStdOverlap = -1;		/* wtf */
	return(1);
}

static int
TXaddToTxtimeinfo(TXTIMEINFO *timeinfo, time_t offset)
/* Adds `offset' to `*timeinfo'.  For use when the time_t equivalent
 * of `*timeinfo' plus `offset' would under/overflow.
 * NOTE: `offset' should be less than one day.
 * Returns 0 on error.
 */
{
	const int	*daysPerMonth;
	int		daysPerThisYear, daysPerThisMonth;

	if (offset < (time_t)(-86400) || offset > (time_t)86400) return(0);
	if (offset < (time_t)0)
	{
		timeinfo->dayOfMonth--;
		timeinfo->dayOfWeek--;
		timeinfo->dayOfYear--;
		offset += 86400;
	}
	timeinfo->second += (int)offset;

	/* Re-normalize: rollover seconds to minutes etc.: */
	timeinfo->minute += timeinfo->second / MINUTE;
	timeinfo->second %= MINUTE;
	timeinfo->hour += timeinfo->minute / 60;
	timeinfo->minute %= 60;
	timeinfo->dayOfMonth += timeinfo->hour / 24;
	timeinfo->dayOfWeek += timeinfo->hour / 24;
	timeinfo->dayOfYear += timeinfo->hour / 24;
	timeinfo->hour %= 24;
	if (timeinfo->dayOfWeek <= 0)
		timeinfo->dayOfWeek += 7;
	else
		timeinfo->dayOfWeek = ((timeinfo->dayOfWeek - 1) % 7) + 1;
	/* wtf this month/year rollover math may be inaccurate if
	 * `offset' is > ~1 day, but for <= ~1 day it should suffice:
	 */
	daysPerMonth = (ISLEAPYEAR(timeinfo->year) ? daysleap : daysnorm);
	daysPerThisYear = DAYS_PER_YEAR(timeinfo->year);
	daysPerThisMonth = daysPerMonth[((unsigned)timeinfo->month - 1) % 12];
	if (timeinfo->dayOfMonth > daysPerThisMonth)
	{
		timeinfo->dayOfMonth -= daysPerThisMonth;
		timeinfo->month++;
	}
	else if (timeinfo->dayOfMonth <= 0)
	{
		if (--timeinfo->month <= 0)
		{
			timeinfo->month += 12;
			timeinfo->year--;
		}
		timeinfo->dayOfMonth +=
			daysPerMonth[(unsigned)(timeinfo->month - 1) % 12];
	}
	if (timeinfo->month > 12)
	{
		timeinfo->month -= 12;
		timeinfo->year++;
	}
	if (timeinfo->dayOfYear > daysPerThisYear)
		timeinfo->dayOfYear -= daysPerThisYear;
	else if (timeinfo->dayOfYear <= 0)
		timeinfo->dayOfYear += DAYS_PER_YEAR(timeinfo->year);
	return(1);
}

int
TXtime_tToLocalTxtimeinfo(time_t tim, TXTIMEINFO *timeinfo)
/* localtime_r implementation, using Texis.
 * Converts `tim' to local time, storing in `*timeinfo'.
 * NOTE: does not set any external vars such as timezone etc.
 * NOTE: may get incorrect time zone, depends on global TxTz* settings.
 * Returns 0 on error.
 * Thread-safe.  Signal-safe.
 */
{
	int		d;
	time_t		timStd;

	timStd = tim + TxTzOff[0];
	/* Use TXaddToTxtimeinfo() as a crutch if `timStd' overflowed: */
	if ((TxTzOff[0] < (time_t)0 && timStd > tim) ||
	    (TxTzOff[0] > (time_t)0 && timStd < tim))
	{
		if (!TXtime_tToGmtTxtimeinfo(tim, timeinfo))
			return(0);		/* error */
		if (!TXaddToTxtimeinfo(timeinfo, TxTzOff[0]))
			return(0);
	}
	else if (!TXtime_tToGmtTxtimeinfo(tim + TxTzOff[0], timeinfo))
		return(0);			/* error */

	/* Compute whether this is DST or not.  WTF USA rules assumed: */
	if (TxTzOff[1] == TxTzOff[0])		/* no DST */
		timeinfo->isDst = 0;
	else if (timeinfo->year < 2007)		/* pre-2007 USA rules: */
	{		/* 1st Sunday in Apr. through last Sunday in Oct. */
		if (timeinfo->month <= 3)		/* Jan. - Mar. */
			timeinfo->isDst = 0;
		else if (timeinfo->month == 4)	/* Apr. */
		{
			if (timeinfo->dayOfMonth <= 7)	/* Apr. 1 - 7 */
			{
				/* d = day-of-month of our week's Sunday: */
				d = timeinfo->dayOfMonth -
					(timeinfo->dayOfWeek - 1);
				/* if that is last month, get 1st Sun. this:*/
				if (d <= 0) d += 7;
				/* see if we are before/on/after 1st Sun.: */
				if (timeinfo->dayOfMonth < d) /* < 1st Sun. */
					timeinfo->isDst = 0;
				else if (timeinfo->dayOfMonth == d)/*1st Sun.*/
					timeinfo->isDst = (timeinfo->hour>=2);
				else		/* we are after first Sun. */
					timeinfo->isDst = 1;
			}
			else			/* Apr. 8 - 30 */
				timeinfo->isDst = 1;
		}
		else if (timeinfo->month <= 9)	/* May - Sept. */
			timeinfo->isDst = 1;
		else if (timeinfo->month == 10)	/* Oct. */
		{
			if (timeinfo->dayOfMonth < 25)	/* Oct. 1 - 24 */
				timeinfo->isDst = 1;
			else			/* Oct. 25 - 31 */
			{
				d = timeinfo->dayOfMonth -
					(timeinfo->dayOfWeek - 1);
				if (d < 25) d += 7;
				if (timeinfo->dayOfMonth < d) /* < last Sun.*/
					timeinfo->isDst = 1;
				else if (timeinfo->dayOfMonth == d)/*last Sun*/
				{
					timeinfo->isDst = (timeinfo->hour < 1);
					timeinfo->isDstStdOverlap =
						(timeinfo->hour == 1);
				}
				else		/* after last Sun. */
					timeinfo->isDst = 0;
			}
		}
		else				/* Nov. - Dec. */
			timeinfo->isDst = 0;
	}
	else					/* 2007+ USA rules: */
	{		/* 2nd Sunday in Mar. through 1st Sunday in Nov. */
		if (timeinfo->month <= 2)		/* Jan. - Feb. */
			timeinfo->isDst = 0;
		else if (timeinfo->month == 3)	/* Mar. */
		{
			if (timeinfo->dayOfMonth <= 7)	/* Mar. 1 - 7 */
				timeinfo->isDst = 0;
			else if (timeinfo->dayOfMonth <= 14) /* Mar. 8 - 14 */
			{
				/* d = day-of-month of our week's Sunday: */
				d = timeinfo->dayOfMonth -
					(timeinfo->dayOfWeek - 1);
				/* if that is not 2nd Sun., set to 2nd Sun: */
				if (d <= 7) d += 7;
				/* see if we are before/on/after 2nd Sun.: */
				if (timeinfo->dayOfMonth < d)  /* < 2nd Sun.*/
					timeinfo->isDst = 0;
				else if (timeinfo->dayOfMonth == d) /*2nd Sun*/
					timeinfo->isDst = (timeinfo->hour>=2);
				else		/* after 2nd Sun. */
					timeinfo->isDst = 1;
			}
			else			/* Mar. 15 - 31 */
				timeinfo->isDst = 1;
		}
		else if (timeinfo->month <= 10)	/* May - Oct. */
			timeinfo->isDst = 1;
		else if (timeinfo->month == 11)	/* Nov. */
		{
			if (timeinfo->dayOfMonth <= 7)	/* Nov. 1 - 7 */
			{
				/* d = day-of-month of our week's Sunday: */
				d = timeinfo->dayOfMonth -
					(timeinfo->dayOfWeek - 1);
				/* if that is last month, get 1st Sun. this:*/
				if (d <= 0) d += 7;
				/* see if we are before/on/after 1st Sun.: */
				if (timeinfo->dayOfMonth < d) /* < 1st Sun. */
					timeinfo->isDst = 1;
				else if (timeinfo->dayOfMonth == d)/*1st Sun.*/
				{
					timeinfo->isDst = (timeinfo->hour < 1);
					timeinfo->isDstStdOverlap =
						(timeinfo->hour == 1);
				}
				else		/* after 1st Sun. */
					timeinfo->isDst = 0;
			}
			else			/* Nov. 8 - 30 */
				timeinfo->isDst = 0;
		}
		else				/* Nov. - Dec. */
			timeinfo->isDst = 0;
	}
	/* Re-compute fields if it's DST: */
	if (timeinfo->isDst)
	{
		int	isDstStdOverlap = timeinfo->isDstStdOverlap;

		if (!TXtime_tToGmtTxtimeinfo(tim + TxTzOff[1], timeinfo))
			return(0);		/* error */
		timeinfo->isDst = 1;
		timeinfo->isDstStdOverlap = isDstStdOverlap;
	}

	timeinfo->gmtOffset = (int)TxTzOff[timeinfo->isDst];
	return(1);				/* success */
}

int
TXlocalTxtimeinfoToTime_t(const TXTIMEINFO *timeinfo, time_t *timP)
/* Converts local-time `*timeinfo' to `*timP'.
 * NOTE: ignores `gmtOffset' in `*timeinfo', and only uses `timeinfo->isDst'
 * if `*timeinfo' is in DST -> STD overlap zone.
 * Returns 0 on error.
 * Thread-safe.  Signal-safe.
 */
{
        TXTIMEINFO      myTimeinfo, splitTimeinfo;

        myTimeinfo = *timeinfo;
        myTimeinfo.gmtOffset = 0;
        myTimeinfo.isDst = 0;
        myTimeinfo.isDstStdOverlap = 0;
	if (!TXtxtimeinfoToTime_t(&myTimeinfo, timP)) return(0);
	/* Convert GMT `*timP' to local STD time: */
	*timP -= TxTzOff[0];			/* GMT = local - tzoff */
	if (TxTzOff[0] != TxTzOff[1])		/* timezone has DST */
	{
		if (!TXtime_tToLocalTxtimeinfo(*timP, &splitTimeinfo))
			return(0);
		/* If re-splitting indicates we are in DST, switch to it.
		 * Note that in DST -> STD overlap zone, we must rely on
		 * caller's `isDst'; e.g. 1:30am could be DST or STD.
		 * All other times are unambiguous; we use computed
		 * `isDst' from `splitTimeinfo':
		 */
		if (splitTimeinfo.isDstStdOverlap > 0 ? (timeinfo->isDst > 0)
                    : splitTimeinfo.isDst)
			*timP = *timP + TxTzOff[0] - TxTzOff[1];
	}
	return(1);				/* success */
}

int
tx_inittz()
/* Called at program start, in main thread, to initialize our idea of
 * time zone and GMT offset.  This is used by TX[os]time_tToLocalTxtimeinfo().
 * WTF don't know when next std/DST flip occurs; assume DST is +1 hour.
 * Returns 0 on error.
 * Thread-unsafe.  Signal-unsafe.
 * NOTE: should use a TXPMBUF arg for putmsgs.
 */
{
	struct tm	*tp;
	time_t		tim;

	if (TxTzDidInit) return(1);			/* already done */
	TxTzDidInit = 1;

	/* We call the real localtime() not only to get timezone info,
	 * but also (some platforms) to let tzset() malloc its buffer,
	 * so a later signal-handler call to localtime() won't need to.
	 * Alas, on some platforms localtime() seems to always use malloc,
	 * so we use TXtime_tToLocalTxtimeinfo() where possible.
	 */
	tim = 1328000000;			/* Jan 31 2012: STD time */
	tp = localtime(&tim);
	if (tp == (struct tm *)NULL) return(0);		/* error */
#ifdef EPI_HAVE_TM_GMTOFF
	TxTzOff[0] = tp->tm_gmtoff;
#elif defined EPI_HAVE_TIMEZONE_VAR
	TxTzOff[0] = -timezone;
#else /* !EPI_HAVE_TM_GMTOFF && !EPI_HAVE_TIMEZONE_VAR */
	TxTzOff[0] = 0;					/* WTF */
#endif /* !EPI_HAVE_TM_GMTOFF && !EPI_HAVE_TIMEZONE_VAR */

	TXstrncpy(TxTzName[0], tzname[0], sizeof(TxTzName[0]));
	TXstrncpy(TxTzName[1], tzname[1], sizeof(TxTzName[1]));

	tim = 1340000000;			/* Jun 18 2012: DST */
	tp = localtime(&tim);
	if (tp == (struct tm *)NULL) return(0);		/* error */
#ifdef EPI_HAVE_TM_GMTOFF
	TxTzOff[1] = tp->tm_gmtoff;
#elif defined(EPI_HAVE_ALTZONE_VAR)
	TxTzOff[1] = -altzone;
#elif defined(EPI_HAVE_TIMEZONE_VAR)
#  ifdef EPI_HAVE__DSTBIAS_VAR
	TxTzOff[1] = -(timezone + _dstbias);
#  else /* !EPI_HAVE__DSTBIAS_VAR */
	TxTzOff[1] = -(timezone - 3600);	/* WTF */
#  endif /* !EPI_HAVE__DSTBIAS_VAR */
#else
	TxTzOff[1] = 0;					/* WTF */
#endif

	return(1);
}

/* ------------------------------------------------------------------------- */

int
TXrawOpen(TXPMBUF *pmbuf, const char *fn, const char *pathDesc,
          const char *path, TXrawOpenFlag txFlags, int flags, int mode)
/* Wrapper for open().  If not `TXrawOpenFlag_Inheritable', adds in
 * TX_O_NOINHERIT, or does its fcntl() equivalent if unsupported.
 * `flags'/`mode' get passed to open().
 * `pathDesc' is optional description for `path'.
 * Preserves open() error code.
 * All open()s should go through this function, to ensure FD_CLOEXEC.
 * Thread-safe, async-signal-safe (if `pmbuf' is).
 */
{
  int           fd;
  const char    *modeDesc, *exclDesc;

  /* NOTE: we may be called in an su-like context (htconn_openskt());
   * caller should use TXPMBUF_SUPPRESS in such cases to prevent
   * privilege side effects here, and report error themselves.
   */

  /* We prefer setting TX_O_NOINHERIT atomically at open(), rather
   * than later fcntl(FD_CLOEXEC): not only is latter a second system
   * call, but more importantly it creates a race where another thread
   * might fork() + exec() between our open() and fcntl(FD_CLOEXEC):
   */
  if (!(txFlags & TXrawOpenFlag_Inheritable)) flags |= TX_O_NOINHERIT;

  TXclearError();
  fd = open(path, flags, mode);
  TX_PUSHERROR();
  if (fd < 0)                                   /* failed */
    {
      switch (flags & 0x3)
        {
        case O_RDONLY:  modeDesc = " read-only"; break;
        case O_WRONLY:  modeDesc = " write-only"; break;
        case O_RDWR:    modeDesc = " read-write"; break;
        default:        modeDesc = " write-only + read-write?"; break;
        }
      exclDesc = ((flags & O_EXCL) ? " exclusive" : "");
      if (flags & O_CREAT) modeDesc = "";
      if ((txFlags & TXrawOpenFlag_SuppressNoSuchFileErr) &&
          TX_ERROR_IS_NO_SUCH_FILE(saveErr))
        ;
      else if ((txFlags & TXrawOpenFlag_SuppressFileExistsErr) &&
               errno == EEXIST)
        ;
      else
        txpmbuf_putmsg(pmbuf, MERR + ((flags & O_CREAT) ? FCE : FOE), fn,
                       "Cannot %s%s%s `%s'%s%s: %s",
                       ((flags & O_CREAT) ? "create" : "open"),
                       (pathDesc ? " " : ""), (pathDesc ? pathDesc : ""),
                       path, modeDesc, exclDesc, TXstrerror(saveErr));
      goto err;
    }

  if (!(txFlags & TXrawOpenFlag_Inheritable) && !TX_O_NOINHERIT)
    {
#ifdef _WIN32
      /* Should not happen, because TX_O_NOINHERIT is non-zero in Windows.
       * But there is no FD_CLOEXEC to fall back on:
       */
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
         "Internal error: No after-open() remedy for lack of TX_O_NOINHERIT");
#else /* !_WIN32 */
      int       fdFlags;

      fdFlags = fcntl(fd, F_GETFD);
      if (fdFlags != -1)                        /* success */
        fcntl(fd, F_SETFD, (fdFlags | FD_CLOEXEC));
#endif /* !_WIN32 */
    }
  goto finally;

err:
  if (fd >= 0)
    {
      close(fd);
      fd = -1;
    }
finally:
  TX_POPERROR();
  return(fd);
}

/* ------------------------------------------------------------------------- */

int
tx_rawread(pmbuf, fd, fnam, buf, sz, flags)
TXPMBUF         *pmbuf; /* (in, opt.) buffer for messages */
int             fd;
CONST char      *fnam;
byte            *buf;
size_t          sz;
int             flags;  /* (in) flags */
/* flags:
 * 0x01: err msgs
 * 0x02: return after first successful read (e.g. stdin line-by-line reads)
 * 0x04: negative return value if EOF reached
 * 0x08: short read (EOF) is not an error.
 * 0x10: EWOULDBLOCK ok
 * Signal-safe iff !_WIN32 (isatty() safeness unknown)
 */
{
  static CONST char     fn[] = "tx_rawread";
  size_t                rd, rda = (size_t)0;
  int                   tries, goteof = 0;
#ifdef _WIN32
  int                   istty;
  unsigned int          n;

  istty = (sz > (size_t)0 && fd >= 0 && isatty(fd));
#else /* !_WIN32 */
  size_t                n;
#endif /* !_WIN32 */

  for (rd = 0; rd < sz; rd += rda)
    {
      if (rd > 0 && (flags & 0x2)) break;       /* e.g. stdin line-by-line */
      tries = 0;
      do
        {
#ifdef _WIN32
          /* Windows read() takes an unsigned int.
           * And it fails if the count is > INT_MAX, not UINT_MAX:
           */
          n = (unsigned int)TX_MIN(sz - rd, (size_t)EPI_OS_INT_MAX);
          /* Under Windows read() uses ReadFile(), which for stdin tty seems
           * to fail with GetLastError() == 8 (not enough space) when buffer
           * size is larger than about 16KB (e.g. 65535).  See Bugzilla 1021.
           */
          if (istty && n > 16384) n = 16384;
#else /* !_WIN32 */
          n = sz - rd;
#endif /* !_WIN32 */
          /* Windows: avoid an "Invalid Parameter" message from our
           * CRT handler: check parameters before calling read(),
           * and report badness ourselves (iff caller requested):
           */
          if (fd < 0)                           /* invalid parameter */
            {
#ifdef _WIN32
              TXseterror(ERROR_INVALID_PARAMETER);
#endif /* _WIN32 */
              errno = EINVAL;
              rda = (size_t)(-1);
            }
          else
            {
              TXclearError();
              rda = (size_t)read(fd, buf + rd, n);
            }
        }
      while (rda == (size_t)(-1) && errno == EINTR && ++tries < 25);
      if (rda == 0) goteof = 1;
      if (rda == (size_t)(-1) || rda == 0) break;
    }
  /* With (flags & 0x2), a partial read is likely but ok, so only check
   * for actual error (rda == -1) rather than short read (rd != sz):
   */
  if (((flags & 0xa) ? (rd == 0 && rda == (size_t)(-1)) : rd != sz) &&
      (flags & 0x1))
    {
      if (!(flags & 0x10) || TXgeterror() != TXERR_EWOULDBLOCK)
        {
          TX_PUSHERROR();
          txpmbuf_putmsg(pmbuf, MERR + FRE, fn,
                         "Cannot read%s 0x%wx bytes from `%s': %s%s",
                         ((flags & 0x08) ? " up to" : ""), (EPI_HUGEUINT)sz,
                         fnam,
                         ((errno != 0) ? strerror(errno) : "Read past EOF?"),
                         (fd < 0 ? " (fd < 0)" : ""));
          TX_POPERROR();
        }
    }
  return((flags & 0x4) && goteof ? -(int)rd : (int)rd);
}

size_t
tx_rawwrite(pmbuf, fd, path, pathIsDescription, buf, sz, inSig)
TXPMBUF         *pmbuf; /* (in, opt.) buffer for messages */
int             fd;     /* (in) file descriptor to write to */
const char      *path;  /* (in) for messages */
TXbool          pathIsDescription;      /* (in) `path' is a description */
byte            *buf;   /* (in) buffer to write */
size_t          sz;     /* (in) size of `buf' */
TXbool          inSig;  /* (in) currently in a signal */
/* Yaps to `pmbuf' if not all of `buf' can be written successfully.
 * Returns number of bytes of `buf' written to `fd'.
 * Thread-safe.  Async-signal-safe (iff `pmbuf' is).
 */
{
  size_t        wr, wra;
  int           tries;
#ifdef _WIN32
  unsigned int  wrTry;
#else /* !_WIN32 */
  size_t        wrTry;
#endif /* !_WIN32 */

  for (wr = 0; wr < sz; wr += wra)
    {
      tries = 0;
      do
        {
          /* Windows: avoid an "Invalid Parameter" message from our
           * CRT handler: check parameters before calling write(),
           * and report badness ourselves (i.e. with filename):
           */
          if (fd < 0)                           /* invalid parameter */
            {
#ifdef _WIN32
              TXseterror(ERROR_INVALID_PARAMETER);
#endif /* _WIN32 */
              errno = EINVAL;
              wra = (size_t)(-1);
            }
          else
            {
#ifdef _WIN32
              /* Windows write() takes an unsigned int.  And it
               * probably fails if the count is > INT_MAX, not
               * UINT_MAX, like read() fails:
               */
              wrTry = (unsigned int)TX_MIN(sz - wr, (size_t)EPI_OS_INT_MAX);
#else /* !_WIN32 */
              wrTry = sz - wr;
#endif /* !_WIN32 */
              TXclearError();
              wra = (size_t)write(fd, buf + wr, wrTry);
            }
        }
      while (wra == (size_t)(-1) && errno == EINTR && ++tries < 25);
      if (wra == (size_t)(-1) || wra == 0) break;
    }
  if (wr != sz)
    {
      int       errnum = MERR + FRE;

      if (inSig) errnum += TX_PUTMSG_NUM_IN_SIGNAL;
      TX_PUSHERROR();
      txpmbuf_putmsg(pmbuf, errnum, __FUNCTION__,
                     "Cannot write 0x%wx bytes to %s%s%s: %s%s",
                     (EPI_HUGEUINT)sz, (pathIsDescription ? "" : "file `"),
                     path, (pathIsDescription ? "" : "'"),
                     /* strerror() is not listed as async-signal-safe: */
                     (errno != 0 ?
                      (inSig ? TXgetOsErrName(errno, "?") : strerror(errno))
                      : "No space?"),
                     (fd < 0 ? " (fd < 0)" : ""));
      TX_POPERROR();
    }
  return(wr);
}

/****************************************************************************/

#ifdef _WIN32

int
TXstartTexisMonitorService(TXPMBUF *pmbuf, int trace)
/* Tries to start Texis Monitor service.
 * Returns nonzero if successful, 0 on error (check TXgeterror()
 * for ERROR_SERVICE_ALREADY_RUNNING).
 */
{
	static const char	fn[] = "TXstartTexisMonitorService";
	SC_HANDLE hSCManager = NULL;
	SC_HANDLE hSTexis = NULL;
	BOOL rc = FALSE, started = FALSE;
	TXERRTYPE	returnErr = 0;

	hSCManager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE,
			      SC_MANAGER_CONNECT);
	returnErr = TXgeterror();
	if (hSCManager)
	{
		hSTexis = OpenService(hSCManager, "Texis Monitor",
				    SERVICE_START);
		returnErr = TXgeterror();
		if (hSTexis)
		{
			rc = StartService(hSTexis, 0, NULL);
			returnErr = TXgeterror();
			if (rc)
				started = TRUE;
			else
			{
				if (trace)
					txpmbuf_putmsg(pmbuf,
						       TXTRACELICENSE_MSG, fn,
					   "StartService() failed: err %d=%s",
						       (int)returnErr,
					  TXgetOsErrName(returnErr, Ques));
			}
			CloseServiceHandle(hSTexis);
			hSTexis = NULL;
		}
		else
		{
			if (trace)
				txpmbuf_putmsg(pmbuf, TXTRACELICENSE_MSG, fn,
				       "OpenService() failed: err %d=%s",
					       (int)returnErr,
					  TXgetOsErrName(returnErr, Ques));
		}
		CloseServiceHandle(hSCManager);
		hSCManager = NULL;
	}
	else
	{
		if (trace)
			txpmbuf_putmsg(pmbuf, TXTRACELICENSE_MSG, fn,
				       "OpenSCManager() failed: err %d=%s",
				       (int)returnErr,
				       TXgetOsErrName(returnErr, Ques));
	}
	if (trace)
		txpmbuf_putmsg(pmbuf, TXTRACELICENSE_MSG, fn,
			       "Texis Monitor service start: %s err %d=%s",
			       (started ? "ok" : "failed"),
			       (int)returnErr,
			       TXgetOsErrName(returnErr, Ques));
	TXseterror(returnErr);
	return(rc ? 1 : 0);
}

#ifdef TX_DEBUG
int
debugbreak()
{
	DebugBreak();
	return 0;
}
#endif

/* ---------------------------------- stat --------------------------------- */

static int tx_isrootuncname ARGS((CONST char *path));
static int
tx_isrootuncname(path)
CONST char      *path;
{
  CONST char    *p;

  if ((strlen(path) >= 5) &&
      (*path == '/' || *path == '\\') &&
      (path[1] == '/' || path[1] == '\\'))
    {
      p = path + 2;
      while (*++p != '\0')
        if (*p == '/' || *p == '\\')
          break;
      if (*p != '\0' && p[1] != '\0')
        {
          while (*++p != '\0')
            if (*p == '/' || *p == '\\')
              break;
          if (*p == '\0' || p[1] == '\0')
            return(1);
        }
    }
  return(0);
}

static unsigned short tx_fa2mode ARGS((DWORD fa, CONST char *name));
static unsigned short
tx_fa2mode(fa, name)
DWORD           fa;     /* file attributes */
CONST char      *name;  /* (in, opt.) path if known */
/* Returns stat() st_mode-style bits for file attributes `fa'.
 */
{
  unsigned short        uxmode;
  CONST char            *p;

  uxmode = ((fa & FILE_ATTRIBUTE_DIRECTORY) ? (S_IFDIR | S_IEXEC) : S_IFREG);
  uxmode |= ((fa & FILE_ATTRIBUTE_READONLY) ? S_IREAD : (S_IREAD | S_IWRITE));
  if (!(uxmode & S_IEXEC) &&
      name != CHARPN &&
      (p = strchr(name, '.')) != CHARPN)
    {
      p++;
      if (strcmpi(p, "exe") == 0 ||
          strcmpi(p, "cmd") == 0 ||
          strcmpi(p, "bat") == 0 ||
          strcmpi(p, "com") == 0)
        uxmode |= S_IEXEC;
    }
  uxmode |= ((uxmode & 0700) >> 3);
  uxmode |= ((uxmode & 0700) >> 6);
  return(uxmode);
}
#endif /* _WIN32 */

static CONST char * CONST     TxFileAttrSymbols[TXFILEATTR_ORD_NUM] =
  {
#undef I
#define I(sym, tok)     #sym,
    TXFILEATTR_SYMBOLS_LIST
#undef I
  };
static CONST char * CONST     TxFileAttrTokens[TXFILEATTR_ORD_NUM] =
  {
#undef I
#define I(sym, tok)     tok,
    TXFILEATTR_SYMBOLS_LIST
#undef I
  };

CONST char *
TXfileAttrName(ord, useInternal)
TXFILEATTR_ORD  ord;
int             useInternal;    /* (in) nonzero: sans `_' */
{
  if ((unsigned)ord < (unsigned)TXFILEATTR_ORD_NUM)
    return(useInternal ? TxFileAttrTokens[(unsigned)ord] :
           TxFileAttrSymbols[(unsigned)ord]);
  return(CHARPN);
}

TXFILEATTR
TXstrToFileAttr(s, n)
CONST char      *s;     /* (in) string to parse */
size_t          n;      /* (in) its length (-1 == strlen) */
/* Returns FILE_ATTRIBUTE bit flag for `s', or 0 if unknown.
 * `s' is TXFILEATTRACTION or FILE_ATTRIBUTE string.
 */
{
  TXFILEATTR_ORD                i;

  if (n == (size_t)(-1)) n = strlen(s);
  for (i = 0; i < TXFILEATTR_ORD_NUM; i++)
    if ((strnicmp(s, TxFileAttrSymbols[i], n) == 0 &&
         TxFileAttrSymbols[i][n] == '\0') ||
        (strnicmp(s, TxFileAttrTokens[i], n) == 0 &&
         TxFileAttrTokens[i][n] == '\0'))
      return((TXFILEATTR)(1 << i));
  return((TXFILEATTR)0);
}

size_t
TXfileAttrModeString(buf, sz, fa, useInternal)
char            *buf;   /* (out) buffer to write to */
size_t          sz;     /* (in) `buf' size */
TXFILEATTR      fa;     /* (in) attrs to write */
int             useInternal;
/* Writes `readonly,archive,...' string for `fa' to `buf'.
 * Returns would-be strlen() of output; if >= `sz', `buf' is too small.
 */
{
  char                  *d, *e;
  TXFILEATTR_ORD        i;
  size_t                n, na;
  CONST char            *s;
  char                  tmp[20 + sizeof(TXFILEATTR)*4];

  e = buf + sz;
  d = buf;
  for (i = (TXFILEATTR_ORD)0; fa; i++)
    {
      if (!(fa & (1 << i))) continue;           /* this bit not set */
      fa &= ~(1 << i);
      s = TXfileAttrName(i, useInternal);
      if (s == CHARPN)
        {
          htsnpf(tmp, sizeof(tmp),
                 (useInternal ? "unknown%04x" : "UNKNOWN%04X"),
                 (unsigned)(1 << i));
          s = tmp;
        }
      n = strlen(s);
      if (d > buf)                              /* need separator */
        {
          if (d < e) *d = ',';
          d++;
        }
      if (d < e)                                /* still room */
        {
          na = n;
          if (na > (size_t)(e - d)) na = e - d;
          memcpy(d, s, na);
        }
      d += n;
    }
  if (sz > 0)
    *(d < e ? d : e - 1) = '\0';
  return(d - buf);
}

TXFILEATTRACTION *
TXfileAttrActionOpen(s)
CONST char      *s;
/* Return a linked list of FILE_ATTRIBUTE mode change operations created from
 * `s', an ASCII mode string that contains either an octal number
 * specifying an absolute mode, or symbolic mode change operations with
 * the form:
 *   +-=[readonly,archive,...]+-=[readonly,archive,...]]
 * Returns NULL if `s' does not contain a valid representation of
 * FILE_ATTRIBUTE mode change operations, or if there is insufficient memory.
 */
{
  TXFILEATTRACTION      *head;          /* First element of the list. */
  TXFILEATTRACTION      *change;        /* An element of the linked list. */
  int                   i;              /* General purpose temporary. */
  size_t                n;
  TXFILEATTR            fa;

  i = oatoi(s);
  if (i >= 0)                                   /* was an octal value */
    {
      if (i >= (1 << TXFILEATTR_ORD_NUM)) return(TXFILEATTRACTIONPN);
      head = talloc(TXFILEATTRACTION);
      if (head == TXFILEATTRACTIONPN) return(TXFILEATTRACTIONPN);
      head->next = TXFILEATTRACTIONPN;
      head->op = '=';
      head->value = (TXFILEATTR)i;
      return(head);
    }

  change = head = TXFILEATTRACTIONPN;

  /* One loop iteration for each "=+-readonly,hidden,...". */
  while (*s != '\0')
    {
      /* Add the element to the tail of the list, so the operations
       * are performed in the correct order.
       */
      if (head == TXFILEATTRACTIONPN)
        {
          head = talloc(TXFILEATTRACTION);
          if (head == TXFILEATTRACTIONPN) return(TXFILEATTRACTIONPN);
          change = head;
        }
      else
        {
          change->next = talloc(TXFILEATTRACTION);
          if (change->next == TXFILEATTRACTIONPN)
            return(TXfileAttrActionClose(head));
          change = change->next;
        }

      change->next = TXFILEATTRACTIONPN;
      switch (*s)
        {
        case '+':
        case '-':
        case '=':
          change->op = *(s++);
          break;
        default:
          goto invalid;
        }
      change->value = 0;

      for ( ; (n = strcspn(s, ",+-=")) > 0; s += n + (s[n] == ','))
        {
          fa = TXstrToFileAttr(s, n);
          if (!fa) break;                       /* not a mode; prolly +-= */
          switch (fa)
            {
              /* These cannot be changed: */
            case TXFILEATTR_DIRECTORY:
            case TXFILEATTR_DEVICE:
              /* Attempting to change these is silently ignored; since
               * there is typically another Windows call to set them,
               * we will make setting them an error until we implement
               * those calls:
               */
            case TXFILEATTR_COMPRESSED:
            case TXFILEATTR_ENCRYPTED:
            case TXFILEATTR_REPARSE_POINT:
            case TXFILEATTR_SPARSE_FILE:
              goto invalid;
            case TXFILEATTR_NORMAL:
              /* FILE_ATTRIBUTE_NORMAL must be set without other attrs: */
              if (change->value || change->op != '=') goto invalid;
              break;
            default:
              break;
            }
          change->value |= fa;
        }
    }

  if (*s == '\0') return(head);
invalid:
  return(TXfileAttrActionClose(head));
}

TXFILEATTRACTION *
TXfileAttrActionClose(f)
TXFILEATTRACTION        *f;
{
  TXFILEATTRACTION      *next;

  while (f != TXFILEATTRACTIONPN)
    {
      next = f->next;
      free(f);
      f = next;
    }
  return(TXFILEATTRACTIONPN);
}

TXFILEATTR
TXfileAttrActionAdjust(f, attrs, mode)
TXFILEATTRACTION        *f;     /* (in) actions to perform */
TXFILEATTR              attrs;  /* (in) file attr bits to adjust (Windows) */
unsigned                *mode;  /* (in/out, opt.) chmod mode to adjust, Unix*/
/* Return FILE_ATTRIBUTES_... `attrs', adjusted as indicated by the list
 * of change operations `f'.  Also modifies chmod-style `mode', for Unix.
 */
{
  TXFILEATTR    newAttrs;
  unsigned      newMode, orgMode;

  newAttrs = attrs;
  newMode = orgMode = (mode ? *mode : 0);

  for ( ; f != TXFILEATTRACTIONPN; f = f->next)
    {
      switch (f->op)
        {
        case '=':
          newAttrs = f->value;
          /* Keep --x--x--x bits, if a directory: */
          if (S_ISDIR(orgMode))
            newMode = (orgMode & (S_IXUSR | S_IXGRP | S_IXOTH));
          else
            newMode = 0;
          if (f->value & TXFILEATTR_READONLY)
            {
              newMode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
              /* Everything under DOS is always readable: */
              newMode |= (S_IRUSR | S_IRGRP | S_IROTH);
            }
          else if (f->value & TXFILEATTR_NORMAL)
            newMode |= (S_IRUSR | S_IRGRP | S_IROTH |
                        S_IWUSR | S_IWGRP | S_IWOTH);
          break;
        case '+':
          newAttrs |= f->value;
          if (f->value & TXFILEATTR_READONLY)
            {
              newMode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
              /* Everything under DOS is always readable: */
              newMode |= (S_IRUSR | S_IRGRP | S_IROTH);
            }
          /* TXFILEATTR_NORMAL only valid alone with `=' operator */
          break;
        case '-':
          newAttrs &= ~f->value;
          if (f->value & TXFILEATTR_READONLY)
            {
              /* Everything under DOS is always readable: */
              newMode |= (S_IRUSR | S_IRGRP | S_IROTH |
                          S_IWUSR | S_IWGRP | S_IWOTH);
            }
          /* TXFILEATTR_NORMAL only valid alone with `=' operator */
          break;
        }
    }
  if (mode) *mode = newMode;
  return(newAttrs);
}

CONST char *
TXftimesrcName(fts)
TXFTIMESRC      fts;
{
  static CONST char * CONST     names[TXFTIMESRC_NUM] =
    {
#undef I
#define I(sym)  #sym,
      TXFTIMESRC_SYMBOLS_LIST
#undef I
    };

  if ((unsigned)fts < (unsigned)TXFTIMESRC_NUM)
    return(names[(unsigned)fts]);
  return(CHARPN);
}

int
TXstat(path, fd, doLink, stbuf)
CONST char      *path;  /* (in, opt.) file to stat */
int             fd;     /* (in, opt.) != -1: file descriptor to fstat() */
int             doLink; /* (in) nonzero: do lstat() */
TXSTATBUF       *stbuf; /* (out) extended stat info */
/* Replacement for stat() that does not use FindFirstFile(), which can
 * fail if the parent dir does not have List Folder / Read Data perms,
 * leading to erroneous File not found error.
 * Sets system (TXgeterror()) error.  Derived from CRT\SRC\STAT.C.
 * Returns 0 on success, -1 on error.
 */
{
#ifdef _WIN32
  int                           drive;
  WIN32_FILE_ATTRIBUTE_DATA     fad;
  BY_HANDLE_FILE_INFORMATION    bhfi;
  HANDLE                        fdHandle;
  unsigned long                 szAvail;
  TXERRTYPE                     savErr;
  int                           devType, res;
  char                          *p;
  char                          tmp[PATH_MAX];

  memset(stbuf, 0, sizeof(TXSTATBUF));
  if (doLink)                                   /* no symlinks in Windows */
    {
      /* wtf */
#  ifdef ENOSYS
      errno = ENOSYS;
#  else /* !ENOSYS */
      errno = EINVAL;
#  endif /* !ENOSYS */
      TXseterror(ERROR_INVALID_FUNCTION);       /* arbitrary error */
      return(-1);
    }

  if (fd != -1)                                 /* fstat(fd) requested */
    {
      /* WTF lock the file if threaded? _lock_fh() */
      if ((fdHandle = (HANDLE)_get_osfhandle(fd)) == INVALID_HANDLE_VALUE)
        goto badFd;
      devType = (GetFileType(fdHandle) & ~FILE_TYPE_REMOTE);
      if (devType != FILE_TYPE_DISK)            /* not a disk file; device? */
        {
          if (devType == FILE_TYPE_CHAR || devType == FILE_TYPE_PIPE)
            {                                   /* no further info available */
              if (devType == FILE_TYPE_CHAR)
                stbuf->stat.st_mode = _S_IFCHR;
              else
                stbuf->stat.st_mode = _S_IFIFO;
              stbuf->stat.st_rdev = stbuf->stat.st_dev = fd;
              stbuf->stat.st_nlink = 1;
              stbuf->stat.st_uid = stbuf->stat.st_gid = stbuf->stat.st_ino =0;
              stbuf->stat.st_atime = stbuf->stat.st_mtime =
                stbuf->stat.st_ctime = (time_t)0;
              stbuf->st_atimeSrc = stbuf->st_mtimeSrc =
                stbuf->st_ctimeSrc = TXFTIMESRC_fixed;
              stbuf->lastAccessedTime = stbuf->lastModifiedTime =
                stbuf->creationTime = -EPI_OS_DOUBLE_MAX;
              stbuf->fileAttrs = TXFILEATTR_DEVICE;     /* ? wtf */
              if (devType == FILE_TYPE_CHAR)
                stbuf->stat.st_size = (EPI_OFF_T)0;
              else
                {
                  res = PeekNamedPipe(fdHandle, NULL, 0, NULL, &szAvail,
                                      NULL);
                  stbuf->stat.st_size = (res ? (EPI_OFF_T)szAvail :
                                         (EPI_OFF_T)0);
                }
              return(0);                        /* success */
            }
          if (devType == FILE_TYPE_UNKNOWN)
            {
            badFd:
              errno = EBADF;
              TXseterror(ERROR_INVALID_HANDLE);
              return(-1);                       /* error */
            }
          /* cannot happen? */
          goto dosMapErr;
        }
      if (!GetFileInformationByHandle(fdHandle, &bhfi))
        {
        dosMapErr:
          errno = TXmapOsErrorToErrno(GetLastError());
          return(-1);
        }
      drive = 1;                                /* wtf not in `bhfi' */
      goto gotFileInfo;
    }

  if (*path != '\0' && path[1] == ':')          /* get drive */
    {
      if (path[2] == '\0')                      /* C: */
        {
          errno = ENOENT;
          TXseterror(ERROR_FILE_NOT_FOUND);
          return(-1);                           /* error */
        }
      drive = ((*path >= 'A' && *path <= 'Z') ? (*path - 'A') + 1 :
               (*path - 'a') + 1);
    }
  else
    {
      if (GetCurrentDirectory(sizeof(tmp), tmp) &&
          tmp[1] == ':')
        drive = ((*tmp >= 'A' && *tmp <= 'Z') ? (*tmp - 'A') + 1 :
                 (*tmp - 'a') + 1);
      else
        drive = 0;                              /* unknown */
    }

  bhfi.nNumberOfLinks = 1;                      /* wtf not in `fad' */
  if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
    {
      savErr = TXgeterror();
      if (!(strpbrk(path, "./\\") != CHARPN &&
            (p = fullpath(tmp, path, sizeof(tmp))) != CHARPN &&
            (strlen(p) == 3 || tx_isrootuncname(p)) &&
            GetDriveType(p) > 1))
        {
          errno = ENOENT;
          /* leave GetFileAttributesEx error: may be netpath not found etc. */
          TXseterror(savErr);
          return(-1);                           /* error */
        }
      /* fake attributes for root dir: */
      bhfi.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      bhfi.nFileSizeHigh = bhfi.nFileSizeLow = 0;
      stbuf->lastAccessedTime = stbuf->lastModifiedTime = stbuf->creationTime=
        -EPI_OS_DOUBLE_MAX;
      stbuf->stat.st_mtime = stbuf->stat.st_atime = stbuf->stat.st_ctime =
        (time_t)315532800 + (time_t)TxTzOff[0]; /* 1980-01-01 local time */
      stbuf->st_atimeSrc = stbuf->st_mtimeSrc = stbuf->st_ctimeSrc =
        TXFTIMESRC_fixed;
    }
  else
    {
      /* Copy `fad' data to `bhfi'; `bhfi' is a superset of `fad': */
      bhfi.ftLastWriteTime = fad.ftLastWriteTime;
      bhfi.ftLastAccessTime = fad.ftLastAccessTime;
      bhfi.ftCreationTime = fad.ftCreationTime;
      bhfi.dwFileAttributes = fad.dwFileAttributes;
      bhfi.nFileSizeHigh = fad.nFileSizeHigh;
      bhfi.nFileSizeLow = fad.nFileSizeLow;
    gotFileInfo:
      stbuf->lastModifiedTime = TXfiletime2time_t(
                                  bhfi.ftLastWriteTime.dwLowDateTime,
                                  bhfi.ftLastWriteTime.dwHighDateTime);
      stbuf->stat.st_mtime = (time_t)stbuf->lastModifiedTime;
      stbuf->st_mtimeSrc = TXFTIMESRC_written;

      if (bhfi.ftLastAccessTime.dwLowDateTime || /* not all filesystems */
          bhfi.ftLastAccessTime.dwHighDateTime)
        {
          stbuf->lastAccessedTime = TXfiletime2time_t(
                                      bhfi.ftLastAccessTime.dwLowDateTime,
                                      bhfi.ftLastAccessTime.dwHighDateTime);
          stbuf->stat.st_atime = (time_t)stbuf->lastAccessedTime;
          stbuf->st_atimeSrc = TXFTIMESRC_accessed;
        }
      else                                      /* no access time set */
        {
          stbuf->lastAccessedTime = -EPI_OS_DOUBLE_MAX;
          /* Most users of stat() assume st_atime is set, so use mtime: */
          stbuf->stat.st_atime = (time_t)stbuf->lastModifiedTime;
          stbuf->st_atimeSrc = TXFTIMESRC_written;
        }

      if (bhfi.ftCreationTime.dwLowDateTime ||   /* not all filesystems */
          bhfi.ftCreationTime.dwHighDateTime)
        {
          stbuf->creationTime = TXfiletime2time_t(
                                  bhfi.ftCreationTime.dwLowDateTime,
                                  bhfi.ftCreationTime.dwHighDateTime);
          /* st_ctime is really *changed* time (inode info), but Windows
           * stat() maps it to creation time:
           */
          stbuf->stat.st_ctime = (time_t)stbuf->creationTime;
          stbuf->st_ctimeSrc = TXFTIMESRC_created;
        }
      else
        {
          stbuf->creationTime = -EPI_OS_DOUBLE_MAX;
          /* Most users of stat() assume st_ctime is set, so use mtime: */
          stbuf->stat.st_ctime = (time_t)stbuf->lastModifiedTime;
          stbuf->st_ctimeSrc = TXFTIMESRC_written;
        }
    }

  stbuf->fileAttrs = (TXFILEATTR)bhfi.dwFileAttributes;

  stbuf->stat.st_mode = tx_fa2mode(bhfi.dwFileAttributes, path);
  stbuf->stat.st_nlink = (short)bhfi.nNumberOfLinks;
#  if EPI_OFF_T_BITS >= 64
  stbuf->stat.st_size = (((EPI_OFF_T)bhfi.nFileSizeHigh) << 32) +
    (EPI_OFF_T)bhfi.nFileSizeLow;
#  else /* EPI_OFF_T_BITS < 64 */
  stbuf->stat.st_size = (EPI_OFF_T)bhfi.nFileSizeLow;
#  endif /* EPI_OFF_T_BITS < 64 */
  stbuf->stat.st_uid = stbuf->stat.st_gid = stbuf->stat.st_ino = 0;
  stbuf->stat.st_rdev = stbuf->stat.st_dev = drive - 1;
  return(0);                                    /* success */
#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  int   res;

  memset(stbuf, 0, sizeof(TXSTATBUF));
  res = (path != CHARPN ? (doLink ? EPI_LSTAT(path, &stbuf->stat) :
                           EPI_STAT(path, &stbuf->stat)) :
         EPI_FSTAT(fd, &stbuf->stat));
  if (res != 0) return(-1);

  stbuf->lastAccessedTime = TX_GET_STAT_ATIME_DOUBLE(stbuf->stat);
  stbuf->lastModifiedTime = TX_GET_STAT_MTIME_DOUBLE(stbuf->stat);
  /* Unix does not store creation time; st_ctime is *change* time: */
  stbuf->creationTime = -EPI_OS_DOUBLE_MAX;
  stbuf->st_atimeSrc = TXFTIMESRC_accessed;
  stbuf->st_mtimeSrc = TXFTIMESRC_written;
  stbuf->st_ctimeSrc = TXFTIMESRC_changed;

  stbuf->fileAttrs = (TXFILEATTR)0;
  if (!(stbuf->stat.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
    stbuf->fileAttrs |= TXFILEATTR_READONLY;
  if (S_ISDIR(stbuf->stat.st_mode))
    stbuf->fileAttrs |= TXFILEATTR_DIRECTORY;
  if (S_ISCHR(stbuf->stat.st_mode) ||
      S_ISBLK(stbuf->stat.st_mode))
    stbuf->fileAttrs |= TXFILEATTR_DEVICE;      /* wtf? reserved in Windows */
  /* If no other flags, set TXFILEATTR_NORMAL, per Windows: */
  if (stbuf->fileAttrs == (TXFILEATTR)0 && S_ISREG(stbuf->stat.st_mode))
    stbuf->fileAttrs |= TXFILEATTR_NORMAL;

  return(0);                                    /* success */
#endif /* !_WIN32 */
}

int
TXmapOsErrorToErrno(osError)
TXERRTYPE       osError;        /* TXgeterror() value */
/* Returns Unix errno value for `osError' (TXgeterror() OS error number).
 * WTF would like to just use MSVC _dosmaperr(), but cannot link it?
 */
{
#ifdef _WIN32
  struct errentry
  {
    unsigned long       oscode;                 /* OS return value */
    int                 errnocode;              /* System V error code */
  };
  static CONST struct errentry errtable[] =
    {
      { ERROR_INVALID_FUNCTION,       EINVAL    },  /* 1 */
      { ERROR_FILE_NOT_FOUND,         ENOENT    },  /* 2 */
      { ERROR_PATH_NOT_FOUND,         ENOENT    },  /* 3 */
      { ERROR_TOO_MANY_OPEN_FILES,    EMFILE    },  /* 4 */
      { ERROR_ACCESS_DENIED,          EACCES    },  /* 5 */
      { ERROR_INVALID_HANDLE,         EBADF     },  /* 6 */
      { ERROR_ARENA_TRASHED,          ENOMEM    },  /* 7 */
      { ERROR_NOT_ENOUGH_MEMORY,      ENOMEM    },  /* 8 */
      { ERROR_INVALID_BLOCK,          ENOMEM    },  /* 9 */
      { ERROR_BAD_ENVIRONMENT,        E2BIG     },  /* 10 */
      { ERROR_BAD_FORMAT,             ENOEXEC   },  /* 11 */
      { ERROR_INVALID_ACCESS,         EINVAL    },  /* 12 */
      { ERROR_INVALID_DATA,           EINVAL    },  /* 13 */
      { ERROR_INVALID_DRIVE,          ENOENT    },  /* 15 */
      { ERROR_CURRENT_DIRECTORY,      EACCES    },  /* 16 */
      { ERROR_NOT_SAME_DEVICE,        EXDEV     },  /* 17 */
      { ERROR_NO_MORE_FILES,          ENOENT    },  /* 18 */
      { ERROR_LOCK_VIOLATION,         EACCES    },  /* 33 */
      { ERROR_BAD_NETPATH,            ENOENT    },  /* 53 */
      { ERROR_NETWORK_ACCESS_DENIED,  EACCES    },  /* 65 */
      { ERROR_BAD_NET_NAME,           ENOENT    },  /* 67 */
      { ERROR_FILE_EXISTS,            EEXIST    },  /* 80 */
      { ERROR_CANNOT_MAKE,            EACCES    },  /* 82 */
      { ERROR_FAIL_I24,               EACCES    },  /* 83 */
      { ERROR_INVALID_PARAMETER,      EINVAL    },  /* 87 */
      { ERROR_NO_PROC_SLOTS,          EAGAIN    },  /* 89 */
      { ERROR_DRIVE_LOCKED,           EACCES    },  /* 108 */
      { ERROR_BROKEN_PIPE,            EPIPE     },  /* 109 */
      { ERROR_DISK_FULL,              ENOSPC    },  /* 112 */
      { ERROR_INVALID_TARGET_HANDLE,  EBADF     },  /* 114 */
      { ERROR_INVALID_HANDLE,         EINVAL    },  /* 124 */
      { ERROR_WAIT_NO_CHILDREN,       ECHILD    },  /* 128 */
      { ERROR_CHILD_NOT_COMPLETE,     ECHILD    },  /* 129 */
      { ERROR_DIRECT_ACCESS_HANDLE,   EBADF     },  /* 130 */
      { ERROR_NEGATIVE_SEEK,          EINVAL    },  /* 131 */
      { ERROR_SEEK_ON_DEVICE,         EACCES    },  /* 132 */
      { ERROR_DIR_NOT_EMPTY,          ENOTEMPTY },  /* 145 */
      { ERROR_NOT_LOCKED,             EACCES    },  /* 158 */
      { ERROR_BAD_PATHNAME,           ENOENT    },  /* 161 */
      { ERROR_MAX_THRDS_REACHED,      EAGAIN    },  /* 164 */
      { ERROR_LOCK_FAILED,            EACCES    },  /* 167 */
      { ERROR_ALREADY_EXISTS,         EEXIST    },  /* 183 */
      { ERROR_FILENAME_EXCED_RANGE,   ENOENT    },  /* 206 */
      { ERROR_NESTING_NOT_ALLOWED,    EAGAIN    },  /* 215 */
      { ERROR_NOT_ENOUGH_QUOTA,       ENOMEM    }    /* 1816 */
    };
#  define ERRTABLESIZE (sizeof(errtable)/sizeof(errtable[0]))
/* The following two constants must be the minimum and maximum
   values in the (contiguous) range of Exec Failure errors. */
#  define MIN_EXEC_ERROR ERROR_INVALID_STARTING_CODESEG
#  define MAX_EXEC_ERROR ERROR_INFLOOP_IN_RELOC_CHAIN
/* These are the low and high value in the range of errors that are
   access violations */
#  define MIN_EACCES_RANGE ERROR_WRITE_PROTECT
#  define MAX_EACCES_RANGE ERROR_SHARING_BUFFER_EXCEEDED
  int   ret, i;

  /* check the table for the OS error code */
  for (i = 0; i < ERRTABLESIZE; ++i)
    {
      if (osError == errtable[i].oscode)
        {
          ret = errtable[i].errnocode;
          goto done;
        }
    }

  /* The error code wasn't in the table.  We check for a range of */
  /* EACCES errors or exec failure errors (ENOEXEC).  Otherwise   */
  /* EINVAL is returned.                                          */

  if (osError >= MIN_EACCES_RANGE && osError <= MAX_EACCES_RANGE)
    ret = EACCES;
  else if (osError >= MIN_EXEC_ERROR && osError <= MAX_EXEC_ERROR)
    ret = ENOEXEC;
  else
    ret = EINVAL;
done:
  return(ret);
#  undef ERRTABLESIZE
#  undef MIN_EXEC_ERROR
#  undef MAX_EXEC_ERROR
#  undef MIN_EACCES_RANGE
#  undef MAX_EACCES_RANGE
#else /* !_WIN32 */
  /* Under Unix, OS error is errno: */
  return(osError);
#endif /* !_WIN32 */
}

int
TXaccess(path, mode)
CONST char      *path;  /* (in) dir or file to check */
int             mode;   /* (in) R_OK, W_OK, X_OK, F_OK */
/* Checks for `mode' permissions on `path'.
 * More reliable than stat(): in particular, under Windows if `path' is a dir
 * and we have write but not read access via ACLs, stat() shows mode 0777,
 * implying we could create a file in `path' and then delete it, but
 * in reality we cannot; can happen to system (instead of user) temp dir.
 * WTF Unix version should use vstat.c:chk_access() to use effective uid
 * as a later open() etc. would; we use access() here which uses real uid.
 * Returns 0 if all access rights in `mode' are granted, -1 if not,
 * -1 on error.  Sets errno.
 */
{
#ifdef _WIN32
  static CONST char     fn[] = "TXaccess";
  HANDLE                hToken = NULL;
  HANDLE                hImpersonationToken = NULL;
  PACL                  pDacl = NULL;
  SECURITY_INFORMATION  secRequested = (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION);
  byte                  tmpSecDescBuf[512];
  SECURITY_DESCRIPTOR   *secDesc = (SECURITY_DESCRIPTOR *)tmpSecDescBuf;
  GENERIC_MAPPING       genericMapping;
  PRIVILEGE_SET         privSet;
  DWORD                 privSetLen, desiredGenericAccess, grantedAccess;
  DWORD                 lenNeeded, secDescLen = (DWORD)sizeof(tmpSecDescBuf);
  BOOL                  fReturn = FALSE;
  TXERRTYPE             osError;
  int                   ret, errnoVal;

  /* http://blog.aaronballman.com/2011/08/how-to-check-access-rights/
   * has some info on MapGenericMask() etc. and these calls:
   */

  lenNeeded = 0;
  if (!GetFileSecurity(path, secRequested, secDesc, secDescLen, &lenNeeded))
    {
      if (TXgeterror() != ERROR_INSUFFICIENT_BUFFER) goto err;
      /* Re-alloc `secDesc' to needed size: */
      secDesc = (SECURITY_DESCRIPTOR *)TXmalloc(TXPMBUFPN, fn, lenNeeded);
      if (!secDesc) goto err;
      secDescLen = lenNeeded;
      lenNeeded = 0;
      if (!GetFileSecurity(path, secRequested, secDesc, secDescLen,
                           &lenNeeded))
        goto err;
    }

  if (mode == F_OK) goto ok;                    /* F_OK: no further checks */

  if (!OpenProcessToken(GetCurrentProcess(),
   (TOKEN_IMPERSONATE | TOKEN_QUERY | TOKEN_DUPLICATE | STANDARD_RIGHTS_READ),
                        &hToken))
    goto err;

  if (!DuplicateToken(hToken, SecurityImpersonation, &hImpersonationToken))
    goto err;

  memset(&genericMapping, 0, sizeof(GENERIC_MAPPING));
  genericMapping.GenericRead = FILE_GENERIC_READ;
  genericMapping.GenericWrite = FILE_GENERIC_WRITE;
  genericMapping.GenericExecute = FILE_GENERIC_EXECUTE;
  genericMapping.GenericAll = FILE_ALL_ACCESS;
  desiredGenericAccess =
    ((mode & R_OK) ? GENERIC_READ : 0) |
    ((mode & W_OK) ? GENERIC_WRITE : 0) |
    ((mode & X_OK) ? GENERIC_EXECUTE : 0);
  MapGenericMask(&desiredGenericAccess, &genericMapping);
  memset(&privSet, 0, sizeof(PRIVILEGE_SET));
  privSetLen = sizeof(PRIVILEGE_SET);
  if (!AccessCheck(secDesc,                     /* in */
                   hImpersonationToken,         /* in */
                   desiredGenericAccess,        /* in   rights we want */
                   &genericMapping,             /* in   what "" mapped to? */
                   &privSet,                    /* out */
                   &privSetLen,                 /* in   sizeof(privSet) */
                   &grantedAccess,              /* out */
                   &fReturn))
    goto err;
  if (!fReturn) goto err;
ok:
  ret = 0;                                      /* success */
  goto done;

err:
  ret = -1;
done:
  /* Get error numbers before cleanup, to preserve them: */
  osError = (ret == 0 ? ERROR_SUCCESS : TXgeterror());
  errnoVal = (ret == 0 ? 0 : TXmapOsErrorToErrno(osError));
  /* cleanup: */
  if (secDesc && secDesc != (SECURITY_DESCRIPTOR *)tmpSecDescBuf)
    secDesc = TXfree(secDesc);
  secDescLen = 0;
  if (hImpersonationToken)
    {
      CloseHandle(hImpersonationToken);
      hImpersonationToken = NULL;
    }
  if (hToken)
    {
      CloseHandle(hToken);
      hToken = NULL;
    }
  /* Restore errors after cleanup: */
  TXseterror(osError);
  errno = errnoVal;
  return(ret);
#else /* !_WIN32 */
  /* WTF use vstat.c:chk_access() which uses effective uid as a later
   * open() etc. would; access() uses real uid; fix calls to TXaccess()
   * if/when it is changed to use effective uid:
   */
  return(access(path, mode));
#endif /* !_WIN32 */
}

int
TXfilenameIsDevice(filename, onAnyPlatform)
CONST char      *filename;      /* (in) filename (sans dir) */
int             onAnyPlatform;  /* (in) nonzero: is device on any platform */
/* Returns 1 if `filename' is a reserved device filename, 0 if not.
 * If `onAnyPlatform' is nonzero, will check if reserved device filename
 * on any known platform, not just the current one.
 */
{
  static CONST char * CONST     windowsDevices[] =
    {
      "NUL",
      "AUX",
      "CON",
      "PRN",
      "CLOCK$",
      /* COMn and LPTn checked specially */
      CHARPN
    };
  size_t                        i, fsz;

#ifdef _WIN32
  onAnyPlatform = 1;
#endif /* _WIN32 */
  if (onAnyPlatform)
    {
      fsz = strlen(filename);
      /* Ignore trailing `:': */
      if (fsz > 0 && filename[fsz - 1] == ':') fsz--;
      /* Check device list: */
      for (i = 0; windowsDevices[i] != CHARPN; i++)
        if (strnicmp(filename, windowsDevices[i], fsz) == 0 &&
            windowsDevices[i][fsz] == '\0')
          return(1);
      /* Check COM and LPT separately, for COM[1-9] and LPT[1-9]: */
      if (fsz == 4 &&
          (strnicmp(filename, "COM", 3) == 0 ||
           strnicmp(filename, "LPT", 3) == 0) &&
          filename[3] >= '0' &&
          filename[3] <= '9')
        return(1);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */

int
TXgetBacktrace(char *buf, size_t bufSz, int flags)
/* Dumps backtrace to `buf' if possible.  More information (e.g.
 * function names) may be available if executable is linked with -rdynamic.
 * Use addr2line (Linux util) to map addresses to file:line pairs.
 * Signal-safe, unless `flags & 0x1' is nonzero, in which case malloc()
 * may be used, and more info (names vs. hex addresses) may be available.
 * Flags:
 *   0x1  safe to call malloc() (not in signal)
 *   0x2  newline- instead of space-separate items
 *   0x4  print TXgetBacktrace() address iff Windows (for address fixup)
 * Returns 2 if full backtrace printed, 1 if partial, 0 if none.
 */
{
#if defined(EPI_HAVE_BACKTRACE) || defined(_WIN32)
#  define MAX_ADDRS       256
  void  *addrs[MAX_ADDRS];
  int   numAddrs, i, numCompleted = 0, ret;
  char  *d, *e, sepChar = ((flags & 0x2) ? '\n' : ' ');
  char  *symbol;
#  ifdef _WIN32
  static TXATOMINT      inUse = 0;
  HANDLE                process = NULL;
  byte                  symBuf[sizeof(SYMBOL_INFO) + 256];
  SYMBOL_INFO           *sym = (SYMBOL_INFO *)symBuf;

  /* SymFromAddr() is not thread-safe: */
  if (TX_ATOMIC_INC(&inUse) > 0) goto err;

  if (flags & 0x1)
    {
      process = GetCurrentProcess();
      if (!SymInitialize(process, NULL, TRUE))
        {
          CloseHandle(process);
          process = NULL;
        }
    }
#  else /* !_WIN32 */
  char  **symbols = NULL;
#  endif /* !_WIN32 */

  /* We only know that backtrace() is async-safe under gcc: */
#  if !defined(_WIN32) && !defined(__GNUC__)
  if (!(flags & 0x1)) goto err;
#  endif /* !_WIN32 && !__GNUC__ */

  /* KNG 20180601 backtrace() hung in signal handler call during main
   * thread malloc() call under Linux 2.6.32, so backtrace() is
   * malloc-unsafe?
   */
#  ifndef _WIN32
  if (!(flags & 0x1) && TXgetSysMemFuncDepth() > 0) goto err;
#  endif /* !_WIN32 */

  d = buf;
  e = buf + bufSz;
  if (e > d) *d = '\0';
#  ifdef _WIN32
  numAddrs = CaptureStackBackTrace(0, MAX_ADDRS, addrs, NULL);
  if (flags & 0x4)
    {
      /* In addition to backtrace, we print address and name of a
       * function as a reference.  The difference between the
       * texis.map address for this function and the address reported
       * here can be added to addresses in the backtrace when looking
       * up symbols in texis.map manually, since
       * TXgetBacktrace()-reported symbols seem to be inaccuracate
       * under Windows.  Could use any function as reference, but an
       * address near TXgetBacktrace() is likely to be in the
       * backtrace, if that is a factor (should not be):
       */
      if (d < e)
        /* Note: source/mapSymbols.vs looks for `(ref: ...)' msg: */
        d += htsnpf(d, e - d, "(ref: %p=%s) ", TXgetBacktrace, __FUNCTION__);
    }
#  else /* !_WIN32 */
  numAddrs = backtrace(addrs, MAX_ADDRS);
#  endif /* !_WIN32 */
  /* backtrace_symbols() uses malloc(), so only call if `mallocOk': */
#  ifndef _WIN32
  if (flags & 0x1)
    symbols = backtrace_symbols(addrs, numAddrs);
#  endif /* !_WIN32 */
  for (i = 0; i < numAddrs && d < e; i++)
    {
      symbol = NULL;
#  ifdef _WIN32
      if ((flags & 0x1) && process)
        {
          memset(symBuf, 0, sizeof(symBuf));
          sym->SizeOfStruct = sizeof(SYMBOL_INFO);
          sym->MaxNameLen =
            (sizeof(symBuf) - sizeof(SYMBOL_INFO))/sizeof(TCHAR) - 1;
          /* WTF this seems to return wrong symbol: */
          if (SymFromAddr(process, (DWORD64)addrs[i], 0, sym))
            symbol = sym->Name;
        }
#  else /* !_WIN32 */
      if (symbols) symbol = symbols[i];
#  endif /* !_WIN32 */
      if (symbol)
        /* Still print address, in case `symbol' is bogus (e.g. Windows): */
        d += htsnpf(d, e - d, "%p=%s", addrs[i], symbol);
      else
        d += htsnpf(d, e - d, "%p", addrs[i]);
      if (d < e && (sepChar != ' ' || i + 1 < numAddrs)) *(d++) = sepChar;
      if (d < e) numCompleted++;
    }

  ret = (numCompleted == numAddrs ? 2 : (numCompleted > 0 ? 1 : 0));
  if (numAddrs >= MAX_ADDRS)                    /* backtrace too big */
    {
      if (d < e) htsnpf(d, e - d, "...");
      if (d < e && sepChar != ' ') *(d++) = sepChar;
      if (ret == 2) ret = 1;
    }
  if (d >= e)
    {
      if (bufSz >= 4)
        strcpy(e - 4, "...");
      else if (bufSz > 0)
        *buf = '\0';
      if (ret == 2) ret = 1;
    }
  goto finally;

err:
  if (bufSz > 0) *buf = '\0';
  ret = 0;
finally:
#  ifdef _WIN32
  if (process)
    {
      SymCleanup(process);
      CloseHandle(process);
      process = NULL;
    }
  (void)TX_ATOMIC_DEC(&inUse);
#  else /* !_WIN32 */
  if (symbols && (flags & 0x1)) symbols = TXfree(symbols);
#  endif /* !_WIN32 */
  return(ret);
#  undef MAX_ADDRS
#else /* !EPI_HAVE_BACKTRACE && !_WIN32 */
  if (bufSz > 0) *buf = '\0';
  return(0);
#endif /* !EPI_HAVE_BACKTRACE && !_WIN32 */
}

/* ------------------------------------------------------------------------ */

#if defined(EPI_HAVE_QSORT_R)
/* TXqsort_r() is a macro for qsort_r() */
#else /* !EPI_HAVE_QSORT_R */
#  ifdef EPI_HAVE_QSORT_S
typedef struct TXqsort_sUsr_tag
{
  TXqsort_rCmpFunc      *cmp;
  void                  *usr;
}
TXqsort_sUsr;

static int
TXqsort_sCmp(void *usr, const void *a, const void *b)
/* qsort_s() compare function: wrapper for our qsort_r()-compatible
 * compare func.
 */
{
  TXqsort_sUsr  *data = (TXqsort_sUsr *)usr;

  return(data->cmp(a, b, data->usr));
}
#  else /* !EPI_HAVE_QSORT_S */
#    include "qsort.c"
#  endif /* !EPI_HAVE_QSORT_S */

void
TXqsort_r(void *base, size_t numEl, size_t elSz, TXqsort_rCmpFunc *cmp,
          void *usr)
{
#  ifdef EPI_HAVE_QSORT_S
  TXqsort_sUsr  data;

  /* We can use re-entrant qsort_s(); only difference is arg order to
   * compare function, which is handled via TXqsort_sCmp() wrapper:
   */
  memset(&data, 0, sizeof(TXqsort_sUsr));
  data.cmp = cmp;
  data.usr = usr;
  qsort_s(base, numEl, elSz, TXqsort_sCmp, &data);
#  else /* !EPI_HAVE_QSORT_S */
  /* use our qsort_r() implementation #include'd above: */
  _quicksort(base, numEl, elSz, cmp, usr);
#  endif /* !EPI_HAVE_QSORT_S */
}
#endif /* !EPI_HAVE_QSORT_R */

/******************************************************************/

#ifdef TEST

#if 0
static void cb ARGS((void *usr));
static void
cb(usr)
void	*usr;
{
	printf(" cb %ld", (long)usr);
	fflush(stdout);
	if ((long)usr > 0L && (long)usr < 10L)
		TXsetalarm(cb, (void *)((EPI_VOIDPTR_INT)usr - 1L),
                           (double)0.5, TXbool_True);
}
#endif

/* ------------------------------------------------------------------------- */

static const char       CommaWhitespace[] = ", \t\r\n\v\f";

#endif /* TEST */

#if defined(TEST) || defined(REQUIREDTESTS)

#  ifdef TEST
static void
prHms(double tim)
{
  EPI_HUGEINT   h;
  int           m;
  double        s;

  h = ((EPI_HUGEINT)tim)/3600;
  tim -= (double)(h*3600);
  m = ((int)tim)/60;
  tim -= (double)(m*60);
  s = tim;

  htpf("%02wd:%02d:%06.3lf", h, m, s);
}
#  endif /* TEST */

int main ARGS((int argc, char *argv[]));
int
main(argc, argv)
int argc;
char *argv[];
{
#  ifdef REQUIREDTESTS
  /* Do any required-behavior tests here; exit non-zero to fail monitor
   * build (see source/texis/Makefile).
   */
  (void)argc;
  (void)argv;
  return(0);                                    /* success */
#  endif /* REQUIREDTESTS */
#  ifdef TEST
  {
    TXwatchPath *watchPath;
    byte        changes[TXwatchPathChange_NUM];
    int         res;

    if (argc == 4 && strcmp(argv[1], "touch") == 0)
      {                                 /* touch <filename> <N-times> */
        int             fd, i;
        const char      *f = argv[2];

        i = atoi(argv[3]);
        printf("touching `%s' %d times...", f, i);
        fflush(stdout);
        for ( ; i > 0; i--)
          {
            fd = open(f, (O_WRONLY|O_CREAT), 0666);
            if (fd < 0)
              {
                putmsg(MERR + FOE, CHARPN, "cannot open `%s': %s",
                       f, TXstrerror(TXgeterror()));
                exit(1);
              }
            tx_rawwrite(NULL, fd, f, (byte *)"TesT", 4);
            close(fd);
          }
        printf("done\n");
        exit(0);
      }

    if (strcmp(argv[1], "watch") == 0)
      {                       /* watch <path> [<watchFlags> [<numEvents>]] */
        int     numEvents = EPI_OS_INT_MAX;
        char    *tok;
        size_t  tokLen;

        if (argc < 3 || argc > 5)
          {
            fprintf(stderr, "Usage: %s <path> [<watchFlags> [<numEvents>]]\n",
                    argv[0]);
            exit(TXEXIT_INCORRECTUSAGE);
          }
        memset(changes, 1, sizeof(changes));
        if (argc >= 4)                          /* has <watchFlags> */
          {
            TXwatchPathChange   chg;

            memset(changes, 0, sizeof(changes));
            for (tok = argv[3]; *tok; tok += tokLen)
              {
                tok += strspn(tok, CommaWhitespace);
                tokLen = strcspn(tok, CommaWhitespace);
                chg = TXwatchPathChangeStrToEnum(tok, tokLen);
                if (chg)
                  changes[chg] = 1;
                else if (tokLen == 3 && strnicmp(tok, "all", tokLen) == 0)
                  memset(changes, 1, sizeof(changes));
                else
                  fprintf(stderr, "Unknown TXwatchPathChange token `%.*s'\n",
                          (int)tokLen, tok);
              }
          }
        if (argc>= 5) numEvents = atoi(argv[4]);        /* has <numEvents> */
        watchPath = TXwatchPathOpen(TXPMBUFPN, argv[2],
                                    changes,
#ifdef _WIN32
                                    TXwatchPathOpenFlag_SubTree
#else /* !_WIN32 */
                                    TXwatchPathOpenFlag_None
#endif /* !_WIN32 */
                                    );
        if (!watchPath) exit(1);
        while (numEvents > 0)
          {
            int                 gotEvent = 0;
            TXwatchPathEvent    *event = NULL;
#ifdef _WIN32
            DWORD       r = 0;
#endif /* _WIN32 */
            printf("waiting...");
            fflush(stdout);
#ifdef _WIN32
            {
              TXFHANDLE waitHandle;
              const char        *handleNames[1];

              handleNames[0] = "watchPathWait";
              waitHandle = TXwatchPathGetWaitableFhandle(watchPath);
              while ((r = TXezWaitForMultipleObjects(TXPMBUFPN,
                                        TX_TRACEPIPE_TO_TRACESKT(TxTracePipe),
                                        __FUNCTION__, &waitHandle, 1,
                                        handleNames, -1.0, TXbool_True)) ==
                     WAIT_IO_COMPLETION);
            }
#else
            ezswaitread(TXwatchPathGetWaitableFhandle(watchPath), -1);
#endif
            res = -1;
            while (numEvents-- > 0 &&
                   (res = TXwatchPathGetEvent(watchPath, &event)) > 0)
              {
                char            flagsBuf[1024], *d, *e;
                TXwatchPathFlag flag, flags;

                gotEvent = 1;
                flags = TXwatchPathEventGetFlags(event);
                e = (d = flagsBuf) + sizeof(flagsBuf);
                *d = '\0';
                for (flag = (TXwatchPathFlag)1;
                     flag < (TXwatchPathFlag)(1 << TXwatchPathFlagIter_NUM);
                     flag <<= 1)
                  if ((flags & flag) && d < e)
                    d += htsnpf(d, e - d, " %s",
                                TXwatchPathFlagEnumToStr(flag));
                printf("got %s event for path `%s' with flags:%s\n",
                 TXwatchPathChangeEnumToStr(TXwatchPathEventGetChange(event)),
                       TXwatchPathEventGetPath(event), flagsBuf);
                event = TXwatchPathEventClose(event);
              }
            if (numEvents <= 0)
              printf("done\n");
            else if (res < 0)
              {
                if (!gotEvent) printf("no event\n");
              }
            else if (res == 0)
              printf("get-event error\n");
            fflush(stdout);
          }
        watchPath = TXwatchPathClose(watchPath);
        exit(0);
      }
  }

    {
        size_t memsz, pgsz, mem[2];
        char    *s = ".";
	float ave[3];
        int   averet, cols, rows, n;
	double  sec, when;
	time_t	t, tim;
        TXSHMINFO *si;
        EPI_UINT32      hi, lo;

	TXinitapp(TXPMBUFPN, argc, argv, INTPN, CHARPPPN);
        memsz = TXphysmem();
        pgsz = TXpagesize();
        averet = TXloadavg(ave);
	sec = TXgettimeofday();
        tim = time(NULL);

        {
          TXdiskSpace   info;

          if (TXgetDiskSpace(s, &info))
            {
              htpf("%s  %kwdK avail  %kwdK free  %kwdK total  %1.0lf%% used\n",
                   s, (EPI_HUGEINT)(info.availableBytes >> 10),
                   (EPI_HUGEINT)(info.freeBytes >> 10),
                   (EPI_HUGEINT)(info.totalBytes >> 10),
                   (double)info.usedPercent);
            }
          else
            printf("*** Cannot determine free space on %s ***\n", s);
        }

        {
          PID_T         pid = TXgetpid(1), *pids;
          size_t        numPids;
          TXprocInfo    *info;
          int           i, val, errnum;
          char          *e;

          for (i = 1; i < argc; i++)
            if (strcmpi(argv[i], "pid") == 0 &&
                i + 1 < argc &&
                (val = TXstrtoi(argv[i+1], NULL, &e, 0, &errnum),
                 errnum == 0))
              pid = (PID_T)val;
          info = TXprocInfoByPid(TXPMBUFPN, NULL, pid, NULL, 0);
          if (info)
            {
              htpf("----------\n");
              htpf("PID: %u  PPID: %u\n", (unsigned)info->pid,
                   (unsigned)info->parentPid);
              htpf("Args:");
              for (i = 0; i < info->argc; i++)
                htpf(" [%s]", info->argv[i]);
              htpf("\nCmd:  %s\n", info->cmd);
              htpf("Exe:  %s\n", info->exePath);
              htpf("UIDs: real: %d effective: %d saved: %d filesystem: %d\n",
                   (int)info->uidReal, (int)info->uidEffective,
                   (int)info->uidSaved, (int)info->uidFileSystem);
              htpf("GIDs: real: %d effective: %d saved: %d filesystem: %d\n",
                   (int)info->gidReal, (int)info->gidEffective,
                   (int)info->gidSaved, (int)info->gidFileSystem);
              htpf("User SID: %s\n", info->sidUser);
              htpf("VSZ:  %kwdK (%kwdK peak)\n",
                   (EPI_HUGEINT)(info->vsz >> 10),
                   (EPI_HUGEINT)(info->vszPeak >> 10));
              htpf("RSS:  %kwdK (%kwdK peak)\n",
                   (EPI_HUGEINT)(info->rss >> 10),
                   (EPI_HUGEINT)(info->rssPeak >> 10));
              htpf("State: %s\n", info->state);
              htpf("Started: %at\n", "%Y-%m-%d %H:%M:%S",
                   (time_t)info->startTime);
              htpf("Time: ");
              prHms(info->userTime);
              htpf(" user + ");
              prHms(info->systemTime);
              htpf(" system = ");
              prHms(info->userTime + info->systemTime);
              htpf(" total\n");
              info = TXprocInfoClose(info);
            }
          else
            htpf("*** Cannot get info about PID %u ***\n", (unsigned)pid);
          numPids = TXprocInfoListPids(TXPMBUFPN, &pids, NULL);
          htpf("%kd PIDs:", (int)numPids);
          for (i = 0; i < (int)numPids; i++)
            htpf(" %u", (unsigned)pids[i]);
          htpf("\n");
        }

        {
          double        *cpuTimes;
          size_t        numCpus, i;

          htpf("----------\n");
          htpf("CPU times:");
          numCpus = TXgetSystemCpuTimes(TXPMBUFPN, &cpuTimes);
          for (i = 0; i < numCpus; i++)
            htpf(" %1.3klfs", cpuTimes[i]);
          htpf("\n");
          htpf("System boot: %at\n", "%Y-%m-%d %H:%M:%S",
               (time_t)TXgetSystemBootTime(TXPMBUFPN));
        }

        htpf("----------\n");
        if (memsz)
          printf("%lu MB of physical memory\n", (unsigned long)memsz);
        else
          printf("*** Cannot determine physical memory ***\n");
        if (pgsz) printf("%lu B page size\n", (unsigned long)pgsz);
        else printf("*** Cannot determine page size ***\n");
        if (averet)
          printf("load average: %.2f %.2f %.2f\n", ave[0], ave[1], ave[2]);
        else
          printf("*** Cannot determine load average ***\n");
	t = (time_t)sec;
        if (sec != (double)(-1.0))
          {
            printf("Time of day: %1.6lf sec = %s", sec, ctime(&t));
            printf("time():      %1lu        sec = %s",(ulong)tim,ctime(&tim));
            if ((int)(tim - t) < -1 || (int)(tim - t) > 1)
              printf("*** TXgettimeofday() differs from time() ***\n");
            if (!TXtime_t2filetime(sec, &lo, &hi))
              printf("*** TXtime_t2filetime() failed ***\n");
            printf("filetime:    0x%lx/0x%lx = %lu/%lu\n",
                   (long)lo, (long)hi, (long)lo, (long)hi);
            when = TXfiletime2time_t(lo, hi);
            printf(" and back:   %1.6lf\n", when);
            if (sec - when > (double)0.1 || when - sec > (double)0.1)
              printf("*** TXfiletime2time_t() wrong? ***\n");
          }
        else
          printf("*** Cannot get time of day ***\n");
        memset(malloc(1000000), 1, 1000000);    /* alloc and touch some mem */
        if (TXgetmeminfo(mem))
          htpf("VSZ = %10kwu %6kwuK  RSS = %10kwu %6kwuK\n",
                 (EPI_HUGEUINT)mem[0], ((EPI_HUGEUINT)mem[0] >> 10),
                 (EPI_HUGEUINT)mem[1], ((EPI_HUGEUINT)mem[1] >> 10));
        else
          printf("*** Cannot determine process mem (VSZ/RSS) ***\n");

        printf("----- Allocating 10 MB touched + 15 MB untouched mem -----\n");
        memset(malloc(10*1048576), 'x', 10*1048576);
        malloc(15*1048576);
        if (TXgetmeminfo(mem))
          htpf("VSZ = %10kwu %6kwuK  RSS = %10kwu %6kwuK\n",
                 (EPI_HUGEUINT)mem[0], ((EPI_HUGEUINT)mem[0] >> 10),
                 (EPI_HUGEUINT)mem[1], ((EPI_HUGEUINT)mem[1] >> 10));
        else
          printf("*** Cannot determine process mem (VSZ/RSS) ***\n");

        if ((si = TXgetshminfo(0xdbaccee5)) != TXSHMINFOPN)
          {
            printf("Shmem: key 0x%x id %d nattach %d size %ld perms:%s%s%s\n",
                   si->key, si->id, si->nattach, (long)si->sz,
                   ((si->perms & TXSM_DEST) ? " dest" : ""),
                   ((si->perms & TXSM_LOCKED) ? " lock" : ""),
                   ((si->perms & TXSM_INIT) ? " init" : ""));
          }
        else
          printf("*** Cannot get license segment info ***\n");
        if (TXgetwinsize(&cols, &rows))
          printf("Window size: %dx%d\n", cols, rows);
        else
          printf("*** Cannot get window size ***\n");

        if ((n = TXgetmaxdescriptors()) >= 0)
          printf("max descriptors:  %d\n", n);
        else
          printf("*** Cannot get max descriptors ***\n");
        if ((n = TXgetopendescriptors()) >= 0)
          printf("open descriptors: %d\n", n);
        else
          printf("*** Cannot get open descriptors ***\n");
        printf("opening file and socket...");
        if (open("/dev/null", O_RDONLY, 0666) < 0)
          printf("file failed...");
        if (socket(AF_INET, SOCK_STREAM, 0) < 0)
          printf("skt failed...");
        printf("done\n");
        if ((n = TXgetopendescriptors()) >= 0)
          printf("open descriptors: %d\n", n);
        else
          printf("*** Cannot get open descriptors ***\n");

	{
		PID_T	pids[10];
		size_t	i, numPids;
#ifdef _WIN32
#  define MORPH3_DIR    "c:\\morph3"
#  define SUFFIX	"testdb" PATH_SEP_S TEXISFNSYSTABLES TX_TABLE_FILE_EXT
#else
#  define MORPH3_DIR    "/usr/local/morph3"
#  define SUFFIX	".dbmonlck"
#endif
		static const char       path[] =
			MORPH3_DIR PATH_SEP_S "texis" PATH_SEP_S SUFFIX;
		numPids = TXfuser(pids, 10, path);
		if (numPids == (size_t)(-1))
			printf("* TXfuser(%s) failed *\n", path);
		else
		{
			printf("%d users of %s:", (int)numPids, path);
			for (i = 0; i < numPids; i++)
				printf(" %u", (unsigned)pids[i]);
			printf("\n");
		}
	}
    }

#if 0
	printf("pid = %d\n", getpid());
	TXsetalarm(cb, (void *)5L, (double)0.5, TXbool_False);
	TXsetalarm(cb, (void *)10L, (double)2.0, TXbool_False);
	TXsetalarm(cb, (void *)11L, (double)13.0, TXbool_False);

	printf("sleeping...");
        fflush(stdout);
	TXsleepmsec(10*1000, 1);
	if (TXunsetalarm(cb, (void *)11L, &when))
		printf(" left: at +%1.1lf\n", when - sec);
	else
		printf(" *** Cannot unset alarm ***\n");
	printf("sleeping again...");
        fflush(stdout);
	TXsleepmsec(5*1000, 1);
	printf(" done\n");
        fflush(stdout);
#endif

#ifdef __alpha
        {
           int ticks;
           getsysinfo(GSI_CLK_TCK, (caddr_t)&ticks, sizeof(ticks));
           printf("%d ticks per sec\n", ticks);
        }
#endif
        return(0);
#endif  /* TEST */
}
#endif /* TEST || REQUIREDTESTS */
