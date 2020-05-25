#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

/* SQL stringformat() implementation.  Based on Vortex <strfmt>. */

typedef struct  TXSFD_tag                       /* stringformat data */
{
  TXPMBUF       *pmbuf;                         /* (opt.) putmsg buffer */
  const char    *fmtBegin, *fmtEnd;             /* begin/end of format str */
  FLD           *args[MAXFLDARGS];              /* user args */
  size_t        numArgs;                        /* total user arg count */
  size_t        numArgsUsed;                    /* # args processed so far */
  HTPFARG       cvtData[MAXFLDARGS];            /* (opt.) converted data */
  void          *dupData[MAXFLDARGS];           /* (opt.) dup data */
}
TXSFD;
#define TXSFDPN ((TXSFD *)NULL)

/* WTF need to tie into whoever is catching SIGFPE currently: */
#undef CATCH_SIGFPE

static CONST char       Whitespace[] = " \t\r\n\v\f";

/* ------------------------------------------------------------------------ */

static void
TXstringformatResetArgs(TXSFD *info)
/* Resets arguments in `info' for another pass.  Also frees any cached
 * converted data.
 */
{
  size_t        i;

  for (i = 0; i < info->numArgsUsed; i++)
    info->dupData[i] = TXfree(info->dupData[i]);
  info->numArgsUsed = 0;
}

/* ------------------------------------------------------------------------ */

static void *
TXstringformatArgCb(HTPFT type, HTPFW what, void *data, char **fmterr,
                    size_t *sz)
/* Argument callback for htbuf_cpf() call in TXfunc_stringformat();
 * returns next requested argument, casting if needed.
 * See also vortex.c:dofmt_argcb().
 */
{
  static CONST char     fn[] = "TXstringformatArgCb";
  static CONST byte     htpft2ftn[HTPFT_NUM] =
  {
    0,                                          /* HTPFT_VOID */
    FTN_INT,                                    /* HTPFT_UNSIGNED */
    FTN_INT,                                    /* HTPFT_INT */
    0,                                          /* HTPFT_INTPTR */
    FTN_LONG,                                   /* HTPFT_LONG */
    FTN_LONG,                                   /* HTPFT_ULONG */
    FTN_DOUBLE,                                 /* HTPFT_DOUBLE */
    FTN_CHAR,                                   /* HTPFT_CHAR */
    (FTN_CHAR | DDVARBIT),                      /* HTPFT_STR */
    FTN_DATE,                                   /* HTPFT_TIME */
    0,                                          /* HTPFT_PTR */
    FTN_INT64,                                  /* HTPFT_LONGLONG */
    FTN_UINT64,                                 /* HTPFT_ULONGLONG */
    FTN_INT64,                                  /* HTPFT_HUGEINT */
    FTN_UINT64,                                 /* HTPFT_HUGEUINT */
    FTN_DOUBLE,                                 /* HTPFT_LONGDOUBLE */
    FTN_DOUBLE,                                 /* HTPFT_HUGEFLOAT */
  };
  TXSFD         *info = (TXSFD *)data;
  FTN           wantFtnType, haveFtnType;
  int           i;
  FLD           *userArgFld = FLDPN, *wantFld = FLDPN, *res = FLDPN;
  FLDOP         *fo = FLDOPPN;
  void          *ret, *v;
  size_t        nsz;
  char          *e;
  HTPFARG       *u = HTPFARGPN, us;
  time_t        tim;
  struct tm     *tm;
  long          lval;
  int           usedAnArg = 0, errnum;

#ifdef EPI_HAVE_LONG_LONG
#  define OPT_LONG_LONG(v)                      \
      if (type == HTPFT_LONGLONG)               \
        {                                       \
          us.ll = (long long)(v);               \
          *sz = sizeof(us.ll);                  \
        }                                       \
      else
#else /* !EPI_HAVE_LONG_LONG */
#  define OPT_LONG_LONG(v)
#endif /* !EPI_HAVE_LONG_LONG */

#ifdef EPI_HAVE_UNSIGNED_LONG_LONG
#  define OPT_UNSIGNED_LONG_LONG(v)             \
      if (type == HTPFT_ULONGLONG)              \
        {                                       \
          us.ull = (unsigned long long)(v);     \
          *sz = sizeof(us.ull);                 \
        }                                       \
      else
#else /* !EPI_HAVE_UNSIGNED_LONG_LONG */
#  define OPT_UNSIGNED_LONG_LONG(v)
#endif /* !EPI_HAVE_UNSIGNED_LONG_LONG */

  /* NOTE: these especially depend on htpft2ftn array above: */
#define BIG_INT_CASES(vl, uvl, v64, uv64, brk)  \
  case FTN_INT64:                               \
    OPT_LONG_LONG(v64)                          \
      {                                         \
        us.hi = (EPI_HUGEINT)(v64);                 \
        *sz = sizeof(us.hi);                    \
      }                                         \
    brk;                                        \
  case FTN_UINT64:                              \
    OPT_UNSIGNED_LONG_LONG(uv64)                \
      {                                         \
        us.hui = (EPI_HUGEUINT)(uv64);              \
        *sz = sizeof(us.hui);                   \
      }                                         \
    brk;                                        \
  case FTN_LONG:                                \
    if (type == HTPFT_LONG)                     \
      {                                         \
        us.l = (long)(vl);                      \
        *sz = sizeof(us.l);                     \
      }                                         \
    else if (type == HTPFT_ULONG)               \
      {                                         \
        us.ul = (ulong)(uvl);                   \
        *sz = sizeof(us.ul);                    \
      }                                         \
    /* !okint64 cases only: */                  \
    else                                        \
    OPT_LONG_LONG(vl)                           \
    OPT_UNSIGNED_LONG_LONG(uvl)                 \
    if (type == HTPFT_HUGEINT)                  \
      {                                         \
        us.hi = (EPI_HUGEINT)(vl);                  \
        *sz = sizeof(us.hi);                    \
      }                                         \
    else /* if (type == HTPFT_HUGEUINT) */      \
      {                                         \
        us.hui = (EPI_HUGEUINT)(uvl);               \
        *sz = sizeof(us.hui);                   \
      }                                         \
    brk

#ifdef CATCH_SIGFPE
  VXfpe = 0;                                    /* ie. catch SIGFPE */
#  define FPERR()       if (VXfpe > 0) goto castfail
#else
#  define FPERR()
#endif

  if (type == HTPFT_VOID)                       /* no argument */
    {
      char      errCharBuf[64];

      switch (what)
        {
        case HTPFW_START:                       /* reset arg count */
          TXstringformatResetArgs(info);
          break;
        case HTPFW_FMTERR:
          if (!fmterr) break;                   /* sanity check */
          if (*fmterr == info->fmtEnd)          /* Bug 6231 */
            TXstrncpy(errCharBuf, "end of format string", sizeof(errCharBuf));
          else if (**fmterr)
            {
              errCharBuf[0] = '`';
              errCharBuf[1] = **fmterr;
              errCharBuf[2] = '\'';
              errCharBuf[3] = '\0';
            }
          else
            {
              errCharBuf[0] = 'n';
              errCharBuf[1] = 'u';
              errCharBuf[2] = 'l';
              errCharBuf[3] = '\0';
            }
          txpmbuf_putmsg(info->pmbuf, MWARN + UGE, CHARPN,
                    "Syntax error at offset %d (%s) in stringformat() format",
                 (int)(*fmterr - info->fmtBegin), errCharBuf);
          break;
        default:                                /* should not happen */
          break;
        }
      ret = NULL;
      goto done;
    }

  if (info->numArgsUsed >= info->numArgs)       /* too few args */
    {
      if (info->numArgsUsed == info->numArgs)   /* first time */
        txpmbuf_putmsg(info->pmbuf, MWARN + UGE, fn,
                       "Too few arguments for stringformat() format");
      ret = NULL;                               /* print nothing */
      goto done;
    }
  /* get FTN type: */
  wantFtnType = ((unsigned)type >= (unsigned)HTPFT_NUM) ? 0 : htpft2ftn[type];

  /* get the arg: */
  userArgFld = info->args[info->numArgsUsed];
  usedAnArg = 1;                                /* delay `numArgsUsed' inc */
  if (userArgFld == FLDPN) goto err;
  if (wantFtnType == 0)                         /* cannot get that type */
    {
    castfail:
      *fmterr = "?";
      ret = NULL;
      goto done;
    }

  wantFtnType = (wantFtnType & FTN_VarBaseTypeMask);
  haveFtnType = (userArgFld->type & FTN_VarBaseTypeMask);
  if (wantFtnType != haveFtnType)               /* must cast the field */
    {
      /* Hack-o-rama: if it's a cast that FOP_CNV doesn't like for various
       * reasons, do it ourselves:
       */
      ret = getfld(userArgFld, &nsz);
      if (wantFtnType == FTN_DATE)
        {
          switch (haveFtnType)
            {
            case FTN_DOUBLE:
              us.t = (time_t)(*((ft_double *)ret));
              FPERR();
              break;
            case FTN_FLOAT:
              us.t = (time_t)(*((ft_float *)ret));
              FPERR();
              break;
            default:    goto castit;
            }
          *sz = sizeof(us.t);
          goto udup;
        }
      else if (haveFtnType == FTN_DATE || haveFtnType == FTN_COUNTER)
        {
          if (haveFtnType == FTN_DATE)
            tim = (time_t)(*((ft_date *)ret));
          else
            tim = (time_t)((ft_counter *)ret)->date;
          if (what == HTPFW_ROMAN)      /* bonus: if roman num, just year */
            {
              tm = localtime(&tim);
              tim = 1900 + tm->tm_year;
            }
          switch (wantFtnType)
            {
            BIG_INT_CASES(tim, tim, tim, tim, goto udup);
            case FTN_INT:
              us.i = (int)tim;
              *sz = sizeof(us.i);
              goto udup;
            case FTN_DOUBLE:
              switch (type)
                {
                case HTPFT_DOUBLE:
                  us.d = (double)tim;
                  FPERR();
                  *sz = sizeof(us.d);
                  goto udup;
                case HTPFT_LONGDOUBLE:
#ifdef EPI_HAVE_LONG_DOUBLE
                  us.longDouble = (long double)tim;
                  FPERR();
                  *sz = sizeof(us.longDouble);
                  goto udup;
#else /* !EPI_HAVE_LONG_DOUBLE */
                  goto castfail;
#endif /* !EPI_HAVE_LONG_DOUBLE */
                case HTPFT_HUGEFLOAT:
                  us.hugeFloat = (EPI_HUGEFLOAT)tim;
                  FPERR();
                  *sz = sizeof(us.hugeFloat);
                  goto udup;
                default:                        /* should not happen */
                  goto castfail;
                }
              break;
            case FTN_CHAR:
              wantFtnType |= DDVARBIT; /* fldmath does varchar but not char */
              break;
            default:
              break;
            }
        }
      /* fldmath will give hex for varbyte -> varchar conversion:  970207 */
      else if ((wantFtnType & ~DDVARBIT) == FTN_CHAR)
        {
          /* int -> char: unlike fldmath, take int as ASCII char:  970325 */
          if (wantFtnType == FTN_CHAR)
            {
              if (what == HTPFW_DECODECHAR)     /* %!c */
                {
                  wantFtnType |= DDVARBIT;
                  goto chkit1;
                }
              else switch (haveFtnType)
                {
                  case FTN_LONG:
                    us.c = (char)(*((ft_long *)ret));
                    break;
                  case FTN_INT:
                  case FTN_INTEGER:
                    us.c = (char)(*((ft_int *)ret));
                    break;
                  case FTN_DOUBLE:
                    us.c = (char)(*((ft_double *)ret));
                    FPERR();
                    break;
                  case FTN_FLOAT:
                    us.c = (char)(*((ft_float *)ret));
                    FPERR();
                    break;
                  case FTN_CHAR:      /* parse hex/decimal string as ASCII */
                  case FTN_CHAR | DDVARBIT:
                    if (nsz <= 0) goto nocast;
                    lval = TXstrtol((char *)ret, CHARPN, &e,
                                    (0 | TXstrtointFlag_NoLeadZeroOctal),
                                    &errnum);
                    if (*(char *)ret != '\0' && *e == '\0' && errnum == 0)
                      {
                        us.c = (char)(lval & 0xffL);
                        break;
                      }
                    /* fall through */
                  case FTN_BYTE:
                  case FTN_BYTE | DDVARBIT:
                  /* fldmath will barf for byte -> char: needs space for hex.
                   * so any [var]{byte|char} -> [var]char leave alone:  970325
                   */
                    goto nocast;
                  default:    goto castit;
                }
              *sz = 1;
              goto udup;
            }
        chkit1:
          if ((haveFtnType & ~DDVARBIT) == FTN_CHAR ||
              (haveFtnType & ~DDVARBIT) == FTN_BYTE)
            goto nocast;
        }
      else if ((haveFtnType & ~DDVARBIT) == FTN_CHAR)
        {                                   /* parse hex/decimal, long long */
          if (wantFtnType == FTN_LONG && what == HTPFW_LOCATION)
            {
              us.l = TXparseLocation((char *)ret, &e, NULL, NULL);
              if (us.l == -1L) goto castfail;
              e += strspn(e, Whitespace);
              if (*e != '\0') goto castfail;
              *sz = sizeof(us.l);
              goto udup;
            }
          switch (wantFtnType)
            {
            BIG_INT_CASES(TXstrtoh((char *)ret, CHARPN, &e,
                                   (0 | TXstrtointFlag_NoLeadZeroOctal),
                                   &errnum),
                          TXstrtoh((char *)ret, CHARPN, &e,
                                   (0 | TXstrtointFlag_NoLeadZeroOctal),
                                   &errnum),
                          TXstrtoh((char *)ret, CHARPN, &e,
                                   (0 | TXstrtointFlag_NoLeadZeroOctal),
                                   &errnum),
                          TXstrtoh((char *)ret, CHARPN, &e,
                                   (0 | TXstrtointFlag_NoLeadZeroOctal),
                                   &errnum),
                          if (*(char *)ret != '\0' && *e == '\0' &&
                              errnum == 0)
                            goto udup;
                          break);
            case FTN_INT:
            case FTN_INTEGER:
              us.i = (int)TXstrtoi32((char *)ret, CHARPN, &e,
                                     (0 | TXstrtointFlag_NoLeadZeroOctal),
                                     &errnum);
              *sz = sizeof(us.i);
              if (*(char *)ret != '\0' && *e == '\0' && errnum == 0)
                goto udup;
              break;
            case FTN_DOUBLE:
              switch (type)
                {
                case HTPFT_LONGDOUBLE:
#ifdef EPI_HAVE_LONG_DOUBLE
                  us.longDouble = TXstrtold((char *)ret, CHARPN, &e, &errnum);
                  *sz = sizeof(us.longDouble);
                  if (*(char *)ret != '\0' && *e == '\0' && errnum == 0)
                    goto udup;
                  break;
#else /* !EPI_HAVE_LONG_DOUBLE */
                  goto castfail;
#endif /* !EPI_HAVE_LONG_DOUBLE */
                case HTPFT_HUGEFLOAT:
                  us.hugeFloat = TXstrtohf((char *)ret, CHARPN, &e, &errnum);
                  *sz = sizeof(us.hugeFloat);
                  if (*(char *)ret != '\0' && *e == '\0' && errnum == 0)
                    goto udup;
                  break;
                default:
                  break;
                }
              if (what != HTPFW_LATITUDE && what != HTPFW_LONGITUDE) break;
              us.d = TXparseCoordinate((char *)ret, (what == HTPFW_LONGITUDE),
                                       &e);
              FPERR();
              if (TXDOUBLE_IS_NaN(us.d)) goto castfail;
              e += strspn(e, Whitespace);
              if (*e != '\0') goto castfail;
              *sz = sizeof(us.d);
              goto udup;
            default:
              ; /* win32 compiler doesn't like an empty case at the end */
            }
        }

    castit:
      /* TXresettimecache(); wtf Vortex only? */ /* so "now" really works */
      if ((wantFld = createfld(ddfttypename(wantFtnType), 1, 0)) == FLDPN)
        goto err;
      /* Put any value into field.  Not used, but fldmath cores if NULL: */
      putfld(wantFld, "", 0);                /* wtf if error? */
      if (fo == FLDOPPN &&
          (fo = TXgetFldopFromCache()) == FLDOPPN)
        goto err;
      if (fopush(fo, userArgFld) != 0 ||
          fopush(fo, wantFld) != 0)
        goto err;
      /* wtf Vortex turned off putmsg here, in case bad date conversions */
      i = foop(fo, FOP_CNV);
      /* restore putmsg here */
      FPERR();
      if (i != 0) goto castfail;                /* cast failed */
      if ((res = fopop(fo)) == FLDPN) goto err;
      v = getfld(res, &nsz);
      *sz = nsz*ddftsize(res->type);
      /* Dup the data; the arg must stick around for the duration of
       * htbuf_cpf():
       */
      if ((ret = TXmalloc(info->pmbuf, fn, *sz + 1)) == NULL) goto err;
      info->dupData[info->numArgsUsed] = ret;   /* remember for later free */
      memcpy(ret, v, *sz);
      ((char *)ret)[*sz] = '\0';                /* in case string */
      if ((res->type & FTN_VarBaseTypeMask) != wantFtnType)
        goto castfail;                          /* wtf should really be err */
    }
  else                                          /* no cast needed */
    {
    nocast:
      ret = getfld(userArgFld, &nsz);
      *sz = nsz*ddftsize(haveFtnType);
    }

  /* Now cast FTN to C type: */
  switch (wantFtnType)
    {
    case FTN_INT:
    case FTN_INTEGER:
      if (type == HTPFT_UNSIGNED)
        {
          us.u = (unsigned)(*((ft_int *)ret));
          *sz = sizeof(us.u);
        }
      else
        {
          us.i = (int)(*((ft_int *)ret));
          *sz = sizeof(us.i);
        }
      break;
    BIG_INT_CASES(*((ft_long *)ret), *((ft_long *)ret),
                  *((ft_int64 *)ret), *((ft_uint64 *)ret), break);
    case FTN_DOUBLE:
      switch (type)
        {
        case HTPFT_LONGDOUBLE:
#ifdef EPI_HAVE_LONG_DOUBLE
          us.longDouble = (long double)(*((ft_double *)ret));
          FPERR();
          *sz = sizeof(us.longDouble);
          break;
#else /* !EPI_HAVE_LONG_DOUBLE */
          goto castfail;
#endif /* !EPI_HAVE_LONG_DOUBLE */
        case HTPFT_HUGEFLOAT:
          us.hugeFloat = (EPI_HUGEFLOAT)(*((ft_double *)ret));
          FPERR();
          *sz = sizeof(us.hugeFloat);
          break;
        default:
          us.d = (double)(*((ft_double *)ret));
          FPERR();
          *sz = sizeof(us.d);
          break;
        }
      break;
    case FTN_DATE:
      us.t = (time_t)(*((ft_date *)ret));
      *sz = sizeof(us.t);
      break;
    default: goto done;
    }
udup:
  u = &info->cvtData[info->numArgsUsed];
  *u = us;
  ret = u;
  goto done;

err:
  *fmterr = "(err)";
  ret = NULL;
done:
  if (res != FLDPN) closefld(res);
  if (wantFld != FLDPN) closefld(wantFld);
  if (fo != FLDOPPN) fo = TXreleaseFldopToCache(fo);
#ifdef CATCH_FPE
  VXfpe = -1;                   /* ie. next SIGFPE causes abend */
#endif
  if (usedAnArg) info->numArgsUsed++;
  return(ret);
#undef BIG_INT_CASES
#undef OPT_LONG_LONG
#undef OPT_UNSIGNED_LONG_LONG
}

int
TXfunc_stringformat(FLD *fmtFld, FLD *argFld1, FLD *argFld2, FLD *argFld3,
                    FLD *argFld4)
/* SQL function stringformat(fmt[, arg[, arg[, arg[, arg]]]]).
 * `fmtFld' must be varchar; `argFld...'s may be any type.
 * Returns 0 on success, else FOP_... error.
 */
{
  static CONST char     fn[] = "TXfunc_stringformat";
  HTBUF                 *outBuf = HTBUFPN;
  size_t                outDataLen, fmtLen;
  char                  *outData, *fmtData = CHARPN, *fmt;
  int                   ret;
  TXSFD                 info;
  TXPMBUF               *pmbuf = TXPMBUFPN;

  memset(&info, 0, sizeof(TXSFD));
  info.pmbuf = pmbuf;
  /* rest cleared by memset() */

  /* Get the format string: - - - - - - - - - - - - - - - - - - - - - - - - */
  if ((fmtFld->type & DDTYPEBITS) != FTN_CHAR)  /* safety check */
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
                     "Wrong first argument type: expected [var]char");
      ret = FOP_EINVAL;
      goto done;
    }
  fmt = (char *)getfld(fmtFld, &fmtLen);
  if (fmt == CHARPN)
    {
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn, "NULL format argument");
      ret = FOP_EINVAL;
      goto done;
    }
  fmtData = TXcesc2str(fmt, fmtLen, &fmtLen);
  if (fmtData == CHARPN) goto noMem;

  /* Alloc and prep `outBuf': - - - - - - - - - - - - - - - - - - - - - - */
  if ((outBuf = openhtbuf()) == HTBUFPN) goto noMem;
#define ADDARG(a)                                       \
 if ((a) != FLDPN && info.numArgs < MAXFLDARGS) info.args[info.numArgs++] = (a)
  ADDARG(argFld1);
  ADDARG(argFld2);
  ADDARG(argFld3);
  ADDARG(argFld4);
  info.numArgsUsed = 0;
#undef ADDARG
  /* WTF could restore/save a TXFMTSTATE, e.g. for continuing
   * Metamorph hit markup anchor numbers.  But we have no SQL handle
   * state to hang it off of, and hanging it on the global (TXApp)
   * state is too broad: would continue across independent SQL handles.
   */

  /* Do the actual printing: - - - - - - - - - - - - - - - - - - - - - - - */
  info.fmtBegin = fmtData;
  info.fmtEnd = fmtData + fmtLen;
  if (!htbuf_cpf(outBuf, fmtData, fmtLen, HTPFF_PROMOTE_TO_HUGE,
                 TXstringformatArgCb, (void *)&info))
    goto err;
  if (info.numArgsUsed < info.numArgs)
    txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                   "Too many arguments for stringformat() format");

  /* Copy the output to the field: - - - - - - - - - - - - - - - - - - - - */
  if (!htbuf_write(outBuf, "", 0)) goto noMem;  /* ensure non-NULL outData */
  outDataLen = htbuf_getdata(outBuf, &outData, 0x3);
  releasefld(fmtFld);                           /* before changing type */
  fmtFld->type = (FTN_CHAR | DDVARBIT);
  fmtFld->elsz = sizeof(ft_char);
  setfldandsize(fmtFld, outData, sizeof(ft_char)*outDataLen + 1, FLD_FORCE_NORMAL);
  ret = 0;                                      /* success */
  goto done;

err:
  ret = FOP_EUNKNOWN;
  goto done;
noMem:
  ret = FOP_ENOMEM;
done:
  outBuf = closehtbuf(outBuf);
  fmtData = TXfree(fmtData);
  TXstringformatResetArgs(&info);               /* free cache data */
  return(ret);
}
