#ifndef FDBI_H
#define FDBI_H

#ifndef BTREE_H
#  include "btree.h"
#endif
#ifndef KDBF_H
#  include "kdbf.h"
#endif
#ifndef SIZES_H
#  include "sizes.h"
#endif
#include "unicode.h"
#include "meter.h"


#ifndef RPPM_SETPN              /* WTF avoid include of rppm.h */
typedef struct RPPM_SET_tag     RPPM_SET;
#  define RPPM_SETPN    ((RPPM_SET *)NULL)
#endif
#ifndef RPPMPN                  /* WTF avoid include of rppm.h */
typedef struct RPPM_tag         RPPM;
#  define RPPMPN        ((RPPM *)NULL)
#endif
#ifndef INDEXSTATSPN            /* WTF avoid include of texint.h */
typedef struct INDEXSTATS_tag   INDEXSTATS;
#  define INDEXSTATSPN  ((INDEXSTATS *)NULL)
#endif

#define FDBI_DATSUF             ".dat"
#define FDBI_TOKSUF             ".tok"

/* Alias for getrlex()/getrex() func: */
typedef byte *FDBI_GETHITFUNC ARGS((void *obj, byte *buf, byte *end, int op));
/* Alias for rlexlen()/rexsize() func: */
typedef int FDBI_GETLENFUNC ARGS((void *obj));

/* FDBIHI callback to decode `hits', if `curhit' is NULL: */
typedef int (FDBIHICB) ARGS((void *usr));
#define FDBIHICBPN      ((FDBIHICB *)NULL)

/* Decoded hit info, per recid: */
typedef struct FDBIHI_tag
{
  RECID         loc;            /* current recid */
  byte          *locdata;       /* VSL loc data */
  size_t        locsz;          /*   its size */
  dword         *hits;          /* array of locations in current recid */
  size_t        hitsz;          /* size `hits' is alloced to */
  dword         *hitLens;       /* (opt.) lengths (in words) of each hit */
  size_t        hitLensSz;      /* size `hitLens' is alloced to */
  size_t        nhits;          /* # of locations */
  size_t        curHit;         /* current hit in `hits', for walk/merge/rank*/
  RPPM_SET      *set;           /* set (filled in just before rank call) */
  FDBIHICB      *decodefunc;    /* (opt.) function to decode `hits' later */
  void          *decodeusr;     /* (opt.) user data to pass to `decodefunc' */

  /* These are currently only used for linear searching: */
  size_t        *byteHits;      /* byte-offsets corresponding to `hits' */
  size_t        byteHitsSz;     /* `byteHits' alloced size (bytes) */
  size_t        *byteHitEnds;   /* byte-offset ends corresponding to `hits' */
  size_t        byteHitEndsSz;  /* `byteHitEnds' alloced size (bytes) */
}
FDBIHI;
#define FDBIHIPN        ((FDBIHI *)NULL)
#define FDBIHIPPN       ((FDBIHI **)NULL)
#define FDBIHIPPPN      ((FDBIHI ***)NULL)

typedef enum FDF_tag
{
  FDF_FULL      = (1 << 0),     /* full inversion index */
  FDF_ERROR     = (1 << 1),     /* (internal) error flag */
  FDF_WILDCARD  = (1 << 2),     /* (internal) FDBIW is wildcard */
  FDF_FREEHEAP  = (1 << 3),     /* (internal) FDBIW heap has data */
  FDF_ROOTWORD  = (1 << 4),     /* (internal) FDBIX root word */
  FDF_TOKFHWR   = (1 << 5),     /* (internal) token file handle is writable */
  FDF_TOKMEMWR  = (1 << 6),     /* (internal) token mmap() mem is writable */
  FDF_VSH7      = (1 << 7),     /* openfdbix(): index version is >= 3 */
  FDF_IGNWDPOS  = (1 << 8)      /* (internal) API3...IGNOREWORDPOSITION set */
}
FDF;
#define FDFPN           ((FDF *)NULL)
#define FDF_PUBMASK     (FDF_FULL)

/* Word info record: */
typedef struct FDBIWI_tag
{
  EPI_HUGEUINT  ndocs;  /* number of documents word occurs in */
  EPI_HUGEUINT  nlocs;  /* total number of occurrences in all docs */
}
FDBIWI;
#define FDBIWIPN        ((FDBIWI *)NULL)

/* FDBIX shared .dat read buffer: */
typedef struct FDBIXBUF_tag     FDBIXBUF;

/* Scanner for a raw word; this struct is not really public: */
typedef struct FDBIX
{
  FDBIHI        *hip;           /* hit info ptr (must be first in struct) */
  FDBIHI        hi;             /* hit info (`hip' points here for ormerge) */
  FDBIWI        wi;             /* document/word count info (if known) */
  FDBIHI *      (*getnext) ARGS((struct FDBIX *fx, RECID loc));
  FDF           flags;
  FDBI          *fi;            /* our root object (if available) */
  KDBF          *df;            /* copy of docs/locs handle */
  FDBIXBUF      *fb;            /* non-NULL: shared buf to use */
  byte          *buf;           /* current position in `fb' */
  size_t        bufsz;          /* desired KDBF read size, current buf size */
  EPI_OFF_T     filoff;         /* current file offset into .dat file */
  size_t        totrd;          /* total word data read so far */
  size_t        totsz;          /* total size of word data in file */
  float         lkfactor;       /* VSH7 lookahead factor */
  RECID         curtbloff;      /* table offset of current data */
  int           wdrank;
  char          *wd;            /**< copy of word for msgs */
}
FDBIX;
#define FDBIXPN                 ((FDBIX *)NULL)
#define FDBIXPPN                ((FDBIX **)NULL)

FDBIX  *closefdbix ARGS((FDBIX *fx));
FDBIX  *openfdbix ARGS((KDBF *df, FDF flags, FDBIWI *wi, RECID loc,
               size_t blksz, dword *hits, size_t nhits, char *wd, FDBI *fi));
int     fdbix_seek ARGS((FDBIX *fx, RECID off));
int     fdbix_iserror ARGS((FDBIX *fx));
size_t  fdbix_slurp ARGS((FDBIX *fx, byte **bufp));

#define FDBI_TOKEL_RECIDSZ      TX_ALIGN_UP_SIZE_T(sizeof(RECID))

/* Macros for copying and comparing RECIDs, when TX_ALIGN_BYTES-aligned: */
#if !defined(TX_ALIGN_BYTES) || !defined(EPI_OFF_T_BITS)
error error error;
#endif
#ifdef EPI_TXALIGN_OFF_T_COPY_SAFE
#  define FDBI_TXALIGN_RECID_COPY(d, s) *((RECID *)(d)) = *((RECID *)(s))
#else /* !EPI_TXALIGN_OFF_T_COPY_SAFE */
#  ifdef __GNUC__
/* gcc tries very hard to optimize this memcpy() into an inline EPI_OFF_T
 * assignment, with possible bus error results on 64-bit Sparc.  It appears
 * we need true byte-pointer args to memcpy() to force a byte-wise copy:
 */
#    define FDBI_TXALIGN_RECID_COPY(d, s)       \
 { char *_d = (char *)(d), *_s = (char *)(s); memcpy(_d, _s, sizeof(RECID)); }
#  else /* !__GNUC__ */
#    define FDBI_TXALIGN_RECID_COPY(d, s)       \
  memcpy((char *)d, (char *)s, sizeof(RECID))
#  endif /* !__GNUC__ */
#endif /* !EPI_TXALIGN_OFF_T_COPY_SAFE */
#ifdef EPI_TXALIGN_OFF_T_CMP_SAFE
#  define FDBI_TXALIGN_RECID_CMP(res, a, b)     (res) = TXrecidcmp(a, b)
#else /* !EPI_TXALIGN_OFF_T_CMP_SAFE */
#  define FDBI_TXALIGN_RECID_CMP(res, a, b)             \
  { RECID _a, _b; FDBI_TXALIGN_RECID_COPY(&_a, a);      \
    FDBI_TXALIGN_RECID_COPY(&_b, b); (res) = TXrecidcmp(&_a, &_b); }
#endif /* !EPI_TXALIGN_OFF_T_CMP_SAFE */

typedef struct TXfdbiIndOpts_tag
{
  char          **wordExpressions;              /* empty-str-term. list */
  char          *locale;                        /* (opt.) */
  TXCFF         textSearchMode;
  int           indexVersion;
  int           maxSingleLocs;
  size_t        indexmem;                       /* computed not user  */
  TXMDT         indexmeter;
}
TXfdbiIndOpts;

#define TXfdbiIndOpts_INIT_FROM_PROCESS_DEFAULTS(f)             \
  { (f)->wordExpressions = NULL;  /* wtf empty-str? */          \
    (f)->locale = (char *)TXgetlocale();                        \
    if (!globalcp) globalcp = TXopenapicp();                    \
    (f)->textSearchMode = globalcp->textsearchmode;             \
    (f)->indexVersion = TxFdbiVersion;                          \
    (f)->maxSingleLocs = TxFdbiMaxSingleLocs;                   \
    (f)->indexmem = TXcomputeIndexmemValue(TXindexmemUser);     \
    (f)->indexmeter = TXindexmeter;                             \
  }
#define TXfdbiIndOpts_INIT_FROM_INDEX_OPTIONS(f, o)             \
  { (f)->wordExpressions = (o)->wordExpressions;                \
    (f)->locale = (char *)TXgetlocale(); /* wtf get from `o'?*/ \
    (f)->textSearchMode = (o)->textsearchmode;                  \
    (f)->indexVersion = (o)->fdbiVersion;                       \
    (f)->maxSingleLocs = (o)->fdbiMaxSingleLocs;                \
    (f)->indexmem = (o)->indexmem;                              \
    (f)->indexmeter = (o)->indexmeter;                          \
  }
#define TXfdbiIndOpts_INIT_FROM_DBI(f, dbi)                     \
  { (f)->wordExpressions = (dbi)->explist;                      \
    (f)->locale = (dbi)->locale;                                \
    (f)->textSearchMode = (TXCFF)(dbi)->textsearchmode;         \
    (f)->indexVersion = (int)(dbi)->version;                    \
    (f)->maxSingleLocs = TxFdbiMaxSingleLocs;                   \
    (f)->indexmem = TXcomputeIndexmemValue(TXindexmemUser);     \
    (f)->indexmeter = TXindexmeter;                             \
  }

/* ----------------------------- Index search ------------------------------ */

typedef struct FDBIS_tag        FDBIS;
#define FDBISPN         ((FDBIS *)NULL)
#define FDBISPPN        ((FDBIS **)NULL)
/* return val for openfdbis() that indicates set is linear-dict searchable: */
#define FDBIS_CANLINEARDICTSEARCH       ((FDBIS *)1)
#define PMISFDBI        100     /* fake PMIS... type for FDBIS */

/* (Optional) initial callback: */
typedef void (FDBIICB) ARGS((void *usr, int postreduce));
#define FDBIICBPN       ((FDBIICB *)NULL)

/* (Optional) callback with aux data.  Returns nonzero if record matches: */
typedef int (FDBIMCB) ARGS((void *usr, RECID recid, void *auxfld));
#define FDBIMCBPN       ((FDBIMCB *)NULL)

/* Callback for each matching record, with rank.  Returns -1 on error (stop),
 * 0 to continue; > 0: lowest rank value to continue calling:
 */
typedef int (FDBIRCB) ARGS((void *usr, RECID recid, void *auxfld, int rank));
#define FDBIRCBPN       ((FDBIRCB *)NULL)

#ifndef FDBIPN
typedef struct FDBI_tag FDBI;   /* declared in fdbi.c */
#  define FDBIPN  ((FDBI *)NULL)
#endif

#define FDBI_BTPGSZ     BT_REALMAXPGSZ
#define VSH_MAXLEN      8       /* max len of a VSH encoding, in bytes */
#define VSH7_MAXLEN     7       /* max len of a VSH7 encoding, in bytes */
#define VSH_BOTHMAXLEN  (VSH_MAXLEN > VSH7_MAXLEN ? VSH_MAXLEN : VSH7_MAXLEN)
#define FDBI_TOKEN_FLAG 0x80    /* Version 3: this byte starts a new token */

/* internal FDBI usage: */
int  fdbi_allocbuf ARGS((CONST char *fn, void **ptr, size_t *sz, size_t req));
int     fdbi_setpostmsg ARGS((FDBI *fi, char *msg));
FDBIS   *openfdbis ARGS((FDBI *fi, MMQL *ql, int setOrgIdx,
                RPPM_SET *rs, int lineardict, int *indg, int *overmaxset));
FDBIS   *closefdbis ARGS((FDBIS *fs));
int     TXfdbisSetRppmSet ARGS((FDBIS *fs, RPPM_SET *set));

size_t  linkstovsl ARGS((dword *klocs, dword firstkloc, byte *buf, size_t *np,
                         long *toterrs));
size_t  locstovsl ARGS((dword *locs, size_t nlocs, byte *buf, long *toterrs));
int     vsltolocs ARGS((byte *bp, size_t sz, dword *array));
int     countvsl ARGS((byte *bp, size_t sz));
byte    *outvsh ARGS((byte *d, EPI_HUGEUINT n));
byte    *invsh ARGS((byte *s, EPI_HUGEUINT *np));

size_t  linkstovsh7 ARGS((dword *klocs, dword firstkloc, byte *buf, size_t *np,
                          long *toterrs));

size_t  locstovsh7 ARGS((dword *locs, size_t nlocs, byte *buf,long *toterrs));
int     vsh7tolocs ARGS((byte *bp, size_t sz, dword *array));
int     countvsh7 ARGS((byte *bp, size_t sz));
byte    *outvsh7 ARGS((byte *d, EPI_HUGEUINT n));
byte    *invsh7 ARGS((byte *s, EPI_HUGEUINT *np));

int     fdbi_btcmp ARGS((void *ablk, size_t asz, void *bblk, size_t bsz,
                         void *usr));

int     fdbi_exists ARGS((char *name));
RECID   TXfdbiGetRecidAndAuxData ARGS((FDBI *fi, EPI_OFF_T tok,
                                       void **recidPtr, void **auxfld));
FDBI    *openfdbi ARGS((char *name, int mode, FDF flags,
                        CONST char *sysindexParams, DBTBL *dbtbl));
FDBI    *closefdbi ARGS((FDBI *fi));
TXbool  TXfdbiResetGlobalHacks(void);
int     fdbi_get ARGS((FDBI *fi, MMAPI *mm, MMQL *mq, FDBIICB *icb,
                       FDBIMCB *mcb, FDBIRCB *rcb, void *usr, int *nopost,
                       int op, BTREE *also, EPI_OFF_T nandrecs));
EPI_OFF_T   fdbi_getnrecs ARGS((FDBI *fi));
EPI_OFF_T   fdbi_gettotrecs ARGS((FDBI *fi));
FDF     TXfdbiGetFlags ARGS((FDBI *fi));
char    *TXfdbiGetTokenPath ARGS((FDBI *fi));
size_t  TXfdbiGetAuxFieldsSize ARGS((FDBI *fi));
A3DBI   *fdbi_getdbi ARGS((FDBI *fi));
RPPM    *fdbi_getrppm ARGS((FDBI *fi));
int     fdbi_getovermaxwords ARGS((FDBI *fi));
int     fdbi_updatetokaux ARGS((FDBI *fi, RECID recid, void *newAux,
                                RECID newRecid));
int     fdbi_flush ARGS((FDBI *fi));
EPI_OFF_T   fdbi_countrecs ARGS((FDBI *fi));
int     fdbi_reinit ARGS((FDBI *fi));
BTLOC   fdbi_search ARGS((FDBI *fi, char *wd, int srch, int *pct));
int     fdbi_setmarker ARGS((FDBI *fi));
int     fdbi_rewind ARGS((FDBI *fi));
BTLOC   fdbi_getnextrow  ARGS((FDBI *fi, CONST char **wd,
                   ft_int64 *rowCount, ft_int64 *occurrenceCount));


extern int      TXtraceIndexBits;       /* 0x1000 and up for now wtf */
extern int      FdbiTraceIdx, FdbiDropMode, FdbiBonusError;
extern size_t   FdbiReadBufSz;
extern RECID    FdbiTraceRecid;
extern int      TxIndexMmap, TxIndexDump, TxIndexSlurp, TxIndexAppend;
extern int      TxIndexWriteSplit;

const char *TXfdbiGetLinearReason(void);

int     TXgetTraceIndex(void);
int     TXsetTraceIndex(int traceIndex);

/* ------------------------ Index creation/update -------------------------- */

typedef struct WTIX_tag WTIX;
#define WTIXPN  ((WTIX *)NULL)
#define WTIXPPN ((WTIX **)NULL)

typedef enum WTIXF_tag
{
  WTIXF_UPDATE  = (1 << 0),     /* update mode (else creation) */
  WTIXF_FULL    = (1 << 1),     /* full inversion (INDEX_FULL) */
  WTIXF_INTER   = (1 << 2),     /* intermediate merge pile */
  WTIXF_FINISH  = (1 << 3),     /* (internal) wtix_finish() to be called */
  WTIXF_ERROR   = (1 << 4),     /* (internal) severe error(s) encountered */
  WTIXF_VERIFY  = (1 << 5),     /* opened via wtix_verify() */
  WTIXF_SLURP   = (1 << 6),     /* save relative offset of last tok in .btr */
  WTIXF_APPEND  = (1 << 7),     /* (internal) append-only update */
  WTIXF_COMPACT = (1 << 8),     /* open for ALTER TABLE ... COMPACT */
  WTIXF_LOADTOK = (1 << 9),     /* load token file for ALTER TABLE COMPACT */
}
WTIXF;
#define WTIXF_USERFLAGS (WTIXF_UPDATE | WTIXF_FULL | WTIXF_INTER | \
                   WTIXF_VERIFY | WTIXF_SLURP | WTIXF_COMPACT | WTIXF_LOADTOK)

int     TXfdbiApplyVersion ARGS((int newVersion, int *curVersion,
                                 int *curMaxSingleLocs));
int     fdbi_setversion ARGS((int vers));
int     TXfdbiApplyMaxSingleLocs ARGS((int newMaxSingleLocs, int curVersion,
                                       int *curMaxSingleLocs));
int     fdbi_setmaxsinglelocs ARGS((int locs));
WTIX    *openwtix(DBTBL *dbtb, char *field, char *indfile,
                  size_t auxfldsz, TXfdbiIndOpts *options,
                  const TXMMPARAMTBLINFO *orgIdxParamTblInfo,
                  int flags, int orgversion, WTIX *wxparent);
WTIX    *closewtix ARGS((WTIX *wx));
int     wtix_insert ARGS((WTIX *wx, void *buf, size_t sz, void *auxfld,
                          BTLOC at));
int     wtix_insertloc ARGS((WTIX *wx, CONST char *s, size_t sz, void *auxfld,
                             BTLOC at, int loc));
int     wtix_needfinish ARGS((WTIX *wx));
EPI_OFF_T   wtix_estdiskusage ARGS((WTIX *wx, EPI_OFF_T tblsize));
int     wtix_finish ARGS((WTIX *wx));
int     TXwtixGetTotalHits ARGS((WTIX *wx, TXMMPARAMTBLINFO *paramTblInfo));
int     wtix_setperms ARGS((char *name, int mode, int uid, int gid));
int     wtix_btree2list ARGS((BTREE *bt, size_t auxfldsz, int verbatim,
                              byte **list, size_t *sz, size_t *n));
int     wtix_getdellist ARGS((WTIX *wx, BTREE *bt));
int     wtix_getnewlist ARGS((WTIX *wx, BTREE *bt));
int     wtix_setupdname ARGS((WTIX *wx, char *name));
int     wtix_setnoiselist ARGS((WTIX *wx, char **list));
BTLOC   wtix_getnextnew ARGS((WTIX *wx, size_t *sz, void *auxfld));
int     wtix_mergeclst ARGS((WTIX *wx, BTREE *out, BTREE *org, BTREE *ins));
int     wtix_getstats ARGS((WTIX *wx, INDEXSTATS *stats));
int     wtix_verify ARGS((DDIC *ddic, char *tname, char *iname, char *fname,
                          char *ifile, int itype, CONST char *sysindexParams,
                          int verbose));
char   *TXwtixGetLiveTokenPath ARGS((WTIX *wx));
char   *TXwtixGetNewTokenPath ARGS((WTIX *wx));
int     TXwtixGetTokenDescriptor ARGS((WTIX *wx));
size_t  TXwtixGetAuxDataSize ARGS((WTIX *wx));
int     TXwtixCreateNextToken ARGS((WTIX *wx, RECID recid, void *auxfld));
RECID   TXwtixMapOldRecidToNew ARGS((WTIX *wx, RECID oldRecid));
size_t  TXwtixGetIndexMemUsed ARGS((WTIX *wx));

extern size_t   FdbiWriteBufSz;
extern int      TxFdbiVersion;                  /* current/default version */
extern int      TxFdbiMaxSingleLocs;
#define FDBI_MAX_VERSION        3               /* highest version we grok */

#endif /* !FDBI_H */
