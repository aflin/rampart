/* -=- kai-mode: John -=- */
#ifndef TXLIC_H
#define TXLIC_H
#ifdef __cplusplus
extern "C" {
#endif

#include "texint.h"

/* License checking code
 *
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE: <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >> Changing things here or elsewhere in the license code can affect the <<
 * >> format of the shared mem segment.  Change LIC_SHMKEY and LIC_VERSION <<
 * >> if the format changes.                                               <<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */

/* Updated: KNG 961101 971218 980921 981221 990120 010425 010509 010719 */
/* KNG also 040509 but same key/version */
/* NOTE: LIC_SHMKEY must be incremented by 1 each revision, do not make up
 * a completely new key (TXlic_getmonpid() depends on it):
 */
#ifndef LIC_SHMKEY
#  define LIC_SHMKEY    0xDBACCEE5      /* shared mem segment ID */
#endif
#define LIC_SHMFMAP     "TEXIS_SCRIPT_USER_LOG2"  /* more NT-land stuff */
#define LIC_VERSION     0x20010719      /* version number of mem segment */

/* This is no longer used; keep it the same for delete: */
#define LIC_SHMFN       "texisscv.dll"  /* mem segment filename under NT */

/* ----------------------- Config (also in Makefile) ----------------------- */
/* LIC_STATS                    keep license stats, even if no checking
 * LIC_PHONE_HOME               phone home to check license
 * LIC_BKGND                    phone home in background (fork)
 * LIC_NO_BKGND                 (debug) override LIC_BKGND
 * LIC_URL                      (debug) phone-home URL string
 * LIC_TEST                     debug tests
 * LIC_APPROVE_PENDING          allow users while waiting during phone home,
 *                                if check is likely to succeed
 * LIC_ALWAYS_APPROVE           always "approve" users, even if check fails,
 *                                and don't print any error messages
 *                                (non-Vortex progs must silence putmsg())
 * LIC_ACTIVE_TIME              max idle time for an active users (seconds)
 * LIC_MAX_PHONE_TIME           max time between phone-home checks (seconds)
 * LIC_MIN_PHONE_TIME           min time between phone-home checks (seconds)
 * LIC_MIN_PHONE_TIME_NOINC     "" if previous check didn't inc license
 */
#if defined(LIC_NO_BKGND)
#  undef LIC_BKGND
#else
#  define LIC_BKGND
#endif
#define LIC_APPROVE_PENDING
#ifdef LIC_PHONE_HOME
#  define LIC_STATS                     /* LIC_PHONE_HOME implies LIC_STATS */
#endif
#if defined(LIC_STATS) && !defined(LIC_PHONE_HOME)
#  define LIC_ALWAYS_APPROVE            /* in case license slots run out */
#endif
#define LIC_SET_NOCACHE         /* set Cache-Control: no-cache header */
#define LIC_ACTIVE_TIME                 60
#ifndef LIC_MAX_PHONE_TIME
#  define LIC_MAX_PHONE_TIME            (24*60*60)
#endif
#ifndef LIC_MIN_PHONE_TIME
#  define LIC_MIN_PHONE_TIME            (5*60)
#endif
#ifndef LIC_MIN_PHONE_TIME_NOINC
#  define LIC_MIN_PHONE_TIME_NOINC      (60*60)
#endif
#ifndef LIC_FETCH_WAIT
#  define LIC_FETCH_WAIT  25    /* fetch timeout per URL when phoning home */
#endif
#define LIC_MON_TICK    30      /* time between monitor heartbeats */
/* ----------------------------- End config -------------------------------- */

#define LVXF_DEFAULT    (0      \
  | LVXF_REQUIRE_TEXIS          \
  | LVXF_WRAPPER_CHECK          \
  | LVXF_COMMENT_COPYRIGHT      \
  | LVXF_VISIBLE_COPYRIGHT      \
  | LVXF_META_COPYRIGHT         \
)

/* note: although LVXF_WEBINATOR_TABLES is deprecated in v5+ if schemas,
 * set it for schema-restricted licenses for back/forward compatibility:
 */
/* KNG 20040930 added web server for Windows ISAPI: */
#define LVXF_WEBINATOR_DEFAULT    (0      \
  | LVXF_REQUIRE_TEXIS          \
  | LVXF_WRAPPER_CHECK          \
  | LVXF_COMMENT_COPYRIGHT      \
  | LVXF_VISIBLE_COPYRIGHT      \
  | LVXF_META_COPYRIGHT         \
  | LVXF_WEBINATOR_TABLES	\
  | LVXF_WEB_SERVER             \
)

/* >>> NOTE: if this enum changed/added, check refs to TX_LIC_NUM in code <<< */
typedef enum TX_LIC_tag                 /* LICVAL_TX_FLAGS */
{
  TX_LIC_CREATE_TABLE   = (1 <<  0),
  TX_LIC_CREATE_INDEX   = (1 <<  1),
  TX_LIC_CREATE_TRIGGER = (1 <<  2),
  TX_LIC_DELETE         = (1 <<  3),
  TX_LIC_UPDATE         = (1 <<  4),
  TX_LIC_INSERT         = (1 <<  5),
  TX_LIC_SELECT         = (1 <<  6),
  TX_LIC_GRANT          = (1 <<  7),
  TX_LIC_REVOKE         = (1 <<  8),
  TX_LIC_VIOL_NO_CREATE = (1 <<  9),    /* no creatdb if license violation */
  TX_LIC_NO_ANYTOTX     = (1 << 10),    /* on: anytotx not allowed */
  TX_LIC_DISTRIB_TX     = (1 << 11),    /* on: distributed texis enabled */
  TX_LIC_JAVASCRIPT     = (1 << 12),    /* on: allow JavaScript plugin */
  TX_LIC_SSL            = (1 << 13),    /* on: allow https plugin */
  TX_LIC_FILE           = (1 << 14),    /* on: allow file:// URLs */
  TX_LIC_SCHEMA_RESTRICT= (1 << 15)     /* on: restrict table schemas */
/* >>> NOTE: if this enum changed/added, check refs to TX_LIC_NUM in code <<< */
}
TX_LIC;

#define TX_LIC_DEFAULT  (0      \
  | TX_LIC_CREATE_TABLE         \
  | TX_LIC_CREATE_INDEX         \
  | TX_LIC_CREATE_TRIGGER       \
  | TX_LIC_DELETE               \
  | TX_LIC_UPDATE               \
  | TX_LIC_INSERT               \
  | TX_LIC_SELECT               \
  | TX_LIC_GRANT                \
  | TX_LIC_REVOKE               \
  | TX_LIC_NO_ANYTOTX           \
)

/* note that TX_LIC_CREATE_TABLE is ignored in v5+ if schema-restrict,
 * so that we don't have to turn it on to create tables, which would let
 * v4- create any table:
 */
#define TX_LIC_WEBINATOR_DEFAULT  (0      \
  | TX_LIC_CREATE_INDEX         \
  | TX_LIC_DELETE               \
  | TX_LIC_UPDATE               \
  | TX_LIC_INSERT               \
  | TX_LIC_SELECT               \
  | TX_LIC_GRANT                \
  | TX_LIC_REVOKE               \
  | TX_LIC_NO_ANYTOTX           \
  | TX_LIC_SSL                  \
  | TX_LIC_SCHEMA_RESTRICT      \
)

#define LIC_NUMDAILY    30      /* #days hits to save */

/* >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> NOTE: <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 * >> Try to keep sizes the same if compiling different versions on the  <<
 * >> same box (32/64-bit shmem compat. e.g. Solaris 2.6/2.8).           <<
 * >> Some historical compatibility issues though:  KNG 020506           <<
 * >> SEE ALSO DATASIZE in texint.h                                      <<
 * >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>><<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
 */
#ifdef __alpha
/* historical back-compatibility: */
typedef EPI_UINT64	LIC_SIZE_T;	/* deprecated */
typedef EPI_INT64	LIC_LONG;	/* deprecated */
typedef EPI_INT64       LIC_PTR;        /* deprecated */
#else /* !__alpha */
/* constant-size on all other platforms: */
typedef EPI_INT32	LIC_SIZE_T;	/* deprecated */
typedef EPI_INT32	LIC_LONG;	/* deprecated */
typedef EPI_INT32       LIC_PTR;        /* deprecated */
#endif /* !__alpha */

#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
/* Texis v7.1 TXLICAUX fields: */
typedef struct TXLICAUX_TBLSTATS_tag
{
	/* These are copied from `DbsizeList' by dbsizescan(): */
	EPI_INT64	fullestTableRows;	/* -1: unset */
	/* `...Limit's are either per-schema or license-wide,
	 * whichever is applicable to `fullestTable...':
	 */
	EPI_INT64	fullestTableRowsLimit;	/* -1: unset */
	EPI_INT64	fullestTableSize;	/* -1: unset */
	EPI_INT64	fullestTableSizeLimit;	/* -1: unset */
	/* wtf storing db and table names eats up shmem;
	 * also note that table/db names may be truncated
	 * (somewhat intelligently) by TXcopyPathTruncated():
	 * Order is TXLIC_FTSTR enum:
	 */
	char		fullestTablesStrings[256];
}
TXLICAUX_TBLSTATS;
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */

#ifdef EPI_LICENSE_6
/* New fields added to LICENSE struct, except for the shmem segment
 * where they are stored separately (from LICENSE_V5) to maintain
 * v5/v6 shmem-attachability:
 */
typedef struct TXLICAUX_tag
{
	/* time_t that ...ThisMinute counts started.  If 0, we know
	 * that EPI_LICENSE_6 fields were just zeroed (e.g. by
	 * pre-EPI_LICENSE_6 Texis).  Generally updated to
	 * on-the-minute (nn:nn:00) value:
	 */
	EPI_INT32	thisMinuteStart;

	EPI_INT32	fetchesToday;		/* network <fetch>es today */
	EPI_INT32	fetchesTodayStart;	/* when "today" started */
	/* Network <fetch>es since `thisMinuteStart', e.g. this minute: */
	EPI_INT32	fetchesThisMinute;
	/* Exponential moving average of <fetch>es/minute over last
	 * `TXlicRateMinutes' (3/15/60) minutes:
	 */
#  define TXLIC_NUM_RATES	3
	EPI_FLOAT64	fetchRates[TXLIC_NUM_RATES];

	EPI_INT32	metamorphsToday;	/* setmmapi() calls today */
	EPI_INT32	metamorphsTodayStart;	/* when "today" started */
	/* setmmapi() calls since `thisMinuteStart': */
	EPI_INT32	metamorphsThisMinute;
	/* exponential moving averages: */
	EPI_FLOAT64	metamorphRates[TXLIC_NUM_RATES];

	/* KNG20130828 New fixed fields can be appended to TXLICAUX
	 * (until sizeof(TXLICAUX) > sizeof(LICSEG.auxUnion.padding),
	 * which will then cause shmem to change size; checked at
	 * build time during ALIGNMENT_TEST in txlicense.c).
	 * v5?/v6/v7.0 license monitors would have cleared these
	 * fields on shmem init (as part of segment init), and would
	 * not have saved them in license.key.  v7.1 monitor will yap
	 * about `License file fixed data too long/short' when reading
	 * v5/v6/v7.0 license.key, and vice versa; harmless (but old
	 * rev will of course lose new rev's data).
	 */
#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
	TXLICAUX_TBLSTATS	tableStats;
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */
}
TXLICAUX;
#define TXLICAUXPN	((TXLICAUX *)NULL)
#endif /* EPI_LICENSE_6 */

/* TXLIC_FTSTR: order of strings in `fullestTablesStrings':
 * NOTE: see compaction in dbsizescan() if this enum changes in any way:
 */
typedef enum TXLIC_FTSTR_tag
{
	/* Put tables first; we know they are DDNAMESZ max: */
	TXLIC_FTSTR_ROWS_TABLE,
	TXLIC_FTSTR_SIZE_TABLE,
	/* rows db before size db: we care about former more than latter: */
	TXLIC_FTSTR_ROWS_DB,
	TXLIC_FTSTR_SIZE_DB,
	TXLIC_FTSTR_NUM				/* must be last */
}
TXLIC_FTSTR;

char *TXfindStrByIdx(const char *buf, size_t bufSz, size_t idx);

/* LICENSE[_V5] initial struct fields: */
#define LICENSE_FIELDS() \
        LIC_SIZE_T fixedsz;     /* LICENSE_FIXEDSZ; must be first */ \
	EPI_INT32  generated;	/* When the license was generated */ \
	EPI_INT32  firstuse;	/* First use time */ \
	EPI_INT32  expires;	/* Time the license expires */ \
	EPI_INT32  duration;	/* How long license lasts (from firstuse)*/ \
	EPI_INT32  violatetime;	/* Violation time while getting new license*/\
	EPI_INT32  maxnophone;	/* Max time to not report home */ \
	EPI_INT32  minnophone;	/* Min time between home calls */ \
	EPI_INT32  defphone;	/* Default time between home calls */ \
	DATASIZE maxtrows;	/* Max number of rows per table */ \
	DATASIZE maxdrows;	/* Max number of rows per database */ \
	DATASIZE maxrows;	/* Max total number of rows */ \
	DATASIZE maxtsize;	/* Max table size */ \
	DATASIZE maxdsize;	/* Max database size */ \
	DATASIZE maxsize;	/* Max total size */ \
	LIC_LONG maxhits;	/* Max hits per day */ \
	LIC_LONG phone;		/* Flag whether to try phoning home */ \
	LIC_LONG viol;		/* Flag whether to ignore violation */ \
	LIC_LONG serial;	/* Serial number of license */ \
	LIC_LONG crc_unused;	/* CRC on license (deprecated/not used) */ \
	LIC_LONG iplock;	/* IP address we are locked to */ \
	LIC_LONG ipmask;	/* Mask of significant bits in iplock */ \
	LIC_LONG old_vxflags;	/* placeholder for old versions */ \
	LIC_LONG old_txflags;	/* placeholder for old versions */ \
	char	version[40];	/* Version to display */ \
/* Most stuff below here gets updated during normal operation, the first */ \
/* section defines the license. */ \
	EPI_INT32 lastinit;	/* Time of last successful phone home */ \
	EPI_INT32 old_lastphone;/* placeholder for old versions */ \
	EPI_INT32 old_lasttryphone;/* placeholder for old versions */ \
	EPI_INT32 lastwrite;	/* Time we last wrote the license file */ \
	EPI_INT32 violationtime;/* What time a violation occurred */ \
	EPI_INT32 hitsreset;	/* When did we reset hits */ \
	LIC_LONG  hits;		/* Hits so far today */ \
	LIC_LONG  highhits;	/* Most hits per day we've seen */ \
	LIC_LONG  hostnum;	/* Local IPv4 (network-order memcpy) */ \
	EPI_INT32 hostmoved;	/* Number of times it's moved */ \
	DATASIZE  cumhits;	/* Cumulative hits, since init firstuse */ \
	DATASIZE  ltsize;	/* Largest table we have */ \
	DATASIZE  ldsize;	/* Largest database we have */ \
	DATASIZE  lssize;	/* Cum. database size */ \
	DATASIZE  ltrows;	/* Rows: Largest table we have */ \
	DATASIZE  ldrows;	/* Rows: Largest database we have */ \
	DATASIZE  lsrows;	/* Rows: Cum. database size */ \
	LIC_LONG  dailyhits[LIC_NUMDAILY]; \
	EPI_INT32 dailyreset[LIC_NUMDAILY]; \
	EPI_INT32 maxtxversionrun; /* highest version # run in last 30 sec.*/\
	EPI_INT32 maxtxversionrunphone;/* highest version # run since phone*/\
	/* Use long not EPI_HUGEINT: no change during 32 -> 64-bit compiles: */ \
	LIC_LONG intval[LICVAL_NUMINT];	/* integer values */ \
	/* these are only in the file, to preserve v4 -> v5 shmem attach: */ \
	/* KNG 20090623 Bug 2493 these are now declared sometimes; */ \
	/* see LICENSE vs. LICENSE_V5 below: */ \
	/* EPI_INT32	zero_v4; */ \
	/* EPI_INT32	crc */	/* LICENSE_CRC_OFF points here */ \
 \
	/* >>>>>>NOTE: if new fields are added, see data2license etc. <<< */\
	/* They should copy up to LICENSE_FIXEDSZ_VER4, not fsz-ZEROCRC_SZ,*/\
	/* and then copy the new fields here. */ \
 \
  /* New fixed fields go here, before LICENSE_VARFLD. This ensures previous*/\
  /* struct fields are mapped ok from old license.key files.  KNG 990302 */ \
  /* KNG 20090625 actually, add new fixed fields to TXLICAUX, but watch */ \
  /* that it does not exceed auxUnion.padding size in txlicense.c */ \
  /* (checked at build time during ALIGNMENT_TEST in txlicense.c) */ \
 \
/* The real pointers are now past the end of the struct (see LICENSEUBER);*/ \
/* these are placeholders for back-compatibility, and constant-size so we */ \
/* can keep a constant-sized struct for shmem.  (Note that in the shmem */ \
/* segment other stuff occurs after this struct, so we can't refer to */ \
/* dbsize/filename there): */ \
	/* these moved to after LICENSE_FIELDS() call, for Windows, */ \
	/* since MVSC cannot handle a comment for a macro arg: */ \
	/* LIC_PTR	dbsize_dum; */	/* Data about databases */ \
	/* LIC_PTR	filename_dum */	/* Filename of license file */

/* A LICENSE object contains all data that is saved to the license.key file.
 * A closely-related LICENSE_V5 struct (LICENSE sans zero_v4/crc/TXLICAUX)
 * is part of the shared-mem LICSEG struct (see texis/txlicense.c);
 * they are distinct now so that shmem-version LICENSE_V5 is still
 * shmem-attachable by v5-:
 */
typedef struct LICENSE_tag
{
#if TX_LICVER5 > 0
#  define LICENSE_FIXEDSZ_VER4	((size_t)&LICENSEPN->intval[LICVAL_NUMINT])
#endif /* TX_LICVER5 > 0 */
#define LICENSE_VARFLD  dbsize_dum  /* first variable-sized (in file) field */
	LICENSE_FIELDS()
	EPI_INT32	zero_v4;
	EPI_INT32	crc;
#ifdef EPI_LICENSE_6
	TXLICAUX	aux;
#endif /* !EPI_LICENSE_6 */
	LIC_PTR		dbsize_dum;		/* Data about databases */
	LIC_PTR		filename_dum;		/* Filename of license file */
#define LICENSE_CRC_OFF		((size_t)&LICENSEPN->crc)
}
LICENSE;
#define LICENSEPN       ((LICENSE *)NULL)
/* The size of fixed (file) fields in the LICENSE struct: */
#define LICENSE_FIXEDSZ \
  ((char *)&LICENSEPN->LICENSE_VARFLD - (char *)LICENSEPN)

/* A LICENSE_V5 object is a v5- LICENSE, i.e. without (file-only)
 * `zero_v4', `crc' or TXLICAUX fields, for v5/v6 shmem back-compatibility:
 */
typedef struct LICENSE_V5_tag
{
	LICENSE_FIELDS()
	LIC_PTR		dbsize_dum;		/* Data about databases */
	LIC_PTR		filename_dum;		/* Filename of license file */
}
LICENSE_V5;
#define LICENSE_V5PN	((LICENSE_V5 *)NULL)

#define LICENSE_V5_FIXEDSZ \
  ((char *)&LICENSE_V5PN->LICENSE_VARFLD - (char *)LICENSE_V5PN)

typedef enum TXlicAwsStatus_tag
{
	/* ...Unknown must be 0 so calloc() at open sets it: */
	TXlicAwsStatus_Unknown = 0,
	TXlicAwsStatus_Invalid,
	TXlicAwsStatus_Valid,
}
TXlicAwsStatus;

typedef struct LICENSEUBER_tag	/* this is what is calloc'd */
{
	LICENSE	lic;
	char	*dbsize;
	char	*filename;	/* not stored in license.key */
#if TX_LICVER5 > 0
	char	*schemas;	/* permitted table schemas (string form) */
#endif /* TX_LICVER5 */
#ifdef EPI_LICENSE_AWS
	char		*systemAwsProductCode;
	char		*requiredAwsProductCodes; /*space/CSV product codes*/
	TXlicAwsStatus	awsStatus;
#endif /* EPI_LICENSE_AWS */
	/* We only care about IPv6 address to send it in phone-home data;
	 * no host-move or network mask checking done.  So put it in
	 * alloc'd LICENSEUBER, but not in file nor shmem data:
	 */
	TXsockaddr	*myIPv6;		/* optional, if known */
/* New variable fields go here, in order.  This ensures correct loading
 * from old license.key files.   KNG 040422
 */
}
LICENSEUBER;
#define LICENSEUBERPN	((LICENSEUBER *)NULL)

#define LICENSEUBER_FIELD(lic, fld)	(((LICENSEUBER *)(lic))->fld)
#define LICDBSIZE(lic)		LICENSEUBER_FIELD(lic, dbsize)
#define LICFILENAME(lic)	LICENSEUBER_FIELD(lic, filename)
#define LICSCHEMAS(lic)		LICENSEUBER_FIELD(lic, schemas)
#define LICALLOCSZ		(sizeof(LICENSEUBER))

/* License modes when opening */
#define LICENSE_GENDEF	1	/* Generate a default license */

#ifndef TX_NO_HIDE_LIC_SYM
#  define openlicense txx_setcpy
#  define closelicense txx_unsetcpy
#  define writelicense txx_mvdoll
#  define TXlicCheckIfHostMoved txx_cpstrlst
#  define TXlicCheckIfInViolation txx_insdoll
#  define TXlicGetLicUpdAcceptUrlPath	tx_rotatebits
#  define TXlicGetSystemAwsProductCode		tx_mogrifybits
#  define TXlicGetRequiredAwsProductCodes	tx_andbits
#endif /* !TX_NO_HIDE_LIC_SYM */

typedef enum TX_LIC_FLUSH_tag
  {
    TX_LIC_FLUSH_NONE,                  /* no flushing */
    TX_LIC_FLUSH_DATA,                  /* flush data to disk */
    TX_LIC_FLUSH_ALL,                   /* flush data and metadata to disk */
    TX_LIC_FLUSH_ALL_POSSIBLE           /* data and/or meta, if possible */
  }
TX_LIC_FLUSH;

LICENSE *openlicense(const char *filename, int mode, TX_LIC_FLUSH flushVal,
		     const TXsockaddr *myIPv4, const TXsockaddr *myIPv6);
LICENSE *closelicense ARGS((LICENSE *));
int writelicense(LICENSE *license, TX_LIC_FLUSH flushVal);
int TXlicCheckIfHostMoved(LICENSE *license, const TXsockaddr *myIPv4);
int TXlicCheckIfInViolation(LICENSE *license, time_t now, char *msgBuf,
			    size_t msgBufSz);
size_t TXlicGetLicUpdAcceptUrlPath(char *buf, size_t sz);
#ifdef EPI_LICENSE_AWS
size_t TXlicGetSystemAwsProductCode(LICENSE *license, char *buf, size_t sz);
size_t TXlicGetRequiredAwsProductCodes(LICENSE *license, char *buf, size_t sz);
#endif /* EPI_LICENSE_AWS */

extern int TXlicTraceDns;

#define LIC_EXPIRE_TIME		1
#define LIC_TABLE_SIZE		2
#define LIC_CREATE_DB		3
#define LIC_MAX_TX_VERSION	4
#define LIC_DISTRIB_TX		5
#define LIC_MAX_TX_VERSION_NUM  6

#define LIC_SUCCESS	0
#define LIC_FAIL	-1
#define LIC_SUCCESS_WITH_INFO	1

#define MONITORFUNCPN   ((MONITORFUNC *)NULL)

#if !defined(TX_DEBUG) && !defined(TX_NO_HIDE_LIC_SYM)
#  define TXlic_init txx_mvifnew
#  define TXlicGetAuxFldsFromShmem txx_freerow2
#  define TXlicAddFetch txx_ejectData
#  define TXlicAddMetamorph txx_startData
#  define TXlicRateMinutes txx_renewData
#  define TXlic_postinit txx_updifnew
#  define TXlic_servertick txx_insifnew
#  define TXlic_refresh txx_freeze
#  define TXlic_setshmval txx_updaterow
#  define TXlic_setskipaddhit txx_deleterow
#  define TXlic_removeHit txx_refreerow
#  define TXlic_gethits txx_mergerow
#  define TXlic_resethits txx_voidrow
#  define TXlic_getmaxtxversionrun txx_fliprow
#  define TXlic_getmaxtxversionnumrun txx_cliprow
#  define TXlic_getmaxtxversionrunphone txx_clip2row
#  define TXlicCopyStatsToSegment txx_kneadrow
#  define TXlic_setmonitorfunc txx_internal2english
#  define TXlic_setMonitorOkArgvExec txx_english2internal
#  define TXlic_addmonitorcbfunc txx_externaltrans
#  define TXlic_newuser txx_publishrow
#  define TXlic_setoklicense txx_cmpcpy
#  define TXlic_pushupdate txx_depublishrow
#  define TXlic_fmtstats txx_indexrow
#  define TXlic_getlicstat txx_reindexrow
#  define TXlicUpdateLicenseViaPhoneHome txx_gtblrow
#  define TXlicUpdateLicenseFromFileOrBuf txx_ptblrow
#  define TXlic_getmonpid txx_settimeofdayaux
#  define TXlic_gettxflags txx_cvttimeofday
#  define TXlic_getvxflags txx_altauxinit
#  define TXlic_okjavascript txx_getdynabyte
#  define TXlicOkSsl  txx_resetdynabyte
#  define TXlic_okfile txx_stdget
#  define TXlic_okwebserver txx_stdput
#  define TXlic_getactive txx_gmt
#  define TXlic_getmaxactive txx_local
#  define TXlic_getmaxactivetime txx_cvtaux
#  define TXlic_getnusers txx_setaux
#  define TXlic_getmaxallow txx_auxget
#  define TXlic_chktex TXelightbar
#  define TXlicGetTableLimits tx_loadtbl
#  define TXlic_array2schemas tx_maxeth
#endif

extern CONST int	TXlicRateMinutes[TXLIC_NUM_RATES];
extern int      TXlicOkSsl;
extern int	TXlicMsgs;

/* public for Vortex functions: */
char    **TXlic_loguser ARGS((int argc, char ***argv));
char    **TXlic_userstats ARGS((int argc, char ***argv));
char    **TXlic_resetstats ARGS((char **ipaddr));


#ifndef HTBUFPN
typedef struct HTBUF_tag        HTBUF;          /* defined in httpi.h */
#  define HTBUFPN ((HTBUF *)NULL)
/* Note: primary definition in cgi.h; defined here just to avoid #include */
#endif /* !HTBUFPN */

HTBUF  *TXlic_array2schemas ARGS((void));
int	TXlic_okdd ARGS((int startmon, CONST char *tbname, CONST DD *dd,
			int perms));
extern CONST char       TxLicUpdEventName[], TxLicCtrEventName[];

#define TXLIC_MAX_TABLE_ROWS_STAT_NAME  "MAXTABLEROWS"
#define TXLIC_MAX_TABLE_SIZE_STAT_NAME  "MAXTABLESIZE"

/* Tokens for <vxcp applylicense> failures; public/std for scripts/I18N: */
#define TX_APPLY_LICENSE_REASON_SYMBOLS_LIST    \
I(Ok)                                           \
I(BadBindAddress)                               \
I(FetchError)                                   \
I(LicenseServiceDisabled)                       \
I(SecureConnectionRequired)                     \
I(Unauthorized)                                 \
I(InvalidLicense)                               \
I(InternalServerError)                          \

typedef enum TXALR_tag
{
#undef I
#define I(tok)  TXALR_##tok,
TX_APPLY_LICENSE_REASON_SYMBOLS_LIST
#undef I
  TXALR_NUM                                     /* must be last */
}
TXALR;
#define TXALRPN ((TXALR *)NULL)

extern CONST char * CONST       TxAlrTokenStr[TXALR_NUM];

int dbsizeadd ARGS((DBTBL *dbtbl, char *db));
int dbsizescan ARGS((LICENSE *license, time_t exptime, int summary));

/******************************************************************/

#ifdef __cplusplus
}
#endif
#endif /* !TXLIC_H */
