#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#include "sizes.h"
#include "texint.h"     /* for TXprkilo() */
#include "mmsg.h"
#include "cgi.h"
#include "http.h"
#include "httpi.h"

/*
 * data           sent                sendlimit         cnt         sz   maxsz
 * v              v                   v                 v           v    v
 * +--------------+-------------------+-----------------+-----------+
 * +--sent to net-+
 *                +-----to be sent----+
 *                +-htbuf_getsend[sz]-+
 *                                    +----hold space---+
 *                                    +-htbuf_getholdsz-+
 *                +---live/used (htbuf_getdatasz())-----+
 *                +---------htbuf_getdata[2]()----------+
 * +-------------+nul        htbuf_getavail()           +-----------+
 * +------------------written to buffer-----------------+nul
 * +----------------------malloc'd `data' buffer--------------------+
 */

#ifdef MEMDEBUG
#  undef openhtbuf
#  undef closehtbuf
#  define MAC_(func)    mac_##func
#  define MEMDEBUGARGS  , file, line, memo
#else /* !MEMDEBUG */
#  define MAC_(func)    func
#  define MEMDEBUGARGS
#endif /* !MEMDEBUG */

#define CHKMOD(buf, fn, err)                                    \
  if ((buf)->flags & (HTBF_CONST | HTBF_ATOMIC | HTBF_ERROR))   \
    {                                                           \
      htbuf_modattempt((buf), (fn));                            \
      err;                                                      \
    }
/* see also htbuf_inc(), htbuf_write(): */
#define CHKINC(buf, n, err)                     \
  if (((buf)->cnt + (size_t)(n) >= (buf)->sz || \
       (buf)->cnt + (size_t)(n) < (buf)->cnt) &&\
      !htbuf_inc((buf), (n))) err

/* Thread-safe, signal-safe: */
#define CLROFF(buf)     \
  ((buf)->cnt = (buf)->sent = (buf)->sendlimit = 0, (buf)->eol = CHARPN)
#if TXATOMINTBITS == EPI_OS_SIZE_T_BITS
#  define CLRBUF(buf)   ((buf)->sz = 0, (buf)->data = CHARPN, CLROFF(buf))
#else /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
#  define CLRBUF(buf)   ((buf)->htbuf_atomcnt = (buf)->htbuf_atomsz = 0, \
                         (buf)->sz = 0, (buf)->data = CHARPN, CLROFF(buf))
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */


static CONST char       NonAtomic[] = "Non-atomic buffer";
static CONST char       NoRingBuffers[] =
  "Internal error: Cannot perform operation on ring buffer";


static void htbuf_modattempt ARGS((HTBUF *buf, CONST char *fn));
static void
htbuf_modattempt(buf, fn)
HTBUF           *buf;
CONST char      *fn;
{
  buf->flags |= HTBF_ERROR;
  if ((buf->flags & (HTBF_CONST | HTBF_NOMSG)) == HTBF_CONST)
    txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                   "Internal error: Cannot modify read-only buffer");
  else if ((buf->flags & (HTBF_NOALLOC | HTBF_NOMSG)) == HTBF_NOALLOC)
    txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                   "Internal error: Fixed-size buffer cannot be re-allocated");
  else if ((buf->flags & (HTBF_ATOMIC | HTBF_NOMSG)) == HTBF_ATOMIC)
    txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                   "Internal error: Non-atomic access to atomic buffer");
}

void
htbuf_init(buf)
HTBUF	*buf;
/* Initialize a just-declared/alloced HTBUF.
 * Deprecated except for some signal-handler usage in Vortex.
 * Thread-safe, signal-safe.
 */
{
  /* also see htbuf_release() */
  CLRBUF(buf);
  buf->flags = HTBF_DO8BIT;
  buf->fmtcp = TXFMTCPPN;                       /* ie. use defaults */
  TXFMTSTATE_INIT(&buf->privateFs);
  buf->fs = &buf->privateFs;
  buf->refcnt = buf->agetcnt = buf->agetsz = 0;
  buf->maxsz = (size_t)EPI_OS_SIZE_T_MAX;
  buf->unhtml = htiso88591_to_iso88591;
  buf->unhtmlFlags = UTF_HTMLDECODE;
  buf->pmbuf = TXPMBUFPN;
}

HTBUF *
openhtbuf(TXALLOC_PROTO_SOLE)
{
  static CONST char     fn[] = "openhtbuf";
  HTBUF                 *buf;

  if ((buf = (HTBUF *)MAC_(calloc)(1, sizeof(HTBUF) MEMDEBUGARGS)) == HTBUFPN)
    {
      putmsg(MERR + MAE, fn, strerror(errno));
      return(HTBUFPN);
    }
  htbuf_init(buf);
  return(buf);
}

HTBUF *
closehtbuf(HTBUF *buf TXALLOC_PROTO)
{
  if (buf == HTBUFPN) goto done;
  if (buf->data != CHARPN && !(buf->flags & (HTBF_CONST | HTBF_NOALLOC)))
    MAC_(free)(buf->data MEMDEBUGARGS);
  if (buf->pmbuf != TXPMBUFPN && !(buf->flags & HTBF_CONST))
    buf->pmbuf = txpmbuf_close(buf->pmbuf);
  TXFMTSTATE_RELEASE(&buf->privateFs);
  if (buf->fmtcp != TXFMTCPPN && (buf->flags & HTBF_OWNFMTCP))
    buf->fmtcp = TxfmtcpClose(buf->fmtcp);
  free(buf);
done:
  return(HTBUFPN);
}

size_t
htbuf_getdata(buf, data, flags)
HTBUF   *buf;
char    **data;
int     flags;          /* 0x1: release data  0x2: realloc to payload */
/* Sets `*data' to buffer pointer (could be NULL), and returns size.
 * Will release data if `flags & 0x1': data must then be freed by caller.
 * Will realloc data to payload size if `flags & 0x2' (if flags & 0x1 too),
 * i.e. removes over-allocation to save mem.
 * NOTE: do not use with ring buffers; will fail if `release'.
 * NOTE: will clear HTBF_ERROR/HTBF_CONST/HTBF_NOALLOC if `release'.
 * NOTE: if HTBF_ATOMIC, caller must ensure call is atomic.
 * Thread-safe (if putmsg() is).
 */
{
  static CONST char     fn[] = "htbuf_getdata";
  size_t                cnt;
  char                  *newData;

  cnt = ((buf->flags & HTBF_ATOMIC) ? (size_t)buf->htbuf_atomcnt : buf->cnt);
  if (data != CHARPPN)
    {
      if ((*data = buf->data) != CHARPN && !(buf->flags & HTBF_CONST))
        (*data)[cnt] = '\0';
    }
  if (flags & 0x1)                              /* caller will own buffer */
    {
      if (buf->sent)                            /* ring buffer */
        {
          if (!(buf->flags & HTBF_NOMSG))
            txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NoRingBuffers);
          if (data != CHARPPN) *data = CHARPN;  /* prevent caller free() */
          return(0);
        }
      if ((flags & 0x2) && data && *data && cnt + 1 != buf->sz &&
          !(buf->flags & (HTBF_CONST | HTBF_NOALLOC)))
        {                                       /* realloc to save mem */
          newData = (char *)TXrealloc(buf->pmbuf, __FUNCTION__,
                                      *data, cnt + 1);
          if (newData) *data = newData;
#ifndef EPI_REALLOC_FAIL_SAFE
          else                                  /* realloc fail freed it */
            {
              *data = NULL;
              cnt = 0;
            }
#endif /* !EPI_REALLOC_FAIL_SAFE */
        }
      CLRBUF(buf);
      buf->flags &= ~(HTBF_ERROR | HTBF_CONST | HTBF_NOALLOC);
    }
  return(cnt);
}

HTBF
htbuf_setflags(buf, flags, set)
HTBUF   *buf;
HTBF    flags;
int     set;
/* Sets or clears (according to `set') `flags'.
 * Returns previous value(s) of `flags'.
 */
{
  HTBF  ret;

  ret = (buf->flags & flags);
  if (set)
    {
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
      if ((flags & HTBF_ATOMIC) && !(buf->flags & HTBF_ATOMIC))
        {
          buf->htbuf_atomcnt = buf->cnt;
          buf->htbuf_atomsz = buf->sz;
          buf->cnt = buf->sz = 0;
        }
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
      buf->flags |= flags;
    }
  else
    {
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
      if ((flags & HTBF_ATOMIC) && (buf->flags & HTBF_ATOMIC))
        {
          buf->cnt = buf->htbuf_atomcnt;
          buf->sz = buf->htbuf_atomsz;
          buf->htbuf_atomcnt = buf->htbuf_atomsz = 0;
        }
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
      buf->flags &= ~flags;
    }
  return(ret);
}

HTBF
htbuf_getflags(buf, flags)
HTBUF   *buf;
HTBF    flags;
{
  return(buf->flags & flags);
}

char *
htbuf_geteol(HTBUF *buf)
{
  return(buf->eol);
}

int
htbuf_setunhtml(buf, unhtml, flags)
HTBUF           *buf;
HTCHARSETFUNC   *unhtml;
UTF             flags;
{
  buf->unhtml = unhtml;
  buf->unhtmlFlags = flags;
  return(1);
}

HTCHARSETFUNC *
htbuf_getunhtml(buf, flags)
HTBUF   *buf;
UTF     *flags;
{
  if (flags != UTFPN) *flags = buf->unhtmlFlags;
  return(buf->unhtml);
}

int
htbuf_setpmbuf(buf, pmbufclone, flags)
HTBUF   *buf;
TXPMBUF *pmbufclone;    /* putmsg buffer to attach */
int     flags;          /* (in) bit 0: subobjs too  bit 1: shared objs too */
/* Closes existing putmsg buffer, and attaches to `pmbufclone' if non-NULL,
 * else leaves putmsg buffer unset.
 * NOTE: `buf' should be closed with closehtbuf() or else `pmbufclone' may
 * be orphaned/leaked.
 * Returns 0 on error.
 */
{
  int   ret = 1;

  (void)flags;
  buf->pmbuf = txpmbuf_close(buf->pmbuf);
  if (pmbufclone != TXPMBUFPN)
    ret = ((buf->pmbuf = txpmbuf_open(pmbufclone)) != TXPMBUFPN);
  return(ret);
}

TXPMBUF *
htbuf_getpmbuf(buf)
HTBUF   *buf;
/* Returns current putmsg buffer (without closing or changing refcnt).
 * Call htbuf_setpmbuf(buf, TXPMBUFPN) to close it.
 */
{
  return(buf->pmbuf);
}

int
htbuf_setmaxsz(buf, maxsz)
HTBUF   *buf;
size_t  maxsz;  /* max size (-1 == unlimited) */
/* Sets max size of buffer (not including nul terminator).
 * If called on in-use buffer, actual max size may be larger.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_setmaxsz";

  if (maxsz == (size_t)(-1) || TX_SIZE_T_VALUE_LESS_THAN_ZERO(maxsz))
    maxsz = (size_t)EPI_OS_SIZE_T_MAX;
  else if (maxsz == (size_t)EPI_OS_SIZE_T_MAX)
    {
      if (!(buf->flags & HTBF_NOMSG))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, "Size too large");
      return(0);
    }
  else
    maxsz++;
  /* Resize buffer, if empty: */
  if (buf->data != CHARPN && maxsz < buf->sz)   /* smaller limit */
    {
      if (buf->cnt || buf->sent)                /* wtf live data */
        {
          buf->maxsz = buf->sz;                 /* cannot make it less */
          return(1);
        }
      free(buf->data);
      buf->data = CHARPN;
      buf->sz = (size_t)0;
    }
  buf->maxsz = maxsz;
  return(1);
}

void
htbuf_setdata(buf, data, cnt, sz, alloc)
HTBUF   *buf;
char    *data;          /* optional buffer */
size_t  cnt, sz;
int     alloc;
/* Sets data buffer to `data' with live- and send- (strlen()) size `cnt'
 * and total (alloc'd) size `sz'.
 * If `alloc' is 2, `data' must be alloc'd and `*buf' will own it.
 * If 1, `data' can be modified but is still owned by caller, and will
 * not be re-alloced (ie. fixed buffer).
 * If 0, `data' will not be touched at all.   Existing buffer (if any)
 * freed/released.  `data' must be '\0'-terminated at `cnt'.
 * NOTE: resets HTBF_CONST/HTBF_NOALLOC/HTBF_ERROR flags.
 * NOTE: if HTBF_ATOMIC, caller must ensure call is atomic.
 */
{
  (void)TX_ATOMIC_INC(&buf->refcnt);
  htbuf_release(buf);
  if (data != CHARPN)
    {
      if (sz <= 0)                              /* sanity checks */
        {
          if (alloc == 2) free(data);
          data = CHARPN;
          cnt = sz = 0;
        }
      else if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(cnt)) cnt = 0;
      else if (cnt >= sz) cnt = sz - 1;
      buf->data = data;
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
      if (buf->flags & HTBF_ATOMIC)
        {
          buf->htbuf_atomcnt = cnt;
          buf->htbuf_atomsz = sz;
        }
      else
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
        {
          buf->cnt = (TXATOMINT)cnt;
          buf->sz = (TXATOMINT)sz;
        }
      buf->sendlimit = cnt;
    }
  switch (alloc)
    {
    case 2:                               break;
    case 1:  buf->flags |= HTBF_NOALLOC;  break;
    case 0:  buf->flags |= HTBF_CONST;    break;
    }
  (void)TX_ATOMIC_DEC(&buf->refcnt);
}

size_t
htbuf_getunused(buf, unused)
HTBUF   *buf;
char    **unused;
/* Sets `*unused' (if non-NULL) to start of unused part of buffer
 * (could be NULL), and returns size.  NOTE: call htbuf_addused[2]()
 * immediately after writing to buffer, with size <= this returned
 * value, or data may be lost.
 * NOTE: do not use with ring buffers, use htbuf_getavail().
 * NOTE: caller responsible for checking HTBF_CONST/HTBF_ATOMIC.
 * Does not modify or alloc buffer.
 * Thread-safe (if putmsg() is).
 */
{
  static CONST char     fn[] = "htbuf_getunused";
  size_t                sz;

  if (buf->flags & HTBF_ATOMIC)
    {
      htbuf_modattempt(buf, fn);
      goto err;
    }
  if (buf->sent)
    {
      if (!(buf->flags & HTBF_NOMSG))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NoRingBuffers);
    err:
      if (unused != CHARPPN) *unused = CHARPN;
      return((size_t)0);
    }

  if (unused != CHARPPN)
    {
      if ((*unused = buf->data) != CHARPN) *unused += buf->cnt;
    }
  sz = buf->sz - buf->cnt;
  if (sz > 0) sz--;                     /* -1 for '\0' */
  return(sz);
}

int
htbuf_addused2(buf, cnt, syncSendlimit)
HTBUF   *buf;
int     cnt;
int     syncSendlimit;          /* (in) nonzero: set `sendlimit' to `cnt' */
/* Adds `cnt' to buffer-used count and '\0'-terminates it.  `cnt'
 * should be less than or equal to the return value of a call to
 * htbuf_getunused() immediately before.  If < 0, decrements count.
 * Returns 0 on error (buf cannot be re-alloced, or is HTBF_CONST).
 */
{
  static CONST char     fn[] = "htbuf_addused2";
  size_t                sz, n;

  CHKMOD(buf, fn, return(0));
  if (cnt < 0)                                  /* deleting from buffer */
    {
      sz = (size_t)(-cnt);
      if (buf->cnt < buf->sent)                 /* split buffer */
        {
          n = buf->cnt;
          if (n > sz) n = sz;
          buf->cnt -= n;
          if (buf->sendlimit > buf->cnt && buf->sendlimit < buf->sent)
            buf->sendlimit = buf->cnt;
          sz -= n;
          if (sz)                               /* still more to delete */
            {
              n = buf->sz - buf->sent;
              if (n > sz) n = sz;
              buf->cnt = buf->sz - n;
              goto chk1;
            }
        }
      else                                      /* one buffer */
        {
          if (sz > buf->cnt - buf->sent) sz = buf->cnt - buf->sent;
          buf->cnt -= sz;
        chk1:
          if (buf->sendlimit > buf->cnt) buf->sendlimit = buf->cnt;
          /* If we deleted all data, reset the ring for neatness,
           * ie. un-split it by resetting pointers to start of `data':
           */
          if (buf->cnt == buf->sent) CLROFF(buf);
        }
      buf->eol = CHARPN;                        /* wtf */
    }
  else                                          /* adding to buffer */
    {
      CHKINC(buf, cnt, return(0));
      if (cnt > 0)
        {
          if (buf->sent)                        /* wtf */
            {
              txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NoRingBuffers);
              return(0);
            }
          buf->cnt += (size_t)cnt;
        }
    }
  if (buf->data != CHARPN) buf->data[buf->cnt] = '\0';
  if (syncSendlimit) buf->sendlimit = buf->cnt;
  return(1);
}

int
htbuf_delused(buf, sz, oksplit)
HTBUF   *buf;
size_t  sz;     /* -1 for all */
int     oksplit;
/* Deletes up to `sz' bytes from _start_ of used (sent => cnt) area.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_delused";
  size_t                livesz, n;

  CHKMOD(buf, fn, return(0));

  livesz = htbuf_getdatasz(buf);
  if (sz > livesz || sz == (size_t)(-1)) sz = livesz;
  if (sz <= (size_t)0) goto ok;
  if (buf->cnt < buf->sent)                     /* split ring buffer */
    {
      if (!oksplit)
        {
          txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                "Internal error: Cannot avoid split on existing ring buffer");
          return(0);
        }
      n = buf->sz - buf->sent;                  /* 1st buffer size */
      if (n > sz) n = sz;
      buf->sent += n;
      if (buf->sendlimit < buf->sent && buf->sendlimit > buf->cnt)
        buf->sendlimit = buf->sent;
      if (buf->sent == buf->sz)                 /* roll over */
        {
          if (buf->sendlimit == buf->sent) buf->sendlimit = 0;
          buf->sent = 0;
        }
      sz -= n;
      if (sz)                                   /* still more to delete */
        {
          buf->sent += sz;
          if (buf->sendlimit < buf->sent) buf->sendlimit = buf->sent;
        }
    }
  else                                          /* one buffer */
    {
      if (oksplit)                              /* can avoid memmove() */
        {
          buf->sent += sz;
          if (buf->sendlimit < buf->sent) buf->sendlimit = buf->sent;
        }
      else
        {
          if (sz < livesz) memmove(buf->data + buf->sent,
                                   buf->data + buf->sent + sz, livesz - sz);
          buf->cnt -= sz;
          if (buf->sendlimit > buf->cnt) buf->sendlimit = buf->cnt;
        }
    }
  buf->eol = CHARPN;
  if (buf->data != CHARPN) buf->data[buf->cnt] = '\0';
ok:
  return(1);
}

int
htbuf_cpfromhold(buf, dest, sz)
HTBUF   *buf;
char    *dest;
size_t  sz;
/* Copies `sz' bytes from start of hold (sendlimit => cnt) area to `dest'.
 * Does not affect `buf' offsets.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_cpfromhold";
  size_t                holdsz, n;

  holdsz = htbuf_getholdsz(buf);
  if (sz > holdsz || TX_SIZE_T_VALUE_LESS_THAN_ZERO(sz))
    {
      if (!(buf->flags & HTBF_NOMSG))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, "Illegal size %wd",
                       (EPI_HUGEINT)sz);
      goto err;
    }
  if (sz == (size_t)0) goto ok;

  if (buf->cnt < buf->sendlimit)                /* split buffer */
    {
      n = buf->sz - buf->sendlimit;             /* 1st buffer size */
      if (n > sz) n = sz;
      memcpy(dest, buf->data + buf->sendlimit, n);
      sz -= n;
      if (sz) memcpy(dest + n, buf->data, sz);  /* still more to copy */
    }
  else                                          /* one buffer */
    memcpy(dest, buf->data + buf->sendlimit, sz);
ok:
  return(1);
err:
  return(0);
}

int
htbuf_delhold(buf, sz)
HTBUF   *buf;
size_t  sz;     /* -1 for all */
/* Deletes up to `sz' bytes from start of hold (sendlimit => cnt) area.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_delhold";
  size_t                holdsz, n;

  CHKMOD(buf, fn, return(0));

  holdsz = htbuf_getholdsz(buf);
  if (sz > holdsz || sz == (size_t)(-1)) sz = holdsz;
  if (sz <= (size_t)0) goto ok;
  if (buf->sent == buf->sendlimit)              /* optimization: del start */
    {
      if (buf->cnt < buf->sent)                 /* split buffer */
        {
          n = buf->sz - buf->sent;              /* 1st buffer size */
          if (n > sz) n = sz;
          buf->sent += n;
          if (buf->sent == buf->sz) buf->sent = 0;
          sz -= n;
          if (sz) buf->sent += sz;
        }
      else                                      /* one buffer */
        buf->sent += sz;
      buf->sendlimit = buf->sent;
    }
  else if (sz == holdsz)                        /* optimization: del end */
    buf->cnt = buf->sendlimit;
  else if (buf->cnt < buf->sendlimit)           /* split buffer */
    {
      n = buf->sz - buf->sendlimit;             /* 1st buffer size */
      if (n > sz) n = sz;
      memmove(buf->data + buf->sent + n, buf->data + buf->sent,
              buf->sendlimit - buf->sent);
      buf->sendlimit += n;
      buf->sent += n;
      sz -= n;
      if (sz)                                   /* still more to delete */
        {
          memmove(buf->data, buf->data + sz, buf->cnt - sz);
          buf->cnt -= sz;
        }
    }
  else                                          /* one buffer */
    {
      memmove(buf->data + buf->sendlimit, buf->data + buf->sendlimit + sz,
              holdsz - sz);
      buf->cnt -= sz;
    }
  buf->eol = CHARPN;
  if (buf->data != CHARPN) buf->data[buf->cnt] = '\0';
ok:
  return(1);
}

size_t
htbuf_getsend(buf, data1, sz1, data2, sz2)
HTBUF   *buf;
char    **data1;        /* (out, opt.) pointer to first block, if any */
size_t  *sz1;           /* (out, opt.) size of first block, if any */
char    **data2;        /* (out, opt.) pointer to second block, if any */
size_t  *sz2;           /* (out, opt.) size of second block, if any */
/* Gets both parts of split ring buffer's send space (sent => sendlimit)
 * not including nul terminator.
 * Returns total size.
 */
{
  size_t        n1, n2;
  char          *d1, *d2;

  if ((d1 = buf->data) != CHARPN) d1 += buf->sent;
  if (buf->sent <= buf->sendlimit)              /* one buffer */
    {
      n1 = buf->sendlimit - buf->sent;
      n2 = (size_t)0;
      d2 = CHARPN;
    }
  else                                          /* one or two buffers */
    {
      n1 = buf->sz - buf->sent;
      n2 = buf->sendlimit;
      d2 = buf->data;
    }
  if (n1 == (size_t)0)                          /* first buffer empty */
    {
      n1 = n2;                                  /* use second buffer first */
      d1 = d2;
      n2 = (size_t)0;
      d2 = CHARPN;
      if (n1 == (size_t)0) d1 = CHARPN;         /* second buffer empty too */
    }
  if (sz1 != SIZE_TPN) *sz1 = n1;
  if (sz2 != SIZE_TPN) *sz2 = n2;
  if (data1 != CHARPPN) *data1 = d1;
  if (data2 != CHARPPN) *data2 = d2;
  return(n1 + n2);
}

int
htbuf_delsend(buf, sz)
HTBUF   *buf;
size_t  sz;     /* -1 for all */
/* Deletes up to `sz' bytes from start of send (sent => sendlimit) area,
 * ie. by advancing `buf->sent'.
 * Returns 0 on error, 1 if ok, 2 if all data now sent (`sendlimit' reached).
 */
{
  static CONST char     fn[] = "htbuf_delsend";
  size_t                sendsz, n;

  if (buf->flags & (HTBF_ATOMIC | HTBF_ERROR))
    {
      htbuf_modattempt(buf, fn);
      return(0);
    }

  sendsz = htbuf_getsendsz(buf);
  if (sz > sendsz || sz == (size_t)(-1)) sz = sendsz;
  if (sz <= (size_t)0) return(2);               /* all sent */
  if (buf->sendlimit < buf->sent)                       /* split buffer */
    {
      n = buf->sz - buf->sent;
      if (sz < n) buf->sent += sz;
      else buf->sent = sz - n;
    }
  else                                                  /* single buffer */
    buf->sent += sz;
  buf->eol = CHARPN;
  return(buf->sent == buf->sendlimit ? 2 : 1);
}

int
TXhtbufUnSend(HTBUF *buf, size_t sz)
/* "Un"sends `sz' bytes (-1 for all) from end of sent (data => sent) area,
 * i.e. preps for re-send of sent data.
 * Should not be used with split-ring buffers; not tested?
 * Returns 0 on error.
 */
{
  static const char     fn[] = "TXhtbufUnSend";
  TXPMBUF               *pmbuf = ((buf->flags & HTBF_NOMSG) ?
                                  TXPMBUF_SUPPRESS : buf->pmbuf);

  if (buf->sent <= buf->sendlimit)              /* one buffer */
    {
      if (sz > buf->sent) sz = buf->sent;
      buf->sent -= sz;
    }
  else
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, NoRingBuffers);
      return(0);
    }
  return(1);
}

int
htbuf_clear(buf)
HTBUF	*buf;
/* "Clears" (empties of data) buffer, without freeing buffer.
 * NOTE: clears HTBF_ERROR.
 */
{
  static CONST char     fn[] = "htbuf_clear";

  CHKMOD(buf, fn, return(0));
  CLROFF(buf);
  if (buf->data != CHARPN) *buf->data = '\0';
  buf->flags &= ~HTBF_ERROR;
  return(1);
}

int
htbuf_release(buf)
HTBUF	*buf;
/* Clears and frees buffer and state info.
 * NOTE: clears HTBF_CONST/HTBF_NOALLOC/HTBF_ERROR flags.
 * NOTE: if HTBF_ATOMIC, caller must ensure call is atomic.
 * Returns 0 on error.
 * Thread-safe iff no Metamorph markup calls were made to `buf'.
 * Signal-safe iff no Metamorph calls and (HTBF_CONST or HTBF_NOALLOC).
 */
{
  if (buf == HTBUFPN) goto done;
  if (buf->data != CHARPN && !(buf->flags & (HTBF_CONST | HTBF_NOALLOC)))
    free(buf->data);
  CLRBUF(buf);
  TXFMTSTATE_RELEASE(&buf->privateFs);
  buf->flags &= ~(HTBF_ERROR | HTBF_CONST | HTBF_NOALLOC);
done:
  return(1);
}

int
htbuf_doinc(buf, sz, hard)
HTBUF	*buf;
size_t	sz;
int     hard;
/* Ensures there is at least `sz' bytes unused room in buffer to write
 * (not including nul terminator).  If `hard', requires all of `sz'.
 * If not `hard', can silently succeed partially, or silently fail
 * if no expansion at all can occur.
 * Returns 0 on error.
 * NOTE: may incur memcpy() with split ring buffers.
 */
{
  static CONST char     fn[] = "htbuf_doinc";
  char                  *odata, *ndata;
  int                   ret = 1, second = -1;
  size_t                reqsz, livesz, osz, n;
  char                  tmp[64];
  TXPMBUF               *pmbufToUse = ((buf->flags & HTBF_NOMSG) ?
                                       TXPMBUF_SUPPRESS : buf->pmbuf);

  (void)TX_ATOMIC_INC(&buf->refcnt);
  /* see also CHKINC(), htbuf_write(): */
  if (buf->sent)                                /* split ring buffer */
    livesz = buf->sz - (htbuf_getavail(buf, CHARPPN, SIZE_TPN, CHARPPN,
                                       SIZE_TPN) + (size_t)1);
  else
    livesz = buf->cnt;
again:
  second++;
  reqsz = livesz + sz;
  if (reqsz < livesz || reqsz >= buf->maxsz)    /* overflow/exceeds limit */
    {
      if (second)
        {
          buf->flags |= HTBF_ERROR;
          txpmbuf_putmsg(pmbufToUse, MERR + MAE, fn,
                         "Will not alloc mem: Internal error");
        }
      else if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(sz))
        {
          buf->flags |= HTBF_ERROR;
          txpmbuf_putmsg(pmbufToUse, MERR + MAE, fn,
                         "Will not alloc mem: Negative increment");
        }
      else if (reqsz < livesz)
        {
          if (!hard)
            {
              sz = (size_t)EPI_OS_SIZE_T_MAX - livesz - (size_t)1;
              goto again;
            }
          buf->flags |= HTBF_ERROR;
          txpmbuf_putmsg(pmbufToUse, MERR + MAE, fn,
                         "Will not alloc mem: Buffer would exceed size_t");
        }
      else
        {
          if (!hard)
            {
              sz = buf->maxsz - livesz - (size_t)1;
              if (sz == (size_t)0) goto err;    /* silent error */
              goto again;
            }
          buf->flags |= HTBF_ERROR;
          txpmbuf_putmsg(pmbufToUse, MERR + MAE, fn,
                        "Will not alloc mem: Buffer would exceed limit of %s",
                         TXprkilo(tmp, sizeof(tmp), (EPI_HUGEUINT)buf->maxsz));
        }
      goto err;
    }
  if (reqsz < buf->sz) goto done;               /* already big enough */

  reqsz++;                                      /* for nul-terminator */
  if (buf->flags & (HTBF_NOALLOC | HTBF_CONST | HTBF_ATOMIC | HTBF_ERROR))
    {
      htbuf_modattempt(buf, fn);
      goto err;
    }

  /* Grab a lot since this is expensive: */
  sz = reqsz - buf->sz;
  n = (buf->sz < ((size_t)1 << 24) ? (buf->sz >> 1) : (buf->sz >> 2));
  if (sz < n) sz = n;                           /* 50% or 25% overalloc */
  if (sz < HTBUF_CHUNK) sz = HTBUF_CHUNK;
  sz += buf->sz;
  if (sz > buf->maxsz || sz < buf->sz) sz = buf->maxsz;
  osz = buf->sz;
  buf->sz = sz;
  odata = buf->data;

  if (buf->cnt == 0 && buf->sent == 0 && buf->sendlimit == 0 &&
      buf->data != CHARPN)                      /* optimization */
    {
      free(buf->data);
      buf->data = CHARPN;
    }

  /* Since we may have to memcpy data anyway for split ring buffer,
   * and we want to retain existing data if alloc fails, avoid realloc():
   */
  if ((ndata = (char *)TXmalloc(pmbufToUse, fn, buf->sz)) == CHARPN)
    {
      buf->flags |= HTBF_ERROR;
    err:
      ret = 0;
      goto done;
    }
  if (buf->sent)                                /* possible split buffer */
    {
      if (buf->cnt < buf->sent)                 /* two original buffers */
        {
          /* Alloc'd size increased, and we're memcpy'ing anyway, so merge: */
          n = osz - buf->sent;
          memcpy(ndata, buf->data + buf->sent, n);
          memcpy(ndata + n, buf->data, buf->cnt);
          if (buf->sendlimit >= buf->sent) buf->sendlimit -= buf->sent;
          else buf->sendlimit += (osz - buf->sent);
          buf->cnt = livesz;
        }
      else                                      /* one original buffer */
        {
          memcpy(ndata, buf->data + buf->sent, livesz);
          buf->sendlimit -= buf->sent;
          buf->cnt -= buf->sent;
        }
      buf->sent = 0;
    }
  else if (buf->cnt > (size_t)0)                /* optimization */
    memcpy(ndata, buf->data, buf->cnt);
  ndata[buf->cnt] = '\0';
  if (buf->data != CHARPN) free(buf->data);
  buf->data = ndata;
  /* Adjust eol pointer to new buf: */
  if (buf->eol != CHARPN && odata != CHARPN)
    buf->eol = buf->data + (buf->eol - odata);

done:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

size_t
htbuf_getavail(buf, data1, sz1, data2, sz2)
HTBUF   *buf;
char    **data1;        /* may be NULL */
size_t  *sz1;           /* may be NULL */
char    **data2;        /* may be NULL */
size_t  *sz2;           /* may be NULL */
/* Gets both parts of split ring buffer's available (to-be-written) space
 * (ie. buf->cnt => buf->sent), not including nul terminator.
 * If writing to returned buffer(s), call htbuf_decavail() immediately after.
 * Returns total size.
 */
{
  size_t        cnt, n1, n2;

  cnt = ((buf->flags & HTBF_ATOMIC) ? (size_t)buf->htbuf_atomcnt : buf->cnt);
  if (data1 != CHARPPN)
    {
      if ((*data1 = buf->data) != CHARPN) *data1 += cnt;
    }
  if (cnt < buf->sent)                          /* one buffer */
    {
      n1 = (buf->sent - cnt) - 1;               /* -1 for nul terminator */
      n2 = 0;
      if (data2 != CHARPPN) *data2 = CHARPN;
    }
  else                                          /* one or two buffers */
    {
      n1 = buf->sz - cnt;
      n2 = buf->sent;
      if (data2 != CHARPPN) *data2 = buf->data;
      if (n2)                                   /* decrement for nul term. */
        {
          if (--n2 == 0 && data2 != CHARPPN) *data2 = CHARPN;
        }
      else if (n1)
        n1--;
    }
  if (sz1 != SIZE_TPN) *sz1 = n1;
  if (sz2 != SIZE_TPN) *sz2 = n2;
  return(n1 + n2);
}

int
htbuf_decavail(buf, sz, sendtoo)
HTBUF   *buf;           /* (in/out) buffer */
size_t  sz;             /* (in) amount to increase live-size */
int     sendtoo;        /* (in) nonzero: increase sendlimit too */
/* Called immediately after htbuf_getavail() and a memcpy, to decrement
 * avail size by `sz' (ie. increment live count).
 *  0 <= `sz' <= htbuf_getavail().  If `sendtoo', also sets sendlimit
 * to live size.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_decavail";
  size_t                cnt, n1, n2;
  int                   ret = 0;
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
  VOLATILE TXATOMINT    asz;                    /* see gcc 2.95.3 note */
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */

  (void)TX_ATOMIC_INC(&buf->refcnt);
  CHKMOD(buf, fn, goto err);                    /* we will nul-terminate */
  if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(sz))       /* sanity check */
    {
      txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                     "Internal error: Negative value %ld", (long)sz);
      goto err;
    }
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
  if (buf->flags & HTBF_ATOMIC)
    cnt = (size_t)buf->htbuf_atomcnt;
  else
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
    cnt = buf->cnt;
  if (cnt < buf->sent)                          /* one avail buffer */
    {
      n1 = buf->sent - cnt;
      if (sz >= n1) goto toolarge;
      goto onebuf;
    }
  else                                          /* one or two avail buffers */
    {
      n1 = buf->sz - cnt;
      n2 = buf->sent;
      if (sz >= n1 + n2)                        /* sanity check */
        {
        toolarge:
          txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
           "Internal error: Increment %lu greater than available buffer size",
                         (long)sz);
          goto err;
        }
      if (sz < n1)                              /* fits in first buf */
        {
        onebuf:
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
          if (buf->flags & HTBF_ATOMIC)
            {
              asz = (TXATOMINT)sz;
              (void)TX_ATOMIC_ADD(&buf->htbuf_atomcnt, asz);
            }
          else
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
            buf->cnt += sz;
        }
      else                                      /* need both bufs */
        {
          sz -= n1;
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
          if (buf->flags & HTBF_ATOMIC)
            buf->htbuf_atomcnt = (TXATOMINT)sz;
          else
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
            buf->cnt = sz;
        }
    }
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
  if (buf->flags & HTBF_ATOMIC)
    {
      if (buf->data != CHARPN) buf->data[buf->htbuf_atomcnt] = '\0';
      if (sendtoo) buf->sendlimit = buf->htbuf_atomcnt;
    }
  else
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
    {
      if (buf->data != CHARPN) buf->data[buf->cnt] = '\0';
      if (sendtoo) buf->sendlimit = buf->cnt;
    }
  ret = 1;
err:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

size_t
htbuf_getdata2(buf, data1, sz1, data2, sz2)
HTBUF   *buf;
char    **data1;        /* may be NULL */
size_t  *sz1;           /* may be NULL */
char    **data2;        /* may be NULL */
size_t  *sz2;           /* may be NULL */
/* Gets both parts of split ring buffer's live (sent => cnt) space.
 * Returns total size.
 */
{
  size_t        cnt, n1, n2;

  cnt = ((buf->flags & HTBF_ATOMIC) ? (size_t)buf->htbuf_atomcnt : buf->cnt);
  if (data1 != CHARPPN)
    {
      if ((*data1 = buf->data) != CHARPN) *data1 += buf->sent;
    }
  if (buf->sent <= cnt)                         /* one buffer */
    {
      n1 = cnt - buf->sent;
      n2 = 0;
      if (data2 != CHARPPN) *data2 = CHARPN;
    }
  else                                          /* one or two buffers */
    {
      n1 = buf->sz - buf->sent;
      n2 = cnt;
      if (data2 != CHARPPN) *data2 = buf->data;
    }
  if (sz1 != SIZE_TPN) *sz1 = n1;
  if (sz2 != SIZE_TPN) *sz2 = n2;
  return(n1 + n2);
}

int
htbuf_write(buf, data, sz)
HTBUF           *buf;
CONST char      *data;
size_t          sz;
/* Writes new data (at `buf->cnt'), advancing `buf->cnt'.
 * Will advance `buf->sendlimit' too (ie. ready for htbuf_flush...()).
 * NOTE: may incur large memcpy() with split ring buffers w/o maxsz.
 * May be used with HTBUF_STDOUT.
 */
{
  static CONST char     fn[] = "htbuf_write";
  int                   ret = 0;
  size_t                avail, sz1, sz2, n;
  char                  *d1, *d2;

  if (buf == HTBUF_STDOUT)
    return(fwrite(data, 1, sz, stdout) == sz);

  (void)TX_ATOMIC_INC(&buf->refcnt);
  CHKMOD(buf, fn, goto err);
  if (buf->sent)                                /* split ring buffer */
    {
      avail = htbuf_getavail(buf, &d1, &sz1, &d2, &sz2);
      if (sz <= avail)                          /* fits as-is */
        {
          n = (sz < sz1 ? sz : sz1);
          if (n > (size_t)0)
            {
              memcpy(d1, data, n);
              data += n;
              sz -= n;
              buf->cnt += n;
              if (buf->cnt == buf->sz) buf->cnt = 0;    /* rollover */
            }
          if (sz > (size_t)0)                   /* more to copy */
            {
              memcpy(d2, data, sz);
              buf->cnt = sz;                    /* rollover */
            }
          buf->data[buf->cnt] = '\0';
        }
      else
        goto doinc;                             /* htbuf_inc() merges buf */
    }
  else
    {
      /* see also CHKINC(), htbuf_inc(): */
      if ((buf->cnt + sz >= buf->sz || buf->cnt + sz < buf->cnt))
        {
        doinc:
          if (!htbuf_inc(buf, sz))
            {
              ret = 0;
              /* Allow partial copy (with error return), eg. HTBF_NOALLOC: */
              if (buf->sz > buf->cnt &&
                  buf->cnt > buf->sent && buf->cnt > buf->sendlimit)
                sz = (buf->sz - buf->cnt) - (size_t)1;
              else
                goto err;
            }
        }

      if (sz > 0)
        {
          memcpy(buf->data + buf->cnt, data, sz);
          buf->cnt += sz;
        }
      buf->data[buf->cnt] = '\0';
    }
  buf->sendlimit = buf->cnt;                    /* ready to be sent */
  ret = 1;
err:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

int
htbuf_writehold(buf, data, sz)
HTBUF           *buf;
CONST char      *data;
size_t          sz;
/* Like htbuf_write(), but does not advance `buf->sendlimit',
 * ie. data will not be htbuf_flush...()-sent until htbuf_rewrite() called.
 */
{
  size_t        sav;
  int           ret;

  sav = buf->sendlimit;
  ret = htbuf_write(buf, data, sz);
  buf->sendlimit = sav;
  return(ret);
}

int
htbuf_rewrite(buf, data, sz)
HTBUF           *buf;
CONST char      *data;          /* may be NULL */
size_t          sz;             /* -1:  whole live-data size */
/* Re-writes `sz' bytes of `data' to `buf', ie. at `buf->sendlimit',
 * overwriting pre-written data.  `data' may be NULL to just increment.
 * Re-written data is eligible for flushing via htbuf_flush[nblk]().
 * NOTE: `sz' must be less than or equal to size of already-written data
 * (ie. htbuf_getdatasz()); partial write (with error return) if larger.
 */
{
  static CONST char     fn[] = "htbuf_rewrite";
  int                   ret = 0;
  size_t                n;

  (void)TX_ATOMIC_INC(&buf->refcnt);
  CHKMOD(buf, fn, goto err);
  if (sz == (size_t)(-1))
    {
      if (data != CHARPN)
        {
          txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, "-1 size with data");
          goto err;
        }
      buf->sendlimit = buf->cnt;
      goto ok;
    }
  n = (buf->cnt < buf->sendlimit ? buf->sz - buf->sendlimit :
       buf->cnt - buf->sendlimit);
  if (n > sz) n = sz;
  if (n)
    {
      if (data != CHARPN) memcpy(buf->data + buf->sendlimit, data, n);
      buf->sendlimit += n;
    }
  if (buf->sendlimit == buf->sz) buf->sendlimit = 0;
  if (sz > n)                                   /* still more data */
    {
      sz -= n;
      if (data != CHARPN) data += n;
      if (buf->sendlimit < buf->cnt)            /* can use 2nd buffer */
        {
          n = buf->cnt - buf->sendlimit;
          if (n > sz) n = sz;
          if (data != CHARPN) memcpy(buf->data + buf->sendlimit, data, n);
          buf->sendlimit += n;
          sz -= n;
        }
      if (sz)                                   /* still more data */
        {
          if (!(buf->flags & HTBF_NOMSG))
            txpmbuf_putmsg(buf->pmbuf, MERR + MAE, fn,
                           "Size exceeds buffer data");
          goto err;
        }
    }
ok:
  ret = 1;
err:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

int
htbuf_insert(buf, insOffset, insData, insSz)
HTBUF           *buf;
size_t          insOffset;      /* live data offset (from `buf->sent') */
CONST char      *insData;
size_t          insSz;
/* Like htbuf_write(), but inserts data at `insOffset' into live data
 * (htbuf_getdatasz()), moving existing data to the right.
 * NOTE: incurs a memmove() for existing data to the right.
 * NOTE: may incur a memcpy().
 */
{
  static CONST char     fn[] = "htbuf_insert";
  int                   ret;
  size_t                avail, sz1, sz2, n, off2, mvChunkSz;
  char                  *d1, *d2;

  (void)TX_ATOMIC_INC(&buf->refcnt);
  CHKMOD(buf, fn, goto err);
  if (buf->sent)                                /* split ring buffer */
    {
      /* Compute available space ala htbuf_getavail(): */
      if (buf->cnt < buf->sent)                 /* avail. space monolithic */
        avail = (buf->sent - buf->cnt) - 1;     /* -1 for nul terminator */
      else                                      /* one or two buffers */
        {
          sz1 = buf->sz - buf->cnt;
          sz2 = buf->sent;
          if (sz2) sz2--;                       /* decrement for nul term. */
          else if (sz1) sz1--;
          avail = sz1 + sz2;
        }
      /* If `insData' will not fit, realloc buffer; htbuf_doinc() will
       * also make live (sent => cnt) data monolithic:
       */
      if (insSz > avail) goto doinc;
      if (insSz <= (size_t)0) goto noData;
      /* Insert into live data, in 3 stages: */
      n = htbuf_getdata2(buf, &d1, &sz1, &d2, &sz2); /*get live data bufs/sz*/
      if (insOffset > n)                        /* sanity check */
        insOffset = n;
      else if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(insOffset))
        insOffset = (size_t)0;
      /* 1) make room in 2nd live block, for `insData' and/or
       * to-be-moved-right existing data from 1st live block:
       */
      if (sz2 > 0)                              /* have 2nd live block */
        {
          off2 = (insOffset > sz1 ? insOffset - sz1 : (size_t)0);
          n = sz2 - off2;                       /* existing data to move */
          if (n > (size_t)0)
            memmove(d2 + off2 + insSz, d2 + off2, n);
        }
      /* 2) move existing data from 1st live block to 2nd, if needed: */
      if (insOffset < sz1)                      /* inserting into 1st block */
        {
          mvChunkSz = sz1 - insOffset;          /*total existing len to move*/
          if (mvChunkSz > insSz)                /* two-part move */
            {
              memmove(d2, d1 + sz1 - insSz, insSz); /* end part to 1st block*/
              memmove(d1 + insOffset + insSz, d1 + insOffset,
                      mvChunkSz - insSz);       /* first part slide right */
              /* 3) insert new data into first block: */
              memcpy(d1 + insOffset, insData, insSz);
            }
          else                                  /* one-part move */
            {
              memmove(d2 + insSz - mvChunkSz, d1 + insOffset, mvChunkSz);
              /* 3) insert new data into first and possibly 2nd blocks: */
              memcpy(d1 + insOffset, insData, mvChunkSz);
              if (insSz > mvChunkSz)            /* remainder to 2nd block */
                memcpy(d2, insData + mvChunkSz, insSz - mvChunkSz);
            }
        }
      else                                      /* inserting into 2nd block */
        memcpy(d2 + insOffset - sz1, insData, insSz);
      goto incCnt;
    }
  else                                          /* monolithic buffer */
    {
      /* see also CHKINC(), htbuf_inc(): */
      if ((buf->cnt + insSz >= buf->sz ||       /* exceeds current alloc */
           buf->cnt + insSz < buf->cnt))        /* overflows size_t */
        {
        doinc:
          if (!htbuf_inc(buf, insSz))           /* realloc() failed */
            {
              ret = 0;                          /* error return */
              /* Allow partial copy (with error return), eg. HTBF_NOALLOC: */
              if (buf->sz > buf->cnt &&         /* available space */
                  buf->cnt > buf->sent && buf->cnt > buf->sendlimit)
                insSz = (buf->sz - buf->cnt) - (size_t)1;
              else
                goto err;
            }
        }

      if (insSz > 0)                            /* non-empty `insData' */
        {
          if (insOffset > buf->cnt)             /* sanity check */
            insOffset = buf->cnt;
          else if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(insOffset))
            insOffset = (size_t)0;
          n = buf->cnt - insOffset;             /* existing data to move */
          if (n > 0)
            memmove(buf->data + insOffset + insSz, buf->data + insOffset,n);
          memcpy(buf->data + insOffset, insData, insSz);
        incCnt:
          buf->cnt += insSz;
        }
    noData:
      buf->data[buf->cnt] = '\0';
    }
  buf->sendlimit = buf->cnt;                    /* ready to be sent */
  ret = 1;                                      /* full success */
  goto done;

err:
  ret = 0;                                      /* error */
done:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

static int htbuf_atomicalloc ARGS((HTBUF *buf, size_t reqsz, char **data,
                                   size_t *actsz));
static int
htbuf_atomicalloc(buf, reqsz, data, actsz)
HTBUF           *buf;
size_t          reqsz;
char            **data;
size_t          *actsz;
{
  static CONST char     fn[] = "htbuf_atomicalloc";
  int                   ret = 1;
  /* WTF gcc 2.95.3 -march=pentium -O on linux 2.4.2 will clear 2nd arg
   * to TX_ATOMIC_ADD() if we do not make it volatile:
   */
  VOLATILE TXATOMINT    off, sz, totsz, avail, back;

  *data = CHARPN;
  *actsz = 0;
  if ((buf->flags & (HTBF_CONST | HTBF_ATOMIC | HTBF_ERROR)) != HTBF_ATOMIC)
    {
      if (!(buf->flags & (HTBF_ATOMIC | HTBF_NOMSG)))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NonAtomic);
      else
        htbuf_modattempt(buf, fn);
      goto err;
    }
  if (buf->sent)                                /* split ring buffer */
    {
      txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NoRingBuffers);
    err:
      buf->flags |= HTBF_ERROR;
      return(0);
    }

  totsz = (TXATOMINT)reqsz;
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
  if ((size_t)totsz != reqsz) goto err1;
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
  if (totsz > 0)
    {
      /* Try to allocate some room for `buf', as atomically as
       * possible since we may be interrupted.  The data copy
       * won't be atomic (due to memcpy), but at least our pointers
       * should stay sane:
       */
      if ((avail = (TXATOMINT)((buf->htbuf_atomsz-1)-buf->htbuf_atomcnt)) > 0)
        {                                       /* some room available */
          sz = totsz;
          if (sz > avail) sz = avail;           /* not enough room */
          off = TX_ATOMIC_ADD((TXATOMINT *)&buf->htbuf_atomcnt, sz);
          buf->sendlimit = (size_t)buf->htbuf_atomcnt;
          if (off < 0)                          /* should not happen */
            {
              if (off + sz >= 0) sz += off;
              else sz = 0;
              off = 0;
            }
          if ((back = (off + sz) - (TXATOMINT)(buf->htbuf_atomsz - 1)) > 0)
            {                                   /* not enough room */
              if (back > sz) back = sz;         /* totally beyond EOB */
              (void)TX_ATOMIC_SUB((TXATOMINT *)&buf->htbuf_atomcnt, back);
              buf->sendlimit = (size_t)buf->htbuf_atomcnt;
              sz -= back;
            }
          *data = buf->data + off;
          *actsz = (size_t)sz;
        }
      else
        sz = 0;
      if (sz < totsz)
        {
#if TXATOMINTBITS != EPI_OS_SIZE_T_BITS
        err1:
#endif /* TXATOMINTBITS != EPI_OS_SIZE_T_BITS */
          buf->flags |= HTBF_ERROR;
          ret = 0;
          if (!(buf->flags & HTBF_NOMSG))
            txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn,
                           "Out of atomic buffer space");
        }
    }
  return(ret);
}

int
htbuf_atomicwrite(buf, data, len)
HTBUF           *buf;
CONST char      *data;
size_t          len;
/* Like htbuf_write(), but as atomically as possible (without blocking).
 * Buffer alloc should be thread-safe and signal-safe, if
 * TX_ATOMIC_THREAD_SAFE.
 * Actual buffer write may be unsafe (could be interrupted).
 * NOTE: buffer is not nul-terminated until htbuf_atomicgetdata().
 * See also htbuf_atomicpf().
 */
{
  char          *loc;
  size_t        locsz;
  int           ret;

  (void)TX_ATOMIC_INC(&buf->refcnt);
  ret = htbuf_atomicalloc(buf, len, &loc, &locsz);
  if (locsz > 0) memcpy(loc, data, locsz);
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

typedef struct ASPFSTATE_tag
{
  char  *st, *end;
}
ASPFSTATE;

static void prstrcb ARGS((void *data, char *s, size_t sz));
static void
prstrcb(data, s, sz)
void    *data;
char    *s;
size_t  sz;
{
  ASPFSTATE     *spt = (ASPFSTATE *)data;

  if (spt->st < spt->end)
    {
      if (spt->st + sz > spt->end) sz = spt->end - spt->st;
      memcpy(spt->st, s, sz);
      spt->st += sz;
    }
}

int CDECL
#ifdef EPI_HAVE_STDARG
htbuf_atomicpf ARGS((HTBUF *buf, CONST char *fmt, ...))
/* Usually thread-safe and signal-safe.  See also htbuf_atomicwrite().
 * Do not attempt Metamorph or other complex/single-threaded ops.
 * NOTE: buffer is not nul-terminated until htbuf_atomicgetdata().
 * See also htbuf_atomicwrite().
 */
{
  va_list	argp;
  int           ret, sz;
  char          *loc;
  size_t        locsz, argn;
  ASPFSTATE     spt;
  HTPFARG       args[32];
  char          stk[1024];

  va_start(argp, fmt);
#else   /* !EPI_HAVE_STDARG */
/* VARARGS */   /* for lint */
htbuf_atomicpf(va_alist)
va_dcl
{
  va_list	argp;
  HTBUF		*buf;
  CONST char	*fmt;
  int           ret, sz;
  char          *loc;
  size_t        locsz, argn;
  ASPFSTATE     spt;
  HTPFARG       args[32];
  char          stk[1024];

  va_start(argp);
  buf = va_arg(argp, HTBUF *);
  fmt = va_arg(argp, CONST char *);
#endif  /* !EPI_HAVE_STDARG */

  (void)TX_ATOMIC_INC(&buf->refcnt);
  /* Copy args to `args' in case we need them again; va_copy() would
   * otherwise be needed on some platforms, and it can invoke malloc:
   */
  argn = sizeof(args)/sizeof(args[0]);
  sz = htvsnpf(stk, sizeof(stk), fmt, HTPFF_WRHTPFARGS, buf->fmtcp, buf->fs,
               argp, args, &argn, buf->pmbuf);
  if ((size_t)sz < sizeof(stk))
    ret = htbuf_atomicwrite(buf, stk, sz);
  else
    {
      ret = htbuf_atomicalloc(buf, sz, &loc, &locsz);
      if (locsz > 0)
        {
          /* same as htvsnpf(), but without nul-termination: */
          spt.st = loc;
          spt.end = loc + locsz;
          if (argn > sizeof(args)/sizeof(args[0]))
            argn = sizeof(args)/sizeof(args[0]);
          htpfengine(fmt, (size_t)(-1), HTPFF_RDHTPFARGS, buf->fmtcp,
                     buf->fs, argp, HTPFCBPN, NULL, args, &argn,
                     prstrcb, (void *)&spt, buf->pmbuf);
        }
    }
  va_end(argp);
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

int
htbuf_atomicgetdata(buf, data)
VOLATILE HTBUF  *buf;
char            **data;
/* Locks `buf' from htbuf_atomicwrite()/htbuf_atomicpf().
 * Returns strlen()-size of buffer (also to be passed to
 * htbuf_atomicgetdatadone()), or -1 on error.  Sets `*data' to buffer.
 * Should be called by single thread, though another htbuf_atomicgetdata()
 * should get the same size buffer.  Must call htbuf_atomicgetdatadone() ASAP.
 */
{
  static CONST char     fn[] = "htbuf_atomicgetdata";
  VOLATILE TXATOMINT    sz;     /* wtf see gcc comment above */

  if (!(buf->flags & HTBF_ATOMIC))
    {
      if (!(buf->flags & HTBF_NOMSG))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NonAtomic);
      buf->flags |= HTBF_ERROR;
      return(-1);
    }

  /* WTF gcc 4.1.2 20070626 i686-unknown-linux2.6.22.14 will not do
   * the second TX_ATOMIC_INC() here (of `buf->agetcnt') with -O2;
   * workaround found after random tests is to inc/dec by 2:
   */
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && __GNUC__ == 4 && __GNUC_MINOR__ == 1
#  define AGETCNT_INC(a)        TX_ATOMIC_ADD((a), 2)
#  define AGETCNT_DEC(a)        TX_ATOMIC_SUB((a), 2)
#  define AGETCNT_ORG           2
#else /* working platforms */
#  define AGETCNT_INC(a)        TX_ATOMIC_INC(a)
#  define AGETCNT_DEC(a)        TX_ATOMIC_DEC(a)
#  define AGETCNT_ORG           1
#endif /* working platforms */

  (void)TX_ATOMIC_INC(&buf->refcnt);
  if (AGETCNT_INC(&buf->agetcnt) == 0)          /* first call */
    buf->agetsz = sz = TX_ATOMIC_ADD((TXATOMINT *)&buf->htbuf_atomcnt,
                                     buf->htbuf_atomsz);
  else
    sz = buf->agetsz;

  /* WTF gcc 2.95.3 -O -march=pentium under linux 2.4.2 modifies
   * buf->htbuf_atomsz above, in TX_ATOMIC_ADD().  This code appears to
   * defeat the broken optimization:  KNG 20041020
   */
#if defined(__GNUC__) && __GNUC__ == 2 && !defined(TX_ATOMIC_FALLBACK_FUNCS)
  {
    char        tmp[16], *s = tmp + sizeof(tmp);
    int         x = buf->htbuf_atomsz;
    do { *(--s) = '0' + (x % 10); x /= 10; break; } while (x > 0);
  }
#endif

  if (sz > buf->htbuf_atomsz) sz = buf->htbuf_atomsz;
  if (data != CHARPPN) *data = buf->data;
  return(sz);
}

int
htbuf_atomicgetdatadone(buf, sz)
VOLATILE HTBUF  *buf;
TXATOMINT       sz;     /* return val of preceding htbuf_atomicgetdata() */
/* Clears htbuf_atomicgetdata() lock, as well as all data in buffer.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "htbuf_atomicgetdatadone";

  if (!(buf->flags & HTBF_ATOMIC))
    {
      if (!(buf->flags & HTBF_NOMSG))
        txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NonAtomic);
      buf->flags |= HTBF_ERROR;
      return(0);
    }

  if (AGETCNT_DEC(&buf->agetcnt) == AGETCNT_ORG)        /* last call */
    {
      (void)TX_ATOMIC_SUB((TXATOMINT *)&buf->htbuf_atomcnt,
                          buf->htbuf_atomsz + sz);
      buf->agetsz = 0;
    }
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(1);
}

int
htbuf_unhtml(buf, s, len)
HTBUF	*buf;
char	*s;
size_t	len;
{
  static CONST char     fn[] = "htbuf_unhtml";
  size_t                n, a, dtot;
  int                   ret = 0, didresize = 0;
  CONST char            *sp;
  UTF                   state;

  (void)TX_ATOMIC_INC(&buf->refcnt);
  CHKMOD(buf, fn, goto err);
  CHKINC(buf, len + 10, goto err);
  if (buf->sent)                                /* wtf */
    {
      txpmbuf_putmsg(buf->pmbuf, MERR + UGE, fn, NoRingBuffers);
      goto err;
    }
again:
  n = (buf->sz - buf->cnt) - 1;
  dtot = 0;
  state = (UTF)0;
  sp = s;
  a = buf->unhtml(buf->data + buf->cnt, n, &dtot, &sp, len,
                  (buf->unhtmlFlags | UTF_FINAL | UTF_START |
                   ((buf->flags & HTBF_DO8BIT) ? UTF_DO8BIT : (UTF)0)),
                  &state, 0, (buf->fmtcp ? buf->fmtcp->htpfobj : HTPFOBJPN),
                  buf->pmbuf);
  if (a > n)                                    /* need to resize */
    {
      if (didresize++)
        {
          if (!(buf->flags & HTBF_NOMSG))
            txpmbuf_putmsg(buf->pmbuf, MERR, fn,
                           "Internal error: buffer resize not enough");
          buf->flags |= HTBF_ERROR;
          goto err;
        }
      if (!htbuf_inc(buf, a)) goto err;
      goto again;
    }
  buf->cnt += a;
  buf->data[buf->cnt] = '\0';
  ret = 1;
err:
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

int
htbuf_vpf(buf, fmt, fmtsz, flags, argp, args, argnp)
HTBUF           *buf;
CONST char      *fmt;
size_t          fmtsz;
HTPFF           flags;
HTPF_SYS_VA_LIST argp;
HTPFARG         *args;
size_t          *argnp;
/* May be passed HTBUF_STDOUT too.
 * Returns 0 on error.
 */
{
  int   cnt;

  if (buf == HTBUF_STDOUT)
    {
      htpfengine(fmt, fmtsz, flags, TXFMTCPPN, TXFMTSTATEPN, argp,
                 HTPFCBPN, NULL, args, argnp, TXhtpfFileCb,
                 (void *)stdout, TXPMBUFPN);
      cnt = (ferror(stdout) ? 0 : 1);
    }
  else                                          /* normal print to a buffer */
    {
      (void)TX_ATOMIC_INC(&buf->refcnt);
      htpfengine(fmt, fmtsz, flags, buf->fmtcp, buf->fs, argp, HTPFCBPN,
                 NULL, args, argnp, (HTPFOUTCB *)htbuf_write, (void *)buf,
                 buf->pmbuf);
      if (buf->data == CHARPN)
        htbuf_write(buf, "", 0);                /* guarantee termination */
      cnt = ((buf->flags & HTBF_ERROR) ? 0 : 1);
      (void)TX_ATOMIC_DEC(&buf->refcnt);
    }
  return(cnt);
}

int CDECL
#ifdef EPI_HAVE_STDARG
htbuf_pf ARGS((HTBUF *buf, CONST char *fmt, ...))
/* May be passed HTBUF_STDOUT too.
 */
{
  va_list	argp;
  int           ret;

  va_start(argp, fmt);
#else   /* !EPI_HAVE_STDARG */
/* VARARGS */   /* for lint */
htbuf_pf(va_alist)
va_dcl
/* May be passed HTBUF_STDOUT too.
 */
{
  va_list	argp;
  HTBUF		*buf;
  CONST char	*fmt;
  int           ret;

  va_start(argp);
  buf = va_arg(argp, HTBUF *);
  fmt = va_arg(argp, CONST char *);
#endif  /* !EPI_HAVE_STDARG */

  if (buf != HTBUF_STDOUT) (void)TX_ATOMIC_INC(&buf->refcnt);
  ret = htbuf_vpf(buf, fmt, (size_t)(-1), (HTPFF)0, argp, HTPFARGPN, NULL);
  va_end(argp);
  if (buf != HTBUF_STDOUT) (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(ret);
}

int
htbuf_cpf(buf, fmt, fmtsz, flags, cb, data)
HTBUF           *buf;
CONST char      *fmt;
size_t          fmtsz;
HTPFF           flags;
HTPFCB          *cb;
void            *data;
{
  int           cnt;
  union
  {
    va_list     argp;
    int         dum;
  }
  dum;

  dum.dum = 0;                                  /* shut up compiler warning */
  flags &= ~(HTPFF_RDHTPFARGS | HTPFF_WRHTPFARGS);
  (void)TX_ATOMIC_INC(&buf->refcnt);
  htpfengine(fmt, fmtsz, flags, buf->fmtcp, buf->fs, dum.argp, cb, data,
             HTPFARGPN, NULL, (HTPFOUTCB *)htbuf_write, (void *)buf,
             buf->pmbuf);
  if (buf->data == CHARPN)
    htbuf_write(buf, "", 0);                    /* guarantee termination */
  cnt = ((buf->flags & HTBF_ERROR) ? 0 : 1);
  (void)TX_ATOMIC_DEC(&buf->refcnt);
  return(cnt);
}

int
htbuf_setfmtstate(buf, fs)
HTBUF           *buf;
TXFMTSTATE      *fs;    /* (in, opt.) */
/* Attaches state `fs' to `buf', which will own it and free it on close.
 * Sets state to internal if `fs' is NULL.
 * Returns 0 on error.
 */
{
  if (buf->fs != TXFMTSTATEPN && buf->fs != &buf->privateFs)
    TxfmtstateClose(buf->fs);
  buf->fs = (fs == TXFMTSTATEPN ? &buf->privateFs : fs);
  return(1);
}

TXFMTSTATE *
htbuf_getfmtstate(buf)
HTBUF   *buf;
/* Returns current state info, which may be private.
 */
{
  return(buf->fs);
}

int
htbuf_setfmtcp(buf, fmtcp, htbufOwns)
HTBUF           *buf;
TXFMTCP         *fmtcp;
int             htbufOwns;      /* (in) nonzero: `buf' to own `fmtcp' */
/* Attaches settings `fmtcp' to `buf', which will own it and free it on close
 * iff `htbufOwns' is nonzero.
 * Sets settings to default if `fmtcp' is NULL.
 * Returns 0 on error.
 */
{
  if (buf->fmtcp != TXFMTCPPN && (buf->flags & HTBF_OWNFMTCP))
    TxfmtcpClose(buf->fmtcp);
  buf->fmtcp = fmtcp;
  if (htbufOwns)
    buf->flags |= HTBF_OWNFMTCP;
  else
    buf->flags &= ~HTBF_OWNFMTCP;
  return(1);
}

TXFMTCP *
htbuf_getfmtcp(buf)
HTBUF   *buf;
/* Returns current settings pointer, which may be NULL for defaults.
 */
{
  return((TXFMTCP *)buf->fmtcp);
}

int
htskipeol(sp, e)
char    **sp;
char    *e;
/* Advances `*sp' over 1 line's EOL char(s), if at EOL.  Returns 1 if
 * was at EOL; 0 if not (`*sp' unchanged); 2 if need to read
 * additional char to make sure (possible split between CR and LF;
 * `*sp' still advanced: NOTE caller needs to restore it then).  `e',
 * if non-NULL, is end of buffer (won't go past).
 */
{
  char  *s = *sp;
  int   ret = 1;

  if (e == CHARPN) e = s + 3;           /* set end beyond what we need */
  if (s < e)
    {
      if (*s == CR)
        {
          s++;
          if (s < e)
            {
              if (*s == LF) s++;
            }
          else
            ret = 2;                    /* might be LF (of CRLF) next */
        }
      else if (*s == LF)
        {
          s++;
          /* KNG 990107 Don't wait for CR after LF: unlikely to ever see it,
           * and may hang if other end nevers sends it (eg. LF-delimited):
           * KNG 20070220 do not eat CR after LF even if already present;
           * no platform we know uses LFCR delimiters, and eating it if
           * present but not waiting for it if not (990107 fix) can lead
           * to inconsistent line counts of the same data if the read()
           * lengths vary.
           */
        }
      else
        return(0);                      /* not at any EOL */
    }
  else
    ret = 2;                            /* might be EOL char next */
  *sp = s;
  return(ret);
}

int
TXskipEolBackwards(bufStart, curLoc)
CONST char      *bufStart;      /* (in, opt.) start of buffer */
CONST char      **curLoc;       /* (in/out) current location to back up */
/* Backs `*curLoc' up over (to start of) 1 line's EOL char(s), if just
 * past EOL.  Returns 1 if was just past EOL; 0 if not (`*curLoc'
 * unchanged); 2 if need to read additional char to make sure
 * (possible split between CR and LF; `*curLoc' still backed up: NOTE
 * caller needs to restore it then).  `bufStart', if non-NULL, is
 * start of buffer (will not back up before it).
 */
{
  CONST char    *s = *curLoc;
  int           ret = 1;

  if (bufStart == CHARPN) bufStart = s - 3;     /* set beyond what we need */
  if (s > bufStart)
    {
      if (s[-1] == LF)
        {
          s--;
          if (s > bufStart)
            {
              if (s[-1] == CR) s--;
            }
          else
            ret = 2;                            /* might be CR (of CRLF) b4 */
        }
      else if (s[-1] == CR)
        {
          s--;
        }
      else
        return(0);                              /* not just-past any EOL */
    }
  else
    ret = 2;                                    /* might be EOL char before */
  *curLoc = s;
  return(ret);
}
