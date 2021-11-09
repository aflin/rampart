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
