/*
 * $Log$
 * Revision 1.18  2010/12/26 23:27:31  john
 * Private: SQL, Enhancement, Texis: Start of support for blobz
 *
 * Revision 1.17  2001/12/28 22:26:28  john
 * Use config.h
 *
 * Revision 1.16  2001-07-09 16:23:28-04  john
 * Function Name
 * 
 * Revision 1.15  1999-12-16 16:40:19-05  john
 * Define compatible types.
 * 
 * Revision 1.14  95/12/19  14:46:46  john
 * Get field name from right table.
 * Improve handline.
 * 
 * Revision 1.13  95/08/07  13:50:00  john
 * Make sure base types match.
 * 
 * Revision 1.12  94/09/23  12:55:32  john
 * remove name restriction.
 * 
 * Revision 1.11  94/09/06  16:21:09  john
 * Return a result code.
 * 
 * Revision 1.10  94/08/09  13:56:00  john
 * Add FLDOP arg.
 * 
 * Revision 1.9  94/06/16  13:43:54  john
 * Def out some declarations.
 * 
 * Revision 1.8  94/03/18  17:33:01  john
 * remove decl
 * 
 * Revision 1.7  94/03/09  13:33:17  john
 * Windows code
 * 
 * Revision 1.6  93/11/02  13:07:43  john
 * Modified for the single row at a time model.
 *
 * Revision 1.5  93/10/21  16:05:05  john
 * Get headers right
 *
 * Revision 1.4  93/10/20  12:44:58  john
 * Remove references to DD, not needed.
 *
 * Revision 1.3  93/10/11  16:31:05  john
 * -> K&R
 *
 * Revision 1.2  93/09/10  14:38:29  john
 * Closedd.
 *
 * Revision 1.1  93/08/24  11:16:33  john
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
#include "texint.h"
#include "fldmath.h"

/****************************************************************************/
/*
 *
 */

int
TXcompatibletypes(type1, type2)
int type1, type2;
{
	int ttype1, ttype2;
	int tt;

	ttype1 = type1 & DDTYPEBITS;
	ttype2 = type2 & DDTYPEBITS;

	if(ttype1 == ttype2)
		return 1;
	if((ttype1 == FTN_BLOB || ttype1 == FTN_BLOBZ) && ttype2 == FTN_BLOBI)
		return 1;
	if(ttype1 == FTN_BLOBI && (ttype2 == FTN_BLOB || ttype2 == FTN_BLOBZ))
		return 1;
	if(ttype2 < ttype1)
	{
		tt = ttype1;
		ttype1 = ttype2;
		ttype2 = tt;
	}
	switch (ttype1)
	{
		case FTN_BYTE:
			switch(ttype2)
			{
				case FTN_CHAR:
					return 1;
				default: return 0;
			}
		case FTN_CHAR:
			switch(ttype2)
			{
				case FTN_BLOB:
				case FTN_BLOBI:
					return 1;
				default: return 0;
			}
		case FTN_DOUBLE:
			switch(ttype2)
			{
				case FTN_FLOAT:
				case FTN_INT:
				case FTN_INTEGER:
				case FTN_LONG:
				case FTN_SHORT:
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_FLOAT:
			switch(ttype2)
			{
				case FTN_INT:
				case FTN_INTEGER:
				case FTN_LONG:
				case FTN_SHORT:
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_INT:
			switch(ttype2)
			{
				case FTN_INTEGER:
				case FTN_LONG:
				case FTN_SHORT:
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_INTEGER:
			switch(ttype2)
			{
				case FTN_LONG:
				case FTN_SHORT:
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_LONG:
			switch(ttype2)
			{
				case FTN_SHORT:
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_SHORT:
			switch(ttype2)
			{
				case FTN_SMALLINT:
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_SMALLINT:
			switch(ttype2)
			{
				case FTN_WORD:
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
		case FTN_WORD:
			switch(ttype2)
			{
				case FTN_DWORD:
					return 1;
				default: return 0;
			}
	}
	return 0;
}

/****************************************************************************/

DBTBL *
tup_append_setup(tbl1, tbl2)
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
	FLD	*fld1, *fld2;

	TBL *tb1, *tb2;

	tb1 = tbl1->tbl;
	tb2 = tbl2->tbl;
	TXrewinddbtbl(tbl1);
	TXrewinddbtbl(tbl2);
	fn1 = getfldname(tb1, i);
	fn2 = getfldname(tb2, i);
	while (fn1 && fn2 && !bzzt)
	{
#ifdef DEBUG
		DBGMSG(9,(999, NULL, "(%d, %s)\t(%d, %s)", i, fn1, i, fn2));
#endif
		if (strchr(fn1, '.'))
			fn1 = strchr(fn1, '.') + 1;
		if (strchr(fn2, '.'))
			fn2 = strchr(fn2, '.') + 1;
#ifdef SIGNIFICANT_NAMES
		if (!strcmp(fn1, fn2))
#else
		if(1)
#endif
		{
			fld1 = getfldn(tb1, i, NULL);
			fld2 = getfldn(tb2, i, NULL);
			if(!fld1)
			{
				putmsg(MWARN, NULL, "Not enough fields in destination");
				bzzt = 1;
			}
			if(!bzzt && !fld2)
			{
				putmsg(MWARN, NULL, "Not enough fields in source");
				bzzt = 1;
			}
			if (!bzzt && !TXcompatibletypes(fld1->type, fld2->type))
			{
				putmsg(MERR, NULL,
			"Types for field %s are not compatible %s != %s",
			fn1, ddfttypename(fld1->type),
			ddfttypename(fld2->type));
				bzzt = 1;
			}
		}
		else
			bzzt = 1;
		i++;
		fn1 = getfldname(tb1, i);
		fn2 = getfldname(tb2, i);
	}
	if (!bzzt && !fn1 && !fn2)
	{
		return tbl1;
	}
	putmsg(MERR, NULL, "Table formats are incompatible");
	return (DBTBL *)NULL;
}

/****************************************************************************/

int
tup_append(tbl1, tbout, fo)
DBTBL *tbl1;
DBTBL *tbout;
FLDOP *fo;
{
/*
	Tables need to be sorted so we can do a merge sort, which will
	effectively give us the union without duplicates.
*/
	if(!TXrecidvalid(tup_write(tbout, tbl1, fo, 0)))
		return -1;
	else
		return 0;
}

/****************************************************************************/

