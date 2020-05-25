/*
 * $Log$
 * Revision 1.4  2001/12/28 22:18:22  john
 * Use config.h
 *
 */

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"

/******************************************************************/
/*
	Free the memory used by an MMLST.

	This does not close the handles that are stored in the
	list.

	Returns NULL.
*/

MMLST *
freemmlst(mhl)
MMLST	*mhl;
{
	if(mhl!=(MMLST *)NULL)
        {
#ifdef NEVER           /* MAW 07-01-94 - name is someone else's data! */
         int i;
                for(i=0;i<MAXMMLST;i++)  /* MAW 06-27-94 - free names */
                        if(mhl->name[i]!=(char *)NULL)
                                free(mhl->name[i]);
#endif
		free(mhl);
        }
	return (MMLST *)NULL;
}

/******************************************************************/
/*
	Open and allocate a new MMLST.

	Returns NULL on failure and a pointer to the new MMLST
	otherwise.
*/

MMLST *
openmmlst()
{
	MMLST	*mhl;
	int	i;

	mhl = (MMLST *)calloc(1, sizeof(MMLST));
	if (mhl)
	{
		mhl->n = 0;
		for (i=0; i < MAXMMLST; i++)
                {
			mhl->handle[i] = (void *)NULL;
			mhl->buf[i] = (char *)NULL;
			mhl->name[i] = (char *)NULL;
                }
	}
	return mhl;
}

/******************************************************************/
/*
	Add a new item to the MMLST.

	This code will add a new handle to the MMLST if there is a
	slot available in the MMLST.

	Returns 0 on success, and -1 if MMLST is full.
*/

int
addmmlst(mhl, v, buf, name)
MMLST	*mhl;
void	*v;
char    *buf;
char    *name;
{
	if (mhl->n >= MAXMMLST)
		return -1;
	mhl->handle[mhl->n] = v;
	mhl->buf[mhl->n] = buf;
	mhl->name[mhl->n] = name;
	mhl->n++;
	return 0;
}

/******************************************************************/

