#include "txcoreconfig.h"
#include <limits.h>
#ifdef sparc
#  include <values.h>
#else
#  include <float.h>		/*<values.h> */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#ifdef EPI_HAVE_PWD_H
#  include <pwd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef USE_FLOCK
#  include <sys/file.h>
#endif
#include <errno.h>
#include <ctype.h>
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"


#define BTCSIZE	TXbtreecache

/******************************************************************/

static int didwrite ARGS((DBTBL *, BTREE *));

static int
didwrite(tbl, bt)
DBTBL *tbl;
BTREE *bt;
{
	BTLOC	btloc;
	size_t	sz;

	sz = sizeof(BTLOC);
	btloc = btsearch(bt, sz, &tbl->recid);
	if (TXrecidvalid(&btloc))
		return 1;
	return 0;
}

/******************************************************************/

static int justwrote ARGS((RECID *, BTREE *));

static int
justwrote(recid, bt)
RECID *recid;
BTREE *bt;
{
	btinsert(bt, recid, sizeof(RECID), recid);
	return 0;
}

/******************************************************************/

/******************************************************************/
/* Checks to make sure that inserting the values in tb2 into the
   record at tb1 would not cause uniqueness to be violated.
   Assumes that all appropriate indices are already open
 */

static int checkforunique ARGS((DBTBL *, DBTBL *));

static int
checkforunique(tb1, tb2)
DBTBL *tb1;
DBTBL *tb2;
{
	TXA2IND *a2i;
	int i,rc=0;
	BTLOC btloc;
	BINDEX *ind;
	DBTBL   *savtbl;

	savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = tb1;			/* for btreelog debug */

	for(i=0; i < tb1->nindex; i++)
	{
		ind = &tb1->indexes[i];
		if(ind->btree->flags & BT_UNIQUE)
		{
			a2i = TXadd2indsetup(tb2, ind);
			if(a2i)
			{
				rc = 0;
				btloc = TXsearch2ind(a2i);
				if(TXrecidvalid(&btloc) &&
				   _recidcmp(&btloc, &tb1->recid))
				   	rc =1;
				a2i = TXadd2indcleanup(a2i);
			}
			if(rc == 1)
			{                   /* MAW 01-19-99 - add XUI */
				char	fldBuf[1024];

				TXddPrintFieldNames(fldBuf, sizeof(fldBuf),
						    ind->table->dd);
				putmsg(MWARN+XUI, NULL,
	"Update would generate non-unique row for field%s %s (index %s)",
					(ind->table->dd->n == 1 ? "" : "s"),
					fldBuf, getdbffn(ind->btree->dbf));
				goto done;
			}
		}
	}
	rc = 0;
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return(rc);
}

/******************************************************************/
/*
   Remove the record in tb1 from indices
   Assumes that all appropriate indices are already open
   Returns 0
 */

int
TXdelfromindices(tb)
DBTBL *tb;
{
	int i, tm;
	BTLOC btloc, x1;
        A3DBI   *dbi;
        DBTBL   *savtbl;

        savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = tb;			/* for btreelog debug */

	btloc = tb->recid;

	for (i = 0; i < tb->nindex; i++)
	{
		deltupfromindex(tb, tb->indexes[i], &btloc);
	}
	for (i = 0; i < tb->ninv; i++)
	{
		delfrominv(tb->invidx[i], &btloc);
	}
	tm = tb->ddic->messages[MESSAGES_FAILED_DELETE];
	tb->ddic->messages[MESSAGES_FAILED_DELETE] = 0;
	for (i = 0; i < tb->ndbi; i++)
	{

/**************************************************************************
 *	How do we delete from 3dbindex?
 *
 *	Problem:
 *
 *		We delete by recid.
 *		Access by tokid.
 *		Need someway to update by tokid.
 *
 *	Solutions:
 *
 *		1. Keep table of recid->tokid.
 *		2. Do linear search of tokid->recid.
 *		3. Build list of recid's to be deleted, and do mass delete.
 *		4. Keep del list and handle on search.
 *			a. Table
 *		*->	b. Btree
 *
 ****************************************************************************/

                dbi = tb->dbies[i];                     /* alias it */
		rewindbtree(dbi->newrec);
#if 1
                delfromnew3dbi(tb, dbi, &btloc);
#else /* !1 */
		if ((x1 = btsearch(dbi->newrec, sizeof(BTLOC), &btloc)),
		    TXrecidvalid(&x1))
			btdelete(dbi->newrec, &x1, sizeof(BTLOC), &btloc);
#endif /* !1 */
                if (dbi->type != INDEX_MM &&
                    dbi->type != INDEX_MMCR &&
                    dbi->type != INDEX_FULL &&
                    dbi->type != INDEX_FULLCR)
                  {
                    rewindbtree(dbi->upd);
                    if ((x1 = btsearch(dbi->upd, sizeof(BTLOC), &btloc)),
                        TXrecidvalid(&x1))
                      btdelete(dbi->upd, &x1, sizeof(BTLOC), &btloc);
                  }
                addtodel3dbi(tb, dbi, &btloc);          /* func KNG 000217 */
	}
        /* KNG 000222 Also del from FDBI list.  These replace `dbies'
         * but only for fixed-size-token-update-only (not delete or insert):
         */
        for (i = 0; i < tb->nfdbi; i++)
          delfromfdbi(tb, tb->fdbies[i], &btloc);
	tb->ddic->messages[MESSAGES_FAILED_DELETE] = tm;

	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return 0;
}

/******************************************************************/
/*
   Removes the record in tb1 from indices
   Assumes that all appropriate indices are already open
   Returns -1 if it is a duplicate row, nothing added to indices
   Returns 0 if added to indices.
 */

int TXaddtoindices ARGS((DBTBL *));

int
TXaddtoindices(db)
DBTBL *db;
{
	int dup, i, ret;
	BTLOC pos;
        DBTBL   *savtbl;

        savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = db;			/* for btreelog debug */

	pos = db->recid;

	for (i = dup = 0; i < db->nindex; i++)
	{
          if (addtuptoindex(db, db->indexes[i], &pos) == -1)
            {
              dup++;
              break;
            }
	}

	if (dup)
	{
          for (i--; i >= 0; i--)                /* remove just our inserts */
            {
              deltupfromindex(db, db->indexes[i], &pos);
            }
	  goto err;
	}

	for (i = 0; i < db->ninv; i++)
		_addtoinv(db->invidx[i], &pos);

	for (i = 0; i < db->ndbi; i++)
		addto3dbi(db, db->dbies[i], &pos);

        /* KNG 000222 Also add to FDBI list.  These replace `dbies'
         * but only for fixed-size-token-update-only (not delete or insert):
         */
	for (i = 0; i < db->nfdbi; i++)
		addtofdbi(db, db->fdbies[i], &pos);

	ret = 0;				/* ok */
	goto done;

err:
	ret = -1;
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return(ret);
}

/******************************************************************/

int
procupd(q, fo)
QUERY *q;
FLDOP *fo;
{
	DBTBL *tb1, *tup;
	int i, rc;
        A3DBI   *dbi;
	char **updfields;

	tb1 = q->in1;

	/* q->usr contains a BTREE with the record ids that we write.  This
	   allows us not to update the same row multiple times. */
	if (!q->usr)
		q->usr = openbtree(NULL, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT);
	if (!q->usr)
		return -1;

	putfld(q->out->frecid, &q->out->recid, 1);
	if (setupdfields(q->out, q->update) == -1)
	{
		q->usr = closebtree(q->usr);
		return -1;
	}
#ifdef NEVER
	showupdfields(q->out, q->update);
#endif
	updfields = TXgetupdfields(q->out, q->update);
	if(TXprepareTableForWriting(tb1, PM_DELETE, updfields) == -1)
	{
		q->usr = closebtree(q->usr);
		return -1;
	}
	updfields = TXfree(updfields);
	if(q->lastread.off > (EPI_OFF_T)0)	/* restore DBF pointer */
	{
		if (ioctldbf(tb1->tbl->df, (DBF_KAI | KDBF_IOCTL_SETNEXTOFF),
				&q->lastread.off) < 0)
		{				/* wasn't KDBF, try again */
			if(!isramdbtbl(tb1))
				TXmygettblrow(tb1, &q->lastread);
		}
	}
	do
	{
		tup = tup_read(tb1, fo, SQL_FETCH_NEXT, 1, NULL, TXCOUNTINFOPN);
	}
	while ((tup != NULL) && (didwrite(tb1, q->usr) != 0));
	if (tup)
	{
		int updchar;

		if (q->tr_before)
			trigexec(q->tr_before, tup, fo);
		updchar = 0;
		tup_copy(q->out, tb1, fo);
		if (updatefields(q->out, q->update, fo, &updchar) == 0)
		{
			RECID *newloc, oldloc;

/* WTF - Fix this so no memory is no problem */
			oldloc = tb1->recid;
			if (ioctldbf(tb1->tbl->df,	/* save DBF pointer */
					(DBF_KAI | KDBF_IOCTL_GETNEXTOFF),
					&q->lastread.off) < 0)
				q->lastread = oldloc;
			newloc= putdbtblrow(q->out, NULL);
			gettblrow(q->out->tbl, newloc);
/*
   Delete from appropriate indexes

   Dont't delete.
   call puttblrow with oldloc.
   Add to appropriate indexes.
 */
			TXdelfromindices(tb1);
			if(!checkforunique(tb1, q->out))
			{
				tup_copy(tb1, q->out, fo);
			}
			else
			{
				tup_copy(q->out, tb1, fo);
				putdbtblrow(q->out, NULL);
			}
			newloc = puttblrow(tb1->tbl, &oldloc);
			if(newloc)
			{
				UPDATE *u;

				if(TXrecidvalid(newloc))
					gettblrow(tb1->tbl, newloc);
				else
					newloc = NULL;
				for (u = q->update; u; u = u->next)
				{
					/* Bug 4026: -1 invalid, do not
					 * freedbf(-1) as that will delete
					 * current block which is incorrect:
					 */
					/* WTF when Bug 4037 implemented maybe
					 * also do not delete offset-0 blobs?
					 */
					if(u->delblob.dbf &&
					   u->delblob.off != (EPI_OFF_T)(-1))
					{
						freedbf((DBF *)u->delblob.dbf,
							       u->delblob.off);
					}
				}
			}
			if(newloc)
			{
				tb1->recid = *newloc;
				TXaddtoindices(tb1);
/*
   tup_delete(tb1, tb1);
   tup_copy(tb1, q->out, fo);
   newloc = putdbtblrow(tb1, NULL);
 */


				justwrote(newloc, q->usr);
				TXmygettblrow(tb1, newloc);
			}
			else
			{
				if(validrow(tb1->tbl, &oldloc))
				{
					TXmygettblrow(tb1, &oldloc);
					tb1->recid = oldloc;
					TXaddtoindices(tb1);
				}
				TXdoneWritingToTable(tb1, &tb1->iwritec);
				putmsg(MERR, "update", "Could not write updated record");
				return -1;
			}
#ifdef TX_DEBUG
			if(tb1->indexfields && TXrecidcmp(newloc, &oldloc))
			{
				DebugBreak();
			}
#endif
			if (q->tr_after)
				trigexec(q->tr_after, tb1, fo);
#ifdef NEVER /* Huh */
			TXmygettblrow(tb1, &oldloc);
#endif
			rc = 0;
			for (i = 0; i < tb1->ndbi; i++)
			{
                                dbi = tb1->dbies[i];            /* alias it */
#ifdef DEBUG
				DBGMSG(9, (999, (char *) NULL, "%d %d",
					   tb1->recid, oldloc));
#endif
#ifdef NEVER /* We only update required now anyway */
				if (updchar || ndupd || _recidcmp(newloc, &oldloc) || dbi->auxsz)
#endif
					put3dbiu(dbi, newloc,
						 &oldloc);
#ifdef NEVER /* We only update required now anyway */
				else
				{
                                  /* was del from newrec, upd, del */
				}
#endif
			}
		}
		else
		{
			putmsg(MWARN, NULL, "Could not update record");
			q->usr = closebtree(q->usr);
			rc = -1;
		}
	}
	else
	{
		q->usr = closebtree(q->usr);
		rc = -1;
	}
	TXdoneWritingToTable(tb1, &tb1->iwritec);
	return rc;
}
