/* Copyright (C) 2020 Aaron Flin - All Rights Reserved
 * You may use, distribute or alter this code under the
 * terms of the MIT license
 * see https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h> // for syscall(SYS_gettid)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <uuid/uuid.h>
#endif
#include <pwd.h>
#include "../core/duktape.h"
#include "evhtp/evhtp.h"
#include "../../rp.h"
#include "../core/module.h"
#include "../../mime.h"
#include "../register.h"
//#define RP_TO_DEBUG
//#define RP_TIMEO_DEBUG
#define COMBINE_EVLOOPS 1

RP_MTYPES *allmimes=rp_mimetypes;
int n_allmimes =nRpMtypes;

extern int RP_TX_isforked;

uid_t unprivu=0;
gid_t unprivg=0;
//#define RP_TIMEO_DEBUG
#ifdef RP_TIMEO_DEBUG
#define debugf(...) printf(__VA_ARGS__);
#else
#define debugf(...) /* Do nothing */
#endif

pthread_mutex_t ctxlock;


volatile int gl_threadno = 0;
int gl_singlethreaded = 0;

duk_context **thread_ctx = NULL, *main_ctx;

int totnthreads = 0;
int rp_using_ssl = 0;
static char *scheme="http://";

/* DEBUGGING MACROS */

static const char *method_strmap[] = {
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
    "UNKNOWN"
};

/*
static const char *scheme_strmap[] = {
    "",
    "ftp://",
    "http://",
    "htps://",
    "nfs://",
    ""
};
*/
/* IPC Pipe info for parent/child and child pid */

#define FORKINFO struct fork_info_s
FORKINFO
{
    int par2child;
    int child2par;
    pid_t childpid;
};

FORKINFO **forkinfo = NULL;

/* a duk http request struct
    keeps info about request, callback,
    timeout and thread number for thread specific info
*/

#define MODULE_NONE 0
#define MODULE_FILE 1
#define MODULE_FILE_RELOAD 2
#define MODULE_PATH 3

#define DHS struct duk_rp_http_s
DHS
{
    duk_idx_t func_idx;     // location of the callback (set at server start, or for modules, upon require())
    duk_context *ctx;       // duk context for this thread (updated in thread)
    evhtp_request_t *req;   // the evhtp request struct for the current request (updated in thread, upon each request)
    struct timeval timeout; // timeout for the duk script
    uint16_t threadno;      // our current thread number
    uint16_t pathlen;       // length of the url path if module==MODULE_PATH
    char *module_name;      // name of the module 
    void *aux;              // generic pointer - currently used for dirlist()
    char module;            // is normal fuction if 0, 1 is js module, 2 js module needs reload.
};

DHS *dhs404 = NULL;
DHS *dhs_dirlist = NULL;

/* mapping for url to filesystem */
#define DHMAP struct fsmap_s
DHMAP
{
    DHS *dhs;
    char *key;
    char *val;
};

static void setheaders(evhtp_request_t *req)
{
    time_t now = time(NULL);
    if (now != -1)
    {
        struct tm *ptm = gmtime(&now);
        char buf[128];
        /* Thu, 21 May 2020 01:41:20 GMT */
        strftime(buf, 128, "%a, %d %b %Y %T GMT", ptm);
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Date", buf, 0, 1));
    }
    //TODO: Discover why this breaks everything.
    //evhtp_request_set_keepalive(req, 1);
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));
    //evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","close",0,0));
}

void sa_to_string(void *sa, char *buf, size_t bufsz)
{
    if (((struct sockaddr *)sa)->sa_family == AF_INET6)
        inet_ntop(AF_INET6, &((struct sockaddr_in6 *)sa)->sin6_addr, buf, bufsz);
    else
        inet_ntop(AF_INET, &((struct sockaddr_in *)sa)->sin_addr, buf, bufsz);
}

#define printerr(args...) do {\
    time_t now = time(NULL);\
    struct tm ti_s, *timeinfo;\
    char date[32];\
    \
    timeinfo = localtime_r(&now,&ti_s);\
    strftime(date,32,"%d/%b/%Y:%H:%M:%S %z",timeinfo);\
    if (pthread_mutex_lock(&errlock) == EINVAL)\
    {\
        fprintf(error_fh, "could not obtain lock in http_callback\n");\
        exit(1);\
    }\
    fprintf(error_fh, "%s ", date);\
    fprintf(error_fh, args);\
    fflush(error_fh);\
    pthread_mutex_unlock(&errlock);\
} while(0)    


static void writelog(evhtp_request_t *req, int code)
{
    evhtp_connection_t *conn = evhtp_request_get_connection(req);
    void *sa = (void *)conn->saddr;
    time_t now = time(NULL);
    struct tm ti_s, *timeinfo;
    char address[INET6_ADDRSTRLEN], date[32], 
        *q="", *qm="", *proto=""; 
    const char *length, *ua, *ref;
    int method = evhtp_request_get_method(req);
    evhtp_path_t *path = req->uri->path;
   
    sa_to_string(sa, address, sizeof(address));

    timeinfo = localtime_r(&now,&ti_s);
    strftime(date,32,"%d/%b/%Y:%H:%M:%S %z",timeinfo);

    if (method >= 16)
        method = 16;

    if(req->uri->query_raw && strlen((char*)req->uri->query_raw))
    {
        qm="?";
        q=(char*)req->uri->query_raw;
    }

    if(req->proto==EVHTP_PROTO_10)
        proto="HTTP/1.0";
    else if (req->proto==EVHTP_PROTO_11)
        proto="HTTP/1.1";

    if( !(length=evhtp_kv_find(req->headers_out,"Content-Length")) )
        length="-";
    
    if( !(ref=evhtp_kv_find(req->headers_in,"Referer")) )
        ref="-";

    if( !(ua=evhtp_kv_find(req->headers_in,"User-Agent")) )
        ua="-";
    
    if (pthread_mutex_lock(&loglock) == EINVAL)
    {
        printerr( "could not obtain lock in http_callback\n");
        exit(1);
    }
    
    fprintf(access_fh,"%s - - [%s] \"%s %s%s%s %s\" %d %s \"%s\" \"%s\"\n",
        address,date,method_strmap[method],
        path->full, qm, q, proto,
        code, length, ref, ua
    );
    fflush(access_fh);
    pthread_mutex_unlock(&loglock);
}


static void sendresp(evhtp_request_t *request, evhtp_res code)
{
    evhtp_send_reply(request, code);
    if(duk_rp_server_logging)
    {
        writelog(request, (int)code);
    }
}

#define putval(key, val) do {                      \
    if ((char *)(val) != (char *)NULL)             \
    {                                              \
        duk_push_string(ctx, (const char *)(val)); \
        duk_put_prop_string(ctx, -2, (key));       \
    }                                              \
} while(0)

int putkvs(evhtp_kv_t *kv, void *arg)
{
    char *v, *k;
    duk_context *ctx = (duk_context *)arg;

    v = duk_rp_url_decode(kv->val, (int)kv->vlen);
    k = duk_rp_url_decode(kv->key, (int)kv->klen);
    putval(k, v);

    free(k);
    free(v);

    return 0;
}

static int putheaders(evhtp_kv_t *kv, void *arg)
{
    duk_context *ctx = (duk_context *)arg;

    duk_push_lstring(ctx, kv->val, (duk_size_t)kv->vlen);
    duk_put_prop_lstring(ctx, -2, kv->key, (duk_size_t)kv->klen);

    return 0;
}

void parseheadline(duk_context *ctx, char *line, size_t linesz)
{
    char *p=line,*e,*prop;
    size_t sz,rem=linesz,propsz;

    e=(char*)memmem(p,rem,":",1);
    
    sz=e-line;
    prop=p;
    propsz=sz;
//    printf("'%.*s'=",sz,p);
    
    p=e;
    p++;rem--;
    if( rem>0 && isspace(*p) ) p++,rem--;
    
    while(rem>0 && *e!=';' && *e!='\r' && *e!='\n')
        e++,rem--;
    sz=e-p;
    //printf("'%.*s'\n",sz,p);
    duk_push_lstring(ctx,p,(duk_size_t)sz);
    duk_put_prop_lstring(ctx,-2,prop,(duk_size_t)propsz);

    if(*e=='\r' || *e=='\n')
        return;

    p=e;
    while( rem>0 && isspace(*p) ) p++,rem--;
    if(*p!=';' || !rem)
        return;
    p++;rem--;
    while( rem>0 && isspace(*p) ) p++,rem--;
    
    // get extra attributes after ';'
    while(rem>0)
    {
        e=p;

        if (rem)
            e++,rem--;

        while(rem>0 && *e!='=')
            e++,rem--;

        if(*e=='=')
        {
            sz=e-p;
            prop=p; 
            propsz=sz;
            //printf("'%.*s'=",sz,p);
        }
        else return;
        
        e++;rem--;
        p=e;

        if(rem)
        {
            if(*p=='"')
            {
                p++;rem--;
                e=p;
                while(rem>0 && *e!='"')
                    e++,rem--;
                if(*e=='"')
                {
                    sz=e-p;
                    //printf("'%.*s'\n",sz,p);
                    duk_push_lstring(ctx,p,(duk_size_t)sz);
                    duk_put_prop_lstring(ctx,-2,prop,(duk_size_t)propsz);
                }
                e++;rem--;
            }
            else
            {
                 e=p;
                 while(rem>0 && !isspace(*e) ) e++,rem--;
                 sz=e-p;
                 //printf("'%.*s'\n",sz,p);
                 duk_push_lstring(ctx,p,(duk_size_t)sz);
                 duk_put_prop_lstring(ctx,-2,prop,(duk_size_t)propsz);
            }
        }
        else
        {
            //printf("''\n");
            duk_push_string(ctx,"");
            duk_put_prop_lstring(ctx,-2,prop,(duk_size_t)propsz);
            return;
        }
        
        p=e;
        while( rem>0 && isspace(*p) ) p++,rem--;
        if(*p!=';' || !rem)
            return;
        p++;rem--;
        while( rem>0 && isspace(*p) ) p++,rem--;
    }
    
}


void parsehead(duk_context *ctx,void *head, size_t headsz)
{
    size_t remaining=headsz;
    char * line=(char *)head;
    char * eol = (char*) memmem(line,remaining,"\r\n",2) + 2;
    
    while(eol)
    {
        size_t lsz=eol-line;
        
        //printf("LINE='%.*s'\n",(int)lsz,line);
        parseheadline(ctx,line,lsz);
        remaining=headsz-(eol-(char*)head);
        line = eol;

        if(remaining)
            eol=(char*) memmem(line,remaining,"\r\n",2)+2;
        else
            break;
    }


}

void push_multipart(duk_context *ctx, char *bound, void *buf, duk_size_t bsz)
{
    size_t remaining=(size_t)bsz, bound_sz=strlen(bound);
    void *b=buf;
    int i=0;
    
    b=memmem(b,remaining,bound,bound_sz);
    while (b)
    {
        b+=bound_sz;
        if (remaining>3)
        {
            void *begin;
            size_t sz;

            if(strncmp(b,"\r\n",2)==0)
            {
                //printf("got beginning\n");
                begin=b+2;
                remaining=bsz-(b-buf);
            }
            else if (strncmp(b,"--\r\n",4)==0)
            {
                //printf("done\n");
                break;
            }
            else
            {
                //printf("bad data!\n");
                break;
            }
            b=memmem(begin,remaining,bound,bound_sz);
            if(b)
            {
                void *n=b-1;
                void *headend=memmem(begin,remaining,"\r\n\r\n",4);
                size_t headsz;
                //void *pushbuf;
                
                if(headend)
                {
                    duk_push_object(ctx);
                    headsz=2+headend-begin;
                    //printf("head=%.*s\n",headsz,begin);
                    parsehead(ctx,begin,headsz);
                    begin=headend+4;
                }
                else
                    break;

                if(*((char*)n)=='-')n--;//end delim is "--"+bound
                if(*((char*)n)=='-')n--;
                if(*((char*)n)=='\n')n--;
                if(*((char*)n)=='\r')n--;
                sz=(n-begin)+1;
                //printf("Part is %d long:\n%.*s\n",(int)sz,(int)sz,(char*)begin);
                //share buffer with body/evbuffer
                if( !duk_get_prop_string(ctx,-1,"name") )
                {
                    duk_push_sprintf(ctx,"data-%s",i);
                    i++;
                }
                duk_pull(ctx,-2);
                duk_push_external_buffer(ctx);
                duk_config_buffer(ctx, -1, begin, sz);
                /*
                //copy buffer
                pushbuf = duk_push_fixed_buffer(ctx, sz);
                memcpy(pushbuf,begin,sz);
                */
                duk_put_prop_string(ctx,-2,"contents");
                duk_put_prop(ctx,-3);
                //duk_put_prop_index(ctx,-2,(duk_uarridx_t)i);
                //i++;
            }
        }
        else
            break;
    }
    
}

#define copy_req_vars(prop) \
    if(duk_get_prop_string(ctx,-1,prop))\
    {\
        duk_enum(ctx,-1,DUK_ENUM_OWN_PROPERTIES_ONLY);\
        while (duk_next(ctx,-1,1))\
        {\
            duk_put_prop(ctx,-6);\
        }\
        duk_pop(ctx);\
    }\
    duk_pop(ctx);\

#define copy_single_req_var(prop,toprop) \
    if(duk_get_prop_string(ctx,-1,prop))\
        duk_put_prop_string(ctx,-3,toprop);\
    else duk_pop(ctx);\

static void flatten_vars(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_dup(ctx,-2);

    /* lowest priority first */
    copy_req_vars("postData")
    copy_req_vars("query")
    copy_req_vars("cookies")
    copy_req_vars("headers")
/*
    copy_single_req_var("ip","clientIp");
    copy_single_req_var("port","clientPort");
    copy_single_req_var("method","httpMethod");


    if(duk_get_prop_string(ctx,-1,"path"))
    {
        duk_remove(ctx,-2); // this
        copy_single_req_var("file","requestFile");
        copy_single_req_var("path","requestPath");
        copy_single_req_var("base","requestBase");
        copy_single_req_var("full","requestFullPath");
    }
*/
    duk_pop(ctx); /* leave only "flat" object on stack */
}


/* copy request vars from evhtp to duktape */

void push_req_vars(DHS *dhs)
{
    duk_context *ctx = dhs->ctx;
    evhtp_path_t *path = dhs->req->uri->path;
    evhtp_authority_t *auth = dhs->req->uri->authority;
    int method = evhtp_request_get_method(dhs->req);
    evhtp_connection_t *conn = evhtp_request_get_connection(dhs->req);
    void *sa = (void *)conn->saddr;
    evhtp_uri_t *uri=dhs->req->uri;
    char address[INET6_ADDRSTRLEN], *q;
    const char *host;
    const char *ct;
    duk_size_t bsz;
    void *buf=NULL;

    /* aux is the local filesystem path for directory listing */
    if(dhs->aux)
    {
        putval("fsPath",(const char *) dhs->aux);
    }

    /* get ip address */
    sa_to_string(sa, address, sizeof(address));
    putval("ip", address);

    /* and port */
    duk_push_int(ctx, (int)ntohs(((struct sockaddr_in *)sa)->sin_port));
    duk_put_prop_string(ctx, -2, "port");

    /*method */
    if (method >= 16)
        method = 16;
    putval("method", method_strmap[method]);

    /* path info */
    duk_push_object(ctx);

    if( !(host=evhtp_kv_find(dhs->req->headers_in,"Host")) )
        host="localhost";

    putval("file", path->file);
    putval("path", path->full);
    putval("base", path->path);
    /* Getting scheme from request:
       this is likely for proxy server stuff (but might be for
       the request part of the library???)
       which probably is not currently possible here.
       TODO: find out if so, and how to handle proxy requests.
     printf("scheme=%s\n",scheme_strmap[dhs->req->uri->scheme]);
    */
    putval("scheme",scheme);
    putval("host", host);
    if(uri->query_raw && strlen((char*)uri->query_raw))
        duk_push_sprintf(ctx,"%s%s%s?%s",scheme,host,path->full,uri->query_raw);
    else
        duk_push_sprintf(ctx,"%s%s%s",scheme,host,path->full);
    duk_put_prop_string(ctx,-2,"url");

    duk_put_prop_string(ctx, -2, "path");

    /*  Auth */
    if( auth->username || auth->password || auth->hostname)
    {
        duk_push_object(ctx);
        putval("user", auth->username);
        putval("pass", auth->password);
        putval("hostname", auth->hostname);
        duk_push_int(ctx, (duk_int_t)auth->port);
        duk_put_prop_string(ctx, -2, "port");
        duk_put_prop_string(ctx, -2, "authority");
    }

    /* fragment -- portion after # */
    putval("fragment", (char *)uri->fragment);

    /* query string * 
     ****** now handled with duk_rp_querystring2object() ********
    duk_push_object(ctx);
    evhtp_kvs_for_each(uri->query, putkvs, ctx);
    duk_put_prop_string(ctx, -2, "query");
    */
    duk_rp_querystring2object(ctx, (char *)uri->query_raw);
    duk_put_prop_string(ctx, -2, "query");

    bsz = (duk_size_t)evbuffer_get_length(dhs->req->buffer_in);
    if(bsz)
    {
        buf=evbuffer_pullup(dhs->req->buffer_in,-1); /* make contiguous and return pointer */
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx,-1,buf,bsz); /* add reference to buf for js buffer */
    }
    else
    {
        (void) duk_push_fixed_buffer(ctx, 0);
    }
    duk_put_prop_string(ctx, -2, "body");

    q = (char *)uri->query_raw;
    if (!q) q="";
    putval("query_raw", q);

    /*  headers */
    duk_push_object(ctx);
    evhtp_headers_for_each(dhs->req->headers_in, putheaders, ctx);
    /* leave headers here to examine below */

    /* examine headers for cookies */
    if(duk_get_prop_string(ctx, -1, "Cookie"))
    {
        const char *c=duk_get_string(ctx,-1), *cend=c,*k=NULL,*v=NULL;
        int klen=0,vlen=0;

        duk_pop(ctx);

        duk_push_object(ctx);
        while(*cend)
        {
            cend++;
            switch(*cend)
            {
                case '=':
                    k=c;
                    klen=cend-c;
                    cend++;
                    c=cend;
                    break;
                case ';':
                case '\0':
                    v=c;
                    vlen=cend-c;
                    if(*cend)cend++;
                    c=cend;
                    while(isspace(*c)) c++;
                    cend=c;
                    break;
            }
            if(v && k)
            {
                duk_push_lstring(ctx,v,vlen);
                duk_put_prop_lstring(ctx,-2,k,klen);
                v=k=NULL;
            }

        }
        duk_put_prop_string(ctx,-3,"cookies");
    }
    else
        duk_pop(ctx);

    /* examine headers for content-type */
    if(duk_get_prop_string(ctx, -1, "Content-Type"))
    {
        ct=duk_get_string(ctx,-1);
        duk_pop(ctx);
        /* done with headers, move off stack into req. */
        duk_put_prop_string(ctx, -2, "headers");
        if(bsz && strncmp("multipart/form-data;",ct,20)==0)
        {
            char *bound=(char*)ct+20;

            if (isspace(*bound)) bound++;
            if (strncmp("boundary=",bound,9)==0)
            {
                bound+=9;
                duk_push_object(ctx);
                push_multipart(ctx,bound,buf,bsz);
                duk_put_prop_string(ctx,-2,"postData");
            }
        } else 
        if(strcmp("application/json",ct)==0)
        {
            duk_push_array(ctx);
            duk_push_object(ctx);
            duk_push_string(ctx,"application/json");
            duk_put_prop_string(ctx,-2,"Content-Type");

            duk_get_global_string(ctx,"JSON");
            duk_get_prop_string(ctx,-1,"parse");
            duk_remove(ctx,-2);
            duk_get_prop_string(ctx, -4, "body");
            duk_buffer_to_string(ctx,-1);
            if(duk_pcall(ctx,1) != 0)
            {
                duk_push_object(ctx);
                duk_safe_to_string(ctx,-2);
                duk_pull(ctx,-2);
                duk_put_prop_string(ctx,-2,"error");
            }
            duk_put_prop_string(ctx,-2,"contents");
            duk_put_prop_index(ctx,-2,0);
            duk_put_prop_string(ctx,-2,"postData");
            
        } else
        if(strcmp("application/x-www-form-urlencoded",ct)==0)
        {
            char *s;
            duk_get_prop_string(ctx, -1, "body");
            duk_buffer_to_string(ctx,-1);
            s=(char *)duk_get_string(ctx,-1);
            duk_rp_querystring2object(ctx, s);
            duk_put_prop_string(ctx,-3,"postData");
            duk_pop(ctx);
        }
    }
    else
    {
        duk_pop(ctx);
        duk_put_prop_string(ctx, -2, "headers");
    }

    //duk_push_c_function(ctx,flatten_vars,0);
    //duk_put_prop_string(ctx, -2, "flatten");
    flatten_vars(ctx);
    duk_put_prop_string(ctx, -2, "params");
}
char msg500[] = "<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>%s</pre></p></body></html>";

static void http_callback(evhtp_request_t *req, void *arg);

static void send500(evhtp_request_t *req, char *msg)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    evbuffer_add_printf(req->buffer_out, msg500, msg);
    sendresp(req, 500);
}

static void send404(evhtp_request_t *req)
{
    if (dhs404)
    {
        http_callback(req, (void *) dhs404);
        return;
    }
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char msg[] = "<html><head><title>404 Not Found</title></head><body><h1>Not Found</h1><p>The requested URL was not found on this server.</p></body></html>";
    evbuffer_add(req->buffer_out, msg, strlen(msg));
    sendresp(req, EVHTP_RES_NOTFOUND);
}

static void send403(evhtp_request_t *req)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char msg[] = "<html><head><title>403 Forbidden</title></head><body><h1>Forbidden</h1><p>The requested URL is Forbidden.</p></body></html>";
    evbuffer_add(req->buffer_out, msg, strlen(msg));
    sendresp(req, 403);
}

static void dirlist(evhtp_request_t *req, char *fn)
{
    if (!dhs_dirlist)
    {
        send403(req);
        return;
    }
    
    DHS newdhs;

    newdhs.func_idx = dhs_dirlist->func_idx;
    newdhs.timeout  = dhs_dirlist->timeout;
    newdhs.pathlen  = dhs_dirlist->pathlen;
    newdhs.module   = dhs_dirlist->module;
    newdhs.aux = (void *)fn;
    http_callback(req, (void *) &newdhs);
}

#define CTXFREER struct ctx_free_buffer_data_str

CTXFREER
{
    duk_context *ctx;
    int threadno;
};

/* free the data after evbuffer is done with it, but check that it hasn't 
   been freed already */
static void refcb(const void *data, size_t datalen, void *val)
{
    CTXFREER *fp = val;
    duk_context *ctx = fp->ctx;
    int threadno = (int)fp->threadno;

    // check if the entire stack was already freed and replaced in redo_ctx
    if (ctx == thread_ctx[threadno])
        duk_free(ctx, (void *)data);
    free(fp);
}

/* free the data after evbuffer is done with it */
static void frefcb(const void *data, size_t datalen, void *val)
{
    free((void *)data);
}

/*
   Because of this: https://github.com/criticalstack/libevhtp/issues/160 
   when ssl, we can't use evbuffer_add_file() since evbuffer_pullup()
   was added to libevhtp.

   At some point (>5M??; need better benchmarking tools - wrk and ab both
   have their problems) the advantanges of loading the file here will
   be outweighted by the cost of not letting evbuffer_add_file() do its
   memmap thing.

   ** corresponding change made in evhtp.c **
*/
static int rp_evbuffer_add_file(struct evbuffer *outbuf, int fd, ev_off_t offset, ev_off_t length)
{
    /* if not ssl, or if size of file is "big", use evbuffer_add_file */
    if( !rp_using_ssl || length-offset > 5242880 )
        return evbuffer_add_file(outbuf, fd, offset, length);
    
    {
        size_t off=0;
        ssize_t nbytes=0;
        char *buf=NULL;
        if(offset)
        {
            if (lseek(fd, offset, SEEK_SET)==-1)
            {
                close(fd);
                return -1;
            }
        }

        REMALLOC(buf,length);

        while ((nbytes = read(fd, buf + off, length - off)) != 0)
        {
            off += nbytes;
        }
        close(fd);
        if (nbytes==-1)
            return -1;
        evbuffer_add_reference(outbuf, buf, (size_t)length, frefcb, NULL);
    }
    return 0;
}

/*
    Send a file, whole or partial with range if requested
      haveCT - non-zero if content-type is already set
      filestat - if already called stat, include here, otherwise set to NULL
*/
static void rp_sendfile(evhtp_request_t *req, char *fn, int haveCT, struct stat *filestat)
{
    int fd = -1;
    ev_off_t beg = 0, len = -1;
    ev_off_t filesize;
    evhtp_res rescode = EVHTP_RES_OK;
    struct stat newstat, *sb=&newstat;
    const char *range = NULL;
    mode_t mode;

    if (filestat)
    {
        sb=filestat;
    }
    else if (stat(fn, sb) == -1)
    {
        send404(req);
        return;
    }

    mode = sb->st_mode & S_IFMT;
    if (mode == S_IFREG)
    {
        if ((fd = open(fn, O_RDONLY)) == -1)
        {
            send404(req);
            return;
        }
    }
    else
    {
        send404(req);
        return;
    }

    filesize=(ev_off_t) sb->st_size; 

    if (!haveCT)
    {
        RP_MTYPES m;
        RP_MTYPES *mres, *m_p = &m;
        char *ext;

        ext = strrchr(fn, '.');

        if (!ext) /* || strchr(ext, '/')) shouldn't happen */
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/octet-stream", 0, 0));
        else
        {
            m.ext = ext + 1;
            /* look for proper mime type listed in mime.h */
            mres = bsearch(m_p, allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
            if (mres)
                evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));
            else
                evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/octet-stream", 0, 0));
        }
    }

    /* http range - give back partial file, set 206 */
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Accept-Ranges", "bytes", 0, 0));
    range=evhtp_kv_find(req->headers_in,"Range");
    if (range && !strncasecmp("bytes=", range, 6))
    {
        char *eptr;
        char reprange[128];

        beg = (ev_off_t)strtol(range + 6, &eptr, 10);
        if (eptr != range + 6)
        {
            ev_off_t endval;

            eptr++; // skip '-'
            if (*eptr != '\0')
            {
                endval = (ev_off_t)strtol(eptr, NULL, 10);
                if (endval && endval > beg)
                    len = endval - beg;
            }
            rescode = 206;
            /* Content-Range: bytes 12812288-70692914/70692915 */
            snprintf(reprange, 128, "bytes %d-%d/%d",
                     (int)beg,
                     (int)((len == -1) ? (filesize - 1) : endval),
                     (int)filesize);
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Range", reprange, 0, 1));
        }
        //range is no longer malloc'd
        //free(range); /* chicken 🐣 */
    }
    else 
        len=filesize;

    rp_evbuffer_add_file(req->buffer_out, fd, beg, len);
    sendresp(req, rescode);
}

static void sendredir(evhtp_request_t *req, char *fn)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("location", fn, 0, 1));
    evbuffer_add_printf(req->buffer_out, "<!DOCTYPE html>\n\
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
</html>",
                        fn, fn);
    sendresp(req, EVHTP_RES_FOUND);
}

static void
fileserver(evhtp_request_t *req, void *arg)
{
    DHMAP *map = (DHMAP *)arg;
    DHS *dhs = map->dhs;
    evhtp_path_t *path = req->uri->path;
    struct stat sb;
    /* take 2 off of key for the slash and * and add 1 for '\0' and one more for a potential '/' */
    char *s, fn[strlen(map->val) + strlen(path->full) + 4 - strlen(map->key)];
    mode_t mode;
    int i = 0;

    dhs->req = req;

    strcpy(fn, map->val);
    s=duk_rp_url_decode( path->full, strlen(path->full) );
    strcpy(&fn[strlen(map->val)], s + strlen(map->key) - 1);
    free(s);

    if (stat(fn, &sb) == -1)
    {
        /* 404 goes here */
        send404(req);
        return;
    }
    mode = sb.st_mode & S_IFMT;

    if (mode == S_IFREG)
        rp_sendfile(req, fn, 0, &sb);
    else if (mode == S_IFDIR)
    {
        /* add 11 for 'index.html\0' */
        char fnindex[strlen(fn) + 11];
        /* redirect if directory doesn't end in '/' */
        i = strlen(fn) - 1;
        if (fn[i] != '/')
        {
            fn[++i] = '/';
            fn[++i] = '\0';
            sendredir(req, &fn[strlen(map->val) - 1]);
            return;
        }
        strcpy(fnindex, fn);
        strcat(fnindex, "index.html");
        if (stat(fnindex, &sb) == -1)
        {
            /* TODO: add dir listing and
               setting to forbid dir listing */
            /* try index.htm */
            fnindex[strlen(fnindex) - 1] = '\0';
            if (stat(fnindex, &sb) == -1)
                dirlist(req, fn);
            else
                rp_sendfile(req, fnindex, 0, &sb);
        }
        else
            rp_sendfile(req, fnindex, 0, &sb);
    }
    else
        send404(req);
}


static void sendbuf(DHS *dhs)
{
    duk_size_t sz;
    const char *s;
    duk_context *ctx = dhs->ctx;

    if ( duk_is_buffer_data(ctx, -1) )
    {
        CTXFREER *freeme = NULL;
        REMALLOC(freeme, sizeof(CTXFREER));

        duk_to_dynamic_buffer(ctx, -1, &sz);
        s = duk_steal_buffer(ctx, -1, &sz);
        freeme->ctx = ctx;
        freeme->threadno = (int)dhs->threadno;
        evbuffer_add_reference(dhs->req->buffer_out, s, (size_t)sz, refcb, freeme);
    }
    else
    /* duk_to_buffer does a copy on a string, so we'll just skip that and copy directly */
    {
        s= duk_get_lstring(ctx, -1, &sz);
        if(s)
        {
            if (*s == '\\' && *(s + 1) == '@')
                s++;
            evbuffer_add(dhs->req->buffer_out,s,(size_t)sz);
        }
    }
}


static evhtp_res sendobj(DHS *dhs)
{
    RP_MTYPES m;
    RP_MTYPES *mres;
    int gotdata = 0, gotct = 0;
    duk_context *ctx = dhs->ctx;
    evhtp_res res = 200;

    if (duk_get_prop_string(ctx, -1, "headers"))
    {
        if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) && !duk_is_function(ctx, -1))
        {
            duk_enum(ctx, -1, 0);
            while (duk_next(ctx, -1, 1))
            {
                const char *key, *val;
                
                if( duk_is_string(ctx, -2) )
                    key=duk_get_string(ctx, -2);
                else
                    key=duk_json_encode(ctx, -2);

                if( duk_is_string(ctx, -1) )
                    val=duk_get_string(ctx, -1);
                else
                    val=duk_json_encode(ctx, -1);

                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));

                if (!strcasecmp(key, "content-type"))
                    gotct = 1;

                duk_pop_2(ctx);
            }

            duk_pop(ctx); /* pop enum and headers obj */
            duk_del_prop_string(ctx, -2, "headers");
        }
        else
        {
            duk_pop(ctx); /* get rid of 'headers' non object */
            duk_del_prop_string(ctx, -1, "headers");
            RP_THROW(ctx, "server.start: callback -- \"headers\" parameter in return value must be set to an object (headers:{...})");
        }
    }
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, -1, "status"))
    {
        res = (evhtp_res)duk_rp_get_int_default(ctx, -1,200);
        if (res < 100)
            res = 200;
        duk_del_prop_string(ctx, -2, "status");
    }
    duk_pop(ctx);

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        if (gotdata)
            goto opterr;

        m.ext = (char *)duk_to_string(ctx, -2);
        mres = bsearch(&m, allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
        if (mres)
        {
            const char *d;

            gotdata = 1;

            if (!gotct)
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));

            /* if data is a string and starts with '@', its a filename */
            if (duk_is_string(ctx, -1) && ((d = duk_get_string(ctx, -1)) || 1) && *d == '@')
            {
                rp_sendfile(dhs->req, (char *)d+1, 1, NULL);
                return (0);
            }
            else
                sendbuf(dhs); /* send buffer or string contents at idx=-1 */

            duk_pop_2(ctx);
            continue;
        }
        else
        {
            const char *bad_option = duk_to_string(ctx, -2);
            char estr[20 + strlen(bad_option)];
            printerr( "unknown option '%s'\n", bad_option);
            sprintf(estr, "unknown option '%s'", bad_option);
            send500(dhs->req, estr);
            return (0);
        }

        opterr:
            printerr( "Data already set from '%s', '%s=\"%s\"' is redundant.\n", m.ext, duk_to_string(ctx, -2), duk_to_string(ctx, -1));
            return (res);
    }
    // allow script to return just status:404 without content
    if(!gotdata && res==404)
    {
        send404(dhs->req);
        return 0;
    }

    duk_pop(ctx);
    return (res);
}

/* *************************************************
    FUNCTIONS FOR COPYING BETWEEN DUK HEAPS/STACKS
**************************************************** */

static int copy_obj(duk_context *ctx, duk_context *tctx, int objid);
static void clean_obj(duk_context *ctx, duk_context *tctx);

/* copy a single function given as callback to all stacks 
   To be called multiple times for each function on the 
   stack, and must be called in order of functions appearance
   on main_ctx stack
*/
static void copy_cb_func(DHS *dhs, int nt)
{
    void *bc_ptr;
    duk_size_t bc_len;
    int i = 0;
    duk_context *ctx = dhs->ctx;
    duk_idx_t fidx;

    duk_get_prop_index(ctx, 0, (duk_uarridx_t) dhs->func_idx);
    fidx=duk_get_top_index(ctx);

    duk_get_prop_string(ctx, fidx, "name");

    if (!strncmp(duk_get_string(ctx, -1), "bound ", 6))
        printerr("Error: server cannot copy a bound function to threaded stacks\n");
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, fidx, DUK_HIDDEN_SYMBOL("is_global")) && duk_get_boolean(ctx, -1))
    {
        const char *name;
        duk_pop(ctx); //true

        if (duk_get_prop_string(ctx, fidx, "name"))
        {
            name = duk_get_string(ctx, -1);
            duk_pop(ctx); //name string

            for (i = 0; i < nt; i++)
            {
                duk_context *tctx = thread_ctx[i];

                duk_get_global_string(tctx, name); //put already copied function on stack
                duk_push_string(tctx, name);       //add fname property to function
                duk_put_prop_string(tctx, -2, "fname");
                duk_put_prop_index(tctx, 0, (duk_uarridx_t) dhs->func_idx);
            }
            duk_push_string(ctx, name); //add fname property to function in ctx as well
            duk_put_prop_string(ctx, fidx, "fname");
            duk_remove(ctx,fidx);
            return; /* STOP HERE */
        }
    }
    duk_pop(ctx);

    /* CONTINUE HERE */
    /* if function is not global (i.e. map:{"/":function(req){...} } */
    duk_dup(ctx, fidx);
    duk_dump_function(ctx);
    bc_ptr = duk_get_buffer_data(ctx, -1, &bc_len);
    duk_dup(ctx, fidx); /* on top of stack for copy_obj */
    /* load function into each of the thread contexts at same position */
    for (i = 0; i < nt; i++)
    {
        duk_context *tctx = thread_ctx[i];
        void *buf = duk_push_fixed_buffer(tctx, bc_len);
        memcpy(buf, (const void *)bc_ptr, bc_len);
        duk_load_function(tctx);
        copy_obj(ctx, tctx, 0);
        clean_obj(ctx, tctx);
        duk_put_prop_index(tctx, 0, (duk_uarridx_t)dhs->func_idx); //put it in same position in array as main_ctx
    }
    duk_pop_2(ctx);
    duk_remove(ctx,fidx);
}

/* get bytecode from stash and execute it with 
   js arguments to this function                */
duk_ret_t duk_rp_bytefunc(duk_context *ctx)
{
    const char *name;

    duk_push_current_function(ctx);
    duk_get_prop_string(ctx, -1, "fname"); //we stored function name here
    name = duk_get_string(ctx, -1);
    duk_pop_2(ctx);

    duk_push_global_stash(ctx);         //bytecode compiled function was previously stored under "name" in stash
    duk_get_prop_string(ctx, -1, name); //get the bytecode compiled function
    duk_insert(ctx, 0);                 //put the bytecode compiled function at beginning
    duk_pop(ctx);                       //pop global stash
    duk_push_this(ctx);
    duk_insert(ctx, 1);
    duk_call_method(ctx, duk_get_top_index(ctx) - 1); //do it
    return 1;
}

/* copy function compiled from bytecode to stash
   create a function with the proper name that will
   call duk_rp_bytefunc() to execute.
   
   duktape does not allow bytecode compiled function
   to be pushed to global object afaik
*/
static void copy_bc_func(duk_context *ctx, duk_context *tctx)
{
    void *buf, *bc_ptr;
    duk_size_t bc_len;
    const char *name = duk_get_string(ctx, -2);
    duk_dup_top(ctx);
    duk_dump_function(ctx);                             //dump function to bytecode
    bc_ptr = duk_get_buffer_data(ctx, -1, &bc_len); //get pointer to bytecode
    buf = duk_push_fixed_buffer(tctx, bc_len);          //make a buffer in thread ctx
    memcpy(buf, (const void *)bc_ptr, bc_len);          //copy bytecode to new buffer
    duk_pop(ctx);                                       //pop bytecode from ctx

    duk_load_function(tctx);     //make copied bytecode in tctx a function
    duk_push_global_stash(tctx); //get global stash and save function there.
    duk_insert(tctx, -2);
    duk_put_prop_string(tctx, -2, name);
    duk_pop(tctx);

    duk_push_c_function(tctx, duk_rp_bytefunc, DUK_VARARGS); //put our wrapper function on stack
    duk_push_string(tctx, name);                             // add desired function name (as set in global stash above)
    duk_put_prop_string(tctx, -2, "fname");
}

static void copy_prim(duk_context *ctx, duk_context *tctx, const char *name)
{

    if (strcmp(name, "NaN") == 0)
        return;
    if (strcmp(name, "Infinity") == 0)
        return;
    if (strcmp(name, "undefined") == 0)
        return;

    switch (duk_get_type(ctx, -1))
    {
    case DUK_TYPE_NULL:
        duk_push_null(tctx);
        break;
    case DUK_TYPE_BOOLEAN:
        duk_push_boolean(tctx, duk_get_boolean(ctx, -1));
        break;
    case DUK_TYPE_NUMBER:
        duk_push_number(tctx, duk_get_number(ctx, -1));
        break;
    case DUK_TYPE_STRING:
        duk_push_string(tctx, duk_get_string(ctx, -1));
        break;
    default:
        duk_push_undefined(tctx);
    }
    duk_put_prop_string(tctx, -2, name);
}

static void clean_obj(duk_context *ctx, duk_context *tctx)
{
    const char *prev = duk_get_string(ctx, -2);
    /* prototype was not marked, so we need to dive into it regardless */
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("objRefId")) || (prev && !strcmp(prev, "prototype")))
    {
        duk_del_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("objRefId"));
        duk_enum(ctx, -2, DUK_ENUM_INCLUDE_HIDDEN);
        while (duk_next(ctx, -1, 1))
        {
            if (duk_is_object(ctx, -1) || duk_is_c_function(ctx, -1))
                clean_obj(ctx, NULL);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);
    if (tctx)
    {
        duk_push_global_stash(tctx);
        duk_del_prop_string(tctx, -1, "objById");
        duk_pop(tctx);
    }
}

typedef duk_ret_t (*duk_func)(duk_context *);

//#define cprintf(...)  printf(__VA_ARGS__);
#define cprintf(...) /* nada */

/* ctx object and tctx object should be at top of stack */
static int copy_obj(duk_context *ctx, duk_context *tctx, int objid)
{
    const char *s;

    /* for debugging */
    //const char *lastvar="global";
    //if(duk_is_string(ctx, -2) )
    //    lastvar=duk_get_string(ctx, -2);
    /* end for debugging */

    objid++;
    const char *prev = duk_get_string(ctx, -2);

    /************** dealing with circular/duplicate references ******************************/
    /* don't copy prototypes, they will match the return from "new object();" and not be new anymore */
    if (prev && !strcmp(prev, "prototype"))
        goto enum_object;

    cprintf("IN COPYOBJ %d\n", objid);
    /* check if we've seen this object before */
    if (duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("objRefId")))
    { // [ [par obj], [ref_objid] ]
        /* get previously seen object (as previously copied to tctx, and return its reference */
        int ref_objid = duk_get_int(ctx, -1);
        cprintf("looking for cached object #%d\n", ref_objid);
        duk_pop(ctx); // [ [par obj] ]
        cprintf("Circular/Duplicate Reference to %s Detected\n", duk_get_string(ctx, -2));
        // replace the copied object on top of tctx stack with the reference
        duk_push_global_stash(tctx);                   // [ [par obj], [global stash] ]
        if (!duk_get_prop_string(tctx, -1, "objById")) // [ [par obj], [global stash], [object|undef] ]
        {
            printf("big time error: this should never happen\n");
            duk_pop_2(tctx); // [ [par obj] ]
            return (objid);  // leave empty object there
        }
        duk_push_sprintf(tctx, "%d", ref_objid); //[ [par obj], [global stash], [stash_obj], [objid] ]
        if( !duk_get_prop(tctx, -2) )                  //[ [par obj], [global stash], [stash_obj], [obj_ref] ]
        {
            duk_pop_3(tctx);
            goto enum_object;
        }
        //printenum(tctx,-1);
        duk_insert(tctx, -4); //[ [obj_ref], [par obj], [global stash], [stash_obj] ]
        duk_pop_3(tctx);      //[ [obj_ref] ]
//if( !strcmp(lastvar,"\xffproxy_obj") ){
//    printf("########start##########\n");
//    printenum(tctx,-1);
//    printf("########end############\n");
//}
        cprintf("copied ref\n");
        return (objid); // stop recursion here.
    }
    duk_pop(ctx);

    /* if we get here, we haven't seen this object before so
       we label this object with unique number */
    duk_push_int(ctx, objid);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("objRefId"));
    /* in tctx, put it where we can find it later */
    //[ [par obj] ]
    duk_push_global_stash(tctx);                   //[ [par obj], [global stash] ]
    if (!duk_get_prop_string(tctx, -1, "objById")) //[ [par obj], [global stash], [object|undef] ]
    {                                              //[ [par obj], [global stash], [undefined] ]
        duk_pop(tctx);                             //[ [par obj], [global stash] ]
        duk_push_object(tctx);                     //[ [par obj], [global stash], [new_stash_obj] ]
    }
    /* copy the object on the top of stack before 
       entering copy_obj */
    cprintf("assigning object id=%d\n", objid);
    duk_push_sprintf(tctx, "%d", objid);      //[ [par obj], [global stash], [stash_obj], [objid] ]
    duk_dup(tctx, -4);                        //[ [par obj], [global stash], [stash_obj], [objid], [copy of par obj] ]
    duk_put_prop(tctx, -3);                   //[ [par obj], [global stash], [stash_obj->{objid:copy of par obj} ]
    duk_put_prop_string(tctx, -2, "objById"); //[ [par obj], [global stash->{"objById":[stash_obj]} ]
    duk_pop(tctx);                            //[ [par obj] ]
    /************** END dealing with circular references ***************************/

    //    cprintf("%s type %d (obj=%d) cfunc=%d, func=%d, obj=%d, obj already in tctx=%d\n",((prev)?prev:"null"),duk_get_type(ctx,-1), DUK_TYPE_OBJECT,
    //            duk_is_c_function(ctx,-1), duk_is_function(ctx,-1), duk_is_object(ctx,-1), duk_has_prop_string( tctx,-1,s ));

enum_object:

    /*  get keys,vals inside ctx object on top of the stack 
        and copy to the tctx object on top of the stack     */
    

    duk_enum(ctx, -1, DUK_ENUM_INCLUDE_HIDDEN|DUK_ENUM_INCLUDE_SYMBOLS);
    while (duk_next(ctx, -1, 1))
    {
        s = duk_get_string(ctx, -2);

        /* these are internal flags and should not be copied to threads */
        if(!strcmp(s, "\xffobjRefId") || !strcmp(s,"\xffthreadsafe") )
        {
            duk_pop_2(ctx);
            continue;
        }

        if (duk_is_ecmascript_function(ctx, -1))
        {
            /* turn ecmascript into bytecode and copy */
            copy_bc_func(ctx, tctx);
            /* recurse and copy JS properties attached to this duktape (but really c) function */
            objid = copy_obj(ctx, tctx, objid);

            if (!prev) // prev==NULL means this is a global
            {
                /* mark functions that are global as such to copy
                   by reference when named in a server.start({map:{}}) callback */
                duk_push_true(ctx);
                duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("is_global"));
            }
            duk_put_prop_string(tctx, -2, s);
        }

        else if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_STRING | DUK_TYPE_MASK_NUMBER | DUK_TYPE_MASK_BOOLEAN | DUK_TYPE_MASK_NULL | DUK_TYPE_MASK_UNDEFINED))
        {
            /* simple copy of primitives */
            copy_prim(ctx, tctx, s);
        }

        else if (duk_is_c_function(ctx, -1) && !duk_has_prop_string(tctx, -1, s))
        {
            /* copy pointers to c functions */
            duk_idx_t length;
            duk_func copyfunc = (duk_func)duk_get_c_function(ctx, -1);

            if (duk_get_prop_string(ctx, -1, "length"))
            {
                length = (duk_idx_t)duk_get_int(ctx, -1);
                if(!length) length = DUK_VARARGS; 
            }
            else
                length = DUK_VARARGS;
            duk_pop(ctx);

            duk_push_c_function(tctx, copyfunc, length);
            /* recurse and copy JS properties attached to this c function */
            objid = copy_obj(ctx, tctx, objid);
            duk_put_prop_string(tctx, -2, s);
        }

        else if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1) && !duk_is_c_function(ctx, -1))
        {
            /* copy normal {} objects */
            if (!duk_has_prop_string(tctx, -1, s) &&
                strcmp(s, "console") != 0 &&
                strcmp(s, "performance") != 0)
            {
                cprintf("copy %s{}\n", s);
                /* recurse and begin again with this ctx object (at idx:-1)
                   and a new empty object for tctx (pushed to idx:-1)              */
                duk_push_object(tctx);
                objid = copy_obj(ctx, tctx, objid);
                duk_put_prop_string(tctx, -2, duk_get_string(ctx, -2));
            }
        }
        else if (duk_is_pointer(ctx, -1) )
        {
            duk_push_pointer(tctx, duk_get_pointer(ctx, -1) );
            duk_put_prop_string(tctx, -2, s);
        }

        /* remove key and val from stack and repeat */
        duk_pop_2(ctx);
    }

    /* remove enum from stack */
    duk_pop(ctx);

    // flag object as copied, in case needed by some function
    duk_push_true(tctx);
    duk_put_prop_string(tctx, -2, DUK_HIDDEN_SYMBOL("thread_copied"));

    if (duk_has_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("proxy_obj")) )
    {
        //printenum(tctx, -1);
        duk_get_prop_string(tctx, -1, DUK_HIDDEN_SYMBOL("proxy_obj"));
        duk_push_proxy(tctx, -1);
        //printf("--------------------------------------------\n");
        //printenum(tctx, -1);
    }

    /* keep count */
    return objid;
}

static void copy_all(duk_context *ctx, duk_context *tctx)
{
    duk_push_global_object(ctx);
    duk_push_global_object(tctx);
    /* copy_obj expects objects on top of both stacks */
    copy_obj(ctx, tctx, 0);
    /* remove hidden symbols and stash-cache used for reference tracking */
    clean_obj(ctx, tctx);
    /* remove global object from both stacks */
    duk_pop(ctx);
    duk_pop(tctx);
}

static void copy_mod_func(duk_context *ctx, duk_context *tctx, duk_idx_t idx)
{
    const char *modname;

    duk_get_prop_index(ctx, 0, (duk_uarridx_t)idx);

    if( duk_get_prop_string(ctx,-1,"module") )
    {
        modname=duk_get_string(ctx,-1);
        duk_pop_2(ctx);

        duk_push_object(tctx);
        duk_push_string(tctx,modname);
        duk_put_prop_string(tctx,-2,"module");
        duk_put_prop_index(tctx, 0, (duk_uarridx_t)idx);
        return;
    }
    duk_pop(ctx);

    if( duk_get_prop_string(ctx,-1,"modulePath") )
    {
        modname=duk_get_string(ctx,-1);
        duk_pop_2(ctx);

        duk_push_object(tctx);
        duk_push_string(tctx,modname);
        duk_put_prop_string(tctx,-2,"modulePath");
        duk_put_prop_index(tctx, 0, (duk_uarridx_t)idx);
        return;
    }
    duk_pop(ctx);//should never get here
}

/* ******************************************
  END FUNCTIONS FOR COPYING BETWEEN DUK HEAPS
********************************************* */

static evthr_t *get_request_thr(evhtp_request_t *request)
{
    evhtp_connection_t *htpconn;

    htpconn = evhtp_request_get_connection(request);

    return htpconn->thread;
}

/* struct to pass to http_dothread
   containing all relevant vars */
#define DHR struct duk_http_req_s
DHR
{
    evhtp_request_t *req;
    DHS *dhs;
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

/* get the function, whether its a function or a (module) object containing the function */
#define getfunction(dhs) do {\
    duk_get_prop_index( (dhs)->ctx, 0, (duk_uarridx_t)((dhs)->func_idx) );\
    if( !duk_is_function( (dhs)->ctx, -1) ) {\
        /* printf("getting %s from %d\n",(dhs)->module_name, (int)(dhs)->func_idx);printstack((dhs)->ctx);*/\
        duk_get_prop_string( (dhs)->ctx, -1, (dhs)->module_name);\
        duk_remove( (dhs)->ctx, -2); /* the module object */\
    }\
} while (0)

static void *http_dothread(void *arg)
{
    DHR *dhr = (DHR *)arg;
    evhtp_request_t *req = dhr->req;
    DHS *dhs = dhr->dhs;
    duk_context *ctx=dhs->ctx;
    evhtp_res res = 200;
    int eno;

#ifdef RP_TIMEO_DEBUG
    pthread_t x = dhr->par;
#endif
    if (dhr->have_timeout)
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* copy function ref to top of stack */
    getfunction(dhs);
    /* push an empty object */
    duk_push_object(ctx);
    /* populate object with details from client */
    push_req_vars(dhs);

    /* delete any previous setting of threadsafe flag */
    duk_push_global_object(ctx);
    duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("threadsafe"));
    duk_pop(ctx);

    /* execute function "myfunc({object});" */
    if ((eno = duk_pcall(ctx, 1)))
    {
        if (dhr->have_timeout)
        {
            pthread_mutex_lock(&(dhr->lock));
            pthread_cond_signal(&(dhr->cond));
        }

        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
        if (duk_is_error(ctx, -1) )
        {
            duk_get_prop_string(ctx, -1, "stack");
            evbuffer_add_printf(req->buffer_out, msg500, duk_safe_to_string(ctx, -1));
            printerr("error in callback: '%s'\n", duk_safe_to_string(ctx, -1));
            duk_pop(ctx);
        }
        else if (duk_is_string(ctx, -1))
        {
            evbuffer_add_printf(req->buffer_out, msg500, duk_safe_to_string(ctx, -1));
        }
        else
        {
            evbuffer_add_printf(req->buffer_out, msg500, "unknown error");
        }
        sendresp(req, 500);

        if (dhr->have_timeout)
            pthread_mutex_unlock(&(dhr->lock));

        return NULL;
    }
    /* a function might conditionally run a c function that sets "threadsafe" false
       check again if this has changed */

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("threadsafe"));
    if (duk_is_boolean(ctx, -1) && !duk_get_boolean(ctx,-1) )
    {
        duk_get_prop_index(ctx, 0, (duk_uarridx_t)dhs->func_idx);
        duk_push_false(ctx);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("threadsafe") );
        duk_pop(ctx);
    }
    duk_pop(ctx);
    
    if (dhr->have_timeout)
    {
        debugf("0x%x, LOCKING in thread_cb\n", (int)x);
        fflush(stdout);
        pthread_mutex_lock(&(dhr->lock));
        pthread_cond_signal(&(dhr->cond));
    }
    /* stack now has return value from duk function call */

    /* set some headers */
    setheaders(req);

    /* don't accept functions or arrays */
    if (duk_is_function(ctx, -1) || duk_is_array(ctx, -1))
    {
        RP_THROW(ctx, "Return value cannot be an array or a function");
    }
    /* object has reply data and options in it */
    if (duk_is_object(ctx, -1))
        res = sendobj(dhs);
    else /* send string or buffer to client */
        sendbuf(dhs);

    if (res)
        sendresp(req, res);

    duk_pop(ctx);

    debugf("0x%x, UNLOCKING in thread_cb\n", (int)x);
    fflush(stdout);
    if (dhr->have_timeout)
        pthread_mutex_unlock(&(dhr->lock));

    return NULL;
}

static duk_context *redo_ctx(int thrno)
{
    duk_context *thr_ctx = duk_create_heap_default();
    duk_idx_t fno = 0;
    void *bc_ptr, *buf;
    duk_size_t bc_len;

    duk_init_context(thr_ctx);
    duk_push_array(thr_ctx);
    /* copy all the functions previously stored at bottom of stack */
    if (pthread_mutex_lock(&ctxlock) == EINVAL)
    {
        printerr( "could not obtain lock in http_callback\n");
        exit(1);
    }

#ifdef COMBINE_EVLOOPS
    /* 
        in this case server.start() has returned 
        deleting the stack which would have our functions
        and our array of functions have instead been stashed
    */
    duk_push_global_stash(main_ctx);
    duk_get_prop_string(main_ctx, -1, "funcstash");
    duk_insert(main_ctx, 0);
    duk_pop(main_ctx);
#endif
    copy_all(main_ctx, thr_ctx);
        
    duk_get_prop_index(main_ctx,0,fno);
    while(!duk_is_undefined(main_ctx,-1))
    {
        /* check if this is a module function */
        if(!duk_is_function(main_ctx,-1) )
        {
            duk_push_object(thr_ctx);
            copy_obj(main_ctx,thr_ctx,0);
            clean_obj(main_ctx,thr_ctx);
            duk_put_prop_index(thr_ctx,0,fno); //put object in same position as was in main_ctx
            
            duk_pop(main_ctx); // object from array[fno]
            duk_get_prop_index(main_ctx,0, ++fno); // for next loop in while()
            continue;
        }

        if (
            duk_get_prop_string(main_ctx, -1, DUK_HIDDEN_SYMBOL("is_global")) 
                && 
            duk_get_boolean_default(main_ctx, -1, 0)  
           )
        {
            const char *name;

            if (duk_get_prop_string(main_ctx, -1, "name"))
            {
                name = duk_get_string(main_ctx, -1);
                duk_pop_2(main_ctx); //name string and true

                duk_get_global_string(thr_ctx, name); //put already copied function on stack
                duk_push_string(thr_ctx, name);       //add fname property to function
                duk_put_prop_string(thr_ctx, -2, "fname");
                duk_put_prop_index(thr_ctx,0,fno); //put function in same position as was in main_ctx

                //next round
                duk_pop(main_ctx); // object from array[fno]
                duk_get_prop_index(main_ctx,0, ++fno); // for next loop in while()
                continue;
            }
            duk_pop(main_ctx);// undefined from get_prop_string "name"
        }
        duk_pop(main_ctx); //undefined or boolean from get_prop_string is_global

        /* copy the function */
        duk_dump_function(main_ctx);
        bc_ptr = duk_get_buffer_data(main_ctx, -1, &bc_len);
        buf = duk_push_fixed_buffer(thr_ctx, bc_len);
        memcpy(buf, (const void *)bc_ptr, bc_len);
        duk_load_function(thr_ctx);
        duk_put_prop_index(thr_ctx,0,fno);

        //next round
        duk_pop(main_ctx); //bytecode
        duk_get_prop_index(main_ctx,0, ++fno);// for next loop in while()

    }

    /* renumber this thread in global "rampart" variable */
    if (!duk_get_global_string(thr_ctx, "rampart"))
    {
        duk_pop(thr_ctx);
        duk_push_object(thr_ctx);
    }
    duk_push_int(thr_ctx, thrno);
    duk_put_prop_string(thr_ctx, -2, "thread_id");
    duk_put_global_string(thr_ctx, "rampart");

#ifdef COMBINE_EVLOOPS
    duk_remove(main_ctx,0);
#endif

    pthread_mutex_unlock(&ctxlock);

    duk_destroy_heap(thread_ctx[thrno]);
    thread_ctx[thrno] = thr_ctx;
    return thr_ctx;
}

static void
http_thread_callback(evhtp_request_t *req, void *arg, int thrno)
{
    DHS *dhs = arg;
    DHR *dhr = NULL;
    pthread_t script_runner;
    pthread_attr_t attr;
    struct timespec ts;
    struct timeval now;
    int ret = 0;
	uint64_t nsec=0;

    //printf("in thread\n");
#ifdef RP_TIMEO_DEBUG
    pthread_t x = pthread_self();
    printf("%d, start\n", (int)x);
    fflush(stdout);
#endif

    gettimeofday(&now, NULL);
    DUKREMALLOC(dhs->ctx, dhr, sizeof(DHR));
    dhr->dhs = dhs;
    dhr->req = req;
#ifdef RP_TIMEO_DEBUG
    dhr->par = x;
#endif
    /* if no timeout, not necessary to thread out the callback */
    if (dhs->timeout.tv_sec == RP_TIME_T_FOREVER)
    {
        debugf("no timeout set");
        dhr->have_timeout = 0;
        (void)http_dothread(dhr);
        return;
    }

    debugf("0x%x: with timeout set\n", (int)x);
    dhr->have_timeout = 1;
    ts.tv_sec = now.tv_sec + dhs->timeout.tv_sec;
    nsec = (dhs->timeout.tv_usec + now.tv_usec) * 1000;
	if(nsec > 1000000000)
	{
		ts.tv_sec += nsec/1000000000;
		nsec=nsec % 1000000000;
	}
	ts.tv_nsec=nsec;

    debugf("now= %d.%06d, timeout=%d.%6d\n",(int)now.tv_sec, (int)now.tv_usec, (int)ts.tv_sec, (int)(ts.tv_nsec/1000));

    if (pthread_mutex_init(&(dhr->lock), NULL) == EINVAL)
    {
        printerr( "could not initialize lock in http_callback\n");
        exit(1);
    }

    pthread_cond_init(&(dhr->cond), NULL);

    /* is this necessary? https://computing.llnl.gov/tutorials/pthreads/#Joining */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    debugf("0x%x, LOCKING in http_callback\n", (int)x);
    if (pthread_mutex_lock(&(dhr->lock)) == EINVAL)
    {
        printerr( "could not obtain lock in http_callback\n");
        exit(1);
    }

    pthread_create(&script_runner, &attr, http_dothread, dhr);
    debugf("0x%x, cond wait, UNLOCKING\n", (int)x);
    fflush(stdout);

    /* unlock dhr->lock and wait for pthread_cond_signal in http_dothread */
    ret = pthread_cond_timedwait(&(dhr->cond), &(dhr->lock), &ts);
    /* reacquire lock after timeout or condition signalled */
    debugf("0x%x, cond wait satisfied, ret=%d (==%d(ETIMEOUT)), LOCKED\n", (int)x, (int)ret, (int)ETIMEDOUT);
    fflush(stdout);

    if (ret == ETIMEDOUT)
    {
        debugf("%d, cancelling\n", (int)x);
        fflush(stdout);
        pthread_cancel(script_runner);
        debugf("%d, cancelled\n", (int)x);
        fflush(stdout);
        
        dhs->ctx = redo_ctx(thrno);
        debugf("0x%x, context recreated after timeout\n", (int)x);
        fflush(stdout);
        if(dhs->module)
        {
            duk_get_prop_index(dhs->ctx, 0, (duk_uarridx_t)dhs->func_idx);
            if(duk_get_prop_string(dhs->ctx, -1, "module"))
                printerr( "timeout in module:%s()\n", duk_to_string(dhs->ctx, -1));
            else
                printerr( "timeout in callback\n");
        
            duk_pop_2(dhs->ctx);
        }
        else
        {
            duk_get_prop_index(dhs->ctx, 0, (duk_uarridx_t)dhs->func_idx);
            duk_get_prop_string(dhs->ctx, -1, "fname");
            if (duk_is_undefined(dhs->ctx, -1))
            {
                duk_pop(dhs->ctx);
                if(duk_get_prop_string(dhs->ctx, -1, "module"))
                {
                    printerr( "timeout in module:%s()\n", duk_to_string(dhs->ctx, -1));
                }
                else
                    printerr( "timeout in callback\n");
            }
            else
                printerr( "timeout in %s()\n", duk_to_string(dhs->ctx, -1));

            duk_pop_2(dhs->ctx);
        }
        send500(req, "Timeout in Script");
    }
#ifdef RP_TIMEO_DEBUG
    gettimeofday(&now, NULL);
    printf("at end: now= %d.%06d, timeout=%d.%6d\n",(int)now.tv_sec, (int)now.tv_usec, (int)ts.tv_sec, (int)(ts.tv_nsec/1000));
#endif
    pthread_join(script_runner, NULL);
    pthread_cond_destroy(&(dhr->cond));
    free(dhr);
    debugf("exiting http_thread_callback\n");
}
/* call with 0.0 to start, use return from first call to get elapsed */
static double
stopwatch(double dtime)
{
    struct timeval time;

    gettimeofday(&time, NULL);

    return (
        ((double)time.tv_sec + ((double)time.tv_usec / 1e6)) - dtime);
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

#define killfork do {\
 kill(finfo->childpid, SIGTERM);\
 finfo->childpid=0;\
 close(finfo->child2par);\
 close(finfo->par2child);\
 finfo->child2par = -1;\
 finfo->par2child = -1;\
} while(0)

#define EVFDATA struct event_fork_data_s
EVFDATA {
    DHS *dhs;
    evhtp_connection_t *conn;
    int par2child;
    int child2par;
    int have_threadsafe_val;
};
#define childwrite(a,b,c) do{\
    if(write((a),(b),(c))==-1) {\
        printerr("child->parent write failed: '%s' -- exiting\n",strerror(errno));\
        exit(1);\
    };\
} while(0)
#define childread(a,b,c) do{\
    if(read((a),(b),(c))==-1) {\
        printerr("child<-parent read failed: '%s' -- exiting\n",strerror(errno));\
        exit(1);\
    };\
} while(0)

extern struct event_base *elbase;

struct timeval evimmediate={0,0};

static void fork_exec_js(evutil_socket_t fd_ignored, short flags, void *arg)
{
    EVFDATA *data= (EVFDATA *) arg;
    DHS *dhs= data->dhs;
    duk_context *ctx = dhs->ctx;
    int child2par = data->child2par;
    int eno=0;
    char *cbor=NULL;
    duk_size_t bufsz;
    uint32_t msgOutSz = 0;
    tprintf("doing fork callback\n");
    if ((eno = duk_pcall(ctx, 1)))
    {
        if (duk_is_error(ctx, -1) || duk_is_string(ctx, -1))
        {

            char msg[] = "{\"status\":500,\"html\":\"<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>\\n%s</pre></p></body></html>\"}";
            char *err;
            if (duk_is_error(ctx, -1))
            {
                duk_get_prop_string(ctx, -1, "stack");
                duk_remove(ctx,-2);
            }
            printf("error in callback: %s\n",duk_get_string(ctx,-1));
            err = (char *)duk_json_encode(ctx, -1);
            //get rid of quotes
            err[strlen(err) - 1] = '\0';
            err++;
            duk_push_sprintf(ctx, msg, err);
        }
        else
        {
            duk_push_string(ctx, "{\"headers\":{\"status\":500},\"html\":\"<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>unknown javascript error</pre></p></body></html>\"}");
        }
        duk_json_decode(ctx, -1);
    }
    /* encode the result of duk_pcall (or the error message) */
    duk_cbor_encode(ctx, -1, 0);
    cbor = duk_get_buffer_data(ctx, -1, &bufsz);
    msgOutSz = (uint32_t)bufsz;

    if(data->conn)
    {
        /* on first loop, safe to close and free here */
        evutil_closesocket(data->conn->sock);
        evhtp_connection_free(data->conn);
        data->conn=NULL;
    }

    /* first: write the size of the message to parent */
    childwrite(child2par, &msgOutSz, sizeof(uint32_t));

    /* second: a single char for thread_safe info */
    if (!data->have_threadsafe_val)
    {
        if (
            duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("threadsafe")) &&
            duk_is_boolean(ctx, -1) &&
            duk_get_boolean(ctx, -1) == 0)
            childwrite(child2par, "F", 1); //message that thread safe==false
        else
            childwrite(child2par, "X", 1); //message that thread safe is not to be set one way or another

        duk_push_global_object(ctx);
        duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("threadsafe"));
        duk_pop_2(ctx);
        data->have_threadsafe_val = 1;
    }
    else
    {
        childwrite(child2par, "X", 1); //message that thread safe is not to be set one way or another
    }

    /* third: write the encoded message */
    tprintf("child sending cbor message %d long on %d:\n", (int)msgOutSz, child2par);
    childwrite(child2par, cbor, msgOutSz);

    tprintf("child sent message %d long. Waiting for input\n", (int)msgOutSz);

    duk_pop(ctx); //buffer
} 

static void fork_read_request(evutil_socket_t fd_ignored, short flags, void *arg)
{
    EVFDATA *data= (EVFDATA *) arg;
    DHS *dhs= data->dhs;
    duk_context *ctx = dhs->ctx;
    int par2child = data->par2child;
    uint32_t msgOutSz = 0;
    fcntl(par2child, F_SETFL, 0);

    /* wait here for the next request and get it's size */
    childread(par2child, &msgOutSz, sizeof(uint32_t));
    tprintf("child to receive message %d long on %d\n", (int)msgOutSz, par2child);

    /* whether we already know the thread-safe status */ 
    childread(par2child, &data->have_threadsafe_val, sizeof(int));

    /* the index of the function we will use for javascript callback */
    childread(par2child, &(dhs->func_idx), sizeof(duk_idx_t)); 
    /* now get the cbor message from parent */
    {
        char *jbuf=NULL, *p;
        
        /* buffer to hold cbor data, sized appropriately */
        REMALLOC(jbuf,(size_t)msgOutSz);
        p=jbuf;
        size_t toread = (size_t)msgOutSz;
        ssize_t TOTread = 0;

        /* read the cbor message from parent */
        TOTread = read(par2child, p, toread);
        while (TOTread > 0)
        {
            toread -= (size_t)TOTread;
            tprintf("read %d bytes, %d bytes left\n",(int)TOTread,(int)toread);
            p += TOTread;
            TOTread = read(par2child, p, toread);
        }
        memcpy(duk_push_fixed_buffer(ctx, (duk_size_t)msgOutSz), jbuf, (size_t)msgOutSz);
        free(jbuf);
    }
    /* decode cbor message */
    duk_cbor_decode(ctx, -1, 0);

    /* get the module name, if any */
    if( duk_get_prop_string(ctx, -1, "rp_module_name" ) )
    {
        dhs->module_name=(char *) duk_get_string(ctx,-1);
        duk_del_prop_string(ctx, -2, "rp_module_name");
    }
    else
        dhs->module_name=NULL;
    duk_pop(ctx);
    
    /* pull function out of stash and prepare to call it with parameters from cbor decoded message */
    getfunction(dhs);
    /* move message/object to top */
    duk_pull(ctx,-2);

    fork_exec_js(-1,0,data);
    fcntl(par2child, F_SETFL, O_NONBLOCK);
}

static void
http_fork_callback(evhtp_request_t *req, DHS *dhs, int have_threadsafe_val)
{
    evhtp_res res = 200;
    int preforked = 0, child2par[2], par2child[2], pidstatus;
    double cstart, celapsed;
    FORKINFO *finfo = forkinfo[dhs->threadno];
    char *cbor;
    uint32_t msgOutSz = 0;
    duk_context *ctx = dhs->ctx;

    signal(SIGPIPE, SIG_IGN); //macos
    signal(SIGCHLD, SIG_IGN);

    cstart = stopwatch(0.0);
    tprintf("in fork\n");
    /* use waitpid to eliminate the possibility of getting a process that isn't our child */
    if (finfo->childpid && !waitpid(finfo->childpid, &pidstatus, WNOHANG))
    {
        tprintf("module=%d\n",(int)dhs->module);
        /* module changed, so must refork */
        if(dhs->module==MODULE_FILE_RELOAD)
            killfork;
        /* if request body is large, fork again rather than piping request.
           Pipe data in req.body and req.contents are shared and will
           double when serialized and piped
        */
        else if(evbuffer_get_length(dhs->req->buffer_in) > 1048576)
        {
            //printf("request is large, killing and reforking\n");
            killfork;
        }
        else
        {
            preforked = 1;
        }
    }
    if(!preforked)
    {
        /* our first run.  create pipes and setup for fork */
        if (pipe(child2par) == -1)
        {
            printerr( "child2par pipe failed\n");
            return;
        }

        if (pipe(par2child) == -1)
        {
            printerr( "par2child pipe failed\n");
            return;
        }
        /* if child died, close old handles */
        if (finfo->child2par > 0)
        {
            close(finfo->child2par);
            finfo->child2par = -1;
        }
        if (finfo->par2child > 0)
        {
            close(finfo->par2child);
            finfo->par2child = -1;
        }

        /***** fork ******/
        finfo->childpid = fork();

        if (finfo->childpid < 0)
        {
            printerr( "fork failed");
            finfo->childpid = 0;
            return;
        }
    }

    /* *************** CHILD PROCESS ************* */ 
    if (!preforked && finfo->childpid == 0)
    { /* child is forked once then talks over pipes. 
           and is killed if timed out                  */

        RP_TX_isforked=1; /* mutex locking not necessary in fork */
        struct sigaction sa;
        evhtp_connection_t *conn = evhtp_request_get_connection(req);
        struct event *ev;

        EVFDATA data_s={0}, *data=&data_s;

        memset(&sa, 0, sizeof(struct sigaction));
        sa.sa_flags = 0;
        sa.sa_handler = rp_exit;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR1, &sa, NULL);

        close(child2par[0]);
        close(par2child[1]);

        tprintf("beginning with par2child=%d, child2par=%d\n", par2child[0], child2par[1]);
        /* on first round, just forked, so we have all the request info
            no need to pipe it */
        /* copy function ref to top of stack */
        getfunction(dhs);

        /* push an empty object */
        duk_push_object(ctx);
        /* populate object with details from client (requires conn) */
        push_req_vars(dhs);

        data->dhs=dhs;
        data->par2child=par2child[0];
        data->child2par=child2par[1];
        data->have_threadsafe_val=have_threadsafe_val;
        data->conn=conn;
        /* TODO: find why this causes delay/hang
        event_reinit(elbase);
        event_base_loopbreak(elbase); //stop event loop in the child
        event_base_free(elbase); // free it
        */
        elbase = event_base_new(); // redo it

        duk_push_global_stash(ctx);
        duk_push_pointer(ctx, (void*)elbase);
        duk_put_prop_string(ctx, -2, "elbase");
        duk_pop(ctx);

        //do the first js exec once here
        ev=event_new(elbase, -1, 0,  fork_exec_js, data);
        event_add(ev, &evimmediate);
        /* make pipe non-blocking */
        fcntl(data->par2child, F_SETFL, O_NONBLOCK);
        ev=event_new(elbase, data->par2child, EV_PERSIST|EV_READ,  fork_read_request, data);
        event_add(ev, NULL);
        /* start new event loop here */
        event_base_loop(elbase, 0);

        close(child2par[1]);
        exit(0);
    }
    else

#define parentabort do{\
    printerr( "server.c line %d: '%s'\n", __LINE__, strerror(errno));\
    send500(req, "Error in Child Process");\
    killfork;\
    return;\
} while(0)

#define parentwrite(a,b,c) do{\
    if(write((a),(b),(c))==-1) {\
        printerr("parent->child write failed: '%s'\n",strerror(errno));\
        parentabort;\
    }\
} while(0)
#define parentread(a,b,c) do{\
    if(read((a),(b),(c))==-1) {\
        printerr("parent<-child read failed: '%s'\n",strerror(errno));\
        parentabort;\
    }\
} while(0)

    /* ********************* PARENT ******************** */
    {
        int totread = 0, is_threadsafe = 1;
        uint32_t bsize;
        duk_size_t bufsz;

        /* check child is still alive */
        if (preforked)
        {
            tprintf("is preforked in parent, c2p:%d p2c%d\n", finfo->child2par, finfo->par2child);

            /* push an empty object */
            duk_push_object(ctx);
            /* populate object with details from client */
            push_req_vars(dhs);

            /* add module info to pass onto child process */
            if(dhs->module_name)
            {
                duk_push_string(ctx,dhs->module_name);
                duk_put_prop_string(ctx, -2, "rp_module_name" );
            }

            /* convert to cbor and send to child */
            duk_cbor_encode(ctx, -1, 0);
            cbor = duk_get_buffer_data(ctx, -1, &bufsz);

            msgOutSz = (uint32_t)bufsz;

            tprintf("parent sending cbor, length %d\n", (int)msgOutSz);
            parentwrite(finfo->par2child, &msgOutSz, sizeof(uint32_t));
            parentwrite(finfo->par2child, &have_threadsafe_val,sizeof(int));
            parentwrite(finfo->par2child, &(dhs->func_idx), sizeof(duk_idx_t));
            parentwrite(finfo->par2child, cbor, msgOutSz);
            tprintf("parent sent cbor, length %d\n", (int)msgOutSz);

            duk_pop(ctx);
        }
        else
        {
            tprintf("first run in parent c2p:%d p2c%d\n", child2par[0], par2child[1]);
            close(child2par[1]);
            close(par2child[0]);
            finfo->child2par = child2par[0];
            finfo->par2child = par2child[1];
        }
        tprintf("parent waiting for input\n");

        if (dhs->timeout.tv_sec == RP_TIME_T_FOREVER)
        {
            if (-1 == read(finfo->child2par, &bsize, sizeof(uint32_t)))
                parentabort;
        }
        else
        {
            double maxtime = (double)dhs->timeout.tv_sec + ((double)dhs->timeout.tv_usec / 1e6);

            /* this loops 4 times for the minimal callback returning a string
               on a sandy bridge 3.2ghz core (without the time check, just the usleep) */
            fcntl(finfo->child2par, F_SETFL, O_NONBLOCK);

            /* wait for and read the size int from child */
            while (-1 == read(finfo->child2par, &bsize, sizeof(uint32_t)))
            {
                celapsed = stopwatch(cstart);
                if (celapsed > maxtime)
                {
                    int loop = 0;
                    kill(finfo->childpid, SIGUSR1);
                    while (!waitpid(finfo->childpid, &pidstatus, WNOHANG))
                    {
                        usleep(50000);
                        if (loop++ > 4)
                        {
                            printerr( "hard timeout in script, no graceful exit.\n");
                            kill(finfo->childpid, SIGTERM);
                            break;
                        }
                    }
                    finfo->childpid = 0;
                    duk_get_prop_index(ctx, 0, (duk_uarridx_t)dhs->func_idx);
                    duk_get_prop_string(ctx, -1, "fname");
                    printerr( "timeout in %s() %f > %f\n", duk_to_string(ctx, -1), celapsed, maxtime);
                    duk_pop_2(ctx);
                    send500(req, "Timeout in Script");
                    return;
                }
                usleep(1000);
            }
            /* reset to blocking mode */
            fcntl(finfo->child2par, F_SETFL, 0);
        }

        /* get thread safe info char and the message.
           put message in a buffer for decoding      */
        tprintf("parent about to read %lld bytes\n", (long long)bsize);
        /* if child is dead here, bsize is bad, bad things will be read later, causing big boom */
        if (waitpid(finfo->childpid, &pidstatus, WNOHANG))
        {
            send500(req, "Error in Child Process");
            return;
        }
        {
            char jbuf[bsize + 1], *p = jbuf, c;
            int toread = (int)bsize;

            parentread(finfo->child2par, &c, 1);
            if (c == 'F')
                is_threadsafe = 0;

            while ((totread = read(finfo->child2par, p, toread)) > 0)
            {
                tprintf("total read=%d\n", totread);
                toread -= totread;
                p += totread;
            }
            if (totread < 1 && toread > 0)
            {
                printerr( "server.c line %d, errno=%d\n", __LINE__, errno);
                killfork;
                send500(req, "Error in Child Process");
                return;
            }
            memcpy(duk_push_fixed_buffer(ctx, (duk_size_t)bsize), jbuf, (size_t)bsize);
        }
        /* decode message */
        duk_cbor_decode(ctx, -1, 0);
        /* if we don't already have it, set the threadsafe info on the function */
        if (!have_threadsafe_val)
        {
            getfunction(dhs);
            duk_push_boolean(ctx, (duk_bool_t)is_threadsafe);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("threadsafe"));
            duk_pop(ctx);
            /* mark it as thread safe on main_ctx as well (for timeout/redo_ctx, but not if module) */
            if(!dhs->module)
            {
                if (pthread_mutex_lock(&ctxlock) == EINVAL)
                {
                    printerr( "could not obtain lock in http_callback\n");
                    exit(1);
                }

#ifdef COMBINE_EVLOOPS
                duk_push_global_stash(main_ctx);
                duk_get_prop_string(main_ctx, -1, "funcstash");
                duk_insert(main_ctx, 0);
                duk_pop(main_ctx);
#endif

                duk_get_prop_index(main_ctx, 0, (duk_uarridx_t)dhs->func_idx);
                duk_push_boolean(main_ctx, (duk_bool_t)is_threadsafe);
                duk_put_prop_string(main_ctx, -2, DUK_HIDDEN_SYMBOL("threadsafe"));
                duk_pop(main_ctx);

#ifdef COMBINE_EVLOOPS
                duk_remove(main_ctx,0);
#endif
                pthread_mutex_unlock(&ctxlock);
            }
        }
    }
    /* stack now has return value from duk function call */

    /* set some headers */
    setheaders(req);
    /* don't accept arrays */
    if (duk_is_array(ctx, -1))
    {
        RP_THROW(ctx, "Return value cannot be an array");
        (void)duk_throw(ctx);
    }

    /* object has reply data and options in it */
    if (duk_is_object(ctx, -1))
        res = sendobj(dhs);
    else /* send string or buffer to client */
        sendbuf(dhs);

    if (res)
        sendresp(req, res);
    duk_pop(ctx);
    tprintf("Parent exit\n");
}

/* load module if not loaded, return position on stack
    if module file is newer than loaded version, reload 
*/
static int getmod(DHS *dhs)
{
    duk_idx_t idx=dhs->func_idx;
    duk_context *ctx=dhs->ctx;
    const char *modname = dhs->module_name;

    duk_get_prop_index(ctx, 0, (duk_uarridx_t) idx); // {module:...}

    /* is it already in the object? */
    if (duk_get_prop_string(ctx, -1, modname) )
    {
        struct stat sb;
        const char *filename;
        
        duk_get_prop_string(ctx, -1, "module_id");
        filename=duk_get_string(ctx,-1);
        duk_pop(ctx);

        if (stat(filename, &sb) != -1)
        {
            time_t lmtime;

            duk_get_prop_string(ctx,-1,"mtime");
            lmtime = (time_t)duk_get_number(ctx,-1);
            duk_pop(ctx); //mtime
            //printf("%d >= %d?\n",(int)lmtime,(int)sb.st_mtime);
            if(lmtime>=sb.st_mtime)
            {
                duk_pop_2(ctx); //the module from duk_get_prop_string(ctx, -1, modname) and duk_get_prop_index(ctx, 0, (duk_uarridx_t) idx);
                return 1; // it is there and up to date
            }
            /* else replace it below */
        }
        /* else file is gone?  Send error */
        else 
        {
            duk_pop_2(ctx);
            return 0;
        }
    }
    duk_pop(ctx); //the module from duk_get_prop_string(ctx, -1, modname) or undefined

    /* load module from file */
    duk_push_sprintf(ctx,"module.resolve(\"%s\",true);", modname);
    if(duk_peval(ctx) != 0)
    {

        evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
        if (duk_is_error(ctx, -1) || duk_is_string(ctx, -1))
        {
            duk_get_prop_string(ctx, -1, "stack");
            evbuffer_add_printf(dhs->req->buffer_out, msg500, duk_safe_to_string(ctx, -1));
            printerr("error in thread '%s'\n", duk_safe_to_string(ctx, -1));
            duk_pop(ctx);
        }
        else
        {
            evbuffer_add_printf(dhs->req->buffer_out, msg500, "unknown error");
        }
        sendresp(dhs->req, 500);
        return -1;
    }
    // on stack is module.exports

    duk_get_prop_string(ctx,-1,"exports");
    /* take mtime & id from "module" and put it in module.exports, 
        signal next run requires a refork by setting dhs->module=MODULE_FILE_RELOAD;
        then remove modules */
    duk_get_prop_string(ctx,-2,"mtime");
    duk_put_prop_string(ctx,-2,"mtime");
    duk_get_prop_string(ctx,-2,"id");
    duk_put_prop_string(ctx,-2,"module_id");
    dhs->module=MODULE_FILE_RELOAD;
    duk_remove(ctx,-2); // now stack is [ func_array, ..., {module:xxx}, func_exports ]

    /* give the function a name for error reporting */
    duk_push_sprintf(ctx,"module:%s",modname);
    duk_put_prop_string(ctx,-2,"fname");

    /* {"module":modname, modname: mod_func} */
    duk_put_prop_string(ctx,-2,modname);

    duk_pop(ctx);  //{module:xxx}
    return 1;
}

static int getmod_path(DHS *dhs)
{
    duk_idx_t idx=dhs->func_idx;
    duk_context *ctx=dhs->ctx;
    evhtp_path_t *path = dhs->req->uri->path;
    const char *modpath;
    struct stat pathstat;

    duk_get_prop_index(ctx, 0, (duk_uarridx_t)idx);
    duk_get_prop_string(dhs->ctx,-1,"modulePath");
    modpath=duk_get_string(dhs->ctx,-1);
    duk_pop_2(dhs->ctx);

    if(stat(modpath, &pathstat)== -1)
    {
        return 0;
    }
    else
    {
        int ret;
        char modname[ strlen(path->full) + strlen(modpath) + 1 ], *s;

        strcpy(modname,modpath);

        if( *(modname+strlen(modname)-1)=='/' )
            *(modname+strlen(modname)-1)='\0';
        //printf("pathlen=%d modpath=%s modname=%s\n",dhs->pathlen,modpath,modname);
        strcat(modname, (path->full) + dhs->pathlen -1);

        // remove the extension:
        s=strrchr(modname,'/');
        if (!s) s=modname;
        s=strchr(s,'.');
        if(s) *s='\0';

        dhs->module_name=strdup(modname);
        ret=getmod(dhs);
        return ret;
    }
}

static void http_callback(evhtp_request_t *req, void *arg)
{
    DHS newdhs, *dhs = arg;
    int dofork = 1, have_threadsafe_val = 0, thrno = 0;
    duk_context *new_ctx;
    if (!gl_singlethreaded)
    {
        evthr_t *thread = get_request_thr(req);
        thrno = *((int *)evthr_get_aux(thread));
    }
    new_ctx = thread_ctx[thrno];

    /* ****************************
      setup duk function callback 
       **************************** */

    newdhs.ctx = new_ctx;
    newdhs.req = req;
    newdhs.func_idx = dhs->func_idx;
    newdhs.timeout = dhs->timeout;
    newdhs.threadno = (uint16_t)thrno;
    newdhs.pathlen = dhs->pathlen;
    newdhs.module=dhs->module;
    newdhs.module_name=NULL;
    newdhs.aux=dhs->aux;
    dhs = &newdhs;
    if(dhs->module==MODULE_FILE)
    {
        int res=0;
        /* format for the module_file object in array at position dhs->func_idx:
            { 
                "module"   : "mymodule",
                "mymodule" : cb_func     //if loaded
            }
        */
        duk_get_prop_index(dhs->ctx, 0, (duk_uarridx_t)dhs->func_idx);
        duk_get_prop_string(dhs->ctx,-1,"module");
        dhs->module_name=strdup(duk_get_string(dhs->ctx,-1));
        duk_pop_2(dhs->ctx);
        res=getmod(dhs);
        if(res==-1)
        {
            free(dhs->module_name);
            return;
        }
        else if (res==0)
        {
            char submsg[64+strlen(dhs->module_name)];
            snprintf(submsg,64+strlen(dhs->module_name),"Error: Could not resolve module id %s: No such file or directory",dhs->module_name);
            evbuffer_add_printf(dhs->req->buffer_out, msg500, submsg);
            sendresp(dhs->req, 500);
            free(dhs->module_name);
            return;
        }
    }
    else if (dhs->module==MODULE_PATH)
    {
        int res=getmod_path(dhs);
        if(res==-1)
        {
            free(dhs->module_name);
            return;
        }
        else if (res==0)
        {
            printf("sending 404\n");
            send404(dhs->req);
            free(dhs->module_name);
            return;
        }
    }
    /* fork until we know it is safe not to fork:
       function will have property threadsafe:true if forking not required 
       after the first time it is called in http_fork_callback              */
//    duk_get_prop_index(dhs->ctx, 0, (duk_uarridx_t)dhs->func_idx);
    getfunction(dhs);
    if (duk_get_prop_string(dhs->ctx, -1, DUK_HIDDEN_SYMBOL("threadsafe")))
    {
        if (duk_is_boolean(dhs->ctx, -1) && duk_get_boolean(dhs->ctx, -1))
            dofork = 0;
        have_threadsafe_val = 1;
    }
    duk_pop_2(dhs->ctx);
    if (dofork)
        http_fork_callback(req, dhs, have_threadsafe_val);
    else
        http_thread_callback(req, dhs, thrno);

    free(dhs->module_name);
}

/* bind to addr and port.  Return mallocd string of addr, which 
   in the case of ipv6 may differ from ip passed to function */

char *bind_sock_port(evhtp_t *htp, const char *ip, uint16_t port, int backlog)
{
    struct sockaddr_storage sin;
    socklen_t len = sizeof(struct sockaddr_storage);
    if (evhtp_bind_socket(htp, ip, port, backlog) != 0)
        return NULL;

    if (getsockname(
            evconnlistener_get_fd(htp->server),
            (struct sockaddr *)&sin, &len) == 0)
    {
        char addr[INET6_ADDRSTRLEN];

        sa_to_string(&sin, addr, INET6_ADDRSTRLEN);
        port = ntohs(((struct sockaddr_in *)&sin)->sin_port);
        if (((struct sockaddr *)&sin)->sa_family == AF_INET6)
        {
            unsigned char a[sizeof(struct in6_addr)];

            /* compress our ipv6 address to compare */
            if (inet_pton(AF_INET6, &ip[5], a) == 1)
            {
                char buf[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, a, buf, INET6_ADDRSTRLEN) == NULL)
                {
                    return NULL;
                }
                /* compare given compressed *ip with what we are actually bound to */
                if (strcmp(buf, addr) != 0)
                    return NULL;
            }
            else
                return NULL;
        }
        else
        {
            /*  ipv4 doesn't get compressed, it fails above if something like 127.0.0.001 */
            if (strcmp(ip, addr) != 0)
                return NULL;
        }
        fprintf(access_fh, "binding to %s port %d\n", addr, port);
        return strdup(addr);
    }

    return NULL;
}

void testcb(evhtp_request_t *req, void *arg)
{
    char rep[] = "hello world";
    /*  
    TODO: figure out keepalive problem
    evhtp_request_set_keepalive(req, 1);
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));
    */
    /*sleep(1);
    printf("%d\n",(int)pthread_self());
    fflush(stdout);*/
    evbuffer_add_printf(req->buffer_out, "%s", rep);
//    sendresp(req, EVHTP_RES_OK);
    evhtp_send_reply(req, EVHTP_RES_OK); // for pure speed in testing - no logs
}

void exitcb(evhtp_request_t *req, void *arg)
{
    //duk_context *ctx=arg;
    char rep[] = "exit";
    int i = 0;

    evbuffer_add_reference(req->buffer_out, rep, strlen(rep), NULL, NULL);
    sendresp(req, EVHTP_RES_OK);
    for (; i < totnthreads; i++)
    {
        //duk_rp_sql_close(thread_ctx[i]);
        duk_destroy_heap(thread_ctx[i]);
    }
    //duk_rp_sql_close(main_ctx);
    duk_destroy_heap(main_ctx);
    free(thread_ctx);
    exit(0);
}

/* TODO: revisit whether DHSA is necessary */
#define DHSA struct dhs_array
DHSA
{
    int n;
    DHS **dhs_pa;
};

DHSA dhsa = {0, NULL};

static DHS *new_dhs(duk_context *ctx, int idx)
{
    DHS *dhs = NULL;
    DUKREMALLOC(ctx, dhs, sizeof(DHS));
    dhs->func_idx = idx;
    dhs->module=MODULE_NONE;
    dhs->module_name=NULL;
    dhs->ctx = ctx;
    dhs->req = NULL;
    dhs->aux = NULL;
    dhs->pathlen=0;
    /* afaik there is no TIME_T_MAX */
    dhs->timeout.tv_sec = RP_TIME_T_FOREVER;
    dhs->timeout.tv_usec = 0;
    dhsa.n++;
    DUKREMALLOC(ctx, dhsa.dhs_pa, sizeof(DHS *) * dhsa.n);
    dhsa.dhs_pa[dhsa.n - 1] = dhs;

    return (dhs);
}

void initThread(evhtp_t *htp, evthr_t *thr, void *arg)
{
    int *thrno = NULL;
    duk_context *ctx;
    struct event_base *base = evthr_get_base(thr);

    if (pthread_mutex_lock(&ctxlock) == EINVAL)
    {
        printerr( "could not obtain lock in http_callback\n");
        exit(1);
    }

    DUKREMALLOC(main_ctx, thrno, sizeof(int));

    /* drop privileges here, after binding port */
    if(unprivu && !gl_threadno)
    {
        if (setgid(unprivg) == -1) 
            RP_THROW(main_ctx, "error setting group, setgid() failed");

        if (setuid(unprivu) == -1) 
            RP_THROW(main_ctx, "error setting user, setuid() failed");

    }

    *thrno = gl_threadno++;
    evthr_set_aux(thr, thrno);
    ctx=thread_ctx[*thrno];

    duk_push_global_stash(ctx);
    duk_push_pointer(ctx, (void*)base);
    duk_put_prop_string(ctx, -2, "elbase");
    duk_pop(ctx);
    pthread_mutex_unlock(&ctxlock);
/*
    printf("threadno = %d, base = %p\n", *thrno, base);
*/
}

static int duk_rp_GPS_icase(duk_context *ctx, duk_idx_t idx, const char * prop)
{
    const char *realprop=NULL, *compprop=NULL;
    
    duk_enum(ctx, idx, 0);
    while (duk_next(ctx, -1 , 0 ))
    {
        compprop=duk_get_string(ctx, -1);
        if ( !strcasecmp( compprop, prop) )
        {
            realprop=compprop;
            duk_pop(ctx);
            break;
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);
    if(realprop)
    {
        duk_get_prop_string(ctx, idx, realprop);
        return 1;
    }
    duk_push_undefined(ctx);
    return 0;
}

/*
#define sslallowconf(opt,flag,defaulton) do {\
    if ( duk_rp_GPS_icase(ctx, ob_idx, opt) ){\
       if(!REQUIRE_BOOL(ctx, -1, "server.start: parameter \"%s\" requires a boolean (true|false)",opt))\
           ssl_config->ssl_opts |= flag;\
    } else if (!defaulton)\
        {printf("'%s'==false\n",opt);ssl_config->ssl_opts |= flag;}\
    duk_pop(ctx);\
} while(0)
*/
static void get_secure(duk_context *ctx, duk_idx_t ob_idx, evhtp_ssl_cfg_t *ssl_config )
{

    ssl_config->ssl_opts = SSL_OP_ALL;
/*
    sslallowconf("allowSSLv2"  ,SSL_OP_NO_SSLv2,0);
    sslallowconf("allowSSLv3"  ,SSL_OP_NO_SSLv3,0);
    sslallowconf("allowTLSv1"  ,SSL_OP_NO_TLSv1,0);
    sslallowconf("allowTLSv1.1",SSL_OP_NO_TLSv1_1,0);
    sslallowconf("allowTLSv1.2",SSL_OP_NO_TLSv1_2,1);
    sslallowconf("allowTLSv1.3",SSL_OP_NO_TLSv1_3,1);
    ssl_config->ssl_opts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
*/



    if (duk_rp_GPS_icase(ctx, ob_idx, "sslkeyfile"))
    {
        ssl_config->privfile = strdup(REQUIRE_STRING(ctx, -1, "server.start: parameter \"sslkeyfile\" requires a string (filename)"));
    }
    duk_pop(ctx);

    if (duk_rp_GPS_icase(ctx, ob_idx, "sslcertfile"))
    {
        ssl_config->pemfile = strdup(REQUIRE_STRING(ctx, -1, "server.start: parameter \"sslcertfile\" requires a string (filename)"));
    }
    duk_pop(ctx);
}

static void connection_error_callback(evhtp_connection_t *conn, evhtp_error_flags type, void *arg)
{
    printf("unhandled connection error of type: 0x%02x", type);
}

evhtp_res pre_accept_callback(evhtp_connection_t *conn, void *arg)
{
    evhtp_connection_set_hook(conn, evhtp_hook_on_conn_error, (void *)&connection_error_callback, NULL);
    //evhtp_connection_set_hook(conn, evhtp_hook_on_error,      (void*) &connection_error_callback,  NULL);
    //evhtp_connection_set_hook(conn, evhtp_hook_on_error,      (void*) &request_error_callback,     NULL);
    return EVHTP_RES_OK;
}

static inline void logging(duk_context *ctx, duk_idx_t ob_idx)
{
    /* logging in daemon */
    if (duk_rp_GPS_icase(ctx, ob_idx, "log") && duk_get_boolean_default(ctx,-1,0) )
    {
        duk_rp_server_logging=1;
    }
    duk_pop(ctx);

    if(duk_rp_server_logging)
    {
        if(duk_rp_GPS_icase(ctx, ob_idx, "accessLog") )
        {
            const char *fn=REQUIRE_STRING(ctx,-1,  "server.start: parameter \"accessLog\" requires a string (filename)");
            access_fh=fopen(fn,"a");
            if(access_fh==NULL)
                RP_THROW(ctx, "server.start: error opening accessLog file '%s': %s", fn, strerror(errno));
        }
        else
        {
            printf("no accessLog specified, logging to stdout\n");
        }

        if(duk_rp_GPS_icase(ctx, ob_idx, "errorLog") )
        {
            const char *fn=REQUIRE_STRING(ctx,-1, "server.start: parameter \"errorLog\" requires a string (filename)");
            error_fh=fopen(fn,"a");
            if(error_fh==NULL)
                RP_THROW(ctx, "server.start: error opening errorLog file '%s': %s", fn, strerror(errno));
        }
        else
        {
            printf("no errorLog specified, logging to stderr\n");
        }
    }
}

static void proc_mimes(duk_context *ctx)
{
    // are we adding or just replacing?
    int must_add=0;

    if (duk_is_object(ctx, -1) && !duk_is_array(ctx, -1) && !duk_is_function(ctx, -1))
    {
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            const char *key, *val;
            RP_MTYPES m;
            RP_MTYPES *mres, *m_p = &m;

            key=duk_get_string(ctx, -2);

            val=REQUIRE_STRING(ctx, -1, "server.start: mime - value (mimetype) must be a string");

            if(*key == '.') key++;
            if(!strlen(key))
                RP_THROW(ctx, "server.start: mime - extension '%s' is not valid", duk_get_string(ctx, -2) );

            m.ext = key;
            /* look for proper mime type listed in mime.h */

            mres = bsearch(m_p, allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
            if (mres)
                mres->mime=val;
            else
                must_add++;

            duk_pop_2(ctx);
        }
            
        duk_pop(ctx); /* pop enum */

    }
    else
    {
        duk_pop(ctx); /* get rid of 'headers' non object */
        RP_THROW(ctx, "server.start: mime - value must be an object (extension to mime-type mappings)");
    }

    /* need to add some more */
    if (must_add)
    {
        int i=0;
        
        allmimes=NULL;
        n_allmimes += must_add;

        REMALLOC(allmimes, n_allmimes * sizeof (RP_MTYPES) );
        for (;i< nRpMtypes; i++)
        {
            allmimes[i].ext = rp_mimetypes[i].ext;
            allmimes[i].mime= rp_mimetypes[i].mime;
        }

        duk_enum(ctx, -1, 0);
        while (duk_next(ctx, -1, 1))
        {
            const char *key, *val;
            RP_MTYPES m;
            RP_MTYPES *mres, *m_p = &m;

            key=duk_get_string(ctx, -2);

            val=duk_get_string(ctx, -1);

            if(*key == '.') key++;

            m.ext = key;
            /* look for proper mime type listed in mime.h */

            mres = bsearch(m_p, rp_mimetypes, nRpMtypes, sizeof(RP_MTYPES), compare_mtypes);
            if (!mres)
            {
                allmimes[i].ext = key;
                allmimes[i].mime= val;
                i++;
            }

            duk_pop_2(ctx);
        }
            
        duk_pop(ctx); /* pop enum */

        qsort(allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
        
        
    }
    /*
    int i=0;
    for (i=0; i<n_allmimes; i++)
        printf("    \"%s\"\t->\t\"%s\"\n",allmimes[i].ext, allmimes[i].mime);
    */
}

#define getipport(sfn) do {\
     char *p, *e;\
     char ibuf[INET6_ADDRSTRLEN]={0};\
     char *s=(char*)(sfn);\
     p=ibuf;\
     while(*s) {\
         if(!isspace(*s)) {*p=*s;p++;}\
         s++;\
     }\
     s=ibuf;\
     if(*s == '['){\
         s++;\
         strcpy(ipany,"ipv6:");\
         e=p=strchr(s,']');\
         if(!p)\
             RP_THROW(ctx,"server.start() - invalid ipv6 address for option bind. Brackets required.");\
         strncpy(&ipany[5], s, e-s );\
         *(ipany + 6 + ( e - s ))='\0';\
         p++;\
         if(*p != ':')\
             RP_THROW(ctx,"server.start() - option bind value must be in format \"ip:port\"");\
         p++;\
         if(!isdigit(*p))\
             RP_THROW(ctx,"server.start() - option bind value must be in format \"ip:port\"");\
         port=(uint16_t)strtol(p, NULL, 10);\
     } else {\
         e=p=strrchr(s,':');\
         if(!p)\
             RP_THROW(ctx,"server.start() - option bind value must be in format \"ip:port\"");\
         strncpy(ipany, s, e-s );\
         *(ipany + ( e - s ))='\0';\
         p++;\
         if(!isdigit(*s))\
             RP_THROW(ctx,"server.start() - option bind value must be in format \"ip:port\"");\
         port=(uint16_t)strtol(p, NULL, 10);\
     }\
} while(0)

#define DIRLISTFUNC \
"    function (req) {\n"\
"        var html=\"<!DOCTYPE html>\\n\"+\n"\
"        '<html><head><meta charset=\"UTF-8\"><title>Index of ' + \n"\
"            req.path.path+ \n"\
"            \"</title><style>td{padding-right:22px;}</style></head><body><h1>\"+\n"\
"            req.path.path+\n"\
"            '</h1><hr><table>';\n"\
"\n"\
"        function hsize(size) {\n"\
"            var ret=rampart.utils.sprintf(\"%d\",size);\n"\
"            if(size >= 1073741824)\n"\
"                ret=rampart.utils.sprintf(\"%.1fG\", size/1073741824);\n"\
"            else if (size >= 1048576)\n"\
"                ret=rampart.utils.sprintf(\"%.1fM\", size/1048576);\n"\
"            else if (size >=1024)\n"\
"                ret=rampart.utils.sprintf(\"%.1fk\", size/1024); \n"\
"            return ret;\n"\
"        }\n"\
"\n"\
"        if(req.path.path != '/')\n"\
"            html+= '<tr><td><a href=\"../\">Parent Directory</a></td><td></td><td>-</td></tr>';\n"\
"        rampart.utils.readdir(req.fsPath).sort().forEach(function(d){\n"\
"            var st=rampart.utils.stat(req.fsPath+'/'+d);\n"\
"            if (st.isDirectory())\n"\
"                d+='/';\n"\
"            html=rampart.utils.sprintf('%s<tr><td><a href=\"%s\">%s</a></td><td>%s</td><td>%s</td></tr>',\n"\
"                html, d, d, st.mtime.toLocaleString() ,hsize(st.size));\n"\
"        });\n"\
"        \n"\
"        html+=\"</table></body></html>\";\n"\
"        return {html:html};\n"\
"    }\n"



#ifdef COMBINE_EVLOOPS
extern struct event_base *elbase;
#endif
duk_ret_t duk_server_start(duk_context *ctx)
{
    int i = 0;
    DHS *dhs = new_dhs(ctx, -1);
    duk_idx_t ob_idx = -1;
    char *ipv6 = "ipv6:::1";
    char *ipv4 = "127.0.0.1";
    char ipany[INET6_ADDRSTRLEN + 5]={0};
    char *ip_addr = NULL;
    uint16_t port = 8088;
    int nthr=0, mapsort=1;
    evhtp_t *htp = NULL;
#ifndef COMBINE_EVLOOPS
    struct event_base *evbase;
#endif
    evhtp_ssl_cfg_t *ssl_config = calloc(1, sizeof(evhtp_ssl_cfg_t));
    int confThreads = -1, mthread = 1, daemon=0;
    struct stat f_stat;
    struct timeval ctimeout;
    duk_uarridx_t fpos =0;
    pid_t dpid=0;

    ctimeout.tv_sec = RP_TIME_T_FOREVER;
    ctimeout.tv_usec = 0;
    main_ctx = ctx;

    /*  an array at stack index 0 to hold all the callback function 
        This gets it off of an otherwise growing and shrink stack and makes
        managment of their locations less error prone in the thread stacks.
    */
    duk_push_array(ctx);
    duk_insert(ctx,0);    
    i = 1;
    if (
        (duk_is_object(ctx, i) && !duk_is_array(ctx, i) && !duk_is_function(ctx, i)) ||
        (duk_is_object(ctx, ++i) && !duk_is_array(ctx, i) && !duk_is_function(ctx, i)))
        ob_idx = i;

    if(i==2) i=1; /* get the other option. There are only 2 */
    else i=2;

    if (duk_is_function(ctx, i))
        dhs->func_idx = i;

    /* check if we are forking before doing any setup*/
    if (ob_idx != -1)
    {
        /* daemon */
        if (duk_rp_GPS_icase(ctx, ob_idx, "daemon"))
        {
            daemon = REQUIRE_BOOL(ctx, -1, "server.start: parameter \"daemon\" requires a boolean (true|false)");
        }
        duk_pop(ctx);

        if (duk_rp_GPS_icase(ctx, ob_idx, "mimeMap"))
        {
            proc_mimes(ctx);
        }
        duk_pop(ctx);
    }

    if (daemon)
    {
        int child2par[2]={0};

        if (pipe(child2par) == -1)
        {
            fprintf(error_fh, "child2par pipe failed\n");
            exit (1);
        }
        
        signal(SIGHUP,SIG_IGN);
        dpid=fork();
        if(dpid==-1)
        {
            fprintf(stderr, "server.start: fork failed\n");
            exit(1);
        }
        else if(!dpid)
        {   /* child */
            char *pname=NULL;
            char portbuf[8];
            int i=1;
            pid_t dpid2;

            close(child2par[0]);
            logging(ctx,ob_idx);
            if (setsid()==-1) {
                fprintf(error_fh, "server.start: failed to become a session leader while daemonising: %s",strerror(errno));
                exit(1);
            }
            /* need to double fork, and need to write back the pid of grandchild to parent. */
            dpid2=fork();
            
            if(dpid==-1)
            {
                fprintf(stderr, "server.start: fork failed\n");
                exit(1);
            }
            else if(dpid2)
            {	/* still child */
                if(-1 == write(child2par[1], &dpid2, sizeof(pid_t)) )
                {
                    fprintf(error_fh, "server.start: failed to send pid to parent\n");
                }
                close(child2par[1]);
                exit(0);
            }
            /* else grandchild */
            event_reinit(elbase);
            /* get first port used */
            if (duk_rp_GPS_icase(ctx, ob_idx, "bind"))
            {
                if ( duk_is_string(ctx, -1) )
                {
                    getipport(duk_get_string(ctx,-1));
                }
                else if ( duk_is_array(ctx, -1) )
                {
                    if(duk_get_prop_index(ctx, -1, 0))
                    {
                        getipport(duk_get_string(ctx, -1));
                    }
                    duk_pop(ctx);
                }
            }
            duk_pop(ctx);

            pname=strdup("rampart-server:");
            snprintf(portbuf,8,"%d",port);
            pname=strcatdup(pname,portbuf);
            for (i=1;i<rampart_argc;i++)
            {
                pname=strcatdup(pname," ");
                pname=strcatdup(pname,rampart_argv[i]);
            }
            strcpy(rampart_argv[0], pname);
            free(pname);

        }
        else
        {
            pid_t dpid2=0;
            int pstatus;

            close(child2par[1]);
            waitpid(dpid,&pstatus,0);
            if(-1 == read(child2par[0], &dpid2, sizeof(pid_t)) )
            {
                fprintf(error_fh, "server.start: failed to get pid from child\n");
            }
            close(child2par[0]);
            duk_push_int(ctx,(int)dpid2);
            return 1;
        }
    }
#ifndef COMBINE_EVLOOPS
    evbase = event_base_new();
#endif
    /* options from server({options},...) */
    if (ob_idx != -1)
    {
        //const char *s;

        /* logging */
        if(!daemon) 
        {
            logging(ctx, ob_idx);
            //stderr=error_fh;
            //stdout=access_fh;            

            /* port */
            if (duk_rp_GPS_icase(ctx, ob_idx, "port"))
            {
                int signed_port = duk_rp_get_int_default(ctx, -1, -1);
                if (signed_port < 1)
                    RP_THROW(ctx, "server.start: parameter \"port\" invalid");
                port=(uint16_t) signed_port;
            }
            duk_pop(ctx);

        }

        /* unprivileged user if we are root */
        if (geteuid() == 0)
        {
            if(duk_rp_GPS_icase(ctx, ob_idx, "user"))
            {
                const char *user=REQUIRE_STRING(ctx, -1, "server.start: parameter \"user\" requires a string (username)");
                struct passwd  *pwd;
                
                if(! (pwd = getpwnam(user)) )
                    RP_THROW(ctx, "server.start: error getting user '%s' in start()\n",user);

                if( !strcmp("root",user) )
                    fprintf(stderr,"\n******* WARNING: YOU ARE RUNNING SERVER AS ROOT. NOT A GOOD IDEA. YOU'VE BEEN WARNED!!!! ********\n\n");
                else
                    fprintf(access_fh,"setting unprivileged user '%s'\n",user);
                unprivu=pwd->pw_uid;
                unprivg=pwd->pw_gid;
            }
            else
                RP_THROW(ctx, "server.start: starting as root requires you name a {user:'unpriv_user_name'} in start()");

            duk_pop(ctx);
        }

        /* threads */
        if (duk_rp_GPS_icase(ctx, ob_idx, "threads"))
        {
            confThreads = duk_rp_get_int_default(ctx, -1, -1);
            if(confThreads == -1)
                RP_THROW(ctx,"server.start: parameter \"threads\" invalid");
        }
        duk_pop(ctx);

        /* multithreaded */
        if (duk_rp_GPS_icase(ctx, ob_idx, "usethreads"))
        {
            mthread = REQUIRE_BOOL(ctx, -1, "server.start: parameter \"threads\" requires a boolean (true|false)");
        }
        gl_singlethreaded = !mthread;
        duk_pop(ctx);

         /* connect timeout */
        if (duk_rp_GPS_icase(ctx, ob_idx, "connectTimeout"))
        {
            double to = REQUIRE_NUMBER(ctx, -1,  "server.start: parameter \"connectTimeout\" requires a number (float)");
            ctimeout.tv_sec = (time_t)to;
            ctimeout.tv_usec = (suseconds_t)1000000.0 * (to - (double)ctimeout.tv_sec);
            fprintf(access_fh, "set connection timeout to %d sec and %d microseconds\n", (int)ctimeout.tv_sec, (int)ctimeout.tv_usec);
        }
        duk_pop(ctx);

        /* script timeout */
        if (duk_rp_GPS_icase(ctx, ob_idx, "scriptTimeout"))
        {
            double to = REQUIRE_NUMBER(ctx, -1, "server.start: parameter \"scriptTimeout\" requires a number (float)");
            dhs->timeout.tv_sec = (time_t)to;
            dhs->timeout.tv_usec = (suseconds_t)1000000.0 * (to - (double)dhs->timeout.tv_sec);
            fprintf(access_fh, "set script timeout to %d sec and %d microseconds\n", (int)dhs->timeout.tv_sec, (int)dhs->timeout.tv_usec);
        }
        duk_pop(ctx);

        /* ssl opts*/
        if (duk_rp_GPS_icase(ctx, ob_idx, "secure"))
        {
            rp_using_ssl = REQUIRE_BOOL(ctx, -1, "server.start: parameter \"secure\" requires a boolean (true|false)");
        }
        duk_pop(ctx);

        if (rp_using_ssl)
        {
            get_secure(ctx, ob_idx, ssl_config);
        }
    }

    /*  got the essential config options, now do some setup before mapping urls */
    if (mthread)
    {
        /* use specified number of threads */
        if (confThreads > 0)
        {
            nthr = confThreads;
        }
        else
        /* not specified, so set to number of processor cores */
        {
            nthr = sysconf(_SC_NPROCESSORS_ONLN);
        }

        totnthreads = nthr;

        fprintf(access_fh, "HTTP server - initializing with %d threads\n",
               totnthreads);
    }
    else
    {
        if (confThreads > 1)
        {
            fprintf(access_fh, "threads>1 -- usethreads==false, so using only one thread\n");
        }
        totnthreads = 1;
        fprintf(access_fh, "HTTP server - initializing single threaded\n");
    }

    /* set up info structs for forking in threads for thread-unsafe duk_c functions */
    REMALLOC(forkinfo, (totnthreads * sizeof(FORKINFO *)));
    for (i = 0; i < totnthreads; i++)
    {
        forkinfo[i] = NULL;
        REMALLOC(forkinfo[i], sizeof(FORKINFO));
        forkinfo[i]->par2child = -1;
        forkinfo[i]->child2par = -1;
        forkinfo[i]->childpid = 0;
    }

    /* initialize a context for each thread */
    REMALLOC(thread_ctx, (totnthreads * sizeof(duk_context *)));
    for (i = 0; i <= totnthreads; i++)
    {
        duk_context *tctx;
        if (i == totnthreads)
        {
            tctx = ctx;
        }
        else
        {
            thread_ctx[i] = duk_create_heap_default();
            tctx = thread_ctx[i];
            /* do all the normal startup done in duk_cmdline but for each thread */
            duk_init_context(tctx);
            duk_push_array(tctx);
            copy_all(ctx, tctx);
        }

        /* add some info in each thread as well as the parent thread*/
        if (!duk_get_global_string(tctx, "rampart"))
        {
            duk_pop(tctx);
            duk_push_object(tctx);
        }
        duk_push_int(tctx, i);
        duk_put_prop_string(tctx, -2, "thread_id");
        duk_push_int(tctx, totnthreads);
        duk_put_prop_string(tctx, -2, "total_threads");

        duk_put_global_string(tctx, "rampart");
    }
#ifdef COMBINE_EVLOOPS
    htp = evhtp_new(elbase, NULL);
#else
    htp = evhtp_new(evbase, NULL);
#endif
    /* testing for pure c benchmarking*
    evhtp_set_cb(htp, "/test", testcb, NULL);

    * testing, quick semi clean exit *
    evhtp_set_cb(htp, "/exit", exitcb, ctx);
    */

    /* file system and function mapping */
    if (ob_idx != -1)
    {
        /* custom 404 page/function */
        if (duk_rp_GPS_icase(ctx, ob_idx, "notFoundFunc"))
        {
            if ( duk_is_object(ctx,-1) )
            {   /* map to function or module */
                /* copy the function to array at stack pos 0 */
                const char *fname;
                char mod=MODULE_NONE;

                if (duk_is_function(ctx, -1))
                {
                    duk_get_prop_string(ctx, -1, "name");
                    fname = duk_get_string(ctx, -1);
                    duk_pop(ctx);
                    fprintf(access_fh, "mapping not found  to function   %-20s ->    function %s()\n", "404", fname);
                }
                else if (duk_get_prop_string(ctx,-1, "module") )
                {
                    fname = duk_get_string(ctx, -1);
                    duk_pop(ctx);
                    fprintf(access_fh, "mapping not found  to function   %-20s ->    module:%s\n", "404", fname);
                    mod=MODULE_FILE;
                }
                else
                    RP_THROW(ctx, "server.start: Option for notFoundFunc must be a function or an object to load a module (e.g. {module:'mymodule'}");

                /* copy function into array at pos 0 in stack and into index fpos in array */
                duk_put_prop_index(ctx, 0, fpos);

                dhs404 = new_dhs(ctx, fpos);
                dhs404->module=mod;

                dhs404->timeout.tv_sec = dhs->timeout.tv_sec;
                dhs404->timeout.tv_usec = dhs->timeout.tv_usec;
                /* copy function to all the heaps/ctxs */
                if(mod)
                {
                    for (i=0;i<totnthreads;i++)
                        copy_mod_func(ctx, thread_ctx[i], fpos);
                }
                else
                    copy_cb_func(dhs404, totnthreads);

                fpos++;
            }
            else
                RP_THROW(ctx, "server.start: Option for notFoundFunc must be a function or an object to load a module (e.g. {module:'mymodule'}");

        }
        duk_pop(ctx);

        /* directory listing page/function */
        if (duk_rp_GPS_icase(ctx, ob_idx, "directoryFunc"))
        {
            if ( duk_is_object(ctx,-1) || duk_is_boolean(ctx, -1) )
            {   /* map to function or module */
                /* copy the function to array at stack pos 0 */
                const char *fname;
                char mod=MODULE_NONE;

                if(duk_is_boolean(ctx, -1))
                {
                    if(duk_get_boolean(ctx, -1))
                    {
                        duk_pop(ctx);
                        duk_compile_string(ctx, DUK_COMPILE_FUNCTION, DIRLISTFUNC);
                        fname="defaultDirlist";
                        fprintf(access_fh, "mapping dir  list  to function   %-20s ->    function %s()\n", "Directory List", fname);
                    }
                    else goto dfunc_end;
                }
                else if (duk_is_function(ctx, -1))
                {
                    duk_get_prop_string(ctx, -1, "name");
                    fname = duk_get_string(ctx, -1);
                    duk_pop(ctx);
                    fprintf(access_fh, "mapping dir  list  to function   %-20s ->    function %s()\n", "Directory List", fname);
                }
                else if (duk_get_prop_string(ctx,-1, "module") )
                {
                    fname = duk_get_string(ctx, -1);
                    duk_pop(ctx);
                    fprintf(access_fh, "mapping dir  list  to function   %-20s ->    module:%s\n", "Directory List", fname);
                    mod=MODULE_FILE;
                }
                else
                    RP_THROW(ctx, "server.start: Option for directoryFunc must be a boolean, a function or an object to load a module (e.g. {module:'mymodule'}");

                /* copy function into array at pos 0 in stack and into index fpos in array */
                duk_put_prop_index(ctx, 0, fpos);

                dhs_dirlist = new_dhs(ctx, fpos);
                dhs_dirlist->module=mod;

                dhs_dirlist->timeout.tv_sec = dhs->timeout.tv_sec;
                dhs_dirlist->timeout.tv_usec = dhs->timeout.tv_usec;
                /* copy function to all the heaps/ctxs */
                if(mod)
                {
                    for (i=0;i<totnthreads;i++)
                        copy_mod_func(ctx, thread_ctx[i], fpos);
                }
                else
                    copy_cb_func(dhs_dirlist, totnthreads);

                fpos++;
            }
            else
                RP_THROW(ctx, "server.start: Option for directoryFunc must be a boolean, a function or an object to load a module (e.g. {module:'mymodule'}");
        }
        dfunc_end:
        duk_pop(ctx);

        if (duk_rp_GPS_icase(ctx, ob_idx, "mapSort"))
        {
            mapsort=REQUIRE_BOOL(ctx, -1, "server.start: parameter mapSort requires a boolean");
        }
        duk_pop(ctx);

        if (duk_rp_GPS_icase(ctx, ob_idx, "map"))
        {
            if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1) && !duk_is_array(ctx, -1))
            {
                int mlen=0,j=0,pathlen=0;
                static char *pathtypes[]= { "exact", "glob ", "regex", "dir  " };

                if(mapsort)
                {
                    /* longer == more specific/match first */
                    //duk_push_string(ctx,"function(map) { return Object.keys(map).sort(function(a, b){  return b.length - a.length; }); }"); 
                    /* priority to exact, regex, path, then by length */
                    duk_push_string(ctx, "function(map){return Object.keys(map).sort(function(a, b){\n\
                                     var blen=b.length, alen=a.length;\n\
                                     if(a.charAt(0) == '~') alen+=1000; else if(a.charAt(0) == '/') alen+=1000000;\n\
                                     if(b.charAt(0) == '~') blen+=1000; else if(b.charAt(0) == '/') blen+=1000000;\n\
                                     return blen - alen;});}");
                }
                else
                {
                    duk_push_string(ctx, "function(map){return Object.keys(map)}");
                }

                /* FIXME:  Allow \* in filesystem mappings and remove the 
                   \ from both
                */
                duk_push_string(ctx,"mapsort");
                duk_compile(ctx,DUK_COMPILE_FUNCTION);
                duk_dup(ctx,-2);
                duk_call(ctx,1);

                mlen=(int)duk_get_length(ctx, -1);
                for (j=0; j<mlen; j++)
                {
                    char *path, *fspath, *fs = (char *)NULL, *s = (char *)NULL;
                    
                    duk_get_prop_index(ctx,-1,j);
                    path = (char*)duk_get_string(ctx,-1);
                    duk_get_prop_string(ctx,-3,path);

                    if (duk_is_object(ctx, -1))
                    { /* map to function or module */
                        /* copy the function to array at stack pos 0 */
                        DHS *cb_dhs;
                        const char *fname;
                        char mod=MODULE_NONE;
                        int cbtype=0;
                        evhtp_callback_t  *req_callback;

                        path = (char *)duk_get_string(ctx, -2);
                        DUKREMALLOC(ctx, s, strlen(path) + 2);

                        if (*path == '~')
                        {
                            cbtype=2;
                            path++;
                            sprintf(s, "%s", path);
                        }
                        else
                        {
                            int k, len=(int)strlen(path);
                            for (k=0; k<len; k++)
                            {
                                if (path[k]=='*' && (k!=0 && path[k-1]!='\\'))
                                {
                                    cbtype=1;
                                    break;
                                }
                            }

                            if (*path != '/' && *path != '*')
                                sprintf(s, "/%s", path);
                            else
                                sprintf(s, "%s", path);

                            if  (!cbtype && *(s + strlen(s) - 1)=='/')
                                cbtype=3;
                        }

                        if (duk_is_function(ctx, -1))
                        {
                            duk_get_prop_string(ctx, -1, "name");
                            fname = duk_get_string(ctx, -1);
                            duk_pop(ctx);
                            fprintf(access_fh, "mapping %s path to function   %-20s ->    function %s()\n", pathtypes[cbtype], s, fname);
                            goto copyfunction;
                        }
                        else 
                        {
                            if (duk_get_prop_string(ctx,-1, "module") )
                            {
                                fname = duk_get_string(ctx, -1);
                                fprintf(access_fh, "mapping %s path to function   %-20s ->    module:%s\n", pathtypes[cbtype],s, fname);
                                mod=MODULE_FILE;
                                duk_pop(ctx);
                                goto copyfunction;
                            }
                            duk_pop(ctx);
                            if (duk_get_prop_string(ctx,-1, "modulePath") )
                            {
                                if( *( s + strlen(s) -1) == '*' || *s=='*')
                                    RP_THROW(ctx, "server.start: parameter \"map:modulePath\" -- glob not allowed in module path %s",s);

                                fname = duk_get_string(ctx, -1);
                                fprintf(access_fh, "mapping %s path to mod folder %-20s ->    module path:%s\n", pathtypes[cbtype], s, fname);
                                mod=MODULE_PATH;
                                duk_pop(ctx);
                                pathlen=strlen(s);
                                goto copyfunction;
                            }
                            duk_pop(ctx);                            
                        }
                        
                        duk_push_error_object(ctx, DUK_ERR_ERROR, "server.start: parameter \"map\" -- Option for path %s must be a function or an object to load a module (e.g. {module:'mymodule'}",s);
                        free(s);
                        (void)duk_throw(ctx);
                        
                        copyfunction:
                        /* copy function into array at pos 0 in stack and into index fpos in array */
                        duk_put_prop_index(ctx,0,fpos);
                        cb_dhs = new_dhs(ctx, fpos);
                        cb_dhs->module=mod;
                        cb_dhs->pathlen=pathlen;
                        cb_dhs->timeout.tv_sec = dhs->timeout.tv_sec;
                        cb_dhs->timeout.tv_usec = dhs->timeout.tv_usec;
                        /* copy function to all the heaps/ctxs */
                        if(mod)
                        {
                            for (i=0;i<totnthreads;i++)
                                copy_mod_func(ctx, thread_ctx[i], fpos);
                        }
                        else
                            copy_cb_func(cb_dhs, totnthreads);
                        
                        fpos++;
                        /* register callback with evhtp using the callback dhs struct */
                        if(cbtype==2)
                        {
                            req_callback =evhtp_set_regex_cb(htp, s, http_callback, cb_dhs);
                            if(!req_callback) 
                                RP_THROW(ctx, "server.start - parameter \"map\": Bad regular expression");
                        } 
                        else if (cbtype==1)
                            evhtp_set_glob_cb(htp, s, http_callback, cb_dhs);
                        else if (cbtype==3)
                            evhtp_set_cb(htp, s, http_callback, cb_dhs);
                        else
                            evhtp_set_exact_cb(htp, s, http_callback, cb_dhs);

                        free(s);
                        duk_pop(ctx);
                    }
                    else
                    /*TODO: duk_is_object and get some config options
                            for each mapped http path                   */
                    { /* map to filesystem */
                        DHMAP *map = NULL;
                        mode_t mode;
                        struct stat sb;
                        int j, len=(int)strlen(path);

                        for (j=0; j<len; j++)
                        {
                            if (path[j]=='*' && (j!=0 && path[j-1]!='\\'))
                            {
                                RP_THROW(ctx, "server.start: parameter \"map\" -- system path \"%s\" name cannot contain glob \"*\"", path);
                            }
                        }

                        DUKREMALLOC(ctx, map, sizeof(DHMAP));

                        DUKREMALLOC(ctx, s, strlen(path) + 4);

                        if (*path != '/' || *(path + strlen(path) - 1) != '/')
                        {
                            if (*path != '/' && *(path + strlen(path) - 1) != '/')
                                sprintf(s, "/%s/", path);
                            else if (*path != '/')
                                sprintf(s, "/%s", path);
                            else
                                sprintf(s, "%s/", path);
                        }
                        else
                            sprintf(s, "%s", path);

                        map->key = s;
                        map->dhs = dhs;
                        
                        fspath = (char *)duk_to_string(ctx, -1);

                        if (stat(fspath, &sb) == -1)
                        {
                            RP_THROW(ctx, "server.start: parameter \"map\" -- Couldn't find fileserver path '%s'",fspath);
                        }
                        mode = sb.st_mode & S_IFMT;

                        if (mode != S_IFDIR)
                            RP_THROW(ctx, "server.start: parameter \"map\" -- Fileserver path '%s' requires a directory",fspath);
                        
                        DUKREMALLOC(ctx, fs, strlen(fspath) + 2)

                        if (*(fspath + strlen(fspath) - 1) != '/')
                        {
                            sprintf(fs, "%s/", fspath);
                        }
                        else
                            sprintf(fs, "%s", fspath);

                        fprintf(access_fh, "mapping filesystem folder        %-20s ->    %s\n", s, fs);
                        map->val = fs;
                        evhtp_set_cb(htp, s, fileserver, map);

                        duk_pop_2(ctx);
                    }
                }
            }
            else
                RP_THROW(ctx, "server.start: value of parameter \"map\" requires an object");
        }
        duk_pop(ctx);
    }
    duk_pop(ctx);

    if (rp_using_ssl)
    {
        int filecount = 0;

        if (ssl_config->pemfile)
        {
            filecount++;
            if (stat(ssl_config->pemfile, &f_stat) != 0)
                RP_THROW(ctx, "server.start: Cannot load SSL cert '%s' (%s)", ssl_config->pemfile, strerror(errno));
            else
            {
                FILE* file = fopen(ssl_config->pemfile,"r");
                X509* x509 = PEM_read_X509(file, NULL, NULL, NULL);
                unsigned long err = ERR_get_error();

                if(x509)
                    X509_free(x509);
                if(err)
                {
                    char tbuf[256];
                    ERR_error_string(err,tbuf);
                    RP_THROW(ctx,"Invalid sslcertfile: %s",tbuf);
                }
            }
        }

        if (ssl_config->privfile)
        {


            filecount++;
            if (stat(ssl_config->privfile, &f_stat) != 0)
                RP_THROW(ctx, "server.start: Cannot load SSL key '%s' (%s)", ssl_config->privfile, strerror(errno));
            else
            {
                FILE* file = fopen(ssl_config->privfile,"r");
                EVP_PKEY* pkey = PEM_read_PrivateKey(file, NULL, NULL, NULL);
                unsigned long err = ERR_get_error();

                if(pkey)
                    EVP_PKEY_free(pkey);
                if(err)
                {
                    char tbuf[256];
                    ERR_error_string(err,tbuf);
                    RP_THROW(ctx, "Invalid sslkeyfile: %s", tbuf);
                }
            }
        }

        if (ssl_config->cafile)
        {
            if (stat(ssl_config->cafile, &f_stat) != 0)
                RP_THROW(ctx, "server.start: Cannot load SSL CA File '%s' (%s)", ssl_config->cafile, strerror(errno));
        }
        if (filecount < 2)
            RP_THROW(ctx, "server.start: Minimally ssl must be configured with, e.g. -\n"
                                  "{\n\t\"secure\":true,\n\t\"sslkeyfile\": \"/path/to/privkey.pem\","
                                  "\n\t\"sslcertfile\":\"/path/to/fullchain.pem\"\n}");
        fprintf(access_fh, "Initializing ssl/tls\n");
        {
            unsigned long err=0;
            if (evhtp_ssl_init(htp, ssl_config) == -1)
                RP_THROW(ctx, "server.start: error setting up ssl/tls server");
            err = ERR_get_error();
            if(err)
            {
                char tbuf[256];
                ERR_error_string(err,tbuf);
                RP_THROW(ctx, "Ssl failed. Error message: %s\n",tbuf);
            }
        }
        if (duk_rp_GPS_icase(ctx, ob_idx, "sslMinVersion"))
        {
            const char *sslver=REQUIRE_STRING(ctx, -1, "server.start: parameter \"sslMaxVer\" requires a string (ssl3|tls1|tls1.1|tls1.2)");
            if (!strcmp("tls1.2",sslver))
                SSL_CTX_set_min_proto_version(htp->ssl_ctx, TLS1_2_VERSION);
            else if (!strcmp("tls1.1",sslver))
                SSL_CTX_set_min_proto_version(htp->ssl_ctx, TLS1_1_VERSION);
            else if (!strcmp("tls1.0",sslver))
                SSL_CTX_set_min_proto_version(htp->ssl_ctx, TLS1_VERSION);
            else if (!strcmp("tls1",sslver))
                SSL_CTX_set_min_proto_version(htp->ssl_ctx, TLS1_VERSION);
            else if (!strcmp("ssl3",sslver))
                SSL_CTX_set_min_proto_version(htp->ssl_ctx, SSL3_VERSION);
            else
                RP_THROW(ctx, "server.start: parameter \"sslMaxVer\" must be ssl3, tls1, tls1.1 or tls1.2");
        }
        else
            SSL_CTX_set_min_proto_version(htp->ssl_ctx, TLS1_2_VERSION);
        duk_pop(ctx);

        scheme="https://";
    } 
    else
        free(ssl_config);

    if (dhs->func_idx != -1)
    {
        duk_pull(ctx, dhs->func_idx);
        duk_put_prop_index(ctx, 0, fpos);
        dhs->func_idx = fpos;
        copy_cb_func(dhs, totnthreads);
        evhtp_set_gencb(htp, http_callback, dhs);
    }

    if(ob_idx != -1)
    {
        if (duk_rp_GPS_icase(ctx, ob_idx, "bind"))
        {
            if ( duk_is_string(ctx, -1) )
            {
                getipport(duk_get_string(ctx,-1));
                if (!(ip_addr = bind_sock_port(htp, ipany, port, 2048)))
                {
                    if (!strncmp("ipv6:",ipany,5))
                    {
                        RP_THROW(ctx, "server.start: could not bind to [%s] port %d", ipany+5, port);
                    }
                    else
                    {
                        RP_THROW(ctx, "server.start: could not bind to %s port %d", ipany, port);
                    }
                }
                free(ip_addr);
                ip_addr=NULL;
            }
            else if ( duk_is_array(ctx, -1) )
            {
                int n=duk_get_length(ctx, -1);
                for (i=0;i<n;i++)
                {
                    if(duk_get_prop_index(ctx,-1,(duk_uarridx_t)i))
                    {
                        getipport(duk_get_string(ctx,-1));
                        if (!(ip_addr = bind_sock_port(htp, ipany, port, 2048)))
                        {
                            if (!strncmp("ipv6:",ipany,5))
                            {
                                RP_THROW(ctx, "server.start: could not bind to [%s] port %d", ipany+5, port);
                            }
                            else
                            {
                                RP_THROW(ctx, "server.start: could not bind to %s port %d", ipany, port);
                            }
                        }
                        free(ip_addr);
                        ip_addr=NULL;
                    }
                    duk_pop(ctx);
                }
            }
            else
                RP_THROW(ctx,"server.start() - option bind requires a string or array of strings");
        }
        else
        {
            if (!(ip_addr = bind_sock_port(htp, ipv4, port, 2048)))
                RP_THROW(ctx, "server.start: could not bind to %s port %d", ipv4, port);
            if (!(ip_addr = bind_sock_port(htp, ipv6, port, 2048)))
                RP_THROW(ctx, "server.start: could not bind to %s, %d", ipv6, port);
        }
        duk_pop(ctx);
    }
    else
    {
        /* default ip and port */
        if (!(ip_addr = bind_sock_port(htp, ipv4, port, 2048)))
            RP_THROW(ctx, "server.start: could not bind to %s port %d", ipv4, port);
        if (!(ip_addr = bind_sock_port(htp, ipv6, port, 2048)))
            RP_THROW(ctx, "server.start: could not bind to %s, %d", ipv6, port);
    }

    //done with options, get rid of ob_idx
    duk_remove(ctx, ob_idx);

    for (i = 0; i <= totnthreads; i++)
    {
        duk_context *tctx;
        if (i == totnthreads)
            tctx = ctx;
        else
            tctx = thread_ctx[i];

        /* add more info */
        if (!duk_get_global_string(tctx, "rampart"))
        {
            duk_pop(tctx);
            duk_push_object(tctx);
        }
/*
        duk_push_object(tctx);

        if (usev4)
        {
            duk_push_string(tctx, ipv4_addr);
            duk_put_prop_string(tctx, -2, "ipv4_addr");
            duk_push_int(tctx, port);
            duk_put_prop_string(tctx, -2, "ipv4_port");
        }
        if (usev6)
        {
            duk_push_string(tctx, ipv6_addr);
            duk_put_prop_string(tctx, -2, "ipv6_addr");
            duk_push_int(tctx, ipv6port);
            duk_put_prop_string(tctx, -2, "ipv6_port");
        }
        duk_put_prop_string(tctx, -2, "server");
*/
        duk_put_global_string(tctx, "rampart");
    }

    evhtp_set_timeouts(htp, &ctimeout, &ctimeout);

    evhtp_set_pre_accept_cb(htp, pre_accept_callback, NULL);

    if (pthread_mutex_init(&ctxlock, NULL) == EINVAL)
    {
        fprintf(stderr, "server.start: could not initialize context lock\n");
        exit(1);
    }

    if (mthread)
    {
        evhtp_use_threads_wexit(htp, initThread, NULL, nthr, NULL);
    }
    else
    {
        fprintf(access_fh, "in single threaded mode\n");
    }

    fflush(access_fh);
    fflush(error_fh);

    if (daemon)
    {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        if (open("/dev/null",O_RDONLY) == -1) {
            fprintf(error_fh,"server.start: failed to reopen stdin while daemonising (errno=%d)",errno);
            exit(1);
        }
        if (open("/dev/null",O_WRONLY) == -1) {
            fprintf(error_fh,"server.start: failed to reopen stdout while daemonising (errno=%d)",errno);
            exit(1);
        }
        close(STDERR_FILENO);
        if (open("/dev/null",O_RDWR) == -1) {
            /* FIXME: what to do when error_fh=stderr? */
            fprintf(error_fh,"server.start: failed to reopen stderr while daemonising (errno=%d)",errno);
            exit(1);
        }
        stderr=error_fh;
        stdout=access_fh;            
#ifdef COMBINE_EVLOOPS
        duk_push_global_stash(ctx);
        duk_pull(ctx,0);
        duk_put_prop_string(ctx, -2, "funcstash");
        // if forking, run loop here in child and don't continue with rest of script
        // script will continue in parent process
        event_base_loop(elbase, 0);
#endif
    }
#ifdef COMBINE_EVLOOPS
//    printstack(ctx);
    duk_push_global_stash(ctx);
    duk_pull(ctx,0);
    duk_put_prop_string(ctx, -2, "funcstash");
#else
    //never return
    event_base_loop(evbase, 0);
#endif
    duk_push_int(ctx, (int) getpid() );    
    return 1;
}

static const duk_function_list_entry utils_funcs[] = {
    {"start", duk_server_start, 2 /*nargs*/},
    {NULL, NULL, 0}};

static const duk_number_list_entry utils_consts[] = {
    {NULL, 0.0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, utils_funcs);
    duk_put_number_list(ctx, -1, utils_consts);

    /*
    duk_compile_string(ctx, DUK_COMPILE_FUNCTION, DIRLISTFUNC);
    duk_push_string(ctx, "defaultDirList");
    duk_put_prop_string(ctx, -2, "fname");
    duk_put_prop_string(ctx, -2, "defaultDirList");
    */

    return 1;
}
