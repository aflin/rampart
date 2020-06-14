#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "dbquery.h"
#include "sizes.h"
#include "texint.h"


static CONST char	CannotAlloc[] ="Cannot alloc %lu bytes of memory: %s";

/* Note ambiguous types like SQL_C_DEFAULT are last, in case overlap: */
#define TX_SQL_C_TYPE_SYMBOLS	\
I(SQL_C_BINARY)			\
I(SQL_C_BIT)			\
I(SQL_C_CDATE)			\
I(SQL_C_CHAR)			\
I(SQL_C_COUNTER)		\
I(SQL_C_DATE)			\
I(SQL_C_DOUBLE)			\
I(SQL_C_FLOAT)			\
I(SQL_C_INTEGER)		\
I(SQL_C_INTERNAL)		\
I(SQL_C_LONG)			\
I(SQL_C_SBIGINT)		\
I(SQL_C_SHORT)			\
I(SQL_C_SLONG)			\
I(SQL_C_SSHORT)			\
I(SQL_C_STINYINT)		\
I(SQL_C_STRLST)			\
I(SQL_C_TIME)			\
I(SQL_C_TIMESTAMP)		\
I(SQL_C_TINYINT)		\
I(SQL_C_TYPE_DATE)		\
I(SQL_C_TYPE_TIME)		\
I(SQL_C_TYPE_TIMESTAMP)		\
I(SQL_C_UBIGINT)		\
I(SQL_C_ULONG)			\
I(SQL_C_USHORT)			\
I(SQL_C_UTINYINT)		\
I(SQL_C_DEFAULT)

const TXCODEDESC	TXsqlCTypeNames[] =
{
#undef I
	/* Do not use TXCODEDESC_ITEM(): will macro-expand `tok': */
#define I(tok)	{ tok, #tok },
	TX_SQL_C_TYPE_SYMBOLS
	{ 0, NULL }
#undef I
};

#define TX_SQL_TYPE_SYMBOLS	\
I(SQL_BIGINT)			\
I(SQL_BINARY)			\
I(SQL_BIT)			\
I(SQL_CDATE)			\
I(SQL_CHAR)			\
I(SQL_COUNTER)			\
I(SQL_DATE)			\
I(SQL_DECIMAL)			\
I(SQL_DOUBLE)			\
I(SQL_DWORD)			\
I(SQL_FLOAT)			\
I(SQL_INTEGER)			\
I(SQL_INTERNAL)			\
I(SQL_LONGVARBINARY)		\
I(SQL_LONGVARCHAR)		\
I(SQL_NUMERIC)			\
I(SQL_REAL)			\
I(SQL_SMALLINT)			\
I(SQL_STRLST)			\
I(SQL_TIME)			\
I(SQL_TIMESTAMP)		\
I(SQL_TINYINT)			\
I(SQL_UINT64)			\
I(SQL_VARBINARY)		\
I(SQL_VARCHAR)			\
I(SQL_WORD)

const TXCODEDESC	TXsqlTypeNames[] =
{
#undef I
	/* Do not use TXCODEDESC_ITEM(): will macro-expand `tok': */
#define I(tok)	{ tok, #tok },
	TX_SQL_TYPE_SYMBOLS
	{ 0, NULL }
#undef I
};

/*
	defctype - return the default c type for a given sql type
*/

static int defctype ARGS((int));

static int
defctype(sqltype)
int sqltype;
{
	switch(sqltype)
	{
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
		case SQL_DECIMAL:
		case SQL_NUMERIC:
		case SQL_BIGINT:
			return SQL_C_CHAR;
		case SQL_BIT:
			return SQL_C_BIT;
		case SQL_TINYINT:
			return SQL_C_TINYINT;
		case SQL_SMALLINT:
			return SQL_C_SHORT;
		case SQL_INTEGER:
			return SQL_C_LONG;
		case SQL_REAL:
			return SQL_C_FLOAT;
		case SQL_FLOAT:
		case SQL_DOUBLE:
			return SQL_C_DOUBLE;
		case SQL_BINARY:
		case SQL_VARBINARY:
		case SQL_LONGVARBINARY:
			return SQL_C_BINARY;
		case SQL_DATE:
			return SQL_C_DATE;
		case SQL_TIME:
			return SQL_C_TIME;
		case SQL_TIMESTAMP:
			return SQL_C_TIMESTAMP;
		default:
			return SQL_TYPE_NULL;
	}
}

/******************************************************************/

#if 0
static void swap2 ARGS((void *));

static void
swap2(v)
void *v;
{
#ifndef EPI_LITTLE_ENDIAN
	char t, *d = v;

	t = d[0];
	d[0] = d[1];
	d[1] = t;
#endif
}

/******************************************************************/

static void swap4 ARGS((void *));

static void
swap4(v)
void *v;
{
#ifndef EPI_LITTLE_ENDIAN
	char t, *d = v;
	t = d[0];
	d[0] = d[3];
	d[3] = t;
	t = d[1];
	d[1] = d[2];
	d[2] = t;
#endif
}

/******************************************************************/

static void swapts ARGS((ft_datetime *));

static void
swapts(ts)
ft_datetime	*ts;
{
	swap2(&ts->year);
	swap2(&ts->month);
	swap2(&ts->day);
	swap2(&ts->hour);
	swap2(&ts->minute);
	swap2(&ts->second);
	swap4(&ts->fraction);
}
#endif /* 0 */

/******************************************************************/

static int fastconv ARGS((void *, int, int, int, void **, size_t *, FLDOP *));

static int
fastconv(indata, inlen, intype, outtype, outdata, outlen, fo)                  
void    *indata;        /* (in) C parameter data to convert */
int     inlen;          /* (in) byte length of `indata' */
int     intype;         /* (in) SQL_C_... type of `indata' */
int     outtype;        /* (in) SQL_... type (or SQL_C_... type?) to output */
void    **outdata;      /* (out, alloced) converted parameter data */
size_t	*outlen;        /* (in/out) byte length for output */
FLDOP	*fo;            /* (in/out) FLDOP to handle conversions */
/* Returns -1 on error, 0 on success.
 */
{
	static CONST char	fn[] = "fastconv";

	if(intype != outtype) return(-1);	/* error */
	switch (intype)
	{
	case SQL_INTERNAL:
		*outdata = tx_fti_copy4read((ft_internal *)indata,
					/* wtf WAG as to # elements: */
					inlen/FT_INTERNAL_ALLOC_SZ);
		*outlen = inlen;
		break;
	default:
		*outdata = (char *)malloc(inlen+1);
		if (*outdata == CHARPN)
		{
			TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn,
					inlen + 1, 1);
			return(-1);		/* error */
		}
		memcpy(*outdata, indata, inlen);
		((char *)(*outdata))[inlen]='\0';
		*outlen = inlen;
		break;
	}
	return 0;				/* success */
}

/******************************************************************/

/*
	takes a buffer containing C data, and converts it into the
	desired SQL data
*/

int
convtosql(indata, inlen, intype, outtype, outdata, outlen, fo)
void	*indata;        /* (in) C parameter data to convert */
int	inlen;          /* (in) byte length of `indata' */
int     intype;         /* (in) SQL_C_... type of `indata' */
int     outtype;        /* (in) SQL_... type (or SQL_C_... type?) to output */
void    **outdata;      /* (out, alloced) converted parameter data */
size_t	*outlen;        /* (in/out) byte length for output */
FLDOP	*fo;            /* (in/out) FLDOP to handle conversions */
/* Returns -1 on error, 0 on success.
 */
{
	static CONST char Fn[] = "convtosql";
	FLD	*infld = FLDPN, *outfld = FLDPN;
	char	*p;
	int	datalen, ret, inFtnType = 0, outFtnType = 0;
	int	incount, inNonNull = 0, outNonNull = 0;
	size_t	outcount;
	CONST char	*inFtnStr = CHARPN, *outFtnStr = CHARPN;

	if (intype == SQL_C_DEFAULT)
		intype = defctype(outtype);
	if (intype == SQL_TYPE_NULL)
		goto ok;
	if (outtype == SQL_DATE) outtype = SQL_CDATE;
#ifndef NO_TRY_FAST_CONV
	if(intype == outtype)
	{
		ret = fastconv(indata, inlen, intype,
			outtype, outdata, outlen, fo);
		goto done;
	}
#endif /* !NO_TRY_FAST_CONV */
	switch(intype)
	{
		case SQL_C_CHAR:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
			inFtnStr = "varchar";
			inFtnType = (DDVARBIT | FTN_CHAR);
			incount=inlen;
			break;
		case SQL_C_BIT:
			inFtnStr = "byte";
			inFtnType = FTN_BYTE;
			incount=inlen;
			break;
		case SQL_C_TINYINT:
		case SQL_C_UTINYINT:
			inFtnStr = "byte";
			inFtnType = FTN_BYTE;
			incount=inlen;
			break;
		case SQL_C_SHORT:
		case SQL_C_SSHORT:
			inFtnStr = "short";
			inFtnType = FTN_SHORT;
			incount=inlen/sizeof(ft_short);
			break;
		case SQL_C_USHORT:
			inFtnStr = "word";
			inFtnType = FTN_WORD;
			incount=inlen/sizeof(ft_word);
			break;
		case SQL_C_LONG:
		case SQL_C_SLONG:
			inFtnStr = "long";
			inFtnType = FTN_LONG;
			incount=inlen/sizeof(ft_long);
			break;
		case SQL_C_ULONG:
			inFtnStr = "dword";
			inFtnType = FTN_DWORD;
			incount=inlen/sizeof(ft_dword);
			break;
		case SQL_C_INTEGER:
			/* Bug 1681: SQL_C_INTEGER is int; can map to ft_int
			 * iff same size:
			 */
			if (sizeof(int) != sizeof(ft_int))
				goto unsupportedIntype;
			inFtnStr = "int";
			inFtnType = FTN_INT;
			incount=inlen/sizeof(ft_int);
			break;
		case SQL_C_FLOAT:
			inFtnStr = "float";
			inFtnType = FTN_FLOAT;
			incount=inlen/sizeof(ft_float);
			break;
		case SQL_C_DOUBLE:
		case SQL_FLOAT:
			inFtnStr = "double";
			inFtnType = FTN_DOUBLE;
			incount=inlen/sizeof(ft_double);
			break;
		case SQL_C_BINARY:
		case SQL_VARBINARY:
		case SQL_LONGVARBINARY:
			inFtnStr = "varbyte";
			inFtnType = (DDVARBIT | FTN_BYTE);
			incount=inlen/sizeof(ft_byte);
			break;
		case SQL_C_DATE:
			inFtnStr = "datetime";
			inFtnType = FTN_DATETIME;
			incount=inlen/sizeof(ft_datetime);
			break;
		case SQL_C_TIME:
			inFtnStr = "timestamp";
			inFtnType = FTN_TIMESTAMP;
			incount=inlen/sizeof(ft_timestamp);
			break;
		case SQL_C_TIMESTAMP:
			inFtnStr = "datetime";
			inFtnType = FTN_DATETIME;
			incount=inlen/sizeof(ft_datetime);
			break;
		case SQL_COUNTER:
			inFtnStr = "counter";
			inFtnType = FTN_COUNTER;
			incount=inlen/sizeof(ft_counter);
			break;
		case SQL_STRLST:
			inFtnStr = "varstrlst";
			inFtnType = (DDVARBIT | FTN_STRLST);
			incount=inlen;
			break;
		case SQL_INTERNAL:
			inFtnStr = "internal";
			inFtnType = FTN_INTERNAL;
			incount = 1;
			inNonNull = 1;
			break;
#ifdef EPI_INT64_SQL
		case SQL_C_SBIGINT:
		case SQL_BIGINT:
			inFtnStr = "int64";
			inFtnType = FTN_INT64;
			incount=inlen/sizeof(ft_int64);
			break;
		case SQL_C_UBIGINT:
		case SQL_UINT64:
			inFtnStr = "uint64";
			inFtnType = FTN_UINT64;
			incount=inlen/sizeof(ft_uint64);
			break;
#endif /* EPI_INT64_SQL */
		default:
		unsupportedIntype:
			putmsg(MWARN, Fn, "Unsupported intype %d", intype);
			goto ok;                     /* MAW 06-09-95 */
	}
	/* Delay `infld'/`outfld' creation until we check `outtype' too:
	 * might still be able to use fastconv():
	 */
	switch(outtype)
	{
		case SQL_CHAR:
		case SQL_VARCHAR:
		case SQL_LONGVARCHAR:
			outFtnStr = "varchar";
			outFtnType = (DDVARBIT | FTN_CHAR);
			break;
		case SQL_BIT:
			outFtnStr = "byte";
			outFtnType = FTN_BYTE;
			break;
		case SQL_TINYINT:
			outFtnStr = "byte";
			outFtnType = FTN_BYTE;
			break;
		case SQL_SMALLINT:
			outFtnStr = "short";
			outFtnType = FTN_SHORT;
			break;
		case SQL_INTEGER:
			outFtnStr = "long";
			outFtnType = FTN_LONG;
			break;
		case SQL_DWORD:
			outFtnStr = "dword";
			outFtnType = FTN_DWORD;
			break;
		case SQL_REAL:
			outFtnStr = "float";
			outFtnType = FTN_FLOAT;
			break;
		case SQL_DOUBLE:
		case SQL_FLOAT:
			outFtnStr = "double";
			outFtnType = FTN_DOUBLE;
			break;
		case SQL_BINARY:
		case SQL_VARBINARY:
		case SQL_LONGVARBINARY:
			outFtnStr = "varbyte";
			outFtnType = (DDVARBIT | FTN_BYTE);
			break;
		case SQL_DATE:
			outFtnStr = "datestamp";
			outFtnType = FTN_DATESTAMP;
			break;
		case SQL_TIME:
			outFtnStr = "timestamp";
			outFtnType = FTN_TIMESTAMP;
			break;
		case SQL_TIMESTAMP:
			outFtnStr = "datetime";
			outFtnType = FTN_DATETIME;
			break;
		case SQL_COUNTER:
			outFtnStr = "counter";
			outFtnType = FTN_COUNTER;
			break;
		case SQL_STRLST:
			outFtnStr = "varstrlst";
			outFtnType = (DDVARBIT | FTN_STRLST);
			break;
		case SQL_CDATE:
			outFtnStr = "date";
			outFtnType = FTN_DATE;
			break;
		case SQL_INTERNAL:
			outFtnStr = "internal";
			outFtnType = FTN_INTERNAL;
			outNonNull = 1;
			break;
#ifdef EPI_INT64_SQL
		case SQL_BIGINT:
			outFtnStr = "int64";
			outFtnType = FTN_INT64;
			break;
		case SQL_UINT64:
			outFtnStr = "uint64";
			outFtnType = FTN_UINT64;
			break;
#endif /* EPI_INT64_SQL */
		default:
			putmsg(MWARN, Fn, "Unsupported outtype %d", outtype);
			goto ok;                     /* MAW 06-09-95 */
	}

	/* KNG 20110224 Bug 1681: Preserve number of values, even if
	 * it means changing `*outlen' (byte length): `outcount' should
	 * equal `incount'.  `outFtnType' may be different size than
	 * `intype'/`outtype', due to promotion above (e.g. float to double);
	 * was using `outcount = *outlen/sizeof(type)' which lost 1/2 values:
	 */
	outcount = incount;

#ifndef NO_TRY_FAST_CONV
	/* Now that `inFtnType' and `outFtnType' have been set,
	 * check again to see if we can use fastconv():
	 */
	if (inFtnType == outFtnType && inNonNull == outNonNull)
	{
		ret = fastconv(indata, inlen, intype,
			/* Use `intype' for output type: we know it is same:*/
			intype, outdata, outlen, fo);
		goto done;
	}
#endif /* !NO_TRY_FAST_CONV */

	infld = createfld((char *)inFtnStr, incount, inNonNull);
	if (infld == FLDPN) goto err;
	putfld(infld, indata, incount);
	outfld = createfld((char *)outFtnStr, outcount, outNonNull);
	if (outfld == FLDPN) goto err;
	_fldcopy(infld, NULL, outfld, NULL, fo);
	/* KNG 20110223 bug: was setting `*outlen' to # items not byte
	 * length; was no consequence (until 20110224) only because
	 * caller ignored it:
	 */
	p = getfld(outfld, &outcount);
	if ((outfld->type & DDTYPEBITS) == FTN_INTERNAL)
	{
		*outdata = tx_fti_copy4read((ft_internal *)p,
				/* wtf WAG as to # elements: */
				inlen/FT_INTERNAL_ALLOC_SZ);
		datalen = inlen;		/* WAG "" */
	}
	else
	{
		datalen = outcount*outfld->elsz;
		*outdata = (char *)malloc(datalen+1);
		if (*outdata == CHARPN)
		{
			TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, Fn,
					datalen + 1, 1);
			goto err;
		}
		memcpy(*outdata, p, datalen);
		((char *)(*outdata))[datalen]='\0';/* MAW 04-14-95 - term for string data */
	}
	*outlen = datalen;
ok:
	ret = 0;
	goto done;

err:
	ret = -1;
done:
	if (outfld != FLDPN)
	{
		freeflddata(outfld);
		outfld = closefld(outfld);
	}
	if (infld != FLDPN) infld = closefld(infld);
	return(ret);
}
