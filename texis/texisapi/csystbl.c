/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "cgi.h"


/******************************************************************/

#ifndef NO_CACHE_TABLE
int
makevalidtable(ddic, tblid)
DDIC	*ddic;	/* (in/out) DDIC */
SYSTBL	tblid;	/* (in) table */
/* Ensures that SYS... table `tblid' cache in `ddic' exists and is up to date,
 * i.e. reflects any changes to actual disk table since the last time it
 * was read.
 * Returns 0 on success, -1 on error.
 */
{
	static CONST char Fn[] = "makevalidtable";
	TXTBLCACHE	**tc;
	TBL		*in;
	void		*rc;
	size_t		sz;
	int		v, tries = 0;
	CONST char	*sysTblName = CHARPN;

again:
	rc = NULL;
	sz = 0;
	v = 0;

	switch(tblid)
	{
		case SYSTBL_INDEX:
			tc = &ddic->indtblcache;
			in = ddic->indextbl;
			sysTblName = "SYSINDEX";
			break;
		case SYSTBL_TABLES:
			tc = &ddic->tbltblcache;
			in = ddic->tabletbl;
			sysTblName = "SYSTABLES";
			break;
		case SYSTBL_USERS:
			tc = &ddic->usrtblcache;
			in = ddic->userstbl;
			sysTblName = "SYSUSERS";
			break;
		case SYSTBL_PERMS:
			tc = &ddic->prmtblcache;
			in = ddic->permstbl;
			sysTblName = "SYSPERMS";
			break;
#ifdef HAVE_LINK_TABLES
		case SYSTBL_LINK:
			tc = &ddic->lnktblcache;
			in = ddic->linktbl;
			sysTblName = "SYSLINK";
			break;
#endif /* HAVE_LINK_TABLES */
		default:
			putmsg(MWARN, NULL, "Unknown tblid %d", (int)tblid);
			return -1;
	}
	if (tries++ > 10)
	{
		txpmbuf_putmsg(ddic->pmbuf, MERR, Fn,
			    "Failed to make %s table %s valid after %d tries",
			       ddic->epname, sysTblName, tries);
		return(-1);
	}
	if(!(*tc))
	{
		if(!in)
		{
			putmsg(MERR, Fn, "Table not opened");
			return -1;
		}
		*tc = (TXTBLCACHE *)TXcalloc(TXPMBUFPN, Fn,
					     1, sizeof(TXTBLCACHE));
		if (!(*tc)) return(-1);
		(*tc)->tbl = createtbl(in->dd, NULL);
#ifndef NO_DBF_IOCTL
		ioctldbf((*tc)->tbl->df, RDBF_SETOVER, NULL);
		ioctldbf((*tc)->tbl->df, RDBF_BLCK_LIMIT, 0);
		ioctldbf((*tc)->tbl->df, RDBF_SIZE_LIMIT, 0);
#endif /* !NO_DBF_IOCTL */
		if (TXlocksystbl(ddic, tblid, R_LCK, &((*tc)->lastr)) == -1)
			return(-1);		/* lock failed */
#ifdef TX_TBL_CACHE_DEBUG
		{
			char	ctrBuf[TX_COUNTER_PRBUFSZ];
			TXprintHexCounter(ctrBuf, sizeof(ctrBuf),
					  &(*tc)->lastr);
			putmsg(999, Fn,
				"(%u) DDIC %p caching %s at %s tbl=%p",
				(unsigned)TXgetpid(0), ddic,
				sysTblName, ctrBuf, (*tc)->tbl);
		}
#endif /* TX_TBL_CACHE_DEBUG */
		rewindtbl(in);
		rc = getdbf(in->df, -1, &sz);
/* WTF - I can't be bothered with fields */
		while(rc)
		{
			putdbf((*tc)->tbl->df, -1, rc, sz);
			rc = getdbf(in->df, -1, &sz);
		}
		TXunlocksystbl(ddic, tblid, R_LCK);
		switch(tblid)
		{
		case SYSTBL_INDEX:
			/* WTF Bug 3217 flush index in btcache etc. */
			(*tc)->flds[0] = nametofld((*tc)->tbl, "TBNAME");
			(*tc)->flds[1] = nametofld((*tc)->tbl, "FNAME");
			(*tc)->flds[2] = nametofld((*tc)->tbl, "FIELDS");
			(*tc)->flds[3] = nametofld((*tc)->tbl, "TYPE");
			(*tc)->flds[4] = nametofld((*tc)->tbl, "NON_UNIQUE");
			(*tc)->flds[5] = nametofld((*tc)->tbl, "NAME");
			(*tc)->flds[6] = nametofld((*tc)->tbl, "PARAMS");
			break;
		case SYSTBL_TABLES:
			/* Bug 3685: flush table cache too: table handles
			 * out of date; plus keeping them open may prevent
			 * full deletion (Windows):
			 */
			TXrmcache(ddic, CHARPN, INTPN);
			break;
		default:
			break;
		}
		return 0;
	}
	v = TXlocksystbl(ddic, tblid, V_LCK, &((*tc)->lastr));
#ifdef TX_TBL_CACHE_DEBUG
	{
		char	ctrBuf[TX_COUNTER_PRBUFSZ];
		TXprintHexCounter(ctrBuf, sizeof(ctrBuf), &(*tc)->lastr);
		putmsg(999, Fn,
		    "(%u) DDIC %p validating %s cache at %s: tbl=%p v=%d %s",
			(unsigned)TXgetpid(0), ddic, sysTblName,
			ctrBuf, (*tc)->tbl, v,
			(v == -2 || !(*tc)->tbl ? "re-reading" : "ok"));
	}
#endif /* TX_TBL_CACHE_DEBUG */

	if (v == -1) return(-1);

	/* Note if SYS... table has changed, for other uses later: */
	if (v == -2)				/* SYS... table changed */
		switch(tblid)
		{
		case SYSTBL_INDEX:
			TXddicSetSysindexChanged(ddic, 1);
			break;
		case SYSTBL_TABLES:
			TXddicSetSystablesChanged(ddic, 1);
			break;
		default:
			break;
		}

	if ((v == -2) || !(*tc)->tbl)		/*SYS... changed or no cache*/
	{
		*tc = TXtblcacheClose(*tc);
                goto again;
	}
	return 0;
}
/******************************************************************/
#endif /* !NO_CACHE_TABLE */

int
TXddicGetSystablesChanged(ddic)
DDIC	*ddic;
/* Returns nonzero if SYSTABLES change was detected, 0 if not.
 */
{
	return(ddic->systablesChanged);
}

int
TXddicSetSystablesChanged(ddic, yes)
DDIC	*ddic;
int	yes;
/* Sets SYSTABLES-changed flag to `yes'.
 * Returns 0 on error.
 */
{
	ddic->systablesChanged = !!yes;
	return(1);
}

int
TXddicGetSysindexChanged(ddic)
DDIC	*ddic;
/* Returns nonzero if SYSINDEX change was detected, 0 if not.
 */
{
	return(ddic->sysindexChanged);
}

int
TXddicSetSysindexChanged(ddic, yes)
DDIC	*ddic;
int	yes;
/* Sets SYSINDEX-changed flag to `yes'.
 * Returns 0 on error.
 */
{
	ddic->sysindexChanged = !!yes;
	return(1);
}
