/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#ifdef _WIN32
#  include <io.h>
#endif /* _WIN32 */
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include "unistd.h"
#endif
#include "texint.h"
#include "fldcmp.h"
#include "jtreadex.h"
#include "fdbi.h"
#include "meter.h"
#include "ramdbf.h"		/* for closerdbf() prototype */
#include "cgi.h"
#ifdef NEED_GETOPT_H
#  ifdef NEED_EPIGETOPT_H
#    include "epigetopt.h"
#  else /* !NEED_EPIGETOPT_H */
#    include <getopt.h>
#  endif /* !NEED_EPIGETOPT_H */
#endif
#if defined(MSDOS) && !defined(fullpath)
#  define fullpath _fullpath
#endif

/* Linear in memory index seems kaput */
#ifdef NEVER
#ifndef NO_USE_MEM_INDEX
# define NO_USE_MEM_INDEX 1
#endif
#else
# undef NO_USE_MEM_INDEX
#endif

extern int optind;
extern char *optarg;

extern char *TxBtreeErr;
static CONST char UnkErr[] = "Unknown error";

#define BTREE_ERR()     (TxBtreeErr != CHARPN ? TxBtreeErr : (char *)UnkErr)
static CONST char CantCreateIndex[] = "Could not create index file %s: %s";

#define CantCreateIndex ((char *)CantCreateIndex)

static CONST char * CONST emptylist[] = { "" };

#define BTCSIZE	TXbtreecache

struct TXMKIND_tag
{
	byte fldtype;				/* field type & DDTYPEBITS */
	FLDOP	*fldOp;				/* for converting to varchar*/
	FLD	*cnvFld;			/* "" */

	FLD *fl, *flo;
	TBL *tb, *tb2;

	BTREE *bt;
	FLDCMP *fc;
	unsigned long token;

	size_t blockmax;
	char **expressions;
        char    *locale;
	TXCFF	textSearchMode;	/* textsearchmode for index */
	int	fdbiVersion;
	FFS *ex;
	uchar *buf;
	BTREE *ct;		/* Tree to handle term count in docs */
	EPI_OFF_T tblsize;		/* Table size */
	int itype;		/* INDEX_... type */
	void *indexobj;		/* subsidiary index object */
	size_t memlimit;
	int output;
	TXA2IND *a2i;
	INDEXSTATS indexstats;
	char	*path;		/* root file name WITH EXTRA SPACE */
	char	*indexName;			/* name of index */
	METER	*meter;		/* primary index meter */
	EPI_OFF_T metersize;	/* its size */
	byte	didFinish;			/* !0 if wtix_finish called */
	byte	isRebuild;			/* 1: rebuild  0: create */
	byte	isUnique;
	char	collSeq;			/* COLL_ASC or COLL_DESC */
	int	finishCode;			/* wtix_finish() result */
	TXMMPARAMTBLINFO	paramTblInfo;	/* WTIX stats for _P.tbl */
	size_t	auxDataSz;
};

/******************************************************************/

int
TXcountterms(char *s)
{
	char *p;
	int n = 0;

	p = s;
	p = strtok(s, " ");
	while (p)
	{
		if (*p != '-')
			n++;
		p = strtok(NULL, " ");
	}
	return n;
}

/******************************************************************/

#if PM_FLEXPHRASE
#  ifndef NEW_PHRASEPROC
#    define NEW_PHRASEPROC
#  endif
#endif

/******************************************************************/

#define EL(x) mmapi->mme->el[x]
#define NELS  mmapi->mme->nels

/******************************************************************/

/*
	Metamorph Inverted Counter Index

	Plan:

		Indexing:
		---------
		Store word (with a leading - if not logic)
		In place of word number, store set number
		Store count of sets instead of count of words
		In the long we can store the following:
			np: number of LOGIANDs (requireds, will be upfront)
			ns: number of required LOGISETSs
			kp: can we know this?  Flag for post proc. (phrase/rex)

		Searching:
		----------
		Find of any - word automatically disqualifies.
		Array of MAXSELS ints, zeroed.
		For each matching hit scan wordlist increment array[word].
		Get number of desired sets.
		See if that many are set.
*/

 /**/
/******************************************************************/
#ifndef NO_EMPTY_LIKEIN
static int
addword(byte *term, size_t len, int pos, BTLOC at, WTIX *ix, TXLOGITYPE logic,
	void *auxfld		/* (opt.) aux data, iff compound index */
	)
/* Returns -1 on error, 0 on success.
 */
{
	static char *tbuf = NULL;
	static size_t tbufsz = 0;

	if (logic != LOGINOT)
	{
		if (!wtix_insertloc(ix, (char *) term, len, auxfld, at, pos))
			return(-1);
	}
	else
	{
		if (tbufsz < len + 2)
		{
			tbufsz = len + 20;
			tbuf = TXfree(tbuf);
			tbuf = TXmalloc(TXPMBUFPN, __FUNCTION__, tbufsz);
			if (!tbuf)
				return -1;
		}
		tbuf[0] = '-';
		memcpy(tbuf + 1, term, len);
		wtix_insertloc(ix, tbuf, len + 1, auxfld, at, pos);
	}
	return 0;
}

/******************************************************************/

int
TXinsertMetamorphCounterIndexRow(query, auxfld, at, ix)
char *query;
void	*auxfld;	/* (opt.) aux data iff compound index */
BTLOC at;
WTIX *ix;
/* Returns -1 on error, else some sort of non-negative count.
 */
{
	static APICP *cp = NULL;
	static MMAPI *mmapi = NULL;
	PMPHR *tph;
	int i, j, fup = 0, np = 0, ns = 0, ret;

	if (!query && !ix)
	{
		if (mmapi)
			mmapi = closemmapi(mmapi);
		if (cp)
			cp = closeapicp(cp);
		goto ok;
	}
	if (!cp)
	{
		TXget_globalcp();
		cp = dupapicp(globalcp);
	}
	if (!cp)
	{
		putmsg(MWARN, NULL, "dupapicp Failed");
		goto err;
	}
	cp->qmaxsets = 0;
	cp->qmaxsetwords = cp->qmaxwords = 0;	/* KNG 990816 */
	if (!mmapi)
		mmapi = openmmapi(NULL, TXbool_False, cp);
	if (mmapi == (MMAPI *) NULL)
	{
		putmsg(MWARN, NULL, "Openmmapi Failed");
		goto err;
	}
	if (!setmmapi(mmapi, query, TXbool_False))
	{
		if (!wtix_insertloc(ix, NULL, 0, auxfld, at, -1)) goto err;
		goto ok;
	}
	ns = mmapi->mme->intersects + 1;
	for (i = 0; i < NELS; i++)	/* perform initial count */
	{
		byte **lst = (byte **) (EL(i)->lst);

		switch (EL(i)->logic)
		{
		case LOGIAND:
			np = i;
			break;
		default:
			break;
		}
		switch ((EL(i)->pmtype))
		{
		case PMISPPM:
			for (j = 0; j < EL(i)->lstsz; j++)
			{
				if (TX_PPM_PHRASE(EL(i)->ps, j))
				{
					for (tph = TX_PPM_PHRASE(EL(i)->ps, j);
					     tph && tph->prev;
					     tph = tph->prev);
					for (; tph; tph = tph->next)
					{
						if (EL(i)->logic != LOGINOT)
						{
                                                  if (addword(tph->term,
                                                              tph->len, i,
                                                              at, ix,
                                                              EL(i)->logic,
							      auxfld) < 0)
                                                    goto err;
                                                }
					}
					if (fup == -1)
						fup = i;
				}
				else
                                  {
                                    if (addword(lst[j],
						strlen((char *) lst[j]), i,
						at, ix, EL(i)->logic, auxfld)
					< 0)
                                      goto err;
                                  }
			}
			break;
		case PMISSPM:
			if (EL(i)->ss->phrase)
			{
				for (tph = EL(i)->ss->phrase;
				     tph && tph->prev; tph = tph->prev);
				for (; tph; tph = tph->next)
				{
					if (EL(i)->logic != LOGINOT)
                                          {
                                            if (addword(tph->term, tph->len,
							i, at, ix,
							EL(i)->logic, auxfld)
						< 0)
                                              goto err;
                                          }
				}
				if (fup == -1)
					fup = i;
			}
			else
                          {
                            if (addword(lst[0], strlen((char *) lst[0]), i,
					at, ix, EL(i)->logic, auxfld) < 0)
                              goto err;
                          }
			break;
		default:
			if (fup == -1)
				fup = i;
			if (EL(i)->logic != LOGINOT)
				ns--;
			break;
		}
	}
	if (fup < np)
	{
		ns += np - fup;
		np = fup;
	}
	ret = (np << 16) + (fup << 8) + ns;
	goto finally;

ok:
	ret = 0;
	goto finally;
err:
	ret = -1;
finally:
	return(ret);
}
#endif /* EMPTY_LIKEIN */

/******************************************************************/
/*      Insert an item into a 3db or full-metamorph-inversion index.
 *      Returns 0 on success, -1 on "safe" error, -2 on major error.
 */

static int do3orfind ARGS((TXMKIND * ind, FLD *vFld, void *v, size_t sz, BTLOC at,
			   char *indfile, void *extra));
static int
do3orfind(ind, vFld, v, sz, at, indfile, extra)
TXMKIND *ind;			/* The index to insert into */
FLD *vFld;			/* The field containing `v' */
void *v;			/* Item to insert */
size_t sz;			/* Size of the item */
BTLOC at;			/* The location of the item */
char *indfile;
void *extra;			/* Extra data associated with BTLOC */
{
	static CONST char Fn[] = "do3orfind";
	EPI_OFF_T token = ind->token;
	int	ret;
	FLD	*resFld = FLDPN;
	EPI_OFF_T nterms = (EPI_OFF_T) 0, estimatedDiskUsageMb;
	FREADX frx;
	ulong off, len;
	size_t	n;
	ft_blobi	*blobi = NULL;

	if (ind->token == 1)
	{
		if (TXindexchunk == 0)
		{
			TXdiskSpace	diskSpace;
			EPI_OFF_T freemegs;

			TXgetDiskSpace(indfile, &diskSpace);
			if (diskSpace.availableBytes != (EPI_HUGEINT)(-1))
			{				/* success */
				if (diskSpace.availableBytes < 0)/* fs full */
					freemegs = 0;
				freemegs =
				  (EPI_OFF_T)(diskSpace.availableBytes >> 20);
			}
			else
				freemegs = (EPI_OFF_T)(-1);
			if (freemegs < (EPI_OFF_T)0)	/* unknown */
			{
				putmsg(MWARN, Fn,
				       "Unable to determine free disk space: Will proceed assuming there is enough");
				freemegs = (EPI_OFF_T)0;
			}
			else if (ind->itype == INDEX_3DB &&
				 freemegs < (ind->tblsize / 419430))
			{
				putmsg(MWARN, Fn,
				       "Free disk space low (%wkdMB): The process may take longer as it conserves space",
				       (EPI_HUGEINT)(diskSpace.availableBytes >> 20));
			}
			else if ((ind->itype == INDEX_MM ||
				  ind->itype == INDEX_FULL) &&
				 freemegs < (estimatedDiskUsageMb =
				 wtix_estdiskusage((WTIX *) ind->indexobj,
						   ind->tblsize)))
			{
				putmsg(MWARN + FWE, Fn,
"Free disk space low: %wkdMB free but up to %wkdMB or more may be needed",
					(EPI_HUGEINT)freemegs,
					(EPI_HUGEINT)estimatedDiskUsageMb);
			}
		}
	}

/* To handle phased indexing - need to hold open _T file, and what limit
is reached add to _T file (See put3dbi() for format), instead of adding
to ttbl.  Also need to do locking. */

	switch (ind->fldtype)
	{
	case FTN_INDIRECT:
		errno = 0;
		if (*(char *)v == '\0')
			goto saferr;
		frx.fh = fopen(v, "rb");
		if (frx.fh == (FILE *) NULL)
		{
			if (*(char *) v != '\0')	/* shut up if no file */
				putmsg(MWARN + FOE, Fn,
				       "Can't open indirect file %s: %s",
				       v, strerror(errno));
			/* this is semi-ok since user may set the correct
			 * file later:
			 */
			goto saferr;
		}
		frx.buf = ind->buf;
		if (!frx.buf)
		{
			frx.buf = (byte *) TXcalloc(TXPMBUFPN, Fn, 1,
						    ind->blockmax);
			if (frx.buf == (byte *) NULL)
			{
				fclose(frx.fh);
				frx.fh = NULL;
				goto err;
			}
			ind->buf = frx.buf;
		}
		frx.end = frx.buf;	/* JMT 1999-07-29 */
		frx.tailsz = 0;
		frx.len = ind->blockmax;
		frx.ex = ind->ex;
		off = 0;
		len = filereadex(&frx);
		while (len > 0)
		{
			if (!wtix_insert((WTIX *) ind->indexobj, frx.buf, len,
					 extra, at))
				goto err;
			token++;
			off += len;
			len = filereadex(&frx);
		}
		fclose(frx.fh);
		ind->token = token;
		break;
	case FTN_STRLST:
		/* Do not index the ft_strlst header, just the data,
		 * which is nul-terminated strings regardless of delimiter:
		 * KNG 20080319
		 */
		n = TX_STRLST_MINSZ;		/* skip the header */
		if (n > sz) n = sz;		/* but only if room */
		v = (byte *)v + n;
		sz -= n;
		goto indexChar;
	case FTN_BLOBI:
		v = TXblobiGetPayload(blobi = (ft_blobi *)v, &sz);
		/* fall through: */
	case FTN_CHAR:
	case FTN_BYTE:
	indexChar:
#ifndef NO_EMPTY_LIKEIN
		if (ind->ct != BTREEPN)
		{
			nterms = (EPI_OFF_T)TXinsertMetamorphCounterIndexRow(v,
					    extra, at, (WTIX *)ind->indexobj);
			if (nterms < (EPI_OFF_T)0)	/* Bug 6243 */
				goto err;
		}
		else
#endif
			if (!wtix_insert((WTIX *) ind->indexobj, v, sz, extra,
					 at))
				goto err;
		if (ind->ct != BTREEPN)	/* add per-recid word count */
		{
			RECID termcount;

#ifndef NO_EMPTY_LIKEIN
			TXsetrecid(&termcount, nterms);
#else
			TXsetrecid(&termcount, TXcountterms((char *) v));
#endif
			btinsert(ind->ct, &termcount, sizeof(at), &at);
		}
		if (blobi) TXblobiFreeMem(blobi);/* save some mem */
		ind->token++;
		break;
	default:
		/* Convert to varchar: */
		if (ind->fldOp == FLDOPPN &&
		    (ind->fldOp = dbgetfo()) == FLDOPPN)
		{
			putmsg(MERR + MAE, Fn, "Cannot open FLDOP");
			goto err;
		}
		if (ind->cnvFld == FLDPN &&
		    (ind->cnvFld = createfld("varchar", 1, 0)) == FLDPN)
		{
			putmsg(MERR + MAE, Fn, "Cannot open FLD");
			goto err;
		}
		putfld(ind->cnvFld, "", 0);
		if (fopush(ind->fldOp, vFld) != 0 ||
		    fopush(ind->fldOp, ind->cnvFld) != 0 ||
		    foop(ind->fldOp, FOP_CNV) != 0 ||
		    (resFld = fopop(ind->fldOp)) == FLDPN)
		{
			putmsg(MERR, Fn,
    "Cannot convert index field type %s to varchar for Metamorph index %s",
				ddfttypename(ind->fldtype), ind->path);
			goto err;
		}
		v = getfld(resFld, &sz);
		goto indexChar;
	}
	ret = 0;
	goto done;

saferr:
	ret = -1;
	goto done;
err:
	ret = -2;
done:
	if (resFld != FLDPN) closefld(resFld);
	return(ret);
}

/******************************************************************/
/*      Insert an item into an inverted btree index.
 *      Returns 0 on success, -1 on "safe" error, -2 on major error.
 */

static int dovind ARGS((TXMKIND *, void *, size_t, BTLOC));

static int
dovind(ind, v, sz, at)
TXMKIND *ind;			/* The index to insert into */
void *v;			/* Item to insert */
size_t sz;			/* Size of the item */
BTLOC at;			/* The location of the item */
{
	BTLOC data;

#ifndef NO_KEEP_STATS
	ind->indexstats.totentries++;
	ind->indexstats.newentries++;
	TXadddatasize(&ind->indexstats.indexeddata, sz);
#endif
	tx_invdata2loc(&data, v, ind->fldtype, (ind->collSeq == COLL_DESC));
	return (btinsert(ind->bt, &data, sizeof(at), &at) < 0 ? -2 : 0);
}

static void create3orfind_abendcb ARGS((void *usr));
static void
create3orfind_abendcb(usr)
void    *usr;
/* ABEND callback for INDEX_MM/INDEX_FULL index: deletes files.
 * NOTE: called during segfault, etc. so nothing fancy.
 */
{
	TXMKIND	*index = (TXMKIND *)usr;
	int	fh;
	char	*d;

	if (index->ct != BTREEPN)			/* _C.btr (counter) */
	{
		fh = getdbffh(index->ct->dbf);
		if (fh > STDERR_FILENO) close(fh);	/* for Windows, NFS */
		unlink(getdbffn(index->ct->dbf));
	}
	if (index->path != CHARPN)
	{
		d = index->path + strlen(index->path);
		/* These files aren't open for (most) of the create process;
		 * mostly OK to unlink without closing a file handle:
		 */
		strcpy(d, "_D.btr");			/* Ok; has space */
		unlink(index->path);
		strcpy(d, "_T.btr");
		unlink(index->path);
		strcpy(d, "_P.tbl");
		unlink(index->path);
		*d = '\0';
	}
}

static int TXindexFinishAndGetInfo ARGS((TXMKIND *ind));
static int
TXindexFinishAndGetInfo(ind)
TXMKIND	*ind;
/* Calls wtix_finish() and gets Total RowCount etc.
 * Returns 0 on error.
 */
{
	WTIX	*wx = (WTIX *)ind->indexobj;

	if (ind->didFinish)
		return(ind->finishCode);

	ind->didFinish = 1;
	if (ind->itype != INDEX_MM && ind->itype != INDEX_FULL)
		return((ind->finishCode = 0));	/* wrong type */
	ind->finishCode = wtix_finish(wx);
	TXwtixGetTotalHits(wx, &ind->paramTblInfo);
	return(ind->finishCode);
}

/******************************************************************/
/*      close3orfind - Close a 3db or full-metamorph-inversion index */

static TXMKIND *close3orfind ARGS((TXMKIND *ind));

static TXMKIND *
close3orfind(ind)
TXMKIND *ind;			/* The index to close */
{
	BTLOC at;
	WTIX *wx;

	if (!ind)
	{
		goto done;
	}

	if (ind->fldOp != FLDOPPN)
	{
		foclose(ind->fldOp);
		ind->fldOp = FLDOPPN;
	}
	if (ind->cnvFld != FLDPN)
	{
		closefld(ind->cnvFld);
		ind->cnvFld = FLDPN;
	}
	if ((wx = (WTIX *) ind->indexobj) != WTIXPN)
	{
		wx = closewtix(wx);
		ind->indexobj = NULL;
	}
	TXdelabendcb(create3orfind_abendcb, ind);	/* before closing */
	if (ind->bt)
		closebtree(ind->bt);
	if (ind->ct)
	{
		closebtree(ind->ct);
		TXsetrecid(&at, (EPI_OFF_T)-1);
		/* NULL `query' and `ix' closes static MMAPI and APICP: */
		TXinsertMetamorphCounterIndexRow(NULL, NULL, at, NULL);
	}
	if (ind->fc != (FLDCMP *) NULL)
		ind->fc = TXclosefldcmp(ind->fc);
	if (ind->tb2 != (TBL *) NULL)
		closetbl(ind->tb2);
	if (ind->expressions)
		_freelst(ind->expressions);
        ind->locale = TXfree(ind->locale);
	if (ind->ex)
		closerex(ind->ex);
	ind->buf = TXfree(ind->buf);
	if (ind->a2i)
		ind->a2i = TXadd2indcleanup(ind->a2i);
	ind->path = TXfree(ind->path);
	ind->indexName = TXfree(ind->indexName);
	ind = TXfree(ind);

      done:
	return NULL;
}

static void createborvind_abendcb ARGS((void *usr));
static void
createborvind_abendcb(usr)
void	*usr;
/* ABEND callback for creating B-tree or inverted index: deletes file.
 * NOTE: called during segfault, etc. so nothing fancy.
 */
{
	TXMKIND	*index = (TXMKIND *)usr;
	int	fh, i;
	BTREE	*bt[2];

	bt[0] = index->bt;				/* RAM B-tree? */
	bt[1] = index->ct;				/* file B-tree */
	for (i = 0; i < 2; i++)
	{
		if (bt[i] == BTREEPN) continue;
		fh = getdbffh(bt[i]->dbf);
		if (fh > STDERR_FILENO) close(fh);	/* for Windows, NFS */
		if ((void *)bt[i]->dbf->close != (void *)closerdbf)
			unlink(getdbffn(bt[i]->dbf));
	}
}

/******************************************************************/
/*      Close a regular BTREE index                               */

static TXMKIND *TXmkindCloseBtree ARGS((TXMKIND *ind));
static TXMKIND *
TXmkindCloseBtree(ind)
TXMKIND *ind;			/* The index to close */
{
	if (!ind)
		return ind;
	TXdelabendcb(createborvind_abendcb, ind);	/* before closing */
#ifndef NO_USE_MEM_INDEX
	if (ind->ct)
	{
		ind->ct->usr = NULL;		/* points to `ind->fc' */
		ind->ct = closebtree(ind->ct);
	}
#endif
	if (ind->bt)
	{
		ind->bt->usr = NULL;		/* points to `ind->fc' */
		ind->bt = closebtree(ind->bt);
	}
	if (ind->fc != (FLDCMP *) NULL)
		ind->fc = TXclosefldcmp(ind->fc);
	if (ind->tb2 != (TBL *) NULL)
		closetbl(ind->tb2);
	if (ind->meter != METERPN)
	{
		meter_end(ind->meter);
		ind->meter = closemeter(ind->meter);
	}
	ind->path = TXfree(ind->path);
	ind->indexName = TXfree(ind->indexName);
	ind = TXfree(ind);
	return NULL;
}

/******************************************************************/
/*      Close an inverted BTREE index                               */

static TXMKIND *closevind ARGS((TXMKIND *));

static TXMKIND *
closevind(ind)
TXMKIND *ind;			/* The index to close */
{
	if (!ind)
		return ind;
	TXdelabendcb(createborvind_abendcb, ind);	/* before close */
	if (ind->bt)
		closebtree(ind->bt);
	if (ind->fc != (FLDCMP *) NULL)
		ind->fc = TXclosefldcmp(ind->fc);
	if (ind->tb2 != (TBL *) NULL)
		closetbl(ind->tb2);
	if (ind->meter != METERPN)
	{
		meter_end(ind->meter);
		ind->meter = closemeter(ind->meter);
	}
	ind->path = TXfree(ind->path);
	ind->indexName = TXfree(ind->indexName);
	ind = TXfree(ind);
	return NULL;
}

/******************************************************************/
/*	Create a DD struct from a field list in the following
 *	format
 *	,,,field1 field2 field3 field 4
 *	I.e. n-1 commas, followed by field names.  Field name will
 *	end in a trailine '-' if it is to be descending.
 */

DD *
TXordspec2dd(dbtb, field, maxf, skip, novar, indexValues, collSeq)
DBTBL *dbtb;			/* Table the fields are in */
char *field;			/* The fields we want */
int maxf;			/* Maximum number of fields we are willing to accept */
int skip;			/* Skip this number of fields. */
int novar;			/* Devariable */
TXindexValues	indexValues;	/* whether to split multi-item fields */
char	*collSeq;		/* (out) COLL_ASC or COLL_DESC */
{
	DD *dd;
	char *fname, *tfield, *fp, *sp;
	int rev = 0, nfields;
	int seen = 0;

	if (collSeq) *collSeq = COLL_ASC;
	nfields = 1;
	for (tfield = field; *tfield && *tfield == ','; tfield++)
		nfields++;
	while (*tfield && *tfield == ' ')
		tfield++;
	if (nfields > maxf)
	{
		putmsg(MWARN + UGE, "CreateIndex",
		       "Too many fields specified");
		return NULL;
	}
	if (nfields == skip)
	{			/* No extra fields */
		return NULL;
	}
	if (nfields < skip)
	{			/* No extra fields, not even enough regular fields */
		putmsg(MWARN + UGE, "CreateIndex",
		       "Too few fields specified");
		return NULL;
	}
	dd = opennewdd(nfields - skip);
	if (!dd)
	{
		putmsg(MWARN, NULL, "Could not create index def");
		return NULL;
	}
	tfield = TXstrdup(TXPMBUFPN, __FUNCTION__, tfield);
	fp = tfield;
	while (tfield)
	{
		sp = strchr(tfield, ' ');
		if (sp)
			*sp = '\0';
		rev = 0;
#ifdef TX_USE_ORDERING_SPEC_NODE
		/* These field names are from/to SYSINDEX, not a PRED,
		 * so `^' `-' flags are still used instead of PRED.orderFlags:
		 */
#endif /* TX_USE_ORDERING_SPEC_NODE */
		if (tfield[strlen(tfield) - 1] == '^')
		{
			if (collSeq) *collSeq = COLL_DESC;
			rev |= OF_IGN_CASE;
			tfield[strlen(tfield) - 1] = '\0';
		}
		if (tfield[strlen(tfield) - 1] == '-')
		{
			if (collSeq) *collSeq = COLL_DESC;
			rev |= OF_DESCENDING;
			tfield[strlen(tfield) - 1] = '\0';
		}
		fname = dbnametoname(dbtb, tfield, FTNPN, INTPN);
		if (!fname)
		{
			putmsg(MERR + FOE, __FUNCTION__,
			       "Unable to find the field (%s)", tfield);
			fp = TXfree(fp);
			if (dd)
				closedd(dd);
			return NULL;
		}
		if (seen++ >= skip)
		{
			if (!copydd(dd, fname, dbtb->tbl, fname, novar))
			{
				putmsg(MERR + FOE, __FUNCTION__,
				       "Unable to copy the field (%s)",
				       fname);
				fp = TXfree(fp);
				if (dd)
					closedd(dd);
				return NULL;
			}
			if (rev)
				ddsetordern(dd, fname, rev);
		}
		if (sp)
			tfield = sp + 1;
		else
			tfield = sp;
	}
	if (dd->n == 1 && ((dd->fd[0].type & DDTYPEBITS) == FTN_STRLST) &&
	    indexValues == TXindexValues_SplitStrlst)
	{
		dd->fd[0].type = FTN_CHAR | DDVARBIT;
	}
	fp = TXfree(fp);
	return dd;
}

/******************************************************************/
/*      Open an index structure for a 3db or full-metamorph-inversion index.
 *      Returns a pointer to an index structure.
 */

static TXMKIND *open3orfind ARGS((int itype, DBTBL *dbtb, char *field,
      CONST char *indexName, char *indfile, int unique, int addcounts,
      TXindOpts *options));
static TXMKIND *
open3orfind(itype, dbtb, field, indexName, indfile, unique, addcounts,
	    options)
int itype;			/* INDEX_3DB, INDEX_MM, INDEX_FULL */
DBTBL *dbtb;			/* The table the field to be indexed is in. */
char *field;			/* The name of the field */
CONST char	*indexName;	/* name of index */
char *indfile;			/* The index file */
int unique;			/* Should this be a unique index */
int addcounts;			/* Add word counts for docs */
TXindOpts	*options;	/* (in) parameters/options */
{
	static CONST char Fn[] = "open3orfind";
	DD *dd2 = NULL;
	TXMKIND *ind;
	BTREE *bt2;
	int type;
	char buf[PATH_MAX];
	char *d, **noise;
	char *tf = NULL;
	size_t iflen;
	EPI_STAT_S sts;
	size_t sz2 = 0;
	A3DBI	*dbi = NULL;
	TXfdbiIndOpts	fdbiOptions;

	TXget_globalcp();
	if ((ind = (TXMKIND *) TXcalloc(TXPMBUFPN, Fn, 1, sizeof(TXMKIND))) == TXMKINDPN)
		goto err;
	ind->itype = itype;
	if (!(dbi = TX3dbiOpen(itype))) goto err;
	TX_INIT_TXMMPARAMTBLINFO(&ind->paramTblInfo);
	if ((iflen = strlen(indfile)) + 10 > sizeof(buf))
	{
		putmsg(MERR + MAE, Fn, "Filename too long");
		goto err;
	}
	if ((d = (char *)TXmalloc(TXPMBUFPN, Fn, iflen + 10)) != CHARPN)
	{
		strcpy(d, indfile);			/* for abend cb */
		ind->path = d;
	}
	else
		goto err;
	ind->isUnique = (byte)unique;
	ind->collSeq = COLL_ASC;
	ind->indexName = TXstrdup(TXPMBUFPN, Fn, indexName);
	if (!ind->indexName) goto err;
	TXaddabendcb(create3orfind_abendcb, ind);
	memcpy(buf, indfile, iflen);
	d = buf + iflen;

	if (EPI_FSTAT(getdbffh(dbtb->tbl->df), &sts) != 0)
		ind->tblsize = (EPI_OFF_T)0;		/* RAM dbf ? */
	else
		ind->tblsize = (EPI_OFF_T)sts.st_size;

	/* Set up aux (compound) fields.  First get aux schema: */
	dd2 = TXordspec2dd(dbtb, field, 50,
			   1,			/* skip the MM text field */
			   1,			/* strip types' DDVARBIT */
			   TXApp->indexValues,	/* WTF TXindexValues_All? */
			   &ind->collSeq);
	if (dd2)				/* has aux fields */
	{
		char *ttf;
		int i;
		TXA2IND *rc;

/* WTF - check for VAR fields, guesstimate size */
		(void)ddsettype(dd2, TEXIS_FAST_TABLE);
		for (i = 0; i < ddgetnfields(dd2); i++)
		{
			if (isddvar(dd2->fd[i].type))
				dd2->fd[i].type ^= DDVARBIT;
		}
		ind->tb2 = createtbl(dd2, NULL);
		rc = (TXA2IND *) TXcalloc(TXPMBUFPN, Fn, 1, sizeof(TXA2IND));
		if (!rc)
			goto err;
		rc->nfields = ddgetnfields(dd2);
		rc->fin = (FLD **) TXcalloc(TXPMBUFPN, Fn, rc->nfields,
						sizeof(FLD *));
		rc->fout = (FLD **) TXcalloc(TXPMBUFPN, Fn, rc->nfields,
						sizeof(FLD *));
		if (!rc->fin || !rc->fout)
			goto err;
		rc->tbl = ind->tb2;
		for (i = 0; i < rc->nfields; i++)
		{				/* for each aux field */
			char *fname;

			fname = ddgetname(dd2, i);
			rc->fin[i] = dbnametofld(dbtb, fname);
			if (!rc->fin[i])	/* should not happen */
			{
				putmsg(MERR, Fn,
				       "Cannot find field `%s' in table",
				       fname);
				goto err;
			}

			/* Bug 3285: Disallow strlst compound fields;
			 * almost certainly truncated, and usually cannot
			 * do anything even partially useful with prefix
			 * as we could for truncated varchars:
			 */
			switch (rc->fin[i]->type & DDTYPEBITS)
			{
			case FTN_STRLST:
			case FTN_COUNTERI:	/* should not happen */
			case FTN_INTERNAL:	/* "" */
				putmsg(MERR + UGE, Fn, "Cannot create Metamorph index %s with compound field `%s': Type %s not allowed in compound fields",
				       indfile, fname,
				       TXfldtypestr(rc->fin[i]));
				goto err;
			}

			rc->fout[i] = nametofld(rc->tbl, fname);
			sz2 += dd2->fd[i].size;
			if (dd2->fd[i].elsz == 1)
			{			/* eg. char/byte/strlst? */
				sz2++;		/* for nul */
				if (dbtb->
				    ddic->optimizations[OPTIMIZE_AUXDATALEN])
				{
				/* Store 1 more byte to ensure searches can
				 * differentiate values of exactly
				 * table-declared-size from values of more
				 * than table-declared-size, to avoid a
				 * post-proc for the former:
				 */
					dd2->fd[i].size++;
					rc->fout[i]->size++;
					sz2++;
				}
			}
			if (sz2 % TX_ALIGN_BYTES)
			{			/* size is not aligned */
				int d =

					TX_ALIGN_BYTES -
					(sz2 % TX_ALIGN_BYTES);
				if (dd2->fd[i].elsz == 1)
				{		/* eg. char/byte/strlst? */
					/* Might as well utilize the alignment
					 * padding to store more data:
					 */
					dd2->fd[i].size += d;
					rc->fout[i]->size += d;
				}
				/* else no use trying to use alignment padding
				 * (or part of) to store more data; probably
				 * no padding needed or it's less than elsz?
				 */
				sz2 += d;
			}
			if (isddvar(rc->fin[i]->type))
			{
				/* elsz == 1 fields have padding so we know
				 * if a table value is shorter than aux sz;
				 * elsz > 1 fields do not.  Also, for strlst,
				 * query size has no bearing on size of
				 * matching strlsts (unlike e.g. varchar), so
				 * for strlst we cannot report a query size
				 * threshold that requires post-processing.
				 * wtf we do not handle compound strlst
				 * queries correctly:
				 */
				type = (dd2->fd[i].type & DDTYPEBITS);
				if (dd2->fd[i].elsz == 1 &&
				    type != FTN_STRLST)
				{
					putmsg(MWARN, NULL,
"Variable size warning: Compound index field %s queries of %d or more %ss may require post-processing",
						fname,
						(int)dd2->fd[i].size/dd2->fd[i].elsz,
						ddfttypename(type));
				}
				else
					putmsg(MWARN, NULL,
"Variable size warning: Compound index field %s queries may require post-processing",
						fname);
			}
		}
		ind->a2i = rc;
		while (*field && (*field == ',' || *field == ' '))
			field++;
		ttf = tf = TXstrdup(TXPMBUFPN, Fn, field);
		field = tf;
		while (*ttf && *ttf != ' ')
			ttf++;
		*ttf = '\0';
	}
	ind->fl = nametofld(dbtb->tbl, field);
	if (ind->fl == FLDPN)
	{
		putmsg(MERR + FOE, Fn, "Unable to find field `%s'", field);
		goto err;
	}


	if (addcounts)
	{
		static CONST char * CONST counterindexexpressions[] = {
			"-?[\\alnum]{1,99}",
			"-?[^\\space\",\\-]{1,99}",
			"-?[\\alnum]{1,99}\\.=[\\alnum]{1,99}",
			"",
			NULL
		};

		strcpy(d, "_C");
		ind->ct = openbtree(buf, BTFSIZE, 20, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
		ind->expressions = _duplst((char **)counterindexexpressions);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
	}
	else
	{
		ind->expressions = _duplst(options->wordExpressions);
	}
	if (!ind->expressions) goto err;
	dbi->explist = _freelst(dbi->explist);
	if (!(dbi->explist = _duplst(ind->expressions))) goto err;

        if (*TXgetlocale() == '\0') TXsetlocale("");
        ind->locale = TXstrdup(TXPMBUFPN, Fn, TXgetlocale());
	dbi->locale = TXfree(dbi->locale);
	if (!(dbi->locale = TXstrdup(TXPMBUFPN, Fn, ind->locale))) goto err;

	ind->textSearchMode = dbi->textsearchmode = options->textsearchmode;

	if (options->keepnoise)
		noise = (char **) emptylist;
	else
		noise = (char **) options->noiselist;
	dbi->noiselist = TXfreeStrEmptyTermList(dbi->noiselist, -1);
	dbi->noiselist = TXdupStrEmptyTermList(TXPMBUFPN, Fn, noise, -1);
	if (!dbi->noiselist) goto err;

	dbi->version = ind->fdbiVersion = options->fdbiVersion;

	dbi->auxdd = dd2;
	dd2 = NULL;				/* `dbi' owns it now */
	dbi->auxsz = sz2;
	sz2 = 0;

	TXfdbiIndOpts_INIT_FROM_INDEX_OPTIONS(&fdbiOptions, options);

	ind->indexobj =
		openwtix(dbtb, field, indfile, dbi->auxsz, &fdbiOptions, NULL,
			 (itype == INDEX_FULL ? WTIXF_FULL : 0), 0, WTIXPN);
	if (ind->indexobj == NULL ||
	    !wtix_setnoiselist((WTIX *) ind->indexobj, dbi->noiselist))
		goto err;

	strcpy(d, "_P");
	if (!TXcreateAndWriteDbiParamsTable(buf, dbi))
		goto err;

	/* Table Size is added after we've indexed, in case the table
	 * size changes.   KNG 000417
	 */

/*      Create a table to store new recids in. */

	strcpy(d, "_T");
	if (dbi->auxdd)
	{
		bt2 = openbtree(buf, 8192, BTCSIZE, 0, O_RDWR | O_CREAT | O_EXCL);
		btreesetdd(bt2, dbi->auxdd);
	}
	else
		bt2 = openbtree(buf, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
	bt2 = closebtree(bt2);	/* We don't need this as we build */

/*      Create a table for deleted recids. */

	strcpy(d, "_D");
	bt2 = openbtree(buf, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
	/* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
	bt2 = closebtree(bt2);	/* We don't need this as we build */
	/* closedd(dd); */

	ind->tb = dbtb->tbl;
	ind->token = 1L;
	ind->fldtype = ind->fl->type & DDTYPEBITS;
	ind->blockmax = TXgetblockmax();
	ind->ex = openrex((byte *) "$", TXrexSyntax_Rex);
	ind->buf = (uchar *) TXcalloc(TXPMBUFPN, Fn, 1, ind->blockmax);
	goto done;

      err:
	ind = close3orfind(ind);
	TXdelindex(indfile, itype);
      done:
	tf = TXfree(tf);
	dd2 = closedd(dd2);
	dbi = close3dbi(dbi);
	return ind;
}

/******************************************************************/
/*      Open an index structure for a btree index.
 *      Returns a pointer to an index structure.
 */

TXMKIND *
TXmkindCreateBtree(dbtb, field, indexName, indfile, unique, isRebuild,
		   options)
DBTBL *dbtb;			/* The table the field to be indexed is in. */
char *field;			/* The name of the field */
CONST char	*indexName;	/* name of index */
char *indfile;			/* index file (sans extension) */
int unique;			/* Should this be a unique index */
int	isRebuild;		/* nonzero: rebuild not create */
TXindOpts	*options;	/* (in/out) settings/options */
{
	static CONST char Fn[] = "TXmkindCreateBtree";
	DD *dd = NULL;
	TXMKIND *ind;
	EPI_STAT_S	st;

	/* int  rev=0; */

	if (existsbtree(indfile))
	{
		putmsg(MWARN, Fn, "File already exists %s.btr", indfile);
		return NULL;
	}
	ind = (TXMKIND *) TXcalloc(TXPMBUFPN, Fn, 1, sizeof(TXMKIND));
	if (ind == (TXMKIND *) NULL)
		return NULL;
	ind->itype = INDEX_BTREE;
	ind->path = (char *)TXmalloc(TXPMBUFPN, Fn, strlen(indfile) + 10);
	if (!ind->path) return(TXmkindClose(ind));
	strcpy(ind->path, indfile);
	ind->indexName = TXstrdup(TXPMBUFPN, Fn, indexName);
	if (!ind->indexName) return(TXmkindClose(ind));
	TXaddabendcb(createborvind_abendcb, ind);
	ind->tb = dbtb->tbl;
	ind->isRebuild = (byte)isRebuild;
	ind->isUnique = (byte)unique;
	ind->collSeq = COLL_ASC;

	dd = TXordspec2dd(dbtb, field, 50, 0, 0, TXApp->indexValues,
			  &ind->collSeq);
	if (!dd) goto err;

#ifndef NO_USE_MEM_INDEX
	ind->memlimit = options->indexmem;
	ind->bt = openbtree(NULL, BT_MAXPGSZ, 20, unique, O_RDWR | O_CREAT | O_EXCL);
	if (!ind->bt)
	{
		putmsg(MERR, NULL, CantCreateIndex, indfile, BTREE_ERR());
		goto err;
	}
	ind->bt->params = options->btparams;
	/* btparamtotext() called later, at end of index build */
	ind->ct = openbtree(indfile, BT_MAXPGSZ, 20, unique | BT_LINEAR, O_RDWR | O_CREAT | O_EXCL);
	if (!ind->ct)
	{
		putmsg(MERR, NULL, CantCreateIndex, indfile, BTREE_ERR());
		goto err;
	}
	ind->ct->params = options->btparams;
	/* btparamtotext() called later, at end of index build */
	btreesetdd(ind->bt, dd);
	btreesetdd(ind->ct, dd);
	btsetcmp(ind->bt, (btcmptype) fldcmp);
	btsetcmp(ind->ct, (btcmptype) fldcmp);

	ind->fc = TXopenfldcmp(ind->bt /* dd */, TXOPENFLDCMP_CREATE_FLDOP);
	ind->bt->usr = ind->fc;
	ind->ct->usr = ind->fc;

	/* See also params set below, for RAM index.  Note that we assume
	 * we can set the same params for both RAM and disk B-tree
	 * (for symmetry) and copy the data from the RAM to disk B-tree
	 * when index is done.  Used to only set params for disk B-tree;
	 * no difference for now, but maybe needed for future params?
	 * KNG 20080402
	 * KNG 20120702 params now copied from `options->btparams', above
	 */

	ind->tb2 = createtbl(dd, NULL);
	dd = closedd(dd);
#else /* NO_USE_MEM_INDEX */
	wtf propagate btparams;
	ind->bt = openbtree(indfile, BT_MAXPGSZ, 20, unique, O_RDWR | O_CREAT | O_EXCL);
	if (!ind->bt)
	{
		putmsg(MERR, NULL, CantCreateIndex, indfile, BTREE_ERR());
		goto err;
	}
	btreesetdd(ind->bt, dd);
	btsetcmp(ind->bt, (btcmptype) fldcmp);

	ind->fc = TXopenfldcmp(ind->bt /* dd */, TXOPENFLDCMP_CREATE_FLDOP);
	ind->bt->usr = ind->fc;

	ind->tb2 = createtbl(dd, NULL);
	dd = closedd(dd);
#endif /* NO_USE_MEM_INDEX */

#ifndef NO_KEEP_STATS
	ind->indexstats.totentries = 0;
	ind->indexstats.newentries = 0;
	ind->indexstats.delentries = 0;
	TXresetdatasize(&ind->indexstats.indexeddata);
#endif

	if (options->indexmeter != TXMDT_NONE)
	{
		errno = 0;
		if (EPI_FSTAT(getdbffh(ind->tb->df), &st) == 0)
			ind->metersize = (EPI_OFF_T)st.st_size;
		/* else RAM dbf */
		if (ind->metersize > (EPI_OFF_T)0)
			ind->meter = openmeter("Indexing data:",
				options->indexmeter,
				MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
				(EPI_HUGEINT)ind->metersize);
	}
	goto done;

err:
	if (dd) dd = closedd(dd);
	ind = TXmkindClose(ind);
done:
	return ind;
}

/******************************************************************/
/*      Open an index structure for an inverted btree index.
 *      Returns a pointer to an index structure.
 */

TXMKIND *
TXmkindCreateInverted(dbtb, field, indexName, indfile, unique, isRebuild,
		      options)
DBTBL *dbtb;			/* The table the field to be indexed is in. */
char *field;			/* The name of the field */
CONST char	*indexName;
char *indfile;			/* The index file */
int unique;			/* Should this be a unique index */
int	isRebuild;		/* nonzero: rebuild not create */
TXindOpts	*options;	/* (in) parameters to use */
{
	static CONST char Fn[] = "TXmkindCreateInverted";
	TXMKIND *ind;
	char *fname;
	char *tfield;
	int rev = 0, tsz;
	FLD *fld;
	EPI_STAT_S	st;

	if (existsbtree(indfile))
	{
		putmsg(MWARN, Fn, "File already exists %s.btr", indfile);
		return NULL;
	}
	ind = (TXMKIND *) TXcalloc(TXPMBUFPN, Fn, 1, sizeof(TXMKIND));
	if (ind == (TXMKIND *) NULL)
		return NULL;
	ind->itype = INDEX_INV;
	ind->path = (char *)TXmalloc(TXPMBUFPN, Fn, strlen(indfile) + 10);
	if (!ind->path) return(closevind(ind));
	strcpy(ind->path, indfile);
	ind->isRebuild = (byte)isRebuild;
	ind->isUnique = (byte)unique;
	ind->collSeq = COLL_ASC;
	ind->indexName = TXstrdup(TXPMBUFPN, Fn, indexName);
	if (!ind->indexName) return(closevind(ind));
	TXaddabendcb(createborvind_abendcb, ind);
	ind->tb = dbtb->tbl;

	tfield = TXstrdup(TXPMBUFPN, Fn, field);
	if (tfield[strlen(tfield) - 1] == '-')
	{
		ind->collSeq = COLL_DESC;
		rev++;
		tfield[strlen(tfield) - 1] = '\0';
	}
	fname = dbnametoname(dbtb, tfield, FTNPN, INTPN);
	if (!fname)
	{
		putmsg(MERR + FOE, Fn, "Unable to find the tfield (%s)",
		       tfield);
		tfield = TXfree(tfield);
		return closevind(ind);
	}
	fld = dbnametofld(dbtb, fname);
	if (!fld)
	{
		putmsg(MERR + FOE, Fn, "Unable to find the tfield (%s)",
		       tfield);
		tfield = TXfree(tfield);
		return closevind(ind);
	}
	/* The value must be safely storable in an EPI_OFF_T.
	 * NOTE: see also tx_invdata2loc():
	 */
	switch (fld->type & FTN_VarBaseTypeMask)
	{
	case FTN_DWORD:
	case FTN_DATE:
	case FTN_WORD:
	case FTN_INT:
	case FTN_INTEGER:
	case FTN_LONG:
	case FTN_SHORT:
	case FTN_SMALLINT:
		tsz = (int)ddftsize(fld->type);
		break;
	default:
		tsz = MAXINT;
		break;
	}
	if (tsz > EPI_OFF_T_BITS/8)
	{
		putmsg(MERR + UGE, Fn,
			"Field must be integral and %d bits or less",
			EPI_OFF_T_BITS);
		tfield = TXfree(tfield);
		return closevind(ind);
	}
	ind->bt = openbtree(indfile, BT_MAXPGSZ, 20, unique,
				(O_RDWR | O_CREAT | O_EXCL));

	if (ind->bt) ind->bt->params = options->btparams;
	/* btparamtotext() called later, at end of index build */

	ind->fl = nametofld(dbtb->tbl, tfield);
	if (ind->fl == FLDPN)
	{
		putmsg(MERR + FOE, Fn, "Unable to find field `%s'", field);
		return(closevind(ind));
	}
	ind->fldtype = (ind->fl->type & DDTYPEBITS);
	tfield = TXfree(tfield);

#ifndef NO_KEEP_STATS
	ind->indexstats.totentries = 0;
	ind->indexstats.newentries = 0;
	ind->indexstats.delentries = 0;
	TXresetdatasize(&ind->indexstats.indexeddata);
#endif
	if (options->indexmeter != TXMDT_NONE)
	{
		errno = 0;
		if (EPI_FSTAT(getdbffh(ind->tb->df), &st) == 0)
			ind->metersize = (EPI_OFF_T)st.st_size;
		/* else RAM dbf */
		if (ind->metersize > (EPI_OFF_T)0)
			ind->meter = openmeter("Indexing data:",
				options->indexmeter,
				MDOUTFUNCPN, MDFLUSHFUNCPN, NULL,
				(EPI_HUGEINT)ind->metersize);
	}
	return ind;
}

TXMKIND *
TXmkindClose(ind)
TXMKIND	*ind;
/* Closes `ind' regardless of type.
 */
{
	if (!ind) return(NULL);
	switch (ind->itype)
	{
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		return(close3orfind(ind));
	case INDEX_BTREE:
		return(TXmkindCloseBtree(ind));
	case INDEX_INV:
		return(closevind(ind));
	default:
		putmsg(MERR + UGE, "TXmkindClose", "Unknown type #%d",
		       (int)ind->itype);
		return(NULL);
	}
}

int
TXmkindBuildIndex(ddic, dbtbl, ind, numTblRdLocks)
DDIC    *ddic;
DBTBL   *dbtbl;         /* (in) table that index is on */
TXMKIND *ind;           /* (in/out) index container object */
int     *numTblRdLocks; /* (in/out) current number of `dbtbl' read locks */
/* Does actual work of building index `ind' by inserting table rows etc.
 * May unlock `dbtbl' (and update `*numTblRdLocks').
 * Returns -1 on error, 0 on partial success (some bad rows), 1 on
 * complete success.
 */
{
  static CONST char     fn[] = "TXmkindBuildIndex";
  static CONST char     couldNotUnsetReadBuf[] =
    "Could not unset readbufsz ioctl on table after %s index %s";
  CONST char		*action;
  size_t                sz = 0, sz2;
  BINDEX                bindex;
  TXA2IND               *a2i = NULL;
  int                   rc, tablereadbufset = 0, reduceVirtualFldMem = 0;
  RECID                 *at, recid;
  EPI_HUGEINT           rows = 0, memrows = 0, memcnt, badrows = 0;
  DATASIZE              tbldata;
  void                  *v = NULL, *auxData;
  METER                 *meter = NULL;
  byte                  *buf;
  int                   gaveNonUniqueDataWarning = 0, success;
  EPI_STAT_S            st;

  memset(&bindex, 0, sizeof(BINDEX));
  TXresetdatasize(&tbldata);
  action = (ind->isRebuild ? "rebuilding" : "creating");

  /* Read-lock table, possibly for the duration of the index build: */
  if (*numTblRdLocks == 0)
    {
      if (TXlocktable(dbtbl, R_LCK) != 0) goto err;
      (*numTblRdLocks)++;
    }

  /* Metamorph index allows table mods during index create, so unlock
   * the table (unless indexbatchbuild).  Other index types unlocked
   * at end:
   */
  switch (ind->itype)
    {
    case INDEX_3DB:
    case INDEX_MM:
    case INDEX_FULL:
      if (!ddic->optimizations[OPTIMIZE_INDEX_BATCH_BUILD])
        {
          TXunlocktable(dbtbl, R_LCK);
          (*numTblRdLocks)--;
        }
      break;
    }

  if (ind->itype == INDEX_BTREE)
    {
      bindex.btree = ind->bt;
      bindex.table = createtbl(btreegetdd(bindex.btree), NULL);
      a2i = TXadd2indsetup(dbtbl, &bindex);
    }

  if (!rewindtbl(ind->tb))
    {
      rc = 0;
      goto closeit;
    }
  /* KNG 20070424 If table is read-locked for the duration -- either
   * because index type demands it (non-Metamorph index) or requested
   * by indexbatchbuild -- then we can read-buffer the table, reducing
   * reads, lseeks and semops.  Downside is no mods allowed on table
   * during build, so indexbatchbuild is off by default.  No error
   * report if ioctl fails, because DBF may not be KDBF.  Bug 1744:
   */
  if (*numTblRdLocks > 0 &&
      TXtableReadBufSz > 0 &&
      ioctldbf(dbtbl->tbl->df, (DBF_KAI | KDBF_IOCTL_READBUFSZ),
               (void *)TXtableReadBufSz) == 0)
    tablereadbufset = 1;

  /* indexvirtualfields optimization: if we are indexing a virtual
   * field, we can free the underlying DBF buffer once the virtual
   * field is established; this can save mem on large rows:
   */
  reduceVirtualFldMem =
    (ddic->optimizations[OPTIMIZE_INDEX_VIRTUAL_FIELDS] &&
     ind->fl &&
     FLD_IS_COMPUTED(ind->fl) &&
     /* index types we know use `v'/`ind->fl' safely: */
     (ind->itype == INDEX_MM ||
      ind->itype == INDEX_FULL));

  for ( ; at = getdbtblrow(dbtbl), TXrecidvalid(at); rows++, memrows++)
    {
#ifndef NO_KEEP_STATS
      TXadddatasize(&tbldata, dbtbl->tbl->irecsz);
#endif
      if (ind->fl)
        v = getfld(ind->fl, &sz);
      else
        {
          v = NULL;
          sz = 0;
        }

      switch (ind->itype)
        {
        case INDEX_BTREE:
#ifndef NO_USE_MEM_INDEX
          if ((size_t)ioctldbf(ind->bt->dbf, RDBF_SIZE, 0) > ind->memlimit)
            {
              btflush(ind->ct);
              rewindbtree(ind->bt);
              if (ind->meter != METERPN)
                meter = opensecondmeter(ind->meter, "Merging to index file:",
                                        memrows);
              for (memcnt = 0; ; memcnt++)
                {
                  sz = BT_REALMAXPGSZ;
                  recid = btgetnext(ind->bt, &sz, NULL, &buf);
                  if (!TXrecidvalid(&recid)) break;
                  btinsert(ind->ct, &recid, sz, buf);
                  if (meter) METER_UPDATEDONE(meter, memcnt);
                }
              if (meter)
                {
                  meter_updatedone(meter, memrows);
                  meter_end(meter);
                  meter = closemeter(meter);
                }
              memrows = 0;
              {
                BTREE   *bt;
                /* Clear `ind->bt' before we close it, in case ABEND
                 * happens during close, so ABEND callback does not
                 * access freed/flummoxed BTREE struct:
                 */
                bt = ind->bt;
                ind->bt = BTREEPN;
		bt->usr = NULL;			/* points to `ind->fc' */
                bt = closebtree(bt);
              }
              ind->bt = openbtree(NULL, BT_MAXPGSZ, 20, ind->isUnique,
                                  (O_RDWR | O_CREAT | O_EXCL));
              if (!ind->bt) goto err;
              /* KNG 20100706 Bug 3210: Propagate stringcomparemode: */
	      ind->bt->params = ind->ct->params;
              bindex.btree = ind->bt;
              btreesetdd(ind->bt, btreegetdd(ind->ct));
              btsetcmp(ind->bt, (btcmptype)fldcmp);
              ind->bt->usr = ind->fc;
              ind->output++;
            }
#endif /* !NO_USE_MEM_INDEX */
          if ((rc = TXadd2ind(a2i, at)) == -1)
            {
              if (!gaveNonUniqueDataWarning && (ind->bt->flags & BT_UNIQUE))
                {
                  putmsg(MWARN, CHARPN,
                         "Creating Unique index on Non-unique data");
                  gaveNonUniqueDataWarning++;
                }
            }
          break;
        case INDEX_3DB:
        case INDEX_MM:
        case INDEX_FULL:
          if (ind->a2i)
            {
              sz2 = TXa2i_setbuf(ind->a2i);
              if (sz2 != ind->auxDataSz)
                {
                  putmsg(MERR, fn,
                         "Wrong aux size from TXa2i_setbuf() for recid 0x%wx while %s index %s: Got %d bytes, expected %d",
                         (EPI_HUGEINT)TXgetoff(at), action, ind->indexName,
			 (int)sz2, (int)ind->auxDataSz);
                  /* We must abandon index, as unlike during update,
                   * we do not have an alternate aux data source:
                   */
                  rc = -2;
                  break;
                }
              auxData = ind->a2i->tbl->orec;
            }
          else
            auxData = NULL;

          /* Free up some mem if possible, now that we are done with
           * raw KDBF data from current `dbtbl' row: `ind->fl' is
           * virtualized, *and* aux data has been fldtobuf'd by
           * TXa2i_setbuf() (Bug 4156).  Only do this on large rows,
           * to avoid constant KDBF re-alloc:
           */
          if (reduceVirtualFldMem &&
              /* Check actual DBF row size, not field size: latter may
               * be large when former is small (e.g. indirects):
               */
              TXdbtblGetRowSize(dbtbl) > TX_INDEX_MAX_SAVE_BUF_LEN)
            TXdbtblReleaseRow(dbtbl);

          rc = do3orfind(ind, ind->fl, v, sz, *at, ind->path, auxData);
          break;
        case INDEX_INV:
          rc = dovind(ind, v, sz, *at);
          break;
        default:                                /* never gets here */
          rc = -2;
          break;
        }

      /* Done with current `ind->fl'/`v' value; free ASAP to maybe
       * save mem at next row read:
       */
      if (reduceVirtualFldMem && sz > TX_INDEX_MAX_SAVE_BUF_LEN)
        {
          setfldandsize(ind->fl, NULL, 0, FLD_KEEP_KIND);
          v = NULL;
          sz = 0;
        }

      if (ind->meter)
        METER_UPDATEDONE(ind->meter, (EPI_HUGEINT)TXgetoff2(at));
      switch (rc)
        {
        case -1:                                /* "safe" error */
          badrows++;
          break;
        case -2:                                /* major error */
          putmsg(MERR, CHARPN, "Abandoning index %s due to errors while %s",
                 ind->indexName, action);
          rc = 0;
          goto closeit;
        }
    }                                           /* end for each row */
  rc = 1;

closeit:
  /* Metamorph index allows table mods during index create, but may
   * still have locked for indexbatchbuild; other index types locked
   * the table regardless.  It it safe to unlock for Metamorph here;
   * other types wait until later.  We do this ASAP after we are done
   * with the table, to release resources (note that we may not be
   * done with final merge):
   */
  switch (ind->itype)
    {
    case INDEX_3DB:
    case INDEX_MM:
    case INDEX_FULL:
      if (tablereadbufset)
        {
          if (ioctldbf(dbtbl->tbl->df,
                       (DBF_KAI | KDBF_IOCTL_READBUFSZ), (void *)0) != 0)
            putmsg(MERR, fn, couldNotUnsetReadBuf, action, ind->indexName);
          tablereadbufset = 0;
        }
      if (*numTblRdLocks > 0)
        {
          TXunlocktable(dbtbl, R_LCK);
          (*numTblRdLocks)--;
        }
      break;
    }

#if !defined(NO_KEEP_STATS)
  if (rc)
    {
      char              *t;
      INDEXSTATS        *idxStats = &ind->indexstats;

      switch (ind->itype)
        {
        case INDEX_3DB:
        case INDEX_MM:
        case INDEX_FULL:
          wtix_getstats(ind->indexobj, idxStats);
          TXsetstatistic(ddic, dbtbl->lname, "NROWS", rows, "", 0);
          TXsetstatistic(ddic, dbtbl->lname, "ROWG", tbldata.gig, "", 0);
          TXsetstatistic(ddic, dbtbl->lname, "ROWB", tbldata.bytes, "", 0);
          t = TXstrcat3(dbtbl->lname, ".", ind->indexName);
	  if (t)
            {
              TXsetstatistic(ddic, t, "NROWS", rows, "", 0);
              TXsetstatistic(ddic, t, "DATAG", idxStats->indexeddata.gig,
			     "", 0);
              TXsetstatistic(ddic, t, "DATAB", idxStats->indexeddata.bytes,
			     "", 0);
              t = TXfree(t);
            }
          TXsetPerTableLimitStats(dbtbl);       /* Bug 4730 */
        }
    }
#endif /* !NO_KEEP_STATS */

  success = (rc ? 1 : 0);
  switch (ind->itype)
    {
    case INDEX_3DB:
    case INDEX_MM:
    case INDEX_FULL:
      /* Update the param table to reflect current table size (Table
       * Size), for VSH7 lookahead: KNG 000417 Also finish index now
       * (instead of at close3orfind()) so we get Total RowCount,
       * Total OccurrenceCount: KNG 20100806
       */
      if (success) success = TXindexFinishAndGetInfo(ind);
      TXseterror(0);
      if (EPI_FSTAT(getdbffh(dbtbl->tbl->df), &st) == 0)
        {
          int   upret = 1;
          char  *s;

          s = ind->path + strlen(ind->path);
          strcpy(s, "_P");
          if (TXlockindex(dbtbl, INDEX_WRITE, NULL) == 0)
            {
              ind->paramTblInfo.originalTableSize = (EPI_OFF_T)st.st_size;
              upret = tx_updateparamtbl(ind->path, ind->itype,
					&ind->paramTblInfo, ind->fdbiVersion);
              TXunlockindex(dbtbl, INDEX_WRITE, NULL);
            }
          *s = '\0';
          if (!upret) success = 0;		/* do not wtix_finish() */
        }
      /* else RAM dbf */
      break;
    case INDEX_BTREE:
      bindex.table = closetbl(bindex.table);
      a2i = TXadd2indcleanup(a2i);
      if (ind->meter)
        {
          meter_updatedone(ind->meter, (EPI_HUGEINT)ind->metersize);
          meter_end(ind->meter);                /* leave open for 2nd */
        }
#ifndef NO_USE_MEM_INDEX
      rewindbtree(ind->bt);
      if (memrows == (EPI_HUGEINT)0) memrows = 1;       /* for meter */
      if (ind->meter)
        meter = opensecondmeter(ind->meter, "Final merge to index:", memrows);
      for (memcnt = 0; ; memcnt++)
        {
          sz = BT_REALMAXPGSZ;
          recid = btgetnext(ind->bt, &sz, NULL, &buf);
          if (!TXrecidvalid(&recid)) break;
          btinsert(ind->ct, &recid, sz, buf);
          if (meter) METER_UPDATEDONE(meter, memcnt);
        }
      if (!ind->output) btflush(ind->ct);
      if (meter)
        {
          meter_updatedone(meter, memrows);
          meter_end(meter);
          meter = closemeter(meter);
        }
#endif /* !NO_USE_MEM_INDEX */
      break;
    case INDEX_INV:
      if (ind->meter)
        meter_updatedone(ind->meter, (EPI_HUGEINT)ind->metersize);
      break;
    }
  goto done;

err:
  success = 0;
done:
  if (meter)
    {
      meter_end(meter);
      meter = closemeter(meter);
    }
  if (tablereadbufset)
    {
      /* Before freeing KDBF read buf, clear any `tbl' fields that
       * might point into it (via buftofld()):
       */
      TXtblReleaseFlds(dbtbl->tbl);

      if (ioctldbf(dbtbl->tbl->df,
                   (DBF_KAI | KDBF_IOCTL_READBUFSZ), (void *)0) != 0)
        putmsg(MERR, fn, couldNotUnsetReadBuf, action, ind->indexName);
      tablereadbufset = 0;
    }
  if (a2i) a2i = TXadd2indcleanup(a2i);
  return(success ? (badrows == (EPI_HUGEINT)0 ? 1 : 0) : -1);
}

/* ------------------------------------------------------------------------ */

const char *
TXgetIndexTypeDescription(int indexType)
/* Returns human-readable index type description for `indexType',
 * even if unknown.
 */
{
	switch (indexType)
	{
	case INDEX_3CR:
	case INDEX_3DB:
	case INDEX_MMCR:
	case INDEX_MM:		return("Metamorph");
	case INDEX_FULLCR:
	case INDEX_FULL:	return("Metamorph inverted");
	case INDEX_3DB2:	return("Metamorph counter");
	case INDEX_BTREE:	return("B-tree");
	case INDEX_INV:		return("inverted");
	case INDEX_TEMP:	return("temporary");
	case INDEX_DEL:		return("deleted");
	default:		return("unknown-type");
	}
}

/******************************************************************/
/*	WTF - needs to do some error checking.  Appears that very
 *	little will prevent it from returning success.  In particular
 *	needs to make sure that a full disk won't puke it.
 *      Returns -1 on severe error, 0 on partial success (some rows
 *      failed but index created), 1 if "complete" success.
 */

int
createindex(ddic, idxfile, indname, table, field, unique, itype, options)
DDIC *ddic;
char *idxfile;	/* (in) [/path/]filename (sans extension) */
char *indname;	/* (in) index name */
char *table;	/* (in) table name */
char *field;	/* (in) SYSINDEX.FIELDS value */
int unique;	/* (in) nonzero if unique index */
int itype;	/* (in) INDEX_... type */
TXindOpts	*options;	/* (in/out) options, opt. w/`WITH ...' */
{
	static CONST char Fn[] = "createindex";
	char *indexTypes = NULL, **indexFiles = NULL, **indexTables = NULL;
	char	*indexNonUnique = NULL, *sysindexFname;
	char	**indexFields = NULL, **sysindexParamsVals = CHARPPN;
	int	uberlock = 0, res, gotPartialErr = 0;
	A3DBI	fakeDbi;
	char indfile[PATH_MAX];
	char sysindexParams[TX_MAX(BTPARAMTEXTSIZE, MM_INDEX_PARAMS_TEXT_MAXSZ)];
	int numIndexes = 0, rc, indexCreateType, intsz;
	size_t dbLen;
	DBTBL *dbtb = (DBTBL *) NULL;
	TXMKIND *ind = NULL;
	int success = 1;

	memset(&fakeDbi, 0, sizeof(A3DBI));

	if (itype == INDEX_3DB)
	{
		putmsg(MERR + UGE, Fn,
   "Index type `%c' is deprecated; create %c or %c index (Metamorph) instead",
		       (int)itype, (int)INDEX_FULL, (int)INDEX_MM);
		goto err;
	}

	if (strlen(indname) > DDNAMESZ)
	{
		putmsg(MWARN, Fn,
		    "Index name `%s' too long: cannot exceed %d characters",
			indname, (int)DDNAMESZ);
		goto err;
	}
	if (!ddic)
	{
		putmsg(MERR + UGE, Fn, "Internal error: NULL DDIC");
		goto err;
	}

	TXclosecacheindex(ddic, table);
	TXdocleanup(ddic);
	numIndexes = ddgetindexbyname(ddic, indname, &indexTypes,
				      &indexNonUnique, &indexFiles,
				      &indexTables, &indexFields,
				      &sysindexParamsVals);
	if (numIndexes > 0)			/* index already exists */
	{
		int i;

		rc = -1;			/* error */
		for (i = 0; i < numIndexes; i++)
		{
			switch (indexTypes[i])
			{
			case INDEX_3DB:
			case INDEX_FULL:
			case INDEX_MM:
				if (!strcmp(indexTables[i], table)
				    && !strcmp(indexFields[i], field))
				{
					if (updindex(ddic, indname, 0, options, NULL) == 0)
						rc = 0;	/* success */
				}
				else
				{
					putmsg(MWARN, Fn,
					       "Metamorph Index %s already exists on %s(%s)",
					       indname, indexTables[i], indexFields[i]);
				}
				break;
			case INDEX_BTREE:
			case INDEX_INV:
				putmsg(MWARN, Fn,
				       "BTREE Index %s already exists on %s(%s)",
				       indname, indexTables[i], indexFields[i]);
				break;
			case INDEX_3CR:
			case INDEX_MMCR:
			case INDEX_FULLCR:
				putmsg(MERR + UGE, CHARPN,
				       "Metamorph index %s is already being created on %s(%s)",
				       indname, indexTables[i], indexFields[i]);
				break;
			case INDEX_CR:
				putmsg(MERR + UGE, CHARPN,
					"BTREE or Inverted Index %s is already being created on %s(%s)",
					indname, indexTables[i], indexFields[i]);
				break;
			case INDEX_DEL:
				putmsg(MERR + FDE, Fn,
	"Cannot create index %s: Previously DROPped index %s not yet deleted, possibly still in use",
				       indname, indname);
				break;
			default:
				putmsg(MERR, Fn,
			"Cannot create index %s: Already exists as type `%c'",
				       indname, (int)indexTypes[i]);
				break;
			}
			/* KNG 20120523 we have already done something for
			 * this SYSINDEX entry -- either updated or yapped --
			 * so do not continue and e.g. silently update
			 * another entry of the same name after having
			 * complained about this one:
			 */
			break;
		}
		if (rc != 0) goto err;
		goto done;
	}
	else					/* error or not found */
	{
		numIndexes = 0;			/* for TXfreeStrList() */
	}

	/* Process options early -- because we need them, and so we
	 * can bail on error before creation -- but only *after* we
	 * know this is a creation and not an update:
	 */
	if (!TXindOptsProcessRawOptions(options, &itype, 0))
		goto err;

	/* Determine index file path.  Note: see similar rules in updindex().
	 * `ddic->pname' and `options->indexspace' assumed to have trailing
	 * slashes:
	 */
	if (TX_ISABSPATH(idxfile))
		res = TXcatpath(indfile, idxfile, "");
	else					/* `idxfile' is relative */
	{
		if (TX_ISABSPATH(options->indexspace) ||
		    options->indexspace[0] == '~')
			res = TXcatpath(indfile, options->indexspace, "");
		else				/* indexspace is db-relative*/
			res = TXcatpath(indfile, ddic->pname,
					options->indexspace);
		res = (res && TXcatpath(indfile, indfile, idxfile));
	}
	if (!res) goto err;

	TX_INDEXDEBUG_MSG((999, Fn,
		"(%u) Creating %s type %c name %s index %s",
		(unsigned)TXgetpid(0), ddic->pname, itype, indname, indfile));
	dbtb = opendbtbl(ddic, table);
	if (!dbtb)
		goto err;
	closeindexes(dbtb);
	switch (itype)
	{
	case INDEX_BTREE:
	case INDEX_UNIQUE:
		itype = INDEX_BTREE;
		ind = TXmkindCreateBtree(dbtb, field, indname, indfile,
					 unique, 0, options);
		break;
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		/* WTF need INDEX_FULL2 type to parallel INDEX_3DB2.
		 * INDEX_FULL updates _C index too if present.
		 */
		ind = open3orfind(itype, dbtb, field, indname, indfile,
				  unique, 0, options);
		goto mkFakeDbi;
		break;
	case INDEX_3DB2:			/* metamorph counter */
		itype = INDEX_MM;
		ind = open3orfind(itype, dbtb, field, indname, indfile,
				  unique, 1, options);
	mkFakeDbi:
		/* Init an A3DBI with all SYSINDEX.PARAMS settings: */
		memset(&fakeDbi, 0, sizeof(A3DBI));
		if (ind)
		{
			fakeDbi.textsearchmode = ind->textSearchMode;
			ind->auxDataSz = (itype == INDEX_MM ||
					  itype == INDEX_FULL ?
			     TXwtixGetAuxDataSize((WTIX *)ind->indexobj) : 0);
		}
		break;
	case INDEX_INV:
		ind = TXmkindCreateInverted(dbtb, field, indname, indfile,
					    unique, 0, options);
		break;
	default:
		putmsg(MWARN + UGE, Fn,
 "Cannot create index %s: Index type `%c' unknown/deprecated/not implemented",
		       indname, (int)itype);
		goto err;
	}
	if (!ind)
		goto err;

	sysindexParams[0] = '\0';
	indexCreateType = -1;
	switch (itype)
	{
	case INDEX_3DB:
		indexCreateType = INDEX_3CR;
		break;
	case INDEX_MM:
	case INDEX_FULL:
		indexCreateType = (itype == INDEX_MM ? INDEX_MMCR : INDEX_FULLCR);
		if (TX3dbiParamsToText(sysindexParams, sizeof(sysindexParams),
				       &fakeDbi) >= sizeof(sysindexParams))
		{
			putmsg(MERR + MAE, Fn,
			       "SYSINDEX.PARAMS value too large for index %s",
			       indname);
			goto err;
		}
		break;
	case INDEX_BTREE:
	case INDEX_INV:
		indexCreateType = INDEX_CR;
		break;
	default:
		putmsg(MERR, Fn,
    "Internal error: Cannot determine creation type for index type %d (`%c')",
		       (int)indexCreateType, indexCreateType);
		goto err;
	}

	/* Read-lock table, possibly for the duration of the index build: */
	if (TXlocktable(dbtb, R_LCK) != 0)
		goto err;
	uberlock++;

	/* KNG 20120601 make creating-index SYSINDEX entry path
	 * db-relative if possible:
	 */
	dbLen = strlen(ddic->pname);		/* db dir + '/' */
	if (TXpathcmp(indfile, dbLen, ddic->pname, dbLen) == 0)
		sysindexFname = indfile + dbLen; /* db-relative */
	else
		sysindexFname = indfile;	/* absolute */
	TXaddindexrec(ddic, indname, table, sysindexFname, ind->collSeq,
		      unique, field, indexCreateType, sysindexParams, RECIDPN);
	rc = TXmkindBuildIndex(ddic, dbtb, ind, &uberlock);
	gotPartialErr = 0;
	switch (rc)
	{
	case 1:		success = 1;			break;
	case 0:		success = gotPartialErr = 1;	break;
	case -1:
	default:	success = 0;			break;
	}

	/* Compute SYSINDEX.PARAMS value, and close `ind': */
	switch (itype)
	{
	case INDEX_3DB:
	case INDEX_MM:
	case INDEX_FULL:
		if (TX3dbiParamsToText(sysindexParams, sizeof(sysindexParams),
				       &fakeDbi) >= sizeof(sysindexParams))
		{
			putmsg(MERR + MAE, Fn,
			       "SYSINDEX.PARAMS value too large for index %s",
			       indname);
			success = 0;
		}
		break;
	case INDEX_BTREE:
		intsz = sizeof(sysindexParams);
		btparamtotext(ind->bt, field, dbtb, &intsz, sysindexParams);
		break;
	case INDEX_INV:
		intsz = sizeof(sysindexParams);
		btparamtotext(ind->bt, field, dbtb, &intsz, sysindexParams);
		break;
	}

	if (success)
	{
		TXaddindexrec(ddic, indname, table, sysindexFname,
			      ind->collSeq, unique, field, itype,
			      sysindexParams, RECIDPN);
		ind = TXmkindClose(ind);
	}
	else
	{
		ind = TXmkindClose(ind);
		TXdelindex(indfile, itype);
	}

	/* Delete the in-progress entry for this index that we
	 * might have added above (eg. Metamorph, full index types):
	 */
	TXdeleteSysindexEntry(ddic, table, indname, indexCreateType);

	if (success)
	{
		TXgetindexes(dbtb, PM_ALLPERMS, NULL, 0);
		setindexperms(dbtb);
	}
	goto done;

      err:
	success = 0;
      done:
	if (uberlock > 0)
	{
		TXunlocktable(dbtb, R_LCK);
		uberlock--;
	}
	if (dbtb)
		dbtb = closedbtbl(dbtb);
	indexTypes = TXfree(indexTypes);
	indexNonUnique = TXfree(indexNonUnique);
	indexFiles = TXfreeStrList(indexFiles, numIndexes);
	indexTables = TXfreeStrList(indexTables, numIndexes);
	indexFields = TXfreeStrList(indexFields, numIndexes);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numIndexes);
	return(success ? (!gotPartialErr ? 1 : 0) : -1);
}

/******************************************************************/

#ifdef TEST

void CDECL
main(argc, argv)
int argc;
char *argv[];
{
	int c;

	char *ds = (char *) NULL;
	char *field = (char *) NULL;
	char *indname = (char *) NULL;
	char *indfile = (char *) NULL;
	char *table = (char *) NULL;
	char unique = 0;
	char itype = INDEX_BTREE;

	while ((c = getopt(argc, argv, "3uf:i:m:s:t:")) != -1)
	{
		switch (c)
		{
		case '3':
			itype = INDEX_3DB;
			break;
		case 'f':
			field = optarg;
			break;
		case 'i':
			indname = optarg;
			indfile = fullpath((char *) NULL, indname, 0);
			break;
		case 'm':
			blockmax = atoi(optarg);
			break;
		case 's':
			ds = optarg;
			break;
		case 't':
			table = optarg;
			break;
		case 'u':
			unique = BT_UNIQUE;
			break;
		}
	}
	if (field == (char *) NULL || table == (char *) NULL)
	{
		fprintf(stderr,
			"usage: %s -3 -u -f fieldname -t tablename [-i indexfile]\n",
			argv[0]);
		exit(2);
	}
	createindex(ds, indfile, indname, table, field, unique, itype);
	exit(0);
}
#endif /* TEST */
