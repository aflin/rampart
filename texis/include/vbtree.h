#ifndef VBTREE_H
#define VBTREE_H

#ifndef BTREEI_H
#  include "btreei.h"
#endif


#ifndef BT_UNIQUE
#define BT_UNIQUE	(unsigned)0x1
#define BT_FIXED	(unsigned)0x2
#define BT_LINEAR       (unsigned)0x8         /* KNG 971016 */
#endif

BTREE *openvbtree ARGS((char *filename, int order, int cashsize, int flags,
                        int mode, EPI_OFF_T hdroff, DBF *sharedbf));
BTREE *closevbtree ARGS((BTREE *));
int    vbtinsert ARGS((BTREE *, BTLOC, int, void *, int));
void   vbtprint ARGS((BTREE *));
BTLOC  vbtsearch ARGS((BTREE *, int, void *, BTLOC *));
BTLOC  vbtgetnext ARGS((BTREE *, size_t *, void *, byte **));
BTLOC  vbtgetlast ARGS((BTREE *, size_t *, void *));
void   vbtupdate ARGS((BTREE *, BTLOC));
int    vbtdelete ARGS((BTREE *, BTLOC, int, void *));
void   vbtsetdefcmp ARGS((BTREE *));
int    vbtappenditeml ARGS((BTREE *t, BPAGE *p, BITEMI *bi, int sp)); /* KNG */
int    TXvbtreeReplaceItem ARGS((BTREE *bt, BPAGE *p, int n, BITEMI *bi));
int    TXvbtreeSetFreespaceAndStacktop(TXPMBUF *pmbuf, BTREE *bt, BPAGE *p,
                                       EPI_OFF_T pageOffset, int fixOther);
int    TXvbtreeChangeLocOrKey ARGS((BTREE *bt, void *key, int keySize,
                                   BTLOC oldLoc, BTLOC newLoc, byte *newKey));

#endif /* !VBTREE_H */
