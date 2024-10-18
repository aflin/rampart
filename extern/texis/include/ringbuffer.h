#ifndef RINGBUFFER_H
#define RINGBUFFER_H

typedef void * TXRingBuffer_Item;

typedef struct TXRingBuffer {
  int capacity;
  int cyclesize;
  int cyclelimit;
  TXATOMINT cycles;
  TXATOMINT nread, nwritten;
  TXRingBuffer_Item *buffer;
} TXRingBuffer;

TXRingBuffer *TXRingBuffer_Create(int maxsz);
TXRingBuffer *TXRingBuffer_Destroy(TXRingBuffer *rb);
int TXRingBuffer_Put(TXRingBuffer *rb, TXRingBuffer_Item rbi);
TXRingBuffer_Item TXRingBuffer_Get(TXRingBuffer *rb);
int TXRingBuffer_Used(TXRingBuffer *rb);
int TXRingBuffer_Free(TXRingBuffer *rb);
EPI_OFF_T TXRingBuffer_nread(TXRingBuffer *rb);
EPI_OFF_T TXRingBuffer_nwritten(TXRingBuffer *rb);

#endif /* RINGBUFFER_H */
