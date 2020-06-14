/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#ifdef NEED_GETOPT_H
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#endif /* NEED_GETOPT_H */
#if defined(sparc) && !defined(__SVR4)
extern int getopt ARGS((int argc, char **argv, char *opts));
extern char *optarg;
extern int optind, opterr, optopt;
#endif

#ifndef DBTBLPN
#  define DBTBLPN       ((DBTBL *)NULL)
#endif


/******************************************************************/

int TXaddtosyscols = 1;

int
TXaddfields(ddic, tname, dd)
DDIC *ddic;
char *tname;	/* (in) logical table name */
DD *dd;
/* Adds column values for logical table `tname' to SYSCOLUMNS.
 * Returns 0 on success, -1 on error.
 */
{
	int i;
	short s;
	FLD *f1, *f2, *f3, *f4, *f5, *f6;
	FLD *f7, *f8, *f9, *f10, *f11, *f12, *f13;
	RECID *at;
	DBTBL *db;
	char *fname;
	int nfields, rc = 0;
	char	temp[128];

	db = opendbtbl(ddic, "SYSCOLUMNS");

	if ((f1 = dbnametofld(db, "NAME")) == FLDPN ||
	    (f2 = dbnametofld(db, "TBNAME")) == FLDPN ||
	    (f3 = dbnametofld(db, "TYPE")) == FLDPN ||
	    (f4 = dbnametofld(db, "IDX")) == FLDPN ||
	    (f5 = dbnametofld(db, "NULLABLE")) == FLDPN ||
	    (f6 = dbnametofld(db, "SQLTYPE")) == FLDPN ||
	    (f7 = dbnametofld(db, "PRECIS")) == FLDPN ||
	    (f8 = dbnametofld(db, "LENGTH")) == FLDPN ||
	    (f9 = dbnametofld(db, "SCALE")) == FLDPN ||
	    (f10 = dbnametofld(db, "RADIX")) == FLDPN ||
	    (f11 = dbnametofld(db, "REMARK")) == FLDPN)
	{
		closedbtbl(db);
		return -1;
	}

	f12 = dbnametofld(db, "ORDINAL_POSITION");
	f13 = dbnametofld(db, "SIZE");
	putfld(f2, tname, strlen(tname));
	nfields = ddgetnfields(dd);
	for (i = 0; i < nfields; i++)
	{
		ft_int precis, length, ordinal, size;
		ft_short sqltype, scale, radix;
		byte type;

		type = ddgetftype(dd, i);
		fname = ddgetname(dd, i);
		ordinal = ddgetnum(dd, i) + 1;
		size = ddgetsize(dd, i);
		putfld(f1, fname, strlen(fname));
		TXstrncpy(temp, ddfttypename(type), sizeof(temp));
		putfld(f3, temp, strlen(temp));
		putfld(f4, "", 0);
		s = TXftnIsNotNullable(type) ? 0 : 1;
		putfld(f5, &s, 1);
		sqltype = 0;
		precis = 0;
		radix = 10;
		scale = 0;
		length = 0;
		switch (type & DDTYPEBITS)
		{
		case FTN_BYTE:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			if (sqltype == SQL_BINARY && precis == 1)
				sqltype = SQL_TINYINT;
			length = precis;
			break;
		case FTN_CHAR:
			if (isddvar(type))
				sqltype = SQL_LONGVARCHAR;
			else
				sqltype = SQL_CHAR;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_DECIMAL:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_DECIMAL;
			precis = 0;
			length = precis;
			break;
		case FTN_DOUBLE:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_DOUBLE;
			precis = 15;
			length = 8;
			break;
		case FTN_DATE:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_DATE;
			precis = 10;
			length = 4;
			break;
		case FTN_FLOAT:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_FLOAT;
			precis = 15;
			length = 8;
			break;
		case FTN_INT:
		case FTN_INTEGER:
		case FTN_LONG:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_INTEGER;
			precis = 10;
			length = 4;
			break;
		case FTN_SHORT:
		case FTN_SMALLINT:
		case FTN_WORD:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_SMALLINT;
			precis = 5;
			length = 2;
			break;
		case FTN_COUNTER:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = 20;
			length = 8;
			break;
		case FTN_STRLST:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_BLOB:
		case FTN_BLOBZ:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_HANDLE:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_INDIRECT:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_DWORD:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_C_LONG + SQL_UNSIGNED_OFFSET;
			precis = 10;
			length = 4;
			break;
		case FTN_BLOBI:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BINARY;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
		case FTN_INTERNAL:
			sqltype = SQL_INTERNAL;
			precis = ddgetsize(dd, i);
			length = precis;
			break;
#ifdef EPI_INT64_SQL
		/* WTF basically copied from FTN_INT: */
		case FTN_INT64:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_BIGINT;
			precis = 10;
			length = 4;
			break;
		case FTN_UINT64:
			if (isddvar(type))
				sqltype = SQL_LONGVARBINARY;
			else
				sqltype = SQL_UINT64;
			precis = 10;
			length = 4;
			break;
#endif /* EPI_INT64_SQL */
		}
		putfld(f6, &sqltype, 1);
		putfld(f7, &precis, 1);
		putfld(f8, &length, 1);
		putfld(f9, &scale, 1);
		putfld(f10, &radix, 1);
		putfld(f11, "", 0);
		if (f12)
			putfld(f12, &ordinal, 1);
		if (f13)
			putfld(f13, &size, 1);
		at = putdbtblrow(db, NULL);
		if (!TXrecidvalid(at))
			rc = -1;
	}
	closedbtbl(db);
	return rc;
}

/******************************************************************/

int
TXaddtablerec(ddic, lname, creator, remark, tbfname, numFields, type, recidp)
DDIC *ddic;		/* (in) DDIC to add to */
CONST char *lname;	/* (in) logical (SQL) name of table */
char *creator;		/* (in) CREATOR e.g. "PUBLIC" */
CONST char *remark;	/* (in) REMARK */
char *tbfname;		/* (in, opt.) path to table (NULL if RAM table) */
int	numFields;	/* (in) number of fields in table */
int type;		/* (in) SYSTABLES.TYPE (TEXIS_..._TABLE) type */
RECID	*recidp;	/* (out) inserted row recid */
/* Adds an entry to SYSTABLES (if not RAM table).  Does not update SYSCOLUMNS.
 * Returns 1 on success, 0 on error.  Sets `*recidp' to inserted row.
 */
{
	FLD *f1, *f2, *f3, *f4, *f5, *f6;
	RECID *at;
	TBL *dtb;
	byte nfields;
	char tstr[2];

	dtb = ddic->tabletbl;
	TXsetrecid(recidp, RECID_INVALID);

	if ((f1 = nametofld(dtb, "NAME")) == FLDPN ||
	    (f2 = nametofld(dtb, "TYPE")) == FLDPN ||
	    (f3 = nametofld(dtb, "WHAT")) == FLDPN ||
	    (f4 = nametofld(dtb, "FC")) == FLDPN ||
	    (f5 = nametofld(dtb, "CREATOR")) == FLDPN ||
	    (f6 = nametofld(dtb, "REMARK")) == FLDPN)
		return(0);			/* error */

	putfld(f1, (char *)lname, strlen(lname));
	tstr[0] = (char)type;
	tstr[1] = '\0';
	putfld(f2, tstr, 1);
	if (tbfname)
		putfld(f3, tbfname, strlen(tbfname));
	else
		return(1);			/* is RAM table: do not add */
	nfields = (byte)numFields;
	putfld(f4, &nfields, 1);
	putfld(f5, creator, strlen(creator));
	putfld(f6, (char *)remark, strlen(remark));
	if (TXlocksystbl(ddic, SYSTBL_TABLES, W_LCK, NULL) == -1)
		return(0);			/* error */
	at = puttblrow(dtb, NULL);
	TXddicSetSystablesChanged(ddic, 1);	/* just in case no locks */
	TXunlocksystbl(ddic, SYSTBL_TABLES, W_LCK);
	if (!TXrecidvalid(at))
		return(0);			/* error */
	if (ddic->tablendx != (BTREE *) NULL)
		if (btinsert(ddic->tablendx, at, strlen(lname), (char *)lname) < 0L)
		{
			deltblrow(dtb, at);
			return(0);		/* error */
		}
	*recidp = *at;
	return(1);				/* success */
}

int
TXdeltablerec(ddic, recid)
DDIC	*ddic;
RECID	recid;
/* Deletes SYSTABLES `recid', previously inserted by TXaddtablerec().
 * Returns 1 if ok, 0 on error.
 */
{
	static CONST char	fn[] = "TXdeltablerec";
	TBL			*tb;
	int			res;

	tb = ddic->tabletbl;
	if (tb == (TBL *)NULL)
	{
		putmsg(TDBDOING, fn, "No SYSTABLES");
		return(0);			/* error */
	}
	if (TXlocksystbl(ddic, SYSTBL_TABLES, W_LCK, NULL) == -1) return(0);
	res = deltblrow(tb, &recid);
	TXunlocksystbl(ddic, SYSTBL_TABLES, W_LCK);
	if (!res)
	{
		putmsg(MERR, fn, "Could not delete from SYSTABLES");
		return(0);
	}
	return(1);				/* ok */
}

int
addtable(ddic, lname, who, comment, tbfname, dd, btree, type)
DDIC *ddic;
char *lname;
char *who;
char *comment;
char *tbfname;
DD *dd;
int btree;
int type;		/* (in) SYSTABLES.TYPE (TEXIS_..._TABLE) type */
/* Returns -1 on error, 0 on success.
 */
{
	char	*fname = CHARPN;
	char	ttype;
	RECID	where;

	fname = ddgettable(ddic, lname, &ttype, 0);
	if (fname != CHARPN)
	{
		putmsg(MWARN, NULL, "Table %s already exists", lname);
		fname = TXfree(fname);
		return(-1);
	}
#ifndef HAVE_VIEWS
	if (strcmp(who, "texis"))
		if (btree)
			type = TEXIS_BTREEINDEX_TABLE;
		else
			type = TEXIS_TABLE;
	else
		type = TEXIS_SYS_TABLE;
#endif /* !HAVE_VIEWS */
	if (!TXaddtablerec(ddic, lname, who, comment, tbfname,
			(int)ddgetnfields(dd), type, &where))
		return(-1);			/* error */ 
	if (TXaddtosyscols == 0)
		return 0;			/* success */
	if (!tbfname) return(0);		/* RAM table: do not add */
	return TXaddfields(ddic, lname, dd);
}

/******************************************************************/

static void usage ARGS((char *, int));

static void
usage(Prog, full)
char *Prog;
int full;
{
	fprintf(stderr,
		"Usage: %s [-d database] [-l tablename] [-c comment] [-u user] [-p password] [-b bits] filename\n",
		Prog);
	if (full)
	{
		int	gap, len;

		fprintf(stderr, "\t--install-dir[-force]"
			EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
			"dir\tAlternate installation dir\n");
		len = strlen(TXINSTALLPATH_VAL);
		gap = 61 - len;
		if (gap < 0) gap = 0;
		else if (gap > 20) gap = 20;
		fprintf(stderr, "    %*s(default is `%s')\n", gap, "",
			TXINSTALLPATH_VAL);
		fprintf(stderr, "\t--texis-conf"
			EPI_TWO_ARG_ASSIGN_OPT_USAGE_STR
			"file\t\tAlternate " TX_TEXIS_INI_NAME " file\n");
		fprintf(stderr, "\t-h\t\tThis help summary.\n");
		fprintf(stderr, "\t-d database\tDatabase to add table to.\n");
		fprintf(stderr,
			"\t-l tablename\tName of table within Texis.\n");
		fprintf(stderr,
			"\t-c comment\tComment to put in SYSTABLES.\n");
		fprintf(stderr, "\t-u user\t\tUsername.\n");
		fprintf(stderr, "\t-p password\tPassword.\n");
		fprintf(stderr,
			"\t-b bits\tFile size bits file created with (e.g. 32).\n");
		fprintf(stderr, "\t<filename>\tFile to add.\n");
	}
	else
		fprintf(stderr, "-h for more options\n");
	exit(TXEXIT_INCORRECTUSAGE);
}

int
TXaddtable(db, file, tbname, comment, user, pass, nbits)
char *db, *file, *tbname, *comment, *user, *pass;
int	nbits;	/* EPI_OFF_T_BITS of source table */
/* Adds table `file' to database `db'.  All but `db', `file' are optional.
 * Returns -2 on perms error, -1 on error, 0 if ok.  More public version
 * than addtable().
 */
{
	static CONST char fn[] = "TXaddtable";
	DBTBL *dbtbl = DBTBLPN;
	TBL *tbl;
	DDIC *dd;
	DD *dd2;
	BTREE *bt;
	int rc = 0, n;
	char *type = CHARPN, *fname, ttype;
	char *what = CHARPN, *ext, *fullwhat = CHARPN;
	char *x;
	char	typeStr[2];
	TXPMBUF	*pmbuf = TXPMBUFPN;

	if (user == CHARPN || *user == '\0')
		user = "PUBLIC";
	if (pass == CHARPN)
		pass = "";
	if (comment == CHARPN)
		comment = "";

	if ((what = TXstrdup(pmbuf, fn, file)) == CHARPN) goto err;
	ext = strrchr(what, '.');
	if (!ext)
	{
		putmsg(MERR + FOE, fn, "Filename must have an extension");
		goto err;
	}
	if (!strcmpi(ext, TX_BTREE_SUFFIX))
		typeStr[0] = TEXIS_BTREEINDEX_TABLE;
	else if (!strcmpi(ext, TX_TABLE_FILE_EXT))
		typeStr[0] = TEXIS_TABLE;
	else
	{
		putmsg(MERR + FOE, NULL, "Invalid filename extension `%s'",
		       ext + 1);
		goto err;
	}
	typeStr[1] = '\0';
	type = typeStr;
	*ext = '\0';				/* remove `what' extension */
	if (tbname == CHARPN || *tbname == '\0')
	{					/* no table name given */
		x = TXstrrcspn(what, TX_PATH_SEP_CHARS_S);
		if (x != CHARPN)
			tbname = x;
		else
			tbname = what;
	}
	dd = ddopen(db);
	if (!dd)
	{
		putmsg(MERR + FOE, fn, "Could not open database %s", db);
		goto err;
	}

	/* Make path correct, and relative to db if possible: */
	if ((fullwhat = fullpath(CHARPN, what, 0)) == CHARPN) goto err;
	n = strlen(dd->pname);				/* wtf drill */
	if (n > 0 && TX_ISPATHSEP(dd->pname[n-1]) &&
	    TXpathcmp(fullwhat, n, dd->pname, n) == 0)	/* in db or subdir */
		memmove(fullwhat, fullwhat + n, strlen(fullwhat + n) + 1);

	/* Check for table existence before spending time converting it: */
	fname = ddgettable(dd, tbname, &ttype, 0);
	if (fname)
	{
		putmsg(MWARN, NULL, "Table %s already exists", tbname);
		fname = TXfree(fname);
		dd = ddclose(dd);
		goto err;
	}
	if (nbits != 0 && TXconverttbl(file, nbits) != 0)
		goto err;

	rc = permstexis(dd, user, pass);
	if (rc == -1)
		goto err;
	if (!(dbtbl = (DBTBL *) TXcalloc(pmbuf, fn, 1, sizeof(DBTBL))))
		goto err;
	dbtbl->rname = tbname;
	if (type[0] == TEXIS_BTREEINDEX_TABLE)
	{
		bt = openbtree(what, BT_MAXPGSZ, 20, 0, O_RDONLY);
		if (bt == BTREEPN)
			rc = -1;
		else
		{
			if ((dd2 = btreegetdd(bt)) == (DD *)NULL)
			{
				putmsg(MERR,fn, "Cannot get DD from B-tree %s",
				       what);
				rc = -1;
			}
			else
				rc = addtable(dd, tbname, user, comment,
					      fullwhat, dd2, 1,
					      TEXIS_BTREEINDEX_TABLE);
		}
		permgrantdef(dd, dbtbl);
		bt = closebtree(bt);
	}
	else
	{
		DD	*fileDd = NULL;

		tbl = opentbl(dd->pmbuf, what);
		if (!tbl)
		{
			putmsg(MERR + FOE, fn, "Could not open table %s",
			       what);
			dd = ddclose(dd);
			goto err;
		}
		/* Bug 4390: convert `tbl->dd' [var]blobi back to blob: */
		fileDd = TXbiddc(tbldd(tbl));
		if (!fileDd)
			rc = -1;
		else
			rc =
				addtable(dd, tbname, user, comment, fullwhat,
					 fileDd, 0, type[0]);
		fileDd = closedd(fileDd);
		permgrantdef(dd, dbtbl);
		tbl = closetbl(tbl);
	}
	dd = ddclose(dd);
	goto done;

      err:
	rc = -1;
      done:
	what = TXfree(what);
	fullwhat = TXfree(fullwhat);
	dbtbl = TXfree(dbtbl);
	return (rc);
}

/******************************************************************/
/*	Main for a program to add a TBL structure to the database
 */

int
ATmain(argc, argv, argcstrip, argvstrip)
int argc, argcstrip;
char *argv[], **argvstrip;
{
	char *dbname = ".";
	char *lname = CHARPN;
	char *comment = CHARPN;
	char *user = CHARPN;
	char *pass = CHARPN;
	int c;
	int nbits = 0;

#ifdef _MSC_VER
	__try
	{
#endif /* _MSC_VER */
		tx_setgenericsigs();

		argc = argcstrip;
		argv = argvstrip;

		TXsetSigProcessName(TXPMBUFPN, "addtable");

		while ((c = getopt(argc, argv, "b:c:d:hl:p:u:")) != -1)
		{
			switch (c)
			{
			case 'b':
				nbits = atoi(optarg);
				break;
			case 'c':
				comment = optarg;
				break;
			case 'd':
				dbname = optarg;
				break;
			case 'l':
				lname = optarg;
				break;
			case 'h':
				usage(argv[0], 1);
				break;
			case 'p':
				pass = optarg;
				break;
			case 'u':
				user = optarg;
				break;
			}
		}
		if (optind < argc)
		{
			switch (TXaddtable
			     (dbname, argv[optind], lname, comment, user,
			      pass, nbits))
			{
			case -2:	exit(TXEXIT_PERMISSIONDENIED);
			case -1:	exit(TXEXIT_UNKNOWNERROR);
			case 0:		exit(TXEXIT_OK);
			default:	exit(TXEXIT_UNKNOWNERROR);
			}
		}
		else
		{
			putmsg(MERR + FOE, NULL, "No filename specified");
			usage(argv[0], 0);
			exit(TXEXIT_INCORRECTUSAGE);
		}
#ifdef _MSC_VER
	}
	__except(TXgenericExceptionHandler(_exception_code(),
					   _exception_info()))
	{
		/* TXgenericExceptionHandler() exits */
	}
#endif /* _MSC_VER */
	exit(TXEXIT_OK);
	return (TXEXIT_OK);
}
