#ifndef HTPF_H
#define HTPF_H
#include "mmsg.h"

/* ---------------------- in htprintf.c:  -KNG 960116 ---------------------- */

/* Solaris uses __va_list instead of va_list for vfprintf(),
 * and gcc typedefs it differently than va_list... argg..:   -KNG 970707
 */
#if defined(sun) && defined(__SVR4)
#  define HTPF_SYS_VA_LIST      __va_list
#else
#  define HTPF_SYS_VA_LIST      va_list
#endif

void TXhtpfFileCb ARGS((void *data, char *s, size_t sz));

typedef struct HTCSCFG_tag HTCSCFG;             /* charset config object */
#define HTCSCFGPN       ((HTCSCFG *)NULL)

typedef struct HTPFOBJ {
  int traceEncoding;
  TXbool charsetmsgs;
  TXbool do8bit;
  TXbool Utf8BadEncAsIso88591;
  TXbool Utf8BadEncAsIso88591Err;
  TXPMBUF *pmbuf;
  HTCSCFG *charsetconfig;
  int     htpferrno;
} HTPFOBJ;
#define HTPFOBJPN ((HTPFOBJ *)NULL)

HTPFOBJ *closehtpfobj(HTPFOBJ *);
HTPFOBJ *duphtpfobj(HTPFOBJ *);
HTCSCFG *htpfgetcharsetconfigobj(HTPFOBJ *);
TXPMBUF *htpfgetpmbuf(HTPFOBJ *);

#ifdef HTPF_STRAIGHT
int CDECL htpf ARGS((CONST char *fmt, ...));
int CDECL htfpf ARGS((FILE *fp, CONST char *fmt, ...));
int htvfpf ARGS((FILE *fp, CONST char *fmt, HTPF_SYS_VA_LIST argp));
SPFRET_TYPE CDECL htspf ARGS((char *buf, CONST char *fmt, ...));
int htvspf ARGS((char *buf, CONST char *fmt, HTPF_SYS_VA_LIST argp));
#else	/* !HTPF_STRAIGHT */
error; this will likely break as Linux 2.0 gcvt() calls sprintf
error; and other functions may recurse too
#  define htpf		printf
#  define htfpf		fprintf
#  define htvfpf	vfprintf
#  define htspf		sprintf
#  define htvspf	vsprintf
#  define htvsnpf	vsnprintf
#  if (defined(hpux) && defined(__LP64__))
int CDECL snprintf ARGS((char *buf, size_t sz, char *fmt, ...));
#  else /* !(hpux && __LP64__) */
int CDECL snprintf ARGS((char *buf, size_t sz, CONST char *fmt, ...));
#  endif /* !(hpux && __LP64__) */
#endif	/* !HTPF_STRAIGHT */
/* these don't exist on most platforms: */
/* KNG 020609 leave htsnpf() straight always, so dlopen() under AIX
 * gets our version, not C lib snprintf():
 */
int CDECL htsnpf ARGS((char *buf, size_t sz, CONST char *fmt, ...));
#if defined(va_list) || defined(_STDARG_H) || defined(_STDARG_H_) || defined(__STDARG_H__) || defined(_INC_STDLIB) || defined(_VA_LIST) || defined(_sys_varargs_h)
#  define CGI_H_GOT_VA_LIST
#else
#  undef CGI_H_GOT_VA_LIST
#endif

/* KNG 960912: */
/* Type of arg expected by HTPFCB callback: */
typedef enum HTPFT_tag
{
  HTPFT_VOID,           /* nothing to return */
  HTPFT_UNSIGNED,
  HTPFT_INT,
  HTPFT_INTPTR,         /* ie. %n */
  HTPFT_LONG,
  HTPFT_ULONG,
  HTPFT_DOUBLE,
  HTPFT_CHAR,
  HTPFT_STR,
  HTPFT_TIME,
  HTPFT_PTR,            /* %p   */
  HTPFT_LONGLONG,       /* KNG 981001 */
  HTPFT_ULONGLONG,      /* KNG 981001 */
  HTPFT_HUGEINT,        /* KNG 010312 */
  HTPFT_HUGEUINT,       /* KNG 010312 */
  HTPFT_LONGDOUBLE,
  HTPFT_HUGEFLOAT,
  HTPFT_NUM             /* last */
}
HTPFT;
/* What arg is: */
typedef enum HTPFW_tag
{
  HTPFW_START,          /* start of format string */
  HTPFW_FMTERR,         /* bad format code */
  HTPFW_WIDTH,          /* %*.  width */
  HTPFW_PREC,           /* %.*  precision */
  HTPFW_QUERY,          /* %m   Metamorph query */
  HTPFW_QUERYBUF,       /* %m   Metamorph search buffer */
  HTPFW_BINARY,         /* %b    */
  HTPFW_OCTAL,          /* %o    */
  HTPFW_CHAR,           /* %c    */
  HTPFW_DECODECHAR,     /* %!c    */
  HTPFW_INT,            /* %d %i */
  HTPFW_UNSIGNED,       /* %u    */
  HTPFW_HEX,            /* %x %X */
  HTPFW_STR,            /* %s    */
  HTPFW_E,              /* %e    */
  HTPFW_F,              /* %f    */
  HTPFW_G,              /* %g    */
  HTPFW_NPRINTED,       /* %n    */
  HTPFW_PTR,            /* %p    */
  HTPFW_ROMAN,          /* %r %R */
  HTPFW_TIMEFMT,        /* %a   format for %t */
  HTPFW_LOCAL,          /* %t    */
  HTPFW_GMT,            /* %T    */
  HTPFW_KSTR,           /* K flag for %d/%i/%f */
  HTPFW_PREX,           /* P flag: REX expr for Metamorph para markup */
  HTPFW_FRAC,           /* %F   double as fraction */
  HTPFW_PREPLACESTR,    /* PP flag: replacement string */
  HTPFW_LATITUDE,       /* %[-]L */
  HTPFW_LONGITUDE,      /* %|L */
  HTPFW_LOCATION,       /* %+L */
  HTPFW_NUM             /* last  */
}
HTPFW;
/* htpfengine() flags: */
typedef enum HTPFF_tag
{
  HTPFF_PROMOTE_TO_HUGE = (1 << 0),     /* promote ints/doubles to huge */
  HTPFF_RDHTPFARGS      = (1 << 1),     /* read HTPFARG array for args */
  HTPFF_WRHTPFARGS      = (1 << 2),     /* write args to HTPFARG array */
  HTPFF_NOOUTPUT        = (1 << 3)      /* suppress output */
}
HTPFF;
#define HTPFFPN ((HTPFF *)NULL)

typedef struct TXFMTCP_tag                      /* <fmtcp> settings */
{
  APICP         *apicp;                         /* (opt.) owned; dup'd 4 use*/
  HTPFOBJ       *htpfobj;                         /* (opt.) */
  size_t        querySetCycleNum;               /* 0 == no limit */
  char          *queryStyle;                    /* (opt.) query style */
  char          **querySetStyles;               /* (opt.) queryset styles */
  int           numQuerySetStyles;              /* "" array length (>= 1) */
  char          *queryClass;                    /* (opt.) "query" class */
  char          **querySetClasses;              /* (opt.) "queryset" classes*/
  int           numQuerySetClasses;             /* "" array length (>= 1) */
  byte          highlightWithinDoc;             /* nonzero: do w/doc too */
  byte          queryFixupMode;
  byte          htpfobjDupOwned;                /* bit 1: dup@dup  0: owned */
}
TXFMTCP;
#define TXFMTCPPN       ((TXFMTCP *)NULL)

extern CONST char       TxfmtcpDefaultQueryStyle[];
extern CONST char * CONST TxfmtcpDefaultQuerySetStyles[];
extern CONST char       TxfmtcpDefaultQueryClass[];
extern CONST char * CONST TxfmtcpDefaultQuerySetClasses[];

#define TXFMTCP_QUERYFIXUPMODE_WITHINDOT        0
#define TXFMTCP_QUERYFIXUPMODE_FINDSETS         1

#define TXFMTCP_DEFAULT_QUERYSETCYCLENUM        10
#if TX_VERSION_MAJOR >= 6
#  define TXFMTCP_DEFAULT_QUERYFIXUPMODE    TXFMTCP_QUERYFIXUPMODE_FINDSETS
#else /* TX_VERSION_MAJOR < 6 */
#  define TXFMTCP_DEFAULT_QUERYFIXUPMODE    TXFMTCP_QUERYFIXUPMODE_WITHINDOT
#endif /* TX_VERSION_MAJOR < 6 */
#define TXFMTCP_DEFAULT_DECL                            \
  {                                                     \
    APICPPN,                                            \
    HTPFOBJPN,                                          \
    TXFMTCP_DEFAULT_QUERYSETCYCLENUM,                   \
    (char *)TxfmtcpDefaultQueryStyle,                   \
    (char **)TxfmtcpDefaultQuerySetStyles,              \
    TXFMTCP_DEFAULT_QUERYSETCYCLENUM,                   \
    (char *)TxfmtcpDefaultQueryClass,                   \
    (char **)TxfmtcpDefaultQuerySetClasses,             \
    TXFMTCP_DEFAULT_QUERYSETCLASSES_NUM,                \
    0,                                                  \
    TXFMTCP_DEFAULT_QUERYFIXUPMODE,                     \
    0                                                   \
  }

extern CONST TXFMTCP    TxfmtcpDefault;

typedef struct TXFMTSTATE_tag                   /* htpfengine() state */
{
  int   lastHitNum;                             /* last <a name="hitNNN"> # */
  void  *mmList;                                /* MM list */
  int   mmNum;                                  /* # items in `mmList' */
}
TXFMTSTATE;
#define TXFMTSTATEPN    ((TXFMTSTATE *)NULL)
/* Open/close for already-declared/alloced TXFMTSTATE structs.
 * Thread-safe, signal-safe iff no Metamorph hit markup used:
 */
#define TXFMTSTATE_INIT(fs)     \
  { (fs)->lastHitNum = (fs)->mmNum = 0; (fs)->mmList = NULL; }
#define TXFMTSTATE_RELEASE(fs)  \
  { (fs)->lastHitNum = 0; if ((fs)->mmList) TxfmtstateCloseCache(fs); }
/* Copy non-cache (ie. state) info from `src' to `dest': */
#define TXFMTSTATE_COPY(dest, src) { (dest)->lastHitNum = (src)->lastHitNum; }

TXFMTSTATE *TxfmtstateOpen ARGS((TXPMBUF *pmbuf));
int         TxfmtstateCloseCache ARGS((TXFMTSTATE *fs));
TXFMTSTATE *TxfmtstateClose ARGS((TXFMTSTATE *fs));

/* Storage for fetched args, when converting from va_list to array: */
typedef union HTPFARG_tag
{
  char                  c;
  unsigned              u;
  int                   i;
  double                d;
  ulong                 ul;
  long                  l;
  void                  *vp;
  char                  *cp;
  int                   *ip;
  time_t                t;
  EPI_HUGEINT           hi;
  EPI_HUGEUINT          hui;
#ifdef EPI_HAVE_LONG_LONG
  long long             ll;
#endif /* EPI_HAVE_LONG_LONG */
#ifdef EPI_HAVE_UNSIGNED_LONG_LONG
  unsigned long long    ull;
#endif /* EPI_HAVE_UNSIGNED_LONG_LONG */
#ifdef EPI_HAVE_LONG_DOUBLE
  long double           longDouble;
#endif /* EPI_HAVE_LONG_DOUBLE */
  EPI_HUGEFLOAT         hugeFloat;
}
HTPFARG;
#define HTPFARGPN       ((HTPFARG *)NULL)

/* Callback to fetch each arg: */
typedef void *(HTPFCB) ARGS((HTPFT type, HTPFW what, void *data,
                             char **fmterr, size_t *sz));
#define HTPFCBPN	((HTPFCB *)NULL)

#ifdef CGI_H_GOT_VA_LIST
int htvsnpf ARGS((char *buf, size_t sz, CONST char *fmt, HTPFF flags,
                  CONST TXFMTCP *fmtcp, TXFMTSTATE *fs, HTPF_SYS_VA_LIST argp,
                  HTPFARG *args, size_t *argnp, TXPMBUF *pmbuf));
size_t TXvsnprintfToRingBuffer(TXPMBUF *pmbuf, char *buf, size_t bufSz,
                  size_t headOffset, size_t *tailOffset, const char *fmt,
                  HTPFF flags, const TXFMTCP *fmtcp, TXFMTSTATE *fs,
                  HTPF_SYS_VA_LIST argp, HTPFARG *args, size_t *argnp);
#endif
int htcsnpf ARGS((char *buf, size_t sz, CONST char *fmt, size_t fmtSz,
           HTPFF flags, CONST TXFMTCP *fmtcp, TXFMTSTATE *fs, HTPFCB *cb,
           void *data));
int htcfpf(FILE *fp, const char *fmt, size_t fmtSz, HTPFF flags,
           const TXFMTCP *fmtcp, TXFMTSTATE *fs, HTPFCB *cb, void *data);

/* KNG 960815: */
int   htfputcu ARGS((int ch, FILE *fp));
int   htfputch ARGS((int ch, FILE *fp));
char *htsputcu ARGS((int ch, char *s));
char *htsputch ARGS((int ch, char *s));
int   htfputsu ARGS((CONST char *s, FILE *fp));
int   htfputsh ARGS((CONST char *s, FILE *fp));
int   htputsu  ARGS((CONST char *s));
int   htputsh  ARGS((CONST char *s));
char *htsputsh ARGS((CONST char *s, char *d));
#define htputcu(ch, fp) htfputcu((ch), (fp))
#define htputch(ch, fp) htfputch((ch), (fp))
#define htputcharu(ch)  htfputcu((ch), stdout)
#define htputcharh(ch)  htfputch((ch), stdout)
/* might as well wrap stdio too: */
#define htfputc         fputc
#define htfputs         fputs
#define htputc          putc
#define htputchar       putchar
#define htputs          puts


#endif /* HTPF_H */
