/* Internal defines and stuff for KDBF.
 */

#ifndef KDBFI_H
#define KDBFI_H

#include "sizes.h"
#include "dbquery.h"
#include "kdbf.h"
#include "fbtree.h"


#ifdef KDBF_HIGHIO
#  if defined(MSDOS)
#    define CIOMODE  "w+b"
#    define IOMODE   "r+b"
#    define IMODE    "rb"
#    define OMODE    "wb"
#  else
#    define CIOMODE  "w+"
#    define IOMODE   "r+"
#    define IMODE    "r"
#    define OMODE    "w"
#  endif
#else	/* !KDBF_HIGHIO */
#  if defined(MSDOS)
#    include <io.h>
#    define CIOMODE	O_RDWR|O_BINARY|O_CREAT, S_IREAD|S_IWRITE
#    define IOMODE	O_RDWR|O_BINARY, 0
#    define IMODE	O_RDONLY|O_BINARY, S_IREAD
#    define OMODE	O_WRONLY|O_CREAT|O_TRUNC|O_BINARY, S_IREAD|S_IWRITE
#  elif defined(macintosh)
#    define CIOMODE     O_RDWR|O_CREAT, 0
#    define IOMODE      O_RDWR, 0
#    define IMODE       O_RDONLY, 0
#    define OMODE       O_WRONLY|O_CREAT|O_TRUNC, 0
#  else
#    define CIOMODE	O_RDWR|O_CREAT, S_IREAD|S_IWRITE
#    define IOMODE	O_RDWR, 0
#    define IMODE	O_RDONLY, 0
#    define OMODE	O_WRONLY|O_CREAT|O_TRUNC, S_IREAD|S_IWRITE
#  endif        /* !MSDOS && !macintosh */
#endif	/* !KDBF_HIGHIO */

#define KDBF_BTREE_FLAGS	(BT_FIXED | BT_UNSIGNED)
#if defined(__alpha) && !defined(KDBF_NO_E_TYPE)
#  define KDBF_CHECK(t) 0xE0    /* KNG 970616 alignment changed */
#  define KDBF_CHECK_OLD_F_TYPE
#  define KDBF_CHECK_OLD(t)     0xF0
#else
#  define KDBF_CHECK(t) 0xF0    /* must differ from fdbf's 0xA0 */
#endif
/* xxx can't use size/used: not read yet:  ((((byte)((t)->at + (t)->size + (t)->used)) & 0x0F) << KDBF_CHKSUMSHIFT)*/

/* The following are derived from KDBF_BLOCK_SIZE and magic knowledge
 * about the btree data structures:  -KNG 951109
 */
#define KDBF_BTORDER (((KDBF_BLOCK_SIZE - sizeof(BPAGE))/sizeof(BITEM) + 1)/2)
/* Actual B-tree page size; this must be same in kdbf, kdbfchk, etc.: */
#define KDBF_BT_PGSZ    TX_FBTREE_PAGESIZE(KDBF_BTORDER)
/* B-tree page size with checksum: */
#ifdef KDBF_CHECKSUM_FREETREE
#  define KDBF_FREETREE_PGSZ	(KDBF_BT_PGSZ + sizeof(KDBF_CHKSUM))
#else
#  define KDBF_FREETREE_PGSZ	KDBF_BT_PGSZ
#endif
/* Magic number for BTREE (this must agree with B-tree code; should be a
 * define):  -KNG 951109
 */
#define KDBF_BTREE_MAGIC	0x009a9a00L

/* Size to align buffer on (note: update KDBF_PREBUFSZ_WANT too): */
#if defined(_WIN32) && !defined(_WIN64) /* JMT 2003-01-15 */
/* historical Windows-32 TX_ALIGN_BYTES was 1; sizeof(void *) is better: */
#  define KDBF_ALIGN_SIZE       (sizeof(void *))
#else
#  define KDBF_ALIGN_SIZE       TX_ALIGN_BYTES
#endif
/* (note: update KDBF_PREBUFSZ_WANT if this changes:) */
#define KDBF_PRE_ALIGN          2       /* # bytes read before alignment */
/* Rounds <n> if needed: */
/* (note: update KDBF_PREBUFSZ_WANT if this changes:) */
#define KDBF_ALIGN_UP_INT(n)	\
((((EPI_VOIDPTR_UINT)(n) + KDBF_ALIGN_SIZE-1)/KDBF_ALIGN_SIZE)*KDBF_ALIGN_SIZE)
#define KDBF_ALIGN_UP(n)	((void *)KDBF_ALIGN_UP_INT(n))
#define KDBF_ALIGN_DN(n)	((void *) \
 ((((EPI_VOIDPTR_UINT)(n))/KDBF_ALIGN_SIZE)*KDBF_ALIGN_SIZE))


/* File format similar to fdbf:
 *
 *  <--- 1 byte ---->
 *  +-+-+-+-+-+-+-+-+			chksum = 0xF  (0xE for alpha)
 *  |chksum |f| type|			f = 1 if free-tree block
 *  +-+-+-+-+-+-+-+-+			type = header type: KDBF_TYPE_xxx
 *  nothing: next block			if type == KDBFNULL, or
 *  +-+-+-+-+-+-+-+-+
 *  | used  |  size |   		if type == KDBFNIBBLE, or
 *  +-+-+-+-+-+-+-+-+
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+	if type == KDBFBYTE
 *  |     used      |      size     |	(etc. for other header types)
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  ... alignment padding (if any) ...
 *  ... block data (used bytes) ...
 *  KDBF_CHKSUM checksum (if free-tree block and KDBF_CHECKSUM_FREETREE)
 *  ... slack space (up through size bytes) ...
 *  next block starts
 *  ... etc. ...
 *  <---------- EPI_OFF_T ---------->
 *  +-+-+-+-+-+-+ ... +-+-+-+-+-+-+-+
 *  | offset of 1st free-tree page  |	B-tree of free blocks
 *  +-+-+-+-+-+-+ ... +-+-+-+-+-+-+-+
 *  |    offset of 1st free page    |	linked list of free B-tree pages
 *  +-+-+-+-+-+-+ ... +-+-+-+-+-+-+-+
 *  <EOF>
 *
 *  Free blocks stored as B-tree, with pages with f == 1, each containing
 *  pointers to free blocks (used == 0).  At EOF is EPI_OFF_T size pointer
 *  (file offset) to first free-tree page (top of B-tree).  Actual
 *  free blocks have size > 0 and used == 0.  Freed B-tree _pages_ are
 *  stored in a linked list, since adding them to the B-tree itself is
 *  hairy and incurs extra B-tree page allocs.  Each freed B-tree page
 *  has used == 0, and contains an EPI_OFF_T next pointer followed by a checksum
 *  struct.
 */


#define KDBFTYBITS		((unsigned)0x07)	/* size type */
#define KDBF_FREEBITS		((unsigned)0x08)	/* free-tree page */
#define KDBF_CHKSUMBITS		((unsigned)0xF0)	/* checksum mask */
#define KDBF_CHKSUMSHIFT	4		/* bit shift for checksum */

/* Struct tacked on to end of free-tree pages to aid finding them
 * after the KDBF file is trashed:
 */
typedef struct {
  EPI_OFF_T	offset;
  dword	chksum;		/* dword -- assumed to always be exactly 32 bits */
} KDBF_CHKSUM;
/* Struct at EOF with start pointers.  Must be smaller than KDBF_MIN_RD_SZ
 * for EOF detection in read_head():  -KNG 951109
 */
/* If this changes, change KDBF_POSTBUFSZ_WANT in kdbf.h: */
typedef struct {
  EPI_OFF_T	btree;		/* root of B-tree */
  EPI_OFF_T	free_pages;	/* 1st free B-tree page */
} KDBF_START;
#define KDBF_STARTPN    ((KDBF_START *)NULL)
#define KDBF_STARTPPN   ((KDBF_START **)NULL)


/* These must be in order of increasing data size: */
enum {	/* the type field */
  KDBFNULL,		/* 1-byte spacer */
  KDBFNIBBLE,
  KDBFBYTE,
  KDBFWORD,
  KDBFSTYPE,
  KDBF_NUM_TYPES
};

/* Structs for each size type (note defines below for size).
 * Total header sizes must be in non-decreasing order:
 */
typedef struct {
  byte	used_size;
} KDBFN;

typedef struct {
  byte	used_size[2];
} KDBFB;

typedef struct {
  epi_word	used_size[2];
} KDBFW;

typedef struct {
  size_t	used_size[2];
} KDBFS;	/* size_t determines the largest allocation */

/* (note: update KDBF_PREBUFSZ_WANT if this changes:) */
typedef union {
  KDBFN	n;
  KDBFB	b;
  KDBFW	w;
  KDBFS	s;
} KDBFALL;

/* Actual header sizes used; these include padding for alignment.
 * Header is read such that byte KDBF_PRE_ALIGN is aligned on
 * KDBF_ALIGN_SIZE boundary:
 */
#if defined(MSDOS)
#  define SIZEOF_KDBFN	1	/* for el funko compilers that disagree KNG */
#else
#  define SIZEOF_KDBFN	sizeof(KDBFN)
#endif
#define KDBF_PRE_HDR    (KDBF_PRE_ALIGN - 1)    /* # bytes before alignment */
#define KDBFN_LEN     \
  (KDBF_PRE_HDR + KDBF_ALIGN_UP_INT(SIZEOF_KDBFN - KDBF_PRE_HDR))
#define KDBFB_LEN     \
  (KDBF_PRE_HDR + KDBF_ALIGN_UP_INT(sizeof(KDBFB) - KDBF_PRE_HDR))
#define KDBFW_LEN     \
  (KDBF_PRE_HDR + KDBF_ALIGN_UP_INT(sizeof(KDBFW) - KDBF_PRE_HDR))
#define KDBFS_LEN     \
  (KDBF_PRE_HDR + KDBF_ALIGN_UP_INT(sizeof(KDBFS) - KDBF_PRE_HDR))
#define KDBF_HMINSIZE 1       /* minimum header size: KDBFNULL */
/* Max-size header (plus byte type and padding): */
/* (note: update KDBF_PREBUFSZ_WANT if this changes:) */
#define KDBF_HMAXSIZE \
  (KDBF_PRE_ALIGN + KDBF_ALIGN_UP_INT(sizeof(KDBFALL) - KDBF_PRE_HDR))
#if 0
/* old values; should use ones above that use KDBF_ALIGN_SIZE: */
#  define KDBFN_LEN     SIZEOF_KDBFN
#  define KDBFB_LEN     (sizeof(KDBFB) + 3)
#  define KDBFW_LEN     (sizeof(KDBFW) + 1)
#  define KDBFS_LEN     (sizeof(KDBFS) + 1)
#  define KDBF_HMINSIZE 1       /* minimum header size: KDBFNULL */
#  define KDBF_HMAXSIZE (1 + sizeof(KDBFALL) + 1)
#endif  /* 0 */

/* Minimum amount to read in read_head().  Must be >= KDBF_HMAXSIZE,
 * and > sizeof(KDBF_START) for EOF detection:   -KNG 951109
 */
typedef union {
  byte		foo[KDBF_HMAXSIZE];
  byte		bar[sizeof(KDBF_START) + 2];
} KDBF_MIN_RD_TYPE;
#define KDBF_MIN_RD_SZ	sizeof(KDBF_MIN_RD_TYPE)

/* Size limits for each type: */
#define KDBFNMAX	0x000F
#define KDBFBMAX	0x00FF
/* KDBFNMAX < KDBFBMAX < KDBFWMAX < KDBFSMAX <= KDBF_MAX_ALLOC <
 *   (min(EPI_OS_SIZE_T_MAX, EPI_OFF_T_MAX) - fudge)  must be true.
 * The fudge is for header sizes, etc. added to size_t vars; must not
 * cause rollover under DOS; and size_t must not rollover EPI_OFF_T:
 * -KNG 95115
 */
typedef union {
  byte	a[KDBF_HMAXSIZE + 2*KDBF_ALIGN_SIZE];
  byte	b[sizeof(KDBF_CHKSUM) + sizeof(EPI_OFF_T)];
  byte	c[KDBF_MIN_RD_SZ];
} KDBF_SMAX_FUDGE;

/* EPI_OFF_T and size_t could be any type, even bigger than a long.
 * We need to cast to the largest type available to compare them
 * accurately; we can't rely on the preprocessor:   KNG 981002
 */
#define KDBF_SIZE_T_MAX                                         \
  ((size_t)((EPI_HUGEUINT)EPI_OS_SIZE_T_MAX < (EPI_HUGEUINT)EPI_OFF_T_MAX ? \
            (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX : (EPI_HUGEUINT)EPI_OFF_T_MAX))

#define KDBFSMAX	(KDBF_SIZE_T_MAX - sizeof(KDBF_SMAX_FUDGE))
#if (EPI_OS_SIZE_T_BITS <= 16)
/* KDBFWMAX must be < KDBFSMAX always, so force it:
 * (really should just not use KDBFWORD type)  -KNG 951115
 */
#  define KDBFWMAX	(KDBFSMAX - 1)
#else
#  define KDBFWMAX	0xFFFF
#endif
/* Maximum overall request size: */
#define KDBF_MAX_ALLOC	KDBFSMAX


/* per-transaction stuff; in a separate struct since we may have
 * 2 transactions going at once (via B-tree):
 */
typedef struct {
  EPI_OFF_T		at;	/* offset of this record */
  EPI_OFF_T		end;	/* offset of end of this record + 1  */
  byte		type;	/* size type flag for the current block */
  size_t	used;	/* how much of the block is used */
  size_t	size;	/* how much can the block hold */
} KDBF_TRANS;

typedef enum KDF_tag {
  KDF_TMP       = (1 << 0),     /* temporary file */
  KDF_APPENDONLY= (1 << 1),     /* we'll only append, and no other writers */
  KDF_NOREADERS = (1 << 2),     /* there are no other readers */
  KDF_FLUSHPTRS = (1 << 3),     /* need to flush start pointers */
  KDF_READONLY  = (1 << 4),     /* file is read-only (set at open) */
  KDF_OVERWRITE = (1 << 5),     /* overwrite file (copydbf) */
  KDF_IGNBADPTRS= (1 << 6),     /* ignore bad start pointers */
  KDF_BADSTART  = (1 << 7),     /* bad start pointers detected */
  KDF_INALLOC   = (1 << 8),     /* between kdbf_begin/endalloc */
  KDF_OBRDSTART = (1 << 9),     /* read start pointers assoc. with outbuf */
  KDF_TRACE     = (1 <<10)      /* tracekdbf active for this file */
} KDF;

struct KDBF_struct {
  TXPMBUF       *pmbuf;         /* (opt.) cloned, for putmsgs */
  char	*fn;  /* the file name used for this struct */
#ifdef KDBF_HIGHIO
  FILE	*fh;  /* the file handle */
#else
  int	fh;  /* the file handle */
#endif
  EPI_OFF_T     fhcuroff;       /* current `fh' offset (-1 if unknown) */

  void	*blk;		/* internal buffer for some reads */
  size_t blksz;		/* size of block buffer */
  void	*blk_data;	/* start of block data in df->blk (ie. past header) */
  size_t blk_data_sz;	/* length of block data read (<= block used size) */
			/* buffer for quick header reads: */
  byte	hdrbuf[KDBF_MIN_RD_SZ + sizeof(EPI_OFF_T) + sizeof(KDBF_CHKSUM)];
  /* Write buffer for write_block().  Separate from `blk' so kdbf_get()
   * data stays intact after a kdbf_put():      KNG 970808
   */
  byte   tmpbuf[KDBF_HMAXSIZE + KDBF_WRITE_CHUNK_SIZE];
  byte   *outbuf;       /* write buffer */
  size_t outbufsz;      /* alloced size of `outbuf' */
  size_t outbufused;    /* amount in use */
  EPI_OFF_T  outbufoff; /* offset into file where `outbuf' data starts */
  EPI_OFF_T  outbuflastsent;    /* last atomic offset sent to outbuf */
  EPI_OFF_T  outbuflastsave;    /* last atomic offset successfully written */

  /*                            rdbufoff
   * malloc            rdbuf   rdbufstart
   *   v                 v         v
   *   +-----------------+---...---+---------------------------------------+
   *   <-KDBF_ALIGN_SIZE-X- - - - - - - - - - - rdbufsz - - - - - - - - - ->
   *                               <- - - - - -rdbufused- - - - - - - - - ->
   */
  byte   *rdbuf;        /* read buffer */
  byte   *rdbufstart;   /* start of valid data in read buffer */
  size_t rdbufsz;       /* alloced size of `rdbuf' */
  size_t rdbufused;     /* amount in use (from `rdbufstart') */
  EPI_OFF_T  rdbufoff;      /* file offset where `rdbufstart' starts */

  byte	overalloc; /* size>>overalloc amount to overallocate new blocks by */

  DBF	*pseudo_dbf;	/* pointer to fake DBF struct used in B-tree ops */
  byte	in_btree;	/* operation recursion level (>0 if in B-tree op) */
  int           callDepth;      /* overall function call depth (est.) */
  EPI_OFF_T	last_at;	/* last loc of non B-tree op */
  EPI_OFF_T	last_end;	/*   "" plus total size of block (w/header) */
  size_t        lastBlkSz;      /* last block size */

#define KDBF_LS_NUM	4
  size_t last_sz[KDBF_LS_NUM];	/* size of last 4 kdbf_get()s */
  int	ls_cur;			/* last_sz pointer */

  KDBF_START	start;	/* current start pointers, if known and valid */
  EPI_OFF_T     start_off;     /* offset of start pointers (eg. EOF-sizeof) */

  /* These are B-tree buffers saved across close/openfbtree calls,
   * and are manipulated in kfbtree.c:
   */
  void	*btree_btree, *btree_cache, *btree_his;
  void	*btree_cache_pages[KDBF_BT_CACHE_SZ];
  int   btree_cache_prebufsz, btree_cache_postbufsz;

  KDF    flags;
  size_t prebufsz, postbufsz;   /* amount caller overalloc'd write buffer */
  size_t btree_prebufsz, btree_postbufsz;       /* "" for internal B-tree */

  int   lasterr;        /* nonzero: last op was error */

  EPI_HUGEUINT  reads, readbytes, writes, writebytes, lseeks, truncates;
  EPI_HUGEUINT  kreads, kreadbytes, kwrites, kwritebytes, kfrees, kvalids;
  EPI_HUGEUINT  mallocs, mallocbytes, frees, memcpys, memcpybytes;
  EPI_HUGEUINT  memmoves, memmovebytes, memsets, memsetbytes;
  EPI_HUGEUINT  skippedlseeks;
};	/* typedef'd in kdbf.h */

#define KDBF_NORM_PTR(p)        ((char *)p)

/* ----------------------------- prototypes -------------------------------- */

/* Internal stuff: */
DBF	*kdbf_pseudo_opendbf ARGS((KDBF *df));
DBF	*kdbf_pseudo_closedbf ARGS((DBF *dbf));
void    kdbf_freebtreefields ARGS((KDBF *df));
EPI_OFF_T	kdbf_get_freetree_root ARGS((KDBF *df));
int	kdbf_put_freetree_root ARGS((KDBF *df, EPI_OFF_T root));
void	*kdbf_pseudo_calloc ARGS((KDBF *df, void **pbuf, size_t n));
int     kdbf_read_start ARGS((KDBF *df));
void    kdbf_zap_start ARGS((KDBF *df));

/* Faster partially-inline version of kdbf_contalloc(): */
#define KDBF_CONTALLOC(df, buf, sz, err)                        \
  if ((df)->outbufused + (sz) <= (df)->outbufsz)                \
    {                                                           \
      memcpy((df)->outbuf + (df)->outbufused, (buf), (sz));     \
      (df)->outbufused += (sz);                                 \
    }                                                           \
  else if (!kdbf_contalloc(df, buf, sz)) err

/* in kdbfutil.c: */
dword kdbf_checksum_block ARGS((void *buf, size_t size));
int kdbf_header_type ARGS((size_t n));
int kdbf_header_size ARGS((int type));
int kdbf_proc_head ARGS((byte *buf, size_t len, EPI_OFF_T at, KDBF_TRANS *trans));
int kdbf_create_head ARGS((KDBF *df, byte *buf, KDBF_TRANS *trans));

typedef void (kdbf_freepage_cb) ARGS((EPI_OFF_T at, void *data));
int kdbf_traverse_free_pages ARGS((KDBF *df, kdbf_freepage_cb *cb,
				   void *data));

/* ------------------------------- tracing: ------------------------------- */

/* TXtraceKdbf: trace KDBF calls
 * NOTE: if these change, see tracekdbf in source/html/common.src:
 * 0x00000001: after open()/close()
 * 0x00000002:       select() placeholder (ignored)
 * 0x00000004: after read()
 * 0x00000008: after write()
 * 0x00000010: after ioctl() (seek-significant)
 * 0x00000020: after ioctl() (other)
 * 0x00000040: after read() data
 * 0x00000080: after write()/ioctl() data
 * 0x00000100:
 * 0x00000200:
 * 0x00000400:
 * 0x00000800:
 * Below flags control when above flags should apply:
 * 0x00001000: after  user calls
 * 0x00002000: after  internal calls
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * 0x00010000: before open()/close()
 * 0x00020000:        select() placeholder (ignored)
 * 0x00040000: before read()
 * 0x00080000: before write()
 * 0x00100000: before ioctl() (seek-significant)
 * 0x00200000: before ioctl() (other)
 * 0x00400000: before read() data
 * 0x00800000: before write()/ioctl() data
 * 0x01000000:
 * 0x02000000:
 * 0x04000000:
 * 0x08000000:
 * Below flags control when above flags should apply:
 * 0x10000000: before  user calls
 * 0x20000000: before  internal calls
 * TXtraceKdbfFile: which files to apply TXtraceKdbf to
 *   [[/]some/db/][file.tbl]
 * If no db, applies to all DBs
 * If no file, applies to all files
 * File can be "SYS" or "USR" for all system or non-system files
 */
#define TXTK_OPEN               0x000001
/*           SELECT */
#define TXTK_READ               0x000004
#define TXTK_WRITE              0x000008
#define TXTK_IOCTLSEEK          0x000010
#define TXTK_IOCTLOTHER         0x000020
#define TXTK_READ_DATA          0x000040
#define TXTK_WRITE_DATA         0x000080
/* ... */
/* ... */
/* ... */
/* ... */
#define TXTK_USERCALLS          0x001000
#define TXTK_INTCALLS           0x002000
#define TXTK_BEFORE(bits)       ((bits) << 16)

#define TXTK_TRACE_BEFORE_START(kf, bits)               \
  if ((TXtraceKdbf & ((bits) | TXTK_BEFORE(bits))) &&   \
      ((kf)->flags & KDF_TRACE))                        \
    {                                                   \
      if ((TXtraceKdbf & TXTK_BEFORE(bits)) &&          \
          (TXtraceKdbf & TXTK_BEFORE((kf)->callDepth == 1 ? \
                     TXTK_USERCALLS : TXTK_INTCALLS)))  \
        {
#define TXTK_TRACE_BEFORE_FINISH()                              \
        }                                                       \
      txtkStart = TXgetTimeContinuousFixedRateOrOfDay();        \
      TXseterror(0);                                            \
    }
#define TXTK_TRACE_AFTER_START(kf, bits)                \
  if ((TXtraceKdbf & (bits)) &&                         \
      ((kf)->flags & KDF_TRACE) &&                      \
      (TXtraceKdbf & ((kf)->callDepth==1 ? TXTK_USERCALLS : TXTK_INTCALLS))) \
    {                                                           \
      TX_PUSHERROR();                                           \
      txtkFinish = TXgetTimeContinuousFixedRateOrOfDay();       \
      txtkTime = txtkFinish - txtkStart;                        \
      if (txtkTime < 0.0 && txtkTime > -0.001)                  \
        txtkTime = 0.0;
#define TXTK_TRACE_AFTER_FINISH()   TX_POPERROR(); }
#define TXTK_TRACE_VARS          double txtkStart = -1.0, txtkFinish = -1.0, \
    txtkTime = -1.0;
#define TXTK_TRACE_TIME()       txtkTime

#define TXTK_INITFMT            "%.*s%s%s(0x%lx=%s"
#define TXTK_INITARGS(kf)       (kf)->callDepth - 1, TXtraceKdbfDepthStr, \
  ((kf)->in_btree ? TXtraceKdbfBtreeOp : ""), fn, (long)(kf),   \
  TXbasename((kf)->fn)
#define TXTK_OFFFMT             "%#wx%s"
#define TXTK_OFFARGS(off)       ((off) != (EPI_OFF_T)(-1) ? (EPI_HUGEUINT)(off) :\
  (EPI_HUGEUINT)0), ((off) != (EPI_OFF_T)(-1) ? "" : "-1")

extern CONST char       TXtraceKdbfBtreeOp[];
extern CONST char       TXtraceKdbfDepthStr[];

/* more externs are in texint.h */

#endif /* !KDBFI_H */
