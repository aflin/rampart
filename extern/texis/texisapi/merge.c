#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  undef getcwd
#  include <unistd.h>
#  ifdef MEMDEBUG
#    define getcwd(a, b)        mac_getcwd(a, b, __FILE__, __LINE__)
#  endif
#endif
#include "texint.h"
#include "heap.h"
#include "pile.h"
#include "meter.h"
#include "merge.h"

struct MERGE_tag
{
  FHEAP         *heap;          /* heap to hold and merge input piles */
  void          *defrag;        /* "low" mem reserved for other pile objs */
  PILE          *memp;          /* 1st-stage in-mem piles (merge_newpile()) */
  PILE          *outp;          /* intermediate output pile */
  PILEOPENFUNC  *interopen;     /* optional for intermediate merge */
  size_t        memsz;          /* max mem to use */
  MERGECMP      *cmp;           /* PILE item comparison function */
  void          *usr;           /* compare, output funcs' user data */
  METER         *prevmeter;     /* optional parent meter */
  METER         *meter;
  TXMDT         metertype;      /* used if no prevmeter */
  MDOUTFUNC     *meterout;      /* used if no prevmeter */
  MDFLUSHFUNC   *meterflush;    /* used if no prevmeter */
  void          *meterusr;      /* used if no prevmeter */
  char          *intermsg;      /* optional intermediate-merge message */
  char          *finalmsg;      /* optional final-merge meter message */
  EPI_HUGEINT   totalitems;     /* total number of items for next merge */
  EPI_HUGEINT   doneitems;      /* items completed (during merge) */
  int           prevNumAllocFailures;   /* previous report # alloc failures */
};

int     TxMergeDefragSz = 0;    /* pre-alloced de-frag mem size */
int     TxMergeFlush = 1;       /* see also setprop.c */

/* ------------------------------------------------------------------------- */

MERGE *
openmerge(cmp, usr, memsz, interopen)
MERGECMP        *cmp;
void            *usr;
size_t          memsz;
PILEOPENFUNC    *interopen;
/* Opens merge object.  `cmp' is pile-item comparison function to use,
 * will be passed `usr'.  `interopen' is pile open function to use for
 * temporary intermediate merge; will be passed `usr' as well.  May be
 * NULL if no intermediate merges to be done (e.g. small temp index to
 * RAM dbf, or no merge_newpile()/merge_newitem() calls).  `memsz' is
 * maximum memory to use for newpile/newitem piles (e.g. before merging
 * to intermediate pile); may be exceeded if `interopen' is NULL.  If
 * `cmp' is MERGECMP_WTIX, assumes WTIX-pile-style compares.
 */
{
  static CONST char     fn[] = "openmerge";
  MERGE                 *m;

  if (!(m = (MERGE *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(MERGE))))
    goto err;
  if (cmp == MERGECMP_WTIX)
    m->heap = TXfheapOpen(FHCMPPN, TXfheapInsertWtix, TXfheapDeleteTopWtix,
                          m, 0);
  else
    m->heap = openfheap((FHCMP *)cmp, m, 0);
  if (m->heap == FHEAPPN) goto err;
  /* wtf if `interopen' is NULL, whack memsz to infinity and beyond, but
   * keep pile's _chunk_ size small, without drilling struct.
   */
  m->interopen = interopen;
  m->memsz = memsz;
  m->cmp = cmp;
  m->usr = usr;
  m->prevNumAllocFailures = TXmemGetNumAllocFailures();
  /* rest cleared by calloc() */
  goto done;

err:
  m = closemerge(m);
done:
  return(m);
}

MERGE *
closemerge(m)
MERGE   *m;
{
  size_t        i;
  PILE          *p;

  if (m == MERGEPN) goto done;

  if (m->heap != FHEAPPN)
    {
      /* Just walk along the heap data in any order, with fheap_elem();
       * no need for overhead of fheap_deletetop():
       */
      for (i = 0; i < fheap_num(m->heap); i++)
        {
          p = (PILE *)fheap_elem(m->heap, i);
          closepile(p);
        }
      closefheap(m->heap);
    }
  m->defrag = TXfree(m->defrag);
  closepile(m->memp);
  closepile(m->outp);
  m->intermsg = TXfree(m->intermsg);
  m->finalmsg = TXfree(m->finalmsg);
  m = TXfree(m);

done:
  return(MERGEPN);
}

int
merge_setmeter(m, intermsg, finalmsg, prevmeter, type, out, flush, usr)
MERGE           *m;
char            *intermsg, *finalmsg;
METER           *prevmeter;
TXMDT           type;
MDOUTFUNC       *out;
MDFLUSHFUNC     *flush;
void            *usr;
/* Sets progress meter(s) for intermediate and final merges.  If
 * `intermsg' and/or `finalmsg' is non-NULL, meter will be created for
 * intermediate and/or final merges.  If `prevmeter' is non-NULL,
 * meters will be child of it, and use its type/out/flush/usr fields;
 * otherwise given type/out/flush/usr is used.  Should be called
 * before any merge_newitem() calls (i.e. before potential merge).
 */
{
  static CONST char     fn[] = "merge_setmeter";

  m->intermsg = TXfree(m->intermsg);
  m->finalmsg = TXfree(m->finalmsg);
  m->prevmeter = METERPN;
  m->metertype = (TXMDT)0;
  m->meterout = MDOUTFUNCPN;
  m->meterflush = MDFLUSHFUNCPN;
  m->meterusr = NULL;
  if (intermsg && !(m->intermsg = TXstrdup(TXPMBUFPN, fn, intermsg)))
    return(0);
  if (finalmsg && !(m->finalmsg = TXstrdup(TXPMBUFPN, fn, finalmsg)))
    return(0);
  m->prevmeter = prevmeter;
  m->metertype = type;
  m->meterout = out;
  m->meterflush = flush;
  m->meterusr = usr;
  return(1);
}

#undef MERGE_WTIX
#undef MERGE_METER
#undef FUNC
#define FUNC(a)         a##_cmp
#include "./mergeaux.c"

#define MERGE_METER
#undef FUNC
#define FUNC(a)         a##_cmpmeter
#include "./mergeaux.c"

#define MERGE_WTIX
#undef FUNC
#define FUNC(a)         a##_wtixmeter
#include "./mergeaux.c"

#undef MERGE_METER
#undef FUNC
#define FUNC(a)         a##_wtix
#include "./mergeaux.c"

#undef MERGE_WTIX
#undef MERGE_METER
#undef FUNC

static int
merge_prepandrun(MERGE *m, PILE *finalout, EPI_HUGEINT outMergeAddItems)
/* Preps and runs a merge pass.  If `finalout' is non-zero, also flips
 * and adds any intermediate piles, and merges to `finalout' instead
 * of intermediate pile.  If `finalout', `outMergeAddItems' is a count
 * of additional items that pile_put(finalout, ...) will add in (Bug
 * 7019; e.g. org index), but that are not in source pile item counts;
 * added to meter to show progress (via pile_out() and/or
 * finalout->mergeFinished() calls to merge_incdone()).
 */
{
  int           ret;
  int           (*onepassfunc)(MERGE *m, PILE *out);
  char          *msg;
  size_t        mem = 0, add = 0, inter = 0;
  EPI_HUGEINT   totalItemsBeforeAddItems;
  TXRESOURCESTATS       org, res;
  size_t        memsz[2];
  char          tmp[3][128];

  if (m->memp != PILEPN && !pile_flip(m->memp)) goto err;

  if (FdbiTraceIdx >= 2)
    {
      if (m->memp != PILEPN) mem = pile_npiles(m->memp);
      add = fheap_num(m->heap);
      if (m->outp != PILEPN) inter = pile_npiles(m->outp);
    }

  if (m->memp != PILEPN)                        /* add mem piles to heap */
    {
      ret = merge_addpile(m, m->memp);
      m->memp = PILEPN;                         /* heap now owns memp */
      if (!ret) goto err;
    }
  m->defrag = TXfree(m->defrag);                /* free "low" mem for outp */

  if (finalout != PILEPN)                       /* final merge */
    {
      if (m->outp != PILEPN)                    /* merge inter. piles too */
        {
          if (!pile_flip(m->outp)) goto err;
          ret = merge_addpile(m, m->outp);
          m->outp = PILEPN;
          if (!ret) goto err;
        }
      msg = m->finalmsg;
    }
  else                                          /* intermediate merge */
    {
      if (m->outp == PILEPN)
        {                                       /* v-- wtf what buf size? */
          m->outp = m->interopen(PILEF_WRITE, 0, m->usr);
          if (m->outp == PILEPN) goto err;
        }
      else if (pile_next(m->outp) == PILEPN)
        goto err;
      msg = m->intermsg;
      if (outMergeAddItems)
        {
          putmsg(MWARN + UGE, __FUNCTION__,
    "Internal warning: Non-zero outMergeAddItems ignored in non-final merge");
          outMergeAddItems = 0;                 /* only valid if `finalout' */
        }
    }
  /* Bug 7019: pile_put(finalout, ...) (e.g. bmpile_putupdate[slurp]())
   * may output more items (e.g. from original index) that are not yet
   * counted in `m->totalitems'; add in to scale meter properly:
   */
  totalItemsBeforeAddItems = m->totalitems;
  m->totalitems += outMergeAddItems;
  if (FdbiTraceIdx >= 2)
    {
      if (!TXgetmeminfo(memsz)) memsz[0] = memsz[1] = (size_t)0;
      putmsg(200, CHARPN,                       /* heap total may differ */
             "%s%s: %kwd mem + %kwd additional %s%kwd intermediate%s = %kwd piles  %kwd merge + %kwd outMergeAdd = %kwd items  vsz: %sB rss: %sB"
#ifdef EPI_TRACK_MEM
             " memCurTotAlloced: %sB"
#endif /* EPI_TRACK_MEM */
             , __FUNCTION__,
             (finalout ? " final" : " intermediate"), (EPI_HUGEINT)mem,
             (EPI_HUGEINT)add, (finalout ? "+ " : "("), (EPI_HUGEINT)inter,
             (finalout ? "" : " not merging yet)"),
             (EPI_HUGEINT)fheap_num(m->heap),
             totalItemsBeforeAddItems, outMergeAddItems,
             (EPI_HUGEINT)m->totalitems,
             TXprkilo(tmp[0], sizeof(tmp[0]), (EPI_HUGEUINT)memsz[0]),
             TXprkilo(tmp[1], sizeof(tmp[1]), (EPI_HUGEUINT)memsz[1])
#ifdef EPI_TRACK_MEM
             , TXprkilo(tmp[2], sizeof(tmp[2]),
                        ((EPI_HUGEUINT)(TXmemCurTotalAlloced < 0 ? 0 :
                                        TXmemCurTotalAlloced) &
                         /* round to KB for terseness: */
                         ~(EPI_HUGEUINT)0x3ff))
#endif /* EPI_TRACK_MEM */
             );
    }

  m->meter = closemeter(m->meter);              /* in case left over */
  if (msg != CHARPN &&                          /* open requested meter */
      (m->meter = (m->prevmeter != METERPN ?
                   opensecondmeter(m->prevmeter, msg, m->totalitems) :
                   openmeter(msg, m->metertype, m->meterout, m->meterflush,
                             m->meterusr, m->totalitems))) != METERPN)
    onepassfunc = (m->cmp == MERGECMP_WTIX ? merge_onepass_wtixmeter :
                   merge_onepass_cmpmeter);
  else
    onepassfunc = (m->cmp == MERGECMP_WTIX ? merge_onepass_wtix :
                   merge_onepass_cmp);
  if (FdbiTraceIdx >= 3) TXgetResourceStats(TXPMBUFPN, TXRUSAGE_SELF, &org);
  ret = onepassfunc(m, (finalout != PILEPN ? finalout : m->outp));
  if (FdbiTraceIdx >= 3)
    {
      if (TXgetResourceStats(TXPMBUFPN, TXRUSAGE_SELF, &res))
        {
          res.values[TXRESOURCESTAT_UserTime] -=
            org.values[TXRESOURCESTAT_UserTime];
          res.values[TXRESOURCESTAT_SystemTime] -=
            org.values[TXRESOURCESTAT_SystemTime];
          res.values[TXRESOURCESTAT_RealTime] -=
            org.values[TXRESOURCESTAT_RealTime];
        }
      if (!TXgetmeminfo(memsz)) memsz[0] = memsz[1] = (size_t)0;
      putmsg(MINFO, CHARPN,
   "merge time%s: user: %1.3lfs sys: %1.3lfs real: %1.3lfs vsz: %sB rss: %sB"
#ifdef EPI_TRACK_MEM
             " memCurTotAlloced: %sB"
#endif /* EPI_TRACK_MEM */
             ,
             (finalout != PILEPN ? " final" : " intermediate"),
             res.values[TXRESOURCESTAT_UserTime],
             res.values[TXRESOURCESTAT_SystemTime],
             res.values[TXRESOURCESTAT_RealTime],
             TXprkilo(tmp[0], sizeof(tmp[0]), (EPI_HUGEUINT)memsz[0]),
             TXprkilo(tmp[1], sizeof(tmp[1]), (EPI_HUGEUINT)memsz[1])
#ifdef EPI_TRACK_MEM
             , TXprkilo(tmp[2], sizeof(tmp[2]),
                        ((EPI_HUGEUINT)(TXmemCurTotalAlloced < 0 ? 0 :
                                        TXmemCurTotalAlloced) &
                         /* round to KB for terseness: */
                         ~(EPI_HUGEUINT)0x3ff))
#endif /* EPI_TRACK_MEM */
             );
    }
  goto done;

err:
  ret = 0;
done:
  m->meter = closemeter(m->meter);
  return(ret);
}

int
merge_incdone(m, nitems)
MERGE   *m;
EPI_HUGEINT nitems;
/* Increments merge's done count by `nitems'.  Only useful while in
 * merge_onepass...().  Called by bmpile_putslurp() to update meter count.
 * Returns 0 on error (should probably ignore, to continue merge).
 */
{
  static int    calledWithNoMeter = 0;

  m->doneitems += nitems;
  /* Normally we are called at most once per merge put-item call, so
   * meter will be updated promptly once we return to merge_onepass().
   * But just in case we get called multiple times per put, update
   * meter here too:
   */
  if (m->meter)
    METER_UPDATEDONE(m->meter, m->doneitems);
  else                                          /* should not happen? */
    {
      if (!calledWithNoMeter)
        {
          calledWithNoMeter = 1;
          putmsg(MWARN + UGE, __FUNCTION__,
                 "Internal warning: Missing meter");
        }
      return(0);
    }
  return(1);
}

int
merge_addpile(m, p)
MERGE   *m;
PILE    *p;
/* Adds piles in `p' to heap of input piles to merge with `m'.  Pile
 * must be original (e.g. openpile()-returned), in read mode, and no
 * pile_next() calls made yet.  `m' will take ownership of pile and will
 * close when done/merged, even on error.  Returns 0 on error.
 * Can be called anytime before merge_finish().  Public use optional:
 * only if pre-existing piles to be merged (e.g. beside merge_newpile() ones).
 */
{
  PILE          *org;
  int           closeorg, ret, goterr;

  org = p;
  closeorg = 1;
  goterr = 0;
  if (!fheap_alloc(m->heap, fheap_num(m->heap) + pile_npiles(org)))
    goto err;
  switch (pile_get(org))
    {
    case 1:                                     /* 1st pile is non-empty */
      if (!fheap_insert(m->heap, org)) goto err;
      closeorg = 0;                             /* `org' now owned by heap */
      m->totalitems += pile_nitems(org);
      break;
    case 0:                                     /* EOF */
      break;
    default:                                    /* error */
      goterr = 1;
      break;
    }
  for (p = pile_next(org); p != PILEPN; p = pile_next(org))
    {
      if (goterr) closepile(p);                 /* previous error; close all*/
      else switch (pile_get(p))
        {
        case 1:                                 /* pile has data */
          if (!fheap_insert(m->heap, p))
            {
              closepile(p);
              goto err;
            }
          m->totalitems += pile_nitems(p);
          break;
        default:                                /* error */
          goterr = 1;
          /* fall through */
        case 0:                                 /* EOF */
          closepile(p);                         /* close error/empty pile */
          break;
        }
    }
  if (goterr) goto err;
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  if (closeorg) closepile(org);
  return(ret);
}

size_t
TXmergeGetMemUsed(m)
MERGE   *m;
/* Returns amount of memory currently used.
 */
{
  return(m->memp ? TXmpileGetMemUsed((MPILE *)m->memp) : 0);
}

int
merge_newpile(m)
MERGE   *m;
/* Starts new (1st-level) in-memory pile for merge_newitem().  Must be
 * called before each pile created.  Optional if merge_newitem() not used
 * (e.g. all merge_addpile() piles).  Returns 0 on error.
 */
{
  static CONST char     fn[] = "merge_newpile";
  int                   ret;

  if (m->memp == PILEPN)                        /* first pile */
    {
      /* Reserve a chunk of "low" heap memory now, and free it before
       * the merge, so that intermediate merge pile objects are malloc'd
       * in it, hopefully de-fragmenting memory:
       */
      if (m->defrag == NULL && TxMergeDefragSz > 0)
        m->defrag = TXmalloc(TXPMBUFPN, fn, TxMergeDefragSz);
      if ((m->memp = (PILE *)openmpile(PILEF_WRITE, m->memsz, NULL)) == PILEPN)
        goto err;
    }
  else
    {
      if (!pile_next(m->memp)) goto err;
    }
  ret = 1;
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

int
merge_newitem(m, blk, sz)
MERGE   *m;
byte    *blk;
size_t  sz;
/* Creates new item with data `blk', size `sz' and adds to current
 * pile of merge.  Assumes items are added in ascending order,
 * according to `cmp' function given at open.  Returns 0 on error.
 */
{
  PILE  p;
  int   res;

  p.blk = blk;
  p.blksz = sz;
  res = pile_put(m->memp, &p);
  if (res == 2 ||                               /* pile says time to merge */
      /* KNG 20111203: Also trigger a merge if we run out of memory,
       * hopefully from benign fdbi_allocbuf() over-alloc:
       */
      (res > 0 && TXmemGetNumAllocFailures() != m->prevNumAllocFailures))
    {
      m->prevNumAllocFailures = TXmemGetNumAllocFailures();
      /* Merge if we have an intermediate pile type, and at least 2
       * piles: number in heap, completed in mem, plus 1 for current
       * mem write pile.  See also merge_finish():
       */
      if (m->interopen != PILEOPENFUNCPN &&
          fheap_num(m->heap) + pile_npiles(m->memp) > 0)
        {
          if (!merge_prepandrun(m, NULL, 0)) goto err;
          return(merge_newpile(m));
        }
      return(1);
    }
  else if (res == 1)                            /* ok */
    return(1);

err:
  return(0);                                    /* error */
}

int
merge_finish(MERGE *m, PILE *out, EPI_HUGEINT outMergeAddItems)
/* Called after last document is finished; does final merge to `out'.
 * `outMergeAddItems' is a count of additional items that will be
 * merged (and merge_incdone()'d) by pile_put(out, ...); Bug 7019.
 * Returns 0 on error.
 */
{
  /* The final merge will probably consist of a few intermediate piles
   * and lots of mem piles, creating a large heap.  If we merge the
   * mem piles first to an intermediate pile (i.e. as if we reached
   * indexmem), then the final heap will be very small (just
   * intermediate piles).  Net time savings comes from pulling
   * intermediate data through a small heap instead of a large one.
   * (But it's a waste of time if if no intermediate piles exist yet.)
   * In practice, doesn't save much time, since indexslurp is already
   * saving so much during final merge anyway; but maybe indexslurp
   * can't be used.   KNG 020115
   */
  if (TxMergeFlush && m->interopen != PILEOPENFUNCPN &&
      m->outp != PILEPN && fheap_num(m->heap) + pile_npiles(m->memp) > 0)
    {
      if (!merge_prepandrun(m, NULL, 0)) return(0);
    }
  return(merge_prepandrun(m, out, outMergeAddItems));   /* final merge */
}

/* ------------------------------------------------------------------------- */

#ifdef TEST
static int      OutTot = 0;
static char     OutLast[1024];

static int mcmp ARGS((PILE *a, PILE *b, void *usr));
static int
mcmp(a, b, usr)
PILE    *a, *b;
void    *usr;
{
  return(strcmp((char *)a->blk, (char *)b->blk));
}

static int mput ARGS((PILE *p, PILE *src));
static int
mput(p, src)
PILE    *p, *src;
{
  printf("%.*s\n", (int)src->blksz, (char *)src->blk);
  if (strlen((char *)src->blk) + 1 != src->blksz)
    printf("Error: block size mismatch\n");
  if (OutTot++ > 0)
    {
      if (strcmp((char *)src->blk, OutLast) < 0)
        printf("Error: next block \"%s\" < previous \"%s\"\n",
               (char *)src->blk, OutLast);
    }
  strncmp(OutLast, (char *)src->blk, sizeof(OutLast));
  return(1);
}

static size_t mnpiles ARGS((PILE *p));
static size_t
mnpiles(p)
PILE    *p;
{
  return((size_t)0);
}

static PILEFUNCS        MFuncs =
{
  NULL,
  NULL,
  mput,
  NULL,
  NULL,
  NULL,
  mnpiles
};

static int scmp ARGS((CONST void *a, CONST void *b));
static int
scmp(a, b)
CONST void      *a, *b;
{
  return(strcmp(*(char **)a, *(char **)b));
}

void
main()
{
  static char   fn[] = "main";
  static char   end[] = "end";
  static char   *data[][10] =
  {
    { "this", "is", "a", "test", CHARPN },
    { CHARPN },            /* empty list */
    { "hello", "doctor", "yesterday", "tomorrow",  CHARPN },
#if 0
    { "so", "is", "this", CHARPN },
    { "The", "ramble", "twins", "crab", "liverish", CHARPN },
    { "Scaly", "scorpions", "are", "good", "water", "fish", CHARPN },
    { "yet", "another", "pile", CHARPN },
    { "and", "a", "bonus", "pile", CHARPN },
#endif
    { end },
  };
  MERGE *m = MERGEPN;
  int   p, i, n, tot, ret;
  PILE  pile;

  OutTot = tot = 0;
  m = openmerge(mcmp, NULL, 90, PILEOPENFUNCPN);
  if (m == MERGEPN) goto err;
  for (p = 0; data[p][0] != end; p++)
    {
      if (!merge_newpile(m)) goto err;
      for (n = 0; data[p][n] != CHARPN; n++);
      qsort(data[p], n, sizeof(data[p][0]), scmp);      /* must be in order */
      printf("Pile %d:\n", p);
      for (i = 0; i < n; i++)
        {
          printf("  Adding \"%s\"\n", data[p][i]);
          if (!merge_newitem(m, data[p][i], strlen(data[p][i]) + 1))
            goto err;
          tot++;
        }
    }
  printf("Added %d total items\n", tot);
  pile.blk = BYTEPN;
  pile.blksz = 0;
  pile.funcs = &MFuncs;
  if (!merge_finish(m, &pile)) goto err;                /* merge to user */
  if (tot != OutTot)
    putmsg(MERR, fn, "Expected %d output items, got %d", tot, OutTot);
  ret = 0;
  goto done;

err:
  ret = 1;
done:
  m = closemerge(m);
  exit(ret);
}

#endif  /* TEST */
