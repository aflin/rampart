/* -=- kai-mode: john -=- */
#ifndef BTREEI_H
#define BTREEI_H

/*   Should be included elsewhere */
/*#include "dbtable.h" */
/*#include "os.h" */


#if 0
typedef struct btree_locator_struct
BTLOC {
	EPI_OFF_T   loc;
}
BTLOC;
#else
typedef RECID BTLOC;
#endif

/* Pointer to item key for vbtree: */
struct bkl {
	short key;				/* offset from page start */
	short len;				/* length */
};

/* B-tree page item, as stored on disk/DBF: */
typedef struct btree_item_struct
{
	EPI_OFF_T   hpage;			/* B-tree page just > item */
	BTLOC   locn;				/* item offset in .tbl */
	union{
		EPI_OFF_T key;			/* item key (for fbtree) */
		struct bkl var;			/* ""       (for vbtree) */
	} vf;
}
BITEM;

/* B-tree page item, as stored in memory: */
typedef struct btree_varitem_struct
{
	EPI_OFF_T   hpage;
	BTLOC   locn;
	short   key;
	short   len;				/* item key length */
	int	alloced;			/* is `string' alloced */
	char    *string;			/* item key */
}
BITEMI;

/* B-tree page, in mem and stored on disk/DBF.  Byte size is `pagesize'.
 * `order' is (preferred?) max # items for fbtree (half full),
 * or same as `pagesize' for vbtree.
 * Note that when alloc'd in mem, there may be pre/postbufsz bytes
 * before the pointer/after pointer + pagesize, for KDBF: see btmkpage().
 *
 * For vbtree, given BPAGE *pg:
 *
 *             BITEM[0]
 * prebufsz       ||                                                postbufsz
 *  |     +-BPAGE--+                                                 |     |
 *  +-----+-------+-BITEMs array--+----Free space---+---Keys stack---+-----+
 *    pg--^-------------------------------stacktop--^                |
 *        +-------------------------BTREE.pagesize-------------------+
 *
 * BITEMs grow upward as an array (heap-like) from bottom.  Keys are
 * stored in downward-growth, variable-sized-keys, possibly
 * non-contiguous stack at top of page.  BPAGE.freesp counts not only
 * contiguous free space between heap and stack (`Free space' above),
 * but also slack free space (if any) between keys in stack.  Room for
 * at least 1*sizeof(BITEM) is maintained by keeping BPAGE.freesp
 * sizeof(BITEM) less than actual free space: unknown reason, probably
 * failure to subtract sizeof(BPAGE.items) from sizeof(BPAGE) when
 * initializing page?
 *
 * For fbtree, `freesp' and `stacktop' are ignored and there is no keys stack.
 */
#define BPAGEPN ((BPAGE *)NULL)
typedef struct btree_page_struct
{
	int     count;				/* number of items on page */
	short   freesp;				/* free space available */
	unsigned short  stacktop;		/* top of item-key stack */
	EPI_OFF_T   lpage;			/* tree page just < 1st item*/
	BITEM   items[1];			/* array of `count' items */
}
BPAGE;

/* In-memory cached B-tree page: */
typedef struct bcache_record_struct
{
	EPI_OFF_T   pid;			/* DBF offset of page */
	/* Note that `page' has `bt->prebufsz' bytes alloced before the
	 * pointer, and `bt->postbufsz' after the end of the page, for
	 * KDBF ioctl optimizations:
	 */
	BPAGE   *page;
	int     inuse;				/* cached page in use */
	int     dirty;				/* cached page has changed */
	int	lastaccess;
}
BCACHE;

typedef struct btree_record_locator
{
	EPI_OFF_T	page;
	int	index;
}
BTRL;
#define BTRLPN  ((BTRL *)NULL)

/*
	When searching a BTREE we might want to return the match,
	or set a cursor just before, or just after a match, so that
	calls to getnext will return appropriately.
*/
typedef enum BTREE_SEARCH_MODE_tag
{
	BT_SEARCH_UNKNOWN = -666,		/* invalid value */
	BT_SEARCH_AFTER = -1,
	BT_SEARCH_FIND = 0,
	BT_SEARCH_BEFORE = 1
} BTREE_SEARCH_MODE;

/* BTREE_STORED_FIELDS_PREFIX: stored-in-DBF fields common to old+new style.
 * NOTE: Changing these fields' types/sizes/order will corrupt existing files:
 */
#define BTREE_STORED_FIELDS_PREFIX	\
	long	magic;			\
	char    flags;			\
	int     order;			\
	int     npages;			\
	int     pagesize;		\
	int     cachesize;		\
	EPI_OFF_T	root

/* BTREEO: Old-style initial structure of B-tree as stored on disk/DBF.
 * Superceded by BTREES but some old .btr files may still use this.
 * NOTE: Initial fields (BTREE_STORED_FIELDS_PREFIX) MUST agree w/BTREES.
 * NOTE: Changing fields here may corrupt existing B-tree files:
 */
typedef struct BTREEO
{
	BTREE_STORED_FIELDS_PREFIX;
	NDD	datad;
} BTREEO;

typedef enum TXindexValues_tag
{
	TXindexValues_SplitStrlst,		/* split just strlst */
	TXindexValues_All,			/* all values (no split) */
	TXindexValues_NUM			/* must be last */
}
TXindexValues;
#define TXindexValuesPN	((TXindexValues *)NULL)
#define TX_INDEX_VALUES_DEFAULT	TXindexValues_SplitStrlst
/* TX_INDEX_VALUES_DEFAULT_OLD: default/only-value before we added
 * `indexvalues' property and stored it in SYSINDEX.PARAMS; legacy,
 * cam never change:
 */
#define TX_INDEX_VALUES_DEFAULT_OLD	TXindexValues_SplitStrlst

TXindexValues TXstrToIndexValues ARGS((CONST char *s, CONST char *e));
CONST char   *TXindexValuesToStr ARGS((TXindexValues indexValues));

/* Note that params should be "safe" such that data can be safely copied from
 * a RAM to disk B-tree, both with params set; see index.c !NO_USE_MEM_INDEX:
 */
typedef struct BTPARAM
{
	int	max_index_text;
	int	stringcomparemode;		/* TXCFF stringcomparemode */
	TXindexValues	indexValues;		/* how to split multi-values*/
} BTPARAM;

/* BTPARAM_INIT(): init BTPARAM struct `*p' to defaults before
 * SYSINDEX.PARAMS parsed, i.e. as if no .PARAMS.  Note that this may differ
 * from current *process* (i.e. `globalcp') defaults, i.e. these are
 * pre-version-6 defaults:
 */
#define BTPARAM_INIT(p)                                         \
  ((p)->max_index_text = 0,                                     \
   (p)->stringcomparemode = TXCFF_STRINGCOMPAREMODE_DEFAULT_OLD,\
   (p)->indexValues = TX_INDEX_VALUES_DEFAULT)

/* BTPARAM_INIT_TO_PROCESS_DEFAULTS(): init BTPARAM `*p' to `globalcp'/`ddic'
 * defaults.  For RAM B-trees.  After BTPARAM_INIT(), and in lieu of
 * bttexttoparam():
 */
#define BTPARAM_INIT_TO_PROCESS_DEFAULTS(p, ddic)               \
  { if (ddic) (p)->max_index_text =                             \
      (ddic)->options[DDIC_OPTIONS_MAX_INDEX_TEXT];             \
    if (globalcp) (p)->stringcomparemode = globalcp->stringcomparemode; \
    if (TXApp) (p)->indexValues = TXApp->indexValues;		\
  }

#define BTPARAMTEXTSIZE	(40 + TX_TXCFFTOSTR_MAXSZ)

/* BTPARAM_ACTIVATE: make BTPARAM settings active for a B-tree op: */
#define BTPARAM_ACTIVATE(p)	{					\
	TXCFF	saveSCM;						\
	if (globalcp == APICPPN) globalcp = TXopenapicp();		\
	saveSCM = globalcp->stringcomparemode;				\
	globalcp->stringcomparemode = (p)->stringcomparemode;
/* Restore previous: */
#define BTPARAM_DEACTIVATE(p)						\
	globalcp->stringcomparemode = saveSCM; }

/* BTREE_STORED_FIELDS: New-style stored-in-DBF fields.  Same as BTREES.
 * NOTE: Changing these fields' types/sizes/order will corrupt existing files:
 */
#define BTREE_STORED_FIELDS		\
	BTREE_STORED_FIELDS_PREFIX;	\
	EPI_OFF_T	datadoff

/* BTREES: New-style initial structure of B-tree as stored on disk/DBF.
 * NOTE: Initial fields (BTREE_STORED_FIELDS_PREFIX) MUST agree w/BTREEO.
 * NOTE: Changing these fields will corrupt existing B-tree files:
 */
typedef struct btree_stored
{
	BTREE_STORED_FIELDS;
}
BTREES;

typedef int (*btcmptype)ARGS((void *, size_t, void *, size_t, void *));

#define BTREE_DEFAULT_HDROFF	((EPI_OFF_T)0)

/* BTREE: In-memory B-tree struct.
 * NOTE: Initial BTREE_STORED_FIELDS read/written to DBF; MUST be first:
 */
typedef struct btree_struct
{
	BTREE_STORED_FIELDS;			/* must be first */

	DD	*datad;

	int	sdepth;	/* start/current history depth for [fv]btgetnext()? */
	int	cdepth;	/* current search history depth in search()? */
	BTREE_SEARCH_MODE	search_pos;
	btcmptype cmp;
	DBF	*dbf;
	BCACHE	*cache;
	BTRL	*his;
	void	*usr;
	int	iamdirty;
	int	dddirty;
	int	szread;
	int	cacheused;
	int	pagereads;
#ifdef DEVEL
	int	useshm;
#endif
	BTRL	rangeend;
	int	stopatrange;
	long	lcode, hcode;/* for betwixt */
	BTPARAM params;
	EPI_OFF_T	hdroff;		/* offset of header (usually 0) */
	int	prebufsz, postbufsz;		/* for KDBF optimize */
	int	openMode;			/* O_... flags */
	byte	gotError;
	/* `numItemsDelta': change in number of items since open.
	 * Since we do not currently store this in the DBF,
	 * it is a delta-since-open, not an absolute count since create.
	 * Useful for RAM B-trees, where open is always create:
	 */
	EPI_SSIZE_T	numItemsDelta;
}
BTREE;

#define BTREEPN       ((BTREE *)NULL)
#define BTREEPPN      ((BTREE **)NULL)

#define TX_BTREE_SET_ERROR(bt, isOn)	((bt)->gotError = !!(isOn))
/* TX_BTREE_GOT_ERROR(bt): nonzero if a call involving `bt' got an error.
 * In-process only; not saved to file.  Not widely supported yet:
 */
#define TX_BTREE_GOT_ERROR(bt)		(!!(bt)->gotError)

typedef struct BTHIST_tag {
  BTREE *bt;
  BTRL  *his, *ohis;
  int   sdepth, osdepth;
  int   cdepth, ocdepth;
} BTHIST;
#define BTHISTPN        ((BTHIST *)NULL)

typedef struct BTBM_tag {
  BTRL  thisrec;
  BTRL  bookmark;
} BTBM;
#define BTBMPN          ((BTBM *)NULL)

int	btcmp ARGS((BTREE *, BPAGE *, int, void *, short));
int   TXbtreeIsValidPage(TXPMBUF *pmbuf, const char *fn, BTREE *btree,
			 EPI_OFF_T pageOffset, BPAGE *page, int *fixed);
BPAGE	*btgetpage ARGS((BTREE *, EPI_OFF_T));
void	btdirtypage ARGS((BTREE *, EPI_OFF_T));
EPI_OFF_T	btgetnewpage ARGS((BTREE *));
BPAGE  *btreleasepage ARGS((BTREE *t, EPI_OFF_T n, BPAGE *b));
BPAGE  *btfreepage ARGS((BTREE *t, EPI_OFF_T n, BPAGE *b));
void    btinitpage ARGS((BTREE *t, BPAGE *p));          /* KNG 971016 */
int     btreadpage ARGS((BTREE *t, EPI_OFF_T off, BPAGE *p, int *dirtied));
EPI_OFF_T   btwritepage ARGS((BTREE *t, EPI_OFF_T off, BPAGE *p));
BPAGE  *btmkpage ARGS((BTREE *t));                      /* KNG 971016 */
BPAGE  *btfrpage ARGS((BTREE *t, BPAGE *p));

int     btupdatebm ARGS((BTREE *t, BTBM *bm, int keysize, void *key));
int     btflushappend ARGS((BTREE *t));                 /* KNG 971016 */

/* in case dbstruct.h not included yet: */
#ifndef DBTBLPN
typedef struct DBTBL_tag DBTBL;
#  define DBTBLPN ((DBTBL *)NULL)
#endif /* !DBTBLPN */

int     btparamtotext ARGS((BTREE *btree, CONST char *indexFields,
                            DBTBL *dbtbl, int *textsz, char *buffer));

EPI_OFF_T btwritebuf ARGS((BTREE *t, EPI_OFF_T off, void *buf, size_t sz));
int     btsetroot ARGS((BTREE *b));
int     btlogop ARGS((BTREE *bt, int n, void *data, BTLOC *locn,
                      CONST char *op, CONST char *result));
size_t	TXbtreePrFlags(BTREE *bt, char *buf, size_t bufSz);
EPI_SSIZE_T TXbtreeGetNumItemsDelta(BTREE *bt);
char    *TXbtreeTupleToStr(TXPMBUF *pmbuf, BTREE *bt, TBL **scratchTbl,
			   void *recBuf, size_t recBufSz);
int	TXbtreeDump(TXPMBUF *pmbuf, BTREE *bt, int indent,
		    int recidsDecimalToo);
const char *TXbtreeCmpFuncToStr(BTREE *bt);

int     TXbtgetoptimize ARGS((void));
int     TXbtsetoptimize ARGS((int flags, int set));
int     TXbtsetexclusiveioctls ARGS((BTREE *bt, int set));

#define TX_BTREE_NAME(btree)	((btree)->dbf ? getdbffn((btree)->dbf) : "?")

int TXfcmp(void *a, size_t al, void *b, size_t bl, void *usr);
int TXfucmp(void *a, size_t al, void *b, size_t bl, void *usr);
int TXfixedUnsignedReverseCmp(void *a, size_t al, void *b, size_t bl,
                              void *usr);

#endif /* !BTREEI_H */
