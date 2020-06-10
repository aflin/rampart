#ifndef BIC_H
#define BIC_H

/* Binary Interpolative Coding */



typedef struct BIC_tag  BIC;
#define BICPN   ((BIC *)NULL)

BIC     *closebic ARGS((BIC *bic));
BIC     *openbic ARGS((void));
size_t  bic_encode ARGS((BIC *bic, EPI_HUGEUINT *data, size_t n, byte **bufp,
                         EPI_HUGEUINT lo, EPI_HUGEUINT hi));
size_t  bic_decode ARGS((BIC *bic, EPI_HUGEUINT *data, size_t n, byte *buf,
                         size_t sz, EPI_HUGEUINT lo, EPI_HUGEUINT hi));
size_t  bic_getdata ARGS((BIC *bic, byte **bufp, int own));

#endif /* !BIC_H */
