#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "txcoreconfig.h"

#ifdef LOCK_SERVER
#include "lockd_connection.h"

static lockd_connection *connections_head = NULL;
static lockd_connection *connections_tail = NULL;
static size_t nconnections = 0;

extern FILE *logfile;

int
connection_create(lockd_connection **res)
{
  lockd_connection *c;
  if(!res) return -1;
  c = calloc(sizeof(lockd_connection), 1);
  *res = c;
  c->prev = connections_tail; connections_tail = c;
  if(c->prev) {
    (c->prev)->next = c;
  }
  c->next = NULL;
  if(!connections_head) connections_head = c;
  nconnections++;
  return 0;
}

int
connection_delete(lockd_connection *c)
{
  if(!c) return -1;
  if(c->prev) {
    (c->prev)->next = c->next;
  } else {
    connections_head = c->next;
  }
  if(c->next) {
    (c->next)->prev = c->prev;
  } else {
    connections_tail = c->prev;
  }
  nconnections--;
  lock_connection_queue_unlock(c);
  if(c->database) resource_detach(c->database);
  free(c);
  return 0;
}

int
connection_set_database(lockd_connection *c, const char *db)
{
  if(!c) return -1;
  if(c->database) resource_detach(c->database);
  return resource_attach(db, NULL, &c->database);
}

lockd_connection *
connection_enumerate(lockd_connection *p)
{
  if(!p) return connections_head;
  return p->next;
}
static json_t *
lock_connection_to_json(lockd_connection *c)
{
  json_t *r, *la;
  lockd_lock *l;

  r = json_object();
  la = json_array();
  l = c->lockq.head;
  while(l)
  {
    json_array_append_new(la, lock_to_json(l));
    l = l->resource_list.next;
  }
  json_object_set_new(r, "locks", la);
  return r;
}

json_t *
connection_to_json(lockd_connection *c)
{
  json_t *r, *t;
  long l;

  r = json_object();

  l = (long)c;
  t = json_integer(l);
  json_object_set_new(r, "connection", t);  
  if(c->database) {
    t = json_string(c->database->resourcename);
  } else {
    t = json_null();
  }
  json_object_set_new(r, "database", t);
  t = lock_connection_to_json(c);
  json_object_set_new(r, "locks", t);
  return r;
}

int
connection_send_response(lockd_connection *connection)
{
  char *res = NULL;
  size_t reslen;
  char resbuf[1024];
  int free_res = 0;
  json_t *response_json;
  struct evbuffer *output;

  if(!connection->response) return -1;
  output = bufferevent_get_output(connection->bev);
  switch(connection->response->type) {
    case TX_LOCK_REQUEST_JSON:
      response_json = TXlockRequest_GetJson(connection->response);
      reslen = json_dumpb(response_json, resbuf, sizeof(resbuf), connection->json_flags);
      if(reslen >= sizeof(resbuf)) {
        res = json_dumps(response_json, connection->json_flags);
        reslen = strlen(res);
        free_res = 1;
      } else {
        res = resbuf;
        res[reslen] = '\0';
      }
      break;
    case TX_LOCK_REQUEST_STRING:
      res = TXlockRequest_GetString(connection->response, &reslen);
  }
  connection->response = TXlockRequest_Close(connection->response);
  connection->response_count = 0;
  if(res) {
    evbuffer_add(output, res, reslen);
    if(free_res) free(res);
  }
  evbuffer_add(output, "\n", 1);
  bufferevent_flush(connection->bev, EV_WRITE, BEV_FLUSH);
  if(logfile) {
    fprintf(logfile, "%p> %s\n", connection, res);
    fflush(logfile);
  }
  return 0;
}

int
connection_add_response(lockd_connection *connection, TXLockRequest *response)
{
  if(!connection) return -1;
  if(!response) return 0;

  if(connection->command_count == 0) return -1; /* No command to reply to */
  if(connection->command_count == 1) {
    connection->response = response;
    connection->response_count = 1;
  }
  if(connection->response_count == connection->command_count)
  {
    connection_send_response(connection);
  }
  return 0;
}
#endif
