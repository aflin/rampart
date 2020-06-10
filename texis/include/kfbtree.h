#ifndef KFBTREE_H
#define KFBTREE_H

/* internal kdbf replacements for some fbtree functions.
 */

#include "btreei.h"
#include "kdbf.h"

BTREE *kdbf_openfbtree ARGS((KDBF *kdf, int flags, EPI_OFF_T root));
BTREE *kdbf_closefbtree ARGS((BTREE *));
BTLOC kdbf_fbtgetcurrent ARGS((BTREE *, size_t *, void *));

typedef void (kdbf_item_cb) ARGS((EPI_OFF_T key, BTLOC loc, void *data));

void kdbf_traverse_tree ARGS((BTREE *bt, EPI_OFF_T root, int depth,
			      kdbf_item_cb *cb, void *data));

#if defined(KDBF_PEDANTIC) || defined(KDBF_TEST)
typedef void (kdbf_page_cb) ARGS((EPI_OFF_T at, BPAGE *bp, void *data));

void kdbf_preorder_tree ARGS((BTREE *bt, EPI_OFF_T root, int depth,
			      kdbf_item_cb *icb, void *idata,
			      kdbf_page_cb *pcb, void *pdata));
#endif	/* KDBF_PEDANTIC || KDBF_TEST */

#endif	/* !KFBTREE_H */
