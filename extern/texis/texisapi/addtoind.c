#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"

void
tx_invdata2loc(ploc, v, type, desc)
BTLOC	*ploc;		/* destination */
void	*v;		/* pointer to FTN_... data */
int	type;		/* FTN_... type */
int	desc;		/* nonzero: descending index */
/* Copies and converts `v' to `*ploc' for inverted index.
 * NOTE: see also openvind()
 */
{
	switch (type & FTN_VarBaseTypeMask)
	{
	case FTN_DWORD:
		TXsetrecid(ploc, (EPI_OFF_T) *(ft_dword *)v);
		/* KNG 020430 historical compatibility: always ulong:
		 * WTF assumes sizeof(ulong) >= sizeof(dword):
		 */
		if (desc)
			TXsetrecid(ploc,
				(EPI_OFF_T) (EPI_OS_ULONG_MAX -
					(unsigned long) TXgetoff(ploc)));
		break;
	case FTN_DATE:
		TXsetrecid(ploc, (EPI_OFF_T) *(ft_date *)v);
		/* KNG 020430 historical compatibility: always ulong.
		 * WTF WTF order-by fails if >32-bit or signed dates:
		 */
		if (desc)
			TXsetrecid(ploc,
				(EPI_OFF_T) (EPI_OS_ULONG_MAX -
					(unsigned long) TXgetoff(ploc)));
		break;
	/* Newly supported types added KNG 020430.  Adding EPI_OFF_T_MAX + 1
	 * to the value makes fucmp() sort our signed values correctly:
	 */
	case FTN_WORD:
		TXsetrecid(ploc, (EPI_OFF_T) *(ft_word *)v);
		if (desc)
			TXsetrecid(ploc, (EPI_OFF_T) ((unsigned long)0xFFFF -
					(unsigned long) TXgetoff(ploc)));
		break;
	case FTN_INT:
		TXsetrecid(ploc, (EPI_OFF_T)((EPI_HUGEUINT)EPI_OFF_T_MAX + (EPI_HUGEUINT)1) +
			(EPI_OFF_T)(desc ? -*(ft_int *)v : *(ft_int *)v));
		break;
	case FTN_INTEGER:
		TXsetrecid(ploc, (EPI_OFF_T)((EPI_HUGEUINT)EPI_OFF_T_MAX + (EPI_HUGEUINT)1) +
			(EPI_OFF_T)(desc ? -*(ft_integer*)v:*(ft_integer*)v));
		break;
	case FTN_LONG:
		TXsetrecid(ploc, (EPI_OFF_T)((EPI_HUGEUINT)EPI_OFF_T_MAX + (EPI_HUGEUINT)1) +
			(EPI_OFF_T)(desc ? -*(ft_long *)v : *(ft_long *)v));
		break;
	case FTN_SHORT:
		TXsetrecid(ploc, (EPI_OFF_T)((EPI_HUGEUINT)EPI_OFF_T_MAX + (EPI_HUGEUINT)1) +
			(EPI_OFF_T)(desc ? -*(ft_short *)v : *(ft_short *)v));
		break;
	case FTN_SMALLINT:
		TXsetrecid(ploc, (EPI_OFF_T)((EPI_HUGEUINT)EPI_OFF_T_MAX + (EPI_HUGEUINT)1) +
			(EPI_OFF_T)(desc?-*(ft_smallint*)v:*(ft_smallint*)v));
		break;
	/* WTF cannot do float/double etc. without changing bt->cmp in
	 * TXindsort2() et al. to handle them.
	 */
	default:
		/* shouldn't happen; openvind() checks this: */
		putmsg(MERR + UGE, "tx_invdata2loc", "Unknown type");	return;
	}
}

/******************************************************************/

int
_addtoinv(bv, pos)
BINVDX	bv;
BTLOC	*pos;
{
	BTLOC	 btloc;

	if (!bv.fld)
		return -1;
	tx_invdata2loc(&btloc, getfld(bv.fld, NULL), bv.fld->type, bv.rev);
	btinsert(bv.btree, &btloc, sizeof(RECID), pos);
	/* WTF */
	return 0;
}

/******************************************************************/

int
delfrominv(bv, pos)
BINVDX	bv;
BTLOC	*pos;
{
	BTLOC	 btloc;

	if (!bv.fld)
		return -1;
	tx_invdata2loc(&btloc, getfld(bv.fld, NULL), bv.fld->type, bv.rev);
	btdelete(bv.btree, &btloc, sizeof(RECID), pos);
	/* WTF */
	return 0;
}


/******************************************************************/

TXA2IND *
TXadd2indsetup(tup, index)
DBTBL *tup;
BINDEX *index;
{
	DD	*idd;
	TBL	*tbl;
	TXA2IND	*rc;
	int	i;
	char	*fname;

	rc = (TXA2IND *)calloc(1, sizeof(TXA2IND));
	if(!rc)
		return rc;
	idd = btreegetdd(index->btree);
	tbl = index->table;
	rc->nfields = ddgetnfields(idd);
	rc->fin = (FLD **)calloc(rc->nfields, sizeof(FLD *));
	rc->fout = (FLD **)calloc(rc->nfields, sizeof(FLD *));
	if(!rc->fin || !rc->fout)
		return TXadd2indcleanup(rc);
	rc->tbl = tbl;
	rc->index = index;
	for (i=0; i < rc->nfields; i++)
	{
		fname = ddgetname(idd, i);
		rc->fin[i] = dbnametofld(tup, fname);
		rc->fout[i] = nametofld(tbl, fname);
	}
	rc->tup = tup;
	return rc;
}

/******************************************************************/

size_t
TXa2i_setbuf(as)
TXA2IND *as;
/* Creates data buffer from appropriate fields, in preparation for
 * writing to as->index->btree (or token file).  NOTE: not used for
 * strlst unless going to token file?
 * Returns size of data in `as->tbl->orec', or -1 on error.
 */
{
  void          *v;
  size_t        sz, maxsz;
  int           i;

  maxsz = (as->index ? as->index->btree->params.max_index_text : 0);
  for (i = 0; i < as->nfields; i++)
    {
      v = getfld(as->fin[i], &sz);
      if(maxsz && sz > maxsz)
      {
	sz = maxsz;
      }
      putfld(as->fout[i], v, sz);
      DBGMSG(9,(999, NULL, "Prepping %s for index insert/delete",
                fldtostr(as->fout[i])));
    }
  return(fldtobuf(as->tbl));
}

/******************************************************************/

char *
TXa2i_tostring(as)
TXA2IND *as;
{
	char *x, *d = NULL;
	char tmpbuf[256];
	int i, sl, finIdx;
	DD	*dd;

	dd = btreegetdd(as->index->btree);
	d = tmpbuf;
	*d = '\0';
	for (i = 0; i < as->nfields; i++)
	{
		if (i > 0)
			*(d++) = ',';
		/* KNG 20090925 Bug 2450: report fields in create order: */
		finIdx = ddgetorign(dd, i);
		if (finIdx < 0)			/* error */
			x = "?";
		else
			x = fldtostr(as->fin[finIdx]);
		sl = strlen(x);
		if (d + sl < tmpbuf + sizeof(tmpbuf) - 4)
		{
			strcpy(d, x);
			d += sl;
		}
		else
		{
			sl = (tmpbuf + sizeof(tmpbuf) - 4) - d;
			if (sl > 0)
			{
				memcpy(d, x, sl);
				d += sl;
			}
			strcpy(d, "...");
			break;
		}
	}
	return strdup(tmpbuf);
}

/******************************************************************/

int
TXa2i_btreeinsert(as, pos)
TXA2IND *as;
BTLOC   *pos;
/* Inserts TXa2i_setbuf()-prepped data from `as' into its B-tree.
 */
{
  int           rc;
  size_t        sz;
  BTLOC         btloc;
  BTREE         *bt = as->index->btree;
  char          *asStr = CHARPN;

  /* WTF strlst is multi-row insert now: */
  if (as->nfields == 1 && (as->fin[0]->type & DDTYPEBITS) == FTN_STRLST &&
      bt->params.indexValues == TXindexValues_SplitStrlst)
    {
      putmsg(MERR, "TXa2i_btreeinsert",
             "Cannot handle split-strlst insert into index %s",
             getdbffn(bt->dbf));
      rc = -1;
      goto done;
    }

  btloc = *pos;
  sz = as->tbl->orecdatasz;
  rc = btinsert(bt, &btloc, sz, as->tbl->orec);
  if (rc == -1 && (as->tup->ddic->messages[MESSAGES_DUPLICATE] ||
                   (!(bt->flags & BT_UNIQUE))))
    {
      asStr = TXa2i_tostring(as);
      putmsg(((bt->flags & BT_UNIQUE) ? (MWARN + XUI) : (MERR + FWE)), CHARPN,
             ((bt->flags & BT_UNIQUE) ?
              "Trying to insert duplicate value (%s) in index %s" :
              "Cannot insert value (%s) in index %s"),
             asStr, getdbffn(bt->dbf));
      if (bt->flags & BT_UNIQUE)
        {
          if (TXbtreedump & (1 << 18))
            btdump(bt, TXbtreedump);
        }
      else
        {
          if (TXbtreedump & (1 << 16))
            btdump(bt, TXbtreedump);
        }
    }
done:
  if (asStr != CHARPN) free(asStr);
  return(rc);
}

int
TXa2i_btreedelete(as, pos)
TXA2IND *as;
BTLOC   *pos;
/* Deletes a TXa2i_setbuf()-prepped `as' from the B-tree.
 * Returns 1 if deleted, 0 if wasn't present in tree, -1 on error.
 * WTF may not handle strlst?
 */
{
  BTLOC btloc;
  int rc;
  BTREE *bt = as->index->btree;
  char *asStr = CHARPN;

  /* WTF strlst is multi-row delete now: */
  if (as->nfields == 1 && (as->fin[0]->type & DDTYPEBITS) == FTN_STRLST &&
      bt->params.indexValues == TXindexValues_SplitStrlst)
    {
      putmsg(MERR, "TXa2i_btreedelete",
             "Cannot handle split-strlst delete from index %s",
             getdbffn(bt->dbf));
      rc = -1;
      goto done;
    }

  btloc = *pos;
  rc = btdelete(bt, &btloc, as->tbl->orecdatasz, as->tbl->orec);
  if ((rc != 1) && (1 == as->tup->ddic->messages[MESSAGES_FAILED_DELETE]))
    {
      asStr = TXa2i_tostring(as);
      putmsg(MERR + FWE, CHARPN, "Cannot delete value (%s) from index %s",
             asStr, getdbffn(bt->dbf));
      if (TXbtreedump & (1 << 17)) btdump(bt, TXbtreedump);
    }
done:
  if (asStr != CHARPN) free(asStr);
  return rc;
}

/* ------------------------------------------------------------------------ */

static int TXaddtoindChangeLocSplitStrlst ARGS((TXA2IND *a2i, BTLOC newLoc));
static int
TXaddtoindChangeLocSplitStrlst(a2i, newLoc)
TXA2IND *a2i;
BTLOC   newLoc;         /* (in) new location */
/* Returns 2 if changed, 1 if one or more values not found, 0 on error.
 */
{
  size_t        sz;
  int           i;
  int           ret = 2;
  char          **list = CHARPPN;
  BTREE         *bt = a2i->index->btree;

  /* Bug 4210: do not skip empty-string items of strlst: */
  list = TXfstrlsttoc(a2i->fin[0], 0);
  if (list == CHARPPN) goto err;
  for (i = 0; list[i] != CHARPN; i++)
    {
      putfld(a2i->fout[0], list[i], strlen(list[i]));
      DBGMSG(9, (999, CHARPN,
                 "Changing recid for %s in index from 0x%wx to 0x%wx",
                 list[i], (EPI_HUGEINT)TXgetoff2(&a2i->tup->recid),
                 (EPI_HUGEINT)TXgetoff2(&newLoc)));
      sz = fldtobuf(a2i->tbl);
      switch (TXbtreeChangeLocOrKey(bt, a2i->tbl->orec, a2i->tbl->orecdatasz,
                                    a2i->tup->recid, newLoc, BYTEPN))
        {
        case 2:                                 /* success */
          break;
        case 1:                                 /* not found */
          ret = 1;
          break;
        case 0:                                 /* error */
        default:
          goto err;
        }
    }
  goto done;

err:
  ret = 0;                                      /* error */
done:
  if (list != CHARPPN) list = freenlst(list);
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXaddtoindChangeLoc(a2i, newLoc, isMm)
TXA2IND *a2i;
BTLOC   newLoc; /* (in) new location */
int     isMm;   /* (in) nonzero: is (part of) a Metamorph index */
/* Changes `a2i' index's current (DBTBL) recid to `newLoc'.
 * Used by TXcompactTable() for Metamorph new list, regular indexes.
 * Returns 2 if changed, 1 if not found, 0 on error.
 */
{
  int   ret;
  BTREE *bt = a2i->index->btree;

  /* strlst is multi-row insert now, for regular (non-Metamorph) indexes: */
  if (!isMm &&
      a2i->nfields == 1 && (a2i->fin[0]->type & DDTYPEBITS) == FTN_STRLST &&
      bt->params.indexValues == TXindexValues_SplitStrlst)
    {
      ret = TXaddtoindChangeLocSplitStrlst(a2i, newLoc);
      goto done;
    }

  if (TXa2i_setbuf(a2i) == (size_t)(-1))        /* prepare `a2i->tbl->orec' */
    goto err;

  ret = TXbtreeChangeLocOrKey(bt, a2i->tbl->orec, a2i->tbl->orecdatasz,
                              a2i->tup->recid, newLoc, BYTEPN);
  goto done;

err:
  ret = 0;                                      /* error */
done:
  return(ret);
}

/* ------------------------------------------------------------------------ */

int
TXaddtoindChangeLocInverted(binv, oldLoc, newLoc)
BINVDX  *binv;  /* (in/out) index object */
BTLOC   oldLoc; /* (in) current location */
BTLOC   newLoc; /* (in) new location */
/* Changes `binv' index's current (DBTBL) recid `oldLoc' to `newLoc'.
 * Used by TXcompactTable() for inverted indexes.
 * Returns 2 if changed, 1 if not found, 0 on error.
 */
{
  static CONST char     fn[] = "TXaddtoindChangeLocInverted";
  BTLOC                 dataLoc;

  if (binv->fld == FLDPN)
    {
      putmsg(MERR + UGE, fn, "Internal error: Missing field");
      return(0);
    }
  tx_invdata2loc(&dataLoc, getfld(binv->fld, NULL), binv->fld->type,
                 binv->rev);
  return(TXbtreeChangeLocOrKey(binv->btree, &oldLoc, sizeof(BTLOC), dataLoc,
                               dataLoc, (byte *)&newLoc));
}

/******************************************************************/

int
TXdel2ind(as, pos)
TXA2IND *as;
BTLOC *pos;
/* Sets up and writes data to index B-tree.  Returns -1 on (possible?)
 * error, 0 if ok.  Does not touch token file if Metamorph index.
 */
{
  if (as->nfields == 1 && (as->fin[0]->type & DDTYPEBITS) == FTN_STRLST &&
      as->index->btree->params.indexValues == TXindexValues_SplitStrlst)
    return(TXdel2indSplitStrlst(as, pos));      /* becomes multi-row insert */
  TXa2i_setbuf(as);
  return(TXa2i_btreedelete(as, pos));
}

/******************************************************************/

int
TXadd2ind(as, pos)
TXA2IND *as;
BTLOC *pos;
/* Sets up and writes data to index B-tree.  Returns -1 on (possible?)
 * error, 0 if ok.  Does not touch token file if Metamorph index.
 */
{
  if (as->nfields == 1 && (as->fin[0]->type & DDTYPEBITS) == FTN_STRLST &&
      as->index->btree->params.indexValues == TXindexValues_SplitStrlst)
    return(TXadd2indSplitStrlst(as, pos));      /* becomes multi-row insert */
  TXa2i_setbuf(as);
  return(TXa2i_btreeinsert(as, pos));
}

/******************************************************************/

int
TXdel2indSplitStrlst(as, pos)
TXA2IND *as;
BTLOC *pos;
/* WTF no parallel in TXa2i_btreedelete()
 */
{
	BTLOC btloc;
	size_t sz;
	int i;
	int rc = 0;
	char **sl;
	BTREE *bt = as->index->btree;

	btloc = *pos;
        /* Bug 4210: do not skip empty-string items of strlst: */
	sl = TXfstrlsttoc(as->fin[0], 0);
	if(!sl)
		return -1;
	for(i=0; sl[i]; i++)
	{
		putfld(as->fout[0], sl[i], strlen(sl[i]));
		DBGMSG(9,(999, NULL, "Adding %s to index at %wd",
			sl[i], (EPI_HUGEINT)TXgetoff(&btloc)));
		sz = fldtobuf(as->tbl);
		rc = btdelete(bt, &btloc, sz, as->tbl->orec);
	}
	sl = freenlst(sl);
	return rc;
}

/******************************************************************/

int
TXadd2indSplitStrlst(as, pos)
TXA2IND *as;
BTLOC *pos;
/* WTF no parallel in TXa2i_btreedelete()
 */
{
	BTLOC btloc;
	size_t sz;
	int i;
	int rc = 0;
	char **sl;
	BTREE *bt = as->index->btree;

	btloc = *pos;
        /* Bug 4210: do not strip empty items from strlst: */
	sl = TXfstrlsttoc(as->fin[0], 0);
	if(!sl)
		return -1;
	for (i = 0; sl[i]; i++)
	{
		putfld(as->fout[0], sl[i], strlen(sl[i]));
		DBGMSG(9,(999, NULL, "Adding %s to index at %wd",
			sl[i], (EPI_HUGEINT)TXgetoff(&btloc)));
		sz = fldtobuf(as->tbl);
		rc = btinsert(bt, &btloc, sz, as->tbl->orec);
		if(rc == -1)
		{
			char *asStr;

			asStr = TXa2i_tostring(as);
			putmsg(((bt->flags & BT_UNIQUE) ? (MWARN + XUI) :
				(MERR + FWE)), CHARPN,
			       ((bt->flags & BT_UNIQUE) ?
		 "Trying to insert duplicate value (%s%s) in index" :
		 "Cannot insert value (%s) in index %s"),
			       (asStr != CHARPN ? asStr : "?"),
				getdbffn(bt->dbf));
			if (asStr != CHARPN) free(asStr);
                        if (bt->flags & BT_UNIQUE)
                          {
                            if (TXbtreedump & (1 << 18))
                              btdump(bt, TXbtreedump);
                          }
                        else
                          {
                            if (TXbtreedump & (1 << 16))
                              btdump(bt, TXbtreedump);
                          }
		}
	}
	sl = freenlst(sl);
	return rc;
}

/******************************************************************/

BTLOC
TXsearch2ind(as)
TXA2IND *as;
{
	BTLOC btloc;
	void *v;
	size_t sz;
	int i;

	for (i=0; i < as->nfields; i++)
	{
		v = getfld(as->fin[i], &sz);
		putfld(as->fout[i], v, sz);
		DBGMSG(9,(999, NULL, "Adding %s to index at %d",
			fldtostr(as->fout[i]), TXgetoff(&btloc)));
	}
	sz = fldtobuf(as->tbl);
	btloc = btsearch(as->index->btree, sz, as->tbl->orec);
	return btloc;
}

/******************************************************************/

TXA2IND *
TXadd2indcleanup(as)
TXA2IND *as;
{
	if(!as)
		return NULL;

	if(as->fin)
		free(as->fin);
	as->fin = NULL;
	if(as->fout)
		free(as->fout);
	as->fout = NULL;
	free(as);
	return NULL;
}

/******************************************************************/
/*	Insert a row from tup into index.  This makes a buffer out
 *	of the field data in tup, and inserts it as a field into
 *	the btree.  The index cannot hold blob data.
 */

int
addtuptoindex(tup, index, pos)
DBTBL *tup;
BINDEX index;
BTLOC *pos;
{
	int	rc;
	void	*ai;

	ai = TXadd2indsetup(tup, &index);
	rc = TXadd2ind(ai, pos);
	TXadd2indcleanup(ai);
	return rc;
}

/******************************************************************/
/*	This deletes a tuple from an index
 * WTF-done this can be replaced with some combo of TXa2i_btreedelete() etc.
 */
int
deltupfromindex(tup, index, pos)
DBTBL *tup;
BINDEX index;
BTLOC *pos;
{
	int	rc;
	void	*ai;

	ai = TXadd2indsetup(tup, &index);
	rc = TXdel2ind(ai, pos);
	TXadd2indcleanup(ai);
	return rc;
}
