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
#include "../../rp.h"
#include "../core/duktape.h"
#include "api3.h"

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

#include "db_misc.c" /* copied and altered thunderstone code for stringformat and abstract */

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
    char pbuf[msgbufsz];\
    duk_rp_log_tx_error(ctx,pbuf);\
    RP_THROW(ctx, "%s error: %s",pref,pbuf);\
}while(0)
#else
#define throw_tx_error(ctx,pref) do{\
    char pbuf[msgbufsz];\
    if(tx) tx = TEXIS_CLOSE(tx);\
    duk_rp_log_tx_error(ctx,pbuf);\
    RP_THROW(ctx, "%s error: %s",pref,pbuf);\
}while(0)
#endif



#define msgtobuf(buf)  do {               \
    size_t sz;                            \
    fseek(mmsgfh, 0, SEEK_SET);           \
    sz=fread(buf, 1, msgbufsz-1, mmsgfh); \
    fclose(mmsgfh);                       \
    mmsgfh = fmemopen(NULL, 4096, "w+");  \
    buf[sz]='\0';                         \
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
#ifdef NEVER_EVER_EVER
        fprintf(stderr, "%s\n", pbuf);
#else
        pthread_mutex_lock(&printlock);
        fprintf(stderr, "%s\n", pbuf);
        pthread_mutex_unlock(&printlock);
#endif
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

static duk_ret_t counter_to_string(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "counterString");
    return(1);
}

static duk_ret_t counter_to_date(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "counterDate");
    return(1);
}

static duk_ret_t counter_to_sequence(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, "counterSequence");
    return(1);
}

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
        duk_push_int(ctx, (duk_int_t) * ((ft_integer *)fl->data[i]));
        break;
    }
    case FTN_LONG:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_long *)fl->data[i]));
        break;
    }
    case FTN_SMALLINT:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_smallint *)fl->data[i]));
        break;
    }
    case FTN_SHORT:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_short *)fl->data[i]));
        break;
    }
    case FTN_DWORD:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_dword *)fl->data[i]));
        break;
    }
    case FTN_WORD:
    {
        duk_push_int(ctx, (duk_int_t) * ((ft_word *)fl->data[i]));
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
        char s[32];
        void *v=NULL;
        ft_counter *acounter = fl->data[i];

        duk_push_object(ctx);
        snprintf(s, 32, "%lx%lx", acounter->date, acounter->seq);
        duk_push_string(ctx, s);
        duk_put_prop_string(ctx, -2, "counterString");
        
        (void)duk_get_global_string(ctx, "Date");
        duk_push_number(ctx, 1000.0 * (duk_double_t) acounter->date);
        duk_new(ctx, 1);
        duk_put_prop_string(ctx, -2, "counterDate");

        duk_push_number(ctx, (duk_double_t) acounter->seq);
        duk_put_prop_string(ctx, -2, "counterSequence");

        duk_push_c_function(ctx, counter_to_string, 0);
        duk_put_prop_string(ctx, -2, "toString");

        duk_push_c_function(ctx, counter_to_date, 0);
        duk_put_prop_string(ctx, -2, "toDate");

        duk_push_c_function(ctx, counter_to_sequence, 0);
        duk_put_prop_string(ctx, -2, "toSequence");

        v=duk_push_fixed_buffer(ctx, 8);
        memcpy(v, acounter, 8);
        duk_put_prop_string(ctx, -2, "counterValue");

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
    q->arryi = -1;
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
    int i = 0;
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

            if (duk_is_array(ctx, i) && q->arryi == -1)
                q->arryi = i;

            /* argument is a function, save where it is on the stack */
            else if (duk_is_function(ctx, i))
            {
                q->callback = i;
            }

            /* object of settings */
            else
            {

                if (duk_get_prop_string(ctx, i, "includeCounts"))
                    q->getCounts = duk_get_boolean_default(ctx, -1, 1);

                duk_pop(ctx);

                if (duk_get_prop_string(ctx, i, "skip"))
                    q->skip = duk_rp_get_int_default(ctx, -1, 0);

                duk_pop(ctx);

                if (duk_get_prop_string(ctx, i, "max"))
                {
                    q->max = duk_rp_get_int_default(ctx, -1, q->max);
                    if (q->max < 0)
                        q->max = INT_MAX;
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
                }

                duk_pop(ctx);
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

/* **************************************************
   Push parameters to the database for parameter 
   substitution (?) is sql. 
   i.e. "select * from tbname where col1 = ? and col2 = ?"
     an array of two values should be passed and will be
     processed here.
     arrayi is the place on the stack where the array lives.
   ************************************************** */

int duk_rp_add_parameters(duk_context *ctx, TEXIS *tx, int arryi)
{
    int rc, arryn = 0;

    /* Array is at arryi. Iterate over members.
       arryn is the index of the array we are examining */
    while (duk_has_prop_index(ctx, arryi, arryn))
    {
        void *v;   /* value to be passed to db */
        long plen; /* lenght of value */
        double d;  /* for numbers */
        int in, out;

        /* push array member to top of stack */
        duk_get_prop_index(ctx, arryi, arryn);

        /* check the datatype of the array member */
        switch (duk_get_type(ctx, -1))
        {
        case DUK_TYPE_NUMBER:
        {
            d = duk_get_number(ctx, -1);
            v = (double *)&d;
            plen = sizeof(double);
            in = SQL_C_DOUBLE;
            out = SQL_DOUBLE;
            break;
        }
        /* all objects are converted to json string
           this works (or will work) for several datatypes (varchar,int(x),strlst,json varchar) */
        case DUK_TYPE_OBJECT:
        {
            char *e;
            char *s = v = (char *)duk_json_encode(ctx, -1);
            plen = strlen(v);

            e = s + plen - 1;
            /* a date (and presumably other single values returned from an object which returns a string)
             will end up in quotes upon conversion, we need to remove them */
            if (*s == '"' && *e == '"' && plen > 1)
            {
                /* duk functions return const char* */
                v = s + 1;
                plen -= 2;
            }
            in = SQL_C_CHAR;
            out = SQL_VARCHAR;
            break;
        }
        /* insert binary data from a buffer */
        case DUK_TYPE_BUFFER:
        {
            duk_size_t sz;
            v = duk_get_buffer_data(ctx, -1, &sz);
            plen = (long)sz;
            in = SQL_C_BINARY;
            out = SQL_BINARY;
            break;
        }
        /* default for strings (not converted)and
           booleans, null and undefined (converted to 
           true/false, "null" and "undefined" respectively */
        default:
        {
            v = (char *)duk_to_string(ctx, -1);
            plen = strlen(v);
            in = SQL_C_CHAR;
            out = SQL_VARCHAR;
        }
        }
        arryn++;
        duk_pop(ctx);
        rc = TEXIS_PARAM(tx, arryn, v, &plen, in, out);
        if (!rc)
        {
            return (0);
        }
    }
    return (1);
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
    duk_idx_t callback_idx = q->callback, colnames_idx=0;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->getCounts)
        texis_getCountInfo(tx,&cinfo);

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
            /* novars */
            case 2:
            {
                duk_push_object(ctx);       /*empty object */
                duk_push_int(ctx, rown++ ); /* index */
                duk_dup(ctx, colnames_idx);

                if(q->getCounts)
                {
                    pushcounts;             /* countInfo */
                    duk_call_method(ctx, 4);/* function({_empty_},resnum,info){} */
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
                    pushcounts;
                    duk_call_method(ctx, 4);/* function({_empty_},resnum,info){} */
                }
                else
                    duk_call_method(ctx, 3);

                break;
            }
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
                    pushcounts;
                    duk_call_method(ctx, 4);/* function({_empty_},resnum,info){} */
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
   Sql.prototype.exec 
   ************************************************** */
duk_ret_t duk_rp_sql_exe(duk_context *ctx)
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


    //DDIC *ddic=texis_getddic(tx);
    //setprop(ddic, "likeprows", "2000");

    if (!TEXIS_PREP(tx, (char *)q->sql))
        throw_tx_error(ctx,"sql prep");

    /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters */

    /* add sql parameters */
    if (q->arryi != -1)
    {
        if (!duk_rp_add_parameters(ctx, tx, q->arryi))
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
    int arryi = -1;
    duk_idx_t i = 0, top=duk_get_top(ctx);;

    /* find the argument that is a string */
    for (i = 0; i < top; i++)
    {
        if ( duk_is_string(ctx, i) )
        {
            stmt = (char *)duk_get_string(ctx, i);
            arryi = i;
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

    if (arryi == -1)
    {
        duk_rp_log_error(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_sprintf(ctx, "select %s;", stmt);
    duk_replace(ctx, arryi);
    duk_rp_sql_exe(ctx);
    duk_get_prop_string(ctx, -1, "results");
    duk_get_prop_index(ctx, -1, 0);
    return (1);
}


static CONST char * null_noiselist[]={""};

int noiselist_needs_free=0;

static void free_noiselist(char **nl)
{
    int i=0;
    char *f;
    if(noiselist_needs_free==0)
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
    noiselist_needs_free=0;
}

duk_ret_t duk_texis_set(duk_context *ctx)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    TEXIS *tx;
    const char *db,*val;
    DB_HANDLE *hcache = NULL;
    char pbuf[msgbufsz];

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

#ifdef USEHANDLECACHE
    hcache = get_handle(db, "settings");
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

#define throwinvalidprop(s) do{\
    RP_THROW(ctx,"invalid option '%s'",(s));\
} while(0)

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        if(!(duk_is_string(ctx, -2)))
            throwinvalidprop( duk_to_string(ctx, -2) );
        if(!strcmp ("noiselist", duk_get_string(ctx, -2) ) )
        {
            if(duk_is_null(ctx, -1))
                globalcp->noise=(byte**)null_noiselist;
            else if(duk_is_array(ctx, -1))
            {
                int i=0, len=duk_get_length(ctx, -1);

                char **nl=NULL;
                
                if(noiselist_needs_free)
                    free_noiselist((char**)globalcp->noise);

                DUKREMALLOC(ctx, nl, sizeof(char*) * (len + 1));

                while (i<len)
                {
                    duk_get_prop_index(ctx, -1, i);
                    if(!(duk_is_string(ctx, -1)))
                        RP_THROW(ctx, "sql.set: noiselist members must be strings");
                    nl[i]=strdup(duk_get_string(ctx, -1));
                    duk_pop(ctx);
                    i++;
                }
                nl[i]=strdup("");
                globalcp->noise=(byte**)nl;
                noiselist_needs_free=1;
            }
            else
                RP_THROW(ctx, "sql.set: noiselist must be an array of strings");
            goto propnext;
        }
        if(duk_is_number(ctx, -1))
            duk_to_string(ctx, -1);
        if(duk_is_boolean(ctx, -1))
        {
            if(duk_get_boolean(ctx, -1))
                val="on";
            else
                val="off";
        }
        else
        {
            if(!(duk_is_string(ctx, -1)))
                throwinvalidprop( duk_to_string(ctx, -1) );
            val=duk_get_string(ctx, -1);
        }

        if(setprop(ddic, (char*)duk_get_string(ctx, -2), (char*)val )==-1)
            throw_tx_error(ctx,"sql set");

        propnext:
        duk_pop_2(ctx);
    }
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
    duk_rp_log_tx_error(ctx,pbuf); /* log any non fatal errors to this.errMsg */
    return 0;
}


/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   There are x handle caches, one for each thread
   There is one handle cache for all new Sql() calls 
   in each thread regardless of how many dbs will be opened.

   Calling new Sql() only stores the name of the db path
   And there is one database per new Sql("/db/path");

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
                RP_THROW(ctx, "cannot create database at '%s' (root path not found, lacking permission or other error\n)", db, pbuf);
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
    {
        extern int TXunneededRexEscapeWarning;
        TXunneededRexEscapeWarning = 0; //silence rex escape warnings
    }

    /* default to json for strlst in and out */
    duk_push_object(ctx);
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
     this will be run once per new duk_context/thread in server.c
     but needs to be done only once for all threads
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
    duk_push_c_function(ctx, duk_rp_sql_exe, 4 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "exec");                    /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* set Sql.prototype.eval */
    duk_push_c_function(ctx, duk_rp_sql_eval, 4 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "eval");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.prototype.close */
    duk_push_c_function(ctx, duk_rp_sql_close, 0 /*nargs*/); /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_close ] */
    duk_put_prop_string(ctx, -2, "close");                   /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close} ] */

    /* set Sql.prototype.set */
    duk_push_c_function(ctx, duk_texis_set, 1 /*nargs*/);   /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_set ] */
    duk_put_prop_string(ctx, -2, "set");                    /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close,set:fn_set} ] */

    /* Set Sql.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, Sql-->[prototype-->{exe=fn_exe,...}] ] */
    duk_put_prop_string(ctx, -2, "init");      /* [ {init()} ] */

    duk_push_c_function(ctx, RPfunc_stringformat, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "stringFormat");

    duk_push_c_function(ctx, RPsqlFuncs_abstract, 2);
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
