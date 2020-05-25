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
