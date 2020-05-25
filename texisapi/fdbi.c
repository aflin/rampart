#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef _WIN32
#  include <io.h>
#else
#  include <sys/time.h>
#  include <sys/resource.h>
#endif
#ifdef TEST
#  include <signal.h>
#endif
#ifdef EPI_HAVE_MMAP
#  include <sys/mman.h>
#endif
#include "dbquery.h"
#include "kdbfi.h"                      /* for KDBF_HMAXSIZE */
#include "texint.h"
#include "cgi.h"                        /* for htsnpf() */

/* Full text-inversion index: search/rank code. */

/* ------------------------------- Config ---------------------------------- */
/* FDBI_NO_SPM_WILDONEWORD: getspm() does not (yet) support wildoneword: */
/* KNG 20080417 now implemented, Bug 2126 */
#undef FDBI_NO_SPM_WILDONEWORD

/* FDBI_NO_SPM_WILDSUFMATCH: getspm() does not (yet) support wildsufmatch: */
/* KNG 20080417 now implemented, Bug 2126 */
#undef FDBI_NO_SPM_WILDSUFMATCH

/* FDBI_WARN: warn about some iffy situations: */
#define FDBI_WARN

/* FDBI_MIN_RDBUFSZ: minimum read-buffer size, at least KDBF_MIN_READCHUNK_SZ:
 */
#define FDBI_MIN_RDBUFSZ        \
  (KDBF_MIN_READCHUNK_SZ > 512 ? KDBF_MIN_READCHUNK_SZ : 512)

/* FDBI_MAX_RDBUFSZ: maximum read-buffer size to allow blocks to coalesce to;
 * mainly to avoid a too-large single malloc.  This is when !(indexmmap & 2):
 */
#define FDBI_MAX_RDBUFSZ        ((size_t)10*((size_t)1 << 20))
/* see TXindexmmapbufsz() for mmap() */

/* FDBI_MAX_RDBLKGAP: maximum gap between .dat blocks to consider adjacent.
 * This is for read(), when !(indexmmap & 2); MMAP is for mmap():
 */
#define FDBI_MAX_RDBLKGAP       1024
#define FDBI_MAX_MMAPBLKGAP     ((size_t)32*((size_t)1 << 20))

/* FDBI_EMBED_SELSORT: do in-line selection sort: */
#define FDBI_EMBED_SELSORT

/* FDBI_MAX_SELSORT: maximum array size to sort with selection sort.
 * Sometimes faster than qsort() for small arrays, or slow
 * implementations (e.g. Irix has slow memcpy()):
 */
#ifdef __sgi
#  define FDBI_MAX_SELSORT      12
#else
#  define FDBI_MAX_SELSORT      4
#endif

/* FDBI_NO_MMAP_WRITE: don't use PROT_WRITE in mmap(): */
#undef FDBI_NO_MMAP_WRITE
#ifdef __FreeBSD__
/* FreeBSD 4.0 seems to not save mmap()-written pages unless you call sync()
 * (not msync()); without it the next process might get complete garbage
 * for some pages when it mmap()s the same file.  Since sync() is expensive
 * we use file I/O for direct-token updates which seems to work:  KNG 001016
 */
#  define FDBI_NO_MMAP_WRITE
#endif

/* see also globals below */
/* ------------------------------------------------------------------------- */

#ifndef MAP_FAILED
#  define MAP_FAILED    ((caddr_t)(-1))
#endif

#ifndef RECIDPN
#  define RECIDPN       ((RECID *)NULL)
#endif
#ifndef BTLOCPN
#  define BTLOCPN       ((BTLOC *)NULL)
#endif
#ifndef FFSPN
#  define FFSPN         ((FFS *)NULL)
#endif
#ifndef SPMSPN
#  define SPMSPN        ((SPMS *)NULL)
#endif
#ifndef SELPN
#  define SELPN         ((SEL *)NULL)
#endif

typedef int CDECL (SORTCB) ARGS((CONST void *a, CONST void *b));
#if defined(sun) && !defined(__SVR4)    /* SunOS */
#  define QS_RET        int
#  define QS_RETVAL     0               /* ? */
#else
#  define QS_RET        void
#  define QS_RETVAL
#endif

typedef int     ISNOISE ARGS((char **list, int n, char *wd));

/* FDBIX shared .dat read buffer: */
typedef enum FIBF_tag
{
  FIBF_ISMMAP   = (1 << 0)              /* buffer is mmap() */
}
FIBF;
#define FIBFPN  ((FIBF *)NULL)

struct FDBIXBUF_tag
{
  FDBIXBUF      *prev, *next;           /* previous, next in list */
  int           nusers;                 /* reference count */
  EPI_OFF_T     off;                    /* starting file offset */
  size_t        sz;                     /* size of raw data */
  byte          *data;                  /* non-NULL: `sz' bytes of data */
  FIBF          flags;
};
#define FDBIXBUFPN              ((FDBIXBUF *)NULL)

#define FDBI_MINDATLEN  (2*VSH_MAXLEN + 1)

/* Scanner for suffix forms of a word: */
typedef struct FDBIW
{
  FDBIHI        *hip;           /* hit info ptr (must be first in struct) */
  FDBIHI        hi;             /* hit info (`hip' may point here) */
  FDBIWI        wi;             /* document/word count info (sum of words) */
  int           (*getnext) ARGS((struct FDBIW *fw, RECID loc));
  FDF           flags;
  FDBI          *fi;            /* our root object */
  FDBIX         **fxp;          /* array of `nwords' scanners */
  int           nfxp;           /* # `fxp' is alloced to */
  int           nwords;         /* total # of scanners in `fxp' */
  FDBIX         **fxcur;        /* array of scanners at current recid */
  int           numcur;         /* # of scanners in `fxcur' */
  FHEAP         *fh;            /* heap for merge */
  char          *wd;            /**< copy of word/wildcard for msgs alloced along with struct*/
}
FDBIW;
#define FDBIWPN         ((FDBIW *)NULL)
#define FDBIWPPN        ((FDBIW **)NULL)

/* Scanner for each phrase: */
typedef struct FDBIF
{
  FDBIHI        *hip;           /* hit info ptr (must be first in struct) */
  FDBIHI        hi;             /* hit info (`hip' may point here) */
  FDBIWI        wi;             /* document/word count info for phrase */
  int           (*getnext) ARGS((struct FDBIF *ff, RECID loc));
  FDF           flags;
  FDBI          *fi;            /* our root object */
  FDBIW         **fwp;          /* array of `nwords' scanners */
  int           *reloff;        /* array of `nwords' relative offsets */
  int           phraseLen;      /* (max) length of phrase in index words */
  int           nwords;         /* total # of scanners in `fwp' */
  int           nindexable;     /* total # of indexable words (inc. noise) */
  char          *phrase;        /**< copy of phrase for msgs alloced along with struct */
}
FDBIF;
#define FDBIFPN         ((FDBIF *)NULL)
#define FDBIFPPN        ((FDBIF **)NULL)

/* Scanner for each set: */
struct FDBIS_tag
{
  FDBIHI        *hip;           /* hit info ptr (must be first in struct) */
  FDBIHI        hi;             /* hit info (`hip' may point here) */
  FDBIWI        wi;             /* document/word count info for set */
  int           (*getnext) ARGS((FDBIS *fs, RECID loc));
  FDF           flags;
  FDBI          *fi;            /* our root object */
  FDBIF         **ffp;          /* array of `nwords' scanners */
  int           nffp;           /* # `ffp' is alloced to */
  int           nphrases;       /* total # of scanners in `ffp' */
  int           minPhraseLen;   /* min len (in index words) of `ffp' items */
  int           maxPhraseLen;   /* max len (in index words) of `ffp' items */
  int           maxExtraExprMatches; /* max (maxPhraseLen - nwords) of `ffp'*/
  FDBIF         **ffcur;        /* array of scanners at current recid */
  int           numcur;         /* # of scanners in `ffcur' */
  int           totwords;       /* total words in this set (#FDBIX objects) */
  int           overmaxsetwords;/* nonzero: tried to exceed qmaxsetwords */
  FHEAP         *fh;            /* heap for merge */
  RPPM_SET      *set;           /* value for hi.set */
  FDBIX         *sfx;           /* single FDBIX for fdbis_getnextone_skip() */
  char          *setname;       /**< copy of set for msgs alloced along with struct */
};

/* Main index search object.  These are quadruply linked, in two lists:
 *   o  `prev'/`next'    links different indexes or non-mmap() buffers
 *   o  `mprev'/`mnext'  links same-token-file-mmap() indexes:
 *
 *    <-prev--+---+ <-prev--+---+ <-prev--+---+ <-prev--+---+
 *            | A |         | B |         | C |         | D |
 * FdbiList-> +---+--next-> +---+--next-> +---+--next-> +---+--next->
 *                          |   ^
 *                        mnext |
 *                          V mprev
 *                              |
 *                          +---+
 *                          | B |
 *                          +---+
 *                          |   ^
 *                        mnext |
 *                          V mprev
 *                              |
 *                          +---+
 *                          | B |
 *                          +---+
 *                          |
 *                        mnext
 *                          V
 *
 * This lets us share mmap() buffers for the same token file (save mem),
 * and work around the lack of true MAP_SHARED support in some Linux kernels,
 * detected at compile time but not run time by EPI_MMAP_SHARED_IS_PRIVATE:
 * if we modify a read/write mmap() buffer, other read-only buffers on the
 * same file in the same process may not see the changes.
 */
struct FDBI_tag
{
  FDBI          *prev, *next;   /* list of semi-unique FDBIs */
  BTREE         *bt;            /* handle to word dictionary B-tree */
  KDBF          *datdf;         /* handle to docs/locs data file */
  FDF           flags;
  int           mode;           /* PM_... flags */
  ISNOISE       *isnoise;       /* noise lookup function */
  char          **noise;        /* noise list */
  int           nnoise;         /* length of noise list (if known) */
  EPI_OFF_T     totrecs;        /* total number of records in index */
  A3DBI         *dbi;
  RLEX          *ixrlex;        /* object to search with index expressions */
  byte          *pgbuf;         /* temp buf for btgetnext() */
  size_t        pgbufsz;        /*   its size */
  dword         *posbuf;        /* temp buf for fdbi_getnext() positions */
  size_t        posbufsz;       /*   its size */
  FHEAP         *omfh;          /* temp heap for ormerge() */
  FHEAP         *wordHeap2;
  FHEAP         *fgfh;          /* heap for fdbi_get() */
  EPI_OFF_T     indexcount;     /* indexcount (sort of) after fdbi_get() */
  RPPM          *rp;
  int           totwords;       /* total words in all sets (#FDBIX objects) */
  int           novermaxwords;  /* number of qmaxwords violations */
  int           novermaxsetwords; /* number of sets violating qmaxsetwords */

  size_t        auxfldsz;       /* size of per-row auxfld data */
  size_t        tokelsz;        /* size of tok buf element (RECID + auxfld) */
  int           tokfh;          /* token -> recid file handle (.tok) */
  char          *tokfn;         /* path of token file */
  byte          *tokbuf;        /* buf (tokbufrdsz; may move if shared) */
  EPI_OFF_T     tokbufstart;    /* token value of first recid in buffer */
  EPI_OFF_T     tokbufend;      /* token value of last recid + 1 */
  EPI_OFF_T     tokfilesz;      /* raw size of token file (tokelsz multiple) */
  int           tokbufismmap;   /* nonzero: `tokbuf' is mmap()'d */
  size_t        tokbufrdsz;     /* read buffer size (~FDBI_TOKBUFREAD_SZ) */
  int           tokdev, tokino; /* token file device/inode */
  FDBI          *mprev, *mnext; /* list of shared mmap() FDBIs */

  FDBIXBUF      *fbstart, *fbrecent;
  byte *        (*outvsh) ARGS((byte *d, EPI_HUGEUINT n));
  byte *        (*invsh) ARGS((byte *s, EPI_HUGEUINT *np));
  int           (*vsltolocs) ARGS((byte *bp, size_t sz, dword *array));
  int           (*countvsl) ARGS((byte *bp, size_t sz));

  char          *query;         /* current query (if searching) */
  char          *postmsg;       /* optional reason for need-post-process */
  int           postmsgnotlineardict;  /* `postmsg' known to be !lineardict */
  char          *lineardictmsg; /* optional reason for linear-dict search */
  DBTBL		*dbtbl;		/* table that this index belongs to */
  TXCFF         textsearchmode; /* textsearchmode of index */
};

static CONST char       InvalidVSH[] = "Invalid VSH bits";
static CONST char       InvalidVSH7[] = "Invalid VSH7 bits";

/* FdbiTraceIdx:  WTF overhaul into bit flags:
 *   0 none
 *   1 some non-fatal warnings, one-time index create/update info
 *   2 index new/del list updates (FdbiTraceRecid only if set)
 *   3 word frequencies, allmatch etc. variable settings;open/closefdbi
 *   4 "" + actual KDBF reads/mmap()s
 *   5 "" + kdbf_nextblock()s
 *   6 "" + FDBIX seeks
 *   7 "" + hits
 *   8 "" + set getnexts
 *   9 "" + phrase getnexts
 *  10 "" + word getnexts
 *  11 "" + FDBIX getnexts, fdbix_readnextbuf()s
 *  12 "" + FDBIX decodes
 *  13 "" + set opens attempted
 *  14 "" + phrase opens attempted
 *  15 "" + word opens attempted
 * flags (in separate TXtraceIndexBits for now, until FdbiTraceIdx is bits):
 *   0x00001000  indexscore() values
 *   0x00002000  IINDEX pointers (if verbose)
 *   0x00004000  IINDEX operator/field values (if verbose)
 *   0x00008000  IINDEX row recids (wtf split into index and recid bits)
 *   0x00010000  IINDEX ANDs/ORs, some ORDER BYs, LIKEPs
 *   0x00020000  IINDEX ANDs/ORs, some ORDER BYs, LIKEPs row recids
 *   0x00040000  indexcache use
 *   0x00080000  indexcache predicates
 *   0x00100000  indexcache rows (recid, rank and data)
 *   0x00200000  LIKE[P,R,3,in] B-tree setup stuff (temp for Bug 6796)
 *   0x00400000  LIKE[P,R,3,in] B-tree/keyrec insertion (temp for Bug 6796)
 *   0x00800000  LIKE[P,R,3,in] B-tree/keyrec sort results (temp for Bug 6796)
 */
int             FdbiTraceIdx = 0;                     /* see also setprop.c */
int             TXtraceIndexBits = 0;

/* FdbiTraceRecid, if set:
 *   Rank only that recid in fdbi_get() (usually FdbiTraceIdx set)
 *   If FdbiTraceIdx == 2, print new/del list updates only for this recid
 */
RECID           FdbiTraceRecid = { (EPI_OFF_T)(-1) }; /* see also setprop.c */

/* FbdiDropMode:  0 retain suffixes and most common words up to word limit
 *                1 drop entire term
 */
int             FdbiDropMode = 0;       /* see also setprop.c */

/* FdbiReadBufSz (indexreadbufsz): .dat (and .tok if not memmapping)
 * read buffer size.  During search, actual .dat read block size could
 * be less (if predicted) or more (if blocks merged).  Also used
 * during index create/update.  Does not apply to mmap()-ed buffers.
 * See also tablereadbufsz (used if indexbatchbuild optimization on).
 */
size_t          FdbiReadBufSz = 64*1024;        /* see also setprop.c */

/* indexmmapbufsz: max mmap() buffer size, if mmap()ing .dat.
 * Also applies to orginal token file mmap/read during index update
 * (but may be exceeded if token file is very large).
 * 0 is default: smaller of 25% of physical mem or 50% of mmap limit.
 * see txglob.c, setprop.c, fdbim.c
 */

int             FdbiBonusError = 0;     /* wtf hack for JMT */
static const char       *TXfdbiLinearReason = NULL;     /* wtf hack */

/* TxIndexMmap (indexmmap): bitmask for .tok and .dat mmap() usage;
 * default is 1:
 *   bit 0:  mmap() .tok
 *   bit 1:  mmap() .dat
 */
int             TxIndexMmap = 1;        /* see also setprop.c */

int             TxIndexDump = 0;        /* OR of: 1=new 2=delete 4=token */

static FDBI     *FdbiList = FDBIPN;     /* open FDBIs */

static CONST char       BadSelect[] =
  "Internal error: PM_SELECT action attempted on non-PM_SELECT index %s";

#define VSL_POSTSZ      (VSH7_MAXLEN > 3 ? VSH7_MAXLEN : 3)

/* ------------------------------------------------------------------------- */

int
TXgetTraceIndex(void)
{
  return(FdbiTraceIdx | TXtraceIndexBits);
}

/* ------------------------------------------------------------------------- */

int
TXsetTraceIndex(int traceIndex)
{
  /* Note: may be called in signal context by vortex.c cleanup()? */
  FdbiTraceIdx = (traceIndex & 0xfff);
  TXtraceIndexBits = (traceIndex & ~0xfff);
  return(1);
}

/* ------------------------------------------------------------------------- */

static int fdbi_alloclist ARGS((void **ptr, int *nalloc, int nreq));
static int
fdbi_alloclist(ptr, nalloc, nreq)
void    **ptr;
int     *nalloc;
int     nreq;
{
  static CONST char     fn[] = "fdbi_alloclist";
  size_t                n, sz;
  void                  *p;

  if (*nalloc < nreq)
    {
      n = (size_t)(*nalloc) + ((size_t)(*nalloc) >> 1);
      if (n < 16) n = 16;
      if (n < (size_t)nreq) n = nreq;
      sz = n*sizeof(void *);
      p = (*ptr == NULL ? TXmalloc(TXPMBUFPN, fn, sz) :
           TXrealloc(TXPMBUFPN, fn, *ptr, sz));
      if (p == NULL)
        {
#ifndef EPI_REALLOC_FAIL_SAFE
          *ptr = NULL;                          /* assume realloc() freed */
          *nalloc = 0;
#endif /* !EPI_REALLOC_FAIL_SAFE */
          return(0);
        }
      *ptr = p;
      *nalloc = (int)n;
    }
  return(1);
}

static QS_RET selsort ARGS((void *base, size_t n, size_t sz, SORTCB *cmp));
static QS_RET
selsort(base, n, sz, cmp)
void    *base;
size_t  n, sz;
SORTCB  *cmp;
/* Selection sort replacement for qsort(); hopefully faster for small
 * arrays.  *NOTE* that `cmp' and `sz' are ignored for speed and are
 * hard-coded to use cmphit_allmatch()-style compare if FDBI_EMBED_SELSORT.
 */
{
  char  *cur, *end, *end1, *cm, *b;
  int   cmpVal;
#ifdef FDBI_EMBED_SELSORT
  FDBIHI        *tmp;
  dword         aLen, bLen;

  (void)sz;
  (void)cmp;
#  define sz    (sizeof(FDBIHI *))
#else /* !FDBI_EMBED_SELSORT */
  char  tmp[sizeof(FDBIHI) + 64];
#endif /* !FDBI_EMBED_SELSORT */

  cur = (char *)base;
  end = cur + n*sz;
  for (end1 = end - sz; cur < end1; cur += sz)
    {
      for (b = cur, cm = cur + sz; cm < end; cm += sz)
        {
#ifdef FDBI_EMBED_SELSORT
          cmpVal = ((*(FDBIHI **)cm)->hits[(*(FDBIHI **)cm)->curHit] -
                    (*(FDBIHI **)b)->hits[(*(FDBIHI **)b)->curHit]);
          if (cmpVal < 0)
            b = cm;
          else if (cmpVal == 0)
            {
              aLen = ((*(FDBIHI **)cm)->hitLens  != DWORDPN ?
                      (*(FDBIHI **)cm)->hitLens[(*(FDBIHI**)cm)->curHit] : 1);
              bLen = ((*(FDBIHI **)b)->hitLens  != DWORDPN ?
                      (*(FDBIHI **)b)->hitLens[(*(FDBIHI**)b)->curHit] : 1);
              if (aLen < bLen)
                b = cm;
            }
#else /* !FDBI_EMBED_SELSORT */
          if (cmp(cm, b) < 0) b = cm;
#endif /* !FDBI_EMBED_SELSORT */
        }
      if (b != cur)
        {
#ifdef FDBI_EMBED_SELSORT
          tmp = *((FDBIHI **)cur);
          *((FDBIHI **)cur) = *((FDBIHI **)b);
          *((FDBIHI **)b) = tmp;
#else /* !FDBI_EMBED_SELSORT */
          memcpy(tmp, cur, sz);
          memcpy(cur, b, sz);
          memcpy(b, tmp, sz);
#endif /* !FDBI_EMBED_SELSORT */
        }
    }
  return QS_RETVAL;
#undef sz
}

static int inorder ARGS((char **list));
static int
inorder(list)
char    **list;
/* Returns nonzero number of items in `list' if all in ascending order.
 */
{
  char  *p;
  int   n;

  p = *(list++);
  if (*p == '\0') return(0);                            /* empty list */
  for (n = 1; **list != '\0'; p = *(list++), n++)
    if (strcmpi(p, *list) > 0) return(0);
  return(n);
}

static int isnoise_lin ARGS((char **list, int n, char *wd));
static int
isnoise_lin(list, n, wd)
char    **list;
int     n;
char    *wd;
/* Linear search through `list' for `wd'.
 */
{
  (void)n;
  for ( ; **list != '\0'; list++)
    if (strcmpi(*list, wd) == 0) return(1);
  return(0);
}

static int isnoise_bin ARGS((char **list, int n, char *wd));
static int
isnoise_bin(list, n, wd)
char    **list;
int     n;
char    *wd;
/* Binary search through `list' for `wd'.
 */
{
  int   l, r, i, cmp;

  l = 0;
  r = n;
  while (l < r)
    {
      i = ((l + r) >> 1);
      cmp = strcmpi(wd, list[i]);
      if (cmp < 0) r = i;
      else if (cmp > 0) l = i + 1;
      else return(1);                                   /* found it */
    }
  return(0);
}

static int isnoise_dum ARGS((char **list, int n, char *wd));
static int
isnoise_dum(list, n, wd)
char    **list;
int     n;
char    *wd;
{
  (void)list;
  (void)n;
  (void)wd;
  return(0);
}

/* ------------------------------------------------------------------------- */

int
fdbi_btcmp(ablk, asz, bblk, bsz, usr)
void    *ablk, *bblk;
size_t  asz, bsz;
void    *usr;
/* Item-compare function for btree.  Same as for WTIX, except data
 * is already in btree, and we don't know recid (btree will sort by recid
 * secondarily).
 */
{
  (void)asz;
  (void)bsz;
  (void)usr;
  return(strcmp((char *)ablk, (char *)bblk));
}

static int fdbi_morphemecmp ARGS((CONST char *key, size_t keyByteLen, char *s,
       size_t sByteLen, int prefixproc, int suffixproc, int wildproc,
       MM3S *mme, int minwordlen));
static int
fdbi_morphemecmp(key, keyByteLen, s, sByteLen, prefixproc, suffixproc,
                 wildproc, mme, minwordlen)
CONST char      *key;           /* (in) root word query (pre/suf removed) */
size_t          keyByteLen;     /* (in) byte length of `key' */
char            *s;             /* (in/out) potential match */
size_t          sByteLen;       /* (in) byte length of `s' */
int             prefixproc;     /* (in) nonzero: do prefix processing */
int             suffixproc;     /* (in) nonzero: do suffix processing */
int             wildproc;       /* (in) nonzero: do wildcard processing */
int             minwordlen;     /* (in) minwordlen (chars not bytes) */
MM3S            *mme;           /* (in) MM3S object */
/* Note: string `s' is modified.  wtf: assumes strlen(s) == sByteLen.
 * Returns 0 if word `s' matches root/key `key' according to `suffixproc'/
 * `wildproc'/`prefixproc'/`minwordlen'/`mme'.
 */
{
  int   ret;

  /* Note: as an optimization we check `sByteLen' < `keyByteLen first,
   * and avoid rmsuffix() + memcmp(), since all we care about is equality.
   * Thus our return value is not always tristate-valid for sorting,
   * like strcmp() is:
   */
  if (sByteLen < keyByteLen) return(1);         /* too short to match */

  if (suffixproc)                               /* wtf? modifies `s' */
    rmsuffix(&s, (char **)mme->suffix, mme->nsuf, minwordlen,
             mme->defsuffrm, mme->phrasewordproc, mme->textsearchmode);
  if (prefixproc)
    rmprefix(&s, (char **)mme->prefix, mme->npre, minwordlen,
             mme->textsearchmode);
  if (prefixproc || suffixproc)
    ret = strcmp(key, s);
  else if (wildproc)
    ret = memcmp(key, s, keyByteLen);
  else
    ret = (keyByteLen == sByteLen && memcmp(key, s, keyByteLen) == 0) ? 0 : 1;
  return(ret);
}

static int ormerge_heapcmp ARGS((void *a, void *b, void *usr));
static int
ormerge_heapcmp(a, b, usr)
void    *a, *b, *usr;
/* Heap-compare function for OR-merge.  Compares two FDBIHI pointers.
 * Keeps first/smallest on top of heap.
 * Also used for `w/N' processing in fdbi_get().
 */
{
  dword aw, bw, aLen, bLen;

  (void)usr;
  aw = ((FDBIHI *)a)->hits[((FDBIHI *)a)->curHit];
  bw = ((FDBIHI *)b)->hits[((FDBIHI *)b)->curHit];
  if (aw < bw)
    return(-1);
  else if (aw > bw)
    return(1);
  else
    {
      aLen = (((FDBIHI *)a)->hitLens != DWORDPN ?
              ((FDBIHI *)a)->hitLens[((FDBIHI *)a)->curHit] : 1);
      bLen = (((FDBIHI *)b)->hitLens != DWORDPN ?
              ((FDBIHI *)b)->hitLens[((FDBIHI *)b)->curHit] : 1);
      if (aLen < bLen)
        return(-1);
      else if (aLen > bLen)
        return(1);
      else
        return(0);
    }
}

static int ormerge ARGS((FDBI *fi, FDBIHI *merge, FDBIHI ***list, size_t num));
static int
ormerge(fi, merge, list, num)
FDBI    *fi;
FDBIHI  *merge, ***list;
size_t  num;
/* Merges `num' position lists in `list', setting `*merge' to
 * (alloced) merged list.  Returns 0 on error, 1 if ok.  Note that
 * `curhit' fields in `list' are modified.
 */
{
  static CONST char     fn[] = "ormerge";
  int                   ret;
  FHEAP                 *fh;
  FDBIHI                *hi, ***hip, ***endp;
  size_t                req;

  merge->nhits = 0;
  merge->loc = (*list[0])->loc;                 /* all locs the same */
  if ((fh = fi->omfh) == FHEAPPN &&
      (fi->omfh = fh = openfheap(ormerge_heapcmp, NULL, 0)) == FHEAPPN)
    goto err;
  fheap_clear(fh);
  for (hip = list, endp = list + num; hip < endp; hip++)
    {
      hi = **hip;
      hi->curHit = 0;
      if (!fheap_insert(fh, hi)) goto err;
    }
  while (fheap_num(fh) > 0)                     /* while heap is non-empty */
    {
      hi = (FDBIHI *)fheap_top(fh);
      fheap_deletetop(fh);
      req = (merge->nhits + 1)*sizeof(dword);
      if (merge->hitsz < req &&
          !fdbi_allocbuf(fn, (void**)(char*)&merge->hits, &merge->hitsz, req))
        {
          merge->nhits = 0;
          goto err;
        }
      if (merge->hitLensSz < req &&
          !fdbi_allocbuf(fn, (void **)(char *)&merge->hitLens,
                         &merge->hitLensSz, req))
        {
          merge->nhits = 0;
          goto err;
        }
      merge->hits[merge->nhits] = hi->hits[hi->curHit];
      merge->hitLens[merge->nhits] = (hi->hitLens != DWORDPN ?
                                      /* If no length given, probably not
                                       * a phrase, so assume word length 1:
                                       */
                                      hi->hitLens[hi->curHit] : 1);
      merge->nhits++;
      if (++hi->curHit < hi->nhits)             /* still more: add to heap */
        {
          if (!fheap_insert(fh, hi)) goto err;
        }
    }
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  merge->curHit = 0;
  return(ret);
}

#define RESETFDBIHI(hi)                 \
  (hi)->curHit = (size_t)(-1);          \
  (hi)->nhits = 0;                      \
  (hi)->decodefunc = FDBIHICBPN;        \
  TXsetrecid(&(hi)->loc, (EPI_OFF_T)(-1))

/* --------------------------------- FDBIX --------------------------------- */

static void fdbi_badstuck ARGS((CONST char *fn, FDBIX *fx));
static void
fdbi_badstuck(fn, fx)
CONST char      *fn;
FDBIX           *fx;
/* This is a separate function instead of being directly called in
 * fdbix_getnext() because on an Alpha putting it inline increases
 * CPU usage extraordinarily (crosses page boundary? cache issue?).
 * KNG 991129
 */
{
  putmsg(MERR, fn,
 "Truncated or corrupt data for word `%s' of query `%s' at 0x%wx in index %s",
         fx->wd, (fx->fi != FDBIPN && fx->fi->query != CHARPN ?
                  fx->fi->query : "?"),
         (EPI_HUGEINT)fx->filoff, kdbf_getfn(fx->df));
}

static FDBIXBUF *closefdbixbuf ARGS((FDBIXBUF *fb, FDBI *fi));
static FDBIXBUF *
closefdbixbuf(fb, fi)
FDBIXBUF        *fb;            /* the object */
FDBI            *fi;            /* (optional) associated list pointers */
/* Does actual close of `fb'.  Use macro to check nusers first.
 */
{
  if (fb->prev != FDBIXBUFPN) fb->prev->next = fb->next;
  else if (fi != FDBIPN) fi->fbstart = fi->fbstart->next;
  if (fb->next != FDBIXBUFPN) fb->next->prev = fb->prev;
  if (fi != FDBIPN && fi->fbrecent == fb) fi->fbrecent = FDBIXBUFPN;
  if (fb->data != BYTEPN)
    {
#ifdef EPI_HAVE_MMAP
      if (fb->flags & FIBF_ISMMAP)
	{
          munmap((caddr_t)fb->data, fb->sz);
          if (FdbiTraceIdx >= 4)
            putmsg(MINFO, CHARPN, "     munmap(%s, %p, 0x%wx)",
                   (fi ? kdbf_getfn(fi->datdf) : "?"), (void *)fb->data,
                   (EPI_HUGEUINT)fb->sz);
	}
      else
#endif /* EPI_HAVE_MMAP */
      fb->data = TXfree(fb->data);
    }
  fb = TXfree(fb);
  return(FDBIXBUFPN);
}

#define CLOSEFDBIXBUF(fb, fi)   \
  (fb != FDBIXBUFPN && --fb->nusers <= 0 ? closefdbixbuf(fb, fi) : FDBIXBUFPN)

static int fdbixbuf_attach ARGS((FDBIXBUF *fb, EPI_OFF_T off, size_t sz));
static int
fdbixbuf_attach(fb, off, sz)
FDBIXBUF        *fb;
EPI_OFF_T       off;
size_t          sz;
/* Returns 1 if `off'/`sz' successfully attached to `fb', 0 if not.
 */
{
  EPI_OFF_T     reqend, fbend;
  size_t        nsz;
#ifdef EPI_HAVE_MMAP
  size_t        mxgap, mxbuf;

  if (fb->flags & FIBF_ISMMAP)                  /* using mmap() and */
    {
      mxgap = FDBI_MAX_MMAPBLKGAP;
      mxbuf = TXgetindexmmapbufsz();
    }
  else
    {
      mxgap = FDBI_MAX_RDBLKGAP;
      mxbuf = FDBI_MAX_RDBUFSZ;
    }
#else /* !EPI_HAVE_MMAP */
#  define mxgap FDBI_MAX_RDBLKGAP
#  define mxbuf FDBI_MAX_RDBUFSZ
#endif /* !EPI_HAVE_MMAP */

  reqend = off + (EPI_OFF_T)sz;
  fbend = fb->off + (EPI_OFF_T)fb->sz;
  if (off >= fb->off)
    {
      if (reqend <= fbend) goto gotall;         /* entirely fits into `fb' */
      if (fb->data == BYTEPN &&                 /* expandable/unread block */
          off <= fbend + (EPI_OFF_T)mxgap)
        {
          nsz = (size_t)(reqend - fb->off);
          if (nsz <= mxbuf) goto got1;
        }
    }
  else                                          /* request starts before buf*/
    {
      if (fb->data == BYTEPN &&                 /* expandable/unread block */
          reqend >= fb->off - (EPI_OFF_T)mxgap)
        {
          nsz = (reqend > fbend ? sz : (size_t)(fbend - off));
          if (nsz <= mxbuf)                     /* within max expand size */
            {
              fb->off = off;
            got1:
              fb->sz = nsz;
            gotall:
              fb->nusers++;
              return(1);
            }
        }
    }
  return(0);
#undef mxgap
#undef mxbuf
}

static int fdbix_getbuf ARGS((FDBIX *fx));
static int
fdbix_getbuf(fx)
FDBIX   *fx;
/* Get an FDBIXBUF buffer for `fx'.  Uses buffers shared with other
 * FDBIX objects, trying to merge our requested block with other read
 * or pending blocks.  By delaying the read as long as possible (until
 * fdbix_getnext) we can let adjacent blocks (e.g. common-root
 * wildcards) coalesce into larger blocks, reducing read calls.
 * Returns 0 on error.  KNG 000314
 */
{
  static CONST char     fn[] = "fdbix_getbuf";
  FDBI                  *fi;
  FDBIXBUF              *fb;

  if ((fi = fx->fi) != FDBIPN)
    {
      if ((fb = fi->fbrecent) != FDBIXBUFPN &&  /* optimize for wildcards */
          fdbixbuf_attach(fb, fx->filoff, fx->bufsz))
        goto gotbuf;
      for (fb = fi->fbstart; fb != FDBIXBUFPN; fb = fb->next)
        {                                       /* check all bufs */
          if (fb == fi->fbrecent) continue;
          if (fdbixbuf_attach(fb, fx->filoff, fx->bufsz)) goto gotbuf;
        }
    }
  else if ((fb = fx->fb) != FDBIXBUFPN &&       /* re-use existing buffer? */
           fdbixbuf_attach(fb, fx->filoff, fx->bufsz))
    goto gotbuf;

  /* Cannot attach/expand an existing buffer.  Create a new one: */
  if (!(fb = (FDBIXBUF *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBIXBUF))))
    goto err;
#ifdef EPI_HAVE_MMAP
  if (TxIndexMmap & 2) fb->flags |= FIBF_ISMMAP;
#endif /* EPI_HAVE_MMAP */
  fb->nusers = 1;
  fb->off = fx->filoff;
  fb->sz = fx->bufsz;
  if (fb->sz < FDBI_MIN_RDBUFSZ) fb->sz = FDBI_MIN_RDBUFSZ;
  if (fi != FDBIPN)                             /* let others use it too */
    {
      fb->next = fi->fbstart;
      if (fi->fbstart != FDBIXBUFPN) fi->fbstart->prev = fb;
      fi->fbstart = fb;
    }

gotbuf:
  if (fi != FDBIPN) fi->fbrecent = fb;
  CLOSEFDBIXBUF(fx->fb, fi);
  fx->fb = fb;
  return(1);
err:
  return(0);
}

static int fdbix_readbuf ARGS((FDBIX *fx));
static int
fdbix_readbuf(fx)
FDBIX   *fx;
/* Actual read of buffer, if not read by another user.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "fdbix_readbuf";
  FDBIXBUF              *fb;
  EPI_OFF_T             at, fbend, dat, a;
  byte                  *buf, *obuf;
  size_t                bsz, b;
  int                   xtra = 0;

  fb = fx->fb;
  if (fb->data == BYTEPN)                       /* no one read the buf yet */
    {
#ifdef EPI_HAVE_MMAP
      if (fb->flags & FIBF_ISMMAP)              /* try mmap() */
	{
          /* The offset for mmap() must be a multiple of the page size.
           * Might as well align the size too; we also add in some INVSH
	   * overflow slack space that is mapped but subtracted from size:
           */
          if ((dat = (EPI_OFF_T)TXpagesize()) <= (EPI_OFF_T)0)
            dat = ((EPI_OFF_T)1 << 14);
          at = (fb->off % dat);
          fb->off -= at;
          fb->sz = ((fb->sz + (size_t)at + FDBI_MINDATLEN + VSL_POSTSZ +
		     (size_t)dat - 1)/dat)*dat;
          errno = 0;
          fb->data = (byte *)mmap(NULL, fb->sz, PROT_READ, MAP_SHARED,
                                  kdbf_getfh(fx->df), fb->off);
          if (FdbiTraceIdx >= 4)
	    {
              int       sav = errno;
              putmsg(MINFO, CHARPN, "     mmap(%s, 0x%wx, 0x%wx) = %p",
                     kdbf_getfn(fx->df), (EPI_HUGEINT)fb->off,
                     (EPI_HUGEUINT)fb->sz, (void *)fb->data);
              errno = sav;
	    }
          if (fb->data == (byte *)(-1)) goto usefileio;
          fb->sz -= FDBI_MINDATLEN + VSL_POSTSZ;  /* INVSH slack space */
          /* wtf update fb->sz if mmap()'d past EOF? */
        }
      else
#endif /* EPI_HAVE_MMAP */
        {
#ifdef EPI_HAVE_MMAP
        usefileio:
          if (FdbiTraceIdx >= 1 && (fb->flags & FIBF_ISMMAP))
            putmsg(MWARN, fn,
             "Cannot mmap() Metamorph index data file %s: %s; using file I/O",
		   kdbf_getfn(fx->df), strerror(errno));
#endif /* EPI_HAVE_MMAP */
	  errno = 0;
	  /* fdbix_getnext() might expect extra slack space for INVSH (?),
	   * so alloc extra FDBI_MINDATLEN but don't read it:
	   */
	  if (!(fb->data = (byte*)TXmalloc(TXPMBUFPN, fn, fb->sz +
                                           FDBI_MINDATLEN + VSL_POSTSZ)))
            goto err;

          /* Valgrind/Purify: Avoid uninitialized memory read errors,
           * e.g. by INVSH in fdbiaux.c.  Such reads should be safe
           * due to extra buffer alloced here and checks after INVSH,
           * but if not, at least make the errors consistent:
           */
          memset(fb->data + fb->sz, 0, FDBI_MINDATLEN + VSL_POSTSZ);

	  bsz = kdbf_readchunk(fx->df, fb->off, fb->data, fb->sz);
	  if (FdbiTraceIdx >= 4)
	    putmsg(MINFO, CHARPN, "     kdbf_readchunk(0x%wx, 0x%X) = 0x%X",
		   (EPI_HUGEINT)fb->off, (int)fb->sz, (int)bsz);
	  fb->sz = bsz;
	  if (fb->sz == (size_t)(-1))           /* read error */
	    {
	      fb->sz = 0;
	      goto err;
	    }
        }
    }

  /* fx's request was originally entirely contained within `fb'.
   * But the actual read of data into `fb' (by `fx' or another `fb' user)
   * might have truncated it (or expanded it if merged with adjacent
   * reads), so update fx's buffer and size:
   */
  at = fx->filoff;
  fbend = fb->off + (EPI_OFF_T)fb->sz;
  if (at >= fbend) goto trunc;
  fx->buf = buf = obuf = fb->data + (size_t)(at - fb->off);
  bsz = (size_t)(fbend - at);
  if (fx->totsz == 0)                           /* first read since seek */
    {
      char      tmp[128], *d, *e;
      a = at;
      b = bsz;
      fx->bufsz = kdbf_nextblock(fx->df, &at, &buf, &bsz, &fx->buf, &dat,
                                 &fx->totsz);
      if (FdbiTraceIdx >= 5)
	{
          d = tmp;
          e = tmp + sizeof(tmp);
          *d = '\0';
          if (fx->totsz != fx->bufsz && d < e)
            d += htsnpf(d, e - d, " (0x%x total data)", (int)fx->totsz);
          if (dat != a && d < e)
            d += htsnpf(d, e - d, " (at 0x%wx)", (EPI_HUGEINT)dat);
          putmsg(MINFO, CHARPN,"      kdbf_nextblock(0x%wx, 0x%wx) = 0x%wx%s",
                 (EPI_HUGEINT)a, (EPI_HUGEUINT)b, (EPI_HUGEUINT)fx->bufsz, tmp);
	}
      if (fx->bufsz == (size_t)(-1)) goto err;
      /* wtf bufsz == 0 could just be benign end-of-buffer; force a new
       * read here?  caller should've checked for this (e.g. fdbix_seek())
       * but may not be fail-safe:   KNG 000817
       */
      if (fx->bufsz == 0 || dat != fx->filoff)
        {
          xtra = 1;
          htsnpf(tmp, sizeof(tmp), " (kdbf_nextblock size 0x%wx at 0x%wx)",
                 (EPI_HUGEUINT)fx->bufsz, (EPI_HUGEINT)dat);
        trunc:
          putmsg(MERR + FRE, fn, "Truncated/empty data block at 0x%wx in %s for `%s' in buffer at 0x%wx size 0x%wx%s",
                 (EPI_HUGEINT)fx->filoff, kdbf_getfn(fx->df), fx->wd,
	         (EPI_HUGEINT)fb->off, (EPI_HUGEUINT)fb->sz, (xtra ? tmp : ""));
          goto err;
        }
      if (fx->fi != FDBIPN && fx->fi->totrecs > (EPI_OFF_T)0)
        fx->lkfactor = (float)fx->totsz/(float)fx->fi->totrecs;
      else
        fx->lkfactor = 0.0;
    }
  else
    {
      fx->bufsz = bsz;
      if (fx->bufsz > fx->totsz - fx->totrd)    /* stay within KDBF block */
        fx->bufsz = fx->totsz - fx->totrd;
    }
  fx->filoff += (EPI_OFF_T)((fx->buf + fx->bufsz) - obuf);
  fx->totrd += fx->bufsz;
  return(1);

err:
  return(0);
}

static FDBIHI *fdbix_getnexteof ARGS((FDBIX *fx, RECID loc));
static FDBIHI *
fdbix_getnexteof(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Gets next recid >= `loc', for at-EOF word.
 */
{
  static CONST char     fn[] = "fdbix_getnexteof";

  fx->flags &= ~FDF_ERROR;
  RESETFDBIHI(&fx->hi);
  if (FdbiTraceIdx >= 11)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc));
  return(FDBIHIPN);
}

static int fdbix_readnextbuf ARGS((FDBIX *fx, size_t minsz));
static int
fdbix_readnextbuf(fx, minsz)
FDBIX   *fx;
size_t  minsz;
/* Reads 2nd and later chunks of data, at least `minsz' bytes.
 * Returns -1 on error, 0 on EOF.
 */
{
  size_t        n;
  int           ret, chk = 0;

  fx->filoff -= (EPI_OFF_T)fx->bufsz;           /* amount not used */
  fx->totrd -= fx->bufsz;
  /* We know the full block size now, so we could read the entire rest
   * of the block in.  But that might eat a lot of mem, so read at
   * most FdbiReadBufSz.  See also openfdbix():
   * KNG 000602 except if mmap(); then use higher limit:
   */
#ifdef EPI_HAVE_MMAP
  if (TxIndexMmap & 2) n = TXgetindexmmapbufsz();
  else
#endif /* EPI_HAVE_MMAP */
    n = FdbiReadBufSz;
  if (n < minsz) n = minsz;
  if (n > fx->totsz - fx->totrd)                /* stay within KDBF block */
    n = fx->totsz - fx->totrd;
  if (n == 0) goto eof;                         /* end of all data */
  fx->bufsz = n;

  /* During index create/update, we're going to read the whole .dat file
   * eventually.  So if we're at the end of the FDBIXBUF and need to
   * read another, don't just read the block remainder, schlep a whole
   * bufferful in:
   */
  if (fx->fi == FDBIPN &&                       /* create/update, not search*/
      (
#ifdef EPI_HAVE_MMAP
       (TxIndexMmap & 2) ? fx->bufsz < TXgetindexmmapbufsz() :
#endif /* EPI_HAVE_MMAP */
       fx->bufsz < FdbiReadBufSz) &&
      fx->filoff + (EPI_OFF_T)fx->bufsz > fx->fb->off + (EPI_OFF_T)fx->fb->sz)
    {
#ifdef EPI_HAVE_MMAP
      if (TxIndexMmap & 2) fx->bufsz = TXgetindexmmapbufsz();
      else
#endif /* EPI_HAVE_MMAP */
        fx->bufsz = FdbiReadBufSz;
      chk = 1;
    }

  if (!fdbix_getbuf(fx)) goto err;
  if (chk)
    {
      if (fx->bufsz > fx->totsz - fx->totrd)    /* stay within KDBF block */
        fx->bufsz = fx->totsz - fx->totrd;
      if (fx->bufsz == 0) goto eof;             /* end of all data */
    }
  if (!fdbix_readbuf(fx)) goto err;
  if (fx->bufsz == 0) goto eof;                 /* end of all data */
  ret = 1;
  goto done;

err:
  fx->bufsz = 0;
  ret = -1;
  goto done;
eof:
  fx->getnext = fdbix_getnexteof;
  fx->bufsz = 0;
  ret = 0;
done:
  return(ret);
}

static int fdbiw_decodemerge ARGS((FDBIW *fw));
static int fdbis_decodemerge ARGS((FDBIS *fs));

#undef FUNC
#define FUNC(a)	a
#undef FDBI_TRACE
#include "fdbiaux.c"
#undef FUNC
#define FUNC(a)	a##_trace
#define FDBI_TRACE
#include "fdbiaux.c"
#undef FUNC
#undef FDBI_TRACE

static FDBIHI *(* CONST FdbixGetnext[]) ARGS((FDBIX *, RECID)) =
{                                       /* order is important! */
  fdbix_getnextsingle,
  fdbix_getnext7single,
  fdbix_getnextmultifirst,
  fdbix_getnext7multifirst,
  fdbix_getnextsingle_trace,
  fdbix_getnext7single_trace,
  fdbix_getnextmultifirst_trace,
  fdbix_getnext7multifirst_trace,
};

FDBIX *
closefdbix(fx)
FDBIX   *fx;
{
  if (fx == FDBIXPN) goto done;
  CLOSEFDBIXBUF(fx->fb, fx->fi);
  fx->hi.hits = TXfree(fx->hi.hits);
  fx->hi.hitLens = TXfree(fx->hi.hitLens);
  fx = TXfree(fx);

done:
  return(FDBIXPN);
}

static size_t fdbix_bufest ARGS((FDBIWI *wi, FDF flags));
static size_t
fdbix_bufest(wi, flags)
FDBIWI  *wi;
FDF     flags;
/* Given `*wi' and `flags', computes likely upper limit to KDBF buffer
 * size for data.  Generally guesses 2x actual amount.
 */
{
  size_t        sz;

  if (flags & FDF_FULL)
    sz = 4*(size_t)wi->ndocs + 4*(size_t)wi->nlocs + 4;
  else
    sz = 4*(size_t)wi->ndocs;
  if (sz < FDBI_MINDATLEN) sz = FDBI_MINDATLEN;
  return(sz);
}

FDBIX *
openfdbix(df, flags, wi, loc, blksz, hits, nhits, wd, fi)
KDBF    *df;
FDF     flags;
FDBIWI  *wi;                    /* req'd for search, NULL if index update */
RECID   loc;                    /* != -1: single-recid word */
size_t  blksz;                  /* != -1: known size of .dat payload data */
dword   *hits;                  /* non-NULL: malloc'd hit data at loc */
size_t  nhits;                  /* # of hits in `hits' */
char    *wd;                    /* (optional) word, for msgs */
FDBI    *fi;                    /* (optional) FDBI object, for msgs */
/* Opens single raw word scanner (i.e. no suffix processing, etc.).
 * Use fx->getnext() to get next item; note that it might change from
 * call to call, or after an fdbix_seek().
 */
{
  static CONST char     fn[] = "openfdbix";
  FDBIX                 *fx;
  size_t                sz;
  int                   idx;

  if (fi != FDBIPN && (int)fi->dbi->version >= 3) flags |= FDF_VSH7;
  idx = ((flags & FDF_VSH7) ? 1 : 0);
  if (wd == CHARPN) wd = "";
  fx = (FDBIX *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBIX) +
                         strlen(wd) + 1);
  if (fx == FDBIXPN)
    goto err;
  if (wi != FDBIWIPN) fx->wi = *wi;
  fx->flags = ((FDF)flags & (FDF_PUBMASK | FDF_VSH7));
  fx->fi = fi;
  TXsetrecid(&fx->hi.loc, (EPI_OFF_T)(-1));
  fx->hip = &fx->hi;
  fx->df = df;
  fx->lkfactor = 0.0;
  fx->wd = (char *)fx + sizeof(FDBIX);
  strcpy(fx->wd, wd);

  if (TXrecidvalid2(&loc))                      /* single-recid word */
    {
      fx->df = KDBFPN;                          /* flag: we don't use .dat */
      fx->curtbloff = loc;                      /* tmp storage */
      if (nhits > 0)
        {
          fx->hi.hits = hits;
          fx->hi.hitsz = sizeof(dword)*nhits;
          fx->buf = (byte *)nhits;              /* tmp storage */
        }
    }
  else
    {
      /* Guess at buffer size we'll need, based on `*wi'.  Too large and
       * we waste I/O and mem; too small and we incur unneeded syscalls.
       * KNG 990816
       * KNG 000111 Upper limit is FdbiReadBufSz.
       * KNG 000602 unless mmap(): higher limit.
       */
      if (blksz != (size_t)(-1))                /* exactly known block size */
        sz = blksz + KDBF_PREBUFSZ_WANT;        /* i.e. plus header size */
      else if (wi != FDBIWIPN)                  /* guesstimate */
        sz = fdbix_bufest(wi, flags);
      else                                      /* index update */
#ifdef EPI_HAVE_MMAP
        if (TxIndexMmap & 2) sz = TXgetindexmmapbufsz();
      else
#endif /* EPI_HAVE_MMAP */
        sz = FdbiReadBufSz;
#ifdef EPI_HAVE_MMAP
      if (TxIndexMmap & 2)
        {
          if (sz > TXgetindexmmapbufsz()) sz = TXgetindexmmapbufsz();
	}
      else
#endif /* EPI_HAVE_MMAP */
        if (sz > FdbiReadBufSz) sz = FdbiReadBufSz;

      fx->bufsz = sz;
      if (fx->flags & FDF_FULL)                 /* constant; set now */
        {
          if (flags & FDF_VSH7)
            fx->hi.decodefunc = (FDBIHICB *)(FdbiTraceIdx >= 12 ?
                                       fdbix_decode7_trace : fdbix_decode7);
          else
            fx->hi.decodefunc = (FDBIHICB *)(FdbiTraceIdx >= 12 ?
                                       fdbix_decode_trace : fdbix_decode);
          fx->hi.decodeusr = fx;
        }
      idx |= 2;
    }
  if (FdbiTraceIdx >= 12) idx |= 4;
  fx->getnext = FdbixGetnext[idx];
  goto done;

err:
  fx = closefdbix(fx);
done:
  return(fx);
}

int
fdbix_seek(fx, off)
FDBIX   *fx;
RECID   off;
/* "Seeks" to start of data for word whose offset is `off'.  Returns 0
 * on error.  Must be called first, before multiple fx->getnext()
 * calls.  Data is not actually read until fx->getnext().  Sets fx->getnext,
 * which may change when called.  If `off' is -1, assumed to be single-recid
 * word, and just sets fx->getnext correctly.
 */
{
  if (FdbiTraceIdx >= 6)
    putmsg(MINFO, CHARPN, "    fdbix_seek(%s, 0x%wx)",
           fx->wd, (EPI_HUGEINT)TXgetoff2(&off));
  TXsetrecid(&fx->curtbloff, (EPI_OFF_T)0);     /* clear garbage/tmp calls */
  fx->filoff = TXgetoff2(&off);
  fx->totsz = fx->totrd = 0;                    /* no fdbix_readbuf() yet */
  fx->lkfactor = 0.0;

  /* `bufsz' is only valid for the first seek; during index update
   * we're doing multiple seeks, and it'll be 0 later.  Make sure we
   * have a sane buffer size: when `bufsz' gets too low, bump it back
   * up to cause a new full-size read.  (But don't bump it up every
   * time or we'll keep re-reading most of the current FDBIXBUF; nor
   * for searches where it's a valid prediction.):
   */
  if (TXrecidvalid2(&off))
    {
      if (fx->fi == FDBIPN &&                   /* index update, not search */
          fx->fb != FDBIXBUFPN &&               /* secondary seek */
          (fx->fb->off+(EPI_OFF_T)fx->fb->sz)-fx->filoff <
          (EPI_OFF_T)(KDBF_HMAXSIZE + FDBI_MINDATLEN))  /* min useful size */
        fx->bufsz = FdbiReadBufSz;
      else if (fx->bufsz < FDBI_MINDATLEN)
        fx->bufsz = FDBI_MINDATLEN;
      if (!fdbix_getbuf(fx)) goto err;
      fx->getnext = FdbixGetnext[(((fx->flags & FDF_VSH7) ? 1 : 0) +
                                 2 +
                                 (FdbiTraceIdx ? 4 : 0))];
    }
  else
    /* WTF fdbix_seek() needs to take the single-recid stuff from openfdbix(),
     * for symmetry in wtix_getnextorg().  Barring that, we set the eof
     * function here because wtix_getnextorg() already did the getnext
     * for single-recid:   KNG 0004013
     */
    fx->getnext = fdbix_getnexteof;
  return(1);
err:
  fx->getnext = fdbix_getnexteof;
  return(0);
}

int
fdbix_iserror(fx)
FDBIX   *fx;
{
  return((fx->flags & FDF_ERROR) ? 1 : 0);
}

size_t
fdbix_slurp(fx, bufp)
FDBIX   *fx;
byte    **bufp;
/* Gets next chunk of raw .dat data for current word of `fx'.  Sets `*bufp'
 * to buffer, returns buffer size or 0 on EOF (end of data for this word)
 * or error.  Callable whenever fx->getnext() valid, e.g. after fdbix_seek().
 * NOTE: assumes 1st getnext (or equivalent: wtix_getnextorg) already called.
 * NOTE: data returned may not end on an atomic token/VSL boundary.
 * NOTE: once this called, fx->getnext() should not be called until next seek.
 * NOTE: it is up to caller to know if 1st VSH is absolute or not, and
 * to get single-recid data from B-tree.
 */
{
  static CONST char     fn[] = "fdbix_slurp";
  int                   stuck = 0, res;
  EPI_OFF_T             pfiloff;
  size_t                sz;

  fx->flags &= ~FDF_ERROR;
nextrecid:
  if ((pfiloff = fx->filoff) < (EPI_OFF_T)0)    /* single-recid word */
    goto eof;
  if (fx->bufsz > 0)                            /* have some data in buf */
    {
      *bufp = fx->buf;
      sz = fx->bufsz;
      fx->buf += sz;                            /* done with this buffer */
      fx->bufsz = 0;                            /* "" */
      return(sz);
    }
  if (stuck >= 64)                              /* don't loop infinitely */
    {
      fdbi_badstuck(fn, fx);
      goto err;
    }
  if ((res = fdbix_readnextbuf(fx, 0)) <= 0)
    {
      if (res == 0) goto eof;                   /* end of all data for word */
      goto err;                                 /* read error */
    }
  if (fx->filoff <= pfiloff) stuck++;           /* no forward progress */
  goto nextrecid;

err:
  fx->flags |= FDF_ERROR;
eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
  *bufp = BYTEPN;
  return((size_t)0);
}

/* --------------------------------- FDBIW --------------------------------- */

static int
fdbiw_decodemerge(fw)
FDBIW   *fw;
/* Delayed decode and merge function for multi-word-occurence FDBIW.
 */
{
  int           i;
  FDBIHI        *h;

  for (i = 0; i < fw->numcur; i++)              /* decode for ormerge() */
    {
      h = fw->fxcur[i]->hip;
      if (h->decodefunc != FDBIHICBPN &&
          !h->decodefunc(h->decodeusr))
        goto err;
    }
  if (!ormerge(fw->fi, &fw->hi, (FDBIHI ***)fw->fxcur, fw->numcur))
    goto err;
  return(1);
err:
  return(0);
}

static FDBIW *closefdbiw ARGS((FDBIW *fw));
static FDBIW *
closefdbiw(fw)
FDBIW   *fw;
{
  int   i;

  if (fw == FDBIWPN) goto done;
  if (fw->fxp != FDBIXPPN)
    {
      for (i = 0; i < fw->nwords; i++)
        closefdbix(fw->fxp[i]);
      fw->fxp = TXfree(fw->fxp);
    }
  fw->fxcur = TXfree(fw->fxcur);
  fw->hi.hits = TXfree(fw->hi.hits);
  fw->hi.hitLens = TXfree(fw->hi.hitLens);
  if (fw->fh != FHEAPPN)
    {
      if (fw->flags & FDF_FREEHEAP)
        for (i = 0; (size_t)i < fheap_num(fw->fh); i++)
          closefdbix((FDBIX *)fheap_elem(fw->fh, i));
      closefheap(fw->fh);
    }
  fw = TXfree(fw);

done:
  return(FDBIWPN);
}

static int fdbiw_heapcmp_getnext ARGS((void *a, void *b, void *usr));
static int
fdbiw_heapcmp_getnext(a, b, usr)
void    *a, *b, *usr;
/* Heap comparison function for fdbiw_getnextmulti().
 */
{
  (void)usr;
  return(TXrecidcmp(&((FDBIX *)a)->hi.loc, &((FDBIX *)b)->hi.loc));
}

static int fdbiw_heapcmp_open ARGS((FDBIX *a, FDBIX *b, void *usr));
static int
fdbiw_heapcmp_open(a, b, usr)
FDBIX   *a, *b;
void    *usr;
/* Heap comparison function for openfdbiw(), when dropping words from
 * a too-large set.  Worst words sort first, to be potentially tossed.
 */
{
  int   rc;

  (void)usr;
  /* First priority is the root word: */
  rc = ((int)(a->flags & FDF_ROOTWORD) - (int)(b->flags & FDF_ROOTWORD));
  if (rc) return(rc);
  /* Next we prefer short words (after suffix stripping): */
  rc = ((int)b->wdrank - (int)a->wdrank);
  if (rc) return(rc);
  /* Next we prefer words that match the most documents, since they're
   * more likely to be desired: 50% of wildcard words are single-match
   * typos that no one searches for:
   */
#if EPI_HUGEUINT_BITS == EPI_OS_INT_BITS
  rc = ((int)a->wi.ndocs - (int)b->wi.ndocs);
#else /* EPI_HUGEUINT_BITS != EPI_OS_INT_BITS */
  if (a->wi.ndocs > b->wi.ndocs) rc = 1;
  else if (a->wi.ndocs < b->wi.ndocs) rc = -1;
  else rc = 0;
#endif /* EPI_HUGEUINT_BITS != EPI_OS_INT_BITS */
  if (rc) return(rc);
#if EPI_HUGEUINT_BITS == EPI_OS_INT_BITS
  return((int)a->wi.nlocs - (int)b->wi.nlocs);
#else /* EPI_HUGEUINT_BITS != EPI_OS_INT_BITS */
  if (a->wi.nlocs > b->wi.nlocs) return(1);
  if (a->wi.nlocs < b->wi.nlocs) return(-1);
  return(0);
#endif /* EPI_HUGEUINT_BITS != EPI_OS_INT_BITS */
}

static int TXfdbiwInitOver ARGS((FDBIW *fw, FDBIS *fs, int set, int *wlim));
static int
TXfdbiwInitOver(fw, fs, set, wlim)
FDBIW   *fw;
FDBIS   *fs;
int     set;            /* nonzero: set limit, not overall query limit */
int     *wlim;          /* word limit to be set */
/* Called when qmaxsetwords/qmaxwords initially exceeded in openfdbiw().
 * Reports error and sets up heap.  Returns 1 to continue, 0 to bail this
 * term, -1 to bail the query (denymode error), or -2 on internal error.
 */
{
  static CONST char     overset[] =
    "%s term `%s' in query `%s': Max words per set exceeded";
  static CONST char     overquery[] =
    "%s term `%s' in query `%s': Max words per query exceeded";
  CONST char            *msg = CHARPN;
  char                  *arg1, *s;
  FDBI                  *fi = fw->fi;
  int                   n;
  FDBIX                 *fx;
  MM3S                  *mm3s;                  /* settings for this set */
  char                  sbuf[256];

  mm3s = fs->set->sel->mm3s;                    /* Bug 3908 */
  *wlim = fw->nwords;
  arg1 = (FdbiDropMode == 1 || *wlim==0 ? "Dropping" : "Partially dropping");
  if (set)                                      /* set word limit exceeded */
    {
      if (fi->novermaxsetwords < 3 && !fs->overmaxsetwords) msg = overset;
      if (!fs->overmaxsetwords) fi->novermaxsetwords += 1;
      fs->overmaxsetwords = 1;
    }
  else                                          /* query word limit exceeded*/
    {
      if (fi->novermaxwords < 3 && !fs->overmaxsetwords) msg = overquery;
      fi->novermaxwords += 1;
    }
  n = MERR + UGE;
  switch (mm3s->denymode)
    {
    case API3DENYERROR:    arg1 = "Search failed at";   break;
    case API3DENYWARNING:  n = MWARN + UGE;             break;
    case API3DENYSILENT:   msg = CHARPN;                break;
    }
  if (msg != CHARPN)
    putmsg(n, CHARPN, msg, arg1, fw->wd, mm3s->query);
  if (mm3s->denymode == API3DENYERROR) return(-1);
  /* If we cannot do all words for this wildcard/equiv set,
   * then we must blow off the whole wildcard, because the word
   * they really want might not be seen yet, sez Bart:  KNG 990816
   * KNG 000106 Bart now sez that's stoopid, keep as many of the
   * "best" words as we can (FdbiDropMode 0), via fdbiw_heapcmp_open().
   */
  if (FdbiDropMode == 1 || *wlim == 0) return(0);

  /* Create a heap for tracking the best words: */
  if ((fw->fh = openfheap((FHCMP *)fdbiw_heapcmp_open, NULL, 0)) == FHEAPPN ||
      !fheap_alloc(fw->fh, *wlim))
    goto err;
  fw->flags |= FDF_FREEHEAP;                    /* heap has to-be-freed data*/
  for ( ; fw->nwords > 0; fw->nwords -= 1)      /* move words to heap */
    {
      fx = fw->fxp[fw->nwords - 1];
      TXstrncpy(sbuf, fx->wd, sizeof(sbuf));    /* WTF fixed size tmp buf */
      n = strlen(fw->wd);
      s = sbuf;
      rmsuffix(&s, (char **)mm3s->suffix, mm3s->nsuf, n, 0,
               mm3s->phrasewordproc, mm3s->textsearchmode);
      /* wtf rmprefix()?  see openfdbiw() too */
      fx->wdrank = strlen(s);
      if (!fheap_insert(fw->fh, fx)) goto err;
    }
  return(1);

err:
  return(-2);
}

static FDBIW *openfdbiw ARGS((FDBI *fi, FDBIS *fs, char *wd,
   size_t wdByteLen, int prefixproc, int suffixproc, int wildproc,
   int minwordlen, int *overwords, int rel, int mmDictFlags));
static FDBIW *
openfdbiw(fi, fs, wd, wdByteLen, prefixproc, suffixproc, wildproc,
          minwordlen, overwords, rel, mmDictFlags)
FDBI    *fi;            /* root FDBI object */
FDBIS   *fs;            /* current set */
char    *wd;            /* the word or prefix */
size_t  wdByteLen;      /* its byte length */
int     prefixproc;     /* (in) nonzero: do prefix processing */
int     suffixproc;     /* (in) nonzero: do suffix processing */
int     wildproc;       /* (in) nonzero: do wildcard processing */
int     minwordlen;     /* (in) minwordlen (chars not bytes) for processing */
int     *overwords;     /* 0: ok  1: too many words  2: "" and bail query */
int     rel;            /* relative phrase position (for trace msg) */
int     mmDictFlags;    /* bit 0: do metamorph linear-dict search
                         * bit 1: do metamorph binary-dict post-proc
                         */
/* Opens scanner for word `wd'.  Does suffix, minwordlen processing as
 * per given flags.  Returns NULL if not found, or error.  FDBIWI
 * field set.  Sets fs->overmaxsetwords, fi->novermaxwords as needed.
 * Derived from wordtottl() in ripmm.c.
 */
{
  static CONST char     fn[] = "openfdbiw";
  static CONST char     cwimsg[] =
    "Corrupt word count for `%s' in Metamorph index %s";
  static CONST char     cwrmsg[] =
    "Corrupt word record for `%s' in Metamorph index %s";
  static CONST RECID    noid = { (EPI_OFF_T)(-1) };
  FDBIW                 *fw;
  byte                  *buf, *d, *h;
  char                  *s = CHARPN, *wdFolded = CHARPN, *xtra, *t;
  size_t                bufsz, sByteLen, sz, allocSz;
  size_t                wdFoldedByteLen, wdFoldedCharLen, sCharLen;
  int                   wpre, wsuf, stot, itot, os, wlim = -1, i, wildtrail;
  RECID                 loc;
  BTREE                 *bt;
  FDBIWI                wi;
  dword                 *hits;
  FDBIX                 *fx;
  SEL                   *sel = SELPN;
  MM3S                  *mm3s;
  TXLOGITYPE            savelogic;
  EPI_HUGEUINT          datblksz;
  char                  sbuf[128], wdFoldedBuf[128], xtrabuf[128];
  char                  xtra2[EPI_OS_INT_BITS+4];

  if (FdbiTraceIdx >= 15) putmsg(MINFO, CHARPN, "  Word at %+d: %s",rel,wd);

  mm3s = fs->set->sel->mm3s;                    /* Bug 3908 */
  *overwords = 0;
  i = strlen(wd);
  wildtrail = (wd[i > 0 ? i - 1 : 0] == '*');   /* WTF MMRIP_WILDTRAIL flag */
  fw = (FDBIW *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBIW) + i + 1);
  if (fw == FDBIWPN)
    goto err;
  fw->flags = (fi->flags & ~(FDF_WILDCARD | FDF_FREEHEAP));
  if (wildproc) fw->flags |= FDF_WILDCARD;
  fw->fi = fi;
  TXsetrecid(&fw->hi.loc, (EPI_OFF_T)(-1));
  fw->hip = &fw->hi;
  wi.nlocs = (EPI_HUGEUINT)0;
  fw->wd = (char *)fw + sizeof(FDBIW);
  strcpy(fw->wd, wd);

  /* Word may have upper-case letters, and/or "*" at end.  Trim and
   * fold case.  May need to loop to re-alloc:
   */
  allocSz = wdByteLen + 1;
  if (allocSz < sizeof(wdFoldedBuf)) allocSz = sizeof(wdFoldedBuf);
  for (;;)
    {
      if (allocSz <= sizeof(wdFoldedBuf))
        wdFolded = wdFoldedBuf;
      else if ((wdFolded = (char *)TXmalloc(TXPMBUFPN, fn, allocSz)) == CHARPN)
        goto err;
      wdFoldedByteLen = TXunicodeStrFold(wdFolded, allocSz, wd, wdByteLen,
                                         fi->textsearchmode);
      /* If there was enough room for string *and* nul-terminator, stop: */
      if (wdFoldedByteLen != (size_t)(-1) && wdFoldedByteLen < allocSz) break;
      /* Otherwise increase allocation request: */
      allocSz += (allocSz >> 1) + 8;
      if (wdFolded != wdFoldedBuf) wdFolded = TXfree(wdFolded);
    }
  /* Will also need `wdFolded' *character* length below: */
  if (fi->textsearchmode & TXCFF_ISO88591)
    wdFoldedCharLen = wdFoldedByteLen;
  else                                          /* UTF-8 mode */
    {
      wdFoldedCharLen = (size_t)(-1);
      TXunicodeGetUtf8CharOffset(wdFolded, wdFolded + wdFoldedByteLen,
                                 &wdFoldedCharLen);
    }

  bt = fi->bt;
  btsetsearch(bt, BT_SEARCH_BEFORE);            /* stop before 1st record */
  sel = fs->set->sel;
  if (mmDictFlags & 0x1)
    rewindbtree(bt);
  else if (prefixproc)                          /* sanity check */
    {
      putmsg(MERR, fn, "Internal error: Cannot binary-search Metamorph index dictionary with prefix-processing enabled");
      goto err;
    }
  else
    btsearch(bt, wdFoldedByteLen, wdFolded);    /* look up word */
  if (minwordlen <= 0 || minwordlen > mm3s->minwordlen)
    minwordlen = mm3s->minwordlen;

  /* KNG 991029  Optimization: we can turn off `suffixproc' if the key
   * word len is less than `minwordlen' - 2, because fdbi_morphemecmp()/
   * Metamorph will never trim another (potentially matching) word
   * down beyond that.  This enables us to bail after the first word
   * if `suffixproc' and `wildproc' are off.  Normally, `suffixproc'
   * is on and `minwordlen' is 255, which means no suffixes will ever
   * match in fdbi_morphemecmp(), but we still read all those words
   * from the B-tree anyway because `suffixproc' is on.  This skips that:
   */
  if ((int)wdFoldedCharLen < (int)minwordlen - 2) suffixproc = 0;

  /* We searched for the root of the word, so the records that follow are
   * 0 or more of the root word, related suffixes, and unrelated suffixes:
   * KNG 030714 or we're linearly searching the whole dictionary
   */

  s = sbuf;
  itot = fi->totwords;
  stot = fs->totwords;
  buf = fi->pgbuf;                              /* scratch buf */
  do
    {
      bufsz = fi->pgbufsz;
      loc = btgetnext(bt, &bufsz, buf, BYTEPPN);
      if (!TXrecidvalid2(&loc)) break;          /* end of dictionary */
      if (s != sbuf) s = TXfree(s);
      sByteLen = strlen((char *)buf);           /* rmsuffix() will mod buf */
      if (sByteLen < sizeof(sbuf))
        {
          s = sbuf;
          strcpy(s, (char *)buf);
        }
      else if ((s = TXstrdup(TXPMBUFPN, fn, (char *)buf)) == CHARPN)
        goto err;
      /* Get `s' *character* length: */
      if (fi->textsearchmode & TXCFF_ISO88591)
        sCharLen = sByteLen;
      else
        {
          sCharLen = (size_t)(-1);
          TXunicodeGetUtf8CharOffset(s, s + sByteLen, &sCharLen);
        }
      /* rmprefix()/rmsuffix() optimization: */
      wpre = (prefixproc && sCharLen >= (size_t)minwordlen);
      wsuf = (suffixproc && sCharLen >= (size_t)minwordlen);
      if (mmDictFlags & 0x3)                    /*Metamorph dict search/post*/
        {
          /* WTF assume *aaa or *aaa* wildcard here: */
          savelogic = sel->logic;
          sel->logic = LOGIAND;                 /* temp override NOT */
          /* KNG 20070225 bugfix do SEARCHNEWBUF init that getmm() would do;
           * remorph()/inset() in findsel() depend on this:  WTF drill:
           */
          for (i = 0; i < mm3s->nels; i++)
            {
              mm3s->el[i]->hit = BPNULL;
              mm3s->el[i]->nib = 0;
            }
          mm3s->start = buf;
          mm3s->end = buf + sByteLen;
          mm3s->hit = BPNULL;
          /* wtf we use findsel() to easily wrap different pattern
           * matchers, for remorph() to handle prefix/suffixes (untested?)
           * and to prefix-match, e.g. `a*bc', which findspm() does not.
           */
          h = findsel(mm3s, fs->set->eln, buf, buf + sByteLen, SEARCHNEWBUF);
          sel->logic = savelogic;
          if (h == BYTEPN) continue;            /* no match */
#ifdef FDBI_NO_SPM_WILDSUFMATCH
          /* getspm() does not yet support TXwildsufmatch; check here: */
          if (TXwildsufmatch &&                 /* *aaa suffix-matches only */
              !wildtrail &&                     /* no trailing wildcard */
              sel->pmtype == PMISSPM &&
              h + spmhitsz(sel->ss) != buf + sByteLen)  /* not tail match */
            continue;
#else /* !FDBI_NO_SPM_WILDSUFMATCH */
          (void)wildtrail;
#endif /* !FDBI_NO_SPM_WILDSUFMATCH */
        }
      else
        {
          if (fdbi_morphemecmp(wdFolded, wdFoldedByteLen, (char *)buf,
                               sByteLen, wpre, wsuf, wildproc, mm3s,
                               minwordlen) != 0)
            continue;                           /* not a valid suffix word */
        }
      if (wlim == -1)                           /* haven't hit word limits? */
        {                                       /*   then check 'em */
          os = 0;
          if (mm3s->qmaxsetwords > 0 && stot >= mm3s->qmaxsetwords)
            os = 2;                             /* over set word limit */
          if (mm3s->qmaxwords > 0 &&            /* word limit is set and */
              itot >= mm3s->qmaxwords &&        /*   that limit exceeded and*/
              stot > 0)                         /*   at least 1 word in set */
            os = 1;
          if (os)                               /* over set/query limit */
            {
              *overwords = 1;
              switch (TXfdbiwInitOver(fw, fs, os - 1, &wlim))
                {
                case 1:   break;                /* continue w/wlim limit */
                case 0:   goto err;             /* drop this term */
                case -1:  *overwords = 2; goto err; /*    "" and bail query */
                case -2:
                default:  goto err;
                }
            }
        }
      d = buf + sByteLen + 1;
      if (TXgetoff2(&loc) < (EPI_OFF_T)0)       /* negative: single-recid */
        {
          wi.ndocs = (EPI_HUGEUINT)1;
          if (d > buf + bufsz)                  /* sanity check: corruption */
            putmsg(MWARN, fn, cwrmsg, s, getdbffn(bt->dbf));
          sz = (buf + bufsz) - d;               /* loc data size */
          if (sz > 0)                           /* if full inverted */
            {
              if (!(hits = (dword*)TXmalloc(TXPMBUFPN, fn, sz*sizeof(dword))))
                goto err;
              wi.nlocs = (EPI_HUGEUINT)fi->vsltolocs(d, sz, hits);
            }
          else
            {
              hits = DWORDPN;
              wi.nlocs = (EPI_HUGEINT)0;
            }
          TXsetrecid(&loc, -TXgetoff2(&loc) - (EPI_OFF_T)2);
          if ((fx = openfdbix(fi->datdf, fw->flags, &wi, loc, (size_t)(-1),
                              hits, (size_t)wi.nlocs, s, fi)) == FDBIXPN)
            goto err;
        }
      else                                      /* point at .dat */
        {
          datblksz = (EPI_HUGEUINT)(-1);
          if ((int)fi->dbi->version >= 3)       /* .dat blk size */
            d = invsh(d, &datblksz);
          d = invsh(d, &wi.ndocs);              /* always 8-bit VSH */
          if (fw->flags & FDF_FULL)
            {
              d = invsh(d, &wi.nlocs);
              if ((int)fi->dbi->version >= 3) wi.nlocs += wi.ndocs;
            }
          if (d > buf + bufsz)                  /* sanity check: corruption */
            putmsg(MWARN, fn, cwimsg, s, getdbffn(bt->dbf));
          if ((fx = openfdbix(fi->datdf, fw->flags, &wi,noid,(size_t)datblksz,
                              DWORDPN, 0, s, fi)) == FDBIXPN)
            goto err;
        }
      /* Set some fx flags for fdbiw_heapcmp_open(): */
      if (wlim == -1 && fw->nwords == 0 && strcmp(s, wdFolded) == 0)
        fx->flags |= FDF_ROOTWORD;
      xtra = "";
      if (wlim >= 0)                            /* word limit set: use heap */
        {
          strcpy((char *)buf, s);               /* scratch space */
          t = (char *)buf;
          rmsuffix(&t, (char **)mm3s->suffix, mm3s->nsuf, wdFoldedCharLen, 0,
                   mm3s->phrasewordproc,  mm3s->textsearchmode);
          /* wtf rmprefix() too?  see TXfdbiwInitOver() too */
          fx->wdrank = strlen(t);
          if ((int)fheap_num(fw->fh) >= wlim)   /* heap is full */
            {
              if (fdbiw_heapcmp_open(fx, (FDBIX*)fheap_top(fw->fh), NULL) > 0)
                {                               /* fx is better than top */
                  if (FdbiTraceIdx >= 3)
                    {
                      htsnpf(xtrabuf, sizeof(xtrabuf),
                             " (replaces %.*s %wd/%wd)",
                             (int)sizeof(xtrabuf) - 60, /*wtf buf overflow?*/
                             ((FDBIX *)fheap_top(fw->fh))->wd,
                             (EPI_HUGEINT)((FDBIX*)fheap_top(fw->fh))->wi.ndocs,
                             (EPI_HUGEINT)((FDBIX*)fheap_top(fw->fh))->wi.nlocs);
                      xtra = xtrabuf;
                    }
                  closefdbix((FDBIX *)fheap_top(fw->fh));
                  fheap_deletetop(fw->fh);
                  if (!fheap_insert(fw->fh, fx)) goto err;
                }
              else
                {
                  closefdbix(fx);
                  fx = FDBIXPN;
                  xtra = " (dropped)";
                }
            }
          else                                  /* heap not full yet */
            if (!fheap_insert(fw->fh, fx)) goto err;
        }
      else                                      /* no limit yet: use array */
        {
          if (!fdbi_alloclist((void **)(char *)&fw->fxp, &fw->nfxp,
                              fw->nwords + 1))
            goto err;
          fw->fxp[fw->nwords++] = fx;
        }
      if (fx != FDBIXPN && fx->df != KDBFPN && !fdbix_seek(fx, loc)) goto err;
      if (FdbiTraceIdx >= 3)
        {
          htsnpf(xtra2, sizeof(xtra2), "%wu/%wu",
                 (EPI_HUGEUINT)wi.ndocs, (EPI_HUGEUINT)wi.nlocs);
          putmsg(MINFO, CHARPN, "   %s %s%s", xtra2, s, xtra);
        }
      stot++;
      itot++;
    }
  while ((mmDictFlags & 0x1) ||                 /* linear dictionary search */
         ((wildproc || suffixproc) &&           /* optimization */
          strncmp(wdFolded, s, wdFoldedByteLen) >= 0)); /*root matches words*/

  if (wlim >= 0)                                /* transfer heap to array */
    {
      if (!fdbi_alloclist((void **)(char *)&fw->fxp, &fw->nfxp,
                          fheap_num(fw->fh)))
        goto err;
      for (i = 0; (size_t)i < fheap_num(fw->fh); i++)
        fw->fxp[i] = (FDBIX *)fheap_elem(fw->fh, i);
      fheap_clear(fw->fh);
      fw->nwords = i;
      fw->flags &= ~FDF_FREEHEAP;
    }
  for (i = 0; i < fw->nwords; i++)
    {
      fx = fw->fxp[i];
      fw->wi.ndocs += fx->wi.ndocs;
      fw->wi.nlocs += fx->wi.nlocs;
    }
  fs->totwords += fw->nwords;
  fi->totwords += fw->nwords;

  switch (fw->nwords)
    {
    case 0:                                     /* nothing found */
      goto err;
    case 1:
      fw->getnext = (FdbiTraceIdx >= 3 ? fdbiw_getnextone_trace :
                     fdbiw_getnextone);
      break;
    default:
      fw->getnext =(FdbiTraceIdx >= 3 ? fdbiw_getnextmulti_trace :
                    fdbiw_getnextmulti);
      if (fw->fh != FHEAPPN)
        {
          fheap_clear(fw->fh);
          fheap_setcmp(fw->fh, fdbiw_heapcmp_getnext);
        }
      else if ((fw->fh = openfheap(fdbiw_heapcmp_getnext, NULL,0)) == FHEAPPN)
        goto err;
      if (!fheap_alloc(fw->fh, fw->nwords)) goto err;
      if (!(fw->fxcur = (FDBIX **)TXmalloc(TXPMBUFPN, fn,
                                           fw->nwords*sizeof(FDBIX*))))
        goto err;
      memcpy(fw->fxcur, fw->fxp, fw->nwords*sizeof(FDBIX *));
      break;
    }
  fw->numcur = fw->nwords;
  goto done;

err:
  fw = closefdbiw(fw);
done:
  if (wdFolded != wdFoldedBuf && wdFolded != CHARPN)
    wdFolded = TXfree(wdFolded);
  if (s != sbuf && s != CHARPN) s = TXfree(s);
  return(fw);
}

/* --------------------------------- FDBIF --------------------------------- */

static FDBIF *closefdbif ARGS((FDBIF *ff));
static FDBIF *
closefdbif(ff)
FDBIF   *ff;
{
  int   i;

  if (ff == FDBIFPN) goto done;
  if (ff->fwp != FDBIWPPN)
    {
      for (i = 0; i < ff->nwords; i++)
        closefdbiw(ff->fwp[i]);
      ff->fwp = TXfree(ff->fwp);
    }
  ff->reloff = TXfree(ff->reloff);
  ff->hi.hits = TXfree(ff->hi.hits);
  ff->hi.hitLens = TXfree(ff->hi.hitLens);
  ff = TXfree(ff);

done:
  return(FDBIFPN);
}

int
fdbi_setpostmsg(fi, msg)
FDBI    *fi;
char    *msg;
/* Sets post-process reason message to `msg', if not set already.
 * `fi' will own malloc'd `msg' regardless.
 */
{
  if (fi->postmsg == CHARPN) fi->postmsg = msg;
  else msg = TXfree(msg);
  return(1);
}

static void fdbi_clearpostmsg ARGS((FDBI *fi));
static void
fdbi_clearpostmsg(fi)
FDBI    *fi;
{
  fi->postmsg = TXfree(fi->postmsg);
  fi->postmsgnotlineardict = 0;
}

static char * CDECL fdbi_prmsg ARGS((char *setmsg, char *curmsg,
                                     CONST char *fmt, ...));
static char * CDECL
fdbi_prmsg(char *setmsg, char *curmsg, CONST char *fmt, ...)
{
  static CONST char     fn[] = "fdbi_prmsg";
  va_list       argp;
  size_t        sz;
  char          *msg;
#  ifdef va_copy
  va_list       argpcopy;
#  else /* !va_copy */
#    define argpcopy    argp
#  endif /* !va_copy */
  char          tmp[256];

  va_start(argp, fmt);
#ifndef EPI_HAVE_STDARG
#  error need stdarg
#endif /* !EPI_HAVE_STDARG */
  if (setmsg != CHARPN || curmsg != CHARPN)
    {
      msg = curmsg;
      goto done;
    }
#ifdef va_copy
  va_copy(argpcopy, argp);
#endif /* va_copy */
  sz = htvsnpf(tmp, sizeof(tmp), fmt, (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN,
               argp, HTPFARGPN, SIZE_TPN, TXPMBUFPN);
  if (sz < sizeof(tmp))
    msg = TXstrdup(TXPMBUFPN, fn, tmp);
  else if ((msg = (char *)TXmalloc(TXPMBUFPN, fn, sz + 1)) != CHARPN)
    htvsnpf(msg, sz + 1, fmt, (HTPFF)0, TXFMTCPPN, TXFMTSTATEPN, argpcopy,
            HTPFARGPN, SIZE_TPN, TXPMBUFPN);
done:
#ifdef va_copy
  va_end(argpcopy);
#endif /* va_copy */
  va_end(argp);
  return(msg);
}

static int fdbi_spmcandictsearch ARGS((FDBI *fi, MMQL *mq,
                 SEL *sel, int setOrpos, int wildoneword));
static int
fdbi_spmcandictsearch(fi, mq, sel, setOrpos, wildoneword)
FDBI    *fi;            /* (in/out) */
MMQL    *mq;            /* (in) the overall Metamorph query */
SEL     *sel;           /* (in) which set to check */
int     setOrpos;       /* (in) set's original-query-order index */
int     wildoneword;    /* (in) TXwildoneword setting */
/* Returns 1 if set `sel' of query `mq' can be dictionary searched
 * for via its SPM scanner (e.g. linearly), and no post-proc needed.
 * Returns 0 if not.
 * NOTE: caller must check TXallineardict.
 * NOTE: `fi->ixrlex' may be used.
 */
{
  static CONST char     fn[] = "fdbi_spmcandictsearch";
  int                   ret, i;
  byte                  *hit;
  MMQI                  *qi = MMQIPN;
  size_t                stripbufsz, sz;
  byte                  *d, *s, *stripbuf, *stripbufend, striptmp[256];

  stripbuf = striptmp;
  stripbufsz = sizeof(striptmp);

  /* Query         stripbuf  qi->nwords   What SPM matches
   * `"six* no"'   `six no'  1            \space `six' .{,80}>>\space= `no'
   * `"six no*"'   `six no'  2            \space `six' \space+ `no'
   * `"*six no*"'  `six no'  2            "" but leading wildcard ignored?
   */

  /* We only deal with SPM: */
  if (sel->pmtype != PMISSPM || sel->ss == SPMSPN)
    goto err;

  /* Find the MMQI for this set, and verify there's only one (should be
   * 'cause SPM?  and we don't want to fail to index-expr check something):
   */
  for (i = 0; i < mq->n; i++)
    if (mq->lst[i].orpos == setOrpos)           /* it is for our set */
      {
        if (qi != MMQIPN) goto err;             /* already found one */
        qi = &mq->lst[i];
      }

  /* Strip all wildcards from the phrase.  Note that this stripping could
   * still leave spaces if it's a phrase, and even if qi->nwords is 1,
   * e.g. `"ab* cd"':
   */
  sz = strlen((char *)qi->s);
  if (qi->wild)                                 /* strip wildcards */
    {                                           /* wtf ask SPM for this info*/
      if (++sz > stripbufsz)                    /* need to realloc */
        {
          if ((stripbuf = (byte *)TXmalloc(TXPMBUFPN, fn, sz)) == BYTEPN)
            goto err;
          stripbufsz = sz;
        }
      for (d = stripbuf, s = qi->s; *s != '\0'; s++)
        {
          if (*s != '*') *(d++) = *s;
        }
      *d = '\0';                                /* just for insurance */
      stripbufend = d;
    }
  else
    {
      stripbuf = qi->s;
      stripbufend = stripbuf + sz;
    }

  /* If wildcards can span words, then a multi-substring SPM may
   * (falsely) not match a dictionary word because it needs to span
   * another word (e.g. `ab*cd' could SPM match `abxyz zyxcd', but
   * dictionary would only have `abxyz' or `zyxcd' alone).
   * Suffix matching (TXwildsufmatch) is checked by SPM or its caller:
   */
  if (qi->wild && qi->needmm && !wildoneword) goto err;

  /* An index expression must entirely match the wildcard-stripped query,
   * as a contiguous buffer: otherwise SPM may (falsely) not match an
   * index word, whereas it should match and we should post-proc
   * (e.g. `ab*.' will not SPM-match index word `abrasive'; wtf we
   * could create our own partial SPM `at*' and turn on post-proc?).
   * WTF NOTE: We assume that wildcards will not span non-indexable
   * chars, i.e. index expressions essentially fully cover SPM's idea
   * of a word.  E.g. `at*t' SPM-matches `AT&T', but if index
   * expressions do not include `&', SPM will (falsely) not match in a
   * linear dictionary search.
   */
  if (!fi->ixrlex) goto err;                    /* closed earlier? */
  for (hit = getrlex(fi->ixrlex, stripbuf, stripbufend, SEARCHNEWBUF);
       hit != BYTEPN;
       hit = getrlex(fi->ixrlex, stripbuf, stripbufend, CONTINUESEARCH))
    {
      if (hit == stripbuf && hit + rlexlen(fi->ixrlex) == stripbufend) break;
    }
  if (hit == BYTEPN) goto err;                  /* not fully indexable */

  /* The wildcard-stripped query must be a decent size, to keep the number
   * of matching index words reasonable.  Note that we used to only check
   * the first substring, e.g. for `a*bcdef' we only checked `a'; but later
   * substrings will reduce matches, so now we include them in the count:
   */
  if (stripbufend - stripbuf < TXindexminsublen) goto err;

  /* All tests passed.
   * WTF openfdbiw() assumes one-element, leading wildcard
   * (e.g. *aaa or *aaa* only) for linear-dictionary search:
   */
  ret = 1;                                      /* ok to use */
  goto done;

err:
  ret = 0;
done:
  if (stripbuf != striptmp && qi != MMQIPN && stripbuf != qi->s &&
      stripbuf != BYTEPN)
    stripbuf = TXfree(stripbuf);
  return(ret);
}

static FDBIF *openfdbif ARGS((FDBI *fi, FDBIS *fs, MMQL *mq,
     int equivIdx, int lineardict, int *ignset, int *needmm, int *overwords));
static FDBIF *
openfdbif(fi, fs, mq, equivIdx, lineardict, ignset, needmm, overwords)
FDBI    *fi;            /* root FDBI object */
FDBIS   *fs;            /* (in/out) current set */
MMQL    *mq;            /* (in) MMQL for the whole query */
int     equivIdx;       /* (in) which equiv (phrase) in `mq' to search for */
int     lineardict;     /* (in) non-zero: use linear-dict. search instead */
int     *ignset, *needmm, *overwords;
/* Opens scanner for `equivIdx'th phrase of `mq'.  Does phrase processing,
 * and suffix/minwordlen processing via FDBIW.  Returns NULL if not found,
 * or error.  Sets FDBIWI field.  Derived from phrasetottl() in ripmm.c.
 * Sets `*ignset' nonzero if this is not an index-searchable phrase
 * (e.g. all single-letter/noise words or other stuff which is never
 * indexed), even if NULL is returned.  Sets `*needmm' if Metamorph
 * post-search is needed, e.g. a non-indexable word is present, or a
 * non-inverted index is being used.  Sets `fi->postmsg' if not set.
 */
{
  static CONST char     fn[] = "openfdbif";
  static CONST char     submatchfmt[] =
#if TX_VERSION_MAJOR >= 6
    "Index expression(s) match non-prefix substring of term `%s'";
#else /* TX_VERSION_MAJOR < 6 */
    "Index expression(s) only substring-match `%s'";
#endif /* TX_VERSION_MAJOR < 6 */
  static CONST char     idxnomatchfmt[] =
    "Index expression(s) do not match term `%s'";
  FDBIF                 *ff;
  FDBIW                 *fw;
  MMQI                  *qi = &mq->lst[equivIdx];
  int                   postmsgnotlineardict = 0;
  int                   prefixprocword, suffixprocword, wildprocword;
  int                   i, minwordlen, needmmwild, hlen;
  size_t                wdByteLen, wdByteLenOrg, wdCharLenOrg, n;
  int                   thisWord_OffsetInPhrase, cur_OffsetInPhrase;
  int                   numresolvable, thisneedmm, owords;
  int                   maxsubhitlen, maxprefixhitlen, invpwp = 0;
  int                   mmDictFlags = (lineardict ? 0x1 : 0);
  char                  *wd, wdch = '\0', *pm = CHARPN, *p;
  byte                  *hit, *phit, *we, *weorg;
  CONST char            *xtra;
  MM3S                  *mm3s;

  if (FdbiTraceIdx >= 14) putmsg(MINFO, CHARPN, " Phrase: %s", qi->s);

  mm3s = fs->set->sel->mm3s;                    /* Bug 3908 */
  *ignset = 1;
  *overwords = *needmm = needmmwild = numresolvable = 0;
  ff = (FDBIF *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBIF) + strlen((char *)qi->s) + 1);
  if (ff == FDBIFPN ||
      (ff->fwp = (FDBIW **)TXcalloc(TXPMBUFPN, fn, qi->nwords,
                                    sizeof(FDBIW *))) == FDBIWPPN ||
      (ff->reloff = (int *)TXcalloc(TXPMBUFPN, fn, qi->nwords,
                                    sizeof(int))) == INTPN)
    goto err;
  ff->flags = fi->flags;
  if (mm3s->exactphrase == API3EXACTPHRASEIGNOREWORDPOSITION)
    ff->flags |= FDF_IGNWDPOS;
  ff->fi = fi;
  ff->hip = &ff->hi;
  ff->phrase = (char *)ff + sizeof(FDBIF);
  strcpy(ff->phrase, (char *)qi->s);

  minwordlen = mm3s->minwordlen;
  cur_OffsetInPhrase = 0;
  /* KNG 20050926 only subtract previous word length (+1 for space) from
   * minwordlen if API3PHRASEWORDMONO: all other modes use constant minwordlen
   * (or none).  Theoretically should check for actual phrase (qi->nwords > 1)
   * too, but loop exits before `minwordlen' used again if qi->nwords == 1:
   */
  for (i = 0; i < qi->nwords; i++, minwordlen -=
         (mm3s->phrasewordproc == API3PHRASEWORDMONO ? (wdCharLenOrg + 1)
          : 0), minwordlen = (minwordlen < 1 ? 1 : minwordlen))
    {                                           /* for each word in phrase */
      mmDictFlags &= ~0x2;
      wd = (char *)qi->words[i];
      wdByteLen = wdByteLenOrg = qi->lens[i];
      thisneedmm = 0;
      if ((n = strlen(wd)) < wdByteLen)         /* wtf? */
        {
#ifdef FDBI_WARN
          putmsg(MWARN, fn, "Word `%s' short (wdByteLen = %wu)",
                 wd, (EPI_HUGEUINT)wdByteLen);
#endif
          wdByteLen = wdByteLenOrg = n;
        }
      /* Get *character* length of `wd' for later use: */
      if (fi->textsearchmode & TXCFF_ISO88591)
        wdCharLenOrg = wdByteLenOrg;
      else
        {
          wdCharLenOrg = (size_t)(-1);
          TXunicodeGetUtf8CharOffset(wd, wd + wdByteLenOrg, &wdCharLenOrg);
        }
      /* NOTE: these are set for compatibility with SPM linear search;
       * they may change based on phrasewordproc (e.g. API3PHRASEWORDALL):
       */
      /* only prefix-process first word in phrase, ala SPM/linear search: */
      prefixprocword = (i == 0 && mm3s->prefixproc);
      /* only suffix-process last word in phrase, ala SPM/linear search: */
      suffixprocword = (i == qi->nwords - 1 && qi->suffixproc);
      /* only wildcard-process last word in phrase, ala SPM/linear search: */
      wildprocword = (i == qi->nwords - 1 && qi->wild);
      thisWord_OffsetInPhrase = cur_OffsetInPhrase;/*save now; latter changes*/
      if (lineardict)                           /* caller sez it's indexable*/
        {
          ff->nindexable++;
          cur_OffsetInPhrase++;
        }
      else if (fi->ixrlex != RLEXPN)            /* we know the index exprs */
        {
          /* See if the word matches an index expression (was indexed
           * in whole or prefix).  Also see if other expressions match
           * part of the word, but at a new, later byte position:
           * those are additional word locations in the index, and we
           * need to correct `cur_OffsetInPhrase' for later words.
           * This parallels the word location algorithm in
           * wtix_insert().  KNG 990111
           */
          we = weorg = (byte *)wd + wdByteLen;
          maxprefixhitlen = -1;
          phit = BYTEPN;
          maxsubhitlen = 0;
          /* KNG 20040713    After texis/ripmm.c:1.32 fix, we will not match
           * even a non-prefix substring (for wildsingle) since wdByteLen==0.
           * Search the whole thing, but note later since we may match `*':
           */
          if (wdByteLen == 0 &&                 /* probable leading wild */
              i == qi->nwords - 1 &&            /* wild applies last word */
              (qi->wild & MMRIP_WILDLEAD))      /* leading wildcard */
            we = (byte *)wd + strlen(wd);
          for (hit = getrlex(fi->ixrlex, (byte *)wd, we, SEARCHNEWBUF);
               hit != BYTEPN;
               phit = hit,
                 hit = getrlex(fi->ixrlex, (byte *)wd, we, CONTINUESEARCH))
            {
              if ((hlen = rlexlen(fi->ixrlex)) > maxsubhitlen)
                maxsubhitlen = hlen;
              if (we != weorg) continue;        /* if *abc fixup */
              if (hit == (byte *)wd)            /* matches a prefix */
                {
                  if (hlen > maxprefixhitlen) maxprefixhitlen = hlen;
                }
              else if (hit > phit && maxprefixhitlen >= 0)
                cur_OffsetInPhrase++;           /* new word location */
            }
          if (maxprefixhitlen < 0)              /* no prefix match */
            {
              /* No prefix match, so we cannot binary-search dictionary.
               * 20030714 Check if linear dictionary search is ok.
               * 20051018 Check moved to openfdbis().
               */
              if (maxsubhitlen >= TXindexminsublen)
                pm = fdbi_prmsg(fi->postmsg, pm, submatchfmt, wd);
              goto ni;                          /* need linear search */
            }
          if (prefixprocword)
            {
              /* Prefix-processing means different prefixes may match,
               * so we cannot binary-search dictionary.
               * openfdbis() will check if linear dictionary search is ok.
               *
               * If prefixproc is on for all words in the phrase, message
               * will be for linear/linear-dict (nothing binary-searchable).
               * If it's on only for some (not all) words in the phrase,
               * this is a post-proc msg because the other word(s) are
               * binary-searchable.  Note that API3PHRASEWORDALL should
               * be checked; ignored for now since it is unsupported.
               * See API3PHRASEWORDALL below too:
               */
              pm = fdbi_prmsg(fi->postmsg, pm,
        (qi->nwords > 1 /* wtf && mm3s->phrasewordproc != API3PHRASEWORDALL */
                              ? "Prefix-processing some phrase words" :
                                "Prefix-processing entire phrase"));
              goto ni;                          /* need linear search */
            }
          ff->nindexable += 1;
          cur_OffsetInPhrase++;                 /* new word location */
          if (maxprefixhitlen < 0 || (size_t)maxprefixhitlen < wdByteLen)
            {                                   /* partial prefix indexable */
              if (pm == CHARPN) postmsgnotlineardict = 1;
              pm = fdbi_prmsg(fi->postmsg, pm,
#if TX_VERSION_MAJOR >= 6
                  "Index expression(s) only partially prefix-match term `%s'",
#else /* TX_VERSION_MAJOR < 6 */
                       "Term `%s' only partially matches index expression(s)",
#endif /* TX_VERSION_MAJOR < 6 */
                              wd);
              if (maxprefixhitlen < TXindexminsublen) goto nmm;
              /* A (decent-sized) prefix of the word matches.  We can
               * use that prefix with the index, but we'll need a
               * post-search to verify the hits:
               */
              wdByteLen = maxprefixhitlen;
              wdch = wd[wdByteLen];
              wd[wdByteLen] = '\0';
              *needmm = thisneedmm = 1;
            }
        }                                       /* end we-know-index-exprs */
      else if (wdByteLen < 2)                   /* wtf assume \alnum{2,99} */
        {
        ni:
          pm = fdbi_prmsg(fi->postmsg, pm, idxnomatchfmt, wd);
          goto nmm;
        }
      else                                      /* wtf assume indexable */
        {
          ff->nindexable += 1;
          cur_OffsetInPhrase++;
        }

      /* Check for noise word, _after_ we checked if it's indexable so
       * `cur_OffsetInPhrase' and `ff->nindexable' are set correctly for
       * "a": KNG 991123
       */
      if (!lineardict &&                        /* not linear-dict search */
          !wildprocword &&                      /* not a wildcard */
          !mm3s->keepnoise && fi->isnoise(fi->noise, fi->nnoise, wd))
        {                                       /* noise word (not in index)*/
          if (pm == CHARPN) postmsgnotlineardict = 1;
          pm = fdbi_prmsg(fi->postmsg, pm, "Term `%s' is an index noise word",
                          wd);
        nmm:
          *needmm = thisneedmm = 1;
          if (wdByteLen < wdByteLenOrg) wd[wdByteLen] = wdch;
          continue;
        }

      /* If this is an "aaa*bbb" wildcard, we need a post-search
       * to resolve the "bbb".  So check qi->needmm: we can trust
       * it if qi->wild is set (only last phrase word can be wildcard?).
       * KNG 000112 ripmm fixed; doesn't set needmm for all phrases:
       */
      if (!lineardict && qi->wild && qi->needmm)
        {
          /* KNG 20080427 Bug 2151  We might be able to let Metamorph
           * do the post-processing in the *dictionary* right now (in
           * openfdbiw()) instead of reading table and post-processing later.
           * Note that since openfdbiw() should still be able to binary
           * search dictionary, we can do this regardless of allineardict:
           */
          if (fdbi_spmcandictsearch(fi, mq, fs->set->sel, qi->orpos,
                                    TXwildoneword))
            mmDictFlags |= 0x2;                 /* Metamorph dict post-proc */
          else
            {
              xtra = "";
              if (!TXwildoneword &&
                  fdbi_spmcandictsearch(fi, mq, fs->set->sel, qi->orpos, 1))
                xtra = " and wildoneword off";
              pm = fdbi_prmsg(fi->postmsg, pm,
                              "Wildcard term `%s' has suffix%s", wd, xtra);
              *needmm = needmmwild = thisneedmm = 1;
            }
        }
      /* KNG 20050926 check phrasewordproc; applies to multi-word phrases: */
      /* 1 word: always prefix+suffixproc; linear dict: openfdbiw() ignores */
      if (lineardict || qi->nwords <= 1) goto dopresuf;
      switch (mm3s->phrasewordproc)
        {
        case API3PHRASEWORDALL:                 /* suffix proc all words */
          /* KNG 20050926 wtf `all' not supported by linear Metamorph yet;
           * treated as default (`last').  Do the same here for consistency:
           * see also "Prefix-processing enable ..." postmsg above.
           */
        default:
          if (!invpwp++)
            putmsg(MWARN + UGE, fn, "Invalid phrasewordproc setting (%d)",
                   (int)mm3s->phrasewordproc);
          /* fall through: */
        case API3PHRASEWORDMONO:                /* treat as monolithic word */
        case API3PHRASEWORDLAST:                /* proc last word only */
          /* Both mono and last will prefix-process the first word only,
           * and suffix-process the last word only (for linear search
           * compatibility); the difference is that `mono' reduces
           * `minwordlen' by previous word lengths (see above):
           */
        dopresuf:
          fw = openfdbiw(fi, fs, wd, wdByteLen, prefixprocword,
                         suffixprocword, wildprocword, minwordlen, &owords,
                         thisWord_OffsetInPhrase, mmDictFlags);
          break;
        case API3PHRASEWORDNONE:                /* no prefix/suffix proc */
          fw = openfdbiw(fi, fs, wd, wdByteLen, 0,
                         0,              0,            0,          &owords,
                         thisWord_OffsetInPhrase, mmDictFlags);
          break;
        }
      if (wdByteLen < wdByteLenOrg) wd[wdByteLen] = wdch;
      if (fw != FDBIWPN)                        /* word found somewhere */
        {
          *ignset = 0;                          /* it's an indexable word */
          ff->fwp[ff->nwords] = fw;
          ff->reloff[ff->nwords++] = thisWord_OffsetInPhrase;
        }
      else                                      /* word not found */
        {
          if (owords)                           /* word limit exceeded */
            {
              if (pm == CHARPN) postmsgnotlineardict = 1;
              pm = fdbi_prmsg(fi->postmsg, pm,
                         "Max words per set/query exceeded at term `%s'", wd);
              *overwords = *needmm = thisneedmm = 1;
              break;
            }
          /* KNG 981102 if (whole or partially) indexable word is not
           * found, then whole phrase is missing too.  KNG 990812 Note
           * that we're blowing off the remaining terms; must ensure
           * `*needmm' and `*ignset' have proper values.  `*ignset'
           * must be cleared for this term; it's indexable.  We can
           * also clear `*needmm' since there's no need to post-process
           * anything in a phrase the index knows cannot be found:
           */
          *ignset = *needmm = 0;
          pm = TXfree(pm);
          goto err;
        }
      if (!thisneedmm) numresolvable++;         /* another 100% index term */
      if (lineardict) break;                    /* only 1 "word" */
    }                                           /* end each word in phrase */

  /* The current index-word offset -- now just beyond last phrase word
   * -- is the phrase's *max* length, i.e. it might also be covered by
   * fewer index words; e.g. "\alnum+" and "\alnum+>>[&']=\alnum+"
   * match "o'brien" as either one (second expression) or two (first
   * expression) index words.  However, we use the *max* length for
   * phrase length, because that length counts when considering any
   * other sets beyond this one.  E.g. for same index expressions, for
   * sets "miles" and "o'brien", while text "miles o'brien" has them
   * completely in a 2 index-word span (using second expression to
   * cover "o'brien" completely), "o'brien miles" is only covered by 3
   * index words -- because "miles" would be indexed at word offset 2,
   * after "brien".
   */
  ff->phraseLen = cur_OffsetInPhrase;

  switch (ff->nwords)                           /* now compute FDBIWI */
    {
    case 0:                                     /* no words: nothing to do */
      goto err;
    case 1:                                     /* 1 word: simple case */
      ff->wi = ff->fwp[0]->wi;
      ff->getnext = (FdbiTraceIdx >= 3 ? fdbif_getnextone_trace :
                     fdbif_getnextone);
      break;
    default:                                    /* N words: Cannot Know yet */
      /* For multi-word phrases, we can turn off Metamorph post-search
       * for unindexable/noise words (but not wildcards) if we're not
       * doing exact phrases, but we should have at least one
       * wholly-indexed term to "anchor" the phrase: KNG 990812
       * KNG 20070501 Note that exactphrase setting `ignorewordposition'
       * still implies `off', since the idea is to emulate non-inverted-index
       * no-postproc phrase behavior (words in any order/position),
       * but on an inverted index:
       */
      if (!needmmwild && numresolvable > 0)
        {
          if (mm3s->exactphrase == API3EXACTPHRASEON)
            {
              if (pm != CHARPN &&
                  (p = (char *)TXmalloc(TXPMBUFPN, fn, strlen(pm) + 20)) !=
                  CHARPN)
                {
                  strcpy(p, pm);
                  strcat(p, " and exactphrase on");
                  pm = TXfree(pm);
                  pm = p;
                }
            }
          else                                  /* off or ignorewordposition*/
            {
              *needmm = 0;
              if (!*ignset && pm != CHARPN)
                pm = TXfree(pm);
            }
        }
      /* Since there's more than one index word; if we don't have an
       * inverted index we cannot do any phrase resolution, exact or not:
       */
      if (!(ff->flags & FDF_FULL))
        {
          *needmm = 1;
          if (pm == CHARPN) postmsgnotlineardict = 1;
          pm = fdbi_prmsg(fi->postmsg, pm,
                     "Index %.*s is not Metamorph inverted (for phrase `%s')",
           (int)(strlen(fi->tokfn)-(sizeof(FDBI_TOKSUF)-1)), fi->tokfn,qi->s);
        }
      /* We cannot know the number of intersections yet, but we know an
       * upper limit, which is the number for the least-frequent term:
       */
      ff->wi.ndocs = ff->wi.nlocs = (EPI_HUGEUINT)(-1);
      for (i = 0; i < ff->nwords; i++)
        {
          if (ff->fwp[i]->wi.ndocs < ff->wi.ndocs)
            ff->wi = ff->fwp[i]->wi;
        }
      ff->getnext = (FdbiTraceIdx >= 3 ? fdbif_getnextmulti_trace :
                     fdbif_getnextmulti);
      break;
    }
  goto done;

err:
  ff = closefdbif(ff);
done:
  if (*ignset)
    pm = fdbi_prmsg(fi->postmsg, pm,
#if TX_VERSION_MAJOR >= 6
                  "Index expression(s) do not match any terms of phrase `%s'",
#else /* TX_VERSION_MAJOR < 6 */
                    "Phrase `%s' has no terms matching index expression(s)",
#endif /* TX_VERSION_MAJOR < 6 */
                    qi->s);
  if (pm != CHARPN)
    {
      if (fi->postmsg == CHARPN)
        fi->postmsgnotlineardict = postmsgnotlineardict;
      fdbi_setpostmsg(fi, pm);
    }
  return(ff);
}

/* --------------------------------- FDBIS --------------------------------- */

static int
fdbis_decodemerge(fs)
FDBIS   *fs;
/* Delayed decode and merge function for multi-phrase-occurence FDBIS.
 */
{
  int           i;
  FDBIHI        *h;

  for (i = 0; i < fs->numcur; i++)              /* decode for ormerge() */
    {
      h = fs->ffcur[i]->hip;
      if (h->decodefunc != FDBIHICBPN &&
          !h->decodefunc(h->decodeusr))
        goto err;
    }
  if (!ormerge(fs->fi, &fs->hi, (FDBIHI ***)fs->ffcur, fs->numcur))
    goto err;
  return(1);
err:
  return(0);
}

FDBIS *
closefdbis(fs)
FDBIS   *fs;
{
  int   i;

  if (fs == FDBISPN || fs == FDBIS_CANLINEARDICTSEARCH) goto done;
  fs->hi.hits = TXfree(fs->hi.hits);
  fs->hi.hitLens = TXfree(fs->hi.hitLens);
  if (fs->ffp != FDBIFPPN)
    {
      for (i = 0; i < fs->nphrases; i++)
        closefdbif(fs->ffp[i]);
      fs->ffp = TXfree(fs->ffp);
    }
  fs->ffcur = TXfree(fs->ffcur);
  closefheap(fs->fh);
  fs = TXfree(fs);

done:
  return(FDBISPN);
}

static int fdbis_heapcmp ARGS((void *a, void *b, void *usr));
static int
fdbis_heapcmp(a, b, usr)
void    *a, *b, *usr;
/* Heap comparison function for fdbis_getnextmulti().
 */
{
  (void)usr;
  return(TXrecidcmp(&((FDBIF *)a)->hip->loc, &((FDBIF *)b)->hip->loc));
}

FDBIS *
openfdbis(fi, mq, setOrpos, rs, lineardict, indg, overmaxset)
FDBI            *fi;
MMQL            *mq;
int             setOrpos;               /* (in) original-order index of set */
RPPM_SET        *rs;
int             lineardict;             /* (in) non-zero: linear-dict */
int             *indg, *overmaxset;
/* Opens scanner for set `setOrpos' of `mq'.  Returns NULL on error, or
 * non-indexable set (eg non-SPM/PPM pattern matcher, or non-indexable
 * phrase present).  Sets FDBIWI field.  Note: if no words found (but
 * indexable), still returns open FDBIS object (0 words).  Sets
 * `*indg' to 1 if index can "guarantee" all hits (e.g. a post-search
 * is not needed), as far as this set is concerned: but still need to
 * check "w/" expressions, etc. elsewhere.  If `lineardict' is 0, may
 * return FDBIS_CANLINEARDICTSEARCH if set is not binary-search indexable
 * but could be linear-dictionary searched (without post-process);
 * caller must determine whether it *may* be searched.
 * (fdbi_get() then may later call w/`lineardict' non-zero).
 */
{
  static CONST char     fn[] = "openfdbis";
  FDBIS                 *fs;
  FDBIF                 *ff;
  MMQI                  *qi;
  SEL                   *sel;
  int                   n, i, unk, ignset, needmm, overwords;
  char                  *s, *savepostmsg;
  int                   savepostmsgnotlineardict;
  MM3S                  *mm3s = rs->sel->mm3s;  /* Bug 3908 */

  if (FdbiTraceIdx >= 13)
    putmsg(MINFO, CHARPN, "Set: %s%s",
           /* KNG 040408 `setOrpos' could be past `nels' if LIKEIN: */
           (setOrpos < mm3s->nels ? (char *)mm3s->set[setOrpos] : "?"),
           (lineardict ? " (lineardict)" : ""));

  /* Save and clear postmsg so we can detect if openfdbif() sets it: */
  savepostmsg = fi->postmsg;
  fi->postmsg = CHARPN;
  savepostmsgnotlineardict = fi->postmsgnotlineardict;
  fi->postmsgnotlineardict = 0;

  *indg = 1;
  *overmaxset = unk = n = 0;
  for (i = 0; i < mq->n; i++)
    {
      qi = &mq->lst[i];
      if (qi->orpos != setOrpos) continue;
      unk += strlen((char *)qi->s) + 1;
      n++;
    }

  if ((fs = (FDBIS *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBIS) + unk)) == FDBISPN ||
      (fs->ffp = (FDBIF **)TXcalloc(TXPMBUFPN, fn, n, sizeof(FDBIF *))) ==
      FDBIFPPN)
    goto err;
  fs->setname = (char*)fs + sizeof(FDBIS);
  s = fs->setname;
  fs->flags = fi->flags;
  fs->fi = fi;
  fs->set = fs->hi.set = rs;
  /* NOTE: NOT-set heap init in fdbi_get() assumes hip set and hip->loc <= 0,
   * before any getnext() calls:
   */
  TXsetrecid(&fs->hi.loc, (EPI_OFF_T)(-1));
  fs->hip = &fs->hi;
  unk = 1;
  for (i = 0; i < mq->n; i++)                   /* open all equivs this set */
    {
      qi = &mq->lst[i];
      if (qi->orpos != setOrpos) continue;
      unk = 0;
      ff = openfdbif(fi, fs, mq, i, lineardict, &ignset, &needmm,
                     &overwords);
      if (ff != FDBIFPN)
        {
          if (fs->nphrases > 0) *(s++) = ',';
          strcpy(s, (char *)qi->s);
          s += strlen(s);
          fs->ffp[fs->nphrases++] = ff;
          fs->wi.ndocs += ff->wi.ndocs;
          fs->wi.nlocs += ff->wi.nlocs;
          if (fs->nphrases == 1 || ff->phraseLen < fs->minPhraseLen)
            fs->minPhraseLen = ff->phraseLen;
          if (ff->phraseLen > fs->maxPhraseLen)
            fs->maxPhraseLen = ff->phraseLen;
          if (ff->phraseLen - ff->nwords > fs->maxExtraExprMatches)
            fs->maxExtraExprMatches = ff->phraseLen - ff->nwords;
        }
      /* Note: we use openfdbif()'s value of `needmm', not qi->needmm,
       * because mmrip() thinks all phrases need a post search.
       * We should really fix mmrip() for inverted indexes...
       */
      if (needmm || ignset) *indg = 0;          /* i.e. need post search */
      if (overwords)                            /* set/total words exceeded */
        {
          if (fs->overmaxsetwords) *overmaxset = 1;
          if (mm3s->denymode == API3DENYERROR)
            {
              FdbiBonusError = 1;               /* wtf so JMT knows */
              goto err;
            }
          /* continue for needmm/ignset logic */
        }
      /* If openfdbif() says nothing in this phrase is indexable,
       * then we have to blow off the whole set, because it's an OR
       * of all the phrases and this one could occur any/everywhere.
       * KNG 20051021 but maybe set can be linear-dictionary searched;
       * check if so and return flag (fdbi_get() uses this later).
       * (This check is now at set-level instead of phrase level
       * because pattern matcher spans whole set.)  We don't check
       * for linear-dictionary search if set is indexable but needs
       * post-proc: that will either be handled in openfbdiw() (WTF)
       * or dict-search cannot do the post-proc.
       */
      if (ignset)
        {
          sel = fs->set->sel;
          fs = closefdbis(fs);
          if (!lineardict &&
              /* do not flag as lineardict-able for overmaxset, noise, etc: */
              !fi->postmsgnotlineardict &&
              fdbi_spmcandictsearch(fi, mq, sel, setOrpos, TXwildoneword))
            {
              fs = FDBIS_CANLINEARDICTSEARCH;
              /* fi->postmsg goes to lineardictmsg: */
              if (fi->lineardictmsg == CHARPN)  /* not set yet */
                {
                  fi->lineardictmsg = fi->postmsg;
                  fi->postmsg = CHARPN;
                }
              else if (fi->postmsg != CHARPN)
                fi->postmsg = TXfree(fi->postmsg);
            }
          goto done;
        }
      if (lineardict) break;                    /* only one "phrase" */
    }

  switch (fs->nphrases)
    {
    case 0:
      if (unk) goto err;                        /* unknown set */
      fs->getnext = (FdbiTraceIdx >= 3 ? fdbis_getnextzero_trace :
                     fdbis_getnextzero);
      break;
    case 1:
      fs->getnext = (FdbiTraceIdx >= 3 ? fdbis_getnextone_trace :
                     fdbis_getnextone);
      /* Optimization: if it's turtles all the way down, skip the middle
       * layers and just do the FDBIX get-next ourselves:  KNG 990115
       */
      if (FdbiTraceIdx >= 3)
        {
          if (fs->ffp[0]->getnext == fdbif_getnextone_trace &&
              fs->ffp[0]->fwp[0]->getnext == fdbiw_getnextone_trace)
            {
              fs->sfx = fs->ffp[0]->fwp[0]->fxp[0];
              fs->getnext = fdbis_getnextone_skip_trace;
            }
        }
      else                                      /* FdbiTraceIdx < 3 */
        {
          if (fs->ffp[0]->getnext == fdbif_getnextone &&
              fs->ffp[0]->fwp[0]->getnext == fdbiw_getnextone)
            {
              fs->sfx = fs->ffp[0]->fwp[0]->fxp[0];
              fs->getnext = fdbis_getnextone_skip;
            }
        }
      break;
    default:
      fs->getnext = (FdbiTraceIdx >= 3 ? fdbis_getnextmulti_trace :
                     fdbis_getnextmulti);
      if ((fs->fh = openfheap(fdbis_heapcmp, NULL, 0)) == FHEAPPN ||
          !fheap_alloc(fs->fh, fs->nphrases))
        goto err;
      if ((fs->ffcur = (FDBIF **)TXmalloc(TXPMBUFPN, fn,
                                   fs->nphrases*sizeof(FDBIF *))) == FDBIFPPN)
        goto err;
      memcpy(fs->ffcur, fs->ffp, fs->nphrases*sizeof(FDBIF *));
      break;
    }
  fs->numcur = fs->nphrases;
  goto done;

err:
  fs = closefdbis(fs);
done:
  if (savepostmsg != CHARPN)
    {
      fi->postmsg = TXfree(fi->postmsg);
      fi->postmsg = savepostmsg;
      fi->postmsgnotlineardict = savepostmsgnotlineardict;
    }
  return(fs);
}

int
TXfdbisSetRppmSet(fs, set)
FDBIS           *fs;
RPPM_SET        *set;
/* Sets `fs->hi.set = set'; also sets descendant FDBI[PWX].hi.set values.
 * Called as RPPM.sets array is compacted, to fix up FDBIHI.set values.
 * Returns 0 on error.
 */
{
  int   f, w, x;
  FDBIF *ff;
  FDBIW *fw;
  FDBIX *fx;

  fs->hi.set = fs->set = set;
  for (f = 0; f < fs->nffp; f++)                /* each phrase in set */
    {
      ff = fs->ffp[f];
      ff->hi.set = set;
      for (w = 0; w < ff->nwords; w++)          /* each wildcard in phrase */
        {
          fw = ff->fwp[w];
          fw->hi.set = set;
          for (x = 0; x < fw->nfxp; x++)        /* each word in wildcard */
            {
              fx = fw->fxp[x];
              fx->hi.set = set;
            }
        }
    }
  return(1);
}

/* ---------------------------- 8-bit VSH/VSL ------------------------------ */

#ifndef VSL1MAX
#  define VSL1MAX 0x3FL
#  define VSL2MAX 0x3FFFL
#  define VSL3MAX 0x3FFFFFL
#  define VSL4MAX 0x3FFFFFFFL
#endif

size_t
linkstovsl(klocs, firstkloc, buf, np, toterrs)
dword   *klocs;
dword   firstkloc;
byte    *buf;
size_t  *np;
long    *toterrs;
/* Encodes the linked list of positions as a list of DELTA VSL's.
 * Assumes that the buffer is large enough to hold the results.
 * Increments `*toterrs' with number of errors (e.g. bad values for VSL);
 * will not report error message for individual values if over 3 errors.
 * Returns the size of the result, and sets `*np' to number of locations.
 */
{
  static CONST char     fn[] = "linkstovsl";
  dword                 loc, oloc = 0, v, av;
  byte                  size;
  short                 i;
  byte                  *bp = buf;
  size_t                n = 0;

  loc = firstkloc;
  do
    {
#ifdef WTIX_UNIQUE_LOCS
      v = loc - oloc;
#else /* !WTIX_UNIQUE_LOCS */
      av = klocs[loc];
      v = av - oloc;
#endif /* !WTIX_UNIQUE_LOCS */
      if (v < VSL1MAX)
        size = 0;
      else if (v < VSL2MAX)
        size = 1;
      else if (v < VSL3MAX)
        size = 2;
      else if (v < VSL4MAX)
        size = 3;
      else
        {
          if (++(*toterrs) <= 3L)
            putmsg(MERR, fn, "Value 0x%wx too large for VSL", (EPI_HUGEUINT)v);
          goto next;                            /* wtf just stumble on */
        }

      for (i = size; i >= 0; i--, v >>= 8)      /* qty in msb->lsb order */
        bp[i] = (byte) (v & 0xff);

      *bp |= (size << 6);                       /* or type into first byte */

      bp += (size + 1);
    next:
#ifdef WTIX_UNIQUE_LOCS
      oloc = loc;
      loc = klocs[loc];
#else /* !WTIX_UNIQUE_LOCS */
      oloc = av;
      loc = klocs[loc + 1];
#endif /* !WTIX_UNIQUE_LOCS */
      n++;
    }
  while (loc != 0);

  *np = n;
  return((size_t)(bp - buf));
}

size_t
locstovsl(locs, nlocs, buf, toterrs)
dword   *locs;          /* positions array */
size_t  nlocs;          /* number of elements of `locs' */
byte    *buf;           /* output buffer */
long    *toterrs;       /* incremented for each error */
/* Same as linkstovsl(), but for locs array, not links (symmetric with
 * vsltolocs()).  Returns the size of data written to `buf'.
 */
{
  static CONST char     fn[] = "locstovsl";
  dword                 v, *e;
  byte                  size;
  short                 i;
  byte                  *bp;

  for (bp = buf, v = 0, e = locs + nlocs; locs < e; v = *(locs++))
    {
      v = *locs - v;
      if (v < VSL1MAX)
        size = 0;
      else if (v < VSL2MAX)
        size = 1;
      else if (v < VSL3MAX)
        size = 2;
      else if (v < VSL4MAX)
        size = 3;
      else
        {
          if (++(*toterrs) <= 3L)
            putmsg(MERR, fn, "Value 0x%wx too large for VSL", (EPI_HUGEUINT)v);
          continue;                             /* wtf just stumble on */
        }

      for (i = size; i >= 0; i--, v >>= 8)      /* qty in msb->lsb order */
        bp[i] = (byte) (v & 0xff);

      *bp |= (size << 6);                       /* or type into first byte */
      bp += (size + 1);
    }
  return((size_t)(bp - buf));
}

int
vsltolocs(bp, sz, array)
byte    *bp;
size_t  sz;
dword   *array;
/* Decodes a list of DELTA VSLs `bp', `sz' bytes long, into (dword)
 * positions.  Assumes that `array' is large enough to hold the
 * results (e.g. should be `sz' elements big).  This is the decode
 * function for a linkstovsl() encode, though we decode into an array
 * of positions rather than the original linked list.  Returns number
 * of positions decoded (up to `sz').  Does not modify `bp' array
 * (could be shared buffer).  NOTE: Expects extra VSL_POSTSZ bytes
 * at end of `bp' array (past `sz'): can be garbage, but should be
 * readable: this is for truncation check.
 */
{
  static CONST char     fn[] = "vsltolocs";
  short                 nbytes;
  dword                 lastarray = 0, *arrayinit;
  byte                  *end;

  for (end = bp + sz, arrayinit = array; bp < end; array++)
    {
      nbytes = (*bp >> 6);              /* find out how big it is */
      *array = ((dword)(*(bp++) & 0x3F) << (nbytes << 3));
      switch (nbytes)                   /* optimization: unroll the loop */
        {
        case 3:  *array += ((dword)(*(bp++)) << 16);
        case 2:  *array += ((dword)(*(bp++)) << 8);
        case 1:  *array +=  (dword)(*(bp++));
        }
      *array += lastarray;
      lastarray = *array;
    }
  if (bp > end)
    {
      putmsg(MERR, fn, "Truncated VSL data");
      if (array > arrayinit) array--;   /* last one was bad */
    }
  return(array - arrayinit);
}

int
countvsl(bp, sz)
byte    *bp;
size_t  sz;
/* Returns number of VSL positions in `bp' (up to `sz').
 */
{
  static CONST char     fn[] = "countvsl";
  short                 nbytes;
  int                   n;
  byte                  *end;

  for (end = bp + sz, n = 0; bp < end; n++)
    {
      nbytes = (*bp >> 6) + 1;          /* find out how big it is */
      if (bp + nbytes > end)
        {
          putmsg(MERR, fn, "Truncated VSL data");
          break;
        }
      bp += nbytes;
    }
  return(n);
}

/*  7 6 5 4 3 2 1 0       bits   max                       7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+                                      +-+-+-+-+-+-+-+-+
 * |0 0 . . . . . .|        6    0x3F          63     X = |. . . . . . . .|
 * +-+-+-+-+-+-+-+-+                                      +-+-+-+-+-+-+-+-+
 * |0 1 . . . . . .| 1X    14    0x3FFF        16,383
 * +-+-+-+-+-+-+-+-+
 * |1 0 . . . . . .| 2X    22    0x3FFFFF           4.1M
 * +-+-+-+-+-+-+-+-+
 * |1 1 0 0 . . . .| 3X    28    0xFFFFFFF          268M
 * +-+-+-+-+-+-+-+-+
 * |1 1 0 1 . . . .| 5X    44    0xFFFFFFFFFFF      17.5 trillion
 * +-+-+-+-+-+-+-+-+
 * |1 1 1 0 . . . .| 7X    60    0xFFFFFFFFFFFFFFF  1.15 quintillion
 * +-+-+-+-+-+-+-+-+
 * |1 1 1 1 . . . .| reserved for future use
 * +-+-+-+-+-+-+-+-+
 */

#ifdef USE_BYTE_MASK
#  define AFF   & 0xff
#else
#  define AFF
#endif
#define VSH1MAX ((EPI_HUGEUINT)0x3FL)
#define VSH2MAX ((EPI_HUGEUINT)0x3FFFL)
#define VSH3MAX ((EPI_HUGEUINT)0x3FFFFFL)
#define VSH4MAX ((EPI_HUGEUINT)0x0FFFFFFFL)
#if defined(EPI_HAVE_LONG_LONG) && EPI_OS_LONG_BITS < 64
#  define VSH6MAX ((EPI_HUGEUINT)0x0FFFFFFFFFFFLL)          /* >32-bit value */
#  define VSH8MAX ((EPI_HUGEUINT)0x0FFFFFFFFFFFFFFFLL)      /* >32-bit value */
#else
#  define VSH6MAX ((EPI_HUGEUINT)0x0FFFFFFFFFFFL)           /* >32-bit value */
#  define VSH8MAX ((EPI_HUGEUINT)0x0FFFFFFFFFFFFFFFL)       /* >32-bit value */
#endif

byte *
outvsh(d, n)
byte            *d;
EPI_HUGEUINT    n;
/* Outputs VSH (Variable Sized Huge) value `n' to `d'.  n < (1 << 60).
 * Values are stored MSB first.  Returns pointer to next byte after
 * data; 0 bytes used if error.  NOTE: WTIX compare funcs
 * (fheap_deletetop_wtix etc.) assume MSB ordering, and that 2 VSHs
 * compare the same with memcmp() as their original numeric values do.
 * NOTE: see also outvsh7().
 */
{
  byte  *s;

  if (n <= VSH1MAX)                             /* 00... */
    {
      *(d++) = (byte)(n AFF);
    }
  else if (n <= VSH2MAX)                        /* 01... */
    {
      *(d++) = (byte)(((unsigned)n >> 8) | 0x40);
      *(d++) = (byte)(n AFF);
    }
  else if (n <= VSH3MAX)                        /* 10... */
    {
      *(d++) = (byte)(((ulong)n >> 16) | 0x80);
      *(d++) = (byte)(((unsigned)n >> 8) AFF);
      *(d++) = (byte)(n AFF);
    }
  else if (n <= VSH4MAX)                        /* 1100... */
    {
      *(d++) = (byte)((n >> 24) | 0xC0);
      *(d++) = (byte)(((ulong)n >> 16) AFF);
      *(d++) = (byte)(((unsigned)n >> 8) AFF);
      *(d++) = (byte)(n AFF);
    }
  else
#if EPI_HUGEUINT_BITS > 32                           /* only check if >32-bit val*/
    if (n <= VSH6MAX)                           /* 1101... */
#endif
    {
      for (s = d, d += 5; d > s; n >>= 8)
        *(d--) = (byte)(n AFF);
      *d = (byte)(n AFF) | 0xD0;
      d += 6;
    }
#if EPI_HUGEUINT_BITS > 32
  else if (n <= VSH8MAX)                        /* 1110... */
    {
      for (s = d, d += 7; d > s; n >>= 8)
        *(d--) = (byte)(n AFF);
      *d = (byte)(n AFF) | 0xE0;
      d += 8;
    }
  else                                          /* 111... reserved */
    {
      putmsg(MERR, "outvsh", "Value too large for VSH (0x%wx)", n);
    }
#endif /* EPI_HUGEUINT_BITS > 32 */
  return(d);
}

byte *
invsh(s, np)
byte            *s;
EPI_HUGEUINT    *np;
/* Reads VSH value from `s' into `*np'.  Returns pointer to next byte
 * after data.
 */
{
  EPI_HUGEUINT  n;

  INVSH(s, n, goto err);
  *np = n;
  return(s);
err:
  putmsg(MERR, "invsh", InvalidVSH);
  *np = (EPI_HUGEUINT)0;
  return(s + 1);
}

/* ---------------------------- 7-bit VSH/VSL ------------------------------ */

size_t
linkstovsh7(klocs, firstkloc, buf, np, toterrs)
dword   *klocs;
dword   firstkloc;
byte    *buf;
size_t  *np;
long    *toterrs;
/* Same as linkstovsl(), but using 7-bit VSHs; high bit is clear in
 * every byte.
 */
{
  dword         loc, oloc = 0, v, av;
  byte          *bp = buf;
  size_t        n = 0;

  (void)toterrs;
  loc = firstkloc;
  do
    {
#ifdef WTIX_UNIQUE_LOCS
      v = loc - oloc;
#else /* !WTIX_UNIQUE_LOCS */
      av = klocs[loc];
      v = av - oloc;
#endif /* !WTIX_UNIQUE_LOCS */
      bp = outvsh7(bp, (EPI_HUGEUINT)v);    /* no err chk; VSH7 handles >32 bits*/
#ifdef WTIX_UNIQUE_LOCS
      oloc = loc;
      loc = klocs[loc];
#else /* !WTIX_UNIQUE_LOCS */
      oloc = av;
      loc = klocs[loc + 1];
#endif /* !WTIX_UNIQUE_LOCS */
      n++;
    }
  while (loc != 0);

  *np = n;
  return((size_t)(bp - buf));
}

size_t
locstovsh7(locs, nlocs, buf, toterrs)
dword   *locs;
size_t  nlocs;
byte    *buf;
long    *toterrs;
/* Same as locstovsl(), but using 7-bit VSHs; high bit is clear in
 * every byte.
 */
{
  dword v, *e;
  byte  *bp;

  (void)toterrs;
  for (bp = buf, v = 0, e = locs + nlocs; locs < e; v = *(locs++))
    {
      v = *locs - v;
      bp = outvsh7(bp, (EPI_HUGEUINT)v);    /* no err chk; VSH7 handles >32 bits*/
    }
  return((size_t)(bp - buf));
}

int
vsh7tolocs(bp, sz, array)
byte    *bp;
size_t  sz;
dword   *array;
/* Same as vsltolocs(), but for 7-bit VSHs.  NOTE: high bit MUST be clear
 * in every byte (uses ..._NOHI version).
 */
{
  static CONST char     fn[] = "vsh7tolocs";
  dword                 lastarray = 0, *arrayinit;
  byte                  *end;
  EPI_HUGEUINT          u;

  for (end = bp + sz, arrayinit = array; bp < end; array++)
    {
      INVSH7_HICLR(bp, u, (putmsg(MERR, fn, InvalidVSH7), u = 0, bp++));
      *array = (dword)u + lastarray;
      lastarray = *array;
    }
  if (bp > end)
    {
      putmsg(MERR, fn, "Truncated VSH7 array data");
      if (array > arrayinit) array--;   /* last one was bad */
    }
  return(array - arrayinit);
}

int
countvsh7(bp, sz)
byte    *bp;
size_t  sz;
/* Same as countvsl(), but for 7-bit VSHs.  NOTE: high bit MUST be clear
 * in every byte (uses ..._NOHI version).
 */
{
  static CONST char     fn[] = "countvsh7";
  int                   n;
  byte                  *end;

  for (end = bp + sz, n = 0; bp < end; n++)
    {
      SKIPVSH7(bp, goto err);
    }
  if (bp > end)
    {
      putmsg(MERR, fn, "Truncated VSH7 array data");
      n--;
    }
  return(n);
err:
  putmsg(MERR, fn, InvalidVSH7);
  return(n);
}

/* 7-bit VSH: don't use the hi bit of any byte, reserved for a user flag.
 *
 *  7 6 5 4 3 2 1 0    	bits  max                           7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+                                       +-+-+-+-+-+-+-+-+
 * |? 0 . . . . . .|   	   6  0x3F              63     X = |? . . . . . . .|
 * +-+-+-+-+-+-+-+-+   	                                   +-+-+-+-+-+-+-+-+
 * |? 1 0 . . . . .| 1X	  12  0xFFF             4,095
 * +-+-+-+-+-+-+-+-+
 * |? 1 1 0 . . . .| 2X	  18  0x3FFFF           262,143
 * +-+-+-+-+-+-+-+-+
 * |? 1 1 1 0 . . .| 3X	  24  0xFFFFFF          16M
 * +-+-+-+-+-+-+-+-+
 * |? 1 1 1 1 0 . .| 4X	  30  0x3FFFFFFF        1.0G
 * +-+-+-+-+-+-+-+-+
 * |? 1 1 1 1 1 0 .| 5X	  36  0xFFFFFFFFF       68.7G
 * +-+-+-+-+-+-+-+-+
 * |? 1 1 1 1 1 1 0| 6X   42  0x3FFFFFFFFFF     4.39 trillion
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |? 1 1 1 1 1 1 1|? 0 . . . . . .|     reserved for future use
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define VSH7_1MAX       ((EPI_HUGEUINT)0x3FL)
#define VSH7_2MAX       ((EPI_HUGEUINT)0x0FFFL)
#define VSH7_3MAX       ((EPI_HUGEUINT)0x3FFFFL)
#define VSH7_4MAX       ((EPI_HUGEUINT)0x0FFFFFFL)
#define VSH7_5MAX       ((EPI_HUGEUINT)0x3FFFFFFFL)
#if defined(EPI_HAVE_LONG_LONG) && EPI_OS_LONG_BITS < 64
#  define VSH7_6MAX     ((EPI_HUGEUINT)0x0FFFFFFFFFLL)      /* >32-bit value */
#  define VSH7_7MAX     ((EPI_HUGEUINT)0x3FFFFFFFFFFLL)     /* >32-bit value */
#else
#  define VSH7_6MAX     ((EPI_HUGEUINT)0x0FFFFFFFFFL)       /* >32-bit value */
#  define VSH7_7MAX     ((EPI_HUGEUINT)0x3FFFFFFFFFFL)      /* >32-bit value */
#endif

byte *
outvsh7(d, n)
byte            *d;
EPI_HUGEUINT    n;
/* Outputs VSH7 (Variable Sized Huge 7-bit) value `n' to `d'.
 * n < (1 << 42).  Values are stored MSB first.  Returns pointer to
 * next byte after data; 0 bytes used if error.  NOTE: WTIX compare
 * funcs (fheap_deletetop_wtix() etc.) assume MSB ordering, and that 2
 * VSHs compare the same with memcmp() as their original numeric
 * values do.  NOTE: all bytes in the output will have the high bit 0,
 * and it is not significant for decoding.
 */
{
  if (n <= VSH7_1MAX)                           /* ?0... */
    {
      *(d++) = (byte)(n AFF);
    }
  else if (n <= VSH7_2MAX)                      /* ?10... */
    {
      *(d++) = (byte)(((unsigned)n >> 7) | 0x40);
      *(d++) = (byte)(n & 0x7F);
    }
  else if (n <= VSH7_3MAX)                      /* ?110... */
    {
      *(d++) = (byte)(((ulong)n >> 14) | 0x60);
      *(d++) = (byte)(((unsigned)n >> 7) & 0x7F);
      *(d++) = (byte)(n & 0x7F);
    }
  else if (n <= VSH7_4MAX)                      /* ?1110... */
    {
      *(d++) = (byte)((n >> 21) | 0x70);
      *(d++) = (byte)(((ulong)n >> 14) & 0x7F);
      *(d++) = (byte)(((unsigned)n >> 7) & 0x7F);
      *(d++) = (byte)(n & 0x7F);
    }
  else if (n <= VSH7_5MAX)                      /* ?11110.. */
    {
      *(d++) = (byte)((n >> 28) | 0x78);
      *(d++) = (byte)(((ulong)n >> 21) & 0x7F);
      *(d++) = (byte)(((ulong)n >> 14) & 0x7F);
      *(d++) = (byte)(((unsigned)n >> 7) & 0x7F);
      *(d++) = (byte)(n & 0x7F);
    }
  else
#if EPI_HUGEUINT_BITS > 32                           /* only check if >32-bit val*/
    if (n <= VSH7_6MAX)                         /* ?111110... */
#endif
    {
#if EPI_HUGEUINT_BITS > 32
      *(d++) = (byte)((n >> 35) | 0x7C);
#else
      *(d++) = (byte)0x7C;
#endif
      *(d++) = (byte)((n >> 28) & 0x7F);
      *(d++) = (byte)(((ulong)n >> 21) & 0x7F);
      *(d++) = (byte)(((ulong)n >> 14) & 0x7F);
      *(d++) = (byte)(((unsigned)n >> 7) & 0x7F);
      *(d++) = (byte)(n & 0x7F);
    }
#if EPI_HUGEUINT_BITS > 32
  else if (n <= VSH7_7MAX)                      /* ?1111110... */
    {
      *(d++) = (byte)(0x7E);
      *(d++) = (byte)((n >> 35) & 0x7F);
      *(d++) = (byte)((n >> 28) & 0x7F);
      *(d++) = (byte)(((ulong)n >> 21) & 0x7F);
      *(d++) = (byte)(((ulong)n >> 14) & 0x7F);
      *(d++) = (byte)(((unsigned)n >> 7) & 0x7F);
      *(d++) = (byte)(n & 0x7F);
    }
  else
    {
      putmsg(MERR, "outvsh7", "Value too large for VSH7 (0x%wx)", n);
    }
#endif /* EPI_HUGEUINT_BITS > 32 */
  return(d);
}

byte *
invsh7(s, np)
byte            *s;
EPI_HUGEUINT    *np;
/* Reads VSH7 value from `s' into `*np'.  Returns pointer to next byte
 * after data.
 */
{
  EPI_HUGEUINT  n;

  INVSH7(s, n, goto err);
  *np = n;
  return(s);
err:
  putmsg(MERR, "invsh7", InvalidVSH7);
  *np = (EPI_HUGEUINT)0;
  return(s + 1);
}

/* ------------------------------------------------------------------------- */

int
fdbi_exists(name)
char    *name;
/* Returns nonzero if FDBI index `name' exists.
 */
{
  return(existsbtree(name));
}

static int fdbi_get_heapcmp ARGS((void *a, void *b, void *usr));
static int
fdbi_get_heapcmp(a, b, usr)
void    *a, *b, *usr;
/* Heap comparison function, used in fdbi_get() token merge for
 * Metamorph non-inverted indexes (INDEX_MM).  Sorts by token, ignores
 * hit offset (not present).  Assumes all sets have a hit.
 * Also used for NOT-set heap (inverted and non-inverted).
 */
{
  (void)usr;
  return(TXrecidcmp(&((FDBIS *)a)->hip->loc, &((FDBIS *)b)->hip->loc));
}

static int fdbi_get_heapcmp_full ARGS((void *a, void *b, void *usr));
static int
fdbi_get_heapcmp_full(a, b, usr)
void    *a, *b, *usr;
/* Heap comparison function, used in fdbi_get() token merge for
 * inverted (INDEX_FULL) indexes.  Sorts by token, then hit offset,
 * then hit length.  Assumes all sets have a hit.
 */
{
  int           cmp;
  FDBIHI        *hitInfoA, *hitInfoB;

  (void)usr;
  cmp = TXrecidcmp(&((FDBIS *)a)->hip->loc, &((FDBIS *)b)->hip->loc);
  if (cmp != 0) return(cmp);
  hitInfoA = ((FDBIS *)a)->hip;
  hitInfoB = ((FDBIS *)b)->hip;
  cmp = hitInfoA->hits[hitInfoA->curHit] - hitInfoB->hits[hitInfoB->curHit];
  if (cmp != 0) return(cmp);
  cmp = (hitInfoA->hitLens != DWORDPN ?
         hitInfoA->hitLens[hitInfoA->curHit] : 1) -
    (hitInfoB->hitLens != DWORDPN ? hitInfoB->hitLens[hitInfoB->curHit] : 1);
  return(cmp);
}

static int CDECL cmphit_allmatch ARGS((CONST void *a, CONST void *b));
static int CDECL
cmphit_allmatch(a,b)
CONST void *a;
CONST void *b;
/* Sort callback, when all sets required.  Sorts by hit offset (tokens
 * already the same.), then hit length.
 * *NOTE* also hard-coded into selsort():
 */
{
  int           cmp;
  FDBIHI        *hitInfoA = *(FDBIHI **)a, *hitInfoB = *(FDBIHI **)b;

  cmp = (hitInfoA->hits[hitInfoA->curHit] -
         hitInfoB->hits[hitInfoB->curHit]);
  if (cmp != 0) return(cmp);
  cmp = (hitInfoA->hitLens != DWORDPN ?
         hitInfoA->hitLens[hitInfoA->curHit] : 1) -
    (hitInfoB->hitLens != DWORDPN ? hitInfoB->hitLens[hitInfoB->curHit] : 1);
  return(cmp);
}

static int CDECL cmpset_initial ARGS((CONST void *a, CONST void *b));
static int CDECL
cmpset_initial(a,b)
CONST void *a;
CONST void *b;
/* Sort callback for initial sorting of sets, for allmatch.
 * Sorts all AND/SET sets before NOTs (NOTE: allmatch search assumption).
 * Secondary sort is least-frequent-first (optimization).
 */
{
  int   rc;

  rc = (((*(FDBIS **)a)->set->logic == LOGINOT) -
        ((*(FDBIS **)b)->set->logic == LOGINOT));
  if (rc) return(rc);
  if ((*(FDBIS **)a)->wi.ndocs < (*(FDBIS **)b)->wi.ndocs) return(-1);
  if ((*(FDBIS **)a)->wi.ndocs > (*(FDBIS **)b)->wi.ndocs) return(1);
  return(0);
}

static int fdbi_rlexstripexclude ARGS((RLEX *rl));
static int
fdbi_rlexstripexclude(rl)
RLEX    *rl;
/* Modifies open REX expressions in `rl' by removing previous/followed-by
 * subexpressions (\P and \F).  We need to do this so the index expressions
 * still match a word alone in a buffer, when we check them in openfdbif():
 * the search buf will be the word alone, not including the discarded
 * \P or \F text in the original indexed text.  WTF WTF this should be in
 * rex.c and rexlex.c.  Returns number of expressions left (0 if all closed).
 */
{
  FFS   *rex, *fs, *next, *prev, *first, *last;
  int   i;

  for (i = 0; i < rl->n; i++)                           /* each index expr. */
    {
      if (rl->ilst[i].ex == TX_FFS_NO_MATCH) continue;
      last = lastexp(rl->ilst[i].ex);
      rex = first = firstexp(rl->ilst[i].ex);
      for ( ; rex != FFSPN && rex->exclude < 0; rex = next)     /* del \P */
        {
          next = rex->next;
          if (next == FFSPN)                            /* no more non-\P */
            {
              rex = closerex(rex);
              goto zap;
            }
          else
            {
              /* Have to make the root subexpression someone else.
               * WTF hope this is ok:
               */
              if (rex->root) next->root = rex->root;
              rex = closefpm(rex);
              next->prev = FFSPN;
              first = next;
            }
        }
      fs = lastexp(rex);
      for ( ; fs != FFSPN && fs->exclude > 0; fs = prev)        /* del \F */
        {
          prev = fs->prev;
          if (prev == FFSPN)                            /* no more non-\F */
            {
              rex = closerex(rex);
              goto zap;
            }
          else
            {
              if (fs->root) prev->root = fs->root;      /* pass the buck */
              fs = closefpm(fs);
              prev->next = FFSPN;
              last = prev;
            }
        }
      for ( ; rex != FFSPN && !rex->root; rex = rex->next);     /* re-root */
      for (fs = first; fs; fs = fs->next)       /* fix up first/last ptrs */
        {
          fs->first = first;
          fs->last = last;
        }
    zap:
      rl->ilst[i].ex = rex;
      if (rex == FFSPN)                                 /* no subexpr. left */
        {
          if (i + 1 < rl->n)
            memmove(rl->ilst + i, rl->ilst + i + 1,
                    ((rl->n - i) - 1)*sizeof(rl->ilst[0]));
          i--;
          rl->n--;
        }
    }
  return(rl->n);
}

RECID
TXfdbiGetRecidAndAuxData(fi, tok, recidPtr, auxfld)
FDBI            *fi;
EPI_OFF_T       tok;            /* (in) Metamorph token number to look up */
void            **recidPtr;     /* (out, opt.) location of recid */
void            **auxfld;       /* (out, opt.) location of aux data */
/* Returns recid for token `tok', or RECID_INVALID on error.  Sets
 * `*recidPtr' to recid location.  Sets `*auxfld' to auxfld data if
 * fi->auxfldsz != 0, else NULL.  Note that `*recidPtr'/`*auxfld' is
 * valid only until next call here or fdbi_updatetokaux().  See also
 * fdbi_updatetokaux().
 */
{
  static CONST char     fn[] = "TXfdbiGetRecidAndAuxData";
  EPI_OFF_T             off, set;
  RECID                 recid;
  int                   rd;
  byte                  *s;

  if (tok >= fi->tokbufstart && tok < fi->tokbufend)
    {
    ok:
      s = fi->tokbuf + fi->tokelsz*(size_t)(tok - fi->tokbufstart);
      if (recidPtr) *recidPtr = s;
      if (auxfld != NULL)
        {
          if (fi->auxfldsz > 0)
            *auxfld = s + FDBI_TOKEL_RECIDSZ;
          else
            *auxfld = NULL;
        }
      FDBI_TXALIGN_RECID_COPY(&recid, s);
      return(recid);
    }

  /* This token is outside our current buffer, so read a new chunk in
   * from the token file.  Try to align it so the token occurs at
   * buffer start; since future calls will most likely be increasing
   * tokens, there's a better chance they'll be in the buffer too:
   */
  if (fi->tokbufismmap) goto bad;       /* we memmap'd the whole file */
  off = (tok - (EPI_OFF_T)1)*(EPI_OFF_T)fi->tokelsz;
  if (off >= fi->tokfilesz || off < (EPI_OFF_T)0) goto bad;
  /* If the buffer would extend past EOF, back off a little so there's
   * more in the buffer:
   */
  if (off + (EPI_OFF_T)fi->tokbufrdsz > fi->tokfilesz)
    {
      off = fi->tokfilesz - (EPI_OFF_T)fi->tokbufrdsz;
      if (off < (EPI_OFF_T)0) off = (EPI_OFF_T)0;
    }
  errno = 0;
  if ((set = EPI_LSEEK(fi->tokfh, off, SEEK_SET)) != off)
    {
      putmsg(MERR + FSE, fn, "Cannot lseek to 0x%wx in %s: %s",
             (EPI_HUGEINT)off, fi->tokfn, strerror(errno));
      goto err;
    }
  rd = tx_rawread(TXPMBUFPN, fi->tokfh, fi->tokfn, fi->tokbuf, fi->tokbufrdsz,
                  0);
  fi->tokbufstart = (EPI_OFF_T)1 + off/(EPI_OFF_T)fi->tokelsz;
  fi->tokbufend = fi->tokbufstart + (EPI_OFF_T)((size_t)rd/fi->tokelsz);
  if (tok >= fi->tokbufstart && tok < fi->tokbufend) goto ok;
bad:
  putmsg(MERR, fn, "Invalid token 0x%wx for Metamorph index token file %s",
         (EPI_HUGEINT)tok, fi->tokfn);
err:
  if (auxfld != NULL) *auxfld = NULL;
  if (recidPtr) *recidPtr = NULL;
  TXsetrecid(&recid, RECID_INVALID);
  return(recid);
}

FDBI *
openfdbi(name, mode, flags, sysindexParams, dbtbl)
char            *name;
int             mode;                   /* PM_... flags */
FDF             flags;
CONST char      *sysindexParams;        /* (in) SYSINDEX.PARAMS field */
DBTBL           *dbtbl;
{
  static CONST char     fn[] = "openfdbi";
  FDBI                  *fi, *f;
  char                  *localedup = CHARPN, path[PATH_MAX];
  CONST char            *locale;
  EPI_STAT_S            st;
  int                   sav, sl;
#ifdef EPI_HAVE_MMAP
  FDBI		        *x;
  byte                  *buf;
  int		        fd;
#endif

  if ((fi = (FDBI *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(FDBI))) == FDBIPN)
    goto err;
  if (FdbiTraceIdx >= 3)
    putmsg(MINFO, CHARPN, "openfdbi(%s, %s%s, %s, %s) = %p",
           name, ((mode & PM_SELECT) ? "R" : ""),
           ((mode & (PM_ALTER|PM_DELETE|PM_INSERT|PM_UPDATE)) ? "W" : ""),
           ((flags & FDF_FULL) ? "F" : "M"), sysindexParams, (void *)fi);
  fi->tokfh = -1;
  fi->flags = (flags & FDF_PUBMASK);
  fi->mode = mode;
  fi->dbtbl = dbtbl;
  if ((fi->dbi = open3dbi(name, mode, INDEX_FULL, sysindexParams)) == A3DBIPN)
    goto err;
  if ((int)fi->dbi->version >= 3)
    {
      fi->invsh = invsh7;
      fi->outvsh = outvsh7;
      fi->vsltolocs = vsh7tolocs;
      fi->countvsl = countvsh7;
    }
  else
    {
      fi->invsh = invsh;
      fi->outvsh = outvsh;
      fi->vsltolocs = vsltolocs;
      fi->countvsl = countvsl;
    }
  /* Check the version number; if it's greater than the current version
   * we might not be compatible with it.
   */
  if ((int)fi->dbi->version > FDBI_MAX_VERSION)
    {
      putmsg(MERR + UGE, CHARPN,
             "Index %s is version %d (expected %d or earlier): Incompatible, use later Texis release",
             name, (int)fi->dbi->version, FDBI_MAX_VERSION);
      goto err;
    }
  if (fi->dbi->auxsz > 0)                       /* auxfld data in token file*/
    {
      fi->auxfldsz = fi->dbi->auxsz;
      fi->tokelsz = FDBI_TOKEL_RECIDSZ + TX_ALIGN_UP_SIZE_T(fi->auxfldsz);
    }
  else
    {
      fi->auxfldsz = 0;
      fi->tokelsz = sizeof(RECID);
    }

  /* Get and check the textsearchmode: if it is an unknown mode, we might
   * not be compatible with it (i.e. unknown dictionary sort function).
   * Note that the error should already have been reported by dbiparams(),
   * but it may not have failed the index open at that point:
   */
  fi->textsearchmode = fi->dbi->textsearchmode;
  if (!TXCFF_IS_VALID_MODEFLAGS(fi->textsearchmode)) goto err;

  if (fi->mode & PM_SELECT)                     /* only open what's needed */
    {
      if ((fi->pgbuf = (byte *)TXmalloc(TXPMBUFPN, fn, BT_REALMAXPGSZ +
                                        VSL_POSTSZ)) == BYTEPN)
        goto err;
      fi->pgbufsz = BT_REALMAXPGSZ;
      if ((fi->bt = openbtree(name, FDBI_BTPGSZ, 10, 0, O_RDONLY)) == BTREEPN)
        goto err;
      btsetcmp(fi->bt, fdbi_btcmp);
      if (!TXcatpath(path, name, FDBI_DATSUF)) goto err;
      sav = TxKdbfQuickOpen;
      TxKdbfQuickOpen = 1;                      /* we know it's KDBF */
      fi->datdf = kdbf_open((dbtbl && dbtbl->ddic ? dbtbl->ddic->pmbuf :
                             TXPMBUFPN), path, O_RDONLY);
      TxKdbfQuickOpen = sav;
      if (fi->datdf == KDBFPN) goto err;
      if (fi->dbi->explist == CHARPPN)          /* no index expressions? */
        {
          /* Only a warning, because we can live without ixrlex: */
          putmsg(MWARN, fn, "Index expression list missing for %s", name);
        }
      else
        {
          sl = 0;
          if (fi->dbi->locale != CHARPN &&      /* set index locale for REX */
              strcmp(fi->dbi->locale, (locale = TXgetlocale())) != 0)
            {
              if ((localedup = TXstrdup(TXPMBUFPN, fn, locale)) == CHARPN)
                goto err;
              if (TXsetlocale(fi->dbi->locale) == CHARPN)
                putmsg(MWARN, fn, "Could not set locale %s for index %s",
                       fi->dbi->locale, name);
              else
                sl = 1;
            }
          if (!(fi->ixrlex = openrlex((const char **)fi->dbi->explist,
                                      TXrexSyntax_Rex)))
            putmsg(MWARN, fn,
                "Could not open RLEX for index expression list for %s", name);
          /* Restore locale now, because we may be open for a while
           * and don't want to affect user's locale.  Downside is if
           * we need index locale to affect other functions later:
           */
          if (sl)
            {
              if (TXsetlocale(localedup) == CHARPN)
                putmsg(MWARN, fn, "Could not restore locale %s for index %s",
                       localedup, name);
            }
        }
      if (!fdbi_rlexstripexclude(fi->ixrlex))
        fi->ixrlex = closerlex(fi->ixrlex);
      if ((fi->noise = fi->dbi->noiselist) == CHARPPN)
        fi->isnoise = isnoise_dum;
      else
        {
          fi->isnoise = isnoise_lin;
          if ((fi->nnoise = inorder(fi->noise)) > 0) /* wtf assume in-order?*/
            fi->isnoise = isnoise_bin;
        }
      if ((fi->fgfh = openfheap((fi->flags&FDF_FULL) ? fdbi_get_heapcmp_full :
                                fdbi_get_heapcmp, NULL, 0)) == FHEAPPN)
        goto err;
    }
  if (!TXcatpath(path, name, FDBI_TOKSUF)) goto err;
  if (fi->mode & (PM_ALTER | PM_DELETE | PM_INSERT | PM_UPDATE))
    {                                           /* may need to modify index */
      fi->tokfh = TXrawOpen(TXPMBUF_SUPPRESS, __FUNCTION__,
                            "Metamorph index token file", path,
                            TXrawOpenFlag_None,
                            (O_RDWR | TX_O_BINARY), 0666);
      if (fi->tokfh < 0) goto tryrd;
      fi->flags |= FDF_TOKFHWR;
    }
  else
    {
    tryrd:
      fi->tokfh = TXrawOpen(TXPMBUFPN, __FUNCTION__,
                            "Metamorph index token file", path,
                            TXrawOpenFlag_None,
                            (O_RDONLY | TX_O_BINARY), 0666);
      if (fi->tokfh < 0) goto err;
    }
  if ((fi->tokfn = TXstrdup(TXPMBUFPN, fn, path)) == CHARPN) goto err;
  errno = 0;
  if (EPI_FSTAT(fi->tokfh, &st) != 0)
    {
      putmsg(MERR + FTE, fn, "Cannot fstat %s: %s",fi->tokfn,strerror(errno));
      goto err;
    }
  fi->tokdev = (int)st.st_dev;
  fi->tokino = (int)st.st_ino;
  fi->totrecs = ((EPI_OFF_T)st.st_size)/(EPI_OFF_T)fi->tokelsz;
  fi->tokfilesz = fi->totrecs*(EPI_OFF_T)fi->tokelsz;
  fi->tokbufrdsz = (FdbiReadBufSz/fi->tokelsz)*fi->tokelsz;
  if (fi->tokbufrdsz < fi->tokelsz) fi->tokbufrdsz = fi->tokelsz;

  /* Find a guy in the list who's mmap()ed the same token file.  Then
   * when we try to mmap() it we can share the buffer.  This not only
   * saves mem, but is required for certain old Linux kernels where
   * EPI_MMAP_SHARED_IS_PRIVATE is set; otherwise modifications to the
   * token file won't be seen by other mmap()s of the same file: KNG 000906
   */
  for (f = FdbiList; f != FDBIPN; f = f->next)
    {
      if (
#ifndef _WIN32                                  /* no dev/inode in Windows */
          fi->tokino == f->tokino &&            /* optimization */
          fi->tokdev == f->tokdev &&
#endif /* !_WIN32 */
          f->tokbufismmap &&
          f->tokfilesz == fi->tokfilesz &&
          strcmp(fi->tokfn, f->tokfn) == 0)
        break;
    }

#ifdef EPI_HAVE_MMAP
  if ((EPI_HUGEUINT)fi->tokfilesz <= (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX && (TxIndexMmap & 1))
    {
      if (
#  ifdef FDBI_NO_MMAP_WRITE
          0
#  else
          fi->mode & (PM_ALTER | PM_DELETE | PM_INSERT | PM_UPDATE)
#  endif
         )
        {                                       /* may need to modify index */
          if (f != FDBIPN)                      /* share this guy's buffer */
            {
              if (!(f->flags & FDF_TOKMEMWR))   /* he's read-only: remap RW */
                { /* wtf use mprotect() to avoid munmap(), but doesn't work */
                  munmap((caddr_t)f->tokbuf, (size_t)f->tokfilesz);
                  errno = 0;
                  if ((void *)mmap((caddr_t)f->tokbuf, (size_t)f->tokfilesz,
                           (PROT_READ | PROT_WRITE), (MAP_SHARED | MAP_FIXED),
                           fi->tokfh, (EPI_OFF_T)0) == (void *)MAP_FAILED)
                    {                           /* try again */
                      errno = 0;
                      if ((buf = (byte *)mmap(NULL, (size_t)f->tokfilesz,
                           (PROT_READ | PROT_WRITE), MAP_SHARED,
                           fi->tokfh, (EPI_OFF_T)0)) != (byte *)MAP_FAILED)
                        goto okflip;            /* worked this time */
                      /* We should probably always warn that R->RW failed,
                       * especially if EPI_MMAP_SHARED_IS_PRIVATE.  However
                       * this seems to fail only in that circumstance anyway.
                       * And it also seems to avert the core problem anyway,
                       * in that re-mmap()ing the same buffer (even read-only)
                       * re-reads it from disk.  msync() should solve this
                       * issue too, but again, it's not available if
		       * EPI_MMAP_SHARED_IS_PRIVATE.  KNG 000906
                       */
                      if (FdbiTraceIdx >= 1)
                        putmsg(MWARN, fn,
         "Could not re-mmap() Metamorph index token file %s from R to RW: %s",
                               f->tokfn, strerror(errno));
                      /* some Linuxes won't mmap()-rdonly a wronly-handle: */
                      fd = TXrawOpen(TXPMBUFPN, __FUNCTION__,
                                     "Metamorph index token file", fi->tokfn,
                                     TXrawOpenFlag_None,
                                     (O_RDONLY | TX_O_BINARY), 0666);
                      errno = 0;                /* restore original rdonly */
                      if ((void *)mmap((caddr_t)f->tokbuf,(size_t)f->tokfilesz,
                              PROT_READ, (MAP_SHARED | MAP_FIXED),
                              (fd != -1 ? fd : fi->tokfh), (EPI_OFF_T)0) ==
                          (void *)MAP_FAILED)
                        {                       /* getting desparate... */
                          errno = 0;
                          if ((f->tokbuf = (byte *)mmap(NULL,
                                (size_t)f->tokfilesz, PROT_READ, MAP_SHARED,
                             (fd!=-1?fd:fi->tokfh), (EPI_OFF_T)0)) ==
                              (byte*)(-1))
                            {                   /* dire straits: wtf fileio?*/
                              putmsg(MERR, fn,
                                     "Could not restore mmap() for %s: %s",
                                     f->tokfn, strerror(errno));
                              f->tokbuf = BYTEPN;
                            }
                          for (x = f; x != FDBIPN; x = x->mnext)
                            {
                              if (f->tokbuf == BYTEPN)
                                {
                                  x->tokbufstart = x->tokbufend =(EPI_OFF_T)0;
                                  x->tokbufismmap = 0;
                                }
                              x->tokbuf = f->tokbuf;
                            }
                        }
                      if (fd >= 0) close(fd);
                      goto trymmapwr;
                    }
                  buf = f->tokbuf;
                okflip:
                  for (x = f; x != FDBIPN; x = x->mnext)
                    {                           /* whole sublist is RW now */
                      if (FdbiTraceIdx >= 4)
                        {
                          if (buf != x->tokbuf)
                            htsnpf(path, sizeof(path), " %p", buf);
                          else
                            *path = '\0';
                          putmsg(MINFO, CHARPN,
                        " handle %p: token file buf %p re-mmap()ed R to RW%s",
                                 (void *)x, (void *)x->tokbuf, path);
                        }
                      x->tokbuf = buf;          /* in case it moved */
                      x->flags |= FDF_TOKMEMWR;
                    }
                }
            mlist:
              if (FdbiTraceIdx >= 4)
                putmsg(MINFO, CHARPN,
                       " handle %p: sharing %p's token buf %p",
                       (void *)fi, (void *)f, (void *)f->tokbuf);
              if (f->flags & FDF_TOKMEMWR) fi->flags |= FDF_TOKMEMWR;
              fi->tokbuf = f->tokbuf;
              fi->mnext = f->mnext;             /* insert into sub-list */
              fi->mprev = f;
              if (f->mnext != FDBIPN) f->mnext->mprev = fi;
              f->mnext = fi;
            }
          else                                  /* no buffer to share */
            {
            trymmapwr:
              errno = 0;
              fi->tokbuf = (byte *)mmap(NULL, (size_t)fi->tokfilesz,
                                        (PROT_READ | PROT_WRITE), MAP_SHARED,
                                        fi->tokfh, (EPI_OFF_T)0);
              if (FdbiTraceIdx >= 4)
                {
                  int   sav = errno;
                  putmsg(MINFO, CHARPN, " mmap(%s, 0x0, 0x%wx, RW) = %p",
                         fi->tokfn, (EPI_HUGEINT)fi->tokfilesz,
                         (void *)fi->tokbuf);
                  errno = sav;
                }
              if (fi->tokbuf == (byte*)(-1)) goto trymmaprd;
              fi->flags |= FDF_TOKMEMWR;
            }
        }
      else                                      /* read-only mmap() */
        {
          if (f != FDBIPN) goto mlist;          /* share this guy's buffer */
        trymmaprd:
          errno = 0;
          fi->tokbuf = (byte *)mmap(NULL, (size_t)fi->tokfilesz,
                                    PROT_READ, MAP_SHARED,
                                    fi->tokfh, (EPI_OFF_T)0);
          if (FdbiTraceIdx >= 4)
            {
              int       sav = errno;
              putmsg(MINFO, CHARPN, " mmap(%s, 0x0, 0x%wx, R) = %p",
                     fi->tokfn, (EPI_HUGEINT)fi->tokfilesz, (void *)fi->tokbuf);
              errno = sav;
            }
          if (fi->tokbuf == (byte*)(-1)) goto usefileio;
        }
      fi->tokbufismmap = 1;
      fi->tokbufstart = (EPI_OFF_T)1;
      fi->tokbufend = (EPI_OFF_T)1 + fi->totrecs;
      if (!(fi->mode & (PM_ALTER | PM_DELETE | PM_INSERT | PM_UPDATE)) ||
          (fi->flags & FDF_TOKMEMWR))
        {
          close(fi->tokfh);                     /* save file handles */
          fi->tokfh = -1;
          fi->flags &= ~FDF_TOKFHWR;            /* since it's closed */
          if (FdbiTraceIdx >= 1)
            putmsg(MINFO, fn,"mmap()ing entire Metamorph index token file %s",
                   fi->tokfn);
        }
      else if (FdbiTraceIdx >= 1)
        putmsg(MWARN, fn, "Can only mmap() Metamorph index token file %s read-only; using file I/O for updates",
               fi->tokfn);
    }
  else
#endif /* EPI_HAVE_MMAP */
    {
#ifdef EPI_HAVE_MMAP
    usefileio:
#endif
      /* File I/O token access instead of mmap() can slow searches
       * down a lot, but it's not an error.  Only warn if they're
       * debugging/tracing:
       */
      if (FdbiTraceIdx >= 1)
        putmsg(MWARN, fn,
             "%snot mmap() Metamorph index token file %s: %s; using file I/O",
#ifdef EPI_HAVE_MMAP
               ((EPI_HUGEUINT)fi->tokfilesz > (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX ||
                (TxIndexMmap & 1) ? "Can" : "Will "),
               fi->tokfn,
               ((EPI_HUGEUINT)fi->tokfilesz > (EPI_HUGEUINT)EPI_OS_SIZE_T_MAX ?
  "Too large" : ((TxIndexMmap & 1) ? strerror(errno) : "(indexmmap & 1) off"))
#else /* !EPI_HAVE_MMAP */
               "Can",
               fi->tokfn,
               "EPI_HAVE_MMAP not defined"
#endif /* !EPI_HAVE_MMAP */
               );
      if (!(fi->tokbuf = (byte *)TXmalloc(TXPMBUFPN, fn, fi->tokbufrdsz)))
        goto err;
      fi->tokbufstart = fi->tokbufend = (EPI_OFF_T)0;
    }
  if (fi->datdf != KDBFPN)
#ifdef EPI_HAVE_MMAP
    if (FdbiTraceIdx >= 1)
      {
        if (TxIndexMmap & 2)
          putmsg(MINFO, fn,
      "Partially mmap()ing Metamorph index data file %s: (indexmmap & 2) set",
                 kdbf_getfn(fi->datdf));
        else
          putmsg(MINFO, fn,
                 "Will not mmap() Metamorph index data file %s: (indexmmap & 2) off; using file I/O",
                 kdbf_getfn(fi->datdf));
      }
#else /* !EPI_HAVE_MMAP */
    if (FdbiTraceIdx >= 1)
      putmsg(MINFO, fn,
             "Cannot mmap() Metamorph index data file %s: EPI_HAVE_MMAP not defined; using file I/O",
             kdbf_getfn(fi->datdf));
#endif /* !EPI_HAVE_MMAP */
  if (fi->mprev == FDBIPN)                      /* not linked somewhere yet */
    {
      fi->next = FdbiList;
      if (FdbiList != FDBIPN) FdbiList->prev = fi;
      FdbiList = fi;
    }
  goto done;

err:
  fi = closefdbi(fi);
done:
  localedup = TXfree(localedup);
  return(fi);
}

FDBI *
closefdbi(fi)
FDBI    *fi;
{
  static CONST char     fn[] = "closefdbi";

  if (fi == FDBIPN) goto done;

  if (FdbiTraceIdx >= 3)
    putmsg(MINFO, CHARPN, "closefdbi(%p)", (void *)fi);

  closerppm(fi->rp);                            /* may have open FDBIS */
  closebtree(fi->bt);
  kdbf_close(fi->datdf);
  close3dbi(fi->dbi);
  closerlex(fi->ixrlex);
  fi->pgbuf = TXfree(fi->pgbuf);
  fi->posbuf = TXfree(fi->posbuf);
  closefheap(fi->omfh);
  fi->wordHeap2 = closefheap(fi->wordHeap2);
  closefheap(fi->fgfh);

  if (fi->tokbuf != BYTEPN)
    {
#ifdef EPI_HAVE_MMAP
      if (fi->tokbufismmap)
        {
          if (fi->mprev == FDBIPN && fi->mnext == FDBIPN)
            {                                   /* we're the last user */
              munmap((caddr_t)fi->tokbuf, (size_t)fi->tokfilesz);
              if (FdbiTraceIdx >= 4)
                putmsg(MINFO, CHARPN, " munmap(%s, %p, 0x%wx)",
                       fi->tokfn, (void *)fi->tokbuf, (EPI_HUGEINT)fi->tokfilesz);
            }
        }
      else
#endif /* EPI_HAVE_MMAP */
        fi->tokbuf = TXfree(fi->tokbuf);
    }
  if (fi->tokfh >= 0)
    {
      errno = 0;
      if (close(fi->tokfh) != 0 && (fi->flags & FDF_TOKFHWR))
        {
          putmsg(MERR + FCE, (char *)fn,
                 "Cannot close Metamorph index token file %s: %s",
                 fi->tokfn, strerror(errno));
        }
    }
  fi->tokfn = TXfree(fi->tokfn);
  /* fbstart list elements are owned by FDBIX objects, closed above */

  fi->query = TXfree(fi->query);
  fdbi_clearpostmsg(fi);
  fi->lineardictmsg = TXfree(fi->lineardictmsg);

  if (fi->mnext != FDBIPN)                      /* unlink from lists */
    {
      if (fi->next != FDBIPN) fi->next->prev = fi->mnext;
      if (fi->prev != FDBIPN) fi->prev->next = fi->mnext;
      else if (FdbiList == fi) FdbiList = fi->mnext;
      fi->mnext->next = fi->next;
      fi->mnext->prev = fi->prev;
    }
  else
    {
      if (fi->next != FDBIPN) fi->next->prev = fi->prev;
      if (fi->prev != FDBIPN) fi->prev->next = fi->next;
      else if (FdbiList == fi) FdbiList = fi->next;
    }
  if (fi->mnext != FDBIPN) fi->mnext->mprev = fi->mprev;
  if (fi->mprev != FDBIPN) fi->mprev->mnext = fi->mnext;

  fi = TXfree(fi);

done:
  return(FDBIPN);
}

/* ------------------------------------------------------------------------- */

FDF
TXfdbiGetFlags(fi)
FDBI    *fi;
{
  return(fi->flags & FDF_PUBMASK);
}

char *
TXfdbiGetTokenPath(fi)
FDBI    *fi;
{
  return(fi->tokfn);
}

size_t
TXfdbiGetAuxFieldsSize(fi)
FDBI    *fi;
{
  return(fi->auxfldsz);
}

A3DBI *
fdbi_getdbi(fi)
FDBI    *fi;
{
  return(fi->dbi);
}

EPI_OFF_T
fdbi_getnrecs(fi)
FDBI    *fi;
{
  return(fi->indexcount);
}

EPI_OFF_T
fdbi_gettotrecs(fi)
FDBI    *fi;
{
  return(fi->totrecs);
}

RPPM *
fdbi_getrppm(fi)
FDBI    *fi;
/* Get RPPM struct opened by fdbi_get().  Caller must close it.  Only
 * valid after an fdbi_get() call.
 */
{
  RPPM  *rp;

  rp = fi->rp;
  fi->rp = RPPMPN;
  /* Change `rp' to a non-index version (e.g. as if openrppm(FDBIPN)),
   * because fdbi_get() is done and thus `rp' won't need the index later.
   * More importantly, rp->sets probably has open FDBIS structs that
   * use `fi' (e.g. for FDBIXBUF buffers), and `fi' could be closed before
   * `rp' (e.g. lock update in btfindcache(), especially if SQL statement
   * is using this index twice):   KNG 030505
   */
  if (rp != RPPMPN) rppm_unindex(rp);
  return(rp);
}

int
fdbi_getovermaxwords(fi)
FDBI    *fi;
{
  return(fi->novermaxwords);
}

int
fdbi_updatetokaux(fi, recid, newAux, newRecid)
FDBI    *fi;            /* (in/out) index object */
RECID   recid;          /* (in) RECID to modify */
void    *newAux;        /* (in, opt.) new aux data for `recid' */
RECID   newRecid;       /* (in, opt.) new RECID for `recid' */
/* Updates the token-file aux data for `recid' to `newAux' (assumed to be
 * same size that open3dbi() reported).  NOTE: Assumes write lock
 * obtained on index.  Returns 0 on error, 1 if no such recid in token
 * file, 2 if updated ok.  If `newAux' and `newRecid' are NULL, no update;
 * just searches for `recid'.   If `newRecid' given, changes `recid' to it.
 * >>> NOTE: `newRecid' must have same relative ordering as `recid', <<<
 * >>> i.e. would have same token number.                            <<<
 */
{
  static CONST char     fn[] = "fdbi_updatetokaux";
  EPI_OFF_T             l, r, t;
  RECID                 rec;
  void                  *curaux = NULL, *curRecid = NULL;

  if (!(fi->mode & (PM_ALTER | PM_DELETE | PM_INSERT | PM_UPDATE)))
    {                                           /* sanity check */
      putmsg(MERR + UGE, fn,
    "Internal error: Attempt to update Metamorph index %s opened search-only",
             fi->tokfn);
      goto err;
    }

  l = (EPI_OFF_T)0;                             /* binary search */
  r = fi->totrecs;
  while (l < r)
    {
      t = ((l + r) >> 1);
      rec = TXfdbiGetRecidAndAuxData(fi, t+(EPI_OFF_T)1, &curRecid, &curaux);
      if (!TXrecidvalid2(&rec)) goto err;
      if (TXgetoff2(&recid) < TXgetoff2(&rec)) r = t;
      else if (TXgetoff2(&recid) > TXgetoff2(&rec)) l = t + 1;
      else goto gotit;
    }
  return(1);                                    /* no such recid */

gotit:
  if (newAux == NULL && !TXrecidvalid2(&newRecid))
    goto ok;                                    /* found recid, no update */
  if (fi->tokbufismmap && (fi->flags & FDF_TOKMEMWR))
    {                                           /* direct mmap() update */
      if (TXrecidvalid2(&newRecid))
        memcpy(curRecid, &newRecid, sizeof(RECID));
      if (newAux != NULL)
        memcpy(curaux, newAux, fi->auxfldsz);
      goto ok;
    }
  if (fi->flags & FDF_TOKFHWR)                  /* file I/O update */
    {
      if (!fi->tokbufismmap)                    /* update mem buffer too */
        {
          if (TXrecidvalid2(&newRecid))
            memcpy(curRecid, &newRecid, sizeof(RECID));
          if (newAux != NULL)
            memcpy(curaux, newAux, fi->auxfldsz);
        }
      /* It's inefficient to write to the token file just a few bytes
       * at a time, but we cannot buffer across multiple recids because
       * of concurrency/locks.  Non-mmap'd token search is slow
       * anyway, and should rarely (never?) happen:
       */
      t = t*(EPI_OFF_T)fi->tokelsz + (EPI_OFF_T)FDBI_TOKEL_RECIDSZ;
      errno = 0;
      if (EPI_LSEEK(fi->tokfh, t, SEEK_SET) != t)
        {
          putmsg(MERR + FSE, fn,
                 "Cannot lseek to 0x%wx in Metamorph index token file %s: %s",
                 (EPI_HUGEINT)t, fi->tokfn, strerror(errno));
          goto err;
        }
      if (TXrecidvalid2(&newRecid) &&
          tx_rawwrite(TXPMBUFPN, fi->tokfh, fi->tokfn, TXbool_False,
                      (byte *)&newRecid, sizeof(RECID), TXbool_False) !=
          sizeof(RECID))
        goto err;
      if (newAux != NULL &&
          tx_rawwrite(TXPMBUFPN, fi->tokfh, fi->tokfn, TXbool_False,
                      (byte *)newAux, fi->auxfldsz, TXbool_False) !=
          fi->auxfldsz)
        goto err;
      /* Other processes' index buffers will be updated via index close/reopen
       * from locks
       */
      goto ok;
    }
  putmsg(MERR, fn,
 "Cannot update Metamorph index token file %s: No mem/file write permissions",
         fi->tokfn);
  goto err;

ok:
  return(2);                                    /* successful update */
err:
  return(0);
}

int
fdbi_flush(fi)
FDBI    *fi;
/* Flushes writes via fdbi_updatetokaux() and dbi.
 */
{
  TXflush3dbi(fi->dbi);
  /* KNG 20060727 ia64 Linux 2.6.5 msync() appears to change file
   * size, rounding it up to next page boundary, which causes
   * erroneous token counts and corruption in future updates/accesses.
   * WTF epi/mmaptest.c changesz test is supposed to test for this:
   */
#if defined(EPI_HAVE_MSYNC) && defined(EPI_HAVE_MMAP) && !defined(__ia64)
  /* fdbi_updatetokaux() should be atomic, but msync() anyway.
   * Where this is really needed -- under EPI_MMAP_SHARED_IS_PRIVATE --
   * it doesn't exist.  Sigh.  KNG 000906
   */
  if (fi->tokbufismmap)
    msync((caddr_t)fi->tokbuf, (size_t)fi->tokfilesz,
          (MS_SYNC | MS_INVALIDATE));
#endif /* EPI_HAVE_MSYNC && EPI_HAVE_MMAP && !__ia64 */
  return(1);
}

EPI_OFF_T
fdbi_countrecs(fi)
FDBI    *fi;
/* Returns count of records in table (i.e. index plus new/delete list).
 * Returns -1 on error.  Assumes read lock obtained.
 */
{
  static CONST char     fn[] = "fdbi_countrecs";
  EPI_OFF_T             cnt, l, r, t;
  BTLOC                 loc, x;
  RECID                 rec;
  size_t                sz;
  void                  *curaux;
  BTREE                 *del, *newrec;
  void                  *tmp;
  char                  tmpbuf[16];

  errno = 0;
  tmp = (fi->tokelsz <= sizeof(tmpbuf) ? tmpbuf :
         TXmalloc(TXPMBUFPN, fn, fi->tokelsz));
  if (tmp == NULL) goto err;

  cnt = fi->totrecs;                            /* total in index */
  del = fi->dbi->del;
  newrec = fi->dbi->newrec;

  if (del != BTREEPN)                           /* correct for deleted recs */
    {
      rewindbtree(del);
      while (sz = sizeof(loc), x = btgetnext(del, &sz, &loc, NULL),
             TXrecidvalid2(&x))
        {
          /* For each delete recid:
           *   If in token file:
           *     Decrease count
           *   Else
           *     Ignore it, it applies to new list checked below
           */
          l = (EPI_OFF_T)0;
          r = fi->totrecs;
          while (l < r)                         /* binary search */
            {
              t = ((l + r) >> 1);
              rec = TXfdbiGetRecidAndAuxData(fi, t + (EPI_OFF_T)1, NULL,
                                             &curaux);
              if (!TXrecidvalid2(&rec)) goto err;
              if (TXgetoff2(&loc) < TXgetoff2(&rec)) r = t;
              else if (TXgetoff2(&loc) > TXgetoff2(&rec)) l = t + 1;
              else
                {
                  cnt--;                        /* to be deleted */
                  break;
                }
            }
        }
    }

  if (newrec != BTREEPN)                        /* correct for new recs */
    {
      rewindbtree(newrec);
      while (sz = fi->tokelsz, x = btgetnext(newrec, &sz, tmp, NULL),
             TXrecidvalid2(&x))
        {
          /* For each new recid:
           *   If in token file:
           *     If in delete list:
           *       Count it, it was deleted from token count above
           *     Else
           *       Don't count, it overlaps a (counted) token file recid
           *   Else
           *     If in delete list:
           *       Count it, it was not deleted above
           *     Else
           *       Count it, it's a new record
           */
          FDBI_TXALIGN_RECID_COPY(&loc, tmp);
          x = btsearch(del, sizeof(loc), &loc);
          if (TXrecidvalid2(&x))
            {
              cnt++;
              continue;
            }
          l = (EPI_OFF_T)0;
          r = fi->totrecs;
          while (l < r)                         /* binary search */
            {
              t = ((l + r) >> 1);
              rec = TXfdbiGetRecidAndAuxData(fi, t + (EPI_OFF_T)1, NULL,
                                             &curaux);
              if (!TXrecidvalid2(&rec)) goto err;
              if (TXgetoff2(&loc) < TXgetoff2(&rec)) r = t;
              else if (TXgetoff2(&loc) > TXgetoff2(&rec)) l = t + 1;
              else goto next;                   /* dup of token file recid */
            }
          cnt++;
        next: ;
        }
    }
  goto done;

err:
  cnt = (EPI_OFF_T)(-1);
done:
  if (tmp != NULL && tmp != (void *)tmpbuf) tmp = TXfree(tmp);
  return(cnt);
}

/* ------------------------------------------------------------------------- */

static int fdbi_rankrecid_trace ARGS((FDBI *fi, RECID recid, FDBIS **sets,
                                      int num));
static int
fdbi_rankrecid_trace(fi, recid, sets, num)
FDBI    *fi;
RECID   recid;
FDBIS   **sets;
int     num;
/* Trace/debug function: seeks to `recid' and ranks it in detail.
 */
{
  static CONST char     fn[] = "fdbi_rankrecid";
  EPI_OFF_T             l, r, t;
  int                   i, n;
  void                  *aux;
  RECID                 rec, tok;
  FDBIS                 *fs;
  FDBIHI                *hits[256], *h;

  if ((size_t)num > sizeof(hits)/sizeof(hits[0]))
    {
      putmsg(MERR + MAE, fn, "Too many sets");
      goto err;
    }

  l = (EPI_OFF_T)0;                             /* binary search for token */
  r = fi->totrecs;
  while (l < r)
    {
      t = ((l + r) >> 1);
      rec = TXfdbiGetRecidAndAuxData(fi, t + (EPI_OFF_T)1, NULL, &aux);
      if (!TXrecidvalid2(&rec)) goto err;
      if (TXgetoff2(&recid) < TXgetoff2(&rec)) r = t;
      else if (TXgetoff2(&recid) > TXgetoff2(&rec)) l = t + 1;
      else goto gotit;
    }
  putmsg(MINFO, fn, "Recid 0x%wx not found in index %s",
         (EPI_HUGEINT)TXgetoff2(&recid), fi->tokfn);
  goto err;                                     /* not found */

gotit:
  TXsetrecid(&tok, t + (EPI_OFF_T)1);
  for (n = i = 0; i < num; i++)                 /* seek all to recid */
    {
      fs = sets[i];
      if (!fs->getnext(fs, tok)) continue;
      if (TXgetoff2(&fs->hip->loc) != TXgetoff2(&tok)) continue;
      sets[n++] = sets[i];
    }
  num = n;
  for (i = 0; i < num; i++)                     /* decode ones at recid */
    {
      hits[i] = h = sets[i]->hip;
      if (h->decodefunc != FDBIHICBPN &&        /* decode if needed */
          !h->decodefunc(h->decodeusr))
        goto err;
    }
  /* NOTE that selsort() is hardcoded for size and cmphit_allmatch(): */
  if (num > 1)
    qsort(hits, num, sizeof(hits[0]), cmphit_allmatch);
  else if (num == 0)
    {
      putmsg(MWARN + UGE, fn,
             "Trace recid 0x%wx not in query result set using index %s",
             (EPI_HUGEINT)TXgetoff2(&recid), fi->tokfn);
      goto err;
    }
  fi->rp->curRecid = recid;
  rppm_rankbest_trace(fi->rp, hits, num, SIZE_TPN); /* rank this document */
  return(1);
err:
  return(0);
}

static int fdbi_dump ARGS((FDBI *fi));
static int
fdbi_dump(fi)
FDBI    *fi;
{
  static CONST char     fn[] = "fdbi_dump";
  byte          *dbuf, *dp;
  size_t        dn, dsz, di;
  EPI_OFF_T     dtok;
  RECID         drec, prec;
  char          *dtmp;
  char          tmp[EPI_OFF_T_BITS/4 + 4];

  if (TxIndexDump & 1)                                  /* dump new list */
    {
      dbuf = BYTEPN;
      dn = dsz = 0;
      wtix_btree2list(fi->dbi->newrec, fi->auxfldsz, 1, &dbuf, &dsz, &dn);
      if (!(dtmp = (char *)TXmalloc(TXPMBUFPN, fn, 3*fi->auxfldsz + 1)))
        goto err;
      putmsg(MINFO, CHARPN, "%wkd recids in new list %s:",
             (EPI_HUGEINT)dn, getdbffn(fi->dbi->newrec->dbf));
      *dtmp = '\0';
      for (dp = dbuf; dn > 0; dn--, dp += fi->tokelsz)
        {
          for (di = 0; di < fi->auxfldsz; di++)
            sprintf(dtmp + 3*di, " %02X",
                    (unsigned)dp[di + FDBI_TOKEL_RECIDSZ]);
          htsnpf(tmp, sizeof(tmp), "0x%wx", (EPI_HUGEINT)(*(EPI_OFF_T *)dp));
          putmsg(MINFO, CHARPN,"%10s   %s", tmp, dtmp);
        }
      dbuf = TXfree(dbuf);
      dtmp = TXfree(dtmp);
    }
  if (TxIndexDump & 2)                                  /* dump delete list */
    {
      dbuf = BYTEPN;
      dn = dsz = 0;
      wtix_btree2list(fi->dbi->del, 0, 1, &dbuf, &dsz, &dn);
      putmsg(MINFO, CHARPN, "%wkd recids in delete list %s:",
             (EPI_HUGEINT)dn, getdbffn(fi->dbi->del->dbf));
      for (dp = dbuf; dn > 0; dn--, dp += sizeof(BTLOC))
        {
          htsnpf(tmp, sizeof(tmp), "0x%wx", (EPI_HUGEINT)(*(EPI_OFF_T *)dp));
          putmsg(MINFO, CHARPN, "%10s", tmp);
        }
      dbuf = TXfree(dbuf);
    }
  if (TxIndexDump & 4)                                  /* dump token file */
    {
      TXsetrecid(&prec, (EPI_OFF_T)(-1));
      if (!(dtmp = (char *)TXmalloc(TXPMBUFPN, fn, 3*fi->auxfldsz + 1)))
        goto err;
      putmsg(MINFO, CHARPN, "%wd recids in token file %s:",
             (EPI_HUGEINT)fi->totrecs, fi->tokfn);          /* WTF assumes htpf */
      *dtmp = '\0';
      for (dtok = (EPI_OFF_T)1; dtok <= fi->totrecs; dtok++)
        {
          drec = TXfdbiGetRecidAndAuxData(fi, dtok, NULL,
                                          (void **)(char *)&dbuf);
          if (TXrecidvalid2(&drec))
            {
              for (di = 0; di < fi->auxfldsz; di++)
                sprintf(dtmp + 3*di, " %02X", (unsigned)dbuf[di]);
              htsnpf(tmp, sizeof(tmp), "0x%wx", (EPI_HUGEINT)TXgetoff2(&drec));
              putmsg(MINFO, CHARPN, "%10s   %s", tmp, dtmp);
            }
          else
            putmsg(MERR, CHARPN, "Invalid recid at token 0x%wx",
                   (EPI_HUGEINT)dtok);
          if (TXgetoff2(&drec) <= TXgetoff2(&prec) ||
	      TXgetoff2(&drec) < (EPI_OFF_T)0)
            putmsg(MERR, CHARPN,
                   "Out-of-order/dup/bad recid 0x%wx for token 0x%wx",
                   (EPI_HUGEINT)TXgetoff2(&drec), (EPI_HUGEINT)dtok);
          prec = drec;
        }
      dtmp = TXfree(dtmp);
    }
  if (TxIndexDump & 8)
    putmsg(MINFO, CHARPN, "%wd records in table via index %s",
           (EPI_HUGEINT)fdbi_countrecs(fi), fi->tokfn);
  return(1);
err:
  return(0);
}

/* ------------------------------------------------------------------------- */

static int TXfdbiIsWithinNAllMatch ARGS((FDBIHI **hits, int numSets,
              int minAndSetsRequired, int minSetsRequired, dword nSz,
              dword windowSz, int withinMode, FHEAP *orHeap, FHEAP *orHeap2));
static int
TXfdbiIsWithinNAllMatch(hits, numSets, minAndSetsRequired, minSetsRequired,
                        nSz, windowSz, withinMode, orHeap, orHeap2)
FDBIHI  **hits;                 /* (in/out) indexable sets, with loc info */
int     numSets;                /* (in) # terms in `hits' array */
int     minAndSetsRequired;     /* (in) number of AND sets required */
int     minSetsRequired;        /* (in) min. sets required (inc. ANDs) */
dword   nSz;                    /* (in) `w/N' size (scaled for index expr.) */
dword   windowSz;               /* (in) max diff (nSz <= windowSz <= 2*nSz) */
int     withinMode;             /* (in) withinmode */
FHEAP   *orHeap;                /* (in/out) OR-merge scratch heap for `hits'*/
FHEAP   *orHeap2;               /* (in/out) 2nd OR-merge scratch heap */
/* Checks if current hit is within N (words or chars, determined earlier).
 * Returns 1 if within N, 0 if not, -1 on error.
 * Assumes fdbi_get() setup.  For all-sets-must-match.
 */
{
  static CONST char     fn[] = "TXfdbiIsWithinNAllMatch";
  size_t                lookAheadIdx, i;
  dword                 loc, leftEdge, rightEdge, diff;
  dword                 rightEdgeMinusN, leftEdgePlusN;
  FDBIHI                *h, *leftSet;
  FHEAP                 *leftHeap = orHeap;

  (void)minAndSetsRequired;
  (void)minSetsRequired;
  (void)withinMode;
  (void)orHeap2;
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, fn, "w/N check at token 0x%wx",
           (EPI_HUGEINT)TXgetoff2(&(*hits)->loc));

  /*   Bug 690/fdbi.c:1.193 bug C: For API3WITHIN_TYPE_RADIUS, it is not
   * enough that all sets are within `windowSz' (typically 2N) span diff;
   * there must also be a middle anchor set such that all sets are within
   * N to the left and N to the right.  E.g. for `A B C w/3', this matches:
   *   A x x B x x C
   * but this does not:
   *   A x B x x x C
   * because `B' and `C' are 4 > N apart, even though the overall span
   * diff is still <= 2*N.
   *
   *   We put all the sets in `leftHeap', and use it to track the
   * left edge of the potential match (i.e. top(leftHeap) is leftmost set).
   * We track the rightmost set's location with `rightEdge'.  When
   * the leftmost and rightmost sets are within `windowSz', we have a
   * potential match; if within `nSz', it is a true match.
   * If `nSz' < diff <= `windowSz', we look for an "anchor" set that is
   * within N of both left and right edges.
   *   KNG 20091016 Bug 2836 can skip the anchor check for
   * API3WITHIN_TYPE_SPAN, as `windowSz' == `nSz' and no anchor needed.
   */
  fheap_clear(leftHeap);
  rightEdge = 0;
  for (i = 0; i < (size_t)numSets; i++)         /* all sets into leftHeap */
    {
      h = hits[i];
      h->curHit = 0;                            /* reset to start of locs */
      if (!fheap_insert(leftHeap, h))
        return(-1);                             /* error */
      loc = h->hits[h->curHit];
      /* Bug 2972: take into account set length too (i.e. phrases): */
      if (h->hitLens != DWORDPN) loc += h->hitLens[h->curHit] - 1;
      if (loc > rightEdge) rightEdge = loc;
    }

  for (;;)                                      /* all locs all sets */
    {
      leftSet = (FDBIHI *)fheap_top(leftHeap);
      leftEdge = leftSet->hits[leftSet->curHit];
      diff = rightEdge - leftEdge;              /* span diff of current hits*/
      if (diff <= windowSz)                     /* potential match */
        {
          /* KNG 20091016 for API3WITHIN_TYPE_SPAN, `windowSz' == `nSz',
           * so this will always be true here:
           */
          if (diff <= nSz) goto gotMatch;       /* definite match */
          /* `nSz' < `diff' <= `windowSz'.  Look for an anchor set
           * that is within `nSz' of *both* `leftEdge' and `rightEdge'
           * (Bug 690/fdbi.c:1.193 bug C).  We just linearly look
           * at all the sets in the heap (except top: we know that is
           * at `leftEdge').  The sets are not in full sorted order,
           * so we cannot binary-search to the desired center of the
           * `diff', and they might be bunched up towards one edge
           * or the other anyway, negating binary-search effectiveness.
           * KNG 20091016 this is for API3WITHIN_TYPE_RADIUS.
           */
          leftEdgePlusN = leftEdge + nSz;
          rightEdgeMinusN = rightEdge - nSz;
          for (i = 1; i < fheap_num(leftHeap); i++)
            {                                   /* all but leftmost set */
              h = (FDBIHI *)fheap_elem(leftHeap, i);
              loc = h->hits[h->curHit];
              if (loc <= leftEdgePlusN)
                {
                  if (h->hitLens != DWORDPN) loc += h->hitLens[h->curHit] - 1;
                  if (loc >= rightEdgeMinusN) goto gotMatch;
                  /* Bug 2899: If we do not find a valid anchor
                   * amongst current hits, we will end up advancing
                   * `leftSet' below.  But maybe we should keep its
                   * current hit and advance an *interior* set instead.
                   * E.g. for word radius query `A (B,C) D w/2' and
                   * this text:
                   *     A B C x D
                   * we are probably looking at hits `A', `B', `D'
                   * now and will not find a valid anchor.  But
                   * `leftSet' is `A' and may not have any more hits,
                   * so we should instead advance `(B,C)' (below) to
                   * get hit `C' and thus a middle anchor.  So look
                   * ahead on current set for that `C' hit *here*.  We
                   * do this only if `diff <= windowSz' (because
                   * otherwise it is impossible to find an anchor and
                   * we *will* need to advance `leftSet' to try to
                   * shorten the diff), and if `h' is within N of the
                   * left edge (since its next hits will be even
                   * farther away from `leftSet').  See also
                   * TXmmAdvanceASetForWithinN() for a (loosely)
                   * related attempt at fix for linear search.
                   * WTF this probably does not catch all possibilities
                   * (but then API3WITHIN_TYPE_RADIUS should be used less
                   * in version 6+ than ..._TYPE_SPAN anyway):
                   */
                  for (lookAheadIdx = h->curHit + 1;
                       lookAheadIdx < h->nhits &&
                         /* no point looking beyond `leftEdgePlusN': */
                         h->hits[lookAheadIdx] <= leftEdgePlusN;
                       lookAheadIdx++)
                    if (h->hits[lookAheadIdx] + (h->hitLens != DWORDPN ?
                         h->hitLens[lookAheadIdx] - 1 : 0) >= rightEdgeMinusN)
                      {
                        h->curHit = lookAheadIdx;
                        goto gotMatch;
                      }
                }
            }
        }
      /* No match: pop `leftSet' off `leftHeap' and advance to next loc: */
      fheap_deletetop(leftHeap);
      if (++leftSet->curHit >= leftSet->nhits)
        /* Out of locs for `leftSet'; since all sets are required,
         * this doc cannot be a match:
         */
        return(0);                              /* this doc is not a match */
      /* Put it back in `leftHeap': */
      if (!fheap_insert(leftHeap, leftSet))
        return(-1);                             /* error */
      /* Update `rightEdge': */
      loc = leftSet->hits[leftSet->curHit];
      if (leftSet->hitLens != DWORDPN)
        loc += leftSet->hitLens[leftSet->curHit] - 1;
      if (loc > rightEdge) rightEdge = loc;
    }

gotMatch:
  return(1);                                    /* it matches */
}

static int TXfdbiIsWithinNSomeMatch ARGS((FDBIHI **hits, int numSets,
              int minAndSetsRequired, int minSetsRequired, dword nSz,
              dword windowSz, int withinMode, FHEAP *orHeap, FHEAP *orHeap2));
static int
TXfdbiIsWithinNSomeMatch(hits, numSets, minAndSetsRequired, minSetsRequired,
                         nSz, windowSz, withinMode, orHeap, orHeap2)
FDBIHI  **hits;                 /* (in/out) indexable sets, with loc info */
int     numSets;                /* (in) # terms in `hits' array */
int     minAndSetsRequired;     /* (in) number of AND sets required */
int     minSetsRequired;        /* (in) min. sets required (inc. ANDs) */
dword   nSz;                    /* (in) `w/N' size (scaled for index expr.) */
dword   windowSz;               /* (in) max diff (nSz <= windowSz <= 2*nSz)*/
int     withinMode;             /* (in) withinmode */
FHEAP   *orHeap;                /* (in/out) OR-merge scratch heap for `hits'*/
FHEAP   *orHeap2;               /* (in/out) 2nd OR-merge scratch heap */
/* Checks if current hit is within N (words or chars, determined earlier).
 * Returns 1 if within N, 0 if not, -1 on error.
 * Assumes fdbi_get() setup.
 */
{
  static CONST char     fn[] = "TXfdbiIsWithinNSomeMatch";
  FDBIHI                *h;
  int                   numAndSetsMatched, numSetsMatched;
  size_t                lookAheadIdx, i;
  dword                 leftEdge, rightEdge, loc, diff;
  dword                 leftEdgePlusN, rightEdgeMinusN;
  FHEAP                 *leftHeap = orHeap, *otherHeap = orHeap2;

  (void)withinMode;
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, fn, "w/N check at token 0x%wx",
           (EPI_HUGEINT)TXgetoff2(&(*hits)->loc));

  /* We do a similar sliding rubber-band window as with
   * TXfdbiIsWithinNAllMatch(), but only the sets that are part of
   * the currently-testing match are in `leftHeap'; the rest are in
   * `otherHeap'.  We pull off sets from `otherHeap' into
   * `leftHeap' until AND/SET logic satisfied.  If we exceed the
   * window, we pull off a (left-edge) `leftHeap' item, get next, and
   * put it back in `otherHeap':
   */
  fheap_clear(leftHeap);
  fheap_clear(otherHeap);
  for (i = 0; i < (size_t)numSets; i++)         /* init the heap */
    {
      h = hits[i];
      h->curHit = 0;                            /* reset to start of locs */
      if (!fheap_insert(otherHeap, h))
        return(-1);                             /* error */
    }

  /* Prime `leftHeap' with a single set from `otherHeap': - - - - - - - - - */
  if (fheap_num(otherHeap) <= 0) return(0);     /* should not happen */
  h = (FDBIHI *)fheap_top(otherHeap);
  fheap_deletetop(otherHeap);
  if (!fheap_insert(leftHeap, h))
    return(-1);                                 /* error */
  numSetsMatched = 1;                           /* since only set in heap */
  numAndSetsMatched = (h->set->logic == LOGIAND);
  leftEdge = rightEdge = h->hits[h->curHit];
  if (h->hitLens != DWORDPN) rightEdge += h->hitLens[h->curHit] - 1;

  for (;;)
    {
      /* We need both set logic and within-N satisfied, but we
       * check one or the other first, depending on which is quicker.
       * If we exceed max `windowSz', there is no point adding more sets
       * to satisfy set logic.  So check match span first:
       */
      diff = rightEdge - leftEdge;
      if (diff <= windowSz)                     /* still under max window */
        {
          /* Since Bug 690/fdbi.c:1.93 bug C check may involve a linear
           * scan of `leftHeap', check for set-logic next:
           */
          if (numAndSetsMatched >= minAndSetsRequired &&
              numSetsMatched >= minSetsRequired) /* set logic satisfied */
            {
              /* KNG 20091016 for API3WITHIN_TYPE_SPAN, `windowSz' == `nSz',
               * so this will always be true here:
               */
              if (diff <= nSz) break;           /* `w/N' ok too: match */
              /* `nSz' < `diff' <= `windowSz': look for middle anchor set
               * ala TXfdbiIsWithinNAllMatch(); Bug 690/fdbi.c:1.193 bug C.
               * Linearly scan all items in `leftHeap'; can skip first
               * item because it is the left edge and would never be
               * the middle anchor set:
               * KNG 20091016 this is for API3WITHIN_TYPE_RADIUS
               */
              leftEdgePlusN = leftEdge + nSz;
              rightEdgeMinusN = rightEdge - nSz;
              for (i = 1; i < fheap_num(leftHeap); i++)
                {
                  h = (FDBIHI *)fheap_elem(leftHeap, i);
                  loc = h->hits[h->curHit];
                  if (loc <= leftEdgePlusN)
                    {
                      if (h->hitLens != DWORDPN)
                        loc += h->hitLens[h->curHit] - 1;
                      if (loc >= rightEdgeMinusN) return(1);    /* match */
                      /* Bug 2899 check (see comments in ...AllMatch()): */
                      for (lookAheadIdx = h->curHit + 1;
                           lookAheadIdx < h->nhits &&
                             /* no point looking beyond `leftEdgePlusN': */
                             h->hits[lookAheadIdx] <= leftEdgePlusN;
                           lookAheadIdx++)
                        if (h->hits[lookAheadIdx] + (h->hitLens != DWORDPN ?
                         h->hitLens[lookAheadIdx] - 1 : 0) >= rightEdgeMinusN)
                          {
                            h->curHit = lookAheadIdx;
                            return(1);          /* match */
                          }
                    }
                }
              /* Set logic ok, but not within-N.  Adding more sets to
               * the right will only widen the potential match span,
               * and thus cannot possibly satisfy within-N.  So remove
               * a match set from the left, to shorten the span:
               */
              goto yankLeftSet;
            }
          else                                  /* set logic not satisfied */
            {
              /* Add a set to `leftHeap' to try to satisfy set logic: */
              if (fheap_num(otherHeap) <= 0) return(0); /* no sets left */
              h = (FDBIHI *)fheap_top(otherHeap);
              fheap_deletetop(otherHeap);
              if (!fheap_insert(leftHeap, h))
                return(-1);                     /* error */
              numSetsMatched++;
              if (h->set->logic == LOGIAND) numAndSetsMatched++;
              /* Bug 2947: must update `leftEdge' too: */
              if ((loc = h->hits[h->curHit]) < leftEdge) leftEdge = loc;
              if (h->hitLens != DWORDPN) loc += h->hitLens[h->curHit] - 1;
              if (loc > rightEdge) rightEdge = loc;
            }
        }
      else                                      /* `diff' > `windowSz' */
        {
        yankLeftSet:
          /* Remove leftmost set from `leftHeap' to shorten the span
           * (and maybe get within-N); update set logic counts to
           * reflect its removal:
           */
          if (fheap_num(leftHeap) <= 0) return(0);  /* should not happen */
          h = (FDBIHI *)fheap_top(leftHeap);
          fheap_deletetop(leftHeap);
          numSetsMatched--;
          if (h->set->logic == LOGIAND) numAndSetsMatched--;
          /* Inc `h' to its next hit, and put in `otherHeap': */
          if (++h->curHit >= h->nhits)
            { /* `h' has no more hits for this doc ... */
              if (h->set->logic == LOGIAND ||
                  (size_t)numSetsMatched + fheap_num(otherHeap) <
                  (size_t)minSetsRequired)
                /* ... and is a required set: thus this doc cannot match: */
                return(0);                      /* not a match */
            }
          else if (!fheap_insert(otherHeap, h))
            return(-1);                         /* error */
          h = (FDBIHI *)fheap_top(leftHeap);
          leftEdge = h->hits[h->curHit];
        }
    }
  /* Got a match: */
  return(1);
}

/* ------------------------------------------------------------------------- */

static const char *
TXfdbiMetamorphPostProcReason(RPPM *rppm, MM3S *mm3s, FDBI *fi,
                              TXbool multiExprWithinNPostProc)
/* Index version of RPPM_WITHIN_POSTMM_REASON(): there are more cases
 * that index can handle (that linear search cannot).
 * KNG 20060724 bug A: if indexwithin does not permit within processing,
 * do not set no-post-proc.  bug E: need post-proc for multiple
 * index expressions even if withinmode word, because we don't know
 * if they overlapped or not in original text.
 * Returns reason for post-processing, or NULL if none needed.
 */
{
  const char    *rppmReason;
  size_t        i, numIndexExprs;

  rppmReason = RPPM_WITHIN_POSTMM_REASON(rppm, mm3s);
  if (!rppmReason)
    return(NULL);
  /* withinmode word and withincount handled by the index: */
  /* Bug 7094: Note that even if withincount > 0, we may still not
   * be *doing* within-N (i.e. if minsets <= 1; see caller `doWithinN').
   * `multiExprWithinNPostProc' should also be false if not doing within-N:
   */
  if (mm3s->withincount == 0)
    return(rppmReason);
  /* can do all TYPEs w/o post-proc, but only word UNITs: */
  if (API3WITHIN_GET_UNIT(mm3s->withinmode) != API3WITHINWORD)
    return("`w/N' delimiter used and withinmode unit is `char'");
  if (!(TXindexWithin & TXindexWithinFlag_Words))
    {
      if (TXindexWithinFlag_Words == 0x2)
        return("`w/N' delimiter used and withinmode unit is `word' but (indexwithin & 2) is off");
      else
        return("`w/N' delimiter used and withinmode unit is `word' but (indexwithin & TXindexWithinFlag_Words) is off");
    }

  if (fi->dbi->explist)
    {
      for (i = 0; *fi->dbi->explist[i] != '\0'; i++);
      numIndexExprs = i;
    }
  else                                          /* using default expression */
    numIndexExprs = 1;

  if (numIndexExprs != 1 && multiExprWithinNPostProc)
    return(TXindexWithinFlag_AssumeOneHitInter == 0x8 ?
           "`w/N' delimiter used with multiple index expressions and (indexwithin & 0x8) is off" :
           "`w/N' delimiter used with multiple index expressions and (indexwithin & TXindexWithinFlag_AssumeOneHitInter) is off");
  if (!(fi->flags & FDF_FULL))
    return("`w/N' delimiter used but Metamorph index is not inverted");
  if (!mm3s->delimseq)
    /* wtf can this happen? */
    return("`w/N' delimiter used with dissimilar expressions");
  return(NULL);
}

/* ------------------------------------------------------------------------- */

static int
TXfdbiInitWithinN(FDBI *fi, FDBIS **sets, int numSets, int withinCount,
                  int withinmode, dword *maxwithindiff, dword *maxwithinhalf,
                  TXbool *multiExprPostProc)
/* Does some prep for `w/N' processing during fdbi_get().
 * Returns 0 on error.
 */
{
  enum
    {
      wst_unknown,
      wst_optimizeChars,
      wst_numIndexExprs
    }
  withinScaleType = wst_unknown;
  dword maxSetLen, maxWithinDiffWords;
  int   numIndexExprs, i, ret;
  int   maxWithinInterveningWords, totalExtraExprMatches;
  FDBIS *fs;

  *maxwithindiff = *maxwithinhalf = maxWithinDiffWords = 0;
  if (!fi->dbi->explist)                        /* using default expression */
    numIndexExprs = 1;
  else
    {
      for (i = 0; *fi->dbi->explist[i] != '\0'; i++);
      numIndexExprs = i;
    }
  *multiExprPostProc = (numIndexExprs > 1);     /* Bug 7094 */

  /* `maxWithinInterveningWords' is max number of words intervening
   * with (i.e. besides) set words, in the `w/N' window.  Does not
   * include any scaling by number of index expressions.
   * `totalExtraExprMatches' is total extra (i.e. beyond 1)
   * index-expression matches for all sets; e.g. for "foo" it is 0
   * (matches once), "o'brien" might be 1 (matches `o' *and* `brien').
   * For both we assume all sets will be present in window; wtf might
   * not be true if not `allmatch'?
   */
  maxWithinInterveningWords = withinCount;
  totalExtraExprMatches = 0;
  for (i = 0; i < numSets; i++)
    {
      maxWithinInterveningWords -= sets[i]->minPhraseLen;
      totalExtraExprMatches += sets[i]->maxExtraExprMatches;
    }
  if (maxWithinInterveningWords < 0) maxWithinInterveningWords = 0;

  /* Although the index has only word not char positions (and doesn't
   * track whitespace), it can be useful not only for API3WITHINWORD
   * but even API3WITHINCHAR, because terms that are at most N chars
   * apart must also be at most N index words apart.  We need to
   * account for possible overlapping index hits from multiple index
   * expressions though:
   */
  /* KNG 20060724 bug B: check withinmode when setting window: */
  switch (API3WITHIN_GET_UNIT(withinmode))
    {
    case API3WITHINCHAR:
      /* The max character "spread" for w/N (char, ...TYPE_RADIUS)
       * is actually 2*N, because of left and right scans for N.
       * But sometimes the limit really is N (e.g. only 2 sets), so
       * we might divide this by 2, below:
       */
      maxWithinDiffWords = 2*withinCount;
      *maxwithindiff = maxWithinDiffWords;
      if (fi->dbi->explist)
        {
          /* Optimization: if the index expression is just the default
           * \alnum{2,99}, then we know each index word covers at least
           * 3 original text characters: minimum 2 for the word, plus an
           * intervening non-alnum byte.  (There might not be a non-alnum
           * byte if the index word is 99 bytes, but then clearly we're
           * already over 3 bytes.)  This lets us scale our window down:
           */
          if ((TXindexWithin & TXindexWithinFlag_OptimizeChars) &&
              numIndexExprs == 1 &&
              strcmp(*fi->dbi->explist, "\\alnum{2,99}") == 0)
            {
              *maxwithindiff = (*maxwithindiff + 2)/3;
              withinScaleType = wst_optimizeChars;
            }
          else
            {
              /* With multiple index expressions, index hits could have
               * overlapped, so scale up our diff limit:
               */
              *maxwithindiff *= numIndexExprs;
              withinScaleType = wst_numIndexExprs;
            }
        }
      break;
    case API3WITHINWORD:
      maxWithinDiffWords = 2*withinCount;       /* see spread comment above */
      /* See overlap comment above: */
      /* Bug 7004: delay `numIndexExprs' fixup/scaling until window
       * type (radius or span) known: we no longer apply same
       * value/scale symmetrically to all words in window, so divide
       * by 2 for `maxwithinhalf' would wrongly divide it for some
       * words.  wtf do same delay for API3WITHINCHAR?
       */
      *maxwithindiff = maxWithinDiffWords;
      withinScaleType = wst_numIndexExprs;
      break;
    default:
      goto unknownWithinmode;
    }
  /* Half the window, e.g. equivalent to N for just-left or just-right: */
  *maxwithinhalf = *maxwithindiff/2;
  /* Optimization (assumed below): if only 2 sets, there can be no
   * "interior" hits, i.e. no set can ever have hits both left and right
   * of itself, so max possible window is N not 2*N:
   * KNG 20060724 bug C partial fix for 2 terms (see below for more):
   */
  if (numSets <= 2) *maxwithindiff = *maxwithinhalf;
  switch (API3WITHIN_GET_TYPE(withinmode))
    {
    case API3WITHIN_TYPE_SPAN:
      /* KNG 20091016 Bug 2836: for API3WITHIN_TYPE_SPAN, we also
       * make the max window == N, and subtract 1 from both since
       * unlike ...TYPE_RADIUS all sets are counted in `w/N'
       * (...TYPE_RADIUS does not count "fencepost" anchor set):
       */
      if (*maxwithinhalf > 0) (*maxwithinhalf)--;
      *maxwithindiff = *maxwithinhalf;
      if (API3WITHIN_GET_UNIT(withinmode) == API3WITHINWORD)
        {                                       /* wtf similar for RADIUS? */
          /* Bug 7004: Multi-match fixup delayed until now; see delay
           * comment above
           */
          /* Bug 7004 comment #5 1 of 2: Instead of assuming all words
           * in window might be multi-match, we can check whether some
           * actually are -- the query terms, since they will be
           * present in the window.  Assumes `allmatch'; wrong if not?
           */
          *maxwithinhalf += totalExtraExprMatches;
          *maxwithindiff += totalExtraExprMatches;
          /* Bug 7004 comment #5 2 of 2: Deal with the rest of the
           * window -- intervening (non-query) words.  Although we
           * can't know unless we post-proc, typically none will be
           * multi-match.  Thus if ...AssumeOneHitInter, assume so:
           * leave the intervening part of the window narrow --
           * unscaled by #-exprs (as a proxy for multi-match) -- and
           * do not post-proc.  Keeping it narrow avoids the ambiguous
           * (multi-match) cases we need to post-proc (at least the
           * multi-match ones due to multi-expr; some multi-match can
           * occur with single exprs e.g. "o'brien" matches `\alnum+'
           * 2x).  Downside is it could also eliminate valid
           * (multi-match intervening word) hits in those cases, so
           * also allow (if !...AssumeOneHitInter) scaling and
           * post-proc, for slower (but including all valid) results:
           */
          if (TXindexWithin & TXindexWithinFlag_AssumeOneHitInter)
            {
              /* window narrow, no post-proc, but maybe drop valid hits */
              *multiExprPostProc = TXbool_False;
            }
          else if (numIndexExprs > 1)
            {
              /* scale up to not drop possible valid results; post-proc:*/
              *maxwithinhalf += maxWithinInterveningWords*(numIndexExprs - 1);
              *maxwithindiff += maxWithinInterveningWords*(numIndexExprs - 1);
            }
        }
      break;
    case API3WITHIN_TYPE_RADIUS:
      /* WTF does Bug 7004 ...AssumeOneHitInter and smarter
       * scaling apply here?
       */
      /* KNG 20100125 Bug 2972 (this part is actually index
       * version of Bug 2901): for ..._TYPE_RADIUS searches, `w/N'
       * count goes right from right edge and left from left edge
       * (of anchor), so must take into account anchor set length.
       * Use longest set, since we do not know what anchor set will be;
       * this may make `maxwithindiff' looser, but TXfdbiIsWithinN...()
       * will validate each particular case:
       */
      maxSetLen = 0;
      for (i = 0; i < numSets; i++)
        {
          fs = sets[i];
          if ((dword)fs->maxPhraseLen > maxSetLen)
            maxSetLen = (dword)fs->maxPhraseLen;
        }
      /* When factoring in potential anchor set length, we need
       * the original, word-based `maxwithindiff', i.e. the value
       * from *before* scaling for index expressions etc.  We then
       * add in potential anchor set length and re-apply scaling:
       */
      switch (withinScaleType)
        {
        case wst_optimizeChars:
          *maxwithindiff = (maxWithinDiffWords + (maxSetLen - 1) + 2)/3;
          break;
        case wst_numIndexExprs:
          *maxwithindiff = (maxWithinDiffWords + (maxSetLen - 1))*
            numIndexExprs;
          break;
        default:
          putmsg(MERR + UGE, __FUNCTION__,
                 "Internal error: Unknown withinScaleType %d",
                 (int)withinScaleType);
          break;
        }
      break;
    default:
    unknownWithinmode:
      putmsg(MERR + UGE, __FUNCTION__, "Unknown withinmode setting %d",
             withinmode);
      break;
    }
  /* Open needed heap(s):  KNG 20060724 bug D: open `wordHeap2' only once: */
  if (!fi->omfh &&
      !(fi->omfh = openfheap(ormerge_heapcmp, NULL, 0)))
    goto err;
  fi->wordHeap2 = closefheap(fi->wordHeap2);
  if (!(fi->wordHeap2 = openfheap(ormerge_heapcmp, NULL, 0)))
    goto err;
  ret = 1;
  goto finally;

err:
  ret = 0;
finally:
  return(ret);
}

const char *
TXfdbiGetLinearReason(void)
{
  return(TXfdbiLinearReason);
}

TXbool
TXfdbiResetGlobalHacks(void)
/* Resets global hack variables that we can't or haven't attached to
 * a Texis object.  Should be called at start of each SQL exec.
 */
{
  FdbiBonusError = 0;
  TXfdbiLinearReason = NULL;
  return(TXbool_True);
}

/* ------------------------------------------------------------------------- */

int
fdbi_get(fi, mm, mq, icb, mcb, rcb, usr, nopost, op, and, nandrecs)
FDBI            *fi;
MMAPI           *mm;
MMQL            *mq;
FDBIICB         *icb;                   /* optional initial callback */
FDBIMCB         *mcb;                   /* optional is-match callback */
FDBIRCB         *rcb;                   /* rank callback */
void            *usr;
int             *nopost;
int             op;
BTREE           *and;
EPI_OFF_T       nandrecs;
/* Searches index `fi' for terms in `mm'/`mq'.  First calls `icb' once
 * (if non-NULL) with post-MM-needed flag.  For each record, calls
 * `mcb' (if non-NULL) with aux data (if present): returns non-zero if
 * record matches.  Then `rcb' (required) is called with each ranked hit.
 * Sets `*nopost' to 1 if hits are completely resolved, or 0 if
 * post-processing with Metamorph is needed (e.g. REX/NPM/XPM in the
 * query, or within processing, or non-inverted index).  `op' is a
 * FOP operand to do (e.g. FOP_PROXIM, FOP_MM, etc.).  Returns 1 if
 * ok, 0 if no indexable sets (i.e. must do linear search: caller must
 * check APICP allinear setting; in predtoiinode()?), or -1 on error.
 * `and', if non-NULL, is a list of in-order RECIDS to AND the results
 * with (and `nandrecs' is # of items in it).
 */
{
  static CONST char     fn[] = "fdbi_get";
  static CONST char     needpost[] =
    /* Note that Webinator/Appliance scripts look for this msg prefix: */
    "Query `%s' would require post-processing%s%s";
  static const char     unindexableTerms[] ="Unindexable term(s) in query";
  int                   i, num, allmatch, nand, skip;
  int                   infthresh, nplinf, indexthresh;
  int                   rank = 0, uberrank, didlineardictchk = 0;
  int                   nlogiandIndexable, nlogisetIndexable;
  int                   nloginotIndexable, lineardictyap = 0;
  int                   optlikewithnots, osindg, osovermaxset, savidx;
  RPPM                  *rp;
  RPPM_SET              *rpset;
  int                   ret, minsets;
  FDBIS                 *fs, **sets = FDBISPPN, **sp;
  MM3S                  *queryMm3s = mm->mme;
  QS_RET        (*sort) ARGS((void *base, size_t n, size_t sz, SORTCB *cmp));
  FHEAP                 *fh;
  BTREE                 *del;
  BTLOC                 x;
  FDBIHI                **hits = FDBIHIPPN, *h, *h2;
  RECID                 curtok, currecid;
  long                  tblfreqs[MAXSELS];
  int                   gains[MAXSELS];
  int                   rpf, nlogisetIndexableNeeded, nopostmm = 0;
  void                  *curauxfld;
  char                  *term;
  dword                 maxwithindiff = 0, maxwithinhalf = 0;
  int                   doWithinN = 0;
  TXbool                multiExprWithinNPostProc = TXbool_False;
  const char            *postReason = NULL, *linearReason = NULL;
  RECID                 nterms;
  int                   (*rankBestOneFunc)(RPPM *rp, FDBIHI *hit,
                                       size_t *byteMedian);
  int                   (*rankBestFunc)(RPPM *rp, FDBIHI **hits, int num,
                                        size_t *byteMedian);
#ifndef NO_EMPTY_LIKEIN
  int                   hitsel[MAXSELS];
  int                   ns, fup;
  int                   j, np;
  EPI_OFF_T             tv;
  dword                 *lp, *le, maxword;
#endif /* EMPTY_LIKEIN */

  (void)nandrecs;
  if (!(fi->mode & PM_SELECT))
    {                                                   /* .dat etc. closed */
      putmsg(MERR + UGE, fn, BadSelect, fi->tokfn);
      goto err;
    }

  if (TxIndexDump) fdbi_dump(fi);
  rankBestOneFunc = (TXtraceRppm ? rppm_rankbest_one_trace:rppm_rankbest_one);
  rankBestFunc = (TXtraceRppm ? rppm_rankbest_trace : rppm_rankbest);
  TXfdbiResetGlobalHacks();     /* wtf what if multiple indexes used? */

  if (!(fi->flags & FDF_FULL) || (op == FOP_RELEV))
    rpf = RPF_RANKTBLFREQ;
  else
    rpf = 0;
  fi->indexcount = (EPI_OFF_T)0;
  fi->query = TXfree(fi->query);
  fdbi_clearpostmsg(fi);
  fi->lineardictmsg = TXfree(fi->lineardictmsg);
  fi->query = TXstrdup(TXPMBUFPN, fn, queryMm3s->query);        /* for msgs */
  closerppm(fi->rp);
  fi->totwords = fi->novermaxwords = fi->novermaxsetwords = 0;
  if ((fi->rp = rp = openrppm(mm, mq, op, fi, rpf)) == RPPMPN) goto err;

  /* Pass index expressions to RPPM since we know them, which might be
   * useful (but slower) to later new-list linear rppm_rankbuf() or
   * abstract() calls.  WTF share our existing RLEX object with RPPM
   * to avoid having to re-open it (and re-set locale); tricky because
   * RPPM ownership may pass elsewhere e.g. at fdbi_getrppm():
   */
  if (fi->dbtbl != (DBTBL *)NULL &&
      fi->dbtbl->ddic != (DDIC *)NULL &&
      fi->dbtbl->ddic->optimizations[OPTIMIZE_LINEAR_RANK_INDEX_EXPS])
    {
      TXrppmSetIndexExprs(rp, fi->dbi->explist, fi->dbi->locale);
      rppm_setflags(rp, RPF_USEINDEXEXPRS, 1);
    }
  if (fi->novermaxsetwords > 3)
    putmsg(MWARN + UGE, CHARPN, "%d sets with too many words in query `%s'",
           fi->novermaxsetwords, queryMm3s->query);
  sets = (FDBIS **)TXcalloc(TXPMBUFPN, fn, rp->numsets, sizeof(FDBIS *));
  hits = (FDBIHI **)TXcalloc(TXPMBUFPN, fn, rp->numsets, sizeof(FDBIHI *));
  if (sets == FDBISPPN || hits == FDBIHIPPN)
    goto err;

recompute:
  /* Compute weights, and set `sets' array to just index sets (e.g. no
   * REX/NPM/XPM sets):
   */
  nlogiandIndexable = nlogisetIndexable = nloginotIndexable = 0;
  nopostmm = *nopost = 0;
  nlogisetIndexableNeeded = queryMm3s->intersects + 1;
  /* Note: after Bug 7375 `queryMm3s->intersects' may validly be < 0
   * (e.g. for LIKEP `+foo bar').  Though it should still be >= -1,
   * so this check should not trigger:
   */
  if (nlogisetIndexableNeeded < 0) nlogisetIndexableNeeded = 0; /* sanity */

  for (i = num = 0; i < rp->numsets; i++)
    {
      rpset = &rp->sets[i];
      fs = rpset->fs;
      if (i < MAXSELS) gains[i] = RPPM_MAX_WT;          /* WTF user-settable*/
      if (fs != FDBISPN && fs != FDBIS_CANLINEARDICTSEARCH)   /* index set */
        {
          if (i < MAXSELS)
#if EPI_OS_LONG_BITS == EPI_HUGEUINT_BITS
            tblfreqs[i] = (long)fs->wi.ndocs;
#else /* EPI_OS_LONG_BITS != EPI_HUGEUINT_BITS */
            tblfreqs[i] = (fs->wi.ndocs > (EPI_HUGEUINT)MAXLONG ? MAXLONG :
                         (long)fs->wi.ndocs);
#endif /* EPI_OS_LONG_BITS != EPI_HUGEUINT_BITS */
          sets[num++] = fs;
          switch (rpset->logic)
            {
            case LOGIAND:  nlogiandIndexable++; break;
            case LOGISET:  nlogisetIndexable++; break;
            case LOGINOT:  nloginotIndexable++; break;
            case LOGIPROP:
              /* LOGIPROP should have been excluded in openrppm(); fall thru*/
              /* Bug 3905: yap if unknown/unhandled logic types seen: */
            default:       goto unknownSetLogic;
            }
        }
      else                                              /* non-indexed set */
        {
          if (i < MAXSELS) tblfreqs[i] = 0;             /* i.e. unknown */
          switch (rpset->logic)
            {
            case LOGIAND:
              break;
            case LOGISET:
              /* This set might satisfy an intersect on some row(s),
               * reducing the number of indexable intersects needed
               * for that row.  But we won't know until
               * post-processing, so reduce `nlogisetIndexableNeeded'
               * so we don't drop such rows.  This could reduce it to
               * 0 (if intersects was less than the number of
               * unindexable SET logic sets), making it linear; must
               * do so if `@' given (!intersectsIsArbitrary) so we
               * respect user-given intersects; e.g. `aa /bb /cc @1'
               * could match a row with just `/bb /cc' and not `aa'.
               * But for arbitrary (LIKEP and no `@') intersects, we can
               * alter effective intersects, so do so (keep `...Needed'
               * above 0) just to avoid linear search.  This will differ
               * from linear search intersect computation, because linear
               * doesn't know what is indexable:
               */
              if (nlogisetIndexableNeeded > (mm->intersectsIsArbitrary ? 1:0))
                nlogisetIndexableNeeded--;
              break;
            case LOGINOT:
              break;
            case LOGIPROP:
              /* LOGIPROP should have been excluded in openrppm(); fall thru*/
            default:
              /* Bug 3905: yap if unknown/unhandled TXLOGITYPE: */
            unknownSetLogic:
              if (rpset->term)
                term = rpset->term;
              else if (rpset->sel->lst[0])
                term = rpset->sel->lst[0];
              else
                term = CHARPN;
              putmsg(MERR, fn,
    "Internal error: Unknown/unexpected logic type %d for query set %d%s%s%s",
                     (int)rpset->logic, (int)rpset->sel->orpos,
                     (term ? " (`" : ""), (term ? term : ""),
                     (term ? "')" : ""));
              break;
            }
        }
    }

  /* KNG 991025 `intersects' could be > nlogiset, if nlogiset == 0 or
   * user set it high with `@':
   */
  if (nlogisetIndexableNeeded > nlogisetIndexable)
    nlogisetIndexableNeeded = nlogisetIndexable;
  rppm_setwts(rp, gains, tblfreqs);

  /* minsets/intersects logic should be centralized in get3eqsapi() now,
   * for consistency amongst fdbi_get(), get3eqsapi(), rppm_precomp():
   */
  if (op == FOP_MMIN)
    {
      minsets = 0;
      allmatch = 0;
    }
  else
    {
      minsets = nlogiandIndexable + nlogisetIndexableNeeded;
      allmatch = (minsets == nlogiandIndexable + nlogisetIndexable);
    }

  /* Now that prereqs (e.g. `minsets', `nloginotIndexable') set, check for
   * `w/N': may be able to use index to help search.  Do before
   * ...PostProcReason() call, since we may set
   * `multiExprWithinNPostProc' which is needed by former:
   */
  if (queryMm3s->withincount > 0 &&             /* numeric range */
      queryMm3s->delimseq &&                    /* old-style wtf */
      nloginotIndexable == 0 &&                 /* cannot do NOTs WTF */
      (fi->flags & FDF_FULL) &&                 /* have word locs */
      minsets > 1 &&                            /* need to do delim */
      /* ok to use index for within: */
      (TXindexWithin & (API3WITHIN_GET_UNIT(queryMm3s->withinmode) ==
                        API3WITHINCHAR ? TXindexWithinFlag_Chars :
                        TXindexWithinFlag_Words)))
    {
      doWithinN = 1;
      if (!TXfdbiInitWithinN(fi, sets, num, queryMm3s->withincount,
                             queryMm3s->withinmode, &maxwithindiff,
                             &maxwithinhalf, &multiExprWithinNPostProc))
        goto err;
    }

  switch (op)                                           /* Set *nopost: */
    {
    case FOP_RELEV:                                     /* liker */
    case FOP_NMM:                                       /* like3 */
    case FOP_MMIN:                                      /* likein */
      *nopost = nopostmm = 1;
      break;
    case FOP_PROXIM:                                    /* likep */
      *nopost = nopostmm = 0;
      /* We can avoid Metamorph _search_ validation (getmmapi()) if
       * there's no "w/" operator, OR there's only one set and we're
       * doing old-style delimiter checking (if the delimiters are the
       * same, it's effectively old-style checking as well).  All
       * other post-process conditions can be handled by running the
       * pattern matchers in rppm_rankbuf() (and we may not want the
       * min-sets requirement of full Metamorph):
       */
      if (!TXfdbiMetamorphPostProcReason(rp, queryMm3s, fi,
                                         multiExprWithinNPostProc))
        {
          nopostmm = 1;
        }
      /* Violating these conditions requires a post-process, but we
       * don't need to getmmapi()-match the rows; we can handle it with
       * the pattern matchers in rppm_rankbuf():
       */
      if (rp->flags & RPF_SETNEEDSPOST)
        /* `fi->postmsg' probably already set for this: */
        postReason = "A query term needs post-processing";
      else if (rp->numsets != rp->numidx)       /* some set(s) not indexable*/
        {
          if ((*queryMm3s->sdexp || *queryMm3s->edexp) &&
              rp->nloginot > 0)
            postReason = "`w/' delimiters used with NOT term(s)";
          else
            postReason = unindexableTerms;
        }
      else if (!(fi->flags & FDF_FULL))
        {
          postReason = "Metamorph index not inverted (for computing ranks)";
          fdbi_setpostmsg(fi, fdbi_prmsg(CHARPN, CHARPN,
                       "Index `%.*s' is not Metamorph inverted (for ranking)",
                         (int)(strlen(fi->tokfn) - (sizeof(FDBI_TOKSUF) - 1)),
                                         fi->tokfn));
        }
      else
        postReason = TXfdbiMetamorphPostProcReason(rp, queryMm3s, fi,
                                                   multiExprWithinNPostProc);
      if (postReason)
        {
          /* Do not report `Unindexable term(s) in query': can
           * appear with linear-dict and linear msg for e.g.
           * qminprelen 0 query `*foo':
           */
          if (!fi->postmsg && postReason != unindexableTerms)
            fdbi_setpostmsg(fi, TXstrdup(TXPMBUFPN, fn, postReason));
        }
      else
        *nopost = 1;
      break;
    case FOP_MM:                                        /* like */
    default:
      *nopost = nopostmm = 0;
      if (rp->flags & RPF_SETNEEDSPOST)
        /* `fi->postmsg' probably already set for this: */
        postReason = "A query term needs post-processing";
      else if (rp->numsets != rp->numidx)       /* some sets not indexable */
        {
          if ((*queryMm3s->sdexp || *queryMm3s->edexp) &&
              rp->nloginot > 0)
            postReason = "`w/' delimiters used with NOT term(s)";
          else
            postReason = unindexableTerms;
        }
      else
        postReason = TXfdbiMetamorphPostProcReason(rp, queryMm3s, fi,
                                                   multiExprWithinNPostProc);
      if (postReason)
        {
          /* Do not report `Unindexable term(s) in query': can
           * appear with linear-dict and linear msg for e.g.
           * qminprelen 0 query `*foo':
           */
          if (!fi->postmsg && postReason != unindexableTerms)
            fdbi_setpostmsg(fi, TXstrdup(TXPMBUFPN, fn, postReason));
        }
      else
        *nopost = nopostmm = 1;
      break;
    }

  /* alpostproc reporting of fi->postmsg delayed until linear-search msg */

  infthresh = rp->vals[RVAR_INFTHRESH].gain;
  indexthresh = rp->vals[RVAR_INDEXTHRESH].gain;
  if (indexthresh == 0) indexthresh = MAXINT;
  optlikewithnots = (fi->dbtbl != (DBTBL *)NULL &&
                     fi->dbtbl->ddic != (DDIC *)NULL &&
                     fi->dbtbl->ddic->optimizations[OPTIMIZE_LIKE_WITH_NOTS]);
  sort = (rp->numidx <= FDBI_MAX_SELSORT ? selsort : qsort);
  del = fi->dbi->del;
  if (del != BTREEPN && btreeisnew(del))
    del = BTREEPN;                                      /* optimization */

  /* Now that minsets is computed, check for linear-dictionary search.
   * May be able to turn some unindexable sets into no-post linear indexable;
   * if so, we need to re-compute minsets etc. above:
   */
  if (!didlineardictchk)
    {
      didlineardictchk = 1;
      /* Linear dictionary search replaces either a linear table
       * search (if no other indexable sets) or post-process (if other
       * index sets).  Linear-dict is faster than linear-table search.
       * Linear-dict probably makes overall query faster than post-process
       * if* binary-index-sets index result set is large (e.g. single
       * and/or high-freq term), but likely *slower* if
       * binary-index-sets index result set is small (e.g. low-freq
       * term or many sets ANDed).  So sometimes we do not want
       * linear-dict search even if we can: determining where to draw
       * the line would involve a reliable search-cost function for
       * each set and the query.  For now, a SWAG heuristic:
       */
      if (minsets <= 1)                         /* lineardict likely helps */
        {
          if (TXallineardict)                   /*   and is allowed */
            {
              savidx = rp->numidx;
              for (i = 0; i < rp->numsets; i++)
                {
                  rpset = rp->sets + i;
                  if (rpset->fs != FDBIS_CANLINEARDICTSEARCH) continue;
                  /* NOTE: see similar open login in openrppm(): */
                  rpset->fs = openfdbis(fi, mq, i, rpset, 1, &osindg,
                                        &osovermaxset);
                  /* wtf we cannot back out of maxsetwords counts,
                   * so bail on error like normal set, do not close
                   * and punt to post-process:
                   */
                  if (rpset->sel->mm3s->denymode == API3DENYERROR)
                    {
                      if (osovermaxset || fdbi_getovermaxwords(fi)) goto err;
                    }
                  if (rpset->fs != FDBISPN &&
                      rpset->fs != FDBIS_CANLINEARDICTSEARCH)
                    {
                      /* linear-dict should not require post-proc (assured
                       * by fdbi_spmcandictsearch()), but maybe we're over
                       * maxsetwords or something:
                       */
                      if (!osindg)              /* post-proc needed */
                        {
                          if (rpset->logic == LOGINOT)
                            rpset->fs = closefdbis(rpset->fs);
                          else
                            rp->flags |= RPF_SETNEEDSPOST;
                        }
                      if (rpset->fs != FDBISPN) rp->numidx++;
                    }
                }
              if (rp->numidx != savidx)         /* successfully opened some */
                {
                  fi->lineardictmsg = TXfree(fi->lineardictmsg);
                  goto recompute;
                }
              goto linmsgtopost;
            }
        }
      else                                      /* lineardict does not help */
        {
        linmsgtopost:
          /* Lineardict would not help; post-process instead.
           * So do not suggest lineardict to user; punt the message
           * to postmsg if none set:
           */
          if (fi->postmsg == CHARPN)
            {
              fi->postmsg = fi->lineardictmsg;
              fi->lineardictmsg = CHARPN;
            }
          else if (fi->lineardictmsg != CHARPN)
            fi->lineardictmsg = TXfree(fi->lineardictmsg);
        }
    }
  for (i = 0; i < rp->numsets; i++)             /* clean up */
    if (rp->sets[i].fs == FDBIS_CANLINEARDICTSEARCH)
      rp->sets[i].fs = FDBISPN;

  fh = fi->fgfh;
  fheap_clear(fh);
  if (allmatch && (optlikewithnots || nloginotIndexable == 0))
    {                                           /* allmatch re-order*/
      qsort(sets, num, sizeof(FDBIS *), cmpset_initial);
      fheap_setcmp(fh, fdbi_get_heapcmp);       /* NOTs only; hit off. ign. */
      /* NOTE: we assume openfdbis() sets hip and hip->loc to <= 0,
       * before getnext().  This allows us to fill the heap here, but
       * postpone getnext() until the last minute (or even avoid it):
       */
      for (i = num - nloginotIndexable; i < num; i++)
        if (!fheap_insert(fh, sets[i])) goto err;
    }
  else
    fheap_setcmp(fh, ((fi->flags & FDF_FULL) ? fdbi_get_heapcmp_full :
                      fdbi_get_heapcmp));       /* reset in case changed */
  uberrank = 0L;
  TXsetrecid(&curtok, (EPI_OFF_T)(0L));
  currecid = curtok;
  nplinf = num;
  if (infthresh > 0)                            /* if likepinfthresh set */
    {
      nplinf = 0;
      for (i = 0; i < num; i++)
        if (sets[i]->wi.ndocs <= (EPI_HUGEUINT)infthresh) nplinf++;
    }
  if (FdbiTraceIdx >= 3)
    putmsg(MINFO, CHARPN,
     "allmatch: %d and: %d set: %d not: %d minsets: %d maxwithindiff: %d maxwithinhalf: %d doWithinN: %d",
           allmatch, nlogiandIndexable, nlogisetIndexable, nloginotIndexable,
           minsets, (int)maxwithindiff, (int)maxwithinhalf, doWithinN);

  /* Report linear-dict message, or map to post-proc message: */
  if (fi->lineardictmsg != CHARPN &&            /* could use linear dict. */
      !TXallineardict)                          /*   but was not allowed */
    {
      lineardictyap = 1;
      /* wtf Bug 3908 we do not know which set caused `fi->lineardictmsg',
       * so must use "global" mm->acp->denymode and not the set's
       * SEL.mm3s->denymode:
       */
      if ((i = mm->acp->denymode) != API3DENYSILENT)
        putmsg((i == API3DENYWARNING ? MWARN : MERR) + UGE, CHARPN,
               "Query `%s' would require linear dictionary search: %s",
               queryMm3s->query, fi->lineardictmsg);
      if (i == API3DENYERROR) goto err;
    }

  /* Report post-proc message: */
  /* wtf Bug 3908 global vs. set `alpostptoc'? */
  if (!*nopost &&                               /* need post-proc */
      !mm->acp->alpostproc)                     /*   but not allowed */
    {
      /* Do not report if we have no msg and we reported linear-dict:
       * linear-dict probably "stole" our msg, and we want to bias
       * the user towards allinear instead of alpostproc:
       */
      /* wtf Bug 3908 global vs. set `denymode'? */
      if ((i = mm->acp->denymode) != API3DENYSILENT &&
          (!lineardictyap || fi->postmsg != CHARPN))
        putmsg((i == API3DENYWARNING ? MWARN : MERR) + UGE, CHARPN,
               needpost, queryMm3s->query,
               (fi->postmsg != CHARPN ? ": " : ""),
               (fi->postmsg != CHARPN ? fi->postmsg : ""));
      if (i == API3DENYERROR) goto err;
      *nopost = nopostmm = 1;                   /* proceed w/o post-proc */
    }

  /* Do linear check after FdbiTraceIdx reporting of minsets etc.: */
  linearReason = NULL;
  if ((num = rp->numidx) == 0)
    linearReason = (fi->novermaxsetwords > 0 || fi->novermaxwords > 0 ?
                    "No indexable terms left in query" :
                    "No indexable terms in query");
  else if (num == nloginotIndexable)
    linearReason = (fi->novermaxsetwords > 0 || fi->novermaxwords > 0 ?
                    "Indexable terms left are all NOT sets" :
                    "Indexable terms are all NOT sets");
      /* KNG 040407 If query could be completely resolved with only
       * unindexable sets matching, then we might miss such rows using
       * the index, so go linear.  E.g. if intersects is less than unindexable
       * SET sets, and there's no indexable AND sets:
       */
  else if (minsets <= 0 && op != FOP_MMIN)
    linearReason = (fi->novermaxsetwords > 0 || fi->novermaxwords > 0 ?
                    "Match possible entirely with unindexable or dropped terms" :
                    "Match possible entirely with unindexable terms");
  if (linearReason)
    {
      TXfdbiLinearReason = linearReason;
      ret = 0;                                  /* linear search */
      goto done;
    }

  if (TXrecidvalid2(&FdbiTraceRecid))
    {
      fdbi_rankrecid_trace(fi, FdbiTraceRecid, sets, num);
      goto ok;
    }

  /* KNG 20080124 Bug 2022: *any* post-process may reduce final hits,
   * not just a need-post-MM:
   */
  if (icb != FDBIICBPN) icb(usr, !*nopost);

nexttoken:
  /* Lossy speed optimization: bail after indexthresh records.
   * This will drop (possibly good) rows and break indexcount:
   * WTF set a global flag when we do this?  xx.minhits is subtracted
   * from indexcount in setf3dbi(), so $indexcount may not be >= indexthresh.
   */
  if (fi->indexcount >= (EPI_OFF_T)indexthresh) goto ok;

  /* Note: some of this logic also in rppm_rankbest(): */
  if (allmatch)                                 /* need all AND/SET sets */
    {                                           /*   but might have NOTs */
      /*   We need the next token where all AND/SET sets are present.
       * So repeatedly loop over them, getting the next hit >= curtok.
       * If a hit is > curtok, then start over.  NOTE: this assumes
       * `sets' has AND/SET sets before NOTs (cmphit_initial sort).
       * Once we have AND/SET sets, check NOT heap (if optlikewithnots).
       *   We also ordered by tblfreq secondarily: when we start over
       * at a new curtok, we start with the least-frequent sets first,
       * so fewer get-next calls may be needed (can save 0-60%+ CPU).
       */
      if (!optlikewithnots && nloginotIndexable > 0) goto nextnotall;
      num = rp->numidx - nloginotIndexable;
    nextallmatch:
      skip = -1;
    startoverallmatch:
      for (i = 0; i < num; i++)                 /* get all AND/SET sets */
        {
          if (i == skip) continue;              /* already got this set */
          fs = sets[i];
          if (!fs->getnext(fs, curtok)) goto ok;/* no more hits at all */
          if (TXgetoff2(&fs->hip->loc) != TXgetoff2(&curtok))
            {                                   /* different (later) token */
              curtok = fs->hip->loc;            /* new target for all sets */
              skip = i;                         /* already got this set */
              goto startoverallmatch;           /* start over w/least freq. */
            }
        }
      /* Now deal with NOTs.  Heap top is earliest next NOT hit: */
      while (fheap_num(fh) > 0 &&               /* still NOTs left */
             TXrecidcmp(&(fs=(FDBIS*)fheap_top(fh))->hip->loc, &curtok) <= 0)
        {
          if (TXrecidcmp(&fs->hip->loc, &curtok) == 0)
            goto nextallmatch;                  /* there's a NOT at curtok */
          fheap_deletetop(fh);
          if (fs->getnext(fs, curtok) &&        /* got another hit */
              !fheap_insert(fh, fs))
            goto err;
        }
      /* Compile/decode `hits' array (for within-processing, ranking, LIKEIN),
       * then sort it (for ranking, LIKEIN?):
       */
      for (i = 0; i < num; i++)
        {
          hits[i] = h = sets[i]->hip;
          if (h->decodefunc != FDBIHICBPN &&    /* decode if needed */
              !h->decodefunc(h->decodeusr))
            goto err;
        }
      if (num > 1 && !(rpf & RPF_RANKTBLFREQ))  /* NOTE: selsort() hardcoded*/
        sort(hits, num, sizeof(hits[0]), cmphit_allmatch);

      /* Do `w/N' processing, but only if we've got at least 2 sets to
       * work on (otherwise there's nothing to compare).  It's worth
       * doing this work even if other non-index sets need to be checked
       * in post-process: maybe the index sets are already too far apart:
       */
      if (doWithinN && num > 1)
        switch (TXfdbiIsWithinNAllMatch(hits, num, nlogiandIndexable, minsets,
                                        maxwithinhalf, maxwithindiff,
                                        queryMm3s->withinmode, fi->omfh,
                                        fi->wordHeap2))
          {
          case 1:  break;                       /* w/N match */
          case 0:  goto nextallmatch;           /* not a match */
          case -1:                              /* error */
          default: goto err;
          }
    }
  else                                          /* optional sets possible */
    {
      /* `sets' array contains curtok-hit sets (or all sets if start),
       * in unknown order.  `fh' heap may contain other, past-curtok
       * sets.  We get-next on curtok sets, throw 'em in the heap, and
       * pull off the next hit into `sets' and `hits', making `hits'
       * ordered by token/hit-offset and containing `num' elements:
       */
    nextnotall:
      for (i = 0; i < num; i++)                 /* get next on current token*/
        {
          fs = sets[i];
          if (fs->getnext(fs, curtok))
            {
              h = fs->hip;
              if (h->decodefunc != FDBIHICBPN && /* decode if needed */
                  !h->decodefunc(h->decodeusr))
                goto err;
              if (!fheap_insert(fh, fs)) goto err;
            }
          else if ((infthresh > 0) && (fs->set->tblfreq <= infthresh))
            nplinf--;                           /* 1 less possible non-inf */
        }
      if (fheap_num(fh) == 0) goto ok;          /* nobody left; index done */
      sp = sets;
      do                                        /* pull off all sets == top */
        {
          *(sp++) = (FDBIS *)fheap_top(fh);
          fheap_deletetop(fh);
        }
      while (fheap_num(fh) > 0 &&TXgetoff2(&((FDBIS*)fheap_top(fh))->hip->loc)
             == TXgetoff2(&(*sets)->hip->loc));
      num = sp - sets;
      if (num < minsets) goto nextnotall;       /* check min. total sets */
      nand = 0;
      for (i = 0; i < num; i++)                 /* check logic */
        {
          switch (sets[i]->set->logic)
            {
            case LOGIAND:    nand++;    break;
            case LOGINOT:    goto nextnotall;   /* no NOTs allowed */
            case LOGISET:
              /* LOGISET ok if minsets (above) and nlogiand (below) are ok */
              break;
            case LOGIPROP:                      /* excluded by openrppm() */
            default:                            /* "" */
              break;
            }
          hits[i] = sets[i]->hip;
        }
      if (nand != nlogiandIndexable) goto nextnotall;  /*chk ANDs/intersects*/

      if (doWithinN && num > 1)                 /* do `w/N' processing */
        switch (TXfdbiIsWithinNSomeMatch(hits, num, nlogiandIndexable, minsets,
                                         maxwithinhalf, maxwithindiff,
                                         queryMm3s->withinmode, fi->omfh,
                                         fi->wordHeap2))
          {
          case 1:  break;                       /* w/N match */
          case 0:  goto nextnotall;             /* not a match */
          case -1:                              /* error */
          default: goto err;
        }
    }
  for (i = 0; i < num; i++)                     /* reset `curHit' pointers */
    hits[i]->curHit = 0;
  curtok = (*hits)->loc;

  /* Translate token to recid.  Must do this here because we're
   * checking for deleted records and the `and' list immediately
   * after.  Would ideally like to postpone until actual result rows
   * read by SQL get-next...:
   */
  currecid = TXfdbiGetRecidAndAuxData(fi, TXgetoff2(&curtok), NULL,
                                      &curauxfld);
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, CHARPN, "Hit at token 0x%wx (recid 0x%wx)",
           (EPI_HUGEINT)TXgetoff2(&curtok), (EPI_HUGEINT)TXgetoff2(&currecid));
  if (!TXrecidvalid2(&currecid)) goto nexttoken; /* corrupt index */
  if (op == FOP_MMIN)
    {
      nterms = btsearch(fdbi_getdbi(fi)->ct, sizeof currecid, &currecid);
#ifndef NO_EMPTY_LIKEIN
      tv = TXgetoff2(&nterms);
      ns = (int)(tv & 0xFF);
      fup = (int)((tv & 0xFF00) >> 8);
      np  = (int)((tv & 0xFF0000) >> 16);
      if (np != 0 || fup != 0)
        {
          memset(hitsel, 0, sizeof(hitsel));
          maxword = 0;
          for (j = 0; j < num; j++)
            {
              h2 = hits[j];
              for (lp = h2->hits, le = lp + h2->nhits; lp < le; lp++)
                {
                  if (*lp > maxword)
                    {
                      if (*lp < maxword)
                        {
                          putmsg(MWARN, fn, "Word out of bounds");
                          continue;
                        }
                      else
                        maxword = *lp;
                    }
                  hitsel[*lp]++;
                }
            }
          for(j = 0; j < np; j++)
            {
              if (!hitsel[j])
                goto nexttoken;                 /* Not a hit */
            }
          for ( ; ns && (dword)j < maxword; j++)
            {
              if (hitsel[j] > 0) ns--;
            }
          if (ns) goto nexttoken;
          rank = (fup ? 500 : 1000);
        }
      else
#endif /* EMPTY_LIKEIN */
      if (num <= nterms.off - 1) goto nexttoken;
      else
        rank = 1000;
    }

  /* If present, AND with `and' list (e.g. from other WHERE clause's index):*/
  if (and != BTREEPN)
    {
      /* A btsearch() of a 10,000-recid tree is as fast as a linear
       * btgetnext() loop of just 4 recids, so don't bother with a
       * getnext-type merge like above, even if we think we'd be close:
       */
      x = btsearch(and, sizeof(currecid), &currecid);
      if (!TXrecidvalid2(&x)) goto nexttoken;
    }

  /* If we have `mcb', call it to see if this record matchs.  We do
   * this as early as possible to avoid the work of decoding and
   * ranking records that don't match, and to keep indexcount sane.
   * Note that we're calling this before the delete check, so mcb()
   * cannot assume this is a valid record.  (mcb() will probably reject
   * more records than the delete check; that's why we do it first.)
   */
  if (mcb != FDBIMCBPN && !mcb(usr, currecid, curauxfld)) goto nexttoken;

  /* Ignore if this is a deleted record.  We must check here for 2 reasons:
   * 1) indexcount must always be accurate, so check before incrementing it
   * 2) heap maintained by i3dbfinsert() should not have deleted records,
   *    otherwise a high-rank deleted record might bump a low-rank valid one
   * -KNG 980402
   */
  if (del != BTREEPN)
    {
      x = btsearch(del, sizeof(currecid), &currecid);
      if (TXrecidvalid2(&x)) goto nexttoken;    /* deleted record: skip */
    }

  fi->indexcount++;                             /* regardless of likeprows */

  /* Another lossy optimization: If all terms are over the infinity threshold,
   * don't bother ranking.  Must be after indexcount so it's still accurate:
   */
  if (infthresh > 0)                            /* likepinfthresh is set */
    {
      if (allmatch && nloginotIndexable == 0)   /* nplinf always valid */
        {
          if (nplinf > 0) goto infcont;
          else goto infskip;
        }
      for (i = 0; i < num; i++)
        if (hits[i]->set->tblfreq <= infthresh) goto infcont;
    infskip:
      /* WTF fake up a rank; WTF WTF WTF assume non-index sets are there?: */
      rank = (RPPM_BEST_RANK*num)/(nlogiandIndexable + nlogisetIndexable);
      i = rcb(usr, currecid, curauxfld, rank);
      if (i == RPPM_MIN_WT - 1) fi->indexcount--;       /* reject */
      else uberrank = i;
      goto nexttoken;
    }
infcont:

  rp->curRecid = currecid;                      /* for RPPM tracing */
#ifndef NO_EMPTY_LIKEIN
  if (op != FOP_MMIN)
#endif /* EMPTY_LIKEIN */
    rank = (num == 1 ? rankBestOneFunc(rp, hits[0], SIZE_TPN) :
            rankBestFunc(rp, hits, num, SIZE_TPN));  /* rank this document */
  /* KNG 991103 don't call rcb() if it would reject it anyway; saves time: */
  if (rank < uberrank && uberrank > 0)
    {
      if (TXtraceIndexBits & 0x400000)          /* Bug 6796 tracing */
        putmsg(MINFO, __FUNCTION__,
       "Discarded recid 0x%08wx rank %d: Less than current threshold rank %d",
               (EPI_HUGEINT)TXgetoff2(&currecid), (int)rank, (int)uberrank);
      goto nexttoken;
    }
  i = rcb(usr, currecid, curauxfld, rank);
  if (i == RPPM_MIN_WT - 1) fi->indexcount--;   /* reject */
  else uberrank = i;
  goto nexttoken;                               /* start over w/next recid */

ok:
  ret = 1;
  goto done;
err:
  ret = -1;
done:
  if ((rp = fi->rp) != RPPMPN)
    {
      rppm_setflags(rp, RPF_RANKTBLFREQ, 0);    /* back to full ranking */
      /* no need for RPF_LOGICHECK: new-records search sets it in
       * 3dbindex.c; anyone else doing ranking is also doing a post-process
       * Metamorph?   KNG 980710
       * we _do_ need to set RPF_LOGICHECK, at least for LIKEP set logic;
       * possibly others?  KNG 991026
       */
      rppm_setflags(rp, RPF_LOGICHECK, 1);      /* since fdbi_get() done */
      switch (op)
        {
        case FOP_MM:                            /* LIKE */
        case FOP_NMM:                           /* LIKE3 */
        case FOP_MMIN:                          /* LIKEIN */
          /* Use mminfo ranking from now on; don't waste time ranking:
           * KNG 980701
           */
          rppm_setflags(rp, RPF_RANKMMINFO, 1);
          break;
        case FOP_PROXIM:                        /* LIKEP */
          /* Force a Metamorph match in rppm_rankbuf(), but only
           * if we need it for matching, not for ranking:
           * KNG 980710
           */
          if (!nopostmm) rppm_setflags(rp, RPF_MMCHECK, 1);
          break;
        }
    }
  sets = TXfree(sets);
  hits = TXfree(hits);
  return(ret);
}

int
fdbi_reinit(fi)
FDBI    *fi;
/* Returns -1 on error, 0 if ok.  WTF
 */
{
  (void)fi;
  return(0);
}

BTLOC
fdbi_search(fi, wd, srch, pct)
FDBI    *fi;
char    *wd;
int     srch;
int     *pct;
/* Searches for given word `wd'.  Used when accessing as table
 * (indexaccess=1).  `srch' is bt->search value (-1 for after, 1 for
 * before).  Sets `*pct' to percentage.  Returns valid BTLOC (0) if
 * found; fdbi_getnextrow() may be called multiple times.
 */
{
  static CONST char     fn[] = "fdbi_search";
  BTLOC                 loc;

  if (!(fi->mode & PM_SELECT))
    {
      putmsg(MERR + UGE, fn, BadSelect, fi->tokfn);
      TXsetrecid(&loc, (EPI_OFF_T)(-1));
      return(loc);
    }

  btsetsearch(fi->bt, srch);
  loc = btsearch(fi->bt, strlen(wd), wd);
  *pct = btgetpercentage(fi->bt);
  return(loc);
}

int
fdbi_setmarker(fi)
FDBI    *fi;
{
  if (TxIndexDump) fdbi_dump(fi);

  btsetsearch(fi->bt, BT_SEARCH_BEFORE);
  return(btreesetmarker(fi->bt));
}

int
fdbi_rewind(fi)
FDBI    *fi;
{
  rewindbtree(fi->bt);
  return(1);
}

BTLOC
fdbi_getnextrow(fi, wd, rowCount, occurrenceCount)
FDBI            *fi;            /* (in/out) Metamorph index handle */
CONST char      **wd;           /* (out) word */
ft_int64        *rowCount;      /* (out) # documents containing `*wd' */
ft_int64        *occurrenceCount; /* (out) # total occurrences of `*wd' */
/* Gets next row of data from `fi' when accessing as a table
 * (indexaccess=1).  Sets `*wd' to Word, `*rowCount' to doc count,
 * `*occurrenceCount' to total occurrences of word (or 0 if unknown,
 * e.g. INDEX_MM index).  Caller must dup info immediately.  May be
 * called after 1 call to fdbi_search().
 * Returns valid BTLOC (0) if ok, invalid on error or EOF.
 */
{
  static CONST char     fn[] = "fdbi_getnextrow";
  byte                  *buf, *s, *bufEnd;
  size_t                bufsz;
  BTLOC                 loc;
  EPI_HUGEUINT          u;

  if (!(fi->mode & PM_SELECT))
    {
      putmsg(MERR + UGE, fn, BadSelect, fi->tokfn);
      TXsetrecid(&loc, (EPI_OFF_T)(-1));
      goto done;
    }

  buf = fi->pgbuf;
  bufsz = fi->pgbufsz;
  loc = btgetnext(fi->bt, &bufsz, buf, BYTEPPN);/* wtf search next? */
  if (!TXrecidvalid2(&loc)) goto done;
  bufEnd = buf + bufsz;
  *wd = (char *)buf;
  s = buf + strlen((char *)buf) + 1;
  if (TXgetoff2(&loc) < (EPI_OFF_T)0)           /* single-recid */
    {
      *rowCount = (ft_int64)1;
      *occurrenceCount = (ft_int64)fi->countvsl(s, bufEnd - s);
    }
  else
    {
      if ((int)fi->dbi->version >= 3) s = invsh(s, &u); /* .dat block size */
      s = invsh(s, &u);                         /* ndocs */
      *rowCount = (ft_int64)u;
      if (fi->flags & FDF_FULL)
        {
          s = invsh(s, &u);                     /* nlocs */
          if ((int)fi->dbi->version >= 3) u += (EPI_HUGEUINT)(*rowCount);
          *occurrenceCount = (ft_int64)u;
        }
      else
        *occurrenceCount = (ft_int64)0;         /* no nlocs info */
    }
  if (s > bufEnd)                               /* sanity check */
    putmsg(MERR, fn, "Corrupt count for word `%s' in index %s",
           *wd, getdbffn(fi->bt->dbf));

done:
  return(loc);
}

int
fdbi_allocbuf(fn, ptr, sz, req)
CONST char      *fn;
void            **ptr;
size_t          *sz;
size_t          req;
/* Re-allocs `ptr', currently `*sz' bytes, to at least `req'.
 * Initial `*sz' bytes preserved (some callers count on this).
 */
{
  static TXATOMINT      numFailed = 0;  /* # failed allocs */
#define MAX_FAIL_REPORTS        25
  size_t                n, n2;
  void                  *newbuf;

  if (*sz >= req) return(1);            /* already have enough */

  n = *sz + (*sz >> 1);                 /* reduce future calls: overalloc */
  if (n < 512) n = 512;
  if (n < req) n = req;
  /* Note TXPMBUF_SUPPRESS: suppress errors, we will retry and report.
   * But failure should still cause TXmemGetNumAllocFailures() to
   * increase, so that merge_newitme() will detect it and trigger a
   * merge in hopes of freeing up some memory:
   */
#ifdef EPI_REALLOC_FAIL_SAFE
  if ((newbuf = (*ptr != NULL ? TXrealloc(TXPMBUF_SUPPRESS, fn, *ptr, n) :
                 TXmalloc(TXPMBUF_SUPPRESS, fn, n))) == NULL)
#else /* !EPI_REALLOC_FAIL_SAFE */
  if ((newbuf = TXmalloc(TXPMBUF_SUPPRESS, fn, n)) == NULL)
#endif /* !EPI_REALLOC_FAIL_SAFE */
    {                                   /* alloc failed */
      /* Compute a smaller amount for retry.  Do not reduce overalloc to 0,
       * because that might vastly increase the calls to us, if caller has a
       * linearly increasing array, as we try to alloc a few bytes at a time.
       * Use ~3% overalloc minimum as a compromise between conserving mem
       * and reducing calls:
       */
      n2 = *sz + (*sz >> 5);
      if (n2 < req) n2 = req;
      if (n2 < n)                       /* new try is smaller: attempt it */
        {
#ifdef EPI_REALLOC_FAIL_SAFE
          newbuf = (*ptr != NULL ? TXrealloc(TXPMBUF_SUPPRESS, fn, *ptr, n2) :
                    TXmalloc(TXPMBUF_SUPPRESS, fn, n2));
#else /* !EPI_REALLOC_FAIL_SAFE */
          newbuf = TXmalloc(TXPMBUF_SUPPRESS, fn, n2);
#endif /* !EPI_REALLOC_FAIL_SAFE */
          if (newbuf != NULL &&                 /* new try succeeded */
              numFailed < MAX_FAIL_REPORTS)     /* not too many yaps yet */
            putmsg(MWARN + MAE, fn,
                   "Low memory: fdbi_allocbuf could not alloc %wku bytes, alloced %wku bytes instead%s",
                   (EPI_HUGEUINT)n, (EPI_HUGEUINT)n2,
                   (numFailed + 1 >= MAX_FAIL_REPORTS ?
                    " (suppressing future messages)" : ""));
          TX_ATOMIC_INC(&numFailed);
          n = n2;                       /* n is last alloc, success or fail */
          n2 = n - 1;                   /* preserve n2 < n check below */
        }
      if (newbuf == NULL)               /* all attempt(s) failed */
        {
          txpmbuf_putmsg(TXPMBUFPN, MERR + MAE, fn,
                  "Cannot alloc%s %wku bytes of memory via fdbi_allocbuf: %s",
                         (n2 < n ? " reduced buffer of" : ""),
                         (EPI_HUGEUINT)n, TXstrerror(TXgeterror()));
          return(0);
        }
    }
#ifndef EPI_REALLOC_FAIL_SAFE
  if (*ptr != NULL)
    {
      if (*sz > (size_t)0) memcpy(newbuf, *ptr, *sz);
      *ptr = TXfree(*ptr);
    }
#endif /* !EPI_REALLOC_FAIL_SAFE */
  *ptr = newbuf;
  *sz = n;
  return(1);
}
