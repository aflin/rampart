#if !defined(DUK_RP_H_INCLUDED)
#define DUK_RP_H_INCLUDED

#include "duktape.h"
#include "texint.h"
#include "texsql.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* settings */
#define nthreads 12                    /* number of threads for evhtp */
#define RESMAX_DEFAULT 10              /* default number of sql rows returned if max is not set */
#define PUTMSG_STDERR                  /* print texis error messages in web server to stdout */
//#define SINGLETHREADED                  /* don't use threads (despite nthreads above) */
#define USEHANDLECACHE                 /* cache texis handles on a per db/query basis */

// this should probably just exit
#define DUKREMALLOC(ctx,s,t)  (s) = realloc( (s) , (t) ); if ((char*)(s)==(char*)NULL){ duk_push_string((ctx),"alloc error"); duk_throw((ctx));}
#define REMALLOC(s,t)  /*printf("malloc=%d\n",(int)t);*/ (s) = realloc( (s) , (t) ); if ((char*)(s)==(char*)NULL){ fprintf( stderr, "error: realloc() "); exit(-1);}

#ifdef SINGLETHREADED
#define totnthreads 1
#else
#define totnthreads (2*nthreads)       /* twice that for ip and ipv6  */
#endif

extern void duk_db_init(duk_context *ctx);
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

#define DB_HANDLE struct db_handle_s_list
DB_HANDLE {
  TEXIS *tx;
  DB_HANDLE *next;
  char *db;
  char *query;
  char putmsg[1024];
};

#define NDB_HANDLES 16



#if defined(__cplusplus)
}
#endif  /* end 'extern "C"' wrapper */

#endif /* DUK_DB_H_INCLUDED */


























