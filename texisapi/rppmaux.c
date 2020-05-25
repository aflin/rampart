/* Code that is #included twice in rppm.c, with and without RPPM_TRACE
 * defined.  FUNC(x) evals to `x_trace' in the former and `x' in the latter.
 */

int FUNC(
rppm_rankcur)(RPPM      *rp,            /* (in) RPPM object */
              FDBIHI    **hits,         /* (in) current hits to rank */
              int       num,            /* (in) number of `hits' */
              size_t    *byteMedian)    /* (out) median byte offset of hits */
/* Kai (Chi) Probability Function.  Returns a rank for current
 * location of `num' `hits' based on several factors.  Assumes hits
 * are sorted by increasing offset, all hits present at least once.
 * Higher rank values are better.  NOTE: see also rppm_rankbest_one().
 * Sets `*byteMedian' to median (center) of `hits', as a byte (not word)
 * offset; usually a guess as `hits' are indexer-word based.
 */
{
#ifdef RPPM_TRACE
  static CONST char     fn[] = "rppm_rankcur_trace";
#endif /* RPPM_TRACE */
  int                   x, setrank, rank, med, i;
  FDBIHI                **ep, **hp, *h, **r;
  RVAL                  *rvs, *rv, *rve;
  int                   raw[RVAR_KNOBNUM];
  int                   *rw;
  unsigned              mask, ex;
#ifdef RPPM_TRACE
  int                   sw, orderOk = 1;
  char                  *t, *e, tmp[512];
#endif /* RPPM_TRACE */

  r = hits + (num >> 1);                        /* compute median */
  if (num & 1)                                  /* odd #sets: use center set*/
    {
      /* `(num >> 1)' is a fudge factor to account for "fenceposts" later: */
      med = (int)(*r)->hits[(*r)->curHit] - (num >> 1);
      if (byteMedian != SIZE_TPN)
        *byteMedian = ((*r)->byteHits != SIZE_TPN ?
          ((*r)->byteHits[(*r)->curHit] + (*r)->byteHitEnds[(*r)->curHit])/2
                       : (size_t)6*(med + (num >> 1)));
    }
  else                                          /* even # of sets: average */
    {                                           /*   the center two sets */
      med = (int)((r[-1]->hits[r[-1]->curHit] + (*r)->hits[(*r)->curHit] + 1)
                  >> 1) - (num >> 1);
      /* `byteMedian' is used by abstract() to center its locus, rather than
       * here to compute rank.  Thus it is better there (and safe here) to
       * pick one of the center two sets, rather than their average,
       * so that two sets that are farther apart than the abstract size
       * do not result in an abstract that contains neither (for
       * TXABS_STYLE_QUERYMEDIAN):
       */
      if (byteMedian != SIZE_TPN)
        *byteMedian = ((*r)->byteHits != SIZE_TPN ?
                       r[-1]->byteHits[r[-1]->curHit] :
                       6*r[-1]->hits[r[-1]->curHit]);
    }

  ep = hits + num;
  rank = 0;
  rvs = rp->vals;
  rve = rvs + RVAR_DOCFREQ;                     /* later knobs pre-computed */

#ifdef RPPM_TRACE
  t = tmp;
  e = tmp + sizeof(tmp) - 1;
  sw = 1;
  for (i = 0; i < num; i++)
    {
      if (t < e)
        t += htsnpf(t, e - t, " %s@%d", hits[i]->set->term,
                    (int)hits[i]->hits[hits[i]->curHit]);
      if (t < e)
        {
          if (hits[i]->byteHits != SIZE_TPN)
            t += htsnpf(t, e - t, ".%d",
                        (int)hits[i]->byteHits[hits[i]->curHit]);
          else
            t += htsnpf(t, e - t, ".?");
        }
      if ((x = strlen(hits[i]->set->term)) > sw) sw = x;
      /* Sanity check: Bug 2012: was being passed hits out of order: */
      if (i > 0 &&
          (hits[i]->hits[hits[i]->curHit] <     /* word pos. out of order*/
           hits[i-1]->hits[hits[i-1]->curHit] ||
           (hits[i]->byteHits != SIZE_TPN &&
            hits[i]->byteHits[hits[i]->curHit] </* byte pos. out of order */
            hits[i-1]->byteHits[hits[i-1]->curHit])))
        orderOk = 0;
    }
  if (t > e)
  {
    char * TruncationPoint = tmp + sizeof(tmp) - 4;
    strcpy(TruncationPoint, "...");
  }
  if (TXtraceRppm & 0x8)
    putmsg(MINFO, CHARPN, "Ranking recid %08wx with median@%d.%d%s:%s",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid),
           med, (byteMedian != SIZE_TPN ? (int)*byteMedian : -1),
           (hits[0]->byteHits != SIZE_TPN ? "" : "?"), tmp);
  if ((TXtraceRppm & 0x1) && !orderOk)
    putmsg(MERR, fn, "Internal error: Set hits not in ascending order");
#  define RI(X)                                                 \
  ((((((X)/rp->sumknobgain)*h->set->gain)/rp->sumpossetgain)*   \
    RPPM_BEST_RANK) >> (EXPBITS + MANBITS))
#endif /* RPPM_TRACE */

  raw[RVAR_LEADBIAS] = med;
  for (hp = hits; hp < ep; hp++, med++)
    {
#ifdef RPPM_TRACE
      t = tmp;
#endif /* RPPM_TRACE */
      h = *hp;
      raw[RVAR_PROXIMITY] = (med - (int)h->hits[h->curHit]);
      if (raw[RVAR_PROXIMITY] < 0) raw[RVAR_PROXIMITY] = -raw[RVAR_PROXIMITY];
      /* raw[RVAR_LEADBIAS] is constant for this loop */
      raw[RVAR_ORDER] = (hp - hits) - h->set->order;
      if (raw[RVAR_ORDER] < 0) raw[RVAR_ORDER] = -raw[RVAR_ORDER];
      raw[RVAR_DOCFREQ] = h->nhits;
      /* raw[RVAR_TBLFREQ] = h->set->rawtblfreq; */
      setrank = 0;
      for (rv = rvs, rw = raw; rv < rve; rv++)
        {
          x = *(rw++);
          KLOG(x, mask, ex);
          setrank += (i = (MAX_KLOG + 1 - x)*rv->gain);
#ifdef RPPM_TRACE
          if (t < e)
            t += htsnpf(t, e - t, " %.4s: %2d/%-2d",
                        TXrppmValsName[rv - rvs] + 5, RI(i), rw[-1]);
#endif /* RPPM_TRACE */
        }
      x = raw[RVAR_DOCFREQ];
      KLOG(x, mask, ex);
      setrank += x*rv->gain;
#ifdef RPPM_TRACE
      if (t < e)
        t += htsnpf(t, e - t, " doc: %2d/%-2d", RI(x*rv->gain),
                    raw[RVAR_DOCFREQ]);
#endif /* RPPM_TRACE */
      setrank += h->set->cookedtblfreq;
#ifdef RPPM_TRACE
      if (t < e)
        t += htsnpf(t, e - t, " tbl: %2d", RI(h->set->cookedtblfreq));
#endif /* RPPM_TRACE */
      rank += i = (setrank/rp->sumknobgain)*h->set->gain;
#ifdef RPPM_TRACE
      if (t > e)
      {
        char * TruncationPoint = tmp + sizeof(tmp) - 4;
        strcpy(TruncationPoint, "...");
      }
      if (TXtraceRppm & 0x10)
        putmsg(MINFO, CHARPN, "  %-*s: %3d =%s",
               sw, h->set->term,
               (((i/rp->sumpossetgain)*RPPM_BEST_RANK) >> (EXPBITS+MANBITS)),
               tmp);
#endif /* RPPM_TRACE */
    }
  i = rank;
  rank = (((rank/rp->sumpossetgain)*RPPM_BEST_RANK) >> (EXPBITS + MANBITS));
#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x4)
    putmsg(MINFO, CHARPN, "  rank = %d / %dk", rank, i/1000);
#endif /* RPPM_TRACE */
  /* Johannes sez rank of 1 may be bad later, as it can get stored
   * as -1 and be misinterpreted as EOF:    KNG 980605
   */
  if (rank == 1) rank = 2;
  else if (rank == -1) rank = -2;
  return(rank);
#undef RI
}

int FUNC(
rppm_rankbest)(RPPM     *rp,            /* (in) RPPM object */
               FDBIHI   **hits,         /* (in/out) hit info arrays */
               int      num,            /* (in) # of `hits' */
               size_t   *byteMedian)    /* (out) median byte offset of best */
/* Ranks a document, given `*num' `hits' of hit info.  Returns best rank.
 * `hits' array is modified.  Assumes `hits' is in ascending order by
 * initial hit.  If RPF_RANKTBLFREQ is set, ranks only based on table
 * frequency.  See also rppm_rankbest_one().
 * Sets `*byteMedian' to best rank's byte median (see rppm_rankcur()),
 * or -1 if unknown.  If RPF_SAVEBESTHITS, returns each `hits[]->curHit'
 * set to best hits also.
 */
{
  static CONST char     fn[] = "rppm_rankbest"
#ifdef RPPM_TRACE
    "_trace"
#endif /* RPPM_TRACE */
    ;
  int                   bestrank, rank, s, nlset, got, i;
  size_t                curByteMedian, *curByteMedianP = &curByteMedian;
  FDBIHI                *h, **hs, **hd, **he;
  dword                 newHitWordOffset;
  FHEAP                 *fh;
#define FIXEDBESTNUM    64
  size_t                fixedBestCurHits[FIXEDBESTNUM];
  size_t                *bestCurHits = SIZE_TPN;
  FDBIHI                *fixedBestHis[FIXEDBESTNUM], **bestHis = FDBIHIPPN;
#ifdef RPPM_TRACE
  char                  *t, *e, tmp[1024];
#endif /* RPPM_TRACE */

  if (byteMedian != SIZE_TPN) *byteMedian = (size_t)(-1);
#ifndef RPPM_TRACE
  else curByteMedianP = SIZE_TPN;
#endif /* !RPPPM_TRACE */

#ifdef RPPM_TRACE
  /* Bug 3905 fallout; sanity checks: was not fixing up FDBI[SFWX].hi.set
   * also, when init'ing RPPM.hits[].set in openrppm().
   */
  if (TXtraceRppm)
    {
      for (s = 0; s < num; s++)
        if (hits[s]->set < rp->sets || hits[s]->set >= rp->sets + rp->numsets)
          putmsg(MERR, fn,
           "Internal error: hits[%d].set out of bounds (%d; expected 0 - %d)",
                 s, (int)(hits[s]->set - rp->sets), (int)rp->numsets - 1);
    }
#endif /* RPPM_TRACE */

  if (rp->flags & RPF_RANKTBLFREQ)              /* eg. liker */
    {
      bestrank = 0;
#ifdef RPPM_TRACE
      t = tmp;
      e = tmp + sizeof(tmp) - 1;
#endif /* RPPM_TRACE */
      for (s = 0; s < num; s++)
        {
          bestrank += hits[s]->set->likerwt;
#ifdef RPPM_TRACE
          if (t < e)
            t += htsnpf(t, e - t, " + %s %d", hits[s]->set->term,
                        hits[s]->set->likerwt);
#endif /* RPPM_TRACE */
        }
      /* current hits already set for RPF_SAVEBESTHITS */
#ifdef RPPM_TRACE
      if (t > e)
      {
        char *TruncationPoint = tmp + sizeof(tmp) - 4;
        strcpy(TruncationPoint, "...");
      }
      if (TXtraceRppm & 0x2)
        putmsg(MINFO, CHARPN, "Final LIKER best rank for recid %08wx: %d = %s",
               (EPI_HUGEINT)TXgetoff2(&rp->curRecid), bestrank, tmp + 3);
#endif /* RPPM_TRACE */
      goto done;
    }

  /* this logic also in fdbi_get(): */
  if (rp->flags & RPF_LOGICHECK)
    {
      if (rp->allpos)                           /* no NOTs, all sets req'd */
        {
          if (num < rp->numsets) goto err;
        }
      else                                      /* NOTs, or optional sets */
        {
          if (num < rp->minsets) goto err;
          nlset = got = 0;
          for (i = 0; i < num; i++)             /* check logic */
            {
              switch (hits[i]->set->logic)
                {
                case LOGISET:  nlset++;  break;
                case LOGIAND:  got++;    break;
                case LOGINOT:  goto err;        /* no NOTs allowed */
                case LOGIPROP: goto err;        /* should not happen; */
                default:       goto err;        /*   excluded at open */
                }
            }
          if (got != rp->nlogiand || nlset < rp->nreqlogiset)
            goto err;                           /* missing needed sets */
        }
    }

  if (rp->flags & RPF_SAVEBESTHITS)             /* alloc `bestCurHits' etc. */
    {
      if (num <= FIXEDBESTNUM)
        {
          bestCurHits = fixedBestCurHits;
          bestHis = fixedBestHis;
          memset(bestCurHits, 0, num*sizeof(size_t));
          memset(bestHis, 0, num*sizeof(FDBIHI *));
        }
      else if ((bestCurHits=(size_t*)calloc(num,sizeof(size_t)))== SIZE_TPN ||
               (bestHis = (FDBIHI**)calloc(num,sizeof(FDBIHI*))) == FDBIHIPPN)
        {
          TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, num, sizeof(size_t));
          goto err;
        }
    }

  bestrank = -MAXINT;                           /* ie. real bad */

  /* We do not want to take the time to try all combinations of set hits,
   * so work similar to XPM: look at a sliding window of `num' adjacent hits.
   * We keep the sets in a heap, pulling off the next hit to the right
   * (from whatever set) and updating the window array which is passed
   * to rppm_rankcur().  There is a bit of trickery with the `curHit'
   * field: the window array has the current hit for a set, while the
   * heap must look at the next hit.  But there's only one `curHit'
   * index, so the heap looks at `curHit[1]' (see rppm_rankbest_heapcmp()).
   * KNG 990312
   */
  he = hits + num;
  fh = rp->fh;
  fheap_clear(fh);
  fheap_setcmp(fh, rppm_rankbest_heapcmp);
  for (hs = hits; hs < he; hs++)                /* 2nd+ hits go into heap */
    if ((*hs)->nhits > 1 && !fheap_insert(fh, *hs)) goto err;
  for (;;)
    {
      rank = FUNC(rppm_rankcur)(rp, hits, num, curByteMedianP);
      if (rank > bestrank)                      /* it's a new best rank */
        {
#ifdef RPPM_TRACE
          if (TXtraceRppm & 0x4)
            putmsg(MINFO, CHARPN, "  New best rank for doc is %d", rank);
#endif /* RPPM_TRACE */
          bestrank = rank;
          if (byteMedian != SIZE_TPN) *byteMedian = curByteMedian;
          if (rp->flags & RPF_SAVEBESTHITS)     /* save `hits[]->curHit' */
            {
              for (s = 0; s < num; s++)
                {
                  bestCurHits[s] = hits[s]->curHit;
                  bestHis[s] = hits[s];
                }
            }
          if (bestrank >= RPPM_BEST_RANK) break;
        }
      /* Get the next available hit (to the right) from the heap,
       * and remove it.  Could be from any set that still has more hits:
       */
      if (fheap_num(fh) == 0) break;            /* no more hits */
      h = (FDBIHI *)fheap_top(fh);              /* get the next hit */
      fheap_deletetop(fh);
      /* Put set back in the heap if still more hits on that set: */
      if (++h->curHit + 1 < h->nhits &&
          !fheap_insert(fh, h))                 /* more hits: back in heap */
        goto err;
      /* Now re-order the window so all sets are still in ascending order.
       * We just need to move the set we popped off into its proper place.
       * We know the new hit is >= the previous hit from that set,
       * since each set's hits are in ascending order.  So the old location
       * "gap" in the window will be <= the new location insertion point,
       * and we can just slide the window left until reaching the right spot.
       * KNG 20080109 Bug 2012: used to just put the set back at window end,
       * which could result in out-of-order hits in the window, which
       * rppm_rankcur() does not expect and thus could produce bad ranks:
       */
      for (hs = hits; hs < he && *hs != h; hs++);   /* find old loc. gap */
      newHitWordOffset = h->hits[h->curHit];
      for (hd = hs++;                           /* slide window left over it*/
           hs < he && (*hs)->hits[(*hs)->curHit] < newHitWordOffset;
           )
        *(hd++) = *(hs++);
      *hd = h;                                  /* store in new location */
    }

#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x2)
    putmsg(MINFO, CHARPN, "Final best rank for recid %08wx is %d",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid), bestrank);
#endif /* RPPM_TRACE */
  if (bestrank == -MAXINT)                      /* no hits at all */
    bestrank = 0;
  else
    {
      if (bestrank < -RPPM_BEST_RANK)          /* keep it in bounds */
        bestrank = -RPPM_BEST_RANK;
      if (rp->flags & RPF_SAVEBESTHITS)
        {
          for (s = 0; s < num; s++)
            {
              hits[s] = bestHis[s];
              hits[s]->curHit = bestCurHits[s];
            }
        }
    }
  goto done;

err:
  bestrank = 0;
#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x2)
    putmsg(MINFO, CHARPN, "Final best rank for recid %08wx is 0 (err)",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid));
#endif /* RPPM_TRACE */
done:
  if (bestCurHits != SIZE_TPN && bestCurHits != fixedBestCurHits)
    free(bestCurHits);
  if (bestHis != FDBIHIPPN && bestHis != fixedBestHis)
    free(bestHis);
  if ((TXtraceMetamorph & TX_TMF_OverallHit) && rp->mm)
    {
      MM3S      *ms = rp->mm->mme;
      char      contextBuf[256];

      TXmmSetupHitContext(ms, contextBuf, sizeof(contextBuf));
      if (bestrank > 0)
        putmsg(MINFO, __FUNCTION__,
               "Rank %d hit for MMAPI object %p for recid 0x%wx: `%s'",
               bestrank, rp->mm, (EPI_HUGEINT)TXgetoff2(&rp->curRecid),
               contextBuf);
      else
        putmsg(MINFO, __FUNCTION__,
               "No hit for MMAPI object %p for recid 0x%wx: `%s'", rp->mm,
               (EPI_HUGEINT)TXgetoff2(&rp->curRecid));
    }
  TXsetrecid(&rp->curRecid, 0x0);               /* caller will re-set */
  return(bestrank);                             /* WTF rank signed? */
}

int FUNC(
rppm_rankbest_one)(rp, hit, byteMedian)
RPPM    *rp;
FDBIHI  *hit;
size_t  *byteMedian;
/* Identical to rppm_rankbest(), but for single set `hit'.
 */
{
  int           rank, nlset, got, x, setrank, i;
  RVAL          *rv, *rve;
  int           raw[RVAR_KNOBNUM];
  int           *rw;
  unsigned      mask, ex;
#ifdef RPPM_TRACE
  char          *t, *e, tmp[512];
#endif /* RPPM_TRACE */

  if (rp->flags & RPF_RANKTBLFREQ)              /* eg. liker */
    {
      rank = hit->set->likerwt;
#ifdef RPPM_TRACE
      if (TXtraceRppm & 0x2)
        putmsg(MINFO, CHARPN,
               "Final LIKER best rank for recid %08wx: %d = %s %d",
               (EPI_HUGEINT)TXgetoff2(&rp->curRecid), rank,
               hit->set->term, hit->set->likerwt);
#endif /* RPPM_TRACE */
      goto done;
    }

  /* this logic also in fdbi_get(): */
  if (rp->flags & RPF_LOGICHECK)
    {
      if (rp->allpos)                           /* no NOTs, all sets req'd */
        {
          if (1 < rp->numsets) goto err;
        }
      else                                      /* NOTs, or optional sets */
        {
          if (1 < rp->minsets) goto err;
          nlset = got = 0;
          switch (hit->set->logic)
            {
            case LOGISET:  nlset++;  break;
            case LOGIAND:  got++;    break;
            case LOGINOT:  goto err;            /* no NOTs allowed */
            case LOGIPROP: goto err;            /* should not happen; */
            default:       goto err;            /*   excluded at open */
            }
          if (got != rp->nlogiand || nlset < rp->nreqlogiset)
            goto err;                           /* missing needed sets */
        }
    }

  /* Rank it.  See also rppm_rankcur(); adapted from it.  We just need
   * to rank the first hit, because for a one-set hit list all the rank
   * knobs are constant, except for leadbias, which will be greatest
   * at the first hit.  (If negative rank gains are ever allowed, we'll
   * have to check and use the _last_ hit.)
   */
  if (byteMedian != SIZE_TPN)
    *byteMedian = (hit->byteHits != SIZE_TPN ?
         (hit->byteHits[hit->curHit] + hit->byteHitEnds[hit->curHit]) / 2 :
                   (size_t)6*(int)hit->hits[hit->curHit]);
#ifdef RPPM_TRACE
  if (hit->byteHits != SIZE_TPN)
    htsnpf(tmp, sizeof(tmp), ".%d", (int)hit->byteHits[hit->curHit]);
  else
    htsnpf(tmp, sizeof(tmp), ".?");
  if (TXtraceRppm & 0x8)
    putmsg(MINFO, CHARPN, "Ranking recid %08wx with median@%d.%d%s: %s@%d%s",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid), (int)hit->hits[hit->curHit],
           (byteMedian != SIZE_TPN ? (int)*byteMedian : -1),
           (hit->byteHits != SIZE_TPN ? "" : "?"),
           hit->set->term, (int)hit->hits[hit->curHit], tmp);
#endif /* RPPM_TRACE */
  raw[RVAR_PROXIMITY] = 0;
  raw[RVAR_LEADBIAS] = (int)hit->hits[hit->curHit];     /* always median? */
  raw[RVAR_ORDER] = hit->set->order;
  raw[RVAR_DOCFREQ] = hit->nhits;
  /* raw[RVAR_TBLFREQ] = hit->set->rawtblfreq; */
  setrank = 0;
#ifdef RPPM_TRACE
  t = tmp;
  e = tmp + sizeof(tmp) - 1;
#  define RI(X)                                                 \
  ((((((X)/rp->sumknobgain)*hit->set->gain)/rp->sumpossetgain)* \
    RPPM_BEST_RANK) >> (EXPBITS + MANBITS))
#endif /* RPPM_TRACE */
  for (rv = rp->vals, rve = rv + RVAR_DOCFREQ, rw = raw; rv < rve; rv++)
    {
      x = *(rw++);
      KLOG(x, mask, ex);
      setrank += (i = MAX_KLOG + 1 - x)*rv->gain;
#ifdef RPPM_TRACE
      if (t < e)
        htsnpf(t, e - t, " %.4s: %2d/%-2d",
               TXrppmValsName[rv - rp->vals] + 5, RI(i), rw[-1]);
#endif /* RPPM_TRACE */
    }
  x = raw[RVAR_DOCFREQ];
  KLOG(x, mask, ex);
  setrank += x*rv->gain;
#ifdef RPPM_TRACE
  if (t < e)
    t += htsnpf(t, e - t, " doc: %2d/%-2d", RI(x*rv->gain),
                raw[RVAR_DOCFREQ]);
#endif /* RPPM_TRACE */
  setrank += hit->set->cookedtblfreq;
#ifdef RPPM_TRACE
  if (t < e)
    t += htsnpf(t, e - t, " tbl: %2d", RI(hit->set->cookedtblfreq));
#endif /* RPPM_TRACE */
  rank = i = (setrank/rp->sumknobgain)*hit->set->gain;
#ifdef RPPM_TRACE
  if (t > e)
  {
    char *TruncationPoint = tmp + sizeof(tmp) - 4;
    strcpy(TruncationPoint, "...");
  }
  if (TXtraceRppm & 0x10)
    putmsg(MINFO, CHARPN, "  %s: %3d =%s",
           hit->set->term,
           (((i/rp->sumpossetgain)*RPPM_BEST_RANK) >> (EXPBITS+MANBITS)),
           tmp);
#endif /* RPPM_TRACE */
  i = rank;
  rank = (((rank/rp->sumpossetgain)*RPPM_BEST_RANK) >> (EXPBITS + MANBITS));
#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x4)
    putmsg(MINFO, CHARPN, "  rank = %d / %dk", rank, i/1000);
#endif /* RPPM_TRACE */
  /* Johannes sez rank of 1 may be bad later, as it can get stored
   * as -1 and be misinterpreted as EOF:    KNG 980605
   */
  if (rank == 1) rank = 2;
  else if (rank == -1) rank = -2;
  else if (rank < -RPPM_BEST_RANK)              /* keep it in bounds */
    rank = -RPPM_BEST_RANK;
#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x2)
    putmsg(MINFO, CHARPN, "Final best rank for recid %08wx is %d",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid), rank);
#endif /* RPPM_TRACE */
  goto done;

err:
  rank = 0;
#ifdef RPPM_TRACE
  if (TXtraceRppm & 0x2)
    putmsg(MINFO, CHARPN, "Final best rank for recid %08wx is 0 (err)",
           (EPI_HUGEINT)TXgetoff2(&rp->curRecid));
#endif /* RPPM_TRACE */
done:
  /* No need to check RPF_SAVEBESTHITS; we left `hit' pointing to best */
  if ((TXtraceMetamorph & TX_TMF_OverallHit) && rp->mm)
    {
      MM3S      *ms = rp->mm->mme;
      char      contextBuf[256];

      TXmmSetupHitContext(ms, contextBuf, sizeof(contextBuf));
      if (rank > 0)
        putmsg(MINFO, __FUNCTION__,
               "Rank %d hit for MMAPI object %p for recid 0x%wx: `%s'",
               rank, rp->mm, (EPI_HUGEINT)TXgetoff2(&rp->curRecid),
               contextBuf);
      else
        putmsg(MINFO, __FUNCTION__,
               "No hit for MMAPI object %p for recid 0x%wx: `%s'", rp->mm,
               (EPI_HUGEINT)TXgetoff2(&rp->curRecid), contextBuf);
    }
  return(rank);                                 /* WTF rank signed? */
}
