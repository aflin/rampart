/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "txcoreconfig.h"
#include "dbquery.h"
#include "texint.h"

/******************************************************************/

int
lockmode(ddic, mode)
DDIC	*ddic;
int	mode;
{
	switch(mode)
	{
		case LOCK_MANUAL:
			ddic->manuallocking = 1;
			ddic->nolocking = 0;
			return 0;
		case LOCK_AUTOMATIC:
			ddic->manuallocking = 0;
			ddic->nolocking = 0;
			return 0;
		case LOCK_NONE:
			ddic->nolocking = 1;
		default:
			return -1;
	}
}

/******************************************************************/

int
locktable(ddic, table, type)
DDIC	*ddic;
char	*table;
int	type;
{
#ifndef OLD_LOCKING
	long	tblid;
#else
	int	tblid;
#endif
	int	rc = 0;
	char	*c;

	c = strtok(table, ", \t");
	while(c)
	{
		tblid = addltable(ddic->pmbuf, ddic->dblock, table);
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
		if(dblock(ddic, ddic->sid, &tblid, type, table, NULL, NULL)==-1)
#else
		if(dblock(ddic, ddic->sid, &tblid, type, table, NULL)==-1)
#endif
#else
		if(dblock(ddic, ddic->sid, tblid, type, table, NULL)==-1)
#endif
			rc = -1;
		c = strtok(NULL, ", \t");
	}
	if(rc==-1)
		unlocktable(ddic, table, type);
	return rc;
}

/******************************************************************/

int
unlocktable(ddic, table, type)
DDIC	*ddic;
char	*table;
int	type;
{
#ifndef OLD_LOCKING
	long	tblid;
#else
	int	tblid;
#endif
	int	rc=0;
	char	*c;

	c = strtok(table, ", \t");
	while(c)
	{
		tblid = TXfindltable(ddic->pmbuf, ddic->dblock, table);
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
		if(dbunlock(ddic, ddic->sid, &tblid, type, table, NULL)==-1)
#else
		if(dbunlock(ddic, ddic->sid, &tblid, type, table)==-1)
#endif
#else
		if(dbunlock(ddic, ddic->sid, tblid, type, table)==-1)
#endif
			rc = -1;
		delltable(ddic->pmbuf, ddic->dblock, table, tblid);
		c = strtok(NULL, ", \t");
	}
	return rc;
}

/******************************************************************/

int
TXtouchindexfile(ddic)
DDIC	*ddic;
{
	TBL	*tbl;
	RECID	*rc;

	tbl = ddic->indextbl;
	if(TXlocksystbl(ddic, SYSTBL_INDEX, W_LCK, NULL) == -1)
	{
		return -1;
	}
	rewindtbl(tbl);
	rc = gettblrow(tbl, NULL);
	if(rc)
		rc = puttblrow(tbl, rc);
	TXunlocksystbl(ddic, SYSTBL_INDEX, W_LCK);
	return 0;
}

/******************************************************************/

int
TXlocksystbl(ddic, tblid, ltype, fc)
DDIC	*ddic;
SYSTBL	tblid;
int	ltype;
ft_counter	*fc;
/* Returns same as dblock(): -2 if locked but recent mods, -1 on error,
 * 0 if locked and no recent mods.
 */
{
	static const char	fn[] = "TXlocksystbl";
	int	rc, attempts;
	
	attempts = 0;
	do
	{
		switch(tblid)
		{
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
			case SYSTBL_INDEX:
				rc = dblock(ddic, ddic->sid,
					      &ddic->iid, ltype, "SYSINDEX", fc, NULL);
				break;
			case SYSTBL_USERS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->uid, ltype, "SYSUSERS", fc, NULL);
				break;
			case SYSTBL_PERMS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->pid, ltype, "SYSPERMS", fc, NULL);
				break;
			case SYSTBL_TRIGGER:
				rc = dblock(ddic, ddic->sid,
					      &ddic->rid, ltype, TEXISSYSTRIG, fc, NULL);
				break;
			case SYSTBL_TABLES:
				rc = dblock(ddic, ddic->sid,
					      &ddic->tid, ltype, "SYSTABLES", fc, NULL);
				break;
			case SYSTBL_COLUMNS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->cid, ltype, "SYSCOLUMNS", fc, NULL);
				break;
#else
			case SYSTBL_INDEX:
				rc = dblock(ddic, ddic->sid,
					      &ddic->iid, ltype, "SYSINDEX", fc);
				break;
			case SYSTBL_USERS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->uid, ltype, "SYSUSERS", fc);
				break;
			case SYSTBL_PERMS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->pid, ltype, "SYSPERMS", fc);
				break;
			case SYSTBL_TRIGGER:
				rc = dblock(ddic, ddic->sid,
					      &ddic->rid, ltype, TEXISSYSTRIG, fc);
				break;
			case SYSTBL_TABLES:
				rc = dblock(ddic, ddic->sid,
					      &ddic->tid, ltype, "SYSTABLES", fc);
				break;
			case SYSTBL_COLUMNS:
				rc = dblock(ddic, ddic->sid,
					      &ddic->cid, ltype, "SYSCOLUMNS", fc);
				break;
#endif
#else
			case SYSTBL_INDEX:
				rc = dblock(ddic, ddic->sid,
					      ddic->iid, ltype, "SYSINDEX", fc);
				break;
			case SYSTBL_USERS:
				rc = dblock(ddic, ddic->sid,
					      ddic->uid, ltype, "SYSUSERS", fc);
				break;
			case SYSTBL_PERMS:
				rc = dblock(ddic, ddic->sid,
					      ddic->pid, ltype, "SYSPERMS", fc);
				break;
			case SYSTBL_TRIGGER:
				rc = dblock(ddic, ddic->sid,
					      ddic->rid, ltype, TEXISSYSTRIG, fc);
				break;
			case SYSTBL_TABLES:
				rc = dblock(ddic, ddic->sid,
					      ddic->tid, ltype, "SYSTABLES", fc);
				break;
			case SYSTBL_COLUMNS:
				rc = dblock(ddic, ddic->sid,
					      ddic->cid, ltype, "SYSCOLUMNS", fc);
				break;
#endif
			default:
				txpmbuf_putmsg(ddic->pmbuf, MERR + UGE, fn,
					       "Unknown system table id %d",
					       (int)tblid);
				rc = -1;
				break;
		}
	} while (rc == -1 &&
		 texispeekerr(ddic) == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT) &&
		 ++attempts < 2 &&
		 /* KNG 20131125: leave error on stack if not retrying: */
		 (texispoperr(ddic), 1));
	return rc;
}

/******************************************************************/

int
TXunlocksystbl(ddic, tblid, ltype)
DDIC	*ddic;
SYSTBL	tblid;
int	ltype;
{
	switch(tblid)
	{
#ifndef OLD_LOCKING
#ifdef STEAL_LOCKS
		case SYSTBL_INDEX:
			return dbunlock(ddic, ddic->sid,
					&ddic->iid, ltype, "SYSINDEX", NULL);
		case SYSTBL_USERS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->uid, ltype, "SYSUSERS", NULL);
		case SYSTBL_PERMS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->pid, ltype, "SYSPERMS", NULL);
		case SYSTBL_TRIGGER:
			return dbunlock(ddic, ddic->sid,
				      &ddic->rid, ltype, TEXISSYSTRIG, NULL);
		case SYSTBL_TABLES:
			return dbunlock(ddic, ddic->sid,
				      &ddic->tid, ltype, "SYSTABLES", NULL);
		case SYSTBL_COLUMNS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->cid, ltype, "SYSCOLUMNS", NULL);
#else
		case SYSTBL_INDEX:
			return dbunlock(ddic, ddic->sid,
					&ddic->iid, ltype, "SYSINDEX");
		case SYSTBL_USERS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->uid, ltype, "SYSUSERS");
		case SYSTBL_PERMS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->pid, ltype, "SYSPERMS");
		case SYSTBL_TRIGGER:
			return dbunlock(ddic, ddic->sid,
				      &ddic->rid, ltype, TEXISSYSTRIG);
		case SYSTBL_TABLES:
			return dbunlock(ddic, ddic->sid,
				      &ddic->tid, ltype, "SYSTABLES");
		case SYSTBL_COLUMNS:
			return dbunlock(ddic, ddic->sid,
				      &ddic->cid, ltype, "SYSCOLUMNS");
#endif
#else
		case SYSTBL_INDEX:
			return dbunlock(ddic, ddic->sid,
					ddic->iid, ltype, "SYSINDEX");
		case SYSTBL_USERS:
			return dbunlock(ddic, ddic->sid,
				      ddic->uid, ltype, "SYSUSERS");
		case SYSTBL_PERMS:
			return dbunlock(ddic, ddic->sid,
				      ddic->pid, ltype, "SYSPERMS");
		case SYSTBL_TRIGGER:
			return dbunlock(ddic, ddic->sid,
				      ddic->rid, ltype, TEXISSYSTRIG);
		case SYSTBL_TABLES:
			return dbunlock(ddic, ddic->sid,
				      ddic->tid, ltype, "SYSTABLES");
		case SYSTBL_COLUMNS:
			return dbunlock(ddic, ddic->sid,
				      ddic->cid, ltype, "SYSCOLUMNS");
#endif
		default:
			return -1;
	}
}

/******************************************************************/

