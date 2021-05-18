#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#ifdef _WIN32
#  include <io.h>
#  include <process.h>
#  define chown(a, b, c)
#endif
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include <string.h>
#include "dbquery.h"
#include "jtreadex.h"
#include "texint.h"
#include "fdbi.h"
#include "cgi.h"
#include "txlic.h"


#ifdef _WIN32
#  define link(a,b) rename(a,b)
#else
#  define O_BINARY      0
#endif
#define BTCSIZE TXbtreecache
#define WRDBUFSZ 1024
#ifndef RECIDPN
#  define RECIDPN       ((RECID *)NULL)
#endif
#ifndef DDPN
#  define DDPN          ((DD *)NULL)
#endif
#ifndef TXA2INDPN
#  define TXA2INDPN     ((TXA2IND *)NULL)
#endif

#ifdef _WIN32
extern int fchmod ARGS((int, int));
#else
#ifndef _AIX
extern int fchmod ARGS((int, mode_t));
#endif
#endif
#ifndef STDIN_FILENO
#  define STDIN_FILENO  0
#endif
#ifndef STDOUT_FILENO
#  define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#  define STDERR_FILENO 2
#endif

static int emove ARGS((CONST char *src, CONST char *dest, int flags));

CONST char TXxPidExt[TXxPidExtLen + 1] = "_X.PID";
CONST char TXtempPidExt[TXtempPidExtLen + 1] = ".PID";

/******************************************************************/

#ifdef _WIN32
static int TXCleanupWait = 2;

int TXSetCleanupWait(x)
int x;
{
	TXCleanupWait = x;
        return(0);
}
#endif /* _WIN32 */

/******************************************************************/

int
tx_delindexfile(errlevel, fn, p, flags)
int             errlevel;       /* e.g. MERR or MWARN */
CONST char      *fn;            /* function name */
CONST char      *p;
int             flags;          /* bit 0: no msg/err for EACCES */
/* Removes `p'.
 * Returns 0 (with message) on error, or 1 (silently) on success or not-exist.
 */
{
  TXERRTYPE     errNum;

  TXseterror(0);
  if (unlink(p) != 0)                           /* failed */
    {
      errNum = TXgeterror();
      if (TX_ERROR_IS_NO_SUCH_FILE(errNum)) return(1);/* does not exist: ok */
      if ((flags & 0x1) == 0 || !TX_ERROR_IS_PERM_DENIED(errNum))
        {
          putmsg(errlevel + FDE, fn, "Cannot delete %s: %s",
                 p, TXstrerror(errNum));
          return(0);
        }
    }
  return(1);
}    

/******************************************************************/

static void updind_abendcb ARGS((void *usr));
static void
updind_abendcb(usr)
void    *usr;
/* ABEND callback: deletes temp update index _X/_Y/_Z files and _X.PID file.
 * NOTE:  Called during core dump, etc. so don't try anything fancy.
 */
{
  A3DBI *index = (A3DBI *)usr;
  BTREE *bt[3];
  int   i, fh;
  char  path[PATH_MAX];

  bt[0] = index->mdel;
  bt[1] = index->mupd;
  bt[2] = index->mnew;
  for (i = 0; i < 3; i++)
    {
      if (bt[i] == BTREEPN) continue;
      fh = getdbffh(bt[i]->dbf);
      if (fh > STDERR_FILENO) close(fh);                /* for Windows */
      unlink(getdbffn(bt[i]->dbf));
    }
  if (index->name != CHARPN)
    {
      TXcatpath(path, index->name, TXxPidExt);
      unlink(path);
    }
}

static void tmpind_abendcb ARGS((void *usr));
static void
tmpind_abendcb(usr)
void    *usr;
/* ABEND callback: deletes temp index _C/_D/_T/_P files.
 * NOTE:  Called during core dump, etc. so don't try anything fancy.
 */
{
  A3DBI *index = (A3DBI *)usr;
  BTREE *bt[4];
  int   i, fh;
  char  path[PATH_MAX];

  bt[0] = index->ct;                            /* _C.btr */
  bt[1] = index->del;                           /* _D.btr */
  bt[2] = index->newrec;                        /* _T.btr */
  bt[3] = BTREEPN;
  for (i = 0; i < 4; i++)
    {
      if (bt[i] == BTREEPN) continue;
      fh = getdbffh(bt[i]->dbf);
      if (fh > 3) close(fh);                    /* for Windows */
      unlink(getdbffn(bt[i]->dbf));
    }
  if (index->name != CHARPN)
    {
      TXcatpath(path, index->name, "_P.tbl");
      unlink(path);
    }
}

static void closedeltmpind ARGS((A3DBI *index));
static void
closedeltmpind(index)
A3DBI   *index;
{
  static CONST char     fn[] = "closedeltmpind";
  char                  buf[PATH_MAX];

  index->mdel = closebtree(index->mdel);
  TXcatpath(buf, index->name, "_X.btr");
  tx_delindexfile(MERR, fn, buf, 0);
  index->mupd = closebtree(index->mupd);
  TXcatpath(buf, index->name, "_Y.btr");
  tx_delindexfile(MERR, fn, buf, 0);
  index->mnew = closebtree(index->mnew);
  TXcatpath(buf, index->name, "_Z.btr");
  tx_delindexfile(MERR, fn, buf, 0);
}

static void
TXa3dbiCloseOpenFiles(A3DBI *dbi)
{
  dbi->del = closebtree(dbi->del);
  dbi->upd = closebtree(dbi->upd);
  dbi->newrec = closebtree(dbi->newrec);
  dbi->mdel = closebtree(dbi->mdel);
  dbi->mupd = closebtree(dbi->mupd);
  dbi->mnew = closebtree(dbi->mnew);
}

/******************************************************************/

static int createtmpind ARGS((A3DBI *index));
static int
createtmpind(index)
A3DBI   *index;
/* Creates temporary BTREEs _X, _Y and _Z, and _X.PID file.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "createtmpind";
  static CONST char     cantmake[] = "Unable to create temp index %s";
  EPI_STAT_S            stb;
  FILE                  *tf;
  int                   ret, stok;
  char                  buf[PATH_MAX];

  stok = (EPI_FSTAT(getdbffh(index->newrec->dbf), &stb) == 0);
  
  TXcatpath(buf, index->name, "_Z");
  index->mnew = openbtree(buf, index->newrec->order, BTCSIZE, index->newrec->flags, O_RDWR | O_CREAT | O_EXCL);
  if (index->mnew == BTREEPN)
    {
      putmsg(MWARN + FOE, fn, cantmake, buf);
      goto err;
    }
  /* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
  if(btreegetdd(index->newrec))
    btreesetdd(index->mnew, btreegetdd(index->newrec));
  btflush(index->mnew);                 /* KNG 000217 so others see the dd */
#ifdef unix  /* wtf nt perms */
  if (stok)
    {
      fchmod(getdbffh(index->mnew->dbf), stb.st_mode);
      fchown(getdbffh(index->mnew->dbf), stb.st_uid, stb.st_gid);
    }
#endif

  TXcatpath(buf, index->name, TXxPidExt);
  errno = 0;
  if ((tf = fopen(buf, "wb")) == FILEPN)
    {
      putmsg(MERR + FOE, fn, "Cannot open %s: %s", buf, strerror(errno));
      goto err;
    }
  fprintf(tf, "%d", TXgetpid(0));
  fclose(tf);

  if (index->type != INDEX_MM && index->type != INDEX_FULL)
    {
      TXcatpath(buf, index->name, "_Y");
      index->mupd = openbtree(buf, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
      if (index->mupd == BTREEPN)
        {
          putmsg(MWARN + FOE, fn, cantmake, buf);
          goto err;
        }
#ifdef unix  /* wtf nt perms */
      if (stok)
        {
          fchmod(getdbffh(index->mupd->dbf), stb.st_mode);
          fchown(getdbffh(index->mupd->dbf), stb.st_uid, stb.st_gid);
        }
#endif
    }

  TXcatpath(buf, index->name, "_X");
  index->mdel = openbtree(buf, BTFSIZE, BTCSIZE, BT_UNIQUE | BT_FIXED, O_RDWR | O_CREAT | O_EXCL);
  if (index->mdel == BTREEPN)
    {
      putmsg(MWARN + FOE, fn, cantmake, buf);
      goto err;
    }
  /* no bttexttoparam() call: SYSINDEX.PARAMS belongs to WTIX/FDBI */
  btflush(index->mdel);                 /* KNG 000217 so others see it */
#ifdef unix  /* wtf nt perms */
  if (stok)
    {
      fchmod(getdbffh(index->mdel->dbf), stb.st_mode);
      fchown(getdbffh(index->mdel->dbf), stb.st_uid, stb.st_gid);
    }
#endif
  ret = 1;
  goto done;

err:
  closedeltmpind(index);
  ret = 0;
done:
  return(ret);
}

int
TXcreateTempIndexOrTableEntry(ddic, dir, logicalName, parentTblName,
                              indexFldNames, numTblFlds, flags, remark,
                              sysindexParams, createdPath, addedRow)
DDIC            *ddic;          /* (in) DDIC */
CONST char      *dir;           /* (in) directory to create temp in */
CONST char      *logicalName;   /* (in) logical name of index/table */
CONST char      *parentTblName; /* (in) name of parent table (iff index) */
CONST char      *indexFldNames; /* (in) field names (iff index) */
int             numTblFlds;     /* (in) `logicalName' # fields (iff table) */
int             flags;          /* (in) bit flags */
CONST char      *remark;        /* (in) REMARK (iff table) */
CONST char      *sysindexParams;/* (in, opt.) SYSINDEX.PARAMS (iff index) */
char            **createdPath;  /* (out) temp path created (sans extension) */
RECID           *addedRow;      /* (out, opt.) recid of entry (if added) */
/* Creates a temporary index or table name in `dir' for `logicalName',
 * and adds to SYSINDEX/SYSTABLES.  Returns path created (sans extension)
 * in `*createdPath'.  Also creates `*createdPath'.PID file (on success) if
 * Windows or is-table or rebuild, and `*createdPath' PID file  if index and
 * Windows.  Also returns SYSINDEX/SYSTABLES recid created in `*addedRow'.
 * `flags':
 * 0x01: is table (not index)
 * 0x02: for rebuild (not update/optimize)
 * 0x04: unused for now
 * 0x08: yap and fail if temp (or creating) entry exists for `logicalName'
 * 0x10: unique (if index)
 * 0x20: descending (if index)
 * Returns 1 on success, 0 on error.
 */
{
  static CONST char     fn[] = "TXcreateTempIndexOrTableEntry";
  static CONST char     cannotCreate[] = "Cannot create %s: %s";
  TBL                   *sysTbl;
  CONST char            *sysTblFileFldName, *objTypeName, *sysTblName;
  int                   sysTblIdx, tries, fileFound, createdOldPid = 0;
  int                   createdTempPid = 0, ret, sysTblIsLocked = 0;
  FLD                   *sysTblFileFld, *sysTblNameFld, *sysTblTypeFld;
  char                  *tempPath = CHARPN, *s;
  size_t                sz, dbLen;
  FILE                  *fp;
  RECID                 insertedRow;
#ifdef _WIN32
  EPI_STAT_S            st;
#endif /* _WIN32 */
  char                  *sysTblFileValue;
  char                  scratchPath[PATH_MAX];

  TXsetrecid(&insertedRow, RECID_INVALID);
  if (flags & 0x1)                              /* table */
    {
      objTypeName = "table";
      sysTblIdx = SYSTBL_TABLES;
      sysTbl = ddic->tabletbl;
      sysTblName = "SYSTABLES";
      sysTblFileFldName = "WHAT";
    }
  else                                          /* index */
    {
      objTypeName = "index";
      sysTblIdx = SYSTBL_INDEX;
      sysTbl = ddic->indextbl;
      sysTblName = "SYSINDEX";
      sysTblFileFldName = "FNAME";
    }
  if (sysTbl == TBLPN ||
      (sysTblNameFld = nametofld(sysTbl, "NAME")) == FLDPN ||
      (sysTblTypeFld = nametofld(sysTbl, "TYPE")) == FLDPN ||
      (sysTblFileFld = nametofld(sysTbl, (char *)sysTblFileFldName)) == FLDPN)
    {
      putmsg(MERR, fn, "Cannot get %s %s table or fields",
             ddic->epname, sysTblName);
      goto err;
    }

  /* Keep `sysTbl' write-locked for duration of all checks and
   * insertion, for atomicity:  KNG/JMT 20101122
   */
  if (TXlocksystbl(ddic, sysTblIdx, W_LCK, NULL) == -1) goto err;
  sysTblIsLocked++;

  for (tries = 0, fileFound = 1; tries < 25 && fileFound; tries++)
    {
      /* KNG 20051206 Use .btr ext and then truncate it; see below: */
      tempPath = TXtempnam(dir, CHARPN,
                           ((flags & 0x1) ? ".tbl" : TX_BTREE_SUFFIX));
      if (tempPath == CHARPN) goto err;
      tempPath[strlen(tempPath) - 4] = '\0';      /* strip .btr extension */
#ifdef _WIN32
      /* check for old-style no-extension placeholder just in case: */
      if (!(flags & 0x1) && EPI_STAT(tempPath, &st) == 0)
        {                                       /* found it */
          tempPath = TXfree(tempPath);
          continue;
        }
#endif /* _WIN32 */

      /* KNG 20060103 Make sure proposed filename is not in use in any
       * way in SYSINDEX/SYSTABLES, to avoid collisions.  Under
       * Windows (or ALTER INDEX REBUILD), TXtransferIndexOrTable()
       * attempts to rename this filename to something similar to
       * index/table logical name.  But that might fail, and
       * TXtransferIndexOrTable() could thus leave this filename live
       * in SYS..., so ensure uniqueness:
       */
      rewindtbl(sysTbl);
      fileFound = 0;
      while (!fileFound && TXrecidvalid(gettblrow(sysTbl, NULL)))
        {
          s = (char *)getfld(sysTblFileFld, &sz);
          fileFound =
            (TXpathcmp(TXbasename(s), -1, TXbasename(tempPath), -1) == 0);
          /* If requested, make sure no temp or creating entries exist: */
          if (flags & 0x8)
            {
              s = (char *)getfld(sysTblNameFld, &sz);
              if (s && strcmp(s, logicalName) == 0)
                {
                  s = (char *)getfld(sysTblTypeFld, &sz);
                  if (flags & 0x1)              /* table */
                    switch (s ? *(byte *)s : 0)
                      {
                      case TEXIS_TEMP_TABLE:
                        putmsg(MERR + FOE, NULL,
                               "Table %s appears to be being compacted",
                               logicalName);
                        goto err;
                      }
                  else                          /* index */
                    switch (s ? *(byte *)s : 0)
                      {
                      case INDEX_3CR:
                      case INDEX_MMCR:
                      case INDEX_FULLCR:
                      case INDEX_CR:
                        putmsg(MERR + FOE, NULL,
                               "Index %s appears to be being created",
                               logicalName);
                        goto err;
                      case INDEX_TEMP:
                        putmsg(MERR + FOE, NULL,
                            "Index %s appears to be being updated or rebuilt",
                               logicalName);
                        goto err;
                      }
                }
            }
        }
      if (fileFound)
        {
          TX_INDEXDEBUG_MSG((999, fn,
           "(%u) Proposed new %s temp %s name %s already in %s, trying again",
                             (unsigned)TXgetpid(0), objTypeName, ddic->epname,
                             tempPath, sysTblName));
          tempPath = TXfree(tempPath);
        }
    }
  if (fileFound || tempPath == CHARPN)
    {
      putmsg(MERR + FOE, fn,
             "Cannot create unique temp filename for %s %s in %s",
             objTypeName, logicalName, ddic->epname);
      goto err;
    }
  else
    {
      TX_INDEXDEBUG_MSG((999, fn,
                "(%u) Proposed new %s temp %s name %s not in %s, will use it",
                         (unsigned)TXgetpid(0), ddic->epname, objTypeName,
                         tempPath, sysTblName));
    }

#ifdef _WIN32
  /* KNG 20051206 This file was apparently created as a placeholder to
   * help TXtempnam() avoid collisions at the next index update,
   * because Windows recycles PIDs often, and we called TXtempnam()
   * with no extension.  Now we call TXtempnam() above with a .btr
   * extension, since all index types have that file live, so this
   * file should not be needed (hence we do not do it for temp tables,
   * which are a more recent (20101028) addition), but keep it in case
   * old process updates:
   */
  if (!(flags & 0x1))                           /* index */
    {
      fp = fopen(tempPath, "wb");
      if (fp == FILEPN)
        {
          putmsg(MERR + FOE, CHARPN, cannotCreate,
                 tempPath, TXstrerror(TXgeterror()));
          goto err;
        }
      createdOldPid = 1;
      fprintf(fp, "%d", TXgetpid(0));
      fclose(fp);
      fp = FILEPN;
    }
#endif /* _WIN32 */

  /* The .PID file lets other processes know that the creator and user
   * of this INDEX_TEMP/TEXIS_TEMP_TABLE is still alive, so they
   * should not remove it when calling TXdocleanup().  Only for
   * Windows indexes (and all platforms' tables, because we are slowly
   * migrating all platforms' TXtransferIndexOrTable() actions to
   * Windows-style temp-and-del-SYSINDEX/SYSTABLE-entry-flip; Bug 3231).
   * Also for all platforms' ALTER INDEX REBUILD:
   */
#ifndef _WIN32
  if (flags & 0x3)                              /* table, or index rebuild */
#endif /* !_WIN32 */
    {
      TXcatpath(scratchPath, tempPath, TXtempPidExt);
      fp = fopen(scratchPath, "wb");
      if (fp == FILEPN)
        {
          putmsg(MERR + FOE, CHARPN, cannotCreate,
                 scratchPath, TXstrerror(TXgeterror()));
          goto err;
        }
      createdTempPid = 1;
      fprintf(fp, "%d", TXgetpid(0));
      fclose(fp);
      fp = FILEPN;
    }

  /* For Windows indexes (and all tables, and all ALTER INDEX REBUILDs),
   * add a temp (INDEX_TEMP/TEXIS_TEMP_TABLE) entry for the new index/table.
   * This entry will be made live at TXtransferIndexOrTable(), since
   * we cannot delete the old files (may still be in use) then.
   * KNG 20050328 Truncate the SYS... file column to just the file
   * (db-relative) if it is in the db; allows db rename/move ala Unix:
   * KNG 20051202 also trim to db-relative if a subdir of db:
   */
  dbLen = strlen(ddic->pname);                  /* db dir + '/' */
  if (TXpathcmp(tempPath, dbLen, ddic->pname, dbLen) == 0)
    sysTblFileValue = tempPath + dbLen;         /* db-relative */
  else
    sysTblFileValue = tempPath;                 /* absolute */
  if (flags & 0x1)                              /* table */
    {
      if (!TXaddtablerec(ddic, logicalName, TXgetusername(ddic),
                         remark, sysTblFileValue, numTblFlds,
                         TEXIS_TEMP_TABLE, &insertedRow))
        goto err;
    }
  else                                          /* index */
    {
      /* Only Windows (and all platforms' ALTER INDEX REBUILDs) make
       * SYSINDEX INDEX_TEMP entries, until Bug 3231 is implemented:
       */
#ifndef _WIN32
      if (flags & 0x2)                          /* rebuild */
#endif /* !_WIN32 */
        {
          if (!TXaddindexrec(ddic, (char *)logicalName, (char *)parentTblName,
                             sysTblFileValue,
			     ((flags & 0x20) ? COLL_DESC : COLL_ASC),
                             ((flags & 0x10) ? 1 : 0),
                             (char *)indexFldNames, INDEX_TEMP,
                             (char *)(sysindexParams ? sysindexParams : ""),
                             &insertedRow))
            goto err;
        }
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;                                      /* error */
  if (tempPath != CHARPN)
    {
      if (createdTempPid)
        {
          TXcatpath(scratchPath, tempPath, TXtempPidExt);
          tx_delindexfile(MERR, fn, scratchPath, 0);
          createdTempPid = 0;
        }
      if (createdOldPid)
        {
          tx_delindexfile(MERR, fn, tempPath, 0);
          createdOldPid = 0;
        }
      tempPath = TXfree(tempPath);
    }
done:
  if (sysTblIsLocked)
    {
      TXunlocksystbl(ddic, sysTblIdx, W_LCK);
      sysTblIsLocked--;
    }
  *createdPath = tempPath;
  if (addedRow) *addedRow = insertedRow;
  return(ret);
}

/******************************************************************/

static char *makeindex ARGS((char *newIndexDir, A3DBI *dbi, DBTBL *dbtb,
         FLD *primaryField, size_t blockmax, char *iname,
         char *primaryFieldName, WTIX **wxp, char *sysindexFields,
	 TXMMPARAMTBLINFO *paramTblInfo, int flags, TXindOpts *options));

static char *
makeindex(newIndexDir, dbi, dbtb, primaryField, blockmax, iname,
	  primaryFieldName, wxp, sysindexFields, paramTblInfo, flags, options)
char *newIndexDir;	/* (in) Path to create new index in */
A3DBI *dbi;		/* (in) Old index */
DBTBL *dbtb;		/* (in) Table containing the field to be indexed */
FLD *primaryField;	/* (in) Primary (text) field being indexed */
size_t blockmax;	/* (in) Maximum block size */
char *iname;		/* (in) Index name */
char *primaryFieldName;	/* (in) Primary (text) field name */
WTIX    **wxp;          /* (in/out) WTIX object (if INDEX_MM or INDEX_FULL) */
char *sysindexFields;	/* (in) SYSINDEX.FIELDS value */
TXMMPARAMTBLINFO *paramTblInfo; /* (out) _P.tbl info */
int	flags;		/* (in) bit flags:  0x2: rebuild */
TXindOpts	*options;
/* Creates "new" (updated/optimized) Metamorph index.
 * Returns path to new (temp) index on success, or NULL on error.
 */
{
	static CONST char Fn[]="makeindex";
        TXA2IND *a2i = TXA2INDPN;
        BINDEX  bi;
	FREADX frx;
	FLD	*resFld = FLDPN, *cnvFld = FLDPN;
	FLDOP	*fldOp = FLDOPPN;
	ulong off, len;
        RECID       cat;
        DD      *auxdd;
	int     found = 0, delidx = 0, res;
	int	reduceVirtualFldMem = 0;
	byte	*auxbuf = BYTEPN;
	size_t  sz, auxbufSz, n;
	void    *v = NULL;
	char    *newIndexPath = CHARPN;
	char    fname[PATH_MAX];
	A3DBI   *newIndexDbi = A3DBIPN;
	BTLOC   btloc, *at;
	RECID	recid;
	BTREE   *ct = BTREEPN;
	byte    primaryFieldType;
	TBL	*tb = dbtb->tbl;
	DDIC	*ddic = dbtb->ddic;
	RECID	addindexrow;
	char	*tbname = dbtb->lname;
	int	didpid = 0;
	FFS	*ex;
	uchar	*buf = NULL;
        BTLOC   (*getnext) ARGS((void *obj, size_t *len, void *x));
        void    *obj;
        char    **noise;
        int     wrongsz = 0;
        size_t  a2isz;
        EPI_STAT_S st;
	ft_blobi	*blobi = NULL;
        char    sysindexParams[MM_INDEX_PARAMS_TEXT_MAXSZ];

	TXsetrecid(&addindexrow, RECID_INVALID);
	TX_INIT_TXMMPARAMTBLINFO(paramTblInfo);

	memset(&bi, 0, sizeof(BINDEX));

	if (dbi->type == INDEX_3DB)		/* old metamorph */
	{
		putmsg(MERR + UGE, Fn, "Unsupported type %c for index %s",
		       (int)dbi->type, dbi->name);
		goto err;
	}

	if (dbi->auxsz > 0)
		auxbuf = TXmalloc(TXPMBUFPN, Fn, dbi->auxsz);
	else
		auxbuf = TXmalloc(TXPMBUFPN, Fn, sizeof(EPI_OFF_T));
	if (!auxbuf) goto err;
	buf = (byte *)TXcalloc(TXPMBUFPN, Fn, 1, blockmax);
	if (!buf) goto err;

        /* Generate a `Tnnn' temp file name in the desired path,
         * create `Tnnn.PID' file for it, and add to SYSINDEX:
         */
        if (TX3dbiParamsToText(sysindexParams, sizeof(sysindexParams), dbi)
            >= sizeof(sysindexParams))
          {
            putmsg(MERR + MAE, Fn,
               "SYSINDEX.PARAMS value too large for update of index %s in %s",
                   iname, ddic->epname);
            goto err;
          }

        res = TXcreateTempIndexOrTableEntry(ddic, newIndexDir, iname, tbname,
                                            sysindexFields, 0,
                                            ((flags & 0x2) | 0x8),
                                            CHARPN, sysindexParams,
                                            &newIndexPath, &addindexrow);
        if (!res) goto err;
#ifdef _WIN32
        didpid = 2;                             /* 2 pid files created */
#else /* !_WIN32 */
	if (flags & 0x2) didpid = 2;		/* 2 pid files created */
#endif /* !_WIN32 */

        if (dbi->noiselist)
          noise = dbi->noiselist;
        else
          noise = (char **)globalcp->noise;

/* Create a scratch 3DB index */
        /* INDEX_FULL only needs this for the _C tree:   -KNG 971117 */
	/* KNG 20120604 but for ALTER INDEX ... REBUILD we want a
	 * complete _P.tbl etc. based on existing `dbi':
	 */
	newIndexDbi = TXcreate3dbiForIndexUpdate(newIndexPath, dbi,
						 (flags & 0x2));
	if (newIndexDbi == (A3DBI *)NULL) goto err;
	/* WTF save pre-existing files at TXdelindex() below, e.g. if
	 * TXtempnam() collision:
	 */
	delidx = 1;

        TXaddabendcb(tmpind_abendcb, newIndexDbi);
	newIndexDbi->mm = closemmtbl(newIndexDbi->mm);

	if (flags & 0x2)			/* rebuild */
	{
		TXfdbiIndOpts	fdbiOptions;

		/* Maintain same options (word expressions etc.) as
		 * original index; init from DBI:
		 */
		TXfdbiIndOpts_INIT_FROM_DBI(&fdbiOptions, newIndexDbi);
		/* But index version can change: */
		fdbiOptions.indexVersion = options->fdbiVersion;
		*wxp = openwtix(dbtb, primaryFieldName, newIndexPath,
				newIndexDbi->auxsz, &fdbiOptions, NULL,
			   (newIndexDbi->type == INDEX_FULL ? WTIXF_FULL : 0),
				(int)newIndexDbi->version, WTIXPN);
		if (!*wxp) goto err;
	}
	else if (!wtix_setupdname(*wxp, newIndexPath))
		goto err;
	if (!wtix_setnoiselist(*wxp, noise))
          goto err;
	delidx = 1;				/* WTF see caveats above */
        getnext = (BTLOC (*) ARGS((void *, size_t *, void *)))wtix_getnextnew;
        obj = *wxp;

	/* Establish LIKEIN counts B-tree, if needed: */
	if (flags & 0x2)			/* rebuilding */
		/* Rebuilding: can output direct to new index's _C.btr: */
		ct = newIndexDbi->ct;
	else if (dbi->ct)			/* counter index */
		/* Updating: use a RAM B-tree, merge with old index later: */
		ct = openbtree(NULL, 500, 20, BT_FIXED,
			       (O_RDWR | O_CREAT | O_EXCL));

	ex = openrex((byte *)"$", TXrexSyntax_Rex);

	primaryFieldType = (primaryField->type & DDTYPEBITS);
	if(!dbi->auxsz)
		auxbufSz = sizeof(EPI_OFF_T);
	else
		auxbufSz = dbi->auxsz;
	if (flags & 0x2)			/* rebuilding */
	{
		TXsetrecid(&btloc, (EPI_OFF_T)(-1));	/* get-next-row */
		rewindtbl(dbtb->tbl);
	}
	else
		btloc = getnext(obj, &auxbufSz, (void *)auxbuf);

        /* While getnext() gives us the aux data from the new list,
         * it could be corrupt/out-of-date (index bugs, etc.).
         * So get the real McCoy from the table.  This is derived
         * from init3dbia2ind():  KNG 000315
         */
        if (dbi->auxsz > 0)
          {
            bi.btree = dbi->newrec;           /* WTF locks? */
            if ((auxdd = btreegetdd(bi.btree)) == DDPN) goto err1;
            if ((bi.table = createtbl(auxdd, NULL)) == TBLPN) goto err1;
            if ((a2i = TXadd2indsetup(dbtb, &bi)) == TXA2INDPN) goto err1;
          }

	reduceVirtualFldMem =
		(ddic->optimizations[OPTIMIZE_INDEX_VIRTUAL_FIELDS] &&
		 primaryField &&
		 FLD_IS_COMPUTED(primaryField) &&
		 /* index types we know use `v'/`primaryField' safely: */
		 (dbi->type == INDEX_MM ||
		  dbi->type == INDEX_FULL));

	/* If rebuilding, we do not have a valid `btloc' yet, as
	 * we only get it when reading the next table row, instead
	 * of reading a *specific* (`btloc') row as when updating:
	 */
	while ((flags & 0x2) || TXrecidvalid(&btloc))
	{
                recid = btloc;
		if(TXlocktable(dbtb, R_LCK)==-1)
		{
                err1:
			buf = TXfree(buf);
                        TXdelabendcb(tmpind_abendcb, newIndexDbi);
			if (ct && (!newIndexDbi || ct != newIndexDbi->ct))
				ct = closebtree(ct);
			newIndexDbi=close3dbi(newIndexDbi);
                        goto err;
		}
#ifdef ASSERT_DEL_BLOCK
		if (!validrow(tb, &recid))
		{
			TXunlocktable(dbtb, R_LCK);
                        goto addmt;
		}
#endif
		at = gettblrow(tb, &recid);
		TXunlocktable(dbtb, R_LCK);
		if (!TXrecidvalid(at))
		{
			if (flags & 0x2) break;	/* EOF */
			goto addmt;
		}
		if (flags & 0x2)		/* rebuilding */
			btloc = recid = *at;

		blobi = NULL;
		v = getfld(primaryField, &sz);
		if (!v) goto addmt;

                if (dbi->auxsz > 0)
                  {
                    /* Copy the real McCoy aux data from the table,
                     * in case the new list's is corrupt:
                     */
                    if ((a2isz = TXa2i_setbuf(a2i)) != (size_t)dbi->auxsz)
                      {
                        if (++wrongsz <= 3)     /* do not yap too much */
                          {
                            putmsg(MWARN, Fn,
                                   "Wrong aux size from TXa2i_setbuf() for recid 0x%wx from index %s: Got %d bytes, expected %d; will get aux data from new list instead",
                                   (EPI_HUGEINT)TXgetoff(&btloc),
                                   getdbffn(dbi->newrec->dbf),
                                   (int)a2isz, (int)dbi->auxsz);
                            /* Leave new-list aux data in place.
                             * Could be corrupt, but it is all we have.
                             */
			    /* If rebuilding index, there is no new list;
			     * must fail?:
			     */
			    if (flags & 0x2)	/* rebuilding */
			    {
				    putmsg(MERR, Fn,
					   "Cannot recover from TXa2i_setbuf() error for index %s: New list not used during index rebuilds",
					   iname);
				    goto err;
			    }
                          }
                      }
                    else
                      memcpy(auxbuf, a2i->tbl->orec, dbi->auxsz);
                  }

		/* Free up some mem if possible, now that we are done with
		 * current `dbtb' row's native/KDBF data: `primaryField' is
		 * virtualized, *and* we copied aux data from the
		 * table row (Bug 4156).  Only do this on large rows,
		 * to avoid constant KDBF re-alloc:
		 */
		if (reduceVirtualFldMem &&
		    /* Check actual DBF row size, not field size:
		     * latter may be large when former is small
		     * (e.g. indirects):
		     */
		    TXtblGetRowSize(tb) > TX_INDEX_MAX_SAVE_BUF_LEN)
			TXtblReleaseRow(tb);

		switch (primaryFieldType)
		{
		case FTN_INDIRECT:
                        /* WTF WTF WTF this should go away: not needed
                         * (dark secret gone) and long != EPI_OFF_T:  KNG 981001
                         */
#ifdef NEVER
			putmsg(MFILEINFO,(char *)NULL, "Adding file %s", v);
#endif
                        errno = 0;
			if (*(char *)v == '\0')
			   goto addmt;
			frx.fh = fopen(v, "rb");
			if (frx.fh == (FILE *)NULL)
			{
                          if (*(char *)v != '\0')       /* shut up if empty */
                            putmsg(MERR + FOE, Fn,
                                   "Cannot open indirect file %s: %s",
                                   v, strerror(errno));
                        addmt:
                          /* We must add _every_ wtix_getnextnew()
                           * record via wtix_insert(), or
                           * tokens/recids get out of sync; KNG 980713
                           */
			  if (!wtix_insert(*wxp, "", 0, auxbuf, btloc))
				  goto err;
                          goto cont;
			}
			frx.buf = buf;
			frx.tailsz = 0;
			frx.len = blockmax;
			frx.ex = ex;
			off = 0;
			len = filereadex(&frx);
			while (len > 0)
			{
				if (!wtix_insert(*wxp, frx.buf, len, auxbuf,btloc))
				      goto err;
				off += len;
				len = filereadex(&frx);
			}
			fclose(frx.fh);
			break;
		case FTN_STRLST:
			/* Do not index the ft_strlst header, just the data,
			 * which is nul-term. strings regardless of delimiter:
			 * KNG 20080319
			 */
			n = TX_STRLST_MINSZ;	/* skip the header */
			if (n > sz) n = sz;	/* but only if room */
			v = (byte *)v + n;
			sz -= n;
			goto indexChar;
		case FTN_BLOBI:
			v = TXblobiGetPayload(blobi = (ft_blobi *)v, &sz);
			if (!v) goto addmt;
			/* fall through: */
		case FTN_CHAR:
		case FTN_BYTE:
		indexChar:
#ifdef NO_EMPTY_LIKEIN
                    TXsetrecid(&cat, (EPI_OFF_T)TXcountterms((char *)v));
#endif
#ifndef NO_EMPTY_LIKEIN
			if (ct)
			{
				int	numTerms;

				numTerms = TXinsertMetamorphCounterIndexRow(v,
							 auxbuf, btloc, *wxp);
				if (numTerms < 0) goto err;
				TXsetrecid(&cat, numTerms);
			}
			else
#endif
                        if (!wtix_insert(*wxp, v, sz, auxbuf, btloc))
                          goto err;
			if (ct) btinsert(ct, &cat, sizeof(btloc), &btloc);
			break;
		default:
			/* Convert to varchar: */
			if (fldOp == FLDOPPN &&
			    (fldOp = dbgetfo()) == FLDOPPN)
			{
				putmsg(MERR + MAE, Fn, "Cannot open FLDOP");
				goto err;
			}
			if (cnvFld == FLDPN &&
			    (cnvFld = createfld("varchar",1,0)) == FLDPN)
			{
				putmsg(MERR + MAE, Fn, "Cannot open FLD");
				goto err;
			}
			putfld(cnvFld, "", 0);
			if (fopush(fldOp, primaryField) != 0 ||
			    fopush(fldOp, cnvFld) != 0 ||
			    foop(fldOp, FOP_CNV) != 0 ||
			    (resFld = fopop(fldOp)) == FLDPN)
			{
				putmsg(MERR, Fn,
    "Cannot convert index field type %s to varchar for Metamorph index %s",
				       ddfttypename(primaryFieldType), iname);
				goto err;
			}
			v = getfld(resFld, &sz);
			goto indexChar;
		}

		/* Done with current `primaryField'/`v' value; free ASAP
		 * to maybe save mem at next row read:
		 */
		if (blobi) TXblobiFreeMem(blobi);	/* before free(v) */
		if (reduceVirtualFldMem && sz > TX_INDEX_MAX_SAVE_BUF_LEN)
		{
			setfldandsize(primaryField, NULL, 0, FLD_KEEP_KIND);
			v = NULL;
			sz = 0;
		}

		found ++;
              cont:
		if (flags & 0x2)		/* rebuilding */
			TXsetrecid(&btloc, -1);	/* get-next-row */
		else
		{
			if(dbi->auxsz)
				auxbufSz = dbi->auxsz;
			else
				auxbufSz = sizeof(EPI_OFF_T);
			btloc = getnext(obj, &auxbufSz, (void *)auxbuf);
		}
	}
	if(ex)
		ex = closerex(ex);
	buf = TXfree(buf);

	/* Get the table size at completion of new-data indexing, before
	 * final merge.  This will be saved to the param table later:
	 * KNG 000417
	 */
        errno = 0;
        if (EPI_FSTAT(getdbffh(dbtb->tbl->df), &st) != 0)
          putmsg(MWARN + FTE, Fn, "Cannot stat %s: %s",
                 getdbffn(dbtb->tbl->df), strerror(errno));
        else
	  paramTblInfo->originalTableSize = (EPI_OFF_T)st.st_size;

        if (!wtix_finish(*wxp)) goto err;
        TXwtixGetTotalHits(*wxp, paramTblInfo);
        if (!(flags & 0x2) &&			/* not rebuilding */
	    newIndexDbi->ct != BTREEPN &&
	    dbi->ct != BTREEPN &&
            !wtix_mergeclst(*wxp, newIndexDbi->ct, dbi->ct, ct))
          goto err;

  TXdelabendcb(tmpind_abendcb, newIndexDbi);
  if (ct && (!newIndexDbi || ct != newIndexDbi->ct))
	  ct = closebtree(ct);
  newIndexDbi = close3dbi(newIndexDbi);
  auxbuf = TXfree(auxbuf);
  goto done;

err:
  if (newIndexDbi != A3DBIPN)
    {
      TXdelabendcb(tmpind_abendcb, newIndexDbi);
      newIndexDbi = close3dbi(newIndexDbi);
    }
  if (newIndexPath != CHARPN)
    {
      if (wxp != WTIXPPN) *wxp = closewtix(*wxp);       /* before B-tree del */
      if (delidx)
        TXdelindex(newIndexPath, INDEX_TEMP);
      else if (didpid)
	{
          if (didpid >= 2)
            {
              TXcatpath(fname, newIndexPath, TXtempPidExt);
              tx_delindexfile(MERR, Fn, fname, 0);
            }
          tx_delindexfile(MERR, Fn, newIndexPath, 0);
        }
      newIndexPath = TXfree(newIndexPath);
    }
  if (TXrecidvalid(&addindexrow)) TXdelindexrec(ddic, addindexrow);
  auxbuf = TXfree(auxbuf);
done:
  if (wrongsz > 3)
    putmsg(MERR, Fn, "%kd TXa2i_setbuf() failures from index %s",
           (int)wrongsz, getdbffn(dbi->newrec->dbf));
  if (a2i != TXA2INDPN) TXadd2indcleanup(a2i);
  if (bi.table != TBLPN) closetbl(bi.table);
  if (resFld != FLDPN) closefld(resFld);
  if (cnvFld != FLDPN) closefld(cnvFld);
  if (fldOp != FLDOPPN) foclose(fldOp);
  return(newIndexPath);
}

/******************************************************************/

static int
emove(src, dest, flags)
CONST char	*dest;
CONST char	*src;
int             flags;
/* Moves `src' to `dest'.
 * `flags' bits:
 *   bit 0:  no msg/err if `src' cannot be removed due to EACCES
 * Returns -1 on error.
 */
{
  static CONST char     fn[] = "emove";
  EPI_STAT_S            stdest, stsrc;
  int                   fdest, fsrc, nread, nra, nwrite, srcexists;
  int                   destexists, er;
  byte                  buf[(1 << 16)];

  destexists = (EPI_STAT(dest, &stdest) != -1);
  srcexists = (EPI_STAT(src, &stsrc) != -1);
  if (destexists && !tx_delindexfile(MERR, fn, dest, 0)) return(-1);
  if (!srcexists) return(0);                    /* nothing to move */
  if (!destexists) stdest.st_mode = 0600;
  if (link(src, dest) == -1)                    /* cannot link: copy it */
    {
      errno = 0;
      fdest = open(dest, (O_WRONLY|O_CREAT|O_TRUNC|O_BINARY), stdest.st_mode);
      if (fdest == -1)
        {
          putmsg(MERR + FME, fn, "Cannot create %s: %s",
                 dest, strerror(errno));
          return(-1);
        }
      errno = 0;
      fsrc = open(src, (O_RDONLY | O_BINARY), 0666);
      if (fsrc == -1)
        {
          putmsg(MERR + FOE, fn, "Cannot open %s: %s", src, strerror(errno));
          close(fdest);
          return(-1);
        }
      er = 0;
#ifdef _WIN32
      stsrc.st_size = (EPI_OFF_T)_filelength(fsrc);
#endif /* _WIN32 */
      while (stsrc.st_size > (EPI_OFF_T)0)
        {
          nread = (stsrc.st_size > (EPI_OFF_T)sizeof(buf) ? (int)sizeof(buf) :
                   (int)stsrc.st_size);
          nra = tx_rawread(TXPMBUFPN, fsrc, src, buf, nread, 1);
          if (nra != nread)
            {
              er = 1;
              break;
            }
          nwrite = (int)tx_rawwrite(TXPMBUFPN, fdest, dest, TXbool_False,
                                    buf, nra, TXbool_False);
          if (nwrite != nra)
            {
              er = 1;
              break;
            }
          stsrc.st_size -= (EPI_OFF_T)nread;
        }
      TXclearError();
      if (close(fdest) != 0)
	putmsg(MERR + FCE, fn, "Cannot close `%s': %s",
               dest, TXstrerror(TXgeterror()));
      fdest = -1;
      close(fsrc);
      fsrc = -1;
      if (er)
        {
          tx_delindexfile(MERR, fn, dest, 0);
          return(-1);
        }
    }
  chmod(dest, stdest.st_mode);
  if (destexists) chown(dest, stdest.st_uid, stdest.st_gid);
  if (!tx_delindexfile(MERR, fn, src, flags)) return(-1);
  return(0);
}

int
tx_updateparamtbl(path, indexType, paramTblInfo, fdbiVersion)
char            *path;          /* full path to _P.tbl, without .tbl */
int             indexType;      /* (in) INDEX_... type */
CONST TXMMPARAMTBLINFO *paramTblInfo; /* (in) _P.tbl info */
int		fdbiVersion;	/* (in) FDBI index version */
/* Updates version number, table size in parameter table.
 * Assumes table/index are locked.  Returns 0 on error.
 */
{
  static CONST char     vers[] = "Version";
  static CONST char     tsz[] = "Table Size";
  static CONST char     totalRowCountStr[] = "Total RowCount";
  static CONST char     totalOccurrenceCountStr[] = "Total OccurrenceCount";
  static CONST char     totalWordsStr[] = "Total Words";
  static CONST char     maxWordLenStr[] = "Max Word Len";
  static CONST char     hugeintFmt[] = "%21wd";
  TBL                   *tbl = TBLPN;
  FLD                   *pfld, *vfld;
  RECID                 *recidp;
  char                  *pname, *strlst[2], tmp[EPI_HUGEINT_BITS+2];
  int                   didvers = 0, didtblsz = 0, sz, writeit, ret;
  int                   didTotalRowCount = 0, didTotalOccurrenceCount = 0;
  int                   didTotalWords = 0, didMaxWordLen = 0;
  ft_strlst             *fsl = (ft_strlst *)NULL;

  if ((tbl = opentbl(TXPMBUFPN, path)) == TBLPN ||
      (pfld = nametofld(tbl, "Param")) == FLDPN ||
      (vfld = nametofld(tbl, "Value")) == FLDPN)
    goto err;                                   /* error or real old _P.tbl */

  for (recidp = gettblrow(tbl, NULL);
       TXrecidvalid(recidp);
       recidp = gettblrow(tbl, NULL))
    {
      writeit = 0;
      if ((pname = (char *)getfld(pfld, NULL)) == CHARPN) continue;
      if (!didvers && strcmp(pname, vers) == 0)
        {
          sprintf(tmp, "%ld", (long)fdbiVersion);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didvers = 1;
        }
      else if (!didtblsz && strcmp(pname, tsz) == 0)
	{
          htsnpf(tmp, sizeof(tmp), hugeintFmt,
                 (EPI_HUGEINT)paramTblInfo->originalTableSize);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didtblsz = 1;
        }
      else if (!didTotalRowCount && strcmp(pname, totalRowCountStr) == 0)
        {
          /* pad w/spaces to avoid DBF block resize: */
          htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalRowCount);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didTotalRowCount = 1;
        }
      else if (!didTotalOccurrenceCount && strcmp(pname, totalOccurrenceCountStr) == 0)
        {
          /* pad w/spaces to avoid DBF block resize: */
          htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalOccurrenceCount);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didTotalOccurrenceCount = 1;
        }
      else if (!didTotalWords && strcmp(pname, totalWordsStr) == 0)
        {
          /* pad w/spaces to avoid DBF block resize: */
          htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalWords);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didTotalWords = 1;
        }
      else if (!didMaxWordLen && strcmp(pname, maxWordLenStr) == 0)
        {
          /* pad w/spaces to avoid DBF block resize: */
          htsnpf(tmp, sizeof(tmp), hugeintFmt,
                 (EPI_HUGEINT)paramTblInfo->maxWordLen);
          strlst[0] = tmp;
          strlst[1] = CHARPN;
          if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
          putfld(vfld, fsl, sz);
          writeit = didMaxWordLen = 1;
        }
      /* Note that puttblrow() could return a different recid than we
       * gave it, e.g. if the record is larger, and thus we might re-read
       * this row again.  Hence the `didvers' etc. flags:
       */
      if (writeit && puttblrow(tbl, recidp) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didvers)
    {
      putfld(pfld, (char *)vers, sizeof(vers) - 1);
      sprintf(tmp, "%ld", (long)fdbiVersion);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didtblsz)
    {
      putfld(pfld, (char *)tsz, sizeof(tsz) - 1);
      htsnpf(tmp, sizeof(tmp), hugeintFmt,
             (EPI_HUGEINT)paramTblInfo->originalTableSize);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didTotalRowCount)
    {
      putfld(pfld, (char *)totalRowCountStr, sizeof(totalRowCountStr) - 1);
      /* space-pad to avoid DBF block resize: */
      htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalRowCount);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didTotalOccurrenceCount && indexType == INDEX_FULL)
    {
      putfld(pfld, (char *)totalOccurrenceCountStr, sizeof(totalOccurrenceCountStr) - 1);
      /* space-pad to avoid DBF block resize: */
      htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalOccurrenceCount);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didTotalWords)
    {
      putfld(pfld, (char *)totalWordsStr, sizeof(totalWordsStr) - 1);
      /* space-pad to avoid DBF block resize: */
      htsnpf(tmp, sizeof(tmp), hugeintFmt, paramTblInfo->totalWords);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  if (!didMaxWordLen)
    {
      putfld(pfld, (char *)maxWordLenStr, sizeof(maxWordLenStr) - 1);
      /* space-pad to avoid DBF block resize: */
      htsnpf(tmp, sizeof(tmp), hugeintFmt, (EPI_HUGEINT)paramTblInfo->maxWordLen);
      strlst[0] = tmp;
      strlst[1] = CHARPN;
      if ((fsl = _ctofstrlst(strlst, &sz)) == (ft_strlst *)NULL) goto err;
      putfld(vfld, fsl, sz);
      if (puttblrow(tbl, RECIDPN) == RECIDPN) goto err;
      fsl = TXfree(fsl);
    }
  ret = 1;
  goto done;

err:
  putmsg(MERR, CHARPN, "Could not update parameter table %s", path);
  ret = 0;
done:
  if (tbl != TBLPN) closetbl(tbl);
  fsl = TXfree(fsl);
  return(ret);
}

int
TXtransferIndexOrTable(oldPath, newPath, ddic, logicalName, type,
                       paramTblInfo, fdbiVersion, flags)
CONST char      *oldPath;       /* old existing index/table path (sans ext) */
CONST char      *newPath;       /* new (temp) path to replace it (sans ext) */
DDIC            *ddic;          /* (in) DDIC */
char            *logicalName;   /* logical name of index/table */
int             type;           /* index/table type: INDEX_MM/INDEX_FULL/...*/
CONST TXMMPARAMTBLINFO *paramTblInfo; /* (in, opt.) _P.tbl info (if MM idx) */
int		fdbiVersion;	/* (in, opt.) (if MM index) */
int             flags;          /* (in) 0x1: is table  0x2: rebuild */
/* Transfer newly created index or table `newPath' (sans extension,
 * e.g. `/dir/dbdir/xtest') to old version `oldPath'.  Assumes locks
 * are held on index/table (and table of index too).  Also update
 * param table for `tbsz', version number (if `!(flags & 0x1)').
 * Returns 0 on success, -1 on error.
 */
{
  static CONST char             fn[] = "TXtransferIndexOrTable";
#ifdef _WIN32
  static CONST char * CONST     testexts[] =
  {
    TX_BTREE_SUFFIX, FDBI_DATSUF, FDBI_TOKSUF, "_D.btr", "_P.tbl", "_T.btr",
    "", TXtempPidExt, TXxPidExt, "_X.btr", "_Z.btr", CHARPN
#  define MAXTESTEXTLEN 6
  };
  CONST char * CONST            *extp;
  /* number of -N tries; 0-9: */
#define MAXI    3
  char          *d;
  int           i, found;
  char          *oldLiveFile = CHARPN;  /* old live-row WHAT/FNAME */
  FLD           *remarkFld = FLDPN;
#else /* !_WIN32 */
  EPI_STAT_S    dbst;
#endif /* !_WIN32 */
  int           locks = 0, ret, sysTblIdx;
  CONST char    *sysTblName, *sysTblFileFldName, *objTypeName;
  /* Macros to lock/unlock SYSINDEX, returning 0 on error: */
#define LOCKSYSTBL()            (locks > 0 ? 1 :        \
  (TXlocksystbl(ddic, sysTblIdx, W_LCK, NULL) == -1 ? 0 : ++locks))
#define UNLOCKSYSTBL()        \
  (locks > 0 ? (locks--, TXunlocksystbl(ddic, sysTblIdx, W_LCK) == 0) : 1)
  char          n1[PATH_MAX], n2[PATH_MAX];
  FLD           *typefld = FLDPN, *namefld = FLDPN, *sysTblFileFld = FLDPN;
  TBL           *sysTbl;
  RECID         *l, liverow, thisrow, temprow, *saveThisRowLocTo;
  char          otype, *s;
  size_t        sz;
#define C1(a, b)        (TXcatpath(n1, (a), (b)), n1)
#define C2(a, b)        (TXcatpath(n2, (a), (b)), n2)
  EPI_STAT_S    newlivest;
  int           dirchange, delThisRow, isMetamorphIndex;
  size_t        newdirlen, dblen;
  char          *nsip = CHARPN;         /* new SYSINDEX.FNAME path */
  CONST char    *oldf, *newf;           /* `oldPath'/`newPath' basenames */
  char          newlive[PATH_MAX];      /* `newPath' when renamed live */

  if (flags & 0x1)                              /* is table */
    {
      isMetamorphIndex = 0;
      objTypeName = "table";
      sysTblIdx = SYSTBL_TABLES;
      sysTbl = ddic->tabletbl;
      sysTblName = "SYSTABLES";
      sysTblFileFldName = "WHAT";
    }
  else                                          /* is index */
    {
      isMetamorphIndex = (type == INDEX_MM || type == INDEX_FULL);
      objTypeName = "index";
      sysTblIdx = SYSTBL_INDEX;
      sysTbl = ddic->indextbl;
      sysTblName = "SYSINDEX";
      sysTblFileFldName = "FNAME";
    }
  if (sysTbl == TBLPN ||
      (sysTblFileFld = nametofld(sysTbl, (char*)sysTblFileFldName)) ==FLDPN ||
      (typefld = nametofld(sysTbl, "TYPE")) == FLDPN ||
      (namefld = nametofld(sysTbl, "NAME")) == FLDPN)
    {
      putmsg(MERR, fn, "Cannot get %s %s table or fields",
             ddic->epname, sysTblName);
      goto err;
    }

  oldf = TXbasename(oldPath);                       /* see if dir change */
  newf = TXbasename(newPath);
  dirchange = (TXpathcmp(oldPath, oldf - oldPath, newPath, newf - newPath)
               != 0);
  dblen = strlen(ddic->pname);

  /* Determine the path and possibly SYSINDEX.FNAME/SYSTABLES.WHAT
   * entry for the new index/table.  Under Unix, we rename the new
   * Tnnn index/table file to the old live file, so we only need to
   * change the FNAME/WHAT field if the dir changed (or ALTER INDEX
   * REBUILD, where we have an INDEX_TEMP entry being made live and
   * its FNAME must change).  KNG 20051203 Under Windows, we cannot
   * delete the old files if they are in use and therefore cannot
   * rename the new Tnnn index/table to old, but we can at least make
   * the new index/table file similar to the logical name of the
   * index/table (perhaps with a suffix).  If we can, then we need to
   * upate FNAME/WHAT too, which already has the right dir (from
   * makeindex()) but only the temp Tnnn name.  20101029 also do
   * Windows-style for all tables, as we migrate for Bug 3231:
   */
#ifndef _WIN32
  if (dirchange || (flags & 0x3))       /* dir changed, or table or rebuild */
    {
#endif /* !_WIN32 */
      if (!TXcatpath(newlive, newPath, "")) goto err;
      newdirlen = newf - newPath;
      newlive[newdirlen] = '\0';                /* newlive = new's dir +'/' */
      nsip = newlive;                           /* nsip = absolute new dir */
      if (TXpathcmp(newlive, dblen, ddic->pname, dblen) == 0)
        nsip = newlive + dblen;                 /* nsip = db-relative subdir*/
#ifndef _WIN32
      else if (EPI_STAT(ddic->pname, &dbst) == 0 &&
               EPI_STAT(newlive, &newlivest) == 0 &&
               dbst.st_dev == newlivest.st_dev &&
               dbst.st_ino == newlivest.st_ino) /* new dir same inode as db */
        nsip = newlive + newdirlen;             /* nsip = basename(newPath) */
      if (newdirlen + strlen(oldf) >= PATH_MAX)
        {
          putmsg(MERR + MAE, fn, "Path `%.30s'... too long", newPath);
          goto err;
        }
      strcpy(newlive + newdirlen, oldf);        /*newlive=new dir + old file*/
    }
  else                          /* index, not rebuild, and new/old same dir */
    {
      if (!TXcatpath(newlive, oldPath, ""))     /* rename temp new to old */
        goto err;
    }
#else /* _WIN32 */
  /* For neatness, try to rename `Tnnn' new files to logical-name-similar
   * name.  (We cannot just move them to the old files because the old ones
   * may still be held open by another process and thus cannot be deleted.)
   * See if we can use the logical name of the index/table for the new file,
   * otherwise try logical name with `-0' appended, then `-1'.  The dashes
   * help ensure (but do not guarantee) that a name we choose will not be
   * used in a future `create index/table' for a new different index/table,
   * as the dash is not a legal SQL token.  Note that all MAXI extension
   * tries might fail, e.g. if orphaned files left around or another index
   * is using this filename, in which case we leave it named `Tnnn'.
   */
  d = newlive + newdirlen;
  /* Normal index/table move: we try to use `logicalName' or a
   * variant as the filename:
   */
  d += strlen(logicalName);
  if (d + 2 + MAXTESTEXTLEN >= newlive + sizeof(newlive))
    {                                           /* +2 for `-n' below */
      putmsg(MERR + MAE, fn, "Path `%.30s'... too long", newPath);
      goto err;
    }
  /* `newlive' = new dir + `logicalName': */
  strcpy(newlive + newdirlen, logicalName);
  if (flags & 0x1) remarkFld = nametofld(sysTbl, "REMARK");

  for (i = 0;
       i < MAXI;
       /* Keep SYS... table locked after last pass, so `newlive'/`oldLiveFile'
        * remain valid:
        */
       ++i < MAXI && (UNLOCKSYSTBL(), oldLiveFile = TXfree(oldLiveFile)))
    {
      if (!LOCKSYSTBL()) goto err;
      if (i > 0)                                /* logicalName + "-N" */
        {
          d[0] = '-';
          d[1] = '0' + (i - 1);
          d[2] = '\0';
        }
      else                                      /* logicalName */
        d[0] = '\0';
      /* Make sure proposed filename is not already in use in SYS...
       * Then keep SYS... locked during stat() checks and SYS... update,
       * to prevent a race with another process testing the same name:
       */
      rewindtbl(sysTbl);
      found = 0;
      while ((!found || oldLiveFile == CHARPN) &&
             TXrecidvalid(l = gettblrow(sysTbl, NULL)))
        {
          s = (char *)getfld(sysTblFileFld, &sz);
          found = (found ||
                   TXpathcmp(s, -1, nsip, -1) == 0 ||
                   TXpathcmp(s, -1, newlive, -1) == 0);
          /* Also find the old/existing live row's WHAT/FNAME value,
           * for later use.  Note that we free this if/when the SYS...
           * table is unlocked, because it is then stale:
           */
          if (oldLiveFile == CHARPN &&          /* sanity: only take 1st */
              strcmp((char *)getfld(namefld, NULL), logicalName) == 0 &&
              *(char *)getfld(typefld, &sz) == type)
            oldLiveFile = TXstrdup(TXPMBUFPN, fn, s);
          TX_INDEXDEBUG_MSG((999, fn,
                             "(%u) Read %s %s row 0x%wx: %s %s %s %s or %s",
                             (unsigned)TXgetpid(0), ddic->epname, sysTblName,
                             (EPI_HUGEINT)TXgetoff2(l), sysTblFileFldName,
               s, (found ? "matches" : "does not match"), nsip, newlive));
        }
      if (found) continue;                     /* in SYS...: try next name */
      TX_INDEXDEBUG_MSG((999, fn,
                "(%u) Proposed %s %s file %s or %s not in %s, checking files",
                         (unsigned)TXgetpid(0), ddic->epname,
                         objTypeName, nsip, newlive, sysTblName));
      /* Make sure no files exist with proposed filename as root: */
      for (extp = testexts; *extp != CHARPN; extp++)
        {
          strcpy(d + (i > 0 ? 2 : 0), *extp);/*newdir+logicalName[+"-N"]+ext*/
          if (EPI_STAT(newlive, &newlivest) == 0) break;
        }
      TX_INDEXDEBUG_MSG((999, fn, "(%u) Proposed %s %s file %s %s",
                         (unsigned)TXgetpid(0), ddic->epname, objTypeName,
                         newlive,
                         (*extp != CHARPN ? "exists: trying next -N suffix" :
                          "and others do not exist: using this -N suffix")));
      if (*extp == CHARPN) break;               /* no files w/proposed name */
    }
  /* Note that SYS... should still be locked here, and should remain
   * so until after SYS... is updated below, so that nothing has
   * changed since we determined `newlive' and `liveWhat' above:
   */
  if (i < MAXI)                                 /* proposed file ok to use */
    d[i > 0 ? 2 : 0] = '\0';                    /* remove textext */
  else                                          /* all proposed files in use*/
    {
      /* No unused -N filename was found, so leave the new index/table
       * as its existing Tnnn filename.  We know it's ok since
       * SYS... uniqueness was checked at TXcreateTempIndexOrTableEntry():
       */
      TXcatpath(newlive, newPath, "");          /* leave new as-is */
      nsip = CHARPN;                            /*no FNAME/WHAT change below*/
      TX_INDEXDEBUG_MSG((999, fn,
                "(%u) Cannot find unused %s %s filename: leaving as %s",
                (unsigned)TXgetpid(0), ddic->epname, objTypeName, newlive));
    }
#endif /* _WIN32 */

  TX_INDEXDEBUG_MSG((999, fn, "(%u) Transferring %s to %s (replace %s)",
                     (unsigned)TXgetpid(0), newPath, newlive, oldPath));

  /* Transfer table: ------------------------------------------------------ */
  if (flags & 0x1)                              /* is table */
    {
#ifdef _WIN32
      if (nsip != CHARPN)                       /* newPath -> logicalName~ */
        {
          if (emove(C2(newPath, ".tbl"), C1(newlive, ".tbl"), 0) < 0)
            goto err;
          if (emove(C2(newPath, ".blb"), C1(newlive, ".blb"), 0) < 0)
            goto err;
        }
#else /* !_WIN32 */
      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, ".tbl"), 0))
        goto err;
      if (emove(C2(newPath, ".tbl"), C1(newlive, ".tbl"), 0) < 0)
        goto err;
      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, ".blb"), 0))
        goto err;
      if (emove(C2(newPath, ".blb"), C1(newlive, ".blb"), 0) < 0)
        goto err;
#endif /* !_WIN32 */
      goto updateSysTbl;
    }

  /* Transfer index: ------------------------------------------------------ */
#ifdef _WIN32
  if (nsip != CHARPN)                           /* newPath -> logicalName~ */
    {
      if (emove(C2(newPath, TX_BTREE_SUFFIX),
                C1(newlive, TX_BTREE_SUFFIX), 0) < 0)
        goto err;

      if (isMetamorphIndex)
        {
          if (emove(C2(newPath, FDBI_DATSUF), C1(newlive, FDBI_DATSUF),0) < 0)
            goto err;

          if (emove(C2(newPath, FDBI_TOKSUF), C1(newlive, FDBI_TOKSUF),0) < 0)
            goto err;
        }
    }

  /* old_X.btr and old_Z.btr may still be held open by another process
   * (or us), so do not consider failure to delete them an error.
   * They will be deleted when `oldPath' is deleted (sometime in the future)
   * after we mark it INDEX_DEL below:
   */
  if (isMetamorphIndex)                         /* Metamorph */
    {
      if (nsip != CHARPN &&
          !tx_delindexfile(MERR, fn, C1(newPath, "_D.btr"), 0))
        goto err;
      if (emove(C2(oldPath, "_X.btr"), C1(newlive, "_D.btr"), 1) < 0)
        goto err;

      if (nsip != CHARPN &&
          !tx_delindexfile(MERR, fn, C1(newPath, "_T.btr"), 0))
        goto err;
      if (emove(C2(oldPath, "_Z.btr"), C1(newlive, "_T.btr"), 1) < 0)
        goto err;

      if (nsip != CHARPN &&
          emove(C2(newPath, "_C.btr"), C1(newlive, "_C.btr"), 0) < 0)
        goto err;

      /* note: here we _copy_ the old index's param table; e.g. cannot
       * change index expressions after index is created:
       * KNG 991109 but we _can_ update the version number, after the copy:
       * KNG 000417 and the table size:
       * KNG 20120606 for ALTER INDEX REBUILD we are remaking everything
       * (even if some things are copied from old, e.g. param table info),
       * so replace old param table with new index's:
       */
      if (flags & 0x2)                          /* rebuild */
        {
          if (!tx_delindexfile(MERR, fn, C1(oldPath, "_P.tbl"), 0))
            goto err;
          if (emove(C2(newPath, "_P.tbl"), C1(newlive, "_P.tbl"), 0) < 0)
            goto err;
        }
      else                                      /* optimize only */
        {
          if (!tx_delindexfile(MERR, fn, C1(newPath, "_P.tbl"), 0))
            goto err;
          if (emove(C2(oldPath, "_P.tbl"), C1(newlive, "_P.tbl"), 1) < 0)
            goto err;
        }
      if ((s = strrchr(C1(newlive, "_P.tbl"), '.')) != CHARPN) *s = '\0';
      /* `paramTblInfo' may be optional, for ALTER TABLE COMPACT: */
      if (paramTblInfo &&
          !tx_updateparamtbl(n1, type, paramTblInfo, fdbiVersion))
        goto err;
    }

#else /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  if (dirchange &&
      !tx_delindexfile(MERR, fn, C1(oldPath, TX_BTREE_SUFFIX), 0))
    goto err;
  if (emove(C2(newPath, TX_BTREE_SUFFIX),
            C1(newlive, TX_BTREE_SUFFIX), 0) < 0)
    goto err;

  if (isMetamorphIndex)
    {
      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, FDBI_DATSUF), 0))
        goto err;
      if (emove(C2(newPath, FDBI_DATSUF), C1(newlive, FDBI_DATSUF), 0) < 0)
        goto err;

      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, FDBI_TOKSUF), 0))
        goto err;
      if (emove(C2(newPath, FDBI_TOKSUF), C1(newlive, FDBI_TOKSUF), 0) < 0)
        goto err;
    }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  if (isMetamorphIndex)                         /* MM */
    {
      if (!tx_delindexfile(MERR, fn, C1(newPath, "_D.btr"), 0)) goto err;
      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, "_D.btr"), 0))
        goto err;
      if (emove(C2(oldPath, "_X.btr"), C1(newlive, "_D.btr"), 0) < 0)
        goto err;

      if (!tx_delindexfile(MERR, fn, C1(newPath, "_T.btr"), 0)) goto err;
      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, "_T.btr"), 0))
        goto err;
      if (emove(C2(oldPath, "_Z.btr"), C1(newlive, "_T.btr"), 0) < 0)
        goto err;

      if (dirchange &&
          !tx_delindexfile(MERR, fn, C1(oldPath, "_C.btr"), 0))
        goto err;
      if (emove(C2(newPath, "_C.btr"), C1(newlive, "_C.btr"), 0) < 0)
        goto err;

      /* note: here we preserve the old index's param table; e.g. cannot
       * change index expressions after index is created:
       * KNG 991109 but we _can_ update the version number, after the copy:
       * KNG 000417 and the table size:
       * KNG 20120606 for ALTER INDEX REBUILD we are remaking everything
       * (even if some things are copied from old, e.g. param table info),
       * so replace old param table with new index's:
       */
      if (flags & 0x2)				/* rebuild */
        {
          if (!tx_delindexfile(MERR, fn, C1(oldPath, "_P.tbl"), 0))
            goto err;
          if (emove(C2(newPath, "_P.tbl"), C1(newlive, "_P.tbl"), 0) < 0)
            goto err;
        }
      else                                      /* optimize only */
        {
          if (!tx_delindexfile(MERR, fn, C1(newPath, "_P.tbl"), 0)) goto err;
          if (dirchange &&
              emove(C2(oldPath, "_P.tbl"), C1(newlive, "_P.tbl"), 0) < 0)
            goto err;
        }
      if ((s = strrchr(C1(newlive, "_P.tbl"), '.')) != CHARPN) *s = '\0';
      /* `paramTblInfo' may be optional, for ALTER TABLE COMPACT: */
      if (paramTblInfo &&
          !tx_updateparamtbl(n1, type, paramTblInfo, fdbiVersion))
        goto err;
    }
#endif  /* !_WIN32 - - - - - - - - - - - - - - - - - - - - - - - - - - - */

updateSysTbl:
  /* Now update SYSINDEX/SYSTABLES as needed.
   * Indexes: - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   *   Under Windows, since we cannot rename in-use files, we mark the
   * live index as deleted (INDEX_DEL), and the new index (currently
   * an INDEX_TEMP `Tnnn' index) as the live one.  KNG 20051130 but we
   * may also have renamed the new `Tnnn' index to a name similar to
   * the logical name (if `nsip' set), so also update FNAME if so.
   *   Under Unix, we have already renamed all the new `Tnnnn' files
   * to live files; only edit SYSINDEX if indexspace changed
   * (`dirchange') or ALTER INDEX REBUILD (`Tnnnn' was INDEX_TEMP).
   * Tables: - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   *   Do Windows-style, since we are slowly migrating that way (Bug 3231).
   * But instead of live -> deleted and temp -> live, keep the live
   * entry live and set WHAT -> `nsip' (new file), and make the temp
   * entry deleted (with WHAT -> `oldLiveFile').  This avoids having
   * to change temp.REMARK/CREATOR fields back to live values
   * (which we may not know).  Eventually do indexes this way too?
   * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   *   Note that all updates here must be able to deal with the updated
   * row(s) reappearing later in the gettblrow() loop (e.g. KDBF moved it):
   *   Note that under Windows, SYSINDEX/SYSTABLES may already be locked
   * to ensure no one else tries to use our `newlive' name.
   * The LOCKSYSTBL() macros know this:
   */
  TXsetrecid(&liverow, RECID_INVALID);          /* row not updated yet */
  TXsetrecid(&temprow, RECID_INVALID);          /* row not updated yet */
#ifdef _WIN32
  /* always */
#else /* !_WIN32 */
  if (dirchange || (flags & 0x3))       /* dir changed, or table or rebuild */
#endif /* !_WIN32 */
    {
      if (!LOCKSYSTBL()) goto err;
      if (!rewindtbl(sysTbl))
        {
          UNLOCKSYSTBL();
          goto err;
        }
      while (TXrecidvalid(l = gettblrow(sysTbl, NULL)))
        {
          thisrow = *l;                         /* save it: `*l' is static */
          delThisRow = 0;
          saveThisRowLocTo = RECIDPN;
          TX_INDEXDEBUG_MSG((999, fn,
                             "(%u) Read %s %s row 0x%wx: type %c file %s",
                             (unsigned)TXgetpid(0), ddic->epname, sysTblName,
                             (EPI_HUGEINT)TXgetoff2(l),
                             (int)*(char *)getfld(typefld, &sz),
                             (char *)getfld(sysTblFileFld, &sz)));
          if (strcmp((char *)getfld(namefld, NULL), logicalName) != 0)
            continue;                           /* not our idx/tbl: next row*/
          if (TXrecidcmp(&thisrow, &liverow) == 0)
            continue;                           /* already updated this row */
          if (TXrecidcmp(&thisrow, &temprow) == 0)
            continue;                           /* already updated this row */
          otype = *(char *)getfld(typefld, &sz);
          if (flags & 0x1)                      /* is table */
            switch (otype)
              {
              case TEXIS_TEMP_TABLE:            /* new (temp) entry: delete */
#ifdef _WIN32
                otype = TEXIS_DEL_TABLE;        /* mark for deletion */
                putfld(typefld, &otype, 1);
                saveThisRowLocTo = &temprow;    /* save new recid to temprow*/
                /* Set WHAT to the old live row's WHAT, so the proper
                 * file gets deleted by TXdocleanup().  Since we may
                 * encounter this `temprow' before `liverow', we
                 * pre-determined `oldLiveFile' above.  Bug 3684:
                 */
                if (oldLiveFile)
                  putfld(sysTblFileFld, oldLiveFile, strlen(oldLiveFile));
                else
                  /* wtf do not know `oldLiveFile'; at least clear out
                   * entry so the old (so-to-be-renamed) temp file is
                   * not in SYS... anymore:
                   */
                  putfld(sysTblFileFld, "", 0);
                /* REMARK may have been `Output of ALTER TABLE COMPACT';
                 * clear that since it no longer is (WHAT changed):
                 */
                if (remarkFld)
                  putfld(remarkFld, "", 0);
#else  /* !_WIN32 */
                delThisRow = 1;
#endif /* !_WIN32 */
                break;
              default:
                /* Be conservative for now: besides temp entries, only
                 * muck with entries that match our `type', unlike
                 * SYSINDEX land where we may have INDEX_3DB ->
                 * INDEX_FULL upgrades (not supported anymore).
		 * We avoid TEXIS_LINK, TEXIS_VIEW etc.; we do not
		 * know much about them:
                 */
                if (otype != type) break;
                /* Live entry.  Set TYPE to our type (it already is)
                 * and SYSTABLES.WHAT to new file value:
                 */
                otype = (char)type;
                putfld(typefld, &otype, 1);
                if (nsip != CHARPN) putfld(sysTblFileFld, nsip, strlen(nsip));
                saveThisRowLocTo = &liverow;    /* save new recid to liverow*/
                break;
              }
          else switch (otype)                   /* is index */
            {
            case INDEX_TEMP:                    /* new index: make live */
              otype = (char)type;
              putfld(typefld, &otype, 1);
              if (nsip != CHARPN) putfld(sysTblFileFld, nsip, strlen(nsip));
              saveThisRowLocTo = &temprow;
              break;
            default:
              /* Bug 3684: mark old B-tree, inverted indexes as INDEX_DEL
               * too, not just Metamorph:
               */
              if (otype != type) break;         /* not our type of index */
#ifdef _WIN32
              otype = INDEX_DEL;                /* old index: mark for del */
              putfld(typefld, &otype, 1);
              saveThisRowLocTo = &liverow;
#else  /* !_WIN32 */
              if (flags & 0x2)                  /* rebuild */
                delThisRow = 1;                 /* old index: delete it */
              else
                {
                  if (dirchange) putfld(sysTblFileFld, nsip, strlen(nsip));
                  saveThisRowLocTo = &liverow;
                }
#endif /* !_WIN32 */
              break;
            }
          if (delThisRow)
            {
              deltblrow(sysTbl, &thisrow);
              l = RECIDPN;
              TX_INDEXDEBUG_MSG((999, fn,
                           "(%u) Deleted %s %s row at 0x%wx: type %c file %s",
                                 (unsigned)TXgetpid(0), ddic->epname,
                                 sysTblName, (EPI_HUGEINT)TXgetoff2(&thisrow),
                                 (int)*(char *)getfld(typefld, &sz),
                                 (char *)getfld(sysTblFileFld, &sz)));
            }
          else
            {
              l = puttblrow(sysTbl, &thisrow);
              TX_INDEXDEBUG_MSG((999, fn,
                 "(%u) Wrote %s %s row at 0x%wx (now 0x%wx): type %c file %s",
                                 (unsigned)TXgetpid(0), ddic->epname,
                                 sysTblName, (EPI_HUGEINT)TXgetoff2(&thisrow),
                                 (EPI_HUGEINT)TXgetoff2(l),
                                 (int)*(char *)getfld(typefld, &sz),
                                 (char *)getfld(sysTblFileFld, &sz)));
            }
          /* Save this recid if it was an update, so we don't update again: */
          if (saveThisRowLocTo != RECIDPN && l != RECIDPN)
            *saveThisRowLocTo = *l;
          if (flags & 0x1)                      /* table */
            TXddicSetSystablesChanged(ddic, 1); /* just in case no locks */
          else
            TXddicSetSysindexChanged(ddic, 1);  /* just in case no locks */
        }
      UNLOCKSYSTBL();
    }

  /* Delete .PID file(s) last: */
#ifdef _WIN32
  if (!(flags & 0x1))                           /* index */
    {
      /* If renaming, delete no-extension PID file, last.  See makeindex: */
      if (nsip != CHARPN) tx_delindexfile(MWARN, fn, newPath, 0);
    }
#endif /* _WIN32 */
#ifndef _WIN32
  if (flags & 0x3)                              /* table, or index rebuild */
#endif /* !_WIN32 */
    tx_delindexfile(MWARN, fn, C1(newPath, TXtempPidExt), 0);
  ret = 0;                                      /* success */
  goto done;

err:
  ret = -1;                                     /* error */
done:
  while (locks > 0) UNLOCKSYSTBL();
  return(ret);
#undef C1
#undef C2
#undef MAXTESTEXTLEN
#undef LOCKSYSTBL
#undef UNLOCKSYSTBL
#undef MAXI
}

/******************************************************************/

static void freeidxlist ARGS((int inum, char *itype, char *indexNonUniques,
			      char **al, char **bl, char **cl));

static void
freeidxlist(inum, itype, indexNonUniques, al, bl, cl)
int inum;
char *itype;
char	*indexNonUniques;
char **al;
char **bl;
char **cl;
{
	int i;

	itype = TXfree(itype);
	indexNonUniques = TXfree(indexNonUniques);
	for(i=0;i<inum;i++)
	{
		if(al)
			al[i] = TXfree(al[i]);
		if(bl)
			bl[i] = TXfree(bl[i]);
		if(cl)
			cl[i] = TXfree(cl[i]);
	}
	al = TXfree(al);
	bl = TXfree(bl);
	cl = TXfree(cl);
}

/******************************************************************/

int
updindex(ddic, indname, flags, options)
DDIC	*ddic;		/* (in) data dictionary */
char	*indname;	/* (in) name of index to update */
int	flags;		/* (in) bit flags:  0x2: rebuild */
TXindOpts	*options;	/* (in) `WITH ...' options; may be modified */
/* Updates Metamorph index named `indname' (or rebuilds any type index).
 * Returns 0 on success, -1 on error.
 */
{
	static CONST char	Fn[] = "updindex";
	A3DBI   *dbi = NULL;
	TXMKIND	*ind = NULL;
	char    *indexTypes = NULL, *indexNonUniques = NULL;
	char    **indexFiles = NULL;
	char    *oldIndexPath = CHARPN;	/* existing/old index path sans ext.*/
	int     numIndexes = 0, i, found, indexType = -1, cr = 0, res, ret;
	char    **indexTables = NULL, *tableName = CHARPN;
	char    **indexFields = NULL, *primaryFieldName = CHARPN;
	char	*sysindexFields = CHARPN;
	char	*sysindexParams = CHARPN;
	char	**sysindexParamsVals = CHARPPN;
	char    *newIndexPath = CHARPN;	/* new index path, sans ext. */
	TBL     *table;
	DBTBL   *dbtable = NULL;
	FLD     *primaryField;
        WTIX    *wx = WTIXPN;
        char    *s;
	int	ctieFlags, afterOptIndexType;
	int	opid = 0, gotErr = 0, isUnique = -1, numTblRdLocks = 0;
        int     updvers = 0, beingUpdated = 0, madeTempIndexEntry = 0;
        TXMMPARAMTBLINFO        paramTblInfo;
	DD	*tmpDd = NULL;
        char    collSeq;
        BTPARAM btparams;
        char    buf[PATH_MAX], newIndexDir[PATH_MAX];

        TX_INIT_TXMMPARAMTBLINFO(&paramTblInfo);
	TXdocleanup(ddic);

	numIndexes = ddgetindexbyname(ddic, indname, &indexTypes,
				      &indexNonUniques, &indexFiles,
				      &indexTables, &indexFields,
				      &sysindexParamsVals);
	if (numIndexes <= 0)
	{
		putmsg(MERR+FOE, Fn, "Could not find index %s", indname);
		goto err;
	}
	found = 0;
	for (i = 0; i < numIndexes; i++)
          switch (indexTypes[i])
            {
            case INDEX_3DB:
              putmsg(MERR + UGE, Fn,
     "Index %s is deprecated old-style Metamorph type %c: Drop and re-create",
                     indname, (int)indexTypes[i]);
              goto err;
            case INDEX_MM:
            case INDEX_FULL:
            case INDEX_BTREE:
            case INDEX_INV:
              oldIndexPath = indexFiles[i];
              tableName = indexTables[i];
              primaryFieldName = indexFields[i];
              indexType = indexTypes[i];
              isUnique = !indexNonUniques[i];
	      sysindexParams = sysindexParamsVals[i];
              found++;
              break;
            case INDEX_3CR:
            case INDEX_MMCR:
            case INDEX_FULLCR:
              cr++;
              break;
	    case INDEX_DEL:
              /* Was not removed by above TXdocleanup() call, but can
               * still be ignored.
               */
              break;
            case INDEX_TEMP:
              /* Was not removed by above TXdocleanup() call, so rebuild
               * (or optimize under Windows) is probably still going on.
	       * Wait to see if we find `oldIndexPath' for putmsg though.
               */
	      beingUpdated++;
	      break;
            }
        if (cr)
          {
            putmsg(MERR + UGE, Fn, "%s is already being created", indname);
	    goto err;
          }
	if (beingUpdated) goto alreadyUpdating;
        if (!found ||
            (indexType != INDEX_MM && indexType != INDEX_FULL &&
             !(flags & 0x2)))
          {
            putmsg(MWARN + UGE, Fn,
                   "%s already exists and is not a Metamorph index",
                   indname);
            goto err;
          }

	/* Process options ASAP (i.e. after we know index type): */
	afterOptIndexType = indexType;
	if (!TXindOptsProcessRawOptions(options, &afterOptIndexType, 1))
		goto err;
	/* ignore changed index type; toss `afterOptIndexType' */

	dbtable = opendbtbl(ddic, tableName);
	if(!dbtable)
	{
		putmsg(MERR+UGE, Fn,
			"Could not open table %s",
			tableName);
		goto err;
	}
#ifndef NEVER
	closeindexes(dbtable);
#endif
	table = dbtable->tbl;
	primaryField = nametofld(table, primaryFieldName);
	sysindexFields = strdup(primaryFieldName);
	if(!primaryField)
	{
		char *tf;

		/* Field `primaryFieldName' not found; probably a compound
		 * name e.g. ",,TextField CompoundField1 CompoundField2".
		 * Set `primaryFieldName' to just "TextField":
		 */
		while(*primaryFieldName && *primaryFieldName == ',')
			primaryFieldName++;
		tf = primaryFieldName;
		while(*tf && *tf != ' ' &&
		      ((indexType != INDEX_BTREE && indexType != INDEX_INV) ||
		       *tf != '-'))
			tf++;
		*tf = '\0';
		primaryField = nametofld(table, primaryFieldName);
		if(!primaryField)
		{
			putmsg(MERR+UGE, Fn,
			       "Could not find field %s in table %s",
			       primaryFieldName, tableName);
			goto err;
		}
	}

        /* Determine new index directory `newIndexdir': */
        if (options->indexspace != CHARPN && *options->indexspace != '\0')
          {
            if (TX_ISABSPATH(options->indexspace) || options->indexspace[0] == '~')
              TXcatpath(newIndexDir, options->indexspace, ""); /* idxsp abs */
            else                                /* indexspace is db-relative*/
              TXcatpath(newIndexDir, ddic->pname, options->indexspace);
            s = newIndexDir + strlen(newIndexDir) - 1;
            if (s > newIndexDir && TX_ISPATHSEP(*s)) *s = '\0';
          }
        else
          TXdirname(TXPMBUFPN, newIndexDir, sizeof(newIndexDir), oldIndexPath);

	if(TXlockindex(dbtable, INDEX_WRITE, NULL) == -1)
		goto err;
        gotErr = 0;
	switch (indexType)
	{
	case INDEX_MM:
	case INDEX_FULL:
          dbi = open3dbi(oldIndexPath, PM_ALLPERMS, indexType,
                         sysindexParams);
          if (dbi == A3DBIPN)
            {
              putmsg(MERR + FOE, Fn, "Unable to open index %s",
                     (oldIndexPath != CHARPN ? oldIndexPath : ""));
              goto err;
            }
          /* Open WTIX for update.  Assumes that wtix_getdellist(),
           * wtix_getnewlist(), wtix_setupdname(), and wtix_finish()
           * will be called eventually, in that order.
           * If rebuilding, no need to open old index (just `dbi'):
           */
          if (!(flags & 0x2))			/* not rebuilding */
	  {
		  TXfdbiIndOpts	fdbiOptions;

		  /* Must preserve (most?) index options from original index,
		   * e.g. word expressions etc., so init from DBI:
		   */
		  TXfdbiIndOpts_INIT_FROM_DBI(&fdbiOptions, dbi);
		  /* But index version can change (and is assumed to change,
		   * to `options->fdbiVersion', below):
		   */
		  fdbiOptions.indexVersion = options->fdbiVersion;
		  if ((wx = openwtix(dbtable, primaryFieldName, oldIndexPath,
				     dbi->auxsz, &fdbiOptions,
                                     &dbi->paramTblInfo,
                  (WTIXF_UPDATE | (indexType == INDEX_FULL ? WTIXF_FULL : 0)),
                             (int)dbi->version, WTIXPN)) == WTIXPN)
		  {
			  dbi = close3dbi(dbi);
			  /* wtf unlock? */
			  goto err;
		  }
	  }
          if (TXlocktable(dbtable, W_LCK) == -1)
            {
              TXunlockindex(dbtable, INDEX_WRITE, NULL);
              dbi = close3dbi(dbi);
              goto err;
            }
          if (dbi->mdel != BTREEPN ||           /* someone else is updating */
              dbi->mupd != BTREEPN ||
              dbi->mnew != BTREEPN)
            {
              FILE  *tf;

              TXcatpath(buf, oldIndexPath, TXxPidExt);
              tf = fopen(buf, "rb");
              if (tf)
                {
                  fscanf(tf, "%d", &opid);
                  fclose(tf);
                  if (opid != TXgetpid(0) && TXprocessexists(opid))
                    {
                      char      pidBuf[2048], *d, *e;

                      TXunlocktable(dbtable, W_LCK);
                      TXunlockindex(dbtable, INDEX_WRITE, NULL);
                      dbi = close3dbi(dbi);
                      wx = closewtix(wx);
                    alreadyUpdating:
                      d = pidBuf;
                      e = pidBuf + sizeof(pidBuf);
                      if (opid)
                        {
                          if (d < e) d += htsnpf(d, e - d, " by PID %u",
                                                 (unsigned)opid);
                          if (d < e) d += TXprintPidInfo(d, e - d, opid, NULL);
                        }
                      else
                        *d = '\0';
                      putmsg(MWARN, CHARPN,
                          "Index %s appears to be being updated or rebuilt%s",
                             (oldIndexPath ? oldIndexPath :
                              indname), pidBuf);
                      goto err;
                    }
                }
              closedeltmpind(dbi);
            }
          if (!(flags & 0x2) && !wtix_getdellist(wx, dbi->del)) goto err2;
          if (TXlockindex(dbtable, INDEX_WRITE, NULL) == -1)
            {
            err2:
              TXunlocktable(dbtable, W_LCK);
              TXunlockindex(dbtable, INDEX_WRITE, NULL);
              dbi = close3dbi(dbi);
#ifdef JMT_TEST
              putmsg(999, NULL, "Couldn't get lock");
#endif
              TXcatpath(buf, oldIndexPath, TXxPidExt);
              tx_delindexfile(MWARN, Fn, buf, 0);
              goto err;
            }
          if (flags & 0x2)                      /* rebuilding */
            res = 1;
          else
            res = (wtix_getnewlist(wx, dbi->newrec) &&
                   wtix_needfinish(wx));        /* anything to do? */
          res = (res && createtmpind(dbi));
          TXunlockindex(dbtable, INDEX_WRITE, NULL);
          break;
        case INDEX_BTREE:
        case INDEX_INV:
          res = 0;
          if (!(flags & 0x2)) break;            /* only proceed if rebuild */
          if (TXlocktable(dbtable, W_LCK) == -1)
            {
              TXunlockindex(dbtable, INDEX_WRITE, NULL);
              goto err;
            }
          /* While we have the index write-locked, make the temp index
           * entry, and ensure that nobody else is trying to rebuild
           * this index (bit flag 0x8):
           */
	  ctieFlags = (flags & 0x2);		/* pass rebuild flag */
	  ctieFlags |= 0x8;			/* fail if temp/cr exists */
	  if (isUnique) ctieFlags |= 0x10;
	  /* wtf we just want `ind->collSeq' but `ind' does not exist yet: */
          tmpDd = TXordspec2dd(dbtable, sysindexFields, 50, 0, 0,
                               TXApp->indexValues, &collSeq);
          if (!tmpDd)
            gotErr = 1;
          else
            {
              tmpDd = closedd(tmpDd);
              if (collSeq == COLL_DESC) ctieFlags |= 0x20;
              if (TXcreateTempIndexOrTableEntry(ddic, newIndexDir, indname,
                                                tableName, sysindexFields, 0,
                                                ctieFlags, NULL,
                                                sysindexParams,
                                                &newIndexPath, NULL))
                madeTempIndexEntry = 1;
              else
                gotErr = 1;
            }
          break;
        default:
          putmsg(MERR, Fn, "Internal error: Unexpected index type %c",
                 indexType);
          goto err;
        }
	if (!gotErr)
          TXtouchindexfile(ddic);       /* force others to reopen & get XYZ */
	TXunlocktable(dbtable, W_LCK);
	TXunlockindex(dbtable, INDEX_WRITE, NULL);

        if (!gotErr && (res || (flags & 0x2)))
          {	                        /* needs updating, or rebuilding */
            switch (indexType)
              {
              case INDEX_MM:
              case INDEX_FULL:
                TXaddabendcb(updind_abendcb, dbi); /* clean up on ABEND */
                /* Warn about updating Metamorph index version here,
                 * after we know we're actually going to modify the
                 * index (e.g. there's new/deleted rows), but before we
                 * spend lots of time on the index update itself.
                 * Actual version update happens via TXtransferIndexOrTable().
                 * Whether we can actually handle the up/downgrade was
                 * checked in openwtix()/openfdbi().  KNG 991109
                 */
                if (wx != WTIXPN && options->fdbiVersion != (int)dbi->version)
                  {
                    putmsg(MINFO, CHARPN, "%sgrading Metamorph index %s from version %d to %d: Old Texis releases may now be %scompatible with it",
                           (options->fdbiVersion > (int)dbi->version ? "Up" :
			    "Down"),
                           dbi->name, (int)dbi->version, options->fdbiVersion,
                           (options->fdbiVersion > (int)dbi->version ? "in" :
                            ""));
                    updvers = 1;                        /* note for later */
                  }
                newIndexPath = makeindex(newIndexDir, dbi, dbtable,
					 primaryField, TXgetblockmax(),
					 indname, primaryFieldName, &wx,
					 sysindexFields, &paramTblInfo, flags,
					 options);
                TXdelabendcb(updind_abendcb, dbi);	/* remove callback */
                break;
	      case INDEX_BTREE:
                BTPARAM_INIT(&btparams);
                if (TXtextParamsToBtparam(&btparams, sysindexParams, indname,
                                          0x1) != 0)
                  goto err;
		options->btparams = btparams;
                ind = TXmkindCreateBtree(dbtable, sysindexFields,
                                         indname, newIndexPath, isUnique,
                                         ((flags & 0x2) ? 1 : 0), options);
                goto buildNonMetamorph;
	      case INDEX_INV:
                BTPARAM_INIT(&btparams);
                if (TXtextParamsToBtparam(&btparams, sysindexParams, indname,
                                          0x1) != 0)
                  goto err;
		options->btparams = btparams;
                ind = TXmkindCreateInverted(dbtable, sysindexFields,
                                         indname, newIndexPath, isUnique,
                                         ((flags & 0x2) ? 1 : 0), options);
              buildNonMetamorph:
                if (!ind) goto err;
                if (TXmkindBuildIndex(ddic, dbtable, ind,&numTblRdLocks) != 1)
                  goto err;
                /* `dbtable' is still read-locked from TXmkindBuildIndex() */
                break;
              }
#if !defined(NO_KEEP_STATS)
	    {
		char *t;
		DATASIZE olddata;
		ft_long	lval;

		if(wx)
		{
		  INDEXSTATS istats;
		  if (wtix_getstats(wx, &istats))
		  {
		    t = TXstrcat3(tableName, ".", indname);
		    TXsetstatistic(ddic,t,"NROWS",istats.totentries, "", 0);
		    TXsetstatistic(ddic,t,"AROWS",istats.newentries, "", 0);
		    TXsetstatistic(ddic,t,"DROWS",istats.delentries, "", 0);
		    TXsetstatistic(ddic,t,"CAROWS",istats.newentries, "", 1);
		    TXsetstatistic(ddic,t,"CDROWS",istats.delentries, "", 1);
		    TXsetstatistic(ddic,t,"IDATAG",istats.indexeddata.gig, "", 0);
		    TXsetstatistic(ddic,t,"IDATAB",istats.indexeddata.bytes,"", 0);
		    if (!(flags & 0x2))		/* not rebuilding */
		    {
			    TXgetstatistic(ddic,t,"CIDATAG", NULL, &lval, NULL);
			    olddata.gig = lval;
			    TXgetstatistic(ddic,t,"CIDATAB", NULL, &lval, NULL);
			    olddata.bytes = lval;
			    TXdatasizeadd(&olddata, &istats.indexeddata);
			    TXsetstatistic(ddic,t,"CIDATAG",olddata.gig, "", 0);
			    TXsetstatistic(ddic,t,"CIDATAB",olddata.bytes,"", 0);
		    }
		    TXsetstatistic(ddic,tableName, "NROWS", istats.totentries, "", 0);
		    t = TXfree(t);
		  }
                  TXsetPerTableLimitStats(dbtable);     /* Bug 4730 */
		}
	    }
#endif /* !NO_KEEP_STATS */
            wx = closewtix(wx);
            ind = TXmkindClose(ind);
            ret = 0;                            /* success so far */
            if(TXlockindex(dbtable, INDEX_WRITE, &dbtable->iwritec)==0)
		{
			if(TXlocktable(dbtable, W_LCK)==0)
			{
                          /* Bug 7015: close open _D.btr, _T.btr etc. files
                           * so that emove's delete during _X.btr -> _Z.btr
                           * etc. does not delete open file: could fail
                           * (even under Unix) on certain filesystems.
                           * (Issue averted under Windows because root name
                           * of new index files is different.):
                           */
                          if (dbi) TXa3dbiCloseOpenFiles(dbi);

                          ret = (newIndexPath == CHARPN ? 0 :
                                 TXtransferIndexOrTable(oldIndexPath,
                                                        newIndexPath, ddic,
                                                        indname, indexType,
                                                        &paramTblInfo,
							options->fdbiVersion,
                                                        (flags & 0x2)));
                          if (newIndexPath && ret == 0)
                            madeTempIndexEntry = 0;
                          TXtouchindexfile(ddic);
                          TXunlocktable(dbtable, W_LCK);
                          if (ret < 0)
                            TXdelindex(newIndexPath, INDEX_TEMP);
                          if (dbi && (ret < 0 || newIndexPath == CHARPN))
                            closedeltmpind(dbi);
			}
			else
                          {
                            ret = -1;           /* error */
                            TXdelindex(newIndexPath, INDEX_TEMP);
                            if (dbi) closedeltmpind(dbi);
                          }
			TXunlockindex(dbtable, INDEX_WRITE, NULL);
		}
            else				/* index lock failed */
              {
                ret = -1;                       /* error */
                TXdelindex(newIndexPath, INDEX_TEMP);
                if (dbi) closedeltmpind(dbi);
              }
            dbi = close3dbi(dbi);
            if (!newIndexPath) ret = -1;
            if (ret < 0)
              putmsg(MERR, CHARPN, "%s of %s index %s failed",
		     ((flags & 0x2) ? "Rebuild" : "Update"),
		     TXgetIndexTypeDescription(indexType), indname);
	}
	else                                    /* no update needed for idx */
          {
            ret = (gotErr ? -1 : 0);
            dbi = close3dbi(dbi);
            wx = closewtix(wx);
          }
        /* wtf del _X, _Z indices on error?  need to lock in case
         * others have them open?               KNG 971114
         */
        switch (indexType)
          {
          case INDEX_MM:
          case INDEX_FULL:
            TXcatpath(buf, oldIndexPath, TXxPidExt);
            tx_delindexfile(MWARN, Fn, buf, 0);
            break;
          }
#ifdef _WIN32
	if(TXlockindex(dbtable, INDEX_WRITE, NULL)==0)
		TXunlockindex(dbtable, INDEX_WRITE, NULL);
	if(TXCleanupWait >= 0)
	{
		Sleep(1000*TXCleanupWait);
		TXbtfreecache(dbtable);
		TXdocleanup(ddic);
	}
#endif /* _WIN32 */
	goto done;

err:
	ret = -1;				/* error */
done:
	if (ind)
	{
		ind = TXmkindClose(ind);
		TXdelindex(newIndexPath, INDEX_TEMP);
	}
	if (madeTempIndexEntry)
	{
		TXdeleteSysindexEntry(ddic, tableName, indname, INDEX_TEMP);
		madeTempIndexEntry = 0;
	}
	tmpDd = closedd(tmpDd);
	for ( ; dbtable && numTblRdLocks > 0; numTblRdLocks--)
		TXunlocktable(dbtable, R_LCK);
	newIndexPath = TXfree(newIndexPath);
	sysindexFields = TXfree(sysindexFields);
	freeidxlist(numIndexes, indexTypes, indexNonUniques, indexFiles,
		    indexTables, indexFields);
	sysindexParamsVals = TXfreeStrList(sysindexParamsVals, numIndexes);
	dbtable = closedbtbl(dbtable);
	return(ret);
}
