#ifndef TXLICFUNC_H
#define TXLICFUNC_H

typedef int (MONITORFUNC) (void);
typedef enum LICAREA_tag                /* domain */
{
  LICAREA_USER,                         /* user-settable counts */
  LICAREA_LIC,                          /* license counts (internal) */
  LICAREA_NUM
}
LICAREA;
#define LICAREAPN       ((LICAENA *)NULL)

/* License values: below LICVAL_NUMINTS are EPI_HUGEINT, above are strings.
 * NOTE: some of these are in the LICENSE struct too:  KNG 010719
 */
typedef enum LICVAL_tag
{
  /* These are longs: */
  LICVAL_LASTPHONE,			/* Last successful phone home */
  LICVAL_LASTTRYPHONE,			/* Last attempt at phone home */
  LICVAL_VX_FLAGS,			/* Vortex feature flags (LVXF) */
  LICVAL_TX_FLAGS,			/* Texis feature flags */
  LICVAL_MAX_TX_VERSION,                /* max Texis date (release) # */
  LICVAL_MAX_TX_VERSION_NUM,            /* max Texis major.minor.minor limit*/
  LICVAL_MAX_TX_VERSION_NUM_RUN,        /* "" actually run */
  LICVAL_MAX_TX_VERSION_NUM_RUN_PHONE,  /* "" phoned home */
  LICVAL_MAX_FETCHES_PER_DAY,           /* max fetches per day */
  LICVAL_MAX_METAMORPHS_PER_DAY,        /* max setmmapi() calls per day */
  LICVAL_RES6,                          /* reserved placeholder */
  LICVAL_RES7,                          /* reserved placeholder */
  LICVAL_RES8,                          /* reserved placeholder */
  LICVAL_RES9,                          /* reserved placeholder */
  LICVAL_RES10,                         /* reserved placeholder */
  /* These are strings: */
#define LICVAL_NUMINT LICVAL_EQUIV_PATH /* MUST be 1st string value */
  LICVAL_EQUIV_PATH,                    /* path to equivs */
  LICVAL_UEQUIV_PATH,                   /* path to user equivs */
  LICVAL_VORTEX_LOG_PATH,               /* path to Vortex log */
  LICVAL_STATS_PIPE,			/* Named pipe to send stats */
  LICVAL_VERSION,			/* Version string for display */
  LICVAL_DEFAULT_DB,			/* Default Database */
  LICVAL_DEFAULT_SCRIPT,		/* Default Script */
  LICVAL_VIOLATION_MSG,                 /* violation message */
  LICVAL_RES11,                         /* reserved placeholder */
  LICVAL_RES12,                         /* reserved placeholder */
  LICVAL_RES13,                         /* reserved placeholder */
  LICVAL_RES14,                         /* reserved placeholder */
  LICVAL_NUM
}
LICVAL;
#define LICVAL_NUMSTR   (LICVAL_NUM - LICVAL_NUMINT)
#define LIC_MAXSTRSZ    256             /* max string size (including '\0') */

#define LICENSE_MAX_TX_VERSION_NOLIMIT  ((time_t)0x7fffffff)
#define LICENSE_MAX_TX_VERSION_NUM_NOLIMIT      (0x7fffffff)

/* >>> NOTE: if this enum changed/added, check refs to LVXF_NUM in code <<< */
typedef enum LVXF_tag                   /* LICVAL_VX_FLAGS */
{
  LVXF_REQUIRE_TEXIS    = (1 <<  0),    /* require "texis" in URL/name */
  LVXF_WRAPPER_CHECK    = (1 <<  1),    /* CGI wrapper/env check */
  LVXF_NO_TIMPORT       = (1 <<  2),    /* <TIMPORT> disabled */
  LVXF_EXECUTE_ONLY     = (1 <<  3),    /* no compilation allowed */
  LVXF_COMMENT_COPYRIGHT= (1 <<  4),    /* comment copyright at start */
  LVXF_VISIBLE_COPYRIGHT= (1 <<  5),    /* Webinator GIF (c) msg, title */
  LVXF_META_COPYRIGHT   = (1 <<  6),    /* <META> tag copyright */
  LVXF_SYSOBJ_EQUIVS    = (1 <<  7),    /* use db/{SYS|USR}OBJECTS.tbl eqv */
  LVXF_WEBINATOR_TABLES = (1 <<  8),    /* create Webinator tables w/db */
  LVXF_DEF_DB_SRC_PATH  = (1 <<  9),    /* default db from src path */
  LVXF_DUMP_URL_OPTION  = (1 << 10),    /* allow -U option */
  LVXF_WEB_SERVER       = (1 << 11)     /* allow bolton web server */
/* >>> NOTE: if this enum changed/added, check refs to LVXF_NUM in code <<< */
}
LVXF;
#define LVXFPN  ((LVXF *)NULL)


#define TX_LICENSE_FUNCTION_LIST \
I(int,    TXchecklicense, (int type, EPI_HUGEINT arg, char *rejectReasonBuf, size_t rejectReasonBufSz))     \
I(int,    TXlicAllowLicenseCalls, (int yes))     \
I(int,    TXlicIsInitialized, (void))     \
I(int,    TXlic_init, (int server, char *remaddr))     \
I(TXbool, TXlic_deinit, (TXPMBUF *pmbuf))     \
I(int,    TXlic_postinit, (void))     \
I(int,    TXlicIsLicenseMonitor, (void))     \
I(void,   TXlicClearAuxFlds, (void *aux, time_t now, int hitsToo))     \
I(int,    TXlicGetAuxFldsFromShmem, (void *dest))     \
I(int,    TXlicAddFetch, (TXPMBUF *pmbuf))     \
I(int,    TXlicAddMetamorph, (TXPMBUF *pmbuf))     \
I(int,    TXlic_servertick, (void))     \
I(int,    TXlic_refresh, (void))     \
I(int,    TXlic_setshmval, (LICVAL what, EPI_HUGEINT intval, const char *strval))     \
I(int,    TXlic_getshmval, (LICVAL what, EPI_HUGEINT *intval, char *strval))     \
I(EPI_INT64, TXlicGetMaxTableRows, (void))     \
I(EPI_INT64, TXlicGetMaxTableSize, (void))     \
I(int,    TXlic_setskipaddhit, (int skipaddhit))     \
I(int,    TXlic_skipaddhit, (void))     \
I(int,    TXlic_addhit, (void))     \
I(int,    TXlic_removeHit, (void))     \
I(unsigned, TXlic_gethits, (void))     \
I(unsigned, TXlic_resethits, (void))     \
I(long,   TXlic_getmaxtxversionrun, (void))     \
I(long,   TXlic_getmaxtxversionnumrun, (void))     \
I(long,   TXlic_getmaxtxversionrunphone, (void))     \
I(void,   TXlicCopyStatsToSegment, (void *lic, int verbose))     \
I(void,   TXlic_setmonitorfunc, (MONITORFUNC *func))     \
I(void,   TXlic_setMonitorOkArgvExec, (TXbool ok))     \
I(void,   TXlic_addmonitorcbfunc, (MONITORFUNC *func))     \
I(int,    TXlic_newuser, (LICAREA ar, char *who))     \
I(int,    TXlic_setoklicense, (int ok, const char *msg))     \
I(int,    TXlic_pushupdate, (void))     \
I(char*,  TXlic_fmtstats, (int html, int verbose, int fromMonitor, const char *extraInfo))     \
I(char*,  TXlic_getlicstat, (char *what, int *ftnp, char **alloc))     \
I(int,    TXlicUpdateLicenseViaPhoneHome, (void *license))     \
I(int,    TXlicUpdateLicenseFromFileOrBuf, (TXPMBUF *pmbuf, void *license, const char *buf, int okFileNotFound))     \
I(PID_T,  TXlic_getmonpid, (int bak))     \
I(int,    TXlic_gettxflags, (int startmon))     \
I(int,    TXlic_getvxflags, (int startmon))     \
I(void,   TXlic_noanytotx, (void))     \
I(int,    TXlic_okjavascript, (int startmon))     \
I(int,    TXlic_okssl, (int startmon))     \
I(int,    TXlic_okfile, (int startmon))     \
I(int,    TXlic_okdd, (int startmon, CONST char *tbname, CONST DD *dd, int perms))   \
I(int,    TXlic_okwebserver, (int startmon))     \
I(int,    TXlic_getactive, (void))     \
I(int,    TXlic_getmaxactive, (void))     \
I(int,    TXlic_getmaxactivetime, (void))     \
I(int,    TXlic_getnusers, (void))     \
I(int,    TXlic_getmaxallow, (void))     \
I(int,    TXlic_chktex, (LVXF *flagsP, int isConfig, CONST char *s1, CONST char *s2)) \
I(int,    TXlicGetTableLimits, (int startmon, const char *tbname, const DD *dd, int perms, EPI_INT64 *maxTableRows, EPI_INT64 *maxTableSize)) \
I(int,    TXsetPerTableLimitStats, (DBTBL *dbtbl))

typedef struct TX_LICENSE_FUNCTIONS {
#undef I
#define I(ret, func, args)	ret (*func) args;
	TX_LICENSE_FUNCTION_LIST
#undef I
} TX_LICENSE_FUNCTIONS;

/* 0 arg funcs */
#define TXlic_addhit() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_addhit?(TXApp->txLicFuncs->TXlic_addhit()):1))
#define TXlic_removehit() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_removehit?(TXApp->txLicFuncs->TXlic_removehit()):1))
#define TXlic_skipaddhit() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_skipaddhit?(TXApp->txLicFuncs->TXlic_skipaddhit()):0))
#define TXlicGetMaxTableRows() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlicGetMaxTableRows?(TXApp->txLicFuncs->TXlicGetMaxTableRows()):0))
#define TXlicGetMaxTableSize() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlicGetMaxTableSize?(TXApp->txLicFuncs->TXlicGetMaxTableSize()):0))
#define TXlicIsLicenseMonitor() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlicIsLicenseMonitor?(TXApp->txLicFuncs->TXlicIsLicenseMonitor()):0))
#define TXlic_noanytotx() ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_noanytotx?(TXApp->txLicFuncs->TXlic_noanytotx()):0))

/* 1 arg funcs */
#define TXlic_okssl(a) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_okssl?(TXApp->txLicFuncs->TXlic_okssl(a)):1))
#define TXsetPerTableLimitStats(a) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXsetPerTableLimitStats?(TXApp->txLicFuncs->TXsetPerTableLimitStats(a)):0))

/* 2 arg funcs */

/* 3 arg funcs */
#define TXlicClearAuxFlds(a,b,c) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlicClearAuxFlds?(TXApp->txLicFuncs->TXlicClearAuxFlds(a,b,c)):0))
#define TXlic_getshmval(a,b,c) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_getshmval?(TXApp->txLicFuncs->TXlic_getshmval(a,b,c)):0))

/* 4 arg funcs */
#define TXchecklicense(a,b,c,d) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXchecklicense?(TXApp->txLicFuncs->TXchecklicense(a,b,c,d)):0))
#define TXlic_okdd(a,b,c,d) ((TXApp&&TXApp->txLicFuncs&&TXApp->txLicFuncs->TXlic_okdd?(TXApp->txLicFuncs->TXlic_okdd(a,b,c,d)):1))

#endif /* TXLICFUNC_H */
