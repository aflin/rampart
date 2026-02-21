#ifdef __cplusplus
#ifndef LINT_ARGS
#  define LINT_ARGS
#endif
extern "C" {
#endif
#ifndef OS_H        /* this should be included AFTER STANDARD headers */
#define OS_H                              /* and BEFORE LOCAL headers */


/* Make sure we have O_CLOEXEC for use below: */
#include <fcntl.h>

#include "txcoreconfig.h"                           /*should already be included*/
#include <sys/types.h>
#include <signal.h>
#ifdef HAVE_WINDOWS_H
#define _CRT_SECURE_NO_WARNINGS
#  include <windows.h>
#endif
#include "txpmbuf.h"
#include "txtypes.h"
/**********************************************************************/
#if defined(_AIX) && !defined(unix)                   /* MAW 07-08-92 */
#  define unix
#endif
#if defined(__unix) && !defined(unix)
#  define unix
#endif
#if defined(__unix__) && !defined(unix)
#  define unix
#endif
#if defined(__MACH__) && !defined(unix)     /* MAW 03-13-02 - mac osx */
#  define unix
#endif
#if defined(__hpux) && !defined(hpux)
#  define hpux __hpux
#endif
#if (defined(__hpux) || defined(hpux)) && !defined(unix)
#  define unix
#endif
#ifdef unix                /* sort out what version of Unix we are on */
#  if defined(hpux) || defined(iAPX286) || defined(sysv) || defined(i386) || defined(__x86_64) || defined(tahoe) || defined(DGUX) || defined(plexus) || defined(__DCC__) || defined(u3b2) || defined(_AIX) || defined(__sgi) || defined(__osf__) || defined(__SVR4) || defined(__ia64) || defined(__PPC__) || defined(__s390__)
#     if defined(__bsdi__) || defined(__FreeBSD__)    /* i386 flavors */
#        ifndef bsd
#           define bsd
#        endif
#     else
#        ifndef sysv
#           define sysv
#        endif
#     endif
#  else                                                       /* !bsd */
#     ifndef bsd
#        define bsd
#     endif
#  endif
#  if defined(sun) && defined(__SVR4)
#     define solaris
#  endif
#endif                                                        /* unix */
/**********************************************************************/
#if defined(__MSDOS__) && !defined(MSDOS)
#  define MSDOS
#endif
#if defined(THINK_C) && !defined(macintosh)
#  define macintosh
#endif

#if defined(__STDC__) && !defined(__stdc__)
#  define __stdc__
#endif
#if defined(__ANSI_CPP__) && !defined(__stdc__)      /* MAW 12-16-92 */
#  define __stdc__
#endif
#if defined(MSDOS) && !defined(LINT_ARGS)
#  define LINT_ARGS
#endif
#if defined(_WIN32) && !defined(LINT_ARGS)          /* MAW 10-18-93 */
#  define LINT_ARGS
#endif
#if defined(macintosh) && !defined(LINT_ARGS)
#  define LINT_ARGS
#endif
#if defined(__GNUC__) && !defined(LINT_ARGS)
#  define LINT_ARGS
#endif
#if defined(__stdc__) && !defined(LINT_ARGS)
#  define LINT_ARGS
#endif

#ifdef __TSC__                      /* topspeed c 3.01 - MAW 07-16-91 */
#  ifndef LINT_ARGS
#     define LINT_ARGS
#  endif
#  ifndef MSDOS
#     define MSDOS
#  endif
#endif
/**********************************************************************/
/* __DCC__ is the compiler for the 88Open machine
** not really a BSD file system, but has readdir() etc.
** MAW 04-05-91 */

#if defined(bsd) || defined(gould) || defined(hpux) || defined(tahoe) || defined(DGUX) || defined(__DCC__) || defined(__stdc__) || defined(_AIX) || defined(__sgi) || defined(i386) || defined(__x86_64) || defined(__osf__)
#  ifndef BSDFS
#     define BSDFS                            /* berkeley file system */
#  endif
#  if defined(hpux) || defined(tahoe) || defined(DGUX) || defined(__DCC__) || defined(i386) || defined(__x86_64) || defined(_AIX) || defined(__sgi) || defined(__osf__)
                   /* MAW 10-02-91 - rm'd "|| defined(sun)" for sparc */
                                                   /* bsd/sysv mutant */
#     ifndef BSDSYSVMUT
#        define BSDSYSVMUT
#     endif
#  endif
#endif

#ifndef ARGS
#  ifdef LINT_ARGS                                    /* MAW 01-10-91 */
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif

#ifndef SEEK_SET                                   /* fseek() origins */
#  define SEEK_SET 0
#  define SEEK_CUR 1
#  define SEEK_END 2
#endif

/* JMT 08/10/94 added __DGUX__ which uses stdarg */

#if !defined(EPI_HAVE_STDARG) && !defined(va_dcl)
#  if defined(macintosh) || defined(__MACH__) || defined(__TURBOC__) || defined(__TSC__) || defined(__convex__) || defined(__stdc__) || defined(_WIN32) || defined(_Windows) || defined (__DGUX__) || defined(_ANSI_C_SOURCE)
#     define EPI_HAVE_STDARG
#  endif
#endif

/* KNG 20070406 EPI_HAVE_VA_COPY dependent on EPI_HAVE_STDARG;
 * wtf move both to epi/Makefile:
 */
#undef EPI_HAVE_VA_COPY
#ifdef EPI_HAVE_STDARG
#  ifdef EPI_HAVE_STDARG_VA_COPY
#    define EPI_HAVE_VA_COPY    1
#  endif /* EPI_HAVE_STDARG_VA_COPY */
#else /* !EPI_HAVE_STDARG */
#  ifdef EPI_HAVE_VARARGS_VA_COPY
#    define EPI_HAVE_VA_COPY    1
#  endif /* EPI_HAVE_VARARGS_VA_COPY */
#endif /* !EPI_HAVE_STDARG */

#ifndef CONST
#  if defined(__stdc__) || defined(__STDC__) || defined(__TURBOC__) || defined(__TSC__) || defined(_WIN32) || defined(__cplusplus)
#     define CONST const
#  else
#     define CONST
#  endif
#endif

#ifdef _WIN32
#include <windows.h>
#  if defined(_INTELC32_) || defined(_WIN32)
#     undef NEAR
#     undef FAR
#     define NEAR
#     define FAR
#     ifndef CDECL
#       if defined(_WIN32)
#        define CDECL __cdecl
#       else
#        define CDECL
#       endif
#     endif
#     define VOLATILE volatile
#  else
#     ifndef NEAR
#        define NEAR near
#     endif
#     ifndef FAR
#        if (_MSC_VER >= 800)
#           define FAR
#        else
#           define FAR  far
#        endif
#     endif
#     ifndef CDECL
#        ifdef __TSC__
#           define CDECL
#        else
#           define CDECL _cdecl
#        endif
#     endif
#     define VOLATILE volatile
#     ifdef _WIN32
#        ifndef LPSTR
#           define LPSTR char FAR *
#        endif
#        ifdef __BORLANDC__
/*#           define sprintf wsprintf*/
/*#           define vsprintf wvsprintf*/
#        else
#           if  defined(_MSC_VER) && _MSC_VER>=800     /* MAW 03-17-94 */
#           else
                  /* this way you don't have to use IGNORECASE w/link */
#              define sprintf WSPRINTF
               int FAR CDECL WSPRINTF(LPSTR,LPSTR,...);
#              define vsprintf wvsprintf
#           endif
#        endif
#     endif                                               /* _WIN32 */
#  endif                                                /* _INTELC32_ */
#ifdef _WIN32
#  define trap24()
#  define error24 ((char *)NULL)
#else                                                        /* !_WIN32 */
   extern void trap24 ARGS((void));
   extern char *error24;
#endif                                                       /* !_WIN32 */
#else
#  undef NEAR
#  undef FAR
#  define NEAR
#  define FAR
#  ifndef CDECL
#     define CDECL
#  endif
#ifndef VOLATILE /* JMT 04-17-96 */
#  if defined(__stdc__) || defined(__TURBOC__) || defined(__TSC__) || defined(_WIN32)
#     define VOLATILE volatile
#  else
#     define VOLATILE
#  endif
#endif
#  define trap24()
#  define error24 ((char *)NULL)
#endif /* _WIN32 */

#if defined(_MSC_VER) && _MSC_VER>=1400 /* Visual Studio >= 2005 */
void txInvalidParameterHandler(const WCHAR * expression, const WCHAR * function,
                               const WCHAR * file, unsigned int line, uintptr_t pReserved);
#endif /* Visual Studio >= 2005 */

#if defined(MAXSIG) || defined(__stdc__) || defined(__STDC__) || defined(_ANSI_C_SOURCE) || defined(__cplusplus)/* newer system */
#  undef SIGTYPE
#  define SIGTYPE void
#  undef SIGRETURN                                    /* KNG 960425 */
#  define SIGRETURN return
#  undef SIGARGS
#  define SIGARGS     ARGS((int sig))
#  undef SIGKNRARGS
#  define SIGKNRARGS sig
#  undef SIGKNRDEF
#  define SIGKNRDEF   int sig
#else                                                 /* older system */
#  undef SIGTYPE
#  define SIGTYPE int
#  undef SIGRETURN                                    /* KNG 960425 */
#  define SIGRETURN   return(0)
#  undef SIGARGS
#  define SIGARGS     ARGS((int sig))
#  undef SIGKNRARGS
#  define SIGKNRARGS sig
#  undef SIGKNRDEF
#  define SIGKNRDEF  int sig
#endif /* older system */

/* KNG 20120130 more modern signal handlers where possible: */
#if defined(EPI_HAVE_SA_SIGACTION) && defined(EPI_HAVE_SIGINFO_T)
#  define TX_SIG_HANDLER_GET(sa)        ((sa)->sa_sigaction)
#  ifdef SA_SIGINFO
#    define TX_SIG_HANDLER_SET(sa, handler)     \
   ((sa)->sa_sigaction = (handler), (sa)->sa_flags |= SA_SIGINFO)
#    define TX_SIG_HANDLER_SIGINFO_TYPE siginfo_t
#  else /* !SA_SIGINFO */
#    define TX_SIG_HANDLER_SET(sa, handler)     \
   ((sa)->sa_sigaction = (handler))
     /* If no #include <signal.h> yet, may not have siginfo_t yet even
      * though EPI_HAVE_SIGINFO_T defined; use SA_SIGINFO as test for
      * #include <signal.h> too, and use void * if needed wtf:
      */
#    define TX_SIG_HANDLER_SIGINFO_TYPE void
#  endif /* !SA_SIGINFO */
#  define TX_SIG_HANDLER_ARGS   \
  (int sigNum, TX_SIG_HANDLER_SIGINFO_TYPE *sigInfo, void *context)
#  define TX_SIG_HANDLER_SIGINFO_ARG    sigInfo
#  define TX_SIG_HANDLER_CONTEXT_ARG    context
#else /* !(EPI_HAVE_SA_SIGACTION && EPI_HAVE_SIGINFO_T) */
#  define TX_SIG_HANDLER_ARGS  (int sigNum)
#  ifdef EPI_HAVE_SIGACTION
#    define TX_SIG_HANDLER_GET(sa)      ((sa)->sa_handler)
#    ifdef SA_SIGINFO
#      define TX_SIG_HANDLER_SET(sa, handler)   \
   ((sa)->sa_handler = (handler), (sa)->sa_flags &= ~SA_SIGINFO)
#    else /* !SA_SIGINFO */
#      define TX_SIG_HANDLER_SET(sa, handler)   ((sa)->sa_handler = (handler))
#    endif /* !SA_SIGINFO */
#  else /* !EPI_HAVE_SIGACTION */
     /* must use signal(): */
#    undef TX_SIG_HANDLER_GET
#    undef TX_SIG_HANDLER_SET
#  endif /* !EPI_HAVE_SIGACTION */
#  ifdef _WIN32
#    define TX_SIG_HANDLER_SIGINFO_TYPE struct exception_pointers
#  else /* !_WIN32 */
#    define TX_SIG_HANDLER_SIGINFO_TYPE void
#  endif /* !_WIN32 */
#  define TX_SIG_HANDLER_SIGINFO_ARG    NULL
#  define TX_SIG_HANDLER_CONTEXT_ARG    NULL
#endif /* !(EPI_HAVE_SA_SIGACTION && EPI_HAVE_SIGINFO_T) */

#if defined(SIGCHLD) && !defined(SIGCLD)     /* MAW 07-24-91 - convex */
#  define SIGCLD SIGCHLD
#endif

typedef SIGTYPE (CDECL TX_SIG_HANDLER) TX_SIG_HANDLER_ARGS;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* TXHANDLE:            Placeholder for a Windows HANDLE when there is no
 *                        real Unix equivalent
 * TXFHANDLE:           Native file handle
 * TXfhandleClose(fh)   Closes `fh'; returns nonzero on success
 */
#ifdef _WIN32
typedef HANDLE  TXHANDLE;
typedef HANDLE  TXFHANDLE;
#  define TXFHANDLE_IS_VALID(fh) \
  ((fh) != TXFHANDLE_INVALID_VALUE && (fh) != TXFHANDLE_CREATE_PIPE_VALUE)
#  define TXFHANDLE_INVALID_VALUE       INVALID_HANDLE_VALUE
#  define TXfhandleClose(fh)            CloseHandle(fh)
#  define TXFHANDLE_STDIN               GetStdHandle(STD_INPUT_HANDLE)
#  define TXFHANDLE_STDOUT              GetStdHandle(STD_OUTPUT_HANDLE)
#  define TXFHANDLE_STDERR              GetStdHandle(STD_ERROR_HANDLE)
#  define TXHANDLE_INVALID_VALUE        INVALID_HANDLE_VALUE
#  define TXhandleClose(hn)             CloseHandle(hn)
#else /* !_WIN32 */
typedef void    *TXHANDLE;
typedef int     TXFHANDLE;
#  define TXFHANDLE_IS_VALID(fh)        ((fh) >= 0)
#  define TXFHANDLE_INVALID_VALUE       (-1)
#  define TXfhandleClose(fh)            (close(fh) == 0)
#  define TXFHANDLE_STDIN               STDIN_FILENO
#  define TXFHANDLE_STDOUT              STDOUT_FILENO
#  define TXFHANDLE_STDERR              STDERR_FILENO
#  define TXHANDLE_INVALID_VALUE        NULL
#  define TXhandleClose(hn)             (void)NULL
#endif /* !_WIN32 */
#define TXHANDLE_IS_VALID(hn)   ((hn) != TXHANDLE_INVALID_VALUE)

#define TXHANDLE_NULL                   ((TXHANDLE)NULL)
#define TXFHANDLE_CREATE_PIPE_VALUE     ((TXFHANDLE)(-2))
#define TX_NUM_STDIO_HANDLES    3

/* pipe[2]() indexes: */
typedef enum TXpipeEnd_tag
  {
    TXpipeEnd_Read      = 0,
    TXpipeEnd_Write     = 1,
    TXpipeEnd_NUM
  }
  TXpipeEnd;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef byte                                           /* 8 bit value */
#  define byte unsigned char
#endif

#ifdef bsd
                               /* give BSD a real toupper()/tolower() */
#  ifdef _toupper
#     undef _toupper
#  endif
#  ifdef _tolower
#     undef _tolower
#  endif
#  define _tolower(c) ((c)-'A'+'a')
#  define _toupper(c) ((c)-'a'+'A')
#ifndef __FreeBSD__                  /* MAW 10-03-00 - freebsd has it */
#  ifdef toupper
#     undef toupper
#  endif
#  ifdef tolower
#     undef tolower
#  endif
#  define toupper(c) (islower(c)?_toupper(c):(c))
#  define tolower(c) (isupper(c)?_tolower(c):(c))
#endif                                                 /* __FreeBSD__ */
#  ifndef __STDC__                                    /* MAW 03-25-94 */
      extern int vsprintf();
#  endif
#endif                                                         /* bsd */

/* these are normally in stdlib.h: */
#ifndef EPI_HAVE_SRANDOM_PROTOTYPE
extern void srandom ARGS((unsigned seed));
#endif /* !EPI_HAVE_SRANDOM_PROTOTYPE */
#ifndef EPI_HAVE_RANDOM_PROTOTYPE
extern long random ARGS((void));
#endif /* !EPI_HAVE_RANDOM_PROTOTYPE */

#ifdef EPI_HAVE_SRANDOM
#  define TX_SRANDOM(s) srandom(s)
#else /* !EPI_HAVE_SRANDOM */
#  define TX_SRANDOM(s) srand(s)
#endif /* !EPI_HAVE_SRANDOM */

#ifdef EPI_HAVE_RANDOM
#  define TX_RANDOM()   random()
#else /* !EPI_HAVE_RANDOM */
#  define TX_RANDOM()   rand()
#endif /* !EPI_HAVE_RANDOM */

  /* TX_SIZE_T_VALUE_LESS_THAN_ZERO() can be used when comparing size_t < 0;
   * avoids compiler warning on unsigned-size_t platforms:
   */
#ifdef EPI_OS_SIZE_T_IS_SIGNED
#  define TX_SIZE_T_VALUE_LESS_THAN_ZERO(n)     ((size_t)(n) < (size_t)(0))
#else /* !EPI_OS_SIZE_T_IS_SIGNED */
#  define TX_SIZE_T_VALUE_LESS_THAN_ZERO(n)     0
#endif /* !EPI_OS_SIZE_T_IS_SIGNED */

/* TX_STRINGIZE_VALUE_OF(x): stringize the value of `x', i.e. after
 * macro expansion.  Implemented in two calls: one to expand `x',
 * then one to stringize it:
 */
#define TX_STRINGIZE_ACTUAL(x)          #x
#define TX_STRINGIZE_VALUE_OF(x)        TX_STRINGIZE_ACTUAL(x)

/* - - - - - - - - - - - - - - txstrncpy.c: - - - - - - - - - - - - - - - - */

int TXstrncpy(char *dest, const char *src, size_t n);   /* JMT 99-01-21 */
int TXstrnispacecmp ARGS((CONST char *a, size_t an, CONST char *b, size_t bn,
                          CONST char *whitespace));
size_t TXstrspnBuf(const char *s, const char *e, const char *accept,
                   size_t acceptLen);
size_t TXstrcspnBuf(const char *s, const char *e, const char *reject,
                    size_t rejectLen);
void    TXstrToLowerCase(char *s, size_t sz);
void    TXstrToUpperCase(char *s, size_t sz);
size_t  TXfindStrInList(char **list, size_t listLen, const char *s,
                        size_t sLen, int flags);
#define TX_STR_LIST_VAL(list, listLen, idx, unkStr)     \
 ((unsigned)(idx) >= (unsigned)(listLen) ? (unkStr) : (list)[(unsigned)(idx)])
int TXversioncmp(const char *a, size_t an, const char *b, size_t bn,
                 const char *separators);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int  TXgetLongOptionValue(char ***argv, const char *optName, char **optValue);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Flags for integral TXstrto...() base: */
typedef enum TXstrtointFlag_tag
  {
    TXstrtointFlag_NoLeadZeroOctal      = 0x0100,
    TXstrtointFlag_Base10CommasOk       = 0x0200,
    TXstrtointFlag_ConsumeTrailingSpace = 0x0400,
    TXstrtointFlag_TrailingSourceIsError= 0x0800,
    TXstrtointFlag_MustHaveDigits       = 0x1000,

    TXstrtointFlag_SolelyNumber = (TXstrtointFlag_ConsumeTrailingSpace |
                                   TXstrtointFlag_TrailingSourceIsError |
                                   TXstrtointFlag_MustHaveDigits)
  }
TXstrtointFlag;

#ifdef EPI_HUGEINTPN                                        /* KNG 990315 */
EPI_HUGEUINT TXstrtointtype(const char *s, const char *e, char **ep, int base,
     int typeIsSigned, EPI_HUGEUINT minVal, EPI_HUGEUINT maxVal, int *errnum);
EPI_HUGEINT TXstrtoh ARGS((CONST char *s, CONST char *e, char **ep,
                           int base, int *errnum));
EPI_HUGEUINT TXstrtouh ARGS((CONST char *s, CONST char *e, char **ep,
                             int base, int *errnum));
#endif /* EPI_HUGEINTPN */
#ifdef EPI_OFF_TPN
EPI_OFF_T TXstrtoepioff_t(const char *s, CONST char *e, char **ep,
                          int base, int *errnum);
#endif /* EPI_OFF_TPN */
#ifdef dword
dword TXstrtodw(const char *s, const char *e, char **ep, int base,
                int *errnum);
#endif /* dword */
#ifdef EPI_OS_SHORT_BITS
/* wtf avoid conflict with `word' when included in epipdftext.cpp: */
#  if EPI_OS_SHORT_BITS == 16
unsigned short
#  else /* EPI_OS_SHORT_BITS != 16 */
error need word type;
#  endif /* EPI_OS_SHORT_BITS != 16 */
     TXstrtow ARGS((CONST char *s, CONST char *e, char **ep, int base,
                    int *errnum));
#endif /* EPI_OS_SHORT_BITS */
int TXstrtoi ARGS((CONST char *s, CONST char *e, char **ep, int base,
                   int *errnum));
unsigned TXstrtou(const char *s, const char *e, char **ep, int base,
                  int *errnum);
long TXstrtol ARGS((CONST char *s, CONST char *e, char **ep, int base,
                    int *errnum));
unsigned long TXstrtoul ARGS((CONST char *s, CONST char *e, char **ep,
                              int base, int *errnum));
short TXstrtos ARGS((CONST char *s, CONST char *e, char **ep, int base,
                     int *errnum));
size_t TXstrtosize_t(const char *s, const char *e, char **ep, int base,
                     int *errnum);
#ifdef EPI_INT16PN
EPI_INT16 TXstrtoi16(const char *s, const char *e, char **ep, int base,
                     int *errnum);
EPI_UINT16 TXstrtoui16(const char *s, const char *e, char **ep, int base,
                       int *errnum);
#endif /* EPI_INT16PN */
#ifdef EPI_INT32PN
EPI_INT32 TXstrtoi32 ARGS((CONST char *s, CONST char *e, char **ep,
                           int base, int *errnum));
EPI_UINT32 TXstrtoui32 ARGS((CONST char *s, CONST char *e, char **ep,
                             int base, int *errnum));
#endif /* EPI_INT32PN */
#ifdef EPI_INT64PN
EPI_INT64 TXstrtoi64 ARGS((CONST char *s, CONST char *e, char **ep,
                           int base, int *errnum));
EPI_UINT64 TXstrtoui64 ARGS((CONST char *s, CONST char *e, char **ep,
                             int base, int *errnum));
#endif /* EPI_INT64PN */
double  TXstrtod(const char *s, const char *e, char **endptr, int *errnum);
#ifdef EPI_HAVE_LONG_DOUBLE
long double  TXstrtold(const char *s, const char *e, char **endptr,
                       int *errnum);
#endif /* EPI_HAVE_LONG_DOUBLE */
#ifdef EPI_HUGEFLOATPN
EPI_HUGEFLOAT TXstrtohf(const char *s, const char *e, char **endptr,
                        int *errnum);
#endif /* EPI_HUGEFLOATPN */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef unix
#if !defined(__stdc__) && !defined(__STDC__) && !defined(__cplusplus)
   extern char *strstr ARGS((CONST char *, CONST char *));  /* MAW 12-14-92   KNG 20070301 const for C++ */
#endif
#endif
   extern char *strstri ARGS((CONST char *, CONST char *));        /* MAW 12-14-92 */
#ifdef unix            /* this may need to be moved into big if below */
   extern char *strrev ARGS((char *));                /* MAW 12-01-92 */
#endif

#if defined(EPI_HAVE_STRNICMP)
  /* use system strnicmp() */
#elif defined(EPI_HAVE_STRNCASECMP)
#  define strcmpi strcasecmp
#  define strnicmp strncasecmp
#else
  /* we will implement strnicmp() */
extern int strcmpi  ARGS((CONST char *s1, CONST char *s2));
extern int strnicmp ARGS((CONST char *d1, CONST char *s2, size_t n));
#endif

#ifdef bsd
#ifndef __STDC__  /* JMT 10/06/94 strdup is already in STDC headers */
   extern char *strdup ARGS((CONST char *));
#endif /* __STDC */
#endif
   extern char *ltrim  ARGS((char *));/* MAW 11-08-91 - these modify arguments */
   extern char *rtrim  ARGS((char *));
   extern char *lrtrim ARGS((char *));

#ifdef unix
#  ifdef sysv
#     ifndef getch              /* only use these if not using curses */
         extern int os_savetty    ARGS((void));
         extern int os_restoretty ARGS((void));
         extern int os_cbreak     ARGS((void));
         extern int os_nocbreak   ARGS((void));
         extern int os_echo       ARGS((void));
         extern int os_noecho     ARGS((void));
         extern int os_nodelay    ARGS((int nd));
         extern int os_getch      ARGS((void));
         extern int os_kbhit      ARGS((void));
#        define cbreak()     os_cbreak()
#        define nocbreak()   os_nocbreak()
#        define echo()       os_echo()
#        define noecho()     os_noecho()
#        define nodelay(a,b) os_nodelay(b)
#        define getch()      os_getch()
#        define kbhit()      os_kbhit()
#     endif                                                     /* getch */
#  endif                                                      /* sysv */
#else
#  if !defined(__TSC__) && !defined(__BORLANDC__)
      extern unsigned int sleep  ARGS((unsigned int));
#  endif
#  ifndef AOSVS
#    if !defined(__cplusplus)
      extern FILE        *popen  ARGS((char *,char *));
      extern int          pclose ARGS((FILE *));
#    endif
#  endif
#endif                                                       /* !unix */
#if !defined(bsd) && !defined(_WIN32) && !defined(MVS) && !defined(__stdc__)
   extern int rename ARGS((CONST char *, CONST char *));
#endif
#ifndef __stdc__
#if defined(solaris) || defined(u3b2)
#  define strerror(a) sysmsg(a)
   extern char *memmove ARGS((char *,char *,int));
#endif
#endif

#if defined(iAPX286) || defined(plexus) || defined(u3b2)
#  define size_t unsigned int
#endif                                                     /* iAPX286 */
#ifdef AOSVS
#  define size_t unsigned int
#  define time_t unsigned long
#endif
#ifdef __BORLANDC__                                   /* MAW 12-20-93 */
#  define off_t long
#endif

#ifdef MVS
#  define getpid() 0
#  define isascii(a) 1
#  if MVS_C_REL < 2
#     define strerror(a) (errno=a,perror(NULL))
#  else
#     define _toupper(a) toupper(a)
#     define _tolower(a) tolower(a)
#  endif                                               /* MVS_C_REL<2 */
#  ifndef STAT_H
#  define STAT_H
   /*******************************************************************/
   struct stat {
       unsigned short st_dev;
       unsigned short st_ino;
       unsigned short st_mode;
       short          st_nlink;
       short          st_uid;
       short          st_gid;
       unsigned short st_rdev;
       long           st_size;
       long           st_atime;
       long           st_mtime;
       long           st_ctime;
   };
#  define S_IFMT      0170000            /* file type mask (st_mode)  */
#  define S_IFDIR     0000000            /* directory                 */
#  define S_IFCHR     0000000            /* character special         */
#  define S_IFREG     0100000            /* regular                   */
#  define S_IREAD     0000444            /* read permission           */
#  define S_IWRITE    0000222            /* write permission          */
#  define S_IEXEC     0000111            /* execute/search permission */

   extern int stat ARGS((char *,struct stat *));
   /*******************************************************************/
#  endif                                                    /* STAT_H */

#     if MVS_C_REL < 2
         extern void qsort ARGS((char *,size_t,size_t,int (*)ARGS((char *,char *))));
#     endif
      extern int   unlink  ARGS((char *));
      extern int   access  ARGS((char *,int));
#endif                                                         /* MVS */
#if defined(MVS) || defined(macintosh) || defined(__MACH__) || defined(__convex__) || defined(AOSVS)
#ifndef __GNUC__                                      /* MAW 03-13-02 */
   extern char *tempnam ARGS((char *,CONST char *));
#endif
#endif
/**********************************************************************/
#ifdef THINK_C                         /* MAW 07-20-92 - think c 5.00 */
#  ifndef STAT_H
#  define STAT_H
   /*******************************************************************/
   struct stat {
       unsigned short st_dev;
       unsigned short st_ino;
       unsigned short st_mode;
       short          st_nlink;
       short          st_uid;
       short          st_gid;
       unsigned short st_rdev;
       long           st_size;
       long           st_atime;
       long           st_mtime;
       long           st_ctime;
   };
#  define S_IFMT      0170000            /* file type mask (st_mode)  */
#  define S_IFDIR     0040000            /* directory                 */
#  define S_IFCHR     0000000            /* character special         */
#  define S_IFREG     0100000            /* regular                   */
#  define S_IREAD     0000444            /* read permission           */
#  define S_IWRITE    0000222            /* write permission          */
#  define S_IEXEC     0000111            /* execute/search permission */

   extern int stat  ARGS((char *,struct stat *));
   /*******************************************************************/
#  endif                                                    /* STAT_H */
   extern int   mkdir   ARGS((char *));
   extern int   rmdir   ARGS((char *));
   extern int   access  ARGS((char *,int));
#  define unlink(a) remove(a)
#endif /* THINK_C */
#ifdef macintosh            /* This stuff stays the same JMT 96-03-29 */
   extern char *strlwr  ARGS((char *));
   extern char *strupr  ARGS((char *));
   extern char *strdup  ARGS((char *));
#  define isascii(a) ((a)>=0&&(a)<=127)
#  define _toupper(a) toupper(a)
#  define _tolower(a) tolower(a)
#  if defined(EDOM) && !defined(ENOMEM)
#     define ENOMEM 0
#  endif
#endif                                                   /* macintosh */
/**********************************************************************/
#define QFOPEN(fh,fn,m) ((fh=fopen(fn,m))!=(FILE *)NULL)
#define QCALLOC(x,n,t)  ((x=(t *)calloc(n,sizeof(t)))!=(t *)NULL)

#ifndef uchar
#  define uchar unsigned char
#endif
#ifndef ushort
#  define ushort unsigned short
#endif
#if !defined(uint) && !defined(HAVE_UINT)
   #define uint unsigned int
#endif
#ifndef ulong
#  define ulong unsigned long
#endif

#ifndef CHARPN
#  define CHARPN ((char *)NULL)
#endif
#ifndef UCHARPN
#  define UCHARPN ((uchar *)NULL)
#endif
#ifndef CHARPPN
#  define CHARPPN ((char **)NULL)
#endif
#ifndef UCHARPPN
#  define UCHARPPN ((uchar **)NULL)
#endif
#ifndef CHARPPPN
#  define CHARPPPN      ((char ***)NULL)
#endif
#ifndef FILEPN
#  define FILEPN ((FILE *)NULL)
#endif
#ifndef VOIDPN
#  define VOIDPN ((void *)NULL)
#endif
#ifndef VOIDPPN
#  define VOIDPPN ((void **)NULL)
#endif
#ifndef BYTEPN
#  define BYTEPN ((byte *)NULL)
#endif
#ifndef BYTEPPN
#  define BYTEPPN ((byte **)NULL)
#endif
#ifndef INTPN
#  define INTPN ((int *)NULL)
#endif
#ifndef INTPPN
#  define INTPPN ((int **)NULL)
#endif
#ifndef SHORTPN
#  define SHORTPN       ((short *)NULL)
#endif
#ifndef LONGPN
#  define LONGPN        ((long *)NULL)
#endif
#ifndef WORDPN
#  define WORDPN        ((word *)NULL)
#endif
#ifndef DWORDPN
#  define DWORDPN       ((dword *)NULL)
#endif
#ifndef TIME_TPN
#  define TIME_TPN      ((time_t *)NULL)
#endif
#ifndef SIZE_TPN
#  define SIZE_TPN      ((size_t *)NULL)
#endif

#ifndef MAXINT
#  define MAXINT        ((int)(((unsigned int)(~0)) >> 1))
#endif
#ifndef MAXLONG
#  define MAXLONG       ((long)((unsigned long)(~0L) >> 1))
#endif

  /* Use TX_O_BINARY/TX_O_NOINHERIT as cross-platform flags, rather
   * than surreptitiously defining OS flag O_BINARY/O_NOINHERIT where
   * it is unsupported, which may lead to confusion:
   */
  /* See #include <fcntl.h> above, to ensure we get O_CLOEXEC if possible: */
#ifdef _WIN32
#  define TX_O_BINARY           O_BINARY
#  define TX_O_NOINHERIT        O_NOINHERIT
#else /* !_WIN32 */
#  ifdef O_BINARY                               /* in case some Unix has it */
#    define TX_O_BINARY         O_BINARY
#  else /* !O_BINARY */
  /* NOTE: users of TX_O_BINARY may assume `0' means not supported: */
#    define TX_O_BINARY         0
#  endif /* !O_BINARY */
#  ifdef O_CLOEXEC
#    define TX_O_NOINHERIT      O_CLOEXEC
#  else /* !O_CLOEXEC */
  /* NOTE: users of TX_O_NOINHERIT may assume `0' means not supported;
   * should set FD_CLOEXEC or equivalent in that case:
   */
#    define TX_O_NOINHERIT      0
#  endif /* !O_CLOEXEC */
#endif /* !_WIN32 */

#ifndef STDIN_FILENO
#  define STDIN_FILENO  0
#endif
#ifndef STDOUT_FILENO
#  define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#  define STDERR_FILENO 2
#endif

/* KNG 990426 Replacements for fseek() and ftell() that use off_t not long,
 * for Solaris 64-bit files:
 */
#undef FSEEKO
#undef FTELLO
#if defined(HAVE_FSEEKO) || defined(_LARGEFILE_SOURCE)
#  define FSEEKO                fseeko          /* true off_t version */
#  define FTELLO                ftello
#else
#  define FSEEKO(fh, off, wh)   fseek((fh), (long)(off), (wh))
#  define FTELLO(fh)            ((off_t)ftell(fh))
#endif

#if defined(_WIN32)                    /* KNG 010314 */
#  ifdef S_IREAD
#    ifndef S_IRUSR
#      define S_IRUSR   S_IREAD
#    endif
#    ifndef S_IRGRP
#      define S_IRGRP   S_IREAD
#    endif
#    ifndef S_IROTH
#      define S_IROTH   S_IREAD
#    endif
#  endif /* S_IREAD */
#  ifdef S_IWRITE
#    ifndef S_IWUSR
#      define S_IWUSR   S_IWRITE
#    endif
#    ifndef S_IWGRP
#      define S_IWGRP   S_IWRITE
#    endif
#    ifndef S_IWOTH
#      define S_IWOTH   S_IWRITE
#    endif
#  endif /* S_IWRITE */
#  ifdef S_IEXEC
#    ifndef S_IXUSR
#      define S_IXUSR   S_IEXEC
#    endif
#    ifndef S_IXGRP
#      define S_IXGRP   S_IEXEC
#    endif
#    ifndef S_IXOTH
#      define S_IXOTH   S_IEXEC
#    endif
#  endif /* S_IEXEC */
#endif /* _WIN32 */
#ifdef S_IFMT                   /* included <sys/stat.h>   KNG 010314 */
#  if !defined(S_ISDIR) && defined(S_IFDIR)
#    define S_ISDIR(mode)       (((mode) & S_IFMT) == S_IFDIR)
#  endif
#  if !defined(S_ISCHR) && defined(S_IFCHR)
#    define S_ISCHR(mode)       (((mode) & S_IFMT) == S_IFCHR)
#  endif
#  if !defined(S_ISBLK) && defined(S_IFBLK)
#    define S_ISBLK(mode)       (((mode) & S_IFMT) == S_IFBLK)
#  endif
#  if !defined(S_ISREG) && defined(S_IFREG)
#    define S_ISREG(mode)       (((mode) & S_IFMT) == S_IFREG)
#  endif
#  if !defined(S_ISFIFO) && defined(S_IFIFO)
#    define S_ISFIFO(mode)      (((mode) & S_IFMT) == S_IFIFO)
#  endif
#  if !defined(S_ISLNK) && defined(S_IFLNK)
#    define S_ISLNK(mode)       (((mode) & S_IFMT) == S_IFLNK)
#  endif
#  if !defined(S_ISSOCK) && defined(S_IFSOCK)
#    define S_ISSOCK(mode)      (((mode) & S_IFMT) == S_IFSOCK)
#  endif
#endif /* S_IFMT */

#ifndef PATH_SEP                  /* MAW 02-24-94 - add PATH_SEP defs */
                                      /* also in dirio.h historically */
#  ifdef _WIN32
#     define PATH_SEP   '\\'
#     define PATH_SEP_S "\\"
#     define TX_PATH_SEP_REX_CHAR_CLASS "[\\\\/]" /*bracketed, sans repeat op*/
#     define TX_PATH_SEP_REX_CHAR_CLASS_LEN     5
#     define TX_PATH_SEP_REPLACE_EXPR "\\\\"
#     define TX_PATH_SEP_REPLACE_EXPR_LEN       2
#     define TX_PATH_SEP_CHARS_S        "\\/"
#  elif defined(unix)
#     define PATH_SEP   '/'
#     define PATH_SEP_S "/"
#     define TX_PATH_SEP_REX_CHAR_CLASS "[/]"
#     define TX_PATH_SEP_REX_CHAR_CLASS_LEN     3
#     define TX_PATH_SEP_REPLACE_EXPR "/"
#     define TX_PATH_SEP_REPLACE_EXPR_LEN       1
#     define TX_PATH_SEP_CHARS_S        PATH_SEP_S
#  elif defined(MVS)
#     define PATH_SEP   '.'
#     define PATH_SEP_S "."
#     define TX_PATH_SEP_REX_CHAR_CLASS "[.]"
#     define TX_PATH_SEP_REX_CHAR_CLASS_LEN     3
#     define TX_PATH_SEP_REPLACE_EXPR "."
#     define TX_PATH_SEP_REPLACE_EXPR_LEN       1
#     define TX_PATH_SEP_CHARS_S        PATH_SEP_S
#  elif defined(macintosh)
#     define PATH_SEP   ':'
#     define PATH_SEP_S ":"
#     define TX_PATH_SEP_REX_CHAR_CLASS "[:]"
#     define TX_PATH_SEP_REX_CHAR_CLASS_LEN     3
#     define TX_PATH_SEP_REPLACE_EXPR ":"
#     define TX_PATH_SEP_REPLACE_EXPR_LEN       1
#     define TX_PATH_SEP_CHARS_S        PATH_SEP_S
#  else
#    error Unknown platform: cannot set PATH_SEP etc.
#  endif
#endif                                                    /* PATH_SEP */


/* TX_ISABSPATH(s) is nonzero if string `s' points to an absolute path.
 * TX_ISPATHSEP(c) is nonzero if char `c' is a path (dir) separator char:
 */
#ifdef _WIN32
#  define TX_ISPATHSEP(c)       ((c) == PATH_SEP || (c) == '/')
#  define TX_IS_PATH_OR_DRIVE_SEP(c)    (TX_ISPATHSEP(c) || (c) == ':')
#  define TX_ISDRIVE(s)         (TX_ISALPHA(*(s)) && (s)[1] == ':')
#  define TX_ISABSPATH(s)                       \
  ((TX_ISDRIVE(s) && TX_ISPATHSEP((s)[2])) ||   \
   (TX_ISPATHSEP(*(s)) && TX_ISPATHSEP((s)[1])))
#  define TX_ISABSLIKEPATH(s)   (TX_ISDRIVE(s) || TX_ISPATHSEP(*(s)))
#  define TX_EXE_EXT_STR        ".exe"
#  define TX_EXE_EXT_STR_LEN    4
#else /* !_WIN32 */
#  define TX_ISPATHSEP(c)       ((c) == PATH_SEP)
#  define TX_IS_PATH_OR_DRIVE_SEP(c)    TX_ISPATHSEP(c)
#  define TX_ISDRIVE(s)         0
#  define TX_ISABSPATH(s)       TX_ISPATHSEP(*(s))
#  define TX_ISABSLIKEPATH(s)   TX_ISABSPATH(s)
#  define TX_EXE_EXT_STR        ""
#  define TX_EXE_EXT_STR_LEN    0
#endif /* !_WIN32 */

#ifdef _WIN32
#  define TX_OS_EOL_STR         "\r\n"
#  define TX_OS_EOL_STR_LEN     2
#elif defined(unix)
#  define TX_OS_EOL_STR         "\n"
#  define TX_OS_EOL_STR_LEN     1
#endif

#define TX_BITS_PER_BYTE        EPI_OS_UCHAR_BITS

/**********************************************************************/
extern const char*    sysmsg      ARGS((int));
extern int        FAR fisdir      ARGS((char FAR *fn));
extern int        FAR fexecutable ARGS((char FAR *fn));
extern int        FAR fwritable   ARGS((char FAR *fn));
extern int        FAR freadable   ARGS((char FAR *fn));
extern int        FAR fexists     ARGS((char FAR *fn));
extern int        FAR fcopyperms  ARGS((char FAR *sfn,char FAR *dfn));
extern char FAR * FAR pathcat     ARGS((char *path,char *fn));
extern char FAR * FAR epipathfind    ARGS((char *fn,char *path));
extern char FAR * FAR epipathfindmode ARGS((char *fn, char *path, int mode));
extern FILE FAR * FAR pathopen    ARGS((char *fn,char *mode,char *path));
extern char FAR * FAR proffind    ARGS((char *fn));
extern FILE FAR * FAR profopen    ARGS((char *fn,char *mode));
extern char FAR * FAR fullpath    ARGS((char *buf,char *fn,int buflen));
/**********************************************************************/
#endif                                                        /* OS_H */
#ifndef MACRO_H
#define MACRO_H
#ifdef USE_EPI
/******************************************************************/
/* JMT 01/09/95   Debug IO. */
/* Only read and seek for now */

#ifdef IODEBUG
#  define read(a,b,c)		mac_read(a,b,c,__FILE__,__LINE__)
#  define lseek(a,b,c)		mac_lseek(a,b,c,__FILE__,__LINE__)

int	mac_read(int, void *, unsigned, char *, int);
int	mac_lseek(int, off_t, int, char *, int);
#endif

/**********************************************************************/

extern void       FAR epi_exit     ARGS((int rc));
extern char FAR * FAR epi_strdup   ARGS((char FAR *));
extern char FAR * FAR epi_tempnam  ARGS((char FAR *,char FAR *));
extern char FAR * FAR epi_getcwd   ARGS((char FAR *,int));
extern char FAR * FAR epi_fullpath ARGS((char FAR *,char FAR *,int));

#ifdef EF_AUX_DEBUG                                       /* KNG 010524 */
extern void       FAR *EF_Malloc ARGS((size_t n));
extern void       FAR *EF_Calloc ARGS((size_t n, size_t sz));
extern void       FAR *EF_Realloc ARGS((void *ptr, size_t n));
extern void       FAR *EF_Valloc ARGS((size_t n));
extern void       FAR *EF_Memalign ARGS((size_t alignment, size_t userSize));
extern void            EF_Free ARGS((void *ptr));
extern void       FAR  EF_SetPerms ARGS((void * address, int perms));
#else /* !EF_AUX_DEBUG */
#  define EF_Malloc     malloc
#  define EF_Calloc     calloc
#  define EF_Realloc    realloc
#  define EF_Valloc     valloc
#  define EF_Memalign   memalign
#  define EF_Free       free
#  define EF_SetPerms(address, perms)
#endif /* !EF_AUX_DEBUG */

#ifdef MEMVISUAL
   extern void mac_visual ARGS((void));
#else
#  define mac_visual()
#endif
/**********************************************************************/
#ifdef _WIN32
#if defined(S_IFMT) || defined(_S_IFMT)
   extern int        FAR epi_stat     ARGS((char FAR *,struct stat FAR *));
#else
   extern int        FAR epi_stat     ARGS(());
#endif
#ifndef _WIN32
#define stat(a,b) epi_stat(a,b)
#endif
#ifndef S_IHIDDEN                /* MAW 03-05-93 - extended stat mode */
#  define S_IHIDDEN 0x0800
#endif
extern long       FAR epi_time     ARGS((long *));
/* #define time(a)   epi_time(a) */
#endif /* _WIN32 */

#define bfread(a,b,c,d)  fread(a,b,c,d)
#define bfwrite(a,b,c,d) fwrite(a,b,c,d)
#define bfseek(a,b,c)    fseek(a,b,c)
#define brewind(a,b,c)   rewind(a)
#define bftell(a)        ftell(a)
#define bfflush(a)       fflush(a)

#ifdef LOWIO                                          /* MAW 08-10-92 */
#  ifdef _WIN32
#     include "io.h"                                  /* lseek() decl */
#  endif
#  define lfread(a,b,c,d)  epi_fread(a,b,c,d)
#  define lfwrite(a,b,c,d) epi_fwrite(a,b,c,d)
#  define lfseek(a,b,c)    epi_fseek(a,b,c)
#  define lrewind(a)       ((void)epi_fseek(a,0L,SEEK_SET))
#  define lftell(a)        epi_ftell(a)
#  define lfflush(a)       epi_fflush(a)

                  /* to use both methods in one source file, undef    */
                  /* the following and use lfread() and bfread() etc. */
#  define fread(a,b,c,d)   epi_fread(a,b,c,d)
#  define fwrite(a,b,c,d)  epi_fwrite(a,b,c,d)
#  define fseek(a,b,c)     epi_fseek(a,b,c)
#  define rewind(a)        ((void)epi_fseek(a,0L,SEEK_SET))
#  define ftell(a)         epi_ftell(a)
#  define fflush(a)        epi_fflush(a)
#else                                                        /* LOWIO */
#  define lfread(a,b,c,d)  fread(a,b,c,d)
#  define lfwrite(a,b,c,d) fwrite(a,b,c,d)
#  define lfseek(a,b,c)    fseek(a,b,c)
#  define lrewind(a)       rewind(a)
#  define lftell(a)        ftell(a)
#  define lfflush(a)       fflush(a)
#endif                                                       /* LOWIO */

extern size_t epi_fread  ARGS((void *buf,size_t sz,size_t n,FILE *fp));
extern size_t epi_fwrite ARGS((void *buf,size_t sz,size_t n,FILE *fp));
extern int  epi_fseek  ARGS((FILE *fp,long off,int orig));
extern long epi_ftell  ARGS((FILE *fp));
extern int  epi_fflush ARGS((FILE *fp));

#else                                                      /* !USE_EPI */

#  define mac_on()
#  define mac_off()
#  define mac_won()
#  define mac_woff()
#  define mac_wptr(a)
#  define mac_sum(a)
#  define mac_vsum()
#  define mac_check(a)
#  define mac_ovchk()
#  define mac_visual()

   extern void FAR * FAR epi_malloc   ARGS((uint));
   extern void FAR * FAR epi_calloc   ARGS((uint,uint));
   extern void FAR * FAR epi_remalloc ARGS((void FAR *,uint));
   extern void FAR * FAR epi_recalloc ARGS((void FAR *,uint,uint));
   extern void FAR * FAR epi_free     ARGS((void FAR *));
   extern void       FAR epi_exit     ARGS((int rc));
   extern char FAR * FAR epi_strdup   ARGS((char FAR *));
   extern char FAR * FAR epi_tempnam  ARGS((char FAR *,char FAR *));
   extern char FAR * FAR epi_getcwd   ARGS((char FAR *,int));
   extern char FAR * FAR epi_fullpath ARGS((char FAR *,char FAR *,int));
#ifdef _WIN32
#if defined(S_IFMT) || defined(_S_IFMT)
   extern int        FAR epi_stat     ARGS((char FAR *,struct stat FAR *));
#else
   extern int        FAR epi_stat     ARGS(());
#endif
#ifndef S_IHIDDEN                /* MAW 03-05-93 - extended stat mode */
#  define S_IHIDDEN 0x0800
#endif
extern long       FAR epi_time     ARGS((long *));
/*#define time(a)   epi_time(a)*/
#endif /* _WIN32 */

#  define bfread(a,b,c,d)  fread(a,b,c,d)
#  define bfwrite(a,b,c,d) fwrite(a,b,c,d)
#  define bfseek(a,b,c)    fseek(a,b,c)
#  define brewind(a,b,c)   rewind(a)
#  define bftell(a)        ftell(a)
#  define bfflush(a)       fflush(a)

#  define lfread(a,b,c,d)  fread(a,b,c,d)
#  define lfwrite(a,b,c,d) fwrite(a,b,c,d)
#  define lfseek(a,b,c)    fseek(a,b,c)
#  define lrewind(a)       rewind(a)
#  define lftell(a)        ftell(a)
#  define lfflush(a)       fflush(a)

#endif                                                     /* !USE_EPI */

extern void       FAR mac_dovsum   ARGS((CONST char FAR *fn,int ln));

/**********************************************************************/

#if defined(_WIN32) && !defined(_WIN64)
  /* in win64 we use MSVS _chsize_s() instead (also takes __int64): */
extern int CDECL _chsizei64 ARGS((int filedes, __int64 size));  /* KNG 020218 */
#endif /* _WIN32 && !_WIN64 */
#if defined(TX_NT_LARGEFILE)
#define EPI_LSEEK(a,b,c) _lseeki64((a),(b),(c))
#define EPI_STAT(a,b)	_stati64((a),(b))
#define EPI_FSTAT(a,b)	_fstati64((a),(b))
#define EPI_STAT_S	struct _stati64
#else /* TX_NT_LARGEFILE */
#define EPI_LSEEK(a,b,c) lseek((a),(b),(c))
#define EPI_STAT(a,b)	stat((a),(b))
#define EPI_FSTAT(a,b)	fstat((a),(b))
#define EPI_LSTAT(a,b)	lstat((a),(b))
#define EPI_STAT_S	struct stat
#endif /* TX_NT_LARGEFILE */
#define EPI_STAT_SPN    ((EPI_STAT_S *)NULL)
#define EPI_STAT_SPPN   ((EPI_STAT_S **)NULL)

#if defined(TX_DEBUG) && defined(_WIN32)
int     debugbreak(void);
#  ifndef TX_NO_CRTDBG_H_INCLUDE
#include "crtdbg.h"
#  endif /* TX_NO_CRTDBG_H_INCLUDE */
#endif /* TX_DEBUG && _WIN32 */

#define EPI_MIN(a, b)   ((a) < (b) ? (a) : (b))
#define EPI_MAX(a, b)   ((a) > (b) ? (a) : (b))

/******************************************************************/

/* These should only be used if <signal.h> was included before this: */
#ifdef SIGHUP
#  define EPI_SIGHUP SIGHUP
#else
#  define EPI_SIGHUP 1
#endif
#ifdef SIGINT
#  define EPI_SIGINT SIGINT
#else
#  define EPI_SIGINT 2
#endif
#ifdef SIGQUIT
#  define EPI_SIGQUIT SIGQUIT
#else
#  define EPI_SIGQUIT 3
#endif
#ifdef SIGILL
#  define EPI_SIGILL SIGILL
#else
#  define EPI_SIGILL 4
#endif
#ifdef SIGTRAP
#  define EPI_SIGTRAP SIGTRAP
#else
#  define EPI_SIGTRAP 5
#endif
#ifdef SIGABRT
#  define EPI_SIGABRT SIGABRT
#else
#  define EPI_SIGABRT 6
#endif
#ifdef SIGIOT
#  define EPI_SIGIOT SIGIOT
#else
#  define EPI_SIGIOT 6
#endif
#ifdef SIGBUS
#  define EPI_SIGBUS SIGBUS
#else
#  define EPI_SIGBUS 7
#endif
#ifdef SIGEMT
#  define EPI_SIGEMT SIGEMT
#else
#  define EPI_SIGEMT 7
#endif
#ifdef SIGFPE
#  define EPI_SIGFPE SIGFPE
#else
#  define EPI_SIGFPE 8
#endif
#ifdef SIGKILL
#  define EPI_SIGKILL SIGKILL
#else
#  define EPI_SIGKILL 9
#endif
#ifdef SIGUSR1
#  define EPI_SIGUSR1 SIGUSR1
#else
#  define EPI_SIGUSR1 10
#endif
#ifdef SIGSEGV
#  define EPI_SIGSEGV SIGSEGV
#else
#  define EPI_SIGSEGV 11
#endif
#ifdef SIGUSR2
#  define EPI_SIGUSR2 SIGUSR2
#else
#  define EPI_SIGUSR2 12
#endif
#ifdef SIGPIPE
#  define EPI_SIGPIPE SIGPIPE
#else
#  define EPI_SIGPIPE 13
#endif
#ifdef SIGALRM
#  define EPI_SIGALRM SIGALRM
#else
#  define EPI_SIGALRM 14
#endif
#ifdef SIGTERM
#  define EPI_SIGTERM SIGTERM
#else
#  define EPI_SIGTERM 15
#endif
#ifdef SIGSTKFLT
#  define EPI_SIGSTKFLT SIGSTKFLT
#else
#  define EPI_SIGSTKFLT 16
#endif
#ifdef SIGCHLD
#  define EPI_SIGCHLD SIGCHLD
#elif defined(SIGCLD)
#  define EPI_SIGCHLD SIGCLD
#else
#  define EPI_SIGCHLD 17
#endif
#ifdef SIGCLD
#  define EPI_SIGCLD  SIGCLD
#elif defined(SIGCHLD)
#  define EPI_SIGCLD SIGCHLD
#else
#  define EPI_SIGCLD 17
#endif
#ifdef SIGCONT
#  define EPI_SIGCONT SIGCONT
#else
#  define EPI_SIGCONT 18
#endif
#ifdef SIGSTOP
#  define EPI_SIGSTOP SIGSTOP
#else
#  define EPI_SIGSTOP 19
#endif
#ifdef SIGTSTP
#  define EPI_SIGTSTP SIGTSTP
#else
#  define EPI_SIGTSTP 20
#endif
#ifdef SIGTTIN
#  define EPI_SIGTTIN SIGTTIN
#else
#  define EPI_SIGTTIN 21
#endif
/* SIGBREAK is Windows: */
#ifdef SIGBREAK
#  define EPI_SIGBREAK SIGBREAK
#else
#  define EPI_SIGBREAK 21
#endif
#ifdef SIGTTOU
#  define EPI_SIGTTOU SIGTTOU
#else
#  define EPI_SIGTTOU 22
#endif
#ifdef SIGURG
#  define EPI_SIGURG SIGURG
#else
#  define EPI_SIGURG 23
#endif
#ifdef SIGXCPU
#  define EPI_SIGXCPU SIGXCPU
#else
#  define EPI_SIGXCPU 24
#endif
#ifdef SIGXFSZ
#  define EPI_SIGXFSZ SIGXFSZ
#else
#  define EPI_SIGXFSZ 25
#endif
#ifdef SIGVTALRM
#  define EPI_SIGVTALRM SIGVTALRM
#else
#  define EPI_SIGVTALRM 26
#endif
#ifdef SIGPROF
#  define EPI_SIGPROF SIGPROF
#else
#  define EPI_SIGPROF 27
#endif
#ifdef SIGWINCH
#  define EPI_SIGWINCH SIGWINCH
#else
#  define EPI_SIGWINCH 28
#endif
#ifdef SIGIO
#  define EPI_SIGIO SIGIO
#else
#  define EPI_SIGIO 29
#endif
#ifdef SIGPOLL
#  define EPI_SIGPOLL SIGPOLL
#else
#  define EPI_SIGPOLL 29
#endif
#ifdef SIGINFO
#  define EPI_SIGINFO SIGINFO
#else
#  define EPI_SIGINFO 29
#endif
#ifdef SIGPWR
#  define EPI_SIGPWR SIGPWR
#else
#  define EPI_SIGPWR 30
#endif
#ifdef SIGSYS
#  define EPI_SIGSYS SIGSYS
#else
#  define EPI_SIGSYS 31
#endif
#ifdef SIGUNUSED
#  define EPI_SIGUNUSED SIGUNUSED
#else
#  define EPI_SIGUNUSED 31
#endif

/* KNG 20150729 these days we are unlikely to find a compiler that
 * does not define __FUNCTION__; try to detect them nonetheless.
 * Since __FUNCTION__ is only defined inside a function, we cannot use
 * `#ifndef __FUNCTION__' here:
 */
#if !defined(__GNUC__) && !defined(_MSC_VER)
#  define __FUNCTION__        "<unknown>"
#endif

#ifndef TX_ALIGNOF
# ifdef EPI_HAVE_ALIGNOF
#  define TX_ALIGNOF(x) alignof(x)
# elif defined(EPI_HAVE__ALIGNOF)
#  define TX_ALIGNOF(x) _Alignof(x)
# elif defined(EPI_HAVE___ALIGNOF)
#  define TX_ALIGNOF(x) __alignof(x)
# elif defined(EPI_HAVE___ALIGNOF__)
#  define TX_ALIGNOF(x) __alignof__(x)
# endif
#endif
/* WTF
#ifndef TX_ALIGNOF
#  error Need alignof()
#endif
*/
typedef int (TXqsort_rCmpFunc)(const void *a, const void *b, void *usr);

#ifdef EPI_HAVE_QSORT_R
#  define TXqsort_r(base, numEl, elSz, cmp, usr)	\
	qsort_r((base), (numEl), (elSz), (cmp), (usr))
#else /* !EPI_HAVE_QSORT_R */
void	TXqsort_r(void *base, size_t numEl, size_t elSz,
		  TXqsort_rCmpFunc *cmp, void *usr);
#endif /* !EPI_HAVE_QSORT_R */

typedef enum TXrawOpenFlag_tag
{
  TXrawOpenFlag_None                    = 0,
  TXrawOpenFlag_Inheritable             = (1 << 0),
  TXrawOpenFlag_SuppressNoSuchFileErr   = (1 << 1),
  TXrawOpenFlag_SuppressFileExistsErr   = (1 << 2)
}
TXrawOpenFlag;
int TXrawOpen(TXPMBUF *pmbuf, const char *fn, const char *pathDesc,
	      const char *path, TXrawOpenFlag txFlags, int flags, int mode);
int     tx_rawread(TXPMBUF *pmbuf, int fd, const char *fnam, byte *buf,
                   size_t sz, int flags);
size_t tx_rawwrite(TXPMBUF *pmbuf, int fd, const char *path,
		TXbool pathIsDescription, byte *buf, size_t sz, TXbool inSig);

#endif                                                     /* MACRO_H */
#ifdef __cplusplus
}
#endif
