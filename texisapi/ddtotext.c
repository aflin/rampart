#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"


char *
TXddtotext(dd)
DD *dd;
{
	int i, type;
	DDFD *ddfd;
	static char outbuf[8192];
	char *t;

	if(!dd)
		return NULL;
	sprintf(outbuf, "%d\n", dd->tbltype);
	t = outbuf + strlen(outbuf);
	sprintf(t, "%d\n", dd->n);
	t = t + strlen(t);
	for(i=0; i < dd->n; i++)
	{
		ddfd = ddgetfd(dd, ddgetorign(dd, i));
		type = ddfd->type;
		if((type & DDTYPEBITS) == FTN_BLOBI)
			type = FTN_BLOB;
		sprintf(t, "%s %s " EPI_SIZE_T_FORMAT " %d\n",
			ddfd->name,
			ddfttypename(type),
			ddfd->size/ddfd->elsz,
			((ddfd->type & FTN_NotNullableFlag) ? 1 : 0));
		t = t + strlen(t);
	}
	return outbuf;
}

#ifdef TEST

main()
{
	size_t sz;
	DBF *dbf;

	dbf=opendbf("/usr/tmp/junk/SYSINDEX.tbl");
	printf("%s", TXddtotext(getdbf(dbf, 0, &sz)));
}

#endif
