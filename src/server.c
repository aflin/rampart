#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>// for syscall(SYS_gettid)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>

#include "duktape.h"
#include "duk_cmdline.h"
#include "evhtp/evhtp.h"
#include "rp.h"
#include "mime.h"


//#define RP_TIMEO_DEBUG

#ifdef RP_TIMEO_DEBUG
#define  debugf(...)  printf(__VA_ARGS__);
#else
#define debugf(...)    /* Do nothing */
#endif


pthread_mutex_t ctxlock;

static int gl_threadno=0;
static int gl_singlethreaded=1;

duk_context **thread_ctx=NULL, *main_ctx;

int totnthreads=0;


/* DEBUGGING MACROS */

static const char * method_strmap[] = {
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "MKCOL",
    "COPY",
    "MOVE",
    "OPTIONS",
    "PROPFIND",
    "PROPATCH",
    "LOCK",
    "UNLOCK",
    "TRACE",
    "CONNECT",
    "PATCH",
    "UNKNOWN",
};

/* IPC Pipe info for parent/child and child pid */

#define FORKINFO struct fork_info_s
FORKINFO {
    int par2child;
    int child2par;
    pid_t childpid;    
};

FORKINFO **forkinfo=NULL;

/* a duk http request struct
    keeps info about request, callback,
    timeout and thread number for thread specific info
*/
#define DHS struct duk_http_s
DHS {
  duk_idx_t func_idx;      // location of the callback
  duk_context *ctx;        // duk context for this thread
  evhtp_request_t *req;    // the evhtp request struct for the current request
  struct timeval timeout;  // timeout for the duk script
  int threadno;
};

/* mapping for url to filesystem */
#define DHMAP struct fsmap_s
DHMAP {
    DHS *dhs;
    char *key;
    char *val;
};


static void setheaders(evhtp_request_t * req)
{
    time_t now = time(NULL);
    if (now != -1) {
        struct tm *ptm = gmtime(&now);
        char buf[128];
        /* Thu, 21 May 2020 01:41:20 GMT */
        strftime(buf, 128, "%a, %d %b %Y %T GMT", ptm);
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Date",buf, 0, 1));
    }
    //TODO: Discover why this breaks everything.
    //evhtp_request_set_keepalive(req, 1);
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","close",0,0));
}

#define putval(key,val) if((char *)(val)!=(char*)NULL){duk_push_string(ctx,(const char *)(val));duk_put_prop_string(ctx,-2,(key));} 

int putkvs(evhtp_kv_t *kv, void *arg)
{
    char *v, *k;
    duk_context *ctx=(duk_context*)arg;

    v=duk_rp_url_decode(kv->val,(int)kv->vlen);
    k=duk_rp_url_decode(kv->key,(int)kv->klen);
    putval(k,v);

    free(k);
    free(v);

    return 0;
}

static int putheaders(evhtp_kv_t *kv, void *arg)
{
    duk_context *ctx=(duk_context*)arg;

    duk_push_lstring(ctx,kv->val,(duk_size_t)kv->vlen);
    duk_put_prop_lstring(ctx,-2,kv->key,(duk_size_t)kv->klen);

    return 0;
}

void sa_to_string(void *sa, char *buf, size_t bufsz)
{
    if(((struct sockaddr*)sa)->sa_family==AF_INET6)
        inet_ntop(AF_INET6, &((struct sockaddr_in6*)sa)->sin6_addr, buf, bufsz);    
    else
        inet_ntop(AF_INET, &((struct sockaddr_in*)sa)->sin_addr, buf, bufsz);
}

/* copy request vars from evhtp to duktape */

void push_req_vars(DHS *dhs)
{
    duk_context *ctx=dhs->ctx;
    evhtp_path_t *path=dhs->req->uri->path;
    evhtp_authority_t *auth=dhs->req->uri->authority;
    int method = evhtp_request_get_method(dhs->req);
    evhtp_connection_t *conn = evhtp_request_get_connection(dhs->req);
    void *sa=(void *)conn->saddr;
    char address[INET6_ADDRSTRLEN], *q, *cl;
    duk_size_t bsz;
    void *buf;

    int i,l;
    /* get ip address */
    sa_to_string(sa,address,sizeof(address));
    putval("ip",address);

    /* and port */
    duk_push_int(ctx,(int)ntohs(((struct sockaddr_in*)sa)->sin_port));
    duk_put_prop_string(ctx,-2,"port");

    /*method */
    if (method >= 16)
        method = 16;
    putval("method",method_strmap[method]);
    
    /* path info */
    duk_push_object(ctx);
    putval("file",path->file);
    putval("full",path->full);
    putval("path",path->path);
    duk_put_prop_string(ctx,-2,"path");

    /*  Auth */
    duk_push_object(ctx);
    putval("user",auth->username);
    putval("pass",auth->password);
    putval("hostname",auth->hostname);
    duk_push_int(ctx,(duk_int_t)auth->port);
    duk_put_prop_string(ctx,-2,"port");
    duk_put_prop_string(ctx,-2,"authority");

    /* fragment -- portion after # */
    putval("fragment",(char*)dhs->req->uri->fragment);

    /* query string */
    duk_push_object(ctx);
    evhtp_kvs_for_each(dhs->req->uri->query,putkvs,ctx);
    duk_put_prop_string(ctx,-2,"query");

    bsz=(duk_size_t)evbuffer_get_length(dhs->req->buffer_in);
    buf=duk_push_fixed_buffer(dhs->ctx,bsz);
    evbuffer_copyout(dhs->req->buffer_in,buf,(size_t)bsz);
    duk_put_prop_string(ctx,-2,"body");

    /* unfortunately this is a copy of data in buffer plus an extra \0 at the end */
    q=(char *)dhs->req->uri->query_raw;
    if(q)
    {
        i=(int)strlen(q);
        cl=(char*)evhtp_kv_find(dhs->req->headers_in,"Content-Length");
        if(cl)
        {
            l=(int)strtol( cl, NULL, 10);
            if(i==l){
                putval("query_raw",q);
            } else {
                /* check that our buffer length matches "Content-Length" */
                i=(int)evbuffer_get_length (dhs->req->buffer_in);
                if(i==l) {
                    putval("query_raw","binary");
                    duk_push_external_buffer(ctx);
                    duk_config_buffer(ctx, -1, q, l);
                    duk_put_prop_string(ctx,-2,"binary");
                } /* TODO: else ????? */
            }
        }
        else
            putval("query_raw",dhs->req->uri->query_raw);
    }

    /*  headers */
    duk_push_object(ctx);
    evhtp_headers_for_each(dhs->req->headers_in,putheaders,ctx);
    duk_put_prop_string(ctx,-2,"headers");
}

static void send500(evhtp_request_t * req, char *msg)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char fmt[]="<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p>%s</p></body></html>";
    evbuffer_add_printf(req->buffer_out,fmt,msg);
    evhtp_send_reply(req, 500);
}

static void send404(evhtp_request_t * req)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char msg[]="<html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";
    evbuffer_add(req->buffer_out,msg,strlen(msg));
    evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
}

static void send403(evhtp_request_t * req)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char msg[]="<html><head><title>403 Forbidden</title></head><body><h1>Forbidden</h1><p>The requested URL is Forbidden.</p></body></html>";
    evbuffer_add(req->buffer_out,msg,strlen(msg));
    evhtp_send_reply(req, 403);
}

static void dirlist(evhtp_request_t * req, char *fn)
{
    send403(req);
}

static int getrange(evhtp_kv_t *kv, void *arg)
{
    char **range=arg;
    
    if(!strncasecmp("range",kv->key,(int)kv->klen))
        *range=strndup(kv->val,kv->vlen);
    
    return 0;
}


static void ht_sendfile(evhtp_request_t * req, char *fn, size_t filesize)
{
    RP_MTYPES m;
    RP_MTYPES *mres, *m_p=&m;
    char *ext,*range=NULL;
    int fd = -1;
    ev_off_t beg=0, len=-1;
    evhtp_res rescode=EVHTP_RES_OK;

    if ((fd = open(fn, O_RDONLY)) == -1) {
        send404(req);
        return;
    }

    ext=strrchr(fn,'.');

    if (!ext) /* || strchr(ext, '/')) shouldn't happen */
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/octet-stream", 0, 0));
    else
    {
        m.ext=ext+1;
        /* look for proper mime type listed in mime.h */
        mres=bsearch(m_p,rp_mimetypes,nRpMtypes,sizeof(RP_MTYPES),compare_mtypes);
        if(mres)
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));
        else
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/octet-stream", 0, 0));
    }

    /* http range - give back partial file, set 206 */
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Accept-Ranges", "bytes", 0, 0));
    /* Content-Range: bytes 12812288-70692914/70692915 */

    evhtp_headers_for_each(req->headers_in,getrange,&range);
    if(range && !strncasecmp("bytes=",range,6)){
        char *eptr;
        char reprange[128];

        beg=(ev_off_t)strtol( range+6, &eptr, 10 );
        if (eptr!=range+6) {
            ev_off_t endval;
            
            eptr++;// skip '-'
            if(*eptr!='\0')
            {
                endval=(ev_off_t)strtol (eptr, NULL, 10 );
                if(endval && endval>beg) len=endval-beg;
            }
            rescode=206;
            snprintf(reprange,128,"bytes %d-%d/%d",
                (int) beg,
                (int) ( (len==-1) ? (filesize-1) : endval),
                (int) filesize);
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Range", reprange, 0, 1));
        }
    }

    free(range);/* chicken 🐣 */

    evbuffer_add_file(req->buffer_out, fd, beg, len);    
    evhtp_send_reply(req, rescode);
}

static void sendredir(evhtp_request_t * req, char *fn)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("location", fn, 0, 1));
    evbuffer_add_printf(req->buffer_out,"<!DOCTYPE html>\n\
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n\
<head>\n\
<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>\n\
<title>302 Moved Temporarily</title>\n\
<style type=\"text/css\"><!--\n\
body, td, th, span { font-family: Geneva,Arial,Helvetica; }\n\
--></style>\n\
</head>\n\
<body>\n\
<h2>302 Moved Temporarily</h2>\n\
<p>The actual URL is:</p>\n\
<p><b><a href=\"%s\">%s</a></b></p>\n\
</body>\n\
</html>",fn,fn);
    evhtp_send_reply(req, EVHTP_RES_FOUND);
}

static void
fileserver(evhtp_request_t * req, void * arg)
{
    DHMAP *map=(DHMAP*)arg;
    DHS *dhs=map->dhs;
    evhtp_path_t *path=req->uri->path;
    struct stat sb;
    /* take 2 off of key for the slash and * and add 1 for '\0' and one more for a potential '/' */
    char fn[strlen(map->val)+strlen(path->full)+4-strlen(map->key)];
    mode_t mode;
    int i=0;

    dhs->req=req;
    
    strcpy(fn,map->val);
    strcpy(&fn[strlen(map->val)],path->full+strlen(map->key)-1);
    if (stat(fn, &sb) == -1) {
        /* 404 goes here */
        send404(req);
        return;
    }
    mode=sb.st_mode&S_IFMT;

    if (mode == S_IFREG)
        ht_sendfile(req,fn,sb.st_size);
    else if (mode == S_IFDIR)
    {
        /* add 11 for 'index.html\0' */
        char fnindex[strlen(fn)+11];
        /* redirect if directory doesn't end in '/' */
        i=strlen(fn)-1;
        if (fn[i]!='/'){
            fn[++i]='/';
            fn[++i]='\0';
            sendredir(req,&fn[strlen(map->val)-1]);
            return;
        }
        strcpy(fnindex,fn);
        strcat(fnindex,"index.html");
        if (stat(fnindex, &sb) == -1) {
            /* TODO: add dir listing and
               setting to forbid dir listing */
            /* try index.htm */
            fnindex[strlen(fnindex)-1]='\0';
            if (stat(fnindex, &sb) == -1)
                dirlist(req,fn);
            else
                ht_sendfile(req,fnindex,sb.st_size);
        }
        else
            ht_sendfile(req,fnindex,sb.st_size);
    }
    else
        send404(req);
}

#define CTXFREER struct ctx_free_buffer_data_str

CTXFREER {
    duk_context *ctx;
    int threadno;
};

/* free the stolen duk buffer after evbuffer is done with it */
static void refcb(const void *data, size_t datalen, void *val)
{
    CTXFREER *fp=val;
    duk_context *ctx=fp->ctx;
    int threadno=(int)fp->threadno;

    /* check if the entire stack was already freed and replaced in redo_ctx */
    if (ctx==thread_ctx[threadno])
        duk_free(ctx,(void *)data);
    free(fp);
}

static void sendbuf(DHS *dhs)
{
    duk_size_t sz;
    const char *s;
    CTXFREER *freeme=NULL;

    REMALLOC(freeme,sizeof(CTXFREER));
    s=duk_get_string_default(dhs->ctx,-1,"  ");
    if(*s=='\\' && *(s+1)=='@')
    {
        s++;
        duk_push_string(dhs->ctx,s);
        duk_replace(dhs->ctx,-2);
    }

    /* turn whatever the return val is into a buffer, 
       then steal the allocation and pass it to evbuffer
       without copying.  duktape will not free it
       when we duk_pop().
    */
    duk_to_dynamic_buffer(dhs->ctx,-1,NULL);
    s=duk_steal_buffer(dhs->ctx, -1, &sz);
    /* add buffer to response, and specify callback to free s when done */
    /* note that dhs and ctx might not exist by the time the callback is executed */
    freeme->ctx=dhs->ctx;
    freeme->threadno=dhs->threadno;
    evbuffer_add_reference(dhs->req->buffer_out,s,(size_t)sz,refcb,freeme);

    /* with copy - this version needs a check for databuffer
    s=duk_get_lstring(dhs->ctx,1,&sz);
    evbuffer_add(dhs->req->buffer_out,s,(size_t)sz);
    */
}

static evhtp_res sendobj(DHS *dhs)
{

    RP_MTYPES m;
    RP_MTYPES *mres;
    int gotdata=0,gotct=0;;
    duk_context *ctx=dhs->ctx;
    evhtp_res res=200;

    if(duk_get_prop_string(ctx, -1, "headers"))
    {
        if (duk_is_object(ctx,-1)  && !duk_is_array(ctx,-1) && !duk_is_function(ctx,-1))
        {
            duk_enum(ctx, -1, DUK_ENUM_SORT_ARRAY_INDICES);
            while (duk_next(ctx, -1 , 1 ))
            {
                const char *key=duk_require_string(ctx,-2);
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key,duk_to_string(ctx,-1), 1, 1));
                if(!strcasecmp(key,"content-type")) gotct=1;
                duk_pop_2(ctx);
            }
            duk_pop(ctx); /* pop enum and headers obj */
            duk_del_prop_string(ctx, -2, "headers");
        }
        else
        {
            duk_pop(ctx); /* get rid of 'headers' non object */
            duk_del_prop_string(ctx, -1, "headers");
            duk_push_string(ctx,"headers option in return value must be set to an object (headers:{...})");
            duk_throw(ctx);
        }
    }
    duk_pop(ctx);
    

    if(duk_get_prop_string(ctx, -1, "status"))
    {
        res=(evhtp_res)duk_require_number(ctx,-1);
        if(res<100) res=200;
        duk_del_prop_string(ctx, -2, "status");
    }
    duk_pop(ctx);
    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1 , 1 ))
    {
        if(gotdata) goto opterr;

        m.ext=(char*)duk_to_string(ctx,-2);
        mres=bsearch(&m,rp_mimetypes,nRpMtypes,sizeof(RP_MTYPES),compare_mtypes);
        if(mres) 
        {
            const char *d;
            gotdata=1;
            if(!gotct)
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));

            /* if data is a string and starts with '@', its a filename */
            if (duk_is_string(ctx,-1) && ( (d=duk_get_string(ctx,-1))||1) && *d=='@' )
            {
                mode_t mode;
                struct stat sb;
                const char *fn=d+1;
                if (stat(fn, &sb) == -1) {
                    /* 404 goes here */
                    send404(dhs->req);
                    return (0);
                }
                mode=sb.st_mode&S_IFMT;
                if (mode == S_IFREG)
                {
                    int fd = -1;
                    
                    if ((fd = open(fn, O_RDONLY)) == -1) {
                        send404(dhs->req);
                        return(0);
                    }
                    evbuffer_add_file(dhs->req->buffer_out, fd, 0, -1);
                }
                else
                {
                    send404(dhs->req);
                    return(0);
                }
            }
            else
                sendbuf(dhs); /* send buffer or string contents at idx=-1 */

            duk_pop_2(ctx);
            continue;
        }
        else
        {
           const char *bad_option=duk_to_string(ctx,-2);
           char estr[20+strlen(bad_option)];
           fprintf(stderr,"unknown option '%s'\n",bad_option);
           sprintf(estr,"unknown option '%s'",bad_option);
           send500(dhs->req,estr);
           return(0);
        }
        
        opterr:
        {
            fprintf(stderr,"Data already set from '%s', '%s=\"%s\"' is redundant.\n",m.ext, duk_to_string(ctx,-2), duk_to_string(ctx,-1));
            return(res);
        }
    }
    duk_pop(ctx);
    return(res);
}

/* ******************************************
    FUNCTIONS FOR COPYING BETWEEN DUK HEAPS
********************************************* */

static int copy_obj(duk_context *ctx,duk_context *tctx, int objid);
static void clean_obj(duk_context *ctx, duk_context *tctx);

/* copy a single function given as callback to all stacks 
   To be called multiple times for each function on the 
   stack, and must be called in order of functions appearance
   on main_ctx stack
*/
static void copy_cb_func(DHS *dhs, int nt) {
    void *bc_ptr;
    duk_size_t bc_len;
    int i=0;
    duk_context *ctx=dhs->ctx;

    duk_get_prop_string(ctx,dhs->func_idx,"name");
    if(!strncmp(duk_get_string(ctx,-1),"bound ",6))
        printf("Error: server cannot copy a bound function to threaded stacks\n");
    duk_pop(ctx);

    if (     duk_get_prop_string(ctx,dhs->func_idx,DUK_HIDDEN_SYMBOL("is_global"))
          && duk_get_boolean(ctx,-1) 
       )
    {
        const char *name;
        duk_pop(ctx); //true


        if( duk_get_prop_string(ctx,dhs->func_idx,"name") )
        {
            name=duk_get_string(ctx,-1);
            duk_pop(ctx);//name string
            
            for(i=0;i<nt;i++)
            {
                duk_context *tctx=thread_ctx[i];
                duk_get_global_string(tctx,name); //put already copied function on stack
                duk_push_string(tctx,name);  //add fname property to function
                duk_put_prop_string(tctx,-2,"fname");
                duk_insert(tctx,dhs->func_idx); //put it in same position on stack as main_ctx
                /*if(!i)
                    dhs->func_idx=duk_get_top_index(tctx);*/
            }
            duk_push_string(ctx,name);  //add fname property to function in ctx as well
            duk_put_prop_string(ctx,dhs->func_idx,"fname");
            return; /* STOP HERE */
        }
    }
    duk_pop(ctx);

    /* CONTINUE HERE */
    /* if function is not global (i.e. map:{"/":function(req){...} } */
    duk_dup(ctx,dhs->func_idx);
    duk_dump_function(ctx);
    bc_ptr = duk_require_buffer_data(ctx, -1, &bc_len);
    duk_dup(ctx,dhs->func_idx); /* on top of stack for copy_obj */
    /* load function into each of the thread contexts at same position */
    for(i=0;i<nt;i++)
    {
        duk_context *tctx=thread_ctx[i];
        void *buf = duk_push_fixed_buffer(tctx, bc_len);
        memcpy(buf, (const void *) bc_ptr, bc_len);
        duk_load_function(tctx);
        copy_obj(ctx,tctx,0);
        clean_obj(ctx,tctx);
        duk_insert(tctx,dhs->func_idx); //put it in same position on stack as main_ctx
        /* 
        if(!i)
            dhs->func_idx=duk_get_top_index(tctx);
        */
    }
    duk_pop_2(ctx);
}

/* get bytecode from stash and execute it with 
   js arguments to this function                */
duk_ret_t duk_rp_bytefunc(duk_context *ctx)
{
    const char *name;

    duk_push_current_function(ctx);
    duk_get_prop_string(ctx,-1,"fname");//we stored function name here
    name=duk_get_string(ctx,-1);
    duk_pop_2(ctx);

    duk_push_global_stash(ctx);  //bytecode compiled function was previously stored under "name" in stash
    duk_get_prop_string(ctx,-1,name); //get the bytecode compiled function
    duk_insert(ctx,0); //put the bytecode compiled function at beginning
    duk_pop(ctx); //pop global stash
    duk_push_this(ctx);
    duk_insert(ctx,1);
    duk_call_method(ctx,duk_get_top_index(ctx)-1); //do it    
    return 1;
}

/* copy function compiled from bytecode to stash
   create a function with the proper name that will
   call duk_rp_bytefunc() to execute.
   
   duktape does not allow bytecode compiled function
   to be pushed to global object afaik
*/ 
static void copy_bc_func(duk_context *ctx,duk_context *tctx)
{
    void *buf,*bc_ptr;
    duk_size_t bc_len;
    const char *name=duk_get_string(ctx,-2);
    duk_dup_top(ctx);
    duk_dump_function(ctx); //dump function to bytecode
    bc_ptr = duk_require_buffer_data(ctx, -1, &bc_len);//get pointer to bytecode
    buf = duk_push_fixed_buffer(tctx, bc_len); //make a buffer in thread ctx
    memcpy(buf, (const void *) bc_ptr, bc_len); //copy bytecode to new buffer
    duk_pop(ctx); //pop bytecode from ctx

    duk_load_function(tctx); //make copied bytecode in tctx a function
    duk_push_global_stash(tctx); //get global stash and save function there.
    duk_insert(tctx,-2);
    duk_put_prop_string(tctx,-2,name);
    duk_pop(tctx);

    duk_push_c_function(tctx,duk_rp_bytefunc,DUK_VARARGS); //put our wrapper function on stack
    duk_push_string(tctx,name);  // add desired function name (as set in global stash above)
    duk_put_prop_string(tctx,-2,"fname");
}

static void copy_prim(duk_context *ctx,duk_context *tctx, const char *name)
{

    if (strcmp(name,"NaN")==0) return;
    if (strcmp(name,"Infinity")==0) return;
    if (strcmp(name,"undefined")==0) return;

    switch(duk_get_type(ctx,-1))
    {
        case DUK_TYPE_NULL:
            duk_push_null(tctx);break;
        case DUK_TYPE_BOOLEAN:
            duk_push_boolean(tctx,duk_get_boolean(ctx,-1));break;
        case DUK_TYPE_NUMBER:
            duk_push_number(tctx,duk_get_number(ctx,-1));break;
        case DUK_TYPE_STRING:
            duk_push_string(tctx,duk_get_string(ctx,-1));break;
        default:
            duk_push_undefined(tctx);
    }
    duk_put_prop_string(tctx,-2,name);
}

static void clean_obj(duk_context *ctx, duk_context *tctx)
{
    const char *prev=duk_get_string(ctx,-2);
    /* prototype was not marked, so we need to dive into it regardless */
    if(duk_get_prop_string(ctx,-1,DUK_HIDDEN_SYMBOL("objRefId")) || ( prev && !strcmp(prev,"prototype")) )
    {
        duk_del_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("objRefId"));
        duk_enum(ctx,-2,DUK_ENUM_INCLUDE_HIDDEN);
        while(duk_next(ctx,-1,1))
        {
            if (duk_is_object(ctx,-1) || duk_is_c_function(ctx,-1))
                clean_obj(ctx,NULL);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);        
    if(tctx)
    {
        duk_push_global_stash(tctx);
        duk_del_prop_string(tctx,-1,"objById");
        duk_pop(tctx);
    }
}

typedef duk_ret_t (*duk_func)(duk_context *);

//#define cprintf(...)  printf(__VA_ARGS__);
#define cprintf(...) /* nada */

/* ctx object and tctx object should be at top of stack */
static int copy_obj(duk_context *ctx,duk_context *tctx, int objid)
{
    const char *s;
    objid++;
    const char *prev=duk_get_string(ctx,-2);

    /************** dealing with circular/duplicate references ******************************/
    /* don't copy prototypes, they will match the return from "new object();" and not be new anymore */
    if(prev && !strcmp(prev,"prototype"))
        goto enum_object; 

    cprintf("IN COPYOBJ %d\n",objid);
    /* check if we've seen this object before */
    if(duk_get_prop_string(ctx,-1,DUK_HIDDEN_SYMBOL("objRefId")))
    {                                                // [ [par obj], [ref_objid] ]
        /* get previously seen object (as previously copied to tctx, and return its reference */
        int ref_objid=duk_get_int(ctx,-1);
        cprintf("looking for cached object #%d\n",ref_objid);
        duk_pop(ctx);                                // [ [par obj] ]
        cprintf("Circular/Duplicate Reference to %s Detected\n",duk_get_string(ctx,-2));
        // replace the copied object on top of tctx stack with the reference
        duk_push_global_stash(tctx);                 // [ [par obj], [global stash] ]
        if(!duk_get_prop_string(tctx,-1,"objById"))  // [ [par obj], [global stash], [object|undef] ]
        {
            cprintf("big time error: this should never happen\n");
            duk_pop_2(tctx);                         // [ [par obj] ] 
            return(objid);                           // leave empty object there
        }
        duk_push_sprintf(tctx,"%d", ref_objid);          //[ [par obj], [global stash], [stash_obj], [objid] ]
        duk_get_prop(tctx,-2);                       //[ [par obj], [global stash], [stash_obj], [obj_ref] ]
        //printenum(tctx,-1);
        duk_insert(tctx,-4);                         //[ [obj_ref], [par obj], [global stash], [stash_obj] ]
        duk_pop_3(tctx);                             //[ [obj_ref] ]
        cprintf("copied ref\n");
        return(objid);                               // stop recursion here.
    }
    duk_pop(ctx);

    /* if we get here, we haven't seen this object before so
       we label this object with unique number */
    duk_push_int(ctx,objid);
    duk_put_prop_string(ctx,-2, DUK_HIDDEN_SYMBOL("objRefId"));
    /* in tctx, put it where we can find it later */
                                                 //[ [par obj] ]
    duk_push_global_stash(tctx);                 //[ [par obj], [global stash] ]
    if(!duk_get_prop_string(tctx,-1,"objById"))  //[ [par obj], [global stash], [object|undef] ]
    {                                            //[ [par obj], [global stash], [undefined] ]
        duk_pop(tctx);                           //[ [par obj], [global stash] ]
        duk_push_object(tctx);                   //[ [par obj], [global stash], [new_stash_obj] ]
    }
    /* copy the object on the top of stack before 
       entering copy_obj */
    cprintf("assigning object id=%d\n",objid);
    duk_push_sprintf(tctx,"%d", objid);          //[ [par obj], [global stash], [stash_obj], [objid] ]
    duk_dup(tctx,-4);                            //[ [par obj], [global stash], [stash_obj], [objid], [copy of par obj] ]
    duk_put_prop(tctx,-3);			 //[ [par obj], [global stash], [stash_obj->{objid:copy of par obj} ]
    duk_put_prop_string(tctx,-2,"objById");      //[ [par obj], [global stash->{"objById":[stash_obj]} ]
    duk_pop(tctx);                               //[ [par obj] ]    
    /************** END dealing with circular references ***************************/


//    cprintf("%s type %d (obj=%d) cfunc=%d, func=%d, obj=%d, obj already in tctx=%d\n",((prev)?prev:"null"),duk_get_type(ctx,-1), DUK_TYPE_OBJECT,
//            duk_is_c_function(ctx,-1), duk_is_function(ctx,-1), duk_is_object(ctx,-1), duk_has_prop_string( tctx,-1,s ));

    enum_object:

    /*  get keys,vals inside ctx object on top of the stack 
        and copy to the tctx object on top of the stack     */ 
    duk_enum(ctx,-1,DUK_ENUM_INCLUDE_HIDDEN);
    while(duk_next(ctx,-1,1))
    {
        s=duk_get_string(ctx,-2);
        if(duk_is_ecmascript_function(ctx,-1))
        {
            /* turn ecmascript into bytecode and copy */
            copy_bc_func(ctx,tctx);
            /* recurse and copy JS properties attached to this duktape (but really c) function */
            objid=copy_obj(ctx,tctx,objid);

            if(!prev) // prev==NULL means this is a global
            {
                /* mark functions that are global as such to copy
                   by reference when named in a server.start({map:{}}) callback */
                duk_push_true(ctx);
                duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("is_global"));
            }
            duk_put_prop_string(tctx,-2,s);
        }

        else if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_STRING|DUK_TYPE_MASK_NUMBER|DUK_TYPE_MASK_BOOLEAN|DUK_TYPE_MASK_NULL|DUK_TYPE_MASK_UNDEFINED)) 
        {
            /* simple copy of primitives */
            copy_prim(ctx,tctx,s);
        }

        else if (duk_is_c_function(ctx,-1) && ! duk_has_prop_string( tctx,-1,s ) )
        {
            /* copy pointers to c functions */
            duk_idx_t length;
            duk_func copyfunc = (duk_func)duk_get_c_function(ctx,-1);

            if(duk_get_prop_string(ctx,-1,"length"))
                length=(duk_idx_t)duk_get_int(ctx,-1);   
            else
                length=DUK_VARARGS;
            duk_pop(ctx);

            duk_push_c_function(tctx,copyfunc,length);
            /* recurse and copy JS properties attached to this c function */
            objid=copy_obj(ctx,tctx,objid);
            duk_put_prop_string(tctx,-2,s);
        }

        else if (duk_is_object(ctx,-1) && !duk_is_function(ctx,-1) && !duk_is_c_function(ctx,-1) )
        {
            /* copy normal {} objects */
            if ( ! duk_has_prop_string( tctx,-1,s ) 
                 &&
                 strcmp(s, "console" )!=0
                 &&
                 strcmp(s, "performance" )!=0
               )
            {
                cprintf("copy %s{}\n",s);
                /* recurse and begin again with this ctx object (at idx:-1)
                   and a new empty object for tctx (pushed to idx:-1)              */
                duk_push_object(tctx);
                objid=copy_obj(ctx,tctx,objid);
                duk_put_prop_string(tctx, -2, duk_get_string(ctx,-2) );
            }
        }

        /* remove key and val from stack and repeat */
        duk_pop_2(ctx);
    }

    /* remove enum from stack */
    duk_pop(ctx);

    /* keep count */
    return objid;
}

static void copy_all(duk_context *ctx,duk_context *tctx)
{
    duk_push_global_object(ctx);
    duk_push_global_object(tctx);
    /* copy_obj expects objects on top of both stacks */
    copy_obj(ctx,tctx,0);
    /* remove hidden symbols and stash-cache used for reference tracking */
    clean_obj(ctx,tctx);
    /* remove global object from both stacks */
    duk_pop(ctx);
    duk_pop(tctx);
}
/* ******************************************
  END FUNCTIONS FOR COPYING BETWEEN DUK HEAPS
********************************************* */

static evthr_t *get_request_thr(evhtp_request_t * request) 
{
    evhtp_connection_t * htpconn;

    htpconn = evhtp_request_get_connection(request);

    return htpconn->thread;
}

/* struct to pass to http_dothread
   containing all relevant vars */
#define DHR struct duk_http_req_s
DHR{
    evhtp_request_t * req;
    DHS   *dhs;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int have_timeout;
#ifdef RP_TIMEO_DEBUG
    pthread_t par;
#endif
};


/*  CALLBACK LOGIC:

    First call of any js function:
      http_callback 
         |-> http_fork_callback -> check for threadsafe

    Subsequent callbacks:
      http_callback
      |-> http_thread_callback -> for thread safe js functions
      |   |-> http_dothread -> Call js func. If there is a timeout set, this function will be threaded
      or
      |-> http_fork_callback -> currently just for sql js functions

    The first callback of any function is run through http_fork_callback.
    If not marked as thread-unsafe (by setting a global variable in the forked child), 
    it will subsequently be run in a thread and the child/fork will be idle.
    Otherwise, if the global variable is set upon calling the function, it will always be 
    run through the forked child. Currently only the sql engine does this.

    Forking is used because although the sql engine can be safely run with mutexes,
    doing so is slower than using its native semaphore locks, which allow for concurrent reads.
    On a six core processor using 12 threads, it is over 10x faster for read only operations on a
    large text index.  However for very simple (e.g. select one row from a table with no "where" clause)
    forking will be slower than if it was done with threads.

    The forked children (one per thread) are only forked once.  They stick around and communicate by IPC/Pipes,
    waiting for future requests.  If a script times out, the child is killed and a new child is forked upon
    the next request.
*/

static void *http_dothread(void *arg)
{
    DHR *dhr=(DHR*)arg;
    evhtp_request_t *req = dhr->req;
    DHS *dhs = dhr->dhs;
    evhtp_res res=200;
    int eno;

#ifdef RP_TIMEO_DEBUG
    pthread_t x=dhr->par;
#endif
    if(dhr->have_timeout) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);

    /* copy function ref to top of stack */
    duk_dup(dhs->ctx,dhs->func_idx);
    /* push an empty object */
    duk_push_object(dhs->ctx);
    /* populate object with details from client */
    push_req_vars(dhs);

    /* execute function "myfunc({object});" */
    if( (eno=duk_pcall(dhs->ctx,1)) ) 
    {
        char msg[]="<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>%s</pre></p></body></html>";
        if(dhr->have_timeout)
        {
            pthread_mutex_lock(&(dhr->lock));
            pthread_cond_signal(&(dhr->cond));
        }

        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
        if(duk_is_error(dhs->ctx,-1) || duk_is_string(dhs->ctx,-1)){
            duk_get_prop_string(dhs->ctx, -1, "stack");
            evbuffer_add_printf(req->buffer_out,msg,duk_safe_to_string(dhs->ctx, -1));
            printf("error in thread '%s'\n", duk_safe_to_string(dhs->ctx, -1));
            duk_pop(dhs->ctx);
        } else {
            evbuffer_add_printf(req->buffer_out,msg,"unknown error");
        }
        evhtp_send_reply(req, 500);

        if(dhr->have_timeout) pthread_mutex_unlock(&(dhr->lock));
 
        return NULL;
    }

    if(dhr->have_timeout)
    {
        debugf("0x%x, LOCKING in thread_cb\n",(int)x);fflush(stdout);
        pthread_mutex_lock(&(dhr->lock));
        pthread_cond_signal(&(dhr->cond));
    }
    /* stack now has return value from duk function call */    

    /* set some headers */
    setheaders(req);

    /* don't accept functions or arrays */
    if(duk_is_function(dhs->ctx,-1) || duk_is_array(dhs->ctx,-1) ){
        duk_push_string(dhs->ctx,"Return value cannot be an array or a function");
        duk_throw(dhs->ctx);
    }
    /* object has reply data and options in it */
    if(duk_is_object(dhs->ctx,-1))
        res=sendobj(dhs);
    else /* send string or buffer to client */
        sendbuf(dhs);
    
    if(res)evhtp_send_reply(req, res);

    duk_pop(dhs->ctx);

    /* logging goes here ? */

    debugf("0x%x, UNLOCKING in thread_cb\n",(int)x);fflush(stdout);
    if(dhr->have_timeout) pthread_mutex_unlock(&(dhr->lock));

    return NULL;
}

static duk_context *redo_ctx(int thrno) 
{
    duk_context *thr_ctx=duk_create_heap_default();
    duk_idx_t fno=0;
    void *bc_ptr, *buf;
    duk_size_t bc_len;
    duk_init_userfunc(thr_ctx);

    /* copy all the functions previously stored at bottom of stack */
    if( pthread_mutex_lock(&ctxlock) == EINVAL ) {
        fprintf(stderr,"could not obtain lock in http_callback\n");
        exit(1);
    }

    copy_all(main_ctx,thr_ctx);

    /* renumber this thread in global "rampart" variable */
    if(!duk_get_global_string(thr_ctx,"rampart"))
    {
        duk_pop(thr_ctx);
        duk_push_object(thr_ctx);
    }
    duk_push_int(thr_ctx,thrno);
    duk_put_prop_string(thr_ctx,-2,"thread_id");
    duk_put_global_string(thr_ctx,"rampart");

    while (duk_is_function(main_ctx,fno))
    {
        /* check if function was already copied in copy_all() */
        if (     duk_get_prop_string(main_ctx,fno,DUK_HIDDEN_SYMBOL("is_global"))
              && duk_get_boolean(main_ctx,-1) 
           )
        {
            const char *name;
            duk_pop(main_ctx); //true

            if( duk_get_prop_string(main_ctx,fno,"name") )
            {
                name=duk_get_string(main_ctx,-1);
                duk_pop(main_ctx);//name string
                
                duk_get_global_string(thr_ctx,name); //put already copied function on stack
                duk_push_string(thr_ctx,name);  //add fname property to function
                duk_put_prop_string(thr_ctx,-2,"fname");
                duk_insert(thr_ctx,fno);
                fno++;
                continue;
            }
        }

        duk_pop(main_ctx);

        duk_dup(main_ctx,fno);
        duk_dump_function(main_ctx);
        bc_ptr = duk_require_buffer_data(main_ctx, -1, &bc_len);

        buf = duk_push_fixed_buffer(thr_ctx, bc_len);
        memcpy(buf, (const void *) bc_ptr, bc_len);
        duk_load_function(thr_ctx);
        duk_pop(main_ctx);
        duk_insert(thr_ctx,fno);
        fno++;
    }


    pthread_mutex_unlock(&ctxlock);

    duk_destroy_heap(thread_ctx[thrno]);
    thread_ctx[thrno]=thr_ctx;
    return thr_ctx;
}

static void
http_thread_callback(evhtp_request_t * req, void * arg, int thrno)
{
    DHS *dhs = arg;
    DHR *dhr=NULL;
    pthread_t script_runner;
    pthread_attr_t attr;
    struct timespec ts;
    struct timeval now;
    int ret=0;

//pid_t x = syscall(SYS_gettid);
#ifdef RP_TIMEO_DEBUG
    pthread_t x=pthread_self();
    printf("%d, start\n",(int)x);fflush(stdout);
#endif

    gettimeofday(&now,NULL);
    DUKREMALLOC(dhs->ctx,dhr,sizeof(DHR));
    dhr->dhs=dhs;
    dhr->req=req;
#ifdef RP_TIMEO_DEBUG
    dhr->par=x;
#endif
    /* if no timeout, not necessary to thread out the callback */
    if(dhs->timeout.tv_sec==RP_TIME_T_FOREVER)
    {
        debugf("no timeout set");
        dhr->have_timeout=0;        
        (void)http_dothread(dhr);
        return;
    }

    debugf("0x%x: with timeout set\n",(int)x);
    dhr->have_timeout=1;
    ts.tv_sec =now.tv_sec + dhs->timeout.tv_sec;
    ts.tv_nsec = (dhs->timeout.tv_usec + now.tv_usec) * 1000;
    

    if( pthread_mutex_init(&(dhr->lock),NULL)  == EINVAL ) {
        fprintf(stderr,"could not initialize lock in http_callback\n");
        exit(1);
    }

    pthread_cond_init(&(dhr->cond), NULL);

    /* is this necessary? https://computing.llnl.gov/tutorials/pthreads/#Joining */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    debugf("0x%x, LOCKING in http_callback\n",(int)x);    
    if( pthread_mutex_lock(&(dhr->lock)) == EINVAL ) {
        fprintf(stderr,"could not obtain lock in http_callback\n");
        exit(1);
    }

    pthread_create(&script_runner, &attr, http_dothread, dhr);
    debugf("0x%x, cond wait, UNLOCKING\n",(int)x);fflush(stdout);    

    /* unlock dhr->lock and wait for pthread_cond_signal in http_dothread */
    ret=pthread_cond_timedwait(&(dhr->cond), &(dhr->lock), &ts);    
    /* reacquire lock after timeout or condition signalled */
    debugf("0x%x, cond wait satisfied, ret=%d (==%d(ETIMEOUT)), LOCKED\n",(int)x, (int)ret, (int)ETIMEDOUT);fflush(stdout);
    
    if(ret==ETIMEDOUT) 
    {
        debugf("%d, cancelling\n",(int)x);fflush(stdout);
        pthread_cancel(script_runner);
        debugf("%d, cancelled\n",(int)x);fflush(stdout);
        dhs->ctx=redo_ctx(thrno);
        debugf("0x%x, context recreated after timeout\n",(int)x);fflush(stdout);

        duk_get_prop_string(dhs->ctx,dhs->func_idx,"fname");
        if(duk_is_undefined(dhs->ctx,-1))
            fprintf(stderr,"timeout in callback\n");
        else
            fprintf(stderr,"timeout in %s()\n",duk_to_string(dhs->ctx,-1));
        send500(req,"Timeout in Script");
        duk_pop(dhs->ctx);
    } 

    pthread_join(script_runner, NULL);
    pthread_cond_destroy(&(dhr->cond));
    free(dhr);
}
/* call with 0.0 to start, use return from first call to get elapsed */
static double
stopwatch( double dtime )
{
    static struct timeval time;

    gettimeofday(&time, NULL);

    return (
        ((double)time.tv_sec + ((double)time.tv_usec / 1e6))
        - dtime
    );

}

void rp_exit(int sig)
{
//    printf("exiting in rp_exit()\n");
    exit(0);
}

//#define tprintf(...)  printf(__VA_ARGS__);
#define tprintf(...) /*nada */

/* a callback must be run once before we can tell if it is thread safe.
   So to be safe, run every callback in a fork the first time.  If it is 
   thread safe, mark it as so and next time we know not to fork.
   A c function that is not thread safe must set the hidden global symbol
   "threadsafe"=false.  After that runs in a thread, the "threadsafe" global
   will be checked and will be set as a property on that function, with the 
   default being true (safe) if not present.
*/

static void
http_fork_callback(evhtp_request_t * req, DHS *dhs, int have_threadsafe_val)
{
    evhtp_res res=200;
    int preforked=0,child2par[2], par2child[2], eno, pidstatus;
    double cstart,celapsed;
    FORKINFO *finfo=forkinfo[dhs->threadno];
    char *cbor;
    uint32_t msgOutSz=0;

    signal(SIGPIPE, SIG_IGN); //macos
    cstart=stopwatch(0.0);
    tprintf("in fork\n");

    /* use waitpid to eliminate the possibility of getting a process that isn't our child */
    if(finfo->childpid && !waitpid(finfo->childpid,&pidstatus,WNOHANG))
    {
        preforked=1;
    }
    else
    {
        /* our first run.  create pipes and setup for fork */
        if (pipe(child2par)==-1) 
        { 
            fprintf(stderr, "child2par pipe failed\n" ); 
            return; 
        } 

        if (pipe(par2child)==-1) 
        { 
            fprintf(stderr, "par2child pipe failed\n" ); 
            return;
        } 
        /* if child died, close old handles */
        if(finfo->child2par>0)
        {
            close(finfo->child2par);
            finfo->child2par=-1;
        }
        if(finfo->par2child>0)
        {
            close(finfo->par2child);
            finfo->par2child=-1;
        }
    }


    if (!preforked)
    {
        finfo->childpid=fork();
        if (finfo->childpid < 0) 
        { 
            fprintf(stderr, "fork Failed" ); 
            finfo->childpid=0;
            return;
        }
    } 

    if (!preforked && finfo->childpid==0)
    {   /* child is forked once then talks over pipes. 
           and is killed if timed out                  */

        duk_size_t bufsz;
        struct sigaction sa = { 0 };
        evhtp_connection_t *conn = evhtp_request_get_connection(req);

        sa.sa_flags   = 0;
        sa.sa_handler = rp_exit;
        sigemptyset (&sa.sa_mask);
        sigaction (SIGUSR1, &sa, NULL);

        close(child2par[0]);
        close(par2child[1]);
        
        
        tprintf("beginning with par2child=%d, child2par=%d\n",par2child[0],child2par[1]);

        /* on first round, just forked, so we have all the request info
            no need to pipe it */
        /* copy function ref to top of stack */
        duk_dup(dhs->ctx,dhs->func_idx);

        /* push an empty object */
        duk_push_object(dhs->ctx);
        /* populate object with details from client (requires conn) */
        push_req_vars(dhs);

        /* safe to close and free here */
        evutil_closesocket(conn->sock);
        evhtp_connection_free(conn);

        do
        {
            /* In first loop, req info is on the stack since we just forked.
               In subsequent loops, req info is piped in  */
            if( (eno=duk_pcall(dhs->ctx,1)) ) 
            {
                if(duk_is_error(dhs->ctx,-1) || duk_is_string(dhs->ctx,-1) ){
                                                  
                    char msg[]="{\"headers\":{\"status\":500},\"html\":\"<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>%s</pre></p></body></html>\"}";
                    char *err;
                    if (duk_is_error(dhs->ctx,-1))
                    {
                        err=(char*)duk_safe_to_string(dhs->ctx, -1);
                    }
                    else
                    {
                        err=(char*)duk_json_encode(dhs->ctx, -1);
                        //get rid of quotes
                        err[strlen(err)-1]='\0';
                        err++;
                    }
                    duk_push_sprintf(dhs->ctx,msg,err);
                }
                else
                {
                    duk_push_string(dhs->ctx,"{\"headers\":{\"status\":500},\"html\":\"<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>unknown javascript error</pre></p></body></html>\"}");
                }
                duk_json_decode(dhs->ctx,-1);
            }
            /* encode the result of duk_pcall or the error message if fails */
            duk_cbor_encode(dhs->ctx,-1,0);
            cbor=duk_get_buffer_data(dhs->ctx,-1,&bufsz);
            msgOutSz=(uint32_t)bufsz;

            /* first: write the size of the message to parent */
            write(child2par[1],&msgOutSz,sizeof(uint32_t));
            /* second: a single char for thread_safe info */
            if( !have_threadsafe_val )
            {
                if (
                    duk_get_global_string(dhs->ctx,DUK_HIDDEN_SYMBOL("threadsafe")) &&
                    duk_is_boolean(dhs->ctx,-1) && 
                    duk_get_boolean(dhs->ctx,-1)==0
                )  
                    write(child2par[1],"F",1); //message that thread safe==false
                else
                    write(child2par[1],"X",1);//message that thread safe is not to be set one way or another 

                duk_push_global_object(dhs->ctx);
                duk_del_prop_string(dhs->ctx,-1,DUK_HIDDEN_SYMBOL("threadsafe"));
                duk_pop_2(dhs->ctx);
                have_threadsafe_val=1;
            }
            else
            {
                write(child2par[1],"X",1);//message that thread safe is not to be set one way or another
            }

            /* third: write the encoded message */
            tprintf("child sending cbor message %d long on %d:\n",(int)msgOutSz,child2par[1]);
            write(child2par[1], cbor, msgOutSz);
            
            tprintf("child sent message %d long. Waiting for input\n",(int)msgOutSz);

            duk_pop(dhs->ctx);//buffer
            /* wait here for the next request and get it's size */
            read(par2child[0],&msgOutSz,sizeof(uint32_t));
            tprintf("child to receive message %d long on %d\n",(int)msgOutSz,par2child[0]);

            read(par2child[0],&(dhs->func_idx),sizeof(duk_idx_t));
            duk_dup(dhs->ctx,dhs->func_idx);

            {
                char jbuf[msgOutSz], *p=jbuf;
                int toread=(int)msgOutSz,totread=0;
                
                while ((totread=read(par2child[0], p, toread))>0)
                {
                    toread-=totread;
                    p+=totread;
                }
                //*(p+1)='\0';
                p=jbuf;
                memcpy(duk_push_fixed_buffer(dhs->ctx,(duk_size_t)msgOutSz),p,(size_t)msgOutSz);
            }
            duk_cbor_decode(dhs->ctx, -1,0);
            
        } while(1);

        close(child2par[1]);
        exit(0);
    }
    else
    {   /* parent */
        int totread=0,is_threadsafe=1;
        uint32_t bsize;
        duk_size_t bufsz;

        /* check child is still alive */
        if(preforked)
        {
            tprintf("is preforked in parent, c2p:%d p2c%d\n",finfo->child2par,finfo->par2child);

            /* push an empty object */
            duk_push_object(dhs->ctx);
            /* populate object with details from client */
            push_req_vars(dhs);
            /* convert to cbor and send to child */
            duk_cbor_encode(dhs->ctx,-1,0);
            cbor=duk_get_buffer_data(dhs->ctx,-1,&bufsz);

            msgOutSz=(uint32_t)bufsz;
            
            tprintf("parent sending cbor, length %d\n",(int)msgOutSz);
            write(finfo->par2child, &msgOutSz,sizeof(uint32_t));
            write(finfo->par2child, &(dhs->func_idx),sizeof(duk_idx_t));
            write(finfo->par2child, cbor, msgOutSz);
            tprintf("parent sent cbor, length %d\n",(int)msgOutSz);
            
            duk_pop(dhs->ctx);
        }
        else
        {
            tprintf("first run in parent c2p:%d p2c%d\n",child2par[0],par2child[1]);
            close(child2par[1]);
            close(par2child[0]);
            finfo->child2par=child2par[0];
            finfo->par2child=par2child[1];
        }
        tprintf("parent waiting for input\n");
        if(waitpid(finfo->childpid,&pidstatus,WNOHANG))
        {
            send500(req,"Error in Child Process");
            return;
        }

        if(dhs->timeout.tv_sec==RP_TIME_T_FOREVER)
        {
            if(-1==read(finfo->child2par,&bsize,sizeof(uint32_t)) )
            {
                fprintf(stderr,"server.c line %d: errno=%d\n",__LINE__,errno);
                send500(req,"Error in Child Process");
                return;
            }
        }
        else
        {
            double maxtime= (double)dhs->timeout.tv_sec + ((double)dhs->timeout.tv_usec / 1e6);

            /* this loops 4 times for the minimal callback returning a string
               on a sandy bridge 3.2ghz core (without the time check, just the usleep) */
            fcntl(finfo->child2par, F_SETFL,O_NONBLOCK);

            /* wait for and read the size int from child */
            while ( -1 == read(finfo->child2par,&bsize,sizeof(uint32_t)))
            {
                celapsed=stopwatch(cstart);
                if(celapsed > maxtime) 
                {
                    int loop=0;
                    kill(finfo->childpid,SIGUSR1);
                    while(!waitpid(finfo->childpid,&pidstatus,WNOHANG))
                    {
                        usleep(50000);
                        if(loop++ > 4)
                        {
                            fprintf(stderr,"hard timeout in script, no graceful exit.\n");
                            kill(finfo->childpid,SIGTERM);
                            break;
                        }
                    }
                    finfo->childpid=0;
                    duk_get_prop_string(dhs->ctx,dhs->func_idx,"fname");
                    fprintf(stderr,"timeout in %s()\n",duk_to_string(dhs->ctx,-1));
                    duk_pop(dhs->ctx);
                    send500(req,"Timeout in Script");
                    return;
                }
                usleep(250);
            }
            /* reset to blocking mode */
            fcntl(finfo->child2par, F_SETFL,0);
        }

        /* get thread safe info char and the message.
           put message in a buffer for decoding      */
        tprintf("parent about to read %d bytes\n",(int)bsize);
        {
            char jbuf[bsize+1], *p=jbuf, c;
            int toread=(int)bsize;
            
            read(finfo->child2par, &c, 1);
            if(c=='F')
                is_threadsafe=0;

            while ((totread=read(finfo->child2par, p, toread))>0)
            {
                tprintf("total read=%d\n",totread);
                toread-=totread;
                p+=totread;
            }
            if(totread<1 && toread>0)
            {
                fprintf(stderr,"server.c line %d, errno=%d\n",__LINE__,errno);
                send500(req,"Error in Child Process");
                return;
            }

            memcpy(duk_push_fixed_buffer(dhs->ctx,(duk_size_t)bsize),jbuf,(size_t)bsize);
        }
        /* decode message */
        duk_cbor_decode(dhs->ctx, -1,0);

        /* if we don't already have it, set the threadsafe info on the function */
        if(!have_threadsafe_val)
        {
            duk_push_boolean(dhs->ctx, (duk_bool_t) is_threadsafe);
            duk_put_prop_string(dhs->ctx, dhs->func_idx, DUK_HIDDEN_SYMBOL("threadsafe") );

            if( pthread_mutex_lock(&ctxlock) == EINVAL ) {
                fprintf(stderr,"could not obtain lock in http_callback\n");
                exit(1);
            }
            /* mark it as thread safe on main_ctx as well (for timeout/redo_ctx) */
            duk_push_boolean(main_ctx, (duk_bool_t) is_threadsafe);
            duk_put_prop_string(main_ctx, dhs->func_idx, DUK_HIDDEN_SYMBOL("threadsafe") );
            pthread_mutex_unlock(&ctxlock);
        }
    }    
    /* stack now has return value from duk function call */    

    /* set some headers */
    setheaders(req);

    /* don't accept arrays */
    if(duk_is_array(dhs->ctx,-1) ){
        duk_push_string(dhs->ctx,"Return value cannot be an array");
        duk_throw(dhs->ctx);
    }

    /* object has reply data and options in it */
    if(duk_is_object(dhs->ctx,-1))
        res=sendobj(dhs);
    else /* send string or buffer to client */
        sendbuf(dhs);
    
    if(res)evhtp_send_reply(req, res);

    duk_pop(dhs->ctx);
    /* logging goes here ? */
}


static void
http_callback(evhtp_request_t * req, void * arg)
{
    DHS newdhs, *dhs = arg;
    int dofork=1,have_threadsafe_val=0, thrno=0;
    duk_context *new_ctx;
    if(!gl_singlethreaded)
    {
        evthr_t *thread= get_request_thr(req);
        thrno=*((int*)evthr_get_aux(thread));
    }
    new_ctx = thread_ctx[thrno];

    /* ****************************
      setup duk function callback 
       **************************** */

    newdhs.ctx=new_ctx;
    newdhs.req=req;
    newdhs.func_idx=dhs->func_idx;
    newdhs.timeout=dhs->timeout;
    newdhs.threadno=thrno;
    dhs=&newdhs;

    /* fork until we know it is safe not to fork:
       function will have property threadsafe:true if forking not required 
       after the first time it is called in http_fork_callback              */
    if( duk_get_prop_string(dhs->ctx, dhs->func_idx, DUK_HIDDEN_SYMBOL("threadsafe")) )
    {
        if( duk_is_boolean(dhs->ctx,-1) && duk_get_boolean(dhs->ctx,-1) ) 
            dofork=0;
        have_threadsafe_val=1;
    }
    duk_pop(dhs->ctx);
    
//    if(!gl_singlethreaded && dofork)
    if(dofork)
        http_fork_callback(req, dhs, have_threadsafe_val);
    else
        http_thread_callback(req, dhs, thrno);
}

/* bind to addr and port.  Return mallocd string of addr, which 
   in the case of ipv6 may differ from ip passed to function */

char *bind_sock_port(evhtp_t *htp, const char *ip, uint16_t port, int backlog) 
{
    struct sockaddr_storage sin;
    socklen_t len = sizeof(struct sockaddr_storage);
    
    if( evhtp_bind_socket(htp, ip, port, backlog) != 0 )
        return NULL;
    
    if  (getsockname(
            evconnlistener_get_fd(htp->server),
            (struct sockaddr *)&sin, &len) == 0
        ) 
    {
        char addr[INET6_ADDRSTRLEN];

        sa_to_string(&sin,addr,INET6_ADDRSTRLEN);
        port = ntohs( ((struct sockaddr_in*)&sin)->sin_port);
        if(((struct sockaddr*)&sin)->sa_family==AF_INET6)
        {
            unsigned char a[sizeof(struct in6_addr)];
            
            /* compress our ipv6 address to compare */
            if (inet_pton(AF_INET6, &ip[5], a) == 1){
                char buf[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, a, buf, INET6_ADDRSTRLEN) == NULL) {
                   return NULL;
                }
                /* compare given compressed *ip with what we are actually bound to */
                if(strcmp(buf,addr)!=0)
                    return NULL;
            }
            else return NULL;
        }
        else
        {
            /*  ipv4 doesn't get compressed, it fails above if something like 127.0.0.001 */
            if(strcmp(ip,addr)!=0)
                return NULL;
        }
        printf("binding to %s port %d\n", addr, port);
        return strdup(addr);
    }

    return NULL;
}

void testcb(evhtp_request_t * req, void * arg)
{
    char rep[]="hello world";
    /*  
    TODO: figure out keepalive problem
    evhtp_request_set_keepalive(req, 1);
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));
    */
    /*sleep(1);
    printf("%d\n",(int)pthread_self());
    fflush(stdout);*/
    evbuffer_add_printf(req->buffer_out,"%s",rep);
    evhtp_send_reply(req, EVHTP_RES_OK);
}

void exitcb(evhtp_request_t * req, void * arg)
{
    //duk_context *ctx=arg;
    char rep[]="exit";
    int i=0;
    
    evbuffer_add_reference(req->buffer_out,rep,strlen(rep),NULL,NULL);
    evhtp_send_reply(req, EVHTP_RES_OK);
    for(;i<totnthreads;i++) 
    {
        //duk_rp_sql_close(thread_ctx[i]);
        duk_destroy_heap(thread_ctx[i]);
    }
    //duk_rp_sql_close(main_ctx);
    duk_destroy_heap(main_ctx);
    free(thread_ctx);
    exit(0);
}

#define DHSA struct dhs_array
DHSA {
    int n;
    DHS **dhs_pa;
};

DHSA dhsa={0,NULL};

static DHS *new_dhs(duk_context *ctx, int idx)
{
    DHS *dhs=NULL;
    DUKREMALLOC(ctx, dhs, sizeof(DHS));
    dhs->func_idx=idx;
    dhs->ctx=ctx;
    dhs->req=NULL;
    /* afaik there is no TIME_T_MAX */
    dhs->timeout.tv_sec=RP_TIME_T_FOREVER;
    dhs->timeout.tv_usec=0;
    dhsa.n++;
    DUKREMALLOC(ctx, dhsa.dhs_pa, sizeof(DHS*)*dhsa.n );
    dhsa.dhs_pa[dhsa.n-1]=dhs;
        
    return(dhs);
}

void initThread (evhtp_t *htp, evthr_t *thr, void *arg)
{
    int *thrno=NULL;
    
    DUKREMALLOC(main_ctx,thrno,sizeof(int));

    *thrno=gl_threadno++;

    evthr_set_aux(thr, thrno);
}

static void get_secure(duk_context *ctx, duk_idx_t ob_idx, evhtp_ssl_cfg_t *ssl_config)
{
    /* all protocols for testing */
    ssl_config->ssl_opts = 0;
    if(duk_get_prop_string(ctx,ob_idx,"sslkeyfile"))
    {
        ssl_config->privfile=strdup(duk_require_string(ctx,-1));
    }
    duk_pop(ctx);
    if(duk_get_prop_string(ctx,ob_idx,"sslcafile"))
    {
        ssl_config->cafile=strdup(duk_require_string(ctx,-1));
    }
    duk_pop(ctx);
    if(duk_get_prop_string(ctx,ob_idx,"sslcertfile"))
    {
        ssl_config->pemfile=strdup(duk_require_string(ctx,-1));
    }
    duk_pop(ctx);
}

static void connection_error_callback(evhtp_connection_t *conn, evhtp_error_flags type, void *arg)
{
    printf("unhandled connection error of type: 0x%02x", type);
}

evhtp_res pre_accept_callback(evhtp_connection_t *conn, void *arg)
{
    evhtp_connection_set_hook(conn, evhtp_hook_on_conn_error, (void*) &connection_error_callback,  NULL);
    //evhtp_connection_set_hook(conn, evhtp_hook_on_error,      (void*) &connection_error_callback,  NULL);
    //evhtp_connection_set_hook(conn, evhtp_hook_on_error,      (void*) &request_error_callback,     NULL);
    return EVHTP_RES_OK;
}


duk_ret_t duk_server_start(duk_context *ctx)
{
    /* TODO: make it so it can bind to any number of arbitary ipv4 or ipv6 addresses */
    int i=0;
    DHS *dhs=new_dhs(ctx,-1);
    duk_idx_t ob_idx=-1;
    char ipv6[INET6_ADDRSTRLEN+5]="ipv6:::1";
    char ipv4[INET_ADDRSTRLEN]="127.0.0.1";
    char *ipv4_addr=NULL, *ipv6_addr=NULL;
    uint16_t port=8080, ipv6port=8080;
    int nthr;
    evhtp_t           * htp4 = NULL;
    evhtp_t           * htp6 = NULL;
    struct event_base * evbase;
    evhtp_ssl_cfg_t *ssl_config=calloc(1, sizeof(evhtp_ssl_cfg_t));
    int secure=0,usev6=1,usev4=1,confThreads=-1,mthread=0;
    struct stat f_stat;
    struct timeval ctimeout;
    duk_idx_t fpos=0;
    
    ctimeout.tv_sec = RP_TIME_T_FOREVER;
    ctimeout.tv_usec = 0;
    main_ctx=ctx;

    i=0;
    if(
        (duk_is_object(ctx,   i   ) && !duk_is_array(ctx,i) && !duk_is_function(ctx,i))
        || 
        (duk_is_object(ctx,  ++i  ) && !duk_is_array(ctx,i) && !duk_is_function(ctx,i)) 
    )
        ob_idx=i;
      
     i=!i; /* get the other option. There's only 2 */

     if(duk_is_function(ctx,i))
         dhs->func_idx=i;

    evbase = event_base_new();

    /* options from server({options},...) */
    if(ob_idx!=-1) {
        const char *s;

        /* ip addr */
        if(duk_get_prop_string(ctx,ob_idx,"ip"))
        {
            s=duk_require_string(ctx,-1);
            strcpy(ipv4,s);
        }
        duk_pop(ctx);

        /* ipv6 addr */
        if(duk_get_prop_string(ctx,ob_idx,"ipv6"))
        {
            s=duk_require_string(ctx,-1);
            strcpy(&ipv6[5],s);
        }
        duk_pop(ctx);

        /* port */
        if(duk_get_prop_string(ctx,ob_idx,"port"))
        {
            port=duk_require_int(ctx,-1);
        }
        duk_pop(ctx);

        /* threads */
        if(duk_get_prop_string(ctx,ob_idx,"threads"))
        {
            confThreads=duk_require_int(ctx,-1);
        }
        duk_pop(ctx);

        /* multithreaded */
        if(duk_get_prop_string(ctx,ob_idx,"usethreads"))
        {
            mthread=duk_require_boolean(ctx,-1);
            gl_singlethreaded=!mthread;
        }
        duk_pop(ctx);
        if(duk_get_prop_string(ctx,ob_idx,"useThreads"))
        {
            mthread=duk_require_boolean(ctx,-1);
            gl_singlethreaded=!mthread;
        }
        duk_pop(ctx);

        /* port ipv6*/
        if(duk_get_prop_string(ctx,ob_idx,"ipv6port"))
        {
            ipv6port=(uint16_t)duk_require_int(ctx,-1);
        }
        else ipv6port=port;
        duk_pop(ctx);

        /* use ipv6*/
        if(duk_get_prop_string(ctx,ob_idx,"useipv6"))
        {
            usev6=duk_require_boolean(ctx,-1);
        }
        duk_pop(ctx);
        if(duk_get_prop_string(ctx,ob_idx,"useIpv6"))
        {
            usev6=duk_require_boolean(ctx,-1);
        }
        duk_pop(ctx);

        /* use ipv4*/
        if(duk_get_prop_string(ctx,ob_idx,"useipv4"))
        {
            usev4=duk_require_boolean(ctx,-1);
        }
        duk_pop(ctx);
        if(duk_get_prop_string(ctx,ob_idx,"useIpv4"))
        {
            usev4=duk_require_boolean(ctx,-1);
        }
        duk_pop(ctx);

        /* timeout */
        if(duk_get_prop_string(ctx,ob_idx,"connectTimeout"))
        {
            double to=duk_require_number(ctx,-1);
            ctimeout.tv_sec=(time_t)to;
            ctimeout.tv_usec=(suseconds_t) 1000000.0 * ( to - (double) ctimeout.tv_sec );
            printf("set connection timeout to %d sec and %d microseconds\n",(int)ctimeout.tv_sec,(int)ctimeout.tv_usec);
        }
        duk_pop(ctx);

        if(duk_get_prop_string(ctx,ob_idx,"scriptTimeout"))
        {
            double to=duk_require_number(ctx,-1);
            dhs->timeout.tv_sec=(time_t)to;
            dhs->timeout.tv_usec=(suseconds_t) 1000000.0 * ( to - (double) dhs->timeout.tv_sec );
            printf("set script timeout to %d sec and %d microseconds\n",(int)dhs->timeout.tv_sec,(int)dhs->timeout.tv_usec);
        }
        duk_pop(ctx);

        /* ssl opts*/
        if(duk_get_prop_string(ctx,ob_idx,"secure"))
        {
            secure=duk_require_boolean(ctx,-1);
        }
        duk_pop(ctx);

        if(secure)
            get_secure(ctx,ob_idx,ssl_config);
    }
     
    /*  mapping options from server({map:{mapoptions}})
        got the essential config options, now do some setup before mapping urls */
    if(mthread)
    {
        if(usev6+usev4 == 0)
        {
            duk_push_string(ctx,"useipv6 and useipv4 cannot both be set to false");
            duk_throw(ctx);
        }

        /* use specified number of threads */
        if(confThreads>0)
        {
            nthr=confThreads;
        }
        else
            /* not specified, so set to number of processor cores */
        {
            nthr=sysconf(_SC_NPROCESSORS_ONLN);
        }

        totnthreads=nthr*(usev6+usev4);

        printf("HTTP server for %s%s%s - initializing with %d threads per server, %d total\n", 
            ((usev4)?"ipv4":""),
            ((usev4 && usev6)? " and ":""),
            ((usev6)?"ipv6":""),
            nthr,
            totnthreads);

    } 
    else 
    {
        if(confThreads>1) {
            printf("threads>1 -- usethreads==false, so using only one thread\n");
        }
        totnthreads=1;
        printf("HTTP server for %s%s%s - initializing single threaded\n", 
            ((usev4)?"ipv4":""),
            ((usev4 && usev6)? " and ":""),
            ((usev6)?"ipv6":""));
    }

    /* set up info structs for forking in threads for thread-unsafe duk_c functions */
    REMALLOC( forkinfo, (totnthreads*sizeof(FORKINFO*)) );
    for(i=0;i<totnthreads;i++){
        forkinfo[i]=NULL;
        REMALLOC( forkinfo[i],sizeof(FORKINFO) );
        forkinfo[i]->par2child=-1;
        forkinfo[i]->child2par=-1;
        forkinfo[i]->childpid=0;
    }

    /* initialize a context for each thread */
    REMALLOC( thread_ctx, (totnthreads*sizeof(duk_context*)) );
    for(i=0;i<=totnthreads;i++){
        duk_context *tctx;
        if(i==totnthreads)
        {
            tctx=ctx;
        }
        else
        {
            thread_ctx[i] = duk_create_heap_default();
            tctx=thread_ctx[i];
            /* do all the normal startup done in duk_cmdline but for each thread */
            duk_init_userfunc(tctx);
            copy_all(ctx,tctx);
        }

        /* add some info in each thread as well as the parent thread*/
        if(!duk_get_global_string(tctx,"rampart"))
        {
            duk_pop(tctx);
            duk_push_object(tctx);
        }
        duk_push_int(tctx,i);
        duk_put_prop_string(tctx,-2,"thread_id");
        duk_push_int(tctx,totnthreads);
        duk_put_prop_string(tctx,-2,"total_threads");

        duk_put_global_string(tctx,"rampart");
    }

    if(usev4) htp4 = evhtp_new(evbase, NULL);
    if(usev6) htp6 = evhtp_new(evbase, NULL);

    /* testing for pure c benchmarking*/
    if (usev4) evhtp_set_cb(htp4, "/test", testcb,NULL);
    if (usev6) evhtp_set_cb(htp6, "/test", testcb,NULL);
    /* testing, quick semi clean exit */
    if (usev4) evhtp_set_cb(htp4, "/exit", exitcb,ctx);
    if (usev6) evhtp_set_cb(htp6, "/exit", exitcb,ctx);

    /* file system and function mapping */
    if(ob_idx!=-1) {
        if(duk_get_prop_string(ctx,ob_idx,"map")){
            if(duk_is_object(ctx,-1) && !duk_is_function(ctx,-1) && !duk_is_array(ctx,-1))
            {
                duk_enum(ctx, -1, 0);
                while (duk_next(ctx, -1 , 1 ))
                {
                    char *path,*fspath,*fs=(char*)NULL,*s=(char*)NULL;

                    if(duk_is_function(ctx,-1))
                    {   /* map to function */
                        /* move the function to earlier in the stack 
                           the stack [object, ?function?, mapobj, enum, path, fspath|func ] 
                                           move to here--^         
                        */
                        DHS *cb_dhs;
                        const char *fname;

                        duk_get_prop_string(ctx,-1,"name");
                        fname=duk_get_string(ctx,-1);
                        duk_pop(ctx);

                        path=(char*)duk_to_string(ctx,-2);
                        DUKREMALLOC(ctx,s,strlen(path)+2);

                        if(*path!='/' && *path!='*')
                            sprintf(s,"/%s",path);
                        else 
                            sprintf(s,"%s",path);

                        duk_insert(ctx,fpos);
                        cb_dhs=new_dhs(ctx,fpos);
                        cb_dhs->timeout.tv_sec=dhs->timeout.tv_sec;
                        cb_dhs->timeout.tv_usec=dhs->timeout.tv_usec;
                        /* copy function to all the heaps/ctxs */
                        copy_cb_func(cb_dhs,totnthreads);
                        fpos++;
                        printf("mapping function   %-20s ->    function %s()\n",s,fname);
                        if (*s=='*' || *(s+strlen(s)-1)=='*')
                        {
                            if (usev4) 
                            {
                                //evhtp_callback_t  *req_callback = 
                                evhtp_set_glob_cb(htp4,s,http_callback,cb_dhs);
                                //evhtp_callback_set_hook(req_callback,evhtp_hook_on_request_fini,req_on_finish, NULL);
                            }
                            if (usev6) 
                            {
                                //evhtp_callback_t  *req_callback = 
                                evhtp_set_glob_cb(htp6,s,http_callback,cb_dhs);
                                //evhtp_callback_set_hook(req_callback,evhtp_hook_on_request_fini,req_on_finish, NULL);
                            }
                        }
                        else
                        {
                            if (usev4) 
                            {
                                //evhtp_callback_t  *req_callback = 
                                evhtp_set_cb(htp4,s,http_callback,cb_dhs);
                                //evhtp_callback_set_hook(req_callback,evhtp_hook_on_request_fini,req_on_finish, NULL);
                            }
                            if (usev6) 
                            {
                                //evhtp_callback_t  *req_callback = 
                                evhtp_set_cb(htp6,s,http_callback,cb_dhs);
                                //evhtp_callback_set_hook(req_callback,evhtp_hook_on_request_fini,req_on_finish, NULL);
                            }
                        }
                        free(s);
                        duk_pop(ctx);
                    }
                    else
                    /*TODO: duk_is_object and get some config options
                            for each mapped http path                   */
                    {   /* map to filesystem */
                        DHMAP *map=NULL;
                        
                        DUKREMALLOC(ctx,map,sizeof(DHMAP));
                
                        /* get the http path and make it a glob */
                        path=(char*)duk_to_string(ctx,-2);
                        DUKREMALLOC(ctx,s,strlen(path)+4);

                        if(*path!='/' || *(path+strlen(path)-1)!= '/')
                        {
                            if(*path!='/' && *(path+strlen(path)-1)!= '/')
                                sprintf(s,"/%s/*",path);
                            else if(*path!='/')
                                sprintf(s,"/%s*",path);
                            else
                                sprintf(s,"%s/*",path);
                        }
                        else sprintf(s,"%s*",path);

                        map->key=s;
                        map->dhs=dhs;

                        fspath=(char*)duk_to_string(ctx,-1);
                        DUKREMALLOC(ctx,fs,strlen(fspath)+2)

                        if(*(fspath+strlen(fspath)-1)!= '/')
                        {
                            sprintf(fs,"%s/",fspath);
                        }
                        else sprintf(fs,"%s",fspath);
                        printf ("mapping folder     %-20s ->    %s\n",s,fs);
                        map->val=fs;
                        if (usev4) evhtp_set_glob_cb(htp4,s,fileserver,map);
                        if (usev6) evhtp_set_glob_cb(htp6,s,fileserver,map);
                        duk_pop_2(ctx);
                    }
                }
            }
            else
            {
                duk_push_string(ctx,"value of 'map' must be an object");
                duk_throw(ctx);
            }
        }
        duk_pop(ctx);
    }
    
    if(secure)
    {
        int filecount=0;
        if (ssl_config->pemfile) {
            filecount++;
            if (stat(ssl_config->pemfile, &f_stat) != 0) {
                duk_push_sprintf(ctx,"Cannot load SSL cert '%s' (%s)", ssl_config->pemfile, strerror(errno));
                duk_throw(ctx);
            }
        }

        if (ssl_config->privfile) {
            filecount++;
            if (stat(ssl_config->privfile, &f_stat) != 0) {
                duk_push_sprintf(ctx,"Cannot load SSL key '%s' (%s)", ssl_config->privfile, strerror(errno));
                duk_throw(ctx);
            }
        }

        if (ssl_config->cafile) {
            if (stat(ssl_config->cafile, &f_stat) != 0) {
                duk_push_sprintf(ctx,"Cannot find SSL CA File '%s' (%s)", ssl_config->cafile, strerror(errno));
                duk_throw(ctx);
            }
        }
        if(filecount<2)
        {
            duk_push_sprintf(ctx,"Minimally ssl must be configured with, e.g. -\n"
            "{\n\t\"secure\":true,\n\t\"sslkeyfile\": \"/path/to/privkey.pem\","
            "\n\t\"sslcertfile\":\"/path/to/fullchain.pem\"\n}");
            duk_throw(ctx);
        }
        printf("Initing ssl/tls\n");
        if (usev4) {
            if (evhtp_ssl_init(htp4, ssl_config)==-1) {
                duk_push_string(ctx,"error setting up ssl/tls server");
                duk_throw(ctx);
            }
        }
        if (usev6) {
            if (evhtp_ssl_init(htp6, ssl_config)==-1) {
                duk_push_string(ctx,"error setting up ssl/tls server");
                duk_throw(ctx);
            }
        }
    }

    if( dhs->func_idx !=-1)
    {
        duk_pull(ctx,dhs->func_idx);
        duk_insert(ctx,fpos);
        dhs->func_idx=fpos;
        copy_cb_func(dhs,totnthreads);
        if (usev4) evhtp_set_gencb(htp4, http_callback, dhs);
        if (usev6) evhtp_set_gencb(htp6, http_callback, dhs);
    }
    
    if (usev4) 
    {
        if(!(ipv4_addr=bind_sock_port(htp4,ipv4,port,2048)))
        {
            duk_push_sprintf(ctx,"could not bind to %s port %d",ipv4,port);
            duk_throw(ctx);
        }
    }
    
    if (usev6) 
    {
        if (!(ipv6_addr=bind_sock_port(htp6,ipv6,ipv6port,2048)))
        {
            duk_push_sprintf(ctx,"could not bind to %s, %d",ipv6,ipv6port);
            duk_throw(ctx);
        }
    }

    for(i=0;i<=totnthreads;i++){
        duk_context *tctx;
        if(i==totnthreads)
            tctx=ctx;
        else
            tctx=thread_ctx[i];

        /* add more info */
        if(!duk_get_global_string(tctx,"rampart"))
        {
            duk_pop(tctx);
            duk_push_object(tctx);
        }

        duk_push_object(tctx);
        if(usev4)
        {
            duk_push_string(tctx,ipv4_addr);
            duk_put_prop_string(tctx,-2,"ipv4_addr");
            duk_push_int(tctx,port);
            duk_put_prop_string(tctx,-2,"ipv4_port");
        }        
        if(usev6)
        {
            duk_push_string(tctx,ipv6_addr);
            duk_put_prop_string(tctx,-2,"ipv6_addr");
            duk_push_int(tctx,ipv6port);
            duk_put_prop_string(tctx,-2,"ipv6_port");
        }        
        duk_put_prop_string(tctx,-2,"server");
        duk_put_global_string(tctx,"rampart");
    }


    if (usev4) evhtp_set_timeouts(htp4, &ctimeout, &ctimeout);
    if (usev6) evhtp_set_timeouts(htp6, &ctimeout, &ctimeout);

    if (usev4) evhtp_set_pre_accept_cb(htp4, pre_accept_callback, NULL);
    if (usev6) evhtp_set_pre_accept_cb(htp6, pre_accept_callback, NULL);

    if(mthread)
    {
        if (usev4) evhtp_use_threads_wexit(htp4, initThread, NULL, nthr, NULL);
        if (usev6) evhtp_use_threads_wexit(htp6, initThread, NULL, nthr, NULL);
    }
    else
    {
        printf("in single threaded mode\n");
    }

    if( pthread_mutex_init(&ctxlock,NULL)  == EINVAL ) {
        fprintf(stderr,"could not initialize context lock\n");
        exit(1);
    }

    event_base_loop(evbase, 0);
    /* never gets here */
    return 0;
}

static const duk_function_list_entry utils_funcs[] = {
    { "start",         duk_server_start, 2 /*nargs*/ },
    { NULL, NULL, 0 }
};

static const duk_number_list_entry utils_consts[] = {
    { NULL, 0.0 }
};

duk_ret_t dukopen_module(duk_context *ctx) {
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, utils_funcs);
    duk_put_number_list(ctx, -1, utils_consts);

    return 1;
}
