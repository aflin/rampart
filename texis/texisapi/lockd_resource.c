#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "txcoreconfig.h"

#ifdef LOCK_SERVER
#include "lockd_resource.h"

static lockd_resource *resource_server = NULL;

static int
resource_delete(lockd_resource *child)
{
  lockd_resource *parent;

  if(!child) return -1;
  parent = child->parent;
  if(parent)
  {
    if(child->prev) {
      (child->prev)->next = child->next;
    } else {
      parent->child_head = child->next;
    }
    if(child->next) {
      (child->next)->prev = child->prev;
    } else {
      parent->child_tail = child->prev;
    }
  }
  return 0;
}

/**
  * Cleanup resources not used since min_age
 **/

int
resource_cleanup(lockd_resource *resource, time_t min_age)
{
  lockd_resource *child, *next_child;

  if(!resource) resource = resource_server;
  if(!resource) return 0;
  child = resource->child_head;
  while(child) {
    next_child = child->next;
    if((child->refcnt == 0) && (child->last_close <= min_age))
    {
      resource_cleanup(child,  min_age);
    }
    child = next_child;
  }
  if((resource->child_head == NULL) && (resource->refcnt == 0) && (resource->last_close <= min_age) && (resource != resource_server)) {
    resource_delete(resource);
  }
  return 0;
}

lockd_resource *
resource_find(const char *name, lockd_resource *parent)
{
  lockd_resource *ret = NULL, *prune = NULL;
  time_t old = time(NULL) - (30 * 60 * 60); /* More than 30 minutes old */

  if(!parent) goto find_done;
  if(!name) goto find_done;
  ret = parent->child_head;
  while(ret)
  {
    if(!strcmp(ret->resourcename, name)) goto find_done;
    if((ret->refcnt == 0) && (ret->last_close <= old)) {
      prune = ret;
      old = ret->last_close;
    }
    ret = ret->next;
  }
find_done:
  resource_delete(prune);
  return ret;
}

static lockd_resource *
resource_create(const char *name, lockd_resource *parent)
{
  lockd_resource *ret = NULL;

  if(name) {
    ret = calloc(1, sizeof(lockd_resource));
    if(ret) {
      ret->resourcename = strdup(name);
      if(!ret->resourcename) {
        free(ret);
        ret = NULL;
      }
      ret->parent = parent;
    }
  }
  return ret;
}

static int
resource_insert(lockd_resource *child, lockd_resource *parent)
{
  if(!parent || !child) return -1;
  child->prev = parent->child_tail;
  parent->child_tail = child;
  if(child->prev) (child->prev)->next = child;
  child->next = NULL;
  if(!parent->child_head) parent->child_head = child;
  return 0;
}

int
resource_attach(const char *name, lockd_resource *parent, lockd_resource **res)
{
  lockd_resource *result;

  if(res) *res = NULL;
  if(!resource_server)
  {
    resource_server = resource_create("Lockd Process", NULL);
    if(!resource_server) return -1;
  }
  if(!parent) parent = resource_server;
  result = resource_find(name, parent);
  if(!result) {
    result = resource_create(name, parent);
    resource_insert(result, parent);
  }
  if(result) {
    result->refcnt++;
    *res = result;
    return 0;
  }
  return -1;
}

int resource_detach(lockd_resource *res)
{
  if(res) {
    if(res->refcnt <=0) return -1;
    res->refcnt--;
    res->last_close = time(NULL);
  }
  return -1;
}

int
resource_lock(lockd_resource *res, lockd_connection *c, lockd_modes mode, lockd_write_times write_times)
{
  if(!res) return -1;

  return lock_create(res, c, mode, write_times);
}

int
resource_unlock(lockd_resource *res, lockd_connection *c, lockd_modes mode)
{
  int r = -1;

  if(!res) return r;
  if(LOCKD_TABLE_WRITE(mode))
  {
    counter_get(&res->table_written);
  }
  if(LOCKD_INDEX_WRITE(mode))
  {
    counter_get(&res->index_written);
  }
  r = lock_queue_unlock(&res->lock_queue, c, mode);
  return r;
}

static json_t *
lock_resource_to_json(lockd_resource *res)
{
  json_t *r, *la, *v, *t;
  lockd_lock *l;

  r = json_object();
  v = json_object();
  t = json_integer(res->lock_queue.current_modes);
  json_object_set_new(v, "as_int", t);
  t = json_string(lockd_mode_to_string(res->lock_queue.current_modes));
  json_object_set_new(v, "as_string", t);
  json_object_set_new(r, "current_state", v);
  la = json_array();
  l = res->lock_queue.granted.head;
  while(l)
  {
    json_array_append_new(la, lock_to_json(l));
    l = l->resource_list.next;
  }
  json_object_set_new(r, "granted", la);
  la = json_array();
  l = res->lock_queue.pending.head;
  while(l)
  {
    json_array_append_new(la, lock_to_json(l));
    l = l->resource_list.next;
  }
  json_object_set_new(r, "pending", la);
  return r;
}

json_t *
resource_dump_json(lockd_resource *r)
{
  json_t *j = NULL;
  json_t *v = NULL;
  json_t *t = NULL;
  char datestr[65];

  if(!r) r = resource_server;
  if(r) {
    j = json_object();
    v = json_string(r->resourcename);
    json_object_set_new(j, "name", v);
    v = json_integer(r->refcnt);
    json_object_set_new(j, "refcnt", v);
    if(r->last_close != 0) {
      struct tm tms;
      if(gmtime_r(&r->last_close, &tms)) {
        strftime(datestr, sizeof(datestr), "%Y%m%dZ%H%M%S", &tms);
        v = json_string(datestr);
        json_object_set_new(j, "last_close", v);
      }
    }
    counter_tostring(&r->table_written, COUNTER_HEX32, datestr, sizeof(datestr));
    t = json_string(datestr);
    json_object_set_new(j, "table_written", t);
    counter_tostring(&r->index_written, COUNTER_HEX32, datestr, sizeof(datestr));
    t = json_string(datestr);
    json_object_set_new(j, "index_written", t);
    json_object_set_new(j, "locks", lock_resource_to_json(r));
    if(r->child_head) {
      lockd_resource *c;
      json_t *cj;

      v = json_array();
      c = r->child_head;
      while(c) {
        cj = resource_dump_json(c);
        json_array_append_new(v, cj);
        c = c->next;
      }
      json_object_set_new(j, "children", v);
    }
  }
  return j;
}

/**
 *
 * Return a string {"name":...,"table_written":...,"index_written":...}
 */

char *
resource_summary_string(lockd_resource *r)
{
  char *res = NULL;
  char *cp;
  char datestr[65];

  if(!r) r = resource_server;
  if(r) {
    if(!r->resourcestatus) {
      r->resourcestatus = malloc(r->namelen + 130 + 44);
      r->resourcestatus_dirty = 1;
      sprintf(r->resourcestatus, "{\"name\":\"%s\",\"table_written\":\"\"}", r->resourcename);
    }
    if(r->resourcestatus_dirty) {
      cp = r->resourcestatus + 28 + r->namelen;
      cp += counter_tostring(&r->table_written, COUNTER_HEX32, cp, sizeof(datestr));
      cp += sprintf(cp, "\",\"index_written\":\"");
      cp += counter_tostring(&r->index_written, COUNTER_HEX32, cp, sizeof(datestr));
      cp += sprintf(cp, "\"}");
    }
    res = r->resourcestatus;
  }
  return res;
}


json_t *
resource_summary_json(lockd_resource *r)
{
  json_t *j = NULL;
  json_t *v = NULL;
  json_t *t = NULL;
  char datestr[65];

  resource_summary_string(r);
  if(!r) r = resource_server;
  if(r) {
    j = json_object();
    v = json_string(r->resourcename);
    json_object_set_new(j, "name", v);
    counter_tostring(&r->table_written, COUNTER_HEX32, datestr, sizeof(datestr));
    t = json_string(datestr);
    json_object_set_new(j, "table_written", t);
    counter_tostring(&r->index_written, COUNTER_HEX32, datestr, sizeof(datestr));
    t = json_string(datestr);
    json_object_set_new(j, "index_written", t);
  }
  return j;
}
#endif /* LOCK_SERVER */
