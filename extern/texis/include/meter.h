#ifndef METER_H
#define METER_H


typedef enum TXMDT_tag                  /* METER display type */
{
  TXMDT_INVALID = -1,
  TXMDT_NONE    = 0,
  TXMDT_SIMPLE,                         /* low-bandwidth, no VT100 codes */
  TXMDT_PERCENT                         /* print %-done, use VT100 codes */
}
TXMDT;
#define TXMDTPN   ((TXMDT *)NULL)

typedef int (MDOUTFUNC) ARGS((void *usr, CONST char *data, size_t sz));
#define MDOUTFUNCPN     ((MDOUTFUNC *)NULL)
#define MDOUTFUNCPPN    ((MDOUTFUNC **)NULL)
typedef int (MDFLUSHFUNC) ARGS((void *usr));
#define MDFLUSHFUNCPN   ((MDFLUSHFUNC *)NULL)
#define MDFLUSHFUNCPPN  ((MDFLUSHFUNC **)NULL)

typedef struct METER_tag
{
  struct METER_tag      *parent;        /* parent meter we are a child of */
  int                   refcnt;
  int                   cols;           /* current width of screen */
  int                   mcols;          /* columns allocated to meter */
  int                   donecols;       /* columns we've printed so far */
  int                   donemils;       /* fraction done, in mils */
  int                   didend;         /* flag: meter_end() called */
  TXMDT                 type;           /* display type */
  MDOUTFUNC             *out;           /* output function */
  MDFLUSHFUNC           *flush;         /* flush function */
  char                  *label;         /* meter label */
  void                  *usr;           
  EPI_HUGEINT           userTotalSz;    /* total bytes of data to process */
  EPI_HUGEINT           totalsz;        /* "" value used (1 if "" is 0) */
  EPI_HUGEINT           donesz;         /* bytes completed */
  EPI_HUGEINT           curfrac;        /* current delta */
  EPI_HUGEINT           mindone;        /* `donesz' delta estimate */
  double                lastprint;      /* time of day of last printing */
  int                   spidx;          /* spinner index */
}
METER;
#define METERPN ((METER *)NULL)


int     meter_stdout ARGS((void *usr, CONST char *data, size_t sz));
int     meter_stdflush ARGS((void *usr));
int     meter_refresh ARGS((void));

METER *openmeter ARGS((char *label, TXMDT type, MDOUTFUNC *out,
                       MDFLUSHFUNC *flush, void *usr, EPI_HUGEINT totalsz));
METER *opensecondmeter ARGS((METER *mp, char *label, EPI_HUGEINT totalsz));
METER *closemeter ARGS((METER *m));

int     meter_updatedone ARGS((METER *m, EPI_HUGEINT donesz));
/* Macro to reduce calls to meter_updatedone() heuristically: */
#define METER_NEEDUPDATE(m, d)  ((EPI_HUGEINT)(d) >= (m)->mindone)
#define METER_UPDATEDONE(m, d)  \
  (METER_NEEDUPDATE(m, d) ? meter_updatedone(m, d) : 1)

int     meter_updatetotal ARGS((METER *m, EPI_HUGEINT totalsz));
int     meter_end ARGS((METER *m));
TXMDT     meter_str2type ARGS((CONST char *s, CONST char *e));
CONST char *TXmeterEnumToStr ARGS((TXMDT meterType));
CONST char *meter_listtypes ARGS((void));

extern int              TxMeterLastEOL, TxMeterCount;
extern MDOUTFUNC        *TxMeterOut;
extern MDFLUSHFUNC      *TxMeterFlush;

#endif /* !METER_H */
