/* Copyright (C) 2023 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include  <stdio.h>
#include  <time.h>
#include "rampart.h"
#include "event.h"
#include "event2/thread.h"
#include "../../register.h"
#include "event2/dns.h"

//do we really need a hard limit?
#ifndef RP_MAX_THREADS
#define RP_MAX_THREADS 4096
#endif

RPTHR **rpthread=NULL;
uint16_t nrpthreads=0;

// the glue that holds it all together
__thread int thread_local_thread_num=0;

pthread_mutex_t cp_lock; //lock for copy/paste ctx
RPTHR_LOCK *rp_cp_lock;

duk_context *cpctx=NULL; //copy/paste ctx for thread_put and thread_get

#ifdef RPTHRDEBUG

#define dprintf(...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    fflush(stdout);\
}while(0)
#define xxdprintf(...) /*nada*/

#else

#define dprintf(...) /*nada*/
#define xxdprintf(...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    fflush(stdout);\
}while(0)

#endif

/* *************************************************
              FUNCTIONS FOR LOCKS
**************************************************** */
/* 
    Obviously the lock that locks the thread that creates and deletes rp_locks 
    that lock when the grab a lock can't lock from the rp_lock list.

    Gosh, were you actually thinking that the lock that maintains
    rp_locks could lock with a lock inside of rp_locks?

    Dewd.., grow up.
*/ 

/* lock when accessing the RPTHR_LOCK *rampart_locks list */
pthread_mutex_t thr_list_lock;
#define LISTLOCK    RP_PTLOCK(&thr_list_lock)
#define LISTUNLOCK  RP_PTUNLOCK(&thr_list_lock)

/* lock to prevent new rampart locks before a fork */
pthread_mutex_t *rp_fork_lock=NULL;
#define FORKLOCK    do{\
    while( (rp_fork_lock) && pthread_mutex_trylock(rp_fork_lock))\
    { dprintf("TRYING to get forklock\n");    usleep(1000000);}\
}while(0)

#define FORKUNLOCK  if(rp_fork_lock) RP_PTUNLOCK(rp_fork_lock)

#define FORKLOCK_INIT do{\
    REMALLOC(rp_fork_lock, sizeof(pthread_mutex_t));\
    RP_PTINIT(rp_fork_lock);\
} while(0)

#define FORKLOCK_DESTROY do{\
    while(pthread_mutex_trylock(rp_fork_lock)) usleep(10000);\
    pthread_mutex_unlock(rp_fork_lock);\
    free(rp_fork_lock);\
    rp_fork_lock=NULL;\
} while(0)

// locks for manipulating the individual locks in the *rampart_locks list
#ifdef RP_USE_LOCKLOCKS
#define LOCKLOCK   RP_PTLOCK(thrlock->locklock)
#define LOCKUNLOCK RP_PTUNLOCK(thrlock->locklock)
#else
#define LOCKLOCK   /* niente */
#define LOCKUNLOCK /* zilch  */
#endif

// set, clear and test whether the lock is marked as set.
#define SETLOCKED     RPTHR_SET   (thrlock, RPTHR_LOCK_FLAG_LOCKED)
#define SETUNLOCKED   RPTHR_CLEAR (thrlock, RPTHR_LOCK_FLAG_LOCKED)
#define ISLOCKED    ( RPTHR_TEST  (thrlock, RPTHR_LOCK_FLAG_LOCKED) )
#define ISUNLOCKED  (!RPTHR_TEST  (thrlock, RPTHR_LOCK_FLAG_LOCKED) )

//RP_MLOCK and RP_MUNLOCK are defined in rampart.h
//struct RPTHR_LOCK is defined in rampart.h
RPTHR_LOCK *rampart_locks=NULL; //the head of the list of all locks maintained in these functions

RPTHR_LOCK *rp_init_lock(pthread_mutex_t *lock)
{
    RPTHR_LOCK *thrlock=NULL, *lastlock=rampart_locks;

    LISTLOCK;

    if(lastlock)
    {
        while( lastlock->next )
            lastlock=lastlock->next;
    }

    REMALLOC(thrlock, sizeof(RPTHR_LOCK));

    if(!lock) //NULL and we make our own lock
    {
        REMALLOC(lock, sizeof(pthread_mutex_t));
        RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_FREELOCK);
    }

    thrlock->lock       = lock;

    thrlock->thread_idx = -1; // this is >-1 only while holding lock
    thrlock->flags      = 0;
    thrlock->next       = NULL;
#ifdef RP_USE_LOCKLOCKS
    thrlock->locklock=NULL;
    REMALLOC(thrlock->locklock, sizeof(pthread_mutex_t));
    RP_PTINIT(thrlock->locklock);    
#endif

    if (pthread_mutex_init((lock),NULL) != 0)
    {
        if(RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_FREELOCK))
            free(thrlock->lock);
        free(thrlock);
        LISTUNLOCK;
        return NULL;
    }

    if(lastlock)
        lastlock->next = thrlock;
    else
        rampart_locks = thrlock;

    LISTUNLOCK;
    return thrlock;
}



int rp_remove_lock(RPTHR_LOCK *thrlock)
{
    RPTHR_LOCK *l=rampart_locks, *prev=NULL;

    if(!l || !thrlock)
        return 0;

    LISTLOCK;
    if(l!=thrlock)
    {
        while(1)
        {
            prev=l;
            l=l->next;
            if (!l || l == thrlock)
                break;
        }
    }

    if(l)
    {
        if(prev)
            prev->next=l->next;
        else
            rampart_locks=l->next;

        if(RPTHR_TEST(l, RPTHR_LOCK_FLAG_FREELOCK))
            free(l->lock);

        free(l);

        LISTUNLOCK;

        return 1;
    }

    LISTUNLOCK;
    return 0;
}
/* currently unused
// 1 - exists, 0 - doesn't exist
static int rp_check_lock(RPTHR_LOCK *thrlock)
{
    RPTHR_LOCK *l=rampart_locks;

    if(!l || !thrlock)
        return 0;

    LISTLOCK;
    if(l!=thrlock)
    {
        while(1)
        {
            l=l->next;
            if (!l || l == thrlock)
                break;
        }
    }

    if(l)
    {
        return 1;
    }

    LISTUNLOCK;
    return 0;
}
*/

int rp_lock(RPTHR_LOCK *thrlock)
{
    int ret=0;
    //printf("islocked=%d thrsame=%d\n", ISLOCKED, (thrlock->thread_idx == thread_local_thread_num));
    //if( ISLOCKED && thrlock->thread_idx == thread_local_thread_num)
    //    return -100;

    FORKLOCK; // No new locking while setting up for fork.

    ret = pthread_mutex_lock(thrlock->lock);
    if(ret)
    {
        LISTUNLOCK;
        return ret;
    }

    LOCKLOCK;

    SETLOCKED;
    thrlock->thread_idx = thread_local_thread_num;

    LOCKUNLOCK;
    FORKUNLOCK;

    return ret;
}

static int _rp_trylock(RPTHR_LOCK *thrlock, int skip_forklock)
{
    int ret=0;

    if(!skip_forklock) //we need to try for the lock in claim_all() below
        FORKLOCK; // No new locking while setting up for fork.

    ret = pthread_mutex_trylock(thrlock->lock);
    if(ret)
    {
        LISTUNLOCK;
        return ret;
    }
    LOCKLOCK;

    SETLOCKED;
    thrlock->thread_idx = thread_local_thread_num;

    LOCKUNLOCK;
    if(!skip_forklock)
        FORKUNLOCK;
    return 0;
}

int rp_trylock(RPTHR_LOCK *thrlock)
{
    return _rp_trylock(thrlock, 0);
}

int rp_unlock(RPTHR_LOCK *thrlock)
{
    LOCKLOCK;

    if( ISUNLOCKED )
    {
        LOCKUNLOCK;
        return -100;
    }

    SETUNLOCKED;
    thrlock->thread_idx = -1; 
    LOCKUNLOCK;
    return pthread_mutex_unlock(thrlock->lock);
}

int rp_get_locking_thread(RPTHR_LOCK *thrlock)
{
    LOCKLOCK;
    if(ISLOCKED)
    {
        int ret = (int) thrlock->thread_idx;
        LOCKUNLOCK;
        return ret;
    }
    LOCKUNLOCK;
    return -1;
}

void rp_unlock_all_locks(int isparent)
{
    RPTHR_LOCK *thrlock=rampart_locks;
    while(1)
    {
        if(RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_TEMP) )
        {
            rp_unlock(thrlock);
            //printf("(%d) UNLOCKING %p\n", (int)getpid(), l->lock);
            RPTHR_CLEAR(thrlock, RPTHR_LOCK_FLAG_TEMP);
        }
        else if(RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_REINIT) )
        {
            RPTHR_CLEAR(thrlock, RPTHR_LOCK_FLAG_REINIT);
            if(!isparent)
            {
                thrlock->thread_idx=-1;
                SETUNLOCKED;
                if( RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_FREELOCK) )
                    free(thrlock->lock);
                thrlock->lock=NULL;
                REMALLOC(thrlock->lock, sizeof(pthread_mutex_t));
                RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_FREELOCK);

                if (pthread_mutex_init((thrlock->lock),NULL) != 0)
                {
                    fprintf(stderr, "Failed to reinitialize lock after fork\n");
                    exit(1);
                }
#ifdef RP_USE_LOCKLOCKS
                free(thrlock->locklock);
                thrlock->locklock=NULL;
                REMALLOC(thrlock->locklock, sizeof(pthread_mutex_t));

                if (pthread_mutex_init((thrlock->locklock),NULL) != 0)
                {
                    fprintf(stderr, "Failed to reinitialize lock after fork\n");
                    exit(1);
                }
#endif
            }
        }

        thrlock=thrlock->next;
        if (!thrlock)
            break;
    }
    FORKUNLOCK; //resume normal locking
    FORKLOCK_DESTROY;
}

/*
    Claim as many locks as we can in this thread.
    Certain locks cannot be claimed (e.g. lmdb write transaction locks), because
    they are released in finalizers which are in threads on duk stacks that all go away
    post fork. 
*/
void rp_claim_all_locks()
{
    RPTHR_LOCK *thrlock=rampart_locks;
    int nlocked_elsewhere=0, nuser_locked_elsewhere=0, lres, nattempts=0;

    FORKLOCK_INIT;
    FORKLOCK;  // don't allow any new locking (unlocked in unlock_all_locks() above)
    // obtain every lock
    while(1)
    {
        if( (! RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_JSFIN) )
        	&&
            (
                (ISLOCKED && thrlock->thread_idx != thread_local_thread_num) //its from another thread
                    ||
                ISUNLOCKED //lock it just in case some other thread is about to do so.
            )
        ){
            lres = _rp_trylock(thrlock,1);
            if(lres) //didn't get lock
            {
                //printf("couldn't get LOCKED lock %p of rplock %p from thread %d\n", thrlock->lock, thrlock, (int)thread_local_thread_num);
                nlocked_elsewhere++;
                if( RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_JSLOCK) )
                    nuser_locked_elsewhere++;
            }
            else
            { 
                //printf("LOCKING %p, was from another thread\n", thrlock->lock);
                RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_TEMP);//mark that we need to unlock this.
            }
        }

        thrlock=thrlock->next;

        if (!thrlock && !nlocked_elsewhere)
            break;

        if (!thrlock)
        {
            thrlock=rampart_locks;
            //printf("trying again with %d locks to get\n", nlocked_elsewhere);
            nattempts++;
            if( !(nattempts % 300 ) )
            {
                fprintf(stderr, "WARN: There are %d held locks outside this thread (%d rampart.lock() locks) while trying to fork.\n",
                    nlocked_elsewhere, nuser_locked_elsewhere);
                goto mark_stubborn_locks;
            }
            usleep(10000);
            nlocked_elsewhere=0;
            nuser_locked_elsewhere=0;
        }        
    }
    return;

    // Either because of a mistake somewhere, or more likely because a user left a JS based lock locked
    // in a thread, we need to do something about it.  Accessing a locked mutex from a non-surviving thread
    // after a fork is begging for a segfault.  So we need to be a little creative and just make a new one.
    // the rationale: Any function in a thread other than this one that holds a lock wil not be able
    //                to unlock it after the fork.  So in this process, we merely mark it and clear it later.
    //		      But in the child process we mark it, then discard it and make a new one, which for our
    //                purposes is the same as having it unlocked in the thread that doesn't survive the
    //                fork anyway.
    mark_stubborn_locks:

    thrlock=rampart_locks;
    while(thrlock)
    {
        if( (! RPTHR_TEST(thrlock, RPTHR_LOCK_FLAG_JSFIN) )
        	&&
            (
                (ISLOCKED && thrlock->thread_idx != thread_local_thread_num) //its from another thread
                    ||
                ISUNLOCKED
            )
        ){
            lres = _rp_trylock(thrlock,1);
            if(lres) //didn't get lock
                RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_REINIT);
            else
                RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_TEMP);//mark that we need to unlock this.
        }

        thrlock=thrlock->next;
    }
}

// user locking

#define check_lock_success(rval) do{\
    int r=(rval);\
    if(r==-100) RP_THROW(ctx,"Locking error: trying to unlock a lock was never locked");\
    else if(r) RP_THROW(ctx,"Locking error: %s", strerror(r));\
}while(0)

static duk_ret_t user_lock_finalizer(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;

    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));
    rp_user_lock=(RPTHR_LOCK *)duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(rp_user_lock)
        rp_remove_lock(rp_user_lock);

    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));

    return 0;
}


static duk_ret_t user_lock_lock(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));
    rp_user_lock = (RPTHR_LOCK *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    check_lock_success(rp_lock(rp_user_lock));
    return 0;
}

static duk_ret_t user_lock_unlock(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));
    rp_user_lock = (RPTHR_LOCK *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    check_lock_success(rp_unlock(rp_user_lock));
    return 0;
}

static duk_ret_t user_lock_trylock(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;
    int ret;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));
    rp_user_lock = (RPTHR_LOCK *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    ret = rp_trylock(rp_user_lock);

    if(ret==EBUSY)
        duk_push_false(ctx);
    else if(ret)
        check_lock_success(ret);
    else
        duk_push_true(ctx);

    return 1;
}

static duk_ret_t user_lock_get_thread(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));
    rp_user_lock = (RPTHR_LOCK *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    duk_push_int(ctx, rp_get_locking_thread(rp_user_lock));

    return 1;
}

static duk_ret_t new_user_lock(duk_context *ctx)
{
    RPTHR_LOCK *rp_user_lock=NULL;
    
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    duk_push_this(ctx);

    rp_user_lock=RP_MINIT(NULL);
    RPTHR_SET(rp_user_lock, RPTHR_LOCK_FLAG_JSLOCK);

    duk_push_pointer(ctx, rp_user_lock);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("RP_USER_LOCK"));

    duk_push_c_function(ctx, user_lock_lock, 0);
    duk_put_prop_string(ctx, -2, "lock");

    duk_push_c_function(ctx, user_lock_trylock, 0);
    duk_put_prop_string(ctx, -2, "trylock");

    duk_push_c_function(ctx, user_lock_unlock, 0);
    duk_put_prop_string(ctx, -2, "unlock");

    duk_push_c_function(ctx, user_lock_get_thread, 0);
    duk_put_prop_string(ctx, -2, "getLockingThread");

    duk_push_c_function(ctx, user_lock_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    return 1;
}


/* *************************************************
            END FUNCTIONS FOR LOCKS
**************************************************** */


/* *************************************************
    FUNCTIONS FOR COPYING BETWEEN DUK HEAPS/STACKS
**************************************************** */



/* get bytecode from self and execute it with
   js arguments to this function                */
duk_ret_t duk_rp_bytefunc(duk_context *ctx)
{
    duk_push_current_function(ctx);

    duk_get_prop_string(ctx, -1, "func"); //we stored function here

    duk_insert(ctx, 0);                   //put the bytecode compiled function at beginning
    duk_pop(ctx);                         //pop object "rp_global_function_stash"
    duk_push_this(ctx);                   // 'this' binding
    duk_insert(ctx, 1);                   // move 'this' to proper position
    duk_call_method(ctx, duk_get_top_index(ctx) - 1); //do it

    return 1;
}

/* Copy function compiled bytecode to stash of new stack.
   Create a function with the proper name that will
   call duk_rp_bytefunc() to execute that bytecode.

   duktape does not allow bytecode compiled function
   to be pushed to global object afaik
*/
void copy_bc_func(duk_context *ctx, duk_context *tctx)
{
    void *buf, *bc_ptr;
    duk_size_t bc_len;
    const char *name = duk_get_string(ctx, -2);

    
    /* get function bytecode from ctx, put it in a buffer in tctx */
    duk_dup_top(ctx);
    duk_dump_function(ctx);                             //dump function to bytecode
    bc_ptr = duk_get_buffer_data(ctx, -1, &bc_len); //get pointer to bytecode
    buf = duk_push_fixed_buffer(tctx, bc_len);          //make a buffer in thread ctx
    memcpy(buf, (const void *)bc_ptr, bc_len);          //copy bytecode to new buffer
    duk_pop(ctx);                                       //pop bytecode from ctx

    /* convert bytecode to a function in tctx */
    duk_dup(tctx, -1);
    duk_load_function(tctx);

    /* use wrapper c_func, store name and converted function in its properties */
    duk_push_c_function(tctx, duk_rp_bytefunc, DUK_VARARGS); //put our wrapper function on stack
    duk_push_string(tctx, name);                             // add desired function name (as set in global stash above)
    duk_put_prop_string(tctx, -2, "fname");
    duk_pull(tctx, -2);                                      // add actual function.
    duk_put_prop_string(tctx, -2, "func");
    duk_pull(tctx, -2);                                      // add actual function bytecode, for grandchildren.
    duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("bcfunc"));
    // it will be stored under global function name after return
}

static void copy_prim(duk_context *ctx, duk_context *tctx, const char *name)
{

    if (strcmp(name, "NaN") == 0)
        return;
    if (strcmp(name, "Infinity") == 0)
        return;
    if (strcmp(name, "undefined") == 0)
        return;

    switch (duk_get_type(ctx, -1))
    {
    case DUK_TYPE_NULL:
        duk_push_null(tctx);
        break;
    case DUK_TYPE_BOOLEAN:
        duk_push_boolean(tctx, duk_get_boolean(ctx, -1));
        break;
    case DUK_TYPE_NUMBER:
        duk_push_number(tctx, duk_get_number(ctx, -1));
        break;
    case DUK_TYPE_STRING:
        duk_push_string(tctx, duk_get_string(ctx, -1));
        break;
    default:
        duk_push_undefined(tctx);
    }
    duk_put_prop_string(tctx, -2, name);
}

void rpthr_clean_obj(duk_context *ctx, duk_context *tctx)
{
    const char *prev = duk_get_string(ctx, -2);
    /* prototype was not marked, so we need to dive into it regardless */
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("objRefId")) || (prev && !strcmp(prev, "prototype")))
    {
        duk_del_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("objRefId"));
        duk_enum(ctx, -2, DUK_ENUM_INCLUDE_HIDDEN|DUK_ENUM_INCLUDE_SYMBOLS);
        while (duk_next(ctx, -1, 1))
        {
            if (duk_is_object(ctx, -1) || duk_is_c_function(ctx, -1))
                rpthr_clean_obj(ctx, NULL);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);
    if (tctx)
    {
        duk_push_global_stash(tctx);
        duk_del_prop_string(tctx, -1, "objById");
        duk_pop(tctx);
    }
}

typedef duk_ret_t (*duk_func)(duk_context *);

//#define cprintf(...)  printf(__VA_ARGS__);
#define cprintf(...) /* nada */
//#define cprintf xxdprintf

// wants [ ..., object_idx, ..., empty_array ]

static void values_to_array(duk_context *ctx, duk_idx_t idx)
{
    duk_uarridx_t i=0;

    idx = duk_normalize_index(ctx, idx);

    duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY|DUK_ENUM_NO_PROXY_BEHAVIOR);
    // [ ..., object_idx, ..., empty_array, enum ]
    while (duk_next(ctx, -1 , 1 ))
    {
        // [ ..., object_idx, ..., empty_array, enum, key, value ]
        duk_put_prop_index(ctx, -4, i);
        // [ ..., object_idx, ..., array[value], enum, key]
        i++;
        duk_pop(ctx);
        // [ ..., object_idx, ..., array, enum ]
    }
    duk_pop(ctx);//enum
    // [ object_idx, ..., array ]
}



/* ctx object and tctx object should be at top of respective stacks */
int rpthr_copy_obj(duk_context *ctx, duk_context *tctx, int objid)
{
    const char *s;

    /* for debugging *
    const char *lastvar="global";
    if(duk_is_string(ctx, -2) )
        lastvar=duk_get_string(ctx, -2);
    printf("rpthr_copy_obj %s top=%d\n",lastvar, (int)duk_get_top(ctx));
    * end for debugging */


    objid++;
    const char *prev = duk_get_string(ctx, -2);

    /************** dealing with circular/duplicate references ******************************/
    /* don't copy prototypes, they will match the return from "new object();" and not be new anymore */
    if (prev && !strcmp(prev, "prototype"))
        goto enum_object;

    cprintf("IN COPYOBJ %d\n", objid);
    /* check if we've seen this object before */
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("objRefId")))
    { // [ [par obj], [ref_objid] ]
        /* get previously seen object (as previously copied to tctx, and return its reference */
        int ref_objid = duk_get_int(ctx, -1);
        cprintf("looking for cached object #%d\n", ref_objid);
        duk_pop(ctx); // [ [par obj] ]
        cprintf("Circular/Duplicate Reference to %s Detected\n", duk_get_string(ctx, -2));
        // replace the copied object on top of tctx stack with the reference
        duk_push_global_stash(tctx);                   // [ [par obj], [global stash] ]
        if (!duk_get_prop_string(tctx, -1, "objById")) // [ [par obj], [global stash], [object|undef] ]
        {
            fprintf(stderr, "big time error: this should never happen\n");
            // well, it did.  Unrelated memory corruption.  However we should exit
            exit(1);
            duk_pop_2(tctx); // [ [par obj] ]
            return (objid);  // leave empty object there
        }
        duk_push_sprintf(tctx, "%d", ref_objid); //[ [par obj], [global stash], [stash_obj], [objid] ]
        if( !duk_get_prop(tctx, -2) )                  //[ [par obj], [global stash], [stash_obj], [obj_ref] ]
        {
            // this really shouldn't happen either. If it does, we may infinitely recurse.
            duk_pop_3(tctx);
            cprintf("ref object not found in stash, going to enum_object\n");
            goto enum_object;
        }
        //printenum(tctx,-1);
        duk_insert(tctx, -4); //[ [obj_ref], [par obj], [global stash], [stash_obj] ]
        duk_pop_3(tctx);      //[ [obj_ref] ]
//if( !strcmp(lastvar,"\xffproxy_obj") ){
//    printf("########start##########\n");
//    printenum(tctx,-1);
//    printf("########end############\n");
//}
        cprintf("copied ref\n");
        return (objid); // stop recursion here.
    }
    duk_pop(ctx);

    /* if we get here, we haven't seen this object before so
       we label this object with unique number 
       If it is an array, we need to deal with it on the tctx stack only */
    if(!duk_is_array(ctx, -1))
    {
        duk_push_int(ctx, objid);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("objRefId"));
    }

    /* in tctx, put it where we can find it later */
    //[ [par obj] ]
    duk_push_global_stash(tctx);                   //[ [par obj], [global stash] ]
    if (!duk_get_prop_string(tctx, -1, "objById")) //[ [par obj], [global stash], [object|undef] ]
    {                                              //[ [par obj], [global stash], [undefined] ]
        duk_pop(tctx);                             //[ [par obj], [global stash] ]
        duk_push_object(tctx);                     //[ [par obj], [global stash], [new_stash_obj] ]
    }
    /* copy the object on the top of stack before
       entering rpthr_copy_obj */
    cprintf("assigning object id=%d\n", objid);
    duk_push_sprintf(tctx, "%d", objid);      //[ [par obj], [global stash], [stash_obj], [objid] ]
    duk_dup(tctx, -4);                        //[ [par obj], [global stash], [stash_obj], [objid], [copy of par obj] ]
    duk_put_prop(tctx, -3);                   //[ [par obj], [global stash], [stash_obj->{objid:copy of par obj} ]
    duk_put_prop_string(tctx, -2, "objById"); //[ [par obj], [global stash->{"objById":[stash_obj]} ]
    duk_pop(tctx);                            //[ [par obj] ]
    /************** END dealing with circular references ***************************/

    //    cprintf("%s type %d (obj=%d) cfunc=%d, func=%d, obj=%d, obj already in tctx=%d\n",((prev)?prev:"null"),duk_get_type(ctx,-1), DUK_TYPE_OBJECT,
    //            duk_is_c_function(ctx,-1), duk_is_function(ctx,-1), duk_is_object(ctx,-1), duk_has_prop_string( tctx,-1,s ));

    enum_object:

    /*  get keys,vals inside ctx object on top of the stack
        and copy to the tctx object on top of the stack     */

    duk_enum(ctx, -1, DUK_ENUM_INCLUDE_HIDDEN|DUK_ENUM_INCLUDE_SYMBOLS|DUK_ENUM_SORT_ARRAY_INDICES);
    while (duk_next(ctx, -1, 1))
    {
        s = duk_get_string(ctx, -2);
        /* this is an internal flags and should not be copied to threads */
        if(!strcmp(s, "\xffobjRefId") )
        {
            duk_pop_2(ctx);
            continue;
        }

        cprintf("copying %s\n",s);
        //special case where we are grandchild thread and this function was already wrapped with duk_rp_bytefunc().
        if (duk_is_function(ctx, -1) && duk_has_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bcfunc")))
        {
            const char *name;
            void *buf, *bc_ptr;
            duk_size_t bc_len;

            duk_get_prop_string(ctx, -1, "fname");
            name=duk_get_string(ctx, -1);
            duk_pop(ctx);

            duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("bcfunc"));
            bc_ptr = duk_get_buffer_data(ctx, -1, &bc_len);     //get pointer to bytecode
            buf = duk_push_fixed_buffer(tctx, bc_len);          //make a buffer in thread ctx
            memcpy(buf, (const void *)bc_ptr, bc_len);          //copy bytecode to new buffer
            duk_pop(ctx);                                       //pop bytecode from ctx

            /* convert bytecode to a function in tctx */
            duk_load_function(tctx);

            /* use wrapper c_func, store name and converted function in its properties */
            duk_push_c_function(tctx, duk_rp_bytefunc, DUK_VARARGS); //put our wrapper function on stack
            duk_push_string(tctx, name);                             // add desired function name (as set in global stash above)
            duk_put_prop_string(tctx, -2, "fname");
            duk_pull(tctx, -2);                                      // add actual function.
            duk_put_prop_string(tctx, -2, "func");
            duk_put_prop_string(tctx, -2, s);

            //TODO: need to recurse and copy other properties, if any.  But cannot copy func, fname, and bcfunc again
        }
        else
        if (duk_is_ecmascript_function(ctx, -1))
        {
            /* turn ecmascript into bytecode and copy */
            copy_bc_func(ctx, tctx);
            /* recurse and copy JS properties attached to this duktape (but really c) function */
            objid = rpthr_copy_obj(ctx, tctx, objid);

            if (!prev) // prev==NULL means this is a global
            {
                /* mark functions that are global as such to copy
                   by reference when named in a server.start({map:{}}) callback */
                duk_push_true(ctx);
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("is_global"));
                duk_push_true(tctx);
                duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("is_global"));
            }
            duk_put_prop_string(tctx, -2, s);
        }

        else if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_STRING | DUK_TYPE_MASK_NUMBER | DUK_TYPE_MASK_BOOLEAN | DUK_TYPE_MASK_NULL | DUK_TYPE_MASK_UNDEFINED))
        {
            /* simple copy of primitives */
            copy_prim(ctx, tctx, s);
        }

        else if (duk_is_c_function(ctx, -1) && !duk_has_prop_string(tctx, -1, s))
        {
            /* copy pointers to c functions */
            duk_idx_t length;
            duk_func copyfunc = (duk_func)duk_get_c_function(ctx, -1);

            if (duk_get_prop_string(ctx, -1, "length"))
            {
                length = (duk_idx_t)duk_get_int(ctx, -1);
                if(!length) length = DUK_VARARGS;
            }
            else
                length = DUK_VARARGS;
            duk_pop(ctx);

            duk_push_c_function(tctx, copyfunc, length);
            /* recurse and copy JS properties attached to this c function */

            objid = rpthr_copy_obj(ctx, tctx, objid);
            duk_put_prop_string(tctx, -2, s);
        }
        else if (duk_is_buffer_data(ctx, -1))
        {
            //no check for buffer type, just copy data into a plain buffer.
            duk_size_t sz;
            int variant=0;
            void *frombuf = duk_get_buffer_data(ctx, -1, &sz);
            void *tobuf;

            duk_inspect_value(ctx, -1);
            duk_get_prop_string(ctx, -1, "variant");
            variant = duk_get_int_default(ctx, -1, 0);
            duk_pop_2(ctx);
            if (variant == 2) //highly unlikely
                variant=0; //copy to fixed buffer

             tobuf = duk_push_buffer(tctx, sz, variant);
             memcpy(tobuf, frombuf, sz);

             duk_put_prop_string(tctx, -2, s);
        }
        else if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1) && !duk_is_c_function(ctx, -1))
        {
            /* check for date */
            if(duk_has_prop_string(ctx, -1, "getMilliseconds") && duk_has_prop_string(ctx, -1, "getUTCDay") )
            {
                duk_push_string(ctx, "getTime");
                //not a lot of error checking here.
                if(duk_pcall_prop(ctx, -2, 0)==DUK_EXEC_SUCCESS)
                {
                    duk_get_global_string(tctx, "Date");
                    duk_push_number(tctx, duk_get_number_default(ctx, -1, 0));
                    duk_new(tctx, 1);
                    duk_put_prop_string(tctx, -2, s);
                }
                duk_pop(ctx);
            }
            // check if it is an array, and if we've seen it before.
            // if not, get its pointer from the main stack, copy the array, store the array indexed by that pointer
            // if so, retrieve it by the pointer from the main stack
            // TODO: check if we want to use this ref tracking method for all objects. Might be cleaner.
            else if (duk_is_array(ctx, -1))
            {
                char ptr_str[32]; //plenty of room for hex of 64 bit ptr (17 chars with null)
                void *p = duk_get_heapptr(ctx, -1);

                snprintf(ptr_str,32,"%p", p); //the key to store/retrieve the array in thread_stack
                cprintf("copy %s[%p] - top:%d\n", s,p, (int)duk_get_top(tctx));
                // get the object holding the key->array in threadctx, create if necessary
                if(!duk_get_global_string(tctx, DUK_HIDDEN_SYMBOL("arrRefPtr")))
                {
                    duk_pop(tctx); // pop undefined from failed retrieval of arrRefPtr
                    duk_push_object(tctx); // new arrRefPtr object
                    duk_push_global_object(tctx); // global object
                    duk_dup(tctx, -2); //copy ref to new arrRefPtr object
                    duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("arrRefPtr")); //store the copy ref
                    duk_pop(tctx); //pop global object, left with ref to new arrRefPtr
                } // else we got arrRefPtr on top of stack

                // check if we've seen this array before
                if(duk_get_prop_string(tctx, -1, ptr_str))
                {
                    //we found our array, but need to remove arrRefPtr
                    cprintf("copy array, found ref\n");
                    duk_remove(tctx, -2);
                }
                else
                {
                    // tctx -> [ ..., arrRefPtr, undefined ]
                    cprintf("copy array, NO ref\n");
                    duk_pop(tctx); //undefined

                    /* recurse and begin again with this ctx object (at idx:-1)
                       and a new empty object for tctx (pushed to idx:-1)              */
                    // tctx -> [ ..., arrRefPtr ]
                    duk_push_array(tctx); // in order to avoid endless loop in rpthr_copy_obj below, need to store this now
                    duk_dup(tctx, -1);
                    // tctx -> [ ..., arrRefPtr, array, dup_array ]
                    duk_put_prop_string(tctx, -3, ptr_str); //[ ..., arrRefPtr, array ]

                    duk_push_object(tctx); 
                    objid = rpthr_copy_obj(ctx, tctx, objid);
                    // tctx -> [ ..., arrRefPtr, array, copied_object ]
                    duk_pull(tctx, -2);
                    // tctx -> [ ..., arrRefPtr, copied_object, array ]
                    values_to_array(tctx, -2);

                    duk_remove(tctx, -2);
                    // tctx -> [ ... arrRefPtr, copied_array ] 

                    duk_remove(tctx, -2);
                    // tctx -> [ ..., copied_array ]
                }

                duk_put_prop_string(tctx, -2, duk_get_string(ctx, -2));
            }
            /* copy normal {} objects */
            else if (!duk_has_prop_string(tctx, -1, s) &&
                strcmp(s, "console") != 0 &&
                strcmp(s, "performance") != 0)
            {
                cprintf("copy %s{}\n", s);
                /* recurse and begin again with this ctx object (at idx:-1)
                   and a new empty object for tctx (pushed to idx:-1)              */
                duk_push_object(tctx);
                objid = rpthr_copy_obj(ctx, tctx, objid);
                // convert back to array if ctx object was an array

                duk_put_prop_string(tctx, -2, duk_get_string(ctx, -2));
            }
        }
        else if (duk_is_pointer(ctx, -1) )
        {
            duk_push_pointer(tctx, duk_get_pointer(ctx, -1) );
            duk_put_prop_string(tctx, -2, s);
        }

        /* remove key and val from stack and repeat */
        duk_pop_2(ctx);
    }

    /* remove enum from stack */
    duk_pop(ctx);

    // flag object as copied, in case needed by some function
    duk_push_true(tctx);
    duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("thread_copied"));

    if (duk_has_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("proxy_obj")) )
    {
        //printenum(tctx, -1);
        duk_get_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("proxy_obj"));
        duk_push_proxy(tctx, -1);
        //printf("--------------------------------------------\n");
        //printenum(tctx, -1);
    }
    /* keep count */
    return objid;
}

void rpthr_copy_global(duk_context *ctx, duk_context *tctx)
{
    duk_push_global_object(ctx);
    duk_push_global_object(tctx);
    /* rpthr_copy_obj expects objects on top of both stacks */
    rpthr_copy_obj(ctx, tctx, 0);
    /* remove hidden symbols and stash-cache used for reference tracking */

    rpthr_clean_obj(ctx, tctx);
    /* remove global object from both stacks */
    duk_pop(ctx);
    duk_del_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("arrRefPtr") ); 
    duk_pop(tctx);
}

/* ******************************************
  END FUNCTIONS FOR COPYING BETWEEN DUK HEAPS
********************************************* */

/* ******************************************
  START FUNCTIONS FOR COPY/PASTE PUT AND GET
********************************************* */

//#define CPLOCK do{ RP_MLOCK(rp_cp_lock); printf("LOCKing on line %d, lock=%p\n", __LINE__, rp_cp_lock);}while(0)
//#define CPUNLOCK do{ RP_MUNLOCK(rp_cp_lock); printf("UNlocking on line %d, lock=%p\n", __LINE__, rp_cp_lock);}while(0)

#define CPLOCK RP_MLOCK(rp_cp_lock)
#define CPUNLOCK RP_MUNLOCK(rp_cp_lock)

duk_ret_t rp_thread_put(duk_context *ctx)
{
    const char *key = REQUIRE_STRING(ctx, 0, "thread_put: First argument must be a string (key)");

    duk_push_object(ctx);
    duk_pull(ctx, 1);
    duk_put_prop_string(ctx, -2, "val");

    CPLOCK;
    if(cpctx==NULL)
        cpctx=duk_create_heap(NULL, NULL, NULL, NULL, duk_rp_fatal);

    if(!duk_get_global_string(cpctx, "allvals"))
    {
        duk_push_object(cpctx);
        duk_dup(cpctx, -1);
        duk_put_global_string(cpctx, "allvals");
    }
    //cpctx: [ allvals_obj(global) ]

    duk_push_object(cpctx);
    //cpctx: [ allvals_obj, holder_object ]

    //copy object with key "val" to new cpctx object
    rpthr_copy_obj(ctx, cpctx, 0);
    rpthr_clean_obj(ctx, cpctx);
    //cpctx: [ allvals_obj, holder_object_filled ]

    duk_put_prop_string(cpctx, -2, key);
    //cpctx: [ allvals_obj(with-holder) ]

    RP_EMPTY_STACK(cpctx);
    //cpctx: []
    CPUNLOCK;

    return 0;
}

duk_ret_t rp_thread_get(duk_context *ctx)
{
    const char *key = REQUIRE_STRING(ctx, 0, "thread_get: First argument must be a string (key)");

    duk_push_object(ctx);

    CPLOCK;

    if(cpctx==NULL)
    {
        CPUNLOCK;
        duk_push_undefined(ctx);
        return 1;
    }

    if(!duk_get_global_string(cpctx, "allvals"))
    {
        duk_pop(cpctx);
        CPUNLOCK;
        return 0;
    }

    //cpctx: [ allvals_obj(global) ]
    if( !duk_get_prop_string(cpctx, -1, key) )
    {
        RP_EMPTY_STACK(cpctx);
        CPUNLOCK;
        return 0;
    }
    //cpctx: [ allvals_obj, holder_object_filled ]

    //copy object with key "val" to ctx object
    rpthr_copy_obj(cpctx, ctx, 0);
    rpthr_clean_obj(cpctx, ctx);
    RP_EMPTY_STACK(cpctx);
    //cpctx: []

    CPUNLOCK;
    
    duk_get_prop_string(ctx, -1, "val");
    return 1;
}

duk_ret_t rp_thread_del(duk_context *ctx)
{
    const char *key = REQUIRE_STRING(ctx, 0, "thread_del: First argument must be a string (key)");

    duk_push_object(ctx);

    CPLOCK;

    if(cpctx==NULL)
    {
        CPUNLOCK;
        duk_push_undefined(ctx);
        return 1;
    }

    if(!duk_get_global_string(cpctx, "allvals"))
    {
        duk_pop(cpctx);
        CPUNLOCK;
        duk_push_undefined(ctx);
        return 1;
    }

    //cpctx: [ allvals_obj(global) ]
    duk_get_prop_string(cpctx, -1, key);
    //cpctx: [ allvals_obj, holder_object_filled ]

    //copy object with key "val" to ctx object
    rpthr_copy_obj(cpctx, ctx, 0);
    rpthr_clean_obj(cpctx, ctx);

    duk_pop(cpctx);
    //cpctx: [ allvals_obj(global) ]
    duk_del_prop_string(cpctx, -1, key); //the delete
    duk_pop(cpctx);
    //cpctx: []
    CPUNLOCK;
    
    duk_get_prop_string(ctx, -1, "val");
    return 1;
}

/* ******************************************
  END FUNCTIONS FOR COPY/PASTE PUT AND GET
********************************************* */

/* ******************************************
  START FUNCTIONS FOR CREATING NEW THREADS
********************************************* */

static RPTHR* get_first_unused()
{
    int i=0;

    for (i=0;i<nrpthreads;i++)
    {
        if( ! RPTHR_TEST(rpthread[i], RPTHR_FLAG_IN_USE) )
            return rpthread[i]; 
    }
   
    return NULL;
}

static duk_context *new_context(RPTHR *thr)
{
    duk_context *pctx=NULL, *tctx = duk_create_heap(NULL, NULL, NULL, NULL, duk_rp_fatal);
    int thrno = get_thread_num();

    duk_init_context(tctx);  //fresh copy of all the rampart extra goodies

    //creating a thread inside another thread that is not the main thread.  proceed with caution.
    // we definitely don't want to use main_ctx, cuz no amount of locking will help.
    if(thrno)
        pctx = rpthread[thrno]->ctx;
    // main_ctx is created first. If main_ctx is NULL, we are right now creating it.
    // so don't copy to self
    else if (main_ctx)
        pctx=main_ctx;

    if(pctx)
    {
        rpthr_copy_global(pctx, tctx);
        // we don't want copied rampart events
        duk_push_object(tctx);
        duk_put_global_string(tctx, DUK_HIDDEN_SYMBOL("jsevents"));
    }

    // add some info in each thread
    if (!duk_get_global_string(tctx, "rampart"))
    {
        fprintf(stderr, "Failed to get rampart global\n"); //this will never happen
        exit(1);
    }
    duk_push_int(tctx, (int)thr->index);
    duk_put_prop_string(tctx, -2, "thread_id");

    RP_EMPTY_STACK(tctx);

    return tctx;
}


static void remove_from_parent(RPTHR *thr)
{
    RPTHR *pthr=thr->parent;
    int i=0, lasti=-1;

    if(!pthr)
        return;
    // several children could be attempting to do this at once
    dprintf("thread %d has %d children, removing %p\n", (int)pthr->index, pthr->nchildren, thr);
    for(i=0; i<pthr->nchildren; i++)
    {
        if(lasti > -1)
        {
            //copy over the entry for thr and splice the rest
            pthr->children[lasti]=pthr->children[i];
            lasti=i;
        }   
        else if(pthr->children[i] == thr)
            lasti=i;  //if last loop, no matter, we leave it there and decrement count anyway.  It will be freed/realloced later
    }

    pthr->nchildren--;
    dprintf("now have %d children\n", pthr->nchildren);
} 

void rp_close_thread(RPTHR *thr)
{
    int i=0;
    
    dprintf("CLOSE THREAD, thread=%d, base=%p\n", (int)thr->index, thr->base);
    thr->flags=0; //reset all flags

    if(! RPTHR_TEST(rpthread[i], RPTHR_FLAG_IN_USE))
        return;

    if(thr->htctx)
    {
        duk_destroy_heap(thr->htctx);
        thr->htctx=NULL;
    }

    if(thr->wsctx)
    {
        duk_destroy_heap(thr->wsctx);
        thr->wsctx=NULL;
    }

    thr->ctx=NULL; //it was the same as one of the above, so no free.

    if(thr->dnsbase)
        evdns_base_free(thr->dnsbase, 0);
    //RPTHR_CLEAR(thr, RPTHR_FLAG_BASE);
    thr->dnsbase=NULL;
    event_base_free(thr->base);
    thr->base=NULL;

    free(thr->children);
    thr->children=NULL;
    thr->nchildren=0;
    dprintf("STILL CLOSE THREAD\n");
    if(thr->fin_cb && thr->ncb)
    {

        for (;i<thr->ncb;i++)
        {
            //run callback with user arg
            (thr->fin_cb[i])(thr->fin_cb_arg[i]);
            free(thr->fin_cb_arg[i]);
        }
        free(thr->fin_cb);
        free(thr->fin_cb_arg);

        thr->fin_cb=NULL;
        thr->fin_cb_arg=NULL;
        thr->ncb=0;
    }
    dprintf("CLOSE THREAD -- remove from parent\n");
    remove_from_parent(thr);
    //RPTHR_CLEAR(thr, RPTHR_FLAG_IN_USE);
    //RPTHR_CLEAR(thr, RPTHR_FLAG_FINAL );
    //thr->flags=0;
}

/* what to do after fork:  
    - Actual pthreads are gone, clean up the mess
    - Make current thread thread 0
    - change main_ctx to thr[cur]->ctx
    - set thread_local_thread_num = 0 
    - close others
*/

RPTHR **saved_threads=NULL;

void rp_post_fork_clean_threads()
{
    //int i=0, curthr_idx=get_thread_num();
    duk_context *ctx=NULL;

    // we cannot safely destroy the duk_contexts as that will trigger finalizers
    // which should not be run twice in two different processes, or before we are ready.  
    // Best to just leave it somewhere, and make it addressable so valgrind doesn't complain
    
    //printf("post_fork htctx=%p, wsctx=%p, thrno=%d\n", (get_current_thread())->htctx, (get_current_thread())->wsctx, get_thread_num());
    
    saved_threads = rpthread;
    ctx=(get_current_thread())->htctx;
    (get_current_thread())->htctx=NULL;
    rpthread=NULL;
    nrpthreads=0;

    // Unlocking a mutex that isn't locked results in "undefined behavior"
    // Works fine on linux.
    // Unlocking a mutex which wasn't locked in this thread is not good.
    // Locking or unlocking a mutex held by a thread that has disappeared after fork
    //   will cause all kinds of bad things (linux kernel sends sigterm and reports it in syslog).  
    // rp_claim_all_locks() uses pthread_mutex_trylock() and waits for locks to be held in the current thread. 
    // And now that we have them and we have forked:
    rp_unlock_all_locks(0);

    // make a new thread structure for this thread, and replace mainthr
    mainthr=rp_new_thread(RPTHR_FLAG_THR_SAFE, ctx);
    mainthr->base = event_base_new();
    mainthr->dnsbase=rp_make_dns_base(mainthr->ctx, mainthr->base);
    RPTHR_SET(mainthr, RPTHR_FLAG_BASE);
    thread_local_thread_num = 0;

    return;
}


struct evdns_base *rp_make_dns_base(duk_context *ctx, struct event_base *base)
{
    /* each thread can/should (but doesn't have to) have a dnsbase.
       If not provided rampart-net will make one for each dns request.
       It should be made before the event loop starts */

    struct evdns_base *dnsbase = evdns_base_new(base,
        EVDNS_BASE_DISABLE_WHEN_INACTIVE );

    if(!dnsbase)
        RP_THROW(ctx, "rampart (new thread) - error creating dnsbase");

    evdns_base_resolv_conf_parse(dnsbase, DNS_OPTIONS_ALL, "/etc/resolv.conf");

    return dnsbase;
} 


RPTHR *rp_new_thread(uint16_t flags, duk_context *ctx)
{
    THRLOCK;
    uint16_t thread_no=nrpthreads;
    RPTHR *ret=NULL;

    if( !(ret=get_first_unused()) )
    {
        if(nrpthreads == RP_MAX_THREADS)
        {
            THRUNLOCK;
            return NULL;
        }

        nrpthreads++;
        REMALLOC(rpthread, sizeof(RPTHR*) * nrpthreads );
        REMALLOC(ret, sizeof(RPTHR) );
        rpthread[thread_no]=ret;
        ret->index=(uint16_t)thread_no;
    }
    ret->flags=flags;

    RPTHR_SET(ret, RPTHR_FLAG_IN_USE);
    dprintf("CREATED THR %d\n", thread_no);

    if(ctx)
        ret->htctx=ctx;
    else
        ret->htctx=new_context(ret);

    ret->ctx=ret->htctx;  //start with htctx, switch temporarily if in websockets.
    ret->fin_cb=NULL;
    ret->fin_cb_arg=NULL;
    ret->ncb=0;
    ret->base=NULL;
    ret->dnsbase=NULL;
    ret->wsctx=NULL;
    ret->parent=NULL;
    ret->children=NULL;
    ret->nchildren=0;
    if(ret->index !=0 ) //if not main thread
    {
        //set our parent thread
        RPTHR *pthr = get_current_thread();
        int pindex=pthr->nchildren;

        ret->parent = pthr;

        // set self as a child of parent
        pthr->nchildren++;
        REMALLOC(pthr->children, sizeof(RPTHR*) * pthr->nchildren);

        pthr->children[pindex]=ret;
        dprintf("Added child %d to %d\n", ret->index, pthr->index);
    }

    if(!ret->ctx) {
        THRUNLOCK;
        return NULL;
    }
    if( RPTHR_TEST(ret, RPTHR_FLAG_SERVER) )
        ret->wsctx=new_context(ret);

    THRUNLOCK;

    return ret;
}

/* ******************************************
  END FUNCTIONS FOR CREATING NEW THREADS
********************************************* */

/* ******************************************
  START JS FUNCTIONS FOR CREATE/EXEC THREADS
********************************************* */


static uint32_t cbidx=0;
#define RPTINFO struct rp_thread_info

RPTINFO {
    void              *cbor;            // cbor data for args and return vals
    size_t             cbor_sz;         // size of above
    void              *bytecode;        // bytecode of function executed in child thread
    size_t             bytecode_sz;     // size of above
    int32_t            index;           // growing number to make key for parent callback function in hidden object 
    struct event      *e;               // event for both parent and child
    RPTHR             *thr;             // rampart thread of child
    RPTHR             *parent_thr;      // current rampart thread (or parent thread if in child)
};


static struct timeval immediate={0,0};
static struct timeval fiftyms={0,50000};

static RPTINFO *clean_info(RPTINFO *info)
{
    if(!info)
        return info;
    if(info->bytecode)
        free(info->bytecode);
    if(info->cbor)
        free(info->cbor);
    if(info->e)
    {
        event_del(info->e);
        event_free(info->e);
    }

    free(info);
    return NULL;
}

// callback (if exists) in parent thread 
static void do_parent_callback(evutil_socket_t fd, short events, void* arg)
{
    RPTINFO *info     = (RPTINFO *)arg;
    RPTHR   *thr      = info->parent_thr;
    duk_context *ctx  = thr->ctx;
    char objkey[16];

    sprintf(objkey,"c%lu",(unsigned long)info->index);
    if( !duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rp_thread_callbacks")) )
        RP_THROW(ctx, "rampart.thread: callbacks in parent thread have disappeared"); //this won't happen (i hope)

    // the stored parent callback function
    duk_get_prop_string(ctx, -1, objkey);
    duk_del_prop_string(ctx, -2, objkey);  //remove it.  We only need it once.
    // the return value
    if(info->cbor && info->cbor_sz)
    {
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, info->cbor, (duk_size_t) info->cbor_sz);
        duk_cbor_decode(ctx, -1, 0);

        if(duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, "error"))
        {
            //I think cbor will make error txt a buffer if non-utf8 chars are present
            duk_get_prop_string(ctx, -1, "error");
            if(duk_is_buffer_data(ctx, -1))
            {
                duk_buffer_to_string(ctx, -1);
                duk_put_prop_string(ctx, -2, "error");
            }
            else duk_pop(ctx);
        }
    }
    else
        duk_push_undefined(ctx);

    info=clean_info(info);

    if(!duk_is_function(ctx, -2))
    {
        dprintf("failed to get function indexed at %s\n", objkey);
        safeprintstack(ctx);
        RP_THROW(ctx,"CALLBACK FUNCTION INDEX LOOKUP ERROR");
    }

    dprintf("running parent javascript callback\n");

    duk_call(ctx, 1);
    RP_EMPTY_STACK(ctx);
}

// execute in thread, from event loop
static void thread_doevent(evutil_socket_t fd, short events, void* arg)
{
    RPTINFO *info     = (RPTINFO *)arg;
    RPTHR   *thr      = info->thr;
    duk_context *ctx  = thr->ctx;
    void *buf=NULL;
    duk_size_t len;
    struct event *tev=info->e;

    buf = duk_push_fixed_buffer(ctx, info->bytecode_sz);
    memcpy(buf, (const void *)info->bytecode, info->bytecode_sz);
    duk_load_function(ctx);

    if(info->cbor && info->cbor_sz)
    {
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, info->cbor, (duk_size_t) info->cbor_sz);
        duk_cbor_decode(ctx, -1, 0);

    }
    else
        duk_push_undefined(ctx);

    dprintf("running thread javascript callback, base =%p\n", thr->base);

    RPTHR_SET(thr, RPTHR_FLAG_ACTIVE);
    if(duk_pcall(ctx,1) != 0)
    {
        RPTHR_CLEAR(thr, RPTHR_FLAG_ACTIVE);
        duk_push_object(ctx);
        if (duk_is_error(ctx, -2) )
            duk_get_prop_string(ctx, -2, "stack");
        else if (duk_is_string(ctx, -2))
            duk_pull(ctx, -2);
        else
            duk_push_string(ctx,"Unspecified in thread callback");

        //if we don't have a parent thread callback
        if(info->index == -1)
        {
            fprintf(stderr, "Error in thread without callback: %s\n", duk_get_string(ctx, -1));
            goto end;
        }
        duk_put_prop_string(ctx, -2, "error");

    }
    RPTHR_CLEAR(thr, RPTHR_FLAG_ACTIVE);

    //error or results is at idx: -1

    //if no parent callback, discard js return value
    dprintf("checking if we have a callback - %s\n", (info->index == -1? "no": "yes"));
    if(info->index == -1)
        goto end;

    //encode and store return in info->cbor, if defined
    if(duk_is_undefined(ctx, -1))
    {
        info->cbor_sz = 0;
        free(info->cbor);
        info->cbor=NULL;
    }
    else
    {
        duk_cbor_encode(ctx, -1, 0);
        buf = duk_get_buffer_data(ctx, -1 ,&len);
        info->cbor_sz = (size_t)len;
        REMALLOC(info->cbor, info->cbor_sz);
        memcpy(info->cbor, buf, info->cbor_sz);       
    }
    dprintf("inserting event for parent callback for thread %d\n",info->parent_thr->index);
    info->e=event_new(info->parent_thr->base, -1, 0, do_parent_callback, info);
    event_add(info->e, &immediate);


    end:
    // if not doing parent callback
    if(info->index==-1)
        info=clean_info(info); //includes event_free((tev|info->e));
    else
        event_free(tev);

    RP_EMPTY_STACK(ctx);
}
#define RPTCLOSER struct rp_thread_closer

RPTCLOSER {
    struct event      *e;               // event used for closing
    RPTHR             *thr;             // rampart thread of child
    duk_context       *ctx;
};

// an event run inside a thread to shut down the thread
static void finalize_event(evutil_socket_t fd, short events, void* arg)
{
    RPTCLOSER *closer=(RPTCLOSER*)arg;
    int npending=0, rpevents_pending=0;
    struct event_base *base=event_get_base(closer->e);
    RPTHR *thr = closer->thr;
    duk_context *ctx = thr->ctx;

    event_del(closer->e);

    if( !RPTHR_TEST(closer->thr, RPTHR_FLAG_IN_USE) )
    {
        dprintf("ALREADY finalized in finalize_event\n");
        free(closer);
        return;
    }
    
    /* check for pending rampart.event */
    if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("jsevents")) )
    {
        duk_enum(ctx,-1,0);
        while(duk_next(ctx, -1, 1))
        {
            //enum on value
            duk_enum(ctx, -1, 0);
            if(duk_next(ctx, -1, 0))
            {
                //printf("got pending %s in thread %d\n", duk_to_string(ctx, -1), get_thread_num());
                rpevents_pending=1;
                duk_pop_n(ctx, 4); //inner key, inner enum ,key, val
                break;
            }

            duk_pop_3(ctx);//inner enum, key,val
        }
        duk_pop(ctx);//enum
    }
    duk_pop(ctx);//jsevents or undefined

    //printf("rp pending = %d\n", rpevents_pending);
    npending= rpevents_pending + event_base_get_num_events(base, EVENT_BASE_COUNT_ADDED);// +
              //event_base_get_num_events(base, EVENT_BASE_COUNT_VIRTUAL) +
              //event_base_get_num_events(base, EVENT_BASE_COUNT_ACTIVE);

    // if we have pending events, or parent thread is active in JS and might add events, keep going
    if(!npending && !RPTHR_TEST(closer->thr->parent, RPTHR_FLAG_ACTIVE))
    {
        RPTHR *thr = closer->thr;
        //int cadd, cvirt, cact;
        // check if any child threads left in use
        if(thr->nchildren != 0) {
            dprintf("still have %d children\n", thr->nchildren);
            goto goagain;
        }
        event_free(closer->e);

        free(closer);
        dprintf("CLOSING thr %d\n", (int)thr->index);
        THRLOCK;
        rp_close_thread(thr);
        THRUNLOCK;
        pthread_exit(NULL);
        return;
    }

    goagain:
        event_add(closer->e, &fiftyms);
}

// insert finalizers if none have been inserted already.
int rp_thread_close_children()
{
    int i=1, ret=0;
    RPTCLOSER *closer=NULL;
    RPTHR *thr;
    for (i=1;i<nrpthreads;i++)
    {
        thr = rpthread[i];
        if(
            RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) && 
            RPTHR_TEST(thr, RPTHR_FLAG_BASE  ) &&
         !  RPTHR_TEST(thr, RPTHR_FLAG_FINAL ) &&
         !  RPTHR_TEST(thr, RPTHR_FLAG_SERVER)    ) //server threads never exit
        {
            RPTHR_SET(thr, RPTHR_FLAG_FINAL);
            closer=NULL;
            REMALLOC(closer, sizeof(RPTCLOSER));

            closer->thr=thr;
            closer->e=event_new(thr->base, -1, 0, finalize_event, closer);
            dprintf("MANUALLY ADDING event finalizer for thread %d, base %p\n",thr->index, thr->base);
            event_add(closer->e, &immediate);
            ret=1;
        }
        else if(RPTHR_TEST(thr, RPTHR_FLAG_IN_USE))
            dprintf ("%d NOT in use, RPTHR_FLAG_BASE=%d\n", i, !!RPTHR_TEST(thr, RPTHR_FLAG_BASE));
    }    
    return ret;
}

// insert event that will shut it down from inside
// if called directly, 'this' must be on stack idx 0
// if triggered by finalizer, 'this' is already present
// BUT -- don't insert in loop if already inserted in rp_thread_close_children() above
static duk_ret_t finalize_thr(duk_context *ctx)
{
    RPTCLOSER *closer=NULL;
    RPTHR *thr=NULL;
    int thrno = 0;

    if(!duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("thr")))
    {
        duk_pop(ctx);
        return 0;
    }

    thrno=duk_get_int(ctx, -1);
    duk_pop(ctx);

    if(thrno >= nrpthreads)
        return 0;

    thr=rpthread[thrno];

    if( ! thr || 
        ! RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) 
    )
    {
        dprintf("ALREADY finalized in finalizer_thr\n");
        return 0;
    }

    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("thr"));

    if(!RPTHR_TEST(thr, RPTHR_FLAG_FINAL ) && thr->base )
    {
        RPTHR_SET(thr, RPTHR_FLAG_FINAL);
        REMALLOC(closer, sizeof(RPTCLOSER));

        closer->thr=thr;
        duk_push_undefined(ctx);
        duk_set_finalizer(ctx, 0);
        closer->e=event_new(closer->thr->base, -1, 0, finalize_event, closer);
        dprintf("adding closer to base %p\n", closer->thr->base);
        event_add(closer->e, &immediate);
    }
    return 0;
}


// insert into event loop
static duk_ret_t loop_insert(duk_context *ctx)
{
    duk_size_t len=0;
    duk_idx_t cbfunc_idx=-1, arg_idx=-1, to_idx=-1;
    RPTINFO *info=NULL;
    RPTHR *thr=NULL, *curthr=get_current_thread();
    int thrno=-1;
    void *buf;
    char objkey[16];
    struct timeval timeout;
    duk_idx_t i=1;

    timeout.tv_sec=0;
    timeout.tv_usec=0; 


    REQUIRE_FUNCTION(ctx, 0, "thr.exec: first argument must be a function");

    for(i=1; i<4; i++)
    {
        if(duk_is_function(ctx,i))
        {
            if(cbfunc_idx == -1)
                cbfunc_idx=i;
            else
                RP_THROW(ctx, "thr.exec: Too many functions as arguments.  Cannot pass a function as an argument to the thread function.");
        }
        else if(arg_idx!=-1 && duk_is_number(ctx, i))
            to_idx=i;
        else if(!duk_is_undefined(ctx,i))
            arg_idx=i;
        else if(!duk_is_undefined(ctx, i))
            RP_THROW(ctx, "thr.exec: unknown/unsupported argument #%d", i+1);
    }

    //setup
    if(to_idx!=-1)
    {
        double to=duk_get_number(ctx, to_idx)/1000.0;

        timeout.tv_sec = (time_t) to;
        timeout.tv_usec = (suseconds_t) ( (to-(double)timeout.tv_sec) * 1000000.0 );
    }

    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thr")))
    {
        thrno=duk_get_int(ctx, -1);
        thr=rpthread[thrno];
    }
    duk_pop(ctx); //int

    if(thr==NULL)
        RP_THROW(ctx, "thr.exec: thread has terminated");

    if(thr->parent != curthr) //this might otherwise be fine, but messes up our thread/loop dependencies.
    {
        dprintf("failed exec, cannot run a thread created outside the current one\n");
        RP_THROW(ctx, "thr.exec: Cannot run a thread created outside the current one.");
    }
    REMALLOC(info, sizeof(RPTINFO));
    info->cbor=NULL;
    info->bytecode=NULL;
    info->cbor_sz=0;
    info->bytecode_sz=0;
    info->index=-1;
    info->parent_thr=curthr;

    if(duk_is_c_function(ctx, 0) && !duk_has_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("bcfunc")))
    {
        RP_THROW(ctx,   "thr.exec - Directly passing of built-in c functions not supported.  Wrap it in a JavaScript function.\n"
                        "    Example:\n"
                        "       thr.exec(console.log, 'hello world'); //illegal, throws this error\n"
                        "       thr.exec(function(arg){console.log(arg);}, 'hello world'); //works with wrapper\n"
        );
    } else {
        //copy bytecode to struct
        if( duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("bcfunc")) )
        {
            // this was already copied (we are grandchild thread)
            buf = duk_get_buffer_data(ctx, -1, &len);
            duk_pop(ctx);
        } else {
            duk_pop(ctx); //undef
            duk_dup(ctx, 0);
            duk_dump_function(ctx);
            buf = duk_get_buffer_data(ctx, -1, &len);
        }

        info->bytecode_sz=(size_t)len;
        REMALLOC(info->bytecode, info->bytecode_sz);
        memcpy(info->bytecode, buf, info->bytecode_sz);
    }
    //copy arg to struct
    if(arg_idx != -1)
    {
        duk_cbor_encode(ctx, arg_idx, 0);
        buf = duk_get_buffer_data(ctx, arg_idx ,&len);
        info->cbor_sz = (size_t)len;
        REMALLOC(info->cbor, info->cbor_sz);
        memcpy(info->cbor, buf, info->cbor_sz);       
    }

    //insert function into function holding object, indexed by info->index
    if(cbfunc_idx!=-1)
    {
        int ix;

        THRLOCK;
        ix = cbidx++;
        THRUNLOCK;

        sprintf(objkey,"c%d", ix);
        if( !duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rp_thread_callbacks")) )
        {
            duk_push_object(ctx);
            duk_dup(ctx, -1);
            duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("rp_thread_callbacks"));
        }
        duk_pull(ctx, cbfunc_idx);
        duk_put_prop_string(ctx, -2, objkey);
        info->index=(int32_t)ix;
    }

    // add our thr struct;
    info->thr=thr;

    //check as late as possible if our thread closed
    if(!RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) || !thr->base || !RPTHR_TEST(thr, RPTHR_FLAG_BASE))
    {
        if(info->cbor)
            free(info->cbor);
        free(info);
        RP_THROW(ctx, "thr.exec: Thread has already been closed");
    }
    //insert into thread's event loop
    info->e = event_new(thr->base, -1, 0, thread_doevent, info);
    dprintf("adding info event(%p) to base(%p)\n",info->e, thr->base);
    event_add(info->e, &timeout);

    return 0;    
}

static void *do_thread_setup(void *arg)
{
    RPTHR *thr=(RPTHR*)arg;

    //set the thread_local variable so we can always find our thr struct
    set_thread_num(thr->index);

    //start event loop
    dprintf("START event loop base(%p) for %d %d\n", thr->base, thr->index, thread_local_thread_num);
    event_base_loop(thr->base, EVLOOP_NO_EXIT_ON_EMPTY);

    return NULL;
}

static duk_ret_t close_thread(duk_context *ctx)
{
    duk_push_this(ctx);
    (void)finalize_thr(ctx);

    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thr"));

    return 0;
}

static duk_ret_t new_js_thread(duk_context *ctx)
{
    RPTHR *thr;
    pthread_attr_t attr;

    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    thr = rp_new_thread(0,NULL);
    if(!thr)
        RP_THROW(ctx, "rampart.thread.new: error creating new thread");

    // make the base
    thr->base = event_base_new();
    thr->dnsbase=rp_make_dns_base(thr->ctx, thr->base);
    RPTHR_SET(thr, RPTHR_FLAG_BASE);
    dprintf("BASE NEW\n");

    duk_push_this(ctx); //return object
    duk_push_int(ctx, (int)thr->index);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thr"));

    duk_push_c_function(ctx, finalize_thr, 1);
    duk_set_finalizer(ctx, -2);

    duk_push_c_function(ctx, loop_insert, 4);
    duk_put_prop_string(ctx, -2, "exec");

    duk_push_c_function(ctx, close_thread,0);
    duk_put_prop_string(ctx, -2, "close");

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);


    // create the thread, which will start the loop

    if( pthread_create(&(thr->self), &attr, do_thread_setup, (void*)thr) )
        RP_THROW(ctx, "Could not create thread\n");

    return 0;
}

/* ******************************************
  END JS FUNCTIONS FOR CREATE/EXEC THREADS
********************************************* */


duk_ret_t get_thread_id(duk_context *ctx)
{
    duk_push_int(ctx, get_thread_num());
    return 1;
}

//called in cmdline.c
void rp_thread_preinit()
{
    RP_PTINIT(&thr_list_lock);
    rp_cp_lock=RP_MINIT(&cp_lock);
}



/**** init function executed in main thread upon startup *****/

void duk_thread_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    duk_push_c_function(ctx, new_js_thread, 0);

    duk_push_c_function(ctx, get_thread_id, 0);
    duk_put_prop_string(ctx, -2, "getId");

    duk_push_c_function(ctx, rp_thread_put, 2);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, rp_thread_get, 1);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, rp_thread_del, 1);
    duk_put_prop_string(ctx, -2, "del");

    duk_put_prop_string(ctx, -2, "thread");

    duk_push_c_function(ctx, new_user_lock, 0);
    duk_put_prop_string(ctx, -2, "lock");

    duk_put_global_string(ctx,"rampart");

}

/* getting, setting thread local thread number */

void set_thread_num(int thrno)
{
  thread_local_thread_num=(int)thrno;
}

int get_thread_num() 
{
  return thread_local_thread_num;
}

RPTHR *get_current_thread()
{
    return rpthread[thread_local_thread_num];
}

// data must be malloced and will be freed automatically after cb is run
void set_thread_fin_cb(RPTHR *thr, rpthr_fin_cb cb, void *data)
{
    REMALLOC(thr->fin_cb_arg, sizeof(void *) * (thr->ncb+1));
    REMALLOC(thr->fin_cb,     sizeof(rpthr_fin_cb *) * (thr->ncb+1));

    thr->fin_cb_arg[thr->ncb] = data;
    thr->fin_cb[thr->ncb] = cb;

    thr->ncb++;    
} 



