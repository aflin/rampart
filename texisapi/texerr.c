/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "dbquery.h"


/******************************************************************/

int
texispusherror(ddic, error)
DDIC	*ddic;
TXERR	error;
{
#ifdef DEBUG
	putmsg(999, NULL, "Pushing error %d", (error & 0xffff));
#endif
	if(ddic->nerrs > MAXTXERR)
		return -1;
	ddic->errstack[ddic->nerrs++] = error;
	return 0;
}

/******************************************************************/

TXERR
texispoperr(ddic)
DDIC	*ddic;
{
	TXERR	error;
	if(ddic->nerrs < 1)
		return 0;
	error = ddic->errstack[--ddic->nerrs];
#ifdef DEBUG
	putmsg(999, NULL, "Popping error %d", (error & 0xffff));
#endif
	return error;
}

/******************************************************************/

TXERR
texispeekerr(ddic)
DDIC	*ddic;
{
	if(ddic->nerrs < 1)
		return 0;
	return ddic->errstack[ddic->nerrs - 1];
}

/******************************************************************/

int
TXtimedout(ddic)
DDIC	*ddic;
{
	int	i;

	for(i=0; i < ddic->nerrs; i++)
	{
		if (ddic->errstack[i] == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
			return 1;
	}
	return 0;
}

int
TXsqlWasCancelled(DDIC *ddic)
/* Returns nonzero if SQL statement was asynchronously cancelled
 * via SQLCancel(), 0 if not.
 */
{
	int	i;

	for (i = 0; i < ddic->nerrs; i++)
	{
		/* wtf LOCK_TIMEOUT is Texis' reaction to state DDIC_STOPPING,
		 * which is set by SQLCancel():
		 */
		if (ddic->errstack[i] == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
			return(1);
	}
	return(0);
}

/******************************************************************/

