#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

int
recidvalid(recid)
RECID	*recid;
{
	if (recid == NULL)
		return 0;
	if (recid->off == (EPI_OFF_T)-1)
		return 0;
	return 1;
}

/******************************************************************/

PRECID
telltbl(tbl)
TBL	*tbl;
{
	static	RECID	recid;

        if(tbl)
           TXsetrecid(&recid, telldbf(tbl->df));
        else
           recid.off = 0;
	return &recid;
}

/******************************************************************/

#undef TXgetoff
EPI_OFF_T
TXgetoff(recid)
PRECID	recid;
{
	if (recid == (PRECID)NULL)
		return -1;
	else
		return recid->off;
}

/******************************************************************/

#undef TXsetrecid
EPI_OFF_T
TXsetrecid(recid, x)
PRECID	recid;
EPI_OFF_T	x;
{
	recid->off = x;
	return x;
}

/******************************************************************/

DBF *
TXgetdbf(tbl, recid)
TBL	*tbl;
PRECID	recid;
{
	return tbl->df;
}

/******************************************************************/

#undef _recidcmp
int
_recidcmp(r1, r2)
PRECID	r1, r2;
{
#if (EPI_OS_INT_BITS==EPI_OFF_T_BITS)
	return r1->off - r2->off;
#else
	return r1->off>r2->off?1:r2->off>r1->off?-1:0;
#endif
}
