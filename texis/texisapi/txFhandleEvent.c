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
  TXFHANDLE     pipe[2];
  int   signalled;
};
#endif /* !_WIN32 */

TXFHANDLE_EVENT *
TXfhandleEventOpen(void)
{
#ifdef _WIN32
  /*Manual-reset for consistency with Unix implementation limitations:*/
  return(opentxevent(1));
#else /* !_WIN32 */
  TXFHANDLE_EVENT	*fEvent;

  fEvent = TX_NEW(TXPMBUFPN, TXFHANDLE_EVENT);
  if (!fEvent) goto err;
  fEvent->pipe[0] = fEvent->pipe[1] = TXFHANDLE_INVALID_VALUE;
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
        putmsg(MERR + FOE, __FUNCTION__, "Cannot create pipe: %s",
               TXstrerror(TXgeterror()));
        goto err;
      }
#  ifndef EPI_HAVE_PIPE2
  {
    int flags0, flags1;

    flags0 = fcntl(fEvent->pipe[0], F_GETFL);
    flags1 = fcntl(fEvent->pipe[1], F_GETFL);
    if (flags0 == -1 || flags1 == -1)
      {
        putmsg(MERR, __FUNCTION__, "Cannot get pipe status flags: %s",
               TXstrerror(TXgeterror()));
        goto err;
      }
    flags0 |= O_NONBLOCK;
    flags1 |= O_NONBLOCK;
    if (fcntl(fEvent->pipe[0], F_SETFL, flags0) != 0 ||
        fcntl(fEvent->pipe[1], F_SETFL, flags1) != 0)
      {
        putmsg(MERR, __FUNCTION__, "Cannot set pipe status flags: %s",
               TXstrerror(TXgeterror()));
        goto err;
      }

    flags0 = fcntl(fEvent->pipe[0], F_GETFD);
    flags1 = fcntl(fEvent->pipe[1], F_GETFD);
    if (flags0 == -1 || flags1 == -1)
      {
        putmsg(MERR, __FUNCTION__, "Cannot get pipe descriptor flags: %s",
               TXstrerror(TXgeterror()));
        goto err;
      }
    flags0 |= FD_CLOEXEC;
    flags1 |= FD_CLOEXEC;
    if (fcntl(fEvent->pipe[0], F_SETFD, flags0) != 0 ||
        fcntl(fEvent->pipe[1], F_SETFD, flags1) != 0)
      {
        putmsg(MERR, __FUNCTION__, "Cannot set pipe descriptor flags: %s",
               TXstrerror(TXgeterror()));
        goto err;
      }
  }
#  endif /* !EPI_HAVE_PIPE2 */
  goto finally;

err:
  fEvent = TXfhandleEventClose(fEvent);
finally:
  return(fEvent);
#endif /* !_WIN32 */
}

TXFHANDLE_EVENT *
TXfhandleEventClose(TXFHANDLE_EVENT *fEvent)
{
#ifdef _WIN32
  return(closetxevent(fEvent));
#else /* !_WIN32 */
  if (fEvent)
    {
      if (fEvent->pipe[0] != TXFHANDLE_INVALID_VALUE)
        close(fEvent->pipe[0]);
      if (fEvent->pipe[1] != TXFHANDLE_INVALID_VALUE)
        close(fEvent->pipe[1]);
      fEvent = TXfree(fEvent);
    }
  return(NULL);
#endif /* !_WIN32 */
}

int
TXfhandleEventSignal(TXFHANDLE_EVENT *fEvent)
/* Returns 0 on error.
 */
{
#ifdef _WIN32
  return(TXsignalevent(fEvent));
#else /* !_WIN32 */
  int   tries, ret;

  /* Do not fill up the OS pipe buffer: */
  if (fEvent->signalled) goto ok;

  for (tries = 0; tries < 25; tries++)
    {
      TXseterror(0);
      switch (write(fEvent->pipe[1], "x", 1))
        {
        case -1:
          if (TXgeterror() == TXERR_EINTR) break;       /* try again */
          /* else fall through and report error: */
        case 0:
          /* We only write one byte; OS buffer should have sufficed: */
          putmsg(MERR + FWE, __FUNCTION__, "Cannot write to pipe: %s",
                 TXstrerror(TXgeterror()));
          goto err;
        default:                                /* presumably 1 byte written*/
          fEvent->signalled = 1;
          goto ok;
        }
    }
  putmsg(MERR + FWE, __FUNCTION__,
         "Cannot write to pipe: Too many interruptions");
  goto err;

ok:
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
#endif /* !_WIN32 */
}

int
TXfhandleEventClear(TXFHANDLE_EVENT *fEvent)
/* Returns 0 on error.
 */
{
#ifdef _WIN32
  return(ResetEvent((HANDLE)fEvent));
#else /* !_WIN32 */
  byte  buf[2];
  int   ret, tries;

  if (!fEvent->signalled) goto ok;

  for (tries = 0; tries < 25; tries++)
    {
      TXseterror(0);
      switch (read(fEvent->pipe[0], buf, 1))
        {
        case -1:
          if (TXgeterror() == TXERR_EINTR) break;       /* try again */
          putmsg(MERR + FWE, __FUNCTION__, "Cannot read from pipe: %s",
                 TXstrerror(TXgeterror()));
          goto err;
        case 0:                                 /* EOF */
        default:                                /* presumably 1 byte read */
          fEvent->signalled = 0;
          goto ok;
        }
    }
  putmsg(MERR + FWE, __FUNCTION__,
         "Cannot write to pipe: Too many interruptions");
  goto err;

ok:
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
           
#endif /* !_WIN32 */
}

TXFHANDLE
TXfhandleEventGetWaitableFhandle(TXFHANDLE_EVENT *fEvent)
{
#ifdef _WIN32
  return((TXFHANDLE)fEvent);
#else /* !_WIN32 */
  return(fEvent->pipe[0]);
#endif /* !_WIN32 */
}
