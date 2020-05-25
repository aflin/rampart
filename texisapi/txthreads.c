/* Thread code.  #included by source/epi/atomicTests.c;
 * the latter is during build-headers stage, so do not include or call
 * anything fancy here.
 */

/* This is compiled early (during make headers), so do not include much: */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#if defined(EPI_HAVE_SYSCALL_GETTID) && defined(EPI_HAVE_SYS_SYSCALL_H)
#  include <sys/syscall.h>
#endif /* EPI_HAVE_SYSCALL_GETTID && EPI_HAVE_SYS_SYSCALL_H */
#include "os.h"                                 /* for VOLATILE */
#include "mmsg.h"
#include "sizes.h"                              /* for EPI_INT... txtypes.h */
#include "txtypes.h"
#include "txthreads.h"
#ifdef TX_ATOMIC_TESTS_C
/* texint.h cannot be included yet */
#  define TX_NEW(pmbuf, type)   ((type *)calloc(1, sizeof(type)))
#  define TXfree(ptr)           (free(ptr), NULL)
#  define TXstrdup(pmbuf, fn, s)        strdup(s)
#  ifdef _WIN32
#    define TXsleepmsec(msec, ignsig)     Sleep(msec)
#  else
#    define TXsleepmsec(msec, ignsig)     sleep((msec)/1000)
#  endif
#  define TXcriticalSectionOpen(type, pmbuf)    ((TXcriticalSection *)1)
#  define TXcriticalSectionClose(csect, pmbuf)  ((TXcriticalSection *)NULL)
#  undef  TXcriticalSectionEnter
#  define TXcriticalSectionEnter(csect, pmbuf)  1
#  undef  TXcriticalSectionExit
#  define TXcriticalSectionExit(csect, pmbuf)   ((void)pmbuf)
int CDECL
txpmbuf_putmsg(TXPMBUF *pmbuf, int num, const char *fn, const char *fmt, ...)
{
  va_list       args;

  (void)pmbuf;
  va_start(args, fmt);
  if ((num/100)*100 == MERR)
    fprintf(stderr, "\nError: ");
  else if ((num/100)*100 == MWARN)
    fprintf(stderr, "\nWarning: ");
  else
    fprintf(stderr, "\nNote: ");
  vfprintf(stderr, fmt, args);
  if (fn) fprintf(stderr, " in the function %s", fn);
  fprintf(stderr, "\n");
  fflush(stderr);
  return(1);
}
#  undef EPI_KILL_OTHER_THREADS_AT_EXIT         /* csect code not linked */
const char      TXunsupportedPlatform[] = "Unsupported platform";
#else /* !TX_ATOMIC_TESTS_C */
#  include "texint.h"                           /* for TX_NEW(), TXfree() */
#  include "cgi.h"                              /* for htsnpf() */
#endif /* !TX_ATOMIC_TESTS_C */


TXATOMINT       TxThreadsCreated = 0;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/* Thread info: */

typedef struct TXthreadInfo_tag
{
  struct TXthreadInfo_tag       *prev, *next;
  TXTHREADFUNC                  *func;          /* NULL for main thread */
  void                          *arg;
  TXTHREADID                    threadId;
  TXthreadAsyncId               asyncId;
#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  PID_T                         pid;
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
  char                          *name;          /* alloced */
}
TXthreadInfo;
static TXthreadInfo             *TXthreadInfoList = NULL;
/* Csect for normal users, refcount for signal handlers: */
static TXcriticalSection        *TXthreadInfoListCsect = NULL;
static TXATOMINT                TXthreadInfoListRefCount = 0;
static const char               TXmainThreadName[] = "main";


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static const char       TXnoCsectFmt[] =
  "Cannot %s critical section in %s at line %d: Missing (NULL pointer)";

#if !defined(_WIN32) && !defined(EPI_USE_PTHREADS)
static CONST char       NoThreadsPlatform[] =
  "Threads are not supported on this platform";
#endif /* !_WIN32 && !EPI_USE_PTHREADS */

/* ------------------------------------------------------------------------ */

static TXthreadInfo *
TXthreadInfoClose(TXthreadInfo *threadInfo)
/* Thread-safe.
 */
{
  if (threadInfo)
    {
      threadInfo->name = TXfree(threadInfo->name);
      threadInfo = TXfree(threadInfo);
    }
  return(NULL);
}

/* ------------------------------------------------------------------------ */

static TXthreadInfo *
TXthreadInfoOpen(TXPMBUF *pmbuf, const char *name)
/* `pmbuf' only used here; not cloned into object.
 * Thread-safe.
 */
{
  TXthreadInfo  *threadInfo;

#ifdef TX_ATOMIC_TESTS_C
  (void)pmbuf;
#endif /* TX_ATOMIC_TESTS_C */

  if (!(threadInfo = TX_NEW(pmbuf, TXthreadInfo)) ||
      !(threadInfo->name = TXstrdup(pmbuf, __FUNCTION__, name)))
    threadInfo = TXthreadInfoClose(threadInfo);
  return(threadInfo);
}

/* ------------------------------------------------------------------------ */

TXbool
TXthreadFixInfoAfterFork(TXTHREADID parentTid)
/* Called in child process after fork.
 * Thread-safe.  Async-signal-unsafe.
 */
{
  TXthreadInfo  *info;

  if (!TXthreadInfoListCsect) goto ok;          /* nothing to fix up */
  if (TXcriticalSectionEnter(TXthreadInfoListCsect, TXPMBUFPN))
    {
      /* No need to inc refcount: we have csect so writers will
       * not interfere, and we are not modifying so (read-only)
       * signal contexts not affected.
       */
      for (info = TXthreadInfoList; info; info = info->next)
        /* Our thread id (especially async id -- PID) probably changed: */
        if (TXthreadidsAreEqual(info->threadId, parentTid))
          {
            info->threadId = TXgetcurrentthreadid();
            info->asyncId = TXgetCurrentThreadAsyncId();
#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
            info->pid = getpid();               /* do not disturb TXgetpid */
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
          }
      TXcriticalSectionExit(TXthreadInfoListCsect, TXPMBUFPN);
    }
  else
    return(TXbool_False);
ok:
  return(TXbool_True);
}

/* ------------------------------------------------------------------------ */

TXbool
TXinitThreads(TXPMBUF *pmbuf)
/* Presumably called in main thread.
 * Returns false on error.
 */
{
  TXthreadInfo  *mainInfo = NULL;
  TXbool        ret;

  if (TXthreadInfoListCsect) goto ok;           /* already inited */

  if (!(mainInfo = TXthreadInfoOpen(pmbuf, TXmainThreadName)) ||
      !(TXthreadInfoListCsect =
        TXcriticalSectionOpen(TXcriticalSectionType_ErrorCheck, pmbuf)))
    goto err;
  /* See also TXthreadFixInfoAfterFork(): */
  mainInfo->threadId = TXgetcurrentthreadid();
  mainInfo->asyncId = TXgetCurrentThreadAsyncId();
#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  mainInfo->pid = getpid();                     /* do not disturb TXgetpid */
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
  /* leave `mainInfo->func' NULL to indicate main thread */
  TXthreadInfoList = mainInfo;
  mainInfo = NULL;                              /* list owns it */
ok:
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
  TXthreadInfoListCsect =
    TXcriticalSectionClose(TXthreadInfoListCsect, pmbuf);
finally:
  mainInfo = TXthreadInfoClose(mainInfo);
  return(ret);
}

/* ------------------------------------------------------------------------ */

const char *
TXgetCurrentThreadName(TXbool inSig)
/* Returns name of current thread if known, else NULL.
 * Return value only valid for life of current thread (to get names of
 * other threads, we'd need to copy to a caller-provided buffer).
 * Thread-safe iff !inSig.
 * Signal-safe iff inSig.
 */
{
  TXthreadAsyncId       asyncId;
  const char            *name = NULL;
  TXthreadInfo          *info;

  /* Optimization: avoid init if we are just being called in putmsg()
   * handler; if not inited, we know we are main:
   */
  if (!TXthreadInfoListCsect) return(TXmainThreadName);

  asyncId = TXgetCurrentThreadAsyncId();

  if (inSig)
    {
      /* Cannot safely use csect, nor pause.  Alert others to our use,
       * and bail if list possibly being modified.  Use compare-and-swap
       * instead of inc, so we do not inc refcount unnecessarily
       * (i.e. when we are not first-in):
       */
      if (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {
          /* wtf could be another (read-only) signal user which is
           * benign here, not a writer which is not; cannot tell the
           * difference
           */
          name = NULL;
        }
      else
        {
          for (info = TXthreadInfoList;
               info && !TXthreadAsyncIdsAreEqual(asyncId, info->asyncId);
               info = info->next)
            ;
          if (info) name = info->name;
          (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
        }
    }
  else                                          /* not in signal */
    {
      if (TXcriticalSectionEnter(TXthreadInfoListCsect, TXPMBUFPN))
        {
          /* No need to inc refcount: we have csect so writers will
           * not interfere, and we are not modifying so (read-only)
           * signal contexts not affected.
           */
          for (info = TXthreadInfoList;
               info && !TXthreadAsyncIdsAreEqual(asyncId, info->asyncId);
               info = info->next)
            ;
          if (info) name = info->name;
          TXcriticalSectionExit(TXthreadInfoListCsect, TXPMBUFPN);
        }
    }

  return(name);
}

/* ------------------------------------------------------------------------ */

static TXTHREADRET
TXthreadWrapper(void *usr)
/* Wrapper function for thread start functions.  Updates TXthreadPids.
 * Thread-safe.
 */
{
  TXthreadInfo          *info = (TXthreadInfo *)usr;
  /* wtf would like to pass in a TXPMBUF, but not cross-thread-safe yet: */
  TXPMBUF               *pmbuf = TXPMBUFPN;
  TXTHREADRET           ret;
  TXTHREADID            myThreadId;
  /* Copy essential info from `info': may lose it after csect: */
  TXTHREADFUNC          *func = info->func;
  void                  *arg = info->arg;

  /* Finish filling out `info', and add to list: */
  info->threadId = myThreadId = TXgetcurrentthreadid();
  info->asyncId = TXgetCurrentThreadAsyncId();
#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
  info->pid = getpid();                   /* do not affect TXgetpid() */
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */
  if (TXcriticalSectionEnter(TXthreadInfoListCsect, pmbuf))
    {
      int       msec = 0;

      /* Also inc refcount, to let in-signal contexts (who cannot
       * safely access csect) know we are modifying list.  Also wait
       * for them, so we do not corrupt list during their access.
       * In-signal contexts cannot wait; should be ok since they also
       * do not modify.  Should not be any deadlock with other
       * non-signal contexts, as we only wait here inside csect, which
       * other non-signal contexts would also have to obtain first.
       */
      while (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {
          TXsleepmsec(msec, 0);
          if (msec < 50) msec++;
        }
      info->prev = NULL;
      info->next = TXthreadInfoList;
      if (TXthreadInfoList) TXthreadInfoList->prev = info;
      TXthreadInfoList = info;
      (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
      info = NULL;                              /* list owns it */
      usr = NULL;
      TXcriticalSectionExit(TXthreadInfoListCsect, pmbuf);
    }

  /* Run the thread: */
  ret = func(arg);

  /* Remove thread info from list.  Cannot assume it is still there;
   * if we are about to be terminated, TXterminatethread() might beat
   * us to removing our info from the list.  Of course, we may also
   * still have it if failed to add to list above:
   */
  if (!info && TXcriticalSectionEnter(TXthreadInfoListCsect, pmbuf))
    {
      int       msec = 0;

      while (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {                                       /* see comments above */
          TXsleepmsec(msec, 0);
          if (msec < 50) msec++;
        }
      for (info = TXthreadInfoList;
           info && !TXthreadidsAreEqual(info->threadId, myThreadId);
           info = info->next)
        ;
      if (info)
        {
          if (info->prev) info->prev->next = info->next;
          else TXthreadInfoList = info->next;
          if (info->next) info->next->prev = info->prev;
          info->prev = info->next = NULL;
        }
      (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
      TXcriticalSectionExit(TXthreadInfoListCsect, pmbuf);
    }

  info = TXthreadInfoClose(info);

  return(ret);
}

/* ------------------------------------------------------------------------ */

#ifdef EPI_KILL_OTHER_THREADS_AT_EXIT

/*   In Linux 2.4, each thread is a separate process (and getpid()
 * returns differently in each thread; not true in Linux 2.6+).  Thus,
 * threads can sometimes continue to run after the main thread/process
 * exits.  There is a manager process (one per threaded process) that
 * tries to avoid this by killing remaining threads when the main
 * thread exits, but its actions can be delayed (till the to-be-killed
 * thread reaches a cancellation point?  a second or two anyway),
 * or not happen at all (e.g. if the manager process was SIGKILLed).
 *   We could try to get all threads to terminate simultaneously at
 * process exit by calling pthread_kill_other_threads_np().  But it
 * may hang if the manager process is dead.
 *   So we try to kill all other threads at exit ourselves, by tracking
 * all thread PIDs and sending them SIGKILL when exiting.
 *
 *   Linux 2.6+ seems much better behaved (NPTL instead of LinuxThreads?):
 * no separate manager process, and _exit() kills all threads.  Threads
 * are still separate processes (?), but do not show separately in ps
 * unless threads requested.
 */

#  ifndef TX_ATOMIC_TESTS_C
size_t
TXprintOtherThreadPids(TXbool inSig, char *buf, size_t bufSz)
/* Prints list of other thread PIDs that would be killed by
 * TXkillOtherThreads().
 * Returns length of `buf' used (not including nul terminator).
 * Thread-safe, iff !inSig.
 * Signal-safe, iff inSig.
 */
{
  char          *d, *e;
  PID_T         myPid = 0;
  int           i;
  TXthreadInfo  *info;

  d = buf;
  if (!buf || bufSz <= 0) bufSz = 0;
  e = buf + bufSz;
#    define BUF_LEFT    (d < e ? e - d : 0)

  myPid = getpid();                             /* do not affect TXgetpid() */

  if (inSig)
    {
      /* Cannot safely use csect, nor pause.  Alert others to our use,
       * and bail if list possibly being modified:
       */
      if (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {
          /* wtf could be another (read-only) signal user which is
           * benign here, not a writer which is not; cannot tell the
           * difference
           */
          d += htsnpf(d, BUF_LEFT, " ?");
        }
      else
        {
          for (info = TXthreadInfoList; info; info = info->next)
            {
              if (info->pid != myPid)
                d += htsnpf(d, BUF_LEFT, " %d", (int)info->pid);
            }
          (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
        }
    }
  else                                          /* not in signal */
    {
      if (!TXthreadInfoListCsect) TXinitThreads(TXPMBUFPN);
      if (TXcriticalSectionEnter(TXthreadInfoListCsect, TXPMBUFPN))
        {
          /* No need to inc refcount: we have csect so writers will
           * not interfere, and we are not modifying so (read-only)
           * signal contexts not affected.
           */
          for (info = TXthreadInfoList; info; info = info->next)
            {
              if (info->pid != myPid)
                d += htsnpf(d, BUF_LEFT, " %d", (int)info->pid);
            }
          TXcriticalSectionExit(TXthreadInfoListCsect, TXPMBUFPN);
        }
    }

  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (bufSz > 0) *(d < e ? d : e - 1) = '\0';   /* ensure nul-termination */

  return((size_t)(d - buf));
#    undef BUF_LEFT
}
#  endif /* TX_ATOMIC_TESTS_C */

/* ------------------------------------------------------------------------ */

void
TXkillOtherThreads(TXbool inSig)
/* Called on exit to try to ensure all threads terminate.
 * Only needed on some platforms (e.g. Linux 2.4-) that might not clean up
 * other threads on exit.
 * Signal-safe.
 */
{
#  define MAX_PIDS      256
  PID_T         myPid = 0, pids[MAX_PIDS];
  int           sig = SIGKILL;
  size_t        numPids = 0, i;
  TXthreadInfo  *info;

  /* pthread_kill_other_threads_np() should ideally take care of
   * things for us, but it may hang if the thread manager process is
   * dead.  So we kill threads ourselves.
   */

  /* NOTE: See csect/refcount comments in TXprintOtherThreadPids() */

  myPid = getpid();                             /* do not affect TXgetpid() */

  if (inSig)
    {
      if (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {
          /* probably writer; wtf could be signal reader */
        }
      else
        {
          for (info = TXthreadInfoList; info; info = info->next)
            if (info->pid != myPid)
              {
                /* Just copy PIDs first, to avoid time-consuming
                 * system call inside "csect" here:
                 */
                if (numPids < MAX_PIDS) pids[numPids++] = info->pid;
                else break;
              }
          (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
        }
    }
  else                                          /* not in signal */
    {
      if (!TXthreadInfoListCsect) TXinitThreads(TXPMBUFPN);
      if (TXcriticalSectionEnter(TXthreadInfoListCsect, TXPMBUFPN))
        {
          for (info = TXthreadInfoList; info; info = info->next)
            {
              /* Just copy PIDs first, to avoid time-consuming
               * system call inside csect here:
               */
              if (numPids < MAX_PIDS) pids[numPids++] = info->pid;
              else break;
            }
          TXcriticalSectionExit(TXthreadInfoListCsect, TXPMBUFPN);
        }
    }

  /* Now kill, outside the csect: */
  for (i = 0; i < numPids; i++)
    kill(pids[i], sig);

#  undef MAX_PIDS
}
#endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

/* ------------------------------------------------------------------------ */

int
TXcreatethread(pmbuf, name, func, arg, flags, stksz, threadp)
TXPMBUF         *pmbuf;         /* just for this call */
const char      *name;          /* (in) short thread name e.g. for putmsgs */
TXTHREADFUNC    *func;
void            *arg;
int             flags;          /* TXTHREADFLAG_... flags */
size_t          stksz;
TXTHREAD        *threadp;	/* (out, opt.) handle to thread */
/* Creates thread that calls func(arg).  Sets *threadp to thread handle.
 * Returns 0 on error.
 * Thread-safe.
 */
{
  int                   ret;
  TXATOMINT             oldCreated = TxThreadsCreated;
  TXthreadInfo          *info = NULL;
#ifdef _WIN32
  DWORD                 tid;
  HANDLE                th = NULL;
#elif defined(EPI_USE_PTHREADS)
  pthread_attr_t        attr;
  TXTHREAD              thread;
  int                   n;
  const char            *f;
#endif /* !_WIN32 && EPI_USE_PTHREADS */

  if (threadp) *threadp = TXTHREAD_NULL;

  if (!TXthreadInfoListCsect) TXinitThreads(pmbuf);

  /* Create info object.  Thread ID etc. will be filled out by thread: */
  if (!(info = TXthreadInfoOpen(pmbuf, name))) goto err;
  info->func = func;
  info->arg = arg;
  func = TXthreadWrapper;
  arg = info;

  /* Avoid thread/signal race: set TxThreadsCreated *before* thread start: */
  TxThreadsCreated = 1;

#ifdef _WIN32 /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
#  ifdef EPI_KILL_OTHER_THREADS_AT_EXIT
#    error EPI_KILL_OTHER_THREADS_AT_EXIT should not be needed under Windows
#  endif /* EPI_KILL_OTHER_THREADS_AT_EXIT */

  /* note: TXTHREADFLAG_SCOPE_PROCESS silently unsupported */
  /* use STACK_SIZE_PARAM_IS_A_RESERVATION so that `stksz' is
   * (hopefully) the total (virtual mem) stack size, not just
   * initially-committed mem.
   */
  th = CreateThread(NULL, stksz, func, arg, STACK_SIZE_PARAM_IS_A_RESERVATION,
                    &tid);
  if (!th)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Cannot CreateThread(): %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  info = NULL;                                  /* thread owns it now */
  if (flags & TXTHREADFLAG_DETACHED)
    {
      CloseHandle(th);
      th = NULL;
    }
  else if (threadp)
    *threadp = th;
  else
    {
      txpmbuf_putmsg(pmbuf, MWARN + UGE, __FUNCTION__,
                     "Internal error: Non-detached thread created without handle return parameter: closing handle to prevent leak");
      CloseHandle(th);
      th = NULL;
    }

#elif defined(EPI_USE_PTHREADS) /*  - - - - - - - - - - - - - - - - - - - - */

  if ((f = "attr_init", (n = pthread_attr_init(&attr))) != 0) goto perr;
  f = "attr_setscope";
  n = pthread_attr_setscope(&attr, (flags & TXTHREADFLAG_SCOPE_PROCESS) ?
                            PTHREAD_SCOPE_PROCESS :
#  ifdef PTHREAD_SCOPE_BOUND_NP
  /* Under Irix 6.5 PTHREAD_SCOPE_SYSTEM is real-time, may usurp device
   * drivers, and requires special privileges; use PTHREAD_SCOPE_BOUND_NP.
   * However older libpthread.so may not support it; fall back if needed:
   */
                            PTHREAD_SCOPE_BOUND_NP
#  else /* !PTHREAD_SCOPE_BOUND_NP */
                            PTHREAD_SCOPE_SYSTEM
#  endif /* !PTHREAD_SCOPE_BOUND_NP */
                            );
  if (n != 0 && !(flags & TXTHREADFLAG_SCOPE_PROCESS))
    n = pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
  if (n != 0) pthread_attr_init(&attr);
  if ((f = "attr_setstacksize", (n =
                (stksz ? pthread_attr_setstacksize(&attr,stksz) != 0 : 0))) ||
      (f = "attr_setdetachstate", (n =
             pthread_attr_setdetachstate(&attr,(flags & TXTHREADFLAG_DETACHED)
          ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE))) != 0 ||
      (f = "create", (n = pthread_create(&thread, &attr, func, arg))) != 0)
    {
    perr:
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot create thread: pthread_%s(): %s",
                     f, strerror(n));
      goto err;
    }
  info = NULL;                                  /* thread owns it now */
  if (threadp) *threadp = thread;

#else /* !_WIN32 && !EPI_USE_PTHREADS - - - - - - - - - - - - - - - - - - - */
  txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, NoThreadsPlatform);
  goto err;
#endif /* !_WIN32 && !EPI_USE_PTHREADS - - - - - - - - - - - - - - - - - - */

  ret = 1;                                      /* success */
  goto finally;

err:
  TxThreadsCreated = oldCreated;
  if (threadp) *threadp = TXTHREAD_NULL;
  ret = 0;
finally:
  info = TXthreadInfoClose(info);
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXwaitforthreadexit(TXPMBUF *pmbuf, TXTHREAD thread, double timeout,
                    TXTHREADRET *exitcodep)
/* Wait for `thread' to exit, and closes it (even on timeout).  If
 * `timeout' is non-negative, only waits that long.  Sets optional
 * `*exitcodep' to exit code of thread.  Returns 0 on error, 1 if
 * timeout reached, 2 if thread exited.  Thread-safe.
 */
{
#ifdef _WIN32
  int           ret = 0;
  DWORD         waitMs;
  double        timeoutMs;

  if (timeout < 0.0)
    waitMs = INFINITE;
  else
    {
      timeoutMs = timeout*1000.0;
      if (timeoutMs >= (double)EPI_UINT32_MAX)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "Timeout %g too long", timeout);
          goto err;
        }
      waitMs = (DWORD)timeoutMs;
    }
  switch (WaitForSingleObject(thread, waitMs))
    {
    case WAIT_OBJECT_0:
      if (exitcodep &&
          !GetExitCodeThread(thread, exitcodep))
        {
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                         "Cannot GetExitCodeThread(): %s",
                         TXstrerror(TXgeterror()));
          goto err;
        }
      ret = 2;
      break;
    case WAIT_TIMEOUT:
      if (exitcodep) *exitcodep = (TXTHREADRET)0;
      ret = 1;
      break;
    default:
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot WaitForSingleObject(): %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
  goto finally;

err:
  ret = 0;
finally:
  CloseHandle(thread);
  return(ret);
#elif defined(EPI_USE_PTHREADS)
  int           res, ret = 0;
  const char    *tryFunc = NULL;

  if (timeout < 0.0)
    {
      tryFunc = "pthread_join";
      do
        res = pthread_join(thread, exitcodep);
      while (res == EINTR);
    }
  else if (timeout == 0.0)
    {
#  ifdef EPI_HAVE_PTHREAD_TRYJOIN_NP
      tryFunc = "pthread_tryjoin_np";
      do
        res = pthread_tryjoin_np(thread, exitcodep);
      while (res == EINTR);
      if (res == EBUSY)
        {
          ret = 1;
          goto finally;
        }
#  elif defined(EPI_HAVE_PTHREAD_TIMEDJOIN_NP)
      goto timedJoin;
#  else /* !EPI_HAVE_PTHREAD_TRYJOIN_NP */
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot try to wait for thread non-blocking: %s",
                     TXunsupportedPlatform);
      goto err;
#  endif /* !EPI_HAVE_PTHREAD_TRYJOIN_NP */
    }
  else
    {
#  ifdef EPI_HAVE_PTHREAD_TIMEDJOIN_NP
      struct timespec   tm;

#    ifndef EPI_HAVE_PTHREAD_TRYJOIN_NP
    timedJoin:
#    endif /* EPI_HAVE_PTHREAD_TRYJOIN_NP */
      tryFunc = "pthread_timedjoin_np";
      if (timeout >= (double)EPI_OS_TIME_T_MAX)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                         "Timeout %g too long", timeout);
          goto err;
        }
      tm.tv_sec = (time_t)timeout;
      tm.tv_nsec = (long)((timeout - (double)tm.tv_sec)*1000000000.0);
      /* no EINTR return ever, sez man: */
      res = pthread_timedjoin_np(thread, exitcodep, &tm);
      if (res == ETIMEDOUT)
        {
          ret = 1;
          goto finally;
        }
#  else /* EPI_HAVE_PTHREAD_TIMEDJOIN_NP */
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot wait for thread with timeout: %s",
                     TXunsupportedPlatform);
      goto err;
#  endif /* EPI_HAVE_PTHREAD_TIMEDJOIN_NP */
    }

  if (res != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Cannot %s(): %s",
                     tryFunc, strerror(res));
    err:
      if (exitcodep) *exitcodep = (TXTHREADRET)0;
      ret = 0;
    }
  else
    ret = 2;
#  if defined(EPI_HAVE_PTHREAD_TRYJOIN_NP) || defined(EPI_HAVE_PTHREAD_TIMEDJOIN_NP)
finally:
#  endif
  return(ret);
#else /* !_WIN32 && !EPI_USE_PTHREADS */
  return(2);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
}

/* ------------------------------------------------------------------------ */

int
TXterminatethread(pmbuf, thread, exitcode)
TXPMBUF         *pmbuf;         /* (in) for this call */
TXTHREAD        thread;         /* (in) thread to terminate */
TXTHREADRET     exitcode;       /* (in) exit code to give it (if possible) */
/* Terminates `thread' immediately (if possible).  Note that pthreads may
 * ignore or defer the request.  Note that Windows may hard-kill the
 * thread, with possible corruption (see man page for TerminateThread()).
 * TXwaitforthreadexit() is preferred instead of this function.
 * Returns 0 on error, 1 if ok.
 */
{
  TXthreadInfo  *info = NULL;
  TXTHREADID    threadId;
  int           ret;

#ifdef _WIN32
  if (!(threadId = GetThreadId(thread)))
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot get thread id from handle %p: %s",
                     (void *)thread, TXstrerror(TXgeterror()));
      goto err;
    }
#else /* !_WIN32 */
  /* WTF assume thread id is thread; true for pthreads: */
  threadId = thread;
#endif /* !_WIN32 */

  /* Remove thread from list, or we might inadvertently kill another
   * unrelated process in the future, if thread's PID re-used:
   */
  if (!TXthreadInfoListCsect) TXinitThreads(pmbuf);
  if (TXcriticalSectionEnter(TXthreadInfoListCsect, pmbuf))
    {
      int       msec = 0;

      /* See other csect usage for comments on this loop */
      while (TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 0, 1) != 0)
        {
          TXsleepmsec(msec, 0);
          if (msec < 50) msec++;
        }
      for (info = TXthreadInfoList;
           info && !TXthreadidsAreEqual(info->threadId, threadId);
           info = info->next)
        ;
      if (info)                                 /* found it */
        {
          if (info->prev) info->prev->next = info->next;
          else TXthreadInfoList = info->next;
          if (info->next) info->next->prev = info->prev;
          info->prev = info->next = NULL;
        }
      (void)TX_ATOMIC_COMPARE_AND_SWAP(&TXthreadInfoListRefCount, 1, 0);
      TXcriticalSectionExit(TXthreadInfoListCsect, pmbuf);
    }

  /* Now actually kill thread: */
#ifdef _WIN32
  if (!TerminateThread(thread, exitcode))
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot TerminateThread(): %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
#elif defined(EPI_USE_PTHREADS)
  (void)exitcode;
  if (pthread_cancel(thread) != 0)
    {
      txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                     "Cannot pthread_cancel(): %s",
                     TXstrerror(TXgeterror()));
      goto err;
    }
#endif /* !_WIN32 && EPI_USE_PTHREADS */

  ret = 1;                                      /* success */
  goto finally;

err:
  ret = 0;
finally:
  info = TXthreadInfoClose(info);
  return(ret);
}

/* ------------------------------------------------------------------------ */

#if !defined(_WIN32) && defined(EPI_USE_PTHREADS)
TXthreadAsyncId
TXgetCurrentThreadAsyncId(void)
/* Returns a unique thread id, not necessarily compatible with TXTHREADID.
 * Thread-safe, async-signal safe (unlike pthread_self()).
 * Will not call putmsg() (assumed by vxPutmsgBuf.c).
 */
{
#ifdef EPI_HAVE_SYSCALL_GETTID
  return((TXthreadAsyncId)syscall(SYS_gettid));
#else
  /* should hopwfully be LinuxThreads, where each thread has unique PID */
  return(getpid());
#endif
}
#endif /* !_WIN32 && EPI_USE_PTHREADS */

/* ------------------------------------------------------------------------ */

int
TXgetCurrentThread(int dup, TXTHREAD *thread)
/* Sets `*thread' to current thread handle, duping if `dup' (must be
 * closed then).
 * Returns 0 on error.
 */
{
#ifdef _WIN32
  if (dup)
    {
      if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                           GetCurrentProcess(), thread, 0, FALSE,
                           DUPLICATE_SAME_ACCESS))
        {
          *thread = TXTHREAD_NULL;
          putmsg(MERR, __FUNCTION__, "DuplicateHandle() failed: %s",
                 TXstrerror(TXgeterror()));
          return(0);
        }
      return(1);
    }
  *thread = GetCurrentThread();
  return(1);
#elif defined(EPI_USE_PTHREADS)
  /* TXTHREAD is pthread_t; no dup needed */
  (void)dup;
  *thread = pthread_self();
  return(1);
#else /* !_WIN32 && ! EPI_USE_PTHREADS */
  (void)dup;
  *thread = TXTHREAD_NULL;
  putmsg(MERR, __FUNCTION__, "%s", TXunsupportedPlatform);
  return(0);
#endif /* !_WIN32 && ! EPI_USE_PTHREADS */
}

/* ------------------------------------------------------------------------ */

int
TXthreadIsAlive(TXTHREAD thread)
/* Returns nonzero if `thread' is alive.
 */
{
#ifdef _WIN32
  DWORD res;

  switch (res = WaitForSingleObject(thread, 0))
    {
    case WAIT_TIMEOUT:  return(1);
    case WAIT_OBJECT_0: return(0);
    default:
      putmsg(MERR, __FUNCTION__, "WaitForSingleObject() returned 0x%x",
             (int)res);
      return(0);
    }
#elif defined(EPI_USE_PTHREADS)
  return(pthread_kill(thread, 0) == 0);
#else /* !_WIN32 && ! EPI_USE_PTHREADS */
  (void)thread;
  putmsg(MERR, __FUNCTION__, "%s", TXunsupportedPlatform);
  return(0);
#endif /* !_WIN32 && ! EPI_USE_PTHREADS */
}

/* ======================================================================== */

#ifndef TX_ATOMIC_TESTS_C

struct TXcriticalSection_tag
{
  TXcriticalSectionType type;
  TXATOMINT             lockDepth;
#  ifdef _WIN32
  CRITICAL_SECTION      csect;
#  elif defined(EPI_USE_PTHREADS)
  pthread_mutex_t       mutex;
#  endif /* EPI_USE_PTHREADS */
};

TXcriticalSection *
TXcriticalSectionOpen(TXcriticalSectionType type, TXPMBUF *pmbuf)
/* Creates a critical-section object.  Reports errors to `pmbuf',
 * but does not clone it: this is a cross-thread object, and TXPMBUF
 * is not cross-thread-safe.
 * Returns alloced object, or NULL on error.
 */
{
  TXcriticalSection     *cs = NULL;

  switch (type)
    {
    case TXcriticalSectionType_Normal:
    case TXcriticalSectionType_ErrorCheck:
    case TXcriticalSectionType_Recursive:
      break;
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Unknown TXcriticalSectionType %d", (int)type);
      goto err;
    }
  if (!(cs = TX_NEW(pmbuf, TXcriticalSection))) goto err;
  cs->type = type;
  cs->lockDepth = 0;
#  ifdef _WIN32
  InitializeCriticalSection(&cs->csect);
#  elif defined(EPI_USE_PTHREADS)
  {
    pthread_mutexattr_t attr;
    TXERRTYPE           errNum;
    int                 mutexType, failed = 0;

    if ((errNum = pthread_mutexattr_init(&attr)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "pthread_mutexattr_init() failed: %s",
                       TXstrerror(errNum));
        failed = 1;
        goto err;
      }
    switch (cs->type)
      {
      case TXcriticalSectionType_Normal:
      default:
        mutexType = PTHREAD_MUTEX_NORMAL;
        break;
      case TXcriticalSectionType_ErrorCheck:
        mutexType = PTHREAD_MUTEX_ERRORCHECK;
        break;
      case TXcriticalSectionType_Recursive:
        mutexType = PTHREAD_MUTEX_RECURSIVE;
        break;
      }
    if ((errNum = pthread_mutexattr_settype(&attr, mutexType)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "pthread_mutexattr_settype() failed: %s",
                       TXstrerror(errNum));
        failed = 1;
      }
    else if ((errNum = pthread_mutex_init(&cs->mutex, &attr)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "pthread_mutex_init() failed: %s",
                       TXstrerror(errNum));
        failed = 1;
      }
    if ((errNum = pthread_mutexattr_destroy(&attr)) != 0)
      txpmbuf_putmsg(pmbuf, MWARN, __FUNCTION__,
                     "pthread_mutexattr_destroy() failed (ignored): %s",
                     TXstrerror(errNum));
    if (failed) goto err;
  }
#  else /* !_WIN32 && !EPI_USE_PTHREADS */
#    error Not implemented for non-Windows and non-pthreads
  /* see also functions below */
#  endif /* !_WIN32 && !EPI_USE_PTHREADS */
  goto finally;

err:
  /* We assume we have not initialized `cs->csect/mutex' if any error: */
  cs = TXfree(cs);
finally:
  return(cs);
}

/* ------------------------------------------------------------------------ */

TXcriticalSection *
TXcriticalSectionClose(TXcriticalSection *cs, TXPMBUF *pmbuf)
/* Closes and frees a critical section object.  Object should not be
 * entered/locked currently.  Reports errors to `pmbuf'.
 */
{
  TXATOMINT_NV  prevLockDepth;

  if (!cs) goto finally;

  switch (cs->type)
    {
    case TXcriticalSectionType_ErrorCheck:
    case TXcriticalSectionType_Recursive:
      if ((prevLockDepth = cs->lockDepth) > 0)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                  "Will not close critical section object: Lock depth %d > 0",
                         (int)prevLockDepth);
          goto err;
        }
      /* now a race from here to DeleteCriticalSection(), but at least
       * we checked...
       */
      break;
    default:
      break;
    }

#  ifdef _WIN32
  DeleteCriticalSection(&cs->csect);            /* returns void */
#  elif defined(EPI_USE_PTHREADS)
  {
    TXERRTYPE   errNum;

    if ((errNum = pthread_mutex_destroy(&cs->mutex)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
          "Cannot close critical section object: pthread_mutex_destroy(): %s",
                       TXstrerror(errNum));
        goto err;
      }
  }
#  endif /* EPI_USE_PTHREADS */
  cs = TXfree(cs);

finally:
err:
  return(NULL);
}

/* ------------------------------------------------------------------------ */

#undef TXcriticalSectionEnter

int
TXcriticalSectionEnter(TXcriticalSection *cs, TXPMBUF *pmbuf
                       TX_CRITICAL_SECTION_TRACE_PARAMS)
/* Returns nonzero on success (call ...Exit() when done), or 0 on error
 * (do not call ...Exit()).  Errors reported to `pmbuf' (not cloned).
 * May safely be called with NULL `cs' (will return error).
 */
{
  int           ret;
  TXATOMINT_NV  prevLockDepth;

  if (!cs)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     TXnoCsectFmt, "enter", callingFunc, line);
      goto err;
    }

  switch (cs->type)
    {
    case TXcriticalSectionType_ErrorCheck:
    case TXcriticalSectionType_Recursive:
      /* If `lockDepth < 0', EnterCriticalSection() might deadlock below: */
      if ((prevLockDepth = cs->lockDepth) < 0)
        {
          txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
        "Will not enter critical section in %s at line %d: Lock depth %d < 0",
                         callingFunc, line, (int)prevLockDepth);
          goto err;
        }
      /* now a race from here to EnterCriticalSection(), but at least
       * we checked...
       */
      break;
    default:
      break;
    }

#  ifdef _WIN32
  /* EnterCriticalSection() returns void, but can raise exception.
   * Sigh.  At least it explicitly allows recursion, so we do not have
   * to inc `lockDepth' outside the critical section (would have to
   * call it `tryCount' then).  Can also potentially deadlock if
   * LeaveCriticalSection() called without a lock, says MSDN; hence
   * check above.
   */
  EnterCriticalSection(&cs->csect);
#  elif defined(EPI_USE_PTHREADS)
  {
    TXERRTYPE   errNum;

    if ((errNum = pthread_mutex_lock(&cs->mutex)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot enter critical section in %s at line %d: pthread_mutex_lock() failed: %s",
                       callingFunc, line, TXstrerror(errNum));
        goto err;
      }
  }
#  endif /* EPI_USE_PTHREADS */

  prevLockDepth = TX_ATOMIC_INC(&cs->lockDepth);
  switch (cs->type)
    {
    case TXcriticalSectionType_ErrorCheck:
      if (prevLockDepth != 0)   /* already locked, or unlocked too much */
        {
          TX_ATOMIC_DEC(&cs->lockDepth);
#  ifdef _WIN32
          LeaveCriticalSection(&cs->csect);
#  elif defined(EPI_USE_PTHREADS)
          {
            TXERRTYPE   errNum;
            if ((errNum = pthread_mutex_unlock(&cs->mutex)) != 0)
              txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                             "Cannot exit critical section in %s at line %d after lock depth check failed: pthread_mutex_unlock() failed: %s",
                             callingFunc, line, TXstrerror(errNum));
          }
#  endif /* EPI_USE_PTHREADS */
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
   "Will not enter critical section in %s at line %d: Lock depth was %d != 0",
                         callingFunc, line, (int)prevLockDepth);
          goto err;
        }
      break;
    case TXcriticalSectionType_Recursive:
      /* Repeat safety check from before the csect: */
      if (prevLockDepth < 0)                    /* unlocked too much */
        {
          TX_ATOMIC_DEC(&cs->lockDepth);
#  ifdef _WIN32
          LeaveCriticalSection(&cs->csect);
#  elif defined(EPI_USE_PTHREADS)
          {
            TXERRTYPE   errNum;
            if ((errNum = pthread_mutex_unlock(&cs->mutex)) != 0)
              txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                             "Cannot exit critical section in %s at line %d after lock depth check failed: pthread_mutex_unlock() failed: %s",
                             callingFunc, line, TXstrerror(errNum));
          }
#  endif /* EPI_USE_PTHREADS */
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
   "Will not enter critical section in %s at line %d: Lock depth was %d < 0",
                         callingFunc, line, (int)prevLockDepth);
          goto err;
        }
      break;
    case TXcriticalSectionType_Normal:
    default:
      break;
    }
  ret = 1;                                      /* success */
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

#define TXcriticalSectionEnter(cs, pmbuf)       \
  TXcriticalSectionEnter(cs, pmbuf TX_CRITICAL_SECTION_TRACE_ARGS)

/* ------------------------------------------------------------------------ */

#  undef TXcriticalSectionExit

int
TXcriticalSectionExit(TXcriticalSection *cs, TXPMBUF *pmbuf
                      TX_CRITICAL_SECTION_TRACE_PARAMS)
/* Call after successful call to ...Enter().
 * May safely be called with NULL `cs' (will return error).
 * Returns nonzero on success, 0 on error (reported to `pmbuf', not cloned).
 */
{
  int           ret;
  TXATOMINT_NV  prevLockDepth;

  if (!cs)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     TXnoCsectFmt, "exit", callingFunc, line);
      goto err;
    }

  prevLockDepth = TX_ATOMIC_DEC(&cs->lockDepth);

  switch (cs->type)
    {
    case TXcriticalSectionType_ErrorCheck:
    case TXcriticalSectionType_Recursive:
      /* Check `lockDepth' before calling LeaveCriticalSection(): */
      if (prevLockDepth <= 0)                   /* was not locked */
        {
          TX_ATOMIC_INC(&cs->lockDepth);
          txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
    "Will not exit critical section in %s at line %d: Lock depth was %d <= 0",
                         callingFunc, line, (int)prevLockDepth);
          goto err;
        }
      break;
    case TXcriticalSectionType_Normal:
    default:
      break;
    }

#  ifdef _WIN32
  LeaveCriticalSection(&cs->csect);             /* returns void */
#  elif defined(EPI_USE_PTHREADS)
  {
    TXERRTYPE   errNum;

    if ((errNum = pthread_mutex_unlock(&cs->mutex)) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot exit critical section in %s at line %d: pthread_mutex_unlock() failed: %s",
                       callingFunc, line, TXstrerror(errNum));
        goto err;
      }
  }
#  endif /* EPI_USE_PTHREADS */

  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

#  define TXcriticalSectionExit(cs, pmbuf)      \
  TXcriticalSectionExit(cs, pmbuf TX_CRITICAL_SECTION_TRACE_ARGS)

/* ------------------------------------------------------------------------ */

int
TXcriticalSectionDepth(TXcriticalSection *cs)
{
  return(cs->lockDepth);
}

#endif /* !TX_ATOMIC_TESTS_C */
