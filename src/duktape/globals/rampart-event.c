/* Copyright (C) 2024 Aaron Flin - All Rights Reserved
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

pthread_mutex_t ev_var_lock;
RPTHR_LOCK *rp_ev_var_lock;

volatile int rp_event_scope_to_module=0;


/* ***********************************************************************
    a list of events, with a bitmap of the threads in which they are used
   *********************************************************************** */
pthread_mutex_t ev_list_lock;


#define RP_EVENT struct rp_event_s
RP_EVENT {
    char          *name;
    unsigned char *map;
    size_t         map_sz;
    RP_EVENT      *next;
};

static RP_EVENT *rp_events=NULL, *rp_last_event=NULL;

/* s will likely be const char * and it will be copied here */
#define rp_new_event(s) ({\
    RP_EVENT *e=NULL;\
    REMALLOC(e, sizeof(RP_EVENT));\
    e->next=NULL;\
    e->map=NULL;\
    e->map_sz=0;\
    e->name=strdup(s);\
    e;\
})

#define rp_add_event(s) ({\
    RP_EVENT *e=rp_new_event((s));\
    if(rp_last_event){\
        rp_last_event->next=e;\
        rp_last_event=e;\
    } else rp_events=rp_last_event=e;\
    e;\
})

#define rp_del_event_string(s) do {\
    RP_EVENT *e=rp_events, *l=NULL;\
    while(e){\
        if(!strcmp(e->evname,(s))){\
            if(e==rp_events) {\
                rp_events=e->next;\
                if(!rp_events) rp_last_event=NULL;\
            } else {\
                l->next=e->next;\
                if(!l->next) rp_last_event=l;\
            }\
            free(e->map);\
            free(e->name);\
            free(e);\
            break; \
        }\
        l=e;\
        e=e->next;\
    }\
} while(0)

#define rp_del_event(event) do {\
    RP_EVENT *e=rp_events, *l=NULL;\
    while(e){\
        if(e==event){\
            if(e==rp_events) {\
                rp_events=e->next;\
                if(!rp_events) rp_last_event=NULL;\
            } else {\
                l->next=e->next;\
                if(!l->next) rp_last_event=l;\
            }\
            free(e->map);\
            /*printf("freeing %s\n",e->name);*/\
            free(e->name);\
            free(e);\
            break; \
        }\
        l=e;\
        e=e->next;\
    }\
} while(0)

#define rp_get_event(s) ({\
    RP_EVENT *e=rp_events, *ret=NULL;\
    while(e){\
        if(!strcmp(e->name,(s))){\
            ret=e;\
            break;\
        }\
        e=e->next;\
    }\
    ret;\
})

#define debug_print_list() do {\
    RP_EVENT *e=rp_events, *ret=NULL;\
    while(e){\
        printf("%s\n",e->name);\
        e=e->next;\
    }\
    ret;\
}while(0)

#define rp_event_grow_bitmap(e,b) do{\
    size_t i=e->map_sz;\
    if( i<(b) ) {\
        REMALLOC(e->map, (b));\
        memset(&((e->map)[i]), 0, b-i);\
        /*for(;i<b;i++) (e->map)[i]=0;*/\
        e->map_sz=(b);\
    }\
} while (0)

#define rp_event_add_thread(e,n) do {\
    size_t curbyte=(n)/8, curbit=(n)%8;\
    if(e){\
        rp_event_grow_bitmap(e, curbyte+1);\
        (e->map)[curbyte] |= (1<<curbit);\
    }\
} while (0)

#define rp_event_del_thread(e,n) do {\
    size_t curbyte=(n)/8, curbit=(n)%8;\
    if(e && curbyte < e->map_sz){\
        (e->map)[curbyte] &= ~(1<<curbit);\
    }\
} while (0)

#define rp_event_test_thread(e,n) ({\
    int ret=0;\
    size_t curbyte=(n)/8, curbit=(n)%8;\
    if(e && curbyte < e->map_sz) {\
            ret = (e->map)[curbyte] & (1<<curbit);\
    }\
    /*if(e && !action) printf("in test, byte=%lu, bit=%lu, val=%d\n", curbyte, curbit, (int)(e->map)[curbyte]);*/\
    ret;\
})

//get existing or make new rp_event struct
//and set bitmap to current thread
#define rp_event_register(s) do{\
    RP_PTLOCK(&ev_list_lock);\
    RP_EVENT *e=rp_get_event(s);\
    if(!e) e=rp_add_event(s);\
    rp_event_add_thread(e,(size_t)get_thread_num());\
    RP_PTUNLOCK(&ev_list_lock);\
} while(0)

#define rp_event_unregister(s) do{\
    RP_PTLOCK(&ev_list_lock);\
    RP_EVENT *ev=rp_get_event(s);\
    /*printf("unregister %s\n",s);*/\
    if(ev){\
        unsigned char keepev=0;\
        int i=0;\
        rp_event_del_thread(ev,(size_t)get_thread_num());\
        /* test if any bit is set */\
        for(; i < ev->map_sz; i++)\
            keepev|=(ev->map)[i];\
        if(!keepev) /* none are set */\
            rp_del_event(ev);\
    }\
    RP_PTUNLOCK(&ev_list_lock);\
} while(0)

#define rp_event_test(s,n) ({\
    RP_PTLOCK(&ev_list_lock);\
    int ret=0;\
    RP_EVENT *e=rp_get_event(s);\
    if(e) ret=rp_event_test_thread(e,(size_t)n);\
    /*else {printf("event %s not found\n",s); debug_print_list();}*/\
    RP_PTUNLOCK(&ev_list_lock);\
    ret;\
})

duk_ret_t duk_scope_to_module(duk_context *ctx)
{
    rp_event_scope_to_module=1;
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
                    if(duk_rp_push_current_module(ctx))
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
        duk_put_prop_string(ctx, -2, evname);// new event object into jsevents

    // mark this thread as having this event
    //if(get_thread_num()==4) printf("registering event in 4\n");
    rp_event_register(evname);
    //printf("regestered %s in thread %d\n", onname, get_thread_num());
    return 0;
}

#define JSEVENT_TRIGGER 0 // trigger event
#define JSEVENT_DELETE  1 // delete the event
#define JSEVENT_DELFUNC 2 // delete a function in the event

#define JSEVARGS struct jsev_args
JSEVARGS {
    struct event *e;
    char    *key;
    char    *fname;
    int      action;
    int      varno;
    int     *refcount;
};

void rp_jsev_doevent(evutil_socket_t fd, short events, void* arg)
{
    JSEVARGS *earg = (JSEVARGS *) arg;
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->htctx;
    duk_idx_t arg_idx = -1;
    size_t varname_sz = strlen(earg->key) + 8;
    char varname[varname_sz];

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
        if( arg_idx ==-1)\
            duk_push_undefined(ctx);\
        else\
            duk_dup(ctx,arg_idx);\
        if(duk_pcall(ctx,2) != 0)\
        {\
            if (duk_is_error(ctx, -1) )\
            {\
                duk_get_prop_string(ctx, -1, "stack");\
                fprintf(stderr, "Error in event callback: %s\n", duk_get_string(ctx, -1));\
                /* throw will never work from within an event.  Best we can do is print the err */\
                /*RP_THROW(ctx, duk_safe_to_string(ctx, -1));*/\
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
    duk_pop(ctx);/* pop enum */\
}while(0)

    /* get the trigger argument key, set idx=0, check refcount */
    if( earg->varno > -1)
    {
        sprintf(varname, "\xff%s%d", earg->key, earg->varno);
        arg_idx=0;
    }

    /* the first round, for normal thread/stack combos */
    /* the second round, if we have websockets */
    do {
        if(!ctx)
            break;

        // VERY IMPORTANT TO copy var BEFORE BEFORE BEFORE decrementing refcount
        // copy trigger var to this stack
        if(arg_idx == 0)
        {
            get_from_clipboard(ctx, varname);
            duk_insert(ctx, 0);
        }
        if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents")) )
        {
            //no events of any kind on this ctx
            duk_pop(ctx);
            goto bottom;
        }
        if(earg->action == JSEVENT_TRIGGER || earg->action == JSEVENT_DELFUNC)
        {
            /* do we have an event named "key" ? */
            if(!duk_get_prop_string(ctx, -1, earg->key))
            {
                goto bottom; //continue to second one or break
            }
            /* yes */
            if(earg->action == JSEVENT_TRIGGER) // run all functions for this event
            {
                    jsev_exec_func;
            }
            else
            {
                //printf("Deleting %s\n", earg->fname);
                duk_del_prop_string(ctx, -1, earg->fname); //delete the named function in this event
                //check if there are any functions left
                duk_enum(ctx,-1,0);
                if(!duk_next(ctx, -1, 0))
                {
                    /* delete this event since there are no functions left*/
                    //[... jsevents, myevent, enum ]
                    duk_del_prop_string(ctx, -3, earg->key);
                    // mark this thread as no longer having this event
                    rp_event_unregister(earg->key);
                }
            }
        }
        else
        {
            /* delete this event and all its functions */
            duk_del_prop_string(ctx, -1, earg->key);
            rp_event_unregister(earg->key);
        }

        bottom:

        duk_set_top(ctx, 0);

        if(ctx == thr->htctx) //finished first round
            ctx = thr->wsctx; //switch stacks. Second round is for websockets if wsctx != NULL.
        else if(ctx == thr->wsctx)
            break;

    } while(1);

    if( earg->varno > -1)
    {
        if(earg->refcount)
        {
            RP_MLOCK(rp_ev_var_lock);
            (*earg->refcount)--;
            //printf("thr=%d, ref=%p refcount = %d\n",get_thread_num(), earg->refcount, *earg->refcount);
            RP_MUNLOCK(rp_ev_var_lock);
        }
    }

    event_free(earg->e);
    free(earg->key);
    if(earg->fname)
        free(earg->fname);
    free(earg);
}

static struct timeval fiftyms={0,50000};
#define ev_add(a,b) do{ /*printf("event:%d - adding %p\n",__LINE__, (a));*/ event_add(a,b);}while(0)

void rp_jsev_freevar(evutil_socket_t fd, short events, void* arg)
{
    JSEVARGS *earg = (JSEVARGS *) arg;
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->ctx;
    int *refcount = earg->refcount;

    RP_MLOCK(rp_ev_var_lock);

    //printf("checking ev %s, ", earg->key);
    //printf("refs=%d\n", *refcount);

    earg->action++; // repurposed as a counter

    if(earg->action > 100) // this shouldn't happen, but if it does, give up
    {
        fprintf(stderr,"WARN: event %s in thread %d has %d unrun events after 5 seconds.  Giving up on freeing\n",
            earg->key, get_thread_num(), *refcount);
        event_del(earg->e);
        event_free(earg->e);
        free(earg->key);
        free(earg);
    }
    else if(*refcount < 1)
    {
        size_t varname_sz = strlen(earg->key) + 8;
        char varname[varname_sz];

        sprintf(varname, "\xff%s%d", earg->key, earg->varno);
        del_from_clipboard(ctx, varname);
        duk_pop(ctx);
        //printf("FREE %p\n", refcount);
        free(refcount);
        event_del(earg->e);
        event_free(earg->e);
        free(earg->key);
        free(earg);
    }
    else
    {
        ev_add(earg->e, &fiftyms);
    }

    RP_MUNLOCK(rp_ev_var_lock);
}


static void evloop_insert(duk_context *ctx, const char *evname, const char *fname, int varno, int action)
{
    struct timeval timeout;
    JSEVARGS *args = NULL;
    int i=0, *refcount=NULL;
    char *lastkey=NULL;

    timeout.tv_sec=0;
    timeout.tv_usec=0; 

    /* if triggering and we are passing a variable, keep a refcount so
       we know when we are done using it                              */
    if(action == JSEVENT_TRIGGER && varno > -1)
    {
        REMALLOC(refcount, sizeof(int));
        *refcount=0;
        //printf("ALLC %p\n", refcount);
    }

    THRLOCK;
    for(i=0; i<nrpthreads; i++)
    {

        //if(action==2) printf("Testing delete %s for thread %d\n", fname, i);

        if(!rp_event_test(evname, i))
        {
            //if(i==4) printf("skipping thr %d\n",i);
            continue;
        }
        //if(action==0) printf("USING %s for thread %d\n", evname, i);

        //if(action==2) printf("Sending delete %s for thread %d\n", fname, i);

        RPTHR *thr=rpthread[i];
        struct event_base *base;

        if( ! RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) || !thr->base )
            continue;

        base=thr->base;

        args=NULL;
        REMALLOC(args,sizeof(JSEVARGS));
        args->key=NULL;

        if(rp_event_scope_to_module)
        {
            const char *id=NULL;

            // are we in a module?
            if(duk_rp_push_current_module(ctx))
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
                if(!lastkey)
                    lastkey=strdup(args->key);
                else
                {
                    REMALLOC(lastkey, strlen(id) + strlen(evname)+3);
                    strcpy(lastkey, args->key);
                }
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
        args->varno=varno;
        args->refcount = refcount;
        if(refcount)
        {
            RP_MLOCK(rp_ev_var_lock);
            (*refcount)++;
            RP_MUNLOCK(rp_ev_var_lock);
        }
        //printf("event = %p, base=%p in thread %d, flag=%d, thr=%p\n", args->e, thr->base, get_thread_num(), thr->flags, thr);
        //if(!action) printf ("Adding event to thread %d, ctx %p\n", i, thr->ctx);
        ev_add(args->e, &timeout);
        //if(i==4)printf("added event '%s' to loop thr=%d\n", evname, i);
    }

    if(refcount && *refcount == 0)
        free(refcount);
    else if (refcount)
    {
        // refcount can be decremented to 0 and freed in other threads running rp_jsev_doevent
        // faster than we can insert the next one.  So rather than free refcount and delete the
        // triggervar in rp_jsev_doevent, we will insert an event to do it after all insertions
        // are done
        args=NULL;
        REMALLOC(args,sizeof(JSEVARGS));
        if(lastkey)
            args->key=lastkey;
        else
            args->key=strdup(evname);
        args->refcount = refcount;
        args->e = event_new((get_current_thread())->base, -1, 0, rp_jsev_freevar, args);
        args->varno=varno;
        args->action=0; //Action not used in rp_jsev_freevar -- repurpose this as a counter
        ev_add(args->e, &timeout);
    }
    THRUNLOCK;
}

static uint16_t cb_var_no=0;

duk_ret_t duk_rp_trigger_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.trigger: parameter must be a string (event name)");
    int varno=-1;

    if(!duk_is_undefined(ctx,1))
    {
        size_t varname_sz = strlen(evname) + 8;
        char varname[varname_sz];

        RP_MLOCK(rp_ev_var_lock);
        varno = (int)(cb_var_no++);
        RP_MUNLOCK(rp_ev_var_lock);

        sprintf(varname, "\xff%s%d", evname, varno);
        put_to_clipboard(ctx, 1, varname);
    }
    evloop_insert(ctx, evname, NULL, varno, JSEVENT_TRIGGER);
    return 0;
}

/* remove named function, wherever it might reside */
duk_ret_t duk_rp_off_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.off: first parameter must be a string (event name)");
    const char *fname = REQUIRE_STRING(ctx, 1, "event.off: second parameter must be a string (function name)");

    evloop_insert(ctx, evname, fname, -1, JSEVENT_DELFUNC);
    return 0;
}

/* remove event and all function, wherever they might reside */
duk_ret_t duk_rp_remove_event(duk_context *ctx)
{
    const char *evname = REQUIRE_STRING(ctx, 0, "event.remove: first parameter must be a string (event name)");

    evloop_insert(ctx, evname, NULL, -1, JSEVENT_DELETE);
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
        rp_ev_var_lock=RP_MINIT(&ev_var_lock);
        RP_PTINIT(&ev_list_lock);
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
