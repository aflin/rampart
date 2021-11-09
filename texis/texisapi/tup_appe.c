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

TXbool
TXcompatibletypes(FTN type1, FTN type2)
{
	FTN ttype1, ttype2;
	FTN tt;

	ttype1 = type1 & DDTYPEBITS;
	ttype2 = type2 & DDTYPEBITS;

	if(ttype1 == ttype2)
		return TXbool_True;
	if((ttype1 == FTN_BLOB || ttype1 == FTN_BLOBZ) && ttype2 == FTN_BLOBI)
		return TXbool_True;
	if(ttype1 == FTN_BLOBI && (ttype2 == FTN_BLOB || ttype2 == FTN_BLOBZ))
		return TXbool_True;
	/* Sort types; we are assuming commutativity, this saves us
	 * checking each type pair twice:
	 */
	if(ttype2 < ttype1)
	{
		tt = ttype1;
		ttype1 = ttype2;
		ttype2 = tt;
	}
	if (TXftnIsNumeric(ttype1) &&
	    TXftnIsNumeric(ttype2))
		return TXbool_True;
	switch (ttype1)
	{
		case FTN_BYTE:
			switch(ttype2)
			{
				case FTN_CHAR:
					return TXbool_True;
				default: return TXbool_False;
			}
		case FTN_CHAR:
			switch(ttype2)
			{
				case FTN_BLOB:
				case FTN_BLOBI:
					return TXbool_True;
				default: return TXbool_False;
			}
		default:
			return TXbool_False;
	}
	return TXbool_False;
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
