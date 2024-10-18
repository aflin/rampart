#include "texint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "kdbf.h"
#include "kdbfi.h"
#include "fbtree.h"
#include "kfbtree.h"


/* ----------------------------- FDBF stuff -------------------------------- */
#define FDBF_CHECK(t)	0xA0	/* for kdbfchk, to id FDBF files */
#define FDBF_CHKSUMBITS	0xF0	/* "" */
/* ------------------------------------------------------------------------- */

#if !defined(KDBF_CHECKSUM_FREETREE)
  Must define KDBF_CHECKSUM_FREETREE for free-tree checksums
#endif

#ifdef KDBF_VERBOSE
#  define VERBMSG(x)	putmsg x
#else
#  define VERBMSG(x)
#endif

#ifndef SIGARGS
#  define SIGARGS     ARGS((int sig))
#endif  /* !SIGARGS */
#ifndef SIZE_TPN
#  define SIZE_TPN      ((size_t *)NULL)
#endif

#define PF(a)	(htpf a, fflush(stdout))
#define NEXT_BLK(at)    (((((EPI_OFF_T)(at))/((EPI_OFF_T)512)) + (EPI_OFF_T)1)*(EPI_OFF_T)512)

/* write_block() flags: */
enum {
  WB_ADD	= (1 << 0),
  WB_ORPHAN	= (1 << 1),
  WB_OKTRUNC    = (1 << 2)
};

/* add_free_block/page flags: */
enum {
  AF_ORPHAN	= (1 << 0),
  AF_ROOT	= (1 << 1),
  AF_TRUNC	= (1 << 2)
};

/* Bogus block counts: */
enum {
  CNT_DELALLOCS,	/* deleted (used set to 0) allocated blocks */
  CNT_TRUNCALLOCS,	/* blocks with truncated allocated data */
  CNT_TRUNCSLACKS,	/* allocated blocks w/truncated slack space */
  CNT_BADTREEPAGES,	/* bogus free-tree pages */
  CNT_BADFREEPAGES,	/* bogus free free-tree pages */
  CNT_UNDERFULLPAGES,	/* underfull free-tree pages */
  CNT_TOODEEPPAGES,	/* pages too deep in tree (exceeds B-tree cache) */
  CNT_BADFREES,		/* free blocks with bad headers (includes pages) */
  CNT_TRUNCFREES,	/* truncated free blocks (includes pages) */
  CNT_FREEORPHANS,	/* orphaned free blocks added */
  CNT_PAGEORPHANS,	/* orphaned free-tree pages added */
  CNT_FREEPADS,		/* free pad blocks added */
  CNT_READERRS,         /* unreadable blocks */
  NUM_CNTS
};

static CONST char * CONST CntNames[NUM_CNTS] = {
  "data block%s deleted",
  "data block%s truncated",
  "data block%s had slack space truncated",
  "bad free-tree page%s found",
  "bad free free-tree page%s found",
  "underfull free-tree page%s found",
  "page%s too deep for B-tree cache",
  "bad free block header%s found",
  "free block%s truncated",
  "orphaned free block%s found",
  "orphaned free-tree page%s found",
  "free block pad%s inserted",
  "read error%s",
};

static CONST char       OutOfMem[] = "Cannot alloc %lu bytes of memory: %s";
#define MAERR(f, n) putmsg(MERR + MAE, f, OutOfMem, (long)(n), strerror(errno))

typedef struct {
  BTREE         *treerev, *treefwd;     /* free tree: reverse/forward order */
  long          blks;                   /* count of blocks in tree */
  EPI_OFF_T     freebytes;              /* number of bytes in tree */
  long          cnt[NUM_CNTS];          /* bad block counts */
} KDBF_FINFO;

typedef enum DBACT_tag
{
  DBACT_DEFAULT,
  DBACT_DEL,
  DBACT_PRINT,
  DBACT_LIST,
  DBACT_HDRSCAN,
  DBACT_REPLACE,
  DBACT_NUM                     /* must be last */
}
DBACT;
#define DBACTPN ((DBACT *)NULL)

/* Minimum possible size (including header) of a free-tree page: */
size_t	MinFPgSz = (size_t)0;

static char             *ProgName = CHARPN;
static char             *OutFile = CHARPN;
static char             **InFiles = CHARPPN;
static int              NumInFiles = 0;
static char             *FreeFile = CHARPN;
static char             *BadBlocksFile = CHARPN;
static FILE             *BadBlocksFh = FILEPN;
static BTREE            *TmpTreeRev = BTREEPN, *TmpTreeFwd = BTREEPN;
static long             BadBlockCnt = 0L, BadBlockLimit = 1000L;
#define RDERR_LIMIT     10
static char             *TmpPath = CHARPN;
static int              OutFh = -1, InFh = -1;
static int              IgnoreOrphans = 0;
static int              AssumeKDBF = 0;
static int              PrintOrphans = 1;
static int              SaveTrunc = 0, Overwrite = 0, QuickFix = 0;
static EPI_OFF_T        TheBlk = (EPI_OFF_T)(-1);
static size_t           TheBlkSz = (size_t)0;
static DBACT            What = DBACT_DEFAULT;
static int              Verbosity = 2;
static size_t           BufSz = 128*1024;
static char             *KcReplaceFile = CHARPN;
static int              Align = 1;
static int              MeterType = TXMDT_SIMPLE;


static void CDECL badprintf ARGS((EPI_OFF_T at, size_t sz, char *fmt, ...));
static void CDECL
#ifdef EPI_HAVE_STDARG
badprintf(EPI_OFF_T at, size_t sz, char *fmt, ...)
{
  va_list	argp;

  va_start(argp, fmt);
#else /* !EPI_HAVE_STDARG */
badprintf(va_alist)	/* args(at, sz, fmt, ...) */
va_dcl
{
  va_list	args;
  EPI_OFF_T	at;
  size_t	sz;
  char		*fmt;

  va_start(argp);
  at = va_arg(argp, EPI_OFF_T);
  sz = va_arg(argp, size_t);
  fmt = va_arg(argp, char *);
#endif /* !EPI_HAVE_STDARG */
  BadBlockCnt++;
  if (BadBlocksFh != FILEPN) {
    if (BadBlockCnt == 1L) {
      htfpf(BadBlocksFh,
#if EPI_OFF_T_BITS == 32
            "Offset     Total size     Comments\n           (with hdr)\n"
#else /* EPI_OFF_T_BITS != 32 */                 /* assume 64 for spacing */
  "Offset             Total size     Comments\n                   (with hdr)\n"
#endif /* EPI_OFF_T_BITS != 32 */
	      );
    }
    if (BadBlockLimit == 0L || BadBlockCnt <= BadBlockLimit) {
      htfpf(BadBlocksFh, "0x%wX 0x%04wX  ", (EPI_HUGEINT)at, (EPI_HUGEINT)sz);
      htvfpf(BadBlocksFh, fmt, argp);           /* print message content */
      htfpf(BadBlocksFh, "\n");
    } else if (BadBlockCnt == BadBlockLimit + 1L) {
      htfpf(BadBlocksFh, "Message limit of %ld exceeded\n", BadBlockLimit);
    }
    fflush(BadBlocksFh);
  }
  va_end(argp);
}

static void abendcb ARGS((void *usr));
static void
abendcb(usr)
void    *usr;
/* Do some cleanup on abend (SIGINT, SIGTERM, etc.).
 */
{
  int   fh;
  char  *fname;

  (void)usr;
  if (BadBlocksFh != FILEPN) fflush(BadBlocksFh);
  if (TmpTreeRev != BTREEPN)
    {
      fh = getdbffh(TmpTreeRev->dbf);
      if (fh > STDERR_FILENO) close(fh);                /* for Windows */
      fname = getdbffn(TmpTreeRev->dbf);
      if (fname != CHARPN && fname != TxRamDbfName) unlink(fname);
    }
  if (TmpTreeFwd != BTREEPN)
    {
      fh = getdbffh(TmpTreeFwd->dbf);
      if (fh > STDERR_FILENO) close(fh);                /* for Windows */
      fname = getdbffn(TmpTreeFwd->dbf);
      if (fname != CHARPN && fname != TxRamDbfName) unlink(fname);
    }
  fflush(stdout);
  fflush(stderr);
}

#ifdef FORCE_BROKEN_READ
static int snrubread ARGS((int line, int fd, void *buf, size_t count));
static int
snrubread(line, fd, buf, count)
int     line, fd;
void    *buf;
size_t  count;
{
  static int    cnt = 0;
  /*  637 scan_freetree_pages   ok
   *  775 read_free_pages
   *  874 read_freetree         ok?
   * 1193 copy_blocks hdr       broke if THIS_WILL_HURT_YOU; else ok?
   * 1297 copy_blocks data      ok?
   * 1491 main     is it FDBF   ok?
   * 1528 main     get start    ok?
   */
  if (
      rand() < RAND_MAX/2 &&
      line == 1198) {
    errno = EIO;
    return(-1);
  } else return(read(fd, buf, count));
}
#  define read(fd, buf, n)      snrubread(__LINE__, fd, buf, n)
#endif  /* FORCE_BROKEN_READ */

static size_t kdbfchk_raw_read ARGS((int fd, void *buf, size_t sz));
static size_t
kdbfchk_raw_read(fd, buf, sz)
int     fd;     /* (in) file descriptor to read from */
void    *buf;   /* (out) buffer to read to */
size_t  sz;     /* (in) number of bytes to read */
/* read() wrapper.  Returns 0 on error or EOF, else number of bytes read.
 * NOTE: All direct (file descriptor) reads should go through this function,
 * for eventual/possible lseek()-tracking ala kdbf_raw_lseek().
 */
{
  int           tries;
  size_t        rd, totrd;

  for (totrd = 0; totrd < sz; totrd += rd)
    {
      tries = 0;
      do
        {
          TXseterror(0);
          rd = (size_t)read(fd, (byte *)buf + totrd, sz - totrd);
          tries++;
        }
      while (rd == (size_t)(-1) && tries < 25 && TXgeterror() == TXERR_EINTR);
      if (rd == (size_t)(-1) || rd == 0) break;
    }
  return(totrd);
}

static size_t kdbfchk_raw_write ARGS((int fd, void *buf, size_t sz));
static size_t
kdbfchk_raw_write(fd, buf, sz)
int     fd;     /* (in) file descriptor to write to */
void    *buf;   /* (in) buffer to write from */
size_t  sz;     /* (in) number of bytes to write */
/* write() wrapper.  Returns 0 on error, else number of bytes written.
 * NOTE: All direct (file descriptor) writes should go through this function,
 * for eventual/possible lseek()-tracking ala kdbf_raw_lseek().
 */
{
  int           tries;
  size_t        wr, totwr;

  for (totwr = 0; totwr < sz; totwr += wr)
    {
      tries = 0;
      do
        {
          TXseterror(0);
          wr = (size_t)write(fd, (byte *)buf + totwr, sz - totwr);
          tries++;
        }
      while (wr == (size_t)(-1) && tries < 25 && TXgeterror() == TXERR_EINTR);
      if (wr == (size_t)(-1) || wr == 0) break;
    }
  return(totwr);
}

static void rderr ARGS((KDBF_FINFO *fi, CONST char *fn, EPI_OFF_T at,
                        size_t sz));
static void
rderr(fi, fn, at, sz)
KDBF_FINFO      *fi;
CONST char      *fn;
EPI_OFF_T       at;
size_t          sz;
{
  int   err = errno;

  fi->cnt[CNT_READERRS] += 1;
  if (fi->cnt[CNT_READERRS] <= RDERR_LIMIT)     /* stop msgs after a lot */
    putmsg(MERR + FRE, fn,
           "Cannot read 0x%wX bytes of file at offset 0x%wX: %s",
           (EPI_HUGEUINT)sz, (EPI_HUGEINT)at, strerror(err));
  badprintf(at, sz, "Read error: %s", strerror(err));
}

static int fdbf_proc_head ARGS((byte *buf, size_t len, EPI_OFF_T at,
				KDBF_TRANS *trans));
static int
fdbf_proc_head(buf, len, at, trans)
byte		*buf;
size_t		len;
EPI_OFF_T	at;
KDBF_TRANS	*trans;
/* Like kdbf_proc_head(), for FDBF buffer.  Just checks for FDBF for now.
 */
{
  (void)len;
  memset(trans, 0, sizeof(KDBF_TRANS));
  trans->at = at;
  trans->type = buf[0];

  if ((trans->type & FDBF_CHKSUMBITS) != FDBF_CHECK(trans)) {
    /* header corrupt */
    return(-1);
  }
  return(1);
}

static int rev_cmp ARGS((void *ap, size_t alen, void *bp, size_t blen,
			 void *usr));
static int
rev_cmp(ap, alen, bp, blen, usr)
void	*ap, *bp, *usr;
size_t	alen, blen;
/* Comparision function for searches in the block B-tree.  Reverses
 * sense of comparison so that fbtgetnext() returns _previous_ item.
 */
{
  EPI_OFF_T	a, b;

  (void)alen;
  (void)blen;
  (void)usr;
  /* Do comparison, without assuming that sizeof(EPI_OFF_T) == sizeof(int),
   * or rolling over EPI_OFF_T:
   */
  a = *((EPI_OFF_T *)ap);
  b = *((EPI_OFF_T *)bp);
  if (b < a)
    return(-1);
  else if (b > a)
    return(1);
  else
    return(0);
}

static int get_free_blk ARGS((KDBF_FINFO *fi, EPI_OFF_T off, EPI_OFF_T *blk,
			      size_t *sz));
static int
get_free_blk(fi, off, blk, sz)
/* wtf not really needed; only start of blocks? */
KDBF_FINFO	*fi;
EPI_OFF_T	off, *blk;
size_t		*sz;
/* Returns 1 if `off' is part of a free block in `fi', and sets `blk' and
 * `sz' to block location and size (if non-NULL).  Returns 0 if `off' is
 * not part of a free block.
 */
{
  static const char     fn[] = "get_free_blk";
  BTLOC                 loc;
  size_t                key_size, size;
  EPI_OFF_T             prev_off;

  if (off < (EPI_OFF_T)0L) return(0);
  loc = fbtsearch(fi->treerev, sizeof(off), &off, (BTLOC *)NULL);
  if (TXgetoff(&loc) >= (EPI_OFF_T)0L) {  /* `off' is start of a free block */
    if (blk) *blk = off;
    if (sz) *sz = (size_t)TXgetoff(&loc);
    return(1);
  } else {
    /* See if `off' is part of the previous block.  We use fbtgetnext()
     * since B-tree is sorted in reverse:
     */
    key_size = sizeof(prev_off);
    loc = fbtgetnext(fi->treerev, &key_size, &prev_off, NULL);
    if (TXgetoff(&loc) != (EPI_OFF_T)-1L) {  /* there is a previous block */
      if (key_size != sizeof(prev_off))
        {
          putmsg(MERR, fn,
                 "Internal error: Unexpected key_size %wd (expected %d)",
                 (EPI_HUGEINT)key_size, (int)sizeof(prev_off));
          return(0);                            /* wtf error */
        }
      size = (size_t)TXgetoff(&loc);
      if (off < prev_off + (EPI_OFF_T)size) {
	if (off <= prev_off)                    /* tree is hosed */
          {
            putmsg(MERR, fn,
               "Internal error: Offset 0x%wx not after previous offset 0x%wx",
                   (EPI_HUGEINT)off, (EPI_HUGEINT)prev_off);
            return(0);                          /* wtf error */
          }
	if (blk) *blk = prev_off;
	if (sz) *sz = size;
	return(1);
      }
    }
  }
  return(0);
}

static int trunc_block ARGS((KDBF_FINFO *fi, EPI_OFF_T *blk, size_t *size));
static int
trunc_block(fi, blk, size)
KDBF_FINFO	*fi;
EPI_OFF_T	*blk;
size_t		*size;
/* Truncates `*blk' of size `*size' to fit without overlapping other blocks
 * in `fi'.  Returns 0 if block fits as-is, or 1 if truncated/moved.
 */
{
  static const char     fn[] = "trunc_block";
  EPI_OFF_T             srch, pblk, bk;
  size_t                sz, psz, key_size;
  BTLOC                 loc;

  /* We check for overlap by looking at all blocks from the end of
   * the new block back to its start, since tree is in reverse order
   * and we use fbtgetnext():
   */
  bk = *blk;
  sz = *size;
  srch = bk + sz;
  /* init point for fbtgetnext(): */
  fbtsearch(fi->treerev, sizeof(srch), &srch, (BTLOC *)NULL);
  while (sz > 0) {
    key_size = sizeof(pblk);
    loc = fbtgetnext(fi->treerev, &key_size, &pblk, NULL);
    if (TXgetoff(&loc) == (EPI_OFF_T)-1L) break;    /* no previous block */
    if (key_size != sizeof(pblk))
      {
        putmsg(MERR, fn,
               "Internal error: Unexpected key_size %wd (expected %d)",
               (EPI_HUGEINT)key_size, (int)sizeof(blk));
        return(0);                              /* wtf error */
      }
    psz = (size_t)TXgetoff(&loc);
    if (bk + (EPI_OFF_T)sz <= pblk)             /* sanity check on B-tree */
      {
        putmsg(MERR, fn, "Internal error: bk + sz = %wd <= pblk = %wd",
               (EPI_HUGEINT)(bk + (EPI_OFF_T)sz), (EPI_HUGEINT)pblk);
        return(0);                              /* wtf error */
      }
    if (pblk >= bk) {
      sz = (size_t)(pblk - bk);		/* truncate end */
    } else {				/* before start of block */
      if (pblk + (EPI_OFF_T)psz > bk) {
	sz -= (size_t)((pblk + (EPI_OFF_T)psz) - bk);
	bk = pblk + (EPI_OFF_T)psz;	/* truncate start */
      }
      break;
    }
  }
  if (bk != *blk || sz != *size) {
    *blk = bk;
    *size = sz;
    return(1);
  }
  return(0);
}

static char *orph ARGS((int flags));
static char *
orph(flags)
int	flags;
{
  return(flags & AF_ORPHAN ? " orphaned" : "");
}

static int add_free_block ARGS((KDBF_FINFO *fi, EPI_OFF_T blk, size_t sz,
				int flags));
static int
add_free_block(fi, blk, sz, flags)
KDBF_FINFO	*fi;
EPI_OFF_T	blk;
size_t		sz;
int		flags;
/* Adds free block of `sz' bytes at `blk' to `fi'.  Truncates block if
 * needed to prevent overlap with existing blocks, in which case 1 is
 * returned, else returns 0 if ok or -1 if internal error
 * (e.g. fbtinsert() failed).  Note that `sz' includes header.
 * Increments count of blocks in `fi'.  If AF_TRUNC, will insert and
 * count truncated blocks.  If AF_ORPHAN, increments orphaned
 * block count.
 */
{
  static CONST char     fn[] = "add_free_block";
  static CONST char     cantadd[] = "Cannot add free block to internal tree";
  BTLOC		        loc;
  int		        trunc = 0;
  EPI_OFF_T	        oblk = blk;
  size_t	        osz = sz;

  VERBMSG((999, fn, "offset 0x%wX size 0x%X%s", (EPI_HUGEINT)blk, sz,
           (flags & AF_TRUNC ? "" : " (no trunc)")));

  if (blk < (EPI_OFF_T)0L || sz <= 0) {
    VERBMSG((999, fn, "Bogus value(s) 0x%wX size 0x%wX",
	     (EPI_HUGEINT)oblk, (EPI_HUGEINT)osz));
    return(1);		/* wtf bogus values */
  }

  trunc = trunc_block(fi, &blk, &sz);	/* trim to fit */
  if (trunc && !(flags & AF_TRUNC))
    return(1);	/* don't insert truncated blocks */

  /* Insert block if nonempty: */
  if (sz == 0) {
    fi->cnt[CNT_TRUNCFREES] += 1;
    badprintf(oblk, osz, "Free%s block truncated away", orph(flags));
    return(1);		/* block truncated away */
  }
  TXsetrecid(&loc, (EPI_OFF_T)sz);
  if (fbtinsert(fi->treerev, loc, sizeof(blk), &blk) < 0) {
    /* Error; assume it was an I/O problem: */
    putmsg(MERR + FWE, fn, cantadd);
    return(-1);
  }
  if (fbtinsert(fi->treefwd, loc, sizeof(blk), &blk) < 0) {
    /* Error; assume it was an I/O problem: */
    putmsg(MERR + FWE, fn, cantadd);
    return(-1);
  }
  fi->blks += 1;
  fi->freebytes += (EPI_OFF_T)sz;
  if (flags & AF_ORPHAN) {
    fi->cnt[CNT_FREEORPHANS] += 1;
    if (PrintOrphans) badprintf(oblk, osz, "Orphaned free block");
  }
  if (trunc) {
    fi->cnt[CNT_TRUNCFREES] += 1;
    badprintf(oblk, osz, "Free%s block truncated to 0x%wX 0x%wX",
	      orph(flags), (EPI_HUGEINT)blk, (EPI_HUGEINT)sz);
    return(1);
  }
  return(0);
}

static int trunc_free_block ARGS((KDBF_FINFO *fi, EPI_OFF_T blk, int len));
static int
trunc_free_block(fi, blk, len)
KDBF_FINFO	*fi;
EPI_OFF_T	blk;
int		len;
/* Truncates block `blk' (already in tree) by `len' bytes.
 * Returns 0 on error (B-tree error), 1 if ok.
 */
{
  static CONST char     fn[] = "trunc_free_block";
  static CONST char     cantins[] =
    "Cannot re-insert block 0x%wX 0x%wX in internal B-tree";
  BTLOC		        loc;
  EPI_OFF_T	        ablk;
  size_t	        asz;

  /* This could theoretically be simpler; all we need to do is search and
   * change a BTLOC, which won't affect the sorting order, instead
   * of deleting and reinserting.  -KNG 950922
   */

  if (!get_free_blk(fi, blk, &ablk, &asz) ||
      ablk != blk ||
      asz < (size_t)len) {
    putmsg(MERR, fn,
           "Internal error: free block 0x%wX lost from internal tree",
	   (EPI_HUGEINT)blk);
    return(0);
  }
  TXsetrecid(&loc, (EPI_OFF_T)asz);
  /* hope this works -- no return value given:  -KNG 950922 */
  fbtdelete(fi->treerev, loc, sizeof(blk), &blk);
  fbtdelete(fi->treefwd, loc, sizeof(blk), &blk);
  asz -= (size_t)len;
  TXsetrecid(&loc, (EPI_OFF_T)asz);
  if (fbtinsert(fi->treerev, loc, sizeof(ablk), &ablk) < 0) {
    putmsg(MERR + FWE, fn, cantins, (EPI_HUGEINT)ablk, (long)asz);
    return(0);
  }
  if (fbtinsert(fi->treefwd, loc, sizeof(ablk), &ablk) < 0) {
    putmsg(MERR + FWE, fn, cantins, (EPI_HUGEINT)ablk, (long)asz);
    return(0);
  }
  fi->freebytes -= len;
  return(1);
}

static int add_freetree_page ARGS((KDBF_FINFO *fi, BPAGE *pg, EPI_OFF_T at,
			   size_t totsize, EPI_OFF_T filesz, int flags));
static int
add_freetree_page(fi, pg, at, totsize, filesz, flags)
KDBF_FINFO	*fi;
BPAGE		*pg;
size_t		totsize;
EPI_OFF_T	at, filesz;
int		flags;
/* Adds free blocks in page `pg' at `at' to B-tree `fi', plus page itself.
 * Note that `pg' may not be aligned on a word boundary.  `filesz' is
 * size of KDBF file, for sanity checks.  Returns 0 if ok, 1 if bad blocks
 * (block(s) overlap/bad offset etc.), or -1 on error (i.e. tree error).
 * `totsize' is total size of page, including header.  Updates count
 * of truncated frees.  `flags' contain AF flags (AF_TRUNC ignored).
 */
{
  BPAGE		page;
  BITEM		item, *ip;
  int		i, truncs = 0, tr;
  EPI_OFF_T	offset;
  size_t	sz, osz;
  byte          *s;

  s = (byte *)pg;                       /* alias to avoid gcc opt. */
  memcpy(&page, s, sizeof(BPAGE));	/* alignment */
  if (page.count <= 0 || (size_t)page.count > 2*KDBF_BTORDER) {
    fi->cnt[CNT_BADTREEPAGES] += 1;
    badprintf(at, totsize, "Free-tree%s page has bogus item count %d",
	      orph(flags), (int)page.count);
    return(1);
  }
  if (!(flags & AF_ROOT) && (page.count < (int)KDBF_BTORDER)) {
    fi->cnt[CNT_UNDERFULLPAGES] += 1;
    badprintf(at, totsize,
	      "Underfull%s free-tree page contains %d items (min is %d)",
	      orph(flags), (int)page.count, (int)KDBF_BTORDER);
    /* add its blocks anyway */
  }

  /* Add the page itself first, in case it fails/overlaps.
   * Don't truncate it, and don't mark as orphan since it will be counted
   * in orphaned page count:
   */
  switch (add_free_block(fi, at, totsize, 0)) {
    case 1:
      return(1);	/* block truncated or already present; ignore blocks */
    case 0:		/* ok */
      break;
    case -1:		/* error */
    default:
      return(-1);
  }
  if (flags & AF_ORPHAN) {
    fi->cnt[CNT_PAGEORPHANS] += 1;
    if (PrintOrphans) badprintf(at, totsize, "Orphaned free-tree page");
  }

  /* Add page's blocks: */
  for (ip = pg->items, i = 0; i < page.count; i++, ip++) {
    int headerType;

    s = (byte *)ip;                             /* alias to avoid gcc opt. */
    memcpy(&item, s, sizeof(BITEM));
    offset = TXgetoff(&item.locn);
    sz = (size_t)item.vf.key;
    headerType = kdbf_header_type(sz);
    if (headerType < 0)
      {
        truncs++;
        tr = 1;
        fi->cnt[CNT_TRUNCFREES]++;
        badprintf(offset, sz,"Free block %d invalid size %wd in%s page 0x%wx",
                  i, (EPI_HUGEINT)sz, orph(flags), (EPI_HUGEINT)at);
        continue;
      }
    sz += kdbf_header_size(headerType);
    osz = sz;
    tr = 0;	/* this block not truncated (yet) */
    if (offset < (EPI_OFF_T)0L || offset >= filesz) {	/* bogus offset */
      truncs++;
      tr = 1;
      fi->cnt[CNT_TRUNCFREES] += 1;
      badprintf(offset, sz, "Free block %d out of bounds in%s page 0x%wX",
		i, orph(flags), (EPI_HUGEINT)at);
      continue;
    }
    if (offset + (EPI_OFF_T)sz > filesz) {
      tr = 1;
      sz = (size_t)(filesz - offset);
      /* print bad below */
    }
    switch (add_free_block(fi, offset, sz, (flags & ~AF_ROOT) | AF_TRUNC)) {
      case 1:	/* block truncated */
	truncs++;
	break;
      case 0:	/* ok */
	if (tr) {
	  /* do this here since we now know add_free_block() didn't: */
	  truncs++;
	  fi->cnt[CNT_TRUNCFREES] += 1;
	  badprintf(offset, osz, "Free block %d truncated in%s page %s",
		    i, orph(flags), (EPI_HUGEINT)at);
	}
	break;
      case -1:	/* error */
      default:
	return(-1);
    }
  }
  return(truncs ? 1 : 0);
}

static int scan_freetree_pages ARGS((KDBF_FINFO *fi,int fh,EPI_OFF_T filesz));
static int
scan_freetree_pages(fi, fh, filesz)
KDBF_FINFO	*fi;
int		fh;
EPI_OFF_T	filesz;
/* Scans `fh' for valid free-tree pages and free free-tree pages (got that?),
 * and adds them to `fi'.  Returns 0 if internal error, 1 if ok.
 * `filesz' is size of file `fh'.
 */
{
  static CONST char     fn[] = "scan_freetree_pages";
  byte		        *buf = NULL, *ptr, *data;
  EPI_OFF_T	        at, szrd, rdoff, next, blk, nextfree, here, hi;
  size_t	        bufsz, skip, sz;
  KDBF_CHKSUM	        chk;
  KDBF_TRANS	        trans;
  int		        hdsz, ret = 1;
  BTLOC		        loc;
  METER                 *meter = METERPN;

  /* Buffer size; must be > free-tree page size: */
#ifdef SMALL_MEM
  bufsz = (size_t)30000;  /* don't malloc > ~30k under DOS  -KNG 951109 */
#else
  bufsz = BufSz;
  if (bufsz < MinFPgSz) bufsz = MinFPgSz;
#endif
  errno = 0;
  if ((buf = (byte *)malloc(bufsz)) == NULL) {
    MAERR(fn, bufsz);
    goto err;
  }

  /* Get the offset of the next (first) free block: */
  nextfree = (EPI_OFF_T)(-1);
  fbtsearch(fi->treefwd, sizeof(nextfree), &nextfree, (BTLOC *)NULL);
  sz = sizeof(nextfree);
  loc = fbtgetnext(fi->treefwd, &sz, &nextfree, NULL);
  if (TXgetoff(&loc) == (EPI_OFF_T)-1L)             /* no next block */
    nextfree = (EPI_OFF_T)EPI_OFF_T_MAX;

  hi = filesz - (EPI_OFF_T)MinFPgSz;
  if (hi < (EPI_OFF_T)0) hi = (EPI_OFF_T)0;

  if (Verbosity >= 2)
    meter = openmeter("Scanning for orphaned free tree pages:", MeterType,
                      MDOUTFUNCPN, MDFLUSHFUNCPN, NULL, (EPI_HUGEINT)hi);
  /* Read a chunk of the file at a time and scan it: */
  for (at = (EPI_OFF_T)0L; at + (EPI_OFF_T)MinFPgSz <= filesz; at += rdoff) {
    if (EPI_LSEEK(fh, at, SEEK_SET) != at ||
 (szrd = (EPI_OFF_T)kdbfchk_raw_read(fh, buf, bufsz)) < (EPI_OFF_T)MinFPgSz ||
	(int)szrd == -1) {
      rderr(fi, fn, at, MinFPgSz);
      /* skip to likely next block, and try to read again: */
      rdoff = NEXT_BLK(at) - at;
      continue;
    }

    /* Scan the chunk for valid tree pages, free or not: */
    ptr = buf;
    skip = 0;	/* shut up lint */
    for (rdoff = (EPI_OFF_T)0L;
	 rdoff + (EPI_OFF_T)MinFPgSz <= szrd;
	 ) {
      here = at + rdoff;
      if (meter != METERPN) METER_UPDATEDONE(meter, here);

      /* First see if we're at a known valid free block, and skip if so:
       * -KNG 960307
       */
      if (here >= nextfree)                     /* optimization */
        {
          if (get_free_blk(fi, here, &blk, &sz))
            {
              if (blk != here)  /* should be `here' since skip 1 at a time? */
                {
                  putmsg(MERR, fn, "Internal error: blk != here");
                  goto err;
                }
              skip = sz;
              goto skipN;
            }
          nextfree = here;
          loc = fbtsearch(fi->treefwd, sizeof(nextfree), &nextfree,
                          (BTLOC *)NULL);
          /* since get_free_blk failed, we know our search failed too */
          sz = sizeof(nextfree);
          loc = fbtgetnext(fi->treefwd, &sz, &nextfree, NULL);
          if (TXgetoff(&loc) == (EPI_OFF_T)-1L)     /* no next block */
            nextfree = (EPI_OFF_T)EPI_OFF_T_MAX;
        }

      /* In-line version of part of kdbf_proc_head(), for speed: */
#ifdef KDBF_CHECK_OLD_F_TYPE
      if ((*ptr & KDBF_CHKSUMBITS) == KDBF_CHECK_OLD(/**/))
        {
          if ((*ptr & KDBFTYBITS) >= KDBF_NUM_TYPES) goto skip1;
        }
      else
#endif  /* KDBF_CHECK_OLD_F_TYPE */
      if ((*ptr & KDBF_CHKSUMBITS) != KDBF_CHECK(wtf) ||
          (*ptr & KDBFTYBITS) >= KDBF_NUM_TYPES)
        goto skip1;

      hdsz = kdbf_proc_head(ptr, (size_t)(szrd - rdoff), here, &trans);
      if (hdsz <= 0 ||				/* bad header */
	  trans.size != KDBF_FREETREE_PGSZ) {	/* bad size */
	/* Note that we always skip to next byte, even if we read a
	 * "valid" header, since we only trust checksummed free-tree pages
	 * at this point.  This can slow things:
	 */
        goto skip1;
      }
      /* see if page extends past end of read buffer: */
      if (rdoff + (EPI_OFF_T)hdsz + (EPI_OFF_T)trans.size > szrd) {
	if (at + szrd >= filesz) goto done;	/* past EOF */
	break;					/* re-read to get whole page */
      }
      data = ptr + hdsz;
      /* Is it an in-use free-tree page?: */
      if (trans.type & KDBF_FREEBITS) {		/* tree page flag set */
	if (trans.used == KDBF_BT_PGSZ) {	/* correct page size */
	  /* Yes; check the checksum: */
	  memcpy(&chk, data + KDBF_BT_PGSZ, sizeof(KDBF_CHKSUM));
	  if (chk.offset == at + rdoff &&
	      chk.chksum == kdbf_checksum_block(data, KDBF_BT_PGSZ)) {
	    /* Valid page: add its free blocks to the tree: */
	    skip = hdsz + trans.size;
	    VERBMSG((999, fn, "Adding page at 0x%wX", (EPI_HUGEINT)(at + rdoff)));
	    /* Don't worry about truncated/overlapping blocks: */
	    if (add_freetree_page(fi, (BPAGE *)data, at+rdoff, skip, filesz,
				  AF_ORPHAN) < 0)
	      goto err;
            goto skipN;
	  }
	}
      } else {					/* !KDBF_FREEBITS */
	if (trans.used == 0) {			/* free block */
	  /* Check the next-pointer and checksum: */
	  memcpy(&next, data, sizeof(next));
	  memcpy(&chk, data + sizeof(EPI_OFF_T), sizeof(KDBF_CHKSUM));
	  if (next >= (EPI_OFF_T)0L &&
	      chk.offset == at + rdoff &&
	      chk.chksum == kdbf_checksum_block(data, sizeof(EPI_OFF_T))) {
	    /* Valid free page: add to the tree: */
	    skip = hdsz + trans.size;
	    VERBMSG((999, fn, "Adding free page at 0x%wX",
		     (EPI_HUGEINT)(at + rdoff)));
	    /* Don't add if truncated: */
	    if (add_free_block(fi, at + rdoff, skip, 0) < 0)
	      goto err;
	    fi->cnt[CNT_PAGEORPHANS] += 1;
	    if (PrintOrphans)
	      badprintf(at + rdoff, skip, "Orphaned free free-tree page");
          skipN:
            rdoff += (EPI_OFF_T)skip;
            ptr += skip;
	    continue;	/* next block */
	  }
	}
      }
      /* Invalid header/block/whatever; skip to next byte: */
    skip1:
      rdoff++;
      ptr++;
    }
  }

done:
  if (meter != METERPN)
    {
      meter_updatedone(meter, (EPI_HUGEINT)hi);
      meter_end(meter);
      meter = closemeter(meter);
    }
errdone:
  if (buf) free(buf);
  return(ret);
err:
  ret = 0;
  if (Verbosity >= 2) PF(("\n"));
  goto errdone;
}

static int read_free_pages ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T filesz,
                      EPI_OFF_T at, EPI_OFF_T startofs, METER *meter));
static int
read_free_pages(fi, fh, filesz, at, startofs, meter)
KDBF_FINFO	*fi;
int		fh;
EPI_OFF_T	filesz, at, startofs;
METER           *meter;                 /* optional meter */
/* Attempts to read free-pages list from `fh', starting at `at',
 * and add to `fi'.  Returns 0 if successful, 1 if free list corrupt,
 * or -1 on severe error.  `startofs' is start-pointer offset.
 */
{
  static CONST char     fn[] = "read_free_pages";
  byte		        *buf = NULL, *data;
  size_t	        numrd;
  int		        hdsz, ret = 0;
  KDBF_TRANS	        trans;
  KDBF_CHKSUM	        chk;
  EPI_OFF_T	        next, prev;

  errno = 0;
  if ((buf = (byte *)malloc(MinFPgSz)) == NULL) {
    MAERR(fn, MinFPgSz);
    return(-1);
  }

  /* Walk the list: */
  for (prev = startofs; at != (EPI_OFF_T)0L; prev = at, at = next) {
    if (at < (EPI_OFF_T)0L || at > filesz - (EPI_OFF_T)MinFPgSz) {
      fi->cnt[CNT_BADFREEPAGES] += 1;
      badprintf(prev, 0, "%s free free-tree page 0x%wX is out-of-bounds",
		(prev == startofs ? "Root" : "Next"), (EPI_HUGEINT)at);
      ret = 1;		/* bogus offset or past EOF */
      break;
    }
    if (EPI_LSEEK(fh, at, SEEK_SET) != at ||
	(numrd = kdbfchk_raw_read(fh, buf, MinFPgSz)) != MinFPgSz) {
      rderr(fi, fn, at, MinFPgSz);
      ret = 1;  /* treat as non-severe error */
      break;
    }
    hdsz = kdbf_proc_head(buf, numrd, at, &trans);
    if (hdsz <= 0 ||				/* bad header */
	trans.used != 0 ||			/* not free */
	trans.size != KDBF_FREETREE_PGSZ ||	/* bad size */
	at + (EPI_OFF_T)(hdsz + trans.size) > filesz) {	/* extends past EOF */
      fi->cnt[CNT_BADFREEPAGES] += 1;
      badprintf(at, 0, "Bad header/size for%s free free-tree page, from 0x%wX",
		(prev == startofs ? " root" : ""), (EPI_HUGEINT)prev);
      ret = 1;
      break;
    }
    data = buf + hdsz;
    memcpy(&next, data, sizeof(next));		/* grab next-pointer */
    /* Check checksum: */
    memcpy(&chk, data + sizeof(EPI_OFF_T), sizeof(chk));
    if (chk.offset != at ||
	chk.chksum != kdbf_checksum_block(data, sizeof(EPI_OFF_T)) ||
	next < (EPI_OFF_T)0L) {	/* bogus ofset */
      fi->cnt[CNT_BADFREEPAGES] += 1;
      badprintf(at, (size_t)hdsz + trans.size,
		"Bad checksum/offset for%s free free-tree page, from 0x%wX",
		(prev == startofs ? " root" : ""), (EPI_HUGEINT)prev);
      ret = 1;
      break;
    }
    /* Add to B-tree.  Stop if truncated block or other error: */
    if ((ret = add_free_block(fi, at, hdsz + trans.size, 0)) != 0) {
      break;
    }
    if (meter != METERPN) METER_UPDATEDONE(meter, (EPI_HUGEINT)fi->freebytes);
  }

  free(buf);
  return(ret);
}

static int read_freetree ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T filesz,
         EPI_OFF_T root, int depth, EPI_OFF_T parent, int it, METER *meter));
static int
read_freetree(fi, fh, filesz, root, depth, parent, it, meter)
KDBF_FINFO	*fi;
int		fh, depth, it;
EPI_OFF_T	filesz, root, parent;
METER           *meter;                 /* optional meter */
/* Attempts to read free tree recursively from `fh', starting at `root',
 * and adds to `fi'.  `depth' is current depth of tree (0 before top).
 * Returns 0 if successful, 1 if free tree corrupt, or -1 on severe error.
 * `parent' is parent page (or start-pointers offset if depth == 0);
 * `it' is item index within that page (-1 == lpage).
 */
{
  static CONST char     fn[] = "read_freetree";
  byte		        *buf = BYTEPN, *s;
  BPAGE		        *bp, page;
  BITEM		        *ip, item;
  size_t	        numrd;
  int		        hdsz, ret = 0, i;
  KDBF_TRANS	        trans;
  KDBF_CHKSUM	        chk;

  depth++;	/* we're "at" the new page, down a level */
  if (meter != METERPN) METER_UPDATEDONE(meter, (EPI_HUGEINT)fi->freebytes);

  if (root == (EPI_OFF_T)0L)
    goto done;	/* "null" page */

  VERBMSG((999, fn, "Reading page 0x%wX", (EPI_HUGEINT)root));

  if (depth >= KDBF_BT_CACHE_SZ) {
    fi->cnt[CNT_TOODEEPPAGES] += 1;
    badprintf(root, 0, "Free-tree depth %d exceeds B-tree cache size", depth);
  }

  errno = 0;
  if ((buf = (byte *)malloc(KDBF_HMAXSIZE + KDBF_FREETREE_PGSZ)) == BYTEPN) {
    MAERR(fn, KDBF_HMAXSIZE + KDBF_FREETREE_PGSZ);
    ret = -1;
    goto done;
  }
  if (root < (EPI_OFF_T)0L || root > filesz - (EPI_OFF_T)MinFPgSz) {
    fi->cnt[CNT_BADTREEPAGES] += 1;
    badprintf(parent, 0,
	      "Out-of-bounds %s free-tree page 0x%wX, at item %d depth %d",
	      (depth > 1 ? "child" : "root"), (EPI_HUGEINT)root, it, depth);
    ret = 1;		/* bogus offset */
    goto done;
  }
  if (EPI_LSEEK(fh, root, SEEK_SET) != root ||
      (numrd = kdbfchk_raw_read(fh, buf, KDBF_HMAXSIZE + KDBF_FREETREE_PGSZ))
      < MinFPgSz ||
      (int)numrd == -1) {
    rderr(fi, fn, root, MinFPgSz);
    ret = 1;            /* treat as non-severe error */
    goto done;
  }
  hdsz = kdbf_proc_head(buf, numrd, root, &trans);
  if (hdsz <= 0 ||				/* bad header */
      trans.used != KDBF_BT_PGSZ ||		/* free, or bad page size */
      !(trans.type & KDBF_FREEBITS) ||		/* not free-tree block */
      root + (EPI_OFF_T)(hdsz + trans.size) > filesz) {	/* extends past EOF */
    fi->cnt[CNT_BADTREEPAGES] += 1;
    badprintf(root, 0,
	   "Bad header for%s free-tree page, depth %d, from parent 0x%wX item %d",
	      (depth > 1 ? "" : " root"), depth, (EPI_HUGEINT)parent, it);
    ret = 1;
    goto done;
  }
  bp = (BPAGE *)(buf + hdsz);
  /* Check checksum: */
  s = (byte *)bp + KDBF_BT_PGSZ;                /* alias to avoid gcc opt. */
  memcpy(&chk, s, sizeof(chk));
  if (chk.offset != root ||
      chk.chksum != kdbf_checksum_block(bp, trans.used)) {
    fi->cnt[CNT_BADTREEPAGES] += 1;
    badprintf(root, (size_t)hdsz + trans.size,
	     "Bad%s free-tree page checksum, depth %d, from parent 0x%wX item %d",
	      (depth > 1 ? "" : " root"), depth, (EPI_HUGEINT)parent, it);
    /* Since header was ok, add to tree anyway:  -KNG 960307 */
    add_free_block(fi, root, (size_t)hdsz + trans.size, 0);
    ret = 1;
    goto done;
  }
  /* Add its blocks to the tree: */
  ret = add_freetree_page(fi, bp, root, (size_t)hdsz + trans.size, filesz,
			  (depth > 1 ? 0 : AF_ROOT));
  if (ret) goto done;	/* corruption or error */

  /* Recursively add this block's pages: */
  s = (byte *)bp;                               /* alias to avoid gcc opt. */
  memcpy(&page, s, sizeof(page));
  ret = read_freetree(fi, fh, filesz, page.lpage, depth, root, -1, meter);
  if (ret != 0) goto done;
  for (ip = bp->items, i = 0; i < page.count; i++, ip++) {
    s = (byte *)ip;                             /* alias to avoid gcc opt. */
    memcpy(&item, s, sizeof(item));
    ret = read_freetree(fi, fh, filesz, item.hpage, depth, root, i, meter);
    if (ret != 0) goto done;
  }

 done:
  if (buf) free(buf);
  return(ret);
}

static void free_iter ARGS((EPI_OFF_T key, BTLOC loc, void *data));
static void
free_iter(key, loc, data)
EPI_OFF_T       key;
BTLOC           loc;
void            *data;
{
  htfpf((FILE *)data, "0x%wX 0x%wX\n",
        (EPI_HUGEINT)key, (EPI_HUGEINT)TXgetoff(&loc));
}

static int get_free_stuff ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T filesz,
				KDBF_START *start));
static int
get_free_stuff(fi, fh, filesz, start)
KDBF_FINFO	*fi;
int		fh;
EPI_OFF_T	filesz;
KDBF_START	*start;
/* Reads and scans free tree from `fh' into `fi'.  Returns 1 if free
 * list and tree intact, 0 if corrupt, or -1 on internal error.
 */
{
  int           rf_ret, rp_ret, ret;
  long          blkcnt;
  EPI_OFF_T     vfilesz;
  METER         *meter = METERPN;

  /* Try to read free tree, assuming start pointers are valid: */
  vfilesz = filesz - (EPI_OFF_T)sizeof(KDBF_START);
  if (Verbosity >= 2)
    meter = openmeter("Reading free tree and list:", MeterType,
                      MDOUTFUNCPN, MDFLUSHFUNCPN,NULL, (EPI_HUGEINT)vfilesz);
  if ((rf_ret = read_freetree(fi, fh, vfilesz, start->btree, 0,
          vfilesz + ((char *)&start->btree - (char *)start), 0, meter)) < 0)
    goto err;           /* severe internal error */
  if ((rp_ret = read_free_pages(fi, fh, vfilesz, start->free_pages,
          vfilesz + ((char *)&start->free_pages - (char *)start), meter)) < 0)
    goto err;
  if (meter != METERPN)
    {
      meter_updatedone(meter, (EPI_HUGEINT)fi->freebytes);
      meter_end(meter);
    }
  if (rf_ret && Verbosity >= 1) htpf("*** Free tree corrupt ***\n");
  if (rp_ret && Verbosity >= 1) htpf("*** Free-pages list corrupt ***\n");

  if (QuickFix)
    {
      if (Verbosity >= 2) htpf("Skipping orphan and data block scan\n");
      goto fin;
    }

  /* Scan for free tree pages, adding blocks found.  Do this even if
   * free tree read succeeded, since there may be orphaned blocks/pages:
   */
  blkcnt = fi->blks;            /* save count */
  if (!scan_freetree_pages(fi, fh, filesz)) goto err;
  if (fi->blks != blkcnt)
    {
      if (Verbosity >= 1) htpf("*** Free tree/list truncated ***\n");
    }
  else                          /* no new blocks added */
    {
    fin:
      if (rf_ret == 0 && rp_ret == 0)
        {
          if (Verbosity >= 2) htpf("Free tree and list appear intact\n");
          ret = 1;              /* all ok */
          goto done;
        }
    }

  ret = 0;                      /* ok scan but corrupt list(s) */
  goto done;
err:
  ret = -1;
done:
  if (meter != METERPN)
    {
      meter_end(meter);
      meter = closemeter(meter);
    }
  return(ret);
}

static int get_size_type ARGS((size_t *size, byte *type, int *pad));
static int
get_size_type(size, type, pad)
size_t	*size;
byte	*type;
int	*pad;
/* Sets `type' and `size' to appropriate type and size for block whose overall
 * size (including header) is `size'.  Note that odd sized blocks (e.g.
 * 18 bytes, from overlap truncation) may get truncated: then `pad' gets set
 * to number of 1-byte null pads needed at end of block, so that the size
 * is appropriate for the header type and type can accurately be derived
 * from size (e.g., so that 0xFE data size has KDBFB header and not word).
 * Returns 0 on error (block too small/big) or header size if ok.
 */
{
  static const char     fn[] = "get_size_type";
  static CONST size_t	maxdsz[KDBF_NUM_TYPES] = {
    0, KDBFNMAX, KDBFBMAX, KDBFWMAX, KDBFSMAX
  };
  static CONST int	hdsz[KDBF_NUM_TYPES] = {
    1, 1 + KDBFN_LEN, 1 + KDBFB_LEN, 1 + KDBFW_LEN, 1 + KDBFS_LEN
  };
  size_t	dsz;
  int		t, gt;

  *pad = 0;
  dsz = *size;
  if (dsz < KDBF_HMINSIZE) return(0);

  for (t = 0; t < KDBF_NUM_TYPES; t++) {
    if (dsz - hdsz[t] <= maxdsz[t]) break;
  }
  if (t >= KDBF_NUM_TYPES)
    {
      putmsg(MERR, fn, "Internal error: t >= KDBF_NUM_TYPES");
      return(0);
    }
  dsz -= hdsz[t];

  /* See if header type is derivable from data size: */
  if ((gt = kdbf_header_type(dsz)) != t) {
    VERBMSG((999, "get_size_type",
	     "Using type %d instead of %d for total block size 0x%wX",
	     gt, t, (EPI_HUGEINT)((EPI_OFF_T)(*size))));
    t = gt;
    dsz = maxdsz[t];
    *pad = *size - (hdsz[t] + dsz);
  }
  *size = dsz;
  *type = t;
  return(hdsz[t]);
}

static int write_block ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T at, size_t sz,
			     byte *data, size_t *used, unsigned flags));
static int
write_block(fi, fh, at, sz, data, used, flags)
KDBF_FINFO	*fi;
int		fh;
EPI_OFF_T	at;
size_t		sz, *used;
byte		*data;
unsigned	flags;
/* Writes block at `at' to `fh'.  `sz' is total size including header.
 * Returns 0 if ok, number of pad bytes if overall size truncated (but
 * maybe not `*used'; pads added to tree and null pad count updated),
 * or -1 on error.  `data' is block data (`*used' bytes) or NULL for
 * free block.  Block is added to tree and counted as free pad if
 * WB_ADD set in `flags', or if data and truncated to 0.  `*used' may
 * be much larger than `sz'; will be truncated if needed (but not
 * counted).  If WB_ORPHAN set, block is counted as orphaned free block
 * instead of pad.  If `fh' is < 0, won't write to file.  If WB_OKTRUNC,
 * will accept truncation during WB_ADD.
 */
{
  static CONST char     fn[] = "write_block";
  KDBF_TRANS	        trans;
  int		        hdsz, pad, i;
  unsigned              af;
  size_t                chunk, cr;
  EPI_OFF_T             left;
  byte		        buf[KDBF_HMAXSIZE];
  byte                  tmp[8192];

  trans.used = (data && used) ? *used : 0;
  trans.size = sz;
  if (!get_size_type(&trans.size, &trans.type, &pad))
    return(-1);
  /* Truncate block data if needed.  Could have been too large originally
   * or because of above get_size_type() call:
   */
  if (trans.used > trans.size) {
    trans.used = trans.size;
    if (trans.used == 0)	/* i.e. it is now a free block */
      flags |= WB_ADD;
  }

  if (used) *used = trans.used;
  /* Clear header buffer so header alignment pad bytes are clear: */
  for (i = 0; (size_t)i < KDBF_HMAXSIZE; i++) buf[i] = 0;
  hdsz = kdbf_create_head(KDBFPN, buf, &trans);
  if (hdsz < 0) return(-1);
  if (fh >= 0) {
    if (EPI_LSEEK(fh, at, SEEK_SET) != at ||
	kdbfchk_raw_write(fh, buf, hdsz) != (size_t)hdsz) {
    err1:
      putmsg(MERR + FWE, fn, "Cannot write block at 0x%wX to output file: %s",
	     (EPI_HUGEINT)at, strerror(errno));
      return(-1);
    }
    if (trans.used > 0 &&			/* write data */
	kdbfchk_raw_write(fh, data, trans.used) != trans.used)
      goto err1;
    /* Clear data if del_block(), and/or expand file if at EOF: */
    if (trans.size > trans.used) {
#if 1
      chunk = (size_t)(trans.size - trans.used);
      if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
      memset(tmp, 0, chunk);
      for (left = trans.size - trans.used;
           left > (EPI_OFF_T)0;
           left -= (EPI_OFF_T)cr)
        {
          cr = (left < (EPI_OFF_T)chunk) ? (size_t)left : chunk;
          if (kdbfchk_raw_write(fh, tmp, cr) != cr) goto err1;
        }
#else /* !1 */
      if (EPI_LSEEK(fh, (EPI_OFF_T)(trans.size - trans.used - 1), SEEK_CUR) <
	  (EPI_OFF_T)0L ||
	  kdbfchk_raw_write(fh, &end, 1) != 1)
	goto err1;
#endif /* !1 */
    }
  }

  if (flags & WB_ADD) {
    if (fi->treerev != BTREEPN)                 /* if not del_block() call */
      {
        af = 0;
        if (flags & WB_ORPHAN) af |= AF_ORPHAN;
        if (flags & WB_OKTRUNC) af |= AF_TRUNC;
        if (add_free_block(fi, at, (size_t)hdsz + trans.size, af) != 0)
          {
            putmsg(MERR, fn, "Internal B-tree error");  /* if 1 returned */
            return(-1);
          }
      }
    if (!(flags & WB_ORPHAN)) {
      fi->cnt[CNT_FREEPADS] += 1;
      badprintf(at, sz, "Free block pad added");
    }
  }

  if (pad) {
    /* Block was truncated; truncate it in the B-tree (if free)
     * and add padding to tree and file:
     */
    if (fi->treerev != BTREEPN &&
        trans.used == 0 &&	/* free block, therefore in B-tree */
	!trunc_free_block(fi, at, pad))
      return(-1);
    at += (EPI_OFF_T)hdsz + (EPI_OFF_T)trans.size;
    trans.used = trans.size = 0;
    trans.type = KDBFNULL;
    hdsz = kdbf_create_head(KDBFPN, buf, &trans);
    if (hdsz < 0) return(-1);                   /* failed */
    if (hdsz != 1)
      {
        putmsg(MERR, fn,
               "Internal error: Unexpected header size %d (expected 1)",
               (int)hdsz);
        return(-1);
      }
    for (i = 0; i < pad; i++, at++) {
      if (fh >= 0 &&
	  kdbfchk_raw_write(fh, buf, 1) != 1) {
	putmsg(MERR + FWE, fn, "Cannot write pad at 0x%wX to output file: %s",
	       (EPI_HUGEINT)at, strerror(errno));
	return(-1);
      }
      if (fi->treerev != BTREEPN &&
          add_free_block(fi, at, 1, 0) != 0)
        {
          putmsg(MERR, fn, "Internal B-tree error");    /* if 1 returned */
          return(-1);
        }
      fi->cnt[CNT_FREEPADS] += 1;
      badprintf(at, (size_t)hdsz, "Null free block pad added");
    }
  }
  return(pad);
}

static EPI_OFF_T find_lasthdr ARGS((KDBF_FINFO *fi, int infh, EPI_OFF_T filesz,
                                KDBF_TRANS *trans));
static EPI_OFF_T
find_lasthdr(fi, infh, filesz, trans)
KDBF_FINFO      *fi;
int             infh;
EPI_OFF_T       filesz;
KDBF_TRANS      *trans;
/* Scans backwards from EOF and returns last valid KDBF header, for
 * quickfix mode.  Returns -1 if none found, -2 on error.  Sets
 * `*trans'. WTF WTF WTF still a small chance we will return a "header"
 * that is really part of data; can not fix unless we scan from start.
 */
{
  static CONST char     fn[] = "find_lasthdr";
  EPI_OFF_T             rdoff, ret;
  size_t                rd, xtra;
  KDBF_TRANS            prevtrans, oktrans, okbuttrans;
  byte                  *s, *e;
  byte                  buf[(2*KDBF_HMAXSIZE > 1000?2*KDBF_HMAXSIZE:1000)+24];

  if (filesz == (EPI_OFF_T)0) goto mt;

  rdoff = filesz;
  rd = sizeof(buf) - KDBF_HMAXSIZE;
  xtra = 0;
  prevtrans.at = filesz;
  oktrans.at = okbuttrans.at = (EPI_OFF_T)(-1);
  do
    {
      if ((EPI_OFF_T)rd > rdoff) rd = (size_t)rdoff;
      rdoff -= (EPI_OFF_T)rd;
      rd += xtra;
      errno = 0;
      if (EPI_LSEEK(infh, rdoff, SEEK_SET) < (EPI_OFF_T)0L ||
          kdbfchk_raw_read(infh, buf, rd) != rd)
        {
          rderr(fi, fn, rdoff, rd);
          goto err;
        }
      for (e = buf + rd, s = e - 1; s >= buf; s--)
        {
          ret = rdoff + (EPI_OFF_T)(s - buf);
          if (kdbf_proc_head(s, e - s, ret, trans) <= 0)
            continue;
          /* If this block abuts EOF, then the file's probably ok: */
          if (trans->end == filesz &&
              trans->end - trans->at > (EPI_OFF_T)1)
            goto done;
          if (oktrans.at == (EPI_OFF_T)(-1))
            oktrans = *trans;
          /* If this block abuts the next header, then it's very
           * likely a complete, valid block, so truncate after it
           * (i.e. at next header).  But it could be a F0 byte that
           * looks like a null header, so don't stop if it is:
           */
          if (trans->end == prevtrans.at &&
              trans->end - trans->at > (EPI_OFF_T)1)
            {
              if (okbuttrans.at == (EPI_OFF_T)(-1))
                okbuttrans = (prevtrans.at != filesz ? prevtrans : *trans);
            }
          else
            prevtrans = *trans;
        }
      rd -= xtra;
      xtra = KDBF_HMAXSIZE;
    }
  while (rdoff > (EPI_OFF_T)0 && (filesz - rdoff) < 100000);
  if (okbuttrans.at != (EPI_OFF_T)(-1))
    {
      *trans = okbuttrans;
      ret = okbuttrans.at;
    }
  else if (oktrans.at != (EPI_OFF_T)(-1))
    {
      *trans = oktrans;
      ret = oktrans.at;
    }
  else
    {
    mt:
      ret = (EPI_OFF_T)(-1);                /* can not find any valid hdrs */
    }
  goto done;

err:
  ret = (EPI_OFF_T)(-2);
done:
  return(ret);
}

static int copy_blocks ARGS((KDBF_FINFO *fi, int infh, EPI_OFF_T filesz,
			     int outfh));
static int
copy_blocks(fi, infh, filesz, outfh)
KDBF_FINFO	*fi;
int		infh, outfh;
EPI_OFF_T	filesz;
/* Reads `infh' (size `filesz') and copies to `outfh' (if >= 0).
 * Copies (and truncates or pads if needed) allocated blocks, and adds
 * free blocks from `fi'.  Returns 1 if ok, or 0 on error.
 */
{
  static CONST char     fn[] = "copy_blocks";
  byte		        *buf = BYTEPN;
  EPI_OFF_T	        at, fblk, blk, lastend;
  size_t	        bufsz, numrd, fsz, tsz, orgused, orgtsz, skip;
  KDBF_TRANS	        trans;
  int		        ret = 1, hdsz, res;
  unsigned	        flags;
  METER                 *meter = METERPN;
  EPI_HUGEINT           msz;
  char                  *msg;

  bufsz = KDBF_HMAXSIZE;
  errno = 0;
  if ((buf = (byte *)malloc(bufsz)) == BYTEPN)
    {
      MAERR(fn, bufsz);
      goto err;
    }

  if (OutFh >= 0)
    msg = "Repairing KDBF file:";
  else
    msg = "Scanning data blocks:";
  at = filesz - (EPI_OFF_T)KDBF_HMINSIZE;
  if (at < (EPI_OFF_T)0) at = (EPI_OFF_T)0;
  msz = (EPI_HUGEINT)at;
  if (Verbosity >= 2)
    meter = openmeter(msg, MeterType, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL, msz);

  for (lastend = at = (EPI_OFF_T)0L;
       at + (EPI_OFF_T)KDBF_HMINSIZE <= filesz;
       at += (EPI_OFF_T)skip) {
    if (meter != METERPN) METER_UPDATEDONE(meter, (EPI_HUGEINT)at);

    /* Parse the next block's header: */
    if (EPI_LSEEK(infh, at, SEEK_SET) != at ||
	(numrd = kdbfchk_raw_read(infh, buf,KDBF_HMAXSIZE)) < KDBF_HMINSIZE ||
	(int)numrd == -1) {
      rderr(fi, fn, at, KDBF_HMINSIZE);
      hdsz = -2;        /* treat as bad header; don't bail (-2 == read err) */
    } else {
      hdsz = kdbf_proc_head(buf, numrd, at, &trans);
    }

    /* See if we're at a free block, and write it out if so: */
    if (get_free_blk(fi, at, &fblk, &fsz)) {
#ifndef THIS_WILL_HURT_YOU
      if (fblk != at)   /* should be fblk==at since skip 1 byte at a time? */
        {
          putmsg(MERR, fn, "Internal error: fblk != at");
          goto err;
        }
#endif
      /* Prefix a free block pad if there's a gap (i.e. skipped bad data): */
      if (at > lastend) {
	if (write_block(fi, outfh, lastend, (size_t)(at - lastend), NULL,
			NULL, WB_ADD
#ifdef THIS_WILL_HURT_YOU
                        | WB_OKTRUNC
#endif
                        ) < 0)
	  goto err;
      }
      lastend = at;
      /* Write the free block: */
      if (write_block(fi, outfh, fblk, fsz, NULL, NULL, 0) < 0)
	goto err;
      skip = fsz;	/* might include pad bytes */
      lastend = at + (EPI_OFF_T)skip;	/* i.e. valid block written */
      /* Warn if original header was bogus, or header size differs: */
      if (hdsz <= 0 ||			/* bad header */
	  trans.size == 0) {		/*    ""      */
	fi->cnt[CNT_BADFREES] += 1;	/* wtf counted in truncfrees too? */
	badprintf(at, 0, "Free block has bad header");
      } else if (trans.used) {
	/* Cannot have free-bits set if not tree page.  Note that a bad block
	 * might have the correct size and free-bits set, but still not
	 * originally be a free page; we cannot detect this since we cannot
	 * flag pages in the internal tree:  -KNG 951010     wtf
	 */
	if (trans.used != KDBF_BT_PGSZ || !(trans.type & KDBF_FREEBITS)) {
	  fi->cnt[CNT_BADFREES] += 1;	/* wtf counted in truncfrees too? */
	  badprintf(at, 0, "Free block was marked allocated in header");
	}
      } else if ((size_t)hdsz + trans.size != fsz) {
	fi->cnt[CNT_TRUNCFREES] += 1;	/* wtf may be counted twice */
	badprintf(at, (size_t)hdsz + trans.size,
		  "Free block total size bad in header (changed)");
      }
      /* wtf what if type changes but size is same */
      continue;
    }

    /* We're not at a known free block.  Check the header, and copy the
     * block if ok.  If this is a free block, also add it to the tree,
     * unless we're ignoring orphaned free blocks (then it'll get rolled
     * into the next pad block):
     */
    if (hdsz <= 0 ||				/* bad header */
        trans.used > KDBF_MAX_ALLOC ||
        trans.size > KDBF_MAX_ALLOC ||
	(trans.size == 0 && (trans.type & KDBFTYBITS) != KDBFNULL) || /* "" */
	(IgnoreOrphans && trans.used == 0) ||	/* ignore free blocks? */
	/* cannot be free-tree page: all (valid ones) are in free tree: */
	(trans.type & KDBF_FREEBITS)) {
      /* Bad; skip a byte and try again.  Skip a block if read error: */
#ifdef THIS_WILL_HURT_YOU
      /* want to skip a whole block on read error, since there's likely
       * to be a bunch of them.  but this breaks assumption that skip == 1,
       * above.  -KNG 960717
       */
      if (hdsz == -2) skip = (size_t)(NEXT_BLK(at) - at);
      else skip = 1;
#else   /* !THIS_WILL_HURT_YOU */
      skip = 1;
#endif  /* !THIS_WILL_HURT_YOU */
      continue;
    }

    /* Prefix a new free block pad if there's a gap (i.e. skipped bad data):*/
    if (at > lastend) {
      if (write_block(fi, outfh, lastend, (size_t)(at - lastend), NULL,
		      NULL, WB_ADD) < 0)
	goto err;
    }

    /* Truncate block if it overlaps known free blocks: */
    orgused = trans.used;
    blk = at;
    orgtsz = tsz = (size_t)hdsz + trans.size;
    /* Truncate if overflow: */
    if (tsz < trans.size) tsz = (size_t)EPI_OS_SIZE_T_MAX;
    if (trunc_block(fi, &blk, &tsz)) {
      if (tsz <= 0)             /* must be > 0 since B-tree checked first */
        {
          putmsg(MERR, fn, "Internal error: tsz <= 0");
          goto err;
        }
      if (blk != at)                    /* blk == at: prev blocks truncated */
        {
          putmsg(MERR, fn, "Internal error: blk != at");
          goto err;
        }
      /* truncate data size below */
    }
    /* Truncate if past EOF: */
    if (at + (EPI_OFF_T)tsz > filesz || at + (EPI_OFF_T)tsz < at)
      tsz = (size_t)(filesz - at);
    /* Truncate data if needed.  Note that it might get truncated
     * again if total size gets modified in write_block():
     */
    if ((size_t)hdsz + trans.used > tsz ||
        (size_t)hdsz + trans.used < trans.used) {
      trans.used = (tsz > (size_t)hdsz ? tsz - (size_t)hdsz : (size_t)0);
      /* could now be a free block (trans.used == 0) */
    }

    /* Resize buffer if needed: */
    if (bufsz < trans.used) {
      bufsz = trans.used;
      if (buf) free(buf);
      errno = 0;
      if ((buf = (byte *)malloc(bufsz)) == NULL) {
	MAERR(fn, bufsz);
	goto err;
      }
    }
    /* Read data: */
    if (trans.used > 0) {
      numrd = -1;
      if (EPI_LSEEK(infh, at + (EPI_OFF_T)hdsz, SEEK_SET) < (EPI_OFF_T)0L ||
	  (numrd = kdbfchk_raw_read(infh, buf, trans.used)) != trans.used) {
        rderr(fi, fn, at + (EPI_OFF_T)hdsz, trans.used);
        /* Clear unread part and continue; don't bail: */
        if ((int)numrd == -1) numrd = 0;        /* no valid data */
        memset(buf + numrd, 0, trans.used - numrd);
      }
    }
    /* Write out block, truncating if needed: */
    flags = 0;
    if (trans.used != orgused && !SaveTrunc)    /* KNG 980127 del trunc blks */
      trans.used = 0;
    if (trans.used == 0) flags |= WB_ADD;	/* it's a free block now */
    if (orgused == 0) flags |= WB_ORPHAN;	/* it was originally free */
    if ((res = write_block(fi, outfh, at, tsz, buf, &trans.used, flags)) < 0)
      goto err;
    if (trans.used != orgused) {
      /* Increment trunc/del counts: */
      if (trans.used > 0) fi->cnt[CNT_TRUNCALLOCS] += 1;
      else fi->cnt[CNT_DELALLOCS] += 1;
 badprintf(at, orgtsz, "Allocated block data truncated from 0x%04X to 0x%04X",
		orgused, trans.used);
    } else if (orgtsz != tsz - res) {
      if (trans.used) {
	fi->cnt[CNT_TRUNCSLACKS] += 1;
	badprintf(at, orgtsz, "Allocated block slack space truncated");
      } else {
	fi->cnt[CNT_TRUNCFREES] += 1;	/* wtf orphaned too */
	badprintf(at, orgtsz, "Orphaned free block truncated by 0x%04X bytes",
		  orgtsz - (tsz - res));
      }
    }
    /* Advance pointers: */
    skip = tsz;
    lastend = at + tsz;
  }
  if (meter != METERPN) meter_updatedone(meter, msz);
  goto done;

err:
  ret = 0;
done:
  if (meter != METERPN)
    {
      meter_end(meter);
      meter = closemeter(meter);
    }
  if (buf != BYTEPN) free(buf);
  return(ret);
}

static int hex_dump ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T at, size_t sz,
                          int align));
static int
hex_dump(fi, fh, at, sz, align)
KDBF_FINFO      *fi;
int             fh;
EPI_OFF_T       at;
size_t          sz;
int             align;          /* non-zero: align on 16-byte boundary */
{
  static CONST char     fn[] = "hex_dump";
  size_t                rd, rda, c, skip;
  byte                  *s, *e;
  char                  *f, asc[16 + 1], hex[3*16 + 1];
  char                  tmp[EPI_OFF_T_BITS/4 + 9];
  byte                  *buf = BYTEPN;
  int                   ret;

  hex[16] = '\0';
  errno = 0;
  if ((buf = (byte *)malloc(BufSz)) == BYTEPN)
    {
      MAERR(fn, BufSz);
      goto err;
    }
  if (EPI_LSEEK(fh, at, SEEK_SET) != at)
    {
      putmsg(MERR + FSE, CHARPN, "Cannot seek to 0x%wX: %s",
             (EPI_HUGEINT)at, strerror(errno));
      goto err;
    }
  skip = (align ? (size_t)(at & (EPI_OFF_T)0xf) : 0);
  for (c = 0; c < skip; c++) hex[3*c] = hex[3*c+1] = hex[3*c+2] = asc[c]=' ';
  sz += skip;
  if (c >= sz) goto ok;                         /* avoid flush of 0 bytes */

  while (c < sz)
    {
      rd = sz - c;
      if (rd > BufSz) rd = BufSz;
      rda = kdbfchk_raw_read(fh, buf, rd);
      if (rda != rd)
        {
          rderr(fi, fn, at, rd);
          goto err;
        }
      for (s = buf, e = buf + rda; s < e; s++, c++, at++)
        {
          if ((c & 0xf) == 0 && c > 0)  /* flush a row */
            {
            flushit:
              htsnpf(tmp, sizeof(tmp), "%8wX", (EPI_HUGEINT)(at - (EPI_OFF_T)16));
              for (f = tmp; *f != '\0'; f++) if (*f == ' ') *f = '0';
              htpf("%s: ", tmp);
              hex[3*16] = ' ';
              fwrite(hex, 1, 3*16 + 1, stdout);
              asc[16] = '\n';
              fwrite(asc, 1, 16 + 1, stdout);
            }
          sprintf(hex + 3*(c & 0xf), " %02X", (unsigned int)(*s));
          asc[c & 0xf] = (char)(*s >= ' ' && *s <= '~' ? *s : '.');
        }
    }
  if (sz > 0)
    {
      c &= 0xf;
      rd = (16 - c) & 0xf;
      at += (EPI_OFF_T)rd;
      for ( ; c & 0xf; c++) hex[3*c] = hex[3*c+1] = hex[3*c+2] = asc[c] = ' ';
      sz = 0;
      s = e = (byte *)"";
      goto flushit;
    }
ok:
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (buf != BYTEPN) free(buf);
  return(ret);
}

static size_t hex2bytebuf ARGS((char *buf, size_t sz));
static size_t
hex2bytebuf(buf, sz)
char    *buf;
size_t  sz;
/* Converts buffer `buf' (size `sz') from text hex buffer to byte values,
 * returning size or 0 on error.  `buf' is expected to be in the format
 * of -p output.
 */
{
  static CONST char     hex[] = "0123456789ABCDEFabcdef";
  byte                  *sd, *d;
  char                  *s, *eol, *eob, c1, c2;
  int                   n, line, numDigits;

  eob = buf + sz;
  sd = d = (byte *)buf;
  line = 0;

  while (buf < eob)
    {
      line++;
      for (eol = buf; eol < eob && *eol != '\n' && *eol != '\r'; eol++);
      for (s = buf; s < eol && *s != '\0' && strchr(hex, *s) != CHARPN; s++);
      if (*s == ':') buf = s + 1;               /* skip leading offset */
      else goto nextline;                       /* no offset: garbage line */
      for (numDigits = 0; buf < eol; numDigits++) /* parse each digit */
	{
          s = buf;
          while (buf < eol && (*buf == ' ' || *buf == '\t')) buf++;
          /* Skip trailing ASCII: */
          if ((eol - buf) <= numDigits) break;  /*short start, full mid line*/
          /* short final, maybe trailing space padding in ASCII: */
          if (numDigits > 0 && buf - s >= 3) break;
          /* Verify we have hex: */
          if (*buf == '\0' || strchr(hex, *buf) == CHARPN)
	    {
	    perr:
	      if (numDigits == 0) goto nextline;
              putmsg(MERR, CHARPN, "Parse error in hex dump text, line %d",
		     line);
	      return(0);
	    }
          c1 = TX_TOUPPER(*buf);
	  buf++;
          if (*buf == '\0' || strchr(hex, *buf) == CHARPN) goto perr;
	  c2 = TX_TOUPPER(*buf);
	  buf++;
	  n = (strchr(hex, c1) - hex)*16 + (strchr(hex, c2) - hex);
	  *(d++) = (byte)n;
	}
    nextline:
      htskipeol(&eol, eob);
      buf = eol;
    }
  if (d == sd)
    putmsg(MERR, CHARPN, "No parseable info in hex dump file");
  return(d - sd);
}

static int do_block ARGS((DBACT what, KDBF_FINFO *fi, int fh, const char *path,
                          EPI_OFF_T filesz, EPI_OFF_T at, size_t sz));
static int
do_block(what, fi, fh, path, filesz, at, sz)
DBACT           what;
KDBF_FINFO      *fi;
int             fh;
const char      *path;  /* for messages */
EPI_OFF_T       filesz;
EPI_OFF_T       at;
size_t          sz;
/* Does action `what' to block `at'.  If `sz' is non-0, will force
 * block to that total size (NOTE: may corrupt file), else uses actual
 * block size.
 */
{
  static CONST char     fn[] = "do_block";
  size_t                numparse;
  EPI_OFF_T             lim;
  int                   hdsz = 0, force = 1, rfh = -1, ret;
  KDBF_TRANS            trans;
  char                  *buf = CHARPN, *s;
  EPI_STAT_S            st;
  byte                  tmp[KDBF_HMAXSIZE];

  memset(&trans, 0, sizeof(KDBF_TRANS));
  if (at < (EPI_OFF_T)0 || at > filesz ||
      (sz <= (size_t)0 && at >= filesz - (EPI_OFF_T)sizeof(KDBF_START)))
    {
      putmsg(MERR + UGE, CHARPN,
 "Offset 0x%wX is invalid for KDBF file `%s' (outside file/in start pointers)",
             (EPI_HUGEINT)at, path);
      goto err;
    }
  errno = 0;
  if (EPI_LSEEK(fh, at, SEEK_SET) != at)
    {
    serr:
      putmsg(MERR + FSE, CHARPN, "Cannot seek to 0x%wX in KDBF file `%s': %s",
             (EPI_HUGEINT)at, path, strerror(errno));
      goto err;
    }
  if (sz <= (size_t)0)                          /* read the block size */
    {
      size_t    numrd;

      force = 0;
      if ((numrd = kdbfchk_raw_read(fh, tmp,KDBF_HMAXSIZE)) < KDBF_HMINSIZE ||
          (int)numrd == -1)
        {
          rderr(fi, fn, at, KDBF_HMINSIZE);
          goto err;
        }
      if ((hdsz = kdbf_proc_head(tmp, numrd, at, &trans)) <= 0)
        {
          putmsg(MERR + FRE, CHARPN,
                 "Offset 0x%wX in KDBF file `%s' is corrupt or not a valid block (give <totsz> to force)",
                 (EPI_HUGEINT)at, path);
          goto err;
        }
      sz = (size_t)trans.used;
    }
  else
    {
      /* Bug 6303: initialize `trans' if forcing `sz': */
      hdsz = 0;
      trans.used = trans.size = sz;
    }
  lim = filesz - (EPI_OFF_T)(force ? 0 : sizeof(KDBF_START));
  if (at + (EPI_OFF_T)sz > lim)
    {
      size_t    newSz = (size_t)(lim - at);
  putmsg(MWARN, CHARPN, "Total block size 0x%wX too large; truncating to 0x%wX",
         (EPI_HUGEINT)((EPI_OFF_T)sz), (EPI_HUGEINT)((EPI_OFF_T)newSz));
      sz = newSz;
    }
  switch (what)
    {
    case DBACT_DEL:
      if (write_block(fi, fh, at, (EPI_OFF_T)(trans.size + hdsz), BYTEPN, SIZE_TPN, 0) < 0)
        goto err;
      htpf("Freed block 0x%wX, total size 0x%wX\n",
           (EPI_HUGEINT)at, (EPI_HUGEINT)((EPI_OFF_T)(trans.size + hdsz)));
      break;
    case DBACT_PRINT:
      if (force)
        htpf("Raw bytes at offset 0x%wX, 0x%wX bytes:\n",
             (EPI_HUGEINT)at, (EPI_HUGEINT)((EPI_OFF_T)sz));
      else
        {
          if (trans.used == 0)
            {
              if (trans.type & KDBF_FREEBITS) s = "Free-free-tree";
              else s = "Free";
            }
          else
            {
              if (trans.type & KDBF_FREEBITS) s = "Free-tree";
              else s = "Data";
            }
          htpf("%s block at 0x%wX (next block at 0x%wX)\n", s,
               (EPI_HUGEINT)at, (EPI_HUGEINT)(at+(EPI_OFF_T)(trans.size + hdsz)));
          htpf("Total size 0x%wX = %d header + 0x%wX data + 0x%wX slack\n",
               (EPI_HUGEINT)((EPI_OFF_T)(trans.size + hdsz)),
               (int)hdsz, (EPI_HUGEINT)((EPI_OFF_T)trans.used),
               (EPI_HUGEINT)((EPI_OFF_T)(trans.size - trans.used)));
          htpf("0x%wX bytes data (0x%wX max possible) starts at 0x%wX:\n",
               (EPI_HUGEINT)((EPI_OFF_T)sz), (EPI_HUGEINT)((EPI_OFF_T)trans.size),
               (EPI_HUGEINT)(at + (EPI_OFF_T)hdsz));
          at += hdsz;
        }
      if (!hex_dump(fi, fh, at, sz, Align)) goto err;
      break;
    case DBACT_REPLACE:
      errno = 0;
      if ((rfh = open(KcReplaceFile, IMODE)) < 0)
	{
	  putmsg(MERR + FOE, CHARPN, "Cannot open hex dump input file %s: %s",
		 KcReplaceFile, strerror(errno));
	  goto err;
	}
      errno = 0;
      if (EPI_FSTAT(rfh, &st) < 0)
	{
	  putmsg(MERR + FTE, CHARPN, "Cannot stat hex dump input file %s: %s",
		 KcReplaceFile, strerror(errno));
	  goto err;
	}
      errno = 0;
      if ((buf = (char *)malloc((size_t)st.st_size + 1)) == CHARPN)
	{
	  putmsg(MERR + MAE, CHARPN, "Cannot alloc %u bytes: %s",
		 (unsigned)st.st_size + 1, strerror(errno));
	  goto err;
	}
      if (tx_rawread(TXPMBUFPN, rfh, KcReplaceFile, (byte*)buf,
                     (size_t)st.st_size, 1) != (int)st.st_size)
	goto err;
      if ((numparse = hex2bytebuf(buf, (size_t)st.st_size)) == 0) goto err;
      errno = 0;
      if (!force) at += hdsz;
      if (EPI_LSEEK(fh, at, SEEK_SET) != at) goto serr;
      if (numparse > sz)
	{
	  putmsg(MWARN, CHARPN,
     "Hex replace data is too long (0x%wX bytes, expected 0x%wX); truncating",
                 (EPI_HUGEINT)((EPI_OFF_T)numparse),
                 (EPI_HUGEINT)((EPI_OFF_T)sz));
	  numparse = sz;
	}
      else if (numparse < sz && !force)
	putmsg(MWARN, CHARPN,
               "Hex replace data is shorter than block data size");
      if (tx_rawwrite(TXPMBUFPN, fh, path, TXbool_False, (byte *)buf,
                      numparse, TXbool_False) != numparse)
	goto err;
      PF(("Wrote 0x%wX bytes at offset 0x%wX\n",
          (EPI_HUGEINT)((EPI_OFF_T)numparse), (EPI_HUGEINT)at));
      break;
    default:
      putmsg(MERR + UGE, fn, "Internal error: Unknown action %d", (int)what);
      goto err;
    }
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (rfh != -1) close(rfh);
  if (buf != CHARPN) free(buf);
  return(ret);
}

static int do_list ARGS((char *file, EPI_OFF_T off));
static int
do_list(file, off)
char            *file;
EPI_OFF_T       off;            /* optional starting offset (if not -1) */
{
  KDBF          *df;
  EPI_OFF_T     at, n, endOffset;
  size_t        dataSz, maxDataSz;
  char          *buf;

  df = kdbf_open(TXPMBUFPN, file, O_RDONLY);
  if (df == KDBFPN) return(0);
  kdbf_ioctl(df, DBF_KAI + KDBF_IOCTL_READBUFSZ, (void *)BufSz);
  if (off != (EPI_OFF_T)(-1))
    {
      htpf("Skipping to offset 0x%wX\n", (EPI_HUGEINT)off);
      if (kdbf_ioctl(df, DBF_KAI + KDBF_IOCTL_SETNEXTOFF, (void *)&off) < 0)
        {
          kdbf_close(df);
          return(0);
        }
    }
  htpf("  Offset  End offset  Header  +  Data  +  Slack = Total size\n");
  for (n = 0;
       (buf = (char *)kdbf_get(df,(EPI_OFF_T)(-1), &dataSz)) != CHARPN;
       n++)
    {
      char      tmpBuf[1024], *s;

      at = kdbf_tell(df);
      endOffset = TXkdbfGetCurrentBlockEnd(df);
      maxDataSz = TXkdbfGetCurrentBlockMaxDataSize(df);
      htsnpf(tmpBuf, sizeof(tmpBuf),
             "%#8wX %#11wX %#7wX %#8wX %#9wX %#12wX",
             (EPI_HUGEINT)at, (EPI_HUGEINT)endOffset,
             (EPI_HUGEINT)((endOffset - at) - maxDataSz),
             (EPI_HUGEINT)dataSz,
             (EPI_HUGEINT)(maxDataSz - dataSz),
             (EPI_HUGEINT)(endOffset - at));
      /* `%#X' also uppercases the `0X'; we want just digits uppercased: */
      for (s = tmpBuf; *s; s++) if (*s == 'X') *s = 'x';
      htpf("%s\n", tmpBuf);
    }
  kdbf_close(df);
  htpf("%wd data blocks\n", (EPI_HUGEINT)n);
  return(1);
}

static int scan_hdrs ARGS((KDBF_FINFO *fi, int fh, EPI_OFF_T off,
                           EPI_OFF_T filesz));
static int
scan_hdrs(fi, fh, off, filesz)
KDBF_FINFO	*fi;
int		fh;
EPI_OFF_T       off;            /* optional starting offset (if not -1) */
EPI_OFF_T       filesz;
/* Scans `fh' for probable KDBF headers and prints them.
 * Returns 0 if internal error, 1 if ok.  `filesz' is size of file `fh'.
 */
{
  static CONST char     fn[] = "scan_hdrs";
  byte		        *buf = BYTEPN, *ptr;
  EPI_OFF_T	        at, szrd, lim, rdoff, here, pend = (EPI_OFF_T)0L;
  size_t	        bufsz, skip;
  char                  *s, *w, *pw;
  KDBF_TRANS	        trans;
  int		        hdsz, ret = 1, gotone = 0;

  if (Verbosity >= 2)
    {
      PF(("Scanning for KDBF headers"));
      if (off != (EPI_OFF_T)(-1)) PF((" from offset 0x%wX", (EPI_HUGEINT)off));
      PF(("\n"));
    }

#ifdef SMALL_MEM
  bufsz = (size_t)30000;  /* don't malloc > ~30k under DOS  -KNG 951109 */
#else
  bufsz = BufSz;
#endif
  errno = 0;
  if ((buf = (byte *)malloc(bufsz)) == NULL)
    {
      MAERR(fn, bufsz);
      goto err;
    }

  if (off < (EPI_OFF_T)0L) off = (EPI_OFF_T)0L;
  if (off >= filesz)
    {
      putmsg(MERR + FSE, CHARPN, "Offset 0x%wX is beyond EOF", (EPI_HUGEINT)off);
      goto err;
    }
  /* Read a chunk of the file at a time and scan it: */
  for (at = off; at < filesz; at += rdoff)
    {
      if (EPI_LSEEK(fh, at, SEEK_SET) != at ||
          (szrd = kdbfchk_raw_read(fh, buf, bufsz)) <= (EPI_OFF_T)0 ||
          (int)szrd == -1)
        {
          rderr(fi, fn, at, bufsz);
          /* skip to likely next block, and try to read again: */
          rdoff = NEXT_BLK(at) - at;
          continue;
        }

      /* Scan the chunk: */
      ptr = buf;
      skip = 0;                                 /* shut up lint */
      lim = szrd;
      if (at + (EPI_OFF_T)lim < filesz) lim -= KDBF_HMAXSIZE;
      for (rdoff = (EPI_OFF_T)0L; rdoff <= lim; )
        {
          /* In-line version of part of kdbf_proc_head(), for speed: */
#ifdef KDBF_CHECK_OLD_F_TYPE
          if ((*ptr & KDBF_CHKSUMBITS) == KDBF_CHECK_OLD(/**/))
            {
              if ((*ptr & KDBFTYBITS) >= KDBF_NUM_TYPES) goto skip1;
            }
          else
#endif /* KDBF_CHECK_OLD_F_TYPE */
          if ((*ptr & KDBF_CHKSUMBITS) != KDBF_CHECK(wtf) ||
              (*ptr & KDBFTYBITS) >= KDBF_NUM_TYPES)
            goto skip1;

          here = at + rdoff;
          w = "";
          if (here >= filesz - (EPI_OFF_T)sizeof(KDBF_START))
            w = " (within start pointers)";
          else if (gotone && here < pend)
            w = " (error: within previous block)";
          else if (gotone && here > pend)
            w = " (error: past previous block end)";
          hdsz = kdbf_proc_head(ptr, (size_t)(szrd - rdoff), here, &trans);
          if (hdsz <= 0)
            {
              if (Verbosity >= 2)
                htpf("0x%wX error: Bad header%s\n", (EPI_HUGEINT)here, w);
              goto skip1;
            }
          gotone = 1;
          pend = here + (EPI_OFF_T)hdsz + (EPI_OFF_T)trans.size;
          pw = "";
          if (pend > filesz || pend < (EPI_OFF_T)0)
            {
              pw = " (error: past EOF)";
              if (*w == '\0') w = " (error: extends past EOF)";
            }
          else if (pend > filesz - (EPI_OFF_T)sizeof(KDBF_START))
            {
              pw = " (error: in start pointers)";
              if (*w == '\0') w = " (error: extends into start pointers)";
            }
          if (trans.used == 0)
            {
              if (trans.type & KDBF_FREEBITS) s = "Free-free-tree";
              else s = "Free";
            }
          else
            {
              if (trans.type & KDBF_FREEBITS) s = "Free-tree";
              else s = "Data";
            }
          if (Verbosity >= 1 || *w == '\0')
            {
              htpf("0x%wX %s header%s; end offset 0x%wX%s",
                   (EPI_HUGEINT)here, s, w, (EPI_HUGEINT)pend, pw);
              htpf("; 0x%X header + 0x%wX data + 0x%wX slack = total size 0x%wX\n",
                   (int)hdsz, (EPI_HUGEINT)((EPI_OFF_T)trans.used),
                   (EPI_HUGEINT)((EPI_OFF_T)(trans.size - trans.used)),
                   (EPI_HUGEINT)((EPI_OFF_T)(trans.size + hdsz)));
            }
	  if (*w != '\0' || *pw != '\0') goto skip1;
          rdoff += hdsz;
          ptr += hdsz;
          continue;
        skip1:
          rdoff++;
          ptr++;
        }
    }
  htpf("0x%wX Start pointers\n0x%wX EOF\n",
       (EPI_HUGEINT)(filesz - (EPI_OFF_T)sizeof(KDBF_START)),
       (EPI_HUGEINT)filesz);
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (buf != BYTEPN) free(buf);
  return(ret);
}

/* ========================================================================= */

static void usage ARGS((int verb));
static void
usage(verb)
int     verb;
{
  static CONST char * CONST     strs[] = {
 "Default action is to scan given <infile>s.",
 "Other actions (offset/size values are decimal or 0x hex):",
 "  -q                  Quick fix: skip orphan scan, just repair tail of file",
 "  -d <off> [<totsz>]  Delete/free block at offset <off>; <totsz> forces write",
 "                      of total size (including header) <totsz> (caution!).",
 "  -p <off> [<totsz>]  Print block data at offset <off>.  <totsz> forces size.",
 "  -r <hexfile> <off> [<totsz>]  Replace block data at offset <off> with hex",
 "                      dump data from <hexfile> (e.g. edited -p output).",
 "                      <totsz> forces total size and raw write (no KDBF header).",
 "  -l [<off>]          List data blocks' offsets and data sizes, optionally",
 "                      starting at valid offset <off> (default start of file)",
 "  -L [<off>]          Scan for any headers, starting at offset <off> (default",
 "                      start of file).  Lower -v values print less info.",
 "Options:",
 "  --install-dir[-force]" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
 "<dir> Alternate installation <dir>",
 (char *)1,
 "  --texis-conf" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
 "<file>         Alternate " TX_TEXIS_INI_NAME " <file>",
 "  -o <outfile>        Output repaired file to <outfile>",
 "  -O                  Overwrite <infile> (required for -q, -d, -r)",
 "  -s                  Save truncated data blocks instead of deleting",
 "  -k                  Assume file is KDBF even if it doesn't look like KDBF",
 "  -i                  Ignore orphaned free blocks in scan (i.e. assume bad)",
 "  -f <file>           Print non-data (free) blocks info to <file>",
 "  -b <file>           Print bad blocks info to <file>:",
 "     -n               Don't list orphaned free blocks/pages",
 "     -m <n>           Limit info to <n> messages (default 1000, 0 = no limit)",
 "  -t <dir>            Use <dir> as temporary directory for internal tree",
 "  -bufsz <n>          Use disk buffer size <n> (default 128K)",
 "  -a                  Align hex dumps on 1-byte instead of 16-byte boundary",
 "  -v <n>              Set verbosity level <n> (default 2):",
 "     0                No output except severe errors",
 "     1                Also print current filename and all corruption info",
 "     2                Also print progress meter",
 "  -M none|simple|pct  Meter type to print",
 "  -version            Print version information",
 "  -h                  Print this message",
 "Exit codes:",
 "   0                  File checks ok",
 "  23                  Incorrect usage",
 "  26                  File is not KDBF",
 "  27                  Internal error",
 "  28                  Unknown error",
 "  29                  File is corrupt",
 "  45                  Cannot write to file",
 CHARPN
  };
  CONST char * CONST    *sp;
  int           len, gap;

  htpf("KDBF File Repair Utility Copyright (c) 1995-2021 Thunderstone - EPI, Inc.\n");
  htpf("Version %d.%02d.%wd %at (%s)\n\n",
       (int)TX_VERSION_MAJOR, (int)TX_VERSION_MINOR, (EPI_HUGEINT)TxSeconds,
       "%Y%m%d", (time_t)TxSeconds, TxPlatformDesc);
  if (verb >= 1)
    {
      htpf("Usage:  %s [options] <infile> [<infile> ...]\n", ProgName);
      for (sp = strs; *sp != CHARPN; sp++)
        {
          if (*sp == (char *)1)
            {
              len = strlen(TXINSTALLPATH_VAL);
              gap = 61 - len;
              if (gap < 0) gap = 0;
              else if (gap > 18) gap = 18;
              htpf("    %*s(default is `%s')\n", gap, "", TXINSTALLPATH_VAL);
            }
          else
            htpf("%s\n", *sp);
        }
    }
  fflush(stdout);
  exit(TXEXIT_INCORRECTUSAGE);
}

static void needarg ARGS((char *s));
static void
needarg(s)
char	*s;
{
  htfpf(stderr, "%s: `%s' requires argument (-h for help)\n",
        ProgName, s);
  exit(TXEXIT_INCORRECTUSAGE);
}

static void
parse_args ARGS((int argc, char *argv[]));
static void
parse_args(argc, argv)
int	argc;
char	*argv[];
{
  char	        *s, *e;
  int	        i, errnum;
  size_t        szVal;
  EPI_OFF_T     off;
#define NA(s)	if (++i >= argc) needarg(s)

  if ((ProgName = strrchr(argv[0], PATH_SEP)) != CHARPN) ProgName++;
  else ProgName = argv[0];

  for (i = 1; (i < argc) && (*(s = argv[i]) == '-'); i++) {
    if (strcmp(s, "-version") == 0) usage(0);
    switch (s[1]) {
      case 'q':  QuickFix = 1;                          break;
      case 'o':
        NA("-o");
        OutFile = argv[i];
        break;
      case 'O':  Overwrite = 1;                         break;
      case 'd':
        NA("-d");
        What = DBACT_DEL;
      getblk:
        off = TXstrtoepioff_t(argv[i], CHARPN, &e, 0, &errnum);
        if (*e != '\0' || errnum != 0)
          {
            if (What == DBACT_LIST || What == DBACT_HDRSCAN) { i--; break; }
            goto badblk;
          }
        if (off < (EPI_OFF_T)0)
          {
          badblk:
            htfpf(stderr,
 "%s: option takes file offset and optional total size (decimal or 0x hex)\n",
                  ProgName);
            exit(TXEXIT_INCORRECTUSAGE);
          }
        TheBlk = off;
        if (i + 1 >= argc) break;
        szVal = TXstrtosize_t(argv[i + 1], CHARPN, &e, 0, &errnum);
        if (*e != '\0' || errnum != 0) break;   /* was not a size */
        i++;
        if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(szVal) ||
            szVal > (size_t)EPI_OS_SIZE_T_MAX ||
            (EPI_HUGEUINT)szVal > (EPI_HUGEUINT)EPI_OFF_T_MAX)
          {
            htfpf(stderr, "%s: total size out of range\n", ProgName);
            exit(TXEXIT_INCORRECTUSAGE);
          }
        TheBlkSz = szVal;
        break;
      case 'p':
        NA("-p");
        What = DBACT_PRINT;
        goto getblk;
      case 'r':
        NA("-r");
        KcReplaceFile = argv[i++];
        if (i >= argc) needarg(s);
        What = DBACT_REPLACE;
        goto getblk;
      case 'l':
        What = DBACT_LIST;
        if (i + 1 < argc) { i++; goto getblk; }
        break;
      case 'L':
        What = DBACT_HDRSCAN;
        if (i + 1 < argc) { i++; goto getblk; }
        break;
      case 's':  SaveTrunc = 1;                         break;
      case 'k':  AssumeKDBF = 1;			break;
      case 'i':  IgnoreOrphans = 1;			break;
      case 'f':  NA("-f");  FreeFile = argv[i];		break;
      case 'b':
        if (strcmp(s, "-bufsz") == 0)
          {
            NA("-bufsz");
            szVal = TXstrtosize_t(argv[i], CHARPN, &e, 0, &errnum);
            if (*e != '\0' || errnum != 0 || szVal < (size_t)KDBF_HMAXSIZE)
              {
                htfpf(stderr, "%s: bad buffer size %s\n", argv[i]);
                exit(TXEXIT_INCORRECTUSAGE);
              }
            BufSz = szVal;
            continue;
          }
        NA("-b");
        BadBlocksFile = argv[i];
	break;
      case 'n':  PrintOrphans = 0;			break;
      case 'm':
	NA("-m");
	if (sscanf(argv[i], "%ld", &BadBlockLimit) != 1 ||
	    BadBlockLimit < 0L) {
	  htfpf(stderr, "%s: `-m' requires nonnegative int (-h for help)\n",
                ProgName);
	  exit(TXEXIT_INCORRECTUSAGE);
	}
	break;
      case 't':  NA("-t");  TmpPath = argv[i];		break;
      case 'a':  Align = 0;                             break;
      case 'v':
        NA("-v");
        if (sscanf(argv[i], "%d", &Verbosity) != 1)
          {
            htfpf(stderr, "%s: `-v' requires int (-h for help)\n",
                  ProgName);
            exit(TXEXIT_INCORRECTUSAGE);
          }
        if (Verbosity < 0) Verbosity = 0;
        break;
      case 'M':
        NA("-M");
        MeterType = meter_str2type(argv[i], CHARPN);
        break;
      case 'H':
      case 'h':  usage(1);				break;
      default:   goto unkopt;
    }
    if (s[2]) {
    unkopt:
      htfpf(stderr, "%s: Unknown option `%s' (-h for help)\n",
            ProgName, s);
      exit(TXEXIT_INCORRECTUSAGE);
    }
  }

  if (i >= argc) usage(1);
  InFiles = argv + i;
  NumInFiles = argc - i;
  if (NumInFiles > 1)
    {
      if (OutFile != CHARPN)
        {
          htfpf(stderr,
             "%s: must specify only one input file if -o output file given\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
    }
  if (What == DBACT_DEL)
    {
      if (OutFile != CHARPN)
        {
          htfpf(stderr, "%s: -o output file and -d are mutually exclusive\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
      if (!Overwrite)
        {
          htfpf(stderr, "%s: -d requires -O (overwrite)\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
      FreeFile = CHARPN;
    }
  else if (Overwrite)
    {
      if (OutFile != CHARPN)
        {
          htfpf(stderr,
             "%s: -o output file and -O (overwrite) are mutually exclusive\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
      else if (!QuickFix && What != DBACT_REPLACE)
        {
          htfpf(stderr, "%s: -O (overwrite) only supported with -q/-d/-r\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
    }
  else if (QuickFix || What == DBACT_REPLACE)
    {
      if (OutFile != CHARPN)
        {
          htfpf(stderr,
        "%s: output file not supported with -q/-r; must use -O (overwrite)\n",
                ProgName);
          exit(TXEXIT_INCORRECTUSAGE);
        }
      if (!Overwrite)
	{
	  htfpf(stderr, "%s: must use -O (overwrite) with -q/-r\n",
                ProgName);
	  exit(TXEXIT_INCORRECTUSAGE);
	}
    }
}

static int checkfile ARGS((char *file));
static int
checkfile(file)
char    *file;
{
#define fn      CHARPN
  int		i, gf_corrupt = 0, bufrd = 0;
  TXEXIT        ret = TXEXIT_OK;
  EPI_STAT_S    st, ist, ost;
  EPI_OFF_T	filesz, neweof;
  KDBF_FINFO	info;
  KDBF_START	start;
  FILE		*ff;
  byte		buf[20];
  KDBF_TRANS	trans;
  int           zapout = 0;

  memset(&info, 0, sizeof(info));

  if (Verbosity >= 1) PF(("Checking file `%s':\n", file));

  if ((InFh = (Overwrite ? open(file, IOMODE) :
               open(file, IMODE))) < 0) {
    putmsg(MERR + FOE, fn, "Cannot open input file `%s'%s: %s",
           file, (Overwrite ? " in read/write mode" : ""), strerror(errno));
    goto err;
  }
  if (EPI_FSTAT(InFh, &ist) != 0)
    {
      putmsg(MERR + FTE, fn, "Cannot fstat `%s': %s", file, strerror(errno));
      goto err;
    }
  if (OutFile != CHARPN)
    {
      if ((OutFh = open(OutFile, OMODE)) < 0)
        {
          putmsg(MERR + FOE, fn, "Cannot open output file `%s': %s",
                 OutFile, strerror(errno));
          goto err;
        }
      if (EPI_STAT(OutFile, &ost) != 0)
        {
          putmsg(MERR + FTE, fn, "Cannot stat `%s': %s",
                 OutFile, strerror(errno));
          goto err;
        }
#ifndef _WIN32
      /* st_ino doesn't exist, and st_dev is 0, under NT: */
      if (ist.st_ino == ost.st_ino &&
          ist.st_dev == ost.st_dev)
        {
          putmsg(MERR + UGE, fn,
                 "`%s' and `%s' are the same file; use -O (overwrite)",
                 file, OutFile);
          goto err;
        }
#endif /* !_WIN32 */
    }
  else if (Overwrite)
    OutFh = InFh;
  if (BadBlocksFile != CHARPN) {
    if ((BadBlocksFh = fopen(BadBlocksFile, "wa")) == FILEPN) {
      putmsg(MERR + FOE, fn, "Cannot open bad blocks file `%s': %s",
             BadBlocksFile, strerror(errno));
      goto err;
    }
    htfpf(BadBlocksFh, "Bad blocks info for file `%s':\n\n", file);
  }

  /* See if it's FDBF: */
  if (!AssumeKDBF) {
    if (EPI_LSEEK(InFh, (EPI_OFF_T)0L, SEEK_SET) != (EPI_OFF_T)0L ||
	(bufrd = (int)kdbfchk_raw_read(InFh, buf, sizeof(buf))) < 0) {
      rderr(&info, fn, (EPI_OFF_T)0L, 1);
      goto err;         /* wtf still error? or just stumble on? KNG 960717 */
    }
    if (fdbf_proc_head(buf, bufrd, (EPI_OFF_T)0L, &trans) > 0) {
      if (Verbosity >= 1) htpf("File is FDBF (use -k to override)\n");
      ret = TXEXIT_NONKDBFINPUTFILE;
      goto fin;
    }
  }

  /* Get file size, free-tree start pointer, and see if file is empty: */
  filesz = ist.st_size;
  if ((size_t)filesz < sizeof(KDBF_START)) {
    if (!AssumeKDBF) {
      if (Verbosity >= 1)
        htpf("File is not KDBF: size too small (use -k to override)\n");
      ret = TXEXIT_NONKDBFINPUTFILE;
      goto fin;
    }
    /* (nearly) empty file; restore free-tree pointers and quit: */
  FIX_EMPTY:
    start.btree = start.free_pages = (EPI_OFF_T)0L;
    if (OutFh >= 0)
      {
        if (kdbfchk_raw_write(OutFh, &start, sizeof(start)) !=sizeof(start) ||
            (OutFh != InFh ? close(OutFh) : 0) != 0)
          {
            OutFh = -1;
            putmsg(MERR + FWE, fn, "Cannot write to output file `%s': %s",
                   OutFile, strerror(errno));
            goto err;
          }
        OutFh = -1;
      }
    if (Verbosity >= 1)
      htpf("*** Truncated/empty KDBF file: restored null free-tree pointers *** \n");
    ret = TXEXIT_CORRUPTINPUTFILE;
    goto done;
  }

  if (EPI_LSEEK(InFh, -((EPI_OFF_T)sizeof(KDBF_START)), SEEK_END) ==
      (EPI_OFF_T)(-1L) ||
      kdbfchk_raw_read(InFh, &start, sizeof(start)) != sizeof(start)) {
    rderr(&info, fn, filesz - (EPI_OFF_T)sizeof(start), sizeof(start));
    /* Assume bogus, to force "free tree corrupt" message,
     * but don't bail:   KNG 960717
     */
    start.btree = start.free_pages = (EPI_OFF_T)(-1L);
  }
  if (filesz == sizeof(KDBF_START)) {	/* empty file */
    if (start.btree != (EPI_OFF_T)0L || start.free_pages != (EPI_OFF_T)0L) {
      goto FIX_EMPTY;	/* Bogus free-tree pointers */
    }
    if (Verbosity >= 2) htpf("Empty KDBF file; nothing to do\n");
    if (OutFh >= 0 && OutFh != InFh)
      {
        close(OutFh);
        unlink(OutFile);
        OutFh = -1;
      }
    ret = TXEXIT_OK;
    goto done;
  }
  /* See if it's KDBF: */
  if (!AssumeKDBF &&
      kdbf_proc_head(buf, bufrd, (EPI_OFF_T)0L, &trans) <= 0) {
    if (Verbosity >= 1) htpf("Not KDBF file (use -k to override)\n");
    ret = TXEXIT_NONKDBFINPUTFILE;
    goto fin;
  }

  switch (What)
    {
    case DBACT_DEL:
    case DBACT_PRINT:
    case DBACT_REPLACE:
      ret = (do_block(What, &info, InFh, file, filesz, TheBlk, TheBlkSz) ?
	     TXEXIT_OK : TXEXIT_UNKNOWNERROR);
      if (FreeFile == CHARPN) goto report;
      break;
    case DBACT_LIST:
      ret = (do_list(file, TheBlk) ? TXEXIT_OK : TXEXIT_UNKNOWNERROR);
      if (FreeFile == CHARPN) goto report;
      break;
    case DBACT_HDRSCAN:
      ret = (scan_hdrs(&info, InFh, TheBlk, filesz) ? TXEXIT_OK :
             TXEXIT_UNKNOWNERROR);
      if (FreeFile == CHARPN) goto report;
      break;
    default: break;
    }

  /* Open trees.  We keep a reverse and a forward order tree because
   * there is no fbtgetprevious():  WTF WTF WTF this is time/mem consuming:
   */
  if ((info.treerev = TmpTreeRev = openfbtree(CHARPN, KDBF_BTORDER,
                        KDBF_BT_CACHE_SZ, BT_FIXED, (O_RDWR | O_CREAT),
                        BTREE_DEFAULT_HDROFF, DBFPN)) == BTREEPN ||
      (info.treefwd = TmpTreeFwd = openfbtree(CHARPN, KDBF_BTORDER,
                        KDBF_BT_CACHE_SZ, BT_FIXED, (O_RDWR | O_CREAT),
                        BTREE_DEFAULT_HDROFF, DBFPN)) == BTREEPN)
    {
      putmsg(MERR + FOE, fn, "Cannot create internal B-trees");
      goto err;
    }
  btsetcmp(info.treerev, rev_cmp);   /* so fbtgetnext() returns previous */

  /* Parse free blocks: */
  switch (get_free_stuff(&info, InFh, filesz, &start)) {
    case 1:	/* free tree/list intact */
      /* Start pointers must be valid, so skip on copy: */
      filesz -= sizeof(KDBF_START);
      gf_corrupt = 0;
      break;
    case 0:	/* corrupt */
      gf_corrupt = 1;
      break;
    case -1:	/* internal error */
    default:
      goto err;
  }

  neweof = (EPI_OFF_T)(-1);
  if (QuickFix)
    {
      /* If a non-empty free tree was read successfully, it's _highly_
       * unlikely the last block was truncated; so don't try to
       * scan for it because we might find the wrong spot:
       */
      if (!gf_corrupt &&
          (start.btree != (EPI_OFF_T)0L || start.free_pages != (EPI_OFF_T)0L))
        goto okusa;
      neweof = find_lasthdr(&info, InFh, filesz, &trans);
      switch (neweof)
        {
        case (EPI_OFF_T)(-2):                       /* error */
          goto err;
        case (EPI_OFF_T)(-1):                       /* none found */
          neweof = (EPI_OFF_T)0;
          goto truncit;
        }
      if (trans.end == filesz)                  /* ok last block */
        {
          if (gf_corrupt)
            neweof = trans.end;
          else                                  /* ok free tree */
            {
            okusa:
              if (OutFh < 0 || Overwrite)       /* nothing to do */
                goto report;
              goto err;                         /* WTF WTF WTF copy it */
            }
        }
      else                                      /* fix last block */
        {
          info.cnt[CNT_DELALLOCS] += 1;         /* WTF truncate if -s */
        }
    truncit:
      if (!Overwrite) goto report;              /* WTF what if out file... */
      /* WTF copy if not Overwrite */
      errno = 0;
      if (!TXtruncateFile(TXPMBUFPN, (Overwrite ? file : OutFile), OutFh,
                          neweof + sizeof(start)))
        goto err;
    }
  else
    {
      /* Copy the whole thing: */
      if (!copy_blocks(&info, InFh, filesz, OutFh))
        goto err;
    }

  /* Write out null free tree pointers.  Really shouldn't orphan the
   * whole free list, but reconstructing it is a pain:
   */
  if (OutFh >= 0)
    {
      start.btree = start.free_pages = (EPI_OFF_T)0L;
      errno = 0;
      if ((neweof != (EPI_OFF_T)(-1) ? EPI_LSEEK(OutFh, neweof, SEEK_SET) :
           EPI_LSEEK(OutFh, 0, SEEK_END)) < (EPI_OFF_T)0L ||
          kdbfchk_raw_write(OutFh, &start, sizeof(start)) != sizeof(start))
        {
          putmsg(MERR + FWE, fn, "Cannot write to output file: %s",
                 strerror(errno));
          goto err;
        }
    }

report:
  if (FreeFile != CHARPN) {
    if ((ff = fopen(FreeFile, "wa")) == NULL) {
      putmsg(MERR + FOE, fn, "Cannot open file `%s' to write free blocks: %s",
             FreeFile, strerror(errno));
      goto err;
    }
    if (Verbosity >= 2) PF(("Writing free list..."));
    htfpf(ff, "Non-data blocks (free blocks, tree pages, free pages) after fixup\nfor file `%s':\n\n", file);
    htfpf(ff,
#if EPI_OFF_T_BITS == 32
          "Offset     Total size (with hdr)\n"
#else
          "Offset             Total size (with hdr)\n"
#endif
          );
    kdbf_traverse_tree(info.treefwd, info.treefwd->root, 0, free_iter, ff);
    fflush(ff);
    if (fclose(ff) != 0) {
      putmsg(MERR+FCE, fn, "Cannot close `%s': %s", FreeFile, strerror(errno));
      goto err;
    }
    if (Verbosity >= 2) PF(("ok\n"));
  }

  /* Print info and set return code if corruption detected: */
  if (gf_corrupt)
    ret = TXEXIT_CORRUPTINPUTFILE;
  for (i = 0; (i < NUM_CNTS) && (info.cnt[i] == 0L); i++);
  if (i < NUM_CNTS && Verbosity >= 1) {
    puts("Bad block summary:");
    for (i = 0; i < NUM_CNTS; i++) {
      if (info.cnt[i]) {
	htpf("%8ld ", info.cnt[i]);
	htpf(CntNames[i], info.cnt[i] > 1 ? "s" : "");
	htpf("\n");
	ret = TXEXIT_CORRUPTINPUTFILE;
      }
    }
  }

 fin:
  if (BadBlocksFh != FILEPN) {
    if (BadBlockCnt == 0L)
      htfpf(BadBlocksFh, "None\n");
    else if (BadBlockCnt > BadBlockLimit)
      htfpf(BadBlocksFh, "%ld missed messages\n",
            BadBlockCnt - BadBlockLimit);
    fflush(BadBlocksFh);
    if (fclose(BadBlocksFh) != 0) {
      putmsg(MERR + FWE, fn, "Could not write/close bad blocks file `%s'",
	     BadBlocksFile);
      ret = TXEXIT_CANNOTWRITETOFILE;
    }
    BadBlocksFh = FILEPN;
  }
  if (!Overwrite)
    {
      errno = 0;
      if (EPI_STAT(file, &st) != 0)
        putmsg(MWARN + FTE, fn, "Cannot stat `%s': %s", file, strerror(errno));
      else if (st.st_size != ist.st_size ||
               st.st_mtime != ist.st_mtime)
        if (Verbosity >= 1)
          htpf(
"*** Input file `%s' was modified by other process(es) during scan: %s ***\n",
               file, (OutFh >= 0 ? "Repaired output file may be corrupt" :
                      "Scan may be invalid"));
    }

  if (What == DBACT_DEFAULT && Verbosity >= 1)
   switch (ret) {
    case TXEXIT_CORRUPTINPUTFILE:
      htpf("*** Corruption detected in input file ***\n");
      break;
    default:
      htpf("*** Internal error ***\n");
      break;
    case TXEXIT_OK:
      htpf("Input file checks ok\n");
      break;
  }
  goto done;

err:
  ret = TXEXIT_UNKNOWNERROR;
  zapout = 1;
done:
  if (info.treerev != BTREEPN) closefbtree(info.treerev);
  info.treerev = TmpTreeRev = BTREEPN;
  if (info.treefwd != BTREEPN) closefbtree(info.treefwd);
  info.treefwd = TmpTreeFwd = BTREEPN;
  errno = 0;
  if (OutFh >= 0 && close(OutFh) != 0 && !zapout)
    {
      putmsg(MERR + FCE, fn, "Cannot close output file %s: %s",
             OutFile, strerror(errno));
      ret = TXEXIT_CANNOTWRITETOFILE;
    }
  if (InFh >= 0 && InFh != OutFh) close(InFh);
  InFh = OutFh = -1;
  if (OutFile != CHARPN && zapout) unlink(OutFile);
  if (BadBlocksFh != FILEPN) fclose(BadBlocksFh);
  BadBlocksFh = FILEPN;
  return(ret);
}

int
#ifdef TEST
main(argc, argv)
#else
KCmain(argc, argv, argcstrip, argvstrip)
#endif
int	argc, argcstrip;
char	*argv[], **argvstrip;
{
#define fn      CHARPN
  TXEXIT        ret = TXEXIT_OK, res;
  int           headerType, i;

  (void)argv;
  (void)argc;

#ifdef _MSC_VER
  __try {
#endif /* _MSC_VER */
  tx_setgenericsigs();
  TXsetSigProcessName(TXPMBUFPN, "kdbfchk");
  TXaddabendcb(abendcb, NULL);

  if (*TXgetlocale() == '\0') TXsetlocale("");  /* if not set yet */
  headerType = kdbf_header_type(KDBF_FREETREE_PGSZ);
  if (headerType < 0)
    {
      ret = TXEXIT_INTERNALERROR;
      goto done;
    }
  MinFPgSz = kdbf_header_size(headerType) + KDBF_FREETREE_PGSZ;

  parse_args(argcstrip, argvstrip);

  if (BadBlocksFile != CHARPN) unlink(BadBlocksFile);
  if (FreeFile != CHARPN) unlink(FreeFile);     /* unlink first: we append */

  for (i = 0; i < NumInFiles; i++)
    {
      res = checkfile(InFiles[i]);
      if ((ret == TXEXIT_OK && res != TXEXIT_OK) ||
          res == TXEXIT_CORRUPTINPUTFILE)
        ret = res;                              /* keep the worst exit code */
    }

done:
  TXdelabendcb(abendcb, NULL);
#ifdef _MSC_VER
    }
  __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
    {
      /* TXgenericExceptionHandler() exits */
    }
#endif /* _MSC_VER */

  return(ret);                                  /* TXEXIT_... code */
}
