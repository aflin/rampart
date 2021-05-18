#include "txcoreconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#ifdef EPI_HAVE_UNISTD_H
#  include <unistd.h>
#endif /* EPI_HAVE_UNISTD_H */
#include "os.h"
#include "texint.h"
#include "kdbfi.h"
#include "meter.h"
#include "cgi.h"


typedef struct TXcmpTbl_tag                     /* COMPACT TABLE object */
{
  int           overwrite;                      /* nonzero: overwrite mode */
  DDIC          *ddic;                          /* DDIC; shared */
  METER         *meter;
  EPI_HUGEINT   meterTotal;
  int           abendCbRegistered;
  /* Table: - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  DBTBL         *dbtbl;                         /* table; shared */
  char          *tableName;                     /* alloced */
  int           tableReadLocks;                 /* table read lock count */
  int           tableWriteLocks;                /* table write lock counts */
  int           indexReadLocks;
  int           indexWriteLocks;
  DBF           *inputDbf;                      /* points into `dbtbl' */
  CONST char    *inputDbfPath;                  /* points into `inputDbf' */
  DBF           *outputDbf;                     /* output DBF */
  char          *outputDbfPath;                 /* allocd or == inputDbfPath*/
  char          *outputTableTempPidPath;        /* alloced */
  RECID         sysTblTempRow;                  /* SYSTABLES entry */
  char          *inputTablePathSansExt;         /* alloced */
  char          *outputTablePathSansExt;        /* alloced */
  EPI_HUGEINT   numInputTableRows;
  /* Blob: - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  DBF           *outputBlobDbf;                 /* opt. */
  char          *outputBlobDbfPath;             /* opt.; alloced */
  /* B-tree indexes: - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  char          **inputBtreePathsSansExt;       /* source B-tree paths */
  BTREE         **outputBtrees;                 /* Tnnn.btr output files */
  RECID         *outputBtreeSysTblTempRows;     /* SYSTABLES temp entries */
  int           numBtrees;                      /* arrays' length */
  /* Inverted indexes: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  char          **inputInvertedPathsSansExt;    /* source inverted paths */
  BTREE         **outputInverteds;              /* Tnnn.btr output files */
  RECID         *outputInvertedSysTblTempRows;  /* SYSTABLES temp entries */
  int           numInverteds;                   /* arrays' length */
  /* Metamorph indexes: - - - - - - - - - - - - - - - - - - - - - - - - - - */
  int           numOutputWtixes;                /* array lengths */
  /* Parallel arrays of `numOutputWtixes' elements: */
  size_t        *inputFdbiAuxFldSizes;          /* input indexes' aux sizes */
  WTIX          **outputWtixes;                 /* Tnnn.tok output files */
  RECID         *outputWtixSysTblTempRows;      /* SYSTABLES temp entries */
  char          **fdbiIndexNames;               /* index names (NULL-term) */
  char          **fdbiTokenPaths;
  FDF           *fdbiIndexFlags;

  int           wtixRecidTranslatorIdx;         /* idx of RECID translator */
  EPI_OFF_T     curWtixToken;                   /* current row's token */
}
TXcmpTbl;
#define TXcmpTblPN      ((TXcmpTbl *)NULL)

/* ------------------------------------------------------------------------ */

int
TXchangeLocInIndices(dbtbl, newLoc)
DBTBL   *dbtbl;         /* (in/out) table */
BTLOC   newLoc;         /* (in) recid to change it to */
/* Changes current `dbtbl' recid to `newLoc' in `dbtbl' indexes.
 * Should be called from TXcompactTable().
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXchangeLocInIndices";
  static CONST char     couldNotUpdate[] =
    "Could not update recid 0x%wx to recid 0x%wx in index `%s'";
  int                   i, ret;
  FDBI                  *fdbi;
  DBTBL                 *savtbl;
  BINDEX                *bindex;
  BINVDX                *binv;

  savtbl = TXbtreelog_dbtbl;
  TXbtreelog_dbtbl = dbtbl;                     /* for btreelog debug */

  for (i = 0; i < dbtbl->nindex; i++)           /* each B-tree index */
    {
      bindex = &dbtbl->indexes[i];
      if (bindex->a2i == TXA2INDPN &&
          (bindex->a2i = TXadd2indsetup(dbtbl, bindex)) == TXA2INDPN)
        goto err;
      switch (TXaddtoindChangeLoc(bindex->a2i, newLoc, 0))
        {
        case 2:                                 /* found and changed */
          break;
        case 1:                                 /* not found */
          /* All table recids must be in each B-tree index,
           * so not finding one is an error:
           */
          putmsg(MERR + FWE, fn, couldNotUpdate,
                 (EPI_HUGEINT)TXgetoff(&dbtbl->recid),
                 (EPI_HUGEINT)TXgetoff2(&newLoc), getdbffn(bindex->btree->dbf));
          goto err;
        case 0:                                 /* error */
          goto err;
        }
    }

  for (i = 0; i < dbtbl->ninv; i++)             /* each inverted index */
    {
      binv = &dbtbl->invidx[i];
      switch (TXaddtoindChangeLocInverted(binv, dbtbl->recid, newLoc))
        {
        case 2:                                 /* found and changed */
          break;
        case 1:                                 /* not found */
          /* All table recids must be in each inverted index,
           * so not finding one is an error:
           */
          putmsg(MERR + FWE, fn, couldNotUpdate,
                 (EPI_HUGEINT)TXgetoff(&dbtbl->recid),
                 (EPI_HUGEINT)TXgetoff2(&newLoc), getdbffn(binv->btree->dbf));
          goto err;
        case 0:                                 /* error */
          goto err;
        }
    }

  /* No `tb->dbies' are open, due to `mmViaFdbi == 1' passed to
   * TXgetindexes().  Metamorph indexes were instead opened as FDBIs
   * in `fdbies' array:
   */
  for (i = 0; i < dbtbl->nfdbi; i++)            /* each Metamorph index */
    {
      fdbi = dbtbl->fdbies[i];
      if (!TXfdbiChangeLoc(dbtbl, fdbi, newLoc))
        goto err;
    }

  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;                                      /* error */
done:
  TXbtreelog_dbtbl = savtbl;                    /* for btreelog debug */
  return(ret);
}

/* ------------------------------------------------------------------------ */

static void TXcmpTblAbendCallback ARGS((void *usr));
static void
TXcmpTblAbendCallback(usr)
void    *usr;   /* (in) TXcmpTbl */
{
  TXcmpTbl      *ct = (TXcmpTbl *)usr;
  int           fd, i;

  if (ct->outputBtrees != BTREEPPN)
    {
      for (i = 0; i < ct->numBtrees; i++)
        {
          fd = getdbffh(ct->outputBtrees[i]->dbf);
          if (fd > TX_NUM_STDIO_HANDLES) close(fd);
          unlink(getdbffn(ct->outputBtrees[i]->dbf));
          /* No temp PID removal: lets rmindex()/TXdocleanup() remove
           * SYS... entry later
           */
        }
    }

  if (ct->outputInverteds != BTREEPPN)
    {
      for (i = 0; i < ct->numInverteds; i++)
        {
          fd = getdbffh(ct->outputInverteds[i]->dbf);
          if (fd > TX_NUM_STDIO_HANDLES) close(fd);
          unlink(getdbffn(ct->outputInverteds[i]->dbf));
          /* No temp PID removal: lets rmindex()/TXdocleanup() remove
           * SYS... entry later
           */
        }
    }

  /* `outputWtixes' handled by WTIX ABEND callback */

  if (ct->outputDbfPath != CHARPN && !ct->overwrite)
    {
      if (ct->outputDbf != DBFPN)
        {
          fd = getdbffh(ct->outputDbf);
          if (fd > TX_NUM_STDIO_HANDLES) close(fd);
        }
      unlink(ct->outputDbfPath);
    }

  if (ct->outputBlobDbfPath != CHARPN && !ct->overwrite)
    {
      if (ct->outputBlobDbf != DBFPN)
        {
          fd = getdbffh(ct->outputBlobDbf);
          if (fd > TX_NUM_STDIO_HANDLES) close(fd);
        }
      unlink(ct->outputBlobDbfPath);
    }

  /* Remove temp PID file last: */
  /* KNG 20101101 Do not remove PID file: lets rmindex()/rmtable()
   * positively see that we have died, and thus TXdocleanup() can
   * remove our entry:
   */
}

/* ------------------------------------------------------------------------ */

/* These return nonzero on success: */
#define CMPTBL_LockTableRead(ct)        \
  (TXlocktable(ct->dbtbl, R_LCK) == 0 ? (ct->tableReadLocks++, 1) : 0)
#define CMPTBL_LockTableWrite(ct)       \
  (TXlocktable(ct->dbtbl, W_LCK) == 0 ? (ct->tableWriteLocks++, 1) : 0)
#define CMPTBL_UnlockTableRead(ct)      \
  (ct->tableReadLocks > 0 ? (TXunlocktable(ct->dbtbl, R_LCK),   \
                             ct->tableReadLocks--, 1) : 0)
#define CMPTBL_UnlockTableWrite(ct)     \
  (ct->tableWriteLocks > 0 ? (TXunlocktable(ct->dbtbl, W_LCK),  \
                              ct->tableWriteLocks--, 1) : 0)

#define CMPTBL_LockIndexRead(ct)                        \
  (TXlockindex(ct->dbtbl, INDEX_READ, NULL) == 0 ?      \
   (ct->indexReadLocks++, 1) : 0)
#define CMPTBL_LockIndexWrite(ct)                       \
  (TXlockindex(ct->dbtbl, INDEX_WRITE, NULL) == 0 ?     \
   (ct->indexWriteLocks++, 1) : 0)
#define CMPTBL_UnlockIndexRead(ct)                                      \
  (ct->indexReadLocks > 0 ? (TXunlockindex(ct->dbtbl, INDEX_READ, NULL),   \
                             ct->indexReadLocks--, 1) : 0)
#define CMPTBL_UnlockIndexWrite(ct)                                     \
  (ct->indexWriteLocks > 0 ? (TXunlockindex(ct->dbtbl, INDEX_WRITE, NULL), \
                              ct->indexWriteLocks--, 1) : 0)

/* ------------------------------------------------------------------------ */

static void TXcmpTblCloseOutputTable ARGS((TXcmpTbl *ct));
static void
TXcmpTblCloseOutputTable(ct)
TXcmpTbl        *ct;
{
  static CONST char     fn[] = "TXcmpTblCloseOutputTable";

  ct->outputBlobDbf = closedbf(ct->outputBlobDbf);
  if (ct->outputBlobDbfPath != CHARPN)
    {
      /* If not overwriting, `outputBlobDbfPath' is a temp file; delete: */
      if (!ct->overwrite)
        tx_delindexfile(MWARN, fn, ct->outputBlobDbfPath, 0);
      ct->outputBlobDbfPath = TXfree(ct->outputBlobDbfPath);
    }

  if (ct->outputDbf != DBFPN)
    ct->outputDbf = closedbf(ct->outputDbf);
  if (ct->outputDbfPath != CHARPN)
    {
      /* If not overwriting, `outputDbfPath' is a temp file; delete: */
      if (!ct->overwrite) tx_delindexfile(MWARN, fn, ct->outputDbfPath, 0);
      if (ct->outputDbfPath != ct->inputDbfPath) TXfree(ct->outputDbfPath);
      ct->outputDbfPath = CHARPN;
    }

  if (ct->outputTableTempPidPath != CHARPN)
    {
      tx_delindexfile(MERR, fn, ct->outputTableTempPidPath, 0);
      ct->outputTableTempPidPath = TXfree(ct->outputTableTempPidPath);
    }
  if (TXrecidvalid(&ct->sysTblTempRow))
    TXdeltablerec(ct->ddic, ct->sysTblTempRow);
}

static int TXcmpTblCreateOutputTable ARGS((TXcmpTbl *ct));
static int
TXcmpTblCreateOutputTable(ct)
TXcmpTbl        *ct;
/* Creates or opens output DBF for table (and blobs if any).
 * Returns nonzero on success.
 */
{
  static CONST char     fn[] = "TXcmpTblCreateOutputTable";
  int                   res, ret, sysTablesLocks = 0, i, hasBlobs = 0;
  void                  *inputBuf;
  size_t                inputBufSz;
  CONST char            *outputDir;
  char                  myType, *path;
  DD                    *dd;
  char                  scratchPath[PATH_MAX];

  /* Make sure we are not colliding with another ALTER TABLE x COMPACT.
   * Just check for any temp tables of `tableName'; no need to check PID,
   * as TXdocleanup() should have removed dead-PID entries:
   */
  if (TXdocleanup(ct->ddic) != 0) goto err;     /* clean up previous runs */
  /* Keep SYSTABLES write-locked for duration of our check *and* temp-table
   * creation, so no one can create a same-name temp table between our
   * check and creation:
   */
  if (TXlocksystbl(ct->ddic, SYSTBL_TABLES, W_LCK, NULL) != 0) goto err;
  sysTablesLocks++;                             /* after successful lock */
  /* Now check for existing table: */
  myType = TEXIS_TEMP_TABLE;
  path = TXddgetanytable(ct->ddic, ct->tableName, &myType, 1);
  if (path != CHARPN)
    {
      putmsg(MERR + UGE, fn, "Table `%s' is already being compacted",
             ct->tableName);
      path = TXfree(path);
      goto err;
    }

  /* Check for blobs: */
  dd = ct->dbtbl->tbl->dd;
  for (i = 0; i < dd->n; i++)
    switch (dd->fd[i].type & DDTYPEBITS)
      {
      case FTN_BLOB:
      case FTN_BLOBI:
        hasBlobs = 1;
        break;
      }

  /* Open a new KDBF (on top of the existing table if `overwrite'),
   * for compacting:
   */
  if (ct->overwrite)
    {
      ct->outputDbfPath = (char *)ct->inputDbfPath;
      ct->outputTablePathSansExt = TXstrdup(TXPMBUFPN, fn, ct->outputDbfPath);
      if (ct->outputTablePathSansExt == CHARPN) goto err;
      *TXfileext(ct->outputTablePathSansExt) = '\0';
      ct->outputDbf = opendbf(TXPMBUFPN, ct->outputDbfPath,
                              (O_RDWR | O_CREAT));
      /* wtf TXcreateTempIndexOrTableEntry() needed?  or not? */
    }
  else                                          /* !overwrite */
    {
      /* Create a temp DBF, to be renamed afterwards.  Respect tablespace: */
      if (ct->ddic->tbspc != CHARPN && *ct->ddic->tbspc != '\0')
        outputDir = ct->ddic->tbspc;
      else
        {
          scratchPath[sizeof(scratchPath) - 1] = 'x'; /* overflow sentinel */
          TXstrncpy(scratchPath, ct->inputDbfPath, sizeof(scratchPath));
          if (scratchPath[sizeof(scratchPath) - 1] != 'x')
            {
              putmsg(MERR + MAE, fn, "Path `%s' too long", ct->inputDbfPath);
              goto err;
            }
          *TXbasename(scratchPath) = '\0';      /* remove filename */
          outputDir = scratchPath;
        }
      res = TXcreateTempIndexOrTableEntry(ct->ddic, outputDir, ct->tableName,
                                          CHARPN, CHARPN, (int)
                                          ddgetnfields(ct->dbtbl->tbl->dd),
                                          1, "Output of ALTER TABLE COMPACT",
                                          CHARPN, &ct->outputTablePathSansExt,
                                          &ct->sysTblTempRow);
      outputDir = CHARPN;                       /* may re-use `scratchPath' */
      if (!res) goto err;
      ct->outputDbfPath = TXstrcat2(ct->outputTablePathSansExt, ".tbl");
      if (ct->outputDbfPath == CHARPN) goto extErr;
      /* Tnnn.PID was created by TXcreateTempIndex...(); note for later: */
      ct->outputTableTempPidPath = TXstrcat2(ct->outputTablePathSansExt,
                                             (char *)TXtempPidExt);
      if (ct->outputTableTempPidPath == CHARPN) goto extErr;
      ct->outputDbf = opendbf(TXPMBUFPN, ct->outputDbfPath,
                              (O_RDWR | O_CREAT));
    }
  if (ct->outputDbf == DBFPN)
    {
      putmsg(MERR + FOE, fn, "Could not %s `%s'",
             (ct->overwrite ? "create" : "re-open"), ct->outputDbfPath);
      goto err;
    }

  /* Open/create .blb file, if blob(s) in the schema: */
  if (hasBlobs)
    {
      if (ct->overwrite)
        {
          /*wtf*/
        }
      else
        {
          ct->outputBlobDbfPath = TXstrcat2(ct->outputTablePathSansExt,
                                            ".blb");
          if (ct->outputBlobDbfPath == CHARPN)
            {
            extErr:
              TXputmsgOutOfMem(TXPMBUFPN, MERR + MAE, fn,
                               strlen(ct->outputTablePathSansExt) + 5, 1);
              goto err;
            }
          ct->outputBlobDbf = opendbf(TXPMBUFPN, ct->outputBlobDbfPath,
                                      (O_RDWR | O_CREAT));
        }
      if (ct->outputBlobDbf == DBFPN)
        {
          putmsg(MERR + FOE, fn, "Could not %s `%s'",
                 (ct->overwrite ? "create" : "re-open"),
                 ct->outputBlobDbfPath);
          goto err;
        }
    }

  /* Done with check-and-create; can unlock SYSTABLES: */
  TXunlocksystbl(ct->ddic, SYSTBL_TABLES, W_LCK);
  sysTablesLocks--;

  /* We need to do some KDBF ioctls on `outputDbf', so fail if not KDBF: */
  if (ct->outputDbf->dbftype != DBF_KAI)
    {
      putmsg(MERR + UGE, fn, "Output DBF `%s' is not KDBF: cannot compact",
             getdbffn(ct->outputDbf));
      goto err;
    }
  if (ct->outputBlobDbf != DBFPN &&
      ct->outputBlobDbf->dbftype != DBF_KAI)
    {
      putmsg(MERR + UGE, fn,
             "Output blob DBF `%s' is not KDBF: cannot compact",
             getdbffn(ct->outputBlobDbf));
      goto err;
    }

  /* Set up some ioctls: */
  if (ct->overwrite)
    {
      /* Set an ioctl to tell the output KDBF it is overwriting the
       * input DBF; no free-tree etc.  This also seeks to 0, so do it
       * before ...SETNEXTOFF below:
       */
      if (ioctldbf(ct->outputDbf, (DBF_KAI | KDBF_IOCTL_OVERWRITE),
                   (void *)1) < 0)
        {
          putmsg(MERR, fn, "Cannot set overwrite mode on output DBF `%s'",
                 getdbffn(ct->outputDbf));
          goto err;
        }
      /* wtf blob dbf */
    }
  else                                          /* !overwrite */
    {
      /* Set some optimization ioctls on `outputDbf': */
      if (ioctldbf(ct->outputDbf, (DBF_KAI|KDBF_IOCTL_APPENDONLY),
                   (void *)1) < 0 ||
          ioctldbf(ct->outputDbf, (DBF_KAI|KDBF_IOCTL_NOREADERS),
                   (void *)1) < 0 ||
          ioctldbf(ct->outputDbf, (DBF_KAI|KDBF_IOCTL_WRITEBUFSZ),
                   (void *)(64*1024)) < 0)
        {
          putmsg(MERR, fn, "Cannot set ioctls on output DBF `%s'",
                 getdbffn(ct->outputDbf));
          goto err;
        }
      if (ct->outputBlobDbf != DBFPN &&
          (ioctldbf(ct->outputBlobDbf, (DBF_KAI|KDBF_IOCTL_APPENDONLY),
                    (void *)1) < 0 ||
           ioctldbf(ct->outputBlobDbf, (DBF_KAI|KDBF_IOCTL_NOREADERS),
                    (void *)1) < 0 ||
           ioctldbf(ct->outputBlobDbf, (DBF_KAI|KDBF_IOCTL_WRITEBUFSZ),
                    (void *)(64*1024)) < 0))
        {
          putmsg(MERR, fn, "Cannot set ioctls on output blob DBF `%s'",
                 getdbffn(ct->outputBlobDbf));
          goto err;
        }
    }

  /* B-tree indexes and Metamorph compound index new lists are keyed
   * by tuple (row fields), not BTLOC.  Therefore we need the DBTBL
   * tuple/fields for each input row in order to update those B-trees;
   * thus we must read here via getdbtblrow() and not getdbf().  But
   * getdbtblrow() skips the first DBF block (data dictionary), thus
   * we must manually read and write that first block with
   * getdbf()/allocdbf() to sync `outputDbf' with `dbtbl':
   */
  inputBuf = getdbf(ct->inputDbf, 0, &inputBufSz);  /* get first block (DD) */
  if (inputBuf == NULL)
    {
      putmsg(MERR + FRE, fn, "Cannot read first block from table");
      goto err;
    }
  /* Write the first block (DD) back, to prime `outputDbf': */
  if (dbfalloc(ct->outputDbf, inputBuf, inputBufSz) == (EPI_OFF_T)(-1))
    {
      putmsg(MERR + FSE, fn, "Cannot write initial output DBF block");
      goto err;
    }
  TXrewinddbtbl(ct->dbtbl);                     /* prep for data read */

  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
  TXcmpTblCloseOutputTable(ct);
done:
  for ( ; sysTablesLocks > 0; sysTablesLocks--)
    TXunlocksystbl(ct->ddic, SYSTBL_TABLES, W_LCK);
  return(ret);
}

static int TXcmpTblMakeOutputTableLive ARGS((TXcmpTbl *ct));
static int
TXcmpTblMakeOutputTableLive(ct)
TXcmpTbl        *ct;
/* Makes new output table live.  Should be called inside proper locks.
 * Returns 0 on error.
 */
{
  /* Close output DBFs (should already be so) so we can transfer them: */
  ct->outputDbf = closedbf(ct->outputDbf);
  ct->outputBlobDbf = closedbf(ct->outputBlobDbf);

  /* Bug 7015: `ct->dbtbl->tbl->dbf' still open; close to avoid
   * delete of open file under Unix (fails on remote Windows filesystem).
   * While `ct->dbtbl' is still needed later (locks to unlock),
   * `ct->dbtbl->tbl' should not be; close it to close open file.
   * WTF drill:
   */
  ct->dbtbl->tbl = closetbl(ct->dbtbl->tbl);

  if (TXtransferIndexOrTable(ct->inputTablePathSansExt,
                             ct->outputTablePathSansExt,
                             ct->ddic, ct->tableName,
                             ct->dbtbl->type, NULL, 0, 0x1) < 0)
    goto err;
  /* Free `ouputDbfPath', so we do not delete it later
   * (no longer exists anyway):
   */
  ct->outputDbfPath = TXfree(ct->outputDbfPath);
  ct->outputBlobDbfPath = TXfree(ct->outputBlobDbfPath);
  ct->outputTablePathSansExt = TXfree(ct->outputTablePathSansExt);
  /* Same for `outputTableTempPidPath', `sysTblTempRow': */
  ct->outputTableTempPidPath = TXfree(ct->outputTableTempPidPath);
  TXsetrecid(&ct->sysTblTempRow, RECID_INVALID);
  return(1);                                    /* success */
err:
  return(0);
}

/* ------------------------------------------------------------------------ */

static void TXcmpTblCloseOutputBtreeIndexes ARGS((TXcmpTbl *ct));
static void
TXcmpTblCloseOutputBtreeIndexes(ct)
TXcmpTbl        *ct;
{
  static CONST char     fn[] = "TXcmpTblCloseOutputBtreeIndexes";
  int                   i, res;
  BTREE                 *btree;
  char                  scratchPath[PATH_MAX];

  if (ct->outputBtrees != BTREEPPN)
    {
      for (i = 0; i < ct->numBtrees; i++)
        {
          if ((btree = ct->outputBtrees[i]) == BTREEPN) continue;
          res = TXcatpath(scratchPath, getdbffn(btree->dbf), "");
          btree->usr = TXclosefldcmp(btree->usr);
          ct->outputBtrees[i] = closebtree(btree);
          if (res)
            {
              tx_delindexfile(MERR, fn, scratchPath, 0);
#ifdef _WIN32
              {
                char    *d;
                d = TXfileext(scratchPath);
                strcpy(d, TXtempPidExt);
                tx_delindexfile(MERR, fn, scratchPath, 0);
              }
#endif /* _WIN32 */
            }
        }
      ct->outputBtrees = TXfree(ct->outputBtrees);
    }
  if (ct->outputBtreeSysTblTempRows != RECIDPN)
    {
      for (i = 0; i < ct->numBtrees; i++)
        if (TXrecidvalid2(&ct->outputBtreeSysTblTempRows[i]))
          TXdelindexrec(ct->ddic, ct->outputBtreeSysTblTempRows[i]);
      ct->outputBtreeSysTblTempRows = TXfree(ct->outputBtreeSysTblTempRows);
    }
  ct->inputBtreePathsSansExt = TXfreeStrList(ct->inputBtreePathsSansExt,
                                             ct->numBtrees);
  ct->numBtrees = 0;
}

static int TXcmpTblCreateOutputBtreeIndexes ARGS((TXcmpTbl *ct));
static int
TXcmpTblCreateOutputBtreeIndexes(ct)
TXcmpTbl        *ct;
/* Creates output B-tree indexes (if not in overwrite mode)
 * and adds temp entries to SYSINDEX if needed.
 */
{
  static CONST char     fn[] = "TXcmpTblCreateOutputBtreeIndexes";
  int                   i, ret;
  size_t                n;
  BTREE                 *inputBtree, *outputBtree;
  DD                    *inputDd, *outputDd;
  FLDCMP                *fc;
  DBTBL                 *dbtbl = ct->dbtbl;
  char                  *path, scratchPath[PATH_MAX];

  if (ct->overwrite) return(1);                 /* wtf what do we do */

  ct->outputBtrees = (BTREE **)TXcalloc(TXPMBUFPN, fn, dbtbl->nindex,
                                        sizeof(BTREE *));
  if (ct->outputBtrees == BTREEPPN) goto err;
  ct->inputBtreePathsSansExt = (char **)TXcalloc(TXPMBUFPN, fn, dbtbl->nindex,
                                                 sizeof(char *));
  if (ct->inputBtreePathsSansExt == CHARPPN) goto err;
  ct->outputBtreeSysTblTempRows = (RECID *)TXcalloc(TXPMBUFPN, fn,
                                                    dbtbl->nindex,
                                                    sizeof(RECID));
  if (ct->outputBtreeSysTblTempRows == RECIDPN) goto err;
  ct->numBtrees = 0;
  for (i = 0; i < dbtbl->nindex; i++)           /* each B-tree index */
    {
      inputBtree = dbtbl->indexes[i].btree;
      ct->inputBtreePathsSansExt[i] = TXstrdup(TXPMBUFPN, fn,
                                               getdbffn(inputBtree->dbf));
      if (ct->inputBtreePathsSansExt[i] == CHARPN) goto err;
      *TXfileext(ct->inputBtreePathsSansExt[i]) = '\0';
      /* wtf respect indexspace: */
      n = TXdirname(TXPMBUFPN, scratchPath, sizeof(scratchPath),
                    ct->inputBtreePathsSansExt[i]);
      if (n == 0) goto err;
      path = CHARPN;
      if (!TXcreateTempIndexOrTableEntry(ct->ddic, scratchPath,
                                         dbtbl->indexNames[i],
                                         ct->tableName,
                                         dbtbl->indexFldNames[i], 0, 0,
                                         CHARPN, dbtbl->indexParams[i], &path,
                                         &ct->outputBtreeSysTblTempRows[i]))
        {
          ct->inputBtreePathsSansExt[i]=TXfree(ct->inputBtreePathsSansExt[i]);
          goto err;
        }
      /* Create temp B-tree same as `inputBtree', but set BT_LINEAR
       * so we can BT_APPEND later:
       */
      ct->outputBtrees[i] = outputBtree = openbtree(path, BT_MAXPGSZ, 20,
                                ((inputBtree->flags & BT_UNIQUE) | BT_LINEAR),
                                              (O_RDWR | O_CREAT | O_EXCL));
      if (outputBtree == BTREEPN)
        {
          putmsg(MERR + FCE, fn,
                 "Could not create output B-tree `%s.btr' for index `%s'",
                 path, dbtbl->indexNames[i]);
          path = TXfree(path);
          ct->inputBtreePathsSansExt[i]=TXfree(ct->inputBtreePathsSansExt[i]);
          TXdelindexrec(ct->ddic, ct->outputBtreeSysTblTempRows[i]);
          TXsetrecid(&ct->outputBtreeSysTblTempRows[i], RECID_INVALID);
          goto err;
        }
      ct->numBtrees++;
      path = TXfree(path);
      if (bttexttoparam(outputBtree, dbtbl->indexParams[i]) < 0) goto err;
      inputDd = btreegetdd(inputBtree);
      if (inputDd != DDPN)
        {
          btreesetdd(outputBtree, inputDd);
          if ((outputDd = btreegetdd(outputBtree)) != DDPN)
            {
              btsetcmp(outputBtree, (btcmptype)fldcmp);
              if (!(fc = TXopenfldcmp(outputBtree, TXOPENFLDCMP_CREATE_FLDOP)))
                goto err;
              outputBtree->usr = fc;
            }
        }
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
  TXcmpTblCloseOutputBtreeIndexes(ct);
done:
  return(ret);
}

static int TXcmpTblMakeOutputBtreeIndexesLive ARGS((TXcmpTbl *ct));
static int
TXcmpTblMakeOutputBtreeIndexesLive(ct)
TXcmpTbl        *ct;
/* Makes new output B-tree indexes live.  Should be called inside
 * proper locks.  Returns 0 on error.
 */
{
  int   i;
  DBTBL *dbtbl = ct->dbtbl;
  char  outputPathSansExt[PATH_MAX];

  if (ct->overwrite) return(1);                 /* wtf */

  for (i = 0; i < ct->numBtrees; i++)
    {
      if (!TXcatpath(outputPathSansExt, getdbffn(ct->outputBtrees[i]->dbf),
                     ""))
        goto err;
      *TXfileext(outputPathSansExt) = '\0';     /* remove .btr extension */
      /* Close the B-tree, to flush it to disk, and prevent deletion at
       * TXcmpTblCloseOutputBtreeIndexes(), and allow transfer to new name:
       */
      ct->outputBtrees[i] = closebtree(ct->outputBtrees[i]);
      if (TXtransferIndexOrTable(ct->inputBtreePathsSansExt[i],
                                 outputPathSansExt, ct->ddic,
                                 dbtbl->indexNames[i],
                                 INDEX_BTREE, NULL, 0, 0x0) < 0)
        goto err;
      /* Set recid invalid, to prevent deletion at ...CloseOutputBtree..: */
      TXsetrecid(&ct->outputBtreeSysTblTempRows[i], RECID_INVALID);
    }
  return(1);                                    /* success */
err:
  return(0);
}

/* ------------------------------------------------------------------------ */

static void TXcmpTblCloseOutputInvertedIndexes ARGS((TXcmpTbl *ct));
static void
TXcmpTblCloseOutputInvertedIndexes(ct)
TXcmpTbl        *ct;
{
  static CONST char     fn[] = "TXcmpTblCloseOutputInvertedIndexes";
  int                   i, res;
  BTREE                 *btree;
  char                  scratchPath[PATH_MAX];

  if (ct->outputInverteds != BTREEPPN)
    {
      for (i = 0; i < ct->numInverteds; i++)
        {
          if ((btree = ct->outputInverteds[i]) == BTREEPN) continue;
          res = TXcatpath(scratchPath, getdbffn(btree->dbf), "");
          ct->outputInverteds[i] = closebtree(btree);
          if (res)
            {
              tx_delindexfile(MERR, fn, scratchPath, 0);
#ifdef _WIN32
              {
                char    *d;
                d = TXfileext(scratchPath);
                strcpy(d, TXtempPidExt);
                tx_delindexfile(MERR, fn, scratchPath, 0);
              }
#endif /* _WIN32 */
            }
        }
      ct->outputInverteds = TXfree(ct->outputInverteds);
    }
  if (ct->outputInvertedSysTblTempRows != RECIDPN)
    {
      for (i = 0; i < ct->numInverteds; i++)
        if (TXrecidvalid2(&ct->outputInvertedSysTblTempRows[i]))
          TXdelindexrec(ct->ddic, ct->outputInvertedSysTblTempRows[i]);
      ct->outputInvertedSysTblTempRows =
        TXfree(ct->outputInvertedSysTblTempRows);
    }
  ct->inputInvertedPathsSansExt = TXfreeStrList(ct->inputInvertedPathsSansExt,
                                                ct->numInverteds);
  ct->numInverteds = 0;
}

static int TXcmpTblCreateOutputInvertedIndexes ARGS((TXcmpTbl *ct));
static int
TXcmpTblCreateOutputInvertedIndexes(ct)
TXcmpTbl        *ct;
/* Creates output inverted indexes (if not in overwrite mode)
 * and adds temp entries to SYSINDEX if needed.
 */
{
  static CONST char     fn[] = "TXcmpTblCreateOutputInvertedIndexes";
  int                   i, ret, btFlags;
  size_t                n;
  BTREE                 *inputInverted, *outputInverted;
  DBTBL                 *dbtbl = ct->dbtbl;
  char                  *path, scratchPath[PATH_MAX];

  if (ct->overwrite) return(1);                 /* wtf what do we do */

  ct->outputInverteds = (BTREE **)TXcalloc(TXPMBUFPN, fn, dbtbl->ninv,
                                           sizeof(BTREE *));
  if (ct->outputInverteds == BTREEPPN) goto err;
  ct->inputInvertedPathsSansExt = (char **)TXcalloc(TXPMBUFPN, fn, dbtbl->ninv,
                                                    sizeof(char *));
  if (ct->inputInvertedPathsSansExt == CHARPPN) goto err;
  ct->outputInvertedSysTblTempRows = (RECID *)TXcalloc(TXPMBUFPN, fn,
                                                       dbtbl->ninv,
                                                       sizeof(RECID));
  if (ct->outputInvertedSysTblTempRows == RECIDPN) goto err;
  ct->numInverteds = 0;
  for (i = 0; i < dbtbl->ninv; i++)             /* each inverted index */
    {
      inputInverted = dbtbl->invidx[i].btree;
      ct->inputInvertedPathsSansExt[i] = TXstrdup(TXPMBUFPN, fn,
                                                getdbffn(inputInverted->dbf));
      if (ct->inputInvertedPathsSansExt[i] == CHARPN) goto err;
      *TXfileext(ct->inputInvertedPathsSansExt[i]) = '\0';
      /* wtf respect indexspace: */
      n = TXdirname(TXPMBUFPN, scratchPath, sizeof(scratchPath),
                    ct->inputInvertedPathsSansExt[i]);
      if (n == 0) goto err;
      path = CHARPN;
      if (!TXcreateTempIndexOrTableEntry(ct->ddic, scratchPath,
                                         dbtbl->invertedIndexNames[i],
                                         ct->tableName,
                                         dbtbl->invertedIndexFldNames[i], 0,
                                         0, CHARPN,
                                         dbtbl->invertedIndexParams[i], &path,
                                        &ct->outputInvertedSysTblTempRows[i]))
        {
          ct->inputInvertedPathsSansExt[i] =
            TXfree(ct->inputInvertedPathsSansExt[i]);
          goto err;
        }
      /* Create temp B-tree same as `inputInverted', but set BT_LINEAR
       * so we can BT_APPEND later, iff big-endian (see comment in
       * TXcmpTblTranslateBtreeAndInvertedIndexes()):
       */
      btFlags = (inputInverted->flags & BT_UNIQUE);
#ifdef EPI_BIG_ENDIAN
      btFlags |= BT_LINEAR;
#endif /* EPI_BIG_ENDIAN */
      ct->outputInverteds[i] = outputInverted =
        openbtree(path, BT_MAXPGSZ, 20, btFlags, (O_RDWR | O_CREAT | O_EXCL));
      if (outputInverted == BTREEPN)
        {
          putmsg(MERR + FCE, fn,
                 "Could not create output B-tree `%s.btr' for index `%s'",
                 path, dbtbl->invertedIndexNames[i]);
          path = TXfree(path);
          ct->inputInvertedPathsSansExt[i] =
            TXfree(ct->inputInvertedPathsSansExt[i]);
          TXdelindexrec(ct->ddic, ct->outputInvertedSysTblTempRows[i]);
          TXsetrecid(&ct->outputInvertedSysTblTempRows[i], RECID_INVALID);
          goto err;
        }
      ct->numInverteds++;
      path = TXfree(path);
      if (bttexttoparam(outputInverted, dbtbl->invertedIndexParams[i]) < 0)
        goto err;
      /* no DD associated with inverted indexes */
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
  TXcmpTblCloseOutputInvertedIndexes(ct);
done:
  return(ret);
}

static int TXcmpTblMakeOutputInvertedIndexesLive ARGS((TXcmpTbl *ct));
static int
TXcmpTblMakeOutputInvertedIndexesLive(ct)
TXcmpTbl        *ct;
/* Makes new output inverted indexes live.  Should be called inside
 * proper locks.  Returns 0 on error.
 */
{
  int   i;
  DBTBL *dbtbl = ct->dbtbl;
  char  outputPathSansExt[PATH_MAX];

  if (ct->overwrite) return(1);                 /* wtf */

  for (i = 0; i < ct->numInverteds; i++)
    {
      if (!TXcatpath(outputPathSansExt, getdbffn(ct->outputInverteds[i]->dbf),
                     ""))
        goto err;
      *TXfileext(outputPathSansExt) = '\0';     /* remove .btr extension */
      /* Close the B-tree, to flush it to disk, and prevent deletion at
       * TXcmpTblCloseOutputInvertedIndexes(), and allow transfer to new name:
       */
      ct->outputInverteds[i] = closebtree(ct->outputInverteds[i]);
      if (TXtransferIndexOrTable(ct->inputInvertedPathsSansExt[i],
                                 outputPathSansExt, ct->ddic,
                                 dbtbl->invertedIndexNames[i],
                                 INDEX_INV, NULL, 0, 0x0) < 0)
        goto err;
      /* Set recid invalid, to prevent deletion at ...CloseOutputInverted..: */
      TXsetrecid(&ct->outputInvertedSysTblTempRows[i], RECID_INVALID);
    }
  return(1);                                    /* success */
err:
  return(0);
}

/* ------------------------------------------------------------------------ */

static void TXcmpTblCloseOutputMetamorphIndexes ARGS((TXcmpTbl *ct));
static void
TXcmpTblCloseOutputMetamorphIndexes(ct)
TXcmpTbl        *ct;
{
  static CONST char     fn[] = "TXcmpTblCloseOutputMetamorphIndexes";
  int                   i, res;
  WTIX                  *wtix;
  char                  *path;
  char                  scratchPath[PATH_MAX];

  if (ct->outputWtixes != WTIXPPN)
    {
      for (i = 0; i < ct->numOutputWtixes; i++)
        {
          if ((wtix = ct->outputWtixes[i]) == WTIXPN) continue;
          path = TXwtixGetNewTokenPath(wtix);
          if (path != CHARPN)                   /* have a new/temp file */
            res = TXcatpath(scratchPath, path, "");
          else
            res = 0;
          ct->outputWtixes[i] = wtix = closewtix(wtix);
          if (res)
            {
              tx_delindexfile(MERR, fn, scratchPath, 0);
#ifdef _WIN32
              strcpy(TXfileext(scratchPath), TXtempPidExt);
              tx_delindexfile(MERR, fn, scratchPath, 0);
#endif /* _WIN32 */
            }
        }
      ct->outputWtixes = TXfree(ct->outputWtixes);
    }
  if (ct->outputWtixSysTblTempRows != RECIDPN)
    {
      for (i = 0; i < ct->numOutputWtixes; i++)
        if (TXrecidvalid2(&ct->outputWtixSysTblTempRows[i]))
          TXdelindexrec(ct->ddic, ct->outputWtixSysTblTempRows[i]);
      ct->outputWtixSysTblTempRows = TXfree(ct->outputWtixSysTblTempRows);
    }
  ct->inputFdbiAuxFldSizes = TXfree(ct->inputFdbiAuxFldSizes);
  ct->fdbiIndexNames = TXfreeStrList(ct->fdbiIndexNames, ct->numOutputWtixes);
  ct->fdbiTokenPaths = TXfreeStrList(ct->fdbiTokenPaths, ct->numOutputWtixes);
  ct->fdbiIndexFlags = TXfree(ct->fdbiIndexFlags);
  ct->numOutputWtixes = 0;
}

static int TXcmpTblCreateOutputMetamorphIndexes ARGS((TXcmpTbl *ct));
static int
TXcmpTblCreateOutputMetamorphIndexes(ct)
TXcmpTbl        *ct;
/* Creates output Metamorph index .tok files (if not in overwrite mode),
 * and temp SYSINDEX entries if needed.
 * Returns nonzero on success.
 */
{
  static CONST char     fn[] = "TXcmpTblCreateOutputMetamorphIndexes";
  int                   i, ret;
  size_t                n, smallestAuxFldSz;
  FDBI                  *fdbi;
  A3DBI                 *dbi;
  WTIX                  *wtix;
  DBTBL                 *dbtbl = ct->dbtbl;
  char                  *tmpPath = CHARPN, orgPath[PATH_MAX];
  TXfdbiIndOpts         fdbiOptions;

  ct->outputWtixes = (WTIX **)TXcalloc(TXPMBUFPN, fn, dbtbl->nfdbi,
                                       sizeof(WTIX *));
  if (ct->outputWtixes == WTIXPPN) goto err;
  ct->outputWtixSysTblTempRows = (RECID *)TXcalloc(TXPMBUFPN, fn,
                                                   dbtbl->nfdbi,
                                                   sizeof(RECID));
  if (ct->outputWtixSysTblTempRows == RECIDPN) goto err;
  ct->inputFdbiAuxFldSizes = (size_t *)TXcalloc(TXPMBUFPN, fn,
                                                dbtbl->nfdbi, sizeof(size_t));
  if (ct->inputFdbiAuxFldSizes == SIZE_TPN) goto err;
  ct->fdbiIndexNames = TXdupStrList(TXPMBUFPN, dbtbl->fdbiIndexNames,
                                    dbtbl->nfdbi);
  if (!ct->fdbiIndexNames) goto err;
  ct->fdbiTokenPaths = (char **)TXcalloc(TXPMBUFPN, __FUNCTION__,
                                         dbtbl->nfdbi + 1, sizeof(char *));
  if (!ct->fdbiTokenPaths) goto err;
  ct->fdbiIndexFlags = (FDF *)TXcalloc(TXPMBUFPN, __FUNCTION__,
                                       dbtbl->nfdbi, sizeof(FDF));
  if (!ct->fdbiIndexFlags) goto err;

  /* Find Metamorph index with smallest aux data size.  This will
   * use the least mem when we keep its original token file in mem
   * for B-tree/inverted index recid translation later:
   */
  smallestAuxFldSz = EPI_OS_SIZE_T_MAX;
  for (i = 0; i < dbtbl->nfdbi; i++)
    {
      fdbi = dbtbl->fdbies[i];
      ct->inputFdbiAuxFldSizes[i] = TXfdbiGetAuxFieldsSize(fdbi);
      if (ct->inputFdbiAuxFldSizes[i] < smallestAuxFldSz)
        {
          ct->wtixRecidTranslatorIdx = i;
          smallestAuxFldSz = ct->inputFdbiAuxFldSizes[i];
        }
    }
  if (ct->wtixRecidTranslatorIdx < 0 &&         /* no Metamorph index found */
      (dbtbl->nindex > 0 || dbtbl->ninv > 0))   /* and we need one for xlat */
    {
      /* WTF open our own WTIX: */
      putmsg(MERR + UGE, fn,
             "Cannot compact table `%s': A Metamorph index must also exist if B-tree/inverted indexes exist",
             ct->tableName);
      goto err;
    }

  /* WTF for Bug 7015 we would like to closeindexes() ASAP, but we
   * need `dbtbl->fdbies' later, so leave open for now (close in
   * TXcmpTblMakeOutputMetamorphIndexesLive())
   */
  ct->numOutputWtixes = 0;
  for (i = 0; i < dbtbl->nfdbi; i++)
    {                                           /* each Metamorph index */
      /* Note that TXcmpTblAppendRowToMetamorphIndexes() assumes a
       * `ct->outputWtixes' entry for each input `dbtbl->fdbies' entry:
       */
      fdbi = dbtbl->fdbies[i];
      dbi = fdbi_getdbi(fdbi);
      /* Copy path for later use after closing `fdbi': */
      ct->fdbiTokenPaths[i] = TXstrdup(TXPMBUFPN, __FUNCTION__,
                                       TXfdbiGetTokenPath(fdbi));
      if (!ct->fdbiTokenPaths[i]) goto err;
      ct->fdbiIndexFlags[i] = TXfdbiGetFlags(fdbi);
      /* Cannot respect indexspace: we are only mucking with .tok file.
       * WTF Bug 3684: we are "changing" (just copying) .btr and .dat too;
       * could respect indexspace:
       */
      n = TXdirname(TXPMBUFPN, orgPath, sizeof(orgPath),
                    ct->fdbiTokenPaths[i]);
      if (n == 0) goto err;
      if (!TXcreateTempIndexOrTableEntry(ct->ddic, orgPath,
                                         dbtbl->fdbiIndexNames[i],
                                         ct->tableName,
                                         dbtbl->fdbiIndexFldNames[i], 0,
                                         0, CHARPN,
                                         dbtbl->fdbiIndexParams[i], &tmpPath,
                                         &ct->outputWtixSysTblTempRows[i]))
        goto err;
      /* We will need the `ct->wtixRecidTranslatorIdx' index's
       * original token file later for B-tree/inverted index recid
       * translation, so open its original file, and then
       * wtix_setupdname() for the temp/output index.  Bug 3684:
       * do this for all Metamorph indexes, since we need to copy .btr
       * and .dat anyway:
       */
      if (!TXcatpath(orgPath, ct->fdbiTokenPaths[i], ""))
        goto err;
      *TXfileext(orgPath) = '\0';           /* remove extension */
      TXfdbiIndOpts_INIT_FROM_DBI(&fdbiOptions, dbi);
      wtix = openwtix(dbtbl, dbtbl->fdbiIndexFldNames[i], orgPath,
                      ct->inputFdbiAuxFldSizes[i], &fdbiOptions,
                      &dbi->paramTblInfo,
                      /* WTIXF_LOADTOK on top of WTIXF_COMPACT
                       * tells wtix_finish() to read in all of the
                       * new token file, for later translation calls:
                       */
                      ((WTIXF_COMPACT | WTIXF_UPDATE) |
                (i == ct->wtixRecidTranslatorIdx ? WTIXF_LOADTOK : (WTIXF)0)),
                      0, WTIXPN);
      if (wtix == WTIXPN)
        {
          putmsg(MERR + FOE, fn,
                 "Could not open index `%s' for token update",
                 dbtbl->fdbiIndexNames[i]);
          goto err;
        }
      if (!wtix_setupdname(wtix, tmpPath))
        {
          putmsg(MERR + FCE, fn,
                 "Could not create output token file `%s" FDBI_TOKSUF
                 "' for index `%s'", tmpPath, dbtbl->fdbiIndexNames[i]);
          goto err;
        }
      ct->outputWtixes[i] = wtix;
      ct->numOutputWtixes++;
      tmpPath = TXfree(tmpPath);
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
  TXcmpTblCloseOutputMetamorphIndexes(ct);
done:
  tmpPath = TXfree(tmpPath);
  return(ret);
}

static int TXcmpTblAppendRowToMetamorphIndexes ARGS((TXcmpTbl *ct,
                                                     RECID outputRecid));
static int
TXcmpTblAppendRowToMetamorphIndexes(ct, outputRecid)
TXcmpTbl        *ct;
RECID           outputRecid;    /* (in) RECID just written to output table */
/* Appends current row's recid (and aux data) to output Metamorph index
 * token files.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXcmpTblAppendRowToMetamorphIndexes";
  int                   i;
  FDBI                  *inputFdbi;
  WTIX                  *outputWtix;
  RECID                 inputRecid;
  void                  *inputAuxData;

  for (i = 0; i < ct->numOutputWtixes; i++)     /* for each Metamorph index */
    {
      inputFdbi = ct->dbtbl->fdbies[i];
      /* Get current row's recid and aux data from this input index: */
      inputRecid = TXfdbiGetRecidAndAuxData(inputFdbi, ct->curWtixToken,
                                            NULL, &inputAuxData);
      if (!TXrecidvalid2(&inputRecid))
        {
          putmsg(MERR + FRE, fn,
                "Cannot get recid/aux data for token %wd of index `%s'",
                 (EPI_HUGEINT)ct->curWtixToken, ct->fdbiTokenPaths[i]);
          goto err;
        }
      /* Write *output* table's recid, with *input* index's aux data,
       * to this output index:
       */
      outputWtix = ct->outputWtixes[i];
      if (!TXwtixCreateNextToken(outputWtix, outputRecid, inputAuxData))
        goto err;
    }
  return(1);                                    /* success */
err:
  return(0);                                    /* failure */
}

static int TXcmpTblTranslateBtreeAndInvertedIndexes ARGS((TXcmpTbl *ct));
static int
TXcmpTblTranslateBtreeAndInvertedIndexes(ct)
TXcmpTbl        *ct;
/* Translate B-tree/inverted index translation.  Must be called after
 * table compaction and Metamorph output indexes created.
 */
{
  static CONST char     fn[] = "TXcmpTblTranslateBtreeAndInvertedIndexes";
  int                   ret, i;
  int                   numIndexesToTranslate;
  EPI_HUGEINT           numIndexRows;
  WTIX                  *wtixTrans;             /* output WTIX for translate*/
  BTREE                 *inputBtree, *inputInverted;
  BTLOC                 inputLoc;
  RECID                 outputRecid, inputRecid;
  DBTBL                 *dbtbl = ct->dbtbl;
  char                  labelTmp[256];
  byte                  inputKeyBuf[BT_REALMAXPGSZ];
  size_t                inputKeySz;

  /* Return immediately if nothing to do: */
  numIndexesToTranslate = dbtbl->nindex + dbtbl->ninv;
  if (numIndexesToTranslate == 0) goto ok;

  /* Open a meter for index translation.  We can only estimate the
   * meter total, because a given index may not have the same number
   * of rows as the table (e.g. if strlst expansion or NULL fields).
   * We try to fix this as we go along, with btgetpercentage():
   */
  if (ct->meter != METERPN)
    {
      meter_end(ct->meter);
      ct->meter = closemeter(ct->meter);
    }
  ct->meterTotal = ((EPI_HUGEINT)numIndexesToTranslate)*ct->numInputTableRows;
  htsnpf(labelTmp, sizeof(labelTmp),
         "Translating B-tree and inverted indexes for table %s:",
         ct->tableName);
  ct->meter = openmeter(labelTmp, TXcompactmeter, MDOUTFUNCPN, MDFLUSHFUNCPN,
                        NULL, ct->meterTotal);
  if (ct->meter == METERPN) goto err;

  /* Get the WTIX and FDBI to use for recid translation: */
  if (ct->wtixRecidTranslatorIdx >= 0)
    wtixTrans = ct->outputWtixes[ct->wtixRecidTranslatorIdx];
  else
    {
      putmsg(MERR, fn,
           "Internal error: No Metamorph index to use for index translation");
      goto err;
    }

  /* Translate B-tree and inverted indexes.  We read them serially,
   * and use `wtixTrans' old/new token files to map their recids to
   * the new table.  Do them one at a time, since they may have varying
   * numbers of records (due to strlst-splitting, or NULLs).
   */
  numIndexRows = 0;
  for (i = 0; i < dbtbl->nindex; i++)           /* for each B-tree index */
    {                                           /* WTF locks? */
      inputBtree = dbtbl->indexes[i].btree;
      rewindbtree(inputBtree);
      while (inputKeySz = sizeof(inputKeyBuf),
             inputLoc = btgetnext(inputBtree, &inputKeySz, inputKeyBuf, NULL),
             TXrecidvalid2(&inputLoc))          /* for each row in index */
        {
          /* Map the input recid to output recid: */
          TXsetrecid(&inputRecid, TXgetoff2(&inputLoc));
          outputRecid = TXwtixMapOldRecidToNew(wtixTrans, inputRecid);
          if (!TXrecidvalid2(&outputRecid))
            {
              putmsg(MERR, fn,
                "Cannot map input recid 0x%wd to output recid for index `%s'",
                   (EPI_HUGEINT)TXgetoff2(&inputRecid), dbtbl->indexNames[i]);
              goto err;
            }
          /* Write the row to the output B-tree: */
          if (btappend(ct->outputBtrees[i], &outputRecid, (int)inputKeySz,
                       inputKeyBuf, 90, BTBMPN) != 0)
            goto err;
          numIndexRows++;
          /* WTF correct this for indexes whose row count != table rows: */
          METER_UPDATEDONE(ct->meter, numIndexRows);
        }
    }
  for (i = 0; i < dbtbl->ninv; i++)             /* for each inverted index */
    {
      inputInverted = dbtbl->invidx[i].btree;
      rewindbtree(inputInverted);
      while (inputKeySz = sizeof(inputKeyBuf),
             inputLoc = btgetnext(inputInverted,&inputKeySz,inputKeyBuf,NULL),
             TXrecidvalid2(&inputLoc))          /* for each row in index */
        /* WTF can we trust TXrecidvalid(&inputLoc) to detect EOF for
         * inverted, since inputLoc is actually the data not RECID?
         */
        {
          /* Map the input recid to output recid.  For inverted indexes,
           * the recid is the data and vice versa:
           */
          if (inputKeySz != sizeof(RECID))
            {
              putmsg(MERR + FRE, fn,
                     "Invalid size %d for key in inverted index `%s'",
                     (int)inputKeySz, dbtbl->invertedIndexNames[i]);
              goto err;
            }
          memcpy(&inputRecid, inputKeyBuf, sizeof(RECID));
          outputRecid = TXwtixMapOldRecidToNew(wtixTrans, inputRecid);
          if (!TXrecidvalid2(&outputRecid))
            {
              putmsg(MERR, fn,
       "Cannot map input recid 0x%wd to output recid for inverted index `%s'",
                     (EPI_HUGEINT)TXgetoff2(&inputRecid),
                     dbtbl->invertedIndexNames[i]);
              goto err;
            }
          /* Write the row to the output B-tree: */
          TXsetrecid(&inputRecid, TXgetoff2(&inputLoc)); /* actually data */
          /* We must use btinsert(), not btappend(), with inverted
           * indexes on little-endian platforms, as the sort order may
           * change: sort function is memcmp, and data is recids:
           */
#ifdef EPI_BIG_ENDIAN
          if (btappend(ct->outputInverteds[i], &inputRecid, sizeof(RECID),
                       &outputRecid, 90, BTBMPN) != 0)
            goto err;
#else /* !EPI_BIG_ENDIAN */
          if (btinsert(ct->outputInverteds[i], &inputRecid, sizeof(RECID),
                       &outputRecid) != 0)
            goto err;
#endif /* !EPI_BIG_ENDIAN */
          numIndexRows++;
          /* WTF correct this for indexes whose row count != table rows: */
          METER_UPDATEDONE(ct->meter, numIndexRows);
        }
    }

  meter_updatedone(ct->meter, ct->meterTotal);  /* finish the meter */
  meter_end(ct->meter);
  ct->meter = closemeter(ct->meter);
ok:
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  if (ct->meter != METERPN)
    {
      meter_end(ct->meter);
      ct->meter = closemeter(ct->meter);
    }
  return(ret);
}

static int TXcmpTblMakeOutputMetamorphIndexesLive ARGS((TXcmpTbl *ct));
static int
TXcmpTblMakeOutputMetamorphIndexesLive(ct)
TXcmpTbl        *ct;
/* Makes new output metamorph indexes live.  Should be called inside
 * proper locks.  Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXcmpTblMakeOutputMetamorphIndexesLive";
  int                   i, ret, res;
  WTIX                  *wtix;
  char                  *path, inputPath[PATH_MAX], outputPath[PATH_MAX];

  if (ct->overwrite) return(1);                 /* wtf */

  /* Bug 7015: close other open handles to Metamorph index files
   * before TXtransferIndexOrTable(), to avoid potential delete of
   * open file under Unix (fails on remote Windows filesystem):
   */
  TXcloseFdbiIndexes(ct->dbtbl);

  for (i = 0; i < ct->numOutputWtixes; i++)
    {
      wtix = ct->outputWtixes[i];
      if (!TXcatpath(inputPath, ct->fdbiTokenPaths[i], ""))
        goto err;
      *TXfileext(inputPath) = '\0';             /* remove file extension */
      /* The WTIX was opened on the input .tok file, and had the
       * output (temp Tnnn) file set via wtix_setupdname():
       */
      path = TXwtixGetNewTokenPath(wtix);
      if (path == CHARPN)
        {
          putmsg(MERR, fn,
                 "Internal error: New token path missing for index `%s'",
                 ct->fdbiIndexNames[i]);
          goto err;
        }
      if (!TXcatpath(outputPath, path, "")) goto err;
      *TXfileext(outputPath) = '\0';            /* remove file extension */
      /* Close the token file, to flush it to disk, and prevent deletion at
       * TXcmpTblCloseOutputMetamorphIndexes(), and allow transfer to
       * new name(s):
       */
      ct->outputWtixes[i] = wtix = closewtix(ct->outputWtixes[i]);
      res = TXtransferIndexOrTable(inputPath, outputPath, ct->ddic,
                                   ct->fdbiIndexNames[i],
                                   ((ct->fdbiIndexFlags[i] & FDF_FULL) ?
                                    INDEX_FULL : INDEX_MM), NULL, 0, 0x0);
      if (res < 0) goto err;
      /* Set recid invalid, to prevent deletion at ...CloseOutput..: */
      TXsetrecid(&ct->outputWtixSysTblTempRows[i], RECID_INVALID);
    }
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  return(ret);
}

/* ------------------------------------------------------------------------ */

static int TXcmpTblCompactTableAndTranslateMetamorphIndexes ARGS((TXcmpTbl
                                                                  *ct));
static int
TXcmpTblCompactTableAndTranslateMetamorphIndexes(ct)
TXcmpTbl        *ct;
/* Compacts table DBF and translates Metamorph index token files.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] =
    "TXcmpTblCompactTableAndTranslateMetamorphIndexes";
  EPI_STAT_S            st;
  DBF                   *dbf;
  int                   ret, i, dataChangedThisRow, gotIndexWriteLock = 0;
  FLD                   *fld;
  RECID                 *inputRecidPtr, outputRecid;
  EPI_OFF_T             inputOffset, outputOffset;
  void                  *blobData = NULL, *fldData;
  size_t                blobDataSz, fldDataSz, outputDataSz;
  ft_blobi              *blobi;
  DBTBL                 *dbtbl = ct->dbtbl;
  TBL                   *tbl = ct->dbtbl->tbl;
  char                  labelTmp[256];

  /* Open a meter: - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  TXseterror(0);
  if (EPI_FSTAT(getdbffh(ct->inputDbf), &st) != 0)
    {
      putmsg(MERR + FTE, fn, "Cannot stat `%s': %s",
             ct->inputDbfPath, TXstrerror(TXgeterror()));
      goto err;
    }

  ct->meterTotal = (EPI_HUGEINT)(st.st_size - sizeof(KDBF_START));
  htsnpf(labelTmp, sizeof(labelTmp), "Compacting table %s:", ct->tableName);
  ct->meter = openmeter(labelTmp, TXcompactmeter, MDOUTFUNCPN, MDFLUSHFUNCPN,
                        NULL, ct->meterTotal);
  if (ct->meter == METERPN) goto err;

  /* Compact the table: - - - - - - - - - - - - - - - - - - - - - - - - - */
  ct->curWtixToken = 0;
  ct->numInputTableRows = 0;
  while ((inputRecidPtr = getdbtblrow(dbtbl)) != RECIDPN &&
         TXrecidvalid2(inputRecidPtr))
    {                                           /* while not input table EOF*/
      ct->numInputTableRows++;
      inputOffset = TXgetoff2(inputRecidPtr);   /* save for later */
      dataChangedThisRow = 0;

      /* Copy blobs, before table row so we have updated blob offsets: */
      if (ct->outputBlobDbf != DBFPN)
        {
          for (i = 0; i < (int)tbl->n; i++)     /* for each table field */
            {
              fld = tbl->field[i];
              fldData = getfld(fld, &fldDataSz);
              switch (fld->type & DDTYPEBITS)
                {
                case FTN_BLOBI:
                  blobi = (ft_blobi *)fldData;
                  blobData = TXblobiGetPayload(blobi, &blobDataSz);
                  if (blobData == NULL)
                    {
                      putmsg(MERR + FRE, fn, "Cannot get blob");
                      goto err;
                    }
                  blobi->off = putdbf(ct->outputBlobDbf, -1, blobData,
                                      blobDataSz);
                  TXblobiFreeMem(blobi);
                  if (blobi->off == (EPI_OFF_T)(-1))
                    {
                      putmsg(MERR + FWE, fn, "Cannot write blob");
                      goto err;
                    }
                  /* `fld->storage' is what fldtobuf() will read (below),
                   * so update it too.  wtf drill:
                   */
                  if (fld->storage &&
                      fld->storage->type == FTN_BLOB &&
                      fld->storage->v)
                    memcpy(fld->storage->v, &blobi->off, sizeof(EPI_OFF_T));
                  dataChangedThisRow = 1;
                  break;
                }
            }
        }

      /* Write input table's data to output table.  Need to call fldtobuf()
       * if we changed anything (i.e. blob fields), and call it directly
       * since puttblrow() would write to `tbl->df' not `ct->outputDbf'
       * as we need:
       */
      ct->curWtixToken++;
      if (dataChangedThisRow)                   /* reconvert FLDs to buf */
        {
          outputDataSz = fldtobuf(tbl);
          if (outputDataSz == (size_t)(-1)) goto err;
          outputOffset = dbfalloc(ct->outputDbf, tbl->orec + tbl->prebufsz,
                                  outputDataSz);
        }
      else                                      /* can re-use input buf */
        outputOffset = dbfalloc(ct->outputDbf, tbl->irec, tbl->irecsz);
      if (outputOffset == (EPI_OFF_T)RECID_INVALID)
        goto err;       /* WTF if `overwrite', table may now be corrupt */
      TXsetrecid(&outputRecid, outputOffset);

      /* Update indexes: */
      if (ct->overwrite)
        {
          /* Update indexes, if the recid has changed (almost certainly): */
          if (outputOffset != inputOffset &&
              !TXchangeLocInIndices(dbtbl, outputRecid))
            goto err;
        }
      else
        {
          if (!TXcmpTblAppendRowToMetamorphIndexes(ct, outputRecid))
            goto err;
          /* B-tree and inverted indexes updated after table is done */
        }

      /* Update meter: */
      METER_UPDATEDONE(ct->meter, (EPI_HUGEINT)inputOffset);
    }

  /* Close `outputDbf', which will flush the start pointers (and
   * truncate the file if `overwrite'):
   */
  if (ct->overwrite && !CMPTBL_LockTableWrite(ct)) goto err;
  /* Clear `outputDbf' before closing it, in case ABEND fires during close: */
  dbf = ct->outputDbf;
  ct->outputDbf = DBFPN;
  dbf = closedbf(dbf);
  /* Same for `outputBlobDbf': */
  dbf = ct->outputBlobDbf;
  ct->outputBlobDbf = DBFPN;
  dbf = closedbf(dbf);
  if (ct->overwrite) (void)CMPTBL_UnlockTableWrite(ct);
  /* Check for input table read errors: */
  if (TXkdbfGetLastError((KDBF *)TXdbfGetObj(ct->inputDbf)) != 0)
    goto err;
  /* Close the table compaction meter, before flushing indexes since
   * their meter may yap:
   */
  meter_updatedone(ct->meter, ct->meterTotal);
  meter_end(ct->meter);
  ct->meter = closemeter(ct->meter);
  /* Flush the output Metamorph indexes (token files).
   * Do during a write lock, because TXwtixCopyBtrDatNewDel() requires it WTF:
   */
  if (!CMPTBL_LockIndexWrite(ct)) goto err;
  gotIndexWriteLock = 1;
  for (i = 0; i < ct->numOutputWtixes; i++)
    if (!wtix_finish(ct->outputWtixes[i])) goto err;
  ret = 1;                                      /* success */
  goto done;

err:
  ret = 0;
done:
  if (gotIndexWriteLock) (void)CMPTBL_UnlockIndexWrite(ct);
  return(ret);
}

/* ------------------------------------------------------------------------ */

static TXcmpTbl *TXcmpTblClose ARGS((TXcmpTbl *ct));
static TXcmpTbl *
TXcmpTblClose(ct)
TXcmpTbl        *ct;
/* Closes and frees `ct'.
 */
{
  static CONST char     fn[] = "TXcmpTblClose";

  if (ct == TXcmpTblPN) goto done;

  if (ct->abendCbRegistered)
    {
      TXdelabendcb(TXcmpTblAbendCallback, ct);
      ct->abendCbRegistered = 0;
    }

  TXcmpTblCloseOutputTable(ct);
  TXcmpTblCloseOutputBtreeIndexes(ct);
  TXcmpTblCloseOutputInvertedIndexes(ct);
  TXcmpTblCloseOutputMetamorphIndexes(ct);

  /* Locks; index locks are outside table locks: - - - - - - - - - - - - - */
  while (ct->tableWriteLocks > 0)
    {
      if (ct->dbtbl != DBTBLPN)
        (void)CMPTBL_UnlockTableWrite(ct);
      else
        putmsg(MERR, fn, "Orphaned table write lock");
    }
  while (ct->tableReadLocks > 0)
    {
      if (ct->dbtbl != DBTBLPN)
        (void)CMPTBL_UnlockTableRead(ct);
      else
        putmsg(MERR, fn, "Orphaned table read lock");
    }
  while (ct->indexWriteLocks > 0)
    {
      if (ct->dbtbl != DBTBLPN)
        (void)CMPTBL_UnlockIndexWrite(ct);
      else
        putmsg(MERR, fn, "Orphaned index write lock");
    }
  while (ct->indexReadLocks > 0)
    {
      if (ct->dbtbl != DBTBLPN)
        (void)CMPTBL_UnlockIndexRead(ct);
      else
        putmsg(MERR, fn, "Orphaned index read lock");
    }
  ct->dbtbl = DBTBLPN;                          /* not owned */
  ct->inputDbf = DBFPN;                         /* points into `dbtbl' */
  ct->inputDbfPath = CHARPN;                    /* points into `inputDbf' */
  ct->inputTablePathSansExt = TXfree(ct->inputTablePathSansExt);
  ct->outputTablePathSansExt = TXfree(ct->outputTablePathSansExt);
  ct->tableName = TXfree(ct->tableName);
  if (ct->meter != METERPN)
    {
      meter_end(ct->meter);
      ct->meter = closemeter(ct->meter);
    }
  ct = TXfree(ct);
done:
  return(TXcmpTblPN);
}

static TXcmpTbl *TXcmpTblOpen ARGS((DBTBL *dbtbl, int overwrite));
static TXcmpTbl *
TXcmpTblOpen(dbtbl, overwrite)
DBTBL   *dbtbl;         /* (in) DBTBL (shared) */
int     overwrite;      /* (in) nonzero: overwrite mode */
{
  static CONST char     fn[] = "TXcmpTblOpen";
  TXcmpTbl              *ct;

  ct = (TXcmpTbl *)TXcalloc(TXPMBUFPN, fn, 1, sizeof(TXcmpTbl));
  if (ct == TXcmpTblPN) goto err;
  TXsetrecid(&ct->sysTblTempRow, RECID_INVALID);
  ct->wtixRecidTranslatorIdx = -1;
  /* rest cleared by calloc() */
  ct->overwrite = overwrite;
  ct->ddic = dbtbl->ddic;
  ct->dbtbl = dbtbl;
  if ((ct->tableName = TXstrdup(TXPMBUFPN, fn, dbtbl->lname)) == CHARPN)
    goto err;
  /* Only support real tables for now; no system tables (too critical and
   * may have cache issues) nor views/indexes/etc. we know nothing about:
   */
  if (ct->dbtbl->type != TEXIS_TABLE)
    {
      putmsg(MERR + UGE, fn,
             "Table `%s' is not a Texis table: cannot compact",
             ct->tableName);
      goto err;
    }
  ct->inputDbf = ct->dbtbl->tbl->df;
  if (ct->inputDbf->dbftype != DBF_KAI)
    {
      putmsg(MERR + UGE, fn, "Table `%s' is not KDBF: cannot compact",
             ct->tableName);
      goto err;
    }
  ct->inputDbfPath = getdbffn(ct->inputDbf);
  ct->inputTablePathSansExt = TXstrdup(TXPMBUFPN, fn, ct->inputDbfPath);
  if (ct->inputTablePathSansExt == CHARPN) goto err;
  *TXfileext(ct->inputTablePathSansExt) = '\0';

  /* Overwriting the DBF in-place avoids doubling the disk space used
   * with a temp file, but may leave the DBF unreadable during the
   * process, and/or corrupt if the process fails.  Need to work out
   * lock/readability issues to see if it is truly possible to leave
   * DBF readable for other processes after each row write; wtf
   * disable overwrite for now:
   */
  if (ct->overwrite)
    {
      putmsg(MERR + UGE, fn, "Overwrite mode not currently supported");
      goto err;
    }

  goto done;

err:
  ct = TXcmpTblClose(ct);
done:
  return(ct);
}

/* ------------------------------------------------------------------------ */

int
TXcompactTable(DDIC *ddic, QUERY *q, int overwrite)
/* Does work of `ALTER TABLE tableName COMPACT'.
 * `q->in1' is the opened table.
 * Returns 0 on error.
 */
{
  static CONST char     fn[] = "TXcompactTable";
  TXcmpTbl              *ct = TXcmpTblPN;
  int                   ret;
  TXMDT                 saveIndexmeter;
  char                  *tblRealName = CHARPN;
  TXindOpts             *options = NULL;
  char                  **fdbiIndexNames = NULL;
  size_t                numFdbiIndexNames = 0, i;

  /* compactmeter overrides indexmeter during ALTER TABLE COMPACT: */
  saveIndexmeter = TXindexmeter;
  TXindexmeter = TXcompactmeter;

  /* WTF call TXclosecacheindex() under Windows here? */
  /* WTF flush table cache? what if we try to compact SYSTABLES? */
  if (q->in1 == DBTBLPN) goto err;
  tblRealName = TXstrdup(TXPMBUFPN, fn, q->in1->rname);
  ct = TXcmpTblOpen(q->in1, overwrite);
  if (ct == TXcmpTblPN) goto err;

  /*   Get index and table read locks for the duration of this process:
   * any writes by other users could alter our table read cursor's
   * correctness and lead to row(s) not copied, and we cannot allow
   * Metamorph index mods either.
   *   Get index lock *before* (outside) table lock to prevent deadlock
   * with another process; this is also how updateindex() does it:
   */
  if (!CMPTBL_LockIndexRead(ct)) goto err;
  if (!CMPTBL_LockTableRead(ct)) goto err;

  /* Create output table.  Done relatively early (e.g. before Metamorph
   * index update) so that another compact-table attempt during our
   * run can detect collision early (by seeing our temp-table entry):
   */
  if (!TXcmpTblCreateOutputTable(ct)) goto err;

  /* Open all indexes, and make sure Metamorph indexes opened as FDBIs: */
  TXclosecacheindex(ddic, ct->tableName);       /* WTF is this proper&needed */
  closeindexes(ct->dbtbl);                      /* WTF "" */
  /* See also ALTER_OP perms in ipreparetree(): */
  TXgetindexes(ct->dbtbl, (PM_ALTER | PM_SELECT | PM_UPDATE), NULL, 1);

  /*   If a Metamorph index is out-of-date, it would be difficult for
   * TXchangeLocInIndices() to update it during compaction: deleted
   * tokens would still remain in the .dat file, so a deleted recid of
   * some sort would still need to exist in the _D.btr delete list and
   * token file, as a placeholder.  But the token file must remain in
   * ascending recid order with no dups, so we would have to make a
   * "fake" deleted recid interposed Dewey-decimal style, and there is
   * only so much room before recids might collide (not to mention
   * potential KDBF errors if a "deleted" recid's block is ever read).
   *   We avoid the issue by updating Metamorph indexes first, before
   * compaction (but inside read locks so no one changes them during
   * compaction):
   */
  if (!(options = TXindOptsOpen(ddic))) goto err;
  /* Bug 7015: close other handles to Metamorph index(es) while
   * updating if possible, to avoid `Cannot delete xtest.btr: Text
   * file busy' under Unix on non-local Windows filesystem (officially
   * unsupported, but at least remove this roadblock):
   */
  fdbiIndexNames = TXdupStrList(TXPMBUFPN, ct->dbtbl->fdbiIndexNames,
                                ct->dbtbl->nfdbi);
  if (!fdbiIndexNames) goto err;
  numFdbiIndexNames = ct->dbtbl->nfdbi;
  TXclosecacheindex(ddic, ct->tableName);       /* avoid potential Bug 7015 */
  closeindexes(ct->dbtbl);                      /* avoid Bug 7015 */
  for (i = 0; i < numFdbiIndexNames; i++)       /* each Metamorph index */
    if (updindex(ddic, fdbiIndexNames[i], 0, options) != 0)
      goto err;
  options = TXindOptsClose(options);
  fdbiIndexNames = TXfreeStrList(fdbiIndexNames, numFdbiIndexNames);
  numFdbiIndexNames = 0;

  /* Refresh index cache, so we get the proper new Metamorph .btrs.
   * Also close table cache.  WTF when Bug 3217 and Bug 3685 done
   * this may not be needed (but keep anyway for insurance):
   */
  TXclosecacheindex(ddic, ct->tableName);       /*WTF is this proper&needed */
  closeindexes(ct->dbtbl);                      /*WTF "" */
  TXrmcache(ddic, tblRealName, INTPN);
  /* Clean up some marked-for-delete files; may save disk space: */
  TXdocleanup(ct->ddic);
  /* See also ALTER_OP perms in ipreparetree(): */
  TXgetindexes(ct->dbtbl, (PM_ALTER | PM_SELECT | PM_UPDATE), NULL, 1);

  /* If not overwriting, create temp new indexes: */
  if (!TXcmpTblCreateOutputBtreeIndexes(ct)) goto err;
  if (!TXcmpTblCreateOutputInvertedIndexes(ct)) goto err;
  if (!TXcmpTblCreateOutputMetamorphIndexes(ct)) goto err;

  /* Set up an ABEND callback, since we have opened all temp files and
   * things will take a while from here on:
   */
  TXaddabendcb(TXcmpTblAbendCallback, ct);
  ct->abendCbRegistered = 1;

  /* Compact table, and translate Metamorph token files: */
  if (!TXcmpTblCompactTableAndTranslateMetamorphIndexes(ct)) goto err;

  /* Translate B-tree and inverted indexes: */
  if (!TXcmpTblTranslateBtreeAndInvertedIndexes(ct)) goto err;

  /* Remove ABEND callback, so there is no interference while we make
   * temp files live:
   */
  if (ct->abendCbRegistered)
    {
      TXdelabendcb(TXcmpTblAbendCallback, ct);
      ct->abendCbRegistered = 0;
    }

  if (!ct->overwrite)
    {
      /* Flip table and indexes live.  Get write locks first, so no
       * one else accesses table or indexes during flip: WTF on
       * indexes, SYSINDEX, SYSTABLES too?  do we need a write lock on
       * SYSTABLES even if not modifying it (Unix?), to signal all
       * processes to refresh their copy of these indexes?
       */
      if (!CMPTBL_LockIndexWrite(ct)) goto err; /* idx outside of tbl lock */
      if (!CMPTBL_LockTableWrite(ct)) goto err;
      if (!TXcmpTblMakeOutputBtreeIndexesLive(ct)) goto err;
      if (!TXcmpTblMakeOutputInvertedIndexesLive(ct)) goto err;
      if (!TXcmpTblMakeOutputMetamorphIndexesLive(ct)) goto err;
      if (!TXcmpTblMakeOutputTableLive(ct)) goto err;
      /* Done with flip: */
      (void)CMPTBL_UnlockTableWrite(ct);
      (void)CMPTBL_UnlockIndexWrite(ct);
    }
  ret = 1;
  goto done;

err:
  ret = 0;                                      /* error */
done:
  ct = TXcmpTblClose(ct);                       /* before closedbtbl() */
  q->in1 = closedbtbl(q->in1);                  /* before TXrmcache() */
  /* Free any other cached versions of this table, to help TXdocleanup().
   * WTF until Bug 3685 fixed this is also needed to prevent re-use of old
   * table file by this DDIC; only Bug 3685 will fix for other handles and
   * processes however.  Must be done after closedbtbl() so cache can free
   * that DBTBL too:
   */
  TXrmcache(ddic, tblRealName, INTPN);
  /* Try to remove old indexes we deleted, after DBTBL (and
   * dbtbl.indexes) closed:
   */
  if (ret) TXdocleanup(ddic);
  tblRealName = TXfree(tblRealName);
  TXindexmeter = saveIndexmeter;
  options = TXindOptsClose(options);
  fdbiIndexNames = TXfreeStrList(fdbiIndexNames, numFdbiIndexNames);
  numFdbiIndexNames = 0;
  return(ret);
}
/* o  make temp-index entries in SYSINDEX.  verify temp-index type is
      not used/modified by other texis processes
   o  check that all `goto' bails clean up ok, especially locks
   o  do not enumerate temp/del tables in TXenumtables? elsewhere?
   o  check that we can drop a temp table with a missing pid,
      in case we die during compaction and remove temp/pid files but not
      SYSTABLES entry.  we cannot: TXdocleanup() must deal with it.  run
      in monitor?
   o  are sysTblTempRows... valid after creation? maybe not, if we let go
      of SYSINDEX/SYSTABLES locks?
      maybe so, since we have a read lock on dbtbl for duration? but that
      only prevents COMPACT TABLE, not necessarily an index mod/drop?
   o  close (or at least close token file) all but translator WTIX
      after open, to reclaim token-file mem?
   o  compact indexes too? as a separate ALTER INDEX ... COMPACT call?
 */
