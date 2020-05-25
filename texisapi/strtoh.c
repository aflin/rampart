#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "sizes.h"
#include "os.h"
#include "txtypes.h"


/* strtoh(): a version of strtol() for HUGEINTs (off_t on some boxes).
 * KNG 990315
 */

EPI_HUGEUINT
TXstrtointtype(const char *s, const char *e, char **ep, int base,
     int typeIsSigned, EPI_HUGEUINT minVal, EPI_HUGEUINT maxVal, int *errnum)
/* Guts of integral TXstrto...() functions.
 *   s                  (in) String to parse
 *   e                  (in, opt.) End of `s'
 *   ep                 (out, opt.) End of parse of `s'
 *   base               (in) Base:
 *                      0     Decimal, octal or hex, based on number's prefix
 *                      2-36  Specified base
 *                      Additional bit flags:
 *     TXstrtointFlag_NoLeadZeroOctal      Do not interpret leading 0 as octal
 *     TXstrtointFlag_Base10CommasOk       Comma ok between digits iff base 10
 *     TXstrtointFlag_ConsumeTrailingSpace   Consume trailing whitespace too
 *     TXstrtointFlag_TrailingSourceIsError  Fail if any source remains
 *     TXstrtointFlag_MustHaveDigits         Fail if no payload digits seen
 *   typeIsSigned       (in) nonzero if final type is signed
 *   minVal             (in) minimum legal value
 *   maxVal             (in) maximum legal value
 *   errnum             (out, opt.) errno value
 */
{
  EPI_HUGEUINT  val, cutoff;
  int           n, overUnderflow = 0, cutlim;
  TXbool        consumeTrailingSpace = TXbool_False;
  TXbool        trailingSourceIsError = TXbool_False;
  TXbool        mustHaveDigits = TXbool_False;
  TXbool        gotDigits = TXbool_False, commasOk = TXbool_False;
  TXbool        octalOk = TXbool_True, isNeg;
  const char    *rets = s, *orgS = s;

  if (errnum) *errnum = 0;
  if (!e) e = s + strlen(s);
  val = 0;
  if (base & TXstrtointFlag_NoLeadZeroOctal)
    {
      base &= ~TXstrtointFlag_NoLeadZeroOctal;
      octalOk = TXbool_False;
    }
  if (base & TXstrtointFlag_Base10CommasOk)
    {
      base &= ~TXstrtointFlag_Base10CommasOk;
      commasOk = TXbool_True;
    }
  if (base & TXstrtointFlag_ConsumeTrailingSpace)
    {
      base &= ~TXstrtointFlag_ConsumeTrailingSpace;
      consumeTrailingSpace = TXbool_True;
    }
  if (base & TXstrtointFlag_TrailingSourceIsError)
    {
      base &= ~TXstrtointFlag_TrailingSourceIsError;
      trailingSourceIsError = TXbool_True;
    }
  if (base & TXstrtointFlag_MustHaveDigits)
    {
      base &= ~TXstrtointFlag_MustHaveDigits;
      mustHaveDigits = TXbool_True;
    }
  if (base != 0)
    {
      if (base < 2 || base > 36)
        {
          if (errnum) *errnum = EINVAL;
          goto done;
        }
    }
  isNeg = TXbool_False;
  for ( ; s < e && TX_ISSPACE(*(byte *)s); s++);
  if (s < e && *s == '-')
    {
      isNeg = TXbool_True;
      s++;
    }
  else if (s < e && *s == '+')
    s++;
  if (s < e && *s == '0')
    {
      rets = ++s;
      if (s < e && (*s == 'x' || *s == 'X'))
        {
          if (base == 0) base = 16;
          if (base == 16) s++;
        }
      else
        {
          gotDigits = TXbool_True;              /* `0' is payload digit */
          if (base == 0 && octalOk) base = 8;
        }
    }
  if (base == 0) base = 10;

  /* `cutoff' is the largest legal value that we can multiply by the
   * base without overflow.  `cutlim' is the largest next digit we can
   * then add without overflow.  E.g. for short decimals, cutoff is
   * 32767 (or -32768) divided by 10, and cutlim is 7 (or 8):
   */
  cutoff = (isNeg && typeIsSigned ? -minVal : maxVal);
  cutlim = (int)(cutoff % (EPI_HUGEUINT)base);
  cutoff /= (EPI_HUGEUINT)base;

  for ( ; s < e; rets = ++s)
    {
      if (*s >= '0' && *s <= '9') n = *s - '0';
      else if (*s >= 'A' && *s <= 'Z') n = (*s - 'A') + 10;
      else if (*s >= 'a' && *s <= 'z') n = (*s - 'a') + 10;
      /* Accept commas if requested, but be stringent, so probable
       * nonsense like `,,,,,1,,,,,2,,,3,' is not accepted:
       */
      else if (*s == ',' && base == 10 && commasOk && gotDigits &&
               s > orgS && s[-1] != ',' &&
               s + 1 < e && (s[1] >= '0' && s[1] <= '9'))
        continue;
      else break;                               /* non-numeric */
      if (n >= base) break;                     /* non-numeric */
      gotDigits = TXbool_True;
      if (!overUnderflow)
        {
          if (val > cutoff || (val == cutoff && n > cutlim))
            overUnderflow = 1;
          else
            {
              val *= (EPI_HUGEUINT)base;
              val += (EPI_HUGEUINT)n;
            }
        }
    }
  if (overUnderflow)
    {
      val = (isNeg && typeIsSigned ? minVal : maxVal);
      if (errnum) *errnum = ERANGE;
    }
  else
    {
      if (isNeg) val = -val;
      if (consumeTrailingSpace)
        {
          for (s = rets; s < e && TX_ISSPACE(*s); s++);
          rets = s;
        }
      if (trailingSourceIsError && rets < e)
        {
          if (errnum) *errnum = EINVAL;
        }
      else if (mustHaveDigits && !gotDigits)
        {
          if (errnum) *errnum = EINVAL;
        }
    }

done:
  if (ep) *ep = (char *)rets;
  return(val);
}

EPI_HUGEUINT
TXstrtouh(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
/* Note: base -1 means base 10 or base 16 is supported (e.g. like base 0
 * but no octal): avoids confusion with leading-zero base-10 numbers.
 * Otherwise behaves like strtoul(), but with EPI_HUGEUINT instead of ulong.
 */
{
  return(TXstrtointtype(s, e, ep, base, 0, 0, EPI_HUGEUINT_MAX, errnum));
}

EPI_HUGEINT
TXstrtoh(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
/* Note: base -1 means base 10 or base 16 is supported (e.g. like base 0
 * but no octal): avoids confusion with leading-zero base-10 numbers.
 * Otherwise behaves like strtol(), but with EPI_HUGEINT instead of long.
 */
{
  return((EPI_HUGEINT)TXstrtointtype(s, e, ep, base, 1,
                             -(EPI_HUGEINT)EPI_HUGEINT_MAX - (EPI_HUGEINT)1,
                                     EPI_HUGEINT_MAX, errnum));
}

EPI_OFF_T
TXstrtoepioff_t(s, e, ep, base, errnum)
const char      *s;
const char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((EPI_OFF_T)TXstrtointtype(s, e, ep, base, 1,
                                   -(EPI_OFF_T)EPI_OFF_T_MAX - (EPI_OFF_T)1,
                                   EPI_OFF_T_MAX, errnum));
}

EPI_UINT16
TXstrtoui16(const char *s, const char *e, char **ep, int base, int *errnum)
{
  return((EPI_UINT16)TXstrtointtype(s, e, ep, base, 0, 0, 65535, errnum));
}

EPI_INT16
TXstrtoi16(const char *s, const char *e, char **ep, int base, int *errnum)
{
  return((EPI_INT32)TXstrtointtype(s, e, ep, base, 1, -65536, 65535, errnum));
}

EPI_UINT32
TXstrtoui32(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((EPI_UINT32)TXstrtointtype(s, e, ep, base, 0, 0, EPI_UINT32_MAX,
                                    errnum));
}

EPI_INT32
TXstrtoi32(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((EPI_INT32)TXstrtointtype(s, e, ep, base, 1,
                                   -(EPI_INT32)EPI_INT32_MAX - (EPI_INT32)1,
                                   EPI_INT32_MAX, errnum));
}

EPI_UINT64
TXstrtoui64(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((EPI_UINT64)TXstrtointtype(s, e, ep, base, 0, 0, EPI_UINT64_MAX,
                                    errnum));
}

EPI_INT64
TXstrtoi64(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((EPI_INT64)TXstrtointtype(s, e, ep, base, 1,
                                   -(EPI_INT64)EPI_INT64_MAX - (EPI_INT64)1,
                                   EPI_INT64_MAX, errnum));
}

dword
TXstrtodw(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((dword)TXstrtointtype(s, e, ep, base, 0, 0, 0xffffffff, errnum));
}

word
TXstrtow(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((word)TXstrtointtype(s, e, ep, base, 0, 0, 0xffff, errnum));
}

int
TXstrtoi(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((int)TXstrtointtype(s, e, ep, base, 1, -MAXINT - 1, MAXINT, errnum));
}

unsigned
TXstrtou(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((unsigned)TXstrtointtype(s, e, ep, base, 0, 0, EPI_OS_UINT_MAX,
                                  errnum));
}

long
TXstrtol(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((long)TXstrtointtype(s, e, ep, base, 1, -EPI_OS_LONG_MAX - 1L,
                              EPI_OS_LONG_MAX, errnum));
}

unsigned long
TXstrtoul(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((unsigned long)TXstrtointtype(s, e, ep, base, 0, 0,
                                       EPI_OS_ULONG_MAX, errnum));
}

short
TXstrtos(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
  return((short)TXstrtointtype(s, e, ep, base, 1, -32768, 32767, errnum));
}

size_t
TXstrtosize_t(s, e, ep, base, errnum)
CONST char      *s;
CONST char      *e;     /* (in, opt.) end of `s' (NULL: s + strlen(s)) */
char            **ep;
int             base;
int             *errnum;
{
#ifdef EPI_OS_SIZE_T_IS_SIGNED
  return((size_t)TXstrtointtype(s, e, ep, base, 1,
                                -(size_t)EPI_OS_SIZE_T_MAX - (size_t)1,
                                EPI_OS_SIZE_T_MAX, errnum));
#else /* !EPI_OS_SIZE_T_IS_SIGNED */
  return((size_t)TXstrtointtype(s, e, ep, base, 0,
                                0,
                                EPI_OS_SIZE_T_MAX, errnum));
#endif /* !EPI_OS_SIZE_T_IS_SIGNED */
}

double
TXstrtod(s, e, endptr, errnum)
CONST char      *s;             /* (in) string to convert to double */
CONST char      *e;             /* (in, opt.) end of `s' (NULL: s+strlen(s))*/
char            **endptr;       /* (out, opt.) end of `s' converted */
int             *errnum;        /* (out, opt.) errno */
/* Wrapper for strtod() that handles some platform-specific issues.
 * Always sets `*errnum': 0 on sucess, nonzero E... on error (e.g.
 * nothing significant parsed).
 * Note: see also TXstrtold().
 */
{
  /* `volatile' to work around gcc 3.4.6 ia64-unknown-linux2.6.9-64-64
   * optimization (loses Inf value):
   */
  VOLATILE double       res;
  static CONST char     whitespace[] = " \t\r\n\v\f";
  int                   isNeg = 0, isNulTerminated = 0;
  CONST char            *t, *beginNonWhitespace;
  char                  *d, *tmpEndPtr, tmpBuf[1024];

  if (errnum) *errnum = 0;
  if (endptr) *endptr = (char *)s;
  if (!e)
    {
      isNulTerminated = 1;
      e = s + strlen(s);
    }
#ifdef EPI_UNSAFE_STRTOD
  /* Alpha etc. strtod() gives large numbers for Inf/-Inf, and 0 for NaN: */
#endif /* EPI_UNSAFE_STRTOD */
  /* Linux 2.4.2 strtod() parses `Infinity' but sets `*endptr' to +3 not +8 */
  t = s;
  t += TXstrspnBuf(t, e, whitespace, -1);
  beginNonWhitespace = t;
  if (t < e && *t == '-')
    {
      isNeg = 1;
      t++;
    }

  /* Accept hexadecimal, in case native strtod() does not, below: */
  if (t + 1 < e && t[0] == '0' && TX_TOLOWER(t[1]) == 'x')
    {
      EPI_HUGEINT       hu;

      hu = TXstrtoh(t, e, endptr, 16, errnum);
      res = (double)hu;
      if (isNeg) res = -res;
      goto done;
    }

  if (t < e && (t[0] == 'i' || t[0] == 'I'))    /* possibly `Inf[inity]' */
    {
      if (e - t >= 3 &&
          strnicmp(t, "inf", 3) == 0 &&
          (t + 3 == e || strchr(whitespace, t[3]) != CHARPN))
        {
          if (endptr != CHARPPN) *endptr = (char *)t + 3;
          goto getInf;
        }
      if (e - t >= 8 &&
          strnicmp(t, "infinity", 8) == 0 &&
          (t + 8 == e || strchr(whitespace, t[8]) != CHARPN))
        {
          if (endptr != CHARPPN) *endptr = (char *)t + 8;
        getInf:
          if (isNeg)
            TXDOUBLE_SET_NegInf(res);
          else
            TXDOUBLE_SET_PosInf(res);
          goto done;
        }
    }
  if (e - t >= 3 &&
      (t[0] == 'n' || t[0] == 'N') &&           /* possibly `NaN' */
      strnicmp(t, "NaN", 3) == 0 &&
      (t + 3 == e || strchr(whitespace, t[3]) != CHARPN))
    {
      TXDOUBLE_SET_NaN(res);
      if (endptr != CHARPPN) *endptr = (char *)t + 3;
    }
  else
    {
      if (endptr != CHARPPN) *endptr = (char *)s;       /* in case not set */
      if (isNulTerminated)
        {
          if (errnum) errno = 0;
          res = strtod(s, &tmpEndPtr);
          if (endptr) *endptr = tmpEndPtr;
          /* If only leading whitespace in `s' was parsed, error: */
          if (errnum)
            {
              if (tmpEndPtr <= beginNonWhitespace) *errnum = EINVAL;
              else if (errno) *errnum = errno;
            }
        }
      else
        {
          /* Copy `s' to scratch `tmpBuf' because strtod() expects
           * nul-termination:
           */
          for (d = tmpBuf, t = s;
               d < tmpBuf + sizeof(tmpBuf) && t < e;
               d++, t++)
            *d = *t;
          if (d >= tmpBuf + sizeof(tmpBuf))     /* out of room in `tmpBuf' */
            {
              if (errnum) *errnum = EINVAL;
              if (endptr) *endptr = (char *)s;
              res = 0.0;
            }
          else
            {
              *d = '\0';
              if (errnum) errno = 0;
              res = strtod(tmpBuf, &tmpEndPtr);
              if (endptr) *endptr = (char *)s + (tmpEndPtr - tmpBuf);
              /* If only leading whitespace in `s' was parsed, error: */
              if (errnum)
                {
                  if (tmpEndPtr <= tmpBuf + (beginNonWhitespace - s))
                    *errnum = EINVAL;
                  else if (errno)
                    *errnum = errno;
                }
            }
        }
    }
done:
  return(res);
}

#ifdef EPI_HAVE_LONG_DOUBLE
long double
TXstrtold(s, e, endptr, errnum)
CONST char      *s;             /* (in) string to convert to double */
CONST char      *e;             /* (in, opt.) end of `s' (NULL: s+strlen(s))*/
char            **endptr;       /* (out, opt.) end of `s' converted */
int             *errnum;        /* (out, opt.) errno */
/* Wrapper for strtold() that handles some platform-specific issues.
 * Note: see also TXstrtod().
 */
{
  /* `volatile' to work around gcc 3.4.6 ia64-unknown-linux2.6.9-64-64
   * optimization (loses Inf value):
   */
  VOLATILE long double  res;
  VOLATILE double       dbl;
  static CONST char     whitespace[] = " \t\r\n\v\f";
  int                   isNeg = 0, isNulTerminated = 0;
  CONST char            *t, *beginNonWhitespace;
  char                  *d, *tmpEndPtr, tmpBuf[1024];

  if (errnum) *errnum = 0;
  if (endptr) *endptr = (char *)s;
  if (!e)
    {
      isNulTerminated = 1;
      e = s + strlen(s);
    }
#  ifdef EPI_UNSAFE_STRTOD
  /* Alpha etc. strtod() gives large numbers for Inf/-Inf, and 0 for NaN: */
#  endif /* EPI_UNSAFE_STRTOD */
  /* Linux 2.4.2 strtod() parses `Infinity' but sets `*endptr' to +3 not +8 */
  t = s;
  t += TXstrspnBuf(t, e, whitespace, -1);
  beginNonWhitespace = t;
  if (t < e && *t == '-')
    {
      isNeg = 1;
      t++;
    }
  if (t < e && (t[0] == 'i' || t[0] == 'I'))    /* possibly `Inf[inity]' */
    {
      if (e - t >= 3 &&
          strnicmp(t, "inf", 3) == 0 &&
          (t + 3 == e || strchr(whitespace, t[3]) != CHARPN))
        {
          if (endptr != CHARPPN) *endptr = (char *)t + 3;
          goto getInf;
        }
      if (e - t >= 8 &&
          strnicmp(t, "infinity", 8) == 0 &&
          (t + 8 == e || strchr(whitespace, t[8]) != CHARPN))
        {
          if (endptr != CHARPPN) *endptr = (char *)t + 8;
        getInf:
          /* WTF TXLONGDOUBLE_SET_{Neg,Pos}Inf(): */
          if (isNeg)
            TXDOUBLE_SET_NegInf(dbl);
          else
            TXDOUBLE_SET_PosInf(dbl);
          res = (long double)dbl;
          goto done;
        }
    }
  if (e - t >= 3 &&
      (t[0] == 'n' || t[0] == 'N') &&           /* possibly `NaN' */
      strnicmp(t, "NaN", 3) == 0 &&
      (t + 3 == e || strchr(whitespace, t[3]) != CHARPN))
    {
      /* WTF TXLONGDOUBLE_SET_NaN(): */
      TXDOUBLE_SET_NaN(dbl);
      res = (long double)dbl;
      if (endptr != CHARPPN) *endptr = (char *)t + 3;
    }
  else
    {
      if (endptr != CHARPPN) *endptr = (char *)s;       /* in case not set */
      if (isNulTerminated)
        {
          if (errnum) errno = 0;
#  ifdef EPI_HAVE_STRTOLD
          res = strtold(s, &tmpEndPtr);
#  else /* !EPI_HAVE_STRTOLD */
          res = (long double)strtod(s, &tmpEndPtr);
#  endif /* !EPI_HAVE_STRTOLD */
          if (endptr) *endptr = tmpEndPtr;
          /* If only leading whitespace in `s' was parsed, error: */
          if (errnum)
            {
              if (tmpEndPtr <= beginNonWhitespace) *errnum = EINVAL;
              else if (errno) *errnum = errno;
            }
        }
      else
        {
          /* Copy `s' to scratch `tmpBuf' because strtold() expects
           * nul-termination:
           */
          for (d = tmpBuf, t = s;
               d < tmpBuf + sizeof(tmpBuf) && t < e;
               d++, t++)
            *d = *t;
          if (d >= tmpBuf + sizeof(tmpBuf))     /* out of room in `tmpBuf' */
            {
              if (errnum) *errnum = EINVAL;
              if (endptr) *endptr = (char *)s;
              res = 0.0;
            }
          else
            {
              *d = '\0';
              if (errnum) errno = 0;
#  ifdef EPI_HAVE_STRTOLD
              res = strtold(tmpBuf, &tmpEndPtr);
#  else /* !EPI_HAVE_STRTOLD */
              res = (long double)strtod(tmpBuf, &tmpEndPtr);
#  endif /* !EPI_HAVE_STRTOLD */
              if (endptr) *endptr = (char *)s + (tmpEndPtr - tmpBuf);
              /* If only leading whitespace in `s' was parsed, error: */
              if (errnum)
                {
                  if (tmpEndPtr <= tmpBuf + (beginNonWhitespace - s))
                    *errnum = EINVAL;
                  else if (errno)
                    *errnum = errno;
                }
            }
        }
    }
done:
  return(res);
}
#  define STR_TO_HUGEFLOAT      TXstrtold
#else /* !EPI_HAVE_LONG_DOUBLE */
#  define STR_TO_HUGEFLOAT      TXstrtod
#endif /* !EPI_HAVE_LONG_DOUBLE */

EPI_HUGEFLOAT
TXstrtohf(s, e, endptr, errnum)
CONST char      *s;             /* (in) string to convert to huge float */
CONST char      *e;             /* (in, opt.) end of `s' (NULL: s+strlen(s))*/
char            **endptr;       /* (out, opt.) end of `s' converted */
int             *errnum;        /* (out, opt.) errno */
{
  return((EPI_HUGEFLOAT)STR_TO_HUGEFLOAT(s, e, endptr, errnum));
}

#ifdef EPI_UNSAFE_FP_OPS
void
TXdouble2float(dp, fp)
CONST double    *dp;    /* (in) double to convert */
float           *fp;    /* (out) float conversion */
/* Sets `*fp' to `*dp' safely.  (Alpha can FPE when converting
 * out-of-float-range -- or NaN -- double to float.)
 */
{
  if (TXDOUBLE_IS_NaN(*dp))
    TXFLOAT_SET_NaN(*fp);
  else if (*dp > (double)EPI_OS_FLOAT_MAX)
    TXFLOAT_SET_PosInf(*fp);
  else if (*dp < -(double)EPI_OS_FLOAT_MAX)
    TXFLOAT_SET_NegInf(*fp);
  else
    *fp = (float)*dp;
}
#endif /* EPI_UNSAFE_FP_OPS */

#ifdef TEST
#  define ERR_NAME(errNum, alt)                 \
  ((errNum) == ERANGE ? " (ERANGE)" :           \
   ((errNum) == EINVAL ? " (EINVAL)" : (alt)))

static int
TXstrtohTests(void)
{
  short         sVal;
  word          wVal;
  EPI_UINT32    ui32Val;
  EPI_INT32     i32Val;
  EPI_OFF_T     eoVal;
  char          *e, *testS;
  int           numFailed = 0, failed, iVal, errnum;
  long          lVal;
  unsigned long ulVal;
  unsigned      uVal;

#define RUNTEST2(var, func, base, s, expVal, expErr, numCharsParsed)    \
  failed = 0;                                                           \
  testS = (s);                                                          \
  (var) = func(testS, NULL, &e, (base), &errnum);                       \
  if (errnum != (expErr)) { failed = 1; fprintf(stderr,                 \
        "%s:%d: got errnum %d%s expected %d%s\n", __FILE__, (int)__LINE__, \
                                 (int)errnum, ERR_NAME(errnum, ""),     \
                                 (int)(expErr), ERR_NAME(expErr, ""));} \
  if ((var) != (expVal)) { failed = 1; fprintf(stderr,                  \
            "%s:%d: got %" EPI_HUGEINT_PRINTF_PFX "d (%"                \
         EPI_HUGEINT_PRINTF_PFX "x) expected %" EPI_HUGEINT_PRINTF_PFX  \
         "d (%" EPI_HUGEINT_PRINTF_PFX "x)\n", __FILE__,                \
            (int)__LINE__, (EPI_HUGEINT)(var), (EPI_HUGEINT)(var),      \
               (EPI_HUGEINT)(expVal),  (EPI_HUGEINT)(expVal)); }        \
  if ((numCharsParsed) >= 0 && (e - testS) != (numCharsParsed))         \
    {                                                                   \
      failed = 1;                                                       \
      fprintf(stderr, "%s:%d: parse end: %s\n", __FILE__, (int)__LINE__, \
              testS);                                                   \
      fprintf(stderr, "%s:%d:            %*s^\n", __FILE__, (int)__LINE__, \
              (int)(e - testS), "");                                    \
      fprintf(stderr, "%s:%d: expected:  %s\n", __FILE__, (int)__LINE__, \
              testS);                                                   \
      fprintf(stderr, "%s:%d:            %*s^\n", __FILE__, (int)__LINE__, \
              (int)(numCharsParsed), "");                               \
    }                                                                   \
  if (failed) numFailed++;

#define RUNTEST(var, func, s, expVal, expErr)                           \
  RUNTEST2(var, func, (0 | TXstrtointFlag_NoLeadZeroOctal), s,          \
           expVal, expErr, -1)

  RUNTEST(sVal, TXstrtos, "-32768", -32768, 0);         /* min */
  RUNTEST(sVal, TXstrtos, "-32769", -32768, ERANGE);    /* min - 1 */
  RUNTEST(sVal, TXstrtos, "32767", 32767, 0);           /* max */
  RUNTEST(sVal, TXstrtos, "32768", 32767, ERANGE);      /* max + 1 */
  /* was not detecting overflow sometimes: */
  RUNTEST(sVal, TXstrtos, "0x12000", 32767, ERANGE);

  RUNTEST(wVal, TXstrtow, "65535", 65535, 0);           /* max */
  RUNTEST(wVal, TXstrtow, "65536", 65535, ERANGE);      /* max + 1 */
  /* was not detecting overflow sometimes: */
  RUNTEST(wVal, TXstrtow, "0x12000", 65535, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(wVal, TXstrtow, "-1", 65535, 0);
  RUNTEST(wVal, TXstrtow, "-65535", 1, 0);
  RUNTEST(wVal, TXstrtow, "-65536", 65535, ERANGE);

  RUNTEST(i32Val, TXstrtoi32, "0x7fffffff", EPI_INT32_MAX, 0);
  RUNTEST(i32Val, TXstrtoi32, "0x80000000", EPI_INT32_MAX, ERANGE);
  RUNTEST(i32Val, TXstrtoi32, "-0x80000000", -EPI_INT32_MAX - 1, 0);
  RUNTEST(i32Val, TXstrtoi32, "-0x80000001", -EPI_INT32_MAX - 1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(i32Val, TXstrtoi32, "0x120000000", EPI_INT32_MAX, ERANGE);
  RUNTEST(i32Val, TXstrtoi32, "-0x120000000", -EPI_INT32_MAX - 1, ERANGE);

  RUNTEST(ui32Val, TXstrtoui32, "0xffffffff", EPI_UINT32_MAX, 0);
  RUNTEST(ui32Val, TXstrtoui32, "0x100000000", EPI_UINT32_MAX, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(ui32Val, TXstrtoui32, "0x120000000", EPI_UINT32_MAX, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(ui32Val, TXstrtoui32, "-1", EPI_UINT32_MAX, 0);
  RUNTEST(ui32Val, TXstrtoui32, "-0xffffffff", 1, 0);
  RUNTEST(ui32Val, TXstrtoui32, "-0x100000000", EPI_UINT32_MAX, ERANGE);

#if EPI_OS_INT_BITS == 32
  RUNTEST(iVal, TXstrtoi, "0x7fffffff", EPI_OS_INT_MAX, 0);
  RUNTEST(iVal, TXstrtoi, "0x80000000", EPI_OS_INT_MAX, ERANGE);
  RUNTEST(iVal, TXstrtoi, "-0x80000000", -EPI_OS_INT_MAX - 1, 0);
  RUNTEST(iVal, TXstrtoi, "-0x80000001", -EPI_OS_INT_MAX - 1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(iVal, TXstrtoi, "0x120000000", EPI_OS_INT_MAX, ERANGE);
  RUNTEST(iVal, TXstrtoi, "-0x120000000", -EPI_OS_INT_MAX - 1, ERANGE);
#elif EPI_OS_INT_BITS == 64
  RUNTEST(iVal, TXstrtoi, "0x7fffffffffffffff", EPI_OS_INT_MAX, 0);
  RUNTEST(iVal, TXstrtoi, "0x8000000000000000", EPI_OS_INT_MAX, ERANGE);
  RUNTEST(iVal, TXstrtoi, "-0x8000000000000000", -EPI_OS_INT_MAX - 1, 0);
  RUNTEST(iVal, TXstrtoi, "-0x8000000000000001", -EPI_OS_INT_MAX - 1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(iVal, TXstrtoi, "0x12000000000000000", EPI_OS_INT_MAX, ERANGE);
  RUNTEST(iVal, TXstrtoi, "-0x12000000000000000", -EPI_OS_INT_MAX - 1, ERANGE);
#else
#  error Need int tests
  Need int tests;
#endif

#if EPI_OS_UINT_BITS == 32
  RUNTEST(uVal, TXstrtou, "0xffffffff", EPI_OS_UINT_MAX, 0);
  RUNTEST(uVal, TXstrtou, "0x100000000", EPI_OS_UINT_MAX, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(uVal, TXstrtou, "0x120000000", EPI_OS_UINT_MAX, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(uVal, TXstrtou, "-1", EPI_OS_UINT_MAX, 0);
  RUNTEST(uVal, TXstrtou, "-0xffffffff", 1, 0);
  RUNTEST(uVal, TXstrtou, "-0x100000000", EPI_OS_UINT_MAX, ERANGE);
#elif EPI_OS_UINT_BITS == 64
  RUNTEST(uVal, TXstrtou, "0xffffffffffffffff", EPI_OS_UINT_MAX, 0);
  RUNTEST(uVal, TXstrtou, "0x10000000000000000", EPI_OS_UINT_MAX, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(uVal, TXstrtou, "0x12000000000000000", EPI_OS_UINT_MAX, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(uVal, TXstrtou, "-1", EPI_OS_UINT_MAX, 0);
  RUNTEST(uVal, TXstrtou, "-0xffffffffffffffff", 1, 0);
  RUNTEST(uVal, TXstrtou, "-0x10000000000000000", EPI_OS_UINT_MAX, ERANGE);
#else
#  error Need uint tests
  Need uint tests;
#endif

#if EPI_OS_LONG_BITS == 32
  RUNTEST(lVal, TXstrtol, "0x7fffffff", EPI_OS_LONG_MAX, 0);
  RUNTEST(lVal, TXstrtol, "0x80000000", EPI_OS_LONG_MAX, ERANGE);
  RUNTEST(lVal, TXstrtol, "-0x80000000", -EPI_OS_LONG_MAX - 1, 0);
  RUNTEST(lVal, TXstrtol, "-0x80000001", -EPI_OS_LONG_MAX - 1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(lVal, TXstrtol, "0x120000000", EPI_OS_LONG_MAX, ERANGE);
  RUNTEST(lVal, TXstrtol, "-0x120000000", -EPI_OS_LONG_MAX - 1, ERANGE);
#elif EPI_OS_LONG_BITS == 64
  RUNTEST(lVal, TXstrtol, "0x7fffffffffffffff", EPI_OS_LONG_MAX, 0);
  RUNTEST(lVal, TXstrtol, "0x8000000000000000", EPI_OS_LONG_MAX, ERANGE);
  RUNTEST(lVal, TXstrtol, "-0x8000000000000000", -EPI_OS_LONG_MAX - 1, 0);
  RUNTEST(lVal, TXstrtol, "-0x8000000000000001", -EPI_OS_LONG_MAX-1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(lVal, TXstrtol, "0x12000000000000000", EPI_OS_LONG_MAX, ERANGE);
  RUNTEST(lVal, TXstrtol, "-0x12000000000000000", -EPI_OS_LONG_MAX-1, ERANGE);
#else
#  error Need long tests
  Need long tests;
#endif

#if EPI_OS_ULONG_BITS == 32
  RUNTEST(ulVal, TXstrtoul, "0xffffffff", EPI_OS_ULONG_MAX, 0);
  RUNTEST(ulVal, TXstrtoul, "0x100000000", EPI_OS_ULONG_MAX, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(ulVal, TXstrtoul, "0x120000000", EPI_OS_ULONG_MAX, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(ulVal, TXstrtoul, "-1", EPI_OS_ULONG_MAX, 0);
  RUNTEST(ulVal, TXstrtoul, "-0xffffffff", 1, 0);
  RUNTEST(ulVal, TXstrtoul, "-0x100000000", EPI_OS_ULONG_MAX, ERANGE);
#elif EPI_OS_ULONG_BITS == 64
  RUNTEST(ulVal, TXstrtoul, "0xffffffffffffffff", EPI_OS_ULONG_MAX, 0);
  RUNTEST(ulVal, TXstrtoul, "0x10000000000000000", EPI_OS_ULONG_MAX, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(ulVal, TXstrtoul, "0x12000000000000000", EPI_OS_ULONG_MAX, ERANGE);
  /* neg. ok for unsigned if within type size: */
  RUNTEST(ulVal, TXstrtoul, "-1", EPI_OS_ULONG_MAX, 0);
  RUNTEST(ulVal, TXstrtoul, "-0xffffffffffffffff", 1, 0);
  RUNTEST(ulVal, TXstrtoul, "-0x10000000000000000", EPI_OS_ULONG_MAX, ERANGE);
#else
#  error Need long tests
  Need long tests;
#endif

#if EPI_OFF_T_BITS == 32
  RUNTEST(eoVal, TXstrtoepioff_t, "0x7fffffff", EPI_OFF_T_MAX, 0);
  RUNTEST(eoVal, TXstrtoepioff_t, "0x80000000", EPI_OFF_T_MAX, ERANGE);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x80000000", -EPI_OFF_T_MAX - 1, 0);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x80000001", -EPI_OFF_T_MAX - 1, ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(eoVal, TXstrtoepioff_t, "0x120000000", EPI_OFF_T_MAX, ERANGE);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x120000000", -EPI_OFF_T_MAX - 1, ERANGE);
#elif EPI_OFF_T_BITS == 64
  RUNTEST(eoVal, TXstrtoepioff_t, "0x7fffffffffffffff", EPI_OFF_T_MAX, 0);
  RUNTEST(eoVal, TXstrtoepioff_t, "0x8000000000000000", EPI_OFF_T_MAX, ERANGE);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x8000000000000000", -EPI_OFF_T_MAX - 1,
          0);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x8000000000000001", -EPI_OFF_T_MAX - 1,
          ERANGE);
  /* was not detecting overflow sometimes: */
  RUNTEST(eoVal, TXstrtoepioff_t, "0x12000000000000000", EPI_OFF_T_MAX,ERANGE);
  RUNTEST(eoVal, TXstrtoepioff_t, "-0x12000000000000000", -EPI_OFF_T_MAX - 1,
          ERANGE);
#else
#  error Need EPI_OFF_T tests
  Need EPI_OFF_T tests;
#endif

  /* Added ignore-commas flag: ------------------------------------------- */
  /* commas only ok if TXstrtointFlag_Base10CommasOk flag given: */
  RUNTEST2(ui32Val, TXstrtoui32,  0, "12,345,678", 12, 0, 2);
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           "12,345,678", 12345678, 0, 10);
  /* no leading commas: */
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           ",12,345,678", 0, 0, 0);
  /* no adjacent commas: */
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           "12,345,,678", 12345, 0, 6);
  /* no trailing commas: */
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           "12,345,678,", 12345678, 0, 10);
  /* no commas with octal or hex: */
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           "012,345,678", 10, 0, 3);
  RUNTEST2(ui32Val, TXstrtoui32, (0 | TXstrtointFlag_Base10CommasOk),
           "0x12,345,678", 18, 0, 4);

  if (numFailed) printf("*** strtoh: %d test(s) failed ***\n", numFailed);
  else printf("strtoh: all tests passed\n");
  return(numFailed ? 0 : 1);
#undef RUNTEST
}

int
main(int argc, char *argv[])
{
  EPI_HUGEINT   val = -1;
  double        dVal = -1.0;
#ifdef EPI_HAVE_LONG_DOUBLE
  long double   ldVal = -1.0;
#endif /* EPI_HAVE_LONG_DOUBLE */
  char          *e, *func = "strtoh";
  int           base = 0, i, errnum;

  if (argc <= 1)
    {
      fprintf(stderr, "Usage: %s string [base [func]]\n", argv[0]);
      fprintf(stderr, "or: %s --run-tests\n", argv[0]);
      fflush(stderr);
      return(1);
    }
  if (argc == 2 && strcmp(argv[1], "--run-tests") == 0)
    return(TXstrtohTests() ? 0 : 1);
  if (argc >= 3) base = atoi(argv[2]);
  if (argc >= 4) func = argv[3];
  e = CHARPN + 1;
  if (strcmpi(func, "TXstrtoh") == 0)
    val = TXstrtoh(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtouh") == 0)
    val = TXstrtouh(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoepioff_t") == 0)
    val = TXstrtoepioff_t(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "strtol") == 0)
    {
      errno = 0;
      val = strtol(argv[1], &e, base);
      errnum = errno;
    }
  else if (strcmpi(func, "strtoul") == 0)
    {
      errno = 0;
      val = strtoul(argv[1], &e, base);
      errnum = errno;
    }
  else if (strcmpi(func, "TXstrtol") == 0)
    val = TXstrtol(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoul") == 0)
    val = TXstrtoul(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoi") == 0)
    val = TXstrtoi(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtou") == 0)
    val = TXstrtou(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtodw") == 0)
    val = TXstrtodw(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtow") == 0)
    val = TXstrtow(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtos") == 0)
    val = TXstrtos(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoi32") == 0)
    val = TXstrtoi32(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoui32") == 0)
    val = TXstrtoui32(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoi64") == 0)
    val = TXstrtoi64(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtoui64") == 0)
    val = TXstrtoui64(argv[1], NULL, &e, base, &errnum);
  else if (strcmpi(func, "TXstrtod") == 0)
    {
      dVal = TXstrtod(argv[1], NULL, &e, &errnum);
      printf("Value is %g %s from %s\n", dVal,
             (errnum == ERANGE ? "ERANGE" : (errnum == EINVAL ? "EINVAL" :
                                 (errnum ? "error" : "ok"))), func);
      goto afterVal;
    }
#ifdef EPI_HAVE_LONG_DOUBLE
  else if (strcmpi(func, "TXstrtold") == 0)
    {
      ldVal = TXstrtold(argv[1], NULL, &e, &errnum);
      printf("Value is %" EPI_OS_LONG_DOUBLE_G_FMT " %s from %s\n", ldVal,
             ERR_NAME(errnum, (errnum ? "error" : "ok")), func);
      goto afterVal;
    }
#endif /* EPI_HAVE_LONG_DOUBLE */
  else
    {
      fprintf(stderr, "Unknown function `%s'\n", func);
      return(1);
    }
  printf("Value is %" EPI_HUGEINT_PRINTF_PFX "d (0x%" EPI_HUGEINT_PRINTF_PFX
         "x)%s from %s\n", (EPI_HUGEINT)val, (EPI_HUGEINT)val,
         ERR_NAME(errnum, (errnum ? "error" : "ok")), func);
  printf("buf: ");
#  ifdef EPI_BIG_ENDIAN
  for (i = 0; i < EPI_HUGEINT_BITS/8; i++)
#  else
  for (i = EPI_HUGEINT_BITS/8 - 1; i >= 0; i--)
#  endif
    printf("%02X", ((unsigned char *)&val)[i]);
  printf("\n");
afterVal:
  if (e >= argv[1] && e <= argv[1] + strlen(argv[1]))
    printf("[%s]\n %*s^parse end\n", argv[1], (int)(e - argv[1]), "");
  else if (!e)
    printf("parse end is NULL\n");
  else if (e == CHARPN + 1)
    printf("parse end is NULL + 1 (function did not set it?)\n");
  else
    printf("parse end is at %+" EPI_HUGEINT_PRINTF_PFX "d\n",
           (EPI_HUGEINT)(e - argv[1]));
  return(0);
}
#endif /* TEST */
