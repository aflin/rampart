#ifndef _TEXSQL_H
#define _TEXSQL_H

#include "sizes.h"
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
#if defined(EPI_INT64_SQL) || defined(EPI_INT64_MAX)
/* Use SQL_BIGINT/SQL_C_SBIGINT for int64
 * WTF no standard SQL_UBIGINT seems to exists;
 * use SQL_UINT64/SQL_C_UBIGINT for uint64
 */
#  define SQL_UINT64	(SQL_TYPE_DRIVER_START - 5)	/* KNG */
#else
	error We should be 64-bit
#endif /* EPI_INT64_SQL */

/************************************************************************/
#endif /* _TEXSQL_H */
