#ifndef FBTREE_H
#define FBTREE_H

#include "btreei.h"

#ifndef BT_UNIQUE
#define BT_UNIQUE	(unsigned)0x1
#define BT_FIXED	(unsigned)0x2
#endif

#define TX_FBTREE_PAGESIZE(order)       \
  (sizeof(BPAGE) + (2*(order) - 1)*sizeof(BITEM))
/* derived from TX_FBTREE_PAGESIZE() and the fact that BTREE.count is int: */
#define TX_FBTREE_MAX_ORDER             \
  (((((size_t)EPI_OS_INT_MAX - sizeof(BPAGE))/sizeof(BITEM)) + 1)/2)

BTREE *openfbtree ARGS((char *filename, int order, int cashsize, int flags,
                        int mode, EPI_OFF_T hdroff, DBF *sharedbf));
BTREE *closefbtree ARGS((BTREE *));
int    fbtinsert ARGS((BTREE *, BTLOC, int, void *));
void   fbtprint ARGS((BTREE *));
BTLOC  fbtsearch ARGS((BTREE *, int, void *, BTLOC *));
BTLOC  fbtgetnext ARGS((BTREE *, size_t *, void *, byte **));
BTLOC  fbtgetlast ARGS((BTREE *, size_t *, void *));
int     fbtdelete ARGS((BTREE *, BTLOC, int, void *));
void   fbtsetdefcmp ARGS((BTREE *));
int    fbtappenditeml ARGS((BTREE *t, BPAGE *p, BITEMI *bi, int sp)); /* KNG */
int    TXfbtreeChangeLocOrKey ARGS((BTREE *bt, void *key, int keySize,
                                   BTLOC oldLoc, BTLOC newLoc, byte *newKey));

#endif /* !FBTREE_H */
