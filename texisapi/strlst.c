#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "texint.h"
#include "cgi.h"				/* for htbuf_... functions */

/******************************************************************/

#define finddelim(a)	-1;

/******************************************************************/

int
TXstrlstcmp(a, b)
char **a;
char **b;
{
	int rc;

	if(!a && !b)
		return 0;
	if(!a || !b)
		return 1;
	for(rc=0; rc == 0 && *a && *b; a++, b++)
	{
		rc = strcmp(*a, *b);
	}
	return rc;
}

/******************************************************************/

int
TXstrlstcount(a)
char **a;
{
	int rc = 0;
	if(a) for ( ; a[rc]; rc++);
	return rc;
}

int
TXstrlstBufBegin(buf)
HTBUF	*buf;	/* (in/out) buffer to write to */
/* Clears `buf' and writes placeholder partial strlst header to it.
 * Returns 0 on error.
 */
{
	ft_strlst	hdr;

	htbuf_clear(buf);
	memset(&hdr, 0, sizeof(ft_strlst));
	return(htbuf_write(buf, (char *)&hdr, TX_STRLST_MINSZ));
}

int
TXstrlstBufAddString(buf, s, sz)
HTBUF		*buf;	/* (in/out) buffer to write to */
CONST char	*s;	/* (in) string to add */
size_t		sz;	/* (in, opt.) length of `s' (-1 == strlen(s)) */
/* Appends `s' to `buf', which is assumed to have been init'd with
 * TXstrlstBegin().
 * Returns 0 on error.
 */
{
	/* Make sure we also write the string's nul as part of payload: */
	if (sz == (size_t)(-1))			/* use strlen(s) */
		/* Can write string + nul in one write; `s' nul-terminated: */
		return(htbuf_write(buf, s, strlen(s) + 1));
	else					/* use `sz' */
		/* Two writes: `s' may not be nul-terminated: */
		return(htbuf_write(buf, s, sz) && htbuf_write(buf, "", 1));
}

int
TXstrlstBufEnd(buf)
HTBUF	*buf;	/* (in/out) buffer to write to */
/* Finishes `buf' after TXstrlstBegin() and 0 or more strings written,
 * i.e. sets strlst header fields.  Finished strlst can be obtained
 * with htbuf_getdata().
 * Returns 0 on error.
 */
{
	char		*data;
	size_t		dataSz, strDataSz;
	ft_strlst	*hdr, dum;
	byte		*s, *e, byteUsed[256];
	size_t		i;

	dataSz = htbuf_getdata(buf, &data, 0);
	/* Make sure we have at least the minimum header from init: */
	if (dataSz < TX_STRLST_MINSZ)		/* no TXstrlstBegin() call */
	{
		memset(&dum, 0, sizeof(ft_strlst));
		if (!htbuf_write(buf, (char *)&dum, TX_STRLST_MINSZ - dataSz))
			return(0);
	}

	/* Add strlst-terminating string: */
	if (!htbuf_write(buf, "", 1)) return(0);

	/* Pad out to at least a full ft_strlst, for safety: */
	dataSz = htbuf_getdata(buf, &data, 0);
	strDataSz = dataSz - TX_STRLST_MINSZ;
	if (dataSz < sizeof(ft_strlst))
	{
		memset(&dum, 0, sizeof(ft_strlst));
		if (!htbuf_write(buf, (char *)&dum,
				 sizeof(ft_strlst) - dataSz))
			return(0);
		dataSz = htbuf_getdata(buf, &data, 0);
		/* `strDataSz' remains the same: we added struct padding
		 * not payload string data
		 */
	}

	hdr = (ft_strlst *)data;
	hdr->nb = strDataSz;
	/* Compute delimiter, via TXVSSEP_CREATE method (regardless of
	 * varchartostrlstsep: we are not really converting an `a,b,c,'
	 * varchar to a strlst, as we get list of strings separately):
	 */
	memset(byteUsed, 0, sizeof(byteUsed));
	for (s = (byte *)hdr->buf, e = s + strDataSz; s < e; s++)
		byteUsed[*s] = 1;
	hdr->delim = '\0';			/* in case none found */
	for (i = 0; i < 256; i++)
		if (!byteUsed[(byte)TxPrefStrlstDelims[i]])
		{
			hdr->delim = TxPrefStrlstDelims[i];
			break;
		}

	return(1);				/* success */
}
