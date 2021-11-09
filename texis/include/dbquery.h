#ifndef _DBQUERY_H
#define _DBQUERY_H

#if defined(USE_SYSV_SEM) || defined(__linux)	/* __linux KNG 960416 */
#  include <sys/ipc.h>
#  ifdef __linux
#    undef word			/* conflicts with <asm/bitops.h> */
#  endif	/* __linux */
#  include <sys/sem.h>
#  if defined(__linux)
#    define word unsigned short
#  endif	/* __linux && SIZES_H */
#endif	/* USE_SYSV_SEM || __linux */
#ifndef _WIN32
#  include <limits.h>
#endif
#include <sys/types.h>
#include "api3.h"
#include "slist.h"
#include "txcoreconfig.h"
#include "txsql.h"
#include "fld.h"
#include "dbtable.h"
#include "nlock.h"
#include "fldmath.h"
#include "btree.h"
#include "ddic.h"
#include "dblock.h"
#include "mmtbl.h"
#include "dbstruct.h"
#include "texsql.h"
#include "texerr.h"
#include "texisapi.h"
#include "strbuf.h"
#include "txtypes.h"                            /* for TXbool */

/* $Id$ */

#if defined(linux) && EPI_OS_MAJOR==2 && EPI_OS_MINOR == 2 && EPI_OS_REV >= 12
/* Linux 2.2.15 has semun buried in linux/sem.h, which conflicts w/others:
 * KNG 000404
 */
#  undef HAVE_SEMUN_UNION
#endif

#ifdef NO_VORTEX_SCHEDULE       /* old license */
#  undef VORTEX_SCHEDULE
#else
#  define VORTEX_SCHEDULE         /* WTF remove when stable KNG 010323 */
#endif

extern char	texisver[];
/******************************************************************/

#define USE_STRBUF	/* Use an expandable buffer for sql */

#ifdef JMT_COMP
#define strcmp(a, b) jtstrcmp((a),(b),__LINE__,__FILE__)
#endif

int	createdb (const char *);

#ifdef DEVEL
#define deldbf(a)	delndbf(a)
#define rendbf(a,b)	renndbf((b), (a))
#else
#define deldbf(a)	unlink(a)
#endif
/******************************************************************/

int	validrow ARGS((TBL *, RECID *));

RECID	*gettblrow ARGS(( TBL *tb, RECID *handle));
RECID	*puttblrow ARGS(( TBL *tb, RECID *handle));

char	*getfldname ARGS(( TBL *tb, int n));
DDFD	*getflddesc ARGS(( TBL *tb, int n));
FLD	*nametofld ARGS(( TBL *tb, char *s));
int	ntblflds ARGS(( TBL *tb));
int     tbgetorign(TBL *tb, int n);

int	rewindtbl ARGS(( TBL *tb));

#ifdef WANT_BLOBS
#define blobdbf(tb) (tb->bf)           /* gets a DBF handle to the blob */
#endif

int     TXoutputVariableSizeLong ARGS((TXPMBUF *pmbuf, byte **bp, ulong n,
                                       const char *desc));
byte	*ivsl ARGS((byte *, ulong *));
byte    *outvsh ARGS((byte *d, EPI_HUGEUINT n));    /* KNG 980514 */
byte    *invsh ARGS((byte *s, EPI_HUGEUINT *np));   /* KNG 980514 */

/* Inline version of invsh() for speed.  `err' is what to do on error,
 * `s' is (advancing) byte *, `n' is resultant EPI_HUGEUINT value:
 */
#define INVSH(s, n, err)                        \
  switch ((byte)(*s) & 0xC0)                    \
    {                                           \
    case 0x00:                                  \
      n = (EPI_HUGEUINT)(*(s++));                   \
      break;                                    \
    case 0x40:                                  \
      n  = (EPI_HUGEUINT)((unsigned)(*(s++) & 0x3F) << 8); \
      n |=  (EPI_HUGEUINT)(*(s++));                 \
      break;                                    \
    case 0x80:                                  \
      n  = (EPI_HUGEUINT)((ulong)(*(s++) & 0x3F) << 16); \
      n |= (EPI_HUGEUINT)((unsigned)(*(s++)) << 8); \
      n |= (EPI_HUGEUINT)(*(s++));                  \
      break;                                    \
    case 0xC0:                                  \
      n = (EPI_HUGEUINT)(*s & 0x0F);                \
      switch ((byte)(*(s++)) & 0xF0)            \
        {                                       \
        case 0xE0:                              \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
        case 0xD0:                              \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
        case 0xC0:                              \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
          n = (n << 8) | (EPI_HUGEUINT)(*(s++));    \
          break;                                \
        default:                                \
          err;                                  \
          break;                                \
        }                                       \
      break;                                    \
    default:                                    \
      err;                                      \
      break;                                    \
    }

/* Inline version of invsh7() for speed.  `err' is what to do for error,
 * `s' is (advancing) byte *, `n' is resultant EPI_HUGEUINT value:
 */
#define INVSH7(s, n, err)        INVSH7_ACTUAL(s, n, err, (*(s++) & 0x7F))

/* Faster version of INVSH7, when you know bit 7 is clear in 2nd+ bytes: */
#define INVSH7_HICLR(s, n, err)  INVSH7_ACTUAL(s, n, err, (*(s++)))

/* NOTE: see also SKIPVSH7 */
#define INVSH7_ACTUAL(s, n, err, gs)                    \
  if ((*s & 0x40) == 0) n = (EPI_HUGEUINT)(*(s++) & 0x3F);  \
  else if ((*s & 0x20) == 0)                            \
    {                                                   \
      n = (EPI_HUGEUINT)((unsigned)(*(s++) & 0x1F) << 7);   \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else if ((*s & 0x10) == 0)                            \
    {                                                   \
      n = (EPI_HUGEUINT)((unsigned)(*(s++) & 0xF) << 14);   \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 7);             \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else if ((*s & 0x8) == 0)                             \
    {                                                   \
      n = (EPI_HUGEUINT)((unsigned)(*(s++) & 0x7) << 21);   \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 14);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 7);             \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else if ((*s & 0x4) == 0)                             \
    {                                                   \
      n = (EPI_HUGEUINT)((EPI_HUGEUINT)(*(s++) & 0x3) << 28);   \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 21);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 14);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 7);             \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else if ((*s & 0x2) == 0)                             \
    {                                                   \
      n = (EPI_HUGEUINT)((EPI_HUGEUINT)(*(s++) & 0x1) << 35);   \
      n |= (EPI_HUGEUINT)((EPI_HUGEUINT)(gs) << 28);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 21);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 14);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 7);             \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else if ((*s & 0x1) == 0)                             \
    {                                                   \
      n = (EPI_HUGEUINT)((EPI_HUGEUINT)(gs) << 35);             \
      n |= (EPI_HUGEUINT)((EPI_HUGEUINT)(gs) << 28);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 21);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 14);            \
      n |= (EPI_HUGEUINT)((unsigned)(gs) << 7);             \
      n |= (EPI_HUGEUINT)(gs);                              \
    }                                                   \
  else                                                  \
    {                                                   \
      err;                                              \
    }

/* SKIPVSH7: advance `s' over a VSH7.  Works regardless of high-bit values: */
#define SKIPVSH7(s, err)                                \
  if ((*s & 0x40) == 0) s++;                            \
  else if ((*s & 0x20) == 0) s += 2;                    \
  else if ((*s & 0x10) == 0) s += 3;                    \
  else if ((*s & 0x8) == 0) s += 4;                     \
  else if ((*s & 0x4) == 0) s += 5;                     \
  else if ((*s & 0x2) == 0) s += 6;                     \
  else if ((*s & 0x1) == 0) s += 7;                     \
  else                                                  \
    {                                                   \
      err;                                              \
    }

#define deltblrow(tb, at) freedbf(TXgetdbf((tb), (at)), TXgetoff(at))

PRECID	telltbl ARGS((TBL *));
int	recidvalid ARGS((RECID *));
#define TXrecidvalid(a) recidvalid(a)	/* For those who know */
/* In-line version for speed.  We can't macro TXrecidvalid() easily because
 * of macro side effects:  KNG 990118
 */
#define TXrecidvalid2(a) ((a)->off != (EPI_OFF_T)(-1))
#define TXrecidvalid3(a) ((a) != BTLOCPN && (a)->off != (EPI_OFF_T)(-1))

/******************************************************************/

DD	*closedd ARGS(( DD *dd));
DD	*opendd ARGS((void));
DD	*opennewdd ARGS((int));
DD	*TXdupDd ARGS((DD *dd));

DD	*convertdd ARGS((void *, size_t));

int	copydd ARGS(( DD *dd, char *name, TBL *table, char *oname, int novar));
int	putdd ARGS(( DD *dd, char *name, char *type, int n, int nonull));
int	dbaddtype ARGS((char *name, int number, int size));

DDFT   *getddft ARGS((char *type));
int	getddfd ARGS((char *type, int n, int nonull, char *name, DDFD *ddfd));
int	getddfdnum ARGS((int type, int n, int nonull, char *name, DDFD *ddfd));
DDFD	*closeddfd ARGS((DDFD *));

char	*ddfttypename ARGS(( int typen));
CONST char *TXfldtypestr ARGS((FLD *fld));      /* KNG 20060227 */
DDFT    *ddftype(int);
size_t	ddftsize ARGS((int typen));
DDFD    *ftn2ddfd_quick ARGS((int type, size_t n));

#define ddsettype(dd,t)	(dd?((dd)->tbltype = (t), 1):0)
#define isddvar(a)	((a) & DDVARBIT)
#define TXftnIsNotNullable(a)	((a) & FTN_NotNullableFlag)
#define TXftnIsNullable(a)	(((a) & FTN_NotNullableFlag) ? 0 : 1)

/******************************************************************/

FLD	*closefld ARGS((FLD *f));
FLD	*newfld ARGS((FLD *f));
FLD	*openfld ARGS((DDFD *fd));
void    releasefld ARGS((FLD *f));
int     initfld ARGS((FLD *f, int type, size_t n));
FLD	*openstfld ARGS((DDFD *fd));
FLD	*createfld ARGS((char *type, int n, int nonnull));
FLD	*emptyfld(FTN type, int n);
void	putfld ARGS(( FLD *f, void *buf, size_t n));
void    putfldinit ARGS(( FLD *f, void *buf, size_t n));
int	setfld ARGS((FLD *f, void *v, size_t n));
void	*TXftnDupData ARGS((void *data, size_t n, int type, size_t size,
                            size_t *alloced));
int	TXftnFreeData ARGS((void *data, size_t n, int type,
                            int blobiMemdataToo));
int     TXftnInitDummyData(TXPMBUF *pmbuf, FTN type, void *data, size_t sz,
                           int forFldMath);
int	freeflddata ARGS((FLD *f));
EPI_SSIZE_T TXprintHexCounter(char *buf, EPI_SSIZE_T bufSz,
                              const ft_counter *ctr);
int	TXparseHexCounter(ft_counter *ctr, const char *s, const char *e);
#define TX_COUNTER_HEX_BUFSZ    \
  (TX_FT_COUNTER_DATE_BITS/4 + TX_FT_COUNTER_SEQ_BITS/4 + 1)
int	clearfld ARGS((FLD *f));
int	copyfld ARGS((FLD *, FLD *));
int	fldnum ARGS((TBL *, FLD *));
int	fldisset ARGS((FLD *f));

#define fldisvar(f) ((f)->type&DDVARBIT)
#define fldisnul(f) (0)
void	*getfld ARGS(( FLD *f, size_t *pn));
#define setfldv(f)	(f)->v=(f)->shadow
#define fldvalloced(f)	(((f)->v==(f)->shadow)&&((f)->frees!=FREESHADOW)?0:1)
#define TXfldGetElsz(f) ((f)->elsz)

TXbool TXfldIsMultipleItemType(FLD *fld, FTN *mainType, FTN *itemType);
size_t TXfldNumItems ARGS((FLD *fld));
void *TXfldGetNextItem ARGS((FLD *fld, void *prevItem, size_t prevItemLen,
                             size_t *itemLen));
char *TXfldGetNextItemStr ARGS((FLD *fld, void **itemPtr, size_t *itemLen));

/******************************************************************/

DDIC *ddopen ARGS((const char *pname));
DDIC *ddclose ARGS((DDIC *));
int  ddicsetstate ARGS((DDIC *, int));
char *TXddgetanytable ARGS((DDIC *ddic, CONST char *tname, char *type,
                            int  reset));
char *ddgettable ARGS((DDIC *, char *, char *, int));
char *ddgettablecreator ARGS((DDIC *ddic, char *tname));
int addtable ARGS((DDIC *, char *, char *, char *, char *, DD *, int, int));
/*
char *fullpath ARGS((char *, char *, int));
*/
int ddgetindex ARGS((DDIC *, char *, char *, char **, char ***, char ***,
                     char ***sysindexParamsVals));
int ddgetindexbyname(DDIC *ddic, char *tname, char **itype,
                     char **nonUnique, char ***paths, char ***tableNames,
                     char ***fields, char ***sysindexParamsVals);
int TXsetVerbose ARGS((int n));
int setprop ARGS((DDIC *, char *, char *));

/******************************************************************/

#ifndef NEVER
#ifndef NO_BUBBLE_INDEX

DBIDX *createdbidx ARGS((void));
void   cleardbidx ARGS((DBIDX *dbidx));

int    TXdbidxUnlock ARGS((DBIDX *dbidx));

DBIDX *opendbidx ARGS((int itype, char *iname, CONST char *sysindexFields,
               CONST char *sysindexParams, DBTBL *dbtbl, int flags));
DBIDX *closedbidx ARGS((DBIDX *));

int    setdbidx ARGS((DBIDX *dbidx, FLD *fld, char *fname, FLD *fld2,
                      int inclo, int inchi));
int    infodbidx ARGS((DBIDX *));

BTLOC	getdbidx ARGS((DBIDX *, void *, size_t *, byte **));

#ifdef NEVER
RECID	dbidxgetnext ARGS((DBIDX *, int *, void *, byte **));
#else /* NEVER */
#define dbidxgetnext(a,b,c,d) getdbidx((a), (c), (b), (d))
#endif /* NEVER */
#else /* BUBBLE_INDEX */
#define dbidxgetnext(a,b,c,d) btgetnext((a)->btree, (b), (c), (d))
#endif /* BUBBLE_INDEX */
#else
RECID	dbidxgetnext ARGS((DBIDX *, int *, void *, byte **));
#endif

/******************************************************************/
/*	Upper-level functions working on DB-Tables */

DBTBL *TXnewDbtbl(TXPMBUF *pmbuf);
DBTBL  *opendbtbl    ARGS((DDIC *, char *));
DBTBL  *createdbtbl  ARGS((DDIC *, DD *, char *, char *, char *, int));
DBTBL  *closedbtbl   ARGS((DBTBL *));
DBTBL  *_closedbtbl   ARGS((DBTBL *));
void	rewinddbtbl  ARGS((DBTBL *));
DBTBL  *deltable     ARGS((DBTBL *));
void	disptable    ARGS((DBTBL *, FLDOP *));
RECID  *getdbtblrow  ARGS((DBTBL *));
int	ioctldbtbl   ARGS((DBTBL *, int, void *));
#define ndbtblflds(a) (ntblflds((a)->tbl))

RECID  *putdbtblrow  ARGS((DBTBL *, RECID *));
int	renametbl    ARGS((DBTBL *, char *));
FLD    *dbnametofld  ARGS((DBTBL *, char *));
int     TXisRankName(const char *name);
char   *dbnametoname ARGS((DBTBL *d, char *s, FTN *type, int *ddIdx));
int	getindexes   ARGS((DBTBL *));
DBTBL  *dostats      ARGS((DBTBL *, FLDOP *));

size_t  TXdbtblGetRowSize(DBTBL *dbtbl);
int     TXdbtblReleaseRow(DBTBL *dbtbl);

void    TXdbtblTraceRowFieldsMsg(const char *fn, DBTBL *dbtbl, RECID recid,
                                 char **tables, char **fields);

/******************************************************************/
/*	Lower level functions, used to manipulate tables */

DBTBL  *tup_read ARGS((DBTBL *, FLDOP *, int, int, int *,
                       TXCOUNTINFO *countInfo));
RECID  *tup_write ARGS((DBTBL *, DBTBL *, FLDOP *, int));
int	tup_copy ARGS((DBTBL *, DBTBL *, FLDOP *));
void	tup_disp (DBTBL *tbl, int width, FLDOP *fo);
void	tup_cdisp ARGS((DBTBL *, int, FLDOP *));
void	tup_sdisp (DBTBL *tbl, int rmnl, int delim, int qt, FLDOP *fo);
int	tup_disp_head (DBTBL *tbl, int width);
int	tup_cdisp_head (DBTBL *tbl, int width);
int	tup_sdisp_head (DBTBL *tbl, int col, int qt);
DBTBL  *tup_union_setup ARGS((DBTBL *, DBTBL *));
DBTBL  *tup_append_setup ARGS((DBTBL *, DBTBL *));
DBTBL  *tup_index_setup(DBTBL *tin, PROJ *proj, FLDOP *fo, TXOF rankdir,
                        DD *outputDd);
DBTBL  *tup_product ARGS((QNODE *qnode, DBTBL *, DBTBL *, DBTBL *, PRED *, PROJ *, FLDOP *));
int	tup_project ARGS((DBTBL *, DBTBL *, PROJ *, FLDOP *));
void	tup_union ARGS((DBTBL *, DBTBL *, DBTBL *, FLDOP *));
int	tup_append ARGS((DBTBL *, DBTBL *, FLDOP *));
RECID  *tup_index ARGS((DBTBL *, DBTBL *, PROJ *, FLDOP *, RECID *));
RECID  *tup_index_search ARGS((DBTBL *, DBTBL *, PROJ *, FLDOP *, RECID *));
int	tup_delete ARGS((DBTBL *, DBTBL *));
int	tup_match ARGS((DBTBL *, PRED *, FLDOP *));

int	TXprocessquery ARGS((QNODE *query, FLDOP *fo));
void	preparequery ARGS((QNODE *, FLDOP *, int));

int     TXnode_table_exec ARGS((QNODE *query, FLDOP *fo, int direction,
                                int offset, int verbose));
int     TXnode_join_exec ARGS((QNODE *query, FLDOP *fo, int direction,
                               int offset, int verbose));
int     TXnode_rename_exec ARGS((QNODE *query, FLDOP *fo, int direction,
                                 int offset, int verbose));

/******************************************************************/
/*	Manipulate predicates	*/

void	TXpredClear ARGS((PRED *pred, int full));
int	TXpredNumFunctionArgs ARGS((PRED *pred));
char    *TXpredGetFirstUsedColumnName(PRED *p);
int	pred_eval ARGS((DBTBL *, PRED *, FLDOP *));
void	*evalpred ARGS((DBTBL *t, PRED *p, FLDOP *fo, size_t *sz, FTN *type));
void   *evalstats ARGS((DBTBL *, PRED *, FLDOP *, size_t *));
PRED   *substpred ARGS((PRED *, DBTBL *));
PRED   *substpred2 ARGS((PRED *, PRED *, DBTBL *));
char	*predtype ARGS((PRED *, DBTBL *, FLDOP *, int *, int *));
PRED   *duppred ARGS((PRED *));
PRED   *closepred ARGS((PRED *));
void	nullmms ARGS((PRED *));
PRED	*optpred ARGS((PRED *, int*));
PRED	*optpred2 ARGS((PRED *, int*));

/******************************************************************/

ft_indirect *mk_ind ARGS((char *, size_t *));

int	buftofld ARGS((byte *, TBL *, size_t));
size_t	fldtobuf ARGS((TBL *));
PROJ   *dupproj ARGS((PROJ *));

PROJ   *closeproj ARGS((PROJ *));
PROD   *closeprod ARGS((PROD *));
/******************************************************************/

FLDOP	*dbgetfo ARGS((void));
FLDOP   *TXgetFldopFromCache ARGS((void));
FLDOP   *TXreleaseFldopToCache ARGS((FLDOP *fldop));

char    *TXtblTupleToStr(TBL *tbl);

char    *TXdbtblSchemaToStr(const DBTBL *dbtbl, int orderToo);
char    *TXdbtblTupleToStr(DBTBL *dbtbl);

DDMMAPI *openddmmapi(QNODE_OP qnodeOp, void *data, FOP mmOp);
DDMMAPI *closeddmmapi ARGS((DDMMAPI *));
int	setddmmapi(DBTBL *tbl, DDMMAPI *ddmmapi, FOP mmOp);
PROD	*doproductsetup ARGS((QUERY *));
int	doproduct ARGS((QUERY *, PRED *, FLDOP *));
time_t	*strtodate ARGS((char *));
int	TXisblob ARGS((int ftype));
DD	*TXbddc ARGS((DD *));
DD	*TXbiddc ARGS((DD *));
void	*btobi ARGS((EPI_OFF_T, TBL *));
char	*dbgettemp ARGS((void));
int	fldcopy ARGS((FLD *, TBL *, FLD *, TBL *, FLDOP *));
int	_fldcopy ARGS((FLD *, TBL *, FLD *, TBL *, FLDOP *));
void	dbresetstats ARGS((DBTBL *));

BLOB	*newblob ARGS((char *, char *, char *, int));
BLOB	*putblob ARGS((BLOB *, char *, int));
int	 getblob ARGS((BLOB *, char **, int *));
char	*newindirect ARGS((char *, char *, char *));

MMLST	*openmmlst ARGS((void));
MMLST	*freemmlst ARGS((MMLST *));
int	addmmlst ARGS((MMLST *, void *,char *, char *));

MMLST	*getmmlst ARGS((HSTMT));

/******************************************************************/
/*	QNODE represents the query in an unprepared state.        */

#define QTOKEN QNODE_OP
QNODE	*openqnode ARGS((QNODE_OP op));
QNODE	*closeqnode ARGS((QNODE *));
PRED	*treetopred ARGS((DDIC *, QNODE *, int, FLDOP *));
PROJ	*treetoproj ARGS((DDIC *, QNODE *, FLDOP *));
UPDATE	*treetoupd ARGS((DDIC *, QNODE *, FLDOP *));
UPDATE	*closeupdate ARGS((UPDATE *));
QNODE	*optqtree ARGS((QNODE *));

DDIC    *tx_texisddic ARGS((TEXIS *tx));
int	TXenumparams ARGS((LPSTMT, QNODE *, int intree, size_t *paramcount));
PARAM	*getparam ARGS((LPSTMT, QNODE *, int));
int     TXnextparamnum(void);
void    TXresetparamcount(void);


int	valuestotbl ARGS((QNODE *, DBTBL *, FLDOP *));
void	columntotbl ARGS((QNODE *, DBTBL *, FLDOP *));
void	listtotbl ARGS((QNODE *, DBTBL *, FLDOP *));

/******************************************************************/

void	adduserfuncs ARGS((FLDOP *));

void	flushindexes ARGS((DBTBL *));
int	TXcloseFdbiIndexes(DBTBL *dbtbl);       /* internal use */
void	closeindexes ARGS((DBTBL *));

int	setupdfields ARGS((DBTBL *, UPDATE *));
int	updatefields ARGS((DBTBL *, UPDATE *, FLDOP *, int *));
int	deltupfromindex ARGS((DBTBL *, BINDEX, BTLOC *));
int	delfrominv ARGS((BINVDX, BTLOC *));

/******************************************************************/
/*	These functions take care of all of the security issues.  */

void	setindexperms ARGS((DBTBL *));
int	permcheck ARGS((DBTBL *, int));
long	strtoperms ARGS((char *));
char   *strtounix ARGS((char *));
int	permgrant ARGS((DDIC *, DBTBL *, char *, long));
int	permrevoke ARGS((DDIC *, DBTBL *, char *, long));
int	permsunix ARGS((DDIC *));
int	permstexis ARGS((DDIC *, char *, char *));
int	dbgetperms ARGS((DBTBL *, DDIC *));
int	permgrantdef ARGS((DDIC *, DBTBL *));

/******************************************************************/
/*	Functions used for locking and concurrency                */

int	addltable ARGS((TXPMBUF *pmbuf, DBLOCK *rc, char *table));
int	delltable ARGS((TXPMBUF *pmbuf, DBLOCK *sem, char *table, int tblid));
int	semlock(TXPMBUF *pmbuf, VOLATILE DBLOCK *dblock, TXbool create);
int	semunlock ARGS((TXPMBUF *pmbuf, VOLATILE DBLOCK *dblock));
void	seminit ARGS((SEM *));
int	initmutex ARGS((TXPMBUF *pmbuf, VOLATILE DBLOCK *));
int	openmutex(TXPMBUF *pmbuf, VOLATILE DBLOCK *dbl, TXbool create);
int	delmutex ARGS((TXPMBUF *pmbuf, VOLATILE DBLOCK *));

#ifdef HAVE_SEMUN_UNION
typedef	union semun     TXSEMUN;
#else /* !HAVE_SEMUN_UNION */
typedef	union sem_un
{
  int	        val;
  struct	semid_ds *buf;
  ushort	*array;
}
  TXSEMUN;
#endif /* !HAVE_SEMUN_UNION */

int     TXsemctl3(TXPMBUF *pmbuf, const char *func, int semid, int semNum,
                  int cmd);
int     TXsemctl4(TXPMBUF *pmbuf, const char *func, int semid, int semNum,
                  int cmd, TXSEMUN su);

int	lockmode ARGS((DDIC *, int));
int	locktable ARGS((DDIC *, char *, int));
int	unlocktable ARGS((DDIC *, char *, int));

#if !defined(USE_NTSHM) && !defined(USE_POSIX_SEM) && defined(HAVE_SEM)
int     tx_delsem ARGS((TXPMBUF *pmbuf, int semid));
#endif

/******************************************************************/

/******************************************************************/

extern int	TXfldtostrHandleBase10;

void	TXfldtostrFreeMemAtExit(void);
char	*fldtostr ARGS((FLD *));
char	*strtodos ARGS((char *));

QUERY	*TXopenQuery ARGS((QUERY_OP op));
QUERY	*closequery ARGS((QUERY *));

int	metamorphop ARGS((FLD *, FLD *));
int	fmetamorphop ARGS((FLD *, FLD *));
int	bmetamorphop ARGS((FLD *, FLD *));

DD *TXcreateSysusersDd(void);
TBL	*createusertbl ARGS((DDIC *));
TBL	*createpermtbl ARGS((DDIC *));

int	adduser ARGS((DDIC *, char *, char *, char *));
int	chpass ARGS((DDIC *, char *, char *, char *));
int	deluser ARGS((DDIC *, char *, char *));
int	administer ARGS((char *, char *, char *, char *, char *));

int	closesqlparse ARGS((void));
#ifdef USE_STRBUF
int	sqlconvert ARGS((char *, char *, DDIC *, STRBUF *, int));
#else
int	sqlconvert ARGS((char *, char *, DDIC *, char *, int));
#endif
int	clearout ARGS((void));
int	setproperties ARGS((char *));
#ifndef NEVER
int	convtosql ARGS((void *, int, int, int, void **, size_t *, FLDOP *));
#else
int	convtosql ARGS((void *, int, int, int, void **, size_t *));
#endif
int	dbttosqlt ARGS((int));
long	setmmtbl ARGS((MMTBL *, char *, unsigned long));

const char *TXgetIndexTypeDescription ARGS((int indexType));

#ifndef TXindOptsPN
typedef struct TXindOpts_tag    TXindOpts;
#  define TXindOptsPN   ((TXindOpts *)NULL)
#endif

int	createindex ARGS((DDIC *ddic, char *idxfile, char *indname,
                          char *table, char *field, int unique, int itype,
                          TXindOpts *options));
int	TXalterIndexes (DDIC *ddic, CONST char *indexName, CONST char *tableName, CONST char *actionOptions, PRED *conditions);

/******************************************************************/

int	texispusherror ARGS((DDIC *, TXERR));
TXERR	texispoperr ARGS((DDIC *));
TXERR	texispeekerr ARGS((DDIC *));

/******************************************************************/

/* Vortex test745 $largeText value must be >> TX_INDEX_MAX_SAVE_BUF_LEN: */
#define TX_INDEX_MAX_SAVE_BUF_LEN	(((size_t)1) << 19)

#define TXtempPidExtLen 4
extern CONST char TXtempPidExt[TXtempPidExtLen + 1];
#define TXxPidExtLen 6
extern CONST char TXxPidExt[TXxPidExtLen + 1];

int     tx_updateparamtbl ARGS((char *path, int indexType,
                                CONST TXMMPARAMTBLINFO *paramTblInfo,
                                int fdbiVersion));
int	updindex (DDIC *ddic, char *indname, int flags, TXindOpts *option, PRED *conditions);
int TXcreateTempIndexOrTableEntry ARGS((DDIC *ddic, CONST char *dir,
    CONST char *logicalName, CONST char *parentTblName,
    CONST char *indexFldNames, int numTblFlds, int flags,
    CONST char *remark, CONST char *sysindexParams, char **createdPath,
    RECID *addedRow));
int TXtransferIndexOrTable ARGS((CONST char *oldPath, CONST char *newPath,
     DDIC *ddic, char *logicalName, int type,
     CONST TXMMPARAMTBLINFO *paramTblInfo, int fdbiVersion, int flags));

ft_counter	*getcounter ARGS((DDIC *));
char	*counttostr ARGS((ft_counter *));
char	*tempfn ARGS((char *, char *));

int	_setproperties ARGS((DDIC *, char *));
int	_addexp ARGS((char *));
int	_delexp ARGS((char *));
int	_listexp ARGS((char *));

FLD	*getfldn ARGS((TBL *tb, int n, TXOF *orderFlags));
FLD	*TXgetrfldn ARGS((TBL *tb, int n, TXOF *orderFlags));

int	_addtoinv ARGS((BINVDX, BTLOC *));
int	addtuptoindex ARGS((DBTBL *, BINDEX, BTLOC *));

ft_strlst	*_ctofstrlst ARGS((char **, int *));

int	_setparam ARGS((int ipar, int ctype, int sqltype, long coldef,
                        int scale, void *value, long *length));
#ifndef OBJECT_READTOKEN
int	setparsestring ARGS((char *));
#endif

#ifndef __BORLANDC__
#ifndef _WIN32
char	*strlwr ARGS((char *));
#endif
#endif

DBF	*TXgetdbf ARGS((TBL *, RECID *));

int	_setstddic ARGS((DDIC *));

TEXIS	*closentexis ARGS((TEXIS *));
TEXIS	*openntexis  ARGS((void *, void *, void *, char *, char *, char *, char *));
TEXIS	*dupntexis  ARGS((void *, void *, void *, TEXIS *));
void     ncbntexis   ARGS((TEXIS *,int));
int      prepntexis  ARGS((TEXIS *,char *,APICP *));
int      paramntexis ARGS((TEXIS *,int,void *,long *,int,int));
int      execntexis  ARGS((TEXIS *));
TEXIS	*setntexis   ARGS((TEXIS *, char *, APICP *));
FLDLST	*getntexis   ARGS((TEXIS *,int));
int	TXsqlCancel(TEXIS *tx);
int      flushntexis ARGS((TEXIS *));
int      flushntexis2 ARGS((TEXIS *tx, int nrows, int max));
int      gcbntexis   ARGS((TEXIS *));
int	 lockmodentexis  ARGS((TEXIS *, int));
int	 lockntexis  ARGS((TEXIS *, char *, int));
int	 unlockntexis  ARGS((TEXIS *, char *, int));
ft_counter	*TXgettxcounter ARGS((TEXIS *));
int      ntexis      ARGS((void *,void *,void *,char *,char *,APICP *,char *,char *,char *));
int		ntexistraps ARGS((int));
int		settxtimeout ARGS((int));
EPI_HUGEUINT	getindcount ARGS((void));
int	TXsqlGetCountInfo(HSTMT hstmt, TXCOUNTINFO *countInfo);
const char *TXsqlRetcodeToToken(RETCODE retcode);
const char *TXsqlRetcodeToMessage(RETCODE retcode);
RETCODE TXsqlGetRetcode(TEXIS *tx);
int	newproctexis ARGS((TEXIS *));
APICP	*TXopenapicp ARGS((void));
int     logontexis ARGS((TEXIS *tx, char *user, char *passwd));
int     resettexis ARGS((TEXIS *tx));
int     resetparamntexis ARGS((TEXIS *tx));
int     ddgetorign ARGS((DD *dd, int n));
int     TXddgetindexinfo(DDIC *ddic, char *tname, char *fldn,
                          char **itype, char **iunique, char ***names,
          char ***files, char ***fields, char ***params, char ***tableNames);

int	_recidcmp ARGS((PRECID, PRECID));
#define ISFNBEG(a) (*(a)=='/'||(isalpha((int)*(unsigned char *)(a))&&*((a)+1)==':'&&*((a)+2)=='\\'))

/******************************************************************/

DBLOCK	*opendblock ARGS((DDIC *ddic));
DBLOCK	*closedblock(TXPMBUF *pmbuf, DBLOCK *dbl, int sid, TXbool readOnly);
DBLOCK  *TXdblockOpenDirect(TXPMBUF *pmbuf, const char *path, int *sid,
                            TXbool readOnly);

#ifndef OLD_LOCKING
#   ifdef STEAL_LOCKS
int	dblock ARGS((DDIC *, ulong, long *, int, char *, ft_counter *, long *));
int	dbunlock ARGS((DDIC *, ulong, long *, int, char *, long *));
#   else
int	dblock ARGS((DDIC *, ulong, long *, int, char *, ft_counter *));
int	dbunlock ARGS((DDIC *, ulong, long *, int, char *));
#   endif
#else
int	dblock ARGS((DDIC *, ulong, long, int, char *, ft_counter *));
int	dbunlock ARGS((DDIC *, ulong, long, int, char *));
#endif

/******************************************************************/

TTL	*rormerge ARGS((TTL **));
TTL	*hormerge ARGS((TTL **));

/******************************************************************/

char	*text2mm ARGS((char *buf, int maxwords, APICP *apicp));
CONST char * CONST *text2mmnoise ARGS((CONST char * CONST *noise));
char	*keywords ARGS((char *, int, APICP *apicp));

#define TXABS_STYLE_DUMB                0
#define TXABS_STYLE_SMART               1
#define TXABS_STYLE_QUERYSINGLE         2
#define TXABS_STYLE_QUERYMULTIPLE       3
/* alias for best mode available; assumed to require a query: */
#define TXABS_STYLE_QUERYBEST           TXABS_STYLE_QUERYMULTIPLE

#define TXABS_DEFSZ     230

int     TXstrToAbs ARGS((CONST char *s));

char	*abstract ARGS((char *, int, int, char *, DBTBL *, char **idxExprs,
			char *locale));

int	TXgetcachedindexdata ARGS((byte **data, size_t *recsz, size_t *count));
int	TXclosecachedindexdata ARGS((void));

/******************************************************************/

#ifdef TRACK_MEMCPY
#define memcpy(a,b,c)	{putmsg(200, NULL, "Memcpy %d bytes at %s:%d", (c), __FILE__, __LINE__); memcpy((a), (b), (c));}
#endif
/******************************************************************/

#define TXGEO_PYTHAG 1
#define TXGEO_GREAT_CIRCLE 2

int  TXsetEastPositive(int eastPositiveParam);
int  TXgetEastPositive(void);

int TXfunc_azimuth2compass(FLD *fld_azimuth, FLD *fld_resolution, FLD *fld_verbose);
int TXazimuth2compass(char **ret, double azimuth, int resolution, int verbose);

int TXfunc_azimuthgeocode(FLD *fld_geocode1, FLD *fld_geocode2,
                          FLD *fld_method);
int TXfunc_azimuthlatlon(FLD *fld_lat1, FLD *fld_lon1,
                         FLD *fld_lat2, FLD *fld_lon2, FLD *fld_method);
double TXazimuthlatlon(double lat1, double lon1, double lat2, double lon2,
                       int method);
int TXfunc_azimuthgeocode(FLD *fld_geocode1, FLD *fld_geocode2,
                          FLD *fld_method);
int TXfunc_latlon2geocode(FLD *fld_lat, FLD *fld_lon);
int TXfunc_latlon2geocodearea(FLD *fld_lat, FLD *fld_lon, FLD *radius);
long TXlatlon2geocode(double lat, double lon);
int TXcanonicalizeGeocodeBox(long *c1, long *c2);
int TXgeocodeDecode(long geocode, double *lat, double *lon);

int TXfunc_geocode2lat(FLD *fld_geocode);
int TXfunc_geocode2lon(FLD *fld_geocode);
double TXgeocode2lat(long geocode);
double TXgeocode2lon(long geocode);

int TXfunc_distGeocode(FLD *fld_geocode1, FLD *fld_geocode2, FLD *fld_method);
double TXdistGeocode(long geocode1, long geocode2, int method);

int TXfunc_distlatlon(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2, FLD *fld_method);
double TXdistlatlon(double lat1, double lon1, double lat2, double lon2, int method);

int TXfunc_greatCircle(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2);
double TXgreatCircle(double lat1, double lon1, double lat2, double lon2);

int TXfunc_pythag(FLD *fld_x1, FLD *flx_y1, FLD *fld_x2, FLD *fld_y2);
double TXpythag(int count,...);
int TXfunc_pythagMiles(FLD *fld_lat1, FLD *fld_lon1, FLD *fld_lat2, FLD *fld_lon2);
double TXpythagMiles(double lat1, double lon1, double lat2, double lon2);

int TXfunc_dms2dec(FLD *fld_dec);
int TXfunc_dec2dms(FLD *fld_dms);
double TXdms2dec(double dec);
double TXdec2dms(double dms);
double TXparseCoordinate(CONST char *buf, int flags, char **end);
long   TXparseLocation(CONST char *buf, char **end, double *lat, double *lon);
int TXfunc_parselatitude(FLD *latitude);
int TXfunc_parselongitude(FLD *longitude);

int  TXfunc_stringformat(FLD *fmtFld, FLD *argFld1, FLD *argFld2,
                         FLD *argFld3, FLD *argFld4);
int  TXfunc_stringcompare(FLD *aFld, FLD *bFld, FLD *modeFld);

int  TXsqlFunc_binToHex ARGS((FLD *byteFld, FLD *modeFld));
int  TXsqlFunc_hexToBin ARGS((FLD *hexFld, FLD *modeFld));

int  TXsqlFunc_metaphone(FLD *termFld, FLD *flagsFld, FLD *modeFld);

/* Helper functions for getting/returning data in SQL functions: ---------- */

void *TXsqlGetFunctionArgData ARGS((TXPMBUF *pmbuf, CONST char *fn, FLD *fld,
                                  int ftnType, FTI ftiType, size_t *dataLen));
int   TXsqlSetFunctionReturnData(CONST char *fn, FLD *fld, void *data,
             FTN ftnType, FTI ftiType, size_t elsz, size_t numEls, int dupIt);

#  ifdef EPI_MIME_API

/* mimeReader/mimeEntity: ------------------------------------------------- */

int  TXsqlFunc_mimeReaderOpenFile(FLD *fileFld, FLD *maxDepthFld);
int  TXsqlFunc_mimeReaderOpenString(FLD *stringFld, FLD *maxDepthFld);
int  TXsqlFunc_mimeReaderOpenEntity(FLD *entityOrReaderFld, FLD *maxDepthFld,
                                    FLD *flagsFld);
int  TXsqlFunc_mimeReaderMoveToNextEntity(FLD *readerFld);
int  TXsqlFunc_mimeReaderMoveToNextEntitySibling(FLD *readerFld);
int  TXsqlFunc_mimeReaderGetEntity(FLD *readerFld);
int  TXsqlFunc_mimeReaderGetFullEntity(FLD *readerFld);

/* mimeEntity... functions can be passed a mimeEntity *or* a
 * mimeReader: if given the latter, the reader's current entity is
 * used.  Saves a mimeReaderGetEntity() call after every
 * mimeReaderMoveToNextEntity().  But we still have separate
 * mimeEntity and mimeReader types (instead of just mimeReader), so
 * that to save an entity for later use (after reader has moved on),
 * we just call mimeReaderGetEntity(reader) and save that, not
 * mimeReaderOpenEntity(reader) to open a whole new parser from the
 * current parser's current entity: former makes more sense.  Also,
 * having distinct types avoids needing two sets of
 * mime...GetSomeAttribute() calls (but only if we ever implement
 * mimeReaderGetFullEntity()): one for the reader's current atomic
 * entity and one for the reader's current full (multipart children
 * unparsed, part of body) entity.  (Though we probably only need
 * duplicates for GetBody/Size; headers etc. are the same for atomic
 * and full entities?).
 */
int  TXsqlFunc_mimeEntityGetHeaderNames(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetHeaderValues(FLD *entityOrReaderFld,
                                         FLD *hdrNameFld);
int  TXsqlFunc_mimeEntityGetRawHeaderValues(FLD *entityOrReaderFld,
                                            FLD *hdrNameFld);
int  TXsqlFunc_mimeEntityGetHeaderParameterNames(FLD *entityOrReaderFld,
                                                 FLD *hdrNameFld);
int  TXsqlFunc_mimeEntityGetHeaderParameterValues(FLD *entityOrReaderFld,
                                          FLD *hdrNameFld, FLD *paramNameFld);
int  TXsqlFunc_mimeEntityGetRawHeaderSection(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetRawHeaderSectionOffset(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetRawHeaderSectionSize(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetRawBody(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetRawBodyOffset(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetRawBodySize(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetBody(FLD *entityOrReaderFld,
                                 FLD *flagsFld);
int  TXsqlFunc_mimeEntityGetText(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetTextCharset(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetTextFormatter(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityIsStartBodyPart(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityIsReparented(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityIsLastChild(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetImapSectionSpecification(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetContentLocation(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetDepth(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetChildNumber(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetSequenceNumber(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetMessageFilename(FLD *entityOrReaderFld);
int  TXsqlFunc_mimeEntityGetSafeFilename(FLD *entityOrReaderFld);

/* mailUtil.c: ------------------------------------------------------------ */

int  TXsqlFunc_headerMailboxGetAddress(FLD *mailboxFld);
int  TXsqlFunc_headerMailboxGetDisplayName(FLD *mailboxFld);
int  TXsqlFunc_mailParseAliases(FLD *aliasStringFld, FLD *whatFld,
                                FLD *flagsFld);

int  TXsqlFunc_headerDecode(FLD *hdrFld);
int  TXsqlFunc_headerGetItems(FLD *hdrFld);
int  TXsqlFunc_headerItemGetParameterNames(FLD *hdrItemFld);
int  TXsqlFunc_headerItemGetParameterValues(FLD *hdrItemFld,FLD*paramNameFld);

#  endif /* EPI_MIME_API */

/* ------------------------------------------------------------------------ */

#ifdef TX_CONVERT_MODE_ENABLED
int     TXsqlFunc_convert(FLD *srcFld, FLD *typeFld, FLD *modeFld);
#endif /* TX_CONVERT_MODE_ENABLED */

int     TXsqlFunc_urlcanonicalize(FLD *urlFld, FLD *flagsFld);

/******************************************************************/
#endif /* _DBQUERY_H */
