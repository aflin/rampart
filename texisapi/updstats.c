/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>
#ifdef HAVE_SYS_UN_H
#  include <sys/time.h>
#  include <sys/resource.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <sys/ioctl.h>
#endif
#if !defined(linux) && (defined(i386) || defined(__SVR4))
#  ifndef _SCO_C_DIALECT
#    include <sys/filio.h>                           /* for FIONBIO */
#  endif
#endif
#ifdef _WIN32
#  include <io.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"
#include "cgi.h"

/******************************************************************/

static int TXsendstats(DDIC *ddic, TXFHANDLE fh, int flags);


static int doupdate ARGS((DDIC *, char *, ft_counter *, long, long, int,
			  int *goPtr));
static int
doupdate(ddic, tblname, lastlook, lastrows, lastsize, force, goPtr)
DDIC *ddic;
char *tblname;
ft_counter *lastlook;
long lastrows, lastsize;
int force;
int	*goPtr;		/* (in, opt.) stop if `!*goPtr' */
{
	DBTBL	*dbtbl;
	EPI_OFF_T	nbytes, nrows;
	long	nMegabytes;

	(void)lastlook;
	(void)lastrows;
	(void)lastsize;
	if (goPtr && !*goPtr)
		return 0;
	dbtbl = opendbtbl(ddic, tblname);
	if(!dbtbl)
		return -1;
	nrows = 0;
	if (force)
		while ((!goPtr || *goPtr) && TXrecidvalid(getdbtblrow(dbtbl)))
		{
			nrows++;
		}
	nbytes = lseek(getdbffh(dbtbl->tbl->df), 0, SEEK_END);
	if (nbytes < (EPI_OFF_T)0) nbytes = (EPI_OFF_T)0;
	if(ddgetblobs(dbtbl->tbl->dd))
	{
		EPI_OFF_T	prev, n;

		prev = nbytes;
		n = lseek(getdbffh(dbtbl->tbl->bf),0,SEEK_END);
		if (n > (EPI_OFF_T)0)
		{
			nbytes += n;
			if (nbytes < prev) nbytes = (EPI_OFF_T)EPI_OFF_T_MAX;
		}
	}
	dbtbl = closedbtbl(dbtbl);
	if (!goPtr || *goPtr)
	{
		if(force)
			TXsetstatistic(ddic, tblname, "NROWS",
				       (nrows < (EPI_OFF_T)EPI_OS_LONG_MAX ?
					(long)nrows : (long)EPI_OS_LONG_MAX),
				       "", 0);
#if EPI_OFF_T_BITS > EPI_OS_LONG_BITS + 20
		if (nbytes > ((EPI_OFF_T)EPI_OS_LONG_MAX << 20))
			nMegabytes = (long)EPI_OS_LONG_MAX;
		else
#endif /* EPI_OFF_T_BITS > EPI_OS_LONG_BITS + 20 */
			nMegabytes = (long)(nbytes >> 20);
		TXsetstatistic(ddic, tblname, "NMEG", nMegabytes, "", 0);
	}
	return(nrows > 0 ? 1 : 0);
}
/******************************************************************/

static int TXupdateAndSendStatsListCmp(const void *a, const void *b)
{
	return(strcmp(*(char **)a, *(char **)b));
}

/******************************************************************/
/*
	The job of this guy is to update the stats of everything
	it can find in the database.

	- For all tables, update NROWS/NBYTES.

	- Get a list of all tables
	- Get a list of all tables we have stats for.

	- Start with the tables we don't have stats for.  This should
	  decrease with time, as we get stats for all tables.

	- Then do tables in reverse order of last updated.
*/

int
TXupdateAndSendStats(ddic, loops, fh, goPtr, flags)
DDIC	*ddic;
int	loops;
TXFHANDLE	fh;	/* (in, opt.) file to write to (default: shmem value
			 * of [Texis] Statistics Pipe) */
int	*goPtr;	/* (in, opt.) stop if `!*goPtr' */
int	flags;	/* (in) 0x01: verbose when actually sending
		 *      0x02: use blocking I/O
		 *    	0x04: force
		 */
/* Returns < 0 on error, 0 on success.
 */
{
#ifndef NO_KEEP_STATS
	static const char	fn[] = "TXupdateAndSendStats";
	DBTBL *stattbl = NULL, *tbltbl = NULL;
	ft_counter lastlook, oldest;
	FLD *namefld = NULL, *typeFld = NULL, *objectFld;
	char *tblname, *otbname, tableType, *s;
	long lastrows, oldrows = 0L, lastsize, oldsize = 0L;
	int n, ret, gotTableNames = 0;
	char	**tableNames = NULL, **indexStatNames = NULL;
	char	**indexNames = NULL, **indexTableNames = NULL;
	size_t	numTableNamesAlloced = 0, numIndexStatNamesAlloced = 0, i;
	size_t	numTableNames = 0, numIndexStatNames = 0, numIndexes = 0;
	char	*objectName;
	char	tmpStatName[256];

	stattbl = opendbtbl(ddic, TEXISSYSSTATS);
	if(!stattbl)
	{
		stattbl = TXcreatestatstable(ddic);
		if(!stattbl)
			goto err;
	}
	stattbl = closedbtbl(stattbl);
	tbltbl = opendbtbl(ddic, TEXISSYSTABLES);
	if (tbltbl) namefld = dbnametofld(tbltbl, "NAME");
	if (tbltbl) typeFld = dbnametofld(tbltbl, "TYPE");
	if (tbltbl && namefld)
	    do
	    {
		oldest.date = time(NULL);
		if (!(flags & 0x04))	
			oldest.date = oldest.date - 60;
		oldest.seq = 0;
		otbname = NULL;
		while(TXrecidvalid(getdbtblrow(tbltbl)))
		{
			tblname = getfld(namefld, NULL);

			/* Bug 4769: ignore del/temp/view/unknown tables: */
			if (typeFld &&
			    (s = (char *)getfld(typeFld, NULL)) != CHARPN)
				tableType = *s;
			else
				tableType = '?';
			switch (tableType)
			{
			case TEXIS_SYS_TABLE:
			case TEXIS_TABLE:
				break;
			default:		/* del/temp/view/unknown */
				continue;
			}

			if (!gotTableNames)
			{
				/* Save list of tables for later cleanup: */
				if (!TX_INC_ARRAY(ddic->pmbuf, &tableNames,
					numTableNames, &numTableNamesAlloced))
					goto err;
				tableNames[numTableNames] =
					TXstrdup(ddic->pmbuf, fn, tblname);
				if (!tableNames[numTableNames]) goto err;
				numTableNames++;
			}

			n = TXgetstatistic(ddic, tblname, "NMEG", &lastlook, &lastsize, NULL);
			if ((flags & 0x04) && (n == 0))
				n = TXgetstatistic(ddic, tblname, "NROWS", &lastlook, &lastrows, NULL);
			if(n == -1)
			{
				doupdate(ddic, tblname, NULL, 0, 0,
					 ((flags & 0x04) ? 1 : 0), goPtr);
			}
			else
			{
				if(lastlook.date < oldest.date) /* Don't care about SEQ order unlikely two stats done in same seconds anyway. */
				{
					oldest = lastlook;
					TXfree(otbname);
					otbname = strdup(tblname);
					oldrows = lastrows;
					oldsize = lastsize;
				}
			}
		}
		if (!gotTableNames)
		{
			gotTableNames = 1;
			/* Sort `tableNames' for later binary searches: */
			qsort(tableNames, numTableNames, sizeof(char *),
			      TXupdateAndSendStatsListCmp);
		}
		TXrewinddbtbl(tbltbl);
		if(otbname)
		{
			doupdate(ddic, otbname, &oldest, oldrows, oldsize,
				 ((flags & 0x04) ? 1 : 0), goPtr);
			free(otbname);
			otbname = NULL;
		}
		if(loops)
		{
			TXsleepmsec(20*1000, 0);
			loops--;
		}
		else
			break;
	    } while (!goPtr || *goPtr);
	tbltbl = closedbtbl(tbltbl);

	if (goPtr && !*goPtr) goto sendStats;

	/* Bug 4900: remove orphaned rows from SYSSTATISTICS.  First
	 * generate a list of valid `table.index' names in `indexStatNames':
	 */
	numIndexes = TXddgetindexinfo(ddic, NULL, NULL, NULL, NULL,
			     &indexNames, NULL, NULL, NULL, &indexTableNames);
	for (i = 0; i < numIndexes; i++)
	{
		if (!TX_INC_ARRAY(ddic->pmbuf, &indexStatNames,
				 numIndexStatNames, &numIndexStatNamesAlloced))
			goto err;
		if (htsnpf(tmpStatName, sizeof(tmpStatName), "%s.%s",
			   indexTableNames[i], indexNames[i]) >=
		    (int)sizeof(tmpStatName))
		{
			txpmbuf_putmsg(ddic->pmbuf, MERR + MAE, fn,
				       "Object name too large");
			/* Cannot safely prevent erroneous deletion of
			 * this statistic; bail on orphan cleanup:
			 */
			goto sendStats;
		}
		indexStatNames[numIndexStatNames] =
			TXstrdup(ddic->pmbuf, fn, tmpStatName);
		if (!indexStatNames[numIndexStatNames]) goto err;
		numIndexStatNames++;
	}
	qsort(indexStatNames, numIndexStatNames, sizeof(char *),
	      TXupdateAndSendStatsListCmp);
	indexNames = TXfreeStrList(indexNames, numIndexes);
	indexTableNames = TXfreeStrList(indexTableNames, numIndexes);

	/* Now iterate over SYSSTATISTICS and delete rows that are
	 * not valid tables nor valid indexes; Bug 4900:
	 */
	stattbl = opendbtbl(ddic, TEXISSYSSTATS);
	if (!stattbl) goto sendStats;
	objectFld = dbnametofld(stattbl, "Object");
	if (!objectFld || (objectFld->type & DDTYPEBITS) != FTN_CHAR)
	{
		stattbl = closedbtbl(stattbl);
		goto sendStats;
	}
	if (TXlocktable(stattbl, R_LCK) == -1) goto sendStats;
	while (TXrecidvalid(getdbtblrow(stattbl)) &&
	       (!goPtr || *goPtr))
	{
		size_t	l, r;
		int	cmp;

		objectName = (char *)getfld(objectFld, NULL);
		if (!objectName) continue;
		/* Check for `objectName' in table list: */
		l = i = 0;
		r = numTableNames;
		while (l < r)			/* binary search */
		{
			i = ((l + r) / 2);
			cmp = strcmp(objectName, tableNames[i]);
			if (cmp < 0) r = i;
			else if (cmp > 0) l = i + 1;
			else break;		/* found it */
		}
		if (l < r) continue;		/* found as table: valid */
		/* Check for `objectName' in index stat name list: */
		l = i = 0;
		r = numIndexStatNames;
		while (l < r)			/* binary search */
		{
			i = ((l + r) / 2);
			cmp = strcmp(objectName, indexStatNames[i]);
			if (cmp < 0) r = i;
			else if (cmp > 0) l = i + 1;
			else break;		/* found it */
		}
		if (l < r) continue;		/* found as index: valid */
		/* `objectName' does not refer to a valid table nor index;
		 * remove all its stats:
		 */
		TXdelstatistic(ddic, objectName, NULL);
	}
	TXunlocktable(stattbl, R_LCK);
	stattbl = closedbtbl(stattbl);

	/* Now send stats, after we've cleaned them up: */
sendStats:
	TXsendstats(ddic, fh, (flags & 0x03));
#endif /* !NO_KEEP_STATS */
	ret = 0;				/* success */
	goto done;

err:
	ret = -1;
done:
	tableNames = TXfreeStrList(tableNames, numTableNames);
	indexNames = TXfreeStrList(indexNames, numIndexes);
	indexTableNames = TXfreeStrList(indexTableNames, numIndexes);
	indexStatNames = TXfreeStrList(indexStatNames, numIndexStatNames);
	stattbl = closedbtbl(stattbl);
	tbltbl = closedbtbl(tbltbl);
	return(ret);
}

/******************************************************************/

static int
TXsendstats(ddic, fh, flags)
DDIC *ddic;
TXFHANDLE	fh;
/* (in, opt.) file to write to (default: shmem [Texis] Statistics Pipe) */
int     flags;
/* `flags':
 *   0x01  verbose
 *   0x02  blocking I/O:  do not set non-blocking I/O
 *         (may cause segfault and/or kernel panic under Linux 1.2.13)
 */
{
	static const char	fn[] = "TXsendstats";
	TXPMBUF			*pmbuf = ddic->pmbuf;
	EPI_OFF_T		rowsSent = 0;
	FLDOP			*fo = NULL;
	int			closeFh = 0;
#ifdef _WIN32
	int	rc;
	char	sockname[LIC_MAXSTRSZ];

	if (flags & 0x01)
		txpmbuf_putmsg(pmbuf, MINFO, fn, "Begin sending stats for %s",
			       ddic->epname);
	if (TXFHANDLE_IS_VALID(fh))
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, fn,
		"Internal error: Specific-file not yet supported in Windows");
		goto err;
	}
	if(!TXlic_getshmval(LICVAL_STATS_PIPE, NULL, sockname))
		goto err;

	fh = CreateFile(sockname, GENERIC_READ + GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, 0, 0);
	if (!TXFHANDLE_IS_VALID(fh)) goto err;
	closeFh = 1;
#else /* !_WIN32 */
	int	rc, isnb = 0;
	char	sockname[LIC_MAXSTRSZ];
	struct  sockaddr_un cl;
        struct  linger lin;
#  ifdef macintosh
	long   	ioarg = 1;
#  else
	ulong   ioarg = 1;
#  endif
        fd_set  rdbits, wrbits, exbits;
        struct timeval tv;
        time_t  end;

	if (flags & 0x01)
		txpmbuf_putmsg(pmbuf, MINFO, fn,
			       "Begin sending stats for %s", ddic->epname);
        end = time(TIME_TPN) + (time_t)30;
	if (!TXFHANDLE_IS_VALID(fh))
	{
		cl.sun_family = AF_UNIX;
		if(!TXlic_getshmval(LICVAL_STATS_PIPE, NULL, sockname))
			goto err;
		TXstrncpy(cl.sun_path, sockname, sizeof(cl.sun_path));
		TXseterror(0);
		fh = socket(AF_UNIX, SOCK_STREAM, 0);
		if (!TXFHANDLE_IS_VALID(fh))
		{
			txpmbuf_putmsg(pmbuf, MERR, fn,
				    "Cannot create socket for stats pipe: %s",
				       TXstrerror(TXgeterror()));
			goto err;
		}
		closeFh = 1;

		lin.l_onoff = 1;                /* set linger */
		lin.l_linger = 30;
		setsockopt(fh, SOL_SOCKET, SO_LINGER,(char*)&lin,sizeof(lin));

		TXseterror(0);			/* set non-blocking mode */
		isnb = (!(flags & 0x02) && ioctl(fh, FIONBIO, &ioarg) == 0);

		TXseterror(0);
		if (connect(fh, (struct sockaddr *)&cl, sizeof(cl)) != 0
#  ifdef EINPROGRESS
		    && errno != EINPROGRESS
#  endif
#  ifdef EWOULDBLOCK
		    && errno != EWOULDBLOCK
#  endif
		    )
		  {
		    txpmbuf_putmsg(pmbuf, MERR, fn,
	     "Cannot connect to stats pipe `%s' to send stats for db %s: %s",
				   cl.sun_path, ddic->epname,
				   TXstrerror(TXgeterror()));
		    goto err;
		  }

		do				/* wait for connect() */
		  {
		    FD_ZERO(&rdbits);
		    FD_ZERO(&wrbits);
		    FD_ZERO(&exbits);
		    FD_SET(fh, &wrbits);
		    FD_SET(fh, &exbits);
		    tv.tv_sec = end - time(TIME_TPN);
		    if ((long)tv.tv_sec <= 0)
		      {
                        txpmbuf_putmsg(pmbuf, MERR, fn,
                "Timeout waiting for stats pipe `%s' to send stats for db %s",
                                       cl.sun_path, ddic->epname);
		      terr:
#  ifdef ETIMEDOUT
			errno = ETIMEDOUT;
#  endif
			goto err;
		      }
		    tv.tv_usec = 0;
		    TXseterror(0);
		  }
		while ((rc=select(fh+1,&rdbits,&wrbits,&exbits,&tv)) == -1 &&
		       (TXgeterror() == EINTR
#  ifdef EAGAIN
			|| TXgeterror() == EAGAIN
#  endif
			));
		if (rc < 0 || !FD_ISSET(fh, &wrbits))
                  {
                    txpmbuf_putmsg(pmbuf, MERR, fn,
          "select() failed for stats pipe `%s' to send stats for db `%s': %s",
                                   cl.sun_path, ddic->epname,
                                   (rc < 0 ? TXstrerror(TXgeterror()) :
                                    "socket not writable"));
                    goto terr;
                  }

		ioarg = 0L;                    /* turn off non-blocking */
		if (isnb && ioctl(fh, FIONBIO, &ioarg) != 0)
			txpmbuf_putmsg(pmbuf, MWARN, fn,
			       "Unable to turn off non-blocking mode");

		/* WTF leave non-blocking on, and TXdumptable() timeout? */
	}
#endif /* !_WIN32 */

	fo = dbgetfo();
	if (!fo) goto err;
	rc = TXdumptable(ddic, TEXISSYSSTATS, CHARPN, fo, -1,
#ifdef _WIN32
			 -1,
#endif /* _WIN32 */
			 fh, TXFHANDLE_IS_VALID(fh), TXMDT_NONE, &rowsSent);
	goto finally;

err:
	rc = -1;
finally:
	fo = foclose(fo);
	if (TXFHANDLE_IS_VALID(fh) && closeFh)
	{
		(void)TXfhandleClose(fh);
		fh = TXFHANDLE_INVALID_VALUE;
	}
	if (flags & 0x01)
		txpmbuf_putmsg(pmbuf, MINFO, fn,
			       "End sending %wd stat%s for %s",
			       (EPI_HUGEINT)rowsSent,
			       (rowsSent == 1 ? "" : "s"), ddic->epname);
	return rc;
}
