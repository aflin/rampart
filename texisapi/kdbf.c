#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <errno.h>
#include "os.h"
#include "mmsg.h"
#include "dbquery.h"
#include "texint.h"	/* for TXgetoff, TXsetrecid  KNG 960430 */
#include "fbtree.h"
#include "kdbf.h"
#include "kdbfi.h"
#include "kfbtree.h"
#include "cgi.h"                                /* for htsnpf() */

#ifndef S_ISDIR
#  define S_ISDIR(m)    ((m) & S_IFDIR)
#endif

#define II(cnt, op)	((df)->cnt++, op)

#ifdef MSDOS
#  define NULLFILE      "nul:"
#  define MAXSTDFILENO  4
#else
#  define NULLFILE      "/dev/null"
#  define MAXSTDFILENO  STDERR_FILENO
#endif

#define BCHK	\
  ((df)->in_btree == 1 ? " (B-tree)" : (df)->in_btree > 1 ? " (B-tree>1)" : "")

/******************************************************************/


int     TxKdbfQuickOpen = 0;
/* TxKdbfIoStats:
   0      no stats
   1      non-SYS KDBF files
   2      all KDBF files
   bit 2  open/closes
 */
int     TxKdbfIoStats = 0;              /* see also setprop.c */
char    *TxKdbfIoStatsFile = CHARPN;    /* see also setprop.c */
#define IOSTATS (TxKdbfIoStats & 0x3)
/* TxKdbfVerify:
 * bit 0:  verify free-tree deletes (dup check)
 * bit 1:  verify free-tree inserts (dup check)
 * bit 2:  verify free blocks (check if alloced first)
 */
int     TxKdbfVerify = 0;
static int      KdbfNumOpen = 0, KdbfNumMax = 0;

/* TXkdbfOptimize (set with kdbf_setoptimize()):
 * bit 0:  lseek to same known location should be skipped
 * bit 1:  use pre/postbufsz in internal B-tree
 * bit 2:  recycle too-small KDBF_IOCTL_READBUFSZ data to main buf (and back)
 * Set/get with kdbf_setoptimize()/kdbf_getoptmize()
 * See also TXbtsetoptmize()/TXbtgetoptmize()
 */
#define KDBF_OPTIMIZE_ALL       0x7
#define KDBF_OPTIMIZE_DEFAULT   KDBF_OPTIMIZE_ALL
static int      TXkdbfOptimize = KDBF_OPTIMIZE_DEFAULT;

/* TXtraceKdbf: see kdbfi.h
 */
CONST char      TXtraceKdbfBtreeOp[] = "B-tree op ";
CONST char      TXtraceKdbfDepthStr[] = "++++++++++?";
int             TXtraceKdbf = 0;
char            *TXtraceKdbfFile = CHARPN;      /* alloced */
TXPMBUF         *TXtraceKdbfPmbuf = TXPMBUFPN;
static CONST char * CONST       TXioctlNames[KDBF_IOCTL_NUM] =
{
#undef I
#define I(sym, argtype, seek)   #sym,
KDBF_IOCTL_SYMS_LIST
#undef I
};
static CONST byte               TXioctlArgTypes[KDBF_IOCTL_NUM] =
{
#undef I
#define I(sym, argtype, seek)   argtype,
KDBF_IOCTL_SYMS_LIST
#undef I
};
static CONST byte               TXioctlSeekSig[KDBF_IOCTL_NUM] =
{
#undef I
#define I(sym, argtype, seek)   seek,
KDBF_IOCTL_SYMS_LIST
#undef I
};

enum {
  KR_UNK,
  KR_NO_SPACE,
  KR_READ_PAST_EOF,
  KR_NO_MEM,
  KR_NUM_ERRS
};
static int	ErrGuess = KR_UNK;	/* guess at error if errno == 0 */
#ifdef _WIN32
#  define SAVE_ERR()    { TXERRTYPE errNum; int errNo, errGuess; \
    errNum = TXgeterror(); errNo = errno; errGuess = ErrGuess
#  define RESTORE_ERR() ErrGuess=errGuess; errno=errNo; TXseterror(errNum); }
#else /* !_WIN32 */
#  define SAVE_ERR()    { TXERRTYPE errNum; int errGuess;       \
    errNum = TXgeterror(); errGuess = ErrGuess
#  define RESTORE_ERR() ErrGuess = errGuess; TXseterror(errNum); }
#endif /* !_WIN32 */
#define CLEAR_ERR()     (TXclearError(), ErrGuess = 0)
static CONST char * CONST       ErrStr[KR_NUM_ERRS] =
{
  "Unknown",
  "No space left on filesystem",
  "Read past EOF",
  "Out of memory",
};

static CONST char       CantReadPtrs[] =
  "Cannot read start pointers from KDBF file %s: %s";
static CONST char       CantWritePtrs[] = 
  "Cannot write start pointers (0x%wx and 0x%wx) to KDBF file %s: %s";
static CONST char       CantWriteRdOnly[] =
  "Cannot write to KDBF file %s: No file write permission";
static CONST char       CantWriteBytes[] =
  "Cannot write 0x%wx bytes at 0x%wx to KDBF file %s: %s";
static CONST char       CorruptBlkHdr[] =
  "Corrupt block header at 0x%wx in KDBF file %s";
static CONST char       CorruptOperation[] =
  "Corrupt operation at 0x%wx in KDBF file %s: %s";
static CONST char       CantReadOverwrite[] =
  "Cannot read from KDBF file %s: KDBF_IOCTL_OVERWRITE set";
static CONST char       CantWriteCorrupt[] =
  "Cannot write to KDBF file %s: Corruption detected";
static CONST char       NoAllocStarted[] =
  "Internal error: No alloc started for KDBF file %s";
static const char       InvalidFunctionWhileInFreeTreeFmt[] =
  "Invalid function call while processing free-tree of KDBF file `%s'";
static CONST char       BitFileSizeExceeded[] =
#if EPI_OFF_T_BITS == 32
  "32-bit file size limit would be exceeded";
#elif EPI_OFF_T_BITS == 64
  "64-bit file size limit would be exceeded";
#else
  "OS file size limit would be exceeded";
#endif
static CONST char       BadParamBlkSize[] =
  "Bad parameter (block size 0x%wx) for KDBF file %s";
#define BADBLKSIZE(df, sz)      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, \
  BadParamBlkSize, (EPI_HUGEUINT)(sz), (df)->fn)
static CONST char       BadParamBufSize[] =
  "Bad parameter (buffer size 0x%wx) for KDBF file %s";
#define BADBUFSIZE(df, sz)      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, \
  BadParamBufSize, (EPI_HUGEUINT)(sz), (df)->fn)

#ifdef KDBF_HIGHIO
   These may break;
#  define KDBF_CLOSE(df)		fclose((df)->fh)
#  define KDBF_FILENO(df)               fileno((df)->fh)
#else	/* !KDBF_HIGHIO */
#  define KDBF_CLOSE(df)		close((df)->fh)
#  define KDBF_FILENO(df)               (df)->fh
#endif	/* !KDBF_HIGHIO */

#define MALLOC(fn, df, n)                                       \
  (CLEAR_ERR(), (df)->mallocs++, (df)->mallocbytes += (EPI_HUGEUINT)(n), TXmalloc(TXPMBUFPN, (fn), (n)))
#define CALLOC(fn, df, n, sz)   (CLEAR_ERR(), (df)->mallocs++,  \
  (df)->mallocbytes += (EPI_HUGEUINT)((n)*(sz)),                    \
  TXcalloc(TXPMBUFPN, (fn), (n), (sz)))
#define REALLOC(fn, df, p, n)   (CLEAR_ERR(), (df)->mallocs++,  \
  (df)->mallocbytes += (EPI_HUGEUINT)(n), TXrealloc(TXPMBUFPN, (fn), (p), (n)))
#define FREE(df, p)	((df)->frees++, TXfree(p))
#define MEMCPY(df, d, s, n)	\
  ((df)->memcpys++, (df)->memcpybytes += (EPI_HUGEUINT)(n), memcpy((d), (s), (n)))
#define MEMMOVE(df, d, s, n)	\
 ((df)->memmoves++, (df)->memmovebytes += (EPI_HUGEUINT)(n), memmove((d),(s), (n)))
#define MEMSET(df, s, c, n)   \
  ((df)->memsets++, (df)->memsetbytes += (EPI_HUGEUINT)(n), memset((s), (c), (n)))

/* read_head() flags: */
enum {
  RH_NOBAIL	= (1 << 0),	/* don't print or bail on bad headers */
  RH_HDRONLY	= (1 << 1),	/* header only (don't change main buffer) */
  RH_FREEPTR	= (1 << 2),	/* read free-page next pointer too */
  RH_LITTLEDATA	= (1 << 3),	/* read just some block data */
  RH_NOSEEK	= (1 << 4),	/* don't seek; already there */
  RH_UPDATEAVG	= (1 << 5)	/* update running last-size values */
};


#define KDBF_ERR_BUFSZ  256

/* Try to reduce alloc'd buffers that exceed this size, to save mem: */
#define TX_KDBF_MAX_SAVE_BUF_LEN        (((size_t)1) << 19)

/* Largest read-ahead amount: reading ahead a lot may be a waste of
 * I/O and mem if the next row is small, and if it is large, two
 * read()s of a large row instead of one will not be noticeable:
 */
#define TX_KDBF_MAX_READ_AHEAD_LEN      (((size_t)1) << 16)

static char *kdbf_strerr ARGS((char *buf, size_t bufSz));
static char *
kdbf_strerr(buf, bufSz)
char    *buf;   /* (out) buffer to write to */
size_t  bufSz;  /* (in) size of `buf' */
/* Wrapper for TXstrerror()/strerror() that uses ErrGuess if errno == 0,
 * and reports Windows error too.  Writes error message to `buf'.
 * Returns `buf'.
 */
{
  TXERRTYPE     errNum;
#ifdef _WIN32
  int           errNo;
#endif /* _WIN32 */
  char          *d = buf, *e = buf + bufSz;

  errNum = TXgeterror();                        /* save in case it changes */
#ifdef _WIN32
  /* KNG 20070809 Report both native and errno for Windows; latter may be
   * ambiguous:
   */
  errNo = errno;
  if (d < e) d += htsnpf(d, e - d, "error %d/%d: ", (int)errNum, (int)errNo);
#else /* !_WIN32 */
  if (d < e) d += htsnpf(d, e - d, "error %d: ", (int)errNum);
#endif /* !_WIN32 */
  if (d < e) d += htsnpf(d, e - d, "%s", TXstrerror(errNum));
#ifdef _WIN32
  if (d < e) d += htsnpf(d, e - d, "/%s", strerror(errNo));
  if (errNum == 0 && errNo == 0)                /* no error detected */
#else /* !_WIN32 */
  if (errNum == 0)                              /* no error detected */
#endif /* !_WIN32 */
    {
      if (ErrGuess > KR_UNK && ErrGuess < KR_NUM_ERRS)
        {                                       /* also report ErrGuess */
          if (d < e ) d += htsnpf(d, e - d, " (%s)", ErrStr[ErrGuess]);
        }
    }
  return(buf);
}

#ifdef KDBF_HIGHIO
  not implemented;
#  define KDBF_ISBADFH(df)      ((df)->fh == FILEPN)
#  define KDBF_BADFH            FILEPN
#else /* !KDBF_HIGHIO */
#  define KDBF_ISBADFH(df)      ((df)->fh < 0)
#  define KDBF_BADFH            -1
static int kdbf_raw_open ARGS((KDBF *df, int flags, int mode));
static int
kdbf_raw_open(df, flags, mode)
KDBF    *df;
int     flags, mode;
/* Wrapper for open() (but slightly different) that attempts to protect
 * a stdin/out/err handle from being used as a KDBF handle, e.g. if our
 * parent forgot to provide valid std handles.  This may help prevent
 * error messages from inadvertently corrupting a KDBF file.  We do this
 * by tying any handle(s) we get in the std range to /dev/null.  KNG 990301
 * NOTE: All direct (file descriptor) open()s must go through this function.
 * Adds in TX_O_NOINHERIT (and works around it if unsupported).
 * Sets `df->fh', `df->fhcuroff'.
 * Returns 0 on error, 1 if ok.
 */
{
  static CONST char     fn[] = "kdbf_raw_open";
  int                   fd, nfd = -1;

  if (flags & O_APPEND)
    {
      /* O_APPEND thwarts our `df->fhcuroff' offset tracking: */
      txpmbuf_putmsg(df->pmbuf, MERR+UGE, fn, "O_APPEND flag not supported");
      goto err;
    }
  for (;;)
    {
      CLEAR_ERR();
      fd = TXrawOpen(TXPMBUF_SUPPRESS /* caller reports? */, __FUNCTION__,
                     "KDBF file", df->fn, TXrawOpenFlag_None, flags, mode);
      if (fd < 0 || fd > MAXSTDFILENO) break;           /* not a std handle */
      close(fd);
      /* preserve `fd' for check below */
      CLEAR_ERR();
      if (nfd == -1 &&
          (nfd = TXrawOpen(df->pmbuf, __FUNCTION__,
                           "stdio placeholder handle file", NULLFILE,
          /* No O_CLOEXEC/TX_O_NOINHERIT: we're making a stdio handle
           * that we want to be inherited (so any exec'd process has stdio):
           */
                           TXrawOpenFlag_Inheritable, O_RDWR, 0666)) == -1)
        {
        bad:
          fd = -1;                                      /* error */
          break;
        }
      CLEAR_ERR();
      if (nfd == fd)
        nfd = -1;
      /* No O_CLOEXEC/TX_O_NOINHERIT: we're making a stdio handle
       * that we want to be inherited (so any exec'd process has stdio):
       */
      else if (dup2(nfd, fd) == -1)
        goto bad;
    }
  if (nfd != -1) close(nfd);
  if (fd < 0) goto err;
  df->fh = fd;
  df->fhcuroff = (EPI_OFF_T)0;
  return(1);                                    /* ok */

err:
  df->fh = KDBF_BADFH;
  df->fhcuroff = (EPI_OFF_T)(-1);
  return(0);                                    /* error */
}
#endif /* !KDBF_HIGHIO */

static size_t kdbf_raw_read ARGS((KDBF *df, void *buf, size_t sz,
                                  size_t minsz));
static size_t
kdbf_raw_read(df, buf, sz, minsz)
KDBF	*df;
void	*buf;
size_t	sz;     /* (in) size to attempt to read */
size_t  minsz;  /* (in, opt.) min size: if != -1, ok to stop read after this*/
/* Wrapper for read() that re-reads if unsuccessful, to try to
 * force an errno error (since a partially successful read() doesn't).
 * Also returns size_t instead of ssize_t like read().
 * NOTE: All direct (file descriptor) reads must go through this function.
 * Returns number of bytes read, or 0 if error.
 */
{
  size_t        gotTotal, gotThisPass, tryThisPass;
  int           tries;

  if (minsz == (size_t)(-1)) minsz = sz;
  CLEAR_ERR();
  for (gotTotal = (size_t)0; gotTotal < minsz; gotTotal += gotThisPass)
    {
      tryThisPass = sz - gotTotal;              /* try to read this much */
      /* read() takes a size_t parameter but returns ssize_t: if request
       * is over SSIZE_MAX results are "undefined" says man 2 read():
       */
#ifdef SSIZE_MAX
      if (tryThisPass > (size_t)SSIZE_MAX) tryThisPass = (size_t)SSIZE_MAX;
#endif /* SSIZE_MAX */
#ifdef KDBF_HIGHIO
      df->reads++;
      gotThisPass = (size_t)fread((char *)buf + gotTotal, 1, tryThisPass,
                                  df->fh);
      if (ferror(df->fh)) break;                /* error */
#else /* !KDBF_HIGHIO */
      tries = 0;
      do
        {
          df->reads++;
          CLEAR_ERR();
          gotThisPass = (size_t)read(df->fh, (char *)buf + gotTotal,
                                     tryThisPass);
        }
      while (gotThisPass == (size_t)(-1) &&     /* error */
             TXgeterror() == TXERR_EINTR &&     /* interrupted: retry-able */
             ++tries < 25);                     /* avoid infinite loop */
      if (gotThisPass == (size_t)(-1)) break;   /* error */
#endif /* !KDBF_HIGHIO */
      if (gotThisPass == (size_t)0) break;      /* no forward progress */
      if (df->fhcuroff >= (EPI_OFF_T)0)         /* if offset pointer valid */
        df->fhcuroff += (EPI_OFF_T)gotThisPass; /*   then update it */
      df->readbytes += (EPI_HUGEUINT)gotThisPass;   /* update stats */
    }
  if (gotTotal < minsz && TXgeterror() == 0) ErrGuess = KR_READ_PAST_EOF;
  return(gotTotal);
}

static size_t kdbf_raw_write ARGS((KDBF *df, void *buf, size_t sz));
static size_t
kdbf_raw_write(df, buf, sz)
KDBF	*df;
void	*buf;
size_t	sz;
/* Wrapper for write() that re-writes if unsuccessful, to try to
 * force an errno error.  Also returns size_t instead of ssize_t.
 * Returns number of bytes written, or 0 if error.
 * NOTE: All direct (file descriptor) writes must go through this function.
 */
{
  size_t                wroteTotal, wroteThisPass, tryThisPass;
  int                   tries;
#ifdef _WIN32
  static CONST char     fn[] = "kdbf_raw_write";
  double                now, writeRetryDeadline = -EPI_OS_DOUBLE_MAX;
  double                sleepPeriod = (double)0.333, sleepTime;
  char                  errBuf[KDBF_ERR_BUFSZ];
#endif /* _WIN32 */

  for (wroteTotal = (size_t)0; wroteTotal < sz; wroteTotal += wroteThisPass)
    {
      tryThisPass = sz - wroteTotal;            /* try to write this much */
      /* write() takes a size_t parameter but returns ssize_t: if request
       * is over SSIZE_MAX results are "undefined" says man 2 write():
       */
#ifdef SSIZE_MAX
      if (tryThisPass > (size_t)SSIZE_MAX) tryThisPass = (size_t)SSIZE_MAX;
#endif /* SSIZE_MAX */
      /* This should fail _and_ set errno on 2nd attempt: */
#ifdef KDBF_HIGHIO
      df->writes++;
      CLEAR_ERR();
      wroteThisPass = (size_t)fwrite((char *)buf + wroteTotal, 1, tryThisPass,
                                     df->fh);
      if (ferror(df->fh)) break;                /* error */
#else /* !KDBF_HIGHIO */
      tries = 0;
      do
        {
#  ifdef _WIN32
        tryAgain:
#  endif /* _WIN32 */
          df->writes++;
          CLEAR_ERR();
          wroteThisPass = (size_t)write(df->fh, (char *)buf + wroteTotal,
                                        tryThisPass);
#  ifdef _WIN32
          /* Pause up to [Texis] Write Timeout on errors considered temp: */
          if (wroteThisPass == (size_t)(-1) &&  /* error */
              TXwriteTimeout > (double)0.0)     /* retry temp write errors */
            switch (TXgeterror())
              {
              case ERROR_NO_SYSTEM_RESOURCES:   /* temporary error? */
                SAVE_ERR();
                now = TXgettimeofday();
                if (writeRetryDeadline == -EPI_OS_DOUBLE_MAX)
                  {                             /* first failure */
                    writeRetryDeadline = now + TXwriteTimeout;
                    /* Only issue message once, the first time encountered: */
                    if (df->fhcuroff >= (EPI_OFF_T)0) /* offset ptr valid */
                      txpmbuf_putmsg(df->pmbuf, MWARN + FWE, fn,
          "Cannot write 0x%wx bytes at 0x%wx to KDBF file %s: %s; will retry",
                                     (EPI_HUGEUINT)tryThisPass,
                                     (EPI_HUGEUINT)df->fhcuroff,
                                     df->fn,
                                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
                    else
                      txpmbuf_putmsg(df->pmbuf, MWARN + FWE, fn,
                   "Cannot write 0x%wx bytes to KDBF file %s: %s; will retry",
                             (EPI_HUGEUINT)tryThisPass,
                             df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
                  }
                RESTORE_ERR();
                sleepPeriod *= (double)1.5;     /* slow down over time */
                sleepTime = sleepPeriod;
                if (now + sleepTime > writeRetryDeadline)
                  sleepTime = writeRetryDeadline - now;
                if (sleepTime > (double)0.0)    /* not past deadline yet */
                  {
                    tries++;
                    TXsleepmsec((long)(sleepTime*(double)1000.0), 1);
                    goto tryAgain;
                  }
              }
#  endif /* _WIN32 */
        }
      while (wroteThisPass == (size_t)(-1) &&   /* error */
             TXgeterror() == TXERR_EINTR &&     /* interrupted: re-tryable */
             ++tries < 25);                     /* avoid infinite loop */
      if (wroteThisPass == (size_t)(-1)) break; /* error */
#endif /* !KDBF_HIGHIO */
      if (wroteThisPass == (size_t)0) break;    /* no forward progress */
      if (df->fhcuroff >= (EPI_OFF_T)0)         /* if offset pointer valid */
        df->fhcuroff += (EPI_OFF_T)wroteThisPass;  /* then update it */
      df->writebytes += (EPI_HUGEUINT)wroteThisPass;/* update stats */
    }
  if (wroteTotal != sz && TXgeterror() == 0) ErrGuess = KR_NO_SPACE;
  return(wroteTotal);
}

static EPI_OFF_T kdbf_raw_lseek ARGS((KDBF *df, EPI_OFF_T offset,int whence));
static EPI_OFF_T
kdbf_raw_lseek(df, offset, whence)
KDBF	        *df;    /* (in/out) KDBF handle */
EPI_OFF_T       offset; /* (in) desired offset */
int             whence; /* (in) SEEK_SET/SEEK_CUR/SEEK_END */
/* Wrapper for lseek().  Tries to save I/O by skipping seeks that it knows
 * have no effect (i.e. seek to same known location).
 * NOTE: All direct (file descriptor) seeks must go through this function.
 * Returns new current file position (absolute), or -1 on error.
 */
{
  /* We update `df->fhcuroff' on all I/O so it is current.  It is valid
   * between/across user kdbf_... calls -- even through unlock/lock --
   * because no one else is mucking with our `df->fh' descriptor
   * (even if another process or handle is writing to the file).
   * NOTE: If that assumption changes (i.e. fork()ed copy of `df->fh'
   * where lseek on the dup'd handle affects original's current offset too)
   * this optimization must be disabled.   KNG 20070418
   */
  if (df->fhcuroff >= (EPI_OFF_T)0 &&           /* we know current offset */
      (TXkdbfOptimize & 0x1) &&                 /* this optimization ok */
      ((whence == SEEK_SET && offset == df->fhcuroff) ||
       (whence == SEEK_CUR && offset == (EPI_OFF_T)0)))
    df->skippedlseeks++;
  else
    {
      df->lseeks++;
      df->fhcuroff = EPI_LSEEK(df->fh, offset, whence);
    }
  return(df->fhcuroff);
}  

static int write_start_ptrs ARGS((KDBF *df));
static int
write_start_ptrs(df)
KDBF	*df;
/* Writes start pointers out to file.  Returns 0 on error, 1 if ok.
 */
{
  static CONST char     fn[] = "write_start_ptrs";
  EPI_OFF_T             at, dest;
  char                  errBuf[KDBF_ERR_BUFSZ];

  /* To save time we write both pointers out together; thus both must now
   * be valid:
   */
  if (df->flags & (KDF_READONLY|KDF_BADSTART))
    {
      if (df->flags & KDF_READONLY)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
      if (df->flags & KDF_BADSTART)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteCorrupt, df->fn);
      return(0);
    }
  else
    {
      if (df->start.btree < (EPI_OFF_T)0)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
                         "Attempt to write invalid internal free-tree start pointer (%wd) to KDBF file `%s'",
                         (EPI_HUGEINT)df->start.btree, df->fn);
          return(0);
        }
      if (df->start.free_pages < (EPI_OFF_T)0)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
                         "Attempt to write invalid internal free free-tree start pointer (%wd) to KDBF file `%s'",
                         (EPI_HUGEINT)df->start.free_pages, df->fn);
          return(0);
        }
    }
  CLEAR_ERR();
  /* If KDF_FLUSHPTRS set, use df->start_off, since pointers are missing: */
  if (df->flags & KDF_FLUSHPTRS)
    {
      if (df->start_off < (EPI_OFF_T)0)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
                         "Invalid block start offset (%wd) in attempt to write start pointers (0x%wx and 0x%wx) to KDBF file `%s'",
                         (EPI_HUGEINT)df->start_off,
                         (EPI_HUGEINT)df->start.btree,
                         (EPI_HUGEINT)df->start.free_pages, df->fn);
          return(0);
        }
      at = kdbf_raw_lseek(df, dest = df->start_off, SEEK_SET);
    }
  else
    at = kdbf_raw_lseek(df, dest = -(EPI_OFF_T)sizeof(KDBF_START), SEEK_END);
  if (at < (EPI_OFF_T)0L)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FSE, fn, "Cannot seek to 0x%wx to write start pointers (0x%wx and 0x%wx) to KDBF file %s: %s",
             (EPI_HUGEINT)dest, (EPI_HUGEINT)df->start.btree,
             (EPI_HUGEINT)df->start.free_pages, df->fn,
             kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      return(0);
    }
  CLEAR_ERR();
  if (kdbf_raw_write(df, &df->start, sizeof(KDBF_START)) !=
      (size_t)sizeof(KDBF_START))
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, "Cannot write start pointers (0x%wx and 0x%wx) at 0x%wx to KDBF file %s: %s",
             (EPI_HUGEINT)df->start.btree, (EPI_HUGEINT)df->start.free_pages,
             (EPI_HUGEINT)at, df->fn,
             kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      return(0);
    }
  df->start_off = at;
  df->flags &= ~KDF_FLUSHPTRS;
  return(1);
}

static int kdbf_trunc ARGS((KDBF *df, EPI_OFF_T sz));
static int
kdbf_trunc(df, sz)
KDBF            *df;
EPI_OFF_T       sz;
/* Truncates file to `sz'.  Returns 0 on error.  Internal use only.
 */
{
  static CONST char     fn[] = "kdbf_trunc";

  df->truncates++;
  df->outbuflastsent = df->outbuflastsave = (EPI_OFF_T)(-1L);
  /* truncation may affect current `df->fh' offset; invalidate: */
  df->fhcuroff = (EPI_OFF_T)(-1);
  CLEAR_ERR();
  if (!TXtruncateFile(TXPMBUFPN, df->fn, KDBF_FILENO(df), sz))
    {
      /* Re-issue a txpmbuf_putmsg() with `KDBF' in it, for log monitors: */
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Could not truncate KDBF file %s to 0x%wx bytes",
                     df->fn, (EPI_HUGEUINT)sz);
      return(0);
    }
  return(1);
}

static void kdbf_truncit ARGS((KDBF *df));
static void
kdbf_truncit(df)
KDBF    *df;
/* Internal function.
 */
{
  /* Write failed, but could have partially succeeded, leaving
   * file corrupt.  Truncate and fix up start pointers.
   */
  /* Use outbuflastsave instead of start_off, if it's valid and earlier;
   * we may not have actually been able to write through start_off (e.g.
   * multiple blocks in write buffer):
   */
  if (df->outbuflastsave > (EPI_OFF_T)0L &&
      df->outbuflastsave < df->start_off)
    df->start_off = df->outbuflastsave;
  kdbf_trunc(df, df->start_off + (EPI_OFF_T)sizeof(KDBF_START));
  df->flags |= KDF_FLUSHPTRS;                       /* use df->start_off */
  write_start_ptrs(df);                             /* even if trunc err */
  df->flags &= ~KDF_FLUSHPTRS;
  /* Caller already yapped, and will return error to its caller */
  df->lasterr = (errno ? errno : EIO);
}

static int TXkdbfAllocBuf ARGS((KDBF *df, size_t len, int preserve));
static int
TXkdbfAllocBuf(df, len, preserve)
KDBF	*df;
size_t	len;
int	preserve;
/* Makes sure df->blk is at least `len' bytes long.  If `preserve',
 * preserves (copies) df->blk_data in new block and `len' is counted from
 * df->blk_data.  Note that blk_data/blk_data_sz should not be counted
 * on across transactions, nor through B-tree ops; only immediately after
 * this function is called.  Returns 1 if ok, 0 if not.
 */
{
  static CONST char     fn[] = "TXkdbfAllocBuf";
  byte                  *buf, *blkAllocEnd;
  size_t                blkDataOffset;

  if (preserve && df->blk_data) {
    blkAllocEnd = (byte *)df->blk + df->blksz;
    /* Make sure pointer is within buffer (i.e. not read_head static buf): */
    if (!((byte *)df->blk_data >= (byte *)df->blk &&
          (byte *)df->blk_data <= blkAllocEnd &&
          (byte *)df->blk_data + df->blk_data_sz <= blkAllocEnd))
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
                 "Invalid internal memory buffer blk_data for KDBF file `%s'",
                       df->fn);
        return(0);
      }
    if ((byte *)df->blk_data + len <= blkAllocEnd && /* buffer large enough */
        df->blksz <= TX_KDBF_MAX_SAVE_BUF_LEN)  /* but not too large */
      return(1);
    blkDataOffset = (byte *)df->blk_data - (byte *)df->blk;
    /* Leave enough room at start for a header so write_block()
     * avoids a memcpy(), and align it to keep data aligned:
     */
    len += (size_t)KDBF_ALIGN_UP(KDBF_HMAXSIZE);
    /* Ensure it is enough to preserve `df->blk_data' (e.g. if resizing
     * buffer down below TX_KDBF_MAX_SAVE_BUF_LEN):
     */
    if (len < blkDataOffset + df->blk_data_sz)
      len = blkDataOffset + df->blk_data_sz;
    /* Round up a bit to maybe avoid some future reallocs: */
    if (len < TX_KDBF_MAX_SAVE_BUF_LEN)
      {
        len += (len >> 3);
        if (len > TX_KDBF_MAX_SAVE_BUF_LEN) len = TX_KDBF_MAX_SAVE_BUF_LEN;
      }
    buf = (byte *)REALLOC(fn, df, df->blk, len);
    if (!buf)
      {
        df->lasterr = ENOMEM;
#ifndef EPI_REALLOC_FAIL_SAFE
        df->blk = NULL;                         /* realloc() fail freed it */
        df->blksz = 0;
        df->blk_data = NULL;
        df->blk_data_sz = 0;
#endif /* !EPI_REALLOC_FAIL_SAFE */
        return(0);
      }
    /* Update `df->blk_data' to the (perhaps-moved) buffer: */
    df->blk_data = buf + blkDataOffset;
  } else {
    df->blk_data = NULL;		/* now invalid */
    df->blk_data_sz = 0;
    if (df->blksz >= len &&                     /* buffer large enough */
        /* Buffer is already large enough, but if it is very large
         * (>TX_KDBF_MAX_SAVE_BUF_LEN), e.g. from a previous large
         * read, save some mem and reduce it.  Might have to re-alloc
         * again later if we do another large read, but a large read()
         * will take much more time than the re-alloc anyway so the
         * relative cost is minimal, and in the meantime we might save
         * some mem (e.g. if indexing):
         */
        df->blksz <= TX_KDBF_MAX_SAVE_BUF_LEN)  /* but not too large */
      return(1);
    if (df->blk)
      {
        FREE(df, df->blk);
        df->blk = BYTEPN;
        df->blksz = 0;
      }
    /* Round up a bit to maybe avoid some future reallocs: */
    if (len < TX_KDBF_MAX_SAVE_BUF_LEN)
      {
        len += (len >> 3);
        if (len > TX_KDBF_MAX_SAVE_BUF_LEN) len = TX_KDBF_MAX_SAVE_BUF_LEN;
      }
    if ((buf = (byte *)MALLOC(fn, df, len)) == NULL) {	/* try to get more */
      df->lasterr = ENOMEM;
      return(0);
    }
  }
  df->blk = buf;
  df->blksz = len;
  return(1);
}

/************************************************************************/

static int checkbadstart ARGS((CONST char *fn, CONST char *name, KDBF *df,
                               EPI_OFF_T blkend, EPI_OFF_T eof));
static int
checkbadstart(fn, name, df, blkend, eof)
CONST char      *fn, *name;
KDBF            *df;
EPI_OFF_T       blkend, eof;
/* Checks block end offset `blkend' against KDBF file size `eof' to see
 * how bad start pointers are.  Called if these offsets don't line up ok.
 * Returns 2 if probable 32/64-bit issue and KDF_IGNBADPTRS ioctl set,
 * 1 if probable 32/64-bit issue and not (msg), 0 if other corruption (msg).
 * if otherwise corrupt.  Prints appropriat message.
 */
{
  int   ret = 0;
#if EPI_OFF_T_BITS == 64
  if (blkend + (EPI_OFF_T)(sizeof(KDBF_START)/2) == eof)
    {                                   /* 64-bit access to a 32-bit file */
      df->flags |= KDF_BADSTART;
      if (df->flags & KDF_IGNBADPTRS) return(2);
      ret = 1;
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, "Corrupt %sheader at 0x%wx in KDBF file %s: Probable 32-bit file; convert to 64-bit via addtable",
                     name, (EPI_HUGEUINT)blkend, df->fn);
      goto bad;
    }
#elif EPI_OFF_T_BITS == 32
  if (blkend + (EPI_OFF_T)(sizeof(KDBF_START)*2) == eof)
    {                                   /* 32-bit access to a 64-bit file */
      df->flags |= KDF_BADSTART;
      if (df->flags & KDF_IGNBADPTRS) return(2);
      ret = 1;
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, "Corrupt %sheader at 0x%wx in KDBF file %s: Probable 64-bit file; convert to 32-bit via addtable",
                     name, (EPI_HUGEUINT)blkend, df->fn);
      goto bad;
    }
#endif /* EPI_OFF_T_BITS == 32 */
  txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
                 "Corrupt %sheader at 0x%wx in KDBF file %s",
                 name, (EPI_HUGEUINT)blkend, df->fn);
bad:
  df->flags |= KDF_BADSTART;
  /* don't abort  KNG 970808 */
  df->lasterr = EIO;
  return(ret);
}

static int read_head ARGS((KDBF *, EPI_OFF_T, KDBF_TRANS *, int));
static int
read_head(df, at, trans, flags)
KDBF            *df;
EPI_OFF_T       at;
KDBF_TRANS      *trans;
int             flags;
/* Reads block head at `at', plus part of block data, into df->blk.  Sets
 * `trans', plus df->blk_data.  `flags' are RH_... constants.  Returns 0
 * on error, 1 if ok.  If RH_HDRONLY or RH_FREEPTR, uses separate buffer;
 * main buffer is not touched (but blk_data/blk_data_sz are set anyway
 * and should be cleared after call since they're not in main buffer).
 * Leaves file seeked to same (relative) position as df->blk_data_sz
 * is to buffer.  If RH_NOSEEK, assumes file is already seeked to `at'.
 */
{
  static CONST char     fn[] = "read_head";
  size_t	        numread = (size_t)(-1), numreq, minsz;
  size_t	        *lp, *le;
  EPI_HUGEUINT          sum;
  int                   hdsz;
  byte                  *buf;
  EPI_OFF_T             eof;

  /* Move to the location and read the header.  Read a big chunk in, since
   * we're reading anyway and might as well read (most of) the whole block
   * in too, to save the overhead of another read() later.  We also
   * try to align things such that the data start will be aligned:
   */
  if (flags & (RH_HDRONLY | RH_FREEPTR)) {
    /* Header only.  Also use a separate buffer to avoid changing the
     * main buffer, for compatibility with el funko don't-muck-get()-data
     * until-next-get() API "standard":  -KNG 950914
     */
    numreq = KDBF_MIN_RD_SZ;	/* read > KDBF_START bytes (DOS) -KNG 951109 */
    if (flags & RH_FREEPTR) numreq += sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM);
    minsz = numreq;
    buf = df->hdrbuf;
  } else {	/* use main buffer */
    if (flags & RH_LITTLEDATA) {
      numreq = minsz = 2*KDBF_MIN_RD_SZ;
    } else {
      minsz = (size_t)KDBF_MIN_RD_SZ
#ifdef KDBF_CHECK_NEXT
        + (size_t)KDBF_HMAXSIZE
#endif
#ifdef KDBF_CHECK_CHECKSUMS
        + sizeof(KDBF_CHKSUM)
#endif
        ;
      /* Use average of last KDBF_LS_NUM gets as read-ahead guess: */
      sum = (EPI_HUGEUINT)0;
      for (lp = df->last_sz, le = lp + KDBF_LS_NUM; lp < le; lp++)
	sum += (EPI_HUGEUINT)(*lp);  /* wtf rollover possible? */
      numreq = (size_t)(sum/(EPI_HUGEUINT)KDBF_LS_NUM) + minsz;
      /* Round up to 512 bytes, since kernel has to read by blocks anyway: */
      if (numreq <= 0)
        numreq = 0x200;
      else
        numreq = (numreq + 0x1FF) & ~0x1FF;
      /* Reduce to a reasonable size to maybe save mem and I/O: */
      if (numreq > TX_KDBF_MAX_READ_AHEAD_LEN)
        numreq = TX_KDBF_MAX_READ_AHEAD_LEN;
      /* But keep the minimum: */
      if (numreq < minsz) numreq = minsz;
    }
    if (!TXkdbfAllocBuf(df, numreq + KDBF_ALIGN_SIZE, 0))
      return(0);
    /* Read at a point that will (hopefully) leave block data starting on
     * an alignment boundary, so we don't have to memmove() it later:
     */
    buf = (byte *)KDBF_ALIGN_UP((byte *)df->blk + KDBF_PRE_ALIGN) -
      KDBF_PRE_ALIGN;
  }

  CLEAR_ERR();
  if ((!(flags & RH_NOSEEK) &&
       kdbf_raw_lseek(df, at, SEEK_SET) < (EPI_OFF_T)0L) ||
      (numread = kdbf_raw_read(df,buf,numreq,minsz)) == (size_t)(-1) ||
      /* See if we read less than start pointers' size, but skosh it if
       * we expect possible 32/64-bit issue:
       */
      ((df->flags & KDF_IGNBADPTRS) ?
#if EPI_OFF_T_BITS == 64
       numread <= sizeof(KDBF_START)/2          /* in case 32-bit file */
#elif EPI_OFF_T_BITS == 32
       numread <= sizeof(KDBF_START)*2          /* in case 64-bit file */
#else
       numread <= sizeof(KDBF_START)            /* unknown; assume same */
#endif
       : numread <= sizeof(KDBF_START))) {
    /* Probably read at or beyond EOF; check.  For a get(-1) at EOF -
     * start this is a benign error (i.e. hit EOF), and also for a
     * read/get on an empty file (for backwards compatibility with
     * fdbf).  Otherwise it's a severe error and we report corruption.
     * (Note that a direct get at EOF - start used to report
     * corruption whereas now it's just an error.)  We check this
     * before parsing header since there's a small chance that the
     * start pointers might look like a valid header.  -KNG 950925
     */
    SAVE_ERR();
    eof = kdbf_raw_lseek(df, (EPI_OFF_T)0, SEEK_END);
    if (at + (EPI_OFF_T)sizeof(KDBF_START) == eof) return(0);   /* EOF */
    if (flags & RH_NOBAIL) return(0);
    RESTORE_ERR();
    checkbadstart(fn, "block ", df, at, eof); /* check 32/64 issue */
    return(0);
  }

  hdsz = kdbf_proc_head(buf, numread, at, trans);
  /* Report corruption if error.  Note that header-truncated error
   * (hdsz == 0) is fatal since we should have read enough:  -KNG 950925
   */
  if (hdsz <= 0) {
    if (!(flags & RH_NOBAIL)) {
      eof = kdbf_raw_lseek(df, (EPI_OFF_T)0, SEEK_END);
      checkbadstart(fn, "block ", df, at, eof); /* check 32/64 issue */
    }
    return(0);
  }

  if (flags & RH_UPDATEAVG) {
    /* Update running average.  Note that free blocks and free-tree pages
     * may get added if we seek over a lot of them:  -KNG 950925
     */
    df->last_sz[df->ls_cur] = trans->used;
    df->ls_cur = (df->ls_cur + 1) % KDBF_LS_NUM;
  }

  /* Note that these may not point to main buffer: */
  df->blk_data = buf + hdsz;		/* note rest of block */
  df->blk_data_sz = numread - hdsz;

  return(1);
}

static int write_head ARGS((KDBF *df, KDBF_TRANS *trans));
static int
write_head(df, trans)
KDBF  		*df;
KDBF_TRANS	*trans;
/* Creates and writes header for `df'/`trans'.  Returns 0 on error.
 * Will not touch main buffer.
 */
{
  static CONST char     fn[] = "write_head";
  byte		        buf[KDBF_HMAXSIZE];
  size_t                sz;
  char                  errBuf[KDBF_ERR_BUFSZ];

  if (df->flags & (KDF_READONLY|KDF_BADSTART))
    {
      if (df->flags & KDF_READONLY)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
      if (df->flags & KDF_BADSTART)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteCorrupt, df->fn);
      return(0);                /* wtf: no abort, since no write? */
    }
  CLEAR_ERR();
  if ((sz = (size_t)kdbf_create_head(df, buf, trans)) == (size_t)(-1) ||
      kdbf_raw_lseek(df, trans->at, SEEK_SET) != trans->at ||
      kdbf_raw_write(df, buf, sz) != sz) {
    txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteBytes,
                   (EPI_HUGEUINT)sz, (EPI_HUGEUINT)trans->at, df->fn,
                   kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
    return(0);
  }
  return(1);
}

/************************************************************************/

static int
TXkdbfReadRestOfBlock(KDBF *df, const KDBF_TRANS *trans, int forAget)
/* Reads in the rest of data from a block, if needed.  Assumes position is
 * correct for the read, and immediately after a read_head().
 * Full block data is thus at df->blk_data.  (Note that this shouldn't
 * get called during B-tree ops, since it changes main buffer and thus
 * possibly a normal get()).  If `forAget' is nonzero, will try to
 * align payload data to start of `df->blk', so ancestor kdbf_aget()
 * can just prune off `df->blk' for its caller.
 */
{
  static const char     fn[] = "TXkdbfReadRestOfBlock";
  size_t	        reqlen, deslen, actlen, rd, n;
  EPI_OFF_T             off;
#ifdef KDBF_CHECK_NEXT
  int                   chknext = 0;
#endif /* KDBF_CHECK_NEXT */
  char                  errBuf[KDBF_ERR_BUFSZ];

  /* Reading free blocks (used == 0) is illegal: */
  if (trans->used == 0)
    return(0);

  /* We always require the in-use portion of the block: */
  reqlen = trans->used;
#ifdef KDBF_CHECK_CHECKSUMS
  /* Also need checksum at end of block: */
  if (df->in_btree) reqlen += sizeof(KDBF_CHKSUM);
#endif
#ifdef KDBF_CHECK_NEXT
  /* We _want_ to read in the entire block plus the next header
   * (but could be at EOF, so don't require it).
   * KNG 031210 but if this was originally a large block now reused
   * as a small block, trans->size may be much greater than reqlen,
   * causing a huge read just for the next header.  Don't bother:
   */
  if (trans->size - trans->used < (size_t)64)   /* no big deal */
    {
      deslen = trans->size + KDBF_HMAXSIZE;
      chknext = 1;
    }
  else
#endif /* !KDBF_CHECK_NEXT */
    deslen = reqlen;
  /* deslen >= reqlen always */

  if (!df->blk_data)                    /* must be set in read_head() */
    {
      txpmbuf_putmsg(df->pmbuf, MERR + MAE, fn,
                 "Invalid internal memory buffer blk_data for KDBF file `%s'",
                     df->fn);
      df->lasterr = -1;
      return(0);
    }

  /* Read rest of block if it's not in buffer: */
  actlen = df->blk_data_sz;		/* what we've read so far */
  if (deslen > actlen) {                        /* missing some desired data*/
    if (forAget)                                /* in kdbf_aget() */
      {
        /*   If we are in kdbf_aget(), we ultimately want to return an
         * alloced buffer to the user that they can own.  Normally we
         * just do a kdbf_get() and then dup that.  But that uses ~2x
         * the block size of memory; not ideal if block is large.
         *   We can instead let kdbf_aget() steal our alloced `df->blk'
         * instead of duping it -- if we can align the block payload
         * (`df->blk_data') to `df->blk'.  So do that here, after
         * we're done with the KDBF header, but before we alloc and
         * read in the rest of the data, so we don't have a large
         * memmove() to do:
         */
        /* First ensure `blk_data' is within `blk'; might be elsewhere?
         * Unlikely; `preserve' is nonzero in TXkdbfAllocBuf() call below:
         */
        if (df->blk_data &&
            df->blk_data != df->blk &&
            (byte *)df->blk_data >= (byte *)df->blk &&
            (byte *)df->blk_data + df->blk_data_sz <=
            (byte *)df->blk + df->blksz)
          {
            MEMMOVE(df, df->blk, df->blk_data, df->blk_data_sz);
            df->blk_data = df->blk;
          }
      }
    if (!TXkdbfAllocBuf(df, deslen, 1)) return(0);
    CLEAR_ERR();
    rd = kdbf_raw_read(df, (byte *)df->blk_data + df->blk_data_sz,
                       deslen - df->blk_data_sz, -1);
    actlen += rd;
    if (rd == (size_t)(-1) || actlen < reqlen) {/*didn't get required length*/
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
                     "Cannot read 0x%wx bytes at 0x%wx in KDBF file %s: %s",
                     (EPI_HUGEUINT)(reqlen - df->blk_data_sz),
                     /* wtf we do not have header byte size, so work
                      * back from `end'; (trans->at + headersz) ==
                      * (trans->end - trans->size):
                      */
                     (EPI_HUGEUINT)((trans->end - (EPI_OFF_T)trans->size) +
                                    (EPI_OFF_T)df->blk_data_sz),
                     df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      df->lasterr = (errno ? errno : -1);
      return(0);
    }
    /* KNG 20070503 If we are read-buffering, copy any remaining data
     * back to read buffer, to save an lseek() and re-read().
     * Since we got to this function only because `rdbuf' did *not*
     * have all the data, we know we can replace `rdbuf' data with our data,
     * which is likely to be needed next (if sequential DBF reads):
     * WTF merge `rdbuf' and `blk' buffers to avoid this (Bug 1772):
     * wtf also make dest a pre-aligned location to avoid alignment memmove?
     */
    n = actlen - reqlen;                        /* excess read */
    if (df->rdbufsz > 0 && n > 0 && (TXkdbfOptimize & 0x4))
      {
        if (n > df->rdbufsz) n = df->rdbufsz;   /* trim to `rdbuf' size */
        MEMCPY(df, df->rdbuf, (byte *)df->blk_data + reqlen, n);
        df->rdbufstart = df->rdbuf;
        df->rdbufused = n;
        /* wtf we do not have header byte size, so work back from `end': */
        df->rdbufoff = (trans->end - (EPI_OFF_T)trans->size) +
          (EPI_OFF_T)reqlen;
      }
  }
#ifdef KDBF_CHECK_CHECKSUMS
  if (df->in_btree) {
    KDBF_CHKSUM	chk;
    MEMCPY(df, &chk, (byte *)df->blk_data + trans->used, sizeof(KDBF_CHKSUM));
    if (kdbf_checksum_block(df->blk_data, trans->used) != chk.chksum ||
	trans->at != chk.offset) {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
	     "Corrupt free-tree page at 0x%wx in KDBF file %s (bad checksum)",
                     (EPI_HUGEUINT)(trans->at), df->fn);
      df->lasterr = EIO;
      return(0);
    }
  }
#endif
#ifdef KDBF_CHECK_NEXT
  if (chknext && actlen > trans->size) {
    /* We (probably) got the next header; check it for consistency: */
    KDBF_TRANS	foo;
    actlen -= trans->size;
    if (kdbf_proc_head((byte *)df->blk_data + trans->size, actlen, trans->end,
		       &foo) <= 0 &&
	/* If error, ignore if at EOF: */
	(off = kdbf_raw_lseek(df, (EPI_OFF_T)0, SEEK_END)) !=
        trans->end + (EPI_OFF_T)sizeof(KDBF_START))
      {
        return(checkbadstart(fn, "next ", df, trans->end, off) ? 1 : 0);
      }
  }
#endif
  return(1);
}

static EPI_OFF_T write_block ARGS((KDBF *df, KDBF_TRANS *trans, byte *buf,
                               EPI_OFF_T cur, KDBF_START **ptrs));
static EPI_OFF_T
write_block(df, trans, buf, cur, ptrs)
KDBF            *df;
KDBF_TRANS      *trans;
byte            *buf;
EPI_OFF_T       cur;
KDBF_START      **ptrs;
/* Puts the header and the data out at the specified trans->at, if buf
 * is NULL then it seeks forward as if a write was performed.  Tries
 * to do one write if possible, writing header and data together.
 * Computes and adds on a checksum if in a B-tree op (i.e. this is a
 * free-tree page); assumes that trans->size (but not trans->used)
 * includes this space already (see kdbf_alloc(), kdbf_put()).  Will not
 * touch main buffer at all.  `cur' is current file offset of df->fh,
 * -1 if unknown.  Assumes data starts at buf + df->[btree_]prebufsz,
 * and there is df->[btree_]postbufsz bytes slack space after.  If
 * `ptrs' is non-NULL, attempts to tack on `*ptrs' to block write,
 * using postbufsz slack space: if successful, `*ptrs' is set NULL.
 * Returns current offset in file (i.e. at end of trans->used (+
 * checksum maybe) data), or -1 on error (and caller should fix up
 * start pointers if needed).
 */
{
  static CONST char     fn[] = "write_block";
  size_t                hsz, wsz = 0, prebufsz, postbufsz, postleft, attWrSz;
  EPI_OFF_T             attWrOff;               /* attempted-write offset */
  byte                  *chunk, *bp;
#ifdef KDBF_CHECKSUM_FREETREE
  KDBF_CHKSUM           chk;
  int                   didchk;
#endif /* KDBF_CHECKSUM_FREETREE */
  char                  errBuf[KDBF_ERR_BUFSZ];

  if (df->in_btree)
    {
      prebufsz = df->btree_prebufsz;
      postbufsz = df->btree_postbufsz;
    }
  else
    {
      prebufsz = df->prebufsz;
      postbufsz = df->postbufsz;
    }

#ifdef KDBF_CHECKSUM_FREETREE
  if (df->in_btree)
    {                                   /* make sure room for checksum */
      if (!(trans->used + sizeof(KDBF_CHKSUM) <= trans->size))
        {
          txpmbuf_putmsg(df->pmbuf, MERR + MAE, fn, "Invalid used/size block values (%wd/%wd) for free-tree block at 0x%wx in KDBF file `%s'",
                         (EPI_HUGEINT)trans->used, (EPI_HUGEINT)trans->size,
                         (EPI_HUGEINT)trans->at, df->fn);
          return((EPI_OFF_T)(-1));
        }
      MEMSET(df, &chk, 0, sizeof(KDBF_CHKSUM)); /* in case struct padding */
      chk.offset = trans->at;
      chk.chksum = kdbf_checksum_block(buf + prebufsz, trans->used);
    }
#endif /* KDBF_CHECKSUM_FREETREE */

  /* Seek to the right location, if not already there: */
  CLEAR_ERR();
  if (cur != trans->at &&               /* KNG 970805 only seek if needed */
      kdbf_raw_lseek(df, trans->at, SEEK_SET) != trans->at)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FSE, fn,
                     "Cannot seek to 0x%wx in KDBF file %s: %s",
                     (EPI_HUGEUINT)(trans->at), df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      return((EPI_OFF_T)(-1L));
    }
  attWrOff = trans->at;

  /* Write the block.  First see if we can prefix the header to the
   * given buffer and do 1 write:
   */
  buf += prebufsz;
  hsz = kdbf_header_size(trans->type);
  if (hsz <= prebufsz)                          /* header will fit w/buf */
    {
      postleft = postbufsz;
      chunk = bp = buf - hsz;                   /* chunk = start of write() */
      if (kdbf_create_head(df, bp, trans) < 0) return((EPI_OFF_T)(-1));
      wsz = hsz + trans->used;
      bp += wsz;
#ifdef KDBF_CHECKSUM_FREETREE
      didchk = 0;
      /* See if we can also fit checksum in postbuf: */
      if (df->in_btree)
        {
          if (postleft >= sizeof(KDBF_CHKSUM))  /* checksum fits: add it */
            {
              MEMCPY(df, bp, &chk, sizeof(KDBF_CHKSUM));
              bp += sizeof(KDBF_CHKSUM);
              wsz += sizeof(KDBF_CHKSUM);
              postleft -= sizeof(KDBF_CHKSUM);
              didchk = 1;
            }
          else                                  /* checksum does not fit */
            /* Checksum does not fit in buffer, but must still be
             * written immediately after `trans->used' on the disk or
             * it will be out of order.  So do not tack on anything more
             * (i.e. start pointers) to the buffer:
             */
            postleft = 0;
        }
#endif /* KDBF_CHECKSUM_FREETREE */
      /* And see if we can tack on the start pointers, if needed: */
      if (ptrs != KDBF_STARTPPN && *ptrs != KDBF_STARTPN &&
          postleft >= sizeof(KDBF_START) &&
          wsz == hsz + trans->size)
        {
          MEMCPY(df, bp, *ptrs, sizeof(KDBF_START));
          bp += sizeof(KDBF_START);
          wsz += sizeof(KDBF_START);
          postleft -= sizeof(KDBF_START);
          *ptrs = KDBF_STARTPN;                 /* tell caller we wrote 'em */
        }
      if (kdbf_raw_write(df, chunk, attWrSz = wsz) != wsz) goto write_err;
      attWrOff += (EPI_OFF_T)wsz;               /* current offset for msgs */
#ifdef KDBF_CHECKSUM_FREETREE
      if (df->in_btree && !didchk)              /* write checksum */
        {
          if (kdbf_raw_write(df, &chk, attWrSz = sizeof(KDBF_CHKSUM)) !=
              (size_t)sizeof(KDBF_CHKSUM))
            goto write_err;
          wsz += sizeof(KDBF_CHKSUM);
          attWrOff += sizeof(KDBF_CHKSUM);
        }
#endif /* KDBF_CHECKSUM_FREETREE */
      return(trans->at + (EPI_OFF_T)wsz);
    }

  /* Cannot prefix header.  If the data can fit, copy it into `tmpbuf'
   * and do just 1 write:
   */
  chunk = bp = df->tmpbuf;
  hsz = kdbf_create_head(df, bp, trans);
  if (hsz == (size_t)(-1)) return((EPI_OFF_T)(-1));
  bp += hsz;
  wsz = hsz + trans->used;
#ifdef KDBF_CHECKSUM_FREETREE
  if (df->in_btree) wsz += sizeof(KDBF_CHKSUM);
#endif /* KDBF_CHECKSUM_FREETREE */
  if (wsz <= sizeof(df->tmpbuf))                /* it'll fit */
    {
      MEMCPY(df, bp, buf, trans->used);         /* copy data */
      bp += trans->used;
#ifdef KDBF_CHECKSUM_FREETREE
      if (df->in_btree)                         /* copy checksum */
        MEMCPY(df, bp, &chk, sizeof(KDBF_CHKSUM));
#endif /* KDBF_CHECKSUM_FREETREE */
      if (kdbf_raw_write(df, chunk, attWrSz = wsz) != wsz) goto write_err;
      attWrOff += wsz;
      return(trans->at + (EPI_OFF_T)wsz);
    }
  else                                          /* will not fit; 2/3 writes */
    {
      if (kdbf_raw_write(df, chunk, attWrSz = hsz) != hsz) goto write_err;
      attWrOff += (EPI_OFF_T)hsz;
      if (kdbf_raw_write(df, buf, attWrSz = trans->used) != trans->used)
        {
        write_err:
          txpmbuf_putmsg(df->pmbuf, MERR+FWE, fn, CantWriteBytes,
                         (EPI_HUGEUINT)((EPI_OFF_T)attWrSz),
                         (EPI_HUGEUINT)attWrOff, df->fn,
                         kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          return((EPI_OFF_T)(-1L));
        }
      attWrOff += (EPI_OFF_T)trans->used;
      wsz = hsz + trans->used;
#ifdef KDBF_CHECKSUM_FREETREE
      if (df->in_btree)                         /* write checksum */
        {
          if (kdbf_raw_write(df, &chk, attWrSz = sizeof(KDBF_CHKSUM)) !=
              (size_t)sizeof(KDBF_CHKSUM))
            goto write_err;
          attWrOff += (EPI_OFF_T)sizeof(KDBF_CHKSUM);
          wsz += sizeof(KDBF_CHKSUM);
        }
#endif /* KDBF_CHECKSUM_FREETREE */
      return(trans->at + (EPI_OFF_T)wsz);
    }
}

#ifdef KDBF_PEDANTIC
static char	*TravMsg = CHARPN;
static HUGEINT  TravCnt = 0, TravTotal = 0, TravPrevTotal = 0;
static KDBF	*TravDf = NULL;

static void traverse_cb ARGS((EPI_OFF_T key, BTLOC loc, void *data));
static void
traverse_cb(key, loc, data)
EPI_OFF_T       key;
BTLOC           loc;
void            *data;
{
  static CONST char     fn[] = "traverse_cb";

  TravTotal++;
  if (TXgetoff2(&loc) == *((EPI_OFF_T *)data)) {
    TravCnt++;
    if (TravMsg != CHARPN)
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "%s free tree for KDBF file %s contains 0x%wx",
                     TravMsg, TravDf->fn, (EPI_HUGEUINT)(TXgetoff2(&loc)));
  }
}
#endif

int
kdbf_getoptimize()
{
  return(TXkdbfOptimize);
}

int
kdbf_setoptimize(flags, set)
int     flags;  /* (in) bit flags to set or clear */
int     set;    /* (in) 2: set all defaults  1: set flags  0:  clear flags */
/* Returns 0 on error.
 */
{
  switch (set)
    {
    case 2:                                     /* set defaults */
      TXkdbfOptimize = KDBF_OPTIMIZE_DEFAULT;
      break;
    case 1:                                     /* set flags */
      if (flags & ~KDBF_OPTIMIZE_ALL) return(0);/* illegal flag */
      TXkdbfOptimize |= flags;
      break;
    case 0:                                     /* clear flags */
      if (flags & ~KDBF_OPTIMIZE_ALL) return(0);/* illegal flag */
      TXkdbfOptimize &= ~flags;
      break;
    default:                                    /* illegal action */
      return(0);
    }
  return(1);                                    /* success */
}

void
kdbf_zap_start(df)
KDBF    *df;
/* Internal function.
 */
{
  df->start.btree = df->start.free_pages = df->start_off = (EPI_OFF_T)(-1L);
}

int
kdbf_read_start(df)
KDBF	*df;
/* Reads and sets start pointers for `df'.  Returns 0 on error, 1 if ok.
 * Internal function.
 */
{
  static CONST char     fn[] = "kdbf_read_start";
  EPI_OFF_T		at;
  KDBF_START	        start;
  char                  errBuf[KDBF_ERR_BUFSZ];

  /* WTF TRACE */

  /* If KDF_FLUSHPTRS is set, start pointers are known to be missing: */
  if (df->flags & KDF_FLUSHPTRS)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, "Internal error: Attempt to read start pointers when missing in KDBF file `%s'",
                     df->fn);
      return(0);
    }
  CLEAR_ERR();
  if ((at = kdbf_raw_lseek(df, -(EPI_OFF_T)sizeof(KDBF_START), SEEK_END)) <
      (EPI_OFF_T)0L || kdbf_raw_read(df, &start,sizeof(KDBF_START),-1) !=
      (size_t)sizeof(KDBF_START)) {
    txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CantReadPtrs, df->fn,
                   kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
    return(0);
  }
  /* Sanity check: */
  if (start.btree < (EPI_OFF_T)0L || start.btree > at ||
      start.free_pages < (EPI_OFF_T)0L || start.free_pages > at) {
    txpmbuf_putmsg(df->pmbuf, MERR, fn,
                   "Corrupt start pointers in KDBF file %s", df->fn);
    return(0);
  }
  df->start = start;
  df->start_off = at;
  return(1);
}

static int write_outbuf ARGS((KDBF *df));
static int
write_outbuf(df)
KDBF    *df;
/* Flushes output data buffer in KDF_APPENDONLY | KDF_NOREADERS mode.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "write_outbuf";
  int                   ret;
  char                  errBuf[KDBF_ERR_BUFSZ];

  if (df->outbufused == 0) goto ok;             /* nothing to write */
  CLEAR_ERR();
  if (kdbf_raw_lseek(df, df->outbufoff, SEEK_SET) != df->outbufoff ||
      kdbf_raw_write(df, df->outbuf, df->outbufused) != df->outbufused)
    {
      txpmbuf_putmsg(df->pmbuf, MERR+FWE,fn, CantWriteBytes,
                     (EPI_HUGEUINT)(df->outbufused),
                     (EPI_HUGEUINT)(df->outbufoff), df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      ret = 0;                                  /* note: caller must repair */
    }
  else                                          /* successful write */
    {
      /* Remember the last atomic KDBF block boundary that was written,
       * so we have a more reliable truncation point if disk space exceeded:
       */
      if (df->outbuflastsent >= df->outbufoff &&
          df->outbuflastsent <= (df->outbufoff + (EPI_OFF_T)df->outbufused) -
            (EPI_OFF_T)sizeof(KDBF_START) &&
          df->outbuflastsent > df->outbuflastsave)
        df->outbuflastsave = df->start_off;
    ok:
      ret = 1;
    }
  df->outbufused = 0;
  df->outbufoff = (EPI_OFF_T)(-2L);             /* invalid, and != -1 */
  return(ret);
}

int
kdbf_flush(df)
KDBF    *df;
/* Flushes any unwritten data to file.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "kdbf_flush";
  int                   ret = 1;
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ") starting", TXTK_INITARGS(df));
  TXTK_TRACE_BEFORE_FINISH()

  df->lasterr = 0;
  CLEAR_ERR();
  if (df->flags & KDF_INALLOC)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
                     "Internal error: incomplete block write to KDBF file %s",
                     df->fn);
      ret = 0;
    }
  if (!write_outbuf(df))
    {
      df->lasterr = (errno ? errno : -1);
      ret = 0;                                  /* WTF fixup start ptrs */
    }
  CLEAR_ERR();
  if ((df->flags & KDF_FLUSHPTRS) &&
      !write_start_ptrs(df))
    {
      if (ret == 1 || df->lasterr == -1) df->lasterr = (errno ? errno : -1);
      ret = 0;
    }
  if (df->flags & KDF_OVERWRITE)
    {
      if (!kdbf_trunc(df, df->start_off + (EPI_OFF_T)sizeof(KDBF_START)))
        ret = 0;
    }
  TXTK_TRACE_AFTER_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT "): %1.3kf sec returned %d %s",
                   TXTK_INITARGS(df), TXTK_TRACE_TIME(), ret,
                   (ret ? "ok" : "ERROR"));
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

EPI_OFF_T
kdbf_alloc(df, buf, len)
KDBF	*df;
void	*buf;           /* PBR 05-31-93 added buf to avoid replicate writes*/
size_t	len;
/* Allocates a new block of `len' bytes, writes `buf' to it, and returns
 * the offset.  Will not modify main (`blk') buffer: can be called
 * after kdbf_get() and kdbf_get()-returned buffer will remain intact.
 * Returns -1 on error.
 */
{
  static CONST char     fn[] = "kdbf_alloc";
  EPI_OFF_T		cur, next, key, eval;
  size_t	size, orgsize;
  size_t	key_size, hsz;
  KDBF_TRANS	trans;
  BTREE		*bt;
  BTLOC		loc;
  KDBF_CHKSUM	chk;
  KDBF_START    *sp, *wsp;
  int           rdstart, zap = 0;
  byte          *d;
  size_t        prebufsz;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;
  prebufsz = (df->in_btree ? df->btree_prebufsz : df->prebufsz);

  TXTK_TRACE_BEFORE_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE))
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                     TXTK_INITFMT ", %wd bytes) starting", TXTK_INITARGS(df),
                     (EPI_HUGEINT)len);
    if ((TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE_DATA)) && len > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE,
                    CHARPN, (byte *)buf + prebufsz, len, 1);
  TXTK_TRACE_BEFORE_FINISH()

  df->lasterr = 0;
  df->kwrites++;
  df->kwritebytes += (EPI_HUGEUINT)len;

  if (len <= 0 || len > KDBF_MAX_ALLOC) /* out of bounds */
    {
      BADBLKSIZE(df, len);
      df->lasterr = EINVAL;
      goto err;
    }
  if (df->flags & (KDF_READONLY|KDF_BADSTART))
    {
      if (df->flags & KDF_READONLY)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
          df->lasterr = EPERM;
        }
      if (df->flags & KDF_BADSTART)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteCorrupt, df->fn);
          df->lasterr = EIO;
        }
      goto err;
    }

  orgsize = len;
  /* overallocate the block for growth: */
  if (df->overalloc && !df->in_btree) {
    key_size = (len >> df->overalloc);
    if ((EPI_HUGEUINT)orgsize + (EPI_HUGEUINT)key_size < (EPI_HUGEUINT)KDBF_MAX_ALLOC)
      orgsize += key_size;
  }
#ifdef KDBF_CHECKSUM_FREETREE
  if (df->in_btree)
    orgsize += sizeof(KDBF_CHKSUM);	/* space for checksum */
#endif

  if (df->flags & (KDF_APPENDONLY | KDF_OVERWRITE))
    {
      if (df->in_btree)                 /* was checked in kdbf_free(), etc. */
        {
          txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, "Internal error: Function not legal in append-only or overwrite mode for KDBF file `%s'",
                         df->fn);
          df->lasterr = -1;
          goto err;
        }
      goto eofwrite;
    }

  /* If we're in the B-tree, check the free-page list first: */
  if (df->in_btree) {
    if (df->start.free_pages == (EPI_OFF_T)(-1L))   /* we read it earlier */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
               "Invalid free free-tree start pointer (%wd) in KDBF file `%s'",
                       (EPI_HUGEINT)df->start.free_pages, df->fn);
        df->lasterr = -1;
        goto err;
      }
    if (orgsize != KDBF_FREETREE_PGSZ)	/* all B-tree pages same */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
                       "Invalid free-tree block size (%wd) in KDBF file `%s'",
                       (EPI_HUGEINT)orgsize, df->fn);
        df->lasterr = -1;
        goto err;
      }
    if ((cur = df->start.free_pages) != (EPI_OFF_T)0L) {
      /* There's a free page in the list; pull it off and use.
       * Note use of RH_HDRONLY for separate buffer: don't muck with main
       * since get() data must be preserved for user:  -KNG 950914
       */
      if (!read_head(df, cur, &trans, (RH_HDRONLY | RH_FREEPTR)))
        {
          df->lasterr = (errno ? errno : -1);
          goto err;
        }
      if (trans.size != KDBF_FREETREE_PGSZ ||
	  trans.used != 0 ||
	  df->blk_data_sz < sizeof(EPI_OFF_T)) {
	txpmbuf_putmsg(df->pmbuf, MERR, fn,
        "Corrupt free free-tree page at 0x%wx in KDBF file %s (bad size/len)",
                       (EPI_HUGEUINT)cur, df->fn);
        df->lasterr = EIO;
        goto err;
      }
      MEMCPY(df, &next, df->blk_data, sizeof(next));
#ifdef KDBF_CHECK_CHECKSUMS
      MEMCPY(df, &chk, (byte *)df->blk_data + sizeof(EPI_OFF_T), sizeof(chk));
      if (df->blk_data_sz < sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM) ||
	  chk.offset != trans.at ||
	  chk.chksum != kdbf_checksum_block(df->blk_data, sizeof(EPI_OFF_T))) {
	txpmbuf_putmsg(df->pmbuf, MERR, fn,
        "Corrupt free free-tree page at 0x%wx in KDBF file %s (bad checksum)",
                       (EPI_HUGEUINT)cur, df->fn);
        df->lasterr = EIO;
        goto err;
      }
#endif
      /* Update the start pointers: */
      df->start.free_pages = next;
      CLEAR_ERR();
      if (!write_start_ptrs(df))
        {
          df->lasterr = (errno ? errno : -1);
          goto err;
        }
      /* Write out the header and data: */
      trans.used = len;
      trans.type |= KDBF_FREEBITS;	/* flag as part of tree */
      if (write_block(df, &trans, buf, (EPI_OFF_T)(-1L), KDBF_STARTPPN) <
          (EPI_OFF_T)0L)
        {
          df->lasterr = (errno ? errno : EIO);
          goto err;
        }
      df->last_at = trans.at;
      df->last_end = trans.end;
      df->lastBlkSz = trans.size;
      goto done;
    }
  } else {	/* not in B-tree */
    /* For normal (non B-tree) allocs, search the free tree first
     * for a best fit free block:
     */
    size = orgsize;
    TXsetrecid(&loc, (EPI_OFF_T)(-1L));
    /* KNG 970806 manually read start pointers, to save B-tree open if 0
     * and to cache them:
     */
    CLEAR_ERR();
    if (!kdbf_read_start(df))
      {
        df->lasterr = (errno ? errno : -1);
        goto err;
      }
    /* Set a flag to invalidate the pointers when done: another guy could
     * change them after this call, and the B-tree calls no longer
     * invalidate the pointers:      -KNG 970807
     */
    zap = 1;
    if (df->start.btree > (EPI_OFF_T)0L &&
        (bt = kdbf_openfbtree(df, KDBF_BTREE_FLAGS, df->start.btree)) != NULL){
      key = (EPI_OFF_T)size;
      loc = fbtsearch(bt, sizeof(key), &key, (BTLOC *)NULL);
      /* If no exact match, get next larger block.  Don't worry about
       * possibility of large size difference; just use it:
       */
      key_size = sizeof(key);
      if (TXgetoff2(&loc) < (EPI_OFF_T)0L)
        loc = fbtgetnext(bt, &key_size, &key, NULL);
      if (TXgetoff2(&loc) >= (EPI_OFF_T)0L) {	/* use it if found */
	if (key_size != sizeof(key))            /* we set it or fbtgetnext */
          {
            txpmbuf_putmsg(df->pmbuf, MERR, fn,
        "Invalid key size (%wd) from free-tree block found in KDBF file `%s'",
                           (EPI_HUGEINT)key_size, df->fn);
            df->lasterr = -1;
            goto err;
          }
	size = (size_t)key;			/* "" */
	/* We found a free block; remove it from free tree.
	 * This will likely recursively call kdbf_free, kdbf_get, etc.:
	 */
#ifdef KDBF_PEDANTIC
	/* block must be in tree exactly once: */
	TravMsg = CHARPN;
	TravDf = NULL;
	TravCnt = TravTotal = 0;
	cur = TXgetoff2(&loc);
	kdbf_traverse_tree(bt, bt->root, 0, traverse_cb, (void *)&cur);
	if (TravCnt != (EPI_HUGEINT)1) {
	  txpmbuf_putmsg(df->pmbuf, MERR, fn, "Free block 0x%wx occurs %wd times in tree before delete (must be 1) for KDBF file %s",
                         (EPI_HUGEUINT)TXgetoff2(&loc), (EPI_HUGEINT)TravCnt,
                         df->fn);
          df->lasterr = -1;
          goto err;
	}
	TravPrevTotal = TravTotal;
#endif
	key = (EPI_OFF_T)size;
	if (fbtdelete(bt, loc, sizeof(key), &key) <= 0) /* failed/not found */
          {
            txpmbuf_putmsg(df->pmbuf, MWARN, fn, "Could not delete block 0x%wx size 0x%wx from free-tree of KDBF file %s; allocating new block",
                           (EPI_HUGEUINT)TXgetoff2(&loc), (EPI_HUGEUINT)size,
                           df->fn);
            TXsetrecid(&loc, (EPI_OFF_T)(-1));
          }
        else if (TxKdbfVerify & 1)              /* verify free-tree deletes */
          {
            while (key = (EPI_OFF_T)size,
                   fbtdelete(bt, loc, sizeof(key), &key) > 0)
              {
                txpmbuf_putmsg(df->pmbuf, MWARN, fn,
                               "Block 0x%wx size 0x%wx multiply defined in free-tree of KDBF file %s; deleting",
                               (EPI_HUGEUINT)TXgetoff2(&loc),
                               (EPI_HUGEUINT)size, df->fn);
              }
          }
#ifdef KDBF_PEDANTIC
	TravMsg = "After delete";
	TravDf = df;
	TravCnt = TravTotal = 0;
	cur = TXgetoff2(&loc);
	kdbf_traverse_tree(bt, bt->root, 0, traverse_cb, (void *)&cur);
	if (TravCnt != 0)
          {
            df->lasterr = -1;
            kdbf_closefbtree(bt);
            goto err;
          }
	if (TravTotal != TravPrevTotal - (EPI_HUGEINT)1) {
	  txpmbuf_putmsg(df->pmbuf, MERR, fn,
 "After delete, free tree contains %wd items (expected %wd) for KDBF file %s",
                         (EPI_HUGEINT)TravTotal,
                         (EPI_HUGEINT)(TravPrevTotal - 1), df->fn);
          df->lasterr = -1;
          kdbf_closefbtree(bt);
          goto err;
	}
#endif
      }
      kdbf_closefbtree(bt);
    }
    /* if we found/used a free block, use it: */
    if (TXgetoff2(&loc) >= (EPI_OFF_T)0L) {
      if (TxKdbfVerify & 4)                     /* verify free blocks */
        {
          if (!read_head(df, TXgetoff2(&loc), &trans, RH_HDRONLY))
            txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
       "Cannot verify free block 0x%wx in KDBF file %s; allocating new block",
                           (EPI_HUGEUINT)TXgetoff2(&loc), df->fn);
          else if (trans.type & KDBF_FREEBITS)
            txpmbuf_putmsg(df->pmbuf, MERR, fn, 
   "Free block 0x%wx is free-tree page in KDBF file %s; allocating new block",
                           (EPI_HUGEUINT)TXgetoff2(&loc), df->fn);
          else if (trans.size < size)
            txpmbuf_putmsg(df->pmbuf, MERR, fn, "Free block 0x%wx (size 0x%wx) is too small (expected 0x%wx) in KDBF file %s; allocating new block",
                           (EPI_HUGEUINT)TXgetoff2(&loc),
                           (EPI_HUGEUINT)trans.size, (EPI_HUGEUINT)size,
                           df->fn);
          else
            goto usefree;
          goto eofwrite;
        }
    usefree:
      /* Fill in fields for block header and write the block.
       * Note that this assumes the correct header type/size
       * for the block we're overwriting:
       */
      trans.at = TXgetoff2(&loc);
      trans.type = kdbf_header_type(size);
      if (trans.type == (byte)(-1))
        {
          df->lasterr = -1;
          goto err;
        }
      trans.end = trans.at + (EPI_OFF_T)kdbf_header_size(trans.type) +
        (EPI_OFF_T)size;
      trans.used = len;
      trans.size = size;
      if (write_block(df, &trans, buf, (EPI_OFF_T)(-1L), KDBF_STARTPPN) <
          (EPI_OFF_T)0L)
        {
          df->lasterr = (errno ? errno : EIO);
          goto err;
        }
      df->last_at = TXgetoff2(&loc);
      df->last_end = trans.end;
      df->lastBlkSz = trans.size;
      trans.at = TXgetoff2(&loc);
      goto done;
    }
  }

  /* There's no free B-tree page, or there's no free block big enough;
   * make a new block at EOF.  At this point we've either read the start
   * pointers, or we're in append-only mode and may be caching them:
   */
eofwrite:
  trans.type = kdbf_header_type(orgsize);
  if (trans.type == (byte)(-1))
    {
      df->lasterr = -1;
      goto err;
    }
  if (df->in_btree) trans.type |= KDBF_FREEBITS; /* flag as free-tree page */
  trans.used = len;
  trans.size = orgsize;
  rdstart = 0;
  if (df->flags & KDF_APPENDONLY)
    {
      /* We're the only writer, so we can cache the start pointers: */
      if (df->start_off == (EPI_OFF_T)(-1L))            /* don't have ptrs */
        {
          CLEAR_ERR();
          if (!kdbf_read_start(df))
            {
              df->lasterr = (errno ? errno : -1);
              goto err;
            }
          rdstart = 1;
          cur = df->start_off + sizeof(KDBF_START);     /* df->fh at EOF */
        }
      else                                              /* read ptrs already */
        {
          cur = df->start_off;
          /* If no other readers, we don't write start pointers till close,
           * so if KDF_FLUSHPTRS is set, pointers have not been written:
           */
          if (!(df->flags & KDF_FLUSHPTRS)) cur += sizeof(KDBF_START);
        }
      trans.at = df->start_off;
    }
  else                                                  /* not append mode */
    {
      if (df->start.btree == (EPI_OFF_T)(-1L))  /* in B-tree or just read */
        {
          txpmbuf_putmsg(df->pmbuf, MERR, fn,
                    "Invalid free-tree start pointer (%wd) in KDBF file `%s'",
                         (EPI_HUGEINT)df->start.btree, df->fn);
          df->lasterr = -1;
          goto err;
        }
      trans.at = df->start_off;
      /* We don't know the current file offset.  This is ok, since
       * anywhere but append-only-and-no-readers mode we have to seek anyway.
       * We also don't know the value for rdstart.  This is ok since
       * we're not in append-only mode so it's not used:
       */
      cur = (EPI_OFF_T)(-1L);
    }
  /* We never allocate offset 0L to the B-tree (screws it internally).
   * (We can assume -- but verify -- this since free-tree pages are
   * always allocated at EOF and should never need to be allocated in
   * an empty file):
   */
  if (trans.at <= (EPI_OFF_T)0L && df->in_btree)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
              "Invalid offset 0x%wx allocated to free-tree in KDBF file `%s'",
                     (EPI_HUGEINT)trans.at, df->fn);
      df->lasterr = -1;
      goto err;
    }
  hsz = kdbf_header_size(trans.type);
  trans.end = trans.at + (EPI_OFF_T)hsz + (EPI_OFF_T)trans.size;
  if (trans.end + (EPI_OFF_T)sizeof(KDBF_START) < trans.at)
    {                                   /* EPI_OFF_T rollover */
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteBytes,
                     (EPI_HUGEUINT)((EPI_OFF_T)hsz + (EPI_OFF_T)trans.size),
                     (EPI_HUGEUINT)trans.at, df->fn, BitFileSizeExceeded);
      df->lasterr = EIO;
      goto err;
    }

  /* Write new block, possibly with start pointers: */
  if (df->flags & KDF_NOREADERS)
    wsp = sp = KDBF_STARTPN;            /* don't tack on start pointers */
  else
    wsp = sp = &df->start;
  if ((df->flags & (KDF_APPENDONLY | KDF_NOREADERS)) ==
      (KDF_APPENDONLY | KDF_NOREADERS)) /* use write buffer */
    {
    bufagain:
      if (df->outbufused == 0)          /* empty buffer: re-init */
        df->outbufoff = trans.at;
      if (trans.end <= df->outbufoff + (EPI_OFF_T)df->outbufsz)
        {                               /* block fits in buffer */
          d = df->outbuf + (size_t)(trans.at - df->outbufoff);
          if (kdbf_create_head(df, d, &trans) < 0) goto fixup; /* failed */
          d += hsz;
          MEMCPY(df, d, (byte *)buf + prebufsz, trans.used);
          if ((size_t)(trans.end - df->outbufoff) > df->outbufused)
            df->outbufused = (size_t)(trans.end - df->outbufoff);
          cur = trans.end;              /* where we would be if we'd written */
          df->start_off = trans.end;
          goto cacheptrs;
        }
      else if (df->outbufused > 0)      /* doesn't fit in buffer: flush */
        {
          if (!write_outbuf(df)) goto fixup;
          goto bufagain;                /* try buffer again */
        }
    }
  if ((cur = write_block(df, &trans, buf, cur, &wsp)) < (EPI_OFF_T)0L)
    {
    fixup:
      kdbf_truncit(df);
      goto err;
    }
  df->start_off = trans.end;            /* write_block() bumped start ptrs */

  /* Now skip to end and write start pointers, if in normal mode.
   * If we're in APPEND_ONLY and NOREADERS, we don't write the pointers
   * till close.  However, df->fh must be at the exact end of the data block,
   * and at EOF (these are assumed above):
   */
  if ((df->flags & (KDF_APPENDONLY | KDF_NOREADERS)) ==
      (KDF_APPENDONLY | KDF_NOREADERS) &&
      trans.end == cur)                 /* at end of data block */
    {
    cacheptrs:
      df->flags |= KDF_FLUSHPTRS;
      if (rdstart && cur < trans.at + (EPI_OFF_T)sizeof(KDBF_START) &&
          !(df->flags & KDF_OVERWRITE))
        {
          /* We're not at EOF, because we wrote a smaller block onto
           * the KDBF_START struct we read above.  Truncate the file:
           */
          if (!kdbf_trunc(df, df->start_off))
            {                           /* wtf return failure? */
              if (!write_start_ptrs(df)) goto bail1;
            }
          /* Seek EOF in case truncate call changed file position: */
          else if ((eval = kdbf_raw_lseek(df, 0, SEEK_END)) != df->start_off)
            {
              txpmbuf_putmsg(df->pmbuf, MERR + FSE, fn,
               "Seek EOF returned 0x%wx, expected 0x%wx for KDBF file %s: %s",
                             (EPI_HUGEUINT)eval, (EPI_HUGEUINT)df->start_off,
                             df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
              if (!write_start_ptrs(df))
                {
                bail1:
                  df->lasterr = (errno ? errno : -1);
                  goto err;
                }
            }
        }
    }
  else                                  /* not (APPEND_ONLY and NOREADERS) */
    {
      if (wsp == sp)                    /* if write_block() didn't write 'em */
        {
          CLEAR_ERR();
          if ((trans.end != cur &&      /* KNG 970805 only seek if needed */
               kdbf_raw_lseek(df, trans.end - cur, SEEK_CUR) != trans.end) ||
              kdbf_raw_write(df, (void *)&df->start, sizeof(KDBF_START)) !=
              (size_t)sizeof(KDBF_START))
            {
              txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWritePtrs,
                             (EPI_HUGEUINT)df->start.btree,
                             (EPI_HUGEUINT)df->start.free_pages, df->fn,
                             kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
              df->lasterr = (errno ? errno : -1);
              goto err;
            }
        }
      df->flags &= ~KDF_FLUSHPTRS;
    }
  df->last_at = trans.at;	/* shouldn't really update if in_btree ?, */
  df->last_end = trans.end;	/* but normal alloc will re-update it */
  df->lastBlkSz = trans.size;
  goto done;

err:
  trans.at = (EPI_OFF_T)(-1L);
done:
  if (zap) kdbf_zap_start(df);
  TXTK_TRACE_AFTER_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_WRITE)
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                     TXTK_INITFMT ", %wd bytes): %1.3lf sec returned offset "
                     TXTK_OFFFMT,
                     TXTK_INITARGS(df), (EPI_HUGEINT)len, TXTK_TRACE_TIME(),
                     TXTK_OFFARGS(trans.at));
    if ((TXtraceKdbf & TXTK_WRITE_DATA) && len > 0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER,
                    CHARPN, (byte *)buf + prebufsz, len, 1);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(trans.at);
}

#define HSZ     (1 + KDBFS_LEN)

EPI_OFF_T
kdbf_beginalloc(df)
KDBF	*df;
/* Allocates a new block like kdbf_alloc(), but doesn't write anything
 * yet; assumes more data for this block will be added via
 * kdbf_contalloc().  Only valid when APPENDONLY and NOREADERS set,
 * and a WRITEBUFSZ > ~64KB has been set.  0 or more calls to
 * kdbf_contalloc() must follow, then a call to kdbf_endalloc() must
 * follow.  Then kdbf_alloc() or kdbf_beginalloc() may be called.
 * NOTE: calls to any other functions may result in corruption.
 * Returns -1 on error, else new block offset.
 */
{
  static CONST char     fn[] = "kdbf_beginalloc";
  KDBF_TRANS            trans;
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ") starting", TXTK_INITARGS(df));
  TXTK_TRACE_BEFORE_FINISH()

  if ((df->flags & (KDF_APPENDONLY | KDF_NOREADERS | KDF_INALLOC)) !=
      (KDF_APPENDONLY | KDF_NOREADERS) ||
      df->outbufsz < (size_t)(KDBF_HMAXSIZE + KDBFWMAX + 1) ||
      df->in_btree)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
             "Internal error: improper ioctl setup for KDBF file %s", df->fn);
      goto err;
    }
  df->lasterr = 0;
  df->flags &= ~KDF_OBRDSTART;
  df->flags |= KDF_INALLOC;
  trans.type = KDBFSTYPE;                               /* largest for now */
  trans.used = trans.size = (size_t)0;                  /* so far */
  if (df->start_off == (EPI_OFF_T)(-1L))                /* don't have ptrs */
    {
      CLEAR_ERR();
      if (!kdbf_read_start(df))
        {
          df->lasterr = (errno ? errno : -1);
          goto err;
        }
      df->flags |= KDF_OBRDSTART;
    }
  trans.at = df->start_off;
  trans.end = (EPI_OFF_T)0L;                            /* not used yet */
  /* We need at least 64KB of buffer space for this block: the actual
   * header size and values will not be known until kdbf_endalloc(), so
   * it seems we cannot write anything until then.  But once we exceed
   * 64KB we know the header size (size_t), header, so we can start
   * writing to disk then, and just back up and update the header
   * values later:
   */
bufagain:
  if (df->outbufused == 0) df->outbufoff = trans.at;    /* re-init buffer */
  if (df->outbufused + (size_t)(HSZ + KDBFWMAX + 1) <= df->outbufsz)
    {
      /* Reserve space in buffer for header, fixed up at kdbf_endalloc()
       * (possibly after this dummy reservation has been flushed to file).
       * wtf could clear it for sanity, but costs some time:
       * KNG 20160928 do clear it, for Valgrind and sanity:
       */
      memset(df->outbuf + df->outbufused, 0, HSZ);
      df->outbufused += HSZ;                            /* wtf hdr sz drill */
    }
  else if (df->outbufused > 0)                          /* flush first */
    {
      if (!write_outbuf(df))
        {
          df->flags &= ~KDF_INALLOC;
          kdbf_truncit(df);                             /* wtf still corrupt*/
          goto err;
        }
      goto bufagain;
    }
  df->flags |= KDF_FLUSHPTRS;
  /* rest of checking we do at kdbf_endalloc() */
  goto done;

err:
  trans.at = (EPI_OFF_T)(-1L);
done:
  TXTK_TRACE_AFTER_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT "): %1.3kf sec returned offset " TXTK_OFFFMT,
                   TXTK_INITARGS(df), TXTK_TRACE_TIME(),
                   TXTK_OFFARGS(trans.at));
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(trans.at);
}

int
kdbf_contalloc(df, buf, sz)
KDBF    *df;
byte    *buf;
size_t  sz;
/* Adds data `buf' of `sz' byes to current block just started with
 * kdbf_beginalloc().  May be called repeatedly; call kdbf_endalloc()
 * when done with block.  Only usable if APPENDONLY and NOREADERS and
 * WRITEBUFSZ > ~64KB.  KDBF_IOCTL_PREBUFSZ does not apply.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "kdbf_contalloc";
  EPI_OFF_T             off;
  int                   ret;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE))
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                     TXTK_INITFMT ", %wd bytes) starting",
                     TXTK_INITARGS(df), (EPI_HUGEINT)sz);
    if ((TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE_DATA)) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE,
                    CHARPN, buf, sz, 1);
  TXTK_TRACE_BEFORE_FINISH()

#ifdef KDBF_SANITY
  if (!(df->flags & KDF_INALLOC))
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, NoAllocStarted, df->fn);
      goto err;
    }
#endif /* KDBF_SANITY */

bufagain:
  /* >>>>> NOTE: this corresponds to KDBF_CONTALLOC(): <<<<< */
  if (df->outbufused + sz <= df->outbufsz)              /* fits in buffer */
    {
      MEMCPY(df, df->outbuf + df->outbufused, buf, sz);
      df->outbufused += sz;
      goto ok;
    }

  if (df->outbufused > 0)                               /* flush buffer */
    {
      off = df->outbufoff + (EPI_OFF_T)df->outbufused;
      if (!write_outbuf(df)) goto truncit;
      df->outbufoff = off;
      goto bufagain;
    }
  CLEAR_ERR();
  if (kdbf_raw_lseek(df, df->outbufoff, SEEK_SET) != df->outbufoff ||
      kdbf_raw_write(df, buf, sz) != sz)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteBytes,
                     (EPI_HUGEUINT)sz, (EPI_HUGEUINT)df->outbufoff, df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
    truncit:
      df->flags &= ~KDF_INALLOC;
      kdbf_truncit(df);                                 /* wtf still corrupt*/
      goto err;
    }
  df->outbufoff += (EPI_OFF_T)sz;
ok:
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  TXTK_TRACE_AFTER_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_WRITE)
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                     TXTK_INITFMT ", %wd bytes): %1.3kf sec returned %d %s",
                   TXTK_INITARGS(df), (EPI_HUGEINT)sz, TXTK_TRACE_TIME(), ret,
                     (ret ? "ok" : "ERROR"));
    if ((TXtraceKdbf & TXTK_WRITE_DATA) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER,
                    CHARPN, buf, sz, 1);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

size_t
kdbf_undoalloc(df, bufp)
KDBF    *df;
byte    **bufp;
/* Undoes the most recent kdbf_beginalloc(), setting `*bufp' to the
 * buffer of kdbf_contalloc() data, and returning its size.  Usable
 * only after a kdbf_beginalloc() and 0 or more kdbf_contalloc()s.
 * NOTE: may fail with -1 if buffer was partially flushed already.
 * Returned buffer must be copied immediately.
 */
{
  static CONST char     fn[] = "kdbf_undoalloc";
  byte                  *d;
  size_t                sz;
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ") starting", TXTK_INITARGS(df));
  TXTK_TRACE_BEFORE_FINISH()

#ifdef KDBF_SANITY
  if (!(df->flags & KDF_INALLOC))
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, NoAllocStarted, df->fn);
      *bufp = BYTEPN;
      goto err;
    }
#endif /* KDBF_SANITY */
  if (df->start_off < df->outbufoff)
    {                                                   /* wtf silent? */
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Cannot undo alloc for KDBF file %s: Partially written",
                     df->fn);
      *bufp = BYTEPN;
      goto err;
    }
  d = df->outbuf + (size_t)(df->start_off - df->outbufoff);
  *bufp = d + HSZ;                                      /* wtf hdr sz drill */
  sz = (df->outbuf + df->outbufused) - *bufp;
  df->outbufused = d - df->outbuf;
  df->flags &= ~KDF_INALLOC;
  goto done;

err:
  sz = (size_t)(-1);
done:
  TXTK_TRACE_AFTER_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT "): %1.3kf sec returned %wu bytes",
                   TXTK_INITARGS(df), TXTK_TRACE_TIME(), (EPI_HUGEUINT)sz);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(sz);
}

EPI_OFF_T
kdbf_endalloc(df, szp)
KDBF    *df;
size_t  *szp;
/* Completes alloc started with kdbf_beginalloc().  Only usable if
 * APPENONLY and NOREADERS and WRITEBUFSZ > ~64KB.  Sets `*szp' to data size.
 * Returns offset of block (same as kdbf_beginalloc()), or -1 on error.
 */
{
  static CONST char     fn[] = "kdbf_endalloc";
  KDBF_TRANS            trans;
  EPI_OFF_T             osz, eval, off;
  size_t                sz, hsz;
  byte                  *d;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ") starting", TXTK_INITARGS(df));
  TXTK_TRACE_BEFORE_FINISH()

#ifdef KDBF_SANITY
  if (!(df->flags & KDF_INALLOC))
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, NoAllocStarted, df->fn);
      goto bail;
    }
#endif /* !KDBF_SANITY */
  osz = (df->outbufoff + (EPI_OFF_T)df->outbufused) -   /* block size */
    (df->start_off + (EPI_OFF_T)HSZ);                   /* wtf hdr sz drill */
  if (osz > (EPI_OFF_T)KDBF_MAX_ALLOC)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
                     "Will not finish block at offset 0x%wx size 0x%wx in KDBF file %s: Max size exceeded",
                     (EPI_HUGEUINT)df->start_off, (EPI_HUGEUINT)osz, df->fn);
      goto bail2;
    }
  sz = (size_t)osz;
  trans.type = kdbf_header_type(sz);
  if (trans.type == (byte)(-1))
    {
      df->lasterr = -1;
      goto bail2;
    }
  trans.at = df->outbuflastsent = df->start_off;
  trans.used = trans.size = sz;
  trans.end = (EPI_OFF_T)0L;                            /* not used yet */
  if (trans.at >= df->outbufoff)                        /* header in buffer */
    {
      d = df->outbuf + (size_t)(trans.at - df->outbufoff);
      hsz = kdbf_create_head(df, d, &trans);
      if (hsz == (size_t)(-1)) goto bail2;
      trans.end = trans.at + (EPI_OFF_T)hsz + (EPI_OFF_T)trans.size;
      if (trans.end + (EPI_OFF_T)sizeof(KDBF_START) < trans.at) goto rollover;
      if (hsz != (size_t)HSZ)                           /* wtf hdr sz drill */
        {                                               /* smaller header */
          MEMMOVE(df, d + hsz, d + (size_t)HSZ,
                  (df->outbuf + df->outbufused) - (d + HSZ));
          df->outbufused -= (size_t)HSZ - hsz;
        }
    }
  else                                                  /* hdr not in buf */
    {
      if (!write_outbuf(df)) goto bail2;
      hsz = kdbf_create_head(df, df->outbuf, &trans);   /* scratch space */
      if (hsz != (size_t)HSZ)                           /* what we reserved */
        {
          txpmbuf_putmsg(df->pmbuf, MERR, fn, "Invalid header size %wd while trying to complete block write to KDBF file `%s'",
                         (EPI_HUGEINT)hsz, df->fn);
          df->lasterr = -1;
          goto bail2;
        }
      trans.end = trans.at + (EPI_OFF_T)hsz + (EPI_OFF_T)trans.size;
      if (trans.end + (EPI_OFF_T)sizeof(KDBF_START) < trans.at)
        {                                               /* file sz rollover */
        rollover:
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteBytes,
                       (EPI_HUGEUINT)((EPI_OFF_T)hsz + (EPI_OFF_T)trans.size),
                         (EPI_HUGEUINT)trans.at, df->fn, BitFileSizeExceeded);
          df->lasterr = EIO;
        bail2:
          df->flags &= ~KDF_INALLOC;
          kdbf_truncit(df);                             /* wtf maybe corrupt*/
          goto bail;
        }
      CLEAR_ERR();
      if (kdbf_raw_lseek(df, trans.at, SEEK_SET) != trans.at ||
          kdbf_raw_write(df, df->outbuf, hsz) != hsz)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE,fn, CantWriteBytes,
                         (EPI_HUGEUINT)hsz,
                         (EPI_HUGEUINT)trans.at, df->fn,
                         kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          goto bail;
        }
    }
  off = trans.at;
  df->start_off = trans.end;                            /* next block */
  if ((df->flags & KDF_OBRDSTART) &&
      trans.end < trans.at + (EPI_OFF_T)sizeof(KDBF_START) &&
      !(df->flags & KDF_OVERWRITE))
    {                                                   /* blk sz < ptrs sz */
      if (!kdbf_trunc(df, df->start_off))
        {                                               /* wtf return err? */
          if (!write_start_ptrs(df)) goto bail1;
        }
      else if ((eval = kdbf_raw_lseek(df, 0, SEEK_END)) != df->start_off)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FSE, fn,
               "Seek EOF returned 0x%wx, expected 0x%wx for KDBF file %s: %s",
                         (EPI_HUGEUINT)eval, (EPI_HUGEUINT)df->start_off,
                         df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          if (!write_start_ptrs(df))
            {
            bail1:
              df->lasterr = (errno ? errno : -1);
            bail:
              *szp = (size_t)0;
              off = (EPI_OFF_T)(-1);
              goto done;
            }
        }
    }
  df->flags &= ~(KDF_OBRDSTART | KDF_INALLOC);
  df->last_at = trans.at;
  df->last_end = trans.end;
  df->lastBlkSz = trans.size;
  /* Count the write here, when finished, so that undoalloc doesn't count: */
  df->kwrites++;
  df->kwritebytes += (EPI_HUGEUINT)sz;
  *szp = sz;
done:
  TXTK_TRACE_AFTER_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT "): %1.3kf sec returned offset " TXTK_OFFFMT,
                   TXTK_INITARGS(df), TXTK_TRACE_TIME(),
                   TXTK_OFFARGS(off));
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(off);
}

#undef HSZ

static void bad_append ARGS((KDBF *df, EPI_OFF_T at, CONST char *fn));
static void
bad_append(df, at, fn)
KDBF            *df;
EPI_OFF_T       at;
CONST char      *fn;
{
  txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
   "Illegal operation attempted in append-only mode at 0x%wx in KDBF file %s",
                 (EPI_HUGEUINT)at, df->fn);
}

int
kdbf_free(df, at)
KDBF            *df;
EPI_OFF_T       at;
/* Frees block at `at'.  Will not change main buffer.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "kdbf_free";
  BTREE                 *bt;
  BTLOC                 loc;
  KDBF_TRANS            trans;
  int                   res, zap = 0, ret;
  KDBF_CHKSUM           chk;
  size_t                len;
  EPI_OFF_T             key, oat = at;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ", offset " TXTK_OFFFMT ") starting",
                   TXTK_INITARGS(df), TXTK_OFFARGS(oat));
  TXTK_TRACE_BEFORE_FINISH()

  df->kfrees++;

  if (df->flags & (KDF_APPENDONLY|KDF_READONLY|KDF_OVERWRITE|KDF_BADSTART))
    {
      if (df->flags & KDF_OVERWRITE)
        txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn, CantReadOverwrite, df->fn);
      if (df->flags & KDF_APPENDONLY)
        bad_append(df, at, fn);
      if (df->flags & KDF_READONLY)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
      if (df->flags & KDF_BADSTART)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteCorrupt, df->fn);
      goto err;
    }

  if (at == (EPI_OFF_T)(-1L)) {
    at = df->last_at;		/* PBR 06-09-93 free current block */
    if (df->in_btree)           /* shouldn't need to free current in B-tree */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
 "Invalid attempt to free current block while in free-tree in KDBF file `%s'",
                       df->fn);
        goto err;
      }
  }
  if (at < (EPI_OFF_T)0L) goto err; /* bogus location */

  /* Get block size.  Use RH_HDRONLY for separate buffer;
   * don't touch main get() buffer:  -KNG 950914
   */
  if (!read_head(df, at, &trans, RH_HDRONLY)) goto err;
  if (trans.used == 0) {
    txpmbuf_putmsg(df->pmbuf, MWARN + FWE, fn,
                   "Attempt to re-free free block at 0x%wx in KDBF file %s",
                   (EPI_HUGEUINT)at, df->fn);
    goto err;
  }

  if (df->in_btree) {
    /* We're in a B-tree op, so don't re-open the B-tree (this is caused
     * by something like: normal alloc -> fbtdelete -> kdbf_free).  Put the
     * page on the free-page list.  This also avoids the near-race condition
     * (if free pages went to the B-tree instead) of page deletion causing more
     * page allocs when added to the tree:  -KNG 950912
     */
    if (at == (EPI_OFF_T)0L)                    /* 0L is null */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Invalid attempt to free offset 0x%wx in KDBF file `%s'",
                       (EPI_HUGEINT)at, df->fn);
        goto err;
      }
    if (trans.size != KDBF_FREETREE_PGSZ) {
      /* Bogus page size; header corrupt: */
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                 "Corrupt free-tree page at 0x%wx in KDBF file %s (bad size)",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
    /* Mark block as free and add to free-pages list: */
    trans.used = 0;
    trans.type &= ~KDBF_FREEBITS;	/* no longer active free-tree page */
#ifdef KDBF_CHECKSUM_FREETREE
    len = sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM);
#else
    len = sizeof(EPI_OFF_T);
#endif
    if (len > sizeof(df->hdrbuf) ||
        df->start.free_pages == (EPI_OFF_T)(-1L))   /* must have been read */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn, "Invalid `len' or free free-tree start pointer not read yet while freeing block in KDBF file `%s'",
                       df->fn);
        goto err;
      }
    *((EPI_OFF_T *)df->hdrbuf) = df->start.free_pages;      /* link next */
#ifdef KDBF_CHECKSUM_FREETREE
    /* Add checksum of the next pointer: */
    MEMSET(df, &chk, 0, sizeof(KDBF_CHKSUM));   /* in case struct padding */
    chk.offset = trans.at;
    chk.chksum = kdbf_checksum_block(df->hdrbuf, sizeof(EPI_OFF_T));
    MEMCPY(df, df->hdrbuf + sizeof(EPI_OFF_T), &chk, sizeof(chk));
#endif
    if (!write_head(df, &trans)) goto err;
    if (kdbf_raw_write(df, df->hdrbuf, len) != len) {
      txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn,
                   "Cannot write 0x%wx bytes after 0x%wx to KDBF file %s: %s",
                     (EPI_HUGEUINT)len, (EPI_HUGEUINT)trans.at, df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      goto err;
    }
    /* Rewrite start pointer: */
    df->start.free_pages = trans.at;
    if (!write_start_ptrs(df)) goto err;
    goto done;
  }

  /* It's a normal block, so add it to the free tree: */
  res = 0;	/* keep lint happy */
  if (!kdbf_read_start(df)) goto err;
  zap = 1;              /* B-tree code no longer invalidates pointers 970807 */
  if ((bt = kdbf_openfbtree(df, KDBF_BTREE_FLAGS, df->start.btree)) != NULL) {
    if (TxKdbfVerify & 2)               /* verify free-tree inserts */
      {
        TXsetrecid(&loc, at);
        key = (EPI_OFF_T)trans.size;
        if (fbtsearch(bt, sizeof(key), &key, &loc).off >= (EPI_OFF_T)0L)
          {
            txpmbuf_putmsg(df->pmbuf, MERR, fn,
      "Free block 0x%wx (size 0x%wx) is already in free-tree in KDBF file %s",
                          (EPI_HUGEUINT)at, (EPI_HUGEUINT)trans.size, df->fn);
            res = 0;
            goto afterins;
          }
      }
#ifdef KDBF_PEDANTIC
    /* block must not be in B-tree: */
    TravMsg = "Before insert";
    TravDf = df;
    TravCnt = TravTotal = 0;
    kdbf_traverse_tree(bt, bt->root, 0, traverse_cb, (void *)&at);
    if (TravCnt != 0)
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
    "Free block 0x%wx (size 0x%wx) is already in free-tree in KDBF file `%s'",
                       (EPI_HUGEUINT)at, (EPI_HUGEUINT)trans.size, df->fn);
        res = 0;
        goto afterins;
      }
    TravPrevTotal = TravTotal;
#endif /* KDBF_PEDANTIC */
    TXsetrecid(&loc, at);
    key = (EPI_OFF_T)trans.size;
    res = fbtinsert(bt, loc, sizeof(key), &key);
  afterins:
#ifdef KDBF_PEDANTIC
    TravMsg = CHARPN;
    TravDf = NULL;
    TravCnt = TravTotal = 0;
    kdbf_traverse_tree(bt, bt->root, 0, traverse_cb, (void *)&at);
    if (TravCnt != (EPI_HUGEINT)1) {
      txpmbuf_putmsg(df->pmbuf, MERR, fn, "After insert, block 0x%wx occurs %wd times in free tree (must be 1) for KDBF file %s",
                     (EPI_HUGEUINT)at, (EPI_HUGEINT)TravCnt, df->fn);
      res = -1;
    }
    if (TravTotal != TravPrevTotal + (EPI_HUGEINT)1) {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
 "After insert, free tree contains %wd items (expected %wd) for KDBF file %s",
                     (EPI_HUGEINT)TravTotal, (EPI_HUGEINT)(TravPrevTotal+1),
                     df->fn);
      res = -1;
    }
#endif /* KDBF_PEDANTIC */
    kdbf_closefbtree(bt);
  }
  if (!bt || res < 0) goto err;         /* tree mucking failed */

  /* Now actually free the page: */
  trans.used = 0;
  if (!write_head(df, &trans)) goto err;
  goto done;

err:
  ret = 0;
  goto edone;
done:
  df->last_at = trans.at;
  df->last_end = trans.end;
  df->lastBlkSz = trans.size;
  ret = 1;
edone:
  if (zap) kdbf_zap_start(df);
  TXTK_TRACE_AFTER_START(df, TXTK_WRITE)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT ", offset " TXTK_OFFFMT
                   "): %1.3kf sec returned %d %s offset " TXTK_OFFFMT,
                   TXTK_INITARGS(df), TXTK_OFFARGS(oat), TXTK_TRACE_TIME(),
                   ret, (ret ? "ok" : "ERROR"), TXTK_OFFARGS(df->last_at));
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

#ifndef unix
static void kdbf_tmpabendcb ARGS((void *usr));
static void
kdbf_tmpabendcb(usr)
void    *usr;
/* ABEND callback: deletes temp KDBF file during segfault, etc.
 */
{
  KDBF  *df = (KDBF *)usr;

  /* Must close before unlink for Windows: */
  if (!KDBF_ISBADFH(df)) KDBF_CLOSE(df);
  if (df->fn != CHARPN) unlink(df->fn);
}
#endif /* !unix */

KDBF *
kdbf_close(df)
KDBF	*df;
{
  static CONST char     fn[] = "kdbf_close";
  int                   ofh;
  char                  *s;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  if (df == KDBFPN) return(NULL);
  if (df->in_btree)
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn, InvalidFunctionWhileInFreeTreeFmt,
                     df->fn);
      return(NULL);
    }

  KdbfNumOpen--;

  df->callDepth++;
  ofh = df->fh;
  TXTK_TRACE_BEFORE_START(df, TXTK_OPEN)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ") starting", TXTK_INITARGS(df));
  TXTK_TRACE_BEFORE_FINISH()

  if (TxKdbfIoStats & 0x4)
    txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN, "kdbf_close(%s) = 0x%lx",
                   df->fn, (long)df);
  kdbf_flush(df);
  if (!KDBF_ISBADFH(df))
    {
      CLEAR_ERR();
      if (KDBF_CLOSE(df) != 0)
        txpmbuf_putmsg(df->pmbuf, MERR + FCE, fn,
                       "Cannot close KDBF file %s: %s",
                       df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      df->fh = KDBF_BADFH;
      df->fhcuroff = (EPI_OFF_T)(-1);
    }
#ifndef unix
  /* Delete temp file.  Already deleted if unix: */
  if (df->flags & KDF_TMP)
    {
      TXdelabendcb(kdbf_tmpabendcb, df);
      unlink(df->fn);
    }
#endif /* !unix */
  if (df->blk) FREE(df, df->blk);
  if (df->outbuf != BYTEPN) FREE(df, df->outbuf);
  if (df->rdbuf != BYTEPN) FREE(df, df->rdbuf - KDBF_ALIGN_SIZE);
  if (df->pseudo_dbf) FREE(df, df->pseudo_dbf);
  kdbf_freebtreefields(df);
  if (IOSTATS >= 1 && (IOSTATS >= 2 ||
                       df->fn == CHARPN || strstr(df->fn, "SYS") == CHARPN))
    {
      if (TxKdbfIoStatsFile != CHARPN)          /* only this file */
        {
          if (df->fn == CHARPN) goto cont;
          s = strrchr(df->fn, PATH_SEP);
          if (s == CHARPN) s = df->fn;
          else s++;
          if (strcmp(s, TxKdbfIoStatsFile) != 0 &&
              strcmp(df->fn, TxKdbfIoStatsFile) != 0)
            goto cont;
        }
      txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN, "I/O stats for %s:", df->fn);
      txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN,
                "  Sys  reads: %kwu (%kwuB) writes: %kwu (%kwuB) seeks: %kwu",
                     (EPI_HUGEUINT)df->reads, (EPI_HUGEUINT)df->readbytes,
                     (EPI_HUGEUINT)df->writes, (EPI_HUGEUINT)df->writebytes,
                     (EPI_HUGEUINT)df->lseeks);  /* wtf skippedlseeks too */
      txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN,
   "  KDBF reads: %kwu (%kwuB) writes: %kwu (%kwuB) frees: %kwu valids: %kwu",
                     (EPI_HUGEUINT)df->kreads, (EPI_HUGEUINT)df->kreadbytes,
                     (EPI_HUGEUINT)df->kwrites, (EPI_HUGEUINT)df->kwritebytes,
                     (EPI_HUGEUINT)df->kfrees, (EPI_HUGEUINT)df->kvalids);
      txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN, "  mallocs: %kwu (%kwuB) memcpys: %kwu (%kwuB) memmoves/sets: %kwu/%kwu (%kwuB/%kwuB)",
                     (EPI_HUGEUINT)df->mallocs,
                     (EPI_HUGEUINT)df->mallocbytes, (EPI_HUGEUINT)df->memcpys,
                     (EPI_HUGEUINT)df->memcpybytes,(EPI_HUGEUINT)df->memmoves,
                     (EPI_HUGEUINT)df->memsets,(EPI_HUGEUINT)df->memmovebytes,
                     (EPI_HUGEUINT)df->memsetbytes);
    }
cont:
  TXTK_TRACE_AFTER_START(df, TXTK_OPEN)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT "): %1.3lf sec closed fd %d",
                   TXTK_INITARGS(df), TXTK_TRACE_TIME(), ofh);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  df->fn = TXfree(df->fn);
  df->pmbuf = txpmbuf_close(df->pmbuf);
  df = TXfree(df);
  return(NULL);
}

int
TXkdbfSetPmbuf(KDBF *df, TXPMBUF *pmbuf)
/* Returns 0 on error.
 */
{
  TXPMBUF       *pmbufOrg = pmbuf;

  pmbuf = txpmbuf_open(pmbuf);		        /* clone `pmbuf' first */
  if (!pmbuf && pmbufOrg) return(0);
  df->pmbuf = txpmbuf_close(df->pmbuf);
  df->pmbuf = pmbuf;
  /* wtf pass to B-tree object too? */
  return(1);                                    /* success */
}

KDBF *
kdbf_open(pmbuf, filename, flags)
TXPMBUF *pmbuf;         /* (in, opt.) buffer to clone for messages */
char *filename;         /* (in, opt.) file; NULL/empty for temp */
int  flags;             /* O_... bits */
/* Passes `flags' direct to open().  If O_RDWR fails and no O_CREAT,
 * tries O_RDONLY.  O_BINARY, O_NOINHERIT automatically or'd in with flags.
 * Errno/TXgeterror() set on error (which is reported).
 */
{
  static CONST char     fn[] = "kdbf_open";
  KDBF		        *df;
  EPI_STAT_S	        st;
  KDBF_START	        start;
  KDF                   traceflag = (KDF)0;
  int		        i, tryflags, trymode;
  CONST char            *tracebase, *filebase;
  char                  *d;
  char                  tmp[2][128];
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  flags |= TX_O_BINARY;         /* kdbf_raw_open() adds TX_O_NOINHERIT */
  tryflags = flags;
  trymode = S_IREAD;
  if (flags & (O_WRONLY|O_RDWR)) trymode |= S_IWRITE;
  CLEAR_ERR();
  if (!(df = (KDBF *)TXcalloc(pmbuf, fn, 1, sizeof(KDBF))))
    goto err;

  df->pmbuf = txpmbuf_open(pmbuf);
  df->blk_data_sz = (size_t)-1;
  df->fh = KDBF_BADFH;
  df->fhcuroff = (EPI_OFF_T)(-1);
  df->outbufoff = (EPI_OFF_T)(-2L);
  df->outbuflastsent = df->outbuflastsave = (EPI_OFF_T)(-1L);
  df->last_at = (EPI_OFF_T)(-1L);	/* haven't read anything yet */
  df->last_end = (EPI_OFF_T)0L;	                /* i.e. start of file */
  df->lastBlkSz = 0;
  for (i = 0; i < KDBF_LS_NUM; i++)
    df->last_sz[i] = KDBF_INIT_READ_CHUNK_SIZE;
  /* rest cleared by calloc() */

  /* Cached free pointers; if set, these can only be valid during atomic
   * operations (i.e. in B-tree):
   */
  kdbf_zap_start(df);

  /* B-tree cache stuff cleared by calloc() */

  if (filename == CHARPN || *filename == '\0')          /* make a temp file */
    {
      df->flags |= KDF_TMP;
      if ((d = getenv("TMPDIR")) == CHARPN &&
          (d = getenv("TMP")) == CHARPN &&
          (d = getenv("TEMP")) == CHARPN
#ifndef _WIN32
          &&
          (d = "/usr/tmp", (EPI_STAT(d, &st) != 0 || !S_ISDIR(st.st_mode))) &&
          (d = "/var/tmp", (EPI_STAT(d, &st) != 0 || !S_ISDIR(st.st_mode)))
#endif /* !_WIN32 */
          )
#ifdef _WIN32
        d = "c:\\tmp";
#else /* !_WIN32 */
        d = "/tmp";
#endif /* !_WIN32 */
      CLEAR_ERR();
      if ((df->fn = TXtempnam(d, CHARPN, CHARPN)) == CHARPN)
        {
          ErrGuess = KR_NO_MEM;
          SAVE_ERR();
          txpmbuf_putmsg(df->pmbuf, MERR + FME, fn,
                         "Cannot create temporary KDBF file: %s",
                         kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          RESTORE_ERR();
          goto err;
        }
    }
  else
    {
      CLEAR_ERR();
      if ((df->fn = TXstrdup(TXPMBUFPN, fn, filename)) == CHARPN)
        goto err;
    }

  df->callDepth++;
  if (TXtraceKdbf)
    {
      if (TXtraceKdbfFile == CHARPN || *TXtraceKdbfFile == '\0')
        {                                       /* all files all DBs */
          df->flags |= KDF_TRACE;
          traceflag = KDF_TRACE;
        }
      else
        {
          tracebase = TXbasename(TXtraceKdbfFile);
          filebase = TXbasename(df->fn);
          if ((tracebase == TXtraceKdbfFile ||   /* no DB: trace all DBs */
               TXpathcmp(TXtraceKdbfFile, tracebase - TXtraceKdbfFile,
                         df->fn, filebase - df->fn) == 0) &&
              /* file must match also: */
              (strcmp(tracebase, "SYS") == 0 ? strncmp(filebase, "SYS", 3)==0:
               (strcmp(tracebase, "USR") == 0 ?strncmp(filebase, "SYS", 3)!=0:
                TXpathcmp(tracebase, -1, filebase, -1) == 0)))
            {
              df->flags |= KDF_TRACE;
              traceflag = KDF_TRACE;
            }
        }
    }

  TXTK_TRACE_BEFORE_START(df, TXTK_OPEN)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   "kdbf_open(%s, %s) starting",
                   df->fn, TXo_flags2str(tmp[0], sizeof(tmp[0]), flags));
  TXTK_TRACE_BEFORE_FINISH()

  /* Do actual file open, but return quietly if corruption since this
   * may be an FDBF file that will be opened silently later:  -KNG 960320
   */
  if (df->flags & KDF_TMP)
    {
      CLEAR_ERR();
      if (!kdbf_raw_open(df, tryflags, trymode))
        {
          SAVE_ERR();
          txpmbuf_putmsg(df->pmbuf, MERR + FOE, fn,
                         "Cannot create temporary KDBF file %s: %s",
                         df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          RESTORE_ERR();
          goto err;
        }
#ifdef unix
      /* Unlink the file now: it will disappear from the directory,
       * but still be accessible via df->fh.  As soon as we exit,
       * for _any_ reason (even SIGKILL) the OS will delete it:
       */
      unlink(df->fn);
#else
      /* Under Windows etc. we cannot delete an in-use file; use
       * an abend callback.  Less reliable since it may not get called:
       */
      TXaddabendcb(kdbf_tmpabendcb, df);
#endif
      goto cont;
    }
  CLEAR_ERR();
  if (kdbf_raw_open(df, tryflags, trymode)) goto cont;
  if ((tryflags & (O_RDWR|O_CREAT)) == O_RDWR)  /* fall back to O_RDONLY */
    {
      tryflags &= ~O_RDWR;
      tryflags |= O_RDONLY;
      trymode &= ~S_IWRITE;
      CLEAR_ERR();
      if (kdbf_raw_open(df, tryflags, trymode)) goto cont;
    }
  /* Yap about error; it's a problem even if we try to fall back to FDBF: */
  SAVE_ERR();
  txpmbuf_putmsg(df->pmbuf, MERR + FOE, CHARPN,
                 "Cannot %s%s KDBF file %s for %s: %s",
                 ((flags & O_CREAT) ? "create" : "open"),
                 ((flags & O_EXCL) ? " exclusive" : ""),
                df->fn, ((flags & (O_WRONLY|O_RDWR)) ? "writing" : "reading"),
                 kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
  RESTORE_ERR();
  goto err;

cont:
  if (!(tryflags & (O_WRONLY|O_RDWR))) df->flags |= KDF_READONLY;

  /* If TxKdbfQuickOpen is nonzero, we skip all the sanity checks.
   * Assumes that file is non-empty KDBF:
   */
  if (TxKdbfQuickOpen) goto ok;

  CLEAR_ERR();
  if (EPI_FSTAT(df->fh, &st) < 0)
    {
      SAVE_ERR();
      txpmbuf_putmsg(df->pmbuf, MERR + FTE, fn,
                     "Cannot fstat() KDBF file %s: %s",
                     df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      RESTORE_ERR();
      goto err;
    }
  if (st.st_size == (EPI_OFF_T)0L) {	/* new file */
    /* Init file: write null pointers: */
    start.btree = start.free_pages = (EPI_OFF_T)0L;	/* B-tree takes 0L as null */
    if (df->flags & KDF_READONLY)
      {
        SAVE_ERR();
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
        RESTORE_ERR();
#ifdef _WIN32
        TXseterror(ERROR_ACCESS_DENIED);
        errno = EACCES;
#else /* !_WIN32 */
        TXseterror(EACCES);
#endif /* !_WIN32 */
        goto err;
      }
    CLEAR_ERR();
    if (kdbf_raw_lseek(df, 0, SEEK_SET) < (EPI_OFF_T)0L ||
	kdbf_raw_write(df, &start, sizeof(start)) != (size_t)sizeof(start)) {
      SAVE_ERR();
      txpmbuf_putmsg(df->pmbuf, MERR+FWE, fn,
                     "Cannot write start pointers to KDBF file %s: %s",
                     df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      RESTORE_ERR();
      goto err;
    }
    /* Do some sanity checks; this could be an fdbf file or otherwise bogus: */
  } else if (st.st_size < (EPI_OFF_T)sizeof(KDBF_START)) {
    /* No kdbf file is smaller than sizeof(KDBF_START): */
    txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                   (EPI_HUGEINT)0, df->fn, "Truncated start pointers");
#ifdef _WIN32
    TXseterror(ERROR_HANDLE_EOF);
    errno = EINVAL;
#else /* !_WIN32 */
    TXseterror(EINVAL);
#endif /* !_WIN32 */
    goto err;
  } else if (st.st_size == (EPI_OFF_T)sizeof(KDBF_START)) {
    /* It's an empty file: make sure free pointers are 0L: */
    CLEAR_ERR();
    if (kdbf_raw_lseek(df, 0, SEEK_SET) < (EPI_OFF_T)0L ||
	kdbf_raw_read(df, &start, sizeof(start), -1) != (size_t)sizeof(start))
      {
        SAVE_ERR();
        txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CantReadPtrs, df->fn,
                       kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
        RESTORE_ERR();
        goto err;
      }
    if (start.btree != (EPI_OFF_T)0L || start.free_pages != (EPI_OFF_T)0L)
      {
        txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                       (EPI_HUGEINT)0, df->fn,
                       "Non-empty start pointers in empty file");
#ifdef _WIN32
        TXseterror(ERROR_INVALID_DATA);
        errno = EINVAL;
#else /* !_WIN32 */
        TXseterror(EINVAL);
#endif /* !_WIN32 */
        goto err;
      }
  } else {
    KDBF_TRANS	t;
    /* Make sure first block has valid header (but don't bail if not): */
    if (!read_head(df, 0, &t, (RH_NOBAIL | RH_HDRONLY)))
      {
        SAVE_ERR();
        txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                       (EPI_HUGEINT)0, df->fn,
                       "Cannot read first block header");
        RESTORE_ERR();
        goto err;
      }
  }
ok:
  if (TxKdbfIoStats & 0x4)
    txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN, "kdbf_open(%s, %s) = 0x%lx%s%s",
                   df->fn, TXo_flags2str(tmp[0], sizeof(tmp[0]), flags),
                   (long)df, (flags == tryflags ? "" : " "),
                   (flags == tryflags ? "" :
                    TXo_flags2str(tmp[1], sizeof(tmp[1]), tryflags)));
  if (++KdbfNumOpen > KdbfNumMax) KdbfNumMax = KdbfNumOpen;
  goto done;

err:
  SAVE_ERR();
  if (TxKdbfIoStats & 0x4)
    txpmbuf_putmsg(df->pmbuf, MINFO, CHARPN, "kdbf_open(%s, %s) FAILED",
                   filename, TXo_flags2str(tmp[0], sizeof(tmp[0]), flags));
  if (df != KDBFPN) KdbfNumOpen++;      /* balance kdbf_close() */
  kdbf_close(df);
  df = KDBFPN;
  RESTORE_ERR();
done:
  if (df)
    {
      TXTK_TRACE_AFTER_START(df, TXTK_OPEN)
      TXTK_TRACE_AFTER_FINISH()
    }
  if ((TXtraceKdbf & (TXTK_OPEN | TXTK_USERCALLS)) ==
      (TXTK_OPEN | TXTK_USERCALLS) && traceflag)
    {
      SAVE_ERR();
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
           "kdbf_open(%s, %s): %1.3lf sec returned fd %d handle 0x%lx%s%s %s",
                     (df ? df->fn : (filename ? filename : "?")),
                     TXo_flags2str(tmp[0], sizeof(tmp[0]), flags),
                     TXTK_TRACE_TIME(), (int)df->fh, (long)df,
                     (flags == tryflags ? "" : " "), (flags == tryflags ? "" :
                             TXo_flags2str(tmp[1], sizeof(tmp[1]), tryflags)),
                     (df ? "ok" : "ERROR"));
      RESTORE_ERR();
    }
  if (df) df->callDepth--;
  return(df);
}

void
TXkdbfStatsMsg(TXPMBUF *pmbuf)
{
  txpmbuf_putmsg(pmbuf, MINFO, CHARPN, "KDBF open handles: %d  max: %d",
                 KdbfNumOpen, KdbfNumMax);
}

int
kdbf_ioctl(df, ioctl, data)
KDBF    *df;
int     ioctl;
void    *data;
/* Handles I/O control calls.  Returns -1 on error, 0 if ok.  NOTE:
 * some ioctls apply separately when called for internal KDBF free
 * page B-tree.  `ioctl' is DBF_KAI + one of the following values:
 *
 *   KDBF_IOCTL_PREBUFSZ
 *     `data' is number of slack bytes at start of buffer (before real
 *     data) that KDBF may scratch on, for header (see diagram).  Applies
 *     to kdbf_alloc(), kdbf_put(); and kdbf_read() when offset is NULL
 *     or 0.  Saves an extra write or read sometimes.  Should be at least
 *     KDBF_PREBUFSZ_WANT bytes big.  NOTE: An extra memmove and read
 *     may be incurred if PREBUFSZ is set and a modified-size block
 *     (used != size) is read with kdbf_read().  NOTE: separate settings
 *     for KDBF user and internal B-tree.
 *
 *   KDBF_IOCTL_POSTBUFSZ
 *     `data' is number of slack bytes after end of data, that KDBF
 *     may scratch on (for start pointers/checksum).  Saves an extra
 *     write for new DBF blocks at EOF if not in no-readers mode, or
 *     an extra read for free-tree pages' checksums.  Used with
 *     KDBF_IOCTL_PREBUFSZ.  Applies to kdbf_alloc(), kdbf_put(),
 *     kdbf_read().  Should be at least KDBF_POSTBUFSZ_WANT bytes big.
 *     NOTE: separate settings for KDBF user and internal B-tree.
 *     Buffer passed to kdbf_put(), kdbf_alloc(), kdbf_read():
 *
 *       buf
 *        V            <----------len (data size)----------->
 *        +------------+------------------------------------+-------------+
 *        <--prebufsz--X-------------trans.used-------------X--postbufsz-->
 *
 *   KDBF_IOCTL_APPENDONLY
 *     Indicates that this handle is the only writer to this file
 *     (other handles may be readers) and that data should always be
 *     appended to EOF.  Saves reading the start pointers on every
 *     write.  `data' is on (non-NULL) / off (NULL) flag.  Reads are
 *     disallowed on the handle.
 *
 *   KDBF_IOCTL_NOREADERS
 *     Indicates that there are no other readers of this file.  Used
 *     in conjunction with KDBF_IOCTL_APPENDONLY.  Saves an lseek,
 *     since start pointers are not written until kdbf_close().
 *     kdbf_close() or kdbf_flush() must be called when finished,
 *     as file is incomplete after writes.  `data' is on/off flag.
 *
 *   KDBF_IOCTL_WRITEBUFSZ
 *     Sets write buffer size.  Used only in conjunction with
 *     KDBF_IOCTL_APPENDONLY and KDBF_IOCTL_NOREADERS.  Reduces writes when
 *     writing small blocks.  `data' is integer size of buffer, 0 for none.
 *     Cannot be used in conjunction with READBUFSZ.
 *
 *   KDBF_IOCTL_SEEKSTART
 *     Sets flag such that the next action, if a kdbf_[a]get(-1), will
 *     read the 2nd (non-free) block from the file.  Essentially like
 *     a kdbf_get(0) and discarding the data, without the I/O: seeks to 0.
 *     (2nd block so we skip the .tbl header).
 *
 *   KDBF_IOCTL_READBUFSZ
 *     Sets read buffer size.  Used only during a contiguous read lock;
 *     set buffer size to 0 when lock is released.  Speeds kdbf_get(),
 *     kdbf_aget() for small tables read wholesale, e.g. SYS... tables.
 *     Cannot be used in conjunction with WRITEBUFSZ, and must be 0
 *     before write attempts.
 *
 *   KDBF_IOCTL_GETNEXTOFF
 *     Sets *(EPI_OFF_T *)data to next block offset, i.e. what block get(-1)
 *     will probably return (probably because it might be a free block
 *     not skipped until the actual get).  Used in conjunction with
 *     SETNEXTOFF to preserve a get(-1) series of calls while other calls
 *     are made to the handle.
 *
 *   KDBF_IOCTL_SETNEXTOFF
 *     Sets offset of probable next block to be returned by get(-1) from
 *     *(EPI_OFF_T *)data, which should be a previous GETNEXTOFF value.
 *
 *   KDBF_IOCTL_OVERWRITE
 *     Overwrites file, i.e. resets position to start of file, overwrites
 *     existing data, free, free-tree, free free-tree blocks, always
 *     appends at current position, and truncates when closed.
 *     Used by copydbf to overwrite and compress an existing file.
 *     Sets KDBF_IOCTL_APPENDONLY and KDBF_IOCTL_NOREADERS (even though
 *     that's false; the src copydbf handle should stay ahead of us).
 *
 *   KDBF_IOCTL_IGNBADPTRS
 *     Ignore start pointers if bad.  Used by addtable when converting
 *     from 32- to 64-bit table.
 *
 *   KDBF_IOCTL_FREECURBUF
 *     Frees memory for current (just-read) block; caller can no longer
 *     use the last returned kdbf_get() etc. returned pointer.  Buffer
 *     will be re-allocated as needed on next kdbf_get() etc. call.
 */
{
  static CONST char     fn[] = "kdbf_ioctl";
  KDF                   flag;
  byte                  *newbuf;
  int                   ret, kio;
  char                  intmp[EPI_OFF_T_BITS/4 + 4], outtmp[EPI_OFF_T_BITS/4+4];
  char                  kiotmp[EPI_OS_LONG_BITS/4 + 6];
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;

  /* Get KDBF_IOCTL_... number; map to KDBF_IOCTL_UNKNOWN if unknown: */
  kio = (ioctl & ~DBF_KAI);
  if ((unsigned)kio >= (unsigned)KDBF_IOCTL_NUM ||
      (ioctl & 0xffff0000) != DBF_KAI)
    {
      kio = KDBF_IOCTL_UNKNOWN;
      htsnpf(kiotmp, sizeof(kiotmp), "0x%lx=?", (long)ioctl);
    }
  else
    *kiotmp = '\0';

  if ((TXtraceKdbf & (TXTK_IOCTLSEEK | TXTK_IOCTLOTHER |
                      TXTK_BEFORE(TXTK_IOCTLSEEK|TXTK_IOCTLOTHER))) &&
      (df->flags & KDF_TRACE))
    {
      switch (TXioctlArgTypes[kio])
        {
	case 0:  htsnpf(intmp, sizeof(intmp), "%" EPI_VOIDPTR_DEC_FMT,(EPI_VOIDPTR_INT)data); break;
        case 1:  strcpy(intmp, (data != NULL ? "on" : "off"));  break;
        case 2:  htsnpf(intmp, sizeof(intmp), "0x%wx",
                        (EPI_HUGEUINT)*(EPI_OFF_T *)data);          break;
        case 3:
        default: strcpy(intmp, "void");                         break;
        }
      TXTK_TRACE_BEFORE_START(df,
                 (TXioctlSeekSig[kio] ? TXTK_IOCTLSEEK : TXTK_IOCTLOTHER))
        txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                       TXTK_INITFMT ", %s%s, %s) starting",
                       TXTK_INITARGS(df),
                       (kio == KDBF_IOCTL_UNKNOWN ? "" : "KDBF_IOCTL_"),
                       (kio == KDBF_IOCTL_UNKNOWN ? kiotmp:TXioctlNames[kio]),
                       intmp);
      TXTK_TRACE_BEFORE_FINISH()
    }

  switch (ioctl)
    {
    case DBF_KAI | KDBF_IOCTL_PREBUFSZ:
      /* See also drill in kfbtree.c: */
      if (df->in_btree) df->btree_prebufsz = (size_t)(EPI_VOIDPTR_INT)data;
      else df->prebufsz = (size_t)(EPI_VOIDPTR_INT)data;
      break;
    case DBF_KAI | KDBF_IOCTL_POSTBUFSZ:
      /* See also drill in kfbtree.c: */
      if (df->in_btree) df->btree_postbufsz = (size_t)(EPI_VOIDPTR_INT)data;
      else df->postbufsz = (size_t)(EPI_VOIDPTR_INT)data;
      break;
    case DBF_KAI | KDBF_IOCTL_APPENDONLY:
      if (df->in_btree)
        {
        btreeio:
          txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
                       "KDBF ioctl(%d) cannot be used within internal B-tree",
                         ioctl);
          goto err;
        }
      if (!kdbf_flush(df)) goto err;
      kdbf_zap_start(df);       /* in case append-only is being turned off */
      flag = KDF_APPENDONLY;
    doflag:
      if (data == NULL)
        df->flags &= ~flag;
      else
        df->flags |= flag;
      break;
    case DBF_KAI | KDBF_IOCTL_NOREADERS:
      if (df->in_btree) goto btreeio;
      if (!kdbf_flush(df)) goto err;
      flag = KDF_NOREADERS;
      goto doflag;
    case DBF_KAI | KDBF_IOCTL_WRITEBUFSZ:
      if (df->in_btree) goto btreeio;
      if (df->rdbufsz > 0 && (EPI_VOIDPTR_INT)data > (EPI_VOIDPTR_INT)0)
        {
        rdwrmutex:
          txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
    "Internal error: KDBF_IOCTL_WRITEBUFSZ/READBUFSZ are mutually exclusive");
          goto err;
        }
      if (!kdbf_flush(df)) goto err;
      if (data == NULL)                                 /* free buffer */
        {
          if (df->outbuf != BYTEPN)
            {
              FREE(df, df->outbuf);
              df->outbuf = BYTEPN;
            }
          df->outbufsz = 0;
        }
      else                                              /* allocate buffer */
        {
          newbuf = (byte *)(df->outbuf == BYTEPN ?
                            MALLOC(fn, df, (EPI_VOIDPTR_INT)data) :
                          REALLOC(fn, df, df->outbuf, (EPI_VOIDPTR_INT)data));
          if (newbuf == BYTEPN)
            {
#ifdef EPI_REALLOC_FAIL_SAFE
              df->outbuf = TXfree(df->outbuf);
#endif /* EPI_REALLOC_FAIL_SAFE */
              df->outbuf = BYTEPN;
              df->outbufsz = df->outbufused = 0;
              df->outbufoff = (EPI_OFF_T)(-2L);
              df->outbuflastsent = df->outbuflastsave = (EPI_OFF_T)(-1L);
              goto err;
            }
          df->outbuf = newbuf;
          df->outbufsz = (size_t)data;
        }
      df->outbufused = 0;
      df->outbufoff = (EPI_OFF_T)(-2L);
      df->outbuflastsent = df->outbuflastsave = (EPI_OFF_T)(-1L);
      break;
    case DBF_KAI | KDBF_IOCTL_SEEKSTART:
      if (df->in_btree) goto btreeio;
      df->last_at = (EPI_OFF_T)0L;
      df->last_end = (EPI_OFF_T)(-2L);              /* flag for seek_block() */
      df->lastBlkSz = 0;
      break;
    case DBF_KAI | KDBF_IOCTL_READBUFSZ:
      if (df->in_btree) goto btreeio;
      if (df->outbufsz > 0 && (EPI_VOIDPTR_INT)data > (EPI_VOIDPTR_INT)0)
          goto rdwrmutex;
      if (!kdbf_flush(df)) goto err;
      if (data == NULL)                                 /* free buffer */
        {
          if (df->rdbuf != BYTEPN)
            {
              FREE(df, df->rdbuf - KDBF_ALIGN_SIZE);
              df->rdbuf = BYTEPN;
            }
          df->rdbufstart = BYTEPN;
          df->rdbufsz = df->rdbufused = 0;
          df->rdbufoff = (EPI_OFF_T)(-1L);
        }
      else
        {
          newbuf = (byte *)(df->rdbuf == BYTEPN ?
                     MALLOC(fn, df, (EPI_VOIDPTR_INT)data + KDBF_ALIGN_SIZE) :
                            REALLOC(fn, df, df->rdbuf - KDBF_ALIGN_SIZE,
                                    (EPI_VOIDPTR_INT)data + KDBF_ALIGN_SIZE));
          if (newbuf == BYTEPN)
            {
#ifdef EPI_REALLOC_FAIL_SAFE
              if (df->rdbuf != BYTEPN) FREE(df, df->rdbuf - KDBF_ALIGN_SIZE);
#endif /* EPI_REALLOC_FAIL_SAFE */
              df->rdbuf = BYTEPN;
              df->rdbufsz = df->rdbufused = 0;
              df->rdbufoff = (EPI_OFF_T)(-1L);
              goto err;
            }
          df->rdbuf = newbuf + KDBF_ALIGN_SIZE;
          df->rdbufstart = df->rdbuf;
          df->rdbufsz = (size_t)data;
          df->rdbufused = 0;
          df->rdbufoff = (EPI_OFF_T)(-1L);
        }
      break;
    case DBF_KAI | KDBF_IOCTL_GETNEXTOFF:
      if (df->in_btree) goto btreeio;
      *(EPI_OFF_T *)data = df->last_end;
      break;
    case DBF_KAI | KDBF_IOCTL_SETNEXTOFF:
      if (df->in_btree) goto btreeio;
      df->last_at = (EPI_OFF_T)0L;  /* ? should be -1? */
      df->last_end = *(EPI_OFF_T *)data;
      df->lastBlkSz = 0;
      break;
    case DBF_KAI | KDBF_IOCTL_OVERWRITE:
      if (kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_APPENDONLY), data) != 0 ||
          kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_NOREADERS), data) != 0)
        goto err;
      if (data != NULL)
        {
          if (kdbf_raw_lseek(df, (EPI_OFF_T)0L, SEEK_SET) != (EPI_OFF_T)0)
            {
              txpmbuf_putmsg(df->pmbuf, MERR+FSE, fn,
                             "Cannot seek to start of KDBF file %s: %s",
                             df->fn, kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
              kdbf_ioctl(df, KDBF_IOCTL_APPENDONLY, NULL);
              kdbf_ioctl(df, KDBF_IOCTL_NOREADERS, NULL);
              goto err;
            }
          df->last_end = df->start.btree = df->start.free_pages =
            df->start_off = (EPI_OFF_T)0L;     /* zap the free tree */
          df->flags |= KDF_FLUSHPTRS;
        }
      flag = KDF_OVERWRITE;
      goto doflag;
      break;
    case DBF_KAI | KDBF_IOCTL_IGNBADPTRS:
      flag = KDF_IGNBADPTRS;
      goto doflag;
      break;
    case DBF_KAI | KDBF_IOCTL_FREECURBUF:
      if (df->blk)
        {
          FREE(df, df->blk);
          df->blk = NULL;
          df->blksz = 0;
          df->blk_data = NULL;
          df->blk_data_sz = 0;
        }
      break;
    default:
      goto err;                                 /* unknown ioctl */
    }
  ret = 0;                                      /* ok */
  goto done;

err:
  ret = -1;
done:
  TXTK_TRACE_AFTER_START(df,
             (TXioctlSeekSig[kio] ? TXTK_IOCTLSEEK : TXTK_IOCTLOTHER))
    switch (TXioctlArgTypes[kio])
      {
      /* only type 2 can actually return data: */
      case 2:   htsnpf(outtmp, sizeof(outtmp), " 0x%wx",
                       (EPI_HUGEUINT)*(EPI_OFF_T *)data);   break;
      default:  *outtmp = '\0';                         break;
      }
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                   TXTK_INITFMT
                   ", %s%s, %s): %1.3kf sec returned%s %s",
                   TXTK_INITARGS(df),
                   (kio == KDBF_IOCTL_UNKNOWN ? "" : "KDBF_IOCTL_"),
                   (kio == KDBF_IOCTL_UNKNOWN ? kiotmp : TXioctlNames[kio]),
                   intmp,
                   TXTK_TRACE_TIME(), outtmp, (ret != -1 ? "ok" : "ERROR"));
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

static int seek_block ARGS((KDBF *df, EPI_OFF_T at, KDBF_TRANS *trans));
static int
seek_block(df, at, trans)
KDBF            *df;
EPI_OFF_T   	at;     /* the handle, or -1 for next block */
KDBF_TRANS      *trans;
/* Reposition the kdbf file to the start of a block and read in the header
 * plus some of the data.  If "at" is -1L it will seek to the next block.
 * Returns 1 if ok, 0 if error or eof.
 * Free-tree pages are skipped over; if we're in a free operation, then
 * seeking the next block with -1L is illegal (free tree is a B-tree; no
 * need to get next block).  Fills in `trans' with block seeked to, and
 * saves position.  -KNG   See also kdbf_get() with READBUFSZ
 */
{
  static CONST char     fn[] = "seek_block";

  if (at == (EPI_OFF_T)(-1L)) {		/* i.e. next block after last one */
    if (df->in_btree)           /* never need to get next free-tree page */
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn, InvalidFunctionWhileInFreeTreeFmt,
                       df->fn);
        df->lasterr = -1;
        return(0);
      }
    if (df->last_end == (EPI_OFF_T)(-2L))           /* KDBF_IOCTL_SEEKSTART */
      {
        if (!read_head(df, (EPI_OFF_T)0L, trans, 0)) return(0);
        df->last_end = trans->end;              /* skip first block */
      }
    if (df->last_end < (EPI_OFF_T)0L)           /* last op was error */
      {
        df->lasterr = (errno ? errno : -1);
        return(0);
      }
    /* Seek to the `end' of the previous block (i.e. next block).
     * read_head() will check EOF for us and return (non-corrupt) error:
     * -KNG 950925
     */
    at = df->last_end;
    if (!read_head(df, at, trans, RH_UPDATEAVG))
      return(0);

    /* Keep looking if it's a free block or a free-tree page: */
    while (trans->used == 0 || (trans->type & KDBF_FREEBITS)) {
      /* use SEEK_SET since read_head() reads beyond header: -KNG 950906 */
      at = trans->end;
      if (!read_head(df, at, trans, RH_UPDATEAVG))
	return(0);
    }
  } else {
    if (!read_head(df, at, trans, RH_UPDATEAVG)) return(0);
  }
  /* Save position even if free block and error, so that -1L seek will
   * work:  -KNG 950824:
   */
  df->last_at = trans->at;
  df->last_end = trans->end;
  df->lastBlkSz = trans->size;
  /* Non free-tree seeks shouldn't be to free-tree pages, and vice versa.
   * Empty blocks are checked by caller:  -KNG 950821
   */
  if ((df->in_btree ? 1 : 0) ^ (trans->type & KDBF_FREEBITS ? 1 : 0)) {
    txpmbuf_putmsg(df->pmbuf, MERR, fn,
	"Seek to %sfree-tree block 0x%wx while %sin free-tree in KDBF file %s",
                   (trans->type & KDBF_FREEBITS ? "" : "non-"),
                   (EPI_HUGEUINT)trans->at, (df->in_btree ? "" : "not "),
                   df->fn);
    df->lasterr = EIO;
    return(0);
  }
  return(1);
}

int
kdbf_valid(df, at)
KDBF            *df;
EPI_OFF_T       at;             /* the handle, or -1 for next block */
{
  static CONST char     fn[] = "kdbf_valid";
  KDBF_TRANS	trans;

  df->kvalids++;

  if (at == (EPI_OFF_T)(-1L)) return(1);

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);
      return(0);
    }
  if (!read_head(df, at, &trans, RH_HDRONLY | RH_NOBAIL)) return(0);

  /* must be free-tree page if we're freeing, and vice-versa: */
  if (trans.used == 0 ||
      ((df->in_btree ? 1 : 0) ^ (trans.type & KDBF_FREEBITS ? 1 : 0)))
    return(0);
  return(1);
}

EPI_OFF_T
kdbf_tell(df)
KDBF *df;
{
  return(df->last_at);
}

EPI_OFF_T
TXkdbfGetCurrentBlockEnd(df)
KDBF *df;
{
  return(df->last_end);
}

size_t
TXkdbfGetCurrentBlockMaxDataSize(df)
KDBF *df;
{
  return(df->lastBlkSz);
}

char *
kdbf_getfn(df)            /* MAW 04-04-94 - add method to remove drill */
KDBF *df;
{
  return(df->fn);
}

int
kdbf_getfh(df)            /* MAW 04-04-94 - add method to remove drill */
KDBF *df;
/* NOTE: Caller must not read/write/lseek on returned handle,
 * or corruption may result.
 */
{
  return(KDBF_FILENO(df));
}

int
TXkdbfReleaseDescriptor(df)
KDBF    *df;
{
  int   fh;

  fh = KDBF_FILENO(df);
  df->fh = KDBF_BADFH;
  return(fh);
}

int
TXkdbfGetLastError(df)
KDBF    *df;
{
  return(df->lasterr);
}

void
kdbf_setoveralloc(df, ov)  /* MAW 04-04-94 - add method to remove drill */
KDBF *df;
int ov;
{
  static CONST char     fn[] = "kdbf_setoveralloc";

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, 0, fn);
      return;
    }
  if (ov >= 0 && ov < EPI_OS_SIZE_T_BITS)	/* sanity check KNG 960320 */
    df->overalloc = ov;
}

static size_t kdbf_getbuf ARGS((KDBF *df, EPI_OFF_T at, KDBF_TRANS *trans,
                                byte **data));
static size_t
kdbf_getbuf(df, at, trans, data)
KDBF            *df;
EPI_OFF_T       at;
KDBF_TRANS      *trans;
byte            **data;
/* Gets block `at' using read buffer.  Returns user data size, or -1
 * on error/EOF, and `*data' set to data.  NOTE: Caller must check if
 * rest of data is in buffer.  Updates last_at, last_end.
 */
{
  static CONST char     fn[] = "kdbf_getbuf";
  size_t                bsz, hsz, rsz = (size_t)(-1), rcsz;
  byte                  *buf, *rcbuf;
  int                   atinbuf;
  EPI_OFF_T             rcat;

  /* `atinbuf': true if `at' is within valid part of buffer: */
  atinbuf = (at >= df->rdbufoff && at < df->rdbufoff+(EPI_OFF_T)df->rdbufused);

  if (!atinbuf)
    {                                           /* outside current buffer */
    readit:
      /* Normally try to read at `at' into all of `rdbuf': */
      rcat = at;
      rcbuf = df->rdbuf;
      rcsz = df->rdbufsz;
      /* KNG 20070424 But if `atinbuf', we can "recycle" the last part
       * of the buffer (at `at') back to the start, instead of re-reading it.
       * May save an lseek() and part of a read(), at the cost of a
       * memmove() equal to the read() savings:
       * wtf KNG 20070502 Bug 1772: realloc buffer now if we know the
       * final (user-data) size of the recycled part, to save alloc later.
       */
      if (atinbuf)                              /* can recycle part of buf */
        {
          /* Set `buf'/`bsz' to remaining (valid) buffer/size at `at': */
          buf = df->rdbufstart + (size_t)(at - df->rdbufoff);
          bsz = df->rdbufused - (size_t)(at - df->rdbufoff);
          /* Move that buffer to start of `rdbuf': */
          MEMMOVE(df, df->rdbuf, buf, bsz);
          /* And skip it during kdbf_readchunk(): */
          rcat += bsz;
          rcbuf += bsz;
          rcsz -= bsz;
        }
      else
        bsz = 0;                                /* no recycle data */
      rsz = kdbf_readchunk(df, rcat, rcbuf, rcsz);
      if (rsz == (size_t)(-1))                  /* error */
        {
          df->lasterr = (errno ? errno : -1);
          goto err;
        }
      if (rsz == 0) goto err;                   /* EOF */
      rsz += bsz;                               /* include recycling */
      df->rdbufused = rsz;
      df->rdbufstart = df->rdbuf;
      df->rdbufoff = at;
    }
  /* Set `buf'/`bsz' to remaining buffer/size at `at': */
  buf = df->rdbufstart + (size_t)(at - df->rdbufoff);
  bsz = df->rdbufused - (size_t)(at - df->rdbufoff);
  if (bsz <= sizeof(KDBF_START)) goto chkreadit;/* possible start pointers */
  hsz = kdbf_proc_head(buf, bsz, at, trans);
  if (hsz == (size_t)(-1))                      /* corrupt header */
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn, CorruptBlkHdr, (EPI_HUGEUINT)at,
                     df->fn);
      df->lasterr = EIO;
      goto err;
    }
  if (hsz == 0) goto chkreadit;                 /* truncated header */
  if (hsz + trans->used > bsz)                  /* truncated block data */
    {
      if (hsz + trans->used < df->rdbufsz)      /* it'll fit */
        {
        chkreadit:
          if (rsz < df->rdbufsz && rsz != (size_t)(-1))
            {                                   /* already read to EOF */
              txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                             (EPI_HUGEUINT)at, df->fn,
                             "Truncated header or data block");
              df->flags |= KDF_BADSTART;
              goto err;
            }
          goto readit;
        }
      /* else caller must check */
    }
  df->last_at = trans->at;
  df->last_end = trans->end;
  df->lastBlkSz = trans->size;
  *data = buf + hsz;
  return(trans->used);

err:
  /* Clear (invalidate) any data in the read buffer: */
  df->rdbufstart = df->rdbuf;
  df->rdbufused = 0;
  df->rdbufoff = (EPI_OFF_T)0;
  /* return nothing/error: */
  *data = BYTEPN;
  return((size_t)(-1));
}

static void *
TXkdbfGetInternal(KDBF *df, EPI_OFF_T at,
                  size_t *psz, /* (out) payload size of block */
                  int forAget  /* (in) nonzero: optimize for kdbf_aget() */)
/* Get a disk block from the kdbf: returns a pointer to the memory
 * that contains the block.  This memory is volatile, and must be copied
 * to be preserved across future kdbf calls.  Pointer is guaranteed
 * to be aligned on ALIGN_SIZE (sizeof(void *)) boundary.
 * Buffer returned is guaranteed to be safe until next (normal) kdbf_get(),
 * kdbf_aget(), or kdbf_close().  This is for backwards compatibility
 * and depending on it is not recommended. -KNG 950914
 * (buf now ok through kdbf_put()/kdbf_alloc() too  KNG 970808)
 * KDBF_IOCTL_READBUFSZ applies.
 * Internal version of kdbf_get(), for use by KDBF.
 */
{
  static const char     fn[] = "TXkdbfGetInternal";
  KDBF_TRANS            trans;
  byte                  *ptr, *ptr2;
  size_t                dsz;
  EPI_OFF_T             oat = at;
  void                  *ret;
  TXTK_TRACE_VARS;

  df->callDepth++;
  TXTK_TRACE_BEFORE_START(df, TXTK_READ)
    txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                   TXTK_INITFMT ", offset " TXTK_OFFFMT ") starting",
                   TXTK_INITARGS(df), TXTK_OFFARGS(oat));
    /* No before-read data: size unknown */
  TXTK_TRACE_BEFORE_FINISH()

  df->lasterr = 0;
  df->kreads++;

  /* Not allowed during B-tree ops, since it will change the buffer
   * and thus the data from a previous (normal) get.  B-tree uses kdbf_read():
   */
  if (df->in_btree)
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn, InvalidFunctionWhileInFreeTreeFmt,
                     df->fn);
      df->lasterr = -1;
      goto err;
    }
  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);
      df->lasterr = EPERM;
      goto err;
    }

  *psz = 0;

  if (df->rdbufsz > 0)                          /* read buffering */
    {                                           /* see also seek_block() */
      if (at == (EPI_OFF_T)(-1L))               /* next-block */
        {
          if (df->last_end == (EPI_OFF_T)(-2L)) /* KDBF_IOCTL_SEEKSTART */
            {
              if (kdbf_getbuf(df, (EPI_OFF_T)0, &trans, &ptr) == (size_t)(-1))
                goto err;
            }
          if (df->last_end < (EPI_OFF_T)0L)     /* last op was error */
            {
              df->lasterr = -1;
              goto err;
            }
          at = df->last_end;
          if ((dsz = kdbf_getbuf(df, at, &trans, &ptr)) == (size_t)(-1))
            goto err;
          /* Keep looking if it's a free block or a free-tree page: */
          while (trans.used == 0 || (trans.type & KDBF_FREEBITS))
            {
              at = trans.end;
              if ((dsz = kdbf_getbuf(df, at, &trans, &ptr)) == (size_t)(-1))
                goto err;
            }
        }
      else
        {
          if ((dsz = kdbf_getbuf(df, at, &trans, &ptr)) == (size_t)(-1))
            goto err;
        }
      if ((df->in_btree ? 1 : 0) ^ (trans.type & KDBF_FREEBITS ? 1 : 0))
        {
          txpmbuf_putmsg(df->pmbuf, MERR, fn,
       "Seek to %sfree-tree block 0x%wx while %sin free-tree in KDBF file %s",
                         (trans.type & KDBF_FREEBITS ? "" : "non-"),
                         (EPI_HUGEUINT)at, (df->in_btree ? "" : "not "),
                         df->fn);
          df->lasterr = EPERM;
          goto err;
        }
      if (dsz == 0) goto err;           /* free block */
      if (ptr + dsz <= df->rdbufstart + df->rdbufused)  /* completely fit */
        {
          ptr2 = (byte *)KDBF_ALIGN_DN(ptr);
          if (ptr2 != ptr)
            {
              MEMMOVE(df, ptr2, ptr, dsz);      /* align our block down */
              /* Invalidate (for future gets) the read buffer before original
               * end-of-block: was memmoved, and thus corrupt/out-of-sync:
               */
              df->rdbufoff += (EPI_OFF_T)((ptr + dsz) - df->rdbufstart);
              df->rdbufused -= ((ptr + dsz) - df->rdbufstart);
              df->rdbufstart = ptr + dsz;
            }
          df->kreadbytes += (EPI_HUGEUINT)dsz;
          *psz = dsz;
          ret = ptr2;
          goto done;
        }
      /* Block data too large for `rdbuf'.  Optimization: we can copy what
       * we have over to main `blk', and avoid seek_block() re-read.
       * WTF merge `rdbuf' and `blk' someday to avoid this kind of memcpy
       * (Bug 1772).
       */
      if (TXkdbfOptimize & 0x4)
        {
          /* Alloc buf to full size we know we'll need for
           * TXkdbfReadRestOfBlock():
           */
          if (!TXkdbfAllocBuf(df, trans.used + KDBF_ALIGN_SIZE, 0)) goto err;
          /* Copy to aligned location, hopefully to avoid a memmove() below.
           * But well into `blk' for safety in case it does happen below:
           */
          df->blk_data = (byte *)df->blk + KDBF_ALIGN_SIZE;
          df->blk_data_sz = (df->rdbufstart + df->rdbufused) - ptr;
          /* Sanity check for memcpy(); should already be large enough: */
          if (df->blk_data_sz > df->blksz - KDBF_ALIGN_SIZE)
            df->blk_data_sz = df->blksz - KDBF_ALIGN_SIZE;
          MEMCPY(df, df->blk_data, ptr, df->blk_data_sz);
          /* wtf update `df->last_sz'/`df->ls_cur'? */
          df->last_at = trans.at;               /* seek_block() would */
          df->last_end = trans.end;             /*   normally set these */
          df->lastBlkSz = trans.size;
          goto skipseek;
        }
    }

  /* 0L means first block for normal reads.  We know it's not a free-tree
   * page since we never allow offset 0L when creating them.  -KNG
   */
  if (!seek_block(df, at, &trans)) goto err;
skipseek:
  if (trans.used == 0) {
#if defined(KDBF_PEDANTIC)
    /* Only warn about this if pedantic, for backwards compatibility
     * with fdbf.  -KNG 951012
     */
    txpmbuf_putmsg(df->pmbuf, MWARN, fn,
                   "Attempt to read free block 0x%wx from KDBF file %s",
                   (EPI_HUGEUINT)at, df->fn);
#endif
    goto err;
  }
  if (!TXkdbfReadRestOfBlock(df, &trans, forAget))
    goto err;                                   /* EOF or error */

  *psz = trans.used;
  df->last_at = trans.at;
  df->last_end = trans.end;
  df->lastBlkSz = trans.size;

  /* Check alignment and adjust if needed:  -KNG 950912 */
  ptr = (byte *)KDBF_ALIGN_DN((byte *)df->blk_data);
  if (ptr != (byte *)df->blk_data) {
    if (ptr < (byte *)df->blk)
      {
        txpmbuf_putmsg(df->pmbuf, MERR, fn,
               "Invalid alignment of internal buffer used for KDBF file `%s'",
                       df->fn);
        goto err;
      }
    MEMMOVE(df, ptr, df->blk_data, trans.used);
    df->blk_data = ptr;
  }
  df->kreadbytes += (EPI_HUGEUINT)(*psz);
  ret = df->blk_data;
  goto done;

err:
  ret = NULL;
done:
  TXTK_TRACE_AFTER_START(df, (TXTK_READ | TXTK_READ_DATA))
    if (TXtraceKdbf & TXTK_READ)
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                     TXTK_INITFMT ", offset " TXTK_OFFFMT
                     "): %1.3kf sec returned offset " TXTK_OFFFMT
                     " %wd bytes %s", TXTK_INITARGS(df), TXTK_OFFARGS(oat),
                     TXTK_TRACE_TIME(), TXTK_OFFARGS(df->last_at),
                     (EPI_HUGEINT)(*psz), (ret != NULL ? "ok" : "ERROR"));
    if ((TXtraceKdbf & TXTK_READ_DATA) && ret != NULL && *psz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER,
                    CHARPN, ret, *psz, 1);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

void *
kdbf_get(KDBF *df, EPI_OFF_T at, size_t *psz)
{
  return(TXkdbfGetInternal(df, at, psz, 0));
}

void *
kdbf_aget(df, at, psz)
KDBF            *df;
EPI_OFF_T       at;
size_t          *psz;   /* (out) size of returned block (not including nul) */
/* Like kdbf_get(), but pointer returned is malloc'd block; caller
 * owns it and must free it when done.  Block is nul-terminated
 * (not counted in `*psz').
 */
{
  static CONST char     fn[] = "kdbf_aget";
  void                  *blk, *ret;

  /* No tracing: handled by kdbf_get() call */

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);
      goto err;
    }

  /* kdbf_get() optimized for kdbf_agent(); tries to make `df->blk'
   * aligned with start of payload:
   */
  blk = TXkdbfGetInternal(df, at, psz, 1);
  if (!blk) goto err;

  /* See if we can avoid dup and just steal `df->blk': */
  if (blk == df->blk && *psz + 1 <= df->blksz)
    {
      df->blk = df->blk_data = NULL;            /* stealing `df->blk' */
      df->blksz = df->blk_data_sz = 0;
      /* Downsize to just what is needed: */
      ret = REALLOC(fn, df, blk, *psz + 1);
      if (!ret)
        {
#ifdef EPI_REALLOC_FAIL_SAFE
          ret = blk;
#else /* EPI_REALLOC_FAIL_SAFE */
          goto err;
#endif /* EPI_REALLOC_FAIL_SAFE */
        }
    }
  else
    {
      if ((ret = MALLOC(fn, df, *psz + 1)) == NULL)
        goto err;
      MEMCPY(df, ret, blk, *psz);
    }
  *((char *)ret + *psz) = '\0';
  goto done;

err:
  ret = NULL;
done:
  return(ret);
}

size_t
kdbf_readchunk(df, at, buf, sz)
KDBF            *df;    /* the handle */
EPI_OFF_T       at;     /* where to start reading from */
byte            *buf;   /* your buffer */
size_t          sz;     /* raw size to read */
/* Reads up to `sz' bytes at `at' into `buf', directly.  Use kdbf_nextblock()
 * to loop over the buffer and get pointer/sizes of each actual KDBF
 * block.  Used to read a lot of adjacent KDBF blocks together in one read,
 * when we're the only user of the file.  Returns raw byte count read,
 * or 0 on EOF, or -1 on error.  Note: caller must check for buf too small,
 * i.e. if kdbf_nextblock() returns 0 on first call, enlarge buf and re-read.
 * `at' does not have to point to a KDBF header, though the buffer given
 * to kdbf_nextblock() does.  `sz' should be at least KDBF_MIN_READCHUNK_SZ.
 */
{
  static CONST char     fn[] = "kdbf_readchunk";
  size_t                rd;
  char                  errBuf[KDBF_ERR_BUFSZ];

  /* WTF TRACE */

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);
      goto err;
    }

  /* Don't allow read-next; `at' must be a known location: */
  if (at < (EPI_OFF_T)0L)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
                     "Bad parameter (offset 0x%wx) for KDBF file %s",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
  if (sz < sizeof(KDBF_START) + KDBF_HMAXSIZE)
    {
      BADBUFSIZE(df, sz);
      goto err;
    }

  CLEAR_ERR();
  if (kdbf_raw_lseek(df, at, SEEK_SET) < (EPI_OFF_T)0L)
    {
    corr:
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                     (EPI_HUGEUINT)at, df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      goto err;
    }
  rd = kdbf_raw_read(df, buf, sz, -1);
  if (rd < sz)                                  /* partial read: EOF? */
    {
      if (kdbf_raw_lseek(df, 0, SEEK_END) != at + (EPI_OFF_T)rd) goto corr;
      if (rd <= sizeof(KDBF_START)) rd = 0;     /* EOF */
    }
  return(rd);  

err:
  return((size_t)(-1));
}

size_t
kdbf_nextblock(df, at, buf, bsz, data, dat, dtot)
KDBF            *df;
EPI_OFF_T       *at;
byte            **buf;
size_t          *bsz;
byte            **data;
EPI_OFF_T       *dat;
size_t          *dtot;
/* Gets the next non-empty KDBF block in buffer `*buf' of `*bsz'
 * bytes, originally read by kdbf_readchunk().  Sets `*data' to start
 * of block data, and returns data size or 0 on buf end or -1 on error.
 * `*buf' and `*bsz' are advanced for next call; may be set to NULL/0
 * if current block ok but next block is EOF.  `*at' is assumed to
 * be file offset, and is advanced on successful "reads" to next block.
 * `df' is used just for the filename on errors.  NOTE: `*data' is not
 * guaranteed to be aligned.  Sets `*dat' to file offset where `*data'
 * is, or -1 if no data returned.  Sets `*dtot' to total (used) data
 * size of block; could be less than returned data size if block is
 * truncated (end of buffer, `*at' is NOT advanced) or 0 if no data returned.
 */
{
  static CONST char     fn[] = "kdbf_nextblock";
  KDBF_TRANS    trans;
  size_t        hsz, ret;

  /* WTF TRACE */

  df->kreads++;

  do
    {
      if (*bsz <= sizeof(KDBF_START)) goto eob; /* possible start pointers */
      hsz = kdbf_proc_head(*buf, *bsz, (EPI_OFF_T)0, &trans);
      if (hsz == (size_t)(-1))                  /* corrupt header */
        {
          txpmbuf_putmsg(df->pmbuf, MERR, fn, CorruptBlkHdr,
                         (EPI_HUGEUINT)(*at), df->fn);
          goto err;
        }
      if (hsz == 0) goto eob;                   /* truncated header */
      *data = *buf + hsz;
      *dtot = ret = trans.used;
      if (*bsz < hsz + trans.used)              /* truncated data */
        ret = *bsz - hsz;
      if (*bsz < hsz + trans.size)              /* truncated next block */
        {
          *buf = BYTEPN;
          *bsz = 0;
        }
      else
        {
          *buf += hsz + trans.size;
          *bsz -= hsz + trans.size;
        }
      *dat = *at;
      *at += hsz + trans.size;
    }
  while (trans.used == 0 || (trans.type & KDBF_FREEBITS));
  df->kreadbytes += (EPI_HUGEUINT)ret;
  if (ret < *dtot) *at = *dat;                  /* truncated data */
  return(ret);
err:
  *buf = *data = BYTEPN;
  *bsz = *dtot = 0;
  *dat = (EPI_OFF_T)(-1);
  return((size_t)(-1));
eob:
  *buf = *data = BYTEPN;
  *bsz = *dtot = 0;
  *dat = (EPI_OFF_T)(-1);
  return(0);
}

size_t
kdbf_read(df, at, off, buf, sz)
KDBF            *df;    /* the handle */
EPI_OFF_T       at;     /* where to start reading from */
size_t          *off;   /* optional offset into block */
void            *buf;   /* your buffer */
size_t          sz;     /* how many bytes you want */
/* Reads `sz' bytes of block `at' directly into `buf'.  Saves a little
 * time when you know the block size already.  Doesn't muck with the
 * buffer used by kdbf_get().  Returns 0 on error.  If `off' is NULL,
 * reads at start of block, and _total_ block data size is returned.
 * If `off' is non-NULL, reads at offset `*off' from start of block
 * and sets `*off' to offset + sz (plus header size), e.g. to continue
 * at next call.  If `*off' is 0, total block data size is returned;
 * if non-zero, size of data read.  NOTE: if `*off' > 0, reading
 * beyond end of block is possible; header is not checked.  NOTE:
 * KDBF_IOCTL_PREBUFSZ applies to `buf' if `off' is NULL or 0:
 * saves a separate header read.  NOTE: KDBF_IOCTL_POSTBUFSZ applies
 * if free-tree read: saves checksum read.
 */
{
  static CONST char     fn[] = "kdbf_read";
  KDBF_TRANS	trans;
  byte		*s, *d, *se;
  size_t	bds, prebufsz, postbufsz, hsz, ahsz, rsz, ret, ooff, osz = sz;
  EPI_OFF_T             oat = at;
  char                  errBuf[KDBF_ERR_BUFSZ];
  TXTK_TRACE_VARS;

  df->callDepth++;
  ooff = (off != SIZE_TPN ? *off : (size_t)0);
  prebufsz = (df->in_btree ? df->btree_prebufsz : df->prebufsz);
#ifdef KDBF_CHECK_CHECKSUMS
  postbufsz = (df->in_btree ? df->btree_postbufsz : 0);
  if (postbufsz < sizeof(KDBF_CHKSUM)) postbufsz = 0;
  else postbufsz = sizeof(KDBF_CHKSUM);
#else /* !KDBF_CHECK_CHECKSUMS */
  postbufsz = 0;
#endif /* !KDBF_CHECK_CHECKSUMS */

  TXTK_TRACE_BEFORE_START(df, (TXTK_READ | TXTK_READ_DATA))
    if (TXtraceKdbf & TXTK_BEFORE(TXTK_READ))
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                     TXTK_INITFMT ", offset " TXTK_OFFFMT
                     " + %wd, %wd bytes) starting", TXTK_INITARGS(df),
                     TXTK_OFFARGS(oat), (EPI_HUGEINT)ooff, (EPI_HUGEINT)osz);
    if ((TXtraceKdbf & TXTK_BEFORE(TXTK_READ_DATA)) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                    (byte *)buf + (ooff ? prebufsz : (size_t)0), sz, 1);
  TXTK_TRACE_BEFORE_FINISH()

  df->kreads++;

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);
      goto err;
    }

  /* Don't allow read-next; `at' must be a known location to avoid
   * seek_block() and thus preserve main buffer:
   */
  if (at < (EPI_OFF_T)0L)
    {
      txpmbuf_putmsg(df->pmbuf, MERR + UGE, fn,
                     "Bad parameter (offset 0x%wx) for KDBF file %s",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
  if (sz <= (size_t)0 || sz > KDBF_MAX_ALLOC)
    {
      BADBLKSIZE(df, sz);
      goto err;
    }

  if (off != (size_t *)NULL && *off > 0)                /* read at offset */
    {
      at += (EPI_OFF_T)(*off);
      CLEAR_ERR();
      if (kdbf_raw_lseek(df, at, SEEK_SET) < (EPI_OFF_T)0L)
        {
        seekerr:
          txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CorruptOperation,
                         (EPI_HUGEUINT)at, df->fn,
                         kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          goto err;
        }
      bds = kdbf_raw_read(df, buf, sz, -1);
      *off += bds;
      df->kreadbytes += (EPI_HUGEUINT)bds;
      ret = bds;
      goto done;
    }

  /* We could try to read in exactly the right amount from the header,
   * since we know the used portion, but block _size_ could be larger
   * and thus the header size would be greater.  That should only happen
   * rarely, but avoid it by reading in max-size header as usual:
   *   -KNG 950915
   * kdbf_read() blocks are mostly B-tree and Metamorph .dat blocks that
   * haven't changed size, so accurate prediction of header size is possible.
   * Use prebufsz to save a read:  -KNG 000302
   */
  rsz = sz + postbufsz;
  if (prebufsz > 0)
    {
      int       headerType;

      headerType = kdbf_header_type(rsz);
      if (headerType < 0) goto err;          
      hsz = kdbf_header_size(headerType);
      if (prebufsz < hsz) goto norm;            /* header will not fit */
      CLEAR_ERR();
      if (kdbf_raw_lseek(df, at, SEEK_SET) < (EPI_OFF_T)0) goto seekerr;
      s = (byte *)buf + (prebufsz - hsz);
      CLEAR_ERR();
      bds = kdbf_raw_read(df, s, hsz + rsz, -1);
      ahsz = (size_t)kdbf_proc_head(s, bds, at, &trans);
      if (ahsz == (size_t)(-1))             /* corrupt header */
        {
        corr:
          txpmbuf_putmsg(df->pmbuf, MERR, fn, CorruptBlkHdr, (EPI_HUGEUINT)at,
                         df->fn);
          goto err;
        }
      if (ahsz == 0)                            /* truncated header: */
        {                                       /*   need to read more */
          if (bds < hsz + rsz) goto corr;       /* we read all there is */
          goto norm;                            /* try it normally */
        }
      if (sz > trans.used)                      /* they requested too much */
        {
          rsz -= (sz - trans.used);
          sz = trans.used;
        }
      if (ahsz != hsz)                          /* align data */
        MEMMOVE(df, (byte *)buf + prebufsz, s + ahsz, bds - ahsz);
      if (ahsz + rsz > bds)                     /* short read */
        {
          if (ahsz > hsz)                       /* we read short */
            {
              CLEAR_ERR();
              if (kdbf_raw_read(df, s + bds - (ahsz - hsz),
                                (ahsz + rsz) - bds, -1) == (ahsz + rsz) - bds)
                goto checkit;
            }
          txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
                       "Cannot read 0x%wx bytes at 0x%wx in KDBF file %s: %s",
                         (EPI_HUGEUINT)ahsz + (EPI_HUGEUINT)rsz,
                         (EPI_HUGEUINT)at, df->fn,
                         kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
          goto err;
        }
      goto checkit;
    }

norm:
  if (!read_head(df, at, &trans, RH_HDRONLY) ||
      ((df->in_btree ? 1 : 0) ^ (trans.type & KDBF_FREEBITS ? 1 : 0))) {
    if (df->in_btree) {
      /* Since B-tree may not check return code of reads, warn about it:
       *   -KNG 950914
       */
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Bad free-tree page header at 0x%wx in KDBF file %s",
                     (EPI_HUGEUINT)at, df->fn);
      MEMSET(df, (byte *)buf + prebufsz, 0, rsz);  /* try to avoid garbage */
    } else {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Bad block header at 0x%wx in KDBF file %s",
                     (EPI_HUGEUINT)at, df->fn);
    }
    goto err;
  }
  if (sz > trans.used)                          /* they requested too much */
    {
      rsz -= (sz - trans.used);
      sz = trans.used;
    }

  /* Copy over excess (start of data) to buffer.  Inline for speed: */
  s = df->blk_data;
  bds = df->blk_data_sz;
  se = s + (bds < rsz ? bds : rsz);
  d = (byte *)buf + prebufsz;
  while (s < se) *d++ = *s++;
  /* Read the rest, if needed: */
  if (bds < rsz) {
    CLEAR_ERR();
    if (kdbf_raw_read(df, d, rsz - bds, -1) != rsz - bds) {
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
               "Cannot read 0x%wx bytes just after 0x%wx in KDBF file %s: %s",
                     (EPI_HUGEUINT)(rsz - bds), (EPI_HUGEUINT)at, df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      goto err;
    }
  }

checkit:
#ifdef KDBF_CHECK_CHECKSUMS
  if (df->in_btree) {
    /* Read and check the checksum: */
    KDBF_CHKSUM	chk;
    if (sz != trans.used ||
        trans.size != trans.used + sizeof(chk))
      goto corrfree;
    if (postbufsz > 0)
      MEMCPY(df, &chk, (byte *)buf + prebufsz + trans.used, sizeof(chk));
    else
      {
        CLEAR_ERR();
        if (kdbf_raw_read(df, &chk, sizeof(chk), -1) != (size_t)sizeof(chk))
          {
            txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn,
    "Cannot read free-tree page 0x%wx checksum at +0x%wx in KDBF file %s: %s",
                           (EPI_HUGEUINT)at, (EPI_HUGEUINT)sz, df->fn,
                           kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
            goto err;
          }
      }
    if (chk.offset != trans.at ||
	chk.chksum != kdbf_checksum_block((byte *)buf + prebufsz, sz)) {
    corrfree:
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
     "Corrupt free-tree page at 0x%wx in KDBF file %s (bad checksum or size)",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
  }
#endif /* KDBF_CHECK_CHECKSUMS */

  df->last_at = trans.at;
  df->last_end = trans.end;
  df->lastBlkSz = trans.size;
  if (off != (size_t *)NULL)                    /* and *off == 0 */
    *off = (size_t)kdbf_header_size(trans.type) + sz;
  df->kreadbytes += trans.used;
  ret = trans.used;
  goto done;

err:
  ret = 0;
done:
  TXTK_TRACE_AFTER_START(df, (TXTK_READ | TXTK_READ_DATA))
    if (TXtraceKdbf & TXTK_READ)
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                     TXTK_INITFMT ", offset " TXTK_OFFFMT
                     " + %wd, %wd bytes): %1.3kf sec returned offset "
                     TXTK_OFFFMT " %wd bytes %s",
                     TXTK_INITARGS(df), TXTK_OFFARGS(oat), (EPI_HUGEINT)ooff,
                     (EPI_HUGEINT)osz, TXTK_TRACE_TIME(),
                     TXTK_OFFARGS(df->last_at), (EPI_HUGEINT)ret,
                     (ret ? "ok" : "ERROR"));
    if ((TXtraceKdbf & TXTK_READ_DATA) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                    (byte *)buf + (ooff ? prebufsz : (size_t)0), sz, 1);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(ret);
}

EPI_OFF_T
kdbf_put(df, at, buf, sz)
KDBF            *df;
EPI_OFF_T       at;
void            *buf;
size_t          sz;
/* Puts a block of memory into the kdbf file: If `at' is -1 then it
 * will allocate a new block on disk.  If `sz' of data will not fit in
 * the existing block then it will free that block and allocate a new
 * one.  Don't ignore the return value because it is not guaranteed
 * that a disk block will not move on a put operation unless the size of
 * the block is the same as the one on disk, and even then: "stuff
 * happens".  The return value is a handle to new disk block, or -1L
 * if error.  Don't try to directly read or write to this file. You'll
 * trash it.
 */
{
  static CONST char     fn[] = "kdbf_put";
  KDBF_TRANS            trans;
  size_t                pad = 0, prebufsz;
  EPI_OFF_T             oldat, oat = at;
  TXTK_TRACE_VARS;

  df->callDepth++;
  prebufsz = (df->in_btree ? df->btree_prebufsz : df->prebufsz);

  TXTK_TRACE_BEFORE_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE))
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE, CHARPN,
                     TXTK_INITFMT ", offset " TXTK_OFFFMT
                     ", %wd bytes) starting",
                     TXTK_INITARGS(df), TXTK_OFFARGS(oat), (EPI_HUGEINT)sz);
    if ((TXtraceKdbf & TXTK_BEFORE(TXTK_WRITE_DATA)) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_BEFORE,
                    CHARPN, (byte *)buf + prebufsz, sz, 1);
  TXTK_TRACE_BEFORE_FINISH()

  df->lasterr = 0;
  if (sz <= 0 || sz > KDBF_MAX_ALLOC)
    {
      BADBLKSIZE(df, sz);
      df->lasterr = EINVAL;                     /* bogus size */
      goto err;
    }
  if (df->flags & (KDF_READONLY|KDF_BADSTART))
    {
      if (df->flags & KDF_READONLY)
        {
          txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteRdOnly, df->fn);
          df->lasterr = EPERM;
        }
      if (df->flags & KDF_BADSTART)
        txpmbuf_putmsg(df->pmbuf, MERR + FWE, fn, CantWriteCorrupt, df->fn);
      goto err;
    }

#ifdef KDBF_CHECKSUM_FREETREE
  if (df->in_btree) {
    /* Make sure size can hold KDBF_CHKSUM too.  Done separately in
     * kdbf_alloc() since size = used + sizeof(KDBF_CHKSUM):
     */
    pad = sizeof(KDBF_CHKSUM);
  }
#endif

  if (at == (EPI_OFF_T)(-1L)) {	/* -1 means allocate a new one */
    at = kdbf_alloc(df, buf, sz);
    goto done;                          /* df->last_at updated by alloc */
  } 

  if (df->flags & KDF_APPENDONLY)
    {
      bad_append(df, at, fn);   /* causes seek write */
      df->lasterr = EINVAL;
      goto err;
    }

  if (!read_head(df, at, &trans, RH_HDRONLY))
    {
      df->lasterr = (errno ? errno : -1);
      goto err;
    }
  if (trans.size < sz + pad) {		/* block too small; make a new one */
    oldat = at;
    if ((at = kdbf_alloc(df, buf, sz)) < (EPI_OFF_T)0L) /* allocate new one */
      goto err;
    /* only free the old one if it's not already free:  KNG 980129 */
    if (trans.used != 0) kdbf_free(df, oldat);
  } else {				/* the existing block is big enough */
    trans.used = sz;
    df->kwrites++;
    df->kwritebytes += (EPI_HUGEUINT)sz;
    if (write_block(df, &trans, buf, (EPI_OFF_T)(-1L), KDBF_STARTPPN) <
        (EPI_OFF_T)0L)
      {
        df->lasterr = (errno ? errno : -1);
        goto err;
      }
    df->last_at = trans.at;
    df->last_end = trans.end;
    df->lastBlkSz = trans.size;
  }
  goto done;

err:
  at = (EPI_OFF_T)(-1L);
done:
  TXTK_TRACE_AFTER_START(df, (TXTK_WRITE | TXTK_WRITE_DATA))
    if (TXtraceKdbf & TXTK_WRITE)
      txpmbuf_putmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                     TXTK_INITFMT ", offset " TXTK_OFFFMT
                     ", %wd bytes): %1.3kf sec returned offset "
                     TXTK_OFFFMT " %s", TXTK_INITARGS(df), TXTK_OFFARGS(oat),
                     (EPI_HUGEINT)sz, TXTK_TRACE_TIME(), TXTK_OFFARGS(at),
                     (at != (EPI_OFF_T)(-1L) ? "ok" : "ERROR"));
    if ((TXtraceKdbf & TXTK_WRITE_DATA) && sz > (size_t)0)
      tx_hexdumpmsg(TXtraceKdbfPmbuf, TXTRACEKDBF_MSG_AFTER, CHARPN,
                    (byte *)buf + prebufsz, sz, 1);
  TXTK_TRACE_AFTER_FINISH()
  df->callDepth--;
  return(at);
}

/* ----------------------------------------------------------------------- */
/* Internal public functions: */

DBF *
kdbf_pseudo_closedbf(dbf)
DBF     *dbf;
/* Closes fake DBF struct used by B-tree stuff.
 */
{
  static const char     fn[] = "kdbf_pseudo_closedbf";
  KDBF	                *df;

  if (!dbf) return(NULL);
  df = (KDBF *)dbf->obj;
  if (df->pseudo_dbf != dbf)            /* really should be same */
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                     "Internal error: pseudo DBF != DBF for KDBF file `%s'",
                     df->fn);
      /* ok to continue? */
    }
  /* Clear in-btree versions of pre/postbufsz, so that they are default-unset
   * on next kdbf_pseudo_opendbf(), just like an ordinary opendbf().
   * It is up to kdbf_openfbtree() to set pre/postbufsz every time if desired:
   */
  df->btree_prebufsz = df->btree_postbufsz = 0;
  df->in_btree -= 1;
  /* Invalidate start pointers, since we cannot guarantee atomicity anymore:*/
  /* kdbf_zap_start(df); */             /* now done by caller 970807 */
  return(NULL);
}

DBF *
kdbf_pseudo_opendbf(df)
KDBF    *df;
/* Produces fake DBF struct that points to already open KDBF struct.
 * For use by B-tree code when dealing with free-tree.
 */
{
  static CONST char     fn[] = "kdbf_pseudo_opendbf";
  DBF                   *dbf;

  if (!df->pseudo_dbf) {
    if ((dbf = (DBF *)CALLOC(fn, df, 1, sizeof(DBF))) == NULL) {
      return(NULL);
    }
    dbf->obj = df;
    dbf->close = NULL;	/* pseudo_closedbf() used */
    dbf->dbfree = (int (*) ARGS((void *, EPI_OFF_T)))kdbf_free;
    dbf->alloc = (EPI_OFF_T (*) ARGS((void *, void *, size_t)))kdbf_alloc;
    dbf->put = (EPI_OFF_T (*) ARGS((void *, EPI_OFF_T, void *, size_t)))kdbf_put;
    dbf->get = (void *(*) ARGS((void *, EPI_OFF_T, size_t *)))kdbf_get;
    dbf->aget = (void *(*) ARGS((void *, EPI_OFF_T, size_t *)))kdbf_aget;
    dbf->read=(size_t(*)ARGS((void *,EPI_OFF_T,size_t *,void *,size_t)))kdbf_read;
    dbf->tell = (EPI_OFF_T (*) ARGS((void *)))kdbf_tell;
    dbf->getfn = (char *(*) ARGS((void *)))kdbf_getfn;
    dbf->getfh = (int (*) ARGS((void *)))kdbf_getfh;
    dbf->setoveralloc = (void (*) ARGS((void *, int)))kdbf_setoveralloc;
    dbf->valid = (int (*) ARGS((void *, EPI_OFF_T)))kdbf_valid;
#ifndef NO_DBF_IOCTL
    dbf->ioctl = (int (*)ARGS((void *, int, void *)))kdbf_ioctl;
    dbf->dbftype = DBF_KAI;
#endif /* !NO_DBF_IOCTL */
    df->pseudo_dbf = dbf;
  }
  df->in_btree += 1;				/* recursion flag */
  /* kdbf_zap_start(df); */                     /* now done by caller 970807 */
  return(df->pseudo_dbf);
}

int
kdbf_put_freetree_root(df, root)
KDBF            *df;
EPI_OFF_T       root;
/* Writes free-tree start point `root' to `df'.  Returns 1 if ok, 0 if error.
 */
{
  static const char     fn[] = "kdbf_put_freetree_root";

  /* WTF TRACE */
  if (!df->in_btree)                            /* KNG 970805 */
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
     "Invalid function call while not processing free-tree of KDBF file `%s'",
                     df->fn);
      return(0);
    }
  df->start.btree = root;
  return(write_start_ptrs(df));
}

void *
kdbf_pseudo_calloc(df, pbuf, n)
KDBF	*df;
void	**pbuf;
size_t	n;
/* Used to alloc buffers for B-tree, and save them to avoid re-allocing.
 */
{
  static CONST char     fn[] = "kdbf_pseudo_calloc";

  if ((int)df->in_btree != (int)1)      /* buffers cannot be re-entered */
    {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
    "Invalid attempt to (re-)enter free-tree buffer alloc for KDBF file `%s'",
                     df->fn);
      return(NULL);
    }
  if (*pbuf == NULL &&
      (*pbuf = MALLOC(fn, df, n)) == NULL)
    return(NULL);
  MEMSET(df, *pbuf, 0, n);
  return(*pbuf);
}

int
kdbf_traverse_free_pages(df, cb, data)
KDBF			*df;
kdbf_freepage_cb	*cb;
void			*data;
/* Traverses free-page list, calling `cb(at, data)' on each page.
 * Returns 1 if ok, 0 on error.
 */
{
  static CONST char     fn[] = "kdbf_traverse_free_pages";
  byte		*buf = NULL;
  size_t	bufsz;
  EPI_OFF_T		at, next;
  KDBF_CHKSUM	chk;
  KDBF_START	start;
  KDBF_TRANS	trans;
  int		ret = 1;
  char                  errBuf[KDBF_ERR_BUFSZ];

  bufsz = KDBF_HMAXSIZE + sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM);
  if ((buf = (byte *)MALLOC(fn, df, bufsz)) == NULL)
    return(0);
  CLEAR_ERR();
  if (kdbf_raw_lseek(df, -(EPI_OFF_T)sizeof(KDBF_START), SEEK_END) <
      (EPI_OFF_T)0L ||
      kdbf_raw_read(df, &start, sizeof(start), -1) != (size_t)sizeof(start))
    {
      txpmbuf_putmsg(df->pmbuf, MERR + FRE, fn, CantReadPtrs, df->fn,
                     kdbf_strerr(errBuf, KDBF_ERR_BUFSZ));
      goto err;
    }

  for (at = start.free_pages; at != (EPI_OFF_T)0L; at = next) {
    /* Read the header and check it: */
    if (!read_head(df, at, &trans, (RH_HDRONLY | RH_FREEPTR)))
      goto err;
    if (trans.size != KDBF_FREETREE_PGSZ ||
	trans.used != 0 ||
	df->blk_data_sz < sizeof(EPI_OFF_T)) {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                  "Corrupt free page at 0x%wx in KDBF file %s (bad size/len)",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
    MEMCPY(df, &next, df->blk_data, sizeof(next));
#ifdef KDBF_CHECK_CHECKSUMS
    MEMCPY(df, &chk, (byte *)df->blk_data + sizeof(EPI_OFF_T), sizeof(chk));
    if (df->blk_data_sz < sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM) ||
	chk.offset != trans.at ||
	chk.chksum != kdbf_checksum_block(df->blk_data, sizeof(EPI_OFF_T))) {
      txpmbuf_putmsg(df->pmbuf, MERR, fn,
                  "Corrupt free page at 0x%wx in KDBF file %s (bad checksum)",
                     (EPI_HUGEUINT)at, df->fn);
      goto err;
    }
#endif
    /* It's ok; call the callback: */
    cb(at, data);
  }

 done:
  buf = TXfree(buf);
  return(ret);
 err:
  ret = 0;
  goto done;
}
