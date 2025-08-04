/* Copyright (C) 2025  Aaron Flin - All Rights Reserved
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
#include <time.h>
#include "dbquery.h"
#include "texint.h"
#include "texisapi.h"
#include "cgi.h"
#include "rampart.h"
#include "api3.h"
#include "../globals/csv_parser.h"
#include "event.h"

static pthread_mutex_t tx_handle_lock;

static int defnoise=1, defsuffix=1, defsuffixeq=1, defprefix=1;

#define RESMAX_DEFAULT 10 /* default number of sql rows returned for select statements if max is not set */

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


static duk_ret_t rp_sql_close(duk_context *ctx);

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
    pid_t childpid;     // process id of the child if in parent (return from fork)
    FMINFO *mapinfo;    // the shared mmap for reading and writing in both parent and child
    char *errmap;       // the shared mmap for errors
    int mapfd;          // file descriptor of mapinfo->map mmem
    int errfd;          // file descriptor of error map mmem
    void *aux;          // if data is larger than mapsize, we need to copy it in chunks into here
    void *auxpos;
    size_t auxsz;
    FLDLST *fl;         // modified FLDLST for parent pointing to areas in mapinfo
};

// info for thread/fork pairing as a thread local
__thread SFI *finfo = NULL;

// address from duk_get_heapptr of the last sql to have its settings applied
__thread void *last_sql_set = NULL;

// some string functions don't fork.  We need an error map for them
char *errmap0;

extern int RP_TX_isforked;

// lock handle list while operating from multiple threads
#define HLOCK RP_PTLOCK(&tx_handle_lock);
#define HUNLOCK RP_PTUNLOCK(&tx_handle_lock);

#define DB_HANDLE struct db_handle_s_list
DB_HANDLE
{
    TEXIS *tx;                  // a texis handle, NULL if forking
    char *db;                   // the db path.
    char *user;                 // db user
    char *pass;                 // db pass
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

static DB_HANDLE *new_handle(const char *db, const char *user, const char *pass)
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
    h->user=strdup(user);
    h->pass=strdup(pass);

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
static DB_HANDLE *find_available_handle(const char *db, const char *user, const char *pass, int in_use)
{
    DB_HANDLE *h=db_handle_available_head;

    //while not at end of list, and no match
    while(h && ( strcmp(h->db, db)!=0 || strcmp(h->user, user)!=0 || strcmp(h->pass, pass)!=0) )
        h=h->next;

    if(h && in_use)
        mark_handle_in_use(h);

    return h;
}


static DB_HANDLE * free_handle(DB_HANDLE *h)
{
    remove_handle(h);
    if(h->db)
        free(h->db);
    if(h->user)
        free(h->user);
    if(h->pass)
        free(h->pass);
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

#define TXLOCK /* currently unused */
#define TXUNLOCK /* currently unused */


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

static void die_nicely(int sig)
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
    rp_log_error(ctx);\
    RP_THROW(ctx, "%s error: %s",pref, finfo->errmap);\
}while(0)

#define throw_tx_error_close(ctx,pref,h) do{\
    rp_log_error(ctx);\
    duk_push_string(ctx, finfo->errmap);\
    h_close(h);\
    RP_THROW(ctx, "%s error: %s",pref, duk_get_string(ctx,-1));\
}while(0)


#define clearmsgbuf() do {                \
    fseek(mmsgfh, 0, SEEK_SET);           \
    if(finfo && finfo->errmap)            \
        finfo->errmap[0]='\0';            \
    else {                                \
        fwrite("\0", 1, 1, mmsgfh);       \
        fseek(mmsgfh, 0, SEEK_SET);       \
    }                                     \
} while(0)


/* **************************************************
     store an error string in this.errMsg
   **************************************************   */
static void rp_log_copy_to_errMsg(duk_context *ctx, char *msg)
{
    duk_push_this(ctx);
    duk_push_string(ctx, msg);
    duk_put_prop_string(ctx, -2, "errMsg");
    duk_pop(ctx);
}

static int rp_log_error(duk_context *ctx)
{
    if(RP_TX_isforked)
    {
        int pos=ftell(mmsgfh);
        if(pos>msgbufsz-1)
            pos=msgbufsz-1;

        if(pos && finfo->errmap[pos-1]=='\n')
            pos--;
        finfo->errmap[pos]='\0';
            return 0;
    }
    else
    {
        int pos = strlen(finfo->errmap);
        if(pos && finfo->errmap[pos-1]=='\n')
            finfo->errmap[pos-1]='\0';
    }
    rp_log_copy_to_errMsg(ctx, finfo->errmap);
    return (int)finfo->errmap[0];  // 0 if strlen == 0
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

#define forkwrite(b,c) ({\
    int r=0,ir=0;\
    while( (r += (ir=write(finfo->writer, (b)+r, (c)-r))) < (c) ) if(ir<1)break;\
    if(ir<1) {\
        fprintf(stderr, "rampart-sql helper: write failed: '%s' at %d, fd:%d\n",strerror(errno),__LINE__,finfo->writer);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    r;\
})

#define forkread(b,c) ({\
    int r=0,ir=0;\
    while( (r += (ir=read(finfo->reader, (b)+r, (c)-r))) < (c) ) if(ir<1)break;\
    if(ir==-1) {\
        fprintf(stderr, "rampart-sql helper: read failed from %d: '%s' at %d\n",finfo->reader,strerror(errno),__LINE__);\
        if(thisfork) {fprintf(stderr, "child proc exiting\n");exit(0);}\
    };\
    if(r!=(int)c) {\
        if(errno) \
            fprintf(stderr, "rampart-sql helper: r=%d, read failed from %d: '%s' at %d\n",r,finfo->reader, strerror(errno),__LINE__);\
        if(thisfork) {if(errno) fprintf(stderr, "child proc exiting\n");exit(0);}\
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
        forkwrite("X", sizeof(char));

        if(finfo->reader != -1)
        {
            close(finfo->reader);
            finfo->reader=-1;
        }
        if(finfo->writer != -1)
        {
            close(finfo->writer);
            finfo->writer=-1;
        }
        if(finfo->mapfd != -1)
        {
            close(finfo->mapfd);
            finfo->mapfd=-1;
        }
        if(finfo->errfd != -1)
        {
            close(finfo->errfd);
            finfo->errfd=-1;
        }
        if(finfo->mapinfo)
        {
            if(finfo->mapinfo->mem)
            {
                if(munmap(finfo->mapinfo->mem, FORKMAPSIZE) != 0)
                    fprintf(stderr, "error unmapping mapinfo->mem at %s:%d - %s\n", __FILE__,__LINE__,strerror(errno));
            }
            free(finfo->mapinfo);
        }

        if(finfo->errmap)
        {
            if(munmap(finfo->errmap, msgbufsz) != 0)
                fprintf(stderr, "error unmapping errmap at %s:%d - %s\n", __FILE__,__LINE__,strerror(errno));
        }

        if(finfo->aux)
            free(finfo->aux);
        if(finfo->fl)
            free(finfo->fl);

        free(finfo);
        finfo=NULL;
    }

    kill(*kpid,SIGTERM);
}

static int rp_memfd_create(size_t size, int type) {
    char shm_name[NAME_MAX];
    int fd;
    int id=get_thread_num();

    // Generate a unique shared memory object name rpmem-pid-thrno-type
    snprintf(shm_name, NAME_MAX, "/rpmem-%d-%d-%d", getpid(), id, type);

    // Create the shared memory object
    fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        perror("shm_open");
        return -1;
    }

    // Unlink the shared memory object to make it anonymous
    if (shm_unlink(shm_name) == -1) {
        perror("shm_unlink");
        close(fd);
        return -1;
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        close(fd);
        return -1;
    }

    // Clear the FD_CLOEXEC flag
    int flags_fd = fcntl(fd, F_GETFD);
    if (flags_fd == -1) {
        perror("fcntl F_GETFD");
        close(fd);
        return -1;
    }

    flags_fd &= ~FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags_fd) == -1) {
        perror("fcntl F_SETFD");
        close(fd);
        return -1;
    }

    return fd;
}

static void do_child_loop(SFI *finfo);
static int fork_setmem();
static int fork_seterr();

#define Create 1
#define NoCreate 0

#define MEMMAP 0
#define ERRMAP 1

static char *scr_txt = "var S=require('rampart-sql.so');S.__helper(%d,%d,%d);\n";

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

            finfo->mapfd=rp_memfd_create(FORKMAPSIZE, MEMMAP);
            if(finfo->mapfd == -1)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                exit(1);
            }

            finfo->mapinfo->mem = mmap(NULL, FORKMAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->mapfd, 0);
            if(finfo->mapinfo->mem == MAP_FAILED)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                exit(1);
            }
            finfo->mapinfo->pos = finfo->mapinfo->mem;

            finfo->errfd=rp_memfd_create(msgbufsz, ERRMAP);
            if(finfo->errfd == -1)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
                exit(1);
            }

            finfo->errmap = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->errfd, 0);
            if(finfo->errmap == MAP_FAILED)
            {
                fprintf(stderr, "mmap failed (%d): %s\n",__LINE__, strerror(errno));
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
        finfo->childpid = fork();

        if (finfo->childpid < 0)
        {
            fprintf(stderr, "fork failed");
            finfo->childpid = 0;
            return NULL;
        }

        if(finfo->childpid == 0)
        { /* child is forked once then talks over pipes. */
            char script[1024];

            close(child2par[0]);
            close(par2child[1]);

            sprintf(script, scr_txt, par2child[0], child2par[1], h->forknum);
            execl(rampart_exec, rampart_exec, "-c", script, NULL);
            exit(0);
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

            if(!fork_setmem())
            {
                free(finfo);
                return NULL;
            }

            if(!fork_seterr())
            {
                free(finfo);
                return NULL;
            }
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

static int fork_open(DB_HANDLE *h, const char *user, const char *pass);

/* find first unused handle, create as necessary */
static DB_HANDLE *h_open(const char *db, const char *user, const char *pass)
{
    DB_HANDLE *h = NULL;

    h=find_available_handle(db, user, pass, DBH_MARK_IN_USE);

    if(!h)
    {
        h=new_handle(db, user, pass);
        add_handle(h); //handle added to main list
        if( DB_HANDLE_IS(h, DB_FLAG_FORK) )
        {
            //  if pipe error
            if( !fork_open(h,user,pass) )
            {
                HLOCK
                h=free_handle(h); //h==NULL
                HUNLOCK
            }
        }
        else
        {
            h->tx=texis_open((char *)(db), (char*)user, (char*)pass);
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
    //printf("%d: texis_prepare(%p, \"%s\")\n", (int)getpid(),tx, sql);
    ret = texis_prepare(tx, sql);

    if(forkwrite(&ret, sizeof(int)) == -1)
        return 0;

    return ret;
}

// return 0 on pipe/fork error
// h->tx set otherwise.
static int fork_open(DB_HANDLE *h, const char *user, const char *pass)
{
    check_fork(h, Create);

    if(finfo->childpid)
    {
        /* write db string to map */
        sprintf(finfo->mapinfo->mem, "%s%c%s%c%s", h->db, 0, user, 0, pass);

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
    char *db = finfo->mapinfo->mem, *user, *pass;
    TEXIS *tx=NULL;

    user = db + strlen(db) + 1;
    pass = user + strlen(user) + 1;

    tx = texis_open((char *)(db), user, pass);


    if(forkwrite(&tx, sizeof(TEXIS *)) == -1)
        return 0;

    return 1;
}

static int fork_setmem()
{
    if(finfo->childpid)
    {
        if(forkwrite("M", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&(finfo->mapfd), sizeof(int)) == -1)
            return 0;

    }
    else
        return 0;

    return 1;
}

static int child_setmem()
{
    if(forkread(&(finfo->mapfd), sizeof(int)) ==-1)
        return 0;
    finfo->mapinfo->mem = mmap(NULL, FORKMAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->mapfd, 0);
    if(finfo->mapinfo->mem == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed: %s\n",strerror(errno));
        return 0;
    }
    finfo->mapinfo->pos = finfo->mapinfo->mem;

    return 1;
}


static int fork_seterr()
{
    if(finfo->childpid)
    {
        if(forkwrite("E", sizeof(char)) == -1)
            return 0;

        if(forkwrite(&(finfo->errfd), sizeof(int)) == -1)
            return 0;

    }
    else
        return 0;

    return 1;
}

static int child_seterr()
{
    if(forkread(&(finfo->errfd), sizeof(int)) ==-1)
        return 0;

    finfo->errmap = mmap(NULL, msgbufsz, PROT_READ|PROT_WRITE, MAP_SHARED, finfo->errfd, 0);
    if(finfo->errmap == MAP_FAILED)
    {
        fprintf(stderr, "errmsg mmap failed: %s\n",strerror(errno));
        exit(1);
    }

    mmsgfh = fmemopen(finfo->errmap, msgbufsz, "w+");

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
        int ret;// = kill(parent_pid,0);

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
            case 'M':
                ret = child_setmem();
                break;
            case 'E':
                ret = child_seterr();
                break;
            case 'X':
                exit(0);
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
static duk_ret_t rp_sql_close(duk_context *ctx)
{
    // Since handles are cached, hard to know what to do here
    // We will just close the first unused handle with same db
    // in order to free up some resources
    const char *user="PUBLIC",
               *pass="",
               *db=NULL;

    DB_HANDLE *h=NULL;

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        RP_THROW(ctx, "no database has been opened");
    }

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    db = duk_get_string(ctx, -1);
    h=find_available_handle(db, user, pass, 0);
    h_close(h);

    return 0;
}



#include "db_misc.c" /* copied and altered thunderstone code for stringformat and abstract */

/* **************************************************
    initialize query struct
   ************************************************** */
static void rp_init_qstruct(QUERY_STRUCT *q)
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

static QUERY_STRUCT rp_get_query(duk_context *ctx)
{
    duk_idx_t i = 0;
    int gotsettings=0, maxset=0, selectmax=-432100000;
    QUERY_STRUCT q_st;
    QUERY_STRUCT *q = &q_st;

    rp_init_qstruct(q);

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
                //int l;
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
                /*  Done in parse_sql_params now -- ajf 2025-07-27
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
                */
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

    // we are only doing objects now -ajf 2025-07-27
    if(q->arr_idx >-1 && q->obj_idx==-1)
    {
        duk_idx_t idx=q->arr_idx;
        duk_uarridx_t i=0, len=duk_get_length(ctx, idx);

        duk_push_object(ctx);
        for (; i<len; i++) {
            duk_get_prop_index(ctx, idx, i);
            duk_put_prop_index(ctx, -2, i);
        }
        duk_replace(ctx, idx);
        q->arr_idx=-1;
        q->obj_idx=idx;
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
            char *r = str_rp_to_json_safe(ctx, -1, NULL, 0);\
            duk_push_string(ctx, r);\
            duk_replace(ctx,-2);\
            free(r);\
            char *s = v = (char*)duk_get_string(ctx, -1);\
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

#define LIKEP_MOD_CHAR '_'
static int rp_add_named_parameters(
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

        //duk_dup(ctx, -1);
        //printf("pushing param '%s'\n", duk_safe_to_string(ctx, -1));
        //duk_pop(ctx);

        if(!duk_is_undefined(ctx, -1))
        {
            push_sql_param;

            /* texis_params is indexed starting at 1 */
            rc = h_param(h, i+1, v, &plen, in, out);
            if (!rc)
            {
                duk_pop(ctx);
                return 0;
            }
        }
        else
        {
            if(*key==LIKEP_MOD_CHAR &&
                 (
                    isdigit(*(key+1)) ||
                    ( *(key+1)==LIKEP_MOD_CHAR && isdigit(*(key+2)) )
                 )
              )
                snprintf(finfo->errmap, msgbufsz-1, "internal error processing likep parameter");
            else
                snprintf(finfo->errmap, msgbufsz-1, "parameter '%s' not found in Object.", key);

            duk_pop(ctx);
            return 0;
        }
        duk_pop(ctx);

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
static void rp_pushfield(duk_context *ctx, FLDLST *fl, int i)
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
    {
        char *v=fl->data[i];
        if(v[0]=='\xff' && v[1] > '\xf9' )
        {
            switch(v[1])
            {
                case '\xff':
                    duk_push_lstring(ctx, v+2, (duk_size_t)fl->ndata[i] -2);
                    break;
                case '\xfe':
                    duk_push_number(ctx, ((double*)v)[1]);
                    break;
                case '\xfd':
                    duk_push_true(ctx);
                    break;
                case '\xfc':
                    duk_push_false(ctx);
                    break;
                case '\xfb':
                    duk_push_null(ctx);
                    break;
                case '\xfa':
                    duk_push_string(ctx, v+2);
                    duk_json_decode(ctx, -1);
                    break;
                default:
                    duk_push_string(ctx,v);
                    break;
            }

            break;
        }
        //else fallthrough
    }
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
static int rp_fetch(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
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
                rp_pushfield(ctx, fl, i);
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
                rp_pushfield(ctx, fl, i);
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
static int rp_fetchWCallback(duk_context *ctx, DB_HANDLE *h, QUERY_STRUCT *q)
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
                    rp_pushfield(ctx, fl, i);
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
                    rp_pushfield(ctx, fl, i);
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
#define IF_CHANGED  0
#define FORCE_RESET 1

static void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx, int force);

#undef pushcounts

/* **************************************************
   Sql.prototype.import
   ************************************************** */
static duk_ret_t rp_sql_import(duk_context *ctx, int isfile)
{
    /* currently only csv
       but eventually add option for others
       by checking options object for
       "type":"filetype" - with default "csv"
    */
    const char *user="PUBLIC",
               *pass="",
               *func_name = isfile?"sql.importCsvFile":"sql.importCsv";
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

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        closecsv;
        RP_THROW(ctx, "no database has been opened");
    }
    db = duk_get_string(ctx, -1);

    duk_pop(ctx);

    h = h_open(db,user,pass);
    if(!h)
        throw_tx_error(ctx, "sql open");

    h_reset_tx_default(ctx, h, this_idx, IF_CHANGED);
    duk_pop(ctx);//this

    tx = h->tx;

    {
        FLDLST *fl;
        char sql[256];

        snprintf(sql, 256, "select NAME, TYPE from SYSCOLUMNS where TBNAME='%s' order by ORDINAL_POSITION;", dcsv.tbname);

        if (!h_prep(h, sql))
        {
            closecsv;
            throw_tx_error_close(ctx, "sql prep", h);
        }

        if (!h_exec(h))
        {
            closecsv;
            throw_tx_error_close(ctx, "sql exec", h);
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
                fn_cleanup(0);
                throw_tx_error_close(ctx, "sql prep", h);
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
                        fn_cleanup(0);
                        throw_tx_error_close(ctx, "sql add parameters", h);
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
                            fn_cleanup(0);
                            throw_tx_error_close(ctx, "sql add parameters", h);
                        }
                    }
                }

                if (!h_exec(h))
                {
                    fn_cleanup(0);
                    throw_tx_error_close(ctx, "sql exec",h);
                }
                if (!h_flush(h))
                {
                    fn_cleanup(0);
                    throw_tx_error_close(ctx, "sql flush", h);
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
    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
    return 1;
}

static duk_ret_t rp_sql_import_csv_file(duk_context *ctx)
{
    return rp_sql_import(ctx, 1);
}

static duk_ret_t rp_sql_import_csv_str(duk_context *ctx)
{
    return rp_sql_import(ctx, 0);
}

/*
   Finds the closing char c. Used for finding single and double quotes
   Tries to deal with escapements
   Returns a pointer to the end of string or the matching character
   *pN is stuffed withe the number of characters it skipped
*/
static char * skip_until_c(char *s,int c,int *pN)
{
   int n=0;
   while(*s)
   {
      if(*s=='\\' && *(s+1)==c)  // deal with escapement
      {
         ++s;
         ++n;
      }
      else
      if(*s==c)
      {
          if(pN)*pN=n;
          return(s);
      }
      ++s;
      ++n;
   }
  if(pN)
     *pN=n;
  return(s);
}

// counts the number of ?'s in the sql string
static int count_sql_parameters(char *s)
{
   int n_params=0;
   while(*s)
   {
      switch(*s)
      {
         case '"' :
         case '\'':
         {
             s=skip_until_c(s+1,*s,NULL);
             break;
         }
         case '\\': ++s; break;
         case '?' : ++n_params;break;
      }
      if(!*s) break;
      ++s;
   }
   return (n_params);
}

static char ** freenames(char **names, int len)
{
    if(!names)
        return names;
    int i=0;
    for (;i<len;i++)
    {
        if(names[i])
            free(names[i]);
    }
    free(names);
    return NULL;
}

// like strndup, but allocates extra_space more bytes
// if len < 0, acts like strdup but allocates extra_space more bytes
static char * strndupx (char *s, ssize_t len, size_t extra_space)
{
    if(!s)
        return NULL;

    char *ret=NULL;

    if(len<0) //get len from *s
    {
        if(extra_space > 0)
        {
            size_t olen = strlen(s) + extra_space + 1;
            CALLOC(ret, olen);
            strcpy(ret, s);
        }
        else
            return strdup(s);
    }
    else
    {
        size_t olen = len + extra_space + 1;
        CALLOC(ret, olen);
        if(len)
            strncpy(ret, s, len);
    }
    return ret;
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

   LIKEP additions: -ajf 2024-07-27
       - likeppos must be freed if not NULL.
       - free_me removed, names[] must be freed with freenames.
       - processing of sql is done even if no '?' params found.
       - semi-colon added if not in string
*/
static int parse_sql_parameters(
    char *old_sql,
    char **new_sql,
    char **names[]
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    ,
    int **likeppos,
    duk_context *ctx,
    duk_idx_t *obj_idx,
    int likepproc
#endif
)
{
    int    n_params=count_sql_parameters(old_sql);
    char * sql      =NULL;
    char **my_names =NULL;
    char * out_p    =NULL;
    char * s        =NULL;
    char idx_s[32];

    int    name_index=0;
    int    quote_len=0;
    int    qm_index=0;
    int gotsemi=0;

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int nlikep=0, likep_index=0;
    char *parsechars="?'\"\\;";
#endif
    //REMALLOC(sql, strlen(old_sql)+1); // the new_sql cant be bigger than the old
    REMALLOC(sql, strlen(old_sql)*2);   // now it can, because of extra
    *new_sql=sql;                 // give the caller the new SQL
    out_p=sql;                    // init the sql output pointer

    if(n_params)
        CALLOC(my_names, n_params*sizeof(char *));

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    if(likeppos)
        *likeppos=NULL;
#endif
    s=old_sql;
    while(*s)
    {
       switch(*s)
       {

          case ';' :
             gotsemi=1;
             break;
#ifdef LIKEP_PARAM_SUBSTITUTIONS
          // find the like?
          case ',' :
          case '(' :
          case ' ' :
             //skip all this if not doing useSuffixPreset, or whatever future thing triggers this
             if(!likepproc)
                 break;
             {
                // we are looking for likep, liker, like3, like, abstract or stingformat('%m..',...),
                // to replace the ? parameter or quoted string if useSuffixPreset == true
                int   vlen=5;
                char *likev=NULL;
                int   mfunc=0;
#define RP_IS_ABST   1
#define RP_IS_STRFMT 2
                int nparan=1;
                int nq=0;
                //copy the char and advance over spaces
                *out_p++=*s++;
                while(isspace(*s))
                {
                    *out_p++=*s++;
                }

                if(!*s)
                    goto noquery;
                else if (strchr(parsechars, *s)) //got one of '"\?;
                    continue;  //continue with the parse
                else if(!strncasecmp("likep", s, 5))
                    likev="likep ?";
                else if(!strncasecmp("liker", s, 5))
                    likev="liker ?";
                else if(!strncasecmp("like3", s, 5))
                    likev="like3 ?";
                else if(!strncasecmp("like", s, 4))
                {
                    likev="like ?";
                    vlen=4;
                }
                else if(!strncasecmp("abstract", s, 8))
                {
                    mfunc=RP_IS_ABST;
                    vlen=8;
                }
                else if(!strncasecmp("stringformat", s, 12))
                {
                    mfunc=RP_IS_STRFMT;
                    vlen=12;
                }

                if(likev || mfunc)
                {
                    char *p = s+vlen;
                    //printf("START = '%s'\n", p);
                    while(isspace(*p))
                        p++;

                    if(mfunc){
                        // abstract( or stringformat(
                        if (*p!='(')
                            goto noquery;
                        p++;
                        //nparan=1;
                    }

                    if(mfunc==RP_IS_ABST) // abstract(text, max, style, query)
                    {
                        //go to first non whitespace char after third comma(not in quotes), if exists
                        int breakpoint=0;
#define RP_ABST_BREAKAFTER 2
                        while(*p)
                        {
                            switch(*p) {
                                case '\'' :
                                case '"'  :
                                    p=skip_until_c(p+1, *p, NULL);
                                    break;
                                case ','  :
                                    if(nparan==1) //only count commas separating own parameters
                                        breakpoint++;
                                    break;
                                case '('  :
                                    nparan++;
                                    break;
                                case ')'  :
                                    nparan--;
                                    if(nparan<1)
                                        breakpoint=100000;
                                    break;
                                case '?'  :
                                    nq++;
                                    break;
                            }
                            if(breakpoint>RP_ABST_BREAKAFTER)
                                break;
                            p++;
                        }
                        if( *p == ',')
                        {
                            p++;
                            while(isspace(*p))
                                p++;
                                //printf("ABSTRACT at '%s'\n", p);
                            // check for ' or ? below
                        }
                        else
                            goto noquery;
                    }
                    else if(mfunc==RP_IS_STRFMT) // stringformat('%something %mxxx', something, ?query, text)
                    {
                        char qc;
                        int paramno=0, mparam=0, breakpoint=0;
                        while(isspace(*p))
                            p++;

                        if(*p == '\'' || *p == '"')
                            qc=*p;
                        else
                            goto noquery; //its something we can't handle

                        // find %m position
                        p++;
                        while (*p && *p != qc)
                        {
                            if(*p=='%') {
                                paramno++;
                                p++;
                                while(*p =='.' || isdigit(*p)) // %10.12mbH is legal
                                    p++;
                                if(*p=='m')
                                    mparam=paramno;
                            }
                            p++;
                        }
                        if(*p != qc || !mparam)
                            goto noquery;
                        p++;
                        // find param after the mparamth ,
                        while(*p)
                        {
                            switch(*p) {
                                case '\'' :
                                case '"'  :
                                    p=skip_until_c(p+1, *p, NULL);
                                    break;
                                case ','  :
                                    if(nparan==1)
                                        breakpoint++;
                                    break;
                                case '('  :
                                    nparan++;
                                    break;
                                case ')'  :
                                    nparan--;
                                    if(nparan<1)
                                        breakpoint=100000;
                                    break;
                                case '?'  :
                                    nq++;
                                    break;
                            }
                            if(breakpoint>=mparam)
                                break;
                            p++;
                        }

                        if( *p == ',')
                        {
                            p++;
                            while(isspace(*p))
                                p++;
                            // check for ' or ? below
                        }
                        else
                            goto noquery;
                    }

                    //printf("checking for ' or ? at >>%.20s<<\n",p);

                    if (*p =='\'' || *p == '"')
                    {
                        // copy the likep phrase to obj at obj_idx
                        // replace with '?' in query
                        // make room for another parameter name and create name as LIKEP_MOD_CHAR+likep_index
                        char endq=*p;

                        n_params++;
                        REMALLOC(my_names, n_params*sizeof(char *));
                        my_names[n_params-1]=NULL;
                        *names=my_names;

                        sprintf(idx_s, "%c%d", LIKEP_MOD_CHAR, likep_index);
                        my_names[name_index++] = strndupx(idx_s, -1, 1);

                        if(mfunc)
                        {
                            size_t clen = p-s;
                            strncpy(out_p, s, clen);
                            out_p+=clen;
                            strcpy(out_p, " ?");
                            out_p+=2;
                        }
                        else
                        {
                            strcpy(out_p, likev);
                            out_p+=vlen+2; //room for ?
                        }

                        // copy the "query terms" in likep 'query terms' to duktape stack
                        p++;
                        s=p;
                        while(*s && *s != endq)
                            s++;

                        if(!*s)
                            goto error_return;

                        if(*obj_idx == -1) // we don't have a q->obj_idx object, so make one
                            *obj_idx = duk_push_object(ctx);

                        // the name for duk_put_prop()
                        duk_push_sprintf(ctx, "%c%d", LIKEP_MOD_CHAR, likep_index++);
                        // the value
                        duk_push_lstring(ctx, p, (duk_size_t)(s-p));
                        duk_put_prop(ctx, *obj_idx);
                        s++; // '\''

                        REMALLOC(*likeppos, sizeof(int) * (nlikep +2));  //last will be -1;
                        (*likeppos)[nlikep++]=name_index-1;              //the corresponding index in my_names
                        (*likeppos)[nlikep]=-1;                          //terminate list
                        continue;
                    }
                    else if (*p == '?')
                    {
                        REMALLOC(*likeppos, sizeof(int) * (nlikep +2));  //last will be -1;
                        (*likeppos)[nlikep++]=name_index+nq;             //the corresponding index in my_names
                        (*likeppos)[nlikep]=-1;                          //terminate list
                        //printf("Post add like s='%s'\n", s);
                        continue;
                    }
                }
                //printf("AT END s='%s'\n", s);
                break;
             }
#endif
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
                //printf("DOING ? at '%s'\n", s);
                ++s;

                if(!(isalnum(*s) || *s=='_' || *s=='"' || *s=='\'')) // check for legal 1st char
                {
                    sprintf(idx_s, "%d", qm_index++);
                    my_names[name_index++] = strndupx(idx_s, -1, 1);
                    *out_p='?';
                    ++out_p;
                    break;
                }

                if(*s=='"' || *s=='\'')          // handle ?"my var"
                {
                   int quote_type=*s;
                   char *p=++s;

                   s=skip_until_c(s,quote_type,&quote_len);
                   if(!*s)           // we hit a null without an ending "
                      goto error_return;

                   my_names[name_index++] = strndupx(p, (ssize_t)(s-p), 1);
                   *out_p='?';
                   ++out_p;
                   ++s;
                   continue;
                }
                else
                {
                   char *p=s;

                   while(*s && (isalnum(*s) || *s=='_'))
                      ++s;
                   my_names[name_index++] = strndupx(p, (ssize_t)(s-p), 1);
                   *out_p='?';
                   ++out_p;
                   *out_p++=*s;

                   if(!*s)           //  terminated at the end of the sql we're done
                      goto end_return;
                   else
                   {
                      *out_p=*s;
                   }
                   ++s;
                 continue;
                }
             }
             break;
       }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
       noquery:
#endif
       *out_p++=*s;
       if(!*s)
          break;
       ++s;
    }

   end_return:

   // terminate string
   *out_p='\0';

   //set names
   *names=my_names; // NULL if not doing names

   // terminate sql if necessary
   if(!gotsemi)
       strcat(sql,";");

   return(n_params);

   error_return:
   // free everything and set passed in *pointer to NULL
   if(my_names)
   {
     *names=freenames(my_names, n_params);
     *names=NULL;
   }
   if(sql)
   {
     free(sql);
     *new_sql=NULL;
   }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
   if(*likeppos)
   {
       free(*likeppos);
       *likeppos=NULL;
   }
#endif
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

#define throw_tx_or_log_error_old(ctx,pref,msg) do{\
    rp_log_error(ctx);\
    if(!isquery) RP_THROW(ctx, "%s error: %s",pref, msg);\
    else if(q && q->callback > -1) duk_push_number(ctx, -1);\
    else {\
        duk_push_object(ctx);\
        duk_push_sprintf(ctx, "%s: %s", pref, msg);\
        duk_put_prop_string(ctx, -2, "error");\
    }\
    goto end_query;\
}while(0)

#define throw_tx_or_log_error(ctx,pref,msg) do{\
    rp_log_error(ctx);\
    if(!isquery) \
        RP_THROW(ctx, "%s error: %s",pref, msg);\
    else\
        duk_push_null(ctx);\
    goto end_query;\
}while(0)

// close resets finfo->errmap
#define throw_tx_or_log_error_close(ctx,pref,msg,h) do{\
    rp_log_error(ctx);\
    if(!isquery) \
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s error: %s",pref, msg);\
    else\
        duk_push_null(ctx);\
    h_close(h);\
    h=NULL;\
    if(!isquery) (void) duk_throw(ctx);\
    goto end_query;\
}while(0)

#define throw_or_log_error_old(msg) do{\
    if(!isquery) RP_THROW(ctx, "%s",msg);\
    else if(q && q->callback > -1){\
        rp_log_copy_to_errMsg(ctx, msg);\
        duk_push_number(ctx, -1);\
    } else {\
        duk_push_object(ctx);\
        duk_push_sprintf(ctx, "%s", msg);\
        duk_put_prop_string(ctx, -2, "error");\
    }\
    goto end_query;\
}while(0)

#define throw_or_log_error(msg) do{\
    rp_log_copy_to_errMsg(ctx, msg);\
    if(!isquery)\
        RP_THROW(ctx, "%s",msg);\
    else\
        duk_push_null(ctx);\
    goto end_query;\
}while(0)

// from rampart.utils - put this in rampart.h
duk_ret_t word_replace_new(duk_context *ctx);

#ifdef LIKEP_PARAM_SUBSTITUTIONS

#include "english_short_nouns.h"

/* load short noun equivs into rampart.utils.replace
   and stash it for future use                         */
static duk_idx_t load_list(duk_context *ctx)
{
    duk_push_global_stash(ctx);
    if(!duk_get_prop_string(ctx, -1, "shortnouns"))
    {
        duk_pop(ctx);
        duk_push_c_function(ctx, word_replace_new, 2);
        duk_push_string(ctx, english_short_nouns);
        duk_push_undefined(ctx);
        duk_new(ctx, 2);
        duk_dup(ctx, -1);
        duk_put_prop_string(ctx, -3, "shortnouns");
    }
    duk_remove(ctx, -2); //stash
    return duk_normalize_index(ctx, -1);
}
#endif // LIKEP_PARAM_SUBSTITUTIONS

/* **************************************************
   Sql.prototype.exec
   ************************************************** */
static duk_ret_t rp_sql_exec_query(duk_context *ctx, int isquery)
{
    TEXIS *tx;
    QUERY_STRUCT *q=NULL, q_st;
    DB_HANDLE *h = NULL;
    const char *db, *user="PUBLIC", *pass="";
    duk_idx_t this_idx;
    struct sigaction sa = { {0} };
    sa.sa_flags = 0; //SA_NODEFER;
    sa.sa_handler = die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    clearmsgbuf();

    int nParams=0;
    char *newSql=NULL, **namedSqlParams=NULL;
    //  signal(SIGUSR2, die_nicely);

    SET_THREAD_UNSAFE(ctx);

    duk_push_this(ctx);
    this_idx = duk_get_top_index(ctx);

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int do_suffix=0;
    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings")))
    {
        if(duk_get_prop_string(ctx, -1, "usesuffixpreset")) {
            do_suffix=duk_get_boolean_default(ctx, -1, 0);
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);
#endif

    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    /* clear the sql.errMsg string */
    duk_del_prop_string(ctx,-1,"errMsg");

    if (!duk_get_prop_string(ctx, this_idx, "db"))
        throw_or_log_error("no database has been opened");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx); //db

    q_st = rp_get_query(ctx);
    q = &q_st;

    /* call parameters error, message is already pushed */
    if (q->err == QS_ERROR_PARAM)
    {
        goto end;
    }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int *likeppos = NULL;
    nParams = parse_sql_parameters((char*)q->sql, &newSql, &namedSqlParams, &likeppos, ctx, &(q->obj_idx), do_suffix);
    /*
    printf("newSql='%s' likeppos=%p\n", newSql, likeppos);
    if(likeppos)
    {
        int i=0, *lpp = likeppos;
        while( lpp[i] > -1 )
        {
            printf("likep found in pos %d: '%s'\n", lpp[i], namedSqlParams[lpp[i]]);
            i++;
        }
    } */
#else
    nParams = parse_sql_parameters((char*)q->sql, &newSql, &namedSqlParams);
#endif

    if (nParams > -1) // even if zero, we will get a newSql - ajf 2025-07-27
    {
        duk_push_string(ctx, newSql);
        duk_replace(ctx, q->str_idx);
        q->sql = duk_get_string(ctx, q->str_idx);
        free(newSql);
    }
#ifdef LIKEP_PARAM_SUBSTITUTIONS
    /* fix parameters or query */
    if(do_suffix && likeppos) {
        int i=0, *lpp = likeppos;

        duk_idx_t listf_idx = load_list(ctx); // load the list and push wordReplace at listf_idx

        // replace each likep parameter with wordReplace(parameter)
        while( lpp[i] > -1 )
        {
            char *param = namedSqlParams[lpp[i]];

            duk_push_string(ctx,"exec");
            duk_get_prop_string(ctx, q->obj_idx, param);

            // bail if not a string
            if(!duk_is_string(ctx, -1))
            {
                duk_pop_2(ctx);
                i++;
                continue;
            }

            //don't alter object beyond adding hidden properties
            memmove(param+1, param, strlen(param));  //make room for appending LIKEP_MOD_CHAR
            *param=LIKEP_MOD_CHAR; //same name, but altered

            // check if we've done this already
            if(duk_has_prop_string(ctx, q->obj_idx, param))
            {
                //printf("repeat of %s\n", param);
                duk_pop_2(ctx);
                i++;
                continue;
            }

            duk_call_prop(ctx, listf_idx, 1);

            //printf("substituted param %s -> likep = '%s'\n", param, duk_get_string(ctx,-1));

            duk_put_prop_string(ctx, q->obj_idx, param);
            i++;
        }
    }

    if(likeppos)
    {
        free(likeppos);
        likeppos=NULL;
    }
#endif // LIKEP_PARAM_SUBSTITUTIONS
    /* OPEN */
    h = h_open(db,user,pass);
    if(!h)
    {
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error(ctx, "sql open", finfo->errmap);
    }
    h_reset_tx_default(ctx, h, this_idx, IF_CHANGED);

//  messes up the count for arg_idx, so just leave it
//    duk_remove(ctx, this_idx); //no longer needed

    tx = h->tx;
    if (!tx)
    {
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error(ctx, "open sql", finfo->errmap);
    }
    /* PREP */
    if (!h_prep(h, (char *)q->sql))
    {
        namedSqlParams=freenames(namedSqlParams, nParams);
        throw_tx_or_log_error_close(ctx, "sql prep", finfo->errmap, h);
    }

    /* PARAMS
       sql parameters are the parameters corresponding to "?key" in a sql statement
       and are provide by passing an object in JS call parameters */
    if( namedSqlParams)
    {
        if(q->obj_idx == -1)
        {
            h_close(h);
            h=NULL;
            namedSqlParams=freenames(namedSqlParams, nParams);
            throw_or_log_error("sql.exec - parameters specified in sql statement, but no corresponding object or array");
        }

        if (!rp_add_named_parameters(ctx, h, q->obj_idx, namedSqlParams, nParams))
        {
            namedSqlParams=freenames(namedSqlParams, nParams);
            throw_tx_or_log_error_close(ctx, "sql add parameters", finfo->errmap, h);
        }

        namedSqlParams=freenames(namedSqlParams, nParams);
    }


    /* sql parameters are the parameters corresponding to "?" in a sql statement
     and are provide by passing array in JS call parameters
     TODO: check that this is indeed dead code given that parse_sql_parameters now
           turns "?, ?" into "?0, ?1"
     *
    else if (q->arr_idx != -1)
    {
        if (!rp_add_parameters(ctx, h, q->arr_idx))
            throw_tx_or_log_error_close(ctx, "sql add parameters", finfo->errmap, h);
    }
    */
    else
    {
        h_resetparams(h);
    }

    /* EXEC */
    if (!h_exec(h))
        throw_tx_or_log_error_close(ctx, "sql exec", finfo->errmap, h);

    /* skip rows using texisapi */
    if (q->skip)
        h_skip(h, q->skip);

    /* callback - return one row per callback */
    if (q->callback > -1)
    {
        int rows = rp_fetchWCallback(ctx, h, q);
        duk_push_int(ctx, rows);
        goto end; /* done with exec() */
    }

    /*  No callback, return all rows in array of objects */
    (void)rp_fetch(ctx, h, q);

    end:
    h_end_transaction(h);

    /* this is not working out.  Just keep it logged and keep going if we got this far
    if(rp_log_error(ctx))
    {
        if(isquery)
            duk_push_null(ctx);
        else
            RP_THROW(ctx, "sql exec: %s", finfo->errmap);
    }
    */
    // just log error
    rp_log_error(ctx);

    return 1; /* returning outer array */

    end_query:
    if(h) h_end_transaction(h);
    return 1; /* returning outer array or error*/

}

static duk_ret_t rp_sql_exec(duk_context *ctx)
{
  return rp_sql_exec_query(ctx, 0);
}

static duk_ret_t rp_sql_query(duk_context *ctx)
{
  return rp_sql_exec_query(ctx, 1);
}

/* **************************************************
   Sql.prototype.eval
   ************************************************** */
static duk_ret_t rp_sql_eval(duk_context *ctx)
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
        rp_log_copy_to_errMsg(ctx, "Error: Eval: No string to evaluate");
        duk_push_int(ctx, -1);
        return (1);
    }

    duk_push_sprintf(ctx, "select %s;", stmt);
    duk_replace(ctx, str_idx);
    rp_sql_exec(ctx);
    duk_get_prop_string(ctx, -1, "rows");
    duk_get_prop_index(ctx, -1, 0);
    return (1);
}

static duk_ret_t rp_sql_one(duk_context *ctx)
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
        RP_THROW(ctx, "sql.one: No sql statement provided");
    }

    duk_push_object(ctx);
    duk_push_number(ctx, 1.0);
    duk_put_prop_string(ctx, -2, "maxRows");
    duk_push_true(ctx);
    duk_put_prop_string(ctx, -2, "returnRows");

    if( obj_idx != -1)
        duk_pull(ctx, obj_idx);

    rp_sql_exec(ctx);
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
    Get the heapptr of last sql object used to do sql.set in this thread
    if different from current, reapply settings
*/

static void h_reset_tx_default(duk_context *ctx, DB_HANDLE *h, duk_idx_t this_idx, int force)
{
    void *cur = duk_get_heapptr(ctx, this_idx);

    // if this was the last one to reapply settings, we don't need to do it again
    if(!force && cur == last_sql_set)
        return; 

     // reset settings if any
     {
         char errbuf[msgbufsz];
         int ret;

         if (!duk_get_prop_string(ctx, this_idx, DUK_HIDDEN_SYMBOL("sql_settings")) )
         {
             //we have no old settings
             duk_pop(ctx);//undefined
             duk_push_object(ctx); //empty object, reset anyway
         }

         ret = h_set(ctx, h, errbuf);

         duk_pop(ctx);

         if(ret == -1)
         {
             h_close(h);
             RP_THROW(ctx, "%s", errbuf);
         }
         else if (ret ==-2)
             throw_tx_error_close(ctx, errbuf, h);
        else
             last_sql_set=cur;
     }
}


static duk_ret_t rp_texis_reset(duk_context *ctx)
{
    const char *db, *user="PUBLIC", *pass="";
    DB_HANDLE *h = NULL;

    duk_push_this(ctx); //idx == 0

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    //remove saved settings
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("sql_settings"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("indlist"));
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist"));

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx);

    h = h_open(db,user,pass);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }

    h_reset_tx_default(ctx, h, -1, FORCE_RESET);

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
    else if (!strcmp("keepequivs", prop) || !strcmp("useequivs", prop) || !strcmp("keepeqvs", prop))
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
   {"indexmeter", "off"},
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
//todo revisit "ul" and capitulating -> capitalism
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

    while (props[0])
    {
        if(setprop(ddic, props[0], props[1] )==-1)
        {
            sprintf(errbuf, "sql reset");
            return -2;
        }
        i++;
        props = prop_defaults[i];
    }

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
        rp_log_error(ctx);
        snprintf(errbuf, msgbufsz, "Texis setprop failed\n%s", finfo->errmap);
        goto return_neg_two;
    }

    lpstmt = tx->hstmt;
    if(lpstmt && lpstmt->dbc && lpstmt->dbc->ddic)
            ddic = lpstmt->dbc->ddic;
    else
    {
        sprintf(errbuf,"sql.set open: %s", finfo->errmap);
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

        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            clearmsgbuf();
            if(setprop(ddic, "addindextmp", (char*)val )==-1)
            {
                sprintf(errbuf, "sql.set: %s", finfo->errmap);
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
    }
    duk_pop(ctx);//list or undef

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("explist")))
    {
        int i=0, len = duk_get_length(ctx, -1);
        const char *val;

        /* delete first entry for an empty list */
        clearmsgbuf();
        if(setprop(ddic, "delexp", "0" )==-1)
        {
            sprintf(errbuf, "sql.set: %s", finfo->errmap);
            goto return_neg_two;
        }
        for (i=0;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
            val = duk_get_string(ctx, -1);
            clearmsgbuf();
            if(setprop(ddic, "addexp", (char*)val )==-1)
            {
                sprintf(errbuf, "sql.set: %s", finfo->errmap);
                goto return_neg_two;
            }
            duk_pop(ctx);
        }
    }
    duk_pop_2(ctx);// list and this

    duk_idx_t setobj_idx=duk_normalize_index(ctx, -1);

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    int do_suffix=0;
    if(duk_get_prop_string(ctx, setobj_idx, "usesuffixpreset")) {
        do_suffix=duk_get_boolean_default(ctx, -1, 0);
    }
    duk_pop(ctx);
#endif


    // apply all settings in object
    duk_enum(ctx, setobj_idx, 0);
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

        /* useDerivations, set eqprefix and related */
        if( strcmp(prop,"usederivations")==0 )
        {
            RPPATH rp={{0}};
            char eqfile[16], eqpath[16];
            const char *dlang=NULL;
            if(duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) )
                dlang="en";
            else if (!duk_is_string(ctx, -1))
            {
                snprintf(errbuf, msgbufsz, "sql.set: useDerivations must be a Boolean or a String (lang code)");
                goto return_neg_one;
            }
            else
                dlang = duk_get_string(ctx, -1);

            if (strlen(dlang) > 2)
            {
                snprintf(errbuf, msgbufsz, "sql.set: useDerivations String must be 2 characters (lang code)");
                goto return_neg_one;
            }
            snprintf(eqfile, 16, "%s-deriv", dlang);
            snprintf(eqpath, 16, "derivations/%s", dlang);
            rp=rp_find_path(eqfile, eqpath);
            if(strlen(rp.path))
            {
                //printf("setting equiv file: '%s'\n", rp.path);
                if(setprop(ddic, "eqprefix", rp.path )==-1)
                {
                    sprintf(errbuf, "sql.set: %s", finfo->errmap);
                    goto return_neg_two;
                }

                if(setprop(ddic, "alequivs", "1" )==-1)
                {
                    sprintf(errbuf, "sql.set: %s", finfo->errmap);
                    goto return_neg_two;
                }
                if(setprop(ddic, "keepeqvs", "1" )==-1)
                {
                    sprintf(errbuf, "sql.set: %s", finfo->errmap);
                    goto return_neg_two;
                }

            }
            else
            {
                sprintf(errbuf, "sql.set: couldn't find %s in %s", eqfile, eqpath);
                goto return_neg_two;
            }
            goto propnext;
        }

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

                clearmsgbuf();
                if(setprop(ddic, (char*)prop, (char*)aval )==-1)
                {
                    sprintf(errbuf, "sql.set: %s", finfo->errmap);
                    goto return_neg_two;
                }
                duk_pop_2(ctx);
            }
            duk_pop(ctx);
        }
        else
        {
#ifdef LIKEP_PARAM_SUBSTITUTIONS
            if(!strcasecmp(prop, "usesuffixpreset"))
                goto propnext;
#endif
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

            clearmsgbuf();
            //printf("setprop(%s, %s)\n", prop, val);
            if(setprop(ddic, (char*)prop, (char*)val )==-1)
            {
                sprintf(errbuf, "sql.set: %s", finfo->errmap);
                goto return_neg_two;
            }
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

#ifdef LIKEP_PARAM_SUBSTITUTIONS
    // shortcut setting, overriding anything set above
    if(do_suffix)
    {
        if(setprop(ddic, "useequiv", "1" )==-1)
        {
            sprintf(errbuf, "sql.set: %s", finfo->errmap);
            goto return_neg_two;
        }
        if(setprop(ddic, "minwordlen", "5" )==-1)
        {
            sprintf(errbuf, "sql.set: %s", finfo->errmap);
            goto return_neg_two;
        }
    }
#endif

    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */

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

static duk_ret_t rp_texis_set(duk_context *ctx)
{
    const char *db, *user="PUBLIC", *pass="";
    DB_HANDLE *h = NULL;
    int ret = 0;
    char errbuf[msgbufsz];
    char propa[64], *prop=&propa[0];

    duk_push_this(ctx); //idx == 1

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("user")))
        user=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("pass")))
        pass=duk_get_string(ctx, -1);
    duk_pop(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
        RP_THROW(ctx, "no database is open");

    db = duk_get_string(ctx, -1);
    duk_pop(ctx);

    h = h_open(db,user,pass);

    if(!h)
    {
        throw_tx_error(ctx, "sql open");
    }

    h_reset_tx_default(ctx, h, -1, FORCE_RESET);

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

        //check for "useequiv", set "alequiv" to true.
        if(strcmp("useequiv", prop)==0 &&
            (
                ( duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1) ) ||
                ( duk_is_number(ctx, -1) && duk_get_number(ctx, -1) != 0 ) ||
                ( duk_is_string(ctx, -1) && strcmp(duk_get_string(ctx,-1), "1")==0) )
            )
        {
            duk_push_true(ctx);
            duk_put_prop_string(ctx, 2, "alequivs");
        }
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

    clean_settings(ctx);
    return (duk_ret_t) ret;
}
int largc;
char **largv;

static void addtbl(duk_context *ctx, char *func, const char *db, char *tbl)
{
    char *err=NULL;

    if(access(tbl,W_OK) != 0)
    {
        err=strerror(errno);
        errno=0;
    }
    else switch ( TXaddtable((char*)db, tbl, NULL, NULL, NULL, NULL, 0) )
    {
        case -2:	err="permission denied";break;
        case -1:	err="unknown error";break;
        case 0:		err=NULL;break;
        default:	err="unknown error";break;
    }
    if(err)
    {
        RP_THROW(ctx, "%s: error importing table %s - %s", func, tbl, err);
    }
}

static duk_ret_t rp_sql_addtable(duk_context *ctx)
{
    const char *db, *tbl = REQUIRE_STRING(ctx, 0, "argument must be a string (/path/to/importTable.tbl)");

    duk_push_this(ctx);

    if (!duk_get_prop_string(ctx, -1, "db"))
    {
        RP_THROW(ctx, "no database has been opened");
    }
    db=duk_get_string(ctx, -1);
    addtbl(ctx, "addTable()", db, (char*)tbl);
    return 0;
}

// the updater daemon script.  Checks that it is needed, then forks
// to monitor text indexes.

static char *updater_js =
"function(npsql) { \n"
    "var su;\n"
    "try {su=require('rampart-sqlUpdate.js');}catch(e){}\n"
    "if(su){su.launchUpdater(npsql);return true;}\n"
    "else return false;\n"
"}";


/* **************************************************
   Sql("/database/path") constructor:

   var sql=new Sql("/database/path");
   var sql=new Sql("/database/path",true); //create db if not exists

   ************************************************** */
static duk_ret_t rp_sql_constructor(duk_context *ctx)
{
    const char *db = NULL, *user="PUBLIC", *pass="";
    DB_HANDLE *h;
    int force=0, addtables=0, create=0, no_updater=0;
    char *default_db_files[]={
        "SYSCOLUMNS.tbl",  "SYSINDEX.tbl",  "SYSMETAINDEX.tbl",  "SYSPERMS.tbl",
        "SYSSTATS.tbl",  "SYSTABLES.tbl",  "SYSTRIG.tbl",  "SYSUSERS.tbl", NULL
    };

    /* allow call to Sql() with "new Sql.connect()" only */
    if (!duk_is_constructor_call(ctx))
    {
        RP_THROW(ctx, "Sql.connection():  Must be called with 'new Sql.connection()");
    }

    if(duk_is_string(ctx, 0))
    {
        db = duk_get_string(ctx, 0);
    }

    if(duk_is_boolean(ctx, 1))
        create = (int) duk_get_boolean(ctx, 1);
    else if (!duk_is_undefined(ctx, 1))
        RP_THROW(ctx, "new Sql.connection(path,create) - create must be a boolean");

    if(duk_is_object(ctx, 0))
    {
        if(duk_get_prop_string(ctx, 0, "path"))
        {
            db = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "user"))
        {
            user = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "pass"))
        {
            pass = REQUIRE_STRING(ctx, -1, "new Sql.connection(params) - params.path must be a string");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "force"))
        {
            force = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.force must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "noUpdater"))
        {
            no_updater = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.noUpdater must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "addtables"))
        {
            addtables = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.addtables must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "addTables"))
        {
            addtables = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params.addTables must be a boolean");
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, 0, "create"))
        {
            create = (int)REQUIRE_BOOL(ctx, -1, "new Sql.connection(params) - params. must be a boolean");
        }
        duk_pop(ctx);

        if(addtables||force)
            create=1;
    }


    if(!db || !strlen(db))
        RP_THROW(ctx,"new Sql.connection() - empty or missing database name");

    clearmsgbuf();

    /* check for db first */
    h = h_open(db, user, pass);
    /* if h, then just open that.  Ignore all other options except addtables */

    if (!h)// otherwise check options
    {
        /*
         if sql=new Sql("/db/path",true), we will
         create the db if it does not exist
        */
        if (create)
        {
            DIR *dir = NULL;

            clearmsgbuf();

            if (rmdir(db) != 0)
            {
#ifdef EEXIST
                if(errno==EEXIST || errno==ENOTEMPTY)
#else
                if(errno==ENOTEMPTY)
#endif
                {
                    dir = opendir(db); //let's have a look inside this dir below
                }
                else if (errno!=ENOENT) //some other error than EEXIST, ENOTEMPTY or ENOENT
                    RP_THROW(ctx, "sql.connection(): cannot create database at '%s' - %s", db, strerror(errno));
                //else the dir doesn't exist, which is just fine.
            }

            // if dir exists, and we are using force or addtables, check if texis system files are present
            if(dir && (force||addtables))
            {
                char **s;
                struct dirent *entry=NULL;

                errno = 0;

                while ((entry = readdir(dir)) != NULL)
                {
                    s=default_db_files;
                    while(*s)
                    {
                        if(entry->d_name[0] != '.' && strcmp(*s, entry->d_name)==0)
                        {
                            closedir(dir);
                            RP_THROW(ctx, "sql.connection(): cannot create '%s', directory exists and has at least one SYS* file (%s)", db, *s);
                            break;
                        }
                        s++;
                    }
                }

                closedir(dir);
                dir=NULL;

                if(errno) //we read the dir
                {
                    int er=errno;
                    errno=0;
                    RP_THROW(ctx, "sql.connection(): cannot create '%s' - %s", db, strerror(er));
                }

                if(strlen(db)+20 > PATH_MAX)
                {
                    RP_THROW(ctx, "sql.connection(): cannot create '%s', path too long", db);
                }
                else
                {
                    char tmppath[PATH_MAX];
                    char topath[PATH_MAX];

                    strcpy(tmppath, db);
                    strcat(tmppath, "/.t");

                    if(!h_create(tmppath))
                    {
                        rp_log_error(ctx);
                        RP_THROW(ctx, "sql.connection(): cannot create database at '%s':\n%s", db, finfo->errmap);
                    }

                    s=default_db_files;
                    while(*s)
                    {
                        strcpy(tmppath, db);
                        strcat(tmppath, "/.t/");
                        strcat(tmppath, *s);
                        strcpy(topath, db);
                        strcat(topath, "/");
                        strcat(topath, *s);
                        if (rename(tmppath, topath))
                        {
                            RP_THROW(ctx, "sql.connection(): cannot create database at '%s': error moving files - %s", db, strerror(errno));
                        }
                        s++;
                    }
                    strcpy(tmppath, db);
                    strcat(tmppath, "/.t");

                    if(addtables)
                    {

                        dir = opendir(db);

                        while ((entry = readdir(dir)) != NULL)
                        {
                            char *e = entry->d_name, **s;
                            int len=strlen(e), issys=0;

                            s=default_db_files;

                            while(*s)
                            {
                                if(strcmp(*s, e)==0)
                                {
                                    issys=1;
                                    break;
                                }
                                s++;
                            }

                            if(!issys && e[len-4]=='.' && e[len-3]=='t' && e[len-2]=='b' && e[len-1]=='l')
                            {
                                char p[PATH_MAX];

                                strcpy(p,db);
                                strcat(p,"/");
                                strcat(p,e);
                                //printf("adding %s to %s\n", p, db);
                                addtbl(ctx, "sql.connection()", db, p);
                            }
                        }

                    }

                    if (rmdir(tmppath) != 0)
                    {
                        RP_THROW(ctx, "sql.connection(): cannot create database at '%s': error removing directory - %s", db, strerror(errno));
                    }
                }

            }
            else if(dir) // there's a dir and we don't have force or addtables.
            {
                closedir(dir);
                RP_THROW(ctx, "sql.connection(): cannot create '%s', directory exists and is not empty", db);
            }
            else if (!h_create(db)) //if !dir, try regular create
            {
                rp_log_error(ctx);
                RP_THROW(ctx, "sql.connection(): cannot open or create database at '%s':\n%s", db, finfo->errmap);
            }

            if(dir)
                closedir(dir);
        }
        else
        {
            rp_log_error(ctx);
            RP_THROW(ctx, "sql.connection(): cannot open database at '%s'\n%s", db, finfo->errmap);
        }
        h = h_open(db, user, pass);
        addtables=0;
    }


    rp_log_error(ctx); /* log any non fatal errors to this.errMsg */
    h_end_transaction(h);

    /* save the name of the database in 'this' */
    duk_push_this(ctx); /* -> stack: [ db this ] */
    duk_push_string(ctx, db);
    duk_put_prop_string(ctx, -2, "db");
    duk_push_number(ctx, RESMAX_DEFAULT);
    duk_put_prop_string(ctx, -2, "selectMaxRows");
    duk_push_string(ctx, user);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("user"));
    duk_push_string(ctx, pass);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("pass"));

    //currently unused, probably can be removed:
    SET_THREAD_UNSAFE(ctx);

    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    // addtables for existing db
    if(addtables)
    {
        int l, i=0;
        const char **existing=NULL;
        struct dirent *entry=NULL;
        DIR *dir = NULL;

        duk_push_string(ctx,"exec");
        duk_push_string(ctx, "select stringformat('%s%s', convert(WHAT, 'varchar' ), '.tbl') tbls from SYSTABLES where \
            WHAT!='SYSCOLUMNS' and  WHAT!='SYSINDEX' and  WHAT!='SYSMETAINDEX' and  WHAT!='SYSPERMS' and \
            WHAT!='SYSSTATS' and  WHAT!='SYSTABLES' and  WHAT!='SYSTRIG' and  WHAT!='SYSUSERS';");
        duk_push_object(ctx);
        duk_push_string(ctx, "array");
        duk_put_prop_string(ctx, -2, "returnType");
        duk_call_prop(ctx, -4, 2);
        duk_get_prop_string(ctx, -1, "rows");
        duk_remove(ctx, -2);

        l=(int)duk_get_length(ctx, -1);

        if(l)
        {
            REMALLOC(existing, l*sizeof(char *));

            for(i=0;i<l;i++)
            {
                duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                existing[i]=duk_to_string(ctx, -1);
                duk_pop(ctx);
            }

        }

        dir = opendir(db);

        while ((entry = readdir(dir)) != NULL)
        {
            char **s, *e = entry->d_name;
            size_t len = strlen(e);

            if(e[len-4]=='.' && e[len-3]=='t' && e[len-2]=='b' && e[len-1]=='l')
            {
                char p[PATH_MAX];
                int skip=0;

                s=default_db_files;

                while(*s)
                {
                    if(strcmp(*s, e)==0)
                    {
                        skip=1;
                        break;
                    }

                    s++;
                }

                if(!skip)
                {
                    for(i=0;i<l;i++)
                    {
                        if(strcmp(e,existing[i])==0)
                        {
                            skip=1;
                            break;
                        }
                    }
                }

                if(!skip)
                {
                    strcpy(p,db);
                    strcat(p,"/");
                    strcat(p,e);

                    addtbl(ctx, "sql.connection()", db, p);
                }
            }

        }
        if(existing)
            free(existing);
        closedir(dir);
        //safeprintstack(ctx);
        duk_pop(ctx); //array of WHATs from SYSTABLES
    }

    if(!no_updater)
    {
        // if there are entries in SYSUPDATE, run the update monitor as a daemon
        duk_push_string(ctx, "rampart-sql.c:indexUpdater()");
        duk_compile_string_filename(ctx, DUK_COMPILE_FUNCTION, updater_js);
        duk_push_this(ctx);
        duk_call(ctx, 1);
    }
    return 0;
}
/*
#define CALLONE 0
#define CALLEXEC 1
#define NOARGS DUK_INVALID_INDEX
static inline void call_sql(duk_context *ctx, char *sql, int type, duk_idx_t param_idx)
{
    int nargs=1;
    if(param_idx != NOARGS)
    {
        nargs=2;
        param_idx=duk_normalize_index(ctx, param_idx);
    }

    if(type==CALLEXEC)
        duk_push_c_function(ctx, rp_sql_exec, nargs);
    else
        duk_push_c_function(ctx, rp_sql_one, nargs);

    duk_push_this(ctx); //has db name

    duk_push_string(ctx, sql);

    if(param_idx != NOARGS)
        duk_pull(ctx, param_idx);
printf("Call method nargs=%d\n", nargs);
safeprintstack(ctx);
    duk_call_method(ctx, nargs);
}
*/


static duk_ret_t fork_helper(duk_context *ctx)
{
    SFI finfo_d = {0};

    finfo=&finfo_d;
    REMALLOC(finfo->mapinfo, sizeof(FMINFO));

    setproctitle("rampart sql_helper");

    finfo->reader = REQUIRE_UINT(ctx,0, "Error, this function is meant to be run upon forking only");
    finfo->writer = REQUIRE_UINT(ctx,1, "Error, this function is meant to be run upon forking only");
    /* to help with debugging, get parent's thread num */
    thisfork = REQUIRE_UINT(ctx,2, "Error, this function is meant to be run upon forking only");

    struct sigaction sa = { {0} };

    RP_TX_isforked=1; // mutex locking not necessary in fork
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = 0;
    sa.sa_handler =  die_nicely;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    setproctitle("rampart sql_helper");

    if(!rp_watch_pid(getppid(), "rampart sql_helper"))
        fprintf(stderr, "Start watcher for sql helper failed\n");

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    TXunneededRexEscapeWarning = 0; //silence rex escape warnings

    do_child_loop(finfo); // loop and never come back;
    // mmap happens in loop

    return 0;
}

static duk_ret_t rp_sql_connect(duk_context *ctx)
{
    duk_push_this(ctx);
    if(!duk_get_prop_string(ctx, -1, "connection"))
        RP_THROW(ctx, "Sql.connect: no this binding");
    duk_insert(ctx, 0);
    duk_pop(ctx); //this
    // [ connection_constructor(), arg0, arg1 ]
    duk_new(ctx, 2);
    return 1;
}

//the sql.scheduleUpdate function
static const char *schupd =
"var su;\n"
"try { su=require('rampart-sqlUpdate.js'); } catch(e){}\n"
"if(su) su.scheduleUpdate\n";

/* **************************************************
   Initialize Sql module
   ************************************************** */
char install_dir[PATH_MAX+21];
duk_ret_t rp_exec(duk_context *ctx);

extern int TX_is_rampart;

duk_ret_t duk_open_module(duk_context *ctx)
{

    TX_is_rampart=1;
    /* Set up locks:
     * this will be run once per new duk_context/thread in server.c
     * but needs to be done only once for all threads
     */
    CTXLOCK;
    if (!db_is_init)
    {
        char *TexisArgv[2];

        RP_PTINIT(&tx_handle_lock);

        TexisArgv[0]=rampart_exec;

        // To help find texislockd -- which might be only in the same dir
        // as 'rampart' executable.
        strcpy (install_dir, "--install-dir-force=");
        strcat (install_dir, rampart_bin);
        TexisArgv[1]=install_dir;

        if( TXinitapp(NULL, NULL, 2, TexisArgv, &largc, &largv) )
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

    duk_push_c_function(ctx, rp_sql_constructor, 2 /*nargs*/);

    /* Push proto object that will be Sql.connection.prototype. */
    duk_push_object(ctx); /* -> stack: [ {}, Sql protoObj ] */

    /* Set Sql.connection.prototype.exec. */
    duk_push_c_function(ctx, rp_sql_exec, 6 /*nargs*/);   /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "exec");                    /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* Set Sql.connection.prototype.query. */
    duk_push_c_function(ctx, rp_sql_query, 6 /*nargs*/);  /* [ {}, Sql protoObj fn_exe ] */
    duk_put_prop_string(ctx, -2, "query");                   /* [ {}, Sql protoObj-->{exe:fn_exe} ] */

    /* set Sql.connection.prototype.eval */
    duk_push_c_function(ctx, rp_sql_eval, 4 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "eval");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.connection.prototype.eval */
    duk_push_c_function(ctx, rp_sql_one, 2 /*nargs*/);  /*[ {}, Sql protoObj-->{exe:fn_exe} fn_eval ]*/
    duk_put_prop_string(ctx, -2, "one");                    /*[ {}, Sql protoObj-->{exe:fn_exe,eval:fn_eval} ]*/

    /* set Sql.connection.prototype.close */
    duk_push_c_function(ctx, rp_sql_close, 0 /*nargs*/); /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_close ] */
    duk_put_prop_string(ctx, -2, "close");                   /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close} ] */

    /* set Sql.connection.prototype.set */
    duk_push_c_function(ctx, rp_texis_set, 1 /*nargs*/);   /* [ {}, Sql protoObj-->{exe:fn_exe,...} fn_set ] */
    duk_put_prop_string(ctx, -2, "set");                    /* [ {}, Sql protoObj-->{exe:fn_exe,query:fn_exe,close:fn_close,set:fn_set} ] */

    /* set Sql.connection.prototype.reset */
    duk_push_c_function(ctx, rp_texis_reset, 0);
    duk_put_prop_string(ctx, -2, "reset");

    /* set Sql.connection.prototype.importCsvFile */
    duk_push_c_function(ctx, rp_sql_import_csv_file, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsvFile");

    /* set Sql.connection.prototype.importCsv */
    duk_push_c_function(ctx, rp_sql_import_csv_str, 4 /*nargs*/);
    duk_put_prop_string(ctx, -2, "importCsv");

    duk_push_c_function(ctx, rp_sql_addtable, 1);
    duk_put_prop_string(ctx, -2, "addTable");

    /* set Sql.connection.prototype.scheduleUpdate */
    duk_eval_string(ctx, schupd);
    duk_put_prop_string(ctx, -2, "scheduleUpdate");

    /* Set Sql.connection.prototype = protoObj */
    duk_put_prop_string(ctx, -2, "prototype"); /* -> stack: [ {}, constructor-->[prototype-->{exe=fn_exe,...}] ] */
    duk_dup(ctx, -1);
    duk_put_prop_string(ctx, -3, "connection");/* [ {connection()} ] */
    duk_put_prop_string(ctx, -2, "init");      /* depricated: [ {init()} ] */

    /* shortcut for new sql.connection() */
    duk_push_c_function(ctx, rp_sql_connect, 2);
    duk_put_prop_string(ctx, -2, "connect");

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

    /* used when forking/execing */
    duk_push_string(ctx, "__helper");
    duk_push_c_function(ctx, fork_helper, 3);
    duk_def_prop(ctx, -3, DUK_DEFPROP_CLEAR_WEC|DUK_DEFPROP_HAVE_VALUE);

    add_exit_func(free_all_handles, NULL);

    return 1;
}
