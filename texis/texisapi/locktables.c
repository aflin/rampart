#include <stdio.h>
#include <string.h>
#include "os.h"
#include "dbquery.h"
#include "texint.h"

/*

LOCK TABLES
    tbl_name [[AS] alias] lock_type
    [, tbl_name [[AS] alias] lock_type] ...

lock_type: {
    READ [LOCAL]
  | [LOW_PRIORITY] WRITE
}

UNLOCK TABLES

LOCAL and LOW_PRIORITY not supported (LOW_PRIORITY is deprecated in mysql)

READ is TABLE and INDEX READ
WRITE is TABLE and INDEX WRITE (and allow read)

If LOCK TABLES succeeds set a flag in DDIC to say we have locks, and store the list of locks.
Possibly pointer to linked list of locks/status?

Returns:

  SUCCESS: Lock Granted - arg for write since?  Based on our lock, don't need to ask.
                          e.g. index lock with time stamp, then LOCK TABLES
  FAILURE: Lock Not Granted, e.g. requested a lock we don't have
  SKIP: Not in LockTables mode, so proceed normally.

  LockTablesOpen()
  LockTablesClose()
  LockTablesLock()
  LockTablesUnlock()

 */

/**
 *
 */

static LOCKTABLES_ENTRY *
LockTablesFromQnode(DDIC *ddic, QNODE *qnode)
{
  LOCKTABLES_ENTRY *ret = NULL;

  if(qnode->left->op == LIST_OP) {
    ret = LockTablesFromQnode(ddic, qnode->left);
    if(!ret) {
      return NULL;
    }
    ret->next = LockTablesFromQnode(ddic, qnode->right);
  } else {
    ret = TXcalloc(NULL, __FUNCTION__, 1, sizeof(LOCKTABLES_ENTRY));
    ret->table = TXstrdup(NULL, __FUNCTION__, qnode->left->tname);
    switch(*(char *)qnode->right->tname) {
      case 'W':
        ret->locktype=W_LCK;
        dblock(ddic, 0, NULL, INDEX_WRITE, ret->table, &ret->mod_date);
        dblock(ddic, 0, NULL, W_LCK, ret->table, &ret->mod_date);
        break;
      case 'R':
        ret->locktype=R_LCK;
        dblock(ddic, 0, NULL, INDEX_READ, ret->table, &ret->mod_date);
        dblock(ddic, 0, NULL, R_LCK, ret->table, &ret->mod_date);
        break;
      default:
        putmsg(MERR, __FUNCTION__, "Unknown lock type %s", qnode->right->tname);
    }
    if(TXverbosity > 1)
      putmsg(MINFO, __FUNCTION__, "Locking Table %s for %s", qnode->left->tname, qnode->right->tname);
  }
  return ret;
}

int
LockTablesInit(DDIC *ddic, QNODE *qnode)
{
  LOCKTABLES_ENTRY *locktable_entry = NULL, *next_entry = NULL;
  if(!ddic) {
    return -1;
  }
  /* Unlock */
  locktable_entry = ddic->locktables_entry;
  ddic->locktables_entry = NULL;
  while(locktable_entry) {
    switch(locktable_entry->locktype) {
      case W_LCK:
        dbunlock(ddic, 0, NULL, W_LCK, locktable_entry->table);
        dbunlock(ddic, 0, NULL, INDEX_WRITE, locktable_entry->table);
        if(TXverbosity > 1)
          putmsg(MINFO, __FUNCTION__, "Unlocking Table %s for W", locktable_entry->table);
        break;
      case R_LCK:
        dbunlock(ddic, 0, NULL, R_LCK, locktable_entry->table);
        dbunlock(ddic, 0, NULL, INDEX_READ, locktable_entry->table);
        if (TXverbosity > 1)
          putmsg(MINFO, __FUNCTION__, "Unlocking Table %s for R", locktable_entry->table);
        break;
    }
    next_entry = locktable_entry->next;
    TXfree(locktable_entry);
    locktable_entry = next_entry;
  }
  if(qnode) {
    ddic->locktables_entry = LockTablesFromQnode(ddic, qnode);
  }
  return 0;
}

LOCKTABLES_RETURN
LockTablesLock(DBTBL *db, int type)
{
  int verify_time = 0;
  int desired_lock = 0;
  LOCKTABLES_ENTRY *locktable_entry;

  if(!db || !db->ddic) {
    return LOCKTABLES_ERR;
  }
  locktable_entry = db->ddic->locktables_entry;
  if(!locktable_entry) {
    return LOCKTABLES_SKIP;
  }
  if (type & V_LCK) {
    verify_time = 1;
  }
  if((type & R_LCK) || (type & INDEX_READ)) {
    desired_lock = R_LCK;
  }
  if((type & W_LCK) || (type & INDEX_WRITE)) {
    desired_lock = W_LCK;
  }
  while(locktable_entry) {
    if((locktable_entry->locktype == W_LCK) || ((locktable_entry->locktype == R_LCK) && (desired_lock == R_LCK))) {
      if(strcmp(locktable_entry->table, db->rname) == 0) {
        /* WTF - handle verify time */
        locktable_entry->lockcount++;
        if(verify_time) {
          int compare;
          CTRCMP(&locktable_entry->mod_date, &db->iwritec, compare);
          if(compare > 0) {
            db->iwritec = locktable_entry->mod_date;
            return LOCKTABLES_MODIFIED;
          }
        }
        return LOCKTABLES_OK;
      }
    }
    locktable_entry = locktable_entry->next;
  }
  /*
   * currently return SKIP here, which will allow locks to automatically be
   * added for the query.
   *
   * mysql semantics would have it return ERR, and fail the SQL statement
   */
  return LOCKTABLES_SKIP;
}

LOCKTABLES_RETURN
LockTablesUnlock(DBTBL *db, int type)
{
  int desired_lock = 0;
  LOCKTABLES_ENTRY *locktable_entry;

  if(!db || !db->ddic) {
    return LOCKTABLES_ERR;
  }
  locktable_entry = db->ddic->locktables_entry;
  if(!locktable_entry) {
    return LOCKTABLES_SKIP;
  }
  if((type & R_LCK) || (type & INDEX_READ)) {
    desired_lock = R_LCK;
  }
  if((type & W_LCK) || (type & INDEX_WRITE)) {
    desired_lock = W_LCK;
  }
  while(locktable_entry) {
    if((locktable_entry->locktype == W_LCK) || ((locktable_entry->locktype == R_LCK) && (desired_lock == R_LCK))) {
      if(strcmp(locktable_entry->table, db->rname) == 0) {
        if(locktable_entry->lockcount <= 0) {
          return LOCKTABLES_ERR;
        }
        locktable_entry->lockcount--;
        return LOCKTABLES_OK;
      }
    }
    locktable_entry = locktable_entry->next;
  }
  return LOCKTABLES_SKIP;
}
