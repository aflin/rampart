/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "fdbi.h"
#include "cgi.h"

/******************************************************************/
/*String compare function for qsort*/
int TXqstrcmp ARGS((CONST void *a, CONST void *b));

int
TXqstrcmp(a, b)
CONST void      *a;
CONST void      *b;
{
	return strcmp(*(char **)a, *(char **)b);
}

/******************************************************************/

char *ddgettablecreator ARGS((DDIC *, char *));

static int canadmin ARGS((DDIC *, char *));

static int
canadmin(ddic, tname)
DDIC *ddic;
char *tname;
{
	char *tuname;
	char *myname;

	myname = TXgetusername(ddic);
	if(!myname)
		return 0;

	if(!strcmp(myname, "_SYSTEM")) /* _SYSTEM can remove anything */
		return 1;

	tuname = ddgettablecreator(ddic, tname);
	if(!tuname)
		return 1;
	if(!strcmp(myname, tuname)) /* I am creator */
	{
		tuname = TXfree(tuname);
		return 1;
	}
	tuname = TXfree(tuname);
	return 0;
}

/******************************************************************/

#define DELDBF(x, rc)	(deldbf(x) == -1 && errno != ENOENT ? (failedErrno = errno, rc = -1) : 0)
int
TXdelindex(iname, type)
char	*iname;	/* file path to index */
int	type;	/* INDEX_... type */
/* Returns 0 on success (all files removed), -1 on error (some remain).
 * Sets errno to error for failed removal.
 */
{
	static const char	fn[] = "TXdelindex";
	char	*fname, *d;
	int	rc = 0, failedErrno = 0;
	TXPMBUF	*pmbuf = TXPMBUFPN;

        fname = TXmalloc(pmbuf, fn, strlen(iname) + 8);
        if (fname == (char *)NULL)
		return(-1);
        strcpy(fname, iname);
        d = fname + strlen(fname);

	switch(type)
	{
		case INDEX_3DB:
		case INDEX_3CR:
		case INDEX_DEL:
		case INDEX_TEMP:
			strcpy(d, "_U.btr");
			DELDBF(fname, rc);

			strcpy(d, "_B.btr");
			DELDBF(fname, rc);

			strcpy(d, "_M.btr");
			DELDBF(fname, rc);

			strcpy(d, "_M.blb");
			DELDBF(fname, rc);

			strcpy(d, "_M.mdd");
			unlink(fname);

			strcpy(d, "_M.mdi");
			unlink(fname);

			strcpy(d, "_Y.btr");
			DELDBF(fname, rc);
			/* fall through */
                case INDEX_MM:
                case INDEX_MMCR:
                case INDEX_FULL:
                case INDEX_FULLCR:
			strcpy(d, ".btr");      /* dictionary */
			DELDBF(fname, rc);

                        strcpy(d, FDBI_DATSUF); /* word locations */
                        DELDBF(fname, rc);

                        strcpy(d, FDBI_TOKSUF); /* token -> recid map */
                        DELDBF(fname, rc);

			strcpy(d, "_D.btr");    /* deleted recid list */
			DELDBF(fname, rc);

			strcpy(d, "_T.btr");    /* new recid/auxdata list */
			DELDBF(fname, rc);

			strcpy(d, "_P.tbl");    /* param table (index exp.) */
			DELDBF(fname, rc);

			strcpy(d, "_X.btr");    /* (new index) delete list */
			DELDBF(fname, rc);

			strcpy(d, "_Z.btr");    /* (new index) new list */
			DELDBF(fname, rc);

			strcpy(d, "_C.btr");    /* MM counter index */
			DELDBF(fname, rc);

			strcpy(d, TXtempPidExt);
			unlink(fname);

			strcpy(d, TXxPidExt);
			unlink(fname);

			if(rc == 0)
			{
				*d = '\0';
				unlink(fname);
			}
			break;
		case INDEX_BTREE:
		case INDEX_INV:
		case INDEX_CR:
			strcpy(d, ".btr");
			DELDBF(fname, rc);
			if(rc == -1)
			{
				switch(errno)
				{
				case EPERM:
				case EACCES:
					break;
				case ENOENT:
					rc = 0;
					break;
				default:
					putmsg(MWARN, fn, "Could not remove index file %s: %s", fname, strerror(errno));
				}
			}
			break;
		default:
			putmsg(MWARN, fn,
			       "Unknown index type `%c' found for index `%s'",
			       type, iname);
			rc = -1;
	}
	fname = TXfree(fname);
	TX_INDEXDEBUG_MSG((999, fn,
		"(%u) Deleted type %c index %s files: %s",
		(unsigned)TXgetpid(0), type, iname,
		(rc == 0 ? "success" : "failed")));
	errno = failedErrno;
	return rc;
}

int
TXdelTableFile(tablePath, tableType)
CONST char	*tablePath; 	/* (in) file path to table (sans extension) */
int	        tableType;	/* TEXIS_..._TABLE type */
/* Deletes file(s) for table with path `tablePath' (sans extension).
 * Returns 0 on success (all files removed), -1 on error (some remain).
 */
{
	int	rc = 0;				/* success so far */
	int	failedErrno = 0;
	char	tempPath[PATH_MAX];

	switch (tableType)
	{
		case TEXIS_SYS_TABLE:		/* do not drop system tables*/
			return 0;		/* success */
		case TEXIS_TEMP_TABLE:
			if (TXcatpath(tempPath, tablePath, TXtempPidExt))
				DELDBF(tempPath, rc);
			else
				rc = -1;
			/* fall through: */
		case TEXIS_TABLE:
		case TEXIS_DEL_TABLE:
			if (!TXcatpath(tempPath, tablePath, ".tbl"))
				return(-1);
			DELDBF(tempPath, rc);
			if (TXcatpath(tempPath, tablePath, ".blb"))
				DELDBF(tempPath, rc);
			else
				rc = -1;
			break;
		case TEXIS_BTREEINDEX_TABLE:
		case 'b':	/* BTREE */
			if (!TXcatpath(tempPath, tablePath, ".btr"))
				return(-1);
			DELDBF(tempPath, rc);
			break;
	}
	errno = failedErrno;
	return(rc);
}

/******************************************************************/
/*
	Dropdindex - Remove an index from the database.
*/

int
TXdropdindex(ddic, iname)
DDIC	*ddic;
char	*iname;
{
	static const char	Fn[] = "TXdropdindex";
	char	*itype, **indn, **tbln, **fldn;
	int	i, ni;
	FLD	*f, *t, *fn, *tbn;
	char	typeVal[2];

	f = nametofld(ddic->indextbl, "NAME");
	tbn = nametofld(ddic->indextbl, "TBNAME");
	t = nametofld(ddic->indextbl, "TYPE");
	fn = nametofld(ddic->indextbl, "FNAME");
	if (!f)
	{
		return 0;
	}
	ni = ddgetindexbyname(ddic, iname, &itype, NULL, &indn, &tbln, &fldn,
			      CHARPPPN);
	if(ni<=0)                       /* MAW 03-02-94 - error check */
	{
		putmsg(MERR + FOE, Fn, "Could not find index %s in database",
		       iname, ddic->epname);
	}
	else
	{
		if(tbln && !canadmin(ddic, tbln[0]))
		{
			putmsg(MWARN, NULL, "Insufficient permissions to drop index %s",
				iname);
			return 0;
		}
		for (i=0; i < ni; i++)
		{
			PRECID	l;

#ifdef _WIN32
			f = nametofld(ddic->indextbl, "NAME");
			tbn = nametofld(ddic->indextbl, "TBNAME");
			t = nametofld(ddic->indextbl, "TYPE");
			fn = nametofld(ddic->indextbl, "FNAME");
			if (!f)
			{
				return 0;
			}
#endif /* _WIN32 */
			if (TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL)==-1)
				return(0);
			rewindtbl(ddic->indextbl);
			while(TXrecidvalid(l = gettblrow(ddic->indextbl, NULL)))
			{
				if (!TXstrcmp(getfld(f, NULL), iname) &&
				    !TXstrcmp(TXddicfname(ddic, getfld(fn, NULL)), indn[i]))
				{
					freedbf(ddic->indextbl->df, TXgetoff(l));
					TXddicSetSysindexChanged(ddic, 1); /* just in case no locks */
				}
			}
			/* Bug 3756: close indexes *before* trying to rm: */
			if (tbln[i])
				TXclosecacheindex(ddic, tbln[i]);
			if(TXdelindex(indn[i], itype[i])==-1)
			{
				typeVal[0] = INDEX_DEL;
				typeVal[1] = '\0';
				putfld(t, typeVal, 1);
				putfld(f, iname, strlen(iname));
				putfld(fn, indn[i], strlen(indn[i]));
				putfld(tbn, tbln[i], strlen(tbln[i]));
				puttblrow(ddic->indextbl, NULL);
				TXddicSetSysindexChanged(ddic, 1); /* just in case no locks */
			}

			/* Bug 4900: also remove from SYSSTATISTICS: */
			/* Bug 5230: do not remove: let TXupdateAndSendStats()
			 * clean it up later (up to an hour).  Removing here
			 * causes potential deadlock with another process'
			 * TXsetstatistic() (because latter gets SYSSTATISTICS
			 * then SYSINDEX locks, opposite order); we could
			 * avoid deadlock by doing the remove after we
			 * unlock SYSINDEX here, but that potentially
			 * removes a wholly-new-index's stats (if index
			 * re-created after our SYSINDEX unlock and before
			 * our statistics remove).
			 */

			TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
			tbln[i] = TXfree(tbln[i]);
			fldn[i] = TXfree(fldn[i]);
			indn[i] = TXfree(indn[i]);
		}
		tbln = TXfree(tbln);
		fldn = TXfree(fldn);
		itype = TXfree(itype);
		indn = TXfree(indn);
	}
	/* Bug 3685: Always clear SYSINDEX cache (even if using locks):
	 * may have been refreshed above, after freedbf(), but *before*
	 * puttblrow(), and thus be out-of-date with respect to latter:
	 */
	ddic->indtblcache = TXtblcacheClose(ddic->indtblcache);
	return ni>0;
}

/******************************************************************/
/*
	Dropindex - Remove an index from the database.
*/

int
TXdropindex(dbase, iname)
char	*dbase;
char	*iname;
{
	int	rc;
	DDIC	*ddic;

	ddic = ddopen(dbase);
	if (ddic == (DDIC *)NULL)
		return 0;
	rc = TXdropdindex(ddic, iname);
	ddclose(ddic);
	return rc;
}

/******************************************************************/
/*
	Droptable - Remove a table from the database.
*/

int
TXdroptable(dbase, tname)
char	*dbase;
char	*tname;
{
	int	rc;
	DDIC	*ddic;

	ddic = ddopen(dbase);
	if (ddic == (DDIC *)NULL)
		return 0;
	rc = TXdropdtable(ddic, tname);
	ddclose(ddic);
	return rc;
}

/******************************************************************/

int
TXdelsyscols(ddic, tbl)
DDIC *ddic;
char *tbl;
{
	int rc;
	long sz;

	if(TXddicstmt(ddic) == -1)
		return -1;
	rc = SQLPrepare(ddic->ihstmt,
			(byte *)"DELETE FROM SYSCOLUMNS WHERE TBNAME = ?;",
			SQL_NTS);
	if(rc != SQL_SUCCESS)
	{
		return -1;
	}
	TXpushid(ddic, 0, 0);
	sz = strlen(tbl);
	SQLSetParam(ddic->ihstmt, 1, SQL_C_CHAR, SQL_CHAR, 0, 0, tbl, &sz);
	SQLExecute(ddic->ihstmt);
	while(SQLFetch(ddic->ihstmt) == SQL_SUCCESS);
	TXpopid(ddic);
	return 0;
}

/******************************************************************/
/*
	Dropalltables - Remove all tables from the system catalogs.

	This will first drop all the indices associated with the table,
	and then drop the table
*/

int
TXdropdtables(ddic)
DDIC *ddic;	/* The database containing the table */
{
	FLD *f;
	char *tbl;

	if (TXlocksystbl(ddic, SYSTBL_TABLES, R_LCK, NULL) == -1)
		return(-1);
	while(TXrecidvalid(gettblrow(ddic->tabletbl, NULL)))
	{
		rewindtbl(ddic->tabletbl);
		f = nametofld(ddic->tabletbl, "NAME");
		tbl = getfld(f, NULL);
		if (!strcmp(tbl, "SYSTABLES"))
			continue;
		if (!strcmp(tbl, "SYSINDEX"))
			continue;
		if (!strcmp(tbl, "SYSUSERS"))
			continue;
		if (!strcmp(tbl, "SYSPERMS"))
			continue;
		if (!strcmp(tbl, "SYSTRIG"))
			continue;
		TXdropdtable(ddic, tbl);
	}
	TXunlocksystbl(ddic, SYSTBL_TABLES, R_LCK);
	return 0;
}

/******************************************************************/
/*
	Droptable - Remove a table from the system catalogs.

	This will first drop all the indices associated with the table,
	and then drop the table
*/

int
TXdropdtable(ddic, tbl)
DDIC *ddic;	/* The database containing the table */
char *tbl;	/* The name of the table being dropped */
/* Returns 0 on error, 1 on success.
 */
{
	static CONST char	fn[] = "TXdropdtable";
	char	type, *fname = NULL, *itype=NULL, *iunique=NULL, **iname=NULL;
	char	**ifields=NULL, **ifiles=NULL, *typeStr, *s;
	int	i, ni, rc, wasram = 0;
	PRECID	l;
	FLD	*nameFld, *typeFld, *whatFld, *fcFld, *creatorFld, *remarkFld;
	FLD	*f;
	char	typeVal[2], creatorVal[128];
	char	*whatVal = CHARPN;
	byte	fcVal[2];

	fcVal[0] = fcVal[1] = '\0';
	*creatorVal = '\0';

/*	Get the table out of the cache */

	TXrmcache(ddic, tbl, &wasram);
/*
	Find and drop the indices on this table
*/

	if(!canadmin(ddic, tbl))
	{
		putmsg(MWARN, NULL, "Insufficient permissions to drop table %s",
			tbl);
		return 0;
	}
	ni = TXddgetindexinfo(ddic, tbl, (char *)NULL, &itype, &iunique,
                              &iname, &ifiles, &ifields, NULL, NULL);
	if(ni < 0)                       /* MAW 03-02-94 - error check */
	{
		if(!TXtimedout(ddic))
			putmsg(MERR+FOE,(char *)NULL,
				"Error getting indices for table %s",tbl);
		return 0;
	}
	if (iname)
	{
		/* Only try and delete each index once, not once per entry  */
		qsort(iname, ni, sizeof(char *), TXqstrcmp);/* JMT 98-06-10 */
		for (i=0; i < ni; i++)
		{
			if(iname[i])
			{
				if(i == (ni - 1) ||
				   strcmp(iname[i], iname[i+1]))
					TXdropdindex(ddic, iname[i]);
				iname[i] = TXfree(iname[i]);
			}
		}
		iname = TXfree(iname);
	}
	if (ifiles)
	{
		for (i=0; i < ni; i++)
			ifiles[i] = TXfree(ifiles[i]);
		ifiles = TXfree(ifiles);
	}
	if (ifields)
	{
		for (i=0; i < ni; i++)
			ifields[i] = TXfree(ifields[i]);
		ifields = TXfree(ifields);
	}
	itype = TXfree(itype);
	iunique = TXfree(iunique);

	if(wasram)
		return 0;
/*	Get Base File Name	*/	

	fname = ddgettable(ddic, tbl, &type, 0);
	if (fname == (char *)NULL)
	{
		/* User-visible table not found.  Try harder: look for
		 * deleted table (e.g. from an earlier incomplete DROP):
		 */
		type = TEXIS_DEL_TABLE;
		fname = TXddgetanytable(ddic, tbl, &type, 0);
		if (fname == CHARPN)
		{
			putmsg(MERR + FOE, fn,
			       "Could not find table %s in database %s",
			       tbl, ddic->epname);
			return(0);
		}
	}

/*	Remove Entry from SYSTABLES */
	nameFld = nametofld(ddic->tabletbl, "NAME");
	typeFld = nametofld(ddic->tabletbl, "TYPE");
	whatFld = nametofld(ddic->tabletbl, "WHAT");
	fcFld = nametofld(ddic->tabletbl, "FC");
	creatorFld = nametofld(ddic->tabletbl, "CREATOR");
	remarkFld = nametofld(ddic->tabletbl, "REMARK");

	if (!nameFld || !typeFld || !whatFld) goto err;

	if (TXlocksystbl(ddic, SYSTBL_TABLES, W_LCK, NULL) == -1) goto err;
	rewindtbl(ddic->tabletbl);
	while(TXrecidvalid(l = gettblrow(ddic->tabletbl, NULL)))
		if (strcmp(getfld(nameFld, NULL), tbl) == 0 &&
		    (typeStr = (char *)getfld(typeFld, NULL)) != CHARPN &&
		    *typeStr == type)
		{
			/* Save FC and CREATOR values for later: */
			if (fcFld != FLDPN &&
			    (s = (char *)getfld(fcFld, NULL)) != CHARPN)
				*fcVal = *(byte *)s;
			if (creatorFld != FLDPN &&
			    (s = (char *)getfld(creatorFld, NULL)) != CHARPN)
				TXstrncpy(creatorVal, s, sizeof(creatorVal));
			if (whatFld != FLDPN &&
			    (s = (char *)getfld(whatFld, NULL)) != CHARPN)
				whatVal = TXstrdup(TXPMBUFPN, fn, s);
			freedbf(ddic->tabletbl->df, TXgetoff(l));
			/* KNG we are only deleting `fname's file(s) below,
			 * so only remove one (hopefully corresponding)
			 * SYSTABLES entry here, or we may orphan other
			 * entries' files (e.g. TEXIS_TEMP_TABLEs):
			 */
			break;
		}

/*	Remove Entries from SYSCOLUMNS */
#ifndef IMMEDIATE_DROP_TABLE
	TXdelsyscols(ddic, tbl);
#else
	if (TXlocksystbl(ddic, SYSTBL_COLUMNS, W_LCK, NULL) != -1)
	{
		rewindtbl(ddic->coltbl);
		f = nametofld(ddic->coltbl, "TBNAME");
		while (TXrecidvalid(l = gettblrow(ddic->coltbl, NULL)))
			if (!strcmp(getfld(f, NULL), tbl))
				freedbf(ddic->coltbl->df, TXgetoff(l));
		TXunlocksystbl(ddic, SYSTBL_COLUMNS, W_LCK);
	}
	else if (!TXsqlWasCancelled(ddic))
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn, "Could not remove table `%s' entries from SYSCOLUMNS: Could not obtain lock",
			       tbl);
#endif

/*	Remove Entries from SYSPERMS */
	if (TXlocksystbl(ddic, SYSTBL_PERMS, W_LCK, NULL) != -1)
	{
		rewindtbl(ddic->permstbl);
		f = nametofld(ddic->permstbl, "P_NAME");
		while (TXrecidvalid(l = gettblrow(ddic->permstbl, NULL)))
			if (!strcmp(getfld(f, NULL), tbl))
				freedbf(ddic->permstbl->df, TXgetoff(l));
		TXunlocksystbl(ddic, SYSTBL_PERMS, W_LCK);
	}
	else if (!TXsqlWasCancelled(ddic))
		txpmbuf_putmsg(ddic->pmbuf, MERR, fn, "Could not remove table `%s' entries from SYSPERMS: Could not obtain lock",
			       tbl);

	rc = TXdelTableFile(fname, type);

	if (rc != 0)				/* file rm not successful */
	{
		/* Mark table as deleted in SYSTABLES: */
		/* wtf why do we have to re-get the FLDs? fails otherwise */
		nameFld = nametofld(ddic->tabletbl, "NAME");
		typeFld = nametofld(ddic->tabletbl, "TYPE");
		whatFld = nametofld(ddic->tabletbl, "WHAT");
		fcFld = nametofld(ddic->tabletbl, "FC");
		creatorFld = nametofld(ddic->tabletbl, "CREATOR");
		remarkFld = nametofld(ddic->tabletbl, "REMARK");
		if (nameFld && typeFld && whatFld)
		{
			putfld(nameFld, tbl, strlen(tbl));
			typeVal[0] = TEXIS_DEL_TABLE;
			typeVal[1] = '\0';
			putfld(typeFld, typeVal, 1);
			if (whatVal)
				putfld(whatFld, whatVal, strlen(whatVal));
			else
				putfld(whatFld, fname, strlen(fname));
			if (fcFld != FLDPN)
				putfld(fcFld, fcVal, 1);
			if (creatorFld != FLDPN)
				putfld(creatorFld, creatorVal, strlen(creatorVal));
			if (remarkFld != FLDPN)
				putfld(remarkFld, "Deleted table", 13);
			puttblrow(ddic->tabletbl, NULL);
		}
	}

	/* Bug 5230: do not update stats here; see other Bug 5230 comment */
	/* TXdelstatistic(ddic, tbl, NULL); */	/* Bug 4900: clean stats */

	/* Now that we are *all* done with SYSTABLES, we can unlock: */
	TXunlocksystbl(ddic, SYSTBL_TABLES, W_LCK);

	/* Bug 3685: Always clear SYSTABLES cache (even if using locks):
	 * may have been refreshed above, after freedbf(), but *before*
	 * puttblrow(), and thus be out-of-date with respect to latter:
	 */
	ddic->tbltblcache = TXtblcacheClose(ddic->tbltblcache);
	if(!ddic->dblock)
		ddic->prmtblcache = TXtblcacheClose(ddic->prmtblcache);
	goto done;

err:
	rc = -1;				/* error */
done:
	fname = TXfree(fname);
	whatVal = TXfree(whatVal);
	return rc?0:1;
}

#ifdef TEST

void
usage(char *prog)
{
	fprintf(stderr, "usage: %s <directory> <object>\n", prog);
	exit(TXEXIT_INCORRECTUSAGE);
}

int
main(int argc, char *argv[])
{
	if (argc != 3)
		usage(argv[0]);
	if (!strcmp(argv[0], "dropi"))
		dropindex(argv[1], argv[2]);
	if (!strcmp(argv[0], "dropt"))
		droptable(argv[1], argv[2]);
	return TXEXIT_OK;
}

#endif
