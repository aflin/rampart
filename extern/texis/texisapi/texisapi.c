#include "texint.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <signal.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef EPI_HAVE_PROCESS_H
#  include <process.h>
#endif
#include <errno.h>
#ifndef _WIN32
#  include <sys/time.h>
#  include <sys/resource.h>
#endif

#ifdef DEBUG
#define DEBUGA(a)
#else
#define DEBUGA(a)
#endif

/******************************************************************/

typedef struct HPARAM
{
	UWORD ipar;
	SWORD fctype;
	SWORD fsqltype;
	UDWORD cbcoldef;
	SWORD ibscale;
	void *rgbvalue;
	SDWORD *pcbvalue;
	struct HPARAM *next;
	struct HPARAM *prev;
}
HPARAM;
static HPARAM *params = NULL;

/**
 * EXtract the low-level DDIC struct from a TEXIS struct
 *
 * @param TEXIS *tx
 * @return DDIC *
 */
DDIC *
texis_getddic(TEXIS *tx)
{
	return((tx && tx->hdbc && ((LPDBC)tx->hdbc)->ddic) ?
		((LPDBC)tx->hdbc)->ddic : (DDIC *)NULL);
}

#ifdef THUNDERSTONE
/*
 *      This is part of the connection code for texis network code
 int ipar;		   (in) SQL parameter # (counting from 1)
 int ctype;		   (in) SQL_C_... type
 int sqltype;		   (in) SQL_... type
 long coldef;		   (in) precision?  passed to SQLSetParam()
 int scale;		   (in) scale?  passed to SQLSetParam()
 void *value;		   (in) parameter data (will be copied)
 long *length;		   (in) byte length of `value'
 */

int
_setparam(int ipar, int ctype, int sqltype, long coldef, int scale, void *value, long *length)
{
	HPARAM *p;
	int found = 0;

	for (p = params; p; p = p->next)	/* MAW 10-07-94 - replace if already there */
	{
		if (p->ipar == ipar)
		{
			found = 1;
			break;
		}
	}
	if (found)		/* MAW 10-24-94 - alloc and cpy data here instead of higher up */
	{			/* so can free later */
		p->rgbvalue = realloc(p->rgbvalue, *length ? (size_t) * length : 1);
	}
	else
	{
		p = (HPARAM *) calloc(1, sizeof(HPARAM));
		p->pcbvalue = (long *) calloc(1, sizeof(long));

		p->rgbvalue = malloc(*length ? (size_t) * length : 1);
	}
	memcpy(p->rgbvalue, value, (size_t) * length);
	p->ipar = ipar;
	p->fctype = ctype;
	p->fsqltype = sqltype;
	p->cbcoldef = coldef;
	p->ibscale = scale;
	*p->pcbvalue = *length;	/* MAW 10-07-94 - remember val, not ptr */

	if (!found)
	{
		if (!params)
		{
			p->next = NULL;
			p->prev = NULL;
			params = p;
		}
		else
		{
			p->next = params;
			p->prev = NULL;
			params->prev = p;
			params = p;
		}
	}
	return 0;
}
#endif
/**********************************************************************/

static void cleanparams (void);

static void
cleanparams()
{
	HPARAM *p;

	for (; params; params = p)	/* MAW 09-30-94 - cleanup on close */
	{
		p = params->next;
		free(params->rgbvalue);
		free(params->pcbvalue);
		free(params);
	}
	params = NULL;
}				/* end cleanparams() */
/**********************************************************************/

extern APICP *globalcp;
static void cleanntexis ARGS((TEXIS * tx));
static int dotraps = 1;		/**< MAW 06-22-94 - control signal trapping in ntexis() */

#define NTXSV 16		/**< MAW 10-12-94 - remember more than 1 */
static TEXIS *globaltx[NTXSV];	/**< for cleanup() */
static int ngtx = 0;		/**< number of above in use */

static SIGTYPE CDECL cleanup (int);

/**********************************************************************/
static SIGTYPE CDECL
cleanup(int sig)
{
	int o;
	TXEXIT		ret = TXEXIT_ABEND;
	CONST char	*what = "ABEND";

#ifdef SIGINT
	signal(SIGINT, _exit);
#endif
#ifdef unix
	signal(SIGQUIT, _exit);
	signal(SIGHUP, _exit);
	signal(SIGTERM, _exit);
	signal(SIGSEGV, _exit);
	signal(SIGABRT, _exit);
#ifdef SIGBUS
	signal(SIGBUS, _exit);
#endif
#endif /* unix */

#if defined(SIGINT) || defined (SIGQUIT) || defined(SIGTERM)
	switch (sig)
	{
#ifdef SIGINT
	case SIGINT:
#endif
#ifdef SIGQUIT
	case SIGQUIT:
#endif
#if defined(SIGINT) || defined(SIGQUIT)
		what = "user interrupt";
		ret = TXEXIT_USERINTERRUPT;
		break;
#endif
#ifdef SIGTERM
	case SIGTERM:
		what = "terminated";
		ret = TXEXIT_TERMINATED;
		break;
#endif /* SIGTERM */
	}
#endif

	o = epilocmsg(1);
	putmsg(MERR, (char *) NULL, "Texis daemon (%d) %s: signal %d",
	       getpid(), what, sig);
	epilocmsg(0);
	/* wtf - adjust cgisql if the text of this msg changes */
	putmsg(MERR, (char *) NULL, "Texis daemon (%d) %s: signal %d",
	       getpid(), what, sig);
        TXcallabendcbs();       /* these should be "safe" callbacks */
	epilocmsg(o);
#ifdef NEVER			/* We are exiting anyway, can only cause more trouble */
	for (i = 0; i < ngtx; i++)
		texis_close(globaltx[i]);
	closeapicp(globalcp);
#endif
	exit(ret);				/* TXEXIT_... code */
}
/**********************************************************************/
static void CDECL dotimeouts (int);
static void traps (int on);
static void
traps(int on)
{
	static SIGTYPE(CDECL * oldint) ARGS((int));
	static SIGTYPE(CDECL * oldquit) ARGS((int));
	static SIGTYPE(CDECL * oldhup) ARGS((int));
	static SIGTYPE(CDECL * oldterm) ARGS((int));
	static SIGTYPE(CDECL * oldsegv) ARGS((int));
	static SIGTYPE(CDECL * oldabrt) ARGS((int));
	static SIGTYPE(CDECL * oldbus) ARGS((int));
	static SIGTYPE(CDECL * oldusr2) ARGS((int));
	static SIGTYPE(CDECL * oldxfsz) ARGS((int));
	static SIGTYPE(CDECL * oldxcpu) ARGS((int));

	if (!dotraps)
		return;
	if (on)
	{
#ifdef SIGINT
		oldint = signal(SIGINT, cleanup);
#endif
#ifdef unix
		oldquit = signal(SIGQUIT, cleanup);
		oldhup = signal(SIGHUP, cleanup);
		oldterm = signal(SIGTERM, cleanup);
		oldsegv = signal(SIGSEGV, cleanup);
		oldabrt = signal(SIGABRT, cleanup);
#ifdef SIGBUS
		oldbus = signal(SIGBUS, cleanup);
#endif
#ifdef SIGUSR2
		oldusr2 = signal(SIGUSR2, dotimeouts);
#endif
#ifdef SIGXFSZ
                oldxfsz = signal(SIGXFSZ, SIG_IGN);
#endif /* SIGXFSZ */
#ifdef SIGXCPU
                oldxcpu = signal(SIGXCPU, SIG_IGN);
#endif /* SIGXCPU */
#endif /* unix */
	}
	else
	{
#ifdef SIGINT
		signal(SIGINT, oldint);
#endif
#ifdef unix
		signal(SIGQUIT, oldquit);
		signal(SIGHUP, oldhup);
		signal(SIGTERM, oldterm);
		signal(SIGSEGV, oldsegv);
		signal(SIGABRT, oldabrt);
#ifdef SIGBUS
		signal(SIGBUS, oldbus);
#endif
#ifdef SIGUSR2
		signal(SIGUSR2, oldusr2);
#endif
#ifdef SIGXFSZ
                signal(SIGXFSZ, oldxfsz);
#endif /* SIGXFSZ */
#ifdef SIGXCPU
                signal(SIGXCPU, oldxcpu);
#endif /* SIGXCPU */
#endif /* unix */
	}
}				/* end traps() */
/**********************************************************************/
/** Add to global list of open handles
 */
static void
addgtx(TEXIS *tx)			/* MAW 10-12-94 - remember open texi */
{
	if (ngtx == NTXSV)
		return;
	globaltx[ngtx] = tx;
	ngtx++;
	if (ngtx == 1)
		traps(1);	/* turn on signal traps on first one */
}				/* end addgtx() */
/**********************************************************************/
static void rmgtx (TEXIS * tx);
static void
rmgtx(TEXIS *tx)			/* MAW 10-12-94 - forget closed texi */
{
	int i;

	for (i = ngtx - 1; i >= 0; i--)
	{
		if (globaltx[i] == tx)
		{
			ngtx--;
			for (; i < ngtx; i++)
				globaltx[i] = globaltx[i + 1];
			if (ngtx == 0)
			{
				traps(0);	/* turn off signal traps when none */
				cleanparams();
			}
			break;
		}
	}
}				/* end rmgtx() */
/**********************************************************************/

/**********************************************************************/
int
ntexistraps(f)			/* MAW 06-22-94 */
int f;
{
	dotraps = f;
	return 0;
}
/**********************************************************************/

/**********************************************************************/
/**
 *	Get result names.  This function takes a statement, and builds
 *	a list of field names.
 *
 *	@param TEXIS *tx
 *	@return a NULL terminated string list.
 */

char **
texis_getresultnames(TEXIS *tx)
{
	unsigned i;
	int j = 0;
	TBL *tbl;
	char *name, *sname, *tname;
	LPSTMT lpstmt = (LPSTMT) tx->hstmt;
	char **names;

	if (!lpstmt->outtbl)
		return NULL;
	tbl = lpstmt->outtbl->tbl;
	names = (char **) calloc(tbl->n + 1, sizeof(char *));

	for (
		    i = 0;
		    i < tbl->n;
		    ++i
		)
	{
		name = getfldname(tbl, i);
		if (!strstr(name, ".$recid"))
		{
			sname = strrchr(name, '.');
			if (sname != (char *) NULL)
			{
                          tname = dbnametoname(lpstmt->outtbl, sname + 1,
					       FTNPN, INTPN);
				if (tname != (char *) NULL)
					name = sname + 1;
			}
			names[j] = name;
			j++;
		}
	}
	names[j] = "";
	return names;
}
/**********************************************************************/

/**********************************************************************/
/**
 *	Get result sizes.  This function takes a statement, and builds
 *	a list of field sizes.
 *
 *	@param TEXIS *tx
 *	@return a -1 terminated integer list.
 */

int *
texis_getresultsizes(TEXIS *tx)
{
	unsigned i;
	int j = 0, n;
	TBL *tbl;
	char *name;
	LPSTMT lpstmt = (LPSTMT) tx->hstmt;
	int *fldsizes;
	DD *dd;

	if (!lpstmt->outtbl)
		return NULL;
	tbl = lpstmt->outtbl->tbl;
	dd = tbl->dd;
	fldsizes = (int *) calloc(tbl->n + 1, sizeof(int));

	for (
		    i = 0;
		    i < tbl->n;
		    ++i
		)
	{
		name = getfldname(tbl, i);
		if (!strstr(name, ".$recid"))
		{
			n = ddgetorign(dd, i);
			if (!TX_SIZE_T_VALUE_LESS_THAN_ZERO(dd->fd[n].elsz))
				fldsizes[j] = dd->fd[n].size / (int)dd->fd[n].elsz;
			else
				fldsizes[j] = (int)dd->fd[n].size;
			j++;
		}
	}
	fldsizes[j] = -1;
	return fldsizes;
}
/**********************************************************************/

/******************************************************************/

int
texis_getrowcount(TEXIS *tx)
{
	SDWORD lprc = -1;

	if (tx->hstmt)
		SQLRowCount(tx->hstmt, &lprc);
	return lprc;
}

/**********************************************************************/
static
int fldtofldlist(FLDLST *fl, int type, void *p, int n, char *name)
{
  if(fl->n==FLDLSTMAX) return(-1);
  fl->type[fl->n]=type;
  fl->data[fl->n]=p;
  fl->ndata[fl->n]=n;
	fl->name[fl->n]=name;
  fl->n+=1;
  return(1);
}
/**********************************************************************/
/*
 *      Dump current row to client.
 *
 *      This should take the current row from the query and dump it
 *      back to the client by calling the registered callback.
 *
 *      Returns the result of the callback.
 */

static int getrow (TEXIS *, int);

static int
getrow(TEXIS *tx, int istr)
{
	int rc, dostr = 0;
	unsigned i;
	FLD *f;
	TBL *tbl;
	char *fname;
	LPSTMT lpstmt = (LPSTMT) tx->hstmt;	/* Statment in which to find the current row */
	void *v;
	size_t sz;

	tbl = lpstmt->outtbl->tbl;
	cleanntexis(tx);
	for (
		    i = 0;
		    i < tbl->n;
		    ++i
		)
	{
		fname = getfldname(tbl, i);
		f = nametofld(tbl, fname);
		if (!strstr(fname, ".$recid"))
		{
			if (i == (unsigned)istr)
				dostr = 1;
			if (dostr)
			{
				tx->fld[i] = createfld("varchar", 20, 0);
				setfldv(tx->fld[i]);
				tx->fld[i]->type = FTN_CHAR + DDVARBIT;
				freeflddata(tx->fld[i]);
				fopush(tx->fo, f);
				fopush(tx->fo, tx->fld[i]);
				rc = foop(tx->fo, FOP_CNV);
				closefld(tx->fld[i]);
				tx->fld[i] = fopop(tx->fo);
#ifndef NO_TRY_FAST_CONV
				if(i >= (unsigned)tx->nfld)
					tx->nfld = i + 1;
#endif
				if (rc == (-1))
					fldtofldlist(&tx->fl, FTN_CHAR + DDVARBIT, fname, (int)strlen(fname) + 1, fname);
				else
				{
					v = getfld(tx->fld[i], &sz);
					fldtofldlist(&tx->fl, tx->fld[i]->type, v, sz, fname);
				}
			}
			else
			{
				v = getfld(f, &sz);
				fldtofldlist(&tx->fl, f->type, v, sz, fname);
			}
		}
	}
	return 1;
}
/**********************************************************************/
static void
cleanntexis(TEXIS *tx)
{
	int i;

#ifndef NO_TRY_FAST_CONV
	for (i = 0; i < tx->nfld; i++)
#else
	for (i = 0; i < DDFIELDS; i++)
#endif
	{
		if (tx->fld[i] != FLDPN)
			tx->fld[i] = closefld(tx->fld[i]);
	}
#ifndef NO_TRY_FAST_CONV
	tx->nfld = 0;
#endif
	tx->lastRetcode = SQL_SUCCESS;
	tx->fl.n = 0;
}				/* end cleanntexis() */
/**********************************************************************/

/**********************************************************************/

int
newproctexis(TEXIS *tx)
{
	return TXddicnewproc(texis_getddic(tx));
}

/**********************************************************************/

int
resetparamstexis(TEXIS *tx)
{
	if(!tx)
		return -1;
	if(tx->hstmt != (HSTMT) NULL)
	{
		if(SQLFreeStmt(tx->hstmt, SQL_RESET_PARAMS) == SQL_SUCCESS)
			return 0;
	}
	return -1;
}

/**********************************************************************/
/**
 *	Close a TEXIS handle and free all associated resources
 *
 *	@param TEXIS *tx the handle to close
 *	@result returns NULL
 */
TEXIS *
texis_close(TEXIS *tx)
{
	if (tx != (TEXIS *) NULL)
	{
		cleanntexis(tx);
		if (tx->hstmt != (HSTMT) NULL)
		{
			LPSTMT lpstmt = (LPSTMT) tx->hstmt;

			SQLFreeStmt(tx->hstmt, SQL_DROP);
		}
		if (tx->hdbc != (HDBC) NULL)
		{
			/* KNG 20050316 Windows MSC optimizer bug?
			 * if we don't make this volatile, then the
			 * refcount == 0 check below may fail, even though
			 * SQLFreeStmt() sets refcount to 0, because compiler
			 * assumes it's still 1 without actually checking.
			 * Debug builds or copious putmsg()s may cause the
			 * check to spontaneously work.  See Bugzilla #829.
			 * is this issue caused by "Assume No Aliasing" flag?
			 */
			VOLATILE LPDBC lpdbc = (LPDBC)tx->hdbc;

			if (lpdbc->refcount == 1)
			{
				DDIC	*ddic;
				ddic = texis_getddic(tx);
				if(ddic && ddic->ihstmt)
				{
					SQLFreeStmt(ddic->ihstmt, SQL_DROP);
					ddic->ihstmt = NULL;
				}
			}
			if (lpdbc->refcount == 0)
			{
				SQLDisconnect(tx->hdbc);
				SQLFreeConnect(tx->hdbc);
				tx->hdbc = NULL;
				if (tx->henv != (HENV) NULL)
				{
					SQLFreeEnv(tx->henv);
					tx->henv = NULL;
				}
			}
		}
		if (tx->fo != FLDOPPN)
			foclose(tx->fo);
		rmgtx(tx);
		free(tx);
	}
	return (TEXISPN);
}
/**********************************************************************/
/** initialize TEXIS struct to empty state
 *
 * @param tx the struct to initialize
 * @return void
 */
static void
TXinitTexisStruct(TEXIS *tx)

{
	tx->henv = (HENV) NULL;
	tx->hdbc = (HDBC) NULL;
	tx->hstmt = (HSTMT) NULL;
	tx->donullcb = 0;
	tx->fo = FLDOPPN;
	tx->lastRetcode = SQL_SUCCESS;
}

/**
 *	Duplicate an existing TEXIS handle, and assign new callbacks
 *
 *	@param void *se;			Server connection
 *	@param void *usr;			Clients data pointer for cb()
 *	@param void *cb;			Pointer to callback function
 *	@param TEXIS *origtx;	Handle we are duping
 *	@return TEXIS *new duplicated handle
 */
TEXIS *
texis_dup(TEXIS *origtx)
{
	TEXIS *tx;
	static CONST char Fn[] = "dupntexis";

	if ((tx = (TEXIS *) calloc(1, sizeof(TEXIS))) == TEXISPN)
		goto zmemerr;
	TXinitTexisStruct(tx);

	addgtx(tx);
	tx->henv = origtx->henv;	/* Share settings */
	tx->hdbc = origtx->hdbc;

	if (SQLAllocStmt(tx->hdbc, &tx->hstmt) != SQL_SUCCESS)
		goto zmemerr;
	if ((tx->fo = dbgetfo()) == FLDOPPN)
		goto zmemerr;
	return (tx);
      zmemerr:
	putmsg(MERR + MAE, Fn, strerror(ENOMEM));
	return (texis_close(tx));
}

/**********************************************************************/
/**
 * Simplified open of database
 *
 * @param char *database;	Path to the database
 * @param char *uid;			user name
 * @param char *passwd;		password
 * @return TEXIS * 				new handle, NULL on error
 */

TEXIS *
texis_open(char *database, char *user, char *password)
{
	if((user == (char *)NULL) || (*user == '\0'))
	{
		user = "PUBLIC";
	}
	return texis_open_options(NULL, NULL, NULL, database, user, NULL, password);
}
/**********************************************************************/
/**
 *	Open a TEXIS handle with all possible options
 *
 * @param void *se;					Server connection
 * @param void *usr;				Clients data pointer for cb()
 * @param void *cb;					Pointer to callback function
 * @param char *datasource;	Name of the database
 * @param char *uid;				user name
 * @param char *gid;				group name
 * @param char *passwd;			password
 * @return TEXIS * new handle, NULL on error
 */

TEXIS *
texis_open_options(void *se, void *usr, void *cb, char *datasource, char *uid, char *gid, char *passwd)
{
	TEXIS *tx;
	static CONST char Fn[] = "openntexis";

	if (uid == (char *) NULL)
		uid = "";
	if (gid == (char *) NULL)
		gid = "";
	if (passwd == (char *) NULL)
		passwd = "";
	if ((tx = (TEXIS *) calloc(1, sizeof(TEXIS))) == TEXISPN)
	{
		goto zmemerr;
	}
	TXinitTexisStruct(tx);
	addgtx(tx);

	if (SQLAllocEnv(&tx->henv) != SQL_SUCCESS)
	{
		goto zmemerr;
	}
	if (SQLAllocConnect(tx->henv, &tx->hdbc) != SQL_SUCCESS)
	{
		goto zmemerr;
	}
	if (SQLAllocStmt(tx->hdbc, &tx->hstmt) != SQL_SUCCESS)
	{
		goto zmemerr;
	}
	if (SQLConnect(tx->hdbc,
		       (UCHAR *) datasource, (SWORD)strlen(datasource),
		       (UCHAR *) uid, (SWORD)strlen(uid),
		       (UCHAR *) passwd, (SWORD)strlen(passwd)) != SQL_SUCCESS)
	{
		putmsg(MERR, Fn, "Could not connect to %s", datasource);
		goto zerr;
	}
	if ((tx->fo = dbgetfo()) == FLDOPPN)
		goto zmemerr;
	return (tx);
      zmemerr:
	putmsg(MERR + MAE, Fn, strerror(ENOMEM));
      zerr:
	return (texis_close(tx));
}
/**********************************************************************/
/**
 * Logoff existing user and login with new user credentials
 *
 * @param TEXIS *tx;		Existing database connnection
 * @param char *user;		New username to use
 * @param char *passwd;	New password to use
 * @return int 					0 on success, -1 on failure
**/

int
texis_logon(TEXIS *tx, char *user, char *passwd)
{
	DDIC *ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return -1;
	permslogoff(ddic);
	if (!user || !user[0])
		return permstexis(ddic, "PUBLIC", "");
	return permstexis(ddic, user, passwd);
}

/**********************************************************************/

int
texis_reset(TEXIS *tx)
{
	DDIC *ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return -1;
	TXresetproperties(ddic);
	return 0;
}

/**********************************************************************/
void
ncbntexis(TEXIS *tx, int f)
{
		tx->donullcb = f;
}
/**********************************************************************/

/**********************************************************************/
/** Prepare a SQL Statment
 *
 *	@param tx Previously opened TEXIS handle
 *	@param query SQL query, possibly containing parameters (?)
 *	@return 1 on success, 0 on failure
 */

int
texis_prepare(TEXIS *tx, char *query)
{
	int rc;
	static CONST char Fn[] = "texis_prepare";

	rc = SQLPrepare(tx->hstmt, (UCHAR *) query, (SDWORD)strlen(query));
	tx->lastRetcode = rc;
	if (rc != SQL_SUCCESS)
	{
		putmsg(MERR, Fn, "SQLPrepare() failed with %d: %s",
		       rc, TXsqlRetcodeToMessage(rc));
		rc = 0;
	}
	else
		rc = 1;
	return (rc);

}
/**********************************************************************/

int
texis_lockmode(TEXIS *tx, int type)
{
	DDIC	*ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return -1;
	return lockmode(ddic, type);
}

/**********************************************************************/

int
texis_lock(TEXIS *tx, char *table, int type)
{
	DDIC	*ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return -1;
	return locktable(ddic, table, type);
}

/******************************************************************/

int
texis_unlock(TEXIS *tx, char *table, int type)
{
	DDIC	*ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return -1;
	return unlocktable(ddic, table, type);
}

/**********************************************************************/

int
texis_resetparams(TEXIS *tx)
{
	int rc;

	if(!tx || !tx->hstmt)
		return 0;
	rc = SQLFreeStmt(tx->hstmt, SQL_RESET_PARAMS);
	return rc==SQL_SUCCESS?1:0;
}

/**********************************************************************/
/**	Set a parameter

 *	@param TEXIS *tx;		(in/out) TEXIS handle
 *	@param int ipar;			(in) SQL parameter # (counting from 1)
 *	@param void *buf;		(in) parameter data (will be copied)
 *	@param long *len;		(in) byte length of `buf'
 *	@param int ctype;		(in) SQL_C_... type
 *	@param int sqltype;	(in) SQL_... type
 *	@return int 0 on success, -1 on failure
**/
int
texis_param(TEXIS *tx, int ipar, void *buf, long *len, int ctype, int sqltype)
{
	int rc;
	static CONST char Fn[] = "paramntexis";

	rc = SQLSetParam(tx->hstmt, (UWORD) ipar, (SWORD) ctype,
			 (SWORD) sqltype, (UDWORD) * len,
			 (SWORD) * len, (PTR) buf,
			 (SDWORD *) len);
	tx->lastRetcode = rc;
	if (rc == SQL_SUCCESS)
		rc = 1;
	else
	{
		putmsg(MERR, Fn, "SQLSetParam(,%d,%d,%d,,,,) failed with %d: %s",
		       ipar, ctype, sqltype, rc, TXsqlRetcodeToMessage(rc));
		rc = 0;
	}
	return (rc);
}
/**********************************************************************/

/**********************************************************************/
int
texis_execute(TEXIS *tx)
{
	int rc;
	static CONST char Fn[] = "texis_execute";

	if (params)
	{
		HPARAM *cparam;

		cparam = params;
		while (cparam)
		{
			SQLSetParam(tx->hstmt, cparam->ipar, cparam->fctype,
				    cparam->fsqltype, cparam->cbcoldef,
				    cparam->ibscale, cparam->rgbvalue,
				    cparam->pcbvalue);
			/* wtf set tx->lastRetcode on error? */
			cparam = cparam->next;
		}
	}
	rc = SQLExecute(tx->hstmt);
	tx->lastRetcode = rc;
	if (rc != SQL_SUCCESS)
	{
		putmsg(MERR, Fn, "SQLExecute() failed with %d: %s",
		       rc, TXsqlRetcodeToMessage(rc));
		rc = 0;
	}
	else
		rc = 1;
	return (rc);
}
/**********************************************************************/

/**********************************************************************/
TEXIS *
texis_prepexec(TEXIS *tx, char *query)
{
	if (!texis_prepare(tx, query))
		return (TEXISPN);
	if (!texis_execute(tx))
		return (TEXISPN);
	return (tx);
}
/**********************************************************************/

/**********************************************************************/
FLDLST *
texis_fetch(TEXIS *tx, int istr)
{
	LPSTMT lpstmt = (LPSTMT) tx->hstmt;
	FLDLST *fl = &tx->fl;

	while ((tx->lastRetcode = SQLFetch(tx->hstmt)) == SQL_SUCCESS &&
		       getrow(tx, istr))
	{
		if (fl->n > 0)
		{
			return (fl);
		}
	}
	return (FLDLSTPN);	/* end/error */
}				/* end getntexis() */
/**********************************************************************/
/** Asynchronously cancels currently-running SQL transaction.
 * 	Thread-safe.  Signal-safe.
 *
 *	@param TEXIS *tx
 * 	@return 0 on error.
**/
int
texis_cancel(TEXIS *tx)
{
	return(tx && tx->hstmt && SQLCancel(tx->hstmt) == SQL_SUCCESS);
}

/**********************************************************************/
/* WTF: Return number rows or -1.
	Allow number to flush

	e.g. flushntexis(tx, 100)  for SKIP=
*/
int
texis_flush(TEXIS *tx)
{
	int rowcount = 0;

	rowcount = texis_flush_scroll(tx, -1);
	if(rowcount > 0)
		return rowcount;
	else if(rowcount < 0)
		return 0;
	else
		return 1;
}				/* end flushntexis() */

int
texis_flush_scroll(TEXIS *tx, int nrows)
{
	int rc = SQL_SUCCESS;
	int rowcount = 0;
	SDWORD lrowcount;
	int optskip;
	LPSTMT lpstmt;

	lpstmt = tx->hstmt;
	optskip = lpstmt->dbc->ddic->optimizations[OPTIMIZE_SKIPAHEAD];
#ifndef NEVER
	if( optskip && nrows > 0)
		tx->lastRetcode = rc = SQLFetchScroll(tx->hstmt, SQL_FETCH_RELATIVE, nrows);
	else
#endif
	while ((nrows < 0 || rowcount < nrows) &&
	       (tx->lastRetcode = rc = SQLFetch(tx->hstmt)) == SQL_SUCCESS)
		rowcount++;
#ifndef NEVER
	SQLRowCount(tx->hstmt, &lrowcount);
	rowcount = lrowcount;
#endif
	switch(rc)
	{
	case SQL_NO_DATA_FOUND:
		/* If we are trying to flush to the end this is not
		   an error.  If we are trying to skip some rows,
		   return an error to indicate we did not skip all
		   of them */
		if(nrows < 0)
			break;
	case SQL_ERROR:
		return (-1 - rowcount);
	}
#ifndef NEVER
	if (SQLRowCount(tx->hstmt, &lrowcount) == SQL_SUCCESS)
	{	/* wtf set tx->lastRetcode? */
		return lrowcount;
	}
#endif
	return rowcount;
}
/**********************************************************************/

int
texis_set(TEXIS *tx, char *property, char *value)
{
	LPSTMT lpstmt;
	DDIC *ddic;

	if(!tx)
	{
		return -1;
	}
	lpstmt = tx->hstmt;
	if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
	{
		ddic = lpstmt->dbc->ddic;
	}
	else
	{
		return -1;
	}
	return setprop(ddic, property, value);
}
/**********************************************************************/

static void CDECL
dotimeouts(int sig)
{
	int o, i;
	DDIC	*ddic;

	(void)sig;
	o = epilocmsg(1);
	putmsg(MWARN, (char *) NULL, "Texis Timeout");
	epilocmsg(0);
	putmsg(MWARN, (char *) NULL, "Texis Timeout");
	epilocmsg(o);
	for (i = 0; i < ngtx; i++)
	{
		ddic = texis_getddic(globaltx[i]);
		if (ddic)
			ddicsetstate(ddic, DDIC_STOPPING);
	}
}

/**********************************************************************/

ft_counter *
TXgettxcounter(TEXIS *tx)
{
	DDIC	*ddic;

	ddic = texis_getddic(tx);
	if (!ddic)
		return (ft_counter *)NULL;
	return getcounter(ddic);
}

/******************************************************************/

int
settxtimeout(int timeout)
{
	static int childpid = 0;

#if !defined(MSDOS)	/* WTF */
	pid_t parent;

	parent = getpid();
	if (childpid)
		kill(childpid, SIGKILL);
	if (timeout > 0 &&
	    (childpid = TXfork(TXPMBUFPN, NULL, NULL, 0x0)) == 0)
	{
		if (!TXsleepmsec(timeout*1000, 0))
			kill(parent, SIGUSR2);
		TXsleepmsec(3*1000, 0);
		kill(parent, SIGINT);
		exit(TXEXIT_OK);
	}
	return 1;
#endif
}

/******************************************************************/

EPI_HUGEUINT
getindcount()
{
	return TXindcnt;
}

/* ------------------------------------------------------------------------ */
/** Gets index/row counts
 *
 * @param	TEXIS				*tx;				(in)Texis handle to query
 * @param	TXCOUNTINFO	*countInfo;	(out) row counts
 * @return 0 on error, 1 on success.
 */
int
texis_getCountInfo(TEXIS *tx, TXCOUNTINFO *countInfo)
{
	return(TXsqlGetCountInfo(tx->hstmt, countInfo));
}

/* ------------------------------------------------------------------------ */

const char *
TXsqlRetcodeToToken(RETCODE retcode)
/* Returns SQL-standard `SQL_...' token for given retcode.
 */
{
	switch (retcode)
	{
	case SQL_INVALID_HANDLE:	return("SQL_INVALID_HANDLE");
	case SQL_ERROR:			return("SQL_ERROR");
	case SQL_SUCCESS:		return("SQL_SUCCESS");
	case SQL_SUCCESS_WITH_INFO:	return("SQL_SUCCESS_WITH_INFO");
	case SQL_STILL_EXECUTING:	return("SQL_STILL_EXECUTING");
	case SQL_NEED_DATA:		return("SQL_NEED_DATA");
	case SQL_NO_DATA_FOUND:		return("SQL_NO_DATA_FOUND");
	default:			return("unknown");
	}
}

/* ------------------------------------------------------------------------ */
/** Returns human-readable message for given retcode.
 *  May change if I18N support added or message(s) clarified;
 *  programs/scripts should check ...ToToken() value.
 */
const char *
TXsqlRetcodeToMessage(RETCODE retcode)
{
	switch (retcode)
	{
	case SQL_INVALID_HANDLE:
		return("An invalid SQL handle was used");
	case SQL_ERROR:
		return("An error occurred");
	case SQL_SUCCESS:
		return("The SQL function completed successfully");
	case SQL_SUCCESS_WITH_INFO:
		return("The SQL function completed successfully but with a nonfatal warning");
	case SQL_STILL_EXECUTING:
		return("The SQL function is still executing");
	case SQL_NEED_DATA:
		return("Needed parameters not supplied");
	case SQL_NO_DATA_FOUND:
		return("No more results available");
	default:
		return("Unknown return code");
	}
}

/* ------------------------------------------------------------------------ */

RETCODE
TXsqlGetRetcode(TEXIS *tx)
{
	return(tx->lastRetcode);
}
