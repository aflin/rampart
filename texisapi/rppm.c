#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#undef EPI_OS_INT_BITS
#undef EPI_OS_OFF_T_BITS
#include "sizes.h"
#include "api3.h"
#include "texint.h"
#include "heap.h"
#include "fdbi.h"
#include "rppm.h"
#include "cgi.h"                                /* for htsnpf() */


#ifndef SELPN
#  define SELPN        ((SEL *)NULL)
#endif


CONST char * CONST      TXrppmValsName[RVAR_NUM] =
{
  "likepproximity",
  "likepleadbias",
  "likeporder",
  "likepdocfreq",
  "likeptblfreq",
  "likepallmatch",
  "likepinfthresh",
  "likepindexthresh",
};
RVAL    RppmValsCur[RVAR_NUM] =
{
  { 512 },
  { 512 },
  { 512 },
  { 512 },
  { 512 },
  {   0 },
  {   0 },
  {   0 },
};
RVAL    RppmValsDef[RVAR_NUM] =
{
  { 512 },
  { 512 },
  { 512 },
  { 512 },
  { 512 },
  {   0 },
  {   0 },
  {   0 },
};
#define RPPM_DOCFREQ_MAX        100     /* wtf arbitrary */

/* TXtraceRppm bits:
 * 0x0001:  sanity/error checks
 * 0x0002:  best rank for document
 * 0x0004:  rank at each window
 * 0x0008:  word.byte positions at each window being ranked
 * 0x0010:  rank knob info for each set at each window
 * 0x0020:  save byte-offset info (may alter behavior)
 */
int     TXtraceRppm = 0;

/* ------------------------------------------------------------------------- */

#define EXPBITS 4               /* # bits in "exponent" of log, <= 5 */
#define MANBITS 8               /* # bits in "mantissa" (eg. precision) */

#define MAX_KLOG        ((1 << (EXPBITS + MANBITS)) - 1)
#define MASK_SHIFT      (1 << (5 - EXPBITS))
#define LLIM            (MANBITS/MASK_SHIFT - 1)

static unsigned klog ARGS((unsigned x));
static unsigned
klog(x)
unsigned        x;
/* Returns a quick approximation of log(1+x), linearly scaled such
 * that klog(1 << 32) = (1 << (EXPBITS + MANBITS)).
 */
{
  unsigned      mask, e;

  for (mask = (~(unsigned)0 << MASK_SHIFT), e = 0; mask != 0; mask <<= MASK_SHIFT, e++)
    {
      if ((x & mask) == 0) break;
    }
  return((e << MANBITS) | (e < LLIM ?
           (x << ((LLIM - e)*MASK_SHIFT)) : (x >> (e - LLIM)*MASK_SHIFT)));
}

#define KLOG(x, mask, ex)                                       \
  if (((x) & (~(unsigned)0 << MASK_SHIFT)) == 0)                          \
    (x) <<= LLIM*MASK_SHIFT;                                    \
  else if (((x) & (~(unsigned)0 << 2*MASK_SHIFT)) == 0)                   \
    (x) = (1 << MANBITS) | ((x) << ((LLIM - 1)*MASK_SHIFT));    \
  else if (((x) & (~(unsigned)0 << 3*MASK_SHIFT)) == 0)                   \
    (x) = (2 << MANBITS) | ((x) << ((LLIM - 2)*MASK_SHIFT));    \
  else                                                          \
    {                                                           \
      for (mask = (~(unsigned)0 << (LLIM + 1)*MASK_SHIFT), ex = LLIM;     \
           mask != 0;                                           \
           mask <<= MASK_SHIFT, ex++)                           \
        {                                                       \
          if (((x) & mask) == 0) break;                         \
        }                                                       \
      (x) = (ex << MANBITS) | ((x) >> (ex - LLIM)*MASK_SHIFT);  \
    }

/* ------------------------------------------------------------------------- */

int
rppm_setgain(var, val)
CONST char      *var;
int             val;
/* Sets global RPPM gain setting `var' to `val'.  These values take
 * effect in future openrppm() calls.  Returns 0 on error.
 * NOTE: if negative gains are ever allowed, see also rppm_rankbest_one().
 */
{
  int   i;

  for (i = 0; i < RVAR_NUM; i++)
    {
      if (strcmpi(TXrppmValsName[i], var) != 0) continue;
      if (val < 0) val = 0;
      else if (val > RPPM_MAX_WT && i < RVAR_KNOBNUM) val = RPPM_MAX_WT;
      RppmValsCur[i].gain = (i < RVAR_KNOBNUM) ? (val*1023)/RPPM_MAX_WT : val;
      return(1);
    }
  return(0);
}

void
rppm_resetvals()
/* Resets RPPM knobs.
 */
{
  memcpy(RppmValsCur, RppmValsDef, RVAR_NUM*sizeof(RppmValsCur[0]));
}

static void rppm_precomp ARGS((RPPM *rp));
static void
rppm_precomp(rp)
RPPM    *rp;
/* Pre-computes factors for Kai Probability Function.  Called when
 * set weights or RVAR vals change (eg. rppm_setwts()).
 */
{
  static CONST char     fn[] = "rppm_precomp";
  int                   i, n, allmatch, maxlikerwt, thresh;
  unsigned              logtotrecs;
  RVAL                  *rv;
  RPPM_SET              *set;
  long                  sumtblfreq, totRecs;
  unsigned              tf;

  rp->sumknobgain = 0;
  for (i = 0; i < RVAR_KNOBNUM; i++)
    {
      rv = rp->vals + i;
      rp->sumknobgain += rv->gain;
    }
  if (rp->sumknobgain == 0)
    {
      putmsg(MWARN + UGE, fn, "LIKEP knob gains must sum to non-zero value");
      rp->sumknobgain = 1;
    }

  /* Bug 3741: make sure `totRecs' is at least as large as largest
   * set `tblfreq' for sanity: we expect log(totRecs) - log(tblfreq) >= 0,
   * otherwise set `likerwt' values may go negative:
   */
  totRecs = rp->totrecs;
  for (i = 0; i < rp->numsets; i++)
    {
      set = rp->sets + i;
      if (totRecs < set->tblfreq) totRecs = set->tblfreq;
    }

  sumtblfreq = 0L;
  logtotrecs = klog((unsigned)totRecs + rp->numsets);
  rp->nlogiand = rp->nlogiset = rp->nloginot = 0;
  rp->sumpossetgain = n = 0;
  for (i = 0; i < rp->numsets; i++)
    {
      set = rp->sets + i;
      /* Use a minimum of 1 for tblfreq, so unindexed sets don't get
       * a likerwt of 0:
       */
      tf = (unsigned)set->tblfreq + 1;
      set->cookedtblfreq = (MAX_KLOG+1 - klog(tf))*rp->vals[RVAR_TBLFREQ].gain;
      switch (set->logic)
        {
        case LOGIAND:  rp->nlogiand += 1;  break;
        case LOGISET:  rp->nlogiset += 1;  break;
        case LOGINOT:  rp->nloginot += 1;  continue;    /* never counted */
        case LOGIPROP:  /* fall through: should have been excluded @ open */
        default:                           continue;    /* should not happen*/
        }
      set->order = n++;
      if (set->gain > 0)                                /* WTF if neg.? */
        rp->sumpossetgain += set->gain;
      sumtblfreq += (long)(logtotrecs - klog(tf));
    }
  rp->numnnot = n;
  if (rp->sumpossetgain == 0)
    {
      putmsg(MWARN + UGE, fn, "LIKEP set gains must sum to non-zero value");
      rp->sumpossetgain = 1;
    }

  /* Compute LIKER weights, as fraction of RPPM_MAX_WT such that sum
   * of non-NOT sets is RPPM_MAX_WT:
   */
  maxlikerwt = 0;
  for (n = i = 0; i < rp->numsets; i++)
    {
      set = rp->sets + i;
      if (set->logic == LOGINOT) continue;
      tf = (unsigned)set->tblfreq + 1;
      if (sumtblfreq == 0L)
        set->likerwt = RPPM_MAX_WT;                     /* arbitrary */
      else
        {
          set->likerwt = (RPPM_MAX_WT*(logtotrecs - klog(tf)))/sumtblfreq;
          /* Bug 3741: sanity check: */
          if (set->likerwt > RPPM_MAX_WT) set->likerwt = RPPM_MAX_WT;
        }
      n += set->likerwt;
      if (set->likerwt > maxlikerwt && set->tblfreq > 0)
        maxlikerwt = set->likerwt;
    }
  if (n != RPPM_MAX_WT)                                 /* be neat: sum 1000 */
    {
      for (i = 0; i < rp->numsets; i++)
        {
          set = rp->sets + i;
          if (set->logic != LOGINOT)
            {
              set->likerwt += RPPM_MAX_WT - n;
              if (set->likerwt > maxlikerwt && set->tblfreq > 0)
                maxlikerwt = set->likerwt;
              break;
            }
        }
    }
  if (TXlikermaxrows > 0)                               /* WTF make RVAR? */
    {
      if ((long)TXlikermaxrows < totRecs)
        thresh = (int)((long)RPPM_MAX_WT*(totRecs - (long)TXlikermaxrows)/
                       totRecs);
      else
        thresh = 0;
      if (maxlikerwt > thresh)
        maxlikerwt = (thresh > 0) ? thresh : 0;
    }
  rp->likerthresh = maxlikerwt;                         /* WTF use this */

  /* KNG 991025 `intersects' could be > nlogisets, if nlogisets == 0: */
  if (rp->intersects > rp->nlogiset) rp->intersects = rp->nlogiset;
  if (rp->qintersects > rp->nlogiset) rp->qintersects = rp->nlogiset;
  /* minsets/intersects logic should be centralized in get3eqsapi() now,
   * for consistency amongst fdbi_get(), get3eqsapi(), rppm_precomp():
   */
  if (rp->fop == FOP_MMIN)
    {
      rp->minsets = 0;
      allmatch = 0;
    }
  else
    {
      rp->minsets = rp->nlogiand + rp->intersects;
      allmatch = (rp->minsets == rp->nlogiand + rp->nlogiset);
    }
  rp->allpos = (allmatch && rp->nloginot == 0);
  rp->nreqlogiset = (allmatch ? rp->nlogiset : 0);
}

int
rppm_setgainlocal(rp, var, val)
RPPM    *rp;
RVAR    var;
int     val;
{
  if ((int)var < 0 || var >= RVAR_NUM)
    return(0);
  if (val < 0) val = 0;
  else if (val > RPPM_MAX_WT && var < RVAR_KNOBNUM) val = RPPM_MAX_WT;
  rp->vals[var].gain = (var < RVAR_KNOBNUM) ? (val*1023)/RPPM_MAX_WT : val;
  rppm_precomp(rp);
  return(1);
}

static int
txRppmLinearSearchHeapCmp(void *a, void *b, void *usr)
/* Heap-comparison function for txRppmLinearSearch(); used to order
 * overlapping hits.
 */
{
  (void)usr;
  return((int)((FDBIHI *)a)->hits[((FDBIHI *)a)->curHit] -
         (int)((FDBIHI *)b)->hits[((FDBIHI *)b)->curHit]);
}

static int
txRppmLinearSearchIdxHeapCmp(void *a,   /* (in) FDBIHI *obj */
                             void *b,   /* (in) FDBIHI *obj */
                             void *usr) /* (in) user data (ignored) */
/* Heap-comparison function for txRppmLinearSearchIdx().
 * Used to order linear pattern matcher set hits, as well as match up
 * set hits with index expression hits.
 */
{
  FDBIHI        *hiA = (FDBIHI *)a;
  FDBIHI        *hiB = (FDBIHI *)b;

  (void)usr;
  /* Sort by earliest byte offset first, as per getrlex()/wtix_insert(): */
  if (hiA->byteHits[hiA->curHit] < hiB->byteHits[hiB->curHit])
    return(-1);
  if (hiA->byteHits[hiA->curHit] > hiB->byteHits[hiB->curHit])
    return(1);

  /* Sort by longest byte length second, as per getrlex()/wtix_insert(): */
  if (hiA->byteHitEnds[hiA->curHit] > hiB->byteHitEnds[hiB->curHit])
    return(-1);
  if (hiA->byteHitEnds[hiA->curHit] < hiB->byteHitEnds[hiB->curHit])
    return(1);
  return(0);
}

static int rppm_rankbest_heapcmp ARGS((void *a, void *b, void *usr));
static int
rppm_rankbest_heapcmp(a, b, usr)
void    *a, *b, *usr;
/* Heap-comparison function for rppm_rankbest().  Compares the next
 * hit in each set, because the current hit is off the heap and being
 * ranked in the window.
 */
{
  (void)usr;
  return((int)((FDBIHI *)a)->hits[((FDBIHI *)a)->curHit + 1] -
         (int)((FDBIHI *)b)->hits[((FDBIHI *)b)->curHit + 1]);
}

static int CDECL rppm_hitcmp ARGS((CONST void *a, CONST void *b));
static int CDECL
rppm_hitcmp(a, b)
CONST void      *a, *b;
/* Sort callback.
 */
{
  int   av, bv;

  av = TXrecidvalid(&(*(FDBIHI **)a)->loc) ?
    (int)(*(FDBIHI **)a)->hits[(*(FDBIHI **)a)->curHit] : -1;
  bv = TXrecidvalid(&(*(FDBIHI **)b)->loc) ?
    (int)(*(FDBIHI **)b)->hits[(*(FDBIHI **)b)->curHit] : -1;
  return(av - bv);
}

static int CDECL TXrppmOrposCmp ARGS((CONST void *a, CONST void *b));
static int CDECL
TXrppmOrposCmp(a, b)
CONST void      *a, *b;
/* Sort callback for sorting `RPPM_SET *' array by `RPPM_SET.sel->orpos'.
 */
{
  int   aOrpos, bOrpos;

  aOrpos = (*(RPPM_SET **)a)->sel->orpos;
  bOrpos = (*(RPPM_SET **)b)->sel->orpos;
  return(aOrpos - bOrpos);
}

/* ------------------------------------------------------------------------- */

RPPM *
openrppm(mm, mq, fop, fi, flags)
MMAPI   *mm;
MMQL    *mq;
int     fop;
FDBI    *fi;
int     flags;
/* Creates RPPM object, using given MMAPI/MMQL objects.  `fop' is
 * FOP_... operator, eg. FOP_RELEV, FOP_PROXIM, etc.  `fi' is
 * optional; opens FDBIS scanners if non-NULL.  Note that `mm'/`mq'
 * must remain open while this RPPM is open.  `flags' are RPF
 * flags.
 */
{
  static CONST char     fn[] = "openrppm";
  RPPM                  *rp;
  RPPM_SET              *setListBuf[32];
  RPPM_SET              *set = RPPM_SETPN, *dset, **setList = setListBuf;
  int                   i, s, n, indg, overmaxset, t, orposGap, curOrpos;
  MMQI                  *qi, *qiend;
  SEL                   **selstart, **selend, **selp, *sel;
  char                  *msg, *term;
  TXPMBUF               *pmbuf = TXPMBUFPN;     /* wtf set/pass-in someday */

  if ((rp = (RPPM *)TXcalloc(pmbuf, fn, 1, sizeof(RPPM))) == RPPMPN)
    goto err;
  memcpy(rp->vals, RppmValsCur, RVAR_NUM*sizeof(rp->vals[0]));
  rp->flags = ((RPF)flags & ~RPF_SETNEEDSPOST);
  if (TXtraceRppm & 0x20) rp->flags |= RPF_SAVEBYTEHITS;
  if (fi != FDBIPN) rp->totrecs = fdbi_gettotrecs(fi);
  rp->fop = fop;
  rp->intersects = mm->mme->intersects + 1;
  if ((rp->qintersects = mm->qintersects) >= 0) rp->qintersects += 1;
  rp->mm = mm;
  /* rest cleared by calloc() */

  rp->numsets = (fop == FOP_MMIN ? mq->n : mm->mme->nels);
  if (rp->numsets <= 0) goto err;
  if ((rp->sets = (RPPM_SET *)TXcalloc(pmbuf, fn, rp->numsets,
                                       sizeof(RPPM_SET))) == RPPM_SETPN ||
      (rp->hits = (FDBIHI *)TXcalloc(pmbuf, fn, rp->numsets,
                                     sizeof(FDBIHI))) == FDBIHIPN ||
      (rp->hitsp = (FDBIHI **)TXcalloc(pmbuf, fn, rp->numsets,
                                       sizeof(FDBIHI *))) == FDBIHIPPN ||
      (rp->orpos2idx = (int *)TXcalloc(pmbuf, fn, rp->numsets,
                                       sizeof(int))) == INTPN)
    goto err;
  for (i = 0; i < rp->numsets; i++) rp->orpos2idx[i] = -1;
  rp->orpos2idxLen = rp->numsets;               /* `rp->numsets' may dec. */
  selstart = mm->mme->el;
  selend = (fop == FOP_MMIN ? selstart + 1 : selstart + rp->numsets);
  qi = mq->lst;
  qiend = qi + mq->n;

  for (i = 0; i < rp->numsets; i++)
    {
      /* `mq->lst' has equivs for all SPM/PPM sets in alphabetical order.
       * Use it to open the sets in (mostly) alphabetical order, for
       * B-tree speed ("mostly" because we open a complete set at a time;
       * some words in a later set might come alphabetically earlier):
       */
      while (qi < qiend)                                /* while SPM/PPMs */
        {
          if ((s = (qi++)->orpos) >= rp->numsets)
            {
              putmsg(MERR, fn, "Internal error: bad `orpos' value: %d", s);
              continue;
            }
          set = rp->sets + s;
          if (set->sel == SELPN) goto gotset;
        }
      for (s = 0; s < rp->numsets; s++)                 /* REX/NPM/XPM */
        {
          set = rp->sets + s;
          if (set->sel == SELPN) break;
        }
    gotset:
      for (selp = selstart; selp < selend && (*selp)->orpos != s; selp++);
      if (fop == FOP_MMIN)
        {
          selp = selstart;
          (*selp)->orpos = s;
          if (*(mq->lst[i].s) == '-')
            (*selp)->logic = LOGINOT;
          else
            (*selp)->logic = LOGISET;
        }
      if (selp >= selend)                               /* shouldn't happen */
        {
          putmsg(MERR, fn, "Internal error: set %d missing from sel list", s);
          continue;
        }
      sel = *selp;
      /* Save and use type/logic separate from SEL, because in LIKEIN
       * the SEL is fake and re-used for each set:  KNG 001026
       * KNG 20080428 Bug 2151 fill out these fields before calling
       * openfdbis(), since latter uses them:
       */
      set->type = sel->pmtype;
      set->logic = sel->logic;
      set->sel = sel;
      set->eln = selp - selstart;
      set->tblfreq = 100;                               /* WAG default */
      set->gain = 1023;                                 /* default */
      if (TXtraceRppm)
        {
          if (sel->lst[0])
            term = sel->lst[0];                 /* wtf all of `lst' */
          else if (set->logic == LOGIPROP)           /* wtf setting? */
            term = "<inline-property>";
          else
            term = "?";
          set->term = TXstrdup(pmbuf, fn, term);
        }
      switch (sel->pmtype)
        {
        case PMISREX:
        case PMISXPM:
        case PMISNPM:
          if (fi != FDBIPN &&
              (msg = (char *)TXmalloc(pmbuf, fn, 21 +
                              strlen((char *)mm->mme->set[s]))) != CHARPN)
            {
              sprintf(msg, "Set `%s' not indexable", mm->mme->set[s]);
              fdbi_setpostmsg(fi, msg);
            }
          break;
        case PMISSPM:
        case PMISPPM:
          if (fi != FDBIPN)
            {
              /* KNG 010430 If there are delimiters, then a NOT set
               * could legally occur in a hit -- outside the delimiters.
               * Since we don't resolve delimiters until a post-process,
               * we can't drop negated terms during the index search,
               * so don't bother opening them:
               */
              if (sel->logic == LOGINOT &&
                  (*mm->mme->sdexp != '\0' || *mm->mme->edexp != '\0'))
                {
                  set->fs = FDBISPN;
                  /* Theoretically need to set postmsg here, but it'll
                   * be set anyway for delimiters: since there can't be
                   * an all-NOT query, we must have multiple sets and
                   * therefore can't skip post-proc for delimiters.
                   */
                  goto prtrace;
                }
              /* KNG 20051021 this could also return
               * FDBIS_CANLINEARDICTSEARCH, if set is not indexable but
               * could be linear-dictionary searched.  We save that value
               * for later use by fdbi_get() (see comments there).
               * NOTE that fdbi_get() may call openfdbis() again:
               * see this logic there as well:
               */
              set->fs = openfdbis(fi, mq, s, set, 0, &indg, &overmaxset);
              if (sel->mm3s->denymode == API3DENYERROR)
                {
                  if (overmaxset || fdbi_getovermaxwords(fi)) goto err;
                }
              if (set->fs != FDBISPN && set->fs != FDBIS_CANLINEARDICTSEARCH)
                {
                  if (!indg)
                    {
                      /* KNG 990811 If an indexable NOT set needs a post
                       * process, we have to blow it off completely.
                       * Otherwise we might discard valid rows in
                       * fdbi_get(): if an index set hit matches the NOT,
                       * we toss the row, but the later post-process might
                       * show the hit _didn't_ match the NOT and therefore
                       * the row was valid.  Note that this happens even
                       * if post-processing is disallowed: we'd rather err
                       * and return too many rows than too few.
                       * WTF maintain per-set post-proc flag and check this
                       * in fdbi_get() instead? still need correct rp->numidx
                       */
                      if (sel->logic == LOGINOT)
                        set->fs = closefdbis(set->fs);
                      else
                        rp->flags |= RPF_SETNEEDSPOST;
                    }
                  if (set->fs != FDBISPN) rp->numidx += 1;
                }
              /* NOTE: set->fs may be closed later; see rppm_unindex() */
            }
          else                                  /* print FDBI's trace */
            {
            prtrace:
              if (FdbiTraceIdx >= 13)
                {
                  putmsg(MINFO, CHARPN, "(no index) Set: %s",mm->mme->set[s]);
                  if (FdbiTraceIdx >= 14)
                    {
                      for (t = 0; t < mq->n; t++)
                        if (mq->lst[t].orpos == s)
                          putmsg(MINFO, CHARPN, " (no index) Phrase: %s",
                                 mq->lst[t].s);
                    }
                }
            }
          break;
        case PMISNOP:                           /* no-op (inline setting) */
          /* Bug 3905: do not include LOGIPROP/PMISNOP sets in RPPM;
           * flummoxes post-proc-needed logic, ranks, etc.:
           */
          set->sel = NULL;                      /* force removal below */
          set->fs = FDBISPN;
          break;
        default:
          putmsg(MWARN, fn,
                 "Internal error: Unknown pattern matcher type %d for set %d",
                 (int)sel->pmtype, (int)s);
          /* Treat it like PMISNOP: */
          set->sel = NULL;                      /* force removal below */
          set->fs = FDBISPN;
          break;
        }
      /* Bug 3905: sanity-check logic: */
      switch (set->logic)
        {
        case LOGIAND:
        case LOGISET:
        case LOGINOT:
          break;
        case LOGIPROP:                          /* no-op */
          set->sel = NULL;                      /* force removal below */
          set->fs = FDBISPN;
          break;
        default:
          putmsg(MWARN, fn,
                 "Internal error: Unknown logic type %d for set %d",
                 (int)set->logic, (int)s);
          /* Treat it like PMISNOP: */
          set->sel = NULL;                      /* force removal below */
          set->fs = FDBISPN;
          break;
        }
    }

  for (dset = set = rp->sets, i = n = 0; i < rp->numsets; i++, set++)
    {                                                   /* delete error sets */
      if (set->sel != SELPN)
        {
          *dset = *set;
          /* Bug 3905: also fix up FDBI[SPWX].hi.set values; otherwise
           * they are off by up to the number of sets deleted here:
           */
          if (set->fs &&
              set->fs != FDBIS_CANLINEARDICTSEARCH &&
              !TXfdbisSetRppmSet(set->fs, dset))
            goto err;
          rp->hits[n].set = dset;
          rp->hitsp[n] = rp->hits + n;
          n++;
          dset++;
        }
      else if (set->fs != FDBISPN && set->fs != FDBIS_CANLINEARDICTSEARCH)
        {                                               /* in case opened */
          set->fs = closefdbis(set->fs);
          rp->numidx--;
        }
    }
  rp->numsets = dset - rp->sets;

  if (rp->numsets == 0) goto err;                       /* nothing to do */
  if ((rp->fh = openfheap(txRppmLinearSearchHeapCmp, NULL, 0)) == FHEAPPN)
    goto err;

  /* Compute RPPM_SET.orgIdx: same as RPPM_SET.sel->orpos, but only
   * counting RPPM sets, i.e. no PMISNOP/LOGIPROP sets.  Also compute
   * RPPM.orpos2idx array, for mapping SEL.orpos to RPPM.sets index:
   */
  if ((size_t)rp->numsets > sizeof(setListBuf)/sizeof(setListBuf[0]) &&
      !(setList = (RPPM_SET **)TXcalloc(pmbuf, fn, rp->numsets,
                                        sizeof(RPPM_SET *))))
    goto err;
  for (i = 0; i < rp->numsets; i++)
    setList[i] = &rp->sets[i];
  /* Sort by `sel->orpos': */
  if (rp->numsets > 1)
    qsort(setList, rp->numsets, sizeof(RPPM_SET *), TXrppmOrposCmp);
  orposGap = 0;
  for (i = 0; i < rp->numsets; i++)
    {
      /* Normally each `orpos' should be +1 from the previous;
       * if it is greater, we skipped a set (e.g. PMISNOP).
       * Removed such gaps from the +1-incrementing `orgIdx' values:
       */
      curOrpos = setList[i]->sel->orpos;
      orposGap += (curOrpos - (i>0 ? (int)setList[i-1]->sel->orpos : -1)) - 1;
      rp->orpos2idx[curOrpos] = setList[i]->orgIdx = curOrpos - orposGap;
    }

  rppm_precomp(rp);
  goto done;

err:
  rp = closerppm(rp);
done:
  if (setList && setList != setListBuf) setList = TXfree(setList);
  return(rp);
}

static void txRppmCloseIndexExprs ARGS((RPPM *rp));
static void
txRppmCloseIndexExprs(rp)
RPPM    *rp;
/* Internal use. */
{
  if (rp->rexobj != NULL)
    {
      if (rp->gethit == (FDBI_GETHITFUNC *)getrlex)
        closerlex((RLEX *)rp->rexobj);
      else
        closerex((FFS *)rp->rexobj);
      rp->rexobj = NULL;
    }
  rp->gethit = NULL;
  rp->getlen = NULL;
}

RPPM *
closerppm(rp)
RPPM    *rp;
{
  int           i;
  RPPM_SET      *rs, *re;
  FDBIHI        *hi;

  if (rp == RPPMPN) goto done;

  if (rp->sets != RPPM_SETPN)
    {
      for (rs = rp->sets, re = rs + rp->numsets; rs < re; rs++)
        {
          closefdbis(rs->fs);
          if (rs->term != CHARPN) free(rs->term);
        }
      free(rp->sets);
    }
  if (rp->hits != FDBIHIPN)
    {
      for (i = 0; i < rp->numsets; i++)
        {
          hi = &rp->hits[i];
          if (hi->hits != DWORDPN) free(hi->hits);
          if (hi->hitLens != DWORDPN) free(hi->hitLens);
          /* Bug NNN: also free `hi->byteHits', `hi->byteHitEnds': */
          hi->byteHits = TXfree(hi->byteHits);
          hi->byteHitsSz = 0;
          hi->byteHitEnds = TXfree(hi->byteHitEnds);
          hi->byteHitEndsSz = 0;
        }
      rp->hits = TXfree(rp->hits);
    }
  rp->hitsp = TXfree(rp->hitsp);
  rp->fh = closefheap(rp->fh);
  rp->orpos2idx = TXfree(rp->orpos2idx);
  txRppmCloseIndexExprs(rp);

  free(rp);

done:
  return(RPPMPN);
}

void
rppm_unindex(rp)
RPPM    *rp;
/* Closes index-related parts of `rp' to dissociate it from its FDBI,
 * eg. as if it were opened with FDBIPN.
 */
{
  RPPM_SET      *s, *e;

  for (s = rp->sets, e = s + rp->numsets; s < e; s++)
    {
      if (s->fs == FDBISPN || s->fs == FDBIS_CANLINEARDICTSEARCH) continue;
      s->fs = closefdbis(s->fs);
      rp->numidx--;
    }
}

void
rppm_setflags(rp, flags, on)
RPPM    *rp;
int     flags, on;
/* Turns RPF `flags' on if `on', else off.
 */
{
  if (on)
    rp->flags |= (RPF)flags;
  else
    rp->flags &= (RPF)~flags;
}

RPF
TXrppmGetFlags(RPPM *rp)
{
  return(rp->flags);
}

void
rppm_setwts(rp, gains, tblfreqs)
RPPM    *rp;
int     *gains;
long    *tblfreqs;
/* `gains' are relative weights of each set, -RPPM_MAX_WT to RPPM_MAX_WT.
 * `tblfreqs' are raw table frequencies.
 */
{
  int           i;
  RPPM_SET      *rs;

  for (i = 0; i < rp->numsets; i++)
    {
      if (i >= MAXSELS) break;
      rs = rp->sets + i;
      rs->gain = (gains != INTPN) ? gains[i] : RPPM_MAX_WT;
      rs->gain = (rs->gain*1023)/RPPM_MAX_WT;
      rs->tblfreq = (tblfreqs != LONGPN) ? tblfreqs[i] : RPPM_MAX_WT;
    }
  rppm_precomp(rp);
}

int
TXrppmSetIndexExprs(rp, exprs, locale)
RPPM    *rp;            /* (in/out) RPPM to modify */
char    **exprs;        /* (in, opt.) index expressions; ""-terminated list */
char    *locale;        /* (in, opt.) locale */
/* Sets index expressions and locale to use during linear searching
 * with RPPM (rppm_searchbuf()).  Increases accuracy of
 * ranking/abstract by more accurately mapping word offsets of linear
 * hits.  `exprs' can be NULL to close existing expressions (if any).
 * See similar code in openwtix().
 * Returns 0 on error.
 * Thread-unsafe (changes locale).
 */
{
  static CONST char     fn[] = "TXrppmSetIndexExprs";
  CONST char            *curLocale;
  char                  *prevLocale = CHARPN;
  int                   ret;

  txRppmCloseIndexExprs(rp);
  if (exprs == CHARPPN || *exprs == CHARPN || **exprs == '\0') goto ok;

  /* Set locale first, before opening REX which will use it: */
  if (locale != CHARPN)                         /* specified locale */
    {
      curLocale = TXgetlocale();
      if (strcmp(locale, curLocale) != 0)       /* different locale */
        {
          if ((prevLocale = strdup(curLocale)) == CHARPN)
            {
              TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn,
                               strlen(curLocale) + 1, 1);
              goto err;
            }
          if (TXsetlocale(locale) == CHARPN)
            {
              putmsg(MERR, fn, "Invalid locale `%s'", locale);
              free(prevLocale);
              prevLocale = CHARPN;
            }
        }
    }

  /* Open expressions, using REX or rlex as needed: */
  if (**exprs != '\0' && (exprs[1] == CHARPN || *exprs[1] == '\0'))
    {                                           /* optimize: direct REX use */
      rp->gethit = (FDBI_GETHITFUNC *)getrex;
      rp->getlen = (FDBI_GETLENFUNC *)rexsize;
      rp->rexobj = openrex((byte *)exprs[0], TXrexSyntax_Rex);
    }
  else                                          /* multiple expressions */
    {
      rp->gethit = (FDBI_GETHITFUNC *)getrlex;
      rp->getlen = (FDBI_GETLENFUNC *)rlexlen;
      rp->rexobj = openrlex((const char **)exprs, TXrexSyntax_Rex);
    }
  if (rp->rexobj == NULL) goto err;
ok:
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  /* Can now restore locale, even though we are not done with `rexobj',
   * because all locale-dependent calls in REX should be in openrex(),
   * not getrex()/rexsize() etc.:
   */
  if (prevLocale != CHARPN)
    {
      if (TXsetlocale(prevLocale) == CHARPN)
        putmsg(MERR, fn, "Cannot restore locale `%s'", prevLocale);
      free(prevLocale);
      prevLocale = CHARPN;
    }
  return(ret);
}

/* ------------------------------------------------------------------------ */

static int
txRppmLinearSearch(RPPM      *rp,    /* (in) RPPM object */
                   FDBIHI    **hits, /* (out) hits to populate */
                   byte      *buf,   /* (in) start of search buf */
                   byte      *end)   /* (in) end of search buf */
/* Searches `buf' (length `end' - `buf') and puts all hit locations in
 * `hits', using associated pattern matchers.  Assumes there are
 * rp->numsets elements of `hits'.  Will re-alloc `hits[]->hits' fields.
 * Note that locations stored are approximate, because byte offsets from
 * scanners are converted into word offsets simply by dividing by 6
 * (arbitrary average word+space size) and fudging overlaps.
 * Caller must sort `hits' by initial hit offset (not-found first);
 * document can then be ranked with rppm_rankbest().
 * Returns number of actual-hit sets at start of array, or -1 on error
 * (caller must clear `hits' on error).
 */
{
  int                   i, nvalid, num, gap, wo, lastwo, ret;
  FDBIHI                *h;
  byte                  *hit;
  size_t                incReq;
  FHEAP                 *fh;
  MM3S                  *mm3s;
#define INCARRAY(array, sz, curNum)                                     \
  incReq = ((curNum) + 1)*sizeof((array)[0]);                           \
  if ((sz) < incReq &&                                                  \
      !fdbi_allocbuf(__FUNCTION__, (void **)(char *)&(array), &(sz), incReq))\
    goto err

  num = rp->numsets;

  for (i = 0; i < num; i++)                     /* get all hits for all sets */
    {
      h = hits[i];
      mm3s = h->set->sel->mm3s;                 /* Bug 3908 */
      mm3s->start = buf;                        /* WTF getmm() struct drill */
      mm3s->end = end;                          /* "" */
      h->nhits = 0;
      if (h->set->logic == LOGINOT)             /* findsel() inverts NOTs */
        {
          /* Bug 4474: use TXfindselWithSels() wrapper: */
          if (TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                                h->set->eln, buf, end, SEARCHNEWBUF) == BYTEPN)
            goto err;                           /* WTF within processing? */
          continue;
        }
      for (hit = TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                                   h->set->eln, buf, end, SEARCHNEWBUF);
           hit != BYTEPN;
           hit = TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                                   h->set->eln, buf, end, CONTINUESEARCH))
        {
          INCARRAY(h->hits, h->hitsz, h->nhits);
          h->hits[h->nhits] = (hit - buf);
          /* Do not need to save `h->hitLens' info here, because that
           * is only used by index search currently?
           */
          if (rp->flags & RPF_SAVEBYTEHITS)
            {
              INCARRAY(h->byteHits, h->byteHitsSz, h->nhits);
              INCARRAY(h->byteHitEnds, h->byteHitEndsSz, h->nhits);
              h->byteHits[h->nhits] = (hit - buf);
              h->byteHitEnds[h->nhits] =
                (hit + rp->mm->mme->el[h->set->eln]->hitsz) - buf;
            }
          h->nhits++;
        }
    }

  /* Translate byte offsets from pattern matchers into word offset
   * guesstimates (ranker is based on word offsets) by dividing by
   * average word separation (6).  But small adjacent words may then
   * overlap: bump them apart so they're separate to the ranker, since
   * word order is important.  (WTF this has the side effect of bumping
   * apart truly overlapping hits, but what the hey):
   * txRppmLinearSearchIdx() tries to be more accurate:
   */
  fh = rp->fh;
  fheap_clear(fh);
  fheap_setcmp(fh, txRppmLinearSearchHeapCmp);
  for (i = 0; i < num; i++)
    {
      h = hits[i];
      h->curHit = 0;
      if (h->nhits > 0 && !fheap_insert(fh, h))
        goto err;
    }
  gap = 0;
  lastwo = -1;
  nvalid = fheap_num(fh);
  while (fheap_num(fh) > 0)
    {
      h = (FDBIHI *)fheap_top(fh);
      fheap_deletetop(fh);
      wo = (int)(h->hits[h->curHit]/6) + gap;   /* word offset guesstimate */
      if (wo == lastwo)                         /* overlap: move apart */
        {
          wo++;
          gap++;
        }
      h->hits[h->curHit++] = lastwo = wo;
      if (h->curHit < h->nhits && !fheap_insert(fh, h))
        goto err;
    }

  ret = nvalid;
  goto done;

err:
  ret = -1;
done:
  return(ret);
#undef INCARRAY
}

static int
txRppmLinearSearchIdx(RPPM      *rp,    /* (in) RPPM object */
                      FDBIHI    **hits, /* (in/out) hits to populate */
                      byte      *buf,   /* (in) start of search buf */
                      byte      *end)   /* (in) end of search buf */
/* Like txRppmLinearSearch(), but uses (already-set) index expressions to set
 * word locations better.  Slower due to use of index expressions.
 */
{
  int                   i, nvalid, num, ret, wordsEof = 0;
  FDBIHI                *setHi, idxWord;
#define PREV    0
#define CUR     1
  dword                 idxWordHits[2];
  size_t                idxWordByteHits[2], idxWordByteHitEnds[2];
  byte                  *hit;
  FHEAP                 *fh;
  MM3S                  *mm3s = rp->mm->mme;
  size_t                incReq, curDiff, prevDiff;
#define INCARRAY(array, sz, curNum)                                     \
  incReq = ((curNum) + 1)*sizeof((array)[0]);                           \
  if ((sz) < incReq &&                                                  \
      !fdbi_allocbuf(__FUNCTION__, (void **)(char *)&(array), &(sz), incReq))\
    goto err
#define ABSDIFF(x, y)   ((x) > (y) ? (x) - (y) : (y) - (x))

  num = rp->numsets;

  /* Initialize the heap with first hit of all sets, to pull off in order: */
  fh = rp->fh;
  fheap_clear(fh);
  fheap_setcmp(fh, txRppmLinearSearchIdxHeapCmp);
  for (i = 0; i < num; i++)                     /* each set */
    {
      setHi = hits[i];
      mm3s->start = buf;                        /* WTF getmm() struct drill */
      mm3s->end = end;                          /* "" */
      setHi->nhits = setHi->curHit = 0;
      if (setHi->set->logic == LOGINOT)         /* findsel() inverts NOTs */
        {
          /* Bug 4474: use TXfindselWithSels() wrapper, in case we change
           * `mm3s' to SEL's MM3S instead of rp->mm->mme:
           */
          if (!TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                                 setHi->set->eln, buf, end, SEARCHNEWBUF))
            goto err;                           /* WTF within processing? */
          continue;
        }
      hit = TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                              setHi->set->eln, buf, end, SEARCHNEWBUF);
      if (hit != BYTEPN)                        /* found a hit */
        {
          /* Note that if RPF_SAVEBYTEHITS is not set, we will never
           * alloc `byteHits'/`byteHitEnds' beyond 1 for the current hit.
           * Normally `curHit' iterates into `hits'/`byteHits'/`byteHitEnds',
           * and `nhits' is a total count.   But for `setHi', `curHit'
           * indexes and counts into `byteHits'/`byteHitEnds'
           * (for txRppmLinearSearchIdxHeapCmp() compatibility), and `nhits'
           * indexes into `hits' (expands regardless of RPF_SAVEBYTEHITS).
           */
          INCARRAY(setHi->hits, setHi->hitsz, setHi->nhits);
          setHi->hits[setHi->nhits] = 0;        /* set when popped off heap */
          INCARRAY(setHi->byteHits, setHi->byteHitsSz, setHi->curHit);
          INCARRAY(setHi->byteHitEnds, setHi->byteHitEndsSz, setHi->curHit);
          setHi->byteHits[setHi->curHit] = hit - buf;
          setHi->byteHitEnds[setHi->curHit] =
            (hit + rp->mm->mme->el[setHi->set->eln]->hitsz) - buf;
          if (!fheap_insert(fh, setHi)) goto err;
        }
    }

  /* Initialize indexer: */
  memset(&idxWord, 0, sizeof(FDBIHI));
  /* We'll only use previous and current index words, so 2-element arrays: */
  idxWord.hits = idxWordHits;
  idxWord.byteHits = idxWordByteHits;
  idxWord.byteHitEnds = idxWordByteHitEnds;
  /* There are always exactly 2 hits in `idxWord' (either real or fake),
   * so that compare functions work: previous-word (#0) and current-word (#1):
   */
  idxWord.hits[PREV] = 0;
  idxWord.byteHits[PREV] = EPI_OS_SIZE_T_MAX;          /*inf. far away so not close*/
  idxWord.byteHitEnds[PREV] = EPI_OS_SIZE_T_MAX;
  idxWord.hits[CUR] = 0;                        /* first word is #0 */
  if ((hit = rp->gethit(rp->rexobj, buf, end, SEARCHNEWBUF)) != BYTEPN)
    {
      idxWord.byteHits[CUR] = hit - buf;
      idxWord.byteHitEnds[CUR] = (hit + rp->getlen(rp->rexobj)) - buf;
    }
  else                                          /* no index hits; fake one */
    {
      idxWord.byteHits[CUR] = 0;                /* at start of text buf */
      idxWord.byteHitEnds[CUR] = 0;
      wordsEof = 1;                             /* no more index words */
    }
  idxWord.nhits = 2;                            /* previous and current */
  idxWord.curHit = CUR;                         /* curHit is current word */


  /* Translate byte offsets from pattern matchers into word offset
   * guesstimates (ranker is based on word offsets) by using nearest
   * index-expression-match word.  We save the original byte offsets
   * for use by abstracter.  Word may be exact match for set offset+len
   * (eg. if single term SPM), may match offset but not len (eg. first
   * word of a set phrase), or may not match at all (eg. REX set of
   * non-indexable chars).  Even if no match, we still match the set to
   * an index-expression word so that word #s and their byte offsets
   * agree with index search, so that ranks (hopefully) agree.
   */
  nvalid = fheap_num(fh);
  while (fheap_num(fh) > 0)                     /* still more set hits */
    {
      setHi = (FDBIHI *)fheap_top(fh);
      fheap_deletetop(fh);                      /* pull off next set hit */
      while (!wordsEof &&
             txRppmLinearSearchIdxHeapCmp(&idxWord, setHi, NULL) < 0)
        {                                       /* while curWord < setHi */
          /* previous word = current word: */
          idxWord.hits[PREV] = idxWord.hits[CUR];
          idxWord.byteHits[PREV] = idxWord.byteHits[CUR];
          idxWord.byteHitEnds[PREV] = idxWord.byteHitEnds[CUR];
          /* curWord = next index word: */
          hit = rp->gethit(rp->rexobj, buf, end, CONTINUESEARCH);
          if (hit == BYTEPN)                    /* no more index words */
            wordsEof = 1;
          else                                  /* found next word */
            {
              idxWord.hits[CUR]++;              /* curWord = next word */
              idxWord.byteHits[CUR] = hit - buf;
              idxWord.byteHitEnds[CUR] = (hit + rp->getlen(rp->rexobj)) - buf;
            }
        }

      /* Pick closest word hit for `setHi': `curWord' or `prevWord'.
       * Unlike txRppmLinearSearchIdxHeapCmp(), this is a 3-way compare:
       * wtf review this algorithm: if hit-off diffs are same,
       * but one is "inside" the set and one is before, inside-set hit
       * should be used (assuming both end at same point)?
       */
      curDiff =ABSDIFF(idxWord.byteHits[CUR], setHi->byteHits[setHi->curHit]);
      prevDiff=ABSDIFF(idxWord.byteHits[PREV],setHi->byteHits[setHi->curHit]);
      if (curDiff < prevDiff)                   /* current starts closest */
        setHi->hits[setHi->nhits] = idxWord.hits[CUR];
      else if (prevDiff < curDiff)              /* prev word starts closest */
        setHi->hits[setHi->nhits] = idxWord.hits[PREV];
      else                                      /* both start equidistant */
        {
          curDiff = ABSDIFF(idxWord.byteHitEnds[CUR],
                            setHi->byteHitEnds[setHi->curHit]);
          prevDiff = ABSDIFF(idxWord.byteHitEnds[PREV],
                             setHi->byteHitEnds[setHi->curHit]);
          if (curDiff < prevDiff)               /* current word ends closest*/
            setHi->hits[setHi->nhits] = idxWord.hits[CUR];
          else                                  /* prev word ends closest */
            setHi->hits[setHi->nhits] = idxWord.hits[PREV];
        }
      setHi->nhits++;                           /* done with current `hits' */

      /* Get next hit for set, and put back into heap if found: */
      hit = TXfindselWithSels(mm3s, rp->mm->mme->el, rp->mm->mme->nels,
                              setHi->set->eln, buf, end, CONTINUESEARCH);
      if (hit != BYTEPN)                        /* found another hit */
        {
          INCARRAY(setHi->hits, setHi->hitsz, setHi->nhits);
          setHi->hits[setHi->nhits] = 0;        /* set when popped off heap*/
          if (rp->flags & RPF_SAVEBYTEHITS)
            {
              setHi->curHit++;                  /* done with this hit */
              INCARRAY(setHi->byteHits, setHi->byteHitsSz, setHi->curHit);
              INCARRAY(setHi->byteHitEnds,setHi->byteHitEndsSz,setHi->curHit);
            }
          setHi->byteHits[setHi->curHit] = hit - buf;
          setHi->byteHitEnds[setHi->curHit] =
            (hit + rp->mm->mme->el[setHi->set->eln]->hitsz) - buf;
          if (!fheap_insert(fh, setHi)) goto err;
        }
    }

  ret = nvalid;
  goto done;

err:
  ret = -1;
done:
  if (!(rp->flags & RPF_SAVEBYTEHITS))
    {
      /* Free `byteHits'/`byteHitEnds' to avoid confusion, as they are
       * allocated to 1 not `nhits'; if RPF_SAVEBYTEHITS not set:
       */
      for (i = 0; i < num; i++)
        {
          setHi = hits[i];
          if (setHi->byteHits != SIZE_TPN)
            {
              free(setHi->byteHits);
              setHi->byteHits = SIZE_TPN;
            }
          setHi->byteHitsSz = 0;
          if (setHi->byteHitEnds != SIZE_TPN)
            {
              free(setHi->byteHitEnds);
              setHi->byteHitEnds = SIZE_TPN;
            }
          setHi->byteHitEndsSz = 0;
        }
    }
  return(ret);
#undef INCARRAY
#undef ABSDIFF
#undef CUR
#undef PREV
}

/* ------------------------------------------------------------------------- */

int
rppm_searchbuf(rp, hits, buf, end)
RPPM    *rp;
FDBIHI  **hits;
byte    *buf, *end;
/* Searches `buf' (length `end' - `buf') and puts all hit locations in
 * `hits', using associated pattern matchers.  Assumes there are
 * rp->numsets elements of `hits'.  Locations stored may be more or less
 * approximate, depending on RPF_USEINDEXEXPRS.  `hits[]->byteHits' etc.
 * only set if RPF_USEINDEXEXPRS.  `hits' will be sorted by
 * initial hit offset (not-found first); document can then be ranked
 * with rppm_rankbest().
 * Returns number of no-hit sets at start of array, or -1 on error.
 */
{
  static CONST char     fn[] = "rppm_searchbuf";
  int                   i, nvalid, num, ret;
  FDBIHI                *h;

  num = rp->numsets;
  if (rp->fop == FOP_MMIN)                      /* KNG 001026 SELs are fake */
    {
      putmsg(MERR + UGE, fn, "Internal error: Cannot handle LIKEIN");
      goto err;
    }

  if ((rp->flags & RPF_USEINDEXEXPRS) && rp->rexobj != NULL)
    nvalid = txRppmLinearSearchIdx(rp, hits, buf, end);
  else
    nvalid = txRppmLinearSearch(rp, hits, buf, end);
  if (nvalid < 0) goto err;

  for (i = 0; i < num; i++)                     /* init arrays for ranker */
    {
      h = hits[i];
      if (h->nhits > 0)
        {
          h->curHit = 0;
          TXsetrecid(&h->loc, (EPI_OFF_T)0);
        }
      else
        {
          h->curHit = (size_t)(-1);
          TXsetrecid(&h->loc, (EPI_OFF_T)(-1));
        }
    }
  if (nvalid > 1 ||                             /* order by initial hit */
      (nvalid > 0 && num > nvalid))
    qsort(hits, num, sizeof(hits[0]), rppm_hitcmp);
  ret = num - nvalid;
  goto done;

err:
  for (i = 0; i < num; i++)
    {
      h = hits[i];
      h->nhits = 0;
      h->curHit = (size_t)(-1);
      TXsetrecid(&h->loc, (EPI_OFF_T)(-1));
    }
  ret = -1;
done:
  return(ret);
}

/* ------------------------------------------------------------------------- */

int
rppm_mminfo2hits(rp, hits, mm)
RPPM    *rp;
FDBIHI  **hits;
MMAPI   *mm;
/* Gets hit info from `mm' (assumes getmmapi() just called) and puts
 * it in `hits'.  Assumes there are rp->numsets elements of `hits'.
 * Will re-alloc `hits' field.  Note that locations stored are
 * approximate, because byte offsets are converted into word offsets.
 * `hits' will be sorted by initial hit offset (not-found first);
 * document can then be ranked with rppm_rankbest().  Returns number
 * of no-hit sets at start of array, or -1 on error.
 */
{
  static CONST char     fn[] = "rppm_mminfo2hits";
  int                   i, nvalid, num, ret, rppmSetIdx;
  FDBIHI                *h;
  MM3S                  *mm3s;
  SEL                   *el;
  size_t                incReq;
#define INCARRAY(array, sz, curNum)                                     \
  incReq = ((curNum) + 1)*sizeof((array)[0]);                           \
  if ((sz) < incReq &&                                                  \
      !fdbi_allocbuf(fn, (void **)(char *)&(array), &(sz), incReq))     \
    goto err

  num = rp->numsets;
  for (i = 0; i < num; i++)
    hits[i]->nhits = 0;
  mm3s = mm->mme;
  nvalid = 0;
  for (i = 0; i < mm3s->nels; i++)
    {
      el = mm3s->el[i];
      if (!el->member) continue;                /* not part of this hit? */
      if ((unsigned)el->orpos >= (unsigned)rp->orpos2idxLen) continue;
      rppmSetIdx = rp->orpos2idx[el->orpos];
      if ((unsigned)rppmSetIdx >= (unsigned)num) continue; /* sanity */
      h = hits[rppmSetIdx];
      if (h->set->logic == LOGINOT) continue;   /* findsel inverts NOTs */
      INCARRAY(h->hits, h->hitsz, h->nhits);
      h->hits[h->nhits] = (el->hit - mm3s->start)/6;
      if (rp->flags & RPF_SAVEBYTEHITS)         /* Bug 3746 */
        {
          INCARRAY(h->byteHits, h->byteHitsSz, h->nhits);
          INCARRAY(h->byteHitEnds, h->byteHitEndsSz, h->nhits);
          h->byteHits[h->nhits] = el->hit - mm3s->start;
          h->byteHitEnds[h->nhits] = (el->hit + el->hitsz) - mm3s->start;
        }
      h->nhits++;
      nvalid++;
    }

  for (i = 0; i < num; i++)                     /* init arrays for ranker */
    {
      h = hits[i];
      if (h->nhits > 0)
        {
          h->curHit = 0;
          TXsetrecid(&h->loc, (EPI_OFF_T)0);
        }
      else
        {
          h->curHit = (size_t)(-1);
          TXsetrecid(&h->loc, (EPI_OFF_T)(-1));
        }
    }
  if (nvalid > 1 ||                             /* order by initial hit */
      (nvalid > 0 && num > nvalid))
    qsort(hits, num, sizeof(hits[0]), rppm_hitcmp);
  ret = num - nvalid;
  goto done;

err:
  for (i = 0; i < num; i++)
    {
      h = hits[i];
      h->nhits = 0;
      h->curHit = (size_t)(-1);
      TXsetrecid(&h->loc, (EPI_OFF_T)(-1));
    }
  ret = -1;
done:
  return(ret);
#undef INCARRAY
}

/* ------------------------------------------------------------------------- */

#undef RPPM_TRACE
#undef FUNC
#define FUNC(x) x
#include "rppmaux.c"

#undef RPPM_TRACE
#undef FUNC
#define RPPM_TRACE
#define FUNC(x) x##_trace
#include "rppmaux.c"

#undef RPPM_TRACE
#undef FUNC

int
rppm_rankbuf(rp, mm, buf, end, byteMedian)
RPPM    *rp;
MMAPI   *mm;
byte    *buf, *end;
size_t  *byteMedian;    /* (out, opt.) median byte offset of best hit */
/* Gets hit info from `mm' (if non-NULL and RPF_RANKMMINFO set) or
 * searches `buf', and ranks buffer.  Returns best rank.
 * Sets `*byteMedian' to best rank's byte median (see rppm_rankcur()).
 */
{
  int           i, n, ret;
  const char    *func = NULL;

  if (byteMedian != SIZE_TPN) *byteMedian = (size_t)(-1);
  rp->numInitSkip = rp->numsets;                /* skip all until ok search */
  /* If RPF_MMCHECK set, we must check buffer with Metamorph;
   * somebody requested a post-process:   KNG 980710
   */
  if ((rp->flags & RPF_MMCHECK) &&
      mm != MMAPIPN &&
      getmmapi(mm, buf, end, SEARCHNEWBUF) == CHARPN)
    /* TX_TMF_OverallHit reported by getmmapi() */
    return(0);                                  /* no hits */

  if ((rp->flags & RPF_RANKMMINFO) && mm)
    {
      func = "rppm_mminfo2hits";
      i = rppm_mminfo2hits(rp, rp->hitsp, mm);
    }
  else
    {
      func = "rppm_searchbuf";
      i = rppm_searchbuf(rp, rp->hitsp, buf, end);
    }
  if (i < 0)                                    /* error */
    {
      ret = -1;                                 /* arbitrary bad rank */
      goto finally;
    }
  n = rp->numsets - (rp->numInitSkip = i);
  if (n == 0)                                   /* no hits */
    {
      ret = 0;
    finally:
      if (TXtraceMetamorph & TX_TMF_OverallHit)
        {
          MM3S  *ms = mm->mme;
          char  contextBuf[256];

          TXmmSetupHitContext(ms, contextBuf, sizeof(contextBuf));
          putmsg(MINFO, __FUNCTION__,
                 "%s of MMAPI object %p: %s `%s'", func, mm,
                 (ret < 0 ? "error with" : "no hits in "), contextBuf);
        }
      return(ret);
    }
  /* TX_TMF_OverallHit reported by rppm_rankbest() */
  if (TXtraceRppm)
    return(n == 1 ? rppm_rankbest_one_trace(rp, rp->hitsp[i], byteMedian) :
           rppm_rankbest_trace(rp, rp->hitsp + i, n, byteMedian));
  else
    return(n == 1 ? rppm_rankbest_one(rp, rp->hitsp[i], byteMedian) :
           rppm_rankbest(rp, rp->hitsp + i, n, byteMedian));
}

int
TXrppmGetBestHitInfo(RPPM       *rp,            /* (in) RPPM object */
                     FDBIHI     ***hits)        /* (out) hits list */
/* Get hit info, immediately after rppm_rankbuf().  Only meaningful if
 * RPF_SAVEBESTHITS was set.
 * Returns length of `*hits' array.  Sets `*hits' to FDBIHI * array with
 * each `curHit' set to best hit that was used for rppm_rankbuf() return.
 * Hit list is sort by ascending word offset.  Do not modify.
 */
{
  *hits = rp->hitsp + rp->numInitSkip;
  return(rp->numsets - rp->numInitSkip);
}
