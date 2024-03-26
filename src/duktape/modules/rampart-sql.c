/* Copyright (C) 2024  Aaron Flin - All Rights Reserved
 * You may use, distribute this code under the
 * terms of the Rampart Source Available License.
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
#include "event.h"

pthread_mutex_t tx_handle_lock;
pthread_mutex_t tx_set_lock;

static int defnoise=1, defsuffix=1, defsuffixeq=1, defprefix=1;

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
    duk_idx_t obj_idx;  /* location of ?named parameters if not using array above, or -1 */
    duk_idx_t str_idx;
    duk_idx_t arg_idx;  /* location of extra argument for callback */
    duk_idx_t callback; /* location of callback in ctx, or -1 */
    int skip;           /* number of results to skip */
    int64_t max;        /* maximum number of results to return */
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
//#define FORKMAPSIZE 2 -- for testing - note that for sql_set in fork, 2048 bytes might be used for errors
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
//char **errmap=NULL;

/* one SFI struct for each thread in rampart-threads.
   Some threads can run without a fork (main thread and one thread in rampart-server)
   while the rest will fork in order to avoid locking
   around texis_* calls.

   If not in the server, or if not threading, then
   all operations will be non forking.
*/

#define SFI struct sql_fork_info_s
SFI
{
    int reader;         // pipe to read from, in parent or child
    int writer;         // pipe to write to, in parent or child
    pid_t childpid;     // process id of the child if in parent (return from fork())
    FMINFO *mapinfo;    // the shared mmap for reading and writing in both parent and child
    char *errmap;       // the shared mmap for errors
    void *aux;          // if data is larger than mapsize, we need to copy it in chunks into here
    void *auxpos;
    size_t auxsz;
    FLDLST *fl;         // modified FLDLST for parent pointing to areas in mapinfo
};

// info for thread/fork pairing as a thread local
__thread SFI *finfo = NULL;

// some string functions don't fork.  We need an error map for them
char *errmap0;

extern int RP_TX_isforked;

// lock handle list while operating from multiple threads
#define HLOCK RP_PTLOCK(&tx_handle_lock);
#define HUNLOCK RP_PTUNLOCK(&tx_handle_lock);

// lock around texis' setprop() and fork()
// NO forking while in the middle of freeing and setting texis globals
#define SETLOCK if(!RP_TX_isforked) RP_PTLOCK(&tx_set_lock);
#define SETUNLOCK if(!RP_TX_isforked) RP_PTUNLOCK(&tx_set_lock);


#define DB_HANDLE struct db_handle_s_list
DB_HANDLE
{
    TEXIS *tx;                  // a texis handle, NULL if forking
    char *db;                   // the db path.
    DB_HANDLE *next;            // linked list
    DB_HANDLE *prev;            // doublylinked list
    uint16_t forknum;           // convenience. Same as the threadnum from rampart-threads. So forknum == threadnum
    char flags;                 // bit 0 - if the texis handle is in the corresponding fork
                                // bit 1 - if handle is available (not in use);
};

// if the handle corresponds to a *tx handle in a forked child
#define DB_FLAG_FORK 1
// if the handle is currently in use (on the main db_handle_head list)
// if not in use, it is on the thread local db_handle_available_head list.
#define DB_FLAG_IN_USE 2

#define DB_HANDLE_SET(h,flag)   do { (h)->flags |=  (flag);  } while (0) 
#define DB_HANDLE_CLEAR(h,flag) do { (h)->flags &= ~(flag); } while (0) 
#define DB_HANDLE_IS(h,flag) ( (h)->flags & flag ) 


__thread DB_HANDLE 
    *db_handle_available_head=NULL,
    *db_handle_available_tail=NULL;

// max handles in available list, per thread/fork
// if more, old handles will be closed and freed.
#define MAX_HANDLES 16
__thread int nhandles=0;

// list of handles in use
DB_HANDLE *db_handle_head=NULL;

static int h_close(DB_HANDLE *h);

static DB_HANDLE *new_handle(const char *db)
{
    RPTHR *thr = get_current_thread();
    DB_HANDLE *h = NULL;

    REMALLOC(h, sizeof(DB_HANDLE));

    if(RPTHR_TEST(thr, RPTHR_FLAG_THR_SAFE))
        h->flags=0; // not using forked process, not in_use
    else
        h->flags=1; // using forked process, not in_use

    h->db=strdup(db);
    h->tx=NULL;
    h->forknum = (uint16_t)get_thread_num();
    h->next = h->prev = NULL;

    return h;
}

// add handle to main list
static void add_handle(DB_HANDLE *h)
{
    HLOCK

    if(db_handle_head != NULL)
        db_handle_head->prev=h;

    h->next=db_handle_head;
    db_handle_head=h;
    h->prev=NULL;
    DB_HANDLE_SET(h, DB_FLAG_IN_USE);

    HUNLOCK
}

// remove handle from whichever list it is on.
// locking must be done if on main list
static void remove_handle(DB_HANDLE *h)
{
    DB_HANDLE *n=h->next, *p=h->prev;
    
    if(p) p->next = n;  //if h=available_tail, n==NULL
    if(n) n->prev = p;  //if h==head, p==NULL

    if(h==db_handle_head)
         db_handle_head=n; // n->prev set to null above
    else if(h==db_handle_available_head)
         db_handle_available_head=n;  //n->prev set to null above

    if(h==db_handle_available_tail)
        db_handle_available_tail=p;  //p->next set to null above

    //if it was in the available list, decrement nhandles
    if(!DB_HANDLE_IS(h, DB_FLAG_IN_USE))
        nhandles--;    

    h->prev = h->next = NULL;
}

//insert at beginning of available list
static void mark_handle_available(DB_HANDLE *h)
{
    if(!h)
        return;

    // remove from main list
    HLOCK
    remove_handle(h);
    HUNLOCK

    // add to beginning of available list
    if(db_handle_available_head != NULL)
        db_handle_available_head->prev=h; // set old head->prev to new head
    else
        db_handle_available_tail=h;  //if null, both beginning and end of list

    h->next=db_handle_available_head; //set new head->next to old head
    db_handle_available_head=h; // h is now new head
    h->prev=NULL; // head always has prev==NULL
    DB_HANDLE_CLEAR(h, DB_FLAG_IN_USE);

    // limit list size:
    nhandles++;

    /* sanity check:
    int nh=0;
    DB_HANDLE *x=db_handle_available_head;
    while(x) {
        nh++;
        x=x->next;
    }
    printf("NHANDLES = %d vs %d\n",nhandles, nh);
    */

    while (nhandles > MAX_HANDLES)
    {
        //assuming a max > 1. i.e. there will always be a tail->prev
        h=db_handle_available_tail;
        db_handle_available_tail=h->prev;
        db_handle_available_tail->next=NULL;

        h_close(h);
        nhandles--;
    }
}

//remove from available list
static void mark_handle_in_use(DB_HANDLE *h)
{
    // remove from available list, no locking necessary (thread local)
    remove_handle(h);

    // add to main list
    add_handle(h); //this does DB_HANDLE_SET(h, DB_FLAG_IN_USE)
}

#define DBH_MARK_AVAILABLE 0
#define DBH_MARK_IN_USE    1

// find unused handle already open with given db
static DB_HANDLE *find_available_handle(const char *db, int in_use)
{
    DB_HANDLE *h=db_handle_available_head;

    //while not at end of list, and no match
    while(h && strcmp(h->db, db)!=0 )
        h=h->next;

    if(h && in_use)
        mark_handle_in_use(h);

    return h;
}


static DB_HANDLE * free_handle(DB_HANDLE *h)
{
    remove_handle(h);
    free(h->db);
    free(h);
    return NULL;
}

// for yosemite:
#ifdef __APPLE__
#include <Availability.h>
#  if __MAC_OS_X_VERSION_MIN_REQUIRED < 101300
#    include "fmemopen.h"
#    include "fmemopen.c"
#  endif
#endif


static int sql_set(duk_context *ctx, TEXIS *tx, char *errbuf);

//#define TXLOCK if(!RP_TX_isforked) {printf("lock from %d\n", thisfork);pthread_mutex_lock(&lock);}
//#define TXLOCK if(!RP_TX_isforked) pthread_mutex_lock(&lock);
//#define TXUNLOCK if(!RP_TX_isforked) pthread_mutex_unlock(&lock);

#define TXLOCK
#define TXUNLOCK


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
    DB_HANDLE *h = db_handle_head, *next;
    while(1)
    {
        if(h->tx)
            texis_cancel(h->tx);

        next=h->next;
        
        if(!next)
            break;
    }
    tx_rp_cancelled = 1;
}


pid_t parent_pid = 0;

#define msgbufsz 4096

#define throw_tx_error(ctx,pref) do{\
    duk_rp_log_error(ctx);\
    RP_THROW(ctx, "%s error: %s",pref, finfo->errmap);\
}while(0)

#define clearmsgbuf() do {                \
    fseek(mmsgfh, 0, SEEK_SET);           \
    fwrite("\0", 1, 1, mmsgfh);           \
    fseek(mmsgfh, 0, SEEK_SET);           \
} while(0)


/* **************************************************
     store an error string in this.errMsg
   **************************************************   */
void duk_rp_log_error_msg(duk_context *ctx, char *msg)
{
    duk_push_this(ctx);
    if(duk_has_prop_string(ctx,-1,"errMsg") )
    {
        duk_get_prop_string(ctx,-1,"errMsg");
        duk_push_string(ctx, msg);
        duk_concat(ctx,2);
    }
    else
        duk_push_string(ctx, msg);

#ifdef PUTMSG_STDERR
    if (msg && strlen(msg))
    {
//        pthread_mutex_lock(&printlock);
        fprintf(stderr, "%s\n", msg);
//        pthread_mutex_unlock(&printlock);
    }
#endif
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
}

void duk_rp_log_error(duk_context *ctx)
{
    finfo->errmap[msgbufsz-1]='\0';  //just a precaution
    duk_rp_log_error_msg(ctx, finfo->errmap);
}

/* get the expression from a /pattern/ or a "string" */
static const char *get_exp(duk_context *ctx, duk_idx_t idx)
{
    const char *ret=NULL;

    if(duk_is_object(ctx,idx) && duk_has_prop_string(ctx,idx,"source") )
    {
        // its a /pattern/, raw text is in property "source"
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
        fprintf(stderr, "fork write failed: '%s' at %d, fd:%d\n",strerror(errno),__LINE__,finfo->writer);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(1);}\
    };\
    r;\
})

#define forkread(b,c) ({\
    int r=0;\
    r= read(finfo->reader,(b),(c));\
    if(r==-1) {\
        fprintf(stderr, "fork read failed: '%s' at %d\n",strerror(errno),__LINE__);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(1);}\
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

static void free_thread_handles();

static void clean_thread(void *arg)
{
    pid_t *kpid = (pid_t*)arg;

    free_thread_handles();

    if(finfo)
    {
        if(finfo->mapinfo)
            free(finfo->mapinfo);
        if(finfo->aux)
            free(finfo->aux);
        if(finfo->fl)
            free(finfo->fl);

        free(finfo);
        finfo=NULL;
    }
    
    kill(*kpid,SIGTERM);
    //printf("killed child %d\n",(int)*kpid);
}

// this is only done in single threaded fork
static void free_all_handles_noClose()
{
    DB_HANDLE *h=db_handle_head, *n;
    while(h)
    {
        n=h->next;
        free(h->db);
        free(h);
        h=n;
    }

    h=db_handle_available_head;
    while(h)
    {
        n=h->next;
        free(h->db);
        free(h);
        h=n;
    }

    db_handle_head = db_handle_available_head = NULL;
}


static void do_child_loop(SFI *finfo);

#define Create 1
#define NoCreate 0

static SFI *check_fork(DB_HANDLE *h, int create)
{
    int pidstatus;
    if(finfo == NULL)
    {
        if(!create)
        {
            fprintf(stderr, "Unexpected Error: previously opened pipe info no longer exists for forknum %d\n",h->forknum);
            exit(1);
        }
        else
        {
            // FIXME: this is a memory leak in child process because other 
            // thread local 'finfo' allocs are lost when threads disappear upon fork
            // It's not growing, so probably harmless.
            REMALLOC(finfo, sizeof(SFI));
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
            finfo->errmap = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);;
            if(finfo->errmap == MAP_FAILED)
            {
                fprintf(stderr, "errmsg mmap failed: %s\n",strerror(errno));
                exit(1);
            }
        }

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
        if (rp_pipe(child2par) == -1)
        {
            fprintf(stderr, "child2par pipe failed\n");
            return NULL;
        }

        if (rp_pipe(par2child) == -1)
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
        SETLOCK
        finfo->childpid = fork();
        SETUNLOCK
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
            sigaction(SIGUSR2, &sa, NULL);

            setproctitle("rampart sql_helper");

            close(child2par[0]);
            close(par2child[1]);
            finfo->writer = child2par[1];
            finfo->reader = par2child[0];
            thisfork=h->forknum;
            mmsgfh = fmemopen(finfo->errmap, msgbufsz, "w+");
            fcntl(finfo->reader, F_SETFL, 0);

            libevent_global_shutdown();

            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            // free up handles from parent, but don't close *tx.
            free_all_handles_noClose();

            do_child_loop(finfo); // loop and never come back;
        }
        else
        {
            pid_t *pidarg=NULL;

            //parent
            signal(SIGPIPE, SIG_IGN); //macos
            signal(SIGCHLD, SIG_IGN);
            rp_pipe_close(child2par,1);
            rp_pipe_close(par2child,0);
            finfo->reader = child2par[0];
            finfo->writer = par2child[1];
            fcntl(finfo->reader, F_SETFL, 0);

            // a callback to kill child
            REMALLOC(pidarg, sizeof(pid_t));
            *pidarg = finfo->childpid;
            set_thread_fin_cb(rpthread[h->forknum], clean_thread, pidarg);
        }
    }

    return finfo;
}

static void free_thread_handles()
{
    DB_HANDLE *h=db_handle_head, *n;
    uint16_t forknum = get_thread_num();    

    // remove and free any in in-use list
    HLOCK
    while(h)
    {
        n=h->next;
        if(forknum == h->forknum)
            h_close(h);    
        h=n;
    }
    HUNLOCK

    // remove and free all in thread local available list
    h=db_handle_available_head;
    while(h)
    {
        n=h->next;
        h_close(h);
        h=n;
    }
    
}

static void free_all_handles(void *unused)
{
    DB_HANDLE *n, *h=db_handle_head;

    while(h)
    {
        n=h->next;
        h_close(h);
        h=n;
    }
}

static int fork_open(DB_HANDLE *h);

/* find first unused handle, create as necessary */
static DB_HANDLE *h_open(const char *db)
{
    DB_HANDLE *h = NULL;

    h=find_available_handle(db, DBH_MARK_IN_USE);

    if(!h)
    {
        h=new_handle(db);
        add_handle(h); //handle added to main list
        if( DB_HANDLE_IS(h, DB_FLAG_FORK) )
        {
            //  if pipe error
            if( !fork_open(h) )
            {
                HLOCK
                h=free_handle(h); //h==NULL
                HUNLOCK
            }
        }
        else
        {
            h->tx=texis_open((char *)(db), "PUBLIC", "");
            // if not using forked child, make sure we have a place to log errors in this proc
            if(!finfo)
            {
                REMALLOC(finfo, sizeof(SFI));
                finfo->errmap=errmap0;
            }
        }

        // error opening tx handle
        if(h && !h->tx)
        {
            HLOCK
            h=free_handle(h); //h==NULL
            HUNLOCK
        }
    }

    return h;
}

/*********** CHILD/PARENT FUNCTION PAIRS ************/

static int fork_create(const char *db)
{
    int ret=0;

    if(strlen(db) > PATH_MAX)
        return ret;

    strncpy(finfo->mapinfo->mem, db, PATH_MAX+1);

    if(forkwrite("C", sizeof(char)) == -1)
        return ret;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_create()
{
    int ret=0;
    
    ret=createdb(finfo->mapinfo->mem);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int get_chunks(int size)
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
    FLDLST *ret=NULL;
    int i=0, retsize=0;
    int *ilst=NULL;
    FMINFO *mapinfo;
    size_t eos=0;
    void *buf;

    check_fork(h, NoCreate);

    mapinfo = finfo->mapinfo;
    buf=mapinfo->mem;

    if(!finfo)
        return NULL;

    if(forkwrite("f", sizeof(char)) == -1)
        return NULL;

    if(forkwrite(&(h->tx), sizeof(TEXIS*)) == -1)
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
        retsize=get_chunks(retsize);
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
        size_t type_size = ddftsize(type),
               size = type_size * (size_t)ret->ndata[i];
        if(size==0)
            ret->data[i]=NULL;
        else
        {
            size_t size_mod = eos % type_size;

            //align to type
            if (size_mod)
                eos += (type_size - size_mod);

            ret->data[i]= (buf) + eos;
            eos += size;
        }
    }
    return ret;
}

static int cwrite(SFI *finfo, void *data, size_t sz)
{
    FMINFO *mapinfo = finfo->mapinfo;
    size_t rem = mmap_rem; //space available in mmap
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

static int cwrite_aligned(SFI *finfo, void *data, size_t sz, size_t tsz)
{
    FMINFO *mapinfo = finfo->mapinfo;

    //align to type
    if(tsz>1)
    {
        size_t sz_mod = (size_t)mapinfo->pos % tsz;
        if(sz_mod)
            mapinfo->pos += (tsz - sz_mod);
    }

    return cwrite(finfo, data, sz);

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

        // TODO: I think this is always the case, but check with TS
        // before removing the next two lines.
        if(fl->data[i] == NULL)
            ndata=0;

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
        size_t type_size = ddftsize(type),
               size = type_size * (size_t)fl->ndata[i];
        if(size !=0 && fl->data[i] != NULL)
        {
            // align ints, etc for arm
            if(!cwrite_aligned(finfo, fl->data[i], size, type_size))
                return -1;
        }
    }

    return (int) mmap_used;
}


static int child_fetch()
{
    int stringFrom=-9999;
    int ret=-1;
    FLDLST *fl;
    TEXIS *tx=NULL;

    /* idx */
    if (forkread(&tx, sizeof(TEXIS *) )  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(!tx)
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

    fl = texis_fetch(tx, stringFrom);

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
    int ret=0;
    FMINFO *mapinfo;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    mapinfo = finfo->mapinfo;
    mmap_reset;
    if(forkwrite("P", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
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

static int child_param()
{
    int ret=0, retsize=0;
    void *buf = finfo->mapinfo->mem;
    int ipar, ctype, sqltype;
    long *len;
    int *ip;
    void *data;
    size_t pos=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }
    if(!tx)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(forkread(&retsize, sizeof(int)) == -1)
        return 0;

    if (retsize<0)
    {
        //not enough space in memmap for entire response
        retsize=get_chunks(retsize);
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

    ret = texis_param(tx, ipar, data, len, ctype, sqltype);

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
    int ret=0;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    if(forkwrite("e", sizeof(char)) == -1)
        return ret;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return ret;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_exec()
{
    TEXIS *tx=NULL;
    int ret=0;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(!tx)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    ret = texis_execute(tx);
    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_prep(DB_HANDLE *h, char *sql)
{
    int ret=0;

    check_fork(h, NoCreate);

    if(!finfo)
        return 0;

    /* write sql statement to start of mmap */

    sprintf(finfo->mapinfo->mem,"%s", sql);

    if(forkwrite("p", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    // get the result back
    if(forkread(&ret, sizeof(int)) == -1)
    {
        return 0;
    }

    return ret;
}

static int child_prep()
{
    TEXIS *tx=NULL;
    int ret=0;
    char *sql=finfo->mapinfo->mem;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(!tx)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    ret = texis_prepare(tx, sql);
    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

// return 0 on pipe/fork error
// h->tx set otherwise.
static int fork_open(DB_HANDLE *h)
{
    check_fork(h, Create);

    if(finfo->childpid)
    {
        /* write db string to map */
        sprintf(finfo->mapinfo->mem, "%s", h->db);

        /* write o for open and the string db is in memmap */
        if(forkwrite("o", sizeof(char)) == -1)
            return 0;

        if(forkread(&(h->tx), sizeof(TEXIS *)) == -1)
        {
            return 0;
        }
    }

    return 1;
}

static int child_open()
{
    char *db = finfo->mapinfo->mem;
    TEXIS *tx=NULL;

    tx = texis_open((char *)(db), "PUBLIC", "");

    if(forkwrite(&tx, sizeof(TEXIS *)) == -1)
        return 0;

    return 1;
}

static int fork_close(DB_HANDLE *h)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("c", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    h->tx = NULL;

    return ret;
}

static int child_close()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        tx=TEXIS_CLOSE(tx);
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
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("F", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS*)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_flush()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS*))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret = texis_flush(tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("g", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    if(ret)
    {
        memcpy(countInfo, finfo->mapinfo->mem, sizeof(TXCOUNTINFO));
    }

    return ret;
}

static int child_getCountInfo()
{
    int ret=0;
    TEXIS *tx=NULL;
    TXCOUNTINFO *countInfo = finfo->mapinfo->mem;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret = texis_getCountInfo(tx, countInfo);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_skip(DB_HANDLE *h, int nrows)
{
    check_fork(h, NoCreate);
    int ret=0;

    if(!finfo)
        return 0;

    if(forkwrite("s", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkwrite(&nrows, sizeof(int)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_skip()
{
    int ret=0, nrows=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if (forkread(&nrows, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
        ret = texis_flush_scroll(tx, nrows);
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_resetparams(DB_HANDLE *h)
{
    check_fork(h, NoCreate);
    int ret=1;

    if(!finfo)
        return 0;

    if(forkwrite("r", sizeof(char)) == -1)
        return 0;

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    if(forkread(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

static int child_resetparams()
{
    int ret=0;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS *))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        ret=texis_resetparams(tx);
    }
    else
        ret=0;

    if (forkwrite(&ret, sizeof(int))  == -1)
        return 0;

    return ret;
}

static int fork_sql_set(duk_context *ctx, DB_HANDLE *h, char *errbuf)
{
    check_fork(h, Create);
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

    if(forkwrite(&(h->tx), sizeof(TEXIS *)) == -1)
        return 0;

    size = (int)sz;
    if(forkwrite(&size, sizeof(int)) == -1) {
        return 0;
    }
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
        strncpy(errbuf, finfo->mapinfo->mem, msgbufsz);
    }

    return ret;
}

static int child_set()
{
    int ret=0, bufsz=0;
    int size=-1;
    RPTHR *thr=get_current_thread();
    duk_context *ctx=thr->ctx;
    TEXIS *tx=NULL;

    if (forkread(&tx, sizeof(TEXIS*))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if (forkread(&bufsz, sizeof(int))  == -1)
    {
        forkwrite(&ret, sizeof(int));
        return 0;
    }

    if(tx)
    {
        char errbuf[msgbufsz];
        void *p;

        errbuf[0]='\0';

        /* get js object needed for sql_set to do its stuff */
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx, -1, finfo->mapinfo->mem, (duk_size_t)bufsz);
        duk_cbor_decode(ctx, -1, 0);

        /* do the set in this child */
        ret = sql_set(ctx, tx, errbuf);

        ((char*)finfo->mapinfo->mem)[0] = '\0';//abundance of caution.

        /* this is only filled if ret < 0
           and is error text to be sent back to JS  */
        if( ret < 0 )
            memcpy(finfo->mapinfo->mem, errbuf, msgbufsz);

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
static void do_child_loop(SFI *finfo)
{
    while(1)
    {
        char command='\0';
        int ret = kill(parent_pid,0);

        //if( ret )
        //    exit(0);

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
                ret = child_open();
                break;
            case 'c':
                ret = child_close();
                break;
            case 'p':
                ret = child_prep();
                break;
            case 'e':
                ret = child_exec();
                break;
            case 'f':
                ret = 1;
                child_fetch();
                break;
            case 'r':
                ret = child_resetparams();
                break;
            case 'P':
                ret = child_param();
                break;
            case 'F':
                ret = child_flush();
                break;
            case 's':
                ret = child_skip();
                break;
            case 'g':
                ret = child_getCountInfo();
                break;
            case 'S':
                ret = child_set();
                break;
            case 'C':
                ret = child_create();
                break;
        }
        // do something with ret?
    }
}

static int h_create(const char *db)
{
    RPTHR *thr = get_current_thread();

    if(RPTHR_TEST(thr, RPTHR_FLAG_THR_SAFE))
        return createdb(db);
    return fork_create(db);
}

static int h_end_transaction(DB_HANDLE *h)
{
    mark_handle_available(h);
    return 1;
}

static int h_set(duk_context *ctx, DB_HANDLE *h, char *errbuf)
{
    if(DB_HANDLE_IS(h,DB_FLAG_FORK))
        return fork_sql_set(ctx, h, errbuf);
    return sql_set(ctx, h->tx, errbuf);
}

static int h_flush(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h,DB_FLAG_FORK))
        return fork_flush(h);
    return TEXIS_FLUSH(h->tx);
}

static int h_getCountInfo(DB_HANDLE *h, TXCOUNTINFO *countInfo)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_getCountInfo(h, countInfo);
    return TEXIS_GETCOUNTINFO(h->tx, countInfo);
}

static int h_resetparams(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_resetparams(h);
    return TEXIS_RESETPARAMS(h->tx);
}

static int h_param(DB_HANDLE *h, int pn, void *d, long *dl, int t, int st)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_param(h, pn, d, dl, t, st);
    return TEXIS_PARAM(h->tx, pn, d, dl, t, st);
}

static int h_skip(DB_HANDLE *h, int n)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_skip(h,n);
    return TEXIS_SKIP(h->tx, n);
}

static int h_prep(DB_HANDLE *h, char *sql)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_prep(h, sql);
    return TEXIS_PREP(h->tx, sql);
}

static int h_close(DB_HANDLE *h)
{
    int ret=1;

    if(!h) {
        return 1;
    }

    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        ret=fork_close(h);
    else
        h->tx = TEXIS_CLOSE(h->tx);
    
    h=free_handle(h);

    return ret;
}

static int h_exec(DB_HANDLE *h)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_exec(h);
    return TEXIS_EXEC(h->tx);
}

static FLDLST *h_fetch(DB_HANDLE *h,  int stringsFrom)
{
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
        return fork_fetch(h, stringsFrom);
    return TEXIS_FETCH(h->tx, stringsFrom);
}


/* **************************************************
   Sql.prototype.close
   ************************************************** */
duk_ret_t duk_rp_sql_close(duk_context *ctx)
{
    // Since handles are cached, hard to know what to do here
    // We will just close the first unused handle with same db
    // in order to free up some resources
    const char *db=NULL;
    DB_HANDLE *h=NULL;

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        RP_THROW(ctx, "no database has been opened");
    }

    db = duk_get_string(ctx, -1);
    h=find_available_handle(db, 0);
    h_close(h);

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
   //except maybe 6 params: see https://rampart.dev/docs/rampart-sql.html#exec under "Caveats for Options, maxRows and skipRows"
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
                        duk_pop(ctx);

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
                        // we have ?named parameters
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
        q->max = INT64_MAX;

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
            if( (d - floord) > 0.0 || (d - floord) < 0.0 || \
                floord < (double)INT64_MIN || floord > (double)INT64_MAX)\
            {\
                v = (double *)&d;\
                plen = sizeof(double);\
                in = SQL_C_DOUBLE;\
                out = SQL_DOUBLE;\
            }\
            else\
            {\
                lval = (int64_t) floord;\
                v = (int64_t *)&lval;\
                plen = sizeof(int64_t);\
                in = SQL_C_SBIGINT;\
                out = SQL_BIGINT;\
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
        int64_t lval;
        int in, out;

        duk_get_prop_string(ctx, obj_loc, key);
        if(!duk_is_undefined(ctx, -1))
        {
            push_sql_param;

            /* texis_params is indexed starting at 1 */
            rc = h_param(h, i+1, v, &plen, in, out);
            if (!rc)
                return 0;
        }
        duk_pop(ctx);

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
        int64_t lval;
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

    if( !fl->data[i]  || !fl->ndata[i])
    {
        duk_push_null(ctx);
        return;
    }

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
    int i       = 0,
        rettype = q->rettype;
    uint64_t rown   = 0,
             resmax = q->max;
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
    /* added "rows", "results" to be removed
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx,-3,"results");
    */
    duk_put_prop_string(ctx,-2,"rows");
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
    int i       = 0,
        rettype = q->rettype;
    uint64_t rown   = 0,
             resmax = q->max;
    duk_idx_t callback_idx = q->callback,
              colnames_idx = 0,
              count_idx    =-1;
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

void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx);

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
    uint8_t *field_type=NULL;
    int field_type_size=0;
    duk_idx_t this_idx;

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
    struct sigaction sa = { {0} };

    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    //  signal(SIGUSR2, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        closecsv;
        RP_THROW(ctx, "no database has been opened");
    }
    db = duk_get_string(ctx, -1);

    duk_pop(ctx);

    h = h_open(db);
    if(!h)
        throw_tx_error(ctx, "sql open");

    h_reset_tx_default(ctx, h, this_idx);
    duk_pop(ctx);//this

    tx = h->tx;

    {
        FLDLST *fl;
        char sql[256];

        snprintf(sql, 256, "select NAME, TYPE from SYSCOLUMNS where TBNAME='%s' order by ORDINAL_POSITION;", dcsv.tbname);

        if (!h_prep(h, sql))
        {
            closecsv;
            h_close(h);
            throw_tx_error(ctx, "sql prep");
        }

        if (!h_exec(h))
        {
            closecsv;
            h_close(h);
            throw_tx_error(ctx, "sql exec");
        }

        while((fl = h_fetch(h, -1)))
        {
            /* an array of column names */
            REMALLOC(field_names, (tbcols+2) * sizeof(char*) );

            /* a uint8_t array of column types */
            if (tbcols + 1 > field_type_size)
            {
                field_type_size +=256;
                REMALLOC(field_type, field_type_size * sizeof(uint8_t));
            }

            field_names[tbcols] = strdup(fl->data[0]);

            /* keep track of which fields are counter */
            if(!strncmp(fl->data[1],"counter",7))
                field_type[tbcols] = 1;
            else
                field_type[tbcols] = 0;

            tbcols++;
        }
        /* table doesn't exist? */
        if(!tbcols)
        {
            closecsv;
            h_close(h);
            RP_THROW(ctx, "Table '%s' does not exist.  Table must exist before importing CSV", dcsv.tbname);
        }

        field_names[tbcols] = NULL;

    }

#define fn_cleanup(exc) do { \
    int j=0; \
    if(tbcols){ \
      while(field_names[j]!=NULL) \
          free(field_names[j++]); \
    }\
    if(field_names)free(field_names); \
    if(field_type)free(field_type); \
    if(exc) h_close(h);\
    else h_end_transaction(h);\
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
                            fn_cleanup(1);
                            RP_THROW(ctx, "%s(): array contains '%s', which is not a known column name", func_name,s);
                        }
                    }
                }
                else if(! duk_is_number(ctx, -1))
                {
                    fn_cleanup(1);
                    RP_THROW(ctx, "%s(): array requires an array of Integers/Strings (column numbers/names)", func_name);
                }
                else
                    aval=duk_get_int(ctx, -1);

                duk_pop(ctx);
                if( aval>=ncols )
                {
                    fn_cleanup(1);
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
            long plen, datelong;
            int in=0, out=0, row=0, col=0, intzero=0;
            DDIC *ddic=NULL;
            ft_counter *ctr = NULL;
            LPSTMT lpstmt;

            lpstmt = tx->hstmt;
            if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
                ddic = lpstmt->dbc->ddic;
            else
            {
                throw_tx_error(ctx, "sql open");;
            }

            snprintf(sql, slen, "insert into %s values (", dcsv.tbname);
            for (i=0;i<tbcols-1;i++)
                strcat(sql,"?,");

            strcat(sql,"?);");

            //printf("%s, %d, %d\n", sql, slen, (int)strlen(sql) );

            if (!h_prep(h, sql))
            {
                fn_cleanup(1);
                throw_tx_error(ctx, "sql prep");
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
                                in=SQL_C_SBIGINT;
                                out=SQL_BIGINT;
                                v=(int64_t*)&item.integer;
                                plen=sizeof(int64_t);
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
                                datelong=(long) mktime(t);
                                v = (long*)&datelong;
                                plen=sizeof(long);
                                break;
                            }
                            case nil:
                                in=SQL_C_INTEGER;
                                out=SQL_INTEGER;
                                v=(int*)&intzero;
                                plen=sizeof(int);
                                break;
                        }
                    }
                    else
                    {
                        /* insert texis counter if field is a counter type */
                        if (field_type[col]==1)
                        {
                            ctr = getcounter(ddic);
                            v=ctr;
                            plen=sizeof(ft_counter);
                            in=SQL_C_COUNTER;
                            out=SQL_COUNTER;
                        }
                        else
                        {
                            v=&intzero;
                            plen=sizeof(int);
                            in=SQL_C_INTEGER;
                            out=SQL_INTEGER;
                        }
                    }
                    if( !h_param(h, col+1, v, &plen, in, out))
                    {
                        if(ctr) free(ctr);
                        ctr=NULL;
                        fn_cleanup(1);
                        throw_tx_error(ctx, "sql add parameters");
                    }
                    if(ctr) free(ctr);
                    ctr=NULL;
                }
                if (col<tbcols)
                {
                    v=(long*)&intzero;
                    plen=sizeof(int);
                    in=SQL_C_INTEGER;
                    out=SQL_INTEGER;
                    for(; col<tbcols; col++)
                    {
                        if( !h_param(h, col+1, v, &plen, in, out))
                        {
                            fn_cleanup(1);
                            throw_tx_error(ctx, "sql add parameters");
                        }
                    }
                }

                if (!h_exec(h))
                {
                    fn_cleanup(1);
                    throw_tx_error(ctx, "sql exec");
                }
                if (!h_flush(h))
                {
                    fn_cleanup(1);
                    throw_tx_error(ctx, "sql flush");
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
    fn_cleanup(0);
    duk_rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
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
    DB_HANDLE *h = NULL;
    const char *db;
    duk_idx_t this_idx;
    struct sigaction sa = { {0} };

    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    int nParams=0;
    char *newSql=NULL, **namedSqlParams=NULL, *freeme=NULL;
    //  signal(SIGUSR2, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database has been opened");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx); //db

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
    h = h_open(db);
    if(!h)
        throw_tx_error(ctx, "sql open");

    h_reset_tx_default(ctx, h, this_idx);

//  messes up the count for arg_idx, so just leave it
//    duk_remove(ctx, this_idx); //no longer needed

    tx = h->tx;
    if (!tx)
        throw_tx_error(ctx, "open sql");

    /* PREP */
    if (!h_prep(h, (char *)q->sql))
    {
        h_close(h);
        throw_tx_error(ctx, "sql prep");
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
            h_close(h);
            RP_THROW(ctx, "sql.exec - parameters specified in sql statement, but no corresponding object or array\n");
        }
        if (!duk_rp_add_named_parameters(ctx, h, idx, namedSqlParams, nParams))
        {
            free(namedSqlParams);
            free(freeme);
            h_close(h);
            throw_tx_error(ctx, "sql add parameters");
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
        if (!duk_rp_add_parameters(ctx, h, q->arr_idx))
        {
            h_close(h);
            throw_tx_error(ctx, "sql add parameters");
        }
    }
    else
    {
        h_resetparams(h);
    }

    /* EXEC */
    if (!h_exec(h))
    {
        h_close(h);
        throw_tx_error(ctx, "sql exec");
    }
    /* skip rows using texisapi */
    if (q->skip)
        h_skip(h, q->skip);

    /* callback - return one row per callback */
    if (q->callback > -1)
    {
        int rows = duk_rp_fetchWCallback(ctx, h, q);
        duk_push_int(ctx, rows);
        goto end; /* done with exec() */
    }

    /*  No callback, return all rows in array of objects */
    (void)duk_rp_fetch(ctx, h, q);

end:
    h_end_transaction(h);
    duk_rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
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
        duk_rp_log_error_msg(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_sprintf(ctx, "select %s;", stmt);
    duk_replace(ctx, str_idx);
    duk_rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "rows");
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
        duk_rp_log_error_msg(ctx, "sql.one: No string (sql statement) provided");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_object(ctx);
    duk_push_number(ctx, 1.0);
    duk_put_prop_string(ctx, -2, "maxRows");
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "returnRows");

    if( obj_idx != -1)
        duk_pull(ctx, obj_idx);

    duk_rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "rows");
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


/*
    reset defaults if they've been changed by another javascript sql handle

    if this_idx > -1 - then we use 'this' to grab previously set settings
    and apply them after the reset.

    if this_idx < 0 - then reset is forced, but don't apply any saved settings
*/

void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx)
{
    int handle_no=-1, last_handle_no=-1;

    if(this_idx > -1)
    {
        if( ! duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("handle_no")) )
            RP_THROW(ctx, "internal error getting handle id");
        handle_no = duk_get_int(ctx, -1);
        duk_pop(ctx);
        // see rampart-server.c:initThread - last_handle is set to -2 upon thread ctx creation
        // to force a reset of all settings that may have been set in main_ctx, but not applied to this thread/fork
        if(duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_last_handle_no")))
            last_handle_no = duk_get_int(ctx, -1);
        duk_pop(ctx);
    }

    //if hard reset, or  we've set a setting before and it was made by a different sql handel
    //printf("handle=%d, last=%d\n",handle_no, last_handle_no);
    if( this_idx < 0 || (last_handle_no != -1       &&  last_handle_no != handle_no) )
    {
        char errbuf[msgbufsz];
        int ret;
        if (this_idx > -1) //we reapply old setting
        {
            if (!duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings")) )
            {
                //we have no old settings
                duk_pop(ctx);//undefined
                duk_push_object(ctx); //empty object, reset anyway
            }

        }
        else // just reset, don't apply settings
        {
            duk_push_object(ctx);
        }

        //if empty object, all settings will be reset to default

        // set the reset and settings if any
        ret = h_set(ctx, h, errbuf);

        duk_pop(ctx);// no longer need settings object on stack

        if(ret == -1)
        {
            h_close(h);
            RP_THROW(ctx, "%s", errbuf);
        }
        else if (ret ==-2)
        {
            h_close(h);
            throw_tx_error(ctx, errbuf);
        }
    }
    if(last_handle_no != handle_no)
    {
        //set this javascript sql handle as the last to have applied settings
        duk_push_int(ctx, handle_no);
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_last_handle_no"));
    }
}


duk_ret_t duk_texis_reset(duk_context *ctx)
{
    const char *db;
    DB_HANDLE *h = NULL;

    duk_push_this(ctx); //idx == 0

    //remove saved settings
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("indlist"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist"));

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    h = h_open(db);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }
    h_reset_tx_default(ctx, h, -1);
    h_end_transaction(h);
    return 0;
}

static void sql_normalize_prop(char *prop, const char *dprop)
{
    int i=0;

    strcpy(prop, dprop);

    for(i = 0; prop[i]; i++)
        prop[i] = tolower(prop[i]);

    /* a few aliases */
    if(!strcmp("listexp", prop) || !strcmp("listexpressions", prop))
        strcpy(prop, "lstexp");
    else if (!strcmp("listindextmp", prop) || !strcmp("listindextemp", prop) || !strcmp("lstindextemp", prop))
        strcpy(prop, "lstindextmp");
    else if (!strcmp("deleteindextmp", prop) || !strcmp("deleteindextemp", prop) || !strcmp("delindextemp", prop))
        strcpy(prop, "delindextmp");
    else if (!strcmp("addindextemp", prop))
        strcpy(prop, "addindextmp");
    else if (!strcmp("addexpressions", prop))
        strcpy(prop, "addexp");
    else if (!strcmp("delexpressions", prop) || !strcmp("deleteexpressions", prop))
        strcpy(prop, "delexp");
    else if (!strcmp("keepequivs", prop) || !strcmp("useequivs", prop))
        strcpy(prop, "useequiv");
    else if (!strcmp("equivsfile", prop))
        strcpy(prop, "eqprefix");
    else if (!strcmp("userequivsfile", prop))
        strcpy(prop, "ueqprefix");
    else if (!strcmp ("listnoise",prop))
        strcpy (prop, "lstnoise");
    else if (!strcmp ("listsuffix",prop))
        strcpy (prop, "lstsuffix");
    else if (!strcmp ("listsuffixequivs",prop))
        strcpy (prop, "lstsuffixeqivs");
    else if (!strcmp ("listprefix",prop))
        strcpy (prop, "lstprefix");
    else if (!strcmp ("noiselist",prop))
        strcpy (prop, "noiselst");
    else if (!strcmp ("suffixlist",prop))
        strcpy (prop, "suffixlst");
    else if (!strcmp ("suffixequivslist",prop))
        strcpy (prop, "suffixeqivslst");
    else if (!strcmp ("suffixeqlist",prop))
        strcpy (prop, "suffixeqlst");
    else if (!strcmp ("prefixlist",prop))
        strcpy (prop, "prefixlst");
}


TEXIS *setprop_tx=NULL;

static char *prop_defaults[][2] = {
   {"defaultLike", "like"},
   {"matchMode", "0"},
   {"pRedoPtType", "0"},
   {"textSearchMode", "unicodemulti, ignorecase, ignorewidth, ignorediacritics, expandligatures"},
   {"stringCompareMode", "unicodemulti, respectcase"},
   {"btreeCacheSize", "20"},
   {"ramRows", "0"},
   {"ramLimit", "0"},
   {"bubble", "1"},
   {"ignoreNewList", "0"},
   {"indexWithin", "0xf"},
   {"wildOneWord", "1"},
   {"wildSufMatch", "1"},
   {"alLinearDict", "0"},
   {"indexMinSublen", "2"},
   {"dropWordMode", "0"},
   {"metamorphStrlstMode", "equivlist"},
   /*{"groupbymem", "1"}, produces an error */
   {"minWordLen", "255"},
   {"suffixProc", "1"},
   {"rebuild", "0"},
   {"intersects", "-1"},
   {"hyphenPhrase", "1"},
   {"wordc", "[\\alpha\\']"},
   {"langc", "[\\alpha\\'\\-]"},
   {"withinMode", "word span"},
   {"phrasewordproc", "last"},
   {"defSuffRm", "1"},
   {"eqPrefix", "builtin"},
   {"exactPhrase", "0"},
   /* {"withinProc", "1"}, produces error */
   {"likepProximity", "500"},
   {"likepLeadBias", "500"},
   {"likepOrder", "500"},
   {"likepDocFreq", "500"},
   {"likepTblFreq", "500"},
   {"likepRows", "100"},
   {"likepMode", "1"},
   {"likepAllMatch", "0"},
   {"likepObeyIntersects", "0"},
   {"likepInfThresh", "0"},
   /* {"likepIndexThresh", "-1"}, ??? */
   /*{"indexSpace", ""},
   {"indexBlock", ""}, */
   {"meter", "on"},
/*   {"addExp", ""},
   {"delExp", ""},
   {"addIndexTmp", ""}
   {"delIndexTmp", ""}, */
   {"indexValues", "splitStrlst"},
   {"btreeThreshold", "50"},
   {"maxLinearRows", "1000"},
   {"likerRows", "1000"},
   {"indexAccess", "0"},
   {"indexMmap", "1"},
   {"indexReadBufSz", "64KB"},
   {"indexWriteBufSz", "128KB"},
   {"indexMmapBufSz", "0"},
   {"indexSlurp", "1"},
   {"indexAppend", "1"},
   {"indexWriteSplit", "1"},
   {"indexBtreeExclusive", "1"},
   {"indexVersion", "2"},
   {"mergeFlush", "1"},
   {"tableReadBufSz", "16KB"},
   /*{"tableSpace", ""},*/
   {"dateFmt", ""},
   /*{"timeZone", ""},
   {"locale", ""}, */
   /*{"indirectSpace", ""},*/
   {"triggerMode", "0"},
   {"paramChk", "1"},
   /* {"message", "1"}, segfault */
   {"varcharToStrlstMode", "json"},
   {"strlstToVarcharMode", "json"},
   {"multiValueToMultiRow", "0"},
   {"inMode", "subset"},
   {"hexifyBytes", "0"},
   {"unalignedBufferWarning", "1"},
   {"nullOutputString", "NULL"},
   /* {"validateBtrees", ""}, no idea */
   {"querySettings", "defaults"},
   {"qMaxWords", "1000"},
   {NULL, NULL}
};
int nnoiseList=181;
char *noiseList[] = {
    "a","about","after","again","ago","all","almost","also","always","am",
    "an","and","another","any","anybody","anyhow","anyone","anything","anyway","are",
    "as","at","away","back","be","became","because","been","before","being",
    "between","but","by","came","can","cannot","come","could","did","do",
    "does","doing","done","down","each","else","even","ever","every","everyone",
    "everything","for","from","front","get","getting","go","goes","going","gone",
    "got","gotten","had","has","have","having","he","her","here","him",
    "his","how","i","if","in","into","is","isn't","it","just",
    "last","least","left","less","let","like","make","many","may","maybe",
    "me","mine","more","most","much","my","myself","never","no","none",
    "not","now","of","off","on","one","onto","or","our","ourselves",
    "out","over","per","put","putting","same","saw","see","seen","shall",
    "she","should","so","some","somebody","someone","something","stand","such","sure",
    "take","than","that","the","their","them","then","there","these","they",
    "this","those","through","till","to","too","two","unless","until","up",
    "upon","us","very","was","we","went","were","what","what's","whatever",
    "when","where","whether","which","while","who","whoever","whom","whose","why",
    "will","with","within","without","won't","would","wouldn't","yet","you","your", ""
};
int nsuffixList=91;

char *suffixList[] = {
    "'","anced","ancer","ances","atery","enced","encer","ences","ibler","ment",
    "ness","tion","able","less","sion","ance","ious","ible","ence","ship",
    "ical","ward","ally","atic","aged","ager","ages","ated","ater","ates",
    "iced","icer","ices","ided","ider","ides","ised","ises","ived","ives",
    "ized","izer","izes","ncy","ing","ion","ity","ous","ful","tic",
    "ish","ial","ory","ism","age","ist","ate","ary","ual","ize",
    "ide","ive","ier","ess","ant","ise","ily","ice","ery","ent",
    "end","ics","est","ed","red","res","ly","er","al","at",
    "ic","ty","ry","en","nt","re","th","es","ul","s", ""
};

int nsuffixEquivsList=4;
char *suffixEquivsList[] = {
    "'","s","ies", ""
};

int nprefixList=29;
char *prefixList[] = {
    "ante","anti","arch","auto","be","bi","counter","de","dis","em",
    "en","ex","extra","fore","hyper","in","inter","mis","non","post",
    "pre","pro","re","semi","sub","super","ultra","un", ""
};

char **copylist(char **list, int len){
    int i=0;

    char **nl=NULL; /* the list to be populated */

    REMALLOC(nl, sizeof(char*) * len);

    while (i<len)
    {
        nl[i]=strdup(list[i]);
        i++;
    }

    return nl;
}

static int sql_defaults(duk_context *ctx, TEXIS *tx, char *errbuf)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;

    int i=0;
    char **props;

    clearmsgbuf();

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        sprintf(errbuf,"sql open");
        return -1;
    }

    props = prop_defaults[i];
    SETLOCK
    while (props[0])
    {
        if(setprop(ddic, props[0], props[1] )==-1)
        {
            sprintf(errbuf, "sql reset");
            SETUNLOCK
            return -2;
        }
        i++;
        props = prop_defaults[i];
    }
    SETUNLOCK
    if(!defnoise)
    {
        globalcp->noise=(byte**)copylist(noiseList, nnoiseList);
        defnoise=1;
    }
    if(!defsuffix)
    {
        globalcp->suffix=(byte**)copylist(suffixList, nsuffixList);
        defsuffix=1;
    }
    if(!defsuffixeq)
    {
        globalcp->suffixeq=(byte**)copylist(suffixEquivsList, nsuffixEquivsList);
        defsuffixeq=1;
    }
    if(!defprefix)
    {
        globalcp->prefix=(byte**)copylist(prefixList, nprefixList);
        defprefix=1;
    }

    return 0;
}

// returns -1 for bad option, -2 for setprop error, 0 for ok, 1 for ok with return value
static int sql_set(duk_context *ctx, TEXIS *tx, char *errbuf)
{
    LPSTMT lpstmt;
    DDIC *ddic=NULL;
    const char *val="";
    int added_ret_obj=0, ret=0;
    char *rlsts[]={"noiseList","suffixList","suffixEquivsList","prefixList"};

    clearmsgbuf();

    if(!tx)
    {
        duk_rp_log_error(ctx);
        snprintf(errbuf, msgbufsz, "Texis setprop failed\n%s", finfo->errmap);
        goto return_neg_two;
    }

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        sprintf(errbuf,"sql open");
        goto return_neg_two;
    }

    if((ret=sql_defaults(ctx, tx, errbuf)))
        return ret;

    /**** Reapply indextmplst and explst if it was modified */
    duk_push_this(ctx);
    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("indlist")))
    {
        int i=0, len = duk_get_length(ctx, -1);
        const char *val;

        SETLOCK
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            if(setprop(ddic, "addindextmp", (char*)val )==-1)
            {
                sprintf(errbuf, "sql set");
                SETUNLOCK
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
        SETUNLOCK
    }
    duk_pop(ctx);//list or undef

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist")))
    {
        int i=0, len = duk_get_length(ctx, -1);
        const char *val;

        /* delete first entry for an empty list */
        SETLOCK
        if(setprop(ddic, "delexp", "0" )==-1)
        {
            SETUNLOCK
            sprintf(errbuf, "sql set");
            goto return_neg_two;
        }
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            if(setprop(ddic, "addexp", (char*)val )==-1)
            {
                sprintf(errbuf, "sql set");
                SETUNLOCK
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
        SETUNLOCK
    }
    duk_pop_2(ctx);// list and this

    // apply all settings in object
    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        /*
        int retlisttype=-1, setlisttype=-1, i=0;
        char propa[64], *prop=&propa[0];
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
        {
            sprintf(errbuf, "sql.set - '%s' - unknown/invalid property", dprop);
            goto return_neg_one;
        }

        sql_normalize_prop(prop, dprop);
        */
        const char *prop=duk_get_string(ctx, -2);
        int retlisttype=-1, setlisttype=-1;

        if (!strcmp ("lstnoise", prop))
            retlisttype=0;
        else if (!strcmp ("lstsuffix", prop))
            retlisttype=1;
        else if (!strcmp ("lstsuffixeqivs", prop))
            retlisttype=2;
        else if (!strcmp ("lstprefix", prop))
            retlisttype=3;
        else if (!strcmp ("noiselst", prop))
            setlisttype=0;
        else if (!strcmp ("suffixlst", prop))
            setlisttype=1;
        else if (!strcmp ("suffixeqivslst", prop))
            setlisttype=2;
        else if (!strcmp ("suffixeqlst", prop))
            setlisttype=2;
        else if (!strcmp ("prefixlst", prop))
            setlisttype=3;

        if( (!strcmp(prop, "lstexp")||!strcmp(prop, "lstindextmp")))
        {
            char **lst;
            int arryi=0;
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    goto propnext;
            }
            else
                RP_THROW(ctx, "sql.set - property '%s' requires a Boolean", prop);

            if(!added_ret_obj)
            {
                duk_push_object(ctx);
                duk_insert(ctx, 0);
                added_ret_obj=1;
            }

            duk_push_array(ctx);

            if (!strcmp(prop, "lstexp"))
                lst=TXgetglobalexp();
            else
                lst=TXgetglobalindextmp();

            while (lst[arryi] && strlen(lst[arryi]))
            {
                duk_push_string(ctx, lst[arryi]);
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)arryi);
                arryi++;
            }

            duk_put_prop_string(ctx, 0,
                (
                    strcmp(prop, "lstindextmp")?"expressionsList":"indexTempList"
                )
            );

            goto propnext;
        }

        if(retlisttype>-1)
        {
            byte *nw;
            byte **lsts[]={globalcp->noise,globalcp->suffix,globalcp->suffixeq,globalcp->prefix};
            char *rprop=rlsts[retlisttype];
            byte **lst=lsts[retlisttype];
            int i=0;

            /* skip if false */
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    goto propnext;
            }
            else
                RP_THROW(ctx, "sql.set - property '%s' requires a Boolean", prop);

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
                REMALLOC(nl, sizeof(char*) * 1);
                nl[0]=strdup("");
            }
            else if(duk_is_array(ctx, -1))
            {
                int len=duk_get_length(ctx, -1), i=0;

                REMALLOC(nl, sizeof(char*) * (len + 1));

                while (i<len)
                {
                    duk_get_prop_index(ctx, -1, i);
                    if(!(duk_is_string(ctx, -1)))
                    {
                        /* note that the RP_THROW below might be caught in js, so we need to clean up *
                         * terminate what we have so far, then free it                                */
                        nl[i]=strdup("");
                        free_list((char**)nl);
                        snprintf(errbuf, msgbufsz, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                        goto return_neg_one;
                    }
                    nl[i]=strdup(duk_get_string(ctx, -1));
                    duk_pop(ctx);
                    i++;
                }
                nl[i]=strdup("");
            }
            else
            {
                snprintf(errbuf, msgbufsz, "sql.set: %s must be an array of strings", rlsts[setlisttype] );
                goto return_neg_one;
            }
            SETLOCK
            switch(setlisttype)
            {
                case 0: free_list((char**)globalcp->noise);
                        globalcp->noise=(byte**)nl;
                        defnoise=0;
                        break;

                case 1: free_list((char**)globalcp->suffix);
                        globalcp->suffix=(byte**)nl;
                        defsuffix=0;
                        break;

                case 2: free_list((char**)globalcp->suffixeq);
                        globalcp->suffixeq=(byte**)nl;
                        defsuffixeq=0;
                        break;

                case 3: free_list((char**)globalcp->prefix);
                        globalcp->prefix=(byte**)nl;
                        defprefix=0;
                        break;
            }
            SETUNLOCK
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
                            goto return_neg_one;
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
                        goto return_neg_one;
                    }
                }
                else if (ptype==0)
                {
                    aval=get_exp(ctx, -1);

                    if(!aval)
                    {
                        sprintf(errbuf, "sql.set: addExpressions - array must be an array of strings or expressions\n");
                        goto return_neg_one;
                    }
                }
                else
                {
                    if(!duk_is_string(ctx, -1))
                    {
                        sprintf(errbuf, "sql.set: addIndexTemp - array must be an array of strings\n");
                        goto return_neg_one;
                    }
                    aval=duk_get_string(ctx, -1);
                }
                SETLOCK
                if(setprop(ddic, (char*)prop, (char*)aval )==-1)
                {
                    sprintf(errbuf, "sql set");
                    SETUNLOCK
                    goto return_neg_two;
                }
                SETUNLOCK
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
                    snprintf(errbuf, msgbufsz, "invalid value '%s'", duk_safe_to_string(ctx, -1));
                    goto return_neg_one;
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
            SETLOCK
            if(setprop(ddic, (char*)prop, (char*)val )==-1)
            {
                sprintf(errbuf, "sql set");
                SETUNLOCK
                goto return_neg_two;
            }
            SETUNLOCK
        }

        /* save the altered list for reapplication after reset */
        if( !strcmp(prop, "addexp")||!strcmp(prop, "addindextmp") ||
            !strcmp(prop, "delexp")||!strcmp(prop, "delindextmp")
          )
        {
            char **lst;
            int arryi=0;
            char type = prop[3]; // i for index, e for expression

            duk_push_this(ctx);
            duk_push_array(ctx);
            if (type == 'e')
                lst=TXgetglobalexp();
            else
                lst=TXgetglobalindextmp();

            while (lst[arryi] && strlen(lst[arryi]))
            {
                duk_push_string(ctx, lst[arryi]);
                duk_put_prop_index(ctx, -2, (duk_uarridx_t)arryi);
                arryi++;
            }
            if (type == 'e')
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("explist"));
            else
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("indlist"));

            duk_pop(ctx);//this
        }

        propnext:
        duk_pop_2(ctx);
    }
    duk_pop(ctx);//enum

    duk_rp_log_error(ctx); /* log any non fatal errors to this.errMsg */

    //tx=texis_close(tx);

    if(added_ret_obj)
    {
        duk_pull(ctx, 0);
        return 1;
    }
    return 0;

    return_neg_two:
        //tx=texis_close(tx);
        return -2;

    return_neg_one:
        //tx=texis_close(tx);
        return -1;
}

/*
static char *stringLower(const char *str)
{
    size_t len = strlen(str);
    char *lower = NULL;
    int i=0;

    REMALLOC(lower, len+1);
    for (; i < len; ++i) {
        lower[i] = tolower((unsigned char)str[i]);
    }
    lower[i] = '\0';
    return lower;
}
*/

// certain settings like lstexp and addexp should not remain in saved settings
static void clean_settings(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings"));
    duk_remove(ctx, -2);
    duk_del_prop_string(ctx, -1, "lstexp");
    duk_del_prop_string(ctx, -1, "delexp");
    duk_del_prop_string(ctx, -1, "addexp");
    duk_del_prop_string(ctx, -1, "lstindextmp");
    duk_del_prop_string(ctx, -1, "delindextmp");
    duk_del_prop_string(ctx, -1, "addindextmp");
    duk_del_prop_string(ctx, -1, "lstnoise");
    duk_del_prop_string(ctx, -1, "lstsuffix");
    duk_del_prop_string(ctx, -1, "lstsuffixeqivs");
    duk_del_prop_string(ctx, -1, "lstprefix");
    duk_pop(ctx);//the settings list object
}

duk_ret_t duk_texis_set(duk_context *ctx)
{
    const char *db;
    DB_HANDLE *h = NULL;
    int ret = 0, handle_no=0;
    char errbuf[msgbufsz];
    char propa[64], *prop=&propa[0];

    duk_push_this(ctx); //idx == 1
    // this should always be true
    if( !duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("handle_no")) )
        RP_THROW(ctx, "internal error getting handle id");
    handle_no=duk_get_int(ctx, -1);
    duk_pop(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx);

    h = h_open(db);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }

    h_reset_tx_default(ctx, h, -1);

    clearmsgbuf();

    if(!duk_is_object(ctx, 0) || duk_is_array(ctx, 0) || duk_is_function(ctx, 0) )
        RP_THROW(ctx, "sql.set() - object with {prop:value} expected as parameter - got '%s'",duk_safe_to_string(ctx, 0));

    //stack = [ settings_obj, this ]
    //get any previous settings from this
    if(! duk_get_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("sql_settings")) )
    {
        duk_pop(ctx);   // pop undefined,
        duk_push_object(ctx); // [ settings_obj, this, new_empty_old_settings ]
    }
                                              // [ settings_obj, this, old_settings ]
    /* copy properties, renamed as lowercase, into saved old settings */
    duk_enum(ctx, 0, 0);                      // [ settings_obj, this, old_settings, enum_obj ]
    while (duk_next(ctx, -1, 1))
    {
        //  inside loop -                        [ settings_obj, this , old_settings, enum_obj, key, val ]
        duk_size_t sz;
        const char *dprop=duk_get_lstring(ctx, -2, &sz);

        if(sz>63)
            RP_THROW(ctx, "sql.set - '%s' - unknown/invalid property", dprop);

        sql_normalize_prop(prop, dprop);

        // put val into old settings, overwriting old if exists
        duk_put_prop_string(ctx, 2, prop);    // [ settings_obj, this , old_settings, enum_obj, key ]
        duk_pop(ctx);                         // [ settings_obj, this , old_settings, enum_obj ]
    }
    duk_pop(ctx);                             // [ settings_obj, this, combined_settings ]
    duk_remove(ctx, 0);                       // [ this , combined_settings ]

    duk_dup(ctx, -1);                         // [ this , combined_settings, combined_settings ]
    duk_put_prop_string(ctx, 0,
      DUK_HIDDEN_SYMBOL("sql_settings"));     // [ this, combined_settings ]
    duk_remove(ctx, 0);                       // [ combined_settings ]

    /* going to a child proc */
    if(DB_HANDLE_IS(h, DB_FLAG_FORK))
    {
        // regular expressions in addexp don't survive cbor,
        // so get the source expression and replace in array
        // before sending to child
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
        ret = fork_sql_set(ctx, h, errbuf);
    }
    else // handled by this proc
    {
        ret = sql_set(ctx, h->tx, errbuf);
    }


    if(ret == -1)
    {
        h_close(h);
        RP_THROW(ctx, "%s", errbuf);
    }
    
    else if (ret ==-2)
    {
        h_close(h);
        throw_tx_error(ctx, errbuf);
    }

    h_end_transaction(h);

    //store which sql object last made a settings change
    duk_push_int(ctx, handle_no);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_last_handle_no"));

    clean_settings(ctx);
    return (duk_ret_t) ret;
}

/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   ************************************************** */
duk_ret_t duk_rp_sql_constructor(duk_context *ctx)
{
    int sql_handle_no = 0;
    const char *db = REQUIRE_STRING(ctx, 0, "new Sql - first parameter must be a string (database path)");
    DB_HANDLE *h;

    /* allow call to Sql() with "new Sql()" only */
    if (!duk_is_constructor_call(ctx))
    {
        return DUK_RET_TYPE_ERROR;
    }

    /* with require_string above, this shouldn't happen */
    if(!db)
        RP_THROW(ctx,"new Sql - db is null\n");

    clearmsgbuf();

    /* check for db first */
    h = h_open( (char*)db);
    if (!h)
    {
        /*
         if sql=new Sql("/db/path",true), we will
         create the db if it does not exist
        */
        if (duk_is_boolean(ctx, 1) && duk_get_boolean(ctx, 1) != 0)
        {
            clearmsgbuf();
            if (!h_create(db))
            {
                duk_rp_log_error(ctx);
                RP_THROW(ctx, "cannot open or create database at '%s' - root path not found, lacking permission or other error\n%s", db, finfo->errmap);
            }
        }
        else
        {
            duk_rp_log_error(ctx);
            RP_THROW(ctx, "cannot open database at '%s'\n%s", db, finfo->errmap);
        }
        h = h_open( (char*)db);
    }
    duk_rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
    h_end_transaction(h);

    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, "db");
    duk_push_number(ctx, RESMAX_DEFAULT);
    duk_put_prop_string(ctx, -2, "selectMaxRows");

    /* make a unique id for this sql handle in this ctx/thread.
     * Used to restore global texis settings previously set via
     * sql.set() so they act as if they are tied to the sql handle */
    if (duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_handle_counter") ) )
    {
        sql_handle_no=duk_get_int(ctx, -1);
    }
    duk_pop(ctx);

    sql_handle_no++;

    // attach number to this
    duk_push_int(ctx, sql_handle_no);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("handle_no") );

    //update last number
    duk_push_int(ctx, sql_handle_no);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_handle_counter"));
    //end unique id for sql handle

    //currently unused, probably can be removed:
    SET_THREAD_UNSAFE(ctx);

    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    return 0;
}

/* **************************************************
   Initialize Sql module
   ************************************************** */
char install_dir[PATH_MAX+21];
duk_ret_t duk_rp_exec(duk_context *ctx);

duk_ret_t duk_open_module(duk_context *ctx)
{
    /* Set up locks:
     * this will be run once per new duk_context/thread in server.c
     * but needs to be done only once for all threads
     */
    CTXLOCK;
    if (!db_is_init)
    {
        char *TexisArgv[2];

        RP_PTINIT(&tx_handle_lock);
        RP_PTINIT(&tx_set_lock);

        TexisArgv[0]=rampart_exec;

        // To help find texislockd -- which might be only in the same dir
        // as 'rampart' executable.
        strcpy (install_dir, "--install-dir-force=");
        strcat (install_dir, rampart_bin);
        TexisArgv[1]=install_dir;

        if( TXinitapp(NULL, NULL, 2, TexisArgv, NULL, NULL) )
        {
            CTXUNLOCK;
            RP_THROW(ctx, "Failed to initialize rampart-sql in TXinitapp");
        }
        errmap0=NULL;
        REMALLOC(errmap0, msgbufsz);
        mmsgfh = fmemopen(errmap0, msgbufsz, "w+");

        db_is_init = 1;
    }
    CTXUNLOCK;

    duk_push_object(ctx); // the return object

    duk_push_c_function(ctx, duk_rp_sql_constructor, 3 /*nargs*/);

    /* Push proto object that will be Sql.init.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Sql protoObj ] */

    /* Set Sql.init.prototype.exec. */
    duk_push_c_function(ctx, duk_rp_sql_exec, 6 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "exec");                    /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* set Sql.init.prototype.eval */
    duk_push_c_function(ctx, duk_rp_sql_eval, 4 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "eval");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.init.prototype.eval */
    duk_push_c_function(ctx, duk_rp_sql_one, 2 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "one");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.init.prototype.close */
    duk_push_c_function(ctx, duk_rp_sql_close, 0 /*nargs*/); /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_close ] */
    duk_put_prop_string(ctx, -2, "close");                   /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close} ] */

    /* set Sql.init.prototype.set */
    duk_push_c_function(ctx, duk_texis_set, 1 /*nargs*/);   /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_set ] */
    duk_put_prop_string(ctx, -2, "set");                    /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close,set:fn_set} ] */

    /* set Sql.init.prototype.reset */
    duk_push_c_function(ctx, duk_texis_reset, 0);
    duk_put_prop_string(ctx, -2, "reset");

    /* set Sql.init.prototype.importCsvFile */
    duk_push_c_function(ctx, duk_rp_sql_import_csv_file, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsvFile");

    /* set Sql.init.prototype.importCsv */
    duk_push_c_function(ctx, duk_rp_sql_import_csv_str, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsv");

    /* Set Sql.init.prototype = protoObj */
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

    duk_push_c_function(ctx, searchtext, 3);
    duk_put_prop_string(ctx, -2, "searchText");

    add_exit_func(free_all_handles, NULL);

    return 1;
}
