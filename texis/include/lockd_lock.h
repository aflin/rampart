#ifndef LOCKD_LOCK_H
#define LOCKD_LOCK_H

#include <jansson.h>

typedef enum lockd_modes {
  NL = 0x00, /** NULL - Interest, but no lock - keep resource around */
  CR = 0x01, /** Concurrent Read */
  CW = 0x02, /** Concurrent Write */
  PR = 0x04, /** Protected Read */
  PW = 0x08, /** Protected Write */
  IR = 0x10, /** Index Read */
  IW = 0x20, /** Index Write */
  EX = 0x80, /** Exclusive */
} lockd_modes ;

typedef enum lockd_write_times {
  NO_TIMES = 0,
  TABLE_TIMES = 1,
  INDEX_TIMES = 2,
  TABLE_INDEX_TIMES = 3
} lockd_write_times;

#define LOCKD_TABLE_WRITE_MODES (CW | PW | EX)
#define LOCKD_INDEX_WRITE_MODES (IW | EX)
#define LOCKD_TABLE_WRITE(a) ((a & LOCKD_TABLE_WRITE_MODES) != 0)
#define LOCKD_INDEX_WRITE(a) ((a & LOCKD_INDEX_WRITE_MODES) != 0)

typedef enum lockd_lock_state {
  LOCK_NO_QUEUE,
  LOCK_PENDING,
  LOCK_GRANTED,
  LOCK_RELEASED
} lockd_lock_state;

typedef struct lockd_lock lockd_lock;
typedef struct lockd_lock_queue lockd_lock_queue;

struct lockd_lock_queue_ptrs {
  lockd_lock *head;
  lockd_lock *tail;
};

struct lockd_lock_queue_list {
  lockd_lock *next;
  lockd_lock *prev;
};

struct lockd_lock {
  struct lockd_lock_queue_list resource_list;
  struct lockd_lock_queue_list connection_list;
  lockd_modes mode;
  lockd_write_times write_times;
  lockd_lock_state state;
  struct lockd_connection *connection; /**< Need this to calculate state of everyone else */
  struct lockd_resource *resource;     /**< Need to know the resource to remove from */
};

struct lockd_lock_queue {
  struct lockd_lock_queue_ptrs granted;
  struct lockd_lock_queue_ptrs pending;
  lockd_modes current_modes;
};

lockd_lock_queue *lock_queue_create(void);
lockd_lock_queue *lock_queue_destroy(lockd_lock_queue *lq);

int lock_create(struct lockd_resource *res, struct lockd_connection *c, lockd_modes mode, lockd_write_times write_times);
int lock_queue_lock(lockd_lock *l);
int lock_queue_unlock(lockd_lock_queue *lq, struct lockd_connection *c, lockd_modes mode);
int lock_connection_queue_unlock(struct lockd_connection *c);
json_t *lock_to_json(lockd_lock *l);
char *lock_to_string(lockd_lock *l);
lockd_modes lockd_string_to_mode(const char *mode);
char *lockd_mode_to_string(lockd_modes mode);
char *lockd_status_to_string(lockd_lock_state state);


#endif /* LOCKD_LOCK_H */
