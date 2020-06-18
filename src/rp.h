#if !defined(DUK_RP_H_INCLUDED)
#define DUK_RP_H_INCLUDED

#include "duktape.h"
#include "texint.h"
//#include "texsql.h"
#include "texisapi.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* settings */
#define nthreads 0                     /* number of threads for evhtp, set to 0 to use num of cpu cores */
#define RESMAX_DEFAULT 10              /* default number of sql rows returned if max is not set */
//#define PUTMSG_STDERR                  /* print texis error messages to stderr */
//#define SINGLETHREADED                 /* don't use threads (despite nthreads above) */
#define USEHANDLECACHE                 /* cache texis handles on a per db/query basis */
/* end settings */

/* declare initilization functions here.  Then put them in register.c */
extern void duk_db_init(duk_context *ctx);
extern void duk_ra_init(duk_context *ctx);
extern void duk_printf_init(duk_context *ctx);
extern void duk_misc_init(duk_context *ctx);

/* this should probably just exit */
#define DUKREMALLOC(ctx,s,t)  (s) = realloc( (s) , (t) ); if ((char*)(s)==(char*)NULL){ duk_push_string((ctx),"alloc error"); duk_throw((ctx));}
#define REMALLOC(s,t)  /*printf("malloc=%d\n",(int)t);*/ (s) = realloc( (s) , (t) ); if ((char*)(s)==(char*)NULL){ fprintf( stderr, "error: realloc() "); exit(-1);}

#ifndef SINGLETHREADED

pthread_mutex_t lock;

#ifdef PUTMSG_STDERR
pthread_mutex_t printlock;
#endif

#endif /* end not SINGLETHREADED */

extern void rp_register_functions(duk_context *ctx);
extern TEXIS *newsql(char* db);

#define QUERY_STRUCT struct rp_query_struct

#define QS_ERROR_DB 1
#define QS_ERROR_PARAM 2
#define QS_SUCCESS 0

QUERY_STRUCT 
{
    const char *sql; /* the sql statement (allocated by duk and on its stack) */
    int arryi; /* location of array of parameters in ctx, or -1 */
    int callback; /* location of callback in ctx, or -1 */
    int skip; /* number of results to skip */
    int max;  /* maximum number of results to return */
    char retarray; /* 0 for return object with key as column names, 
                    1 for array with no column names and
                    2 for array with first row populated with column names */
    char err;
}
;

duk_ret_t duk_rp_sql_close(duk_context *ctx);

#define DB_HANDLE struct db_handle_s_list
DB_HANDLE {
  TEXIS *tx;
  DB_HANDLE *next;
  char *db;
  char *query;
  char putmsg[1024];
};

#define NDB_HANDLES 16

char *duk_rp_url_encode(char *str, int len);
char *duk_rp_url_decode(char *str, int len);

#define RP_TIME_T_FOREVER 2147483647


#if defined(__cplusplus)
}
#endif  /* end 'extern "C"' wrapper */

#endif /* DUK_DB_H_INCLUDED */

