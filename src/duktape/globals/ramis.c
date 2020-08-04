#include <stdarg.h> /* va_list etc */
#include <stddef.h> /* size_t */
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include "../../rp.h"
#include "../core/duktape.h"
#include "../../ramis/ramis.h"
#include "../../ramis/resp_protocol.h"
#include "../../ramis/resp_client.h"

/* ************************************************************** 

   RAMIS - redis compatible client functions 

   ************************************************************** */

const char *duk_rp_require_bufOrStr(duk_context *ctx, duk_idx_t idx)
{
  if (duk_is_buffer_data(ctx, idx))
  {
    duk_size_t sz;
    return ((const char *)duk_require_buffer_data(ctx, idx, &sz));
  }
  return duk_require_string(ctx, idx);
}

#define SETARG(type, t, f)  \
  do                        \
  {                         \
    r->type = (t)f(ctx, 1); \
  } while (0)

RP_VA_RET duk_rp_getarg(duk_context *ctx, const char *type)
{
  RP_VA_RET ret, *r = &ret;

  if (duk_is_undefined(ctx, 1))
  {
    duk_push_string(ctx, "not enough arguments for exec(fmt,...)");
    (void)duk_throw(ctx);
  }

  switch (*type)
  {
  case 'c':
  {
    SETARG(c, char *, duk_require_string);
    break;
  }
  case 's':
  {
    SETARG(s, size_t, duk_require_number);
    break;
  }
  case 'i':
  {
    SETARG(i, int, duk_require_int);
    break;
  }
  case 'l':
  {
    if (strlen(type) > 5)
      SETARG(L, long long, duk_require_number);
    else
      SETARG(l, long, duk_require_number);
    break;
  }
  case 'u':
  {
    int len = strlen(type);
    if (len > 13)
      SETARG(I, unsigned long long, duk_require_number);
    else if (len > 9)
      SETARG(U, unsigned long, duk_require_number);
    else
      SETARG(u, unsigned, duk_require_number);
    break;
  }
  case 'd':
  {
    SETARG(d, double, duk_require_number);
    break;
  }
  case 'b':
  {
    SETARG(c, char *, duk_rp_require_bufOrStr);
    break;
  }
  }
  duk_pull(ctx, 1); //move item to top of stack.  next item is now #1
  return ret;
}

RESPROTO *rc_send(duk_context *ctx, RESPCLIENT *rcp)
{
  char *fmt = (char *)duk_require_string(ctx, 0);

  duk_push_undefined(ctx); //marker for end

  return sendRespCommand(rcp, fmt, ctx);
}

void ra_push_response(duk_context *ctx, RESPROTO *response)
{
  int i, endofarray = -1, skipnextput = 0;
  duk_uarridx_t l;

  duk_push_array(ctx); // the return array
  if (response)
  {
    RESPITEM *item = response->items;

    for (i = 0; i < response->nItems; i++, item++)
    {
      switch (item->respType)
      {
      case RESPISNULL:
      {
        duk_push_null(ctx);
        break;
      }
      case RESPISFLOAT:
      {
        duk_push_number(ctx, (double)item->rfloat);
        break;
      }
      case RESPISINT:
      {
        duk_push_number(ctx, (double)item->rinteger);
        break;
      }

      case RESPISARRAY:
      {
        /* create an inner array and keep track
                 of where it's supposed to end */
        duk_push_array(ctx);
        endofarray = i + item->nItems;
        skipnextput = 1;
        break;
      }
      case RESPISBULKSTR:
      {
        size_t l = 1 + strnlen((const char *)item->loc, item->length);
        if (l < item->length)
        { /* if it's binary, put it in a buffer */
          void *b = duk_push_fixed_buffer(ctx, item->length);
          memcpy(b, item->loc, item->length);
          break;
        } /* else fall through and copy string */
      }
      case RESPISSTR:
      case RESPISPLAINTXT:
      {
        duk_push_string(ctx, (const char *)item->loc);
        break;
      }
      /* TODO: how to return errors?? */
      case RESPISERRORMSG:
      {
        duk_push_sprintf(ctx, "Error message: %s\n", item->loc);
        break;
      }
      }

      if (i == endofarray)
      { /* the inner array */
        l = duk_get_length(ctx, -2);
        duk_put_prop_index(ctx, -2, l);
        endofarray = -1;
      }

      l = duk_get_length(ctx, -2);

      if (!skipnextput)
        duk_put_prop_index(ctx, -2, l); //the outer array

      skipnextput = 0;
    }
  }
  //TODO: else NULL response == Error, push something useful or throw
}

duk_ret_t duk_rp_ra_send(duk_context *ctx)
{
  RESPCLIENT *rcp;
  RESPROTO *response;

  /* get the client */
  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient"));
  rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);
  duk_pop_2(ctx);
  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);

  return 1;
}

/* **********************************************************

   SQL and RAMIS constructor functions &
   SQL and RAMIS init function

   ********************************************************** */

/* **************************************************
   Ramis(host,port) constructor
   var ra=new Ramis("127.0.0.1",6379);

   ************************************************** */
duk_ret_t duk_rp_ra_constructor(duk_context *ctx)
{
  const char *ip = duk_get_string_default(ctx, 0, "127.0.0.1");
  int port = (int)duk_get_int_default(ctx, 1, 6379);
  RESPCLIENT *respClient = NULL;
  char stashvar[32];

  if (!duk_is_constructor_call(ctx))
  {
    return DUK_RET_TYPE_ERROR;
  }

  snprintf(stashvar, 32, "%s:%d", ip, port);

  duk_push_heap_stash(ctx);
  if (duk_get_prop_string(ctx, -1, stashvar))
  {
    respClient = (RESPCLIENT *)duk_get_pointer(ctx, -1);
  }
  else
  {
    respClient = connectRespServer((char *)ip, port);
    if (respClient)
    {
      duk_push_pointer(ctx, respClient);
      duk_put_prop_string(ctx, -3, stashvar); /* stack -> [ stash , undefined, pointer ] */
    }
  }

  if (!respClient)
  {
    duk_push_sprintf(ctx, "respClient: Failed to connect to %s:%d\n", ip, port);
    (void)duk_throw(ctx);
  }
  // TODO: ask what this should be set to
  respClient->waitForever = 1;
  //  respClientWaitForever(respClient,1);
  duk_push_this(ctx);
  duk_push_pointer(ctx, (void *)respClient);
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("respclient"));
  return 0;
}
//FIXME: Make this a module or put it in rampart.Ramis */
/* **************************************************
   Initialize ramis client into global object. 
   ************************************************** */
void duk_ra_init(duk_context *ctx)
{
  /* Set up locks:
     this will be run once per new duk_context/thread in server.c
     but needs to be done only once for all threads
  */
  /* Push constructor function */
  duk_push_c_function(ctx, duk_rp_ra_constructor, 2 /*nargs*/);

  /* Push object that will be Sql.prototype. */
  duk_push_object(ctx); /* -> stack: [ Sql protoObj ] */

  /* Set Sql.prototype.exec. */
  duk_push_c_function(ctx, duk_rp_ra_send, DUK_VARARGS); /* [ Sql proto fn_exe ] */
  duk_put_prop_string(ctx, -2, "exec");                  /* [Sql protoObj-->[exe=fn_exe] ] */

  /* Set Sql.prototype = proto */
  duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ Sql-->[prototype-->[exe=fn_exe,...]] ] */

  /* Finally, register Sql to the global object */
  duk_put_global_string(ctx, "Ramis"); /* -> stack: [ ] */
}
