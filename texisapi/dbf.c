/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#if defined(__MSDOS__) && !defined(MSDOS)
#  define MSDOS __MSDOS__
#endif
#include "stdio.h"
#include "stdlib.h"
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "os.h"
/* #include "mmsg.h" */
#define HAVE_TXERRNO
#include "texerr.h"
#include "dbquery.h"
#include "texint.h"
#include "fdbf.h"
#include "ramdbf.h"
#include "dbf.h"


int TXerrno = 0;

#ifdef HAVE_JDBF
#include "jdbf.h"

/************************************************************************/

int
initjdbf(dbf, fn, oflags)
DBF *dbf;
char *fn;
int oflags;

/* Inits function pointers and such.
 */
{
	dbf->obj = (void *) jdbf_open(fn, oflags);
	if (!dbf->obj)
		return 0;
	dbf->close = (void *(*)ARGS((void *))) jdbf_close;
	dbf->dbfree = (int (*)ARGS((void *, EPI_OFF_T))) jdbf_free;
	dbf->alloc = (EPI_OFF_T(*)ARGS((void *, void *, size_t))) jdbf_alloc;
	dbf->put = (EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t))) jdbf_put;
	dbf->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) jdbf_get;
	dbf->aget = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) jdbf_aget;
	dbf->read =
		(size_t(*)ARGS((void *, EPI_OFF_T, size_t *, void *, size_t)))
		jdbf_read;
	dbf->tell = (EPI_OFF_T(*)ARGS((void *))) jdbf_tell;
	dbf->getfn = (char *(*)ARGS((void *))) jdbf_getfn;
	dbf->getfh = (int (*)ARGS((void *))) jdbf_getfh;
	dbf->setoveralloc = (void (*)ARGS((void *, int))) jdbf_setoveralloc;
	dbf->valid = (int (*)ARGS((void *, EPI_OFF_T))) jdbf_valid;
#ifndef NO_DBF_IOCTL
	dbf->ioctl = (int (*)ARGS((void *, int, void *))) jdbf_ioctl;
	dbf->dbftype = DBF_JMT;
#endif
	return (1);
}

#endif /* HAVE_JDBF */

/************************************************************************/
#ifndef NO_KDBF
#include "kdbf.h"
static int
initkdbf(DBF *dbf, char *fn, int oflags)
/* Inits function pointers and such.
 */
{
	dbf->obj = (void *) kdbf_open(dbf->pmbuf, fn, oflags);
	if (!dbf->obj)
		return 0;
	dbf->close = (void *(*)ARGS((void *))) kdbf_close;
	dbf->dbfree = (int (*)ARGS((void *, EPI_OFF_T))) kdbf_free;
	dbf->alloc = (EPI_OFF_T(*)ARGS((void *, void *, size_t))) kdbf_alloc;
	dbf->put = (EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t))) kdbf_put;
	dbf->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) kdbf_get;
	dbf->aget = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) kdbf_aget;
	dbf->read =
		(size_t(*)ARGS((void *, EPI_OFF_T, size_t *, void *, size_t)))
		kdbf_read;
	dbf->tell = (EPI_OFF_T(*)ARGS((void *))) kdbf_tell;
	dbf->getfn = (char *(*)ARGS((void *))) kdbf_getfn;
	dbf->getfh = (int (*)ARGS((void *))) kdbf_getfh;
	dbf->setoveralloc = (void (*)ARGS((void *, int))) kdbf_setoveralloc;
	dbf->valid = (int (*)ARGS((void *, EPI_OFF_T))) kdbf_valid;
#ifndef NO_DBF_IOCTL
	dbf->ioctl = (int (*)ARGS((void *, int, void *))) kdbf_ioctl;
	dbf->dbftype = DBF_KAI;
#endif
	return (1);
}

#endif

/************************************************************************/

static int
initfdbf(DBF *df, char *fn)
{
	if ((df->obj = (void *) openfdbf(fn)) != (void *) NULL)
	{
		/* wtf pass on dbf->pmbuf to openfdbf(): */
		df->close = (void *(*)ARGS((void *))) closefdbf;
		df->dbfree = (int (*)ARGS((void *, EPI_OFF_T))) freefdbf;
		df->alloc =
			(EPI_OFF_T(*)ARGS((void *, void *, size_t))) fdbfalloc;
		df->put =
			(EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t)))
			putfdbf;
		df->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) getfdbf;
		df->aget =
			(void *(*)ARGS((void *, EPI_OFF_T, size_t *))) agetfdbf;
		df->read =
			(size_t(*)ARGS
			 ((void *, EPI_OFF_T, size_t *, void *, size_t)))
			readfdbf;
		df->tell = (EPI_OFF_T(*)ARGS((void *))) tellfdbf;
		df->getfn = (char *(*)ARGS((void *))) getfdbffn;
		df->getfh = (int (*)ARGS((void *))) getfdbffh;
		df->setoveralloc =
			(void (*)ARGS((void *, int))) setfdbfoveralloc;
		df->valid = (int (*)ARGS((void *, EPI_OFF_T))) validfdbf;
#ifndef NO_DBF_IOCTL
		df->ioctl = NULL;
		df->dbftype = DBF_FILE;
#endif
		return (1);
	}
	return (0);
}

/************************************************************************/

#ifdef HAVE_VDBF

#include "vdbf.h"

static int
initvdbf(df, fn)
DBF *df;
char *fn;
{
	/* wtf pass on dbf->pmbuf to openvdbf(): */
	if ((df->obj = (void *) openvdbf(fn)) != (void *) NULL)
	{
		df->close = (void *(*)ARGS((void *))) closevdbf;
		df->dbfree = (int (*)ARGS((void *, EPI_OFF_T))) freevdbf;
		df->alloc =
			(EPI_OFF_T(*)ARGS((void *, void *, size_t))) vdbfalloc;
		df->put =
			(EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t)))
			putvdbf;
		df->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) getvdbf;
		df->aget =
			(void *(*)ARGS((void *, EPI_OFF_T, size_t *))) agetvdbf;
		df->read =
			(size_t(*)ARGS
			 ((void *, EPI_OFF_T, size_t *, void *, size_t)))
			readvdbf;
		df->tell = (EPI_OFF_T(*)ARGS((void *))) tellvdbf;
		df->getfn = (char *(*)ARGS((void *))) getvdbffn;
		df->getfh = (int (*)ARGS((void *))) getvdbffh;
		df->setoveralloc =
			(void (*)ARGS((void *, int))) setvdbfoveralloc;
		df->valid = (int (*)ARGS((void *, EPI_OFF_T))) validvdbf;
#ifndef NO_DBF_IOCTL
		df->ioctl = NULL;
		df->dbftype = DBF_FILE;
#endif
		return (1);
	}
	return (0);
}
#endif

/************************************************************************/

static int initrdbf ARGS((DBF *));

static int
initrdbf(df)
DBF *df;
{
	/* wtf pass on dbf->pmbuf to openrdbf(): */
	if ((df->obj = (void *) openrdbf()) != (void *) NULL)
	{
		df->close = (void *(*)ARGS((void *))) closerdbf;
		df->dbfree = (int (*)ARGS((void *, EPI_OFF_T))) freerdbf;
		df->alloc =
			(EPI_OFF_T(*)ARGS((void *, void *, size_t))) rdbfalloc;
		df->put =
			(EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t)))
			putrdbf;
		df->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *))) getrdbf;
		df->aget =
			(void *(*)ARGS((void *, EPI_OFF_T, size_t *))) agetrdbf;
		df->read =
			(size_t(*)ARGS
			 ((void *, EPI_OFF_T, size_t *, void *, size_t)))
			readrdbf;
		df->tell = (EPI_OFF_T(*)ARGS((void *))) tellrdbf;
		df->getfn = (char *(*)ARGS((void *))) getrdbffn;
		df->getfh = (int (*)ARGS((void *))) getrdbffh;
		df->setoveralloc =
			(void (*)ARGS((void *, int))) setrdbfoveralloc;
		df->valid = (int (*)ARGS((void *, EPI_OFF_T))) validrdbf;
#ifndef NO_DBF_IOCTL
		df->ioctl = (int (*)ARGS((void *, int, void *))) ioctlrdbf;
#ifdef NEVER			/* This does not appear to be a good default */
		((RDBF *) df->obj)->dfover = df;
#endif
#endif
#ifndef NO_DBF_IOCTL
		df->dbftype = DBF_RAM;
#endif
		return (1);
	}
	return (0);
}

/************************************************************************/

int
TXinitNoOpDbf(DBF *df)
/* Returns 0 on error, 1 on success.
 */
{
	if ((df->obj = (void *)TXnoOpDbfOpen()) == NULL) return(0);

	TXnoOpDbfSetPmbuf((TXNOOPDBF *)df->obj, df->pmbuf);
	df->close = (void *(*)ARGS((void *)))TXnoOpDbfClose;
	df->dbfree = (int (*)ARGS((void *, EPI_OFF_T)))TXnoOpDbfFree;
	df->alloc = (EPI_OFF_T(*)ARGS((void *, void *, size_t)))TXnoOpDbfAlloc;
	df->put = (EPI_OFF_T(*)ARGS((void *, EPI_OFF_T, void *, size_t)))
		TXnoOpDbfPut;
	df->get = (void *(*)ARGS((void *, EPI_OFF_T, size_t *)))TXnoOpDbfGet;
	df->aget = (void *(*)ARGS((void *, EPI_OFF_T, size_t *)))
		TXnoOpDbfAllocGet;
	df->read = (size_t(*)ARGS ((void *, EPI_OFF_T, size_t *, void *,
				    size_t)))TXnoOpDbfRead;
	df->tell = (EPI_OFF_T(*)ARGS((void *)))TXnoOpDbfTell;
	df->getfn = (char *(*)ARGS((void *)))TXnoOpDbfGetFilename;
	df->getfh = (int (*)ARGS((void *)))TXnoOpDbfGetFileDescriptor;
	df->setoveralloc = (void (*)ARGS((void *, int)))TXnoOpDbfSetOverAlloc;
	df->valid = (int (*)ARGS((void *, EPI_OFF_T)))TXnoOpDbfBlockIsValid;
	df->ioctl = (int (*)ARGS((void *, int, void *)))TXnoOpDbfIoctl;
	df->dbftype = DBF_NOOP;
	return (1);
}

/************************************************************************/

DBF *
closedbf(df)
DBF *df;
{
	if (df != DBFPN)
	{			/* chech the pointer and the close function */
		if (df->obj != (void *) NULL
		    && df->close != (void *(*)ARGS((void *))) NULL)
			(*df->close) (df->obj);	/* close the obj */
		df->obj = (void *) NULL;	/* null it */
		df->pmbuf = txpmbuf_close(df->pmbuf);
		df = TXfree((void *) df);
	}
	return (DBFPN);
}

/************************************************************************/

DBF *
opendbf(pmbuf, fn, oflags)
TXPMBUF	*pmbuf;		/* (in, opt.) buffer to clone for messages */
char *fn;		/* (in, opt.) filename, or NULL/TXNOOPDBF_PATH */
int oflags;
{
	static CONST char	Fn[] = "opendbf";
	DBF *df = (DBF *) TXcalloc(pmbuf, Fn, 1, sizeof(DBF));

	if (df != DBFPN)
	{
		df->pmbuf = txpmbuf_open(pmbuf);	/* clone and attach */
		if (fn == TXNOOPDBF_PATH)
		{
			if (O_CREAT != (oflags & O_CREAT))
			{
				txpmbuf_putmsg(pmbuf, MWARN + UGE, Fn,
				  "Trying to open TXNOOPDBF without O_CREAT");
			}
			if (!TXinitNoOpDbf(df))
				return(closedbf(df));
		}
		else if (fn == (char *) NULL || *fn == '\0')
		{
			if (O_CREAT != (oflags & O_CREAT))
			{
				txpmbuf_putmsg(pmbuf, MWARN, Fn,
				     "Trying to open RAMDBF without O_CREAT");
			}
			if (!initrdbf(df))
				return (closedbf(df));
		}
		else
#ifdef HAVE_JDBF
		if(!initjdbf(df, fn, oflags))
#endif
#ifndef NO_KDBF
			if (!initkdbf(df, fn, oflags))
		{
			if((O_CREAT | O_EXCL) != (oflags & (O_CREAT | O_EXCL)))
			{
#endif
#ifdef HAVE_VDBF
				if (!initvdbf(df, fn))
#endif
					/* KNG 20130430 do not even bother
					 * with FDBF if disabled; just gives
					 * confusing `Probable corrupt KDBF'
					 * message, even if just perm-denied:
					 */
					if (!TXfdbfIsEnabled() ||
					    !initfdbf(df, fn))
						return (closedbf(df));
			}
			else
				return (closedbf(df));
		}
	}
	return (df);
}

/* ------------------------------------------------------------------------ */

int
TXdbfSetPmbuf(DBF *dbf, TXPMBUF *pmbuf)
/* Clones `pmbuf' and attaches to `dbf', closing previous buffer.
 * Returns 0 on error.
 */
{
	TXPMBUF	*pmbufOrg = pmbuf;

	pmbuf = txpmbuf_open(pmbuf);		/* clone `pmbuf' first */
	if (!pmbuf && pmbufOrg) return(0);
	dbf->pmbuf = txpmbuf_close(dbf->pmbuf);
	dbf->pmbuf = pmbuf;
	/* Pass `pmbuf' to subsidiary object too: */
	switch (dbf->dbftype)
	{
	case DBF_KAI:
		TXkdbfSetPmbuf(dbf->obj, pmbuf);
		break;
	/* wtf pass to other types too */
	}
	return(1);
}

/************************************************************************/
#ifndef NO_DBF_IOCTL

static int
TXdbfmakemeafile(DBF *dbf, void *data)
{
	static int no_kdbf_tmp = 0;
	RDBF *rdbf;
	void *v;
	size_t sz;
	EPI_OFF_T rc, ret = 0;

	DBGMSG(1, (999, NULL, "Making a file"));
	if(no_kdbf_tmp)
		return ret;
	rdbf = dbf->obj;
	initkdbf(dbf, NULL, (O_RDWR | O_CREAT | O_EXCL));
	if(!dbf->obj)
	{
		no_kdbf_tmp++;
		dbf->obj = rdbf;
		return ret;
	}
	v = getrdbf(rdbf, (EPI_OFF_T) 0, &sz);
	while (v)
	{
		rc = kdbf_put(dbf->obj, -1, v, sz);
		if (rc == -1)
			return -1;
		if ((void *) tellrdbf(rdbf) == data)
			ret = rc;
		v = getrdbf(rdbf, (EPI_OFF_T) - 1, &sz);
	}
	closerdbf(rdbf);
	return ret;
}

/******************************************************************/

int
ioctldbf(dbf, ioctl, data)
DBF *dbf;
int ioctl;
void *data;
{
	if (ioctl > 0x0000FFFF)
	{
		if (dbf->ioctl)
			return (*dbf->ioctl) (dbf->obj, ioctl, data);
	}
	switch (ioctl)
	{
	case DBF_MAKE_FILE:
		if (dbf->dbftype == DBF_RAM)
			return TXdbfmakemeafile(dbf, data);
	case DBF_AUTO_SWITCH:
		if (dbf->dbftype == DBF_RAM)
			((RDBF *) dbf->obj)->dfover = dbf;
	}
	return -1;
}

#endif /* NO_DBF_IOCTL */
/******************************************************************/
