#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "os.h"
#include "mmsg.h"
#include "dbquery.h"
#include "texint.h"


/* TXNOOPDBF: for some QNODE-to-QNODE DBTBLs, where we just need the
 * current DBTBL.fields tuple and do not need to store data in a DBF.
 * Silently and successfully accepts all writes; fails all reads
 * except offset 0.  Its presence in a DBF tells puttblrow() etc. to
 * maybe skip fldtobuf() and putdbf().
 */

struct TXNOOPDBF_tag
{
  TXPMBUF       *pmbuf;
  EPI_OFF_T     lastOffset;
  EPI_OFF_T     nextWriteOffset;
  int           nextReadFromStart;
};

static const char       CannotPerformOp[] =
  "Internal error: Cannot perform operation on no-op DBF";


TXNOOPDBF *
TXnoOpDbfClose(TXNOOPDBF *noOpDbf)
{
  if (noOpDbf)
    {
      noOpDbf->pmbuf = txpmbuf_close(noOpDbf->pmbuf);
      noOpDbf = TXfree(noOpDbf);
    }
  return(TXNOOPDBFPN);
}

TXNOOPDBF *
TXnoOpDbfOpen(void)
{
  static const char     fn[] = "TXnoOpDbfOpen";
  TXNOOPDBF             *noOpDbf;

  noOpDbf = (TXNOOPDBF *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(TXNOOPDBF));
  return(noOpDbf);
}

int
TXnoOpDbfFree(TXNOOPDBF *noOpDbf, EPI_OFF_T at)
{
  return(1);
}

EPI_OFF_T
TXnoOpDbfAlloc(TXNOOPDBF *noOpDbf, void *buf, size_t n)
{
  /* Silently accept all writes like /dev/null; we yap on reads: */
  return(noOpDbf->lastOffset = noOpDbf->nextWriteOffset++);
}

EPI_OFF_T
TXnoOpDbfPut(TXNOOPDBF *noOpDbf, EPI_OFF_T at, void *buf, size_t sz)
{
  if (at == (EPI_OFF_T)(-1)) at = noOpDbf->nextWriteOffset++;
  noOpDbf->lastOffset = at;
  /* Silently accept all writes like /dev/null; we yap on reads: */
  return(at);
}

int
TXnoOpDbfBlockIsValid(TXNOOPDBF *noOpDbf, EPI_OFF_T at)
{
  static const char     fn[] = "TXnoOpDbfBlockIsValid";

  txpmbuf_putmsg(noOpDbf->pmbuf, MERR + UGE, fn, CannotPerformOp);
  return(0);
}

void *
TXnoOpDbfGet(TXNOOPDBF *noOpDbf, EPI_OFF_T at, size_t *psz)
{
  static const char     fn[] = "TXnoOpDbfGet";

  if (at == (EPI_OFF_T)(-1))                    /* get next block */
    at = (noOpDbf->nextReadFromStart ? (EPI_OFF_T)0 :
          noOpDbf->lastOffset + (EPI_OFF_T)1);
  noOpDbf->nextReadFromStart = 0;

  noOpDbf->lastOffset = at;

  /* We allow a read at offset 0, to simulate a .tbl header: */
  if (at == (EPI_OFF_T)0)
    {
      *psz = 0;
      return("");
    }

  txpmbuf_putmsg(noOpDbf->pmbuf, MERR + UGE, fn, CannotPerformOp);
  *psz = 0;
  return(NULL);
}

void *
TXnoOpDbfAllocGet(TXNOOPDBF *noOpDbf, EPI_OFF_T at, size_t *psz)
{
  static const char     fn[] = "TXnoOpDbfAllocGet";
  void                  *res, *dup;

  res = TXnoOpDbfGet(noOpDbf, at, psz);
  if (!res) return(NULL);
  if (!(dup = TXmalloc(noOpDbf->pmbuf, fn, *psz + 1))) return(NULL);
  if (*psz > (size_t)0) memcpy(dup, res, *psz);
  ((byte *)dup)[*psz] = '\0';
  return(dup);
}

size_t
TXnoOpDbfRead(TXNOOPDBF *noOpDbf, EPI_OFF_T at, size_t *off, void *buf,
              size_t sz)
{
  static const char     fn[] = "TXnoOpDbfRead";

  txpmbuf_putmsg(noOpDbf->pmbuf, MERR + UGE, fn, CannotPerformOp);
  *off = 0;
  return(0);
}

EPI_OFF_T
TXnoOpDbfTell(TXNOOPDBF *noOpDbf)
{
  return(noOpDbf->lastOffset);
}

char *
TXnoOpDbfGetFilename(TXNOOPDBF *noOpDbf)
{
  return("(no-op DBF)");
}

int
TXnoOpDbfGetFileDescriptor(TXNOOPDBF *noOpDbf)
{
  return(-1);
}

void
TXnoOpDbfSetOverAlloc(TXNOOPDBF *noOpDbf, int ov)
{
  static const char     fn[] = "TXnoOpDbfSetOverAlloc";

  txpmbuf_putmsg(noOpDbf->pmbuf, MERR + UGE, fn, CannotPerformOp);
}

int
TXnoOpDbfIoctl(TXNOOPDBF *noOpDbf, int ioctl, void *data)
/* Returns -1 on error, 0 on success.
 */
{
  static const char     fn[] = "TXnoOpDbfIoctl";

  if ((ioctl & 0xffff0000) != DBF_NOOP) return(-1);

  switch (ioctl & ~0xffff0000)
    {
    case TXNOOPDBF_IOCTL_SEEKSTART:
      noOpDbf->lastOffset = (EPI_OFF_T)0;
      noOpDbf->nextReadFromStart = 1;
      return(0);
    }
  txpmbuf_putmsg(noOpDbf->pmbuf, MERR + UGE, fn, CannotPerformOp);
  return(-1);
}

int
TXnoOpDbfSetPmbuf(TXNOOPDBF *noOpDbf, TXPMBUF *pmbuf)
/* Returns 0 on error.
 */
{
  TXPMBUF       *pmbufOrg = pmbuf;

  pmbuf = txpmbuf_open(pmbuf);		        /* clone `pmbuf' first */
  if (!pmbuf && pmbufOrg) return(0);
  noOpDbf->pmbuf = txpmbuf_close(noOpDbf->pmbuf);
  noOpDbf->pmbuf = pmbuf;
  return(1);                                    /* success */
}
