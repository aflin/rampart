/* Copyright (C) 2026  Aaron Flin - All Rights Reserved
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
#include "event2/listener.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/rand.h"

#ifdef __CYGWIN__
#include <dlfcn.h>
#else
#include <resolv.h>
#include <arpa/nameser.h>
#endif

#if defined(__FreeBSD__)
#include <netinet/in.h>
#endif


//macos
#if !defined(SOL_TCP) && defined(IPPROTO_TCP)
#define SOL_TCP IPPROTO_TCP
#endif
#if !defined(TCP_KEEPIDLE) && defined(TCP_KEEPALIVE)
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

char *rp_net_def_bundle=NULL;

#ifdef __CYGWIN__
/* rp_cygwin_add_dns_servers is defined in rampart-thread.c and
   declared in rampart.h. It uses GetNetworkParams from iphlpapi.dll
   to add Windows DNS servers to an evdns base. */

static struct evdns_base *rp_cygwin_new_dnsbase(struct event_base *base)
{
    struct evdns_base *dnsbase = evdns_base_new(base,
        EVDNS_BASE_DISABLE_WHEN_INACTIVE);
    if (!dnsbase) return NULL;

    /* Use Windows DNS API to configure nameservers.
       Don't call evdns_base_resolv_conf_parse - it fails on Cygwin
       (no /etc/resolv.conf) and can leave the base in a bad state. */
    if (rp_cygwin_add_dns_servers(dnsbase) != 0)
        evdns_base_nameserver_ip_add(dnsbase, "1.1.1.1");

    return dnsbase;
}
#endif /* __CYGWIN__ */

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

By the time the socket.on('connect') callback run in the event loop, 'this'
is gone via garbage collection if a reference isn't saved in javascript.  So
we save it in the stash before any other libevent events calling javascript
need it.

It is then removed upon cleanup below.

The downside of all of this is that we cannot have a finalizer on the return value from new net.Socket().
But at least the server disconnecting acts as a finalizer.

*/

/* mutex locking for thread dns resolvers
   only needed on creation and destruction */

volatile uint32_t this_id=0, evcb_id=0;

#define RPSOCK struct rp_socket_struct

RPSOCK {
    duk_context           * ctx;            // a copy of the current duk context
    void                  * thisptr;        // heap pointer to 'this' of current net object
    struct event_base     * base;           // current event base for this thread
    struct bufferevent    * bev;            // current bufferevent
    struct evdns_base     * dnsbase;        // event loop dns base for this thread/evbase
    timeout_callback      * post_dns_cb;    // next callback (via settimeout) after dns resolution
    struct evconnlistener **listeners;      // for server only - a list of listeners
    void                  * aux;            // generic pointer
    SSL_CTX               * ssl_ctx;        //
    SSL                   * ssl;            //
    RPSOCK                * parent;         // if a connection from server, the server RPSOCK struct
    size_t                  read;           // bytes read
    size_t                  written;        // bytes written
    int                     fd;             // file descriptor for passing from server to socket
    uint32_t                thiskey;        // a key for stashing a non-disappearing ref to 'this' in stash
    uint32_t                open_conn;      // number of open connections for server, whether opened and incremented for client.
    uint32_t                max_conn;       // maximum number of open connections for server, after which we start dropping connections
};

int rp_put_gs_object(duk_context *ctx, const char *objname, const char *key);

static RPSOCK * new_sockinfo(duk_context *ctx)
{
    RPSOCK *args = NULL;
    char keystr[16];
    RPTHR *thr = get_current_thread();
    void *thisptr=NULL;

    duk_push_this(ctx);
    thisptr = duk_get_heapptr(ctx, -1);
    REMALLOC(args, sizeof(RPSOCK));
    args->ctx=ctx;
    args->thisptr=thisptr;
    args->base = thr->base;
    args->dnsbase = thr->dnsbase;
    args->written=0;
    args->read=0;
    args->open_conn=0;
    args->max_conn=0;
    args->bev=NULL;
    args->thiskey = this_id++;
    args->ssl_ctx=NULL;
    args->ssl=NULL;
    args->aux=NULL;
    args->listeners=NULL;
    args->parent=NULL;
    args->post_dns_cb=NULL;
    args->fd=-1;

    sprintf(keystr, "%d", (int) args->thiskey);
    rp_put_gs_object(ctx, "connkeymap", keystr );

    return args;
}

// put supplied function into this._event using supplied event name
// this_idx may be DUK_INVALID_INDEX, in which case this will be retrieved by function
// cb_idx must contain a function, or error is thrown
static void duk_rp_net_on(duk_context *ctx, const char *fname, const char *evname, duk_idx_t cb_idx, duk_idx_t this_idx)
{
    duk_idx_t tidx, top=duk_get_top(ctx);
    int id = (int) (evcb_id++);

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

    if(!duk_get_prop_string(ctx, -1, evname)) // the object of callbacks
    {
        duk_pop(ctx); //undefined
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, evname);
    }

    // check for duplicate
    if(duk_get_prop_string(ctx, cb_idx, DUK_HIDDEN_SYMBOL("evcb_id")) )
    {
        //printf("%s ID IS THERE\n", evname);
        // don't change the id if this function is in another event
        id = duk_get_int(ctx, -1);
        if(duk_has_prop(ctx, -2)) //this pops the id
        {
            //printf("AND IS IN %s event\n", evname);
            duk_set_top(ctx, top);
            return;
        }
    }
    else
    {
        //printf("%s id not present\n", evname);
        duk_pop(ctx);
    }

    // key for _event.evname object
    duk_push_int(ctx, id);

    // function stored with key id
    duk_dup(ctx, cb_idx);

    // put id in function
    duk_push_int(ctx, id);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("evcb_id") );

    // store it under that id
    duk_put_prop(ctx, -3);

    /*
    duk_pop_2(ctx); // "_events", _event.evname_object

    if(this_idx == DUK_INVALID_INDEX)
        duk_remove(ctx, tidx);
    */
    // end where we started
    duk_set_top(ctx, top);
}


static duk_ret_t duk_rp_net_x_off(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "socket.on: first argument must be a String (event name)" );

    if(!duk_is_function(ctx, 1))
        RP_THROW(ctx, "socket.off: second argument must be a function");

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "_events");

    if(duk_get_prop_string(ctx, -1, evname)) // the object of callbacks
    {
        // get id from function
        duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("evcb_id") );
        // delete function from object of callbacks
        duk_del_prop(ctx, -2);
    }

    duk_pull(ctx, -2);
    return 1;
}

static duk_ret_t duk_rp_net_x_on(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "socket.on: first argument must be a String (event name)" );

    if(!duk_is_function(ctx, 1))
        RP_THROW(ctx, "socket.on: second argument must be a function");

    duk_push_this(ctx);
    duk_rp_net_on(ctx, "socket.on", evname, 1, -1);

    return 1;
}

static duk_ret_t duk_rp_net_x_once(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "socket.on: first argument must be a String (event name)" );

    if(!duk_is_function(ctx, 1))
        RP_THROW(ctx, "socket.once: second argument must be a function");

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("once"));

    duk_push_this(ctx);
    duk_rp_net_on(ctx, "socket.once", evname, 1, -1);

    return 1;
}

/* 'this' must be on top of stack
       unless there are args, then it must directly preceed args.
   'this' must have callbacks stored in an object called "_events"
   'this' and args are removed from stack
*/
static int do_callback(duk_context *ctx, const char *ev_s, duk_idx_t nargs)
{
    duk_idx_t  j=0, errobj=-1;
    duk_idx_t exit_top = duk_get_top(ctx) -1 -nargs; //before 'this'
    int nerrorcb=-1; // number of error callbacks, or -1 if not an error event
    //[ ..., this, [args, args] ]
    if(!strcmp("error",ev_s))
    {
        const char *errstr = "unspecified error";

        nerrorcb=0;

        errobj = duk_get_top_index(ctx);

        if(nargs > 0 && duk_is_string(ctx, -1))
            errstr = duk_get_string(ctx, -nargs);

        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s", errstr);
        duk_replace(ctx, -2);
    }

    //printf("%s top=%d, exittop=%d, looking for events in %d\n", ev_s, duk_get_top(ctx), exit_top, (int)(-1 - nargs));
    duk_get_prop_string(ctx, -1 - nargs, "_events");

    //[ ..., this, [args, args], eventobj ]
    if(duk_get_prop_string(ctx, -1, ev_s))
    {

        //[ ..., this, [args, args], eventobj, callback_object ]
        duk_enum(ctx, -1, 0);
        //[ ..., this, [args, args], eventobj, callback_object, enum ]
        while(duk_next(ctx, -1, 1))
        {
            if(nerrorcb > -1)
                nerrorcb++;
            //[ ..., this, [args, args], eventobj, callback_object, enum, key, val(function) ]

            // check if function is to be run only once
            if(duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("once")) )
            {
                //first delete the "once" property
                duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("once"));
                duk_pull(ctx, -2); // put function key on top of stack
                //[ ..., this, [args, args], eventobj, callback_object, enum, val(function), key ]
                duk_del_prop(ctx, -4);
                //[ ..., this, [args, args], eventobj, callback_object, enum, val(function) ]
            }
            else
            {
                //[ ..., this, [args, args], eventobj, callback_object, enum, key, val(function) ]
                duk_remove(ctx, -2);
                //[ ..., this, [args, args], eventobj, callback_object, enum, val(function) ]
            }

            //dup this
            duk_dup(ctx, -5 - nargs);
            //[ ..., this, [args, args], eventobj, callback_object, enum, val(function), this ]

            for(j=0;j < nargs;j++)
            {
                duk_dup(ctx, -5 - nargs);
            }
            //[ ..., this, [args, args], eventobj, callback_object, enum, val(function), this, [args, args] ]

            if(duk_pcall_method(ctx, nargs) != 0)
            {
                const char *errmsg = rp_push_error(ctx, -1, NULL, rp_print_error_lines);
                fprintf(stderr, "Error in %s callback:\n", ev_s);
                fprintf(stderr, "%s\n", errmsg);
                duk_pop_2(ctx);
                // [ ..., this, [args, args], eventobj, callback_object, enum]
            }
            else // [ ..., this, [args, args], eventobj, callback_object, enum, retval ]
                duk_pop(ctx); //discard retval
        }// while
    }
    //else if(strcmp(ev_s,"lookup")==0){ printf("event %s not found\n", ev_s); safeprettyprintstack(ctx); }
    // else  [ ..., this, [args, args], eventobj, undefined ]
    if(nerrorcb == 0)
    {
        // currently rampart cannot throw errors in event loop.
        // without an ABORT.  Probably (maybe?) don't want to do so anyway.
        duk_pull(ctx, errobj);
        duk_get_prop_string(ctx, -1, "stack");
        fprintf(stderr, "Uncaught Async %s\n", duk_get_string(ctx, -1));
    }
    duk_set_top(ctx, exit_top);
    // [ ... ]

    return 0;
}

duk_ret_t duk_rp_net_x_trigger(duk_context *ctx)
{
    const char *argzero = REQUIRE_STRING(ctx, 0, "first argument must be a string (name of event to trigger)");
    char *ev = strdup(argzero);
    duk_idx_t nargs = 0;

    duk_push_this(ctx);
    duk_replace(ctx, 0); //remove event name string, replace with 'this'

    if(duk_is_undefined(ctx,1)) // no arg
        duk_pop(ctx); //get rid of undefined
    else
        nargs=1;

    do_callback(ctx, ev, nargs);

    // bug fix: free strdup'd event name before return - 2026-02-27
    free(ev);
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

        if(!duk_is_object(ctx, val_idx))
            RP_THROW(ctx, "argument not an object in put_gs_object at stack %d", (int)val_idx);

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

#define WITH_CALLBACKS 1
#define SKIP_CALLBACKS 0

static void socket_cleanup(duk_context *ctx, RPSOCK *sinfo, int docb)
{
    char keystr[16];
    duk_idx_t top;
    if (!sinfo)
        return;

    if(!ctx)
        ctx = sinfo->ctx;

    top=duk_get_top(ctx);
    // this
    duk_push_heapptr(ctx, sinfo->thisptr);
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "destroyed");
    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "connected");
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "pending");
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );
// keep these so we can see last connection in "close"
//    duk_del_prop_string(ctx, -1, "remoteAddress");
//    duk_del_prop_string(ctx, -1, "remotePort");
//    duk_del_prop_string(ctx, -1, "remoteFamily");
    duk_del_prop_string(ctx, -1, "readyState");


    if(sinfo->bev)
    {
        bufferevent_free(sinfo->bev);
        sinfo->bev=NULL;
    }

    // server sinfo
    if(sinfo->listeners)
    {
        struct evconnlistener **l = sinfo->listeners;
        while (*l)
        {
            evconnlistener_free(*l);
            l++;
        }
        free(sinfo->listeners);
        sinfo->listeners=NULL;

        // server has it's own server context
        // clients reuse the global one
        if(sinfo->ssl_ctx)
            SSL_CTX_free(sinfo->ssl_ctx);

        duk_push_false(ctx);
        duk_put_prop_string(ctx, -2, "listening");

        // no open connections:
        if(sinfo->open_conn==0)
        {
            sprintf(keystr, "%d", (int) sinfo->thiskey);
            if(!rp_del_gs_object(ctx, "connkeymap", keystr ))
                fprintf(stderr,"failed to find server keystr in connkeymap\n");
            free(sinfo);
            if(docb)
                do_callback(ctx, "close", 0);
            duk_set_top(ctx,top);
            return;
        }
        else
        // pending connections on server close
        {
            duk_set_top(ctx,top);
            return;
        }
    }

    sprintf(keystr, "%d", (int) sinfo->thiskey);
    if(!rp_del_gs_object(ctx, "connkeymap", keystr ))
        fprintf(stderr,"failed to find keystr in connkeymap\n");

    if(sinfo->parent) //connection from server
    {
        RPSOCK *server = sinfo->parent;
        if(sinfo->open_conn)
            server->open_conn--;
        // server was "closed" and no more listeners
        if(server->listeners==NULL && !server->open_conn)
        {
            //printf("No more open connections\n");

            // run connection close event
            free(sinfo);
            if(docb)
                do_callback(ctx, "close", 0);// uses connection 'this'

            // run server close event
            duk_push_heapptr(ctx, server->thisptr); //server 'this' for callback

            sprintf(keystr, "%d", (int) server->thiskey);
            if(!rp_del_gs_object(ctx, "connkeymap", keystr ))
                fprintf(stderr,"failed to find server keystr in connkeymap\n");

            free(server);
            if(docb)
                do_callback(ctx, "close", 0); //server.close() or server close event callback
            duk_set_top(ctx,top);
            return;
        }
    }

    free(sinfo);

    //'this' is still on top
    if(docb)
        do_callback(ctx, "close", 0);
    duk_set_top(ctx,top);
}

/* ****************** dns/resolve functions ******************* */

static duk_ret_t resolve_finalizer(duk_context *ctx)
{
    struct addrinfo *res;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("resolvep"));
    res = duk_get_pointer(ctx, -1);
    freeaddrinfo(res);

    return 0;
}

static duk_ret_t evresolve_finalizer(duk_context *ctx)
{
    struct addrinfo *res;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("resolvep"));
    res = duk_get_pointer(ctx, -1);
    evutil_freeaddrinfo(res);

    return 0;
}

/* take an addrinfo and put info it into an object */

static void push_addrinfo(duk_context *ctx, struct addrinfo *res, const char *hn, int native_finalizer)
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
            // bug fix: skip unknown ai_family instead of using uninitialized saddr - 2026-02-27
            default:
                res = res->ai_next;
                continue;
        }
        inet_ntop (res->ai_family, saddr, addrstr, INET6_ADDRSTRLEN);

        if(!i)
        {
            /* this isnt working with libevent dns, but works with native. no idea why.
            if(res->ai_canonname)
                duk_push_string(ctx, (const char *) res->ai_canonname);
            else
                duk_push_null(ctx);
            duk_put_prop_string(ctx, obj_idx, "canonName");
            */

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


    duk_push_pointer(ctx, freeres);
    duk_put_prop_string(ctx, obj_idx, DUK_HIDDEN_SYMBOL("resolvep"));

    duk_remove(ctx, iarr_idx);
    duk_remove(ctx, i6arr_idx);
    duk_remove(ctx, i4arr_idx);

    // obj_idx is now at -1
    if(native_finalizer)
        duk_push_c_function(ctx, resolve_finalizer, 1);
    else
        duk_push_c_function(ctx, evresolve_finalizer, 1);

    duk_set_finalizer(ctx, -2);
}

/* *** DNS record type support *** */

typedef struct {
    const char *name;
    int type_val;  /* standard DNS type code (same on all platforms) */
} dns_type_map_t;

static const dns_type_map_t dns_types[] = {
    {"A",     1},
    {"NS",    2},
    {"CNAME", 5},
    {"SOA",   6},
    {"PTR",   12},
    {"MX",    15},
    {"TXT",   16},
    {"AAAA",  28},
    {"SRV",   33},
    {NULL,    0}
};

static int dns_type_from_string(const char *type_str)
{
    int i;
    for (i = 0; dns_types[i].name != NULL; i++) {
        if (strcasecmp(type_str, dns_types[i].name) == 0)
            return dns_types[i].type_val;
    }
    return -1;
}

static int push_resolve(duk_context *ctx, const char *hn)
{
    struct addrinfo hints, *res=NULL;
    int ecode;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;


    ecode = getaddrinfo (hn, NULL, &hints, &res);
    if (ecode != 0)
    {
        if(res)
            freeaddrinfo(res);
        duk_push_object(ctx);
        duk_push_string(ctx, gai_strerror(ecode));
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }
    push_addrinfo(ctx, res, hn, 1);

    return 1;
}

/* resolve arbitrary record types */
#ifndef __CYGWIN__

static int push_resolve_type(duk_context *ctx, const char *hn, int dns_type, const char *type_str)
{
    unsigned char answer[4096];
    int len;
    ns_msg msg;
    ns_rr rr;
    int rrcount, i;
    duk_idx_t obj_idx, arr_idx;
    char dname[NS_MAXDNAME];
    char addrstr[INET6_ADDRSTRLEN];

    len = res_query(hn, ns_c_in, dns_type, answer, sizeof(answer));
    if (len < 0) {
        duk_push_object(ctx);
        duk_push_string(ctx, hstrerror(h_errno));
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    if (ns_initparse(answer, len, &msg) < 0) {
        duk_push_object(ctx);
        duk_push_string(ctx, "Failed to parse DNS response");
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    duk_push_object(ctx);
    obj_idx = duk_get_top_index(ctx);
    duk_push_string(ctx, hn);
    duk_put_prop_string(ctx, obj_idx, "host");
    duk_push_string(ctx, type_str);
    duk_put_prop_string(ctx, obj_idx, "type");
    duk_push_array(ctx);
    arr_idx = duk_get_top_index(ctx);

    rrcount = ns_msg_count(msg, ns_s_an);
    for (i = 0; i < rrcount; i++) {
        if (ns_parserr(&msg, ns_s_an, i, &rr) < 0)
            continue;
        if (ns_rr_type(rr) != dns_type)
            continue;

        switch (dns_type) {
            case 16: /* TXT */ {
                const unsigned char *rdata = ns_rr_rdata(rr);
                int rdlen = ns_rr_rdlen(rr);
                int pos = 0;
                /* concatenate all character-strings within this TXT RR */
                duk_push_string(ctx, "");
                while (pos < rdlen) {
                    int slen = rdata[pos++];
                    if (pos + slen > rdlen) break;
                    duk_push_lstring(ctx, (const char*)&rdata[pos], slen);
                    duk_concat(ctx, 2);
                    pos += slen;
                }
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 15: /* MX */ {
                const unsigned char *rdata = ns_rr_rdata(rr);
                uint16_t priority = ns_get16(rdata);
                if (ns_name_uncompress(ns_msg_base(msg), ns_msg_end(msg),
                        rdata + 2, dname, sizeof(dname)) < 0)
                    continue;
                duk_push_object(ctx);
                duk_push_int(ctx, priority);
                duk_put_prop_string(ctx, -2, "priority");
                duk_push_string(ctx, dname);
                duk_put_prop_string(ctx, -2, "exchange");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 5:  /* CNAME */
            case 2:  /* NS */
            case 12: /* PTR */ {
                if (ns_name_uncompress(ns_msg_base(msg), ns_msg_end(msg),
                        ns_rr_rdata(rr), dname, sizeof(dname)) < 0)
                    continue;
                duk_push_string(ctx, dname);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 6: /* SOA */ {
                const unsigned char *rdata = ns_rr_rdata(rr);
                const unsigned char *eom = ns_msg_end(msg);
                const unsigned char *base = ns_msg_base(msg);
                char mname[NS_MAXDNAME], rname[NS_MAXDNAME];
                int n;

                n = ns_name_uncompress(base, eom, rdata, mname, sizeof(mname));
                if (n < 0) continue;
                rdata += n;
                n = ns_name_uncompress(base, eom, rdata, rname, sizeof(rname));
                if (n < 0) continue;
                rdata += n;
                /* 5 x 32-bit integers: serial, refresh, retry, expire, minimum */
                duk_push_object(ctx);
                duk_push_string(ctx, mname);
                duk_put_prop_string(ctx, -2, "mname");
                duk_push_string(ctx, rname);
                duk_put_prop_string(ctx, -2, "rname");
                duk_push_uint(ctx, ns_get32(rdata));
                duk_put_prop_string(ctx, -2, "serial");
                duk_push_uint(ctx, ns_get32(rdata + 4));
                duk_put_prop_string(ctx, -2, "refresh");
                duk_push_uint(ctx, ns_get32(rdata + 8));
                duk_put_prop_string(ctx, -2, "retry");
                duk_push_uint(ctx, ns_get32(rdata + 12));
                duk_put_prop_string(ctx, -2, "expire");
                duk_push_uint(ctx, ns_get32(rdata + 16));
                duk_put_prop_string(ctx, -2, "minimum");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 33: /* SRV */ {
                const unsigned char *rdata = ns_rr_rdata(rr);
                uint16_t priority = ns_get16(rdata);
                uint16_t weight   = ns_get16(rdata + 2);
                uint16_t port     = ns_get16(rdata + 4);
                if (ns_name_uncompress(ns_msg_base(msg), ns_msg_end(msg),
                        rdata + 6, dname, sizeof(dname)) < 0)
                    continue;
                duk_push_object(ctx);
                duk_push_int(ctx, priority);
                duk_put_prop_string(ctx, -2, "priority");
                duk_push_int(ctx, weight);
                duk_put_prop_string(ctx, -2, "weight");
                duk_push_int(ctx, port);
                duk_put_prop_string(ctx, -2, "port");
                duk_push_string(ctx, dname);
                duk_put_prop_string(ctx, -2, "target");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 1: /* A */
                inet_ntop(AF_INET, ns_rr_rdata(rr), addrstr, sizeof(addrstr));
                duk_push_string(ctx, addrstr);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 28: /* AAAA */
                inet_ntop(AF_INET6, ns_rr_rdata(rr), addrstr, sizeof(addrstr));
                duk_push_string(ctx, addrstr);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
        }
    }

    duk_put_prop_string(ctx, obj_idx, "records");
    return 1;
}

#else /* __CYGWIN__ */

/* Minimal Windows DNS API definitions for DnsQuery_A / DnsRecordListFree.
   Follows the same pattern as CYGWIN_FIXED_INFO in rampart-thread.c.
   IMPORTANT: Use 'unsigned int' (4 bytes) for Windows DWORD fields, not
   'unsigned long' which is 8 bytes on 64-bit Cygwin (LP64 vs Windows LLP64). */

#define CYGWIN_DNS_FREE_RECORD_LIST 1

typedef struct _CYGWIN_DNS_RECORD {
    struct _CYGWIN_DNS_RECORD *pNext;
    char  *pName;
    unsigned short wType;
    unsigned short wDataLength;
    unsigned int   Flags;
    unsigned int   dwTtl;
    unsigned int   dwReserved;
    union {
        struct { unsigned int IpAddress; } A;
        struct { char *pNameHost; } PTR; /* NS, CNAME, PTR share this layout */
        struct { char *pNamePrimaryServer; char *pNameAdministrator;
                 unsigned int dwSerialNo; unsigned int dwRefresh;
                 unsigned int dwRetry; unsigned int dwExpire;
                 unsigned int dwDefaultTtl; } SOA;
        struct { char *pNameExchange; unsigned short wPreference;
                 unsigned short Pad; } MX;
        struct { unsigned int dwStringCount; char *pStringArray[1]; } TXT;
        struct { char *pNameTarget; unsigned short wPriority;
                 unsigned short wWeight; unsigned short wPort;
                 unsigned short Pad; } SRV;
        struct { unsigned char Ip6Address[16]; } AAAA;
    } Data;
} CYGWIN_DNS_RECORD;

typedef int (*cygwin_DnsQuery_A_fn)(const char *, unsigned short,
    unsigned int, void *, CYGWIN_DNS_RECORD **, void *);
typedef void (*cygwin_DnsRecordListFree_fn)(CYGWIN_DNS_RECORD *, int);

static int push_resolve_type(duk_context *ctx, const char *hn, int dns_type, const char *type_str)
{
    void *lib;
    cygwin_DnsQuery_A_fn queryFn;
    cygwin_DnsRecordListFree_fn freeFn;
    CYGWIN_DNS_RECORD *results = NULL, *rec;
    int status;
    duk_idx_t obj_idx, arr_idx;
    char addrstr[INET6_ADDRSTRLEN];

    lib = dlopen("dnsapi.dll", RTLD_LAZY);
    if (!lib) {
        duk_push_object(ctx);
        duk_push_string(ctx, "Failed to load dnsapi.dll");
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    queryFn = (cygwin_DnsQuery_A_fn)dlsym(lib, "DnsQuery_A");
    freeFn = (cygwin_DnsRecordListFree_fn)dlsym(lib, "DnsRecordListFree");
    if (!queryFn || !freeFn) {
        dlclose(lib);
        duk_push_object(ctx);
        duk_push_string(ctx, "Failed to resolve DNS API functions");
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    /* DNS_QUERY_STANDARD = 0 */
    status = queryFn(hn, (unsigned short)dns_type, 0, NULL, &results, NULL);
    if (status != 0) {
        if (results) freeFn(results, CYGWIN_DNS_FREE_RECORD_LIST);
        dlclose(lib);
        duk_push_object(ctx);
        duk_push_sprintf(ctx, "DNS query failed (status %d)", status);
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    duk_push_object(ctx);
    obj_idx = duk_get_top_index(ctx);
    duk_push_string(ctx, hn);
    duk_put_prop_string(ctx, obj_idx, "host");
    duk_push_string(ctx, type_str);
    duk_put_prop_string(ctx, obj_idx, "type");
    duk_push_array(ctx);
    arr_idx = duk_get_top_index(ctx);

    for (rec = results; rec; rec = rec->pNext) {
        if (rec->wType != dns_type) continue;

        switch (dns_type) {
            case 16: /* TXT */ {
                unsigned int j;
                duk_push_string(ctx, "");
                for (j = 0; j < rec->Data.TXT.dwStringCount; j++) {
                    duk_push_string(ctx, rec->Data.TXT.pStringArray[j]);
                    duk_concat(ctx, 2);
                }
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            }
            case 15: /* MX */
                duk_push_object(ctx);
                duk_push_int(ctx, rec->Data.MX.wPreference);
                duk_put_prop_string(ctx, -2, "priority");
                duk_push_string(ctx, rec->Data.MX.pNameExchange);
                duk_put_prop_string(ctx, -2, "exchange");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 5:  /* CNAME */
            case 2:  /* NS */
            case 12: /* PTR */
                duk_push_string(ctx, rec->Data.PTR.pNameHost);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 6: /* SOA */
                duk_push_object(ctx);
                duk_push_string(ctx, rec->Data.SOA.pNamePrimaryServer);
                duk_put_prop_string(ctx, -2, "mname");
                duk_push_string(ctx, rec->Data.SOA.pNameAdministrator);
                duk_put_prop_string(ctx, -2, "rname");
                duk_push_uint(ctx, rec->Data.SOA.dwSerialNo);
                duk_put_prop_string(ctx, -2, "serial");
                duk_push_uint(ctx, rec->Data.SOA.dwRefresh);
                duk_put_prop_string(ctx, -2, "refresh");
                duk_push_uint(ctx, rec->Data.SOA.dwRetry);
                duk_put_prop_string(ctx, -2, "retry");
                duk_push_uint(ctx, rec->Data.SOA.dwExpire);
                duk_put_prop_string(ctx, -2, "expire");
                duk_push_uint(ctx, rec->Data.SOA.dwDefaultTtl);
                duk_put_prop_string(ctx, -2, "minimum");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 33: /* SRV */
                duk_push_object(ctx);
                duk_push_int(ctx, rec->Data.SRV.wPriority);
                duk_put_prop_string(ctx, -2, "priority");
                duk_push_int(ctx, rec->Data.SRV.wWeight);
                duk_put_prop_string(ctx, -2, "weight");
                duk_push_int(ctx, rec->Data.SRV.wPort);
                duk_put_prop_string(ctx, -2, "port");
                duk_push_string(ctx, rec->Data.SRV.pNameTarget);
                duk_put_prop_string(ctx, -2, "target");
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 1: /* A */
                inet_ntop(AF_INET, &rec->Data.A.IpAddress, addrstr, sizeof(addrstr));
                duk_push_string(ctx, addrstr);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
            case 28: /* AAAA */
                inet_ntop(AF_INET6, rec->Data.AAAA.Ip6Address, addrstr, sizeof(addrstr));
                duk_push_string(ctx, addrstr);
                duk_put_prop_index(ctx, arr_idx,
                    (duk_uarridx_t)duk_get_length(ctx, arr_idx));
                break;
        }
    }

    duk_put_prop_string(ctx, obj_idx, "records");

    freeFn(results, CYGWIN_DNS_FREE_RECORD_LIST);
    dlclose(lib);
    return 1;
}

#endif /* __CYGWIN__ */


duk_ret_t duk_rp_net_resolve(duk_context *ctx)
{
    const char *hn = REQUIRE_STRING(ctx, 0, "resolve: first argument must be a String (hostname)");
    const char *type_str = NULL;
    int dns_type;

    if (duk_is_string(ctx, 1))
        type_str = duk_get_string(ctx, 1);

    /* no type -> use existing getaddrinfo path (returns full address object) */
    if (!type_str)
    {
        push_resolve(ctx, hn);
        return 1;
    }

    /* explicit type -> consistent {host, type, records} format */
    dns_type = dns_type_from_string(type_str);
    if (dns_type < 0)
        RP_THROW(ctx, "resolve: unknown record type '%s'", type_str);

    push_resolve_type(ctx, hn, dns_type, type_str);
    return 1;
}

/* ********* resolve_async ***************** */

#define DNS_CB_ARGS struct dns_callback_arguments_s

DNS_CB_ARGS {
    struct evutil_addrinfo *addr;
    char *host;
    uint8_t freebase;
};

static int lookup_callback(void *arg, int after)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    DNS_CB_ARGS *dnsargs = (DNS_CB_ARGS *) sinfo->aux;
    duk_context *ctx = sinfo->ctx;

    if(after)
        return 0;

    duk_push_heapptr(ctx, sinfo->thisptr); //this

    push_addrinfo(ctx, dnsargs->addr, dnsargs->host, 0);
    duk_put_prop_string(ctx, -2, "_hostAddrs");

    if(dnsargs->freebase)
        evdns_base_free(sinfo->dnsbase, 0);

    free(dnsargs->host);
    sinfo->aux=NULL;
    free(dnsargs);

    duk_get_prop_string(ctx, -1, "_hostAddrs");
    do_callback(ctx, "lookup", 1);
    if(sinfo->post_dns_cb)
        (void)(sinfo->post_dns_cb)(sinfo, 0);
    return 0;
}

static void async_dns_callback(int errcode, struct evutil_addrinfo *addr, void *arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;
    DNS_CB_ARGS *dnsargs = (DNS_CB_ARGS *) sinfo->aux;
    duk_idx_t top=duk_get_top(ctx);

    if(errcode)
    {
        duk_push_heapptr(ctx, sinfo->thisptr); //this
        free(dnsargs->host);
        if(dnsargs->freebase && sinfo && sinfo->dnsbase)
            evdns_base_free(sinfo->dnsbase, 0);
        sinfo->aux=NULL;
        free(dnsargs);
        duk_push_string(ctx, evutil_gai_strerror(errcode) );
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
    }
    else
    {
        // resolver.on("lookup") might be set after resolver.resolve()
        // and since this might not be in the event loop (see evdns_getaddrinfo)
        // we need to put it in so it will be run after lookup is set.

        dnsargs->addr = addr;
        duk_rp_insert_timeout(ctx, 0, "resolve", lookup_callback, arg,
            DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

    }
    duk_set_top(ctx,top);
}

static void async_resolve(duk_context *ctx, RPSOCK *sinfo, const char *hn, const char *server)
{
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

    REMALLOC(dnsargs, sizeof(DNS_CB_ARGS));
    dnsargs->host = strdup(hn);
    dnsargs->addr = NULL;
    sinfo->aux = (void *)dnsargs;

    if(server) {
        sinfo->dnsbase = evdns_base_new(sinfo->base,
            EVDNS_BASE_DISABLE_WHEN_INACTIVE);
        if(evdns_base_nameserver_ip_add(sinfo->dnsbase, server) != 0)
        {
            free(dnsargs->host);
            free(dnsargs);
            RP_THROW(ctx, "resolve: failed to configure nameserver");
        }
        dnsargs->freebase=1;
    } else
    // not using thread dnsbase
    if(!sinfo->dnsbase)
    {
#ifdef __CYGWIN__
        sinfo->dnsbase = rp_cygwin_new_dnsbase(sinfo->base);
#else
        sinfo->dnsbase = evdns_base_new(sinfo->base,
            EVDNS_BASE_DISABLE_WHEN_INACTIVE|EVDNS_BASE_INITIALIZE_NAMESERVERS );
#endif
        dnsargs->freebase=1;
    }
    else
        dnsargs->freebase=0;
//    req =
    // async_dns_callback may be run directly
    // or run in event loop.

    evdns_getaddrinfo(
            sinfo->dnsbase, hn, NULL,
            &hints, async_dns_callback, sinfo);
}


static int resolver_callback(void *arg, int unused)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;

    socket_cleanup(ctx, sinfo, WITH_CALLBACKS);

    return 0;
}


static duk_ret_t duk_rp_net_resolver_resolve(duk_context *ctx)
{
    RPSOCK *args = NULL;
    const char *server=NULL, *host = REQUIRE_STRING(ctx, 0, "net.resolve: first argument must be a string");
    duk_idx_t func_idx=1;

    duk_push_this(ctx);

    args = new_sockinfo(ctx);

    args->post_dns_cb = resolver_callback; // for cleanup

    if(duk_is_string(ctx, 1)) {
        func_idx=2;
        server = duk_get_string(ctx, 1);
    }

    if(duk_is_function(ctx, func_idx))
    {
        duk_rp_net_on(ctx, "resolve_async", "lookup", func_idx, 3); // run callback as "lookup" event
    }

    async_resolve(ctx, args, host, server);

    return 1; // this on top
}

/* this is basically something like:

  net.resolve_async = function(host, callback) {
      var resolver = new net.Resolver();
      resolver.resolve(host, callback);
  }
  where callback is function(hostinfo){}

   It is necessary to have a constructor in order to work with
   the dns functions above.
*/

static duk_ret_t duk_rp_net_resolve_async(duk_context *ctx)
{
    if(!duk_is_string(ctx, 0))
        RP_THROW(ctx, "resolve_async: first argument must be a String (hostname)");

    if(!duk_is_function(ctx, 1) && !duk_is_string(ctx, 1))
        RP_THROW(ctx, "resolve_async: second argument must be a String (Server) or Function (callback)");

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "Resolver");

    duk_new(ctx, 0); // var r = new resolver();
    duk_push_string(ctx, "resolve"); // r.resolve
    duk_dup(ctx,0); // host
    duk_dup(ctx,1); // function or server
    duk_dup(ctx,2); // function or undefined
    duk_call_prop(ctx, -5, 3); // r.resolve(host, server, function(){})
    return 1;
}


/* *********** reverse async ***************** */

static int push_reverse(duk_context *ctx, const char *hn)
{
    struct addrinfo hints, *res=NULL;
    int ecode;
    char hbuf[NI_MAXHOST];
    struct sockaddr *sa;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;


    ecode = getaddrinfo (hn, NULL, &hints, &res);
    if (ecode != 0)
    {
        if(res)
            freeaddrinfo(res);
        duk_push_object(ctx);
        duk_push_string(ctx, gai_strerror(ecode));
        duk_put_prop_string(ctx, -2, "errMsg");
        return 0;
    }

    sa = res->ai_addr;

    if (getnameinfo(sa, res->ai_addrlen, hbuf, sizeof(hbuf), NULL, 0, NI_NAMEREQD)) 
    {
        freeaddrinfo(res);
        duk_push_null(ctx);
        return 0;
    }

    // bug fix: use separate res2 variable to avoid clobbering original res pointer - 2026-02-27
    struct addrinfo *res2 = NULL;
    if (getaddrinfo(hbuf, "0", &hints, &res2) == 0) {
        /* malicious PTR record */
        freeaddrinfo(res2);
        freeaddrinfo(res);
        duk_push_null(ctx);
        return 0;
    }

    freeaddrinfo(res);
    duk_push_string(ctx, hbuf);

    return 1;
}

duk_ret_t duk_rp_net_reverse(duk_context *ctx)
{
    push_reverse(ctx,
        REQUIRE_STRING(ctx, 0, "reverse: argument must be a String") );
    return 1;
}

static void async_dns_rev_callback(int errcode, char type, int count, int ttl, void *addresses, void *arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;

    duk_push_heapptr(ctx, sinfo->thisptr); //this
    if(errcode)
    {
        if(sinfo->aux)
            evdns_base_free(sinfo->dnsbase, 0);
        sinfo->aux=NULL;
        duk_push_string(ctx, evdns_err_to_string(errcode) );
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        return;
    }

    if(type != DNS_PTR)
    {
        duk_push_string(ctx, "Unexpected results for reverse lookup, not a dns ptr record");
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        return;
    }

    duk_push_string(ctx, ((const char **) addresses)[0]);
    do_callback(ctx, "lookup", 1);
    socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
}

static void async_reverse(RPSOCK *sinfo, duk_context *ctx, struct addrinfo *res, const char *server)
{

    if(server)
    {
        sinfo->dnsbase = evdns_base_new(sinfo->base,
            EVDNS_BASE_DISABLE_WHEN_INACTIVE);
        if(evdns_base_nameserver_ip_add(sinfo->dnsbase, server) != 0)
        {
            RP_THROW(ctx, "reverse: failed to configure nameserver");
        }
        //just a flag here
        sinfo->aux=(void*)1;
    } 
    else
    // not using thread dnsbase
    if(!sinfo->dnsbase)
    {
#ifdef __CYGWIN__
        sinfo->dnsbase = rp_cygwin_new_dnsbase(sinfo->base);
#else
        sinfo->dnsbase = evdns_base_new(sinfo->base,
            EVDNS_BASE_DISABLE_WHEN_INACTIVE|EVDNS_BASE_INITIALIZE_NAMESERVERS );
#endif
        //just a flag here
        sinfo->aux=(void*)1;
    }

    if(res->ai_family == AF_INET)
        evdns_base_resolve_reverse(sinfo->dnsbase, &((struct sockaddr_in *) res->ai_addr)->sin_addr, 0, async_dns_rev_callback, sinfo);
    else
        evdns_base_resolve_reverse_ipv6(sinfo->dnsbase, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, 0, async_dns_rev_callback, sinfo);

    freeaddrinfo(res);
}


static duk_ret_t duk_rp_net_resolver_reverse(duk_context *ctx)
{
    RPSOCK *args = NULL;
    const char *server=NULL,
               *host = REQUIRE_STRING(ctx, 0, "net.reverse: first argument must be a string");
    duk_idx_t func_idx=1;
    struct addrinfo hints, *res=NULL;
    int ecode;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;

    duk_push_this(ctx);

    ecode = getaddrinfo (host, NULL, &hints, &res);
    if (ecode != 0)
    {
        if(res)
            freeaddrinfo(res);
        // we aren't async at this point, check for callback first
        duk_get_prop_string(ctx, -1, "_events");
        if(!duk_has_prop_string(ctx, -1, "error"))
            RP_THROW(ctx, "resolve.reverse: %s", gai_strerror(ecode));

        duk_pop(ctx);            
        duk_push_string(ctx, gai_strerror(ecode));
        do_callback(ctx, "error", 1);
        return 1;
    }

    args = new_sockinfo(ctx);

    if(duk_is_string(ctx, 1))
    {
        server=duk_get_string(ctx, 1);
        func_idx=2;
    }

    if(duk_is_function(ctx, func_idx))
    {
        duk_rp_net_on(ctx, "resolve_async", "lookup", func_idx, 3); // run callback as "lookup" event
    }
    async_reverse(args, ctx, res, server);

    return 1; // this on top
}

static duk_ret_t duk_rp_net_reverse_async(duk_context *ctx)
{
    if(!duk_is_string(ctx, 0))
        RP_THROW(ctx, "resolve_async: first argument must be a String (hostname)");

    if(!duk_is_function(ctx, 1) && !duk_is_string(ctx, 1))
        RP_THROW(ctx, "resolve_async: second argument must be a String (server) or Function (callback)");

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "Resolver");

    duk_new(ctx, 0); // var r = new Resolver();
    duk_push_string(ctx, "reverse"); // r.reverse()
    duk_dup(ctx,0); // host
    duk_dup(ctx,1); // function or server
    duk_dup(ctx,2); // function or undefined
    duk_call_prop(ctx, -5, 3); // r.resolve(host, server, function(){})
    return 1;
}

/* *** new net.Resolver() *** */
static duk_ret_t duk_rp_net_resolver(duk_context *ctx)
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

// chicken & egg problem.  We are getting 'this' from sinfo
// but sinfo might be freed from eof and we cant tell that without 'this'.
// so we need another struct.
#define DESTROYINFO struct destroy_info_s

DESTROYINFO {
    void *thisptr;
    duk_context *ctx;
};

static int destroy_callback(void *arg, int after)
{
    DESTROYINFO *dinfo = (DESTROYINFO*)arg;
    RPSOCK *sinfo;

    duk_context *ctx = dinfo->ctx;

    duk_push_heapptr(ctx, dinfo->thisptr);

    if(duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dinfo") ))
    {
        duk_push_undefined(ctx);
        duk_set_finalizer(ctx, -2);
    }

    free(dinfo);
    dinfo=NULL;
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        duk_pop_2(ctx);
        return 0;
    }
    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    if(sinfo)
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);

    return 0;
}

static duk_ret_t finalize_dinfo(duk_context *ctx)
{
    DESTROYINFO *dinfo;
    RPSOCK *sinfo;

    duk_push_undefined(ctx);
    duk_set_finalizer(ctx, 0);
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dinfo") );
    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("dinfo") );
    dinfo=duk_get_pointer(ctx,-1);
    duk_pop(ctx);
    if(dinfo)
        free(dinfo);
    dinfo=NULL;

    if(!duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        return 0;
    }
    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    if(sinfo)
        free(sinfo);

    return 0;
}

static duk_ret_t socket_destroy(duk_context *ctx)
{
    RPSOCK *sinfo;
    DESTROYINFO *dinfo = NULL;

    duk_push_this(ctx);


    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        duk_pop(ctx);
        duk_push_true(ctx);
        duk_put_prop_string(ctx, -2, "destroyed");
        return 1; //this
    }

    sinfo = (RPSOCK *) duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    REMALLOC(dinfo, sizeof(DESTROYINFO));
    dinfo->ctx = sinfo->ctx;
    dinfo->thisptr = sinfo->thisptr;

    // we need a finalizer to free dinfo if we exit after socket.destroy() and before
    // destroy_callback

    duk_push_pointer(ctx, dinfo);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("dinfo") );
    duk_push_c_function(ctx, finalize_dinfo, 1);
    duk_set_finalizer(ctx, -2);

    // closing here will cutoff any pending writes if called from an event callback
    // so we insert the disconnect and cleanup into the event loop via destroy_callback().
    if(sinfo)
        duk_rp_insert_timeout(ctx, 0, "socket.destroy", destroy_callback, dinfo,
            DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

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
        duk_pop(ctx);
        duk_push_string(ctx, "Socket is not open");
        do_callback(ctx, "error", 1);
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

    errno=0;
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
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
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
    duk_idx_t top = duk_get_top(ctx);

    /*
    if (sinfo->ssl && !(events & BEV_EVENT_EOF)) {
        unsigned long sslerr;

        while ((sslerr = bufferevent_get_openssl_error(bev))) {
            printf("SSL ERROR %lu:%i:%s:%i:%s:%i:%s",
                sslerr,
                ERR_GET_REASON(sslerr),
                ERR_reason_error_string(sslerr),
                ERR_GET_LIB(sslerr),
                ERR_lib_error_string(sslerr),
                ERR_GET_FUNC(sslerr),
                ERR_func_error_string(sslerr));
        }
    }


    *
    printf("%p %p eventcb %s%s%s%s\n", arg, (void *)bev,
        events & BEV_EVENT_CONNECTED ? "connected" : "",
        events & BEV_EVENT_ERROR     ? "error"     : "",
        events & BEV_EVENT_TIMEOUT   ? "timeout"   : "",
        events & BEV_EVENT_EOF       ? "eof"       : "");
    //*/

    if (events & BEV_EVENT_CONNECTED)
    {
        int fd = bufferevent_getfd(bev);
        struct sockaddr_storage addr;
        socklen_t sz = sizeof(struct sockaddr_storage);
        char addrstr[INET6_ADDRSTRLEN];
        int localport, insecure=0;

        duk_push_heapptr(ctx, sinfo->thisptr);
        duk_push_pointer(ctx, sinfo);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );

        if(duk_get_prop_string(ctx, -1, "insecure"))
            insecure=duk_get_boolean(ctx, -1);
        duk_pop(ctx);

        /* count open connections for server */
        if(sinfo->parent)
        {
            RPSOCK *server = sinfo->parent;
            server->open_conn++;
            sinfo->open_conn=1; //mark as open, so we know to do sinfo->parent->open_conn-- above
            // ideally we'd like to catch all of these in listener_callback
            // before we do all the work to get here.
            // but given the async nature of things, too many can make it past
            // listener_callback before we increment below.
            // so we need to check here as well.
            if(server->max_conn!=0 && server->open_conn > server->max_conn)
            {
                //printf("in eventcb: %d > %d connections, closing\n", server->open_conn, server->max_conn);
                socket_cleanup(ctx, sinfo, SKIP_CALLBACKS);
                duk_set_top(ctx,top);
                return;
            }

        }
        else if (sinfo->ssl)
        {
            const char *ciphername=SSL_get_cipher_name(sinfo->ssl);

            if(ciphername)
            {
                duk_push_string(ctx, ciphername);
                duk_put_prop_string(ctx, -2, "sslCipher");
            }
            if(!insecure)
            {
                // as client we want to verify server by default
                X509 *peer;

                if ((peer = SSL_get_peer_certificate(sinfo->ssl)))
                {
                    long lerr = SSL_get_verify_result(sinfo->ssl);
                    //char buf[256];

                    if(lerr != X509_V_OK)
                    {
                        duk_push_string(ctx, X509_verify_cert_error_string(lerr));
                        do_callback(ctx, "error", 1);
                        X509_free(peer);
                        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
                        duk_set_top(ctx,top);
                        return;
                    }
                    //X509_NAME_oneline(X509_get_subject_name(peer), buf, 256);
                    //printf("peer = %s\n", buf);
                }
                else
                {
                    duk_push_string(ctx, "failed to retrieve peer certificate");
                    do_callback(ctx, "error", 1);
                    X509_free(peer);
                    socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
                    duk_set_top(ctx,top);
                    return;
                }
                X509_free(peer);
            }
        }
        /* set flags */
        duk_push_false(ctx);
        duk_put_prop_string(ctx, -2, "connecting");

        errno=0;
        /* set local host/port */
        if(getsockname(fd, (struct sockaddr *)&addr, &sz ))
        {
            //do error - this really shouldn't happen since libevent says we are connected
            //duk_dup(ctx, -1);
            duk_push_sprintf(ctx, "socket.connect: could not get local host/port - %s", strerror(errno));
            do_callback(ctx, "error", 1);
            socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
            duk_set_top(ctx,top);
            return;
        }
        else
        {

            duk_push_true(ctx);
            duk_put_prop_string(ctx, -2, "connected");

            duk_push_false(ctx);
            duk_put_prop_string(ctx, -2, "pending");

            duk_push_string(ctx, "open");
            duk_put_prop_string(ctx, -2, "readyState");

            if( addr.ss_family == AF_INET ) {
                struct sockaddr_in *sa = ( struct sockaddr_in *) &addr;

                inet_ntop (addr.ss_family, (void * ) &( ((struct sockaddr_in *)&addr)->sin_addr), addrstr, INET6_ADDRSTRLEN);
                localport = (int) ntohs(sa->sin_port);
            }
            else
            {
                struct sockaddr_in6 *sa = ( struct sockaddr_in6 *) &addr;

                inet_ntop (addr.ss_family, (void*) &(((struct sockaddr_in6*)&addr)->sin6_addr), addrstr, INET6_ADDRSTRLEN);
                localport = (int) ntohs(sa->sin6_port);
            }

            duk_push_string(ctx, addrstr);
            duk_put_prop_string(ctx, -2, "localAddress");
            duk_push_int(ctx, localport);
            duk_put_prop_string(ctx, -2, "localPort");
        }

        // sever initiated sockets won't have remoteAddress set

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
        duk_set_top(ctx,top);
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
            duk_set_top(ctx,top);
            return;
        }

    }

    if (events & BEV_EVENT_EOF)
    {
        duk_push_heapptr(ctx, sinfo->thisptr);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        do_callback(ctx, "end", 0);
        duk_set_top(ctx,top);
        return;
    }

    if( events & BEV_EVENT_ERROR)
    {
        const char *errstr="unknown error";
        unsigned long errnum=0;
        duk_push_heapptr(ctx, sinfo->thisptr);

        do {
            errnum = EVUTIL_SOCKET_ERROR();
            if(errnum)
            {
                errstr=evutil_socket_error_to_string(errnum);
                break;
            }
            if(sinfo->ssl)
            {
                //char errbuf[256];
                errnum = bufferevent_get_openssl_error(bev);
                /*
                while(errnum){
                ERR_error_string_n(errnum, errbuf, 256);
                printf("ERR: %s\n", errbuf); // ERR: error:00000005:lib(0):func(0):DH lib
                errnum = bufferevent_get_openssl_error(bev);
                }
                */
                /*  Need to do research on what is fatal and what is not.  Getting a 'DH lib' error cuz we didn't provide dhparam?
                    But why does connection appear to succeed until end, then we get this error?
                    And why with connection to google.com:443, but not with yahoo or others?
                    HEEEELLLLLP!!!!
                */
                if(errnum == 5) // errstr = "DH lib"
                {
                    //printf("ladies and gentlemen, this is error number 5\n");

                    //we actually get all the data and are disconnected by server as expected. So all is ok?
                    duk_push_heapptr(ctx, sinfo->thisptr);
                    socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
                    do_callback(ctx, "end", 0);
                    duk_set_top(ctx,top);
                    return;
                }
                if(errnum)
                {
                    errstr=ERR_reason_error_string(errnum);
                    break;
                }
            }

            if(errno)
            {
                errstr=strerror(errno);
                break;
            }
        } while(0);

        duk_push_string(ctx, errstr);//evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        do_callback(ctx, "error", 1);
        errno=0;
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        duk_set_top(ctx,top);
        return;
    }

    if ( events & BEV_EVENT_TIMEOUT )
    {
        sock_do_callback(sinfo, "timeout");
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        duk_set_top(ctx,top);
        return;
    }
    duk_set_top(ctx,top);
    return;
}

static void push_remote(duk_context *ctx, struct sockaddr *ai_addr, duk_idx_t this_idx)
{
    void *saddr=NULL;
    char addrstr[INET6_ADDRSTRLEN];
    int ipf=4, port=0;
    switch(ai_addr->sa_family)
    {
        case AF_INET:
            saddr = &((struct sockaddr_in *) ai_addr)->sin_addr;
            port = ntohs(((struct sockaddr_in *) ai_addr)->sin_port);
            break;
        case AF_INET6:
            ipf=6;
            port = ntohs(((struct sockaddr_in6 *) ai_addr)->sin6_port);
            saddr = &((struct sockaddr_in6 *) ai_addr)->sin6_addr;
            break;
    }
    inet_ntop (ai_addr->sa_family, saddr, addrstr, INET6_ADDRSTRLEN);
    duk_push_string(ctx, addrstr);
    duk_put_prop_string(ctx, this_idx, "remoteAddress");
    duk_push_int(ctx, port);
    duk_put_prop_string(ctx, this_idx, "remotePort");
    duk_push_sprintf(ctx, "IPv%d", ipf);
    duk_put_prop_string(ctx, this_idx, "remoteFamily");
}


static int make_sock_conn(void *arg, int after)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    struct addrinfo *res=NULL;
    duk_context *ctx = sinfo->ctx;
    int port=-1, err=0, family=0, tls=0;
    struct sockaddr   * sin;
    size_t sin_sz;
    duk_idx_t top = duk_get_top(ctx);
    unsigned long ssl_err=0;
    char *ssl_err_str=NULL;
    const char *hostname=NULL;

    duk_push_heapptr(ctx, sinfo->thisptr);

    if(duk_get_prop_string(ctx, -1, "_hostPort") )
        port = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "family") )
        family = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "tls") )
        tls = duk_get_boolean_default(ctx, -1, 0);
    duk_pop(ctx);

    // a provided hostname for ssl verification
    if(duk_get_prop_string(ctx, -1,"hostname"))
    {
        hostname = duk_get_string(ctx, -1);
    }
    duk_pop(ctx);

    // get ip address(es)
    if(duk_get_prop_string(ctx, -1,"_hostAddrs"))
    {
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("resolvep")) )
            res = (struct addrinfo *) duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        if(!hostname)
        {
            if(duk_get_prop_string(ctx, -1, "host"))
                hostname = duk_get_string(ctx, -1);
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);

    if(sinfo->fd > -1 && sinfo->parent==NULL)
    {
        //server stopped listening, info gone. reject connection
        duk_push_string(ctx, "connection after server shut down");
        do_callback(ctx, "error", 1);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        return 0;
    }

    if(tls)
    {
        int insecure=0;
        const char *ca_path      = NET_CA_PATH,
                   *ca_file      = rp_net_def_bundle;

        if(strlen(ca_path)==0)
            ca_path = NULL;
        if(strlen(ca_file)==0)
            ca_file = NULL;

        if(duk_get_prop_string(ctx, -1, "cacert"))
        {
            ca_file = duk_get_string(ctx, -1);
            ca_path = NULL;
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, -1, "capath"))
        {
            ca_path = duk_get_string(ctx, -1);
            ca_file = NULL;
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, -1, "insecure"))
            insecure=duk_get_boolean(ctx, -1);
        duk_pop(ctx);
        sinfo->bev = NULL;

        do {

            if(sinfo->fd < 0)
            {   //client connection

                if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("ssl_ctx")))
                    sinfo->ssl_ctx = duk_get_pointer(ctx, -1);
                duk_pop(ctx);

                if(!sinfo->ssl_ctx)
                {
                    sinfo->ssl_ctx = SSL_CTX_new(TLS_client_method());
                    if(!sinfo->ssl_ctx)
                    {
                        ssl_err=ERR_get_error();
                        break;
                    }
                    duk_push_pointer(ctx, sinfo->ssl_ctx);
                    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("ssl_ctx"));
                }

                if(!insecure)
                {   //secure client connecton
                    SSL_CTX_set_options(sinfo->ssl_ctx,SSL_OP_ALL);
                    //not needed
    //                SSL_CTX_set_verify(sinfo->ssl_ctx, SSL_VERIFY_NONE, NULL); // no immediate disconnect

                    if (SSL_CTX_load_verify_locations(sinfo->ssl_ctx, ca_file, ca_path) < 1)
                    {
                        if(SSL_CTX_load_verify_locations(sinfo->ssl_ctx, NULL, ca_path) < 1)
                        {
                            ssl_err_str="failed to load ca certificate path/file for ssl";
                            break;
                        }
                    }
                }
                sinfo->ssl = SSL_new(sinfo->ssl_ctx);
                if(!sinfo->ssl)
                {
                    ssl_err=ERR_get_error();
                    break;
                }

                if (!SSL_set_tlsext_host_name(sinfo->ssl, hostname))
                {
                    ssl_err_str="failed to set hostname for ssl verification";
                    break;
                }
                sinfo->bev = bufferevent_openssl_socket_new(
                    sinfo->base, sinfo->fd, sinfo->ssl, BUFFEREVENT_SSL_CONNECTING, BEV_OPT_CLOSE_ON_FREE
                );
            }
            else
            {
                //server connection, use parent's ssl_ctx
                sinfo->ssl = SSL_new(sinfo->parent->ssl_ctx);
                if(!sinfo->ssl)
                {
                    ssl_err=ERR_get_error();
                    break;
                }
                sinfo->bev = bufferevent_openssl_socket_new(
                    sinfo->base, sinfo->fd, sinfo->ssl, BUFFEREVENT_SSL_ACCEPTING, BEV_OPT_CLOSE_ON_FREE
                );
            }

        } while (0);
    }
    else //non tls
        sinfo->bev = bufferevent_socket_new(sinfo->base, sinfo->fd, BEV_OPT_CLOSE_ON_FREE);

    if(!sinfo->bev)
    {
        if(ssl_err_str)
            duk_push_string(ctx, ssl_err_str);
        else if(ssl_err)
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
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        return 0;
    }


    // we will only have a file descriptor at this point if the server is giving us a connection.
    // with client functions, fd is -1;
    if(sinfo->fd > -1)
    {
        // For server individual connection:
        struct sockaddr remote;
        socklen_t len = sizeof(struct sockaddr_in6);

        //behavior differs with ssl. sock_eventcb with BEV_EVENT_CONNECTED is called after this.
        if(sinfo->ssl)
        {
            bufferevent_enable(sinfo->bev, EV_READ);
            // Set up rest of connection in sock_eventcb
            bufferevent_setcb( sinfo->bev, NULL, NULL, sock_eventcb, sinfo);
        }
        else
        {
            //somehow without ssl, we are too late and connect event won't be triggered
            //so we need to call it manually.
            sock_eventcb(sinfo->bev, BEV_EVENT_CONNECTED, (void *)sinfo);
        }
        errno=0;
        if( !getpeername(sinfo->fd, &remote, &len) )
            push_remote(ctx, &remote, -2);
        /*
            not sure we should kill the connection just because we couldn't get the peer ip
            and not when/how that would happen anyway. Some research is needed.
        else
        {
            duk_push_sprintf(ctx, "socket.connect: Error getting peer address - %s", strerror(errno));
            do_callback(ctx, "error", 1);
            duk_set_top(ctx, top);
            socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
            return 0;
        }
        */
        return 0;
    }
    else
    {
        bufferevent_enable(sinfo->bev, EV_READ);
        // Set up rest of connection in sock_eventcb
        bufferevent_setcb( sinfo->bev, NULL, NULL, sock_eventcb, sinfo);
    }

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
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
        return 0;
    }

    sin = res->ai_addr;

    // add port to sin
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

    push_remote(ctx, res->ai_addr, -2);

    err = bufferevent_socket_connect(sinfo->bev, sin, sin_sz);

    if (err)
    {
        duk_push_sprintf(ctx, "socket.connect: could not connect - %s", strerror(errno));
        do_callback(ctx, "error", 1);
        duk_set_top(ctx, top);
        socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
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

static duk_ret_t duk_rp_net_socket_connect(duk_context *ctx)
{
    int port=0, family=0, fd=-1;
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

    if(duk_get_prop_string(ctx, -1, "readyState"))
    {
        duk_pop(ctx);
        duk_push_string(ctx,"socket already connected or connecting");
        do_callback(ctx, "error", 1);
        duk_push_this(ctx);
        return 1;
    }

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

        if(duk_get_prop_string(ctx, obj_idx, "fd"))
        {
            fd=duk_get_int_default(ctx, -1, -1);
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

        if(duk_get_prop_string(ctx, obj_idx, "timeout"))
        {
            double tout = duk_get_number_default(ctx, -1, 0.0);
            if(tout<=0.0)
                RP_THROW(ctx, "socket.connect: option 'timeout' must be a Number > 0");
            duk_put_prop_string(ctx, tidx, "timeout");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "ssl"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'ssl' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "tls");
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

        if(duk_get_prop_string(ctx, obj_idx, "insecure"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'insecure' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "insecure");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "cacert"))
        {
            if(!duk_is_string(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'cacert' must be a String");
            duk_put_prop_string(ctx, tidx, "cacert");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "capath"))
        {
            if(!duk_is_string(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'capath' must be a String");
            duk_put_prop_string(ctx, tidx, "capath");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "hostname"))
        {
            if(!duk_is_string(ctx, -1))
                RP_THROW(ctx, "socket.connect: option 'hostname' must be a String");
            duk_put_prop_string(ctx, tidx, "hostname");
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

    if(!port && fd==-1)
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

    if(conncb_idx!=DUK_INVALID_INDEX)
    {
        duk_dup(ctx, conncb_idx);
        duk_rp_net_on(ctx, "socket.connect", "connect", -1, tidx);
    }

    args = new_sockinfo(ctx);
    if(fd < 0)
    {
        duk_push_int(ctx,port);
        duk_put_prop_string(ctx, tidx, "_hostPort");

        args->post_dns_cb = make_sock_conn;
        async_resolve(ctx, args, host, NULL);
    }
    else
    {
        args->fd=fd;
        duk_rp_insert_timeout(ctx, 0, "socket.connect", make_sock_conn, args,
            DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

        // attach server_sinfo to connection_sinfo->parent
        if(duk_get_prop_string(ctx, tidx, "Server"))
        {
            if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")))
            {
                RPSOCK *parent = duk_get_pointer(ctx, -1);
                args->parent=parent;
            }
            duk_pop(ctx);
        }
        duk_pop(ctx);
    }
    duk_pull(ctx, tidx);
    duk_push_string(ctx, "opening");
    duk_put_prop_string(ctx, -2, "readyState");

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "connecting");

    return 1;
}

static duk_ret_t duk_rp_net_socket(duk_context *ctx)
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

static duk_ret_t net_create_connection(duk_context *ctx)
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

/* *************************** net.Server() ******************************* */

static duk_ret_t duk_rp_net_connection_count(duk_context *ctx)
{
    RPSOCK *sinfo = NULL;

    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        sinfo = duk_get_pointer(ctx, -1);
        duk_push_number(ctx, (double)sinfo->open_conn);
    }
    else
        duk_push_number(ctx, 0.0);

    return 1;
}

static duk_ret_t duk_rp_net_max_connections(duk_context *ctx)
{
    RPSOCK *sinfo = NULL;
    double maxconn = 0;

    if(!duk_is_undefined(ctx, 0))
        maxconn = REQUIRE_NUMBER(ctx, 0, "server.maxConnections: First argument must be an integer");

    if(maxconn < 0 || maxconn > UINT32_MAX)
        maxconn=0;

    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        sinfo = duk_get_pointer(ctx, -1);
        if(sinfo) //should always be non-NULL
            sinfo->max_conn=(uint32_t)maxconn;
    }
    duk_pop(ctx);

    duk_push_number(ctx, maxconn);
    duk_put_prop_string(ctx, -2, "maxConnections");

    return 0;
}

static duk_ret_t server_close(duk_context *ctx)
{
    RPSOCK *sinfo = NULL;

    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sinfo")) )
    {
        sinfo = duk_get_pointer(ctx, -1);
    }
    else
        RP_THROW(ctx, "server.close: internal error retrieving socket info");

    duk_pop(ctx);

    if(duk_is_function(ctx, 0))
        duk_rp_net_on(ctx, "server.close", "close", 0/*func*/, 1/*this*/);

    socket_cleanup(ctx, sinfo, WITH_CALLBACKS);

    return 1; //this
}

static void listener_callback(struct evconnlistener *listener, int fd,
    struct sockaddr *addr, int addrlen, void *arg)
{

    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context * ctx = sinfo->ctx;
    duk_idx_t top = duk_get_top(ctx);

    // sinfo->max_conn == 0 means unlimited/not set
    if(sinfo->max_conn != 0 && sinfo->open_conn >= sinfo->max_conn)
    {
        //printf("%d > %d connections, closing\n", sinfo->open_conn, sinfo->max_conn);
        close(fd);
        return;
    }

    duk_push_heapptr(ctx, sinfo->thisptr);
    duk_get_prop_string(ctx, -1, "Socket");
    duk_new(ctx, 0); //new Socket()
    duk_dup(ctx, -2); //dup new Server()
    duk_put_prop_string(ctx, -2, "Server"); //save server 'this' in socket as .Server

    //copy server's tls value to new socket
    duk_get_prop_string(ctx, -2, "tls");
    duk_put_prop_string(ctx, -2, "tls");

    // run server's 'connection' callback
    duk_pull(ctx, -2);// Server 'this'
    duk_dup(ctx, -2);// pass connection socket as argument
    do_callback(ctx, "connection", 1); // run 'connection' callback(s);

    // run the socket.connect({fd:fd}) function to set up bufferevent for current connection
    duk_push_string(ctx, "connect");
    duk_push_object(ctx);
    duk_push_int(ctx, fd);
    duk_put_prop_string(ctx, -2, "fd");
    duk_call_prop(ctx, -3, 1);
    duk_set_top(ctx, top);
}


static void
accept_error_cb(struct evconnlistener *listener, void *arg)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;
    int err = EVUTIL_SOCKET_ERROR();

    duk_push_heapptr(ctx, sinfo->thisptr);
    duk_push_string(ctx, evutil_socket_error_to_string(err));

    do_callback(ctx, "error", 1);
}

static int listen_addrs(duk_context *ctx, RPSOCK *sinfo, int nlisteners, int port, int backlog, int family)
{
    struct addrinfo *res=NULL;
    unsigned int ipv6_flag=0;

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("resolvep")) )
    {
        res = (struct addrinfo *) duk_get_pointer(ctx, -1);
        duk_pop(ctx);
        for (; res ; res=res->ai_next)
        {
            if( res->ai_family == AF_INET )
            {
                if(family == 6)
                    continue;
                else
                {
                    struct sockaddr_in *sa = ( struct sockaddr_in *) res->ai_addr;
                    ipv6_flag=0;
                    sa->sin_port = htons(port);
                }
            }
            else if (res->ai_family == AF_INET6 )
            {
                if(family == 4)
                    continue;
                else
                {
                    struct sockaddr_in6 *sa = ( struct sockaddr_in6 *) res->ai_addr;
                    ipv6_flag=LEV_OPT_BIND_IPV6ONLY;
                    sa->sin6_port = htons(port);
                }
            }
            else
                continue;

            REMALLOC(sinfo->listeners, sizeof(struct evconnlistener *) * (nlisteners+1));
            sinfo->listeners[nlisteners]=NULL;

            sinfo->listeners[nlisteners-1] = evconnlistener_new_bind(
                sinfo->base, listener_callback, sinfo, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE|ipv6_flag,
                backlog, res->ai_addr, (int)res->ai_addrlen
            );
            if(!sinfo->listeners[nlisteners-1])
            {
                //return nlisteners; // this kinda mimics node - but it is idiotic. server.listen(80,'google.com')
                                     // listens to nothing with node but remains in even in event loop
                const char *h;
                duk_get_prop_string(ctx, -1, "host");
                h=duk_get_string(ctx, -1);
                duk_pop(ctx);
                duk_push_heapptr(ctx, sinfo->thisptr);
                duk_push_sprintf(ctx, "socket.Server: could not listen on host:port %s:%d - %s", h, port, strerror(errno));
                do_callback(ctx, "error", 1);
                socket_cleanup(ctx, sinfo, WITH_CALLBACKS);
                return -1;
            }
            evconnlistener_set_error_cb(sinfo->listeners[nlisteners-1], accept_error_cb);

            nlisteners++;
        }
        return nlisteners;
    }
    else
    {
        duk_pop(ctx);
        return nlisteners;
    }
}


static int make_server(void *arg, int after)
{
    RPSOCK *sinfo = (RPSOCK *) arg;
    duk_context *ctx = sinfo->ctx;
    int port=-1,family=0;
    int nlisteners=1, backlog=511, len=0, i=0;;

    duk_push_heapptr(ctx, sinfo->thisptr);

    // get port
    if(duk_get_prop_string(ctx, -1, "_hostPort") )
        port = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "backlog") )
        backlog = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "family") )
        family = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "backlog") )
        backlog = duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, "maxConnections") )
        sinfo->max_conn = (uint32_t)duk_get_number(ctx, -1);
    duk_pop(ctx);

    //get ip address(es)
    if(duk_get_prop_string(ctx, -1,"_hostAddrs"))
    {
        len = duk_get_length(ctx, -1); //array length
        while (i<len)
        {
            duk_get_prop_index(ctx, -1, i);
            nlisteners = listen_addrs(ctx, sinfo, nlisteners, port, backlog, family);
            duk_pop(ctx);
            if(nlisteners == -1) //error binding addr:port
                return 0;
            i++;
        }
    }
    duk_pop(ctx);

    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "listening");

    do_callback(ctx, "listening", 0);

return 0;
}

#define MAX_BACKLOG 32767

static duk_ret_t duk_rp_net_server_listen(duk_context *ctx)
{
    int port=0, family=0, sslver=TLS1_2_VERSION, backlog=511;
    duk_bool_t tls=0;
    const char *host=NULL;
    duk_idx_t i=0, tidx, obj_idx=-1, hosts_idx=-1, listening_cb_idx=DUK_INVALID_INDEX;
    RPSOCK *args = NULL;

    while (i < 4)
    {
        if(duk_is_number(ctx,i))
        {
            // first number is port
            if(!port)
                socket_getport(port, i, "net.server");
            else
            // second number is backlog
            {
                backlog=duk_get_int(ctx,i);
                if(backlog < -1)
                    backlog = -1;
                else if (backlog > MAX_BACKLOG)
                    RP_THROW(ctx, "backlog must be an integer less that %d", MAX_BACKLOG);
            }
        }
        else if (duk_is_string(ctx,i))
        {
            if(hosts_idx != -1)
                RP_THROW(ctx, "net.server: multiple host entries, host parameter must be a String OR Array of Strings");
            host = duk_get_string(ctx, i);
        }
        else if (duk_is_array(ctx, i))
        {
            if(host != NULL)
                RP_THROW(ctx, "net.server: multiple host entries, host parameter must be a String OR Array of Strings");
            hosts_idx=i;
        }
        else if (duk_is_function(ctx, i))
            listening_cb_idx=i;
        else if (!duk_is_array(ctx, i) && duk_is_object(ctx, i) )
            obj_idx=i;
        else if (!duk_is_undefined(ctx, i))
            RP_THROW(ctx, "net.server: argument %d is invalid", (int)i+1);
        i++;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    if(obj_idx != -1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "port"))
        {
            socket_getport(port, -1, "net.server");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "backlog"))
        {
            backlog=duk_get_int(ctx,-1);
            if(backlog < -1)
                backlog = -1;
            else if (backlog > MAX_BACKLOG)
                RP_THROW(ctx, "backlog must be an integer less that %d", MAX_BACKLOG);
        }
        duk_pop(ctx);
        /*
        if(duk_get_prop_string(ctx, obj_idx, "keepAlive"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "net.server: option 'keepAlive' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "keepAlive");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "tls"))
        {
            if(!duk_is_boolean(ctx, -1))
                RP_THROW(ctx, "net.server: option 'tls' must be a Boolean");
            duk_put_prop_string(ctx, tidx, "tls");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInitialDelay"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "net.server: option 'keepAliveInitialDelay' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInitialDelay");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveInterval"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "net.server: option 'keepAliveInterval' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveInterval");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "keepAliveCount"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "net.server: option 'keepAliveCount' must be a Number");
            duk_put_prop_string(ctx, tidx, "keepAliveCount");
        }
        else
            duk_pop(ctx);
        */
        if(duk_get_prop_string(ctx, obj_idx, "family"))
        {
            if(!duk_is_number(ctx, -1))
                RP_THROW(ctx, "net.server: option 'family' must be a Number (0, 4 or 6)");

            family = duk_get_int_default(ctx, -1, 0);

            if(family != 0 && family != 6 && family != 4)
                RP_THROW(ctx, "net.server: option 'family' must be a Number (0, 4 or 6)");

            duk_put_prop_string(ctx, tidx, "family");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "maxConnections"))
        {
            double maxconn = REQUIRE_NUMBER(ctx, -1, "net.server: option 'maxConnections' must be a Number");

            if(maxconn < 1 || maxconn > UINT32_MAX)
                maxconn=0;

            duk_push_number(ctx, maxconn);
            duk_put_prop_string(ctx, tidx, "maxConnections");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "host"))
        {
            if( duk_is_string(ctx, -1) )
            {
                if(hosts_idx != -1 || host != NULL)
                    RP_THROW(ctx, "net.server: multiple host entries, host parameter must be a String OR Array of Strings");
                host = REQUIRE_STRING(ctx, -1, "net.server: option 'host' must be a String");
                duk_pop(ctx);
            }
            else if (duk_is_array(ctx, -1))
            {
                if(hosts_idx != -1 || host != NULL)
                    RP_THROW(ctx, "net.server: multiple host entries, host parameter must be a String OR Array of Strings");
                hosts_idx=duk_normalize_index(ctx, -1);
                //no pop
            }
            else
                RP_THROW(ctx, "net.server: host parameter must be a String OR Array of Strings");
        }
    }

    // get tls, which might have been in this previously or as set from object above
    if(duk_get_prop_string(ctx, tidx, "tls"))
    {
        tls=duk_get_boolean(ctx, -1);
    }
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("sslver")))
    {
        sslver=duk_get_int(ctx, -1);
    }
    duk_pop(ctx);

    if(!port)
        RP_THROW(ctx, "net.server: port must be specified");

    if(!host && hosts_idx==-1)
        host="any";

    if(host && !strcmp(host,"any"))
    {
        duk_uarridx_t j=0;
        duk_push_array(ctx);
        if(family != 6)
        {
            duk_push_string(ctx, "0.0.0.0");
            duk_put_prop_index(ctx, -2, j++);
        }
        if(family != 4)
        {
            duk_push_string(ctx, "::");
            duk_put_prop_index(ctx, -2, j);
        }
        hosts_idx=duk_get_top_index(ctx);
        host=NULL;
    }

    duk_push_array(ctx); //_hostAddrs

    if(host)
    {
        if(!push_resolve(ctx, host))
        {
            duk_dup(ctx, tidx);
            duk_get_prop_string(ctx, -2, "errMsg");
            do_callback(ctx, "error", 1);
            duk_pull(ctx, tidx);
            return 1;
        }
        if(family == 4 && !duk_has_prop_string(ctx, -1, "ipv4") )
            RP_THROW(ctx, "net.server: ipv4 specified but host '%s' has no ipv4 address", host);

        if(family == 6 && !duk_has_prop_string(ctx, -1, "ipv6") )
            RP_THROW(ctx, "net.server: ipv6 specified but host '%s' has no ipv6 address", host);
        duk_put_prop_index(ctx, -2, 0);
    }
    else //array
    {
        int idx=0, len = (int)duk_get_length(ctx, hosts_idx);

        while (idx < len)
        {
            duk_get_prop_index(ctx, hosts_idx, idx);
            host = REQUIRE_STRING(ctx, -1, "net.server: host parameter must be a String OR Array of Strings");
            duk_pop(ctx);

            // make localhost both v4 and v6
            if(strcmp("localhost", host)==0)
            {
                if(family == 4)
                    host="127.0.0.1";
                else if (family == 6)
                     host="::1";
                else
                {
                    duk_push_string(ctx, "::1");
                    duk_put_prop_index(ctx, hosts_idx, len);
                    len++;
                    host="127.0.0.1";
                }
            }

            if(!push_resolve(ctx, host))
            {
                duk_dup(ctx, tidx);
                duk_get_prop_string(ctx, -2, "errMsg");
                do_callback(ctx, "error", 1);
                duk_pull(ctx, tidx);
                return 1;
            }

            if(family == 4 && !duk_has_prop_string(ctx, -1, "ipv4") )
                RP_THROW(ctx, "net.server: ipv4 specified but host '%s' has no ipv4 address", host);

            if(family == 6 && !duk_has_prop_string(ctx, -1, "ipv6") )
                RP_THROW(ctx, "net.server: ipv6 specified but host '%s' has no ipv6 address", host);

            duk_put_prop_index(ctx, -2, idx);
            idx++;
        }
    }

    duk_put_prop_string(ctx, tidx, "_hostAddrs");

    duk_push_int(ctx,port);
    duk_put_prop_string(ctx, tidx, "_hostPort");

    duk_push_int(ctx, backlog);
    duk_put_prop_string(ctx, tidx, "backlog");

    if(listening_cb_idx!=DUK_INVALID_INDEX)
    {
        duk_rp_net_on(ctx, "server.listen", "listening", listening_cb_idx, tidx);
    }

    args = new_sockinfo(ctx);

    if(tls)
    {
        if (!RAND_poll())
            RP_THROW(ctx, "RAND_poll failed");
        args->ssl_ctx = SSL_CTX_new(TLS_server_method());
        if(!args->ssl_ctx)
        {
            unsigned long ssl_err=ERR_get_error();
            char errmsg[256];
            ERR_error_string(ssl_err, errmsg);
            duk_dup(ctx, tidx);
            duk_push_string(ctx, errmsg);
            do_callback(ctx, "error", 1);
            socket_cleanup(ctx, args, WITH_CALLBACKS);
            return 1;
        }

        SSL_CTX_set_min_proto_version(args->ssl_ctx, sslver);

        if(duk_get_prop_string(ctx, tidx, "sslCertFile"))
        {
            const char *cfile = duk_get_string(ctx, -1);

            if(!SSL_CTX_use_certificate_chain_file(args->ssl_ctx, cfile))
                RP_THROW(ctx, "error loading ssl cert file %s", cfile);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, tidx, "sslKeyFile"))
        {
            const char *kfile = duk_get_string(ctx, -1);

            if(!SSL_CTX_use_PrivateKey_file(args->ssl_ctx, kfile, SSL_FILETYPE_PEM))
                RP_THROW(ctx, "error loading ssl key file %s", kfile);
        }
        duk_pop(ctx);

        duk_push_true(ctx);
        duk_put_prop_string(ctx, tidx, "tls");

    }

    duk_rp_insert_timeout(ctx, 0, "net.server", make_server, args,
        DUK_INVALID_INDEX, DUK_INVALID_INDEX, 0);

    duk_pull(ctx, tidx);
    duk_push_pointer(ctx, args);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("sinfo") );

    return 1;

}

static duk_ret_t duk_rp_net_server(duk_context *ctx)
{
    duk_bool_t tls=0;
    duk_idx_t tidx, func_idx=-1, obj_idx=-1, i=0;
    struct stat f_stat;
    int version = TLS1_2_VERSION;

    /* allow call to net.Socket() with "new net.Socket()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    while(i<2)
    {
        if(duk_is_function(ctx, i))
            func_idx=i;
        else if(duk_is_object(ctx, i))
            obj_idx=i;
        else if(!duk_is_undefined(ctx, i))
            RP_THROW(ctx, "new Server: argument %d must be a Function (listening callback) or an Object (options)", i+1);
        i++;
    }

    // copy Socket to 'this'
    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, "Socket");
    duk_put_prop_string(ctx, -3, "Socket");
    duk_pop(ctx);

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "listening");

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, tidx, "_events");
    // get options:
    if(obj_idx>-1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "secure"))
        {
            tls = REQUIRE_BOOL(ctx, -1, "new Server: option 'secure' must be a Boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "tls"))
        {
            tls = REQUIRE_BOOL(ctx, -1, "new Server: option 'tls' must be a Boolean");
        }
        duk_pop(ctx);

        if(tls)
        {
            if(duk_get_prop_string(ctx, obj_idx, "sslKeyFile"))
            {
                const char *kfile = REQUIRE_STRING(ctx, -1, "new Server: option 'sslKeyFile' must be a String");

                if (stat(kfile, &f_stat) != 0)
                    RP_THROW(ctx, "server.start: Cannot load ssl key '%s' (%s)", kfile, strerror(errno));
                else
                {
                    FILE* file = fopen(kfile,"r");
                    // bug fix: added NULL check after fopen and fclose after use for SSL key validation - 2026-02-27
                    if(!file)
                        RP_THROW(ctx, "server.start: Cannot open ssl key '%s' (%s)", kfile, strerror(errno));
                    EVP_PKEY* pkey = PEM_read_PrivateKey(file, NULL, NULL, NULL);
                    unsigned long err = ERR_get_error();
                    fclose(file);

                    if(pkey)
                        EVP_PKEY_free(pkey);
                    if(err)
                    {
                        char tbuf[256];
                        ERR_error_string(err,tbuf);
                        RP_THROW(ctx, "Invalid ssl keyfile: %s", tbuf);
                    }
                }

                duk_put_prop_string(ctx, tidx, "sslKeyFile");
            }
            else
                duk_pop(ctx);

            if(duk_get_prop_string(ctx, obj_idx, "sslCertFile"))
            {
                const char *pfile = REQUIRE_STRING(ctx, -1, "new Server: option 'sslCertFile' must be a String");

                if (stat(pfile, &f_stat) != 0)
                    RP_THROW(ctx, "server.start: Cannot load ssl cert file '%s' (%s)", pfile, strerror(errno));
                else
                {
                    FILE* file = fopen(pfile,"r");
                    // bug fix: added NULL check after fopen and fclose after use for SSL cert validation - 2026-02-27
                    if(!file)
                        RP_THROW(ctx, "server.start: Cannot open ssl cert file '%s' (%s)", pfile, strerror(errno));
                    X509* x509 = PEM_read_X509(file, NULL, NULL, NULL);
                    unsigned long err = ERR_get_error();
                    fclose(file);

                    if(x509)
                        X509_free(x509);
                    if(err)
                    {
                        char tbuf[256];
                        ERR_error_string(err,tbuf);
                        RP_THROW(ctx,"Invalid ssl cert file: %s",tbuf);
                    }
                }
                duk_put_prop_string(ctx, tidx, "sslCertFile");
            }
            else
                duk_pop(ctx);

            if(duk_get_prop_string(ctx, obj_idx, "sslMinVersion"))
            {
                const char *sslver=REQUIRE_STRING(ctx, -1, "server.start: parameter \"sslMinVersion\" requires a string (ssl3|tls1|tls1.1|tls1.2)");
                if (!strcmp("tls1.2",sslver))
                    version =TLS1_2_VERSION;
                else if (!strcmp("tls1.1",sslver))
                    version =TLS1_1_VERSION;
                else if (!strcmp("tls1.0",sslver))
                    version =TLS1_VERSION;
                else if (!strcmp("tls1",sslver))
                    version =TLS1_VERSION;
                else if (!strcmp("ssl3",sslver))
                    version =SSL3_VERSION;
                else
                    RP_THROW(ctx, "server.start: parameter \"sslMinVersion\" must be ssl3, tls1, tls1.1 or tls1.2");
            }
            duk_pop(ctx);
        }
    }

    // put ssl version in 'this'
    duk_push_int(ctx, version);
    duk_put_prop_string(ctx, tidx, DUK_HIDDEN_SYMBOL("sslver"));

    // put tls setting in 'this'
    duk_push_boolean(ctx, tls);
    duk_put_prop_string(ctx, tidx, "tls");

    if(func_idx>-1) {
        duk_dup(ctx, tidx);
        duk_rp_net_on(ctx, "server.on", "connection", func_idx, -1);
    }


    return 0;
}

static duk_ret_t net_create_server(duk_context *ctx)
{
    duk_idx_t i=1, connl_idx=-1;

    // find connection listener function
    while(i<4)
    {
        if(duk_is_function(ctx, i))
        {
            connl_idx=i;
            break;
        }
        i++;
    }

    //get net.Server
    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, "Server");

    // [ arg1, arg2, arg3, arg4, curfunc, Server ]
    duk_remove(ctx , -2); // current function
    // [ arg1, arg2, arg3, arg4 Server ]


    // make new Server
    if(duk_is_object(ctx, 0))
    {
        duk_dup(ctx, 0);
        // [ arg1, arg2, arg3, arg4 Server, arg1_obj ]
        duk_new(ctx, 1);
    }
    else
        duk_new(ctx, 0);

    // [ arg1, arg2, arg3, arg4, new_server]

    // add listener to new_server
    if(connl_idx>0)
    {
        duk_rp_net_on(ctx, "createServer", "connection", connl_idx, -1);
        // we need to replace function with undefined so that if/when
        // we call listen() below, function isn't mistaken for "listening" callback
        duk_push_undefined(ctx);
        duk_replace(ctx, connl_idx);
    }

    //net.createServer(options[, connectionListener])
    //     or
    // net.createServer(port[, host[, backlog]] [, connectListener])
    // if we have port, just do listen now -- NOTE: according to docs, node doesn't do this
    if( ( duk_is_object(ctx,0) && duk_has_prop_string(ctx,0,"port") )
                ||
        duk_is_number(ctx,0)
      )
    {
        // [ arg1, arg2, arg3, arg4, new_server]
        duk_push_string(ctx, "listen");
        // [ arg1, arg2, arg3, arg4, new_server, "listen" ]
        duk_insert(ctx, 0);
        duk_insert(ctx, 0);
        // [ new_server, "listen", arg1, arg2, arg3, arg4 ]
        duk_call_prop(ctx, 0, 4);
        // [ new_server, retval ]
        duk_pop(ctx);
        // [ new_server ]
    }
    else if (!duk_is_object(ctx,0) && !duk_is_undefined(ctx,0) )
        RP_THROW(ctx, "net.createServer: first parameter must be a number (port) or object (options)");

    return 1;
}

/* *****************************************  udp_socket ********************************************** *
Coming soon.  Probably should follow node model and redo below.
static duk_ret_t duk_rp_net_udp_socket(duk_context *ctx)
{
    duk_idx_t tidx;
    int port = 0, family=0;
    const char *host = "127.0.0.1";

    // allow call to net.Socket() with "new net.Socket()" only
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);
    tidx = duk_get_top_index(ctx);

    duk_push_false(ctx);
    duk_put_prop_string(ctx, -2, "bound");

    //object to hold event callbacks
    duk_push_object(ctx);
    duk_put_prop_string(ctx, tidx, "_events");

    if(duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "host"))
        {
            host = REQUIRE_STRING(ctx, -1, "socket.connect: option 'host' must be a String");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "port"))
        {
            socket_getport(port, -1, "socket.connect");
            duk_put_prop_string(ctx, tidx, "port");
        }
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "family"))
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

}

*/


static duk_ret_t net_finalizer(duk_context *ctx)
{
    SSL_CTX *ssl_ctx;

    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("ssl_ctx"));
    ssl_ctx = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(ssl_ctx)
        SSL_CTX_free(ssl_ctx);
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("ssl_ctx"));
    duk_pop(ctx);

    return 0;
}

static duk_ret_t setbundle(duk_context *ctx)
{
    const char *bundle = REQUIRE_STRING(ctx, 0, "net.setCaCert - argument must be a string");

    if(access(bundle, R_OK) != 0)
        RP_THROW(ctx, "net.setCaCert - Setting '%s': %s\n", bundle, strerror(errno));

    // store it where it won't be freed
    duk_push_this(ctx);
    duk_push_string(ctx, "default_ca_file");
    rp_net_def_bundle = (char *) duk_push_string(ctx, bundle);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE |DUK_DEFPROP_CLEAR_WEC|DUK_DEFPROP_FORCE);

    return 0;
}


/* INIT MODULE */
duk_ret_t duk_open_module(duk_context *ctx)
{

    // if the default is there
    if(access(NET_CA_FILE, R_OK) == 0)
        //set it to default
        rp_net_def_bundle = NET_CA_FILE;
    else 
        //set it to the one we found in rampart-utils.c
        rp_net_def_bundle = rp_ca_bundle; 

    /* return object */
    duk_push_object(ctx);

    duk_push_string(ctx, "default_ca_file");
    duk_push_string(ctx, rp_net_def_bundle);
    duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE |DUK_DEFPROP_CLEAR_WEC);

    /* net.setCaCert */
    duk_push_c_function(ctx, setbundle, 1);
    duk_put_prop_string(ctx, -2, "setCaCert");
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
        duk_push_c_function(ctx, socket_write, 1);
        duk_put_prop_string(ctx, -2, "write");

        // socket.on()
        duk_push_c_function(ctx, duk_rp_net_x_on, 2);
        duk_put_prop_string(ctx, -2, "on");

        duk_push_c_function(ctx, duk_rp_net_x_once, 2);
        duk_put_prop_string(ctx, -2, "once");

        duk_push_c_function(ctx, duk_rp_net_x_off, 2);
        duk_put_prop_string(ctx, -2, "off");

        // socket.trigger()
        duk_push_c_function(ctx, duk_rp_net_x_trigger, 2);
        duk_put_prop_string(ctx, -2, "trigger");

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
    duk_dup(ctx, -1); //copy of Socket for createConnection below
    duk_dup(ctx, -1); //copy of Socket for Server below
    //[ net, Socket, Socket, Socket ]
    duk_put_prop_string(ctx, -4, "Socket");

    // net.createConnection & net.connect alias
    duk_push_c_function(ctx, net_create_connection, 3);
    // [ net, Socket, Socket, connect ]
    duk_pull(ctx, -2); //Socket reference
    // [ net, Socket, connect, Socket ]
    duk_put_prop_string(ctx, -2, "Socket"); //save Socket as property of function
    // [ net, Socket, connect ]
    duk_dup(ctx, -1);  //make a copy and save under both connect and createConnection
    // [ net, Socket, connect, connect ]
    duk_put_prop_string(ctx, -4, "connect");
    // [ net, Socket, connect ]
    duk_put_prop_string(ctx, -3, "createConnection");
    // [ net, Socket ]

    //net.Server
    duk_push_c_function(ctx, duk_rp_net_server, 2);
    // [ net, Socket, Server ]
    duk_pull(ctx, -2); //Socket reference
    // [ net, Server, Socket ]
    duk_put_prop_string(ctx, -2, "Socket"); //save Socket as property of function
    // [net, Server ]
    duk_push_object(ctx);
    //server methods:

        // server.listen()
        duk_push_c_function(ctx, duk_rp_net_server_listen, 4);
        duk_put_prop_string(ctx, -2, "listen");

        // server.connectionCount()
        duk_push_c_function(ctx, duk_rp_net_connection_count, 0);
        duk_put_prop_string(ctx, -2, "connectionCount");

        // server.maxConnections()
        duk_push_c_function(ctx, duk_rp_net_max_connections, 1);
        duk_put_prop_string(ctx, -2, "maxConnections");

        // server.on()
        duk_push_c_function(ctx, duk_rp_net_x_on, 2);
        duk_put_prop_string(ctx, -2, "on");

        duk_push_c_function(ctx, duk_rp_net_x_once, 2);
        duk_put_prop_string(ctx, -2, "once");

        duk_push_c_function(ctx, duk_rp_net_x_off, 2);
        duk_put_prop_string(ctx, -2, "off");

        //server.close()
        duk_push_c_function(ctx, server_close, 1);
        duk_put_prop_string(ctx, -2, "close");

        duk_put_prop_string(ctx, -2, "prototype");

    // copy for createServer below
    duk_dup(ctx, -1);

    // new net.Server()
    duk_put_prop_string(ctx, -3, "Server");

    // net.createServer
    duk_push_c_function(ctx, net_create_server, 4);
    // [ net, Server, createServer ]
    duk_pull(ctx, -2); //Server reference
    // [ net, createServer, Server ]
    duk_put_prop_string(ctx, -2, "Server"); //save Server as property of function
    // [ net, createServer]
    duk_put_prop_string(ctx, -2, "createServer");
    // [ net ]

    // resolve
    duk_push_c_function(ctx, duk_rp_net_resolve, 2);
    duk_put_prop_string(ctx, -2, "resolve");

    // resolve
    duk_push_c_function(ctx, duk_rp_net_reverse, 1);
    duk_put_prop_string(ctx, -2, "reverse");

    // net.Resolver()
    duk_push_c_function(ctx, duk_rp_net_resolver, 0);
    duk_push_object(ctx); // prototype

        //resolver.resolve
        duk_push_c_function(ctx, duk_rp_net_resolver_resolve, 3);
        duk_put_prop_string(ctx, -2, "resolve");

        //resolver.reverse
        duk_push_c_function(ctx, duk_rp_net_resolver_reverse, 3);
        duk_put_prop_string(ctx, -2, "reverse");

        // resolver.on()
        duk_push_c_function(ctx, duk_rp_net_x_on, 2);
        duk_put_prop_string(ctx, -2, "on");

        duk_push_c_function(ctx, duk_rp_net_x_once, 2);
        duk_put_prop_string(ctx, -2, "once");

        duk_push_c_function(ctx, duk_rp_net_x_off, 2);
        duk_put_prop_string(ctx, -2, "off");

        duk_put_prop_string(ctx, -2, "prototype");
    duk_put_prop_string(ctx, -2, "Resolver");

    //resolve_async
    duk_push_c_function(ctx, duk_rp_net_resolve_async, 3);
    duk_put_prop_string(ctx, -2, "resolve_async");

    //reverse_async
    duk_push_c_function(ctx, duk_rp_net_reverse_async, 3);
    duk_put_prop_string(ctx, -2, "reverse_async");

    duk_push_c_function(ctx, net_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    return 1;
}
