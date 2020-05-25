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
TXnode_join_prep(IPREPTREEINFO *prepinfo,
		  QNODE *query,
		  QNODE *parentquery,
		  int *success)
{
	DDIC *ddic;
	QUERY *q;

	ddic = prepinfo->ddic;
	q = query->q;

	q->op = Q_RENAME;

	if(prepinfo->analyze)
	{
		if(parentquery)
			query->pfldlist = parentquery->fldlist;
		if(!query->fldlist)
		{
			if(parentquery && parentquery->fldlist)
				query->fldlist = sldup(parentquery->fldlist);
		}
	}

	prepinfo->preq |= PM_SELECT;

	q->in1 = ipreparetree(prepinfo, query->left, query, success);
	if (q->in1 == NULL)
		return (DBTBL *) NULL;
	q->in2 = ipreparetree(prepinfo, query->right, query, success);
	if (q->in2 == NULL)
		return (DBTBL *) NULL;


	if(prepinfo->analyze)
	{
		if(query->left && query->left->afldlist)
		{
			if(!query->afldlist)
				query->afldlist = sldup(query->left->afldlist);
			else
				sladdslst(query->afldlist,query->left->afldlist, 1);
		}
		if(query->right && query->right->afldlist)
		{
			if(!query->afldlist)
				query->afldlist = sldup(query->right->afldlist);
			else
				sladdslst(query->afldlist,query->right->afldlist, 1);
		}
	}
	q->pred = (PRED *) NULL;
	q->proj = (PROJ *) NULL;
	q->op = Q_CPRODUCT;
	preparequery(query, prepinfo->fo, prepinfo->allowbubble);
	if (!q->out || (q->out->tbl == NULL))
		return (DBTBL *) NULL;
	return q->out;
}

/****************************************************************************/

int
TXnode_join_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
{
	static CONST char Fn[] = "node_join_exec";
	
	QUERY *q = query->q;

	int toskip = 0;
	int r;

	if(verbose) putmsg(MINFO, Fn, "Handling a table join");

	switch(direction)
	{
		case SQL_FETCH_RELATIVE:
			if(offset == 0)	return -1;
			if(offset > 0)	toskip = offset - 1;
			else		return -1;
	}

	if(TXproductsetup(query, q, fo) == -1)
		return -1;

	do
	{
		r = TXproduct(query, q, fo);
	} while(r == 0 && (toskip-- > 0));

	query->state = q->state;
	query->left->state = q->state;

	q->nrows += (toskip + 1);

	return r;
}

/******************************************************************/
