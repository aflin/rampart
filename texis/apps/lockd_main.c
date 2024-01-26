/* For sockaddr_in */
#include <netinet/in.h>
/* For socket functions */
#include <sys/socket.h>
/* For fcntl */
#include <fcntl.h>

/* for setrlimit */
#include <sys/time.h>
#include <sys/resource.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "jansson.h"
#include "lockd.h"

FILE *logfile;

#define MAX_LINE 16384

void do_read(evutil_socket_t fd, short events, void *arg);
void do_write(evutil_socket_t fd, short events, void *arg);

static json_t *
error_json(const char *function, int line, char *message)
{
  json_t *ej = NULL;

  if (!ej) {
    json_t *jt;

    ej = json_object();
    jt = json_true();           json_object_set_new(ej, "error", jt);
    jt = json_string(function); json_object_set_new(ej, "function", jt);
    jt = json_integer(line);    json_object_set_new(ej, "line", jt);
    jt = json_string(message);  json_object_set_new(ej, "message", jt);
  }
  return ej;
}
static json_t *
success_json(void)
{
  json_t *ej = NULL;

  if (!ej) {
    json_t *jt = json_true();

    ej = json_object();
    json_object_set_new(ej, "success", jt);
  }
  return ej;
}

static json_t *
procconnect(json_t *j, lockd_connection *connection)
{
  json_t *jd = json_object_get(j, "database");
  json_t *r = NULL;

  if(jd){
    if(json_is_string(jd)){
      connection_set_database(connection, json_string_value(jd));
      r = success_json();
    }
  }
  jd = json_object_get(j, "pretty");
  if(json_is_true(jd)) {
    connection->json_flags = JSON_INDENT(3);
  } else {
    connection->json_flags = JSON_COMPACT;
  }
  if(!r) r = error_json(__func__, __LINE__, "Connect Failed");
  return r;
}

static json_t *
proccleanup(json_t *j, lockd_connection *connection)
{
  json_t *r = NULL;

  (void)j;
  (void)connection;
  resource_cleanup(NULL, time(NULL));
  r = success_json();
  return r;
}

static char *
j_string_value(char *l, char *k)
{
  size_t qk_len, plen;
  char *qk, *poss, *poss_e, *v = NULL, *d;

  qk_len = strlen(k)+3;
  qk = malloc(qk_len);
  snprintf(qk, qk_len, "\"%s\"", k);
  qk_len--;
  for(poss = strstr(l, qk) + qk_len; poss; poss = strstr(poss, qk) + qk_len) {
    if(*poss == '\0') return NULL;
    while(isspace(*poss)) poss++;
    if(*poss != ':') return NULL;
    poss++;
    while(isspace(*poss)) poss++;
    if(*poss != '\"') return NULL;
    poss++;
    for(poss_e = poss, plen=0; *poss_e && *poss_e != '\"'; poss_e++, plen++) {
      if(*poss_e == '\\') poss_e++;
    }
    v = malloc(plen+1);
    for(poss_e = poss, d=v; *poss_e && *poss_e != '\"'; poss_e++, d++) {
      if(*poss_e == '\\') poss_e++;
      *d = *poss_e;
    }
    *d='\0';
    return v;
  }
  return v;
}

static int
proclock_s(char *l, lockd_connection *connection)
{
  json_t *r;
  char *name, *mode_s;
  lockd_resource *res = NULL;
  lockd_modes mode = EX;

  if(!connection) {
    r = error_json(__func__, __LINE__, "No connection");
    return connection_add_response(connection, TXlockRequest_CreateJson(r));
  }
  if(!connection->database) {
    r = error_json(__func__, __LINE__, "Not connected");
    return connection_add_response(connection, TXlockRequest_CreateJson(r));
  }
  name = j_string_value(l, "name");
  mode_s = j_string_value(l, "mode");

  resource_attach(name, connection->database, &res);
  mode = lockd_string_to_mode(mode_s);
  resource_lock(res, connection, mode, TABLE_INDEX_TIMES);
  if(name) free(name);
  if(mode_s) free(mode_s);
  return 0;
}

static int
proclock_st(char *l, lockd_connection *connection)
{
  int i;
  char *name, *mode_s;
  lockd_resource *res = NULL;
  lockd_modes mode = EX;
  lockd_write_times wt = NO_TIMES;

  if(!connection) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(!connection->database) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(l[0] != 'L' || l[1] != ':') {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Lock Cmd", 14));
  }
  mode_s = l+2;
  for(i=0; i < 5; i++) {
    if(mode_s[i] == '\0') {
      return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Lock Syntax", 13));
    }
  }
  name = l + 7;
  switch(mode_s[3]) {
    case 'T': wt = TABLE_TIMES; break;
    case 'I': wt = INDEX_TIMES; break;
  }

  resource_attach(name, connection->database, &res);
  mode = lockd_string_to_mode(mode_s);
  resource_lock(res, connection, mode, wt);
  return 0;
}

static json_t *
proclock(json_t *j, lockd_connection *connection)
{
  json_t *r = NULL;

  if(!connection) {
    r = error_json(__func__, __LINE__, "No connection");
    goto lockdone;
  }
  if(!connection->database) {
    r = error_json(__func__, __LINE__, "Not connected");
  }
  if(!j) {
    r = error_json(__func__, __LINE__, "No command");
  }

  if(j) {
    json_t *jn = json_object_get(j, "name");
    lockd_resource *res = NULL;
    lockd_modes mode = EX;

    resource_attach(json_string_value(jn), connection->database, &res);
    jn = json_object_get(j, "mode");
    if(jn) {
      mode = lockd_string_to_mode(json_string_value(jn));
    }
    resource_lock(res, connection, mode, TABLE_INDEX_TIMES);
  }
  /* Get Reource Name */
  /* Attach to Resource */
  /* Get Lock Type */
  /* Lock Resource */
lockdone:
  return r;
}

static json_t *
procunlock(json_t *j, lockd_connection *connection)
{
  json_t *t, *r = json_object();
  int l_res;

  if(!connection) {
    r = error_json(__func__, __LINE__, "No connection");
    goto unlockdone;
  }
  if(!connection->database) {
    r = error_json(__func__, __LINE__, "Not connected");
    goto unlockdone;
  }
  if(!j) {
    r = error_json(__func__, __LINE__, "No command");
    goto unlockdone;
  }
  if(j) {
    json_t *jn = json_object_get(j, "name");
    lockd_resource *res = NULL;
    lockd_modes mode = EX;

    res = resource_find(json_string_value(jn), connection->database);
    jn = json_object_get(j, "mode");
    if(jn) {
      mode = lockd_string_to_mode(json_string_value(jn));
    }
    l_res = resource_unlock(res, connection, mode);
    t = json_integer(l_res);
    json_object_set_new(r, "result", t);
    if(connection->json_flags != JSON_COMPACT) {
      t = json_string(lockd_status_to_string(l_res));
      json_object_set_new(r, "result_string", t);
    }
    resource_detach(res);
  }

  /* Get Reource Name */
  /* Attach to Resource */
  /* Get Lock Type */
  /* Unlock Resource */
unlockdone:
  return r;
}

static int
procunlock_st(char *l, lockd_connection *connection)
{
  int i, l_res;
  char *name, *mode_s, response[16];
  lockd_resource *res = NULL;
  lockd_modes mode = EX;

  if(!connection) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(!connection->database) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(l[0] != 'U' || l[1] != ':') {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Unlock Cmd", 16));
  }
  mode_s = l+2;
  for(i=0; i < 3; i++) {
    if(mode_s[i] == '\0') {
      return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Lock Syntax", 13));
    }
  }
  name = l + 5;

  res = resource_find(name, connection->database);
  if(res) {
    mode = lockd_string_to_mode(mode_s);
    l_res = resource_unlock(res, connection, mode);
    switch(l_res) {
      case -1: sprintf(response, "E:%2.2s", mode_s); break;
      case 0: sprintf(response, "Y:%2.2s", mode_s); break;
      default: sprintf(response, "E:Error"); break;
    }
  } else {
    sprintf(response, "E:ErrorRes");
  }
  resource_detach(res);
  return connection_add_response(connection, TXlockRequest_CreateStaticString(response, -1));
}

static int
procquery_st(char *l, lockd_connection *connection)
{
  int i;
  char *name, *mode_s, response[256], *t;
  lockd_resource *res = NULL;

  if(!connection) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(!connection->database) {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Connected", 15));
  }
  if(l[0] != 'Q' || l[1] != ':') {
    return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Not Query Cmd", 15));
  }
  mode_s = l+2;
  for(i=0; i < 5; i++) {
    if(mode_s[i] == '\0') {
      return connection_add_response(connection, TXlockRequest_CreateStaticString("E:Lock Syntax", 13));
    }
  }
  name = l + 7;

  res = resource_find(name, connection->database);
  if(res) {
    sprintf(response, "Y:%c%c:", mode_s[0], mode_s[1]);
    t = response+5;
    switch(mode_s[3]) {
      case 'T':
        t += counter_tostring(&res->table_written, COUNTER_HEX32, t, 255-(t-response));
        break;
      case 'I':
        t += counter_tostring(&res->index_written, COUNTER_HEX32, t, 255-(t-response));
        break;
    }
    *t = '\0';
  } else {
    sprintf(response, "E:ErrorRes");
  }
  return connection_add_response(connection, TXlockRequest_CreateStaticString(response, -1));
}

static json_t *
procstatus(json_t *j, lockd_connection *connection)
{
  json_t *r = json_object();
  json_t *ra = json_array();
  json_t *js;

  lockd_connection *c = connection_enumerate(NULL);

  (void)j; /* Options ? */
  while(c)
  {
    js = connection_to_json(c);
    if (c == connection) {
      json_object_set_new(js, "me", json_true());
      /* This is my connection */
    }
    json_array_append_new(ra, js);
    c = connection_enumerate(c);
  }
  json_object_set_new(r, "connections", ra);
  json_object_set_new(r, "resources", resource_dump_json(NULL));
  return r;
}

static json_t *
proccounter(json_t *j, lockd_connection *connection)
{
  json_t *t, *r = json_object();
  lockd_counter res;
  char tmpbuf[65];

  (void)connection;
  (void)j;
  counter_get(&res);
  counter_tostring(&res, COUNTER_HEX32, tmpbuf, sizeof(tmpbuf));
  t = json_string(tmpbuf);
  json_object_set_new(r, "counter", t);
  return r;
}

static int
proccmd(json_t *j, lockd_connection *connection)
{
  const char *key;
  json_t *r = NULL;
  json_t *value;

  json_object_foreach(j, key, value) {
    if(!strcmp(key, "lock")) {
      r = proclock(value, connection);
    }
    if(!strcmp(key, "unlock")) {
      r = procunlock(value, connection);
    }
    if(!strcmp(key, "connect"))
    {
      r = procconnect(value, connection);
    }
    if(!strcmp(key, "status"))
    {
      r = procstatus(value, connection);
    }
    if(!strcmp(key, "counter"))
    {
      r = proccounter(value, connection);
    }
    if(!strcmp(key, "cleanup"))
    {
      r = proccleanup(value, connection);
    }
  }
  if(!r) return 0;
  return connection_add_response(connection, TXlockRequest_CreateJson(r));
}

void
readcb(struct bufferevent *bev, void *ctx)
{
  struct evbuffer *input, *output;
  char *line, *cmd_s, *cmd_e;
  size_t cmd_len;
  size_t n;
  json_t *j, *t, *tl;
  json_error_t e;
  lockd_connection *connection = ctx;

  input = bufferevent_get_input(bev);
  output = bufferevent_get_output(bev);

  if(connection->response) {
    return;
  }

  if ((line = evbuffer_readln(input, &n, EVBUFFER_EOL_LF))) {
    connection->response_count = 0;
    /*
     * Line format JSON: {"command":....}
     * Get past the {" at
    **/
    if(logfile) {
      fprintf(logfile, "%p< %s\n", connection, line);
    }
    for (cmd_s = line; ((*cmd_s != '\0') && isspace(*cmd_s)); cmd_s++);
    connection->command_format = CMD_SHORT_TEXT;
    switch(*cmd_s) {
      case 'L':
        proclock_st(cmd_s, connection);
        free(line);
        return;
      case 'U':
        procunlock_st(cmd_s, connection);
        free(line);
        return;
      case 'Q':
        procquery_st(cmd_s, connection);
        free(line);
        return;
      case '{': connection->command_format = CMD_JSON; break;
      default:
        connection_add_response(connection, TXlockRequest_CreateStaticString("Unknown", 7));
        free(line);
        return;
    }
    for(cmd_s = line; ((*cmd_s != '\0') && (*cmd_s != '{')); cmd_s++);
    if(*cmd_s == '\0') goto error;
    for(cmd_e = cmd_s, cmd_len = 0; ((*cmd_e != '\0') && (*cmd_e != '\"')); cmd_e++, cmd_len++);
    switch(cmd_len) {
      case 4:
        if (!strncmp(cmd_s, "lock", 4)) {
          proclock_s(line, connection);
          free(line);
          return;
        }
    }
    j = json_loads(line, 0, &e);
    if(!j) {
      connection->command_count = 1;
      t = error_json(__func__, __LINE__, "Invalid JSON");
      tl = json_string(line);
      json_object_set_new(t, "input", tl);
      connection_add_response(connection, TXlockRequest_CreateJson(t));
    } else {
      switch(json_typeof(j)) {
        case JSON_OBJECT:
          connection->command_count = 1;
          proccmd(j, connection);
          break;
        default:
          connection_add_response(connection, TXlockRequest_CreateJson(error_json(__func__, __LINE__, "Not a JSON object")));
      }
      json_decref(j);
    }
    if(line) free(line);
    return;
  error:
    t = error_json(__func__, __LINE__, "Invalid Command");
    connection_add_response(connection, TXlockRequest_CreateJson(t));
    if(line) free(line);
    return;
  }

  if (evbuffer_get_length(input) >= MAX_LINE) {
      /* Too long; just process what there is and go on so that the buffer
       * doesn't grow infinitely long. */
      char buf[1024];
      while (evbuffer_get_length(input)) {
          evbuffer_remove(input, buf, sizeof(buf));
      }
      evbuffer_add(output, "{\"error\":true}", 14);
      evbuffer_add(output, "\n", 1);
  }
}

void
errorcb(struct bufferevent *bev, short error, void *ctx)
{
  lockd_connection *c;

  c = ctx;
  if (error & BEV_EVENT_EOF) {
      /* connection has been closed, do any clean up here */
      /* ... */
  } else if (error & BEV_EVENT_ERROR) {
      /* check errno to see what error occurred */
      /* ... */
  } else if (error & BEV_EVENT_TIMEOUT) {
      /* must be a timeout event handle, handle it */
      /* ... */
  }
  bufferevent_free(bev);
  connection_delete(c);
}

void
do_accept(evutil_socket_t listener, short event, void *arg)
{
    struct event_base *base = arg;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listener, (struct sockaddr*)&ss, &slen);

    if (fd < 0) {
        perror("accept");
#ifdef WIN32
    } else if (fd > FD_SETSIZE) {
        close(fd);
#endif
    } else {
        lockd_connection *ctx = NULL;
        evutil_make_socket_nonblocking(fd);
        connection_create(&ctx);
        ctx->bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(ctx->bev, readcb, NULL, errorcb, (void *)ctx);
        bufferevent_setwatermark(ctx->bev, EV_READ, 0, MAX_LINE);
        bufferevent_enable(ctx->bev, EV_READ|EV_WRITE);
    }
    (void)event;
}

int
run(void)
{
    evutil_socket_t listener;
    struct sockaddr_in sin;
    struct event_base *base;
    struct event *listener_event;

    base = event_base_new();
    if (!base)
        return -1;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = htons(40713);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    evutil_make_socket_nonblocking(listener);

#ifndef WIN32
    {
        int one = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif

    if (bind(listener, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("bind");
        return -1;
    }

    if (listen(listener, 1024)<0) {
        perror("listen");
        return -1;
    }

    listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
    /*XXX check it */
    event_add(listener_event, NULL);

    event_base_dispatch(base);

    return 0;
}


int
main(int c, char **v)
{
  int dologging = 0;
  (void)v;

  // increase ulimit -n to max
  struct rlimit rlp;

  /* set max files open limit to hard limit */
  getrlimit(RLIMIT_NOFILE, &rlp);
  rlp.rlim_cur = rlp.rlim_max;
  setrlimit(RLIMIT_NOFILE, &rlp);

  if(c == 1) {
    daemon(0, 0);
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  logfile = NULL;
  if(dologging) {
    logfile = fopen("/tmp/lockserver.log", "w");
  }
  return run();
}
