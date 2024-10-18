#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include "os.h"
#include "strbuf.h"
#include "mmsg.h"


static CONST char	CannotAlloc[] = "Cannot alloc %ld bytes of memory";


STRBUF *
openstrbuf()
{
	STRBUF *rc;
	rc = (STRBUF *)malloc(sizeof(STRBUF));
	if(rc)
	{
		rc->alloced = rc->len = 0;
		rc->data = NULL;
	}
	else
	{
		putmsg(MERR + MAE, "openstrbuf", CannotAlloc,
			(long)sizeof(STRBUF));
	}
	return rc;
}

/******************************************************************/

STRBUF *
closestrbuf(rc)
STRBUF *rc;
{
	if(rc)
	{
		if(rc->data)
			free(rc->data);
		free(rc);
	}
	return NULL;
}

/******************************************************************/

int
addstrbuf(sb, st, stlen)
STRBUF *sb;	/* buffer to append to */
char *st;	/* string to append */
int stlen;	/* length of `st' (<0 == strlen(st)) */
/* Appends `st' to `sb'.  Returns 0 if ok, -1 on error.
 */
{
	size_t sl,nsz;
	char *v;

	if(stlen < 0)
		sl = strlen(st);
	else
		sl = stlen;
	if(sl == 0)
		return 0;			/* nothing to add */
	if(sl+sb->len >= sb->alloced)		/* need to realloc */
	{
		nsz = ((sl+sb->len+1024)/1024)*1024;
		if(sb->data)
			v = (char *)realloc(sb->data, nsz);
		else
			v = (char *)malloc(nsz);
		if(!v)
		{
			putmsg(MERR + MAE, "addstrbuf", CannotAlloc,
				(long)nsz);
#ifndef EPI_REALLOC_FAIL_SAFE			/* assume realloc() freed */
			if (sb->data)
			{
				sb->data = CHARPN;
				sb->alloced = sb->len = 0;
			}
#endif /* !EPI_REALLOC_FAIL_SAFE */
			return -1;
		}
		sb->alloced = nsz;
		sb->data = v;
	}
	TXstrncpy(sb->data+sb->len, st, sl+1);
	sb->len += sl;
	return 0;
}

/******************************************************************/

int
resetstrbuf(sb)
STRBUF *sb;
/* Clears `sb', ie. sets to empty.  Does not free buffer.
 */
{
	sb->len = 0;
	if(sb->data && sb->alloced > 0)
		sb->data[0] = '\0';
	return(0);
}

/******************************************************************/

int
lenstrbuf(sb)
STRBUF *sb;
/* Returns length of `sb'.
 */
{
	return sb->len;
}
