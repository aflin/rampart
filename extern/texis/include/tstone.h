#ifndef TSTONE_H
#define TSTONE_H
#ifdef __cplusplus
extern "C" {
#endif
/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(push,tstone_h,2)
#endif
/**********************************************************************/
#include "api3.h"
#include "dbquery.h"
#ifndef BLOB
#  define BLOB void
#  define BLOBPN (BLOB *)NULL
#endif
#ifndef MAXMMLST
#  define MAXMMLST 16
#endif
/**********************************************************************/
#include "tisp.h"
#ifndef EXPORT
#if defined(_MSC_VER) && _MSC_VER>=800 && defined(_WIN32)
        /* !@#$%^&* msc 800 is not the same language across 16/32 bit */
#  define EXPORT __declspec(dllexport)
#else
#  ifdef _WIN32
#     define EXPORT _export
#  else
#     define EXPORT
#  endif
#endif
#endif

#ifndef TX_VERSION_NUM
error: TX_VERSION_NUM undefined;
#endif /* !TX_VERSION_NUM */

                          /* routines for manual vararg style calling */
#if TX_VERSION_MAJOR >= 6
#define TSPMAXARGS 1000
#else
#define TSPMAXARGS 100    /* JMT 98-01-05 expanded.  WTF, make dynamic.
			     There is no more limit in dbtable.h      */
			             /* same as DDFIELDS in dbtable.h */
#endif

#define MMOFFSE  struct mmoffse_struct
#define MMOFFSEPN  (MMOFFSE *)NULL
#define MMOFFS  struct mmoffs_struct
#define MMOFFSPN (MMOFFS *)NULL
MMOFFS                           /* offset info for Metamorph subhits */
{
   int n;                                 /* number of off's in array */
   MMOFFSE {
      long start;                                  /* start of region */
      long end;                             /* one past end of region */
   } *off;                             /* array of subhit offset info */
   int nhits;                             /* number of hit's in array */
   MMOFFSE *hit;                          /* array of hit offset info */
};

#define SRCHLST struct net_SRCHLST
#undef SRCHLST

typedef struct TXtsql_struct TSQL;
#define TSQLPN ((TSQL *)NULL)
struct TXtsql_struct
{
   SERVER *se;                            /* remembered server handle */
   int     idresults;             /* issue results from insert,delete */
   int     doresults;         /* issue results from current statement */
   char   *instmt;                                /* parsed statement */
   char   *outstmt;                        /* output fmt string cache */
   int    *inparamlst;     /* list of parameter FTN_ types - term w/0 */
   int    *outparamlst;    /* list of parameter FTN_ types - term w/0 */
   int    *xinparamlst;                  /* extra data for inparamlst */
   int    *xoutparamlst;                /* extra data for outparamlst */
   void  **inparammem;                       /* mem lst for NCGDIRECT */
   int     istr;                          /* ret field[i-n] as string */
   void   *tx;                                        /* texis handle */
};

#define TSPA struct tspa_struct
#define TSPAPN (TSPA *)NULL
TSPA
{
   TSP     *ts;
   int     (*cb) ARGS((void *usr, void *tx, FLDLST *fl));
   void    *usr;
   void    *tx;
   FLDLST   fl;
};

#define           tsacnt(a) ((a)->fl.n)
extern TSPA *     tsaopen       ARGS((TSP *ts,void *usr,void *cb,void *tx));
extern int        tsareset      ARGS((TSPA *ta));
extern TSPA *     tsaclose      ARGS((TSPA *ta));
extern int        tsacall       ARGS((TSPA *ta));
extern int        tsaout        ARGS((TSPA *ta,int type,void *p,int n));
extern int EXPORT tsarcv        ARGS((TSP *ts));
extern int        tsadrcv       ARGS((void *usr,void *tx,void *fl));
extern int        tsadirect;

#define TSANEEDHINST 0
#define tsahinstance(a) a
#define tsamprocinst(a) ((void *)(a))
#define tsafprocinst(a)

/**********************************************************************/
#include "ntexis.h"

#ifndef TSTONE_C
extern int CDECL  thunderstone  ARGS((void *usr,int (*tstonecb)(void *usr,TEXIS *tx,FLDLST *fl),char *url,...));
#endif
                                                    /* simple SQL api */
extern int  CDECL texis         ARGS((SERVER *unused,char *queryfmt,...));
                                                            /* TX api */
extern TX        *opentx        ARGS((void));
extern TX        *duptx         ARGS((TX *tx));
extern TX * CDECL settx         ARGS((SERVER *unused,TX *tx,char *queryfmt,...));
extern int  CDECL preptx        ARGS((SERVER *unused,TX *tx,char *queryfmt,...));
extern int        paramtx       ARGS((TX *tx,int ipar,void *buf,int *len,int ctype,int sqltype));
extern int        resetparamtx  ARGS((TX *tx));
extern FLDLST    *gettx         ARGS((TX *tx));
extern FLDLST    *getitx        ARGS((TX *tx,int istr));
extern MMOFFS    *offstx        ARGS((TX *tx,char *fldname));
extern int        runtx         ARGS((TX *tx));
extern int        bgtxcheck     ARGS((TX *tx));
extern ft_counter *gettxcounter  ARGS((TX *tx));
                                                          /* TSQL api */
extern TSQL      *opentsql    ARGS((void));
extern TSQL      *duptsql     ARGS((TSQL *ots));
extern TSQL      *closetsql   ARGS((TSQL *ts));
extern int        resulttsql  ARGS((TSQL *ts,int flag));
extern int        settsql     ARGS((TSQL *ts,char *stmt));
extern int        exectsql    ARGS((TSQL *ts,...));
extern int        gettsql     ARGS((TSQL *ts,char *fmt,...));
extern int        dotsql      ARGS((TSQL *ts,char *stmt,...));
extern ft_counter *gettsqlcounter ARGS((TSQL *ts));
extern ft_counter *n_gettsqlcounter ARGS((TSQL	*ts));
extern int newproctsql ARGS((TSQL *ts));
extern int n_newproctsql ARGS((TSQL *ts));
                                                              /* misc */
extern int        regtexiscb    ARGS((void *usr,int (WCBEXPORT *texiscb)(void *usr,TEXIS *sr,FLDLST *fl) ));
extern int        fillsrchlst   ARGS((TEXIS *tx,FLDLST *fl));
extern int	  settexisparam ARGS((int,void *,int *,int,int));
extern FLDLST    *freefldlst    ARGS((FLDLST *fl));
extern void       cleanmmoffs   ARGS((MMOFFS *mo));
extern MMOFFS    *freemmoffs    ARGS((MMOFFS *mo));
extern APICP     *getglobalcp   ARGS((void));
extern APICP     *TXsaveGlobalCp ARGS((void));
extern int        TXrestoreGlobalCp ARGS((APICP *cp));
extern int        tsadirect;
#ifdef NCGDIRECT                                      /* MAW 04-15-94 */
                                                    /* simple SQL api */
#define           n_texis              texis
                                                            /* TX api */
#define           n_opentx(a)          opentx()
#define           n_duptx(a,b)         duptx(b)
#define           n_settx              settx
#define           n_preptx             preptx
#define           n_paramtx(a,b,c,d,e,f,g) paramtx(b,c,d,e,f,g)
/* #define           n_resetparamtx(a,b)  resetparamtx(b) */
#define           n_gettx(a,b)         gettx(b)
#define           n_getitx(a,b,c)      getitx(b,c)
#define           n_offstx(a,b,c)      offstx(b,c)
#define           n_runtx(a,b)         runtx(b)
#define           n_bgtxcheck(a,b)     bgtxcheck(b)
#define		  n_gettxcounter(a,b)  gettxcounter(b)
                                                          /* TSQL api */
#define           n_opentsql(a)        opentsql()
#define           n_duptsql(a)         duptsql(a)
#define           n_closetsql(b)       closetsql(b)
#define           n_resulttsql(b,c)    resulttsql(b,c)
#define           n_settsql(b,c)       settsql(b,c)
#define           n_exectsql           exectsql
#define           n_gettsql            gettsql
#define           n_dotsql             dotsql
                                                              /* misc */
#define           n_regtexiscb(a,b,c)  regtexiscb(b,c)
#define           n_fillsrchlst(a,b,c) fillsrchlst(b,c)
#define           n_settexisparam(a,b,c,d,e,f)    settexisparam(b,c,d,e,f)
#else                                                    /* NCGDIRECT */
                                                    /* simple SQL api */
extern int CDECL  n_texis       ARGS((SERVER *se,char *queryfmt,...));
                                                            /* TX api */
extern TX        *n_opentx      ARGS((SERVER *se));
extern TX        *n_duptx       ARGS((SERVER *se,TX *tx));
extern TX * CDECL n_settx       ARGS((SERVER *se,TX *tx,char *queryfmt,...));
extern int  CDECL n_preptx      ARGS((SERVER *se,TX *tx,char *queryfmt,...));
extern int        n_paramtx     ARGS((SERVER *se,TX *tx,int ipar,void *buf,int *len,int ctype,int sqltype));
/* extern int        n_resetparamtx     ARGS((SERVER *se,TX *tx)); */
extern FLDLST    *n_gettx       ARGS((SERVER *se,TX *tx));
extern FLDLST    *n_getitx      ARGS((SERVER *se,TX *tx,int istr));
extern MMOFFS    *n_offstx      ARGS((SERVER *se,TX *tx,char *fldname));
extern int        n_runtx       ARGS((SERVER *se,TX *tx));
extern int        n_bgtxcheck   ARGS((SERVER *se,TX *tx));
extern ft_counter *n_gettxcounter  ARGS((SERVER *se,TX *tx));
                                                          /* TSQL api */
extern TSQL      *n_opentsql    ARGS((SERVER *se));
extern TSQL      *n_duptsql     ARGS((TSQL *ots));
extern TSQL      *n_closetsql   ARGS((TSQL *ts));
extern int        n_resulttsql  ARGS((TSQL *ts,int flag));
extern int        n_settsql     ARGS((TSQL *ts,char *stmt));
extern int        n_exectsql    ARGS((TSQL *ts,...));
extern int        n_gettsql     ARGS((TSQL *ts,char *fmt,...));
extern int        n_dotsql      ARGS((TSQL *ts,char *stmt,...));
                                                              /* misc */
extern int        n_regtexiscb  ARGS((SERVER *se,void *usr,int (WCBEXPORT *texiscb)(void *usr,TEXIS *sr,FLDLST *fl) ));
extern int        n_fillsrchlst ARGS((SERVER *se,TEXIS *tx,FLDLST *fl));
extern int	  n_settexisparam ARGS((SERVER *,int,void *,int *,int,int));
#endif                                                   /* NCGDIRECT */
                                                      /* internal use */
extern int         tspicounter   ARGS((TSP *ts,ft_counter *p));
extern int         tspocounter   ARGS((TSP *ts,ft_counter *p));
extern ft_counter *tspccounter   ARGS((ft_counter *n,ft_counter *o));
extern int         tspidatetime  ARGS((TSP *ts, ft_datetime *p));
extern int         tspodatetime  ARGS((TSP *ts, ft_datetime *p));
extern int         tspistrlst    ARGS((TSP *ts,ft_strlst **p));
extern int         tspostrlst    ARGS((TSP *ts,ft_strlst **p));
extern ft_strlst  *tspcstrlst    ARGS((ft_strlst *n,ft_strlst *o));
extern int         tspifldlst    ARGS((TSP *ts,FLDLST *fl));
extern int         tspofldlst    ARGS((TSP *ts,FLDLST *fl));
extern FLDLST     *tspcfldlst    ARGS((FLDLST *n,FLDLST *o));

extern SERVER     *VXserver      ARGS((void)); /* PBR 1/Sep/1996  added for Vortex */
extern TX         *VXtexis       ARGS((void)); /* PBR 1/Sep/1996  added for Vortex */



/**********************************************************************/
/* legacy Windows-32 packing: */
#if defined(_WIN32) && !defined(_WIN64)
#  pragma pack(pop,tstone_h)
#endif
#ifdef __cplusplus
}
#endif
#endif                                                    /* TSTONE_H */
