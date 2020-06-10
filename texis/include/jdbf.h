#ifndef JDBF_H
#define JDBF_H


typedef struct JDBF_struct JDBF;	/* defined in jdbfi.h */

#define JDBFPN  ((JDBF *)NULL)


JDBF  *jdbf_close ARGS((JDBF *df));
JDBF  *jdbf_open  ARGS((char *filename, int flags));
void   jdbf_stats ARGS((void));
int    jdbf_free  ARGS((JDBF *df, EPI_OFF_T at));
EPI_OFF_T  jdbf_alloc ARGS((JDBF *df, void *buf, size_t n));

EPI_OFF_T  jdbf_beginalloc ARGS((JDBF *df));
int        jdbf_contalloc ARGS((JDBF *df, byte *buf, size_t sz));
size_t     jdbf_undoalloc ARGS((JDBF *df, byte **bufp));
EPI_OFF_T  jdbf_endalloc ARGS((JDBF *df, size_t *szp));

EPI_OFF_T  jdbf_put   ARGS((JDBF *df, EPI_OFF_T at, void *buf, size_t sz));
void  *jdbf_get   ARGS((JDBF *df, EPI_OFF_T at, size_t *psz));
void  *jdbf_aget  ARGS((JDBF *df, EPI_OFF_T at, size_t *psz));
size_t jdbf_readchunk ARGS((JDBF *df, EPI_OFF_T at, byte *buf, size_t sz));
size_t jdbf_nextblock ARGS((JDBF *df, EPI_OFF_T *at, byte **buf, size_t *bsz,
                            byte **data, EPI_OFF_T *dat, size_t *dtot));
size_t jdbf_read ARGS((JDBF *df, EPI_OFF_T at, size_t *off, void *buf, size_t sz));
EPI_OFF_T  jdbf_tell  ARGS((JDBF *df));
char  *jdbf_getfn ARGS((JDBF *df));
int    jdbf_getfh ARGS((JDBF *df));
void   jdbf_setoveralloc ARGS((JDBF *df, int ov));
int    jdbf_valid ARGS((JDBF *df, EPI_OFF_T at));
int    jdbf_ioctl ARGS((JDBF *df, int ioctl, void *data));
int    jdbf_flush ARGS((JDBF *df));
char *jdbf_proff_t ARGS((EPI_OFF_T at));

#endif /* !JDBF_H */
