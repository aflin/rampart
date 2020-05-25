/* -=- kai-mode: john -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#ifndef _WIN32
#  include <sys/time.h>
#endif
#ifdef HAVE_MMAP
#  include <sys/mman.h>
#endif
#ifdef USE_EPI
#  include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldmath.h"
#include "fldops.h"
#include "cgi.h"

/* Handle LIKEP stuff here, at least the new stuff to delay LIKEP rank
   calculation till as late as possible. */

#define BTCSIZE TXbtreecache

/******************************************************************/

int
TXcalcrank(tbl, pred, nrank, fo)
DBTBL *tbl;
PRED *pred;
int *nrank;
FLDOP *fo;
{
	static CONST char	fn[] = "TXcalcrank";
	int lrank, nleft = 0, ret;
	int rrank, nright = 0;
	DDMMAPI *ddmmapi;
	FLD	*resFld = FLDPN;
        MMAPI   *mm;
	PROXBTREE *bt;
	size_t sz, buflen, toread, nread, n;
	byte *v = NULL;
	FILE *fd;
	int order = 0, maxorder;
	ft_int	saveTblRank;
	ft_blobi	*blobi = NULL;

	if(!pred)
		goto err;

	switch(pred->op)
	{
		case FOP_AND: /* Average ranks */
			lrank = TXcalcrank(tbl, pred->left, &nleft, fo);
			rrank = TXcalcrank(tbl, pred->right, &nright, fo);
			*nrank = nleft + nright;
			/* Bug 4199: if either side is false, AND is false: */
			ret = (lrank && rrank ? lrank + rrank : 0);
			break;
		case FOP_OR: /* Take Max rank */
			lrank = TXcalcrank(tbl, pred->left, &nleft, fo);
			rrank = TXcalcrank(tbl, pred->right, &nright, fo);
			/* Bug 4207: If a PRED refers to $rank and
			 * `tbl->rank' is 0, tup_match() will call
			 * TXcalcrank() to calculate it -- using the
			 * top-level `tbl->pred' -- causing infinite
			 * recursion.  Set a non-zero `tbl->rank' for
			 * tup_match(), to avoid this.  Will not be
			 * wholly accurate, but then that is
			 * impossible, as we do not yet know the
			 * overall rank anyway.  Make sure to set it
			 * only temporarily: an already-non-zero
			 * `tbl->rank' *here* is assumed to be an
			 * *input* -- the tup_read_index() index rank,
			 * which we may use below for LIKE{3,R}.
			 */
			saveTblRank = tbl->rank;	/* save before chg */
			if (tbl->rank <= 0)		/* prevent recursion*/
			{
				tbl->rank = TX_MAX((nleft ? lrank / nleft :
						    RPPM_BEST_RANK),
						   (nright ? rrank / nright :
						    RPPM_BEST_RANK));
				if (tbl->rank <= 0)
					tbl->rank = 1;
			}
			if(nleft)
				 lrank /= nleft;
			else if(tup_match(tbl, pred->left, fo))
					lrank = RPPM_BEST_RANK;
			if(nright)
				rrank /= nright;
			else if(tup_match(tbl, pred->right, fo))
				 rrank = RPPM_BEST_RANK;
			tbl->rank = saveTblRank;	/* restore it */
			*nrank = 1;
			if(lrank > rrank)
				ret = lrank;
			else
				ret = rrank;
			break;
		case FOP_NMM:			/* LIKE3 */
		case FOP_RELEV:			/* LIKER */
			/*   Bug 4199 side-effect: LIKE3 and LIKER are never
			 * supposed to go linear, but we were trying to
			 * rppm_rankbuf()-rank them here when an index-only
			 * LIKE (which thus became a LIKE3 after index use)
			 * was ANDed with a post-proc/linear LIKEP:
			 * rppm_rankbuf() failed (rank 0) on the LIKE[3]
			 * because RPF_MMINFO set but no mminfo to rank with,
			 * and our Bug 4199 TXcalcrank() AND fix thus made
			 * the AND fail because one side was rank 0 (Texis
			 * test158).  (We did not see the issue previous to
			 * the Bug 4199 AND fix, because then AND was *not*
			 * failing when that LIKE3 erroneously ranked 0.)
			 *   Fix is to return a non-zero rank for LIKE{3,R}
			 * instead of trying rppm_rankbuf(), since we "know"
			 * the LIKE{3,R} already passed.  Ideally this should
			 * be the index rank that was already determined,
			 * and passed up by tup_read_indexed() via tbl->rank.
			 * It might have been altered by indexand(), but still
			 * has more per-row relevance than the fallback --
			 * a fixed rank of RPPM_BEST_RANK.  For insurance,
			 * only use tbl->rank if pred->handled, which should
			 * help indicate this PRED was handled by the index
			 * (that we assume set tbl->rank...).
			 *   Note that just because `pred->handled' is true,
			 * we cannot assume descendant PREDs are also
			 * handled (e.g. Texis test623), so we cannot use
			 * `tbl->rank' for e.g. FOP_AND/FOP_OR.
			 */
			*nrank = 1;
			ret = (tbl->rank > 0 && pred->handled ? tbl->rank :
			       RPPM_BEST_RANK);
			break;
		case FOP_MM:			/* LIKE */
		case FOP_PROXIM:		/* LIKEP */
			ddmmapi = (DDMMAPI *)getfld(pred->right, NULL);
                        if(!ddmmapi)/* No query */
                               goto err;
			bt = ddmmapi->bt;
                        mm = ddmmapi->mmapi;

			*nrank = *nrank + 1;

			if(!bt) /* WTF */
			{
				FLD *f = FLDPN;

				if(pred->lt == NAME_OP)
					f = dbnametofld(tbl, pred->left);
				else if(pred->lt == FIELD_OP)
					f = pred->left;
				else		/* Bug 7412 wtf */
				{
					putmsg(MERR, __FUNCTION__,
					       "Internal error: cannot determine usable FLD");
					goto err5;
				}
				bt = TXmkprox(mm, f, pred->op);
				if(!bt)
				{
				err5:
					ret = 5;
					goto done;
				}
				ddmmapi->bt = bt;
			}

			v = getfld(bt->f, &sz);
			if (!v) goto err;
			bt->cnt++;
			bt->r->curRecid = tbl->recid;	/* for RPPM tracing */
			switch(bt->f->type & DDTYPEBITS)
			{
				case FTN_STRLST:
					/* Do not index the ft_strlst header,
					 * just the data, which is nul-term.
					 * strings regardless of delimiter:
					 * KNG 20080319
					 */
					n = TX_STRLST_MINSZ;	/* skip hdr */
					if (n > sz) n = sz;	/*  if room */
					v = (byte *)v + n;
					sz -= n;
					/* fall through: */
				case	FTN_CHAR:
				case	FTN_BYTE:
				indexChar:
					order = rppm_rankbuf(bt->r, mm, v,
							v + sz, SIZE_TPN);
					DBGMSG(9, (999, NULL, "Kai says %d",
							order));
					break;
				case	FTN_BLOBI:
					blobi = (ft_blobi *)v;
					v = TXblobiGetPayload(blobi, &sz);
					if (!v) goto err;
					order = rppm_rankbuf(bt->r, mm, v,
							v + sz, SIZE_TPN);
					DBGMSG(9, (999, NULL, "Kai says %d",
							order));
					TXblobiFreeMem(blobi);
					v = NULL;
					break;
				case	FTN_INDIRECT:
                                        if (*(char *)v == '\0')/* MAW 05-05-99 */
                                        {
                                           errno = ENOENT;
                                           goto err;
                                        }
                                        errno = 0;
					fd = fopen((char *)v, "rb");
					if (fd == (FILE *)NULL)
					{
                                          if (*(char *)v != '\0')
                                            putmsg(MERR+FOE, "proximity",
                                           "Cannot open indirect file %s: %s",
                                                   v, strerror(errno));
                                          goto err;
					}
					fseek(fd, 0L, SEEK_END);
					sz = ftell(fd);
					fseek(fd, 0L, SEEK_SET);
#ifdef HAVE_MMAP
					v = (unsigned char *)mmap((void *)NULL,
					    sz,PROT_READ|PROT_WRITE,MAP_PRIVATE,
					    fileno(fd), 0);
					if(v == (void *)-1)
					{
						fclose(fd);
						goto err;
					}
					fclose(fd);
					order = rppm_rankbuf(bt->r, mm, v,
							v + sz, SIZE_TPN);
					munmap((void *)v, sz);
					DBGMSG(9, (999, NULL, "Kai says %d",
							order));
					break;
#else /* HAVE_MMAP */
	/* Make a loop here */
	/* Need to read as many buffers of size ? till we get to flen */
					buflen = TXgetblockmax();
					toread = sz;
					v = (unsigned char *)TXmalloc(TXPMBUFPN, fn, buflen);
					maxorder = -1;
					while(toread > 0)
					{
                                          nread = fread(v, 1, buflen, fd);
                                          order = rppm_rankbuf(bt->r, mm,
                                                      v, v + nread, SIZE_TPN);
                                          if (order > maxorder)
                                            maxorder = order;
                                          toread -= nread;
					}
					order = maxorder;
					DBGMSG(9, (999, NULL, "Kai says %d",
							order));
					v = TXfree(v);
					fclose(fd);
#endif /* HAVE_MMAP */
					break;
				default:
					/* Convert to varchar: */
					if (bt->fldOp == FLDOPPN &&
					    (bt->fldOp = dbgetfo()) == FLDOPPN)
					{
						putmsg(MERR + MAE, fn,
							"Cannot open FLDOP");
						goto err;
					}
					if (bt->cnvFld == FLDPN &&
					    (bt->cnvFld = createfld("varchar",
							1, 0)) == FLDPN)
					{
						putmsg(MERR + MAE, fn,
							"Cannot open FLD");
						goto err;
					}
					putfld(bt->cnvFld, "", 0);
					if (fopush(bt->fldOp, bt->f) != 0 ||
					    fopush(bt->fldOp, bt->cnvFld)!=0 ||
					    foop(bt->fldOp, FOP_CNV) != 0 ||
					    (resFld = fopop(bt->fldOp))==FLDPN)
					{
						char	tmp[64];
						putmsg(MERR, fn,
    "Cannot convert index field type %s to varchar for Metamorph operator %s",
						       TXfldtypestr(bt->f),
						       TXqnodeOpToStr(pred->op,
							   tmp, sizeof(tmp)));
						goto err;
					}
					v = getfld(resFld, &sz);
					goto indexChar;
			}
			ret = order;
			break;
		default:			/* unhandled FOP_... */
			/* Bug 4199: give it a rank like we do for OR sides:*/
			*nrank = 1;
			/* bug 4207: ensure non-zero `tbl->rank' to avoid
			 * infinite recursion via tup_match():
			 */
			saveTblRank = tbl->rank;
			if (tbl->rank <= 0)
				tbl->rank = RPPM_BEST_RANK;
			ret = (tup_match(tbl, pred, fo) ? RPPM_BEST_RANK : 0);
			tbl->rank = saveTblRank;
			break;
	}
	goto done;

err:
	ret = 0;
done:
	if (resFld != FLDPN) closefld(resFld);
	return(ret);
}

/******************************************************************/

int
TXpredHasOp(PRED *p, QNODE_OP op)
/* Returns 1 if `p' tree contains `op', 0 if not.
 */
{
	if(!p)
		return 0;
	switch(p->op)
	{
		case FOP_AND:
		case FOP_OR:
			return(TXpredHasOp(p->left, op) ||
			       TXpredHasOp(p->right, op));
		default:
			return(p->op == op);
	}
}

/******************************************************************/
/*
	This is designed to do a post process on the data coming
	out, and calculate a more accurate rank.  Needs to be
	disabled if we used MMINV index, as we already have
	accurate ranks.
*/

int
dolikep(query, fo)
QNODE *query;
FLDOP *fo;
/* Returns 0 on success, -1 on error.
 */
{
	QUERY *q = query->q;
	QNODE	*parentQnode = query->parentqn;
	EPI_HUGEINT nrowsMatched = 0, nrowsReturned = 0;
	int nrank, rankval, needTrim, noSort;
	EPI_OFF_T t_rankval;
	double	endTime = 0.0;
	BTREE *btree;
	unsigned	btFlags;

	if (!TXpredHasOp(query->left->q->pred, FLDMATH_PROXIM))
	{
		return 0;
	}

	/* Bug 4166 comment #3: if we will do ORDER BY other than
	 * `$rank desc' later, no point in `$rank desc' sort here.
	 * More importantly, do not trim to likeprows either: may cut
	 * off a row that later ORDER BY would put first/early:
	 */
	noSort = (TXApp && TXApp->legacyVersion7OrderByRank ? 0 :
		  (parentQnode &&
		   (parentQnode->op == ORDER_OP ||
		    /* Earlier in TXsettablepred() (which indirectly
		     * calls setf3dbi()) we knew of `ORDER BY num'
		     * existence but not its schema yet; here we know
		     * the schema and can check it.
		     *
		     * Note that `SELECT $rank myFld ... ORDER BY 1'
		     * will be !TXprojIsRankDescOnly() because the
		     * latter cannot see that `myFld' is $rank; ok
		     * since false negatives are ok here, false
		     * positives are not:
		     */
		    parentQnode->op == ORDERNUM_OP) &&
		   parentQnode->q &&
		   parentQnode->q->proj &&
		   !TXprojIsRankDescOnly(parentQnode->q->proj)));

	btFlags = (BT_FIXED | BT_UNSIGNED);
	if (noSort) btFlags |= BT_LINEAR;
	btree = openbtree(NULL, BTFSIZE, BTCSIZE, btFlags, (O_RDWR | O_CREAT));
	if (!btree) return(-1);
	/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
	BTPARAM_INIT_TO_PROCESS_DEFAULTS(&btree->params, DDICPN/*WTF*/);

	/* `btree' will sort by rank desc; use reverse cmp w/positive ranks: */
	if (!(TXApp && TXApp->legacyVersion7OrderByRank))
		btsetcmp(btree, TXfixedUnsignedReverseCmp);

	if (TXlikeptime) endTime = TXgettimeofday() + (double)TXlikeptime;

	/* Post-process IINDEX/RAM B-tree of index results,
	 * adding matched rows with their post-proc rank to `btree'.
	 * Bug 6824: we cannot stop at likeprows yet, because
	 * ranks may change (e.g. increase) after post-proc;
	 * last row in index list might be highest final rank.
	 * Or (Bug 4166) a later ORDER BY other than `$rank desc'
	 * may also push rows higher that we would cut off here:
	 */
	while (TXdotree(query->left, fo, SQL_FETCH_NEXT, 1) != -1)
	{
		if (TXlikeptime && TXgettimeofday() >= endTime) break;
		nrank = 0;
		rankval = TXcalcrank(q->in1, query->left->q->pred, &nrank, fo);
		if(rankval > TXlikepthresh)
		{
			nrowsMatched++;
			t_rankval = TX_RANK_USER_TO_INTERNAL(TXApp, rankval);
			/* Note that if there is no sort, we could
			 * indeed trim to likeprows in this loop,
			 * saving the copy loop below.  But same
			 * conditions that make `noSort' true also
			 * preclude trimming to likeprows anyway.
			 */
			if (noSort)
				btappend(btree, &q->in1->recid,
					 sizeof(t_rankval), &t_rankval,
					 100, NULL);
			else
				btinsert(btree, &q->in1->recid,
					 sizeof(t_rankval), &t_rankval);
		}
	}
	btflush(btree);				/* BT_LINEAR needs flush */

	nrowsReturned = nrowsMatched;
	needTrim = (!noSort &&			/* Bug 4166 */
		    TXnlikephits &&
		    TXbtreeGetNumItemsDelta(btree) > TXnlikephits);

	if (TXtraceIndexBits & 0x30000)
	{
		txpmbuf_putmsg(TXPMBUFPN, MINFO, __FUNCTION__,
		 "%wkd B-tree %p records after post-processing for LIKEP%s%s",
			       (EPI_HUGEINT)TXbtreeGetNumItemsDelta(btree),
			       btree,
			       (noSort ?
	       " (no rank sort nor likeprows trim: ORDER BY not $rank desc)" :
				(needTrim ?
			     " (and rank sorting; before trim to likeprows)" :
		 " (and rank sorting; less than likeprows; no trim needed)")),
			       ((TXtraceIndexBits & 0x20000) ? ":" : ""));
		if (TXtraceIndexBits & 0x20000)
			TXbtreeDump(TXPMBUFPN, btree, 2, 0);
	}

	/* Bug 6824: do likeprows trim *after* rank sort: */
	if (needTrim)
	{
		BTREE		*btTrunc = NULL;
		BTLOC		btloc;
		EPI_OFF_T	key;
		size_t		keySz;

		/* Open new B-tree for truncation.  BT_APPEND; no
		 * re-sort needed:
		 */
		btTrunc = openbtree(NULL, BTFSIZE, BTCSIZE,
				    (BT_FIXED | BT_UNSIGNED | BT_LINEAR),
				    (O_RDWR | O_CREAT));
		if (!btTrunc)
		{
			btree = closebtree(btree);
			return(-1);
		}
		/* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
		BTPARAM_INIT_TO_PROCESS_DEFAULTS(&btree->params,DDICPN/*WTF*/);

		if (!(TXApp && TXApp->legacyVersion7OrderByRank))
			btsetcmp(btree, TXfixedUnsignedReverseCmp);

		/* Copy likeprows hits to `btTrunc': */
		rewindbtree(btree);
		for (nrowsReturned = 0;
		     nrowsReturned < TXnlikephits;
		     nrowsReturned++)
		{
			keySz = sizeof(EPI_OFF_T);
			btloc = btgetnext(btree, &keySz, &key, NULL);
			if (!TXrecidvalid(&btloc)) break;
			btappend(btTrunc, &btloc, keySz, &key, 100, BTBMPN);
		}
		btflush(btTrunc);

		/* `btTrunc' is the new result B-tree: */
		btree = closebtree(btree);
		btree = btTrunc;
		btTrunc = NULL;

		if (TXtraceIndexBits & 0x30000)
		{
			txpmbuf_putmsg(TXPMBUFPN, MINFO, __FUNCTION__,
		  "%wkd B-tree %p records after trimming to %wkd likeprows%s",
			       (EPI_HUGEINT)TXbtreeGetNumItemsDelta(btree),
				       btree, (EPI_HUGEINT)TXnlikephits,
				   ((TXtraceIndexBits & 0x20000) ? ":" : ""));
			if (TXtraceIndexBits & 0x20000)
				TXbtreeDump(TXPMBUFPN, btree, 2, 0);
		}
	}

	closedbidx(&q->in1->index);
	q->in1->index.btree = btree;
	q->in1->index.nrank = nrank;
	q->in1->index.type = DBIDX_MEMORY;
	q->in1->indguar = 1;
	/* Bug 6172: let others know we populated `q->in1': */
	q->flags |= TXqueryFlag_In1Populated;

	/* KNG 20081210 update row count stats for Vortex: */
	query->countInfo.rowsMatchedMin = nrowsMatched;
	query->countInfo.rowsMatchedMax = nrowsMatched;
	/* can also set returned rows, since result set is all done now: */
	query->countInfo.rowsReturnedMin = nrowsReturned;
	query->countInfo.rowsReturnedMax = nrowsReturned;

	rewindbtree(btree);
	TXresetnewstats(q->in1->nfldstat);

	return 0;
}
