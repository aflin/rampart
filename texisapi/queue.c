#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "queue.h"
#include "texint.h"


/******************************************************************/

QUEUE *
closequeue(q)
QUEUE *q;
{
	Q_ITEM *qi = q->head;

	while (qi != (Q_ITEM *)NULL)
	{
		q->head = qi->next;
		free(qi);
		qi = q->head;
	}
	free(q);
	return NULL;
}

/******************************************************************/
/*	Implementation of a queue of BTLOCS.			*/

QUEUE *
openqueue()
{
	return (QUEUE *)calloc(1, sizeof(QUEUE));
}

/******************************************************************/

void
q_put(q, v)
QUEUE *q;
BTLOC v;
{
	Q_ITEM *qi = (Q_ITEM *)calloc(1, sizeof(Q_ITEM));

	qi->v = v;
	qi->next = (Q_ITEM *)NULL;
	qi->prev = q->tail;
	if (qi->prev != (Q_ITEM *)NULL)
		qi->prev->next = qi;
	q->tail = qi;
	if (q->head == (Q_ITEM *)NULL)
		q->head = qi;
}

/******************************************************************/
/*	This is a queue, so FIFO				*/

int
q_get(q, v)
QUEUE *q;
BTLOC *v;
{
	Q_ITEM *qi = q->head;

	if (qi == (Q_ITEM *)NULL)
		return -1;
	*v = qi->v;
	q->head = qi->next;
	free(qi);
	return 0;
}

/* ------------------------------------------------------------------------ */

TXFQUEUE *
TXfqueueOpen(pmbuf, maxItems)
TXPMBUF *pmbuf;         /* (in) buffer for errors */
size_t  maxItems;       /* (in) max items to store in queue */
{
  static CONST char     fn[] = "TXfqueueOpen";
  TXFQUEUE              *fq;
  size_t                n;

  /* We need `fq->items' length to be `maxItems' + 1 to ensure there
   * is always a gap in the array; this lets us distinguish full from
   * empty.  `fq->items' is declared length 1 in the struct, so just
   * add `maxItems':
   */
  if (maxItems <= 0) maxItems = 0;
  fq = (TXFQUEUE *)calloc(1, n = sizeof(TXFQUEUE) +
                          maxItems*sizeof(fq->items[0]));
  if (fq == TXFQUEUEPN)
    {
      TXputmsgOutOfMem(pmbuf, MERR + MAE, fn, 1, n);
      return(TXFQUEUEPN);
    }
  fq->itemsLen = maxItems + 1;
  /* rest cleared by calloc() */
  return(fq);
}

TXFQUEUE *
TXfqueueClose(fq)
TXFQUEUE        *fq;    /* (in/out) queue to close */
{
  if (fq != TXFQUEUEPN) free(fq);
  return(TXFQUEUEPN);
}

