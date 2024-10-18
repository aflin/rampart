#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "texint.h"     /* for WTIX_HUGEUINT_CMP */
#include "pile.h"       /* for WTIX inline versions */
#include "heap.h"

/* Code for TXfheapDeleteTopWtix(), TXfheapInsertWtix().
 */

/* NOTE: see also merge.c/mergeaux.c, fdbim.c: */
#undef CMP
#undef FUNC
#ifdef WTIX_HUGEUINT_CMP
#  define CMP(fh, a, b, usr)                                                \
  (*(EPI_HUGEUINT *)((PILE *)(a))->blk < *(EPI_HUGEUINT *)((PILE *)(b))->blk ? -1 : \
   (*(EPI_HUGEUINT *)((PILE *)(a))->blk > *(EPI_HUGEUINT *)((PILE *)(b))->blk ? 1 : \
    memcmp(((PILE *)(a))->blk, ((PILE *)(b))->blk,                          \
           (((PILE *)(a))->blksz < ((PILE *)(b))->blksz ?                   \
            ((PILE *)(a))->blksz : ((PILE *)(b))->blksz))))
#else /* !WTIX_HUGEUINT_CMP */
#  define CMP(fh, a, b, usr)  memcmp(((PILE *)(a))->blk, ((PILE *)(b))->blk, \
                        (((PILE *)(a))->blksz < ((PILE *)(b))->blksz ?       \
                         ((PILE *)(a))->blksz : ((PILE *)(b))->blksz))
#endif /* !WTIX_HUGEUINT_CMP */
#define FUNC(a) a##Wtix
#include "fheapaux.c"
