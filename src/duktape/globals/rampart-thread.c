/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include  <stdio.h>
#include  <time.h>
#include <poll.h>
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

pthread_mutex_t *cp_lock=NULL; //lock for copy/paste clipboard ctx
RPTHR_LOCK *rp_cp_lock;

//clipboard cond, to wait for var change
pthread_mutex_t cblock;


duk_context *cpctx=NULL; //copy/paste ctx for thread_put and thread_get

#ifdef RPTHRDEBUG

#define rp_debug_printf(...) do{\
    printf("(%d) at %d (thread %d): ", (int)getpid(),__LINE__, get_thread_num());\
    printf(__VA_ARGS__);\
    fflush(stdout);\
}while(0)
#define xxrp_debug_printf(...) /*nada*/

#else

#define rp_debug_printf(...) /*nada*/
#define xxrp_debug_printf(...) do{\
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
    { rp_debug_printf("TRYING to get forklock\n");    usleep(1000000);}\
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
#define LOCKLOCK   RP_PTLOCK(&thrlock->locklock)
#define LOCKUNLOCK RP_PTUNLOCK(&thrlock->locklock)
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
    RP_PTINIT(&thrlock->flaglock);
    thrlock->flags      = 0;

    if(!lock) //NULL and we make our own lock
    {
        REMALLOC(lock, sizeof(pthread_mutex_t));
        RPTHR_SET(thrlock, RPTHR_LOCK_FLAG_FREELOCK);
    }

    thrlock->lock       = lock;

    thrlock->thread_idx = -1; // this is >-1 only while holding lock
    thrlock->next       = NULL;
#ifdef RP_USE_LOCKLOCKS
    RP_PTINIT(&thrlock->locklock);
#endif

    if (pthread_mutex_init((thrlock->lock),NULL) != 0)
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
        //LISTUNLOCK;
        FORKUNLOCK;
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
        //LISTUNLOCK;
        if(!skip_forklock)
            FORKUNLOCK;
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

                if (pthread_mutex_init(&(thrlock->flaglock),NULL) != 0)
                {
                    fprintf(stderr, "Failed to reinitialize lock after fork\n");
                    exit(1);
                }

#ifdef RP_USE_LOCKLOCKS
                if (pthread_mutex_init(&(thrlock->locklock),NULL) != 0)
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
    Certain locks cannot be claimed, because
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

    rp_user_lock=RP_MINIT(NULL); // this calls rp_init_lock()
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

static void copy_prim(duk_context *ctx, duk_context *tctx)
{

    /* this makes no sense and not sure why it is here
    if (strcmp(name, "NaN") == 0)
        return;
    if (strcmp(name, "Infinity") == 0)
        return;
    if (strcmp(name, "undefined") == 0)
        return;
    */

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
//#define cprintf xxrp_debug_printf

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

static int copy_any(duk_context *ctx, duk_context *tctx, duk_idx_t idx, int objid, int is_global_func)
{
    idx = duk_normalize_index(ctx, idx);

    //special case where we are grandchild thread and this function was already wrapped with duk_rp_bytefunc().
    if (duk_is_function(ctx, idx) && duk_has_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("bcfunc")))
    {
        const char *name;
        void *buf, *bc_ptr;
        duk_size_t bc_len;

        duk_get_prop_string(ctx, idx, "fname");
        name=duk_get_string(ctx, -1);
        duk_pop(ctx);

        duk_get_prop_string(ctx, idx, DUK_HIDDEN_SYMBOL("bcfunc"));
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

        //TODO: need to recurse and copy other properties, if any.  But cannot copy func, fname, and bcfunc again
    }
    else
    if (duk_is_ecmascript_function(ctx, idx))
    {
        /* turn ecmascript into bytecode and copy */
        copy_bc_func(ctx, tctx);

        /* recurse and copy JS properties attached to this duktape (but really c) function */
        if( idx == duk_get_top_index(ctx) )
            objid = rpthr_copy_obj(ctx, tctx, objid, 0);
        else
        {
            duk_dup(ctx, idx);
            objid = rpthr_copy_obj(ctx, tctx, objid, 0);
            duk_pop(ctx);
        }

        if (is_global_func)
        {
            /* mark functions that are global as such to copy
               by reference when named in a server.start({map:{}}) callback */
            duk_push_true(ctx);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("is_global"));
            duk_push_true(tctx);
            duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("is_global"));
        }
    }

    else if (duk_check_type_mask(ctx, idx, DUK_TYPE_MASK_STRING | DUK_TYPE_MASK_NUMBER | DUK_TYPE_MASK_BOOLEAN | DUK_TYPE_MASK_NULL | DUK_TYPE_MASK_UNDEFINED))
    {
        /* simple copy of primitives */
        copy_prim(ctx, tctx);
    }

    else if (duk_is_c_function(ctx, idx)) // don't think we need this && !duk_has_prop_string(tctx, -1, propname))
    {
        /* copy pointers to c functions */
        duk_idx_t length;
        duk_func copyfunc = (duk_func)duk_get_c_function(ctx, idx);

        if (duk_get_prop_string(ctx, idx, "length"))
        {
            length = (duk_idx_t)duk_get_int(ctx, -1);
            if(!length) length = DUK_VARARGS;
        }
        else
            length = DUK_VARARGS;
        duk_pop(ctx);

        duk_push_c_function(tctx, copyfunc, length);

        /* recurse and copy JS properties attached to this c function */
        if( idx == duk_get_top_index(ctx) )
            objid = rpthr_copy_obj(ctx, tctx, objid, 0);
        else
        {
            duk_dup(ctx, idx);
            objid = rpthr_copy_obj(ctx, tctx, objid, 0);
            duk_pop(ctx);
        }
    }
    else if (duk_is_buffer_data(ctx, idx))
    {
        //no check for buffer type, just copy data into a plain buffer.
        duk_size_t sz;
        int variant=0;
        void *frombuf = duk_get_buffer_data(ctx, idx, &sz);
        void *tobuf;

        duk_inspect_value(ctx, idx);
        duk_get_prop_string(ctx, idx, "variant");
        variant = duk_get_int_default(ctx, -1, 0);
        duk_pop_2(ctx);
        if (variant == 2) // unlikely
            variant=0; //copy to fixed buffer

         tobuf = duk_push_buffer(tctx, sz, variant);
         memcpy(tobuf, frombuf, sz);
    }
    else if (duk_is_object(ctx, idx) && !duk_is_function(ctx, idx) && !duk_is_c_function(ctx, idx))
    {
        /* check for date */
        if(duk_has_prop_string(ctx, idx, "getMilliseconds") && duk_has_prop_string(ctx, idx, "getUTCDay") )
        {
            duk_dup(ctx, idx);
            duk_push_string(ctx, "getTime");
            //not a lot of error checking here.
            if(duk_pcall_prop(ctx, -2, 0)==DUK_EXEC_SUCCESS)
            {
                duk_get_global_string(tctx, "Date");
                duk_push_number(tctx, duk_get_number_default(ctx, -1, 0));
                duk_new(tctx, 1);
            }
            else
                duk_push_undefined(tctx);

            duk_pop_2(ctx); //res of pcall and the dup of the date object
        }
        // check if it is an array, and if we've seen it before.
        // if not, get its pointer from the main stack, copy the array, store the array indexed by that pointer
        // if so, retrieve it by the pointer from the main stack
        // TODO: check if we want to use this ref tracking method for all objects. Might be cleaner.
        else if (duk_is_array(ctx, idx))
        {
            char ptr_str[32]; //plenty of room for hex of 64 bit ptr (17 chars with null)
            void *p = duk_get_heapptr(ctx, idx);

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
                duk_dup(ctx, idx); //put a ref of obj at idx on top of stack for rpthr_copy_obj
                objid = rpthr_copy_obj(ctx, tctx, objid, 0);
                // tctx -> [ ..., arrRefPtr, array, copied_object ]
                duk_pop(ctx); //remove ref of obj at idx
                duk_pull(tctx, -2);
                // tctx -> [ ..., arrRefPtr, copied_object, array ]
                values_to_array(tctx, -2);

                duk_remove(tctx, -2);
                // tctx -> [ ... arrRefPtr, copied_array ]

                duk_remove(tctx, -2);
                // tctx -> [ ..., copied_array ]
            }
        }
        /* copy normal {} objects */
        else
        {
            /* recurse and begin again with this ctx object (at idx:-1)
               and a new empty object for tctx (pushed to idx:-1)              */
            duk_push_object(tctx);

            if( idx == duk_get_top_index(ctx) )
                objid = rpthr_copy_obj(ctx, tctx, objid, 0);
            else
            {
                duk_dup(ctx, idx);
                objid = rpthr_copy_obj(ctx, tctx, objid, 0);
                duk_pop(ctx);
            }
        }
    }
    else if (duk_is_pointer(ctx, idx) )
        duk_push_pointer(tctx, duk_get_pointer(ctx, -1) );
    else
        duk_push_undefined(tctx);

    return objid;
}

void rpthr_copy(duk_context *ctx, duk_context *tctx, duk_idx_t idx)
{
    copy_any(ctx, tctx, idx, 0, 0);
}

/* ctx object and tctx object should be at top of respective stacks */
int rpthr_copy_obj(duk_context *ctx, duk_context *tctx, int objid, int skiprefcnt)
{
    const char *s;
    int is_global=0, has_ref=0;

    /* for debugging *
    const char *lastvar="global";
    if(duk_is_string(ctx, -2) )
        lastvar=duk_get_string(ctx, -2);
    printf("rpthr_copy_obj %s top=%d\n",lastvar, (int)duk_get_top(ctx));
    * end for debugging */


    objid++;
    const char *prev = duk_get_string(ctx, -2);

    is_global = !prev;

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
        has_ref=1;
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
    /* copy the object on the top of stack to objById object */
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

    int do_finalizer=0;

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

        /* If this property exists, run specified function */
        if(!has_ref && !strcmp(s, "\xffobjOnCopyCallback") )
        {
            duk_dup(ctx, -1); // the callback
            duk_dup(ctx, -5);  // the object
            duk_call(ctx, 1); // call callback(object)
            duk_pop(ctx);     //discard return, and proceed as usual
            do_finalizer=1;
        }

        // if the object has a hidden refcnt *int, increment it
        // this is currently not used, but leaving in for possible future use
        /*
        if(!skiprefcnt && !strcmp(s, "\xffrefcnt_ptr") )
        {
            int *refcnt_ptr;
            refcnt_ptr=duk_get_pointer(ctx, -1);
            (*refcnt_ptr)++;
        }
        */

        cprintf("copying %s\n",s);

        // don't copy props we already have, or console or performance if we are in global object
        if ( !duk_has_prop_string(tctx, -1, s) &&
            (!is_global || (strcmp(s, "console") != 0 && strcmp(s, "performance") != 0))
        ){
            objid=copy_any(ctx, tctx, -1, objid, is_global);
            duk_put_prop_string(tctx, -2, s);
        }

        /* remove key and val from stack and repeat */
        duk_pop_2(ctx);
    }

    /* remove enum from stack */
    duk_pop(ctx);

    // there are many functions that aren't ready for this yet and will
    // barf if the finalizer is called several times.  For now, only use with
    // objects that have a objOnCopyCallback callback.
    if(do_finalizer) {
        /* add finalizers back in */
        duk_get_finalizer(ctx, -1);
        if(!duk_is_undefined(ctx, -1))
        {
            objid=copy_any(ctx, tctx, -1, objid, 0);
            duk_set_finalizer(tctx, -2);
        }
        duk_pop(ctx);
    }

    // flag object as copied, in case needed by some function in the future (currently used in redis module)
    duk_push_true(tctx);
    duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("thread_copied"));

    // also used in redis module
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
    rpthr_copy_obj(ctx, tctx, 0, 0);
    /* remove hidden symbols and stash-cache used for reference tracking */

    rpthr_clean_obj(ctx, tctx);
    /* remove global object from both stacks */
    duk_pop(ctx);
    duk_del_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("arrRefPtr") );
    duk_pop(tctx);
//    rpthr_copy_stash(ctx, tctx); // copy stash too
}

/* ******************************************
  END FUNCTIONS FOR COPYING BETWEEN DUK HEAPS
********************************************* */

/* *******************************************
START FUNCTIONS FOR COPY/PASTE PUT AND GET
 ********************************************* */

// copy locking
#define CPLOCK RP_MLOCK(rp_cp_lock)
#define CPUNLOCK RP_MUNLOCK(rp_cp_lock)
//#define CPLOCK do { printf("%d - %d - CP Locking, last=%d\n",thread_local_thread_num,__LINE__,cplastlock); RP_MLOCK(rp_cp_lock); printf("%d - %d - CP Locked\n",thread_local_thread_num,__LINE__); cplastlock=thread_local_thread_num;} while(0)
//#define CPUNLOCK do { printf("%d - %d - CP Unlocking\n",thread_local_thread_num,__LINE__); RP_MUNLOCK(rp_cp_lock); cplastlock=-thread_local_thread_num;printf("%d - %d - CP Unlocked\n",thread_local_thread_num,__LINE__);} while(0)

//clipboard locking
//_Atomic volatile int lastlock = 0, cplastlock=0;
//#define CBLOCKLOCK do { printf("%d - %d - Locking, %s last=%d\n",thread_local_thread_num,__LINE__, lastlock>-1 ?"LOCKED":"",lastlock); fflush(stdout);RP_PTLOCK(&cblock); printf("%d - %d - Locked\n",thread_local_thread_num,__LINE__); fflush(stdout);lastlock=thread_local_thread_num;} while(0)
//#define CBLOCKUNLOCK do { printf("%d - %d - Unlocking\n",thread_local_thread_num,__LINE__);fflush(stdout); RP_PTUNLOCK(&cblock); lastlock=-thread_local_thread_num;printf("%d - %d - Unlocked\n",thread_local_thread_num,__LINE__);fflush(stdout);} while(0)
#define CBLOCKLOCK RP_PTLOCK(&cblock)
#define CBLOCKUNLOCK RP_PTUNLOCK(&cblock)

#define pipe_err do{\
    fprintf(stderr,"pipe error in rampart.thread %s:%d\n", __FILE__, __LINE__);\
}while(0)

#define thrwrite(b,c) ({\
    int r=0;\
    if(thr->writer>-1) {\
        r=write(thr->writer, (b),(c));\
        if(r==-1) {\
            fprintf(stderr, "thread write failed: '%s' at %d, fd:%d\n",strerror(errno),__LINE__,thr->writer);\
            exit(0);\
        }\
    }\
    r;\
})

#define thrread(b,c) ({\
    int r=0;\
    if(thr->reader>-1) {\
        r= read(thr->reader,(b),(c));\
        if(r==-1) {\
            fprintf(stderr, "thread read failed: '%s' at %d\n",strerror(errno),__LINE__);\
            exit(0);\
        }\
    }\
    r;\
})

void put_to_clipboard(duk_context *ctx, duk_idx_t val_idx, char *key)
{
    val_idx = duk_normalize_index(ctx, val_idx);

    duk_push_object(ctx);
    duk_dup(ctx, val_idx);
    duk_put_prop_string(ctx, -2, "val");

    CPLOCK;
    if(cpctx==NULL)
        cpctx=duk_create_heap(NULL, NULL, NULL, NULL, duk_rp_fatal);

    duk_push_global_stash(cpctx);

    //cpctx: [ cpctx_global_stash ]

    duk_push_object(cpctx);
    //cpctx: [ cpctx_global_stash, holder_object ]

    //copy object with key "val" to new cpctx object
    // do not increment refcnt on way in.
    rpthr_copy_obj(ctx, cpctx, 0, 1);
    rpthr_clean_obj(ctx, cpctx);
    //cpctx: [ cpctx_global_stash, holder_object_filled ]

    duk_put_prop_string(cpctx, -2, key);
    //cpctx: [ cpctx_global_stash(with-holder) ]

    RP_EMPTY_STACK(cpctx);
    //cpctx: []

    CPUNLOCK;
    duk_pop(ctx);
}


duk_ret_t rp_thread_put(duk_context *ctx)
{
    duk_size_t len;
    const char *key = REQUIRE_LSTRING(ctx, 0, &len, "thread.put: First argument must be a string (key)");
    int i=0;

    duk_remove(ctx,0);

    if(duk_is_undefined(ctx, 0))
        RP_THROW(ctx, "thread.put: Second argument is a variable to store and must be defined");

    put_to_clipboard(ctx, 0, (char *)key);

    CBLOCKLOCK;

    len++; //include \0

    for (i=0;i<nrpthreads;i++)
    {
        RPTHR *thr=rpthread[i];
        if( RPTHR_TEST(thr, RPTHR_FLAG_WAITING) )
        {
            thrwrite(&len, sizeof(duk_size_t));
            thrwrite(key, len);
        }
    }
    CBLOCKUNLOCK;
    return 0;
}

static duk_ret_t _thread_get_del(duk_context *ctx, char *key, int del)
{
    if(cpctx==NULL)
        return 0;

    CPLOCK;

    duk_push_global_stash(cpctx);
    //cpctx: [ cpctx_global_stash ]
    if( !duk_get_prop_string(cpctx, -1, key) )
    {
        RP_EMPTY_STACK(cpctx);
        CPUNLOCK;
        return 0;
    }
    //cpctx: [ cpctx_global_stash, holder_object_filled ]

    //copy object with key "val" to ctx object
    duk_push_object(ctx);
    rpthr_copy_obj(cpctx, ctx, 0, 0);
    rpthr_clean_obj(cpctx, ctx);

    if(del)
    {
        duk_pop(cpctx);
        //cpctx: [ cpctx_global_stash ]
        duk_del_prop_string(cpctx, -1, key); //the delete
        duk_pop(cpctx);
        //cpctx: []
    }
    RP_EMPTY_STACK(cpctx);

    //cpctx: []

    CPUNLOCK;

    duk_get_prop_string(ctx, -1, "val");
    duk_remove(ctx, -2);

    return 1;
}

#define LATE_LOCK   0
#define EARLY_LOCK  1
#define GET_NODEL   0
#define GET_DEL     1

duk_ret_t get_from_clipboard(duk_context *ctx, char *key)
{
    if(!_thread_get_del(ctx, key, GET_NODEL))
        duk_push_undefined(ctx); //we need to push onto the stack when called internally
    return 1;
}

duk_ret_t del_from_clipboard(duk_context *ctx, char *key)
{
    if(!_thread_get_del(ctx, key, GET_DEL))
        duk_push_undefined(ctx); //we need to push onto the stack when called internally
    return 1;
}


#define closepipes do{\
    CBLOCKLOCK;\
    RPTHR_CLEAR(thr, RPTHR_FLAG_WAITING);\
    close(thr->reader);\
    close(thr->writer);\
    thr->reader=-1;\
    thr->writer=-1;\
    CBLOCKUNLOCK;\
}while(0)

#define openpipes(_thr, _locked) ({\
    int _fd[2];\
    int _ret=0;\
    if(!_locked)CBLOCKLOCK;\
    if (rp_pipe(_fd) == -1){\
        fprintf(stderr, "thread pipe creation failed\n");\
        _ret=0;\
    } else {\
        _thr->reader=_fd[0];\
        _thr->writer=_fd[1];\
        RPTHR_SET(thr, RPTHR_FLAG_WAITING);\
        _ret=1;\
    }\
    CBLOCKUNLOCK;\
    _ret;\
})

#define KEYLIST struct onget_keylist_s
KEYLIST {
    char          *key;
    KEYLIST       *next;
    KEYLIST       *prev;
};

#define GETEV struct onget_ev_s
GETEV {
    struct event *e;
    KEYLIST *keys;
};

__thread GETEV *getev=NULL;

static void onget_event(evutil_socket_t fd, short events, void* arg)
{
    RPTHR *thr=get_current_thread();
    duk_context *ctx = thr->ctx;
    char *waitkey=NULL;
    KEYLIST *entry = getev->keys;
    duk_idx_t cb_idx=-1;
    duk_size_t len=0;

    CBLOCKLOCK;

    thrread(&len, sizeof(duk_size_t));

    REMALLOC(waitkey, len);

    thrread(waitkey, len);

    CBLOCKUNLOCK;

    while(entry)
    {
        char *matchkey = entry->key;
        duk_size_t mlen = strlen(matchkey);

        if(mlen>0 && matchkey[mlen-1]=='*')
            mlen-=1;
        else if(len>mlen)
            mlen=len;

        if(!mlen || strncmp(matchkey, waitkey, mlen)==0)
        {
            if(cb_idx==-1)
            {
                duk_push_global_stash(ctx);
                if(!duk_get_prop_string(ctx, -1, "waitfor_cbs"))
                {
                    fprintf(stderr, "internal error getting onGet callback");
                    duk_set_top(ctx, 0);
                    free(waitkey);
                    return;
                }
                duk_remove(ctx, -2);//stash
                cb_idx=duk_get_top_index(ctx);
            }
            duk_push_sprintf(ctx, "%p", entry);
            if(duk_get_prop(ctx, cb_idx))
            {
                // when callback is called, entry might get removed and freed
                // and wpev might get nulled and freed
                KEYLIST *prev=entry->prev;

                //this
                duk_push_sprintf(ctx, "this_%p", entry);
                duk_get_prop(ctx, cb_idx);

                //key
                duk_push_string(ctx, waitkey);

                // val
                if(!_thread_get_del(ctx, waitkey, GET_NODEL))
                    duk_push_undefined(ctx);

                // match (different from key if glob or empty)
                duk_push_string(ctx, entry->key);

                if(duk_pcall_method(ctx, 3))
                {
                    const char *errmsg = rp_push_error(ctx, -1, "onGet - error in callback:", rp_print_error_lines);
                    fprintf(stderr, "%s\n", errmsg);
                }
                duk_pop(ctx);//TODO: check for false
                //check if list altered
                if(!getev) //if no entries at all
                {
                    free(waitkey);
                    return;
                }
                if(!prev && getev->keys != entry)//if we were the first, and now the first changed
                {
                    entry=getev->keys;
                    continue;
                }
                else if (prev && prev->next != entry) //if in list, but we were removed
                {
                    entry=prev->next;
                    continue;
                }
            }
        }
        entry=entry->next;
    }
    duk_set_top(ctx, 0);
    // might be nulled in the callback with stopwait_async
    if(getev)
        event_add(getev->e,NULL);
    free(waitkey);
}

static duk_ret_t onget_remove(duk_context *ctx)
{
    if(!getev)
    {
        duk_push_false(ctx);
        return 1;
    }

    KEYLIST *entry=getev->keys, *rementry;

    duk_push_this(ctx);
    if(! duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("entry")))
    {
        duk_push_false(ctx);
        return 1;
    }
    rementry = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    while(entry)
    {
        if(entry == rementry)
        {
            if(entry == getev->keys)
            {
                getev->keys=entry->next;
                if(getev->keys)
                    getev->keys->prev=NULL;
            }
            else
            {
                entry->prev->next = entry->next;
                if(entry->next)
                    entry->next->prev = entry->prev;
            }
            duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("entry"));
            break;
        }
        entry=entry->next;
    }

    if(!entry)
    {
        duk_push_false(ctx);
        return 1;
    }

    if(!getev->keys)
    {
        RPTHR *thr=get_current_thread();
        closepipes;
        event_del(getev->e);
        event_free(getev->e);
        free(getev);
        getev=NULL;
    }

    duk_push_global_stash(ctx);
    duk_get_prop_string(ctx, -1, "waitfor_cbs");
    duk_push_sprintf(ctx, "%p", entry);
    duk_del_prop(ctx, -2);

    duk_push_sprintf(ctx, "this_%p", entry);
    duk_del_prop(ctx, -2);

    free(entry->key);
    free(entry);

    duk_push_true(ctx);
    return 1;
}

static duk_ret_t rp_onget(duk_context *ctx)
{
    RPTHR *thr=get_current_thread();
    const char *key=REQUIRE_STRING(ctx, 0, "thread.onGet: first argument must be a String (key)");
    REQUIRE_FUNCTION(ctx, 1, "thread.onGet: second argument must be a function (callback)");

    KEYLIST *entry=NULL;

    duk_idx_t retidx = duk_push_object(ctx);

    duk_push_global_stash(ctx);

    if(!getev)
    {
        CALLOC(entry, sizeof(KEYLIST));
        entry->key=strdup(key);
        duk_push_object(ctx);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "waitfor_cbs");

        duk_push_sprintf(ctx, "%p", entry);
        duk_dup(ctx, 1);
        duk_put_prop(ctx, -3);

        duk_push_sprintf(ctx, "this_%p", entry);
        duk_dup(ctx, retidx);
        duk_put_prop(ctx, -3);

        openpipes(thr, LATE_LOCK);

        CALLOC(getev,sizeof(GETEV));
        getev->e = event_new(thr->base, thr->reader, EV_READ, onget_event, getev);
        getev->keys=entry;
        event_add(getev->e,NULL);
    }
    else
    {
        duk_get_prop_string(ctx, -1, "waitfor_cbs");

        CALLOC(entry, sizeof(KEYLIST));
        entry->key=strdup(key);

        duk_push_sprintf(ctx, "%p", entry);
        duk_dup(ctx, 1);
        duk_put_prop(ctx, -3);

        duk_push_sprintf(ctx, "this_%p", entry);
        duk_dup(ctx, retidx);
        duk_put_prop(ctx, -3);

        entry->next=getev->keys;
        getev->keys->prev=entry;
        getev->keys=entry;
    }

    duk_push_pointer(ctx, entry);
    duk_put_prop_string(ctx, retidx, DUK_HIDDEN_SYMBOL("entry"));
    duk_push_c_function(ctx, onget_remove, 0);
    duk_put_prop_string(ctx, retidx, "remove");

    duk_pull(ctx, retidx);
    return 1;
}

static duk_ret_t _thread_waitfor(duk_context *ctx, const char *key, const char *funcname, int del, int locked)
{
    char *waitkey=NULL;
    duk_size_t len, lastlen=64;
    RPTHR *thr = get_current_thread();
    //int fd[2];
    int to=-1; // indefinite/forever
    uint64_t timemarker, tmcur;
    struct pollfd ufds[1];
    struct timespec ts;
    int pret;

    if(!duk_is_undefined(ctx, 1))
        to=REQUIRE_UINT(ctx, 1, "thread.%s: second argument, if provided, must be a positive number (milliseconds)", funcname);

    if(!openpipes(thr, locked))
        return 0;
    
    if(!to) /* return immediately if to==0 */
        return _thread_get_del(ctx, (char *)key, del);

    ufds[0].fd = thr->reader;
    ufds[0].events = POLLIN;

    REMALLOC(waitkey, lastlen);
    clock_gettime(CLOCK_REALTIME, &ts);
    //timespec_get(&ts, TIME_UTC);
    timemarker = ts.tv_sec *1000 + ts.tv_nsec/1000000;

    //wait for a message from any other thread that a var has been updated
    while(1)
    {
        if((pret = poll(ufds, 1, to)) == -1)
            pipe_err;

        // timeout
        if(pret==0)
        {
            free(waitkey);
            closepipes;
            return 0;
        }

        CBLOCKLOCK;

        thrread(&len, sizeof(duk_size_t));

        if(lastlen<len)
            REMALLOC(waitkey, len);

        thrread(waitkey, len);

        CBLOCKUNLOCK;

        if(strcmp(key, waitkey)==0)
        {
            closepipes;
            free(waitkey);
            return _thread_get_del(ctx, (char *)key, del);
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        //timespec_get(&ts, TIME_UTC);
        tmcur = ts.tv_sec *1000 + ts.tv_nsec/1000000;
        to -= (int)( tmcur - timemarker );
        timemarker = tmcur;
    }
}

duk_ret_t rp_thread_get(duk_context *ctx)
{
    const char *key = REQUIRE_STRING(ctx, 0, "thread.get: First argument must be a string (key)");
    duk_ret_t ret;

    // lock early so we don't get a message in pipe before we set it up
    if(!duk_is_undefined(ctx,1))
        CBLOCKLOCK;

    ret = _thread_get_del(ctx, (char*)key, GET_NODEL);
    if(!ret && !duk_is_undefined(ctx,1))
        ret = _thread_waitfor(ctx, key, "get", GET_NODEL, EARLY_LOCK);
    else if (!duk_is_undefined(ctx,1)) // if ret && defined
        CBLOCKUNLOCK;

    return ret;
}

duk_ret_t rp_thread_del(duk_context *ctx)
{
    const char *key = REQUIRE_STRING(ctx, 0, "thread.del: First argument must be a string (key)");

    duk_ret_t ret;

    if(!duk_is_undefined(ctx,1))
        CBLOCKLOCK;;

    ret = _thread_get_del(ctx, (char *)key, GET_DEL);
    if(!ret && !duk_is_undefined(ctx,0))
        ret = _thread_waitfor(ctx, key, "get", GET_DEL, EARLY_LOCK);
    else if (!duk_is_undefined(ctx,1)) // if ret && defined
        CBLOCKUNLOCK;;

    return ret;
}


static duk_ret_t rp_thread_waitfor(duk_context *ctx)
{
    duk_size_t len;
    const char *key = REQUIRE_LSTRING(ctx, 0, &len, "thread.waitfor: First argument must be a string (key)");

    return _thread_waitfor(ctx, key, "waitfor", GET_NODEL, LATE_LOCK);
}

static duk_ret_t rp_thread_getwait(duk_context *ctx)
{
    duk_ret_t ret;
    duk_size_t len;
    const char *key = REQUIRE_LSTRING(ctx, 0, &len, "thread.getwait: First argument must be a string (key)");

    ret = _thread_get_del(ctx, (char *)key, GET_NODEL);
    if(ret)
        return ret;

    return _thread_waitfor(ctx, key, "getwait", GET_NODEL, LATE_LOCK);
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

// get number of threads that are active
// if **alist is not NULL, make a list of active thread numbers (needs free)
int get_thread_count(int **alist)
{
    int i=0, ret=0;

    for (i=0;i<nrpthreads;i++)
    {
        if( RPTHR_TEST(rpthread[i], RPTHR_FLAG_IN_USE) )
            ret++;
    }

    if(alist)
    {
        int *list = NULL, li=0;

        if(!ret) //won't happen, should always have the main thread
        {
            *alist=NULL;
            return ret;
        }

        REMALLOC(list, sizeof(int) * ret);
        for (i=0;i<nrpthreads;i++)
        {
            if( RPTHR_TEST(rpthread[i], RPTHR_FLAG_IN_USE) )
                list[li++]=i;
        }
        alist=&list;
    }

    return ret;
    
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
    rp_debug_printf("thread %d has %d children, removing %p\n", (int)pthr->index, pthr->nchildren, thr);
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
    rp_debug_printf("now have %d children\n", pthr->nchildren);
}

void rp_close_thread(RPTHR *thr)
{
    int i=0;

    rp_debug_printf("CLOSE THREAD, thread=%d, base=%p\n", (int)thr->index, thr->base);
    if(! RPTHR_TEST(thr, RPTHR_FLAG_IN_USE))
        return;

    thr->flags=0; //reset all flags

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

    thr->dnsbase=NULL;
    event_base_free(thr->base);
    thr->base=NULL;

    free(thr->children);
    thr->children=NULL;
    thr->nchildren=0;
    rp_debug_printf("STILL CLOSE THREAD\n");
    if(thr->fin_cb && thr->ncb)
    {

        for (i=0;i<thr->ncb;i++)
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
    rp_debug_printf("CLOSE THREAD -- remove from parent\n");
    remove_from_parent(thr);
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

    //redo CPLOCK
    rp_remove_lock(rp_cp_lock);
    cp_lock=NULL;
    REMALLOC(cp_lock, sizeof(pthread_mutex_t));
    rp_cp_lock=RP_MINIT(cp_lock);
    RPTHR_SET(rp_cp_lock, RPTHR_LOCK_FLAG_FREELOCK);


    // make a new thread structure for this thread, and replace mainthr
    mainthr=rp_new_thread(RPTHR_FLAG_THR_SAFE, ctx);
    mainthr->base = event_base_new();
    mainthr->dnsbase=rp_make_dns_base(mainthr->ctx, mainthr->base);
    RPTHR_SET(mainthr, RPTHR_FLAG_BASE);
    thread_local_thread_num = 0;
    mainthr->index=0;
    main_ctx=mainthr->ctx;
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
        RP_PTINIT(&ret->flaglock);
    }


    ret->flags=flags;

    RPTHR_SET(ret, RPTHR_FLAG_IN_USE);
    rp_debug_printf("CREATED THR %d\n", thread_no);

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
    ret->reader=-1;
    ret->writer=-1;
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
        rp_debug_printf("Added child %d to %d\n", ret->index, pthr->index);
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
static int32_t cbidx=0;
static int32_t varidx=0;
#define RPTINFO struct rp_thread_info

RPTINFO {
    void              *bytecode;        // bytecode of function executed in child thread
    size_t             bytecode_sz;     // size of above
    int32_t            index;           // growing number to make key for parent callback function in hidden object
    int32_t            varindex;        // same but for copying arguments via cpctx
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
    if(info->varindex > -1)
    {
        char key[16];

        snprintf(key, 16, "\xff%d", (int) info->varindex);
        _thread_get_del(ctx, key, GET_DEL); //get and delete
    }
    else
        duk_push_undefined(ctx);

    info=clean_info(info);

    if(!duk_is_function(ctx, -2))
    {
        rp_debug_printf("failed to get function indexed at %s\n", objkey);
        safeprintstack(ctx);
        RP_THROW(ctx,"CALLBACK FUNCTION INDEX LOOKUP ERROR");
    }

    rp_debug_printf("running parent javascript callback\n");

    if(duk_is_object(ctx, -1))
    {
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("error"))) {
            duk_push_undefined(ctx); // [ func, obj, errmsg, undefined ]
            duk_replace(ctx, -3); // [ func, undefined, errmsg ]
        }
        // else no pop, leave undefined on stack  [ func, objval, undefined ]
    }
    else
        duk_push_undefined(ctx); // [ func, val, undefined ]

    if(duk_pcall(ctx, 2))
    {
        const char *errmsg = rp_push_error(ctx, -1, "thr.exec() - error in parent thread callback:", rp_print_error_lines);
        fprintf(stderr, "%s\n", errmsg);
    }
    RP_EMPTY_STACK(ctx);
}

#define ev_add(a,b) do{ rp_debug_printf("thread.c:%d #%d- adding %p\n",__LINE__, get_thread_num(),(a)); event_add(a,b);}while(0)

// execute in thread, from event loop
static void thread_doevent(evutil_socket_t fd, short events, void* arg)
{
    RPTINFO *info     = (RPTINFO *)arg;
    RPTHR   *thr      = info->thr;
    duk_context *ctx  = thr->ctx;
    void *buf=NULL;
    struct event *tev=info->e;

    buf = duk_push_fixed_buffer(ctx, info->bytecode_sz);
    memcpy(buf, (const void *)info->bytecode, info->bytecode_sz);
    duk_load_function(ctx);

    if(info->varindex > -1)
    {
        char key[16];

        snprintf(key, 16, "\xff%d", (int) info->varindex);
        _thread_get_del(ctx, key, GET_DEL); //get and delete
    }
    else
        duk_push_undefined(ctx);

    //printf("exec in %s\n", duk_to_string(ctx, -1));

    rp_debug_printf("running thread javascript callback, base =%p\n", thr->base);

    RPTHR_SET(thr, RPTHR_FLAG_ACTIVE);
    if(duk_pcall(ctx,1) != 0)
    {
        RPTHR_CLEAR(thr, RPTHR_FLAG_ACTIVE);
        const char *errmsg;

        duk_push_object(ctx);

        errmsg = rp_push_error(ctx, -2, "thr.exec() - error in thread callback:",rp_print_error_lines);
        //if we don't have a parent thread callback
        if(info->index == -1)
        {
            fprintf(stderr, "%s\n", errmsg);
            RPTHR_CLEAR(thr, RPTHR_FLAG_ACTIVE);
            goto end;
        }
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("error"));
    }
    RPTHR_CLEAR(thr, RPTHR_FLAG_ACTIVE);

    //error or results is at idx: -1

    //if no parent callback, discard js return value
    rp_debug_printf("checking if we have a callback - %s\n", (info->index == -1? "no": "yes"));
    if(info->index == -1)
        goto end;

    //encode and store return in cpctx if defined
    if(duk_is_undefined(ctx, -1))
    {
        info->varindex = -1;
    }
    else
    {
        int32_t ix;
        char key[16];

        THRLOCK;
        if(varidx == INT32_MAX)
        {
            varidx=0;
            ix=0;
        }
        else
            ix = varidx++;
        THRUNLOCK;

        info->varindex=ix;

        snprintf(key, 16, "\xff%d", (int) info->varindex);
        put_to_clipboard(ctx, -1, key);
    }

    rp_debug_printf("inserting event for parent callback for thread %d\n",info->parent_thr->index);
    info->e=event_new(info->parent_thr->base, -1, 0, do_parent_callback, info);
    ev_add(info->e, &immediate);


    end:
    // if not doing parent callback
    if(info->index==-1)
        info=clean_info(info); //includes event_free((tev|info->e));
    else
        event_free(tev);

    RP_EMPTY_STACK(ctx);
}

// an event run inside a thread to shut down the thread
static void finalize_event(evutil_socket_t fd, short events, void* arg)
{
    struct event **e = (struct event **)arg;
    int npending=0, rpevents_pending=0;
    struct event_base *base=event_get_base(*e);
    RPTHR *thr = get_current_thread();
    duk_context *ctx = thr->ctx;

    //event_del(*e);

    if( !RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) )
    {
        rp_debug_printf("ALREADY finalized in finalize_event\n");
        event_free(*e);
        free(arg);
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
                rpevents_pending=1;
                duk_pop_n(ctx, 4); //inner key, inner enum ,key, val
                break;
            }

            duk_pop_3(ctx);//inner enum, key,val
        }
        duk_pop(ctx);//enum
    }
    duk_pop(ctx);//jsevents or undefined

    npending= rpevents_pending + event_base_get_num_events(base, EVENT_BASE_COUNT_ADDED);

    // if we have pending events, or parent thread is active in JS and might add events, keep going
    if(!npending && !RPTHR_TEST(thr->parent, RPTHR_FLAG_ACTIVE))
    {
        // check if any child threads left in use
        if(thr->nchildren != 0) {
            rp_debug_printf("still have %d children\n", thr->nchildren);
            goto goagain;
        }
        event_free(*e);
        free(arg);
        rp_debug_printf("CLOSING thr %d\n", (int)thr->index);
        THRLOCK;
        rp_close_thread(thr);
        THRUNLOCK;
        pthread_exit(NULL);
        return;
    }

    goagain:
        ev_add(*e, &fiftyms);

}

// insert finalizers if none have been inserted already.
int rp_thread_close_children()
{
    int i=1, ret=0;
    struct event **e=NULL;
    RPTHR *thr;
    for (i=1;i<nrpthreads;i++)
    {
        thr = rpthread[i];
        if(
            RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) &&
            RPTHR_TEST(thr, RPTHR_FLAG_BASE  ) &&
         !  RPTHR_TEST(thr, RPTHR_FLAG_SERVER) &&   //server threads never exit
         !  RPTHR_TEST(thr, RPTHR_FLAG_KEEP_OPEN) ) //user specified to not autoclose.
        {
            if(!RPTHR_TESTSET(thr, RPTHR_FLAG_FINAL)) //test and set at same time with lock, avoid race
            {
                e=NULL;
                REMALLOC(e, sizeof(struct event *));
                *e=event_new(thr->base, -1, 0, finalize_event, e);
                rp_debug_printf("MANUALLY ADDING event finalizer for thread %d, thr=%p, ev=%p, flags=%x\n",thr->index, thr, *e, thr->flags);
                ev_add(*e, &immediate);
                ret=1;
                //if(!RPTHR_TEST(thr,RPTHR_FLAG_FINAL)) { printf("epic fail thr=%p,  %x\n", thr, thr->flags);exit(1); }
            }
        }
    }
    return ret;
}

// insert event that will shut it down from inside
// if called directly, 'this' must be on stack idx 0
// if triggered by finalizer, 'this' is already present
// BUT -- don't insert in loop if already inserted in rp_thread_close_children() above
static duk_ret_t finalize_thr(duk_context *ctx)
{
    struct event **e=NULL;
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
        rp_debug_printf("ALREADY finalized in finalizer_thr\n");
        return 0;
    }

    duk_del_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("thr"));

    if(thr->base && !RPTHR_TESTSET(thr, RPTHR_FLAG_FINAL ))
    {
        duk_push_undefined(ctx);
        duk_set_finalizer(ctx, 0);
        REMALLOC(e, sizeof(struct event *));
        *e=event_new(thr->base, -1, 0, finalize_event, e);
        rp_debug_printf("adding closer to base %p\n", thr->base);
        ev_add(*e, &immediate);
    }
    return 0;
}

// insert into event loop
static duk_ret_t loop_insert(duk_context *ctx)
{
    duk_size_t len=0;
    duk_idx_t cbfunc_idx=-1, arg_idx=-1, to_idx=-1, thrfunc_idx=-1, obj_idx=-1;
    RPTINFO *info=NULL;
    RPTHR *thr=NULL, *curthr=get_current_thread();
    int thrno=-1;
    void *buf;
    char objkey[16];
    struct timeval timeout;
    duk_idx_t i=1;

    timeout.tv_sec=0;
    timeout.tv_usec=0;

    for(i=0; i<5; i++)
    {
        if(duk_is_function(ctx,i))
        {
            if(thrfunc_idx == -1)
                thrfunc_idx=i;
            else if(cbfunc_idx == -1)
                cbfunc_idx=i;
            else
                RP_THROW(ctx, "thr.exec: Too many functions as arguments.  Cannot pass a function as an argument to the thread function.");
        }
        else if(
            duk_is_object(ctx, i) &&
            !duk_is_array(ctx, i) &&
            (
                duk_has_prop_string(ctx,i,"threadFunc")   ||
                duk_has_prop_string(ctx,i,"threadDelay")  ||
                duk_has_prop_string(ctx,i,"callbackFunc") ||
                duk_has_prop_string(ctx,i,"threadArg")
            )
        )
            obj_idx=i;
        else if(arg_idx!=-1 && duk_is_number(ctx, i))
            to_idx=i;
        else if(!duk_is_undefined(ctx,i) && arg_idx ==-1)
            arg_idx=i;
        else if(!duk_is_undefined(ctx,i))
            RP_THROW(ctx, "thr.exec: unknown/unsupported argument #%d", i+1);
    }

    if(obj_idx != -1)
    {
        if(duk_get_prop_string(ctx, obj_idx, "threadFunc"))
            thrfunc_idx = duk_normalize_index(ctx, -1);
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "threadDelay"))
            to_idx = duk_normalize_index(ctx, -1);
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "callbackFunc"))
            cbfunc_idx = duk_normalize_index(ctx, -1);
        else
            duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "threadArg"))
            arg_idx = duk_normalize_index(ctx, -1);
        else
            duk_pop(ctx);
    }

    REQUIRE_FUNCTION(ctx, thrfunc_idx, "thr.exec: threadFunc must be provided in options object or as first parameter");
    if(cbfunc_idx != -1)
        REQUIRE_FUNCTION(ctx, cbfunc_idx, "thr.exec: callbackFunc must be a function");

    // check for built in c functions (but not the wrapper function with bytecode from a JS func in it)
    if(duk_is_c_function(ctx, thrfunc_idx) && !duk_has_prop_string(ctx, thrfunc_idx, DUK_HIDDEN_SYMBOL("bcfunc")))
    {
        RP_THROW(ctx,   "thr.exec - Directly passing of built-in c functions not supported.  Wrap it in a JavaScript function.\n"
                        "    Example:\n"
                        "       thr.exec(console.log, 'hello world'); //illegal, throws this error\n"
                        "       thr.exec(function(arg){console.log(arg);}, 'hello world'); //works with wrapper\n"
        );
    }

    //setup
    if(to_idx!=-1)
    {
        double to=REQUIRE_NUMBER(ctx, to_idx, "threadDelay value must be a positive number");

        if(to<0.0)
            RP_THROW(ctx, "threadDelay value must be a positive number");

        to/=1000.0;

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
        rp_debug_printf("failed exec, cannot run a thread created outside the current one\n");
        RP_THROW(ctx, "thr.exec: Cannot run a thread created outside the current one.");
    }
    // FIXME: mem leak when forked from a different thread and this one disappears.
    //        Perhaps we need to store it in a list with thread number, and when forked, clean up other threads.
    REMALLOC(info, sizeof(RPTINFO));

    info->bytecode=NULL;
    info->bytecode_sz=0;
    info->index=-1;
    info->varindex=-1;
    info->parent_thr=curthr;

    //copy bytecode to struct
    if( duk_get_prop_string(ctx, thrfunc_idx, DUK_HIDDEN_SYMBOL("bcfunc")) )
    {
        // this was already copied (we are grandchild thread)
        buf = duk_get_buffer_data(ctx, -1, &len);
        duk_pop(ctx);
    } else {
        duk_pop(ctx); //undef
        duk_dup(ctx, thrfunc_idx);
        duk_dump_function(ctx);
        buf = duk_get_buffer_data(ctx, -1, &len);
    }

    info->bytecode_sz=(size_t)len;
    REMALLOC(info->bytecode, info->bytecode_sz);
    memcpy(info->bytecode, buf, info->bytecode_sz);
    //copy arg to cpctx
    if(arg_idx != -1)
    {
        int32_t ix;
        char key[16];

        THRLOCK;
        if(varidx == INT32_MAX)
        {
            varidx=0;
            ix=0;
        }
        else
            ix = varidx++;
        THRUNLOCK;

        info->varindex=ix;

        snprintf(key, 16, "\xff%d", (int) ix);
        put_to_clipboard(ctx, arg_idx, key);
    }

    //insert function into function holding object, indexed by info->index
    if(cbfunc_idx!=-1)
    {
        int32_t ix;

        THRLOCK;
        if(cbidx == INT32_MAX)
        {
            cbidx=0;
            ix=0;
        }
        else
            ix = cbidx++;
        THRUNLOCK;

        sprintf(objkey,"c%d", ix);
        if( !duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("rp_thread_callbacks")) )
        {
            duk_push_object(ctx);
            duk_dup(ctx, -1);
            duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("rp_thread_callbacks"));
        }

        duk_dup(ctx, cbfunc_idx);
        duk_put_prop_string(ctx, -2, objkey);
        info->index=ix;
    }

    // add our thr struct;
    info->thr=thr;

    //check as late as possible if our thread closed
    if(!RPTHR_TEST(thr, RPTHR_FLAG_IN_USE) || !thr->base || !RPTHR_TEST(thr, RPTHR_FLAG_BASE))
    {
        if(info->bytecode)
            free(info->bytecode);
        free(info);

        if( event_base_get_num_events(get_current_thread()->base,
            EVENT_BASE_COUNT_ACTIVE|EVENT_BASE_COUNT_ADDED|EVENT_BASE_COUNT_VIRTUAL))
            RP_THROW(ctx, "thr.exec: Thread has already been closed");
        else if (cbfunc_idx!=-1)
        {
            duk_dup(ctx, cbfunc_idx);
            duk_push_object(ctx);
            duk_push_string(ctx, "thr.exec: Thread has already been closed");
            duk_put_prop_string(ctx, -2, "error");
            duk_call(ctx, 1);
            return 0;
        }
        else
        { 
            fprintf(stderr, "thr.exec: Error with no callback: Thread has already been closed\n");
            return 0;
        }
    }
    //insert into thread's event loop
    info->e = event_new(thr->base, -1, 0, thread_doevent, info);
    //printf("adding from %s\n", duk_to_string(ctx, tmpidx));
    rp_debug_printf("adding info event(%p) to base(%p)\n",info->e, thr->base);
    ev_add(info->e, &timeout);

    return 0;
}

static void *do_thread_setup(void *arg)
{
    RPTHR *thr=(RPTHR*)arg;

    //set the thread_local variable so we can always find our thr struct
    set_thread_num(thr->index);

    //start event loop
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

// get thread number of the thread created with new rampart.thread()
// -i.e. number of a thread from outside the thread.
duk_ret_t get_thread_id(duk_context *ctx)
{
    duk_push_this(ctx);

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("thr")))
        return 0;

    return 1;
}

static duk_ret_t new_js_thread(duk_context *ctx)
{
    RPTHR *thr;
    pthread_attr_t attr;
    int keepopen=0;

    if(!duk_is_undefined(ctx,0))
        keepopen=REQUIRE_BOOL(ctx, 0, "new rampart.thread - arugment must be a boolean (keepOpen)");

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
    rp_debug_printf("BASE NEW\n");

    if(keepopen)
        RPTHR_SET(thr, RPTHR_FLAG_KEEP_OPEN);

    duk_push_this(ctx); //return object
    duk_push_int(ctx, (int)thr->index);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("thr"));

    duk_push_c_function(ctx, finalize_thr, 1);
    duk_set_finalizer(ctx, -2);

    duk_push_c_function(ctx, loop_insert, 5);
    duk_put_prop_string(ctx, -2, "exec");

    duk_push_c_function(ctx, get_thread_id, 0);
    duk_put_prop_string(ctx, -2, "getId");

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


// get thread number of current thread
duk_ret_t get_current_id(duk_context *ctx)
{
    duk_push_int(ctx, get_thread_num());
    return 1;
}


//called in cmdline.c
void rp_thread_preinit()
{
    RP_PTINIT(&thr_list_lock);
    REMALLOC(cp_lock, sizeof(pthread_mutex_t));
    rp_cp_lock=RP_MINIT(cp_lock);
    RPTHR_SET(rp_cp_lock, RPTHR_LOCK_FLAG_FREELOCK);
}



/**** init function executed in main thread upon startup *****/

void duk_thread_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    RP_PTINIT(&cblock);

    duk_push_c_function(ctx, new_js_thread, 1);

    duk_push_c_function(ctx, get_current_id, 0);
    duk_put_prop_string(ctx, -2, "getCurrentId");

    duk_push_c_function(ctx, rp_thread_put, 2);
    duk_put_prop_string(ctx, -2, "put");

    duk_push_c_function(ctx, rp_thread_get, 2);
    duk_put_prop_string(ctx, -2, "get");

    duk_push_c_function(ctx, rp_thread_waitfor, 2);
    duk_put_prop_string(ctx, -2, "waitfor");

    duk_push_c_function(ctx, rp_onget, 2);
    duk_put_prop_string(ctx, -2, "onGet");

    duk_push_c_function(ctx, rp_thread_getwait, 2);
    duk_put_prop_string(ctx, -2, "getwait");

    duk_push_c_function(ctx, rp_thread_del, 2);
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
