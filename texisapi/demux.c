#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "texint.h"


/* TXDEMUX de-multiplexing object: splits each row into multiple rows,
 * one per value in a multi-value field.  For GROUP BY, DISTINCT on strlst.
 */

/* ------------------------------------------------------------------------ */

int
TXdemuxAddDemuxQnodeIfNeeded(qnode)
QNODE   *qnode; /* (in/out) node, e.g. GROUP BY or DISTINCT */
/* Adds a TXDEMUX node between `qnode' and its input child, if needed
 * (i.e. it really does split, not a no-op).
 * Returns 0 on error.
 */
{
  QNODE         *demuxQnode = QNODEPN;
  TXDEMUX       *demux = TXDEMUXPN;
  QUERY         *q = qnode->q;
  int           ret, freeDemuxOnErr = 1;

  demux = TXdemuxOpen(q->in1, q->proj, tbldd(q->in2->tbl));
  if (demux == TXDEMUXPN) goto err;

  /* If this demux is not splitting (e.g. no strlsts), we do not need
   * to add a node.  tup_index_setup() would have determined whether
   * any fields are to be split (for GROUP BY/DISTINCT):
   */
  if (!TXdemuxIsSplitter(demux))
    {
      demux = TXdemuxClose(demux);
      goto ok;
    }

  /* Make a QNODE and hang this demuxer on it: */
  if ((demuxQnode = openqnode(DEMUX_OP)) == QNODEPN) goto err;
  if ((demuxQnode->q = TXopenQuery(Q_DEMUX)) == QUERYPN) goto err;
  demuxQnode->q->usr = demux;
  freeDemuxOnErr = 0;                           /* owned by QNODE now */
  demuxQnode->q->in1 = q->in1;                  /* same input as GROUP BY */
  demuxQnode->q->out = demux->outputDbtbl;      /* but new demuxed output */

  /* Insert the DEMUX_OP QNODE between GROUP BY and GROUP BY's input.
   *         From this:                         To this:
   *            GROUPBY                          GROUPBY
   *            /     \                          /     \
   * group-by-cols    table           group-by-cols    DEMUX
   *                                                       \
   *                                                       table
   */
  demuxQnode->right = qnode->right;             /* take GROUP BY's input */
  demuxQnode->parentqn = qnode;                 /* demux parent is GROUP BY */
  demuxQnode->right->parentqn = demuxQnode;     /* input parent is demux */
  qnode->right = demuxQnode;                    /* GROUP BY input = us */
  q->in1 = demux->outputDbtbl;                  /* GROUP BY input = our out */
  demux->ownOutputDbtbl = 0;                    /* let closequery() free it */
ok:
  ret = 1;                                      /* success */
  goto done;

err:
  if (freeDemuxOnErr) demux = TXdemuxClose(demux);
  demuxQnode = closeqnode(demuxQnode);
  ret = 0;
done:
  return(ret);
}

/* ------------------------------------------------------------------------ */

TXDEMUX *
TXdemuxOpen(inputDbtbl, splitProj, splitSchema)
DBTBL   *inputDbtbl;    /* (in) input table (e.g. with strlsts) */
PROJ    *splitProj;     /* (in) mux projection */
DD      *splitSchema;   /* (in) mux fields (e.g. with varchars) */
/* Opens and prepares a TXDEMUX object.  Rows from `inputDbtbl'
 * will be split on fields that are common between `inputDbtbl' and
 * `splitSchema' but are multi-value (e.g. strlst) in `inputDbtbl'
 * yet single-value (e.g. varchar) in `splitSchema'.  If there are
 * no fields to split (or TXApp->multiValueToMultiRow is off),
 * the object will be a 1-to-1 no-op/pass-through.
 * Returns pointer to object, or NULL on error.
 */
{
  static CONST char     fn[] = "TXdemuxOpen";
  TXPMBUF               *pmbuf = inputDbtbl->ddic->pmbuf;
  TXDEMUX               *demux;
  DD                    *inputDd, *outputDd = DDPN;
  DDFD                  *ddfd;
  DBTBL                 *outputDbtbl;
  int                   idx;

  demux = (TXDEMUX *)TXcalloc(pmbuf, fn, 1, sizeof(TXDEMUX));
  if (demux == TXDEMUXPN) goto err;
  demux->splitFldIdx = -1;
  /* rest cleared by calloc() */

  /* Bug 2397: Find out if we need to split up multi-value fields
   * (e.g. strlsts) into individual items (e.g. varchars):
   */
  inputDd = tbldd(inputDbtbl->tbl);
  if (!TXApp->multiValueToMultiRow ||
      (idx = TXgetMultiValueSplitFldIdx(splitProj, splitSchema, inputDd)) < 0)
    {
      demux->outputDbtbl = outputDbtbl = inputDbtbl;
      demux->ownOutputDbtbl = 0;
      demux->splitFldIdx = -1;                  /* no-op i.e. not splitting */
      goto done;
    }

  /* We are going to split the input field number `idx'.
   * `outputDbtbl' will be a copy of `inputDbtbl', except that field `idx'
   * will be the split type (e.g. varchar instead of strlst).
   * tup_index_setup() will already have set the GROUP BY's QNODE `q->in2'/
   * `q->out' schemas so that the split field is the split-item type:
   */
  demux->splitFldIdx = idx;
  if ((outputDd = TXexpanddd(inputDd, 0)) == DDPN) goto err;
  /* WTF need to putdd() at a specific index, not add to end: */
  ddfd = &outputDd->fd[idx];
  ddfd->type = (DDVARBIT | FTN_CHAR);
  ddfd->size = 1*sizeof(ft_char);
  ddfd->elsz = sizeof(ft_char);
  /* Make output DBTBL with our schema: */
  demux->outputDbtbl = outputDbtbl =
    (DBTBL *)TXcalloc(pmbuf, fn, 1, sizeof(DBTBL));
  if (outputDbtbl == DBTBLPN) goto err;
  demux->ownOutputDbtbl = 1;                    /* we own it (for now) */
  outputDbtbl->tbl = createtbl(outputDd, CHARPN);
  if (outputDbtbl->tbl == TBLPN) goto err;
  /* rest is from preparequery() wtf: */
  if (inputDbtbl->lname != CHARPN)
    outputDbtbl->lname = TXstrdup(pmbuf, fn, inputDbtbl->lname);
  outputDbtbl->type = TEXIS_TABLE;
  outputDbtbl->ddic = inputDbtbl->ddic;
  outputDbtbl->frecid = createfld("long", 1, 0);
  outputDbtbl->tblid = -1;
  putfld(outputDbtbl->frecid, &outputDbtbl->recid, 1);
  goto done;

err:
  demux = TXdemuxClose(demux);
done:
  closedd(outputDd);
  return(demux);
}

/* ------------------------------------------------------------------------ */

TXDEMUX *
TXdemuxClose(demux)
TXDEMUX *demux;
{
  if (demux == TXDEMUXPN) return(TXDEMUXPN);

  if (demux->outputDbtbl != DBTBLPN && demux->ownOutputDbtbl)
    _closedbtbl(demux->outputDbtbl);
  return(TXfree(demux));
}

/* ------------------------------------------------------------------------ */

int
TXdemuxReset(demux)
TXDEMUX *demux; /* (in/out) */
/* Ends multi-item (e.g. strlst) split of current row and resets for next row.
 * Can be called regardless of whether actually splitting row.
 * Returns 0 on error.
 */
{
  demux->curItem = NULL;                    /* reset for next row */
  demux->curItemLen = 0;
  demux->haveRow = 0;
  return(1);
}

/* ------------------------------------------------------------------------ */

int
TXdemuxGetNextRow(demux, inputQnode, fo)
TXDEMUX         *demux;         /* (in/out) */
QNODE           *inputQnode;    /* (in/out) input QNODE */
FLDOP           *fo;            /* (in/out) FLDOP to use */
/* Gets next split item row into `demux->outputDbtbl'.  First reads real
 * input row (if needed) via TXdotree().  `inputQnode' is input QNODE,
 * e.g. the GROUP_BY_OP's original (before DEMUX node added) right-side input.
 * If not splitting, just leaves row as-is, i.e. returns one item per row.
 * Call TXdemuxReset() if ending early (before EOF/err return).
 * Returns 0 on EOF/error, 1 on success.
 */
{
  static CONST char     fn[] = "TXdemuxGetNextRow";
  FTN                   curItemType;
  int                   gotRow;
  FLD                   *splitFldSrc, *splitFldDest;

  if (!TXdemuxIsSplitter(demux))
    {
      /* Not splitting; just read a row: */
      gotRow = (TXdotree(inputQnode, fo, SQL_FETCH_NEXT, 1) == 0);
      demux->haveRow = gotRow;
      return(gotRow);
    }

  splitFldSrc = inputQnode->q->out->tbl->field[demux->splitFldIdx];
  splitFldDest = demux->outputDbtbl->tbl->field[demux->splitFldIdx];

  /* Get next item from the current input row's strlst field: */
  TXfldIsMultipleItemType(splitFldSrc, NULL, &curItemType);
  while (!demux->haveRow ||
         (demux->curItem =
          TXfldGetNextItem(splitFldSrc, demux->curItem,
                           demux->curItemLen, &demux->curItemLen)) == NULL)
    {
      /* No more items this row.  Reset and read a real row from input: */
      TXdemuxReset(demux);
      gotRow = (TXdotree(inputQnode, fo, SQL_FETCH_NEXT, 1) == 0);
      demux->outputDbtbl->recid = inputQnode->q->out->recid;
      demux->haveRow = gotRow;
      if (!gotRow) return(0);                   /* EOF */
    }
  if (TXverbosity > 3) putmsg(MINFO, CHARPN, "Splitting record");

  /* Sanity check: is it the expected type? */
  if (curItemType != (FTN)splitFldDest->type)
    {
      putmsg(MERR, fn, "Internal error: Type mismatch in split: data type %s (%d) != expected field type %s (%d)",
             ddfttypename(curItemType), curItemType,
             ddfttypename(splitFldDest->type),
             (int)splitFldDest->type);
      TXdemuxReset(demux);
      return(0);                                /* error */
    }

  /* Copy the tuple to our output table: */
  if (tup_copy(demux->outputDbtbl, inputQnode->q->out, fo) != 0)
    return(0);                                  /* error */
  /* But change the split field to split type: */
  putfld(splitFldDest, demux->curItem, demux->curItemLen);

  return(1);                                    /* success with data */
}

/* ------------------------------------------------------------------------ */

int
TXgetMultiValueSplitFldIdx(proj, projDd, tblDd)
PROJ    *proj;   /* (in) PROJ to check, i.e. GROUP BY */
DD      *projDd; /* (in) DD corresponding to `proj' (wtf almost unneeded) */
DD      *tblDd;  /* (in) original table DD */
/* Checks that `proj' is a multi-value-split schema: must be only one
 * predicate, which must be a split target type (e.g. varchar) whose
 * corresponding field in `tblDd' is a split source type
 * (e.g. strlst).  Returns index into `tblDd->fd' of field, or -1 if
 * not.
 */
{
  int   idx, tblIdx;
  char  *firstColName;

  /* Check for only 1 predicate in `proj'; we do not split strlst on
   * compound indexes (KNG 20090504 pre-existing B-tree indexes have
   * this behavior):
   */
  if (proj->n > 1) return(-1);          /* 2+ predicates */

  /* Check that the predicate is varchar in `projDd'.  WTF we could
   * use predtype() on `proj->preds[0]' instead of needing `projDd',
   * but we lack a FLDOP (and would be duplicating work?):
   */
  idx = ddgetorign(projDd, 0);          /* index of first field */
  if (idx < 0) return(-1);              /* should not happen */
  if ((projDd->fd[idx].type & DDTYPEBITS) != FTN_CHAR) return(-1);

  /* Check that the field was originally strlst in `tblDd'.
   * Bug 1995: GROUP BY groups on `proj', but we demux on
   * its input table `tblDd'.  For a simple `GROUP BY tableCol' the two
   * columns' schemas are the same, but for more complex expressions,
   * e.g. `GROUP BY lookup(tableCol)', we are looking at `lookup()' in
   * `proj'/`projDd' but `tableCol' in `tblDd': hope that `lookup()'
   * returns varchar-for-varchar and strlst-for-strlst.  Because of
   * potential expression, we cannot just look up `projDd' column name
   * in `tblDd': we must look for `tableCol' in `proj->preds[0]' tree
   * (and hope there is only one such column input, otherwise demux
   * behavior may be inconsistent/indeterminable...).
   * See also tup_index_setup() comments:
   */
  firstColName = TXpredGetFirstUsedColumnName(proj->preds[0]);
  if (!firstColName) return(-1);        /* nothing found */
  tblIdx = ddfindname(tblDd, firstColName);
  if (tblIdx < 0) return(-1);                   /* not found in `tblDd' */
  if ((tblDd->fd[tblIdx].type & DDTYPEBITS) != FTN_STRLST) return(-1);

  /* All checks passed: */
  return(tblIdx);
}

/* ------------------------------------------------------------------------ */

int
TXqnodeRewindInput(qnode)
QNODE   *qnode;         /* (in/out) e.g. a GROUP_BY_OP node */
/* Rewinds input of `qnode'.  WTF hack, currently for GROUP_BY_OP only.
 * Returns 2 if index present and rewound, 1 if not, 0 on error.
 */
{
  TXDEMUX       *demux = TXDEMUXPN;
  QNODE         *realInputQnode;
  DBTBL         *realInputDbtbl;

  /* Get the real (pre-DEMUX) input QNODE/DBTBL; since DEMUX_OP
   * breaks strlsts into varchars, drill past it to real input:
   */
  realInputQnode = qnode->right;
  realInputDbtbl = qnode->q->in1;
  TX_CLEAR_COUNTINFO(&realInputQnode->countInfo);
  if (realInputQnode->op == DEMUX_OP)           /* not the real/raw input */
    {
      demux = (TXDEMUX *)realInputQnode->q->usr;
      realInputDbtbl = realInputQnode->q->in1;
      realInputQnode = realInputQnode->right;   /* i.e. DEMUX's input */
      TX_CLEAR_COUNTINFO(&realInputQnode->countInfo);
    }

  if (realInputDbtbl != DBTBLPN && realInputDbtbl->index.btree != BTREEPN)
    {
      if (demux != TXDEMUXPN)
        TXdemuxReset(demux);                    /* since rewinding */
      return(TXdbidxrewind(&realInputDbtbl->index) == 0 ? 2 : 0);
    }
  return(1);                                    /* ok but no index rewound */
}
