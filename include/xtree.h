#ifndef XTREE_H
#define XTREE_H


#include "unicode.h"                            /* for TXCFF */

/* changed to an 2-3-4 tree structure (Sedgewick) PBR 11-08-91 */
/* PBR 05-08-93 added walktree functions and counter variable in struct */
/* PBR 06-15-93 changed STREE into XTREE. made it binary instead of str */
/**********************************************************************/

/************************************************************************/
#ifndef ARGS
#  ifdef LINT_ARGS
#     define ARGS(a) a
#  else
#     define ARGS(a) ()
#  endif
#endif
/************************************************************************/

#ifndef byte
#define byte unsigned char
#endif

typedef struct XTN_tag  XTN;
#define XTNPN     ((XTN *)NULL)
#define XTNPPN    ((XTN **)NULL)

/* base size of an XTN, ie. without the `s' data: */
#define XTN_BASE_SZ      ((char *)XTNPN->s - (char *)XTNPN)

typedef struct XTREE_tag        XTREE;
#define XTPN  (XTREE *)NULL
#define XTREEPN XTPN

/**********************************************************************/
extern char *xtnoiselst[];

/* An XTREE sequence number (or pointer): */
typedef union XTREESEQ_tag
{
  size_t        num;
  void          *ptr;
}
XTREESEQ;
#define XTREESEQPN      ((XTREESEQ *)NULL)

int    putxtree     ARGS((XTREE *tr,byte  *s,size_t len));
size_t putxtreeseq  ARGS((XTREE *tr,byte  *s,size_t len, XTREESEQ *seq));
int    incxtree     ARGS((XTREE *tr,byte  *s,size_t len));
size_t incxtreeseq  ARGS((XTREE *tr,byte  *s,size_t len, XTREESEQ *seq));
int    writextree   ARGS((XTREE *tr,FILE *fh));
size_t getxtree     ARGS((XTREE *tr, byte *s, size_t len));  /* KNG 960405 */
size_t getxtreeseq  ARGS((XTREE *tr, byte *s, size_t len, XTREESEQ *seq));
void   delxtree     ARGS((XTREE *tr,byte *s,size_t len));
size_t delxtreeseq  ARGS((XTREE *tr,byte *s,size_t len, XTREESEQ *seq));
void   delxtreesl   ARGS((XTREE *tr,byte **sl));
XTREE *closextree   ARGS((XTREE *tr));
XTREE *openxtree    ARGS((TXPMBUF *pmbuf, size_t maxmem));
TXCFF  TXxtreeGetCmpMode ARGS((XTREE *tr));
int    TXxtreeSetCmpMode ARGS((XTREE *tr, TXCFF cmpMode));
int    TXxtreeGetStoreFolded ARGS((XTREE *tr));
int    TXxtreeSetStoreFolded ARGS((XTREE *tr, int storeFolded));
int    TXxtreeGetNumItems ARGS((XTREE *xtree, size_t *numItems));
int    TXxtreeGetNumAllocatedItems ARGS((XTREE *xtree, size_t *numAllocatedItems));
int    TXxtreeGetMemUsed ARGS((XTREE *xtree, size_t *memUsed));
int    TXxtreeSetSequenceMode ARGS((XTREE *xtree, int usePtr));
int    TXxtreeGetMaxDepth ARGS((XTREE *xtree, size_t *maxDepth));
void   zeroxtree    ARGS((XTREE *tr));

#ifdef __BORLANDC__                    /* can't parse func ptrs right */
void   rmwalkxtree  ARGS((XTREE *tr,int (*callback)(),void *usr));
void   walkxtree    ARGS((XTREE *tr,int (*callback)(),void *usr));
#else
void   rmwalkxtree  ARGS((XTREE *tr,int (*callback)(void *userptr, byte *s, size_t len, size_t cnt, XTREESEQ seq), void *usr));
void   walkxtree    ARGS((XTREE *tr,int (*callback)(void *userptr, byte *s, size_t len, size_t cnt, XTREESEQ seq), void *usr));
#endif

int     xtree_walkstart ARGS((XTREE *tr, int clear));   /* KNG 990910 */
int     xtree_walknext ARGS((XTREE *tr, byte **sp, size_t *lenp, size_t *cntp,
                             XTREESEQ *seqp));
void    xtree_walkstop ARGS((XTREE *tr));

/**********************************************************************/
#endif                                                     /* !XTREE_H */


