#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "http.h" /* For HTBUF */

/************************************************************************/

int
TXlogsqlinit(DDIC *ddic)
{
	int rc;
	char *fname, ct;

	fname = ddgettable(ddic, TEXISSYSLOGSQL, &ct, 1);
	if(fname)
	{
		ddic->options[DDIC_OPTIONS_LOG_SQL_STATEMENTS] = 1;
		rc = 0;
	}
	else
	{
		ddic->options[DDIC_OPTIONS_LOG_SQL_STATEMENTS] = 0;
		rc = -1;
	}
	return rc;
}

/************************************************************************/

int
TXlogsqlstatement(LPSTMT lpstmt)
{
	DDIC *ddic;
	const char	*uFmt;

	uFmt = (TX_LEGACY_VERSION_7_URL_CODING_CURRENT(TXApp) ? "%U" : "%qU");

	ddic = lpstmt->dbc->ddic;
	if(ddic->options[DDIC_OPTIONS_LOG_SQL_STATEMENTS] && (ddic->ihstmt != lpstmt) && !lpstmt->noresults)
	{
		HTBUF *buf = HTBUFPN;
		char *params = "";
		int rc;
		long szsqlstr, szparams;

		if(TXddicstmt(ddic) == -1)
			return -1;
		if(lpstmt->nparams > 0)
		{
			int i;

			buf = openhtbuf();
			if(!buf)
			{
				putmsg(MERR, NULL, "Couldn't Log SQL Statement");
				return -1;
			}
			for (i = 1; i <= lpstmt->nparams; i++)
			{
				htbuf_pf(buf, "&P%d=", i);
				htbuf_pf(buf, uFmt,
					 fldtostr(lpstmt->param[i].fld));
			}
			htbuf_getdata(buf, &params, 0);
			if(params)
				params++;
		}
		rc = SQLPrepare(ddic->ihstmt, (byte *)"INSERT INTO SYSLOGSQL (SQLID, SQL, SQLPARAMS) VALUES(counter, ?, ?);", SQL_NTS);
		szsqlstr = strlen(lpstmt->sqlstr);
		SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0, lpstmt->sqlstr, &szsqlstr);
		szparams = strlen(params);
		SQLSetParam(ddic->ihstmt, 2, SQL_C_CHAR, SQL_CHAR, 0, 0, params, &szparams);
		SQLExecute(ddic->ihstmt);
		while(SQLFetch(ddic->ihstmt) == SQL_SUCCESS);
#ifdef NEVER
		putmsg(MINFO, NULL, "SQLSTR [%d]: INSERT INTO %s (SQLID, SQL, SQLPARAMS) VALUES (counter, %s, %s);",
			lpstmt->noresults, TEXISSYSLOGSQL, lpstmt->sqlstr, params);
#endif
		if(buf)
		{
			buf = closehtbuf(buf);
		}
	}
	return 0;
}
