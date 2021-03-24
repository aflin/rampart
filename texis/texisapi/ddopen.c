/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#if defined(unix) || defined(__unix)
#  include <fcntl.h>
#  ifndef _AIX
#    include <sys/fcntl.h>
#  endif /* !AIX */
#endif /* unix || __unix */
#ifdef _WIN32
#  include <io.h>			/* access() */
#  include <fcntl.h>
#  ifdef __BORLANDC__
#    include <time.h>
#  endif /* __BORLANDC__ */
#endif /* _WIN32 */
#include <limits.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include "os.h"
#include "mmsg.h"
#include "dirio.h"
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"
#include "cgi.h"
#include "http.h"


#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#endif

static char LastDbMonPath[PATH_MAX];
static time_t LastDbMonTime = (time_t) 0;

/******************************************************************/

int
TXddicnewproc(ddic)
DDIC *ddic;
{
	static CONST char Fn[] = "ddicnewproc";

	if (!ddic)
		return -1;
	TXgetpid(1);
	if (!TXsingleuser)
	{
		ddic->dblock = opendblock(ddic);
		if (!ddic->dblock)
		{
			txpmbuf_putmsg(ddic->pmbuf, MERR, Fn,
				       "Could not open locking mechanism");
			return -1;
		}
		ddic->tid = addltable(ddic->pmbuf, ddic->dblock,
				      TEXISSYSTABLES);
		ddic->cid = addltable(ddic->pmbuf, ddic->dblock,
				      TEXISSYSCOLUMNS);
		ddic->iid = addltable(ddic->pmbuf, ddic->dblock,
				      TEXISSYSINDEX);
		if (ddic->userstbl != (TBL *) NULL)
			ddic->uid = addltable(ddic->pmbuf, ddic->dblock,
					      TEXISSYSUSERS);
		if (ddic->permstbl != (TBL *) NULL)
			ddic->pid = addltable(ddic->pmbuf, ddic->dblock,
					      TEXISSYSPERMS);
		if (ddic->trigtbl != (TBL *) NULL)
			ddic->rid = addltable(ddic->pmbuf, ddic->dblock,
					      TEXISSYSTRIG);
	}
	return 0;
}

/******************************************************************/

DDIC *
ddopen(pname)
const char *pname;
{
	TXDDOPENFLAG	flags = (TXDDOPENFLAG)0;

	if (TXsingleuser)
		flags |= TXDDOPENFLAG_READ_ONLY;
	if (TXlicIsLicenseMonitor())
		flags |= TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY;
	return(TXddopen(TXPMBUFPN, pname, flags));
}

/******************************************************************/

PID_T
tx_chkrundbmonitor(ddic, now)
DDIC *ddic;
time_t now;
/* Checks for and runs database monitor, if needed.  `now' is current time.
 * Returns -1 on error, 0 if ok (but nothing exec'd), else PID of db monitor
 * exec'd.
 */
{
	char *monpath;
	int	i;
	PID_T	dbMonPid = 0;			/* ok, nothing exec'd */
#  ifdef _WIN32
	char *cmdline = NULL, *arg;
	HTBUF	*buf = HTBUFPN;
	int	q;
	STARTUPINFO siStartInfo;
	PROCESS_INFORMATION piProcInfo;
	HANDLE hNullF;
#  else	/* !_WIN32 */
	PID_T pid;
#    define MAXARGS	64
	char	*args[MAXARGS];
	int	nargs;
#  endif /* !_WIN32 */

	if(TXApp->NoMonitorStart)
		goto done;
	if (ddic->dblock &&
	    TXddicGetDbMonitorPid(ddic) == 0 &&
	    (monpath = TXgetmonitorpath()) != CHARPN &&
	    (now - LastDbMonTime > 5 || strcmp(ddic->pname, LastDbMonPath)))
	{
		errno = 0;
		if (!fexecutable(monpath))	/* check _before_ exec KNG */
		{		/* WTF warning or error? */
			txpmbuf_putmsg(ddic->pmbuf, MWARN + PRM, CHARPN,
			       "Cannot exec database monitor %s: %s", monpath,
			       strerror(errno));
			goto err;
		}
		else
		{
#ifndef _WIN32
			char	cmdArgs[PATH_MAX];

			htsnpf(cmdArgs, sizeof(cmdArgs), "%s -d %s -z",
			       monpath, ddic->epname);
			pid = TXfork(ddic->pmbuf, "Database monitor", cmdArgs,
				     0xe);
			switch (pid)
			{
			case -1:	/* error */
				dbMonPid = (PID_T)(-1);
				break;
			case 0:	/* child */
				chdir("/");	/* don't sit on NFS */
				/* KNG 20100325 leave full path in args,
				 * so we can tell in ps where this monitor
				 * exe is (install dir vs. test dir etc.):
				 */
				nargs = 0;
				args[nargs++] = monpath;
				for (i = 0;
				     i < TxGlobalOptsArgc && i + 5 < MAXARGS;
				     i++)
					args[nargs++] = TxGlobalOptsArgv[i];
				args[nargs++] = "-d";
				args[nargs++] = ddic->epname;
				args[nargs++] = "-z";
				args[nargs] = CHARPN;
				execv(monpath, args);
				_exit(TXEXIT_CANNOTEXECMON);	/* last resort if failure */
			}
			/* Add to misc. process list so Vortex wait() knows it: */
			dbMonPid = pid;
#else /* _WIN32 */
			hNullF =
				CreateFile("NUL",
					   GENERIC_READ | GENERIC_WRITE,
					   FILE_SHARE_READ | FILE_SHARE_WRITE,
					   NULL, OPEN_ALWAYS, 0, NULL);
			ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
			siStartInfo.cb = sizeof(STARTUPINFO);
			siStartInfo.lpReserved = NULL;
			siStartInfo.lpReserved2 = NULL;
			siStartInfo.lpTitle = NULL;
			siStartInfo.cbReserved2 = 0;
			siStartInfo.lpDesktop = NULL;
			siStartInfo.hStdInput = hNullF;
			siStartInfo.hStdOutput = hNullF;
			siStartInfo.hStdError = hNullF;
			siStartInfo.dwFlags =
				(STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
			siStartInfo.wShowWindow = SW_HIDE;

			if ((buf = openhtbuf()) == HTBUFPN) goto err;
			htbuf_setpmbuf(buf, ddic->pmbuf, 0x3);
			/* KNG 20100325 leave full path in args,
			 * so we can tell in ps where this monitor
			 * exe is (install dir vs. test dir etc.):
			 */
			q = (int)monpath[strcspn(monpath, " \t\r\n\v\f")];
			htbuf_pf(buf, "%s%s%s", (q ? "\"" : ""),
				monpath, (q ? "\"" : ""));
			for (i = 0; i < TxGlobalOptsArgc; i++)
			{
				arg = TxGlobalOptsArgv[i];
				q = (int)arg[strcspn(arg, " \t\r\n\v\f")];
				htbuf_pf(buf, " %s%s%s", (q ? "\"" : ""),
					arg, (q ? "\"" : ""));
			}
			htbuf_pf(buf, " -d \"%s\" -z", ddic->epname);
			htbuf_getdata(buf, &cmdline, 1);
			if (cmdline)
			{
				size_t cmdlnsz = strlen(cmdline);
				if(cmdline[cmdlnsz-1] == '\"' &&
				   cmdline[cmdlnsz-2] == '\\')
				{
					cmdline[cmdlnsz-1] = '\0';
					cmdline[cmdlnsz-2] = '\"';
				}
			}
			if (CreateProcess(monpath,
					  cmdline,
					  NULL,
					  NULL,
					  FALSE,
					  CREATE_NEW_PROCESS_GROUP,
					  NULL,
					  NULL, &siStartInfo, &piProcInfo))
			{			/* success */
				CloseHandle(piProcInfo.hThread);
				CloseHandle(piProcInfo.hProcess);
				dbMonPid = (PID_T)piProcInfo.dwProcessId;
			}
			else		/* CreateProcess() failed */
				dbMonPid = (PID_T)(-1);
#endif /* _WIN32 */
			/* Try not to start the same database monitor very often;
			 * give the first a chance to start:
			 */
			LastDbMonTime = now;
			TXstrncpy(LastDbMonPath, ddic->pname, PATH_MAX);
		}
	}
	goto done;

err:
	dbMonPid = (PID_T)(-1);
done:
#ifdef _WIN32
	buf = closehtbuf(buf);
	cmdline = TXfree(cmdline);
#endif /* _WIN32 */
	return(dbMonPid);
}

/******************************************************************/
/*	Open a data dictionary.  Effectively opens a database.
 *
 *	Returns: a handle to a DDIC structure if successful and
 *	NULL if there was an error.
 *
 *	Comments:  This function may be extended to handle security
 *	and login functions.
 */

#ifdef _WIN32
#  define DUMMYFILE "nul:"
#  define MINFILE 4
#else
#  define DUMMYFILE "/dev/null"
#  define MINFILE 2
#endif

DDIC *
TXddopen(pmbuf, pname, flags)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer to clone for messages */
const char *pname;			/* Path to the database to open */
TXDDOPENFLAG	flags;	/* (in) bit flags */
{
	static CONST char Fn[] = "TXddopen";
	time_t now;
	char tname[PATH_MAX];
	char fname[PATH_MAX];
	DDIC *ddic = DDICPN;
	int fd;
	size_t epnamelen, tnameLen, fnameLen, appendLen;
	TXCREATELOCKSMETHOD	methodsToTry[TXCREATELOCKSMETHOD_NUM];

	if (TXinitapp(pmbuf, NULL, 0, CHARPPN, INTPN, CHARPPPN) > 0)
		goto err;

	/* Make sure stdin/stdout/stderr file descriptors are already
	 * open, so that none of our e.g. SYS* KDBF file descriptors
	 * that we are about to open use a std #: could cause
	 * corruption if something writes to a std handle that is
	 * actually a DBF:
	 */
	do
	{
		fd = open(DUMMYFILE, O_RDONLY, 0666);
		if (fd > MINFILE)		/* already past std #s */
                  {
			close(fd);
			fd = -1;
			break;
		  }
	}
	while ((fd >= 0) && (fd <= MINFILE));

	/* Ignore [Texis] Createlocks Methods and only open direct,
	 * if requested by `flags':
	 */
	if (flags & TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY)
	{
		int	i;

		for (i = 0; i < (int)TXCREATELOCKSMETHOD_NUM; i++)
			methodsToTry[i] = TXCREATELOCKSMETHOD_UNKNOWN;
		methodsToTry[0] = TXCREATELOCKSMETHOD_DIRECT;
	}
	else
		memcpy(methodsToTry, TXApp->createLocksMethods,
		       TXCREATELOCKSMETHOD_NUM*sizeof(TXCREATELOCKSMETHOD));

	TXgetpid(1);
	ddic = (DDIC *) TXcalloc(pmbuf, Fn, 1, sizeof(DDIC));
	if (ddic == (DDIC *) NULL) goto err;
	ddic->pmbuf = txpmbuf_open(pmbuf);	/* attach to buffer */

	tname[sizeof(tname) - 1] = 'x';
	TXstrncpy(tname, pname, sizeof(tname));
	if (tname[sizeof(tname) - 1] != 'x')
	{
		txpmbuf_putmsg(pmbuf, MERR + MAE, Fn,
			       "Database path `%s' too long", pname);
		goto err;
	}
	if ((tnameLen = strlen(tname)) == 0)
	{
		strcpy(tname, ".");
		tnameLen = 1;
	}
	if (tnameLen > (size_t)1 && TX_ISPATHSEP(tname[tnameLen - 1]))
		tname[tnameLen - 1] = '\0';	/* remove trailing slash */
#ifdef _WIN32
	_fullpath(fname, tname, PATH_MAX);
#else
	fullpath(fname, tname, PATH_MAX);
#endif
	if (access(fname, F_OK) != 0)		/* cannot access dir */
	{
/*		if (mkdir(fname, 0755) == -1) */
		{
			if (!(flags & TXDDOPENFLAG_IGNORE_OPEN_FAILURES))
				txpmbuf_putmsg(pmbuf, MERR + FOE, Fn,
					       "Cannot open %s: %s",
					       fname, strerror(errno));
			goto err;
		}
	}
	fnameLen = strlen(fname);
	if (fnameLen > (size_t)0 && !TX_ISPATHSEP(fname[fnameLen - 1]))
	{					/* append a trailing slash */
		fname[fnameLen++] = PATH_SEP;
		fname[fnameLen] = '\0';
	}
	TXstrncpy(tname, fname, sizeof(tname));
	tnameLen = strlen(tname);
	appendLen = sizeof(TEXISFNSYSTABLES TX_TABLE_FILE_EXT) - 1;/*-1: nul*/
	if (tnameLen + appendLen >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSTABLES TX_TABLE_FILE_EXT);
	if (access(tname, F_OK) != 0)		/* cannot access SYSTABLES */
	{
		if (!(flags & TXDDOPENFLAG_IGNORE_OPEN_FAILURES))
			txpmbuf_putmsg(pmbuf, MERR + FOE,Fn,
				       "Cannot open %s: no SYSTABLES", fname);
		goto err;
	}
	strcpy(tname + tnameLen, "SYSDESTROYED");
	if (0 == access(tname, F_OK))
	{
		txpmbuf_putmsg(pmbuf, MERR + FOE, Fn,
			       "Cannot open %s: database destroyed", fname);
		goto err;
	}
	ddic->pname = TXstrdup(pmbuf, Fn, fname);
	ddic->tbspc = TXstrdup(pmbuf, Fn, "");
	ddic->indspc = TXstrdup(pmbuf, Fn, "");
	ddic->indrctspc = ddic->pname;
	if (!ddic->pname || !ddic->tbspc || !ddic->indspc)
		goto err;
	ddic->epname = TXstrdup(pmbuf, Fn, ddic->pname);
	if (!ddic->epname)
		goto err;
	epnamelen = strlen(ddic->epname);
	if (epnamelen >= 2 && TX_ISPATHSEP(ddic->epname[epnamelen-1]))
	{					/* strip trailing slash */
		ddic->epname[epnamelen-1] = '\0';
	}

	/* Open lock structure: */
	if (!(flags & TXDDOPENFLAG_READ_ONLY))
	{
#ifdef LOCK_SERVER
		ddic->dblock = opendblock(ddic);
#else
		ddic->dblock = TXdblockOpenViaMethods(pmbuf, ddic->epname,
			 methodsToTry, TXApp->createLocksTimeout, &ddic->sid);
#endif
		if (!ddic->dblock) goto err;
		ddic->dblock->ddic = ddic;
	}
	else
	{
		ddic->nolocking = 1;
	}

	/* Start the database monitor, unless no-db-monitor was requested: */
	now = time(NULL);
	if (!(flags & TXDDOPENFLAG_NO_DB_MONITOR))
		tx_chkrundbmonitor(ddic, now);

	/* `tname' + TEXISFNSYSTABLES size was checked above: */
	strcpy(tname + tnameLen, TEXISFNSYSTABLES);
	ddic->tabletbl = opentbl(pmbuf, tname);
	if (ddic->tabletbl == (TBL *) NULL)
	{
		if (!(flags & TXDDOPENFLAG_IGNORE_OPEN_FAILURES))
			txpmbuf_putmsg(pmbuf, MERR, Fn,
				       "Could not open SYSTABLES");
		goto err;
	}
	ddic->tid = addltable(pmbuf, ddic->dblock, TEXISSYSTABLES);
#ifdef OPEN_SYSCOLUMNS
	if (tnameLen + sizeof(TEXISFNSYSCOLUMNS) >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSCOLUMNS);
	ddic->coltbl = opentbl(pmbuf, tname);
	if (ddic->coltbl == (TBL *) NULL)
	{
		if (!(flags & TXDDOPENFLAG_IGNORE_OPEN_FAILURES))
			txpmbuf_putmsg(pmbuf, MERR, Fn,
				       "Could not open SYSCOLUMNS");
		goto err;
	}
	ddic->cid = addltable(pmbuf, ddic->dblock, TEXISSYSCOLUMNS);
#endif
	if (tnameLen + sizeof(TEXISFNSYSINDEX) >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSINDEX);
	ddic->indextbl = opentbl(pmbuf, tname);
	if (ddic->indextbl == (TBL *) NULL)
	{
		if (!(flags & TXDDOPENFLAG_IGNORE_OPEN_FAILURES))
			txpmbuf_putmsg(pmbuf, MERR, Fn,
				       "Could not open SYSINDEX");
		goto err;
	}

	/* SYSTABLES, SYSCOLUMNS, SYSINDEX appear to be the only
	 * required tables; if this changes, update TXcreateDb()
	 * `requiredFiles'.
	 */

	ddic->iid = addltable(pmbuf, ddic->dblock, TEXISSYSINDEX);
	if (tnameLen + sizeof(TEXISFNSYSUSERS) >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSUSERS);
	ddic->userstbl = opentbl(pmbuf, tname);
	if (ddic->userstbl != (TBL *) NULL)
		ddic->uid = addltable(pmbuf, ddic->dblock, TEXISSYSUSERS);
	if (tnameLen + sizeof(TEXISFNSYSPERMS) >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSPERMS);
	ddic->permstbl = opentbl(pmbuf, tname);
	if (ddic->permstbl != (TBL *) NULL)
		ddic->pid = addltable(pmbuf, ddic->dblock, TEXISSYSPERMS);
	if (tnameLen + sizeof(TEXISFNSYSTRIG) >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, TEXISFNSYSTRIG);
	ddic->trigtbl = opentbl(pmbuf, tname);
	if (ddic->trigtbl != (TBL *) NULL)
		ddic->rid = addltable(pmbuf, ddic->dblock, TEXISSYSTRIG);

#ifdef USE_SYSTABLE_INDICES
	error these function calls should use pmbuf;
#  define SYSTABLE_INDEX_LEN	8
	if (tnameLen + SYSTABLE_INDEX_LEN >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, "SYS1.btr");
	if (access(tname, R_OK | W_OK) == 0)	/* SYS1.btr exists */
	{
		strcpy(tname + tnameLen, "SYS1");
		ddic->tablendx = openbtree(tname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
#  ifdef OPEN_SYSCOLUMNS
	if (tnameLen + SYSTABLE_INDEX_LEN >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, "SYS2.btr");
	if (access(tname, R_OK | W_OK) == 0)	/* SYS2.btr exists */
	{
		strcpy(tname + tnameLen, "SYS2");
		ddic->colndx = openbtree(tname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
	if (tnameLen + SYSTABLE_INDEX_LEN >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, "SYS3.btr");
	if (access(tname, R_OK | W_OK) == 0)	/* SYS3.btr exists */
	{
		strcpy(tname + tnameLen, "SYS3");
		ddic->coltblndx = openbtree(tname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
#  endif /* OPEN_SYSCOLUMNS */
	if (tnameLen + SYSTABLE_INDEX_LEN >= sizeof(tname))
		goto absPathTooLong;
	strcpy(tname + tnameLen, "SYS4.btr");
	if (access(tname, R_OK | W_OK) == 0)	/* SYS4.btr exists */
	{
		strcpy(tname + tnameLen, "SYS4");
		ddic->indexndx = openbtree(tname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
#endif /* USE_SYSTABLE_INDICES */
	if (ddic->tabletbl == (TBL *) NULL ||
#ifdef OPEN_SYSCOLUMNS
	    ddic->coltbl == (TBL *) NULL ||
#endif
	    ddic->indextbl == (TBL *) NULL)
		goto err;
	ddic->ddcache = TXopencache(pmbuf);
	TXsetstddic(ddic);
#ifdef NEVER
	TXstartcleanup(ddic);
#endif
#ifdef DO_PARAMS
	error function call should use pmbuf;
	TXreadparms(ddic);
#endif
	ddic->starttime = now;	/* Set start time */
	ddic->manuallocking = 0;	/* Manual locking off */
	TXddicdefaultoptimizations(ddic);
	ddic->ch = TXlic_skipaddhit()? 0 : 1;

	ddic->messages[MESSAGES_DUPLICATE] = 1;
	ddic->messages[MESSAGES_FAILED_DELETE] = 1;
	ddic->messages[MESSAGES_TIME_FDBI] = 0;
	/* ddic->rlocks, ddic->wlocks cleared by calloc() */
#ifdef SQLLOGGING
	error function call should use pmbuf;
	TXlogsqlinit(ddic);
#endif
	goto done;

absPathTooLong:
	txpmbuf_putmsg(pmbuf, MERR + MAE, Fn,
		       "Absolute database path `%s' too long", fname);
	/* fall through to err */

err:
	ddic = ddclose(ddic);
done:
	return ddic;
}

/******************************************************************/

DDIC *
ddreset(ddic)
DDIC *ddic;
{
	char fname[PATH_MAX];

/* CLOSE */

	if (ddic->tabletbl != (TBL *) NULL)
		ddic->tabletbl = closetbl(ddic->tabletbl);
	if (ddic->coltbl != (TBL *) NULL)
		ddic->coltbl = closetbl(ddic->coltbl);
	if (ddic->indextbl != (TBL *) NULL)
		ddic->indextbl = closetbl(ddic->indextbl);
	if (ddic->userstbl != (TBL *) NULL)
		ddic->userstbl = closetbl(ddic->userstbl);
	if (ddic->permstbl != (TBL *) NULL)
		ddic->permstbl = closetbl(ddic->permstbl);
	if (ddic->trigtbl != (TBL *) NULL)
		ddic->trigtbl = closetbl(ddic->trigtbl);

	if (ddic->tablendx != (BTREE *) NULL)
		ddic->tablendx = closebtree(ddic->tablendx);
	if (ddic->colndx != (BTREE *) NULL)
		ddic->colndx = closebtree(ddic->colndx);
	if (ddic->coltblndx != (BTREE *) NULL)
		ddic->coltblndx = closebtree(ddic->coltblndx);
	if (ddic->indexndx != (BTREE *) NULL)
		ddic->indexndx = closebtree(ddic->indexndx);
/* REOPEN */

	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSTABLES);
	ddic->tabletbl = opentbl(ddic->pmbuf, fname);
	if (ddic->tabletbl == (TBL *) NULL)
		return ddclose(ddic);
#ifdef OPEN_SYSCOLUMNS
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSCOLUMNS);
	ddic->coltbl = opentbl(ddic->pmbuf, fname);
	if (ddic->coltbl == (TBL *) NULL)
		return ddclose(ddic);
#endif
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSINDEX);
	ddic->indextbl = opentbl(ddic->pmbuf, fname);
	if (ddic->indextbl == (TBL *) NULL)
		return ddclose(ddic);

	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSUSERS);
	ddic->userstbl = opentbl(ddic->pmbuf, fname);

	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSPERMS);
	ddic->permstbl = opentbl(ddic->pmbuf, fname);

	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSTRIG);
	ddic->trigtbl = opentbl(ddic->pmbuf, fname);

	strcpy(fname, ddic->pname);
	strcat(fname, "SYS1.btr");
	if (access(fname, R_OK | W_OK) == 0)	/* SYS1.btr exists */
	{
		strcpy(fname, ddic->pname);
		strcat(fname, "SYS1");
		ddic->tablendx = openbtree(fname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
	strcpy(fname, ddic->pname);
	strcat(fname, "SYS2.btr");
	if (access(fname, R_OK | W_OK) == 0)	/* SYS2.btr exists */
	{
		strcpy(fname, ddic->pname);
		strcat(fname, "SYS2");
		ddic->colndx = openbtree(fname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
	strcpy(fname, ddic->pname);
	strcat(fname, "SYS3.btr");
	if (access(fname, R_OK | W_OK) == 0)	/* SYS3.btr exists */
	{
		strcpy(fname, ddic->pname);
		strcat(fname, "SYS3");
		ddic->coltblndx = openbtree(fname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
	strcpy(fname, ddic->pname);
	strcat(fname, "SYS4.btr");
	if (access(fname, R_OK | W_OK) == 0)	/* SYS4.btr exists */
	{
		strcpy(fname, ddic->pname);
		strcat(fname, "SYS4");
		ddic->indexndx = openbtree(fname, BT_MAXPGSZ, 20, 0, O_RDWR);
	}
	if (ddic->tabletbl == (TBL *) NULL ||
#ifdef OPEN_SYSCOLUMNS
	    ddic->coltbl == (TBL *) NULL ||
#endif
	    ddic->indextbl == (TBL *) NULL)
		return ddclose(ddic);
	return ddic;
}

/******************************************************************/

int
ddicsetstate(ddic, state)
DDIC *ddic;
int state;
/* Thread-safe.  Signal-safe.
 */
{
	int os;

	os = ddic->state;
	ddic->state = state;
	return os;
}

/******************************************************************/

int
TXddicvalid(DDIC * ddic, char **reason)
/* Returns 1 if `ddic' is still valid (database still exists), 0 if not.
 */
{
	static char	buf[128];
	EPI_STAT_S	stb;

	if (!ddic)
	{
		if (reason) *reason = "NULL DDIC";
		return (0);
	}
	if (EPI_FSTAT(getdbffh(ddic->tabletbl->df), &stb) == 0 &&
	    stb.st_nlink == 0)
	{
		if (reason) *reason = "SYSTABLES deleted";
		return 0;
	}
#ifndef LOCK_SERVER
	if (ddic->dblock && ddic->dblock->idbl)
	{
		if (TXminserver == 0 &&
		    TXddicGetDbMonitorPid(ddic) != TXgetpid(0))
		{
			if (reason)
			{
			sprintf(buf,
			  "Database Monitor slot 0 changed from pid %u to %u",
				(unsigned)TXgetpid(0),
				(unsigned)TXddicGetDbMonitorPid(ddic));
				*reason = buf;
			}
			return 0;
		}
		if (TXminserver > 0 &&
		    ddic->dblock->idbl->servers[ddic->sid].pid != TXgetpid(0))
		{
			if (reason)
			{
			sprintf(buf,"Server slot %d changed from pid %u to %u",
				(int)ddic->sid,
				(unsigned)TXgetpid(0),
				(unsigned)ddic->dblock->idbl->servers[ddic->sid].pid);
				*reason = buf;
			}
			return 0;
		}
	}
#endif /* LOCK_SERVER */
	return 1;
}

/******************************************************************/

int
TXddicdefaultoptimizations(DDIC *ddic)
/* Returns 0 on error.
 */
{
	ddic->optimizations[OPTIMIZE_JOIN] = 1;
	ddic->optimizations[OPTIMIZE_COMPOUND_INDEX] = 1;
	ddic->optimizations[OPTIMIZE_COPY] = 1;
	ddic->optimizations[OPTIMIZE_COUNT_STAR] = 1;
	ddic->optimizations[OPTIMIZE_MINIMAL_LOCKING] = 1;
	ddic->optimizations[OPTIMIZE_GROUP] = 1;
	ddic->optimizations[OPTIMIZE_FASTSTATS] = 1;
	ddic->optimizations[OPTIMIZE_READLOCK] = 1;
	ddic->optimizations[OPTIMIZE_ANALYZE] = 1;
	ddic->optimizations[OPTIMIZE_SKIPAHEAD] = 1;
#if defined(_WIN32)
	ddic->optimizations[OPTIMIZE_AUXDATALEN] = 1;
#else
	ddic->optimizations[OPTIMIZE_AUXDATALEN] = 0;
#endif
	ddic->optimizations[OPTIMIZE_INDEXONLY] = 1;
	ddic->optimizations[OPTIMIZE_MMIDXUPDATE] = 1;
	ddic->optimizations[OPTIMIZE_INDEXDATAGROUP] = 1;
	ddic->optimizations[OPTIMIZE_LIKE_WITH_NOTS] = 1;
	ddic->optimizations[OPTIMIZE_SHORTCUTS] = 1;
	/* KNG 20070424 indexbatchbuild is off by default, so that table
	 * can be modified during metamorph index builds.  Turning it on
	 * enables read buffering of table, which may speed up builds on
	 * slow-lseek-on-large-file platforms.  It is effectively always on
	 * for non-Metamorph index builds, since there is no delete/new list:
	 */
	ddic->optimizations[OPTIMIZE_INDEX_BATCH_BUILD] = 0;
	/* KNG 20080104 linearrankindexexps is off by default, for speed.
	 * Can be turned on for better linear-search (new-list) ranking
	 * (but takes ~70% more user time).  Note that other issues also
	 * cause linear/index ranks to differs, eg. linear no-index LIKEP
	 * does not check all rows, and table-frequency will be missing:
	 */
	ddic->optimizations[OPTIMIZE_LINEAR_RANK_INDEX_EXPS] = 0;
	ddic->optimizations[OPTIMIZE_PTRS_TO_STRLSTS] = 1;
	ddic->optimizations[OPTIMIZE_SORTED_VARFLDS] = 1;
	ddic->optimizations[OPTIMIZE_PRED] = 1;
	ddic->optimizations[OPTIMIZE_INDEX_VIRTUAL_FIELDS] = 1;
	ddic->optimizations[OPTIMIZE_INDEX_DATA_ONLY_CHECK_PREDICATES] = 1;
	ddic->optimizations[OPTIMIZE_GROUP_BY_MEM] = 1;
	ddic->optimizations[OPTIMIZE_LIKE_HANDLED] = 1;	/* Bug 6404 */
	/* Bug 6974: */
	ddic->optimizations[OPTIMIZE_SQL_FUNCTION_PARAMETER_CACHE] = 1;
	return(1);				/* success */
}

/******************************************************************/
