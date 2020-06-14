#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"

/************************************************************************/

/*	This returns the number of columns associated with the database
 *	attached to "lpstmt".
 */

RETCODE SQL_API SQLNumResultCols(hstmt, pccol)
HSTMT	hstmt;
SWORD FAR *pccol;
{
	LPSTMT lpstmt = (LPSTMT)hstmt;

	if (lpstmt->outtbl == (DBTBL *)NULL ||
	    lpstmt->outtbl->tbl == (TBL *)NULL)
		*pccol = 0;
	else
		*pccol = lpstmt->outtbl->tbl->n;
	return SQL_SUCCESS;
}

/************************************************************************/

/*	Return information about the database column the user wants
 *	information about.
 */

#ifdef LINT_ARGS
RETCODE SQL_API SQLDescribeCol(
HSTMT	hstmt,
UWORD	icol,
UCHAR FAR *colname,
SWORD	colnamemax,
SWORD FAR *pcolname,
SWORD FAR *sqltype,
UDWORD FAR *coldef,
SWORD FAR *scale,
SWORD FAR *nullable)
#else
RETCODE SQL_API SQLDescribeCol(hstmt, icol, colname, colnamemax, pcolname, sqltype, coldef, scale, nullable)
HSTMT	hstmt;
UWORD	icol;
UCHAR FAR *colname;
SWORD	colnamemax;
SWORD FAR *pcolname;
SWORD FAR *sqltype;
UDWORD FAR *coldef;
SWORD FAR *scale;
SWORD FAR *nullable;
#endif
{
	DDFD *ddfd;
	byte type;
	LPSTMT lpstmt = (LPSTMT)hstmt;

	icol --;
	ddfd = getflddesc(lpstmt->outtbl->tbl, icol);
	TXstrncpy((char *)colname, ddfd->name, colnamemax);
	*pcolname = strlen((char *)colname);
	type = ddfd->type & DDTYPEBITS;
	*sqltype = dbttosqlt(type);
	switch (*sqltype)
	{
		case SQL_CHAR :
		case SQL_BINARY :
			*coldef = (unsigned long)ddfd->size;
			*coldef = 2000;
			break;
		default :
			*coldef = (unsigned long)20;
			break;
	}
	*scale = 0;
	*nullable = (TXftnIsNotNullable(ddfd->type) ? SQL_NO_NULLS :
		     SQL_NULLABLE);
	return SQL_SUCCESS;
}

/************************************************************************/

/*	Returns result column descriptor information for a result set.  */

#ifdef LINT_ARGS
RETCODE SQL_API SQLColAttributes(
HSTMT	hstmt,
UWORD	icol,
UWORD   fDescType,
PTR	rgbDesc,
SWORD	cbDescMax,
SWORD FAR *pcbDesc,
SDWORD FAR *pfDesc)
#else
RETCODE SQL_API SQLColAttributes(hstmt, icol, fDescType, rgbDesc, cbDescMax, pcbDesc, pfDesc)
HSTMT	hstmt;
UWORD	icol;
UWORD   fDescType;
PTR	rgbDesc;
SWORD	cbDescMax;
SWORD FAR *pcbDesc;
SDWORD FAR *pfDesc;
#endif
{
    LPSTMT lpstmt = hstmt;
    switch(fDescType)
    {
    	case SQL_COLUMN_COUNT:
	{
	    SWORD t;

	    SQLNumResultCols(hstmt, &t);
	    *pfDesc = t;
	    break;
	}
	case SQL_COLUMN_NAME:
	{
	    DDFD *ddfd;

	    icol --;
	    ddfd = getflddesc(lpstmt->outtbl->tbl, icol);
	    TXstrncpy((char *)rgbDesc, ddfd->name, cbDescMax);
	    *pcbDesc = strlen((char *)rgbDesc);
	    break;
	}
	case SQL_COLUMN_TYPE:
	{
	    DDFD *ddfd;
	    byte type;

	    icol --;
	    ddfd = getflddesc(lpstmt->outtbl->tbl, icol);
	    type = ddfd->type & DDTYPEBITS;
	    *pfDesc = dbttosqlt(type);
	    break;
	}
	case SQL_COLUMN_LENGTH:
	case SQL_COLUMN_DISPLAY_SIZE:
	{
	    DDFD *ddfd;
	    SDWORD sqltype;
	    byte type;

	    icol --;
	    ddfd = getflddesc(lpstmt->outtbl->tbl, icol);
	    type = ddfd->type & DDTYPEBITS;
	    sqltype = dbttosqlt(type);
	    switch (sqltype)
	    {
		case SQL_CHAR :
		case SQL_BINARY :
			*pfDesc = (unsigned long)ddfd->size;
			break;
		default :
			*pfDesc = (unsigned long)20;
			break;
	    }
	    break;
	}
	default:
		return SQL_ERROR;
    }
    return SQL_SUCCESS;
}

/************************************************************************/

/*	Associate a user-supplied buffer with a database column.        */

#ifdef LINT_ARGS
RETCODE SQL_API SQLBindCol(
HSTMT	hstmt,
UWORD	icol,
SWORD	fCType,
PTR	rgbValue,
SDWORD	cbValueMax,
SDWORD FAR *pcbValue)
#else
RETCODE SQL_API SQLBindCol(hstmt, icol, fCType, rgbValue, cbValueMax, pcbValue)
HSTMT	hstmt;
UWORD	icol;
SWORD	fCType;
PTR	rgbValue;
SDWORD	cbValueMax;
SDWORD FAR *pcbValue;
#endif
{
	static char Fn[] = "SQLBindCol";
	LPSTMT lpstmt = hstmt;

	switch(fCType)
	{
		case SQL_C_CHAR:
			if(lpstmt->boundcols)
			{
				lpstmt->boundcols[icol-1].type = fCType;
				lpstmt->boundcols[icol-1].data = rgbValue;
				lpstmt->boundcols[icol-1].datasz = cbValueMax;
				lpstmt->boundcols[icol-1].adatasz = pcbValue;
				break;
			}
			else
			{
				putmsg(MINFO, Fn, "Boundcols not alloced");
			}
		default:
	putmsg(MINFO, Fn, "Unsupported (hstmt=%lx, icol=%d, fCType=%d, rgbValue=%lx, cbValueMax=%d, pcbValue=%lx", hstmt, icol, fCType, rgbValue, cbValueMax, pcbValue);
	}
	return SQL_SUCCESS;
}

/************************************************************************/

/*	Returns data for bound columns in the current row ("lpstmt->iCursor"),
 *	advances the cursor.
 */

#ifdef LINT_ARGS
RETCODE SQL_API SQLFetchScroll(
	HSTMT	hstmt,
	SWORD	direction,
	SDWORD	offset)
#else
RETCODE SQL_API SQLFetchScroll(hstmt, direction, offset)
HSTMT	hstmt;
SWORD	direction;
SDWORD	offset;
#endif
{
	static const char	fn[] = "SQLFetchScroll";
	LPSTMT lpstmt = (LPSTMT)hstmt;
	FLDOP *fo = lpstmt->fo;
	int   rc, trunc = 0;
	DDIC	*ddic = DDICPN;

	if (TXApp->failIfIncompatible &&
	    TXApp->setCompatibilityVersionFailed)
	{
		putmsg(MERR, fn, TXfailIfIncompatibleMsg);
		return(SQL_ERROR);
	}

	if (lpstmt->noresults)
		return SQL_NO_DATA_FOUND;
	if (lpstmt->outtbl)
	{
		ddic = lpstmt->outtbl->ddic;
		/* WTF - JMT - Can we free anything currently in outtbl
		   here?  This would give considerable savings in memory
		   on retrieval maybe needs to be flagged if it is a
		   cursor don't free, otherwise do */
		if (lpstmt->query)
		{
			TXdeltmprow(lpstmt->outtbl);
			rc = TXdotree(lpstmt->query, fo, direction, offset);
			lpstmt->nrows = lpstmt->query->q->nrows;
			if (ddic && ddic->messages[MESSAGES_DUMP_QNODE_FETCH])
			{
				HTBUF	*buf = openhtbuf();
				char	*s;

				TXdumpqnode(buf, ddic, lpstmt->query, NULL, fo, 0);
				htbuf_getdata(buf, &s, 0);
				putmsg(MINFO, CHARPN, "QNODE tree after fetch:\n%s", s);
				buf = closehtbuf(buf);
			}
		}
		else
		{
			RECID	*r = NULL;
			int	skipped = 0;
			int	toskip = 0;

			switch(direction)
			{
			case SQL_FETCH_ABSOLUTE:
				rewindtbl(lpstmt->outtbl->tbl);
			case SQL_FETCH_RELATIVE:
				if(offset == 0)
				{
					rc = -1;
					break;
				}
				for(toskip = offset -1;
				    skipped < toskip;
				    toskip --, skipped++)
				{
					r=gettblrow(lpstmt->outtbl->tbl, NULL);
					if(!TXrecidvalid(r))
						break;
				}
			case SQL_FETCH_NEXT:
				r = gettblrow(lpstmt->outtbl->tbl, NULL);
				break;
			default:
				rc = -1;
			}
			if (TXrecidvalid(r))
			{
				rc = 0;
				lpstmt->nrows += 1;
			}
			else
				rc = -1;
		}
#ifdef NEVER
		putmsg(999, "SQLFetch", "rc = %d, nrows = %d, boundcols = %lx",
			rc, lpstmt->nrows, lpstmt->boundcols);
#endif
		switch(rc)
		{
			case -1:
				return SQL_NO_DATA_FOUND;
			case -2:
				return SQL_ERROR;
			default:
			if(lpstmt->boundcols)
			{
				size_t i;

				for(i = 0;
				    i < lpstmt->outtbl->tbl->n;
				    i++)
				{
					if(lpstmt->boundcols[i].data &&
					   (lpstmt->boundcols[i].type ==
					   SQL_C_CHAR))
					{
						char *b;
						size_t sz;

						b = fldtostr(nametofld(lpstmt->outtbl->tbl, getfldname(lpstmt->outtbl->tbl, i)));
						sz = strlen(b);
						*lpstmt->boundcols[i].adatasz = sz;
						if(lpstmt->boundcols[i].data)
							TXstrncpy(lpstmt->boundcols[i].data, b, lpstmt->boundcols[i].datasz);
						if(sz > (size_t)lpstmt->boundcols[i].datasz - 1)
							trunc ++;
#ifdef NEVER
						putmsg(MINFO, "SQLFetch", "Col %d binding string %s || %s (%d/%d)", (int)i+1, b, lpstmt->boundcols[i].data, sz, lpstmt->boundcols[i].datasz);
#endif
					}
				}
			}
			return SQL_SUCCESS;
#ifdef NEVER
			return trunc ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
#endif
		}
	}
	return SQL_ERROR;
}

RETCODE SQL_API SQLFetch(hstmt)
HSTMT	hstmt;
{
	return SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 1);
}

/*  Returns result data for a single column in the current row. */

#ifdef LINT_ARGS
RETCODE SQL_API SQLGetData(
HSTMT	hstmt,
UWORD	icol,
SWORD	fCType,
PTR	    rgbValue,
SDWORD	cbValueMax,
SDWORD FAR *pcbValue)
#else
RETCODE SQL_API SQLGetData(hstmt, icol, fCType, rgbValue, cbValueMax, pcbValue)
HSTMT	hstmt;
UWORD	icol;
SWORD	fCType;
PTR	    rgbValue;
SDWORD	cbValueMax;
SDWORD FAR *pcbValue;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*	This determines whether there are more results sets available for
 *  the "lpstmt".
 */

RETCODE SQL_API SQLMoreResults(hstmt)
HSTMT	hstmt;
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*	This returns the number of rows associated with the database
 *	attached to "lpstmt".
 */

RETCODE SQL_API SQLRowCount(hstmt, pcrow)
HSTMT	hstmt;
SDWORD FAR *pcrow;
{
	LPSTMT	lpstmt = hstmt;

	*pcrow = lpstmt->nrows;
	return SQL_SUCCESS;
}

/************************************************************************/

/*	This positions the cursor within a block of data. */

#ifdef LINT_ARGS
RETCODE SQL_API SQLSetPos(
HSTMT	hstmt,
UWORD   irow,
UWORD    fRefresh,
UWORD    fLock)
#else
RETCODE SQL_API SQLSetPos(hstmt, irow, fRefresh, fLock)
HSTMT	hstmt;
UWORD   irow;
UWORD    fRefresh;
UWORD    fLock;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*	This fetchs a block of data (rowset). */

#ifdef LINT_ARGS
RETCODE SQL_API SQLExtendedFetch(
HSTMT	hstmt,
UWORD   fFetchType,
SDWORD  irow,
UDWORD FAR *pcrow,
UWORD FAR *rgfRowStatus)
#else
RETCODE SQL_API SQLExtendedFetch(hstmt, fFetchType, irow, pcrow, rgfRowStatus)
HSTMT	hstmt;
UWORD   fFetchType;
SDWORD  irow;
UDWORD FAR *pcrow;
UWORD FAR *rgfRowStatus;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Returns the next SQL error information. */

#ifdef LINT_ARGS
RETCODE SQL_API SQLError(
HENV	henv,
HDBC	hdbc,
HSTMT	hstmt,
UCHAR FAR *szSqlState,
SDWORD FAR *pfNativeError,
UCHAR FAR *szErrorMsg,
SWORD	cbErrorMsgMax,
SWORD FAR *pcbErrorMsg)
#else
RETCODE SQL_API SQLError(henv, hdbc, hstmt, szSqlState, pfNativeError, szErrorMsg, cbErrorMsgMax, pcbErrorMsg)
HENV	henv;
HDBC	hdbc;
HSTMT	hstmt;
UCHAR FAR *szSqlState;
SDWORD FAR *pfNativeError;
UCHAR FAR *szErrorMsg;
SWORD	cbErrorMsgMax;
SWORD FAR *pcbErrorMsg;
#endif
{
	if(szSqlState)
		szSqlState[0] = '\0';
	return SQL_NO_DATA_FOUND;
}
