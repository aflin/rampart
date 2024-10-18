#include "texint.h"
#include "ringbuffer.h"
#include <stdlib.h>

TXRingBuffer *
TXRingBuffer_Create(int maxsz)
{
  TXRingBuffer *rb = NULL;
  int maxcycles;

  rb = (TXRingBuffer *)calloc(1, sizeof(TXRingBuffer));
  if(rb) {
    rb->nread = rb->nwritten = 0;
    rb->cycles = 0;
    maxcycles = TXATOMINT_MAX / maxsz;
    if(maxcycles < 2) {
      rb = TXfree(rb);
      return rb;
    }
    rb->cyclelimit = maxcycles * maxsz;
    rb->cyclesize = (maxcycles - 1) * maxsz;
    rb->capacity = maxsz;
    rb->buffer = (TXRingBuffer_Item *)calloc(maxsz, sizeof(TXRingBuffer_Item));
    if(!rb->buffer)
      rb = TXRingBuffer_Destroy(rb);
  }
  return rb;
}

TXRingBuffer *
TXRingBuffer_Destroy(TXRingBuffer *rb)
{
  /* WTF: Need to free any items left on the buffer? */
  rb->buffer = TXfree(rb->buffer);
  return TXfree(rb);
}

int
TXRingBuffer_Put(TXRingBuffer *rb, TXRingBuffer_Item rbi)
{
  int offset;

  if(rb->nwritten >= rb->cyclelimit) {
      TX_ATOMIC_INC(&rb->cycles);
      TX_ATOMIC_SUB(&rb->nread, rb->cyclesize);
      TX_ATOMIC_SUB(&rb->nwritten, rb->cyclesize);
  }
  if((rb->nwritten - rb->nread) >= rb->capacity)
    return -1; /* Temporarily Full */

  offset = TX_ATOMIC_INC(&rb->nwritten) % rb->capacity;
  rb->buffer[offset] = rbi;
  return (rb->nwritten - rb->nread);
}

TXRingBuffer_Item
TXRingBuffer_Get(TXRingBuffer *rb)
{
  int offset;

  if(rb->nread >= rb->nwritten)
    return NULL; /* Read all there is */

  offset = TX_ATOMIC_INC(&rb->nread) % rb->capacity;
  return rb->buffer[offset];
}

EPI_OFF_T
TXRingBuffer_nread(TXRingBuffer *rb)
{
  EPI_OFF_T ret;

  ret = (rb->cycles * rb->cyclesize) + rb->nread;
  return ret;
}

EPI_OFF_T
TXRingBuffer_nwritten(TXRingBuffer *rb)
{
  EPI_OFF_T ret;

  ret = (rb->cycles * rb->cyclesize) + rb->nwritten;
  return ret;
}

int
TXRingBuffer_Used(TXRingBuffer *rb)
{
  int used = rb->nwritten - rb->nread;

  if(used > 0) return used;
  return 0;
}

int
TXRingBuffer_Free(TXRingBuffer *rb)
{
  int used = rb->nwritten - rb->nread;

  if(used >= rb->capacity) return 0;
  return rb->capacity - used;
}
