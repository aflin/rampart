/* 
   some of this is code copied from texis/texisapi dir 
   see copyright messages there.
*/

typedef struct  RPSFD_tag                       /* stringformat data */
{
  TXPMBUF       *pmbuf;                         /* (opt.) putmsg buffer */
  const char    *fmtBegin, *fmtEnd;             /* begin/end of format str */
  FLD           **args;                         /* user args */
  size_t        numArgs;                        /* total user arg count */
  size_t        numArgsUsed;                    /* # args processed so far */
  HTPFARG       *cvtData;                       /* (opt.) converted data */
  void          **dupData;                       /* (opt.) dup data */
}
RPSFD;
#define RPSFDPN ((RPSFD *)NULL)

/* WTF need to tie into whoever is catching SIGFPE currently: */
#undef CATCH_SIGFPE

static CONST char       Whitespace[] = " \t\r\n\v\f";

/* ------------------------------------------------------------------------ */

static void
RPstringformatResetArgs(RPSFD *info)
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
RPstringformatArgCb(HTPFT type, HTPFW what, void *data, char **fmterr,
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
  RPSFD         *info = (RPSFD *)data;
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
          RPstringformatResetArgs(info);
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


FLD *rp_add_arg(duk_context *ctx, duk_idx_t idx)
{
  FLD *ret=NULL;
  
  /* convert object to json or string */
  if(duk_is_object(ctx,idx))
  {
    duk_push_string(ctx,"function(o){return JSON.stringify(o)}");
    duk_push_string(ctx,"internal");
    duk_compile(ctx,DUK_COMPILE_FUNCTION);
    duk_dup(ctx,idx);
    if(duk_pcall(ctx,1))
      duk_pop(ctx);
    else
      duk_replace(ctx,idx);

   //duk_json_encode(ctx,idx);//doesn't catch errors
   //no goto done
  }

  if(!duk_is_string(ctx,idx) && ! duk_is_number(ctx,idx) )
    duk_safe_to_string(ctx,idx);

  if(duk_is_string(ctx,idx))
  {
    duk_size_t sz;
    char *s=(char *)duk_get_lstring(ctx,idx,&sz);

    if((ret=createfld("varchar",1,0)) == FLDPN)
      goto err;

    putfld(ret,s,sz);
    goto done;
  }
  
  if(duk_is_number(ctx,idx))
  {
    double *d=NULL;
    REMALLOC(d,sizeof(double));
    
    *d=(double)duk_get_number(ctx,idx);
    if((ret=createfld("double",1,0)) == FLDPN)
      goto err;

    putfld(ret,d,1);
    goto done;
  }


  err:
    duk_push_string(ctx,"stringformat: error assigning value to field");
    duk_throw(ctx);

  done:
  return ret;
}


duk_ret_t
RPfunc_stringformat(duk_context *ctx)
{
  static CONST char     fn[] = "TXfunc_stringformat";
  HTBUF                 *outBuf = HTBUFPN;
  size_t                outDataLen, fmtLen;
  char                  *outData, *fmtData = CHARPN, *fmt;
  RPSFD                 info;
  TXPMBUF               *pmbuf = TXPMBUFPN;
  int 			i,argn=(duk_get_top(ctx)<2)?1:duk_get_top(ctx);
  duk_idx_t		curidx=1;
  duk_size_t            duk_sz;
  FLD			*args[argn];
  HTPFARG		cvtData[argn];
  void			*dupData[argn];


  if (duk_get_top(ctx)==0)
  {
    duk_push_string(ctx,"stringformat: arguments required");
    duk_throw(ctx);
  }

  memset(&info, 0, sizeof(RPSFD));
  memset(&dupData,0,argn*sizeof(void *));

  info.pmbuf = pmbuf;
  info.args=args;
  info.cvtData=cvtData;
  info.dupData=dupData;  

  fmt=(char *)duk_require_lstring(ctx,0,&duk_sz);
  fmtLen=(size_t)duk_sz;
  fmtData = TXcesc2str(fmt, fmtLen, &fmtLen);

  if (fmtData == CHARPN) goto noMem;

  /* Alloc and prep `outBuf': - - - - - - - - - - - - - - - - - - - - - - */
  if ((outBuf = openhtbuf()) == HTBUFPN) goto noMem;

  while (curidx < argn )
  {
    info.args[info.numArgs++]=rp_add_arg(ctx,curidx);
    curidx++;
  }


  /* Do the actual printing: - - - - - - - - - - - - - - - - - - - - - - - */
  info.fmtBegin = fmtData;
  info.fmtEnd = fmtData + fmtLen;
  if (!htbuf_cpf(outBuf, fmtData, fmtLen, HTPFF_PROMOTE_TO_HUGE,
                 RPstringformatArgCb, (void *)&info))
    goto err;
  if (info.numArgsUsed < info.numArgs)
    txpmbuf_putmsg(pmbuf, MWARN + UGE, fn,
                   "Too many arguments for stringformat() format");

  /* Copy the output to the field: - - - - - - - - - - - - - - - - - - - - */
  if (!htbuf_write(outBuf, "", 0)) goto noMem;  /* ensure non-NULL outData */
  outDataLen = htbuf_getdata(outBuf, &outData, 0x3);
  duk_push_lstring(ctx,outData,(duk_size_t)outDataLen);
  free(outData);
  for (i=0;i<info.numArgs;i++)
    closefld(info.args[i]);

    outBuf = closehtbuf(outBuf);
    fmtData = TXfree(fmtData);
    RPstringformatResetArgs(&info);               /* free cache data */

    return 1;

  err:
    for (i=0;i<info.numArgs;i++)
      closefld(info.args[i]);

    duk_push_string(ctx,"stringformat failed");
    duk_throw(ctx);

  noMem:
    duk_push_string(ctx,"out of memeory in stringformat");
    duk_throw(ctx);

}

duk_ret_t
RPsqlFuncs_abstract(duk_context *ctx)
{
	ft_char *text, *query = CHARPN;
	ft_long maxsz=230;
	ft_long style = TXABS_STYLE_SMART;
	DBTBL *tbl=NULL;
	duk_idx_t idx=0;
	char *ab;

	if (globalcp == APICPPN) globalcp = TXopenapicp();

	if( duk_is_string(ctx,idx) || ( (idx=1) && duk_is_string(ctx,idx) ) )
	{
	    text=(ft_char *)duk_get_string(ctx,idx);
	}
	else
	{
	    duk_push_string(ctx,"abstract: no text provided");
	    duk_throw(ctx);
	}

	idx=!idx;
	
	if( duk_is_object(ctx,idx) )
	{
	    if(duk_get_prop_string(ctx,idx,"max"))
	        maxsz=(ft_long)duk_require_number(ctx,-1);
            duk_pop(ctx);

	    if(duk_get_prop_string(ctx,idx,"maxsize"))
	        maxsz=(ft_long)duk_require_number(ctx,-1);
            duk_pop(ctx);

            if(duk_get_prop_string(ctx,idx,"query"))
                query=(ft_char *)strdup(duk_require_string(ctx,-1));
            duk_pop(ctx);

            if(duk_get_prop_string(ctx,idx,"q"))
                query=(ft_char *)strdup(duk_require_string(ctx,-1));
            duk_pop(ctx);

            if(duk_get_prop_string(ctx,idx,"style"))
                style= TXstrToAbs((char *)duk_require_string(ctx,-1));
            duk_pop(ctx);

	}
	ab=(char*)abstract(text, maxsz, style, query, tbl, CHARPPN, CHARPN);
	duk_push_string(ctx, ab);
	free(query);
	free(ab);
	return 1;
}

static const char *get_exp(duk_context *ctx, duk_idx_t idx)
{
    const char *ret=NULL;

    if(duk_is_object(ctx,idx) && duk_has_prop_string(ctx,idx,"source") )
    {
        duk_get_prop_string(ctx,idx,"source");
        ret=duk_get_string(ctx,-1);
        duk_pop(ctx);
    }
    else if ( duk_is_string(ctx,idx) )
        ret=duk_get_string(ctx,idx);

    return ret;
}

char **VXsandr ARGS((char **, char **, char **));


static void sandr(duk_context *ctx, int re2)
{
        int ns=1, nr=1, nt=1;
        if(duk_get_top(ctx) != 3)
        {
            duk_push_string(ctx,"sandr: exactly three arguments required: search, replace, text_to_search\n");
            duk_throw(ctx);
        }

        if(duk_is_array(ctx,0))
            ns=(int)duk_get_length(ctx,0);
        else if (!duk_is_string(ctx,0) && !duk_is_object(ctx,0))
        {
            duk_push_string(ctx,"sandr: search (arg 1) must be a string/pattern or array of strings/patterns");
            duk_throw(ctx);
        }

        if(duk_is_array(ctx,1))
            nr=(int)duk_get_length(ctx,1);
        else if (!duk_is_string(ctx,1))
        {
            duk_push_string(ctx,"sandr: replace (arg 2) must be a string or array of strings");
            duk_throw(ctx);
        }

        if(duk_is_array(ctx,2))
            nt=(int)duk_get_length(ctx,2);
        else if (!duk_is_string(ctx,2))
        {
            duk_push_string(ctx,"sandr: text (arg 3) must be a string or array of strings");
            duk_throw(ctx);
        }

#define getstrings(s,idx,arg,argn) do{\
        int i=0;\
        duk_enum(ctx,idx,DUK_ENUM_ARRAY_INDICES_ONLY);\
        while (duk_next(ctx, -1, 1)) { \
            if(!duk_is_string(ctx,-1)) {duk_push_sprintf(ctx,"sandr: %s (arg %d) array must contain only strings",arg,argn);duk_throw(ctx);}\
            s[i]=(char*)duk_get_string(ctx,-1);\
            /*printf("%s[%d]=%s\n",arg,i,s[i]);*/\
            i++;\
            duk_pop_2(ctx);\
        }\
        duk_pop(ctx);\
}while(0)

        {

            char *srch[ns+1], *repl[nr+1];
            char *in[nt+1], **out;

            srch[ns] = NULL;
            repl[nr] = NULL;
            in[nt] = NULL;

            if (ns>1) 
            {
                //getstrings(srch,0,"search",1);
                int i=0;
                duk_enum(ctx,0,DUK_ENUM_ARRAY_INDICES_ONLY);
                while (duk_next(ctx, -1, 1)) 
                {
                    const char *exp=get_exp(ctx,-1);
                    if(!exp)
                    {
                        duk_push_string(ctx,"sandr: search (arg 1) array must contain only strings/patterns");
                        duk_throw(ctx);
                    }
                    if (re2)
                    {
                        char s[strlen(exp)+8];

                        strcpy(s,"\\<re2\\>");
                        strcat(s,exp);
                        srch[i]=strdup(s);
                    }
                    else
                        srch[i]=(char*)exp;
                    i++;
                    duk_pop_2(ctx);
                }
                duk_pop(ctx);
            }
            else
            {
                const char *exp=get_exp(ctx,0);
                if(!exp)
                {
                    duk_push_string(ctx,"sandr: search (arg 1) array must contain only strings/patterns");
                    duk_throw(ctx);
                }
                if (re2)
                {
                    char s[strlen(exp)+8];
                    
                    strcpy(s,"\\<re2\\>");
                    strcat(s,exp);
                    srch[0]=strdup(s);
                }
                else
                    srch[0]=(char*)exp;
            }

            if (nr>1)
                getstrings(repl,1,"replace",2);
            else
                repl[0]=(char*)duk_get_string(ctx,1);

            if (nt>1)
                getstrings(in,2,"text",3);
            else
                in[0]=(char*)duk_get_string(ctx,2);

            out = VXsandr(srch, repl, in);

            if (out && out[0])
            {
                if(out[1]!=NULL) //return array
                {
                    int i=0;
                    duk_push_array(ctx);
                    while(out[i])
                    {
                        //printf("%d: %s\n",i,out[i]);
                        duk_push_string(ctx,out[i]);
                        duk_put_prop_index(ctx, -2, i);
                        free(out[i]);
                        i++;
                    }
                }
                else
                {
                    duk_push_string(ctx,out[0]);
                    free(out[0]);
                }
            }
            if (out)
                    free(out);
            if(re2)
            {
                int i=0;
                
                for(;i<ns;i++)
                    free (srch[i]);
            }
        }
#undef getstrings
}


static int rex (
    duk_context *ctx, 
    byte *str, 
    byte *end, 
    duk_idx_t opt_idx, 
    duk_idx_t func_idx, 
    TXrexSyntax type, 
    int i
){
    RLEX *rl;
    const char *exp;
    int ret_subs=0,docallback=1,nexp=1;
    int rem=2; /* default to "duplicate" */
    duk_idx_t ret_idx;
    byte *p, *last_p=NULL;
    int last_psz=0;
    /* stack:
        0: string or array of rex/re2 patterns
        1: string or filename, ignored here
        2: object, callback or undefined (corresponding to func_idx and opt_idx)
        3: object, callback or undefined (but not same as 2 unless both undefined)
        4: return object from prev run from rexfile 
    */
    /* return subexpressions by default if have callback */
    if(func_idx>0)
        ret_subs=1;

    /* get options */
    if( opt_idx>0 )
    {
        /* get options: exclude */
        if( duk_get_prop_string(ctx,opt_idx,"exclude") )
        {
            const char *s=duk_require_string(ctx,-1); 
            if(!strcmp("none",s))
                rem=0;
            else if (!strncmp("overlap",s,7))
                rem=1;
            else if(!strncmp("duplicate",s,9))
                rem=2;
        }
        duk_pop(ctx);

        /* get options: submatches */
        if( duk_get_prop_string(ctx,opt_idx,"submatches") )
        {
            if(duk_is_boolean(ctx,-1))
                ret_subs=(int)duk_get_boolean(ctx,-1);
        }
        duk_pop(ctx);
    }


    /* get expression */
    if (duk_is_array(ctx,0))
    {
        nexp=(int)duk_get_length(ctx,0);    
    }
    else if (! (exp=get_exp(ctx,0)) )
    {
        duk_push_string(ctx,"rex: expression (arg 1), must be a string or pattern, or array of strings/patterns");
        duk_throw(ctx);
    }

    /* open lexer */
    if ((rl=openrlexadd(nexp)) == RLEXPN)
    {
        duk_push_string(ctx,"rex: error opening lexer");
        duk_throw(ctx);
    }

    /* add expression */
    if (nexp==1)
    {
        if(!rlex_addexp(rl, 0, exp, type))
        {
            closerlex(rl);
            duk_push_sprintf(ctx,"rex: error in expression '%s'",exp);
            duk_throw(ctx);
        }
    }
    else
    /* add multiple expressions */
    {
        int eno=0;
        duk_enum(ctx,0,DUK_ENUM_ARRAY_INDICES_ONLY);
        while (duk_next(ctx, -1, 1)) {
            exp=get_exp(ctx,-1);
            if(!exp)
            {
                closerlex(rl);
                duk_push_string(ctx,"rex: expression array (arg 1) contains something not a string or pattern");
                duk_throw(ctx);
            }
            if(!rlex_addexp(rl, eno, exp, type))
            {
                closerlex(rl);
                duk_push_sprintf(ctx,"rex: error in expression '%s'",exp);
                duk_throw(ctx);
            }
            duk_pop_2(ctx);
            eno++;
        }
        duk_pop(ctx);            
    }

    /* if no callback, return array of matches */
    if(func_idx==-1)
    {
        /* "return" from previous loop in rexfile will be at top of stack.  Add to that.*/
        if(!duk_is_array(ctx,-1))
            duk_push_array(ctx);
        docallback=0;
    }

    ret_idx=duk_get_top_index(ctx); /* should always be 4 */


/* true if two strings overlap */
#define overlaps(a,asz,b,bsz)    ( ((a)>=(b) && (a)<=(b)+(bsz)) || ((b)>=(a) && (b)<=(a)+(asz)) )

/* true if b is a substring of a */
#define encompasses(a,asz,b,bsz) ( ((a)<=(b) && (b)+(bsz)<=(a)+(asz)) )

/* call function with single hit */
#define runcallback do{\
    if(ret_subs)duk_call(ctx,3);/*[ function, match, submatches, i ] */\
    else duk_call(ctx,2); /*[ function, match, i] */\
}while(0)

/* push { expressionIndex:idx, submatches:[list,of,submatches] } */
#define pushsubhit do{\
    duk_push_object(ctx);\
    duk_push_int(ctx,rl->i);\
    duk_put_prop_string(ctx,-2,"expressionIndex");\
    duk_push_array(ctx);\
    if(type==TXrexSyntax_Rex){\
        FFS *sub= rl->ilst[rl->i].ex->first;\
        FFS *last=rl->ilst[rl->i].ex->last;\
        do\
        {\
            duk_push_lstring(ctx,sub->hit,(duk_size_t)sub->hitsize);\
            duk_put_prop_index(ctx,-2,(int)sub->subExprIndex);\
            if(sub==last) break;\
            sub=sub->next;\
        } while (1);\
    }else{\
        int i=0,n=rl->ilst[rl->i].ex->re2NumCaptureGroups;\
        const char **subh=rl->ilst[rl->i].ex->re2CaptureHits;\
        size_t *ssize=rl->ilst[rl->i].ex->re2CaptureHitSizes;\
        for (;i<n;i++){\
            duk_push_lstring( ctx, subh[i], (duk_size_t)ssize[i] );\
            duk_put_prop_index(ctx,-2,i);\
        }\
    }\
    duk_put_prop_string(ctx,-2,"submatches");\
}while(0)


    if (docallback)
    {

        for(p=getrlex(rl,str,end,SEARCHNEWBUF);
            p!=BPNULL;
            p=getrlex(rl,str,end,CONTINUESEARCH) 
        ){
            int  psz;

            p = rlexhit(rl);
            psz=rlexlen(rl);

            /* if overlap with last, user longer of the two */
            if( (rem==1 && overlaps(p,psz,last_p,last_psz)) || (rem==2 && encompasses(last_p,last_psz,p,psz)) )
            {
                /* can't undo what we've already returned (like in no callback), so use the last one */
                if(psz>last_psz)//remove last one, and continue to process this one
                {
                    duk_pop_3(ctx); //remove [match|function, submatches|match, i] 
                    if(ret_subs)
                        duk_pop(ctx); //remove [function]
                }
                else
                    continue;//skip this one, check last with next
            }
            else if(last_p) /* skip first loop, nothing on stack yet */
            {
                /* process last one */
                runcallback;
                if(duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1)) /* exit loop if cb return is false */
                {
                    goto end;
                }
                duk_pop(ctx);
            }

            /* set up next loop */
            duk_dup(ctx,func_idx);/* copy function, setup for callback */
            duk_push_lstring(ctx,p,(duk_size_t)psz);

            if(ret_subs)
                pushsubhit;

            duk_push_int(ctx,i); // index of current match

            last_p=p;
            last_psz=psz;
            
            i++;
        }//for
        // do the last one on the stack, ignore return val since it's last 
        runcallback;
        duk_pop(ctx);
        
    } // end callback
    else //no callback
    {

        for(p=getrlex(rl,str,end,SEARCHNEWBUF);
            p!=BPNULL;
            p=getrlex(rl,str,end,CONTINUESEARCH) 
        ){
            int  psz;

            p = rlexhit(rl);
            psz=rlexlen(rl);
            /* if overlap with last, user longer of the two */
            if( (rem==1 && overlaps(p,psz,last_p,last_psz)) || (rem==2 && encompasses(last_p,last_psz,p,psz)) )
            {

                if(psz>last_psz)//remove last one
                {
                    i--;
                    duk_del_prop_index(ctx,ret_idx,i);
                }
                else
                {
                    continue;//skip this one
                }

            }

            if(ret_subs)
                pushsubhit;

            /* put it in an array to be returned */
            duk_push_lstring(ctx,p,(duk_size_t)psz);
            if(ret_subs)
                /* put match into subhit object */
                duk_put_prop_string(ctx,-2,"match"); /* {expressionIndex:X,submatches:Y,match:Z} is on top of stack */
            /* now top of stack is the match string, or object if ret_sub */
            duk_put_prop_index(ctx, ret_idx, i);/* put obj or string into array */

            last_p=p;
            last_psz=psz;
            
            i++;
        }//for
    }//end no callback
    end:
    if(docallback) /* return number of matches */
    {
        if(duk_is_number(ctx,ret_idx))
            duk_pop(ctx);
        duk_push_int(ctx,(i));
    }
    closerlex(rl);

    return i;
#undef overlaps
#undef encompasses
#undef runcallback
#undef pushsubhit
}




    /* find options and callback in pos 2,3 
       find callback script first since a script is also an object      */
#define get_func_opt    do{\
 if(! (duk_is_ecmascript_function(ctx,2) && (func_idx=2)) )\
      if(duk_is_ecmascript_function(ctx,3))\
        func_idx=3;\
    if( func_idx != 3 && duk_is_object(ctx,3))\
        opt_idx=3;\
    else if(func_idx != 2 && duk_is_object(ctx,2))\
        opt_idx=2; }while(0)





static duk_ret_t 
rex_re2(duk_context *ctx, TXrexSyntax type)
{
    byte *str, *end;
    duk_size_t sz;
    duk_idx_t opt_idx=-1, func_idx=-1; //-1 == not found

    get_func_opt;
    /* get string/buffer to be searched */
    if(duk_is_string(ctx,1))
        str=(byte*)duk_get_lstring(ctx,1,&sz);
    else if (duk_is_buffer_data(ctx,1))
        str=(byte*)duk_get_buffer_data(ctx,1,&sz);
    else
    {
        duk_push_string(ctx,"rex: item to be matched (arg 2), must be a string or buffer");
        duk_throw(ctx);
    }
    end = str+sz;
    
    rex(ctx,str,end,opt_idx,func_idx,type,0);
    return 1;
}

/* from mmapi/freadex.c - altered for duktape */

static int                                                   /* n bytes read */
rpfreadex(ctx,fh,buf,len,ex)
duk_context *ctx;
FHTYPE fh;
byte *buf;
int  len;
FFS  *ex;
{
 register int nread,nactr;  /* val returned, number actually returned */
 byte *loc;
 static char Fn[]="freadex";

 errno = 0;
 nactr=fread((char *)buf,sizeof(byte),len,fh);           /* do a read */
 if(nactr<0)
    {
     duk_push_sprintf(ctx,"Can't read file: %s", strerror(errno));
     duk_throw(ctx);
    }
 nread=nactr;                                          /* end of buffer */
 if(nread && nread==len)               /* read ok && as big as possible */
    {
     loc=getrex(ex,buf,buf+nread,BSEARCHNEWBUF);
     if(loc==BPNULL)                   /* no expression within buffer */
        {
         putmsg(MWARN,Fn,"no end delimiter located within buffer");
         return(nread);
        }
     else
     if(loc==buf)                      /* MAW 10-17-95 - handle w/all */
        {
         loc=buf+nread;
        }
     nread=(int)(loc-buf)+rexsize(ex);            /* just beyond expr */
     errno = 0;
     if (FSEEKO(fh, (off_t)(nread - nactr), SEEK_CUR) == -1)
        {
         putmsg(MWARN+FSE,Fn,"Can't seek to realign buffer: %s",
                strerror(errno));
         return(0);
        }
     return(nread);
    }
 return(nactr);                   /* read was smaller || eof || error */
}

/* from mmapi/rex.c */
static const byte def_eexp[2]={ '$' ,'\0' };

static duk_ret_t 
rex_re2_file(duk_context *ctx, TXrexSyntax type)
{
    byte *end;
    size_t strsz=128*1024;
    byte str[strsz];
    const char *fname;
    int nread;
    FFS *endex;
    FILE *ipfh;
    int i=0;
    duk_idx_t opt_idx=-1, func_idx=-1; //-1 == not found
    
    get_func_opt;
    /* get endex/delimiter option */
    if( opt_idx>0)
    {
        if (duk_get_prop_string(ctx,opt_idx,"delimiter") )
        {
            const char *s=duk_require_string(ctx,-1); 
            endex=openrex((byte *)s, TXrexSyntax_Rex);
        }
        else
        {
            endex=openrex((byte *)def_eexp, TXrexSyntax_Rex);
        }
        duk_pop(ctx);
    }
    else
    {	
        endex=openrex((byte *)def_eexp, TXrexSyntax_Rex);   
    }

    /* get string/buffer to be searched */
    if(duk_is_string(ctx,1))
        fname=duk_get_string(ctx,1);
    else
    {
        duk_push_string(ctx,"rexfile: item to be matched (arg 2), must be a string (filename)");
        duk_throw(ctx);
    }

    ipfh=fopen((char *)fname,"r");
    if(ipfh==(FILE *)NULL)
    {
        duk_push_sprintf(ctx,"rexfile: error opening file '%s'",fname);
        duk_throw(ctx);
    }

    while((nread=rpfreadex(ctx,ipfh,str,strsz,endex))>0)
    {
         end=str+nread;
         i=rex(ctx,str,end,opt_idx,func_idx,type,i);
    }
    closerex(endex);

    return 1;
}
/* duktape rex functions */

duk_ret_t
RPdbFunc_rex(duk_context *ctx)
{
    return rex_re2(ctx,TXrexSyntax_Rex);
}

duk_ret_t
RPdbFunc_re2(duk_context *ctx)
{
//    printenum(ctx,0);
//    return 0;
    return rex_re2(ctx,TXrexSyntax_Re2);
}


duk_ret_t
RPdbFunc_rexfile(duk_context *ctx)
{
    return rex_re2_file(ctx,TXrexSyntax_Rex);
}

duk_ret_t
RPdbFunc_re2file(duk_context *ctx)
{
    return rex_re2_file(ctx,TXrexSyntax_Re2);
}

duk_ret_t
RPsqlFunc_sandr(duk_context *ctx)
{
    sandr(ctx, 0);
    return 1;
}

duk_ret_t
RPsqlFunc_sandr2(duk_context *ctx)
{
    sandr(ctx, 1);
    return 1;
}