/* stuff for dbtable.h */


#ifndef FLDMATH_H
#define FLDMATH_H

extern FLD *dupfld ARGS((FLD *f));

/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/**********************************************************************/
typedef struct FLDSTK_tag FLDSTK;
#define FLDSTKPN (FLDSTK *)NULL
struct FLDSTK_tag {
   FLD  *f;                                        /* array of fields */
   int   numAlloced;                            /* size of f array */
   int   numUsed;          /* number pushed onto `f' (index of next avail) */
   byte *flg;         /* MAW 11-10-93 - parallel array of flags for f */
   byte  lastflg;     /* MAW 11-10-93 - value of flg[i] from last pop */
#ifndef OLD_FLDMATH
   byte *mine; /* JMT 96-05-23 - parallel array saying to free or not */
#endif
};
#define FLDSTKINC 128

#define MAXFLDARGS 5                  /* max # of args for a function */
typedef struct FLDFUNC_tag FLDFUNC;
#define FLDFUNCPN (FLDFUNC *)NULL
struct FLDFUNC_tag {
   char *name;                                    /* name of function */
   int (*func)ARGS((void));                                /* handler */
   int   minargs;                   /* minimum # of arguments allowed */
   int   maxargs;                   /* maximum # of arguments allowed */
   int   rettype;                                      /* return type */
   int   types[MAXFLDARGS];     /* argument types, 0 means don't care */
};

#define FO_NTTBL 1 /* 4 */
#ifndef OLD_FLDMATH
typedef int (*fop_type)ARGS((FLD *f1,FLD *f2,FLD *fr, int op));
#else
typedef int (*fop_type)ARGS((FLD *f1,FLD *f2,int op));
#endif
typedef struct FLDOP_tag FLDOP;
#define FLDOPPN (FLDOP *)NULL
struct FLDOP_tag {
   FLDSTK *fs;
   fop_type *ops;         /* sq tbl of func ptrs for every type combo */
   int *row;                                   /* indexes to ops rows */
   int ntypes;                                     /* number of types */
#if FO_NTTBL!=1
   int tblsz;                              /* ntypes*ntypes for speed */
#endif
   FLDFUNC *fldfuncs;                           /* list of functions */
   int  nfldfuncs;                             /* number of functions */
   FLD *tf1, *tf2;
   int	owntf1, owntf2;
   int	hadtf1, hadtf2;
};
                                                          /* op types */
                       /* compare ops, only, always have 0x80 bit set */
                     /* op value is unique even when its stripped off */
            /* so you can check it to see if comparision vs operation */
typedef enum FOP_tag
  {
    FOP_CMP = 0x80,                             /* compare bit */
/* NOTE: some FOP_LAST-sized arrays assume FOP_... values are consecutive: */
    FOP_ADD = 0x01,                             /* f1+f2           */
    FOP_SUB = 0x02,                             /* f1-f2           */
    FOP_MUL = 0x03,                             /* f2*f2           */
    FOP_DIV = 0x04,                             /* f1/f2           */
    FOP_MOD = 0x05,                             /* f1%f2           */
    FOP_CNV = 0x06,                             /* f1=(f2 type)f1  */
    FOP_ASN = 0x07,                             /* f1=(f1 type)f2  */
    FOP_EQ  = (FOP_CMP | 0x08),                 /* compare: f1==f2 */
    FOP_LT  = (FOP_CMP | 0x09),                 /* compare: f1<f2  */
    FOP_LTE = (FOP_CMP | 0x0a),                 /* compare: f1<=f2 */
    FOP_GT  = (FOP_CMP | 0x0b),                 /* compare: f1>f2  */
    FOP_GTE = (FOP_CMP | 0x0c),                 /* compare: f1>=f2 */

    FOP_AND = 0x0d,                             /* f1 && f2        */
    FOP_OR  = 0x0e,                             /* f1 || f2        */
    FOP_NEQ = (FOP_CMP | 0x0f),                 /* compare: f1==f2 */

    FOP_MM  = 0x10,                             /* metamorph LIKE    */
    FOP_NMM = 0x11,                             /* metamorph no post LIKE3 */
    FOP_MAT = 0x12,                             /* filespec MATCHES */
    FOP_RELEV = 0x13,                           /* Relevancy Ranking LIKER */
    FOP_PROXIM = 0x14,                          /* Relevancy Ranking LIKEP */
    FOP_IN    = (FOP_CMP | 0x15),               /* is member of set */
    FOP_COM   = (FOP_CMP | 0x16),               /* Compare (<0 0 >0 return)*/
    FOP_MMIN  = (FOP_CMP | 0x17),		/* Metamorph LIKEIN */
    FOP_TWIXT = (FOP_CMP | 0x18),               /* BETWEEN */
    FOP_IS_SUBSET = (FOP_CMP | 0x19),           /* set A is-subset-of set B */
    FOP_INTERSECT = 0x1a,                       /* intersection of 2 sets */
/* these FOP_...EMPTY ops are needed because the index optimizer can only
 * look at a single `x FOP_... y' predicate, not `x FOP... y IS NOT EMPTY'.
 * In addition, a NULL strlst (unknown/not present?) should probably behave
 * differently than an empty strlst (present with no elements); thus
 * we use `IS [NOT] EMPTY' instead of `IS [NOT] NULL':
 */
    FOP_INTERSECT_IS_EMPTY  =  (FOP_CMP | 0x1b), /* set-intersect is empty*/
    FOP_INTERSECT_IS_NOT_EMPTY = (FOP_CMP | 0x1c), /* "" is not empty */
/* NOTE: see FOP_LAST-sized arrays if FOP_... values change or are added to */
/* NOTE: also update QNODE_OP enum if changing/adding FOP_... values */
    FOP_LAST = 0x1c,                            /* highest op - 0x80 bit */

#define FOP_NTYPES      FTN_LAST    /* # built in types - dep on dbtable.h */

    /* errors: */
    FOP_EOK      = 0,                           /* no error: ok */
    FOP_EINVAL   = (-1),                        /* invalid operation */
    FOP_ENOMEM   = (-2),                        /* no memory */
    FOP_ESTACK   = (-3),                        /* math stack underflow */
    FOP_EDOMAIN  = (-4),                        /* bad domain (input) */
    FOP_ERANGE   = (-5),                        /* bad range (result) */
    FOP_EUNKNOWN = (-6),                        /* unknown/unknowable value */
    FOP_EILLEGAL = (-7)                 /* illegal, do not attempt promop() */
  }
FOP;

/**********************************************************************/
#ifndef OLD_FLDMATH
extern int     fld2byte     ARGS((FLD *f, FLD *f2));
extern int     fld2char     ARGS((FLD *f, FLD *f2));
extern int     fld2double   ARGS((FLD *f, FLD *f2));
extern int     fld2float    ARGS((FLD *f, FLD *f2));
extern int     fld2int      ARGS((FLD *f, FLD *f2));
extern int     fld2integer  ARGS((FLD *f, FLD *f2));
extern int     fld2long     ARGS((FLD *f, FLD *f2));
extern int     fld2short    ARGS((FLD *f, FLD *f2));
extern int     fld2smallint ARGS((FLD *f, FLD *f2));
extern int     fld2word     ARGS((FLD *f, FLD *f2));
extern int     fld2dword    ARGS((FLD *f, FLD *f2));
extern int     fld2int64    ARGS((FLD *f, FLD *f2));
extern int     fld2uint64   ARGS((FLD *f, FLD *f2));
extern int     fld2handle   ARGS((FLD *f, FLD *f2));
#else
extern int     fld2byte     ARGS((FLD *f));
extern int     fld2char     ARGS((FLD *f));
extern int     fld2double   ARGS((FLD *f));
extern int     fld2float    ARGS((FLD *f));
extern int     fld2int      ARGS((FLD *f));
extern int     fld2integer  ARGS((FLD *f));
extern int     fld2long     ARGS((FLD *f));
extern int     fld2short    ARGS((FLD *f));
extern int     fld2smallint ARGS((FLD *f));
extern int     fld2word     ARGS((FLD *f));
extern int     fld2dword    ARGS((FLD *f));
#endif

extern FLDSTK *TXfsopen       ARGS((void));
extern FLDSTK *fsclose      ARGS((FLDSTK *));
extern CONST char *TXfldopname ARGS((int op));
extern CONST char *TXfldFuncName ARGS((fop_type func));
extern int     fspush       ARGS((FLDSTK *fs,FLD *f));
extern int     fspush2      ARGS((FLDSTK *fs,FLD *f, int mine));
extern int     fsmark       ARGS((FLDSTK *fs));
extern int     fsnmark      ARGS((FLDSTK *fs));
extern FLD    *fspop        ARGS((FLDSTK *fs));
extern int     fsdisc       ARGS((FLDSTK *fs));
/* fspeekn(): returns (without popping) the Nth item from the top of
 * the stack (N == 1 is top item); NULL if not present:
 */
#define fspeekn(a, n)   ((a)->numUsed < (n) ||                               \
                         (a)->numUsed - ((n) - 1) > (a)->numAlloced ? FLDPN :\
                         &(a)->f[(a)->numUsed - (n)])
#define fspeek(a)       fspeekn(a, 1)
#define fspeek2(a)      fspeekn(a, 2)
#define fsismark(a) ((a)->lastflg)

/* Like fspeekn(), but for `mine': */
#define TXfsIsMineN(fs, n)                              \
  ((fs)->numUsed < (n) ||                               \
   (fs)->numUsed - ((n) - 1) > (fs)->numAlloced ? 0 :   \
   (fs)->mine[(fs)->numUsed - (n)])
#define TXfsIsMineTop(fs)       TXfsIsMineN(fs, 1)
#define TXfsSetMineN(fs, n, val)                        \
  ((fs)->numUsed < (n) ||                               \
   (fs)->numUsed - ((n) - 1) > (fs)->numAlloced ? 0 :   \
   ((fs)->mine[(fs)->numUsed - (n)] = (val), 1))
#define TXfsSetMineTop(fs, val) TXfsSetMineN(fs, 1, val)

extern int      TXfldmathverb;
extern int      TXfldmathVerboseMaxValueSize;
extern TXbool   TXfldmathVerboseHexInts;

extern FLDOP   *foopen      ARGS((void));
extern FLDOP   *foclose     ARGS((FLDOP *fo));
extern int      fosetop     ARGS((FLDOP *fo,int type1,int type2,fop_type newfunc,fop_type *oldfunc));
extern int      foaddfuncs  ARGS((FLDOP *fo,FLDFUNC *functbl,int ntbl));
extern FLDFUNC *fofunc      ARGS((FLDOP *fo,char *fname));
#define TXfldopFuncNameCompare  strcmpi
extern int      fofuncret   ARGS((FLDOP *fo,char *name));
extern int      foop        ARGS((FLDOP *fo,int op));
extern int      TXfldmathopmsg ARGS((FLD *f1, FLD *f2, int op, CONST char *opName));
extern int      foop2       ARGS((FLDOP *fo,int op, FLD *f3, fop_type *infunc));
extern int      focall      ARGS((FLDOP *fo, char *funcname, TXPMBUF *pmbuf));
extern int	fogetop	    ARGS((FLDOP *fo, int type1, int type2, fop_type *pfunc));
#define fopush(a,b)  fspush2((a)->fs,(b),0)
#define fopush2(a,b,c)  fspush2((a)->fs,(b),(c))
#define fomark(a)    fsmark((a)->fs)
#define fonmark(a)   fsnmark((a)->fs)
#define fopop(a)     fspop((a)->fs)
#define fopeek(a)    fspeek((a)->fs)
#define fopeek2(a)   fspeek2((a)->fs)
#define fopeekn(a, n) fspeekn((a)->fs, (n))   /* `n'th from top (1 == top) */
#define fodisc(a)    fsdisc((a)->fs)
#define foismark(a)  fsismark((a)->fs)

extern CONST FLDFUNC    TXdbfldfuncs[];

int     TXfldmathReturnNull(FLD *f1, FLD *f3);

/**********************************************************************/
#endif                                                   /* FLDMATH_H */
