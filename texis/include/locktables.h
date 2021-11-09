#ifndef LOCKTABLES_H
#define LOCKTABLES_H


#endif /* LOCKTABLES_H */
typedef enum LOCKTABLES_RETURN {
  LOCKTABLES_OK,
  LOCKTABLES_ERR,
  LOCKTABLES_SKIP,
  LOCKTABLES_MODIFIED
} LOCKTABLES_RETURN;

typedef struct LOCKTABLES_ENTRY {
  struct LOCKTABLES_ENTRY *next;
  char *table;
  int locktype;
  int lockcount;
  ft_counter mod_date;
} LOCKTABLES_ENTRY;

LOCKTABLES_ENTRY *LockTablesOpen(void);
LOCKTABLES_ENTRY *LockTablesClose(LOCKTABLES_ENTRY *locktables);

LOCKTABLES_RETURN LockTablesLock(DBTBL *dbtbl, int type);
LOCKTABLES_RETURN LockTablesUnlock(DBTBL *dbtbl, int type);
