#if !defined(RP_H_INCLUDED)
#define RP_H_INCLUDED
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include "duktape/core/duktape.h"
//#include "texint.h"
//#include "texisapi.h"

#if defined(__cplusplus)
extern "C"
{
#endif

extern char **rampart_argv;
extern int   rampart_argc;

#define RP_THROW(ctx,...) do {\
    duk_push_error_object(ctx, DUK_ERR_ERROR, __VA_ARGS__);\
    (void) duk_throw(ctx);\
} while(0)

#define REQUIRE_STRING(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    const char *r=duk_get_string((ctx),i);\
    r;\
})

#define REQUIRE_LSTRING(ctx,idx,len,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_string((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    const char *r=duk_get_lstring((ctx),i,(len));\
    r;\
})

#define REQUIRE_INT(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    int r=duk_get_int((ctx),i);\
    r;\
})

#define REQUIRE_BOOL(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_boolean((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    int r=duk_get_boolean((ctx),i);\
    r;\
})

#define REQUIRE_NUMBER(ctx,idx,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_number((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    double r=duk_get_number((ctx),i);\
    r;\
})

#define REQUIRE_BUFFER_DATA(ctx,idx,sz,...) ({\
    duk_idx_t i=(idx);\
    if(!duk_is_buffer_data((ctx),i)) {\
        RP_THROW(ctx, __VA_ARGS__ );\
    }\
    void *r=duk_get_buffer_data((ctx),i,(sz));\
    r;\
})


/* settings */
#define nthreads 0        /* number of threads for evhtp, set to 0 to use num of cpu cores */
#define RESMAX_DEFAULT 10 /* default number of sql rows returned if max is not set */
//#define PUTMSG_STDERR                  /* print texis error messages to stderr */
//#define SINGLETHREADED                 /* don't use threads (despite nthreads above) */
#define USEHANDLECACHE    /* cache texis handles on a per db/query basis */
                          /* end settings */
#define RESMAX_DEFAULT 10 /* default number of sql rows returned if max is not set */
//#define PUTMSG_STDERR     /* print texis error messages to stderr */
/* end settings */

/* this should probably just exit */
#define DUKREMALLOC(ctx, s, t)                 \
    (s) = realloc((s), (t));                   \
    if ((char *)(s) == (char *)NULL)           \
    {                                          \
        duk_push_string((ctx), "alloc error"); \
        (void)duk_throw((ctx));                \
    }
#define REMALLOC(s, t) /*printf("malloc=%d\n",(int)t);*/ \
    (s) = realloc((s), (t));                             \
    if ((char *)(s) == (char *)NULL)                     \
    {                                                    \
        fprintf(stderr, "error: realloc() ");            \
        exit(-1);                                        \
    }

    pthread_mutex_t lock;

#ifdef PUTMSG_STDERR
    pthread_mutex_t printlock;
#endif

    extern void rp_register_functions(duk_context *ctx);

#define QUERY_STRUCT struct rp_query_struct

#define QS_ERROR_DB 1
#define QS_ERROR_PARAM 2
#define QS_SUCCESS 0

    QUERY_STRUCT
    {
        const char *sql; /* the sql statement (allocated by duk and on its stack) */
        int arryi;       /* location of array of parameters in ctx, or -1 */
        int callback;    /* location of callback in ctx, or -1 */
        int skip;        /* number of results to skip */
        int max;         /* maximum number of results to return */
        char retarray;   /* 0 for return object with key as column names, 
                            1 for array with no column names and
                            2 for array with first row populated with column names 
                            3 for novars                                           */
        char err;
        char getCounts; /* whether to include metamorph counts in return */
    };

    duk_ret_t duk_rp_sql_close(duk_context *ctx);

    char *duk_rp_url_encode(char *str, int len);
    char *duk_rp_url_decode(char *str, int len);

#define SET_THREAD_UNSAFE(ctx)                                       \
    do                                                               \
    {                                                                \
        duk_push_false(ctx);                                         \
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("threadsafe")); \
    } while (0)

#define RP_TIME_T_FOREVER 2147483647

#define printstack(ctx)                           \
    do                                            \
    {                                             \
        duk_push_context_dump((ctx));             \
        printf("%s\n", duk_to_string((ctx), -1)); \
        duk_pop((ctx));                           \
    } while (0)

#define printenum(ctx, idx)                                                                                          \
    do                                                                                                               \
    {                                                                                                                \
        duk_enum((ctx), (idx), DUK_ENUM_INCLUDE_NONENUMERABLE | DUK_ENUM_INCLUDE_HIDDEN | DUK_ENUM_INCLUDE_SYMBOLS); \
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

/* func from rampart-utils.c */
extern void duk_misc_init(duk_context *ctx);
char *duk_rp_object2querystring(duk_context *ctx, duk_idx_t qsidx);
void  duk_rp_querystring2object(duk_context *ctx, char *q);
duk_ret_t duk_rp_object2q(duk_context *ctx);
char *strcatdup(char *s, char *adds);
char *strjoin(char *s, char *adds, char c);
char *duk_rp_url_encode(char *str, int len);
char *duk_rp_url_decode(char *str, int len);
void duk_rp_toHex(duk_context *ctx, duk_idx_t idx, int ucase);
int duk_rp_get_int_default(duk_context *ctx, duk_idx_t i, int def);

#define RPPATH struct rp_path_s
RPPATH {
    struct stat stat;
    char path[PATH_MAX];
};
RPPATH rp_find_path(char *file, char *subdir);
int rp_mkdir_parent(const char *path, mode_t mode);
RPPATH rp_get_home_path(char *file, char *subdir);

/* babelize in cmdline.c */
const char *duk_rp_babelize(duk_context *ctx, char *fn, char *src, time_t src_mtime);

extern pthread_mutex_t loglock;
extern pthread_mutex_t errlock;
extern FILE *access_fh;
extern FILE *error_fh;
extern int duk_rp_server_logging;


#if defined(__cplusplus)
}
#endif /* end 'extern "C"' wrapper */

#endif /* RP_H_INCLUDED */
