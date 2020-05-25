#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include "txtypes.h"
#include "cgi.h"
#include "txthreads.h"
#define TX_SYSDEP_C
#include "texint.h"


#define MUTEX_TRACE_ARGS        , const char *file, int line


static TXMUTEX *
TXmutexCloseActual(TXMUTEX *mutex, int sysInitFailed)
{
#ifdef _WIN32
  if (!mutex) return(TXMUTEXPN);
  if (mutex->sysObj != NULL && !sysInitFailed)
    {
      CloseHandle(mutex->sysObj);
      mutex->sysObj = NULL;
    }
#elif defined(EPI_USE_PTHREADS)
  int   n;

  if (!mutex) return(TXMUTEXPN);
  if (!sysInitFailed &&
      (n = pthread_mutex_destroy(&mutex->sysObj)) != 0)
    txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
                   "Cannot pthread_mutex_destroy(): %s", strerror(n));
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
  mutex->pmbuf = txpmbuf_close(mutex->pmbuf);
  mutex = TXfree(mutex);
  return(TXMUTEXPN);
}

TXMUTEX *
TXmutexOpen(TXPMBUF *pmbuf, const char *name)
/* Creates a mutex for thread synchronization.
 * NOTE: Mutex is *not* recursively lockable 2+ times by same thread:
 * may deadlock (Solaris 2.6?), undefined behavior elsewhere for pthreads.
 * `pmbuf' attached to.  `name' only valid for Windows.
 * NOTE: `pmbuf' should be TXPMBUFPN or TXPMBUF_SUPPRESS: non-special
 * `pmbuf' is not cross-thread-safe, and a mutex is to be used across threads.
 * Thread-safe.
 */
{
  TXMUTEX       *mutex = TXMUTEXPN;

  if (pmbuf != TXPMBUFPN && pmbuf != TXPMBUF_SUPPRESS)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                     "Internal error: `pmbuf' must be TXPMBUFPN or TXPMBUF_SUPPRESS for cross-thread safety");
      return(TXMUTEXPN);
    }

  mutex = TX_NEW(pmbuf, TXMUTEX);
  if (!mutex) return(TXMUTEXPN);
  if (TXApp) mutex->trace = TXApp->traceMutex;
  mutex->pmbuf = txpmbuf_open(pmbuf);
  /* rest cleared by calloc() */
  {
#ifdef _WIN32
    SECURITY_ATTRIBUTES secAttr;
    SECURITY_DESCRIPTOR secDesc;
    char                *oname = CHARPN;

    if (!tx_initsec(pmbuf, &secDesc, &secAttr))
      return(TXmutexCloseActual(mutex, 1));
    if (!(oname = TXGlobalName(pmbuf, name)) && name)
      return(TXmutexCloseActual(mutex, 1));
    mutex->sysObj = CreateMutex(&secAttr,
                                FALSE,          /* initially unlocked */
                                oname);
    if (mutex->sysObj == NULL)                  /* failed */
      {
        TX_PUSHERROR();
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Cannot CreateMutex(): %s",
                       /* wtf TXstrerror() not thread-safe in Windows */
                       TXgetOsErrName(TXgeterror(), "?"));
        mutex = TXmutexCloseActual(mutex, 1);
        TX_POPERROR();
      }
    if (oname)
      {
        TX_PUSHERROR();
        oname = TXfree(oname);
        TX_POPERROR();
      }
#elif defined(EPI_USE_PTHREADS) /* - - - - - - - - - - - - - - - - - - - - */
    int res;

    if (name)
      {
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                       "Cannot create a named mutex: %s",
                       TXunsupportedPlatform);
        return(TXmutexCloseActual(mutex, 1));
      }
    /* Would like to try PTHREAD_MUTEX_RECURSIVE so a thread can lock
     * recursively (if desired for modularity/safety).  But that is
     * not supported everywhere, and does not seem safely emulatable;
     * also, Solaris 8 Bugid 4288299 appears to indicate that
     * recursive mutexes could be broken on customer boxes.  So stick
     * with default (non-recursive) mutexes:
     */
    if ((res = pthread_mutex_init(&mutex->sysObj, (pthread_mutexattr_t*)NULL))
        != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__, "Cannot create mutex: %s",
                       TXstrerror(res));
        return(TXmutexCloseActual(mutex, 1));
      }
#else /* !_WIN32 && !EPI_USE_PTHREADS - - - - - - - - - - - - - - - - - - - */
    if (name)
      {
        txpmbuf_putmsg(pmbuf, MERR + UGE, __FUNCTION__,
                       "Cannot create a named mutex: %s",
                       TXunsupportedPlatform);
        return(TXmutexClose(mutex));
      }
    /* Use spinlock, which is wasteful and may break (especially if
     * !TX_ATOMIC_THREAD_SAFE).  But we do not bother to issue a
     * warning, because the fact that we do not have threads means
     * there probably is not going to be a race condition we need to
     * deconflict with this mutex.
     */
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
    return(mutex);
  }
}

TXMUTEX *
TXmutexClose(TXMUTEX *mutex)
/* Thread-safe. */
{
  return(TXmutexCloseActual(mutex, 0));
}

#undef TXmutexLock
int
TXmutexLock(TXMUTEX *mutex, double waitSec MUTEX_TRACE_ARGS)
/* Blocks current thread while waiting for exclusive lock on `mutex'.
 * Waits up to `waitSec' seconds, or forever if less than 0.
 * NOTE: Non-forever times are inefficient under non-Windows.
 * Returns -1 on error, 0 on timeout, 1 if lock obtained.
 * NOTE: Mutex is *not* recursively lockable 2+ times by same thread:
 * may deadlock (Solaris 2.6?), undefined behavior elsewhere for pthreads.
 * Thread-safe.
 */
{
  TXATOMINT     prevLockCount;
  double        curTime;
  char          tmp[32];
#ifdef _WIN32 /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  int           res;
  const char    *handleNames[1];

  handleNames[0] = "mutex";
  switch (res = TXezWaitForMultipleObjects(mutex->pmbuf, 0, __FUNCTION__,
                                           &mutex->sysObj, 1, handleNames,
                                           waitSec, TXbool_False))
    {
    case WAIT_ABANDONED:                        /* wtf other guy died */
    case WAIT_OBJECT_0:			        /* now have exclusive lock */
      break;
    case WAIT_TIMEOUT:
      goto gotTimeout;
    default:
      txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
        "Cannot WaitForSingleObject(): Returned unknown value 0x%x; error %s",
                     /* WTF TXstrerror() thread-unsafe */
                     res, TXgetOsErrName(TXgeterror(), "?"));
      return(-1);                               /* error */
    }
#elif defined(EPI_USE_PTHREADS) /* - - - - - - - - - - - - - - - - - - - - */
  int           res;
  const char    *try;
  long          nextWaitMs = 125L, thisWaitMs;

  if (waitSec < 0.0)                            /* infinite wait */
    {
      try = "";
      do
        res = pthread_mutex_lock(&mutex->sysObj);
      while (res == EINTR);
    }
  else                                          /* timed wait */
    {
      try = "try";
      do
        {
          do
            res = pthread_mutex_trylock(&mutex->sysObj);
          while (res == EINTR);
          if (res == EBUSY)                     /* other guy still has lock */
            {
              if (waitSec <= 0.0) goto gotTimeout;
              thisWaitMs = TX_MIN((long)(waitSec*1000.0), nextWaitMs);
              if (nextWaitMs < 1000L) nextWaitMs <<= 1;
              waitSec -= ((double)(thisWaitMs - TXsleepmsec(thisWaitMs, 0)))
                / 1000.0;
            }
        }
      while (res == EBUSY);
    }
  if (res != 0)
    {
      txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
                     "Cannot pthread_mutex_%slock(): %s", try,
                     strerror(res));
      return(-1);                               /* error */
    }
#else /* !_WIN32 && !EPI_USE_PTHREADS */
  /* Spin lock.  Slow and probably unsafe (especially if
   * TX_ATOMIC_THREAD_SAFE not defined):
   */
  long          nextWaitMs = 125L, thisWaitMs;

#  error Check this code
  while (TX_ATOMIC_INC(&mutex->spinLock) != 0)
    {
      TX_ATOMIC_DEC(&mutex->spinLock);          /* undo our attempt */
      /* Sleep and try again: */
      if (waitSec < 0.0)                        /* infinite wait */
        TXsleepmsec(nextWaitMs, 0);
      else                                      /* timed wait */
        {
          if (waitSec <= 0.0) goto gotTimeout;
          thisWaitMs = TX_MIN((long)(waitSec*1000.0), nextWaitMs);
          waitSec -= ((double)(thisWaitMs - TXsleepmsec(thisWaitMs, 0)))
            / 1000.0;
        }
      if (nextWaitMs < 1000L) nextWaitMs <<= 1; /* slow down */
    }
#endif /* !_WIN32 && !EPI_USE_PTHREADS */

  /* Success: log it and return: */
  prevLockCount = TX_ATOMIC_INC(&mutex->lockCount);
  if (mutex->trace & 0x1)
    curTime = TXgettimeofday();
  else
    curTime = 0.0;
  /* Even though Windows allows recursive locking, other platforms do
   * not, and we do not currently use recursive locks, so any
   * recursive lock is bad; always report it regardless of tracing:
   */
  if (prevLockCount > 0 /* && (mutex->trace & 0x2) */)
    {
      if (mutex->trace & 0x1)
        htsnpf(tmp, sizeof(tmp), " %1.6kfs ago",
               (double)(curTime - mutex->lastLockerTime));
      else
        *tmp = '\0';
      txpmbuf_putmsg(mutex->pmbuf, MERR + UGE, __FUNCTION__,
                     "Recursive mutex lock (depth %d) obtained at %s:%d: previous lock held at %s:%d%s",
                     (int)prevLockCount + 1, TXbasename(file),
                     line, TXbasename(mutex->lastLockerFile),
                     mutex->lastLockerLine, tmp);
    }
  mutex->lastLockerFile = file;
  mutex->lastLockerLine = line;
  mutex->lastLockerTime = curTime;
  return(1);                                    /* success */

gotTimeout:
  if (mutex->trace & 0x1)
    {
      curTime = TXgettimeofday();
      htsnpf(tmp, sizeof(tmp), " %1.6kfs ago",
             (double)(curTime - mutex->lastLockerTime));
    }
  else
    strcpy(tmp, "");
  txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
 "Mutex lock attempt timeout (depth %d) at %s:%d: previous lock%s at %s:%d%s",
                 (int)mutex->lockCount, TXbasename(file), line,
                 (mutex->lockCount > 0 ? " held" : " was"),
                 TXbasename(mutex->lastLockerFile),
                 mutex->lastLockerLine, tmp);
  return(0);                                    /* timeout */
}
#define TXmutexLock	ErrorRedefineMacro

#undef TXmutexUnlock
int
TXmutexUnlock(TXMUTEX *mutex MUTEX_TRACE_ARGS)
/* Releases lock after successful TXmutexLock() by current thread.
 * Returns 0 on error, 1 if lock released.
 * Thread-safe.
 */
{
#if defined(_WIN32) || defined(EPI_USE_PTHREADS)
  int           res;
#else /* !(_WIN32 || EPI_USE_PTHREADS) */
  TXATOMINT     prevSpinLock;
#endif /* !(_WIN32 || EPI_USE_PTHREADS) */

  /* We must dec lock count and log *before* releasing the lock, to
   * avoid race:
   */
  (void)TX_ATOMIC_DEC(&mutex->lockCount);
  mutex->lastUnlockerFile = file;
  mutex->lastUnlockerLine = line;
  if (mutex->trace & 0x1)
    mutex->lastUnlockerTime = TXgettimeofday();
  else
    mutex->lastUnlockerTime = 0.0;

#ifdef _WIN32
  if (!(res = ReleaseMutex(mutex->sysObj)))
    {
      txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
                     "Cannot ReleaseMutex(): %s",
                     TXstrerror(TXgeterror()));
      return(0);
    }
#elif defined(EPI_USE_PTHREADS)
  do
    res = pthread_mutex_unlock(&mutex->sysObj);
  while (res == EINTR);
  if (res != 0)
    {
      txpmbuf_putmsg(mutex->pmbuf, MERR, __FUNCTION__,
                     "Cannot pthread_mutex_unlock(): %s",
                     strerror(res));
      return(0);
    }
#else /* !_WIN32 && !EPI_USE_PTHREADS */
  prevSpinLock = TX_ATOMIC_DEC(&mutex->spinLock);
  if (prevSpinLock <= 0)
    txpmbuf_putmsg(mutex->pmbuf, MWARN, __FUNCTION__,
                   "Unexpected spinlock value %d",
                   (int)prevSpinLock);
#endif /* !_WIN32 && !EPI_USE_PTHREADS */
  return(1);                                    /* success */
}
#define TXmutexUnlock	ErrorRedefineMacro
