#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "os.h"
#ifdef EPI_HAVE_STDARG
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif
#include "sizes.h"
#include "mmsg.h"
#include "dbquery.h"
#include "texint.h"
#include "kdbf.h"
#include "kdbfi.h"

#ifdef KDBF_CHECKSUM_FREETREE
dword
kdbf_checksum_block(buf, size)
void	*buf;
size_t	size;
/* Computes a checksum of `size' bytes of `buf'.  Used for checksumming
 * free-tree pages.  Assumes that `buf' may not be aligned.
 */
{
  dword		*ptr, *end;
  dword		sum, val;
  byte		*bp, *be;
#define	DOSUM(v)	sum ^= (v)

  /* Do checksum, and try to be fast: */
  sum = (dword)0L;
  end = (dword *)buf + (size/sizeof(dword));
  if (KDBF_ALIGN_DN(buf) == buf) {	/* if aligned */
    for (ptr = buf; ptr < end; ptr++) {
      DOSUM(*ptr);
    }
  } else {	/* not aligned */
    /* Assume sizeof(dword) == 4:  -KNG 951109 */
    for (ptr = buf; ptr < end; ptr++) {
      /* Do the alignment thang: */
      *((byte *)&val) = *((byte *)ptr);
      ((byte *)&val)[1] = ((byte *)ptr)[1];
      ((byte *)&val)[2] = ((byte *)ptr)[2];
      ((byte *)&val)[3] = ((byte *)ptr)[3];
      DOSUM(val);
    }
  }
  /* Add in any remaining chunk as bytes cast to dword, since we don't
   * know the endian-ness of this box (assume sizeof(dword) == 4):
   */
  for (bp = (byte *)end, be = bp + (size & (size_t)0x3); bp < be; bp++) {
    DOSUM((dword)*bp);
  }
  if (sum == (dword)0L) sum--;		/* don't allow 0L == no checksum */
  return(sum);
#undef DOSUM
}
#endif	/* KDBF_CHECKSUM_FREETREE */

int
kdbf_header_type(n)
size_t	n;
/* Returns -1 (and yaps) on error, else header type for size `n'.
 */
{
  static char	fn[] = "kdbf_header_type";

  if (TX_SIZE_T_VALUE_LESS_THAN_ZERO(n)) goto badness;
  if (n == (size_t)0) return(KDBFNULL);
  if (n <= (size_t)KDBFNMAX) return(KDBFNIBBLE);
  if (n <= (size_t)KDBFBMAX) return(KDBFBYTE);
  if (n <= (size_t)KDBFWMAX) return(KDBFWORD);
  if (n <= (size_t)KDBFSMAX) return(KDBFSTYPE);

  /* Too big; report error since this must be an internal error
   * or B-tree screwup (size was checked before original allocation):
   */
badness:
  putmsg(MERR+UGE, fn, "Internal error: KDBF block size 0x%wx is beyond max",
         (EPI_HUGEINT)n);
  return(-1);
}

int
kdbf_header_size(type)
int	type;
{
  /* Note the alignment padding on some types:  -KNG 950918 */
  static CONST int	sz[KDBF_NUM_TYPES] = {
    0, KDBFN_LEN, KDBFB_LEN, KDBFW_LEN, KDBFS_LEN
  };
  type &= KDBFTYBITS;
  return((unsigned)type < KDBF_NUM_TYPES ? 1 + sz[type] : -1);
}

#ifdef KDBF_CHECK_OLD_F_TYPE
int
kdbf_old_f_header_size(type)
int	type;
/* KNG 970616 handle old F type for alpha's
 */
{
  /* Note the alignment padding on some types:  -KNG 950918 */
  static CONST int	sz[KDBF_NUM_TYPES] = {
    0, KDBFN_LEN, 5, 5, 9
  };
  type &= KDBFTYBITS;
  return((unsigned)type < KDBF_NUM_TYPES ? 1 + sz[type] : -1);
}
#endif  /* KDBF_CHECK_OLD_F_TYPE */

int
kdbf_proc_head(buf, len, at, trans)
byte		*buf;
size_t		len;
EPI_OFF_T	at;
KDBF_TRANS	*trans;
/* Processes header in `buf' (`len' bytes), setting fields in
 * `*trans'.  Returns 0 if header truncated (buffer too short), -1
 * on error (header corrupt), otherwise header size if ok.  `at' is
 * current file offset.
 */
{
  int		hdsz;
  KDBFALL	hdr;

  if (len <= 0) return(0);                      /* need at least type byte */
  trans->at = at;
  trans->type = buf[0];
#ifdef KDBF_CHECK_OLD_F_TYPE
  /* See if it's the original-alignment 0xF0 KDBF type for alpha:
   * KNG 970616
   */
  if ((trans->type & KDBF_CHKSUMBITS) == KDBF_CHECK_OLD(trans))
    {
      hdsz = kdbf_old_f_header_size((int)trans->type);
      if (hdsz < 0) return(-1);
      if (len < (size_t)hdsz) return(0);
      goto cont;
    }
#endif  /* KDBF_CHECK_OLD_F_TYPE */

  hdsz = kdbf_header_size((int)trans->type);
  if (hdsz < 0) return(-1);			/* bogus type: corrupt */
  if (len < (size_t)hdsz) return(0);		/* header truncated */

  if ((trans->type & KDBF_CHKSUMBITS) != KDBF_CHECK(trans)) {
    /* Header corrupt.  Note that a read at 0 on an empty file can
     * generate this, but should be a non-fatal error for backwards
     * compatibility with fdbf.  This must be checked by caller
     * beforehand.  -KNG 950925
     */
    return(-1);
  }

#ifdef KDBF_CHECK_OLD_F_TYPE
cont:
#endif
  switch ((int)trans->type & KDBFTYBITS) {	/* determine structure type */
    case KDBFNULL:
      trans->used = trans->size = (size_t)0;
      break;
    case KDBFNIBBLE:
      hdr.n.used_size = buf[1];		/* quicker than memcpy */
      trans->used = (size_t)((unsigned)hdr.n.used_size >> 4);
      trans->size = (size_t)((unsigned)hdr.n.used_size & 0x0F);
      break;
    case KDBFBYTE:
      trans->used = (size_t)(buf[1]);	/* quicker than memcpy */
      trans->size = (size_t)(buf[2]);
      break;
    case KDBFWORD:
      memcpy(&hdr.w, buf + 1, sizeof(hdr.w));
#if EPI_OS_SIZE_T_BITS <= 16
      /* check against KDBFWMAX in case < 0xFFFF (i.e. DOS):  -KNG 951110 */
      if (hdr.w.used_size[0] > KDBFWMAX ||
	  hdr.w.used_size[1] > KDBFWMAX)
	return(-1);
#endif /* EPI_OS_SIZE_T_BITS <= 16 */
      trans->used = (size_t)hdr.w.used_size[0];
      trans->size = (size_t)hdr.w.used_size[1];
      break;
    case KDBFSTYPE:
      memcpy(&hdr.s, buf + 1, sizeof(hdr.s));
      /* Check limit, since it's less than the type max: */
      if (hdr.s.used_size[1] > KDBFSMAX ||
	  /* size_t might be signed:  -KNG 951116 */
          TX_SIZE_T_VALUE_LESS_THAN_ZERO(hdr.s.used_size[0]) ||
          TX_SIZE_T_VALUE_LESS_THAN_ZERO(hdr.s.used_size[1]))
	return(-1);
      trans->used = (size_t)hdr.s.used_size[0];
      trans->size = (size_t)hdr.s.used_size[1];
      break;
    default:
      return(-1);
  }
  if (trans->used > trans->size) return(-1);	/* bogus condition */
  trans->end = at + (EPI_OFF_T)hdsz + (EPI_OFF_T)trans->size;
  return(hdsz);
}

int
kdbf_create_head(df, buf, trans)
KDBF            *df;    /* (out, opt.) for msgs */
byte		*buf;
KDBF_TRANS	*trans;
/* Writes header for `trans' to `buf', which is assumed to be at least
 * KDBF_HMAXSIZE bytes long.
 * Returns number of bytes written, or -1 on error.
 */
{
  static const char     fn[] = "kdbf_create_head";
  KDBFALL               u;              /* a union of all the header types */
  size_t                sz, limitSz;
  int                   i;
  TXPMBUF               *pmbuf = (df ? df->pmbuf : TXPMBUFPN);
  const char            *kdbfFile = (df ? df->fn : "?");

  trans->type &= ~KDBF_CHKSUMBITS;              /* mask off the old stuff */
  trans->type |= KDBF_CHECK(trans);		/* checksum it */
  *buf++ = trans->type;

  if (trans->used > trans->size)
    {
      txpmbuf_putmsg(pmbuf, MERR, fn, "Used-size exceeds total-size while trying to create a block header for KDBF file `%s'",
                     kdbfFile);
      goto err;
    }

  switch ((int)trans->type & KDBFTYBITS) {	/* determine structure type */
    case KDBFNULL:
      if (trans->size != (limitSz = 0)) goto sizeExceeded;
      i = 0;
      sz = (size_t)0;
      break;
    case KDBFNIBBLE:
      if (trans->size > (limitSz = KDBFNMAX)) goto sizeExceeded;
      buf[0] = (trans->used << 4) | (trans->size);
      i = SIZEOF_KDBFN;
      sz = KDBFN_LEN;
      break;
    case KDBFBYTE:
      if (trans->size > (limitSz = KDBFBMAX)) goto sizeExceeded;
      buf[0] = (byte)trans->used;
      buf[1] = (byte)trans->size;
      i = sizeof(KDBFB);
      sz = KDBFB_LEN;
      break;
    case KDBFWORD:
      if (trans->size > (limitSz = KDBFWMAX)) goto sizeExceeded;
      u.w.used_size[0] = (EPI_UINT16)trans->used;
      u.w.used_size[1] = (EPI_UINT16)trans->size;
      i = sizeof(KDBFW);
      sz = KDBFW_LEN;			/* size includes padding */
      memcpy(buf, &u.w, sizeof(KDBFW));
      break;
    case KDBFSTYPE:
      if (trans->size > (limitSz = KDBFSMAX))
        {
        sizeExceeded:
          txpmbuf_putmsg(pmbuf, MERR, fn, "Total size exceeds type %d block limit of 0x%wx while trying to create a block header for KDBF file `%s'",
                         (int)((int)trans->type & KDBFTYBITS),
                         (EPI_HUGEINT)limitSz, kdbfFile);
          goto err;
        }
#ifdef EPI_OS_SIZE_T_IS_SIGNED
      /* size_t might be signed:  -KNG 951222 */
      if (!(trans->used >= (size_t)0 && trans->size >= (size_t)0))
        {
          txpmbuf_putmsg(pmbuf, MERR, fn, "Used-size or total-size less than 0 while trying to create a block header for KDBF file `%s'",
                         kdbfFile);
          goto err;
        }
#endif /* EPI_OS_SIZE_T_IS_SIGNED */
      u.s.used_size[0] = (size_t)trans->used;
      u.s.used_size[1] = (size_t)trans->size;
      i = sizeof(KDBFS);
      sz = KDBFS_LEN;
      memcpy(buf, &u.s, sizeof(KDBFS));
      break;
    default:
      txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
   "Unknown type %d while trying to create a block header for KDBF file `%s'",
                     (int)((int)trans->type & KDBFTYBITS), kdbfFile);
      goto err;
  }
  for ( ; (size_t)i < sz; i++) buf[i] = 0;    /* clear padding for neatness */
  return(++sz);				/* add 1 for leading type-byte */

err:
  return(-1);
}
