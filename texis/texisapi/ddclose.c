/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "dbquery.h"
#include "texint.h"


TXTBLCACHE *
TXtblcacheClose(TXTBLCACHE *tblCache)
{
	if (!tblCache) goto finally;

	tblCache->tbl = closetbl(tblCache->tbl);
	tblCache = TXfree(tblCache);
finally:
	return(NULL);
}

/******************************************************************/
/* Close a data dictionary.  This closes a data dictionary, and all the
 * System tables associated with it, and frees any memory allocated.
 *
 * Returns NULL
 */

DDIC *
ddclose(ddic)
DDIC *ddic;	/* The data dictionary to close. */
{
	if (ddic != (DDIC *)NULL)
	{
#ifndef NO_KEEP_STATS
		if (ddic->ihstmt)
		{
			SQLFreeStmt(ddic->ihstmt, SQL_DROP);
			ddic->ihstmt = NULL;
		}
		if (ddic->dbc && ddic->dbcalloced)
		{
			LPDBC lpdbc = ddic->dbc;

			if(lpdbc->datasource)
				free(lpdbc->datasource);
			free(lpdbc);
		}
#endif
		TXunsetstddic(ddic);
		ddic->tbspc = TXfree(ddic->tbspc);
		ddic->indspc = TXfree(ddic->indspc);
		if (ddic->indrctspc != ddic->pname)
			ddic->indrctspc = TXfree(ddic->indrctspc);
		ddic->pname = TXfree(ddic->pname);
		ddic->epname = TXfree(ddic->epname);
		if (ddic->tabletbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSTABLES, ddic->tid);
			ddic->tabletbl = closetbl(ddic->tabletbl);
		}
		if (ddic->coltbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSCOLUMNS, ddic->cid);
			ddic->coltbl = closetbl(ddic->coltbl);
		}
		if (ddic->indextbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSINDEX, ddic->iid);
			ddic->indextbl = closetbl(ddic->indextbl);
		}
		if (ddic->userstbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSUSERS, ddic->uid);
			ddic->userstbl = closetbl(ddic->userstbl);
		}
		if (ddic->permstbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSPERMS, ddic->pid);
			ddic->permstbl = closetbl(ddic->permstbl);
		}
		if (ddic->trigtbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSTRIG, ddic->rid);
			ddic->trigtbl = closetbl(ddic->trigtbl);
		}
#ifdef HAVE_LINK_TABLES
		if (ddic->linktbl != (TBL *)NULL)
		{
			if (ddic->dblock != (DBLOCK *)NULL)
				delltable(ddic->pmbuf, ddic->dblock,
					  TEXISSYSLINK, ddic->lid);
			ddic->linktbl = closetbl(ddic->linktbl);
		}
#endif /* HAVE_LINK_TABLES */

		if (ddic->tablendx != (BTREE *)NULL)
			ddic->tablendx = closebtree(ddic->tablendx);
		if (ddic->colndx != (BTREE *)NULL)
			ddic->colndx = closebtree(ddic->colndx);
		if (ddic->coltblndx != (BTREE *)NULL)
			ddic->coltblndx = closebtree(ddic->coltblndx);
		if (ddic->indexndx != (BTREE *)NULL)
			ddic->indexndx = closebtree(ddic->indexndx);
		ddic->perms = TXfree(ddic->perms);
		if (ddic->ddcache)
			ddic->ddcache = TXclosecache(ddic->ddcache);
		if (ddic->dblock != (DBLOCK *)NULL)
			ddic->dblock = closedblock(ddic->pmbuf, ddic->dblock,
						   ddic->sid,
						   TXbool_False /*!readOnly*/);
#ifndef NO_CACHE_TABLE
		ddic->indtblcache = TXtblcacheClose(ddic->indtblcache);
		ddic->tbltblcache = TXtblcacheClose(ddic->tbltblcache);
		ddic->prmtblcache = TXtblcacheClose(ddic->prmtblcache);
		ddic->usrtblcache = TXtblcacheClose(ddic->usrtblcache);
#ifdef HAVE_LINK_TABLES
		ddic->lnktblcache = TXtblcacheClose(ddic->lnktblcache);
#endif /* HAVE_LINK_TABLES */
#endif
                ddic->pmbuf = txpmbuf_close(ddic->pmbuf);
		ddic = TXfree(ddic);
	}
	return ddic;
}

