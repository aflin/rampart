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
        duk_push_lstring(ctx, (const char *)item->loc, (duk_size_t) item->length);
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
/* send command for init()/exec() */
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
   var Ramis = require("rpramis");
   var ra=new Ramis.init("127.0.0.1",6379);

   ************************************************** */
duk_ret_t duk_rp_ra_constructor(duk_context *ctx)
{
  const char *ip = duk_get_string_default(ctx, 0, "127.0.0.1");
  int port = (int)duk_get_int_default(ctx, 1, 6379);
  RESPCLIENT *respClient = NULL;

  if (!duk_is_constructor_call(ctx))
  {
    return DUK_RET_TYPE_ERROR;
  }

  respClient = connectRespServer((char *)ip, port);
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

/*************** FUNCTION FOR createClient ****************************** */
#define checkresp do {\
  /*printenum(ctx,-1);*/\
  if(duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thread_copied") ) ){\
    int port; const char *ip;\
    duk_get_prop_string(ctx, -1, "ip");\
    ip=duk_get_string(ctx, -1);\
    duk_pop(ctx);\
    duk_get_prop_string(ctx, -1, "port");\
    port=duk_get_int(ctx, -1);\
    duk_pop(ctx);\
    duk_del_prop_string( ctx, -1, DUK_HIDDEN_SYMBOL("thread_copied") );\
    rcp= connectRespServer((char *)ip, port);\
    if(!rcp) RP_THROW(ctx,"could not reconnect to resp server");\
    duk_push_pointer(ctx, (void *) rcp);\
    duk_put_prop_string(ctx, -2, "client_p");\
  }\
} while(0)

duk_ret_t duk_rp_cc_docmd(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  char fmt[1024]={0};
  duk_idx_t top=0, i=0;
  int gotfunc=0;

  /* get the client */
  duk_push_this(ctx);
  if( duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient")) ) {
    checkresp;
    duk_get_prop_string(ctx, -1, "client_p");
    rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);
  }
  duk_pop_2(ctx);

  if( rcp == NULL )
    RP_THROW(ctx, "error: ramis - client not connected");

  duk_push_current_function(ctx);
  duk_get_prop_string(ctx, -1, "command");
  strcat(fmt, duk_safe_to_string(ctx,-1) );
  duk_pop_2(ctx);
  
  top=duk_get_top(ctx);
  if(top>300) // illegal anyway, but here it will go past fmt buffer
    RP_THROW(ctx,"Too many options for resp command");

  while (i < top)
  {
    if( duk_is_object(ctx, i) && duk_has_prop_string(ctx, i, "byteLength"))
    {
      duk_get_prop_string(ctx, i, "byteLength");
      i++;
      duk_insert(ctx, i);
      strcat(fmt," %b");
    }
    else if (duk_is_string(ctx, i))
      strcat(fmt," %s");
    else if (duk_is_number(ctx, i))
      strcat(fmt," %f");
    else if (duk_is_ecmascript_function(ctx, i))
    {
      if (!gotfunc)
      {
        //get callback out of the way
        duk_push_this(ctx);
        duk_pull(ctx,i);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("callback"));
        gotfunc=1;
        duk_pop(ctx);//this
      }
      else
        duk_remove(ctx,i);

      i--;
    }
    else
    {
      duk_safe_to_string(ctx,i);
      strcat(fmt," %s");
    }
    i++;
  }

  top=duk_get_top(ctx);

  duk_push_string(ctx,fmt);
  duk_insert(ctx,0);  
  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);

  return 1;
}


duk_ret_t duk_rp_ramvar_destroy(duk_context *ctx);
void duk_rp_ramvar_makeproxy(duk_context *ctx);

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
    else RP_THROW(ctx, "ramvar: internal error");\
  }\
} while(0);

  /* this does not get carried over and for some reason cannot be
     added to properties of the proxy object at the bottom of duk_rp_ramvar()
     constructor function.
  */
#define copytotarg do{\
  if(!duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("respclient"))\
    ||\
    !duk_has_prop_string(ctx, 0, "_destroy") )\
  {\
    duk_push_c_function(ctx, duk_rp_ramvar_destroy, 0);\
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
    duk_rp_ramvar_makeproxy(ctx);\
    duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("proxy_obj"));\
  }\
} while(0)

/************
 proxy get
************* */

duk_ret_t duk_rp_ramvar_get(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *key, *hname=NULL;
  duk_size_t sz;

  key=duk_get_string(ctx,1);
  
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
    RP_THROW(ctx, "error: ramis.ramvar(): container object has been destroyed");    
  
  duk_pop_3(ctx);// target, key, this

  duk_push_sprintf(ctx, "HGET %s %s", hname, key);

  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);
  duk_get_prop_index(ctx, -1, 0);
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

duk_ret_t duk_rp_ramvar_set(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *key, *hname=NULL;

  /* get the client & hname */
  duk_push_this(ctx);
  getresp(rcp,-1,0);
  gethname;
  copytotarg;
  /*
  printf("------------THIS----------\n");
  printenum(ctx,-1);
  printf("------------TARG----------\n");
  printenum(ctx,0);
  */

  key=duk_get_string(ctx,1);

  /* keys starting with '_' are local only */
  if(*key == '_'  || *key == '\xff')
  {
    duk_pull(ctx,2);
    duk_put_prop_string(ctx, 3, key);
    return 0;
  }
  
  if( rcp == NULL ) return 0;
//    RP_THROW(ctx, "error: ramis.ramvar(): container object has been destroyed");    
  
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
  ra_push_response(ctx, response);
  duk_get_prop_index(ctx, -1, 0);
  return 0;
}
/************
 proxy del
************* */

duk_ret_t duk_rp_ramvar_del(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *key, *hname=NULL;

  /* get the client & hname */
  duk_push_this(ctx);
  getresp(rcp,-1,0);
  gethname;
  copytotarg;
  /*
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient"));
  rcp = (RESPCLIENT *)duk_get_pointer(ctx, -1);
  */
  duk_pop(ctx); // pointer;

  key=duk_get_string(ctx,1);
  duk_del_prop_string(ctx, 0, key);

  /* keys starting with '_' are local only */
  if(*key == '_' )
    return 0;

  if( rcp == NULL )
    RP_THROW(ctx, "error: ramis.ramvar(): container object has been destroyed");    
  
  duk_pop_2(ctx);// targ, key, this

  duk_push_sprintf(ctx, "HDEL %s %s", hname, key);
  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);
  duk_get_prop_index(ctx, -1, 0);
  return 0;
}

/************
 destroy proxy
************* */
duk_ret_t duk_rp_ramvar_destroy(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  char *hname=NULL;

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
    printf("deleting %s from this\n", duk_get_string(ctx, -1));
    duk_del_prop_string(ctx, -3, duk_get_string(ctx, -1));
    duk_pop(ctx);
  }
  duk_pop(ctx);//enum

  if( rcp == NULL )
    RP_THROW(ctx, "error: ramis.ramvar(): container object has been destroyed");    
  
  duk_pop(ctx);// this

  duk_push_sprintf(ctx, "DEL %s", hname);
  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);
  duk_get_prop_index(ctx, -1, 0);
  free(hname);
  return 0;
}

/************
 proxy ownKeys
************* */
duk_ret_t duk_rp_ramvar_ownkeys(duk_context *ctx)
{
  RESPCLIENT *rcp=NULL;
  RESPROTO *response;
  const char *hname=NULL;

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
    RP_THROW(ctx, "error: ramis.ramvar(): container object has been destroyed");    
  
  duk_push_sprintf(ctx, "HKEYS %s", hname);
  response = rc_send(ctx, rcp);
  ra_push_response(ctx, response);
  duk_get_prop_index(ctx, -1, 0);

  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, "_targ");
  duk_insert(ctx,0); //put target back at 0
  duk_del_prop_string(ctx, -1, "_targ");
  duk_pop(ctx);//this

  duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
  while (duk_next(ctx, -1, 1))
  {
    duk_push_null(ctx);
    duk_put_prop_string(ctx, 0, duk_get_string(ctx, -2) );
    duk_pop_2(ctx);
  }
  duk_pop(ctx);

  duk_push_string(ctx, "_destroy");
  duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2) );
  duk_push_string(ctx, DUK_HIDDEN_SYMBOL("respclient"));
  duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2) );
  duk_push_string(ctx, DUK_HIDDEN_SYMBOL("proxy_obj"));
  duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2) );
  duk_push_string(ctx, "_hname");
  duk_put_prop_index(ctx, -2, duk_get_length(ctx, -2) );
  return 1;
}

/*******************
 make the proxy obj
******************** */

void duk_rp_ramvar_makeproxy(duk_context *ctx)
{
  duk_push_object(ctx); //handler
  duk_push_c_function(ctx, duk_rp_ramvar_ownkeys, 1);
  duk_put_prop_string(ctx, -2, "ownKeys");
  duk_push_c_function(ctx, duk_rp_ramvar_get, 2);
  duk_put_prop_string(ctx, -2, "get");
  duk_push_c_function(ctx, duk_rp_ramvar_del, 2);
  duk_put_prop_string(ctx, -2, "deleteProperty");
  duk_push_c_function(ctx, duk_rp_ramvar_set, 4);
  duk_put_prop_string(ctx, -2, "set");
}

/*******************
 ramvar constructor
******************** */

duk_ret_t duk_rp_ramvar(duk_context *ctx)
{
  if (!duk_is_constructor_call(ctx))
    return DUK_RET_TYPE_ERROR;
  
  duk_push_this(ctx);
  (void)REQUIRE_STRING(ctx, 0, "createClient.ramvar(name) requires an argument (name)");

  /* copy over the client pointer */
  duk_push_current_function(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("respclient"));
  duk_remove(ctx, -2);
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("respclient"));
  duk_pull(ctx, 0);
  duk_put_prop_string(ctx, -2, "_hname");
  duk_rp_ramvar_makeproxy(ctx);
  
  duk_dup(ctx, -1);
  duk_put_prop_string( ctx, 0, DUK_HIDDEN_SYMBOL("proxy_obj")); 

  duk_push_proxy(ctx, 0);  /* new Proxy(this, handler) */
  
  return 1;
}


/* **************************************************
   Ramis(host,port) constructor
   var Ramis = require("rpramis");
   var ra=new Ramis.createClient(6379, "127.0.0.1");

   ************************************************** */
duk_ret_t duk_rp_cc_constructor(duk_context *ctx)
{
  int port = (int)duk_get_int_default(ctx, 0, 6379);
  const char *ip = duk_get_string_default(ctx, 1, "127.0.0.1");
  RESPCLIENT *respClient = NULL;

  if (!duk_is_constructor_call(ctx))
  {
    return DUK_RET_TYPE_ERROR;
  }

  respClient = connectRespServer((char *)ip, port);
  if (!respClient)
  {
    duk_push_sprintf(ctx, "respClient: Failed to connect to %s:%d\n", ip, port);
    (void)duk_throw(ctx);
  }

  // TODO: ask what this should be set to
  respClient->waitForever = 1;
  //  respClientWaitForever(respClient,1);
  duk_push_this(ctx);
  duk_push_c_function(ctx, duk_rp_ramvar, 1);

  duk_push_object(ctx); //put it in an object so there is only copy with multiple references
  duk_push_pointer(ctx, (void *)respClient);
  duk_put_prop_string(ctx, -2, "client_p");
  duk_push_string(ctx, ip);
  duk_put_prop_string(ctx, -2, "ip");
  duk_push_int(ctx,port);
  duk_put_prop_string(ctx, -2, "port");
  duk_dup(ctx, -1);
  duk_put_prop_string(ctx, -4, DUK_HIDDEN_SYMBOL("respclient")); /* copy on this */
  duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("respclient")); /* copy on duk_rp_ramvar */

  duk_put_prop_string(ctx, -2, "ramvar"); // c_function into this

  return 0;
}

#define PUSHCMD(name,command) do {\
  duk_push_c_function(ctx, duk_rp_cc_docmd, DUK_VARARGS);\
  duk_push_string(ctx,name);\
  duk_put_prop_string(ctx, -2, "fname");\
  duk_push_string(ctx,command);\
  duk_put_prop_string(ctx, -2, "command");\
  duk_put_prop_string(ctx, -2, name);\
} while (0);  

char *commandnames[] = {
    "acl_load", "acl_save", "acl_list", "acl_users", "acl_getuser", "acl_setuser", "acl_deluser", "acl_cat", "acl_genpass", 
    "acl_whoami", "acl_log", "acl_help", "append", "auth", "bgrewriteaof", "bgsave", "bitcount", "bitfield", "bitop", "bitpos", "blpop", 
    "brpop", "brpoplpush", "bzpopmin", "bzpopmax", "client_caching", "client_id", "client_kill", "client_list", "client_getname", "client_getredir", 
    "client_pause", "client_reply", "client_setname", "client_tracking", "client_unblock", "cluster_addslots", "cluster_bumpepoch", 
    "cluster_count-failure-reports", "cluster_countkeysinslot", "cluster_delslots", "cluster_failover", "cluster_flushslots", "cluster_forget", 
    "cluster_getkeysinslot", "cluster_info", "cluster_keyslot", "cluster_meet", "cluster_myid", "cluster_nodes", "cluster_replicate", "cluster_reset", 
    "cluster_saveconfig", "cluster_set-config-epoch", "cluster_setslot", "cluster_slaves", "cluster_replicas", "cluster_slots", "command", "command_count", 
    "command_getkeys", "command_info", "config_get", "config_rewrite", "config_set", "config_resetstat", "dbsize", "debug_object", "debug_segfault", "decr", 
    "decrby", "del", "discard", "dump", "echo", "eval", "evalsha", "exec", "exists", "expire", "expireat", "flushall", "flushdb", "geoadd", "geohash", "geopos", 
    "geodist", "georadius", "georadiusbymember", "get", "getbit", "getrange", "getset", "hdel", "hello", "hexists", "hget", "hgetall", "hincrby", "hincrbyfloat", 
    "hkeys", "hlen", "hmget", "hmset", "hset", "hsetnx", "hstrlen", "hvals", "incr", "incrby", "incrbyfloat", "info", "lolwut", "keys", "lastsave", "lindex", 
    "linsert", "llen", "lpop", "lpos", "lpush", "lpushx", "lrange", "lrem", "lset", "ltrim", "memory_doctor", "memory_help", "memory_malloc-stats", "memory_purge", 
    "memory_stats", "memory_usage", "mget", "migrate", "module_list", "module_load", "module_unload", "monitor", "move", "mset", "msetnx", "multi", "object", 
    "persist", "pexpire", "pexpireat", "pfadd", "pfcount", "pfmerge", "ping", "psetex", "psubscribe", "pubsub", "pttl", "publish", "punsubscribe", "quit", 
    "randomkey", "readonly", "readwrite", "rename", "renamenx", "restore", "role", "rpop", "rpoplpush", "rpush", "rpushx", "sadd", "save", "scard", "script_debug", 
    "script_exists", "script_flush", "script_kill", "script_load", "sdiff", "sdiffstore", "select", "set", "setbit", "setex", "setnx", "setrange", "shutdown", 
    "sinter", "sinterstore", "sismember", "slaveof", "replicaof", "slowlog", "smembers", "smove", "sort", "spop", "srandmember", "srem", "stralgo", "strlen", 
    "subscribe", "sunion", "sunionstore", "swapdb", "sync", "psync", "time", "touch", "ttl", "type", "unsubscribe", "unlink", "unwatch", "wait", "watch", "zadd", 
    "zcard", "zcount", "zincrby", "zinterstore", "zlexcount", "zpopmax", "zpopmin", "zrange", "zrangebylex", "zrevrangebylex", "zrangebyscore", "zrank", "zrem", 
    "zremrangebylex", "zremrangebyrank", "zremrangebyscore", "zrevrange", "zrevrangebyscore", "zrevrank", "zscore", "zunionstore", "scan", "sscan", "hscan", 
    "zscan", "xinfo", "xadd", "xtrim", "xdel", "xrange", "xrevrange", "xlen", "xread", "xgroup", "xreadgroup", "xack", "xclaim", "xpending", "latency_doctor", 
    "latency_graph", "latency_history", "latency_latest", "latency_reset", "latency_help"
};

char *commandcommands[] = {
    "ACL LOAD", "ACL SAVE", "ACL LIST", "ACL USERS", "ACL GETUSER", "ACL SETUSER", "ACL DELUSER", "ACL CAT", "ACL GENPASS", 
    "ACL WHOAMI", "ACL LOG", "ACL HELP", "APPEND", "AUTH", "BGREWRITEAOF", "BGSAVE", "BITCOUNT", "BITFIELD", "BITOP", "BITPOS", "BLPOP", 
    "BRPOP", "BRPOPLPUSH", "BZPOPMIN", "BZPOPMAX", "CLIENT CACHING", "CLIENT ID", "CLIENT KILL", "CLIENT LIST", "CLIENT GETNAME", "CLIENT GETREDIR", 
    "CLIENT PAUSE", "CLIENT REPLY", "CLIENT SETNAME", "CLIENT TRACKING", "CLIENT UNBLOCK", "CLUSTER ADDSLOTS", "CLUSTER BUMPEPOCH", 
    "CLUSTER COUNT-FAILURE-REPORTS", "CLUSTER COUNTKEYSINSLOT", "CLUSTER DELSLOTS", "CLUSTER FAILOVER", "CLUSTER FLUSHSLOTS", "CLUSTER FORGET", 
    "CLUSTER GETKEYSINSLOT", "CLUSTER INFO", "CLUSTER KEYSLOT", "CLUSTER MEET", "CLUSTER MYID", "CLUSTER NODES", "CLUSTER REPLICATE", "CLUSTER RESET", 
    "CLUSTER SAVECONFIG", "CLUSTER SET-CONFIG-EPOCH", "CLUSTER SETSLOT", "CLUSTER SLAVES", "CLUSTER REPLICAS", "CLUSTER SLOTS", "COMMAND", "COMMAND COUNT", 
    "COMMAND GETKEYS", "COMMAND INFO", "CONFIG GET", "CONFIG REWRITE", "CONFIG SET", "CONFIG RESETSTAT", "DBSIZE", "DEBUG OBJECT", "DEBUG SEGFAULT", "DECR", 
    "DECRBY", "DEL", "DISCARD", "DUMP", "ECHO", "EVAL", "EVALSHA", "EXEC", "EXISTS", "EXPIRE", "EXPIREAT", "FLUSHALL", "FLUSHDB", "GEOADD", "GEOHASH", "GEOPOS", 
    "GEODIST", "GEORADIUS", "GEORADIUSBYMEMBER", "GET", "GETBIT", "GETRANGE", "GETSET", "HDEL", "HELLO", "HEXISTS", "HGET", "HGETALL", "HINCRBY", "HINCRBYFLOAT", 
    "HKEYS", "HLEN", "HMGET", "HMSET", "HSET", "HSETNX", "HSTRLEN", "HVALS", "INCR", "INCRBY", "INCRBYFLOAT", "INFO", "LOLWUT", "KEYS", "LASTSAVE", "LINDEX", 
    "LINSERT", "LLEN", "LPOP", "LPOS", "LPUSH", "LPUSHX", "LRANGE", "LREM", "LSET", "LTRIM", "MEMORY DOCTOR", "MEMORY HELP", "MEMORY MALLOC-STATS", "MEMORY PURGE", 
    "MEMORY STATS", "MEMORY USAGE", "MGET", "MIGRATE", "MODULE LIST", "MODULE LOAD", "MODULE UNLOAD", "MONITOR", "MOVE", "MSET", "MSETNX", "MULTI", "OBJECT", 
    "PERSIST", "PEXPIRE", "PEXPIREAT", "PFADD", "PFCOUNT", "PFMERGE", "PING", "PSETEX", "PSUBSCRIBE", "PUBSUB", "PTTL", "PUBLISH", "PUNSUBSCRIBE", "QUIT", 
    "RANDOMKEY", "READONLY", "READWRITE", "RENAME", "RENAMENX", "RESTORE", "ROLE", "RPOP", "RPOPLPUSH", "RPUSH", "RPUSHX", "SADD", "SAVE", "SCARD", "SCRIPT DEBUG", 
    "SCRIPT EXISTS", "SCRIPT FLUSH", "SCRIPT KILL", "SCRIPT LOAD", "SDIFF", "SDIFFSTORE", "SELECT", "SET", "SETBIT", "SETEX", "SETNX", "SETRANGE", "SHUTDOWN", 
    "SINTER", "SINTERSTORE", "SISMEMBER", "SLAVEOF", "REPLICAOF", "SLOWLOG", "SMEMBERS", "SMOVE", "SORT", "SPOP", "SRANDMEMBER", "SREM", "STRALGO", "STRLEN", 
    "SUBSCRIBE", "SUNION", "SUNIONSTORE", "SWAPDB", "SYNC", "PSYNC", "TIME", "TOUCH", "TTL", "TYPE", "UNSUBSCRIBE", "UNLINK", "UNWATCH", "WAIT", "WATCH", "ZADD", 
    "ZCARD", "ZCOUNT", "ZINCRBY", "ZINTERSTORE", "ZLEXCOUNT", "ZPOPMAX", "ZPOPMIN", "ZRANGE", "ZRANGEBYLEX", "ZREVRANGEBYLEX", "ZRANGEBYSCORE", "ZRANK", "ZREM", 
    "ZREMRANGEBYLEX", "ZREMRANGEBYRANK", "ZREMRANGEBYSCORE", "ZREVRANGE", "ZREVRANGEBYSCORE", "ZREVRANK", "ZSCORE", "ZUNIONSTORE", "SCAN", "SSCAN", "HSCAN", 
    "ZSCAN", "XINFO", "XADD", "XTRIM", "XDEL", "XRANGE", "XREVRANGE", "XLEN", "XREAD", "XGROUP", "XREADGROUP", "XACK", "XCLAIM", "XPENDING", "LATENCY DOCTOR", 
    "LATENCY GRAPH", "LATENCY HISTORY", "LATENCY LATEST", "LATENCY RESET", "LATENCY HELP"
};

/* **************************************************
   Initialize ramis module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
  int i=0;
  size_t sz = sizeof(commandnames)/sizeof(char*);

  duk_push_object(ctx); // the return object

  /* Push constructor function */
  duk_push_c_function(ctx, duk_rp_ra_constructor, 2 /*nargs*/);

  duk_push_object(ctx); /* -> stack: [ ret protoObj ] */

  duk_push_c_function(ctx, duk_rp_ra_send, DUK_VARARGS); /* [ ret proto fn_exec ] */
  duk_put_prop_string(ctx, -2, "exec");                  /* [ ret protoObj-->[exec=fn_exec] ] */

  duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ ret-->[prototype-->[exe=fn_exe,...]] ] */

  duk_put_prop_string(ctx, -2, "init"); /* -> stack: [ ret ] */

  /* create_client for node.js-like redis interface */

  duk_push_c_function(ctx, duk_rp_cc_constructor, 3);

  duk_push_object(ctx);
  for (i=0; i<sz; i++)
    PUSHCMD(commandnames[i], commandcommands[i]);

  duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ ret-->[prototype-->[exe=fn_exe,...]] ] */

  duk_put_prop_string(ctx, -2, "createClient"); /* -> stack: [ ret ] */



  return 1;
}
