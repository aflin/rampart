#ifndef PILE_H
#define PILE_H


/* ---------------------- Generic merge pile prototype --------------------- */

typedef struct PILE_tag PILE;
#define PILEPN          ((PILE *)NULL)

/* Manipulation functions associated with a pile: */
typedef struct PILEFUNCS_tag
{
  PILE *        (*close) ARGS((PILE *p));
  int           (*put) ARGS((PILE *p, PILE *src));
  int           (*get) ARGS((PILE *p));
  PILE *        (*next) ARGS((PILE *p));
  int           (*flip) ARGS((PILE *p));
  size_t        (*npiles) ARGS((PILE *p));
  EPI_HUGEINT   (*nitems) ARGS((PILE *p));
  int           (*mergeFinished)(PILE *pile);
}
PILEFUNCS;
#define PILEFUNCSPN     ((PILEFUNCS *)NULL)

typedef PILE * (PILEOPENFUNC) ARGS((int flags, size_t bufsz, void *usr));
#define PILEOPENFUNCPN  ((PILEOPENFUNC *)NULL)

/* Flags passed to pile open(): */
typedef enum PILEF_tag
{
  PILEF_WRITE   = (1 << 0)      /* write mode */
}
PILEF;

/* Piles used by merge have this at the start of struct, readable by merge: */
struct PILE_tag
{
  byte                  *blk;           /* current item at top of pile */
  size_t                blksz;          /*   its size */
  CONST PILEFUNCS       *funcs;         /* manipulation functions */
};

#define closepile(p)            ((p) != PILEPN ? (p)->funcs->close(p) : PILEPN)
#define pile_put(p, src)        (p)->funcs->put((p), (src))
#define pile_get(p)             (p)->funcs->get(p)
#define pile_next(p)            (p)->funcs->next(p)
#define pile_flip(p)            (p)->funcs->flip(p)
#define pile_npiles(p)          (p)->funcs->npiles(p)
#define pile_nitems(p)          (p)->funcs->nitems(p)
#define pile_mergeFinished(p)   (p)->funcs->mergeFinished(p)

/* ------------------------------ Memory piles ----------------------------- */

typedef struct MPILE_tag MPILE;
#define MPILEPN          ((MPILE *)NULL)
#define MPILEPPN         ((MPILE **)NULL)

PILE    *openmpile ARGS((int flags, size_t bufsz, void *usr));
PILE    *closempile ARGS((PILE *mptr));
int     mpile_put ARGS((PILE *mptr, PILE *src));
int     mpile_get ARGS((PILE *mptr));
PILE    *mpile_next ARGS((PILE *mptr));
int     mpile_flip ARGS((PILE *mptr));
size_t  mpile_npiles ARGS((PILE *mptr));
EPI_HUGEINT mpile_nitems ARGS((PILE *mptr));
size_t  TXmpileGetMemUsed(MPILE *mptr);

#endif  /* !PILE_H */
