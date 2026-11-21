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
#include "texint.h"            /* for WTIX_HUGEUINT_CMP */
#include "pile.h"

/* ------------------------------------------------------------------------- */
/*                                Memory piles                               */
/* ------------------------------------------------------------------------- */

/* ------------------------------ Config ----------------------------------- */
#define PILE_SANITY                     /* CYA checks (little perf. cost) */
#define MINPREFCHUNKSZ  (256*1024)      /* minimum preferred chunk size */
#define MAXPREFCHUNKSZ  (4*1024*1024)   /* maximum preferred chunk size */
#define MINCHUNKSZ      (VSH_MAXLEN + sizeof(MPILEPTR))
/* ------------------------------------------------------------------------- */

/* End-of-pile flag.  VSH size must be <= VSH size of size(MPILEPTR): */
#define EOP_VAL         0

typedef struct CHUNK_tag        CHUNK;
struct CHUNK_tag
{
  CHUNK         *next;                  /* next chunk in chain */
  byte          *end;                   /* end of buffer */
  byte          buf[2*sizeof(EPI_HUGEUINT)];/* variable-sized buffer */
};
#define CHUNKPN         ((CHUNK *)NULL)

typedef struct MPILEPTR_tag             /* next pile in list */
{
  CHUNK         *chunk;
  byte          *start;
}
MPILEPTR;
#define MPILEPTRPN      ((MPILEPTR *)NULL)

struct MPILE_tag
{
  PILE          hdr;                    /* public header (must be 1st) */
  size_t        npiles;                 /* number of completed piles */
  CHUNK         *chunklist, *chunk;     /* internal buffer list start/end */
  byte          *chunkcur;              /* cur point in `chunk' */
  size_t        chunksz;                /* size to alloc each chunk */
  byte          *lastchunknext;         /* last next-pile link in chunk list*/
  size_t        memused;                /* mem used (write + future read) */
  size_t        bufsz;                  /* mem limit */
  PILEF         flags;                  /* write mode, etc. */
  MPILE         *org;                   /* original "parent" pile */
  int           refcnt;                 /* (org only) # of MPILEs opened */
  EPI_HUGEINT   nitems;                 /* # of items put to this pile */
};

/* Functions to manipulate MPILE as generic PILE: */
static CONST PILEFUNCS MpileFuncs =
{
  closempile,
  mpile_put,
  mpile_get,
  mpile_next,
  mpile_flip,
  mpile_npiles,
  mpile_nitems,
  NULL                                          /* mergeFinished() */
};

#ifdef EPI_UNALIGN_OFF_T_COPY_SAFE
#  define MPILEPTR_COPY(d, s)  (*(MPILEPTR *)(d) = *(MPILEPTR *)(s))
#else /* !EPI_UNALIGN_OFF_T_COPY_SAFE */
/* gcc needs explicit char * args to avoid optimizing to unaligned copy: */
#  define MPILEPTR_COPY(d, s)  { char *_d = (char *)(d), *_s = (char *)(s); \
  memcpy((void *)_d, (void *)_s, sizeof(MPILEPTR)); }
#endif /* !EPI_UNALIGN_OFF_T_COPY_SAFE */

/* Alignment macro for WTIX_HUGEUINT_CMP optimization.  Wastes ~20% mem
 * to save ~6% CPU (optimal pile size for mm index should be smaller anyway).
 * NOTE: see also fdbim.c, fheap.c/fheapaux.c, merge.c/mergeaux.c:
 */
#ifdef WTIX_HUGEUINT_CMP
#  define ALIGNUP(p)                            \
if ((unsigned)(p) & (sizeof(EPI_HUGEUINT) - 1))     \
  (p) += (sizeof(EPI_HUGEUINT) - ((unsigned)(p) & (sizeof(EPI_HUGEUINT) - 1)))
#endif /* WTIX_HUGEUINT_CMP */

/* ------------------------------------------------------------------------- */

static CHUNK *newchunk ARGS((MPILE *p, size_t sz));
static CHUNK *
newchunk(p, sz)
MPILE   *p;
size_t  sz;
/* Allocates a new chunk, and adds to pile's list.  Returns NULL on error.
 */
{
  static CONST char     fn[] = "newchunk";
  CHUNK                 *chunk;
  size_t                n;

  n = (sizeof(CHUNK) - sizeof(chunk->buf)) + sz;
  if ((chunk = (CHUNK *)TXmalloc(TXPMBUFPN, fn, n)) != CHUNKPN)
    {
      chunk->next = CHUNKPN;
      chunk->end = chunk->buf + sz;
      if (p->chunk != CHUNKPN)                    /* append to list end */
        {
          p->chunk->end = p->chunkcur;              /* wtf realloc() down? */
          p->chunk->next = chunk;
          p->chunk = chunk;
        }
      else                                      /* start new list */
        p->chunk = p->chunklist = chunk;
      p->chunkcur = chunk->buf;
      p->memused += n;
    } 
  return(chunk);
}

PILE *
openmpile(flags, bufsz, usr)
int     flags;
size_t  bufsz;
void    *usr;
/* Opens MPILE.  `bufsz' is buffer (mem) size to use.  `flags' must
 * be PILEF_WRITE; cannot open a read-only memory pile...
 */
{
  static CONST char     fn[] = "openmpile";
  MPILE                 *mp;
  size_t                n;
  CHUNK                 *chunk;
  MPILEPTR              ptr;

  (void)usr;
  if ((mp = (MPILE *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(MPILE))) == MPILEPN)
    goto err;
  mp->hdr.funcs = &MpileFuncs;
  /* rest cleared by calloc(); see also mpile_next(), mpile_flip() */
  mp->flags = (PILEF)flags;
  mp->memused = sizeof(MPILE);
  mp->org = mp;
  mp->refcnt = 1;
#ifdef PILE_SANITY
  if (!(mp->flags & PILEF_WRITE))
    {
      putmsg(MERR + UGE, fn, "Cannot open read-only in-memory pile");
      goto err;
    }
#endif /* PILE_SANITY */
  mp->bufsz = bufsz;
  n = (bufsz >> 5);                             /* guess at a chunk size */
  if (n < MINPREFCHUNKSZ) n = MINPREFCHUNKSZ;
  else if (n > MAXPREFCHUNKSZ) n = MAXPREFCHUNKSZ;
  if (n > bufsz) n = bufsz;
  if (n < MINCHUNKSZ) n = MINCHUNKSZ;           /* absolute min. requirement */
  mp->chunksz = n;
  if ((chunk = newchunk(mp, mp->chunksz)) == CHUNKPN) goto err;
  ptr.chunk = CHUNKPN;
  ptr.start = BYTEPN;                           /* will be set later */
  MPILEPTR_COPY(mp->chunkcur, &ptr);
  mp->lastchunknext = mp->chunkcur;
  mp->chunkcur += sizeof(MPILEPTR);
  goto done;

err:
  mp = (MPILE *)closempile((PILE *)mp);
done:
  return((PILE *)mp);
}

PILE *
closempile(mptr)
PILE    *mptr;
{
  CHUNK *chunk, *next;
  MPILE *mp = (MPILE *)mptr;

  if (mp == MPILEPN) goto done;

  /* If we're in write mode, then the user hasn't seen our subsidiary
   * piles (no flip yet), so close everything:  KNG 000330
   */
  if (mp->flags & PILEF_WRITE) mp->org->refcnt = 1;

  /* Must close the original pile last, so check refcnt first.
   * WTF this is mainly due to a check in mpile_next():
   */
  mp->org->refcnt -= 1;                         /* one less pile */
  if (mp == mp->org &&                          /* if I am the original and */
      mp->refcnt > 0)                           /*   others are still open */
    goto done;                                  /*   then postpone close */

  for (chunk = mp->chunklist; chunk != CHUNKPN; chunk = next)
    {
      next = chunk->next;
      chunk = TXfree(chunk);
    }

  if (mp->org->refcnt <= 0 &&                   /* if I am last one out and */
      mp != mp->org)                            /*   I am not the original */
    closempile((PILE *)mp->org);                /*  then close original too */
  mp = TXfree(mp);                              /* wtf update root memused */

done:
  return(PILEPN);
}

int
mpile_put(mptr, src)
PILE    *mptr, *src;
/* Puts chunk-o-data src->blk, size src->blksz into MPILE `*mptr'.  Returns
 * 0 on error, 1 if ok, 2 if mem limit exceeded (still ok).
 */
{
#ifdef PILE_SANITY
  static CONST char     fn[] = "mpile_put";
#endif /* PILE_SANITY */
  MPILE                 *mp = (MPILE *)mptr;
  CHUNK                 *chunk;
  size_t                chunksz, n, actblksz;
  int                   ret;

#ifdef PILE_SANITY
  if (!(mp->flags & PILEF_WRITE))
    {
      putmsg(MERR + UGE, fn, "Cannot put to read-only pile");
      return(0);
    }
#endif /* PILE_SANITY */
  chunk = mp->chunk;
  ret = 1;
  if ((actblksz = src->blksz) == EOP_VAL)               /* mpile_next flag */
    actblksz = sizeof(MPILEPTR);

  if (VSH_MAXLEN + actblksz
#ifdef WTIX_HUGEUINT_CMP
      + 2*(sizeof(EPI_HUGEUINT) - 1)                        /* align + padding */
#endif /* WTIX_HUGEUINT_CMP */
      <= (size_t)(chunk->end - mp->chunkcur))
    {                                                   /* fits in chunk */
    addit:
      mp->chunkcur = outvsh(mp->chunkcur, (EPI_HUGEUINT)src->blksz);
#ifdef WTIX_HUGEUINT_CMP
      ALIGNUP(mp->chunkcur);
      *(EPI_HUGEUINT *)mp->chunkcur = (EPI_HUGEUINT)0;
#endif /* WTIX_HUGEUINT_CMP */
      memcpy(mp->chunkcur, src->blk, actblksz);
#ifdef WTIX_HUGEUINT_CMP
      if (actblksz < sizeof(EPI_HUGEUINT))
        mp->chunkcur += sizeof(EPI_HUGEUINT);
      else
#endif /* WTIX_HUGEUINT_CMP */
        mp->chunkcur += actblksz;
      mp->nitems++;
    }
  else                                                  /* need new chunk */
    {
      if (mp->memused >= mp->bufsz)                     /* over desired lim. */
        {
          ret = 2;
          chunksz = 0;
        }
      else
        chunksz = mp->bufsz - mp->memused;
      if (chunksz > mp->chunksz) chunksz = mp->chunksz;
      n = VSH_MAXLEN + actblksz
#ifdef WTIX_HUGEUINT_CMP
        + 2*(sizeof(EPI_HUGEUINT) - 1)                      /* align + padding */
#endif /* WTIX_HUGEUINT_CMP */
        + 2*MINCHUNKSZ;
      if (chunksz < n) chunksz = n;
      if ((chunk = newchunk(mp, chunksz)) == CHUNKPN) ret = 0;
      else goto addit;
    }
  return(ret);
}

int
mpile_get(mptr)
PILE    *mptr;
/* Reads next chunk-o-data into mp->blk, mp->blksz.  Returns 1 if ok,
 * 0 on end of pile, -1 on error.
 */
{
  MPILE         *mp = (MPILE *)mptr;
  CHUNK         *chunk;
  EPI_HUGEUINT  sz;

#ifdef PILE_SANITY
  if (mp->flags & PILEF_WRITE)
    {
      putmsg(MERR + UGE, "mpile_get", "Cannot get from write-only pile");
      goto err;
    }
  if (mp->hdr.blksz == (size_t)(-1)) return(0);         /* at pile end (2nd+)*/
#endif /* PILE_SANITY */
  while (mp->chunk != CHUNKPN && mp->chunkcur >= mp->chunk->end)
    {                                                   /* skip mt */
      mp->chunk = mp->chunk->next;
      mp->chunkcur = mp->chunk->buf;
    }
  if ((chunk = mp->chunk) == CHUNKPN) return(0);        /* EOF */
  mp->chunkcur = invsh(mp->chunkcur, &sz);
  if ((size_t)sz == (size_t)EOP_VAL)                    /* end of pile */
    {
      mp->hdr.blk = BYTEPN;
      mp->hdr.blksz = (size_t)(-1);
      return(0);
    }
#ifdef WTIX_HUGEUINT_CMP
  ALIGNUP(mp->chunkcur);
#endif /* WTI_HUGEUINT_CMP */
  mp->hdr.blk = mp->chunkcur;
  mp->hdr.blksz = (size_t)sz;
#ifdef WTIX_HUGEUINT_CMP
  if ((size_t)sz < sizeof(EPI_HUGEUINT))
    mp->chunkcur += sizeof(EPI_HUGEUINT);
  else
#endif /* WTI_HUGEUINT_CMP */
    mp->chunkcur += mp->hdr.blksz;
  return(1);
#ifdef PILE_SANITY
err:
  return(-1);
#endif /* PILE_SANITY */
}

PILE *
mpile_next(mptr)
PILE    *mptr;
/* Switches to next pile in chain.  Returns NULL on error or EOF
 * (read).  `mptr' must be originally-opened MPILE, not a
 * mpile_next()-returned one.  If write mode, returns same handle
 * (eg. new pile; old one cannot be written to any more).  If read
 * mode, returns new handle, and returned handles (and original
 * handle) may be closed in any order.
 */
{
  static CONST char     fn[] = "mpile_next";
  MPILE                 *mp = (MPILE *)mptr, *np = MPILEPN;
  MPILEPTR              ptr;
  PILE                  p;

#ifdef PILE_SANITY
  if (mp->org != mp)    /* wtf just Do The Right Thing: we have mp->org... */
    {
      putmsg(MERR + UGE, fn, "Attempt to clone a cloned pile");
      return(PILEPN);
    }
#endif /* PILE_SANITY */

  if (mp->flags & PILEF_WRITE)                          /* write mode */
    {
      /* Write out an MPILEPTR, flagged with size EOP_VAL so reader
       * will know this is the end of the current pile.  Also link the
       * previous MPILEPTR (mp->lastchunknext) to this new pile.
       * This MPILEPTR stays NULL (EOF) unless/until next pile links it:
       */
      ptr.chunk = CHUNKPN;                              /* EOF till next one*/
      ptr.start = BYTEPN;
      p.blk = (byte *)&ptr;
      p.blksz = EOP_VAL;                                /* flag as EOP */
      if (mpile_put((PILE *)mp, &p))                    /* add EOF ptr */
        {
          ptr.chunk = mp->chunk;                        /* link prev -> this*/
          /* NOTE: subtracting sizeof(MPILEPTR) from chunkcur assumes that
           * sizeof(MPILEPTR) >= sizeof(EPI_HUGEUINT) if WTIX_HUGEUINT_CMP
           * is defined (ie. no padding was added by mpile_put()):
           */
          ptr.start = mp->chunkcur - sizeof(MPILEPTR);    /*   wtf drill */
          MPILEPTR_COPY(mp->lastchunknext, &ptr);
          mp->lastchunknext = ptr.start;
          mp->npiles += 1;
          mp->memused += sizeof(MPILE);                 /* mem used at read */
          mp->nitems--;                                 /* don't count this */
          np = mp;
        }
    }
  else                                                  /* read mode */
    {
      if ((np = (MPILE*)TXcalloc(TXPMBUFPN, fn, 1, sizeof(MPILE))) == MPILEPN)
        goto done;
      np->hdr.funcs = &MpileFuncs;
      np->flags = mp->flags;
      np->org = mp;
      mp->refcnt += 1;                                  /* new pile created */
      /* rest cleared by calloc() */
      MPILEPTR_COPY(&ptr, mp->lastchunknext);
      if (ptr.chunk == CHUNKPN)                         /* EOF */
        {
          np = (MPILE *)closempile((PILE *)np);
          goto done;
        }
      np->chunk = ptr.chunk;
      np->chunkcur = ptr.start + sizeof(MPILEPTR);
      np->npiles = mp->npiles;
      mp->lastchunknext = ptr.start;                    /*for next pile_next*/
      MPILEPTR_COPY(&ptr, mp->lastchunknext);
      if (ptr.chunk == CHUNKPN)                         /* EOF */
        np = (MPILE *)closempile((PILE *)np);
    }
done:
  return((PILE *)np);
}

int
mpile_flip(mptr)
PILE    *mptr;
/* Flips MPILE `*mptr' from write mode to read mode.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "mpile_flip";
  MPILE                 *mp = (MPILE *)mptr;

#ifdef PILE_SANITY
  if (!(mp->flags & PILEF_WRITE))
    {
      putmsg(MERR + UGE, fn, "Cannot flip read-only pile");
      goto err;
    }
#endif /* PILE_SANITY */
  if (mpile_next((PILE *)mp) == PILEPN) goto err;
  mp->chunk = mp->chunklist;
  mp->lastchunknext = mp->chunk->buf;
  mp->chunkcur = mp->chunk->buf + sizeof(MPILEPTR);
  mp->hdr.blk = BYTEPN;
  mp->hdr.blksz = 0;
  mp->flags &= ~PILEF_WRITE;
  return(1);

err:
  return(0);   
}

size_t
mpile_npiles(mptr)
PILE    *mptr;
{
  return(((MPILE *)mptr)->npiles);
}

EPI_HUGEINT
mpile_nitems(mptr)
PILE    *mptr;
{
  return(((MPILE *)mptr)->nitems);
}

size_t
TXmpileGetMemUsed(mptr)
MPILE   *mptr;
/* Returns amount of memory currently used (and needed by future reads).
 */
{
  return(mptr->memused);
}

/* ============================ Accumulator pile =========================== */
/* An in-memory pile for building a Metamorph index from table rows read in
 * token order.  Items are `word nul rest' (see fdbim.c).  Instead of one
 * sorted pile per row -- which must then be merged with every other row's
 * pile through a heap, item by item -- each item's `rest' is appended to a
 * list kept for its word.  Rows come in ascending token order, so each
 * word's list is already in order; when read, only the distinct words
 * need sorting.  Reads back as ONE pile, in the same order the merged
 * per-row piles would give.
 */

#define APILE_BLK0      16              /* data bytes in a word's 1st block */
#define APILE_BLKMAX    4096            /* ... and at most, in later ones */
#define APILE_BLKSZ(n)  ((n) >= 8 ? (size_t)APILE_BLKMAX : ((size_t)APILE_BLK0 << (n)))
#define APILE_ALIGN(n)  (((n) + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1))

typedef struct ACHUNK_tag       ACHUNK;
struct ACHUNK_tag                       /* arena chunk; data follows */
{
  ACHUNK        *next;
};

typedef struct AWORD_tag
{
  byte          *wd;                    /* the word, nul-terminated */
  size_t        wlen;                   /* its length, including the nul */
  byte          *first, *last;          /* its blocks: [next ptr][data] */
  byte          *cur, *end;             /* write position in `last' */
  size_t        used;                   /* data bytes written */
  unsigned      nblk;                   /* number of blocks */
  unsigned      hash;
}
AWORD;

typedef struct APILE_tag
{
  PILE          hdr;                    /* public header (must be 1st) */
  PILEF         flags;
  ACHUNK        *chunks;                /* arena */
  byte          *ccur, *cend;
  size_t        chunksz;
  AWORD         **tab;                  /* hash table (write mode) */
  size_t        tabsz, nwords;
  AWORD         **sorted;               /* words in order (read mode) */
  size_t        sidx;                   /* next word to read */
  AWORD         *rw;                    /* word being read */
  byte          *rblk;                  /*   its current block */
  size_t        rblkoff, rblksz, rleft; /*   position; data bytes left */
  unsigned      rblkn;
  byte          *item;                  /* current item, rebuilt for reader */
  size_t        itemsz;
  size_t        memused, bufsz;
  EPI_HUGEINT   nitems;
}
APILE;

static PILE  *closeapile ARGS((PILE *aptr));
static int    apile_put ARGS((PILE *aptr, PILE *src));
static int    apile_get ARGS((PILE *aptr));
static PILE  *apile_next ARGS((PILE *aptr));
static int    apile_flip ARGS((PILE *aptr));
static size_t apile_npiles ARGS((PILE *aptr));
static EPI_HUGEINT apile_nitems ARGS((PILE *aptr));

static CONST PILEFUNCS ApileFuncs =
{
  closeapile,
  apile_put,
  apile_get,
  apile_next,
  apile_flip,
  apile_npiles,
  apile_nitems,
  NULL                                          /* mergeFinished() */
};

static byte *
apile_alloc(APILE *ap, size_t n)
/* Returns `n' bytes of arena memory (pointer-aligned), or NULL. */
{
  ACHUNK        *c;
  size_t        sz;
  byte          *ret;

  n = APILE_ALIGN(n);
  if ((size_t)(ap->cend - ap->ccur) < n)
    {
      sz = ap->chunksz;
      if (sz < n) sz = n;
      c = (ACHUNK *)TXmalloc(TXPMBUFPN, __FUNCTION__, sizeof(ACHUNK) + sz);
      if (!c) return(BYTEPN);
      c->next = ap->chunks;
      ap->chunks = c;
      ap->ccur = (byte *)(c + 1);
      ap->cend = ap->ccur + sz;
      ap->memused += sizeof(ACHUNK) + sz;
    }
  ret = ap->ccur;
  ap->ccur += n;
  return(ret);
}

static int
apile_write(APILE *ap, AWORD *w, CONST byte *s, size_t n)
/* Appends `n' bytes to word `w's list.  Returns 0 on error. */
{
  size_t        k, sz;
  byte          *b;

  w->used += n;
  while (n > 0)
    {
      if (w->cur == w->end)                     /* block full: add one */
        {
          sz = APILE_BLKSZ(w->nblk);
          if (!(b = apile_alloc(ap, sizeof(byte *) + sz))) return(0);
          *(byte **)b = BYTEPN;
          if (w->last) *(byte **)w->last = b;
          else w->first = b;
          w->last = b;
          w->cur = b + sizeof(byte *);
          w->end = w->cur + sz;
          w->nblk++;
        }
      k = (size_t)(w->end - w->cur);
      if (k > n) k = n;
      memcpy(w->cur, s, k);
      w->cur += k;
      s += k;
      n -= k;
    }
  return(1);
}

static void
apile_read(APILE *ap, byte *d, size_t n)
/* Reads the next `n' bytes of the word being read into `d'. */
{
  size_t        k;

  ap->rleft -= n;
  while (n > 0)
    {
      if (ap->rblkoff == ap->rblksz)            /* next block */
        {
          ap->rblk = *(byte **)ap->rblk;
          ap->rblkn++;
          ap->rblksz = APILE_BLKSZ(ap->rblkn);
          ap->rblkoff = 0;
        }
      k = ap->rblksz - ap->rblkoff;
      if (k > n) k = n;
      memcpy(d, ap->rblk + sizeof(byte *) + ap->rblkoff, k);
      ap->rblkoff += k;
      d += k;
      n -= k;
    }
}

static int
apile_growtab(APILE *ap)
/* Doubles the hash table.  Returns 0 on error. */
{
  AWORD         **nt, *w;
  size_t        nsz = (ap->tabsz ? ap->tabsz << 1 : 4096), i, j;

  nt = (AWORD **)TXcalloc(TXPMBUFPN, __FUNCTION__, nsz, sizeof(AWORD *));
  if (!nt) return(0);
  for (i = 0; i < ap->tabsz; i++)
    if ((w = ap->tab[i]) != NULL)
      {
        for (j = w->hash & (nsz - 1); nt[j]; j = (j + 1) & (nsz - 1));
        nt[j] = w;
      }
  ap->memused += (nsz - ap->tabsz)*sizeof(AWORD *);
  TXfree(ap->tab);
  ap->tab = nt;
  ap->tabsz = nsz;
  return(1);
}

PILE *
openapile(flags, bufsz, usr)
int     flags;
size_t  bufsz;
void    *usr;
/* Opens accumulator pile.  `bufsz' is the memory to use before
 * apile_put() asks for a merge.  `flags' must be PILEF_WRITE.
 */
{
  APILE *ap;
  size_t n;

  (void)usr;
  if (!(ap = (APILE *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(APILE))))
    return(PILEPN);
  ap->hdr.funcs = &ApileFuncs;
  ap->flags = (PILEF)flags;
  ap->bufsz = bufsz;
  ap->memused = sizeof(APILE);
  n = (bufsz >> 5);                             /* chunk size, as MPILE */
  if (n < MINPREFCHUNKSZ) n = MINPREFCHUNKSZ;
  else if (n > MAXPREFCHUNKSZ) n = MAXPREFCHUNKSZ;
  ap->chunksz = n;
  if (!apile_growtab(ap)) return(closeapile((PILE *)ap));
  return((PILE *)ap);
}

static PILE *
closeapile(aptr)
PILE    *aptr;
{
  APILE         *ap = (APILE *)aptr;
  ACHUNK        *c, *next;

  if (!ap) return(PILEPN);
  for (c = ap->chunks; c; c = next)
    {
      next = c->next;
      TXfree(c);
    }
  TXfree(ap->tab);
  TXfree(ap->sorted);
  TXfree(ap->item);
  TXfree(ap);
  return(PILEPN);
}

static int
apile_put(aptr, src)
PILE    *aptr, *src;
/* Adds item `src' (`word nul rest') to the pile.  Returns 0 on error,
 * 1 if ok, 2 if ok and the memory limit is reached.
 */
{
  APILE         *ap = (APILE *)aptr;
  AWORD         *w;
  CONST byte    *s = src->blk, *e;
  size_t        wlen, tlen, i, n;
  unsigned      h = 2166136261u;
  byte          lenbuf[10], *d;

  e = (CONST byte *)memchr(s, '\0', src->blksz);
  if (!e)
    {
      putmsg(MERR, __FUNCTION__, "Internal error: item has no word");
      return(0);
    }
  wlen = (size_t)(e - s) + 1;
  tlen = src->blksz - wlen;
  for (i = 0; i + 1 < wlen; i++) h = (h ^ s[i])*16777619u;      /* FNV-1a */

  for (i = h & (ap->tabsz - 1); (w = ap->tab[i]) != NULL;
       i = (i + 1) & (ap->tabsz - 1))
    if (w->hash == h && w->wlen == wlen && memcmp(w->wd, s, wlen) == 0)
      break;
  if (!w)                                       /* new word */
    {
      if (!(d = apile_alloc(ap, sizeof(AWORD) + wlen))) return(0);
      w = (AWORD *)d;
      memset(w, 0, sizeof(AWORD));
      w->wd = d + sizeof(AWORD);
      memcpy(w->wd, s, wlen);
      w->wlen = wlen;
      w->hash = h;
      ap->tab[i] = w;
      if (++ap->nwords > (ap->tabsz >> 1) && !apile_growtab(ap)) return(0);
    }

  d = lenbuf;                                   /* length of `rest', 7 bits */
  for (n = tlen; n >= 0x80; n >>= 7) *(d++) = (byte)(n | 0x80);  /* a byte */
  *(d++) = (byte)n;
  if (!apile_write(ap, w, lenbuf, (size_t)(d - lenbuf)) ||
      !apile_write(ap, w, s + wlen, tlen))
    return(0);
  ap->nitems++;
  return(ap->memused >= ap->bufsz ? 2 : 1);
}

static int
apile_wordcmp(CONST void *a, CONST void *b)
/* Word order, as the merge compares items (memcmp from the item start;
 * the nul ends a word that is the start of another before it).
 */
{
  CONST AWORD   *wa = *(AWORD * CONST *)a, *wb = *(AWORD * CONST *)b;
  int           c;

  c = memcmp(wa->wd, wb->wd, (wa->wlen < wb->wlen ? wa->wlen : wb->wlen));
  return(c ? c : (wa->wlen < wb->wlen ? -1 : (wa->wlen > wb->wlen ? 1 : 0)));
}

static int
apile_flip(aptr)
PILE    *aptr;
/* Flips pile from write mode to read mode.  Returns 0 on error. */
{
  APILE         *ap = (APILE *)aptr;
  size_t        i, n = 0;

  ap->sorted = (AWORD **)TXcalloc(TXPMBUFPN, __FUNCTION__, ap->nwords + 1,
                                  sizeof(AWORD *));
  if (!ap->sorted) return(0);
  for (i = 0; i < ap->tabsz; i++)
    if (ap->tab[i]) ap->sorted[n++] = ap->tab[i];
  ap->tab = TXfree(ap->tab);
  ap->tabsz = 0;
  qsort(ap->sorted, n, sizeof(AWORD *), apile_wordcmp);
  ap->sidx = 0;
  ap->rw = NULL;
  ap->rleft = 0;
  ap->hdr.blk = BYTEPN;
  ap->hdr.blksz = 0;
  ap->flags &= ~PILEF_WRITE;
  return(1);
}

static int
apile_get(aptr)
PILE    *aptr;
/* Reads next item into hdr.blk/blksz.  Returns 1 if ok, 0 at the end,
 * -1 on error.
 */
{
  APILE         *ap = (APILE *)aptr;
  AWORD         *w;
  size_t        tlen, need;
  int           shift;
  byte          c;

  if (ap->rleft == 0)                           /* next word */
    {
      if (ap->sidx >= ap->nwords)
        {
          ap->hdr.blk = BYTEPN;
          ap->hdr.blksz = 0;
          return(0);
        }
      w = ap->rw = ap->sorted[ap->sidx++];
      ap->rleft = w->used;
      ap->rblk = w->first;
      ap->rblkn = 0;
      ap->rblksz = APILE_BLKSZ(0);
      ap->rblkoff = 0;
    }
  w = ap->rw;
  tlen = 0;
  shift = 0;
  do                                            /* length of `rest' */
    {
      apile_read(ap, &c, 1);
      tlen |= ((size_t)(c & 0x7f) << shift);
      shift += 7;
    }
  while (c & 0x80);
  /* room for the item, and zero padding for an integer compare of
   * its start (WTIX_HUGEUINT_CMP):
   */
  need = w->wlen + tlen + sizeof(EPI_HUGEUINT);
  if (need > ap->itemsz)
    {
      TXfree(ap->item);
      ap->itemsz = need + (need >> 1) + 64;
      ap->item = (byte *)TXmalloc(TXPMBUFPN, __FUNCTION__, ap->itemsz);
      if (!ap->item)
        {
          ap->itemsz = 0;
          return(-1);
        }
    }
  memcpy(ap->item, w->wd, w->wlen);
  apile_read(ap, ap->item + w->wlen, tlen);
  memset(ap->item + w->wlen + tlen, 0, sizeof(EPI_HUGEUINT));
  ap->hdr.blk = ap->item;
  ap->hdr.blksz = w->wlen + tlen;
  return(1);
}

static PILE *
apile_next(aptr)
PILE    *aptr;
/* Write mode (a new table row): the same pile goes on.  Read mode: there
 * is only the one pile, so NULL.
 */
{
  APILE *ap = (APILE *)aptr;

  return((ap->flags & PILEF_WRITE) ? aptr : PILEPN);
}

static size_t
apile_npiles(aptr)
PILE    *aptr;
{
  return(((APILE *)aptr)->nitems > 0 ? 1 : 0);
}

static EPI_HUGEINT
apile_nitems(aptr)
PILE    *aptr;
{
  return(((APILE *)aptr)->nitems);
}

size_t
TXapileGetMemUsed(aptr)
PILE    *aptr;
{
  return(((APILE *)aptr)->memused);
}
