/* -=- kai-mode: john -=- */
#ifndef NO_USE_EXTRA
#include "txcoreconfig.h"
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include "dbquery.h"
#include "texint.h"
#include "http.h"

/******************************************************************/

#ifdef SMALL_MEM
#define BTCSIZE 5
#else
#define BTCSIZE 20
#endif

/******************************************************************/
static FASTBUF *fastbufinit ARGS((void *buf, TBL *tbl, size_t sz));
static FASTBUF *
fastbufinit(buf, tbl, sz)
void *buf;
TBL *tbl;
size_t sz;
{
	static const char	fn[] = "fastbufinit";
	FASTBUF *rc;
	size_t	i;
	FLD *fld;

	rc = TXcalloc(TXPMBUFPN, fn, tbl->n, sizeof(FASTBUF));
	if (!rc) return(NULL);
	buftofld(buf, tbl, sz);
	for (i = 0; i < tbl->n; i++)
	{
		fld = tbl->field[i];
		if (fldisvar(fld))
		{
			rc = TXfree(rc);	/* Can't fastbuf varflds */
			return NULL;
		}
		rc[i].fld = fld;
		rc[i].off = (byte *) fld->v - (byte *) buf;
	}
	return rc;
}

/******************************************************************/

static int fastbuftofld ARGS((void *, TBL *, size_t, FASTBUF *));

static int
fastbuftofld(buf, tbl, sz, fbufinfo)
void *buf;
TBL *tbl;
size_t sz;
FASTBUF *fbufinfo;
{
	size_t	i;
	FLD *fld;

	(void)sz;
	for (i = 0; i < tbl->n; i++)
	{
		fld = fbufinfo[i].fld;
		fld->shadow = (byte *) buf + fbufinfo[i].off;
		setfldv(fld);
	}
	return 0;
}

/******************************************************************/

static EXTRA *exscache = NULL;
static PRED *predcache = NULL;
static char *mmqcache = NULL;
static char *fldncache = NULL;

EXTRA *
closeextra(exs, type)
EXTRA *exs;
int type;
{
	static const char	fn[] = "closeextra";
	int			isTheCache = (exs && exs == exscache);

	if (exs)
	{
		if ((TXtraceIndexBits & 0x40000) && isTheCache)
			txpmbuf_putmsg(TXPMBUFPN, MINFO, fn,
				   "Closing indexcache EXTRA object %p", exs);
		if(type == 1)
		{
			pred_rmalts(exs->goodpred);
			pred_rmfieldcache(exs->origpred, exs->dbtbl);
			exs->btree = closebtree(exs->btree);
		}
		else
		{
			exs->keyrec = keyrecclose(exs->keyrec);
			exs->fbufinfo = TXfree(exs->fbufinfo);
		}
		exs->dbtbl = closedbtbl(exs->dbtbl);
		exs->fc = TXclosefldcmp(exs->fc);
		exs = TXfree(exs);
		if (isTheCache)
		{
			exscache = NULL;
			TXclosecachedindexdata();
		}
	}
	return NULL;
}

/******************************************************************/

int
TXgetcachedindexdata(byte ** data, size_t * recsz, size_t * count)
{
	static const char	fn[] = "TXgetcachedindexdata";
	KEYREC *keyrec;

	if (exscache && exscache->keyrec)
	{
		if (TXtraceIndexBits & 0x40000)
		{
			txpmbuf_putmsg(TXPMBUFPN, MINFO, fn,
			  "Re-using indexcache EXTRA object %p (%d results)",
				       exscache, (int)exscache->keyrec->used);
		}
		keyrec = exscache->keyrec;
		*data = keyrec->items;
		*recsz = keyrec->keysz;
		*count = keyrec->used;
		return 0;
	}
	return -1;
}

/******************************************************************/

int
TXclosecachedindexdata()
/* Closes indexcache `exscache', if not in use.
 */
{
	if (exscache)
	{
		if (exscache->iscached == 2)
		{				/* PROXBTREE done with it */
			if (exscache->keyrec)
				exscache->keyrec = keyrecclose(exscache-> keyrec);
			exscache->fbufinfo = TXfree(exscache->fbufinfo);
			exscache = closeextra(exscache, 1);
		}
		else
		{
			/* Possibly in use by PROXBTREE (1), or not for
			 * indexcache use at all (0).  Mark not for use
			 * so we can remove from `exscache' on close:
			 */
			exscache->iscached = 0;
		}
	}
	mmqcache = TXfree(mmqcache);
	fldncache = TXfree(fldncache);
	if (predcache)
		predcache = closepred(predcache);
	return 0;
}

/******************************************************************/

EXTRA *
TXiextraForIndexCache(TBSPEC *tbspec, DD *auxdd, size_t auxsz, int *inv,
		      DBTBL *dbtbl, DDMMAPI *ddmmapi, char *fldname)
/* Like iextra(), but uses and/or creates indexcache `exscache'
 */
{
	static const char	*fn = __FUNCTION__;
	EXTRA *exs = NULL;
	TXPMBUF	*pmbuf = (dbtbl && dbtbl->ddic ? dbtbl->ddic->pmbuf : NULL);

	pred_rmalts(tbspec->pred);
	exs = (EXTRA *)TXcalloc(pmbuf, fn, 1, sizeof(EXTRA));
	if (!exs) goto err;
	exs->dbtbl = (DBTBL *)TXcalloc(pmbuf, fn, 1, sizeof(DBTBL));
	if (!exs->dbtbl) goto err;
	exs->dbtbl->tbl = createtbl(auxdd, NULL);
	if (!exs->dbtbl->tbl) goto err;
	exs->dbtbl->type = TEXIS_TABLE;
	exs->dbtbl->lname = TXstrcatN(pmbuf, fn, "EXTRA object for table ",
				      dbtbl->lname, NULL);
	exs->goodpred = TXmakepredvalid(tbspec->pred, exs->dbtbl, 0, 0, 0);
	pred_sethandled(exs->goodpred);
	if (pred_allhandled(tbspec->pred))	/* WTF ??? Don't need invert
						   if doing complete query */
	{
		static const char	predDiffers[] =
			"Aux predicate differs from indexcache";
		const char	*reason;

		*inv = 0;
		ddmmapi->lonely = 1;
		if (!predcache || !mmqcache || !fldncache || !exscache)
			reason = "No previous indexcache data";
		else if (!TXpredcmp(exs->goodpred, predcache))
			reason = predDiffers;
		else if (TXstrcmp(mmqcache, ddmmapi->query) != 0)
			reason = "Metamorph query differs from indexcache";
		else if (TXstrcmp(fldncache, fldname) != 0)
			reason ="Metamorph field name differs from indexcache";
		else
			reason = NULL;
		if (!reason)			/* no reason not to use cache*/
		{				/* so re-use it (`exscache')*/
			closeextra(exs, 1);
			if (TXtraceIndexBits & 0x40000)
				txpmbuf_putmsg(pmbuf, MINFO, fn,
			   "Re-using indexcache EXTRA object %p (%d results)",
					       exscache,
					       (int)(exscache->keyrec ?
						 exscache->keyrec->used : 0));
			exs = exscache;
			if (exs->keyrec)
			{
				exs->keyrec->sorted = 0;
				exs->cachedkeyrec = 1;	/* is indexcache */
			}
			else
				exs->cachedkeyrec = 0;
		}
		else				/* no usable indexcache */
		{
			if ((TXtraceIndexBits & 0x40000) && exscache)
			{
				txpmbuf_putmsg(pmbuf, MINFO, __FUNCTION__,
				"Not re-using indexcache EXTRA object %p: %s",
					       exscache, reason);
				if (reason == predDiffers &&
				    (TXtraceIndexBits & 0x80000))
				{
					HTBUF	*buf;
					char	*s;

					if ((buf = openhtbuf()) != NULL)
					{
						TXdumpPred(buf, exs->goodpred,
							   0);
						htbuf_getdata(buf, &s, 0);
						txpmbuf_putmsg(pmbuf, MINFO,
							       __FUNCTION__,
			      "indexcache: Candidate predicate is:\n%s",
						       s);
						htbuf_clear(buf);
						TXdumpPred(buf, predcache, 0);
						htbuf_getdata(buf, &s, 0);
						txpmbuf_putmsg(pmbuf, MINFO,
							       __FUNCTION__,
			      "indexcache: Candidate differs from existing cached predicate:\n%s",
						       s);
						buf = closehtbuf(buf);
					}
				}
			}
			TXclosecachedindexdata();
			mmqcache = TXstrdup(pmbuf, fn, ddmmapi->query);
			fldncache = TXstrdup(pmbuf, fn, fldname);
			predcache = duppred(exs->goodpred);
			if (TXtraceIndexBits & 0x40000)
			{
				DD	*dd;
				char	fieldsBuf[1024];

				dd = convertdd(auxdd, 0);
				if (dd)
				{
					TXddPrintFieldNames(fieldsBuf,
							 sizeof(fieldsBuf) - 1,
							    dd);
					fieldsBuf[sizeof(fieldsBuf)-1] = '\0';
				}
				else
					*fieldsBuf = '\0';
				txpmbuf_putmsg(pmbuf, MINFO, fn,
					       "indexcache: Caching aux fields `%s' of Metamorph query `%s' on field `%s' of table `%s' via EXTRA object %p",
					       fieldsBuf, mmqcache, fldncache,
					       dbtbl->lname, exs);
			}
			if (TXtraceIndexBits & 0x80000)
			{
				HTBUF	*buf;
				char	*s;

				if ((buf = openhtbuf()) != NULL)
				{
					TXdumpPred(buf, predcache, 0);
					htbuf_getdata(buf, &s, 0);
					txpmbuf_putmsg(pmbuf, MINFO, fn,
					 "indexcache: Caching predicate:\n%s",
						       s);
					buf = closehtbuf(buf);
				}
			}
			exscache = exs;
			exscache->iscached = 1;	/* 1: save for indexcache */
			exs->goodpred = predcache;
		}
	}
	exs->extrasz = auxsz;
	if (!*inv)
	{
		if(NULL==(exs->btree=TXsetupauxorder(exs,auxdd,tbspec,dbtbl)))
		{
			exs->fc = TXopenfldcmp(NULL,TXOPENFLDCMP_CREATE_FLDOP);
			if (!exs->fc) goto err;
			exs->btree = openbtree(NULL, BTFSIZE, BTCSIZE,
					       (BT_FIXED | BT_UNSIGNED),
					       (O_RDWR | O_CREAT));
			if (!exs->btree) goto err;
		}
	}
	else
	{
		exs->fc = TXopenfldcmp(NULL,TXOPENFLDCMP_CREATE_FLDOP);
		if (!exs->fc) goto err;
		exs->btree = openbtree(NULL, BTFSIZE, BTCSIZE,
				       (BT_FIXED | BT_UNSIGNED),
				       (O_RDWR | O_CREAT));
		if (!exs->btree) goto err;
	}
	/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
	 * stringcomparemode:
	 */
	BTPARAM_INIT_TO_PROCESS_DEFAULTS(&exs->btree->params, dbtbl->ddic);
	goto finally;

err:
	exs = closeextra(exs, 1);
finally:
	return exs;
}

/******************************************************************/

EXTRA *
iextra(tbspec, auxdd, auxsz, inv, dbtbl, nbtree)
TBSPEC *tbspec;
DD *auxdd;
size_t auxsz;
int *inv;
DBTBL *dbtbl;
int nbtree;
{
	static const char	fn[] = "iextra";
	DD *dd;
	FLDOP *fo = NULL;
	EXTRA *exs;
	BTREE *rc = BTREEPN;
	int i;
	TXPMBUF	*pmbuf = (dbtbl && dbtbl->ddic ? dbtbl->ddic->pmbuf : NULL);

	pred_rmalts(tbspec->pred);
	exs = (EXTRA *)TXcalloc(pmbuf, fn, 1, sizeof(EXTRA));
	if (!exs) goto err;
	exs->dbtbl = (DBTBL *)TXcalloc(pmbuf, fn, 1, sizeof(DBTBL));
	if (!exs->dbtbl) goto err;
	exs->dbtbl->tbl = createtbl(auxdd, NULL);
	if (!exs->dbtbl->tbl) goto err;
	exs->dbtbl->type = TEXIS_TABLE;
	exs->dbtbl->lname = TXstrcatN(pmbuf, fn, "EXTRA object for table ",
				      dbtbl->lname, NULL);
	if (tbspec->gpred)
	{
		exs->goodpred =
			TXmakepredvalid(tbspec->gpred, exs->dbtbl, 0, 0, 0);
		exs->origpred = tbspec->gpred;
	}
	else
	{
		exs->goodpred =
			TXmakepredvalid(tbspec->pred, exs->dbtbl, 0, 0, 0);
		exs->origpred = tbspec->pred;
	}
	if (!exs->goodpred || exs->goodpred->assumetrue)
		goto err;
	pred_sethandled(exs->goodpred);
	if (pred_allhandled(tbspec->pred))	/* WTF ??? Don't need invert
						   if doing complete query */
		*inv = 0;
	exs->extrasz = auxsz;
	fo = dbgetfo();
	if (!*inv && nbtree)
	{
		PROJ *proj = tbspec->proj;
		int didorder;

		if (proj)
		{
			didorder = 1;
			dd = convertdd(auxdd, 0);
			for (i = 0; i < dd->n; i++)
				dd->fd[i].order = OF_DONT_CARE;
			for (i = 0; i < proj->n; i++)
			{
				TXOF	rev;
				int sz;
				int nn;
				char *fname, *nname;

				fname = TXdisppred(proj->preds[i], 0, 0, 1000);
#ifdef TX_USE_ORDERING_SPEC_NODE
				rev = proj->preds[i]->orderFlags;
#else /* !TX_USE_ORDERING_SPEC_NODE */
				rev = (TXOF)0;
				if (fname[strlen(fname) - 1] == '^')	/* IGNCASE ? */
				{
					fname[strlen(fname) - 1] = '\0';
					rev |= OF_IGN_CASE;
				}
				if (fname[strlen(fname) - 1] == '-')	/* DESC ? */
				{
					fname[strlen(fname) - 1] = '\0';
					rev |= OF_DESCENDING;
				}
#endif /* !TX_USE_ORDERING_SPEC_NODE */
				nname =
					predtype(proj->preds[i], exs->dbtbl,
						 fo, &sz, &nn);
				if (!nname)
				{
					putmsg(MWARN, NULL,
					       "Can't handle ORDER BY");
					didorder = 0;
				}
				else
				{
					ddsetordern(dd, fname, rev);
				}
				fname = TXfree(fname);
			}
			if (didorder)
				dbtbl->order = dupproj(proj);
			exs->keyrec =
				keyrecopen(NULL, exs->extrasz, 0, 0, 0, 10000,
					   dd);
			if (nbtree)
			{
				rc =
					openbtree(NULL, BTFSIZE, BTCSIZE,
						  BT_LINEAR, O_RDWR | O_CREAT);
				if (!rc)
					goto err;
				btreesetdd(rc, dd);
				/* KNG 20100706 Bug 3210: Linear ORDER BY was not using
				 * stringcomparemode:
				 */
				BTPARAM_INIT_TO_PROCESS_DEFAULTS(&rc->params, dbtbl->ddic);
			}
/* WTF - want rank ? */
			if (!rc) goto err;
			if (!(exs->fc = TXopenfldcmp(rc, fo))) goto err;
			if (nbtree)
			{
				rc->usr = exs->fc;
				btsetcmp(rc, (btcmptype) fldcmp);
				exs->btree = rc;
			}
			dd = closedd(dd);
		}
		else
		{
			if (!(exs->fc = TXopenfldcmp(NULL, fo))) goto err;
			if (nbtree)
			{
				rc =
					openbtree(NULL, BTFSIZE, BTCSIZE,
						  BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
				if (!rc) goto err;
				/* KNG 20100706 Bug 3210: Linear ORDER
				 * BY was not using stringcomparemode:
				 */
				BTPARAM_INIT_TO_PROCESS_DEFAULTS(&rc->params, dbtbl->ddic);
			}
		}
	}
	else
	{
		if (!(exs->fc = TXopenfldcmp(NULL, fo))) goto err;
		if (nbtree)
		{
			rc =
				openbtree(NULL, BTFSIZE, BTCSIZE,
					  BT_FIXED | BT_UNSIGNED, O_RDWR | O_CREAT);
			if (!rc) goto err;
			/* KNG 20100706 Bug 3210: Linear ORDER BY was
			 * not using stringcomparemode:
			 */
			BTPARAM_INIT_TO_PROCESS_DEFAULTS(&rc->params, dbtbl->ddic);
		}
	}
	goto finally;

err:
	if (fo)
		foclose(fo);
	exs = closeextra(exs, 1);
finally:
	return(exs);
}

/******************************************************************/

int
iextraok(exs, recid, auxfld, bufsz)
EXTRA *exs;
RECID recid;
void *auxfld;
size_t bufsz;

/* We check here if there are any where clauses we can use to discard
 * this record.  Returns nonzero if this record matches.  Note that
 * this could be a deleted recid, so we can't save the recid.
 */
{
	DBTBL *dbtbl;
	size_t extrasz = 0;
	FLDOP *fo;
	PRED *goodpred;

	(void)recid;
	if (auxfld != NULL && exs != EXTRAPN && exs->goodpred)
	{
		dbtbl = exs->dbtbl;
		extrasz = exs->extrasz;
		fo = exs->fc->fo;
		goodpred = exs->goodpred;
		if (extrasz == 0)
		{
			buftofld(auxfld, dbtbl->tbl, bufsz);
		}
		else
		{
			if (!exs->fbufinfo)
				exs->fbufinfo =
					fastbufinit(auxfld, dbtbl->tbl,
						    extrasz);
			else
				fastbuftofld(auxfld, dbtbl->tbl, extrasz,
					     exs->fbufinfo);
		}
		if (goodpred->lat == FIELD_OP && goodpred->rt == FIELD_OP)
		{
			FLD *f1, *f2;
			FLD f3;

			initfld(&f3, FTN_INT, 1);
			f1 = goodpred->altleft;
			f2 = goodpred->right;
			if (!exs->cmpfunc)
				fogetop(fo, f1->type, f2->type,
					&exs->cmpfunc);
			if (exs->cmpfunc)
			{
				if (exs->cmpfunc(f1, f2, &f3, goodpred->op) ==
				    0)
					return *(int *) getfld(&f3, NULL);
			}
		}
		if (!tup_match(dbtbl, goodpred, fo))
		{
			return 0;	/* 0 = doesn't match */
		}
	}
	return 1;
}
#endif
