#ifndef MMAPI_H
#define MMAPI_H

/**********************************************************************/
#ifdef __cplusplus
extern "C" {
#endif
#include "txcoreconfig.h"                             /* for TX_VERSION_NUM */
/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(push,api3_h,2)
#endif
#ifdef _WIN32
#  if defined(_WIN32) && defined(GENERIC_READ)
#     define FHTYPE HANDLE
#     define FHTYPEPN INVALID_HANDLE_VALUE
#  else
#     define FHTYPE FILE *
#     define FHTYPEPN (FILE *)NULL
#  endif
#else
#  define FHTYPE FILE *
#  define FHTYPEPN (FILE *)NULL
#endif
#include "sizes.h"
#include "os.h"
#include "pm.h"
#include "cp.h"
#include "mdx.h"
#include "presuf.h"
#include "mm3e.h"
#include "eqv.h"
#include "mdpar.h"
#include "mmsg.h"
#ifdef _WIN32
#  ifdef DWORD
      extern DWORD FAR epi_wmemamt(DWORD newamt);
#  endif
   extern int  FAR       setmmsgfh    ARGS((int nfh));
   extern char FAR * FAR setmmsgfname ARGS((char FAR *nfn));
#ifdef __BORLANDC__                                     /* parser bug */
   extern void FAR       setmmsg      ARGS(( int (FAR *func)() ));
#else
   extern void FAR       setmmsg      ARGS(( int (FAR *func)(int,char FAR *,char FAR *,va_list) ));
#endif
#endif

#ifndef TX_VERSION_NUM
error: TX_VERSION_NUM undefined;
#endif /* !TX_VERSION_NUM */

/**********************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif                                                        /* ARGS */
/**********************************************************************/
#define BP (byte FAR *)

#define API3SUFFIXPROC       1
#define API3PREFIXPROC       1
#define API3REBUILD          1
#define API3INCSD            0
#define API3INCED            1
#define API3WITHINPROC       1
#define API3SUFFIXREV        0
#define API3MINWORDLEN       5
#define API3INTERSECTS       (-1)
#define API3SDEXP            BP"[^\\digit\\upper][.?!][\\space'\"]"
#define API3EDEXP            BP"[^\\digit\\upper][.?!][\\space'\"]"
#define API3SEE              0
#define API3KEEPEQVS         1
#define API3KEEPNOISE        1
#define API3MAXSELECT        20000000
#define   API3DENYSILENT       0
#define   API3DENYWARNING      1
#define   API3DENYERROR        2
#define API3DENYMODE         API3DENYWARNING
#define API3ALPOSTPROC       1
#define API3ALLINEAR         1
#define API3ALWILD           1
#define API3ALNOT            1
#define API3ALWITHIN         1
#define API3ALINTERSECTS     1
#define API3ALEQUIVS         1
#define API3ALPHRASE         1
#define API3EXACTPHRASE      API3EXACTPHRASEON
#define API3EXACTPHRASEOFF                      0
#define API3EXACTPHRASEON                       1
/* `ignorewordposition' also implies `off', ie. noise ignored too: */
#define API3EXACTPHRASEIGNOREWORDPOSITION       2
#define API3QMINWORDLEN      1
#define API3QMINPRELEN       1
#define API3QMAXSETS         100
#define API3QMAXSETWORDS     0
#define API3QMAXWORDS        0
#define API3DEFSUFFRM        1/* JMT 1999-10-01 */
#define API3REQSDELIM        1/* JMT 2000-02-11 */
#define API3REQEDELIM        1/* JMT 2000-02-11 */
#define API3OLDDELIM         0/* JMT 2000-02-11 */

/* withinmode:
 * bit 0:   unit: char/word
 * bit 1-2: type: radius/span/gap
 */
#define API3WITHIN_GET_UNIT(mode)       ((mode) & 0x1)
#define API3WITHIN_GET_TYPE(mode)       ((mode) & 0x6)
/* ...UNITs: */
#define API3WITHINCHAR       0
#define API3WITHINWORD       1
/* API3WITHIN_TYPE_RADIUS is legacy, must be 0: */
#define API3WITHIN_TYPE_RADIUS  (0 << 1)  /* within N-left/N-right of anchor*/
#define API3WITHIN_TYPE_SPAN    (1 << 1)  /* entire span is <= N */
/* future implementation: #define API3WITHIN_TYPE_GAP     (2 << 1) */
#if TX_VERSION_MAJOR >= 6
/* `word span' is most logical and useful mode from a user perspective,
 * and is also fastest to search for (with index at least):
 */
#  define API3WITHINMODE     (API3WITHINWORD | API3WITHIN_TYPE_SPAN)
#else
#  define API3WITHINMODE     (API3WITHINCHAR | API3WITHIN_TYPE_RADIUS)
#endif

/* KNG 2004-04-14 */
#define API3PHRASEWORDMONO   0  /* suffix/wild as monolithic word */
#define API3PHRASEWORDNONE   1  /* no suffix/wild proc on phrase */
#define API3PHRASEWORDLAST   2  /* suffix/wild proc last word */
#define API3PHRASEWORDALL    3  /* suffix/wild proc all words */
#if TX_VERSION_MAJOR >= 5
#define API3PHRASEWORDPROC   API3PHRASEWORDLAST
#else
#define API3PHRASEWORDPROC   API3PHRASEWORDMONO
#endif

/* Version 6+ only: */
#define API3_QUERYSETTINGS_DEFAULTS             0
#define API3_QUERYSETTINGS_TEXIS5DEFAULTS       1
#define API3_QUERYSETTINGS_VORTEXDEFAULTS       2
#define API3_QUERYSETTINGS_PROTECTIONOFF        3

#define R3DB_LOWTIME         0L
#ifdef ULONG_MAX
#define R3DB_HIGHTIME        ULONG_MAX/* in ANSI limits.h, old unix values.h */
#else
#define R3DB_HIGHTIME        LONG_MAX/* in ANSI limits.h, old unix values.h */
#endif
#define R3DB_ENABLEMM        1
#define R3DB_BLOCKSZ         35840                             /* 35k */
#define R3DB_BLOCKMAX        71680                             /* 70k */
#define R3DB_MAXSIMULT       200
#define R3DB_ADMINMSGS       1
#define R3DB_ALLOW
#define R3DB_IGNORE          "*.arc", "*.zip", "*.pcx", "*.tif", "*.gif",
#ifdef unix
/* KNG 990322 Default equivs is now builtin: */
/* KNG 20090107 See txGetApicpDefaults() install-dir fixup if these change: */
/* #  define API3EQPREFIX           BP"/usr/local/morph3/equivs" */
#  define API3EQPREFIX           BP"builtin"
#  define API3UEQPREFIX          BP"/usr/local/morph3/eqvsusr"
#  define API3PROFILE            BP"/usr/local/morph3/profile.mm3"
#  define R3DB_DATABASE          BP"/usr/local/morph3/index/3db"
#  define R3DB_FILESPEC          BP"*"
#  define R3DB_BUFLEN            128000
#  define R3DB_SYSALLOW
#  define R3DB_SYSIGNORE         "*.o", "*.a", "*.Z", "*.z",".[!/.]*","*/.[!/.]*",
#else
#  ifdef _WIN32
/* KNG 990322 Default equivs is now builtin: */
/* KNG 20090107 See txGetApicpDefaults() install-dir fixup if these change: */
/* #     define API3EQPREFIX        BP"c:\\morph3\\equivs" */
#     define API3EQPREFIX        BP"builtin"
#     define API3UEQPREFIX       BP"c:\\morph3\\eqvsusr"
#     define API3PROFILE         BP"c:\\morph3\\profile.mm3"
#     define R3DB_DATABASE       BP"c:\\morph3\\index\\3db"
#     define R3DB_FILESPEC       BP"*"
#     define R3DB_BUFLEN         30000
#     define R3DB_SYSALLOW
#     define R3DB_SYSIGNORE      "*.com","*.exe","*.bin","*.ovl","*.obj","*.sys","*.lib","*.dll","*.bmp",
#  else
     stop.                                /* unknown system */
#  endif
#endif

/**********************************************************************/

#define APICP   struct mm_api_cp
#define APICPPN ((APICP *)NULL)
APICP
{
 byte             suffixproc   ;
 byte             prefixproc   ;
 byte             rebuild      ;
 byte             incsd        ;
 byte             inced        ;
 byte             withinproc   ;
 byte             suffixrev    ;
 int              minwordlen   ;
 int              intersects   ;
 byte FAR *       sdexp        ;
 byte FAR *       edexp        ;
 byte FAR *       query        ;
 /* `set[n]' is the equiv-format string representing the list of terms
  * (or the REX/etc. expression) for set n.  It is in original query order,
  * empty-string-terminated, and owned+alloced by APICP:
  */
 byte FAR * FAR * set          ;
 byte FAR * FAR * suffix       ;
 byte FAR * FAR * suffixeq     ;
 byte FAR * FAR * prefix       ;
 byte FAR * FAR * noise        ;
 byte FAR *       eqprefix     ;
 byte FAR *       ueqprefix    ;
 byte             see          ;
 byte             keepeqvs     ;
 byte             keepnoise    ;
 int  (FAR * eqedit ) ARGS((APICP FAR *apicp));/* 0 means ok to continue, else stop */
 int  (FAR * eqedit2) ARGS((APICP FAR *apicp,EQVLST ***eqlp));/* 0 means ok to continue, else stop */

 byte FAR *       database;     /* 3db - name of database master file */
 ulong            lowtime;                  /* 3db - oldest file time */
 ulong            hightime;                 /* 3db - newest file time */
 byte FAR *       filespec;         /* 3db - wildcard filename filter */
 byte             enablemm;/* 3db - is the mm3 api to be opened at all */
 int              buflen;/* 3db - size of buffer to use for mm post-search */
 char FAR * FAR * worddef;
 char FAR * FAR * blockdelim;
 ulong            blocksz;
 ulong            blockmax;
 int              maxsimult;
 byte             adminmsgs;
 char FAR * FAR * allow;
 char FAR * FAR * ignore;

 long             maxselect;/* max amount of data to select before cutoff */
 byte FAR *       profile;    /* where these settings came from/go to */
 void FAR *       usr          ;            /* arbitrary user pointer */
                            /* MAW 06-01-98 - query parser/proc flags */
 int              denymode;                             /* parse,proc */
 byte             alpostproc;                                 /* proc */
 byte             allinear;                             /* parse,proc */
 byte             alwild;                                    /* parse */
 byte             alnot;                                     /* parse */
 byte             alwithin;                                  /* parse */
 byte             alintersects;                              /* parse */
 byte             alequivs;                                  /* parse */
 byte             alphrase;                                  /* parse */
 byte             exactphrase;                                /* proc */
 int              qminwordlen;                               /* parse */
 int              qminprelen;                                /* parse */
 int              qmaxsets;                                  /* parse */
 int              qmaxsetwords;                          /* KNG index */
 int              qmaxwords;                             /* KNG index */
 byte		  defsuffrm;    /* JMT 1999-10-01 - remove def suffix */
 byte             reqsdelim;    /* JMT 2000-02-11 - require end delim */
 byte             reqedelim;  /* JMT 2000-02-11 - require start delim */
 byte             olddelim;   /* JMT 2000-02-11 - old delim behaviour */
 int              withinmode;    /* JMT 2004-02-27 - within word/char */
 int              withincount;           /* JMT 2004-02-27 - within N */
 int              phrasewordproc;                   /* KNG 2004-04-14 */
 int              textsearchmode;               /* TXCFF mode for txt srch */
 int              stringcomparemode;            /* TXCFF mode for str comp */
 int              *setqoffs;                    /* query offsets for `set' */
 int              *setqlens;                    /* query lengths for `set' */
 /* `originalPrefixes[n]' is the original-user-query prefix for `set[n]',
  * i.e. the set logic, tilde, open-paren or REX/NPM/XPM char(s), if any.
  * NULL-terminated array:
  */
 char            **originalPrefixes;
 /* `sourceExprLists[n]' is the source expressions or phrases for `set[n]',
  * i.e. from the original query (but without quotes/parens/logic).
  * NULL-terminated array of NULL-terminated arrays:
  */
 char           ***sourceExprLists;
};

/* internal txGetApicpDefaults() use: */
extern CONST byte       TxEqPrefixDefault[];
extern CONST byte       TxUeqPrefixDefault[];

extern APICP            TxApicpDefault;         /* texis.ini-modifiable */
extern APICP            TxApicpDefaultIsFromTexisIni;
extern APICP            TxApicpBuiltinDefault;
#ifndef EPI_ENABLE_APICP_6
extern int              TxEpiEnableApicp6;      /* EPI_ENABLE_APICP_6 set */
#endif /* !EPI_ENABLE_APICP_6 */

void    TXapicpFreeDefaultStr ARGS((char *s));
void    TXapicpFreeDefaultStrLst ARGS((char **s));
TXbool  TXapicpGetLikepAllMatch(void);
TXbool  TXapicpSetLikepAllMatch(TXbool likepAllMatch);
TXbool  TXapicpGetLikepObeyIntersects(void);
TXbool  TXapicpSetLikepObeyIntersects(TXbool likepObeyIntersects);

extern int     (*TxSetmmapiValidateFunc) ARGS((TXPMBUF *pmbuf));

/**********************************************************************/

#define MMAPI struct mm_api
#define MMAPIPN (MMAPI *)NULL
MMAPI
{
 APICP   FAR *acp;
 MM3S    FAR *mme;
 EQV     FAR *eq;  /* don't close this one, may be a copy from dupmmapi() */
 EQV     FAR *eqreal;
 int     intersects;                            /* MAW 05-25-95 */
 int     qintersects;                           /* KNG 001024 query's @ val */
 TXbool    intersectsIsArbitrary;
};

extern CONST char FAR api3cpr[];           /* api copyright message */
extern char FAR api3_Out_of_mem[];         /* "Out of memory" message */
/**********************************************************************/
extern APICP FAR * FAR closeapicp  ARGS((APICP FAR *acp));
extern MMAPI FAR * FAR closemmapi  ARGS((MMAPI FAR *mp));
extern char  FAR * FAR getmmapi    ARGS((MMAPI FAR *mp,byte FAR *buf,byte FAR *end,int op));
extern int         FAR infommapi   ARGS((MMAPI FAR *mp,int index,char FAR * FAR *srched,char FAR * FAR *found,int FAR *len));
extern APICP FAR * FAR openapicp   ARGS((void));
extern MMAPI *openmmapi(const char *query, TXbool isRankedQuery, APICP *acp);
extern int         FAR rdmmapi     ARGS((byte FAR *buf,int n,FHTYPE fh,MMAPI FAR *mp));
extern MMAPI *setmmapi(MMAPI *mp, const char *query, TXbool isRankedQuery);
extern void        FAR closemmeq   ARGS((MMAPI FAR *));
extern int         FAR openmmeq    ARGS((MMAPI FAR *));
extern MMAPI *dupmmapi(MMAPI *omp, const char *query, TXbool isRankedQuery);
extern APICP FAR * FAR dupapicp    ARGS((APICP FAR *acp));
extern int         FAR readapicp   ARGS((APICP *acp,char *fn));

extern MM3S  FAR * FAR open3eapi   ARGS((APICP FAR *));/* internal use */
extern MM3S  FAR * FAR close3eapi  ARGS((MM3S FAR *));/* internal use */
extern int get3eqsapi(MMAPI *mp, TXbool isRankedQuery); /* internal use */
extern byte *getmmdelims (const byte *query, APICP *apicp); /* internal use */
extern byte  FAR * FAR * FAR blstdup ARGS((byte FAR * FAR *slst));
extern byte FAR * FAR bstrdup ARGS((byte FAR *s));

extern int   FAR       acpdeny     ARGS((APICP FAR *acp,char FAR *feature));

extern char **TXapi3FreeNullList ARGS((char **list));
extern byte **TXapi3FreeEmptyTermList(byte **lst);

/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(pop,api3_h)
#endif
#ifdef __cplusplus
}
#endif
/**********************************************************************/
#endif                                                     /* MMAPI_H */
