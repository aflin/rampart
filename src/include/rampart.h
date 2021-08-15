/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
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

#if defined(__cplusplus)
extern "C"
{
#endif

/* macros to help with require_* and throwing errors with
   a stack trace.
*/

#define RP_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define RP_SYNTAX_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_SYNTAX_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define REQUIRE_STRING(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    const char *r=duk_get_string((ctx),i);\
    r;\
})

#define REQUIRE_LSTRING(ctx,idx,len,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    const char *r=duk_get_lstring((ctx),i,(len));\
    r;\
})

#define REQUIRE_INT(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_int((ctx),i);\
    r;\
})

#define REQUIRE_UINT(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_int((ctx),i);\
    if(r<0) RP_THROW((ctx), __VA_ARGS__ );\
    r;\
})

#define REQUIRE_BOOL(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_boolean((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    int r=duk_get_boolean((ctx),i);\
    r;\
})

#define REQUIRE_NUMBER(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    double r=duk_get_number((ctx),i);\
    r;\
})

#define REQUIRE_FUNCTION(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_function((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_OBJECT(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_object((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
})

#define REQUIRE_BUFFER_DATA(ctx,idx,sz,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_buffer_data((ctx),i)) {\
        RP_THROW((ctx), __VA_ARGS__ );\
    }\
    void *r=duk_get_buffer_data((ctx),i,(sz));\
    r;\
})

#define REQUIRE_STR_TO_BUF(ctx, idx, sz, ...) ({\
    void *r=NULL;\
    duk_idx_t i=(idx);\
    if( duk_is_string( (ctx), i ) )\
        r=duk_to_buffer( (ctx), i, (sz) );\
    else if ( duk_is_buffer( (ctx), i ) )\
        r=duk_get_buffer( (ctx), i, (sz) );\
    else\
        RP_THROW( (ctx), __VA_ARGS__);\
    r;\
})

#define REQUIRE_STR_OR_BUF(ctx, idx, sz, ...) ({\
    const char *r=NULL;\
    duk_idx_t i=(idx);\
    if( duk_is_string( (ctx), i ) )\
        r=duk_get_lstring( (ctx), i, (sz) );\
    else if ( duk_is_buffer( (ctx), i ) )\
        r=(const char *)duk_get_buffer( (ctx), i, (sz) );\
    else\
        RP_THROW( (ctx), __VA_ARGS__);\
    r;\
})

/* debugging macros */
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

#define printenum(ctx, idx)                                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        duk_enum((ctx), (idx), DUK_ENUM_NO_PROXY_BEHAVIOR | DUK_ENUM_INCLUDE_NONENUMERABLE |                         \
                               DUK_ENUM_INCLUDE_HIDDEN | DUK_ENUM_INCLUDE_SYMBOLS);                                  \
        while (duk_next((ctx), -1, 1))                                                                               \
        {                                                                                                            \
            printf("%s -> %s\n", duk_get_string((ctx), -2), duk_safe_to_string((ctx), -1));                          \
            duk_pop_2((ctx));                                                                                        \
        }                                                                                                            \
        duk_pop((ctx));                                                                                              \
    } while (0)

#define printat(ctx, idx)                                                 \
    do                                                                    \
    {                                                                     \
        duk_dup((ctx), idx);                                              \
        printf("at %d: %s\n", (int)(idx), duk_safe_to_string((ctx), -1)); \
        duk_pop((ctx));                                                   \
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


extern char **rampart_argv;              // same as argv
extern int   rampart_argc;               // same as argc
extern char argv0[PATH_MAX];             // in rampart-server argv[0] is changed.  Original is saved here.
extern char rampart_exec[PATH_MAX];      // the full path to the executable
extern char rampart_dir[PATH_MAX];       // the base directory
extern duk_context *main_ctx;            // the context if/when single threaded
extern duk_context **thread_ctx;         // array of ctxs for server, 2 per thread (http and ws)
extern struct event_base *elbase;        // the main event base for the main event loop
extern struct event_base **thread_base;  // each thread (or pair or ctxs) gets an event loop
extern int totnthreads;                  // when threading in server - number of ctx contexts


/* mutex locking in general */
#define RP_MLOCK(lock) do{\
    if (pthread_mutex_lock((lock)) != 0)\
        {fprintf(stderr,"could not obtain lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

#define RP_MUNLOCK(lock) do{\
    if (pthread_mutex_unlock((lock)) != 0)\
        {fprintf(stderr,"could not release lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

#define RP_MINIT(lock) do{\
    if (pthread_mutex_init((lock),NULL) != 0)\
        {fprintf(stderr,"could not create lock in %s at %d\n",__FILE__,__LINE__);exit(1);}\
} while(0)

/* mutex for locking main_ctx when in a thread with other duk stacks open */
extern pthread_mutex_t ctxlock;
#define CTXLOCK RP_MLOCK(&ctxlock)
#define CTXUNLOCK RP_MUNLOCK(&ctxlock)


#define REMALLOC(s, t) 					 \
    (s) = realloc((s), (t));                             \
    if ((char *)(s) == (char *)NULL)                     \
    {                                                    \
        fprintf(stderr, "error: realloc() ");            \
        exit(1);                                         \
    }

#define CALLOC(s, t) 					 \
    (s) = calloc(1, (t));                                \
    if ((char *)(s) == (char *)NULL)                     \
    {                                                    \
        fprintf(stderr, "error: calloc() ");             \
        exit(1);                                         \
    }

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
#define RP_TIME_T_FOREVER 2147483647


/* func from rampart-utils.c */
extern void duk_misc_init(duk_context *ctx);

#define ARRAYREPEAT 0
#define ARRAYBRACKETREPEAT 1
#define ARRAYCOMMA 2
#define ARRAYJSON 3
char *duk_rp_object2querystring(duk_context *ctx, duk_idx_t qsidx, int atype);
void  duk_rp_querystring2object(duk_context *ctx, char *q);

duk_ret_t duk_rp_object2q(duk_context *ctx);
char *strcatdup(char *s, char *adds);
char *strjoin(char *s, char *adds, char c);
char *duk_rp_url_encode(char *str, int len);
char *duk_rp_url_decode(char *str, int *len);
void duk_rp_toHex(duk_context *ctx, duk_idx_t idx, int ucase);
int duk_rp_get_int_default(duk_context *ctx, duk_idx_t i, int def);
char *to_utf8(const char *in_str);
/* we might want to do something right before the timeout when using generically */
typedef int (timeout_callback)(void *, int);
duk_ret_t duk_rp_set_to(duk_context *ctx, int repeat, const char *fname, timeout_callback *cb, void *arg);
void timespec_add_ms(struct timespec *ts, duk_double_t add);
duk_double_t timespec_diff_ms(struct timespec *ts1, struct timespec *ts2);

#define RPPATH struct rp_path_s
RPPATH {
    struct stat stat;
    char path[PATH_MAX];
};
RPPATH rp_find_path(char *file, char *subdir);
int rp_mkdir_parent(const char *path, mode_t mode);
RPPATH rp_get_home_path(char *file, char *subdir);

/* babelize in cmdline.c */
const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime, int exclude_strict);

extern pthread_mutex_t loglock;
extern pthread_mutex_t errlock;
extern FILE *access_fh;
extern FILE *error_fh;
extern int duk_rp_server_logging;

/* functions to be run upon exit */
typedef void (*rp_vfunc)(void* arg);

/* debugging
void add_exit_func_2(rp_vfunc func, void *arg, char *nl);

#define add_exit_func(f, a) do{\
    char nl[128];\
    sprintf(nl,"%s at %d", __FILE__, __LINE__);\
    add_exit_func_2( (f), (a), strdup(nl) );\
} while(0)
*/
void add_exit_func(rp_vfunc func, void *arg);

void duk_rp_exit(duk_context *ctx, int ec);

extern void duk_rp_fatal(void *udata, const char *msg);

#define DUK_USE_FATAL_HANDLER(udata,msg) do { \
    const char *fatal_msg = (msg); /* avoid double evaluation */ \
    (void) udata; \
    fprintf(stderr, "*** FATAL ERROR: %s\n", fatal_msg ? fatal_msg : "no message"); \
    fflush(stderr); \
    abort(); \
} while (0)

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
#define SLISTLOCK RP_MLOCK(&slistlock)
#define SLISTUNLOCK RP_MUNLOCK(&slistlock)

SLIST_HEAD(slisthead, ev_args) tohead;

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


#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */

#endif /* RP_H_INCLUDED */
