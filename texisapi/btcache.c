/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
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
	struct  BTREEC_tag  *next, *prev;
} BTREEC ;
#define BTREECPN        ((BTREEC *)NULL)

/******************************************************************/

static void *btfindcache ARGS((BTREEC *, char *, int, ft_counter *, DBTBL *,
			       CONST char *sysindexParams));

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
			/* WTF - need to check for no writes */
			switch(TXlockindex(dbtbl, INDEX_VERIFY, &c->lastread))
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
					if (closeIdx) break;
					c->dbi = open3dbi(name, PM_SELECT,
						     type, c->sysindexParams);
					break;
                                    case INDEX_MM:
                                        c->fi = closefdbi(c->fi);
					if (closeIdx) break;
                                        c->fi = openfdbi(name, PM_SELECT, 0,
							 c->sysindexParams,
							 dbtbl);
                                        break;
                                    case INDEX_FULL:
                                        c->fi = closefdbi(c->fi);
					if (closeIdx) break;
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
			c->state = 1;
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
	}
	return bt;
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

