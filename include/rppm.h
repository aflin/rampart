#ifndef RPPM_H
#define RPPM_H

#ifndef FDBI_H
#  include "fdbi.h"
#endif
#ifndef HEAP_H
#  include "heap.h"
#endif


/* ---------------------------- Config stuff ------------------------------- */
#define RPPM_CHECK_OVERLAP              /* check & ignore overlapping hits */
#define RPPM_BEST_RANK  1000            /* perfect rank value */
#define RPPM_MAX_WT     1000            /* maximum weight value */
#define RPPM_MIN_WT     -1000           /* minimum weight value */
/* ----------------------------- End config -------------------------------- */

#ifndef RPPM_SETPN                      /* WTF see also fdbi.h */
typedef struct RPPM_SET_tag     RPPM_SET;
#  define RPPM_SETPN    ((RPPM_SET *)NULL)
#endif
#ifndef RPPMPN                          /* WTF see also fdbi.h */
typedef struct RPPM_tag         RPPM;
#  define RPPMPN        ((RPPM *)NULL)
#endif

struct RPPM_SET_tag
{
  SEL           *sel;                   /* pointer into MMAPI list */
  int           orgIdx;                 /* `sel->orpos' sans PMISNOP sets */
  int           eln;                    /* MMAPI->mm->el[n] index number */
  FDBIS         *fs;                    /* if using FDBI index */
  long          tblfreq;                /* table frequency */
  int           gain;                   /* +1023/-1023 gain */
  int           order;                  /* order # (excluding LOGINOT sets) */
  int           cookedtblfreq;          /* pre-computed log x gain value */
  int           likerwt;                /* pre-computed liker weight */
  char          *term;                  /* this set's term (if RppmDebug) */
  TXPMTYPE      type;                   /* PMIS... type (REX, PPM etc.) */
  TXLOGITYPE    logic;                  /* LOGI... value (LOGISET, etc.) */
};
#define RPPM_SETPPN     ((RPPM_SET **)NULL)

typedef enum RVAR_tag
{
  RVAR_PROXIMITY,
  RVAR_LEADBIAS,
  RVAR_ORDER,
  RVAR_DOCFREQ,
  RVAR_TBLFREQ,                         /* must be last knob */
#define RVAR_KNOBNUM    RVAR_ALLMATCH   /* # gain/importance knobs */
  RVAR_ALLMATCH,
  RVAR_INFTHRESH,
  RVAR_INDEXTHRESH,
  RVAR_NUM                              /* must be last */
}
RVAR;

typedef struct RVAL_tag
{
  int           gain;                   /* 0-1023 */
}
RVAL;
#define RVALPN  ((RVAL *)NULL)

typedef enum RPF_tag
{
  RPF_RANKTBLFREQ       = (1 << 0),     /* liker: rank on table freq only */
  RPF_LOGICHECK         = (1 << 1),     /* new records: check logic */
  RPF_SETNEEDSPOST      = (1 << 2),     /* post search needed by set(s) */
  RPF_RANKMMINFO        = (1 << 3),     /* like: rank on infommapi() only */
  RPF_MMCHECK           = (1 << 4),     /* verify hits with getmmapi() */
  RPF_USEINDEXEXPRS     = (1 << 5),     /* use index exprs during linear */
  RPF_SAVEBYTEHITS      = (1 << 6),     /* save byte offsets during linear */
  RPF_SAVEBESTHITS      = (1 << 7)      /* rppm_rankbest(): save best hits */
}
RPF;

struct RPPM_tag
{
  RPPM_SET      *sets;                  /*query sets (w/o PMISNOP/LOGIPROPs)*/
  MMAPI         *mm;                    /* MMAPI passed to openrppm() */
  FDBIHI        *hits, **hitsp;         /* scratch for rppm_rankbuf() */
  RPF           flags;                  /* RPF flags */
  long          totrecs;                /* >0: total # of records in index */
  int           fop;                    /* FOP_... operator */
  int           numsets;                /* # of sets (w/o PMISNOP/LOGIPROPs)*/
  int           numidx;                 /* number of index-searchable sets */
  int           numnnot;                /* number of non-LOGINOT sets */
  int           nlogiand, nlogiset, nloginot;
  int           intersects, minsets;    /* intersects/minimum # of sets */
  int           qintersects;            /* query's `@' value */
  int           allpos, nreqlogiset;
  int           sumknobgain, sumpossetgain;     /* for non-LOGINOT sets */
  int           likerthresh;            /* LIKER threshold */
  RVAL          vals[RVAR_NUM];
  FHEAP         *fh;                    /* heap for merges */
  int           numInitSkip;            /* # `hits' skipped at rppm_rankbuf */
  int           *orpos2idx;             /* orpos2idx[SEL.orpos] = RPPM.sets */
                                        /*   index; -1 if not present */
  int           orpos2idxLen;

  /* Indexer fields (see similar code in fdbim.c): */
  FDBI_GETHITFUNC       *gethit;
  FDBI_GETLENFUNC       *getlen;
  void                  *rexobj;

  RECID         curRecid;                       /* (optional) for tracing */
};

/* Returns string reason if row-match post-proc (getmmapi()) is needed
 * as far as within-processing is concerned.  Or NULL if no post-proc:
 * no delimiters, or (one set and (olddelim or delimiters equal)).
 * Note additional conditions this may be NULL for index searches, in
 * fdbi.c:
 */
#define RPPM_WITHIN_POSTMM_REASON(rp, mm3s)                                  \
  ((*(mm3s)->sdexp || *(mm3s)->edexp) ?                                      \
   ((rp)->numsets>1 ? "`w/' delimiter expressions used with multiple terms" :\
    ((!(mm3s)->olddelim && !(mm3s)->delimseq) ?                              \
   "Dissimilar `w/' delimiter expressions used (and olddelim is off)":NULL)):\
   NULL)

RPPM   *openrppm ARGS((MMAPI *mm, MMQL *mq, int fop, FDBI *fi, int flags));
RPPM   *closerppm ARGS((RPPM *rp));
void    rppm_unindex ARGS((RPPM *rp));
void    rppm_setflags ARGS((RPPM *rp, int flags, int on));
RPF     TXrppmGetFlags ARGS((RPPM *rp));
int     rppm_setgain ARGS((CONST char *var, int val));
int     rppm_setgainlocal ARGS((RPPM *rp, RVAR var, int val));
void    rppm_resetvals ARGS((void));
void    rppm_setwts ARGS((RPPM *rp, int *gains, long *tblfreqs));
int     TXrppmSetIndexExprs ARGS((RPPM *rp, char **exprs, char *locale));
int     rppm_rankbest ARGS((RPPM *rp, FDBIHI **hits, int num,
                            size_t *byteMedian));
int     rppm_rankbest_trace ARGS((RPPM *rp, FDBIHI **hits, int num,
                                  size_t *byteMedian));
int     rppm_rankbest_one ARGS((RPPM *rp, FDBIHI *hit,
                                size_t *byteMedian));
int     rppm_rankbest_one_trace ARGS((RPPM *rp, FDBIHI *hit,
                                      size_t *byteMedian));
int     rppm_rankbuf ARGS((RPPM *rp, MMAPI *mm, byte *buf, byte *end,
                           size_t *byteMedian));
int     TXrppmGetBestHitInfo(RPPM *rp, FDBIHI ***hits);
int     rppm_searchbuf ARGS((RPPM *rp, FDBIHI **hits, byte *buf, byte *end));
int     rppm_mminfo2hits ARGS((RPPM *rp, FDBIHI **hits, MMAPI *mm));
int     rppm_rankcur ARGS((RPPM *rp, FDBIHI **hits, int num,
                           size_t *byteMedian));
int     rppm_rankcur_trace ARGS((RPPM *rp, FDBIHI **hits, int num,
                                 size_t *byteMedian));

extern RVAL     RppmValsCur[];          /* current global settings */
extern CONST char * CONST       TXrppmValsName[];
extern int      TXtraceRppm;

#endif  /* !RPPM_H */
