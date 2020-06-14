/*
 * $Log$
 * Revision 1.20  2008/12/14 17:03:43  kai
 * Added TXCOUNTINFO arg to tup_read() calls
 *
 * Revision 1.19  2001/12/28 22:27:40  john
 * Use config.h
 *
 * Revision 1.18  2001-07-09 16:19:49-04  john
 * Function Name
 * 
 * Revision 1.17  2001-07-08 16:00:01-04  john
 * Need texint.
 * 
 * Revision 1.16  1999-12-16 17:33:35-05  john
 * Make sure we close the dd.
 * 
 * Revision 1.15  1999/11/09 22:49:12  john
 * Use TXcompatibletypes call.
 * New args to tup_read.
 * 
 * Revision 1.14  95/01/11  15:08:59  john
 * Use fast table.
 * 
 * Revision 1.13  94/09/06  16:22:19  john
 * Use RECID.
 * 
 * Revision 1.12  94/08/09  14:16:57  john
 * Use new RECID scheme.
 * 
 * Revision 1.11  94/03/18  17:33:41  john
 * remove decl.
 * 
 * Revision 1.10  94/03/09  13:35:07  john
 * Windows code.
 * 
 * Revision 1.9  93/10/21  16:05:09  john
 * Get headers right
 *
 * Revision 1.8  93/10/11  16:31:15  john
 * -> K&R
 *
 * Revision 1.7  93/10/07  13:52:54  john
 * tblid.
 *
 * Revision 1.6  93/08/31  10:09:40  john
 * Add ddic to result table.
 *
 * Revision 1.5  93/08/24  11:11:49  john
 * Add couple extra tests.
 *
 * Revision 1.4  93/08/17  14:04:13  john
 * Add recid field to the temporary table.
 *
 * Revision 1.3  93/08/09  16:48:14  john
 * rewindtbl -> rewinddbtbl
 * Set type of temp table.
 * Update call to tup_read.
 *
 * Revision 1.2  93/08/04  18:04:43  john
 * Added the code to perform the union.
 *
 * Revision 1.1  93/08/04  16:45:47  john
 * Initial revision
 *
*/

#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "fldmath.h"
#include "texint.h"

/****************************************************************************/
/*
 *	Read a Tuple from a table
 */

/****************************************************************************/

DBTBL *
tup_union_setup(tbl1, tbl2)
DBTBL *tbl1;
DBTBL *tbl2;
{
/*
	Need to determine if the tables are compatible, and set up
	a table for the results.

	Initially compatible means same fields in same order, with
	at least same suffix on name.
*/
	int i = 0, bzzt = 0;
	char *fn1, *fn2;

	DD *dd;
	DBTBL *dbt;
	TBL *tb1, *tb2;

	dd = opendd();
        if(!dd)
         return (DBTBL *)NULL;
	dbt = (DBTBL *)calloc(1, sizeof(DBTBL));
	(void)ddsettype(dd, 1);
	tb1 = tbl1->tbl;
	tb2 = tbl2->tbl;
	TXrewinddbtbl(tbl1);
	TXrewinddbtbl(tbl2);
	fn1 = getfldname(tb1, i);
	fn2 = getfldname(tb1, i);
	while (fn1 && fn2 && !bzzt)
	{
		if (strchr(fn1, '.'))
			fn1 = strchr(fn1, '.') + 1;
		if (strchr(fn2, '.'))
			fn2 = strchr(fn2, '.') + 1;
		if (!strcmp(fn1, fn2))
		{
			if (TXcompatibletypes(tb1->field[i]->type,
			                      tb2->field[i]->type))
				copydd(dd, fn1, tb1, fn1, 0);
			else
				bzzt = 1;
		}
		else
			bzzt = 1;
		i++;
		fn1 = getfldname(tb1, i);
		fn2 = getfldname(tb1, i);
	}
	if (!bzzt && !fn1 && !fn2)
	{
		dbt->tbl = createtbl(dd, NULL);
		dbt->type = 'T';
		dbt->ddic = tbl1->ddic;
		dbt->frecid = createfld("recid", 1, 0);
		dbt->tblid = -1;
		putfld(dbt->frecid, &dbt->recid, 1);
	}
	dd = closedd(dd);
	return dbt;
}

/****************************************************************************/

void
tup_union(tbl1, tbl2, tbout, fo)
DBTBL *tbl1;
DBTBL *tbl2;
DBTBL *tbout;
FLDOP *fo;
{
/*
	Tables need to be sorted so we can do a merge sort, which will
	effectively give us the union without duplicates.
*/
	DBTBL *t;

	if ((t=tup_read(tbl1, NULL, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN))!=(DBTBL *)NULL)
	{
		tup_write(tbout, t, fo, 0);
		return;
	}
	if ((t=tup_read(tbl2, NULL, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN))!=(DBTBL *)NULL)
		tup_write(tbout, t, fo, 0);
}

/****************************************************************************/

