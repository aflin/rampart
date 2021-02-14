/* Copyright (C) 2021 Aaron Flin - All Rights Reserved
 * You may use, distribute this code under the
 * terms of the Rampart Open Source License.
 * see rsal.txt for details
 */
#include "txcoreconfig.h"
#include <limits.h>
#include <stdlib.h>
#include <pthread.h>
#include <ctype.h>
#include <float.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "dbquery.h"
#include "texint.h"
#include "texisapi.h"
#include "cgi.h"
#include "rampart.h"
#include "api3.h"
#include "../globals/csv_parser.h"

/* settings */
#define RESMAX_DEFAULT 10 /* default number of sql rows returned if max is not set */
//#define PUTMSG_STDERR                  /* print texis error messages to stderr */
#define USEHANDLECACHE    /* cache texis handles on a per db/query basis */
/* end settings */

#define QUERY_STRUCT struct rp_query_struct

#define QS_ERROR_DB 1
#define QS_ERROR_PARAM 2
#define QS_SUCCESS 0

QUERY_STRUCT
{
    const char *sql;    /* the sql statement (allocated by duk and on its stack) */
    duk_idx_t arr_idx;          /* location of array of parameters in ctx, or -1 */
    duk_idx_t obj_idx;
    duk_idx_t str_idx;
    duk_idx_t callback; /* location of callback in ctx, or -1 */
    int skip;           /* number of results to skip */
    int max;            /* maximum number of results to return */
    char rettype;       /* 0 for return object with key as column names, 
                           1 for array
                           2 for novars                                           */
    char err;
    char getCounts;     /* whether to include metamorph counts in return */
};


duk_ret_t duk_rp_sql_close(duk_context *ctx);


extern int TXunneededRexEscapeWarning;
int texis_resetparams(TEXIS *tx);
int texis_cancel(TEXIS *tx);
#define DB_HANDLE struct db_handle_s_list
    DB_HANDLE
    {
        TEXIS *tx;
        DB_HANDLE *next;
        char *db;
        char *query;
        char putmsg[1024];
    };

#define NDB_HANDLES 16




#ifdef __APPLE__
#include <Availability.h>
#  if __MAC_OS_X_VERSION_MIN_REQUIRED < 101300
#    include "fmemopen.h"
#    include "fmemopen.c"
#  endif
#endif

extern int RP_TX_isforked;

#define TXLOCK if(!RP_TX_isforked) pthread_mutex_lock(&lock);
#define TXUNLOCK if(!RP_TX_isforked) pthread_mutex_unlock(&lock);


/* TODO: make not using handle cache work with cancel */
#ifndef USEHANDLECACHE
#define USEHANDLECACHE
#endif

int db_is_init = 0;
int tx_rp_cancelled = 0;

#define EXIT_IF_CANCELLED \
    if (tx_rp_cancelled)  \
        exit(0);


#ifdef DEBUG_TX_CALLS

#define xprintf(...)                 \
    printf("(%d): ", (int)getpid()); \
    printf(__VA_ARGS__);

#else

#define xprintf(...) /* niente */

#endif

#define TEXIS_OPEN(tdb) ({                        \
    xprintf("Open\n");                            \
    TXLOCK                                        \
    TEXIS *rtx = texis_open((tdb), "PUBLIC", ""); \
    TXUNLOCK                                      \
    EXIT_IF_CANCELLED                             \
    rtx;                                          \
})

#define TEXIS_CLOSE(rtx) ({     \
    xprintf("Close\n");         \
    TXLOCK                      \
    (rtx) = texis_close((rtx)); \
    TXUNLOCK                    \
    EXIT_IF_CANCELLED           \
    rtx;                        \
})

#define TEXIS_PREP(a, b) ({           \
    xprintf("Prep\n");                \
    TXLOCK                            \
    int r = texis_prepare((a), (b));  \
    TXUNLOCK                          \
    EXIT_IF_CANCELLED                 \
    r;                                \
})

#define TEXIS_EXEC(a) ({        \
    xprintf("Exec\n");          \
    TXLOCK                      \
    int r = texis_execute((a)); \
    TXUNLOCK                    \
    EXIT_IF_CANCELLED           \
    r;                          \
})

#define TEXIS_FETCH(a, b) ({           \
    xprintf("Fetch\n");                \
    TXLOCK                             \
    FLDLST *r = texis_fetch((a), (b)); \
    TXUNLOCK                           \
    EXIT_IF_CANCELLED                  \
    r;                                 \
})

#define TEXIS_SKIP(a, b) ({               \
    xprintf("skip\n");                    \
    TXLOCK                                \
    int r = texis_flush_scroll((a), (b)); \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_FLUSH(a) ({               \
    xprintf("skip\n");                    \
    TXLOCK                                \
    int r = texis_flush((a)); \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_PARAM(a, b, c, d, e, f) ({               \
    xprintf("Param\n");                                \
    TXLOCK                                             \
    int r = texis_param((a), (b), (c), (d), (e), (f)); \
    TXUNLOCK                                           \
    EXIT_IF_CANCELLED                                  \
    r;                                                 \
})

DB_HANDLE *g_hcache = NULL;
pid_t g_hcache_pid = 0;

static void free_handle(DB_HANDLE *h, int close_tx)
{
    if (close_tx)
        h->tx = TEXIS_CLOSE(h->tx);
    free(h->db);
    free(h->query);
    free(h);
}

void free_all_handles(void *unused)
{
    DB_HANDLE *next;
    DB_HANDLE *h = g_hcache;
    while (h != NULL)
    {
        next = h->next;
        free_handle(h, 1);
        h = next;
    }
    g_hcache = NULL;
}

void die_nicely(int sig)
{
    DB_HANDLE *next;
    DB_HANDLE *h = g_hcache;
    while (h != NULL)
    {
        next = h->next;
        texis_cancel(h->tx);
        h = next;
    }
    tx_rp_cancelled = 1;
}

static void free_all_handles_noClose(void *unused)
{
    DB_HANDLE *next;
    DB_HANDLE *h = g_hcache;
    while (h != NULL)
    {
        next = h->next;
        free_handle(h, 0);
        h = next;
    }
    g_hcache = NULL;
}

static DB_HANDLE *new_handle(const char *d, const char *q)
{
    DB_HANDLE *h = NULL;
    REMALLOC(h, sizeof(DB_HANDLE));
    h->tx = TEXIS_OPEN((char *)(d));
    if (h->tx == NULL)
    {
        free(h);
        return NULL;
    }
    h->db = strdup(d);
    h->query = strdup(q);
    //h->query=NULL;
    h->next = NULL;
    return (h);
}

/* for debugging *
static void print_handles(DB_HANDLE *head)
{
    DB_HANDLE *h = head;
    if (h == NULL)
        printf("[]\n");
    else
    {
        printf("[\n\tcur=%p, next=%p, q='%s'\n", h, h->next, h->query);
        while (h->next != NULL)
        {
            h = h->next;
            printf("\tcur=%p, next=%p, q='%s'\n", h, h->next, h->query);
        }
        printf("]\n");
    }
}
*/
/* the LRU Texis Handle Cache is indexed by
   query and db. Currently we have to reprep the 
   query each time it is used, negating the
   usefullness of indexing it by query, but 
   there may be a future version in which we 
   can use the same prepped handle and just 
   rewind the database to the first row without 
   having to texis_prep() again.  
   ---
   Turns out it is not very likely we will ever
   be able to skip the texis_prep.
   Indexing by query is still convenient as 
   it allows us to keep an appropriate number of
   handles cached.
   So for now, this structure stays.
*/

/* LRU:
   add a handle to beginning of list
   count number of items on list.
   if one too many, evict last one.
*/
static void add_handle(DB_HANDLE *h)
{
    int i = 1;
    DB_HANDLE *last, *head = g_hcache;
    if (head != NULL)
        h->next = head;
    head = h;
    while (h->next != NULL)
    {
        last = h;
        h = h->next;
        i++;
    }
    if (i == NDB_HANDLES + 1)
    {
        last->next = NULL;
        free_handle(h, 1);
    }
    g_hcache = head;
}

/*  search for handle (starting at h==g_hcache) that has same query and database
    if found, move to beginning of list
    if not, make new, and add to beginning of list
*/
static DB_HANDLE *get_handle(const char *d, const char *q)
{
    DB_HANDLE *last = NULL, *h = g_hcache;

    if (!g_hcache)
    {
        /*
       if(!TXaddabendcb(free_all_handles,(void*)NULL))
       {
           fprintf(stderr,"Error registering TXaddabendcb in db.c\n");
       }
       */
        g_hcache_pid = getpid();
        g_hcache = new_handle(d, q);
        return (g_hcache);
    }
    else if (g_hcache_pid != getpid())
    {
        /* start over. don't use handles from another proc */
        //printf("discarding handles from parent proc\n");
        free_all_handles_noClose(NULL);
        g_hcache = NULL;
        return get_handle(d, q);
    }

    do
    {
        if (!strcmp((d), h->db) && !strcmp((q), h->query))
        {
            if (last != NULL)
            {                         /* if not at beginning of list */
                last->next = h->next; /*remove/pull item out of list*/
                h->next = g_hcache;   /* put this item at the head of the list */
                g_hcache = h;
            } /* else it's already at beginning */
            return (g_hcache);
        }
        last = h;
        h = h->next;
    } while (h != NULL);

    /* got to the end of the list, not found */
    h = new_handle(d, q);
    if (h)
        add_handle(h);
    else
        return (NULL);

    /* requested handle will always be at beginning of list */
    return (g_hcache);
}

/* **************************************************
   Sql.prototype.close 
   ************************************************** */
duk_ret_t duk_rp_sql_close(duk_context *ctx)
{
    SET_THREAD_UNSAFE(ctx);
    free_all_handles(NULL);
    return 0;
}

#define msgbufsz 4096

#ifdef USEHANDLECACHE
#define throw_tx_error(ctx,pref) do{\
    char pbuf[msgbufsz] = {0};\
    duk_rp_log_tx_error(ctx,pbuf);\
    RP_THROW(ctx, "%s error: %s",pref,pbuf);\
}while(0)
#else
#define throw_tx_error(ctx,pref) do{\
    char pbuf[msgbufsz] = {0};\
    if(tx) tx = TEXIS_CLOSE(tx);\
    duk_rp_log_tx_error(ctx,pbuf);\
    RP_THROW(ctx, "%s error: %s",pref,pbuf);\
}while(0)
#endif

#define clearmsgbuf() do {                \
    fseek(mmsgfh, 0, SEEK_SET);           \
    fwrite("\0", 1, 1, mmsgfh);           \
    fseek(mmsgfh, 0, SEEK_SET);           \
} while(0)

/* sz and strlen(buf) vary by platform.
   ignore for now
*/
#define msgtobuf(buf)  do {                       \
    size_t sz;                                    \
    int pos = ftell(mmsgfh);                      \
    fseek(mmsgfh, 0, SEEK_SET);                   \
    sz=fread(buf, 1, msgbufsz-1, mmsgfh);         \
    if(0 & (sz>1) && sz != strlen(buf))           \
        fprintf(stderr, "msgtobuf read error\n"); \
    fclose(mmsgfh);                               \
    mmsgfh = fmemopen(NULL, 4096, "w+");          \
    buf[pos]='\0';                                \
} while(0)

/* **************************************************
     store an error string in this.errMsg 
   **************************************************   */
void duk_rp_log_error(duk_context *ctx, char *pbuf)
{
    duk_push_this(ctx);
    if(duk_has_prop_string(ctx,-1,"errMsg") )
    {
        duk_get_prop_string(ctx,-1,"errMsg");
        duk_push_string(ctx, pbuf);
        duk_concat(ctx,2);
    }
    else
        duk_push_string(ctx, pbuf);

#ifdef PUTMSG_STDERR
    if (pbuf && strlen(pbuf))
    {
        pthread_mutex_lock(&printlock);
        fprintf(stderr, "%s\n", pbuf);
        pthread_mutex_unlock(&printlock);
    }
#endif
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
}

void duk_rp_log_tx_error(duk_context *ctx, char *buf)
{
    TXLOCK
    msgtobuf(buf);
    TXUNLOCK
    duk_rp_log_error(ctx, buf);
}

/* get the expression from a /pattern/ or a "string" */
static const char *get_exp(duk_context *ctx, duk_idx_t idx)
{
    const char *ret=NULL;

    if(duk_is_object(ctx,idx) && duk_has_prop_string(ctx,idx,"source") )
    {
        duk_get_prop_string(ctx,idx,"source");
        ret=duk_get_string(ctx,-1);
        duk_pop(ctx);
    }
    else if ( duk_is_string(ctx,idx) )
        ret=duk_get_string(ctx,idx);

    return ret;
}


#include "db_misc.c" /* copied and altered thunderstone code for stringformat and abstract */


/* **************************************************
  push a single field from a row of the sql results
   ************************************************** */
void duk_rp_pushfield(duk_context *ctx, FLDLST *fl, int i)
{
    char type = fl->type[i] & 0x3f;

    switch (type)
    {
    case FTN_CHAR:
    case FTN_INDIRECT:
    {
        duk_push_string(ctx, (char *)fl->data[i]);
        break;
    }
    case FTN_STRLST:
    {
        ft_strlst *p = (ft_strlst *)fl->data[i];
        char *s = p->buf;
        size_t l = strlen(s);
        char *end = s + (p->nb);
        int j = 0;

        duk_push_array(ctx);
        while (s < end)
        {
            duk_push_string(ctx, s);
            duk_put_prop_index(ctx, -2, j++);
            s += l;
            while (s < end && *s == '\0')
                s++;
            l = strlen(s);
        }
        break;
    }
    case FTN_INT:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_int *)fl->data[i]));
        break;
    }
    /*        push_number with (duk_t_double) cast, 
              53bits of double is the best you can
              do for exact whole numbers in javascript anyway */
    case FTN_INT64:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_int64 *)fl->data[i]));
        break;
    }
    case FTN_UINT64:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_uint64 *)fl->data[i]));
        break;
    }
    case FTN_INTEGER:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_integer *)fl->data[i]));
        break;
    }
    case FTN_LONG:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_long *)fl->data[i]));
        break;
    }
    case FTN_SMALLINT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_smallint *)fl->data[i]));
        break;
    }
    case FTN_SHORT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_short *)fl->data[i]));
        break;
    }
    case FTN_DWORD:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_dword *)fl->data[i]));
        break;
    }
    case FTN_WORD:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_word *)fl->data[i]));
        break;
    }
    case FTN_DOUBLE:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_double *)fl->data[i]));
        break;
    }
    case FTN_FLOAT:
    {
        duk_push_number(ctx, (duk_double_t) * ((ft_float *)fl->data[i]));
        break;
    }
    case FTN_DATE:
    {
        /* equiv to js "new Date(seconds*1000)" */
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) * ((ft_date *)fl->data[i]));
        duk_new(ctx, 1);
        break;
    }
    case FTN_COUNTER:
    {
        char s[33];
        //void *v=NULL;
        ft_counter *acounter = fl->data[i];

        //duk_push_object(ctx);
        snprintf(s, 33, "%lx%lx", acounter->date, acounter->seq);
        duk_push_string(ctx, s);
        /*
        duk_put_prop_string(ctx, -2, "counterString");
        
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) acounter->date);
        duk_new(ctx, 1);
        duk_put_prop_string(ctx, -2, "counterDate");

        duk_push_number(ctx, (duk_double_t) acounter->seq);
        duk_put_prop_string(ctx, -2, "counterSequence");

        */

        break;
    }
    case FTN_BYTE:
    {
        unsigned char *p;

        /* create backing buffer and copy data into it */
        p = (unsigned char *)duk_push_fixed_buffer(ctx, fl->ndata[i] /*size*/);
        memcpy(p, fl->data[i], fl->ndata[i]);
        break;
    }
    default:
        duk_push_int(ctx, (int)type);
    }
}


/* **************************************************
    initialize query struct
   ************************************************** */
void duk_rp_init_qstruct(QUERY_STRUCT *q)
{
    q->sql = (char *)NULL;
    q->arr_idx = -1;
    q->str_idx = -1;
    q->obj_idx=-1;
    q->callback = -1;
    q->skip = 0;
    q->max = RESMAX_DEFAULT;
    q->rettype = 0;
    q->getCounts = 1;
    q->err = QS_SUCCESS;
}

/* **************************************************
   get up to 4 parameters in any order. 
   object=settings, string=sql, 
   array=params to sql, function=callback 
   example: 
   sql.exec(
     "select * from SYSTABLES where NAME=?",
     ["mytable"],
     {max:1,skip:0,returnType:"array:},
     function (row) {console.log(row);}
   );
   ************************************************** */
/* TODO: leave stack as you found it */

QUERY_STRUCT duk_rp_get_query(duk_context *ctx)
{
    int i = 0, gotsettings=0;
    QUERY_STRUCT q_st;
    QUERY_STRUCT *q = &q_st;

    duk_rp_init_qstruct(q);

    for (i = 0; i < 4; i++)
    {
        int vtype = duk_get_type(ctx, i);
        switch (vtype)
        {
            case DUK_TYPE_STRING:
            {
                int l;
                if (q->sql != (char *)NULL)
                {
                    duk_rp_log_error(ctx, "Only one string may be passed as a parameter and must be a sql statement.\n");
                    duk_push_int(ctx, -1);
                    q->sql = (char *)NULL;
                    q->err = QS_ERROR_PARAM;
                    return (q_st);
                }
                q->sql = duk_get_string(ctx, i);
                q->str_idx=i;
                l = strlen(q->sql) - 1;
                while (*(q->sql + l) == ' ' && l > 0)
                    l--;
                if (*(q->sql + l) != ';')
                {
                    duk_dup(ctx, i);
                    duk_push_string(ctx, ";");
                    duk_concat(ctx, 2);
                    duk_replace(ctx, i);
                    q->sql = (char *)duk_get_string(ctx, i);
                }
                break;
            }
            case DUK_TYPE_OBJECT:
            {
                /* array of parameters*/

                if (duk_is_array(ctx, i) && q->arr_idx == -1)
                    q->arr_idx = i;

                /* argument is a function, save where it is on the stack */
                else if (duk_is_function(ctx, i))
                {
                    q->callback = i;
                }

                /* object of settings or parameters*/
                else
                {

                    /* the first object with these properties is our settings object */
                    if(!gotsettings)
                    {
                        if (duk_get_prop_string(ctx, i, "includeCounts"))
                        {
                            q->getCounts = duk_get_boolean_default(ctx, -1, 1);
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "skipRows"))
                        {
                            q->skip = duk_rp_get_int_default(ctx, -1, 0);
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "maxRows"))
                        {
                            q->max = duk_rp_get_int_default(ctx, -1, q->max);
                            if (q->max < 0)
                                q->max = INT_MAX;
                            gotsettings=1;
                        }

                        if (duk_get_prop_string(ctx, i, "returnType"))
                        {
                            const char *rt = duk_to_string(ctx, -1);

                            if (!strcasecmp("array", rt))
                            {
                                q->rettype = 1;
                            }
                            else if (!strcasecmp("novars", rt))
                            {
                                q->rettype = 2;
                            }
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if(gotsettings)
                            break;
                    }
                    
                    if ( q->arr_idx == -1 && q->obj_idx == -1)
                    {
                        q->obj_idx = i;
                        break;
                    }
                }
                break;
            } /* case */
        } /* switch */
    }     /* for */
    if (q->sql == (char *)NULL)
    {
        q->err = QS_ERROR_PARAM;
        duk_rp_log_error(ctx, "No sql statement present.\n");
        duk_push_int(ctx, -1);
    }
    return (q_st);
}

#define push_sql_param do{\
    switch (duk_get_type(ctx, -1))\
    {\
        case DUK_TYPE_NUMBER:\
        {\
            double floord;\
            d = duk_get_number(ctx, -1);\
            floord = floor(d);\
            if( (d - floord) > 0.0 || (d - floord) < 0.0)\
            {\
                v = (double *)&d;\
                plen = sizeof(double);\
                in = SQL_C_DOUBLE;\
                out = SQL_DOUBLE;\
            }\
            else\
            {\
                lval = (long) floord;\
                v = (long *)&lval;\
                plen = sizeof(long);\
                in = SQL_C_LONG;\
                out = SQL_INTEGER;\
            }\
            break;\
        }\
        /* all objects are converted to json string\
           this works (or will work) for several datatypes (varchar,int(x),strlst,json varchar) */\
        case DUK_TYPE_OBJECT:\
        {\
            char *e;\
            char *s = v = (char *)duk_json_encode(ctx, -1);\
            plen = strlen(v);\
            e = s + plen - 1;\
            /* a date (and presumably other single values returned from an object which returns a string)\
             will end up in quotes upon conversion, we need to remove them */\
            if (*s == '"' && *e == '"' && plen > 1)\
            {\
                /* duk functions return const char* */\
                v = s + 1;\
                plen -= 2;\
            }\
            in = SQL_C_CHAR;\
            out = SQL_VARCHAR;\
            break;\
        }\
        /* insert binary data from a buffer */\
        case DUK_TYPE_BUFFER:\
        {\
            duk_size_t sz;\
            v = duk_get_buffer_data(ctx, -1, &sz);\
            plen = (long)sz;\
            in = SQL_C_BINARY;\
            out = SQL_BINARY;\
            break;\
        }\
        /* default for strings (not converted)and\
           booleans, null and undefined (converted to \
           true/false, "null" and "undefined" respectively */\
        default:\
        {\
            v = (char *)duk_to_string(ctx, -1);\
            plen = strlen(v);\
            in = SQL_C_CHAR;\
            out = SQL_VARCHAR;\
        }\
    }\
} while(0)


int duk_rp_add_named_parameters(
    duk_context *ctx, 
    TEXIS *tx, 
    duk_idx_t obj_loc, 
    char **namedSqlParams, 
    int nParams
)
{
    int rc=0, i=0;

    for(i=0;i<nParams;i++)
    {
        char *key = namedSqlParams[i];
        void *v;   /* value to be passed to db */
        long plen; /* lenght of value */
        double d;  /* for numbers */
        long lval;
        int in, out;

        if(!duk_get_prop_string(ctx, obj_loc, key))
            RP_THROW(ctx, "sql - could not find named parameter '%s' in supplied object", key);
        /* check the datatype of the array member */
        push_sql_param;

        /* texis_params is indexed starting at 1 */
        rc = TEXIS_PARAM(tx, i+1, v, &plen, in, out);
        duk_pop(ctx);

        if (!rc)
            return 0;
    }
    return 1;
}

/* **************************************************
   Push parameters to the database for parameter 
   substitution (?) is sql. 
   i.e. "select * from tbname where col1 = ? and col2 = ?"
     an array of two values should be passed and will be
     processed here.
     arrayi is the place on the stack where the array lives.
   ************************************************** */

int duk_rp_add_parameters(duk_context *ctx, TEXIS *tx, duk_idx_t arr_loc)
{
    int rc=0;
    duk_uarridx_t arr_i = 0;

    /* Array is at arr_loc. Iterate over members.
       arr_i is the index of the array member we are examining */
    while (duk_has_prop_index(ctx, arr_loc, arr_i))
    {
        void *v;   /* value to be passed to db */
        long plen; /* lenght of value */
        double d;  /* for numbers */
        long lval;
        int in, out;

        /* push array member to top of stack */
        duk_get_prop_index(ctx, arr_loc, arr_i);

        /* check the datatype of the array member */
        push_sql_param;
        arr_i++;
        rc = TEXIS_PARAM(tx, (int)arr_i, v, &plen, in, out);
        duk_pop(ctx);
        if (!rc)
            return 0;
    }
    return 1;
}

#define pushcounts do{\
    duk_push_object(ctx);\
    duk_push_number(ctx,(double)cinfo.indexCount );\
    duk_put_prop_string(ctx,-2,"indexCount");\
    duk_push_number(ctx,(double)cinfo.rowsMatchedMin );\
    duk_put_prop_string(ctx,-2,"rowsMatchedMin");\
    duk_push_number(ctx,(double)cinfo.rowsMatchedMax );\
    duk_put_prop_string(ctx,-2,"rowsMatchedMax");\
    duk_push_number(ctx,(double)cinfo.rowsReturnedMin );\
    duk_put_prop_string(ctx,-2,"rowsReturnedMin");\
    duk_push_number(ctx,(double)cinfo.rowsReturnedMax );\
    duk_put_prop_string(ctx,-2,"rowsReturnedMax");\
}while(0);


/* **************************************************
   This is called when sql.exec() has no callback.
   fetch rows and push results to array 
   return number of rows 
   ************************************************** */
int duk_rp_fetch(duk_context *ctx, TEXIS *tx, QUERY_STRUCT *q)
{
    int rown = 0, i = 0,
        resmax = q->max,
        rettype = q->rettype;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->getCounts)
        texis_getCountInfo(tx,&cinfo);
    /* create return object */
    duk_push_object(ctx);

    /* create results array (outer array if rettype>0) */
    duk_push_array(ctx);

    /* array of arrays or novars requested */
    if (rettype)
    {

        /* still fill columns if rexmax == 0 */
        /* WTF: return columns if table is empty */
        if (resmax < 1)
        {
            if((fl = TEXIS_FETCH(tx, -1)))
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }        

        } else
        /* push values into subarrays and add to outer array */
        while (rown < resmax && (fl = TEXIS_FETCH(tx, -1)))
        {
            /* novars, we need to get each row (for del and return count value) but not return any vars */
            if (rettype == 2)
            {
                rown++;
                continue;
            }
            /* we want first row to be column names */
            if (!rown)
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }

            /* push values into array */
            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_rp_pushfield(ctx, fl, i);
                duk_put_prop_index(ctx, -2, i);
            }
            duk_put_prop_index(ctx, -2, rown++);

        } /* while */
    }
    else
    /* array of objects requested
     push object of {name:value,name:value,...} into return array */
    {
        while (rown < resmax && (fl = TEXIS_FETCH(tx, -1)))
        {
            if (!rown)
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_string(ctx, -3, "columns");
            }

            duk_push_object(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_rp_pushfield(ctx, fl, i);
                //duk_dup_top(ctx);
                //printf("%s -> %s\n",fl->name[i],duk_to_string(ctx,-1));
                //duk_pop(ctx);
                duk_put_prop_string(ctx, -2, (const char *)fl->name[i]);
            }
            duk_put_prop_index(ctx, -2, rown++);
        }
    }

    duk_put_prop_string(ctx,-2,"results");
    if(q->getCounts)
    {
        pushcounts;
        duk_put_prop_string(ctx,-2,"countInfo");
    }
    duk_push_int(ctx, rown);
    duk_put_prop_string(ctx,-2,"rowCount");
    
    return (rown);
}

/* **************************************************
   This is called when sql.exec() has a callback function 
   Fetch rows and execute JS callback function with 
   results. 
   Return number of rows 
   ************************************************** */
int duk_rp_fetchWCallback(duk_context *ctx, TEXIS *tx, QUERY_STRUCT *q)
{
    int rown = 0, i = 0,
        resmax = q->max,
        rettype = q->rettype;
    duk_idx_t callback_idx = q->callback, colnames_idx=0, count_idx=-1;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->getCounts)
    {
        texis_getCountInfo(tx,&cinfo);
        pushcounts;             /* countInfo */
        count_idx=duk_get_top_index(ctx);
    }
    while (rown < resmax && (fl = TEXIS_FETCH(tx, -1)))
    {

        if (!rown)
        {
            /* an array of column names */
            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_push_string(ctx, fl->name[i]);
                duk_put_prop_index(ctx, -2, i);
            }
            colnames_idx=duk_get_top_index(ctx); 
        }

        duk_dup(ctx, callback_idx); /* the function */
        duk_push_this(ctx);         /* calling with this */

        switch (rettype)
        {
            /* object requested */
            case 0:
            {
                duk_push_object(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_rp_pushfield(ctx, fl, i);
                    duk_put_prop_string(ctx, -2, (const char *)fl->name[i]);
                }
                duk_push_int(ctx, rown++ );
                duk_dup(ctx, colnames_idx);

                if(q->getCounts)
                {
                    duk_dup(ctx, count_idx);  /* countInfo */
                    duk_call_method(ctx, 4);/* function({_res_},resnum,colnames,info){} */
                }
                else
                    duk_call_method(ctx, 3);

                break;
            }
            /* array */
            case 1:
            {
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_rp_pushfield(ctx, fl, i);
                    duk_put_prop_index(ctx, -2, i);
                }

                duk_push_int(ctx, rown++ );
                duk_dup(ctx, colnames_idx);
                if(q->getCounts)
                {
                    duk_dup(ctx, count_idx);  /* countInfo */
                    duk_call_method(ctx, 4);/* function([_res_],resnum,colnames,info){} */
                }
                else
                    duk_call_method(ctx, 3);

                break;
            }
            /* novars */
            case 2:
            {
                duk_push_object(ctx);       /*empty object */
                duk_push_int(ctx, rown++ ); /* index */
                duk_dup(ctx, colnames_idx);

                if(q->getCounts)
                {
                    duk_dup(ctx, count_idx);  /* countInfo */
                    duk_call_method(ctx, 4);  /* function({_empty_},resnum,colnames,info){} */
                }
                else
                    duk_call_method(ctx, 3);

                break;
            }
        } /* switch */
        /* if function returns false, exit while loop, return number of rows so far */
        if (duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
        {
            duk_pop(ctx);
            return (rown);
        }
        /* get rid of ret value from callback*/
        duk_pop(ctx);
    } /* while fetch */

    return (rown);
}
#undef pushcounts

/* ************************************************** 
   Sql.prototype.import
   ************************************************** */
duk_ret_t duk_rp_sql_import(duk_context *ctx, int isfile)
{
    /* currently only csv
       but eventually add option for others
       by checking options object for
       "type":"filetype" - with default "csv"
    */
    const char *func_name = isfile?"sql.importCsvFile":"sql.importCsv";
    DCSV dcsv=duk_rp_parse_csv(ctx, isfile, 1, func_name);
    int ncols=dcsv.csv->cols, i=0;
    int tbcols=0, start=0;

    TEXIS *tx=NULL;
    char **field_names=NULL;

#define closecsv do {\
    int col;\
    for(col=0;col<dcsv.csv->cols;col++) \
        free(dcsv.hnames[col]); \
    free(dcsv.hnames); \
    closeCSV(dcsv.csv); \
} while(0)


    if(strlen(dcsv.tbname)<1)
        RP_THROW(ctx, "%s(): option tableName is required", func_name);

    const char *db;
    char pbuf[msgbufsz];
    struct sigaction sa = { {0} };

    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    //  signal(SIGUSR1, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        closecsv;
        RP_THROW(ctx, "no database has been opened");
    }
    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    {
        FLDLST *fl;
        char sql[128];
       
        snprintf(sql, 128, "select NAME from SYSCOLUMNS where TBNAME='%s' order by ORDINAL_POSITION;", dcsv.tbname);
        
        tx = TEXIS_OPEN((char *)db);

        if (!tx)
        {
            closecsv;
            throw_tx_error(ctx,"open sql");
        }

        if (!TEXIS_PREP(tx, sql))
        {
            closecsv;
            throw_tx_error(ctx,"sql prep");
        }

        if (!TEXIS_EXEC(tx))
        {
            closecsv;
            throw_tx_error(ctx,"sql exec");
        }


        while((fl = TEXIS_FETCH(tx, -1)))
        {
            /* an array of column names */
            DUKREMALLOC(ctx, field_names, (tbcols+2) * sizeof(char*) );
            
            field_names[tbcols] = strdup(fl->data[0]);
            tbcols++;
        }        
        field_names[tbcols] = NULL;

    }

#define fn_cleanup do { \
    int j=0; \
    while(field_names[j]!=NULL) \
        free(field_names[j++]); \
    free(field_names); \
    closecsv; \
} while(0);

    
    {
        /* ncols = number of columns in the csv
           tbcols = number of columns in the table   */
        int col_order[tbcols]; /* one per table column and value is 0 -> ncols-1 */

        if(dcsv.arr_idx>-1)
        {
            duk_idx_t idx=dcsv.arr_idx;
            int aval=0, len= (int)duk_get_length(ctx, idx);
            char **hn=dcsv.hnames;
            
            for(i=0;i<len;i++)
            {
                duk_get_prop_index(ctx, idx, (duk_uarridx_t)i);
                if( duk_is_string(ctx, -1))
                {
                    int j=0;
                    const char *s=duk_get_string(ctx, -1);
                    if (strlen(s)==0)
                        aval=-1;
                    else
                    {
                        while (hn[j]!=NULL)
                        {
                            if(strcmp(hn[j],s)==0)
                            {
                                aval=j;
                                break;
                            }
                            j++;
                        }
                        if (hn[j]==NULL)
                        {
                            fn_cleanup;
                            RP_THROW(ctx, "%s(): array contains '%s', which is not a known column name", func_name,s);
                        }
                    }
                }
                else if(! duk_is_number(ctx, -1))
                {
                    fn_cleanup;
                    RP_THROW(ctx, "%s(): array requires an array of Integers/Strings (column numbers/names)", func_name);
                }
                else
                    aval=duk_get_int(ctx, -1);

                duk_pop(ctx);
                if( aval>=ncols )
                {
                    fn_cleanup;
                    RP_THROW(ctx, "%s(): array contains column number %d. There are %d columns in the csv (numbered 0-%d)",
                        func_name, aval, ncols, ncols-1);
                }
                col_order[i]=aval;
                //printf("order[%d]=%d\n",i,aval);
            }
            /* fill rest, if any, with -1 */
            for (i=len; i<tbcols; i++)
                col_order[i]=-1;
        }    
        else
        {
            /* insert order is col order */
            for(i=0;i<tbcols;i++)
                col_order[i]=i;
        }
        
        {
            int slen = 24 + strlen(dcsv.tbname) + (2*tbcols) -1;
            char sql[slen];
            CSV *csv = dcsv.csv;
            void *v=NULL;   /* value to be passed to db */
            long plen, l; /* lenght of value */
            int in=0, out=0, row=0, col=0;

            snprintf(sql, slen, "insert into %s values (", dcsv.tbname);
            for (i=0;i<tbcols-1;i++)
                strcat(sql,"?,");
            
            strcat(sql,"?);");

            //printf("%s, %d, %d\n", sql, slen, (int)strlen(sql) );

            if (!TEXIS_PREP(tx, sql))
            {
                fn_cleanup;
                throw_tx_error(ctx,"sql prep");
            }

            if(dcsv.hasHeader) start=1;
            for(row=start;row<csv->rows;row++)      // iterate through the CSVITEMS contained in each row and column
            {
                for(  col=0; /* col<csv->cols && */col<tbcols; col++)
                {
                    if(col_order[col]>-1)
                    {
                    //printf("doing col_order[%d] = %d\n", col, col_order[col]);
                        CSVITEM item=csv->item[row][col_order[col]];
                        switch(item.type)
                        {
                            case integer:        
                                in=SQL_C_INTEGER;
                                out=SQL_INTEGER;
                                v=(long*)&item.integer;
                                plen=sizeof(long);
                                break;
                            case floatingPoint:  
                                in=SQL_C_DOUBLE;
                                out=SQL_DOUBLE;
                                v=(double *)&item.integer;
                                plen=sizeof(double);
                                break;
                            case string:
                                v = (char *)item.string;
                                plen = strlen(v);
                                in = SQL_C_CHAR;
                                out = SQL_VARCHAR;
                                break;
                            case dateTime:
                            {
                                struct tm *t=&item.dateTime;
                                in=SQL_C_LONG;
                                out=SQL_DATE;
                                l=(long) mktime(t);
                                v = (long*)&l;
                                plen=sizeof(long);
                                break;
                            }
                            case nil:
                                l=0;
                                in=SQL_C_INTEGER;
                                out=SQL_INTEGER;
                                v=(long*)&l;
                                plen=sizeof(long);
                                break;
                        }
                    }
                    else
                    {
                        l=0;
                        v=(long*)&l;
                        plen=sizeof(long);
                        in=SQL_C_INTEGER;
                        out=SQL_INTEGER;
                    }
                    if( !TEXIS_PARAM(tx, col+1, v, &plen, in, out))
                    {
                        fn_cleanup;
                        throw_tx_error(ctx,"sql add parameters");
                    }


                }
                if (col<tbcols)
                {
                    l=0;
                    v=(long*)&l;
                    plen=sizeof(long);
                    in=SQL_C_INTEGER;
                    out=SQL_INTEGER;
                    for(; col<tbcols; col++)
                    {
                        if( !TEXIS_PARAM(tx, col+1, v, &plen, in, out))
                        {
                            fn_cleanup;
                            throw_tx_error(ctx,"sql add parameters");
                        }
                    }
                }
                
                if (!TEXIS_EXEC(tx))
                {
                    fn_cleanup;
                    throw_tx_error(ctx,"sql exec");
                }
                if (!TEXIS_FLUSH(tx))
                {
                    fn_cleanup;
                    throw_tx_error(ctx,"sql flush");
                }

                texis_resetparams(tx);

                if (dcsv.func_idx > -1 && !( (row-start) % dcsv.cbstep ) )
                {
                    duk_dup(ctx, dcsv.func_idx);
                    duk_push_int(ctx, row-start);
                    duk_call(ctx, 1);
                    if(duk_is_boolean(ctx, -1) && ! duk_get_boolean(ctx, -1) )
                        goto funcend;
                    duk_pop(ctx);
                }
            }
        }
    }

    funcend:

    duk_push_int(ctx, dcsv.csv->rows - start);
    fn_cleanup;    
    duk_rp_log_tx_error(ctx,pbuf); /* log any non fatal errors to this.errMsg */
    tx=TEXIS_CLOSE(tx);
    return 1;
}

duk_ret_t duk_rp_sql_import_csv_file(duk_context *ctx)
{
    return duk_rp_sql_import(ctx, 1);
}

duk_ret_t duk_rp_sql_import_csv_str(duk_context *ctx)
{
    return duk_rp_sql_import(ctx, 0);
}

/*
   Finds the closing char c. Used for finding single and double quotes
   Tries to deal with escapements
   Returns a pointer to the end of string or the matching character
   *pN is stuffed withe the number of characters it skipped
*/
static char * skip_until_c(char *s,int c,int *pN)
{
   *pN=0;                        // init the character counter to 0
   while(*s)
   {
      if(*s=='\\' && *(s+1)==c)  // deal with escapement
      {
         ++s;
         ++*pN;
      }
      else
      if(*s==c)
       return(s);
      ++s;
      ++*pN;
   }
  return(s);
}

// counts the number of ?'s in the sql string
static int count_sql_parameters(char *s)
{
   int n_params=0;
   int unused;
   while(*s)
   {
      switch(*s)
      {
         case '"' :
         case '\'': s=skip_until_c(s+1,*s,&unused);break;
         case '\\': ++s; break;
         case '?' : ++n_params;break;
      }
     ++s;
   }
   return (n_params);
}

/*
   This parses parameter names out of SQL statements.
   
   Parameter names must be int the form of:
      ?legal_SQL_variable_name   where legal is (\alpha || \digit || _)+
      or
      ?" almost anything "
      or
      ?' almost anything '
      
   Returns the number of parameters or -1 if there's syntax error involving parameters
   It removes the names from the SQL and places the result in *new_sql
   It places an array of pointers to the paramater names in names[] ( in order found )
   it places a pointer to a buffer it uses for the name space in *free_me.
   
   Both names[] and free_me must be freed by the caller BUT ONLY IF return is >0
   
*/
static int parse_sql_parameters(char *old_sql,char **new_sql,char **names[],char **free_me)
{
    int    n_params=count_sql_parameters(old_sql);
    char * my_copy  =NULL;
    char * sql      =NULL;
    char **my_names =NULL;
    char * out_p    =NULL;
    char * s        =NULL;
    
    int    name_index=0;
    int    quote_len;
    int    inlen = strlen(old_sql)+1;
    int    qm_index=0;
    /* width in chars of largest number + '\0' if all ? are treated as ?0, ?1, etc */
    int    numwidth;

    if(!n_params)                  // nothing to do
       return(0);

    if(n_params < 10)
        numwidth = 2;
    else if (n_params < 100)
        numwidth = 3;
    else
        numwidth = (int)(floor(log10((double)n_params))) + 2;

    /* copy the string, and make room at the end for printing extra numbers */
    REMALLOC(my_copy, inlen + n_params * numwidth +1);
    strcpy(my_copy, old_sql);      // extract the names in place and mangle the sql copy

    *free_me =my_copy;             // tell the caller what to free when they're done
    s        =my_copy;             // we're going to trash our copy with nulls
    
    REMALLOC(sql, strlen(old_sql)+1); // the new_sql cant be bigger than the old
    
    *new_sql=sql;                 // give the caller the new SQL
    out_p=sql;                    // init the sql output pointer
    
    REMALLOC(my_names, n_params*sizeof(char *));
   
    *names=my_names;
   
    while(*s)
    {
       switch(*s)
       {
          case '"' :
          case '\'':
             {
                char *t=s;
                s=skip_until_c(s+1,*s,&quote_len);
                memcpy(out_p,t,quote_len+2);     // the plus 2 is for the quote characters
                out_p+=quote_len+2;
             }
          case '\\': ++s; break;
          case '?' :
             {
                ++s;

                if(!(isalnum(*s) || *s=='_' || *s=='"' || *s=='\'')) // check for legal 1st char
                {
                    my_names[name_index++] = my_copy + inlen;
                    inlen += sprintf( my_copy + inlen, "%d", qm_index++) + 1;
                    *out_p='?';
                    ++out_p;
                    break;
                }

                if(*s=='"' || *s=='\'')          // handle ?"my var"
                {
                   int quote_type=*s;
                   my_names[name_index++]=++s;
                   s=skip_until_c(s,quote_type,&quote_len);
                   if(!*s)           // we hit a null without an ending "
                      goto error_return;
                   *s='\0';          // terminate this variable name
                   *out_p='?';
                   ++out_p;
                   ++s;
                   continue;
                }
                else
                {
                   my_names[name_index++]=s++;
                   while(*s && (isalnum(*s) || *s=='_'))
                      ++s;
                   *out_p='?';
                   ++out_p;
                    *out_p++=*s;
                   if(!*s)           //  terminated at the end of the sql we're done
                      return(n_params);
                   else
                   {
                      *out_p=*s;
                      *s='\0';
                   }
                   ++s;
                 continue;
                }
             } break;
       }
       
      *out_p++=*s;
      ++s;
    }
   *out_p='\0';
   return(n_params);

   error_return:
   if(my_names)
     free(my_names);
   if(my_copy)
     free(my_copy);
   if(sql)
     free(sql);
   return(-1);
}

/*
void check_parse(char *sql,char *new_sql,char **names,int n_names)
{
   int i;
   printf("IN :%s\nOUT:%s\n%d names\n",sql,new_sql,n_names);
   for(i=0;i<n_names;i++)
      printf("%5d %s\n",i,names[i]);
   printf("\n\n");
}
*/

/* ************************************************** 
   Sql.prototype.exec 
   ************************************************** */
duk_ret_t duk_rp_sql_exec(duk_context *ctx)
{
    TEXIS *tx;
    QUERY_STRUCT *q, q_st;
#ifdef USEHANDLECACHE
    DB_HANDLE *hcache = NULL;
#endif
    const char *db;
    char pbuf[msgbufsz];
    struct sigaction sa = { {0} };
    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    int nParams=0;
    char *newSql=NULL, **namedSqlParams=NULL, *freeme=NULL;

    //  signal(SIGUSR1, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database has been opened");

    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    q_st = duk_rp_get_query(ctx);
    q = &q_st;

    /* call parameters error, message is already pushed */
    if (q->err == QS_ERROR_PARAM)
    {
        goto end;
    }

    nParams = parse_sql_parameters((char*)q->sql, &newSql, &namedSqlParams, &freeme);
    //check_parse((char*)q->sql, newSql, namedSqlParams, nParams);
    //return 0;
    if (nParams > 0)
    {
        duk_push_string(ctx, newSql);
        duk_replace(ctx, q->str_idx);
        q->sql = duk_get_string(ctx, q->str_idx);
        free(newSql);
    }
    else
    {
        namedSqlParams=NULL;
        freeme=NULL;
    }

#ifdef USEHANDLECACHE
    hcache = get_handle(db, q->sql);
    if(!hcache)
        throw_tx_error(ctx,"sql open");
    tx = hcache->tx;
#else
    tx = TEXIS_OPEN((char *)db);
#endif
    if (!tx)
        throw_tx_error(ctx,"open sql");

    if (!TEXIS_PREP(tx, (char *)q->sql))
    {
        throw_tx_error(ctx,"sql prep");
    }
    /* sql parameters are the parameters corresponding to "?key" in a sql statement
       nd are provide by passing an object in JS call parameters */
    if( namedSqlParams)
    {
        duk_idx_t idx=-1;
        if(q->obj_idx != -1)
            idx=q->obj_idx;
        else if (q->arr_idx != -1)
            idx = q->arr_idx;
        else
            RP_THROW(ctx, "sql.exec - parameters specified in sql statement, but no corresponding object or array\n");

        if (!duk_rp_add_named_parameters(ctx, tx, idx, namedSqlParams, nParams))
        {
            free(namedSqlParams);
            free(freeme);
            throw_tx_error(ctx,"sql add parameters");
        }
        free(namedSqlParams);
        free(freeme);
    }
    /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters 
     TODO: check that this is indeed dead code given that parse_sql_parameters now
           turns "?, ?" into "?0, ?1"
     */
    else if (q->arr_idx != -1)
    {
        if (!duk_rp_add_parameters(ctx, tx, q->arr_idx))
            throw_tx_error(ctx,"sql add parameters");
    }
    else
    {
        texis_resetparams(tx);
    }

    if (!TEXIS_EXEC(tx))
        throw_tx_error(ctx,"sql exec");

    /* skip rows using texisapi */
    if (q->skip)
        TEXIS_SKIP(tx, q->skip);

    /* callback - return one row per callback */
    if (q->callback > -1)
    {
        int rows = duk_rp_fetchWCallback(ctx, tx, q);
        duk_push_int(ctx, rows);
#ifndef USEHANDLECACHE
        tx = TEXIS_CLOSE(tx);
#endif
        goto end; /* done with exec() */
    }

    /*  No callback, return all rows in array of objects */
    (void)duk_rp_fetch(ctx, tx, q);
#ifndef USEHANDLECACHE
    tx = TEXIS_CLOSE(tx);
#endif

end:
    duk_rp_log_tx_error(ctx,pbuf); /* log any non fatal errors to this.errMsg */
    return 1; /* returning outer array */
}

/* **************************************************
   Sql.prototype.eval 
   ************************************************** */
duk_ret_t duk_rp_sql_eval(duk_context *ctx)
{
    char *stmt = (char *)NULL;
    duk_idx_t str_idx = -1;
    duk_idx_t i = 0, top=duk_get_top(ctx);;

    /* find the argument that is a string */
    for (i = 0; i < top; i++)
    {
        if ( duk_is_string(ctx, i) )
        {
            stmt = (char *)duk_get_string(ctx, i);
            str_idx = i;
        }
        else if( duk_is_object(ctx, i) && !duk_is_array(ctx, i) )
        {
            /* remove returnType:'arrayh' as only one row will be returned */
            if(duk_get_prop_string(ctx, i, "returnType"))
            {
                if(! strcmp(duk_get_string(ctx, -1), "arrayh") )
                    duk_del_prop_string(ctx, i, "returnType");
            }
            duk_pop(ctx);
        }
    }

    if (str_idx == -1)
    {
        duk_rp_log_error(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_sprintf(ctx, "select %s;", stmt);
    duk_replace(ctx, str_idx);
    duk_rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "results");
    duk_get_prop_index(ctx, -1, 0);
    return (1);
}

duk_ret_t duk_rp_sql_one(duk_context *ctx)
{
    duk_idx_t str_idx = -1, i = 0, obj_idx = -1;

    for (i = 0; i < 2; i++)
    {
        if ( duk_is_string(ctx, i) )
            str_idx = i;
        else if( duk_is_object(ctx, i) && !duk_is_array(ctx, i) )
            obj_idx=i;
    }

    if (str_idx == -1)
    {
        duk_rp_log_error(ctx, "sql.one: No string (sql statement) provided");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_object(ctx);
    duk_push_number(ctx, 1.0);
    duk_put_prop_string(ctx, -2, "maxRows");
    
    if( obj_idx != -1)
        duk_pull(ctx, obj_idx);

    duk_rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "results");
    duk_get_prop_index(ctx, -1, 0);
    if(duk_is_undefined(ctx, -1))
        duk_push_object(ctx);
    return (1);
}


static void free_list(char **nl)
{
    int i=0;
    char *f;

    if(nl==NULL)
        return;

    f=nl[i];
    
    while(1)
    {
        if (*f=='\0')
        {
            free(f);
            break;
        }
        free(f);
        i++;
        f=nl[i];
    }    
    free(nl);
//    *needs_free=0;
}

void rp_set_tx_defaults(duk_context *ctx, TEXIS *tx, int rampartdef)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    DB_HANDLE *hcache = NULL;
    const char *db=NULL;
    if (tx==NULL)
    {
        duk_push_this(ctx);

        if (!duk_get_prop_string(ctx, -1, "db"))
            RP_THROW(ctx, "no database is open");
#ifdef USEHANDLECACHE
        hcache = get_handle(db, "settings");
        if(!hcache)
            throw_tx_error(ctx,"sql open");
        tx = hcache->tx;
#else
        tx = TEXIS_OPEN((char *)db);
#endif
    }

    if(!tx)
        throw_tx_error(ctx,"open sql");
    
    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
        throw_tx_error(ctx,"open sql");

    TXresetproperties(ddic);

#define duk_tx_set_prop(prop,val) \
if(setprop(ddic, prop, val )==-1)\
    throw_tx_error(ctx,"sql set");

    duk_tx_set_prop("querysettings","defaults");

    /* rampart defaults */
    if(rampartdef)
    {
        duk_tx_set_prop("strlsttovarcharmode","json");
        duk_tx_set_prop("varchartostrlstmode","json");
        TXunneededRexEscapeWarning = 0; //silence rex escape warnings
    }
    else
        TXunneededRexEscapeWarning = 1;
}

duk_ret_t duk_texis_set(duk_context *ctx)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    TEXIS *tx;
    const char *db,*val="";
    DB_HANDLE *hcache = NULL;
    char pbuf[msgbufsz];
    int added_ret_obj=0;
    char *rlsts[]={"noiseList","suffixList","suffixEquivsList","prefixList"};

    clearmsgbuf();

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

#ifdef USEHANDLECACHE
    hcache = get_handle(db, "settings");
    if(!hcache)
        throw_tx_error(ctx,"sql open");
    tx = hcache->tx;
#else
    tx = TEXIS_OPEN((char *)db);
#endif

    if(!tx)
        throw_tx_error(ctx,"open sql");

    duk_rp_log_error(ctx, "");

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
        throw_tx_error(ctx,"open sql");

    if(!duk_is_object(ctx, -1) || duk_is_array(ctx, -1) || duk_is_function(ctx, -1) )
        RP_THROW(ctx, "sql.set() - object with {prop:value} expected as parameter - got '%s'",duk_safe_to_string(ctx, -1));

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        int retlisttype=-1, setlisttype=-1, i=0;
        char propa[64], *prop=&propa[0];
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
            RP_THROW(ctx, "sql.set - '%s' - unknown/invalid property", dprop);

        strcpy(prop, dprop);

        for(i = 0; prop[i]; i++)
            prop[i] = tolower(prop[i]);

        /* a few aliases */
        if(!strcmp("listexp", prop) || !strcmp("listexpressions", prop))
            prop="lstexp";
        else if (!strcmp("listindextmp", prop) || !strcmp("listindextemp", prop) || !strcmp("lstindextemp", prop))
            prop="lstindextmp"; 
        else if (!strcmp("deleteindextmp", prop) || !strcmp("deleteindextemp", prop) || !strcmp("delindextemp", prop))
            prop="delindextmp"; 
        else if (!strcmp("addindextemp", prop))
            prop="addindextmp";
        else if (!strcmp("addexpressions", prop))
            prop="addexp";
        else if (!strcmp("delexpressions", prop) || !strcmp("deleteexpressions", prop))
            prop="delexp";
        else if (!strcmp("keepequivs", prop) || !strcmp("useequivs", prop))
            prop="useequiv";
        else if (!strcmp("equivsfile", prop))
            prop="eqprefix";
        else if (!strcmp("userequivsfile", prop))
            prop="ueqprefix";
        else if (!strcmp ("lstnoise", prop) || !strcmp ("listnoise",prop))
            retlisttype=0;
        else if (!strcmp ("lstsuffix", prop) || !strcmp ("listsuffix",prop))
            retlisttype=1;
        else if (!strcmp ("lstsuffixeqivs", prop) || !strcmp ("listsuffixequivs",prop))
            retlisttype=2;
        else if (!strcmp ("lstprefix", prop) || !strcmp ("listprefix",prop))
            retlisttype=3;
        else if (!strcmp ("noiselst", prop) || !strcmp ("noiselist",prop))
            setlisttype=0;
        else if (!strcmp ("suffixlst", prop) || !strcmp ("suffixlist",prop))
            setlisttype=1;
        else if (!strcmp ("suffixeqivslst", prop) || !strcmp ("suffixequivslist",prop))
            setlisttype=2;
        else if (!strcmp ("suffixeqlst", prop) || !strcmp ("suffixeqlist",prop))
            setlisttype=2;
        else if (!strcmp ("prefixlst", prop) || !strcmp ("prefixlist",prop))
            setlisttype=3;
        else if (!strcmp ("defaults", prop))
        {
            if(duk_is_boolean(ctx, -1))
            {
                if (duk_get_boolean(ctx, -1) )
                {
                    rp_set_tx_defaults(ctx, tx, 1);
                }
                goto propnext;
            }
            if(duk_is_string(ctx, -1))
                val=duk_get_string(ctx, -1);
            else
                goto default_err;
            if(!strcasecmp("texis", val))
            {
                rp_set_tx_defaults(ctx, tx, 0);
                goto propnext;
            }
            else if(!strcasecmp("rampart", val))
            {
                rp_set_tx_defaults(ctx, tx, 1);
                goto propnext;
            }

            default_err:
            RP_THROW(ctx, "sql.set() - property defaults option requires a string('texis' or 'rampart') or a boolean");
        }

        if(retlisttype>-1)
        {
            byte *nw;
            byte **lsts[]={globalcp->noise,globalcp->suffix,globalcp->suffixeq,globalcp->prefix};
            char *rprop=rlsts[retlisttype];
            byte **lst=lsts[retlisttype];
            
            i=0;
            /* skip if false */
            if(duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
                goto propnext;
            
            if(!added_ret_obj)
            {
                duk_push_object(ctx);
                duk_insert(ctx, 0);
                added_ret_obj=1;
            }

            duk_push_array(ctx);
            while ( (nw=lst[i]) && *nw != '\0' )
            {
                duk_push_string(ctx, (const char *) nw);
                duk_put_prop_index(ctx, -2, i++);
            }

            duk_put_prop_string(ctx, 0, rprop); 

            goto propnext;
        }

        if(setlisttype>-1)
        {
            char **nl=NULL; /* the list to be populated */
            /* set the new list up, then free and replace current list *
             * should be null or an array of strings ONLY              */
            if(duk_is_null(ctx, -1))
            {
                DUKREMALLOC(ctx, nl, sizeof(char*) * 1);
                nl[0]=strdup("");
            }
            else if(duk_is_array(ctx, -1))
            {
                int len=duk_get_length(ctx, -1);

                i=0;

                DUKREMALLOC(ctx, nl, sizeof(char*) * (len + 1));

                while (i<len)
                {
                    duk_get_prop_index(ctx, -1, i);
                    if(!(duk_is_string(ctx, -1)))
                    {
                        /* note that the RP_THROW below might be caught in js, so we need to clean up *
                         * terminate what we have so far, then free it                                */
                        nl[i]=strdup("");
                        free_list((char**)nl);
                        RP_THROW(ctx, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                    }
                    nl[i]=strdup(duk_get_string(ctx, -1));
                    duk_pop(ctx);
                    i++;
                }
                nl[i]=strdup("");
            }
            else
            {
                RP_THROW(ctx, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
            }
            switch(setlisttype)
            {
                case 0: free_list((char**)globalcp->noise);
                        globalcp->noise=(byte**)nl;
                        break;

                case 1: free_list((char**)globalcp->suffix);
                        globalcp->suffix=(byte**)nl;
                        break;

                case 2: free_list((char**)globalcp->suffixeq);
                        globalcp->suffixeq=(byte**)nl;
                        break;

                case 3: free_list((char**)globalcp->prefix);
                        globalcp->prefix=(byte**)nl;
                        break;
            }

            goto propnext;
        }

        /* addexp, delexp and addindextmp take one at a time, but may take multiple 
           so handle arrays here
        */
        if 
        (
            duk_is_array(ctx, -1) && 
            (
                !strcmp(prop,"addexp") |
                !strcmp(prop,"delexp") |
                !strcmp(prop,"delindextmp") |
                !strcmp(prop,"addindextmp")
            )
        )
        {
            int ptype=0;
            
            if(!strcmp(prop,"delexp"))
                ptype=1;
            else if (!strcmp(prop,"addindextmp"))
                ptype=2;
            else if (!strcmp(prop,"delindextmp"))
                ptype=3;
            
            duk_enum(ctx, -1, DUK_ENUM_ARRAY_INDICES_ONLY);
            while(duk_next(ctx, -1, 1))
            {
                const char *aval=NULL;
                
                if (ptype==1)
                {
                    if(duk_is_number(ctx, -1))
                    {
                        duk_to_string(ctx, -1);
                        aval=duk_get_string(ctx, -1);
                    }
                    else
                    {
                        aval=get_exp(ctx, -1);
                    
                        if(!aval)
                        {
                            RP_THROW
                            (
                                ctx, 
                                "sql.set: deleteExpressions - array must be an array of strings, expressions or numbers (expressions or expression index)\n"
                            );
                        }
                    }                
                }
                else if (ptype==3)
                {
                    if(duk_is_number(ctx, -1))
                    {
                        duk_to_string(ctx, -1);
                        aval=duk_get_string(ctx, -1);
                    }
                    else if (duk_is_string(ctx, -1))
                    {
                        aval=duk_get_string(ctx, -1);
                    }
                    else
                    {
                        RP_THROW
                        (
                            ctx, 
                            "sql.set: deleteIndexTemp - array must be an array of strings or numbers\n"
                        );
                    }                
                }
                else if (ptype==0)
                {
                    aval=get_exp(ctx, -1);
                    
                    if(!aval)
                    {
                        RP_THROW
                        (
                            ctx, 
                            "sql.set: addExpressions - array must be an array of strings or expressions\n"
                        );
                    }
                }
                else
                {
                    if(!duk_is_string(ctx, -1))
                    {
                        RP_THROW(ctx, "sql.set: addIndexTemp - array must be an array of strings\n");
                    }
                    aval=duk_get_string(ctx, -1);
                }
                if(setprop(ddic, (char*)prop, (char*)aval )==-1)
                {
                    throw_tx_error(ctx,"sql set");
                }
                duk_pop_2(ctx);
            }
            duk_pop(ctx);
        }
        else
        {
            if(duk_is_number(ctx, -1))
                duk_to_string(ctx, -1);
            if(duk_is_boolean(ctx, -1))
            {
                if(duk_get_boolean(ctx, -1))
                    val="1";
                else
                    val="0";
            }
            else
            {
                if(!(duk_is_string(ctx, -1)))
                {
                    RP_THROW(ctx,"invalid value '%s'", duk_safe_to_string(ctx, -1));
                }
                val=duk_get_string(ctx, -1);
            }

/*
            if(!strcmp(prop,"querydefaults") || !strcmp(prop,"querydefault"))
            {
                if(!duk_get_boolean_default(ctx, -1, 1))
                    goto propnext;
                prop="querysettings";
                val="defaults";
            }
*/
            if(setprop(ddic, (char*)prop, (char*)val )==-1)
            {
                throw_tx_error(ctx,"sql set");
            }
        }
        /* lstexp and lstindextmp are output via putmsg, capture output here */
        if( (!strcmp(prop, "lstexp")||!strcmp(prop, "lstindextmp")) 
                && strcmp(val,"off") )
        {
            if(!added_ret_obj)
            {
                duk_push_object(ctx);
                duk_insert(ctx, 0);
                added_ret_obj=1;
            }
            duk_push_array(ctx);
            msgtobuf(pbuf);
            if(strncmp("200  ",pbuf,5)==0)
            {
                char *s=pbuf+5, *end;
                duk_size_t slen=0;
                int arrayi=0;

                while (*s != '\0')
                {
                    while(isdigit(*s)||*s==':'||*s==' ')s++;
                    end=strchr(s,'\n');
                    if(end)
                        slen=(duk_size_t)(end-s);
                    else
                        break;
                    duk_push_lstring(ctx,s,slen);
                    duk_put_prop_index(ctx, -2, arrayi++);
                    s+=slen+1;
                }
            }
            duk_put_prop_string(ctx, 0, 
                ( 
                    strcmp(prop, "lstindextmp")?"expressionsList":"indexTempList"
                )
            );
        }

        propnext:
        duk_pop_2(ctx);
        /* capture and throw errors here */
        TXLOCK
        msgtobuf(pbuf);
        TXUNLOCK
        if(*pbuf!='\0')
            RP_THROW(ctx, "sql.set(): %s", pbuf+4);
    }
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    duk_rp_log_tx_error(ctx,pbuf); /* log any non fatal errors to this.errMsg */

    if(added_ret_obj)
    {
        duk_pull(ctx, 0);
        return 1;
    }
    return 0;
}


/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   There are x handle caches, one for each thread
   There is one handle cache for all new sql.exec() calls 
   in each thread regardless of how many dbs will be opened.

   Calling new Sql() only stores the name of the db path
   And there is only one database per new Sql("/db/path");

   Here we only check to see that the database exists and
   construct the js object.  Actual opening and caching
   of handles to the db is done in exec()
   ************************************************** */
duk_ret_t duk_rp_sql_constructor(duk_context *ctx)
{

    TEXIS *tx;
    const char *db = duk_get_string(ctx, 0);
    char pbuf[msgbufsz];

    /* allow call to Sql() with "new Sql()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    /* 
     if sql=new Sql("/db/path",true), we will 
     create the db if it does not exist 
  */
    if (duk_is_boolean(ctx, 1) && duk_get_boolean(ctx, 1) != 0)
    {
        /* check for db first */
        tx = TEXIS_OPEN((char *)db);
        if (tx == NULL)
        {
            /* don't log the error */
            fclose(mmsgfh);
            mmsgfh = fmemopen(NULL, 4096, "w+");
            if (!createdb(db))
            {
                duk_rp_log_tx_error(ctx,pbuf);
                RP_THROW(ctx, "cannot create database at '%s' (root path not found, lacking permission or other error\n%s)", db, pbuf);
            }
        }
        else
            tx = TEXIS_CLOSE(tx);
    }
    //  get_handle(db,"");
    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, "db");

    SET_THREAD_UNSAFE(ctx);

    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    /* settings object for defaults*/
    duk_push_object(ctx);
    /* vortex defaults */
    duk_push_string(ctx, "defaults");
    duk_put_prop_string(ctx, -2, "querysettings");
    /* default to json for strlst in and out */
    duk_push_string(ctx, "json");
    duk_put_prop_string(ctx, -2, "strlsttovarcharmode");
    duk_push_string(ctx, "json");
    duk_put_prop_string(ctx, -2, "varchartostrlstmode");
    (void)duk_texis_set(ctx);

    duk_rp_log_tx_error(ctx,pbuf); /* log any non fatal errors to this.errMsg */

    return 0;
}

/* **************************************************
   Initialize Sql module 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    /* Set up locks:
     * this will be run once per new duk_context/thread in server.c
     * but needs to be done only once for all threads
     */

    if (!db_is_init)
    {
        if (pthread_mutex_init(&lock, NULL) != 0)
        {
            printf("\n mutex init failed\n");
            exit(1);
        }
#ifdef PUTMSG_STDERR
        if (pthread_mutex_init(&printlock, NULL) != 0)
        {
            printf("\n mutex init failed\n");
            exit(1);
        }
#endif

        mmsgfh = fmemopen(NULL, 4096, "w+");
        db_is_init = 1;
    }

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_sql_constructor, 3 /*nargs*/);

    /* Push object that will be Sql.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Sql protoObj ] */

    /* Set Sql.prototype.exec. */
    duk_push_c_function(ctx, duk_rp_sql_exec, 4 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "exec");                    /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* set Sql.prototype.eval */
    duk_push_c_function(ctx, duk_rp_sql_eval, 4 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "eval");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.prototype.eval */
    duk_push_c_function(ctx, duk_rp_sql_one, 2 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "one");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.prototype.close */
    duk_push_c_function(ctx, duk_rp_sql_close, 0 /*nargs*/); /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_close ] */
    duk_put_prop_string(ctx, -2, "close");                   /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close} ] */

    /* set Sql.prototype.set */
    duk_push_c_function(ctx, duk_texis_set, 1 /*nargs*/);   /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_set ] */
    duk_put_prop_string(ctx, -2, "set");                    /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close,set:fn_set} ] */

    /* set Sql.prototype.importCsvFile */
    duk_push_c_function(ctx, duk_rp_sql_import_csv_file, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsvFile");

    /* set Sql.prototype.importCsv */
    duk_push_c_function(ctx, duk_rp_sql_import_csv_str, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsv");

    /* Set Sql.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, Sql-->[prototype-->{exe=fn_exe,...}] ] */
    duk_put_prop_string(ctx, -2, "init");      /* [ {init()} ] */

    duk_push_c_function(ctx, RPfunc_stringformat, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "stringFormat");

    duk_push_c_function(ctx, RPsqlFuncs_abstract, 4);
    duk_put_prop_string(ctx, -2, "abstract");

    duk_push_c_function(ctx, RPsqlFunc_sandr, 3);
    duk_put_prop_string(ctx, -2, "sandr");

    duk_push_c_function(ctx, RPsqlFunc_sandr2, 3);
    duk_put_prop_string(ctx, -2, "sandr2");

    /* rex|re2( 
          expression,                     //string or array of strings 
          searchItem,                     //string or buffer
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
            }
        );
   return value is an array of matches.
   If callback is specified, return value is number of matches.
  */
    duk_push_c_function(ctx, RPdbFunc_rex, 4);
    duk_put_prop_string(ctx, -2, "rex");

    duk_push_c_function(ctx, RPdbFunc_re2, 4);
    duk_put_prop_string(ctx, -2, "re2");

    /* rexfile|re2file( 
          expression,                     //string or array of strings 
          filename,                       //file with text to be searched
          callback,                       // optional callback function
          options  -
            {
              exclude:                    // string: "none"      - return all hits
                                          //         "overlap"   - remove the shorter hit if matches overlap
                                          //         "duplicate" - current default - remove smaller if one hit entirely encompasses another
              submatches:		  true|false - include submatches in an array.
                                          if have callback function (true is default)
                                            - true  --  function(
                                                          match,
                                                          submatchinfo={expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]},
                                                          matchindex
                                                        )
                                            - false --  function(match,matchindex)
                                          if no function (false is default)
                                            - true  --  ret= [{match:"match1",expressionIndex:matchedExpressionNo,submatches:["array","of","submatches"]},{...}...]
                                            - false --  ret= ["match1","match2"...]
              delimiter:		  expression to match -- delimiter for end of buffer.  Default is "$" (end of line).  If your pattern crosses lines, specify
                                                                 a delimiter which will not do so and you will be guaranteed to match even if a match crosses internal read buffer boundry
            }
        );
        
   return value is an array of matches.
   If callback is specified, return value is number of matches.
  */
    duk_push_c_function(ctx, RPdbFunc_rexfile, 4);
    duk_put_prop_string(ctx, -2, "rexFile");

    duk_push_c_function(ctx, RPdbFunc_re2file, 4);
    duk_put_prop_string(ctx, -2, "re2File");

    duk_push_c_function(ctx, searchfile, 3);
    duk_put_prop_string(ctx, -2, "searchFile");
    return 1;
}
