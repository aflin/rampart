#include "txcoreconfig.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef EPI_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef MSDOS
#include <process.h>
#endif
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "strbuf.h"
#include "texint.h"


#define FIX_SQL_CDATE	/* KNG 980505 Alpha bugfix? */

#ifdef USE_STRBUF
static STRBUF *cmd;
#else
static char cmd[MAXINSZ];
#endif

const char	TXfailIfIncompatibleMsg[] = "Will not execute SQL: set compatibilityversion failed and failifincompatible is enabled";

/************************************************************************/

void
TXfreesqlbuf()
{
#ifdef USE_STRBUF
	cmd = closestrbuf(cmd);
#endif /* USE_STRBUF */
}

/*  Allocate a SQL statement */

RETCODE SQL_API
SQLAllocStmt(hdbc, phstmt)
HDBC hdbc;
HSTMT FAR *phstmt;
{
    LPSTMT  hstmt;

    if (hdbc == NULL)
    {
        *phstmt = SQL_NULL_HSTMT;
        return SQL_ERROR;
    }
    hstmt = (LPSTMT) calloc (1, sizeof (STMT));
    if (hstmt == NULL)
    {
        *phstmt = SQL_NULL_HSTMT;
        return SQL_ERROR;
    }
    hstmt->dbc = (LPDBC)hdbc;
    hstmt->dbc->refcount += 1;
    hstmt->fo = dbgetfo();
    *phstmt = hstmt;
    return SQL_SUCCESS;
}

/************************************************************************/

#ifdef LINT_ARGS
RETCODE SQL_API SQLFreeStmt(HSTMT hstmt, UWORD fOption)
#else
RETCODE SQL_API SQLFreeStmt(hstmt, fOption)
HSTMT	hstmt;
UWORD   fOption;
#endif
{
	LPSTMT lpstmt = (LPSTMT) hstmt;
	FLD	*f;

	switch (fOption)
	{
	    case SQL_DROP:
	    case SQL_CLOSE:
		if (lpstmt->outtbl != (DBTBL *)NULL)
			lpstmt->outtbl = closedbtbl(lpstmt->outtbl);
		if (lpstmt->query != (QNODE *)NULL)
			lpstmt->query = closeqnode(lpstmt->query);
		if (lpstmt->result != (SRESULT *)NULL)
		{
			free(lpstmt->result);
			lpstmt->result = (SRESULT *)NULL;
		}
		if (lpstmt->boundcols != (SBOUNDCOL *)NULL)
		{
			free(lpstmt->boundcols);
			lpstmt->boundcols = (SBOUNDCOL *)NULL;
		}
		if (lpstmt->param)
		{
			size_t i;

			for (i = 0; i < lpstmt->allocedparam; i++)
			{
				/* KNG 20060403 see SQLSetParam() comment */
				if ((f = lpstmt->param[i].fld) != FLDPN)
					closefld(f);
			}
			lpstmt->param = TXfree(lpstmt->param);
			lpstmt->allocedparam = 0;
			lpstmt->nparams = 0;
		}
		lpstmt->sqlstr = TXfree(lpstmt->sqlstr);
		if(fOption == SQL_DROP)
		{
			if (lpstmt->fo != (FLDOP *)NULL)
				lpstmt->fo = foclose(lpstmt->fo);
			lpstmt->dbc->refcount -= 1;
			lpstmt = TXfree(lpstmt);
		}
		break;
	    case SQL_RESET_PARAMS:
	    	if(!lpstmt->query)
			return SQL_ERROR;
		TXresetparams(lpstmt);
		break;
	}
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Perform a Prepare on the SQL statement */

RETCODE SQL_API
SQLPrepare(hstmt, sqlstr, cbSqlStr)
HSTMT	hstmt;
UCHAR FAR *sqlstr;
SDWORD	cbSqlStr;
{
/* This should have the processed query tree ready to run */
	static CONST char	fn[] = "SQLPrepare";
	QNODE *q;
	int rc, succ;
	size_t	nparams;
	LPSTMT lpstmt = (LPSTMT) hstmt;
#ifdef NEVER
	char *t;
	int foundword = 0;
#endif
#ifdef OBJECT_READTOKEN
	TX_READ_TOKEN *toke = NULL;
#endif
#ifdef NEVER
	time_t	t;

	t = time(NULL);
	putmsg(999, NULL, "Preparing <%s> at %s", sqlstr, ctime(&t));
#endif
	if(lpstmt->msgq)
		putmsg(MQUERY, "SQLPrepare", "<%s> (%d)", sqlstr, getpid());
	if (lpstmt->outtbl != (DBTBL *)NULL)
		lpstmt->outtbl = closedbtbl(lpstmt->outtbl);
	if(lpstmt->query == (QNODE *)0xfefefefe)
	{
		putmsg(MERR, fn, "LPSTMT.query is corrupt at line %d", __LINE__);
		return(SQL_ERROR);
	}
	if (lpstmt->query != (QNODE *)NULL)
		lpstmt->query = closeqnode(lpstmt->query);
	if(lpstmt->query == (QNODE *)0xfefefefe)
	{
		putmsg(MERR, fn, "LPSTMT.query is corrupt at line %d", __LINE__);
		return(SQL_ERROR);
	}
	if (lpstmt->result != (SRESULT *)NULL)
	{
		free(lpstmt->result);
		lpstmt->result = (SRESULT *)NULL;
	}
	if (lpstmt->boundcols != (SBOUNDCOL *)NULL)
	{
		free(lpstmt->boundcols);
		lpstmt->boundcols = (SBOUNDCOL *)NULL;
	}
	if(lpstmt->query == (QNODE *)0xfefefefe)
	{
		putmsg(MERR, fn, "LPSTMT.query is corrupt at line %d", __LINE__);
		return(SQL_ERROR);
	}
	lpstmt->sqlstr = TXfree(lpstmt->sqlstr);

	if (TXApp->failIfIncompatible &&
	    TXApp->setCompatibilityVersionFailed)
	{
		putmsg(MERR, fn, TXfailIfIncompatibleMsg);
		return(SQL_ERROR);
	}

#ifdef USE_STRBUF
	if(!cmd)
		cmd = openstrbuf();
	else
		resetstrbuf(cmd);
#endif
#ifdef NEVER
	/*
		Lower case first word to make Lex happy
	*/
        for(t = sqlstr; *t && foundword < 2; t++)
        {
                if(!isspace(*t))
                {
                        foundword = 1;
                        if(isalpha(*t))
                                *t = tolower(*t);
                }

                if(isspace(*t) && foundword)
                        foundword++;

        }
#endif
	lpstmt->sqlstr = strdup((char *)sqlstr);
	rc = sqlconvert((char *)sqlstr, lpstmt->dbc->datasource, lpstmt->dbc->ddic, cmd, MAXINSZ);
	if (rc)
		return SQL_ERROR;
	if(lpstmt->dbc->ddic->messages[MESSAGES_SQL_PREPARE_CONVERT] && (lpstmt->dbc->ddic->ihstmt != hstmt)) {
		putmsg(MINFO, fn, "sqlconvert(%s) ==> %s", sqlstr, cmd->data);
	}
	lpstmt->noresults = 0;
#ifdef USE_STRBUF
	if(lenstrbuf(cmd) == 0)
#else
        if(*cmd=='\0')         /* JMT 06-15-94 - already finished */
#endif
	{
		lpstmt->noresults = 1;
		return SQL_SUCCESS;
	}
#ifdef USE_STRBUF
#  ifdef OBJECT_READTOKEN
	toke = setparsestring(toke, cmd->data);
#ifdef NEVER
	printf("%s\n", cmd->data);
#endif
#  else
	setparsestring(cmd->data);
#ifdef NEVER
	printf("%s\n", cmd->data);
#endif
#  endif
#else
	setparsestring(cmd);
#ifdef NEVER
	printf("%s\n", cmd);
#endif
#endif
#ifdef OBJECT_READTOKEN
	q = readnode(lpstmt->dbc->ddic, lpstmt->fo, toke, 0);
	toke = TXfree(toke);
#else
	q = readnode(lpstmt->dbc->ddic, lpstmt->fo, 0);
#endif
	q = TXreorgqnode(q);
	if(!q)
		return SQL_ERROR;
	rc = TXenumparams(lpstmt, q, 0, &nparams);
	if (rc == -1)
		return SQL_ERROR;
	q = TXbuffer_node_init(q);
	if(!q)
		return SQL_ERROR;
	lpstmt->nparams = nparams;
	lpstmt->query = q;
	if (!nparams)
	{
		lpstmt->outtbl = TXpreparetree(lpstmt->dbc->ddic, q, lpstmt->fo, &succ, NULL);
		if (succ)
		{
			lpstmt->noresults = 1;
			return SQL_SUCCESS;
		}
		if (lpstmt->outtbl == (DBTBL *)NULL)
			return SQL_ERROR;
#if defined(ANALYZE)
		analyzetree(lpstmt->dbc->ddic, q, NULL, lpstmt->fo, 0);
#endif
		if (lpstmt->outtbl->tbl->n)
		  {
		    lpstmt->result = (SRESULT *)
		      calloc(lpstmt->outtbl->tbl->n, sizeof(SRESULT));
		    lpstmt->boundcols = (SBOUNDCOL *)
			calloc(lpstmt->outtbl->tbl->n, sizeof(SBOUNDCOL));
		  }
		else            /* don't calloc(0) for SYSDUMMY KNG 000524 */
		  {
		    lpstmt->result = (SRESULT *)NULL;
		    lpstmt->boundcols = (SBOUNDCOL *)NULL;
		  }
	}
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Set parameters on a statement handle */

#ifdef LINT_ARGS
RETCODE SQL_API SQLSetParam(
HSTMT	hstmt,
UWORD	ipar,
SWORD	fCType,
SWORD	fSqlType,
UDWORD	cbColDef,
SWORD	ibScale,
PTR	rgbValue,
SDWORD FAR *pcbValue)
#else
RETCODE SQL_API SQLSetParam(hstmt, ipar, fCType, fSqlType, cbColDef, ibScale, rgbValue, pcbValue)
HSTMT	hstmt;          /* (in) HSTMT handle */
UWORD	ipar;           /* (in) SQL parameter # (counting from 1) */
SWORD	fCType;         /* (in) SQL_C_... type */
SWORD	fSqlType;       /* (in) SQL_... type */
UDWORD	cbColDef;       /* (in) precision?  output/converted-field byte len */
SWORD	ibScale;        /* (in) scale?  ignored */
PTR	rgbValue;       /* (in) parameter data (will be copied) */
SDWORD FAR *pcbValue;   /* (in) >=0: `rgbValue' byte len  <0: SQL_... val */
#endif
{
/*	Convert the value to a field, and then attach it. */
        static CONST char Fn[]="SQLSetParam";
	FLD	*f = FLDPN;	/* The field we are creating */
	PARAM	*p;
	LPSTMT	lpstmt = hstmt;
	int	tocopy, ret, isAtExec = 0;
	void	*data = NULL;
	size_t	size;

	p = getparam(lpstmt, lpstmt->query, ipar);
	if (!p) goto err;

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 * KNG 20110224 Bug 1681: convert the data *before* making the FLD,
	 * so that we have the correct size:
	 */
	if ((*pcbValue == SQL_DATA_AT_EXEC) ||
            (*pcbValue <= SQL_LEN_DATA_AT_EXEC_OFFSET))
	{
		isAtExec = 1;
		tocopy = 0;
	}
	else if (*pcbValue == SQL_NULL_DATA)
		tocopy = 0;
	else
		tocopy = (int)*pcbValue;
#ifdef FIX_SQL_CDATE
	/* on an Alpha we seem to be getting fSqlType = SQL_CDATE but
	 * with a length = sizeof(ft_long):            -KNG 980505
	 *
	 * was due to dotexisparam() passing time_t as SQL_C_LONG;
	 * now uses int64 (SQL_C_SBIGINT) if needed (e.g. win64 where
	 * sizeof(time_t) > sizeof(long); Alpha was long > time_t);
	 * also (Bug 3895) avoid divide-by-zero here e.g. win64:
	 */
	if (fSqlType == SQL_CDATE && fCType == SQL_C_LONG)
		size = (sizeof(time_t) > sizeof(long) ?
			((size_t)cbColDef)*(sizeof(time_t)/sizeof(long)) :
			((size_t)cbColDef)/(sizeof(long)/sizeof(time_t)));
	else if (fSqlType == SQL_CDATE && fCType == SQL_C_SBIGINT)
		size = (sizeof(time_t) > sizeof(ft_int64) ?
			((size_t)cbColDef)*(sizeof(time_t)/sizeof(ft_int64)) :
			((size_t)cbColDef)/(sizeof(ft_int64)/sizeof(time_t)));
	else
#endif /* FIX_SQL_CDATE */
	size = (size_t)cbColDef;
	if (!isAtExec &&
	    convtosql((rgbValue == NULL && tocopy == 0 ? "" : rgbValue),
		     tocopy, fCType, fSqlType, &data, &size, lpstmt->fo) != 0)
		goto err;

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 * Create the FLD, now that we have the converted size;
	 * Bug 1681 use it for varint/int(n>1) params.  WTF we still
	 * have to guess at size if `isAtExec':
	 */
#ifndef NO_TRY_FAST_CONV
	if(p->fld != NULL)
	{
		f = p->fld;
		/* Free alloc'd data: initfld() would orphan it: */
		freeflddata(f);
		TXfreefldshadownotblob(f);
		switch(fSqlType)
		{
			case SQL_CHAR :
			case SQL_VARCHAR :
			case SQL_LONGVARCHAR :
				initfld(f, FTN_CHAR|DDVARBIT, 1);
				break;
			case SQL_INTEGER:
				initfld(f, FTN_LONG,
				    (isAtExec ? 1 : size/sizeof(ft_long)));
				break;
			case SQL_SMALLINT:
				initfld(f, FTN_SHORT,
				    (isAtExec ? 1 : size/sizeof(ft_short)));
				break;
#  if EPI_OS_USHORT_BITS == 16
			case SQL_WORD:
				initfld(f, FTN_WORD,
				    (isAtExec ? 1 : size/sizeof(ft_word)));
				break;
#  endif /* EPI_OS_USHORT_BITS == 16 */
			case SQL_DWORD:
				initfld(f, FTN_DWORD,
				    (isAtExec ? 1 : size/sizeof(ft_dword)));
				break;
#  ifdef EPI_INT64_SQL
			case SQL_BIGINT:
				initfld(f, FTN_INT64,
				    (isAtExec ? 1 : size/sizeof(ft_int64)));
				break;
			case SQL_UINT64:
				initfld(f, FTN_UINT64,
				    (isAtExec ? 1 : size/sizeof(ft_uint64)));
				break;
#  endif /* EPI_INT64_SQL */
			case SQL_BINARY :
			case SQL_VARBINARY :
			case SQL_LONGVARBINARY :
				initfld(f, FTN_BYTE|DDVARBIT, 1);
				break;
			case SQL_REAL:
				initfld(f, FTN_FLOAT,
				    (isAtExec ? 1 : size/sizeof(ft_float)));
				break;
			case SQL_DOUBLE:
			case SQL_FLOAT:
				initfld(f, FTN_DOUBLE,
				    (isAtExec ? 1 : size/sizeof(ft_double)));
				break;
			case SQL_COUNTER:
				initfld(f,FTN_COUNTER,
				    (isAtExec ? 1 : size/sizeof(ft_counter)));
				break;
			case SQL_STRLST:
				initfld(f, FTN_STRLST|DDVARBIT, 1);
				break;
			case SQL_DATE:
			case SQL_CDATE:
				initfld(f, FTN_DATE,
				    (isAtExec ? 1 : size/sizeof(ft_date)));
				break;
			case SQL_INTERNAL:
				/* WTF guess at # items; probably ok: */
				initfld(f, FTN_INTERNAL,
				  (isAtExec ? 1 : size/FT_INTERNAL_ALLOC_SZ));
				break;
			default:
				goto err;
		}
	}
	else
#else /* NO_TRY_FAST_CONV */
#endif /* NO_TRY_FAST_CONV */
	switch(fSqlType)
	{
		case SQL_CHAR :
		case SQL_VARCHAR :
		case SQL_LONGVARCHAR :
			f = createfld("varchar", 1, 1);
			break;
		case SQL_INTEGER:
			f = createfld("long",
				(isAtExec ? 1 : size/sizeof(ft_long)), 1);
			break;
#  ifdef EPI_INT64_SQL
		case SQL_BIGINT:
			f = createfld("int64",
				(isAtExec ? 1 : size/sizeof(ft_int64)), 1);
			break;
		case SQL_UINT64:
			f = createfld("uint64",
				(isAtExec ? 1 : size/sizeof(ft_uint64)), 1);
			break;
#  endif /* EPI_INT64_SQL */
		case SQL_SMALLINT:
			f = createfld("short",
				(isAtExec ? 1 : size/sizeof(ft_short)), 1);
			break;
#if EPI_OS_USHORT_BITS == 16
		case SQL_WORD:
			f = createfld("word",
				(isAtExec ? 1 : size/sizeof(ft_word)), 1);
			break;
#endif /* EPI_OS_USHORT_BITS == 16 */
		case SQL_DWORD:
			f = createfld("dword",
				(isAtExec ? 1 : size/sizeof(ft_dword)), 1);
			break;
		case SQL_BINARY :
		case SQL_VARBINARY :
		case SQL_LONGVARBINARY :
			f = createfld("varbyte", 1, 1);
			break;
		case SQL_REAL:
			f = createfld("float",
				(isAtExec ? 1 : size/sizeof(ft_float)), 1);
			break;
		case SQL_DOUBLE:
		case SQL_FLOAT:
			f = createfld("double",
				(isAtExec ? 1 : size/sizeof(ft_double)), 1);
			break;
		case SQL_COUNTER:
			f = createfld("counter",
				(isAtExec ? 1 : size/sizeof(ft_counter)), 1);
			break;
		case SQL_STRLST:
			f = createfld("varstrlst", 1, 0);
			break;
		case SQL_DATE:
		case SQL_CDATE:
			f = createfld("date",
				(isAtExec ? 1 : size/sizeof(ft_date)), 1);
			break;
		case SQL_INTERNAL:
			/* WTF guess at # items; probably ok: */
			f = createfld("internal",
				(isAtExec ? 1 :size/FT_INTERNAL_ALLOC_SZ), 1);
			break;
		default:
			goto err;
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 * Finish up SQL_DATA_AT_EXEC etc. now that we have FLD:
	 */
	if (isAtExec)
	{
		p->needdata = 1;
		if (p->fld)
		{
			freeflddata(p->fld);
			/* KNG WTF what if f == p->fld? clear it to be safe:*/
			if (f == p->fld) f = FLDPN;
			p->fld = closefld(p->fld);
		}
		p->fld = f;
		memcpy(&p->usr, &rgbValue, sizeof(p->usr));
		goto ok;
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	 * Set converted `data' into the field:
	 */
	switch(fSqlType)
	{
		case SQL_CHAR :
		case SQL_VARCHAR :
		case SQL_LONGVARCHAR :
		case SQL_BINARY :
		case SQL_VARBINARY :
		case SQL_LONGVARBINARY :
		case SQL_STRLST :
			/* KNG 20060403 use setfld() so FLD owns the data;
			 * avoids TXsetshadowalloc() drill at SQLFreeStmt():
			 */
			/* KNG 20110223 bug: `size' is already byte length,
			 * do not multiply by `f->elsz'.  Was no consequence
			 * only because `f->elsz' is always 1 here:
			 */
			setfldandsize(f, data, size + 1, FLD_FORCE_NORMAL);
			data = NULL;		/* owned by `f' now */
			break;
		case SQL_INTEGER :
#ifdef EPI_INT64_SQL
		case SQL_BIGINT:
		case SQL_UINT64:
#endif /* EPI_INT64_SQL */
		case SQL_SMALLINT :
		case SQL_WORD:
		case SQL_DWORD:
		case SQL_FLOAT:
		case SQL_REAL:
		case SQL_DOUBLE:
		case SQL_COUNTER:
		case SQL_CDATE:
		case SQL_DATE:
		case SQL_INTERNAL:
			/* KNG 20060403 See setfld() comment above. */
			/* KNG 20110224 Use `size' not `f->elsz';
			 * former corresponds to `data':
			 */
			setfldandsize(f, data, size + 1, FLD_FORCE_NORMAL);
			data = NULL;		/* owned by `f' now */
			break;
		default :
			putmsg(MERR+UGE,Fn,
			    "Unknown type: ipar = %d byte size = %d Type = %d",
				(int)ipar, (int)*pcbValue, (int)fSqlType);
			goto err;
	}
#ifndef NO_TRY_FAST_CONV
	if (!p->fld)
		p->fld = f;
#else /* NO_TRY_FAST_CONV */
	if (p->fld)
	{
		freeflddata(p->fld);
		closefld(p->fld);
	}
	p->fld = f;
#endif /* NO_TRY_FAST_CONV */
	p->needdata = 0;
ok:
	ret = SQL_SUCCESS;
	goto done;

err:
	ret = SQL_ERROR;
done:
	if (data)				/* orphaned `data' */
	{
		if (fSqlType == SQL_INTERNAL)
			tx_fti_close((ft_internal *)data,
				/* WTF WAG: */ size/FT_INTERNAL_ALLOC_SZ);
		else
			TXfree(data);
		data = NULL;
	}
	return(ret);
}

/************************************************************************/

/*  Returns the description of a parameter marker. */

#ifdef LINT_ARGS
RETCODE SQL_API SQLDescribeParam(
HSTMT	hstmt,
UWORD	ipar,
SWORD FAR *pfSqlType,
UDWORD FAR *pcbColDef,
SWORD FAR *pibScale,
SWORD FAR *pfNullable)
#else
RETCODE SQL_API SQLDescribeParam(hstmt, ipar, pfSqlType, pcbColDef, pibScale, pfNullable)
HSTMT	hstmt;
UWORD	ipar;
SWORD FAR *pfSqlType;
UDWORD FAR *pcbColDef;
SWORD FAR *pibScale;
SWORD FAR *pfNullable;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Sets multiple values (arrays) for the set of parameter markers. */

RETCODE SQL_API SQLParamOptions(lpstmt, crow, pirow)
HSTMT	lpstmt;
UDWORD  crow;
UDWORD FAR *pirow;
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Returns the number of parameter markers. */

RETCODE SQL_API SQLNumParams(lpstmt, pcpar)
HSTMT	lpstmt;
SWORD FAR *pcpar;
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Sets options that control the behavior of cursors. */

#ifdef LINT_ARGS
RETCODE SQL_API SQLSetScrollOptions(
HSTMT	lpstmt,
UWORD   fConcurrency,
SDWORD  crowKeyset,
UWORD   crowRowset)
#else
RETCODE SQL_API SQLSetScrollOptions(lpstmt, fConcurrency, crowKeyset, crowRowset)
HSTMT	lpstmt;
UWORD   fConcurrency;
SDWORD  crowKeyset;
UWORD   crowRowset;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Set the cursor name on a statement handle */

#ifdef LINT_ARGS
RETCODE SQL_API SQLSetCursorName(HSTMT lpstmt, UCHAR FAR *szCursor,
SWORD cbCursor)
#else
RETCODE SQL_API SQLSetCursorName(lpstmt, szCursor, cbCursor)
HSTMT	lpstmt;
UCHAR FAR *szCursor;
SWORD	cbCursor;
#endif
{
	return SQL_SUCCESS;
}

/************************************************************************/

/*  Return the cursor name for a statement handle */

#ifdef LINT_ARGS
RETCODE SQL_API SQLGetCursorName(
HSTMT	lpstmt,
UCHAR FAR *szCursor,
SWORD	cbCursorMax,
SWORD FAR *pcbCursor)
#else
RETCODE SQL_API SQLGetCursorName(lpstmt, szCursor, cbCursorMax, pcbCursor)
HSTMT	lpstmt;
UCHAR FAR *szCursor;
SWORD	cbCursorMax;
SWORD FAR *pcbCursor;
#endif
{
	return SQL_SUCCESS;
}
