/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include  <stdio.h>
#include  <time.h>
#include "event.h"
#include "event2/thread.h"
#include "rampart.h"

pthread_mutex_t cborlock;
RPTHR_LOCK *rp_cborlock;

volatile int rp_event_scope_to_module=0;

duk_ret_t duk_scope_to_module(duk_context *ctx)
{
    rp_event_scope_to_module=1;
    return 0;
}

/* unused
duk_ret_t duk_rp_new_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.new: parameter must be a string (event name)");

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents"));
    if(!duk_get_prop_string(ctx, -1, evname))
    {
        duk_push_object(ctx);
        duk_put_prop_string(ctx, -2, evname);
    }
    return 0;
}
*/

// As far as ugly hacks go, this is as bad as it gets.  No idea how else to extract the call stack
// and get the name of our current module, except by hacking at duktape itself.
static int push_current_module(duk_context *ctx)
{
    const char *errstr;
    char *p=NULL;

    duk_push_error_object(ctx, DUK_ERR_TYPE_ERROR, "");
    duk_get_prop_string(ctx, -1, "stack");
    errstr = duk_get_string(ctx, -1);

    duk_remove(ctx,-2);
    p=strstr(errstr, "rampart-event.c");
    if(p)
    {
        //the first path with a name should be our last module
        p=strstr(p, "(/" );
        if(p)
        {
            char *e=strchr(p,':'); //end position is at ':' in "script.js:linenum"

            if(e)
            {
                p++; //start at '/'

                duk_push_global_stash(ctx);
                if(duk_get_prop_string(ctx, -1, "module_id_map"))
                {
                    int ret = 0;
                    duk_push_lstring(ctx, p, (e-p) );
                    if(duk_get_prop(ctx, -2))
                        ret=1; 
                    //either our prop, or undefined at top of stack
                    duk_remove(ctx,-2); //module_id_map
                    duk_remove(ctx,-2); //global_stash
                    duk_remove(ctx,-2); //errstr backing string
                    return ret;
                }
                duk_pop_2(ctx); //global_stash and undefined               
            }
        }
    }
    duk_pop(ctx);
    duk_push_undefined(ctx);
    return 0;
}


duk_ret_t duk_rp_on_event(duk_context *ctx)
{
    const char *evname=NULL, *onname=NULL;
    duk_idx_t i=0, func_idx=-1, param_idx=-1;
    int have_object=1;

    while (i<3)
    {
        if(duk_is_string(ctx, i))
        {
            if(evname)
                onname=duk_get_string(ctx, i);
            else
            {
                if(rp_event_scope_to_module)
                {
                    const char *id=NULL;
                    // are we in a module?
                    if(push_current_module(ctx))
                    {
                        if(duk_get_prop_string(ctx, -1, "id"))
                        {
                                id=duk_get_string(ctx, -1);
                        }
                        duk_pop(ctx);
                    }
                    duk_pop(ctx);

                    if(id)
                    {
                        evname=duk_get_string(ctx, i);
                        duk_push_sprintf(ctx, "\xFE%s:%s", id, evname);
                        duk_replace(ctx, i);  
                    }
                }
                evname=duk_get_string(ctx, i);
            }
        }
        else if (duk_is_function(ctx, i))
            func_idx=i;
        i++;
    }

    if(func_idx == -1 || !onname || !evname)
        RP_THROW(ctx, "event.on: parameters must be a string (event name), a second string(function name) and a function(callback)");

    if(!duk_is_undefined(ctx, 3))
        param_idx=3;

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents"));
    if(!duk_get_prop_string(ctx, -1, evname))
    {
        have_object=0;
        duk_pop(ctx);//undefined
        duk_push_object(ctx);
    }
    duk_push_object(ctx);
    duk_dup(ctx, func_idx);    
    duk_put_prop_string(ctx, -2, "cb");
    if(param_idx>-1)
    {
        duk_dup(ctx,param_idx);
        duk_put_prop_string(ctx, -2, "param");
    }
    duk_put_prop_string(ctx, -2, onname);

    if(!have_object)
        duk_put_prop_string(ctx, -2, evname);// new object into jsevents

    return 0;
}

#define CBH struct cbor_holder_s
CBH {
    int    refcount;
    void   *data;
    size_t size;
};

#define JSEVENT_TRIGGER 0 // trigger event
#define JSEVENT_DELETE  1 // delete the event
#define JSEVENT_DELFUNC 2 // delete a function in the event

#define JSEVARGS struct jsev_args
JSEVARGS {
    int          thread_no;
    struct event *e;
    char         *key;
    char	 *fname;
    int          action;
    int		 count;
    CBH         *cbor;
};

void rp_jsev_doevent(evutil_socket_t fd, short events, void* arg)
{
    JSEVARGS *earg = (JSEVARGS *) arg;
    duk_context *ctx = main_ctx;
    duk_idx_t arg_idx = -1;

    /* if using threads, there are two thread_ctxs per thread/event-loop */
    if(earg->thread_no > -1)
        ctx=rpthread[earg->thread_no]->ctx;

#define jsev_exec_func do{\
    duk_enum(ctx, -1, 0); /* [ {jsevents}, {myevent}, enum ]*/\
    while(duk_next(ctx, -1, 1))\
    {\
        /* [ {jsevents}, {myevent}, enum, func_name, {object} ]*/\
        if(!duk_get_prop_string(ctx, -1, "cb"))\
        {\
            /* [ {jsevents}, {myevent}, enum, func_name, {object}, undefined ]*/\
            duk_pop_3(ctx); /*[ {jsevents}, {myevent}, enum ] */\
            continue;\
        }\
        duk_get_prop_string(ctx, -2, "param"); /* [ {jsevents}, {myevent}, enum, func_name, {object}, func, param ]*/\
        if(arg_idx == -1 && earg->cbor != NULL)\
        {\
            duk_push_external_buffer(ctx);\
            duk_config_buffer(ctx, -1, earg->cbor->data, (duk_size_t) earg->cbor->size);\
            duk_cbor_decode(ctx, -1, 0);\
            duk_get_prop_string(ctx, -1, "arg");\
            duk_insert(ctx, 0);\
            duk_pop(ctx);\
            arg_idx = 0;\
        }\
        if( arg_idx ==-1)\
            duk_push_undefined(ctx);\
        else\
            duk_dup(ctx,arg_idx);\
        if(duk_pcall(ctx,2) != 0)\
        {\
            if (duk_is_error(ctx, -1) )\
            {\
                duk_get_prop_string(ctx, -1, "stack");\
                printf("Error in event callback: %s\n", duk_get_string(ctx, -1));\
                /* why won't throw work?  It seems to be in the middle of a throw anyway.\
                   but I thought that was the point of duk_pcall */\
                RP_THROW(ctx, duk_safe_to_string(ctx, -1));\
                duk_pop(ctx);\
            }\
            else if (duk_is_string(ctx, -1))\
            {\
                printf("Error in event callback: %s\n", duk_get_string(ctx, -1));\
                RP_THROW(ctx, "Error in event callback: %s\n", duk_get_string(ctx, -1));\
            }\
            else\
            {\
                printf("Error in event callback\n");\
                RP_THROW(ctx, "Error in event callback\n");\
            }\
        }\
        duk_pop_3(ctx); /* [ {jsevents}, {myevent}, enum]*/\
    }\
    duk_pop(ctx);/*enum*/\
}while(0)

    /* the first one */
    do {
        if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents")) )
        {
            //no events on this ctx
            duk_pop(ctx);
            break;
        }
        if(earg->action == JSEVENT_TRIGGER || earg->action == JSEVENT_DELFUNC)
        {
            /* do we have an event named "key" ? */
            if(!duk_get_prop_string(ctx, -1, earg->key))
            {
                duk_pop_2(ctx);
                break; //continue to second one
            }
            /* yes */
            if(earg->action == JSEVENT_TRIGGER) // run all functions for this event
                jsev_exec_func;
            else
            {
                duk_del_prop_string(ctx, -1, earg->fname); //delete the named function in this event
            }
            duk_pop_2(ctx); // []
        }
        else
        {
            /* delete this event and all its functions */
            duk_del_prop_string(ctx, -1, earg->key);
            duk_pop(ctx);
        }
    } while(0);    

    /* clean up the first one */
    if(arg_idx != -1)
    {
        duk_remove(ctx, arg_idx);
        arg_idx = -1;
    }

    /* the second one, if thread */
    if(earg->thread_no > -1)
    {

        ctx = rpthread[earg->thread_no]->wsctx; //switch stacks. Second stack is for websockets.
        if(!ctx)
            goto end;

        if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents")) )
        {
            //no events on this ctx
            duk_pop(ctx);
            goto end;
        }

        if(earg->action == JSEVENT_TRIGGER || earg->action == JSEVENT_DELFUNC)
        {
            if(!duk_get_prop_string(ctx, -1, earg->key))
            {
                duk_pop_2(ctx);
                goto end;
            }
            if(earg->action == JSEVENT_TRIGGER)
                jsev_exec_func;
            else
            {
                duk_del_prop_string(ctx, -1, earg->fname);
            }
            duk_pop_2(ctx); // []
        }
        else
        {
            duk_del_prop_string(ctx, -1, earg->key);
            duk_pop(ctx);
        }
    }

    end:
    /* clean up the second one */
    if(arg_idx != -1)
        duk_remove(ctx, arg_idx);

    if(earg->cbor)
    {
        RP_MLOCK(rp_cborlock);

        earg->cbor->refcount--;
        if(!earg->cbor->refcount)
        {
            free(earg->cbor->data);
            free(earg->cbor);
        }

        RP_MUNLOCK(rp_cborlock);
    }
    event_free(earg->e);
    free(earg->key);
    if(earg->fname)
        free(earg->fname);
    free(earg);
}

static void evloop_insert(duk_context *ctx, const char *evname, const char *fname, CBH *cbor, int action)
{
    struct timeval timeout;
    JSEVARGS *args = NULL;
    int i=0;

    timeout.tv_sec=0;
    timeout.tv_usec=0; 

    for(i=0; i<nrpthreads; i++)
    {
        RPTHR *thr=rpthread[i];
        struct event_base *base;

        base=thr->base;

        args=NULL;
        REMALLOC(args,sizeof(JSEVARGS));
        args->thread_no = i;
        args->key=NULL;

        if(rp_event_scope_to_module)
        {
            const char *id=NULL;

            // are we in a module?
            if(push_current_module(ctx))
            {
                if(duk_get_prop_string(ctx, -1, "id"))
                {
                    id=duk_get_string(ctx, -1);
                }
                duk_pop(ctx);
            }
            duk_pop(ctx);

            if(id)
            {
                REMALLOC(args->key, strlen(id) + strlen(evname)+3);
                sprintf(args->key, "\xFE%s:%s", id, evname);
            }
            
        }
        if(!args->key)
            args->key = strdup(evname);

        args->action=action;

        if(fname)
            args->fname=strdup(fname);
        else
            args->fname=NULL;

        args->e = event_new(base, -1, 0, rp_jsev_doevent, args);

        args->cbor = cbor;

        event_add(args->e, &timeout);
    }
}


duk_ret_t duk_rp_trigger_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.trigger: parameter must be a string (event name)");
    CBH *cbor = NULL;

    if(!duk_is_undefined(ctx,1))
    {
        duk_size_t sz;
        void *buf;

        REMALLOC(cbor, sizeof(CBH));
        duk_push_object(ctx);
        duk_pull(ctx, -2);
        duk_put_prop_string(ctx, -2, "arg");
        duk_cbor_encode(ctx, -1, 0);
        buf = duk_get_buffer_data(ctx, -1 ,&sz);
        cbor->size = (size_t)sz;
        cbor->data=NULL;
        REMALLOC(cbor->data, cbor->size);
        memcpy(cbor->data, buf, cbor->size);       
        //cbor->refcount = totnthreads+1;
        cbor->refcount = nrpthreads;
    }
    evloop_insert(ctx, evname, NULL, cbor, JSEVENT_TRIGGER);
    return 0;
}

/* remove named function, wherever it might reside */
duk_ret_t duk_rp_off_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.off: first parameter must be a string (event name)");
    const char *fname = REQUIRE_STRING(ctx, 1, "event.off: second parameter must be a string (function name)");

    evloop_insert(ctx, evname, fname, NULL, JSEVENT_DELFUNC);
    return 0;
}

/* remove event and all function, wherever they might reside */
duk_ret_t duk_rp_remove_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.remove: first parameter must be a string (event name)");

    evloop_insert(ctx, evname, NULL, NULL, JSEVENT_DELETE);
    return 0;
}

void duk_event_init(duk_context *ctx)
{
    static int isinit=0;
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    if(!duk_get_prop_string(ctx,-1,"event"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    if(!isinit)
    {
        rp_cborlock=RP_MINIT(&cborlock);
        isinit=1;
    }

    // currently unused
    //duk_push_c_function(ctx, duk_rp_new_event, 1);
    //duk_put_prop_string(ctx, -2, "new");

    duk_push_c_function(ctx, duk_scope_to_module, 0);
    duk_put_prop_string(ctx, -2, "scopeToModule");

    duk_push_c_function(ctx, duk_rp_on_event, 4);
    duk_put_prop_string(ctx, -2, "on");

    duk_push_c_function(ctx, duk_rp_off_event, 2);
    duk_put_prop_string(ctx, -2, "off");

    duk_push_c_function(ctx, duk_rp_remove_event, 1);
    duk_put_prop_string(ctx, -2, "remove");

    duk_push_c_function(ctx, duk_rp_trigger_event, 2);
    duk_put_prop_string(ctx, -2, "trigger");

    duk_put_prop_string(ctx, -2,"event");
    duk_put_global_string(ctx,"rampart");

    duk_push_object(ctx);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents"));
}
