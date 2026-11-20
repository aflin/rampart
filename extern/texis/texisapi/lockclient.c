#include "txcoreconfig.h"
#ifdef LOCK_SERVER

#include <unistd.h>       /* getpid() */
#include <jansson.h>
#include "texint.h"
#include "lockrequest.h"

int TXInLockBlock = 0;           /* depth of read batches in progress */

/* The tables the read batches in progress have locked (TXnode_buffer_exec()).
 * Lock calls are skipped inside a batch for these only: any other table --
 * one read by a subquery, a system table being reloaded -- is not held, and
 * must be locked for real. */
#define TX_LOCK_BATCH_MAX 128
static struct { DDIC *ddic; char *name; } txLockBatchTables[TX_LOCK_BATCH_MAX];
static int txLockBatchNum = 0;

int
TXlockBatchMark(void)
{
  return txLockBatchNum;
}

void
TXlockBatchAddTable(DDIC *ddic, const char *name)
{
  if(!name || txLockBatchNum >= TX_LOCK_BATCH_MAX) return;
  txLockBatchTables[txLockBatchNum].ddic = ddic;
  txLockBatchTables[txLockBatchNum].name = strdup(name);
  if(txLockBatchTables[txLockBatchNum].name) txLockBatchNum++;
}

void
TXlockBatchRelease(int mark)
{
  while(txLockBatchNum > mark && txLockBatchNum > 0) {
    txLockBatchNum--;
    free(txLockBatchTables[txLockBatchNum].name);
    txLockBatchTables[txLockBatchNum].name = NULL;
  }
}

/* Counters the lock server gave during the read batch in progress.  A
 * table the batch holds cannot change until the batch ends (10 rows or
 * 10 ms), and a catalogue change is picked up at the next batch, so each
 * is asked for once per batch -- not once per row a subquery is run for. */
#define TX_BATCH_VERIFY_MAX 16
static struct { DDIC *ddic; char name[48]; char kind; ft_counter c; unsigned epoch; }
  txBatchVerify[TX_BATCH_VERIFY_MAX];
static unsigned txBatchEpoch = 1;

void
TXlockBatchVerifyFresh(void)
/* Forget them: a batch begins, or the caller must have the truth now. */
{
  txBatchEpoch++;
}

static int
txBatchVerifyGet(DDIC *ddic, const char *name, char kind, ft_counter *c)
{
  int i;

  for(i = 0; i < TX_BATCH_VERIFY_MAX; i++)
    if(txBatchVerify[i].epoch == txBatchEpoch && txBatchVerify[i].ddic == ddic &&
       txBatchVerify[i].kind == kind && !strcmp(txBatchVerify[i].name, name)) {
      *c = txBatchVerify[i].c;
      return 1;
    }
  return 0;
}

static void
txBatchVerifyPut(DDIC *ddic, const char *name, char kind, ft_counter *c)
{
  int i, use = 0;

  if(strlen(name) >= sizeof(txBatchVerify[0].name)) return;
  for(i = 0; i < TX_BATCH_VERIFY_MAX; i++)
    if(txBatchVerify[i].epoch != txBatchEpoch) { use = i; break; }
  txBatchVerify[use].ddic = ddic;
  strcpy(txBatchVerify[use].name, name);
  txBatchVerify[use].kind = kind;
  txBatchVerify[use].c = *c;
  txBatchVerify[use].epoch = txBatchEpoch;
}

int
TXlockBatchHas(DDIC *ddic, const char *name)
/* 1 if a batch in progress already holds `name' */
{
  int i;

  for(i = 0; i < txLockBatchNum; i++)
    if(txLockBatchTables[i].ddic == ddic && !strcmp(txLockBatchTables[i].name, name))
      return 1;
  return 0;
}

static int
txLockBatchCovers(DDIC *ddic, const char *tname)
{
  int i;

  if(!TXInLockBlock) return 0;
  for(i = 0; i < txLockBatchNum; i++)
    if(txLockBatchTables[i].ddic == ddic && !strcmp(txLockBatchTables[i].name, tname))
      return 1;
  return 0;
}

/* set when a lock request fails even after reconnecting */
static int txLockServerLost = 0;

static TXEZsockbuf *
txLockConnect(DDIC *ddic, long replaces, long *idp)
/* Opens a lock server connection for `ddic''s database, starting the
 * lock server if it is not running, and announces the database and our
 * pid.  `replaces' (if > 0) is our previous connection's id: the server
 * drops it and its locks.  Sets `*idp' to the new connection's id (0 if
 * the server does not give one).  Returns NULL if that fails. */
{
  TXLockRequest *request = NULL, *response = NULL;
  json_t *t = NULL, *t2 = NULL;
  TXEZsockbuf *ezsb = NULL;
  int tries;

    /* For debugging on port 40712

    mkfifo 2way
    ncat -o logfile -l 40712 0<2way | ncat localhost 40713 1>2way

    */
  ezsb = TXEZsockbuf_client("localhost", 40713, TXPMBUF_SUPPRESS);
  if(!ezsb) {
    /* Not running (it exits when idle): start it, and give it up to 2s
     * to listen -- a single short wait failed the open on a slow start */
    TXrunlockdaemon(ddic);
    for(tries = 0; tries < 40 && !ezsb; tries++) {
      TXsleepmsec(50, 1);
      ezsb = TXEZsockbuf_client("localhost", 40713,
                                tries == 39 ? TXPMBUFPN : TXPMBUF_SUPPRESS);
    }
  }
  *idp = 0;
  if(!ezsb) goto done;
  t = json_object();
  json_object_set_new(t, "database", json_string(ddic->pname));
  json_object_set_new(t, "pid", json_integer((json_int_t)getpid()));
  if(replaces > 0)
    json_object_set_new(t, "replaces", json_integer((json_int_t)replaces));
  t2 = json_object();
  json_object_set_new(t2, "connect", t);
  request = TXlockRequest_CreateJson(t2);   /* owns t2 now */
  t2 = NULL;
  response = TXlockRequest(ezsb, request);
  if(response) {
    json_t *jid;

    t2 = TXlockRequest_GetJson(response);
    t = json_object_get(t2, "success");
    jid = json_object_get(t2, "id");
    if(json_is_integer(jid))
      *idp = (long)json_integer_value(jid);
  }
  if(!response || !json_is_true(t))
    ezsb = TXEZsockbuf_close(ezsb);
done:
  if(t2) json_decref(t2);
  if(request) TXlockRequest_Close(request);
  if(response) TXlockRequest_Close(response);
  return ezsb;
}

DBLOCK *
opendblock(DDIC *ddic)
{
  DBLOCK *dblock = NULL;

  if(!ddic) {
    return NULL;
  }
  dblock = (DBLOCK *)TXcalloc(ddic->pmbuf, __FUNCTION__, 1, sizeof(DBLOCK));
  if(dblock) {
    dblock->lockServerSocket = txLockConnect(ddic, 0, &dblock->lockServerId);
    if(!dblock->lockServerSocket)
      return closedblock(ddic->pmbuf, dblock, 0, TXbool_False);
    if(TXApp) {
      dblock->dumponbad = TXApp->LogBadSYSLOCKS;
    } else {
      dblock->dumponbad = 0;
    }
    dblock->ddic = ddic;
    ddic->dblock = dblock;
  }
  return dblock;
}

TXLockRequest *
TXlockRequestDb(DDIC *ddic, TXLockRequest *request)
/* Sends `request' on `ddic''s lock server connection.  If that fails
 * (e.g. EPIPE: the connection was dropped), closes it and retries once on
 * a new connection that replaces it, so the server releases its locks.
 * Returns the response, or NULL (and yaps, and sets the flag read by
 * TXlockServerLostTake()) if the lock server cannot be reached. */
{
  DBLOCK *dbl = ddic->dblock;
  TXLockRequest *response;

  response = TXlockRequest(dbl->lockServerSocket, request);
  if(response) return response;
  dbl->lockServerSocket = TXEZsockbuf_close(dbl->lockServerSocket);
  /* replaces: the server drops the old connection's locks even if our
   * close never reached it (e.g. the OS had already dropped it) */
  dbl->lockServerSocket = txLockConnect(ddic, dbl->lockServerId, &dbl->lockServerId);
  dbl->lockServerReconnects++;          /* locks held on the old one are gone */
  if(dbl->lockServerSocket)
    response = TXlockRequest(dbl->lockServerSocket, request);
  if(!response) {
    putmsg(MERR, __FUNCTION__,
           "Lost the connection to the lock server (texislockd) for database %s and could not reconnect",
           ddic->pname);
    txLockServerLost = 1;
  }
  return response;
}

int
TXlockServerLostTake(void)
/* Nonzero (once) if a lock request failed even after reconnecting since
 * the last call, i.e. the current statement ran without its locks. */
{
  int r = txLockServerLost;

  txLockServerLost = 0;
  return r;
}

DBLOCK *
closedblock(TXPMBUF *pmbuf, DBLOCK *dbl, int sid, TXbool readOnly)
{
  if(!dbl) return dbl; /* Allow NULL struct to close */
  /* TODO: If socket open, close */
  if(dbl->lockServerSocket) {
    dbl->lockServerSocket = TXEZsockbuf_close(dbl->lockServerSocket);
  }
  return TXfree(dbl);
}

ft_counter *
getcounter(DDIC *ddic)
{
  ft_counter *rc;

	if ((rc = (ft_counter *) TXcalloc(TXPMBUFPN, __FUNCTION__, 1, sizeof(ft_counter))) == NULL)
		return(NULL);
	else if (rgetcounter(ddic, rc, 1) <= -2)	/* complete failure */
		rc = TXfree(rc);
	return rc;
}

int
rgetcounter(DDIC *ddic, ft_counter *rc, int lock)
{
  TXLockRequest *request = NULL, *response = NULL;
  json_t *j_resp, *t;
  const char *counter_rc;
  int ret = -2;
  DBLOCK *dblock = ddic->dblock;
  static ft_counter	lcount = { 0L, 0L }, *l = &lcount;

  rc->date = time(NULL);
  if(lock && dblock) {
    request = TXlockRequest_CreateStaticString("{\"counter\":null}\n", -1);
    response = TXlockRequestDb(ddic, request);
    if(response) {
      j_resp = TXlockRequest_GetJson(response);
      t = json_object_get(j_resp, "counter");
      if(t) {
        counter_rc = json_string_value(t);
        TXparseHexCounter(rc, counter_rc, NULL);
        ret = 0;
        lcount = *rc;
      }
      json_decref(j_resp);
    }
  }
  if(ret < 0)
  {
    ret = lock ? -1 : 0;
    if (rc->date <= l->date)
    {
      l->seq++;
      rc->seq = l->seq;
      rc->date = l->date;
    }
    else
    {
      l->seq = 0;
      l->date = rc->date;
      rc->seq = 0;
    }
  }
done:
  if(request) TXlockRequest_Close(request);
  if(response) TXlockRequest_Close(response);
  return ret;
}

#define SET_VERIFY(a) if(want_v) *want_v = (a)

static char *
lock_type_to_char(int type, int *want_v, char *lockCmd)
{
  SET_VERIFY(0);
  if(type & INDEX_VERIFY) {
    SET_VERIFY(INDEX_VERIFY);
    type -= INDEX_VERIFY;
  }
  if(type & V_LCK) {
    SET_VERIFY(V_LCK);
    type -= V_LCK;
  }
  if(lockCmd) *lockCmd = 'L';
  switch(type) {
    case 0: if(lockCmd) *lockCmd = 'Q'; return("NL");
    case R_LCK: return("PR");
    case W_LCK: return("PW");
    case INDEX_READ: return("IR");
    case INDEX_WRITE: return("IW");
  }
  printf("Unknown type %d\n", type);
  return NULL;
}

/* The one lock block of this process (TXlockBlockBegin()): its DDIC, the
 * pid that began it (a forked child inherits these but holds no locks),
 * and the connection's reconnect count then (a reconnect loses the locks).
 * The process holds the table exclusively, so every DDIC of the process on
 * that database uses it without asking the lock server.  The two counters
 * stand in for the server's table and index write counters meanwhile, so
 * those DDICs still see each other's writes at their verifies. */
static DDIC *txLockBlockOwner = NULL;
static pid_t txLockBlockPid = 0;
static long txLockBlockReconnects = 0;
static unsigned txLockBlockCalls = 0;
static ft_counter txLockBlockTblC, txLockBlockIdxC;
static unsigned long txLockBlockSeq = 0;
static int txLockBlockSysDirty = 0;     /* 1: SYSINDEX  2: SYSPERMS */
#define TX_LOCK_BLOCK_CHECK_EVERY 2048  /* local requests per liveness check */

/* A value the lock server never gives: its dates are positive. */
static void
txLockBlockBump(ft_counter *c)
{
  c->date = -1;
  c->seq = ++txLockBlockSeq;
}

static int
txLockBlockIsOwner(DDIC *ddic)
{
  return (txLockBlockOwner == ddic && ddic->lockBlockTable &&
          txLockBlockPid == getpid());
}

static void
txLockBlockForget(DDIC *ddic)
{
  ddic->lockBlockTable = TXfree(ddic->lockBlockTable);
  if(txLockBlockOwner == ddic) txLockBlockOwner = NULL;
}

/* The locks last only as long as the connection, and if the lock server
 * is restarted nothing shows it while no requests are made: so ask, now
 * and then.  Returns 0 (block dropped, statement failed) if they are gone. */
static int
txLockBlockCheck(DDIC *own)
{
  DBLOCK *dbl = own->dblock;
  TXLockRequest *request, *response = NULL;
  int alive = 0;

  if(++txLockBlockCalls < TX_LOCK_BLOCK_CHECK_EVERY)
    return 1;
  txLockBlockCalls = 0;
  if(dbl)
  {
    request = TXlockRequest_CreateStaticString("{\"counter\":null}\n", -1);
    response = TXlockRequestDb(own, request);
    if(request) TXlockRequest_Close(request);
    if(response) TXlockRequest_Close(response);
    alive = (response && dbl->lockServerReconnects == txLockBlockReconnects);
  }
  if(alive) return 1;
  putmsg(MERR + LKE, NULL,
         "The lock on table %s for a bulk load was lost: the lock server connection was dropped",
         own->lockBlockTable);
  txLockBlockForget(own);
  txLockServerLost = 1;                 /* fails the current statement */
  return 0;
}

/* The block's table (returns 1), SYSINDEX (2), SYSPERMS (3), or 0 if
 * `ddic'/`tname' is nothing the block is concerned with. */
static int
txLockBlockWhich(DDIC *ddic, CONST char *tname)
{
  DDIC *own = txLockBlockOwner;

  if(!own || !own->lockBlockTable || txLockBlockPid != getpid())
    return 0;
  if(ddic != own &&
     !(ddic->pname && own->pname && !strcmp(ddic->pname, own->pname)))
    return 0;                           /* another database */
  if(!strcmp(tname, own->lockBlockTable)) return 1;
  if(!strcmp(tname, "SYSINDEX")) return 2;
  if(!strcmp(tname, "SYSPERMS")) return 3;
  return 0;
}

/* Answers a lock request locally if a lock block covers it: returns 1 and
 * sets `*rcp' to dblock()'s return value, else 0 (ask the lock server). */
static int
txLockBlockLock(DDIC *ddic, CONST char *tname, int type, ft_counter *counterp,
                int *rcp)
{
  int which = txLockBlockWhich(ddic, tname), sysBit;
  int base = (type & ~(V_LCK | INDEX_VERIFY));
  ft_counter *cur;

  if(!which) return 0;
  if(which != 1)
  {
    /* The owner's SYSINDEX/SYSPERMS change checks report no change:
     * permissions were checked when the block began, and no other process
     * can add or drop an index of the table (both need its locks).  This
     * process can, so a write to SYSINDEX/SYSPERMS here sends the owner's
     * next check to the server. */
    sysBit = which - 1;
    if(base == W_LCK || base == INDEX_WRITE)
    {
      txLockBlockSysDirty |= sysBit;
      return 0;
    }
    if(ddic != txLockBlockOwner || base != 0) return 0;
    if(txLockBlockSysDirty & sysBit)
    {
      txLockBlockSysDirty &= ~sysBit;
      return 0;
    }
    if(!txLockBlockCheck(txLockBlockOwner)) return 0;
    *rcp = 0;
    return 1;
  }
  if(!txLockBlockCheck(txLockBlockOwner)) return 0;
  /* as the server: verify against the write counter, and a granted lock
   * records it */
  if(type & INDEX_VERIFY) cur = &txLockBlockIdxC;
  else if(type & V_LCK) cur = &txLockBlockTblC;
  else cur = ((base == INDEX_READ || base == INDEX_WRITE) ?
              &txLockBlockIdxC : &txLockBlockTblC);
  *rcp = 0;
  if((type & (V_LCK | INDEX_VERIFY)) && counterp &&
     (counterp->date != cur->date || counterp->seq != cur->seq))
    *rcp = -2;
  if(counterp && base != 0) *counterp = *cur;
  return 1;
}

/* Answers an unlock locally if a lock block covers it (returns 1).  As the
 * server does, a write unlock sets a new counter and reports it. */
static int
txLockBlockUnlock(DDIC *ddic, CONST char *tname, int type,
                  ft_counter *counterp)
{
  ft_counter *cur = NULL;

  if(txLockBlockWhich(ddic, tname) != 1) return 0;
  if(type == INDEX_WRITE) cur = &txLockBlockIdxC;
  else if(type == W_LCK) cur = &txLockBlockTblC;
  if(cur)
  {
    txLockBlockBump(cur);
    if(counterp) *counterp = *cur;
  }
  return 1;
}

/* Holds `tname' write- and index-write-locked until TXlockBlockEnd(), for a
 * bulk load that should not pay for a lock round trip per row.  Same order
 * as a normal write (index then table), so it cannot deadlock with one.
 * Returns 0 on success, -1 on error (no locks held). */
int
TXlockBlockBegin(DDIC *ddic, CONST char *tname)
{
  long tblid = -1;
  char *name;

  if(ddic->nolocking || ddic->manuallocking)
    return 0;                                   /* nothing to batch */
  if(!ddic->dblock || !tname || !*tname)
    return -1;
  if(txLockBlockOwner && txLockBlockPid == getpid())
    return 0;           /* one per process: this load locks per row */
  txLockBlockOwner = NULL;                      /* inherited across a fork */
  ddic->lockBlockTable = TXfree(ddic->lockBlockTable);
  if(!(name = TXstrdup(ddic->pmbuf, __FUNCTION__, tname)))
    return -1;
  /* start the stand-in counters at the server's, so nothing looks
   * changed that is not */
  txLockBlockBump(&txLockBlockIdxC);
  txLockBlockBump(&txLockBlockTblC);
  if(dblock(ddic, ddic->sid, &tblid, INDEX_WRITE, name, &txLockBlockIdxC) == -1)
  {
    TXfree(name);
    return -1;
  }
  if(dblock(ddic, ddic->sid, &tblid, W_LCK, name, &txLockBlockTblC) == -1)
  {
    dbunlock(ddic, ddic->sid, &tblid, INDEX_WRITE, name);
    TXfree(name);
    return -1;
  }
  ddic->lockBlockTable = name;
  txLockBlockOwner = ddic;
  txLockBlockPid = getpid();
  txLockBlockReconnects = ddic->dblock->lockServerReconnects;
  txLockBlockCalls = 0;
  /* another process may have dropped an index, or changed permissions,
   * between our last statement and these locks: the first check of each
   * goes to the server; after that nothing can change them unseen */
  txLockBlockSysDirty = 3;
  return 0;
}

/* Ends a lock block, releasing its locks.  Safe to call when none.
 * Returns -1 if the locks were not held to the end (the lock server
 * connection was dropped meanwhile), else 0. */
int
TXlockBlockEnd(DDIC *ddic)
{
  long tblid = -1;
  char *name;
  int rc = 0, saveNoLocking;

  if(!txLockBlockIsOwner(ddic))
  {
    /* none, or lost and already reported, or a forked child's copy */
    if(txLockBlockOwner == ddic) txLockBlockOwner = NULL;
    ddic->lockBlockTable = TXfree(ddic->lockBlockTable);
    return 0;
  }
  name = ddic->lockBlockTable;
  ddic->lockBlockTable = NULL;                  /* real requests again */
  txLockBlockOwner = NULL;
  if(!ddic->dblock)                             /* connection closed: */
    rc = -1;                                    /*   server dropped them */
  else
  {
    if(ddic->dblock->lockServerReconnects != txLockBlockReconnects)
      rc = -1;
    saveNoLocking = ddic->nolocking;            /* set meanwhile? still ours */
    ddic->nolocking = 0;
    if(dbunlock(ddic, ddic->sid, &tblid, W_LCK, name) == -1) rc = -1;
    if(dbunlock(ddic, ddic->sid, &tblid, INDEX_WRITE, name) == -1) rc = -1;
    ddic->nolocking = saveNoLocking;
  }
  TXfree(name);
  return rc;
}

int
dblock(DDIC *ddic, ulong sid, long *tblid, int type, char *tname, ft_counter *counterp)
/* Returns -2 if `type' is/contains V_LCK/INDEX_VERIFY and `*tblid'/`tname'
 * was modified since `*counterp'; -1 on error; 0 if no mods (or INDEX_VERIFY/
 * V_LCK not set).  Lock only *not* granted if return is -1, or if `type'
 * is V_LCK/INDEX_VERIFY alone (no ...READ/...WRITE/etc.).
 * NOTE: might not issue putmsg() if LOCK_TIMEOUT error set, or if just
 * verifying.
 */
{
  int rc = -1, dov, blockVerify = 0, batchOnce = 0;
  char *type_c, lockCmd;
  const char *lw;
  ft_counter last_write;
  TXLockRequest *request, *response;
  json_t *request_j, *response_j, *request_details, *t;
  char *response_s, *counter_s = NULL, *counter_e, *request_s;
  char tmpbuf[1024];
  char verify;
  size_t reqlen;

  if(!tname) {
    return 0;
  }
  /* A read batch holds the locks for the tables of its query, so their lock
   * calls are skipped.  Only theirs. */
  if(txLockBatchCovers(ddic, tname)) {
    /* But "has it changed?" still needs a true answer, not "no": ask,
     * without locking. */
    if(!(type & (V_LCK | INDEX_VERIFY)) || !counterp || ddic->nolocking)
      return 0;
    blockVerify = ((type & ~(V_LCK | INDEX_VERIFY)) != 0);  /* caller takes it as locked */
    type &= (V_LCK | INDEX_VERIFY);
    batchOnce = 1;
  }
  else if(TXInLockBlock && !(type & ~(V_LCK | INDEX_VERIFY)) && counterp &&
          strncmp(tname, "SYS", 3) == 0)
    batchOnce = 1;                      /* a catalogue's counter */
  if(ddic->nolocking)
  {
    if(0 != (type & (V_LCK | INDEX_VERIFY)))
      return TXverifysingle;
    return 0;
  }
  if(txLockBlockOwner && txLockBlockLock(ddic, tname, type, counterp, &rc))
    return rc;
  rc = -1;
  type_c = lock_type_to_char(type, &dov, &lockCmd);
  if(!type_c) return -1;

  switch(dov) {
    case V_LCK: verify = 'T'; break;
    case INDEX_VERIFY: verify = 'I'; break;
    default:
      /* A plain lock with `counterp' returns the current write counter
       * in it, as the shared-memory dblock() did: without it a cache
       * keyed on that counter looks changed on every verify. */
      if(counterp && lockCmd == 'L')
        verify = (type == INDEX_READ || type == INDEX_WRITE) ? 'I' : 'T';
      else
        verify = '_';
      break;
  }
  /* asked already in this batch: the same answer, without asking */
  if(batchOnce && lockCmd != 'Q') batchOnce = 0;
  if(batchOnce && txBatchVerifyGet(ddic, tname, verify, &last_write)) {
    rc = 0;
    if(dov && counterp && (counterp->date != last_write.date || counterp->seq != last_write.seq))
      rc = -2;
    if(counterp && blockVerify)
      *counterp = last_write;
    return rc;
  }
  reqlen = snprintf(tmpbuf, sizeof(tmpbuf), "%c:%s:%c:%s\n", lockCmd, type_c, verify, tname);
  if(reqlen < sizeof(tmpbuf)) {
    request = TXlockRequest_CreateStaticString(tmpbuf, reqlen);
  } else {
    request_s = malloc(reqlen + 1);     /* was malloc(reqlen) + 1 */
    if(!request_s) return -1;
    sprintf(request_s, "%c:%s:%c:%s\n", lockCmd, type_c, verify, tname);
    request = TXlockRequest_CreateString(request_s, reqlen);
  }
  response = TXlockRequestDb(ddic, request);
  response_s = TXlockRequest_GetString(response, NULL);
  if(response_s && !strncmp(response_s, "E:Deadlock", 10)) {
    /* the lock server refused it: granting it needed a lock held by
     * something that is itself waiting for us */
    putmsg(MERR + LKE, NULL,
           "Deadlock on table %s: this statement and another each hold a lock the other is waiting for; this statement was stopped",
           tname);
    txLockServerLost = 1;               /* fails the current statement */
  }
  if(response_s && (strlen(response_s) > 4)) {
    if(*response_s == 'Y')
    {
      rc = 0;
      if(dov == V_LCK || dov == INDEX_VERIFY || verify != '_') {
        counter_s = response_s + 5;
#ifdef NEVER
        switch(dov) {
          case V_LCK:
            break;
          case INDEX_VERIFY:
            counter_s = strchr(counter_s, ':') + 1;
            break;
        }
#endif
        if(!*counter_s) goto done;
        counter_e = counter_s;
        while(*counter_e && *counter_e != ':') counter_e++;
        TXparseHexCounter(&last_write, counter_s, counter_e);
        if(batchOnce) txBatchVerifyPut(ddic, tname, verify, &last_write);
        if(dov && counterp && (counterp->date != last_write.date || counterp->seq != last_write.seq)) {
          rc = -2;
        }
        /* a granted lock (not a bare verify) records the counter */
        if(counterp && (lockCmd == 'L' || blockVerify))
          *counterp = last_write;
      }
    }
  }
done:
  if(TXgetlockverbose()) {
    printf("%s: %d %s - %d\n", __FUNCTION__, type, type_c, rc);
  }
  TXlockRequest_Close(request);
  TXlockRequest_Close(response);
  return rc;
}

int
dbunlock(DDIC *ddic, ulong sid, long *tblid, int type, char *tname)
{
  return TXdbunlockCounter(ddic, sid, tblid, type, tname, NULL);
}

int
TXdbunlockCounter(DDIC *ddic, ulong sid, long *tblid, int type, char *tname,
                  ft_counter *counterp)
/* As dbunlock(); a write unlock also stores in `*counterp' the counter it
 * set, if the lock server reports it (older servers do not). */
{
  int rc = -1;
  char *type_c, *request_s, *response_s;
  TXLockRequest *request, *response;
  char tmpbuf[1024];
  size_t reqlen;

  if(!tname) {
    return 0;
  }
  if(txLockBatchCovers(ddic, tname)) {
    return 0;
  }
  if(ddic->nolocking) {
    return 0;
  }
  if(txLockBlockOwner && txLockBlockUnlock(ddic, tname, type, counterp))
    return 0;
  type_c = lock_type_to_char(type, NULL, NULL);
  if(!type_c) return -1;

  reqlen = snprintf(tmpbuf, sizeof(tmpbuf), "U:%s:%s\n", type_c, tname);
  if(reqlen < sizeof(tmpbuf)) {
    request = TXlockRequest_CreateStaticString(tmpbuf, reqlen);
  } else {
    request_s = malloc(reqlen + 1);     /* was malloc(reqlen) + 1 */
    if(!request_s) return -1;
    sprintf(request_s, "U:%s:%s\n", type_c, tname);
    request = TXlockRequest_CreateString(request_s, reqlen);
  }
  response = TXlockRequestDb(ddic, request);
  response_s = TXlockRequest_GetString(response, NULL);
  if(response_s && *response_s == 'Y')
  {
    rc = 0;
    if(counterp && strlen(response_s) > 5 && response_s[4] == ':')
    {
      char *counter_s = response_s + 5, *counter_e = counter_s;

      while(*counter_e && *counter_e != ':') counter_e++;
      if(counter_e > counter_s)
        TXparseHexCounter(counterp, counter_s, counter_e);
    }
  }
  TXlockRequest_Close(request);
  TXlockRequest_Close(response);
  return rc;
}

int
addltable(TXPMBUF	*pmbuf, DBLOCK	*sem, char	*table)
{
  return 0;
}

int
delltable(TXPMBUF	*pmbuf, DBLOCK	*sem, char	*table, int tblid)
{
  return 0;
}

int
TXsetsleepparam(unsigned int param, int value)
{
  return 0;
}

int
TXsetfairlock(int n)
{
  /* TODO */
  return -1;
}

int
TXddicGetDbMonitorPid(DDIC *ddic)
{
  /* TODO: */
  return 0;
}

#endif /* LOCK_SERVER */
