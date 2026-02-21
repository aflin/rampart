#include "txcoreconfig.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef _WIN32
#  include <io.h>
#else /* !_WIN32 */
#  include <sys/time.h>
#  include <sys/resource.h>
#endif /* !_WIN32 */
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  undef getcwd
#  include <unistd.h>
#endif
#ifdef EPI_HAVE_MMAP
#  include <sys/mman.h>
#endif
#include "texint.h"
#include "btree.h"
#include "kdbf.h"
#include "kdbfi.h"              /* for KDBF_CONTALLOC() */
#include "pile.h"
#include "merge.h"
#include "fdbi.h"
#include "meter.h"
#include "cgi.h"                /* for htsnpf() */


/* -------------------------------- Config --------------------------------- */
#define WTIX_DEFMEMLIMIT_MB     16
#define WTIX_MINMEMLIMIT_MB     1
#define WTIX_DEFMMAPLIMIT_MB    128
#define WTIX_MINMMAPLIMIT_MB    1
#define WTIX_SLACKMEM_MB        16
#define WTIX_BTREEPCT           100  /* % full to make B-tree pages (0-100) */
#define WTIX_DEFDATBUFSZ        (32*1024)       /* > sizeof(FDBIWI) */
#define WTIX_INITKLOCS          (32*1024)       /* initial kloc buf size */
#define WTIX_MAX_SAVE_BUF_LEN   (1*1024*1024)   /* realloc down if > */
#define WTIX_SANITY             /* sanity checks */
/* see below for some vars too */
/* ------------------------------------------------------------------------- */

/* PBR 970704 Wrote the Texis text index code for full inversion.
   Notes (these are PBR's original, may have changed since then -KNG 980302)

 The structure of an inverted index entry:

 abs-record-id  record-block-size n-offsets [abs-offset,delta-offset......] 
 [ rel-record-id  record-block-size [n-offsets abs-offset,delta-offset......] ] 
 abs-record-id       32 the full record-id of the first matching record
 record-block-size   32 how many bytes to skip to next record id
 n-offsets           32 word frequency within this record ( how many offsets )
 abs-offset          32 full word position within record
 delta-offset        32 word distance from occurance of this word
 rel-record-id       32 distance from last record in the list

 The linked list array works by storing the index of the next occurance
 of a word into the position of the current word. This array is used to
 store word position data while rexlex is matching words.

 I  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
 Array[I] 2  3  5  10 9  7  13 23 12 11 15 16 14 18 19 17 20 22 21 24 0  25 0  0  0 
 Word[I] a  a  a  b  a  c  c  d  a  b  b  a  c  c  b  a  a  c  b  a  b  c  d  a  c 

   recid(32),nwords(32),wordsize(32),nklocs(32)
   wordloc(32),word\0,wordloc(32),word\0,wordloc(32),word\0,...
   klocs(nklocs*32)

   General program  flow:

   open index data structure
   while there's data to index:
   get record id and data
   for each matching lex item
   insert lex item into itree
   record word offset into list
   for each word in itree
   build inverted index entry
   check to see if too much mem is used
   Yes: merge data and write it out

  ----------------------------------------------------------------------------
  KNG 980420 Final file format.
    []   encloses parts only present in full inversion index (INDEX_FULL)
    {}   encloses parts only present for Version 2+ indexes for
         single-recid-occurence words with nlocs <= TxFdbiMaxSingleLocs
    <>   encloses parts only present for Version 3+ indexes  KNG 000420
         (VSH7 encodings, .dat size in B-tree)
    %%   encloses parts only present for indexslurp optimization,
         in intermediate piles

  Word dictionary stored in B-tree; each record is:

    word nul (%VSH-lasttok% <VSH-dat-size> VSH-ndocs [VSH-nlocs]) |
             {[<VSH7-loc-data> | VSL-loc-data ...]}

  ndocs = total number of records word appears in
  nlocs = total number of appearances of word in all records
          (KNG 000420 Version 3+ nlocs is stored as nlocs - ndocs)
  VSH-dat-size = size of .dat payload data (only present if not a
                 single-recid word)
  VSH-lasttok = last token occurence of word

  KNG 991104: If B-tree loc is negative, the word occurs in only one
  recid, whose token is -loc - 2 (-1 is EOF, -2 is reserved for future
  use).  VSH-ndocs and VSH-nlocs are then not present, and if full
  inversion, VSL-loc-data follows instead in the B-tree.  Only
  single-recid words with nlocs <= TxFdbiMaxSingleLocs are stored this
  way; rest are in .dat.  Also _P.tbl has Version >= 2.  Saves
  space/reads from .dat.

  If B-tree loc is >= 0, it points to offset in KDBF .dat file with position
  info, one KDBF record per word.  For Version <= 2, each record is:

    VSH-1st-token         [VSH-loc-data-size VSL-loc-data ...]
    VSH-next-token-offset [VSH-loc-data-size VSL-loc-data ...]
    ...

  Tokens are assigned in recid order, 1 == first recid, 2 == second,
  etc.  .tok file is linear array of recids and aux fld data,
  e.g. recid is at offset (token-1)*(sizeof(RECID) + sizeof(auxfld)).
  auxfld size may be 0.

  KNG 000413 Version 3 index .dat format:

    VSH7-1st-token         [VSH7-loc-data ...]
    VSH7-next-token-offset [VSH7-loc-data ...]
    ...

  First byte of each token VSH7 has high bit on; no other byte does.
  This lets us jump far ahead in the data and sync to a token VSH7
  (KNG 000420 doesn't work: tokens are relative...)  as well as
  getting rid of VSH-loc-data-size (20%+ .dat space saved).

  VSL is Variable-Size Long:  unsigned 30-bit integer, stored in 1 to 4 bytes
  VSH is Variable-Size Huge:  unsigned 60-bit integer, stored in 1 to 8 bytes
  VSH7 is Variable-Size Huge, 7-bits: unsigned 42-bit integer, stored in
     1 to 7 bytes, with high bit of each byte unused and available as flag.

  Intermediate format (in-memory piles):

    word nul (VSH|VSH7)-token [(VSL|VSH7)-loc-data ...]
  */


#ifndef RECIDPN
#  define RECIDPN       ((RECID *)NULL)
#endif
#ifndef DBTBLPN
#  define DBTBLPN       ((DBTBL *)NULL)
#endif
#ifndef BTLOCPN
#  define BTLOCPN       ((BTLOC *)NULL)
#endif
#ifndef S_ISDIR
#  define S_ISDIR(m)    ((m) & S_IFDIR)
#endif

/* Macros to clear the padding after recid/auxdata in the token buffer.
 * Not strictly necessary, but keeps the token file "clean" so a
 * byte-compare of same-live-data token files works (for tests):
 */
#define CLRRECIDPAD(d)                                  \
  if (sizeof(RECID) < FDBI_TOKEL_RECIDSZ)               \
    memset((byte *)(d) + sizeof(RECID), 0, FDBI_TOKEL_RECIDSZ - sizeof(RECID))
#define CLRAUXPAD(d, asz, tsz)                          \
  if (FDBI_TOKEL_RECIDSZ + (asz) < (tsz))               \
    memset((byte *)(d) + FDBI_TOKEL_RECIDSZ + (asz), 0, \
           (tsz) - (FDBI_TOKEL_RECIDSZ + (asz)))

/* ------------------------------------------------------------------------- */

typedef struct WTN_tag  WTN;    /* word tree node */
struct WTN_tag
{
  WTN           *h;             /* hi node */
  WTN           *l;             /* low node */
  byte          red;
  int           cnt;            /* 0 means deleted! */
  size_t        len;            /* length of `s' data */
  size_t        lastkloc;
  size_t        firstkloc;
#ifdef WTREE_HUGEUINT_REVCMP
  struct
#else /* !WTREE_HUGEUINT_REVCMP */
  union
#endif /* !WTREE_HUGEUINT_REVCMP */
  {
    EPI_HUGEUINT h;             /* align/size for WTREE_HUGEUINT_[REV]CMP */
    byte        s[sizeof(EPI_HUGEUINT)];   /* note: size is pad for ...REVCMP */
  }
  u;
};
#define WTNPN   ((WTN *)NULL)

typedef struct WTREE_tag        /* word tree */
{
  WTN           *root;          /* the root of the tree */
  WTN           *z;             /* PBR 11-08-91 */
  WTN           zdummy;         /* PBR 11-10-91 */
  dword         *kloc;          /* holds the 'linked list' of key locations */
  size_t        nklocs;         /* total number of key locs */
  size_t        maxklocs;       /* total number of available key locs */
  int           (*usercall) ARGS((void *, WTN *));
  void          *userptr;
}
WTREE;
#define WTREEPN ((WTREE *)NULL)

struct WTIX_tag
{
  void          *rexobj;        /* RLEX or FFS object */
  FDBI_GETHITFUNC *gethit;      /* getrlex() or getrex() */
  FDBI_GETLENFUNC *getlen;      /* rlexlen() or rexsize() */
  WTREE         *tr;            /* the inverting tree */
  RECID         recid;          /* current table recid during initial index */
  dword         curloc;         /* current word loc within `recid' */
  long          totvslerrs;     /* count of VSL errors */
  MERGE         *m;
  MERGE         *tokenMerge;
  TXbool        inTokenMergeFinish;
  EPI_HUGEINT   numTokenMergeOrgDelBeforeFinish;
  KDBF          *datdf;         /* handle to docs/locs data file */
  byte          *datbuf;        /* output buffer for current word to "" */
  size_t        datbufsz;       /*   size alloced */
  size_t        datbufoff;      /*   offset of actual data in datbuf */
  size_t        datbufused;     /*   size in use (not including datbufoff) */
  BTREE         *bt;            /* B-tree with word dictionary */
  byte          *btbuf;         /* temp buffer to write to B-tree */
  size_t        btbufsz;        /*   size alloced */
  byte          *lwrbuf;        /* temp buffer to lower and align words */
  size_t        lwrbufsz;       /*   size alloced */

  size_t        auxfldsz;       /* size of per-row auxfld data (as passed) */
  size_t        tokelsz;        /* size of tok buf element (RECID + auxfld) */
  int           tokfh;          /* file handle to token -> recid file (.tok) */
  char          *tokfn;         /* path of token file */
  byte          *tokbuf;        /* alloc'd or mmap'd buffer */
  size_t        tokbufsz;       /* size of `tokbuf' */
  size_t        tokbufNum;      /* # tokens in `tokbuf' (if ...Load'ed) */
  EPI_OFF_T     tokbuffirst;    /* (token-1) of first recid in `tokbuf' */
  EPI_OFF_T     tokbuflast;     /* (token-1) of recid at end (outside) buf */
  EPI_OFF_T     token;          /* current token value */
  int           tokorgfh;       /* original index token file handle */
  char          *tokorgfn;      /* original index token file name */
  byte          *tokbuforg;     /* all original recids (may not include aux)*/
  size_t        tokbuforgnum;   /* #tokens in tokbuforg == last tok _in_ buf*/
  size_t        tokbuforgelsz;  /* size of _tokbuforg_ el. (may != tokelsz) */
  byte          *tokbuforgtmp;  /* part/all org. recids+aux (may=tokbuforg) */
  size_t        tokbuforgtmpsz; /* allocated size of `tokbuforgtmp' */
  EPI_OFF_T     tokbuforgtmprd; /* total #tokens read via `tokbuforgtmp' */
  int           tokorgismmap;
  int           tokbufIsMmap;   /* nonzero: `tokbuf' is mmap'd not malloc'd */
  EPI_OFF_T     tokorgsz;       /* file size of original token file */
  RECID         prevrecid;      /* previous recid sent to token file */

  EPI_OFF_T     outtoken;       /* current table token during output */
  BTBM          bm;
  byte          *mi;            /* temp item buffer */
  size_t        misz;           /*   its current size */
  FDBIWI        wi;             /* word count info during output */
  TXMMPARAMTBLINFO paramTblInfo;
  TXMMPARAMTBLINFO orgIdxParamTblInfo;  /* original-index info at update */
  FDBIWI        srcIdxWordInfo; /* word count info from wtix_getnextorg() */
  EPI_OFF_T     srcIdxLastToken;/* last token for word "" */
  char          *curword;       /* current word during output (if !NULL) */
  WTIXF         flags;
  char          **noise;        /* list of noise words not to include */
  BTLOC         *del;           /* deleted recids, for update */
  byte          *new;           /* new recids + auxflds, for update */
  size_t        delsz, newsz;   /* size of buffers */
  size_t        ndel, nnew;     /* # of recids in each */
  size_t        curnew;         /* current new recid being read for insert */
  size_t        curinsnew;      /*   ""     ""   ""  actually inserted */
  size_t        curdel;         /* current del recid read (for token merge) */
  size_t        curinsdel;      /*   ""     ""   ""  actually present */
  BTREE         *btorg;         /* original B-tree dictionary (for update) */
  FDBIX         *fxorg;         /* original data file (for update) */
  KDBF          *dforg;         /* original data file handle (for update) */
  char          *wdorg;         /* current word */
  size_t        wdorgsz;        /*   size alloced */
  size_t        wdorglen;       /*   current word length (including nul) */
  PILE          *curorg;        /* buffer for current word/recid data */
  size_t        curorgsz;       /*   size alloced */
  dword         *locsorg;       /* loc data translate buffer */
  size_t        locsorgsz;      /*   size alloced */
  EPI_OFF_T     estindexsize;   /* estimated final index size */
  INDEXSTATS    stats;          /* statistics */
  int           orgversion;     /* MM version of ..org fields (update/inter) */
  byte *        (*orgoutvsh) ARGS((byte *, EPI_HUGEUINT));
  byte *        (*orginvsh) ARGS((byte *, EPI_HUGEUINT *));
  size_t        (*orglinkstovsl) ARGS((dword *,dword,byte *,size_t *,long *));
  size_t        (*orglocstovsl) ARGS((dword *, size_t, byte *, long *));
  int           (*orgvsltolocs) ARGS((byte *, size_t, dword *));
  int           (*orgcountvsl) ARGS((byte *, size_t));
  byte *        (*newoutvsh) ARGS((byte *d, EPI_HUGEUINT n));
  byte *        (*newinvsh) ARGS((byte *s, EPI_HUGEUINT *np));
  size_t        (*newlinkstovsl) ARGS((dword *,dword,byte *,size_t *,long *));
  size_t        (*newlocstovsl) ARGS((dword *, size_t, byte *, long *));
  int           (*newvsltolocs) ARGS((byte *, size_t, dword *));
  int           (*newcountvsl) ARGS((byte *, size_t));
  EPI_OFF_T     tblsize;        /* size of table being indexed (if > 0) */
  METER         *meter;         /* progress meter, if requested */
  EPI_OFF_T     metersize;      /* total size for meter (table or new data) */
  char          *prevlocale;    /* previous locale set before open */
  TXfdbiIndOpts options;                        /* other settings */

  RECID         *tblrecids;     /* table recid list (for verify) */
  size_t        curtblrecid, numtblrecids;
};

/* Pile object for intermediate merge: */
typedef struct WPILE_tag        WPILE;
#define WPILEPN         ((WPILE *)NULL)

struct WPILE_tag
{
  PILE          hdr;            /* must be first */
  WPILE         *org;           /* original (first) pile in chain */
  int           refcnt;         /* reference count (org only) */
  WPILE         *next;          /* next pile in chain (all) */
  WPILE         *last;          /* last pile in chain (org only) */
  PILEF         flags;          /* flags */
  WTIX          *wxorg;         /* original WTIX object */
  char          *path;          /* file path */
  WTIX          *wx;            /* our private WTIX object */
  EPI_HUGEINT   nitems;         /* # items written to us */
};

static PILE  *openwpile ARGS((int flags, size_t bufsz, void *wxorg));
static PILE  *closewpile ARGS((PILE *wptr));
static int    wpile_put ARGS((PILE *wptr, PILE *src));
static int    wpile_get ARGS((PILE *wptr));
static PILE  *wpile_next ARGS((PILE *wptr));
static int    wpile_flip ARGS((PILE *wptr));
static size_t wpile_npiles ARGS((PILE *wptr));
static EPI_HUGEINT wpile_nitems ARGS((PILE *wptr));

static CONST PILEFUNCS  WPileFuncs =
{
  closewpile,
  wpile_put,
  wpile_get,
  wpile_next,
  wpile_flip,
  wpile_npiles,
  wpile_nitems,
  NULL                                          /* mergeFinished() */
};

/* Pile object for final merge (create and update): */

typedef struct BMPILE_tag
{
  PILE  hdr;
  WTIX  *wx;
}
BMPILE;
#define BMPILEPN        ((BMPILE *)NULL)

static int      bmpile_put ARGS((PILE *bp, PILE *src));
static int      bmpile_putupdate ARGS((PILE *bp, PILE *src));
static int      bmpile_putslurp ARGS((PILE *bp, PILE *src));
static int      bmpile_putupdateslurp ARGS((PILE *bp, PILE *src));
static int      bmpile_mergeFinishedUpdate(PILE *pile);


static CONST char       IndexWord[] = "(index update)";
#define IndexWord       ((char *)IndexWord)
static CONST char       CantFstat[] = "Cannot fstat `%s': %s";
static CONST char       CantDel[] = "Cannot delete `%s': %s";
size_t          FdbiWriteBufSz = (128*1024);    /* see also setprop.c */
int             TxIndexSlurp = 1;               /* see also setprop.c */
int             TxIndexAppend = 1;              /* see also setprop.c */
int             TxIndexWriteSplit = 1;          /* see also setprop.c */


/* TxFdbiVersion (indexversion): _P.tbl index version number.
 * version 1: original index (also w/no version number)
 * version 2: single-recid words stored in B-tree only, no .dat info
 * version 3: "" plus 7-bit VSHs, no loc data size VSH in .dat EXPERIMENTAL
 * NOTE: set version and max single locs only with fdbi_setversion()
 * and fdbi_setmaxsinglelocs(); see also setprop.c.
 * NOTE: these may be overridden via TXindOpts:
 */
int             TxFdbiVersion = 2;              /* default version */
int             TxFdbiMaxSingleLocs = 8;        /* Version < 2: -1 */


int
TXfdbiApplyVersion(newVersion, curVersion, curMaxSingleLocs)
int     newVersion;     /* (in) new version to set */
int     *curVersion;    /* (in/out) current version, maybe updated */
int     *curMaxSingleLocs;      /* (in/out) current maxsinglelocs; updated */
/* Applies new Metamorph index version `newVersion' to `*curVersion'/
 * `*curMaxSingleLocs'.  Version 0 sets default.
 * Returns 0 on error (with message).
 */
{
  switch (newVersion)
    {
    case 1:
      *curMaxSingleLocs = -1;
      break;
    case 0:
      newVersion = 2;
      /* fall through */
    case 2:
    case 3:
      *curMaxSingleLocs = 8;
      break;
    default:
      putmsg(MERR + UGE, CHARPN,
             "Cannot set Metamorph index version %d: Unknown/invalid",
             newVersion);
      return(0);
    }
  if (newVersion == 3)
    putmsg(MWARN, CHARPN,
           "Version 3 Metamorph index is experimental: use with caution");
  *curVersion = newVersion;
  return(1);
}

int
fdbi_setversion(vers)
int     vers;
/* Sets global default Metamorph index version (overridable via `WITH ...'
 * options).
 * Returns 0 on error.
 */
{
  return(TXfdbiApplyVersion(vers, &TxFdbiVersion, &TxFdbiMaxSingleLocs));
}

int
TXfdbiApplyMaxSingleLocs(newMaxSingleLocs, curVersion, curMaxSingleLocs)
int     newMaxSingleLocs;       /* (in) new maxsinglelocs to set */
int     curVersion;             /* (in) current version */
int     *curMaxSingleLocs;      /* (in/out) current setting; updated */
{
  if (newMaxSingleLocs >= 0 && curVersion < 2)
    {
      putmsg(MERR + UGE, __FUNCTION__,
           "Invalid indexmaxsingle value (%d) for Metamorph index version %d",
             newMaxSingleLocs, curVersion);
      return(0);
    }
  *curMaxSingleLocs = newMaxSingleLocs;
  return(1);
}

int
fdbi_setmaxsinglelocs(locs)
int     locs;
/* Sets global default Metamorph index max-single-locs
 * (overridable via `WITH ...' options).
 * Returns 0 on error.
 */
{
  return(TXfdbiApplyMaxSingleLocs(locs, TxFdbiVersion, &TxFdbiMaxSingleLocs));
}


typedef int     (WTREECB) ARGS((void *usr, WTN *ts));

/************************************************************************

                             BEGIN OF WTREE.C
                             
This code is derived from xtree.c. I needed different functionality
in some places and less diversity in others. PBR 970704
*************************************************************************/

static WTN *rotate ARGS((byte *s, size_t len, WTN *x));
static WTN *split ARGS((WTREE *tr, byte *s, size_t len, WTN *gg, WTN *g,
                        WTN *p, WTN *x));

/************************************************************************/
                  /*  wtree comparison function */
#define WTNCMP_NORM(s, len, x)                                            \
  ((_i = memcmp((void *)(s), (void *)(x)->u.s, TX_MIN((len), (x)->len)))  \
   == 0 ? (int)(len) - (int)(x)->len : _i)
/* >>> NOTE: WTNCMP_PREP() must be called for each new value of s <<< */
#ifdef WTREE_HUGEUINT_CMP
/* Faster compare for big-endian machines that is usually inline.
 * NOTE: `s' and x->s must be 0-padded at least EPI_HUGEUINT wide, and aligned.
 * >>> NOTE: See also merge.c/mergeaux.c and fheap.c/fheapaux.c <<<
 */
#  define WTNCMP_DECL() int _i
#  define WTNCMP_PREP(s)
#  define WTNCMP(s, len, x)                                               \
  (*(EPI_HUGEUINT *)(s) < (x)->u.h ? -1 : (*(EPI_HUGEUINT *)(s) > (x)->u.h ? 1 :  \
   WTNCMP_NORM(s, len, x)))
#elif defined(WTREE_HUGEUINT_REVCMP)
/* Faster compare for little-endian machines that is usually inline.
 * NOTE: `s' must be 0-padded at least EPI_HUGEUINT wide.
 * Slower than big-endian version.
 */
#  define WTNCMP_DECL() int _i; EPI_HUGEUINT _h
#  if EPI_HUGEUINT_BITS == 32
#    define WTNCMP_PREP(s)                                                \
  ((byte *)&_h)[0] = s[3];                                                \
  ((byte *)&_h)[1] = s[2];                                                \
  ((byte *)&_h)[2] = s[1];                                                \
  ((byte *)&_h)[3] = s[0]
#  elif EPI_HUGEUINT_BITS == 64 
#    define WTNCMP_PREP(s)                                                \
  ((byte *)&_h)[0] = s[7];                                                \
  ((byte *)&_h)[1] = s[6];                                                \
  ((byte *)&_h)[2] = s[5];                                                \
  ((byte *)&_h)[3] = s[4];                                                \
  ((byte *)&_h)[4] = s[3];                                                \
  ((byte *)&_h)[5] = s[2];                                                \
  ((byte *)&_h)[6] = s[1];                                                \
  ((byte *)&_h)[7] = s[0]
#  else /* EPI_HUGEUINT_BITS != 32 && EPI_HUGEUINT_BITS != 64 */
error; hand code something;
#  endif /* EPI_HUGEUINT_BITS != 32 && EPI_HUGEUINT_BITS != 64 */
#  define WTNCMP(s, len, x)                                               \
  (_h < (x)->u.h ? -1 : (_h > (x)->u.h ? 1 : WTNCMP_NORM(s, len, x)))
#else /* !WTREE_HUGEUINT_CMP && !WTREE_HUGEUINT_REVCMP */
#  define WTNCMP_DECL() int _i
#  define WTNCMP_PREP(s)
#  define WTNCMP(s, len, x)     WTNCMP_NORM(s, len, x)
#endif /* !WTREE_HUGEUINT_CMP && !WTREE_HUGEUINT_REVCMP */

/************************************************************************/

static void freewtn ARGS((WTN *ts, WTN *z));
static void
freewtn(ts, z)
WTN     *ts, *z;
{
  if (ts != z)
    {                           /* PBR 07-05-93 chged to !=z from !=ts */
      if (ts->h != z) freewtn(ts->h, z);
      if (ts->l != z) freewtn(ts->l, z);
      ts = TXfree(ts);
    }
}

/************************************************************************/

static WTREE *closewtree ARGS((WTREE *tr));
static WTREE *
closewtree(tr)
WTREE   *tr;
{
  if (tr == WTREEPN) goto done;

  if (tr->root != WTNPN) freewtn(tr->root, tr->z);
  if (tr->kloc != NULL) tr->kloc = TXfree(tr->kloc);
  tr = TXfree(tr);
done:
  return(WTREEPN);
}

/************************************************************************/

static WTREE *openwtree ARGS((void));
static WTREE *
openwtree()
{
  WTREE *tr;

  TXseterror(0);
  tr = (WTREE *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(WTREE));
  if (tr == WTREEPN) goto err;
  tr->kloc = DWORDPN;
  TXseterror(0);
  /* include padding: */
  tr->root = (WTN *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(WTN));
  if (tr->root == WTNPN) goto err;

  tr->z = &tr->zdummy;
  tr->z->l = tr->z->h = tr->z;                          /* PBR 11-08-91 */
  tr->z->red = 0;

  tr->maxklocs = tr->nklocs = 0;
  TXseterror(0);
  tr->kloc = (dword *)TXmalloc(TXPMBUFPN, __FUNCTION__,
                               WTIX_INITKLOCS*sizeof(dword));
  if (tr->kloc == NULL) goto err;
  tr->maxklocs = WTIX_INITKLOCS;

  tr->root->h = tr->root->l = tr->z;                    /* PBR 11-08-91 */
  /* rest of tr->root cleared by calloc */
  goto done;

err:
  tr = closewtree(tr);
done:
  return(tr);
}

/************************************************************************/

static WTN *
rotate(s, len, x)                /* page 225 sedgewick v2 */
byte    *s;
size_t  len;
WTN     *x;
{
  WTN   *c, *gc;
  WTNCMP_DECL();

  WTNCMP_PREP(s);
  if (WTNCMP(s, len, x) < 0)
    c = x->l;
  else
    c = x->h;

  if (WTNCMP(s, len, c) < 0)
    {
      gc = c->l;
      c->l = gc->h;
      gc->h = c;
    }
  else
    {
      gc = c->h;
      c->h = gc->l;
      gc->l = c;
    }

  if (WTNCMP(s, len, x) < 0)
    x->l = gc;
  else
    x->h = gc;

  return(gc);
}

/************************************************************************/

static WTN *
split(tr, s, len, gg, g, p, x)        /* page 226 sedgewick v2 */
WTREE   *tr;
byte    *s;
size_t  len;
WTN     *gg, *g, *p, *x;
{
  WTNCMP_DECL();
  int   gCmp;

  x->red = 1;
  x->l->red = 0;
  x->h->red = 0;
  if (p->red)
    {
      g->red = 1;
      WTNCMP_PREP(s);
      gCmp = WTNCMP(s, len, g);
      if ((gCmp < 0) != (WTNCMP(s, len, p) < 0))
        p = rotate(s, len, g);
      x = rotate(s, len, gg);
      x->red = 0;
    }
  tr->root->h->red = 0;
  return(x);
}

/************************************************************************/

static int putkloc ARGS((WTREE *tr, WTN *x, dword loc));
static int
putkloc(tr, x, loc)
WTREE   *tr;
WTN     *x;
dword   loc;            /* word location */
/* Add a key to the key location index.
 * Returns 0 on error (out of mem), 1 if ok.
 */
{
  static TXATOMINT      allocfailed = 0;
  dword                 *newklocs;
  size_t                n, n2;

  if ((tr->nklocs + 2) > tr->maxklocs)                  /* must realloc */
    {
      n = tr->maxklocs + (tr->maxklocs >> 1) + 2;
      if (n < tr->maxklocs || n > EPI_OS_SIZE_T_MAX / sizeof(dword)
#if EPI_OS_SIZE_T_BITS > 32
          || n >= ((size_t)1 << 32)
#endif
          )
        {
          putmsg(MERR + MAE, __FUNCTION__, "Word loc buffer overflow");
          goto err;
        }
#ifdef EPI_REALLOC_FAIL_SAFE
      if (tr->kloc != DWORDPN)
        /* note TXPMBUF_SUPPRESS: silence errors, we retry on error: */
        newklocs = (dword *)TXrealloc(TXPMBUF_SUPPRESS, __FUNCTION__,
                                      tr->kloc, n*sizeof(dword));
      else
#endif /* EPI_REALLOC_FAIL_SAFE */
        /* note TXPMBUF_SUPPRESS: silence errors, we retry on error: */
        newklocs = (dword *)TXmalloc(TXPMBUF_SUPPRESS, __FUNCTION__,
                                     n*sizeof(dword));
      if (newklocs == DWORDPN)                  /* 50% overalloc failed */
        {                                       /* try smaller chunk (~3%) */
          n2 = tr->maxklocs + (tr->maxklocs >> 5) + 2;
          TXseterror(0);
#ifdef EPI_REALLOC_FAIL_SAFE
          if (tr->kloc != DWORDPN)
            newklocs = (dword *)TXrealloc(TXPMBUFPN, __FUNCTION__, tr->kloc,
                                          n2*sizeof(dword));
          else
#endif /* EPI_REALLOC_FAIL_SAFE */
            newklocs = (dword *)TXmalloc(TXPMBUFPN, __FUNCTION__,
                                         n2*sizeof(dword));
          if (newklocs == DWORDPN)              /* smaller chunk failed too */
            goto err;
          else if (!allocfailed)                /* success but note failure */
            {
              putmsg(MWARN + MAE, __FUNCTION__,
"Low memory: could not alloc %wku bytes of memory, alloced %wku bytes instead",
                     (EPI_HUGEUINT)n*sizeof(dword),
                     (EPI_HUGEUINT)n2*sizeof(dword));
              allocfailed = 1;
            }
          n = n2;
        }
#ifndef EPI_REALLOC_FAIL_SAFE
      memcpy(newklocs, tr->kloc, tr->nklocs*sizeof(dword));
      tr->kloc = TXfree(tr->kloc);
#endif /* !EPI_REALLOC_FAIL_SAFE */
      tr->kloc = newklocs;
      tr->maxklocs = n;
    }

#ifdef WTIX_UNIQUE_LOCS
  /* Old-style loc list.  Note that we ignore `loc': it's nklocs. */
  tr->kloc[x->lastkloc] = tr->nklocs;
  tr->kloc[tr->nklocs] = 0;
  x->lastkloc = tr->nklocs;
  tr->nklocs += 1;
#else /* !WTIX_UNIQUE_LOCS */
  /* There could be multiple occurences of different words at the
   * _same_ word position, due to overlapping index expression hits.
   * So we need two klocs per word loc (actual loc plus pointer to
   * next) instead of one (pointer to next): KNG 990108
   */
  tr->kloc[x->lastkloc + 1] = tr->nklocs;       /* prev -> us */
  tr->kloc[tr->nklocs] = loc;                   /* our data */
  tr->kloc[tr->nklocs + 1] = 0;                 /* us -> NULL pointer */
  x->lastkloc = tr->nklocs;
  tr->nklocs += 2;                              /* we used data + pointer */
#endif /* !WTIX_UNIQUE_LOCS */
  return(1);
err:
  return(0);
}

/******************************************************************/

static int putwtree ARGS((WTREE *tr, byte *s, size_t len, dword loc));
static int
putwtree(tr, s, len, loc)       /* page 221 sedgewick v2 */
WTREE   *tr;
byte    *s; /* item NOTE: must be aligned and 0-pad >= sizeof(EPI_HUGEUINT) */
size_t  len;    /* length of the item to be added */
dword   loc;    /* word location (counter) */
/* Returns 1 if added, 0 if failed.
 */
{
  WTN                   *gg, *g, *p, *z, *x;
  int                   cmp;
  size_t                sz;
  WTNCMP_DECL();

  /* Note: Bug 905 applies here too; zero-length words are lost */

  z = tr->z;
  g = p = x = tr->root;
  WTNCMP_PREP(s);

  do
    {
      gg = g;
      g = p;
      p = x;
      cmp = WTNCMP(s, len, x);
      if (cmp == 0)
        {
          if (!putkloc(tr, x, loc)) return(0);
          x->cnt += 1;
          return(1);
        }
      else if (cmp < 0)
        x = x->l;
      else
        x = x->h;
      if (x->l->red && x->h->red)       /* split nodes on the way down */
        x = split(tr, s, len, gg, g, p, x);
    }
  while (x != z);

  /* alloc and init new node */
#ifdef WTREE_HUGEUINT_CMP
  if (len < sizeof(x->u)) sz = sizeof(WTN);
  else sz = (sizeof(WTN) - sizeof(x->u)) + len;
#elif defined(WTREE_HUGEUINT_REVCMP)
  if (len < sizeof(x->u.s)) sz = sizeof(WTN);
  else sz = (sizeof(WTN) - sizeof(x->u.s)) + len;
#else /* !WTREE_HUGEUINT_CMP && !WTREE_HUGEUINT_REVCMP */
  sz = (sizeof(WTN) - sizeof(x->u)) + len;
#endif /* !WTREE_HUGEUINT_CMP && !WTREE_HUGEUINT_REVCMP */
  TXseterror(0);
  if ((x = (WTN *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sz)) == WTNPN)
    return(0);

  x->firstkloc = tr->nklocs;
  x->lastkloc = tr->nklocs;

  if (!putkloc(tr, x, loc))
    {
      x = TXfree(x);
      return(0);
    }

  x->l = z;
  x->h = z;
  x->cnt = 1;
  memcpy(x->u.s, s, len);                       /* PBR 06-15-93 */
#ifdef WTREE_HUGEUINT_REVCMP
  /* Copy initial EPI_HUGEUINT bytes to `h', reversed since little-endian.
   * Assumes padding in x->u.s (due to above oversized calloc()).
   * Will be int-compared in WTNCMP():
   */
#  if EPI_HUGEUINT_BITS == 32
  ((char *)&x->u.h)[0] = x->u.s[3];
  ((char *)&x->u.h)[1] = x->u.s[2];
  ((char *)&x->u.h)[2] = x->u.s[1];
  ((char *)&x->u.h)[3] = x->u.s[0];
#  elif EPI_HUGEUINT_BITS == 64
  ((char *)&x->u.h)[0] = x->u.s[7];
  ((char *)&x->u.h)[1] = x->u.s[6];
  ((char *)&x->u.h)[2] = x->u.s[5];
  ((char *)&x->u.h)[3] = x->u.s[4];
  ((char *)&x->u.h)[4] = x->u.s[3];
  ((char *)&x->u.h)[5] = x->u.s[2];
  ((char *)&x->u.h)[6] = x->u.s[1];
  ((char *)&x->u.h)[7] = x->u.s[0];
#  else
  error; hand code something;
#  endif
#endif /* WTREE_HUGEUINT_REVCMP */
  x->len = len;
  if (cmp < 0)
    p->l = x;
  else
    p->h = x;
  x = split(tr, x->u.s, x->len, gg, g, p, x);
  return(1);
}

/************************************************************************/
   /*  WTF: doesn't actually del anything, just sets its counts to 0 */

static void delwtree ARGS((WTREE *tr, byte *s, size_t len));
static void
delwtree(tr, s, len)
WTREE   *tr;
byte    *s;
size_t  len;
{
  WTN   *x = tr->root;
  int   cmp;
  WTNCMP_DECL();

  WTNCMP_PREP(s);
  do
    {
      cmp = WTNCMP(s, len, x);
      if (cmp == 0)
        {
          x->cnt = 0;
          return;
        }
      else if (cmp < 0)
        x = x->l;
      else
        x = x->h;
    }
  while (x != tr->z);
}

/************************************************************************/

     /* removes (zero's the entries) a string list from the wtree */

static void delwtreesl ARGS((WTREE *tr, byte **sl));
static void
delwtreesl(tr, sl)
WTREE   *tr;
byte    **sl;   /* an array of ptrs to strings, the last one is empty ("") */
/* NOTE: strings must be aligned, and 0-padded to at least EPI_HUGEUINT in size.
 */ 
{
  for ( ; **sl != '\0'; sl++)
    delwtree(tr, *sl, strlen((char *)(*sl)));
}

/************************************************************************/

static int walkwtn ARGS((WTREE *tr, WTN *ts));
static int                      /* PBR 05-08-93 added this function */
walkwtn(tr, ts)
WTREE   *tr;
WTN     *ts;
/* Walks WTN node.  Bails ASAP on error (returning 0); could be EOF
 * somewhere, don't keep generating errors.
 */
{
  if (ts == tr->z) return(1);
  if (!walkwtn(tr, ts->l)) return(0);
  if (ts->cnt > 0)
    {
      if (!tr->usercall(tr->userptr, ts)) return(0);
    }
  return(walkwtn(tr, ts->h));
}

/************************************************************************/

static int walkwtree ARGS((WTREE *tr, WTREECB *cb, void *usr));
static int
walkwtree(tr, callback, usr)    /* PBR 05-08-93 added this function */
WTREE   *tr;
int     (*callback) ARGS((void *, WTN *));
void    *usr;
/* Bails ASAP on error, returning 0.
 */
{
  tr->usercall = callback;
  tr->userptr = usr;
  if (!walkwtn(tr, tr->root->l) ||
      !walkwtn(tr, tr->root->h))
    return(0);
  return(1);
}

/************************************************************************/

/************************************************************************
Start WTIX: WTREE text indexer code
*************************************************************************/

char *
TXwtixGetLiveTokenPath(wx)
WTIX    *wx;
{
  return(wx->tokorgfn != CHARPN ? wx->tokorgfn : wx->tokfn);
}

char *
TXwtixGetNewTokenPath(wx)
WTIX    *wx;
{
  /* `wx->tokfn' is the Tnnn.tok file iff `wx->tokorgfn' is set
   * (after wtix_setupdname() called):
   */
  return(wx->tokorgfn != CHARPN ? wx->tokfn : CHARPN);
}

int
TXwtixGetTokenDescriptor(wx)
WTIX    *wx;
{
  return(wx->tokfh);
}

size_t
TXwtixGetAuxDataSize(wx)
WTIX    *wx;
{
  return(wx->auxfldsz);
}

static char *wtix_livename ARGS((WTIX *wx, size_t *n));
static char *
wtix_livename(wx, n)
WTIX    *wx;
size_t  *n;
{
  char  *s;

  s = (wx->tokorgfn != CHARPN ? wx->tokorgfn : wx->tokfn);
  *n = strlen(s) - strlen(FDBI_TOKSUF);
  return(s);
}

static int wtix_addmerge ARGS((void *usr, WTN *ts));
static int
wtix_addmerge(usr, ts)
void    *usr;
WTN     *ts;
/* WTREE callback: creates item for current (word, token, loc data)
 * row and adds to merge.  Returns 0 on error.
 */
{
  WTIX          *wx = (WTIX *)usr;
  size_t        fwsz, a;
  byte          *d, *c;

  fwsz = ts->len + 1;                                   /* +1 for nul */
  a = fwsz + VSH_BOTHMAXLEN;
  if (wx->flags & WTIXF_FULL)
    a += sizeof(dword)*wx->tr->nklocs;                  /* max mem needed */
  if (wx->misz < a && !fdbi_allocbuf(__FUNCTION__, (void **)&wx->mi,
                                     &wx->misz, a))
    goto err;
  d = wx->mi;
  memcpy((char *)d, (char *)ts->u.s, ts->len);
  d[ts->len] = '\0';                                    /* wtf if binary? */
  d += fwsz;
  c = wx->newoutvsh(d, (EPI_HUGEUINT)wx->token);
  if (c == d)                                           /* outvsh() error */
    {
      c = (byte *)wtix_livename(wx, &a);
      putmsg(MERR, __FUNCTION__, "Bad token for index `%.*s'", a, (char *)c);
      goto err;
    }
  if (wx->flags & WTIXF_FULL)                           /* locs if inverted */
    c += wx->newlinkstovsl(wx->tr->kloc, (dword)ts->firstkloc, c, &a,
                           &wx->totvslerrs);
  return(merge_newitem(wx->m, wx->mi, (c - wx->mi)));
err:
  return(0);
}

/* Item-compare function for merge, now a macro.  Since the word is
 * first in the block, and VSHs are stored MSB first, we can get away
 * with a memcmp() of the data and save a separate strcmp(), invsh()
 * and compare.  KNG 000324
 * >>> NOTE: see also fheap_deletetop_wtix(), fdbi_btcmp(), merge, etc. <<<
 */
#define WTIX_CMP(a, b)  \
 memcmp((a)->blk, (b)->blk, (a)->blksz < (b)->blksz ? (a)->blksz : (b)->blksz)

/* --------------------------- Token manipulation -------------------------- */

static int wtix_flushtokens ARGS((WTIX *wx));
static int
wtix_flushtokens(wx)
WTIX    *wx;
/* Flushes token -> recid file buffer.  Returns 0 on error.
 */
{
  size_t        n;

  n = wx->tokelsz*(size_t)(wx->token - wx->tokbuffirst);
  if (n == 0) goto ok;                          /* nothing to do */
  if (!(wx->flags & WTIXF_VERIFY))              /* no output file if verify */
    {
      if (wx->tokbufIsMmap)
        {
          /* sanity check; `tokbuf' should only be mmap()'d after
           * wtix_finish(), for ALTER TABLE ... COMPACT use of
           * TXwtixGetNewRecidAndAuxData():
           */
          putmsg(MERR, __FUNCTION__,
                 "Internal error: tokbuf for index `%s' is mmap()'d",
                 wx->tokfn);
          return(0);
        }
      if (tx_rawwrite(TXPMBUFPN, wx->tokfh, wx->tokfn, TXbool_False,
                      wx->tokbuf, n, TXbool_False) != n)
        return(0);                              /* WTF truncate file? */
    }
  n = (size_t)(wx->token - wx->tokbuffirst);
  wx->tokbuffirst += (EPI_OFF_T)n;
  wx->tokbuflast += (EPI_OFF_T)n;
ok:
  return(1);
}

int
TXwtixCreateNextToken(wx, recid, auxfld)
WTIX    *wx;
RECID   recid;
void    *auxfld;
/* Creates new wx->token for `recid' and adds `recid' and `auxfld' to
 * token file.  Used during index creation, and token file merge
 * during update.  Returns 0 on error.
 */
{
  size_t        idx, bsz, l, r, i, row;
  byte          *d;
  BTLOC         *p;
  int           cmp;
  char          *what;

  if (wx->tokbufIsMmap)
    {
      /* `tokbuf' mmap() is not expected until ALTER TABLE ... COMPACT
       * calls, after wtix_finish():
       */
      putmsg(MERR, __FUNCTION__,
             "Internal error: tokbuf for index `%s' is mmap()'d", wx->tokfn);
      return(0);
    }

  if (TXgetoff2(&recid) <= TXgetoff2(&wx->prevrecid))   /* bad recid */
    {
      /* Out-of-order recids indicate real badness (merge/heap failure): */
      if (TXgetoff2(&recid) < TXgetoff2(&wx->prevrecid) ||
          wx->token <= (EPI_OFF_T)0)
        {
          putmsg(MERR, __FUNCTION__,
   "Out-of-order recid 0x%wx (after 0x%wx) sent to token file `%s' during %s",
                 (EPI_HUGEINT)TXgetoff2(&recid),
                 (EPI_HUGEINT)TXgetoff2(&wx->prevrecid), wx->tokfn,
                 (wx->tokenMerge ? "token merge" : "indexing"));
          return(0);
        }
      /* Duplicates can arise from a bad new/delete list, e.g. a recid
       * present in the new list and token file, but not the delete
       * list, as per 3dbindex.c rev 1.85 bug.  Could also be bad dups
       * in the new list, but wtix_btree2list() should take care of that.
       * (Could also be a valid recid inserted while index was being created
       * or updated but before that recid was indexed: we cannot distinguish
       * that from an error so don't report a warning normally.  WTF)
       * If we're updating the index, try to recover here by adding
       * the recid to the delete list (for proper token math later)
       * and not adding it to the token file (no dup).   KNG 000309
       * >>>>> NOTE: delete list is being updated by tpile_getorg() too <<<<<
       */
      if (!(wx->flags & WTIXF_UPDATE) || !wx->tokenMerge)
        {                                       /* cannot fix */
          putmsg(MERR, __FUNCTION__,
                 "Duplicate recid 0x%wx sent to token file `%s'",
                 (EPI_HUGEINT)TXgetoff2(&recid), wx->tokfn);
          return(0);
        }
      if (FdbiTraceIdx >= 1 || (wx->flags & WTIXF_VERIFY))
        putmsg(MINFO, __FUNCTION__,
               "Duplicate recid 0x%wx sent to token file `%s'; %s",
               (EPI_HUGEINT)TXgetoff2(&recid), wx->tokfn,
  ((wx->flags & WTIXF_VERIFY) ? "fixable at index update" : "attempting fix"));
      if (wx->curinsdel >= wx->curdel)          /* need to make room */
        {
          bsz = (wx->ndel + 1)*sizeof(BTLOC);
          if (!fdbi_allocbuf(__FUNCTION__, (void **)&wx->del, &wx->delsz, bsz))
            {                                   /* wtf array lost */
              wx->curdel = wx->curinsdel = wx->ndel = 0;
              return(0);
            }
          if (wx->curdel < wx->ndel)
            memmove(wx->del + (wx->curdel + 1), wx->del + wx->curdel,
                    (wx->ndel - wx->curdel)*sizeof(BTLOC));
          wx->curdel++;
          wx->ndel++;
        }
      /* KNG 010307 Back up to correct position, which may be earlier: */
      for (p = wx->del + wx->curinsdel; p > wx->del; p--)
        {
          if (TXgetoff2(&recid) > TXgetoff2(p - 1)) break;
          *p = p[-1];
        }
      *p = recid;                               /* insert at correct place */
      wx->curinsdel++;
      /* We need the new instead of old-token aux data in the token file,
       * so update the aux data if this recid is the new one (prev was
       * likely old-token).  wtf drill-o-rama:
       */
      if (!(wx->flags & WTIXF_VERIFY) &&        /* not verifying and */
          wx->auxfldsz > 0 &&                   /*   aux-data type index and */
          ((byte *)auxfld >= wx->new &&         /*   auxfld is from new list */
           (byte *)auxfld < wx->new + wx->nnew*wx->tokelsz))
        {
          if (wx->token > wx->tokbuffirst)      /* prev still in buffer */
            {
              idx = (size_t)((wx->token - (EPI_OFF_T)1) - wx->tokbuffirst);
              d = wx->tokbuf + idx*wx->tokelsz + FDBI_TOKEL_RECIDSZ;
              memcpy(d, auxfld, wx->auxfldsz);
            }
          else                                  /* already written */
            {
              if (wx->token != wx->tokbuffirst)
                {
                  putmsg(MERR, __FUNCTION__,
                         "Internal error: bad tokbuffirst");
                  return(0);
                }
              TXseterror(0);
              if (EPI_LSEEK(wx->tokfh,
                       (EPI_OFF_T)FDBI_TOKEL_RECIDSZ - (EPI_OFF_T)wx->tokelsz,
                       SEEK_CUR) == (EPI_OFF_T)(-1))
                {
                badseek:
                  putmsg(MERR + FSE, __FUNCTION__,
                         "Cannot seek in token file `%s': %s",
                         wx->tokfn, TXstrerror(TXgeterror()));
                  return(0);
                }
              if (tx_rawwrite(TXPMBUFPN, wx->tokfh, wx->tokfn, TXbool_False,
                              (byte *)auxfld, wx->auxfldsz, TXbool_False) !=
                  wx->auxfldsz)
                return(0);
              TXseterror(0);
              if (EPI_LSEEK(wx->tokfh,(EPI_OFF_T)0,SEEK_END)==(EPI_OFF_T)(-1))
                goto badseek;
            }
        }
      return(1);
    }

  if (wx->token >= wx->tokbuflast &&            /* need to flush */
      !wtix_flushtokens(wx))
    return(0);
  /* Note that we don't use aligned FDBI_TOKEL_RECIDSZ if wx->auxfldsz == 0,
   * for back compatibility in case sizeof(RECID) != TX_ALIGN_BYTES:
   */
  idx = (size_t)(wx->token - wx->tokbuffirst);
  if (wx->auxfldsz == 0)
    ((RECID *)wx->tokbuf)[idx] = recid;
  else
    {
      if (!auxfld)                              /* Bug 6243 sanity check */
        {
          putmsg(MERR + UGE, __FUNCTION__,"Internal error: Missing aux data");
          return(0);
        }
      d = wx->tokbuf + idx*wx->tokelsz;
      FDBI_TXALIGN_RECID_COPY(d, &recid);
      CLRRECIDPAD(d);
      memcpy(d + FDBI_TOKEL_RECIDSZ, auxfld, wx->auxfldsz);
      CLRAUXPAD(d, wx->auxfldsz, wx->tokelsz);
    }

  if ((wx->flags & WTIXF_VERIFY) && wx->tokenMerge)
    {                                           /* verify tok recid in table */
      if (wx->curtblrecid >= wx->numtblrecids ||
          TXrecidcmp(&recid, &wx->tblrecids[wx->curtblrecid]) != 0)
        {
          l = i = 0;
          r = wx->numtblrecids;
          while (l < r)                         /* binary search */
            {
              i = ((l + r) >> 1);
              cmp = TXrecidcmp(&recid, &wx->tblrecids[i]);
              if (cmp < 0) r = i;
              else if (cmp > 0) l = i + 1;
              else break;                       /* found it */
            }
          if ((byte *)auxfld >= wx->new &&
              (byte *)auxfld < wx->new + wx->nnew*wx->tokelsz)
            {
              what = "new list";
              row = ((byte *)auxfld - wx->new)/wx->tokelsz;
            }
          else if ((byte *)auxfld >= wx->tokbuforgtmp &&
                   (byte *)auxfld < wx->tokbuforgtmp + wx->tokbuforgtmpsz)
            {
              what = "token file";
              row = (size_t)wx->tokbuforgtmprd - (size_t)1;
            }
          else
            {
              what = "unknown source";
              row = 0;
            }
          if (l < r)
            putmsg(MERR, CHARPN,
                   "#%wu %s recid 0x%wx off by %+wd rows in table",
                   (EPI_HUGEUINT)row, what, (EPI_HUGEINT)TXgetoff2(&recid),
                   (EPI_HUGEUINT)i - (EPI_HUGEUINT)wx->curtblrecid);
          else
            putmsg(MERR, CHARPN, "#%wu %s recid 0x%wx not found in table",
                   (EPI_HUGEUINT)row, what, (EPI_HUGEINT)TXgetoff2(&recid));
        }
      wx->curtblrecid++;                        /* even if not found... */
    }

  wx->token += (EPI_OFF_T)1;                    /* next token */
  wx->prevrecid = recid;                        /* save for sanity check */
  return(1);
}

static EPI_OFF_T wtix_orgtok2new ARGS((WTIX *wx, EPI_OFF_T tok));
static EPI_OFF_T
wtix_orgtok2new(wx, tok)
WTIX            *wx;
EPI_OFF_T       tok;
/* Translates token `tok' from original index to new token and returns it,
 * or 0 if deleted recid, or -1 on error.  Assumes all new recids were
 * inserted already (and hence "bad" ones deleted).
 */
{
  RECID         recid, *s;
  size_t        l, r, i;
  int           cmp;

  if (tok > (EPI_OFF_T)wx->tokbuforgnum || tok < (EPI_OFF_T)1)
    {
      putmsg(MERR, __FUNCTION__, "Invalid token 0x%wx for token file `%s'",
             (EPI_HUGEINT)tok, wx->tokorgfn);
      goto err;
    }
  s = (RECID *)(wx->tokbuforg + wx->tokbuforgelsz*((size_t)tok - 1));
  FDBI_TXALIGN_RECID_COPY(&recid, s);

  l = 0;                                        /* skip recid if deleted: */
  r = wx->ndel;
  while (l < r)                                 /* binary search into del */
    {
      i = ((l + r) >> 1);
      cmp = TXrecidcmp(&recid, &wx->del[i]);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else                                      /* found it: it's deleted */
        {
          tok = (EPI_OFF_T)0;
          goto done;
        }
    }
  /* We found the offset into the delete list where the original recid
   * would be, if we inserted it.  Thus, that many recids were deleted
   * from the original list when translated, so subtract that from
   * tok.  KNG 010306: This math assumes that the delete list is a
   * subset of the original index, i.e. (del & org) == del.
   * tpile_getorg() assured this (and also checked org order).
   */
  tok -= (EPI_OFF_T)l;

  l = 0;
  r = wx->curinsnew;
  while (l < r)                                 /* binary search into new */
    {
      i = ((l + r) >> 1);
      s = (RECID *)(wx->new + wx->tokelsz*i);
      FDBI_TXALIGN_RECID_CMP(cmp, &recid, s);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else
        {
          l = i;
          break;                                /* found it */
        }
    }
  /* Similarly for new recids: add 1 for each recid we've inserted before
   * this one.  Assumes every new recid that is also an original, is also
   * a delete recid, i.e. ((new & org) & del) == (new & org).
   * TXwtixCreateNextToken() during token merge assured this.
   */
  tok += (EPI_OFF_T)l;
  goto done;

err:
  tok = (EPI_OFF_T)(-1);
done:
  return(tok);
}

static int wtix_curinsnew2tok ARGS((WTIX *wx, RECID recid));
static int
wtix_curinsnew2tok(wx, recid)
WTIX    *wx;
RECID   recid;
/* Sets wx->token to token to use for currently-inserted new `recid',
 * during update.  Assumes it was just inserted into wx->new.
 * Returns 0 on error.
 */
{
  size_t        l, r, i;
  int           cmp;
  EPI_OFF_T     tok;
  RECID         *op;

  l = 0;
  r = wx->tokbuforgnum;
  while (l < r)                                 /* binary search into tokens */
    {
      i = ((l + r) >> 1);
      op = (RECID *)(wx->tokbuforg + wx->tokbuforgelsz*i);
      FDBI_TXALIGN_RECID_CMP(cmp, &recid, op);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else
        {
          l = i;
          break;                                /* found it */
        }
    }
  /* We found the recid, or at least where it would belong, in the
   * original token -> recid list.  So its token value is the array
   * index plus 1 (tokens start at 1).  But other recids may have been
   * inserted before this one, so add 1 for each of those:
   */
  tok = (EPI_OFF_T)(l + wx->curinsnew);

  /* Now check the delete list: subtract 1 for each recid deleted
   * before this one:
   */
  l = 0;
  r = wx->ndel;
  while (l < r)                                 /* binary search into del */
    {
      i = ((l + r) >> 1);
      cmp = TXrecidcmp(&recid, &wx->del[i]);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else
        {
          l = i;
          break;                                /* found it */
        }
    }
  /* WTF bounds check wx->token; we know delete list is ok, but...: */
  wx->token = tok - (EPI_OFF_T)l;
  return(1);
}

RECID
TXwtixMapOldRecidToNew(wx, oldRecid)
WTIX    *wx;
RECID   oldRecid;
/* Returns new-token-file recid for the token whose old-token-file
 * recid is `oldRecid', or RECID_INVALID on error.
 * For use by TXcompactTable(), after wtix_finish().
 */
{
  RECID *recidPtr, newRecid;
  int   cmp;

  /* Look up old recid's token: */
  if (!wtix_curinsnew2tok(wx, oldRecid) ||
      wx->token < 1 || wx->token > (EPI_OFF_T)wx->tokbuforgnum)
    goto err;

  /* Verify that it was actually there: */
  recidPtr = (RECID *)(wx->tokbuforg + wx->tokbuforgelsz*(wx->token - 1));
  FDBI_TXALIGN_RECID_CMP(cmp, &oldRecid, recidPtr);
  if (cmp != 0) goto err;                       /* not found */

  /* Map token to new recid: */
  if (wx->token > (EPI_OFF_T)wx->tokbufNum) goto err;
  recidPtr = (RECID *)(wx->tokbuf + wx->tokelsz*(wx->token - 1));
  FDBI_TXALIGN_RECID_COPY(&newRecid, recidPtr);
  goto done;

err:
  TXsetrecid(&newRecid, RECID_INVALID);
done:
  return(newRecid);
}

static void wtix_prslurp ARGS((WTIX *wx));
static void
wtix_prslurp(wx)
WTIX    *wx;
{
  int   n;
  char  *s;

  s = (wx->tokorgfn != CHARPN ? wx->tokorgfn : wx->tokfn);
  n = (int)strlen(s) - (sizeof(FDBI_TOKSUF) - 1);
  if (wx->flags & WTIXF_SLURP)
    putmsg(MINFO, CHARPN, "Slurp optimization set for index `%.*s'",
           n, s);
  else
    putmsg(MINFO,CHARPN,"Slurp optimization not set for index `%.*s': %s",
           n, s, (!TxIndexSlurp ? "indexslurp is 0" :
                  "original/new token ranges overlap"));
}

/* --------------------------- Token file merge ---------------------------- */

typedef struct TPILE_tag
{
  PILE          hdr;            /* must be first */
  WTIX          *wx;            /* WTIX object */
  byte          *bufend;        /* end of data (for read) */
  RECID         prevrecid;      /* previous recid (for read) */
  EPI_HUGEINT   nitems;         /* number of items originally in pile */
}
TPILE;
#define TPILEPN         ((TPILE *)NULL)

static int tpile_cmp ARGS((PILE *a, PILE *b, void *usr));
static int
tpile_cmp(a, b, usr)
PILE    *a, *b;
void    *usr;
/* Item-compare function for token file merge during update.
 */
{
  (void)usr;
  return(TXrecidcmp((RECID *)a->blk, (RECID *)b->blk));
}

static PILE *closetpile ARGS((PILE *tptr));
static PILE *
closetpile(tptr)
PILE    *tptr;
/* "Closes" TPILE.  No-op since these aren't alloced.
 */
{
  (void)tptr;
  return(PILEPN);
}

static int tpile_put ARGS((PILE *tptr, PILE *src));
static int
tpile_put(tptr, src)
PILE    *tptr;
PILE    *src;
/* Outputs token from `src' to new token file `tptr'.  Returns 0 on error.
 */
{
  RECID recid;

  FDBI_TXALIGN_RECID_COPY(&recid, src->blk);
  /* Note that if tptr->wx->auxfldsz == 0, there is no auxfld: */
  return(TXwtixCreateNextToken(((TPILE *)tptr)->wx, recid,
                               src->blk + FDBI_TOKEL_RECIDSZ));
}

static int tpile_getorg ARGS((PILE *tptr));
static int
tpile_getorg(tptr)
PILE    *tptr;
/* Advances to next recid+auxfld from original list, minus deleted
 * recids.  Returns 1 if ok, 0 on EOF, -1 on error.  Updates
 * wx->curdel/curinsdel/del.  Used during index update for merging
 * tokens into new token file.  >>>>> NOTE: see also TXwtixCreateNextToken(),
 * which may futz with curdel/curinsdel/del. <<<<<
 */
{
  WTIX          *wx;
  int           cmp;
  size_t        tsz;
  TPILE         *tp = (TPILE *)tptr;

  wx = tp->wx;

  /* Flush pending merge_incdone(), if merge_finish() started: */
  if (wx->numTokenMergeOrgDelBeforeFinish > 0 &&
      wx->inTokenMergeFinish &&
      wx->options.indexmeter != TXMDT_NONE)
    {
      merge_incdone(wx->tokenMerge, wx->numTokenMergeOrgDelBeforeFinish);
      wx->numTokenMergeOrgDelBeforeFinish = 0;
    }

nextrecid:
  tp->hdr.blk += wx->tokelsz;
  if (tp->hdr.blk >= tp->bufend)                /* end of current buffer */
    {
      if ((size_t)wx->tokbuforgtmprd >= wx->tokbuforgnum)    /* EOF */
        return(0);
      /* Read in a new buffer from original token file.  Note sanity
       * check: if we're mmap()'d, we should have already hit EOF:
       */
#ifdef WTIX_SANITY
      if (wx->tokbuforgtmp == wx->tokbuforg
#  ifdef EPI_HAVE_MMAP
          || wx->tokorgismmap
#  endif /* EPI_HAVE_MMAP */
          )
        {
          putmsg(MERR + UGE, __FUNCTION__,
                 "Internal error: temp token buffer not distinct");
          return(-1);
        }
#endif /* WTIX_SANITY */
      tsz = wx->tokbuforgtmpsz;
      if (wx->tokbuforgtmprd + (EPI_OFF_T)(tsz/wx->tokelsz) >
          (EPI_OFF_T)wx->tokbuforgnum)
        tsz = ((wx->tokbuforgnum - (size_t)wx->tokbuforgtmprd))*wx->tokelsz;
      if (tx_rawread(TXPMBUFPN, wx->tokorgfh, wx->tokorgfn, wx->tokbuforgtmp,
                     tsz, 1) != (int)tsz)
        return(-1);                             /* read error */
      tp->hdr.blk = wx->tokbuforgtmp;
      tp->bufend = tp->hdr.blk + tsz;
    }
  FDBI_TXALIGN_RECID_CMP(cmp, (RECID *)tp->hdr.blk, &tp->prevrecid);
  if (cmp <= 0)
    {
      putmsg(MERR, __FUNCTION__,
             "Corrupt token file: Out-of-order recid 0x%wx (after 0x%wx) at offset 0x%wx in `%s'",
             (EPI_HUGEINT)TXgetoff2((RECID *)tp->hdr.blk),
             (EPI_HUGEINT)TXgetoff2(&tp->prevrecid),
             (EPI_HUGEINT)(wx->tokbuforgtmprd*(EPI_OFF_T)wx->tokelsz),
             wx->tokorgfn);
      return(-1);                               /* error */
    }
  wx->tokbuforgtmprd++;
  FDBI_TXALIGN_RECID_COPY(&tp->prevrecid, tp->hdr.blk);
  for ( ; wx->curdel < wx->ndel; wx->curdel++)  /* still have dels to check */
    {
      cmp = TXrecidcmp((RECID *)tp->hdr.blk, &wx->del[wx->curdel]);
      if (cmp < 0) break;                       /* ok recid */
      if (cmp == 0)                             /* deleted recid */
        {
          /* We update the delete list, only keeping those that are
           * actually in the original list (some delete recids may be
           * missing).  Otherwise our old-to-new-token fixup math will
           * be in error later, i.e. in wtix_orgtok2new():
           */
          wx->del[wx->curinsdel++] = wx->del[wx->curdel++];
          /* Bug 7019: update merge meter for deleted recids, as they
           * were counted in TPILE.nitems (part of meter's total-items
           * metric).  Missing this call was not noticed before
           * because token merge is generally fast, so no stall was
           * noticed, and we used to force the meter to completion
           * after merge in merge_onepass(), hiding this and other
           * mismatched meter counts:
           */
          if (wx->options.indexmeter != TXMDT_NONE)
            {
              if (wx->inTokenMergeFinish)
                merge_incdone(wx->tokenMerge, 1);
              else                              /* await merge_finish() */
                wx->numTokenMergeOrgDelBeforeFinish++;
            }
          goto nextrecid;
        }
      /* This delete list recid isn't in the original list, probably
       * because it was inserted after indexing but before .dat merge
       * finished, then deleted/updated.  Remove it from delete list.
       */
    }
  return(1);
}

static int tpile_getnew ARGS((PILE *tptr));
static int
tpile_getnew(tptr)
PILE    *tptr;
/* Advances to next recid+auxfld in new recid/auxfld list.
 * Returns 1 if ok, 0 on EOF, -1 on error.
 */
{
  WTIX  *wx;
  TPILE *tp = (TPILE *)tptr;

  wx = tp->wx;
  tp->hdr.blk += wx->tokelsz;
  if (tp->hdr.blk >= tp->bufend) return(0);     /* EOF */
  return(1);
}

static PILE *tpile_next ARGS((PILE *tptr));
static PILE *
tpile_next(tptr)
PILE    *tptr;
/* Returns next pile in chain.  Always just 1 pile.
 */
{
  (void)tptr;
  return(PILEPN);
}

static size_t tpile_npiles ARGS((PILE *tptr));
static size_t
tpile_npiles(tptr)
PILE    *tptr;
/* Returns number of piles.  Always just 1 pile.
 */
{
  (void)tptr;
  return((size_t)1);
}

static EPI_HUGEINT tpile_nitems ARGS((PILE *tptr));
static EPI_HUGEINT
tpile_nitems(tptr)
PILE    *tptr;
/* Returns number of items.
 */
{
  return(((TPILE *)tptr)->nitems);
}

static CONST PILEFUNCS  TpileOrgFuncs =
{
  closetpile,
  NULL, /* tpile_put */
  tpile_getorg,
  tpile_next,
  NULL, /* tpile_flip */
  tpile_npiles,
  tpile_nitems,
  NULL                                          /* mergeFinished() */
};

static int wtix_transtokens ARGS((WTIX *wx));
static int
wtix_transtokens(wx)
WTIX    *wx;
/* Translates original token list to new token list during index
 * update: after wtix_getnew/dellist() and wtix_setupdname() called
 * but before new records are indexed.  Returns 0 on error.  Also
 * removes bad recids from wx->del delete list, and possibly adds some.
 * Also sets WTIXF_APPEND if appropriate.
 */
{
  TPILE                 tporg, tpnew, tpout;
  PILEFUNCS             newf, outf;
  int                   ret;
  BTLOC                 *rec, *recend;
  RECID                 *orglast, *newfirst, *orgfirst, *newlast;
  char                  *s;
  size_t                sz;

#ifdef WTIX_SANITY
  if (!(wx->flags & WTIXF_UPDATE))
    {
      putmsg(MERR + UGE, __FUNCTION__,
           "Internal error: attempt to merge token file on non-update index");
      return(0);
    }
#endif

  wx->inTokenMergeFinish = TXbool_False;
  wx->numTokenMergeOrgDelBeforeFinish = 0;
  wx->tokenMerge = openmerge(tpile_cmp, wx, 0, PILEOPENFUNCPN);
  if (!wx->tokenMerge) goto err;
  if (wx->options.indexmeter != TXMDT_NONE)
    merge_setmeter(wx->tokenMerge, NULL, "Creating new token file:", METERPN,
                   wx->options.indexmeter, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL);
  wx->curdel = wx->curinsdel = 0;
  memset(&tporg, 0, sizeof(tporg));
  memset(&tpnew, 0, sizeof(tpnew));
  memset(&tpout, 0, sizeof(tpout));
  tporg.hdr.blk = wx->tokbuforgtmp - wx->tokelsz;       /* 1 before start */
  tporg.bufend = wx->tokbuforgtmp;
  if (wx->tokbuforgtmp == wx->tokbuforg)                /* has all aux data */
    tporg.bufend += wx->tokbuforgtmpsz;
  else                                                  /* to read from org */
    {
      TXseterror(0);
      if (EPI_LSEEK(wx->tokorgfh, (EPI_OFF_T)0, SEEK_SET) != (EPI_OFF_T)0)
        {
          putmsg(MERR + FSE, __FUNCTION__, "Cannot rewind token file `%s': %s",
                 wx->tokorgfn, TXstrerror(TXgeterror()));
          goto err;
        }
    }
  wx->tokbuforgtmprd = (EPI_OFF_T)0;
  TXsetrecid(&tporg.prevrecid, (EPI_OFF_T)(-1));
  tporg.nitems = (EPI_HUGEINT)wx->tokbuforgnum;
  tpnew.hdr.blk = wx->new - wx->tokelsz;                /* 1 before start */
  tpnew.bufend = wx->new + wx->tokelsz*wx->nnew;
  TXsetrecid(&tpnew.prevrecid, (EPI_OFF_T)(-1));
  tpnew.nitems = wx->nnew;
  tporg.hdr.funcs = (PILEFUNCS *)&TpileOrgFuncs;
  newf = TpileOrgFuncs;
  newf.get = (int (*) ARGS((PILE *)))tpile_getnew;
  tpnew.hdr.funcs = &newf;
  outf = TpileOrgFuncs;
  outf.get = NULL;
  outf.put = (int (*) ARGS((PILE *, PILE *)))tpile_put;
  tpout.hdr.funcs = &outf;
  tporg.wx = tpnew.wx = tpout.wx = wx;
  if (!merge_addpile(wx->tokenMerge, &tporg.hdr) ||
      !merge_addpile(wx->tokenMerge, &tpnew.hdr))
    goto err;
  wx->inTokenMergeFinish = TXbool_True;
  if (!merge_finish(wx->tokenMerge, &tpout.hdr, 0))
    goto err;
  wx->inTokenMergeFinish = TXbool_False;
  wx->ndel = wx->curinsdel;
  if (!wtix_flushtokens(wx)) goto err;

  /* We may have changed the delete list.  Check that it's still ascending: */
  for (rec = wx->del + 1, recend = wx->del + wx->ndel; rec < recend; rec++)
    if (TXgetoff2(rec) <= TXgetoff2(rec - 1))
      {
        putmsg(MERR, __FUNCTION__, "Internal error: Out-of-order recid 0x%wx (after 0x%wx) in modified delete list after merging new token file for `%s'",
               (EPI_HUGEINT)TXgetoff2(rec), (EPI_HUGEINT)TXgetoff2(rec - 1),
               wx->tokorgfn);
        goto err;
      }

  /* Check if WTIXF_APPEND can be set: all new recids (if any) occur
   * after all originals (if any), and there's no delete list.  If so,
   * orginal tokens do not need translation via wtix_orgtok2new(),
   * saving some calls.  We check this here (after token translation)
   * when we know the new/delete lists are finalized:
   */
  orgfirst = (RECID *)wx->tokbuforg;
  orglast = (RECID *)(wx->tokbuforg + wx->tokbuforgelsz*(wx->tokbuforgnum-1));
  newfirst = (RECID *)wx->new;                  /* first new recid */
  newlast = (RECID *)(wx->new + wx->tokelsz*(wx->nnew - 1));
  if (wx->tokbuforgnum == 0 || wx->nnew == 0 ||
      TXrecidcmp(orglast, newfirst) < 0)
    {
      if (TxIndexAppend && wx->ndel == 0) wx->flags |= WTIXF_APPEND;
      /* Related but looser conditions also make indexslurp possible
       * during index update; normally only possible at index create.
       * (It's only possible for intermediate src piles though;
       * original src index requires last-tok-per-word info, to be
       * stored in next index version.)  The delete list is irrelevant
       * since we cannot slurp the original index (yet); as long as the
       * original token range doesn't overlap the new list, we can do
       * indexslurp.  Like WTIXF_APPEND this depends on finalized
       * new/delete lists, so we had to wait until now, but set it
       * before any intermediate piles are opened:
       */
      if (TxIndexSlurp) wx->flags |= WTIXF_SLURP;
    }
  else if (TXrecidcmp(newlast, orgfirst) < 0 && TxIndexSlurp)
    wx->flags |= WTIXF_SLURP;
  if (FdbiTraceIdx >= 1)
    {
      s = wtix_livename(wx, &sz);
      if (wx->flags & WTIXF_APPEND)
        putmsg(MINFO, CHARPN, "Append optimization set for index `%.*s'",
               sz, s);
      else
        putmsg(MINFO, CHARPN,
               "Append optimization not set for index `%.*s': %s",
               sz, s, (wx->ndel ? "Delete list non-empty" :
                (TxIndexAppend ? "First new list recid before last original" :
                 "indexappend is 0")));
      wtix_prslurp(wx);
    }

  /* note:  wx->token may get reused by wtix_curinsnew2tok(); save numrecs: */
#if EPI_OS_LONG_BITS < EPI_OFF_T_BITS
  wx->stats.totentries = (wx->token > (EPI_OFF_T)EPI_OS_LONG_MAX ? (long)EPI_OS_LONG_MAX :
                          (long)wx->token);
#else /* EPI_OS_LONG_BITS >= EPI_OFF_T_BITS */
  wx->stats.totentries = (long)wx->token;
#endif /* EPI_OS_LONG_BITS >= EPI_OFF_T_BITS */
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  wx->tokenMerge = closemerge(wx->tokenMerge);
  return(ret);
}

/* ------------------------------------------------------------------------- */

static int wtix_flushword ARGS((WTIX *wx));
static int
wtix_flushword(wx)
WTIX    *wx;
/* Flushes current word's data to data file and B-tree during output
 * stage.  Resets buffers/counts.  Returns 0 on error.
 */
{
  size_t                wsz, esz, datsz;
  RECID                 where;
  EPI_OFF_T             datoff;
  int                   ret;
  byte                  *d, *e, *c;
  EPI_HUGEUINT          u;

  if (wx->curword == CHARPN) goto ok;           /* nothing to do */
  e = BYTEPN;                                   /* no loc data in B-tree */
  esz = 0;
  wx->paramTblInfo.totalRowCount += wx->wi.ndocs; /* update stats */
  wx->paramTblInfo.totalOccurrenceCount += wx->wi.nlocs;
  wx->paramTblInfo.totalWords++;
  if (wx->wi.ndocs == (EPI_HUGEUINT)1 &&            /* single-recid word */
      (EPI_HUGEINT)wx->wi.nlocs <= (EPI_HUGEINT)wx->options.maxSingleLocs)
    {
      if (TxIndexWriteSplit)
        {
          /* Need to get the data back from KDBF: we're going to store it
           * in the B-tree.  Note that this could fail, but it's unlikely
           * since we have 64KB or more of KDBF buffer space:
           */
          datsz = kdbf_undoalloc(wx->datdf, &c);
          if (datsz == (size_t)(-1)) goto err;
          e = c;
        }
      else
        {
          e = c = wx->datbuf + wx->datbufoff;
          datsz = wx->datbufused;
        }
      e = wx->newinvsh(e, &u);                  /* get first (only) token */
      datoff = -(EPI_OFF_T)u - (EPI_OFF_T)2;    /* negate it for B-tree */
      if (wx->flags & WTIXF_FULL)
        {
          if (wx->options.indexVersion >= 3)    /* no loc size stored */
            esz = (c + datsz) - e;
          else
            {
              e = wx->newinvsh(e, &u);          /* get loc data ptr, size */
              esz = (size_t)u;
            }
        }
    }
  else                                          /* multi-recid: to .dat */
    {
      if (TxIndexWriteSplit)
        datoff = kdbf_endalloc(wx->datdf, &datsz);
      else
        datoff = kdbf_alloc(wx->datdf, wx->datbuf, datsz = wx->datbufused);
      if (datoff == (EPI_OFF_T)(-1)) goto err;
    }

  wsz = strlen(wx->curword);                    /* word+loc info to B-tree */
  if (wsz > wx->paramTblInfo.maxWordLen)
    wx->paramTblInfo.maxWordLen = wsz;
  wsz++;                                        /* for nul */
  if (wsz + 2*VSH_BOTHMAXLEN + esz > wx->btbufsz &&
      !fdbi_allocbuf(__FUNCTION__, (void **)&wx->btbuf, &wx->btbufsz,
                     wsz + 2*VSH_BOTHMAXLEN + esz))
    goto err;
  d = wx->btbuf;
  memcpy(d, wx->curword, wsz);
  d += wsz;
  if (e == BYTEPN)                              /* if not single-recid */
    {
      if ((wx->flags & (WTIXF_SLURP | WTIXF_INTER)) ==
          (WTIXF_SLURP | WTIXF_INTER))
        {
          /* Store last token.  This saves bmpile_putslurp() from
           * having to decode every token to compute the last one.
           * Note check of WTIXF_INTER above: don't change format of
           * final index, only intermediate (temp) piles.  Don't
           * bother making it relative; extra work, and not likely to
           * compress much:
           */
          c = outvsh(d, (EPI_HUGEUINT)wx->outtoken);
          if (c == d) goto badn;
          d = c;
        }
      if (wx->options.indexVersion >= 3)        /* Version 3: .dat blk size */
	{
          c = outvsh(d, (EPI_HUGEUINT)datsz);
          if (c == d) goto badn;
          d = c;
	}
      c = outvsh(d, wx->wi.ndocs);              /* always 8-bit VSH */
      if (c == d)                               /* outvsh() error */
        {
        badn:
          c = (byte *)wtix_livename(wx, &esz);
          putmsg(MERR, __FUNCTION__,
                 "Bad lasttok/blksz/ndocs/nlocs value for index `%.*s'",
                 esz, (char *)c);
          goto err;
        }
      d = c;
    }
  if (wx->flags & WTIXF_FULL)
    {
      if (esz > 0)                              /* loc data to B-tree */
        {
          memcpy(d, e, esz);
          d += esz;
        }
      else
        {
          c = outvsh(d, (wx->options.indexVersion >= 3 ?
                         wx->wi.nlocs - wx->wi.ndocs :
                         wx->wi.nlocs));        /* always 8-bit VSH */
          if (c == d) goto badn;                /* outvsh() error */
          d = c;
        }
    }
  TXsetrecid(&where, datoff);
  if (btappend(wx->bt, &where, d - wx->btbuf, wx->btbuf, WTIX_BTREEPCT,
               BTBMPN) < 0)
    goto err;
  wx->datbufused = 0;                           /* wtf realloc down if huge? */
  wx->curword = TXfree(wx->curword);
  wx->wi.ndocs = wx->wi.nlocs = (EPI_HUGEUINT)0;
ok:
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

static int wtix_out ARGS((WTIX *wx, PILE *src));
static int
wtix_out(wx, src)
WTIX    *wx;
PILE    *src;
/* Output stage function (merge intermediate/final, via WPILE).  Given
 * current item in `src', writes to index.  Returns 0 on error.  Used
 * by WPILE object to write items.
 */
{
  char                  *w;
  byte                  *s, *d, *ds, *c;
  int                   ret;
  size_t                n, sz;
  EPI_HUGEUINT          u;
  byte                  tmp[2*VSH_BOTHMAXLEN];

  s = src->blk;
  sz = src->blksz;
  w = (char *)s;
  s += strlen(w) + 1;
  s = wx->newinvsh(s, &u);                      /* get token */
  if (wx->curword == CHARPN)                    /* very first word */
    {
    newword:
      TXseterror(0);
      if ((wx->curword = TXstrdup(TXPMBUFPN, __FUNCTION__, w)) == CHARPN)
        goto err;
      wx->outtoken = (EPI_OFF_T)0;              /* so 1st token is absolute */
      if (TxIndexWriteSplit &&
          kdbf_beginalloc(wx->datdf) == (EPI_OFF_T)(-1))
        goto err;
    }
  else if (strcmp(w, wx->curword) != 0)         /* new subsequent word */
    {
      if (!wtix_flushword(wx)) goto err;        /* write to B-tree and file */
      goto newword;
    }
  sz -= s - src->blk;
  wx->wi.ndocs++;
  if (wx->flags & WTIXF_FULL)
    wx->wi.nlocs += (EPI_HUGEUINT)wx->newcountvsl(s, sz);
  if (TxIndexWriteSplit)
    d = ds = tmp;
  else
    {
      n = wx->datbufoff + wx->datbufused + 2*VSH_BOTHMAXLEN + sz;
      if (wx->datbufsz < n &&                   /* guess at output size */
          !fdbi_allocbuf(__FUNCTION__, (void **)&wx->datbuf, &wx->datbufsz, n))
        goto err;
      d = ds = wx->datbuf + wx->datbufoff + wx->datbufused;
    }
  d = wx->newoutvsh(d, u - (EPI_HUGEUINT)wx->outtoken); /* relative token */
  if (d == ds)                                  /* outvsh() error */
    {
      w = wtix_livename(wx, &n);
      putmsg(MERR, __FUNCTION__, "Bad relative token for index `%.*s'", n, w);
      goto err;
    }
  wx->outtoken = (EPI_OFF_T)u;
  if (wx->flags & WTIXF_FULL)
    {
      if (wx->options.indexVersion >= 3)        /* no loc data size stored */
        {
          *ds |= FDBI_TOKEN_FLAG;
          c = d;
        }
      else
        {
          c = wx->newoutvsh(d, (ulong)sz);      /* output VSH loc data size */
          if (c == d)                           /* outvsh() error */
            {
              w = wtix_livename(wx, &n);
              putmsg(MERR, __FUNCTION__, "Bad loc data size for index `%.*s'",
                     (int)n, w);
              goto err;
            }
        }
      if (TxIndexWriteSplit)
        {
          n = c - tmp;
          KDBF_CONTALLOC(wx->datdf, tmp, n, goto err);
          KDBF_CONTALLOC(wx->datdf, s, sz, goto err);
          goto ok;
        }
      memcpy(c, s, sz);                         /* output VSL loc data */
      d = c + sz;
    }
  if (TxIndexWriteSplit)
    {
      n = d - tmp;
      KDBF_CONTALLOC(wx->datdf, tmp, n, goto err);
    }
  else
    wx->datbufused += (d - ds);
ok:
  ret = 1;
  goto done;

err:
  wx->flags |= WTIXF_ERROR;
  ret = 0;
done:
  return(ret);
}

static int wtix_outslurp ARGS((WTIX *wx, byte *datbuf, size_t datsz));
static int
wtix_outslurp(wx, datbuf, datsz)
WTIX    *wx;
byte    *datbuf;
size_t  datsz;
/* Same as wtix_out(), but writes out a multi-token .dat buffer `datbuf'
 * of `datsz' bytes.  Used by bmpile_putslurp() to bypass merge/heap
 * during final merge (optimization).  Returns 0 on error.
 * NOTE: `datbuf' does not have atomic boundaries (except 1st call per word).
 * NOTE: 1st call per src pile word MUST be after first token was wtix_out()'d
 * (so it's properly relative to previous pile's token for this word).
 * NOTE: call wtix_flushslurp() when done with src pile's current word.
 */
{
  int                   ret;
  size_t                n;

#ifdef WTIX_SANITY
  /* We assume a wtix_out() was called already for this word
   * to sync on the first absolute token:
   */
  if (wx->outtoken == (EPI_OFF_T)0 || wx->curword == CHARPN)
    {
      putmsg(MERR, __FUNCTION__,
             "Internal error: no prior token for current word");
      goto err;
    }
#endif /* WTIX_SANITY */

  if (TxIndexWriteSplit)
    {
      KDBF_CONTALLOC(wx->datdf, datbuf, datsz, goto err);
    }
  else
    {
      n = wx->datbufoff + wx->datbufused + datsz;
      if (wx->datbufsz < n &&                   /* guess at output size */
          !fdbi_allocbuf(__FUNCTION__, (void **)&wx->datbuf, &wx->datbufsz, n))
        goto err;
      memcpy(wx->datbuf + wx->datbufoff + wx->datbufused, datbuf, datsz);
      wx->datbufused += datsz;
    }
  ret = 1;
  goto done;

err:
  wx->flags |= WTIXF_ERROR;
  ret = 0;
done:
  return(ret);
}

static int wtix_flushslurp ARGS((WTIX *wx, FDBIWI *wi, EPI_OFF_T lasttok));
static int
wtix_flushslurp(wx, wi, lasttok)
WTIX            *wx;
FDBIWI          *wi;
EPI_OFF_T       lasttok;
/* Called after all data written for a src pile's word by bmpile_outslurp().
 * `wi' has total docs/locs for src pile word, minus first token's written
 * by wtix_out().  `lasttok' is the absolute value of the last token in src
 * pile's word, used to correct future data for this word from other piles.
 */
{
  char                  *s;
  size_t                sz;

  if (lasttok <= (EPI_OFF_T)0)                  /* sanity check */
    {
      s = wtix_livename(wx, &sz);
      putmsg(MERR, __FUNCTION__,
             "Invalid last token 0x%wx sent to index `%.*s'",
             (EPI_HUGEINT)lasttok, sz, s);
      return(0);
    }
  wx->wi.ndocs += wi->ndocs;
  wx->wi.nlocs += wi->nlocs;
  wx->outtoken = lasttok;
  return(1);
}

static int wtix_getnextorg ARGS((WTIX *wx));
static int
wtix_getnextorg(wx)
WTIX    *wx;
/* Gets next record from original index, skipping deleted records and
 * translating token.  Used during index update, and by WPILE (merge
 * read of intermediate pile).  Sets wx->curorg->blk[sz].
 * Also (if slurp and inter) sets wx->srcIdx{WordInfo,LastToken}.
 * Returns 1 if ok, 0 on EOF, -1 on error.
 */
{
  static CONST RECID    noid = { (EPI_OFF_T)(-1L) };
  size_t                sz, n;
  byte                  *d, *e, *c, *s;
  RECID                 off;
  FDBIHI                *hi, hit;
  EPI_OFF_T             tok;
  EPI_HUGEUINT          u;
  byte                  tmp[VSH_BOTHMAXLEN];

  d = (byte *)(wx->curorg + 1);
  if (wx->wdorglen == 0)                        /* very first call */
    {
    nextword:                                   /* get next word */
      wx->srcIdxWordInfo.ndocs = wx->srcIdxWordInfo.nlocs = (EPI_HUGEUINT)0;
      wx->srcIdxLastToken = (EPI_OFF_T)0;
      sz = BT_REALMAXPGSZ;                      /* v-- tmp wx->curorg use */
      off = btgetnext(wx->btorg, &sz, d, BYTEPPN);
      if (!TXrecidvalid2(&off))                 /* EOF */
        {
          wx->curorg->blksz = 0;
          wx->wdorglen = 0;
          return(0);
        }
      e = d + sz;
      wx->wdorglen = strlen((char *)d) + 1;
      if ((hit.locdata = d + wx->wdorglen) > e) /* sanity check */
        {
        corrupt:
          putmsg(MERR, __FUNCTION__, "Corrupt record in Metamorph index `%s'",
                 getdbffn(wx->btorg->dbf));
          goto err;
        }
      if (wx->wdorglen > wx->wdorgsz &&
          !fdbi_allocbuf(__FUNCTION__, (void **)&wx->wdorg, &wx->wdorgsz,
                         wx->wdorglen))
        goto err;
      strcpy(wx->wdorg, (char *)d);
      if (TXgetoff2(&off) < (EPI_OFF_T)0)       /* negative: single-recid */
        {
          TXsetrecid(&hit.loc, -TXgetoff2(&off) - (EPI_OFF_T)2);
          hit.locsz = e - hit.locdata;
          hi = &hit;
          if ((wx->flags & (WTIXF_SLURP | WTIXF_INTER)) ==
              (WTIXF_SLURP | WTIXF_INTER))      /* docs/locs/tok for slurp */
            {
              wx->srcIdxWordInfo.ndocs = (EPI_HUGEUINT)1;
              if (wx->flags & WTIXF_FULL)
                wx->srcIdxWordInfo.nlocs = wx->orgcountvsl(hit.locdata,
                                                           hit.locsz);
              else
                wx->srcIdxWordInfo.nlocs = (EPI_HUGEUINT)0;
              wx->srcIdxLastToken = TXgetoff2(&hit.loc);
            }
	  /* WTF this sets getnexteof; should be getnextsingle for
	   * search symmetry:  KNG 000414
	   */
	  if (!fdbix_seek(wx->fxorg, noid)) goto err;
          goto tokit;
        }
      if ((wx->flags & (WTIXF_SLURP | WTIXF_INTER)) ==
          (WTIXF_SLURP | WTIXF_INTER))          /* docs/locs/tok for slurp */
        {
          s = hit.locdata;
          s = invsh(s, &u);                     /* lasttok */
          if (s >= e) goto corrupt;
          wx->srcIdxLastToken = (EPI_OFF_T)u;
          if (wx->orgversion >= 3)
            {
              s = invsh(s, &u);                 /* skip .dat size */
              if (s >= e) goto corrupt;
            }
          s = invsh(s, &u);                     /* ndocs */
          if (s > e) goto corrupt;
          wx->srcIdxWordInfo.ndocs = u;
          if (wx->flags & WTIXF_FULL)
            {
              s = invsh(s, &u);                 /* nlocs */
              if (s > e) goto corrupt;
              if (wx->orgversion >= 3) u += wx->srcIdxWordInfo.ndocs;
              wx->srcIdxWordInfo.nlocs = u;
            }
          else
            wx->srcIdxWordInfo.nlocs = (EPI_HUGEUINT)0;
        }
      if (!fdbix_seek(wx->fxorg, off)) goto err;
    }

nextrecid:
  hi = wx->fxorg->getnext(wx->fxorg, noid);     /* get next token for word */
  if (hi == FDBIHIPN)
    {
      if (fdbix_iserror(wx->fxorg)) goto err;   /* read error */
      goto nextword;                            /* end of word */
    }

tokit:
  /* Bug 7019: update final-merge meter for extra `outMergeAddItems'
   * added in merge_finish() call (i.e. original index data).  We do
   * this in wtix_getnextorg() -- instead of
   * bmpile_putupdate[slurp]/bmpile_mergeFinishedUpdate() after every
   * wtix_out() -- because we need to update meter for deleted items
   * too, as `outMergeAddItems' included them.  (This is also why we
   * do this before the deleted-token check just below.)  Only for
   * original index, not intermediate; intermediate is already
   * meter-counted by MERGE object.
   *
   * Note that we could also have done this inc at each new word
   * (above), instead of here at each new word+tok, for fewer calls.
   * Meter update frequency would be less (not an issue -- still
   * zillions of words).  But more importantly, would need to update
   * `wx->srcIdxWordInfo.ndocs' above for org indexes, not just inter.
   * Not worth the effort.
   */
  if ((wx->flags & (WTIXF_INTER | WTIXF_UPDATE)) == WTIXF_UPDATE &&
      wx->options.indexmeter != TXMDT_NONE)
    merge_incdone(wx->m, 1);

  /* Map original token to new, and see if deleted: */
  if ((wx->flags & (WTIXF_UPDATE|WTIXF_INTER|WTIXF_APPEND)) == WTIXF_UPDATE)
    {
      tok = wtix_orgtok2new(wx, TXgetoff2(&hi->loc));
      if (tok == (EPI_OFF_T)(-1)) goto err;     /* error */
      if (!tok)                                 /* deleted token */
        {
          if (hi == &hit) goto nextword;        /* single-recid: no getnext */
          else goto nextrecid;
        }
      TXsetrecid(&off, tok);
    }
  else                                          /* intermediate or append */
    off = hi->loc;

  /* Expand buffer if too small.  hi->locsz is doubled because we might
   * have to translate from VSH to VSH7 or vice versa; WTF don't know
   * the new size so be conservative and double it:   KNG 000331
   */
  sz = sizeof(PILE) + wx->wdorglen + VSH_BOTHMAXLEN + 2*hi->locsz;
  if (sz > wx->curorgsz)
    {                                           /* assume buf preserved! */
      if (!fdbi_allocbuf(__FUNCTION__, (void**)&wx->curorg, &wx->curorgsz, sz))
        goto err;
      d = (byte *)(wx->curorg + 1);             /* reset: may have moved */
    }
  wx->curorg->blk = d;
  d += wx->wdorglen;                            /* word is already there */
  if (hi == &hit && hit.locsz > 0)              /* single-recid w/loc data */
    {
      sz = wx->newoutvsh(tmp, (EPI_HUGEUINT)TXgetoff2(&off)) - tmp;
      if (sz == 0)                              /* outvsh() error */
        {
        badtok:
          d = (byte *)wtix_livename(wx, &sz);
          putmsg(MERR, __FUNCTION__, "Bad token for index `%.*s'",
                 (int)sz, (char *)d);
          goto err;
        }
      if (wx->newvsltolocs != wx->orgvsltolocs) /* need to translate VSLs */
        {
          if (hit.locsz*sizeof(dword) > wx->locsorgsz &&
              !fdbi_allocbuf(__FUNCTION__, (void **)&wx->locsorg,
                             &wx->locsorgsz, hit.locsz*sizeof(dword)))
            goto err;
          n = wx->orgvsltolocs(d, hit.locsz, wx->locsorg);
          hit.locsz = wx->newlocstovsl(wx->locsorg, n, d+sz, &wx->totvslerrs);
        }
      else
        memmove(d + sz, d, hit.locsz);          /* move VSL loc data over */
      memcpy(d, tmp, sz);
      d += sz + hit.locsz;
    }
  else
    {
      c = wx->newoutvsh(d, (EPI_HUGEUINT)TXgetoff2(&off));
      if (c == d) goto badtok;                  /* outvsh() error */
      d = c;
      if (wx->flags & WTIXF_FULL)               /* VSL loc data to move */
        {
          if (wx->newvsltolocs != wx->orgvsltolocs)     /* translate VSLs */
            {
              if (hi->locsz*sizeof(dword) > wx->locsorgsz &&
                  !fdbi_allocbuf(__FUNCTION__, (void **)&wx->locsorg,
                                 &wx->locsorgsz, hi->locsz*sizeof(dword)))
                goto err;
              n = wx->orgvsltolocs(hi->locdata, hi->locsz, wx->locsorg);
              d += wx->newlocstovsl(wx->locsorg, n, d, &wx->totvslerrs);
            }
          else
            {
              memcpy(d, hi->locdata, hi->locsz);
              d += hi->locsz;
            }
        }
    }
  wx->curorg->blksz = d - wx->curorg->blk;
  return(1);

err:
  wx->curorg->blksz = 0;
  wx->wdorglen = 0;
  wx->srcIdxWordInfo.ndocs = wx->srcIdxWordInfo.nlocs = (EPI_HUGEUINT)0;
  wx->srcIdxLastToken = (EPI_OFF_T)0;           /* reset to invalid */
  return(-1);
}

static void wtix_abendcb ARGS((void *usr));
static void
wtix_abendcb(usr)
void    *usr;
/* ABEND callback for WTIX: deletes temp/incomplete files.
 */
{
  WTIX  *wx = (WTIX *)usr;
  int   fh;

  if (!(wx->flags & WTIXF_UPDATE) ||
      (wx->btorg != BTREEPN && wx->dforg != KDBFPN))
    {
      /* New index; or an intermediate (temp) index; or an update index
       * and we've created the new files.  All are temp/incomplete:
       */
      if (wx->bt != BTREEPN)
        {
          fh = getdbffh(wx->bt->dbf);
          if (fh > TX_NUM_STDIO_HANDLES) close(fh);     /* for Windows */
          unlink(getdbffn(wx->bt->dbf));
        }
      if (wx->datdf != KDBFPN)
        {
          fh = kdbf_getfh(wx->datdf);
          if (fh > TX_NUM_STDIO_HANDLES) close(fh);     /* for Windows */
          unlink(kdbf_getfn(wx->datdf));
        }
      if (wx->tokfh >= 0)
        {
          if (wx->tokfh > TX_NUM_STDIO_HANDLES) close(wx->tokfh);
          if (wx->tokfn != CHARPN) unlink(wx->tokfn);
        }
    }
  if (wx->flags & WTIXF_INTER)
    {
      /* Intermediate (temp) index: delete the org files if we have them,
       * because we were flipped and reading from them:
       */
      if (wx->btorg != BTREEPN)
        {
          fh = getdbffh(wx->btorg->dbf);
          if (fh > TX_NUM_STDIO_HANDLES) close(fh);     /* for Windows */
          unlink(getdbffn(wx->btorg->dbf));
        }
      if (wx->dforg != KDBFPN)
        {
          fh = kdbf_getfh(wx->dforg);
          if (fh > TX_NUM_STDIO_HANDLES) close(fh);     /* for Windows */
          unlink(kdbf_getfn(wx->dforg));
        }
      /* no token file */
    }
}

WTIX *
closewtix(wx)
WTIX    *wx;
{
#if defined(EPI_HAVE_FSYNC)
 static CONST char cantfsync[] ="Cannot fsync() Metamorph index file `%s': %s";
#elif defined(_WIN32)
static CONST char cantfsync[]="Cannot _commit() Metamorph index file `%s': %s";
#endif
 int                    fd;

  if (wx == WTIXPN) goto done;

  TXdelabendcb(wtix_abendcb, wx);
  if (wx->gethit == (FDBI_GETHITFUNC *)getrlex) closerlex((RLEX *)wx->rexobj);
  else closerex((FFS *)wx->rexobj);
  wx->rexobj = NULL;
  wx->tr = closewtree(wx->tr);
  wx->m = closemerge(wx->m);
  wx->tokenMerge = closemerge(wx->tokenMerge);
#if defined(EPI_HAVE_FSYNC) || defined(_WIN32)
  /* Call fsync() on the .btr/.dat/.tok files, if we've modified them;
   * there is a small possibility of data corruption during the later
   * rename operation if the server crashes:   KNG 010322
   */
#  ifdef _WIN32
#    define fsync       _commit
#  endif /* _WIN32 */
  if (!(wx->flags & WTIXF_UPDATE) ||
      (wx->btorg != BTREEPN && wx->dforg != KDBFPN))
    {
      if (wx->bt != BTREEPN)
        {
          fd = getdbffh(wx->bt->dbf);
          /* `fd' may have been closed already e.g. for ALTER TABLE COMPACT */
          if (fd >= 0 && fsync(fd) != 0)
            putmsg(MERR + FWE, __FUNCTION__, cantfsync,
                   getdbffn(wx->bt->dbf), TXstrerror(TXgeterror()));
        }
      if (wx->datdf != KDBFPN)
        {
          fd = kdbf_getfh(wx->datdf);
          /* `fd' may have been closed already e.g. for ALTER TABLE COMPACT */
          if (fd >= 0 && fsync(fd) != 0)
            putmsg(MERR + FWE, __FUNCTION__, cantfsync,
                   kdbf_getfn(wx->datdf), TXstrerror(TXgeterror()));
        }
      if (wx->tokfh >= 0)
        {
          if (fsync(wx->tokfh) != 0)
            putmsg(MERR + FWE, __FUNCTION__, cantfsync,
                   (wx->tokfn != CHARPN ? wx->tokfn : ""),
                   TXstrerror(TXgeterror()));
        }
    }
#endif /* EPI_HAVE_FSYNC || _WIN32 */
  wx->datdf = kdbf_close(wx->datdf);
  wx->datbuf = TXfree(wx->datbuf);
  wx->bt = closebtree(wx->bt);
  wx->btbuf = TXfree(wx->btbuf);
  wx->lwrbuf = TXfree(wx->lwrbuf);

  if (wx->tokfh >= 0)
    {
      TXseterror(0);
      if (close(wx->tokfh) != 0)                        /* don't flush */
        putmsg(MERR + FWE, __FUNCTION__,
               "Cannot close Metamorph index token file `%s': %s",
               (wx->tokfn != CHARPN ? wx->tokfn : ""),
               TXstrerror(TXgeterror()));
      wx->tokfh = -1;
    }
  if (wx->tokbuf != BYTEPN)
    {
#ifdef EPI_HAVE_MMAP
      if (wx->tokbufIsMmap)
        munmap((caddr_t)wx->tokbuf, wx->tokbufsz);
      else
#endif /* EPI_HAVE_MMAP */
        TXfree(wx->tokbuf);
      wx->tokbuf = BYTEPN;
    }
  wx->tokfn = TXfree(wx->tokfn);
  if (wx->tokorgfh >= 0 && wx->tokorgfh != wx->tokfh) close(wx->tokorgfh);
  wx->tokorgfh = -1;
  wx->tokorgfn = TXfree(wx->tokorgfn);
  if (wx->tokbuforgtmp != BYTEPN && wx->tokbuforgtmp != wx->tokbuforg)
    wx->tokbuforgtmp = TXfree(wx->tokbuforgtmp);
  wx->tokbuforgtmp = BYTEPN;
  if (wx->tokbuforg != BYTEPN)
    {
#ifdef EPI_HAVE_MMAP
      if (wx->tokorgismmap)
        {
          munmap((caddr_t)wx->tokbuforg, wx->tokbuforgelsz*wx->tokbuforgnum);
          wx->tokbuforg = BYTEPN;
        }
      else
#endif /* EPI_HAVE_MMAP */
        wx->tokbuforg = TXfree(wx->tokbuforg);
    }

  wx->mi = TXfree(wx->mi);
  wx->curword = TXfree(wx->curword);
  _freelst(wx->noise);
  wx->noise = CHARPPN;
  wx->del = TXfree(wx->del);
  wx->new = TXfree(wx->new);
  wx->btorg = closebtree(wx->btorg);
  wx->fxorg = closefdbix(wx->fxorg);
  wx->dforg = kdbf_close(wx->dforg);
  wx->wdorg = TXfree(wx->wdorg);
  wx->curorg = TXfree(wx->curorg);
  wx->locsorg = TXfree(wx->locsorg);
  /* close meter _after_ merge, which may have a pointer to it: */
  if (wx->meter != METERPN)
    {
      meter_end(wx->meter);
      wx->meter = closemeter(wx->meter);
    }
  if (wx->prevlocale != CHARPN)
    {
      if (TXsetlocale(wx->prevlocale) == CHARPN)
        putmsg(MERR, __FUNCTION__, "Cannot restore locale `%s'",
               wx->prevlocale);
      wx->prevlocale = TXfree(wx->prevlocale);
    }

  wx->options.wordExpressions =
    TXfreeStrEmptyTermList(wx->options.wordExpressions, -1);
  wx->options.locale = TXfree(wx->options.locale);

  wx->tblrecids = TXfree(wx->tblrecids);
#ifdef KAI_STATS
  if (!(wx->flags & WTIXF_INTER))
    {
      putmsg(MINFO, CHARPN, "tot: %wd  new: %wd  del: %wd  indexed: %wd",
             (EPI_HUGEUINT)wx->stats.totentries,
             (EPI_HUGEUINT)wx->stats.newentries,
             (EPI_HUGEUINT)wx->stats.delentries,
             (EPI_HUGEUINT)wx->stats.indexeddata.bytes);
    }
#endif /* KAI_STATS */
  wx = TXfree(wx);
done:
  return(WTIXPN);
}

static void getulimit ARGS((size_t *data, size_t *as));
static void
getulimit(data, as)
size_t  *data, *as;
{
#if defined(RLIMIT_DATA) || defined(RLIMIT_AS)
  EPI_HUGEINT   soft, hard;
#endif /* RLIMIT_DATA || RLIMIT_AS */

#ifdef RLIMIT_DATA
  if (TXgetrlimit(TXPMBUFPN, RLIMIT_DATA, &soft, &hard) != 1) /* if unknown */
    *data = (size_t)EPI_OS_SIZE_T_MAX;
  else if ((EPI_HUGEUINT)soft >= (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX)
    *data = (size_t)EPI_OS_SIZE_T_MAX;
  else
    *data = (size_t)soft;
#else /* !RLIMIT_DATA */
  *data = (size_t)EPI_OS_SIZE_T_MAX;
#endif /* !RLIMIT_DATA */
#ifdef RLIMIT_AS
  if (TXgetrlimit(TXPMBUFPN, RLIMIT_AS, &soft, &hard) != 1) /* if unknown */
    *as = (size_t)EPI_OS_SIZE_T_MAX;
  else if ((EPI_HUGEUINT)soft >= (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX)
    *as = (size_t)EPI_OS_SIZE_T_MAX;
  else
    *as = (size_t)soft;
#else /* !RLIMIT_AS */
  *as = (size_t)EPI_OS_SIZE_T_MAX;
#endif /* !RLIMIT_AS */
}

size_t
TXcomputeIndexmemValue(indexmemUser)
size_t  indexmemUser;   /* (in) indexmem value from user */
/* Returns guesstimate for memory limit for creating an index.
 * If `indexmemUser' is <= 100, uses that % of the minimum of real mem/RDATA
 * limit/RAS limit.  If > 100, uses that many bytes (regardless of
 * rlimit).  Default is 40%.  If real mem size cannot be determined,
 * uses WTIX_DEFMEMLIMIT_MB (16MB).
 */
{
  size_t        memlimmb, mempct, rlimmb, physmemmb, rdatamb, rasmb;
  size_t        indexmemVal;

  /* If they set a specific size, return that without checking.  Could exceed
   * rlimit, but maybe rlimit is wrong; user should know what they're doing:
   */
  if (indexmemUser > (size_t)100)               /* direct size */
    {
      indexmemVal = indexmemUser;
      goto ok;
    }

  /* Get minimum of physical mem, data rlimit, address rlimit, size_t: */
  physmemmb = TXphysmem();
  if (!physmemmb)                                       /* unknown physmem */
    memlimmb = WTIX_DEFMEMLIMIT_MB;
  else if (physmemmb >= ((size_t)EPI_OS_SIZE_T_MAX >> 20))
    memlimmb = ((size_t)EPI_OS_SIZE_T_MAX >> 20);
  else
    memlimmb = physmemmb;
  getulimit(&rdatamb, &rasmb);
  rdatamb >>= 20;
  rasmb >>= 20;
  if (rdatamb < memlimmb) memlimmb = rdatamb;
  if (rasmb < memlimmb) memlimmb = rasmb;

  /* Now take indexmem percentage: */
  mempct = (indexmemUser <= (size_t)0 ? (size_t)40 : indexmemUser);
  memlimmb = ((memlimmb*mempct)/(size_t)100);
  if (memlimmb < WTIX_MINMEMLIMIT_MB) memlimmb = WTIX_MINMEMLIMIT_MB;

  /* Make sure we've got at least some slack mem left in current rlimit: */
  rlimmb = rdatamb;
  if (rasmb < rlimmb) rlimmb = rasmb;
  if (memlimmb >= rlimmb || rlimmb - memlimmb < WTIX_SLACKMEM_MB)
    {
      if (rlimmb < (WTIX_SLACKMEM_MB << 1))
        memlimmb = (rlimmb >> 1);
      else
        memlimmb = rlimmb - WTIX_SLACKMEM_MB;
    }
  indexmemVal = (memlimmb << 20);
ok:
  return(indexmemVal);
}

size_t
TXgetindexmmapbufsz()
/* Returns guesstimate for mmap buffer size limit, derived from indexmmapbufsz
 * Default is 25% of real mem/rlimit/mmap-limit, with a minimum of
 * WTIX_MINMMAPLIMIT_MB.
 */
{
  size_t        memlimmb, rlimmb, rasmb, rdatamb, physmemmb;

  if (TXindexmmapbufsz_val > (size_t)0) goto done;      /* already computed */

  if (TXindexmmapbufsz > (size_t)0)                     /* direct size */
    {
      TXindexmmapbufsz_val = TXindexmmapbufsz;
      goto done;
    }

  /* Get minimum of physical mem, data rlimit, address rlimit, size_t: */
  physmemmb = TXphysmem();
  if (!physmemmb)                                       /* unknown physmem */
    memlimmb = WTIX_DEFMMAPLIMIT_MB;
  else if (physmemmb >= ((size_t)EPI_OS_SIZE_T_MAX >> 20))
    memlimmb = ((size_t)EPI_OS_SIZE_T_MAX >> 20);
  else
    memlimmb = physmemmb;
  getulimit(&rdatamb, &rasmb);
  rdatamb >>= 20;
  rasmb >>= 20;
  if (rdatamb < memlimmb) memlimmb = rdatamb;
  if (rasmb < memlimmb) memlimmb = rasmb;
#ifdef EPI_MMAP_FILE_SIZE_MAX
  if (((size_t)EPI_MMAP_FILE_SIZE_MAX >> 20) < memlimmb)
    memlimmb = ((size_t)EPI_MMAP_FILE_SIZE_MAX >> 20);
#endif /* EPI_MMAP_FILE_SIZE_MAX */

  /* Now take a percentage: */
  memlimmb = ((memlimmb*25)/(size_t)100);
  if (memlimmb < WTIX_MINMMAPLIMIT_MB) memlimmb = WTIX_MINMMAPLIMIT_MB;

  /* Make sure we've got at least some slack mem left in current rlimit: */
  rlimmb = rdatamb;
  if (rasmb < rlimmb) rlimmb = rasmb;
  if (memlimmb >= rlimmb || rlimmb - memlimmb < WTIX_SLACKMEM_MB)
    {
      if (rlimmb < (WTIX_SLACKMEM_MB << 1))
        memlimmb = (rlimmb >> 1);
      else
        memlimmb = rlimmb - WTIX_SLACKMEM_MB;
    }
  TXindexmmapbufsz_val = (memlimmb << 20);
done:
  return(TXindexmmapbufsz_val);
}

static int wtix_setioctls ARGS((WTIX *wx, KDBF *df, int on));
static int
wtix_setioctls(wx, df, on)
WTIX    *wx;
KDBF    *df;
int     on;
{
  size_t        sz;

  if (on)
    {                                   /* (semi-)optional ioctls for speed */
      if (kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_APPENDONLY), (void *)1) != 0 ||
          kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_NOREADERS), (void *)1) != 0)
        return(0);
      if (kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_PREBUFSZ),
                     (void *)KDBF_PREBUFSZ_WANT) == -1)
        wx->datbufoff = 0;
      else
        wx->datbufoff = KDBF_PREBUFSZ_WANT;
      sz = FdbiWriteBufSz;
      /* kdbf_beginalloc() requires a writebuf of >64KB: */
      if (TxIndexWriteSplit && sz < (size_t)(65*1024)) sz = (size_t)(65*1024);
      if (kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_WRITEBUFSZ), (void *)sz) != 0)
        return(0);
      return(1);
    }
  else
    return(kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_APPENDONLY), NULL) == 0 &&
           kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_NOREADERS), NULL) == 0 &&
           kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_PREBUFSZ), NULL) == 0 &&
           kdbf_ioctl(df, (DBF_KAI | KDBF_IOCTL_WRITEBUFSZ), NULL) == 0);
}

WTIX *
openwtix(dbtb, field, indfile, auxfldsz, options, orgIdxParamTblInfo, flags,
         orgversion, wxparent)
DBTBL   *dbtb;          /* The table the field to be indexed is in. */
char    *field;         /* The name of the field */
char    *indfile;       /* Path to index (sans extension; NULL if RAM dbf?) */
size_t  auxfldsz;       /* Fixed size of extra fldbuf row data (default 0) */
TXfdbiIndOpts   *options;       /* (in) other options */
const TXMMPARAMTBLINFO  *orgIdxParamTblInfo;    /* (in, opt.) org idx info */
int     flags;          /* WTIXF flags */
int     orgversion;     /* Existing version if UPDATE (0 if unknown/create) */
WTIX    *wxparent;      /* Parent object, for indexmeter */
/* Atomically deletes all and only created files if error.
 */
{
  WTIX                  *wx;
  size_t                sz, rdata, ras, memInfo[2];
  int                   f, oflags, btflags;
  EPI_STAT_S            st;
  CONST char            *curlocale;
  char                  path[PATH_MAX];
  char                  tmp[12][128];

  (void)field;
  (void)wxparent;
  TXseterror(0);
  if (!(wx = (WTIX *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(WTIX))))
    goto err;
  wx->tokfh = wx->tokorgfh = -1;
  wx->flags = (flags & WTIXF_USERFLAGS);
  TX_INIT_TXMMPARAMTBLINFO(&wx->orgIdxParamTblInfo);
  TXaddabendcb(wtix_abendcb, wx);               /* soon, after basic init */

  /* Copy options/info: */
  wx->options = *options;
  wx->options.wordExpressions = NULL;
  wx->options.locale = NULL;
  if (options->wordExpressions &&
      !(wx->options.wordExpressions =
        TXdupStrEmptyTermList(TXPMBUFPN, __FUNCTION__,
                              options->wordExpressions, -1)))
    goto err;
  if (options->locale &&
      !(wx->options.locale = TXstrdup(TXPMBUFPN, __FUNCTION__,
                                      options->locale)))
    goto err;
  if (orgIdxParamTblInfo) wx->orgIdxParamTblInfo = *orgIdxParamTblInfo;

  /* KNG 991104 Check for version we cannot handle, and refuse to update;
   * might corrupt it:
   */
  wx->orgversion = orgversion;
  if (wx->options.indexVersion >= 3)
    {
      wx->newinvsh = invsh7;
      wx->newoutvsh = outvsh7;
      wx->newlinkstovsl = linkstovsh7;
      wx->newlocstovsl = locstovsh7;
      wx->newvsltolocs = vsh7tolocs;
      wx->newcountvsl = countvsh7;
    }
  else
    {
      wx->newinvsh = invsh;
      wx->newoutvsh = outvsh;
      wx->newlinkstovsl = linkstovsl;
      wx->newlocstovsl = locstovsl;
      wx->newvsltolocs = vsltolocs;
      wx->newcountvsl = countvsl;
    }
  if (((flags&(WTIXF_UPDATE|WTIXF_INTER))==WTIXF_UPDATE && orgversion >= 3) ||
      ((flags & WTIXF_INTER) && wx->options.indexVersion >= 3))
    {
      wx->orginvsh = invsh7;
      wx->orgoutvsh = outvsh7;
      wx->orglinkstovsl = linkstovsh7;
      wx->orglocstovsl = locstovsh7;
      wx->orgvsltolocs = vsh7tolocs;
      wx->orgcountvsl = countvsh7;
    }
  else
    {
      wx->orginvsh = invsh;
      wx->orgoutvsh = outvsh;
      wx->orglinkstovsl = linkstovsl;
      wx->orglocstovsl = locstovsl;
      wx->orgvsltolocs = vsltolocs;
      wx->orgcountvsl = countvsl;
    }
  if ((flags & WTIXF_UPDATE) && orgversion != 0)
    {
      if (orgversion > FDBI_MAX_VERSION)        /* cannot handle it */
        {
          putmsg(MERR + UGE, CHARPN,
                 "Index `%s' is version %d (expected %d or earlier): Incompatible, use later Texis release",
                 indfile, orgversion, FDBI_MAX_VERSION);
          goto err;
        }
      /* else if orgversion != wx->options.indexVersion,
       * updindex will update _P.tbl
       */
    }
  if (auxfldsz <= (size_t)0)
    {
      /* For back-compatibility, if `auxfldsz' is 0 then the .tok file
       * is an array of RECIDs, and maybe sizeof(RECID) != TX_ALIGN_BYTES:
       */
      wx->auxfldsz = 0;
      wx->tokelsz = sizeof(RECID);
    }
  else
    {
      wx->auxfldsz = auxfldsz;
      wx->tokelsz = FDBI_TOKEL_RECIDSZ + TX_ALIGN_UP_SIZE_T(auxfldsz);
    }
  wx->token = (EPI_OFF_T)0;
  TXsetrecid(&wx->prevrecid, (EPI_OFF_T)(-1));
  TXresetdatasize(&wx->stats.indexeddata);
  TXsetrecid(&wx->recid, (EPI_OFF_T)(-1));
  wx->curloc = (dword)(-1L);
  wx->totvslerrs = 0L;
  wx->outtoken = (EPI_OFF_T)(-1);

  /* If we cannot handle the textsearchmode, then we cannot modify the
   * index or we might corrupt it.  Note that the error should already
   * have been reported by dbiparams(), but which could not fail the
   * open-index at that time:
   */
  if (!TXCFF_IS_VALID_MODEFLAGS(wx->options.textSearchMode)) goto err;

  if (!(wx->flags & WTIXF_INTER))
    {
      if (!(wx->flags & WTIXF_COMPACT))
        {
          if (wx->options.locale != CHARPN &&
              strcmp(wx->options.locale, (curlocale = TXgetlocale())) != 0)
            {                                   /* different locale */
              TXseterror(0);
              if (!(wx->prevlocale = TXstrdup(TXPMBUFPN, __FUNCTION__,
                                              curlocale)))
                goto err;
              if (TXsetlocale(wx->options.locale) == CHARPN)
                {
                  putmsg(MERR, __FUNCTION__, "Invalid locale `%s'",
                         wx->options.locale);
                  wx->prevlocale = TXfree(wx->prevlocale);
                }
            }
          if (**wx->options.wordExpressions != '\0' &&
              *wx->options.wordExpressions[1] == '\0')
            {           /* only 1 expression: optimize: direct REX use */
              wx->gethit = (FDBI_GETHITFUNC *)getrex;
              wx->getlen = (FDBI_GETLENFUNC *)rexsize;
              wx->rexobj = openrex((byte *)wx->options.wordExpressions[0],
                                   TXrexSyntax_Rex);
            }
          else
            {
              wx->gethit = (FDBI_GETHITFUNC *)getrlex;
              wx->getlen = (FDBI_GETLENFUNC *)rlexlen;
              wx->rexobj = openrlex((const char**)wx->options.wordExpressions,
                                    TXrexSyntax_Rex);
            }
          if (wx->rexobj == NULL ||
              (wx->tr = openwtree()) == WTREEPN)
            goto err;
          wx->m = openmerge(MERGECMP_WTIX, wx, wx->options.indexmem,
                            (PILEOPENFUNC *)openwpile);
          if (wx->m == MERGEPN) goto err;
        }
      TXcatpath(path, indfile, FDBI_TOKSUF);
      f = (TX_O_BINARY);
      if (wx->flags & WTIXF_UPDATE)
        f |= O_RDONLY;                          /* read-only for Linux mmap */
      else
        f |= (O_RDWR | O_CREAT | O_EXCL);
      wx->tokfh = TXrawOpen(TXPMBUFPN, __FUNCTION__,
                            "Metamorph index token file", path,
                            TXrawOpenFlag_None, f, 0600);
      if (wx->tokfh < 0) goto err;
      TXseterror(0);
      if (EPI_FSTAT(wx->tokfh, &st) != 0)
        {
          putmsg(MERR + FTE, __FUNCTION__, CantFstat, path,
                 TXstrerror(TXgeterror()));
          goto err;
        }
      wx->tokorgsz = (EPI_OFF_T)st.st_size;
      if (!(wx->flags & WTIXF_UPDATE) && wx->tokorgsz != (EPI_OFF_T)0)
        goto cantcreate;                        /* additional O_EXCL check */
      wx->stats.totentries = (long)(wx->tokorgsz/(EPI_OFF_T)wx->tokelsz);
      TXseterror(0);
      if ((wx->tokfn = TXstrdup(TXPMBUFPN, __FUNCTION__, path)) == CHARPN)
        goto err;
      sz = FdbiWriteBufSz/wx->tokelsz;
      if (sz < 1) sz = 1;                       /* sanity check */
      wx->tokbufsz = sz*wx->tokelsz;
      TXseterror(0);
      wx->tokbufIsMmap = 0;
      wx->tokbuf = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__, wx->tokbufsz);
      if (wx->tokbuf == BYTEPN)
        goto err;
      wx->tokbuffirst = (EPI_OFF_T)0;
      wx->tokbuflast = (EPI_OFF_T)sz;
      if (wx->options.indexmeter != TXMDT_NONE &&
          !(wx->flags & (WTIXF_UPDATE | WTIXF_COMPACT)))
        {                                       /* see also wtix_getnewlist */
          TXseterror(0);
          if (EPI_FSTAT(getdbffh(dbtb->tbl->df), &st) == 0)   /* WTF drill */
            wx->tblsize = wx->metersize = (EPI_OFF_T)st.st_size;
          /* else RAM dbf */
          if (wx->metersize > (EPI_OFF_T)0)
            {
              wx->meter = openmeter("Indexing data:", wx->options.indexmeter,
                                    MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
                                    (EPI_HUGEINT)wx->metersize);
              merge_setmeter(wx->m, "Merging to temp file:",
                             "Final merge to index:", wx->meter,
                             0, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL);
            }
        }
      /* Set flag for slurp optimization; propagates to WPILEs' WTIXes,
       * where it's needed at intermediate merge to write lasttok in the
       * B-tree, then wtix_finish() checks it to set bmpile_putslurp().
       * NOTE: May also be set with WTIXF_APPEND in wtix_transtokens():
       */
      if (TxIndexSlurp && !(wx->flags & WTIXF_UPDATE))
        wx->flags |= WTIXF_SLURP;
      if (FdbiTraceIdx >= 1)
        {
          if (!TXgetmeminfo(memInfo))
            memInfo[0] = memInfo[1] = (size_t)0;
          getulimit(&rdata, &ras);
          putmsg(MINFO, CHARPN, "physmem=%sB ulimit=%sB/%sB indexmem=%sB indexmmapbufsz=%sB indexreadbufsz=%sB indexwritebufsz=%sB vsz=%sB rss=%sB"
#ifdef EPI_TRACK_MEM
                 " memCurTotAlloced=%sB"
#endif /* EPI_TRACK_MEM */
                 ,
                 TXprkilo(tmp[0], sizeof(tmp[0]), (EPI_HUGEUINT)TXphysmem()<<20),
#if EPI_HUGEUINT_BITS > EPI_OS_SIZE_T_BITS
                 TXprkilo(tmp[1], sizeof(tmp[1]), (rdata==(size_t)EPI_OS_SIZE_T_MAX&&
            ((size_t)EPI_OS_SIZE_T_MAX & (size_t)0x3FFFFFFF) == (size_t)0x3FFFFFFF) ?
                 ((EPI_HUGEUINT)EPI_OS_SIZE_T_MAX + (EPI_HUGEUINT)1) : (EPI_HUGEUINT)rdata),
                 TXprkilo(tmp[2], sizeof(tmp[2]), (ras == (size_t)EPI_OS_SIZE_T_MAX&&
            ((size_t)EPI_OS_SIZE_T_MAX & (size_t)0x3FFFFFFF) == (size_t)0x3FFFFFFF) ?
                 ((EPI_HUGEUINT)EPI_OS_SIZE_T_MAX + (EPI_HUGEUINT)1) : (EPI_HUGEUINT)ras),
#else /* EPI_HUGEUINT_BITS <= EPI_OS_SIZE_T_BITS */
                 TXprkilo(tmp[3], sizeof(tmp[3]), (EPI_HUGEUINT)rdata),
                 TXprkilo(tmp[4], sizeof(tmp[4]), (EPI_HUGEUINT)ras),
#endif /* EPI_HUGEUINT_BITS <= EPI_OS_SIZE_T_BITS */
                 TXprkilo(tmp[5], sizeof(tmp[5]),
                          (EPI_HUGEUINT)wx->options.indexmem),
                 TXprkilo(tmp[6], sizeof(tmp[6]),
                          (EPI_HUGEUINT)TXgetindexmmapbufsz()),
                 TXprkilo(tmp[7], sizeof(tmp[7]), (EPI_HUGEUINT)FdbiReadBufSz),
                 TXprkilo(tmp[8], sizeof(tmp[8]), (EPI_HUGEUINT)FdbiWriteBufSz),
                 TXprkilo(tmp[9], sizeof(tmp[9]), (EPI_HUGEUINT)memInfo[0]),
                 TXprkilo(tmp[10], sizeof(tmp[10]), (EPI_HUGEUINT)memInfo[1])
#ifdef EPI_TRACK_MEM
                 , TXprkilo(tmp[11], sizeof(tmp[11]),
                            ((EPI_HUGEUINT)(TXmemCurTotalAlloced < 0 ? 0 :
                                            TXmemCurTotalAlloced) &
                             /* round to KB for terseness: */
                             ~(EPI_HUGEUINT)0x3ff))
#endif /* EPI_TRACK_MEM */
                 );
          putmsg(MINFO, CHARPN,
                 "Merge flush optimization %sset for index `%s'%s",
                 (TxMergeFlush ? "" : "not "), indfile,
                 (TxMergeFlush ? "" : ": mergeflush is 0"));
          putmsg(MINFO, CHARPN,
                 "Write split optimization %sset for index `%s'%s",
                 (TxIndexWriteSplit ? "" : "not "), indfile,
                 (TxIndexWriteSplit ? "" : ": indexwritesplit is 0"));
          if (!(wx->flags & WTIXF_UPDATE)) wtix_prslurp(wx);
        }
    }

  /* Open dictionary B-tree .btr: - - - - - - - - - - - - - - - - - - - - - */
  if (!TXcatpath(path, indfile, TX_BTREE_SUFFIX)) goto err;
  oflags = O_RDWR;
  btflags = 0;
  if (!(wx->flags & WTIXF_UPDATE))              /* file must be new */
    {
      if (existsbtree(path)) goto cantcreate;
      oflags |= (O_CREAT | O_EXCL);
      btflags |= BT_LINEAR;
      if (TXindexBtreeExclusive) btflags |= BT_EXCLUSIVEACCESS;
    }
  wx->bt = openbtree(indfile, FDBI_BTPGSZ, 10, btflags, oflags);
  if (wx->bt == BTREEPN) goto err;
  btsetcmp(wx->bt, fdbi_btcmp);
  if (!fdbi_allocbuf(__FUNCTION__, (void **)&wx->btbuf, &wx->btbufsz,
                     BT_REALMAXPGSZ))
    goto err;
  /* pre-alloc; need at least sizeof(EPI_HUGEUINT) anyway: */
  if (!fdbi_allocbuf(__FUNCTION__, (void **)&wx->lwrbuf, &wx->lwrbufsz, 128))
    goto err;

  /* Open .dat file: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (!TXcatpath(path, indfile, FDBI_DATSUF)) goto err;
  oflags = O_RDWR;
  if (!(wx->flags & WTIXF_UPDATE))              /* file must be new */
    oflags |= (O_CREAT | O_EXCL);
  if ((wx->datdf = kdbf_open((dbtb && dbtb->ddic ? dbtb->ddic->pmbuf :
                              TXPMBUFPN), path, oflags)) == KDBFPN)
    {
      putmsg(MERR + FOE, __FUNCTION__,
             "Cannot open Metamorph index .dat file `%s': %s",
             path, TXstrerror(TXgeterror()));
      goto err;
    }
  if (!(wx->flags & WTIXF_UPDATE))
    {
      if (kdbf_get(wx->datdf, (EPI_OFF_T)0L, &sz) != NULL)
        {                                       /* file must be new */
        cantcreate:
          putmsg(MERR + FOE, __FUNCTION__,
                 "Metamorph index file `%s' already exists", path);
          goto err;
        }
      if (!wtix_setioctls(wx, wx->datdf, 1)) goto err;
    }
  if (!TxIndexWriteSplit &&
      !fdbi_allocbuf(__FUNCTION__, (void **)&wx->datbuf, &wx->datbufsz,
                     WTIX_DEFDATBUFSZ))         /* prealloc */
    goto err;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (wx->flags & WTIXF_COMPACT)
    wx->flags |= WTIXF_FINISH;                  /* so wtix_finish runs */
  goto done;

err:
  /* Delete any newly-created files: */
  if (wx != WTIXPN && !(wx->flags & WTIXF_UPDATE))
    {
      if (wx->datdf != KDBFPN)
        {
          f = TXcatpath(path, kdbf_getfn(wx->datdf), "");
          wx->datdf = kdbf_close(wx->datdf);
          if (f && unlink(path) != 0)
            putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
                   TXstrerror(TXgeterror()));
        }
      if (wx->bt != BTREEPN)
        {
          f = TXcatpath(path, getdbffn(wx->bt->dbf), "");
          wx->bt = closebtree(wx->bt);
          if (f && unlink(path) != 0)
            putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
                   TXstrerror(TXgeterror()));
        }
      if (wx->tokfh != -1)
        {
          f = TXcatpath(path, indfile, FDBI_TOKSUF);
          close(wx->tokfh);
          wx->tokfh = -1;
          if (f && unlink(path) != 0)
            putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
                   TXstrerror(TXgeterror()));
        }
    }
  wx = closewtix(wx);
done:
  return(wx);
}

size_t
TXwtixGetIndexMemUsed(wx)
WTIX *wx;
/* Returns amount of memory currently used by `wx' that is charged
 * against indexmem.
 */
{
  return(wx->m ? TXmergeGetMemUsed(wx->m) : 0);
}

static int wtix_flushrow ARGS((WTIX *wx));
static int
wtix_flushrow(wx)
WTIX    *wx;
/* Flushes current table row's index data to merge.  Returns 0 on error.
 */
{
  int                   rc;

  if (wx->tr == WTREEPN) return(1);             /* out of mem earlier? */
  if (wx->noise != CHARPPN) delwtreesl(wx->tr, (byte **)wx->noise);
  rc = walkwtree(wx->tr, wtix_addmerge, wx);
  wx->tr = closewtree(wx->tr);                  /* wtf */
  if (!rc) wx->flags |= WTIXF_ERROR;

  /* Conserve mem: if `wx->mi' is large (e.g. this was a large row),
   * reduce it (it is realloced as needed).  If later rows are small,
   * this saves mem; if later rows are large, the realloc cost is
   * minimal compared to the indexing work already being done:
   */
  if (wx->misz > WTIX_MAX_SAVE_BUF_LEN)
    {
      wx->mi = TXfree(wx->mi);
      wx->misz = 0;
      /* Realloc to WTIX_MAX_... instead of leaving it completely
       * free, to avoid a bunch of incremental reallocs if the next
       * row is medium/large:
       */
      wx->mi = TXmalloc(TXPMBUFPN, __FUNCTION__, WTIX_MAX_SAVE_BUF_LEN);
      if (wx->mi) wx->misz = WTIX_MAX_SAVE_BUF_LEN;
    }

  return(rc);
}

static int wtix_startnewrow ARGS((WTIX *wx, void *auxfld, BTLOC at));
static int
wtix_startnewrow(wx, auxfld, at)
WTIX    *wx;
void    *auxfld;
BTLOC   at;
/* Called by wtix_insert... functions at each new recid `at'.  Flushes
 * current row data and sets up a new row.  Returns 0 on error.
 */
{
  byte                  *d;
  EPI_OFF_T             off;

  if (wx->meter != METERPN)
    {
      if (wx->flags & WTIXF_UPDATE)
        METER_UPDATEDONE(wx->meter, (EPI_HUGEINT)(wx->curnew - 1));
      else
        METER_UPDATEDONE(wx->meter, (EPI_HUGEINT)TXgetoff2(&at));
    }

  wx->stats.newentries += 1L;                   /* new row: count it */
  if (!wtix_flushrow(wx) ||
      (wx->tr = openwtree()) == WTREEPN ||
      !merge_newpile(wx->m))
    goto err;
  wx->recid = at;
  wx->curloc = (dword)(-1L);                    /* reset word counter */
  if (wx->flags & WTIXF_UPDATE)
    {
#ifdef WTIX_SANITY
      if (wx->new == BYTEPN)
        {
          putmsg(MERR + UGE, __FUNCTION__,
                 "Internal error: New list not set for updating index");
          goto err;
        }
#endif /* WTIX_SANITY */
      /*   Copy the recid and aux data to the new list.  Not really
       * needed anymore because the actually-indexed new recid list
       * (curinsnew) and "original" new list (curnew) must be the
       * same.  (We used to not call wtix_insert() for recids that
       * updindex couldn't read, hence curinsnew could fall behind
       * curnew, but that was a bug.)
       *   But the aux data still might differ, if the new list is
       * corrupt (dups removed or aux bad), or legitimately if a
       * new-list recid is updated after wtix_transtokens() but before
       * it's indexed.  We now get the real table aux data during
       * wtix_insert() on index update too, so compare it with what
       * wtix_transtokens() wrote and update if needed:  KNG 000315
       */
      off = (EPI_OFF_T)0;
      if (wx->auxfldsz == 0)
        ((RECID *)wx->new)[wx->curinsnew++] = at;
      else
        {
          d = wx->new + wx->curinsnew*wx->tokelsz;
          FDBI_TXALIGN_RECID_COPY(d, &at);
          CLRRECIDPAD(d);
          if (memcmp(d + FDBI_TOKEL_RECIDSZ, auxfld, wx->auxfldsz) != 0)
            {
              /* Cannot complain normally; this could happen legitimately: */
              if (FdbiTraceIdx >= 1)
                putmsg(MINFO, __FUNCTION__,
                       "Bad compound data for recid 0x%wx in new token file to replace file `%s'; correcting",
                       (EPI_HUGEINT)TXgetoff2(&at), wx->tokorgfn);
              off = (EPI_OFF_T)1;
            }
          memcpy(d + FDBI_TOKEL_RECIDSZ, auxfld, wx->auxfldsz);
          CLRAUXPAD(d, wx->auxfldsz, wx->tokelsz);
          wx->curinsnew += 1;
        }
      if (!wtix_curinsnew2tok(wx, at)) goto err;
      if (off != (EPI_OFF_T)0)                  /* fix up the token file */
        {
          /* One-at-a-time unbuffered writes here, but these are rare
           * so it's ok:
           */
          off = (wx->token - (EPI_OFF_T)1)*wx->tokelsz +
            (EPI_OFF_T)FDBI_TOKEL_RECIDSZ;
          TXseterror(0);
          if (EPI_LSEEK(wx->tokfh, off, SEEK_SET) != off)
            {
              putmsg(MERR + FSE, __FUNCTION__,
                     "Cannot seek to 0x%wx in token file `%s': %s",
                     (EPI_HUGEINT)off, wx->tokfn, TXstrerror(TXgeterror()));
              goto err;
            }
          if (tx_rawwrite(TXPMBUFPN, wx->tokfh, wx->tokfn, TXbool_False,
                          (byte*)auxfld, wx->auxfldsz, TXbool_False) !=
              wx->auxfldsz)
            goto err;
        }
    }
  else if (!TXwtixCreateNextToken(wx, at, auxfld)) /* new tok now if create */
    goto err;
  return(1);

err:
  return(0);
}

int
wtix_insert(wx, buf, sz, auxfld, at)
WTIX    *wx;    /* The index to insert into */
void    *buf;   /* Item to insert */
size_t  sz;     /* Size of the item */
void    *auxfld;/* Extra fldbuf data (NULL if `auxfldsz' was 0 at open) */
BTLOC   at;     /* The location (recid) of the item */
/* Called to index each new table row (field).  `auxfld' is extra row
 * data (fldtobuf) stored in token file.  May be called several times
 * with consecutive blocks for the same row, in order (e.g. indirect
 * fields): `auxfld' is ignored on subsequent calls for same recid.
 * If updating index, assumes wtix_getnewlist(), etc. already called.
 * Returns 0 on error.  >>> NOTE: _Every_ new recid read by
 * wtix_getnextnew() _must_ be inserted via wtix_insert(), even if
 * error (e.g. missing indirect file: give empty buffer); otherwise
 * tokens will be out of sync with recids (wtix_curinsnew2tok()
 * assumes curinsnew inc'd here).  See also wtix_insertloc().
 */
{
  byte                  *hit, *phit, *bufend;
  size_t                hitlen, phitlen, foldLen, allocSz;
  int                   ret;

  TXadddatasize(&wx->stats.indexeddata, (long)sz);

  if (TXrecidcmp(&at, &wx->recid) != 0 &&       /* new row started */
      !wtix_startnewrow(wx, auxfld, at))
    goto err;

  phit = BYTEPN;
  phitlen = (size_t)(-1);
  bufend = (byte *)buf + sz;
  for (hit = wx->gethit(wx->rexobj, (byte *)buf, bufend, SEARCHNEWBUF);
       hit != BYTEPN;
       hit = wx->gethit(wx->rexobj, (byte *)buf, bufend, CONTINUESEARCH))
    {
      hitlen = wx->getlen(wx->rexobj);
      /* With multiple index expressions, we could have overlapping
       * hits from the text.  If the matches are identical (same
       * position and byte length), ignore the dups.  If they start at
       * the same position but differ in length, consider them the
       * same word location (this helps phrase searches with
       * punctuation).  We do a parallel correction during the search
       * in openfdbif().  KNG 990111
       * and in rppm_searchbuf() KNG 20070913
       */
      if (hit == phit)                          /* same byte position */
        {
          if (hitlen == phitlen) continue;      /* and same length: ignore */
        }
      else                                      /* new position: new loc */
        {
          if (++wx->curloc == (dword)(-1L))     /* overflow? KNG 990628 */
            {
              putmsg(MERR + UGE, __FUNCTION__,
     "Word count exceeds dword size (truncated) at recid 0x%wx in index `%s'",
                     (EPI_HUGEINT)TXgetoff2(&wx->recid),
                     kdbf_getfn(wx->datdf));
              break;                            /* stumble on to next recid */
            }
        }
      phit = hit;                               /* before modifying `hit' */
      phitlen = hitlen;

      /* Case-fold the hit into `wx->lwrbuf'.  Loop if re-alloc needed: */
      allocSz = hitlen + 1;                     /* initial guess */
      do
        {
          if (allocSz > wx->lwrbufsz &&
              !fdbi_allocbuf(__FUNCTION__, (void**)&wx->lwrbuf, &wx->lwrbufsz,
                             allocSz))
            goto err;
#if defined(WTREE_HUGEUINT_CMP) || defined(WTREE_HUGEUINT_REVCMP)
          *(EPI_HUGEUINT *)wx->lwrbuf = (EPI_HUGEUINT)0;
#endif /* WTREE_HUGEUINT_CMP || WTREE_HUGEUINT_REVCMP */
          foldLen = TXunicodeStrFold((char *)wx->lwrbuf, wx->lwrbufsz,
                                     (char *)hit, hitlen,
                                     wx->options.textSearchMode);
          if (foldLen == (size_t)(-1))          /* buf too small */
            allocSz = wx->lwrbufsz + (wx->lwrbufsz >> 1) + 8;
        }
      while (foldLen == (size_t)(-1));

      /* Add to tree: */
      if (!putwtree(wx->tr, wx->lwrbuf, foldLen, wx->curloc))
        {
          putmsg(MERR + MAE, __FUNCTION__,
  "Could not add word `%.*s' loc %wku of %wku-byte recid 0x%wx to index `%s'",
                 (int)foldLen, wx->lwrbuf, (EPI_HUGEUINT)wx->curloc,
                 (EPI_HUGEUINT)sz,
                 (EPI_HUGEINT)TXgetoff2(&wx->recid), kdbf_getfn(wx->datdf));
          goto err;
        }
    }
  wx->flags |= WTIXF_FINISH;                    /* even if no words found */
  ret = 1;                                      /*   (may be del recids) */
  goto done;

err:
  wx->flags |= WTIXF_ERROR;
  ret = 0;
done:
  return(ret);
}

int
wtix_insertloc(wx, s, sz, auxfld, at, loc)
WTIX            *wx;    /* The index to insert into */
CONST char      *s;     /* Word to insert */
size_t          sz;     /* Size of the word */
void            *auxfld;/* Extra fldbuf data (NULL if `auxfldsz' was 0) */
BTLOC           at;     /* The location (recid) of the word */
int             loc;    /* Word location of the word */
/* Same as wtix_insert(), but we're given a specific word `s' of size `sz'
 * and a specific location `loc' within the current recid `at', instead
 * of REXing for words ourselves.  Same caveats apply as for wtix_insert():
 * can be called multiple times for the same recid (consecutively only).
 * NOTE: it is entirely up to the caller to determine correct word locations,
 * and to call this function even for empty rows (give NULL `s', -1 `loc').
 * Returns 0 on error.
 */
{
  int                   ret;
  size_t                foldLen, allocSz;

  if (TXrecidcmp(&at, &wx->recid) != 0 &&       /* new row started */
      !wtix_startnewrow(wx, auxfld, at))
    goto err;

  if (s != CHARPN && loc >= 0)
    {
      /* Case-fold the hit into `wx->lwrbuf'.  Loop if re-alloc needed: */
      allocSz = sz + 1;                         /* initial guess */
      do
        {
          if (allocSz > wx->lwrbufsz &&
              !fdbi_allocbuf(__FUNCTION__, (void **)&wx->lwrbuf,
                             &wx->lwrbufsz, allocSz))
            goto err;
#if defined(WTREE_HUGEUINT_CMP) || defined(WTREE_HUGEUINT_REVCMP)
          *(EPI_HUGEUINT *)wx->lwrbuf = (EPI_HUGEUINT)0;
#endif /* WTREE_HUGEUINT_CMP || WTREE_HUGEUINT_REVCMP */
          foldLen = TXunicodeStrFold((char *)wx->lwrbuf, wx->lwrbufsz,
                                     s, sz, wx->options.textSearchMode);
          if (foldLen == (size_t)(-1))          /* buf too small */
            allocSz = wx->lwrbufsz + (wx->lwrbufsz >> 1) + 8;
        }
      while (foldLen == (size_t)(-1));

      /* Add to tree: */
      if (!putwtree(wx->tr, wx->lwrbuf, foldLen, loc))
        {
          putmsg(MERR + MAE, __FUNCTION__,
            "Could not add word `%.*s' loc %wku of recid 0x%wx to index `%s'",
                 (int)foldLen, wx->lwrbuf, (EPI_HUGEUINT)loc,
                 (EPI_HUGEINT)TXgetoff2(&wx->recid), kdbf_getfn(wx->datdf));
          goto err;
        }
    }
  wx->flags |= WTIXF_FINISH;                    /* even if no word */
  ret = 1;
  goto done;

err:
  wx->flags |= WTIXF_ERROR;
  ret = 0;
done:
  return(ret);
}  

EPI_OFF_T
wtix_estdiskusage(wx, tblsize)
WTIX            *wx;
EPI_OFF_T       tblsize;
/* Returns estimated maximum disk usage (i.e. during creation) for
 * given index, in megabytes, given table size `tblsize'.
 */
{
  EPI_OFF_T     sz;

  /* Final WTIXF_FULL index uses about 0.7x data size; non-WTIXF_FULL,
   * about 0.3x data size.  Worst-case, we need twice that, for a temp
   * merge as well (i.e. temp merge just before last row; just 1 row in
   * mem for final merge).  WTF temp files could be spread across
   * other filesystems.
   */
  sz = (((EPI_OFF_T)((wx->flags & WTIXF_FULL) ? 14 : 6))*
        (EPI_OFF_T)(tblsize >> 20))/(EPI_OFF_T)10;
  if (sz < (EPI_OFF_T)5) sz = (EPI_OFF_T)5;
  wx->estindexsize = (sz >> 1);
  return(sz);
}

static int TXwtixLoadTokenFile ARGS((WTIX *wx, EPI_OFF_T tokenFileSz,
               CONST char *tokenPath, int tokenDesc, int forNew));
static int
TXwtixLoadTokenFile(wx, tokenFileSz, tokenPath, tokenDesc, forNew)
WTIX            *wx;
EPI_OFF_T       tokenFileSz;    /* (in) total token file size */
CONST char      *tokenPath;     /* (in) token file path */
int             tokenDesc;      /* (in) open `tokenPath' descriptor */
int             forNew;         /* (in) nonzero: new token file, not org */
/* mmap()'s or reads entire original token file to `tokbuforg',
 * or new token file to `tokbuf'.
 * Returns 0 on error.
 */
{
  size_t                mmapBufSz, totalSz, accumSz, szToRead, accumNum;
  METER                 *meter = METERPN;
  int                   ret;
  RECID                 *destRecid;
  byte                  *src, *srcEnd;
  /* "return" vars: */
  int                   tokenBufIsMmap = 0, tokenBufDesc = -1;
  byte                  *tokenBuf = BYTEPN, *tokenBufTmp = BYTEPN;
  size_t                tokenBufTmpSz = 0, tokenBufNum = 0;
  size_t                tokenBufElSz = 0;

  if ((EPI_HUGEUINT)tokenFileSz > (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX)
    {
      /* Exceeds malloc()/mmap() size limit; wtf work around this: */
      putmsg(MERR + MAE, __FUNCTION__,
             "Metamorph index token file `%s' too large", tokenPath);
      goto err;
    }
  totalSz = (((size_t)tokenFileSz)/wx->tokelsz)*wx->tokelsz;
  /* `tokbuf' may have started out as alloc'd, partial-size buffer; free it:*/
  if (forNew && !wx->tokbufIsMmap)
    wx->tokbuf = TXfree(wx->tokbuf);

  tokenBufNum = totalSz/wx->tokelsz;
  tokenBufElSz = wx->tokelsz;                   /* may change below */
  mmapBufSz = TXgetindexmmapbufsz();
  if (totalSz > 0)
    {
      TXseterror(0);
#ifdef EPI_HAVE_MMAP
      if ((TxIndexMmap & 1) &&                  /* ok to mmap() tok file & */
          totalSz <= mmapBufSz &&               /*   size is under limit & */
          (tokenBuf = (byte *)mmap(NULL, totalSz, PROT_READ, MAP_SHARED,
                                   tokenDesc, 0)) != (byte *)(-1))
        {
          tokenBufIsMmap = 1;
          tokenBufTmp = tokenBuf;
          tokenBufTmpSz = totalSz;
          if (FdbiTraceIdx >= 1)
            putmsg(MINFO, CHARPN,
                   "mmap()ing entire Metamorph index token file `%s'",
                   tokenPath);
        }
      else
#endif /* EPI_HAVE_MMAP */
        {
          /* Warn about non-mmap()-able token file, for debuggers: */
          if (FdbiTraceIdx >= 1)
            putmsg(MINFO, CHARPN,
               "Cannot mmap() Metamorph index token file `%s': %s; reading %s",
                   tokenPath,
#ifdef EPI_HAVE_MMAP
                   ((TxIndexMmap & 1) ?
                    (totalSz <= mmapBufSz ? TXstrerror(TXgeterror()) :
                     "Size exceeds indexmmapbufsz") : "(indexmmap & 1) off"),
#else /* !EPI_HAVE_MMAP */
                   "EPI_HAVE_MMAP not defined",
#endif /* !EPI_HAVE_MMAP */
                   ((wx->auxfldsz == 0 || totalSz <= mmapBufSz) ?
                    "whole file verbatim" : "just recids"));
          /* We are about to read the original token file, instead of
           * mmap()ing it.  We need all the recids, because we are
           * going to randomly access all of them in wtix_curinsnew2tok().
           * However, we only really need the aux data during the
           * wtix_transtokens() merge, and it accesses them linearly.
           * Since the token file may be large, we can save mem if
           * there is aux data by just reading the recids into an
           * array, instead of a verbatim read of the recid+aux data.
           * But first check: if there is no aux data, we would get no
           * space savings; or if the file size is <= indexmmapbufsz,
           * it would have been entirely mmap()'able if possible/allowed.
           * In either case, read the file verbatim instead:  KNG 011127
           */
          TXseterror(0);
          if (EPI_LSEEK(tokenDesc, (EPI_OFF_T)0, SEEK_SET) != (EPI_OFF_T)0)
            {
              putmsg(MERR + FSE, __FUNCTION__,
                     "Cannot rewind token file `%s': %s",
                     tokenPath, TXstrerror(TXgeterror()));
              goto err;
            }
          if (wx->options.indexmeter != TXMDT_NONE)
            meter = openmeter("Reading original token file:",
                              wx->options.indexmeter,
                          MDOUTFUNCPN, MDFLUSHFUNCPN,
                          NULL, (EPI_HUGEINT)totalSz);
          if (wx->auxfldsz == 0 || totalSz <= mmapBufSz)
            {                                   /* read verbatim */
              tokenBuf = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__, totalSz);
              if (tokenBuf == BYTEPN) goto err;
              szToRead = FdbiReadBufSz;
              for (accumSz = 0; accumSz < totalSz; accumSz += szToRead)
                {
                  if (meter != METERPN) METER_UPDATEDONE(meter, accumSz);
                  if (totalSz - accumSz < szToRead)
                    szToRead = totalSz - accumSz;
                  if (tx_rawread(TXPMBUFPN, tokenDesc, tokenPath,
                                 tokenBuf + accumSz, szToRead, 1) !=
                      (int)szToRead)
                    goto err;
                }
              tokenBufTmp = tokenBuf;
              tokenBufTmpSz = totalSz;
            }
          else                                  /* read just recids */
            {
              tokenBufElSz = sizeof(RECID);
              tokenBuf = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__,
                                             tokenBufNum*sizeof(RECID));
              if (tokenBuf == BYTEPN) goto err;
              if ((szToRead = FdbiReadBufSz) < 8*wx->tokelsz)
                szToRead = 8*wx->tokelsz;
              szToRead = (szToRead/wx->tokelsz)*wx->tokelsz;
              tokenBufTmp = (byte*)TXmalloc(TXPMBUFPN, __FUNCTION__, szToRead);
              if (tokenBufTmp == BYTEPN) goto err;
              tokenBufTmpSz = szToRead;
              for (accumNum = 0, destRecid = (RECID *)tokenBuf;
                   accumNum < tokenBufNum;
                   )
                {
                  if (accumNum + szToRead/wx->tokelsz > tokenBufNum)
                    szToRead = (tokenBufNum - accumNum)*wx->tokelsz;
                  if (tx_rawread(TXPMBUFPN, tokenDesc, tokenPath, tokenBufTmp,
                                 szToRead, 1) != (int)szToRead)
                    goto err;
                  for (src = tokenBufTmp, srcEnd = src + szToRead;
                       src < srcEnd;
                       src += wx->tokelsz, destRecid++)
                    FDBI_TXALIGN_RECID_COPY(destRecid, src);
                  accumNum += szToRead/wx->tokelsz;
                  if (meter != METERPN)
                    METER_UPDATEDONE(meter, accumNum*wx->tokelsz);
                }
              tokenBufDesc = tokenDesc;         /* save for later */
            }
          if (meter != METERPN)
            meter_updatedone(meter, totalSz);
        }
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  if (meter != METERPN)
    {
      meter_end(meter);
      meter = closemeter(meter);
    }
  if (forNew)                                   /* COMPACT TABLE, new tok */
    {
      wx->tokbuf = tokenBuf;
      wx->tokbufIsMmap = tokenBufIsMmap;
      /* No need for tokenBufTmp: */
      if (tokenBufTmp != BYTEPN && tokenBufTmp != tokenBuf)
        tokenBufTmp = TXfree(tokenBufTmp);
      tokenBufTmp = BYTEPN;
      wx->tokbufNum = tokenBufNum;
      wx->tokelsz = tokenBufElSz;               /* wtf we lose original */
      wx->tokbufsz = tokenBufNum*tokenBufElSz;
    }
  else                                          /* normal update/create */
    {
      wx->tokbuforg = tokenBuf;
      wx->tokorgismmap = tokenBufIsMmap;
      wx->tokbuforgtmp = tokenBufTmp;
      wx->tokbuforgtmpsz = tokenBufTmpSz;
      wx->tokbuforgnum = tokenBufNum;
      wx->tokbuforgelsz = tokenBufElSz;
      wx->tokorgfh = tokenBufDesc;
      wx->tokbuforgtmprd = (EPI_OFF_T)0;
    }
  return(ret);
}

int
wtix_needfinish(wx)
WTIX    *wx;
/* Returns nonzero if wtix_finish() needs to be called to finish merge.
 * Should be called after wtix_getdellist(), etc.; just before
 * wtix_finish() and wtix_close().
 */
{
  return((wx->flags & WTIXF_FINISH) ? 1 : 0);
}

static int TXwtixCopyFile ARGS((int destFh, CONST char *destFn,
          int srcFh, CONST char *srcFn, EPI_OFF_T srcFileSz,
          METER *meter, EPI_OFF_T meterOffset));
static int
TXwtixCopyFile(destFh, destFn, srcFh, srcFn, srcFileSz, meter, meterOffset)
int             destFh;
CONST char      *destFn;
int             srcFh;
CONST char      *srcFn;
EPI_OFF_T       srcFileSz;
METER           *meter;         /* (in/out, opt.) progress meter to update */
EPI_OFF_T       meterOffset;    /* (in) meter_updatedone() offset */
/* Copies `srcFh' to `destFh'.
 * Returns 0 on error.
 */
{
  int                   ret;
  byte                  *buf = BYTEPN;
  EPI_OFF_T             accumSz, szToRead;

  if (!(buf = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__, FdbiReadBufSz)))
    goto err;
  if (EPI_LSEEK(srcFh, (EPI_OFF_T)0, SEEK_SET) != (EPI_OFF_T)0 ||
      EPI_LSEEK(destFh, (EPI_OFF_T)0, SEEK_SET) != (EPI_OFF_T)0)
    {
      putmsg(MERR + FSE, __FUNCTION__, "Cannot rewind file descriptors: %s",
             TXstrerror(TXgeterror()));
      goto err;
    }
  if (!TXtruncateFile(TXPMBUFPN, destFn, destFh, 0)) goto err;
  for (accumSz = 0; accumSz < srcFileSz; accumSz += szToRead)
    {
      if (meter != METERPN) METER_UPDATEDONE(meter, meterOffset + accumSz);
      szToRead = (EPI_OFF_T)FdbiReadBufSz;
      if (szToRead > srcFileSz - accumSz) szToRead = srcFileSz - accumSz;
      if (tx_rawread(TXPMBUFPN, srcFh, srcFn, buf, (size_t)szToRead, 1) !=
          (int)szToRead)
        goto err;
      if (tx_rawwrite(TXPMBUFPN, destFh, destFn, TXbool_False, buf,
                      (size_t)szToRead, TXbool_False) != (size_t)szToRead)
        goto err;
    }
  if (meter != METERPN) METER_UPDATEDONE(meter, meterOffset + accumSz);
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  buf = TXfree(buf);
  return(ret);
}

static int TXwtixCopyBtrDatNewDel ARGS((WTIX *wx));
static int
TXwtixCopyBtrDatNewDel(wx)
WTIX    *wx;
/* Copies original .btr and .dat files to new/update name.
 * For ALTER TABLE ... COMPACT, where we create a new .tok but not
 * a new .btr or .dat, so we must copy the originals so that all 3
 * .btr/.dat/.tok files are same base name.
 * NOTE: assumes write lock on index, because we make original-name
 * _X.btr and _Z.btr files here -- no one else should modify them
 * before TXcompactTable() flips them live, nor attempt to read them
 * while we are writing them here.
 * Returns 0 on error.
 */
{
  static CONST char     cannotClose[] = "Cannot close `%s': %s";
  /* no exts should be longer than ~10 chars; see `srcPath'/`destPath': */
  static CONST char * CONST     srcExts[] =  { "_D.btr", "_T.btr", CHARPN };
  static CONST char * CONST     destExts[] = { "_X.btr", "_Z.btr", CHARPN };
  EPI_STAT_S            st;
  int                   ret, i;
  int                   btrOrgFh = -1, datOrgFh = -1;
  int                   btrNewFh = -1, datNewFh = -1;
  char                  *btrOrgFn = CHARPN, *datOrgFn = CHARPN;
  char                  *btrNewFn = CHARPN, *datNewFn = CHARPN;
  EPI_OFF_T             btrOrgSz, datOrgSz, meterOffset;
  char                  *d;
  int                   srcFh = -1, destFh = -1;
  char                  srcPath[PATH_MAX + 10], destPath[PATH_MAX + 10];

  /* Sanity checks: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (wx->btorg == BTREEPN || wx->dforg == KDBFPN ||
      wx->bt == BTREEPN || wx->datdf == KDBFPN)
    {
      putmsg(MERR + UGE, __FUNCTION__, "Internal error: output index not set");
      goto err;
    }

  /* Init: - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* Flush newly-created BTREE now, before we steal its file descriptor.
   * We will overwrite it anyway, but this lets it close cleanly below
   * (without needing to write something via a non-existent file descriptor):
   */
  btflush(wx->bt);
  if ((btrOrgFh = TXkdbfReleaseDescriptor(TXdbfGetObj(wx->btorg->dbf))) < 0 ||
      (datOrgFh = TXkdbfReleaseDescriptor(wx->dforg)) < 0 ||
      (btrOrgFn = getdbffn(wx->btorg->dbf)) == CHARPN ||
      (datOrgFn = kdbf_getfn(wx->dforg)) == CHARPN ||
      (btrNewFh = TXkdbfReleaseDescriptor(TXdbfGetObj(wx->bt->dbf))) < 0 ||
      (datNewFh = TXkdbfReleaseDescriptor(wx->datdf)) < 0 ||
      (btrNewFn = getdbffn(wx->bt->dbf)) == CHARPN ||
      (datNewFn = kdbf_getfn(wx->datdf)) == CHARPN)
    {
      putmsg(MERR + UGE, __FUNCTION__,
            "Internal error: Cannot get .btr/.dat file descriptors or names");
      goto err;
    }
  if (EPI_FSTAT(btrOrgFh, &st) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, "Cannot stat `%s': %s",
             btrOrgFn, TXstrerror(TXgeterror()));
      goto err;
    }
  btrOrgSz = st.st_size;
  if (EPI_FSTAT(datOrgFh, &st) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, "Cannot stat `%s': %s",
             datOrgFn, TXstrerror(TXgeterror()));
      goto err;
    }
  datOrgSz = st.st_size;

  /* Set up meter: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (wx->options.indexmeter != TXMDT_NONE)
    {
      if (wx->meter != METERPN)
        {
          meter_end(wx->meter);                 /* just in case */
          wx->meter = closemeter(wx->meter);
        }
      /* wtf _D.btr and _T.btr file sizes should be negligible,
       * because TXcompactTable() should already have updated the
       * indexes before this WTIX was even opened, thus _D.btr and _T.btr
       * should be new/empty.  Do not both including in meter size:
       */
      wx->metersize = btrOrgSz + datOrgSz;
      if (wx->metersize < btrOrgSz || wx->metersize < datOrgSz)
        wx->metersize = EPI_OFF_T_MAX;          /* wtf overflow */
      wx->meter = openmeter("Copying dictionary and location files:",
                            wx->options.indexmeter, MDOUTFUNCPN,
                            MDFLUSHFUNCPN, NULL, (EPI_HUGEINT)wx->metersize);
    }

  /* Copy files: - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  meterOffset = 0;
  if (!TXwtixCopyFile(btrNewFh, btrNewFn, btrOrgFh, btrOrgFn, btrOrgSz,
                      wx->meter, meterOffset))
    goto err;
  meterOffset += btrOrgSz;
  if (!TXwtixCopyFile(datNewFh, datNewFn, datOrgFh, datOrgFn, datOrgSz,
                      wx->meter, meterOffset))
    goto err;
  meterOffset += datOrgSz;
  for (i = 0; srcExts[i] && destExts[i]; i++)   /* _D.btr and _T.btr */
    {
      if (!TXcatpath(srcPath, btrOrgFn, "")) goto err;
      d = TXfileext(srcPath);
      strcpy(d, srcExts[i]);
      if (!TXcatpath(destPath, btrOrgFn, "")) goto err;
      d = TXfileext(destPath);
      strcpy(d, destExts[i]);
      srcFh = TXrawOpen(TXPMBUFPN, __FUNCTION__, NULL, srcPath,
                        TXrawOpenFlag_None, (O_RDONLY | TX_O_BINARY), 0666);
      if (srcFh < 0) goto err;
      destFh = TXrawOpen(TXPMBUFPN, __FUNCTION__, NULL, destPath,
                         TXrawOpenFlag_None,
                         (O_WRONLY | O_CREAT | O_EXCL | TX_O_BINARY), 0666);
      if (destFh < 0) goto err;
      if (EPI_FSTAT(srcFh, &st) != 0)
        {
          putmsg(MERR + FTE, __FUNCTION__, "Cannot stat `%s': %s",
                 srcPath, TXstrerror(TXgeterror()));
          goto err;
        }
      if (!TXwtixCopyFile(destFh, destPath, srcFh, srcPath, st.st_size,
                          wx->meter, meterOffset))
        goto err;
      meterOffset += st.st_size;
      close(srcFh);
      srcFh = -1;
      TXclearError();
      if (close(destFh) != 0)
        {
          destFh = -1;
          putmsg(MERR + FCE, __FUNCTION__, cannotClose, destPath,
                 TXstrerror(TXgeterror()));
          goto err;
        }
      destFh = -1;
    }

  /* Clean up: - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  if (wx->meter)
    {
      meter_updatedone(wx->meter, wx->metersize);
      meter_end(wx->meter);
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;                                      /* error */
done:
  if (srcFh >= 0) close(srcFh);
  srcFh = -1;
  if (destFh >= 0) close(destFh);
  destFh = -1;
  if (btrOrgFh >= 0 &&
      close(btrOrgFh) != 0)
    putmsg(MERR + FCE, __FUNCTION__, cannotClose, btrOrgFn,
           TXstrerror(TXgeterror()));
  btrOrgFh = -1;
  if (datOrgFh >= 0 &&
      close(datOrgFh) != 0)
    putmsg(MERR + FCE, __FUNCTION__, cannotClose, datOrgFn,
           TXstrerror(TXgeterror()));
  datOrgFh = -1;
  if (btrNewFh >= 0 &&
      close(btrNewFh) != 0)
    putmsg(MERR + FCE, __FUNCTION__, cannotClose, btrNewFn,
           TXstrerror(TXgeterror()));
  btrNewFh = -1;
  if (datNewFh >= 0 &&
      close(datNewFh) != 0)
    putmsg(MERR + FCE, __FUNCTION__, cannotClose, datNewFn,
           TXstrerror(TXgeterror()));
  datNewFh = -1;
  /* We wrote to `wx->bt'/`wx->datdf' via direct file descriptor, and
   * released (stole) file descriptors above to prevent corruption via
   * any future flush.  Could close bt/btorg/df/dforg here, but keep
   * open so ABEND callback can properly delete bt/df.
   */
  return(ret);
}

int
wtix_finish(wx)
WTIX    *wx;
/* Called before close, to finish and flush stuff, for main
 * create/update index.  Returns 0 on error.
 */
{
  int                   ret;
  BMPILE                bpout;
  PILEFUNCS             bfout;

  memset(&bpout, 0, sizeof(BMPILE));
  memset(&bfout, 0, sizeof(PILEFUNCS));
  if (wx->meter != METERPN)                     /* "Indexing [new] data" */
    {
      meter_updatedone(wx->meter, wx->metersize);       /* last row */
      meter_end(wx->meter);                     /* but leave open for merge */
      if ((wx->flags & (WTIXF_FINISH|WTIXF_UPDATE)) == (WTIXF)0)
        {
          /* Creating an index on 0 rows or no data.  Call merge_finish()
           * anyway, to make the "Final merge" meter.  No pile funcs set,
           * since merge should not call any:
           */
          if (!merge_finish(wx->m, (PILE *)&bpout, 0)) goto err;
        }
    }
  if (!(wx->flags & WTIXF_FINISH)) goto ok;     /* nothing to do */

  if (!(wx->flags & (WTIXF_ERROR | WTIXF_COMPACT)) &&
      !wtix_flushrow(wx))
    goto err;
  if (wx->totvslerrs > 0L)
    putmsg(MERR, CHARPN, "%ld bad VSL values while %s index `%s'",
           wx->totvslerrs, ((wx->flags & WTIXF_UPDATE)?"updating":"creating"),
           kdbf_getfn(wx->datdf));
  if (wx->flags & WTIXF_ERROR) goto err;        /* previous severe error(s) */

  bpout.wx = wx;
  bpout.hdr.funcs = &bfout;
  if ((wx->flags & (WTIXF_UPDATE | WTIXF_COMPACT)) == WTIXF_UPDATE)
    {                                           /* updating && !compacting */
      EPI_HUGEINT       outMergeAddItems;

      if (wx->btorg == BTREEPN || wx->fxorg == FDBIXPN || wx->dforg == KDBFPN)
        {
          putmsg(MERR + UGE, __FUNCTION__,
                 "Internal error: output index not set");
          goto err;
        }
      /*   We could add the original index as another pile in the merge,
       * but since it typically has several times more items than all
       * of the other (new-list) piles, it will be the top pile on the
       * merge heap significantly more often than any other pile.
       * This means we'll often delete and re-insert it back to the
       * top, at about 2lg(P) cost each time (P=number of piles).
       * Since P usually doesn't decrease much until the end of the
       * merge, this costs us a lot of time to drag so many items
       * through the entire heap (maybe 50% of overall index time).
       *
       *   We save this time by manually merging the original index,
       * here and in bmpile_putupdate[slurp]: this costs one extra compare
       * (in bmpile_putupdate[slurp]) for every output item, but saves
       * 2lg(P)*orgitems compares (in the merge/heap), a large net
       * savings.  If the original index is small compared to the new
       * list, the net savings are less; at the extreme (empty
       * original index) it actually costs time, but very little (0.3%).
       * Note that this optimization partially makes up for not being
       * able to do indexslurp (yet) for the original index src pile.
       * NOTE: it is also assumed by bmpile_putslurp().
       * KNG 020109
       */
      /* Use to prime (get an org word) with wtix_getnextorg() call
       * here.  Moved to bmpile_putupdate[slurp]() so that all
       * merge_incdone() calls (via getnextorg) occur during merge (or
       * our flush just after), not before; i.e. during meter:
       */
      if (wx->flags & WTIXF_SLURP)
        bfout.put = bmpile_putupdateslurp;
      else if (wx->tokbuforgnum > 0)
        bfout.put = bmpile_putupdate;
      else                                      /* tiny optimization 020109 */
        bfout.put = bmpile_put;
      /* Bug 7019: copy of rest of original index moved to bmpile_finish()
       * (was inline after this merge_finish() call), so that meter updated:
       */
      bfout.mergeFinished = bmpile_mergeFinishedUpdate;
      outMergeAddItems = wx->orgIdxParamTblInfo.totalRowCount;
      if (outMergeAddItems < (EPI_HUGEINT)0) outMergeAddItems = 0;
      if (!merge_finish(wx->m, &bpout.hdr, outMergeAddItems)) goto err;
    }
  else                                          /* index create or COMPACT */
    {
      if (!wtix_flushtokens(wx)) goto err;
#if EPI_OS_LONG_BITS < EPI_OFF_T_BITS
      wx->stats.totentries = (wx->token > (EPI_OFF_T)EPI_OS_LONG_MAX ?
                              (long)EPI_OS_LONG_MAX : (long)wx->token);
#else /* EPI_OS_LONG_BITS >= EPI_OFF_T_BITS */
      wx->stats.totentries = (long)wx->token;
#endif /* EPI_OS_LONG_BITS >= EPI_OFF_T_BITS */
      bfout.put = ((wx->flags & WTIXF_SLURP) ? bmpile_putslurp : bmpile_put);
      if (!(wx->flags & WTIXF_COMPACT) &&
          !merge_finish(wx->m, &bpout.hdr, 0))
        goto err;
      /* We only created a new .tok file, but .btr/.dat/.tok must all
       * be the same (base) name.  So we need to copy the .btr and
       * .dat to the new base name too.  Bug 3684:
       */
      if ((wx->flags & WTIXF_UPDATE) &&
          !TXwtixCopyBtrDatNewDel(wx))
        goto err;
    }
  if (wx->flags & WTIXF_COMPACT)
    {
      /* The token file is being updated by ALTER TABLE ... COMPACT.
       * If it is also the index to be used for B-tree/inverted recid
       * translation, load the new token file into mem too:
       */
      if (wx->flags & WTIXF_LOADTOK)
        {
          EPI_STAT_S    st;

          if (EPI_FSTAT(wx->tokfh, &st) != 0)
            {
              putmsg(MERR + FTE, __FUNCTION__, "Cannot stat `%s': %s",
                     wx->tokfn, TXstrerror(TXgeterror()));
              goto err;
            }
          if (!TXwtixLoadTokenFile(wx, st.st_size, wx->tokfn, wx->tokfh, 1))
            goto err;
        }
    }
  else                                          /* normal update/create */
    {
      if (!wtix_flushword(wx)) goto err;
    }

ok:
  if (!(wx->flags & WTIXF_COMPACT))
    btflush(wx->bt);                            /* wtf return code */
  ret = 1;                                      /* wtf flush KDBF */
  goto done;

err:
  ret = 0;
done:
  wx->flags &= ~WTIXF_FINISH;                   /* don't retry if failure */
  return(ret);
}

int
TXwtixGetTotalHits(wx, paramTblInfo)
WTIX                    *wx;            /* (in) WTIX object */
TXMMPARAMTBLINFO        *paramTblInfo;  /* (out) info to fill out */
/* Gets totalRowCount, totalOccurrenceCount, totalWords, maxWordLen.
 * Does *not* get originalTableSize.
 * Should be called after wtix_finish().
 * Returns 0 on error.
 */
{
  EPI_OFF_T     saveTableSize;

  saveTableSize = paramTblInfo->originalTableSize;
  *paramTblInfo = wx->paramTblInfo;
  paramTblInfo->originalTableSize = saveTableSize;
  return(1);
}

int
wtix_setperms(name, mode, uid, gid)
char    *name;
int     mode, uid, gid;
{
  char  path[PATH_MAX];

  if (TXcatpath(path, name, TX_BTREE_SUFFIX))
    {
#ifndef _WIN32
      chown(path, uid, gid);
#endif
      chmod(path, mode);
    }
  if (TXcatpath(path, name, FDBI_DATSUF))
    {
#ifndef _WIN32
      chown(path, uid, gid);
#endif
      chmod(path, mode);
    }
  if (TXcatpath(path, name, FDBI_TOKSUF))
    {
#ifndef _WIN32
      chown(path, uid, gid);
#endif
      chmod(path, mode);
    }
  return(1);
}

int
wtix_btree2list(bt, auxfldsz, verbatim, list, sz, n)
BTREE   *bt;
size_t  auxfldsz;
int     verbatim;
byte    **list;
size_t  *sz;
size_t  *n;
/* Internal use. Reads keys from variable/fixed BTREE `bt' into array
 * `*list' (may be NULL), which is already size `*sz' (bytes),
 * realloced if needed.  `auxfldsz' is size of auxiliary field data
 * (usually 0).  Sets `*n' to number of items.  Returns 0 on error.
 * Checks that list is sorted ascending and unique by recid,
 * and returns it as such unless (verbatim & 1).  If (verbatim & 2),
 * reports every error.
 */
{
  BTLOC                 loc, prev;
  size_t                bsz, rsz, num, bufsz, auxoff, onum = 0;
  int                   ret, needsort = 0, baddup = 0, badrec = 0;
  byte                  *s, *d, *e;
  RECID                 lastbad;
  byte                  *tmp = BYTEPN;

  if (auxfldsz > 0)                             /* auxfld data: vbtree */
    {
      bufsz = FDBI_TOKEL_RECIDSZ + TX_ALIGN_UP_SIZE_T(auxfldsz);
      auxoff = FDBI_TOKEL_RECIDSZ;
    }
  else                                          /* no auxfld data: fbtree */
    {
      bufsz = sizeof(BTLOC);
      auxoff = 0;
    }
  TXsetrecid(&prev, (EPI_OFF_T)0);
  for (rsz = num = 0, rewindbtree(bt); ; num++)
    {
      rsz += bufsz;
      if (rsz > *sz && !fdbi_allocbuf(__FUNCTION__, (void **)list, sz, rsz))
        goto err;
      d = *list + bufsz*num;
      bsz = bufsz - auxoff;
      loc = btgetnext(bt, &bsz, d + auxoff, BYTEPPN);
      if (!TXrecidvalid2(&loc)) break;
      FDBI_TXALIGN_RECID_COPY(d, &loc);
      if (auxoff > 0)
        {
          CLRRECIDPAD(d);
          CLRAUXPAD(d, auxfldsz, bufsz);
        }
      /* If the current recid is less than or equal to the previous
       * one, the list is either out of order (expected for aux data),
       * or has non-unique entries (new-list update bug?):
       */
      if (TXgetoff2(&loc) <= TXgetoff2(&prev)) needsort = 1;
      prev = loc;
    }
  if (needsort)
    {
      /* Recid list needs sorting, probably because auxfldsz > 0 and
       * the tree was a vbtree "sorted" by aux data not recid.  But
       * check for non-aux data: should have been fbtree and didn't
       * need sort:  KNG 991122
       */
      if (auxfldsz == 0)
        putmsg(MERR, __FUNCTION__,
       "Recid list `%s' unexpectedly out of order, possible index corruption",
               getdbffn(bt->dbf));
      if (verbatim & 1)                         /* preserve order */
        {
          tmp = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__, num*bufsz);
          memcpy(tmp, *list, num*bufsz);
          onum = num;
        }
      qsort(*list, num, bufsz,
            (int (CDECL *) ARGS((CONST void *, CONST void *)))_recidcmp);
      /* Check for uniqueness after sort (possible new-list update bug?).
       * If delete or update list is not unique ascending the token math
       * in wtix_orgtok2new() and elsewhere gets hosed:  KNG 000126
       * Even if we unique the list, aux data may be incorrect (which one
       * is right?); this is corrected in wtix_startnewrow():  KNG 000315
       */
      TXsetrecid(&prev, (EPI_OFF_T)0);
      TXsetrecid(&lastbad, (EPI_OFF_T)0);
      for (s = d = *list, e = *list + num*bufsz; s < e; s += bufsz)
        {
          FDBI_TXALIGN_RECID_COPY(&loc, s);
          if (TXgetoff2(&loc) <= TXgetoff2(&prev))
            {
              num--;
              baddup++;
              if ((badrec == 0 || TXgetoff2(&lastbad) != TXgetoff2(&loc)) &&
                  (++badrec <= 3 || (verbatim & 2)))
                putmsg((verbatim ? MERR : MWARN), __FUNCTION__,
                       "Recid 0x%wx duplicated in index `%s'%s",
                       (EPI_HUGEINT)TXgetoff2(&loc), getdbffn(bt->dbf),
                       (verbatim ? "" : "; correcting"));
              lastbad = loc;
            }
          else
            {
              if (s != d) memcpy(d, s, bufsz);
              d += bufsz;
              prev = loc;
            }
        }
      if (badrec > 3 && !(verbatim & 2))
        putmsg(MERR, __FUNCTION__, "%d duplicates of %d recids in index `%s'",
               baddup, badrec, getdbffn(bt->dbf));
      if (verbatim & 1)                         /* restore original order */
        {
          num = onum;
          memcpy(*list, tmp, num*bufsz);
          tmp = TXfree(tmp);
        }
    }
  ret = 1;
  goto done;

err:
  ret = num = 0;
done:
  *n = num;
  return(ret);
}

int
wtix_getdellist(wx, bt)
WTIX    *wx;
BTREE   *bt;
/* Reads `bt' into ram, which is BTREE of deleted recids.
 * Assumes it's locked.  Returns 0 on error.
 */
{
  int   rc;

  rc = wtix_btree2list(bt, 0, ((wx->flags & WTIXF_VERIFY) ? 2 : 0),
                       (byte **)&wx->del, &wx->delsz, &wx->ndel);
  if (wx->ndel > 0) wx->flags |= WTIXF_FINISH;
  wx->stats.delentries = (long)wx->ndel;
  return(rc);
}

int
wtix_getnewlist(wx, bt)
WTIX    *wx;
BTREE   *bt;
/* Reads `bt' into ram, which is BTREE of new recids to index (..._T.btr).
 * Assumes it's locked.  Returns 0 on error.
 */
{
  int   rc;

  wx->curnew = wx->curinsnew = 0;
  rc = wtix_btree2list(bt, wx->auxfldsz, ((wx->flags & WTIXF_VERIFY) ? 2 : 0),
                       &wx->new, &wx->newsz, &wx->nnew);
  if (wx->nnew > 0) wx->flags |= WTIXF_FINISH;
  return(rc);
}

int
wtix_setupdname(wx, name)
WTIX    *wx;
char    *name;  /* (in) path to new temp index (sans extension) */
/* Sets (temporary) file name for update BTREE and opens those files.
 * Also creates new token list.  Returns 0 on error.
 * Also called during index verification (`name' is NULL).
 * Atomic: will delete all newly-created files on error.
 */
{
  static CONST RECID    noid = { (EPI_OFF_T)RECID_INVALID };
  BTREE                 *bt = BTREEPN;
  FDBIX                 *fx = FDBIXPN;
  KDBF                  *df = KDBFPN, *dftmp;
  int                   fh = -1, ret, btflags;
  char                  *tfn = CHARPN;
  size_t                sz;
  char                  path[PATH_MAX];

  if (!(wx->flags & WTIXF_VERIFY))
    {
      if (!(wx->flags & WTIXF_UPDATE))
        {
          putmsg(MERR + UGE, __FUNCTION__, "Internal error: Bad mode");
          goto err;
        }
      if (!(wx->flags & WTIXF_COMPACT))
        {
          /* WTF WTF WTF see wpile_flipwtix() WTF WTF WTF WTF WTF */
          if (!fdbi_allocbuf(__FUNCTION__, (void **)&wx->curorg, &wx->curorgsz,
                             sizeof(PILE) + BT_REALMAXPGSZ)) /* <- min size */
            goto err;
          fx = openfdbix(wx->datdf, (((wx->flags & WTIXF_FULL) ? FDF_FULL :
                                      (FDF)0) |
                                 (wx->orgversion >= 3 ? FDF_VSH7 : (FDF)0)),
                         FDBIWIPN, noid, (size_t)(-1), DWORDPN, 0, IndexWord,
                         FDBIPN);
          if (fx == FDBIXPN) goto err;
        }
    }

  /* mmap() or read the whole token -> recid file; we're going to use it
   * a lot:
   */
  /* WTF WTF WTF should this be separate function, called _during_ locks?
   * probably ok without, since .PID file locks original index...
   */
  if (!TXwtixLoadTokenFile(wx, wx->tokorgsz, wx->tokfn, wx->tokfh, 0))
    goto err;

  if (!(wx->flags & WTIXF_VERIFY))
    {
      /* Open new B-tree, dat and token files.  Done after token
       * file read above, to minimize delay between create here
       * and swap below: if abend happens in between, cb might not
       * delete these new files.
       * Bug 3684: need new .btr and .dat for WTIXF_COMPACT too;
       * will need to copy originals to new in TXwtixCopyBtrDatNewDel().
       */
      btflags = BT_LINEAR;
      if (TXindexBtreeExclusive) btflags |= BT_EXCLUSIVEACCESS;
      if ((bt = openbtree(name, FDBI_BTPGSZ, 10, btflags,
                          (O_RDWR | O_CREAT | O_EXCL))) == BTREEPN ||
          !TXcatpath(path, name, FDBI_DATSUF) ||
     (df = kdbf_open(TXPMBUFPN, path, (O_RDWR | O_CREAT | O_EXCL))) == KDBFPN)
        goto err;
      btsetcmp(bt, fdbi_btcmp);
      if (kdbf_get(df, (EPI_OFF_T)0L, &sz) != NULL)
        {                                   /* WTF merge with openwtix */
          putmsg(MERR + FOE, __FUNCTION__, "File `%s' already exists", path);
          goto err;
        }
      if (!wtix_setioctls(wx, df, 1)) goto err;
      TXcatpath(path, name, FDBI_TOKSUF);
      fh = TXrawOpen(TXPMBUFPN, __FUNCTION__, "Metamorph index token file",
                     path, TXrawOpenFlag_None,
                     (O_RDWR | O_CREAT | O_EXCL | TX_O_BINARY), 0600);
      if (fh < 0) goto err;
      TXseterror(0);
      if ((tfn = TXstrdup(TXPMBUFPN, __FUNCTION__, path)) == CHARPN) goto err;
    }

#ifdef EPI_HAVE_MMAP
  if (FdbiTraceIdx >= 1 && !(wx->flags & WTIXF_COMPACT))
    {
      if (TxIndexMmap & 2)
        putmsg(MINFO, CHARPN,
    "Partially mmap()ing Metamorph index data file `%s': (indexmmap & 2) set",
               kdbf_getfn(wx->datdf));
      else
        putmsg(MINFO, CHARPN,
               "Cannot mmap() Metamorph index data file `%s': (indexmmap & 2) off; using file I/O",
               kdbf_getfn(wx->datdf));
    }
#else /* !EPI_HAVE_MMAP */
  if (FdbiTraceIdx >= 1 && !(wx->flags & WTIXF_COMPACT))
    putmsg(MINFO, CHARPN,
           "Cannot mmap() Metamorph index data file `%s': EPI_HAVE_MMAP not defined; using file I/O",
           kdbf_getfn(wx->datdf));
#endif /* !EPI_HAVE_MMAP */

  wx->btorg = wx->bt;                           /* swap with main files */
  wx->bt = bt;
  bt = BTREEPN;                                 /* don't remove below */
  btsetsearch(wx->btorg, BT_SEARCH_BEFORE);
  rewindbtree(wx->btorg);                       /* make sure it's rewound */
  wx->wdorglen = 0;                             /* force start at new word */
  wx->fxorg = fx;
  fx = FDBIXPN;
  dftmp = wx->datdf;
  wx->datdf = df;
  wx->dforg = dftmp;                            /* do last, for abend cb */
  df = KDBFPN;

  if (wx->tokorgfh < 0)                         /* if org handle not needed */
    {
      TXseterror(0);
      if (close(wx->tokfh) != 0)
        putmsg(MWARN + FWE, __FUNCTION__,
               "Cannot close original Metamorph index token file `%s': %s",
               wx->tokfn, TXstrerror(TXgeterror()));
    }
  wx->tokfh = fh;                               /* swap token fields */
  fh = -1;
  wx->tokorgfn = TXfree(wx->tokorgfn);
  wx->tokorgfn = wx->tokfn;
  wx->tokfn = tfn;
  tfn = CHARPN;
  wx->tokbuffirst = (EPI_OFF_T)0;
  wx->tokbuflast = wx->tokbufsz/wx->tokelsz;
  wx->token = (EPI_OFF_T)0;
  TXsetrecid(&wx->prevrecid, (EPI_OFF_T)(-1));

  if (wx->flags & WTIXF_COMPACT)
    {
      /* Set `curinsnew' to 1 so we get correct token from
       * wtix_curinsnew2tok() call in TXwtixTranslateOldRecidToNew():
       */
      wx->curinsnew = 1;
    }
  else                                          /* not ALTER TABLE COMPACT */
    {
      if (!wtix_transtokens(wx))                /* failed */
        {                                       /* unwind for del at `err:' */
          bt = wx->bt;
          wx->bt = BTREEPN;
          df = wx->datdf;
          wx->datdf = KDBFPN;
          fh = wx->tokfh;
          wx->tokfh = -1;
          goto err;
        }

      if (wx->options.indexmeter != TXMDT_NONE && wx->meter == METERPN)
        {
          /* Delayed meter open until we know there's data to index or delete,
           * and after any read-tokens or wtix_transtokens() meters.
           * See also openwtix() for create-index meter:
           */
          wx->metersize = (EPI_OFF_T)wx->nnew;
          wx->meter = openmeter("Indexing new data:", wx->options.indexmeter,
                                MDOUTFUNCPN, MDFLUSHFUNCPN,
                                NULL, (EPI_HUGEINT)wx->metersize);
          merge_setmeter(wx->m, "Merging to temp file:",
                         "Final merge to index:",
                         wx->meter, 0, MDOUTFUNCPN, MDFLUSHFUNCPN, NULL);
        }
    }

  ret = 1;
  goto done;

err:
  wx->flags |= WTIXF_ERROR;
  ret = 0;
done:
  if (bt != BTREEPN)
    {
      TXstrncpy(path, getdbffn(bt->dbf), sizeof(path));
      closebtree(bt);
      TXseterror(0);
      if (unlink(path) != 0)
        putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
               TXstrerror(TXgeterror()));
    }
  closefdbix(fx);
  if (df != KDBFPN)
    {
      TXstrncpy(path, kdbf_getfn(df), sizeof(path));
      kdbf_close(df);
      TXseterror(0);
      if (unlink(path) != 0)
        putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
               TXstrerror(TXgeterror()));
    }
  if (fh != -1)
    {
      TXcatpath(path, name, FDBI_TOKSUF);
      close(fh);
      TXseterror(0);
      if (unlink(path) != 0)
        putmsg(MERR + FDE, __FUNCTION__, CantDel, path,
               TXstrerror(TXgeterror()));
    }
  tfn = TXfree(tfn);
  return(ret);
}

#if defined(WTREE_HUGEUINT_CMP) || defined(WTREE_HUGEUINT_REVCMP)
static char **wtix_dupnoiselist ARGS((char **slst));
static char **
wtix_dupnoiselist(slst)
char    **slst;
/* Duplicates ""-terminated string list `slst', making sure each item
 * is aligned, and 0-padded to at least EPI_HUGEUINT in size.
 */
{
  char                  **lst;
  int                   i, n;
  size_t                sz;

  if (slst == CHARPPN) return(CHARPPN);
  for (n = 0; *slst[n] != '\0'; n++);
  n++;
  if (!(lst = (char **)TXcalloc(TXPMBUFPN, __FUNCTION__, n, sizeof(char *))))
    goto merr;
  for (i = 0; i < n; i++)
    {
      sz = strlen(slst[i]);
      if (sz < sizeof(EPI_HUGEUINT)) sz = sizeof(EPI_HUGEUINT);
      if (!(lst[i] = (char *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sz + 1)))
        {
          for (i--; i >= 0; i--) lst[i] = TXfree(lst[i]);
          lst = TXfree(lst);
        merr:
          return(CHARPPN);
        }
      strcpy(lst[i], slst[i]);
    }
  return(lst);
}
#else /* !(WTREE_HUGEUINT_CMP || WTREE_HUGEUINT_REVCMP) */
#  define wtix_dupnoiselist     _duplst
#endif /* !(WTREE_HUGEUINT_CMP || WTREE_HUGEUINT_REVCMP) */

int
wtix_setnoiselist(wx, list)
WTIX    *wx;
char    **list;
/* Sets noise list for given WTIX `wx'.  Must be called before any
 * inserts.
 */
{
  _freelst(wx->noise);
  wx->noise = wtix_dupnoiselist(list);
  return(wx->noise != CHARPPN);
}

BTLOC
wtix_getnextnew(wx, sz, auxfld)
WTIX    *wx;
size_t  *sz;
void    *auxfld;
/* Returns next BTLOC from new list set with wtix_getnewlist(), with
 * `auxfld' set to auxfld data.  Returns invalid loc if at end of
 * list.  Acts like btgetnext().  `*sz' should be set to size of
 * buffer that `auxfld' points to; on return it is set to auxfld size.
 */
{
  BTLOC                 btloc;
  byte                  *s;

  if (wx->auxfldsz > 0 && *sz != wx->auxfldsz)  /* be semi-strict */
    {
      putmsg(MERR + UGE, __FUNCTION__,
             "Internal error: auxfld buffer wrong size");
      goto err;
    }
  if (wx->curnew < wx->nnew)                    /* still ones left */
    {
      s = wx->new + wx->tokelsz*wx->curnew;
      FDBI_TXALIGN_RECID_COPY(&btloc, s);
      if (wx->auxfldsz > 0) memcpy(auxfld, s+FDBI_TOKEL_RECIDSZ,wx->auxfldsz);
      wx->curnew++;
    }
  else
    {
    err:
      TXsetrecid(&btloc, (EPI_OFF_T)(-1));
    }
  return(btloc);
}

int
wtix_mergeclst(wx, out, org, ins)
WTIX    *wx;
BTREE   *out, *org, *ins;
/* Merges _C count BTREEs.  `org' is existing tree, `ins' is new record
 * counts.  Deletes records from previously-called wtix_setdellist().
 * Outputs to `out'.  Returns 0 on error.
 * wtf based on updindex.c:mergeclst(); should be merged with it someday.
 */
{
  BTLOC                 insloc, orgloc;
  size_t                inss, orgs, curdel;
  EPI_OFF_T             insv, orgv, delv = (EPI_OFF_T)(-1);
  int                   dok;

  if (ioctlbtree(out, BTREE_IOCTL_LINEAR, (void *)1) < 0)   /* should be ok */
    goto err;
  /* BTREE_IOCTL_EXCLUSIVEACCESS can fail benignly on RAM DBF,
   * but ioctlbtree() silently returns ok for that.  So error here is real:
   */
  if (TXindexBtreeExclusive &&
      ioctlbtree(out, BTREE_IOCTL_EXCLUSIVEACCESS, (void *)1) != 0)
    {
      putmsg(MERR, __FUNCTION__,
   "Could not set BTREE_IOCTL_EXCLUSIVEACCESS on Metamorph index B-tree `%s'",
             getdbffn(out->dbf));
      goto err;
    }

  rewindbtree(org);
  rewindbtree(ins);
  inss = orgs = sizeof(insv);
  insloc = btgetnext(ins, &inss, &insv, BYTEPPN);
  orgloc = btgetnext(org, &orgs, &orgv, BYTEPPN);
  curdel = 0;
  dok = curdel < wx->ndel;
  if (dok) delv = TXgetoff2(&wx->del[curdel++]);
  while (TXrecidvalid2(&orgloc) && TXrecidvalid2(&insloc))
    {
      while (dok && delv < orgv)
        {
          dok = curdel < wx->ndel;
          if (dok) delv = TXgetoff2(&wx->del[curdel++]);
        }
      if (orgv < insv)
        {
          if (!dok || (orgv != delv))
            if (btappend(out, &orgloc, orgs, &orgv, WTIX_BTREEPCT, BTBMPN) < 0)
              goto err;
          orgs = sizeof(orgv);
          orgloc = btgetnext(org, &orgs, &orgv, BYTEPPN);
          continue;
        }
      if (orgv > insv)
        {
          if (btappend(out, &insloc, inss, &insv, WTIX_BTREEPCT, BTBMPN) < 0)
            goto err;
          inss = sizeof(insv);
          insloc = btgetnext(ins, &inss, &insv, BYTEPPN);
          continue;
        }
      if (btappend(out, &insloc, inss, &insv, WTIX_BTREEPCT, BTBMPN) < 0)
        goto err;
      inss = orgs = sizeof(insv);
      insloc = btgetnext(ins, &inss, &insv, BYTEPPN);
      orgloc = btgetnext(org, &orgs, &orgv, BYTEPPN);
    }
  while (TXrecidvalid2(&insloc))                        /* finish new ones */
    {
      if (btappend(out, &insloc, inss, &insv, WTIX_BTREEPCT, BTBMPN) < 0)
        goto err;
      inss = sizeof(insv);
      insloc = btgetnext(ins, &inss, &insv, BYTEPPN);
    }
  while (TXrecidvalid2(&orgloc))                        /* finish original */
    {
      while (dok && delv < orgv)
        {
          dok = curdel < wx->ndel;
          if (dok) delv = TXgetoff2(&wx->del[curdel++]);
        }
      if (!dok || (orgv != delv))
        if (btappend(out, &orgloc, orgs, &orgv, WTIX_BTREEPCT, BTBMPN) < 0)
          goto err;
      orgs = sizeof(orgv);
      orgloc = btgetnext(org, &orgs, &orgv, BYTEPPN);
    }
  return(1);
err:
  return(0);
}

int
wtix_getstats(wx, stats)
WTIX            *wx;
INDEXSTATS      *stats;
/* Returns index statistics in `*stats'.  Should be called after
 * index is finished.  Returns 0 on error.
 */
{
  *stats = wx->stats;
  return(1);
}

/* -------------------------- Pile wrapper for merge ----------------------- */

static int wpile_flipwtix ARGS((WPILE *wp, WTIX *wx));
static int
wpile_flipwtix(wp, wx)
WPILE   *wp;
WTIX    *wx;
/* Prepares WTIX for wtix_getnextorg().  WTF see also wtix_setupdname().
 * Returns 0 on error.
 */
{
  static CONST RECID    noid = { (EPI_OFF_T)(-1) };
  FDF                   fdflags;

  if (!fdbi_allocbuf(__FUNCTION__, (void **)&wx->curorg, &wx->curorgsz,
                     sizeof(PILE) + BT_REALMAXPGSZ))    /* <- min size */
    goto err;
  wx->btorg = wx->bt;
  wx->bt = BTREEPN;
  wx->dforg = wx->datdf;
  wx->datdf = KDBFPN;

  /* WTF mod ioctlbtree() to allow BT_LINEAR to be turned off: */
  /* ioctlbtree() can already turn off BT_EXCLUSIVEACCESS: */
  closebtree(wx->btorg);
  wx->btorg = openbtree(wp->path, FDBI_BTPGSZ, 10, 0, O_RDONLY);
  if (wx->btorg == BTREEPN) goto err;
  btsetcmp(wx->btorg, fdbi_btcmp);
  if (!wtix_setioctls(wx, wx->dforg, 0)) goto err;
  fdflags = (FDF)0;
  if (wx->flags & WTIXF_FULL) fdflags |= FDF_FULL;
  if (wx->options.indexVersion >= 3) fdflags |= FDF_VSH7;
  wx->fxorg = openfdbix(wx->dforg, fdflags, FDBIWIPN, noid, (size_t)(-1),
                        DWORDPN, 0, IndexWord, FDBIPN);
  if (wx->fxorg == FDBIXPN) goto err;           /* WTF whack bufsize up? */
  /* no token fields used (?) */
  btsetsearch(wx->btorg, BT_SEARCH_BEFORE);
  rewindbtree(wx->btorg);                       /* make sure it's rewound */
  wx->wdorglen = 0;                             /* force start at new word */
  return(1);
err:
  return(0);
}

static char *wpile_mktemp ARGS((WPILE *org));
static char *
wpile_mktemp(org)
WPILE   *org;
/* Returns a (malloc'd) temp file name in an indextmp dir, based on
 * available free space.  Returns NULL on error.
 */
{
  char          *path, *d, dch;
  char          **idxtmp, *idxdef[8];
  int           i, max;
  EPI_OFF_T     fr, maxfree, est;
  EPI_STAT_S    st, maxst;
  dev_t         indexdev;
  char          buf[PATH_MAX];

  if (EPI_STAT(kdbf_getfn(org->wxorg->datdf), &st) == 0)    /* WTF drill */
    indexdev = st.st_dev;
  else
    indexdev = (dev_t)(-1);

  if ((idxtmp = TXgetglobalindextmp()) == CHARPPN ||
      *idxtmp == CHARPN ||
      **idxtmp == '\0')
    {                                           /* no indextmp; use $TMP */
      i = 0;
      idxdef[i++] = kdbf_getfn(org->wxorg->datdf);      /* WTF drill */
      if ((path = getenv("TMP")) != CHARPN)
        idxdef[i++] = path;
      if ((path = getenv("TMPDIR")) != CHARPN)
        idxdef[i++] = path;
      idxdef[i++] = "";
      idxtmp = idxdef;
    }

  /* Pick the filesystem with the most free space, from indextmp values: */
  max = -1;
  maxfree = (EPI_OFF_T)0L;
  for (i = 0; idxtmp[i] != CHARPN && *idxtmp[i] != '\0'; i++)
    {
      TXdiskSpace       diskSpace;

      path = idxtmp[i];
      if (EPI_STAT(path, &st) != 0) continue;   /* error */
      TXgetDiskSpace(path, &diskSpace);
      fr = (diskSpace.availableBytes == (EPI_HUGEINT)(-1) ?
            (EPI_OFF_T)(-1) : (EPI_OFF_T)(diskSpace.availableBytes >> 20));
      if (st.st_dev == indexdev)                /* same filesystem as index */
        {
          est = org->wxorg->estindexsize;
          if (est > fr)
            fr = 1L;
          else
            fr -= est;                          /* reserve space for index */
        }
      if (fr > maxfree)
        {
          max = i;
          maxfree = fr;
          maxst = st;
        }
    }
  if (max >= 0 && TXcatpath(buf, idxtmp[max], ""))
    {
      if (!S_ISDIR(maxst.st_mode))              /* chop to dir */
        {
          d = strrchr(buf, PATH_SEP);
#if defined(MSDOS)
          if (d == CHARPN) d = strrchr(buf, '/');
#endif
          if (d == CHARPN) goto def;            /* WTF use cwd? */
          if (d == buf) d++;
          *d = '\0';
          if (EPI_STAT(buf, &maxst) != 0 || !S_ISDIR(maxst.st_mode))
            goto def;
        }
      path = TXtempnam(buf, CHARPN, CHARPN);
      goto done;
    }

def:
  path = kdbf_getfn(org->wxorg->datdf);         /* WTF avoid drill */
  if ((d = strrchr(path, PATH_SEP)) == CHARPN
#if defined(MSDOS)
      && (d = strrchr(path, '/')) == CHARPN
#endif
      )
    d = path + strlen(path);
  dch = *d;
  *d = '\0';
  path = TXtempnam(path, CHARPN, CHARPN);
  *d = dch;
done:
  return(path);
}

static PILE *
openwpile(flags, bufsz, wxorg)
int     flags;          /* PILEF flags */
size_t  bufsz;          /* ignored for now */
void    *wxorg;         /* original WTIX object */
/* Opens WPILE object.  Used during intermediate/final stages of merge.
 */
{
  WPILE                 *wp;
  WTIXF                 wf;

  (void)bufsz;
  TXseterror(0);
  if (!(wp = (WPILE *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(WPILE))))
    goto err;
  wp->hdr.funcs = &WPileFuncs;  /* NOTE: bmpile_putslurp etc. assumes this */
  wp->org = wp;
  wp->refcnt = 1;
  wp->last = wp;
  wp->flags = (PILEF)flags;
  wp->wxorg = (WTIX *)wxorg;
  /* rest cleared by calloc() -- also see wpile_next() */
  if ((wp->path = wpile_mktemp(wp->org)) == CHARPN) goto err;
  wf = (WTIXF_INTER | (wp->wxorg->flags & (WTIXF_FULL | WTIXF_SLURP)));
  wp->wx = openwtix(DBTBLPN, CHARPN, wp->path, wp->wxorg->auxfldsz,
                    &wp->wxorg->options, NULL, wf,
                    wp->wxorg->options.indexVersion, wp->wxorg);
  if (wp->wx == WTIXPN) goto err;
  goto done;

err:
  wp = (WPILE *)closewpile((PILE *)wp);
done:
  return((PILE *)wp);
}

static PILE *
closewpile(wptr)
PILE    *wptr;
/* Closes WPILE and deletes index.
 */
{
  int   type;
  WPILE *wp = (WPILE *)wptr, *np, *next;

  if (wp == WPILEPN) goto done;

  /* If we're in write mode, then the user hasn't seen our subsidiary
   * piles (no flip yet), so close them too.  Otherwise we'll leak mem,
   * file handles and disk space if closing merge early on error: KNG 000330
   */
  if (wp == wp->org && (wp->flags & PILEF_WRITE))
    {
      for (np = wp->next; np != WPILEPN; np = next)
        {
          next = np->next;
          closewpile((PILE *)np);
        }
      wp->next = WPILEPN;                       /* don't re-close later */
    }

  /* Must close the original pile last, so check refcnt first: */
  wp->org->refcnt -= 1;                         /* one less pile */
  if (wp == wp->org &&                          /* if I am the original and */
      wp->refcnt > 0)                           /*   others are still open */
    goto done;                                  /*   then postpone close */

  /* wtf flush? */
  type = (wp->wx != WTIXPN && (wp->wx->flags & WTIXF_FULL)) ? INDEX_FULL :
    INDEX_MM;
  closewtix(wp->wx);
  if (wp->path != CHARPN)
    {
      TXdelindex(wp->path, type);               /* we're an intermediate */
      wp->path = TXfree(wp->path);
    }

  if (wp->org->refcnt <= 0 &&                   /* if I am last one out and */
      wp != wp->org)                            /*   I am not the original */
    closewpile((PILE *)wp->org);                /*   then close original too*/
  wp = TXfree(wp);

done:
  return(PILEPN);
}

static int
wpile_put(wptr, src)
PILE    *wptr;
PILE    *src;
/* Pile output function for intermediate piles.
 */
{
  WPILE                 *wp = (WPILE *)wptr;

  wp = wp->last;                                        /* switch to writer */
#ifdef WTIX_SANITY
  if (!(wp->flags & PILEF_WRITE))
    {
      putmsg(MERR + UGE, __FUNCTION__, "Cannot write to read-only pile");
      return(0);
    }
#endif
  wp->nitems++;
  return(wtix_out(wp->wx, src));        /* wtf return 2 if we want merge? */
}

static int
wpile_get(wptr)
PILE    *wptr;
/* Gets next item from intermediate pile.  Returns 1 if ok, 0 on EOF,
 * -1 on error.
 */
{
  int                   ret;
  WTIX                  *wx;
  WPILE                 *wp = (WPILE *)wptr;
#ifdef WTIX_HUGEUINT_CMP
  byte                  *d, *e;
#endif /* WTIX_HUGEUINT_CMP */

  wx = wp->wx;
#ifdef WTIX_SANITY
  if (wp->flags & PILEF_WRITE)
    {
      putmsg(MERR + UGE, __FUNCTION__, "Cannot read from write-only pile");
      goto err;
    }
#endif
  switch (wtix_getnextorg(wx))
    {
    case 1:  break;                             /* ok */
    case 0:  ret = 0;  goto done;               /* EOF */
    default: goto err;                          /* error */
    }
  wp->hdr.blk = wx->curorg->blk;                /* WTF same pile? */
  wp->hdr.blksz = wx->curorg->blksz;
#ifdef WTIX_HUGEUINT_CMP
  /* NOTE: assumes there's room to do this; ok since BT_REALMAXPGSZ: */
  for (d = wp->hdr.blk + wp->hdr.blksz, e = wp->hdr.blk + sizeof(EPI_HUGEUINT);
       d < e;
       d++)
    *d = 0;
#endif /* WTIX_HUGEUINT_CMP */
  ret = 1;
  goto done;

err:
  ret = -1;
done:
  return(ret);
}

static PILE *
wpile_next(wptr)
PILE    *wptr;
/* Starts new pile.  If in write mode, returns same handle.  If read mode,
 * returns new handle.  Returns NULL on error, or EOF (read mode).
 * (Should only be called if intermediate merge pile.)
 */
{
  WPILE                 *wp = (WPILE *)wptr, *np = WPILEPN;
  WTIXF                 wf;

#ifdef WTIX_SANITY
  if (wp != wp->org)                            /* wtf Do The Right Thing? */
    {
      putmsg(MERR + UGE, __FUNCTION__, "Attempt to re-clone a cloned pile");
      goto err;
    }
#endif
  if (wp->flags & PILEF_WRITE)                  /* write mode */
    {
      /* WTF WTF WTF merge with wpile_flip(): */
      if (!wtix_flushword(wp->last->wx)) goto err; /* flush current writer */
      wp->last->hdr.blk = BYTEPN;               /* clear just in case */
      wp->last->hdr.blksz = 0;
      /* flag in flip, in case this is original pile (still need write mode
       * for future next's: */
      /*wp->last->flags &= ~WPILEF_WRITE;*/
      if (!wpile_flipwtix(wp->last, wp->last->wx)) goto err;
      /* Must create new pile now, even though we return the original,
       * because we need to open a new WTIX:
       */
      if (!(np = (WPILE *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(WPILE))))
        goto err;
      np->hdr.funcs = &WPileFuncs;              /* bmpile_slurp assumes this*/
      np->org = wp->org;
      np->flags = (wp->flags | PILEF_WRITE);
      np->wxorg = wp->wxorg;
      wp->last->next = np;
      wp->last = np;
      wp->refcnt += 1;
      wf = (WTIXF_INTER | (np->wxorg->flags & (WTIXF_FULL | WTIXF_SLURP)));
      if ((np->path = wpile_mktemp(np->org)) == CHARPN ||
          (np->wx = openwtix(DBTBLPN, CHARPN, np->path, np->wxorg->auxfldsz,
                             &np->wxorg->options, NULL, wf,
                             np->wxorg->options.indexVersion, np->wxorg)) ==
          WTIXPN)
        {
          np = WPILEPN;                         /* already linked in */
          goto err;
        }
      /* rest cleared by calloc(); see also openwpile() */
    }
  else                                          /* read mode */
    {
      np = wp->next;
      if (np == WPILEPN) goto done;             /* end of piles */
      wp->next = np->next;                      /* for next wpile_next() */
    }
  goto done;

err:
  np = (WPILE *)closewpile((PILE *)np);
done:
  return((PILE *)np);
}

static int
wpile_flip(wptr)
PILE    *wptr;
/* Flips pile from write mode to read mode.  Returns 0 on error.
 */
{
  int                   ret;
  WPILE                 *wp = (WPILE *)wptr, *np;

  wp = wp->last;                                /* switch to writer */
#ifdef WTIX_SANITY
  if (!(wp->flags & PILEF_WRITE))
    {
      putmsg(MERR + UGE, __FUNCTION__, "Cannot flip read-only pile");
      goto err;
    }
#endif
  /* WTF flush: see also wpile_next(); merge code? */
  if (!wtix_flushword(wp->wx)) goto err;
  wp->hdr.blk = BYTEPN;                         /* clear just in case */
  wp->hdr.blksz = 0;
  if (!wpile_flipwtix(wp, wp->wx)) goto err;
  for (np = wp->org; np != WPILEPN; np = np->next)
    np->flags &= ~PILEF_WRITE;
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

static size_t
wpile_npiles(wptr)
PILE    *wptr;
{
  return((size_t)((WPILE *)wptr)->refcnt);
}

static EPI_HUGEINT
wpile_nitems(wptr)
PILE    *wptr;
{
  return(((WPILE *)wptr)->nitems);
}

/* --------------------- Pile wrapper for update merge -------------------- */

static int
bmpile_put(bp, src)
PILE    *bp;
PILE    *src;
/* Pile output function for final merge, during index creation.
 * Also used during certain index updates (e.g. no original data).
 */
{
  return(wtix_out(((BMPILE *)bp)->wx, src));
}

static int
bmpile_putupdate(bp, src)
PILE    *bp;
PILE    *src;
/* Pile output function for final merge, during index update (not creation).
 * Merges with existing index, adding new list items (this `src') and
 * deleting delete list.  Returns 0 on error.
 */
{
  WTIX  *wx;
  PILE  *org;

  wx = ((BMPILE *)bp)->wx;

  if (wx->wdorglen == 0 &&                      /* very first call: */
      wtix_getnextorg(wx) < 0)                  /*   prime us with a word */
    goto err;

  /* KNG 020329: wx->curorg may change, reset for each loop: */
  while ((org = wx->curorg)->blksz > 0 &&       /* while still original data*/
         WTIX_CMP(org, src) < 0)                /*   and original < new blk */
    {
      if (!wtix_out(wx, org) ||                 /* output it */
          wtix_getnextorg(wx) < 0)              /*   and get next original */
        goto err;
    }
  return(wtix_out(wx, src));

err:
  return(0);
}

static int
bmpile_putslurp(bp, src)
PILE    *bp;
PILE    *src;
/* Same as bmpile_put(), but for "slurp" optimization (may read whole word's
 * worth of data from `src' and output it, e.g. more than current item).
 * May be used during index create and certain updates.  Returns 0 on error.
 */
{
  WTIX          *wxout, *wxsrc;
  FDBIWI        wi;
  byte          *datbuf;
  size_t        datsz;
  int           ret;

  wxout = ((BMPILE *)bp)->wx;
  /* This optimization is possible only if `src' is an intermediate pile:
   * mem piles we cannot drill via fdbix_slurp(), and are too small to get
   * any time savings anyway.  So output `src' normally ala bmpile_put().
   * WTF more reliable way to detect `src' is not WPILE?:
   */
  if (src->funcs != &WPileFuncs) return(wtix_out(wxout, src));

  /*   We're about to output `src's top word/token item, so we know it
   * occurs before any other source pile's.  Since table data is read
   * in token order as it is indexed, we also know `src' pile's
   * _entire_ token set for this word will also occur before all other
   * source piles' entire token sets for this word.  We can thus save
   * some merge effort and directly output all remaining word/token
   * items of this `src' word here -- which may be a lot since this is
   * an intermediate pile.  (This is not possible in general for index
   * _update_, because original-index tokens may be intermingled with
   * new-list tokens for a word, so neither intermediate nor original
   * index src piles are contiguous for a word.  But it is possible if
   * WTIXF_APPEND is set: all new-list tokens occur after original.
   * This is checked when WTIXF_SLURP is set.)  It's much like a
   * 2-digit radix sort: the input was already sorted by the secondary
   * digit, the token.
   *
   *   We save even more time by skipping the decoding of .dat data,
   * because it's in the same format as the output.  All but the first
   * token is relative to the previous: nothing will change except the
   * first token.  (This is not possible for original-index `src' data
   * because token-translation is needed, unless WTIXF_APPEND is set.
   * But `src' is never the original index anyway: that's handled by
   * bmpile_putupdate... and wtix_finish.)
   *
   *   We do this here instead of the merge code because:
   *     o  The merge code cannot know this optimization is possible;
   *        it depends on the sequence of input piles' tokens and
   *        the nature of our sort function
   *     o  We'd need another pile function to get-data-for-output
   *        (our whole word's .dat) that is distinct from the current
   *        get-data-for-sorting (our next token for this word).
   *     o  This optimization is not always possible for index update
   * The price is some drilling into `src' and `bp' piles.
   * KNG 011025
   */

  /* We must output first token normally via wtix_out() for it to be
   * properly relativized in target pile; it's already been read
   * anyway.  We also subtract its ndocs/nlocs from `src' pile's total
   * word counts to keep them correct:
   */
  wxsrc = ((WPILE *)src)->wx;
  wi = wxout->wi;                                       /* WTF drill */
  if (!wtix_out(wxout, src)) goto err;
  if (wxout->wi.ndocs > wi.ndocs)                       /* still same word */
    wi.nlocs = wxsrc->srcIdxWordInfo.nlocs - (wxout->wi.nlocs - wi.nlocs);
  else                                                  /* new output word */
    wi.nlocs = wxsrc->srcIdxWordInfo.nlocs - wxout->wi.nlocs;
  wi.ndocs = wxsrc->srcIdxWordInfo.ndocs - (EPI_HUGEUINT)1;

  /* Now output the rest of `src's .dat data: */
  while ((datsz = fdbix_slurp(wxsrc->fxorg, &datbuf)) != (size_t)0)
    if (!wtix_outslurp(wxout, datbuf, datsz)) goto err;
  if (fdbix_iserror(wxsrc->fxorg)) goto err;            /* was read error */
  if (!wtix_flushslurp(wxout, &wi, wxsrc->srcIdxLastToken)) goto err;
  /* We slurped some of `src's data so merge doesn't have to merge it;
   * let merge meter know:
   */
  if (wxout->options.indexmeter != TXMDT_NONE)
    merge_incdone(wxout->m, wi.ndocs);                  /* update meter */
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

static int
bmpile_putupdateslurp(bp, src)
PILE    *bp;
PILE    *src;
/* Pile output function for final merge, during index update with indexslurp.
 * Merges original index with indexslurp-read new list (`src').
 */
{
  WTIX  *wx;
  PILE  *org;

  wx = ((BMPILE *)bp)->wx;

  if (wx->wdorglen == 0 &&                      /* very first call: */
      wtix_getnextorg(wx) < 0)                  /*   prime us with a word */
    goto err;

  /* WTF someday apply slurp optimization to original index here and in
   * wtix_finish(), if indexappend set: new indexversion needed for lasttok
   * KNG 020329 wx->curorg may change; reset for each loop:
   */
  while ((org = wx->curorg)->blksz > 0 &&       /* while still original data*/
         WTIX_CMP(org, src) < 0)                /*   and original < new blk */
    {
      if (!wtix_out(wx, org)) goto err;
      if (wtix_getnextorg(wx) < 0) goto err;
    }
  return(bmpile_putslurp(bp, src));

err:
  return(0);
}

static int
bmpile_mergeFinishedUpdate(PILE *pile)
/* Called once after final output merge, for index updates (slurp or not).
 * Merges (i.e. copies) any remaining original index data.  May continue
 * to update final merge meter (via merge_incdone() in wtix_getnextorg()).
 * Returns 0 on error.
 */
{
  WTIX  *wx = ((BMPILE *)pile)->wx;
  int   ret;

  if (wx->wdorglen == 0 &&                      /* very first call: */
      wtix_getnextorg(wx) < 0)                  /*   prime us with a word */
    goto err;

  while (wx->curorg->blksz > 0)                 /* while original data */
    {
      if (!wtix_out(wx, wx->curorg)) goto err;
      if (wtix_getnextorg(wx) < 0) goto err;
    }                                           /* no token flush; done */
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

/* ----------------------------- Integrity test ---------------------------- */

int
wtix_verify(ddic, tname, iname, fname, ifile, itype, sysindexParams, verbose)
DDIC            *ddic;
char            *tname, *iname, *fname, *ifile;
int             itype;
CONST char      *sysindexParams;
int             verbose;
/* Returns 0 if error(s) detected, 1 if ok.
 */
{
  static CONST char     cantstat[] = "Cannot stat `%s': %s";
  EPI_STAT_S            sttok, stnew, stdel, sttbl;
  EPI_STAT_S            asttok, astnew, astdel, asttbl;
  int                   ret = 1;
  DBTBL                 *dbtbl;
  WTIX                  *wx = WTIXPN;
  A3DBI                 *dbi = A3DBIPN;
  RECID                 *lptr, loc, prev, *tblrecids = RECIDPN;
  size_t                rsz, numtblrecids, asz;
  TXfdbiIndOpts         options;

  if ((dbtbl = opendbtbl(ddic, tname)) == DBTBLPN)
    {
      putmsg(MERR + FOE, __FUNCTION__, "Cannot open table `%s'", tname);
      goto err;
    }
  if ((dbi = open3dbi(ifile, PM_ALLPERMS, itype, sysindexParams)) == A3DBIPN)
    goto err;
  TXfdbiIndOpts_INIT_FROM_DBI(&options, dbi);
  options.indexmeter = TXMDT_NONE;      /* not really an index operation? */
  wx = openwtix(dbtbl, fname, ifile, dbi->auxsz, &options, &dbi->paramTblInfo,
        (itype == INDEX_FULL ? WTIXF_FULL : 0) | (WTIXF_VERIFY | WTIXF_UPDATE),
                dbi->version, WTIXPN);
  if (wx == WTIXPN) goto err;

  TXseterror(0);
  if (EPI_STAT(getdbffn(dbtbl->tbl->df), &sttbl) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbtbl->tbl->df), TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(wx->tokfn, &sttok) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             wx->tokfn, TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(getdbffn(dbi->newrec->dbf), &stnew) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbi->newrec->dbf), TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(getdbffn(dbi->del->dbf), &stdel) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbi->del->dbf), TXstrerror(TXgeterror()));
      goto err;
    }

  if (verbose)
    putmsg(MINFO, CHARPN, "Reading new list from index `%s'", iname);
  if (!wtix_getnewlist(wx, dbi->newrec)) goto err;

  if (verbose) putmsg(MINFO, CHARPN, "Reading delete list");
  if (!wtix_getdellist(wx, dbi->del)) goto err;

  if (verbose) putmsg(MINFO, CHARPN, "Reading recids from table `%s'", tname);
  TXsetrecid(&prev, (EPI_OFF_T)0);
  for (rsz = numtblrecids = asz = 0; ; numtblrecids++)
    {
      rsz += sizeof(RECID);
      if (rsz > asz &&
          !fdbi_allocbuf(__FUNCTION__, (void **)&tblrecids, &asz, rsz))
        goto err;
      lptr = getdbtblrow(dbtbl);
      if (lptr == RECIDPN) break;
      tblrecids[numtblrecids] = loc = *lptr;
      if (TXgetoff2(&loc) <= TXgetoff2(&prev))
        {
          putmsg(MERR, __FUNCTION__, "Out-of-order table recid 0x%wx",
                 (EPI_HUGEINT)TXgetoff2(&loc));
          ret = 0;
        }
      prev = loc;
    }
  wx->tblrecids = tblrecids;
  wx->numtblrecids = numtblrecids;
  wx->curtblrecid = (size_t)0;
  tblrecids = RECIDPN;

  /* WTF: also check aux data, don't abort early in tpile_getorg(): */
  if (verbose) putmsg(MINFO, CHARPN, "Comparing token recids to table");
  if (!wtix_setupdname(wx, CHARPN)) ret = 0;
  if (wx->curtblrecid > wx->numtblrecids)
    putmsg(MERR, CHARPN, "%wd too many token/new list recids",
           (EPI_HUGEUINT)wx->curtblrecid - (EPI_HUGEUINT)wx->numtblrecids);
  else if (wx->curtblrecid < wx->numtblrecids)
    putmsg(MERR, CHARPN,
           "%wd too few token/new list recids (or abort from earlier errors)",
           (EPI_HUGEUINT)wx->numtblrecids - (EPI_HUGEUINT)wx->curtblrecid);

  TXseterror(0);
  if (EPI_STAT(getdbffn(dbtbl->tbl->df), &asttbl) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbtbl->tbl->df), TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(wx->tokorgfn, &asttok) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             wx->tokorgfn, TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(getdbffn(dbi->newrec->dbf), &astnew) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbi->newrec->dbf), TXstrerror(TXgeterror()));
      goto err;
    }
  TXseterror(0);
  if (EPI_STAT(getdbffn(dbi->del->dbf), &astdel) != 0)
    {
      putmsg(MERR + FTE, __FUNCTION__, cantstat,
             getdbffn(dbi->del->dbf), TXstrerror(TXgeterror()));
      goto err;
    }
  if (asttbl.st_mtime != sttbl.st_mtime ||
      asttok.st_mtime != sttok.st_mtime ||
      astnew.st_mtime != stnew.st_mtime ||
      astdel.st_mtime != stdel.st_mtime)
    putmsg(MERR + UGE, CHARPN,
           "Table or index was modified during verify: errors may be bogus");

  goto done;

err:
  ret = 0;
done:
  tblrecids = TXfree(tblrecids);
  closewtix(wx);
  close3dbi(dbi);
  closedbtbl(dbtbl);
  return(ret);
#undef cantstat
}
