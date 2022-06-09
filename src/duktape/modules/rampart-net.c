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
duk_ret_t duk_rp_net_on(duk_context *ctx)
{
    duk_idx_t tidx;
    const char *fname="net";

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    if(duk_get_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("netfunc")))
        fname=duk_get_string(ctx,-1);
    duk_pop(ctx);

    if(!duk_is_string(ctx, 0))
        RP_THROW(ctx, "%s: first argument must be a String (event name)", fname);

    if(!duk_is_function(ctx,1))
        RP_THROW(ctx, "%s: second argument must be a Function (listener)", fname);

    // _event must be present and an object that was set up in constructor func. No check here.
    duk_get_prop_string(ctx, tidx, "_events");
    duk_dup(ctx, 0);
    duk_dup(ctx, 1);
    duk_put_prop(ctx, -3);

    // return 'this'
    duk_pull(ctx, tidx);
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

//get put or delete a key from a named object in global stash.
//return 1 if successful, leave retrieved, put or deleted value on stack;
//return 0 if fail(get), doesn't exist(delete) and leave undefined on stack.
// put always succeeds, putting value on top of stack to stash = { objname: {key: val} } .
// if key is NULL, operates on object itself.  stash = { objname: val }
// but val must be an object for put

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
            RP_THROW("YOU SCREWED UP, in put_gs_object");

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
        duk_dup(ctx);
        duk_put_prop_string(ctx, stash_idx, objname); 
    }

    obj_idx = duk_get_top_index(ctx);

    duk_dup(ctx, val_idx);
    duk_put_prop_string(ctx, obj_idx, key);

    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return 1;
}

int rp_del_gs_object(duk_context *ctx, const char *objname, const char *key)
{
    duk_idx_t stash_idx = -1, obj_idx=-1;
    int ret=1;

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    // if no key, get the object itself
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

int rp_get_gs_object(duk_context *ctx, const char *objname, const char *key)
{
    duk_idx_t stash_idx = -1, obj_idx=-1;
    int ret=1;

    duk_idx_t stash_idx = -1, obj_idx=-1;
    int ret=1;

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    // if no key, get the object itself
    if(key == NULL)
    {
        if( ! duk_get_prop_string(ctx, stash_idx, objname) )
            ret = 0; //top of stack == undefined

        duk_remove(ctx, stash_idx);
        return ret;
    }

    // with a key:
    if(!duk_get_prop_string(ctx, stash_idx, objname))
    {
        duk_remove(ctx, stash_idx);
        return 0; //undefined on top
    }

    obj_idx = duk_get_top_index(ctx);

    if( !duk_get_prop_string(ctx, obj_idx, key) )
        ret=0;

    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return ret;
}

/*


static int gs_object(duk_context *ctx, const char *objname, const char *key, int op)
{
    duk_idx_t val_idx=-1, stash_idx = -1, obj_idx=-1;
    int ret=1;

    if(op == STASH_PUT)
        val_idx=duk_get_top_index(ctx);

    duk_push_global_stash(ctx);
    stash_idx = duk_get_top_index(ctx);

    // if no key, get the object itself
    if(key == NULL)
    {
        switch (op)
        {
            case STASH_PUT:
                //put the object from stack top to global_stash.objname=top_obj

                //temporary warning for debugging -- remove later:
                if(!duk_is_object(ctx, val_idx))
                    RP_THROW("YOU SCREWED UP, in gs_object");

                duk_dup(ctx, val_idx);
                duk_put_prop_string(ctx, stash_idx, objname);
                break;
            case STASH_GET:
                if( !duk_get_prop_string(ctx, stash_idx, objname) )
                    ret=0; //top of stack == undefined
                break;
            case STASH_DEL: //leave retrieved object on top of stack, but delete in stash
                if( duk_get_prop_string(ctx, stash_idx, objname) )
                    duk_del_prop_string(ctx, stash_idx, objname);
                else
                    ret=0; //top of stack == undefined
                break;
        }
        duk_remove(ctx, stash_idx);

        return ret;
    }

    if(!duk_get_prop_string(ctx, stash_idx, objname))
    {
        if(op==STASH_GET || op==STASH_DEL)
        {
            duk_remove(ctx, stash_idx);
            return 0; //undefined on top
        }

        duk_pop(ctx); //remove undefined
        // make new object, store ref in global stash
        duk_push_object(ctx);
        duk_dup(ctx);
        duk_put_prop_string(ctx, stash_idx, objname); 
    }

    obj_idx = duk_get_top_index(ctx);


    switch (op)
    {
        case STASH_PUT:
            duk_dup(ctx, val_idx);
            duk_put_prop_string(ctx, obj_idx, key);
            break;

        case STASH_GET:
            if(!duk_get_prop_string(ctx, obj_idx, key))
                ret=0;
            break;

        case STASH_DEL:
            if( duk_get_prop_string(ctx, obj_idx, key) )
            {
                duk_del_prop_string(ctx, obj_idx, key);
                duk_enum(ctx, obj_idx, 0);
                if(duk_next
            }
            else
                ret=0;
            break;
    }
    duk_remove(ctx, obj_idx);
    duk_remove(ctx, stash_idx);

    return ret;
}
 
*/

/* a list of timeouts pending */
#define NETARGS struct net_args

NETARGS {
    duk_context *ctx;
    struct event *e;
    double key;
    int repeat;
    struct timeval timeout;
    struct timespec start_time;
    callback *cb;
    void *cbarg;
    SLIST_ENTRY(net_args) entries;
};




duk_ret_t duk_rp_net_socket_connect(duk_context *ctx)
{
    int port=0, localport=0, family=0;
    const char *host=NULL, *localAddr=NULL;
    duk_idx_t tidx;
    struct event_base *base=NULL;

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

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    if(!push_resolve(ctx, host))
    {
        duk_get_prop_string(ctx, -1, "errMsg");
        duk_put_prop_string(ctx, tidx, "Error");
        duk_pull(ctx, tidx);
        return 1;       
    }

    duk_put_prop_string(ctx, tidx, "_hostAddrs");

    /* if we are threaded, base will not be global struct event_base *elbase */
    duk_push_global_stash(ctx);
    if( duk_get_prop_string(ctx, -1, "elbase") )
        base=duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);



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

    // socket.on()        
    duk_push_c_function(ctx, duk_rp_net_on, 2);
    duk_put_prop_string(ctx, -2, "on");

    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "Socket");
    

    // resolve
    duk_push_c_function(ctx, duk_rp_net_resolve, 1);
    duk_put_prop_string(ctx, -2, "resolve");

    return 1;
}


