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
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
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

#define RESMAX_DEFAULT 10 /* default number of sql rows returned for select statements if max is not set */
//#define PUTMSG_STDERR                  /* print texis error messages to stderr */

#define QUERY_STRUCT struct rp_query_struct

#define QS_ERROR_DB 1
#define QS_ERROR_PARAM 2
#define QS_SUCCESS 0

QUERY_STRUCT
{
    const char *sql;    /* the sql statement (allocated by duk and on its stack) */
    duk_idx_t arr_idx;  /* location of array of parameters in ctx, or -1 */
    duk_idx_t obj_idx;
    duk_idx_t str_idx;
    duk_idx_t arg_idx;  /* location of extra argument for callback */ 
    duk_idx_t callback; /* location of callback in ctx, or -1 */
    int skip;           /* number of results to skip */
    int max;            /* maximum number of results to return */
    signed char rettype;/* 0 for return object with key as column names, 
                           1 for array
                           2 for novars                                           */
    char err;
    char getCounts;     /* whether to include metamorph counts in return */
};


duk_ret_t duk_rp_sql_close(duk_context *ctx);

extern int TXunneededRexEscapeWarning;
int texis_resetparams(TEXIS *tx);
int texis_cancel(TEXIS *tx);
/*
   info for shared memory
   which is used BOTH for thread and fork versions
   in order to keep it simple
*/
#define FORKMAPSIZE 1048576
//#define FORKMAPSIZE 2
#define FMINFO struct sql_map_info_s
FMINFO
{
    void *mem;
    void *pos;
};

#define mmap_used ((size_t)(mapinfo->pos - mapinfo->mem))
#define mmap_rem (FORKMAPSIZE - mmap_used)
#define mmap_reset do{ mapinfo->pos = mapinfo->mem;} while (0)

int thisfork =0; //set after forking.

/* shared mem for logging errors */
char **errmap=NULL;

/* one SFI struct for each thread in rampart-server.so.
   Thread 0 in server can run without a fork
   while the rest will fork in order to avoid locking
   around texis_* calls.

   If not in the server, or if not threading, then 
   thread 0 will of course be non forking.
   
   A note on thread/fork SFI numbering:

       In the server, there is the main thread, with 
       threads 0..n serveing http requests. The main thread
       will never call texis functions after the server is 
       started.

       Only one texis call can be happening concurrently
       in a process, so "main thread" or "thread 0" can use it
       in the current process, while threads >0 need to
       fork in order to avoid locking slowdown.

       Thus, SFI[0] can serve both the main thread
       and thread 0.  Or in the case of no server, 
       SFI[0] is exclusively used. But since SFI
       contains information about forking, in reality
       SFI[0] is not used at all.

       This does create some numbering confusion with the
       first usable/used SFI being SFI[1].  But it's better
       than having to do SFI[threadno-1], which would be
       really confusing, messy and error prone.

       It also allows for an easy test to see if we are forking:
       DB_HANDLE *h = ...;
       if(h->forkno) ...;
*/

#define SFI struct sql_fork_info_s
SFI
{
    int reader;         // pipe to read from in parent or child
    int writer;         // pipe to write to in parent or child
    pid_t childpid;     // process id of the child if in parent
    FMINFO *mapinfo;    // the shared mmap for reading and writing in both parent and child
    void *aux;          // if data is larger than mapsize, we need to copy it in chunks into here
    void *auxpos;
    size_t auxsz;
    FLDLST *fl;         // modified FLDLST for parent pointing to areas in mapinfo
};
SFI **sqlforkinfo = NULL;
static int n_sfi=0;


#define DB_HANDLE struct db_handle_s_list
DB_HANDLE
{
    TEXIS *tx;          // a texis handle, NULL if forkno > 0
    int idx;            // the index of this handle in all_handles[]
    int fork_idx;       // the index of this handle in the forks all_handles[]
    uint16_t forkno;    // corresponds to the threadno from rampart-server. One fork per thread (except thread 0)
    char inuse;         // whether this handle is being used by a transaction
    char *db;           // the db name.  Handles must be closed and reopened if used on a different db.
};

#define NDB_HANDLES 32

DB_HANDLE *all_handles[NDB_HANDLES] = {0};


#ifdef __APPLE__
#include <Availability.h>
#  if __MAC_OS_X_VERSION_MIN_REQUIRED < 101300
#    include "fmemopen.h"
#    include "fmemopen.c"
#  endif
#endif

extern int RP_TX_isforked;

static int sql_set(duk_context *ctx, DB_HANDLE *hcache, char *errbuf);

//#define TXLOCK if(!RP_TX_isforked) {printf("lock from %d\n", thisfork);pthread_mutex_lock(&lock);}
//#define TXLOCK if(!RP_TX_isforked) pthread_mutex_lock(&lock);
//#define TXUNLOCK if(!RP_TX_isforked) pthread_mutex_unlock(&lock);

#define TXLOCK
#define TXUNLOCK

#define HLOCK if(!RP_TX_isforked) pthread_mutex_lock(&lock);
//#define HLOCK if(!RP_TX_isforked) {printf("lock from %d\n", thisfork);pthread_mutex_lock(&lock);}
#define HUNLOCK if(!RP_TX_isforked) pthread_mutex_unlock(&lock);

int db_is_init = 0;
int tx_rp_cancelled = 0;
/*
#define EXIT_IF_CANCELLED \
    if (tx_rp_cancelled)  \
        exit(0);
*/

#define EXIT_IF_CANCELLED

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
    TEXIS *rtx = texis_open((char *)(tdb), "PUBLIC", ""); \
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

#define TEXIS_GETCOUNTINFO(a, b) ({       \
    xprintf("getCountInfo\n");            \
    TXLOCK                                \
    int r = texis_getCountInfo((a), (b)); \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_FLUSH(a) ({                 \
    xprintf("skip\n");                    \
    TXLOCK                                \
    int r = texis_flush((a));             \
    TXUNLOCK                              \
    EXIT_IF_CANCELLED                     \
    r;                                    \
})

#define TEXIS_RESETPARAMS(a) ({           \
    xprintf("resetparams\n");             \
    TXLOCK                                \
    int r = texis_resetparams((a));       \
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

void die_nicely(int sig)
{
    DB_HANDLE *h;
    int i=0;
    for (; i<NDB_HANDLES; i++)
    {
        h= all_handles[i];
        if(h && h->inuse)
        {
            texis_cancel(h->tx);
        }
    }
    tx_rp_cancelled = 1;
}


pid_t g_hcache_pid = 0;
pid_t parent_pid = 0;

#define msgbufsz 4096

#define throw_tx_error(ctx,h,pref) do{\
    char pbuf[msgbufsz] = {0};\
    duk_rp_log_tx_error(ctx,h,pbuf);\
    RP_THROW(ctx, "%s error: %s",pref,pbuf);\
}while(0)

#define clearmsgbuf() do {                \
    fseek(mmsgfh, 0, SEEK_SET);           \
    fwrite("\0", 1, 1, mmsgfh);           \
    fseek(mmsgfh, 0, SEEK_SET);           \
} while(0)

#define msgtobuf(buf)  do {                          \
    int pos = ftell(mmsgfh);                         \
    if(pos && errmap[thisfork][pos-1]=='\n') pos--;  \
    errmap[thisfork][pos]='\0';                      \
    strcpy((buf), errmap[thisfork]);                 \
    clearmsgbuf();                                   \
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
//        pthread_mutex_lock(&printlock);
        fprintf(stderr, "%s\n", pbuf);
//        pthread_mutex_unlock(&printlock);
    }
#endif
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
}

void duk_rp_log_tx_error(duk_context *ctx, DB_HANDLE *h, char *buf)
{
    msgtobuf(buf);
    duk_rp_log_error(ctx, buf);
    //duk_rp_log_error(ctx, errmap[thisfork]);
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
/* if in a child process, bail on error.  A new child will be forked
   and hopefully everything will get back on track                    */
#define forkwrite(b,c) ({\
    int r=0;\
    r=write(finfo->writer, (b),(c));\
    if(r==-1) {\
        fprintf(stderr, "fork write failed: '%s' at %d\n",strerror(errno),__LINE__);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    r;\
})

#define forkread(b,c) ({\
    int r=0;\
    r= read(finfo->reader,(b),(c));\
    if(r==-1) {\
        fprintf(stderr, "fork read failed: '%s' at %d\n",strerror(errno),__LINE__);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    r;\
})
/*
static size_t mmwrite(FMINFO *mapinfo, void *data, size_t size)
{
    if(size < mmap_rem)
    {
        memcpy(mapinfo->pos, data, size);
        mapinfo->pos += size;
        return size;
    }
    return 0;
}

static size_t mmread(FMINFO *mapinfo, void *data, size_t size)
{
    if(size < mmap_rem)
    {
        memcpy(data, mapinfo->pos, size);
        mapinfo->pos += size;
        return size;
    }
    return 0;
}
*/
static void do_fork_loop(SFI *finfo);

#define Create 1
#define NoCreate 0

static SFI *check_fork(DB_HANDLE *h, int create)
{
    int pidstatus;
    SFI *finfo = sqlforkinfo[h->forkno];

    if(finfo == NULL)
    {
        fprintf(stderr, "previously opened pipe info no longer exists?  This should not be possible.\n");
        exit(1);
    }

    parent_pid=getpid();

    /* waitpid: like kill(pid,0) except only works on child processes */
    if (!finfo->childpid || waitpid(finfo->childpid, &pidstatus, WNOHANG)) 
    {
        if (!create)
            return NULL;

        int child2par[2], par2child[2];
        //signal(SIGPIPE, SIG_IGN); //macos
        //signal(SIGCHLD, SIG_IGN);
        /* our creation run.  create pipes and setup for fork */
        if (pipe(child2par) == -1)
        {
            fprintf(stderr, "child2par pipe failed\n");
            return NULL;
        }

        if (pipe(par2child) == -1)
        {
            fprintf(stderr, "par2child pipe failed\n");
            return NULL;
        }

        /* if child died, close old pipes */
        if (finfo->writer > 0)
        {
            close(finfo->writer);
            finfo->writer = -1;
        }
        if (finfo->reader > 0)
        {
            close(finfo->reader);
            finfo->reader = -1;
        }


        /***** fork ******/
        finfo->childpid = fork();
        if (finfo->childpid < 0)
        {
            fprintf(stderr, "fork failed");
            finfo->childpid = 0;
            return NULL;
        }

        if(finfo->childpid == 0)
        { /* child is forked once then talks over pipes. */
            struct sigaction sa = { {0} };
            RP_TX_isforked=1; /* mutex locking not necessary in fork */
            memset(&sa, 0, sizeof(struct sigaction));
            sa.sa_flags = 0;
            sa.sa_handler =  die_nicely;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGUSR1, &sa, NULL);

            close(child2par[0]);
            close(par2child[1]);
            finfo->writer = child2par[1];
            finfo->reader = par2child[0];
            thisfork=h->forkno;
            mmsgfh = fmemopen(errmap[h->forkno], msgbufsz, "w+");
            fcntl(finfo->reader, F_SETFL, 0);
            do_fork_loop(finfo); // loop and never come back;
        }
        else
        {
            //parent
            signal(SIGPIPE, SIG_IGN); //macos
            signal(SIGCHLD, SIG_IGN);
            close(child2par[1]);
            close(par2child[0]);
            finfo->reader = child2par[0];
            finfo->writer = par2child[1];
            fcntl(finfo->reader, F_SETFL, 0);
        }
    }

    return finfo;
}


static void free_all_handles(void *unused)
{
    DB_HANDLE *h;
    int i=0;
    for (; i<NDB_HANDLES; i++)
    {
        tx_rp_cancelled = 1;
        h= all_handles[i];
        if(h)
        {
            free(h->db);
            if(h->inuse && h->forkno)
                texis_cancel(h->tx);

            TEXIS_CLOSE(h->tx);
            free(h);
            all_handles[i]=NULL;
        }
    }
}


static void free_all_handles_noClose(void *unused)
{
    DB_HANDLE *h;
    int i=0;
    for (; i<NDB_HANDLES; i++)
    {
        h = all_handles[i];
        if(h)
        {
            free(h->db);
            free(h);
            all_handles[i]=NULL;
        }
    }
}

static DB_HANDLE *make_handle(int i, const char *db)
{
    DB_HANDLE *h = NULL;
    REMALLOC(h, sizeof(DB_HANDLE));
    h->idx = i;
    h->forkno = 0;
    h->inuse=0;
    h->db=strdup(db);
    h->tx=NULL;

    return h;
}

static int fork_open(DB_HANDLE *h);

#define fromContext -1

/* find first unused handle, create as necessary */
static DB_HANDLE *h_open(const char *db, int forkno, duk_context *ctx)
{
    DB_HANDLE *h = NULL;
    int i=0;

    if(forkno == fromContext)
    {
        duk_get_global_string(ctx, "rampart");
        duk_get_prop_string(ctx, -1, "thread_id");
        forkno = duk_get_int_default(ctx, -1, 0);
        duk_pop_2(ctx);
    }
//printf("forkno = %d\n", thisfork);
    /* not necessary except for testing */
    if (totnthreads<=forkno)
        totnthreads=forkno+1;

    if (g_hcache_pid != getpid())
    {
        free_all_handles_noClose(NULL);
    }

    for (; i<NDB_HANDLES; i++)
    {
        h = all_handles[i];
        if(!h)
        {
            g_hcache_pid = getpid();
            h=make_handle(i,db);
            all_handles[i]=h;
            break;
        }
        else
        {
            if(!h->inuse && !strcmp(db, h->db) && forkno == h->forkno)
            {
                break;
            }
        }
        h=NULL; // no suitable candidate found.
    }
    /* no handle open with same db, need to close an unused one and reopen*/
    if(!h)
    {
        HLOCK
        for (i=0; i<NDB_HANDLES; i++)
        {
            h = all_handles[i];
            if(!h->inuse)
            {
                if(h->tx)
                {
                    texis_close(h->tx);
                }
                free(h);
                h = all_handles[i] = make_handle(i, db);
                h->inuse=1;
                break;
            }
            h = NULL;
        }
        HUNLOCK
    }
    if(h)
    {
        h->inuse=1;
        if(forkno > 0)
        {
            h->forkno = forkno;
            h->fork_idx = fork_open(h);
        }            
        else if (h->tx == NULL)
            h->tx = texis_open((char *)(db), "PUBLIC", "");;
    }
    return h;
}

static inline void release_handle(DB_HANDLE *h)
{
    h->inuse=0;
}

/*********** CHILD/PARENT FUNCTION PAIRS ************/

static int get_chunks(SFI *finfo, int size)
{
    int pos=0;
    size *= -1;

    if(finfo->auxsz < FORKMAPSIZE * 2)
    {
        finfo->auxsz = FORKMAPSIZE * 2;
        REMALLOC(finfo->aux, finfo->auxsz);
    }

    while(1)
    {
        finfo->auxpos = finfo->aux + pos;
        memcpy(finfo->auxpos, finfo->mapinfo->mem, size);
        pos += size; // for next round
        if(forkwrite("C",sizeof(char))==-1) //ask for more
            return 0;
        if(forkread(&size,sizeof(int))==-1) //get the next chunk size
            return 0;
        if(size > -1) //we are done, get remaining data
        {
            if(size + pos > finfo->auxsz)
            {
                finfo->auxsz += size;
                REMALLOC(finfo->aux, finfo->auxsz);
            }
            finfo->auxpos = finfo->aux + pos;
            memcpy(finfo->auxpos, finfo->mapinfo->mem, size);

            return (int)size; //size of final chunk
        }
        size *=-1;
        if (size + pos > finfo->auxsz)
        {
            finfo->auxsz *=2;
            REMALLOC(finfo->aux, finfo->auxsz);
        }
    }
    return 0; // no nag
}

static FLDLST * fork_fetch(DB_HANDLE *h,  int stringsFrom)
{
    SFI *finfo = check_fork(h, NoCreate);
    FLDLST *ret=NULL;
    int i=0, retsize=0;
    int *ilst=NULL;
    FMINFO *mapinfo;
    size_t eos=0;
    void *buf;

    mapinfo = finfo->mapinfo;
    buf=mapinfo->mem;

    if(!finfo)
        return NULL;

    if(forkwrite("f", sizeof(char)) == -1)
        return NULL;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return NULL;
    
    if(forkwrite(&(stringsFrom), sizeof(int)) == -1)
        return NULL;

    if(forkread(&retsize, sizeof(int)) == -1)
        return NULL;

    if(retsize==-1)
    {
        // -1 means error or no more rows
        if(finfo->aux)
        {
            free(finfo->aux);
            finfo->aux=NULL;
            finfo->auxsz=0;
            finfo->auxpos=NULL;
        }
        return NULL;
    }
    else if (retsize<-1)
    {
        //not enough space in memmap for entire response
        retsize=get_chunks(finfo,retsize);
        buf = finfo->aux;
    }

    /* unserialize results and make a new fieldlist */
    if (finfo->fl == NULL)
    {
        REMALLOC(finfo->fl, sizeof(FLDLST));
        finfo->fl->n=0;
        memset(finfo->fl, 0, sizeof(FLDLST));        
    }
    ret = finfo->fl;

    /* first int is fl->n */
    ilst = buf;
    ret->n = ilst[0];
    eos += sizeof(int);

    /* types is an array of ints at beginning */
    ilst=(buf) + eos;
    for (i=0;i<ret->n;i++)
        ret->type[i]=ilst[i];
    eos += sizeof(int) * ret->n;

    /* ndata is an array of ints following type ints */
    ilst=(buf) + eos;
    for (i=0;i<ret->n;i++)
        ret->ndata[i] = ilst[i];
    eos += sizeof(int) * ret->n;

    /* next an array of null terminated strings for names */
    for (i=0;i<ret->n;i++)
    {
        ret->name[i]= (buf) + eos;
        eos += strlen(ret->name[i]) + 1;
    }

    /* last is the data itself.  Each field is not necessarily NULL terminated
       and strings may be shorter than field width */
    for (i=0;i<ret->n;i++)
    {
        char type = ret->type[i] & 0x3f;
        size_t size = ddftsize(type) * (size_t)ret->ndata[i];
        ret->data[i]= (buf) + eos;
        eos += size;
    }
    return ret;    
}

static int cwrite(SFI *finfo, void *data, size_t sz)
{
    FMINFO *mapinfo = finfo->mapinfo;
    size_t rem = mmap_rem;
    char c;
    int used = -1 * FORKMAPSIZE;

    while (rem < sz)
    {
        memcpy(mapinfo->pos, data, rem);
        /* send negative to signal chunk */
        if( forkwrite(&used,sizeof(int)) == -1)
            return 0;
        /* wait for parent to be ready for next */
        if( forkread(&c,sizeof(char)) == -1)
            return 0;
        /* reset to beginning of map mem */
        mapinfo->pos=mapinfo->mem;
        data += rem;
        sz -= rem;
        rem = FORKMAPSIZE;
    }

    memcpy(mapinfo->pos, data, sz);
    mapinfo->pos += sz;

    return 1;
}

static int serialize_fl(SFI *finfo, FLDLST *fl)
{
    FMINFO *mapinfo = finfo->mapinfo;
    int i=0;
    mmap_reset;

    /* single int = fl->n */
    if(!cwrite(finfo, &(fl->n), sizeof(int)))
        return -1;

    /* array of ints for type*/    
    for (i = 0; i < fl->n; i++)
    {
        int type =  fl->type[i];
        if(!cwrite(finfo, &type, sizeof(int)))
            return -1;
    }

    /* array of ints for ndata */
    for (i = 0; i < fl->n; i++)
    {
        int ndata =  fl->ndata[i];
        if(!cwrite(finfo, &ndata, sizeof(int)))
            return -1;
    }

    /* array of names -> [col_name1, \0, col_name2, \0 ...] */
    for (i = 0; i < fl->n; i++)
    {
        char *name =  fl->name[i];
        size_t l = strlen(name)+1; //include the \0
        if(!cwrite(finfo, name, l))
            return -1;
    }
    /* data in seq - length of each determined by sizeof(type) * ndata */
    for (i = 0; i < fl->n; i++)
    {
        char type = fl->type[i] & 0x3f;
        size_t size = ddftsize(type) * (size_t)fl->ndata[i];
        if(!cwrite(finfo, fl->data[i], size))
            return -1;
    }

    return (int) mmap_used;
}


static int child_fetch(SFI *finfo, int *idx)
{
    int stringFrom=-9999;
    DB_HANDLE *h;
    int ret=-1;
    FLDLST *fl;

    /* idx */
    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(size_t));
        return 0;
    }
    if(*idx != -1)
    {
        h=all_handles[*idx];
    }
    else
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    /* stringFrom */
    if (forkread(&stringFrom, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(stringFrom == -9999)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    fl = texis_fetch(h->tx, stringFrom);

    if(fl)
        ret = serialize_fl(finfo,fl);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    if(ret<0)
        return 0;

    return ret;
}

static int fork_param(
    DB_HANDLE *h, 
    int ipar, 
    void *buf,
    long *len,
    int ctype,
    int sqltype
)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;
    FMINFO *mapinfo;

    if(!finfo)
        return 0;

    mapinfo = finfo->mapinfo;
    mmap_reset;
    if(forkwrite("P", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;

    if(!cwrite(finfo, &ipar, sizeof(int)))
        return 0;    

    if(!cwrite(finfo, &ctype, sizeof(int)))
        return 0;    

    if(!cwrite(finfo, &sqltype, sizeof(int)))
        return 0;    

    if(!cwrite(finfo, len, sizeof(long)))
        return 0;    

    if(!cwrite(finfo, buf, (size_t)*len))
        return 0;    

    ret = (int)mmap_used;

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_param(SFI *finfo, int *idx)
{
    DB_HANDLE *h;
    int ret=0, retsize=0;
    void *buf = finfo->mapinfo->mem;
    int ipar, ctype, sqltype;
    long *len;
    int *ip;
    void *data;
    size_t pos=0;

    /* idx */
    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(size_t));
        return 0;
    }
    if(*idx != -1)
    {
        h=all_handles[*idx];
    }
    else
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(forkread(&retsize, sizeof(int)) == -1)
        return 0;

    if (retsize<0)
    {
        //not enough space in memmap for entire response
        retsize=get_chunks(finfo,retsize);
        buf = finfo->aux;
    }

    ip = buf;
    ipar = *ip;
    pos += sizeof(int);

    ip = pos + buf; 
    ctype = *ip;
    pos += sizeof(int);

    ip = pos + buf;
    sqltype = *ip;
    pos += sizeof(int);

    len = pos + buf;
    pos += sizeof(long);

    data = pos + buf;

    ret = texis_param(h->tx, ipar, data, len, ctype, sqltype); 

    if(finfo->aux)
    {
        free(finfo->aux);
        finfo->aux=NULL;
        finfo->auxsz=0;
        finfo->auxpos=NULL;
    }

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}



static int fork_exec(DB_HANDLE *h)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("e", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return ret;
    
    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;    
}

static int child_exec(SFI *finfo, int *idx)
{
    DB_HANDLE *h;
    int ret=0;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        h=all_handles[*idx];
    }
    else
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    ret = texis_execute(h->tx);
    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_prep(DB_HANDLE *h, char *sql)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    /* write sql statement to start of mmap */

    sprintf(finfo->mapinfo->mem,"%s", sql);

    if(forkwrite("p", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;

    // get the result back
    if(forkread(&ret, sizeof(int)) == -1)
    {
        return 0;
    }

    return ret;
}

static int child_prep(SFI *finfo, int *idx)
{
    DB_HANDLE *h;
    int ret=0;
    char *sql=finfo->mapinfo->mem;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        h=all_handles[*idx];
    }
    else
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    ret = texis_prepare(h->tx, sql);
    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int fork_open(DB_HANDLE *h)
{
    SFI *finfo;
    int ret=-1;
    if(n_sfi < totnthreads)
    {
        int i=n_sfi;
        REMALLOC(sqlforkinfo, totnthreads  * sizeof(SFI *));
        REMALLOC(errmap, totnthreads  * sizeof(char *));
        n_sfi = totnthreads;
        while (i<n_sfi)
        {
            if(i) /* errmap[0] is elsewhere */
                errmap[i]=NULL;
            sqlforkinfo[i++]=NULL;
        }   
    }

    if (h->forkno>=n_sfi)
        return ret;

    if(sqlforkinfo[h->forkno] == NULL)
    {
        REMALLOC(sqlforkinfo[h->forkno], sizeof(SFI));
        finfo=sqlforkinfo[h->forkno];
        finfo->reader=-1;
        finfo->writer=-1;
        finfo->childpid=0;
        /* the field list for any fetch calls*/
        finfo->fl=NULL;
        /* the shared mmap */
        finfo->mapinfo=NULL;
        /* the aux buffer */
        finfo->aux=NULL;
        finfo->auxsz=0;
        finfo->auxpos=NULL;
        REMALLOC(finfo->mapinfo, sizeof(FMINFO));

        finfo->mapinfo->mem = mmap(NULL, FORKMAPSIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);;
        if(finfo->mapinfo->mem == MAP_FAILED)
        {
            fprintf(stderr, "mmap failed: %s\n",strerror(errno));
            exit(1);
        }
        finfo->mapinfo->pos = finfo->mapinfo->mem;

        errmap[h->forkno] = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);;
        if(errmap[h->forkno] == MAP_FAILED)
        {
            fprintf(stderr, "errmsg mmap failed: %s\n",strerror(errno));
            exit(1);
        }
    }

    finfo = check_fork(h, Create);
    if(finfo->childpid)
    {
        /* write db string to map */
        sprintf(finfo->mapinfo->mem, "%s", h->db);

        /* write o for open, the size of db and the string db */
        if(forkwrite("o", sizeof(char)) == -1)
            return ret;

        if(forkread(&ret, sizeof(int)) == -1)
        {
            return -1;
        }
    }
    return ret;
}

static int child_open(SFI *finfo, int *idx)
{
    DB_HANDLE *h;
    char *db = finfo->mapinfo->mem;

    h=h_open(db,0,NULL);
    if(h)
        *idx = h->idx;

    if(forkwrite(idx, sizeof(int)) == -1)
        return 0;

    return 1;
}

static int fork_close(DB_HANDLE *h)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("c", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;
    
    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    release_handle(h);
    return ret;    
}

static int child_close(SFI *finfo, int *idx)
{
    int ret=0;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        release_handle(all_handles[*idx]);
        ret=1;
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_flush(DB_HANDLE *h)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("F", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;
    
    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;    
}

static int child_flush(SFI *finfo, int *idx)
{
    int ret=0;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        ret = texis_flush(all_handles[*idx]->tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("g", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;
    
    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    if(ret)
    {
        memcpy(countInfo, finfo->mapinfo->mem, sizeof(TXCOUNTINFO));
    }

    return ret;    
}

static int child_getCountInfo(SFI *finfo, int *idx)
{
    int ret=0;
    TXCOUNTINFO *countInfo = finfo->mapinfo->mem;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        ret = texis_getCountInfo(all_handles[*idx]->tx, countInfo);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_skip(DB_HANDLE *h, int nrows)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("s", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;

    if(forkwrite(&nrows, sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;    
}

static int child_skip(SFI *finfo, int *idx)
{
    int ret=0, nrows=0;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if (forkread(&nrows, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        ret = texis_flush_scroll(all_handles[*idx]->tx, nrows);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_resetparams(DB_HANDLE *h)
{
    SFI *finfo = check_fork(h, NoCreate);
    int ret=1;

    if(!finfo)
        return 0;

    if(forkwrite("r", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;    
}

static int child_resetparams(SFI *finfo, int *idx)
{
    int ret=0;

    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        ret=texis_resetparams(all_handles[*idx]->tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_set(duk_context *ctx, DB_HANDLE *h, char *errbuf)
{
    SFI *finfo = check_fork(h, Create);
    duk_size_t sz;
    int size;
    void *p;

    int ret=0;

    if(!finfo)
        return 0;

    duk_cbor_encode(ctx, -1, 0);
    p=duk_get_buffer_data(ctx, -1, &sz);
    memcpy(finfo->mapinfo->mem, p, (size_t)sz);

    if(forkwrite("S", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->fork_idx), sizeof(int)) == -1)
        return 0;

    size = (int)sz;
    if(forkwrite(&size, sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    if(ret > 0)
    {
        if(forkread(&size, sizeof(int)) == -1)
            return 0;

        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, finfo->mapinfo->mem, (duk_size_t)size);
        duk_cbor_decode(ctx, -1, 0);
    } 
    else if (ret < 0)
    {
        strncpy(errbuf, finfo->mapinfo->mem, 1023);
    }

    return ret;    
}

static int child_set(SFI *finfo, int *idx)
{
    int ret=0, bufsz=0;
    duk_context *ctx;
    int size=-1;

    if(thread_ctx && thread_ctx[thisfork])
        ctx = thread_ctx[thisfork];
    else
        ctx = main_ctx;
    if (forkread(idx, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if (forkread(&bufsz, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(*idx != -1)
    {
        char errbuf[1024];
        void *p;

        errbuf[0]='\0';

        /* get js object needed for sql_set to do its stuff */
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, finfo->mapinfo->mem, (duk_size_t)bufsz);
        duk_cbor_decode(ctx, -1, 0);

        /* do the set in this child */
        ret = sql_set(ctx, all_handles[*idx], errbuf);

        ((char*)finfo->mapinfo->mem)[0] = '\0';//abundance of caution.

        /* this is only filled if ret < 0 
           and is error text to be sent back to JS  */
        if( ret < 0 )
            strncpy(finfo->mapinfo->mem, errbuf, 1023);

        /*there's some JS data in an object that needs to be sent back */
        else if (ret > 0 )
        {
            duk_size_t sz;

            duk_cbor_encode(ctx, -1, 0);
            p=duk_get_buffer_data(ctx, -1, &sz);
            memcpy(finfo->mapinfo->mem, p, (size_t)sz);
            size = (int)sz;
        }
        /* else == 0 -- all is ok, just no data to send back */
    }
    else
        ret=-1;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    if(size > -1)
    {
        if (forkwrite(&size, sizeof(int))  == -1)
            return 0;
    }

    return 1; // close will always happen
}

/* in child process, loop and await commands */
static void do_fork_loop(SFI *finfo)
{
    while(1)
    {
        int idx_d=-1, *idx = &idx_d;
        char command='\0';
        int ret = kill(parent_pid,0);

        if( ret )
            exit(0);

        ret = forkread(&command, sizeof(char));
        if (ret == 0)
        {
            /* a read of 0 size might mean the parent exited, 
               otherwise this shouldn't happen                 */
            usleep(10000);
            continue;
        }
        /* this is in fork read now
        else if (ret == -1)
            exit(0);
        */

        clearmsgbuf(); // reset the position of the errmap buffer.

        switch(command)
        {
            case 'o':
                ret = child_open(finfo, idx);
                break;
            case 'c':
                ret = child_close(finfo, idx);
                break;
            case 'p':
                ret = child_prep(finfo, idx);
                break;
            case 'e':
                ret = child_exec(finfo, idx);
                break;
            case 'f':
                ret = 1;
                child_fetch(finfo, idx);
                break;
            case 'r':
                ret = child_resetparams(finfo, idx);
                break;
            case 'P':
                ret = child_param(finfo, idx);
                break;
            case 'F':
                ret = child_flush(finfo, idx);
                break;
            case 's':
                ret = child_skip(finfo, idx);
                break;
            case 'g':
                ret = child_getCountInfo(finfo, idx);
                break;
            case 'S':
                ret = child_set(finfo, idx);
                break;
        }
        /* there will be a call to h_close in parent as well
           but since something went wrong somewhere, we want 
           to make sure any errors don't result in
           a pileup of unreleased handles */
        if(!ret && *idx > -1)
            release_handle(all_handles[*idx]);
    }
}

static int h_flush(DB_HANDLE *h)
{
    if(h->forkno>0)
        return fork_flush(h);
    return TEXIS_FLUSH(h->tx);
}

static int h_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    if(h->forkno>0)
        return fork_getCountInfo(h, countInfo);
    return TEXIS_GETCOUNTINFO(h->tx, countInfo);
}

static int h_resetparams(DB_HANDLE *h)
{
    if(h->forkno>0)
        return fork_resetparams(h);
    return TEXIS_RESETPARAMS(h->tx);
}

static int h_param(DB_HANDLE *h, int pn, void *d, long *dl, int t, int st)
{
    if(h->forkno>0)
        return fork_param(h, pn, d, dl, t, st);
    return TEXIS_PARAM(h->tx, pn, d, dl, t, st);
}

static int h_skip(DB_HANDLE *h, int n)
{
    if(h->forkno>0)
        return fork_skip(h,n);
    return TEXIS_SKIP(h->tx, n);
}

static int h_prep(DB_HANDLE *h, char *sql)
{
    if(h->forkno>0)
        return fork_prep(h, sql);
    return TEXIS_PREP(h->tx, sql);
}

static int h_close(DB_HANDLE *h)
{
    if(h->forkno>0)
        return fork_close(h);

    release_handle(h);
    return 1;
}

/* TODO: check for this
   Error: sql exec error: 005 Corrupt block header at 0x0 in KDBF file ./docdb/SYSTABLES.tbl in the function: read_head
   and if so, try redoing all the texis handles to see if the table was dropped and readded.
*/

static int h_exec(DB_HANDLE *h)
{
    if(h->forkno>0)
        return fork_exec(h);
    return TEXIS_EXEC(h->tx);
}

static FLDLST *h_fetch(DB_HANDLE *h,  int stringsFrom)
{
    if(h->forkno>0)
        return fork_fetch(h, stringsFrom);
    return TEXIS_FETCH(h->tx, stringsFrom);
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



#include "db_misc.c" /* copied and altered thunderstone code for stringformat and abstract */

/* **************************************************
    initialize query struct
   ************************************************** */
void duk_rp_init_qstruct(QUERY_STRUCT *q)
{
    q->sql = (char *)NULL;
    q->arr_idx = -1;
    q->str_idx = -1;
    q->obj_idx=-1;
    q->arg_idx=-1;
    q->callback = -1;
    q->skip = 0;
    q->max = -432100000; //-1 means unlimit, -0.4321 billion means not set.
    q->rettype = -1;
    q->getCounts = 0;
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
    duk_idx_t i = 0;
    int gotsettings=0, maxset=0, selectmax=-432100000;
    QUERY_STRUCT q_st;
    QUERY_STRUCT *q = &q_st;

    duk_rp_init_qstruct(q);

    for (i = 0; i < 6; i++)
    {
        int vtype = duk_get_type(ctx, i);
        switch (vtype)
        {
            case DUK_TYPE_NUMBER:
            {
                if(maxset==1)
                    q->skip=duk_get_int(ctx, i);
                else if(!maxset)
                    q->max=duk_get_int(ctx, i);
                else
                    RP_THROW(ctx, "too many Numbers in parameters to sql.exec()");

                maxset++;
                break;
            }
            case DUK_TYPE_STRING:
            {
                int l;
                if (q->sql != (char *)NULL)
                {
                    RP_THROW(ctx, "Only one string may be passed as a parameter and must be a sql statement.\n");
                    //duk_push_int(ctx, -1);
                    //q->sql = (char *)NULL;
                    //q->err = QS_ERROR_PARAM;
                    //return (q_st);
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
                /* it hasn't been set yet. we don't want to overwrite returnRows or returnType */
                if(q->rettype == -1)
                {
                    if(strncasecmp(q->sql, "select", 6))
                        q->rettype=2;
                    else
                        q->rettype=0;
                }
                
                /* selectMaxRows from this */
                if(!strncasecmp(q->sql, "select", 6))
                {
                    duk_push_this(ctx);
                    duk_get_prop_string(ctx, -1, "selectMaxRows");
                    selectmax=duk_get_int_default(ctx, -1, RESMAX_DEFAULT);
                    duk_pop_2(ctx);
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
                            q->getCounts = REQUIRE_BOOL(ctx, -1, "sql: includeCounts must be a Boolean");
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "argument"))
                        {
                            q->arg_idx = duk_get_top_index(ctx);
                            gotsettings=1;
                        }
                        /* leave it on the stack for use in callback */
                        else
                        {
                            duk_pop(ctx);
                            /* alternative */
                            if (duk_get_prop_string(ctx, i, "arg"))
                            {
                                q->arg_idx = duk_get_top_index(ctx);
                                gotsettings=1;
                            }
                            /* leave it on the stack for use in callback */
                            else 
                                duk_pop(ctx);
                        }

                        if (duk_get_prop_string(ctx, i, "skipRows"))
                        {
                            q->skip = REQUIRE_INT(ctx, -1, "skipRows must be a Number");
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "maxRows"))
                        {
                            q->max = REQUIRE_INT(ctx, -1, "sql: maxRows must be a Number");
                            gotsettings=1;
                        }

                        if (duk_get_prop_string(ctx, i, "returnRows"))
                        {
                            if (REQUIRE_BOOL(ctx, -1, "sql: returnRows must be a Boolean"))
                                q->rettype = 0;
                            else
                                q->rettype = 2;
                            gotsettings=1;
                        }
                        duk_pop(ctx);

                        if (duk_get_prop_string(ctx, i, "returnType"))
                        {
                            const char *rt = REQUIRE_STRING(ctx, -1, "sql: returnType must be a String");

                            if (!strcasecmp("array", rt))
                            {
                                q->rettype = 1;
                            }
                            else if (!strcasecmp("novars", rt))
                            {
                                q->rettype = 2;
                            }
                            else if (!strcasecmp("object", rt))
                                q->rettype=0;
                            else
                                RP_THROW(ctx, "sql: returnType '%s' is not valid", rt);
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

    /* if qmax is not set and we are in a select, set to this.selectMaxRows, or RESMAX_DEFAULT */
    if( q->max == -432100000 && selectmax != -432100000)
        q->max = selectmax;

    if (q->max < 0)
        q->max = INT_MAX;

    if (q->skip < 0)
        q->skip = 0;

    if (q->sql == (char *)NULL)
    {
        //q->err = QS_ERROR_PARAM;
        RP_THROW(ctx, "sql - No sql statement present.\n");
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
    DB_HANDLE *h,
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

        duk_get_prop_string(ctx, obj_loc, key);
        if(!duk_is_undefined(ctx, -1))
        {
            push_sql_param;

            /* texis_params is indexed starting at 1 */
            rc = h_param(h, i+1, v, &plen, in, out);
        }
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

int duk_rp_add_parameters(duk_context *ctx, DB_HANDLE *h, duk_idx_t arr_loc)
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
        rc = h_param(h, (int)arr_i, v, &plen, in, out);
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
        duk_size_t  sz = (duk_size_t) strlen(fl->data[i]);

        if(sz > fl->ndata[i])
            sz = fl->ndata[i];
        duk_push_lstring(ctx, (char *)fl->data[i], sz );
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
   This is called when sql.exec() has no callback.
   fetch rows and push results to array 
   return number of rows 
   ************************************************** */
int duk_rp_fetch(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
{
    int rown = 0, i = 0,
        resmax = q->max,
        rettype = q->rettype;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->getCounts)
        h_getCountInfo(h, &cinfo);

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
            if((fl = h_fetch(h, -1)))
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
        while (rown < resmax && (fl = h_fetch(h, -1)))
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
        while (rown < resmax && (fl = h_fetch(h, -1)))
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
int duk_rp_fetchWCallback(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
{
    int rown = 0, i = 0,
        resmax = q->max,
        rettype = q->rettype;
    duk_idx_t callback_idx = q->callback, colnames_idx=0, count_idx=-1;
    FLDLST *fl;
    TXCOUNTINFO cinfo;

    if(q->getCounts)
    {
        h_getCountInfo(h, &cinfo);
        pushcounts;             /* countInfo */
    }
    else
    {
        duk_push_object(ctx);
    }
    count_idx=duk_get_top_index(ctx);

#define docallback do {\
    duk_dup(ctx, count_idx);\
    if(q->arg_idx > -1){\
        duk_dup(ctx, q->arg_idx);\
        duk_call_method(ctx, 5);\
    } else duk_call_method(ctx, 4);\
} while(0)

    while (rown < resmax && (fl = h_fetch(h, -1)))
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
                duk_push_int(ctx, q->skip + rown++ );
                duk_dup(ctx, colnames_idx);
                docallback;
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

                duk_push_int(ctx, q->skip + rown++ );
                duk_dup(ctx, colnames_idx);
                docallback;
                break;
            }
            /* novars */
            case 2:
            {
                duk_push_object(ctx);       /*empty object */
                duk_push_int(ctx, q->skip + rown++ ); /* index */
                duk_dup(ctx, colnames_idx);
                docallback;
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
    DB_HANDLE *h = NULL;
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
    h = h_open(db, fromContext, ctx);
    if(!h)
        throw_tx_error(ctx,h,"sql open");
    tx = h->tx;
    if (!tx)
    {
        h_close(h);
        closecsv;
        throw_tx_error(ctx,h,"open sql");
    }

    {
        FLDLST *fl;
        char sql[128];

        snprintf(sql, 128, "select NAME from SYSCOLUMNS where TBNAME='%s' order by ORDINAL_POSITION;", dcsv.tbname);

        if (!h_prep(h, sql))
        {
            closecsv;
            h_close(h);
            throw_tx_error(ctx,h,"sql prep");
        }

        if (!h_exec(h))
        {
            closecsv;
            h_close(h);
            throw_tx_error(ctx,h,"sql exec");
        }


        while((fl = h_fetch(h, -1)))
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
    h_close(h);\
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

            if (!h_prep(h, sql))
            {
                fn_cleanup;
                throw_tx_error(ctx,h,"sql prep");
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
                    if( !h_param(h, col+1, v, &plen, in, out))
                    {
                        fn_cleanup;
                        throw_tx_error(ctx,h,"sql add parameters");
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
                        if( !h_param(h, col+1, v, &plen, in, out))
                        {
                            fn_cleanup;
                            throw_tx_error(ctx,h,"sql add parameters");
                        }
                    }
                }
                
                if (!h_exec(h))
                {
                    fn_cleanup;
                    throw_tx_error(ctx,h,"sql exec");
                }
                if (!h_flush(h))
                {
                    fn_cleanup;
                    throw_tx_error(ctx,h,"sql flush");
                }

                h_resetparams(h);

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
    duk_rp_log_tx_error(ctx,h,pbuf); /* log any non fatal errors to this.errMsg */
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
    
    //REMALLOC(sql, strlen(old_sql)+1); // the new_sql cant be bigger than the old
    REMALLOC(sql, strlen(old_sql)*2);   // now it can, because of extra
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
                memcpy(out_p,t,quote_len+1);     // the plus 1 is for the quote character
                out_p+=quote_len+1;
                break;
             }
          case '\\': break;
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
    DB_HANDLE *hcache = NULL;
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

    /* OPEN */
    hcache = h_open(db, fromContext, ctx);
    if(!hcache)
        throw_tx_error(ctx,hcache,"sql open");
    tx = hcache->tx;
    if (!tx && !hcache->forkno)
        throw_tx_error(ctx,hcache,"open sql");

    /* PREP */
    if (!h_prep(hcache, (char *)q->sql))
    {
        h_close(hcache);
        throw_tx_error(ctx,hcache,"sql prep");
    }


    /* PARAMS
       sql parameters are the parameters corresponding to "?key" in a sql statement
       and are provide by passing an object in JS call parameters */
    if( namedSqlParams)
    {
        duk_idx_t idx=-1;
        if(q->obj_idx != -1)
            idx=q->obj_idx;
        else if (q->arr_idx != -1)
            idx = q->arr_idx;
        else
        {
            h_close(hcache);
            RP_THROW(ctx, "sql.exec - parameters specified in sql statement, but no corresponding object or array\n");
        }
        if (!duk_rp_add_named_parameters(ctx, hcache, idx, namedSqlParams, nParams))
        {
            free(namedSqlParams);
            free(freeme);
            h_close(hcache);
            throw_tx_error(ctx,hcache,"sql add parameters");
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
        if (!duk_rp_add_parameters(ctx, hcache, q->arr_idx))
        {
            h_close(hcache);
            throw_tx_error(ctx,hcache,"sql add parameters");
        }
    }
    else
    {
        h_resetparams(hcache);
    }
    /* EXEC */

    if (!h_exec(hcache))
    {
        h_close(hcache);
        throw_tx_error(ctx,hcache,"sql exec");
    }
    /* skip rows using texisapi */
    if (q->skip)
        h_skip(hcache, q->skip);

    /* callback - return one row per callback */
    if (q->callback > -1)
    {
        int rows = duk_rp_fetchWCallback(ctx, hcache, q);
        duk_push_int(ctx, rows);
        goto end; /* done with exec() */
    }

    /*  No callback, return all rows in array of objects */
    (void)duk_rp_fetch(ctx, hcache, q);

end:
    h_close(hcache);
    duk_rp_log_tx_error(ctx,hcache,pbuf); /* log any non fatal errors to this.errMsg */
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
    //if(duk_is_undefined(ctx, -1))
    //    duk_push_object(ctx);
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

static void rp_set_tx_defaults(duk_context *ctx, DB_HANDLE *h, int rampartdef)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    TEXIS *tx = h->tx;

    if(!tx && !h->forkno)
        throw_tx_error(ctx,h,"open sql");
    
    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
        throw_tx_error(ctx,h,"open sql");

    TXresetproperties(ddic);

#define duk_tx_set_prop(prop,val) \
if(setprop(ddic, prop, val )==-1)\
    throw_tx_error(ctx,h,"sql set");

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

static int sql_set(duk_context *ctx, DB_HANDLE *hcache, char *errbuf)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    TEXIS *tx = hcache->tx;
    const char *val="";
    char pbuf[msgbufsz];
    int added_ret_obj=0;
    char *rlsts[]={"noiseList","suffixList","suffixEquivsList","prefixList"};

    clearmsgbuf();

    duk_rp_log_error(ctx, "");

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        sprintf(errbuf,"sql open");
        return -2;
    }

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        int retlisttype=-1, setlisttype=-1, i=0;
        char propa[64], *prop=&propa[0];
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
        {
            sprintf(errbuf, "sql.set - '%s' - unknown/invalid property", dprop);
            return -1;
        }
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
                    rp_set_tx_defaults(ctx, hcache, 1);
                }
                goto propnext;
            }
            if(duk_is_string(ctx, -1))
                val=duk_get_string(ctx, -1);
            else
                goto default_err;
            if(!strcasecmp("texis", val))
            {
                rp_set_tx_defaults(ctx, hcache, 0);
                goto propnext;
            }
            else if(!strcasecmp("rampart", val))
            {
                rp_set_tx_defaults(ctx, hcache, 1);
                goto propnext;
            }

            default_err:
            sprintf(errbuf, "sql.set() - property defaults option requires a string('texis' or 'rampart') or a boolean");
            return -1;
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
                        sprintf(errbuf, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                        return -1;
                    }
                    nl[i]=strdup(duk_get_string(ctx, -1));
                    duk_pop(ctx);
                    i++;
                }
                nl[i]=strdup("");
            }
            else
            {
                sprintf(errbuf, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                return -1;
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
                            sprintf(errbuf, "sql.set: deleteExpressions - array must be an array of strings, expressions or numbers (expressions or expression index)\n");
                            return -1;
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
                        sprintf(errbuf, "sql.set: deleteIndexTemp - array must be an array of strings or numbers\n");
                        return -1;
                    }                
                }
                else if (ptype==0)
                {
                    aval=get_exp(ctx, -1);
                    
                    if(!aval)
                    {
                        sprintf(errbuf, "sql.set: addExpressions - array must be an array of strings or expressions\n");
                        return -1;
                    }
                }
                else
                {
                    if(!duk_is_string(ctx, -1))
                    {
                        sprintf(errbuf, "sql.set: addIndexTemp - array must be an array of strings\n");
                        return-1;
                    }
                    aval=duk_get_string(ctx, -1);
                }
                if(setprop(ddic, (char*)prop, (char*)aval )==-1)
                {
                    sprintf(errbuf, "sql set");
                    return -2;
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
                    sprintf(errbuf, "invalid value '%s'", duk_safe_to_string(ctx, -1));
                    return -1;
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
                sprintf(errbuf, "sql set");
                return -2;
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
        msgtobuf(pbuf);
        if(*pbuf!='\0')
        {
            sprintf(errbuf, "sql.set(): %s", pbuf+4);
            return -1;
        }
    }

    duk_rp_log_tx_error(ctx,hcache,pbuf); /* log any non fatal errors to this.errMsg */

    if(added_ret_obj)
    {
        duk_pull(ctx, 0);
        return 1;
    }
    return 0;
}

duk_ret_t duk_texis_set(duk_context *ctx)
{
    const char *db;
    DB_HANDLE *hcache = NULL;
    duk_push_this(ctx);
    int ret = 0;
    char errbuf[1024];

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    hcache = h_open(db, fromContext, ctx);

    if(!hcache)
        throw_tx_error(ctx,hcache,"sql open");

    if(!hcache || (!hcache->tx && !hcache->forkno) )
    {
        h_close(hcache);
        throw_tx_error(ctx,hcache,"open sql");
    }
    
    duk_rp_log_error(ctx, "");

    if(!duk_is_object(ctx, -1) || duk_is_array(ctx, -1) || duk_is_function(ctx, -1) )
        RP_THROW(ctx, "sql.set() - object with {prop:value} expected as parameter - got '%s'",duk_safe_to_string(ctx, -1));

    if(hcache->forkno)
    {
        // regular expressions in addexp don't survive cbor
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            const char *k = duk_get_string(ctx, -2);
            if(
                !strcasecmp("addexp",k)           || 
                !strcasecmp("addexpressions",k)   ||
                !strcmp("delexpressions", k)      || 
                !strcmp("deleteexpressions", k)   ||
                !strcasecmp("delexp",k)
            )
            {
                duk_uarridx_t ix=0, len;

                if(!duk_is_array(ctx, -1))
                    RP_THROW(ctx, "sql.set: %s - array must be an array of strings or expressions\n", k);

                len=duk_get_length(ctx, -1);
                while (ix < len)
                {
                    duk_get_prop_index(ctx, -1, ix);
                    if(duk_is_object(ctx,-1) && duk_has_prop_string(ctx,-1,"source") )
                    {
                        duk_get_prop_string(ctx, -1,"source");
                        duk_put_prop_index(ctx, -3, ix);
                    }
                    duk_pop(ctx);
                    ix++;
                }
            }
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
        ret = fork_set(ctx, hcache, errbuf);
    }
    else
    {
        ret = sql_set(ctx, hcache, errbuf);
    }
    h_close(hcache);

    if(ret == -1)
        RP_THROW(ctx, "%s", errbuf);
    else if (ret ==-2)
        throw_tx_error(ctx, hcache, errbuf);

    return (duk_ret_t) ret;
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

    const char *db = duk_get_string(ctx, 0);
    char pbuf[msgbufsz];

    /* allow call to Sql() with "new Sql()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }
    g_hcache_pid = getpid();

    /* 
     if sql=new Sql("/db/path",true), we will 
     create the db if it does not exist 
    */
    if (duk_is_boolean(ctx, 1) && duk_get_boolean(ctx, 1) != 0)
    {
        /* check for db first */
        DB_HANDLE *h = h_open( (char*)db, fromContext, ctx );
        if (!h || (h->tx == NULL && !h->forkno) )
        {
            /* don't log the error */
            //fclose(mmsgfh);
            //mmsgfh = fmemopen(NULL, msgbufsz, "w+");
            clearmsgbuf();
            if (!createdb(db))
            {
                duk_rp_log_tx_error(ctx,h,pbuf);
                RP_THROW(ctx, "cannot create database at '%s' (root path not found, lacking permission or other error\n%s)", db, pbuf);
            }
        }
        duk_rp_log_tx_error(ctx,h,pbuf); /* log any non fatal errors to this.errMsg */
        h_close(h);
    }

    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, "db");
    duk_push_number(ctx, RESMAX_DEFAULT);
    duk_put_prop_string(ctx, -2, "selectMaxRows");
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

    return 0;
}

/* **************************************************
   Initialize Sql module 
   ************************************************** */
char install_dir[PATH_MAX+15];

duk_ret_t duk_open_module(duk_context *ctx)
{
    /* Set up locks:
     * this will be run once per new duk_context/thread in server.c
     * but needs to be done only once for all threads
     */

    if (!db_is_init)
    {
        char *rampart_path=getenv("RAMPART_PATH");
        char *TexisArgv[2];

        if (pthread_mutex_init(&lock, NULL) != 0)
        {
            printf("\n mutex init failed\n");
            exit(1);
        }

        REMALLOC(errmap, sizeof(char*));
        errmap[0]=NULL;
        //if 0 was to fork, we'd do this.
        //errmap[0] = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);;
        //but currently it does not, so we do this:
        REMALLOC(errmap[0], msgbufsz);
        mmsgfh = fmemopen(errmap[0], msgbufsz, "w+");

        TexisArgv[0]=argv0;
        if(!rampart_path)
        {
            struct stat sb;

            if (stat("/usr/local/rampart", &sb) == 0 && S_ISDIR(sb.st_mode))
                TexisArgv[1]="--install-dir=/usr/local/rampart";
        }
        else
        {
            strcpy (install_dir, "--install-dir=");
            strcat (install_dir, rampart_path);
            TexisArgv[1]=install_dir;
        }
        TXinitapp(NULL, NULL, 2, TexisArgv, NULL, NULL);
        db_is_init = 1;
    }

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_sql_constructor, 3 /*nargs*/);

    /* Push object that will be Sql.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Sql protoObj ] */

    /* Set Sql.prototype.exec. */
    duk_push_c_function(ctx, duk_rp_sql_exec, 6 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
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

    duk_push_c_function(ctx, RPsqlFuncs_abstract, 5);
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

    add_exit_func(free_all_handles, NULL);

    return 1;
}
