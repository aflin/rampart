#include "texint.h"

/******************************************************************/

static TXEXIT
chkindex(indexName, verbose, force)
char *indexName;	/* name of index */
int verbose;
int force;
/* Return value is TXEXIT_... code. */
{
	FLD *fld;
	TXEXIT	ret;
	BTREE *updtbl;
	EPI_OFF_T totsize = (EPI_OFF_T)0L;
	EPI_OFF_T thresh;
	DBTBL *dbtbl;
	A3DBI *dbi;
	EPI_STAT_S statb;
	RECID at, recid1;
	char indpath[PATH_MAX];
	int inum, i, type = 0;
	char *itype, **inames, **itbname, **ifname;
	char *tbname = NULL, *fname = NULL, *fldname = NULL;
	char **sysindexParamsVals = CHARPPN, *sysindexParams = CHARPN;
	size_t	saveTXindexmemUser = 0;
	int	indexmemIsSet = 0, yap = 0;

	if (verbose || ChkindVerbose) {
		yap = ((!TXchkindVerboseDatabases || !*TXchkindVerboseDatabases || TXtokenInCsvList(TXchkindVerboseDatabases, ddic->epname)) &&
		       (!TXchkindVerboseIndexes   || !*TXchkindVerboseIndexes   || TXtokenInCsvList(TXchkindVerboseIndexes, indexName)));
  }

	/* Use [Chkind] Indexmem, if set: */
	if (TXchkindIndexmemUser != (size_t)(-1))
	{
		saveTXindexmemUser = TXindexmemUser;
		TXindexmemUser = TXchkindIndexmemUser;
		indexmemIsSet = 1;
	}

	if ((verbose || (ChkindVerbose & 2)) && yap)
		putmsg(MINFO, __FUNCTION__, "Checking %s index %s", ddic->epname, indexName);

	inum = ddgetindexbyname(ddic, indexName, &itype, NULL, &inames, &itbname,	&ifname, &sysindexParamsVals);
	if (inum <= 0)
	{
		putmsg(MERR, __FUNCTION__, "Could not find %s index %s", ddic->epname, indexName);
		ret = TXEXIT_NOINDEX;
		goto done;
	}
	for (i = 0; i < inum; i++)
	{
		switch (itype[i])
		{
		case INDEX_DEL:
			TXdelindex(inames[i], INDEX_DEL);
			break;

		case INDEX_3DB:
		case INDEX_MM:
		case INDEX_FULL:
			tbname = itbname[i];
			fname = inames[i];
			fldname = firstfield(ifname[i]);
			type = itype[i];
			sysindexParams = sysindexParamsVals[i];
			break;

		case INDEX_TEMP:	/* WINDOWS - updating */
		case INDEX_3CR:
		case INDEX_MMCR:
		case INDEX_FULLCR:
		default:
			break;
		}
	}
	if (!tbname)
	{
		putmsg(MERR, __FUNCTION__, "Could not find %s index %s",
			ddic->epname, indexName);
		ret = TXEXIT_NOINDEX;
		goto done;
	}
	dbtbl = opendbtbl(ddic, tbname);
	if (!dbtbl)
	{
		putmsg(MERR, __FUNCTION__, "Could not open %s table %s",
			ddic->epname, tbname);
		ret = TXEXIT_TBLOPENFAILED;
		goto done;
	}
	fld = dbnametofld(dbtbl, fldname);
	if (!fld)
	{
		putmsg(MERR, __FUNCTION__, "Could not open field %s", fldname);
		ret = TXEXIT_TBLOPENFAILED;
		goto done;
	}
	if (ISFNBEG(fname))
		TXcatpath(indpath, fname, "");
	else
		TXcatpath(indpath, ddic->pname, fname);
	if(TXlockindex(dbtbl, INDEX_READ, NULL) == -1)
	{
		putmsg(MERR, __FUNCTION__, "Could not lock %s index %s",
			ddic->epname, indexName);
		ret = TXEXIT_LOCKOPENFAILED;
		goto done;
	}
	dbi = open3dbi(indpath, PM_SELECT, type, sysindexParams);
	if (!dbi)
	{
		putmsg(MERR, __FUNCTION__, "Could not open %s index %s",
			ddic->epname, indpath);
		TXunlockindex(dbtbl, INDEX_READ, NULL);
		ret = TXEXIT_INDEXOPENFAILED;
		goto done;
	}
	updtbl = dbi->newrec;
	if (force)
	{
		thresh = 0;
	}
	else
	{
		thresh = DFL_THRESH;
		TXddgetsysmi(ddic, indexName, &thresh, NULL);
	}
	rewindbtree(updtbl);
	while ((totsize <= thresh) && ((recid1 = btgetnext(updtbl, NULL, NULL, NULL)), TXrecidvalid(&recid1)))
	{
		RECID recid;
		size_t sz2;
		ft_blobi	*bi;

		at = recid1;
		recid = TXmygettblrow(dbtbl, &at);
		if (TXrecidvalid(&recid))
		{
			switch (fld->type & DDTYPEBITS)
			{
			case FTN_CHAR:
				getfld(fld, &sz2);
				totsize += (EPI_OFF_T)sz2;
				break;
			case FTN_INDIRECT:
				if (EPI_STAT(getfld(fld, NULL), &statb) != -1)
					totsize += (EPI_OFF_T)statb.st_size;
				break;
			case FTN_BLOB:		/* WTF need size */
				break;
			case FTN_BLOBI:
				bi = (ft_blobi *)getfld(fld, &sz2);
				if (!bi || sz2 < sizeof(ft_blobi)) break;
				sz2 = TXblobiGetPayloadSize(bi);
				if (sz2 != (size_t)(-1))
					totsize += (EPI_OFF_T)sz2;
				TXblobiFreeMem(bi);
				break;
			}
		}
	}
	if ((updtbl = dbi->upd) != BTREEPN)	/* INDEX_FULL has no upd */
	{
		rewindbtree(updtbl);
		while ((totsize <= thresh) && ((recid1 = btgetnext(updtbl, NULL, NULL, NULL)), TXrecidvalid(&recid1)))
		{
			ft_blobi	*blobi = NULL;
			size_t		sz;

			at = recid1;
			TXmygettblrow(dbtbl, &at);
			if (TXrecidvalid(gettblrow(dbtbl->tbl, &at)))
			{
				switch (fld->type & DDTYPEBITS)
				{
				case FTN_CHAR:
					totsize += (EPI_OFF_T)fld->size;
					break;
				case FTN_INDIRECT:
					if (EPI_STAT(getfld(fld, NULL), &statb) != -1)
						totsize += (EPI_OFF_T)statb.st_size;
					break;
				case FTN_BLOB:		/* WTF need size */
					break;
				case FTN_BLOBI:
					blobi = (ft_blobi *)getfld(fld, &sz);
					if (blobi && sz >= sizeof(ft_blobi))
					{
					    sz = TXblobiGetPayloadSize(blobi);
					    if (sz != (size_t)(-1))
						    totsize += (EPI_OFF_T)sz;
					    TXblobiFreeMem(blobi);
					}
					break;
				}
			}
		}
	}
	{
		/* Record our check, and how much changed. */
		char *t;

		t = TXstrcat3(tbname, ".", indexName);
#if EPI_OS_LONG_BITS >= EPI_OFF_T_BITS
		TXsetstatistic(ddic, t, "CHKIND", (long)totsize, "", 0);
#else
		TXsetstatistic(ddic, t, "CHKIND", (long)TX_MIN(totsize, (EPI_OFF_T)EPI_OS_LONG_MAX), "", 0);
#endif
	}
	dbi = close3dbi(dbi);
	TXunlockindex(dbtbl, INDEX_READ, NULL);
	dbtbl = closedbtbl(dbtbl);
	if ((verbose || (ChkindVerbose & 2)) && yap)
		putmsg(MINFO, __FUNCTION__,
		       "Found %kwdB new data for index %s",
			(EPI_HUGEUINT)totsize, indexName);
	if (force || totsize > thresh)
	{
		TXindOpts	*options = NULL;

		if ((verbose || (ChkindVerbose & 1)) && yap)
			putmsg(MINFO, __FUNCTION__, "%s to update %s index %s",
				(totsize > thresh ? "Found enough new data" : "Forced"),
			       ddic->epname, indexName);
		TXsetProcDescForChkindProgress(ddic->epname, indexName,
					       "updating");
		options = TXindOptsOpen(ddic);
		if (!options)
		{
			ret = TXEXIT_OUTOFMEMORY;
			goto done;
		}
		updindex(ddic, indexName, 0, options);
		options = TXindOptsClose(options);
		if ((verbose || (ChkindVerbose & 1)) && yap)
			putmsg(MINFO, __FUNCTION__, "Finished updating %s index %s",
			       ddic->epname, indexName);
	}
	else if ((verbose || (ChkindVerbose & 2)) && yap)
		putmsg(MINFO, __FUNCTION__, "Not enough changed to update %s index %s (threshold %kwdB)", ddic->epname, indexName, (EPI_HUGEUINT)thresh);
	ret = TXEXIT_OK;
done:
	if (indexmemIsSet)
		TXindexmemUser = saveTXindexmemUser;
	return(ret);
}

/******************************************************************/

static TXEXIT dochkindonce ARGS((int, int));

static TXEXIT
dochkindonce(verbose, force)
int verbose;
int force;
/* Returns TXEXIT code. */
{
	char *itype, **indexNames;
	int inum, i;
	TXEXIT	ret = TXEXIT_OK, res;

	inum = TXddgetindexinfo(ddic, NULL, NULL, &itype, NULL,
                                &indexNames, NULL, NULL, NULL, NULL);
	for (i = 0; i < inum; i++)
	{
		switch (itype[i])
		{
		case INDEX_3DB:
		case INDEX_MM:
		case INDEX_FULL:
			res = chkindex(indexNames[i], verbose, force);
			if (res != TXEXIT_OK) ret = res;
			break;
		}
	}

	return ret;
}

/******************************************************************/

int
main(int argc, char **argv)
{
  int largc;
  char **largv;
  int i, rc;

  TXinitapp(NULL, NULL, argc, argv, &largc, &largv);
  TXcloseapp();
  if(largv) {
    for(i = 0; i < largc; i++) {
      if(largv[i]) {
        largv[i] = TXfree(largv[i]);
      }
    }
    largv = TXfree(largv);
  }
  return rc;
}
