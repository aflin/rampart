#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "txcoreconfig.h"
#include "sizes.h"
#ifdef TEST
#  include <stdarg.h>
#endif /* TEST */
#include "texint.h"
#include "meter.h"


/*
  TXMDT_SIMPLE:  no codes

Indexing new data:
0%-----------------25%------------------50%---------------75%--------------100%
########################################


  TXMDT_PERCENT: uses BS, CR codes

Indexing new data:
##############################################-------------------------- 65.0%/

*/

MDOUTFUNC       *TxMeterOut = meter_stdout;     /* default I/O funcs */
MDFLUSHFUNC     *TxMeterFlush = meter_stdflush; /*   for openmeter(NULL) */
static CONST char       Spin[] = "|/-\\";

#define REFRESH_TIME    0.8                     /* refresh time goal */


/* ------------------------------------------------------------------------- */


int
meter_stdout(usr, data, sz)
void            *usr;
CONST char      *data;
size_t          sz;
/* Default output function for meters: prints to stdout.
 */
{
  (void)usr;
  if (sz > 0) fwrite(data, 1, sz, stdout);
  return(1);
}

int
meter_stdflush(usr)
void    *usr;
/* Default flush function for meters: prints to stdout.
 */
{
  (void)usr;
  fflush(stdout);
  return(1);
}

static void meter_redraw ARGS((METER *m));
static void
meter_redraw(m)
METER   *m;
/* Prints scale and amount done.  Does not flush.
 */
{
  int   c, r;
  char  tmp[16];

  switch (m->type)                              /* print label */
    {
    case TXMDT_SIMPLE:
    case TXMDT_PERCENT:
      c = strlen(m->label);
      if (c >= m->cols) c = m->cols - 1;
      m->out(m->usr, m->label, c);
      m->out(m->usr, "\n", 1);
      break;
    default:
      break;
    }
  switch (m->type)                              /* print scale and meter */
    {
    case TXMDT_SIMPLE:
      c = 0;
      if (m->cols >= 7)
        {
          m->out(m->usr, "0%", 2);
          c += 2;
        }
      else
        {
          for (r = m->cols - 5; r > 0; r--, c++) m->out(m->usr, "-", 1);
        }
      if (m->cols >= 31)
        {
          for (r=(m->cols+2)/4-4, c += r; r > 0; r--) m->out(m->usr, "-", 1);
          m->out(m->usr, "25%", 3);
          c += 3;
          for (r = m->cols/2 - c - 2, c+=r; r>0; r--) m->out(m->usr, "-", 1);
        }
      else
        {
          for (r = (m->cols-1)/2-4; r > 0; r--, c++) m->out(m->usr, "-", 1);
        }
      if (m->cols >= 12)
        {
          m->out(m->usr, "50%", 3);
          c += 3;
        }
      else
        {
          for (r = (m->cols - 8); r > 0; r--, c++) m->out(m->usr, "-", 1);
        }
      if (m->cols >= 31)
        {
          for (r = m->cols/4 - 3, c += r; r > 0; r--) m->out(m->usr, "-", 1);
          m->out(m->usr, "75%", 3);
          c += 3;
        }
      for (r = m->cols - c - 5; r > 0; r--, c++) m->out(m->usr, "-", 1);
      if (m->cols >= 5)
        m->out(m->usr, "100%\n", 5);
      else
        {
          for (r = m->cols - 1; r > 0; r--, c++) m->out(m->usr, "-", 1);
          m->out(m->usr, "\n", 1);
        }
      for (c = 0; c < m->donecols; c++) m->out(m->usr, "#", 1);
      break;
    case TXMDT_PERCENT:
      for (c = 0; c < m->donecols; c++) m->out(m->usr, "#", 1);
      for ( ; c < m->mcols; c++) m->out(m->usr, "-", 1);
      sprintf(tmp, "%3d.%d%%%c", m->donemils/10, m->donemils%10,
              Spin[m->spidx]);
      m->out(m->usr, tmp, 7);
      break;
    default:
      break;
    }
}

METER *
openmeter(label, type, out, flush, usr, totalsz)
char            *label;         /* label for meter */
TXMDT           type;
MDOUTFUNC       *out;           /* output function */
MDFLUSHFUNC     *flush;         /* flush function */
void            *usr;           /* user data for out/flush */
EPI_HUGEINT     totalsz;        /* total byte size to reach 100% done */
/* Opens meter and prints initial header and scale.  If `out' or `flush'
 * is NULL, TxMeterOut and TxMeterFlush are used.
 */
{
  static char   fn[] = "openmeter";
  METER         *m;
  int           c, r;
  char          *s, *e;

  if ((m = (METER *)calloc(1, sizeof(METER))) == METERPN)
    {
      c = sizeof(METER);
    merr:
      putmsg(MERR + MAE, fn, "Can't alloc %u bytes of memory", c);
      goto done;
    }
  m->refcnt = 1;
  m->cols = 80;
  if ((s = getenv("COLUMNS")) != CHARPN &&
      (c = (int)strtol(s, &e, 0)) > 0 && e > s)
    m->cols = c;
  else if (TXgetwinsize(&c, &r) && c > 0)
    m->cols = c;
  m->type = type;
  m->parent = METERPN;
  m->out = (out == MDOUTFUNCPN ? TxMeterOut : out);
  m->flush = (flush == MDFLUSHFUNCPN ? TxMeterFlush : flush);
  m->usr = usr;
  c = strlen(label);
  if ((m->label = strdup(label)) == CHARPN)
    {
      c++;
      goto merr;
    }
  switch (m->type)
    {
    case TXMDT_SIMPLE:
      m->mcols = m->cols - 1;
      break;
    case TXMDT_PERCENT:
      m->mcols = m->cols - 8;
      break;
    default:
      m->mcols = m->cols - 1;
      break;
    }
  meter_redraw(m);
  m->flush(m->usr);
  m->lastprint = TXgettimeofday();
  if (m->mcols < 1) m->mcols = 1;
  m->donecols = m->donemils = 0;
  m->donesz = (EPI_HUGEINT)0;
  meter_updatetotal(m, totalsz);

done:
  return(m);
}

METER *
opensecondmeter(mp, label, totalsz)
METER   *mp;            /* parent meter */
char    *label;
EPI_HUGEINT totalsz;
/* Opens secondary meter, to be printed below existing meter `*mp'.
 */
{
  METER *m;

  if (!mp->didend)
    {
      meter_updatedone(mp, mp->donesz);         /* flush */
      switch (mp->type)                         /* go below current meter */
        {
        case TXMDT_PERCENT:
          mp->out(mp->usr, "\b ", 2);           /* delete spinner */
        case TXMDT_SIMPLE:
          mp->out(mp->usr, "\n", 1);
          break;
        default:
          break;
        }
    }
  m = openmeter(label, mp->type, mp->out, mp->flush, mp->usr, totalsz);
  if (m == METERPN) goto done;
  /* Increment refcnt to detect if parent is closed before us: */
  for (m->parent = mp; mp != METERPN; mp = mp->parent) mp->refcnt++;
done:
  return(m);
}

METER *
closemeter(m)
METER   *m;
/* Closes meter.  Does not print anything; see meter_end().
 */
{
  METER *mp;

  if (m == METERPN) goto done;
  if (m->refcnt > 1)
    {
      putmsg(MERR + UGE, "closemeter",
             "Internal error: meter closed before child");
      goto done;
    }
  for (mp = m->parent; mp != METERPN; mp = mp->parent) mp->refcnt--;
  if (m->label != CHARPN) free(m->label);
  free(m);

done:
  return(METERPN);
}

int
meter_updatedone(m, donesz)
METER   *m;
EPI_HUGEINT donesz;
/* Updates meter to reflect total bytes done `donesz', printing as needed.
 * Usually called via METER_UPDATEDONE() macro for efficiency (fewer calls).
 */
{
  int           cols, mils;
  char          tmp[16];
  double        now, td, mul;

  if (donesz > m->totalsz) m->donesz = m->totalsz;
  else if (donesz < (EPI_HUGEINT)0) m->donesz = (EPI_HUGEINT)0;
  else m->donesz = donesz;

  /* Compute additional meter columns to print: */
#if EPI_HUGEINT_BITS >= 64
  cols = (int)((m->donesz*(EPI_HUGEINT)m->mcols)/m->totalsz);
#else /* EPI_HUGEINT_BITS < 64 */                    /* avoid integer overflow */
  cols = (int)(((double)m->donesz*(double)m->mcols)/(double)m->totalsz);
#endif /* EPI_HUGEINT_BITS < 64 */
  if (cols > m->mcols) cols = m->mcols;
  cols -= m->donecols;                          /* convert to delta */
  
  switch (m->type)
    {
    case TXMDT_SIMPLE:
      if (cols <= 0) break;                     /* nothing new to print */
      m->donecols += cols;
      for ( ; cols > 0; cols--) m->out(m->usr, "#", 1);
      m->flush(m->usr);
      break;
    case TXMDT_PERCENT:
      /* Compute the percentage to print: */
#if EPI_HUGEINT_BITS >= 64
      mils = (int)((m->donesz*(EPI_HUGEINT)1000)/m->totalsz);
#else /* EPI_HUGEINT_BITS < 64 */                    /* avoid integer overflow */
      mils = (int)(((double)m->donesz*(double)1000.0)/(double)m->totalsz);
#endif /* EPI_HUGEINT_BITS < 64 */
      if (mils > 1000) mils = 1000;
      mils -= m->donemils;                      /* convert to delta */
      now = TXgettimeofday();
      td = now - m->lastprint;
      if (td <= (double)0.001)                  /* real fast or time reset */
        {
          if (td > (double)0.0) m->curfrac <<= 2;
        }
      else                                      /* too fast or slow */
        {
          mul = ((double)REFRESH_TIME)/td;
          if (mul >= (double)4.0)
            m->curfrac <<= 2;
          else if (mul <= (double)0.125)
            m->curfrac >>= 3;
          else
            m->curfrac = (EPI_HUGEINT)(mul*(double)m->curfrac);
        }
      if (m->curfrac < (EPI_HUGEINT)1) m->curfrac = (EPI_HUGEINT)1;
      m->lastprint = now;
      if (cols <= 0)                            /* no new meter to print */
        m->out(m->usr, "\b\b\b\b\b\b\b", 7);    /* back up over percentage */
      else
        {
          m->out(m->usr, "\r", 1);              /* start of line for meter */
          m->donecols += cols;
          for (cols = 0; cols < m->donecols; cols++) m->out(m->usr, "#", 1);
          for ( ; cols < m->mcols; cols++) m->out(m->usr, "-", 1);
        }
      if (mils > 0) m->donemils += mils;
      m->spidx = ((m->spidx + 1) & 3);
      sprintf(tmp, "%3d.%d%%%c", m->donemils/10, m->donemils % 10,
              Spin[m->spidx]);
      m->out(m->usr, tmp, 7);
      m->flush(m->usr);
      break;
    default:
      break;
    }
  m->mindone = m->donesz + m->curfrac;
  if (m->mindone > m->totalsz) m->mindone = m->totalsz;
  return(1);
}

int
meter_updatetotal(m, totalsz)
METER   *m;
EPI_HUGEINT totalsz;
/* Sets total size of data to `totalsz', which may be greater than the
 * original value.  May be called occasionally if the size of data to do
 * increases.  Will not update meter.
 */
{
  int   c;

  m->userTotalSz = totalsz;
  if (totalsz < (EPI_HUGEINT)1) totalsz = (EPI_HUGEINT)1;
  m->totalsz = totalsz;

  /* Compute the minimum amount needed to be done before anything new
   * is actually printed by the meter (`mindone').  This is used by the
   * METER_UPDATEDONE() macro to minimize calls to meter_updatedone():
   */
  switch (m->type)
    {
    case TXMDT_SIMPLE:
      m->curfrac = m->totalsz/(EPI_HUGEINT)m->cols;
      break;
    case TXMDT_PERCENT:
    default:
      c = (m->mcols > 1000 ? m->mcols : 1000);
      m->curfrac = m->totalsz/(EPI_HUGEINT)c;
      /* `curfrac' will be dynamically adjusted by meter_updatedone(),
       * but limit its initial value here to prevent a long wait for
       * the first update:
       */
      if (m->curfrac > (EPI_HUGEINT)25000) m->curfrac = (EPI_HUGEINT)25000;
      break;
    }
  m->mindone = m->donesz + m->curfrac;
  if (m->mindone > m->totalsz) m->mindone = m->totalsz;
  return(1);
}

int
meter_end(m)
METER   *m;
/* Finishes meter bar with dashes, and prints newlines as needed to
 * put cursor after meter output, or back to previous meter.  Called
 * when done with meter (note: may not be at 100%; use meter_updatedone()).
 */
{
  int   i;

  /* If 0 total items were originally set (e.g. update index with no
   * new items, or nothing to merge), we may not have shown progress yet.
   * Add 1 done to get us to 100%, as we should show 100% done when
   * done, even if nothing done.  Bug 7019:
   */
  if (m->userTotalSz == 0 && m->donesz == 0) meter_updatedone(m, 1);

  if (m->didend) return(1);
  m->didend = 1;
  if (m->parent != METERPN)                     /* back to previous meter */
    {
      switch (m->type)
        {
        case TXMDT_PERCENT:
          m->out(m->usr, "\b \n", 3);           /* delete spinner */
          if (!m->parent->didend) meter_redraw(m->parent);
          break;
        case TXMDT_SIMPLE:
          for (i = m->donecols; i < m->mcols; i++) m->out(m->usr, "-", 1);
          m->out(m->usr, "\n", 1);
          if (!m->parent->didend) meter_redraw(m->parent);
          break;
        default:
          return(1);
        }
    }
  else                                          /* down past current meter */
    {
      switch (m->type)
        {
        case TXMDT_PERCENT:
          m->out(m->usr, "\b \n", 3);           /* delete spinner */
          break;
        case TXMDT_SIMPLE:
          for (i = m->donecols; i < m->mcols; i++) m->out(m->usr, "-", 1);
          m->out(m->usr, "\n", 1);
          break;
        default:
          return(1);
        }
    }
  return(m->flush(m->usr));
}

TXMDT
meter_str2type(s, e)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
{
  char          *ei;
  int           i, errnum;
  size_t        sLen;

  if (e == CHARPN) e = s + strlen(s);
  sLen = e - s;

  if (sLen == 0 || (sLen == 4 && strnicmp(s, "none", 4) == 0))
    return(TXMDT_NONE);
  else if (sLen == 6 && strnicmp(s, "simple", 6) == 0)
    return(TXMDT_SIMPLE);
  else if ((sLen == 7 && strnicmp(s, "percent", 7) == 0) ||
           (sLen == 3 && strnicmp(s, "pct", 3) == 0))
    return(TXMDT_PERCENT);

  i = TXstrtoi(s, e, &ei, 0, &errnum);
  if (i < 0) i = 0;
  if (sLen > 0 && ei == e && errnum == 0) return((TXMDT)i);
  i = TXgetBooleanOrInt(TXPMBUF_SUPPRESS, "", "meter type", s, e, 4);
  if (i >= 0) return((TXMDT)i);

  return(TXMDT_INVALID);
}

CONST char *
TXmeterEnumToStr(meterType)
TXMDT     meterType;
{
  switch (meterType)
    {
    case TXMDT_NONE:    return("none");
    case TXMDT_SIMPLE:  return("simple");
    case TXMDT_PERCENT: return("percent");
    default:            return("unknown");
    }
}

CONST char *
meter_listtypes()
/* Returns list of possible meter types, eg. for a usage message.
 */
{
  return("none|simple|pct");
}

/* ------------------------------------------------------------------------- */

#ifdef TEST
PUTMSG()
{
  fprintf(stderr, "%03d error: %s\n", n, fmt);
  fflush(stderr);
  return(0);
}}

double
TXgettimeofday()
{
  struct timeval        tv;
  struct timezone       tz;

  if (gettimeofday(&tv, &tz) != 0) return((double)(-1.0));
  return((double)tv.tv_sec + (double)tv.tv_usec / (double)1000000.0);
}

int
TXgetwinsize(cols, rows)
int     *cols, *rows;
/* Returns current tty window size in `*cols' and `*rows', and 1, or
 * 0 if unknown.  NOTE: should be callable from signal handler.
 */
{
  *cols = *rows = -1;
  return(0);
}

static int output ARGS((void *usr, CONST char *data, size_t sz));
static int
output(usr, data, sz)
void            *usr;
CONST char      *data;
size_t          sz;
{
  fwrite(data, 1, sz, (FILE *)usr);
  return(1);
}

static int flush ARGS((void *usr));
static int
flush(usr)
void    *usr;
{
  fflush((FILE *)usr);
  return(1);
}

static void sleepmsec ARGS((long msec));
static void
sleepmsec(msec)
long    msec;
{
#ifdef _WIN32
  Sleep(msec);
#else /* !_WIN32 */
  struct timeval        tv;

  tv.tv_sec = msec/1000L;
  tv.tv_usec = 1000L*(msec % 1000L);
  select(0, NULL, NULL, NULL, &tv);
#endif /* !_WIN32 */
}

int
main(argc, argv)
int     argc;
char    **argv;
{
  METER *m;
  TXMDT type = TXMDT_SIMPLE;
  int   tot, i, second = 0;
#define SZ      100
#define SEC     100L

  if (argc > 1) type = meter_str2type(argv[1]);
  if (argc > 2) second = (strcmpi(argv[2], "second") == 0);
  m = openmeter("Test of meter object:", type, output, flush, stdout, SZ);
  srand(time(NULL));
  for (tot = 0; tot <= SZ; tot++)
    {
      sleepmsec(SEC);
      METER_UPDATEDONE(m, tot);
      if (tot % (SZ/4) == 0)
        {
          METER *m2;
          int   i;
          sleepmsec(2000);
          m2 = opensecondmeter(m, "Secondary test:", 256);
          for (i = 1; i <= 256; i++)
            {
              sleepmsec(20L);
              METER_UPDATEDONE(m2, i);
            }
          meter_end(m2);
          m2 = closemeter(m2);
          sleepmsec(2000);
        }
    }
  meter_end(m);
  m = closemeter(m);
  return(0);
}
#endif /* TEST */
