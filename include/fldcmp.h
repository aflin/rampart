#ifndef FLDCMP_H
#define FLDCMP_H
/**********************************************************************/
typedef struct FLDCMP_tag {
	FLDOP *fo;
	TBL *tbl1, *tbl2;
}
FLDCMP;
#define FLDCMPPN	((FLDCMP *)NULL)

void	closetmpfo ARGS((void));
int     TXfldCmpSameType(FLD *f1, FLD *f2, int *status, TXOF orderFlags);
int	fldcmp ARGS((void *, size_t, void *, size_t, FLDCMP *));
FLDCMP	*TXclosefldcmp(FLDCMP *fc);
FLDCMP	*TXopenfldcmp(BTREE *bt, FLDOP *fo);
#define TXOPENFLDCMP_CREATE_FLDOP       ((FLDOP *)1)
#define TXOPENFLDCMP_INTERNAL_FLDOP     ((FLDOP *)2)
int	TXsetdontcare ARGS((FLDCMP *, int, int, TXOF orderFlags));
int	TXresetdontcare ARGS((FLDCMP *, int, TXOF orderFlags));
/**********************************************************************/
#endif                                                    /* FLDCMP_H */
