/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "dbquery.h"
#include "texint.h"
#include "fdbi.h"


typedef struct BTREEC_tag
{
	BTREE	*btree;
	A3DBI	*dbi;
        FDBI    *fi;
	char	*name;
	int	state;
	char	*sysindexParams;		/* SYSINDEX.PARAMS value */

	ft_counter	lastread;
	time_t	pathCheckTime;	/* when the file at our path was last compared */
	struct  BTREEC_tag  *next, *prev;
} BTREEC ;
#define BTREECPN        ((BTREEC *)NULL)

/******************************************************************/

static void *btfindcache ARGS((BTREEC *, char *, int, ft_counter *, DBTBL *,
			       CONST char *sysindexParams));

/* Nonzero if the file `bt' has open is no longer the file of that name:
 * another process dropped the index and created it again.  Re-reading
 * pages from the open handle would then read the old, deleted index. */
static int
txBtreeFileReplaced(BTREE *bt)
{
	struct stat	open_st, name_st;
	int		fh;
	char		*fn;

	if (!bt || !bt->dbf) return 0;
	fh = getdbffh(bt->dbf);
	fn = getdbffn(bt->dbf);
	if (fh < 0 || !fn || fstat(fh, &open_st) != 0) return 0;
	if (stat(fn, &name_st) != 0) return 1;		/* gone */
	return (open_st.st_ino != name_st.st_ino ||
		open_st.st_dev != name_st.st_dev);
}

void TXlockBatchVerifyFresh(void);

/* 1 if SYSINDEX, looked at now, lists file `name' as a finished index of
 * the table.  0 if not: it was dropped (and perhaps is being built again
 * under the same name -- its file then exists but is not to be read). */
static int
txIndexListedFinal(DBTBL *dbtbl, const char *name)
{
	char	**paths = NULL, *itype = NULL;
	int	n, i, listed = 0;

	if (!dbtbl || !dbtbl->ddic || !dbtbl->rname) return(1);
	TXlockBatchVerifyFresh();		/* the list as it is now */
	n = ddgetindex(dbtbl->ddic, dbtbl->rname, NULL, &itype, &paths, NULL,
		       NULL);
	for (i = 0; i < n && paths && itype; i++)
		if (paths[i] && strcmp(paths[i], name) == 0 &&
		    itype[i] != INDEX_CR && itype[i] != INDEX_3CR &&
		    itype[i] != INDEX_MMCR && itype[i] != INDEX_FULLCR &&
		    itype[i] != INDEX_VECCR && itype[i] != INDEX_DEL &&
		    itype[i] != INDEX_TEMP)
			listed = 1;
	if (paths) paths = TXfreeStrList(paths, n);
	itype = TXfree(itype);
	return(listed);
}

static void *
btfindcache(btc, name, type, fc, dbtbl, sysindexParams)
BTREEC	*btc;		/* The cache to search */
char	*name;		/* Index we are looking for. */
int	type;
ft_counter	*fc;
DBTBL	*dbtbl;
CONST char	*sysindexParams;	/* (in) Up-to-date SYSINDEX.PARAMS */
{
	static CONST char	fn[] = "btfindcache";
	BTREEC	*c;	/* Current pointer */
	int	closeIdx = 0, paramsChg = 0;

	c = btc;
	while(c)
	{
		if(c->state == 0 && !strcmp(name, c->name))
		{
			int	vrc, gone;

			/* WTF - need to check for no writes */
			vrc = TXlockindex(dbtbl, INDEX_VERIFY, &c->lastread);
			/* an entry left empty by a drop is opened afresh; so is
			 * one whose file is no longer the file at its path
			 * (looked at once a second at most) */
			if (vrc == 0 && type == INDEX_BTREE)
			{
				time_t	now = time(NULL);

				if (!c->btree)
					vrc = -2;
				else if (now != c->pathCheckTime)
				{
					c->pathCheckTime = now;
					if (txBtreeFileReplaced(c->btree) &&
					    existsbtree(c->name))	/* another, not none */
						vrc = -2;
				}
			}
			/* dropped by someone else: nothing to reopen */
			gone = (vrc == -2 && !existsbtree(c->name));
			switch(vrc)
			{
			    case -2:
				/* SYSINDEX.PARAMS may have changed,
				 * ie. if index was dropped and re-created:
				 */
				paramsChg = (strcmp(c->sysindexParams ?
						    c->sysindexParams : "",
						    sysindexParams) != 0);
				if (paramsChg)
				{
					TXfree(c->sysindexParams);
					c->sysindexParams =
						strdup(sysindexParams);
					if (c->sysindexParams == CHARPN)
					{
					 TXputmsgOutOfMem(TXPMBUFPN, MERR+MAE,
					    fn, strlen(sysindexParams)+1, 1);
						closeIdx = 1;
					}
				}
				switch(type)
				{
				    case INDEX_BTREE:
					if (!closeIdx && !paramsChg &&
					    (!c->btree ||
					     txBtreeFileReplaced(c->btree)))
						paramsChg = 1;	/* reopen by name */
					/* another file under the name: read it
					 * only if it is a finished index */
					if (!gone && paramsChg &&
					    !txIndexListedFinal(dbtbl, c->name))
						gone = 1;
					if (gone) closeIdx = 1;
					if (closeIdx || paramsChg)
						c->btree=closebtree(c->btree);
					if (!closeIdx)
					{
						if (paramsChg)
						{
							c->btree = openbtree(c->name, BT_MAXPGSZ, 20, 0, O_RDONLY);
							if (c->btree && bttexttoparam(c->btree, c->sysindexParams) < 0)
								c->btree = closebtree(c->btree);
						}
						else
							btreinit(c->btree);
					}
					break;
				    case INDEX_3DB:
				  	c->dbi = close3dbi(c->dbi);
					if (closeIdx || gone) break;
					c->dbi = open3dbi(name, PM_SELECT,
						     type, c->sysindexParams);
					break;
                                    case INDEX_MM:
                                        c->fi = closefdbi(c->fi);
					if (closeIdx || gone) break;
                                        c->fi = openfdbi(name, PM_SELECT, 0,
							 c->sysindexParams,
							 dbtbl);
                                        break;
                                    case INDEX_FULL:
                                        c->fi = closefdbi(c->fi);
					if (closeIdx || gone) break;
                                        c->fi = openfdbi(name, PM_SELECT,
                                                         FDF_FULL,
							 c->sysindexParams,
							 dbtbl);
                                        break;
				}
				break;
		            case -1:
			        return NULL;
			}
			c->lastread = *fc;
			/* in use only if there is something to use */
			c->state = ((type == INDEX_BTREE ? (void *)c->btree :
				     type == INDEX_3DB ? (void *)c->dbi :
				     (void *)c->fi) != NULL);
			switch(type)
			{
				case INDEX_BTREE:
					if(c->btree)
						c->btree->stopatrange = 0;
					return c->btree;
				case INDEX_3DB:
					if(c->dbi)
					{
						if(c->dbi->mm->ql!=MMQLPN)
						  c->dbi->mm->ql=
						TXclosemmql(c->dbi->mm->ql,0);
						c->dbi->mm->mm = NULL;
					}
					return c->dbi;
                                case INDEX_MM:
                                case INDEX_FULL:
                                  /* wtf no need for INDEX_3DB-style fixup? */
                                  return(c->fi);
			}
		}
		c = c->next;
	}
	return NULL;
}

/******************************************************************/

static BTREEC *btaddcache ARGS((BTREEC *, char *, int, ft_counter *, void *,
				CONST char *sysindexParams));

static BTREEC	*
btaddcache(btc, name, type, fc, bt, sysindexParams)
BTREEC	*btc;
char	*name;
int	type;
ft_counter	*fc;
void	*bt;
CONST char	*sysindexParams;
{
	static CONST char	fn[] = "btaddcache";
	BTREEC	*c;

	c = (BTREEC *)calloc(1, sizeof(BTREEC));
	if(c)
	{
		c->name = strdup(name);
		switch(type)
		{
			case INDEX_BTREE:
				c->btree = bt;
				break;
			case INDEX_3DB:
				c->dbi = bt;
				break;
                        case INDEX_MM:
                        case INDEX_FULL:
                                c->fi = bt;
                                break;
		}
		c->next = btc;
		c->prev = BTREECPN;
		c->lastread = *fc;
		c->state = 1;
		if ((c->sysindexParams = strdup(sysindexParams)) == CHARPN)
			TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn,
					 strlen(sysindexParams) + 1, 1);
		if(btc)
			btc->prev = c;
	}
	return c;
}

/******************************************************************/
/*	Implement btree caching.  We need to know the table that
 *	the btree belongs to (can be NULL, in which case assumed
 *	single owner), to perform consistency checks.
 *
 *	- if no writes since last read, just return cached tree
 *	- if writes have occurred, re-init tree.
 *	- initially only handle trees for read.
 *	- will handle locking issues.
 */
/******************************************************************/

void	*
TXbtcacheopen(dbtbl, name, type, mode, sysindexParams)
DBTBL	*dbtbl;	/* The table the index belongs to */
char	*name;	/* File name of the index. */
int	type;	/* Index Type */
int	mode;	/* Mode to be opened in.  (Currently only INDEX_READ) */
CONST char	*sysindexParams;	/* SYSINDEX.PARAMS value */
{
	static const char	fn[] = "TXbtcacheopen";
	BTREEC	*btc;
	void	*bt;
	ft_counter	fc = { 0L, 0L };

	btc = dbtbl->btcache;
	if(mode != INDEX_READ)
	{
		txpmbuf_putmsg((dbtbl && dbtbl->ddic ? dbtbl->ddic->pmbuf :
				TXPMBUFPN), MERR + UGE, fn, "Invalid mode %d",
			       (int)mode);
		return NULL;
	}
	/* Note that `fc' is uninitialized here; ok because we aren't passing
	 * INDEX_VERIFY flag to TXlockindex(), so it won't read `fc':
	 */
	if(dbtbl)
	{
		if (TXlockindex(dbtbl, INDEX_READ, &fc) == -1)
			return(NULL);
	}
	bt = btfindcache(btc, name, type, &fc, dbtbl, sysindexParams);
	if(!bt)
	{
		/* Dropped while we waited for the lock: nothing to open.  Nor
		 * if it is being built again under the same name. */
		if (dbtbl && (!existsbtree(name) ||
			      !txIndexListedFinal(dbtbl, name)))
		{
			TXunlockindex(dbtbl, INDEX_READ, NULL);
			return(NULL);
		}
		switch(type)
		{
		    case INDEX_BTREE:
			bt = openbtree(name, BT_MAXPGSZ, 20, 0, O_RDONLY);
			if (bt && bttexttoparam(bt, (char*)sysindexParams) < 0)
				bt = closebtree(bt);
			break;
		    case INDEX_3DB:
		    	bt = open3dbi(name, PM_SELECT, type, sysindexParams);
			break;
                    case INDEX_MM:
                        bt = openfdbi(name, PM_SELECT, 0,
				      sysindexParams, dbtbl);
                        break;
                    case INDEX_FULL:
                        bt = openfdbi(name, PM_SELECT, FDF_FULL,
				      sysindexParams, dbtbl);
                        break;
		    default:
		    	putmsg(999, NULL, "Unknown index type %d", type);
			break;
		}
		if(bt)
			dbtbl->btcache = btaddcache(btc, name, type, &fc, bt,
						    sysindexParams);
		else if (dbtbl)
			/* no one closes a failed open: give the lock back */
			TXunlockindex(dbtbl, INDEX_READ, NULL);
	}
	return bt;
}

/******************************************************************/

int
TXindexWasDropped(dbtbl, name)
DBTBL	*dbtbl;
char	*name;	/* File name of the index, as SYSINDEX gave it */
/* Returns 1 if index `name', which the caller took from the index list a
 * moment ago, has since been dropped by someone else: its file is gone
 * and a fresh look at SYSINDEX no longer lists it (or it has already been
 * created again).  The caller then just goes on without it.  0 if it is
 * still listed and has no file (a real problem).
 */
{
	char	**paths = NULL;
	int	n, i, listed = 0;

	int	locked;

	if (!dbtbl || !dbtbl->ddic || !dbtbl->rname || !name) return(0);
	/* A drop removes the file before SYSINDEX shows the change, all
	 * under the table's index lock: wait for it there, then look. */
	locked = (TXlockindex(dbtbl, INDEX_READ, NULL) != -1);
	TXlockBatchVerifyFresh();		/* the list as it is now */
	/* there again: dropped and created anew since the caller looked */
	if (existsbtree(name))
		listed = 0;
	else
	{
		n = ddgetindex(dbtbl->ddic, dbtbl->rname, NULL, NULL, &paths,
			       NULL, NULL);
		for (i = 0; i < n && paths; i++)
			if (paths[i] && strcmp(paths[i], name) == 0)
				listed = 1;
		if (paths) paths = TXfreeStrList(paths, n);
	}
	if (locked) TXunlockindex(dbtbl, INDEX_READ, NULL);
	return(!listed);
}

/******************************************************************/

void	*
TXbtcacheclose(dbtbl, name, type, mode, btree)
DBTBL	*dbtbl;
char	*name;
int	type;
int	mode;
void	*btree;
{
	BTREEC	*btc;

	btc = dbtbl->btcache;
	while(btc)
	{
		if(btc->state == 1 &&
		   (btc->btree == btree ||
		    btc->dbi == btree ||
                    btc->fi == btree) &&
		   !strcmp(btc->name, name))
		{
			btc->state = 0;
			if((NULL != dbtbl) && (0 != mode))
				TXunlockindex(dbtbl, mode, NULL);
			return NULL;
		}
		btc = btc->next;
	}
	switch(type)
	{
		case INDEX_BTREE:
			closebtree(btree);
			break;
		case INDEX_3DB:
			close3dbi(btree);
			break;
                case INDEX_MM:
                case INDEX_FULL:
                        closefdbi(btree);
                        break;
		default:
		    	putmsg(999, NULL, "Unknown index type %d", type);
			break;
	}
	if((NULL != dbtbl) && (0 != mode))
		TXunlockindex(dbtbl, mode, NULL);
	return NULL;
}

/******************************************************************/

int
TXbtfreecache(dbtbl)
DBTBL	*dbtbl;
{
	BTREEC	*btc, *next;
	DBTBL	*saveBtreeLogDbtbl;

	saveBtreeLogDbtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = dbtbl;		/* for btreelog */

	/* Bug 6247: closedbidx() instead of setting dbtbl->index.btree NULL;
	 * avoid memleak.  Also do this outside of the free-btcache loop,
	 * in case closedbidx() calls any btcache functions:
	 */
	for (btc = dbtbl->btcache; btc; btc = btc->next)
		if (dbtbl->index.btree == btc->btree)
		{
			/* closedbidx() will call TXbtcacheclose(),
			 * which could alter `dbtbl->btcache' list,
			 * so break out of list after calling it:
			 */
			closedbidx(&dbtbl->index);
			break;
		}

	while ((btc = dbtbl->btcache) != NULL)
	{
		/* Remove `btc' from list before freeing it,
		 * in case free functions call btcache functions:
		 */
		dbtbl->btcache = btc->next;

		if(btc->btree)
			btc->btree = closebtree(btc->btree);
		if(btc->dbi)
			btc->dbi = close3dbi(btc->dbi);
                btc->fi = closefdbi(btc->fi);
		btc->name = TXfree(btc->name);
		btc->sysindexParams = TXfree(btc->sysindexParams);
		btc = TXfree(btc);
	}

	TXbtreelog_dbtbl = saveBtreeLogDbtbl;
	return 0;
}

/******************************************************************/

