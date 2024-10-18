#ifndef BUFFERDBF_H
#define BUFFERDBF_H

#include "ringbuffer.h"

typedef struct BUFFERDBFBLOCK {
  void  *userdata;
  size_t datasize;
} BUFFERDBFBLOCK;

typedef struct BUFFERDBF {
  DD *dd;
  size_t dd_size;
  TXRingBuffer *RingBuffer;
  char *name; /**< Name for messages etc */
  BUFFERDBFBLOCK *tofree;
  TXPMBUF *pmbuf;
  EPI_OFF_T last_at;
} BUFFERDBF;

#define BUFFERDBFPN     ((BUFFERDBF *)NULL)

BUFFERDBF   *TXRingBufferDbfClose(BUFFERDBF *df);
BUFFERDBF   *TXRingBufferDbfOpen(void);
int          TXRingBufferDbfFree(BUFFERDBF *df, EPI_OFF_T at);
EPI_OFF_T    TXRingBufferDbfAlloc(BUFFERDBF *df, void *buf, size_t n);
EPI_OFF_T    TXRingBufferDbfPut(BUFFERDBF *df, EPI_OFF_T at, void *buf, size_t sz);
int          TXRingBufferDbfBlockIsValid(BUFFERDBF *df, EPI_OFF_T at);
void        *TXRingBufferDbfGet(BUFFERDBF *df, EPI_OFF_T at, size_t *psz);
void        *TXRingBufferDbfAllocGet(BUFFERDBF *df, EPI_OFF_T at, size_t *psz);
size_t       TXRingBufferDbfRead(BUFFERDBF *df, EPI_OFF_T at, size_t *off, void *buf, size_t sz);
EPI_OFF_T    TXRingBufferDbfTell(BUFFERDBF *df);
char        *TXRingBufferDbfGetFilename(BUFFERDBF *df);
int          TXRingBufferDbfGetFileDescriptor(BUFFERDBF *df);
void         TXRingBufferDbfSetOverAlloc(BUFFERDBF *noOpDbf, int ov);
int          TXRingBufferDbfIoctl(BUFFERDBF *noOpDbf, int ioctl, void *data);
int          TXRingBufferDbfSetPmbuf(BUFFERDBF *noOpDbf, TXPMBUF *pmbuf);

int          TXinitRingBufferDbf(DBF *df);

#endif                                                    /* BUFFERDBF_H */
