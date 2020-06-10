#ifndef BTREE_H
#define BTREE_H

#ifndef BTREEI_H
#  include "btreei.h"
#endif


#define BT_MAXPGSZ	8192
#define BT_REALMAXPGSZ  32000   /* true maximum page size  KNG 971024 */
#if 0
#ifdef __alpha
  #define BT_MAXPGSZ BT_REALMAXPGSZ
#endif
#endif
#define BTFSIZE		250	/* Max number in fixed btree */

#define TX_BTREE_SUFFIX ".btr"

/* NOTE: this is a char field in BTREE struct: */
#define BT_UNIQUE		((unsigned)0x01)
#define BT_FIXED		((unsigned)0x02)
#define BT_UNSIGNED		((unsigned)0x04)
#define BT_LINEAR		((unsigned)0x08)	/* KNG 971016 */
#define BT_LOGOPS		((unsigned)0x10)
#define BT_NOCOMP		((unsigned)0x20)
#define BT_SHAREDBF		((unsigned)0x40)	/* KNG 20041011 */
/* BT_EXCLUSIVEACCESS: caller guarantees that while this B-tree handle is
 * open, it will be the only one accessing the B-tree (for reads OR writes):
 */
#define BT_EXCLUSIVEACCESS	((unsigned)0x80)	/* KNG 20070425 */

#define BT_SAVE_FLAGS   (BT_UNIQUE | BT_FIXED | BT_UNSIGNED | BT_LOGOPS) /* saved to file*/

#define BTREE_IOCTL_LINEAR		(BTREE_IOCTL | 0x0001)/* KNG 971016 */
#define BTREE_IOCTL_EXCLUSIVEACCESS	(BTREE_IOCTL | 0x0002)/*KNG 20070425*/
#define BTREE_MAGIC     0x009A9A00

/******************************************************************/

BTREE_SEARCH_MODE btsetsearch(BTREE *, BTREE_SEARCH_MODE);
int     btdump ARGS((BTREE *t, int flags));

/******************************************************************/

int    existsbtree ARGS((char *));
BTREE *openbtree ARGS((char *, int, int, int, int));
BTREE *closebtree ARGS((BTREE *));
#define btreeisnew(b)   ((b)->root == (off_t)0L)        /* KNG 980402 */

void   btprint ARGS((BTREE *));

char  *btkey2str ARGS((char *buf, size_t bufsz, CONST void *key,
			size_t keysize));

int    btinsert  ARGS((BTREE *, BTLOC *, size_t, void *));
int    btspinsert  ARGS((BTREE *, BTLOC *, size_t, void *, int));
int    btappend ARGS((BTREE *t, BTLOC *loc, int keysize, void *key, int sp,
                      BTBM *bm));                       /* KNG 971016 */
int    btdelete ARGS((BTREE *, BTLOC *, int, void *));

void   btsetdefcmp  ARGS((BTREE *));

BTLOC  btsearch  ARGS((BTREE *, int, void *));
BTLOC  btsearch2 ARGS((BTREE *, int, void *, BTLOC *));
BTLOC  btgetnext ARGS((BTREE *, size_t *, void *, byte **));
BTLOC  btgetlast ARGS((BTREE *, size_t *, void *));
void   btupdate  ARGS((BTREE *, BTLOC));
void   rewindbtree  ARGS((BTREE *));
int    btreesetmarker ARGS((BTREE *bt));
int    btreeunsetmarker ARGS((BTREE *bt));
int    btflush	ARGS((BTREE *));
int    btreinit	ARGS((BTREE *));
int    btreeclear	ARGS((BTREE *));

EPI_OFF_T btpg2off ARGS((BTREE *bt, BPAGE *pg));	     /* KNG 011113 */
void   btcantgetpage ARGS((CONST char *fn, BTREE *bt, EPI_OFF_T off,
                           EPI_OFF_T refoff, int refitem));  /* KNG 011113 */
int	btgetpercentage ARGS((BTREE *));
int    btreesetdd ARGS((BTREE *, DD *));
DD     *btreegetdd ARGS((BTREE *));
#define btsetcmp(bt, cmpf)	((bt)->cmp = (cmpf))

BTHIST  *openbthist ARGS((BTREE *bt));                  /* KNG 971024 */
BTHIST  *closebthist ARGS((BTHIST *bh));                /* KNG 971024 */
BTHIST  *dupbthist ARGS((BTREE *bt));                   /* KNG 980205 */
int     btsethist ARGS((BTHIST *bh));
int     btunsethist ARGS((BTHIST *bh));

/* in case dbstruct.h not included yet: */
#ifndef DBTBLPN
typedef struct DBTBL_tag DBTBL;
#  define DBTBLPN ((DBTBL *)NULL)
#endif /* !DBTBLPN */

FLD   *TXgetNextIndexFldFromTable ARGS((CONST char *indexFields,
                                        DBTBL *dbtbl, CONST char **curField));
int     TXbtreeHasSplitValues ARGS((CONST char *indexFields, DBTBL *dbtbl,
                                    TXindexValues indexValues));
int     TXbtreeIsOnMultipleItemType ARGS((CONST char *indexFields,
                                          DBTBL *dbtbl));
int     TXtextParamsToBtparam(BTPARAM *p, const char *sysindexParams,
                              const char *indexPath, int flags);

int     bttexttoparam(BTREE *btree, char *params);
int     TXbtreeScoreIndex(const char *sysindexFields,
                          const char *sysindexParams,
                          const int *ddicOptions,
                          const char *indexPath, DBTBL *dbtbl,
                          int fldOp, FLD *param, int paramIsRHS);

int    ioctlbtree ARGS((BTREE *t, int ioctl, void *data));    /* KNG 971016 */

int    TXbtreeChangeLocOrKey ARGS((BTREE *bt, void *key, int keySize,
                                   BTLOC oldLoc, BTLOC newLoc, byte *newKey));

#endif  /* !BTREE_H */
