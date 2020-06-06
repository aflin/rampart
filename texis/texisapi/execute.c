#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"

/************************************************************************/

/*  Execute a prepared SQL statement */

static int allowrexec = 0;

int TXsetallowrexec(int r);
int
TXsetallowrexec(int r)
{
	int	rc;

	rc = allowrexec;
	allowrexec = r;
	return rc;
}

/* ------------------------------------------------------------------------ */

int
TXsqlGetCountInfo(HSTMT hstmt, TXCOUNTINFO *countInfo)
/* Gets index/row counts from `hstmt'.
 * Returns 0 on error, 1 on success.
 */
{
	QNODE	*query;

	query = ((LPSTMT)hstmt)->query;
	if (!query) goto err;

	if (countInfo)
		*countInfo = query->countInfo;
	return(1);				/* success */

err:
	if (countInfo)
		TX_CLEAR_COUNTINFO(countInfo);
	return(0);				/* error */
}

/* ------------------------------------------------------------------------ */

RETCODE SQL_API SQLExecute(hstmt)
HSTMT	hstmt;			/* statement to execute. */
{
	static const char	fn[] = "SQLExecute";
	LPSTMT	lpstmt = (LPSTMT) hstmt;
	int	results, i;

	lpstmt->nrows = 0;
	memset(TXApp->didOncePerSqlMsg, 0, TXoncePerSqlMsg_NUM);
	TXfdbiResetGlobalHacks();

	if (TXApp->failIfIncompatible &&
	    TXApp->setCompatibilityVersionFailed)
	{
		putmsg(MERR, fn, TXfailIfIncompatibleMsg);
		return(SQL_ERROR);
	}

	if (lpstmt->nparams && lpstmt->query)
	{
		if (TXparamunset(lpstmt->query,
				 TXgetDiscardUnsetParameterClauses()))
		{
			putmsg(MERR, NULL, "There are missing required parameters");
			return SQL_ERROR;
		}
#ifdef TEST_REPREP
		if(!lpstmt->query->q || !allowrexec)
#endif
		if (lpstmt->outtbl != (DBTBL *)NULL)
			lpstmt->outtbl = closedbtbl(lpstmt->outtbl);
		if (lpstmt->result != (SRESULT *)NULL)
		{
			free(lpstmt->result);
			lpstmt->result = (SRESULT *)NULL;
		}
		if (TXneeddata(lpstmt->query,
			       TXgetDiscardUnsetParameterClauses()))
			return SQL_NEED_DATA;
#ifdef TEST_REPREP
		if(lpstmt->query->q && allowrexec)
			lpstmt->outtbl = TXrepreparetree(lpstmt->dbc->ddic,
				lpstmt->query, lpstmt->fo, &results, NULL);
		else
#endif
		{
			TXunpreparetree(lpstmt->query);
			lpstmt->outtbl = TXpreparetree(lpstmt->dbc->ddic,
				lpstmt->query, lpstmt->fo, &results, NULL);
		}
		/* KNG 011210 set prevneeddata for next TXunpreparetree() */
		for (i = 0; i < lpstmt->nparams; i++)
			lpstmt->param[i].prevneeddata = lpstmt->param[i].needdata;
		if (results)
		{
			lpstmt->noresults = 1;
		}
		else
		{
			if (lpstmt->outtbl == (DBTBL *)NULL)
				return SQL_ERROR;
			lpstmt->result = (SRESULT *)
				calloc(lpstmt->outtbl->tbl->n, sizeof(SRESULT));
			if(lpstmt->boundcols == NULL)
				lpstmt->boundcols = (SBOUNDCOL *)
					calloc(lpstmt->outtbl->tbl->n, sizeof(SBOUNDCOL));
		}
	}
	TXresettimecache();
	if (lpstmt->noresults || (lpstmt->query && lpstmt->outtbl))
	{
#ifdef SQLLOGGING
		TXlogsqlstatement(lpstmt);
#endif
		return SQL_SUCCESS;
	}
	return SQL_ERROR;
}

/************************************************************************/

/*  Performs the equivalent of SQLPrepare, followed by SQLExecute. */

RETCODE SQL_API
SQLExecDirect(hstmt, sqlstr, cbsqlstr)
HSTMT	hstmt;
UCHAR FAR *sqlstr;
SDWORD	cbsqlstr;
{
	LPSTMT lpstmt = (LPSTMT)hstmt;

	if (SQLPrepare(lpstmt, sqlstr, cbsqlstr) == SQL_ERROR)
		return SQL_ERROR;
	return SQLExecute(lpstmt);
}

/************************************************************************/

/*  Returns the SQL string as modified by the driver. */

RETCODE SQL_API SQLNativeSql(hstmt, szSqlStrIn, cbSqlStrIn, szSqlStr, cbSqlStrMax, pcbSqlStr)
HSTMT	hstmt;
UCHAR FAR *szSqlStrIn;
SDWORD	cbSqlStrIn;
UCHAR FAR *szSqlStr;
SDWORD	cbSqlStrMax;
SDWORD FAR *pcbSqlStr;
{
	(void)hstmt;
	(void)szSqlStrIn;
	(void)cbSqlStrIn;
	(void)szSqlStr;
	(void)cbSqlStrMax;
	(void)pcbSqlStr;
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Supplies parameter data at execution time.  Used in conjuction with
    SQLPutData. */

RETCODE SQL_API SQLParamData(hstmt, prbgValue)
HSTMT	hstmt;
PTR FAR *prbgValue;
{
	LPSTMT lpstmt = (LPSTMT) hstmt;
	PARAM	*p;

	if (lpstmt->nparams && lpstmt->query)
	{
		p = TXneeddata(lpstmt->query,
			       TXgetDiscardUnsetParameterClauses());
		if (p)
		{
			*prbgValue = p->usr;
			lpstmt->curp = p;
			return SQL_NEED_DATA;
		}
	}
	return SQLExecute(hstmt);
}

/************************************************************************/

/*  Supplies parameter data at execution time.  Used in conjunction with
    SQLParamData. This adds the data to the data currently in the
    parameter. */

RETCODE SQL_API SQLPutData(hstmt, rgbValue, cbValue)
HSTMT	hstmt;
PTR	rgbValue;
SDWORD  cbValue;
{
	LPSTMT	lpstmt = hstmt;
	PARAM	*p = lpstmt->curp;
	FLD	*f = p->fld;
	int	nsize;
	char	*nbuf;
	void	*v;

	if (cbValue == SQL_NTS)
		cbValue = strlen(rgbValue);
	v = getfld(f, NULL);
	if (v == (void *)NULL)
	{
		nbuf = malloc((size_t)cbValue);
		memcpy(nbuf, rgbValue, (size_t)cbValue);
		putfld(f, nbuf, cbValue/f->elsz);
	}
	else
	{
		nsize = f->size + (size_t)cbValue;
		nbuf = malloc(nsize);
		memcpy(nbuf, v, f->size);
		memcpy(nbuf+f->size, rgbValue, (size_t)cbValue);
		freeflddata(f);
		putfld(f, nbuf, nsize / f->elsz);
	}
	p->needdata = 0;
	return SQL_SUCCESS;
}

/************************************************************************/

RETCODE SQL_API SQLCancel(hstmt)
HSTMT	hstmt;		/* Statement to cancel. */
/* Asynchronously cancels currently-running SQL transaction.
 * Returns SQL_SUCCESS on success.
 * Thread-safe.  Signal-safe.
 */
{
	LPSTMT	lpstmt = hstmt;

	/*	TXquitqnode(lpstmt->query); */	/* signal-unsafe; noop */
	if (!lpstmt || !lpstmt->dbc || !lpstmt->dbc->ddic)
		return(SQL_INVALID_HANDLE);
	ddicsetstate(lpstmt->dbc->ddic, DDIC_STOPPING);
	return SQL_SUCCESS;
}

/************************************************************************/
