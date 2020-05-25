/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldcmp.h"
#include "fldops.h"
#include "queue.h"
#include "cgi.h"

const char	TXbufferTooSmallDataTruncated[] =
	"Buffer too small: data truncated";

/******************************************************************/

GROUPBY_INFO *
TXcloseginfo(ginfo)
GROUPBY_INFO *ginfo;
{
	if(ginfo)
	{
		if(ginfo->statloc)
		{
			NFLDSTAT *nfs = NULL;
			BTLOC btloc;

			rewindbtree(ginfo->statloc);
			do
			{
				btloc=btgetnext(ginfo->statloc,NULL,NULL,NULL);
				nfs = (NFLDSTAT *)TXgetoff(&btloc);
				if(TXrecidvalid(&btloc))
					TXclosenewstats(&nfs);
			}
			while(TXrecidvalid(&btloc));
			closebtree(ginfo->statloc);
		}
		if(ginfo->tmptbl)
			ginfo->tmptbl = closedbtbl(ginfo->tmptbl);
		if(ginfo->fc.tbl1)
			ginfo->fc.tbl1 = closetbl(ginfo->fc.tbl1);
		if(ginfo->fc.tbl2)
			ginfo->fc.tbl2 = closetbl(ginfo->fc.tbl2);
		TXclosenewstats(&ginfo->origfldstat);
		ginfo->cmpbuf = TXfree(ginfo->cmpbuf);
		ginfo = TXfree(ginfo);
	}
	return NULL;
}

GROUPBY_INFO *
TXopenGroupbyinfo()
{
	static CONST char	fn[] = "TXopenGroupbyinfo";
	GROUPBY_INFO		*ginfo;

	ginfo = (GROUPBY_INFO *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(GROUPBY_INFO));
	if (!ginfo) goto done;
	/* rest cleared by calloc() */
	/* Bug 4720: GROUPBY_INFO.cmpbuf must be TX_ALIGN_BYTES-aligned;
	 * let malloc do it:
	 */
#define SZ	8192
	ginfo->cmpbuf = (byte *)TXmalloc(TXPMBUFPN, fn, SZ);
	if (!ginfo->cmpbuf)
	{
		ginfo = TXfree(ginfo);
		goto done;
	}
	ginfo->cmpbufAllocedSz = SZ;
#undef SZ
done:
	return(ginfo);
}

/******************************************************************/
/* Perform the operations requested in query */

static int groupbysetup2 ARGS((QNODE *, FLDOP *));

int
groupbysetup(query, fo)
QNODE *query;
FLDOP *fo;
{
	static CONST char	fn[] = "groupbysetup";
	TXPMBUF			*pmbuf;
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo;
	TXCOUNTINFO	*countInfo = &query->countInfo;
	RECID		*tupWriteRes;
	DD		*dd;
	QNODE		*realInputQnode;
	DBTBL		*realInputDbtbl;
	TXDEMUX		*demux = TXDEMUXPN;

/*
 *    We are grouping the rows coming to us.
 */
	FLD *tf = NULL;
	int i;

	pmbuf = q->out->ddic->pmbuf;

	if (query->state == QS_ACTIVE)	/* Already done setup? */
		return 1;

	if(q->out->ddic->optimizations[OPTIMIZE_GROUP])
		return groupbysetup2(query, fo);

	/* rowsMatchedMin/max will be updated by tup_read();
	 * actual GROUP BY code must update rowsReturnedMin/Max,
	 * overriding tup_read()'s changes:
	 */
	countInfo->rowsReturnedMin = 0;
	countInfo->rowsReturnedMax = countInfo->rowsMatchedMax;

	ginfo = TXopenGroupbyinfo();
	if(!ginfo)
		return -1;
	q->usr=ginfo;

	/* Get the real (pre-DEMUX) input QNODE/DBTBL; since DEMUX_OP
	 * breaks strlsts into varchars, drill past it to real input:
	 */
	realInputQnode = query->right;
	realInputDbtbl = q->in1;
	if (query->right->op == DEMUX_OP)
	{
		realInputQnode = query->right->right;
		realInputDbtbl = query->right->q->in1;
		demux = (TXDEMUX *)query->right->q->usr;
	}

	/* Check if the (pre-DEMUX) input uses an index (?):
	 * (see same check in preparequery()):
	 */
	if (realInputQnode->op == SELECT_OP &&
	    projcmp(q->proj, realInputQnode->q->in1->order))
		query->ordered++;
	if (realInputQnode->op == NAME_OP &&
	    projcmp(q->proj, realInputQnode->q->out->order))
		query->ordered++;
	if (query->ordered)
		TXqnodeRewindInput(query);

	/* KNG 20090429 We are not doing OPTIMIZE_INDEXDATAGROUP here
	 * (because this is old code?), so if index is already
	 * split on strlst, warn that results may be incorrect:
	 * `realInputDbtbl' will read the index's already-split-out
	 * varchars, but return the original table strlst values instead
	 * (because `realInputDbtbl->index.indexdataonly' not set).
	 * The DEMUX node will then split these redundantly, giving
	 * too many input rows to GROUP_BY (or if DEMUX is off, GROUP_BY
	 * will see the same strlst for different split-out values;
	 * either way it is incorrect).
	 * Could fix this by doing OPTIMIZE_INDEXDATAGROUP here?
	 */
	if (realInputDbtbl->index.btree != BTREEPN &&
	    (dd = btreegetdd(realInputDbtbl->index.btree)) != DDPN &&
	TXgetMultiValueSplitFldIdx(q->proj, dd, realInputDbtbl->tbl->dd) >= 0)
		txpmbuf_putmsg(pmbuf, MWARN, fn, "Results may be incorrect: Using indexvalues-split index %s but groupby/indexdatagroupby optimizations are off",
			getdbffn(realInputDbtbl->index.btree->dbf));

#ifndef NO_ORDERED_GROUP
	if (query->ordered)	/* Don't do much work */
	{
		TXdeltmprow(realInputDbtbl);
		if (TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == -1)
		{				/* EOF? */
			/* rowsMatchedMin/Max handled by tup_read/TXdotree */
			countInfo->rowsReturnedMax =
				countInfo->rowsReturnedMin;
			return -1;
		}
		q->nrows += 1;
		query->state = QS_ACTIVE;
		tupWriteRes = tup_write(q->out, q->in1, fo, 1);
		TXqnodeRewindInput(query);
		return(tupWriteRes ? 1 : 0);
	}
#endif /* !NO_ORDERED_GROUP */

/*
   What we do here is copy the table to q->out, while
   creating an index on it for later lookups.

   q->in1  -  the source of the data
   q->in2  -  out index: unique RAM index on the GROUP BY columns of `q->in1'
   q->out  -  output table, and input to the next guy
 */

#ifndef NO_DBF_IOCTL
	/* KNG 20090413 Disable DBF_AUTO_SWITCH/RDBF_TOOBIG for now;
	 * breaks recid-to-stats-struct association.  Downside is
	 * potentially excessive RAM usage.  See Bug 2582:
	 */
	/* ioctldbtbl(q->in1, DBF_AUTO_SWITCH, NULL); */
#endif
	while (TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == 0)
	{					/* for each input row read */
		RECID *idx;

		if (TXverbosity >= 3)
			txpmbuf_putmsg(pmbuf, MINFO, fn, "Indexing record");
		/* Copy the current tuple to `q->out': */
		idx = tup_write(q->out, q->in1, fo, 0);
		if (!TXrecidvalid(idx))
			break;
		tup_index(q->in1, q->in2, q->proj,
			  fo, idx);		/* unique index on group-by */
		/* KNG 20090413 Disable DBF_AUTO_SWITCH/RDBF_TOOBIG for now;
		 * breaks recid-to-stats-struct association.  Downside is
		 * potentially excessive RAM usage.  See Bug 2582:
		 */
#if 0 /* was !NO_DBF_IOCTL */		/* Switch to real file */
		if (ioctldbtbl(q->out, RDBF_TOOBIG, NULL) > 0)
		{
			BTREE *bt2;
			BTREE *bt;

			ioctldbtbl(q->out, DBF_MAKE_FILE, NULL);
			bt = q->in2->index.btree;
			bt2 = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
			ioctldbf(bt2->dbf, DBF_MAKE_FILE, NULL);
			btreesetdd(bt2, btreegetdd(bt));
			/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
			 * stringcomparemode:
			 */
			BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt2->params, q->in2->ddic);

			bt2->usr = bt->usr;
			bt2->cmp = bt->cmp;
			closebtree(bt);
			q->in2->index.btree = bt2;
			for (i = 0; i < q->proj->n; i++)
				pred_rmalts(q->proj->preds[i]);
			TXrewinddbtbl(q->out);
			while ((index = getdbtblrow(q->out)), TXrecidvalid(index))
				tup_index(q->out, q->in2, q->proj, fo, index);
		}
		if (ioctldbtbl(q->in2, RDBF_TOOBIG, NULL) > 0)
		{
		}
#endif /* NO_DBF_IOCTL */
	}					/* for each input row read */
	if (demux != TXDEMUXPN)			/* reset just to make sure */
		TXdemuxReset(demux);

	for (i = 0, tf = TXgetrfldn(q->in2->tbl, i, NULL);
	     tf; i++)
	{
		freeflddata(tf);
		tf = TXgetrfldn(q->in2->tbl, i, NULL);
	}
	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;
	TXrewinddbtbl(q->out);
	TXrewinddbtbl(q->in2);

	/* KNG 20081211 propagate match row counts from SELECT (query->right),
	 * which we completely read, up to our GROUP_BY node (query).
	  `rowsReturnedMin'/Max not determined yet:
	 */
	countInfo->rowsMatchedMin = query->right->countInfo.rowsMatchedMin;
	countInfo->rowsMatchedMax = query->right->countInfo.rowsMatchedMax;
	countInfo->indexCount = query->right->countInfo.indexCount;

	/* KNG 20090504 was a rewind here if query->ordered, but we already
	 * bailed above if query->ordered
	 */

	return 0;
}

/******************************************************************/

static RECID *nextrow ARGS((QNODE *, FLDOP *));

static RECID *
nextrow(query, fo)
QNODE *query;
FLDOP *fo;
{
	QUERY *q = query->q;
	RECID *rc = NULL;

	(void)fo;
	rc = getdbtblrow(q->in2);	/* Where is the next row to read */
	return rc;
}

/******************************************************************/
/*
   This will do the group by for an ordered input
 */

static int orderedgroupby ARGS((QNODE *, FLDOP *));

static int
orderedgroupby(query, fo)
QNODE *query;
FLDOP *fo;
{
	/* Can we assume there is an index, and it is on what we
	   are grouping by?  No.  We will need to maintain a dummy
	   table that will hold the group by fields.  Copy those
	   to there to build the compare buffer, and then use that */

	/*
		1 - Reset stats
		2 - Read a row, do stats
		3 - Build cmpbuf.
		4 - Copy to out, do stats on out
		5 - If same as last row repeat at 2
		6 - If different, return -1;
	*/
	static const char	fn[] = "orderedgroupby";
	TXPMBUF			*pmbuf;
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo = q->usr;
	TXCOUNTINFO	*countInfo = &query->countInfo;
	TXCOUNTINFO	*subCountInfo;
	int		diff, dotreeRes, ret;

	pmbuf = q->out->ddic->pmbuf;

	if(!ginfo->tmptbl)
		ginfo->tmptbl = TXtup_project_setup(q->in1, q->proj, fo, 0);
	if(!ginfo->tmptbl)
		return -1;

	ginfo->fc.fo = fo;
	if(!ginfo->fc.tbl1)
		ginfo->fc.tbl1 = createtbl(ginfo->tmptbl->tbl->dd, NULL);
	if(!ginfo->fc.tbl2)
		ginfo->fc.tbl2 = createtbl(ginfo->tmptbl->tbl->dd, NULL);

	if(ginfo->dontread)
		dbresetstats(q->out);
	if(ginfo->dontread && !ginfo->where)
		return -1;
nextrow:
	dotreeRes = -1;				/* e.g. EOF */
	if(!ginfo->dontread)
	{
		/* KNG 20090529 Bug 2397 was tup_read(q->in1), which fails
		 * with new DEMUX_OP node `query->right':
		 */
		dotreeRes = TXdotree(query->right, fo, SQL_FETCH_NEXT, 1);
		/* Propagate up all counts except rowsReturnedMin/Max;
		 * we determine those here in this function:
		 */
		subCountInfo = &query->right->countInfo;
		countInfo->rowsMatchedMin = subCountInfo->rowsMatchedMin;
		countInfo->rowsMatchedMax = subCountInfo->rowsMatchedMax;
		countInfo->indexCount = subCountInfo->indexCount;
	}
	if (!ginfo->dontread && (dotreeRes != 0) && ginfo->where)
	{					/* EOF? */
		if(ginfo->tmptbl)
			ginfo->tmptbl = closedbtbl(ginfo->tmptbl);
		if(ginfo->fc.tbl1)
			ginfo->fc.tbl1 = closetbl(ginfo->fc.tbl1);
		if(ginfo->fc.tbl2)
			ginfo->fc.tbl2 = closetbl(ginfo->fc.tbl2);

		/* EOF, so we know max = min now: */
		countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;

		ginfo->dontread = 0;
		if(ginfo->cmpbufsz > 0 &&
		   q->out->needstats == 0 &&
		   ginfo->where)
		{
			ginfo->dontread = 1;
			ginfo->where = 0;
			return 0;
		}
		return -1;
	}
	ginfo->where = &q->in1->recid;
	tup_project(q->in1, ginfo->tmptbl, q->proj, fo);
	if(ginfo->cmpbufsz == 0)		/* this is first row */
	{
		size_t	sz = ginfo->tmptbl->tbl->orecdatasz;

		if (sz > ginfo->cmpbufAllocedSz)
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
				       TXbufferTooSmallDataTruncated);
			sz = ginfo->cmpbufAllocedSz;
		}
		memcpy(ginfo->cmpbuf, ginfo->tmptbl->tbl->orec, sz);
		ginfo->cmpbufsz = sz;
		countInfo->rowsReturnedMin++;	/* at least one result */
	}
	diff = fldcmp(ginfo->tmptbl->tbl->orec, ginfo->tmptbl->tbl->orecdatasz, ginfo->cmpbuf, ginfo->cmpbufsz, &ginfo->fc);
/*
	tup_disp(q->in1, fo);
	tup_disp(tmptbl, fo);
	putmsg(999, NULL, "diff = %d", diff);
*/
	if(diff)				/* row starts new group */
	{
		size_t	sz = ginfo->tmptbl->tbl->orecdatasz;

		if (sz > ginfo->cmpbufAllocedSz)
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
				       TXbufferTooSmallDataTruncated);
			sz = ginfo->cmpbufAllocedSz;
		}
		ginfo->dontwrite = 0;
		memcpy(ginfo->cmpbuf, ginfo->tmptbl->tbl->orec, sz);
		ginfo->cmpbufsz = sz;
		ginfo->dontread = 1;

		/* KNG 20081212 update row counts.  tup_read() takes care
		 * of `rowsMatchedMin'/max; we update returned rows --
		 * which are reduced by the GROUP BY -- here:
		 */
		countInfo->rowsReturnedMin++;	/* new group -> new row */

		if(ginfo->cmpbufsz > 0 && q->out->needstats == 0)
			ret = 0;
		else
			ret = -1;
	}
	else					/* row in same group as prev.*/
	{
		ginfo->dontread = 0;
		if((0 == ginfo->dontwrite) || (0 == q->out->nfldstatcountonly))
		{
			ginfo->dontwrite = 1;
			tup_write(q->out, q->in1, fo, 1);
		}
		if(ginfo->cmpbufsz > 0 && q->out->needstats == 0)
			goto nextrow;
		dostats(q->out, fo);
		ret = 0;
	}
	/* Bug 4770: we are done with the projected row in
	 * `ginfo->tmptbl', so delete it to avoid accumulating GROUP
	 * BY columns in mem for all input rows.  We could not use
	 * TXNOOPDBF for `tmptbl' (and thus avoid this tup_delete())
	 * because we do need the fldtobuf() `orec' data from it:
	 */
	if (q->out->ddic->optimizations[OPTIMIZE_GROUP_BY_MEM])
	{
		tup_delete(DBTBLPN, ginfo->tmptbl);
		TXsetrecid(&ginfo->tmptbl->recid, -1);
	}
	return(ret);
}

/******************************************************************/

static int groupby2 ARGS((QNODE *, FLDOP *));

int
groupby(query, fo)
QNODE *query;
FLDOP *fo;
{
	static const char	fn[] = "groupby";
	TXPMBUF			*pmbuf;
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo = q->usr;
	TXCOUNTINFO	*countInfo = &query->countInfo, prevCountInfo;
	int diff = 0, rc;

/*
 *    We are grouping the rows coming to us.
 */
	pmbuf = q->out->ddic->pmbuf;

#ifndef NO_ORDERED_GROUP
	if(query->ordered)
		return orderedgroupby(query, fo);
#endif
	if(q->out->ddic->optimizations[OPTIMIZE_GROUP])
		return groupby2(query, fo);
nextgroup:
#ifdef NEVER
	putmsg(999, NULL,
	       "sz %d, diff %d, statstat %d, rc %d, dontread %d",
	       sz, diff, statstat, rc, dontread);
#endif
	if (!ginfo->dontread)	/* Do I already know which row to read */
	{
		prevCountInfo = query->countInfo;
		ginfo->where = nextrow(query, fo);/* Find next row location */
		/* Ignore tup_read()'s mods to rowsReturnedMin/Max;
		 * we determine those here in this function:
		 */
		query->countInfo.rowsReturnedMin =
			prevCountInfo.rowsReturnedMin;
		query->countInfo.rowsReturnedMax =
			prevCountInfo.rowsReturnedMax;

		if (!TXrecidvalid(ginfo->where))	/* No more rows. */
		{
			ginfo->dontread = 0;
			ginfo->statstat = 0;
			/* EOF, so we know max = min now: */
			countInfo->rowsReturnedMax =
				countInfo->rowsReturnedMin;
			return -1;
		}

		if (ginfo->sz == 0)		/* first row read */
			countInfo->rowsReturnedMin++;	/* at least one */

		/* Is this the same as the last row I read from index */

		ginfo->tsz = fldtobuf(q->in2->tbl);
		if (ginfo->sz > 0 && ginfo->tsz > 0)
			diff = fldcmp(ginfo->cmpbuf, ginfo->sz,
				q->in2->tbl->orec, ginfo->tsz,
				q->in2->index.btree->usr);

		/* If it is different then hand up previous */

		if (ginfo->statstat && ginfo->sz > 0 && diff != 0 && q->out->needstats)
		{
			countInfo->rowsReturnedMin++;	/* new row later */
			/* Pump it up for aggregates */
			ginfo->dontread = 1;
			ginfo->statstat = 0;
			return -1;
		}
	}
	else
		/* Have a row already.  Must be first of a new block */
	{
		rc = 1;
		dbresetstats(q->out);
		ginfo->dontread = 0;
		diff = -1;
	}

	if (TXlocktable(q->out, R_LCK) == -1)
	{
		if (texispeekerr(q->out->ddic) == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
			return -2;
		return -1;
	}
#ifdef ASSERT_DEL_BLOCK
	if (!validrow(q->out->tbl, where))
	{
		TXunlocktable(q->out, R_LCK);
		return -1;
	}
	rc = TXrecidvalid(gettblrow(q->out->tbl, where)) ? 1 : -1;
#else
	rc = gettblrow(q->out->tbl, ginfo->where) ? 1 : -1;
#endif
#ifdef NEVER
	putmsg(999, NULL,
	       "sz %d, diff %d, statstat %d, rc %d, dontread %d",
	       sz, diff, statstat, rc, dontread);
#endif
	if (ginfo->statstat && ginfo->sz > 0 && diff != 0 && q->out->needstats)
	{
		countInfo->rowsReturnedMin++;	/* new row later */
		/* Pump it up for aggregates */
		ginfo->dontread = 1;
		ginfo->statstat = 0;
		TXunlocktable(q->out, R_LCK);
		return -1;
	}
	ginfo->statstat = 1;

	/* If it is the same as previous row, collect stats, and get another
	   row */
	if (ginfo->sz > 0 && diff == 0 && !q->out->needstats)
	{
#ifdef NEVER
		putmsg(999, NULL, "Going around again");
#endif
		dostats(q->out, fo);
		TXunlocktable(q->out, R_LCK);
		goto nextgroup;
	}
#ifdef NEVER
	putmsg(999, NULL, "Tsz = %d, sz = %d, diff %d", tsz, sz, diff);
#endif
	/* Remember the row for future comparison */
	{
		size_t	sz = ginfo->tsz;

		if (sz > ginfo->cmpbufAllocedSz)
		{
			txpmbuf_putmsg(pmbuf, MERR + MAE, fn,
				       TXbufferTooSmallDataTruncated);
			sz = ginfo->cmpbufAllocedSz;
		}
		memcpy(ginfo->cmpbuf, q->in2->tbl->orec, sz);
		ginfo->sz = sz;
	}
	if (q->out->needstats)	/* Collect stats */
		dostats(q->out, fo);
	TXunlocktable(q->out, R_LCK);
	if (rc != -1)
		q->nrows += 1;
	return rc;
}
/******************************************************************/
/* Perform the operations requested in query */

static int
groupbysetup2(query, fo)
QNODE *query;
FLDOP *fo;
{
	static const char Fn[] = "groupbysetup2";
	TXPMBUF			*pmbuf;
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo;
	TXCOUNTINFO	*countInfo = &query->countInfo, prevCountInfo;
	int		res;
	RECID		*tupWriteRes;
	QNODE		*realInputQnode;
	DBTBL		*realInputDbtbl;
	TXDEMUX		*demux = TXDEMUXPN;
	FLD *tf = NULL;
	int i;
/*
 *    We are grouping the rows coming to us.
 */

	pmbuf = q->out->ddic->pmbuf;

	if (query->state == QS_ACTIVE)	/* Already done setup? */
		return 1;

	/* rowsMatchedMin/max will be updated by tup_read();
	 * actual GROUP BY code must update rowsReturnedMin/Max,
	 * overriding tup_read()'s changes:
	 */
	countInfo->rowsReturnedMin = 0;
	countInfo->rowsReturnedMax = countInfo->rowsMatchedMax;

	ginfo = TXopenGroupbyinfo();
	if(!ginfo)
		return -1;
#if EPI_OFF_T_BITS < EPI_OS_VOIDPTR_BITS
	txpmbuf_putmsg(pmbuf, MERR + MAE, Fn,
		       "Cannot store NFLDSTAT pointers in RAM B-tree");
	return(-1);
#endif
	q->usr=ginfo;
	ginfo->statstat = 1;
	ginfo->statloc = openbtree(NULL, 500, 20, BT_FIXED, O_RDWR | O_CREAT);

	/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
	 * stringcomparemode:
	 */
	BTPARAM_INIT_TO_PROCESS_DEFAULTS(&ginfo->statloc->params, q->in1->ddic);

	/* Get the real (pre-DEMUX) input QNODE/DBTBL; since DEMUX_OP
	 * breaks strlsts into varchars, drill past it to real input:
	 */
	realInputQnode = query->right;
	realInputDbtbl = q->in1;
	if (query->right->op == DEMUX_OP)
	{
		realInputQnode = query->right->right;
		realInputDbtbl = query->right->q->in1;
		demux = (TXDEMUX *)query->right->q->usr;
	}

	/* Check if the (pre-DEMUX) input uses an index (?):
	 * (see same check in preparequery()):
	 */
	if (realInputQnode->op == SELECT_OP &&
	    projcmp(q->proj, realInputQnode->q->in1->order))
		query->ordered++;
	if (realInputQnode->op == NAME_OP &&
	    projcmp(q->proj, realInputQnode->q->out->order))
		query->ordered++;
	if (query->ordered)
		TXqnodeRewindInput(query);

	if(q->in1->ddic->optimizations[OPTIMIZE_INDEXDATAGROUP])
	{
		DD	*dd, *groupByInputDd;

		/* Determine if index (if any) on the input `realInputDbtbl'
		 * contains all of the fields we will need here and later
		 * (e.g. `SELECT fld FROM myTable GROUP BY fld', with index
		 * on `fld').  If so, set `ginfo->indexonly':
		 */
		ginfo->indexonly = 0;
		if(realInputDbtbl->index.btree &&
		   (dd = realInputDbtbl->index.btree->datad) != NULL &&
		   (groupByInputDd = q->in1->tbl->dd) != NULL)
		{
			ginfo->indexonly = 1;
			for(i = 0; i < query->fldlist->cnt -1; i++)
			{
				int	indexDdIdx, groupByInputDdIdx;
				char	*fldName = query->fldlist->s[i];

				/* KNG 20120305 types must also agree;
				 * see TXtup_project_setup() comment.
				 * We compare index field type to *GROUP BY*
				 * input field type, not to `realInputDbtbl'
				 * field type: GROUP BY input is always
				 * same type as final output, demux or not:
				 */
				if ((indexDdIdx = ddfindname(dd, fldName)) < 0 ||
				    (groupByInputDdIdx = ddfindname(groupByInputDd, fldName)) < 0 ||
				    (dd->fd[indexDdIdx].type & FTN_VarBaseTypeMask) !=
				    (groupByInputDd->fd[groupByInputDdIdx].type & FTN_VarBaseTypeMask))
				{
					ginfo->indexonly = 0;
					break;
				}
			}
		}
		if(ginfo->indexonly)		/* can use index w/o table */
		{
			/* Set `index.indexdataonly' so that
			 * tup_read_indexed() knows it need only read from
			 * the index, and can skip the read of the table:
			 */
			realInputDbtbl->index.indexdataonly = 1;
			if(!realInputDbtbl->index.indexdbtbl)
			{
				/* Open a DBTBL so tup_read_indexed() has
				 * proper DD to convert index data to fields:
				 */
				realInputDbtbl->index.indexdbtbl =
				    TXopentmpdbtbl(NULL, NULL, NULL,
					realInputDbtbl->index.btree->datad,
					realInputDbtbl->ddic);
			}
			if(query->ordered) /* WTF - Prime so no nulls */
				getdbtblrow(realInputDbtbl);
		}
		if (TXverbosity >= 1)
		{
			txpmbuf_putmsg(pmbuf, MINFO, Fn, "ginfo->indexonly %d",
				      ginfo->indexonly);
		}
		/* WTF now that we will be reading already-split (varchar)
		 * values, can we short-circuit/remove the DEMUX node,
		 * to avoid wasteful varchar-to-strlst (tup_read_indexed())
		 * and strlst-to-varchar (TXdemuxGetNextRow())?  Difficult
		 * because TXDEMUX schemas are already in place.  Could we
		 * do OPTIMIZE_INDEXDATAGROUP earlier, e.g. at the time we
		 * call TXdemuxAddDemuxQnodeIfNeeded(), so we could know
		 * to avoid the TXDEMUX at that point?
		 */
	}					/*if OPTIMIZE_INDEXDATAGROUP*/
#ifndef NO_ORDERED_GROUP
	if (query->ordered)	/* Don't do much work */
	{
		TXdeltmprow(realInputDbtbl);
		prevCountInfo = query->right->countInfo;
		res = TXdotree(query->right, fo, SQL_FETCH_NEXT, 1);
		/* Ignore tup_read()'s mods to rowsReturnedMin/Max;
		 * we determine those here in this function or groupby[2]():
		 */
		query->right->countInfo.rowsReturnedMin =
			prevCountInfo.rowsReturnedMin;
		query->right->countInfo.rowsReturnedMax =
			prevCountInfo.rowsReturnedMax;

		if (res == -1)			/* EOF */
		{
			/* rowsMatchedMin/Max handled by tup_read/TXdotree */
			countInfo->rowsReturnedMax =
				countInfo->rowsReturnedMin;
			return -1;
		}
		q->nrows += 1;
		query->state = QS_ACTIVE;
		tupWriteRes = tup_write(q->out, q->in1, fo, 1);
		if (TXqnodeRewindInput(query) == 2)
		{				/* rewound an index */
			/* KNG 20081211 `query->right' NAME_OP (DEMUX_OP?)
			 * will still have a TXCOUNTINFO row counted that
			 * we are unwinding here, but it should be ignored
			 * as only the `query' GROUP_BY_OP row counts should
			 * propagate up:
			 */
		}
		else
		/* KNG 20081211 credit the GROUP_BY_OP node with the read: */
			query->countInfo = query->right->countInfo;
		return(tupWriteRes ? 1 : 0);
	}					/* if (query->ordered) */
#endif /* !NO_ORDERED_GROUP */

/*
   What we do here is copy the table to q->out, while
   creating an index on it for later lookups.

   q->in1  -  the source of the data
   q->in2  -  out index: unique RAM index on the GROUP BY columns of `q->in1'
   q->out  -  output table, and input to the next guy
 */

#ifndef NO_DBF_IOCTL
	/* KNG 20090413 Disable DBF_AUTO_SWITCH/RDBF_TOOBIG for now;
	 * breaks recid-to-stats-struct association.  Downside is
	 * potentially excessive RAM usage.  See Bug 2582:
	 */
	/* ioctldbtbl(q->in1, DBF_AUTO_SWITCH, NULL); */
#endif /* !NO_DBF_IOCTL */
	if(TXverbosity >= 3)
	{
		char	*ddSchema;

		ddSchema = TXdbtblSchemaToStr(q->in2, 2);
		txpmbuf_putmsg(pmbuf, MINFO, Fn,
			       "Indexing records using schema %s", ddSchema);
		ddSchema = TXfree(ddSchema);
	}
	while (TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == 0)
	{					/* for each input row read */
		RECID *idx, *idx2;
		RECID index3;
		NFLDSTAT *nfs;

		/* Copy the current tuple to `q->out': */
		idx = tup_write(q->out, q->in1, fo, 0);
		if (!TXrecidvalid(idx))
			break;
		idx2 = tup_index(q->in1, q->in2, q->proj,
				  fo, idx);	/* unique index on group-by */
		if(TXverbosity >= 3)
		{
			char	*dbtblTuple = NULL, recidBuf[256];;

			dbtblTuple = TXdbtblTupleToStr(q->in2);
			if (idx2)
				htsnpf(recidBuf, sizeof(recidBuf), "0x%wx",
				       (EPI_HUGEINT)TXgetoff2(idx2));
			else
				strcpy(recidBuf, "(none)");
			txpmbuf_putmsg(pmbuf, MINFO, Fn,
				       "Indexed record %s: got recid %s",
				       dbtblTuple, recidBuf);
			dbtblTuple = TXfree(dbtblTuple);
		}
		if(idx2)			/* starts new group */
		{
			countInfo->rowsReturnedMin++;
			nfs = TXdupnewstats(q->out->nfldstat);
			TXaddstatrow(nfs, q->out, fo);
			TXsetrecid(&index3, nfs);
			btinsert(ginfo->statloc, &index3, sizeof(RECID), idx);
		}
		else				/* same as an earlier group */
		{
			freedbf(q->out->tbl->df, TXgetoff(idx));
			/* Get the recid where we last saw this group.
			 * wtf can we obtain this from the failed tup_index()
			 * above (which should already point to it), to
			 * avoid another B-tree search here?  KNG
			 */
			idx2 = tup_index_search(q->in1,
						q->in2, q->proj, fo, RECIDPN);
			if(idx2)
			{
				/* Get the stats struct for that recid.
				 * wtf can we put the stats struct directly
				 * in the RAM table, to avoid another B-tree
				 * search here?  might be hard once the DBF
				 * flips from RAM to disk though...  KNG
				 */
				index3 = btsearch(ginfo->statloc,
					sizeof(RECID),idx2);
				if (TXrecidvalid2(&index3))
				{
					nfs = (NFLDSTAT *)TXgetoff(&index3);
					TXaddstatrow(nfs, q->out, fo);
				}
				/* KNG 011213 WTF why is this -1 for a groupby
				 * with identical fields > max_index_text?
				 */
				else if (TXverbosity >= 1)
					txpmbuf_putmsg(pmbuf, MERR, Fn,
						       "Invalid NFLDSTAT *");
				idx2 = TXfree(idx2);
			}
		}
		/* KNG 20090413 Disable DBF_AUTO_SWITCH/RDBF_TOOBIG for now;
		 * breaks recid-to-stats-struct association.  Downside is
		 * potentially excessive RAM usage.  See Bug 2582:
		 */
#if 0 /* was !NO_DBF_IOCTL */		/* Switch to real file */
		if (ioctldbtbl(q->out, RDBF_TOOBIG, NULL) > 0)
		{
			BTREE *bt2;
			BTREE *bt;

			ioctldbtbl(q->out, DBF_MAKE_FILE, NULL);

			/* Need to fix statloc */
			if(ginfo->statloc)
			{
				BTLOC tl1, *tl2;

				bt = openbtree(NULL, 500, 20, BT_FIXED, O_RDWR | O_CREAT);

				/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
				 * stringcomparemode:
				 */
				BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt->params, q->out->ddic);

				TXrewinddbtbl(q->out);
				rewindbtree(ginfo->statloc);
				while((tl1 = btgetnext(ginfo->statloc, NULL,
						NULL, NULL))
					,TXrecidvalid(&tl1))
				{
					tl2 = getdbtblrow(q->out);
					if(TXrecidvalid(tl2))
						btinsert(bt, &tl1, sizeof(RECID), tl2);
				}
				closebtree(ginfo->statloc);
				ginfo->statloc = bt;
			}

			bt = q->in2->index.btree;
			bt2 = openbtree(NULL, BT_MAXPGSZ, 20, 0, O_RDWR | O_CREAT);
			ioctldbf(bt2->dbf, DBF_MAKE_FILE, NULL);
			btreesetdd(bt2, btreegetdd(bt));

			/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
			 * stringcomparemode:
			 */
			BTPARAM_INIT_TO_PROCESS_DEFAULTS(&bt2->params, q->in2->ddic);

			bt2->usr = bt->usr;
			bt2->cmp = bt->cmp;
			bt2->flags = bt->flags;
			closebtree(bt);
			q->in2->index.btree = bt2;
			for (i = 0; i < q->proj->n; i++)
				pred_rmalts(q->proj->preds[i]);
			TXrewinddbtbl(q->out);
			while ((index = getdbtblrow(q->out)), TXrecidvalid(index))
				tup_index(q->out, q->in2, q->proj, fo, index);
		}
		if (ioctldbtbl(q->in2, RDBF_TOOBIG, NULL) > 0)
		{
		}
#endif /* NO_DBF_IOCTL */
	}					/* for each input row read */
	if (demux != TXDEMUXPN)			/* reset just to make sure */
		TXdemuxReset(demux);

	for (i = 0, tf = TXgetrfldn(q->in2->tbl, i, NULL);
	     tf; i++)
	{
		freeflddata(tf);
		tf = TXgetrfldn(q->in2->tbl, i, NULL);
	}
	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;
	TXrewinddbtbl(q->out);
	TXrewinddbtbl(q->in2);

	/* KNG 20081211 propagate match row counts from SELECT (query->right),
	 * which we completely read, up to our GROUP_BY node (query).
	 * `rowsReturnedMin' was determined above (inside loop),
	 * but now that we are at EOF we can also set `rowsReturnedMax':
	 */
	countInfo->rowsMatchedMin = query->right->countInfo.rowsMatchedMin;
	countInfo->rowsMatchedMax = query->right->countInfo.rowsMatchedMax;
	countInfo->indexCount = query->right->countInfo.indexCount;
	countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;

	/* KNG 20090504 was a rewind here if query->ordered, but we already
	 * bailed above if query->ordered
	 */

	return 0;
}

/******************************************************************/
/*
	Note on return codes.  Returns 1 to start a group, -1 to
	end a group, and file.
*/

static int
groupby2(query, fo)
QNODE *query;
FLDOP *fo;
{
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo = q->usr;
	RECID index3;
	NFLDSTAT *nfs;
	int diff = 0, rc = 0;

	/* `query->countInfo' stats already done in groupbysetup2() */
/*
 *    We are grouping the rows coming to us.
 */

#ifndef NO_ORDERED_GROUP
	if(query->ordered)
		return orderedgroupby(query, fo);
#endif
#ifdef NEVER
	putmsg(999, NULL,
	       "sz %d, diff %d, statstat %d, rc %d, dontread %d",
	       ginfo->sz, diff, ginfo->statstat, rc, ginfo->dontread);
#endif
	if (!ginfo->dontread)	/* Do I already know which row to read */
	{
		ginfo->where = nextrow(query, fo);/* Find next row location */
		if (!TXrecidvalid(ginfo->where))	/* No more rows. */
		{
			BTLOC btloc;
			ginfo->dontread = 0;
			ginfo->statstat = 0;
			if(!ginfo->statloc) /* We've already shutdown */
				return -1;	/* EOF */
			rewindbtree(ginfo->statloc);
			do
			{
				NFLDSTAT *nfs2;

				btloc=btgetnext(ginfo->statloc,NULL,NULL,NULL);
				nfs2 = (NFLDSTAT *)TXgetoff(&btloc);
				if(TXrecidvalid(&btloc))
					TXclosenewstats(&nfs2);
			}
			while(TXrecidvalid(&btloc));
			ginfo->statloc = closebtree(ginfo->statloc);
			if(ginfo->origfldstat)
			{
				q->out->nfldstat = ginfo->origfldstat;
				q->out->nfldstatisdup = ginfo->origfldstat->isdup;
				ginfo->origfldstat = NULL;
			}
			return -1;		/* EOF */
		}
	}
	else
		/* Have a row already.  Must be first of a new block */
	{
		rc = 1;
		dbresetstats(q->out);
		ginfo->dontread = 0;
		diff = -1;
	}
	if(ginfo->statstat == 1)
	{
		ginfo->statstat = 0;
		ginfo->dontread = 1;
		return 1;			/* new group */
	}

	if (TXlocktable(q->out, R_LCK) == -1)
	{
		if (texispeekerr(q->out->ddic) == MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
			return -2;
		return -1;			/* EOF */
	}
#ifdef ASSERT_DEL_BLOCK
	if (!validrow(q->out->tbl, where))
	{
		TXunlocktable(q->out, R_LCK);
		return -1;			/* EOF */
	}
	rc = TXrecidvalid(gettblrow(q->out->tbl, where)) ? 1 : -1;
#else
	rc = gettblrow(q->out->tbl, ginfo->where) ? 1 : -1;
#endif
	index3 = btsearch(ginfo->statloc,sizeof(RECID),ginfo->where);
	nfs = (NFLDSTAT *)TXgetoff(&index3);
	rc = ginfo->statstat;
	ginfo->statstat = 1;
	if(q->out->nfldstat)
	{
		if(!q->out->nfldstat->isdup)
			ginfo->origfldstat = q->out->nfldstat;
	}
	q->out->nfldstat = nfs;
	if(nfs)
		q->out->nfldstatisdup = nfs->isdup;
	else
		q->out->nfldstatisdup = 0;
	TXunlocktable(q->out, R_LCK);
	q->nrows += 1;
	return -1;				/* EOF */
}

/******************************************************************/
