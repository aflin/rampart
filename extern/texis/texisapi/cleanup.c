#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "txcoreconfig.h"
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"

int	TXdbCleanupVerbose = 0;
/* 0x01: removal success
 * 0x02: removal failure
 * 0x04: in-use checks
 */

/******************************************************************/

static int rmindex ARGS((char *fname, int type));

static int
rmindex(fname, type)
char    *fname;
int     type;
/* Removes index with path `fname', if not in use.
 * Returns -1 on error/still in use, 0 if index successfully removed.
 */
{
        int		pid = 0, procexists = 0, delindex = 0, ret;
        FILE            *f;
	CONST char	*msgType = "unknown";
	char		buf[PATH_MAX];

        switch(type)
        {
                case    INDEX_TEMP:
			msgType = "temp";
			if (!TXcatpath(buf, fname, TXtempPidExt)) return(-1);
                        f = fopen(buf, "rb");
                        if(f)
                        {
				if (fscanf(f, "%d", &pid) != 1) pid = 0;
                                fclose(f);
                        }
			/* Be conservative and only delete if we positively
			 * read the pid and know it doesn't exist: a missing
			 * PID file could still have the index in use.
			 */
			if (pid) procexists = TXprocessexists((PID_T)pid);
			if (pid && !procexists) delindex = 1;
			if (TXdbCleanupVerbose & 0x04)
				putmsg(MINFO, CHARPN,
		"DB Cleanup: %s index %s created by PID %u: process %s%s; %s",
				       msgType, fname, (unsigned)pid,
				       (procexists ? "exists" :
					"does not exist"), (f ? "" :
					      " (PID file does not exist)"),
				       (delindex ? "will remove" :
					"will not remove"));
                        break;
                case    INDEX_DEL:		/* marked for delete */
			msgType = "deleted";
			delindex = 1;
                        break;
                default:			/* unknown type: leave it */
			break;
        }
	ret = -1;
	if (delindex)
	{
		ret = TXdelindex(fname, type);
		if ((TXdbCleanupVerbose & 0x01) && ret == 0)
			putmsg(MINFO + FDE, CHARPN,
			       "DB Cleanup: successfully removed %s index %s",
			       msgType, fname);
		else if ((TXdbCleanupVerbose & 0x02) && ret != 0)
			putmsg(MINFO + FDE, CHARPN,
		               "DB Cleanup: failed to remove %s index %s: %s",
			       msgType, fname, strerror(errno));
	}
	TX_INDEXDEBUG_MSG((999, "rmindex",
		"(%u) Checked type %c index %s: PID %d %s: %s%s",
		(unsigned)TXgetpid(0), type, fname, pid,
		(procexists ? "exists" : "does not exist"),
		(delindex ? "deleted index" : "left index"),
		(delindex ? (ret == 0 ? ": success" : ": failed") : "")));
	return(ret);
}

static int rmtable ARGS((CONST char *tblPath, int type));
static int
rmtable(tblPath, type)
CONST char	*tblPath;
int		type;
/* Removes table with path `tblPath' (sans extension), if not in use.
 * Returns -1 on error/still in use, 0 if table successfully removed.
 */
{
	int	pid = 0, procexists = 0, deltable = 0, ret;
	FILE	*f;
	CONST char	*msgType = "unknown";
	char	buf[PATH_MAX];

        switch(type)
        {
	case TEXIS_TEMP_TABLE:
		msgType = "temp";
		if (!TXcatpath(buf, tblPath, TXtempPidExt)) return(-1);
		f = fopen(buf, "rb");
		if (f)
		{
			if (fscanf(f, "%d", &pid) != 1) pid = 0;
			fclose(f);
		}
		/* Be conservative and only delete if we positively
		 * read the pid and know it doesn't exist: a missing
		 * PID file could still have the table in use:
		 */
		if (pid) procexists = TXprocessexists((PID_T)pid);
		if (pid && !procexists) deltable = 1;
		if (TXdbCleanupVerbose & 0x04)
			putmsg(MINFO, CHARPN,
		"DB Cleanup: %s table %s created by PID %u: process %s%s; %s",
			       msgType, tblPath, (unsigned)pid, (procexists ?
					 "exists" : "does not exist"),
			       (f ? "" : " (PID file does not exist)"),
			       (deltable ? "will remove" :"will not remove"));
		break;
	case TEXIS_DEL_TABLE:			/* marked for delete */
		msgType = "deleted";
		deltable = 1;
		break;
	default:				/* unknown type: leave it */
		break;
        }
	ret = -1;
	if (deltable)
	{
		ret = TXdelTableFile(tblPath, type);
		if ((TXdbCleanupVerbose & 0x01) && ret == 0)
			putmsg(MINFO + FDE, CHARPN,
			       "DB Cleanup: successfully removed %s table %s",
			       msgType, tblPath);
		else if ((TXdbCleanupVerbose & 0x02) && ret != 0)
			putmsg(MINFO + FDE, CHARPN,
			       "DB Cleanup: failed to remove %s table %s: %s",
			       msgType, tblPath, strerror(errno));
	}
	TX_INDEXDEBUG_MSG((999, "rmtable",
		"(%u) Checked type %c table %s: PID %d %s: %s%s",
		(unsigned)TXgetpid(0), type, tblPath, pid,
		(procexists ? "exists" : "does not exist"),
		(deltable ? "deleted table" : "left table"),
		(deltable ? (ret == 0 ? ": success" : ": failed") : "")));
	return(ret);
}

/******************************************************************/

static int TXcleanupSystemTable ARGS((DDIC *ddic, int doTables));
static int
TXcleanupSystemTable(ddic, doTables)
DDIC    *ddic;
int	doTables;	/* (in) nonzero: do tables not indexes */
/* Perform the actual cleanup that is required.  Makes one
 * loop through SYSINDEX/SYSTABLES, removing deleted indexes/tables,
 * and temps not in use.
 * Returns -1 on error, 0 on success.
 */
{
        TBL     *tbl;
        FLD     *ft, *fn;
        RECID   *l;
        int     nu = 0, sysTblIdx = (doTables ? SYSTBL_TABLES : SYSTBL_INDEX);
	int	deletedARow = 0;
	char	fname[PATH_MAX];

        if (!ddic)
              return -1;
        tbl = (doTables ? ddic->tabletbl : ddic->indextbl);
        if (!tbl)
              return -1;
        ft = nametofld(tbl, "TYPE");
        fn = nametofld(tbl, (doTables ? "WHAT" : "FNAME"));
        if (!ft || !fn)
              return -1;
        nu = W_LCK;
        if (TXlocksystbl(ddic, sysTblIdx, nu, NULL) == -1)
		return(-1);
        rewindtbl(tbl);
        while(TXrecidvalid(l = gettblrow(tbl, NULL)))
        {
                char    type;
                char    *tmps, *pfx;
                size_t  sz;

                type = *(char *)getfld(ft, &sz);
                tmps = (char *)getfld(fn, &sz);
		/* KNG 20101101 do not unlock here (per John); otherwise
		 * someone else might rm, deltblrow(), and re-use this
		 * entry for non-temp usage.  Keep locked for duration.
		 */
		if (tmps[0] == PATH_SEP ||
		    TX_ISABSPATH(tmps))
			pfx = "";
		else
			pfx = ddic->pname;
		TX_INDEXDEBUG_MSG((999, "TXcleanupSystemTable",
			"(%u) Read %s SYS... row at 0x%wx: type %c file %s",
			(unsigned)TXgetpid(0), ddic->epname,
			(EPI_HUGEINT)TXgetoff2(l), type, tmps));
		if (!TXcatpath(fname, pfx, tmps)) continue;
		if (doTables)
			switch (type)
			{
                        case TEXIS_TEMP_TABLE:
			case TEXIS_DEL_TABLE:
                                if (rmtable(fname, type) == 0)
					goto afterOkDelFile;
                                break;
                        default:
                                break;
			}
		else switch(type)		/* index */
                {
                        case    INDEX_DEL:
                        case    INDEX_TEMP:
                                if(rmindex(fname, type) == 0)
                                {
				afterOkDelFile:
                                        deltblrow(tbl, l);
					deletedARow = 1;
					TX_INDEXDEBUG_MSG((999,
						"TXcleanupSystemTable",
		"(%u) Deleted %s SYS... row at 0x%wx: type %c file %s",
					(unsigned)TXgetpid(0), ddic->epname,
					(EPI_HUGEINT)TXgetoff2(l), type, tmps));
                                }
                                break;
                        default:
                                break;
                }
        }
        if(nu)
                TXunlocksystbl(ddic, sysTblIdx, nu);
	/* As a courtesy, updated the cache if we made it invalid: */
	if (deletedARow) makevalidtable(ddic, sysTblIdx);
        return 0;				/* success */
}

int
TXdocleanup(ddic)
DDIC    *ddic;
/* Tries to remove unused deleted and temp indexes and tables.
 * Returns 0 on success, -1 on error.
 */
{
	int	rc = 0;

	/* Cleanup SYSINDEX: */
	if (TXcleanupSystemTable(ddic, 0) != 0) rc = -1;

	/* Cleanup SYSTABLES: */
	if (TXcleanupSystemTable(ddic, 1) != 0) rc = -1;
	return(rc);
}

/******************************************************************/

#ifdef TEST
static int loopcleanup ARGS((DDIC *));
static int keeplooping = 1;

static int
loopcleanup(ddic)
DDIC    *ddic;
{
#ifdef TEST
        HANDLE     die;
        char       name[1024];
        char       *t;

        strcpy(name, ddic->pname);
        strcat(name, "CLEANUP");
        for(t=name; *t; t++)
        {
                *t = toupper(*t);
                if(*t == '\\')
                   *t = '_';
        }
        die = TXCreateEvent(name, FALSE, 1);
#endif /* TEST */
        while(keeplooping)
        {
#ifdef NEVER
#ifdef TEST
		int rc;
                if((rc = WaitForSingleObject(die, 30000)) != WAIT_FAILED)
		{
                     break;
		}
		if(rc == -1)
		{
			DWORD err;

			err = GetLastError();
			break;
		}
#else /* !TEST */
                sleep(30);
#endif /* !TEST */
#endif /* NEVER */
		Sleep(30000);
                TXdocleanup(ddic);
        }
        return 0;
}

int
main(int argc, char *argv[])
{
        DDIC  *ddic;

        ddic = ddopen(argv[1]);
        loopcleanup(ddic);
        ddic = ddclose(ddic);
}
#endif /* TEST */
