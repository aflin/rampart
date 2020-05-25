#ifndef DISTDB_H
#define DISTDB_H
/**********************************************************************/
#ifdef NCGDISTRIBUTED                                 /* RSO 03-28-02 */
int CDECL  d_texis       ARGS((SERVER *se,char *queryfmt,...));
                                                            /* TX api */
TX        *d_opentx      ARGS((SERVER *se));
TX        *d_duptx       ARGS((SERVER *se,TX *tx));
TX * CDECL d_settx       ARGS((SERVER *se,TX *tx,char *queryfmt,...));
int        d_paramtx     ARGS((SERVER *se,TX *tx,int ipar,void *buf,int *
len,int ctype,int sqltype));
/* extern int        n_resetparamtx     ARGS((SERVER *se,TX *tx)); */
FLDLST    *d_gettx       ARGS((SERVER *se,TX *tx));
FLDLST    *d_getitx      ARGS((SERVER *se,TX *tx,int istr));
MMOFFS    *d_offstx      ARGS((SERVER *se,TX *tx,char *fldname));
int        d_runtx       ARGS((SERVER *se,TX *tx));
int        d_bgtxcheck   ARGS((SERVER *se,TX *tx));
ft_counter *d_gettxcounter  ARGS((SERVER *se,TX *tx));
                                                          /* TSQL api */
TSQL      *d_opentsql    ARGS((SERVER *se));
TSQL      *d_duptsql     ARGS((TSQL *ots));
TSQL      *d_closetsql   ARGS((TSQL *ts));
int        d_resulttsql  ARGS((TSQL *ts,int flag));
int        d_settsql     ARGS((TSQL *ts,char *stmt));
SERVER *   d_openserver  ARGS((char *host,char *port));
SERVER *   d_openserver  ARGS((char *host,char *port));
SERVER *   d_closeserver  ARGS((SERVER *se));

int  CDECL d_preptx      ARGS((SERVER *se,TX *tx,char *queryfmt,...));
int        d_exectsql    ARGS((TSQL *ts,...));
int        d_gettsql     ARGS((TSQL *ts,char *fmt,...));
int        d_dotsql      ARGS((TSQL *ts,char *stmt,...));
int        d_exectx      ARGS((SERVER *se,TX   *tx));
int        d_flushtx     ARGS((SERVER *se,TX   *tx));
                                                              /* misc */
int        d_regtexiscb  ARGS((SERVER *se,void *usr,int (WCBEXPORT *texiscb)(void *usr,TEXIS *sr,FLDLST *fl) ));
int        d_fillsrchlst ARGS((SERVER *se,TEXIS *tx,FLDLST *fl));
int        d_settexisparam ARGS((SERVER *,int,void *,int *,int,int));
                                                    /* n_ to d_ SQL api */
#ifndef DISTAPI_C                                     /* RSO 03-28-02 */
#define           n_texis            d_texis
                                                    /* distributed TX api */
#define           n_opentx(a)        d_opentx(a)
#define           n_duptx(a,b)       d_duptx(a,b)
#define           n_settx            d_settx
#define           n_preptx           d_preptx
#define           n_paramtx(a,b,c,d,e,f,g) d_paramtx(a,b,c,d,e,f,g)
/* #define           n_resetparamtx(a,b)  resetparamtx(b) */
#define           n_gettx(a,b)       d_gettx(a,b)
#define           n_getitx(a,b,c)    d_getitx(a,b,c)
#define           n_offstx(a,b,c)    d_offstx(a,b,c)
#define           n_runtx(a,b)       d_runtx(a,b)
#define           n_bgtxcheck(a,b)   d_bgtxcheck(a,b)
#define           n_gettxcounter(a,b) d_gettxcounter(a,b)
                                                          /* TSQL api */
#define           n_opentsql(a)      d_opentsql(a)
#define           n_duptsql(a)       d_duptsql(a)
#define           n_closetsql(b)     d_closetsql(b)
#define           n_resulttsql(b,c)  d_resulttsql(b,c)
#define           n_settsql(b,c)     d_settsql(b,c)
#define           n_exectsql         d_exectsql
#define           n_gettsql          d_gettsql
#define           n_dotsql           d_dotsql
#define           n_exectx           d_exectx
#define           n_flushtx          d_flushtx

                                                              /* misc */
#define           n_regtexiscb(a,b,c)  d_regtexiscb(a,b,c)
#define           n_fillsrchlst(a,b,c) d_fillsrchlst(a,b,c)
#define           n_settexisparam(a,b,c,d,e,f)  d_settexisparam(a,b,c,d,e,f)
/* I got these from ntexis.h */
#define           openserver(h,p) d_openserver(h,p)
#define           closeserver(s) d_closeserver(s)
#define           n_setdatabase(a,b) d_setdatabase(a,b)
#endif                                                   /* DISTAPI_C */
#endif                                                   /* NCGDISTRIBUTED */
/**********************************************************************/
#endif                                                    /* DISTDB_H */

