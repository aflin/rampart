#ifndef _TEXSQL_H
#define _TEXSQL_H

#include "txsql.h"
/************************************************************************/

#if !defined(_WIN32) && !defined(_Windows)
#ifndef FAR
#define FAR
#endif
#ifndef NEAR
#define NEAR
#endif
#ifndef PASCAL
#define PASCAL
#endif
#define HWND short
#define WORD short
#define LONG long
#define HANDLE void *
#define BOOL char
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif                                                   /* !_WIN32 */

/************************************************************************/

/*  Environment information.  This is allocated by "SQLAllocEnv".       */

typedef	struct	tagENV {
#ifdef NEVER
	void *ta;
#endif
	long	errcode;
}	ENV,
	FAR * LPENV;

/************************************************************************/

/*  Database connection information.  This is allocated by "SQLAllocConnect". */

typedef struct	tagDBC {
	LPENV	env;
	DDIC	*ddic;
	char	*datasource;
	long	errcode;
	int	refcount;
}	DBC,
	FAR * LPDBC;

/************************************************************************/

#define SRESULT struct tagSRESULT
SRESULT {
	int	fieldno;
	int	type;
	int	datasz;
	void	*data;
	long	errcode;
};

typedef struct SBOUNDCOL
{
	int	colno;
	int	type;
	void	*data;
	int	datasz;
	SDWORD	*adatasz;
	long	errcode;
} SBOUNDCOL;

/*  Statment information.  This is allocated by "SQLAllocStmt".         */

typedef struct	tagSTMT {
	int	nrows;		/* Number of rows affected */
	long	errcode;
	LPDBC	dbc;
	DBTBL	*outtbl;
	QNODE	*query;
	SRESULT	*result;
	FLDOP	*fo;
	int	nparams;
	PARAM	*curp;		/* Current parameter for PutData */
	int	lastcol;
	long	datalen;
	long	dataoff;
	char	*data;
	int	noresults;	/* Does not produce results */
	int	msgq;		/* Putmsg with the query */
	void *ta;
	SBOUNDCOL	*boundcols;	/* Bound Columns */
	size_t	allocedparam;
	PARAM	*param;		/* Parameters set in statement */
	char	*sqlstr;	/* The SQL String that was prep'd */
}	STMT,
	FAR * LPSTMT;

/************************************************************************/

/* #include "txsql.h" Should be included by txsqlext.h */
#include "txsqlext.h"

#define SQL_COUNTER	SQL_TYPE_DRIVER_START
#define SQL_C_COUNTER	SQL_COUNTER
#define SQL_WORD	(SQL_SMALLINT+SQL_UNSIGNED_OFFSET)
#define SQL_DWORD	(SQL_INTEGER+SQL_UNSIGNED_OFFSET)
#define SQL_STRLST	(SQL_TYPE_DRIVER_START-1)
#define SQL_C_STRLST	SQL_STRLST
#define SQL_CDATE	(SQL_TYPE_DRIVER_START-2)
#define SQL_C_CDATE	SQL_CDATE
#define SQL_C_INTEGER	(SQL_TYPE_DRIVER_START-3)
#define SQL_INTERNAL	(SQL_TYPE_DRIVER_START-4)/* KNG for FTN_INTERNAL */
#define SQL_C_INTERNAL	SQL_INTERNAL
#ifdef EPI_INT64_SQL
/* Use SQL_BIGINT/SQL_C_SBIGINT for int64
 * WTF no standard SQL_UBIGINT seems to exists;
 * use SQL_UINT64/SQL_C_UBIGINT for uint64
 */
#  define SQL_UINT64	(SQL_TYPE_DRIVER_START - 5)	/* KNG */
#else
	error We should be 64-bit
#endif /* EPI_INT64_SQL */

#define FLDLSTMAX 1000
#define FLDLSTPN (FLDLST *)NULL
/** Container for results from texis_ calls
 *
 * Contains the results from texis_fetch etc.
 *
 * ndata contains the actual size of the data returned.
 * ondata contains the declared size of the field in the table, which may
 * be different than ndata if it is a var field.
 */
typedef struct FLDLST
{
   int      n;                 /**< number of fields in the lstst */
   int      type [FLDLSTMAX];  /**< Data types - of FTN_ type */
   void    *data [FLDLSTMAX];  /**< pointer to data */
   int      ndata[FLDLSTMAX];  /**< number of items in data, e.g. char length */
   char    *name [FLDLSTMAX];  /**< name of field */
   int      ondata[FLDLSTMAX]; /**< number of items in schema */
} FLDLST;

/************************************************************************/
                             /* MAW 06-27-94 - TEXIS was HSTMT before */
#define TEXIS struct texis_struct
#define TEXISPN (TEXIS *)NULL
TEXIS {
   HENV     henv;
   HDBC     hdbc;
   HSTMT    hstmt;
   int      donullcb;
   FLDOP   *fo;/* MAW 10-05-94 - for doing conversions on result rows */
   FLD     *fld[DDFIELDS];          /* MAW 10-06-94 - for conversions */
	 FLDLST   fl;
   int	   nfld;   /* JMT 1999-03-04 - count fld, make free efficient */
   RETCODE      lastRetcode;    /* last SQLPrepare() etc. RETCODE */
};
/************************************************************************/
TEXIS *texis_open(char *database, char *user, char *password);
TEXIS *texis_open_options(void *, void *, void *, char *database, char *user, char *group, char *password);
TEXIS *texis_dup(TEXIS *);
int texis_prepare(TEXIS *, char *sqlQuery);
int texis_execute(TEXIS *);
int texis_param(TEXIS *, int paramNum, void *data, long *dataLen, int cType, int sqlType);
FLDLST *texis_fetch(TEXIS *, int stringsFrom);
int texis_flush(TEXIS *tx);
int texis_flush_scroll(TEXIS *tx, int nrows);
char **texis_getresultnames(TEXIS *tx);
int *texis_getresultsizes(TEXIS *tx);
int texis_getrowcount(TEXIS *tx);
TEXIS *texis_close(TEXIS *);

/************************************************************************/
#endif /* _TEXSQL_H */
