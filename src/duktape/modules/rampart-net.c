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
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include "rampart.h"
#include "event.h"
#include "event2/bufferevent_ssl.h"
#include "event2/dns.h"
#include "openssl/ssl.h"
#include "openssl/err.h"

/*
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

 
//  kinda unused and purposeless right now 
//  In node it appears to be used for net.blockList

duk_ret_t duk_rp_net_socket_address(duk_context *ctx) {

    const char *ip = "127.0.0.1", *dom_name="ipv4",
               *func_name = "SocketAddress";
    int port = 0, flow=0, domain = AF_INET;
    duk_idx_t tidx;

    // allow call to SocketAddress() with "new SocketAddress()" only
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
*/


/* ****************** net general functions ****************** */

/* the problem being addressed with sinfo->thiskey:
It is mostly for net.connect, but better illustrated with the following - 

This works fine:
var s = new net.Socket();

s.connect({port:8088}, function(){
    this.on('data', function(data) {
        console.log(data.length);
    });
    this.write("GET / HTTP/1.0\r\n\r\n");
});

This wouldn't work but for the fix below using sinfo->thiskey

(new net.Socket()).connect({port:8088}, function(){
    this.on('data', function(data) {
        console.log(data.length);
    });
    this.write("GET / HTTP/1.0\r\n\r\n");
});

By the time the socket.on('connect') callback run in the event loop, 'this' is gone if a reference
isn't saved in javascript. So we save it in the stash before any other libevent events 
calling javascript need it.

It is then removed upon cleanup below.

The downside of all of this is that we cannot have a finalizer on the return value from new net.Socket().
But at least the server disconnecting acts as a finalizer.

*/

volatile uint32_t this_id=0;

#define RPSOCK struct rp_socket_callback_s

RPSOCK {
    duk_context           * ctx;            // a copy of the current duk context
    void                  * thisptr;        // heap pointer to 'this' of current net object
    uint32_t                thiskey;        // a key for stashing an non-disappearing ref to 'this' in stash
    struct event_base     * base;           // current event base
    struct bufferevent    * bev;            // current bufferevent
    timeout_callback      * post_dns_cb;    // next callback (via settimeout) after dns resolution
    struct evconnlistener **listeners;      // for server only - a list of listeners
    void                  * aux;            // generic pointer
    SSL_CTX               * ssl_ctx;        //
    SSL                   * ssl;            //
    size_t                  read;           // bytes read
    size_t                  written;        // bytes written
};

int rp_put_gs_object(duk_context *ctx, const char *objname, const char *key);

RPSOCK * new_sockinfo(duk_context *ctx)
{
    RPSOCK *args = NULL;
    char keystr[16];
    struct event_base *base=NULL;
    void *thisptr=NULL;

    duk_push_this(ctx);
    thisptr = duk_get_heapptr(ctx, -1);

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
    args->bev=NULL;
    args->thiskey = this_id++;
    args->ssl_ctx=NULL;
    args->ssl=NULL;
    args->aux=NULL;
    args->listeners=NULL;
    args->post_dns_cb=NULL;

    sprintf(keystr, "%d", (int) args->thiskey);
    rp_put_gs_object(ctx, "connkeymap", keystr );

    return args;
}

// put supplied function into this._event using supplied event name
// this_idx may be DUK_INVALID_INDEX, in which case this will be retrieved by function
// cb_idx must contain a function, or error is thrown
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


static duk_ret_t duk_rp_net_x_on(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "socket.on: first argument must be a String (event name)" );

    duk_push_this(ctx);
    duk_rp_net_on(ctx, "socket.on", evname, 1, -1);

    return 1;
}

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
            }
            // [ ..., callback_array, this, [args, args], retval/err ]
            duk_pop(ctx); //discard retval/err
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


/* get put or delete a key from a named object in global stash.
   return 1 if successful, leave retrieved, put or deleted value on stack;
   return 0 if fail(get), doesn't exist(delete) and leave undefined on stack.
   put always succeeds, putting value on top of stack to stash = { objname: {key: val} } .
   if key is NULL, operates on object itself.  stash = { objname: val }
   but if NULL, val must be an object for put
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

void socket_cleanup(duk_context *ctx, RPSOCK *sinfo)
{
    char keystr[16];

    if (!sinfo)
        return;

    if(!ctx) 
        ctx = sinfo->ctx;


    // this
    duk_push_heapptr(ctx, sinfo->thisptr);
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "destroyed");
    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );
    duk_del_prop_string(ctx, -1, "remoteAddress");
    duk_del_prop_string(ctx, -1, "remotePort");
    duk_del_prop_string(ctx, -1, "remoteFamily");
    duk_del_prop_string(ctx, -1, "readyState");

    sprintf(keystr, "%d", (int) sinfo->thiskey);
    if(!rp_del_gs_object(ctx, "connkeymap", keystr ))
        fprintf(stderr,"failed to find keystr in connkeymap\n");

    if(sinfo->bev)
        bufferevent_free(sinfo->bev);

    //freed by bufferevent
    //if(sinfo->ssl)
    //    SSL_free(sinfo->ssl);

    if(sinfo->ssl_ctx)
        SSL_CTX_free(sinfo->ssl_ctx);

    free(sinfo);

    //'this' is still on top
    do_callback(ctx, "close", 0);
}

/* ****************** dns/resolve functions ******************* */

static duk_ret_t resolve_finalizer(duk_context *ctx)
{
    struct addrinfo *res;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("resolvep"));
    res = duk_get_pointer(ctx, -1);
    //printf("freeing res\n");
    freeaddrinfo(res);

    return 0;
}

/* take an addrinfo and put info it into an object */

static void push_addrinfo(duk_context *ctx, struct addrinfo *res, const char *hn)
{
    struct addrinfo *freeres;
    int i=0;
    char addrstr[INET6_ADDRSTRLEN];
    void *saddr=NULL;
    duk_idx_t obj_idx, i4arr_idx, i6arr_idx, iarr_idx;

    freeres = res;

    duk_push_object(ctx);
    obj_idx = duk_get_top_index(ctx);

//    duk_push_string(ctx, "");
//    duk_put_prop_string(ctx, obj_idx, "errMsg");

    if(hn)
    {
        duk_push_string(ctx, hn);
        duk_put_prop_string(ctx, obj_idx, "host");
    }

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

}

static int push_resolve(duk_context *ctx, const char *hn)
{
    struct addrinfo hints, *res;
    int ecode;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;


    ecode = getaddrinfo (hn, NULL, &hints, &res);
    if (ecode != 0)
    {
        //printf("ecode = %d\n",ecode);
        duk_push_object(ctx);
        duk_push_string(ctx, gai_strerror(ecode));
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    push_addrinfo(ctx, res, hn);

    return 1;    
}


duk_ret_t duk_rp_net_resolve(duk_context *ctx)
{
    push_resolve(ctx, 
        REQUIRE_STRING(ctx, 0, "resolve: argument must be a String") );    
    return 1;
}

#define DNS_CB_ARGS struct dns_callback_arguments_s

DNS_CB_ARGS {
    struct evdns_base *dnsbase;
    char *host;
};

void async_dns_callback(int errcode, struct evutil_addrinfo *addr, void *arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;
    DNS_CB_ARGS *dnsargs = (DNS_CB_ARGS *) sinfo->aux;  

    duk_push_heapptr(ctx, sinfo->thisptr); //this

    if(errcode)
    {
        free(dnsargs->host);
        evdns_base_free(dnsargs->dnsbase,0 );
        free(sinfo->aux);
        sinfo->aux=NULL;
        duk_push_string(ctx, evutil_gai_strerror(errcode) );        
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo);
        return;
    }
    else
    {
        push_addrinfo(ctx, addr, dnsargs->host);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "_hostAddrs");
        do_callback(ctx, "lookup", 1);
        free(dnsargs->host);
        evdns_base_free(dnsargs->dnsbase, 0);
        free(sinfo->aux);
        sinfo->aux=NULL;
        if(sinfo->post_dns_cb)
            (void)(sinfo->post_dns_cb)(sinfo, 0);
    }
}

void async_resolve(RPSOCK *sinfo, const char *hn)
{
    struct evdns_base *dnsbase;
    duk_context *ctx = sinfo->ctx;
    struct evutil_addrinfo hints;
    DNS_CB_ARGS *dnsargs = NULL;
//    struct evdns_getaddrinfo_request *req;
//    struct user_data *user_data;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = EVUTIL_AI_CANONNAME;

    /* Unless we specify a socktype, we'll get at least two entries for
     * each address: one for TCP and one for UDP. That's not what we
     * want. */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    dnsbase = evdns_base_new(sinfo->base, 
        EVDNS_BASE_DISABLE_WHEN_INACTIVE|EVDNS_BASE_INITIALIZE_NAMESERVERS );
    if (!dnsbase)
    {
        duk_push_heapptr(ctx, sinfo->thisptr); //this

        duk_push_string(ctx, "Could not initiate dns_base" );
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo);
        return;
    }

    REMALLOC(dnsargs, sizeof(DNS_CB_ARGS));
    dnsargs->host = strdup(hn);
    dnsargs->dnsbase = dnsbase;
    sinfo->aux = (void *)dnsargs;

//    req = 
    evdns_getaddrinfo(
            dnsbase, hn, NULL,
            &hints, async_dns_callback, sinfo);
}

/* ********* resolve_async ***************** */

int resolver_callback(void *arg, int unused)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;

    socket_cleanup(ctx,sinfo);

    return 0;        
}


duk_ret_t duk_rp_net_resolver_async_resolve(duk_context *ctx)
{
    RPSOCK *args = NULL;
    const char *host = duk_get_string(ctx, 0);
    duk_idx_t tidx;

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    args = new_sockinfo(ctx);

    // for error callback

    args->post_dns_cb = resolver_callback; // for cleanup

    if(duk_is_function(ctx, 1))
    {
        duk_dup(ctx, 1);
        duk_put_prop_string(ctx, tidx, "_callback"); // for error func using net.resolve_async()

        duk_rp_net_on(ctx, "resolve_async", "lookup", 1, 2); // run callback as "lookup" event
    }
    async_resolve(args, host);

    return 1; // this on top
}

duk_ret_t resolve_async_err(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_callback");
    duk_push_object(ctx);
    duk_pull(ctx, 0);
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_call(ctx, 1);
    return 0;
}

/* we need a prototype and a 'this' for async_resolve above */
duk_ret_t duk_rp_net_resolver_async(duk_context *ctx)
{
    duk_push_this(ctx);

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "_events");

    /* error event */
    duk_push_c_function(ctx, resolve_async_err, 2);
    duk_rp_net_on(ctx, "resolve_async", "error", -1, -2);
    duk_pop(ctx);

    return 1;
}

/* this is basically something like:

  net.resolve_async = function(host, callback) {
      var resolver = new net.Resolver();
      resolver.on("error", function(err) {
          callback({errMsg:err});
      }
      resolver.resolve(host, callback);
  }
  where callback is function(hostinfo){}

   It is necessary to have a constructor in order to work with
   the dns functions above.
*/

duk_ret_t duk_rp_net_resolve_async(duk_context *ctx)
{
    if(!duk_is_string(ctx, 0))
        RP_THROW(ctx, "resolve_async: first argument must be a String (hostname)");

    if(!duk_is_function(ctx, 1))
        RP_THROW(ctx, "resolve_async: second argument must be a function");  

    duk_push_c_function(ctx, duk_rp_net_resolver_async, 0);
    duk_push_object(ctx); // prototype
    duk_push_c_function(ctx, duk_rp_net_resolver_async_resolve, 2);
    duk_put_prop_string(ctx, -2, "resolve");
    duk_put_prop_string(ctx, -2, "prototype");
    duk_new(ctx, 0); // var r = new resolver();
    duk_push_string(ctx, "resolve"); // r.resolve
    duk_dup(ctx,0); // host
    duk_dup(ctx,1); // function
    duk_call_prop(ctx, -4, 2); // r.resolve(host, function(){})
    return 0;
}

/* *** new net.Resolver() *** */
duk_ret_t duk_rp_net_resolver(duk_context *ctx)
{
    /* allow call to net.Resolver() with "new net.Resolver()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "_events");

    return 1;

}


/* ****************** net.Socket() functions ****************** */



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
    
    /* function not required. may be set later with socket.on() */
    if(duk_is_function(ctx, 1))
    {
        //duk_push_this(ctx);
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


static int set_nodelay(int fd, int nodelay)
{
    errno=0;
    if(setsockopt(fd, SOL_TCP, TCP_NODELAY, &nodelay, sizeof(int) ))
        return 0;// fail
    return 1;
}

static duk_ret_t socket_set_nodelay(duk_context *ctx)
{
    int nd=1, fd;
    RPSOCK *sinfo;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo") );
    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!sinfo)
        RP_THROW(ctx, "socket.setNoDelay: No connection info found.  Socket must be connected in order set keepalive");

    fd = bufferevent_getfd(sinfo->bev);

    nd = duk_get_boolean_default(ctx, 0, 1);
    
    if(!set_nodelay(fd, nd))
        RP_THROW(ctx, "socket.setNoDelay: Error setting noDelay - %s", strerror(errno) );

    return 1;    
}

static int set_keepalive(int fd, int keepalive, int idle, int intv, int cnt) {
    do {
        size_t sz = sizeof(int);

        errno=0;
        if(idle < 1) idle = 1;
        if(intv < 1) intv = 1;
        if(cnt  < 1) cnt  = 1;

    	if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sz ))
    	    break;

        if(!keepalive)
            return 1;

        if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sz ))
            break;

        if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sz ))
            break;

        if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intv, sz ))
            break;

        return 1;//success
    } while(0);

	return 0;//fail, check errno
}

static duk_ret_t socket_set_keepalive(duk_context *ctx)
{
    int ka=0, ka_params[3]= {1, 1, 10}, fd=-1, i=1;
    RPSOCK *sinfo;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo") );
    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(!sinfo)
        RP_THROW(ctx, "socket.setKeepAlive: No connection info found.  Socket must be connected in order set keepalive");

    fd = bufferevent_getfd(sinfo->bev);

    ka = REQUIRE_BOOL(ctx, 0, "socket.setKeepAlive: first parameter must be a Boolean (enable keepalive)");
    if(!ka)
    {
        if(!set_keepalive(fd, 0, 1, 1, 1))
            RP_THROW(ctx, "socket.setKeepAlive: Error setting keepalive - %s", strerror(errno) );
        return 1; //'this' on top
    } 

    while (i < 4)
    {
        if(!duk_is_undefined(ctx, i))
            ka_params[i-1]=REQUIRE_NUMBER(ctx, i, "socket.setKeepAlive: argument %d must be a number\n", (int)i);
        i++;
    }    

    if(!set_keepalive(fd, 1,  ka_params[0], ka_params[1], ka_params[2]))
        RP_THROW(ctx, "socket.setKeepAlive: Error setting keepalive - %s", strerror(errno) );


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
        int fd = bufferevent_getfd(bev);
        struct sockaddr addr;
        socklen_t sz = sizeof(struct sockaddr);
        char addrstr[INET6_ADDRSTRLEN];
        int localport;

        duk_push_heapptr(ctx, sinfo->thisptr);
        duk_push_pointer(ctx, sinfo);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );

        /* set flags */
        duk_push_false(ctx);
        duk_put_prop_string(ctx, -2, "connecting");

        duk_push_true(ctx);
        duk_put_prop_string(ctx, -2, "connected");

        duk_push_string(ctx, "open");
        duk_put_prop_string(ctx, -2, "readyState");

        /* set local host/port */
        if(getsockname(fd, &addr, &sz ))
        {
            //do error
            duk_dup(ctx, -1);
            duk_push_sprintf(ctx, "socket.connect: could not get local host/port");        
            do_callback(ctx, "error", 1);
        }

        if( addr.sa_family == AF_INET ) {
            struct sockaddr_in *sa = ( struct sockaddr_in *) &addr;

            inet_ntop (addr.sa_family, (void * ) &( ((struct sockaddr_in *)&addr)->sin_addr), addrstr, INET6_ADDRSTRLEN);
            localport = (int) ntohs(sa->sin_port);
        }
        else
        {
            struct sockaddr_in6 *sa = ( struct sockaddr_in6 *) &addr;

            inet_ntop (addr.sa_family, (void*) &(((struct sockaddr_in6*)&addr)->sin6_addr), addrstr, INET6_ADDRSTRLEN);
            localport = (int) ntohs(sa->sin6_port);
        }
            
        duk_push_string(ctx, addrstr);
        duk_put_prop_string(ctx, -2, "localAddress");
        duk_push_int(ctx, localport);
        duk_put_prop_string(ctx, -2, "localPort");

        /* apply timeout and keepalive */
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
        duk_pop(ctx);
        
        duk_get_prop_string(ctx, -1, "keepAlive");
        if(duk_get_boolean_default(ctx, -1, 0))
        {
            int idle, intv, cnt;

            duk_pop(ctx); // true
            duk_get_prop_string(ctx, -1, "keepAliveInitialDelay");
            idle = duk_get_int_default(ctx, -1, 1);
            duk_pop(ctx);//number/undefined

            duk_get_prop_string(ctx, -1, "keepAliveInterval");
            intv = duk_get_int_default(ctx, -1, 1);
            duk_pop(ctx);//number/undefined

            duk_get_prop_string(ctx, -1, "keepAliveCount");
            cnt = duk_get_int_default(ctx, -1, 10);
            duk_pop(ctx);//number/undefined

            if(!set_keepalive(fd, 1, idle, intv, cnt))
            {
                duk_push_heapptr(ctx, sinfo->thisptr);
                duk_push_sprintf(ctx, "keepAlive internal error: %s", strerror(errno));        
                do_callback(ctx, "error", 1);
            }
        }
        else 
            duk_pop(ctx); // false/undef     
        
        duk_pop(ctx); //this

        /* finish bevent setup */
        bufferevent_enable(bev, EV_READ|EV_WRITE);
        bufferevent_setcb(bev, sock_readcb, sock_writecb, sock_eventcb, sinfo);

        /* do js evnet callbacks */
        sock_do_callback(sinfo, "connect");
        sock_do_callback(sinfo, "ready");

        return;
    }

    if (events == (BEV_EVENT_EOF | BEV_EVENT_READING)) {

        if (errno == EAGAIN) {
            // note from libevhtp_ws:
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
    int port=-1, err=0, family=0, tls=0;
    struct sockaddr   * sin;
    size_t sin_sz;
    duk_idx_t top = duk_get_top(ctx);
    unsigned long ssl_err=0;

    duk_push_heapptr(ctx, sinfo->thisptr);

    // get port
    if(duk_get_prop_string(ctx, -1, "_hostPort") )
        port = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "family") )
        family = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "tls") )
        tls = duk_get_boolean_default(ctx, -1, 0);
    duk_pop(ctx);

    // get ip address(es)
    if(duk_get_prop_string(ctx, -1,"_hostAddrs"))
    {
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("resolvep")) )
            res = (struct addrinfo *) duk_get_pointer(ctx, -1);
        duk_pop(ctx);
    }
    duk_pop(ctx);
    

    if(tls)
    {
        sinfo->bev = NULL;
        do {
            sinfo->ssl_ctx = SSL_CTX_new(TLS_client_method());
            if(!sinfo->ssl_ctx)
            {
                ssl_err=ERR_get_error();
                break;
            }

            sinfo->ssl = SSL_new(sinfo->ssl_ctx);
            if(!sinfo->ssl)
            {
                ssl_err=ERR_get_error();
                break;
            }

            sinfo->bev = bufferevent_openssl_socket_new(
                sinfo->base, -1, sinfo->ssl, BUFFEREVENT_SSL_CONNECTING, BEV_OPT_CLOSE_ON_FREE
            );

        } while (0);
    }
    else
        sinfo->bev = bufferevent_socket_new(sinfo->base, -1, BEV_OPT_CLOSE_ON_FREE);

    if(!sinfo->bev)
    {
        if(ssl_err)
        {
            char errmsg[256];
            ERR_error_string(ssl_err, errmsg);
            duk_push_string(ctx, errmsg);
        }
        else if(errno)
            duk_push_string(ctx, strerror(errno));
        else
            duk_push_string(ctx, "error opening socket");
        //[ ..., this, errmsg ]
        do_callback(ctx, "error", 1);
        errno=0;
        socket_cleanup(ctx, sinfo);
        return 0;
    }
        

    bufferevent_enable(sinfo->bev, EV_READ);

    bufferevent_setcb( sinfo->bev, NULL, NULL, sock_eventcb, sinfo);

    if(family)
    {
        int sfam = family==6 ? AF_INET6 : AF_INET;
        while (res)
        {
            if(res->ai_family == sfam)
                break;
            res = res->ai_next;
        }
    }    

    /* shouldn't happen */
    if( !res )
    {
        duk_push_sprintf(ctx, "socket.connect: could not find an ipv%d address for host", family);        
        do_callback(ctx, "error", 1);
        duk_set_top(ctx, top);
        socket_cleanup(ctx, sinfo);
        return 0;
    }        

    {
        void *saddr=NULL;
        char addrstr[INET6_ADDRSTRLEN];
        int ipf=4;
        switch(res->ai_family)
        {
            case AF_INET:
                saddr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
                break;
            case AF_INET6:
                ipf=6;
                saddr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
                break;
        }
        inet_ntop (res->ai_family, saddr, addrstr, INET6_ADDRSTRLEN);
        duk_push_string(ctx, addrstr);
        duk_put_prop_string(ctx, -2, "remoteAddress");
        duk_push_int(ctx, port);
        duk_put_prop_string(ctx, -2, "remotePort");
        duk_push_sprintf(ctx, "IPv%d", ipf);
        duk_put_prop_string(ctx, -2, "remoteFamily");
    }

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
        socket_cleanup(ctx, sinfo);
        return 0;
    }        

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "destroyed");

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "pending");

    //duk_pop(ctx); //this
    duk_set_top(ctx, top);
    return 0;
}


#define socket_getport(port, idx, func_name) do {\
double port_num = REQUIRE_NUMBER(ctx, idx, "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);\
if (port_num != floor(port_num) || port_num < 1 || port_num > 65535)\
    RP_THROW(ctx,  "%s: option 'port' must be an Integer >= 0 and < 65536", func_name);\
port = (int) port_num;\
} while(0)

duk_ret_t duk_rp_net_socket_connect(duk_context *ctx)
{
    int port=0, family=0;
    const char *host="localhost";
    duk_idx_t i=0, tidx, obj_idx=-1, conncb_idx=DUK_INVALID_INDEX;
    RPSOCK *args = NULL;

    while (i < 3)
    {
        if(duk_is_number(ctx,i))
            socket_getport(port, i, "socket.connect");
        else if (duk_is_string(ctx,i))
            host = duk_get_string(ctx, i);
        else if (duk_is_function(ctx, i))
            conncb_idx=i;
        else if (!duk_is_array(ctx, i) && duk_is_object(ctx, i) )
            obj_idx=i;
        else if (!duk_is_undefined(ctx, i))
            RP_THROW(ctx, "socket.connect: argument %d is invalid", (int)i+1);
        i++;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    if(obj_idx != -1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "host"))
            host = REQUIRE_STRING(ctx, -1, "socket.connect: option 'host' must be a String");
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "port"))
        {
            socket_getport(port, -1, "socket.connect");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "keepAlive"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAlive' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "keepAlive");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "tls"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'tls' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "tls");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInitialDelay"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveInitialDelay' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInitialDelay");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInterval"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveInterval' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInterval");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveCount"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveCount' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveCount");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "family"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'family' must be a Number (0, 4 or 6)");

            family = duk_get_int_default(ctx, -1, 0);

            if(family != 0 && family != 6 && family != 4)
                RP_THROW(ctx, "socket.connect: option 'family' must be a Number (0, 4 or 6)");
            
            duk_put_prop_string(ctx, tidx, "family");
        }   
        else
            duk_pop(ctx);     

    }

    if(!port)
        RP_THROW(ctx, "socket.connect: port must be specified");

/*

    if(!push_resolve(ctx, host))
    {
        duk_get_prop_string(ctx, -1, "errMsg");
        duk_put_prop_string(ctx, tidx, "Error");
        duk_pull(ctx, tidx);
        return 1;       
    }
    if(family == 4 && !duk_has_prop_string(ctx, -1, "ipv4") )
        RP_THROW(ctx, "socket.connect: ipv4 specified but host '%s' has no ipv4 address", host);

    if(family == 6 && !duk_has_prop_string(ctx, -1, "ipv6") )
        RP_THROW(ctx, "socket.connect: ipv6 specified but host '%s' has no ipv6 address", host);
    duk_put_prop_string(ctx, tidx, "_hostAddrs");
*/

    duk_push_int(ctx,port);
    duk_put_prop_string(ctx, tidx, "_hostPort");

    if(conncb_idx!=DUK_INVALID_INDEX)
    {
        duk_dup(ctx, conncb_idx);
        duk_rp_net_on(ctx, "socket.connect", "connect", -1, tidx);
    }

    args = new_sockinfo(ctx);
    args->post_dns_cb = make_sock_conn;
    async_resolve(args, host);
//    duk_rp_insert_timeout(ctx, 0, "socket.connect", make_sock_conn, args, 
//        DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);


    duk_push_string(ctx, "opening");
    duk_put_prop_string(ctx, -2, "readyState");

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "connecting");
    return 1;
}

duk_ret_t duk_rp_net_socket(duk_context *ctx)
{
    //int fd=-1;
    //duk_bool_t readable=1, writable=1, allowHalf=0;
    duk_bool_t tls=0;
    duk_idx_t tidx;

    /* allow call to net.Socket() with "new net.Socket()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);    
    tidx = duk_get_top_index(ctx);

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "pending");

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "connected");

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, tidx, "_events");

    // get options:
    if(duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "tls"))
        {
            tls = REQUIRE_BOOL(ctx, -1, "new Socket: option 'tls' must be a Boolean");
        }
        duk_pop(ctx);

        /* not implemented
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
        */
    } 
    else if (!duk_is_undefined(ctx, 0) )
        RP_THROW(ctx, "new net.Socket: first argument must be an Object (options)");

    // put options in 'this'
    duk_push_boolean(ctx, tls);
    duk_put_prop_string(ctx, tidx, "tls");

    /* not implemented
    duk_push_boolean(ctx, readable);
    duk_put_prop_string(ctx, tidx, "readable");

    duk_push_boolean(ctx, writable);
    duk_put_prop_string(ctx, tidx, "writable");

    duk_push_boolean(ctx, allowHalf);
    duk_put_prop_string(ctx, tidx, "allowHalfOpen");

    duk_push_int(ctx, fd);
    duk_put_prop_string(ctx, tidx, "fd");
    */

    return 0;
}

/* ******************************* net.connect() ******************* */

duk_ret_t duk_rp_net_server_connect(duk_context *ctx)
{
    int port=0, family=0;
    const char *host="localhost";
    duk_idx_t i=0, tidx, obj_idx=-1, conncb_idx=DUK_INVALID_INDEX;
    struct event_base *base=NULL;
    void *thisptr=NULL;
    RPSOCK *args = NULL;
    char keystr[16];

    while (i < 3)
    {
        if(duk_is_number(ctx,i))
            socket_getport(port, i, "socket.connect");
        else if (duk_is_string(ctx,i))
            host = duk_get_string(ctx, i);
        else if (duk_is_function(ctx, i))
            conncb_idx=i;
        else if (!duk_is_array(ctx, i) && duk_is_object(ctx, i) )
            obj_idx=i;
        else if (!duk_is_undefined(ctx, i))
            RP_THROW(ctx, "socket.connect: argument %d is invalid", (int)i+1);
        i++;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);
    thisptr = duk_get_heapptr(ctx, -1);

    if(obj_idx != -1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "host"))
            host = REQUIRE_STRING(ctx, -1, "socket.connect: option 'host' must be a String");
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "port"))
        {
            socket_getport(port, -1, "socket.connect");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "keepAlive"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAlive' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "keepAlive");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "tls"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'tls' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "tls");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInitialDelay"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveInitialDelay' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInitialDelay");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInterval"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveInterval' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInterval");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveCount"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'keepAliveCount' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveCount");
        }   
        else
            duk_pop(ctx);     

        if(duk_get_prop_string(ctx, obj_idx, "family"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'family' must be a Number (0, 4 or 6)");

            family = duk_get_int_default(ctx, -1, 0);

            if(family != 0 && family != 6 && family != 4)
                RP_THROW(ctx, "socket.connect: option 'family' must be a Number (0, 4 or 6)");
            
            duk_put_prop_string(ctx, tidx, "family");
        }   
        else
            duk_pop(ctx);     

    }

    if(!port)
        RP_THROW(ctx, "socket.connect: port must be specified");

    if(!push_resolve(ctx, host))
    {
        duk_get_prop_string(ctx, -1, "errMsg");
        duk_put_prop_string(ctx, tidx, "Error");
        duk_pull(ctx, tidx);
        return 1;       
    }
    if(family == 4 && !duk_has_prop_string(ctx, -1, "ipv4") )
        RP_THROW(ctx, "socket.connect: ipv4 specified but host '%s' has no ipv4 address", host);

    if(family == 6 && !duk_has_prop_string(ctx, -1, "ipv6") )
        RP_THROW(ctx, "socket.connect: ipv6 specified but host '%s' has no ipv6 address", host);

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
    args->bev=NULL;
    args->thiskey = this_id++;
    args->ssl_ctx=NULL;
    args->ssl=NULL;


    duk_rp_insert_timeout(ctx, 0, "socket.connect", make_sock_conn, args, 
        DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

    duk_pull(ctx, tidx);
    // see discussion above about sinfo->thiskey
    sprintf(keystr, "%d", (int) args->thiskey);
    rp_put_gs_object(ctx, "connkeymap", keystr );

    duk_push_string(ctx, "opening");
    duk_put_prop_string(ctx, -2, "readyState");

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "connecting");
    return 1;
}
duk_ret_t net_create_connection(duk_context *ctx)
{
    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, "Socket");

    // [ arg1, arg2, arg3, net, socket ]
    duk_remove(ctx , -2); // current function
    // [ arg1, arg2, arg3, socket ]
    
    //net.createConnection(options[, connectListener])
    if(duk_is_object(ctx, 0))
    {
        duk_dup(ctx, 0);
        // [ arg1, arg2, arg3_undef, socket, arg1_obj ]
        duk_new(ctx, 1);
        // [ arg1, arg2, arg3_undef, new_socket ]
        duk_push_string(ctx, "connect");
        // [ arg1, arg2, arg3_undef, new_socket, "connect" ]
        duk_pull(ctx, 0);
        duk_pull(ctx, 0);
        // [ arg3_undef, new_socket, "connect", arg1, arg2 ]
        duk_remove(ctx, 0);
        // [ new_socket, "connect", arg1, arg2 ]
        duk_call_prop(ctx, 0, 2);
        // [ new_socket, retval ]
        duk_pop(ctx);
        // [ new_socket ]
    }
    else if (duk_is_number(ctx, 0))
    // net.createConnection(port[, host][, connectListener]) 
    {
        // [ arg1_port, arg2, arg3, socket ]
        duk_new(ctx, 0);
        // [ arg1_port, arg2, arg3, new_socket ]
        duk_insert(ctx, 0);
        // [ new_socket, arg1_port, arg2, arg3 ]
        duk_push_string(ctx, "connect");
        duk_insert(ctx, 1);
        // [ new_socket, "connect", arg1_port, arg2, arg3 ]
        duk_call_prop(ctx, 0, 3);
        // [ new_socket, retval ]
        duk_pop(ctx);
        // [ new_socket ]
    }

    return 1;
}        



duk_ret_t duk_open_module(duk_context *ctx)
{
    /* return object */
    duk_push_object(ctx);

    /*
    //SocketAddress -- currently unused
    duk_push_c_function(ctx, duk_rp_net_socket_address, 1);
    duk_push_object(ctx);
    duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "SocketAddress");
    */

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
        duk_push_c_function(ctx, duk_rp_net_x_on, 2);
        duk_put_prop_string(ctx, -2, "on");

        //socket.destroy()
        duk_push_c_function(ctx, socket_destroy, 1);
        duk_put_prop_string(ctx, -2, "destroy");

        //socket.setTimeout
        duk_push_c_function(ctx, socket_set_timeout, 2);
        duk_put_prop_string(ctx, -2, "setTimeout");

        //socket.setKeepAlive
        duk_push_c_function(ctx, socket_set_keepalive, 4);
        duk_put_prop_string(ctx, -2, "setKeepAlive");

        //socket.setNodelay
        duk_push_c_function(ctx, socket_set_nodelay, 1);
        duk_put_prop_string(ctx, -2, "setNoDelay");

        duk_put_prop_string(ctx, -2, "prototype");

    // new net.Socket()
    duk_dup(ctx, -1); //copy of Socket
    duk_put_prop_string(ctx, -3, "Socket");


    // net.createConnection & net.connect alias
    duk_push_c_function(ctx, net_create_connection, 3);

    duk_pull(ctx, -2); //Socket reference
    duk_put_prop_string(ctx, -2, "Socket"); //save Socket as property of function
    duk_dup(ctx, -1);  //make a copy and save under both connect and createConnection
    duk_put_prop_string(ctx, -3, "connect");
    duk_put_prop_string(ctx, -2, "createConnection");

    // resolve
    duk_push_c_function(ctx, duk_rp_net_resolve, 1);
    duk_put_prop_string(ctx, -2, "resolve");

    // net.Resolver()
    duk_push_c_function(ctx, duk_rp_net_resolver, 0);
    duk_push_object(ctx); // prototype

        //resolver.resolve
        duk_push_c_function(ctx, duk_rp_net_resolver_async_resolve, 2);
        duk_put_prop_string(ctx, -2, "resolve");

        // resolver.on()        
        duk_push_c_function(ctx, duk_rp_net_x_on, 2);
        duk_put_prop_string(ctx, -2, "on");

        duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "Resolver");

    //resolve_async
    duk_push_c_function(ctx, duk_rp_net_resolve_async, 2);
    duk_put_prop_string(ctx, -2, "resolve_async");

    return 1;
}
