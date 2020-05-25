/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"
#include "cgi.h"
#include "heap.h"


/******************************************************************/
/*
	A set of routines to handle database size info.

	Needs to be able to:

		- keep database size info in memory
		- query for total datasize, max db size, max table size
		- query for total rows, max db rows, max table rows
		- convert info to text for storage
		- reverse on read
		- read data to add
*/
/******************************************************************/

typedef struct DBSIZE {
	time_t	 validdata;
	char	 *rawdata;
	DATASIZE highestTableSize;
	char	*highestTableSizeName;
	/*   `fullestTableSize' is the byte size of the table in this
	 * database that is fullest percentage-wise in proportion to
	 * its license limit (thus, only non-unlimited tables are
	 * counted).  `...Limit' is that table's license limit (either
	 * table-specific or general license-wide): since it may be
	 * table-specific, it must be saved here for dbsizescan() use.
	 *   Note that since there are now per-schema license limits
	 * possible, TXlic_inviolation() must check these `fullest...'
	 * (potentially per-schema) limits, not the license-wide
	 * LICENSE.maxtrows limit.  Thus, we must ensure that if any
	 * table violates, it will be noted here as the `fullest...';
	 * thus the sorting function for `fullest...' is
	 * largest-percentage-of-limit, not necessarily biggest-table
	 * (e.g. a smaller table might violate its smaller limit):
	 */
	EPI_INT64	fullestTableSize;	/* -1: unset */
	EPI_INT64	fullestTableSizeLimit;	/* -1: unset */
	char		*fullestTableSizeName;
	DATASIZE totalSize;			/* sum of all tables' size */
	long	 highestTableRows;
	char	*highestTableRowsName;
	EPI_INT64	fullestTableRows;	/* -1: unset */
	EPI_INT64	fullestTableRowsLimit;	/* -1: unset */
	char		*fullestTableRowsName;
	byte		fullestTableSizeLimitIsSchemaLimit;
	byte		fullestTableRowsLimitIsSchemaLimit;
	DATASIZE totalRows;			/* sum of all tables' rows */
	struct DBSIZE *next;
	char	 *db;
} DBSIZE;
#define DBSIZEPN        ((DBSIZE *)NULL)

typedef struct STATEL {
	struct STATEL	*next;
	ft_counter	id;
	char		*Object;
	char 		*Stat;
	ft_long		value;
	char		*strval;
} STATEL;

static DBSIZE *DbsizeList = DBSIZEPN;

/******************************************************************/

static DBSIZE *findblock ARGS((char *db));

static DBSIZE *
findblock(db)
char *db;
{
	DBSIZE *c;

	for (c = DbsizeList; c; c = c->next)
		if (TXpathcmp(db, -1, c->db, -1) == 0)
			return c;
	return NULL;
}

/******************************************************************/

static int
addlblock(char *text, char *db, DATASIZE *highestTableSize,
	  const char *highestTableSizeName,
	  EPI_INT64 fullestTableSize,		/* -1: unset */
	  EPI_INT64 fullestTableSizeLimit,	/* -1: unset */
	  int	fullestTableSizeLimitIsSchemaLimit,
	  const char *fullestTableSizeName,	/* optional if no fullest */
	  DATASIZE *totalSize,
	  long highestTableRows, const char *highestTableRowsName,
	  EPI_INT64 fullestTableRows,		/* -1: unset */
	  EPI_INT64 fullestTableRowsLimit,	/* -1: unset */
	  int	fullestTableRowsLimitIsSchemaLimit,
	  const char *fullestTableRowsName,	/* optional if no fullest */
	  DATASIZE *totalRows, time_t tsent)
/* Adds information to static RAM linked list.
 */
{
	static const char	fn[] = "addlblock";
	DBSIZE *block;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	block = findblock(db);
	if(!block)
	{
		block = (DBSIZE *)TXcalloc(pmbuf, fn, 1, sizeof(DBSIZE));
		if(!block)
			return -1;
		block->db = TXstrdup(pmbuf, fn, db);
		block->next = DbsizeList;
		DbsizeList = block;
	}
	if(block->validdata > tsent)
		return 0;
	block->validdata = tsent;
	block->rawdata = TXfree(block->rawdata);
	block->rawdata = TXstrdup(pmbuf, fn, text);

	block->highestTableRows = highestTableRows;
	block->highestTableRowsName = TXfree(block->highestTableRowsName);
	if (highestTableRowsName)
		block->highestTableRowsName =
			TXstrdup(pmbuf, fn, highestTableRowsName);

	block->fullestTableRows = fullestTableRows;
	block->fullestTableRowsLimit = fullestTableRowsLimit;
	block->fullestTableRowsLimitIsSchemaLimit =
		fullestTableRowsLimitIsSchemaLimit;
	block->fullestTableRowsName =
		TXfree(block->fullestTableRowsName);
	if (fullestTableRowsName)
		block->fullestTableRowsName = TXstrdup(pmbuf, fn,
						 fullestTableRowsName);
	block->totalRows = *totalRows;
	block->totalSize = *totalSize;

	block->highestTableSize = *highestTableSize;
	block->highestTableSizeName = TXfree(block->highestTableSizeName);
	if (highestTableSizeName)
		block->highestTableSizeName =
			TXstrdup(pmbuf, fn,highestTableSizeName);

	block->fullestTableSize = fullestTableSize;
	block->fullestTableSizeLimit = fullestTableSizeLimit;
	block->fullestTableSizeLimitIsSchemaLimit =
		fullestTableSizeLimitIsSchemaLimit;
	block->fullestTableSizeName =
		TXfree(block->fullestTableSizeName);
	if (fullestTableSizeName)
		block->fullestTableSizeName = TXstrdup(pmbuf, fn,
						 fullestTableSizeName);
	return 0;
}

/******************************************************************/

static int
statelcmp(STATEL **a, STATEL **b)
{
	int rc;

	rc = strcmp((*a)->Object, (*b)->Object);
	if(rc)
		return rc;
	return(strcmp((*a)->Stat, (*b)->Stat));
}

/******************************************************************/

int
dbsizeadd(dbtbl, db)
DBTBL *dbtbl;
char *db;
/* Converts data from `dbtbl' -- a RAM table in SYSSTATISTICS format --
 * to text form, and adds to global static `DbsizeList' linked list,
 * noting highest table rows etc.
 * Returns -1 on error, 0 if ok.
 */
{
	static CONST char	fn[] = "dbsizeadd";
	static const char	whitespace[] = " \t\r\n\v\f";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	long nrows, i;
	int rc = -1;
	STATEL *sts = NULL, *el, **elarray = NULL;
	FLD *fid, *fob, *fst, *fva, *fsv;
	size_t	off;
	char		*obv, *stv, *svv, *csvdata, *t;
	ft_counter	*idv;
	ft_long		*vav;
	char *current = NULL, *last = NULL;
	long	crows = 0L;			/* current table rows */
	long	highestTableRows = 0L;		/* highest table rows seen */
	/* `fullestTable...' is the table with the highest percent-full
	 * rows/size in proportion to its limit (hence only non-unlimited
	 * tables are counted).  See notes in DBSIZE struct on why
	 * it must be a percentage:
	 */
	EPI_INT64	fullestTableRows = -1, fullestTableSize = -1;
	double		fullestTableRowsFrac = -1, fullestTableSizeFrac = -1;
	EPI_INT64	fullestTableRowsLimit = -1, fullestTableSizeLimit = -1;
	int	fullestTableRowsLimitIsSchemaLimit = 0;
	int	fullestTableSizeLimitIsSchemaLimit = 0;
	char	*fullestTableRowsName = NULL, *fullestTableSizeName = NULL;
	char	*highestTableRowsName = NULL, *highestTableSizeName = NULL;
	DATASIZE 	currentSize;
	DATASIZE	highestTableSize;	/* highest table size seen */
	DATASIZE	totalRows;		/* sum of all tables' rows */
	DATASIZE	totalSize;		/* sum of all tables' data */
	HTBUF	*out;
	/* `currentMaxTable{Rows,Size}' are the current table's
	 * per-table license limits, if any.  Note that -1 means
	 * unspecified, which means `defaultMaxTable{Rows,Size}' applies;
	 * -1 does not means unlimited (0 means unlimited):
	 */
	EPI_INT64	currentMaxTableRows, currentMaxTableSize;
	/* `defaultMaxTable{Rows,Size}' are the default license-wide
	 * limits; may be overridden on a particular table by
	 *  `currentMaxTable{Rows,Size}' (if latter is not -1):
	 */
	EPI_INT64	defaultMaxTableRows, defaultMaxTableSize;
	EPI_INT64	limit;
	double		frac;

	TXresetdatasize(&currentSize);
	TXresetdatasize(&highestTableSize);
	TXresetdatasize(&totalRows);
	TXresetdatasize(&totalSize);
	currentMaxTableRows = currentMaxTableSize = -1;
	defaultMaxTableRows = TXlicGetMaxTableRows();
	if (defaultMaxTableRows < 0)		/* error */
		defaultMaxTableRows = 0;	/* max unlimited wtf */
	defaultMaxTableSize = TXlicGetMaxTableSize();
	if (defaultMaxTableSize < 0)		/* error */
		defaultMaxTableSize = 0;	/* max unlimited wtf */

	if ((out = openhtbuf()) == HTBUFPN) goto err;
	htbuf_pf(out, "BEGIN %s\n", db);
	fid = dbnametofld(dbtbl, "id");
	fob = dbnametofld(dbtbl, "Object");
	fst = dbnametofld(dbtbl, "Stat");
	fva = dbnametofld(dbtbl, "NumVal");
	fsv = dbnametofld(dbtbl, "StrVal");

	/* Make `sts' list as copy of `dbtbl': */
	for (TXrewinddbtbl(dbtbl), nrows = 0; getdbtblrow(dbtbl); )
	{
		if ((fid->type & FTN_VarBaseTypeMask) != FTN_COUNTER ||
		    (fob->type & DDTYPEBITS) != FTN_CHAR ||
		    (fst->type & DDTYPEBITS) != FTN_CHAR ||
		    (fva->type & FTN_VarBaseTypeMask) != FTN_LONG ||
		    (fsv->type & DDTYPEBITS) != FTN_CHAR)
			continue;
		idv = (ft_counter *)getfld(fid, NULL);
		obv = (char *)getfld(fob, NULL);
		stv = (char *)getfld(fst, NULL);
		vav = (ft_long *)getfld(fva, NULL);
		svv = (char *)getfld(fsv, NULL);
		if (!idv || !obv || !stv || !vav || !svv) continue;
		if (*obv == '\0' || *stv == '\0') continue;

		if (!(el = (STATEL *)TXcalloc(pmbuf, fn, 1, sizeof(STATEL))))
			goto err;
		el->next = sts;		/* link first in case maerr */
		sts = el;
		el->id = *idv;
		if ((el->Object = TXstrdup(pmbuf, fn, obv)) == CHARPN ||
		    (el->Stat = TXstrdup(pmbuf, fn, stv)) == CHARPN)
			goto err;
		el->value = *vav;
		if ((el->strval = TXstrdup(pmbuf, fn, svv)) == CHARPN)
			goto err;
		nrows++;
		if (TXApp && (TXApp->traceLicense & 0x200))
		{
			char	tmp[256];
			TXdecodestr(tmp, sizeof(tmp),
			     /*#*/ "Received db %s stat %s %s %wkd %s" /*#*/);
			putmsg(MINFO, NULL, tmp, db, el->Object,
			       el->Stat, (EPI_HUGEINT)el->value, el->strval);
		}
	}

	/* Now sort by Object, Stat: */
	if (!(elarray = (STATEL **)TXcalloc(pmbuf, fn, nrows,
					    sizeof(STATEL *))))
		goto err;
	for (el = sts, i = 0; i < nrows && el != NULL; i++, el = el->next)
		elarray[i] = el;
	qsort(elarray, nrows, sizeof(STATEL *),
	      (int (*)ARGS((CONST void *,CONST void *)))statelcmp);

	/* Print the data to `out', and determine highest table rows/size: */
	for(i=0; i < nrows; i++)
	{
		el = elarray[i];
		if ((current = TXstrdup(pmbuf, fn, el->Object)) == CHARPN)
			goto err;
		t = strchr(current, '.');
		if(t) *t = '\0';			/* just table name */

		if(el->value || el->strval[0])
		{
			char	tmp[TX_COUNTER_HEX_BUFSZ];

			TXprintHexCounter(tmp, sizeof(tmp), &el->id);
			htbuf_pf(out, "%s,%s,%s,%ld,%s\n",
				 tmp, el->Object, el->Stat,
				(long)el->value, el->strval);
		}

		if(last && strcmp(current, last) != 0)	/* new object/table */
		{					/* flush current */
			TXadddatasize(&totalRows, crows);
			TXdatasizeadd(&totalSize, &currentSize);
			if(crows > highestTableRows)
			{
				highestTableRows = crows;
				highestTableRowsName =
					TXfree(highestTableRowsName);
				highestTableRowsName =
					TXstrdup(pmbuf, fn, last);
			}

			limit = (currentMaxTableRows != -1 ?
				 currentMaxTableRows : defaultMaxTableRows);
			frac = (limit >0 ? (double)crows/(double)limit : -1.0);
			if (limit > 0 &&	/* limited */
			    frac > fullestTableRowsFrac)
			{
				fullestTableRows = crows;
				fullestTableRowsLimit = limit;
				fullestTableRowsLimitIsSchemaLimit =
					(currentMaxTableRows != -1);
				fullestTableRowsFrac = frac;
				fullestTableRowsName =
					TXfree(fullestTableRowsName);
				fullestTableRowsName =
					TXstrdup(pmbuf, fn, last);
			}

			if (currentSize.gig > highestTableSize.gig ||
			    (currentSize.gig == highestTableSize.gig &&
			     currentSize.bytes > highestTableSize.bytes))
			{
				highestTableSize = currentSize;
				highestTableSizeName =
					TXfree(highestTableSizeName);
				highestTableSizeName =
					TXstrdup(pmbuf, fn, last);
			}

			limit = (currentMaxTableSize != -1 ?
				 currentMaxTableSize : defaultMaxTableSize);
			frac = (limit > 0 ?
			   (double)TX_GET_DATASIZE(&currentSize)/(double)limit
			       : -1.0);
			if (limit > 0 &&	/* limited */
			    frac > fullestTableSizeFrac)
			{
				fullestTableSize =
					TX_GET_DATASIZE(&currentSize);
				fullestTableSizeLimit = limit;
				fullestTableSizeLimitIsSchemaLimit =
					(currentMaxTableSize != -1);
				fullestTableSizeFrac = frac;
				fullestTableSizeName =
					TXfree(fullestTableSizeName);
				fullestTableSizeName =
					TXstrdup(pmbuf, fn, last);
			}

			/* Reset current-table values for next table: */
			crows = 0;
			TXresetdatasize(&currentSize);
			currentMaxTableRows = currentMaxTableSize = -1;
		}
		if(!strcmp(el->Stat, "NROWS"))	/* index/table rows */
		{
			if(crows < el->value)
				crows = el->value;
		}
		else if (strcmp(el->Stat, TXLIC_MAX_TABLE_ROWS_STAT_NAME) == 0)
		{
			EPI_INT64	i64;
			char		*e;
			int		errnum;

			/* Use `el->strval' not `el->value': latter is
			 * long and may overflow:
			 */
			i64 = TXstrtoi64(el->strval, NULL, &e, 0, &errnum);
			if (i64 >= 0 && e > el->strval && errnum == 0 &&
			    (!*e || strchr(whitespace, *e)))
				currentMaxTableRows = i64;
		}
		else if (strcmp(el->Stat, TXLIC_MAX_TABLE_SIZE_STAT_NAME) == 0)
		{
			EPI_INT64	i64;
			char		*e;
			int		errnum;

			i64 = TXstrtoi64(el->strval, NULL, &e, 0, &errnum);
			if (i64 >= 0 && e > el->strval && errnum == 0 &&
			    (!*e || strchr(whitespace, *e)))
				currentMaxTableSize = i64;
		}
		else if(el->Stat[0] != 'C' &&	/* non-cumulative */
			el->Stat[0] != '\0' &&
		   el->Stat[strlen(el->Stat)-1] == 'B')	/* bytes */
		{
			DATASIZE tsz;

			tsz.bytes = el->value;
			if(i < nrows -1 &&
			   elarray[i+1]->Stat[0] != '\0' &&
			   elarray[i+1]->Stat[strlen(elarray[i+1]->Stat)-1] == 'G')
			{				/* gigabytes */
				tsz.gig = elarray[i+1]->value;
			}
			else
				tsz.gig = 0;
			if(tsz.gig > currentSize.gig)
				currentSize = tsz;
			else if (tsz.gig == currentSize.gig &&
				 tsz.bytes > currentSize.bytes)
				currentSize = tsz;
		}
		last = TXfree(last);
		last = current;
		current = CHARPN;
	}
	/* flush last table: */
	TXadddatasize(&totalRows, crows);
	TXdatasizeadd(&totalSize, &currentSize);
	if(crows > highestTableRows)
	{
		highestTableRows = crows;
		highestTableRowsName = TXfree(highestTableRowsName);
		highestTableRowsName = TXstrdup(pmbuf, fn, last);
	}
	limit = (currentMaxTableRows != -1 ?
		 currentMaxTableRows : defaultMaxTableRows);
	frac = (limit > 0 ? (double)crows/(double)limit : -1.0);
	if (limit > 0 &&			/* limited */
	    frac > fullestTableRowsFrac)
	{
		fullestTableRows = crows;
		fullestTableRowsLimit = limit;
		fullestTableRowsLimitIsSchemaLimit =
			(currentMaxTableRows != -1);
		fullestTableRowsFrac = frac;
		fullestTableRowsName =
			TXfree(fullestTableRowsName);
		fullestTableRowsName = TXstrdup(pmbuf, fn, last);
	}
	if (currentSize.gig > highestTableSize.gig ||
	    (currentSize.gig == highestTableSize.gig &&
	     currentSize.bytes > highestTableSize.bytes))
	{
		highestTableSize = currentSize;
		highestTableSizeName = TXfree(highestTableSizeName);
		highestTableSizeName = TXstrdup(pmbuf, fn, last);
	}
	limit = (currentMaxTableSize != -1 ?
		 currentMaxTableSize : defaultMaxTableSize);
	frac = (limit > 0 ?
	       (double)TX_GET_DATASIZE(&currentSize)/(double)limit : -1.0);
	if (limit > 0 &&			/* limited */
	    frac > fullestTableSizeFrac)
	{
		fullestTableSize =
			TX_GET_DATASIZE(&currentSize);
		fullestTableSizeLimit = limit;
		fullestTableSizeLimitIsSchemaLimit =
			(currentMaxTableSize != -1);
		fullestTableSizeFrac = frac;
		fullestTableSizeName =
			TXfree(fullestTableSizeName);
		fullestTableSizeName = TXstrdup(pmbuf, fn, last);
	}

	if (TXApp && (TXApp->traceLicense & 0x100))
	{
		char	tmp[1024];

		TXdecodestr(tmp, sizeof(tmp), /*#*/ "Summary of received stats for db %s: highestTableSize: %wkd (%s) fullestTableSize: %wkd (%d%% of maximum %wkd) (%s) highestTableRows: %wkd (%s) fullestTableRows: %wkd (%d%% of maximum %wkd) (%s)" /*#*/);
			putmsg(MINFO, CHARPN, tmp, db,
			      (EPI_HUGEINT)TX_GET_DATASIZE(&highestTableSize),
			  (highestTableSizeName ? highestTableSizeName : "?"),
			       (EPI_HUGEINT)fullestTableSize,
			       (int)(fullestTableSizeFrac*100),
			       (EPI_HUGEINT)fullestTableSizeLimit,
		          (fullestTableSizeName ? fullestTableSizeName : "?"),
			       (EPI_HUGEINT)highestTableRows,
		          (highestTableRowsName ? highestTableRowsName : "?"),
			       (EPI_HUGEINT)fullestTableRows,
			       (int)(fullestTableRowsFrac*100),
			       (EPI_HUGEINT)fullestTableRowsLimit,
			  (fullestTableRowsName ? fullestTableRowsName : "?")
			       );
	}

	off = htbuf_getdata(out, CHARPPN, 0);
	htbuf_pf(out, "END a:%ld b:%ld c:%ld d:%ld e:%ld f:%ld g:%ld",
		highestTableRows, (long)highestTableSize.gig, (long)highestTableSize.bytes,
		 (long)totalRows.gig, (long)totalRows.bytes,
		 (long)totalSize.gig, (long)totalSize.bytes);
	htbuf_write(out, "\n", 1);
	htbuf_getdata(out, &csvdata, 0);
	addlblock(csvdata, db, &highestTableSize, highestTableSizeName,
		  fullestTableSize, fullestTableSizeLimit,
		  fullestTableSizeLimitIsSchemaLimit,
		  fullestTableSizeName, &totalSize, highestTableRows,
		  highestTableRowsName, fullestTableRows,
		  fullestTableRowsLimit, fullestTableRowsLimitIsSchemaLimit,
		  fullestTableRowsName, &totalRows, time(NULL));
	rc = 0;
	goto done;

err:
	rc = -1;
done:
	for ( ; sts != NULL; sts = el)
	{
		el = sts->next;
		sts->Object = TXfree(sts->Object);
		sts->Stat = TXfree(sts->Stat);
		sts->strval = TXfree(sts->strval);
		sts = TXfree(sts);
	}
	elarray = TXfree(elarray);
	out = closehtbuf(out);
	last = TXfree(last);
	current = TXfree(current);
	highestTableRowsName = TXfree(highestTableRowsName);
	fullestTableRowsName = TXfree(fullestTableRowsName);
	highestTableSizeName = TXfree(highestTableSizeName);
	fullestTableSizeName = TXfree(fullestTableSizeName);
	return rc;
}

/******************************************************************/
#ifdef NEVER /* WTF What were these for? */

dbsizewrite()
{
}

/******************************************************************/

dbsizeclose()
{
}

/******************************************************************/

dbsizeparse()
{
}

#endif
/******************************************************************/

static int sumcmp ARGS((void *a, void *b, void *usr));
static int
sumcmp(a, b, usr)
void	*a, *b, *usr;
/* Heap comparison function for dbsizescan() summary mode: keep track
 * of "most interesting" databases, those with highest limited table rows.
 */
{
	DBSIZE	*dbA = (DBSIZE *)a, *dbB = (DBSIZE *)b;
	double	aFrac, bFrac;

	(void)usr;

	/* Sort by license-limited table rows' fraction first;
	 * probably more interested in those than unlimited tables:
	 */
	aFrac = (dbA->fullestTableRowsLimit > 0 ?
     (double)dbA->fullestTableRows/(double)dbA->fullestTableRowsLimit : -1.0);
	bFrac = (dbB->fullestTableRowsLimit > 0 ?
     (double)dbB->fullestTableRows/(double)dbB->fullestTableRowsLimit : -1.0);
	if (aFrac > bFrac)
		return(-1);
	if (aFrac < bFrac)
		return(1);

	/* Sort by unlimited table rows next (i.e. in case completely
	 * unlimited license):
	 */
	if (dbA->highestTableRows > dbB->highestTableRows)
		return(-1);
	if (dbA->highestTableRows < dbB->highestTableRows)
		return(1);
	return(0);
}

static int listcmp ARGS((CONST void *a, CONST void *b));
static int
listcmp(a, b)
CONST void	*a, *b;
/* Sorts pointers in list in reverse to help make unique.
 */
{
	return((*(DBSIZE **)a) > (*(DBSIZE **)b) ? -1 :
		((*(DBSIZE **)a) < (*(DBSIZE **)b) ? 1 : 0));
}

static int
TXcopyPathTruncated(char *buf, size_t bufSz, const char *path)
/* Copies file `path' to `buf', truncating if needed.  Tries to keep
 * `buf' human-readable as a path, i.e. tries to remove dir components
 * atomically, put ellipsis in for missing section, preserve trailing
 * filename etc.
 * Returns 1 if `path' fits completely, 0 if not.
 */
{
	static const char	ellipsis[] = "...";
#define ELLIPSIS_LEN	(sizeof(ellipsis) - 1)
	size_t			pathLen, delLen;
	const char		*lastElement;
	const char		*delBegin, *delEnd;
	char			*d;

	pathLen = strlen(path);
	if (pathLen + 1 <= bufSz)		/* fits completely */
	{
		memcpy(buf, path, pathLen + 1);
		return(1);
	}

	/* Figure out how long a chunk to remove from `path'.
	 * The removed chunk will be replaced with `...', so we must
	 * delete that length additionally:
	 */
	delLen = ((pathLen + 1) - bufSz) + ELLIPSIS_LEN;
	if (delLen > pathLen)			/* `bufSz' too small */
	{
		if (bufSz > 0) *buf = '\0';
		return(0);
	}

	/* Set `lastElement' to last dir/file element of `path',
	 * including leading dir/drive separator:
	 */
	lastElement = TXbasename(path);
	if (!*lastElement)			/* e.g. path `/foo/bar/' */
	{
		for (lastElement--;
		     lastElement > path &&
			     !TX_IS_PATH_OR_DRIVE_SEP(lastElement[-1]);
		     lastElement--)
			;
		if (lastElement < path) lastElement = path;
	}
	if (lastElement > path) lastElement--;	/* include `/' */

	/* Stuff towards the end of `path' is probably more
	 * interesting, but save some from the start too.  Pick a
	 * delete start point such that 1/3 of the `path' remainder
	 * will be before it, and 2/3 after:
	 */
	for (delBegin = path + (pathLen - delLen)/3;
	     delBegin > path && !TX_IS_PATH_OR_DRIVE_SEP(delBegin[-1]);
	     delBegin--)
		;
	delEnd = delBegin + delLen;
	/* `lastElement' has priority; slide deletion left if overlap: */
	if (delEnd > lastElement)
	{
		delBegin -= (delEnd - lastElement);
		if (delBegin < path) delBegin = path;
		delEnd = delBegin + delLen;
	}

	/* Copy lead part of `path', then `...', then trail part of `path': */
	d = buf;
	memcpy(d, path, delBegin - path);
	d += (delBegin - path);
	memcpy(d, ellipsis, ELLIPSIS_LEN);
	d += ELLIPSIS_LEN;
	memcpy(d, delEnd, (path + pathLen + 1) - delEnd);
	return(0);
}

int
dbsizescan(license, exptime, summary)
LICENSE *license;	/* (in/out) alloc'd (not shmem) license object */
time_t exptime;
int     summary;
/* Sets license->dbsize, l[tds]{size|rows},
 * license->aux.tableStats.fullestTable{Rows,Size}[Limit] to
 * statistics data since `exptime'.  If `summary' is nonzero, will
 * only include "interesting" databases in license->dbsize, to
 * conserve space (eg. just before phone home): largest table/database
 * and a few others, up to ~8KB of data.
 * Returns 0 if ok, -1 on error.
 * >>>>> NOTE: caller should save and restore previous license->dbsize
 * >>>>> if `summary' is set.
 */
{
	static const char	fn[] = "dbsizescan";
	TXPMBUF	*pmbuf = TXPMBUFPN;
	DBSIZE *c;
	DATASIZE tds;
	size_t textsize;
	char *rawd, *d, *bufEnd;
	int	nlist = 0, i;
	FHEAP	*fh = FHEAPPN;
	DBSIZE	*list[4];
#define NLIST	(sizeof(list)/sizeof(list[0]))
#define MAXSUMDATA	(8*1024)
	const char	*ftStrs[TXLIC_FTSTR_NUM];
#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
	TXLICAUX_TBLSTATS	*auxTblStats = &license->aux.tableStats;
	double	fullestTableSizeFrac = -1, fullestTableRowsFrac = -1;
	double	curSizeFrac, curRowsFrac;
	EPI_INT64	tableRowsLicenseWideLimit;
	EPI_INT64	tableSizeLicenseWideLimit;

	tableRowsLicenseWideLimit = TXlicGetMaxTableRows();
	tableSizeLicenseWideLimit = TXlicGetMaxTableSize();
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */

	TXresetdatasize(&license->ltsize);		/* list[0] */
	TXresetdatasize(&license->ldsize);		/* list[1] */
	TXresetdatasize(&license->lssize);
	TXresetdatasize(&license->ltrows);		/* list[2] */
	TXresetdatasize(&license->ldrows);		/* list[3] */
	TXresetdatasize(&license->lsrows);
	TXlicClearAuxFlds(&license->aux, 0, 0);	/* just `tableStats' */
	memset(ftStrs, 0, sizeof(ftStrs));
	memset(list, 0, sizeof(list));
	textsize = 1;
	if (summary && (fh = openfheap(sumcmp, NULL, (HPF)0)) == FHEAPPN)
		return(-1);
	for (c = DbsizeList; c; c = c->next)
	{
#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
		/* Optimization: Update `fullestTable{Rows,Size}Limit'
		 * now if possible (i.e. if license-wide):
		 * license-wide limit may have just changed (perhaps
		 * increased) due to a license update, so try to
		 * reflect that ASAP to maybe get us out of
		 * violation.  Otherwise it will not be updated
		 * until next dbsizeadd(), i.e. next db monitor -s,
		 * which occur hourly and at db monitor start:
		 */
		if (!c->fullestTableSizeLimitIsSchemaLimit)
			c->fullestTableSizeLimit = tableSizeLicenseWideLimit;
		/* else limit is schema-specific: we do not know
		 * schema and thus must await db monitor -s
		 */
		if (!c->fullestTableRowsLimitIsSchemaLimit)
			c->fullestTableRowsLimit = tableRowsLicenseWideLimit;

		curSizeFrac = (c->fullestTableSizeLimit > 0 ?
	 (double)c->fullestTableSize/(double)c->fullestTableSizeLimit : -1.0);
		curRowsFrac = (c->fullestTableRowsLimit > 0 ?
	 (double)c->fullestTableRows/(double)c->fullestTableRowsLimit : -1.0);
		if (TXApp && (TXApp->traceLicense & 0x800))
		{
			char	tmp[1024];
			TXdecodestr(tmp, sizeof(tmp), /*#*/ "Scanning stats for db %s: highestTableSize: %wkd (%s) fullestTableSize: %wkd (%s, %d%% of maximum %wkd) (%s) highestTableRows: %wkd (%s) fullestTableRows: %wkd (%s, %d%% of maximum %wkd) (%s) valid: %at%s" /*#*/);
			putmsg(MINFO, CHARPN, tmp, c->db,
			   (EPI_HUGEINT)TX_GET_DATASIZE(&c->highestTableSize),
		    (c->highestTableSizeName ? c->highestTableSizeName : "?"),
			       (EPI_HUGEINT)c->fullestTableSize,
			       (c->fullestTableSizeLimitIsSchemaLimit ?
				"schema limit" : "license-wide limit"),
			       (int)(100.0*curSizeFrac),
			       (EPI_HUGEINT)c->fullestTableSizeLimit,
		    (c->fullestTableSizeName ? c->fullestTableSizeName : "?"),
			       (EPI_HUGEINT)c->highestTableRows,
		    (c->highestTableRowsName ? c->highestTableRowsName : "?"),
			       (EPI_HUGEINT)c->fullestTableRows,
			       (c->fullestTableRowsLimitIsSchemaLimit ?
				"schema limit" : "license-wide limit"),
			       (int)(100.0*curRowsFrac),
			       (EPI_HUGEINT)c->fullestTableRowsLimit,
		    (c->fullestTableRowsName ? c->fullestTableRowsName : "?"),

			       "%Y-%m-%d %H:%M:%S", (time_t)c->validdata,
			       (c->validdata < exptime ? " (expired)" : ""));
		}
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */ 
		if(c->validdata < exptime) continue;
		TXdatasizeadd(&license->lssize, &c->totalSize);
		if(TXdatasizecmp(&c->totalSize, &license->ldsize) > 0)
		{
			license->ldsize = c->totalSize;
			list[1] = c;
		}
		if(TXdatasizecmp(&c->highestTableSize, &license->ltsize) > 0)
		{
			license->ltsize = c->highestTableSize;
			list[0] = c;
		}
		TXdatasizeadd(&license->lsrows, &c->totalRows);
		if(TXdatasizecmp(&c->totalRows, &license->ldrows) > 0)
		{
			license->ldrows = c->totalRows;
			list[3] = c;
		}
		TXresetdatasize(&tds);
		TXadddatasize(&tds, c->highestTableRows);
		if(TXdatasizecmp(&tds, &license->ltrows) > 0)
		{
			license->ltrows = tds;
			list[2] = c;
		}
#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
		if (c->fullestTableRowsLimit > 0 &&	/* limited table */
		    curRowsFrac > fullestTableRowsFrac)
		{
			auxTblStats->fullestTableRows = c->fullestTableRows;
			auxTblStats->fullestTableRowsLimit =
				c->fullestTableRowsLimit;
			fullestTableRowsFrac = curRowsFrac;
			ftStrs[TXLIC_FTSTR_ROWS_DB] = c->db;
			ftStrs[TXLIC_FTSTR_ROWS_TABLE]=c->fullestTableRowsName;
		}
		if (c->fullestTableSizeLimit > 0 &&	/* limited table */
		    curSizeFrac > fullestTableSizeFrac)
		{
			auxTblStats->fullestTableSize = c->fullestTableSize;
			auxTblStats->fullestTableSizeLimit =
				c->fullestTableSizeLimit;
			fullestTableSizeFrac = curSizeFrac;
			ftStrs[TXLIC_FTSTR_SIZE_DB] = c->db;
			ftStrs[TXLIC_FTSTR_SIZE_TABLE]=c->fullestTableRowsName;
		}
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */
		textsize += strlen(c->rawdata);
		if (fh) fheap_insert(fh, c);
	}

#ifdef TX_PER_SCHEMA_LICENSE_LIMITS
	if (TXApp && (TXApp->traceLicense & 0x400))
	{
		char	tmp[1024];
		TXdecodestr(tmp, sizeof(tmp), /*#*/ "Summary of stats scan: highestTableSize: %wkd (%s:%s) fullestTableSize: %wkd (%d%% of maximum %wkd) (%s:%s) highestTableRows: %wkd (%s:%s) fullestTableRows: %wkd (%d%% of maximum %wkd) (%s:%s)" /*#*/);
		putmsg(MINFO, CHARPN, tmp,
		       (EPI_HUGEINT)TX_GET_DATASIZE(&license->ltsize),
		       (list[0] && list[0]->db ? list[0]->db : "?"),
		       (list[0] && list[0]->highestTableSizeName ?
			list[0]->highestTableSizeName : "?"),
		       (EPI_HUGEINT)auxTblStats->fullestTableSize,
		       (int)(100.0*fullestTableSizeFrac),
		       (EPI_HUGEINT)auxTblStats->fullestTableSizeLimit,
		       (ftStrs[TXLIC_FTSTR_SIZE_DB] ?
			ftStrs[TXLIC_FTSTR_SIZE_DB] : "?"),
		       (ftStrs[TXLIC_FTSTR_SIZE_TABLE] ?
			ftStrs[TXLIC_FTSTR_SIZE_TABLE] : "?"),
		       (EPI_HUGEINT)TX_GET_DATASIZE(&license->ltrows),
		       (list[2] && list[2]->db ? list[2]->db : "?"),
		       (list[2] && list[2]->highestTableSizeName ?
			list[2]->highestTableSizeName : "?"),
		       (EPI_HUGEINT)auxTblStats->fullestTableRows,
		       (int)(100.0*fullestTableRowsFrac),
		       (EPI_HUGEINT)auxTblStats->fullestTableRowsLimit,
		       (ftStrs[TXLIC_FTSTR_ROWS_DB] ?
			ftStrs[TXLIC_FTSTR_ROWS_DB] : "?"),
		       (ftStrs[TXLIC_FTSTR_ROWS_TABLE] ?
			ftStrs[TXLIC_FTSTR_ROWS_TABLE] : "?")
		       );
	}

	/* Copy `ftStrs' to `auxTblStats->fullestTablesStrings',
	 * compacting if needed:
	 */
	/* Try to equitably distribute space to `ftStrs'; the two
	 * table names are first, because they are max DDNAMESZ:
	 */
	d = auxTblStats->fullestTablesStrings;
	bufEnd = d + sizeof(auxTblStats->fullestTablesStrings);
	TXcopyPathTruncated(d, TX_MIN(bufEnd - d, DDNAMESZ),
			    (ftStrs[0] ? ftStrs[0] : ""));
	d += strlen(d) + 1;
	if (d < bufEnd)
	{
		TXcopyPathTruncated(d, TX_MIN(bufEnd-d, DDNAMESZ),
				    (ftStrs[1] ? ftStrs[1] : ""));
		d += strlen(d) + 1;
	}
	if (d < bufEnd)
	{
		/* We do not really care about TXLIC_FTSTR_SIZE_DB,
		 * so let TXLIC_FTSTR_ROWS_DB have as much space as needed:
		 */
		TXcopyPathTruncated(d, bufEnd - d,
				    (ftStrs[2] ? ftStrs[2] : ""));
		d += strlen(d) + 1;
	}
	/* Copy remaining strings: */
	for (i = 3; i < (int)TXLIC_FTSTR_NUM; i++)
		if (d < bufEnd)
		{
			TXcopyPathTruncated(d, bufEnd - d,
					    (ftStrs[i] ? ftStrs[i] : ""));
			d += strlen(d) + 1;
		}
	if (d < bufEnd) memset(d, '\0', bufEnd - d);
#endif /* TX_PER_SCHEMA_LICENSE_LIMITS */

	/* Set new LICDBSIZE(license) (raw text data): */
	LICDBSIZE(license) = TXfree(LICDBSIZE(license));
	if (fh)						/* summary mode */
	{
		qsort(list, NLIST, sizeof(list[0]), listcmp);
		for (nlist = 0, i = 1; i < (int)NLIST; i++)
		{				/* unique `list' */
			if (list[i] == list[nlist]) continue;
			list[++nlist] = list[i];
		}
		while (nlist >= 0 && list[nlist] == DBSIZEPN) nlist--;
		nlist++;
		for (textsize = 1, i = 0; i < nlist; i++)
			textsize += strlen(list[i]->rawdata);
		fheap_stkreset(fh);
		while (fheap_num(fh) > 0 && textsize < MAXSUMDATA)
		{					/* get up to 8K data */
			c = (DBSIZE *)fheap_top(fh);
			fheap_deletetop(fh);
			for (i = 0; i < nlist && list[i] != c; i++);
			if (i < nlist) continue;	/* got it already */
			fheap_stkpush(fh, c);
			textsize += strlen(c->rawdata);
		}
	}
	if ((rawd = TXmalloc(pmbuf, fn, textsize)) == CHARPN)
	{
		if (fh) closefheap(fh);
		return(-1);
	}
	*(d = rawd) = '\0';
	if (fh)						/* summary mode */
	{
		for (i = 0; i < nlist; i++, d += strlen(d))
			strcpy(d, list[i]->rawdata);
		for ( ; fheap_stknum(fh) > 0; d += strlen(d))
		{
			c = (DBSIZE *)fheap_stktop(fh);
			fheap_stkpop(fh);
			strcpy(d, c->rawdata);
		}
		closefheap(fh);
	}
	else						/* normal mode */
	{
		for (c = DbsizeList; c; c = c->next, d += strlen(d))
		{
			if(c->validdata < exptime) continue;
			strcpy(d, c->rawdata);
		}
	}
	LICDBSIZE(license) = rawd;
	/* WTF - Add text */
	return 0;
#undef NLIST
#undef MAXSUMDATA
}
