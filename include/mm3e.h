#ifndef MM3E_H
#define MM3E_H


#include "txtypes.h"

/**********************************************************************/
/*
 pbr 9/27/90 : added the "cmd" var to the MM3S struct
*/

	 /* MM3E.H  COPYRIGHT (C) 1988 , P. BARTON RICHARDS */
/************************************************************************/
			      /* DEFINES */
/************************************************************************/

			      /* LIMITS */

#ifndef MSDOS
#   define MMRDBUFSZ  60000		     /* size of the read buffer */
#else
#   define MMRDBUFSZ   30000		      /* to keep it small model */
#endif
#define MMRDBUFMINSZ 10000
#define MMRDBUFDECSZ 2000
#define MAXALLOCS   500    /* maximum number of memory blocks allocated */
#define MAXSELS     100 			 /* max search elements */
#define MAXEQS	    200 		    /* max n of eqs per element */
#define MAXPREFIXES 100 		  /* maximum number of prefixes */
#define MAXSUFFIXES 100 		  /* maximum number of suffixes */
#define MAXFILES    100 		  /* max number of files opened */
#define MAXHITLNSZ  256 		    /* max length of a hit line */
#define FSEEKCHAR   '@'  /* char that designates seek offset in a fname */
#define HUGEFILESZ  0x7FFFFFFF	   /* the biggest file that can be read */

#define REMORPHSZ 10 /* this constant defines how big a located morpheme
must be before it is considered to be accurate enough by itself to not
pass it through a morpheme check . Another way of saying this is:  if a
located string is smaller than this, it will be morpheme checked. */

#define REMORPHBUFSZ 128 /* size of the remorph buffer that holds a
located morpheme for processing */


/************************************************************************/

			/* SPECIAL CHARACTERS */

#define ANDSYM	 '+'       /* these are the types of logic in SEL.logic */
#define NOTSYM	 '-'
#define SETSYM	 '='
#define REXSYM	 '/'           /* symbols that tell what type of search */
#define XPMSYM	 '%'
#define NPMSYM	 '#'
#define EQDELIM  ','                /* delimiters within the equiv line */
#define POSDELIM ';'
#define EQESC    '\\'

/************************************************************************/

			     /* CONSTANTS */

/* the following are used in the SEL struct as itentifiers and they also
determine list sort order, so be careful */

typedef enum TXPMTYPE_tag                       /* pattern matcher types */
{
  PMISREX = 1,
  PMISPPM,
  PMISXPM,
  PMISSPM,
  PMISNPM,
  PMISNOP                      /* A no-op pattern matcher, e.g. @prop=value */
}
TXPMTYPE;
#define TXPMTYPEPN      ((TXPMTYPE *)NULL)

typedef enum TXLOGITYPE_tag                     /* logic types */
{
  LOGIAND = 1,
  LOGISET,
  LOGINOT,
  LOGIPROP
}
TXLOGITYPE;
#define TXLOGITYPEPN    ((TXLOGITYPE *)NULL)

/************************************************************************/
			      /* MACROS */

#ifndef ctoi
#   define ctoi(c)  ((c)-'0')      /* works for ascii ebcdic and primos */
#endif

/************************************************************************/
		       /* STRUCTURES and UNIONS */
/************************************************************************/

/* may also be defined in pm.h: */
#ifndef MM3SPN
typedef struct MM3S_tag MM3S;
#  define MM3SPN        ((MM3S *)NULL)
#endif

typedef struct SEL_tag
{
 FFS  *ex;					  /* regular expression */
 PPMS *ps;				      /* parallel pattern match */
 XPMS *xs;				   /* approximate pattern match */
 SPMS *ss;					/* single pattern match */
 NPMS *np;				       /* NUMERIC pattern match */
 TXPMTYPE       pmtype;                         /* pm type to use */
 TXLOGITYPE     logic;                          /* logic for this element */
 byte  lang;				   /* process this as language	*/
  /* `lst' elements are alloced; list is usually but not always
   * empty-string terminated (which is not alloced):
   */
 char *lst[MAXEQS];		   /* array of pointers to list members */
 int   lstsz;				   /* number of elements in lst */
 byte *srchs;		    /* the string searched for that was located */
 byte *hit;					     /* location of hit */
 int   hitsz;						 /* size of hit */
 byte  member;		     /* is it a member of the logic im matching */
 byte  nib;       /* 0x1: not in the buffer  0x2: not in current delims */
 byte  orpos;             /* JMT 6/10/94 original position in the query */
#if PM_FLEXPHRASE                                     /* MAW 01-20-97 */
 PMPHR *srchp;
#endif
 MM3S  *mm3s;                   /* Settings for this SEL JMT 2009-06-25 */
 size_t numHitsSameLoc;                         /* infinite-loop detection */
}
SEL;
#define SELPN   ((SEL *)NULL)

/* note:  I know I could have used a union instead of individual struct
pointers for all the pm types in a SEL, but it would have made already
difficult to read code even more so.  */

struct MM3S_tag
{
 TXbool	 suffixproc  ;				/* do prefix processing */
 TXbool	 prefixproc  ;					/* "" suffix "" */
 TXbool	 rebuild;		       /* perform morph rebuild process */
 TXbool	 filter_mode ;			/* echo the input to the output */
 TXbool	 incsd,inced ;	     /* include start and end delimiters in hit */
 int	 minwordlen  ;		      /* how much do I strip off a word */
 int	 intersects  ;		 /* how many set intersections are reqd */
 byte *  sdexp , *edexp  ;		     /* start and end delim exp */
 byte *  cmd;	    /* pbr 11/27/90 : a command to execute on every hit */
 byte ** searchfiles ;			   /* what files do I search in */
 /* `set' is not owned by MM3S; owned by APICP: */
 byte ** set	     ;				     /* set  list lines */
 byte ** suffix, **prefix;		     /*  suffix and prefix list */
 int npre,nsuf; 		 /* the number of prefixes and suffixes */
 CP   *  cps	     ;			    /* pointer to the cp struct */
 SEL  *  el[MAXSELS] ;            /* element list, sorted by first term */
 int	 nels	     ;				  /* number of elements */
 int	 nands,nsets,nnots;	    /* subtotal of each type of element */
 FFS  *  sdx, *edx    ; 			   /* sd and ed structs */
 byte *  start , *end ; 		     /* start and end of buffer */
 byte *  hit	     ;					  /* hit if any */
 int	 hitsz	     ;				  /* how big is the hit */
 FILE *fh;					/* current file pointer */
 MDXRS *mindex; 				   /* mindex read struct */
 char *query;				/* the original query performed */
 unsigned int mmrdbufsz;
 int     fhispipe;        /* MAW - 02-26-93 - for reading via popen() */
 TXbool  defsuffrm;        /* JMT 1999-10-01 - strip default suffixes */
 TXbool  reqsdelim;           /* JMT 2000-02-11 - require start delim */
 TXbool  reqedelim;             /* JMT 2000-02-11 - require end delim */
 TXbool  olddelim;            /* JMT 2000-02-11 - old delim behaviour */
 TXbool  delimseq;           /* JMT 2000-02-11 - are the delims equal */
 int     withinmode;                   /* JMT 2004-02-27 - withinmode */
 int     withincount;                    /* JMT 2004-02-27 N from w/N */
 int     phrasewordproc;                            /* KNG 2004-04-14 */
 int     textsearchmode;                        /* TXCFF mode for text srch */
 int     stringcomparemode;                     /* TXCFF mode for str comp */
 int     refcount; /* For copy-on-write into SELs */
 int     noNotInversion;                /* do not invert NOTs in findsel() */
 int     denymode;
 int     qmaxsetwords;
 int     qmaxwords;
 byte    exactphrase;
 byte    keepnoise;
 TXbool  isRankedQuery;                         /* LIKE{P,R} */
};

/************************************************************************/
#ifdef INTEG_ENG
	      /* This stuff is for the integrated engine */
			    /* PBR 11/6/89 */

#ifdef MVS
#  define TMT time_t
#else
#  define TMT long
#endif

typedef struct MM3IS_tag                        /* MM3 integ struct */
{
 int state;			       /* what state is this routine in */
 byte *buf;					   /* buffer for readin */
 MM3S *mms;						 /* mm3e struct */
 char *Fn;
 char *fname;			  /* MAW 04-02-90 - current file name */
 int nread;				       /*  number of bytes read */
 long foff;						 /* file offset */
 byte *hitloc,*end;			 /* hit location, end of buffer */
 TMT stime,etime;		    /* start and end time of the engine */
 int hits;
 FNS *fns;			     /* MAW 04-02-90 - filename stuff */
 long ftot;			     /* MAW 04-02-90 - filename stuff */
 long ntot;			     /* MAW 04-03-90 - filename stuff */
 long shits, sntot;		     /* MAW 04-03-90 - filename stuff */
}
MM3IS;
#undef TMT


#ifdef LINT_ARGS
MM3IS *closemmie(MM3IS *);
MM3IS *openmmie(char *);
int    getmmie(MM3IS *);
#else
MM3IS *closemmie();
MM3IS *openmmie();
int    getmmie();
#endif

#endif                                                   /* INTEG_ENG */
/************************************************************************/

#ifdef LINT_ARGS
char *mmcalloc(int n,int size);
void mmfree(char *p);
void closesels(MM3S *ms);
void rmpresuf(byte *daword,MM3S *ms);
int  xpmsetup(byte *s,SEL *ea,MM3S *ms);
int  lstsetup(byte *s,SEL *ea);
int  opensels(MM3S **ms);
MM3S *closemm(MM3S *ms);
int  CDECL selcmp(CONST void *,CONST void *);
MM3S *openmm(char *cfn);
byte *selsrch(SEL *el,byte *start,byte *end,int op);
byte *findsel(MM3S *ms,int n,byte *start,byte *end,int op);
byte *TXfindselWithSels(MM3S *ms, SEL **sels, int numSels, int selIdx,
                        byte *start, byte *end, TXPMOP op);
int  inset(MM3S *ms,int n);
int  remorph(MM3S *,int);
int  checklang(MM3S *ms);
int  findintrsect(MM3S *,int);
byte *findmm(MM3S *ms);
byte *getmm(MM3S *ms,byte *buf,byte *end,int operation);
int  domme(MM3S *ms);
void mmreport(MM3S *);
int fnameseek(char *,long *,long *);
byte *allocmmrdbuf(MM3S *);
#else
char *mmcalloc();
void mmfree();
void closesels();
void rmpresuf();
int  eq2lst();
int  xpmsetup();
int  lstsetup();
int  opensels();
MM3S *closemm();
int  CDECL selcmp();
MM3S *openmm();
byte *selsrch();
byte *findsel();
int  inset();
int  remorph();
int  checklang();
int  findintrsect();
byte *findmm();
byte *getmm();
int  domme();
void mmreport();
int fnameseek();
byte *allocmmrdbuf();
#endif							 /* LINT_ARGS */

/* internal debug/trace use: */
int TXmmShowHitContext(char *outBuf, size_t outBufSz, size_t hitOffset,
                       size_t hitLen, size_t *subHitOffsets,
                       size_t *subHitLens, size_t numSubHits,
                       const char *searchBuf, size_t searchBufSz);
int TXmmSetupHitContext(MM3S *ms, char *contextBuf, size_t contextBufSz);

/**********************************************************************/
#endif                                                      /* MM3E_H */
