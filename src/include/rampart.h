/* Copyright (C) 2025 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#if !defined(RP_H_INCLUDED)
#define RP_H_INCLUDED
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include "duktape.h"
#include "sys/queue.h"
#include "rp_string.h"

#if defined(__cplusplus)
extern "C"
{
#endif

// How errors are reported when thrown
extern int rp_print_simplified_errors;
// How many source lines to print with error message
extern int rp_print_error_lines;

/* *******************************************************
   macros to help with require_* and throwing errors with
   a stack trace.
   ******************************************************* */

#define RP_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define RP_SYNTAX_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define REQUIRE_STRING(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_string((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    const char *r=duk_get_string((ctx),__rp_i);\
    r;\
})

#define REQUIRE_LSTRING(ctx,idx,len,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_string((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    const char *r=duk_get_lstring((ctx),__rp_i,(len));\
    r;\
})

#define REQUIRE_INT(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_int((ctx),__rp_i);\
    r;\
})

#define REQUIRE_UINT(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    double rd=duk_get_number((ctx),__rp_i);\
    if(rd<0.0) RP_THROW((ctx), __VA_ARGS__ );\
    unsigned int r = (unsigned int)rd;\
    r;\
})

#define REQUIRE_POSINT(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_int((ctx),__rp_i);\
    if(r<0) RP_THROW((ctx), __VA_ARGS__ );\
    r;\
})

#define REQUIRE_INT64(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int64_t r=(int64_t) duk_get_number((ctx),__rp_i);\
    r;\
})

#define REQUIRE_UINT64(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    double rd=duk_get_number((ctx),__rp_i);\
    if(rd<0.0) RP_THROW((ctx), __VA_ARGS__ );\
    uint64_t r = (uint64_t)rd;\
    r;\
})

#define REQUIRE_BOOL(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_boolean((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_boolean((ctx),__rp_i);\
    r;\
})

#define REQUIRE_NUMBER(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_number((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    double r=duk_get_number((ctx),__rp_i);\
    r;\
})

#define REQUIRE_FUNCTION(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_function((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_OBJECT(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_object((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_PLAIN_OBJECT(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_object((ctx),__rp_i) || duk_is_array((ctx),__rp_i) || duk_is_function((ctx),__rp_i) ) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_ARRAY(ctx,idx,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_array((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_BUFFER_DATA(ctx,idx,sz,...) ({\
    duk_idx_t __rp_i=(idx);\
    if(!duk_is_buffer_data((ctx),__rp_i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    void *r=duk_get_buffer_data((ctx),__rp_i,(sz));\
    r;\
})

#define REQUIRE_STR_TO_BUF(ctx, idx, sz, ...) ({\
    void *r=NULL;\
    duk_idx_t __rp_i=(idx);\
    if( duk_is_string( (ctx), __rp_i ) )\
        r=duk_to_buffer( (ctx), __rp_i, (sz) );\
    else if ( duk_is_buffer_data( (ctx), __rp_i ) )\
        r=duk_get_buffer_data( (ctx), __rp_i, (sz) );\
    else\
        RP_THROW( (ctx), __VA_ARGS__);\
    r;\
})

#define REQUIRE_STR_OR_BUF(ctx, idx, sz, ...) ({\
    const char *r=NULL;\
    duk_idx_t __rp_i=(idx);\
    if( duk_is_string( (ctx), __rp_i ) )\
        r=duk_get_lstring( (ctx), __rp_i, (sz) );\
    else if ( duk_is_buffer_data( (ctx), __rp_i ) )\
        r=(const char *)duk_get_buffer_data( (ctx), __rp_i, (sz) );\
    else\
        RP_THROW( (ctx), __VA_ARGS__);\
    r;\
})

// macro to empty stack
#define RP_EMPTY_STACK(rpctx) duk_set_top((rpctx),0)


/* ***************debugging macros ***************** */
#define printstack(ctx)                           \
    do                                            \
    {                                             \
        duk_push_context_dump((ctx));             \
        printf("%s\n", duk_to_string((ctx), -1)); \
        duk_pop((ctx));                           \
    } while (0)

#define prettyprintstack(ctx)                       \
    do                                              \
    {                                               \
        duk_push_context_dump((ctx));               \
        duk_get_global_string((ctx), "Duktape");    \
        duk_push_string((ctx), "dec");              \
        duk_push_string((ctx), "jx");               \
        duk_pull((ctx), -4);                        \
        const char *res=duk_to_string(((ctx)), -1); \
        char *ob=strstr(res,"stack=")+6;            \
        printf("%.*s\n", (int)(ob-res), res);       \
        duk_push_string((ctx), ob);                 \
        duk_remove((ctx),-2);                       \
        duk_call_prop((ctx), -4, 2);                \
        duk_remove((ctx),-2);                       \
        duk_get_global_string((ctx), "JSON");       \
        duk_push_string((ctx), "stringify");        \
        duk_pull((ctx), -3);                        \
        duk_push_null((ctx));                       \
        duk_push_int((ctx), 4);                     \
        duk_call_prop((ctx), -5, 3);                \
        printf("%s\n", duk_to_string((ctx), -1));   \
        duk_pop_2((ctx));                           \
    } while (0)

//when stack contains cyclic objects
#define safeprintstack(ctx)                           \
    do {                                              \
        duk_idx_t i=0, top=duk_get_top(ctx);          \
        char *s;                                      \
        printf("ctx: top=%d, stack(%p)={\n", (int)top, ctx);   \
        while (i<top) {                               \
            if(i) printf(",\n");                      \
            s=str_rp_to_json_safe(ctx, i, NULL,1);    \
            printf("   %d: %s", (int)i, s);           \
            free(s);                                  \
            i++;                                      \
        }                                             \
        printf("\n}\n");                              \
    } while (0)

//when stack contains cyclic objects
#define safeprettyprintstack(ctx)                           \
    do {                                              \
        duk_idx_t i=0, top=duk_get_top(ctx);          \
        char *s;                                      \
        printf("ctx: top=%d, stack(%p)={\n", (int)top, ctx);   \
        while (i<top) {                               \
            if(i) printf(",\n");                      \
            s=str_rp_to_json_safe(ctx, i, NULL,1);    \
            duk_get_global_string((ctx), "JSON");     \
            duk_push_string(ctx, "parse");            \
            duk_push_string(ctx, s);                  \
            duk_call_prop(ctx, -3, 1);                \
            duk_push_string((ctx), "stringify");      \
            duk_pull(ctx, -2);                        \
            duk_push_null((ctx));                     \
            duk_push_int((ctx), 4);                   \
            duk_call_prop((ctx), -5, 3);              \
            free(s);                                  \
            s=(char*)duk_to_string(ctx, -1);          \
            if(strchr(s,'{'))                         \
                printf("   %d:\n%s", (int)i, s);       \
            else                                      \
                printf("   %d: %s", (int)i, s);      \
            duk_pop_2(ctx);                           \
            i++;                                      \
        }                                             \
        printf("\n}\n");                              \
    } while (0)

#define printenum(ctx, idx)                                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        const char *s;                                                                                               \
        duk_enum((ctx), (idx), DUK_ENUM_NO_PROXY_BEHAVIOR | DUK_ENUM_INCLUDE_NONENUMERABLE |                         \
                               DUK_ENUM_INCLUDE_HIDDEN | DUK_ENUM_INCLUDE_SYMBOLS);                                  \
        while (duk_next((ctx), -1, 1))                                                                               \
        {                                                                                                            \
            s = duk_get_string((ctx), -2);                                                                           \
            if(*s=='\xff') printf("DUK_HIDDEN_SYMBOL(%s) -> %s\n", s+1, duk_safe_to_string((ctx), -1));              \
            else printf("%s -> %s\n", s, duk_safe_to_string((ctx), -1));                                             \
            duk_pop_2((ctx));                                                                                        \
        }                                                                                                            \
        duk_pop((ctx));                                                                                              \
    } while (0)

#define printat(ctx, idx)                                                 \
    do                                                                    \
    {                                                                     \
        char *s=str_rp_to_json_safe(ctx, idx, NULL, 1);                   \
        printf("at %d: %s\n", (int)(idx), s);                             \
        free(s);                                                          \
    } while (0)

#define printfuncs(ctx)                                                                  \
    do                                                                                   \
    {                                                                                    \
        duk_idx_t i = 0;                                                                 \
        while (duk_get_top(ctx) > i)                                                     \
        {                                                                                \
            if (duk_is_function(ctx, i))                                                 \
            {                                                                            \
                duk_get_prop_string(ctx, i, "fname");                                    \
                if (!duk_is_undefined(ctx, -1))                                          \
                {                                                                        \
                    printf("func->fname at %d: %s()\n", (int)i, duk_to_string(ctx, -1)); \
                    duk_pop(ctx);                                                        \
                }                                                                        \
                else                                                                     \
                {                                                                        \
                    duk_pop(ctx);                                                        \
                    duk_get_prop_string(ctx, i, "name");                                 \
                    printf("func->name at %d: %s()\n", (int)i, duk_to_string(ctx, -1));  \
                    duk_pop(ctx);                                                        \
                }                                                                        \
            }                                                                            \
            i++;                                                                         \
        }                                                                                \
    } while (0)

/*********** globals for convenience *******/
extern char **rampart_argv;                  // same as argv
extern int   rampart_argc;                   // same as argc
extern char argv0[PATH_MAX];                 // in rampart-server argv[0] is changed.  Original is saved here.
extern char rampart_exec[PATH_MAX];          // the full path to the executable
extern char rampart_dir[PATH_MAX];           // the base directory
extern char rampart_bin[PATH_MAX];           // the base directory - with /bin if executable is in bin
extern char modules_dir[PATH_MAX];           // where modules live
extern duk_context *main_ctx;                // the context if/when single threaded
extern struct event_base **thread_base;      // each thread (or pair or ctxs) gets an event loop
extern int totnthreads;                      // when threading in server - number of ctx contexts
extern struct evdns_base **thread_dnsbase;   // list of dns resolvers in event loops
extern int nthread_dnsbase;                  // number of above
extern char **environ;
extern char *RP_script_path;
extern char *RP_script;

/*************  Rampart threads ************/

#ifdef __APPLE__

typedef uint64_t rp_tid;
#define rp_gettid() ({\
    uint64_t  tid;\
    if(pthread_threadid_np(NULL, &tid))\
        tid=0;\
    (rp_tid)tid;\
})

#else

//linux
typedef pid_t rp_tid;
#define rp_gettid() (rp_tid) syscall(SYS_gettid)

#endif

/******************** THREAD AND LOCK RELATED *********************/
typedef void (*rpthr_fin_cb)(void *fin_cb_arg);
#define RPTHR struct rampart_thread_s

RPTHR {
    duk_context       *ctx;         // js context paired with this thread.  One of the two below.
    duk_context       *wsctx;       // only for server threads and websocket requests
    duk_context       *htctx;       // only for server threads and NON-websocket requests
    struct event_base *base;        // libevent base for event loop
    struct evdns_base *dnsbase;     // libevent base for event loop
    void             **fin_cb_arg;  // user data array for finalizer callback
    rpthr_fin_cb      *fin_cb;      // finalizer callbacks
    pthread_t          self;        // unneeded now.
    RPTHR             *parent;      // Parent, if not main.  Otherwise NULL
    RPTHR            **children;    // child threads.  monitor and don't exit if still active
    pthread_mutex_t    flaglock;    // lock for flags
    uint16_t           flags;       // some flags
    uint16_t           index;       // index pos in **rpthread; remove if not used
    uint16_t           ncb;         // how many of above fin_cb
    uint16_t           nchildren;   // number of child threads.
    int                reader;      // reader for thread.waitfor()
    int                writer;      // writer for thread.waitfor()
};

#define RPTHR_FLAG_IN_USE     0x01  // if struct is in use and ctxs are set up
#define RPTHR_FLAG_THR_SAFE   0x02  // if we can execute texis or python without forking
#define RPTHR_FLAG_SERVER     0x04  // if called from server (and we need wsctx initialized)
#define RPTHR_FLAG_BASE       0x08  // event base has been created 
#define RPTHR_FLAG_FINAL      0x10  // finalizer called
#define RPTHR_FLAG_FORKED     0x20  // for python - flag that we've already forked
#define RPTHR_FLAG_ACTIVE     0x40  // flag that this thread is running JS in a duk_pcall()
#define RPTHR_FLAG_WAITING    0x80  // flag that we are waiting in thread.waitfor()
#define RPTHR_FLAG_KEEP_OPEN  0x100 //  do not autoclose when thread event base is empty


#define RP_USE_LOCKLOCKS

#define RPTHR_LOCK struct rampart_thread_lock_s
RPTHR_LOCK {
    pthread_mutex_t *lock;
#ifdef RP_USE_LOCKLOCKS
    pthread_mutex_t  locklock;
#endif
    RPTHR_LOCK      *next;
    pthread_mutex_t  flaglock;    // lock for flags
    uint16_t         thread_idx;
    uint16_t         flags;
};

#define RPTHR_LOCK_FLAG_LOCKED    0x01  // if the lock is currently held
#define RPTHR_LOCK_FLAG_JSLOCK    0x02  // if this is a javascript based user lock
#define RPTHR_LOCK_FLAG_TEMP      0x04  // if this is a temporary lock set before forking
#define RPTHR_LOCK_FLAG_JSFIN     0x08  // if this is a lock that will be unlocked in a js finalizer
                                        // as a result, when there is a fork by rampart-server, it 
                                        // will never be, or need to be unlocked.
#define RPTHR_LOCK_FLAG_FREELOCK  0x10  // pthread mutex was alloced. Needs to be freed
#define RPTHR_LOCK_FLAG_REINIT    0x20  // a lock from another thread that could not be unlocked before forking


int           rp_lock(RPTHR_LOCK *thrlock);	    //obtain the lock
int           rp_unlock(RPTHR_LOCK *thrlock);       //release the lock
RPTHR_LOCK   *rp_init_lock(pthread_mutex_t *lock);  //may be null and it will be alloced
int           rp_remove_lock(RPTHR_LOCK *thrlock);  //remove lock from list and dealloc
void          rp_claim_all_locks(); 		    //do before forking
void          rp_unlock_all_locks(int isparent);    //do after forking in parent and child
void          rp_thread_preinit();

/* for locks and threads flags */
extern pthread_mutex_t testset_lock;
#define FLAGLOCK(_thr)   RP_PTLOCK(&_thr->flaglock)
#define FLAGUNLOCK(_thr) RP_PTUNLOCK(&_thr->flaglock)
#define RPTHR_SET(thread, flag)   do{ FLAGLOCK(thread); ( (thread)->flags |=  (flag) ); FLAGUNLOCK(thread);}while(0)
#define RPTHR_CLEAR(thread, flag) do{ FLAGLOCK(thread); ( (thread)->flags &= ~(flag) ); FLAGUNLOCK(thread);}while(0)
#define RPTHR_TEST(thread, flag)  ({uint16_t _ret; FLAGLOCK(thread); _ret=(thread)->flags & (flag); FLAGUNLOCK(thread);_ret;})
#define RPTHR_TESTSET(thr, flag) ({\
    FLAGLOCK(thr);\
    int ret = (thr)->flags & (flag);\
    (thr)->flags |=  (flag);\
    FLAGUNLOCK(thr);\
    ret;\
})

/* mutex locking in general */
#define RP_PTLOCK(lock) do{\
    int e;\
    if ((e=pthread_mutex_lock((lock))) != 0)\
        {fprintf(stderr,"could not obtain lock in %s at %d %d -%s\n",__FILE__,__LINE__,e,strerror(e));exit(1);}\
} while(0)

#define RP_PTUNLOCK(lock) do{\
    if (pthread_mutex_unlock((lock)) != 0)\
        {fprintf(stderr,"could not release lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

#define RP_PTINIT(lock) do{\
    if (pthread_mutex_init((lock),NULL) != 0)\
        {fprintf(stderr,"could not create lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

#define RP_PTINIT_COND(cond) do{\
    if (pthread_cond_init((cond),NULL) != 0)\
        {fprintf(stderr,"could not create pthread_cond in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)


/* mutex locking with tracking for some internal locks and all new rampart.thread.lock()s */
#define RP_MLOCK(lock) do{\
    /*printf("(%d:%d) locking %s %p at %s:%d\n", (int)getpid(), (int)get_thread_num(), #lock, lock, __FILE__, __LINE__);*/\
    if (rp_lock((lock)) != 0)\
        {fprintf(stderr,"could not obtain lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

#define RP_MTRYLOCK(lock) ({\
    int ret=1;\
    /*printf("(%d:%d) trying lock %s %p at %s:%d\n", (int)getpid(), (int)get_thread_num(), #lock, lock, __FILE__, __LINE__);*/\
    ret = rp_trylock((lock));\
    ret;\
})

#define RP_MUNLOCK(lock) do{\
    /*printf("(%d:%d) unlocking %s %p at %s:%d\n", (int)getpid(), (int)get_thread_num(), #lock, lock, __FILE__, __LINE__);*/\
    int r;\
    if ((r=rp_unlock((lock))) != 0)\
        {fprintf(stderr,"could not release lock in %s at %d - ret=%d\n",__FILE__,__LINE__,r);exit(1);}\
} while(0)

#define RP_MINIT(lock) ({\
    RPTHR_LOCK *ret=NULL;\
    /*printf("(%d:%d) creating %s %p at %s:%d\n", (int)getpid(), (int)get_thread_num(), #lock, lock, __FILE__, __LINE__);*/\
    if ( (ret=rp_init_lock((lock))) == NULL )\
        {fprintf(stderr,"could not create lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
    ret;\
})

/* mutex for locking main_ctx when in a thread with other duk stacks open */
// mainly for the server, which abandons the main thread (but keeps it around for its global vars)
extern pthread_mutex_t ctxlock;
extern RPTHR_LOCK *rp_ctxlock;
#define CTXLOCK RP_MLOCK(rp_ctxlock)
#define CTXUNLOCK RP_MUNLOCK(rp_ctxlock)

extern RPTHR **rpthread;                     //every thread in a global array
extern uint16_t nrpthreads;                  //number of threads malloced in **rpthread
extern RPTHR *mainthr;                       //the thread of the main process

// create new thread struct in **rpthread, add base and ctx
// if ctx is NULL, a new context is created
RPTHR *rp_new_thread(uint16_t flags, duk_context *ctx);         // create new thread struct in **rpthread, add base and ctx
struct evdns_base *rp_make_dns_base(
  duk_context *ctx, struct event_base *base);// create dns base for new server

/* copy vars between stacks functions */
int  rpthr_copy_obj    (duk_context *ctx, duk_context *tctx, int objid, int skiprefcnt);
// objects are marked to avoid recursion while copying.  They need to be cleaned of marks.
void rpthr_clean_obj   (duk_context *ctx, duk_context *tctx);
// copy all global vars from one ctx to another tctx
void rpthr_copy_global (duk_context *ctx, duk_context *tctx);
duk_ret_t duk_rp_bytefunc(duk_context *ctx);


/* **************************************************
   like duk_put_prop_string but makes it read only
   ************************************************** */
void duk_rp_put_prop_string_ro(duk_context *ctx, duk_idx_t idx, const char *s);


/* getting, setting thread local thread number */
void set_thread_num(int thrno);  //don't use this outside of rampart-thread.c
int get_thread_num();

// return current thread via above
// convenient for getting the current duktape stack regardless of where you are:
// i.e. duk_context *ctx = get_current_thread()->ctx;
RPTHR *get_current_thread();

// get number of threads that are active
// if alist is not NULL, fill it with an array of ints (active thread numbers)
int get_thread_count(int **alist);

/* cleanup after fork for rampart-server */
void rp_post_fork_clean_threads();

/* closing children at end of main loop when finalizer is not called 
   returns 1 if finalizers were actually set
*/
int rp_thread_close_children();

// set a callback to be run when thread is closed
// may be called more than once.  Executed fifo
// data must be malloced
// see typedef void (*rpthr_fin_cb)(void *fin_cb_arg); above
void set_thread_fin_cb(RPTHR *thr, rpthr_fin_cb cb, void *data);

// a lock for manipulating RPTHR **rpthread and others
extern pthread_mutex_t thr_lock;
extern RPTHR_LOCK *rp_thr_lock;
#define THRLOCK do{RP_MLOCK(rp_thr_lock);/* printf("lock at %s:%d\n",__FILE__,__LINE__);*/}while(0)
#define THRUNLOCK RP_MUNLOCK(rp_thr_lock)

/************* END THREAD AND LOCK RELATED ************************/

// main macro for malloc uses realloc.  Set var to null to initiate
#define REMALLOC(s, t) do{                                        \
    (s) = realloc((s), (t));                                      \
    if ((char *)(s) == (char *)NULL)                              \
    {                                                             \
        fprintf(stderr, "error: realloc(var, %d) in %s at %d\n",  \
            (int)(t), __FILE__, __LINE__);                        \
        abort();                                                  \
    }                                                             \
} while(0)

#define CALLOC(s, t) do{                                          \
    (s) = calloc(1, (t));                                         \
    if ((char *)(s) == (char *)NULL)                              \
    {                                                             \
        fprintf(stderr, "error: calloc(var, %d) in %s at %d\n",   \
            (int)(t), __FILE__, __LINE__);                        \
        abort();                                                  \
    }                                                             \
} while(0)

#ifdef PUTMSG_STDERR
    pthread_mutex_t printlock;
#endif

extern void rp_register_functions(duk_context *ctx);

/*
#define SET_THREAD_UNSAFE(ctx)                                       \
do                                                               \
{                                                                \
    duk_push_false(ctx);                                         \
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("threadsafe")); \
} while (0)
*/

//currently unused, probably can be removed
#define SET_THREAD_UNSAFE(ctx) /* nada */

// used for no timeout in server
#define RP_TIME_T_FOREVER -1

// When in threads and copying vars between, i.e. rampart.thread.put()
// a devoted "clipboard" duktape stack is used

/* copy from/to the clipboard stack */
duk_ret_t get_from_clipboard(duk_context *ctx, char *key);
duk_ret_t del_from_clipboard(duk_context *ctx, char *key);
void put_to_clipboard(duk_context *ctx, duk_idx_t val_idx, char *key);

/* generic copy from ctx at idx to tctx (top of stack) */
void rpthr_copy(duk_context *ctx, duk_context *tctx, duk_idx_t idx);

/* func from rampart-utils.c */
extern void duk_misc_init(duk_context *ctx);
extern char *rp_ca_bundle;

/* query string processing */
#define ARRAYREPEAT 0
#define ARRAYBRACKETREPEAT 1
#define ARRAYCOMMA 2
#define ARRAYJSON 3
char *duk_rp_object2querystring(duk_context *ctx, duk_idx_t qsidx, int atype);
void  duk_rp_querystring2object(duk_context *ctx, char *q);
duk_ret_t duk_rp_object2q(duk_context *ctx);

/* random utility functions. TODO: add descriptions */
char *strcatdup(char *s, char *adds);
char *strjoin(char *s, char *adds, char c);
char *duk_rp_url_encode(char *str, int len);
char *duk_rp_url_decode(char *str, int *len);
void duk_rp_toHex(duk_context *ctx, duk_idx_t idx, int ucase);
int duk_rp_get_int_default(duk_context *ctx, duk_idx_t i, int def);
char *to_utf8(const char *in_str);
duk_ret_t duk_rp_values_from_object(duk_context *ctx, duk_idx_t idx);
duk_ret_t duk_rp_read_file(duk_context *ctx);// rampart.utils.readFile()
// unused, maybe later
// FILE *duk_rp_push_fopen_buffer(duk_context *ctx, size_t chunk);
duk_ret_t duk_rp_push_current_module(duk_context *ctx);
duk_ret_t rp_auto_scandate(duk_context *ctx);

/* getType(): alternate to typeof, with only one answer
  Init cap to distinguish from typeof */
#define RP_TYPE_STRING 0
#define RP_TYPE_ARRAY 1
#define RP_TYPE_NAN 2
#define RP_TYPE_NUMBER 3
#define RP_TYPE_FUNCTION 4
#define RP_TYPE_BOOLEAN 5
#define RP_TYPE_BUFFER 6
#define RP_TYPE_NULL 7
#define RP_TYPE_UNDEFINED 8
#define RP_TYPE_SYMBOL 9
#define RP_TYPE_DATE 10
#define RP_TYPE_OBJECT 11
#define RP_TYPE_FILEHANDLE 12
#define RP_TYPE_UNKNOWN 13
#define RP_NTYPES 14

int rp_gettype(duk_context *ctx, duk_idx_t idx);

//returns a safe json-like string.  prints cyclic references as paths in object.
//needs free
char *str_rp_to_json_safe(duk_context *ctx, duk_idx_t idx, char *r, int dohidden);

// standard error printing after pcall()
// * msg is a message to preppend, or NULL
// * nlines is the number of lines in source file to print
const char* rp_push_error(duk_context *ctx, duk_idx_t eidx, char *msg, int nlines);
// support for above
duk_ret_t rp_get_line(duk_context *ctx, const char *filename, int line_number, int nlines);

/* ******* setTimeout and similar ****** */

/* we might want to do something right before the timeout when using generically */
typedef int (timeout_callback)(void *, int);

/* setTimeout functions */
duk_ret_t duk_rp_insert_timeout(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg, 
        duk_idx_t func_idx, duk_idx_t arg_start_idx, double to);
duk_ret_t duk_rp_set_to(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg);
void timespec_add_ms(struct timespec *ts, duk_double_t add);
duk_double_t timespec_diff_ms(struct timespec *ts1, struct timespec *ts2);

/* event functions */
duk_ret_t duk_rp_trigger_event(duk_context *ctx);

typedef int (net_callback)(void *);

#define RPPATH struct rp_path_s
RPPATH {
    struct stat stat;
    char path[PATH_MAX];
};
RPPATH rp_find_path_vari(char *file, ...);
#define rp_find_path(file, ...) rp_find_path_vari(file, __VA_ARGS__, NULL)

int rp_mkdir_parent(const char *path, mode_t mode);
RPPATH rp_get_home_path(char *file, char *subdir);


/* ****************** babelize in cmdline.c **************** */
extern char *main_babel_opt;
// no options
#define babel_setting_none 0
// remove use strict
#define babel_setting_nostrict 1
// dont exit, return malloced error string or NULL, remove use strict
#define babel_setting_eval 2

const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime, int setting, char *opt);

/* ****************** tickify in cmdline.c **************** */
/* end states for tickify */
#define ST_NONE 0
#define ST_DQ   1
#define ST_SQ   2
#define ST_BT   3
#define ST_TB   4
#define ST_BS   5
#define ST_SR   6  //for string raw

#define ST_PM   20
#define ST_PN   21

char *tickify(char *src, size_t sz, int *err, int *ln);
char *tickify_err(int err);

extern pthread_mutex_t loglock;
extern pthread_mutex_t errlock;
extern FILE *access_fh;
extern FILE *error_fh;

/* ****** functions to be run upon exit or before loop ****** */
typedef void (*rp_vfunc)(void* arg);
void add_exit_func(rp_vfunc func, void *arg);
void duk_rp_exit(duk_context *ctx, int ec);

/* functions to be run before a new thread starts its loop */
void add_b4loop_func(rp_vfunc func, void *arg);
void run_b4loop_funcs();


extern void duk_rp_fatal(void *udata, const char *msg);

/*
#define DUK_USE_FATAL_HANDLER(udata,msg) do { \
    const char *fatal_msg = (msg); \
    (void) udata; \
    fprintf(stderr, "*** FATAL ERROR (%d) (%s:%d) ***: %s\n", (int)getpid(),__FILE__, __LINE__,fatal_msg ? fatal_msg : "no message"); \
    fflush(stderr); \
    abort(); \
} while (0)
*/

/* a list of timeouts pending */
#define EVARGS struct ev_args

EVARGS {
    duk_context *ctx;
    struct event *e;
    double key;
    int repeat;
    struct timeval timeout;
    struct timespec start_time;
    timeout_callback *cb;
    void *cbarg;
    SLIST_ENTRY(ev_args) entries;
};

extern pthread_mutex_t slistlock;
extern RPTHR_LOCK *rp_slistlock;
#define SLISTLOCK RP_MLOCK(rp_slistlock)
#define SLISTUNLOCK RP_MUNLOCK(rp_slistlock)

SLIST_HEAD(slisthead, ev_args);

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar)      \
    for ((var) = SLIST_FIRST((head));                   \
      (var) && ((tvar) = SLIST_NEXT((var), field), 1);  \
      (var) = (tvar))
#endif

#if (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__ || __linux || defined __APPLE__ )
#define USE_SETPROCTITLE
#endif

#if (defined __linux || defined __APPLE__)
#define RP_SPT_NEEDS_INIT
void spt_init(int argc, char *argv[]);
void setproctitle(const char *fmt, ...);
#endif

#ifdef __APPLE__

//clock_gettime for macos < sierra
#ifndef CLOCK_MONOTONIC
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>
#define CLOCK_REALTIME CALENDAR_CLOCK
#define CLOCK_MONOTONIC SYSTEM_CLOCK
#define NEEDS_CLOCK_GETTIME
typedef int clockid_t;
int clock_gettime(clockid_t type, struct timespec *rettime);
#endif //CLOCK_MONOTONIC

#endif //__APPLE__

//#define debug_pipe

#ifdef debug_pipe

#define rp_pipe(x) ({\
    int r=pipe((x));\
    printf("created pipe %d:%d at %s line %d\n",(x)[0], (x)[1], __FILE__,__LINE__);\
    r;\
})
// don't close a pipe twice, it might be recreated elsewhere with same int
#define rp_pipe_close(x,i) ({\
    int p=(x)[(i)];\
    if( p != -111111 ) {\
        close(p);\
        (x)[(i)] = -111111;\
    }\
    else printf("did NOT ");\
    printf("close pipe %d at %s line %d\n", p, __FILE__,__LINE__);\
})

#else //debug_pipe

#define rp_pipe_close(x,i) ({\
    close((x)[(i)]);\
})

#define rp_pipe(x) pipe((x))

#endif //debug_pipe

//duk_console.h -- code is now in rampart-utils.c
void duk_console_init(duk_context *ctx, duk_uint_t flags);
/* Use a proxy wrapper to make undefined methods (console.foo()) no-ops. */
#define DUK_CONSOLE_PROXY_WRAPPER (1U << 0)

/* Flush output after every call. */
#define DUK_CONSOLE_FLUSH (1U << 1)

/* Send output to stdout only (default is mixed stdout/stderr). */
#define DUK_CONSOLE_STDOUT_ONLY (1U << 2)

/* Send output to stderr only (default is mixed stdout/stderr). */
#define DUK_CONSOLE_STDERR_ONLY (1U << 3)

//create thread to watch a pid, exit if it disappears
int rp_watch_pid(pid_t pid, const char *msg);

//modes for rampart-crypto-passwd
#define crypto_passwd_unset 0  //unused
#define crypto_passwd_crypt 1
#define crypto_passwd_md5 2
#define crypto_passwd_apr1 3
#define crypto_passwd_aixmd5 4
#define crypto_passwd_sha256 5
#define crypto_passwd_sha512 6

#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */

#endif /* RP_H_INCLUDED */
