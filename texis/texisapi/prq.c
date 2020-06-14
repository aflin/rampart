/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"

/******************************************************************/
/*	Prepare a query for execution.  Sets up as much stuff as
 *	possible without actually doing anything.
 *
 *	Mainly creates all the temporary tables that will be used
 *	along the way.
 */

void
preparequery(query, fo, allowbubble)
QNODE *query;	/* Query to prepare */
FLDOP *fo;	/* A FLDOP to do any calcs with */
int allowbubble;
{
	static const char	fn[] = "preparequery";
	DBTBL 	*tb1, *tb2;
	QUERY	*q;
	DD	*outputDd = DDPN;
	int	isOrdered;
	QNODE	*realInputQnode;
	char	*tblPath;

	q = query->q;
	switch (q->op)
	{
		case Q_CPRODUCT:
			tb1 = TXtup_product_setup(q->in1, q->in2,
				1, query->fldlist);
			if (tb1 && q->proj)
			{
				DBGMSG(1, (999, NULL, "Added a proj"));
				q->out = TXtup_project_setup(tb1, q->proj, fo, 1);
				closedbtbl(tb1);
			}
			else
				q->out = tb1;
			break;
		case Q_PROJECT:
			tb1 = q->in1;
			q->out = TXtup_project_setup(tb1, q->proj, fo, 1);
			break;
		case Q_SELECT:
		{
#ifndef OLD_ORDERBY
#else
			DD *dd ;
#endif
			int x = 0;
			tb1 = q->in1;
#ifndef OLD_ORDERBY
			q->out = q->in1;
#else
			q->out = (DBTBL *)calloc(1, sizeof(DBTBL));
			dd = TXbddc(tbldd(tb1->tbl));
#if defined(NEW_TMP_TABLE)
			ddsettype(dd, TEXIS_TMP_TABLE);
#endif
			q->out->tbl = createtbl(dd, NULL);
			closedd(dd);
			q->out->lname = strdup(tb1->lname);
/*
			q->out->rname = strdup(tb1->lname);
*/
			q->out->rname = NULL;
			q->out->type = 'T';
			q->out->ddic = tb1->ddic;
			q->out->frecid = createfld("long", 1, 0);
			q->out->tblid = -1;
			putfld(q->out->frecid, &q->out->recid, 1);
#endif
#ifndef ALLOW_MISSING_FIELDS
			if(!TXispredvalid(tb1->ddic->pmbuf, q->pred, tb1,
					  0x3, DBTBLPN, INTPN))
			{
				q->in1 = closedbtbl(q->in1);
				q->out = NULL;
			}
			else
#endif
			if(!allowbubble)
			{
				x = TXbtreemaxpercent;
				TXbtreemaxpercent = 200;
			}
			TXsettablepred(query, tb1, q->pred, q->order, fo, allowbubble, query->fldlist, query->pfldlist);
			if(!allowbubble)
			{
				TXbtreemaxpercent = x;
			}
			break;
		}
		case Q_UNION:
			tb1 = q->in1;
			tb2 = q->in2;
			q->out = tup_union_setup(tb1, tb2);
			break;
		case Q_APPEND:
			tb1 = q->in1;
			tb2 = q->in2;
			q->out = tup_append_setup(tb1, tb2);
			if (tb1->trigger &&
			    tb1->trigger->insert &&
			    tb1->trigger->insert->before &&
			    tb1->trigger->insert->before->row)
				q->tr_before=tb1->trigger->insert->before->row;
			if (tb1->trigger &&
			    tb1->trigger->insert &&
			    tb1->trigger->insert->after &&
			    tb1->trigger->insert->after->row)
				q->tr_after = tb1->trigger->insert->after->row;
			if (tb1->trigger &&
			    tb1->trigger->insert &&
			    tb1->trigger->insert->before &&
			    tb1->trigger->insert->before->statement)
			    	trigexec(tb1->trigger->insert->before->statement,
					tb2, fo);
			break;
		case Q_DELETE:
			q->out = q->in1;
			tb1 = q->out;
			if (tb1->trigger &&
			    tb1->trigger->deltrg &&
			    tb1->trigger->deltrg->before &&
			    tb1->trigger->deltrg->before->row)
				q->tr_before=tb1->trigger->deltrg->before->row;
			if (tb1->trigger &&
			    tb1->trigger->deltrg &&
			    tb1->trigger->deltrg->after &&
			    tb1->trigger->deltrg->after->row)
				q->tr_after=tb1->trigger->deltrg->after->row;
#ifndef ALLOW_MISSING_FIELDS
			if(!TXispredvalid(tb1->ddic->pmbuf, q->pred, tb1,
					  0x3, DBTBLPN, INTPN))
			{
				q->in1 = closedbtbl(q->in1);
				q->out = NULL;
			}
			else
#endif
			TXsettablepred(query, tb1, q->pred, q->order, fo, 0, query->fldlist, NULL);
			break;
		case Q_UPDATE:
			q->out = q->in1;
			tb1 = q->in1;
			q->in2 = (DBTBL *)calloc(1, sizeof(DBTBL));
			q->in2->tbl = createtbl(tbldd(tb1->tbl), NULL);
			q->in2->lname = strdup(tb1->lname);
/*
			q->in2->rname = strdup(tb1->lname);
*/
			q->in2->rname = NULL;
			q->in2->type = 'T';
			q->in2->ddic = tb1->ddic;
			q->in2->frecid = createfld("long", 1, 0);
			q->in2->tblid = -1;
			putfld(q->out->frecid, &q->out->recid, 1);
			if (tb1->trigger &&
			    tb1->trigger->update &&
			    tb1->trigger->update->before &&
			    tb1->trigger->update->before->row)
				q->tr_before=tb1->trigger->update->before->row;
			if (tb1->trigger &&
			    tb1->trigger->update &&
			    tb1->trigger->update->after &&
			    tb1->trigger->update->after->row)
				q->tr_after=tb1->trigger->update->after->row;
#ifndef ALLOW_MISSING_FIELDS
			if(!TXispredvalid(tb1->ddic->pmbuf, q->pred, tb1,
					  0x3, DBTBLPN, INTPN))
			{
				q->out = NULL;
				q->in2 = closedbtbl(q->in2);
				break;
			}
			else
#endif
			TXsettablepred(query, tb1, q->pred, q->order, fo, 0, query->fldlist, query->pfldlist);
#ifndef OLD_UPDATE_METHOD
			q->out = q->in2;
#endif
			break;
		case Q_ORDER:
			tb1 = q->in1;
			q->out = (DBTBL *)calloc(1, sizeof(DBTBL));
#ifndef NO_PROP_RANK
			{
				DD *dd = TXexpanddd(tbldd(tb1->tbl),1);

				if(q->priorproj)
					putdd(dd, (char *)TXrankColumnName,
					   (char *)TXrankColumnTypeStr, 1, 1);
				q->out->tbl = createtbl(dd, NULL);
				closedd(dd);
			}
#else
			q->out->tbl = createtbl(tbldd(tb1->tbl), NULL);
#endif
			if(tb1->lname)
				q->out->lname = strdup(tb1->lname);
/*
			q->out->rname = strdup(tb1->lname);
*/
			q->out->rname = NULL;
			q->out->type = 'T';
			q->out->ddic = tb1->ddic;
			q->out->frecid = createfld("long", 1, 0);
			q->out->tblid = -1;
			putfld(q->out->frecid, &q->out->recid, 1);
			if (query->pfldlist &&
			    q->in1->index.btree &&
			    q->in1->indguar &&
			    q->in1->index.btree->datad)
			{
				DD	*indexDd = q->in1->index.btree->datad;
				DD	*dbtblDd = q->in1->tbl->dd;
				int indexonly = 1, i;
				SLIST *flist;

				flist = query->pfldlist;
				for (i = 0; i < flist->cnt - 1; i++)
				{
					int	indexDdIdx, dbtblDdIdx;
					char	*fname = flist->s[i];

					/* KNG 20120305 types must also agree;
					 * see TXtup_project_setup() comment:
					 */
					if ((indexDdIdx =
					     ddfindname(indexDd, fname)) < 0 ||
					    /* Bug 6881: while $rank
					     * needs to be in `indexDd' for
					     * `indexonly' to be true, it
					     * need not be in `dbtblDd':
					     */
					    ((dbtblDdIdx =
					      ddfindname(dbtblDd, fname)) < 0 ?
					 strcmp(fname, TXrankColumnName) != 0 :
					    (indexDd->fd[indexDdIdx].type &
					     FTN_VarBaseTypeMask) !=
					    (dbtblDd->fd[dbtblDdIdx].type &
					     FTN_VarBaseTypeMask)))

					{
						indexonly = 0;
#ifdef NO_TRY_FAST_CONV
						fname = TXfree(fname);
#endif
						break;
					}
#ifdef NO_TRY_FAST_CONV
					fname = TXfree(fname);
#endif
				}
				if(indexonly)
				{
					q->in1->index.indexdataonly=1;
					if(!q->in1->index.indexdbtbl)
					{
						q->in1->index.indexdbtbl =
							TXopentmpdbtbl(
						NULL, NULL, NULL, indexDd,
						q->in1->ddic);
					}
					getdbtblrow(q->in1); /* Prime for no NULLs */
				}
			}
			q->in2 = tup_index_setup(q->in1, q->proj, fo,
				OF_DESCENDING, DDPN);
			break;
		case Q_GROUP:
		case Q_DISTINCT:
			/* The output DD is the input DD plus $rank.
			 * But we may alter the output DD in
			 * tup_index_setup(), ie. to map strlst to varchar:
			 */
			tb1 = q->in1;
			outputDd = TXexpanddd(tbldd(tb1->tbl), 1);
#ifndef NO_PROP_RANK
			if(q->priorproj)
				putdd(outputDd, (char *)TXrankColumnName,
				      (char *)TXrankColumnTypeStr, 1, 1);
#endif
			/* `q->in2' will be a (unique RAM) index on
			 * the GROUP BY columns (`q->proj'), used to
			 * determine when a new group starts:
			 */
			q->in2 = tup_index_setup(q->in1, q->proj, fo,
				OF_DONT_CARE, outputDd);
			if(tb1->ddic->optimizations[OPTIMIZE_GROUP])
			{
				q->in2->index.btree->flags |= BT_UNIQUE;
			}

			/* Now create the output table, after `outputDd'
			 * mods by tup_index_setup().
			 * First check if this GROUP BY is ordered
			 * (see same check in groupbysetup[2]():
			 */
			isOrdered = 0;
			realInputQnode = query->right;
			/* no DEMUX op yet, so no check for it needed */
			if (realInputQnode->op == SELECT_OP &&
			    projcmp(q->proj, realInputQnode->q->in1->order))
				isOrdered++;
			if (realInputQnode->op == NAME_OP &&
			    projcmp(q->proj, realInputQnode->q->out->order))
				isOrdered++;

			q->out = TXnewDbtbl(tb1->ddic->pmbuf);
			if (!q->out) goto err;
			/* Bug 4770: if ordered, we only need the output
			 * DBTBL.fields a row at a time, and do not need
			 * to store all the rows in a RAM DBF; use TXNOOPDBF:
			 */
			tblPath = CHARPN;	/* RAM DBF */
			if (tb1->ddic->optimizations[OPTIMIZE_GROUP_BY_MEM] &&
			    isOrdered)
				tblPath = TXNOOPDBF_PATH;
			q->out->tbl = createtbl(outputDd, tblPath);
			outputDd = closedd(outputDd);
			q->out->lname = TXstrdup(tb1->ddic->pmbuf, fn,
						 tb1->lname);
/*
			q->out->rname = strdup(tb1->lname);
*/
			q->out->rname = NULL;
			q->out->type = 'T';
			q->out->ddic = tb1->ddic;
			q->out->frecid = createfld("long", 1, 0); /* KNG wtf should be "recid"? */
			q->out->tblid = -1;
			putfld(q->out->frecid, &q->out->recid, 1);

			/* If the GROUP BY/DISTINCT is on a strlst, we must
			 * "demux" each row into multiple rows, by splitting
			 * multi-value fields (eg. strlst).  Which fields
			 * to split were determined by tup_index_setup().
			 * Add a DEMUX_OP node between GROUP BY and its
			 * input to handle this:
			 */
			TXdemuxAddDemuxQnodeIfNeeded(query);
			/* wtf bail on error */
			break;
		case Q_DEMUX:
			/* Nothing to do; set up in TXdemuxAddDemuxQnode() */
			break;
		case Q_OFFSET:
		case Q_RENAME:
		case Q_DIFFERENCE:
		case Q_PROXIM:
		case Q_PROP:
		default:
			break;
	}
	goto done;				/* wtf success return */

err:
done:
	return;					/* wtf error return */
}

