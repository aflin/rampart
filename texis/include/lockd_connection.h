#include "lockd_lock.h"

#ifndef LOCKD_CONNECTION_H
#define LOCKD_CONNECTION_H

/** Connection info
 *  Stored as double linked list
 **/

typedef struct lockd_connection lockd_connection;

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <jansson.h>

#include "lockd_resource.h"
#include "lockrequest.h"

enum command_fmt {
  CMD_JSON,
  CMD_SHORT_TEXT
};

struct lockd_connection {
  char *description;        /**< Human readable description */
  lockd_connection *prev;   /**< For linked list */
  lockd_connection *next;   /**< For linked list */
  struct lockd_lock_queue_ptrs lockq; /**< Our locks */
  lockd_resource *database; /**< Connection has a database resource */
  struct bufferevent *bev;  /**< Event Buffer for sending responses */
  size_t command_count;     /**< How many commands were sent */
  enum command_fmt command_format; /**< What format was the last command in **/
  size_t response_count;    /**< How many responses have we collected */
  TXLockRequest *response;  /**< Where the responses are collected (array or object) */
  size_t json_flags;        /**< Flags for encoding response */
  int fairlock;             /**< Fairlock flag - will I wait for the head of the line */
};

int connection_create(lockd_connection **res);
int connection_delete(lockd_connection *c);
int connection_set_database(lockd_connection *c, const char *db);
lockd_connection *connection_enumerate(lockd_connection *p);
json_t *connection_to_json(lockd_connection *c);
int connection_send_response(lockd_connection *c);
int connection_add_response(lockd_connection *c, TXLockRequest* r);

#endif /* LOCKD_CONNECTION_H */
