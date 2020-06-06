/* -=- kai-mode: John -=- */
#include "txcoreconfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef USE_EPI
#include "os.h"
#endif
#include "dbquery.h"
#include "texint.h"
#include "fldcmp.h"
#include "fdbi.h"

#if defined(unix) && !defined(_AIX)
int fchmod ARGS((int, mode_t));
#endif
void closeindexes ARGS((DBTBL *));

/******************************************************************/

int
getindexes(t)
DBTBL	*t;
{
	static	char	Fn[]="getindexes";

	(void)t;
	putmsg(MINFO, Fn, "Obsolete function call.  This call will be removed in a future version");
	return 0;
}

/******************************************************************/

static int fieldsmatch ARGS((char *field, char **fieldlist));
static int
fieldsmatch(field, fieldlist)
char *field;
char **fieldlist;
{
	char *tfield, *cfname, **cfield;
	int i;

	if(!fieldlist)
		return 1;
	tfield = strdup(field);
	cfname = strtok(tfield, ", -^");
	i = 1;
	while(cfname)
	{
		for(cfield=fieldlist; *cfield[0]; cfield++)
			if(!strcmp(cfname, *cfield))
			{
				free(tfield);
				return i;
			}
		cfname = strtok(NULL, ", -^");
		i++;
	}
	free(tfield);
	return 0;
}

/******************************************************************/
/*	Open all the indexes associated with t.  This allows them
 *	to be updated on insert, update etc.
 */

int
TXgetindexes(t, mode, fields, mmViaFdbi)
DBTBL	*t;
int	mode;		/* (in) PM_... flags */
char	**fields;
int	mmViaFdbi;	/* (in) nonzero: open MM as FDBI objects */
/* Returns 0 on success, -1 on error.
 */
{
	static CONST char	Fn[] = "TXgetindexes";
	static CONST char	unableToOpen[] =
					"Unable to open index %s for %s";
	int i, nb=0, n3=0, nf=0, nv = 0, nt, rc = 0, updfield = 0;
	int dof;
	char *itype, *iunique, **iidxn, **ifields, **iname, **iparams;
	FLDCMP *fc;
	BTREE *index;
	A3DBI *t3;
        FDBI    *f;
	DBTBL	*savtbl;
	char	permBuf[128];

	savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = t;			/* for btreelog debug */

	if(TXlocktable(t, R_LCK)==-1)		/* failed to get lock */
		goto err;
	nt = TXddgetindexinfo(t->ddic, t->lname, (char *)NULL, &itype,
		&iunique, &iidxn, &iname, &ifields, &iparams, NULL);
	dof = t->ddic->optimizations[OPTIMIZE_MMIDXUPDATE];
	TXunlocktable(t, R_LCK);
	for (i=0; i < nt; i++)
	{
		updfield = fieldsmatch(ifields[i], fields);
		if(updfield > 0)
		{
			switch(itype[i])
			{
				case INDEX_BTREE: nb++; break;
				case INDEX_MM:                  /* KNG 980420 */
				case INDEX_FULL:                /* KNG 971111 */
					if((updfield > 1 && dof) || mmViaFdbi)
					{
						nf++;
						break;
					}
				case INDEX_MMCR:                /* KNG 980420 */
				case INDEX_FULLCR:              /* KNG 971112 */
				case INDEX_3CR:
				case INDEX_3DB: n3++; break;
				case INDEX_INV: nv++; break;
			}
		}
	}
	closeindexes(t);
	if(nb)
	{
		t->indexes = (BINDEX *)TXcalloc(TXPMBUFPN, Fn, nb,
						sizeof(BINDEX));
		t->indexNames = (char **)TXcalloc(TXPMBUFPN, Fn, nb,
						  sizeof(char *));
		t->indexFldNames = (char **)TXcalloc(TXPMBUFPN, Fn, nb,
							sizeof(char *));
		t->indexParams = (char **)TXcalloc(TXPMBUFPN, Fn, nb,
							sizeof(char *));
	}
	else
	{
		t->indexes = NULL;
		t->indexNames = t->indexFldNames = t->indexParams = CHARPPN;
	}
	if(nv)
	{
		t->invidx = (BINVDX *)TXcalloc(TXPMBUFPN, Fn, nv,
						sizeof(BINVDX));
		t->invertedIndexNames = (char **)TXcalloc(TXPMBUFPN, Fn,nv,
							  sizeof(char *));
		t->invertedIndexFldNames = (char **)TXcalloc(TXPMBUFPN, Fn,
							nv, sizeof(char *));
		t->invertedIndexParams = (char **)TXcalloc(TXPMBUFPN, Fn,
							nv, sizeof(char *));
	}
	else
	{
		t->invidx = NULL;
		t->invertedIndexNames = t->invertedIndexFldNames = CHARPPN;
		t->invertedIndexParams = CHARPPN;
	}
	if(n3)
		t->dbies = (A3DBI **)TXcalloc(TXPMBUFPN, Fn, n3,
						 sizeof(A3DBI *));
	else
		t->dbies = NULL;
	if(nf)
	{
		t->fdbies = (FDBI **)TXcalloc(TXPMBUFPN, Fn, nf,
						 sizeof(FDBI *));
		t->fdbiIndexNames = (char **)TXcalloc(TXPMBUFPN, Fn, nf,
							 sizeof(char *));
		t->fdbiIndexFldNames = (char **)TXcalloc(TXPMBUFPN, Fn, nf,
							    sizeof(char *));
		t->fdbiIndexParams = (char **)TXcalloc(TXPMBUFPN, Fn, nf,
							  sizeof(char *));
	}
	else
	{
		t->fdbies = NULL;
		t->fdbiIndexNames = t->fdbiIndexFldNames = CHARPPN;
		t->fdbiIndexParams = CHARPPN;
	}
	t->nindex = nb;
	t->ndbi = n3;
	t->ninv = nv;
	t->nfdbi = nf;
	nb = 0; n3 = 0; nv = 0, nf = 0;
	for (i=0; i < nt; i++)
	{
		updfield = fieldsmatch(ifields[i], fields);
		if(updfield > 0)
		{
			switch(itype[i])
			{
			case INDEX_BTREE :
				index = t->indexes[nb].btree =
					openbtree(iname[i], BT_MAXPGSZ, 20, 0, O_RDWR);
				if(index)
				{
					DD *dd;

					if (bttexttoparam(index, iparams[i])<0)
					{	/* failed */
						index = t->indexes[nb].btree =
							closebtree(index);
						goto btreeErr;
					}
					dd = btreegetdd(index);
					if(dd)
					{
					t->indexNames[nb] = TXstrdup(TXPMBUFPN, Fn, iidxn[i]);
					t->indexFldNames[nb] = TXstrdup(TXPMBUFPN, Fn, ifields[i]);
					t->indexParams[nb] = TXstrdup(TXPMBUFPN, Fn, iparams[i]);
					btsetcmp(index, (btcmptype)fldcmp);
					fc = TXopenfldcmp(index,
						   TXOPENFLDCMP_CREATE_FLDOP);
					index->usr = fc;
					t->indexes[nb].table=createtbl(dd,NULL);
					nb++;
					}
				}
				else
				{
				btreeErr:
					putmsg(MERR, NULL,  unableToOpen,
						iname[i],
						TXpermModeToStr(permBuf,
						  sizeof(permBuf), mode));
					rc = -1;
					goto cleanup;
				}
				break;
			case INDEX_MM:                  /* KNG 980420 */
			case INDEX_FULL:                /* KNG 971111 */
				if((updfield > 1 && dof) || mmViaFdbi)
				{
					if(TXlocktable(t, R_LCK)==-1)
					{
						rc = -1;
						goto cleanup;
					}
					f = t->fdbies[nf] = openfdbi(iname[i], mode, (itype[i] == INDEX_FULL ? FDF_FULL : (FDF)0), iparams[i], t);
					if(t->fdbies[nf] != (FDBI *)NULL)
					{
						t->fdbiIndexNames[nf] = TXstrdup(TXPMBUFPN, Fn, iidxn[i]);
						t->fdbiIndexFldNames[nf] = TXstrdup(TXPMBUFPN, Fn, ifields[i]);
						t->fdbiIndexParams[nf] = TXstrdup(TXPMBUFPN, Fn, iparams[i]);
                                          /* wtf set `creating' if we ever
                                           * open these on a creating index
                                           */
						nf++;
					}
					TXunlocktable(t, R_LCK);
					if (f == FDBIPN)
					{
						putmsg(MERR, NULL,
							unableToOpen, iname[i],
						    TXpermModeToStr(permBuf,
						      sizeof(permBuf), mode));
						rc = -1;
						goto cleanup;
					}
					break;
				}
                                /* Don't use FDBI direct-token update during
                                 * index create: token file is in flux.
                                 * Also not during index update, but that's
                                 * checked in delfromfdbi()/addtofdbi().
                                 */
			case INDEX_3CR :
			case INDEX_3DB :
			case INDEX_MMCR:                /* KNG 980420 */
			case INDEX_FULLCR:              /* KNG 971112 */
				if(TXlocktable(t, R_LCK)==-1)
				{
					rc = -1;
					goto cleanup;
				}
				t3 = t->dbies[n3] = open3dbi(iname[i], mode, itype[i], iparams[i]);
				if(t->dbies[n3] != (A3DBI *)NULL)
				{
					if(itype[i] == INDEX_3CR ||
					   itype[i] == INDEX_MMCR ||
					   itype[i] == INDEX_FULLCR)
						t->dbies[n3]->creating = 1;
					n3++;
				}
				TXunlocktable(t, R_LCK);
				if(t3 == (A3DBI *)NULL)
				{
					putmsg(MERR, NULL, unableToOpen,
						iname[i],
						TXpermModeToStr(permBuf,
						    sizeof(permBuf), mode));
					rc = -1;
					goto cleanup;
				}
				break;
			case INDEX_INV :
				t->invidx[nv].btree = index =
					openbtree(iname[i], BT_MAXPGSZ, 20, 0, O_RDWR);
				if (index != BTREEPN)
				{
					t->invertedIndexNames[nv] = TXstrdup(TXPMBUFPN, Fn, iidxn[i]);
					t->invertedIndexFldNames[nv] = TXstrdup(TXPMBUFPN, Fn, ifields[i]);
					t->invertedIndexParams[nv] = TXstrdup(TXPMBUFPN, Fn, iparams[i]);
					if (bttexttoparam(index, iparams[i])<0)
					{	/* failed */
						index = t->invidx[nv].btree =
							closebtree(index);
						goto invErr;
					}
					t->invidx[nv].fld =
						dbnametofld(t, ifields[i]);
					t->invidx[nv].rev =
						ifields[i][strlen(ifields[i])-1] == '-' ? 1 : 0;
					nv++;
				}
				else
				{
				invErr:
					putmsg(MERR, NULL, unableToOpen,
						iname[i],
						TXpermModeToStr(permBuf,
						    sizeof(permBuf), mode));
					rc = -1;
					goto cleanup;
				}
				break;
			case INDEX_DEL:		/* Windows: to be deleted */
				/* no update needed */
				break;
			case INDEX_TEMP:	/* Windows MM-update or */
				                /* ALTER INDEX REBUILD */
				/* no update needed: new/del _X/Z lists of
				 * original index will be updated.  _D/_T
				 * lists of INDEX_TEMP may not even exist.
				 */
				break;
			case INDEX_CR:
				if (mode == PM_SELECT) break;	/* no mods */
				/* KNG 20070409 We should not have seen a
				 * regular index-being-created, because the
				 * creator holds a read lock for the duration.
				 * Index was probably abandoned during create;
				 * in any event, table mods may make it
				 * out-of-sync:
				 */
				putmsg(MERR + FOE, Fn,
		"Cannot modify table %s: Index %s still being created",
					t->lname, iidxn[i]);
				rc = -1;
				goto cleanup;
			default:
			/* KNG 20070212 If an unknown index type is found,
			 * and we're trying to modify the table, fail the
			 * transaction, because we cannot update that index
			 * and thus it will get out of sync with the table.
			 * This could happen if an old version of Texis
			 * attempts to update a table with a new type of
			 * index created by a new version of Texis.
			 * SELECTs are ok; we just cannot utilize that index:
			 */
				if (mode & ~PM_SELECT)	/* PM_UPDATE etc. */
				{
					putmsg(MERR + FOE, Fn,
		"Cannot modify table %s: Unknown type `%c' for index %s",
					t->lname, (int)itype[i], iidxn[i]);
					rc = -1;
					goto cleanup;
				}
				break;
			}
		}
	}
cleanup:
	t->nindex = nb;
	t->ndbi = n3;
	t->ninv = nv;
	t->nfdbi = nf;
	if (itype != (char *)NULL)
		free(itype);
	if (iunique != (char *)NULL)
		free(iunique);
	if (iname != (char **)NULL)
	{
		for (i = 0; i < nt; i++)
			if (iname[i])
				free(iname[i]);
		free(iname);
	}
	if (iidxn != (char **)NULL)
	{
		for (i = 0; i < nt; i++)
			if(iidxn[i])
				free(iidxn[i]);
		free(iidxn);
	}
	if (ifields != (char **)NULL)
	{
		for (i = 0; i < nt; i++)
			if(ifields[i])
				free(ifields[i]);
		free(ifields);
	}
	if (iparams != (char **)NULL)
	{
		for (i = 0; i < nt; i++)
			if(iparams[i])
				free(iparams[i]);
		free(iparams);
	}
	if(t->indexfields)
	{
		t->indexfields = _freelst(t->indexfields);
	}
	if(fields)
	{
		t->indexfields = _duplst(fields);
	}
	goto done;

err:
	rc = -1;
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
	return rc;
}

/******************************************************************/
/*	Flush all the indexes opened by getindexes
 */

void
flushindexes(db)
DBTBL *db;
{
	int i;
	DBTBL	*savtbl;
#ifdef NEVER
	FLDCMP *fc;
	EPI_STAT_S stb;
	int	fh;
#endif

	savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = db;			/* for btreelog debug */

	if(TXsingleuser)
		goto done;
#ifndef NEVER
	if (db->indexes)
	{
		for (i = 0; i < db->nindex; i++)
		{
			btflush(db->indexes[i].btree);
		}
	}
	if (db->dbies)
	{
		for (i = 0; i < db->ndbi; i++)
			TXflush3dbi(db->dbies[i]);
	}
	if (db->fdbies)
	{
		for (i = 0; i < db->nfdbi; i++)
			fdbi_flush(db->fdbies[i]);
	}
	if (db->invidx)
	{
		for (i = 0; i < db->ninv; i++)
			btflush(db->invidx[i].btree);
	}
	goto done;
#endif /* !NEVER */
	closeindexes(db);
	goto done;

	/* WTF */

	if (db->indexes)
	{
		for (i = 0; i < db->nindex; i++)
		{
			btflush(db->indexes[i].btree);
		}
	}
	if (db->dbies)
	{
		for (i = 0; i < db->ndbi; i++)
			TXflush3dbi(db->dbies[i]);
	}
	if (db->fdbies)
	{
		for (i = 0; i < db->nfdbi; i++)
			fdbi_flush(db->fdbies[i]);
	}
	if (db->invidx)
	{
		for (i = 0; i < db->ninv; i++)
			btflush(db->invidx[i].btree);
	}
#ifdef NEVER
/* Touch SYSINDEX, reset our time */
	TXtouchindexfile(db->ddic);	/* Force everyone else to reread */
	fh = getdbffh(db->ddic->indextbl->df);
	if (fh != -1)
	{
		EPI_FSTAT(fh, &stb);
		db->idxtime = stb.st_mtime;
	}
#endif /* NEVER */
done:
	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
}

int
TXcloseFdbiIndexes(DBTBL *dbtbl)
/* Returns 0 on error.
 */
{
	int	i;

	if (dbtbl->fdbies)
	{
		for (i = 0; i < dbtbl->nfdbi; i++)
			closefdbi(dbtbl->fdbies[i]);
		dbtbl->fdbies = TXfree(dbtbl->fdbies);
	}
	dbtbl->fdbiIndexNames = TXfreeStrList(dbtbl->fdbiIndexNames,
					      dbtbl->nfdbi);
	dbtbl->fdbiIndexFldNames = TXfreeStrList(dbtbl->fdbiIndexFldNames,
						 dbtbl->nfdbi);
	dbtbl->fdbiIndexParams = TXfreeStrList(dbtbl->fdbiIndexParams,
					       dbtbl->nfdbi);
	dbtbl->nfdbi = 0;
	return(1);
}

/******************************************************************/
/*	Close all the indexes opened by getindexes
 */

void
closeindexes(db)
DBTBL *db;
{
	int i;
	FLDCMP *fc;
	DBTBL	*savtbl;

	savtbl = TXbtreelog_dbtbl;
	TXbtreelog_dbtbl = db;			/* for btreelog debug */

	db->idxtime = 0;
	if (db->indexes)
	{
		for (i = 0; i < db->nindex; i++)
		{
			fc = db->indexes[i].btree->usr;
			db->indexes[i].btree->usr = TXclosefldcmp(fc);
			closebtree(db->indexes[i].btree);
			closetbl(db->indexes[i].table);
		}
		free (db->indexes);
		db->indexes = (BINDEX *)NULL;
	}
	db->indexNames = TXfreeStrList(db->indexNames, db->nindex);
	db->indexFldNames = TXfreeStrList(db->indexFldNames, db->nindex);
	db->indexParams = TXfreeStrList(db->indexParams, db->nindex);
	db->nindex = 0;
	if (db->dbies)
	{
		for (i = 0; i < db->ndbi; i++)
			close3dbi(db->dbies[i]);
		free (db->dbies);
		db->dbies = (A3DBI **)NULL;
		db->ndbi = 0;
	}
	TXcloseFdbiIndexes(db);
	if (db->invidx)
	{
		for (i = 0; i < db->ninv; i++)
			closebtree(db->invidx[i].btree);
		free (db->invidx);
		db->invidx = (BINVDX *)NULL;
	}
	db->invertedIndexNames = TXfreeStrList(db->invertedIndexNames,
						db->ninv);
	db->invertedIndexFldNames = TXfreeStrList(db->invertedIndexFldNames,
						db->ninv);
	db->invertedIndexParams = TXfreeStrList(db->invertedIndexParams,
						db->ninv);
	db->ninv = 0;

	TXbtreelog_dbtbl = savtbl;		/* for btreelog debug */
}

/******************************************************************/
/*
 *	Update permissions on indices.  This function looks at
 *	the owner and permissions on a table and tries to update
 *	the ownership and permissions on all the associated
 *	indices.  If you are not the owner of the index file then
 *	the update of ownership and permissions will fail.
 */

/* WTF: needs to loop through indexes, and not need them all open
   at the same time. */

void
setindexperms(t)
DBTBL *t;
{
#ifdef unix                         /* MAW 02-15-94 wtf? perms on nt? */
        A3DBI   *dbi = A3DBIPN;
        char    buf[PATH_MAX + 1];
	int     fd, i, nfd;
	EPI_STAT_S stb;
#ifdef NEVER
        int     wo = 0;

	if (t->ndbi == 0 && t->nindex == 0)
		wo = 1;
	if (wo)
		getindexes(t);
#endif
	fd = getdbffh(t->tbl->df);      /* MAW 04-04-94 replace drill */
	if(fd==(-1))
		return;
	EPI_FSTAT(fd, &stb);            /* Find table stat info */
	for (i=0; i < t->nindex; i++)
	{
		nfd = getdbffh(t->indexes[i].btree->dbf);
		if(nfd==(-1))
			continue;
		fchmod(nfd, stb.st_mode);
		fchown(nfd, stb.st_uid, stb.st_gid);
	}
	for (i=0; i < t->ninv; i++)
	{
		nfd = getdbffh(t->invidx[i].btree->dbf);
		if(nfd==(-1))
			continue;
		fchmod(nfd, stb.st_mode);
		fchown(nfd, stb.st_uid, stb.st_gid);
	}
	for (i=0; i < t->ndbi; i++)
	{
            dbi = t->dbies[i];              /* alias it */
            if (dbi->type != INDEX_MM &&
                dbi->type != INDEX_MMCR &&
                dbi->type != INDEX_FULL &&
                dbi->type != INDEX_FULLCR)
              {
		if((nfd = getdbffh(dbi->td->dbf))!=(-1))
		{
			fchmod(nfd, stb.st_mode);
			fchown(nfd, stb.st_uid, stb.st_gid);
		}
		if((nfd = getdbffh(dbi->upd->dbf))!=(-1))
		{
			fchmod(nfd, stb.st_mode);
			fchown(nfd, stb.st_uid, stb.st_gid);
		}
              }
		if((nfd = getdbffh(dbi->del->dbf))!=(-1))
		{
			fchmod(nfd, stb.st_mode);
			fchown(nfd, stb.st_uid, stb.st_gid);
		}
		if((nfd = getdbffh(dbi->newrec->dbf))!=(-1))
		{
			fchmod(nfd, stb.st_mode);
			fchown(nfd, stb.st_uid, stb.st_gid);
		}
                switch (dbi->type)
                  {
                  case INDEX_MM:
                  case INDEX_MMCR:
                  case INDEX_FULL:
                  case INDEX_FULLCR:
                    wtix_setperms(dbi->name, stb.st_mode, stb.st_uid,
                                  stb.st_gid);
                    break;
#ifdef OLD_MM_INDEX
                  default:              /* INDEX_3DB */
                    /* check return val of _openupd3dbi():  -KNG 960501 */
                    if ((dbi->ttbl != TTBLPN &&
                         dbi->ttbl->ti != NULL) ||
                        _openupd3dbi(dbi) != (-1))
                      {
                        if((nfd = getdbffh(dbi->ttbl->bt->dbf))!=(-1))
                          {
                            fchmod(nfd, stb.st_mode);
                            fchown(nfd, stb.st_uid, stb.st_gid);
                          }
                        if((nfd = getdbffh(dbi->ttbl->bdbf))!=(-1))
                          {
                            fchmod(nfd, stb.st_mode);
                            fchown(nfd, stb.st_uid, stb.st_gid);
                          }
                        if((nfd = (dbi->ttbl->ti->md->ifh))!=(-1))
                          {
                            fchmod(nfd, stb.st_mode);
                            fchown(nfd, stb.st_uid, stb.st_gid);
                          }
                        if((nfd = (dbi->ttbl->ti->md->dfh))!=(-1))
                          {
                            fchmod(nfd, stb.st_mode);
                            fchown(nfd, stb.st_uid, stb.st_gid);
                          }
                      }
                    break;
#endif /* OLD_MM_INDEX */
                  }
                if (dbi != A3DBIPN && TXcatpath(buf, dbi->name, "_P.tbl"))
                  {
                    chmod(buf, stb.st_mode);
                    chown(buf, stb.st_uid, stb.st_gid);
                  }
	}
#ifdef NEVER
	if (wo)
		closeindexes(t);
#endif /* NEVER */
#endif  /* unix */
}
