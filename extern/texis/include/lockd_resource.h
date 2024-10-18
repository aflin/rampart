#ifndef LOCKD_RESOURCE_H
#define LOCKD_RESOURCE_H

#include <time.h>
#include <jansson.h>

typedef struct lockd_resource lockd_resource;

#include "lockd.h"

struct lockd_resource {
  lockd_resource *parent;
  lockd_resource *next;
  lockd_resource *prev;
  lockd_resource *child_head;
  lockd_resource *child_tail;
  char *resourcename;
  size_t namelen;
  int refcnt;
  time_t last_close;
  lockd_counter table_written;
  lockd_counter index_written;
  lockd_lock_queue lock_queue;
  char *resourcestatus;
  int resourcestatus_dirty;
};

int resource_attach(const char *name, lockd_resource *parent, lockd_resource **res);
lockd_resource *resource_find(const char *name, lockd_resource *parent);
int resource_detach(lockd_resource *res);
int resource_cleanup(lockd_resource *res, time_t min_age);
int resource_lock(lockd_resource *res, lockd_connection *c, lockd_modes mode, lockd_write_times write_times);
int resource_unlock(lockd_resource *res, lockd_connection *c, lockd_modes mode);
json_t *resource_dump_json(lockd_resource *r);
json_t *resource_summary_json(lockd_resource *r);
char *resource_summary_string(lockd_resource *r);

#endif /* LOCKD_RESOURCE_H */
