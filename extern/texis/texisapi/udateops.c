/* -=- kai-mode: John -=- */
/* udate <-> other type conversions.
 *
 * udate is a signed int64 count of MICROSECONDS since 1970-01-01 UTC.
 * It is backed by int64 so that comparison, max(), ORDER BY and index
 * ordering come free (see fldcmp.c and the FTN_UDATE rows in fldops.h);
 * this file supplies only the conversions whose semantics are NOT plain
 * integer identity:
 *
 *   udate <-> char    date string, with optional fractional seconds
 *   udate <-> date    scale by 1e6
 *   udate <-> double  SECONDS with a fractional part (unix time as float)
 *
 * The integer-identity pairs (int64, int) reuse the existing int64
 * functions directly and are declared in fldops.h.
 *
 * ROUNDING RULE: every narrowing conversion floors toward NEGATIVE
 * INFINITY, never toward zero.  C integer division truncates toward
 * zero, which would round pre-1970 (negative) values toward the future
 * while rounding modern values toward the past -- an asymmetry across
 * the epoch that silently corrupts ordering of historical documents.
 */

#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>       /* gettimeofday() for `now' */
#include "sizes.h"
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"
#include "parsetim.h"

#define TX_USEC_PER_SEC ((ft_udate)1000000)
/* largest whole seconds that still fit in int64 microseconds */
#define TX_UDATE_MAX_SECS ((ft_int64)9223372036854)

/* floor-divide by 1e6: seconds containing this microsecond instant */
static ft_udate
TXudateToSecs(ft_udate us)
{
  ft_udate q = us / TX_USEC_PER_SEC;
  if (us < 0 && (us % TX_USEC_PER_SEC) != 0) q--;   /* toward -infinity */
  return(q);
}

/* the 0..999999 microsecond remainder, always non-negative */
static ft_udate
TXudateSubSec(ft_udate us)
{
  ft_udate r = us % TX_USEC_PER_SEC;
  if (r < 0) r += TX_USEC_PER_SEC;
  return(r);
}

/* ------------------------------------------------------------------------ */
/* udate <-> date : scale                                                    */

int
foudda(f1, f2, f3, op)          /* f1 udate, f2 date */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_udate      *vp1, *vp3;
  ft_date       *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(fodaud(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  vp1 = (ft_udate *)getfld(f1, &n1);
  vp2 = (ft_date *)getfld(f2, &n2);
  (void)vp1;
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_udate *)getfld(f3, NULL);
      *vp3 = (ft_udate)(*vp2) * TX_USEC_PER_SEC;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

int
fodaud(f1, f2, f3, op)          /* f1 date, f2 udate */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_date       *vp3;
  ft_udate      *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(foudda(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_udate *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_date *)getfld(f3, NULL);
      *vp3 = (ft_date)TXudateToSecs(*vp2);       /* floor, not truncate */
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */
/* udate <-> double : SECONDS with fraction                                  */

int
fouddo(f1, f2, f3, op)          /* f1 udate, f2 double */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_udate      *vp3;
  ft_double     *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(fodoud(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_double *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_udate *)getfld(f3, NULL);
      /* seconds-as-double in, microseconds stored */
      if (*vp2 > (ft_double)TX_UDATE_MAX_SECS ||
          *vp2 < -(ft_double)TX_UDATE_MAX_SECS)
        {
          putmsg(MWARN, "fouddo",
                 "udate seconds value %g out of range (are these microseconds?)",
                 (double)*vp2);
          *vp3 = (ft_udate)0;
        }
      else
        *vp3 = (ft_udate)floor(*vp2 * 1000000.0 + 0.5);
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

int
fodoud(f1, f2, f3, op)          /* f1 double, f2 udate */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_double     *vp3;
  ft_udate      *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(fouddo(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_udate *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_double *)getfld(f3, NULL);
      /* exact for |value| < 2^53 us, i.e. years 1684..2255 */
      *vp3 = (ft_double)(*vp2) / 1000000.0;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */
/* udate <-> int64 : value identity, but the RESULT TYPE must follow f1.
 *
 * Reusing foi6i6 for these pairs looked free (ft_udate is EPI_INT64) but
 * types the result from whichever field it is handed, so
 * convert(ucol,'int64') came back typed udate and rendered as a date
 * string.  Explicit functions keep the value identical and the type
 * correct. */

int
foudi6(f1, f2, f3, op)          /* f1 udate, f2 int64 -- INPUT: seconds */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_udate      *vp3;
  ft_int64      *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(foi6ud(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_int64 *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      /* SECONDS in, to match double.  rampart-sql binds an integral JS
       * Number as SQL_C_SBIGINT and a fractional one as SQL_C_DOUBLE,
       * so if int64 meant microseconds here then 1786117587 and
       * 1786117587.5 -- the same instant to a caller -- would land 56
       * years apart, decided purely by whether a fraction happened to
       * be present.  A JS Number therefore always means seconds.
       *
       * NOTE the deliberate asymmetry with the OUTPUT direction
       * (foi6ud), which yields exact microseconds: output has no
       * ambiguity because convert() names the type explicitly, and
       * microseconds are the only exact numeric route until BigInt
       * lands.  Consequence: convert(u,'int64') is NOT a value you can
       * insert back into a udate column unchanged -- use a Date, a
       * date string, or divide by 1e6 first. */
      /* guard the multiply: seconds outside +/-9.22e12 overflow int64
       * microseconds (signed overflow is UB, and the usual way to hit
       * this is passing a microsecond value where seconds are meant) */
      TXmakesimfield(f1, f3);
      vp3 = (ft_udate *)getfld(f3, NULL);
      if (*vp2 > TX_UDATE_MAX_SECS || *vp2 < -TX_UDATE_MAX_SECS)
        {
          putmsg(MWARN, "foudi6",
                 "udate seconds value %lld out of range (are these microseconds?)",
                 (long long)*vp2);
          *vp3 = (ft_udate)0;
        }
      else
        *vp3 = (ft_udate)*vp2 * TX_USEC_PER_SEC;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

int
foi6ud(f1, f2, f3, op)          /* f1 int64, f2 udate */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_int64      *vp3;
  ft_udate      *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(foudi6(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_udate *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_int64 *)getfld(f3, NULL);
      *vp3 = (ft_int64)*vp2;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */
/* udate <-> uint64                                                          */
/*                                                                           */
/* Mirrors the int64 pair exactly: microseconds OUT, seconds IN.  Keeping    */
/* the two 64-bit integer types identical means the rule has no exceptions   */
/* -- a numeric value supplied to a udate is always seconds, and int64 or    */
/* uint64 read back out is always microseconds.                              */
/*                                                                           */
/* uint64 cannot represent pre-1970 instants, so the OUT direction guards    */
/* against a negative udate rather than letting it wrap to a huge positive.  */

int
foudu6(f1, f2, f3, op)          /* f1 udate, f2 uint64 -- INPUT: seconds */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_udate      *vp3;
  ft_uint64     *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(fou6ud(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_uint64 *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_udate *)getfld(f3, NULL);
      if (*vp2 > (ft_uint64)TX_UDATE_MAX_SECS)
        {
          putmsg(MWARN, "foudu6",
                 "udate seconds value %llu out of range (are these microseconds?)",
                 (unsigned long long)*vp2);
          *vp3 = (ft_udate)0;
        }
      else
        *vp3 = (ft_udate)*vp2 * TX_USEC_PER_SEC;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

int
fou6ud(f1, f2, f3, op)          /* f1 uint64, f2 udate -- OUTPUT: microseconds */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_uint64     *vp3;
  ft_udate      *vp2;
  size_t        n1, n2;

  if (op == FOP_CNV) return(foudu6(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_udate *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      TXmakesimfield(f1, f3);
      vp3 = (ft_uint64 *)getfld(f3, NULL);
      if (*vp2 < (ft_udate)0)
        {
          putmsg(MWARN, "fou6ud",
                 "pre-1970 udate %lld cannot be represented as uint64",
                 (long long)*vp2);
          *vp3 = (ft_uint64)0;
        }
      else
        *vp3 = (ft_uint64)*vp2;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

/* ------------------------------------------------------------------------ */
/* udate <-> char                                                            */

#define TX_UDATE_STRSZ  64

/* "YYYY-MM-DD HH:MM:SS.uuuuuu" in local time, matching what a date
 * column formats, plus the microsecond fraction. */
static int
TXudateToStr(ft_udate us, char *buf, size_t bufsz)
{
  time_t        secs = (time_t)TXudateToSecs(us);
  ft_udate      frac = TXudateSubSec(us);
  struct tm     *tm;
  size_t        n;

  tm = localtime(&secs);
  if (tm == NULL) return(-1);
  n = strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", tm);
  if (n == 0) return(-1);
  snprintf(buf + n, bufsz - n, ".%06ld", (long)frac);
  return(0);
}

static ft_udate TXstrToUdateParsed ARGS((const char *s, size_t len));

static int
TXudateIsNow(const char *s, size_t len)
/* Returns 1 if `s' is exactly the keyword `now' (case-insensitive,
 * surrounding whitespace ignored, NUL-terminated early is fine -- a
 * fixed-width char field arrives padded).
 *
 * `now' is special-cased because parsetime() returns a time_t: routed
 * through it, `now' would always land on a whole second and quietly
 * defeat the entire point of a microsecond type.  Anything more
 * complex that merely CONTAINS `now' (parsetime accepts `now + 1
 * hour') is deliberately not matched here and keeps second
 * granularity -- offsets that coarse do not need sub-second precision.
 */
{
  const char    *e;

  for (e = s; (size_t)(e - s) < len && *e != '\0'; e++) ;
  while (s < e && (*s == ' ' || *s == '\t')) s++;
  while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
  if ((size_t)(e - s) != 3) return(0);
  return((s[0] == 'n' || s[0] == 'N') &&
         (s[1] == 'o' || s[1] == 'O') &&
         (s[2] == 'w' || s[2] == 'W'));
}

static ft_udate
TXstrToUdate(const char *s, size_t len)
{
  /* exact microseconds straight from the clock -- deliberately NOT
   * TXgettimeofday(), which collapses the timeval into a double and
   * would reintroduce ~0.5us of rounding at present-day epoch values */
  if (TXudateIsNow(s, len))
    {
      struct timeval    tv;

      if (gettimeofday(&tv, NULL) != 0) return((ft_udate)(-1));
      return((ft_udate)tv.tv_sec * TX_USEC_PER_SEC + (ft_udate)tv.tv_usec);
    }
  return(TXstrToUdateParsed(s, len));
}

/* Parse via the engine's own date parser, then add any fractional
 * seconds it discarded.  parsetime() handles every format datefmt
 * accepts, so we do not reimplement date parsing -- we only recover the
 * sub-second part, which it has no way to express. */
static ft_udate
TXstrToUdateParsed(const char *s, size_t len)
{
  ft_date       secs;
  ft_udate      frac = 0;
  const char    *p, *e = s + len;

  secs = (ft_date)parsetime((char *)s, len);
  if (secs == (ft_date)(-1)) return((ft_udate)(-1));

  /* find ".ddd" attached to the seconds field */
  for (p = s; p < e; p++)
    {
      if (*p == '.' && p + 1 < e && *(p+1) >= '0' && *(p+1) <= '9')
        {
          ft_udate scale = TX_USEC_PER_SEC / 10;
          for (p++; p < e && *p >= '0' && *p <= '9' && scale > 0; p++)
            {
              frac += (ft_udate)(*p - '0') * scale;
              scale /= 10;
            }
          break;
        }
    }
  return((ft_udate)secs * TX_USEC_PER_SEC + frac);
}

int
foudch(f1, f2, f3, op)          /* f1 udate, f2 char */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_udate      *vp3;
  ft_char       *vp2;
  size_t        n1, n2;
  ft_udate      v;

  if (op == FOP_CNV) return(fochud(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_char *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      v = (*vp2 == '\0') ? (ft_udate)0 : TXstrToUdate((char *)vp2, n2);
      if (v == (ft_udate)(-1))
        {
          putmsg(MWARN, "foudch", "Date %s is invalid", (char *)vp2);
          v = (ft_udate)0;
        }
      TXmakesimfield(f1, f3);
      vp3 = (ft_udate *)getfld(f3, NULL);
      *vp3 = v;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}

int
fochud(f1, f2, f3, op)          /* f1 char, f2 udate */
FLD *f1; FLD *f2; FLD *f3; int op;
{
  ft_char       *vp3;
  ft_udate      *vp2;
  size_t        n1, n2, na;
  char          buf[TX_UDATE_STRSZ];
  char          *mem;

  if (op == FOP_CNV) return(foudch(f2, f1, f3, FOP_ASN));
  if (TXfldIsNull(f2)) return(TXfldmathReturnNull(f1, f3));
  (void)getfld(f1, &n1);
  vp2 = (ft_udate *)getfld(f2, &n2);
  switch (op)
    {
    case FOP_ASN:
      if (TXudateToStr(*vp2, buf, sizeof(buf)) != 0)
        return(TXfldmathReturnNull(f1, f3));
      na = strlen(buf) + 1;
      mem = (char *)TXcalloc(TXPMBUFPN, "fochud", na, 1);
      if (!mem) return(FOP_EUNKNOWN);
      memcpy(mem, buf, na);
      TXmakesimfield(f1, f3);
      setfldandsize(f3, mem, na, FLD_FORCE_NORMAL);
      vp3 = (ft_char *)getfld(f3, NULL);
      (void)vp3;
      break;
    default:
      return(FOP_EINVAL);
    }
  return(0);
}
