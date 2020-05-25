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
TXnode_rename_prep(IPREPTREEINFO *prepinfo,
		  QNODE *query,
		  QNODE *parentquery,
		  int *success)
{
	static CONST char Fn[] = "node_rename_prep";
	DDIC *ddic;
	QUERY *q;

	ddic = prepinfo->ddic;
	q = query->q;

	q->op = Q_RENAME;

	if(query->tname && (strlen(query->tname) > DDNAMESZ))
	{
		putmsg(MWARN, Fn, "Table alias name too long");
		return NULL;
	}

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

	q->out = ipreparetree(prepinfo, query->left, query, success);

	if (q->out != (DBTBL *) NULL)
	{
		renametbl(q->out, query->tname);

		if(prepinfo->analyze && query->left && query->left->afldlist)
		{
			if(!query->afldlist)
				query->afldlist = slistrename(query->left->afldlist, query->tname);
			else
				sladdslst(query->afldlist,query->left->afldlist, 1);
		}
	}
	return q->out;

}

/****************************************************************************/

int
TXnode_rename_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
{
	static CONST char Fn[] = "node_rename_exec";
	QUERY *q = query->q;

	int r;

	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;

	if(verbose)
		putmsg(MINFO, Fn, "Handling a table alias");

	r = TXdotree(query->left, fo, direction, offset);
	q->nrows = query->left->q->nrows;

	return r;
}


