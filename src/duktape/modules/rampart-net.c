/* Copyright (C) 2022  Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h> // for syscall(SYS_gettid)
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include "rampart.h"
/*
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <grp.h>
#ifdef __APPLE__
#include <uuid/uuid.h>
#include <sys/sysctl.h>
#endif
#include <pwd.h>

#include "evhtp/evhtp.h"
#include "../ws/evhtp_ws.h"
#include "rampart.h"
#include "../core/module.h"
#include "mime.h"
#include "../register.h"
#include "../globals/printf.h"
#include "libdeflate.h"

*/

#include "event.h"

static int push_ip_canon(duk_context *ctx, const char *addr, int domain)
{
    unsigned char buf[sizeof(struct in6_addr)];
    char str[INET6_ADDRSTRLEN];
    int result = inet_pton(domain, addr, buf);

    if(!result)
    {
        duk_push_undefined(ctx);
        return 0;
    }

    if (inet_ntop(domain, buf, str, INET6_ADDRSTRLEN) == NULL)
    {
        duk_push_undefined(ctx);
        return 0;
    }
    
    duk_push_string(ctx, str);
    return 1;
}


duk_ret_t duk_rp_net_socket_address(duk_context *ctx) {

    const char *ip = "127.0.0.1", *dom_name="ipv4",
               *func_name = "SocketAddress";
    int port = 0, flow=0, domain = AF_INET;
    duk_idx_t tidx;

    /* allow call to SocketAddress() with "new SocketAddress()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "pending");

    if(duk_get_prop_string(ctx, 0, "address"))
        ip=REQUIRE_STRING(ctx,-1, "%s: option 'address' must be a String", func_name);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, 0, "family"))
    {
        dom_name=REQUIRE_STRING(ctx,-1, "%s: option 'family' must be a String ('ipv4' or 'ipv6')", func_name);
        if ( strcmp("ipv6",dom_name) == 0 )
            domain = AF_INET6;
        else if( strcmp("ipv4",dom_name) != 0 )
            RP_THROW(ctx, "%s: option 'family' must be a String ('ipv4' or 'ipv6')", func_name);
    }
    duk_pop(ctx);

    if(!push_ip_canon(ctx, ip, domain))
        RP_THROW(ctx, "%s: invalid ip address", func_name);
    duk_put_prop_string(ctx, tidx, "address");

    duk_push_string(ctx, dom_name);
    duk_put_prop_string(ctx, tidx, "family");

    if(duk_get_prop_string(ctx, 0, "port"))
    {
        double port_num = REQUIRE_NUMBER(ctx, -1, "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);
        
        if (port_num != floor(port_num) || port_num < 0 || port_num > 65535)
            RP_THROW(ctx,  "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);
        
        port = (int) port_num;
    }
    duk_pop(ctx);
    duk_push_int(ctx, port);
    duk_put_prop_string(ctx, tidx, "port");

    if(duk_get_prop_string(ctx, 0, "flowlabel"))
    {
        double flow_num = REQUIRE_NUMBER(ctx, -1, "%s: option 'flowlabel' must be an Integer >= 0 and < 4294967296", func_name);
        
        if (flow_num != floor(flow_num) || flow_num < 0 || flow_num > 4294967295)
            RP_THROW(ctx, "%s: option 'flowlabel' must be an Integer >= 0 and < 4294967296", func_name);
        
        flow = (int) flow_num;
    }
    duk_pop(ctx);
    duk_push_int(ctx, flow);
    duk_put_prop_string(ctx, tidx, "flow");

    return 0; //constructor, ret value not needed.
}


duk_ret_t duk_rp_net_socket(duk_context *ctx)
{
    duk_bool_t readable=1, writable=1, allowHalf=0;
    int fd=-1;
    duk_idx_t tidx;

    /* allow call to SocketAddress() with "new SocketAddress()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);    
    tidx = duk_get_top_index(ctx);

    // the name of our function, for socket.on, etc.
    duk_push_string(ctx, "socket");
    duk_put_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("netfunc"));

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, tidx, "_events");

    // get options:
    if(duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "fd"))
        {
            fd=REQUIRE_INT(ctx, -1, "new Socket: option 'fd' must be an integer"); 
        }
        duk_pop(ctx);

        if(fd > -1) {
            readable=0;
            writable=0;

            if(duk_get_prop_string(ctx, 0, "readable"))
            {
                readable = REQUIRE_BOOL(ctx, -1, "new Socket: option 'readable' must be a Boolean");
            }
            duk_pop(ctx);

            if(duk_get_prop_string(ctx, 0, "writable"))
            {
                writable = REQUIRE_BOOL(ctx, -1, "new Socket: option 'writable' must be a Boolean");
            }
            duk_pop(ctx);
        }

        if(duk_get_prop_string(ctx, 0, "allowHalfOpen"))
        {
            allowHalf = REQUIRE_BOOL(ctx, -1, "new Socket: option 'allowHalfOpen' must be a Boolean");
        }
        duk_pop(ctx);
    }

    // put options in 'this'
    duk_push_boolean(ctx, readable);
    duk_put_prop_string(ctx, tidx, "readable");

    duk_push_boolean(ctx, writable);
    duk_put_prop_string(ctx, tidx, "writable");

    duk_push_boolean(ctx, allowHalf);
    duk_put_prop_string(ctx, tidx, "allowHalfOpen");

    duk_push_int(ctx, fd);
    duk_put_prop_string(ctx, tidx, "fd");

    return 0;
}



// put supplied function into this._event using supplied event name
static void duk_rp_net_on(duk_context *ctx, const char *fname, const char *evname, duk_idx_t cb_idx, duk_idx_t this_idx)
{
    duk_idx_t tidx;
    duk_size_t len=0;

    cb_idx = duk_normalize_index(ctx, cb_idx);

    if(this_idx == DUK_INVALID_INDEX)
    {
        duk_push_this(ctx);
        tidx = duk_get_top_index(ctx);
    }
    else
        tidx = duk_normalize_index(ctx, this_idx);

    if(!duk_is_function(ctx,cb_idx))
        RP_THROW(ctx, "%s: argument %d must be a Function (listener)", fname, (int)cb_idx+1);

    // _event must be present and an object that was set up in constructor func. No check here.
    duk_get_prop_string(ctx, tidx, "_events");

    if(!duk_get_prop_string(ctx, -1, evname)) // the array of callbacks
    {
        duk_pop(ctx); //undefined
        duk_push_array(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, evname);
    }
    else
        len=duk_get_length(ctx, -1);

    duk_dup(ctx, cb_idx);
    duk_put_prop_index(ctx, -2, len);
    duk_pop_2(ctx); // "_events", array

    if(this_idx == DUK_INVALID_INDEX)
        duk_remove(ctx, tidx);
}


static duk_ret_t duk_rp_socket_on(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "socket.on: first argument must be a String (event name)" );

    duk_push_this(ctx);
    duk_rp_net_on(ctx, "socket.on", evname, 1, -1);

    return 1;
}

static duk_ret_t resolve_finalizer(duk_context *ctx)
{
    struct addrinfo *res;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("resolvep"));
    res = duk_get_pointer(ctx, -1);
    //printf("freeing res\n");
    freeaddrinfo(res);

    return 0;
}

static int push_resolve(duk_context *ctx, const char *hn)
{

    struct addrinfo hints, *res, *freeres;
    int ecode, i=0;
    char addrstr[INET6_ADDRSTRLEN];
    void *saddr=NULL;
    duk_idx_t obj_idx, i4arr_idx, i6arr_idx, iarr_idx;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;

    duk_push_object(ctx);
    obj_idx = duk_get_top_index(ctx);

    ecode = getaddrinfo (hn, NULL, &hints, &res);
    if (ecode != 0)
    {
        //printf("ecode = %d\n",ecode);
        duk_push_string(ctx, gai_strerror(ecode));
        duk_put_prop_string(ctx, obj_idx, "errMsg");
        return 0;
    }

    freeres = res;

    duk_push_string(ctx, "");
    duk_put_prop_string(ctx, obj_idx, "errMsg");

    duk_push_string(ctx, hn);
    duk_put_prop_string(ctx, obj_idx, "host");
    
    duk_push_array(ctx);
    i4arr_idx = duk_get_top_index(ctx);
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, obj_idx, "ip4addrs");
 
    duk_push_array(ctx);
    i6arr_idx = duk_get_top_index(ctx);
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, obj_idx, "ip6addrs");

    duk_push_array(ctx);
    iarr_idx = duk_get_top_index(ctx);
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, obj_idx, "ipaddrs");
    
  
    while(res)
    {
        switch(res->ai_family)
        {
            case AF_INET:
                saddr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
                break;
            case AF_INET6:
                saddr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
                break;
        }
        inet_ntop (res->ai_family, saddr, addrstr, INET6_ADDRSTRLEN);

        if(!i)
        {
            if(res->ai_canonname)
                duk_push_string(ctx, (const char *) res->ai_canonname);
            else
                duk_push_null(ctx);
            duk_put_prop_string(ctx, obj_idx, "canonName");

            duk_push_string(ctx, (const char *) addrstr);
            duk_put_prop_string(ctx, obj_idx, "ip");
        }

        duk_push_string(ctx, (const char *) addrstr);
        duk_dup(ctx, -1);
        duk_put_prop_index(ctx, iarr_idx, (duk_uarridx_t)duk_get_length(ctx, iarr_idx) );

        if( res->ai_family == PF_INET6 )
        {
            duk_uarridx_t l = (duk_uarridx_t)duk_get_length(ctx, i6arr_idx);
            if(!l)
            {
                duk_dup(ctx, -1);
                duk_put_prop_string(ctx, obj_idx, "ipv6");
            }
            duk_put_prop_index(ctx, i6arr_idx, l );
        }
        else
        {
            duk_uarridx_t l = (duk_uarridx_t)duk_get_length(ctx, i4arr_idx);
            if(!l)
            {
                duk_dup(ctx, -1);
                duk_put_prop_string(ctx, obj_idx, "ipv4");
            }
            duk_put_prop_index(ctx, i4arr_idx, l );
        }
        res = res->ai_next;
        i++;
    }
  
    //freeaddrinfo(freeres);
    duk_push_pointer(ctx, freeres);
    duk_put_prop_string(ctx, obj_idx, DUK_HIDDEN_SYMBOL("resolvep"));

    duk_remove(ctx, iarr_idx);
    duk_remove(ctx, i6arr_idx);
    duk_remove(ctx, i4arr_idx);

    // obj_idx is now at -1
    duk_push_c_function(ctx, resolve_finalizer, 1);
    duk_set_finalizer(ctx, -2); 
    return 1;    
}


duk_ret_t duk_rp_net_resolve(duk_context *ctx)
{
    push_resolve(ctx, 
        REQUIRE_STRING(ctx, 0, "resolve: argument must be a String") );    

    return 1;
}

/* get put or delete a key from a named object in global stash.
   return 1 if successful, leave retrieved, put or deleted value on stack;
   return 0 if fail(get), doesn't exist(delete) and leave undefined on stack.
   put always succeeds, putting value on top of stack to stash = { objname: {key: val} } .
   if key is NULL, operates on object itself.  stash = { objname: val }
   but val must be an object for put
*/

int rp_put_gs_object(duk_context *ctx, const char *objname, const char *key)
{
    duk_idx_t val_idx=-1, stash_idx = -1, obj_idx=-1;

    val_idx=duk_get_top_index(ctx);

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    // if no key, store to the object itself
    if(key == NULL)
    {
        //put the object from stack top to global_stash.objname=top_obj

        //temporary warning for debugging -- remove later:
        if(!duk_is_object(ctx, val_idx))
            RP_THROW(ctx, "YOU SCREWED UP, in put_gs_object");

        duk_dup(ctx, val_idx);
        duk_put_prop_string(ctx, stash_idx, objname);

        duk_remove(ctx, stash_idx);

        return 1;
    }

    if(!duk_get_prop_string(ctx, stash_idx, objname))
    {

        duk_pop(ctx); //remove undefined

        // make new object, store ref in global stash
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, stash_idx, objname); 
    }

    obj_idx = duk_get_top_index(ctx);

    duk_dup(ctx, val_idx);
    duk_put_prop_string(ctx, obj_idx, key);

    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return 1;
}

/*
    Delete a value from an object named objname in the global stash using key .
    If key is NULL, delete the entire object named objname.
    A copy of value/undefined is left on top of stack, 
      returns 1/value for success, 0/undefined for not present.
    To delete the final reference, 'pop(ctx);'
*/

int rp_del_gs_object(duk_context *ctx, const char *objname, const char *key)
{
    duk_idx_t stash_idx = -1, obj_idx=-1;
    int ret=1;

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    // if no key, delete the object/item 'objname' itself
    if(key == NULL)
    {
        if( duk_get_prop_string(ctx, stash_idx, objname) )
            duk_del_prop_string(ctx, stash_idx, objname);
        else
            ret = 0; //top of stack == undefined

        duk_remove(ctx, stash_idx);
        return ret; // top of stack == deleted object
    }

    // with a key:
    if(!duk_get_prop_string(ctx, stash_idx, objname))
    {
        duk_remove(ctx, stash_idx);
        return 0; //undefined on top
    }

    obj_idx = duk_get_top_index(ctx);

    if( duk_get_prop_string(ctx, obj_idx, key) )
        duk_del_prop_string(ctx, obj_idx, key); //val on top
    else
        ret=0; //undefined on top

    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return ret;
}

/*
    Get a value from an object named objname in the global stash using key .
    If key is NULL, get the entire object named objname.
    Value/undefined is left on top of stack, returns 1/value for success, 0/undefined for not present
*/

int rp_get_gs_object(duk_context *ctx, const char *objname, const char *key)
{
    duk_idx_t stash_idx = -1, obj_idx=-1;
    int ret=1;

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    if(!duk_get_prop_string(ctx, stash_idx, objname))
    {
        duk_remove(ctx, stash_idx);
        return 0; //undefined on top
    }

    //if no key, leave item/object 'objname' on top of stack
    if(key == NULL)
        return ret;

    obj_idx = duk_get_top_index(ctx);

    if( !duk_get_prop_string(ctx, obj_idx, key) )
        ret=0;

    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return ret;
}


#define NETARGS struct net_args

NETARGS {
    duk_context *ctx;
    struct event *e;
    double key;
    int repeat;
    struct timeval timeout;
    struct timespec start_time;
    net_callback *cb;
    void *cbarg;
    SLIST_ENTRY(net_args) entries;
};


#define RPSOCK struct rp_socket_callback_s


RPSOCK {
    duk_context        * ctx;
    void               * thisptr;
    struct event_base  * base;
    struct bufferevent * bev;
    size_t               read;
    size_t               written;
};

/* 'this' must be on top of stack
       unless there are args, then it must directly preceed args. 
   'this' must have callbacks stored in an object called "_events"
   'this' and args are removed from stack
*/
static int do_callback(duk_context *ctx, const char *ev_s, duk_idx_t nargs)
{
    //[ ..., this, [args, args] ]

    duk_get_prop_string(ctx, -1 - nargs, "_events");

    //[ ..., this, [args, args], eventobj ]

    if(duk_get_prop_string(ctx, -1, ev_s))
    {
        //[ ..., this, [args, args], eventobj, callback_array ]

        duk_size_t i=0, len = duk_get_length(ctx, -1);
        duk_idx_t  j=0, arr_idx = -2 - nargs;
        duk_remove(ctx, -2);
        // [..., this, [args, args], callback_array ]
        duk_insert(ctx, -2 - nargs );
        // [..., callback_array, this, [args, args] ]

        while (i < len)
        {
            duk_get_prop_index(ctx, arr_idx, i);
            // [..., callback_array, this, [args, args], callback ]
            
            duk_dup(ctx, arr_idx); //relative so its the next item
            // [..., callback_array, this, [args, args], callback, this ]
            
            j=0;
            while(j < nargs)
            {
                duk_dup(ctx, arr_idx);
                j++;
            }
            // [..., callback_array, this, [args, args], callback, this, [args, args] ]

            if(duk_pcall_method(ctx, nargs) != 0)
            {
                if (duk_is_error(ctx, -1) )
                {
                    duk_get_prop_string(ctx, -1, "stack");
                    fprintf(stderr, "Error in %s callback: %s\n", ev_s, duk_get_string(ctx, -1));
                    duk_pop(ctx);
                }
                else if (duk_is_string(ctx, -1))
                {
                    fprintf(stderr, "Error in %s callback: %s\n", ev_s, duk_get_string(ctx, -1));
                }
                else
                {
                    fprintf(stderr, "Error in %s callback\n", ev_s);
                }
                // [..., callback_array, this, [args, args], err ]
                duk_pop(ctx); 
                // [..., callback_array, this, [args, args] ]
            }
            // [ ..., callback_array, this, [args, args], retval ]
            duk_pop(ctx); //discard retval
            i++;
            // [ ..., callback_array, this, [args, args] ]
        }
        duk_pop_n(ctx, 2 + nargs);
        return 0;
    }
    // [ ..., this,  [args, args], eventobj, undefined ]
    duk_pop_n(ctx, 3 + nargs);

    return 0;
}

static int sock_do_callback(RPSOCK *sinfo, const char *ev_s)
{
    duk_context *ctx = sinfo->ctx;

    // push this
    duk_push_heapptr(ctx, sinfo->thisptr);
    return do_callback(ctx, ev_s, 0);
}


static void sock_readcb(struct bufferevent * bev, void * arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    struct evbuffer *evbuf = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(evbuf);
    unsigned char *buf = evbuffer_pullup(evbuf, -1);
    duk_context *ctx = sinfo->ctx;

    sinfo->read += len;

    // [ ..., this ]
    duk_push_heapptr(ctx, sinfo->thisptr);
    
    duk_push_number(ctx, sinfo->read);
    duk_put_prop_string(ctx, -2, "bytesRead");

    duk_push_external_buffer(ctx);
    duk_config_buffer(ctx, -1, buf, len);
    // [ ..., this, bufferdata ] 
    duk_dup(ctx, -1);
    duk_insert(ctx, 0);// save for later to invalidate it.
    // [ bufferdata, ..., this, bufferdata ]
    do_callback(ctx, "data", 1);
    // [ bufferdata, ...]
    duk_config_buffer(ctx, 0, NULL, 0);// zero out buffer in case there are other refs in js.
    duk_remove(ctx, 0); //remove it

    evbuffer_drain(evbuf, len);//drain it.

/*
printf("readcb\n");
    char buf[1024];
    int n;
    struct evbuffer *input = bufferevent_get_input(bev);
    while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, stdout);
    }
*/
}

void socket_cleanup(duk_context *ctx, RPSOCK *sinfo)
{
    if (!sinfo)
        return;

    if(!ctx) 
        ctx = sinfo->ctx;


    // this
    duk_push_heapptr(ctx, sinfo->thisptr);
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "destroyed");
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );

    bufferevent_free(sinfo->bev);
    free(sinfo);

    //'this' is still on top
    do_callback(ctx, "close", 0);
}


static duk_ret_t socket_destroy(duk_context *ctx)
{
    RPSOCK *sinfo;
    duk_idx_t tidx;

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        duk_pop(ctx);
        duk_push_true(ctx);
        duk_put_prop_string(ctx, -2, "destroyed");
        return 1; //this
    }

    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    if(sinfo)
        socket_cleanup(ctx,sinfo);

    duk_pull(ctx, tidx);
    return 1;
}

static duk_ret_t socket_set_timeout(duk_context *ctx)
{
    duk_idx_t tidx;

    (void) REQUIRE_NUMBER(ctx, 0, "socket.setTimeout: first argument must be a number (timeout in ms)");

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);
    
    if(duk_is_function(ctx, 1))
    {
        duk_push_this(ctx);
        duk_rp_net_on(ctx, "socket.on", "timeout", 1, tidx);
    }

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo"));

    duk_dup(ctx,0);
    duk_put_prop_string(ctx, tidx, "timeout");

    duk_pull(ctx, tidx);
    return 1;
}

static void sock_writecb(struct bufferevent * bev, void * arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    sock_do_callback(sinfo, "drain");
}


static duk_ret_t socket_write(duk_context *ctx)
{
    RPSOCK *sinfo;
    duk_size_t sz;
    const void *data;
    int res;

    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        duk_push_false(ctx);
        return 1;
    }

    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!sinfo)
    {
        duk_push_false(ctx);
        return 1;
    }

    data = (void *)REQUIRE_STR_OR_BUF(ctx, 0, &sz, "socket.write: Argument must be a String or Buffer");

    res = bufferevent_write(sinfo->bev, data, sz);


    if(res == 0)
    {
        sinfo->written+=sz;
        duk_push_number(ctx, sinfo->written);
        duk_put_prop_string(ctx, -2, "bytesWritten");
        duk_push_true(ctx);
    }
    else
    {
        if(errno)
            duk_push_string(ctx, strerror(errno));
        else
            duk_push_string(ctx, "error writing");
        do_callback(ctx, "error", 1);
        errno=0;
        socket_cleanup(ctx, sinfo);
        duk_push_false(ctx);
    }

    return 1;
}




#define true 1
#define false 0

#define SOCKET_IS_READING(b) ((bufferevent_get_enabled(b) & \
                                EV_READ) ? true : false)


static void sock_eventcb(struct bufferevent * bev, short events, void * arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;
/*
    printf("%p %p eventcb %s%s%s%s\n", arg, (void *)bev,
        events & BEV_EVENT_CONNECTED ? "connected" : "",
        events & BEV_EVENT_ERROR     ? "error"     : "",
        events & BEV_EVENT_TIMEOUT   ? "timeout"   : "",
        events & BEV_EVENT_EOF       ? "eof"       : "");
*/
    if (events & BEV_EVENT_CONNECTED)
    {

        duk_push_heapptr(ctx, sinfo->thisptr);
        duk_push_pointer(ctx, sinfo);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );

        if(duk_get_prop_string(ctx, -1, "timeout") )
        {
            double to = duk_get_number_default(ctx, -1, 0);
            if(to == 0.0)
                bufferevent_set_timeouts(sinfo->bev, NULL, NULL);
            else
            {
                struct timeval ctimeout;
                to /= 1000; //ms to seconds
                ctimeout.tv_sec = (time_t) to;
                ctimeout.tv_usec = (suseconds_t)1000000.0 * (to - (double)ctimeout.tv_sec);
                bufferevent_set_timeouts(sinfo->bev, &ctimeout, &ctimeout);
            }
        }
        duk_pop_2(ctx);

        bufferevent_enable(bev, EV_READ|EV_WRITE);
        bufferevent_setcb(bev, sock_readcb, sock_writecb, sock_eventcb, sinfo);
        sock_do_callback(sinfo, "connect");
        sock_do_callback(sinfo, "ready");
        return;
    }

    if (events == (BEV_EVENT_EOF | BEV_EVENT_READING)) {

        if (errno == EAGAIN) {
            /* libevent will sometimes recv again when it's not actually ready,
             * this results in a 0 return value, and errno will be set to EAGAIN
             * (try again). This does not mean there is a hard socket error, but
             * simply needs to be read again.
             *
             * but libevent will disable the read side of the bufferevent
             * anyway, so we must re-enable it.
             */

            if (SOCKET_IS_READING(bev) == false) {
                bufferevent_enable(bev, EV_READ);
            }

            errno = 0;

            return;
        }

    }

    if (events & BEV_EVENT_EOF)
    {
        sock_do_callback(sinfo, "end");
        socket_cleanup(ctx, sinfo);
        return;
    }

    if( events & BEV_EVENT_ERROR)
    {
        duk_push_heapptr(ctx, sinfo->thisptr);
        duk_push_string(ctx, strerror(errno));        
        do_callback(ctx, "error", 1);
        errno=0;
        socket_cleanup(ctx, sinfo);
        return;
    }

    if ( events & BEV_EVENT_TIMEOUT ) 
    {
        sock_do_callback(sinfo, "timeout");
        socket_cleanup(ctx, sinfo);
        return;
    }        
}

int make_sock_conn(void *arg, int after)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    struct addrinfo *res=NULL;
    duk_context *ctx = sinfo->ctx;
    int port=-1, err=0;
    struct sockaddr   * sin;
    size_t sin_sz;
    duk_idx_t top = duk_get_top(ctx);

    duk_push_heapptr(ctx, sinfo->thisptr);

    // get port
    if(duk_get_prop_string(ctx, -1, "_hostPort") )
        port = duk_get_int(ctx, -1);
    duk_pop(ctx);

    // get ip address(es)
    if(duk_get_prop_string(ctx, -1,"_hostAddrs"))
    {
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("resolvep")) )
            res = (struct addrinfo *) duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);
    

    sinfo->bev = bufferevent_socket_new(sinfo->base, -1, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_enable(sinfo->bev, EV_READ);

    bufferevent_setcb( sinfo->bev, NULL, NULL, sock_eventcb, sinfo);

    sin = res->ai_addr;

    switch(sin->sa_family)
    {
        case AF_INET:
        {
            struct sockaddr_in *in4 = (struct sockaddr_in *) sin;
            in4->sin_port=htons(port);
            sin_sz = sizeof(struct sockaddr_in);
            break;
        }
        case AF_INET6:
        {
            struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) sin;
            in6->sin6_port=htons(port);
            sin_sz = sizeof(struct sockaddr_in6);
            break;
        }

        default:
            duk_set_top(ctx, top);
            return 0;
    }

    err = bufferevent_socket_connect(sinfo->bev, sin, sin_sz);

    if (err)
    {
        duk_push_string(ctx, strerror(errno));        
        do_callback(ctx, "error", 1);
        duk_set_top(ctx, top);
        return 0;
    }        

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "destroyed");

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "connecting");

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "pending");

    //duk_pop(ctx); //this
    duk_set_top(ctx, top);
    return 0;
}


#define socket_getport(port, idx, func_name) do {\
double port_num = REQUIRE_NUMBER(ctx, idx, "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);\
if (port_num != floor(port_num) || port_num < 0 || port_num > 65535)\
    RP_THROW(ctx,  "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);\
port = (int) port_num;\
} while(0)

duk_ret_t duk_rp_net_socket_connect(duk_context *ctx)
{
    int port=0;
    const char *host="127.0.0.1";
    duk_idx_t tidx, conncb_idx=DUK_INVALID_INDEX;
    struct event_base *base=NULL;
    void *thisptr=NULL;
    RPSOCK *args = NULL;

    //socket.connect(port[, host][, connectListener])    
    if( duk_is_number(ctx, 0)) {
        socket_getport(port, 0, "socket.connect");
        if(duk_is_string(ctx,1)) 
        {
            host = duk_get_string(ctx, 1);
            if(duk_is_function(ctx,2))
                conncb_idx=2;
        }
        else if (!duk_is_undefined(ctx, 1))
            RP_THROW(ctx, "socket.connect: second argument must be a string (hostname) when first argument is a port number");
    }
    else
    //socket.connect(options[, connectListener])
    {
        if(!duk_is_object(ctx,0))
            RP_THROW(ctx, "socket.connect: parameter must be an Object");

        if(!duk_get_prop_string(ctx, 0, "host"))
            RP_THROW(ctx, "socket.connect: option 'host' must be set and be a String");
        host = REQUIRE_STRING(ctx, -1, "socket.connect: option 'host' must be a String");
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "port"))
        {
            double port_num = REQUIRE_NUMBER(ctx, -1, "socket.connect: option 'port' must be an Integer >= 0 and < 65536");
            
            if (port_num != floor(port_num) || port_num < 0 || port_num > 65535)
                RP_THROW(ctx,  "%s: option 'port' must be an Integer >= 0 and < 65536");
            
            port = (int) port_num;
        }
        else
            RP_THROW(ctx, "socket.connect: option 'port' must be set and be an Integer >= 0 and < 65536");

        duk_pop(ctx);

        if(duk_is_function(ctx,1))
            conncb_idx=1;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);
    thisptr = duk_get_heapptr(ctx, -1);

    if(!push_resolve(ctx, host))
    {
        duk_get_prop_string(ctx, -1, "errMsg");
        duk_put_prop_string(ctx, tidx, "Error");
        duk_pull(ctx, tidx);
        return 1;       
    }

    duk_put_prop_string(ctx, tidx, "_hostAddrs");

    duk_push_int(ctx,port);
    duk_put_prop_string(ctx, tidx, "_hostPort");

    if(conncb_idx!=DUK_INVALID_INDEX)
    {
        duk_dup(ctx, conncb_idx);
        duk_rp_net_on(ctx, "socket.connect", "connect", -1, tidx);
    }

    /* if we are threaded, base will not be global struct event_base *elbase */
    duk_push_global_stash(ctx);
    if( duk_get_prop_string(ctx, -1, "elbase") )
        base=duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    REMALLOC(args, sizeof(RPSOCK));
    args->ctx=ctx;
    args->thisptr=thisptr;
    args->base = base;
    args->written=0;
    args->read=0;

    duk_rp_insert_timeout(ctx, 0, "socket.connect", make_sock_conn, args, 
        DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

    duk_pull(ctx, tidx);

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "connecting");
    return 1;
}


duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);

    //SocketAddress
    duk_push_c_function(ctx, duk_rp_net_socket_address, 1);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "SocketAddress");


    //net.Socket
    duk_push_c_function(ctx, duk_rp_net_socket, 1);
    duk_push_object(ctx);
    //socket methods:
    
    // socket.connect()
    duk_push_c_function(ctx, duk_rp_net_socket_connect, 3);
    duk_put_prop_string(ctx, -2, "connect");

    // socket.write()
    duk_push_c_function(ctx, socket_write,1);
    duk_put_prop_string(ctx, -2, "write");

    // socket.on()        
    duk_push_c_function(ctx, duk_rp_socket_on, 2);
    duk_put_prop_string(ctx, -2, "on");

    //socket.destroy()
    duk_push_c_function(ctx, socket_destroy, 1);
    duk_put_prop_string(ctx, -2, "destroy");

    //socket.setTimeout
    duk_push_c_function(ctx, socket_set_timeout, 2);
    duk_put_prop_string(ctx, -2, "setTimeout");

    duk_put_prop_string(ctx, -2, "prototype");
    // new net.Socket()
    duk_put_prop_string(ctx, -2, "Socket");
    

    // resolve
    duk_push_c_function(ctx, duk_rp_net_resolve, 1);
    duk_put_prop_string(ctx, -2, "resolve");

    return 1;
}


