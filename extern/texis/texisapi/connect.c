#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"

/************************************************************************
 *
 *	CONNECT.C
 *
 *	SQLC connection functions.
 *
 ************************************************************************/

/*	Allocate an environment (ENV) block. */

RETCODE SQL_API SQLAllocEnv(phenv)
HENV FAR * phenv;
{
	LPENV	lpenv;

	lpenv = (LPENV)calloc(1, sizeof (ENV));
	if (lpenv == NULL)
	{
		*phenv = SQL_NULL_HENV;
		return SQL_ERROR;
	}
	*phenv = (HENV)lpenv;
	return SQL_SUCCESS;
}

/************************************************************************/

/*	Allocate a DBC block. */

RETCODE	SQL_API SQLAllocConnect(lpenv, phdbc)
HENV	lpenv;
HDBC FAR * phdbc;
{
	LPDBC	lpdbc;

	lpdbc = (LPDBC)calloc(1, sizeof (DBC));
	if (lpdbc == (LPDBC)NULL)
	{
		*phdbc = SQL_NULL_HENV;
		return SQL_ERROR;
	}
	lpdbc->env = lpenv;
	*phdbc = lpdbc;
	return SQL_SUCCESS;
}

/************************************************************************/

#ifdef LINT_ARGS
RETCODE SQL_API SQLConnect(
HDBC	hdbc,
UCHAR FAR *datasource,
SWORD	cbDSN,
UCHAR FAR *uid,
SWORD	cbUID,
UCHAR FAR *auth,
SWORD	cbAuthStr)
#else
RETCODE SQL_API SQLConnect(hdbc, datasource, cbDSN, uid, cbUID, auth, cbAuthStr)
HDBC	hdbc;
UCHAR FAR *datasource;
SWORD	cbDSN;
UCHAR FAR *uid;
SWORD	cbUID;
UCHAR FAR *auth;
SWORD	cbAuthStr;
#endif
{
	LPDBC dbc = (LPDBC)hdbc;
	int rc;

	dbc->ddic = ddopen((char *)datasource);
	dbc->datasource = strdup((char *)datasource);
	if (dbc->ddic == (DDIC *)NULL ||
		dbc->ddic->tabletbl == (TBL *)NULL ||
#ifdef OPEN_SYSCOLUMNS
		dbc->ddic->coltbl == (TBL *)NULL ||
#endif
		dbc->ddic->indextbl == (TBL *)NULL ||
		dbc->datasource == (char *)NULL)
		return SQL_ERROR;
	if (cbUID == 0)
		rc = permsunix(dbc->ddic);
	else
		rc = permstexis(dbc->ddic, (char *)uid, (char *)auth);
	dbc->ddic->dbc = dbc;
	if(rc == -1)
		return SQL_ERROR;
	return SQL_SUCCESS;
}

/************************************************************************/

/*	This function as its "normal" behavior is supposed to bring up a
 *	dialog box if it isn't given enough information via "szConnStrIn".  If
 *	it is given enough information, it's supposed to use "szConnStrIn" to
 *	establish a database connection.  In either case, it returns a
 *	string to the user that is the string that was eventually used to
 *	establish the connection.
 */

#ifdef LINT_ARGS
RETCODE	SQL_API SQLDriverConnect(
HDBC	hdbc,
HWND	hwnd,
UCHAR FAR *szConnStrIn,
SWORD	cbConnStrIn,
UCHAR FAR *szConnStrOut,
SWORD	cbConnStrOutMax,
SWORD FAR *pcbConnStrOutMax,
UWORD fDriverCompletion)
#else
RETCODE	SQL_API SQLDriverConnect(hdbc, hwnd, szConnStrIn, cbConnStrIn, szConnStrOut, cbConnStrOutMax, pcbConnStrOutMax, fDriverCompletion)
HDBC	hdbc;
HWND	hwnd;
UCHAR FAR *szConnStrIn;
SWORD	cbConnStrIn;
UCHAR FAR *szConnStrOut;
SWORD	cbConnStrOutMax;
SWORD FAR *pcbConnStrOutMax;
UWORD fDriverCompletion;
#endif
{
	short	iRet;
	BOOL	fPrompt = FALSE;

	if ((szConnStrIn == NULL) || (!cbConnStrIn) ||
		((cbConnStrIn == SQL_NTS) && (!szConnStrIn[0])))
		fPrompt = TRUE;
	else {
        /*  Check connection string for completeness */
        if (fDriverCompletion == SQL_DRIVER_COMPLETE ||
            fDriverCompletion == SQL_DRIVER_PROMPT)
		    fPrompt = TRUE;
	}
	if (fPrompt) {
		/*
		 *	It is not necessary to call "MakeProcInstance" if you
		 *	generate a dialog box from a DLL.
		 */
		iRet = 0;
		if ((!iRet) || (iRet == -1))
			return SQL_NO_DATA_FOUND;
	}
    return SQL_SUCCESS;
}

/************************************************************************/

#ifdef LINT_ARGS
RETCODE	SQL_API SQLBrowseConnect(
HDBC	hdbc,
UCHAR FAR *szConnStrIn,
SWORD	cbConnStrIn,
UCHAR FAR *szConnStrOut,
SWORD	cbConnStrOutMax,
SWORD FAR *pcbConnStrOut)
#else
RETCODE	SQL_API SQLBrowseConnect(hdbc, szConnStrIn, cbConnStrIn, szConnStrOut, cbConnStrOutMax, pcbConnStrOut)
HDBC	hdbc;
UCHAR FAR *szConnStrIn;
SWORD	cbConnStrIn;
UCHAR FAR *szConnStrOut;
SWORD	cbConnStrOutMax;
SWORD FAR *pcbConnStrOut;
#endif
{
    return SQL_SUCCESS;
}

/************************************************************************/

RETCODE SQL_API SQLDisconnect(hdbc)
HDBC	hdbc;
{
	LPDBC dbc = (LPDBC)hdbc;

	if (dbc->ddic != (DDIC *)NULL)
		dbc->ddic = ddclose(dbc->ddic);
	if (dbc->datasource != (char *)NULL)
		free(dbc->datasource);
	dbc->datasource = NULL;
	return SQL_SUCCESS;
}

/************************************************************************/

RETCODE	SQL_API SQLFreeConnect(hdbc)
HDBC	hdbc;
{
	LPDBC	dbc = hdbc;
	if (dbc->datasource != (char *)NULL)
		free(dbc->datasource);
	dbc->datasource = NULL;
	free(hdbc);
	return SQL_SUCCESS;
}

/************************************************************************/

RETCODE SQL_API SQLFreeEnv(henv)
HENV	henv;
{
	free (henv);
	closesqlparse();
	return SQL_SUCCESS;
}
