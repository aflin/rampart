/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

typedef struct INCACHE {
	IINODE	*iinode;	/* Cached index results */
	DBTBL	*dbtbl;		/* Which table does this belong to */
	PRED	*pred;		/* Predicate it matches */
	int	asc;		/* Allow ShortCuts */
	int	inv;		/* In an AND */
	char	*apred;		/* ASCII rendition DEBUG */
} INCACHE;

/******************************************************************/

IINODE *
TXgetcachediinode(tb, p, fo, asc, inv)
DBTBL *tb;
PRED *p;
FLDOP *fo;
int asc;	/* Allow short-cut, i.e. parent of `p' (if any) is AND */
int inv;			/* Get index ready for AND */
{
	IINODE *rc = NULL;

#ifdef CACHE_IINODE
	char *x, *y;

	x = TXdisppred(p, 0, 0, 0);
	if (x)
	{
#ifdef NEVER
		putmsg(200, NULL, "Looking for index for '%s'", x);
#endif
		if(tb->cacheinode)
		{
			y = TXdisppred(tb->cacheinode->gpred, 0, 0, 0);
#ifdef NEVER
			putmsg(200, NULL, "Appear to have info for %s", y);
#endif
			if(!strcmp(x,y))
			{
				rc = tb->cacheinode;
#ifdef NEVER /* Keep for another day */
				tb->cacheinode = NULL;
#endif
			}
			if(y) free(y);
		}
		free(x);
	}
#endif
	return rc;
}

/******************************************************************/

int
TXcacheiinode(iinode, tb, p, fo, asc, inv)
IINODE *iinode;
DBTBL *tb;
PRED *p;
FLDOP *fo;
int asc;	/* Allow short-cut, i.e. parent of `p' (if any) is AND */
int inv;			/* Get index ready for AND */
{
#ifdef CACHE_IINODE
	char *x;

	x = TXdisppred(p, 0, 0, 0);
	if (x)
	{
#ifdef NEVER
		putmsg(200, NULL, "Storing index for '%s'", x);
#endif
		free(x);
	}
#endif
	return(0);
}

/******************************************************************/

