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
#include "cgi.h"
#include "../../rp.h"
#include "../core/duktape.h"

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
    TEXIS *rtx = texis_open((tdb), "PUBLIC", ""); \
    EXIT_IF_CANCELLED                             \
    rtx;                                          \
})

#define TEXIS_CLOSE(rtx) ({     \
    xprintf("Close\n");         \
    (rtx) = texis_close((rtx)); \
    EXIT_IF_CANCELLED           \
    rtx;                        \
})

#define TEXIS_PREP(a, b) ({           \
    xprintf("Prep\n");                \
    /*printf("texisprep %s\n",(b));*/ \
    int r = texis_prepare((a), (b));  \
    EXIT_IF_CANCELLED                 \
    r;                                \
})

#define TEXIS_EXEC(a) ({        \
    xprintf("Exec\n");          \
    int r = texis_execute((a)); \
    EXIT_IF_CANCELLED           \
    r;                          \
})

#define TEXIS_FETCH(a, b) ({           \
    xprintf("Fetch\n");                \
    FLDLST *r = texis_fetch((a), (b)); \
    EXIT_IF_CANCELLED                  \
    r;                                 \
})

#define TEXIS_SKIP(a, b) ({               \
    xprintf("skip\n");                    \
    int r = texis_flush_scroll((a), (b)); \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_PARAM(a, b, c, d, e, f) ({               \
    xprintf("Param\n");                                \
    int r = texis_param((a), (b), (c), (d), (e), (f)); \
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

/* for debugging */
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
    DB_HANDLE *hcache = NULL;

    SET_THREAD_UNSAFE(ctx);
    duk_push_heap_stash(ctx);
    if (duk_get_prop_string(ctx, -1, "hcache"))
    {
        hcache = (DB_HANDLE *)duk_get_pointer(ctx, -1);
        free_all_handles(NULL);
        duk_del_prop_string(ctx, -2, "hcache");
    }
    return 0;
}

#ifdef USEHANDLECACHE
#define throw_tx_error(ctx,pref) do{\
    char pbuf[2048];\
    duk_rp_log_tx_error(ctx, pbuf);\
    duk_push_sprintf(ctx, "%s: %s",pref,pbuf);\
    duk_throw(ctx);\
}while(0)
#else
#define throw_tx_error(ctx,pref) do{\
    char pbuf[2048];\
    duk_rp_log_tx_error(ctx, pbuf);\
    duk_push_sprintf(ctx, "%s: %s",pref,pbuf);\
    if(tx) tx = TEXIS_CLOSE(tx);\
    duk_throw(ctx);\
}while(0)
#endif



#define msgbufsz 2048
#define msgtobuf(buf)                \
    fseek(mmsgfh, 0, SEEK_SET);      \
    fread(buf, msgbufsz, 1, mmsgfh); \
    fseek(mmsgfh, 0, SEEK_SET);

/* **************************************************
     store an error string in this.lastErr 
   **************************************************   */
void duk_rp_log_error(duk_context *ctx, char *pbuf)
{
    duk_push_this(ctx);
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
    duk_put_prop_string(ctx, -2, "lastErr");
    duk_pop(ctx);
}

void duk_rp_log_tx_error(duk_context *ctx, char *buf)
{
    msgtobuf(buf);
    duk_rp_log_error(ctx, buf);
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
        int i = 0;

        duk_push_array(ctx);
        while (s < end)
        {
            duk_push_string(ctx, s);
            duk_put_prop_index(ctx, -2, i++);
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
        /* TODO: revisit how we want this to be presented to JS */
        /* create backing buffer and copy data into it */
        //p=(unsigned char *) duk_push_fixed_buffer(ctx, 8 /*size*/);
        //memcpy(p,fl->data[i],8);

        char s[32];
        ft_counter *acounter = fl->data[i];
        snprintf(s, 32, "%lx%lx", acounter->date, acounter->seq);
        duk_push_string(ctx, s);
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
   like duk_get_int_default but if string, converts 
   string to number with strtol 
   ************************************************** */
int duk_rp_get_int_default(duk_context *ctx, duk_idx_t i, int def)
{
    if (duk_is_number(ctx, i))
        return duk_get_int_default(ctx, i, def);
    if (duk_is_string(ctx, i))
    {
        char *end, *s = (char *)duk_get_string(ctx, i);
        int ret = (int)strtol(s, &end, 10);

        if (end == s)
            return (def);
        return (ret);
    }
    return (def);
}
/*
    CURRENTLY UNUSED and UNTESTED

* **************************************************
   like duk_require_int but if string, converts 
   string to number with strtol 
   ************************************************** *
int duk_rp_require_int(duk_context *ctx,duk_idx_t i) {
  if(duk_is_number(ctx,i))
    return duk_get_int(ctx,i);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(int)strtol(s, &end, 10);
          
    if (end!=s)
      return (ret);
  }

  //throw standard error
  return duk_require_int(ctx,i);
}

* **************************************************
   like duk_get_number_default but if string, converts 
   string to number with strtod 
   ************************************************** *
double duk_rp_get_number_default(duk_context *ctx,duk_idx_t i,double def) {
  if(duk_is_number(ctx,i))
    return duk_get_number_default(ctx,i,def);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(double)strtod(s, &end);
          
    if (end==s) return (def);
      return (ret);
  }
  return (def);
}

* **************************************************
   like duk_require_number but if string, converts 
   string to number with strtod 
   ************************************************** *
int duk_rp_require_number(duk_context *ctx,duk_idx_t i) {
  if(duk_is_number(ctx,i))
    return duk_get_number_default(ctx,i,def);
  if(duk_is_string(ctx,i))
  {
    char *end,*s=(char *)duk_get_string(ctx,i);
    int ret=(int)strtod(s, &end);
          
    if (end!=s)
      return (ret);
  }

  //throw standard error
  return duk_require_number(ctx,i);
}
*/

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
    q->retarray = 0;
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
                duk_rp_log_error(ctx, "Only one string may be passed as a parameter and must be a sql statement.");
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
                        q->retarray = 1;
                    }
                    else if (!strcasecmp("arrayh", rt))
                    {
                        q->retarray = 2;
                    }
                    else if (!strcasecmp("novars", rt))
                    {
                        q->retarray = 3;
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
        duk_rp_log_error(ctx, "No sql statement.");
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
        rc = TEXIS_PARAM(tx, arryn, v, &plen, in, out);
        if (!rc)
        {
            return (0);
        }
        duk_pop(ctx);
    }
    return (1);
}

/* **************************************************
   This is called when sql.exec() has no callback.
   fetch rows and push results to array 
   return number of rows 
   ************************************************** */
int duk_rp_fetch(duk_context *ctx, TEXIS *tx, QUERY_STRUCT *q)
{
    int rown = 0, i = 0,
        resmax = q->max,
        retarray = q->retarray;
    FLDLST *fl;
    /* create return array (outer array) */
    duk_push_array(ctx);

    /* array of arrays or novars requested */
    if (retarray)
    {
        /* push values into subarrays and add to outer array */
        while (rown < resmax && (fl = TEXIS_FETCH(tx, -1)))
        {
            /* novars, we need to get each row (for del and return count value) but not return any vars */
            if (retarray == 3)
            {
                rown++;
                continue;
            }
            /* we want first rowto be column names */
            if (retarray == 2)
            {
                /* an array of column names */
                duk_push_array(ctx);
                for (i = 0; i < fl->n; i++)
                {
                    duk_push_string(ctx, fl->name[i]);
                    duk_put_prop_index(ctx, -2, i);
                }
                duk_put_prop_index(ctx, -2, rown++);
                retarray = 1; /* do this only once */
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
        retarray = q->retarray,
        callback = q->callback;
    FLDLST *fl;

    while (rown < resmax && (fl = TEXIS_FETCH(tx, -1)))
    {
        duk_dup(ctx, callback);
        duk_push_this(ctx);

        /* array requested */
        switch (retarray)
        {
        /* novars */
        case 3:
        {
            duk_dup(ctx, callback);
            duk_push_this(ctx);
            duk_push_object(ctx);
            duk_push_int(ctx, rown++);
            duk_call_method(ctx, 2);
            break;
        }

        /* requesting first row to be column names
           so for the first row, we do two callbacks:
           one for headers, one for first row
        */
        case 2:
        {
            /* set up an extra call to callback */
            duk_dup(ctx, callback);
            duk_push_this(ctx);

            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_push_string(ctx, fl->name[i]);
                duk_put_prop_index(ctx, -2, i);
            }

            duk_push_int(ctx, -1);
            duk_call_method(ctx, 2); /* function(res,resnum){} */
            retarray = 1;            /* give names to callback only once */

            /* if function returns false, exit while loop, return number of rows so far */
            if (duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx, -1))
            {
                duk_pop(ctx);
                return (rown);
            }
            duk_pop(ctx);
            /* no break, fallthrough */
        }
        case 1:
        {
            duk_dup(ctx, callback);
            duk_push_this(ctx);
            duk_push_array(ctx);
            for (i = 0; i < fl->n; i++)
            {
                duk_rp_pushfield(ctx, fl, i);
                duk_put_prop_index(ctx, -2, i);
            }

            duk_push_int(ctx, rown++);
            duk_call_method(ctx, 2); /* function(res,resnum){} */
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
            duk_push_int(ctx, rown++);
            duk_call_method(ctx, 2); /* function(res,resnum){} */
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

    struct sigaction sa = {0};
    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    //  signal(SIGUSR1, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        duk_push_string(ctx, "no database is open");
        duk_throw(ctx);
    }
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
    tx = hcache->tx;
#else
    tx = TEXIS_OPEN((char *)db);
#endif
    if (!tx)
        throw_tx_error(ctx,"open sql");

    /* clear the sql.lastErr string */
    duk_rp_log_error(ctx, "");

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

    return 1; /* returning outer array */
}

/* **************************************************
   Sql.prototype.eval 
   ************************************************** */
duk_ret_t duk_rp_sql_eval(duk_context *ctx)
{
    char *stmt = (char *)NULL;
    char out[2048];
    size_t len;
    int arryi = -1, i = 0;

    /* find the argument that is a string */
    for (i = 0; i < 4; i++)
    {
        int vtype = duk_get_type(ctx, i);
        if (vtype == DUK_TYPE_STRING)
        {
            stmt = (char *)duk_get_string(ctx, i);
            arryi = i;
            len = strlen(stmt);
            break;
        }
    }

    if (arryi == -1)
    {
        duk_rp_log_error(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    if (len > 2036)
    {
        duk_rp_log_error(ctx, "Error: Eval: Eval string too long (>2036)");
        duk_push_int(ctx, -1);
        return (1);
    }

    sprintf(out, "select (%s);", stmt);
    duk_push_string(ctx, out);
    duk_replace(ctx, arryi);

    return (duk_rp_sql_exe(ctx));
}

duk_ret_t duk_texis_set(duk_context *ctx)
{
    LPSTMT lpstmt;
    DDIC *ddic;
    TEXIS *tx;
    const char *db,*val;
    DB_HANDLE *hcache = NULL;

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        duk_push_string(ctx, "no database is open");
        duk_throw(ctx);
    }
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
    duk_push_sprintf(ctx,"invalid option '%s'",(s));\
    duk_throw(ctx);\
} while(0)

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        if(!(duk_is_string(ctx,-2)))
            throwinvalidprop( duk_to_string(ctx,-2) );
        if(duk_is_number(ctx,-1))
            duk_to_string(ctx,-1);
        if(duk_is_boolean(ctx,-1))
        {
            if(duk_get_boolean(ctx,-1))
                val="on";
            else
                val="off";
        }
        else
        {
            if(!(duk_is_string(ctx,-1)))
                throwinvalidprop( duk_to_string(ctx,-1) );
            val=duk_get_string(ctx,-1);
        }

        if(setprop(ddic, (char*)duk_get_string(ctx,-2), (char*)val )==-1)
            throw_tx_error(ctx,"sql set");

        duk_pop_2(ctx);
    }
#ifndef USEHANDLECACHE
    tx=TEXIS_CLOSE(tx);
#endif
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
    char pbuf[2048];

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
            if (!createdb(db))
            {
                duk_rp_log_tx_error(ctx, pbuf);
                duk_push_sprintf(ctx, "cannot create database at '%s' (root path not found, lacking permission or other error\n)", db, pbuf);
                duk_throw(ctx);
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
    return 0;
}

/* if false, turn off.  Otherwise turn on */
duk_ret_t duk_rp_sql_singleuser(duk_context *ctx)
{
    if (duk_is_boolean(ctx, 0) && duk_get_boolean(ctx, 0) == 0)
        TXsingleuser = 0;
    else
        TXsingleuser = 1;

    return 0;
}

/* **************************************************
   Initialize Sql into global object. 
   ************************************************** */
duk_ret_t duk_open_module(duk_context *ctx)
{
    /* Set up locks:
     this will be run once per new duk_context/thread in server.c
     but needs to be done only once for all threads
  */

    if (!db_is_init)
    {
#ifndef NEVER_EVER_EVER
        if (pthread_mutex_init(&lock, NULL) != 0)
        {
            printf("\n mutex init failed\n");
            exit(1);
        }
#endif
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

    /* for single_user */
    duk_push_c_function(ctx, duk_rp_sql_singleuser, 1 /*nargs*/);
    duk_put_prop_string(ctx, -2, "single_user");

    duk_push_c_function(ctx, RPfunc_stringformat, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "stringformat");

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
    duk_put_prop_string(ctx, -2, "rexfile");

    duk_push_c_function(ctx, RPdbFunc_re2file, 4);
    duk_put_prop_string(ctx, -2, "re2file");
    return 1;
}