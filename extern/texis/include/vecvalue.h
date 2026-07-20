#ifndef VECVALUE_H
#define VECVALUE_H
#ifdef __cplusplus
extern "C" {
#endif
/* Self-describing chunked-vector VALUES (see vec_value.c).
 * Optional in-band header written by chunkembed() ahead of the cells;
 * every cell reader decodes through TXvecValDecode() so headerless
 * (pre-header / embed()/user-supplied) values behave exactly as
 * before.  Linked into BOTH texisapi and the lean texisbuild. */

#define TXVEC_HDR_FIXED_BYTES 24
#define TXVEC_HDR_F_SPANS     0x01

/* header dtype codes (informational + validation) */
#define TXVEC_HDR_DT_F16  1
#define TXVEC_HDR_DT_BF16 2
#define TXVEC_HDR_DT_F32  3
#define TXVEC_HDR_DT_F64  4
#define TXVEC_HDR_DT_I8   5
#define TXVEC_HDR_DT_U8   6

typedef struct TXvecValInfo
{
    const void        *cells;    /* first cell (== value start if plain) */
    size_t             ncells;   /* total cells (k*dim if header)        */
    size_t             k;        /* chunk count; 0 = no header           */
    size_t             dim;      /* cells per chunk; 0 = no header       */
    int                dtype;    /* TXVEC_HDR_DT_*; 0 = no header        */
    size_t             hdrBytes; /* 0 = no header                        */
    const EPI_UINT32 *spans;    /* k {start,end} pairs or NULL          */
}
TXvecValInfo;

size_t TXvecHdrElsz(int dtypeCode);
size_t TXvecHdrSize(size_t k, int withSpans);
int    TXvecValDecode(const void *data, size_t nbytes, size_t elszHint,
                      TXvecValInfo *out);
int    TXvecValSkipHdrCells(void **cellsPtr, size_t *ncellsPtr,
                            size_t elsz);
size_t TXvecHdrWrite(void *buf, size_t k, size_t dim, int dtypeCode,
                     const EPI_UINT32 *spans);

#ifdef __cplusplus
}
#endif
#endif /* VECVALUE_H */
