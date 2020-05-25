#ifndef TX_THREADS_H
#define TX_THREADS_H

/* Thread objects and functions.  Defined in a separate header for
 * use by source/epi/atomicTests.c at header-build time, when config.h
 * etc. in flux.  Try not to include or do anything fancy her.e
 */
#ifndef TX_CONFIG_H
#  include "txcoreconfig.h"                           /* for EPI_..._PTHREADS */
#endif /* !TX_CONFIG_H */
#include "txtypes.h"

#if defined(EPI_HAVE_PTHREADS) && !defined(EPI_PTHREADS_UNSAFE)
#  define EPI_USE_PTHREADS
#else
#  undef  EPI_USE_PTHREADS
#endif

#ifdef EPI_USE_PTHREADS
#  ifdef __APPLE__
/* `word' conflicts with pthread.h? */
#    undef word
#  endif /* __APPLE__ */
#  ifdef _AIX
#    undef LOCK_FAIL   /* sys/lock_def.h conflict; see also texerr.h */
#  endif /* _AIX */
#  include <pthread.h>
#  ifdef __APPLE__
/* restore `word' for tstone.c etc. */
#    if EPI_OS_SHORT_BITS == 16
#      define word unsigned short
#    else /* EPI_OS_SHORT_BITS != 16 */
error define word;
#    endif /* EPI_OS_SHORT_BITS != 16 */
#  endif /* __APPLE__ */
#endif /* EPI_USE_PTHREADS */

/* A TXCINIT object is a critical-init object, ie. a critical section
 * for one-time global initialization.  It must be declared statically:
 *
 *   static TXCINIT_DECL(myInit);
 *   static void *myGlobalObject = NULL;
 *
 *   void
 *   InitializeMe()
 *   {
 *     int   ret = TXCINIT_OK;
 *
 *     TXentercinit(myInit);
 *     ... critical-init area: myGlobalObject = whatever ...
 *     ... ret = TXCINIT_OK or TXCINIT_FAIL;
 *     TXexitcinit(myInit, ret);
 *     if (TXcinitisok(myInit)) ... ok ... else ... failure ...
 *   }
 *
 *   At end (single thread):  TXdeletecinit(myInit);
 *
 * Critical-init area will only run once, and atomically across threads.
 * Less overhead on Windows than a CRITICAL_SECTION object.
 *
 * -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
 *
 * A TXcriticalSection object protects critical sections.  Usage:
 *
 *   TXcriticalSection  *csp;
 *   csp = TXcriticalSectionOpen(TXcriticalSectionType_...);
 *
 *   void
 *   MultiThreadFunc()
 *   {
 *     if (TXcriticalSectionEnter(csp))
 *       {
 *         ... critical section code ...
 *         TXcriticalSectionExit(csp);
 *       }
 *     else
 *       reportError();
 *   }
 *
 *   When all done:  csp = TXcriticalSectionClose(csp);
 */

#define TXCINIT_FAIL            0
#define TXCINIT_INIT            1       /* internal use only */
#define TXCINIT_OK              2
#define TXcinitisok(ci)         (ci == TXCINIT_OK)

typedef enum TXcriticalSectionType_tag
  {
    TXcriticalSectionType_Normal,       /* undefined recursive behavior */
    /* ...ErrorCheck might also check for unlock of non-locked
     * critical section, or might not, depending on platform:
     */
    TXcriticalSectionType_ErrorCheck,   /* disallow recursion */
    /* ...Recursive allows recursive enters by same thread (must have
     * equal number of exits), and also does some error checking:
     */
    TXcriticalSectionType_Recursive,    /* allow recursion */
    TXcriticalSectionType_Default = TXcriticalSectionType_Normal
  }
  TXcriticalSectionType;

typedef struct TXcriticalSection_tag    TXcriticalSection;

TXcriticalSection *TXcriticalSectionOpen(TXcriticalSectionType type,
                                         TXPMBUF *pmbuf);
TXcriticalSection *TXcriticalSectionClose(TXcriticalSection *cs,
                                          TXPMBUF *pmbuf);
#define TX_CRITICAL_SECTION_TRACE_PARAMS        \
  , const char *callingFunc, int line
#define TX_CRITICAL_SECTION_TRACE_ARGS          \
  , __FUNCTION__, __LINE__
int     TXcriticalSectionEnter(TXcriticalSection *cs, TXPMBUF *pmbuf
                               TX_CRITICAL_SECTION_TRACE_PARAMS);
#define TXcriticalSectionEnter(cs, pmbuf)       \
  TXcriticalSectionEnter(cs, pmbuf TX_CRITICAL_SECTION_TRACE_ARGS)
int     TXcriticalSectionExit(TXcriticalSection *cs, TXPMBUF *pmbuf
                              TX_CRITICAL_SECTION_TRACE_PARAMS);
#define TXcriticalSectionExit(cs, pmbuf)        \
  TXcriticalSectionExit(cs, pmbuf TX_CRITICAL_SECTION_TRACE_ARGS)
int     TXcriticalSectionDepth(TXcriticalSection *cs);

#undef EPI_HAVE_THREADS

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#ifdef _WIN32
#  define EPI_HAVE_THREADS      1
typedef HANDLE                  TXTHREAD;
#  define TXTHREADPN            ((TXTHREAD *)NULL)
typedef DWORD                   TXTHREADID;
typedef TXTHREADID              TXthreadAsyncId;
#  if EPI_OS_INT_BITS == 32
#    define TXthreadAsyncId_StrFmt      "0x%x"
typedef int                     TXthreadAsyncId_StrFmtType;
#  else
#    error Need TXthreadAsyncId_StrFmt[Type]
#  endif
#  define TXTHREAD_NULL         ((HANDLE)NULL)
typedef DWORD                   TXTHREADRET;
#  define TXTHREADPFX           WINAPI
#  define TXgetcurrentthreadid()        GetCurrentThreadId()
#  define TXthreadidsAreEqual(a, b)     ((a) == (b))
#  define TXthreadAsyncIdsAreEqual(a, b) TXthreadidsAreEqual(a, b)
#  define TXgetCurrentThreadAsyncId()   TXgetcurrentthreadid()
#  define TXgetThreadId(h)              GetThreadId(h) /* `h' is a TXTHREAD */
/* TXthreadClose(): just closes `handle' from TXcreatethread(), if needed: */
#  define TXthreadClose(handle) \
  (handle ? (CloseHandle(handle), TXTHREAD_NULL) : TXTHREAD_NULL)
typedef HANDLE                  TXMUTEXSYSOBJ;
typedef void                    TXEVENT;      /* note: TXEVENT * is HANDLE */
/* NOTE: we assume PVOID is of the size and alignment for atomic increment: */
#if _MSC_VER <= 1200            /* old inconsistent prototypes */
#  define InterlockedCompareExchangePointer InterlockedCompareExchange
#  define InterlockedExchangePointer(a, b)  \
  InterlockedExchange((LPLONG)(a), (LONG)(b))
#endif
#  define TXCINIT_DECL(ci)      volatile PVOID ci = (PVOID)TXCINIT_FAIL
#  define TXentercinit(ci)      if ((ci) != (PVOID)TXCINIT_OK) { if \
  (InterlockedCompareExchangePointer(&(ci), (PVOID)TXCINIT_INIT,    \
  (PVOID)TXCINIT_FAIL) == (PVOID)TXCINIT_FAIL) {
#  define TXexitcinit(ci, val)  \
  InterlockedExchangePointer(&(ci), (PVOID)(val)); } \
  else while ((ci) == (PVOID)TXCINIT_INIT) Sleep(50L); }
#  define TXdeletecinit(ci)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#elif defined(EPI_USE_PTHREADS)
#  define EPI_HAVE_THREADS      1
typedef pthread_t               TXTHREAD;
#  define TXTHREADPN            ((TXTHREAD *)NULL)
typedef pthread_t               TXTHREADID;
typedef PID_T                   TXthreadAsyncId;
#  define TXthreadAsyncId_StrFmt        "%d"            /* probably a PID */
typedef int                     TXthreadAsyncId_StrFmtType;
#  define TXTHREAD_NULL         ((pthread_t)(-1))       /* WTF */
typedef void                    *TXTHREADRET;
#  define TXTHREADPFX
#  define TXgetcurrentthreadid()        pthread_self()
#  define TXthreadidsAreEqual(a, b)     pthread_equal(a, b)
#  define TXthreadAsyncIdsAreEqual(a, b) ((a) == (b))
TXthreadAsyncId TXgetCurrentThreadAsyncId(void);
#  define TXgetThreadId(h)              (h)     /* `h' is a TXTHREAD */
/* TXthreadClose(): just closes `handle' from TXcreatethread(), if needed: */
#  define TXthreadClose(handle) TXTHREAD_NULL
typedef pthread_mutex_t         TXMUTEXSYSOBJ;
typedef pthread_cond_t          TXEVENT;

typedef struct TXCINIT_tag
{
  int                   state;
  pthread_mutex_t       mutex;
}
TXCINIT;
#  define TXCINIT_DECL(ci)      \
  TXCINIT ci = { TXCINIT_FAIL, EPI_PTHREAD_MUTEX_INITIALIZER }
#  define TXentercinit(ci)      \
  pthread_mutex_lock(&ci.mutex); if (ci.state == TXCINIT_FAIL) {
#  define TXexitcinit(ci, val)  \
  ci.state = val; } pthread_mutex_unlock(&ci.mutex)
#  define TXdeletecinit(ci)     \
  pthread_mutex_destroy(&ci.mutex); ci.mutex = EPI_PTHREAD_MUTEX_INITIALIZER

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#else /* !_WIN32 && !(EPI_HAVE_PTHREADS && !EPI_PTHREADS_UNSAFE) */
typedef void                    *TXTHREAD;
#  define TXTHREADPN            ((TXTHREAD *)NULL)
typedef void                    *TXTHREADID;
typedef TXTHREADID              TXthreadAsyncId;
#  define TXthreadAsyncId_StrFmt      "%p"
typedef void                    *TXthreadAsyncId_StrFmtType;
#  define TXTHREAD_NULL         ((void *)NULL)
typedef int                     TXTHREADRET;
#  define TXTHREADPFX
#  define TXgetcurrentthreadid()        ((TXTHREADID)0)
#  define TXthreadidsAreEqual(a, b)     (1)
#  define TXthreadAsyncIdsAreEqual(a, b) TXthreadidsAreEqual(a, b)
#  define TXgetCurrentThreadAsyncId()   TXgetcurrentthreadid()
/* TXthreadClose(): just closes `handle' from TXcreatethread(), if needed: */
#  define TXthreadClose(handle) TXTHREAD_NULL
typedef void                    TXMUTEXSYSOBJ;
typedef void                    TXEVENT;

#  define TXCINIT_DECL(ci)      VOLATILE int ci = TXCINIT_FAIL
#  define TXentercinit(ci)      if (ci == TXCINIT_FAIL) {
#  define TXexitcinit(ci, val)  ci = val; }
#  define TXdeletecinit(ci)

#endif /* !_WIN32 && !(EPI_HAVE_PTHREADS && !EPI_PTHREADS_UNSAFE) - - - - - */

#define TXTHREADRETPN   ((TXTHREADRET *)NULL)

typedef TXTHREADRET TXTHREADPFX TXTHREADFUNC ARGS((void *arg));
#define TXTHREADFUNCPN  ((TXTHREADFUNC *)NULL)

typedef enum TXTHREADFLAG_tag
{
  TXTHREADFLAG_SCOPE_PROCESS    = (1 << 0),     /* share CPU with others */
  TXTHREADFLAG_DETACHED         = (1 << 1)      /* no TXwaitforthreadexit() */
}
TXTHREADFLAG;
#define TXTHREADFLAGPN  ((TXTHREADFLAG *)NULL)

#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
size_t TXprintOtherThreadPids(TXbool inSig, char *buf, size_t bufSz);
void TXkillOtherThreads(TXbool inSig);
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

int TXcreatethread(TXPMBUF *pmbuf, const char *name, TXTHREADFUNC *func,
                   void *arg, int flags, size_t stksz, TXTHREAD *threadp);
int TXwaitforthreadexit(TXPMBUF *pmbuf, TXTHREAD thread, double timeout,
                        TXTHREADRET *exitcodep);
#if defined(_WIN32) || (defined(EPI_USE_PTHREADS) && defined(EPI_HAVE_PTHREAD_TIMEDJOIN_NP))
#  define TX_HAVE_TIMED_THREAD_WAIT     1
#else
#  undef TX_HAVE_TIMED_THREAD_WAIT
#endif

int TXterminatethread(TXPMBUF *pmbuf, TXTHREAD thread, TXTHREADRET exitcode);
extern TXATOMINT        TxThreadsCreated;
#define TXthreadscreated()      (TxThreadsCreated != 0)
TXbool  TXinitThreads(TXPMBUF *pmbuf);
TXbool  TXthreadFixInfoAfterFork(TXTHREADID parentTid);
const char      *TXgetCurrentThreadName(TXbool inSig);
int     TXgetCurrentThread(int dup, TXTHREAD *thread);
int     TXthreadIsAlive(TXTHREAD thread);


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Make TXMUTEX a wrapper that adds tracing: */
typedef struct TXMUTEX_tag
{
  TXMUTEXSYSOBJ sysObj;                         /* actual system mutex */
  TXPMBUF       *pmbuf;                         /* buffer for messages */
  int           trace;                          /* copy of TXApp->traceMutex*/
  TXATOMINT     lockCount;                      /* # lockers (should be 0/1)*/
  const char    *lastLockerFile;                /* last locker's __FILE__ */
  int           lastLockerLine;                 /* last locker's __LINE__ */
  double        lastLockerTime;                 /* "" time */
  const char    *lastUnlockerFile;              /* last unlocker *attempt* */
  int           lastUnlockerLine;               /* "" __LINE__ */
  double        lastUnlockerTime;               /* "" time */
#if !defined(_WIN32) && !defined(EPI_USE_PTHREADS)
  TXATOMINT     spinLock;
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}
TXMUTEX;
int TXmutexLock(TXMUTEX *mutex, double waitSec, const char *file, int line);
int TXmutexUnlock(TXMUTEX *mutex, const char *file, int line);
#  define TXmutexLock(mutex, waitSec)      \
  TXmutexLock((mutex), (waitSec), __FILE__, __LINE__)
#  define TXmutexUnlock(mutex)  TXmutexUnlock((mutex), __FILE__, __LINE__)
#define TXMUTEXPN       ((TXMUTEX *)NULL)
#define TXMUTEXPPN      ((TXMUTEX **)NULL)
TXMUTEX *TXmutexOpen(TXPMBUF *pmbuf, const char *name);
TXMUTEX *TXmutexClose(TXMUTEX *mutex);

#endif /* !TX_THREADS_H */
