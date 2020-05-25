#ifndef BITFUNCS_H
#define BITFUNCS_H

/* Bit-wise Texis SQL functions */


/* Bitfield type should be same size across platforms for cpdb etc.
 * portability: sizeof(ft_int) == 4 everywhere.  Should also be signed
 * so that non-bitfield args (which we make the same type as bitfield
 * args for code simplicity) can be signed.  We use ft_int instead of
 * ft_byte for speedier code (check 32 instead of 8 bits at a time)
 * and because TX_ALIGN_BYTES is typically 4 anyway, so we are not
 * wasting any additional space in table or compound Metamorph index
 * by using ft_int over ft_byte:
 */
typedef ft_int          bittype;        /* fundamental ft_... type */
#define BITTYPEPN       ((bittype *)NULL)
#define BITTYPE_FTN     FTN_INT         /* FTN_... value for bittype */
#define BITTYPEBITS     32              /* must be preprocessor-safe */
typedef EPI_UINT32      ubittype;       /* unsigned type same size */


int     txfunc_bitand ARGS((FLD *f1, FLD *f2));
int     txfunc_bitor ARGS((FLD *f1, FLD *f2));
int     txfunc_bitxor ARGS((FLD *f1, FLD *f2));

int     txfunc_bitnot ARGS((FLD *f1));
int     txfunc_bitsize ARGS((FLD *f1));
int     txfunc_bitcount ARGS((FLD *f1));
int     txfunc_bitmin ARGS((FLD *f1));
int     txfunc_bitmax ARGS((FLD *f1));
int     txfunc_bitlist ARGS((FLD *f1));

int     txfunc_bitshiftleft ARGS((FLD *f1, FLD *f2));
int     txfunc_bitshiftright ARGS((FLD *f1, FLD *f2));
int     txfunc_bitrotateleft ARGS((FLD *f1, FLD *f2));
int     txfunc_bitrotateright ARGS((FLD *f1, FLD *f2));
int     txfunc_bitset ARGS((FLD *f1, FLD *f2));
int     txfunc_bitclear ARGS((FLD *f1, FLD *f2));
int     txfunc_bitisset ARGS((FLD *f1, FLD *f2));

#endif /* !BITFUNCS_H */
