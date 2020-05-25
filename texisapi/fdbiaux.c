/* This gets included into fdbi.c twice, once for the normal version
 * of functions and once for the ..._trace (debug) versions, with
 * FUNC() and FDBI_TRACE defined accordingly.
 */


static int FUNC(fdbix_decode) ARGS((FDBIX *fx));
static int FUNC(
fdbix_decode)(fx)
FDBIX   *fx;
/* Decodes VSL loc data from immediately previous call to fdbix_getnext().
 * Returns 0 on error.  Sets remaining FDBIHI fields.
 */
{
  static CONST char     fn[] = "fdbix_decode";
  FDBIHI                *hi;

  if (!(fx->flags & FDF_FULL)) goto done;
  hi = &fx->hi;
  if (hi->hitsz < sizeof(dword)*hi->locsz &&
      !fdbi_allocbuf(fn, (void **)(char *)&hi->hits, &hi->hitsz,
                     sizeof(dword)*hi->locsz))
    return(0);
  hi->nhits = vsltolocs(hi->locdata, hi->locsz, hi->hits);
  hi->curHit = 0;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 11)
    {
      char      *locs, *d, *e;
      size_t    i, n;

      n = hi->nhits*(EPI_HUGEINT_BITS/3 + 5);
      if ((locs = (char *)TXmalloc(TXPMBUFPN, fn, n)) != CHARPN)
        {
          for (i = 0, d = locs, e = d + n; i < hi->nhits && d < e; i++)
            d += htsnpf(d, e - d, " %wd", (EPI_HUGEINT)hi->hits[i]);
        }
      putmsg(MINFO, CHARPN, "   fdbix_decode(%s): %wd locs:%s",
             fx->wd, (EPI_HUGEINT)hi->nhits, (locs != CHARPN ? locs : ""));
      locs = TXfree(locs);
    }
#endif /* FDBI_TRACE */
done:
  return(1);
}

static int FUNC(fdbix_decode7) ARGS((FDBIX *fx));
static int FUNC(
fdbix_decode7)(fx)
FDBIX   *fx;
/* Identical to fdbix_decode(), but for 7-bit VSHs.
 */
{
  static CONST char     fn[] = "fdbix_decode7";
  FDBIHI                *hi;

  if (!(fx->flags & FDF_FULL)) goto done;
  hi = &fx->hi;
  if (hi->hitsz < sizeof(dword)*hi->locsz &&
      !fdbi_allocbuf(fn, (void **)(char *)&hi->hits, &hi->hitsz,
                     sizeof(dword)*hi->locsz))
    return(0);
  hi->nhits = vsh7tolocs(hi->locdata, hi->locsz, hi->hits);
  hi->curHit = 0;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 11)
    {
      char      *locs, *d, *e;
      size_t    i, n;

      n = hi->nhits*(EPI_HUGEINT_BITS/3 + 5);
      if ((locs = (char *)TXmalloc(TXPMBUFPN, fn, n)) != CHARPN)
        {
          for (i = 0, d = locs, e = d + n; i < hi->nhits && d < e; i++)
            d += htsnpf(d, e - d, " %wd", (EPI_HUGEINT)hi->hits[i]);
        }
      putmsg(MINFO, CHARPN, "   fdbix_decode(%s): %wd locs:%s",
             fx->wd, (EPI_HUGEINT)hi->nhits, (locs != CHARPN ? locs : ""));
      locs = TXfree(locs);
    }
#endif /* FDBI_TRACE */
done:
  return(1);
}

static FDBIHI *FUNC(fdbix_getnextsingle) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnextsingle)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Gets next recid >= `loc', for single-recid word.
 */
{
#ifdef FDBI_TRACE
  static CONST char     fn[] = "fdbix_getnextsingle";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
  fx->hi.loc = fx->curtbloff;               	/* was tmp */
  fx->hi.nhits = (size_t)fx->buf;           	/* buf was tmp nhits */
  fx->hi.curHit = 0;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc) ||
      !TXrecidvalid2(&fx->curtbloff))       	/* was called again */
    goto eof;
  TXsetrecid(&fx->curtbloff, (EPI_OFF_T)(-1));  	/* flag so we don't re-use */
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx", fn, fx->wd,
	   (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc));
#endif /* FDBI_TRACE */
  return(&fx->hi);

eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnextmultipred1buf) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnextmultipred1buf)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Same as fdbix_getnextmultipred(), when we know we've got the whole block
 * in the buffer and no additional reads are needed.
 */
{
  static CONST char     fn[] = "fdbix_getnextmultipred1buf";
  byte                  *s, *e;
  EPI_HUGEUINT          u, r;
#ifdef FDBI_TRACE
  char                  *xtra = "";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
nextrecid:
  u = (EPI_HUGEUINT)0;
  s = fx->buf;
  e = fx->buf + fx->bufsz;
  /* Used to check if s >= e (eob) here, but now we roll that into the
   * s >= e check after INVSH to save time.  We can get away with it
   * because the buffer has VSH_MAXLEN extra slack bytes.
   */
  INVSH(s, r, r = 0; if (s < e) goto badvsh);   /* get token */
  if (s >= e)                                   /* hit eob */
    {
      if (fx->buf >= e) goto eof;               /* eob before INVSH => eof */
      if (s > e || (fx->flags & FDF_FULL)) goto trunc;
    }
  if (fx->flags & FDF_FULL)
    {
      INVSH(s, u, goto badvsh);                 /* size of loc data */
      if (s > e) u = (EPI_HUGEUINT)1;               /* u is corrupt */
      if (s + (size_t)u > e) goto trunc;
      fx->hi.locdata = s;
      fx->hi.locsz = (size_t)u;
    }
  fx->buf = s + (size_t)u;                      /* advance `buf' */
  fx->bufsz = e - fx->buf;
  TXsetrecid(&fx->curtbloff, TXgetoff2(&fx->curtbloff) + (EPI_OFF_T)r);
  fx->hi.loc = fx->curtbloff;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc)) goto nextrecid;

#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx%s", fn, fx->wd,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc), xtra);
#endif /* FDBI_TRACE */
  /* decodefunc set at open */
  fx->hi.curHit = (size_t)(-1);
  return(&fx->hi);

badvsh:
  putmsg(MERR, fn, InvalidVSH);
  goto err;
trunc:
  fdbi_badstuck(fn, fx);
err:
  fx->flags |= FDF_ERROR;
eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE%s",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc), xtra);
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnextmultipred) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnextmultipred)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Gets next recid >= `loc' and its location data for current word,
 * for multi-recid words, after fdbix_seek() called, after
 * fdbix_getnextmultifirst() called for first location.  Returns
 * pointer to hit data, NULL if end of word's data or error (check
 * fdbix_iserror()).  Pointer is valid until next FDBIX call.  Note:
 * only fx->hi.loc (and locdata/locsz if FDF_FULL) valid until
 * fdbix_decode() called, immediately after this.
 */
{
  static CONST char     fn[] = "fdbix_getnextmultipred";
  byte                  *s, *e;
  EPI_HUGEUINT          u, r;
  int                   stuck = 0, res;
  EPI_OFF_T             pfiloff;
#ifdef FDBI_TRACE
  char                  *xtra = "";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
nextrecid:
  u = (EPI_HUGEUINT)0;
  s = fx->buf;
  e = fx->buf + fx->bufsz;
  /* Used to check if s >= e (eob) here, but now we roll that into the
   * s >= e check after INVSH to save time.  We can get away with it
   * because the buffer has VSH_MAXLEN extra slack bytes.
   */
  INVSH(s, r, r = 0; if (s < e) goto badvsh);   /* get token */
  if (s >= e)                                   /* hit eob */
    {
      if (s > e || (fx->flags & FDF_FULL) ||
          fx->buf >= e)                         /* eob before INVSH */
	{
	doread:
	  if (stuck >= 64)                      /* don't loop infinitely */
	    {
	      fdbi_badstuck(fn, fx);
	      goto err;
	    }
	  pfiloff = fx->filoff;
#ifdef FDBI_TRACE
	  xtra = " (fdbix_readnextbuf)";
#endif /* FDBI_TRACE */
	  if ((res = fdbix_readnextbuf(fx, FDBI_MINDATLEN + (size_t)u)) <= 0)
	    {
	      if (res == 0) goto eof;           /* end of all data for word */
	      goto err;                         /* read error */
	    }
	  if (fx->filoff <= pfiloff) stuck++;   /* no forward progress */
	  if (fx->totrd >= fx->totsz)           /* have all remaining data */
#ifdef FDBI_TRACE
	    fx->getnext = fdbix_getnextmultipred1buf_trace;
#else /* !FDBI_TRACE */
	    fx->getnext = fdbix_getnextmultipred1buf;
#endif /* !FDBI_TRACE */
	  goto nextrecid;                       /* fx->buf may have changed */
	}
    }
  if (fx->flags & FDF_FULL)
    {
      INVSH(s, u, goto badvsh);                 /* size of loc data */
      if (s > e) u = (EPI_HUGEUINT)1;               /* u is corrupt */
      if (s + (size_t)u > e) goto doread;
      fx->hi.locdata = s;
      fx->hi.locsz = (size_t)u;
    }
  fx->buf = s + (size_t)u;                      /* advance `buf' */
  fx->bufsz = e - fx->buf;
  TXsetrecid(&fx->curtbloff, TXgetoff2(&fx->curtbloff) + (EPI_OFF_T)r);
  fx->hi.loc = fx->curtbloff;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc)) goto nextrecid;

#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx%s", fn, fx->wd,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc), xtra);
#endif /* FDBI_TRACE */
  /* decodefunc set at open */
  fx->hi.curHit = (size_t)(-1);
  return(&fx->hi);

badvsh:
  putmsg(MERR, fn, InvalidVSH);
err:
  fx->flags |= FDF_ERROR;
eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE%s",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc), xtra);
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnextmultifirst) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnextmultifirst)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Like fdbix_getnextmultipred(), but for first call.
 */
{
#ifdef FDBI_TRACE
  static CONST char     fn[] = "fdbix_getnextmultifirst";

  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx)",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
  fx->flags &= ~FDF_ERROR;
  if (fx->totsz == 0 && !fdbix_readbuf(fx)) goto err;  	/* first call */
  if (fx->totrd >= fx->totsz)                           /* we have all data */
#ifdef FDBI_TRACE
    fx->getnext = fdbix_getnextmultipred1buf_trace;
  else
    fx->getnext = fdbix_getnextmultipred_trace;
#else /* !FDBI_TRACE */
    fx->getnext = fdbix_getnextmultipred1buf;
  else
    fx->getnext = fdbix_getnextmultipred;
#endif /* !FDBI_TRACE */
  return(fx->getnext(fx, loc));

err:
  fx->flags |= FDF_ERROR;
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnext7single) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnext7single)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Identical to fdbix_getnextsingle(), but for 7-bit VSHs.
 */
{
#ifdef FDBI_TRACE
  static CONST char     fn[] = "fdbix_getnext7single";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
  fx->hi.loc = fx->curtbloff;               	/* was tmp */
  fx->hi.nhits = (size_t)fx->buf;           	/* buf was tmp nhits */
  fx->hi.curHit = 0;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc) ||
      !TXrecidvalid2(&fx->curtbloff))       	/* was called again */
    goto eof;
  TXsetrecid(&fx->curtbloff, (EPI_OFF_T)(-1));  	/* flag so we don't re-use */
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx", fn, fx->wd,
	   (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc));
#endif /* FDBI_TRACE */
  return(&fx->hi);

eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnext7multipred1buf) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnext7multipred1buf)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Identical to fdbix_getnextmultipred1buf(), but for 7-bit VSHs.
 */
{
  static CONST char     fn[] = "fdbix_getnext7multipred1buf";
  byte                  *s, *e, *nt;
  EPI_HUGEUINT          r;
  size_t                sz;
#ifdef FDBI_TRACE
  char                  *xtra = "";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
nextrecid:
  sz = 0;
  s = fx->buf;
  e = fx->buf + fx->bufsz;
  /* Used to check if s >= e (eob) here, but now we roll that into the
   * s >= e check after INVSH to save time.  We can get away with it
   * because the buffer has VSH_MAXLEN extra slack bytes.
   */
  INVSH7_HICLR(s, r, r = 0; if (s < e) goto badvsh);    /* get token */
  if (s >= e)                                   /* hit eob */
    {
      if (fx->buf >= e) goto eof;               /* eob before INVSH => eof */
      if (s > e || (fx->flags & FDF_FULL)) goto trunc;
    }
  nt = s;
  if (fx->flags & FDF_FULL)
    {
      for ( ; nt < e && (*nt & FDBI_TOKEN_FLAG) == 0; nt++);
      sz = nt - s;                              /* size of loc data */
      fx->hi.locdata = s;
      fx->hi.locsz = sz;
    }
  fx->buf = nt;                                 /* advance `buf' */
  fx->bufsz = e - fx->buf;
  TXsetrecid(&fx->curtbloff, TXgetoff2(&fx->curtbloff) + (EPI_OFF_T)r);
  fx->hi.loc = fx->curtbloff;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc)) goto nextrecid;

#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx%s", fn, fx->wd,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc), xtra);
#endif /* FDBI_TRACE */
  /* decodefunc set at open */
  fx->hi.curHit = (size_t)(-1);
  return(&fx->hi);

badvsh:
  putmsg(MERR, fn, InvalidVSH7);
  goto err;
trunc:
  fdbi_badstuck(fn, fx);
err:
  fx->flags |= FDF_ERROR;
eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE%s",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc), xtra);
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnext7multipred) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnext7multipred)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Identical to fdbix_getnextmultipred(), but for 7-bit VSHs.
 */
{
  static CONST char     fn[] = "fdbix_getnext7multipred";
  byte                  *s, *e, *nt;
  EPI_HUGEUINT          r;
  int                   stuck = 0, res;
  EPI_OFF_T             pfiloff;
  size_t                sz;
#ifdef FDBI_TRACE
  char                  *xtra = "";
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
nextrecid:
  sz = 0;
  s = fx->buf;
  e = fx->buf + fx->bufsz;
  /* Used to check if s >= e (eob) here, but now we roll that into the
   * s >= e check after INVSH to save time.  We can get away with it
   * because the buffer has VSH_MAXLEN extra slack bytes.
   */
  INVSH7_HICLR(s, r, r = 0; if (s < e) goto badvsh);    /* get token */
  if (s >= e)                                   /* hit eob */
    {
      if (s > e || (fx->flags & FDF_FULL) ||
          fx->buf >= e)                         /* eob before INVSH */
	{
	doread:
	  if (stuck >= 64)                      /* don't loop infinitely */
	    {
	      fdbi_badstuck(fn, fx);
	      goto err;
	    }
	  pfiloff = fx->filoff;
#ifdef FDBI_TRACE
	  xtra = " (fdbix_readnextbuf)";
#endif /* FDBI_TRACE */
	  if ((res = fdbix_readnextbuf(fx, FDBI_MINDATLEN + sz)) <= 0)
	    {
	      if (res == 0) goto eof;           /* end of all data for word */
	      goto err;                         /* read error */
	    }
	  if (fx->filoff <= pfiloff) stuck++;   /* no forward progress */
	  if (fx->totrd >= fx->totsz)           /* have all remaining data */
#ifdef FDBI_TRACE
	    fx->getnext = fdbix_getnext7multipred1buf_trace;
#else /* !FDBI_TRACE */
	    fx->getnext = fdbix_getnext7multipred1buf;
#endif /* !FDBI_TRACE */
	  goto nextrecid;                       /* fx->buf may have changed */
	}
    }
  nt = s;
  if (fx->flags & FDF_FULL)
    {
      for ( ; nt < e && (*nt & FDBI_TOKEN_FLAG) == 0; nt++);
      if (nt >= e && fx->totrd < fx->totsz)
        {
          sz += (sz >> 1) + 16;                 /* guess an increasing size */
          goto doread;
        }
      sz = nt - s;                              /* size of loc data */
      fx->hi.locdata = s;
      fx->hi.locsz = sz;
    }
  fx->buf = nt;                                 /* advance `buf' */
  fx->bufsz = e - fx->buf;
  TXsetrecid(&fx->curtbloff, TXgetoff2(&fx->curtbloff) + (EPI_OFF_T)r);
  fx->hi.loc = fx->curtbloff;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc)) goto nextrecid;

#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): 0x%wx%s", fn, fx->wd,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fx->hi.loc), xtra);
#endif /* FDBI_TRACE */
  /* decodefunc set at open */
  fx->hi.curHit = (size_t)(-1);
  return(&fx->hi);

badvsh:
  putmsg(MERR, fn, InvalidVSH7);
err:
  fx->flags |= FDF_ERROR;
eof:
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx): NONE%s",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc), xtra);
#endif /* FDBI_TRACE */
  return(FDBIHIPN);
}

static FDBIHI *FUNC(fdbix_getnext7multifirst) ARGS((FDBIX *fx, RECID loc));
static FDBIHI *FUNC(
fdbix_getnext7multifirst)(fx, loc)
FDBIX   *fx;
RECID   loc;
/* Identical to fdbix_getnextmultifirst(), but for 7-bit VSHs.
 */
{
#ifdef FDBI_TRACE
  static CONST char     fn[] = "fdbix_getnext7multifirst";

  if (FdbiTraceIdx >= 10)
    putmsg(MINFO, CHARPN, "   %s(%s, 0x%wx)",
           fn, fx->wd, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */

  fx->flags &= ~FDF_ERROR;
  if (fx->totsz == 0 && !fdbix_readbuf(fx)) goto err;   /* first call */

  /* lookahead doesn't work because tokens are relative.  were they not,
   * add (seekloc - curloc)*fx->lkfactor to current buffer pointer in
   * getnext to guess next recid location, then seek to FDBI_TOKEN_FLAG,
   * then seek forward/back linearly to seekloc.  KNG 000419
   */

  if (fx->totrd >= fx->totsz)                           /* we have all data */
#ifdef FDBI_TRACE
    fx->getnext = fdbix_getnext7multipred1buf_trace;
  else
    fx->getnext = fdbix_getnext7multipred_trace;
#else /* !FDBI_TRACE */
    fx->getnext = fdbix_getnext7multipred1buf;
  else
    fx->getnext = fdbix_getnext7multipred;
#endif /* !FDBI_TRACE */
  return(fx->getnext(fx, loc));

err:
  fx->flags |= FDF_ERROR;
  RESETFDBIHI(&fx->hi);
  fx->getnext = fdbix_getnexteof;
  return(FDBIHIPN);
}

/* --------------------------------- FDBIW --------------------------------- */

static int FUNC(fdbiw_getnextone) ARGS((FDBIW *fw, RECID loc));
static int FUNC(
fdbiw_getnextone)(fw, loc)
FDBIW   *fw;
RECID   loc;
/* Get next word location >= `loc', for single-index-item word.
 * Returns 0 on error/no more, 1 if ok.  Sets fw->hip.  See also
 * fdbis_getnextone_skip().
 */
{
  FDBIHI        *hi;
  FDBIX         *fx;

  fx = fw->fxp[0];
  if ((hi = fx->getnext(fx, loc)) == FDBIHIPN)
    {
      RESETFDBIHI(&fw->hi);                     /* just in case */
      fw->hip = FDBIHIPN;
#ifdef FDBI_TRACE
      if (FdbiTraceIdx >= 9)
        putmsg(MINFO, CHARPN, "  fdbiw_getnextone(%s, 0x%wx): NONE",
               fw->wd, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
      return(0);
    }
  fw->hip = hi;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 9)
    putmsg(MINFO, CHARPN, "  fdbiw_getnextone(%s, 0x%wx): 0x%wx", fw->wd,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&hi->loc));
#endif /* FDBI_TRACE */
  return(1);
}

static int FUNC(fdbiw_getnextmulti) ARGS((FDBIW *fw, RECID loc));
static int FUNC(
fdbiw_getnextmulti)(fw, loc)
FDBIW   *fw;
RECID   loc;
/* Get next word location >= `loc', for multiple-index-item word.
 * Returns 0 on error/no more, 1 if ok.  Sets fw->hip.
 */
{
  int   i, n, ret;
  FDBIX *fx;
  FHEAP *fh;

  RESETFDBIHI(&fw->hi);
  fh = fw->fh;
nextrecid:                                      /* WTF same code as fdbi_get */
  for (i = 0; i < fw->numcur; i++)              /* get next on current recid */
    {
      fx = fw->fxcur[i];
      if (fx->getnext(fx, loc) != FDBIHIPN)
        {
          if (!fheap_insert(fh, fx)) goto err;
        }
    }
  if (fheap_num(fh) == 0) goto err;             /* no more words */
  n = 0;
  do                                            /* pull off all sets == top */
    {
      fw->fxcur[n++] = fx = (FDBIX *)fheap_top(fh);
      fheap_deletetop(fh);
    }
  while (fheap_num(fh) > 0 &&
         TXgetoff2(&((FDBIX*)fheap_top(fh))->hi.loc)==TXgetoff2(&fx->hi.loc));
  fw->numcur = n;
  if (TXgetoff2(&fx->hi.loc) < TXgetoff2(&loc)) goto nextrecid;

  if (n == 1 ||                                 /* one word or */
      !(fw->flags & FDF_FULL))                  /*   no position info */
    {
      fw->hip = &fx->hi;                        /* just propagate up */
    }
  else                                          /* merge 'em later */
    {
      fw->hip = &fw->hi;
      fw->hi.loc = fx->hi.loc;
      fw->hi.curHit = (size_t)(-1);
      fw->hi.decodefunc = (FDBIHICB *)fdbiw_decodemerge;
      fw->hi.decodeusr = fw;
    }
  ret = 1;
  goto done;

err:
  ret = 0;
done:
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 9)
    {
      if (ret)
        putmsg(MINFO, CHARPN, "  fdbiw_getnextmulti(%s, 0x%wx): 0x%wx",
               fw->wd, (EPI_HUGEINT)TXgetoff2(&loc),
               (EPI_HUGEINT)TXgetoff2(&fw->hip->loc));
      else
        putmsg(MINFO, CHARPN, "  fdbiw_getnextmulti(%s, 0x%wx): NONE",
               fw->wd, (EPI_HUGEINT)TXgetoff2(&loc));
    }
#endif /* FDBI_TRACE */
  return(ret);
}

/* --------------------------------- FDBIF --------------------------------- */

static int FUNC(fdbif_getnextone) ARGS((FDBIF *ff, RECID loc));
static int FUNC(
fdbif_getnextone)(ff, loc)
FDBIF   *ff;
RECID   loc;
/* Get next recid location >= `loc' for phrase, for one-word phrase.
 * Returns 0 on error/no more, 1 if ok.  Sets ff->hip.
 */
{
  FDBIW *fw;

  fw = ff->fwp[0];
  if (!fw->getnext(fw, loc))
    {
      RESETFDBIHI(&ff->hi);
      ff->hip = FDBIHIPN;
#ifdef FDBI_TRACE
      if (FdbiTraceIdx >= 8)
        putmsg(MINFO, CHARPN, " fdbif_getnextone(%s, 0x%wx): NONE",
               ff->phrase, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
      return(0);
    }
  ff->hip = fw->hip;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 8)
    putmsg(MINFO, CHARPN, " fdbif_getnextone(%s, 0x%wx): 0x%wx", ff->phrase,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&ff->hip->loc));
#endif /* FDBI_TRACE */
  return(1);
}

static int FUNC(fdbif_getnextmulti) ARGS((FDBIF *ff, RECID loc));
static int FUNC(
fdbif_getnextmulti)(ff, loc)
FDBIF   *ff;
RECID   loc;
/* Get next recid location >= `loc' for phrase, for multi-word phrase.
 * Returns 0 on error/no more, 1 if ok.  Sets ff->hip.
 */
{
  static CONST char     fn[] = "fdbif_getnextmulti";
  int                   i, maxi, ret, got;
  FDBIW                 *fw;
  dword                 maxhit, proper;
  FDBIHI                *hi, *merge, *h;
  size_t                req;
#ifdef FDBI_TRACE
  RECID                 oloc = loc;
#endif /* FDBI_TRACE */

  merge = &ff->hi;
  RESETFDBIHI(merge);

nextrecid:
  i = got = 0;
  while (got < ff->nwords)                      /* next same recid for all */
    {
      fw = ff->fwp[i];
      if (!fw->getnext(fw, loc))                /* 1 missing: must be done */
        goto err;
      if (TXgetoff2(&fw->hip->loc) != TXgetoff2(&loc))
        {                                       /* different (later) token */
          loc = fw->hip->loc;
          got = 0;
        }
      got++;
      if (++i >= ff->nwords) i = 0;             /* next word */
    }
  merge->loc = loc;
  if ((ff->flags & (FDF_FULL | FDF_IGNWDPOS)) != FDF_FULL)
    {
      /* Either we have no word-position info to merge (!FDF_FULL),
       * or API3EXACTPHRASEIGNOREWORDPOSITION is set (FDF_IGNWDPOS).
       * Skip word-position decode and merge:
       */
      ff->hip = ff->fwp[0]->hip;                /* just propagate recid up */
      goto ok;                                  /* must use Metamorph later */
    }
  ff->hip = merge;

  /* We can always *advance* a phrase word to its next location to try
   * to make a phrase match, but we cannot *back up* a word to try to
   * match, because we are already at the starting locations.  So find
   * the phrase word whose starting loc (`maxhit') occurs *last*, since
   * that will be the earliest possible phrase match:
   */
  maxhit = 0;
  maxi = 0;
  for (i = 0; i < ff->nwords; i++)
    {
      hi = ff->fwp[i]->hip;
      if (hi->decodefunc != FDBIHICBPN &&       /* decode if needed */
          !hi->decodefunc(hi->decodeusr))
        goto err;
      hi->curHit = 0;                           /* reset for below */
      if (hi->hits[hi->curHit] > maxhit)
        {
          maxhit = hi->hits[hi->curHit];
          maxi = i;
        }
    }

  /* Adjust `maxhit' so it is the loc of the *first* word in the phrase
   * (if that word actually has a loc there, which we will verify below):
   */
  maxhit -= (dword)ff->reloff[maxi];

  for (;;)                                      /* get every in-seq. merge */
    {
      for (i = 0; i < ff->nwords; i++)          /* get word hits in sequence */
        {
          /* `proper' is where we expect the current phrase word to be.
           * wtf may initially be negative if corrupt index?
           */
          proper = maxhit + (dword)ff->reloff[i];
          hi = ff->fwp[i]->hip;                 /* v-WTF use binary search? */
          while (hi->hits[hi->curHit] < proper) /* seek this word to seq. */
            {
              if (++hi->curHit >= hi->nhits)
                {                               /* end of word this recid */
                  if (merge->nhits) goto ok;
                  else goto nextrecid;          /* 0 merges: try next recid */
                }
            }
          if (hi->hits[hi->curHit] > proper)    /* overshot; restart */
            {
              maxhit = hi->hits[hi->curHit] - (dword)ff->reloff[i];
              i = -1;
            }
        }
      req = (merge->nhits + 1)*sizeof(dword);   /* finally got hit; add it */
      if (merge->hitsz < req &&
          !fdbi_allocbuf(fn, (void **)(char *)&merge->hits, &merge->hitsz,
                         req))
        {
          merge->nhits = 0;
          goto err;
        }
      if (merge->hitLensSz < req &&
          !fdbi_allocbuf(fn, (void **)(char *)&merge->hitLens,
                         &merge->hitLensSz, req))
        {
          merge->nhits = 0;
          goto err;
        }
      /* KNG 990108 account for possible noise words before/after phrase: */
      h = ff->fwp[0]->hip;
      merge->hits[merge->nhits] = h->hits[h->curHit] - ff->reloff[0];
      /* KNG 20100119 Bug 2972:  Also save phrase length (in `hitLens'),
       * for later TXfdbiIsWithinN...() usage:
       */
      merge->hitLens[merge->nhits] = ff->phraseLen;
      merge->nhits++;
      h = ff->fwp[ff->nwords - 1]->hip;     /* last phrase word's hit info */
      maxhit = h->hits[h->curHit] + ff->nindexable -
        ff->reloff[ff->nwords - 1];
    }

ok:
  merge->curHit = 0;
  ret = 1;
  goto done;

err:
  TXsetrecid(&ff->hi.loc, (EPI_OFF_T)(-1));
  ff->hip = FDBIHIPN;
  ret = 0;
done:
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 8)
    {
      if (ret)
        putmsg(MINFO, CHARPN, " fdbif_getnextmulti(%s, 0x%wx): 0x%wx",
               ff->phrase, (EPI_HUGEINT)TXgetoff2(&oloc),
               (EPI_HUGEINT)TXgetoff2(&merge->loc));
      else
        putmsg(MINFO, CHARPN, " fdbif_getnextmulti(%s, 0x%wx): NONE",
               ff->phrase, (EPI_HUGEINT)TXgetoff2(&oloc));
    }
#endif /* FDBI_TRACE */
  return(ret);
}

/* --------------------------------- FDBIS --------------------------------- */

static int FUNC(fdbis_getnextzero) ARGS((FDBIS *fs, RECID loc));
static int FUNC(
fdbis_getnextzero)(fs, loc)
FDBIS   *fs;
RECID   loc;
/* Gets next record's hit data, for zero-phrase (empty) sets.
 */
{
  RESETFDBIHI(&fs->hi);
  fs->hip = FDBIHIPN;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, CHARPN, "fdbis_getnextzero(%s, 0x%wx): NONE",
           fs->setname, (EPI_HUGEINT)TXgetoff2(&loc));
#else /* !FDBI_TRACE */
  (void)loc;
#endif /* !FDBI_TRACE */
  return(0);
}

static int FUNC(fdbis_getnextone) ARGS((FDBIS *fs, RECID loc));
static int FUNC(
fdbis_getnextone)(fs, loc)
FDBIS   *fs;
RECID   loc;
/* Gets next record >= `loc' and its hit data, for one-phrase sets.
 * Returns 0 on error/no more, 1 if ok.  Sets fs->hip and its `set'
 * ptr.
 */
{
  FDBIF *ff;

  ff = fs->ffp[0];
  if (!ff->getnext(ff, loc))
    {
      RESETFDBIHI(&fs->hi);
      fs->hip = FDBIHIPN;
#ifdef FDBI_TRACE
      if (FdbiTraceIdx >= 7)
        putmsg(MINFO, CHARPN, "fdbis_getnextone(%s, 0x%wx): NONE",
               fs->setname, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
      return(0);
    }
  fs->hip = ff->hip;                            /* just propagate up */
  fs->hip->set = fs->set;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, CHARPN, "fdbis_getnextone(%s, 0x%wx): 0x%wx", fs->setname,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&fs->hip->loc));
#endif /* FDBI_TRACE */
  return(1);
}

static int FUNC(fdbis_getnextone_skip) ARGS((FDBIS *fs, RECID loc));
static int FUNC(
fdbis_getnextone_skip)(fs, loc)
FDBIS   *fs;
RECID   loc;
/* Gets next record >= `loc' and its hit data, for one-phrase sets,
 * when we also know the phrase is one word and one index item; this
 * is essentially fdbiw_getnextone().  Returns 0 on error/no more, 1
 * if ok.  Sets fs->hip and its `set' ptr.
 */
{
  FDBIX         *fx;
  FDBIHI        *hi;

  fx = fs->sfx;
  if ((hi = fx->getnext(fx, loc)) == FDBIHIPN)
    {
      RESETFDBIHI(&fs->hi);                     /* just in case */
      fs->hip = FDBIHIPN;
#ifdef FDBI_TRACE
      if (FdbiTraceIdx >= 7)
        putmsg(MINFO, CHARPN, "fdbis_getnextone_skip(%s, 0x%wx): NONE",
               fs->setname, (EPI_HUGEINT)TXgetoff2(&loc));
#endif /* FDBI_TRACE */
      return(0);
    }
  fs->hip = hi;                                 /* just propagate up */
  hi->set = fs->set;
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 7)
    putmsg(MINFO, CHARPN, "fdbis_getnextone_skip(%s, 0x%wx): 0x%wx",
           fs->setname,
           (EPI_HUGEINT)TXgetoff2(&loc), (EPI_HUGEINT)TXgetoff2(&hi->loc));
#endif /* FDBI_TRACE */
  return(1);
}

static int FUNC(fdbis_getnextmulti) ARGS((FDBIS *fs, RECID loc));
static int FUNC(
fdbis_getnextmulti)(fs, loc)
FDBIS   *fs;
RECID   loc;
/* Gets next record >= `loc' and its hit data, for multi-phrase sets.
 * Returns 0 on error/no more, 1 if ok.  Sets fs->hi and its `set'
 * ptr.
 */
{
  int           i, n, ret;
  FDBIF         *ff;
  FHEAP         *fh;

  RESETFDBIHI(&fs->hi);
  fh = fs->fh;
nextrecid:                                      /* WTF same code as fdbi_get */
  for (i = 0; i < fs->numcur; i++)              /* get next on current recid */
    {
      ff = fs->ffcur[i];
      if (ff->getnext(ff, loc))
        {
          if (!fheap_insert(fh, ff)) goto err;
        }
    }
  if (fheap_num(fh) == 0) goto err;             /* no more words */
  n = 0;
  do                                            /* pull off all sets == top */
    {
      fs->ffcur[n++] = ff = (FDBIF *)fheap_top(fh);
      fheap_deletetop(fh);
    }
  while (fheap_num(fh) > 0 && TXgetoff2(&((FDBIF *)fheap_top(fh))->hip->loc)
         == TXgetoff2(&ff->hip->loc));
  fs->numcur = n;
  if (TXgetoff2(&ff->hip->loc) < TXgetoff2(&loc)) goto nextrecid;

  if (n == 1 ||                                 /* one word or */
      !(fs->flags & FDF_FULL))                  /*   no position info */
    {
      fs->hip = ff->hip;                        /* just propagate up */
    }
  else                                          /* merge 'em later */
    {
      fs->hip = &fs->hi;
      fs->hi.loc = ff->hip->loc;
      fs->hi.curHit = (size_t)(-1);
      fs->hi.decodefunc = (FDBIHICB *)fdbis_decodemerge;
      fs->hi.decodeusr = fs;
    }
  fs->hip->set = fs->set;
  ret = 1;
  goto done;

err:
  TXsetrecid(&fs->hi.loc, (EPI_OFF_T)(-1));
  fs->hip = FDBIHIPN;
  ret = 0;
done:
#ifdef FDBI_TRACE
  if (FdbiTraceIdx >= 7)
    {
      if (ret)
        putmsg(MINFO, CHARPN, "fdbis_getnextmulti(%s, 0x%wx): 0x%wx",
               fs->setname, (EPI_HUGEINT)TXgetoff2(&loc),
               (EPI_HUGEINT)TXgetoff2(&fs->hip->loc));
      else
        putmsg(MINFO, CHARPN, "fdbis_getnextmulti(%s, 0x%wx): NONE",
               fs->setname, (EPI_HUGEINT)TXgetoff2(&loc));
    }
#endif /* FDBI_TRACE */
  return(ret);
}
