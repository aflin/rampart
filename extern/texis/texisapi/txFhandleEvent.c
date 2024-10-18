#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <errno.h>
#include <string.h>
#include "texint.h"


#ifndef _WIN32
struct TXFHANDLE_EVENT_tag
{
  TXFHANDLE     pipe[TXpipeEnd_NUM];
  TXbool        signalled;
};
#endif /* !_WIN32 */

TXFHANDLE_EVENT *
TXfhandleEventOpen(TXPMBUF *pmbuf)
/* `pmbuf' is only used for messages here; it is not cloned/saved.
 */
{
#ifdef _WIN32
  /* Manual-reset for consistency with Unix implementation limitations: */
  return(opentxevent(pmbuf, TXbool_True /* manual-reset */));
#else /* !_WIN32 */
  TXFHANDLE_EVENT	*fEvent;

  fEvent = TX_NEW(pmbuf, TXFHANDLE_EVENT);
  if (!fEvent) goto err;
  fEvent->pipe[TXpipeEnd_Read] = fEvent->pipe[TXpipeEnd_Write] =
    TXFHANDLE_INVALID_VALUE;
  /* rest cleared by calloc() */
  /* We prefer setting O_CLOEXEC atomically at open via pipe2(),
   * rather than pipe() + later fcntl(FD_CLOEXEC): not only is latter
   * a second system call, but more importantly it creates a race
   * where another thread might fork() + exec() between our
   * pipe() and fcntl(FD_CLOEXEC):
   */
#  ifdef EPI_HAVE_PIPE2
  if (pipe2(fEvent->pipe, (O_NONBLOCK | O_CLOEXEC)) != 0)
#  else /* !EPI_HAVE_PIPE2 */
    if (pipe(fEvent->pipe) != 0)
#  endif /* !EPI_HAVE_PIPE2 */
      {
        txpmbuf_putmsg(pmbuf, MERR + FOE, __FUNCTION__,
                       "Cannot create pipe: %s", TXstrerror(TXgeterror()));
        goto err;
      }
#  ifndef EPI_HAVE_PIPE2
  {
    int flags0, flags1;

    flags0 = fcntl(fEvent->pipe[TXpipeEnd_Read], F_GETFL);
    flags1 = fcntl(fEvent->pipe[TXpipeEnd_Write], F_GETFL);
    if (flags0 == -1 || flags1 == -1)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot get pipe status flags: %s",
                       TXstrerror(TXgeterror()));
        goto err;
      }
    flags0 |= O_NONBLOCK;
    flags1 |= O_NONBLOCK;
    if (fcntl(fEvent->pipe[TXpipeEnd_Read], F_SETFL, flags0) != 0 ||
        fcntl(fEvent->pipe[TXpipeEnd_Write], F_SETFL, flags1) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot set pipe status flags: %s",
                       TXstrerror(TXgeterror()));
        goto err;
      }

    flags0 = fcntl(fEvent->pipe[TXpipeEnd_Read], F_GETFD);
    flags1 = fcntl(fEvent->pipe[TXpipeEnd_Write], F_GETFD);
    if (flags0 == -1 || flags1 == -1)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot get pipe descriptor flags: %s",
                       TXstrerror(TXgeterror()));
        goto err;
      }
    flags0 |= FD_CLOEXEC;
    flags1 |= FD_CLOEXEC;
    if (fcntl(fEvent->pipe[TXpipeEnd_Read], F_SETFD, flags0) != 0 ||
        fcntl(fEvent->pipe[TXpipeEnd_Write], F_SETFD, flags1) != 0)
      {
        txpmbuf_putmsg(pmbuf, MERR, __FUNCTION__,
                       "Cannot set pipe descriptor flags: %s",
                       TXstrerror(TXgeterror()));
        goto err;
      }
  }
#  endif /* !EPI_HAVE_PIPE2 */
  goto finally;

err:
  fEvent = TXfhandleEventClose(pmbuf, fEvent);
finally:
  return(fEvent);
#endif /* !_WIN32 */
}

TXFHANDLE_EVENT *
TXfhandleEventClose(TXPMBUF *pmbuf, TXFHANDLE_EVENT *fEvent)
{
  (void)pmbuf;
#ifdef _WIN32
  return(closetxevent(fEvent));
#else /* !_WIN32 */
  if (fEvent)
    {
      if (fEvent->pipe[TXpipeEnd_Read] != TXFHANDLE_INVALID_VALUE)
        close(fEvent->pipe[TXpipeEnd_Read]);
      if (fEvent->pipe[TXpipeEnd_Write] != TXFHANDLE_INVALID_VALUE)
        close(fEvent->pipe[TXpipeEnd_Write]);
      fEvent = TXfree(fEvent);
    }
  return(NULL);
#endif /* !_WIN32 */
}

TXbool
TXfhandleEventSignal(TXPMBUF *pmbuf, TXFHANDLE_EVENT *fEvent)
/* Returns false on error.
 */
{
#ifdef _WIN32
  return(TXsignalevent(pmbuf, fEvent));
#else /* !_WIN32 */
  int           tries;
  TXbool        ret;

  /* Do not fill up the OS pipe buffer: */
  if (fEvent->signalled) goto ok;

  for (tries = 0; tries < 25; tries++)
    {
      TXseterror(0);
      switch (write(fEvent->pipe[TXpipeEnd_Write], "x", 1))
        {
        case -1:
          if (TXgeterror() == TXERR_EINTR) break;       /* try again */
          /* else fall through and report error: */
        case 0:
          /* We only write one byte; OS buffer should have sufficed: */
          txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                         "Cannot write to pipe: %s",
                         TXstrerror(TXgeterror()));
          goto err;
        default:                                /* presumably 1 byte written*/
          fEvent->signalled = TXbool_True;
          goto ok;
        }
    }
  txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                 "Cannot write to pipe: Too many interruptions");
  goto err;

ok:
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
#endif /* !_WIN32 */
}

TXbool
TXfhandleEventClear(TXPMBUF *pmbuf, TXFHANDLE_EVENT *fEvent)
/* Returns false on error.
 */
{
#ifdef _WIN32
  if (!ResetEvent((HANDLE)fEvent))
    {
      txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                     "Cannot ResetEvent: %s", TXstrerror(TXgeterror()));
      return(TXbool_False);
    }
  return(TXbool_True);
#else /* !_WIN32 */
  byte          buf[1024];
  int           tries;
  TXbool        ret;

  if (!fEvent->signalled) goto ok;

  for (tries = 0; tries < 25; tries++)
    {
      TXseterror(0);
      /* We only write 1 byte per signal, but try to read a bunch
       * to clear it, in case backed up:
       */
      switch (read(fEvent->pipe[TXpipeEnd_Read], buf, sizeof(buf)))
        {
        case -1:
          if (TXgeterror() == TXERR_EINTR) break;       /* try again */
          txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
                         "Cannot read from pipe: %s",
                         TXstrerror(TXgeterror()));
          goto err;
        case 0:                                 /* EOF */
        default:                                /* presumably 1 byte read */
          fEvent->signalled = TXbool_False;
          goto ok;
        }
    }
  txpmbuf_putmsg(pmbuf, MERR + FWE, __FUNCTION__,
         "Cannot write to pipe: Too many interruptions");
  goto err;

ok:
  ret = TXbool_True;
  goto finally;

err:
  ret = TXbool_False;
finally:
  return(ret);
           
#endif /* !_WIN32 */
}

TXFHANDLE
TXfhandleEventGetWaitableFhandle(TXPMBUF *pmbuf, TXFHANDLE_EVENT *fEvent)
{
  (void)pmbuf;
#ifdef _WIN32
  return((TXFHANDLE)fEvent);
#else /* !_WIN32 */
  return(fEvent->pipe[TXpipeEnd_Read]);
#endif /* !_WIN32 */
}
