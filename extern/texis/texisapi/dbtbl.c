/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <limits.h>
#ifdef sparc
#  include <values.h>
#else
#  include <float.h>		/*<values.h> */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#undef EPI_OS_CHAR_BITS
#undef EPI_OS_SHORT_BITS
#undef EPI_OS_INT_BITS
#undef EPI_OS_LONG_BITS
#undef EPI_OS_FLOAT_BITS
#undef EPI_OS_DOUBLE_BITS
#if !defined(MSDOS)
#  include <unistd.h>
#  include <pwd.h>
#else
#  include <io.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef USE_FLOCK
#  include <sys/file.h>
#endif
#include <errno.h>
#include <ctype.h>
#ifdef _WIN32
#  include <winsock.h>
#else /* !_WIN32 */
#  include <netinet/in.h>
#endif /* !_WIN32 */
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"
#include "fldops.h"
#include "fdbi.h"
#include "bitfuncs.h"
#include "inetfuncs.h"
#include "txlic.h"
#include "http.h"
#ifdef LIBXML2API
#include "xmlapi.h"
#endif
#ifdef LIBXSLTAPI
#include "xsltapi.h"
#endif

#ifdef _WIN32			/* wtf fix os.h? */
#  if (_MSC_VER < 800)
#    undef stat
#  endif
#endif
#ifndef DBL_MAX
#  define DBL_MAX MAXDOUBLE
#endif
#ifndef DBL_MIN
#  define DBL_MIN MINDOUBLE
#endif

#ifndef F_OK
#  define F_OK 0
#  define R_OK 4
#  define W_OK 2
#endif

const char	TXrankColumnName[TX_RANK_COLUMN_NAME_LEN + 1] = "$rank";
const char	TXrankColumnTypeStr[] = "int";
const char	TXrecidColumnName[TX_RECID_COLUMN_NAME_LEN + 1] = "$recid";
const char	TXrecidColumnTypeStr[] = "recid";

static byte tempbuf[BT_REALMAXPGSZ + TX_ALIGN_BYTES];
static void closestats ARGS((DBTBL *));

static CONST char	OutOfMem[] = "Out of memory: %s";

/******************************************************************/

static char *indexastable ARGS((DDIC *, DBTBL *, char *, char *));

static char *
indexastable(ddic, db, name, otype)
DDIC *ddic;
DBTBL *db;
char *name, *otype;
{
	char *rc = NULL;
	char *type = NULL, **fname = NULL, **tables = NULL, **fields = NULL;
	int ni, i;
	char	**sysindexParamsVals = CHARPPN;

	if (!TXallowidxastbl)
		return rc;
	ni = ddgetindexbyname(ddic, name, &type, NULL, &fname, &tables,
			      &fields, &sysindexParamsVals);
	for (i = 0; i < ni; i++)
	{
		switch (type[i])
		{
		case INDEX_BTREE:
		case INDEX_3DB:
		case INDEX_MM:
		case INDEX_FULL:
			*otype = type[i];
			rc = strdup(fname[i]);
			if (db->indexAsTableSysindexParams != CHARPN)
				free(db->indexAsTableSysindexParams);
			db->indexAsTableSysindexParams = sysindexParamsVals[i];
			sysindexParamsVals[i] = CHARPN;
			if (db->rname)
				free(db->rname);
			db->rname = tables[i];
			tables[i] = CHARPN;	/* `db->rname' owns it */
			break;
		}
	}
	/* WTF:  Clean up memory */
	fname = TXfreeStrList(fname, ni);
	tables = TXfreeStrList(tables, ni);
	fields = TXfreeStrList(fields, ni);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, ni);
	if (type)
		free(type);
	if (fname)
		free(fname);
	if (tables)
		free(tables);
	if (fields)
		free(fields);
	return rc;
}

DBTBL *
TXnewDbtbl(TXPMBUF *pmbuf)
/* Create a new DBTBL object.  Should be the only function that creates
 * this struct, for proper init.
 */
{
	static const char	fn[] = "TXnewDbtbl";
	DBTBL			*dbtbl;

	dbtbl = (DBTBL *)TXcalloc(pmbuf, fn, 1, sizeof(DBTBL));
	if (!dbtbl) return(DBTBLPN);
	TXsetrecid(&dbtbl->recid, -1);
	/* rest cleared by calloc? */
	return(dbtbl);
}

/********************************************************************/
/*	Open a DBTBL.   */

DBTBL *
opendbtbl(ddic, name)
DDIC *ddic;			/* Data dictionary to use */
char *name;			/* Name of the table to open */
{
	static CONST char Fn[] = "opendbtbl";
	DBTBL *db, *saveBtreeLogDbTbl;
	char type;
	char *fname;

	if ((db = TXgetcache(ddic, name)) != NULL)
	{
		if (db->rankindex.btree != (BTREE *) NULL)
			db->rankindex.btree = closebtree(db->rankindex.btree);
		if (db->index.btree != (BTREE *) NULL)
		{
#ifndef NO_BUBBLE_INDEX
			closedbidx(&db->index);
#else
			if (db->index.btree->usr)
				db->index.btree->usr =
					closefldcmp(db->index.btree->usr);
			db->index.btree = closebtree(db->index.btree);
#endif
		}
		if (db->order != (PROJ *) NULL)
			db->order = closeproj(db->order);
		if (db->dbi != (A3DBI *) NULL)
			close3dbi(db->dbi);
		db->pred = NULL;
		db->ipred = NULL;
		db->indguar = 0;
		if (db->lname && db->rname && strcmp(db->lname, db->rname))
		{
			free(db->lname);
			db->lname = strdup(db->rname);
		}
		TXrewinddbtbl(db);
		if (!isramdbtbl(db))
			db->tblid = addltable(ddic->pmbuf, ddic->dblock, name);
		if (db->tblid == -1)
		{
			putmsg(MERR, Fn, "Could not create locks for %s",
			       name);
			return closedbtbl(db);
		}
		if (dbgetperms(db, ddic) == -1)
		{
			putmsg(MERR, Fn,
			       "Insufficient permissions to open %s", name);
			return closedbtbl(db);
		}
		db->needstats = 0;
		/* license schema was checked before adding to cache */
		return db;
	}
#ifdef HAVE_LINK_TABLES
	if (strchr(name, '@'))
	{
		return TXopenlinktbl(ddic, name);
	}
#endif
	db = TXnewDbtbl(ddic->pmbuf);
	if (!db) return(db);
	db->lname = strdup(name);
	db->rname = strdup(name);
	db->needstats = 0;
	if (db->lname == (char *) NULL || db->rname == (char *) NULL)
	{
		putmsg(MWARN + MAE, Fn, "Out of memory");
		return closedbtbl(db);
	}
	fname = ddgettable(ddic, name, &type, 1);
	if (fname == (char *) NULL)
	{
		fname = indexastable(ddic, db, name, &type);
		if (!fname)
		{
			putmsg(MWARN + TNF, Fn,
		       "Table %s not found in data dictionary of database %s",
			       name, ddic->epname);
			free(db->lname);
			free(db->rname);
			free(db);
			return (DBTBL *) NULL;
		}
	}
	switch (type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
		db->tbl = opentbl(ddic->pmbuf, fname);
		break;
	case INDEX_BTREE:
	case 'b':		/* ? KNG 971111 */
		saveBtreeLogDbTbl = TXbtreelog_dbtbl;
		TXbtreelog_dbtbl = db;		/* for btreelog debug */
		db->index.btree = openbtree(fname, BT_MAXPGSZ, 20, 0, O_RDWR);
		/* KNG 20080402 set params: */
		if (db->index.btree &&
		    db->indexAsTableSysindexParams &&
		    bttexttoparam(db->index.btree,
				  db->indexAsTableSysindexParams) < 0)
			db->index.btree = closebtree(db->index.btree);
#ifndef NO_BUBBLE_INDEX
		db->index.type = DBIDX_NATIVE;
#endif
		if (db->index.btree)
		{
			db->tbl =
				createtbl(btreegetdd(db->index.btree), NULL);
			TXsetfldcmp(db->index.btree);
		}
		else
			db->tbl = NULL;
		TXbtreelog_dbtbl = saveBtreeLogDbTbl; /* for btreelog debug */
		break;
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		{
			DD *dd;

			dd = opennewdd(5);
			putdd(dd, "Word", "varchar", 20, 0);
			putdd(dd, "Count", "int64", 1, 0); /* deprecated v6 */
			putdd(dd, "RowCount", "int64", 1, 0);
			if (type == INDEX_FULL)
				putdd(dd, "OccurrenceCount", "int64", 1, 0);
			if (type == INDEX_MM || type == INDEX_FULL)
			{
				db->fi = openfdbi(fname, PM_SELECT,
						  (type ==
						   INDEX_FULL) ? FDF_FULL :
						  0,
	(db->indexAsTableSysindexParams ? db->indexAsTableSysindexParams : ""),
						  db);
				if (db->fi != FDBIPN)
					db->dbi = fdbi_getdbi(db->fi);
			}
			else
				db->dbi = open3dbi(fname, PM_ALLPERMS, type,
      (db->indexAsTableSysindexParams ? db->indexAsTableSysindexParams : ""));
			if (db->dbi != A3DBIPN)
				db->tbl = createtbl(dd, NULL);
			dd = closedd(dd);
			free(db->rname);
			db->rname = NULL;
			break;
		}
	case TEXIS_VIEW:
		{
#ifdef HAVE_VIEWS
			STRBUF *cmd = openstrbuf();
			QNODE *q;

			sqlconvert(fname, NULL, ddic, cmd, MAXINSZ);
			setparsestring(cmd->data);
			q = readnode(ddic, 0);
			q = TXreorgqnode(q);
			db->qnode = q;
			db->type = type;
			db->ddic = ddic;
			dbresetstats(db);
			if (dbgetperms(db, ddic) == -1)
			{
				putmsg(MERR, Fn,
				       "Insufficient permissions to open %s",
				       name);
				return closedbtbl(db);
			}
			return db;
#endif
			break;
		}
	default:
		putmsg(MERR + UGE, Fn,
			"Table %s is unknown/unsupported type `%c'",
			name, type);
	}
	if (db->tbl == (TBL *) NULL)
	{
		putmsg(MERR + FOE, Fn, "Unable to open table %s", fname);
		free(fname);
		return closedbtbl(db);
	}
	if (!isramdbtbl(db))
		db->tblid = addltable(ddic->pmbuf, ddic->dblock, name);
	free(fname);
	db->type = type;
	db->ddic = ddic;
#ifdef STEAL_LOCKS
	db->lockid = -1;
#endif
	db->frecid = createfld("recid", 1, 0);
	if (dbgetperms(db, ddic) == -1)
	{
		putmsg(MERR, Fn, "Insufficient permissions to open %s", name);
		return closedbtbl(db);
	}
	opentrigger(ddic, db);
	putfld(db->frecid, &db->recid, 1);
	dbresetstats(db);
	switch (type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
#if TX_LICVER5 > 0
		if (!TXlic_okdd(1, name, db->tbl->dd, PM_OPEN))
			return closedbtbl(db);
#endif /* TX_LICVER5 > 0 */
		TXputcache(ddic, db);
		break;
	default:
		break;
	}
	return db;
}

/******************************************************************/

DBTBL *
closedbtbl(db)
DBTBL *db;
{
	if (db && db->ddic)
	{
#ifndef NEVER
#ifdef NEVER
		closeindexes(db);
#endif
		while (db->nireadl)
			TXunlockindex(db, INDEX_READ, NULL);
		while (db->niwrite)
			TXunlockindex(db, INDEX_WRITE, NULL);
#endif
		while (db->rlock > 0)
			TXunlocktable(db, R_LCK);
		while (db->wlock > 0)
			TXunlocktable(db, W_LCK);
#ifdef STEAL_LOCKS
		TXclearlocks(db->ddic->dblock, db->lockid);
		db->lockid = -1;
#endif
#ifdef NEVER			/* TX_DEBUG */
/*		db->pred = TXclosepredvalid2(db->pred); */
#endif
		db->perms = TXfree(db->perms);
		if (db->rname && strcmp(db->rname, "SYSDUMMY"))
		{
			delltable(db->ddic->pmbuf, db->ddic->dblock,
				  db->rname, db->tblid);
			db->tblid = 0;
			TXungetcache(db->ddic, db);
			return NULL;
		}
		if (db->projfldcache)
			free(db->projfldcache);
		db->projfldcache = NULL;
		db->cachedproj = NULL;
		if(db->tbl)
			TXclosetblvirtualfields(db->tbl);
		if (!db->rname)
			TXungetcache(db->ddic, db);
		return NULL;
	}
	else
	{
		return _closedbtbl(db);
	}
}

/********************************************************************/
/*	This closes a DBTBL structure. The table is closed, and
 *	all the memory associated with it is freed.
 *
 *	Returns NULL.
 */

DBTBL *
_closedbtbl(db)
DBTBL *db;			/* The table to close */
{
	if (db != (DBTBL *) NULL)
	{
		if (db->rname && db->tblid && db->ddic && db->ddic->dblock)
		{
			delltable(db->ddic->pmbuf, db->ddic->dblock,
				  db->rname, db->tblid);
			db->tblid = 0;
		}
		if (db->tbl != (TBL *) NULL)
			db->tbl = closetbl(db->tbl);
		if (db->trigger != (TRIGGER *) NULL)
			closetrigger(db->trigger);
		if (db->rankindex.btree != (BTREE *) NULL)
			db->rankindex.btree = closebtree(db->rankindex.btree);
		if (db->index.btree != (BTREE *) NULL)
		{
#ifndef NO_BUBBLE_INDEX
			closedbidx(&db->index);
#else
			if (db->index.btree->usr)
				db->index.btree->usr =
					closefldcmp(db->index.btree->usr);
			db->index.btree = closebtree(db->index.btree);
#endif
		}
		if (db->order != (PROJ *) NULL)
			db->order = closeproj(db->order);
		if (db->dbi != (A3DBI *) NULL && db->fi == FDBIPN)
			close3dbi(db->dbi);
		closefdbi(db->fi);	/* INDEX_FULL */
#ifdef STEAL_LOCKS
		TXclearlocks(db->ddic->dblock, db->lockid);
		db->lockid = -1;
#endif
		closeindexes(db);
		closestats(db);
		TXbtfreecache(db);
#ifdef CACHE_IINODE
		if (db->cacheinode)
		{
			db->cacheinode->gpred =
				closepred(db->cacheinode->gpred);
			db->cacheinode = TXcloseiinode(db->cacheinode);
		}
#endif
		if (db->frecid != (FLD *) NULL)
			closefld(db->frecid);
		if (db->perms)
			free(db->perms);
		if (db->indexfields)
			db->indexfields = _freelst(db->indexfields);
		if (db->projfldcache)
			free(db->projfldcache);
		db->projfldcache = NULL;
		db->cachedproj = NULL;
		/* Free these last, after potential tx_loglockop() usage: */
		if (db->lname != (char *) NULL)
		{
			free(db->lname);
			db->lname = CHARPN;
		}
		if (db->rname != (char *) NULL)
		{
			free(db->rname);
			db->rname = CHARPN;
		}
		if (db->indexAsTableSysindexParams != CHARPN)
		{
			free(db->indexAsTableSysindexParams);
			db->indexAsTableSysindexParams = CHARPN;
		}
		free(db);
	}
	return (DBTBL *) NULL;
}

/********************************************************************/
/*	Create a DBTBL, and insert it into the data dictionary.
 *	The type of the table can be:
 *	'T' (TEXIS_TABLE) for a regular table
 *	'B' for a unique BTREE table
 *	'b' for a TEXIS_BTREE_TABLE
 *	'S' (TEXIS_SYS_TABLE) for a system table
 *	'V' (TEXIS_VIEW) for a view
 *	BTREE tables provide fast access from the primary key,
 *	although no further indices are allowed.
 */

DBTBL *
createdbtbl(ddic, dd, tn, lname, comment, type)
DDIC *ddic;			/* data dictionary to use */
DD *dd;				/* data definition for table */
char *tn;			/* file name for the table */
char *lname;			/* Logical table name */
char *comment;			/* A description of the table */
int type;			/* Table type */
{
	static CONST char Fn[] = "createdbtbl";
	DBTBL *t;
	char *fname = (char *) NULL;
	char *filename;
	char ct;
	char *uname;
	int	triedCleanup = 0;

again:
	ct = type;
	if (tn)
	{
		if (ISFNBEG(tn) || tn[0] == '~')
			strcpy((char *) tempbuf, "");
		else
			strcpy((char *) tempbuf, ddic->pname);
		strcat((char *) tempbuf, tn);
#if TX_LICVER5 > 0
		if (!TXlic_okdd(1, lname, dd, PM_CREATE))
			return (DBTBL *)NULL;
#endif /* TX_LICVER5 > 0 */
		filename = (char *)tempbuf;
	}
	else
		filename = NULL;
	/* Bug 4390: do not allow `CREATE TABLE dest AS SELECT * FROM src'
	 * to create a table with [var]blobi columns indirectly because
	 * `src' has blobs; should have been corrected elsewhere:
	 */
	if (!TXddOkForTable(ddic->pmbuf, dd)) return(DBTBLPN);
	if (lname != (char *) NULL)
	{
		ct = '\1';			/* any type */
		/* KNG 20110506 check for *any* type of table named `lname';
		 * DROPped table(s) of same name should also prevent create:
		 */
		fname = TXddgetanytable(ddic, lname, &ct, 1);
		if (fname == CHARPN) ct = type;
	}
	if (fname != (char *) NULL)
	{
		if (!TXtimedout(ddic))
		{
			char	*suffix;

			if (!triedCleanup)
				switch (ct)
				{
				case TEXIS_TEMP_TABLE:
				case TEXIS_DEL_TABLE:
					/* Try to finish a previous DROP: */
					TXdocleanup(ddic);
					triedCleanup = 1;
					fname = TXfree(fname);
					goto again;
				}

			switch (ct)
			{
			case TEXIS_TEMP_TABLE:
				suffix = " (as temp table)";
				break;
			case TEXIS_DEL_TABLE:
				suffix = " (as deleted table)";
				break;
			default:
				suffix = "";
				break;
			}
			putmsg(MWARN + TAE, Fn,
			       "Table %s already exists in data dictionary%s",
			       lname, suffix);
		}
		fname = TXfree(fname);
		return (DBTBL *) NULL;
	}
	t = TXnewDbtbl(ddic->pmbuf);
	if (t == (DBTBL *) NULL) return(t);
	if (ct == 'S')
		uname = "_SYSTEM";
	else
		uname = TXgetusername(ddic);
	switch (type)
	{
	case 'B':
	case 'b':
		{
			if (type == 'B')
				t->index.btree =
					openbtree(filename, BT_MAXPGSZ, 20,
						  BT_UNIQUE,
						  O_RDWR | O_CREAT | O_EXCL);
			else
				t->index.btree =
					openbtree(filename, BT_MAXPGSZ, 20, 0,
						  O_RDWR | O_CREAT | O_EXCL);
			/* no bttexttoparam() possible: no SYSTABLES.PARAMS */
#ifndef NO_BUBBLE_INDEX
			t->index.type = DBIDX_NATIVE;
#endif
			btreesetdd(t->index.btree, dd);
			t->tbl = createtbl(dd, NULL);
			if (t->tbl != NULL && lname != (char *) NULL)
				addtable(ddic, lname, uname, comment, tn, dd,
					 1, 'B');
			break;
		}
	case TEXIS_VIEW:
		if (lname != (char *) NULL)
			addtable(ddic, lname, uname, comment, tn, dd, 0,
				 TEXIS_VIEW);
		break;
	default:
		{
			t->tbl = createtbl(dd, filename);
			if (t->tbl != NULL && lname != (char *) NULL)
				addtable(ddic, lname, uname, comment, tn, dd,
					 0, type);
			break;
		}
	}
	if (t->tbl == (TBL *) NULL)
	{
		if (type != TEXIS_VIEW)
			putmsg(MERR + FOE, Fn, "Unable to open table %s",
			       lname);
		return closedbtbl(t);
	}
	if (lname)
	{
		t->lname = strdup(lname);
		t->rname = strdup(lname);
		if (t->lname == (char *) NULL || t->rname == (char *) NULL)
		{
			putmsg(MWARN + MAE, Fn, "Out of memory");
			return closedbtbl(t);
		}
		if (!isramdbtbl(t))
			t->tblid = addltable(ddic->pmbuf, ddic->dblock, lname);
	}
	t->type = type;
	t->ddic = ddic;
	t->frecid = createfld("recid", 1, 0);
	putfld(t->frecid, &t->recid, 1);
	if (!isramdbtbl(t) || type == 'B' || type == 'b')
		permgrantdef(ddic, t);
	dbresetstats(t);
	/* KNG 20110810 do not cache SYSDUMMY: TX[un]getcache() tries not to,
	 * but TXgetcache()'s createtbl(SYSDUMMY) call would otherwise
	 * unavoidably cache here, and be left orphaned (memleak):
	 */
	if (!tn && strcmp(lname, "SYSDUMMY") != 0)
		TXputcache(ddic, t);
	if (!ddic->dblock)
	{
		if (ddic->tbltblcache)
			ddic->tbltblcache->tbl =
				closetbl(ddic->tbltblcache->tbl);
		if (ddic->prmtblcache)
			ddic->prmtblcache->tbl =
				closetbl(ddic->prmtblcache->tbl);
	}
	return t;
}

DBTBL *
TXcreateinternaldbtblcopy(DBTBL *source, TX_DBF_TYPE dbftype)
{
	DBTBL *rc;
	DD *dd;

	if(!source) return source;
	rc = (DBTBL *)calloc(1, sizeof(DBTBL));
	dd = TXbddc(tbldd(source->tbl));
	rc->tbl = TXcreateinternaltbl(dd, dbftype);
	if(!rc->tbl) {
		return closedbtbl(rc);
	}
	closedd(dd);
	rc->lname = strdup(source->lname);
	rc->rname = NULL;
	rc->type = 'T';
	rc->ddic = source->ddic;
	rc->frecid = createfld("long", 1, 0);
	rc->tblid = -1;
	putfld(rc->frecid, &rc->recid, 1);
	return rc;
}
/********************************************************************/

int
ioctldbtbl(dbtbl, ioctl, data)
DBTBL *dbtbl;
int ioctl;
void *data;
{
	/*
	   static CONST char        Fn[] = "ioctldbtbl";
	 */

	switch (dbtbl->type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
		return ioctldbf(dbtbl->tbl->df, ioctl, data);
	case INDEX_BTREE:
		if ((ioctl & TYPE_IOCTL_MASK) == BTREE_IOCTL)
			return (ioctlbtree(dbtbl->index.btree, ioctl, data));
		else
			return (ioctldbf
				(dbtbl->index.btree->dbf, ioctl, data));
/*
		default:
			putmsg(999, Fn, "Unknown type %c", dbtbl->type);
*/
	}
	return (-1);		/* error */
}

/******************************************************************/


int
TXtblstillthere(DBTBL * dbtbl)
{
	int fd;
	EPI_STAT_S statb;

	if (!dbtbl->tbl)
		return 0;
	if (!dbtbl->tbl->df)
		return 0;
	fd = getdbffh(dbtbl->tbl->df);
	if (fd == -1)
	{
		if (dbtbl->tbl->df->dbftype == DBF_RAM)
			return 1;
		else
			return 0;
	}
	if (EPI_FSTAT(fd, &statb) == -1)
		return 0;
	if (statb.st_nlink == 0)
		return 0;
	return 1;
}

/******************************************************************/

static void
closestats(db)
DBTBL *db;
{
#ifdef OLD_STATS
	int i;
#endif

#ifdef NEVER
	putmsg(999, NULL, "In closestats");
#endif
#ifndef OLD_STATS
	if (!db->nfldstatisdup)
		TXclosenewstats(&db->nfldstat);
#else
	if (db != (DBTBL *) NULL)
		for (i = 0; i < DDFIELDS; i++)
		{
			if (db->fldstats[i].count != NULL)
				db->fldstats[i].count =
					closefld(db->fldstats[i].count);
			if (db->fldstats[i].sum != NULL)
				db->fldstats[i].sum =
					closefld(db->fldstats[i].sum);
			if (db->fldstats[i].max != NULL)
				db->fldstats[i].max =
					closefld(db->fldstats[i].max);
			if (db->fldstats[i].min != NULL)
				db->fldstats[i].min =
					closefld(db->fldstats[i].min);
		}
#endif
}

/******************************************************************/

void
dbresetstats(db)
DBTBL *db;
{
	if (db != (DBTBL *) NULL)
#ifndef OLD_STATS		/* WTF */
		TXresetnewstats(db->nfldstat);
#else
		closestats(db);
#endif
}

/********************************************************************/
/*	Rewind a table so that the next get will return the first
 *	row in the table.
 */

void
TXrewinddbtblifnoindex(db)
DBTBL *db;			/* The table to rewind */
{
	if (db->index.btree == (BTREE *) NULL)
		TXrewinddbtbl(db);
	TXrowsread = 0;
}

/********************************************************************/
/*	Rewind a table so that the next get will return the first
 *	row in the table.
 */

void
TXrewinddbtbl(db)
DBTBL *db;			/* The table to rewind */
{
	if (TXlocktable(db, R_LCK) == 0)
	{
		if (db->tbl != (TBL *) NULL)
			rewindtbl(db->tbl);
		if (db->index.btree != (BTREE *) NULL)
			rewindbtree(db->index.btree);
		if (db->fi != FDBIPN)
			fdbi_rewind(db->fi);
		TXunlocktable(db, R_LCK);
		dbresetstats(db);
		TXsetrecid(&db->recid, 0);
	}
	TXrowsread = 0;
}

/******************************************************************/

#define LIMIT_TABLE_SIZE tablesizelimit

static EPI_OFF_T tablesizelimit = 0;

void
TXsettablesizelimit(sz)
EPI_OFF_T sz;
{
	tablesizelimit = sz;
}

/********************************************************************/
/*	Writes the current row in the table. The current data in
 *	the tables output buffer as set with putfld() is written to
 *	the table proper at the specified
 *	position.  If where  is -1 then the row is inserted at an
 *	undefined spot.
 *
 *	Returns the location where the record was stored.  Note that
 *	if a location is specified that is too small for the current
 *	data the location specified in where might not be used.
 */

RECID *
putdbtblrow(db, where)
DBTBL *db;			/* The table to write out */
RECID *where;			/* Where to put the data */
{
	static CONST char Fn[] = "putdbtblrow";
	size_t sz;
	static BTLOC btloc;
	static BTLOC pos;
	BTLOC *recidp;
	int rc;

	if ((int)(long)where == -1)
		where = (RECID *) NULL;
	if (TXprepareTableForWriting(db, PM_INSERT, NULL) == -1)
	{
		/* If SQLCancel() called, top-level user will yap: */
		if (!TXsqlWasCancelled(db->ddic))
			putmsg(MWARN, Fn, "Could not open indexes");
		return NULL;
	}
	switch (db->type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
		recidp = puttblrow(db->tbl, where);
		if (recidp)
		{
			char	msg[1024];
			pos = *recidp;
#if !defined(OLD_UPDATE_METHOD)	/* WTF - Attempt to fix KDBF problem, it moves the input buffer */
#ifdef KDBF_VOLATILE_GET	/* KNG 970818 KDBF now allows put after get */
			if (validrow(db->tbl, recidp))
				gettblrow(db->tbl, recidp);
#endif
#endif
			if (db->rname &&
			    db->tbl->df->dbftype != DBF_RAM &&
			    TXchecklicense(LIC_TABLE_SIZE,
					   (EPI_HUGEINT)TXgetoff(recidp), msg,
					   sizeof(msg)) < 0)
			{
				freedbf(db->tbl->df, TXgetoff(recidp));
				putmsg(MWARN, NULL,
			   "License violation for database `%s' table %s: %s",
				       (db->ddic ? db->ddic->epname : NULL),
				       db->lname, msg);
				TXsetrecid(&pos, (EPI_OFF_T) - 1);
			}
#ifdef LIMIT_TABLE_SIZE
			if (LIMIT_TABLE_SIZE && db->rname
			    && TXgetoff(recidp) > LIMIT_TABLE_SIZE
			    && db->tbl->df->dbftype != DBF_RAM)
			{
				freedbf(db->tbl->df, TXgetoff(recidp));
				putmsg(MWARN, NULL, "Table %s too big",
				       db->lname);
				TXsetrecid(&pos, (EPI_OFF_T) - 1);
			}
#endif
		}
		else
			TXsetrecid(&pos, (EPI_OFF_T) - 1);
		break;
	case 'B':
		sz = fldtobuf(db->tbl);
		if (TXrecidvalid(where))
			btloc = *where;
		else
			TXsetrecid(&btloc, (EPI_OFF_T) 0);
		rc = btinsert(db->index.btree, &btloc, sz, db->tbl->orec);
		btflush(db->index.btree);
		TXdoneWritingToTable(db, NULL);
		return (rc < 0 ? NULL : &btloc);
	default:
		TXdoneWritingToTable(db, NULL);
		return NULL;
	}
	db->recid = pos;

/*
	WTF: Should check for duplicates in unique index here.  Would
	need to back out insert possibly.  Actually should search
	indices first?  What about concurrent adds?  Need to lock all
	indices for this period.
*/

	if (TXrecidvalid(&pos))
	{
		if (TXaddtoindices(db) < 0)	/* duplicate */
		{
			freedbf(db->tbl->df, TXgetoff(&db->recid));
#ifdef NEVER
			putmsg(999, NULL, "Removing row at %d", pos.off);
#endif
			TXdoneWritingToTable(db, NULL);
			return NULL;
		}
	}
	else
	{
		putmsg(MWARN, NULL, "Record write failed");
	}
	TXdoneWritingToTable(db, NULL);
	return &pos;
}

void
TXdbtblTraceRowFieldsMsg(const char *fn, DBTBL *dbtbl, RECID recid,
			 char **tables, char **fields)
/* Issues tracerowfields putmsgs for `dbtbl', if listed in `tables'/`fields'.
 */
{
	static const char fmt[] =
		"Read table `%s' recid 0x%wx: field `%s' is `%s'";
	size_t	idx, colIdx, numCols, orgColIdx;
	char	*tblName, *fldName, *colName;
	DD	*dd;
	TXPMBUF	*pmbuf = (dbtbl->ddic ? dbtbl->ddic->pmbuf : TXPMBUFPN);

	if (!tables || !fields) return;
	for (idx = 0;
	     (tblName = tables[idx]) != NULL && (fldName = fields[idx])!=NULL;
	     idx++)
	{
		if ((*tblName == '*' && tblName[1] == '\0') ||
		    (dbtbl->lname && strcmp(dbtbl->lname, tblName) == 0) ||
		    (dbtbl->rname && strcmp(dbtbl->rname, tblName) == 0))
			;			/* table name matches */
		else
			continue;
		dd = tbldd(dbtbl->tbl);
		numCols = ddgetnfields(dd);
		for (colIdx = 0; colIdx < numCols; colIdx++)
		{				/* each real column */
			orgColIdx = ddgetorign(dd, (int)colIdx);
			colName = ddgetname(dd, (int)orgColIdx);
			if ((*fldName == '*' && fldName[1] == '\0') ||
			    strcmp(colName, fldName) == 0)
				txpmbuf_putmsg(pmbuf, MINFO, fn, fmt,
					       (dbtbl->lname ? dbtbl->lname :
						dbtbl->rname),
					       (EPI_HUGEINT)TXgetoff2(&recid),
					       colName,
				      fldtostr(dbtbl->tbl->field[orgColIdx]));
		}
		numCols = dbtbl->tbl->nvfield;
		for (colIdx = 0; colIdx < numCols; colIdx++)
		{				/* each virtual field */
			colName = dbtbl->tbl->vfname[colIdx];
			if ((*fldName == '*' && fldName[1] == '\0') ||
			    strcmp(colName, fldName) == 0)
				txpmbuf_putmsg(pmbuf, MINFO, fn, fmt,
					       (dbtbl->lname ? dbtbl->lname :
						dbtbl->rname),
					       (EPI_HUGEINT)TXgetoff2(&recid),
					       colName,
					fldtostr(dbtbl->tbl->vfield[colIdx]));
		}
	}
}

/********************************************************************/
/*	Reads the next row in from a table.
 *
 *	Returns the location of the row retrieved.
 */

RECID *
getdbtblrow(db)
DBTBL *db;			/* The table to read from */
{
	static const char	fn[] = "getdbtblrow";
	size_t i;
	BTLOC *rc = NULL;
	static BTLOC btloc;
	int nolock = 0;
	FLD *fword, *fcount;
	TTL *ttl;
	byte *bufp;

	switch (db->type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
		if (db->rlock > 0)
			nolock = 1;
		else
		{
			if (TXlocktable(db, R_LCK) == -1)
				goto err;
		}
		rc = gettblrow(db->tbl, NULL);
		if (nolock == 0)
			TXunlocktable(db, R_LCK);
		goto finally;
	case INDEX_BTREE:
		i = BT_MAXPGSZ;
		bufp = tempbuf;
#if defined(__hpux) || defined(__DGUX__)
		while ((long) bufp % TX_ALIGN_BYTES)
			bufp++;
#endif
		btloc = dbidxgetnext(&db->index, &i, bufp, NULL);
		if (!TXrecidvalid(&btloc))
		{
			/* KNG 20081211 some callers expect NULL not &{-1}: */
			goto err;
		}
		buftofld(bufp, db->tbl, i);
		rc = &btloc;
		goto finally;
	case INDEX_3DB:
		do
		{
			i = BT_MAXPGSZ;
			btloc = btgetnext(db->dbi->mm->bt, &i, tempbuf, NULL);
		}
		while (TXrecidvalid(&btloc) &&
		       !strcmp((char *) tempbuf, LASTTOKEN));
		if (!TXrecidvalid(&btloc))
			goto err;
		fword = nametofld(db->tbl, "Word");
		fcount = nametofld(db->tbl, "Count");
		ttl = getdbfttl(db->dbi->mm->bdbf, TXgetoff(&btloc));
		i = countttl(ttl);
		ttl = closettl(ttl);
		putfld(fword, tempbuf, strlen((char *) tempbuf));
		putfld(fcount, &i, 1);
		gettblrow(db->tbl, puttblrow(db->tbl, NULL));
		rc = &btloc;
		goto finally;
	case INDEX_MM:
	case INDEX_FULL:
		{
			CONST char	*wd;
			ft_int64	rowCount, occurrenceCount;
			FLD		*fld;

			btloc = fdbi_getnextrow(db->fi, &wd, &rowCount,
						&occurrenceCount);
			if (TXrecidvalid(&btloc))
			{
				if ((fld = nametofld(db->tbl, "Word")) != FLDPN &&
				    (fld->type & DDTYPEBITS) == FTN_CHAR)
                                  putfld(fld, (char *)wd, strlen(wd));
				if ((fld = nametofld(db->tbl, "Count")) != FLDPN &&
				    (fld->type & DDTYPEBITS) == FTN_INT64)
					putfld(fld, &rowCount, 1);
				if ((fld = nametofld(db->tbl, "RowCount")) != FLDPN &&
				    (fld->type & DDTYPEBITS) == FTN_INT64)
					putfld(fld, &rowCount, 1);
				if ((fld = nametofld(db->tbl, "OccurrenceCount")) != FLDPN &&
				    (fld->type & DDTYPEBITS) == FTN_INT64)
					putfld(fld, &occurrenceCount, 1);
				gettblrow(db->tbl, puttblrow(db->tbl, NULL));	/* ? */
				rc = &btloc;
				goto finally;
			}
			goto err;
		}
		break;
#ifdef HAVE_LINK_TABLES
	case TEXIS_LINK:
		rc = gettblrow(db->tbl, NULL);
		goto finally;
#endif
	}

err:
	rc = NULL;
finally:
	if (rc && TXApp && TXApp->traceRowFieldsTables &&
	    TXApp->traceRowFieldsFields)
	{
		TXdbtblTraceRowFieldsMsg(fn, db, *rc,
					 TXApp->traceRowFieldsTables,
					 TXApp->traceRowFieldsFields);
	}
	return(rc);
}

/* ------------------------------------------------------------------------ */

size_t
TXdbtblGetRowSize(dbtbl)
DBTBL	*dbtbl;
/* Returns size of current row.
 */
{
	return(dbtbl->tbl ? dbtbl->tbl->irecsz : 0);
}

/* ------------------------------------------------------------------------ */

int
TXdbtblReleaseRow(dbtbl)
DBTBL   *dbtbl;
/* Releases memory associated with current row of `dbtbl'.
 * Any fields pointing directly into its data will be cleared
 * and should not be used until next getdbtblrow().
 * Returns 2 on success, 1 if ok (nothing freed), 0 on error.
 */
{
	switch (dbtbl->type)
	{
	case TEXIS_TABLE:
	case TEXIS_SYS_TABLE:
		return(TXtblReleaseRow(dbtbl->tbl));
	/* These table types may use KDBF underneath, but they are not
	 * one-to-one table-to-KDBF:
	 */
	case INDEX_BTREE:
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
#ifdef HAVE_LINK_TABLES
	case TEXIS_LINK:
#endif /* HAVE_LINK_TABLES */
	default:
		break;
	}
	return(1);			/* ok but nothing freed */
}

/********************************************************************/

int
renametbl(db, newname)
DBTBL *db;
char *newname;
{
	static CONST char Fn[] = "renametbl";
	char tn[80], *old, *nn;
	int i;
	int nfields;

	nn = strdup(newname);
	if (nn == (char *) NULL)
	{
		putmsg(MWARN + MAE, Fn, strerror(ENOMEM));
		return -1;
	}
	if (db->lname != (char *) NULL)
		free(db->lname);
	db->lname = nn;
	nfields = ddgetnfields(db->tbl->dd);
	for (i = 0; i < nfields; i++)
	{
		old = ddgetname(db->tbl->dd, i);
		if (strchr(old, '.'))
		{
			strcpy(tn, newname);
			strcat(tn, strchr(old, '.'));
			strcpy(old, tn);
		}
	}
	return 0;
}

DBTBL *
TXopentmpdbtbl_tbl(TBL *tbl, char *lname, char *rname, DDIC * ddic)
{
	DBTBL *rc = NULL;

	rc = TXnewDbtbl(ddic->pmbuf);
	if (rc)
	{
		if (lname)
			rc->lname = strdup(lname);
		if (rname)
			rc->rname = strdup(rname);
		rc->tbl = tbl;
		rc->type = TEXIS_TABLE;
		rc->ddic = ddic;
		rc->frecid = createfld("recid", 1, 0);
		rc->tblid = -1;
		putfld(rc->frecid, &rc->recid, 1);
	}
	return rc;
}

DBTBL *
TXopentmpdbtbl(char *fname, char *lname, char *rname, DD * dd, DDIC * ddic)
{
	DBTBL *rc = NULL;
	TBL *tbl = NULL;

	tbl = createtbl(dd, fname);
	if(tbl)
		rc = TXopentmpdbtbl_tbl(tbl, lname, rname, ddic);
	return rc;
}

static char fqs[80];

/******************************************************************/
/* Convert a virtual field name such that all components exist as
   written in the virtual field.
 */

static char *convvirtname ARGS((DBTBL *, char *));

static char *
convvirtname(d, s)
DBTBL *d;
char *s;
{
	char *e, *r, *c;
	static char rc[1024];
	FLD *f;

	f = nametofld(d->tbl, s);
	if (f)
        {
		return s;
        }
	c = s;
	e = strchr(c, '\\');
	if (!e)
		return s;
	rc[0] = '\0';
	while (e)
	{
		*e = '\0';
		r = dbnametoname(d, c, FTNPN, INTPN);
		if (r)
		{
			strcat(rc, r);
			strcat(rc, "\\");
		}
		else
		{
			*e = '\\';
			return s;
		}
		c += strlen(c) + 1;
		*e = '\\';
		e = strchr(c, '\\');
	}
	r = dbnametoname(d, c, FTNPN, INTPN);
	if (r)
		strcat(rc, r);
	else
	{
		return s;
	}
	return rc;
}

/********************************************************************/
/*	Get a field from a DBTBL.
 *
 *	This will hunt for the field in the table, taking into account
 *	fully qualified or partial names.
 *
 *	Returns field named s in d
 */

FLD *
dbnametofld(d, t)
DBTBL *d;			/* Table to search */
char *t;			/* Name of the field */
{
	char *f, *s, *x;
	FLD *fld = (FLD *) NULL;
	unsigned i;
	char	tmp[80];

	if (!d)			/* Sanity check */
		return NULL;
	if (!t)			/* Sanity check */
		return NULL;
#ifdef TX_USE_ORDERING_SPEC_NODE
	/* With a separate ORDERING_SPEC_OP node, most field names
	 * no longer have `^' `-' etc. appended, especially in QNODE
	 * tree.  But SYSINDEX.FIELDS values still do, and may be
	 * passed in here (e.g. from TXgetNextIndexFldFromTable()),
	 * so we still need to check for them:
	 */
#endif /* TX_USE_ORDERING_SPEC_NODE */
	if (t[0] && t[strlen(t) - 1] == '^')	/* Should remove IGNCASE mark */
	{
		strcpy((char *) tempbuf, t);
		s = (char *) tempbuf;
		s[strlen(s) - 1] = '\0';
	}
	else
		s = t;
	if (s[0] && s[strlen(s) - 1] == '-')	/* Should remove DESC mark */
	{
		if(s != (char *)tempbuf)
		{
			strcpy((char *) tempbuf, s);
			s = (char *) tempbuf;
		}
		s[strlen(s) - 1] = '\0';
	}
	x = s;
	s = convvirtname(d, x);
	if (d == (DBTBL *) NULL || d->tbl == (TBL *) NULL)
		return (FLD *) NULL;
	if (d->rname && !strcmp(s, TXrecidColumnName))
	{
		return d->frecid;
	}
	if (d->rname != (char *) NULL)
	{
		strcpy(tmp, d->rname);
		strcat(tmp, ".$recid");
		if (!strcmp(s, tmp))
		{
			return d->frecid;
		}
		if (d->lname != CHARPN)
		{
			strcpy(tmp, d->lname);
			strcat(tmp, ".$recid");
			if (!strcmp(s, tmp))
			{
				return d->frecid;
			}
		}
	}
	if (strchr(s, '.') && !strchr(s, '\\') && !strstr(s, ".$.") && !strstr(s, ".$["))	/* We are searching for a fully qualified name */
	{
		for (i = 0; i < d->tbl->n; i++)
		{
			f = ddgetname(d->tbl->dd, i);
			if (strchr(f, '.'))
			{
				if (strncmp(s, f, DDNAMESZ - 1) == 0)
				{
					return d->tbl->field[i];
				}
			}
			else if (d->lname != CHARPN)
			{
				strcpy(tmp, d->lname);
				strcat(tmp, ".");
				strcat(tmp, f);
				if (strncmp(s, tmp, DDNAMESZ - 1) == 0)
				{
					return d->tbl->field[i];
				}
			}
		}
		return (FLD *) NULL;
	}
	else
	{
		fld = nametofld(d->tbl, s);
		if (fld != (FLD *) NULL)
		{
			return fld;
		}
		for (i = 0; i < d->tbl->n; i++)
		{
			const char	*dot;

			f = ddgetname(d->tbl->dd, i);
			if (f && (dot = strchr(f, '.')) != CHARPN)
			{
				if (strcmp(dot + 1, s) == 0)
				{
					if (fld != (FLD *) NULL)
					{
						return (FLD *) NULL;
					}
					fld = d->tbl->field[i];
				}
			}
		}
		return fld;
	}
}

/* ------------------------------------------------------------------------ */

int
TXisRankName(const char *name)
/* Returns nonzero if `name' is the $rank column name, or a DESC/NOCASE
 * variant.
 */
{
	const char	*e;

	e = name + strlen(name);
	/* Bug 4849: allow $rank DESC: */
#ifdef TX_USE_ORDERING_SPEC_NODE
	/* See ORDERING_SPEC_OP comment above */
#endif /* TX_USE_ORDERING_SPEC_NODE */
	if (e > name && e[-1] == '^') e--;	/* NOCASE/IGNCASE */
	if (e > name && e[-1] == '-') e--;	/* DESC */
	if (e - name == TX_RANK_COLUMN_NAME_LEN &&
	    strncmp(name, TXrankColumnName, TX_RANK_COLUMN_NAME_LEN) == 0)
		return(1);
	return(0);
}

/********************************************************************/
/*
   Converts a field name to the name actually used in the table.
 */

char *
dbnametoname(d, s, type, ddIdx)
DBTBL *d;
char *s;
FTN	*type;	/* (out, opt.) type of `s' */
int	*ddIdx;	/* (out, opt.) `d->tbl->dd->fd' index, if applicable (or -1)*/
{
	char *f = NULL, *f1 = (char *) NULL;
	FTN	firstType;
	int	firstIdx;
	char *x;
	FLD *fld = (FLD *) NULL;
	unsigned i;
	size_t	lnameLen;

	if (ddIdx) *ddIdx = -1;
	if (!strcmp(s, TXrankColumnName))
	{
		strcpy(fqs, s);
		if (type) *type = TX_RANK_COLUMN_TYPE_FTN;
		return fqs;
	}
	if (!strcmp(s, TXrecidColumnName))
	{
		strcpy(fqs, s);
		if (type) *type = TX_RECID_COLUMN_TYPE_FTN;
		return fqs;
	}
	if (d->lname && (lnameLen = strlen(d->lname)) + 1 +
	    TX_MAX(TX_RANK_COLUMN_NAME_LEN, TX_RECID_COLUMN_NAME_LEN) <
	    sizeof(fqs))
	{
		strcpy(fqs, d->lname);
		fqs[lnameLen] = '.';
		strcpy(fqs + lnameLen + 1, TXrankColumnName);
		if (!strcmp(s, fqs))
		{
			if (type) *type = TX_RANK_COLUMN_TYPE_FTN;
			return fqs;
		}
		strcpy(fqs + lnameLen + 1, TXrecidColumnName);
		if (!strcmp(s, fqs))
		{
			if (type) *type = TX_RECID_COLUMN_TYPE_FTN;
			return fqs;
		}
	}
#ifndef ALLOW_MISSING_FIELDS
	x = s;
	s = convvirtname(d, x);
#endif
	if (strchr(s, '.') &&
           !strchr(s, '\\') &&
           !strstr(s, ".$.") &&
           !strstr(s, ".$["))	/* We are searching for a fully qualified name */
	{
		for (i = 0; i < d->tbl->n; i++)
		{
			f = ddgetname(d->tbl->dd, i);
			if (strchr(f, '.'))
			{
				if (strncmp(s, f, DDNAMESZ - 1) == 0)
				{
					if (type)
						*type = d->tbl->field[i]->type;
					if (ddIdx)
						*ddIdx = i;
					return f;
				}
			}
			else if (d->lname != CHARPN)
			{
				strcpy(fqs, d->lname);
				strcat(fqs, ".");
				strcat(fqs, f);
				if (strncmp(s, fqs, DDNAMESZ - 1) == 0)
				{
					if (type)
						*type = d->tbl->field[i]->type;
					if (ddIdx)
						*ddIdx = i;
					return f;
				}
			}
		}
		if (type) *type = (FTN)0;
		return (char *) NULL;
	}
	else
	{
		fld = nametofld(d->tbl, s);
		if (fld != (FLD *) NULL)
		{
			if (type) *type = fld->type;
			/* wtf can we set *ddIdx?  sometimes... */
			return s;
		}
		f1 = NULL;
		firstType = (FTN)0;
		firstIdx = -1;
		for (i = 0; i < d->tbl->n; i++)
		{
			const char	*dot;

			f = ddgetname(d->tbl->dd, i);
			if (f && (dot = strchr(f, '.')) != CHARPN)
			{
				if (strcmp(dot + 1, s) == 0)
				{
					if (f1 != (char *) NULL)
					{
						if (type) *type = (FTN)0;
						return (char *) NULL;
					}
					firstType = d->tbl->field[i]->type;
					firstIdx = i;
					f1 = f;
				}
			}
		}
		if (type) *type = firstType;
		if (ddIdx) *ddIdx = firstIdx;
		return f1;
	}
}

/********************************************************************/

DBTBL *
deltable(tb)
DBTBL *tb;
{
	static CONST char Fn[] = "deltable";
	char *fname = getdbffn(tb->tbl->df);	/* MAW 04-04-94 replace drill */

	if (fname != (char *) NULL && deldbf(fname) == -1)
	{
		putmsg(MWARN + FDE, Fn, strerror(errno));
	}
	return closedbtbl(tb);
}

/******************************************************************/

int
TXlocktable(db, type)
DBTBL *db;
int type;	/* (in) lock type: {R,W}_LCK [ | V_LCK] */
/* Locks table `db'.
 * Returns -1 on error (no lock), or 0 on success (and no mods if V_LCK?).
 * Cannot return -2 (`type' contains V_LCK and `db' was modified
 * since last lock) because we pass NULL `counterp' to dblock() here?
 * NOTE: V_LCK flag may be ignored (by dblock() call here)?
 */
{
	static const char	fn[] = "TXlocktable";
	int rc;
	int attempts;
	LOCKTABLES_RETURN ltr;

	attempts = 0;
	if ((!db->ddic) || db->ddic->manuallocking)
		return 0;
	if (isramdbtbl(db))
		return 0;
	if (!db->rname)
		return 0;
      retrytlock:
	ltr = LockTablesLock(db, type);
	switch (ltr) {
		case LOCKTABLES_SKIP: break;
		case LOCKTABLES_ERR: return -1;
		case LOCKTABLES_OK: return 0;
		case LOCKTABLES_MODIFIED: return -2;
	}
#ifdef USE_FLOCK
	switch (type)
	{
	case R_LCK:
		rc = flock(getdbffh(db->tbl->df), LOCK_SH);
		break;
	case W_LCK:
		rc = flock(getdbffh(db->tbl->df), LOCK_EX);
		break;
	}
#else
	switch (type)
	{
	case R_LCK:
	case R_LCK | V_LCK:
		if (db->rlock)
		{
			db->rlock++;
			db->ddic->rlocks++;	/* KNG 030627 table+index */
			return 0;
		}
		break;
	case W_LCK:
	case W_LCK | V_LCK:
		if (db->wlock)
		{
			db->wlock++;
			db->ddic->wlocks++;	/* KNG 030627 table+index */
			return 0;
		}
		break;
	default:
		/* e.g. V_LCK alone invalid here: we pass NULL `counterp'
		 * to dblock() below:
		 */
		txpmbuf_putmsg(db->ddic->pmbuf, MERR, fn,
			       "Invalid lock type %d", (int)type);
		return(-1);
	}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
	rc =
		dblock(db->ddic, db->ddic->sid, &db->tblid, type, db->rname,
		       NULL, &db->lockid);
#else
	rc =
		dblock(db->ddic, db->ddic->sid, &db->tblid, type, db->rname,
		       NULL);
#endif
#else
	rc =
		dblock(db->ddic, db->ddic->sid, db->tblid, type, db->rname,
		       NULL);
#endif
#endif
	DBGMSG(9,
	       (999, NULL, "%d Locking table %s mode %d result %d", getpid(),
		db->rname, type, rc));
	if (rc == -1)				/* failed to get lock */
	{
		if (texispeekerr(db->ddic) ==
		    MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
		{
			attempts++;
			if (attempts < 2)
			{
				texispoperr(db->ddic);	/* Get rid of error */
				goto retrytlock;
			}
			/* no error message on LOCK_TIMEOUT: caused by
			 * signal or timeout, which should have already
			 * yapped, and top-level caller should call
			 * TXsqlWasCancelled() and yap too
			 */
		}
		else
		    texispusherror(db->ddic, MAKEERROR(MOD_LOCK, LOCK_FAIL));
	}
	else					/* successfully got lock */
	{
		switch (type)
		{
		case R_LCK:
			db->rlock++;
			db->ddic->rlocks++;	/* KNG 030627 table+index */
			break;
		case W_LCK:
			db->wlock++;
			db->ddic->wlocks++;	/* KNG 030627 table+index */
			break;
		}
	}
	return rc;
}

/******************************************************************/

int
TXunlocktable(db, type)
DBTBL *db;
int type;
{
	int rc;
	LOCKTABLES_RETURN ltr;

	if ((!db->ddic) || db->ddic->manuallocking)
		return 0;
	if (isramdbtbl(db))
		return 0;
	ltr = LockTablesUnlock(db, type);
	switch (ltr) {
		case LOCKTABLES_SKIP: break;
		case LOCKTABLES_ERR: return -1;
		case LOCKTABLES_OK: return 0;
		case LOCKTABLES_MODIFIED: return -2;
	}
	switch (type)
	{
	case R_LCK:
		if (db->rlock == 0)
			return 0;
		if (db->rlock > 1)
		{
			db->rlock--;
			db->ddic->rlocks--;	/* KNG 030627 table+index */
			return 0;
		}
		break;
	case W_LCK:
		if (db->wlock == 0)
			return 0;
		if (db->wlock > 1)
		{
			db->wlock--;
			db->ddic->wlocks--;	/* KNG 030627 table+index */
			return 0;
		}
		break;
	}
	if (!db->rname)
		return 0;
#ifdef USE_FLOCK
	rc = flock(getdbffh(db->tbl->df), LOCK_UN);
	if (rc == -1 && errno == EBADF)
		rc = 0;
	rc = 0;
#else
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
	rc =
		dbunlock(db->ddic, db->ddic->sid, &db->tblid, type, db->rname,
			 &db->lockid);
#else
	rc = dbunlock(db->ddic, db->ddic->sid, &db->tblid, type, db->rname);
#endif
#else
	rc = dbunlock(db->ddic, db->ddic->sid, db->tblid, type, db->rname);
#endif
#endif
	DBGMSG(9,
	       (999, NULL, "Unlocking table %s mode %d result %d", db->rname,
		type, rc));
	if (rc == -1)
	{
		switch (type)
		{
		case R_LCK:
			db->ddic->rlocks -= db->rlock;
			db->rlock = 0;
			break;
		case W_LCK:
			db->ddic->wlocks -= db->wlock;
			db->wlock = 0;
			break;
		}
		return rc;
	}
	switch (type)
	{
	case R_LCK:
		db->rlock--;
		db->ddic->rlocks--;	/* KNG 030627 table+index */
		break;
	case W_LCK:
		db->wlock--;
		db->ddic->wlocks--;	/* KNG 030627 table+index */
		break;
	}
	return rc;
}

/******************************************************************/

int
TXlockindex(db, mode, fc)
DBTBL *db;
int mode;		/* (in) INDEX_WRITE, INDEX_READ, INDEX_VERIFY, etc. */
ft_counter *fc;		/* in: verify this  out: new time */
/* Obtains lock indicated by `mode' (if only V_LCK/INDEX_VERIFY: no lock,
 * and `*fc' not modified).
 * Returns -2 if INDEX_VERIFY flag set in `mode' and index modified since
 * `*fc'; -1 on error (no lock granted); 0 if no mods (or INDEX_VERIFY flag
 * not set).  Lock granted unless -1 returned (or no ...READ/WRITE in `mode').
 */
{
	static const char	fn[] = "TXlockindex";
	int rc = -1;
	int attempts;
	ft_counter	b4;

	if (fc)
		b4 = *fc;
	else
	{
		b4.date = 0L;
		b4.seq = 0L;
	}
	DBGMSG(9, (999, NULL, "%d Locking %s, in mode %d at %lx:%lx",
		   getpid(), db->rname, mode, fc ? fc->date : 0,
		   fc ? fc->seq : 0));
	attempts = 0;
	if (isramdbtbl(db))
		goto ok;
	do
	{
		switch (mode)
		{
		case INDEX_READ:
			if (db->nireadl)
			{
				db->nireadl += 1;
				db->ddic->rlocks++;	/* KNG table+index */
				if (fc)
					rgetcounter(db->ddic, fc, 1);
				goto ok;
			}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc, &db->lockid);
#else
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc);
#endif
#else
			rc = dblock(db->ddic, db->ddic->sid, db->tblid,
				    mode, db->rname, fc);
#endif
#ifdef NEVER
			putmsg(999, NULL,
			       "Locking index %s mode %d result %d",
			       db->rname, mode, rc);
#endif
			if (rc == 0)		/* success */
			{
				db->nireadl = 1;
				db->ddic->rlocks++;	/* KNG table+index */
			}
			break;
		case INDEX_WRITE:
			if (db->niwrite)
			{
				db->niwrite += 1;
				db->ddic->wlocks++;	/* KNG table+index */
				if (fc)
					rgetcounter(db->ddic, fc, 1);
				goto ok;
			}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc, &db->lockid);
#else
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc);
#endif
#else
			rc = dblock(db->ddic, db->ddic->sid, db->tblid,
				    mode, db->rname, fc);
#endif
#ifdef NEVER
			putmsg(999, NULL,
			       "Locking index %s mode %d result %d at %lx:%lx",
			       db->rname, mode, rc, fc ? fc->date : 0,
			       fc ? fc->seq : 0);
#endif
			if (rc == 0)		/* success */
			{
				db->niwrite = 1;
				db->ddic->wlocks++;	/* KNG table+index */
			}
			break;
		case INDEX_VERIFY:
#ifdef NEVER
			if (!db->niwrite)
				goto err;
#endif
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc, &db->lockid);
#else
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc);
#endif
#else
			rc = dblock(db->ddic, db->ddic->sid, db->tblid,
				    mode, db->rname, fc);
#endif
#ifdef NEVER
			if (rc == -1)
				putmsg(999, NULL, "Verify %d", rc);
#endif
			break;
		case INDEX_VERIFY | INDEX_WRITE:
			if (db->niwrite)
			{
				/* Assume still good, as we have lock */
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
				rc =
					dblock(db->ddic, db->ddic->sid,
					       &db->tblid, INDEX_VERIFY,
					       db->rname, fc, &db->lockid);
#else
				rc =
					dblock(db->ddic, db->ddic->sid,
					       &db->tblid, INDEX_VERIFY,
					       db->rname, fc);
#endif
#else
#endif
				db->niwrite += 1;
				db->ddic->wlocks++;	/* KNG table+index */
				if (fc)
					rgetcounter(db->ddic, fc, 1);
				goto done;
			}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc, &db->lockid);
#else
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc);
#endif
#else
			rc = dblock(db->ddic, db->ddic->sid, db->tblid,
				    mode, db->rname, fc);
#endif
#ifdef NEVER
			putmsg(999, NULL,
			       "%d Locking index %s mode %d result %d at %lx:%lx",
			       getpid(), db->rname, mode, rc,
			       fc ? fc->date : 0, fc ? fc->seq : 0);
#endif
#ifdef NEVER
			if (rc == 0)		/* success + no recent mods */
#endif
				if (rc != -1)	/* success */
				{
					db->niwrite = 1;
					db->ddic->wlocks++;	/* KNG table+index */
				}
			goto done;
		case INDEX_VERIFY | INDEX_READ:
			if (db->nireadl)
			{
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
				rc =
					dblock(db->ddic, db->ddic->sid,
					       &db->tblid, INDEX_VERIFY,
					       db->rname, fc, &db->lockid);
#else
				rc =
					dblock(db->ddic, db->ddic->sid,
					       &db->tblid, INDEX_VERIFY,
					       db->rname, fc);
#endif
#else
#endif
				/* Assume still good, as we have lock */
				db->nireadl += 1;
				db->ddic->rlocks++;	/* KNG table+index */
				if (fc)
					rgetcounter(db->ddic, fc, 1);
				goto done;
			}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc, &db->lockid);
#else
			rc = dblock(db->ddic, db->ddic->sid, &db->tblid,
				    mode, db->rname, fc);
#endif
#else
			rc = dblock(db->ddic, db->ddic->sid, db->tblid,
				    INDEX_READ, db->rname, fc);
			rc = -1;
#endif
#ifdef NEVER
			putmsg(999, NULL,
			       "%d Locking index %s mode %d result %d at %lx:%lx",
			       getpid(), db->rname, mode, rc,
			       fc ? fc->date : 0, fc ? fc->seq : 0);
#endif
#ifdef NEVER
			if (rc == 0)		/* success + no recent mods */
#endif
				if (rc != -1)	/* success */
				{
					db->nireadl = 1;
					db->ddic->rlocks++;	/* KNG table+index */
				}
			goto done;
		}
	}
	while (rc == -1 &&
	       texispeekerr(db->ddic) == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT) &&
	       ++attempts < 1 &&		/* no point trying twice */
	       /* KNG 20131125: only pop LOCK_TIMEOUT error if retrying: */
	       (texispoperr(db->ddic), 1));
	/* no error message on LOCK_TIMEOUT: caused by signal or
	 * timeout, which should have already yapped, and top-level
	 * caller should call TXsqlWasCancelled() and yap too
	 */
	goto done;

ok:
	rc = 0;
done:
	if (!TXTRACELOCKS_IS_OFF())
		tx_loglockop(db->ddic, fn, mode, NULL, db, &b4, fc, &rc);
	return rc;
}

/******************************************************************/

int
TXunlockindex(db, mode, fc)
DBTBL *db;
int mode;
ft_counter *fc;
{
	static const char	fn[] = "TXunlockindex";
	int rc;
	ft_counter	b4;

	if (fc)
		b4 = *fc;
	else
	{
		b4.date = 0L;
		b4.seq = 0L;
	}

#ifdef NEVER
	goto ok;
#endif
	if (!db->tbl || isramdbtbl(db))
	{
		db->ddic->rlocks -= db->nireadl;	/* KNG table+index */
		db->nireadl = 0;
		db->ddic->wlocks -= db->niwrite;	/* KNG table+index */
		db->niwrite = 0;
		goto ok;
	}
	DBGMSG(9, (999, NULL, "Unlocking %s, in mode %d", db->rname, mode));
	switch (mode)
	{
	case INDEX_READ:
		if (db->nireadl > 1)
		{
			db->nireadl -= 1;
			db->ddic->rlocks--;	/* KNG table+index */
			if (fc)
				rgetcounter(db->ddic, fc, 1);
			goto ok;
		}
		if (!db->nireadl)
			goto err;
		break;
	case INDEX_WRITE:
		flushindexes(db);
		if (db->niwrite > 1)
		{
			db->niwrite -= 1;
			db->ddic->wlocks--;	/* KNG table+index */
			if (fc)
				rgetcounter(db->ddic, fc, 1);
			goto ok;
		}
		if (!db->niwrite)
			goto err;
		break;
	}
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
	rc =
		dbunlock(db->ddic, db->ddic->sid, &db->tblid, mode, db->rname,
			 &db->lockid);
#else
	rc = dbunlock(db->ddic, db->ddic->sid, &db->tblid, mode, db->rname);
#endif
#else
	rc = dbunlock(db->ddic, db->ddic->sid, db->tblid, mode, db->rname);
#endif
#ifdef NEVER
	putmsg(999, NULL, "Unlocking index %s mode %d result %d", db->rname,
	       mode, rc);
#endif
#ifdef NEVER			/* If unlock failed, there are no locks */
	if (rc == 0)
#endif
		switch (mode)
		{
		case INDEX_READ:
			db->ddic->rlocks -= db->nireadl;/* KNG table+index */
			db->nireadl = 0;
			break;
		case INDEX_WRITE:
			db->ddic->wlocks -= db->niwrite;/* KNG table+index */
			db->niwrite = 0;
			break;
		}
	goto done;

ok:
	rc = 0;
	goto done;
err:
	rc = -1;
done:
	if (!TXTRACELOCKS_IS_OFF())
		tx_loglockop(db->ddic, fn, mode, NULL, db,
			(fc ? &b4 : fc), fc, &rc);
	return rc;
}

/******************************************************************/

int
TXprepareTableForWriting(DBTBL * tb, int mode, char **updfields)
/* Returns 0 on success, -1 on error.
 */
{
	int rc;

	if ((tb->type == TEXIS_TABLE || tb->type == TEXIS_SYS_TABLE)
	    && tb->rname)
	{
		if (!updfields)
			if (!tb->indexfields)
				rc = tb->nindex + tb->ndbi + tb->ninv;
			else
				rc = 0;
		else
		{
			rc = TXstrlstcmp(updfields, tb->indexfields);
			rc = rc ? 0 : 1;
		}
		if (rc == 0)
		{
			if (TXlockindex(tb, INDEX_WRITE, &tb->iwritec) == -1)
				return(-1);	/* KNG 20131125 check ret */
			if (TXgetindexes(tb, mode, updfields, 0) == -1)
			{			/* failed to open indexes */
				TXunlockindex(tb, INDEX_WRITE, NULL);
				return -1;
			}
		}
		else
		{
			switch (TXlockindex(tb, INDEX_VERIFY | INDEX_WRITE,
					    &tb->iwritec))
			{
			case -2:		/* success (+ recent mods) */
				if (TXgetindexes(tb, mode, updfields, 0) == -1)
				{		/* failed to open indexes */
					TXunlockindex(tb, INDEX_WRITE, NULL);
					return -1;
				}
				break;
			case -1:		/* failed to lock */
				return -1;
			}
		}
	}
	if(TXlocktable(tb, W_LCK) == -1) {
		TXunlockindex(tb, INDEX_WRITE, NULL);
		return -1;
	}
	return 0;
}

/******************************************************************/

int
TXdoneWritingToTable(DBTBL *tb, ft_counter *fc)
{
	flushindexes(tb);
	TXunlocktable(tb, W_LCK);
	if ((tb->type == TEXIS_TABLE || tb->type == TEXIS_SYS_TABLE)
			&& tb->rname)
	{
		TXunlockindex(tb, INDEX_WRITE, fc);
	}
	return 0;
}
/******************************************************************/

fop_type o_n_fchch = NULL, o_n_fidch = NULL, o_n_fdwch = NULL,
	o_n_fblch = NULL, o_n_fbych = NULL;

int metamorphop ARGS((FLD *, FLD *));
int fmetamorphop ARGS((FLD *, FLD *));

int
n_fchch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_int rc;

	switch (op)
	{
	case FOP_MM:
		mac_vsum();
		rc = metamorphop(f1, f2);
		fld2finv(f3, rc);
		return 0;
	case FOP_NMM:
	case FOP_RELEV:
	case FOP_PROXIM:
		return fld2finv(f3, 1);
	case FOP_MMIN:
		rc = TXlikein(f1, f2);
		fld2finv(f3, rc);
		return 0;
	case FOP_MAT:
		rc = TXmatchesc(f1, f2);
		fld2finv(f3, rc);
		return 0;
	default:
		DBGMSG(9, (999, NULL, "OP = %d", op));
		if (o_n_fchch)
			return ((*o_n_fchch) (f1, f2, f3, op));
		return(FOP_EINVAL);
	}
}

/******************************************************************/
/*	The fldmath handler for the metamorph op.
 */

int
n_fbych(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_int rc;

	switch (op)
	{
	case FOP_MM:
		mac_vsum();
		rc = metamorphop(f1, f2);
		fld2finv(f3, rc);
		return 0;
	case FOP_NMM:
	case FOP_RELEV:
	case FOP_PROXIM:
		return fld2finv(f3, 1);
	case FOP_MAT:
		rc = TXmatchesc(f1, f2);
		fld2finv(f3, rc);
		return 0;
	default:
		DBGMSG(9, (999, NULL, "OP = %d", op));
		if (o_n_fbych)
			return ((*o_n_fbych) (f1, f2, f3, op));
		return(FOP_EINVAL);
	}
}

/******************************************************************/
/*	Fldmath handler for metamorph on an indirect.
 */

int
n_fidch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_int rc;

	switch (op)
	{
	case FOP_MM:
		mac_vsum();
		rc = fmetamorphop(f1, f2);
		fld2finv(f3, rc);
		return 0;
	case FOP_NMM:
	case FOP_RELEV:
	case FOP_PROXIM:
		return fld2finv(f3, 1);
	default:
		if (o_n_fidch)
			return ((*o_n_fidch) (f1, f2, f3, op));
		return(FOP_EINVAL);
	}
}

/******************************************************************/
/*	Fldmath handler for metamorph on a blob.
 */

int
n_fblch(f1, f2, f3, op)
FLD *f1;
FLD *f2;
FLD *f3;
int op;
{
	ft_int rc;

	switch (op)
	{
	case FOP_MM:
		rc = bmetamorphop(f1, f2);
		fld2finv(f3, rc);
		return 0;
	default:
		if (o_n_fblch)
			return ((*o_n_fblch) (f1, f2, f3, op));
	}
	return -1;
}

/******************************************************************/
/*
 *	Text2mm's a buffer.
 */
int
TXsqlFunc_text2mm(f1, f2)
FLD *f1;
FLD *f2;
{
	char *mmr;
	char *text;
	size_t sz;
	int maxwords;

	if (f2)
		maxwords = *(ft_long *) getfld(f2, &sz);
	else
		maxwords = 0;
	if ((f1->type & DDTYPEBITS) != FTN_CHAR)
		if ((f1->type & DDTYPEBITS) != FTN_BYTE)
		{
			return -1;
		}
	text = getfld(f1, &sz);
	mmr = text2mm(text, maxwords, globalcp);
	f1->type = FTN_CHAR | DDVARBIT;
	f1->kind = TX_FLD_NORMAL;
	f1->elsz = 1;
	putfld(f1, mmr, strlen(mmr));
	/*
	   setfld(f1, mmr, strlen(mmr));
	   f1->alloced = f1->n = f1->size = strlen(mmr);
	 */
	return 0;
}

/******************************************************************/
/*
 *	Keyword's a buffer.
 */
int
TXsqlFunc_keywords(f1, f2)
FLD *f1;
FLD *f2;
{
	char *mmr;
	char *text;
	size_t sz;
	int maxwords;

	if (f2)
		maxwords = *(ft_long *) getfld(f2, &sz);
	else
		maxwords = 0;
	if ((f1->type & DDTYPEBITS) != FTN_CHAR)
		if ((f1->type & DDTYPEBITS) != FTN_BYTE)
		{
			return -1;
		}
	text = getfld(f1, &sz);
	mmr = keywords(text, maxwords, globalcp);
	f1->type = FTN_CHAR | DDVARBIT;
	f1->kind = TX_FLD_NORMAL;
	f1->elsz = 1;
	setfld(f1, mmr, strlen(mmr));
	f1->n = f1->size = strlen(mmr);
	return 0;
}

/******************************************************************/
/*
 *
 */

int
TXsqlFunc_separator(f1)
FLD	*f1;		/* (in/out) arg/return value */
/* Returns separator character of strlst value `f1'.
 */
{
	static CONST char	fn[] = "TXsqlFunc_separator";
	ft_char			*retData;
	ft_strlst		slHdr;

	if ((f1->type & DDTYPEBITS) != FTN_STRLST)
		return -1;

	TXgetStrlst(f1, &slHdr);		/* get separator char */

	/* Set return value in `f1': */
	retData = (ft_char *)calloc(1, sizeof(ft_char) + 1);
	if (retData == (ft_char *)NULL)
	{
		TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, 1,
				 sizeof(ft_char) + 1);
		return(FOP_ENOMEM);
	}
	*retData = (ft_char)slHdr.delim;
	f1->type = (FTN_CHAR | DDVARBIT);
	f1->elsz = sizeof(ft_char);
	setfldandsize(f1, retData, (slHdr.delim == '\0' ? 0 : sizeof(ft_char))
				+ 1, FLD_FORCE_NORMAL);

	return 0;
}

int
TXsqlFunc_length(f1, f2)
FLD	*f1;		/* (in/out) FTN_CHAR field to modify */
FLD	*f2;		/* (in, opt.) FTN_CHAR field with mode */
{
	static CONST char	fn[] = "TXsqlFunc_length";
	char			*src, *modeStr, *srcEnd;
	size_t			srcLen, n;
	ft_long			*retData;
	TXCFF			mode, defMode;
	ft_strlst		slHdr;

	switch (f1->type & DDTYPEBITS)
	{
	case FTN_CHAR:
		break;				/* handled below */
	case FTN_STRLST:
		if (f2 != FLDPN) goto wrongNumArgs;
		/* KNG 20081113 return number of strings: */
		src = TXgetStrlst(f1, &slHdr);
		srcEnd = src + slHdr.nb - 1;	/* -1: w/o list terminator */
		for (n = 0; src < srcEnd; n++)
		{
			/* Advance `src' over current string: */
			for ( ; src < srcEnd && *src != '\0'; src++);
			if (src < srcEnd) src++;
		}
		goto setRet;
	case FTN_BLOB:
	case FTN_BLOBI:
	case FTN_INDIRECT:
		/* wtf read the blob/indirect? */
		putmsg(MERR + UGE, fn, "Unsupported data type %s",
			ddfttypename(f1->type));
		return(FOP_EINVAL);
	default:
		if (f2 != FLDPN)
		{
		wrongNumArgs:
			putmsg(MERR + UGE, fn,
			"Wrong number of arguments to function: Expected 1 arg if given non-char arg");
			return(FOP_EINVAL);
		}
		/* KNG 20081113 return number of elements: */
		src = (char *)getfld(f1, &srcLen);
		if (src == CHARPN || TX_SIZE_T_VALUE_LESS_THAN_ZERO(srcLen))
			goto nullData;
		n = srcLen;
		goto setRet;
	}

	src = (char *)getfld(f1, &srcLen);
	if (src == CHARPN || TX_SIZE_T_VALUE_LESS_THAN_ZERO(srcLen))
	{
	nullData:
		/* Just treat NULL data as 0 items? */
		n = 0;
		goto setRet;
	}
	defMode = globalcp->stringcomparemode;
	mode = defMode;

	/* Get optional mode: */
	if (f2 != FLDPN &&
	    (modeStr = (char *)getfld(f2, SIZE_TPN)) != CHARPN &&
	    *modeStr != '\0')
	{
		if (!TXstrToTxcff(modeStr, CHARPN, globalcp->textsearchmode,
				  globalcp->stringcomparemode, defMode, 1,
				  defMode, &mode))
		{				/* invalid mode string */
			putmsg(MERR + UGE, fn, "Invalid fold mode `%s'",
			       modeStr);
			/* TXstrToTxcff() still sets `mode' to best guess */
		}
	}

	/* Compute strlen: */
	if (mode & TXCFF_ISO88591)
		n = strlen(src);
	else
	{
		n = (size_t)(-1);
		TXunicodeGetUtf8CharOffset(src, CHARPN, &n);
	}

setRet:
	/* Set return value in `f1': */
	retData = (ft_long *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(ft_long) + 1);
	if (!retData) return(FOP_ENOMEM);
	*retData = (ft_long)n;
	/* Bug 8000: set return field properly: */
	if (!TXsqlSetFunctionReturnData(__FUNCTION__, f1, retData, FTN_LONG,
					FTI_UNKNOWN, sizeof(ft_long),
					1, 0))
		return(FOP_EUNKNOWN);

	return(FOP_EOK);
}

int
TXsqlFunc_ifNull(FLD *testFld, FLD *valIfNullFld)
/* SQL function ifNull(testFld, valIfNullFld): returns `testFld' if
 * not NULL, else `valIfNullFld' cast to `testFld' type.  NOTE: return
 * type must correspond with TXgetIfNullFuncReturnType().
 * Returns FOP_EOK (0) on success, else FOP_E... error (less than 0).
 */
{
	FLDOP	*fldop = NULL;
	int	ret;
	FLD	*resultFld = NULL, testFldLenOne;
	size_t	len;

	if (!TXfldIsNull(testFld)) goto ok;	/* return `testFld' */

	/* `testFld' is NULL; cast `valIfNullFld' to `testFld'.
	 * But use a copy that is at least length 1, so cast fits
	 * (e.g. convert of long(1) to double(0) would fail):
	 */
	getfld(testFld, &len);
	testFldLenOne = *testFld;
	if (len < 1)
	{
		testFldLenOne.n = 1;
		testFldLenOne.size = testFldLenOne.elsz;
	}
	if (!(fldop = TXgetFldopFromCache())) goto err;
	if ((ret = fopush(fldop, valIfNullFld)) != FOP_EOK) goto finally;
	if ((ret = fopush(fldop, &testFldLenOne)) != FOP_EOK) goto finally;
	if ((ret = foop(fldop, FOP_CNV)) != FOP_EOK) goto finally;
	if (!(resultFld = fopop(fldop))) goto err;

	/* Move `resultFld' data to `testFld' to return it: */
	if (!TXfldMoveFld(testFld, resultFld)) goto err;
ok:
	ret = FOP_EOK;
	goto finally;

err:
	ret = FOP_EUNKNOWN;
finally:
	resultFld = closefld(resultFld);
	fldop = TXreleaseFldopToCache(fldop);
	return(ret);
}

int
TXsqlFunc_isNull(FLD *testFld)
/* SQL function isNull(testFld): SQL-returns long 1 if NULL, 0 if not.
 * Returns FOP_EOK (0) on success, else FOP_E... error (less than 0).
 */
{
	static const char	fn[] = "TXsqlFunc_isNull";
	ft_long			longVal[2];

	longVal[0] = TXfldIsNull(testFld);
	longVal[1] = 0;				/* nul-terminate */
	if (!TXsqlSetFunctionReturnData(fn, testFld, longVal, FTN_LONG,
					FTI_UNKNOWN, sizeof(ft_long), 1, 1))
		return(FOP_EUNKNOWN);
	return(FOP_EOK);
}

/******************************************************************/
/*
 *
 */
int
TXsqlFunc_seq(f1, f2)
FLD *f1, *f2;
{
	long inc = 1;
	long *l;

	if (f1)
	{
		l = getfld(f1, NULL);
		if (l)
			inc = *l;
	}
	if (f2)
	{
		l = getfld(f2, NULL);
		if (l)
			TXseq = *l;
	}
	fld2finv(f1, TXseq);
	TXseq += inc;
	return 0;
}


/******************************************************************/
/*	Wrapper for strtol().  Mainly for parsing octal strings, since
 *	convert() of string to int-type explicitly does not.
 *	Returns 0 on success and -1 on failure.
 */
int
TXsqlFunc_strtol(f1, f2)
FLD	*f1, *f2;
{
	int	base = 0;
	void	*v;
	ft_long	*vp;

	if ((vp = (ft_long *)calloc(1, sizeof(ft_long))) == (ft_long *)NULL)
	{
		putmsg(MERR + MAE, "fstrtol", strerror(ENOMEM));
		return(-1);
	}
	if (f2 && (v = getfld(f2, NULL)) != NULL)
		base = (int)(*(ft_int *)v);
	if (f1 && (v = getfld(f1, NULL)) != NULL)
		*vp = (ft_long)strtol((char *)v, CHARPPN, base);
	f1->type = FTN_LONG;
	f1->kind = TX_FLD_NORMAL;
	f1->elsz = sizeof(ft_long);
	setfld(f1, vp, 1);
	f1->n = 1;
	return 0;
}


/******************************************************************/
/*	Wrapper for strtoul().  Mainly for parsing octal strings, since
 *	convert() of string to int-type explicitly does not.
 *	Returns 0 on success and -1 on failure.
 */
int
TXsqlFunc_strtoul(f1, f2)
FLD	*f1, *f2;
{
	int		base = 0;
	void		*v;
	ft_dword	*vp;

	if ((vp = (ft_dword *)calloc(1, sizeof(ft_dword))) == (ft_dword*)NULL)
	{
		putmsg(MERR + MAE, "fstrtoul", strerror(ENOMEM));
		return(-1);
	}
	if (f2 && (v = getfld(f2, NULL)) != NULL)
		base = (int)(*(ft_int *)v);
	if (f1 && (v = getfld(f1, NULL)) != NULL)
		*vp = (ft_dword)strtoul((char *)v, CHARPPN, base);
	f1->type = FTN_DWORD;
	f1->kind = TX_FLD_NORMAL;
	f1->elsz = sizeof(ft_dword);
	setfld(f1, vp, 1);
	f1->n = 1;
	return 0;
}


/******************************************************************/
/*	This function takes a pointer to a file name and returns a
 *	pointer to the data it contains.
 *	Returns 0 on success and -1 on failure.
 */

int
TXsqlFunc_fromfile(f1, f2, f3)
FLD *f1;			/* Holds File Name */
FLD *f2;			/* Holds offset. */
FLD *f3;			/* Holds length */
{
	char *fname, *saveFname = NULL;
	FILE *fh = NULL;
	EPI_OFF_T sz;
	long off = 0L;
	EPI_STAT_S statb;
	size_t readsz;
	int	ret;

	if (((f1->type & DDTYPEBITS) != FTN_CHAR) &&
	    ((f1->type & DDTYPEBITS) != FTN_INDIRECT))
		goto err;
	fname = getfld(f1, NULL);
	EPI_STAT(fname, &statb);
	fh = fopen(fname, "rb");
	if (fh == (FILE *) NULL)
	{
		putmsg(MERR + FOE, "fromfile", "Unable to open `%s': %s",
		       fname, TXstrerror(TXgeterror()));
		goto err;
	}
	saveFname = TXstrdup(TXPMBUFPN, __FUNCTION__, fname);
	freeflddata(f1);
	sz = statb.st_size;
	if (f2)
	{
		off = *(ft_long *) getfld(f2, NULL);
		if ((off > sz) || fseek(fh, off, SEEK_SET))
		{
			putmsg(MERR + UGE, "fromfile",
			       "Offset %wd beyond end of file `%s'",
			       (EPI_HUGEINT)off, saveFname);
			goto err;
		}
	}
	if (f3)
	{
		sz = *(ft_long *) getfld(f3, NULL);
	}
	if (sz > off + statb.st_size)
	{
		if (f2 && f3)
			putmsg(MERR + UGE, "fromfile", "Length too big");
		sz = statb.st_size - off;
	}
	if ((EPI_HUGEUINT) sz > (EPI_HUGEUINT) (EPI_OS_SIZE_T_MAX - 1))
	{
		putmsg(MWARN + UGE, "fromfile", "Length too big, truncating");
		readsz = EPI_OS_SIZE_T_MAX - 1;
	}
	else
	{
		readsz = (size_t) sz;
	}
	fname = TXmalloc(TXPMBUFPN, __FUNCTION__, readsz + 1);
	if (!fname) goto merr;
	if (fread(fname, 1, readsz, fh) != readsz)
	{
		fname = TXfree(fname);
		putmsg(MERR + FRE, __FUNCTION__,
		       "Cannot read %wd bytes from `%s': %s",
		       (EPI_HUGEINT)readsz, saveFname,
		       TXstrerror(TXgeterror()));
	merr:
		ret = FOP_ENOMEM;
		goto finally;
	}
	fname[readsz] = '\0';
	f1->type = FTN_BYTE | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, fname, readsz + 1, FLD_FORCE_NORMAL);
	ret = FOP_EOK;
	goto finally;

err:
	ret = FOP_EINVAL;
finally:
	if (fh)
	{
		fclose(fh);
		fh = NULL;
	}
	saveFname = TXfree(saveFname);
	return(ret);
}

/******************************************************************/
/*	This function takes a pointer to a file name and returns a
 *	pointer to the data it contains.
 *	Returns 0 on success and -1 on failure.
 */
int
TXsqlFunc_fromfiletext(f1, f2, f3)
FLD *f1;			/* Holds File Name */
FLD *f2;			/* Holds offset. */
FLD *f3;			/* Holds length */
{
	char *fname, *saveFname = NULL;
	FILE *fh = NULL;
	EPI_OFF_T sz;
	long off = 0L;
	EPI_STAT_S statb;
	size_t readsz;
	int	ret;

	if (((f1->type & DDTYPEBITS) != FTN_CHAR) &&
	    ((f1->type & DDTYPEBITS) != FTN_INDIRECT))
		goto err;
	fname = getfld(f1, NULL);
	EPI_STAT(fname, &statb);
	fh = fopen(fname, "rb");
	if (fh == (FILE *) NULL)
	{
		putmsg(MERR + FOE, "fromfiletext", "Unable to open `%s': %s",
		       fname, TXstrerror(TXgeterror()));
		goto err;
	}
	saveFname = TXstrdup(TXPMBUFPN, __FUNCTION__, fname);
	freeflddata(f1);
	sz = statb.st_size;
	if (f2)
	{
		off = *(ft_long *) getfld(f2, NULL);
		if ((off > sz) || fseek(fh, off, SEEK_SET))
		{
			putmsg(MERR + UGE, "fromfiletext",
			       "Offset %wd beyond end of file `%s'",
			       (EPI_HUGEINT)off, saveFname);
			goto err;
		}
	}
	if (f3)
	{
		sz = *(ft_long *) getfld(f3, NULL);
	}
	if (sz > off + statb.st_size)
	{
		if (f2 && f3)
			putmsg(MERR + UGE, "fromfiletext", "Length too big");
		sz = statb.st_size - off;
	}
	if ((EPI_HUGEUINT) sz > (EPI_HUGEUINT) (EPI_OS_SIZE_T_MAX - 1))
	{
		putmsg(MWARN + UGE, "fromfiletext",
		       "Length too big, truncating");
		readsz = EPI_OS_SIZE_T_MAX - 1;
	}
	else
	{
		readsz = (size_t) sz;
	}
	fname = TXmalloc(TXPMBUFPN, __FUNCTION__, readsz + 1);
	if (!fname)
	{
		ret = FOP_ENOMEM;
		goto finally;
	}
	if (fread(fname, 1, readsz, fh) != readsz)
	{
		fname = TXfree(fname);
		putmsg(MERR + FRE, __FUNCTION__,
		       "Cannot read %wd bytes from `%s': %s",
		       (EPI_HUGEINT)readsz, saveFname,
		       TXstrerror(TXgeterror()));
		goto err;
	}
	fname[readsz] = '\0';
	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, fname, readsz + 1, FLD_FORCE_NORMAL);
	ret = FOP_EOK;
	goto finally;

err:
	ret = FOP_EINVAL;
finally:
	if (fh)
	{
		fclose(fh);
		fh = NULL;
	}
	saveFname = TXfree(saveFname);
	return(ret);
}

/******************************************************************/

static char *mime2text ARGS((CGISL *hdrs, char *src, size_t sz));
static char *
mime2text(hdrs, src, sz)
CGISL	*hdrs;	/* (in) optional headers   NULL: parse from `src' */
char	*src;	/* (in) nul-terminated body (or headers + body) */
size_t	sz;	/* (in) size of `src' */
/* Parses MIME buffer `fc' and returns text.  May write to (but not
 * permanently modify) `src'.
 * Returns NULL on error, else alloced text.
 */
{
	TXPMBUF	*pmbuf = TXPMBUFPN;
	MIME	*mime = NULL;
	char	*h, *ret;
	CGISL	*parms = CGISLPN, *parms2 = CGISLPN;
	char	**sp, ch, *data, *subPart;
	CONST char	*e;
	size_t	n;
	HTBUF	*buf = NULL;

	h = src;

	mime = TXmimeOpen(HTPFOBJPN, hdrs, CHARPN, src, src + sz, CHARPN, 0);
	if (!mime) goto err;
	h = (char *)TXmimeGetNextPart(mime);	/* get non-MIME alt. body */
	if (!TXmimeIsMultipartMsg(mime))
	{					/* everything past headers */
		ret = TXstrdup(pmbuf, __FUNCTION__, (h ? h : ""));
		goto finally;
	}

	/* Parse the body for each MIME part; add parts w/filenames: */
	if (!(buf = openhtbuf())) goto err;
	for (data = (char *)TXmimeGetNextPart(mime);
	     data != CHARPN;
	     data = (char*)TXmimeGetNextPart(mime))
	{
		n = TXmimeGetPartBodySize(mime);
		parms = TXmimeGetPartHeaders(mime, 1);
		if ((sp = getcgisl(parms,"Content-Disposition")) != CHARPPN &&
		    cgiparsehdr(NULL, *sp, &e, &parms2) != CHARPN &&
		    parms2 != CGISLPN &&
		    (sp = getcgisl(parms2, "filename")) != CHARPPN)
		{
			ch = data[n];
			data[n] = '\0';			/* temp terminate */
			subPart = mime2text(parms, data, n);
			data[n] = ch;			/* restore */
			/* KNG 2010-10-12 suppress filename if it is likely
			 * a temp name from anytotx:
			 */
			htbuf_pf(buf, "%s\n\n%s\003\n",
				(strncmp(*sp, "cvto", 4) == 0 ? "" : *sp),
				 (subPart ? subPart : ""));
			subPart = TXfree(subPart);
		}
		else
			htbuf_pf(buf, "\n\n%.*s\003\n", n, data);
		parms = closecgisl(parms);
		parms2 = closecgisl(parms2);
	}
	htbuf_getdata(buf, &ret, 0x3);
	if (!ret) ret = TXstrdup(pmbuf, __FUNCTION__, "");
	goto finally;

err:
	ret = NULL;
finally:
	mime = TXmimeClose(mime);
	buf = closehtbuf(buf);
	return(ret);
}

/******************************************************************/

int
TXsqlFunc_totext(f1, f2)
FLD *f1;
FLD *f2;
{
	FLD *fc = NULL;		/* Field to hold the command. */
	char tbuf[2048];
	int nargs, rc = 0, i;
	char	*d, *tbufe = tbuf + sizeof(tbuf), *res, *s;
	EPI_HUGEINT val;

	if (!TXlic_getshmval(LICVAL_TX_FLAGS, &val, CHARPN))
		goto bad;
	if (val & TX_LIC_NO_ANYTOTX)
	{
		TXlic_noanytotx();
	      bad:
		rc = FOP_EILLEGAL;
		res = TXstrdup(TXPMBUFPN, __FUNCTION__, "");
		goto setRes;
	}

#ifdef _WIN32
	rc =(TXINSTALLPATH_VAL[strcspn(TXINSTALLPATH_VAL," \t\r\n\v\f")]?1:0);
	if (rc) tbuf[0] = '"';
	TXstrncpy(tbuf + rc, TXINSTALLPATH_VAL, sizeof(tbuf) - 10);
	strcat(tbuf, PATH_SEP_S "anytotx");
	if (rc) strcat(tbuf, "\"");
#else /* !_WIN32 */
	TXstrncpy(tbuf, TXINSTALLPATH_VAL, sizeof(tbuf) - 12);
	strcat(tbuf, PATH_SEP_S "bin" PATH_SEP_S "anytotx");
#endif /* !_WIN32 */
	d = tbuf + strlen(tbuf);
	/* Tack on global command-line args, eg. --install-dir: */
	for (i = 0; i < TxGlobalOptsArgc; i++)
	{
		s = TxGlobalOptsArgv[i];
#ifdef _WIN32
		rc = (s[strcspn(s, " \t\r\n\v\f")] != '\0' ? 1 : 0);
#endif /* _WIN32 */
		if (d < tbufe - 1) *(d++) = ' ';
#ifdef _WIN32
		if (rc && d < tbufe - 1) *(d++) = '"';
#endif /* _WIN32 */
		TXstrncpy(d, s, tbufe - d);
		d += strlen(d);
#ifdef _WIN32
		if (rc && d < tbufe - 1) *(d++) = '"';
#endif /* _WIN32 */
	}
	if (f2)			/* OPTIONAL arg to anyto tx *//* JMT 1999-08-27 */
	{
		if (d < tbufe - 1) *(d++) = ' ';
		TXstrncpy(d, getfld(f2, NULL), tbufe - d);
		d += strlen(d);
	}
	if ((f1->type & DDTYPEBITS) == FTN_INDIRECT)
	{
		if (d < tbufe - 1) *(d++) = ' ';
		TXstrncpy(d, getfld(f1, NULL), tbufe - d);
		d += strlen(d);
		nargs = 1;
	}
	else
	{
		char *fname;
		char *t;
		size_t sz;

		fname = getfld(f1, NULL);
		for (sz = 0, t = fname; *t && sz < 1025; t++, sz++);
		if (sz < 1024 && access(fname, R_OK) == -1)
		{
			nargs = 2;
		}
		else
		{
			if (d < tbufe - 2) { *(d++) = ' '; *(d++) = '"'; }
			TXstrncpy(d, getfld(f1, NULL), tbufe - d);
			d += strlen(d);
			if (d < tbufe - 1) *(d++) = '"';
			*d = '\0';
			nargs = 1;
		}
	}
	fc = createfld("varchar", 50, 1);
	putfld(fc, tbuf, strlen(tbuf));
	switch (nargs)
	{
	case 1:
		doshell(fc, NULL, NULL, NULL, NULL);
		break;
	case 2:
		doshell(fc, f1, NULL, NULL, NULL);
		break;
	default:
		rc = FOP_EINVAL;
		goto finally;
	}
	res = fldtostr(fc);
	if (res != CHARPN)
		res = mime2text(CGISLPN, res, strlen(res));
	if (res == CHARPN)
		res = TXstrdup(TXPMBUFPN, __FUNCTION__, "");
	if (res == CHARPN)
	{
		rc = FOP_ENOMEM;
		goto finally;
	}

setRes:
	if (!TXsqlSetFunctionReturnData(__FUNCTION__, f1, res, FTN_varCHAR,
					FTI_UNKNOWN, sizeof(ft_char),
					strlen(res), 0))
		rc = FOP_ENOMEM;
finally:
	fc = closefld(fc);
	return rc;
}

/******************************************************************/

int
TXsqlFunc_random(f1, f2)
FLD *f1;
FLD *f2;
{
	ft_long *m;
	long rv = 0L;
	static int init = 0;

	if (!init)
	{
		if (f2)
		{
			m = (ft_long *)getfld(f2, NULL);
			if (m)
				rv = *m;
		}
		else
		{
			/* KNG 20090924 use TXgettimeofday() instead of time()
			 * for more randomness, with TXgetpid() for good
			 * measure:
			 */
			rv = (long)(((double)1000000.0)*
				fmod(TXgettimeofday(), 1000.0)) +
				((long)TXgetpid(0) << 20);
		}
		TX_SRANDOM(rv);
		init++;
	}
	rv = TX_RANDOM();
	if (f1)
	{
		m = (ft_long *)getfld(f1, NULL);
		if (m && *m)
			rv %= *m;
	}
	fld2finv(f1, rv);
	return 0;
}

/******************************************************************/

#define fxxxxx TXacos
#define  xxxxx  acos
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXasin
#define  xxxxx  asin
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXatan
#define  xxxxx  atan
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXatan2
#define  xxxxx  atan2
#include "math2.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXcos
#define  xxxxx  cos
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXsin
#define  xxxxx  sin
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXtan
#define  xxxxx  tan
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXcosh
#define  xxxxx  cosh
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXsinh
#define  xxxxx  sinh
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXtanh
#define  xxxxx  tanh
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXexp
#define  xxxxx  exp
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXlog
#define  xxxxx  log
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXlog10
#define  xxxxx  log10
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXsqrt
#define  xxxxx  sqrt
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXceil
#define  xxxxx  ceil
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXfabs
#define  xxxxx  fabs
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXfloor
#define  xxxxx  floor
#include "math.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXfmod
#define  xxxxx  fmod
#include "math2.pro"
#undef fxxxxx
#undef xxxxx

#define fxxxxx TXpow
#define  xxxxx  pow
#include "math2.pro"
#undef fxxxxx
#undef xxxxx

/******************************************************************/

int
TXsqlFuncs_abstract(f1, f2, f3, f4, f5)
FLD *f1;
FLD *f2;
FLD *f3;
FLD *f4;
FLD *f5;
{
	ft_char *text, *rc, *query = CHARPN;
	int	queryAlloced = 0;
	ft_long *pmaxsz, maxsz;
	ft_long style;
	void	*data;
	DBTBL *tbl;
	size_t sz;
	TXPMBUF	*pmbuf = TXPMBUFPN;

	if (!f1 || !(text = getfld(f1, NULL)))
	{
		/* Returning "NULL" fld for NULL input lets SQL proceed,
		 * e.g. Bug 7789:
		 */
		rc = TXstrdup(pmbuf, __FUNCTION__, TXfldGetNullOutputString());
		goto dupRc;
	}

	if (f2 && (pmaxsz = getfld(f2, NULL)) != NULL)
		maxsz = *pmaxsz;
	else
		maxsz = 0;

	if (f3 && (data = getfld(f3, NULL)) != NULL)
		style = TXstrToAbs((char *)data);
	else
		style = TXABS_STYLE_SMART;

	if (f4)
	{
		query = TXfldToMetamorphQuery(f4);
		queryAlloced = 1;
	}

	if (f5)
	{
		tbl = getfld(f5, NULL);
	}
	else
		tbl = NULL;

	/* wtf cannot get index exprs/locale args due to FLD math limit of 5,
	 * but they may be set already in DDMMAPI of `tbl', which
	 * findrankabs() will drill out:
	 */
	rc = abstract(text, maxsz, style, query, tbl, CHARPPN, CHARPN);
dupRc:
	sz = strlen(rc);
	setfldandsize(f1, rc, sz + 1, FLD_FORCE_NORMAL);
	if (queryAlloced) free(query);
	return(FOP_EOK);
}

/******************************************************************/

static int txFuncDoStrFold ARGS((FLD *f1, FLD *f2, TXCFF defCaseStyle));
static int
txFuncDoStrFold(f1, f2, defCaseStyle)
FLD	*f1;		/* (in/out) FTN_CHAR field to modify */
FLD	*f2;		/* (in, opt.) FTN_CHAR field with mode */
TXCFF	defCaseStyle;	/* (in) default case style */
/* Guts of lower(), upper(), initcap().
 */
{
	static CONST char	fn[] = "txFuncDoStrFold";
	char			*src, *dest, *modeStr;
	size_t			srcLen, destSz, res;
	TXCFF			mode, defMode;

	src = (char *)getfld(f1, &srcLen);
	if (src == CHARPN || srcLen <= 0) return(0);	/* nothing to do */
	defMode = globalcp->stringcomparemode;
	if (defCaseStyle != (TXCFF)(-1))
		defMode = TXCFF_SUBST_CASESTYLE(defMode, defCaseStyle);
	mode = defMode;

	/* Get optional mode: */
	if (f2 != FLDPN &&
	    (modeStr = (char *)getfld(f2, SIZE_TPN)) != CHARPN &&
	    *modeStr != '\0')
	{
		if (!TXstrToTxcff(modeStr, CHARPN, globalcp->textsearchmode,
				  globalcp->stringcomparemode, defMode,
				/* If `defCaseStyle' set, we are parsing
				 * a "custom" setting, not stringcomparemode:
				 */
				(defCaseStyle != (TXCFF)(-1) ? -1 : 1),
				defMode, &mode))
		{				/* invalid mode string */
			putmsg(MERR + UGE, fn, "Invalid fold mode `%s'",
			       modeStr);
			/* TXstrToTxcff() still sets `mode' to best guess */
		}
	}

	dest = CHARPN;
	destSz = srcLen + (srcLen >> 5) + 2;	/* guess; add some slop */
	do
	{
		if (dest != CHARPN) free(dest);
		if ((dest = (char *)malloc(destSz)) == CHARPN)
		{
			TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn, destSz, 1);
			return(FOP_ENOMEM);
		}
		res = TXunicodeStrFold(dest, destSz, src, srcLen, mode);
		destSz += (destSz >> 2) + 4;	/* inc for next pass */
	}
	while (res == (size_t)(-1));

	setfldandsize(f1, dest, res + 1, FLD_FORCE_NORMAL);
	return 0;
}

/******************************************************************/

int
TXsqlFunc_upper(FLD *f1, FLD *f2)
{
	return(txFuncDoStrFold(f1, f2, TXCFF_CASESTYLE_UPPER));
}

/******************************************************************/

int
TXsqlFunc_lower(FLD *f1, FLD *f2)
{
	return(txFuncDoStrFold(f1, f2, TXCFF_CASESTYLE_LOWER));
}

/******************************************************************/

int
TXsqlFunc_initcap(FLD *f1, FLD *f2)
{
	return(txFuncDoStrFold(f1, f2, TXCFF_CASESTYLE_TITLE));
}

/******************************************************************/

char **VXsandr ARGS((char **, char **, char **));

int
TXsqlFunc_sandr(f1, f2, f3)
FLD *f1;
FLD *f2;
FLD *f3;
{
	char *srch[2], *repl[2];
	char *in[2], **out;
	ft_char *rc;
	size_t sz;

	srch[0] = srch[1] = NULL;
	repl[0] = repl[1] = NULL;
	in[0] = in[1] = NULL;
	if (f1)
		srch[0] = getfld(f1, NULL);
	else
		return -1;

	if (f2)
		repl[0] = getfld(f2, NULL);
	else
		return -1;

	if (f3)
		in[0] = getfld(f3, NULL);
	else
		return -1;

	out = VXsandr(srch, repl, in);
	if (out && out[0])
	{
		rc = out[0];
		sz = strlen(rc);
		setfldandsize(f1, rc, sz + 1, FLD_FORCE_NORMAL);
	}
	if (out)
		free(out);
	return 0;
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_pathcmp(f1, f2)
FLD	*f1, *f2;
/* SQL function wrapper for TXpathcmp(a, b): behaves like strcmp() but for
 * file paths, ie. is case-insensitive and considers / == \ for Windows.
 */
{
	static CONST char	fn[] = "TXsqlFunc_pathcmp";
	char	*a, *b;
	ft_long	*vp;

	if ((vp = (ft_long *)calloc(1, sizeof(ft_long))) == (ft_long *)NULL)
	{
		putmsg(MERR + MAE, fn, OutOfMem,
			TXstrerror(TXgeterror()));
		return(FOP_ENOMEM);
	}
	if (!f1 || (a = (char *)getfld(f1, NULL)) == CHARPN ||
	    !f2 || (b = (char *)getfld(f2, NULL)) == CHARPN)
	{
		putmsg(MERR + UGE, fn, "Missing value(s)");
		return(FOP_EINVAL);
	}

	*vp = (ft_long)TXpathcmp(a, (size_t)(-1), b, (size_t)(-1));
	f1->type = FTN_LONG;
	f1->kind = TX_FLD_NORMAL;
	f1->elsz = sizeof(ft_long);
	setfld(f1, vp, 1);
	f1->n = 1;
	return 0;
}

/* ------------------------------------------------------------------------ */

int
TXfld_canonpath(f1, f2)
FLD	*f1;
FLD	*f2;
/* SQL function wrapper for TXcanonpath(a): canonicalize file path,
 * fully-qualifying and removing symlinks.  2nd arg is bit flags (default 0):
 *   bit 0:  error messages
 *   bit 1:  return empty on error instead of given path
 */
{
	static CONST char	fn[] = "TXfld_canonpath";
	char	*a;
	char	*vp;
	ft_long	*b;
	int	flags = 0;

	if (!f1 || (a = (char *)getfld(f1, NULL)) == CHARPN)
	{
		putmsg(MERR + UGE, fn, "Missing value(s)");
		return(FOP_EINVAL);
	}
	if (f2 && (b = (ft_long *)getfld(f2, NULL)) != (ft_long *)NULL)
		flags = (int)(*b);
	vp = TXcanonpath(a, (flags & 1));
	if (vp == CHARPN &&			/* failed */
	    (vp = strdup((flags & 2) ? "" : a)) == CHARPN)
	{
		putmsg(MERR + MAE, fn, OutOfMem, TXstrerror(TXgeterror()));
		return(FOP_ENOMEM);
	}
	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, vp, strlen(vp) + 1, FLD_FORCE_NORMAL);
	return 0;
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_basename(f1)
FLD	*f1;
/* SQL function wrapper for TXbasename(): returns base filename of `f1',
 * i.e. everything after the last dir separator.  Modeled after Python.
 */
{
	static CONST char	fn[] = "TXsqlFunc_basename";
	char			*src;
	char			*baseName;

	if (!f1 || (src = (char *)getfld(f1, NULL)) == CHARPN)
	{
		putmsg(MERR + UGE, fn, "Missing value");
		return(FOP_EINVAL);
	}
	baseName = TXbasename(src);
	if (!(baseName = TXstrdup(TXPMBUFPN, fn, baseName)))
		return(FOP_ENOMEM);

	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, baseName, strlen(baseName) + 1, FLD_FORCE_NORMAL);
	return(0);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_dirname(f1)
FLD	*f1;
/* SQL function wrapper for TXdirname(): returns directory part of
 * `f1'.  Will not contain trailing slash, unless needed as leading
 * slash.  May be empty.  Modeled after Python.
 */
{
	static CONST char	fn[] = "TXsqlFunc_dirname";
	char			*src, *e, *s;
	char			*dirName;

	if (!f1 || (src = (char *)getfld(f1, NULL)) == CHARPN)
	{
		putmsg(MERR + UGE, fn, "Missing value");
		return(FOP_EINVAL);
	}

	/* We parallel Python os.path.dirname() behavior: return
	 * before the basename, with trailing path sep stripped:
	 */
	e = TXbasename(src);
	s = src;
	/* Per Python, leave trailing separator(s) iff part of root
	 * dir or drive:
	 */
#ifdef _WIN32
	if (((*s >= 'a' && *s <= 'z') ||
	     (*s >= 'A' && *s <= 'Z')) &&
	    s[1] == ':')
		s += 2;				/* save drive letter */
#endif /* _WIN32 */
	while (TX_ISPATHSEP(*s)) s++;		/* save root separator(s) */
	while (e > s &&				/* trim trail separator(s) */
	       TX_ISPATHSEP(e[-1]))
		e--;

	if (!(dirName = TXstrndup(TXPMBUFPN, fn, src, e - src)))
		return(FOP_ENOMEM);

	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, dirName, (e - src) + 1, FLD_FORCE_NORMAL);
	return(0);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_fileext(f1)
FLD	*f1;
/* SQL function wrapper for TXfileext(): returns file extension of `f1',
 * starting with the `.'.
 */
{
	static CONST char	fn[] = "TXsqlFunc_fileext";
	char			*src;
	char			*fileExt;

	if (!f1 || (src = (char *)getfld(f1, NULL)) == CHARPN)
	{
		putmsg(MERR + UGE, fn, "Missing value");
		return(FOP_EINVAL);
	}
	fileExt = TXfileext(src);
	if (!(fileExt = TXstrdup(TXPMBUFPN, fn, fileExt)))
		return(FOP_ENOMEM);

	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, fileExt, strlen(fileExt) + 1, FLD_FORCE_NORMAL);
	return(0);
}

/* ------------------------------------------------------------------------ */

static int TXsqlFunc_joinpathActual ARGS((int flags, FLD *f1, FLD *f2,
					  FLD *f3, FLD *f4, FLD *f5));
static int
TXsqlFunc_joinpathActual(flags, f1, f2, f3, f4, f5)
int	flags;
FLD	*f1;
FLD	*f2;
FLD	*f3;
FLD	*f4;
FLD	*f5;
/* Guts of joinpath() and joinpathabsolute().
 */
{
	char	*joinedPath = NULL, *part;
	char	**partList = NULL;
	size_t	partListLen = 0, partListAlloced = 0;
	size_t	fldIdx, numFlds, itemLen;
	FLD	*fldList[5];
	void	*itemPtr;

	fldIdx = 0;
	if (f1) fldList[fldIdx++] = f1;
	if (f2) fldList[fldIdx++] = f2;
	if (f3) fldList[fldIdx++] = f3;
	if (f4) fldList[fldIdx++] = f4;
	if (f5) fldList[fldIdx++] = f5;
	numFlds = fldIdx;

	/* Convert all data to strings and put into array `partList': */
	for (fldIdx = 0; fldIdx < numFlds; fldIdx++)
	{
		itemPtr = NULL;
		itemLen = 0;
		while ((part = TXfldGetNextItemStr(fldList[fldIdx], &itemPtr,
						   &itemLen)) != CHARPN)
		{
			if (!TX_INC_ARRAY(TXPMBUFPN, &partList,
					  partListLen, &partListAlloced))
			{
				partList = TXfree(partList);
				return(FOP_ENOMEM);
			}
			partList[partListLen++] = part;
		}
	}

	joinedPath = TXjoinpath(TXPMBUFPN, flags, partList, partListLen);
	partList = TXfree(partList);
	if (!joinedPath) return(FOP_ENOMEM);

	f1->type = FTN_CHAR | DDVARBIT;
	f1->elsz = 1;
	setfldandsize(f1, joinedPath, strlen(joinedPath) + 1, FLD_FORCE_NORMAL);
	return(0);
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_joinpath(f1, f2, f3, f4, f5)
FLD	*f1;
FLD	*f2;
FLD	*f3;
FLD	*f4;
FLD	*f5;
/* SQL function wrapper for TXjoinpath(NULL, 0x0).
 */
{
	return(TXsqlFunc_joinpathActual(0x0, f1, f2, f3, f4, f5));
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_joinpathabsolute(f1, f2, f3, f4, f5)
FLD	*f1;
FLD	*f2;
FLD	*f3;
FLD	*f4;
FLD	*f5;
/* SQL function wrapper for TXjoinpath(NULL, 0x1).
 */
{
	return(TXsqlFunc_joinpathActual(0x1, f1, f2, f3, f4, f5));
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_isNaN(f1)
FLD	*f1;
/* SQL function isNaN(x): returns 1 if x is NaN, 0 if not.
 * Use instead of x == NaN, since IEEE defines NaN == NaN as false.
 */
{
	static CONST char	fn[] = "TXfunc_isNaN";
	void			*a;
	ft_double		*dp;
	ft_float		*fp;
	ft_int			*vp;
	size_t			i;

	/* calloc(2): 1 val + nul-term.: */
	if ((vp = (ft_int *)calloc(2, sizeof(ft_int))) == (ft_int *)NULL)
	{
		putmsg(MERR + MAE, fn, OutOfMem,
			TXstrerror(TXgeterror()));
		return(FOP_ENOMEM);
	}
	*vp = 0;
	if (f1 && (a = (void *)getfld(f1, NULL)) != NULL)
	{
		switch (f1->type & DDTYPEBITS)
		{
		case FTN_FLOAT:
			fp = (ft_float *)a;
			for (i = 0; i < f1->n; i++, fp++)
			{
				/* wtf do not copy value, even to a local var;
				 * some platforms (x86_64 linux2.6.9-64-64)
				 * modify copy such that TXDOUBLE_IS_NaN()
				 * fails:
				 */
				if (TXFLOAT_IS_NaN(*fp))
				{
					*vp = 1;
					break;
				}
			}
			break;
		case FTN_DOUBLE:
			dp = (ft_double *)a;
			for (i = 0; i < f1->n; i++, dp++)
			{
				/* wtf do not copy value, even to a local var;
				 * some platforms (x86_64 linux2.6.9-64-64)
				 * modify copy such that TXDOUBLE_IS_NaN()
				 * fails:
				 */
				if (TXDOUBLE_IS_NaN(*dp))
				{
					*vp = 1;
					break;
				}
			}
			break;
		default:			/* no NaNs */
			break;
		}
	}
	/* Set `*vp' as return value: */
	f1->type = FTN_INT;
	f1->elsz = sizeof(ft_int);
	setfldandsize(f1, vp, sizeof(ft_int) + 1, FLD_FORCE_NORMAL);
	return 0;				/* success */
}

/* ------------------------------------------------------------------------ */

int
TXsqlFunc_hasFeature(FLD *featureFld)
/* SQL return is 1 if varchar `featureFld' token is supported, 0 if not
 * or unknown.
 * Returns FOP_OK on success, else negative value (FOP_E... code) on error.
 */
{
  TXPMBUF       *pmbuf = TXPMBUFPN;
  char          *feature;
  int           ret, res;
  ft_int        *intData = NULL;
  size_t        idx;

  feature = (char *)TXsqlGetFunctionArgData(pmbuf, __FUNCTION__, featureFld,
					    FTN_CHAR, FTI_UNKNOWN, NULL);
  if (!feature) goto err;

  intData = (ft_int *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_int));
  if (!intData)
    {
      ret = FOP_ENOMEM;
      goto finally;
    }

  *intData = 0;
  for (idx = 0; TXFeatures[idx]; idx++)
    if (strcmpi(TXFeatures[idx], feature) == 0)
      {
        *intData = 1;
        break;
      }

  res = TXsqlSetFunctionReturnData(__FUNCTION__, featureFld, intData, FTN_INT,
                                   FTI_UNKNOWN, sizeof(ft_int), 1, 0);
  intData = NULL;
  if (!res) goto err;
  ret = FOP_EOK;                                /* success */
  goto finally;

err:
  ret = FOP_EUNKNOWN;
finally:
  intData = TXfree(intData);
  return(ret);
}

/******************************************************************/

/******************************************************************/

#include "fldFuncsSorted.c"
#define NFLDFUNCS	TX_ARRAY_LEN(TXdbfldfuncs)

/******************************************************************/
/*	Get a FLDOP.  This function creates a FLDOP with the
 *	handlers required for Texis.
 *	Returns a pointer to the FLDOP struct.
 */

FLDOP *
dbgetfo()
{
	FLDOP *fo;

	fo = foopen();
	if (fo == FLDOPPN) goto err;
	if (foaddfuncs(fo, (FLDFUNC *)TXdbfldfuncs, NFLDFUNCS) != 0) goto err;
	adduserfuncs(fo);
#undef I
#define I(type1, type2, func)	\
		fosetop(fo, (type1), (type2), func, &o_##func);
TX_TEXIS_EXTRA_FLDOP_SYMBOLS_LIST
#undef I
	return fo;
err:
	return(foclose(fo));
}

FLDOP *
TXgetFldopFromCache()
/* Opens a new FLDOP, or reuses one if available, and returns it.
 * Release with TXreleaseFldopToCache() when done.
 */
{
	FLDOP	*fldop;

	if (TXApp == TXAPPPN || TXApp->fldopCacheSz <= 0)
		return(dbgetfo());		/* nothing in the cache */

	/* We have an unsed FLDOP in the cache; return it: */
	fldop = TXApp->fldopCache[--TXApp->fldopCacheSz];
	TXApp->fldopCache[TXApp->fldopCacheSz] = FLDOPPN;
	while (fodisc(fldop) == 0);		/* clear the stack */
	return(fldop);
}

FLDOP *
TXreleaseFldopToCache(FLDOP *fldop)
/* Releases a FLDOP previously allocated via TXgetFldopFromCache(),
 * for later reuse.
 */
{
	if (fldop == FLDOPPN) return(FLDOPPN);

	if (TXApp == TXAPPPN ||
	    TXApp->fldopCacheSz >= (size_t)TX_FLDOP_CACHE_MAX_SZ)
		return(foclose(fldop));		/* no room in cache */

	TXApp->fldopCache[TXApp->fldopCacheSz++] = fldop;
	return(FLDOPPN);
}

char *
TXdbtblSchemaToStr(dbtbl, orderToo)
CONST DBTBL	*dbtbl;		/* (in) DBTBL to get schema from */
int		orderToo;	/* (in) 1: DESC etc. too 2: ASC too */
/* Prints schema for `dbtbl' in human-readable, as-created order,
 * eg. `table(column1 type(size), column2 type(size), ...)\n'.
 * Returns alloced string.
 */
{
	char	*schemaStr = NULL, *ret;

	schemaStr = TXddSchemaToStr(dbtbl->tbl->dd, orderToo);
	if (!schemaStr) return(NULL);
	ret = TXstrcatN(TXPMBUFPN, __FUNCTION__, dbtbl->lname, schemaStr,
			NULL);
	schemaStr = TXfree(schemaStr);
	return(ret);
}

char *
TXdbtblTupleToStr(DBTBL *dbtbl)
/* Prints current `dbtbl' tuple (row), as `table(col, col, ...)'.
 * Returns alloced string.
 */
{
	char	*tupleStr = NULL, *ret;

	tupleStr = TXtblTupleToStr(dbtbl->tbl);
	if (!tupleStr) return(NULL);
	ret = TXstrcatN(TXPMBUFPN, __FUNCTION__, dbtbl->lname, tupleStr,
			NULL);
	tupleStr = TXfree(tupleStr);
	return(ret);
}
