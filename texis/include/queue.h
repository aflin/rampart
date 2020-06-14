#ifndef QUEUE_H
#define QUEUE_H

typedef struct tagQ_ITEM {
	BTLOC v;
	struct tagQ_ITEM *next;
	struct tagQ_ITEM *prev;
} Q_ITEM ;

typedef struct tagQUEUE {
	Q_ITEM	*head;
	Q_ITEM	*tail;
} QUEUE ;

QUEUE *openqueue ARGS((void));
QUEUE *closequeue ARGS((QUEUE *));
void	q_put ARGS((QUEUE *, BTLOC));
int	q_get ARGS((QUEUE *, BTLOC *));

/* Fast fixed-size queue of pointers: ------------------------------------- */

typedef struct TXFQUEUE_tag
{
  size_t        firstIdx;                       /* first item index */
  size_t        nextInsertIdx;                  /* next item inserted index */
  size_t        itemsLen;                       /* length of `items' */
  void          *items[1];                      /* storage for queue items */
}
TXFQUEUE;
#define TXFQUEUEPN      ((TXFQUEUE *)NULL)

TXFQUEUE *TXfqueueOpen ARGS((TXPMBUF *pmbuf, size_t maxItems));
TXFQUEUE *TXfqueueClose ARGS((TXFQUEUE *fq));
#define TXFQUEUE_IS_EMPTY(fq)   ((fq)->firstIdx == (fq)->nextInsertIdx)
#define TXFQUEUE_IS_FULL(fq)    \
  (((fq)->nextInsertIdx + 1) % (fq)->itemsLen == (fq)->firstIdx)

/* TXFQUEUE_INSERT() only safe if !TXFQUEUE_IS_FULL(): */
#define TXFQUEUE_INSERT(fq, item)               \
  ((fq)->items[(fq)->nextInsertIdx++] = (item), \
   (fq)->nextInsertIdx %= (fq)->itemsLen)

/* These are only safe if !TXFQUEUE_IS_EMPTY(): */
#define TXFQUEUE_FIRST_ITEM(fq) ((fq)->items[(fq)->firstIdx])
#define TXFQUEUE_REMOVE_FIRST_ITEM(fq)  \
  ((fq)->firstIdx = ((fq)->firstIdx + 1) % (fq)->itemsLen)

/* Iteration of queue in order:
 *   for (i = 1stIterBegin; i < 1stIterEnd; i++)
 *   for (i = 2ndIterBegin; i < 2ndIterEnd; i++)
 */
#define TXFQUEUE_1ST_ITER_BEGIN(fq)     ((fq)->firstIdx)
#define TXFQUEUE_1ST_ITER_END(fq)       \
((fq)->nextInsertIdx >= (fq)->firstIdx ? (fq)->nextInsertIdx : (fq)->itemsLen)
#define TXFQUEUE_2ND_ITER_BEGIN(fq)     (0)
#define TXFQUEUE_2ND_ITER_END(fq)       \
  ((fq)->nextInsertIdx >= (fq)->firstIdx ? 0 : (fq)->nextInsertIdx)
#define TXFQUEUE_ITEM(fq, i)    ((fq)->items[i])

/* Number of items in the queue: */
#define TXFQUEUE_LENGTH(fq)                     \
  ((fq)->firstIdx <= (fq)->nextInsertIdx ?      \
   (fq)->nextInsertIdx - (fq)->firstIdx :       \
   ((fq)->nextInsertIdx + (fq)->itemsLen) - (fq)->firstIdx)

/* Clear (empty) the queue: */
#define TXFQUEUE_CLEAR(fq)      ((fq)->firstIdx = (fq)->nextInsertIdx = 0)

#endif /* !QUEUE_H */
