/*#define PPMLONG */
#ifndef PM_H
#define PM_H

            /* PM.H Copyright (C) 1988 P. Barton Richards */

 /* this file defines all pertinent data for the REX pattern matcher */

#ifndef ARGS
#  ifdef LINT_ARGS                                    /* MAW 01-10-91 */
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif

#include "txtypes.h"

#ifdef NOMALLOCDEFS
extern char *calloc();
extern char *malloc();
extern void  free();
#endif

#define BYTESZ   8                                    /* bits in a byte */
#define DYNABYTE 256                     /* the dynamic range of a byte */
#define BPNULL (byte *)NULL

typedef enum TXPMOP_tag                         /* pattern matcher op */
{
  CONTINUESEARCH        = 0,                    /* tells getrex what to do */
  SEARCHNEWBUF          = 1,
  BCONTINUESEARCH       = 2,                    /* backwards */
  BSEARCHNEWBUF         = 3
}
TXPMOP;
#define TXPMOPPN        ((TXPMOP *)NULL)

#ifdef NEVER                                        /* MAW/PBR 01-02-97 */
#define MAXFREP 32000                      /* maximum number of repeats */
#else
#define MAXFREP (INT_MAX-32)               /* maximum number of repeats */
#endif

/* KNG 20080408 avoid dragging in unicode.h: */
#ifndef TXUPMPN
typedef struct TXUPM_tag        TXUPM;
#  define TXUPMPN       ((TXUPM *)NULL)
#endif /* !TXUPMPN */

/************************************************************************/
void  pm_hyeqsp   ARGS((int truefalse));
int   pm_getHyphenPhrase ARGS((void));
void  pm_reinitct ARGS((void));
void  pm_resetct  ARGS((void));
int  *pm_getct    ARGS((void));
int   pm_getCmpTabHas8bitCrossing ARGS((void));
/************************************************************************/

/* max pattern length now arbitrary (but see f3par()) and
 * independent of DYNABYTE: KNG 040413
 */
#define FFS_MAX_PAT_LEN         256

typedef struct FFS_tag FFS;                         /* fast find struct */
#define FFSPN   ((FFS *)NULL)

struct FFS_tag
{
 byte *exp;                         /* the expression that started this */
 byte *expEnd;                                  /* end of `exp' */
 byte **setlist;            /* the longest pattern is the range of byte */
 /* setlist[i][c] == 1 if `i'th char (set) in expr. has match for byte `c'
  * setlist[i][c] == 0 if not        range of `c' is 0-255
  */
 byte *skiptab;                             /* DYNABYTE-size jump table */
 byte *bskiptab;                  /* DYNABYTE-size backwards jump table */
 byte *start;                           /* start of area to be searched */
 byte *end;                                            /* the end of it */
 byte *hit;                       /* where last hit was (null if none ) */
 unsigned int hitsize;                            /* how big is the hit */
 int from,to,n;                                  /* from x to y repeats */
 FFS *next;                                    /* next pattern in chain */
 FFS *prev;                                    /* last pattern in chain */
 FFS *first;                                    /* first pattern in chain */
 FFS *last;                                     /* last pattern in chain */
 size_t subExprIndex;                           /* subexpression # (from 0) */
 int exclude;                      /* <0: `\P'-excluded  >0: `\F'-excluded */
  void  *re2;                                   /* (opt.) RE2 object instead*/
  int   re2NumCaptureGroups;
  const char    **re2CaptureHits;               /* alloc'd */
  size_t        *re2CaptureHitSizes;            /* alloc'd */
 byte nralloced;                  /* number in `setlist' really alloced */
 byte patsize;                               /* how many sets there are */
 byte backwards;                                   /* is this backwards */
 byte root;                                 /* is this the root pattern */
 byte is_not;                                          /* is this a not */
};
#define FFSHIT(a) ((a)->hit)
#define FFSSIZE(a) ((a)->hitsize)

/* REX `\<...\>' escape token (ala `\<re2\>') to indicate this
 * expression matches everything that doesn't match other expessions.
 * Used by RLEX since it only makes sense in a multiple-expression
 * context, but REX needs to know it to complain if it sees it:
 */
#define TX_REX_TOKEN_NO_MATCH           "nomatch"
#define TX_REX_TOKEN_NO_MATCH_LEN       (sizeof(TX_REX_TOKEN_NO_MATCH) - 1)

/************************************************************************/

         /* MAW 06-10-97 - PM_FLEXPHRASE and PM_NEWLANG on by default */
#ifndef PM_FLEXPHRASE
#  define PM_FLEXPHRASE 1                               /* MAW 01-08-97 */
#endif
#ifndef PM_NEWLANG
#  define PM_NEWLANG 1                                  /* MAW 03-04-97 */
#endif

typedef struct PMPHR_tag        PMPHR;
#define PMPHRPN ((PMPHR  *)NULL)
struct PMPHR_tag
{
   byte *buf;
   byte *term;
   int   len;
   int   nterms;
   byte *remain;
   PMPHR *prev;
   PMPHR *next;
   int  useUnicode;
   int textsearchmode;                          /* TXCFF value */
   CONST int    *cmptab;                        /* compare table to use */
};

/* avoid dragging in mm3e.h: KNG 20080416 */
#ifndef MM3SPN
typedef struct MM3S_tag MM3S;
#  define MM3SPN        ((MM3S *)NULL)
#endif

PMPHR *closepmphr   ARGS((PMPHR *ph));
PMPHR *openpmphr    ARGS((byte *s,int stoponstar, MM3S *ms, int hyphenPhrase));
byte  *verifyphrase ARGS((PMPHR *ph,byte *bufptr,byte *endptr,byte *hit,byte **hitend));
int    samephrase   ARGS((PMPHR *ph,byte *s));
EPI_SSIZE_T     TXphrasePrint(PMPHR *ph, char *buf, EPI_SSIZE_T bufSz);

/************************************************************************/

extern byte *pm_getlangc ARGS((void));
extern byte *pm_getwordc ARGS((void));
extern void  pm_initwlc  ARGS((void));
extern int   pm_setlangc ARGS((CONST char *s));
extern int   pm_setwordc ARGS((CONST char *s));

extern int TxLocaleSerial;
int     TXgetlocaleserial ARGS((void));

#define LANG_FALSE	0
#define LANG_TRUE	1
#define LANG_WILD	2

/* Trace Metamorph flags: */
typedef enum
  {
    TX_TMF_Open         = 0x0001,       /* SEL/PPM/etc. open/close */
    TX_TMF_Findsel      = 0x0002,       /* findsel() hit/miss */
    TX_TMF_Inset        = 0x0004,       /* inset() rejection */
    TX_TMF_Remorph      = 0x0008,       /* remorph() check */
    TX_TMF_Phrase       = 0x0010,       /* verifyphrase(), matchphrase() */
    TX_TMF_OverallHit   = 0x0100,       /* overall hit/miss: getmmapi() */
    TX_TMF_Getppm       = 0x1000,       /* getppm() */
    TX_TMF_PpmInternal  = 0x2000,       /* PPM internal (pre-phrase)*/
  }
TX_TMF;

extern TX_TMF   TXtraceMetamorph;

/************************************************************************/

               /* Data for the parallel pattern matcher */

#ifndef PPMLONG
typedef byte PMBITGROUP;                        /* parallel match data type */
#define PMTBITS  8                       /* number of bits in the above */
#define PMTHIBIT 0x80                           /* hi value of PM TYPE */
#else
typedef unsigned long PMBITGROUP                /* parallel match data type */
#define PMTBITS  EPI_OS_ULONG_BITS      /* number of bits in the above */
#define PMTHIBIT (1UL << (EPI_OS_ULONG_BITS - 1))  /* hi value of PM TYPE */
#endif

/* Bit mask value for array index `idx': */
#define TXPPM_INDEX_BIT_MASK(idx)       ((PMBITGROUP)1 << ((idx) % PMTBITS))

/* TX_PPM_MAX_TERM_LEN: arbitrary maximum length of a PPM search term.
 * Must be < DYNABYTE due to byte-size jumps in `jumpTable'?
 * Has historically been DYNABYTE - 1:
 */
#define TX_PPM_MAX_TERM_LEN     255

typedef struct PPMS_tag PPMS;
#define PPMSPN  ((PPMS *)NULL)
#define PPMSPNULL       PPMSPN          /* legacy use */

typedef enum TXPPM_BF_tag
  {
    TXPPM_BF_IS_DUP_OF_NEXT_TERM        = (1 << 0),
    TXPPM_BF_IS_LANGUAGE_QUERY          = (1 << 1)
  }
TXPPM_BF;

struct PPMS_tag
{
  /* `wordList`, `orgTermList', `phraseObjList', `flags' are all
   * parallel arrays.
   */

  /* `wordList' is the list of single words to actually PPM-search for.
   * `wordList[i]' points to either `orgTermList[i]' (if that is
   * single-word) or `phraseObjList[i]->term' (if `orgTermList[i]' is
   * multi-word).  `wordList' is alloced and owned by PPMS, but
   * its members are not:
   */
 byte **wordList;                               /* the string list */
  /* `orgTermList' is the original string array from the user,
   * re-sorted by PPMS alphabetically ascending (ignore-case according
   * to locale).  It is owned not by PPMS but by the user/caller, who
   * must keep it around for the duration of this PPMS.  It may contain
   * single words and/or multi-word phrases.
   */
 byte **orgTermList;                            /* the input string list */
  /* `phraseObjList[i]' is the alloc'd phrase object for
   * `orgTermList[i]'; NULL if single-word.  Each `phraseObjList[i]'
   * is the longest (?) word of its phrase; other words may be linked
   * before/after in the PMHR object:
   */
 PMPHR **phraseObjList;
 TXPPM_BF       *flags;
 byte  *hitend;                         /* end of `hit' in search buf */
  /* `setTable[j][b]' has TXPPM_INDEX_BIT_MASK(i) bit set for every
   * `wordList[i]' that would match a byte value `b' at string offset `j':
   */
  PMBITGROUP    *setTable[TX_PPM_MAX_TERM_LEN + 1];     /* +1 for stop */
  /* `lengthTable[j]' has TXPPM_INDEX_BIT_MASK(i) bit set for every
   * `wordList[i]' whose last byte is at offset `j'.  `lengthTable' is
   * used to determine if the current (matching) byte offset ends
   * search term(s) and thus a full match has (potentially) been found:
   */
  PMBITGROUP    lengthTable[TX_PPM_MAX_TERM_LEN];       /* length table */
 byte    jumpTable[DYNABYTE];                   /* jump table */
 byte    byteCompareTable[DYNABYTE]; /* ignore-case etc. byte compare table */
 int    minTermLen, maxTermLen;           /* min/max `wordList[i]' lengths */
 int    numTerms;               /* # of search strings (`wordList' length) */
 byte   *searchBuf, *searchBufEnd;              /* search buffer begin/end */
 byte   *wordHit;               /* `wordList[i]' hit in `searchBuf' */
 byte   *prevWordHit;           /* previous "" */
 byte *hit;                     /* overall hit (w/phrase) in `searchBuf' */
 byte   *prevHit;               /* previous "" (during CONTINUESEARCH) */
  size_t numHitsSameLoc;                        /* # hits at `prevHit' */
 byte mask;                                              /* stored mask */
 byte   localeChangeYapped;
 int    localeSerial;                   /* locale serial # we init'd with */
  int   maskOffset;                     /* mask byte offset */
 int  hitTermListIndex;                 /* `wordList' index for current hit */
 byte *langc, *wordc;                /* language and word char arrays */
};

/************************************************************************/
             /* Data for the approximate pattern matcher */

typedef struct XPMS_tag XPMS;
#define XPMSPN  ((XPMS *)NULL)
struct XPMS_tag
{                                 /* PBR 09-16-93 - rearrange structure */
 byte patsize;                                      /* the pattern size */
 word thresh;                  /* threshold above which a hit can occur */
 word maxthresh;                                    /* it is the string */
 word thishit;                         /* this hits value  PBR 07-12-93 */
 word maxhit;                     /* max threshold located PBR 07-12-93 */
 byte *start;                                        /* start of buffer */
 byte *end;                                            /* end of buffer */
 byte *hit;                                                    /* a hit */
 byte maxstr[DYNABYTE];                      /* string of highest value */
 byte *xa[DYNABYTE];                                     /* the x table */
};


/************************************************************************/

                 /* Data for the fast pattern matcher */

  /* PBR 08-23-91 removed cmptab from struct to static array */
#define SPMWILDSZ 80               /* PBR 08-23-91 max length of a * op */
typedef struct SPMS_tag SPMS;
#define SPMSPN  ((SPMS *)NULL)
struct SPMS_tag
{
 byte skiptab[DYNABYTE];                              /* the jump table */
 byte *start;                           /* start of area to be searched */
 byte *end;                                            /* the end of it */
 byte *hit;                       /* where last hit was (null if none ) */
 byte *sstr;
 unsigned int patsize;                            /* how big is the hit */
 unsigned int patlen;                       /* PBR 08-23-91 added for * */
 SPMS *next;                   /* PBR 08-23-91 added support for * && ? */
 PMPHR *phrase;
 byte *hitend;
 byte *langc, *wordc;                /* language and word char arrays */
 byte lang;                                          /* language flag */
 TXUPM  *upm;                                   /* for Unicode searches */
 CONST int      *cmptab;                        /* compare table to use */
};

/************************************************************************/
                /* data for the numeric pattern matcher */

#define NPGT  '>'
#define NPLT  '<'
#define NPARB '#'
#define NPEQ  '='
#define NPRNG ','
#define NPMSSSZ 80        /* max size of an xlated search string in npm */

typedef struct NPMS_tag NPMS;
#define NPMSPN  ((NPMS *)NULL)
struct NPMS_tag
{
 char ss[NPMSSSZ];
 PPMS *ps;                                        /* ppm struct pointer */
 byte **tlst;                                    /* starting token list */
 byte *hit;                                                  /* the hit */
 int hitsz;                                      /* the size of the hit */
 double hx,hy;                                      /* located value(s) */
 double x;                                           /* compare 1 value */
 double y;                                           /* compare 2 value */
 char  xop,yop;                                    /* compare operation */
};

extern TXbool freadex_strip8;
/************************************************************************/
                        /* REX FUNCTION PROTOS */
/************************************************************************/

#ifdef TEST
void  instructem   ARGS((void ));
byte *bindex       ARGS((byte **buf,byte **bufend,FFS *hitex,FFS *begex,FFS *endex));
byte *printex      ARGS((byte *buf,byte *bufend,FFS *hitex,FFS *begex,FFS *endex));
void  allocsrchbuf ARGS((void));
int   findlines    ARGS((byte *fname,FFS *ex,FFS *begex,FFS *endex));
int   offlenrex    ARGS((byte *fname,FFS *ex,FFS *begex,FFS *endex));
long  countoccs    ARGS((byte *fname,FFS *ex,FFS *begex,FFS *endex));
int   filefind     ARGS((byte *fname,FFS *ex,FFS *begex,FFS *endex));
int   parserepl    ARGS((char *s, int *a, size_t aLen));
int   rentmp       ARGS((char *tfn,char *ofn));
int   sandr        ARGS((FFS *ex,char *rs,char *fn,char *tfn,FFS *se,FFS *ee));
void  xlate_exp    ARGS((char *ex));
void  readlsec     ARGS((int drive,unsigned int sec,unsigned int n,byte *buf));
void  searchdisk   ARGS((int drive,char *s));
#endif                                                          /* TEST */

int   strn1cmp     ARGS((byte *a,byte *b));
int   dobslash     ARGS((byte * *s,byte *a));
int   dorange      ARGS((byte * *s,byte *a));
void  eatspace     ARGS((byte * *sp));
void  eatdigit     ARGS((byte * *sp));
int   reppar       ARGS((size_t sOff, byte * *s,FFS *fs));
void  initskiptab  ARGS((FFS *fs));
FFS  *closefpm     ARGS((FFS *fs));
FFS  *openfpm(size_t sOff, char *s);
#define lastexp(fs)     ((fs)->last)
#define firstexp(fs)    ((fs) ? (fs)->first : FFSPN)
FFS  *mknegexp     ARGS((FFS *fs));
int   notpm        ARGS((FFS *fs));
int   repeatpm     ARGS((FFS *fs));
int   backnpm      ARGS((FFS *fs,byte *beg));
int   forwnpm      ARGS((FFS *fs,byte *end));


typedef enum TXrexSyntax_tag
{
  TXrexSyntax_Rex,                              /* default REX syntax */
  TXrexSyntax_Re2,                              /* default RE2 syntax */
}
TXrexSyntax;

byte *rexhit       ARGS((FFS *fs));
#define rexfirst(fs)    ((fs)->first->hit)
int   rexsize      ARGS((FFS *fs));
int   rexfsize     ARGS((FFS *fs));
int   rexscnt      ARGS((FFS *fs));
FFS  *rexsexpr     ARGS((FFS *fs,int subex));
byte *rexshit      ARGS((FFS *fs,int subex));
int   rexssize     ARGS((FFS *fs,int subex));
byte *getrex       ARGS((FFS *fs,byte *buf, byte *end, TXPMOP operation));
byte *getfpm       ARGS((FFS *fs,byte *buf, byte *end, TXPMOP operation));
FFS  *closerex     ARGS((FFS *fs));
FFS  *openrex(byte *s, TXrexSyntax syntax);
int   fastpm       ARGS((FFS *fs));
#if defined(_WIN32) && defined(GENERIC_READ)
int   freadex      ARGS((HANDLE fh,byte *buf,int len,FFS *ex));
int   pipereadex   ARGS((HANDLE fh,byte *buf,int len,FFS *ex));
#else
int   freadex      ARGS((FILE *fh,byte *buf,int len,FFS *ex));
int   pipereadex   ARGS((FILE *fh,byte *buf,int len,FFS *ex));
#endif

int   rexsavemem ARGS((int yes));       /* KNG 040413 */

/************************************************************************/
                          /* XPM FUNC PROTOS */
/************************************************************************/

XPMS *closexpm     ARGS((XPMS *xs));
XPMS *openxpm      ARGS((char *s,int threshold));
byte *getxpm       ARGS((XPMS *xs, byte *buf, byte *end, TXPMOP operation));
/************************************************************************/
                          /* PPM FUNC PROTOS */
/************************************************************************/

int   TXppmStrcmp  ARGS((PPMS *ps, byte *a, byte *b));
int   TXppmStrPrefixCmp ARGS((PPMS *ps, byte *prefix, byte *s));
int CDECL ppmsortcmp ARGS((CONST void *,CONST void *));
PPMS *closeppm     ARGS((PPMS *ps));
PPMS *openppm      ARGS((byte **sl));
int   ppmstrcmp    ARGS((byte *ls,byte *hs));
int   ppmstrn      ARGS((PPMS *ps, PMBITGROUP mask));
int   pfastpm      ARGS((PPMS *ps));
byte *getppm       ARGS((PPMS *ps, byte *buf, byte *end, TXPMOP op));
void  xlateppm     ARGS((PPMS *tfs));
#define ppmsrchs(a) ((a)->orgTermList[(a)->hitTermListIndex])
#define ppmsrchp(a) ((a)->phraseObjList[(a)->hitTermListIndex])
#define ppmhitsz(a)  ((a)->hitend-(a)->hit)
#define TX_PPM_TERM_IS_LANGUAGE_QUERY(ppm, i)   \
  (!!((ppm)->flags[(i)] & TXPPM_BF_IS_LANGUAGE_QUERY))
#define ppmlang(a)    TX_PPM_TERM_IS_LANGUAGE_QUERY(a, (a)->hitTermListIndex)
#define TX_PPM_NUM_STRS(ppm)    ((ppm)->numTerms)
#define TX_PPM_PHRASE(ppm, i)   ((ppm)->phraseObjList[i])
/************************************************************************/

                      /* SPM function prototypes */
SPMS *openspm      ARGS((char *s));
SPMS *openspmmm3s  ARGS((char *s, MM3S *ms));
SPMS *closespm     ARGS((SPMS *fs));
int   findspm      ARGS((SPMS *fs));
byte *getspm       ARGS((SPMS *fs, byte *buf, byte *end, TXPMOP op));
int   spmhitsz     ARGS((SPMS *fs));
#define spmsrchp(a) ((a)->phrase)
#define spmlang(a) ((a)->lang)
/************************************************************************/
                             /* NPM protos */
NPMS *opennpm      ARGS((char *s));
NPMS *closenpm     ARGS((NPMS *));
byte *getnpm       ARGS((NPMS *,byte *,byte *,int));

/************************************************************************/
#endif                                                        /* PM_H */
