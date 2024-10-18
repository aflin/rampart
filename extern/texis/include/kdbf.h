#ifndef KDBF_H
#define KDBF_H


/* ---------------------------- Config stuff ----------------------------- */
/* Define for checksumming in free-tree blocks:
 * (not tested without this  -KNG 951109)
 */
#define KDBF_CHECKSUM_FREETREE

/* Define to check checksums when reading free-tree blocks:
 * (not tested without this  -KNG 951109)
 */
#define KDBF_CHECK_CHECKSUMS

/* Define to read and check next block's header on getdbf() (usually doesn't
 * incur another read(), just a slightly larger one):
 */
#define KDBF_CHECK_NEXT

/* Size to make B-tree pages (approximately).  Must be well less than
 * (max(size_t) - sizeof(KDBF_CHKSUM)):
 */
#define KDBF_BLOCK_SIZE	1024

/* Cache size must be large enough to hold as many pages as the depth
 * of the tree (or the B-tree code may barf).  8 should be plenty for
 * ~1G of free blocks with KDBF_BLOCK_SIZE at 1024:
 */
#define KDBF_BT_CACHE_SZ	8

/* Amount to start read-ahead guess at.  Must be >= 0.  Dynamically resized.
 */
#define KDBF_INIT_READ_CHUNK_SIZE	3072

/* Max amount of write data (including header) to combine in one write().
 * Larger values save write()s but increase memcpy() usage.  Must be >= 0.
 */
#define KDBF_WRITE_CHUNK_SIZE	128

/* Define for fopen(), etc. calls (not tested) or undefine for open():
 */
#undef KDBF_HIGHIO

/* Define to replace standard fbtcmp() function with one that does not
 * cast long to int (bugfix):
 */
#define KDBF_FIX_BTCMP

/* See Makefile for other debugging defines */
/* --------------------------- End config stuff ---------------------------- */

/* Sanity checks: */
#if KDBF_BLOCK_SIZE < 1024
#  undef KDBF_BLOCK_SIZE
#  define KDBF_BLOCK_SIZE	1024	/* keep a sensible minimum */
#endif
#if defined(KDBF_CHECK_CHECKSUMS) && !defined(KDBF_CHECKSUM_FREETREE)
#  define KDBF_CHECKSUM_FREETREE
#endif
/* ------------------------------------------------------------------------- */

typedef struct KDBF_struct	KDBF;	/* defined in kdbfi.h */
#define KDBFPN  ((KDBF *)NULL)


KDBF  *kdbf_close ARGS((KDBF *df));
KDBF  *kdbf_open  ARGS((TXPMBUF *pmbuf, char *filename, int flags));
void   TXkdbfStatsMsg(TXPMBUF *pmbuf);
int    kdbf_free  ARGS((KDBF *df, EPI_OFF_T at));
EPI_OFF_T  kdbf_alloc ARGS((KDBF *df, void *buf, size_t n));
int     TXkdbfSetPmbuf(KDBF *df, TXPMBUF *pmbuf);

EPI_OFF_T  kdbf_beginalloc ARGS((KDBF *df));
int        kdbf_contalloc ARGS((KDBF *df, byte *buf, size_t sz));
size_t     kdbf_undoalloc ARGS((KDBF *df, byte **bufp));
EPI_OFF_T  kdbf_endalloc ARGS((KDBF *df, size_t *szp));

EPI_OFF_T  kdbf_put   ARGS((KDBF *df, EPI_OFF_T at, void *buf, size_t sz));
void  *kdbf_get   ARGS((KDBF *df, EPI_OFF_T at, size_t *psz));
void  *kdbf_aget  ARGS((KDBF *df, EPI_OFF_T at, size_t *psz));
size_t kdbf_readchunk ARGS((KDBF *df, EPI_OFF_T at, byte *buf, size_t sz));
size_t kdbf_nextblock ARGS((KDBF *df, EPI_OFF_T *at, byte **buf, size_t *bsz,
                            byte **data, EPI_OFF_T *dat, size_t *dtot));
size_t kdbf_read ARGS((KDBF *df, EPI_OFF_T at, size_t *off, void *buf, size_t sz));
EPI_OFF_T  kdbf_tell  ARGS((KDBF *df));
EPI_OFF_T  TXkdbfGetCurrentBlockEnd ARGS((KDBF *df));
size_t     TXkdbfGetCurrentBlockMaxDataSize(KDBF *df);
char  *kdbf_getfn ARGS((KDBF *df));
int    kdbf_getfh ARGS((KDBF *df));
int    TXkdbfReleaseDescriptor ARGS((KDBF *df));
int    TXkdbfGetLastError ARGS((KDBF *df));
void   kdbf_setoveralloc ARGS((KDBF *df, int ov));
int    kdbf_valid ARGS((KDBF *df, EPI_OFF_T at));
int    kdbf_ioctl ARGS((KDBF *df, int ioctl, void *data));
int    kdbf_flush ARGS((KDBF *df));
int    kdbf_getoptimize ARGS((void));
int    kdbf_setoptimize ARGS((int flags, int set));

/* I(sym, argtype, seek-significant)
 * argtype: 0 = int  1 = boolean  2 = EPI_OFF_T ptr  3 = void
 */
#define KDBF_IOCTL_SYMS_LIST                                    \
I(UNKNOWN,      3, 1)   /* also placeholder for 0 */            \
I(PREBUFSZ,     0, 0)   /* size before data in put/alloc */     \
I(POSTBUFSZ,    0, 0)   /* size after data in put/alloc */      \
I(APPENDONLY,   1, 0)   /* we're only going to append */        \
I(NOREADERS,    1, 0)   /* there are no other readers */        \
I(WRITEBUFSZ,   0, 0)   /* write buffer when append-only */     \
I(SEEKSTART,    3, 1)   /* next get(-1) from 2nd block */       \
I(READBUFSZ,    0, 0)   /* read buffer during locks */          \
I(GETNEXTOFF,   2, 0)   /* get probable get(-1) offset */       \
I(SETNEXTOFF,   2, 1)   /* set next get(-1) offset */           \
I(OVERWRITE,    1, 1)   /* overwrite file for copydbf */        \
I(IGNBADPTRS,   1, 1)   /* ignore bad start pointers */         \
I(FREECURBUF,   3, 0)   /* free current block's mem (done w/it) */

typedef enum KDBF_IOCTL_tag
{
#undef I
#define I(sym, argtype, seek)   KDBF_IOCTL_##sym,
KDBF_IOCTL_SYMS_LIST
#undef I
  KDBF_IOCTL_NUM                                /* must be last */
}
KDBF_IOCTL;
#define KDBF_IOCTLPN    ((KDBF_IOCTL *)NULL)

/* Minimum recommended values for PRE/POSTBUFSZ ioctls:
 * Note: These are at user level; see kfbtree.c for some internal use:
 */
#define KDBF_PREBUFSZ_WANT      24      /* >= KDBF_HMAXSIZE */
#define KDBF_POSTBUFSZ_WANT     16      /* >= sizeof(KDBF_START|KDBF_CHKSUM)*/

#define KDBF_MIN_READCHUNK_SZ   34      /* KDBF_HMAXSIZE+sizeof(KDBF_START) */

extern int      TxKdbfQuickOpen, TxKdbfIoStats, TxKdbfVerify;
extern char     *TxKdbfIoStatsFile;

#endif /* !KDBF_H */
