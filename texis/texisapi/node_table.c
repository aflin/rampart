/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <errno.h>
#include <stdio.h>
#if defined(unix) || defined(__unix)	/* MAW 02-15-94 wtf? groups on nt? */
#include <grp.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#  include <winsock.h>
#else /* !_WIN32 */
#  include <netinet/in.h>
#endif /* !_WIN32 */
#include "os.h"
#include "dbquery.h"
#include "texint.h"
#include "txlic.h"

/****************************************************************************/

DBTBL *
TXnode_table_prep(IPREPTREEINFO *prepinfo, QNODE *query, QNODE *parentquery, int *success)
{
	static CONST char Fn[] = "node_table_prep";
	DDIC *ddic;
	DBTBL *rc;
	QUERY *q;
        size_t  i;

	ddic = prepinfo->ddic;
	q = query->q;

	rc = opendbtbl(ddic, query->tname);
	if (!prepinfo->preq)
		prepinfo->preq |= PM_SELECT;
#ifdef HAVE_VIEWS
	if(rc && rc->type == 'V')
	{
		query->left = rc->qnode;
		query->op = RENAME_OP;
		q->out = ipreparetree(prepinfo, query->left, query, success);
		rc = q->out;
	}
#endif
	if (rc == (DBTBL *) NULL || rc->tbl == (TBL *) NULL)
	{
		putmsg(MWARN + UGE, NULL,
		       "No such table: %s in the database: %s",
		       query->tname, ddic->pname);
		return (DBTBL *) NULL;
	}
	if(ddic->ch && prepinfo->preq == PM_SELECT && !isramdbtbl(rc))
	{
		/* If we haven't done it yet this statement, but
		   we have in the program */
		if(!(prepinfo->stmthits)++ && TXlicensehits++)
		{
			if(!TXlic_addhit())
				return closedbtbl(rc);
			/* check license */
		}
	}
	if (!permcheck(rc, prepinfo->preq))
	{
		putmsg(MERR, Fn, "Insufficient permissions on %s", query->tname);
		return closedbtbl(rc);
	}
	q->out = rc;
	if(prepinfo->analyze)
	{
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(!query->fldlist)
		{
			if(parentquery && parentquery->fldlist)
				query->fldlist = sldup(parentquery->fldlist);
		}
		if(query->fldlist)
		{
			if(!query->afldlist)
			{
				query->afldlist = slopen();
			}
			for(i=0; i < q->out->tbl->n; i++)
			{
				char *nm;
				int freenm = 0;

				nm = q->out->tbl->dd->fd[i].name;
				if(!slfind(query->fldlist, nm))
					if (q->out->rname)
					{
						nm = TXstrcat3(q->out->rname,
							".", nm);
						freenm++;
					}
				if (nm)
				{
					/* KNG 011210 could already be in list
					 * from previous SQLExecute(); check:
					 */
					if (!slfind(query->afldlist, nm))
						sladd(query->afldlist, nm);
					if(freenm) free(nm);
				}
			}
		}
	}
	return rc;
}

/****************************************************************************/

int
TXnode_table_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
/* Returns -1 at EOF, 0 if row obtained.
 */
{
	QUERY *q = query->q;
	DBTBL *rc;

	int locked;
	int skipped;

	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;

	/*
	 * If we will need to linearly check rows according to indguar
	 * (index guarantee) and READLOCK optimization set then do one
	 * lock for all reads
	 */
	if((q->out->indguar == 0) && /* May read many rows */
	    q->out->ddic->optimizations[OPTIMIZE_READLOCK])
	{
		locked = TXlockindex(q->out, INDEX_READ, NULL);
		if(locked != -1)
		{
			locked = TXlocktable(q->out, R_LCK);
			if(locked == -1)
                          TXunlockindex(q->out, INDEX_READ, NULL);
		}
	}
	else
	{
		locked = -1;
	}

	/*
	 * Actually read the row from the table
	 */
	rc = tup_read(q->out, fo, direction, offset, &skipped, &query->countInfo);
	q->nrows += skipped;

	/*
	 * If we manually locked, unlock
	 */
	if(locked >= 0)
	{
		TXunlocktable(q->out, R_LCK);
		TXunlockindex(q->out, INDEX_READ, NULL);
	}

	if (rc == (DBTBL *)NULL)
	{
		if(verbose)
			putmsg(MINFO, NULL, "No more rows [%d] from %s",
				q->nrows, q->out->rname);
		return -1;
	}
	else
	{
		q->nrows += 1;
		if(verbose)
			putmsg(MINFO, NULL, "Read %d rows so far from %s",
				q->nrows, q->out->rname);
		return 0;
	}
}
