/* -=- kai-mode: John -=- */
#ifndef TEXINT_H
#define TEXINT_H

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#include "txcoreconfig.h"
#include "version.h"
#ifdef _WIN32
#  include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#ifndef _WIN32
#  include <sys/time.h>
#  include <sys/resource.h>
#endif /* _WIN32 */
#include <signal.h>
#include "sizes.h"
#include "rppm.h"
#include "fldcmp.h"
#include "keyrec.h"
#include "txtypes.h"
#include "meter.h"
#include "strcat.h"
#include "unicode.h"
#include "refInfo.h"
#include "ezsock.h"
#include "txputmsgflags.h"
#include "urlprotocols.h"
#include "urlutils.h"
#include "cgi.h"

#ifndef EPI_HAVE_RLIM_T
typedef long rlim_t;
#endif /* !EPI_HAVE_RLIM_T */

#undef QUERY                            /* avoid arpa/nameser.h collision */

#if defined(TX_DEBUG) && defined(TX_NT_LARGEFILE)
#	undef NEW_I
#	define NEW_I
#endif
/******************************************************************/

#define COLL_ASC	'A'
#define COLL_DESC	'D'

#define O_WTF	0

/******************************************************************/

/*
	All the functions in here will end up with a TX prefix.
	Theses are supposed to be completely internal to Texis,
	and never called by the user.
*/

#define processexists(a)	TXprocessexists(a)

#define _locktable(a,b)		TXlocktable((a),(b))
#define _unlocktable(a,b)	TXunlocktable((a),(b))
#define locksystbl(a,b,c)	TXlocksystbl((a),(b),(c))
#define unlocksystbl(a,b,c)	TXunlocksystbl((a),(b),(c))

#define delindex(a, b)		TXdelindex((a),(b))

#define flush3dbi(a)		TXflush3dbi((a))

#define makepredvalid(a, b, c)	TXmakepredvalid((a), (b), (c))

/* ------------------------------------------------------------------------ */

typedef struct TXMKIND_tag      TXMKIND;
#define TXMKINDPN       ((TXMKIND *)NULL)

TXMKIND *TXmkindCreateBtree ARGS((DBTBL *dbtb, char *field,
            CONST char *indexName, char *indfile, int unique, int isRebuild,
            TXindOpts *options));
TXMKIND *TXmkindCreateInverted ARGS((DBTBL *dbtb, char *field,
	    CONST char *indexName, char *indfile, int unique, int isRebuild,
            TXindOpts *options));
TXMKIND *TXmkindClose ARGS((TXMKIND *ind));
int     TXmkindBuildIndex ARGS((DDIC *ddic, DBTBL *dbtb, TXMKIND *ind,
                                int *numTblRdLocks));

DD *TXordspec2dd ARGS((DBTBL *dbtb, char *field, int maxf, int skip,
		       int novar, TXindexValues indexValues, char *collSeq));

/* ------------------------------------------------------------------------ */

/* Note: must be sorted ascending: */
/* Note: if this list is altered, update TXindOptsProcessOptions(): */
#define TX_INDEX_OPTIONS_SYMBOLS_LIST   \
I(counts)               \
I(indexmaxsingle)       \
I(indexmem)             \
I(indexmeter)           \
I(indexspace)           \
I(indexvalues)          \
I(indexversion)         \
I(keepnoise)            \
I(max_index_text)       \
I(noiselist)            \
I(stringcomparemode)    \
I(textsearchmode)       \
I(wordexpressions)      \
I(wordpositions)

typedef enum TXindOpt_tag
{
  TXindOpt_Unknown = -1,                        /* must be first */
#undef I
#define I(tok)  TXindOpt_##tok,
  TX_INDEX_OPTIONS_SYMBOLS_LIST
  TXindOpt_NUM                                  /* must be last */
#undef I
}
TXindOpt;
#define TXindOptPN      ((TXindOpt *)NULL)

/* All `WITH ...' index options for a `CREATE INDEX ...' statement: */
#ifndef TXindOptsPN
typedef struct TXindOpts_tag    TXindOpts;
#  define TXindOptsPN   ((TXindOpts *)NULL)
#endif

struct TXindOpts_tag
{
  /* Raw options, set with TXindOptsGetRawOptions(): */
  TXindOpt      option[TXindOpt_NUM];           /* unique options */
  char          **values[TXindOpt_NUM];         /* alloc'd NULL-term; for ""*/
  int           numOptions;                     /* # options in "" */

  byte          wasProcessed;                   /*`rawValues' were processed*/
  /* Cooked values: global defaults on open, `WITH ...' options merged
   * in later:
   */
  BTPARAM       btparams;
  TXCFF         textsearchmode;
  char          *indexspace;                    /* alloc'd, w/trail `/' */
  byte          keepnoise;
  char          **noiselist;                    /* alloc'd, empty-str-term. */
  char          **wordExpressions;              /* alloc'd, empty-str-term. */
  int           fdbiVersion;
  int           fdbiMaxSingleLocs;

  size_t        indexmem;
  TXMDT         indexmeter;
};

TXindOpt        TXindOptStringToEnum ARGS((const char *s));
const char      *TXindOptEnumToString ARGS((TXindOpt opt));

TXindOpts       *TXindOptsOpen ARGS((DDIC *ddic));
TXindOpts       *TXindOptsClose ARGS((TXindOpts *options));
int           TXindOptsGetRawOptions ARGS((TXindOpts *options, QNODE *qnode));
int             TXindOptsProcessRawOptions ARGS((TXindOpts *options,
                                int *indexType, int forUpdate));

/******************************************************************/

#ifdef _WIN32
char    *TXwideCharToUtf8(TXPMBUF *pmbuf, const wchar_t *wideStr,
			  size_t wideByteLen);
wchar_t *TXutf8ToWideChar(TXPMBUF *pmbuf, const char *utf8Str,
			  size_t utf8ByteLen);
#endif

#ifdef _WIN32
typedef unsigned        UID_T;
typedef unsigned        GID_T;
#else /* !_WIN32 */
typedef uid_t           UID_T;
typedef gid_t           GID_T;
#endif /* !_WIN32 */
#define UID_TPN ((UID_T *)NULL)
#define GID_TPN ((GID_T *)NULL)

/* Info about a particular process: */
typedef struct TXprocInfo_tag
{
  byte          isAlloced;                      /* if object alloc'd */
  PID_T         pid;
  PID_T         parentPid;                      /* -1 if unknown */
  int           argc;                           /* may be 0 */
  char          **argv;                         /* may be empty */
  char          *cmd;                           /* may differ from argv[0] */
  char          *exePath;                       /* full path to executable */
  UID_T         uidReal, uidEffective, uidSaved, uidFileSystem; /* -1 unk. */
  GID_T         gidReal, gidEffective, gidSaved, gidFileSystem; /* -1 unk. */
  char          *sidUser;                       /* Windows: SID of user */
  EPI_HUGEINT   vsz, vszPeak, rss, rssPeak;     /* in bytes; -1 unk. */
  char          state[32];                      /* e.g. `R (running)' */
  double        startTime;                      /* time_t; -1 if unknown */
  double        userTime, systemTime;           /* in seconds; -1 unknown */
}
TXprocInfo;

/* Opaque list of all-processes info: */
typedef struct TXprocInfoList_tag       TXprocInfoList;

typedef enum TXtrap_tag         /* <TRAP SIGNALS=n>; also non-Vortex sigs */
{
  TXtrap_CatchNormal     = 0x0001, /* catch normal signals (SIGTERM etc.)*/
  TXtrap_CatchBad        = 0x0002, /* catch "bad" signals (SIGSEGV etc.) */
  TXtrap_CoreViaNull     = 0x0004, /* dump core after bad signal via *NULL */
  TXtrap_CoreViaReturn   = 0x0008, /* dump core after bad signal via return */
  TXtrap_InfoRegisters   = 0x0010, /* print registers at bad signal */
  TXtrap_InfoStack1K     = 0x0020, /* print +1KB of stack at bad signal */
  TXtrap_InfoStack16K    = 0x0040, /* print +16KB of stack at bad signal */
  TXtrap_InfoLocationBad = 0x0080, /* print location for bad signals */
  TXtrap_IgnoreSighup    = 0x0100, /* ignore SIGHUP */
  TXtrap_TimeoutBad      = 0x0200, /* treat timeout as bad signal */
  TXtrap_InfoPid         = 0x0400, /* print info of signalling PID */
  TXtrap_InfoPpid        = 0x0800, /* "" + PPID (+_InfoPid: all ancestors) */
  TXtrap_InfoBacktrace   = 0x1000, /* print backtrace at bad signal */
  TXtrap_All             = 0x1FFF, /* all flags */
  TXtrap_Default         =
  /* If default changes, see various Vortex tests, webtests driver.vs: */
  (TXtrap_CatchNormal | TXtrap_CatchBad | TXtrap_InfoLocationBad |
   TXtrap_InfoPid | TXtrap_InfoRegisters | TXtrap_InfoBacktrace)
}
TXtrap;

size_t TXgetSystemCpuTimes(TXPMBUF *pmbuf, double **times);
double TXgetSystemBootTime(TXPMBUF *pmbuf);

#ifdef _WIN32
int    TXqueryInformationProcess(TXPMBUF *pmbuf, HANDLE handle, int infoClass,
				 void *buf, size_t bufSz);
#endif /* _WIN32 */

size_t TXprocInfoListPids(TXPMBUF *pmbuf, PID_T **pids,
                          TXprocInfoList **list);
TXprocInfoList *TXprocInfoListClose(TXprocInfoList *procInfoList);
TXprocInfo *TXprocInfoByPid(TXPMBUF *pmbuf, TXprocInfoList *list,
                            PID_T pid, byte *heap, size_t heapSz);
TXprocInfo *TXprocInfoClose(TXprocInfo *procInfo);
size_t	TXprintPidInfo(char *buf, size_t bufSz, PID_T pid, PID_T *ppid);
size_t	TXprintUidAndAncestors(char *buf, size_t bufSz,
		       TX_SIG_HANDLER_SIGINFO_TYPE *sigInfo, TXtrap flags);
size_t	TXprintSigCodeAddr(char *buf, size_t bufSz,
			   TX_SIG_HANDLER_SIGINFO_TYPE *sigInfo);

int     TXkill ARGS((PID_T pid, int sig));

/* Does a process exist */
int	TXprocessexists ARGS((PID_T));
#ifdef _WIN32
int	TXprocessexists_handle ARGS((HANDLE proc));
#else /* !_WIN32 */
PID_T   TXfork(TXPMBUF *pmbuf, const char *description, const char *cmdArgs,
               int flags);
#endif /* !_WIN32 */
int     TXgetInForkedChild(void);
char	*TXgetprocname ARGS((int));


int	TXtimedout ARGS((DDIC *));
int	TXsqlWasCancelled(DDIC *ddic);
int	TXresetproperties ARGS((DDIC *));
int	TXsetproperties ARGS((DDIC *ddic, char *set));

char   *TXgetstrne ARGS((char *));

char   *TXdupwsep ARGS((char *path));

int     TXparseWithinmode ARGS((TXPMBUF *pmbuf, CONST char *val, int *mode));

extern const char	TXfailIfIncompatibleMsg[];

/******************************************************************/

#define IINDEX struct tagIINDEX
IINDEX {
#ifdef NEW_I
	DBIDX	*orig;		/* Original */
	DBIDX	*inv;		/* Inverted, data->key */
	DBIDX	*revinv;	/* Reversed and inverted */
	DBIDX	*mirror;	/* Data->data */
	DBIDX	*ordered;	/* Ordered by order */
	DBIDX	*rev;		/* Reverse ordered */
#else
	BTREE	*orig;		/* Original: data -> recid */
	BTREE	*inv;		/* Inverted: recid -> data */
	BTREE	*revinv;	/* Reversed and inverted */
	BTREE	*mirror;	/* Fixed B-tree: recid -> recid */
	BTREE	*ordered;	/* Ordered by order */
	BTREE	*rev;		/* Reverse ordered */
#endif
	PRED	*start;
	PRED	*end;
	char	**order;
	int	ko;
	int	ki;
	int	kr;
	int	km;
	int	ks;
	int	kv;
	EPI_HUGEUINT	cntorig;	/* Number of items in orig */
	EPI_HUGEINT	rowsReturned;	/* != -1: `orig' size after likeprows */
	int	nrank;		/* Number of rank things included */
	int	orank;		/* Order by rank.  There are cases
	                           where you want to keep a rank value,
				   but not order by it. */
	IINDEX	*piand;		/* Index it is already anded with */
};
#define IINDEXPN        ((IINDEX *)NULL)

/******************************************************************/

struct IINODE_tag {
	IINDEX	*index;
	int	op;
	IINODE	*left;
	IINODE	*right;
	PRED	*ipred;
	PRED	*gpred;
	int	fgpred;
#ifdef CACHE_IINODE
	int	cached;
#endif
};

/******************************************************************/

typedef struct TBSPEC
{
	PRED    *pred;  /* Full pred for this table */
	PROJ    *proj;  /* Ordering we want */
	IINDEX  *pind;	/* Recids to be anded in */
	SLIST	*flist; /* List of desired fields */
	SLIST	*pflist;/* List of desired fields for parent */
	PRED    *gpred; /* Pred for this table which is anded at this point */
	int	haveOrderByNum;	/* flagged for LIKEP ORDER BY num */
} TBSPEC ;
#define TBSPECPN	((TBSPEC *)NULL)

/******************************************************************/
typedef struct INDEXINFO INDEXINFO;

typedef struct INDEXSCORE
{
	const INDEXINFO	*indexinfo;	/* parent INDEXINFO */
	int	orgArrayIdx; /*INDEXINFO iscore,fields,itype,iname etc. index*/
	int	score;
} INDEXSCORE;

#define TX_INDEX_SUBSCORE_MAX	1023

struct INDEXINFO
{
	int	numIndexes;
	char	*itypes;		/* array of `numIndexes' index types*/
	char	**paths;		/* array of `numIndexes' file paths */
	char	**fields;		/* "" */
	char	**sysindexParamsVals;	/* SYSINDEX.PARAMS values */
	INDEXSCORE *iscores;		/* array of `numindex' scores */
	int	initialized;
	int	lastreturn;
	TBSPEC	*tbspec;
};

/******************************************************************/

void	*TXbtcacheopen ARGS((DBTBL *, char *, int, int,
                             CONST char *sysindexParams));
void	*TXbtcacheclose ARGS((DBTBL *, char *, int, int, void *));
int	TXbtfreecache ARGS((DBTBL *));

/******************************************************************/

typedef struct TXAPP_tag        TXAPP;
#define TXAPPPN ((TXAPP *)NULL)

typedef enum TXCREATELOCKSMETHOD_tag
{
	TXCREATELOCKSMETHOD_UNKNOWN = -1,		/* must be first */
	TXCREATELOCKSMETHOD_DIRECT,			/* do directly */
	TXCREATELOCKSMETHOD_MONITOR,		/* do via Texis Monitor */
	TXCREATELOCKSMETHOD_NUM			/* must be last */
}
TXCREATELOCKSMETHOD;
#define TXCREATELOCKSMETHODPN	((TXCREATELOCKSMETHOD *)NULL)

TXCREATELOCKSMETHOD TXstrToCreateLocksMethod(const char *s, const char *e);
const char *TXcreateLocksMethodToStr(TXCREATELOCKSMETHOD method);
int TXsetCreateLocksMethods(TXPMBUF *pmbuf, TXAPP *app, const char *srcDesc,
                            const char *s, size_t sLen);

extern const char	TXxCreateLocksOptionsHdrName[];

#define TX_CREATELOCKS_VERBOSE_IS_SET()     (TXverbosity >= 2)

/* ------------------------------------------------------------------------ */

int	TXlockindex	ARGS((DBTBL *, int, ft_counter *));
int	TXunlockindex	ARGS((DBTBL *, int, ft_counter *));
int	TXlockandload	ARGS((DBTBL *, int, char **));
int	TXlocktable	ARGS((DBTBL *, int));
int	TXunlocktable	ARGS((DBTBL *, int));
int	TXlocksystbl(DDIC *ddic, SYSTBL tblid, int ltype, ft_counter *fc);
int	TXunlocksystbl(DDIC *ddic, SYSTBL tblid, int ltype);

DBLOCK *TXdblockOpenViaMethods(TXPMBUF *pmbuf, const char *path,
                         TXCREATELOCKSMETHOD *methods, int timeout, int *sid);

typedef enum TXDDOPENFLAG_tag
  {
    TXDDOPENFLAG_READ_ONLY      = (1 << 0),     /* open read-only */
    TXDDOPENFLAG_NO_DB_MONITOR  = (1 << 1),     /* do not start a db monitor*/
    TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY = (1 << 2), /*i.e. no TXApp methods*/
    TXDDOPENFLAG_IGNORE_OPEN_FAILURES   = (1 << 3) /* for createdb */
  }
TXDDOPENFLAG;
DDIC	*TXddopen(TXPMBUF *pmbuf, const char *pname, TXDDOPENFLAG flags);

int	TXddicdefaultoptimizations ARGS((DDIC *ddic));
DDIC	*ddreset ARGS((DDIC *ddic));
PID_T   tx_chkrundbmonitor ARGS((DDIC *ddic, time_t now));
int	TXshortsleep	ARGS((int));
int	TXsandman	ARGS((void));
int	TXrooster	ARGS((void));
int	rgetcounter	ARGS((DDIC *, ft_counter *, int));
IDBLOCK *getshared(TXPMBUF *pmbuf, DBLOCK *dbl, const char *path,
		   TXbool readOnly);
DBLOCK *ungetshared(TXPMBUF *pmbuf, DBLOCK *l, TXbool readOnly);
TXEXIT TXdumpshared(TXPMBUF *pmbuf, const char *database, const char *outPath);

#define SLEEP_MULTIPLIER	0
#define SLEEP_METHOD		1
#define SLEEP_INCREMENT		2
#define SLEEP_DECREMENT		3
#define SLEEP_MAXSLEEP		4
#define SLEEP_BACKOFF		5
#define SLEEP_SLEEPTYPE		6
/* Make sure SIZEOFLIST is one past max */
#define SLEEP_SIZEOFLIST	7

int	TXsetsleepparam	ARGS((unsigned int, int));

/******************************************************************/

int	TXdelindex	ARGS((char *, int));
int	TXtouchindexfile ARGS((DDIC *));
int	TXstartcleanup	ARGS((DDIC *));
PID_T	TXgetcleanuppid	ARGS((DDIC *));
int	TXsetcleanuppid	ARGS((DDIC *, PID_T));
int	TXclosecacheindex ARGS((DDIC *ddic, char *name));
int	TXdelTableFile ARGS((CONST char *tablePath, int tableType));
int	TXdropdtables ARGS((DDIC *ddic));

#ifdef TX_INDEXDEBUG
#  define TX_INDEXDEBUG_MSG(a)  putmsg a
#else /* !TX_INDEXDEBUG */
#  define TX_INDEXDEBUG_MSG(a)
#endif /* !TX_INDEXDEBUG */

char   *TXgetusername ARGS((DDIC *));
/******************************************************************/

EPI_OFF_T	TXsetrecid ARGS((BTLOC *, EPI_OFF_T));
int	TXindcompat ARGS((char *));
int	TXsetdatefmt ARGS((char *));
int     TXsetjsonfmt(char *fmt );

/******************************************************************/

void	TXflush3dbi ARGS((A3DBI *));
size_t	TXgetblockmax ARGS((void));

/******************************************************************/
char 	*TXpredToFieldOrderSpec(PRED *pred);

PRED	*TXtreetopred ARGS((DDIC *, QNODE *, int, FLDOP *, DBTBL *));
PRED	*TXmakepredvalid ARGS((PRED *, DBTBL *, int, int, int));
PRED	*TXduppredvalid  ARGS((PRED *, DBTBL *, int, int, int));
PRED	*TXduppredvalid2 ARGS((PRED *, DBTBL *, int, int, int));
PRED	*TXclosepredvalid2 ARGS((PRED *));
int	TXsetpredalts    ARGS((PRED *, DBTBL *, int, int, int));
int	TXsetprednames   ARGS((PRED *, DBTBL *, int, int, int));
void    TXsettablepred ARGS((QNODE *qnode, DBTBL *, PRED *, PROJ *, FLDOP *, int, SLIST *, SLIST *));
char	*TXdisppred ARGS((PRED *, int ext, int nomalloc, int maxsize));

/******************************************************************/

#define VSL_MAXSZ	4

#undef TXgetoff
#undef TXsetrecid
#undef TXrecidcmp
EPI_OFF_T	TXgetoff ARGS((RECID *));
EPI_OFF_T	TXsetrecid ARGS((RECID *, EPI_OFF_T));

/* Macro versions for speed:   KNG 980218 */
#if EPI_OS_INT_BITS == EPI_OFF_T_BITS
#  define TXrecidcmp(a, b)      ((a)->off - (b)->off)
#else
#  define TXrecidcmp(a, b)      \
   ((a)->off > (b)->off ? 1 : ((a)->off < (b)->off ? -1 : 0))
#endif
#define TXgetoff(r)             ((r) == (RECID *)NULL ? (EPI_OFF_T)(-1) : (r)->off)
#define TXgetoff2(r)            ((r)->off)
#define TXsetrecid(r, o)        ((r)->off = (EPI_OFF_T)(o))

char *TXtexisver ARGS((void));          /* KNG 990319 */

/******************************************************************/

/* Inline sort optimization for big-endian machines.
 * WTREE_HUGEUINT_CMP saves about 10% CPU
 * WTIX_HUGEUINT_CMP saves another 6%, at the cost of 20% of indexmem
 */
#ifndef SIZES_H
error;  /* depends on EPI_BIG_ENDIAN being consistent in all relevant files */
#endif
#if defined(EPI_BIG_ENDIAN) && !defined(__sgi) /* SGI alignment/malloc? issue */
#  define WTREE_HUGEUINT_CMP
#  define WTIX_HUGEUINT_CMP
#else
#  undef WTREE_HUGEUINT_CMP
#  undef WTIX_HUGEUINT_CMP
#endif
#ifdef EPI_LITTLE_ENDIAN
#  define WTREE_HUGEUINT_REVCMP
/* WTIX_HUGEUINT_REVCMP would be too complicated, not worth attempting */
#else
#  undef WTREE_HUGEUINT_REVCMP
#endif

/******************************************************************/

int	settxtimeout ARGS((int));
char   *TXpermModeToStr ARGS((char *buf, size_t sz, int permMode));
int	TXgetindexes ARGS((DBTBL *t, int mode, char **fields, int mmViaFdbi));
int	TXaddtoindices ARGS((DBTBL *));
void    tx_invdata2loc ARGS((BTLOC *ploc, void *v, int type, int desc));
int	TXsetstddic ARGS((DDIC *));
int	TXunsetstddic ARGS((DDIC *));
int	TXusestddic(DDIC *d);

int     TXchangeLocInIndices ARGS((DBTBL *dbtbl, BTLOC newLoc));

/* KNG 990120 */
#undef TXdecodestr
#define TXdecodestr     TXlcopy         /* security through obscurity */
char    *TXdecodestr ARGS((char *dest, size_t destSz, CONST char *src));

char    **freenlst ARGS((char **lst));

int     TXaddtablerec ARGS((DDIC *ddic, CONST char *lname, char *creator,
                            CONST char *remark, char *tbfname, int numFields,
                            int type, RECID *recidp));
int     TXdeltablerec ARGS((DDIC *ddic, RECID recid));
int	TXaddfields ARGS((DDIC *, char *, DD *));
int	TXaddtable ARGS((char *db, char *file, char *tbname, char *comment,
                         char *user, char *pass, int nbits));
char    *TXfd2file ARGS((int fd, int flags));
/* wtf also see prototype in ncgsvr.c: */
char   *TXbasename ARGS((CONST char *path));
size_t  TXdirname ARGS((char *dest, size_t destSz, CONST char *src));
char   *TXfileext ARGS((CONST char *path));
char   *TXjoinpath ARGS((TXPMBUF *pmbuf, int flags, char **srcs,
                         size_t numSrcs));
int     TXpathcmpGetDiff(const char **aPath, size_t alen,
			 const char **bPath, size_t blen);
int     TXpathcmp(const char *a, size_t alen, const char *b, size_t blen);
char   *TXcanonpath ARGS((CONST char *path, int yap));
char   *TXstrrspn ARGS((CONST char *a, CONST char *b));
char   *TXstrrcspn ARGS((CONST char *a, CONST char *b));
char    *TXfullpath ARGS((char *buffer, char *fn, int buflen, int flags));

/******************************************************************/

MMQL	*TXclosemmql ARGS((MMQL *, int));

/******************************************************************/

char	**TXgetglobalexp ARGS((void));

int     TXaddindextmp ARGS((char *value));      /* KNG 980515 */
int     TXdelindextmp ARGS((char *value));      /* KNG 980515 */
int     TXlistindextmp ARGS((char *value));     /* KNG 980515 */
char    **TXgetglobalindextmp ARGS((void));     /* KNG 980515 */
int     TXresetindextmp ARGS((void));           /* KNG 980515 */
void    TXsaveindexexp ARGS((char **expr, int *frexpr));      /* KNG 990722 */
void    TXrestoreindexexp ARGS((char **expr, int *frexpr));   /* KNG 990722 */

char	**_duplst ARGS((char **));
char	**_freelst ARGS((char **));

RECID	TXmygettblrow ARGS((DBTBL *, RECID *));
int	TXdeltmprow ARGS((DBTBL *));
int	TXkeepgoing ARGS((DDIC *));
int	TXisinfinite ARGS((TTL *));

/* See `PROXBTREE flags' comment in setf3dbi() for more on these flags: */
typedef enum PBF_tag
{
	PBF_SETWEIGHTS = (1 << 0),	/* rppm_setwts() called */
	PBF_RECIDORDER = (1 << 1),	/* results in recid not rank order */
	PBF_INVTREE    = (1 << 2)	/* flip rank and recid in tree */
}
PBF;

typedef struct tag_PROXBTREE {
        RPPM    *r;
        BTREE   *s;             /* Token -> recid */
        BTREE   *i;             /* Output (rank -> recid or vice versa) */
        BTREE   *d;             /* Deleted list */
        BTREE   *newlist;       /* New list */
        void    *fh;            /* FHEAP * */
	DBTBL	*t;
	FLD	*f;
	PBF	flags;
	int	maxperc;
	long	threshold;
	EPI_HUGEUINT	cnt;
	EPI_HUGEUINT	cntg;
	EPI_HUGEUINT	minhits;
	EPI_HUGEUINT	maxhits;
	TBSPEC	*tbspec;	/* Table spec, where clause and order by */
	void	*xxx;		/* Placeholder for extra info struct */
	FLDOP	*fldOp;		/* for converting fields to varchar */
	FLD	*cnvFld;	/* "" */
} PROXBTREE;
#define PROXBTREEPN     ((PROXBTREE *)NULL)
PROXBTREE *TXmkprox ARGS((MMAPI *mm, FLD *fld, int fop));

/******************************************************************/

long	TXsetmmatbl ARGS((MMTBL *, DDMMAPI *, unsigned long,
      int(*)(void *, long, long, short *), void *, int *, short *, int));
long	TXsetfmmatbl ARGS((void *fdbi, DDMMAPI *ddmmapi,
      int (*callback) ARGS((void *, BTLOC, int)), void *usr, int *nopost,
      int op));

/******************************************************************/

DBTBL  *tup_product_setup ARGS((DBTBL *, DBTBL *));
DBTBL  *TXtup_product_setup ARGS((DBTBL *, DBTBL *, int, SLIST *));

TXbool TXpredicateIsResolvableWithAltTable(PRED *pred, DBTBL *orgDbtbl,
					   DBTBL *altDbtbl, TXbool checkPred);

DBTBL  *TXtup_project_setup ARGS((DBTBL *tin, PROJ *proj, FLDOP *fo,
                                  int flags));

int	TXsetresult ARGS((FLD *, char *));
char   *TXmatchesi ARGS((char *buf, ft_internal *fti));
ft_int	TXmatchesc ARGS((FLD *, FLD *));
char   *TXmatchorig(void *v);
int     TXsetmatchmode(int m);
int	TXgetmatchmode(void);
char   *TXmatchgetr(FLD *f, size_t *sz);

int	TXfldnamecmp ARGS((DBTBL *, char *, char *));
int	TXpredcmp ARGS((PRED *, PRED *));
int	TXprojcmp ARGS((PROJ *, PROJ *));
int	TXpred_haslikep ARGS((PRED *));
int	TXpred_countnames ARGS((PRED *));
int	TXprojIsRankDescOnly(PROJ *proj);
int	TXpredHasRank(PRED *p);
int	TXprojHasRank(PROJ *proj);
int	TXclearpredvalid ARGS((PRED *));
int	TXpredrtdist ARGS((PRED *));

/* KNG 971111 */
int     groupbysetup ARGS((QNODE *query, FLDOP *fo));
int     groupby ARGS((QNODE *query, FLDOP *fo));
int     TXdistinctsetup ARGS((QNODE *query, FLDOP *fo));
int     TXdistinct ARGS((QNODE *query, FLDOP *fo));
int     dolikep ARGS((QNODE *query, FLDOP *fo));
DBTBL	*TXpreparetree ARGS((DDIC *, QNODE *, FLDOP *, int *, DBTBL *));
DBTBL	*TXrepreparetree ARGS((DDIC *, QNODE *, FLDOP *, int *, DBTBL *));
int	TXunpreparetree ARGS((QNODE *query));
int	TXdotree ARGS((QNODE *query, FLDOP *fo, int direction, int offset));
int	TXproductsetup ARGS((QNODE *, QUERY *, FLDOP *));
int	TXproduct ARGS((QNODE *, QUERY *, FLDOP *));

void    TXfreesqlbuf ARGS((void));
int     TXfreeSqlParser ARGS((void));

MMQL    *mmrip ARGS((MMAPI *mm, int isfdbi));           /* KNG 980630 */
/* flags for MMQI.wild field: */
#define MMRIP_WILDMIDTRAIL      0x1     /* eg. ab*cd or ab* */
#define MMRIP_WILDLEAD          0x2     /* eg. *ab */

/******************************************************************/

int     TXcopystats ARGS((NFLDSTAT *, NFLDSTAT *));
int     TXresetnewstats ARGS((NFLDSTAT *table));
int	TXopennewstats ARGS((DBTBL *, PROJ *, FLDOP *, NFLDSTAT **));
NFLDSTAT *TXdupnewstats ARGS((NFLDSTAT *));
int	TXclosenewstats ARGS((NFLDSTAT **));
int	TXaddstatrow ARGS((NFLDSTAT *, DBTBL *, FLDOP *));
int	TXsetcountstat ARGS((NFLDSTAT *, EPI_HUGEUINT));
int	TXisprojcountonly ARGS((DBTBL *, PROJ *, FLDOP *));
FLD    *TXgetstatfld ARGS((DBTBL *, PRED *));
void	TXrewinddbtbl ARGS((DBTBL *));
void	TXrewinddbtblifnoindex ARGS((DBTBL *));
int	TXtblstillthere ARGS((DBTBL *));

/******************************************************************/

QNODE  *TXreorgqnode ARGS((QNODE *));
PARAM	*TXneeddata ARGS((QNODE *, int));
int	TXparamunset ARGS((QNODE *, int));
FLD *   TXqtreetofld ARGS((QNODE *, DBTBL *, int *, FLDOP *));
int	TXquitqnode ARGS((QNODE *query));

/******************************************************************/

DD	*TXexpanddd ARGS((DD *, int));


/* - - - - - - - - - - - - TXftiValueWithCooked: - - - - - - - - - - - - - */

typedef enum TXdup_tag
{
	TXdup_DupIt,				/* callee dups and frees */
	TXdup_TakeAndFree,			/* callee owns and frees */
	TXdup_IsPermanent,			/* callee does not free */
}
TXdup;

typedef void    TXftiValueWithCooked_CloseCookedFunc(TXPMBUF *pmbuf,
                                                     void *cooked);
typedef struct TXftiValueWithCooked_tag	TXftiValueWithCooked;

TXbool TXftiValueWithCooked_GetCookedAndCloseFunc(TXftiValueWithCooked
                                                  *valueWithCooked,
           void **cooked, TXftiValueWithCooked_CloseCookedFunc **closeCooked);
TXbool TXftiValueWithCooked_SetCookedAndCloseFunc(TXPMBUF *pmbuf,
	   TXftiValueWithCooked *valueWithCooked,
           void *cooked, TXftiValueWithCooked_CloseCookedFunc *closeCooked);
TXbool TXftiValueWithCooked_SetValue(TXPMBUF *pmbuf,
				     TXftiValueWithCooked *valueWithCooked,
		       void *value, FTN type, size_t n, size_t sz, TXdup dup);
void *TXftiValueWithCooked_GetValue(TXftiValueWithCooked *valueWithCooked,
				    FTN *type, size_t *n, size_t *sz);

/******************************************************************/

/* Also typedef'd in dbstruct.h: */
#ifndef TXA2INDPN
typedef struct TXA2IND_tag	TXA2IND;
#  define TXA2INDPN	((TXA2IND *)NULL)
#endif /* !TXA2INDPN */

struct TXA2IND_tag {
	FLD **fin;
	FLD **fout;
	int nfields;
	TBL *tbl;
	BINDEX *index;
	DBTBL *tup;
};

int     procupd ARGS((QUERY *q, FLDOP *fo));
int     TXdelfromindices ARGS((DBTBL *));

char **TXgetupdfields ARGS((DBTBL *t, UPDATE *u));

/******************************************************************/

TXA2IND *TXadd2indsetup ARGS((DBTBL *, BINDEX *));
int	TXadd2ind ARGS((TXA2IND *, BTLOC *));
int     TXadd2indSplitStrlst ARGS((TXA2IND *, BTLOC *));
int     TXdel2indSplitStrlst(TXA2IND *as, BTLOC *pos);
int     TXdel2ind ARGS((TXA2IND *as, BTLOC *pos));

char    *TXa2i_tostring ARGS((TXA2IND *as));
size_t  TXa2i_setbuf ARGS((TXA2IND *as));
int     TXa2i_btreeinsert ARGS((TXA2IND *as, BTLOC *pos));
int     TXa2i_btreedelete ARGS((TXA2IND *as, BTLOC *pos));
int     TXaddtoindChangeLoc ARGS((TXA2IND *a2i, BTLOC newLoc, int isMm));
int     TXaddtoindChangeLocInverted ARGS((BINVDX *binv, BTLOC oldLoc,
                                          BTLOC newLoc));
BTLOC	TXsearch2ind ARGS((TXA2IND *));
TXA2IND *TXadd2indcleanup ARGS((TXA2IND *));

/******************************************************************/

int TXverifylocks(TXPMBUF *pmbuf, DBLOCK *dblock, int locked, int fix,
		  TXbool verbose);
int TXfindltable ARGS((TXPMBUF *pmbuf, DBLOCK *sem, char *table));

/******************************************************************/

int TXsetfldcmp ARGS((BTREE *));

/******************************************************************/

#ifdef DEBUG
extern int	TXDebugLevel;

#define DBGMSG(a,b)	if(a < TXDebugLevel) putmsg b
#else
#define DBGMSG(a,b)
#endif

/******************************************************************/

#define OP_IOCTL_MASK   0x00007FFF      /* bits reserved for individual op */
#define TYPE_IOCTL_MASK 0xFFFF8000      /* type bits */

#define BTREE_IOCTL     0x00008000      /* B-tree ioctl type  KNG 971016 */
#define DBF_RAM		0x00010000
#define DBF_FILE	0x00020000
#define DBF_KAI		0x00040000
#define DBF_DBASE	0x00080000
#define DBF_MEMO	0x00100000
#ifdef HAVE_JDBF
#define DBF_JMT		0x00200000
#endif
#define DBF_NOOP        0x00400000

#define DBF_MAKE_FILE	0x00000001
#define DBF_AUTO_SWITCH	0x00000002
#define DBF_SIZE	0x00000005

#define RDBF_SETOVER	DBF_RAM | 0x00000001
#define RDBF_TOOBIG	DBF_RAM | 0x00000002
#define RDBF_BLCK_LIMIT	DBF_RAM | 0x00000003
#define RDBF_SIZE_LIMIT	DBF_RAM | 0x00000004
#define RDBF_SIZE	DBF_RAM | DBF_SIZE
/* Separate ioctl RDBF_SET_NAME to set RAM DBF "file" name, because the
 * normal way to set a DBF name -- at opendbf() -- we can only pass NULL,
 * since that is the way to indicate RAM DBF:
 */
#define RDBF_SET_NAME	(DBF_RAM | 0x00000006)

#define	isramdbtbl(a)	((a) && (a)->tbl && (a)->tbl->df && ((a)->tbl->df->dbftype & DBF_RAM) == DBF_RAM)
#define	isramtbl(a)	((a) && (a)->df && ((a)->df->dbftype & DBF_RAM) == DBF_RAM)

/* ------------------------------ noopdbf.c: ------------------------------ */

typedef struct TXNOOPDBF_tag    TXNOOPDBF;
#define TXNOOPDBFPN     ((TXNOOPDBF *)NULL)

/* "Filename" to pass to opendbf() to indicate TXNOOPDBF: */
#define TXNOOPDBF_PATH  ((char *)1)

#define TXNOOPDBF_IOCTL_SEEKSTART       0x1

TXNOOPDBF *TXnoOpDbfClose(TXNOOPDBF *df);
TXNOOPDBF *TXnoOpDbfOpen(void);
int     TXnoOpDbfFree(TXNOOPDBF *df, EPI_OFF_T at);
EPI_OFF_T TXnoOpDbfAlloc(TXNOOPDBF *df, void *buf, size_t n);
EPI_OFF_T TXnoOpDbfPut(TXNOOPDBF *df, EPI_OFF_T at, void *buf, size_t sz);
int     TXnoOpDbfBlockIsValid(TXNOOPDBF *df, EPI_OFF_T at);
void    *TXnoOpDbfGet(TXNOOPDBF *df, EPI_OFF_T at, size_t *psz);
void    *TXnoOpDbfAllocGet(TXNOOPDBF *df, EPI_OFF_T at, size_t *psz);
size_t  TXnoOpDbfRead(TXNOOPDBF *df, EPI_OFF_T at, size_t *off, void *buf,
                      size_t sz);
EPI_OFF_T TXnoOpDbfTell(TXNOOPDBF *df);
char    *TXnoOpDbfGetFilename(TXNOOPDBF *df);
int     TXnoOpDbfGetFileDescriptor(TXNOOPDBF *df);
void    TXnoOpDbfSetOverAlloc(TXNOOPDBF *noOpDbf, int ov);
int     TXnoOpDbfIoctl(TXNOOPDBF *noOpDbf, int ioctl, void *data);
int     TXnoOpDbfSetPmbuf(TXNOOPDBF *noOpDbf, TXPMBUF *pmbuf);

int     TXinitNoOpDbf(DBF *df);

/* ------------------------------------------------------------------------ */

int	makevalidtable(DDIC *ddic, SYSTBL tblid);
int     TXddicGetSystablesChanged ARGS((DDIC *ddic));
int     TXddicSetSystablesChanged ARGS((DDIC *ddic, int yes));
int     TXddicGetSysindexChanged ARGS((DDIC *ddic));
int     TXddicSetSysindexChanged ARGS((DDIC *ddic, int yes));

void	pred_rmalts ARGS((PRED *));
void	pred_rmfieldcache ARGS((PRED *, DBTBL *));
void    pred_sethandled ARGS((PRED *p));
int     pred_allhandled ARGS((PRED *p));

typedef enum TXpwEncryptMethod_tag
{
	TXpwEncryptMethod_Unknown = -1,		/* must be first and -1 */
	TXpwEncryptMethod_DES,
	TXpwEncryptMethod_MD5,			/* `$1$...' */
	TXpwEncryptMethod_SHA256,		/* `$5$...' */
	TXpwEncryptMethod_SHA512,		/* `$6$...' */
	TXpwEncryptMethod_NUM			/* must be last */
}
TXpwEncryptMethod;

/* These are all standard for SHA mode in crypt(); do not change: */
#define TX_PWENCRYPT_ROUNDS_MIN		1000
#define TX_PWENCRYPT_ROUNDS_MAX		999999999
#define TX_PWENCRYPT_ROUNDS_MAX_DIGITS	9	/* # digits in ROUNDS_MAX */
#define TX_PWENCRYPT_ROUNDS_DEFAULT	5000

#define TX_PWENCRYPT_SALT_STR_DES	"$0$"

char   *TXpwEncrypt(const char *clearPass, const char *salt);
TXpwEncryptMethod TXpwEncryptMethodStrToEnum(const char *s);
const char *TXpwEncryptMethodEnumToStr(TXpwEncryptMethod method);

int	TXsetdfltpass ARGS((char *, char *, char *, char *));
int	TXcreateDb(TXPMBUF *pmbuf, const char *path,
                   const char *encSystemPass, const char *encPublicPass,
                   TXDDOPENFLAG flags);

#ifndef OLD_FLDMATH
int	TXmakesimfield ARGS((FLD *, FLD *));
int	TXfreevirtualdata ARGS((FLD *));
#endif
int	TXlikein ARGS((FLD *, FLD *));
int	TXftoind ARGS((FLD *));
char	*TXgetindirectfname ARGS((DDIC *));
int	TXisindirect ARGS((char *));

TXbool	TXispredvalid(TXPMBUF *pmbuf, PRED *p, DBTBL *t, int flags,
		      DBTBL *orgDbtbl, int *colsUsed);

void    TXsettablesizelimit ARGS((EPI_OFF_T));

typedef struct TXdiskSpace_tag
{
  /* These are -1 if unknown.  Note that sometimes these can legitimately
   * be less than zero, e.g. availableBytes when filesystem filled by root:
   */
  EPI_HUGEINT   availableBytes;                 /* available to non-root */
  EPI_HUGEINT   freeBytes;                      /* available to root */
  EPI_HUGEINT   totalBytes;
  double        usedPercent;
}
TXdiskSpace;
#define TX_DISKSPACE_INIT(ds)                                           \
  { (ds)->availableBytes = (ds)->freeBytes = (ds)->totalBytes = -1;     \
    (ds)->usedPercent = -1.0; }

int     TXgetDiskSpace(const char *path, TXdiskSpace *diskSpace);
TXbool	TXmkdir(TXPMBUF *pmbuf, const char *path, unsigned mode);
char    *TXproff_t ARGS((EPI_OFF_T at));
char    *TXprkilo ARGS((char *buf, size_t bufsz, EPI_HUGEUINT sz));
int     TXparseCEscape ARGS((TXPMBUF *pmbuf, CONST char **buf,
                             CONST char *bufEnd, int *charVal));
char    *TXcesc2str ARGS((CONST char *s, size_t slen, size_t *dlen));
size_t  TXstrToCLiteral(char *d, size_t dlen, const char **sp, size_t slen);
void    tx_hexdumpmsg ARGS((TXPMBUF *pmbuf, int errnum, CONST char *fn,
                            CONST byte *buf, size_t sz, int withoff));
size_t  TXpagesize ARGS((void));
size_t  TXphysmem ARGS((void));
int     TXloadavg ARGS((float *avgs));
/* wtf also see prototype in ncgsvr.c: */
long    TXsleepmsec ARGS((long msec, int ignsig));
double  TXfiletime2time_t ARGS((EPI_UINT32 lo, EPI_UINT32 hi));
int     TXtime_t2filetime ARGS((double tim, EPI_UINT32 *lo, EPI_UINT32 *hi));
double  TXgettimeofday ARGS((void));
TXbool  TXgetTimeContinuousFixedRate(double *theTime);
double  TXgetTimeContinuousFixedRateOrOfDay(void);  /* wtf see ezsock.h too */
TXbool  TXgetTimeContinuousVariableRate(double *theTime);
int     TXsetFileTime ARGS((CONST char *path, int fd,
                            CONST double *creationTime,
                            CONST double *accessTime,
                            CONST double *modificationTime, int allNow));
size_t  TXcomputeIndexmemValue ARGS((size_t indexmemUser));
size_t  TXgetindexmmapbufsz ARGS((void));

extern const char       TXunsupportedPlatform[];

#ifdef _WIN32
CONST char *TXctrlEventName ARGS((int type));
#endif /* _WIN32 */
int     TXstartEventThreadAlarmHandler(void);
char    *TXsignalname ARGS((int sigval));
int     TXsignalval ARGS((char *signame));
const char *TXsiginfoCodeName(int signal, int code);
size_t  TXprintRegisters(char *buf, size_t bufSz, void *si, void *ctx);
TXbool  TXsetSigProcessName(TXPMBUF *pmbuf, const char *name);
void    TXsysdepCleanupAtExit ARGS((void));
/* wtf also see prototype in ncgsvr.c: */
void    tx_setgenericsigs (void);
/* wtf also see prototype in ncgsvr.c: */
void    tx_unsetgenericsigs ARGS((int level));
#ifndef SIGARGS
#  define SIGARGS       ARGS((int sig))
#endif
void    TXmkabend(void);
SIGTYPE CDECL tx_genericsighandler TX_SIG_HANDLER_ARGS;
TX_SIG_HANDLER *TXcatchSignal ARGS((int sigNum, TX_SIG_HANDLER *handler));
TXbool	TXcatchMemoryErrors(void);

int    	txmaxrlim(TXPMBUF *pmbuf);
void 	TXmaximizeCoreSize(void);

int     TXcountterms ARGS((char *s));
int     TXcatpath ARGS((char *dest, CONST char *src, CONST char *ext));
/* WTF see also prototype in epi/eqvwr.c: */
char   *TXtempnam ARGS((CONST char *dir, CONST char *prefix, CONST char *ext));
size_t  TXfuser ARGS((PID_T *pids, size_t maxpids, CONST char *path));
int     TXrlimname2res ARGS((CONST char *name));
CONST char *TXrlimres2name ARGS((int res));
int     TXgetrlimit(TXPMBUF *pmbuf, int res, EPI_HUGEINT *soft,
		    EPI_HUGEINT *hard);
int     TXsetrlimit(TXPMBUF *pmbuf, int res, EPI_HUGEINT soft,
		    EPI_HUGEINT hard);
PID_T	TXgetpid ARGS((int));
extern PID_T	TXpid;

int	TXismmop (QNODE_OP, FOP *);
char	**TXfstrlsttoc ARGS((FLD *fld, int emptyStripAndTerminate));
int     TXll2code ARGS((long latv, long lonv, long *code));
int     TXcode2ll ARGS((long code, long *lat, long *lon));
int     TXcodes2box ARGS((long, long, long *, long *, long *, long *));


typedef enum TXPMF_tag
{
  TXPMF_DONE    = (1 << 0),     /* (internal) process has exited */
  TXPMF_SAVE    = (1 << 1)      /* TXaddproc() caller will report */
}
TXPMF;
#define TXPMFPN ((TXPMF *)NULL)

/* Optional callback when process exits: */
typedef void TXprocExitCallback ARGS((void *userData, PID_T pid,
                                      int signalNum, int exitCode));
#define TXprocExitCallbackPN        ((TXprocExitCallback *)NULL)

int     TXinitChildProcessManagement(void);
int     TXsetInProcessWait(int on);
int     TXaddproc ARGS((PID_T pid, char *desc, char *cmd, TXPMF flags,
                        CONST char * CONST *xitdesclist,
                        TXprocExitCallback *callback, void *userData));
int     TXsetprocxit ARGS((PID_T pid, int owner, int sig, int xit,
                           char **desc, char **cmd, CONST char **xitdesc));
int     TXgetprocxit ARGS((PID_T pid, int owner, int *sig, int *xit,
                           char **desc, char **cmd, CONST char **xitdesc));
void    TXprocDelete ARGS((PID_T pid, TXprocExitCallback *callback,
                           void *userData));
void    TXcleanproc ARGS((void));
void    TXfreeAllProcs ARGS((void));

int     TXisTexisProg ARGS((CONST char *prog));

int	TXstringcompare ARGS((char *, char *, size_t, size_t));
char    *TXsetlocale ARGS((char *s));
CONST char *TXgetlocale ARGS((void));
CONST char *TXgetDecimalSep ARGS((void));
char    *TXgetlibpath ARGS((void));
int     TXsetlibpath ARGS((TXPMBUF *pmbuf, CONST char *path));
extern  int TxLibPathSerial;
#define TXgetlibpathserial()    TxLibPathSerial
char    *TXgetentropypipe ARGS((void));
int     TXsetentropypipe ARGS((CONST char *pipe));

#ifdef _WIN32
int     TXgetfileinfo ARGS((char *filename, LPDWORD ownsize, char *owner,
                            LPDWORD grpsize, char *group));
char    **queryregistry ARGS((TXPMBUF *pmbuf, char *key, char *subkey,
                  char *value, char *defaultvalue, size_t expectedsize));
#  ifdef _INC_EXCPT             /* <excpt.h> included alreadu */
struct exception_pointers
{
	EXCEPTION_RECORD	*er;
	CONTEXT			*cxt;
};
#  endif /* _INC_EXCPT */
int	TXSetCleanupWait(int);

int tx_initsec(TXPMBUF *pmbuf, SECURITY_DESCRIPTOR *sd,
	       SECURITY_ATTRIBUTES *sa);
char *TXGlobalName(TXPMBUF *pmbuf, const char *Name);

#endif /* _WIN32 */

int     tx_parsesz(TXPMBUF *pmbuf, const char *s, EPI_HUGEINT *szp,
		   const char *setting, int bitsz, TXbool byteSuffixOk);
int	TXprintSz(char *buf, size_t bufSz, EPI_HUGEINT num);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* note: under Windows TXLIB * <=> HINSTANCE: */
typedef void    TXLIB;
#define TXLIBPN ((TXLIB *)NULL)

size_t  TXlib_expandpath ARGS((CONST char *path, char ***listp));
char    **TXlib_freepath ARGS((char **list, size_t n));
TXLIB   *TXopenlib ARGS((CONST char *file, CONST char *path, int flags,
                         TXPMBUF *pmbuf));
TXLIB   *TXcloselib ARGS((TXLIB *lib));
int TXopenLibs(const char *libs, const char *path, int flags, TXPMBUF *pmbuf);
void    *TXlib_getaddr(TXLIB *lib, TXPMBUF *pmbuf, const char *name);
size_t   TXlib_getaddrs(TXLIB *lib, TXPMBUF *pmbuf, const char * const *names,
			void **addrs, size_t num);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef TX_THREADS_H
#  include "txthreads.h"
#endif /* !TX_THREADS_H */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TXEVENTPN       ((TXEVENT *)NULL)

TXEVENT *opentxevent(int manualReset);
TXEVENT *closetxevent ARGS((TXEVENT *event));
int TXunlockwaitforevent ARGS((TXEVENT *event, TXMUTEX *mutex));
int TXsignalevent ARGS((TXEVENT *event));

/* - - - - - - - - - - - - - txFhandleEvent.c: - - - - - - - - - - - - - - */

/* An event object with a TXFHANDLE as its active part, to enable its
 * use in select() under Unix (e.g. with other TXFHANDLEs) where a
 * TXEVENT cannot be used.  Note that this is a manual-reset event,
 * due to limitations in the Unix implementation.
 */

#ifdef _WIN32
typedef TXEVENT				TXFHANDLE_EVENT;
#else /* !_WIN32 */
typedef struct TXFHANDLE_EVENT_tag	TXFHANDLE_EVENT;
#endif /* !_WIN32 */

TXFHANDLE_EVENT *TXfhandleEventOpen(void);
TXFHANDLE_EVENT *TXfhandleEventClose(TXFHANDLE_EVENT *fEvent);
int	         TXfhandleEventSignal(TXFHANDLE_EVENT *fEvent);
int		 TXfhandleEventClear(TXFHANDLE_EVENT *fEvent);
TXFHANDLE	 TXfhandleEventGetWaitableFhandle(TXFHANDLE_EVENT *fEvent);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

extern TXATOMINT       TxSignalDepthVar;
/* TXentersignal() returns nonzero if we were in signal before this call: */
#define TXentersignal() (TX_ATOMIC_INC(&TxSignalDepthVar) > 0)
#define TXexitsignal()  (void)TX_ATOMIC_DEC(&TxSignalDepthVar)
#define TXinsignal()    (TxSignalDepthVar > 0)

int	TXinitAbendSystem(TXPMBUF *pmbuf);

/* ABEND cleanup callbacks: */
typedef void (TXABENDCB) ARGS((void *usr));     /* ABEND callback function */
int TXaddabendcb ARGS((TXABENDCB *func, void *usr));
int TXdelabendcb ARGS((TXABENDCB *func, void *usr));
void TXcallabendcbs ARGS((void));

/* ABEND location callbacks (eg. URL:line): */
typedef size_t (TXABENDLOCCB) ARGS((char *buf, size_t sz, void *usr));
int TXaddabendloccb ARGS((TXABENDLOCCB *func, void *usr));
int TXdelabendloccb ARGS((TXABENDLOCCB *func, void *usr));
size_t TXprabendloc ARGS((char *buf, size_t sz));

void TXfreeabendcache ARGS((void));

/* on-normal-exit callbacks: */
int TXaddOnExitCallback(TXPMBUF *pmbuf, TXABENDCB *func, void *usr);
int TXremoveOnExitCallback(TXPMBUF *pmbuf, TXABENDCB *func, void *usr);
void TXcallOnExitCallbacks(void);

void TXexit(TXEXIT status);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_PWD_H) || defined(_PWD_INCLUDED) || defined(_H_PWD) || defined(_PWD_H_) || defined(__PWD_H__)
#  define TX_INCLUDED_PWD_H
#endif

#ifndef _WIN32
#  ifdef TX_INCLUDED_PWD_H
/* Give prototypes iff we know `struct passwd' has been defined.
 * Give TX_GETPW_R_BUFSZ iff prototypes given: force compile err otherwise.
 */
#    define TX_GETPW_R_BUFSZ    1024
struct passwd *TXgetpwuid_r ARGS((UID_T uid, struct passwd *pwbuf,
                                  char *buf, size_t bufsz));
struct passwd *TXgetpwnam_r ARGS((CONST char *name, struct passwd *pwbuf,
                                  char *buf, size_t bufsz));
#  endif /* TX_INCLUDED_PWD_H */
#endif /* !_WIN32 */

char *TXgetRealUserName ARGS((TXPMBUF *pmbuf));
char *TXgetEffectiveUserName ARGS((TXPMBUF *pmbuf));
int   TXisUserAdmin(void);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_INSTBINVARS_NUM      3
extern CONST char * CONST      TxInstBinVars[];
extern CONST char * CONST      TxInstBinVals[];
#define TXREPLACEVAL_BINDIR  ((CONST char *)1)
#define TXREPLACEVAL_EXEDIR  ((CONST char *)2)

int  TXsplitdomainuser ARGS((TXPMBUF *pmbuf, CONST char *domain,
			     CONST char *user, char **adomain, char **auser));
char *tx_replacevars ARGS((TXPMBUF *pmbuf, CONST char *val, int yap,
                           CONST char * CONST *vars, size_t numVars,
                           CONST char * CONST *vals,
                           CONST int *valsAreExpanded));
char *tx_c2dosargv ARGS((char **argv, int quote));
char **tx_dos2cargv ARGS((CONST char *cmdline, int unquote));

typedef void (TXPOPENFUNC) ARGS((void *usr, PID_T pid, TXHANDLE proc));
#define TXPOPENFUNCPN   ((TXPOPENFUNC *)NULL)

/* RWO:  TXpreadwrite()-only flag */
typedef enum TXPDF_tag
{
  TXPDF_BKGND           = (1 << 0),     /* background process (implies
                                         * TXPDF_NEWPROCESSGROUP) */
  TXPDF_FASTLOGON       = (1 << 1),     /* (Windows) CreateProcessWithLogonW*/
  TXPDF_QUOTEARGS       = (1 << 2),     /* (Windows) quote args w/spaces */
  TXPDF_REAP            = (1 << 3),     /* (Unix wtf) add to reapable procs */
  TXPDF_SAVE            = (1 << 4),     /* (Unix wtf) save */
  TXPDF_QUIET           = (1 << 5),     /* do not report error messages */
  TXPDF_ANYDATARD       = (1 << 6),     /* RWO write all, return @ 1st rd */
  TXPDF_NEWPROCESSGROUP = (1 << 7)      /* create new process group */
}
TXPDF;
#define TXPDFPN ((TXPDF *)NULL)

/* Args for TXpopenduplex().  All fields are input/read-only: */
typedef struct TXPOPENARGS_tag
{
  char          *cmd;           /* path of program to run */
  char          **argv;         /* args */
  char          **envp;         /* (opt.) environment */
  TXPDF         flags;          /* flags */
  char          *desc;          /* (opt.) description for TXPDF_REAP */
  char          *cwd;           /* (opt.) working dir. for process */
  char          *domain;        /* (opt. Windows) domain for process */
  char          *user;          /* (opt. Windows) [domain\]user for process */
  char          *pass;          /* (opt. Windows) password for user */
  TXFHANDLE     fh[3];          /* parent end of child stdin/out */
  void          *usr;           /* (opt.) user data for callbacks */
  TXPOPENFUNC   *childpostfork; /* (opt. Unix) */
  TXPOPENFUNC   *childerrexit;  /* (opt. Unix) */
  TXPMBUF       *pmbuf;         /* (opt.) putmsg buffer */
  double        endiotimeout;   /* (opt. Windows) -1 for infinite */
  TXprocExitCallback    *exitCallback;  /* (opt.) callback on exit */
  void          *exitUserData;  /* (opt.) user data */
}
TXPOPENARGS;
#define TXPOPENARGSPN   ((TXPOPENARGS *)NULL)

/* use TXpgetdefendiotimeout() value; see sysdep.c for compiled default: */
#define TX_POPEN_ENDIOTIMEOUT_DEFAULT   ((double)(-2.0))
/* infinite (no timeout): */
#define TX_POPEN_ENDIOTIMEOUT_INFINITE  ((double)(-1.0))

#ifndef HTBUFPN
typedef struct HTBUF_tag        HTBUF;          /* defined in httpi.h */
#  define HTBUFPN ((HTBUF *)NULL)
#endif /* !HTBUFPN */

#ifndef PIPE_MAX
#  define PIPE_MAX      1024
#endif

#ifdef _WIN32
#  if _WIN32_WINNT >= 0x0502            /* Windows Server 2003 and up */
#    define SECURE_ZERO_MEM(p, sz)	SecureZeroMemory(p, sz)
#  else /* _WIN32_WINNT < 0x0502 */
#    define SECURE_ZERO_MEM(p, sz)	ZeroMemory(p, sz)
#  endif /* _WIN32_WINNT < 0x0502 */
#else /* !_WIN32 */
#  define SECURE_ZERO_MEM(p, sz)	memset(p, 0, sz)
#endif /* !_WIN32 */

/* object for each stdio pipe to a subprocess:
 *   PR  parent thread reads this var
 *   PW  parent thread writes this var
 *   IR  I/O thread reads this var
 *   IW  I/O thread writes this var
 */
typedef struct TXPIPEOBJ_tag
{
  TXFHANDLE     fh;             /* PRW IR  parent end of child pipe handle */
  HTBUF         *buf;           /* PRW IRW read/write buffer (may be NULL) */
  TXPMBUF       *pmbuf;         /* PW  IR  (opt.) putmsg buffer */
#ifdef _WIN32
  int           stdIdx;         /* PW  IR  stdio index */
  TXTHREAD      parentthread;   /* PRW IR  parent thread (shared handle!) */
  TXTHREAD      iothread;       /* PRW     thread to handle I/O on `fd' */
  byte          haveParentThread; /* PRW */
  byte          haveIoThread;     /* PRW */
  HANDLE        iobeginevent;   /* PRW IRW event to signal `iothread' */
  enum                          /* PRW IR  what to do on `iobeginevent': */
  {
    TPOTASK_IDLE,               /* idle */
    TPOTASK_EXIT,               /* exit thread */
    TPOTASK_READ,               /* read data */
    TPOTASK_WRITE               /* write data */
  }
  task;
  HANDLE        ioendevent;     /* PRW IRW event to signal parent thread */
  TXERRTYPE     resulterrnum;   /* PR  IW  error from `task' (0 == success) */
  size_t        resultlen;      /* PR  IW  bytes actually read */
  char          *iobuf;         /* PR  IW  buffer for `iothread' to read to */
#  define TPO_IOBUFSZ   8192
  size_t        iobufsztodo;    /* PW IR   size of `iobuf' to read/write */
#endif /* _WIN32 */
}
TXPIPEOBJ;

/* Args for htbuf_preadwrite() (this is initialized by TXpopenduplex()): */
typedef struct TXPIPEARGS_tag
{
  TXPIPEOBJ     pipe[TX_NUM_STDIO_HANDLES];	/* pipe objects for stdio */
  PID_T         pid;            /* process ID of subprocess */
  TXHANDLE      proc;           /* (Windows) subprocess handle */
  TXPDF         flags;          /* RWO flags (user-settable each call) */
  TXPMBUF       *pmbuf;         /* (optional) putmsg buffer */
  EPI_UINT32    endiomsec;      /* (opt. Windows) endiothread() timeout */
}
TXPIPEARGS;
#define TXPIPEARGSPN    ((TXPIPEARGS *)NULL)

#define TXPOPENARGS_INIT(ptr)                           \
  (memset((ptr), 0, sizeof(TXPOPENARGS)), (ptr)->fh[0] =\
   (ptr)->fh[1] = (ptr)->fh[2] = TXFHANDLE_INVALID_VALUE,\
   (ptr)->endiotimeout = TX_POPEN_ENDIOTIMEOUT_DEFAULT)
#define TXPIPEARGS_INIT(ptr)                            \
  (memset((ptr), 0, sizeof(TXPIPEARGS)), (ptr)->pipe[0].fh =    \
   (ptr)->pipe[1].fh = (ptr)->pipe[2].fh = TXFHANDLE_INVALID_VALUE)

TXbool TXpopenSetParentStdioNonInherit(TXPMBUF *pmbuf,
                   TXFHANDLE hParentStd[TX_NUM_STDIO_HANDLES],
                   EPI_UINT32 dwParentStdOrgHandleInfo[TX_NUM_STDIO_HANDLES]);

int TXpopenduplex ARGS((CONST TXPOPENARGS *po, TXPIPEARGS *pa));
int TXpreadwrite ARGS((TXPIPEARGS *pa, int timeout));
int TXpendio ARGS((TXPIPEARGS *pa, int all));
int TXpkill ARGS((TXPIPEARGS *pa, int yap));
int TXpgetexitcode ARGS((TXPIPEARGS *pa, int flags, int *code, int *issig));
int TXpcloseduplex ARGS((TXPIPEARGS *pa, int flags));
double TXpgetendiotimeoutdefault ARGS((void));
int TXpsetendiotimeoutdefault ARGS((double sec));
int TXreportProcessExit(TXPMBUF *pmbuf, const char *fn, const char *procDesc,
                        const char *cmd, PID_T pid, int exitCodeOrSig,
                        int isSig, const char *exitDesc);

#ifdef _WIN32
CONST char *TXwaitRetToStr ARGS((DWORD res, int *delta));
#endif /* _WIN32 */

/* Descriptions of integer codes (NULL-`description'-terminated array): */
typedef struct TXCODEDESC_tag
{
  int           code;
  const char    *description;                   /* brief phrase desciption */
}
TXCODEDESC;
#define TXCODEDESCPN	((TXCODEDESC *)NULL)

#define TXCODEDESC_ITEM(sym)	{ sym, #sym }

extern const TXCODEDESC	TXsystemStatuses[];
extern const TXCODEDESC	TXerrnoNames[];
extern const TXCODEDESC	TXh_errnoNames[];

const char *TXgetCodeDescription(const TXCODEDESC *list, int code,
				 const char *unkCodeDesc);

#ifdef _WIN32
extern const TXCODEDESC	TXwin32ErrNames[];
#  define TX_OS_ERR_NAMES	TXwin32ErrNames
#else /* !_WIN32 */
#  define TX_OS_ERR_NAMES	TXerrnoNames
#endif /* !_WIN32 */
/* TXgetOsErrName() must be thread-safe and async-signal-safe: */
#define TXgetOsErrName(err, unkName)	\
	TXgetCodeDescription(TX_OS_ERR_NAMES, (err), (unkName))
#define TXgetHerrnoName(err, unkName)	\
	TXgetCodeDescription(TXh_errnoNames, (err), (unkName))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Portable version of struct tm: */
typedef struct TXTIMEINFO_tag
{
  /* Note that we define some fields differently from struct tm
   * (e.g. count days and months from 1 not 0 consistently).  `year'
   * is time_t to handle large dates when sizeof(time_t) > sizeof(int):
   */
  time_t        year;             /* year, e.g. 2014 */
  int           month;            /* 1 == January */
  int           dayOfMonth;       /* 1 - 31 */
  int           hour;             /* 0 - 23 */
  int           minute;           /* 0 - 59 */
  int           second;           /* 0 - 60 (leap second?) */
  int           dayOfWeek;        /* 1 == Sunday */
  int           dayOfYear;        /* 1 == January 1 */
  int           isDst;            /* is DST?  1: yes  0: no  -1: unknown */
  int           gmtOffset;        /* seconds from GMT */
  int           isDstStdOverlap;  /* STD side of DST -> STD overlap; 1/0/-1 */
}
TXTIMEINFO;
#define TXTIMEINFOPN    ((TXTIMEINFO *)NULL)

#define TXTIMEINFO_INIT(ti)     { memset((ti), 0, sizeof(TXTIMEINFO));  \
    (ti)->isDst = (ti)->isDstStdOverlap = -1; }

void TXtxtimeinfoToStructTm(const TXTIMEINFO *timeinfo, struct tm *tm);
void TXstructTmToTxtimeinfo(const struct tm *tm, TXTIMEINFO *timeinfo);
size_t TXosStrftime ARGS((char *s, size_t max, CONST char *format,
                          CONST TXTIMEINFO *timeinfo));
size_t TXstrftime ARGS((char *s, size_t max, CONST char *format,
                        CONST TXTIMEINFO *tm));
int TXosTime_tToGmtTxtimeinfo ARGS((time_t tim, TXTIMEINFO *timeinfo));
int TXtime_tToGmtTxtimeinfo ARGS((time_t tim, TXTIMEINFO *timeinfo));
int TXosTime_tToLocalTxtimeinfo ARGS((time_t tim, TXTIMEINFO *timeinfo));
int TXtime_tToLocalTxtimeinfo ARGS((time_t tim, TXTIMEINFO *timeinfo));
int TXtxtimeinfoToTime_t ARGS((const TXTIMEINFO *timeinfo, time_t *timP));
int TXlocalTxtimeinfoToTime_t ARGS((const TXTIMEINFO *timeinfo, time_t *timP));
int tx_inittz ARGS((void));

int TXprocessInit(TXPMBUF *pmbuf);

int TXgetmaxdescriptors ARGS((void));
int TXgetopendescriptors ARGS((void));
typedef enum TXrawOpenFlag_tag
{
  TXrawOpenFlag_None                    = 0,
  TXrawOpenFlag_Inheritable             = (1 << 0),
  TXrawOpenFlag_SuppressNoSuchFileErr   = (1 << 1),
  TXrawOpenFlag_SuppressFileExistsErr   = (1 << 2)
}
TXrawOpenFlag;
int TXrawOpen(TXPMBUF *pmbuf, const char *fn, const char *pathDesc,
	      const char *path, TXrawOpenFlag txFlags, int flags, int mode);
int     tx_rawread ARGS((TXPMBUF *pmbuf, int fd, CONST char *fnam, byte *buf,
                         size_t sz, int flags));
size_t tx_rawwrite(TXPMBUF *pmbuf, int fd, const char *path,
		TXbool pathIsDescription, byte *buf, size_t sz, TXbool inSig);

/* stat() buffer with extended information where possible: */
#ifndef TXSTATBUFPN
typedef struct TXSTATBUF_tag    TXSTATBUF;
#  define TXSTATBUFPN   ((TXSTATBUF *)NULL)
#endif /* !TXSTATBUFPN */

/* TX_GET_STAT_[AMC]TIME(): returns [amc]time from stat() buf `st',
 * as a double with sub-second precision if available:
 */
#ifdef EPI_HAVE_ST_ATIMENSEC
#  define TX_GET_STAT_ATIME_DOUBLE(st)  ((double)((st).st_atime) +      \
    ((double)((st).st_atimensec))/(double)1000000000.0)
#elif defined(EPI_HAVE_ST_ATIM_TV_NSEC)
#  define TX_GET_STAT_ATIME_DOUBLE(st)  ((double)((st).st_atime) +      \
    ((double)((st).st_atim.tv_nsec))/(double)1000000000.0)
#else
#  define TX_GET_STAT_ATIME_DOUBLE(st)  ((double)((st).st_atime))
#endif
#ifdef EPI_HAVE_ST_MTIMENSEC
#  define TX_GET_STAT_MTIME_DOUBLE(st)  ((double)((st).st_mtime) +      \
    ((double)((st).st_mtimensec))/(double)1000000000.0)
#elif defined(EPI_HAVE_ST_MTIM_TV_NSEC)
#  define TX_GET_STAT_MTIME_DOUBLE(st)  ((double)((st).st_mtime) +      \
    ((double)((st).st_mtim.tv_nsec))/(double)1000000000.0)
#else
#  define TX_GET_STAT_MTIME_DOUBLE(st)  ((double)((st).st_mtime))
#endif
#ifdef EPI_HAVE_ST_CTIMENSEC
#  define TX_GET_STAT_CTIME_DOUBLE(st)  ((double)((st).st_ctime) +      \
    ((double)((st).st_ctimensec))/(double)1000000000.0)
#elif defined(EPI_HAVE_ST_CTIM_TV_NSEC)
#  define TX_GET_STAT_CTIME_DOUBLE(st)  ((double)((st).st_ctime) +      \
    ((double)((st).st_ctim.tv_nsec))/(double)1000000000.0)
#else
#  define TX_GET_STAT_CTIME_DOUBLE(st)  ((double)((st).st_ctime))
#endif

/* NOTE: these are defined exactly in order, as per Windows, for quick copy:*/
/* I(FILE_ATTRIBUTE-sym, TXFILEATTRACTION-string-token) */
#define TXFILEATTR_SYMBOLS_LIST                 \
I(READONLY,             "readonly")             \
I(HIDDEN,               "hidden")               \
I(SYSTEM,               "system")               \
I(VOLUME_LABEL,         "volumelabel")          \
I(DIRECTORY,            "directory")            \
I(ARCHIVE,              "archive")              \
I(DEVICE,               "device")               \
I(NORMAL,               "normal")               \
I(TEMPORARY,            "temporary")            \
I(SPARSE_FILE,          "sparsefile")           \
I(REPARSE_POINT,        "reparsepoint")         \
I(COMPRESSED,           "compressed")           \
I(OFFLINE,              "offline")              \
I(NOT_CONTENT_INDEXED,  "notcontentindexed")    \
I(ENCRYPTED,            "encrypted")

typedef enum TXFILEATTR_ORD_tag                 /* internal use: ordinal */
{
#undef I
#define I(sym, tok)     TXFILEATTR_ORD_##sym,
TXFILEATTR_SYMBOLS_LIST
#undef I
TXFILEATTR_ORD_NUM                              /* must be last */
}
TXFILEATTR_ORD;

typedef enum TXFILEATTR_tag
{
#undef I
#define I(sym, tok)     TXFILEATTR_##sym = (1 << TXFILEATTR_ORD_##sym),
TXFILEATTR_SYMBOLS_LIST
#undef I
}
TXFILEATTR;
#define TXFILEATTRPN    ((TXFILEATTR *)NULL)

typedef struct TXFILEATTRACTION_tag /* FILE_ATTRIBUTE action list element */
{
  char                          op;             /* '=', '+', '-' */
  TXFILEATTR                    value;          /* Bits to add/remove/set */
  struct TXFILEATTRACTION_tag   *next;          /* Next guy in list */
}
TXFILEATTRACTION;
#define TXFILEATTRACTIONPN      ((TXFILEATTRACTION *)NULL)

TXFILEATTRACTION *TXfileAttrActionOpen ARGS((CONST char *s));
TXFILEATTRACTION *TXfileAttrActionClose ARGS((TXFILEATTRACTION *fa));
TXFILEATTR TXfileAttrActionAdjust ARGS((TXFILEATTRACTION *f, TXFILEATTR attrs,
                                        unsigned *mode));

#define TXFTIMESRC_SYMBOLS_LIST \
I(changed)                      \
I(accessed)                     \
I(written)                      \
I(created)                      \
I(fixed)

typedef enum TXFTIMESRC_tag
{
#undef I
#define I(sym)  TXFTIMESRC_##sym,
TXFTIMESRC_SYMBOLS_LIST
#undef I
TXFTIMESRC_NUM                                  /* must be last */
}
TXFTIMESRC;
#define TXFILESRCPN     ((TXFTIMESRC *)NULL)

#if defined(EPI_STAT_SPN) && (defined(_SYS_STAT_H) || defined(_SYS_STAT_H_) || defined(_INC_STAT) || defined(_SYS_STAT_INCLUDED) || defined(_H_STAT)) /* os.h and sys/stat.h include */
struct TXSTATBUF_tag
{
  EPI_STAT_S    stat;
  /* these are -EPI_OS_DOUBLE_MAX if unset/unknown: */
  double        lastAccessedTime;               /* time_t */
  double        lastModifiedTime;               /* time_t */
  double        creationTime;                   /* time_t */
  TXFTIMESRC    st_atimeSrc, st_mtimeSrc, st_ctimeSrc;
  TXFILEATTR    fileAttrs;
};
#  define TXSTATBUF_DEFINED
#endif /* EPI_STAT_SPN && S_IFMT */

int TXstat ARGS((CONST char *path, int fd, int doLink, TXSTATBUF *stbuf));
CONST char *TXfileAttrName ARGS((TXFILEATTR_ORD ord, int useInternal));
TXFILEATTR  TXstrToFileAttr ARGS((CONST char *s, size_t n));
size_t      TXfileAttrModeString ARGS((char *buf, size_t sz, TXFILEATTR fa,
                                       int useInternal));

CONST char *TXftimesrcName ARGS((TXFTIMESRC fts));

#ifdef _WIN32
int TXstartTexisMonitorService(TXPMBUF *pmbuf, int trace);
#endif /* _WIN32 */

int TXmapOsErrorToErrno ARGS((TXERRTYPE osError));
int TXaccess ARGS((CONST char *path, int mode));
/* For TXaccess(): */
#ifndef R_OK
#  define R_OK          04
#endif
#ifndef W_OK
#  define W_OK          02
#endif
#ifndef X_OK
#  define X_OK          01
#endif
#ifndef F_OK
#  define F_OK          0
#endif

int     TXfilenameIsDevice ARGS((CONST char *filename, int onAnyPlatform));

int     TXgetBacktrace(char *buf, size_t bufSz, int flags);
#ifdef _WIN32
extern int (*TXinvalidParameterHandlerGetBacktraceFunc)
                   (char *buf, size_t bufSz, int flags);
#endif /* _WIN32 */

/* ------------------------------------------------------------------------ */

/* This logic must correspond with TXwatchPathOpen().
 * Note that these ..._SUPPORTED macros indicate platform/OS support,
 * not whether the feature is enabled (see TXvxVortexWatchpathEnabled):
 */
#ifdef EPI_HAVE_SYS_INOTIFY_H
#  define TX_WATCHPATH_SUPPORTED	1
#  undef  TX_WATCHPATH_SUBTREE_SUPPORTED
#elif defined(_WIN32)
#  define TX_WATCHPATH_SUPPORTED	1
#  define TX_WATCHPATH_SUBTREE_SUPPORTED 1
#else
#  undef  TX_WATCHPATH_SUPPORTED
#  undef  TX_WATCHPATH_SUBTREE_SUPPORTED
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* Types of changes to watch for; a watchpath event is one of these: */
#define TX_WATCHPATH_CHANGE_SYMBOLS_LIST        \
  I(Other)        /* must be first */           \
  I(Access)                                     \
  I(Create)                                     \
  I(Delete)                                     \
  I(Modify)                                     \
  I(MoveFrom)                                   \
  I(MoveTo)                                     \
  I(Attribute)                                  \
  I(Size)                                       \
  I(Security)                                   \
  I(CreationTime)                               \
  I(Open)                                       \
  I(CloseWrite)                                 \
  I(CloseNonWrite)                              \
  I(Unmount)                                    \
  I(Overflow)     /* too many/missed events */  \
  I(RemoveWatch)  /* watch deleted */

typedef enum TXwatchPathChange_tag
{
  TXwatchPathChange_UNKNOWN = -1,
#undef I
#define I(tok)  TXwatchPathChange_##tok,
  TX_WATCHPATH_CHANGE_SYMBOLS_LIST
#undef I
  TXwatchPathChange_NUM
}
TXwatchPathChange;

void              TXwatchPathChangeSetAll(byte changes[TXwatchPathChange_NUM],
                                          int set);
TXwatchPathChange  TXwatchPathChangeStrToEnum(const char *s, size_t sLen);
const char        *TXwatchPathChangeEnumToStr(TXwatchPathChange chg);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_WATCHPATH_OPEN_FLAG_SYMBOLS_LIST	\
I(SubTree)					\
I(Symlink)					\
I(LinkedOnly)					\
I(DirOnly)

typedef enum TXwatchPathOpenFlagIter_tag
{
#undef I
#define I(tok)  TXwatchPathOpenFlagIter_##tok,
  TX_WATCHPATH_OPEN_FLAG_SYMBOLS_LIST
#undef I
  TXwatchPathOpenFlagIter_NUM
}
TXwatchPathOpenFlagIter;

typedef enum TXwatchPathOpenFlag_tag
{
  TXwatchPathOpenFlag_None = 0,
#undef I
#define I(tok)  TXwatchPathOpenFlag_##tok = (1 << TXwatchPathOpenFlagIter_##tok),
  TX_WATCHPATH_OPEN_FLAG_SYMBOLS_LIST
#undef I
}
TXwatchPathOpenFlag;

TXwatchPathOpenFlag TXwatchPathOpenFlagStrToEnum(const char *s, size_t sLen);
const char        *TXwatchPathOpenFlagEnumToStr(TXwatchPathOpenFlag flag);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TX_WATCHPATH_EVENT_FLAG_SYMBOLS_LIST	\
I(IsDir)

typedef enum TXwatchPathEventFlagIter_tag
{
#undef I
#define I(tok)  TXwatchPathEventFlagIter_##tok,
  TX_WATCHPATH_EVENT_FLAG_SYMBOLS_LIST
#undef I
  TXwatchPathEventFlagIter_NUM
}
TXwatchPathEventFlagIter;

typedef enum TXwatchPathEventFlag_tag
{
  TXwatchPathEventFlag_None = 0,
#undef I
#define I(tok)  TXwatchPathEventFlag_##tok = (1 << TXwatchPathEventFlagIter_##tok),
  TX_WATCHPATH_EVENT_FLAG_SYMBOLS_LIST
#undef I
}
TXwatchPathEventFlag;

TXwatchPathEventFlag TXwatchPathEventFlagStrToEnum(const char *s, size_t sLen);
const char        *TXwatchPathEventFlagEnumToStr(TXwatchPathEventFlag flag);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

typedef struct TXwatchPath_tag		TXwatchPath;
typedef struct TXwatchPathEvent_tag	TXwatchPathEvent;

TXwatchPath *TXwatchPathOpen(TXPMBUF *pmbuf, const char *path,
			     byte changes[TXwatchPathChange_NUM],
			     TXwatchPathOpenFlag openFlags);
TXwatchPath *TXwatchPathClose(TXwatchPath *watchPath);
TXFHANDLE    TXwatchPathGetWaitableFhandle(TXwatchPath *watchPath);
char        *TXwatchPathGetPath(TXwatchPath *watchPath);
byte        *TXwatchPathGetRequestedChanges(TXwatchPath *watchPath);
int     TXwatchPathGetEvent(TXwatchPath *watchPath, TXwatchPathEvent **event);

TXwatchPathEvent *TXwatchPathEventClose(TXwatchPathEvent *event);
TXwatchPathEvent *TXwatchPathEventOpen(TXwatchPath *watchPath,
    const char *path, TXwatchPathChange change,
    TXwatchPathEventFlag eventFlags, int osFlags, EPI_UINT32 id);
size_t	TXwatchPathEventGetSize(TXwatchPathEvent *event);
char	         *TXwatchPathEventGetPath(TXwatchPathEvent *event);
TXwatchPathChange TXwatchPathEventGetChange(TXwatchPathEvent *event);
TXwatchPathEventFlag   TXwatchPathEventGetEventFlags(TXwatchPathEvent *event);
EPI_UINT32        TXwatchPathEventGetId(TXwatchPathEvent *event);
double	          TXwatchPathEventGetEventTime(TXwatchPathEvent *event);

extern void (*tx_fti_watchPath_closeHook)(void *obj);

/* TXtraceWatchPath:
 * 0x0001  open/close
 * 0x0002  events opened
 * 0x0004  events closed
 * 0x0040  select()/Wait...() before
 * 0x0080  select()/Wait...() after
 */
extern int      TXtraceWatchPath;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

extern CONST char       TxPlatformDesc[];

/******************************************************************/

extern char     TxRamDbfName[];

extern int TXverbosity;
extern int TXlikepstartwt;
extern long TXlikeptime;
extern int TXlikepmode;
extern int TXnlikephits;
extern int TXlikermaxthresh;
extern int TXlikermaxrows;
extern int TXbtreemaxpercent;
extern int TXmaxlinearrows;
extern int TXwildoneword;
extern int TXwildsufmatch;
extern int TXallineardict;
extern int TXindexminsublen;

TXbool	TXgetDiscardUnsetParameterClauses(void);
TXbool	TXsetDiscardUnsetParameterClauses(TXbool discard);

typedef enum TXindexWithinFlag
{
  TXindexWithinFlag_Chars         = 0x01, /* use index for within-chars */
  TXindexWithinFlag_Words         = 0x02, /* use index for within-words */
  /* ...OptimizeChars: Optimize within-chars window.  Currently
   * applies iff index exprs are only `\alnum{2,99}':
   */
  TXindexWithinFlag_OptimizeChars = 0x04,
  /* ...AssumeOneHitInter: Narrow window by assuming that intervening
   * words (non-query words in the window) match index expression(s)
   * at only *one* unique byte offset, which is most often the case
   * (but which we do not know at index search time).  Excludes more
   * too-wide hits during index search, but risks also excluding valid
   * hits (those with intervening multi-expr-match words).  Implicitly
   * turns off post-processing: not needed since narrower window size
   * removes ambiguous cases needing post-proc.  If valid hits are
   * excluded (e.g. intervening word "o'malley" counts as 2 not 1
   * index word), user can either increase their `w/N' -- and
   * potentially get "foo bar" as 2 intervening words instead of
   * "o'malley" as 1 -- or turn off this flag to get more-exact
   * answers -- at cost of post-proc.  Bug 7004:
   */
  TXindexWithinFlag_AssumeOneHitInter = 0x08,
  TXindexWithinFlag_Default       = 0x0f
}
TXindexWithinFlag;

extern TXindexWithinFlag        TXindexWithin;

extern size_t TXtableReadBufSz;
extern int TXindexBtreeExclusive;

extern int TXbtreedump;
extern char *TXbtreelog;
extern char **TXbtreelog_srcfile;
extern int *TXbtreelog_srcline;
extern DBTBL *TXbtreelog_dbtbl;
/* TXTRACELOCKS_IS_OFF() is true iff lock logging is known to be off: */
#define TXTRACELOCKS_IS_OFF()	\
	(!TXApp || (!TXApp->traceLocksDatabase && !TXApp->traceLocksTable))

#ifdef NO_NEW_CASE
extern int TXigncase;
#endif
extern int TXallowidxastbl;
extern int TXindexchunk;
extern size_t TXindexmemUser;
extern size_t TXindexmmapbufsz, TXindexmmapbufsz_val;
extern TXMDT TXindexmeter, TXcompactmeter;
extern int TXrowsread;
extern int TXlikepthresh;
extern int TXseq;
extern int TXunlocksig;
extern int TXdefaultlike;
extern int TXlikepminsets;
extern int TXsingleuser;/* JMT 98-05-07 */
extern int TXbtreecache;/* JMT 98-05-29 */
extern char texisver[];
int	TXsetinstallpath ARGS((char *));
int	TXregsetinstallpath ARGS((char *));

/* WTF if these change see also source/epi/backref.c: */
extern char TXInstallPath[];
#define TXINSTALLPATH_VAL       (TXInstallPath + 16)

extern int TXinstallpathset;
extern char *TXMonitorPath;
extern APICP *globalcp;
extern int TxUniqNewList;                           /* KNG 000127 */
extern void (*TxInForkFunc) ARGS((int on));             /* KNG 991219 */
extern int TXintsem;                                /* JMT 990115 */
extern TXbool TXclearStuckSemaphoreAlarmed;
#define TXlicensehits  tx_compilewarn
extern int TXlicensehits;                           /* JMT 990301 */
extern int TXminimallocking;                    /* JMT 1999-06-04 */
extern int TXverbosepredvalid;
extern int TXdisablenewlist;/* JMT 1999-08-11 */
extern int TXverifysingle;/* JMT 1999-08-11 */
extern int TXexceptionbehaviour;/* JMT 2000-07-07 */
extern int TXminserver;
extern const char TXWhitespace[];

int	TXsetTexisApicpDefaults ARGS((APICP *cp, int setBuiltin,
                                      int setTexis5));
int     TXgetMonitorRunLevel(void);
int     TXgetIsTexisMonitorService(void);
#ifdef _WIN32
int     TXsetIsTexisMonitorService(TXPMBUF *pmbuf, int isTexisMonitorService);
#endif /* _WIN32 */

/******************************************************************/
/*	Manipulate indices                                        */

IINDEX *openiindex ARGS((void));
IINDEX *closeiindex ARGS((IINDEX *));
IINDEX *openiindex ARGS((void));
IINDEX *indexand ARGS((IINDEX *, IINDEX *, int));
IINDEX *indexor ARGS((IINDEX *, IINDEX *, int));
int     indinv ARGS((IINDEX *));
int     _indrev ARGS((IINDEX *));
int     indexmirror ARGS((IINDEX *));
int	indsort ARGS((IINDEX *, IINDEX*, int));

CONST char * TXiindexTypeName ARGS((IINDEX *iin));
void    TXdumpIindex ARGS((TXPMBUF *pmbuf, int indent, IINDEX *iin));

int	dbidxrewind ARGS((DBIDX *));
int	TXdbidxrewind ARGS((DBIDX *));
const char *TXdbidxGetName(DBIDX *dbidx);
#ifdef NEW_I
DBIDX	*dbidxopen(char *name, int type, int, int, int, int);
BTREE_SEARCH_MODE	dbidxsetsearch(DBIDX *, BTREE_SEARCH_MODE);
BTLOC	dbidxsearch(DBIDX *, size_t, void *);
BTLOC	TXdbidxgetnext(DBIDX *, size_t *, void *, byte **);
int	dbidxinsert(DBIDX *, BTLOC *, size_t, void *);
int	dbidxspinsert(DBIDX *, BTLOC *, size_t, void *, int);
DBIDX	*dbidxclose(DBIDX *);
DBIDX	*dbidxfrombtree(BTREE *name, int type);
#endif

int	TXindsort2 ARGS((IINDEX *, IINDEX*, int, DBIDX *));
int	TXindexinv ARGS((IINDEX *));

int tx_delindexfile ARGS((int errlevel, CONST char *fn, CONST char *p,
                          int flags));

/******************************************************************/

int	TXgetparsetimemesg ARGS((void));
int	TXsetparsetimemesg ARGS((int));
time_t  TXindparsetime(const char *buf, size_t bufSz, int flags,
		       TXPMBUF *pmbuf);
int	ddgetorign ARGS((DD *, int));
int	ddfindname ARGS((DD *dd, char *fname));

char	*TXddtotext ARGS((DD *));
int	TXloadtable ARGS((DDIC *, FLDOP *, int, CONST char *localTableName));
int	TXdumptable(DDIC *ddic, const char *localTableName,
		    const char *remoteTableName, FLDOP *fo,
		    EPI_OFF_T rowlimit, int fd,
#ifdef _WIN32
		    HANDLE handle,
#endif /* _WIN32 */
		    int usewrite, TXMDT metertype, EPI_OFF_T *rowsSent);
#ifdef unix
int	TXwriteall ARGS((int, char *, size_t, int));
DBTBL	*TXgettable ARGS((int, char **));
#endif
#ifdef _WIN32
int	TXwriteall ARGS((int, HANDLE, char *, size_t, int));
DBTBL	*TXgettable ARGS((int, char **, HANDLE));
#endif

void	*TXblobiGetPayload(ft_blobi *v, size_t *sz);
size_t	TXblobiGetPayloadSize(ft_blobi *v);
int	TXblobiFreeMem(ft_blobi *bi);
void	*TXblobiGetMem(ft_blobi *bi, size_t *sz);
int	TXblobiSetMem(ft_blobi *bi, void *mem, size_t sz, int isAlloced);
void	*TXblobiRelinquishMem(ft_blobi *bi, size_t *sz);
int	TXblobiIsInMem(ft_blobi *bi);
DBF	*TXblobiGetDbf(ft_blobi *bi);
int	TXblobiSetDbf(ft_blobi *bi, DBF *dbf);
FTN	TXblobiGetStorageType(ft_blobi *bi);

EPI_OFF_T bitob ARGS((void *bi, TBL *outtbl));

size_t TXblobzGetUncompressedSize(TXPMBUF *pmbuf, const char *file,
	     EPI_OFF_T offset, const byte *buf, size_t bufSz, size_t fullSz);
void   *TXagetblobz ARGS((ft_blobi *v, size_t *sz));
void   *bztobi ARGS((EPI_OFF_T b, TBL *intbl));
EPI_OFF_T bitobz ARGS((void *bi, TBL *outtbl));

int	TXcalcrank ARGS((DBTBL *, PRED *, int *nrank, FLDOP *));
int     TXpredHasOp(PRED *p, QNODE_OP op);
int	TXtrybubble ARGS((DBTBL *, PRED *, PROJ *, FLDOP *, TBSPEC *));

int	TXcompatibletypes ARGS((int, int));

int	TXcodesintersect ARGS((long, long, long, long));
int	TXcodesintersect1 ARGS((long, long, long));
int	TXqstrcmp ARGS((CONST void *a, CONST void *b));
char	*TXpredflds ARGS((PRED *));
IINODE	*TXcloseiinode ARGS((IINODE *));
IINODE  *TXgetcachediinode(DBTBL *tb, PRED *p, FLDOP *fo, int asc, int inv);
int	TXcacheiinode(IINODE *iinode, DBTBL *tb, PRED *p, FLDOP *fo, int asc,
		      int inv);
#ifdef CACHE_IINODE
IINODE	*TXpredtoiinode ARGS((DBTBL *, PRED *, TBSPEC *, FLDOP *, int, int));
#endif
int	TXpredgetindx ARGS((PRED *, DBTBL *, DBTBL *));

FLD     *TXpredGetColumnAndField ARGS((PRED *p, int *lookrightp,
                                       char **fnamep));
FLD     *TXdemoteSingleStrlstToVarchar ARGS((FLD *fld));
int     TXfixupMultiItemRelopSingleItem ARGS((FLD *colFld,
             CONST char *colName, int op, FLD **paramFld,
             FLD **promotedParamFld, FLDOP *fo));

int	TXsetstatistic ARGS((DDIC *, char *, char *, long, char *, int));
int	TXdelstatistic(DDIC *ddic, char *object, char *stat);
int	TXgetstatistic ARGS((DDIC *, char *, char *, ft_counter *, long *, char **));
DBTBL	*TXcreatestatstable ARGS((DDIC *));
int   TXupdateAndSendStats(DDIC *ddic, int loops, TXFHANDLE fh, int *goPtr,
			   int flags);

extern int TXdbCleanupVerbose;

int	TXdocleanup ARGS((DDIC *));

int	TXinitshared ARGS((IDBLOCK *));
int  TXlockenglish(IDBLOCK *a, int verbose, const int *semPreVal, FILE *ofh);
TXEXIT	TXdumplocks(TXPMBUF *pmbuf, DBLOCK *aa, int verbose,
		    const char *outPathForShmem);

void	TXsleepForLocks(TXPMBUF *pmbuf, int msec, const char *func, int line);

char 	*TXgetmonitorpath ARGS((void));
TXEXIT	CDECL TXtsqlmain ARGS((int, char **, int, char **));
TXEXIT	VXmain ARGS((int, char **, int, char **, TXPMBUF *cachedMsgs));
TXEXIT	TXcopydbf ARGS((int, char **, int, char **));
int	WLmain ARGS((int, char**, int, char **));
int	TXverifyindex ARGS((DDIC *ddic, char *iname, long tblrows));
int	DBVmain ARGS((int, char**, int, char **));
TXEXIT	CKmain ARGS((char *));

int VHmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int ATmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int KCmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int CDmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int LRmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int COPYDBmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int TIMPmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int BRmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));
int TXTOCmain ARGS((int argc, char *argv[], int argcstrip, char **argvstrip));

int TXcompactTable ARGS((DDIC *ddic, QUERY *q, int overwrite));

TXEXIT TXcreateDbMain(int argcLocal, char *argvLocal[]);

/* TX_OK_SQL_INT_N(): nonzero if SQL-int(N) supported and enabled.
 * see also ntexis.n:
 */
#ifdef EPI_SQL_INT_N
#  ifdef EPI_ENABLE_SQL_INT_N
#    define TX_OK_SQL_INT_N()   1
#  else /* !EPI_ENABLE_SQL_INT_N */
extern int              TXenableSqlIntN;
extern CONST char       TXenableSqlIntNVar[];
#    define TX_OK_SQL_INT_N()   (TXenableSqlIntN == -1 ? \
(TXenableSqlIntN = (getenv(TXenableSqlIntNVar) != CHARPN)) : TXenableSqlIntN)
#  endif /* !EPI_ENABLE_SQL_INT_N */
#else /* !EPI_SQL_INT_N */
#  define TX_OK_SQL_INT_N()     0
#endif /* !EPI_SQL_INT_N */

int	TXresettimecache ARGS((void));
int	TXregroup ARGS((void));
#ifdef _WIN32
#  define tx_savefd(fd)         1
#  define tx_releasefd(fd)      1
#else /* !_WIN32 */
int     tx_savefd ARGS((int fd));
int     tx_releasefd ARGS((int fd));
#endif /* !_WIN32 */
int     TXclosedescriptors ARGS((int flags));
int     doshell ARGS((FLD *cmd, FLD *arg1, FLD *arg2, FLD *arg3, FLD *arg4));
#ifndef OLD_EXEC
int     dobshell ARGS((FLD *cmd, FLD *arg1, FLD *arg2, FLD *arg3, FLD *arg4));
#endif
MMQL	*mmripq ARGS((char *));
int	TXshowindexes ARGS((int));
int	TXshowactions ARGS((int));

int	projcmp ARGS((PROJ *, PROJ *));

FLD	*TXqueryfld ARGS((DDIC *, DBTBL *, QNODE *, FLDOP *, SUBQ_TYPE, int verbose));
void	*TXfreefldshadow ARGS((FLD *));
void	TXfreefldshadownotblob ARGS((FLD *));
int     setfldandsize ARGS((FLD *f, void *v, size_t n, TXbool forceNormal));
#define TXfldType(fld)          ((FTN)(fld)->type)
#define TXfldbasetype(fl)       ((FTN)(((fl)->type) & (DDTYPEBITS)))
void   *TXfldTakeAllocedData ARGS((FLD *fld, size_t *pn));

/* for fldmath tracing: */
int     TXfldresultmsg ARGS((CONST char *pfx, CONST char *arg, FLD *fr,
                             int res, int verb));

typedef struct TXsharedBuf_tag
{
  size_t        refCnt;
  byte          *data;
  size_t        dataSz;
}
TXsharedBuf;
#define TXsharedBufPN ((TXsharedBuf *)NULL)

/* if TXALLOC... changes see also txtypes.h wtf: */
#undef TXALLOC_PROTO_SOLE
#undef TXALLOC_PROTO
#undef TXALLOC_ARGS_PASSTHRU
#undef TXALLOC_ARGS_DEFAULT
#ifdef MEMDEBUG
#  define TXALLOC_PROTO_SOLE    CONST char *file, int line, CONST char *memo
#  define TXALLOC_PROTO         , TXALLOC_PROTO_SOLE
#  define TXALLOC_ARGS_PASSTHRU , file, line, memo
#  define TXALLOC_ARGS_DEFAULT  , __FILE__, __LINE__, CHARPN
#  undef TXfree
#  undef TXdupStrList
#  undef TXfreeStrList
#  undef TXdupStrEmptyTermList
#  undef TXfreeStrEmptyTermList
#  undef TXfreeArrayOfStrLists
#  undef TXmalloc
#  undef TXcalloc
#  undef TXrealloc
#  undef TXstrdup
#  undef TXstrndup
#  undef TXexpandArray
#  undef TXallocProtectable
#  undef TXfreeProtectable
#  undef TXprotectMem
#  undef TXsharedBufOpen
#  undef TXsharedBufClose
#else /* !MEMDEBUG */
#  define TXALLOC_PROTO_SOLE    void
#  define TXALLOC_PROTO
#  define TXALLOC_ARGS_PASSTHRU
#  define TXALLOC_ARGS_DEFAULT
#endif /* !MEMDEBUG */
void	*TXfree ARGS((void *p TXALLOC_PROTO));
size_t TXcountStrList ARGS((char **list));
char   **TXdupStrList ARGS((TXPMBUF *pmbuf, char **list, size_t n
                            TXALLOC_PROTO));
char   **TXfreeStrList ARGS((char **list, size_t n TXALLOC_PROTO));
size_t TXcountStrEmptyTermList ARGS((char **list));
char   **TXdupStrEmptyTermList ARGS((TXPMBUF *pmbuf, CONST char *fn,
                                     char **list, size_t n TXALLOC_PROTO));
char   **TXfreeStrEmptyTermList ARGS((char **list, size_t n TXALLOC_PROTO));
char   ***TXfreeArrayOfStrLists ARGS((char ***list, size_t n TXALLOC_PROTO));
void	*TXmalloc ARGS((TXPMBUF *pmbuf, CONST char *fn,
                        size_t sz TXALLOC_PROTO));
void	*TXcalloc ARGS((TXPMBUF *pmbuf, CONST char *fn,
                        size_t n, size_t sz TXALLOC_PROTO));
void	*TXrealloc ARGS((TXPMBUF *pmbuf, CONST char *fn, void *p,
                         size_t sz TXALLOC_PROTO));
char	*TXstrdup ARGS((TXPMBUF *pmbuf, CONST char *fn,
                        CONST char *s TXALLOC_PROTO));
char	*TXstrndup ARGS((TXPMBUF *pmbuf, CONST char *fn,
                         CONST char *s, size_t n TXALLOC_PROTO));
int     TXexpandArray ARGS((TXPMBUF *pmbuf, CONST char *fn, void **array,
               size_t *allocedNum, size_t incNum, size_t elSz TXALLOC_PROTO));
#ifdef TX_ENABLE_MEM_PROTECT
void    *TXallocProtectable(TXPMBUF *pmbuf, const char *fn, size_t sz,
                            TXMEMPROTECTFLAG flags TXALLOC_PROTO);
int     TXfreeProtectable(TXPMBUF *pmbuf, void *p TXALLOC_PROTO);
int     TXprotectMem(TXPMBUF *pmbuf, void *p,
                     TXMEMPROTECTPERM perms TXALLOC_PROTO);
#endif /* TX_ENABLE_MEM_PROTECT */

TXsharedBuf *TXsharedBufOpen(TXPMBUF *pmbuf, byte *data,
			 size_t dataSz, TXbool dataIsAlloced TXALLOC_PROTO);
TXsharedBuf *TXsharedBufClone(TXsharedBuf *buf);
TXsharedBuf *TXsharedBufClose(TXsharedBuf *buf TXALLOC_PROTO);
int	TXgetSysMemFuncDepth(void);
size_t	TXgetMemUsingFuncs(const char **funcs, size_t funcsLen);
typedef void *(TXOBJCLOSEFUNC)(void *obj);
void 	*TXfreeObjectArray(void *array, size_t numItems,
			   TXOBJCLOSEFUNC *objClose);

int	TXmemGetNumAllocFailures ARGS((void));
#ifdef MEMDEBUG
#  define TXfree(p)             TXfree(p TXALLOC_ARGS_DEFAULT)
#  define TXdupStrList(pmbuf, list, n)  \
  TXdupStrList(pmbuf, list, n TXALLOC_ARGS_DEFAULT)
#  define TXfreeStrList(list, n)   TXfreeStrList(list, n TXALLOC_ARGS_DEFAULT)
#  define TXdupStrEmptyTermList(pmbuf, fn, list, n)     \
  TXdupStrEmptyTermList(pmbuf, fn, list, n TXALLOC_ARGS_DEFAULT)
#  define TXfreeStrEmptyTermList(list, n)       \
   TXfreeStrEmptyTermList(list, n TXALLOC_ARGS_DEFAULT)
#  define TXfreeArrayOfStrLists(list, n)        \
   TXfreeArrayOfStrLists(list, n TXALLOC_ARGS_DEFAULT)
#  define TXmalloc(pmbuf, fn, sz)    \
  TXmalloc(pmbuf, fn, sz TXALLOC_ARGS_DEFAULT)
#  define TXcalloc(pmbuf, fn, n, sz) \
  TXcalloc(pmbuf, fn, n, sz TXALLOC_ARGS_DEFAULT)
#  define TXrealloc(pmbuf, fn, p, sz) \
  TXrealloc(pmbuf, fn, p, sz TXALLOC_ARGS_DEFAULT)
#  define TXstrdup(pmbuf, fn, s)     \
  TXstrdup(pmbuf, fn, s TXALLOC_ARGS_DEFAULT)
#  define TXstrndup(pmbuf, fn, s, n) \
  TXstrndup(pmbuf, fn, s, n TXALLOC_ARGS_DEFAULT)
#  define TXexpandArray(pmbuf, fn, array, allocedNum, incNum, elSz)     \
TXexpandArray(pmbuf, fn, array, allocedNum, incNum, elSz TXALLOC_ARGS_DEFAULT)
#  define TXallocProtectable(pmbuf, fn, sz, flags)      \
  TXallocProtectable(pmbuf, fn, sz, flags TXALLOC_ARGS_DEFAULT)
#  define TXfreeProtectable(pmbuf, p)   \
  TXfreeProtectable(pmbuf, p TXALLOC_ARGS_DEFAULT)
#  define TXprotectMem(pmbuf, p, flags) \
  TXprotectMem(pmbuf, p, flags TXALLOC_ARGS_DEFAULT)
#  define TXsharedBufOpen(pmbuf, data, dataSz, dataIsAlloced)	\
  TXsharedBufOpen(pmbuf, data, dataSz, dataIsAlloced TXALLOC_ARGS_DEFAULT)
#  define TXsharedBufClose(buf)                 \
  TXsharedBufClose(buf TXALLOC_ARGS_DEFAULT)
#endif /* MEMDEBUG */

/* Increments `*arrayPtr' by `inc'.  May add to `*allocedNumPtr'.
 * Does *not* inc `usedNum'; caller must.  Returns 0 on error.
 * New data not cleared.  `arrayPtr' is e.g. `&array' if `struct foo *array'.
 * Returns 0 on error:
 */
#define TX_EXPAND_ARRAY(pmbuf, arrayPtr, usedNum, allocedNumPtr, inc)       \
 ((usedNum)+(inc)<=*(allocedNumPtr) ? 1 : TXexpandArray((pmbuf),__FUNCTION__,\
   (void **)(void *)(arrayPtr), (allocedNumPtr), (inc), sizeof(**(arrayPtr))))

/* Increments `*arrayPtr' by 1.  May add to `*allocedNumPtr'.
 * Does *not* inc `usedNum'; caller must.  Returns 0 on error:
 * New data not cleared.  `arrayPtr' is e.g. `&array' if `struct foo *array'.
 * Returns 0 on error:
 */
#define TX_INC_ARRAY(pmbuf, arrayPtr, usedNum, allocedNumPtr)	\
  TX_EXPAND_ARRAY(pmbuf, arrayPtr, usedNum, allocedNumPtr, 1)

#define TX_NEW_ARRAY(pmbuf, num, type)				\
	((type *)TXcalloc((pmbuf), __FUNCTION__, (num), sizeof(type)))
#define TX_NEW(pmbuf, type)	TX_NEW_ARRAY(pmbuf, 1, type)

void    TXputmsgOutOfMem ARGS((TXPMBUF *pmBuf, int num, CONST char *fn,
                               size_t nElems, size_t elSize));

int TXincarray(TXPMBUF *pmbuf, void **array, size_t num, size_t *anum, size_t elsz);
int	TXstrcmp ARGS((const char *, const char *));

#ifdef EPI_TRACK_MEM
extern EPI_HUGEINT      TXmemCurTotalAlloced;
#endif /* EPI_TRACK_MEM */

/******************************************************************/

/* NOTE: this type is in the license struct; see notes in txlic.h: */
#ifdef __alpha
/* historical back-compatibility; was long: */
typedef EPI_INT64 TX_DATASIZE_INT;
#else /* !__alpha */
/* constant-size on all other platforms: */
typedef EPI_INT32 TX_DATASIZE_INT;
#endif /* !__alpha */

typedef struct DATASIZE {
	TX_DATASIZE_INT	gig;
	TX_DATASIZE_INT	bytes;
} DATASIZE;
#define DATASIZEPN      ((DATASIZE *)NULL)

#define TX_DATASIZE_GIG (1 << 30)

/* TX_GET_DATASIZE(ds) returns EPI_INT64 value of `*ds': */
#define TX_GET_DATASIZE(ds)     \
((((EPI_INT64)(ds)->gig)*(EPI_INT64)TX_DATASIZE_GIG) + (EPI_INT64)(ds)->bytes)
/* TX_SET_DATASIZE(ds) sets `*ds' from `val': */
#define TX_SET_DATASIZE(ds, val)                        \
  ((ds)->gig = (val)/(EPI_INT64)TX_DATASIZE_GIG,        \
   (ds)->bytes = (val) % (EPI_INT64)TX_DATASIZE_GIG)

int	TXresetdatasize ARGS((DATASIZE *));
int	TXadddatasize ARGS((DATASIZE *, long));
int	TXadddatasizeh ARGS((DATASIZE *, EPI_HUGEINT));
int	TXsubdatasize ARGS((DATASIZE *, long));
int	TXdatasizecmp ARGS((DATASIZE *, DATASIZE *));
int	TXdatasizeadd ARGS((DATASIZE *, DATASIZE *));

struct INDEXSTATS_tag {
	long	 totentries;
	long	 newentries;
	long	 delentries;
	DATASIZE indexeddata;
};
#ifndef INDEXSTATSPN            /* WTF avoid mutual include with fdbi.h */
typedef struct INDEXSTATS_tag   INDEXSTATS;
#  define INDEXSTATSPN  ((INDEXSTATS *)NULL)
#endif

/******************************************************************/

void	TXshowplan ARGS((void));
int	TXplantablepred ARGS((DBTBL *, PRED *, PROJ *, FLDOP *));

int	resetindexinfo ARGS((INDEXINFO *));
INDEXINFO *closeindexinfo ARGS((INDEXINFO *));
int	TXchooseindex(INDEXINFO *indexinfo, DBTBL *dbtbl, QNODE_OP fop,
		      FLD *param, int paramIsRHS);

extern EPI_HUGEUINT	TXindcnt;

/******************************************************************/
/* STRLST functions */

int TXstrlstcount ARGS((char **));
int TXstrlstcmp	ARGS((char **, char **));
char *TXgetStrlst ARGS((FLD *f, ft_strlst *hdr));
size_t TXgetStrlstLength ARGS((const ft_strlst *hdr, const char *strData));

int TXstrlstBufBegin ARGS((HTBUF *buf));
int TXstrlstBufAddString ARGS((HTBUF *buf, CONST char *s, size_t sz));
int TXstrlstBufEnd ARGS((HTBUF *buf));

/******************************************************************/

int TXfld_optimizeforin(FLD *f, DDIC *ddic);
int TX_fldSortStringList(FLD *f);

/******************************************************************/

int	TXcompatibletypes ARGS((int, int));
int	TXddicstmt ARGS((DDIC *));
int	TXddicBeginInternalStmt(const char *fn, DDIC *ddic);
int	TXddicEndInternalStmt(DDIC *ddic);
int	TXddicvalid ARGS((DDIC *ddic, char **reason));
int	TXddicnewproc ARGS((DDIC *));
int	TXddicsetdefaultoptimzations ARGS((DDIC *));

/******************************************************************/
/* Functions we don't want want to be well known */

#define TXpushid	TXind1
#define TXpopid		TXind2

int	TXpushid ARGS((DDIC *, int, int));
int	TXpopid ARGS((DDIC *));

/******************************************************************/

typedef struct GROUPBY_INFO {
	FLDCMP fc;
	DBTBL *tmptbl;
	size_t cmpbufsz;

	long found;/* JMT 98-03-03  make long for 64-bit alignment */
	/* KNG 20130418 Win64 long < 64-bit but need 64-bit alignment;
	 * malloc the buffer separately and let malloc() align it:
	 */
	byte *cmpbuf;
	size_t	cmpbufAllocedSz;
	size_t tsz, sz;
	int dontread;
	RECID *where;
	int statstat;
	BTREE *statloc;
	NFLDSTAT *origfldstat;
	int indexonly;
	int dontwrite;
} GROUPBY_INFO;
#define GROUPBY_INFOPN	((GROUPBY_INFO *)NULL)

GROUPBY_INFO *TXcloseginfo ARGS((GROUPBY_INFO *));
GROUPBY_INFO *TXopenGroupbyinfo ARGS((void));

/* Object for "de-multiplexing" a table: splitting a row into multiple rows,
 * one per value of a multi-value field (eg. strlst).  Can also be a no-op
 * (ie. pass-through unchanged).  Used by GROUP BY and DISTINCT on strlst:
 */
typedef struct TXDEMUX_tag
{
	/* `outputDbtbl' is a copy of the GROUP BY's `q->in1' input table,
	 * but with its own FLD for the split column, eg. varchar type
	 * instead of strlst.  If not splitting, `outputDbtbl' is just
	 * `q->in1':
	 */
	DBTBL	*outputDbtbl;
	int	ownOutputDbtbl;			/* do we own `outputDbtbl' */
	int	splitFldIdx;			/* split fld # in "" */
	int	haveRow;			/* nonzero: read a row */
	void	*curItem;			/* current split item */
	size_t	curItemLen;			/* its length */
}
TXDEMUX;
#define TXDEMUXPN	((TXDEMUX *)NULL)

int	TXdemuxAddDemuxQnodeIfNeeded ARGS((QNODE *qnode));
TXDEMUX	*TXdemuxOpen ARGS((DBTBL *inputDbtbl, PROJ *splitProj,
                           DD *splitSchema));
TXDEMUX	*TXdemuxClose ARGS((TXDEMUX *demux));
#define	TXdemuxIsSplitter(demux)	((demux)->splitFldIdx >= 0)
int	TXdemuxReset ARGS((TXDEMUX *gi));
int	TXdemuxGetNextRow ARGS((TXDEMUX *demux, QNODE *inputQnode, FLDOP *fo));

int	TXgetMultiValueSplitFldIdx ARGS((PROJ *proj, DD *projDd, DD *tblDd));

int	TXqnodeRewindInput ARGS((QNODE *qnode));

/******************************************************************/

/* NOTE: user/system/real time must be adjacent and in that order;
 * assumed in vsysinfo.c:
 */
#define TXRESOURCESTAT_SYMBOLS_LIST     \
  I(UserTime)                           \
  I(SystemTime)                         \
  I(RealTime)                           \
  I(MaxVirtualMemorySize)               \
  I(MaxResidentSetSize)                 \
  I(IntegralSharedMemSize)              \
  I(IntegralUnsharedDataSize)           \
  I(IntegralUnsharedStackSize)          \
  I(MinorPageFaults)                    \
  I(MajorPageFaults)                    \
  I(Swaps)                              \
  I(BlockInputOps)                      \
  I(BlockOutputOps)                     \
  I(MessagesSent)                       \
  I(MessagesReceived)                   \
  I(SignalsReceived)                    \
  I(VoluntaryContextSwitches)           \
  I(InvoluntaryContextSwitches)

typedef enum TXRESOURCESTAT_tag
{
  TXRESOURCESTAT_UNKNOWN        = -1,           /* must be -1 and first */
#undef I
#define I(tok)  TXRESOURCESTAT_##tok,
  TXRESOURCESTAT_SYMBOLS_LIST
#undef I
  TXRESOURCESTAT_NUM                            /* must be last */
}
TXRESOURCESTAT;
#define TXRESOURCESTATPN        ((TXRESOURCESTAT *)NULL)

typedef struct TXRESOURCESTATS_tag
{
  double        values[TXRESOURCESTAT_NUM];
}
TXRESOURCESTATS;
#define TXRESOURCESTATSPN       ((TXRESOURCESTATS *)NULL)

/* update TXgetResourceStats() if this list changes: */
#define TXRUSAGE_SYMBOLS_LIST   \
  I(SELF)                       \
  I(CHILDREN)                   \
  I(BOTH)                       \
  I(THREAD)

typedef enum
{
  TXRUSAGE_UNKNOWN      = -1,                   /* must be -1 and first */
#undef I
#define I(tok)  TXRUSAGE_##tok,
  TXRUSAGE_SYMBOLS_LIST
#undef I
  TXRUSAGE_NUM                                  /* must be last */
}
TXRUSAGE;
#define TXRUSAGEPN      ((TXRUSAGE *)NULL)

TXRUSAGE TXstrToTxrusage(const char *s, size_t sLen);
TXRESOURCESTAT TXstrToTxresourcestat(const char *s, size_t sLen);
int     TXgetResourceStats(TXPMBUF *pmbuf, TXRUSAGE who,
                           TXRESOURCESTATS *stats);

int     TXsetProcessStartTime(void);

int	TXshowproctimeinfo ARGS((TXRESOURCESTATS *pinfo1,
                                 TXRESOURCESTATS *pinfo2));
int	TXshowmaxmemusage ARGS((void));
int     TXgetmeminfo ARGS((size_t mem[2]));
char   *TXo_flags2str ARGS((char *buf, size_t bufsz, int flags));

#ifndef _WIN32
#  if !defined(i386) || defined(__linux__) || defined(__bsdi__) || defined(__FreeBSD__)
#    define _environ      environ
#  endif
extern char     **_environ;
#endif /* !_WIN32 */

int	tx_setenv(const char *name, const char *value);
int     tx_unsetenv ARGS((CONST char *name));
char    **tx_mksafeenv ARGS((int how));

int     TXtruncateFile ARGS((TXPMBUF *pmbuf, CONST char *path, int fd,
                             EPI_OFF_T sz));

typedef struct TXFMODE_tag      /* file mode bits list element */
{
  char                  op;     /* '=', '+', '-' */
  char                  flags;  /* MODE_... bit flags */
  unsigned              mask;   /* Mask of affected bits */
  unsigned              value;  /* Bits to add/remove/set */
  struct TXFMODE_tag    *next;  /* Next guy in list */
}
TXFMODE;
#define TXFMODEPN       ((TXFMODE *)NULL)

#define MODE_MASK_EQUALS        1
#define MODE_MASK_PLUS          2
#define MODE_MASK_MINUS         4
#define MMALL   (MODE_MASK_EQUALS | MODE_MASK_PLUS | MODE_MASK_MINUS)
#define MODE_X_IF_ANY_X         01
#define MODE_COPY_EXISTING      02

TXFMODE *opentxfmode ARGS((CONST char *s, unsigned opmask));
TXFMODE *closetxfmode ARGS((TXFMODE *f));
unsigned txfmode_adjust ARGS((TXFMODE *f, unsigned mode));
void    txfmode_string ARGS((char *buf, unsigned mode, int forchmod));

#define TXSM_DEST       0x1
#define TXSM_LOCKED     0x2
#define TXSM_INIT       0x4

typedef struct TXSHMINFO_tag
{
  int   id, key;
  int   perms;          /* TXSM flags */
  int   nattach;        /* number attached */
  size_t sz;            /* size in bytes */
}
TXSHMINFO;
#define TXSHMINFOPN     ((TXSHMINFO *)NULL)

TXSHMINFO * TXgetshminfo ARGS((int key));
CONST char *TXgetExeFileName ARGS((void));
int         TXgetBooleanOrInt ARGS((TXPMBUF *pmbuf, CONST char *settingGroup,
   CONST char *settingName, CONST char *val, CONST char *valEnd, int isbool));
int TXgetwinsize ARGS((int *cols, int *rows));

typedef struct FASTBUF
{
	EPI_OFF_T off;
	FLD *fld;
}
FASTBUF;

typedef struct DBI_SEARCH
{
	IINDEX *iindex;
	void *fip;
	FLD *infld;
	char *fname;
	DBTBL *dbtbl;
	int nopre;
	EPI_HUGEUINT nhits;
	int nopost;
	int op;
	int inv;
	TBSPEC *tbspec;
	int imode;
}
DBI_SEARCH;

typedef struct EXTRA_tag
{
	DBTBL *dbtbl;
	PRED *goodpred;
	size_t extrasz;
	FLDCMP *fc;
	KEYREC *keyrec;
	FASTBUF *fbufinfo;
	fop_type cmpfunc;
	BTREE *btree;
	int	hasvarfld;
	int cachedkeyrec;	/* 1: `keyrec' from previous indexcache qry */
	int iscached;	/* 0: no  1: indexcache'ing  2: 1 & done w/it */
	PRED *origpred;/* Original Pred assigned.  Goodpred should be subset */
	int cachednopost;
	size_t keysz;
	PRED *computedorderby;
	int nosort;
	int	lonely;				/* i3dbfinit() copy */
	int	useMaxRows;			/* maxrows value to use */
	int	haveOrderByContainingRank;	/* but not ORDER BY num */
	int	haveOrderByNum;
}
EXTRA;

#define EXTRAPN ((EXTRA *)NULL)

EXTRA	*iextra ARGS((TBSPEC *, DD *, size_t, int *, DBTBL *, int));
EXTRA   *TXiextraForIndexCache(TBSPEC *tbspec, DD *auxdd, size_t auxsz,
		      int *inv, DBTBL *dbtbl,DDMMAPI *ddmmapi, char *fldname);

EXTRA	*closeextra ARGS((EXTRA *, int));
int	iextraok ARGS((EXTRA *exs, RECID recid, void *auxfld, size_t bufsz));

BTREE *TXsetupauxorder(EXTRA * exs, DD * auxdd, TBSPEC * tbspec,
		       DBTBL * dbtbl);

/******************************************************************/

char	*TXddicfname ARGS((DDIC *, char *));
TBL	*TXcreatetbl_dbf ARGS((DD *dd, DBF *df, DBF *bf));
DBTBL	*TXopentmpdbtbl_tbl ARGS((TBL *tbl, char *lname, char *rname, DDIC *ddic));
DBTBL	*TXopentmpdbtbl ARGS((char *fname, char *lname, char *rname, DD *dd, DDIC *ddic));
int     TXddgetsysmi ARGS((DDIC *ddic, char *index, EPI_OFF_T *threshold, time_t *wait));
int	TXinsertMetamorphCounterIndexRow(char *query, void *auxfld, BTLOC at,
					 WTIX *ix);

/******************************************************************/

#ifndef RECIDPN
#  define RECIDPN       ((RECID *)NULL)
#endif

int	TXaddindexrec ARGS((DDIC *, char *, char *, char *, int , int , char *, int, char *, RECID *recidp));
int	TXdelindexrec ARGS((DDIC *ddic, RECID recid));
int	TXdeleteSysindexEntry ARGS((DDIC *ddic, CONST char *tableName,
                                    CONST char *indexName, int type));
int	TXdroptable ARGS((char *, char *));
int	TXdropdtable ARGS((DDIC *, char *));
int	TXdropindex ARGS((char *, char *));
int	TXdropdindex ARGS((DDIC *, char *));
int	TXdroptrigger ARGS((DDIC *, char *));
int	TXdropuser ARGS((DDIC *, char *));
int	TXdelsyscols ARGS((DDIC *, char *));
int     createuser( DDIC *ddic, char *user, char *pass);
int     changeuser( DDIC *ddic, char *user, char *pass);

int	TXdestroydb ARGS((DDIC *));

/******************************************************************/

DDCACHE	*TXopencache ARGS((TXPMBUF *pmbuf));
DDCACHE *TXclosecache ARGS((DDCACHE *));
DBTBL	*TXgetcache ARGS((DDIC *, char *));
int	 TXputcache ARGS((DDIC *, DBTBL *));
int	 TXungetcache ARGS((DDIC *, DBTBL *));
int	 TXrmcache ARGS((DDIC *, char *, int *));
int     TXclosedummy ARGS((void));

extern int      TXtraceDdcache;
extern char     *TXtraceDdcacheTableName;

/******************************************************************/

/*	Prototypes for trigger operations.                        */

int	createtrigger ARGS((DDIC *, char *, char *, char *, char *, char *, char *, char *));
int	createtrigtbl ARGS((DDIC *));
int	opentrigger ARGS((DDIC *, DBTBL *));
TRIGGER *closetrigger ARGS((TRIGGER *));
int	trigexec ARGS((IIITRIGGER *, DBTBL *, FLDOP *));

int(*TXsetopendbfunc(int(*newfunc)(void *), void *usr, void **oldusr))(void *);
/******************************************************************/

typedef struct TX_EVENT
{
        time_t when;
	size_t count;
	struct TX_EVENT *next;
}
TX_EVENT;

TX_EVENT *TXvcal ARGS((char *rule, time_t start, time_t end, int skip, int maxocc, time_t min, time_t max, int exact));
TX_EVENT *TXcloseevent ARGS((TX_EVENT *event));
CONST char *tx_english2vcal ARGS((char *buf, size_t sz, CONST char *eng));

char	*TXaddscheduledevent ARGS((DDIC *ddic, TXMUTEX *ddicMutex, char *path,
              char *suffix, char *start, char *schedule, char *vars,
              char *comments, char *options, int pid, char *errbuf,
              size_t errbufsz, int verbose));
char	*TXdelscheduledevent ARGS((DDIC *ddic, char *path, char *suffix,
		char *schedule, char *vars, char *comments, char *options,
		int verbose));
int	TXrunscheduledevents ARGS((DDIC *ddic, TXMUTEX *ddicMutex,
                                   char *texis, int verb));
int     TXcreatesysschedule ARGS((DDIC *ddic));

int	TXgetMonitorServerUrl(char **url, int *runLevel,
			      int *hasScheduleService);

int	TXcreateLocksViaMonitor(TXPMBUF *pmbuf, const char *dbPath,
                                int timeout);

#define TX_SCHEDULE_PROTOCOL_VERSION	1

extern char     *TxSchedJobMutexName;
extern char     *TxSchedNewJobEventName;
extern double   TXschedJobMutexTimeoutSec;

/******************************************************************/

#ifndef CONFFILEPN
typedef struct CONFFILE_tag     CONFFILE;
#  define CONFFILEPN ((CONFFILE *)NULL)
#endif /* !CONFFILEPN */

char  *TXgetcharsetconv ARGS((void));
CONFFILE *closeconffile(CONFFILE *);
int     TXconfSetDocumentRootVar ARGS((CONFFILE *conf, CONST char *docRoot,
                                       int isExpanded));
int     TXconfSetServerRootVar ARGS((CONFFILE *conf, CONST char *serverRoot,
                                     int isExpanded));
int     TXconfSetScriptRootVar ARGS((CONFFILE *conf, CONST char *scriptRoot,
                                     int isExpanded));
CONFFILE *openconffile ARGS((char *filename, int yap));
char **TXgetConfStrings(TXPMBUF *pmbuf, CONFFILE *conffile,
			const char *sectionName, int sectionNum,
			const char *attrib, char *defval);
char *getconfstring(CONFFILE *conffile, CONST char *section,
                    CONST char *attrib, char *defval);
char *TXconfGetRawString ARGS((CONFFILE *conffile, CONST char *section,
                               CONST char *attrib, char *defval));
int getconfint(CONFFILE *conffile, CONST char *section, CONST char *attrib,
               int defval);
char *getnextconfstring(CONFFILE *conffile, CONST char *section,
                        CONST char **attrib, int i);
size_t  TXconfGetNumSections ARGS((CONFFILE *conf));
CONST char *TXconfGetSectionName ARGS((CONFFILE *conf, size_t sectionIdx));

#define TX_DEFSCHEDULEPORT              10005
#define TX_DEFSCHEDULEPORT_SECURE       10006
#define TX_DEFDISTRIBDBFPORT            10007
#define TX_DEFDISTRIBDBFPORT_SECURE     10008

#define TX_DEF_SCHEDULER_RUN_LEVEL	0x01
/* Note that TX_DEF_SCHEDULER_SCHEDULER_ENABLED value must agree
 * with whether `schedule' is in TX_DEF_SCHEDULER_SERVICES *and*
 * whether 0x1 is set in TX_DEF_SCHEDULER_RUN_LEVEL:
 */
#ifdef EPI_MONITOR_SERVER_LICENSE_INFO
#  define TX_DEF_SCHEDULER_SERVICES     "schedule createlocks licenseinfo"
#else /* !EPI_MONITOR_SERVER_LICENSE_INFO */
#  define TX_DEF_SCHEDULER_SERVICES	"schedule createlocks"
#endif /* !EPI_MONITOR_SERVER_LICENSE_INFO */
#define TX_DEF_SCHEDULER_SCHEDULER_ENABLED      1
#define TX_DEF_SCHEDULER_VERBOSE        0

typedef struct TX_READ_TOKEN {
	char	*zztext;
	char	*inbuf;
	unsigned bufend;
	unsigned curpos;
	char	 restore;
	char	 c;
} TX_READ_TOKEN ;

#ifdef OBJECT_READTOKEN
QTOKEN	 readtoken ARGS((TX_READ_TOKEN *));
QNODE	*readnode ARGS((DDIC *, FLDOP *, TX_READ_TOKEN *, int));
TX_READ_TOKEN *setparsestring ARGS((TX_READ_TOKEN *, char *));
#else /* !OBJECT_READTOKEN */
QTOKEN	 readtoken ARGS((void));
QNODE	*readnode ARGS((DDIC *, FLDOP *, int));
#endif /* !OBJECT_READTOKEN */

/* tracesqlparse bits:
 * 0x0001	SQL string set
 *   these two unimplemented: need to make tracesqlparse map into
 *   YYDEBUG/yydebug in sql1.c (i.e. modify yacc output during build):
 * 0x0002	TBD: Lex tokens read by yacc during sqlconvert()
 * 0x0004	TBD: Reduce/shift/etc. rules from yacc during sqlconvert()
 * 0x0010	SQL token string set
 * 0x0020	SQL tokens read
 */
extern int	TXtraceSqlParse;

extern CONFFILE *TxConf;        /* in texglob.c */
extern char     TxConfFile[];   /* ""; full path to actual open conf file */
#define TxConfFile_SZ   PATH_MAX

extern int      TxOrgArgc;			/* before mods */
extern char     **TxOrgArgv;			/* before mods */
extern int      TxGlobalOptsArgc;
extern char     **TxGlobalOptsArgv;
extern char     *TxExeFileName;

extern int      TxTracePipe;    /* documented */
/* 0x00000001: after open()/close()/TXcreatethread()/LogonUser()/etc.
 * 0x00000002: after select()/WaitForMultipleObjects()
 * 0x00000004: after read()
 * 0x00000008: after write()
 * 0x00000010: after Set/ResetEvent()
 * 0x00000020: after thread run (at soft exit in thread)
 * 0x00000040: after read() data
 * 0x00000080: after write() data
 * 0x00000100:
 * 0x00000200:
 * 0x00000400:
 * 0x00000800:
 * 0x00001000:
 * 0x00002000:
 * 0x00004000:
 * 0x00008000:
 * 0x00010000: before open()/close()/TXcreatethread()/LogonUser()/etc.
 * 0x00020000: before select()/WaitForMultipleObjects()
 * 0x00040000: before read()
 * 0x00080000: before write()
 * 0x00100000: before Set/ResetEvent()
 * 0x00200000: before thread run (at start in thread)
 * 0x00400000: before read() data
 * 0x00800000: before write() data
 */
#define TPF_OPEN        0x0001
#define TPF_SELECT      0x0002
#define TPF_READ        0x0004
#define TPF_WRITE       0x0008
#define TPF_EVENT       0x0010
#define TPF_THREAD_RUN  0x0020
#define TPF_READ_DATA   0x0040
#define TPF_WRITE_DATA  0x0080
#define TPF_BEFORE(bits)        ((bits) << 16)
#define TPF_MSG_READ_DATA       4
#define TPF_MSG_WRITE_DATA      8
#define TRACEPIPE_BEFORE_START(bits)                    \
  if (TxTracePipe & ((bits) | TPF_BEFORE(bits)))        \
    {
#define TRACEPIPE_BEFORE_END()						\
      tracePipeStart = TXgetTimeContinuousFixedRateOrOfDay();		\
    }
#define TRACEPIPE_AFTER_START(bits)     				\
  if (TxTracePipe & (bits))             				\
    {                                   				\
      TX_PUSHERROR();							\
      tracePipeFinish = TXgetTimeContinuousFixedRateOrOfDay();		\
      tracePipeTime = tracePipeFinish - tracePipeStart;			\
      /* Avoid floating-point roundoff to negative: */			\
      if (tracePipeTime < 0.0 && tracePipeTime > -0.001)		\
	tracePipeTime = 0.0;
#define TRACEPIPE_AFTER_END()   TX_POPERROR(); }
#define TRACEPIPE_VARS          double tracePipeStart = -1.0, \
  tracePipeFinish = -1.0, tracePipeTime = -1.0
#define TRACEPIPE_TIME()        tracePipeTime

/******************************************************************/

extern int      TxTraceLib;
/* 0x0001:  TXlib_expandpath() calls
 * 0x0002:  TXopenlib() calls
 * 0x0004:  TXlib_getaddr() calls
 */

/******************************************************************/

extern int              TXtraceKdbf;
extern char             *TXtraceKdbfFile;
extern TXPMBUF          *TXtraceKdbfPmbuf;

/******************************************************************/

#ifdef _WIN32
typedef void (TXTERMFUNC) ARGS((void));
#  define TXTERMFUNCPN    ((TXTERMFUNC *)NULL)

void TXgenericTerminateEventHandler ARGS((void));
TXTERMFUNC *TXsetTerminateEventHandler ARGS((TXTERMFUNC *func));
BOOL WINAPI TXgenericConsoleCtrlHandler(DWORD type);
int CDECL TXgenericExceptionHandler(int sigNum,
	  TX_SIG_HANDLER_SIGINFO_TYPE *info /* struct exception_pointers */);
#endif /* _WIN32 */

/******************************************************************/

/* see texglob.c: */
extern int      TXtracedumptable;
extern double   TXwriteTimeout;

PID_T   TXddicGetDbMonitorPid(DDIC *ddic);
int     TXdblockGetNumAttachedServers(DBLOCK *dblock);
int     TXddicGetNumAttachedServers(DDIC *ddic);
int	TXrmlocks  ARGS((const char *db, int forceremoval, int verbose));

#ifdef _WIN32
#  ifndef TX_SYSDEP_C
#    undef  CreateEvent
#    define CreateEvent(a,b,c,d)	DONT_USE
#    undef  OpenEvent
#    define OpenEvent(a,b,c)		DONT_USE
#    undef  CreateNamedPipe
#    define CreateNamedPipe(a,b,c,d)	DONT_USE
#    undef  CreateMutex
#    define CreateMutex(a,b,c,d)	DONT_USE
#    undef  OpenMutex
#    define OpenMutex(a,b,c,d)		DONT_USE
#    undef  CreateFileMapping
#    define CreateFileMapping(a,b,c,d)	DONT_USE
#    undef  OpenFileMapping
#    define OpenFileMapping(a,b,c,d)	DONT_USE
#  endif /* !TX_SYSDEP_C */

HANDLE	TXCreateEvent(char *name, int ManualReset, int yap);
HANDLE	TXOpenEvent(DWORD Access, BOOL Inherit, char *name, int yap);
HANDLE	TXCreateMutex(TXPMBUF *pmbuf, char *name);
HANDLE	TXOpenMutex(TXPMBUF *pmbuf, char *name);
HANDLE	TXCreateFileMapping(HANDLE hFile, DWORD flProtect, DWORD MaxSizeHi, DWORD MaxSizeLo, char *name, int yap);
HANDLE	TXOpenFileMapping(char *name, int yap);
HANDLE	TXCreateNamedPipe(char *name, int yap);
#endif /* _WIN32 */

int	TXresetparams ARGS((LPSTMT));
char	**TXcreateargv ARGS((char *, int *));

/******************************************************************/

typedef struct TXCPDB_CONN {
	int	keepgoing;
	int	verbose;
	int	version;
	int	isSingleThreaded;
	int	port;				/* server port to listen on */
	TXsockaddr	network;	/* network to accept for */
	int		netbits;		/* network # of bits */
	int	fd;
	char	*fdDesc;			/* alloced */
	int	verbmsg;
	EPI_OFF_T	rowlimit;
	int	arraysize;
	char	*okdb;				/* ok-db (may end in `*') */
	size_t	okdblen;			/* strlen(okdb) */
	int	okdbisprefix;			/* `okdb' is prefix */
	PID_T	serverpid;			/*pid to kill for server xit*/
	DDIC	*ddic;
	FLDOP	*fo;
	char	readbuf[10000];
	char	command[10000];
} TXCPDB_CONN;
#define TXCPDB_CONNPN	((TXCPDB_CONN *)NULL)

/******************************************************************/

typedef struct TX_LINK_INFO {
	char *name;
	char *host;
	char *db;
	TXCPDB_CONN	*conn;
} TX_LINK_INFO, *PTX_LINK_INFO;


int	TXcreatelink(DDIC *ddic, char *name, char *user, char *connect);
int	TXdroplink(DDIC *ddic, char *name);

/******************************************************************/

typedef struct OCACHEITEM {
	void	*obj;
	int	inuse;
	int	lastuse;
	struct OCACHEITEM *next;
	struct OCACHEITEM *prev;
} OCACHEITEM;

/******************************************************************/

typedef struct OCACHE {
	OCACHEITEM	*head;
	OCACHEITEM	*tail;
	void *(*close_obj)(void *);
	int   (*cmpf)(void *, void *);
	int maxhold;
} OCACHE;

/******************************************************************/

#if !defined(TX_DEBUG) && !defined(TX_NO_HIDE_LIC_SYM)
#  define stxalcrtbl txx_abash
#  define stxalcrndx txx_flog
#  define stxalcrtrig txx_scoff
#  define stxaldelete txx_stair
#  define stxalupdate txx_amortize
#  define stxalinsert txx_cons
#  define stxalselect txx_stretch
#  define stxalgrant txx_reserve
#  define stxalrevoke txx_door
#  define stxalflags txx_therm
#endif /* TX_DEBUG || TX_NO_HIDE_LIC_SYM */

int stxalcrtbl ARGS((int truefalse));
int stxalcrndx ARGS((int truefalse));
int stxalcrtrig ARGS((int truefalse));
int stxaldelete ARGS((int truefalse));
int stxalupdate ARGS((int truefalse));
int stxalinsert ARGS((int truefalse));
int stxalselect ARGS((int truefalse));
int stxalgrant ARGS((int truefalse));
int stxalrevoke ARGS((int truefalse));
int stxalflags ARGS((int flags));

/******************************************************************/

int	TXconverttbl(char *fname, int nbits);

/******************************************************************/
/* For FTN_INTERNAL, what are the types */

#define TX_INTERNAL_MATCHES	1

/******************************************************************/

typedef struct TXPERMS_tag
{
	int	state;		/* The state of it */
	int	unixperms;	/* Just use unix perms */
	int	texuid; 	/* Texis user id (validated) */
	int	texgid; 	/* Texis group id (validated) */
	char	uname[20];	/* User name */
	int	texsuid;	/* Stored user id */
	int	texsgid;	/* Stored group id */
        char	suname[20];	/* Stored user name */
	int	pushcnt;	/* Depth of TXpushid() "stack" */
}
TXPERMS;
#define TXPERMSPN       ((TXPERMS *)NULL)

#define	TX_PUBLIC_UID	9999
#define TX_SYSTEM_UID	0

#define	TX_PM_UNSET	0
#define TX_PM_FAILED	1
#define TX_PM_SUCCESS	2


int permslogoff(DDIC *ddic);
int TXsetfairlock(int n);
size_t TXsetblockmax(size_t bm);
int TXsetinfinitythreshold(int t);
int TXsetinfinitypercent(int t);
TXbool TXsetlockverbose(int n);
int TXgetlockverbose(void);
#define TX_LOCKVERBOSELEVEL()	(TXgetlockverbose() & 0xf)
const char *TXlockTypeDescription(int lockType);
int tx_loglockop ARGS((CONST DDIC *ddic, CONST char *act, int ltype,
    VOLATILE LTABLE *t, DBTBL *dbtbl, VOLATILE ft_counter *a,
    VOLATILE ft_counter *b, int *rc));
int TXqnodeCountNames ARGS((QNODE *qtree));
int TXpredopttype(int a);
int TXsetramblocks(int n);
int TXsetramsize(int n);
int TXresetexpressions(void);
PROXBTREE *TXcloseproxbtree(PROXBTREE *bt);
int TXinitenumtables(DDIC *ddic);
int TXenumtables(DDIC *ddic, char *name, char *creator);
void TXrewinddbtblifnoindex(DBTBL *db);
int TXsystem(CONST char *cmd);

FTN TXsqlFuncLookup_GetReturnType(FTN keysType, size_t keysLen,
				  FTN binRangesType, size_t binRangesLen,
				  FTN binNamesType, size_t binNamesLen);

int mminfo(FLD *queryFld, FLD *dataFld, FLD *numHitsFld, FLD *unusedFld,
	   FLD *msgsFld);
int TXclosemminfo(void);

/* ------------------------------------------------------------------------ */

int TXsqlFuncs_abstract(FLD *f1, FLD *f2, FLD *f3, FLD *f4, FLD *f5);
int TXacos(FLD *f1);
int TXasin(FLD *f1);
int TXatan(FLD *f1);
int TXatan2(FLD *f1, FLD *f2);
int TXcos(FLD *f1);
int TXsin(FLD *f1);
int TXtan(FLD *f1);
int TXcosh(FLD *f1);
int TXsinh(FLD *f1);
int TXtanh(FLD *f1);
int TXexp(FLD *f1);
int TXlog(FLD *f1);
int TXlog10(FLD *f1);
int TXsqrt(FLD *f1);
int TXceil(FLD *f1);
int TXfabs(FLD *f1);
int TXfloor(FLD *f1);
int TXfmod(FLD *f1, FLD *f2);
int TXpow(FLD *f1, FLD *f2);
int TXsqlFunc_basename(FLD *f1);
int TXfld_canonpath(FLD *f1, FLD *f2);
int TXsqlFunc_dirname(FLD *f1);
int TXsqlFunc_fileext(FLD *f1);
int TXsqlFunc_fromfile(FLD *f1, FLD *f2, FLD *f3);
int TXsqlFunc_fromfiletext(FLD *f1, FLD *f2, FLD *f3);
int TXsqlFunc_ifNull(FLD *testFld, FLD *valIfNullFld);
int TXsqlFunc_initcap(FLD *f1, FLD *f2);
int TXsqlFunc_isNaN(FLD *f1);
int TXsqlFunc_isNull(FLD *testFld);
int TXsqlFunc_joinpath(FLD *f1, FLD *f2, FLD *f3, FLD *f4, FLD *f5);
int TXsqlFunc_joinpathabsolute(FLD *f1, FLD *f2, FLD *f3, FLD *f4, FLD *f5);
int TXsqlFunc_keywords(FLD *f1, FLD *f2);
int TXsqlFunc_length(FLD *f1, FLD *f2);
int TXsqlFunc_lookup(FLD *keysFld, FLD *rangesFld, FLD *namesFld);
int TXsqlFunc_lookupCanonicalizeRanges(FLD *rangesFld, FLD *keyTypeFld);
int TXsqlFunc_lookupParseRange(FLD *rangeFld, FLD *partsFld);
int TXsqlFunc_lower(FLD *f1, FLD *f2);
int TXsqlFunc_upper(FLD *f1, FLD *f2);
int TXsqlFunc_pathcmp(FLD *f1, FLD *f2);
int TXsqlFunc_random(FLD *f1, FLD *f2);
int TXsqlFunc_sandr(FLD *f1, FLD *f2, FLD *f3);
int TXsqlFunc_separator(FLD *f1);
int TXsqlFunc_seq(FLD *f1, FLD *f2);
int TXsqlFunc_strtol(FLD *f1, FLD *f2);
int TXsqlFunc_strtoul(FLD *f1, FLD *f2);
int TXsqlFunc_text2mm(FLD *f1, FLD *f2);
int TXsqlFunc_totext(FLD *f1, FLD *f2);
int TXsqlFunc_hasFeature(FLD *featureFld);
extern char 	*TXFeatures[];

/********************************* lang.c ***********************************/

size_t TXgetbigramcounts ARGS((CONST char *buf, size_t sz, int firstuni,
                               int lastuni, int igncase, size_t **countsp));
double TXcomputebigramislang ARGS((CONST double *exparr,
                                   CONST size_t *actcounts, size_t actbigrams,
                                   int firstuni, int lastuni));
double TXcomputedictwordscore ARGS((CONST char *buf, size_t sz,
         CONST char *equivfile, CONST char *altequivfile, int igncase,
         TXPMBUF *pmbuf));

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define TXNGRAM_MAX_SZ  4

/* NOTE: if TXNGRAM changes see genNgrams.c printing: */
typedef struct TXNGRAM_tag
{
  size_t        count;                          /* #times this ngram occurs */
  char          text[TXNGRAM_MAX_SZ];           /* TXNGRAMSET.ngramSz length*/
}
TXNGRAM;
#define TXNGRAMPN       ((TXNGRAM *)NULL)

/* NOTE: if TXNGRAMSET changes see genNgrams.c printing: */
typedef struct TXNGRAMSET_tag
{
  TXPMBUF       *pmbuf;
  size_t        ngramSz;                        /* text bytes per N-gram */
  /* N-grams are either in sorted array `ngrams'/`numGrams' (e.g. if const),
   * or B-tree `btree' (e.g. during construction):
   */
  TXNGRAM       *ngrams;
  size_t        numNgrams;
  BTREE         *btree;
  double        sqrtSumCountsSquared;   /* sqrt(sum of ngrams[i].count^2) */
}
TXNGRAMSET;
#define TXNGRAMSETPN    ((TXNGRAMSET *)NULL)

typedef struct TXNGRAMSETLANG_tag
{
  const TXNGRAMSET      *ngramset;
  char                  langCode[4];            /* ISO 639-1 language code */
}
TXNGRAMSETLANG;
#define TXNGRAMSETLANGPN        ((TXNGRAMSETLANG *)NULL)

extern const TXNGRAMSETLANG   TXngramsetlangs[];

TXNGRAMSET *TXngramsetOpen(TXPMBUF *pmbuf, size_t ngramSz);
TXNGRAMSET *TXngramsetClose(TXNGRAMSET *ngramset);
TXNGRAMSET *TXngramsetOpenFromFile(TXPMBUF *pmbuf, int ngramSz,
                                   const char *path);
int     TXngramsetPrepText(TXNGRAMSET *ngramset, char **destText,
                   size_t *destTextSz, const char *srcText, size_t srcTextSz);
int     TXngramsetAddNgramsFromText(TXNGRAMSET *ngramset, const char *text,
                                    size_t textSz);
int     TXngramsetFinish(TXNGRAMSET *ngramset);
double  TXngramsetCosineDistance(const TXNGRAMSET *ngramsetA,
                                 const TXNGRAMSET *ngramsetB);
double  TXngramsetIdentifyLanguage(TXPMBUF *pmbuf,
                 const TXNGRAMSETLANG *ngramsetlangs,
                 const char *text, size_t textSz, TXNGRAMSETLANG **langIdent);
int     TXsqlFuncIdentifylanguage(FLD *textFld, FLD *langFld,
                                  FLD *sampleSizeFld);

/* 16KB is small enough for slow machines (1GHz Pentium III) to run in <0.2s,
 * and large enough to still reliably identify text (assuming it is
 * representative of the entire document):
 */
#define TX_LANG_IDENT_BUF_SZ    16384

/******************************************************************/

#define TX_METAMORPH_STRLST_SYMBOLS_LIST	\
I(allwords)					\
I(anywords)					\
I(allphrases)					\
I(anyphrases)					\
I(equivlist)

typedef enum TXMSM_tag				/* Metamorph Strlst Mode */
{
	TXMSM_UNKNOWN = -1,			/* must be first */
#undef I
#define I(tok)	TXMSM_##tok,
TX_METAMORPH_STRLST_SYMBOLS_LIST
#undef I
	TXMSM_NUM				/* must be last */
}
TXMSM;
#define TXMSMPN	((TXMSM *)NULL)

typedef enum TXVSSEP_tag			/* varcharToStrlstSep */
{
	TXVSSEP_UNKNOWN		= -3,		/* invalid/unknown */
	TXVSSEP_LASTCHAR	= -2,		/* use last char in string */
	TXVSSEP_CREATE		= -1,		/* create sep: str is 1 val */
	/* 0-255 indicate a specific char: */
	TXVSSEP_MAX		= 255		/* placeholder */
}
TXVSSEP;
#define TXVSSEPPN	((TXVSSEP *)NULL)

typedef enum TXoncePerSqlMsg_tag
{
	TXoncePerSqlMsg_TupIndexPredEvalWrongType,
	TXoncePerSqlMsg_LookupRangesNamesNumDiffer,
	TXoncePerSqlMsg_LookupFailed,
	TXoncePerSqlMsg_NUM			/* must be last */
}
TXoncePerSqlMsg;

typedef enum TX_BETA_FEATURES
{
 BETA_JSON
,BETA_COUNT /* KEEP_AS_LAST -- must be last item in enum */
} TX_BETA_FEATURES;

typedef enum strlst2charmode
{
  TXs2c_trailing_delimiter,
  TXs2c_json_array
} strlst2charmode;
typedef enum char2strlstmode
{
  TXc2s_unspecified = 0,
  TXc2s_create_delimiter = 1,
  TXc2s_defined_delimiter = 2,
  TXc2s_trailing_delimiter= 4,
  TXc2s_json_array = 8,
  TXc2s_json_lax = 16,
  TXc2s_json_array_lax = 24 /* 8 (json_array) + 16 (lax) */
} char2strlstmode;
#define CLEAR_C2S_DELIMITERS(a) ((a &= ~(TXc2s_create_delimiter|TXc2s_defined_delimiter|TXc2s_trailing_delimiter)))
#define CLEAR_C2S_JSON(a) ((a &= ~(TXc2s_json_array|TXc2s_json_lax)))
typedef struct TXstrlstCharConfig
{
  strlst2charmode fromStrlst;
  char2strlstmode toStrlst;
  char delimiter;
} TXstrlstCharConfig;

int	TXstrToTxvssep(TXPMBUF *pmbuf, const char *settingName, const char *s, const char *e, TXstrlstCharConfig *res);
#include "txlicfunc.h"

/* NOTE: TXAPP is (supposed to be) a per-thread struct; there are
 * no mutexes e.g. on TXgetGetFldop():
 */
struct TXAPP_tag
{
	int	LogBadSYSLOCKS;
	TXMSM	metamorphStrlstMode;		/* how to convert strlst */
  TXstrlstCharConfig charStrlstConfig;
  TXstrlstCharConfig charStrlstConfigFromIni;
	TXVSSEP	xvarcharToStrlstSepFromTexisIni;	/* from texis.ini */
	TXVSSEP	xvarcharToStrlstSep;		/* current setting */
  int xvarcharToStrlstFromJsonArray; /**< Check for JSON Array syntax */
  int xvarcharFromStrlstToJsonArray; /**< Convert to JSON Array syntax */
	TXindexValues	indexValues;		/* Bug 4070 */
	TXCREATELOCKSMETHOD	createLocksMethods[TXCREATELOCKSMETHOD_NUM];
	int	createLocksTimeout;
	byte	vortexAddTrailingSlash;		/* <addtrailingslash> def. */
	byte	multiValueToMultiRow;		/* split strlst on GROUP BY */
	byte	texisDefaultsWarning;		/* warn if texisdefaults set*/
	byte	unalignedBufferWarning;		/* fbuftofld() warning */
	byte	createDbOkDirExists;
  byte  NoMonitorStart; /* Don't try and start monitor */
	TXtrap	trap;				/* for all but Vortex */

	byte	allowRamTableBlob;		/* RAM table blob ok Bug 4031*/
#define TX_ALLOW_RAM_TABLE_BLOB_DEFAULT	0

	/* `ORDER BY $rank/<rankExpression>' orders as per Texis Version 7
	 * and earlier when `legacyVersion7OrderByRank' is nonzero:
	 * numerically descending (usually; varies with MM vs MM-aux index,
	 * <rankExpression> vs. $rank etc.), and use negative ranks
	 * internally (which can sometimes flummox `ORDER BY $rank + posFld').
	 * For legacy scripts; may also enable related legacy bugs.
	 * Bug 6843.  When 0, orders like any other `ORDER BY':
	 * numerically descending iff DESC, ascending otherwise.
	 */
	byte	legacyVersion7OrderByRank;
#define TX_LEGACY_VERSION_7_ORDER_BY_RANK_DEFAULT(app)		\
	(!TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST((app), 8))

	byte	sqlModEnabled;			/* SQL `%' enabled */
	/* always on in version 8+ */

	TXbool	restoreStdioInheritance;	/* Windows exec() issue */

	/* `ipv6Enabled' is always on in version 8+; setting is temp: */
	TXbool	ipv6Enabled;			/* support IPv6 */
#ifdef EPI_ENABLE_IPv6
#  define TX_IPv6_ENABLED(app)	1
#else /* !EPI_ENABLE_IPv6 */
#  define TX_IPv6_ENABLED(app)	\
	((app) ? (app)->ipv6Enabled : (getenv("EPI_ENABLE_IPv6") != (char *)NULL))
#endif /* !EPI_ENABLE_IPv6 */

	/* `pwEncryptMethodsEnabled' always on in 8+; setting is temp: */
	TXbool	pwEncryptMethodsEnabled;
#ifdef EPI_ENABLE_PWENCRYPT_METHODS
#  define TX_PWENCRYPT_METHODS_ENABLED(app)	1
#else /* !EPI_ENABLE_PWENCRYPT_METHODS */
#  define TX_PWENCRYPT_METHODS_ENABLED(app)		\
	((app) ? (app)->pwEncryptMethodsEnabled : 	\
	 (getenv("EPI_ENABLE_PWENCRYPT_METHODS") != NULL))
#endif /* !EPI_ENABLE_PWENCRYPT_METHODS */

	TXpwEncryptMethod	defaultPasswordEncryptionMethod;
#define TXpwEncryptMethod_BUILTIN_DEFAULT(app)				\
	(TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST((app), 8) ||	\
	TX_PWENCRYPT_METHODS_ENABLED(app) ? TXpwEncryptMethod_SHA512 :	\
	 TXpwEncryptMethod_DES)
#define TXpwEncryptMethod_CURRENT(app)					     \
	((app) && (app)->defaultPasswordEncryptionMethod !=		     \
	TXpwEncryptMethod_Unknown ? (app)->defaultPasswordEncryptionMethod : \
	 TXpwEncryptMethod_BUILTIN_DEFAULT(app))

	int			defaultPasswordEncryptionRounds;

	TXbool	legacyVersion7UrlCoding;
#define TX_LEGACY_VERSION_7_URL_CODING_DEFAULT(app)		\
	(TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST((app), 8) ?	\
	 TXbool_False : TXbool_True)
#define TX_LEGACY_VERSION_7_URL_CODING_CURRENT(app)		\
	((app) ? (app)->legacyVersion7UrlCoding : 		\
	 TX_LEGACY_VERSION_7_URL_CODING_DEFAULT(app))

	TXbool	metaCookiesDefault;
#define TX_METACOOKIES_DEFAULT_DEFAULT(app)				\
	(!TX_COMPATIBILITY_VERSION_MAJOR_IS_AT_LEAST((app), 8))

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 * These features are true by default (unless otherwise noted)
	 * in Texis version 7+.  Note that other existing features may
	 * have changed defaults in version 7+:
	 */
	byte	inModeIsSubset;			/* inmode='subset' Bug 4065 */
	byte	subsetIntersectEnabled;		/* SUBSET/INTERSECT enabled */
	/* `strlstRelopVarcharPromoteViaCreate': for varchar RELOP
	 * strlst, use TXVSSEP_CREATE (not TXVSSEP_LASTCHAR) to create
	 * a strlst from `varchar' before performing RELOP, and use
	 * strlst RELOP strlst behavior; this is so $param RELOP
	 * strlstCol is consistent whether arrayconvert makes $param a
	 * varchar or strlst:
	 */
	byte	strlstRelopVarcharPromoteViaCreate;
	byte	useStringcomparemodeForStrlst;	/* Bug 3677 */
	byte    deDupMultiItemResults;		/* Bug 4064 */
	/* hexifyBytes is true in version 6-; mostly false in version 7+: */
	byte	hexifyBytes;			/* convert() hexifies bytes */
#define TX_PRE_LOAD_BLOBS_DEFAULT	0
	byte	preLoadBlobs;			/* read blobs ASAP */

	/* major/minor Texis version we are attempting to be
	 * compatible with; defaults to TX_VERSION_{MAJOR,MINOR}:
	 */
	int	compatibilityVersionMajor;
	int	compatibilityVersionMinor;
	/* `setCompatibilityVersionFailed': nonzero if most recent call to
	 * TXAppSetCompatibilityVersion() failed:
	 */
	byte	setCompatibilityVersionFailed;
	/* SQL failifincompatible property value: */
	byte	failIfIncompatible;
	/* end version 7 settings - - - - - - - - - - - - - - - - - - - - - */

	int	traceLicense;			/* tracelicense bit flags */
	/* Undocumented [Texis] Trace License bit flags:
	 * 0x0001: schema checks
	 * 0x0002: schema check table details
	 * 0x0004: schema check field details
	 * 0x0008: TXlic_init() start/finish if not init'd
	 * 0x0010: TXlic_init() start/finish if init'd
	 * 0x0020: license segment open/create
	 * 0x0040: license event open/create
	 * 0x0080: license monitor exec/start
	 * 0x0100: stats received summary (table size/rows etc.)
	 * 0x0200: stats received all
	 * 0x0400: stats scan (after receive) summary
	 * 0x0800: stats scan (after receive) all
	 * 0x1000: texis -license requests sent to Texis Monitor
	 * 0x2000: AWS product code verification details
	 * See also [Monitor] Verbose 0x08 and 0x10 for stats sender messages,
	 * [Scheduler] Verbose 0x20 for texis -license requests received.
	 */
	int	traceMutex;
	/* TXtraceMutex (undocumented):
	 * 0x00001: Save timestamp of calls
	 * 0x00002: Report recursive locks obtained (always on)
	 * 0x00004: Report TXlockmutex() timeouts
	 */
	char *blobCompressExe;
	char *blobUncompressExe;

	char	*traceLocksDatabase;		/* NULL if unset */
	char	*traceLocksTable;		/* NULL if unset */
	/* traceRowTables, traceRowFields: parsed-out alloced parallel
	 * NULL-terminated arrays of tables and fields to print at
	 * getdbtblrow().  Set with CSV value to `tracerowfields':
	 */
	char	**traceRowFieldsTables;
	char	**traceRowFieldsFields;

	int	validateBtrees;			/* sanity-check B-trees */
	/* 0x0001:    validate tree on open
	 * 0x0002:    validate page on read
	 * 0x0004:    validate page on write
	 * 0x0008:    validate page on release
	 * 0x0010:    other page-release errors
	 * 0x0020:  P more stringent limits
	 * 0x0040:  P validate on page manipulation
	 * 0x1000:    try to fix bad pages
	 * 0x2000:  P overwrite freed pages in mem
	 * P: may be performance hit
	 */
#if defined(TX_DEBUG) || defined(MEMDEBUG)
#  define TX_VALIDATE_BTREES_DEFAULT	0xffff
#else
#  define TX_VALIDATE_BTREES_DEFAULT	0x101f
#endif

#define TX_FLDOP_CACHE_MAX_SZ	16
	FLDOP	*fldopCache[TX_FLDOP_CACHE_MAX_SZ];	/* unused FLDOPs */
	size_t	fldopCacheSz;

	char	*logDir;	/* currently just for cores; wtf expand use */
	byte	didOncePerSqlMsg[TXoncePerSqlMsg_NUM];
  int     betafeatures[BETA_COUNT];  /* Beta Features */
  TXPUTMSGFLAGS putmsgFlags;
  TX_LICENSE_FUNCTIONS	*txLicFuncs;
};

extern TXAPP *TXApp;

TXbool TXparseTexisVersion(const char *texisVersion,
			   const char *texisVersionEnd, int earliestMajor,
			   int earliestMinor, int latestMajor,
			   int latestMinor, int *versionMajor,
			   int *versionMinor, char *errBuf, size_t errBufSz);
TXbool TXAppSetCompatibilityVersion(TXAPP *app, const char *texisVersion,
				    char *errBuf, size_t errBufSz);
size_t TXAppGetCompatibilityVersion(TXAPP *app, char *buf, size_t bufSz);
int TXAppSetTraceRowFields(TXPMBUF *pmbuf, TXAPP *app,
			   const char *traceRowFields);
int TXAppSetLogDir(TXPMBUF *pmbuf, TXAPP *app, const char *logDir,
		   size_t logDirSz);
TXbool	TXAppSetDefaultPasswordEncryptionMethod(TXPMBUF *pmbuf, TXAPP *app,
						TXpwEncryptMethod method);
TXbool TXAppSetDefaultPasswordEncryptionRounds(TXPMBUF *pmbuf, TXAPP *app,
					       int rounds);

/* wtf also see prototype in ncgsvr.c: */
int TXinitapp(TXPMBUF *pmbuf, const char *progName, int argc, char **argv,
	      int *argcStripped, char ***argvStripped);
void TXcloseapp(void);
int  TXsetProcessDescriptionPrefixFromPath(TXPMBUF *pmbuf, const char *path);
int  TXsetargv(TXPMBUF *pmbuf, int argc, char **argv);
void TXsetglobaloptsargv ARGS((TXPMBUF *pmbuf, int argc, char **argv));
size_t TXgetMaxProcessDescriptionLen(TXPMBUF *pmbuf);
const char *TXgetProcessDescription(void);
int TXsetProcessDescription(TXPMBUF *pmbuf, const char *msg);
TXMSM TXstrToTxmsm ARGS((CONST char *s));
CONST char *TXmsmToStr ARGS((TXMSM msm));
char *TXfldToMetamorphQuery ARGS((FLD *fld));

int TXdumpPred ARGS((HTBUF *buf, PRED *pred, int depth));
int TXdumpqnode ARGS((HTBUF *buf, DDIC *ddic, QNODE *query,
                      QNODE *parentquery, FLDOP *fo, int depth));
char *TXqnodeOpToStr ARGS((QNODE_OP op, char *buf, size_t bufSz));
char *TXqnodeListGetItem ARGS((QNODE *query, int idx));
QNODE *TXqnodeListGetSubList ARGS((QNODE *query, int idx));

extern CONST char       TxPrefStrlstDelims[256];

/******************************************************************/

typedef struct IPREPTREEINFO {
	DDIC	*ddic;
	FLDOP	*fo;
	int	preq;	/* Pre-requisite permissions.  Add to this as needed */
	int	prepq;
	DBTBL	*dbtbl;
	int	allowbubble;
	int	analyze;
	int	countonly; /* Do we only want a count, or anything else */
	int	stmthits;  /* How many license hits this statement */
} IPREPTREEINFO;

/******************************************************************/

DBTBL *ipreparetree ARGS((IPREPTREEINFO *, QNODE *, QNODE *, int *));

DBTBL * TXnode_join_prep(IPREPTREEINFO *, QNODE *, QNODE *, int *);
DBTBL * TXnode_rename_prep(IPREPTREEINFO *, QNODE *, QNODE *, int *);
DBTBL * TXnode_table_prep(IPREPTREEINFO *, QNODE *, QNODE *, int *);
DBTBL * TXnode_hint_prep(IPREPTREEINFO *, QNODE *, QNODE *, int *);

PROJ *TXddToProj(DBTBL *dbtbl);

int 	TXnode_hint_exec(QNODE *query, FLDOP *fo, int direction, int offset,
			 int verbose);
DBTBL  *TXnode_hint_prep(IPREPTREEINFO *prepinfo, QNODE *query,
			 QNODE *parentquery, int *success);

/******************************************************************/
/* JSON Texis SQL functions */

int      txfunc_isjson (FLD *f1);
int      txfunc_json_type (FLD *f1);
int      txfunc_json_format (FLD *f1, FLD *f2);
int      txfunc_json_value (FLD *f1, FLD *f2);
int      txfunc_json_query (FLD *f1, FLD *f2);
int      txfunc_json_modify (FLD *f1, FLD *f2, FLD *f3);
int      txfunc_json_merge_patch (FLD *f1, FLD *f2);
int      txfunc_json_merge_preserve (FLD *f1, FLD *f2);
int      TXmkComputedJson(FLD *f);

/******************************************************************/

int	TXsetFreeMemAtExit(int on);
int	TXgetFreeMemAtExit(void);

#ifdef EPI_ENABLE_CONF_TEXIS_INI                /* ie. version 6+ */
/* Note that this is just the preferred name; others are checked too: */
#  define TX_TEXIS_INI_NAME     "conf" PATH_SEP_S "texis.ini"
#else /* !EPI_ENABLE_CONF_TEXIS_INI */
#  define TX_TEXIS_INI_NAME     "texis.cnf"
#endif /* !EPI_ENABLE_CONF_TEXIS_INI */

typedef enum TXFILTERFLAG_tag
{
  TXFILTERFLAG_ENCODE   = 0,                    /* mutex with ...DECODE */
  TXFILTERFLAG_DECODE   = (1 << 0),             /* mutex with ...ENCODE */
}
TXFILTERFLAG;
#define TXFILTERFLAGPN  ((TXFILTERFLAG *)NULL)

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* A handle to a Unix compress/decompress object: */
typedef struct TXunixCompress_tag       TXunixCompress;
#define TXunixCompressPN        ((TXunixCompress *)NULL)

TXunixCompress *TXunixCompressOpen(TXFILTERFLAG flags, TXPMBUF *pmbuf);
TXunixCompress *TXunixCompressClose ARGS((TXunixCompress *uc));
int             TXunixCompressTranslate ARGS((TXunixCompress *uc,
                         byte **inBuf, size_t inBufSz, byte **outBuf,
                         size_t outBufSz, TXCTEHF flags));

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

typedef struct TXZLIB_tag       TXZLIB;
#define TXZLIBPN        ((TXZLIB *)NULL)

typedef enum TXZLIBFORMAT_tag
{
  TXZLIBFORMAT_RAWDEFLATE,
  TXZLIBFORMAT_ZLIBDEFLATE,
  TXZLIBFORMAT_GZIP,
  TXZLIBFORMAT_ANY		/* any of the above formats (decode only) */
}
TXZLIBFORMAT;
#define TXZLIBFORMATPN  ((TXZLIBFORMAT *)NULL)

TXZLIB *TXzlibClose ARGS((TXZLIB *zlib));
TXZLIB *TXzlibOpen(TXZLIBFORMAT format, TXFILTERFLAG flags, int traceEncoding,
		   TXPMBUF *pmbuf);
int     TXzlibReset ARGS((TXZLIB *zlib));
int     TXzlibTranslate ARGS((TXZLIB *zlib, TXCTEHF flags, byte **inBuf,
        size_t inBufSz, byte **outBuf, size_t outBufSz));

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#  undef I
#  define I(fld)					\
  int TXsqlFunc_refInfoGet##fld(FLD *refInfoFld);
TX_REFINFO_STRING_SYMBOLS
TX_REFINFO_SSIZE_T_SYMBOLS
#  undef I
int     TXsqlFunc_refInfoGetTypes(FLD *refInfoFld);
int     TXsqlFunc_refInfoGetFlags(FLD *refInfoFld);
int     TXsqlFunc_refInfoGetTagName(FLD *refInfoFld);
int     TXsqlFunc_refInfoGetSourceAttribute(FLD *refInfoFld);
int     TXsqlFunc_refInfoGetAttribute(FLD *refInfoFld, FLD *attrNameFld);
int     TXsqlFunc_refInfoGetAttributes(FLD *refInfoFld);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int TXltestShowHelp(void);
void reshowlocks(DBLOCK *aa, int firsts, int names, int times);

typedef void (CDECL TXALARMFUNC) ARGS((void *usr));
#define TXALARMFUNCPN   ((TXALARMFUNC *)NULL)

double  TXgetalarm ARGS((TXALARMFUNC *func, void *usr));
int     TXsetalarm(TXALARMFUNC *func, void *usr, double sec, TXbool inSig);
int     TXunsetalarm ARGS((TXALARMFUNC *func, void *usr, double *secp));
int     TXunsetallalarms ARGS((void));

extern int      TxTraceAlarm;

int     TXgetSemlockCount(void);
TXbool  TXsetSemunlockCallback(TXALARMFUNC *func, void *usr);

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

extern const TXCODEDESC	TXsqlCTypeNames[];
#define TXsqlCTypeName(sqlCType, unkName)		\
	TXgetCodeDescription(TXsqlCTypeNames, (sqlCType), (unkName))
extern const TXCODEDESC	TXsqlTypeNames[];
#define TXsqlTypeName(sqlType, unkName)		\
	TXgetCodeDescription(TXsqlTypeNames, (sqlType), (unkName))

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef __cplusplus
}  /* extern "C" */
#endif /* __cplusplus */

#if defined(TX_DEBUG) && defined(_WIN32)
int	debugbreak(void);
#  ifndef TX_NO_CRTDBG_H_INCLUDE
#    include "crtdbg.h"
#  endif /* TX_NO_CRTDBG_H_INCLUDE */
#endif /* TX_DEBUG && _WIN32 */

#include "3dbindex.h"

/* Returns `ptr' aligned up to `sz' boundary: */
#define TX_ALIGN_UP_PTR_SZ(ptr, sz)    ((void *)((char *)(ptr) +	\
          ((EPI_VOIDPTR_UINT)(ptr) % (sz) ?                     	\
           ((EPI_VOIDPTR_UINT)(sz) - ((EPI_VOIDPTR_UINT)(ptr) % (sz))) : 0)))
#define TX_ALIGN_UP_PTR(ptr)	TX_ALIGN_UP_PTR_SZ(ptr, TX_ALIGN_BYTES)
#define TX_ALIGN_UP_SIZE_T_SZ(n, sz)	\
  ((((size_t)(n) + (size_t)(sz) - (size_t)1) / (size_t)(sz)) * (size_t)(sz))
#define TX_ALIGN_UP_SIZE_T(n)	TX_ALIGN_UP_SIZE_T_SZ((n), TX_ALIGN_BYTES)

#define TX_RANK_USER_TO_INTERNAL(txApp, rank)	\
	((txApp) && (txApp)->legacyVersion7OrderByRank ? -(rank) : (rank))
#define TX_RANK_INTERNAL_TO_USER(txApp, rank)	\
	((txApp) && (txApp)->legacyVersion7OrderByRank ? -(rank) : (rank))

#endif  /* TEXINT_H */
