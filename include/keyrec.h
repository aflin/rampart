/* -=- kai-mode: John -=- */
#ifndef KEYREC_H
#define KEYREC_H

/******************************************************************/

typedef struct KEYREC_tag {
	int	increment;
	int	sorted;
	long	flags;
	size_t	alloced;
	size_t	used;				/* # `items' in use */
	size_t  cloc;		/* current `items' index, when reading */
	size_t	keysz;				/* aligned */
	char	*fname;
	/* `items' is an array of items.  Each item is of the form:
	 *
	 *   recid rank        key        extra
	 *         <- ranksz -><- keysz -><- extrasz ->
	 *         <------------ totalsz ------------->
	 *         ^--- TX_KEYREC_ITEM_DATA()
	 *   ^--------- TX_KEYREC_ITEM_BTLOC()
	 *
	 * recid is sizeof(RECID) aligned up to TX_ALIGN_BYTES
	 * rank is assumed to be sizeof(EPI_OFF_T); wtf not always?
	 * `keysz' is aligned up to TX_ALIGN_BYTES
	 * `extra' is *computed*-from-aux (as opposed to *actual* aux) data,
	 *   e.g. for `ORDER BY aux-expr' but `aux-expr' not SELECTed
	 */
	byte	*items;
	TXqsort_rCmpFunc	*cmpf;
	FLDCMP	*fldcmp;			/* for sorting; not owned */
	byte	*auxitems;
	size_t	auxalloced;			/* # elements in `cmporder' */
	size_t	auxused;
	size_t	auxcloc;
	size_t	auxkeysz;
	DD	*dd;				/* schema for data */
	/* `cmporder' is array of `auxalloced' ints, giving indices of
	 * fields to sort, in order, during keyrecsort().  Indices are
	 * internal-DD/stored not user field order:
	 */
	int	*cmporder;
	size_t  extrasz;
	size_t  ranksz;
	size_t  totalsz;			/* key size: item w/o recid */
	FHEAP	*toprows;
	int	maxsortrows;
} KEYREC;
#define KEYRECPN        ((KEYREC *)NULL)

#define TX_KEYREC_ITEM_BTLOC_OFFSET	0
/* Data offset; sizeof(RECID) aligned up to TX_ALIGN_BYTES: */
#define TX_KEYREC_ITEM_DATA_OFFSET	FDBI_TOKEL_RECIDSZ
/* Size of a keyrec item: */
#define TX_KEYREC_ITEM_SZ(k)	((k)->totalsz + TX_KEYREC_ITEM_DATA_OFFSET)
/* TX_KEYREC_ITEM(k, i) points to `i'th item in `k->items': */
#define TX_KEYREC_ITEM(k, i)	((k)->items + (i)*TX_KEYREC_ITEM_SZ(k))
/* TX_KEYREC_ITEM_BTLOC(k, i) points to `i'th item's BTLOC: */
#define TX_KEYREC_ITEM_BTLOC(k, i)	\
	(TX_KEYREC_ITEM((k), (i)) + TX_KEYREC_ITEM_BTLOC_OFFSET)
/* TX_KEYREC_ITEM_DATA(k, i) points to `i'th item's data (rank+key+extra): */
#define TX_KEYREC_ITEM_DATA(k, i)	\
	(TX_KEYREC_ITEM((k), (i)) + TX_KEYREC_ITEM_DATA_OFFSET)
/* TX_KEYREC_ITEM_INDEX(k, item) returns `item' array index: */
#define TX_KEYREC_ITEM_INDEX(k, item)	\
	(((byte *)(item) - (k)->items)/TX_KEYREC_ITEM_SZ(k))

/******************************************************************/
int	TXkeyrecSetCmpRankDescRecid(KEYREC *keyrec);

BTLOC keyrecgetnext ARGS((KEYREC *, void *, size_t *));
int keyrecsort ARGS((KEYREC *));
int keyrecrewind ARGS((KEYREC *));
char *TXkeyrecTupleToStr(KEYREC *keyrec, size_t itemIdx);
int TXkeyrecDump(TXPMBUF *pmbuf, KEYREC *keyrec, int indent);
KEYREC *keyrecclose ARGS((KEYREC *));
KEYREC *keyrecopen(char *fname, size_t keysz, size_t ranksize, size_t extrasz,
		   long flags, int inc, DD *dd);
int keyrecappend ARGS((KEYREC *, BTLOC *, void *, EPI_OFF_T, void *));
int keyrecsetfldcmp(KEYREC *, FLDCMP *);
const char *TXkeyrecCmpFuncToStr(KEYREC *keyrec);
int keyrecsetmaxsortrows(KEYREC *, int);
#endif /* KEYREC_H */
