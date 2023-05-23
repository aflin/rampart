/* Copyright (C) 2022  Aaron Flin - All Rights Reserved
 * Copyright (C) 2022  P. Barton Richards - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <stdarg.h> /* va_list etc */
#include <stddef.h> /* size_t */
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <ctype.h>
#include "rampart.h"
#include "../../redis/rampart-redis.h"
#include "../../redis/resp_protocol.h"
#include "../../redis/resp_client.h"
#include "event.h"
#include "event2/thread.h"
#include <fcntl.h>

#define ASYNCFLAG        1<<8
#define RETBUFFLAG       1<<9
#define XREADAUTOFLAG    1<<10

/* ************************************************************** 

   rampart-redis - redis compatible client functions 

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
    case 'f':
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

static RESPROTO *rc_send(duk_context *ctx, RESPCLIENT *rcp)
{
  char *fmt = (char *)duk_require_string(ctx, 0);

  duk_push_undefined(ctx); //marker for end

  if( sendRespCommand(rcp, fmt, ctx) )
    return getRespReply(rcp);
  else
    return NULL;
}

static int rc_send_async(duk_context *ctx, RESPCLIENT *rcp)
{
  char *fmt = (char *)duk_require_string(ctx, 0);

  duk_push_undefined(ctx); //marker for end

  return sendRespCommand(rcp, fmt, ctx);
}

static void procstring(duk_context *ctx, RESPITEM *item, int retbuf)
{
  char *s;
  if (item->length >4)
  {
    s = (char *) item->loc + item->length -4;
    if( *s == 'e' && s++ && (*s=='-'||*s=='+') && isdigit(*(s+1)) && isdigit(*(s+2)) )
    {
      double d;
      char *e, t[item->length +1];
      strncpy(t,(char*)item->loc,item->length);
      t[item->length]='\0';
      errno=0;
      d=strtod(t, &e);
      if(!errno && e != t)
      {
        duk_push_number(ctx,d);
        return;
      }
    } 
  }

  s = (char *) item->loc;

  if(isdigit(*s) || *s=='-')
  {
    char *e = (char *) item->loc + item->length;
    s++;
    while (s<e && isdigit(*s))s++;
    if(s==e)
    {
      char t[item->length +1];
      strncpy(t,(char*)item->loc,item->length);
      t[item->length]='\0';
      errno=0;
      long int l = strtol(t, &e, 10);
      if(!errno && e != t)
      {
        duk_push_number(ctx,(double)l);
        return;
      }
    }
  }

  if(retbuf)
  {
    void *b=duk_push_fixed_buffer(ctx, (duk_size_t) item->length);
    memcpy(b, item->loc, item->length);
  }
  else
    duk_push_lstring(ctx, (const char *)item->loc, (duk_size_t) item->length);
  return;
}

#define STANDARDCASES \
  case RESPISNULL:\
  {\
    duk_push_null(ctx);\
    break;\
  }\
  case RESPISFLOAT:\
  {\
    duk_push_number(ctx, (double)item->rfloat);\
    break;\
  }\
  case RESPISINT:\
  {\
    duk_push_number(ctx, (double)item->rinteger);\
    break;\
  }\
  case RESPISBULKSTR:\
  case RESPISSTR:\
  case RESPISPLAINTXT:\
  {\
    procstring(ctx, item, retbuf);\
    break;\
  }\
  case RESPISERRORMSG:\
  {/* this should never happen, as will be the first and only item and thus caught in rd_push_response */\
    RP_THROW(ctx, "Unexpected Error - %s: %s", fname, item->loc);\
    break;\
  }

static int array_push_single(duk_context *ctx, RESPROTO *response, int ridx, const char *fname, int retbuf)
{
  RESPITEM *item = &response->items[ridx];
  switch(item->respType)
  {
    STANDARDCASES
    case RESPISARRAY:
    {
      int i;
      /* sub array */
      ridx++;
      if(item->nItems==1) //don't put single items in an array
        ridx = array_push_single(ctx, response, ridx, fname, retbuf);
      else
      {
        duk_push_array(ctx);
        for (i=0; i<item->nItems && ridx<response->nItems; i++)
        {
          ridx = array_push_single(ctx, response, ridx, fname, retbuf);
          duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));
        }
      }
      return ridx;
    }
  }//switch
  ridx++;
  return ridx;
}

/* assumes everything is in one array of ints 1 or 0*/
static int push_response_array_bool(duk_context *ctx, RESPROTO *response, const char *fname, int retbuf)
{
  int ridx=1;

  if(response->items->respType != RESPISARRAY)
    return -1;

  duk_push_array(ctx);    
  while (ridx < response->nItems)
  {
    RESPITEM *item = &response->items[ridx];
    switch(item->respType)
    {
      case RESPISINT:
        duk_push_boolean(ctx, item->rinteger);
        break;
      default:
        return -1;
    }
    duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));
    ridx++;
  }
  return 0;
} 

/* assumes everything is in one array */
static int push_response_array(duk_context *ctx, RESPROTO *response, const char *fname, int retbuf)
{
  int ridx=1;

  if(response->items->respType != RESPISARRAY)
    return -1;

  duk_push_array(ctx);    
  while (ridx < response->nItems)
  {
    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));
  }
  return 0;
} 

/* for zsets withscores */
static int push_response_array_wscores(duk_context *ctx, RESPROTO *response, const char *fname, int retbuf, int ridx)
{
  int ii=0;

  if(response->items->respType != RESPISARRAY)
    return -1;

  duk_push_array(ctx);    
  while (ridx < response->nItems)
  {
    if(!(ii%2))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if(!(ii%2))
    {
      duk_put_prop_string(ctx, -2, "value");
    }
    else
    {
      duk_put_prop_string(ctx,-2, "score");
      duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2));
    }
    ii++;
  }
  return 0;
} 

/* for blocking zsets withscores, no array, one object only */
static int push_response_kv_wscores(duk_context *ctx, RESPROTO *response, const char *fname, int retbuf, int ridx)
{
  int ii=0;

  if(response->items->respType != RESPISARRAY)
    return -1;

  while (ridx < response->nItems)
  {
    if(!(ii%3))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if((ii%3)==0)
    {
      duk_put_prop_string(ctx, -2, "key");
    }
    else if((ii%3)==1)
    {
      duk_put_prop_string(ctx, -2, "value");
    }
    else
    {
      duk_put_prop_string(ctx,-2, "score");
      return 0;
    }
    ii++;
  }
  return -1;
} 

/* assumes everything is in one array */
static int push_response_object(duk_context *ctx, RESPROTO *response, int ridx, const char *fname, int retbuf)
{
  int ii=0;

  if(response->items->respType != RESPISARRAY)
    return -1;

  duk_push_object(ctx);    
  while (ridx < response->nItems)
  {
    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if( ii%2 )
      duk_put_prop(ctx, -3);
    ii++;    
  }
  return 0;
} 

/* assumes everything is in one array */
static int push_response_object_labeled(duk_context *ctx, RESPROTO *response, int ridx, const char *fname, int retbuf)
{
  int ii=0;

  if(response->items->respType != RESPISARRAY)
    return -1;

  duk_push_object(ctx);    
  while (ridx < response->nItems)
  {
    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if( ii%2 )
      duk_put_prop_string(ctx, -2, "value");
    else
      duk_put_prop_string(ctx, -2, "key");
    ii++;    
  }
  return 0;
} 

/* [key,val], [key,val], ... */
static int push_nested(duk_context *ctx, RESPROTO *response, int ridx, const char *fname, int retbuf, int topnest, const char **lastid)
{
  int ii=0, narry;
  RESPITEM *item = &response->items[ridx];
//printf("entering nested, ridx=%d\n",ridx);
  if(item->respType != RESPISARRAY)
    return -1;

  narry = (int)item->nItems;
  ridx++;
   
  while (ridx < response->nItems && ii < narry)
  {
    item = &response->items[ridx];
    if (item->respType==RESPISARRAY)
    {
      duk_push_object(ctx);
      ridx = push_nested(ctx, response, ridx, fname, retbuf, 0, NULL);
    }
    else
    {
      ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    }

    if(topnest)
    {
      if( ii%2 )
        duk_put_prop_string(ctx, -2, "value");
      else
      {
        if(lastid)
            *lastid=duk_get_string(ctx, -1);
        duk_put_prop_string(ctx, -2, "id");
      }
    }
    else
    {
      if( ii%2 )
      {
        //if(lastid)
        //  *lastid=duk_get_string(ctx, -2);
        duk_put_prop(ctx, -3);
      }
    }
    ii++;    
  }
  return ridx;
} 
/* [ [key,val], [key,val], ... ]*/
static int push_response_object_nested(duk_context *ctx, RESPROTO *response, int ridx, const char *fname, int retbuf, const char **lastid)
{
  int ii=0, narry;
  RESPITEM *item = &response->items[ridx];

  if(item->respType != RESPISARRAY)
    return -1;

  narry = (int)item->nItems;
  duk_push_array(ctx);
  ridx++;
   
  while (ridx < response->nItems && ii < narry)
  {
    item = &response->items[ridx];
    if (item->respType==RESPISARRAY)
    {
      duk_push_object(ctx);
      ridx = push_nested(ctx, response, ridx, fname, retbuf, 1, lastid);
      duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2) );
    }
    else
      RP_THROW(ctx, "rampart-redis %s - Unexpected format of response from redis server", fname);
    ii++;    
  }
  return ridx;
} 

static int rd_push_response(duk_context *ctx, RESPROTO *response, const char *fname, int type)
{
  int retbuf = type & RETBUFFLAG;
  int ret=0;

  type &= 255;

  if (response)
  {
    RESPITEM *item = response->items;
    if( response->nItems == 1)
    {
      if(item->respType == RESPISERRORMSG)
      {
        duk_push_this(ctx);
        duk_push_string(ctx, (const char *)item->loc);
        duk_put_prop_string(ctx, -2, "errMsg");
        duk_pop(ctx);
        return 0;
      }
      else if(item->respType == RESPISARRAY) //empty list reply
      {
        //duk_push_null(ctx);
        duk_push_array(ctx); //return empty array
        return 1;
      }
      if(type!=5)
        type=0;//only one item, return it as is regardless of type
    }

    switch(type)
    {
      case 0:
        switch(item->respType)
        {
          STANDARDCASES
          default:
            ret = -1;
        }
        break;
      case 1:
        ret = push_response_array(ctx, response, fname, retbuf);
        break;
      case 2:
        duk_push_array(ctx);
        ret = push_response_array(ctx, response, fname, retbuf);
        break;
      case 3:
        ret = push_response_object(ctx, response, 1, fname, retbuf);
        break;
      case 4:
        //rd_push_response_object_nested(ctx, &item, 0, response->nItems, fname, retbuf);
//        duk_push_object(ctx);
        ret = push_response_object_nested(ctx, response, 0, fname, retbuf, NULL);
        break;
      case 5:
        if(item->respType== RESPISINT)
        {
          if(item->rinteger)
            duk_push_true(ctx);
          else
            duk_push_false(ctx);
        }
        else
          ret = -1;
        break;
      case 6:
        duk_push_object(ctx);
        array_push_single(ctx, response, 1, fname, 0);
        duk_put_prop_string(ctx, -2, "cursor");
        ret = push_response_object(ctx, response, 3, fname, retbuf);
        if(ret>-1)
          duk_put_prop_string(ctx, -2, "values");
        break;
      case 7:
        duk_push_object(ctx);
        array_push_single(ctx, response, 1, fname, 0);
        duk_put_prop_string(ctx, -2, "cursor");
        response->nItems-=2;
        response->items+=2;
        ret = push_response_array(ctx, response, fname, retbuf);
        if(ret>-1)
          duk_put_prop_string(ctx, -2, "values");
        response->nItems+=2;
        response->items-=2; //restore for free
        break;
      case 8:
        ret = push_response_array_bool(ctx, response, fname, retbuf);
        break;
      case 9:
        ret = push_response_array_wscores(ctx, response, fname, retbuf, 1);
        break;
      case 10:
        duk_push_object(ctx);
        array_push_single(ctx, response, 1, fname, 0);
        duk_put_prop_string(ctx, -2, "cursor");
        ret = push_response_array_wscores(ctx, response, fname, retbuf, 3);
        duk_put_prop_string(ctx, -2, "values");
        break;        
      case 11:
      {
        int nmem, i=0,ridx=2;
        if(response->items->respType != RESPISARRAY)
        {
          ret = -1;
          break;
        }

        nmem=(int)response->items->nItems;
        duk_push_array(ctx);
        while(i<nmem)
        {
          duk_push_object(ctx);
          array_push_single(ctx, response, ridx, fname, 0);
          duk_put_prop_string(ctx, -2, "stream");
          ridx+=1;
          ridx=push_response_object_nested(ctx, response, ridx, fname, retbuf, NULL);
          if(ridx < 0)
          {
            ret = -1;
            break;
          }
          duk_put_prop_string(ctx, -2, "data");
          duk_put_prop_index(ctx, -2, duk_get_length(ctx,-2));
          ridx++;
          i++;
        }
        break;
      }
      case 12:
        ret = push_response_object_labeled(ctx, response, 1, fname, retbuf);
        break;
      case 13:
        ret = push_response_kv_wscores(ctx, response, fname, retbuf, 1);
        break;
    }      
  }
  else
     return 0;
    //RP_THROW(ctx, "rampart-redis - error getting response from redis server (disconnected?)");

  if(ret < 0)
  {
    duk_push_this(ctx);
    duk_push_string(ctx, "Unexpected format of response from redis server");
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
    return 0;
  }

  return 1;
}

#define HANDLE_PCALL_ERR \
if(ret!=DUK_EXEC_SUCCESS) {\
      if (duk_is_error(ctx, -1) ){\
          duk_get_prop_string(ctx, -1, "stack");\
          if(isasync) fprintf(stderr, "error in redis async callback: '%s'\n", duk_safe_to_string(ctx, -1));\
          else RP_THROW(ctx, "%s", duk_safe_to_string(ctx, -1));\
      }\
      else if (duk_is_string(ctx, -1)){\
          if(isasync) fprintf(stderr, "error in redis async callback: '%s'\n", duk_safe_to_string(ctx, -1));\
          else RP_THROW(ctx, "%s", duk_safe_to_string(ctx, -1));\
      } else {\
          if(isasync) fprintf(stderr,"unknown error in redis async callback");\
          else RP_THROW(ctx, "unknown error in callback");\
      }\
}

#define docallback do{          \
  int ret;                      \
  if(format_err) {              \
    duk_push_undefined(ctx);    \
    duk_push_string(ctx, "Unexpected format of response from redis server");\
    duk_put_prop_string(ctx, this_idx, "errMsg");\
  }\
  duk_dup(ctx, cb_idx);         \
  duk_dup(ctx, this_idx);       \
  duk_pull(ctx, -3);            \
  ret=duk_pcall_method(ctx, 1);                 \
  HANDLE_PCALL_ERR \
  if(!duk_get_boolean_default(ctx, -1, 1)) \
    goto cb_end;\
  duk_pop(ctx);/*return value*/\
  total++;  \
  /* if we closed in the callback, response, items, rcp etc will be freed and invalid */\
  /* and removed from respclient in 'this' */\
  if( !duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("respclient")) )\
    RP_THROW(ctx, "internal error checking connection");\
  if(format_err || !duk_has_prop_string(ctx, -1, (isasync?"async_client_p":"client_p")) ){\
    duk_pop(ctx);\
    goto cb_end;\
  }\
  duk_pop(ctx);\
}while(0)

static void push_response_cb_scores(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0, ii=0;
  int isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;

  if(response->items->respType != RESPISARRAY)
  {
    format_err = 1;
    docallback; //goto cb_end
  }

  while (ridx < response->nItems)
  {
    if(!(ii%2))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if(!(ii%2))
    {
      duk_put_prop_string(ctx, -2, "value");
    }
    else
    {
      duk_put_prop_string(ctx,-2, "score");
      docallback;
    }
    ii++;
  }

  cb_end:
  duk_push_int(ctx, total);
}

static void push_response_cb_kv_scores(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0, ii=0;
  int isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;

  if(response->items->respType != RESPISARRAY)
  {
    format_err = 1;
    docallback; //goto cb_end
  }

  while (ridx < response->nItems)
  {
    if(!(ii%3))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    if((ii%3)==0)
    {
      duk_put_prop_string(ctx, -2, "key");
    }
    else if((ii%3)==1)
    {
      duk_put_prop_string(ctx, -2, "value");
    }
    else
    {
      duk_put_prop_string(ctx,-2, "score");
      docallback;
    }
    ii++;
  }

  cb_end:
  duk_push_int(ctx, total);
}

static void push_arrays(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0;
  int isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;

  if(response->items->respType != RESPISARRAY)
    format_err = 1;

  while (ridx < response->nItems)
  {
    ridx = array_push_single(ctx, response, ridx, fname, retbuf);
    docallback;
  }

  cb_end:
  duk_push_int(ctx, total);
}


/* expecting [val, val, val, ...] */
static void push_response_cb_single(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag)
{
  push_arrays(ctx, response, cb_idx, this_idx, fname, flag, 1);
}

/* expect [ [val, val], [val, val], ...] */
static void push_response_cb_array(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag)
{
  push_arrays(ctx, response, cb_idx, this_idx, fname, flag, 0);
}

static void push_response_cb_single_bool(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag)
{
  int total=0, format_err=0;
  int isasync = flag & ASYNCFLAG;
  int ridx=1;

  if(response->items->respType != RESPISARRAY)
    format_err = 1;

  while (ridx < response->nItems)
  {
    RESPITEM *item = &response->items[ridx];
    switch(item->respType)
    {
      case RESPISINT:
        duk_push_boolean(ctx, item->rinteger);
        break;
      default:
        format_err = 1;
    }
    docallback;
    ridx++;
  }

  cb_end:
  duk_push_int(ctx, total);
}

/* expecting [key,val,key,val,...] -> {key:val, key:val,...}  */
static void push_response_cb_keyval(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0, ii=0;
  int isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;

  if(response->items->respType != RESPISARRAY)
    format_err = 1;

  while (ridx < response->nItems)
  {
    if(!(ii%2))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);

    if (ii%2)
    {
      duk_put_prop(ctx, -3);
      docallback;
    }
    ii++;
  }

  cb_end:
  duk_push_int(ctx, total);
}

/* expecting [key,val,key,val,...] -> {key:val, key:val,...}  */
static void push_response_cb_keyval_labeled(duk_context *ctx, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0, ii=0;
  int isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;

  if(response->items->respType != RESPISARRAY)
    format_err = 1;

  while (ridx < response->nItems)
  {
    if(!(ii%2))
      duk_push_object(ctx);

    ridx = array_push_single(ctx, response, ridx, fname, retbuf);

    if (ii%2)
    {
      duk_put_prop_string(ctx, -2, "value");
      docallback;
    }
    else
    {
      duk_put_prop_string(ctx, -2, "key");
    }
    ii++;
  }

  cb_end:
  duk_push_int(ctx, total);
}

/* [ [key,val], [key,val], ... ]*/
static void push_response_object_cb_nested(duk_context *ctx, RESPROTO *response,  duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag, int ridx)
{
  int total=0, format_err=0, isasync = flag & ASYNCFLAG, retbuf = flag & RETBUFFLAG;
  int ii=0, narry;
  RESPITEM *item = &response->items[ridx];

  if(item->respType != RESPISARRAY)
    format_err = 1;

  narry = (int)item->nItems;
  ridx++;
   
  while (ridx < response->nItems && ii < narry)
  {
    item = &response->items[ridx];
    if (item->respType==RESPISARRAY)
    {
      duk_push_object(ctx);
      ridx = push_nested(ctx, response, ridx, fname, retbuf, 1, NULL);
    }
    else
      format_err = 1;

    docallback;
    ii++;    
  }
  cb_end:
  duk_push_int(ctx, total);
} 

static void rd_push_response_cb(duk_context *ctx, RESPCLIENT *rcp, RESPROTO *response, duk_idx_t cb_idx, duk_idx_t this_idx, const char *fname, int flag)
{
  int type = flag & 255;
  int retbuf = flag & RETBUFFLAG;
  int isasync = flag & ASYNCFLAG;
  int xreadauto = flag & XREADAUTOFLAG;
  int goterr=0;

  /* reset error message */
  duk_push_string(ctx, "");
  duk_put_prop_string(ctx, this_idx, "errMsg");

  if (response)
  {
    RESPITEM *item = response->items;
    if( response->nItems == 1)
    {
      if(item->respType == RESPISERRORMSG)
      {
        duk_push_string(ctx, (const char *)item->loc);
        goterr=1;
      }
      else if(item->respType == RESPISARRAY) //empty list reply
      {
        duk_push_int(ctx,0);
        return;
      }
      if(type != 5)
        type=0;//only one item, return it as is regardless of type
    }
  }
  else
  {
    goterr=1;
    duk_push_sprintf(ctx, "%s: redis server connection error:\n%s", fname, (rcp->rppFrom->errorMsg?rcp->rppFrom->errorMsg:"Unknown Error") );
  }

  if(goterr)
  {
    int ret;

    duk_put_prop_string(ctx, this_idx, "errMsg");
    duk_dup(ctx, cb_idx);
    duk_dup(ctx, this_idx);
    ret=duk_pcall_method(ctx, 0);

    HANDLE_PCALL_ERR

    duk_push_int(ctx, 0);
    return;
  }

  switch(type)
  {
    case 0:
    {
      RESPITEM *item = response->items;
      int isasync = flag & ASYNCFLAG;
      int total=0, format_err=0;
      switch(item->respType)
      {
        STANDARDCASES
        default:
          format_err = 1;
      }
      docallback;
      duk_push_int(ctx, total);
      break;
    }
    case 1:
      push_response_cb_single(ctx, response, cb_idx, this_idx, fname, flag);
      break;
    case 2:
      push_response_cb_array(ctx, response, cb_idx, this_idx, fname, flag);
      break;
    case 3:
      push_response_cb_keyval(ctx, response, cb_idx, this_idx, fname, flag, 1);
      break;
    case 4:
      //push_response_cb_keyval_pairs(ctx, response, cb_idx, this_idx, fname, flag);
      push_response_object_cb_nested(ctx, response,  cb_idx, this_idx, fname, flag, 0);
      break;
    case 5:
    {
      RESPITEM *item = response->items;
      int isasync = flag & ASYNCFLAG;
      int total=0, format_err=0;
      switch(item->respType)
      {
        case RESPISINT:
          duk_push_boolean(ctx, item->rinteger);
          break;
        default:
          format_err = 1;
      }
      docallback;
      duk_push_int(ctx, total);
      break;
    }
    case 6:
      push_response_cb_keyval(ctx, response, cb_idx, this_idx, fname, flag, 3);
      array_push_single(ctx, response, 1, fname, 0); //return cursor
      break;
    case 7:
      push_arrays(ctx, response, cb_idx, this_idx, fname, flag, 3);
      array_push_single(ctx, response, 1, fname, 0); //return cursor
      break;
    case 8:
      push_response_cb_single_bool(ctx, response, cb_idx, this_idx, fname, flag);
      break;
    case 9:
      push_response_cb_scores(ctx, response, cb_idx, this_idx, fname, flag, 1);
      break;
    case 10:
      push_response_cb_scores(ctx, response, cb_idx, this_idx, fname, flag, 3);
      array_push_single(ctx, response, 1, fname, 0);
      break;
    case 11:
    {
      int nmem, i=0,ridx=2,total=0, format_err=0;
      int isasync = flag & ASYNCFLAG;

      if(response->items->respType != RESPISARRAY)
      {
        format_err = 1;
        docallback; //goto cb_end
      }

      nmem=(int)response->items->nItems;

      if(xreadauto)
      {
        RESPCLIENT *subrcp=NULL;
        duk_idx_t xo_idx, j;
        int nargs=0;
        char *fmt=NULL;

        duk_get_prop_string(ctx, this_idx, "xreadArgs");
        REQUIRE_OBJECT(ctx, -1, "xread_auto_async: xreadArgs must be an Object");

        xo_idx=duk_get_top_index(ctx);

        while(i<nmem)
        {
          const char *lastid, *streamid;
          duk_push_object(ctx);
          array_push_single(ctx, response, ridx, fname, 0);
          streamid=duk_get_string(ctx, -1);
          duk_put_prop_string(ctx, -2, "stream");
          ridx+=1;
          ridx=push_response_object_nested(ctx, response, ridx, fname, retbuf, &lastid);
          if(ridx<0)
          {
            format_err = 1;
            docallback; //goto cb_end
          }
          duk_put_prop_string(ctx, -2, "data");
          ridx++;
          i++;

          /* update last id for next call to xread_auto_async before callback 
             as duktape will free them after the callback                      */
          duk_push_string(ctx, streamid);
          duk_push_string(ctx, lastid);
          duk_put_prop(ctx, xo_idx);

          docallback;

        }
        // get the array of arguments and the object holding the stream ids and last ids
        duk_pull(ctx, xo_idx);
        //get the current rcp pointer for this async connection
        if( !duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("respclient")) )
          RP_THROW(ctx,"async: internal error getting redis handle object");
        if(duk_get_prop_string(ctx, -1, "async_client_p")) 
        {
          subrcp=duk_get_pointer(ctx,-1);
          duk_pop_2(ctx);
        } 
        else
          RP_THROW(ctx,"async: internal error getting redis handle");

        //leave only the top one on the stack (xreadargs_array)
        while(duk_get_top(ctx)>1) duk_remove(ctx,0);

        // push streamids , lastids in order [stream1, stream2, ..., id1, id2, ...]
        j=duk_get_top(ctx)+1;
        duk_enum(ctx, 0, 0);
        duk_insert(ctx, 0);
        while(duk_next(ctx, 0, 1))
        {
          if(duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
          {
            // if set to false in callback, "unsubscribe" (ie, on next round dont listen for) that stream
            duk_pop_2(ctx);
          }
          else
          {
            duk_pull(ctx, -2);
            duk_insert(ctx, j);
            j++;
            nargs+=2;
          }
        }

        duk_remove(ctx,0);//xread object
        duk_remove(ctx,0);//enum

        REMALLOC(fmt, 22 + (nargs*3));
        strcpy(fmt,"XREAD BLOCK 0 STREAMS");
        while(nargs--)
          strcat(fmt, " %s");
        duk_push_string(ctx, fmt);
        duk_insert(ctx, 0);
        free(fmt);

        /* refresh the current xread command exactly as given in the last
           round, except with updated ids for subsequent messages         */
        if(!rc_send_async(ctx, subrcp))
          RP_THROW(ctx, "error sending command to redis server");

        /* stack will be cleared in rp_rdev_doevent */ 

      }
      else
      {
        while(i<nmem)
        {
          duk_push_object(ctx);
          array_push_single(ctx, response, ridx, fname, 0);
          duk_put_prop_string(ctx, -2, "stream");
          ridx+=1;
          ridx=push_response_object_nested(ctx, response, ridx, fname, retbuf, NULL);
          duk_put_prop_string(ctx, -2, "data");
          ridx++;
          i++;
          docallback;
        }
      }
      duk_push_int(ctx, total);
    }//case 11
    case 12:
      push_response_cb_keyval_labeled(ctx, response, cb_idx, this_idx, fname, flag, 1);
      break;
    case 13:
      push_response_cb_kv_scores(ctx, response, cb_idx, this_idx, fname, flag, 1);
      break;
  }
  cb_end:
  return;
}

/*************** FUNCTION FOR init ****************************** */
#define checkresp do {\
  /* thread_copied is set in rampart-server if object was copied from a different thread */\
  /* In that case, we need to open a separate connection to redis */\
  if(duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thread_copied") ) ){\
    int port; const char *ip;\
    duk_del_prop_string(ctx, -1, "async_client_p");\
    duk_get_prop_string(ctx, -1, "ip");\
    ip=duk_get_string(ctx, -1);\
    duk_pop(ctx);\
    duk_get_prop_string(ctx, -1, "port");\
    port=duk_get_int(ctx, -1);\
    duk_pop(ctx);\
    duk_pop(ctx);\
    duk_del_prop_string( ctx, -1, DUK_HIDDEN_SYMBOL("thread_copied") );\
    rcp= connectRespServer((char *)ip, port);\
    if(!rcp) RP_THROW(ctx,"could not reconnect to resp server");\
    duk_get_prop_string(ctx, -1, "timeout");\
    rcp->timeout=duk_get_int(ctx, -1);\
    duk_put_prop_string(ctx, -2, "client_p");\
  }\
} while(0)

#define getsubresp do {\
    int port; const char *ip;\
    duk_push_this(ctx);\
    if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) )\
      RP_THROW(ctx,"async: internal error getting ip and port");\
    if(duk_get_prop_string(ctx, -1, "async_client_p")) {\
      subrcp=duk_get_pointer(ctx,-1);\
      duk_pop_3(ctx);\
      break;\
    } else duk_pop(ctx);\
    duk_get_prop_string(ctx, -1, "ip");\
    ip=duk_get_string(ctx, -1);\
    duk_pop(ctx);\
    duk_get_prop_string(ctx, -1, "port");\
    port=duk_get_int(ctx, -1);\
    duk_pop(ctx);\
    subrcp= connectRespServer((char *)ip, port);\
    if(!subrcp) RP_THROW(ctx,"subscribe: could not connect to resp server");\
    subrcp->timeout=-1;/* always blocking for async */\
    duk_push_pointer(ctx, (void *) subrcp);\
    duk_put_prop_string(ctx, -2, "async_client_p");\
    duk_pop_2(ctx);\
} while(0)

#define RDEVARGS struct rd_event_args_s

RDEVARGS {
  duk_context *ctx;
  struct event *e;
  RESPCLIENT *rcp;
  char * fname;
  int flag;
};


static duk_ret_t _close_async_(duk_context *ctx)
{
  RESPCLIENT *subrcp=NULL;
  RDEVARGS *args;

  if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("args")) )
  {
    args = duk_get_pointer(ctx,-1);
    event_del(args->e);
    event_free(args->e);
    free(args->fname);
    free(args);
    duk_del_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("args"));
  }
  duk_pop(ctx);

  if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) )
    RP_THROW(ctx,"close_async: internal error getting client handle");
  if(duk_get_prop_string(ctx, -1, "async_client_p")) 
  {
    subrcp = duk_get_pointer(ctx,-1);
    duk_del_prop_string(ctx, -2, "async_client_p");
    subrcp = closeRespClient(subrcp);
  }
  duk_pop_2(ctx);
  return 0;
}

static duk_ret_t duk_rp_rd_close_async(duk_context *ctx)
{
  duk_push_this(ctx);
  return _close_async_(ctx);
}

static duk_ret_t _close_(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;

  if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) )
    RP_THROW(ctx,"close: internal error getting client handle");

  if(duk_get_prop_string(ctx, -1, "client_p")) 
  {
    rcp = duk_get_pointer(ctx,-1);
    duk_del_prop_string(ctx, -2, "client_p");
    rcp = closeRespClient(rcp);
  }
  duk_pop_2(ctx);

  return _close_async_(ctx);
}

static duk_ret_t duk_rp_rd_close(duk_context *ctx)
{
  duk_push_this(ctx);
  return _close_(ctx);
}

static void rp_rdev_doevent(evutil_socket_t fd, short events, void* arg)
{
  RDEVARGS *args = (RDEVARGS *)arg;
  duk_context *ctx = args->ctx;
  RESPROTO *response;

  //start with empty stack
  while (duk_get_top(ctx) > 0) duk_pop(ctx);

  if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rd_event")) )
    RP_THROW(ctx, "internal error in redis async callback");

  duk_push_pointer(ctx, (void*)args->rcp); // [ ..., rd_event, pointer ]
  duk_get_prop(ctx, -2);// [ ..., rd_event, this ]
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("subcallback"));// [ ..., rd_event, this, callback ]
  duk_remove(ctx,-3);// [ ..., this, callback ];

  // get response from redis
  response = getRespReply(args->rcp);
  //parse response, run callback
  rd_push_response_cb(ctx, args->rcp, response, duk_normalize_index(ctx, -1), duk_normalize_index(ctx, -2), args->fname, args->flag);
  // [ ..., this, callback, callback_return_val ]

  // if not subscribe or xread_auto_async, its a one time event.
  if(strcmp("subscribe", args->fname) && strcmp("xread_auto_async", args->fname) )
  {
    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rd_event"));
    duk_push_pointer(ctx, (void*)args->rcp);
    duk_get_prop(ctx, -2);
    _close_async_(ctx);
  }
  //end with empty stack
  while (duk_get_top(ctx) > 0) 
    duk_pop(ctx);
  // [ ]
}


#define IF_FMT_INSERT \
    if( duk_is_buffer_data(ctx, i))\
    {\
      duk_push_int(ctx, (int)duk_get_length(ctx, i));\
      i++;\
      duk_insert(ctx, i);\
      top=duk_get_top(ctx);\
      strcat(fmt," %b");      \
    }\
    else if (duk_is_string(ctx, i))\
      strcat(fmt," %s");\
    else if (duk_is_number(ctx, i))\
    {\
      double d, d2 = duk_get_number(ctx, i);\
      d = (double)((int)d2);\
      d -= d2;\
      if( ! (d<0.0 || d>0.0) )\
        strcat(fmt," %lld");\
      else\
        strcat(fmt," %lf");\
    }

#define ELSE_IF_FMT_INSERT\
  else if (duk_is_object(ctx, i))\
  {\
    duk_json_encode(ctx, i);\
    strcat(fmt," %s");\
  }\
  else\
  {\
    duk_safe_to_string(ctx,i);\
    strcat(fmt," %s");\
  }

/* generic function for all the rcl.xxx() functions */

static duk_ret_t duk_rp_cc_docmd(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  duk_idx_t top=0, i=0;
  int gotfunc=0, isasync=0, commandtype=0, retbuf=0, totalels=0;
  const char *fname, *cname;
  duk_size_t sz;

  duk_push_this(ctx);
  /* reset any error messages */
  duk_push_string(ctx, "");
  duk_put_prop_string(ctx, -2, "errMsg");

  /* get the client */
  if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) ) 
  {
    checkresp;
    duk_get_prop_string(ctx, -1, "client_p");
    rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
  }
  duk_pop_2(ctx);

  if( rcp == NULL )
    RP_THROW(ctx, "error: rampart-redis - client not connected");

  top=duk_get_top(ctx);


  duk_push_current_function(ctx);

  duk_get_prop_string(ctx, -1, "type");
  commandtype = duk_get_int(ctx,-1);
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "fname");
  fname = duk_get_lstring(ctx, -1, &sz);
  duk_pop(ctx);

  //zpop is always 9
  if(commandtype==9 && strcmp(fname,"zpopmin") && strcmp(fname,"zpopmax"))
  {
    duk_idx_t last = top-1;
    while(last > 0 && !duk_is_string(ctx, last)) last--;
    // if "WITHSCORES" is not present, type 9 becomes type 1 */
    if( !duk_is_string(ctx,last) || strcasecmp("WITHSCORES", duk_get_string(ctx,last))!=0 )
      commandtype=1;
  }

  if( !strcmp( (fname + sz - 6), "_async") )
    isasync=1;
  else if( 
      !strcmp(fname, "subscribe") || !strcmp(fname, "psubscribe")  ||
      !strcmp(fname, "unsubscribe") || !strcmp(fname, "punsubscribe")
    )
    isasync=1;

  /* for all commands except format & format_async - generate the format string */
  if(strcmp(fname, "format") != 0 && strcmp(fname, "format_async") != 0)
  {
    char *fmt=NULL;

    i=0;
    /* get count of elements for the format, including array members and object keys and vals */
    while(i < top)
    {
      if(duk_is_ecmascript_function(ctx, i)||duk_is_boolean(ctx, i))
      {
        i++;
        continue;
      }
      else if(duk_is_array(ctx,i))
        totalels += (int)duk_get_length(ctx,i);
      else if(duk_is_buffer_data(ctx,i))
        totalels++;
      else if(duk_is_object(ctx, i))
      {
        int keys=0;
        duk_enum(ctx, i, 0);
        while(duk_next(ctx, -1, 0))
        {
          duk_pop(ctx);
          keys++;
        }
        duk_pop(ctx);
        totalels += keys * 2;
      }
      else
        totalels++;
      i++;
    }

    duk_get_prop_string(ctx, -1, "command");
    cname=duk_get_lstring(ctx, -1, &sz);
    REMALLOC(fmt, 5 * (size_t)totalels + (sz + 1) );
    strcpy(fmt, cname);
    duk_pop_2(ctx);//command and current_function

    i=0;
    while (i < top)
    {
      IF_FMT_INSERT
      else if (duk_is_ecmascript_function(ctx, i))
      {
        if (!gotfunc)
        {
          //get callback out of the way
          duk_push_this(ctx);
          duk_pull(ctx,i);
          if(isasync)
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("subcallback"));
          else
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("callback"));
          gotfunc=1;
          duk_pop(ctx);//this
        }
        else
          duk_remove(ctx,i);

        top=duk_get_top(ctx);
        i--;
      }
      else if (duk_is_array(ctx, i))
      {
        duk_idx_t oldi=i;
        duk_uarridx_t j = 0, l = duk_get_length(ctx, i);
        for (;j<l;j++)
        {
          i++;
          duk_get_prop_index(ctx, oldi, j);
          duk_insert(ctx, i);
          IF_FMT_INSERT
          ELSE_IF_FMT_INSERT
        }
        duk_remove(ctx, oldi);
        i--;
        top=duk_get_top(ctx);
      } 
      else if (duk_is_object(ctx, i))
      {
        duk_idx_t oldi=i, ins_idx;
        /* if stack idx 0 is object and has property "returnBuffer" and/or "async", it's an option object */
        if(i==0 && ( 
                      duk_has_prop_string(ctx,0,"returnBuffer") || 
                      duk_has_prop_string(ctx,0,"async")        ||
                      duk_has_prop_string(ctx,0,"timeout")
                   ) 
          )
        {
          if(duk_get_prop_string(ctx, 0, "returnBuffer") && duk_get_boolean_default(ctx, -1, 0))
            retbuf = RETBUFFLAG;
          duk_pop(ctx);

          if(duk_get_prop_string(ctx, 0, "async") && duk_get_boolean_default(ctx, -1, 0))
            isasync = 1;
          duk_pop(ctx);

          if(duk_get_prop_string(ctx, 0, "timeout"))
            rcp->timeout = (int)(1000.0 * REQUIRE_NUMBER(ctx, -1, "redis.init - property 'timeout' must be a Number (timeout in seconds)") );
          duk_pop(ctx);

          duk_remove(ctx, oldi);//object
        }
        else if(strcmp(fname,"xread_auto_async")==0)
        {
          /* turn [ ..., { s1: "0-0", s2: "0-1", ...} ]
             into [ ..., s1, s2, "0-0","0-1", ... ]        
             for: XREAD [COUNT count] [BLOCK milliseconds] STREAMS key [key ...] ID [ID ...]  */ 
          duk_idx_t enum_idx=duk_get_top(ctx);//next item to top the stack will be the enum below

          duk_push_this(ctx);
          duk_dup(ctx, i);
          duk_put_prop_string(ctx, -2, "xreadArgs");
          duk_pop(ctx);//this
          commandtype|=XREADAUTOFLAG;

          duk_enum(ctx, i, 0);
          ins_idx=duk_get_top(ctx); //where the stream names go
          while(duk_next(ctx, enum_idx, 1))
          {
            //new location for key
            duk_pull(ctx, -2);//do key first
            duk_insert(ctx, ins_idx);
            //leave val where it is, but make sure it's a string
            (void)duk_to_string(ctx, -1);
            ins_idx++;
          }
          duk_remove(ctx, enum_idx);
          duk_remove(ctx, oldi);//object
        }
        else if(strcmp(fname,"zadd")==0)
        {
          /* turn [ ..., { val1: 0, val2: 1} ]
             into [ ..., 0, val1, 1, val2, ... ]        
             for:  ZADD key [NX|XX] [GT|LT] [CH] [INCR] score member [score member ...]  */
          duk_idx_t enum_idx=duk_get_top(ctx);//next item to top the stack will be the enum below

          ins_idx = i+1;
          duk_enum(ctx, i, 0);
          while(duk_next(ctx, enum_idx, 1))
          {
            duk_insert(ctx, ins_idx++); /* property value, zset score */
            duk_insert(ctx, ins_idx++); /* property/key, zset value */
            enum_idx+=2;
          }
          duk_remove(ctx, enum_idx);
          duk_remove(ctx, oldi);//object
        }
        else
        {
          duk_enum(ctx, i, 0);
          while(duk_next(ctx, -1, 1))
          {
            i++; //new location for key
            duk_pull(ctx, -2);//do key first
            duk_insert(ctx, i);
            strcat(fmt," %s");
            i++;//new location for val
            duk_insert(ctx, i);
            IF_FMT_INSERT
            ELSE_IF_FMT_INSERT
          }
          duk_pop(ctx);//enum
          duk_remove(ctx, oldi);//object
        }
        i--;
        top=duk_get_top(ctx);
      }
/*
      else if (duk_is_boolean(ctx, i))
      {
        if(duk_get_boolean(ctx, i))
          retbuf = RETBUFFLAG;
        duk_remove(ctx, i);
        top=duk_get_top(ctx);
        i--;
      }
*/
      else
      {
        duk_safe_to_string(ctx,i);
        strcat(fmt," %s");
      }
      i++;
    }

    duk_push_string(ctx,fmt);
    free(fmt);
    duk_insert(ctx,0);
  }
  else // is format or format_async
  {
    duk_pop(ctx);//current_function
    i=0;
    while (i < top)
    {
      if (duk_is_ecmascript_function(ctx, i))
      {
        if (!gotfunc)
        {
          //get callback out of the way
          duk_push_this(ctx);
          duk_pull(ctx,i);
          if(isasync)
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("subcallback"));
          else
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("callback"));
          gotfunc=1;
          duk_pop(ctx);//this
        }
        else
          duk_remove(ctx,i);

        top=duk_get_top(ctx);
        i--;
      }//ecmascript
      i++;
    }//while
  } // if format or format_async

  /* async functions (pub/sub) will be handled using the current libevent loop */
  if(isasync)
  {
    RESPCLIENT *subrcp=NULL;

    /* we will use a separate connection to redis so that the main
       connection can still be used without any extra hassle       */
    getsubresp;

    /* send the command */
    if(rc_send_async(ctx, subrcp))
    {
      RDEVARGS *args=NULL;
      RPTHR *thr = get_current_thread();

      /* Allow subscribing to more channels */
      duk_push_this(ctx);
      if( duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("args")) )
      {
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("args"));
        args=duk_get_pointer(ctx, -1);
        duk_pop_2(ctx);
        if(strcmp(args->fname,"subscribe"))
          RP_THROW(ctx, "%s: Cannot send to redis server: redis is already blocking on the %s command", fname, args->fname);
        return 0;
      }

      if (!gotfunc)
        RP_THROW(ctx, "%s: subscribe and *_async functions require a callback", fname);

      REMALLOC(args, sizeof(RDEVARGS));
      /* args struct is populated with items we will need in the event callback */
      args->ctx = ctx;
      args->rcp = subrcp;
      args->fname = strdup(fname);
      args->flag = ASYNCFLAG | commandtype | retbuf;
      //fcntl(subrcp->socket, flags|O_NONBLOCK); //seems to work without this.

      // a new event to handle our callback
      args->e = event_new(thr->base, subrcp->socket, EV_READ|EV_PERSIST, rp_rdev_doevent, args);
      event_add(args->e, NULL);

      /* 'this' goes into a global hidden object named rd_event indexed by the subrcp pointer itself 
           so it can be retrieved in event callback using args->rcp                                   */
      if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rd_event")) )
      {
        /* rd-event doesn't exist.  create it and store a reference to it */
        duk_pop(ctx);//undefined
        duk_push_object(ctx); //new object
        duk_dup(ctx, -1); //a reference to the object
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("rd_event"));// store the reference as rd_event
      }
      duk_push_pointer(ctx, (void*)subrcp); //the key
      duk_push_this(ctx); //the value
      duk_push_pointer(ctx, args); //also add the args pointer
      duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("args"));//and put it in 'this' as 0xffargs
      duk_put_prop(ctx, -3);//store key(subrcp)/val('this') in rd_event
      duk_pop(ctx); // rd_event object

      return 0;
    }
    else
      RP_THROW(ctx, "%s: error sending command to redis server", fname);
  }
  else
  {

    response = rc_send(ctx, rcp);

    if(gotfunc)
    {
      duk_push_this(ctx);
      duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("callback"));
      //error handled in callback
      rd_push_response_cb(ctx, rcp, response, duk_normalize_index(ctx, -1), duk_normalize_index(ctx, -2), fname, commandtype|retbuf);
    }
    else
    {
      if(!response) 
      {
        //connection or other error
        duk_push_this(ctx);
        duk_push_sprintf(ctx, "%s: redis server connection error:\n%s", fname, (rcp->rppFrom->errorMsg?rcp->rppFrom->errorMsg:"Unknown Error") );
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;//return undefined
      }
      if(!rd_push_response(ctx, response, fname, commandtype|retbuf))
        return 0; //return undefined if redis sent errMsg
    }
  }
  return 1;
}

static duk_ret_t duk_rp_proxyobj_destroy(duk_context *ctx);
static void duk_rp_proxyobj_makeproxy(duk_context *ctx);

/* normally pointer is in this.  When copied in server.c, 
   the only place we can copy respclient to is targ */


#define getresp(rcp,this_idx,targ_idx) do{\
  duk_idx_t tidx = duk_normalize_index(ctx, (this_idx) );\
  if( duk_get_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("respclient")) ) {\
    checkresp;\
    duk_get_prop_string(ctx, -1, "client_p");\
    rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);\
    duk_pop(ctx);\
  }\
  duk_pop(ctx); \
  if(!rcp){\
    if( duk_get_prop_string(ctx, (targ_idx), DUK_HIDDEN_SYMBOL("respclient")) ){\
      checkresp;\
      duk_get_prop_string(ctx, -1, "client_p");\
      rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);\
      duk_pop(ctx);\
      duk_put_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("respclient"));\
    }\
    else duk_pop(ctx);\
  }\
} while (0)

#define gethname do {\
  if( duk_get_prop_string(ctx, 0, "_hname") )\
    hname=duk_get_string(ctx, -1);\
  duk_pop(ctx);\
  if(!hname){\
    if( duk_get_prop_string(ctx, -1, "_hname") ){\
      hname=duk_get_string(ctx, -1);\
      duk_put_prop_string(ctx, 0, "_hname");\
    }\
    else RP_THROW(ctx, "proxyObj: internal error");\
  }\
} while(0);

  /* this does not get carried over and for some reason cannot be
     added to properties of the proxy object at the bottom of duk_rp_proxyobj()
     constructor function.
  */
#define copytotarg do{\
  if(!duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("respclient"))\
    ||\
    !duk_has_prop_string(ctx, 0, "_destroy") )\
  {\
    duk_push_c_function(ctx, duk_rp_proxyobj_destroy, 0);\
    duk_put_prop_string(ctx, 0, "_destroy");\
\
    duk_push_string(ctx, hname);\
    duk_put_prop_string(ctx, 0, "_hname");\
\
    if ( duk_get_prop_string(ctx, -1,  DUK_HIDDEN_SYMBOL("respclient")) )\
    {\
      if(duk_is_object(ctx, -1) )\
        duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("respclient"));\
      else\
        duk_pop(ctx);\
    } else duk_pop(ctx);\
\
    duk_rp_proxyobj_makeproxy(ctx);\
    duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("proxy_obj"));\
  }\
} while(0)

/************
 proxy get
************* */

static duk_ret_t duk_rp_proxyobj_get(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *key, *hname=NULL;
  duk_size_t sz;
  int ret=0;

  key=duk_to_string(ctx,1);
  
  /* keys starting with '_' are local only */
  if(*key == '_'  || *key == '\xff')
  {
    duk_get_prop_string(ctx, 0, key);
    return 1;
  }

  /* get the client & hname */
  duk_push_this(ctx);
  getresp(rcp,-1,0);
  gethname;
  copytotarg;

  if( rcp == NULL )
    RP_THROW(ctx, "error: rampart-redis.proxyObj(): container object has been destroyed");    
  
  duk_pop_3(ctx);// target, key, this

  duk_push_sprintf(ctx, "HGET %s %s", hname, key);

  response = rc_send(ctx, rcp);
  ret = rd_push_response(ctx, response, "proxyObj", 1);
  if(!ret)
  {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "errMsg");
    RP_THROW(ctx, "Redis Proxy Object Error: %s\n", duk_to_string(ctx, -1));
  }
//  duk_get_prop_index(ctx, -1, 0);
  if(duk_is_null(ctx, -1) || duk_is_undefined(ctx, -1) )
  {
    duk_push_undefined(ctx);
    return 1;
  }
  duk_to_buffer(ctx, -1, &sz);
  duk_cbor_decode(ctx, -1, 0);
//printstack(ctx);
  return 1;  
}

/************
 proxy set
************* */

static duk_ret_t duk_rp_proxyobj_set(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  int ret=0;
  const char *key, *hname=NULL;
  /* get the client & hname */
  duk_push_this(ctx);

  getresp(rcp,-1,0);
  gethname;
  copytotarg;

  key=duk_to_string(ctx,1);

  /* keys starting with '_' are local only */
  if(*key == '_'  || *key == '\xff')
  {
    duk_pull(ctx,2);
    duk_put_prop_string(ctx, 3, key);
    return 0;
  }
  
  if( rcp == NULL ) return 0;
//    RP_THROW(ctx, "error: rampart-redis.proxyObj(): container object has been destroyed");    
  
  /* add local copy as null */
  duk_push_null(ctx);
  duk_put_prop_string(ctx, -2, key);

  duk_pop_2(ctx);// recv, this
  duk_remove(ctx, 0);// target;
  duk_remove(ctx, 0);// key
  
  // now stack[0]==val;
//printstack(ctx);
  duk_cbor_encode(ctx, 0, 0);

  duk_push_sprintf(ctx, "HSET %s %s %%b", hname, key);
  duk_pull(ctx, 0);
  duk_get_prop_string(ctx, -1, "byteLength");
  response = rc_send(ctx, rcp);
  ret = rd_push_response(ctx, response, "proxyObj", 1);
  if(!ret)
  {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "errMsg");
    RP_THROW(ctx, "Redis Proxy Object Error: %s\n", duk_to_string(ctx, -1));
  }
  duk_get_prop_index(ctx, -1, 0);

  return 0;
}
/************
 proxy del
************* */

static duk_ret_t duk_rp_proxyobj_del(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *key, *hname=NULL;
  int ret=0;

  /* get the client & hname */
  duk_push_this(ctx);
  getresp(rcp,-1,0);
  gethname;
  copytotarg;

  duk_pop(ctx); // pointer;

  key=duk_to_string(ctx,1);
  duk_del_prop_string(ctx, 0, key);

  /* keys starting with '_' are local only */
  if(*key == '_' )
    return 0;

  if( rcp == NULL )
    RP_THROW(ctx, "error: rampart-redis.proxyObj(): container object has been destroyed");    
  
  duk_pop_2(ctx);// targ, key, this

  duk_push_sprintf(ctx, "HDEL %s %s", hname, key);
  response = rc_send(ctx, rcp);
  ret = rd_push_response(ctx, response, "proxyObj", 1);
  if(!ret)
  {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "errMsg");
    RP_THROW(ctx, "Redis Proxy Object Error: %s\n", duk_to_string(ctx, -1));
  }
  duk_get_prop_index(ctx, -1, 0);
  return 0;
}

/************
 destroy proxy
************* */
static duk_ret_t duk_rp_proxyobj_destroy(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  char *hname=NULL;
  int ret=0;

  /* get the client & hname */
  duk_push_this(ctx);
  if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) )
  {
    duk_get_prop_string(ctx, -1, "client_p");
    rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
  }
  duk_pop(ctx);

  duk_get_prop_string(ctx, -1, "_hname");
  hname=strdup( duk_get_string(ctx, -1) );
  duk_pop(ctx);
  
  duk_enum(ctx, -1, DUK_ENUM_NO_PROXY_BEHAVIOR|DUK_ENUM_INCLUDE_HIDDEN);
  while (duk_next(ctx, -1, 0)) 
  {
    duk_del_prop_string(ctx, -3, duk_get_string(ctx, -1));
    duk_pop(ctx);
  }
  duk_pop(ctx);//enum

  if( rcp == NULL )
  {
    free(hname);
    RP_THROW(ctx, "error: rampart-redis.proxyObj(): container object has been destroyed");    
  }  
  duk_pop(ctx);// this

  duk_push_sprintf(ctx, "DEL %s", hname);
  response = rc_send(ctx, rcp);
  ret = rd_push_response(ctx, response, "proxyObj", 1);
  if(!ret)
  {
    free(hname);
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "errMsg");
    RP_THROW(ctx, "Redis Proxy Object Error: %s\n", duk_to_string(ctx, -1));
  }
//  duk_get_prop_index(ctx, -1, 0);
  free(hname);
  return 0;
}

/************
 proxy ownKeys
************* */
static duk_ret_t duk_rp_proxyobj_ownkeys(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *hname=NULL;
  int ret=0;

  /* get the client & hname */
  duk_push_this(ctx);
/*
  printf("------------THIS----------\n");
  printenum(ctx,-1);
  printf("------------TARG----------\n");
  printenum(ctx,0);
*/
  getresp(rcp,-1,0);
  gethname;
  copytotarg;

  duk_pull(ctx,0);
  duk_put_prop_string(ctx, -2, "_targ");//get target out of way for now
  duk_pop(ctx);
  
  if( rcp == NULL )
    RP_THROW(ctx, "error: rampart-redis.proxyObj(): container object has been destroyed");    
  
  duk_push_sprintf(ctx, "HKEYS %s", hname);
  response = rc_send(ctx, rcp);
  ret = rd_push_response(ctx, response, "proxyObj", 1);
  if(!ret)
  {
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "errMsg");
    RP_THROW(ctx, "Redis Proxy Object Error: %s\n", duk_to_string(ctx, -1));
  }

  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, "_targ");
  duk_insert(ctx,0); //put target back at 0
  duk_del_prop_string(ctx, -1, "_targ");
  duk_pop(ctx);//this

  duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
  while (duk_next(ctx, -1, 1))
  {
    // every value in array of keys must be a string, but all-numeric strings are
    // automatically converted to numbers when coming in from redis
    if (!duk_is_string(ctx, -1))
    {
        // [ ..., array, enum, key, val ]
        duk_to_string(ctx, -1);
        // [ ..., array, enum, key, strval ]
        duk_dup(ctx, -2);
        duk_dup(ctx, -2);
        // [ ..., array, enum, key, strval, key, strval ]
        duk_put_prop(ctx, -6); 
    }
    // [ ..., array, enum, key, strval ]
    duk_push_null(ctx);
    // [ ..., array, enum, key, strval, null ]
    duk_put_prop_string(ctx, 0, duk_get_string(ctx, -2) );
    duk_pop_2(ctx);
    // [ ..., array, enum ]
  }
  duk_pop(ctx);
  // [ ..., array ]

  return 1;
}

/*******************
 make the proxy obj
******************** */

static void duk_rp_proxyobj_makeproxy(duk_context *ctx)
{
  duk_push_object(ctx); //handler
  duk_push_c_function(ctx, duk_rp_proxyobj_ownkeys, 1);
  duk_put_prop_string(ctx, -2, "ownKeys");
  duk_push_c_function(ctx, duk_rp_proxyobj_get, 2);
  duk_put_prop_string(ctx, -2, "get");
  duk_push_c_function(ctx, duk_rp_proxyobj_del, 2);
  duk_put_prop_string(ctx, -2, "deleteProperty");
  duk_push_c_function(ctx, duk_rp_proxyobj_set, 4);
  duk_put_prop_string(ctx, -2, "set");
}

/*******************
 proxyObj constructor
******************** */

static duk_ret_t duk_rp_proxyobj(duk_context *ctx)
{
  if (!duk_is_constructor_call(ctx))
    return DUK_RET_TYPE_ERROR;
  
  duk_push_this(ctx);
  (void)REQUIRE_STRING(ctx, 0, "proxyObj(name) requires a String (name)");

  /* copy over the client pointer */
  duk_push_current_function(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient"));
  duk_remove(ctx, -2);
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("respclient"));
  duk_pull(ctx, 0);
  duk_put_prop_string(ctx, -2, "_hname");
  duk_rp_proxyobj_makeproxy(ctx);
  
  duk_dup(ctx, -1);
  duk_put_prop_string( ctx, 0, DUK_HIDDEN_SYMBOL("proxy_obj")); 

  duk_push_proxy(ctx, 0);  /* new Proxy(this, handler) */
  
  return 1;
}


/* **************************************************
   new redis.init([host,] [port,] [timeout]) constructor
     or
   new redis.init([port,] [host,] [timeout]) constructor
     or
   new redis.init( { [port:xxx,] [ip: yyy,] [timeout:zzz]})
   var redis = require("rampart-redis");
   var rcl=new redis.init(6379, "127.0.0.1");

   ************************************************** */
static duk_ret_t duk_rp_cc_constructor(duk_context *ctx)
{
  int port = 6379, timeout=-1;
  const char *ip = "127.0.0.1";
  RESPCLIENT *rcp = NULL;

  if (!duk_is_constructor_call(ctx))
  {
    return DUK_RET_TYPE_ERROR;
  }

  if (duk_is_object(ctx,0))
  {
    if(duk_get_prop_string(ctx, 0, "port"))
      port = REQUIRE_INT(ctx, -1, "redis.init - property 'port' must be an Integer (port of redis server)");
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "ip"))
      ip = REQUIRE_STRING(ctx, -1, "redis.init - property 'ip' must be a String (ip address of redis server)");
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "timeout"))
      timeout = (int)(1000.0 * REQUIRE_NUMBER(ctx, -1, "redis.init - property 'timeout' must be a Number (timeout in seconds)") );
    duk_pop(ctx);
  }
  else
  {
    if(duk_is_number(ctx, 0))
    {
      port = duk_get_int(ctx, 0);
      ip = duk_get_string_default(ctx, 1, "127.0.0.1");
    }
    else if(duk_is_string(ctx, 0))
    {
      ip = duk_get_string(ctx, 0);
      port = (int)duk_get_int_default(ctx, 1,  6379);
    }

    if(!duk_is_undefined(ctx, -2))
      timeout=(int) ( 1000.0 * REQUIRE_NUMBER(ctx, -2, "redis.init - third parameter must be a number (timeout in seconds)") );
  }

  rcp = connectRespServer((char *)ip, port);

  if (!rcp)
    RP_THROW(ctx, "redis.init - Failed to connect to %s:%d", ip, port);

  rcp->timeout = timeout; // wait forever

  duk_push_this(ctx);

  duk_push_c_function(ctx, _close_, 1);
  duk_set_finalizer(ctx, -2);

  duk_push_c_function(ctx, duk_rp_proxyobj, 1);

  duk_push_object(ctx); //put it in an object so there is only copy with multiple references
  duk_push_pointer(ctx, (void *)rcp);
  duk_put_prop_string(ctx, -2, "client_p");
  duk_push_string(ctx, ip);
  duk_put_prop_string(ctx, -2, "ip");
  duk_push_int(ctx,port);
  duk_put_prop_string(ctx, -2, "port");
  duk_push_int(ctx,timeout);
  duk_put_prop_string(ctx, -2, "timeout");
  duk_dup(ctx, -1);
  duk_put_prop_string(ctx, -4, DUK_HIDDEN_SYMBOL("respclient")); /* copy on this */
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("respclient")); /* copy on duk_rp_proxyobj */

  duk_put_prop_string(ctx, -2, "proxyObj"); // c_function into this

  return 0;
}

#define PUSHCMD(name,command,type) do {\
  duk_push_c_function(ctx, duk_rp_cc_docmd, DUK_VARARGS);\
  duk_push_string(ctx,name);\
  duk_put_prop_string(ctx, -2, "fname");\
  duk_push_string(ctx,command);\
  duk_put_prop_string(ctx, -2, "command");\
  duk_push_int(ctx,type);\
  duk_put_prop_string(ctx, -2, "type");\
  duk_put_prop_string(ctx, -2, name);\
} while (0);  

char *commandnames[] = {
/* 000 */"acl_load", "acl_save", "acl_list", "acl_users", "acl_getuser", "acl_setuser", "acl_deluser", "acl_cat", "acl_genpass", "acl_whoami",
/* 010 */"acl_log", "acl_help", "append", "auth", "bgrewriteaof", "bgsave", "bitcount", "bitfield", "bitop", "bitpos",
/* 020 */"blpop", "brpop", "brpoplpush", "bzpopmin", "bzpopmax", "client_caching", "client_id", "client_kill", "client_list", "client_getname",
/* 030 */"client_getredir", "client_pause", "client_reply", "client_setname", "client_tracking", "client_unblock", "cluster_addslots", "cluster_bumpepoch", "cluster_count-failure-reports", "cluster_countkeysinslot",
/* 040 */"cluster_delslots", "cluster_failover", "cluster_flushslots", "cluster_forget", "cluster_getkeysinslot", "cluster_info", "cluster_keyslot", "cluster_meet", "cluster_myid", "cluster_nodes",
/* 050 */"cluster_replicate", "cluster_reset", "cluster_saveconfig", "cluster_set-config-epoch", "cluster_setslot", "cluster_slaves", "cluster_replicas", "cluster_slots", "command", "command_count",
/* 060 */"command_getkeys", "command_info", "config_get", "config_rewrite", "config_set", "config_resetstat", "dbsize", "debug_object", "debug_segfault", "decr",
/* 070 */"decrby", "del", "discard", "dump", "echo", "eval", "evalsha", "exec", "exists", "expire",
/* 080 */"expireat", "flushall", "flushdb", "geoadd", "geohash", "geopos", "geodist", "georadius", "georadiusbymember", "get",
/* 090 */"getbit", "getrange", "getset", "hdel", "hello", "hexists", "hget", "hgetall", "hincrby", "hincrbyfloat",
/* 100 */"hkeys", "hlen", "hmget", "hmset", "hset", "hsetnx", "hstrlen", "hvals", "incr", "incrby",
/* 110 */"incrbyfloat", "info", "lolwut", "keys", "lastsave", "lindex", "linsert", "llen", "lpop", "lpos",
/* 120 */"lpush", "lpushx", "lrange", "lrem", "lset", "ltrim", "memory_doctor", "memory_help", "memory_malloc-stats", "memory_purge",
/* 130 */"memory_stats", "memory_usage", "mget", "migrate", "module_list", "module_load", "module_unload", "monitor", "move", "mset",
/* 140 */"msetnx", "multi", "object", "persist", "pexpire", "pexpireat", "pfadd", "pfcount", "pfmerge", "ping",
/* 150 */"psetex", "psubscribe", "pubsub", "pttl", "blpop_async", "publish"   , "punsubscribe", "quit", "randomkey", "readonly",
/* 160 */"readwrite", "rename", "renamenx", "restore", "role", "rpop", "rpoplpush", "rpush", "rpushx", "sadd",
/* 170 */"save", "scard", "script_debug", "script_exists", "script_flush", "script_kill", "script_load", "sdiff", "sdiffstore", "select",
/* 180 */"set", "setbit", "setex", "setnx", "setrange", "shutdown", "sinter", "sinterstore", "sismember", "slaveof",
/* 190 */"replicaof", "slowlog", "smembers", "smove", "sort", "spop", "srandmember", "srem", "stralgo", "strlen",
/* 200 */"subscribe", "sunion", "sunionstore", "swapdb", "sync", "psync", "time", "touch", "ttl", "type",
/* 210 */"unsubscribe", "unlink", "unwatch", "wait", "watch", "zadd", "zcard", "zcount", "zincrby", "zinterstore",
/* 220 */"zlexcount", "zpopmax", "zpopmin", "zrange", "zrangebylex", "zrevrangebylex", "zrangebyscore", "zrank", "zrem", "zremrangebylex",
/* 230 */"zremrangebyrank", "zremrangebyscore", "zrevrange", "zrevrangebyscore", "zrevrank", "zscore", "zunionstore", "scan", "sscan", "hscan",
/* 240 */"zscan", "xinfo", "xadd", "xtrim", "xdel", "xrange", "xrevrange", "xlen", "xread", "xread_block_async",
/* 250 */"xgroup", "xreadgroup", "xack", "xclaim", "xpending", "latency_doctor", "latency_graph", "latency_history", "latency_latest", "latency_reset",
/* 260 */"latency_help", "reset", "getdel", "getex", "hrandfield", "lmove", "smismember", "zdiff", "zdiffstore", "zinter",
/* 270 */"zunion", "zrangestore", "zmscore", "zrandmember", "xread_auto_async", "brpop_async", "blmove", "blmove_async", "format", "format_async",
/* 280 */"bzpopmin_async", "bzpopmax_async"
};
char *commandcommands[] = {
/* 000 */"ACL LOAD", "ACL SAVE", "ACL LIST", "ACL USERS", "ACL GETUSER", "ACL SETUSER", "ACL DELUSER", "ACL CAT", "ACL GENPASS", "ACL WHOAMI",
/* 010 */"ACL LOG", "ACL HELP", "APPEND", "AUTH", "BGREWRITEAOF", "BGSAVE", "BITCOUNT", "BITFIELD", "BITOP", "BITPOS",
/* 020 */"BLPOP", "BRPOP", "BRPOPLPUSH", "BZPOPMIN", "BZPOPMAX", "CLIENT CACHING", "CLIENT ID", "CLIENT KILL", "CLIENT LIST", "CLIENT GETNAME",
/* 030 */"CLIENT GETREDIR", "CLIENT PAUSE", "CLIENT REPLY", "CLIENT SETNAME", "CLIENT TRACKING", "CLIENT UNBLOCK", "CLUSTER ADDSLOTS", "CLUSTER BUMPEPOCH", "CLUSTER COUNT-FAILURE-REPORTS", "CLUSTER COUNTKEYSINSLOT",
/* 040 */"CLUSTER DELSLOTS", "CLUSTER FAILOVER", "CLUSTER FLUSHSLOTS", "CLUSTER FORGET", "CLUSTER GETKEYSINSLOT", "CLUSTER INFO", "CLUSTER KEYSLOT", "CLUSTER MEET", "CLUSTER MYID", "CLUSTER NODES",
/* 050 */"CLUSTER REPLICATE", "CLUSTER RESET", "CLUSTER SAVECONFIG", "CLUSTER SET-CONFIG-EPOCH", "CLUSTER SETSLOT", "CLUSTER SLAVES", "CLUSTER REPLICAS", "CLUSTER SLOTS", "COMMAND", "COMMAND COUNT",
/* 060 */"COMMAND GETKEYS", "COMMAND INFO", "CONFIG GET", "CONFIG REWRITE", "CONFIG SET", "CONFIG RESETSTAT", "DBSIZE", "DEBUG OBJECT", "DEBUG SEGFAULT", "DECR",
/* 070 */"DECRBY", "DEL", "DISCARD", "DUMP", "ECHO", "EVAL", "EVALSHA", "EXEC", "EXISTS", "EXPIRE",
/* 080 */"EXPIREAT", "FLUSHALL", "FLUSHDB", "GEOADD", "GEOHASH", "GEOPOS", "GEODIST", "GEORADIUS", "GEORADIUSBYMEMBER", "GET",
/* 090 */"GETBIT", "GETRANGE", "GETSET", "HDEL", "HELLO", "HEXISTS", "HGET", "HGETALL", "HINCRBY", "HINCRBYFLOAT",
/* 100 */"HKEYS", "HLEN", "HMGET", "HMSET", "HSET", "HSETNX", "HSTRLEN", "HVALS", "INCR", "INCRBY",
/* 110 */"INCRBYFLOAT", "INFO", "LOLWUT", "KEYS", "LASTSAVE", "LINDEX", "LINSERT", "LLEN", "LPOP", "LPOS",
/* 120 */"LPUSH", "LPUSHX", "LRANGE", "LREM", "LSET", "LTRIM", "MEMORY DOCTOR", "MEMORY HELP", "MEMORY MALLOC-STATS", "MEMORY PURGE",
/* 130 */"MEMORY STATS", "MEMORY USAGE", "MGET", "MIGRATE", "MODULE LIST", "MODULE LOAD", "MODULE UNLOAD", "MONITOR", "MOVE", "MSET",
/* 140 */"MSETNX", "MULTI", "OBJECT", "PERSIST", "PEXPIRE", "PEXPIREAT", "PFADD", "PFCOUNT", "PFMERGE", "PING",
/* 150 */"PSETEX", "PSUBSCRIBE", "PUBSUB", "PTTL", "BLPOP" /* async,*/, "PUBLISH", "PUNSUBSCRIBE", "QUIT", "RANDOMKEY", "READONLY",
/* 160 */"READWRITE", "RENAME", "RENAMENX", "RESTORE", "ROLE", "RPOP", "RPOPLPUSH", "RPUSH", "RPUSHX", "SADD",
/* 170 */"SAVE", "SCARD", "SCRIPT DEBUG", "SCRIPT EXISTS", "SCRIPT FLUSH", "SCRIPT KILL", "SCRIPT LOAD", "SDIFF", "SDIFFSTORE", "SELECT",
/* 180 */"SET", "SETBIT", "SETEX", "SETNX", "SETRANGE", "SHUTDOWN", "SINTER", "SINTERSTORE", "SISMEMBER", "SLAVEOF",
/* 190 */"REPLICAOF", "SLOWLOG", "SMEMBERS", "SMOVE", "SORT", "SPOP", "SRANDMEMBER", "SREM", "STRALGO", "STRLEN",
/* 200 */"SUBSCRIBE", "SUNION", "SUNIONSTORE", "SWAPDB", "SYNC", "PSYNC", "TIME", "TOUCH", "TTL", "TYPE",
/* 210 */"UNSUBSCRIBE", "UNLINK", "UNWATCH", "WAIT", "WATCH", "ZADD", "ZCARD", "ZCOUNT", "ZINCRBY", "ZINTERSTORE",
/* 220 */"ZLEXCOUNT", "ZPOPMAX", "ZPOPMIN", "ZRANGE", "ZRANGEBYLEX", "ZREVRANGEBYLEX", "ZRANGEBYSCORE", "ZRANK", "ZREM", "ZREMRANGEBYLEX",
/* 230 */"ZREMRANGEBYRANK", "ZREMRANGEBYSCORE", "ZREVRANGE", "ZREVRANGEBYSCORE", "ZREVRANK", "ZSCORE", "ZUNIONSTORE", "SCAN", "SSCAN", "HSCAN",
/* 240 */"ZSCAN", "XINFO", "XADD", "XTRIM", "XDEL", "XRANGE", "XREVRANGE", "XLEN", "XREAD", "XREAD BLOCK",
/* 250 */"XGROUP", "XREADGROUP", "XACK", "XCLAIM", "XPENDING", "LATENCY DOCTOR", "LATENCY GRAPH", "LATENCY HISTORY", "LATENCY LATEST", "LATENCY RESET",
/* 260 */"LATENCY HELP", "RESET", "GETDEL", "GETEX", "HRANDFIELD", "LMOVE", "SMISMEMBER", "ZDIFF", "ZDIFFSTORE", "ZINTER",
/* 270 */"ZUNION", "ZRANGESTORE", "ZMSCORE", "ZRANDMEMBER", "XREAD BLOCK 0 STREAMS", "BRPOP" /* async */, "BLMOVE", "BLMOVE" /* async */, "" /* format */, "" /* format_async*/,
/* 280 */"BZPOPMIN", "BZPOPMAX"
};

// 0 - expects "val" - single response only
// 1 - expects [val, val, val] - make an array, or return one value per callback
// 2 - expects [ [val, val, ...], [val, val, ...], ...], - grouping of arrays or one array per callback. 
// 3 - expects [key, val, key, val, ...] - make object
// 4 - expects [ [key,val], [key,val], ...] - make object
// 5 - like 0 but expects 0/1 and returns boolean
// 6 - scans with cursors - objects
// 7 - scans with cursors - arrays
// 8 - array of bools
// 9 - same as 1, except if last option is WITHSCORES make objects with keys score & value
//10 - only for zscan, cross between 7 and 9
//11 - only for xread.
//12 - like 3, but returns {key:"key_name", value:"some value"}
//13 - like 9 WITHSCORES and includes key name. also only one object, so no array. -> {key:xxx, value:yyy, score:zzz}

int commandtypes[] = {
/* 000 */0,  0,  1,  1,  1,  0,  0,  1,  0,  0,
/* 010 */0,  1,  0,  0,  0,  0,  0,  1,  0,  0,
/* 020 */12, 12, 0,  13, 13, 0,  0,  0,  0,  0,
/* 030 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 040 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 050 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 060 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 070 */0,  0,  0,  0,  0,  0,  0,  0,  5,  0,
/* 080 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 090 */0,  0,  0,  0,  1,  5,  0,  3,  0,  0,
/* 100 */1,  0,  1,  0,  0,  5,  0,  1,  0,  0,
/* 110 */0,  1,  0,  1,  0,  0,  0,  0,  1,  0,
/* 120 */0,  0,  1,  0,  0,  0,  0,  0,  0,  0,
/* 130 */0,  0,  1,  0,  0,  0,  0,  0,  0,  0,
/* 140 */5,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 150 */0,  0,  0,  0,  12, 0,  0,  0,  0,  0,
/* 160 */0,  0,  0,  0,  0,  1,  0,  0,  0,  0,
/* 170 */0,  0,  0,  0,  0,  0,  0,  1,  0,  0,
/* 180 */0,  0,  0,  5,  0,  0,  1,  0,  5,  0,
/* 190 */0,  0,  1,  0,  1,  1,  1,  0,  3,  0,
/* 200 */2,  1,  0,  0,  0,  0,  1,  0,  0,  0,
/* 210 */0,  0,  0,  0,  0,  0,  0,  0,  0,  9,
/* 220 */0,  9,  9,  9,  1,  9,  9,  0,  0,  0,
/* 230 */0,  0,  9,  9,  0,  0,  9,  7,  7,  6,
/* 240 */10, 3,  0,  0,  0,  4,  4,  0,  11, 11,
/* 250 */0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
/* 260 */0,  0,  0,  0,  1,  0,  8,  9,  9,  9,
/* 270 */9,  0,  1,  9,  11, 12, 0,  0,  1,  1,
/* 280 */13, 13
};

/* **************************************************
   Initialize redis module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
  int i=0;
  size_t sz = sizeof(commandnames)/sizeof(char*);

  duk_push_object(ctx);

  /* create_client for node.js-like redis interface */

  duk_push_c_function(ctx, duk_rp_cc_constructor, 3);

  duk_push_object(ctx);
  for (i=0; i<sz; i++)
    PUSHCMD(commandnames[i], commandcommands[i], commandtypes[i]);

  duk_push_c_function(ctx, duk_rp_rd_close, 0);
  duk_put_prop_string(ctx, -2, "close");
  duk_push_c_function(ctx, duk_rp_rd_close_async, 0);
  duk_put_prop_string(ctx, -2, "close_async");

  duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ ret-->[prototype-->[exe=fn_exe,...]] ] */

  duk_put_prop_string(ctx, -2, "init"); /* -> stack: [ ret ] */

  return 1;
}
