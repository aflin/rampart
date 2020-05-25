#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "mmsg.h"

int
TXaddindexrec(ddic, iname, tbname, fname, order, unique, fields, itype, params, recidp)
DDIC *ddic;
char *iname;
char *tbname;
char *fname;
int order;
int unique;
char *fields;
int itype;
char *params;
RECID   *recidp;
/* Returns 1 on success, 0 on error.  Sets `*recidp' to inserted row.
 */
{
	TBL *tb;
#if 0
	BTREE *bt;
	BTLOC loc;
#endif
	FLD *f1, *f2, *f3, *f4, *f5, *f6, *f7, *f8;
        RECID   *rowp;
	char co=order, cu, ci=itype;

        if (recidp != RECIDPN) TXsetrecid(recidp, -1);
#if 0
	bt = openbtree("SYS4", BT_MAXPGSZ, 20, BT_UNIQUE);
	loc = btsearch(bt, strlen(iname), iname);
	if (loc.loc != -1)
	{
		closebtree(bt);
		return(0);			/* error */
	}
#endif
	cu = unique ? 0 : 1;
	tb = ddic->indextbl;
	if (tb == (TBL *)NULL)
	{
		putmsg(TDBDOING, "Addindex", "No index TABLE");
		return(0);			/* error */
	}
	f1 = nametofld(tb, "NAME");
	f2 = nametofld(tb, "TBNAME");
	f3 = nametofld(tb, "FNAME");
	f4 = nametofld(tb, "COLLSEQ");
	f5 = nametofld(tb, "NON_UNIQUE");
	f6 = nametofld(tb, "FIELDS");
	f7 = nametofld(tb, "TYPE");
	f8 = nametofld(tb, "PARAMS");

	putfld(f1, iname, strlen(iname));
	putfld(f2, tbname, strlen(tbname));
	putfld(f3, fname, strlen(fname));
	putfld(f4, &co, 1);
	putfld(f5, &cu, 1);
	putfld(f6, fields, strlen(fields));
	putfld(f7, &ci, 1);
	if(f8)
		putfld(f8, params, strlen(params));
	else
	{
		if(params[0] != '\0')
			putmsg(MWARN, NULL, "Could not store parameters in SYSINDEX due to old format");
	}

	if (TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL) == -1) return(0);
	rowp = puttblrow(tb, NULL);
	TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
	if(rowp == RECIDPN || !TXrecidvalid(rowp))
	{
		putmsg(MERR, NULL, "Could not write to SYSINDEX");
		TXdelindex(fname, itype);       /*WTF caller should do this?*/
		return(0);
	}
	if (recidp != RECIDPN) *recidp = *rowp;
#if 0
	btinsert(bt, loc, strlen(iname), iname);
	closebtree(bt);
#endif
	return(1);				/* success */
}

int
TXdelindexrec(ddic, recid)
DDIC	*ddic;
RECID	recid;
/* Deletes SYSINDEX `recid', previously inserted by TXaddindexrec().
 * Returns 1 if ok, 0 on error.
 */
{
	static CONST char	fn[] = "TXdelindexrec";
	TBL			*tb;
	int			res;

	tb = ddic->indextbl;
	if (tb == (TBL *)NULL)
	{
		putmsg(TDBDOING, fn, "No index TABLE");
		return(0);			/* error */
	}
	if (TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL) == -1) return(0);
	res = deltblrow(tb, &recid);
	TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
	if (!res)
	{
		putmsg(MERR, fn, "Could not delete from SYSINDEX");
		return(0);
	}
	return(1);				/* ok */
}

int
TXdeleteSysindexEntry(ddic, tableName, indexName, type)
DDIC		*ddic;
CONST char	*tableName;	/* (in) table name */
CONST char	*indexName;	/* (in) index name */
int		type;		/* (in, opt.) INDEX_... type */
/* Deletes SYSINDEX entry for `name' (and optionally of `type').
 * Returns number of rows deleted plus 1, or 0 on error.
 */
{
	FLD	*nameFld, *typeFld;
	RECID	*at;
	int	numFound = 0, gotErr = 0;

	if (!ddic->indextbl) return(0);
	nameFld = nametofld(ddic->indextbl, "NAME");
	typeFld = nametofld(ddic->indextbl, "TYPE");
	if (!nameFld || !typeFld) return(0);

	if (TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL) == -1)
		return(0);
	rewindtbl(ddic->indextbl);
	while (TXrecidvalid(at = gettblrow(ddic->indextbl, NULL)))
		if (strcmp(getfld(nameFld, NULL), indexName) == 0 &&
		    (type == 0 ||
		     (int)(*(char *)getfld(typeFld, NULL)) == type))
		{
			numFound++;
			if (!freedbf(ddic->indextbl->df, TXgetoff(at)))
			{
				gotErr++;
				break;
			}
		}
	/* Close any cached indexes we have on the table we indexed,
	 * since we may have just changed one of them:
	 */
	TXclosecacheindex(ddic, (char *)tableName);
	/* If no locks, makevalidtable() will not be able to detect
	 * that SYSINDEX has changed, and thus our SYSINDEX cache will
	 * stay stale.  Invalidate the cache now, to force refresh at
	 * next use:
	 */
	if (!ddic->dblock)
		ddic->indtblcache->tbl = closetbl(ddic->indtblcache->tbl);
	TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
	return(gotErr ? 0 : numFound);
}
