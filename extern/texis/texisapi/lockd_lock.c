#include <stdio.h>
#include <string.h>

#include <jansson.h>
#include "lockd_lock.h"
#include "lockd_connection.h"

#define LOCKD_MODE_COUNT 8
static int lockcompatibility[LOCKD_MODE_COUNT][LOCKD_MODE_COUNT] = {
  /*       NL CR CW PR PW IR IW EX  */
/* NL */  { 1, 1, 1, 1, 1, 1, 1, 1},
/* CR */  { 1, 1, 1, 1, 1, 1, 1, 0},
/* CW */  { 1, 1, 1, 0, 0, 0, 0, 0},
/* PR */  { 1, 1, 0, 1, 0, 1, 0, 0},
/* PW */  { 1, 1, 0, 0, 0, 0, 0, 0},
/* IR */  { 1, 1, 0, 1, 0, 1, 0, 0},
/* IW */  { 1, 1, 0, 0, 0, 0, 0, 0},
/* EX */  { 1, 0, 0, 0, 0, 0, 0, 0}
};

static int
singlecompatibility(lockd_modes have, lockd_modes want)
{
  lockd_modes a, b;

  if (have > want) {
    a = want;
    b = have;
  } else {
    a = have;
    b = want;
  }
  if(a == NL) return 1;
  switch(a) {
    case NL: return 1; /* See above */
    case CR:
      if(b == EX) return 0;
      return 1;
    case CW:
      if(b == CW) return 1;
      return 0;
    case PR:
    case IR:
      if(b == PR || b == IR) return 1;
      return 0;
    default: return 0;
  }
}

static int
lock_queue_resource_add(struct lockd_lock_queue_ptrs *lqp, lockd_lock *l)
{
  l->resource_list.prev = lqp->tail; lqp->tail = l;
  if(l->resource_list.prev) { (l->resource_list.prev)->resource_list.next = l; }
  l->resource_list.next = NULL;
  if(!lqp->head) lqp->head = l;
  return 0;
}

static int
lock_queue_resource_del(struct lockd_lock_queue_ptrs *lqp, lockd_lock *l)
{
  if(!l) return -1;

  if(l->resource_list.prev) {
    (l->resource_list.prev)->resource_list.next = l->resource_list.next;
  } else {
    lqp->head = l->resource_list.next;
  }
  if(l->resource_list.next) {
    (l->resource_list.next)->resource_list.prev = l->resource_list.prev;
  } else {
    lqp->tail = l->resource_list.prev;
  }
  return 0;
}

static int compatiblestates(lockd_modes current, lockd_modes want)
{
  lockd_modes singlec, singlew, allmodes[] = {NL,CR,CW,PR,PW,IR,IW,EX};
  int modec, modew, compat, is_singlec, is_singlew;

  if(current == NL || want == NL)
    return 1;
  if((current == PR) && (want == PR))
    return 1;
  is_singlec = ((current & (current-1)) == 0) ? 1 : 0;
  is_singlew = ((want & (want-1)) == 0) ? 1 : 0;
  if(is_singlec && is_singlew)
    return singlecompatibility(current, want);
  if(is_singlew) {
    /* Must be multiple current */
    for(modec=1; modec < LOCKD_MODE_COUNT; modec++) {
      singlec = current & allmodes[modec];
      compat = singlecompatibility(singlec, want);
      if(compat == 0) return 0;
    }
  } else if(is_singlec) {
    /* Must be multiple want */
    for(modew=1; modew < LOCKD_MODE_COUNT; modew++) {
      singlew = want & allmodes[modew];
      compat = singlecompatibility(current, singlew);
      if(compat == 0) return 0;
    }
  } else {
    /* Multiple have and want */
    for(modec=1; modec < LOCKD_MODE_COUNT; modec++) {
      singlec = current & allmodes[modec];
      if(singlec != NL) {
        for(modew=1; modew < LOCKD_MODE_COUNT; modew++) {
          singlew = want & allmodes[modew];
          compat = singlecompatibility(singlec, singlew);
          if(compat == 0) return 0;
        }
      }
    }
  }
  return 1;
  return lockcompatibility[current][want];
  return 0;
}

static lockd_modes
computemystate(lockd_connection *connection, lockd_lock_queue *lq)
{
    lockd_modes result = NL;
    lockd_lock *l;

    for(l = lq->granted.head; l; l = l->resource_list.next) {
      if(l->connection == connection) {
        result = result | l->mode;
      }
    }
    return result;
}

static lockd_modes
computeotherstate(lockd_connection *connection, lockd_lock_queue *lq)
{
    lockd_modes result = NL;
    lockd_lock *l;

    for(l = lq->granted.head; l; l = l->resource_list.next) {
      if(l->connection != connection) {
        result = result | l->mode;
      }
    }
    return result;
}

static int
iscompatible(lockd_lock_queue *lq, lockd_lock *l)
{
  lockd_lock *pending;
  lockd_modes my_status = NL, other_status = NL;

  if(l->connection->fairlock) {
    my_status = computemystate(l->connection, lq);
    if(my_status == NL) {
      pending = lq->pending.head;
      /* If there's a pending lock (not this lock) that isn't compatible wait for it first */
      if(pending && (l != pending) && !compatiblestates(pending->mode, l->mode)) {
        return 0;
      }
    }
  }
  if(compatiblestates(lq->current_modes, l->mode)) {
    return 1;
  }
  /* If I have a write lock, and want a read lock go ahead */
  if((my_status & PW) && (l->mode == PR)) {
    return 1;
  }
  /* Are we compatible with the locks everyone else has */
  other_status = computeotherstate(l->connection, lq);
  return compatiblestates(other_status, l->mode);
}

/**
 *   recomputestate
 *
 *   Set the current state of the lock queue.  Returns a flag if it changed
 *   or not.  If it did then maybe the head of the queue can be granted.
 *
 *   @param lq the queue to calculate the state
 *   @param grantPending non-zero if we should see if the heads of the pending can be granted
 *   @return 0 if it didn't change, 1 otherwise
**/

/* Moves pending lock `l' to granted and tells its connection. */
static void
grant_pending(lockd_lock_queue *lq, lockd_lock *l)
{
  lockd_connection *c = l->connection;
  char tmpbuf[256], *t;

  lock_queue_resource_del(&lq->pending, l);
  lock_queue_resource_add(&lq->granted, l);
  l->state = LOCK_GRANTED;
  lq->current_modes = lq->current_modes | l->mode;
  switch(c->command_format) {
    case CMD_JSON:
      connection_add_response(c, TXlockRequest_CreateJson(lock_to_json(l)));
      break;
    case CMD_SHORT_TEXT:
      sprintf(tmpbuf, "Y:%s:", lockd_mode_to_string(l->mode));
      t = tmpbuf+5;
      if(l->write_times & TABLE_TIMES) {
        t += counter_tostring(&l->resource->table_written, COUNTER_HEX32, t, 255-(t-tmpbuf));
        *t++ = ':';
        *t = '\0';
      }
      if(l->write_times & INDEX_TIMES) {
        t += counter_tostring(&l->resource->index_written, COUNTER_HEX32, t, 255-(t-tmpbuf));
        *t = '\0';
      }
      connection_add_response(c, TXlockRequest_CreateStaticString(tmpbuf, (t-tmpbuf)));
  }
}

static int
recomputestate(lockd_lock_queue *lq, int grantPending)
{
  lockd_lock *l, *next;
  lockd_modes previous_state = lq->current_modes;
  int r;

  lq->current_modes = NL;
  for(l = lq->granted.head; l; l = l->resource_list.next)
  {
    lq->current_modes = lq->current_modes | l->mode;
  }
  r = (lq->current_modes == previous_state) ? 0 : 1;
  if (grantPending) {
    /* Upgrades first, and whether or not the overall state changed: a
     * connection that holds a lock here and waits for a stronger one can
     * never release what the requests ahead of it are waiting for, so
     * left in line it waits for ever.  (When the last other reader
     * leaves, the state is still "read", held by the upgrader itself.) */
    for(l = lq->pending.head; l; l = next) {
      next = l->resource_list.next;
      if(computemystate(l->connection, lq) != NL &&
         compatiblestates(computeotherstate(l->connection, lq), l->mode))
        grant_pending(lq, l);
    }
  }
  if (r && grantPending) {
    l = lq->pending.head;
    while(l && iscompatible(lq, l)) {
      next = l->resource_list.next;
      grant_pending(lq, l);
      l = next;
    }
  }
  return r;
}

static int
lock_queue_connection_add(struct lockd_lock_queue_ptrs *lqp, lockd_lock *l)
{
  l->connection_list.prev = lqp->tail; lqp->tail = l;
  if(l->connection_list.prev) { (l->connection_list.prev)->connection_list.next = l; }
  l->connection_list.next = NULL;
  if(!lqp->head) lqp->head = l;
  return 0;
}

static int
lock_queue_connection_del(struct lockd_lock_queue_ptrs *lqp, lockd_lock *l)
{
  if(!l) return -1;

//  struct lockd_lock_queue_ptrs *lqp = &l->connection->lockq;

  if(l->connection_list.prev) {
    (l->connection_list.prev)->connection_list.next = l->connection_list.next;
  } else {
    lqp->head = l->connection_list.next;
  }
  if(l->connection_list.next) {
    (l->connection_list.next)->connection_list.prev = l->connection_list.prev;
  } else {
    lqp->tail = l->connection_list.prev;
  }
  return 0;
}


static struct lockd_lock_queue_ptrs free_lock_q = {NULL,NULL};

int
lock_create(struct lockd_resource *res, struct lockd_connection *c, lockd_modes mode, lockd_write_times write_times)
{
  lockd_lock *l = NULL;

  if(free_lock_q.head) {
    l = free_lock_q.head;
    lock_queue_connection_del(&free_lock_q, l);
  } else {
    l = (lockd_lock *)calloc(1, sizeof(lockd_lock));
  }
  if(l)
  {
    l->mode = mode;
    l->connection = c;
    l->resource = res;
    l->state = LOCK_NO_QUEUE;
    l->write_times = write_times;
  }
  return lock_queue_lock(l);
}

int
lock_add_to_free_list(lockd_lock *l)
{
  if(!l) return -1;

  l->mode = NL;
  l->connection = NULL;
  l->resource = NULL;
  l->state = LOCK_NO_QUEUE;
  return lock_queue_connection_add(&free_lock_q, l);
}

/* Deadlock detection.  Connection `from' has a request waiting; does it
 * wait, directly or through others, for `target'?  A waiting request waits
 * for every connection holding a conflicting lock on its resource, and --
 * unless it is an upgrade, which is granted ahead -- for every request
 * queued before it there, as the queue is granted in order. */
#define LOCKD_DEADLOCK_MAX 256
static lockd_connection *deadlockSeen[LOCKD_DEADLOCK_MAX];
static int deadlockNumSeen;

static int
waits_for(lockd_connection *from, lockd_connection *target)
{
  lockd_lock *p, *o;
  lockd_lock_queue *lq;
  int i, pass;

  for(i = 0; i < deadlockNumSeen; i++)
    if(deadlockSeen[i] == from) return 0;
  if(deadlockNumSeen >= LOCKD_DEADLOCK_MAX) return 0;
  deadlockSeen[deadlockNumSeen++] = from;

  for(p = from->lockq.head; p; p = p->connection_list.next) {
    if(p->state != LOCK_PENDING || !p->resource) continue;
    lq = &p->resource->lock_queue;
    for(pass = 0; pass < 2; pass++) {
      if(pass == 1 && computemystate(from, lq) != NL) break;      /* an upgrade */
      for(o = (pass ? lq->pending.head : lq->granted.head); o; o = o->resource_list.next) {
        if(pass == 1 && o == p) break;                            /* only those ahead */
        if(o->connection == from) continue;
        if(pass == 0 && compatiblestates(o->mode, p->mode)) continue;
        if(o->connection == target) return 1;
        if(waits_for(o->connection, target)) return 1;
      }
    }
  }
  return 0;
}

int
lock_queue_lock(lockd_lock *l)
{
  lockd_lock_queue *lq;
  struct lockd_connection *c;
  char tmpbuf[256];
  char *t;

  if(!l) return -1;

  lq = &l->resource->lock_queue;
  c = l->connection;

  lock_queue_connection_add(&c->lockq, l);
  if(iscompatible(lq, l))
  {
    lock_queue_resource_add(&lq->granted, l);
    l->state = LOCK_GRANTED;
    /* WTF: Add Response OK */
    recomputestate(lq, 0);
    switch(c->command_format) {
      case CMD_JSON:
        connection_add_response(c, TXlockRequest_CreateJson(lock_to_json(l)));
        break;
      case CMD_SHORT_TEXT:
        sprintf(tmpbuf, "Y:%s:", lockd_mode_to_string(l->mode));
        t = tmpbuf+5;
        if(l->write_times & TABLE_TIMES) {
          t += counter_tostring(&l->resource->table_written, COUNTER_HEX32, t, 255-(t-tmpbuf));
          *t++ = ':';
        }
        if(l->write_times & INDEX_TIMES) {
          t += counter_tostring(&l->resource->index_written, COUNTER_HEX32, t, 255-(t-tmpbuf));
          *t = '\0';
        }
        connection_add_response(c, TXlockRequest_CreateStaticString(tmpbuf, (t-tmpbuf)));
    }
  } else {
    lock_queue_resource_add(&lq->pending, l);
    l->state = LOCK_PENDING;
    deadlockNumSeen = 0;
    if(waits_for(c, c)) {
      /* Granting this would need a lock held by something that is itself
       * waiting for this connection: refuse it, so its statement fails
       * and lets go, instead of all of them waiting for ever. */
      lock_queue_resource_del(&lq->pending, l);
      lock_queue_connection_del(&c->lockq, l);
      switch(c->command_format) {
        case CMD_JSON:
          connection_add_response(c, TXlockRequest_CreateJson(json_pack("{s:b,s:s}", "success", 0, "error", "Deadlock")));
          break;
        case CMD_SHORT_TEXT:
          connection_add_response(c, TXlockRequest_CreateStaticString("E:Deadlock", 10));
      }
      lock_add_to_free_list(l);
      return -1;
    }
  }
  return l->state;
}

static int
lock_queue_unlock_lock(lockd_lock_queue *lq, lockd_lock *l)
{
  switch(l->state) {
    case LOCK_GRANTED:
      lock_queue_resource_del(&lq->granted, l);
      recomputestate(lq, 1);
      break;
    case LOCK_PENDING:
      lock_queue_resource_del(&lq->pending, l);
      break;
    default:
      return -1;
  }
  lock_queue_connection_del(&l->connection->lockq, l);
  lock_add_to_free_list(l);
  return 0;
}

int
lock_queue_unlock(lockd_lock_queue *lq, struct lockd_connection *c, lockd_modes mode)
{
  lockd_lock *l = NULL, *next;
  int n = 0;

  if(!lq) return -1;
  for(l = lq->granted.head; l; l = next)
  {
    next = l->resource_list.next;
    if((l->connection == c) && (l->mode == mode))
    {
      lock_queue_unlock_lock(lq, l);
      n++;
    }
  }
  for(l = lq->pending.head; l; l = next)
  {
    next = l->resource_list.next;
    if((l->connection == c) && (l->mode == mode))
    {
      lock_queue_unlock_lock(lq, l);
      l->state = LOCK_RELEASED;
      n++;
    }
  }
  if(recomputestate(lq, 1)) {
    /* WTF - check pending for locks I can grant */
  }
  if(n > 0) return 0;
  return -1;
}

/**
  * Delete all locks on a connection queue
  * This is called when a connection is aborted
  * Also detach the resources for the locks
 **/

int
lock_connection_queue_unlock(struct lockd_connection *c)
{
  lockd_lock *l, *next;
  lockd_resource *resource;

  for(l = c->lockq.head; l; l = next) {
    next = l->connection_list.next;
    resource = l->resource;
    lock_queue_unlock_lock(&resource->lock_queue, l);
    resource_detach(resource);
  }
  return 0;
}

char *
lockd_mode_to_string(lockd_modes mode)
{
  char *m = "Unknown";

  switch(mode) {
    case NL: m = "NL"; break;
    case CR: m = "CR"; break;
    case CW: m = "CW"; break;
    case PR: m = "PR"; break;
    case PW: m = "PW"; break;
    case IR: m = "IR"; break;
    case IW: m = "IW"; break;
    case EX: m = "EX"; break;
    case PR+IR: m="Read (PR+IR)"; break;
    case PW+IW: m="Write (PW+IW)"; break;
  }
  return m;
}

char *
lockd_status_to_string(lockd_lock_state status)
{
  char *m = "Failed";

  switch(status) {
    case LOCK_NO_QUEUE: m = "No Queue"; break;
    case LOCK_GRANTED:  m = "Granted"; break;
    case LOCK_PENDING:  m = "Pending"; break;
    case LOCK_RELEASED: m = "Released"; break;
  }
  return m;
}

lockd_modes
lockd_string_to_mode(const char *m)
{
  int isread = 0, iswrite = 0, isexcl = 0;
  if(!m) return NL;
  if(m[0] == '\0') return NL;
  switch(m[1]) {
    case 'R': isread=1; break;
    case 'W': iswrite=1; break;
    case 'X': isexcl = 1; break;
    default: return NL;
  }
  switch(m[0])
  {
    case 'P':
      if(isread) {
        return PR;
      } else if(iswrite) {
        return PW;
      }
      return NL;
    case 'I':
      if(isread) {
        return IR;
      } else if(iswrite) {
        return IW;
      }
      return NL;
    case 'B':
      if(isread) {
        return PR|IR;
      } else if(iswrite) {
        return PW|IW;
      }
      return NL;
    case 'C':
      if(isread) {
        return CR;
      } else if(iswrite) {
        return CW;
      }
      return NL;
    case 'N':
      return NL;
    case 'E':
      if(isexcl) {
        return EX;
      }
      return NL;
  }
  return NL;
}

json_t *
lock_to_json(lockd_lock *l)
{
  json_t *r, *t;

  lock_to_string(l);
  r = json_object();

  t = json_string(lockd_mode_to_string(l->mode));
  json_object_set_new(r, "mode", t);
  if(l->resource) {
    t = resource_summary_json(l->resource);
    json_object_set_new(r, "resource", t);
  }
  if(l->connection) {
    t = json_integer((long)l->connection);
    json_object_set_new(r, "connection", t);
  }

  return r;
}

char *
lock_to_string(lockd_lock *l)
{
  char *resource_s;

  if(!l) return NULL;

  resource_s = resource_summary_string(l->resource);
  return resource_s;
}
