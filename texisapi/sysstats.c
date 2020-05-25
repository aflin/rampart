/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>
#ifdef unix
#  include <sys/time.h>
#  include <sys/resource.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

/*
	Maintain statistics about the database.  The following fields
	will be defined:

	id	counter	- unique identifier, updated with stat
	Object	varchar - Which object this applies to, e.g. TABLE.INDEX.COLUMN
	Stat	varchar - What we measure, e.g. NROWS, NBYTES
	NumVal	long  	- numeric value of statistic
	StrVal	varchar - String value of stat, or comment on NumVal.

	Need to be able to

		UPDATE/ADD stat.
		DELETE stat.
		RETRIEVE stats.
*/

/******************************************************************/

int
TXddicstmt(ddic)
DDIC *ddic;
{
	if(!ddic)
		return -1;
	if(!ddic->ihstmt)
	{
		if(!ddic->dbc)
		{
			LPDBC lpdbc;

			lpdbc = (LPDBC)calloc(1, sizeof(DBC));
			lpdbc->ddic = ddic;
			ddic->dbc = lpdbc;
			ddic->dbcalloced = 1;
			lpdbc->datasource = strdup(ddic->pname);
		}
		if(SQLAllocStmt(ddic->dbc, &ddic->ihstmt) != SQL_SUCCESS)
			return -1;
	}
	return 0;
}

int
TXddicBeginInternalStmt(const char *fn, DDIC *ddic)
/* Begins `ddic->ihstmt' use.
 * Returns 0 on error (do not call TXddicEndInternalStmt() on error).
 */
{
	if (TXddicstmt(ddic) == -1) return(0);
	if (ddic->ihstmtIsInUse)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn,
			       "Internal error: DDIC.ihstmt already in use");
		return(0);
	}
	ddic->ihstmtIsInUse++;
	return(1);
}

int
TXddicEndInternalStmt(DDIC *ddic)
/* Returns 0 on error.
 */
{
	if (ddic->ihstmtIsInUse != 1)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, CHARPN,
			       "Internal error: DDIC.ihstmtIsInUse != 1");
		return(0);
	}
	ddic->ihstmtIsInUse--;
	return(1);
}

/******************************************************************/

DBTBL *
TXcreatestatstable(ddic)
DDIC *ddic;
{
	DD *dd;
	DBTBL *dbtbl;
	int     svcr, svgr;

	TXpushid(ddic, 0, 0);	/* su so that _SYSTEM owns the table */
        svcr = stxalcrtbl(1);   /* allow create table */
        svgr = stxalgrant(1);   /* allow grant */

	if(!ddic)
		goto err;

	dd = opennewdd(5);
	if(!dd)
		goto err;


	if(
	   !putdd(dd, "id", "counter", 1, 1) ||
	   !putdd(dd, "Object", "varchar", 80, 1) ||
	   !putdd(dd, "Stat",   "varchar", 80, 1) ||
	   !putdd(dd, "NumVal", "long",   1, 0) ||
	   !putdd(dd, "StrVal", "varchar", 80, 0))
	{
		closedd(dd);
		goto err;
	}
	(void)ddsettype(dd, TEXIS_FAST_TABLE);
	dbtbl = createdbtbl(ddic, dd, TEXISFNSYSSTATS, TEXISSYSSTATS,
		"Database Statistics", 'S');
	closedd(dd);
	if(!dbtbl) goto err;
	if (dbgetperms(dbtbl, ddic) >= 0)
		permgrant(ddic, dbtbl, "PUBLIC", PM_SELECT);
        goto done;

err:
        dbtbl = NULL;
done:
	stxalcrtbl(svcr);		/* release su perms */
	stxalgrant(svgr);
	TXpopid(ddic);
	return dbtbl;			/* note: has su perms */
}

/******************************************************************/

int
TXsetstatistic(ddic, object, stat, numval, strval, cum)
DDIC *ddic;
char *object;
char *stat;
long numval;
char *strval;
int  cum;
/* Sets SYSSTATISTICS.Stat value named `stat' to `numval'/`strval'.
 * If `cum' is nonzero, will add to existing numeric value if present.
 * Some `object'/`stat' values are:
 *   tbl        NROWS   Row count (MM index)
 *   tbl.idx    NROWS   ""
 *   tbl        ROWG/B  Full row size read from table (MM index)
 *   tbl.idx    DATAG/B Data size actually indexed (MM index)
 *   tbl.idx    CHKIND  New (unindexed) data size for index, from chkind
 * Returns 0 if ok, -1 on error.
 */
{
#ifndef NO_KEEP_STATS
	static const char	fn[] = "TXsetstatistic";
	long sz;
	int rc, och, ret;
	char *tbname = NULL, tableType;

	if (!TXddicBeginInternalStmt(fn, ddic)) return(-1);
	tbname = ddgettable(ddic, TEXISSYSSTATS, &tableType, 0);
	if(!tbname)
		closedbtbl(TXcreatestatstable(ddic));
	else
		tbname = TXfree(tbname);

	TXpushid(ddic, 0, 0);			/* su to _SYSTEM */
        och = ddic->ch;
	ddic->ch = 0;				/* don't count license hits */
	if(cum)
		rc = SQLPrepare(ddic->ihstmt, (byte *)"UPDATE " TEXISSYSSTATS " SET id = counter, NumVal = NumVal + ?, StrVal = ? WHERE Object = ? and Stat = ?;", SQL_NTS);
	else
		rc = SQLPrepare(ddic->ihstmt, (byte *)"UPDATE " TEXISSYSSTATS " SET id = counter, NumVal = ?, StrVal = ? WHERE Object = ? and Stat = ?;", SQL_NTS);
	if(rc != SQL_SUCCESS) goto err;

	sz = sizeof(long);
	SQLSetParam(ddic->ihstmt, 1, SQL_C_LONG, SQL_INTEGER, 15, 0, &numval, &sz);
	sz = strlen(strval);
	SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0, strval, &sz);
	sz = strlen(object);
	SQLSetParam(ddic->ihstmt, 3, SQL_C_CHAR, SQL_CHAR, 0, 0, object, &sz);
	sz = strlen(stat);
	SQLSetParam(ddic->ihstmt, 4, SQL_C_CHAR, SQL_CHAR, 0, 0, stat, &sz);
	if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS ||
	    SQLFetch(ddic->ihstmt) == SQL_NO_DATA_FOUND)
	{
		if(SQLPrepare(ddic->ihstmt, (byte *)"INSERT INTO " TEXISSYSSTATS " (id, NumVal, StrVal, Object, Stat) VALUES (counter, ?, ?, ?, ?);", SQL_NTS) != SQL_SUCCESS)
		  goto err;
		sz = sizeof(long);
		SQLSetParam(ddic->ihstmt, 1, SQL_C_LONG, SQL_INTEGER, 15, 0, &numval, &sz);
		sz = strlen(strval);
		SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0, strval, &sz);
		sz = strlen(object);
		SQLSetParam(ddic->ihstmt, 3, SQL_C_CHAR, SQL_CHAR, 0, 0, object, &sz);
		sz = strlen(stat);
		SQLSetParam(ddic->ihstmt, 4, SQL_C_CHAR, SQL_CHAR, 0, 0, stat, &sz);
		if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS ||
		    SQLFetch(ddic->ihstmt) != SQL_SUCCESS)
			goto err;
	}
	ret = 0;
	goto done;
err:
	ret = -1;
done:
	ddic->ch = och;
	TXpopid(ddic);
	TXddicEndInternalStmt(ddic);
	return ret;
#endif
}

/******************************************************************/

int
TXdelstatistic(ddic, object, stat)
DDIC *ddic;
char *object;	/* (in) object, e.g. "table" or "table.index" */
char *stat;	/* (in, opt.) specific statistic to delete */
/* Removes statistic `stat' for `object' in SYSSTATISTICS table, if it exists.
 * Returns 0 on success (or stat not found), -1 on error.
 */
{
	static const char	fn[] = "TXdelstatistic";
	long sz;
	int rc, och, ret;
	char	*tbname = NULL, tableType;
	char	stmt[256];

	if (!TXddicBeginInternalStmt(fn, ddic)) return(-1);
	tbname = ddgettable(ddic, TEXISSYSSTATS, &tableType, 0);
	if (!tbname)
		closedbtbl(TXcreatestatstable(ddic));
	else
		tbname = TXfree(tbname);

	TXpushid(ddic, 0, 0);			/* su to _SYSTEM */
        och = ddic->ch;
	ddic->ch = 0;				/* don't count license hits */

	strcpy(stmt, "DELETE FROM " TEXISSYSSTATS " WHERE Object = ?");
	if (stat) strcat(stmt, " AND Stat = ?");
	strcat(stmt, ";");
	rc = SQLPrepare(ddic->ihstmt, (byte *)stmt, SQL_NTS);
	if (rc != SQL_SUCCESS) goto err;

	sz = strlen(object);
	SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0, object, &sz);
	if (stat)
	{
		sz = strlen(stat);
		SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0,
			    stat, &sz);
	}
	if (SQLExecute(ddic->ihstmt) != SQL_SUCCESS) goto err;
	while ((rc = SQLFetch(ddic->ihstmt)) == SQL_SUCCESS);
	switch (rc)
	{
	case SQL_NO_DATA_FOUND:	break;		/* nothing to delete */
	case SQL_SUCCESS:	break;
	default:		goto err;
	}

	ret = 0;				/* success */
	goto done;
err:
	ret = -1;
done:
	ddic->ch = och;
	TXpopid(ddic);
	TXddicEndInternalStmt(ddic);
	return(ret);
}

/******************************************************************/

int
TXgetstatistic(ddic, object, stat, id, longval, strval)
DDIC *ddic;
char *object;
char *stat;
ft_counter *id;
ft_long *longval;
char **strval;
{
	static const char	fn[] = "TXgetstatistic";
	long sz;
	DBTBL *rtable;
	FLD *idf, *lf, *sf;
	int och = 0, pop = 0, ret;

#ifndef NO_KEEP_STATS
	if (!TXddicBeginInternalStmt(fn, ddic)) goto errout;
	TXpushid(ddic, 0, 0);			/* su to _SYSTEM */
	och = ddic->ch; ddic->ch = 0;		/* don't count license hits */
	pop = 1;
	if(SQLPrepare(ddic->ihstmt, (byte *)"SELECT id, NumVal, StrVal from " TEXISSYSSTATS " WHERE Object = ? and Stat = ?;", SQL_NTS) != SQL_SUCCESS)
	  goto errout;
	sz = strlen(object);
	SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0, object, &sz);
	sz = strlen(stat);
	SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0, stat, &sz);
	SQLExecute(ddic->ihstmt);
	if(SQLFetch(ddic->ihstmt) == SQL_NO_DATA_FOUND) goto errout;
	rtable = ((LPSTMT)ddic->ihstmt)->outtbl;
	idf = dbnametofld(rtable, "id");
	lf = dbnametofld(rtable, "NumVal");
	sf = dbnametofld(rtable, "StrVal");
	if(!idf || !lf || !sf)
		goto errout;
	if(id)
		*id = *(ft_counter *)getfld(idf, NULL);
	if(longval)
		*longval = *(ft_long *)getfld(lf, NULL);
	if(strval)
		*strval = strdup(getfld(sf, NULL));
#endif
	ret = 0;
	goto done;
errout:
	ret = -1;
	if(id)
		memset(id, 0, sizeof(ft_counter));
	if(longval)
		*longval = 0;
	if(strval)
		*strval = NULL;
done:
	if (pop)
	{
		ddic->ch = och;
		TXpopid(ddic);
		TXddicEndInternalStmt(ddic);
	}
	return ret;
}
