/* -=- kai-mode: john -=- */
#include "texint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/************************************************************************/

static char    TxRingBufferDbfName[] = "(Ring Buffer DBF)";


/******************************************************************/
/** Free Old Row
**/

static int
TXRingBufferDbfFreeOld(BUFFERDBF *df)
{
  if(df->tofree) {
    df->tofree->userdata = TXfree(df->tofree->userdata);
    df->tofree = TXfree(df->tofree);
  }
  return 0;
}
/******************************************************************/

BUFFERDBF *
TXRingBufferDbfClose(BUFFERDBF *df)
{
 	if(df!=BUFFERDBFPN) {
    TXRingBufferDbfFreeOld(df);
    df->RingBuffer = TXRingBuffer_Destroy(df->RingBuffer);
    df->name = TXfree(df->name);
    df->dd = TXfree(df->dd);
  	df = TXfree(df);
	}
	return(BUFFERDBFPN);
}

/************************************************************************/

BUFFERDBF *
TXRingBufferDbfOpen()
{
	static CONST char     fn[] = "openrdbf";

	BUFFERDBF *df=(BUFFERDBF *)TXcalloc(TXPMBUFPN, fn, 1,sizeof(BUFFERDBF));
	if(df!=BUFFERDBFPN) {
    df->RingBuffer = TXRingBuffer_Create(TXApp->intSettings[TX_APP_INT_SETTING_RING_BUFFER_SIZE]);
	}
	return(df);
}

/************************************************************************/

int
TXRingBufferDbfFree(BUFFERDBF *df,EPI_OFF_T at)
{
  /* Delete of at = -1 is invalid (Bug 4026), and ringbuffer doesn't support
     random access */
	return -1;
}

/************************************************************************/

EPI_OFF_T
TXRingBufferDbfAppend(BUFFERDBF *df, void *buf, size_t n)
{
  EPI_OFF_T ret = -1;
  BUFFERDBFBLOCK *rb=(BUFFERDBFBLOCK *)TXcalloc(df->pmbuf, __FUNCTION__, 1, sizeof(BUFFERDBFBLOCK));
  if(rb) {
    rb->userdata = buf;
    rb->datasize = n;

    if(TXRingBuffer_Put(df->RingBuffer, rb) == -1) {
      /* Couldn't write to ring buffer, full? */
      TXfree(rb);
    } else {
      ret = TXRingBuffer_nwritten(df->RingBuffer);
    }
  }
  if(ret != -1) {
    df->last_at = ret;
  }
  return ret;
}

/************************************************************************/

EPI_OFF_T
TXRingBufferDbfAlloc(BUFFERDBF *df, void *buf, size_t n)
{
  void *copy = TXmalloc(df->pmbuf, __FUNCTION__, n);
  memcpy(copy, buf, n);
  return TXRingBufferDbfAppend(df, copy, n);
}

/************************************************************************/

EPI_OFF_T              /* supposedly alters the contents of an existing DBF */
TXRingBufferDbfPut(BUFFERDBF *df, EPI_OFF_T at, void *buf, size_t sz)
{
  switch(at) {
    case -1:
      /* If there isn't a block 0 (DD) yet, then don't store row */
      if(df->dd) {
        return TXRingBufferDbfAlloc(df, buf, sz);
      }
    /* Offset 0 is special for tables, and meant to hold the DD */
    case 0:
      TXfree(df->dd);
      df->dd = TXmalloc(df->pmbuf, __FUNCTION__, sz);
      if(df->dd) {
        memcpy(df->dd, buf, sz);
        df->dd_size = sz;
        return 0;
      }
      return -1;
    default:
      return -1; /* No random writes */
  }
}

/************************************************************************/

int
TXRingBufferDbfBlockIsValid(BUFFERDBF *df, EPI_OFF_T at)
{
	(void)df;
  (void)at;
  return -1;
}

/************************************************************************/

/* get a ram block from the rdbf: returns a pointer to the memory
*  that contains the block. It is not allocated, so you have to make a copy
*  of it if you want to keep it.
*/

void *
TXRingBufferDbfFetch(BUFFERDBF *df,EPI_OFF_T at,size_t *psz, int keep)
{
  BUFFERDBFBLOCK *rb;
  void *ret = NULL;

  TXRingBufferDbfFreeOld(df);
  switch(at) {
    case 0:
      if(psz) *psz = df->dd_size;
      df->last_at = 0;
      return df->dd;
    case -1:
      rb = TXRingBuffer_Get(df->RingBuffer);
      if(rb) {
        if(psz) *psz = rb->datasize;
        ret = rb->userdata;
        df->last_at = TXRingBuffer_nread(df->RingBuffer);
      }
      if(keep) {
        df->tofree = rb;
      } else {
        TXfree(rb);
      }
      return ret;
    default:
      return NULL;
  }
}

void *
TXRingBufferDbfGet(BUFFERDBF *df,EPI_OFF_T at,size_t *psz)
{
  return TXRingBufferDbfFetch(df, at, psz, 1);
}

void *
TXRingBufferDbfAllocGet(BUFFERDBF *df,EPI_OFF_T at,size_t *psz)
{
  void *ret;
  ret = TXRingBufferDbfFetch(df, at, psz, 0);
  return ret;
}

/************************************************************************/
/* Read into callers buffer.  This is less efficient than AllocGet for Ring Buffer */

size_t
TXRingBufferDbfRead(BUFFERDBF *df,EPI_OFF_T at,size_t *off,void *buf,size_t sz)
{
 void *vp;
 size_t blocksize;

 (void)off;
 vp = TXRingBufferDbfAllocGet(df, at, &blocksize);
 if(!vp) return 0;
 if (sz > blocksize) sz = blocksize;
 memcpy(buf,vp,sz);
 TXfree(vp);
 return(blocksize);
}

/************************************************************************/

EPI_OFF_T
TXRingBufferDbfTell(BUFFERDBF *df)
{
  return df->last_at;
}

/************************************************************************/

char *
TXRingBufferDbfGetFilename(df)
BUFFERDBF *df;
{
 return(df->name ? df->name : TxRingBufferDbfName);/* return something semi-useful */
}

/************************************************************************/

int
TXRingBufferDbfGetFileDescriptor(df)
BUFFERDBF *df;
{
 (void)df;
 return(-1);
}

/************************************************************************/

void                                                   /* means nothing */
TXRingBufferDbfSetOverAlloc(df,n)
BUFFERDBF *df;
int n;
{
 (void)df;
 (void)n;
}

/************************************************************************/

#ifndef NO_DBF_IOCTL
int
TXRingBufferDbfIoctl(df, ioctl, data)
BUFFERDBF	*df;
int	ioctl;
void	*data;
{
	char	*newName = NULL;

	if((ioctl & 0xFFFF0000) != DBF_RINGBUFFER)
		return -1;
	switch(ioctl)
	{
    case RINGBUFFER_DBF_HAS_DATA:
      return TXRingBuffer_Used(df->RingBuffer);
    case RINGBUFFER_DBF_HAS_SPACE:
      return TXRingBuffer_Free(df->RingBuffer);
		case RINGBUFFER_DBF_SET_NAME:
			if (data)
			{
				newName = TXstrdup(TXPMBUFPN, __FUNCTION__,
						   (char *)data);
				if (!newName) return(-1);
			}
			else
				newName = NULL;
			df->name = TXfree(df->name);
			df->name = newName;
			newName = NULL;
			return(0);		/* success */
		default:
			return -1;
	}
}
#endif

/************************************************************************/
int
TXRingBufferDbfSetPmbuf(BUFFERDBF *df, TXPMBUF *pmbuf)
/* Returns 0 on error.
 */
{
  TXPMBUF       *pmbufOrg = pmbuf;

  pmbuf = txpmbuf_open(pmbuf);                  /* clone `pmbuf' first */
  if (!pmbuf && pmbufOrg) return(0);
  df->pmbuf = txpmbuf_close(df->pmbuf);
  df->pmbuf = pmbuf;
  return(1);                                    /* success */
}
