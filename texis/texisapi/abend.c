#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "texint.h"
#include "cgi.h"


typedef struct TXABEND_tag
{
  struct TXABEND_tag    *next;
  TXABENDCB             *func;
  void                  *usr;
}
TXABEND;
#define TXABENDPN       ((TXABEND *)NULL)

typedef struct TXABENDLOC_tag
{
  struct TXABENDLOC_tag *prev, *next;
  TXTHREADID            threadid;
  TXABENDLOCCB          *func;
  void                  *usr;
}
TXABENDLOC;
#define TXABENDLOCPN    ((TXABENDLOC *)NULL)


static VOLATILE TXATOMINT       TxAbendsCalled = 0;
static TXABEND                  *TxAbends = TXABENDPN;
static TXcriticalSection        *TxAbendCs = NULL;

static VOLATILE TXATOMINT       TxAbendLocsCalling = 0;
static TXABENDLOC               *TxAbendLocs = TXABENDLOCPN;
static TXABENDLOC               *TxAbendLocsLast = TXABENDLOCPN;
static int                      TxAbendLocCnt = 0, TxAbendLocMax = 0;
static TXABENDLOC               *TxAbendLocSpares = TXABENDLOCPN;
static int                      TxAbendLocSpareCnt = 0;
static TXcriticalSection        *TxAbendLocCs = NULL;

static TXABEND                  *TXonExitCallbacks = NULL;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
TXinitAbendSystem(TXPMBUF *pmbuf)
/* Called at process start.  `pmbuf' not cloned.
 */
{
  int   ret = 1;

  if (!TxAbendCs &&
      !(TxAbendCs =
        TXcriticalSectionOpen(TXcriticalSectionType_Normal, pmbuf)))
    ret = 0;
  if (!TxAbendLocCs &&
      !(TxAbendLocCs =
        TXcriticalSectionOpen(TXcriticalSectionType_Normal, pmbuf)))
    ret = 0;
  return(ret);
}

/* - - - - - - - - - - - - ABEND cleanup callbacks - - - - - - - - - - - - - */

int
TXaddabendcb(func, usr)
TXABENDCB       *func;
void            *usr;
/* Registers func(usr) to be called if ABEND reached.  Returns 0 on error.
 * `func' should not attempt complex ops, e.g. malloc, etc.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "TXaddabendcb";
  TXABEND               *ab;

  if ((ab = (TXABEND *)TXmalloc(TXPMBUFPN, fn, sizeof(TXABEND))) == TXABENDPN)
    return(0);
  ab->func = func;
  ab->usr = usr;
  /* Add to start of list, to ensure calling in LIFO order: */
  if (TXcriticalSectionEnter(TxAbendCs, TXPMBUFPN))
    {
      ab->next = TxAbends;
      TxAbends = ab;
      ab = NULL;
      TXcriticalSectionExit(TxAbendCs, TXPMBUFPN);
    }
  else
    {
      ab = TXfree(ab);
      return(0);
    }
  return(1);
}

int
TXdelabendcb(func, usr)
TXABENDCB       *func;
void            *usr;
/* Unregisters func(usr) from ABEND functions.
 * Returns 0 on error silently (no such callback), 1 if deleted.
 * Thread-safe.
 */
{
  TXABEND       *ab, *prev;
  int           ret = 0;

  if (!TXcriticalSectionEnter(TxAbendCs, TXPMBUFPN)) return(0);

  for (prev = TXABENDPN, ab = TxAbends;
       ab != TXABENDPN;
       prev = ab, ab = ab->next)
    if (ab->func == func && ab->usr == usr)
      {
        if (prev != TXABENDPN) prev->next = ab->next;
        else TxAbends = ab->next;
        ret = 1;
        break;
      }

  TXcriticalSectionExit(TxAbendCs, TXPMBUFPN);

  ab = TXfree(ab);
  return(ret);
}

void
TXcallabendcbs()
/* Call this from a signal handler that's about to exit.  It calls
 * the abend callbacks.
 * Thread-unsafe.
 * Signal-safe (if registered abend callbacks are).
 */
{
  TXABEND       *ab;

  if (TX_ATOMIC_INC(&TxAbendsCalled)) return;
  for (ab = TxAbends; ab != TXABENDPN; ab = ab->next)
    ab->func(ab->usr);
}

/* - - - - - - - - - - - - ABEND location callbacks - - - - - - - - - - - - */

int
TXaddabendloccb(func, usr)
TXABENDLOCCB    *func;  /* callback function */
void            *usr;   /* user data for `func' */
/* Registers func(buf, sz, usr) to be called if ABEND reached, to print
 * current location (e.g. URL:line).  Returns 0 on error.
 * `func' should not attempt complex ops, e.g. malloc, etc.
 * Thread-safe.
 */
{
  static CONST char     fn[] = "TXaddabendloccb";
  TXABENDLOC            *newab;
  int                   ret;

  if (!TXcriticalSectionEnter(TxAbendLocCs, TXPMBUFPN)) return(0);

  if (TxAbendLocSpares != TXABENDLOCPN)         /* we have an extra */
    {
      newab = TxAbendLocSpares;
      TxAbendLocSpares = TxAbendLocSpares->next;
      TxAbendLocSpareCnt--;
    }
  else if (!(newab = (TXABENDLOC *)TXmalloc(TXPMBUFPN, fn,
                                            sizeof(TXABENDLOC))))
    goto err;
  newab->threadid = TXgetcurrentthreadid();
  newab->func = func;
  newab->usr = usr;
  /* Add to end of list so they are called in FIFO order: */
  newab->next = TXABENDLOCPN;
  if (TxAbendLocsLast != TXABENDLOCPN) TxAbendLocsLast->next = newab;
  else TxAbendLocs = newab;
  newab->prev = TxAbendLocsLast;
  TxAbendLocsLast = newab;
  TxAbendLocCnt++;
  if (TxAbendLocCnt > TxAbendLocMax) TxAbendLocMax = TxAbendLocCnt;
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  TXcriticalSectionExit(TxAbendLocCs, TXPMBUFPN);
  return(ret);
}

int
TXdelabendloccb(func, usr)
TXABENDLOCCB    *func;  /* callback function */
void            *usr;   /* user data for `func' */
/* Unregisters func(buf, sz, usr) from ABEND location functions.
 * Returns 0 on error silently (no such callback), 1 if deleted.
 * Thread-safe.
 */
{
  TXABENDLOC    *ab;
  TXTHREADID    threadid;
  int           ret = 0;

  threadid = TXgetcurrentthreadid();
  if (!TXcriticalSectionEnter(TxAbendLocCs, TXPMBUFPN)) return(0);

  /* Search in reverse order as an optimization; we're likely deleting
   * the last one added:
   */
  for (ab = TxAbendLocsLast; ab != TXABENDLOCPN; ab = ab->prev)
    if (TXthreadidsAreEqual(ab->threadid, threadid) &&
        ab->func == func &&
        ab->usr == usr)
      {
        /* Remove from list: */
        if (ab->next != TXABENDLOCPN) ab->next->prev = ab->prev;
        else TxAbendLocsLast = ab->prev;
        if (ab->prev != TXABENDLOCPN) ab->prev->next = ab->next;
        else TxAbendLocs = ab->next;
        TxAbendLocCnt--;
        /* The same ABEND location callback may get added and removed a lot.
         * Save mem by recycling the last few:
         */
        if (TxAbendLocSpareCnt < TxAbendLocMax) /* guess at max to save */
          {
            ab->prev = TXABENDLOCPN;
            ab->next = TxAbendLocSpares;
            TxAbendLocSpares = ab;
            TxAbendLocSpareCnt++;
          }
        else
          ab = TXfree(ab);
        ret = 1;
        break;
      }
  TXcriticalSectionExit(TxAbendLocCs, TXPMBUFPN);
  return(ret);
}

size_t
TXprabendloc(buf, sz)
char            *buf;   /* buffer to print to */
size_t          sz;     /* its size */
/* Prints current location (e.g. URL:line) using ABEND location callbacks.
 * Returns size of `buf' used.  Called by signal handler.
 * Thread-unsafe.
 * Signal-safe (if registered ABEND location callbacks are).
 */
{
  static CONST char     hexup[] = "0123456789ABCDEF";
  static const char     inMemMsg[] = " in malloc lib";
  TXABENDLOC            *tab, *ab;
  char                  *d, *e;
  size_t                used, n;
  EPI_UINT32            u;
  int                   i;

  d = buf;
  if (!buf) sz = 0;
  e = buf + sz;
#define BUF_LEFT        (d < e ? e - d : 0)

  if (TX_ATOMIC_INC(&TxAbendLocsCalling)) goto done;

  /* Callbacks are in FIFO order per thread, but threads may be mixed.
   * Call one thread's list at a time:
   */
  for (tab = TxAbendLocs; tab != TXABENDLOCPN; tab = tab->next)
    {                                           /* each thread */
      if (tab->prev != TXABENDLOCPN &&
          TXthreadidsAreEqual(tab->threadid, tab->prev->threadid))
        continue;
      if (TXthreadscreated())                   /* print TID:0x...: */
        {
          if (e - d < 17) break;                /* no room */
          strcpy(d, " TID:0x");
          d += 7;
          u = (EPI_UINT32)tab->threadid;
          for (n = 0; n < 8; n++)               /* safe hex print */
            *(d++) = hexup[(u >> ((7 - n)*4)) & 0xF];
          *(d++) = ':';
          *(d++) = ' ';
        }
      for (ab = tab; ab != TXABENDLOCPN; ab = ab->next)
        {                                       /* each callback this thread*/
          if (!TXthreadidsAreEqual(ab->threadid, tab->threadid)) continue;
          if (d + 2 > e) break;                 /* no room */
          *(d++) = ' ';                         /* separate each callback */
          n = (size_t)(e - d);                  /* space left */
          used = ab->func(d, n, ab->usr);
          if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(used)) used = 0;  /* sanity */
          else if (used > n) used = n;
          d += used;
        }
    }
  if (d + sizeof(inMemMsg) <= e && TXgetSysMemFuncDepth() > 0)
    {
      static const char pfx[] = " called from";
      const char                *funcs[10];
      size_t                    numFuncs, i;

      memcpy(d, inMemMsg, sizeof(inMemMsg) - 1);
      d += sizeof(inMemMsg) - 1;
      numFuncs = TXgetMemUsingFuncs(funcs, TX_ARRAY_LEN(funcs));
      if (numFuncs > 0 && BUF_LEFT >= sizeof(pfx))
        {
          strcpy(d, pfx);
          d += sizeof(pfx) - 1;
          for (i = 0; i < numFuncs && i < TX_ARRAY_LEN(funcs) && d < e; i++)
            d += htsnpf(d, BUF_LEFT, " %s", (funcs[i] ? funcs[i] : "?"));
        }
    }

done:
  /* Put ellipsis at end if (potential) overflow (and room): */
  if (BUF_LEFT <= 0)
    {
      for (i = 2; i < 5 && e >= buf + i; i++) e[-i] = '.';
    }
  if (sz > 0) *(d < e ? d : e - 1) = '\0';      /* ensure nul-termination */

  TX_ATOMIC_DEC(&TxAbendLocsCalling);
  return((size_t)(d - buf));
#undef BUF_LEFT
}

/* - - - - - - - - - - - - On-exit cleanup callbacks - - - - - - - - - - - */

int
TXaddOnExitCallback(TXPMBUF *pmbuf, TXABENDCB *func, void *usr)
/* Registers func(usr) to be called during TXexit().  Will be called
 * in LIFO order.
 * Returns 0 on error.
 * Thread-safe.
 */
{
  static const char     fn[] = "TXaddOnExitCallback";
  TXABEND               *cb;
  int                   ret = 1;

  if (!(cb = (TXABEND *)TXmalloc(pmbuf, fn, sizeof(TXABEND))))
    return(0);
  cb->func = func;
  cb->usr = usr;
  /* Add to start of list, to ensure calling in LIFO order: */
  if (TXcriticalSectionEnter(TxAbendCs, pmbuf))
    {
      cb->next = TXonExitCallbacks;
      TXonExitCallbacks = cb;
      cb = NULL;
      TXcriticalSectionExit(TxAbendCs, pmbuf);
    }
  else
    ret = 0;
  return(ret);
}

int
TXremoveOnExitCallback(TXPMBUF *pmbuf, TXABENDCB *func, void *usr)
/* Unregisters func(usr) from TXexit() functions.
 * Returns 0 on error silently (no such callback), 1 if deleted.
 * Yaps on significant error (csect problem).
 * Thread-safe.
 */
{
  TXABEND       *cb = NULL, *prev;
  int           ret = 0;

  if (TXcriticalSectionEnter(TxAbendCs, pmbuf))
    {
      for (prev = NULL, cb = TXonExitCallbacks; cb; prev = cb, cb = cb->next)
        if (cb->func == func && cb->usr == usr)
          {
            if (prev) prev->next = cb->next;
            else TXonExitCallbacks = cb->next;
            ret = 1;
            break;
          }
      TXcriticalSectionExit(TxAbendCs, pmbuf);
      cb = TXfree(cb);
    }
  else
    ret = 0;
  return(ret);
}

void
TXcallOnExitCallbacks()
/* Called when about to exit normally (not through signal).
 * Thread-unsafe.
 */
{
  TXABEND       *cb, *next;

  /* Free as we go, both to avoid mem leak and to ensure one-time-call: */
  if (TXcriticalSectionEnter(TxAbendCs, TXPMBUF_SUPPRESS))
    {
      cb = TXonExitCallbacks;
      TXonExitCallbacks = NULL;
      TXcriticalSectionExit(TxAbendCs, TXPMBUF_SUPPRESS);
    }
  else
    {
      /* wtf do not yap; putmsg() may be unsafe? */
      return;
    }

  for ( ; cb; cb = next)
    {
      next = cb->next;
      cb->func(cb->usr);
      cb = TXfree(cb);
    }
}

void
TXexit(TXEXIT status)
/* Calls on-exit callbacks, and then exits with `status'.
 */
{
  TXcallOnExitCallbacks();
  exit(status);
}

/* - - - - - - - - - - - - - - - - Miscellaneous - - - - - - - - - - - - - */

void
TXfreeabendcache()
/* Frees ABEND callback/location cache.
 * Called mainly at process end, for MEMDEBUG.
 * Thread-safe.
 */
{
  TXABENDLOC    *next;

  if (TXcriticalSectionEnter(TxAbendLocCs, TXPMBUFPN))
    {
      while (TxAbendLocSpares != TXABENDLOCPN)
        {
          next = TxAbendLocSpares->next;
          TxAbendLocSpares = TXfree(TxAbendLocSpares);
          TxAbendLocSpareCnt--;
          TxAbendLocSpares = next;
        }
      TXcriticalSectionExit(TxAbendLocCs, TXPMBUFPN);
    }
}
