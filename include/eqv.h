#ifndef EQV_H
#define EQV_H
/**********************************************************************/
#include "salloc.h"
#include "otree.h"
#ifndef APICP
#define APICP   struct mm_api_cp
#endif
extern void FAR eqprepstr ARGS((char FAR *s));/* from mmprep.h for the api */
/**********************************************************************/
#define EQVHDR struct eqvhdr_struct
#define EQVHDRPN (EQVHDR *)NULL
EQVHDR {                              /* header on tail of index file */
   dword magic;                             /* magic value to id file */
   word  maxwrdlen;         /* length of longest root word in eq file */
   word  maxreclen;            /* length of longest record in eq file */
   word  maxwords;   /* largest # of equivs for a word including word */
   dword nrecs;                        /* number of root word entries */
   dword dataoff;              /* offset of beginning of data records */
   dword fixcacheoff;                /* offset of fixed cache records */
   byte  nfixcache;                       /* # of fixed cache records */
   dword chainoff;                   /* offset of chain filename or 0 */
   byte  chainlen;                   /* length of chain filename or 0 */
   byte  version;                               /* equiv file version */
};

#define EQVLSTPN  ((EQVLST *)NULL)
#define EQVLSTPPN ((EQVLST **)NULL)
typedef struct EQVLST_tag
{
   char **words;
   char **clas ;         /* MAW 09-02-92 - "class" conflicts with c++ */
   char  *op;
   char   logic;
   int    sz;
   int    used;
   int    qoff;                                 /* offset into query */
   int    qlen;                                 /* length in query */
   /* Original set logic, tilde, open-paren, and/or pattern-matcher-char: */
   char   *originalPrefix;
   /* Source expressions for `words', i.e. the original REX/NPM/XPM
    * expression, phrase, or paren list of phrases from user query
    * (without quotes):
    */
   char   **sourceExprs;                        /* NULL-terminated */
}
EQVLST;

#define EQVREC struct eqvrec_struct
#define EQVRECPN (EQVREC *)NULL
EQVREC {
   long    n;                         /* record number of this record */
   long    off;                              /* file offset of record */
   int     lenw;                               /* length of root word */
   int     lena;                    /* length of entire record - calc */
   char   *buf;        /* alloced to hdr.maxreclen+1, '\0' terminated */
   EQVLST *eql;                          /* alloced to hdr.maxwords+1 */
};

#define EQVCACHE struct eqvcache_struct
#define EQVCACHEPN (EQVCACHE *)NULL
EQVCACHE {
   long    n;                         /* record number of this record */
   long    off;                              /* file offset of record */
   int     lenw;                               /* length of root word */
   char   *buf;        /* alloced to hdr.maxwrdlen+1, '\0' terminated */
};
#define NEQVFIXCALEV  6
#define NEQVFIXCACHE 63
#define NEQVCACHE    (NEQVFIXCACHE+16)

#define EQV struct eqv_struct
#define EQVPN (EQV *)NULL
EQV {
   EQV       *chain;                      /* lookup in this one first */
   FILE      *fp;                                   /* opened eq file */
   EQVHDR     hdr;                        /* info from eq file header */
   EQVREC     rec;                      /* place to read records into */
   EQVCACHE   cache[NEQVCACHE];                  /* cache for bsearch */
   int        nextc;                             /* next cache to use */
   int        see;                                 /* lookup see refs */
   int        sufproc;                   /* perform suffix processing */
   char     **suflst;                                  /* suffix list */
   int        nsuf;                   /* number of suffixes in suflst */
   int        minwl;              /* min word length for suffix strip */
   int        kpsee;                     /* keep see refs w/leading @ */
   int        kpeqvs;          /* keep equivs for words/phrases found */
   int        kpnoise;                            /* keep noise words */
   char     **noise;                                    /* noise list */
   int      (*isnoise)ARGS((char **lst,char *wrd,void *isnarg));
   void      *isnarg;                            /* arg for isnoise() */
   EQVLST  *(*ueq)ARGS((EQVLST *eqp,void *ueqarg));/* user equiv func */
   void      *ueqarg;                                /* arg for above */
   char    *(*uparse)ARGS((char *wrd,void *uparsearg));/* user parse func */
   void      *uparsearg;                             /* arg for above */
   int        kppunct;/* MAW 08-11-93 - keep punctuation in/around words */
   APICP     *acp;     /* MAW 06-01-98 - apicp with controlling flags */
   int        myacp;                /* MAW 06-01-98 - did i alloc acp */
   CONST byte *ram;           /* MAW 02-02-99 - use this equiv in ram */
   int        ramsz;                                  /* MAW 02-02-99 */
   CONST byte *ramptr;                                /* MAW 02-02-99 */
   int        rmdef;                                /* JMT 2001-11-27 */
};
#define EQV_ISNOISEPN ((int (*)ARGS((char **,char *,void *)))NULL)
#define EQV_UEQPN     ((EQVLST *(*)ARGS((EQVLST *,void *)))NULL)
#define EQV_UPARSEPN  ((char *(*)ARGS((char *,void *)))NULL)

#define eqvsee(eq,flag)      ((eq)->see=flag)
#define eqvsufproc(eq,flag)  ((eq)->sufproc=flag)
#define eqvsuflst(eq,lst)    ((eq)->suflst=lst)
#define eqvminwl(eq,num)     ((eq)->minwl=num)
#define eqvkpsee(eq,flag)    ((eq)->kpsee=flag)
#define eqvkpeqvs(eq,flag)   ((eq)->kpeqvs=flag)
#define eqvkpnoise(eq,flag)  ((eq)->kpnoise=flag)
#define eqvrmdef(eq,flag)    ((eq)->rmdef=flag)
#define eqvkppunct(eq,flag)  ((eq)->kppunct=flag)
#define eqvnoise(eq,lst)     ((eq)->noise=lst)
#define eqvisnoise(eq,func)  ((eq)->isnoise=func)
#define eqvisnarg(eq,ptr)    ((eq)->isnarg=ptr)
#define eqvueq(eq,func)      ((eq)->ueq=func)
#define eqvueqarg(eq,ptr)    ((eq)->ueqarg=ptr)
#define eqvuparse(eq,func)   ((eq)->uparse=func)
#define eqvuparsearg(eq,ptr) ((eq)->uparsearg=ptr)
                                 /* default values for settable parms */
#define DEF_EQV_SEE        0
#define DEF_EQV_SUFPROC    1
#define DEF_EQV_SUFLST     CHARPPN
#define DEF_EQV_MINWL      5
#define DEF_EQV_KPSEE      0
#define DEF_EQV_KPEQVS     1
#define DEF_EQV_KPNOISE    1
#define DEF_EQV_KPPUNCT    1
#define DEF_EQV_NOISE      CHARPPN
#define DEF_EQV_ISNOISE    EQV_ISNOISEPN
#define DEF_EQV_ISNARG     VOIDPN
#define DEF_EQV_UEQ        EQV_UEQPN
#define DEF_EQV_UEQARG     VOIDPN
#define DEF_EQV_UPARSE     EQV_UPARSEPN
#define DEF_EQV_UPARSEARG  VOIDPN
                                /* macro to set all values to default */
#define eqvdefaults(eq) \
   eqvsee((eq),DEF_EQV_SEE), \
   eqvsufproc((eq),DEF_EQV_SUFPROC), \
   eqvsuflst((eq),DEF_EQV_SUFLST), \
   eqvminwl((eq),DEF_EQV_MINWL), \
   eqvkpsee((eq),DEF_EQV_KPSEE), \
   eqvkpeqvs((eq),DEF_EQV_KPEQVS), \
   eqvkpnoise((eq),DEF_EQV_KPNOISE), \
   eqvkppunct((eq),DEF_EQV_KPPUNCT), \
   eqvnoise((eq),DEF_EQV_NOISE), \
   eqvisnoise((eq),DEF_EQV_ISNOISE), \
   eqvisnarg((eq),DEF_EQV_ISNARG), \
   eqvueq((eq),DEF_EQV_UEQ), \
   eqvueqarg((eq),DEF_EQV_UEQARG), \
   eqvuparse((eq),DEF_EQV_UPARSE), \
   eqvuparsearg((eq),DEF_EQV_UPARSEARG)

#define eqvcpy(eqdest,eqsrc) \
   eqvsee((eqdest),(eqsrc)->see), \
   eqvsufproc((eqdest),(eqsrc)->sufproc), \
   eqvsuflst((eqdest),(eqsrc)->suflst), \
   eqvminwl((eqdest),(eqsrc)->minwl), \
   eqvkpsee((eqdest),(eqsrc)->kpsee), \
   eqvkpeqvs((eqdest),(eqsrc)->kpeqvs), \
   eqvkpnoise((eqdest),(eqsrc)->kpnoise), \
   eqvkppunct((eqdest),(eqsrc)->kppunct), \
   eqvnoise((eqdest),(eqsrc)->noise), \
   eqvisnoise((eqdest),(eqsrc)->isnoise), \
   eqvisnarg((eqdest),(eqsrc)->isnarg), \
   eqvueq((eqdest),(eqsrc)->ueq), \
   eqvueqarg((eqdest),(eqsrc)->ueqarg), \
   eqvuparse((eqdest),(eqsrc)->uparse), \
   eqvuparsearg((eqdest),(eqsrc)->uparsearg)

#define EQVX struct eqvx_struct
#define EQVXPN (EQVX *)NULL
EQVX {                                           /* info for indexing */
   FILE    *ofp;             /* backref'd, indexed, binary equiv file */
   FILE    *ndxfp;                 /* tmp work files while making ofp */
   FILE    *datafp;
   char    *ofn;
   char    *ndxfn;
   char    *datafn;
   char    *chainfn;       /* MAW 05-28-92 - chainto filename or NULL */
   long     totwrds;
   int      rmofn;
   char    *buf;
   int      bufsz;
   EQVHDR   hdr;
};

#define EQV_CISECT    '@'
#define EQV_CREX      '/'
#define EQV_CXPM      '%'
#define EQV_XPMDEFPCT 80
#define EQV_CNPM      '#'
#define EQV_CPPM      '('
#define EQV_CEPPM     ')'
#define EQV_CINVERT   '~'

#define EQV_CSET      '='
#define EQV_CAND      '+'
#define EQV_CNOT      '-'
#define EQV_CINVAL    ' '

#define EQV_CSEE      '@'                 /* internal see ref trigger */
#define EQV_CREPL     '='
#define EQV_CADD      ','
#define EQV_CDEL      '~'
#define EQV_CCLAS     ';'
#define EQV_CESC      '\\'

#define eqcmp(s1,s2) strcmpi((s1),(s2))

                                                /* backref'ing levels */
#define BREF_LSYNTAX   0              /* syntax check only, no output */
#define BREF_LINDEX    1                               /* index as is */
#define BREF_LBACKREF  2                    /* backref once and index */
#define BREF_LFBACKREF 3                   /* fully backref and index */
#define BREF_LDEFAULT  BREF_LBACKREF
/**********************************************************************/
/* read interface */
extern EQV      *closeeqv     ARGS((EQV *eq));
extern EQV      *openeqv      ARGS((char *filename,APICP *cp));
extern EQVLST  **geteqvs      ARGS((EQV *eq,char *query,int *intersects));
extern EQVLST   *geteqv       ARGS((EQV *eq,char *wrd));
extern EQVLST   *freeeqp      ARGS((EQVLST *eqp));
extern EQVLST  **freeeqplst   ARGS((EQVLST **eqplst));
extern void      rmdupeqp     ARGS((EQVLST *eqp));
extern void      rmdupeqps    ARGS((EQVLST **eqplst));
extern void      closeueqv    ARGS((EQV *eq));
extern EQV      *openueqv     ARGS((EQV *eq,char *filename));
extern EQVLST   *getueqv      ARGS((EQVLST *eqp,void *arg));
extern void      cpyeq2ueq    ARGS((EQV *eq));
#ifdef WORDSONLY
extern int       rdeqvrec     ARGS((EQV *eq,EQVREC *rec,long n,int full));
#endif

int epi_findrec ARGS((EQV *eq,char *s,int isutf8));     /* KNG 20060314 */

/* list handling functions */
extern EQVLST   *closeeqvlst  ARGS((EQVLST *eql));
extern EQVLST   *closeeqvlst2 ARGS((EQVLST *eql));
extern EQVLST  **closeeqvlst2lst ARGS((EQVLST **eql));
extern EQVLST   *openeqvlst   ARGS((int n));
extern int       addeqvlst    ARGS((EQVLST *eql,char *w,char *c,int o));
extern int       rmeqvlst2    ARGS((EQVLST *eql,char *w,char *c));
extern int       rmeqvlst     ARGS((EQVLST *eql,char *w,char *c));
extern void      clreqvlst2   ARGS((EQVLST *eql));
extern void      clreqvlst    ARGS((EQVLST *eql));
extern void      rmdupeql     ARGS((EQVLST *eql));
extern void      rmdupeqls    ARGS((EQVLST **eql));
extern EQVLST   *dupeqvlst    ARGS((EQVLST *eql));
extern EQVLST   *dupeqvstru   ARGS((EQVLST *eql));

extern char     *eqvfmti      ARGS((EQVLST *eql,int *a_lenw,int *a_lena,int *a_nwrds, int forceLit));
#define eqvfmt(e) eqvfmti((e),(int *)NULL,(int *)NULL,(int *)NULL, 0)
extern int       eqvsfmt      ARGS((EQVLST *eql,FILE *src));
extern EQVLST   *eqvparse     ARGS((char *buf, int forceLit));

/* write interface */
extern EQVX     *openeqvx     ARGS((char *ofn));
extern int       writeeqvx    ARGS((EQVX *eqx,EQVLST *eql));
extern int       finisheqvx   ARGS((EQVX *eqx));
extern EQVX     *closeeqvx    ARGS((EQVX *eqx));
extern int       eqvmkndx     ARGS((char *src,char *bin,int level,int bufsz,int dump));

/* interactive editor */
extern int       editeq       ARGS((char *main,char *usrc,char *ubin));

#include "api3.h"
/**********************************************************************/
#endif                                                       /* EQV_H */
