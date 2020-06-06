/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"
#include "mmsg.h"
#include "cgi.h"

#undef CACHE_STATS
#ifdef CACHE_STATS
static unsigned hit, miss;
#endif

/******************************************************************/

/* DDCACHE Flags */

typedef struct DDCACHEITEM_tag DDCACHEITEM;
struct DDCACHEITEM_tag
{
	byte	inUse;				/* nonzero: in active use */
	byte	isClone;			/* nonzero: clone of other */
	byte	toBeFreed;			/* nonzero: free when done */
	time_t	lastuse;/* When was this last used */
	DBTBL	*dbtbl; /* The data we are caching */
	DDCACHEITEM	*next;
	DDCACHEITEM	*prev;
};

typedef struct DDCACHELIST_tag DDCACHELIST;
struct DDCACHELIST_tag
{
	DDCACHE *cache;
	DDCACHELIST *next;
	DDCACHELIST *prev;
};

struct DDCACHE_tag
{
	int		maxhold;/* The maximum number of tables to hold */
	DDCACHEITEM	*head;	/* The actual cache */
	DDCACHEITEM	*tail;	/* The actual cache */
};

static DDCACHELIST *head = NULL, *tail = NULL;
static DDCACHEITEM *freeitem ARGS((DDCACHE *, DDCACHEITEM *));
static DDCACHEITEM *TXaddToDdcache(DDIC *ddic, DBTBL *dbtbl);

static DBTBL *dummy = NULL;

int	TXtraceDdcache = 0;
/* 0x0001       items created/freed
 * 0x0002       items marked in/out of use
 * 0x0004       items marked as clone
 */
char	*TXtraceDdcacheTableName = NULL;	/* non-NULL: only this table*/


static void
TXtraceDdcacheMsg(DDCACHE *cache, DDCACHEITEM *item, const char *msg)
/* Prints trace message. `cache' and `item' are optional.
 */
{
	DBTBL	*dbtbl = (item ? item->dbtbl : DBTBLPN);
	char	rnameBuf[1024];

	if (TXtraceDdcacheTableName &&		/* only yap for this table */
	    (!dbtbl ||
	     !(dbtbl->lname ?
	       strcmp(dbtbl->lname, TXtraceDdcacheTableName) == 0 :
	       (dbtbl->rname ?
		strcmp(dbtbl->rname, TXtraceDdcacheTableName) == 0 : 0))))
		return;

	if (dbtbl && dbtbl->lname && dbtbl->rname &&
	    strcmp(dbtbl->lname, dbtbl->rname) != 0)
		htsnpf(rnameBuf, sizeof(rnameBuf), " (`%+.*s')",
		       (int)(sizeof(rnameBuf) - 6));
	else
		*rnameBuf = '\0';
	putmsg(MINFO, CHARPN,
	   "DDCACHE %p DDCACHEITEM %p inUse=%d DBTBL %p `%s'%s: %s",
	       cache, item, (item ? (int)item->inUse : 0), dbtbl,
	       (dbtbl ? dbtbl->lname : NULL), rnameBuf, msg);
}

/******************************************************************/

int
TXclosedummy(void)
{
	if (dummy)
		dummy = _closedbtbl(dummy);
	return 0;
}

/******************************************************************/

DDCACHE *
TXclosecache(cache)
DDCACHE *cache;
{
	DDCACHEITEM *current, *next;
	DDCACHELIST *search;

	if (cache)
	{
		for (current = cache->head; current; current = next)
		{
			next = current->next;
			if (current->inUse) continue;
			if (current->isClone) current->dbtbl = DBTBLPN;
			current = freeitem(cache, current);
		}
		free(cache);
#ifdef CACHE_STATS
		if ((hit + miss) > 0)
			putmsg(0, NULL,
			       "Attempts = %d, Hit %%age = %d, Miss %%age = %d",
			       (hit + miss), 100 * hit / (hit + miss),
			       100 * miss / (hit + miss));
#endif
		for (search = head; search; search = search->next)
		{
			if (search->cache == cache)
			{
				if (search->prev)
					search->prev->next = search->next;
				if (search->next)
					search->next->prev = search->prev;
				if (search == head)
					head = search->next;
				if (search == tail)
					tail = search->prev;
				free(search);
				break;
			}
		}
	}
	return NULL;
}

/******************************************************************/

DDCACHE *
TXopencache(TXPMBUF *pmbuf)
{
	static const char	fn[] = "TXopencache";
	DDCACHE *cache = NULL;
	DDCACHELIST *clist;

	cache = (DDCACHE *) TXcalloc(pmbuf, fn, 1, sizeof(DDCACHE));
	if (!cache) return NULL;
	cache->head = NULL;
	cache->tail = NULL;
	cache->maxhold = 1;
#ifdef CACHE_STATS
	hit = 0;
	miss = 0;
#endif
	clist = (DDCACHELIST *) TXcalloc(pmbuf, fn, 1, sizeof(DDCACHELIST));
	if (clist != NULL)
	{
		clist->cache = cache;
		clist->next = head;
		clist->prev = NULL;
		if (head)
			head->prev = clist;
		else
			tail = clist;
		head = clist;
	}
	return cache;
}

/******************************************************************/

static DBTBL *cloneramdbtbl ARGS((DBTBL *));

static DBTBL *
cloneramdbtbl(db)
DBTBL *db;
{
	return db;
}


/******************************************************************/
/*
	Search the cache for the desired table that is not in
	use and return it.  If it is not in the cache then
	NULL is returned.
*/

DBTBL *
TXgetcache(ddic, name)
DDIC *ddic;
char *name;
{
	DDCACHE *cache;
	DDCACHEITEM *current, *next, *rc = NULL;

	cache = ddic->ddcache;
	if (!strcmp(name, "SYSDUMMY"))
	{
		if (!dummy)
		{
			DD *dd = opennewdd(1);

			dummy =
				createdbtbl(ddic, dd, NULL, "SYSDUMMY", "",
					    'T');
			putdbtblrow(dummy, NULL);
			closedd(dd);
		}
		else
			dummy->ddic = ddic;
		return dummy;
	}
	if (!cache)
		return NULL;
	for (current = cache->head; current; current = next)
	{
		next = current->next;
		if (!current->inUse)
		{
			/* Clean up a little while we're in here: */
			if (current->toBeFreed)
			{
				if (current->isClone) current->dbtbl = DBTBLPN;
				current = freeitem(cache, current);
			}
			/* Is this a usable entry?: */
			else if (!rc && !strcmp(current->dbtbl->rname, name) &&
			    current->dbtbl->tbl)
			{
				if(!TXtblstillthere(current->dbtbl))
					current = freeitem(cache, current);
				else
				{
					if (TXtraceDdcache & 0x2)
						TXtraceDdcacheMsg(cache,
								  current,
							 "Marking as in use");
					current->inUse = 1;
					current->lastuse = 0;
					rc = current;
#ifdef CACHE_STATS
					hit++;
#endif
				}
			}
			else if (++current->lastuse > cache->maxhold)
				if (!isramdbtbl(current->dbtbl))
					current = freeitem(cache, current);
		}
	}
#ifdef CACHE_STATS
	if (!rc)
		miss++;
#endif
	if (!rc)
	{
		for (current = cache->head; current; current = next)
		{
			next = current->next;
			if (!rc && !strcmp(current->dbtbl->rname, name))
			{
				if (current->inUse &&
				    !current->toBeFreed &&
				    isramdbtbl(current->dbtbl))
				{
					DBTBL	*dbtbl;
					dbtbl = cloneramdbtbl(current->dbtbl);
					if (!dbtbl) goto err;
					rc = TXaddToDdcache(ddic, dbtbl);
					if (!rc) return(dbtbl);
					if (TXtraceDdcache & 0x4)
						TXtraceDdcacheMsg(cache, rc,
							  "Marking as clone");
					rc->isClone = 1;
				}
			}
		}
	}
	return(rc ? rc->dbtbl : DBTBLPN);
err:
	return(NULL);
}

/******************************************************************/
/*
	Attempts to insert the item into the cache.  Whether it
	fails or succeeds is largely irrelevant, as it can still
	be used.
*/

static DDCACHEITEM *
TXaddToDdcache(ddic, dbtbl)
DDIC *ddic;
DBTBL *dbtbl;
/* Internal version.
 * Returns DDCACHEITEM created, or NULL on error.
 */
{
	static const char fn[] = "TXaddToDdcache";
	DDCACHE *cache;
	DDCACHEITEM *current = NULL;

	cache = ddic->ddcache;
	if (!cache)
		goto err;
	current = (DDCACHEITEM *)TXcalloc(TXPMBUFPN, fn, 1,
					  sizeof(DDCACHEITEM));
	if (!current)
		goto err;

	current->dbtbl = dbtbl;
	current->inUse = 1;
	current->next = cache->head;
	current->prev = NULL;
	if (cache->head)
		cache->head->prev = current;
	else
		cache->tail = current;
	cache->head = current;

	if (TXtraceDdcache & 0x1)
		TXtraceDdcacheMsg(cache, current,
				  "Created item and marked in use");

	return(current);
err:
	current = freeitem(cache, current);
	return(NULL);
}

int
TXputcache(ddic, dbtbl)
DDIC *ddic;
DBTBL *dbtbl;
/* Adds `dbtbl' to DBTBL cache of `ddic'.
 * Returns -1 on error, 0 on success.
 */
{
	return(TXaddToDdcache(ddic, dbtbl) ? 0 : -1);
}

/******************************************************************/
/*
	Tries to mark the item as being out of use.  If the item
	does not appear to be in the cache it closes the table.
*/

int
TXungetcache(ddic, dbtbl)
DDIC *ddic;
DBTBL *dbtbl;
{
	DDCACHE *cache;
	DDCACHEITEM *current;

	if (dbtbl == dummy ||
	    (dbtbl->rname != CHARPN && !strcmp(dbtbl->rname, "SYSDUMMY")))
		return 0;
	cache = ddic->ddcache;
	if (!cache)
		return 0;
	for (current = cache->head; current; current = current->next)
	{
		if (current->dbtbl != dbtbl) continue;	/* not our table */

		if (TXtraceDdcache & 0x2)
			TXtraceDdcacheMsg(cache, current,
					  "Marking as not in use");

#ifdef CACHE_PEDANTIC
		if (!current->inUse)
			putmsg(999, "TXungetcache", "Not in use %s",
				dbtbl->rname);
#endif /* CACHE_PEDANTIC */
		if (current->isClone)
		{
			current->dbtbl = DBTBLPN;	/* owned elsewhere */
			current = freeitem(cache, current);
			return(0);
		}
		current->inUse = 0;
		current->lastuse = 0;
		if (current->toBeFreed)
			current = freeitem(cache, current);
		return(0);
	}
	/* Not found in cache; just close the table: */
	_closedbtbl(dbtbl);
	return 0;
}

/******************************************************************/

static DDCACHEITEM *
freeitem(cache, item)
DDCACHE *cache;		/* (in, opt.) */
DDCACHEITEM *item;	/* (in, opt.) */
/* Removes `item' from `cache' and closes it.
 * Returns NULL.
 */
{
	if (!item) return(NULL);

	if (TXtraceDdcache & 0x1)
		TXtraceDdcacheMsg(cache, item, "Freeing item");

	item->dbtbl = _closedbtbl(item->dbtbl);
	if (item->prev)
		item->prev->next = item->next;
	if (item->next)
		item->next->prev = item->prev;
	if (cache)
	{
		if (item == cache->head)
			cache->head = item->next;
		if (item == cache->tail)
			cache->tail = item->prev;
	}
	item = TXfree(item);
	return(NULL);
}

/******************************************************************/

int
TXrmcache(ddic, name, wasram)
DDIC *ddic;
char *name;	/* (in, opt.) only for this table name, else all non-RAM */
int *wasram;	/* (out, opt.) 1 if table was RAM table, 0 if not */
/* Search the cache for the desired table and remove it from the cache
 * (or at least mark for deletion, if in use).
 * Returns 0 on success, -1 on error.
 */
{
	DDCACHE *cache;
	DDCACHEITEM *current, *next;
	int	isRamTbl;

	if (wasram) *wasram = 0;
	cache = ddic->ddcache;
	if (!cache)
		return -1;
	for (current = cache->head; current; current = next)
	{
		next = current->next;
		isRamTbl = (current->dbtbl->tbl &&
			    isramtbl(current->dbtbl->tbl));
		if (name != CHARPN ?
		    (current->dbtbl->rname != CHARPN &&
		     strcmp(current->dbtbl->rname, name) != 0) : isRamTbl)
			continue;		/* not our table */

		if (wasram) *wasram = isRamTbl;
		/* Bug 3756: do not just close `current->dbtbl->tbl':
		 * thwarts later TXclosecacheindex(), which will then
		 * not call TXbtfreecache(), which prevents index file
		 * rm under Windows at table DROP.  Also check if in-use:
		 */
		if (current->inUse)
		{
			if (TXtraceDdcache & 0x2)
				TXtraceDdcacheMsg(cache, current,
				   "Still in use: Marking to be freed later");
			current->toBeFreed = 1;
		}
		else
		{
			if (current->isClone) current->dbtbl = DBTBLPN;
			current = freeitem(cache, current);
		}
	}
	return 0;
}

/******************************************************************/
/*
	Search the cache for the desired table and close indexes
	on it.
*/

int
TXclosecacheindex(ddic, name)
DDIC *ddic;
char *name;	/* (in) table name */
{
	int rc = 0;
	DDCACHE *cache;
	DDCACHEITEM *current, *next;

	cache = ddic->ddcache;
	if (!cache)
		return rc;
	for (current = cache->head; current; current = next)
	{
		next = current->next;
		/* Bug 3756: do not free in-use table's info: */
		if (!current->inUse &&
		    strcmp(current->dbtbl->rname, name) == 0)
		{
			closeindexes(current->dbtbl);
			TXbtfreecache(current->dbtbl);
			rc++;
		}
	}
	return rc;
}
