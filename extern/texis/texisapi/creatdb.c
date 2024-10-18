/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef EPI_HAVE_IO_H
#  include <io.h>
#endif /* EPI_HAVE_IO_H */
#ifdef _WIN32
#  include <direct.h>
#endif /* _WIN32 */
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"

#ifndef F_OK
#  define F_OK 0
#  define R_OK 4
#  define W_OK 2
#endif

static const char	TXsystemUserName[] = "_SYSTEM";
static const char	TXpublicUserName[] = "PUBLIC";

typedef struct tagUSER {
	char	*name;
	char	*penc;
	char	*plain;
	int	uid;
	int	gid;
} USER;

static USER	users[] = {
{(char *)TXsystemUserName, "", "", TX_SYSTEM_UID, 0},/* must be first */
{(char *)TXpublicUserName, "", "", TX_PUBLIC_UID, 9999}, /* must be second */
{"", "", "", 0, 0},
};


/******************************************************************/

static int createsysmtbl ARGS((DDIC *));

static int
createsysmtbl(ddic)
DDIC	*ddic;
/* Returns 0 on success, -1 on error.
 */
{
	static char Fn[] = "createsysmtbl";
	DD	*dd;
	DBTBL	*dbtbl;
	char	*fname;

	dd = opendd();
	fname = TXmalloc(ddic->pmbuf, Fn, PATH_MAX);
	if (!dd || !fname)
	{
		dd = closedd(dd);
		fname = TXfree(fname);
		return -1;
	}
	(void)ddsettype(dd, TEXIS_FAST_TABLE);
	putdd(dd, "NAME", "char", DDNAMESZ, 0);
	putdd(dd, "WAIT", "long", 1, 0);
	putdd(dd, "THRESH", "long", 1, 0);
	strcpy(fname, TEXISFNSYSMETAI);
	dbtbl = createdbtbl(ddic, dd, fname, TEXISSYSMETAI, "Chkind Parameters", 'S');
	closedbtbl(dbtbl);
	strcpy(fname, ddic->pname);
	strcat(fname, TEXISFNSYSMETAI);
	dd = closedd(dd);
	fname = TXfree(fname);
	return 0;
}

/******************************************************************/

int
TXsetdfltpass(sys, pub, sysplain, pubplain)
char *sys; /* Encrypted _SYSTEM password */
char *pub; /* Encrypted PUBLIC password */
char *sysplain;
char *pubplain;
{
	users[0].penc = sys;
	users[1].penc = pub;
	users[0].plain = sysplain;
	users[1].plain = pubplain;
	return 0;
}

/******************************************************************/

static int adddfltusers ARGS((DDIC *ddic, const char *encSystemPass,
			      const char *encPublicPass));

static int
adddfltusers(ddic, encSystemPass, encPublicPass)
DDIC		*ddic;
const char	*encSystemPass;	/* (in, opt.) encrypted _SYSTEM passowrd */
const char	*encPublicPass;	/* (in, opt.) encrypted PUBLIC passowrd */
{
	TBL	*tbl;
	FLD	*uf, *pf, *df, *gf;
	USER	*current;
	char	*uname, *penc = "";
	int	uid, gid;

	tbl = ddic->userstbl;
	if (!tbl)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, NULL,
			       "SYSUSERS did not create properly");
		return -1;
	}
	uf = nametofld(tbl, "U_NAME");
	pf = nametofld(tbl, "U_PASSWD");
	df = nametofld(tbl, "U_UID");
	gf = nametofld(tbl, "U_GID");
	if (!uf || !pf || !df || !gf)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, NULL,
			       "SYSUSERS is corrupt");
		return -1;
	}

	for (current = users; *current->name != '\0'; current++)
	{
		uname = current->name;
		switch (current - users)
		{
		case 0:
			penc = (encSystemPass ? (char *)encSystemPass : "");
			break;
		case 1:
			penc = (encPublicPass ? (char *)encPublicPass : "");
			break;
		default:
			penc = current->penc;
			break;
		}
		uid = current->uid;
		gid = current->gid;

		putfld(uf, uname, strlen(uname));
		putfld(pf, penc, strlen(penc));
		putfld(df, &uid, 1);
		putfld(gf, &gid, 1);
		puttblrow(tbl, NULL);

		if (*penc == '\0')
			txpmbuf_putmsg(ddic->pmbuf, MWARN, NULL,
		   "User %s has been added to database %s without a password",
				       uname, ddic->epname);
	}
	return 0;
}

/* security through obscurity (but mainly through static declaration): */
#define TXpermsSu	TXcreateDbAux

static int
TXpermsSu(DDIC *ddic, const char *user, UID_T uid, GID_T gid)
/* Like permstexis(ddic, user, ...) but no need for password; like su.
 * Internal use by createdb() only.
 * Returns 0 if ok, -1 on error.
 */
{
	static	const char fn[] = "TXcreateDbAux";
	TXPERMS	*p;

	if (ddic->perms) return(-1);

	p = (TXPERMS *)TXcalloc(ddic->pmbuf, fn, 1, sizeof(TXPERMS));
	if (!p) return(-1);

	p->unixperms = 0;
	p->texuid = -1;
	p->texgid = -1;
	p->texsuid = -1;
	p->texsgid = -1;

	p->state = TX_PM_SUCCESS;
	p->texuid = uid;
	p->texgid = gid;

	TXstrncpy(p->uname, user, sizeof(p->uname));
	ddic->perms = p;
	return(0);
}

/******************************************************************/
/*	Creates an empty database.  Creates the required system
 *	tables in the directory specified by path.  If the last
 *	component of path does not exist it will be created.  The
 *	remaining components must exist.  This is the internal
 *	direct version; createdb() is for public use.
 *	`encSystemPass'/`encPublicPass' are optional; empty if NULL.
 *	Only TXDDOPENFLAG_NO_DB_MONITOR (always on) and
 *	TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY respected.
 *
 *	Returns 0 on failure, 1 on success.
 */

int
TXcreateDb(TXPMBUF *pmbuf, const char *path, const char *encSystemPass,
	   const char *encPublicPass, TXDDOPENFLAG flags)
{
	static const char	Fn[] = "TXcreateDb";
	DD	*dd;
	DDIC	*ddic = DDICPN;
	DBLOCK	*saveLocks = NULL;
	int	serverId = 0;
	DBTBL	*dbtbl;
	TBL	*stb = NULL, *scl = NULL, *six = NULL;
	char	*mergedPath = CHARPN;
	int	res, ret, i;
	char	*files[32];
	int	numFiles = 0, createdFile[32];
	int	createdDir = 0;
	TXDDOPENFLAG	ddopenFlags;
	char	reasonBuf[1024];

	if (flags & ~(TXDDOPENFLAG_NO_DB_MONITOR |
		      TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY))
	{
		txpmbuf_putmsg(pmbuf, MERR + UGE, Fn,
			       "Invalid flags specified");
		goto err;
	}
	/* createdb never starts a monitor (historical behavior): */
	flags |= TXDDOPENFLAG_NO_DB_MONITOR;

	if(TXchecklicense(LIC_CREATE_DB, 0, reasonBuf, sizeof(reasonBuf)) != 0)
	{
		txpmbuf_putmsg(pmbuf, MWARN, Fn,
			 "Cannot create database `%s': License violation: %s",
			       path, reasonBuf);
		goto err;
	}

	ddopenFlags = TXDDOPENFLAG_IGNORE_OPEN_FAILURES;
	if (TXsingleuser) ddopenFlags |= TXDDOPENFLAG_READ_ONLY;
	ddopenFlags |= (flags & (TXDDOPENFLAG_NO_DB_MONITOR |
				 TXDDOPENFLAG_CREATELOCKS_DIRECT_ONLY));

	/* See if the database already exists: */
	ddic = TXddopen(pmbuf, path, ddopenFlags);
	if (ddic)
	{
		txpmbuf_putmsg(pmbuf, MWARN, NULL,
			       "Database `%s' already exists", path);
		ddic = ddclose(ddic);
		goto err;
	}

	if (!TXApp->createDbOkDirExists ||	/* dir should not exist or */
	    access(path, F_OK) != 0)		/* it does not exist */
	{
		errno = 0;
#ifndef _WIN32
		res = mkdir(path, 0777);
#else /* _WIN32 */
		res = mkdir(path);
#endif /* _WIN32 */
		createdDir = (res == 0);
		if (res != 0 &&			/* mkdir() failed */
		    (!TXApp->createDbOkDirExists /* should not have existed */
#ifdef EEXIST
		     || errno != EEXIST		/* ok if it already exists */
#endif /* EEXIST */
		     ))
		{
			const char	*msg;
			EPI_STAT_S	st;

			msg = strerror(errno);
			if (EPI_STAT(path, &st) == 0 &&
			    S_ISDIR(st.st_mode))
				msg = "Directory exists";
			txpmbuf_putmsg(pmbuf, MERR + FME, Fn,
			       "Cannot create directory `%s': %s", path, msg);
			goto err;
		}
	}
#ifndef _WIN32
	chmod(path, 0777);
#endif /* !_WIN32 */

	mergedPath = TXstrcatN(pmbuf, Fn, path, PATH_SEP_S TEXISFNSYSTABLES
			       TX_TABLE_FILE_EXT, NULL);
	if (!mergedPath) goto err;
	if (access(mergedPath, F_OK) == 0)
	{
		txpmbuf_putmsg(pmbuf, MWARN, NULL, "Table %s already exists", mergedPath);
		goto err;
	}
	mergedPath = TXfree(mergedPath);

	mergedPath = TXstrcatN(pmbuf, Fn, path, PATH_SEP_S TEXISFNSYSTABLES,
			       NULL);
	if (!mergedPath) goto err;
	if ((dd = opendd()) == DDPN ||
		(ddsettype(dd, TEXIS_FAST_TABLE) == -1) ||
		!putdd(dd, "NAME", "char", DDNAMESZ, 1) ||
		!putdd(dd, "TYPE", "char", 1, 1) ||
		!putdd(dd, "WHAT", "char", 255, 0) ||
		!putdd(dd, "FC", "byte", 1, 0) ||
		!putdd(dd, "CREATOR", "char", DDNAMESZ, 0) ||
		!putdd(dd, "REMARK", "char", 80, 0) ||
		(stb = createtbl(dd, mergedPath))==TBLPN)
			goto err;
	dd = closedd(dd);
	files[numFiles] = TEXISFNSYSTABLES TX_TABLE_FILE_EXT;
	/* createtbl() creates with O_EXCL, so if it succeeds we know
	 * we really created the table:
	 */
	createdFile[numFiles++] = 1;
	mergedPath = TXfree(mergedPath);
	/* Bug 3896: keep independent `stb' around for later use */

	mergedPath = TXstrcatN(pmbuf, Fn, path, PATH_SEP_S TEXISFNSYSCOLUMNS,
			       NULL);
	if (!mergedPath) goto err;
	if ((dd = opendd()) == DDPN ||
		(ddsettype(dd, TEXIS_FAST_TABLE) == -1) ||
		!putdd(dd, "NAME", "char", DDNAMESZ, 1) ||
		!putdd(dd, "TBNAME", "char", DDNAMESZ, 1) ||
		!putdd(dd, "TYPE", "char", 15, 1) ||
		!putdd(dd, "SIZE", "int", 1, 0) ||
		!putdd(dd, "ORDINAL_POSITION", "int", 1, 0) ||
		!putdd(dd, "IDX", "char", DDNAMESZ, 0) ||
		!putdd(dd, "NULLABLE", "short", 1, 0) ||
		!putdd(dd, "SQLTYPE", "short", 1, 0) ||
		!putdd(dd, "PRECIS", "int", 1, 0) ||
		!putdd(dd, "LENGTH", "int", 1, 0) ||
		!putdd(dd, "SCALE", "short", 1, 0) ||
		!putdd(dd, "RADIX", "short", 1, 0) ||
		!putdd(dd, "REMARK", "char", 80, 0) ||
		(scl = createtbl(dd, mergedPath))==TBLPN)
			goto err;
	dd = closedd(dd);
	files[numFiles] = TEXISFNSYSCOLUMNS TX_TABLE_FILE_EXT;
	createdFile[numFiles++] = 1;
	mergedPath = TXfree(mergedPath);
	/* Bug 3896: keep independent `scl' around for later use */

	mergedPath = TXstrcatN(pmbuf, Fn, path, PATH_SEP_S TEXISFNSYSINDEX,
			       NULL);
	if (!mergedPath) goto err;
	if ((dd = opendd()) == DDPN ||
		(ddsettype(dd, TEXIS_FAST_TABLE) == -1) ||
		!putdd(dd, "NAME", "char", DDNAMESZ, 1) ||
		!putdd(dd, "TBNAME", "char", DDNAMESZ, 1) ||
		!putdd(dd, "FNAME", "varchar", 255, 1) ||
/* JMT  2/11/94 Make long enough for file names */
		!putdd(dd, "COLLSEQ", "char", 1, 1) ||
		!putdd(dd, "TYPE", "char", 1, 0) ||
		!putdd(dd, "NON_UNIQUE", "byte", 1, 0) ||
		!putdd(dd, "FIELDS", "varchar", DDNAMESZ, 0) ||
		!putdd(dd, "PARAMS", "varchar", DDNAMESZ, 0) ||
		(six = createtbl(dd, mergedPath))==TBLPN)
			goto err;
	dd = closedd(dd);
	files[numFiles] = TEXISFNSYSINDEX TX_TABLE_FILE_EXT;
	createdFile[numFiles++] = 1;
	mergedPath = TXfree(mergedPath);
	/* Bug 3896: keep our `ddic'-independent `six' handle around,
	 * instead of closing it here and using `ddic->indextbl' below:
	 * latter may have gotten closed and reopened on us via
	 * TXddgetanytable(reset=1) (though not anymore).
	 */
	ddic = TXddopen(pmbuf, path, ddopenFlags);
	if (!ddic)
		goto err;
	ddic->increate = 1;
	permsunix(ddic);
	addtable(ddic, TEXISSYSCOLUMNS, (char *)TXsystemUserName,
		 "Catalog of Columns", TEXISFNSYSCOLUMNS, tbldd(scl), 0, 'S');
	addtable(ddic, TEXISSYSTABLES , (char *)TXsystemUserName,
		 "Catalog of Tables" , TEXISFNSYSTABLES , tbldd(stb), 0, 'S');
	addtable(ddic, TEXISSYSINDEX  , (char *)TXsystemUserName,
		 "Catalog of Indices", TEXISFNSYSINDEX  , tbldd(six), 0, 'S');
	if (createusertbl(ddic))		/* success */
	{
		files[numFiles] = TEXISFNSYSUSERS TX_TABLE_FILE_EXT;
		createdFile[numFiles++] = 0;	/* unsure if creator */
	} else {
		goto err;
	}
	adddfltusers(ddic, encSystemPass, encPublicPass);
	if (createpermtbl(ddic))		/* success */
	{
		files[numFiles] = TEXISFNSYSPERMS TX_TABLE_FILE_EXT;
		createdFile[numFiles++] = 0;	/* unsure if creator */
	}

	/* When we close `ddic' here, we will likely close the only
	 * copy of its locks, causing us to hit the monitor to create
	 * locks again at the following ddopen() (if Windows).  Try to
	 * avoid that by opening another handle to locks to hold open
	 * across the close/open cycle.  We can open directly since
	 * we know locks already exist via open `ddic':
	 */
#ifndef LOCK_SERVER
	saveLocks = TXdblockOpenDirect(pmbuf, path, &serverId,
				       TXbool_False /* !readOnly */);
#endif
	ddic = ddclose(ddic);
	ddic = TXddopen(pmbuf, path, ddopenFlags);
#ifndef LOCK_SERVER
	saveLocks = closedblock(pmbuf, saveLocks, serverId,
				TXbool_False /* !readOnly */);
#endif
	if (!ddic) goto err;

	/* Bug 4732: replaced permstexis(ddic, "_SYSTEM", users[0].plain)
	 * call with TXpermsSu(), so that we do not need to know the
	 * plaintext password here, which means only the encrypted
	 * (and not plaintext) password need be sent over-the-wire
	 * by TXcreateLocksViaMonitor():
	 *   Above comment pertains to earlier Bug 4732 implementation,
	 * where monitor may be doing the full createdb(); now monitor
	 * does locks at most:
	 */
	if (TXpermsSu(ddic, TXsystemUserName, TX_SYSTEM_UID, 0) != 0)
	{					/* failed */
		ddic = ddclose(ddic);
		goto err;
	}
	if (createtrigtbl(ddic) == 0)		/* success */
	{
		files[numFiles] = TEXISFNSYSTRIG TX_TABLE_FILE_EXT;
		createdFile[numFiles++] = 0;	/* unsure if creator */
	}
	if (createsysmtbl(ddic) == 0)		/* success */
	{
		files[numFiles] = TEXISFNSYSMETAI TX_TABLE_FILE_EXT;
		createdFile[numFiles++] = 0;	/* unsure if creator */
	}
	dbtbl = opendbtbl(ddic, "SYSTABLES");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSCOLUMNS");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSINDEX");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSUSERS");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSPERMS");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSTRIG");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_SELECT);
	dbtbl = closedbtbl(dbtbl);
	dbtbl = opendbtbl(ddic, "SYSMETAINDEX");
	permgrant(ddic, dbtbl, (char *)TXpublicUserName, PM_ALLPERMS);
	dbtbl = closedbtbl(dbtbl);
#ifndef NO_KEEP_STATS
	dbtbl = TXcreatestatstable(ddic);
	if(dbtbl)	/* permgrant handled by TXcreatestatstable */
	{
		dbtbl = closedbtbl(dbtbl);
		files[numFiles] = TEXISFNSYSSTATS TX_TABLE_FILE_EXT;
		createdFile[numFiles++] = 1;
	}
#endif
	ddic = ddclose(ddic);
	ret = 1;				/* success */
	goto done;

err:
	ret = 0;				/* failure */
done:
	saveLocks = closedblock(pmbuf, saveLocks, serverId,
				TXbool_False /* !readOnly */);
	ddic = ddclose(ddic);
	if (stb) stb = closetbl(stb);
	if (scl) scl = closetbl(scl);
	if (six) six = closetbl(six);
	mergedPath = TXfree(mergedPath);
	if (!ret)				/* failure */
	{
		/* Remove partial remnants of database to avoid open
		 * errors for other processes.  But only remove what
		 * we know we created, to avoid making things worse if
		 * in a race with another creator (should not be):
		 */
		for (i = 0; i < numFiles; i++)
		{
			mergedPath = TXstrcatN(pmbuf, Fn, path, PATH_SEP_S,
					       files[i], NULL);
			if (mergedPath && (createdFile[i] || createdDir))
				unlink(mergedPath);
			mergedPath = TXfree(mergedPath);
		}
		if (createdDir) rmdir(path);
	}
	return(ret);
}

int
createdb(const char *path)
/* Public create-database function.
 * Returns 0 on failure, 1 on success.
 */
{
	return(TXcreateDb(TXPMBUFPN, path, users[0].penc, users[1].penc,
			  TXDDOPENFLAG_NO_DB_MONITOR));
}

/* ------------------------------------------------------------------------ */

static void
TXcreateDbUsage(void)
/* Prints usage message for command-line creatdb tool.
 */
{
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
#  define TWO_ARG_ASSIGN_OPT_USAGE_STR	"{=| }"
#else /* !EPI_TWO_ARG_ASSIGN_OPTS */
#  define TWO_ARG_ASSIGN_OPT_USAGE_STR	" "
#endif /* !EPI_TWO_ARG_ASSIGN_OPTS */

	fprintf(stdout,
"Usage: creatdb [options] database\n"
"Options are:\n"
"  --install-dir[-force]" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
		"dir       Alternate Texis install dir\n"
"  --texis-conf" EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
		"file               Alternate texis.ini config file\n"
"  --system-password" TWO_ARG_ASSIGN_OPT_USAGE_STR
		"password      _SYSTEM password (default empty)\n"
"  --public-password" TWO_ARG_ASSIGN_OPT_USAGE_STR
		"password      PUBLIC password (default empty)\n"
"  --createlocks-methods" TWO_ARG_ASSIGN_OPT_USAGE_STR
		"method[,...]  Createlocks methods (direct,monitor)\n"
"  --if-not-exist                  Only create if nonexistent, else exit ok\n"
"  -V                              Increase verbosity (same as tsql -V)\n"
"  -h                              Show this help\n"
		);
}

/* ------------------------------------------------------------------------ */

TXEXIT
TXcreateDbMain(int argcLocal, char *argvLocal[])
/* creatdb command line main().  `...local' variants are argc/argv
 * with global options stripped.  Returns TXEXIT code suitable for exit().
 */
{
	int			i, verboseLevel = 0;
	TXEXIT			ret;
	TXPMBUF			*pmbuf = TXPMBUFPN;
	char			*systemPass = "", *publicPass = "";
	char			*encryptedSystemPass = CHARPN;
	char			*encryptedPublicPass = CHARPN;
	size_t			optLen;
	TXbool			ifNotExist = TXbool_False;
	const char		*dbPath = NULL;
	char			*sysTablesPath = NULL;

#ifdef _WIN32
__try
{
#endif /* _WIN32 */
	tx_setgenericsigs();
	TXsetSigProcessName(pmbuf, "creatdb");

	/* Parse options: - - - - - - - - - - - - - - - - - - - - - - - - - */
	for (i = 1; i < argcLocal - 1; i++)
	{
		optLen = strcspn(argvLocal[i], "=");
		if (strcmp(argvLocal[i], "-V") == 0)
			verboseLevel++;
		else if (strcmp(argvLocal[i], "-h") == 0)
		{
			TXcreateDbUsage();
			return(TXEXIT_OK);
		}
		else if (optLen == 17 &&
			 strncmp(argvLocal[i], "--system-password", 17) == 0)
		{
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
			if (argvLocal[i][optLen] == '=')
			{
				systemPass = argvLocal[i] + optLen + 1;
				continue;
			}
			else if (argvLocal[i][optLen])
				goto usage;
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			if (++i >= argcLocal) goto requiresArg;
			systemPass = argvLocal[i];
		}
		else if (optLen == 17 &&
			 strncmp(argvLocal[i], "--public-password", 17) == 0)
		{
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
			if (argvLocal[i][optLen] == '=')
			{
				publicPass = argvLocal[i] + optLen + 1;
				continue;
			}
			else if (argvLocal[i][optLen])
				goto usage;
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			if (++i >= argcLocal) goto requiresArg;
			publicPass = argvLocal[i];
		}
		else if (optLen == 21 &&
			 strncmp(argvLocal[i], "--createlocks-methods", 21) == 0)
		{
			const char	*methods;

#ifdef EPI_TWO_ARG_ASSIGN_OPTS
			if (argvLocal[i][optLen] == '=')
			{
				methods = argvLocal[i] + optLen + 1;
				goto doMethods;
			}
			else if (argvLocal[i][optLen])
				goto usage;
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			if (++i >= argcLocal)
			{
			requiresArg:
				txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
					 "%s requires argument (-h for help)",
					       argvLocal[i - 1]);
				return(TXEXIT_INCORRECTUSAGE);
			}
			methods = argvLocal[i];
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
		doMethods:
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			if (!TXsetCreateLocksMethods(pmbuf, TXApp,
						     "--createlocks-methods",
						     methods, -1))
				return(TXEXIT_INCORRECTUSAGE);
		}
		else if (optLen == 14 &&
			 strncmp(argvLocal[i], "--if-not-exist", 14) == 0)
		{
			ifNotExist = TXbool_True;
		}
		else
		{
#ifdef EPI_TWO_ARG_ASSIGN_OPTS
		usage:
#endif /* EPI_TWO_ARG_ASSIGN_OPTS */
			txpmbuf_putmsg(pmbuf, MERR + UGE, CHARPN,
				       "Unknown option `%s' (-h for help)",
				       argvLocal[i]);
			return(TXEXIT_INCORRECTUSAGE);
		}
	}
	if (i != argcLocal - 1)
	{
		TXcreateDbUsage();
		return(TXEXIT_INCORRECTUSAGE);
	}
	dbPath = argvLocal[i];

	/* Apply options: - - - - - - - - - - - - - - - - - - - - - - - - - */
	if (verboseLevel) TXsetVerbose(verboseLevel);

	if (*systemPass || *publicPass)
	{
		encryptedSystemPass = TXpwHash(systemPass, CHARPN);
		if (!encryptedSystemPass) return(TXEXIT_UNKNOWNERROR);
		encryptedPublicPass = TXpwHash(publicPass, CHARPN);
		if (!encryptedPublicPass) return(TXEXIT_UNKNOWNERROR);
		TXsetdfltpass(encryptedSystemPass, encryptedPublicPass,
			      systemPass, publicPass);
	}

	if (ifNotExist)
	{
		EPI_STAT_S	st;

		sysTablesPath = TXstrcatN(pmbuf, __FUNCTION__, dbPath,
				PATH_SEP_S TEXISFNSYSTABLES TX_TABLE_FILE_EXT,
					  NULL);
		if (!sysTablesPath)
		{
			ret = TXEXIT_OUTOFMEMORY;
			goto finally;
		}
		if (EPI_STAT(sysTablesPath, &st) == 0)
		{				/* SYSTABLES exists */
			txpmbuf_putmsg(pmbuf, MINFO, NULL,
			   "Database `%s' already exists: Will not create it",
				       dbPath);
			ret = TXEXIT_OK;
			goto finally;
		}
	}

	/* Create db: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
	ret = (createdb(dbPath) ? TXEXIT_OK : TXEXIT_DBOPENFAILED);

finally:
	TXsetdfltpass("", "", "", "");
	encryptedSystemPass = TXfree(encryptedSystemPass);
	encryptedPublicPass = TXfree(encryptedPublicPass);
	sysTablesPath = TXfree(sysTablesPath);
#ifdef _WIN32
    }
  __except(TXgenericExceptionHandler(_exception_code(), _exception_info()))
    {
      /* TXgenericExceptionHandler() exits */
    }
#endif /* _WIN32 */
	return(ret);
}

/******************************************************************/

#ifdef TEST
void
main(int argc, char *argv[])
{
	if (argc > 1)
		createdb(argv[1]);
	else
		createdb(".");
}
#endif /* TEST */
