#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"


/******************************************************************/

int
TXresetdatasize(ds)
DATASIZE *ds;
/* Returns 0 if ok, -1 on error. */
{
	ds->gig = 0;
	ds->bytes = 0;
	return(0);				/* success */
}

/******************************************************************/

int
TXadddatasize(ds, bytes)
DATASIZE *ds;
long bytes;
/* Returns 0 if ok, -1 on error. */
{
	ds->bytes +=bytes;
	while(ds->bytes > (TX_DATASIZE_INT)TX_DATASIZE_GIG)
	{
		ds->gig++;
		ds->bytes -= (TX_DATASIZE_INT)TX_DATASIZE_GIG;
	}
	return(0);				/* success */
}

/******************************************************************/

int
TXadddatasizeh(ds, bytes)
DATASIZE *ds;
EPI_HUGEINT bytes;
{
	while(bytes > (EPI_HUGEINT)TX_DATASIZE_GIG)
	{
		ds->gig++;
		bytes -= (EPI_HUGEINT)TX_DATASIZE_GIG;
	}
	ds->bytes += (TX_DATASIZE_INT)bytes;
	while(ds->bytes > (TX_DATASIZE_INT)TX_DATASIZE_GIG)
	{
		ds->gig++;
		ds->bytes -= (TX_DATASIZE_INT)TX_DATASIZE_GIG;
	}
	return 0;
}

/******************************************************************/

int
TXsubdatasize(ds, bytes)
DATASIZE *ds;
long bytes;
/* Returns 0 if ok, -1 on error. */
{
	ds->bytes -= bytes;
	while((ds->bytes < 0 && ds->gig > 0) ||
	      (ds->bytes < (TX_DATASIZE_INT)(-TX_DATASIZE_GIG)))
	{
		ds->gig--;
		ds->bytes += (TX_DATASIZE_INT)TX_DATASIZE_GIG;
	}
	return(0);				/* success */
}

/******************************************************************/

int
TXdatasizecmp(ds, ds2)
DATASIZE *ds;
DATASIZE *ds2;
{
	if(ds->gig > ds2->gig)
		return 1;
	if(ds->gig < ds2->gig)
		return -1;
	if(ds->bytes > ds2->bytes)
		return 1;
	if(ds->bytes < ds2->bytes)
		return -1;
	return 0;
}

/******************************************************************/

int
TXdatasizeadd(ds, ds2)
DATASIZE *ds;
DATASIZE *ds2;
/* Returns 0 if ok, -1 on error. */
{
	ds->gig = ds->gig + ds2->gig;
	TXadddatasize(ds, ds2->bytes);
	return 0;
}
