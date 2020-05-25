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

static int dohint(DBTBL *dbtbl, char *hint, int onoff)
{
   int rc;
   if(!strcmpi(hint, "TABLOCKX"))
   {
      if(onoff > 0)
      {
         rc = TXlockindex(dbtbl, INDEX_WRITE, NULL);
         if(rc != -1)
         {
            rc = TXlocktable(dbtbl, W_LCK);
            if(rc == -1)
               TXunlockindex(dbtbl, INDEX_WRITE, NULL);
         }
         return rc;
      }
      else
      {
         TXunlocktable(dbtbl, W_LCK);
         return TXunlockindex(dbtbl, INDEX_WRITE, NULL);
      }
   }
   if(!strcmpi(hint, "TABLOCK"))
   {
      if(onoff > 0)
      {
         rc = TXlockindex(dbtbl, INDEX_READ, NULL);
         if(rc != -1)
         {
            rc = TXlocktable(dbtbl, R_LCK);
            if(rc == -1)
               TXunlockindex(dbtbl, INDEX_READ, NULL);
         }
         return rc;
      }
      else
      {
         TXunlocktable(dbtbl, R_LCK);
         return TXunlockindex(dbtbl, INDEX_READ, NULL);
      }
   }
   return 0;
}

/****************************************************************************/

static int dohints(DBTBL *dbtbl, QNODE *query, int onoff)
{
   int rc = 0;

   switch (query->op)
   {
      case NAME_OP:
         rc = dohint(dbtbl, query->tname, onoff);
         break;
      case LIST_OP:
         if(dohints(dbtbl, query->left, onoff) == -1)
          rc = -1;
         if(dohints(dbtbl, query->right, onoff) == -1)
          rc = -1;
      default:
        /* No Hints */
        break;
   }
   return rc;
}

/****************************************************************************/

DBTBL *
TXnode_hint_prep(IPREPTREEINFO *prepinfo,
		  QNODE *query,
		  QNODE *parentquery,
		  int *success)
{
	static CONST char Fn[] = "node_hint_prep";
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
         dohints(q->out, query->right, 1);
	}
	return q->out;

}

/****************************************************************************/

int
TXnode_hint_exec(QNODE *query, FLDOP *fo, int direction, int offset, int verbose)
{
	static CONST char Fn[] = "node_hint_exec";
	QUERY *q = query->q;

	int r;

	query->state = QS_ACTIVE;
	q->state = QS_ACTIVE;

	if(verbose)
		putmsg(MINFO, Fn, "Handling a table alias");

	r = TXdotree(query->left, fo, direction, offset);
	q->nrows = query->left->q->nrows;
	if (r == -1)
	{
         dohints(q->out, query->right, -1);
	}

	return r;
}
