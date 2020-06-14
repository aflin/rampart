/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#ifdef _WINDLL
int __cdecl sscanf(const char *, const char *, ...);
#endif /* _WINDLL */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "os.h"
#include "sizes.h"
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"
#include "parsetim.h"
#include "sregex.h"
#include "cgi.h"				/* for htsnpf() */


/**********************************************************************/

static int
TX_Stringcmp(const void *a, const void *b)
{
	const TX_String	*as = (TX_String *)a, *bs = (TX_String *)b;

	return TXstringcompare(as->v, bs->v, as->len, bs->len);
}

/**********************************************************************/

int
TX_fldSortStringList(FLD *f2)
{
	if(f2->dsc.ptrsused > 0
	  && (f2->dsc.ptrsused == f2->dsc.ptrsalloced))
	{
		if(!f2->issorted)
		{
			qsort(f2->dsc.dptrs.strings,f2->dsc.ptrsused,sizeof(TX_String), TX_Stringcmp);
			f2->issorted=1;
		}
	}
	return(1);				/* success */
}

/******************************************************************/
