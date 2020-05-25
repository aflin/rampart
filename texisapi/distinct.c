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

extern const char	TXbufferTooSmallDataTruncated[];

/******************************************************************

   WTF: Plan to improve performance.

   Create index as now, but don't run through all the records first,
   just as we pile through spit out ones we could insert.

 ******************************************************************/


/******************************************************************/
/* Perform the operations requested in query */

int
TXdistinctsetup(query, fo)
QNODE *query;
FLDOP *fo;
{
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo;
	RECID	*tupWriteRes;
	QNODE		*realInputQnode;
	DBTBL		*realInputDbtbl;
	TXDEMUX		*demux = TXDEMUXPN;
	TXCOUNTINFO	*countInfo = &query->countInfo;

/*
 *    We are grouping the rows coming to us.
 */

	if (query->state == QS_ACTIVE)	/* Already done setup? */
		return 1;

	/* rowsMatchedMin/max will be updated by tup_read();
	 * actual DISTINCT code must update rowsReturnedMin/Max,
	 * overriding tup_read()'s changes:
	 */
	countInfo->rowsReturnedMin = 0;
	countInfo->rowsReturnedMax = countInfo->rowsMatchedMax;

	ginfo = TXopenGroupbyinfo();
	if (!ginfo)
		return -1;
	q->usr = ginfo;

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

	/* Check if the (pre-DEMUX) input uses an index (?): */
	if (realInputQnode->op == SELECT_OP &&
	    projcmp(q->proj, realInputQnode->q->in1->order))
		query->ordered++;
	if (realInputQnode->op == NAME_OP &&
	    projcmp(q->proj, realInputQnode->q->out->order))
		query->ordered++;
	if (query->ordered)
		TXqnodeRewindInput(query);

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

/*
   What we do here is copy the table to q->out, while
   creating an index on it for later lookups.

   q->in1  -  the source of the data
   q->in2  -  out index: unique RAM index on the GROUP BY columns of `q->in1'
   q->out  -  output table, and input to the next guy
 */

	q->in2->index.btree->flags = BT_UNIQUE;
	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;
	return 0;
}

/******************************************************************/
/*
   This will do the group by for an ordered input
 */
static int ordereddistinct ARGS((QNODE *query, FLDOP *fo));

static int
ordereddistinct(query, fo)
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
	static const char	fn[] = "ordereddistinct";
	QUERY *q = query->q;
	GROUPBY_INFO *ginfo = q->usr;
	TXCOUNTINFO	*subCountInfo, *countInfo = &query->countInfo;
	int		diff, dotreeRes;

	if (!ginfo->tmptbl)
		ginfo->tmptbl = TXtup_project_setup(q->in1, q->proj, fo, 0);
	if (!ginfo->tmptbl)
		return -1;

	ginfo->fc.fo = fo;
	if (!ginfo->fc.tbl1)
		ginfo->fc.tbl1 = createtbl(ginfo->tmptbl->tbl->dd, NULL);
	if (!ginfo->fc.tbl2)
		ginfo->fc.tbl2 = createtbl(ginfo->tmptbl->tbl->dd, NULL);

	if (ginfo->dontread)
		dbresetstats(q->out);

	dotreeRes = -1;				/* eg. EOF */
	if (!ginfo->dontread)
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
	if (!ginfo->dontread && (dotreeRes != 0))
	{					/* EOF? */
		if (ginfo->tmptbl)
			ginfo->tmptbl = closedbtbl(ginfo->tmptbl);
		if (ginfo->fc.tbl1)
			ginfo->fc.tbl1 = closetbl(ginfo->fc.tbl1);
		if (ginfo->fc.tbl2)
			ginfo->fc.tbl2 = closetbl(ginfo->fc.tbl2);

		/* EOF, so we know max = min now: */
		countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;

		ginfo->dontread = 0;
		return -1;
	}
	tup_project(q->in1, ginfo->tmptbl, q->proj, fo);
	if (ginfo->cmpbufsz == 0)		/* this is first row */
	{
		size_t	sz = ginfo->tmptbl->tbl->orecsz;

		if (sz > ginfo->cmpbufAllocedSz)
		{
			putmsg(MERR + MAE, fn, TXbufferTooSmallDataTruncated);
			sz = ginfo->cmpbufAllocedSz;
		}
		memcpy(ginfo->cmpbuf, ginfo->tmptbl->tbl->orec, sz);
		ginfo->cmpbufsz = sz;
		countInfo->rowsReturnedMin++;	/* at least one result */
	}
	diff =
		fldcmp(ginfo->tmptbl->tbl->orec, ginfo->tmptbl->tbl->orecsz,
		       ginfo->cmpbuf, ginfo->cmpbufsz, &ginfo->fc);
	if (diff)				/* new distinct row */
	{
		size_t	sz = ginfo->tmptbl->tbl->orecsz;

		if (sz > ginfo->cmpbufAllocedSz)
		{
			putmsg(MERR + MAE, fn, TXbufferTooSmallDataTruncated);
			sz = ginfo->cmpbufAllocedSz;
		}
		memcpy(ginfo->cmpbuf, ginfo->tmptbl->tbl->orec, sz);
		ginfo->cmpbufsz = sz;
		ginfo->dontread = 1;

		/* KNG 20090602 update row counts.  tup_read() takes care
		 * of `rowsMatchedMin'/max; we update returned rows --
		 * which are reduced by the DISINCT -- here:
		 */
		countInfo->rowsReturnedMin++;

		return -1;
	}
	else					/* same as previous row */
	{
		ginfo->dontread = 0;
		tup_write(q->out, q->in1, fo, 1);
		dostats(q->out, fo);
		return 0;
	}
}

/******************************************************************/

int
TXdistinct(query, fo)
QNODE *query;
FLDOP *fo;
{
	QUERY *q = query->q;
	FLD *tf = NULL;
	TXCOUNTINFO	*countInfo = &query->countInfo, *subCountInfo;

/*
 *    We are grouping the rows coming to us.
 */

	RECID *where;
	int rc, i;

	if (query->ordered)
		return ordereddistinct(query, fo);

	/* Read until we get a distinct row or EOF: */
	where = NULL;
	while (TXdotree(query->right, fo, SQL_FETCH_NEXT, 1) == 0)
	{					/* for each row read */
		RECID *index;

		index = tup_write(q->out, q->in1, fo, 0);
		if (!TXrecidvalid(index))	/* write error? */
			break;
		/* unique RAM index on distinct: */
		where = tup_index(q->in1, q->in2, q->proj, fo, index);
		if(!TXrecidvalid(where))	/* not a new DISTINCT value */
		{
			freedbf(q->out->tbl->df, TXgetoff(&q->out->recid));
		}
		if(TXrecidvalid(where))		/* got a DISTINCT value */
			break;
	}

	/* Propagate up all counts except rowsReturnedMin/Max;
	 * we determine those here in this function:
	 */
	subCountInfo = &query->right->countInfo;
	countInfo->rowsMatchedMin = subCountInfo->rowsMatchedMin;
	countInfo->rowsMatchedMax = subCountInfo->rowsMatchedMax;
	countInfo->indexCount = subCountInfo->indexCount;

	if (!TXrecidvalid(where))	/* No more rows. */
	{
		for (i = 0, tf = TXgetrfldn(q->in2->tbl, i, NULL); tf; i++)
		{
			freeflddata(tf);
			tf = TXgetrfldn(q->in2->tbl, i, NULL);
		}
		/* EOF, so we know max = min now: */
		countInfo->rowsReturnedMax = countInfo->rowsReturnedMin;
		return -1;
	}
	if (TXlocktable(q->out, R_LCK) == -1)
	{
		if (texispeekerr(q->out->ddic) ==
		    MAKEERROR(MOD_LOCK, LOCK_TIMEOUT))
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
	rc = gettblrow(q->out->tbl, where) ? 1 : -1;
#endif

	if (q->out->needstats)	/* Collect stats */
		dostats(q->out, fo);
	TXunlocktable(q->out, R_LCK);
	if (rc != -1)
	{
		q->nrows += 1;
		countInfo->rowsReturnedMin++;
	}
	return 0;
}
