/* Copyright (C) 2022  Aaron Flin - All Rights Reserved
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
#include <sys/mman.h>
#include <sys/param.h>
#include <grp.h>
#ifdef __APPLE__
#include <uuid/uuid.h>
#include <sys/sysctl.h>
#endif
#include <pwd.h>

#include "evhtp/evhtp.h"
#include "../ws/evhtp_ws.h"
#include "rampart.h"
#include "../core/module.h"
#include "mime.h"
#include "../register.h"
#include "../globals/printf.h"
#include "libdeflate.h"


//#define RP_TO_DEBUG
//#define RP_TIMEO_DEBUG

RP_MTYPES *allmimes=rp_mimetypes;
int n_allmimes =nRpMtypes;
struct timeval ctimeout;

extern int RP_TX_isforked;
extern char *RP_script_path;
uid_t unprivu=0;
gid_t unprivg=0;
//#define RP_TIMEO_DEBUG
#ifdef RP_TIMEO_DEBUG
#define debugf(...) printf(__VA_ARGS__);
#else
#define debugf(...) /* Do nothing */
#endif


volatile int gl_threadno = 0;
int gl_singlethreaded = 0;
int rampart_server_started=0;
int developer_mode=0;
__thread int local_thread_number=0;
extern duk_context **thread_ctx;

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



/* a duk http request struct
    keeps info about request, callback,
    timeout and thread number for thread specific info
*/

#define MODULE_NONE 0
#define MODULE_FILE 1
#define MODULE_FILE_RELOAD 2 //unused
#define MODULE_PATH 3

#define AUX_BUF_CHUNK 16384
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
    char *reqpath;          // same as dhs->dhs->req->uri->path->full
    void *aux;              // generic pointer - currently used for dirlist()
    char module;            // is 0 for normal fuction, 1 for js module, 3 for module path.
    void * auxbuf;          // aux buffer for req.printf and req.put.
    size_t bufsz;           // size of aux buffer.
    size_t bufpos;          // end position of data in aux buffer.
    uint8_t freeme;         // whether this is a temporary struct that needs to be freed after use.
};

DHS *dhs404 = NULL;
DHS *dhs_dirlist = NULL;

static DHS *new_dhs(duk_context *ctx, int idx)
{
    DHS *dhs = NULL;

    REMALLOC(dhs, sizeof(DHS));
    dhs->func_idx = (duk_idx_t)idx;
    dhs->module=MODULE_NONE;
    dhs->module_name=NULL;
    dhs->ctx = ctx;
    dhs->req = NULL;
    dhs->aux = NULL;
    dhs->reqpath=NULL;
    dhs->pathlen=0;
    /* afaik there is no TIME_T_MAX */
    dhs->timeout.tv_sec = RP_TIME_T_FOREVER;
    dhs->timeout.tv_usec = 0;
    dhs->auxbuf = NULL;
    dhs->bufsz = 0;
    dhs->bufpos = 0;
    dhs->freeme = 0;

    return (dhs);
}


/* mapping for url to filesystem */
#define DHMAP struct fsmap_s
DHMAP
{
    DHS *dhs;
    int nheaders;
    char **hkeys;
    char **hvals;
    char *key;
    char *val;
};
DHMAP **all_dhmaps=NULL;
int n_dhmaps=0;

static void simplefree(void *arg)
{
    if(arg)
        free(arg);
}

static void setdate_header(evhtp_request_t *req, time_t secs)
{
    if(!secs)
        secs=time(NULL);
    if(secs != -1)
    {
        struct tm *ptm = gmtime(&secs);
        char buf[128];
        /* Thu, 21 May 2020 01:41:20 GMT */
        strftime(buf, 128, "%a, %d %b %Y %T GMT", ptm);
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Date", buf, 0, 1));
    }
}

/* free the data after evbuffer is done with it */
static void frefcb(const void *data, size_t datalen, void *val)
{
    if(val)
        free((void *)val);
    else
        free((void *)data);
}

static int comp_min_size=1000;
static int compress_scripts = 0;

static int compress_resp(evhtp_request_t *req, int level, void **outbuf)
{
    struct libdeflate_compressor *compressor;
    size_t inlen=0, outlen=0;
    void *outgz = NULL;
    void *in = NULL;
    struct evbuffer *buf = req->buffer_out;

    inlen = evbuffer_get_length(buf);
    if (inlen < comp_min_size)
    {
        return 0;
    }
    in = (void*) evbuffer_pullup(buf, -1);

    compressor=libdeflate_alloc_compressor(level);
    if(!compressor)
        return 0;

    outlen = libdeflate_gzip_compress_bound(compressor, inlen);
    REMALLOC(outgz, outlen);
    outlen = libdeflate_gzip_compress(compressor, in, inlen, outgz, outlen);

    libdeflate_free_compressor(compressor);

    if (outlen == 0)
        return 0;

    // error check this. -1 return is an error - but then what?  Is the buffer still good?
    evbuffer_drain(buf, inlen);
    evbuffer_add_reference(buf, outgz, outlen, frefcb, outgz);
    if(outbuf)
        *outbuf = outgz;

    return outlen;
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
    RP_MLOCK(&errlock);\
    fprintf(error_fh, "%s ", date);\
    fprintf(error_fh, args);\
    fflush(error_fh);\
    RP_MUNLOCK(&errlock);\
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

    RP_MLOCK(&loglock);

    fprintf(access_fh,"%s - - [%s] \"%s %s%s%s %s\" %d %s \"%s\" \"%s\"\n",
        address,date,method_strmap[method],
        path->full, qm, q, proto,
        code, length, ref, ua
    );
    fflush(access_fh);
    RP_MUNLOCK(&loglock);
}


static void sendresp(evhtp_request_t *request, evhtp_res code, int chunked)
{
    if (!chunked)
        evhtp_send_reply(request, code);
    else
        evhtp_send_reply_chunk_start(request, code);

    if(duk_rp_server_logging)
        writelog(request, (int)code);

}

#define putval(key, val) do {                      \
    if ((char *)(val) != (char *)NULL)             \
    {                                              \
        duk_push_string(ctx, (const char *)(val)); \
        duk_put_prop_string(ctx, -2, (key));       \
    }                                              \
} while(0)

/*
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
*/

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
            //printf("'%.*s'=",(int)sz,p);
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
                    //printf("'%.*s'\n",(int)sz,p);
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
                 //printf("'%.*s'\n",(int)sz,p);
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
    //int i=0;
    //const char *name=NULL;

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
                /* share the memory, and the love */
                duk_push_external_buffer(ctx);
                duk_config_buffer(ctx, -1, begin, sz);
                duk_put_prop_string(ctx,-2,"content");
                duk_put_prop_index(ctx,-2, (duk_uarridx_t)duk_get_length(ctx, -2));
            }
        }
        else
            break;
    }
}

static void copy_post_vars(duk_context *ctx)
{
    if (!duk_get_prop_string(ctx, -1, "postData"))
    {
        duk_pop(ctx);
        return;
    }

    if( !duk_get_prop_string(ctx, -1, "content") )
    {
        duk_pop_2(ctx);
        return;
    }
    duk_remove(ctx, -2); /* discard postData ref, content is on top */

    /* multipart data */
    if(duk_is_array(ctx, -1))
    {
        duk_uarridx_t len, i=0;
        const char *name=NULL;

        len=duk_get_length(ctx, -1);

        for (;i<len;i++)
        {
            duk_get_prop_index(ctx, -1, i);
            if(duk_get_prop_string(ctx, -1, "filename"))
            {
                name=duk_get_string(ctx, -1);
                duk_pop(ctx);
            }
            else
            {
                duk_pop(ctx); /* undefined from filename */
                duk_get_prop_string(ctx, -1, "name");
                name=duk_get_string(ctx, -1);
                duk_pop(ctx);
            }
            duk_get_prop_string(ctx, -1, "content");
            duk_put_prop_string(ctx, -5, name);
            duk_pop(ctx);
        }
    }
    else if (duk_is_object(ctx, -1))
    {
        duk_enum(ctx,-1,DUK_ENUM_OWN_PROPERTIES_ONLY);
        while (duk_next(ctx,-1,1))
        {
            duk_put_prop(ctx,-6); //wow -where the hell am I?
        }
        duk_pop(ctx);
    }

    duk_pop(ctx);
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
    copy_post_vars(ctx);
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

static inline void rp_out_buffer(char character, void *buffer, size_t idx, size_t maxlen)
{
    if (idx < maxlen)
    {
        ((char *)buffer)[idx] = character;
    }
}

static inline void rp_out_null(char character, void *buffer, size_t idx, size_t maxlen)
{
    (void)character;
    (void)buffer;
    (void)idx;
    (void)maxlen;
}


#define checkauxsize(dhs, size) do{\
    size_t required = dhs->bufpos + size;\
    if(dhs->bufsz < required)\
    {\
        while (dhs->bufsz < required)\
            dhs->bufsz += AUX_BUF_CHUNK;\
        REMALLOC(dhs->auxbuf, dhs->bufsz);\
    }\
} while(0)

/* TODO: make this work with forking
duk_ret_t duk_server_add_cookie(duk_context *ctx)
{
    DHS *dhs;
    const char *cookie = NULL;

    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dhs"));
    dhs=duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    if(duk_is_string(ctx, 0))
    {
        cookie = duk_get_string(ctx, 0);
        evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Set-Cookie", cookie, 0, 1));
    }
    else if (duk_is_array(ctx, 0))
    {
        duk_uarridx_t i = 0, len=(duk_uarridx_t) duk_get_length(ctx, 0);
        for(; i<len; i++)
        {
            duk_get_prop_index(ctx, 0, i);
            cookie = REQUIRE_STRING(ctx, -1, "req.addCookie requires a String or array of Strings (cookie values)");
            evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Set-Cookie", cookie, 0, 1));
            duk_pop(ctx);
        }
    }
    else
        RP_THROW(ctx, "req.addCookie requires a String (cookie value)");
    return 0;
}

duk_ret_t duk_server_add_header(duk_context *ctx)
{
    DHS *dhs;

    duk_push_this(ctx);\
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("dhs"));\
    dhs=duk_get_pointer(ctx, -1);\
    duk_pop_2(ctx);\

    if(duk_is_string(ctx, 0))
    {
        const char *key = duk_get_string(ctx, 0);
        const char *val = REQUIRE_STRING(ctx, 1, "req.addHeader - second argument must be a string");
        evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));
    }
    else if (duk_is_object(ctx, 0) && !duk_is_array(ctx, 0) && !duk_is_function(ctx, 0) )
    {
        const char *key, *val;
        duk_enum(ctx, 0, DUK_ENUM_OWN_PROPERTIES_ONLY);
        while(duk_next(ctx, -1, 1))
        {
            key = duk_get_string(ctx, -2);
            if (duk_is_array(ctx, -1))
            {
                duk_uarridx_t i = 0, len=(duk_uarridx_t) duk_get_length(ctx, -1);
                for(; i<len; i++)
                {
                    duk_get_prop_index(ctx, -1, i);
                    val = REQUIRE_STRING(ctx, -1, "req.addHeader(object) requires values that are a String or array of Strings");
                    evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));
                    duk_pop(ctx);
                }
            }
            else
            {
                val = REQUIRE_STRING(ctx, -1, "req.addHeader(object) requires values that are a String or array of Strings");
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));
            }
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    else
        RP_THROW(ctx, "req.addHeader requires a String or Object as its first argument");

    return 0;
}
*/

static void sendws(DHS *dhs);

/* in case of websockets, the dhs may have disappeared
   when http_callback returns.  We'll create a new one
   here as necessary for use in printf, put, wsSend, etc.  */
static DHS *get_dhs(duk_context *ctx)
{
    DHS *dhs=NULL;
    int thrno;

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));
    dhs=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(dhs)
        return dhs;

    dhs=new_dhs(ctx, -1);
    dhs->freeme=1;
    duk_push_this(ctx);
    if (duk_is_undefined(ctx, -1))
        RP_THROW(ctx, "server websockets- reference to req is no longer valid");
    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("evreq")))
        fprintf(stderr, "FIXME: evreq not found\n");
    else
        dhs->req = duk_get_pointer(ctx, -1);

    duk_pop_2(ctx);

    duk_get_global_string(ctx, "rampart");
    duk_get_prop_string(ctx, -1, "thread_id");
    thrno = duk_get_int(ctx, -1);
    duk_pop_2(ctx);

    dhs->threadno = thrno;

    /* the one we will use for now */
    duk_push_pointer(ctx, (void*)dhs);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));

    /* the one we need to free eventually */
    duk_push_pointer(ctx, (void*)dhs);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("fdhs"));

    return dhs;
}

static DHS *free_dhs(DHS *dhs)
{
    duk_context *ctx = dhs->ctx;
    if(dhs->freeme)
    {
        if(dhs->auxbuf)
            free(dhs->auxbuf);
        free(dhs);
        dhs=NULL;
    }
    duk_push_pointer(ctx, (void*)NULL);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));

    duk_push_pointer(ctx, (void*)NULL);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("fdhs"));
    return dhs;
}

/*
    save the callback for disconnect in stash
    indexed by the connection.

    the disconnect callback runs function then
    removes it from stash of callbacks.

*/

#define DISCB struct rp_ws_disconnect_s
DISCB {
    duk_context *ctx;
    uint32_t ws_id;
};

evhtp_hook ws_dis_cb(evhtp_connection_t * conn, short events, void * arg)
{
    DISCB *info = (DISCB*)arg;
    duk_context *ctx = info->ctx;
    double ws_id = (double)info->ws_id;

    free(info);
    duk_push_global_stash(ctx);

    if(duk_get_prop_string(ctx, -1, "wsdis"))
    {
        duk_push_number(ctx, ws_id);
        // [ global_stash, {wsdis}, ws_id ]
        if (duk_get_prop(ctx, -2))
        {
            // [ global_stash, {wsdis}, cbfunc() ]
            if ((duk_pcall(ctx, 0)))
            {
                if (duk_is_error(ctx, -1) )
                {
                    duk_get_prop_string(ctx, -1, "stack");
                    // [ global_stash, {wsdis}, errobj, errstack ]
                    printerr("error in wsOnDisconnect callback:\n %s\n", duk_safe_to_string(ctx, -1));
                    duk_pop(ctx);
                    // [ global_stash, {wsdis}, errobj ]
                }
                else if (duk_is_string(ctx, -1))
                {
                    printerr("error in wsOnDisconnect callback: '%s'\n", duk_safe_to_string(ctx, -1));
                    // [ global_stash, {wsdis}, errobj ]
                }
                else
                {
                    printerr("unknown error in wsOnDisconnect callback");
                }
            }
            // [ global_stash, {wsdis}, ret/err ]
            duk_pop(ctx);//return value or err
            // [ global_stash, {wsdis} ]
            // remove the callback
            duk_push_number(ctx, ws_id);
            duk_del_prop(ctx, -2);
            // [ global_stash, {wsdis} ]
        } else {
            // [ global_stash, {wsdis}, undefined ]
            duk_pop(ctx);
        }
    }
    duk_pop_2(ctx); //global stash and undefined/{wsdis}

    /* null out req */
    if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("wsreq")))
    {
        duk_pop(ctx);
        return 0;
    }

    duk_push_number(ctx, ws_id);

    if(duk_get_prop(ctx, -2))
    {
        /* other references might exist, so replace evhtp req with null */
        duk_push_pointer(ctx,(void*)NULL);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("evreq"));
    }
    duk_pop(ctx);

    /* get rid of this reference */
    duk_push_number(ctx, ws_id);
    duk_del_prop(ctx, -2);
    duk_pop(ctx);

    return 0;
}


duk_ret_t duk_server_ws_set_disconnect(duk_context *ctx)
{
    evhtp_request_t *req;

    if(!duk_is_function(ctx, 0))
        RP_THROW(ctx, "wsOnDisconnect argument must be a function");

    duk_push_this(ctx);
    if (duk_is_undefined(ctx, -1))
        RP_THROW(ctx, "server req.wsOnDisconnect- reference to req is no longer valid");
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("evreq"));
    req = (evhtp_request_t *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    if(!req)
        return 0;

    duk_push_global_stash(ctx);

    if(!duk_get_prop_string(ctx, -1, "wsdis"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    duk_push_number(ctx, (double)req->ws_id);
    duk_dup(ctx, -1);
    /* only set it once */
    if(duk_has_prop(ctx, -3))
        return 0;

    duk_pull(ctx,0);
    duk_put_prop(ctx, -3);

    duk_put_prop_string(ctx, -2, "wsdis");

    return 0;
}


duk_ret_t duk_server_ws_end(duk_context *ctx)
{
    evhtp_request_t *req;
    DHS *dhs;
    int disconnect_timing = EVHTP_DISCONNECT_DEFER;

    if(duk_get_boolean_default(ctx, 0, 0))
    {
        disconnect_timing = EVHTP_DISCONNECT_IMMEDIATE;
    }

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));
    dhs=duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if(dhs)
        dhs->req=NULL;

    duk_push_this(ctx);
    if (duk_is_undefined(ctx, -1))
        RP_THROW(ctx, "server req.wsEnd- reference to req is no longer valid");
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("req"));
    req = (evhtp_request_t *)duk_get_pointer(ctx, -1);
    duk_pop_2(ctx);

    if(!req)
        return 0;

    evhtp_ws_disconnect(req, disconnect_timing);

    duk_push_this(ctx);
    duk_push_pointer(ctx, (void*)NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("req"));
    return 0;
}

duk_ret_t duk_server_ws_send(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);

    if(!dhs->req)
    {
        duk_push_false(ctx);
        return 1;
    }
    sendws(dhs);

    dhs = free_dhs(dhs);
    duk_push_true(ctx);
    return 1;
}

duk_ret_t duk_server_printf(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);
    size_t size;

    size = rp_printf(rp_out_null, NULL, 0, ctx, 0, NULL);
    checkauxsize(dhs, size);
    size = rp_printf(rp_out_buffer, (char*)dhs->auxbuf + dhs->bufpos, size, ctx, 0, NULL);
    dhs->bufpos += size;

    duk_push_int(ctx, (int) size);
    return 1;
}

duk_ret_t duk_server_put(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);
    size_t size;
    duk_size_t dsz;
    const char *s;

    s = REQUIRE_STR_OR_BUF(ctx, 0, &dsz, "req.put requires a string or buffer");
    size = (size_t)dsz; // probably the same type, but IDK

    checkauxsize(dhs, size);
    memcpy(dhs->auxbuf + dhs->bufpos, s, size);
    dhs->bufpos += size;
    duk_push_int(ctx, (int) size);

    return 1;
}

duk_ret_t duk_server_getbuffer(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);
    void *buf;

    buf = duk_push_fixed_buffer(ctx, dhs->bufpos);
    memcpy(buf, dhs->auxbuf, dhs->bufpos);

    return 1;
}

duk_ret_t duk_server_rewind(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);
    int position = REQUIRE_INT(ctx, 0, "req.rewind requires a number greater than 0 as its argument");

    if(position<0)
        position = dhs->bufpos + position;

    if( position <= dhs->bufpos)
            dhs->bufpos=position;
    else
        RP_THROW(
            ctx,
            "req.rewind - cannot set a position past the end of buffer (requested position:%d > current position:%d)\n",
            position, (int) dhs->bufpos
        );
    return 0;
}

duk_ret_t duk_server_getpos(duk_context *ctx)
{
    DHS *dhs = get_dhs(ctx);

    duk_push_number(ctx, (double)dhs->bufpos);

    return 1;
}

duk_ret_t duk_server_getmime(duk_context *ctx)
{
    const char *ext = REQUIRE_STRING(ctx, 0, "req.getMime - parameter must be a string (filename extension)");
    RP_MTYPES m;
    RP_MTYPES *mres, *m_p = &m;

    m.ext = ext;
    /* look for proper mime type listed in mime.h */
    mres = bsearch(m_p, allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
    if (mres)
        duk_push_string(ctx, mres->mime);
    else
        return 0;
    return 1;
}


/* update request vars for ws connection */
static int update_req_vars(DHS *dhs, int gotbuf)
{
    int ret=0;
    double d=0;
    duk_context *ctx = dhs->ctx;

    if(!gotbuf)
    {
        duk_size_t bsz;
        void *buf=NULL;

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
            ret=-1;//signal no post data at all.
        }
        duk_put_prop_string(ctx, -2, "body");

        if( dhs->req->ws_opcode == OP_BIN)
            duk_push_true(ctx);
        else
            duk_push_false(ctx);
    }
    else
    {
        /* this is our first connection, set the callback */
        evhtp_connection_t *conn;
        DISCB *info=NULL;
        conn = evhtp_request_get_connection(dhs->req);

        REMALLOC(info, sizeof(DISCB));
        info->ctx = ctx;
        info->ws_id = dhs->req->ws_id;

        evhtp_connection_set_hook(conn, evhtp_hook_on_event, (evhtp_hook)ws_dis_cb, (void*)info);
        duk_push_false(ctx);
    }
    // from true/false push above
    duk_put_prop_string(ctx, -2, "wsIsBin");

    duk_get_prop_string(ctx, -1, "count");
    if(duk_is_number(ctx, -1))
        d = duk_get_number(ctx, -1) + 1;
    duk_pop(ctx);
    duk_push_number(ctx, d);
    duk_put_prop_string(ctx, -2, "count");

    duk_push_number(ctx, (double) dhs->req->ws_id);
    duk_put_prop_string(ctx, -2, "websocketId");


    return ret;
}

/* copy request vars from evhtp to duktape */

static int push_req_vars(DHS *dhs)
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
    int ret=0;

    duk_push_c_function(ctx, duk_server_printf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "printf");
    duk_push_c_function(ctx, duk_server_put, 1);
    duk_put_prop_string(ctx, -2, "put");
    duk_push_c_function(ctx, duk_server_getpos, 0);
    duk_put_prop_string(ctx, -2, "getpos");
    duk_push_c_function(ctx, duk_server_rewind, 1);
    duk_put_prop_string(ctx, -2, "rewind");
    duk_push_c_function(ctx, duk_server_getbuffer, 0);
    duk_put_prop_string(ctx, -2, "getBuffer");
    duk_push_c_function(ctx, duk_server_getmime, 1);
    duk_put_prop_string(ctx, -2, "getMime");

    if(dhs->req->cb_has_websock)
    {
        duk_push_pointer(ctx, (void*) (dhs->req));
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("req"));
        duk_push_c_function(ctx, duk_server_ws_send,1);
        duk_put_prop_string(ctx, -2, "wsSend");
        duk_push_c_function(ctx, duk_server_ws_end,1);
        duk_put_prop_string(ctx, -2, "wsEnd");
        duk_push_c_function(ctx, duk_server_ws_set_disconnect,1);
        duk_put_prop_string(ctx, -2, "wsOnDisconnect");
    }

    if(!duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("evreq")))
    {
        duk_pop(ctx);
        duk_push_pointer(ctx, (void*) dhs->req);
        duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("evreq"));
    }
    /*
    duk_push_c_function(ctx, duk_server_add_header, 2);
    duk_put_prop_string(ctx, -2, "addHeader");
    duk_push_c_function(ctx, duk_server_add_cookie, 1);
    duk_put_prop_string(ctx, -2, "addCookie");
    */

    /* aux is the local filesystem path for directory listing */
    /* or the error message set to 404 if developer_mode == 0 */
    if(dhs->aux)
    {
        if(!strncmp(dhs->aux,"500",3))
        {
            putval("errMsg", (const char *) (dhs->aux)+3);
        }
        else
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
    }

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
    {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_put_prop_string(ctx,-3,"cookies");
    }
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
                duk_push_string(ctx,"multipart/form-data");
                duk_put_prop_string(ctx, -2, "Content-Type");
                duk_push_array(ctx);
                push_multipart(ctx,bound,buf,bsz);
                duk_put_prop_string(ctx,-2,"content");
                duk_put_prop_string(ctx,-2,"postData");
                ret=1; // we have an external buffer
            }
        } else
        if(strncmp("application/json",ct,16)==0)
        {
            //duk_push_array(ctx);
            duk_push_object(ctx);
            duk_push_string(ctx, "application/json");
            duk_put_prop_string(ctx, -2, "Content-Type");
            duk_get_global_string(ctx, "JSON");
            duk_get_prop_string(ctx, -1, "parse");
            duk_remove(ctx,-2);
            //duk_get_prop_string(ctx, -3, "body");
            duk_push_external_buffer(ctx);
            duk_config_buffer(ctx,-1,buf,bsz); /* add reference to buf for js buffer */
            if (!duk_is_undefined(ctx,-1))//why would this be undefined.  Think about why you did this and remove if no good reason.
            {
                duk_buffer_to_string(ctx, -1);
                if(duk_pcall(ctx,1) != 0)
                {
                    duk_push_object(ctx);
                    duk_safe_to_string(ctx,-2);
                    duk_pull(ctx,-2);
                    duk_put_prop_string(ctx,-2,"error parsing JSON content");
                }
                duk_put_prop_string(ctx,-2,"content");
                //duk_put_prop_index(ctx,-2,0);
                //duk_put_prop_string(ctx,-2,"postData");
            }
            else
                duk_pop_2(ctx); /*parse() and undefined */

            duk_put_prop_string(ctx,-2,"postData");

        } else
        if(strncmp("application/x-www-form-urlencoded",ct,33)==0)
        {
            char *s;
            duk_push_object(ctx);
            duk_push_string(ctx, "application/x-www-form-urlencoded");
            duk_put_prop_string(ctx, -2, "Content-Type");
            //duk_get_prop_string(ctx, -2, "body");
            duk_push_external_buffer(ctx);
            duk_config_buffer(ctx,-1,buf,bsz); /* add reference to buf for js buffer */
            duk_buffer_to_string(ctx,-1);
            s=(char *)duk_get_string(ctx,-1);
            duk_rp_querystring2object(ctx, s);
            duk_put_prop_string(ctx, -3, "content");
            duk_pop(ctx);
            duk_put_prop_string(ctx, -2, "postData");
        }
        else
        /* unknown type -- report type and reference body as content */
        {
            duk_push_object(ctx);
            duk_push_string(ctx, ct);
            duk_put_prop_string(ctx, -2, "Content-Type");
            //duk_get_prop_string(ctx, -2, "body");
            duk_push_external_buffer(ctx);
            duk_config_buffer(ctx,-1,buf,bsz); /* add reference to buf for js buffer */
            duk_put_prop_string(ctx, -2, "content");
            duk_put_prop_string(ctx, -2, "postData");
            ret=1;
        }
    }
    else
    {
        duk_pop(ctx);
        duk_push_object(ctx);
        duk_push_string(ctx, "");
        duk_put_prop_string(ctx, -2, "Content-Type");
        duk_put_prop_string(ctx, -3, "postData");
        // This is still on the stack in the 'else' case.
        // In if(getpropstring("content-type")) above. 'headers' is alredy put in object higher.
        duk_put_prop_string(ctx, -2, "headers");
    }

    flatten_vars(ctx);
    duk_put_prop_string(ctx, -2, "params");

    /* we want body at the end for easy viewing if converted to json*/
    if(bsz)
    {
        duk_push_external_buffer(ctx);
        duk_config_buffer(ctx,-1,buf,bsz); /* add reference to buf for js buffer */
    }
    else
    {
        (void) duk_push_fixed_buffer(ctx, 0);
        ret=-1;//signal no post data at all.
    }
    duk_put_prop_string(ctx, -2, "body");
    return ret;
}
char msg500[] = "<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>%s</pre></p></body></html>";

static void http_callback(evhtp_request_t *req, void *arg);


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
    sendresp(req, EVHTP_RES_NOTFOUND, 0);
}

static void send500(evhtp_request_t *req, char *msg)
{
    if(developer_mode)
    {
        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
        evbuffer_add_printf(req->buffer_out, msg500, msg);
        sendresp(req, 500, 0);
    }
    else if(dhs404)
    {
        char *s=NULL;
        DHS newdhs;

        newdhs.func_idx = dhs404->func_idx;
        newdhs.timeout  = dhs404->timeout;
        newdhs.pathlen  = dhs404->pathlen;
        newdhs.module   = dhs404->module;

        REMALLOC(s, strlen(msg)+4);
        strcpy(s, "500");
        strcat(s, msg);
        newdhs.aux=s;
        http_callback(req, &newdhs);
        free(s);
        dhs404->aux=NULL;
    }
    else
        send404(req);
}
static void send403(evhtp_request_t *req)
{
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
    char msg[] = "<html><head><title>403 Forbidden</title></head><body><h1>Forbidden</h1><p>The requested URL is Forbidden.</p></body></html>";
    evbuffer_add(req->buffer_out, msg, strlen(msg));
    sendresp(req, 403, 0);
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
    /* FIXME: it is going to be pullup'd anyway, why not just add it (not by reference) */
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

static int compress_level = 1;
static char *_compressibles[] =
{
    "html",
    "css",
    "js",
    "json",
    "xml",
    "txt",
    "text",
    "htm",
    NULL
};

static char **compressibles = _compressibles;
static char *cachedir = ".gzipcache/";
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
    const char *range = NULL, *accept=NULL;
    mode_t mode;
    char *ext=NULL;
    char gzipfile[ strlen(fn) + strlen(cachedir) + 4 ];
    char *p, slen[64];

    if (compressibles)
    {
        /* turn /my/path/to/file.html into /my/path/to/.gzipcache/file.html.gz */
        strcpy(gzipfile, fn);
        p=strrchr(gzipfile, '/');
        if(!p)
            p=gzipfile;
        else
            p++;
        strcpy(p,cachedir);
        p=strrchr(fn,'/');
        if(!p)
            p=fn;
        else
            p++;
        strcat(gzipfile,p);
        strcat(gzipfile,".gz");
    }

    if (filestat)
    {
        sb=filestat;
    }
    else if (stat(fn, sb) == -1)
    {
        if(compressibles && stat(gzipfile, sb) != -1)
        {
            if(unlink(gzipfile)==-1)
                printerr("error removing cache file %s: %s\n", gzipfile, strerror(errno));
        }
        send404(req);
        return;
    }
    // the path name cannot have /.gzipcache/ in it.
    if(strstr(fn, "/.gzipcache/"))
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

    setdate_header(req, sb->st_mtime);

    if (!haveCT)
    {
        RP_MTYPES m;
        RP_MTYPES *mres, *m_p = &m;

        ext = strrchr(fn, '.');

        if (!ext) /* || strchr(ext, '/')) shouldn't happen */
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "application/octet-stream", 0, 0));
        else
        {
            ext++;
            m.ext = ext;
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
            len = filesize - beg;
        }
    }
    else
    {
        len=filesize;
    }


    accept = evhtp_kv_find(req->headers_in,"Accept-Encoding");
    if (compressibles && accept && strcasestr(accept, "gzip"))
    {
        char **comps = compressibles;
        while( *comps && strcmp(*comps, ext) )
            comps++;

        if( *comps )
        {
            // check cache
            struct stat cstat;
            int makecache=0, cfd=-1;
            char *p = strrchr(gzipfile,'/');

            if (stat(gzipfile, &cstat) == -1)
            {
                //file doesn't exist.  Check for directory.
                *p='\0';
                if (stat(gzipfile, &cstat) == -1)
                {
                    // make it
                    mode_t old_umask=umask(0);

                    if (mkdir(gzipfile, 0775) != 0)
                        makecache=0;
                    else
                        makecache=1;
                    (void)umask(old_umask);
                }
                else
                {
                    //check that it is a dir and is writable
                    makecache=1;
                    //if( cstat.st_mode & S_IFMT != S_IFDIR)
                    if(!S_ISDIR(cstat.st_mode) || (!( S_IRWXU | cstat.st_mode)) )
                        makecache=0;
                }
                *p='/';
            }
            else
            {
                // exists, check the date.
                if(sb->st_mtime > cstat.st_mtime)
                    makecache=1;
                else
                {
                    mode = sb->st_mode & S_IFMT;
                    if (mode != S_IFREG)
                        makecache=0;
                    else if ((cfd = open(gzipfile, O_RDONLY)) == -1)
                        makecache=0;
                    else
                    {
                        //its a normal file and we can read it.
                        len = (ev_off_t) cstat.st_size;
                        if(len)
                        {
                            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Encoding", "gzip", 0, 0));
                            snprintf(slen, 64, "%d", (int)len);
                            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Length", slen, 0, 1));
                            rp_evbuffer_add_file(req->buffer_out, cfd, 0, len);
                            sendresp(req, rescode, 0);
                            return;
                        }
                    }

                }
            }
            /* the actual compression */
            {
                void *buf=NULL;
                rp_evbuffer_add_file(req->buffer_out, fd, beg, len);
                int tlen = compress_resp(req, compress_level, &buf);
                if(tlen)
                {
                    len = (ev_off_t) tlen;
                    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Encoding", "gzip", 0, 0));
                    /* save the gz file */
                    if(makecache)
                    {
                        FILE *f = fopen(gzipfile,"w");
                        if(f)
                        {
                            size_t wrote=fwrite(buf,1,(size_t)len,f);
                            if (wrote != (size_t)len)
                            {
                                printerr("error writing to %s: %s\n", gzipfile, strerror(errno));
                                unlink(gzipfile);
                            }
                            fclose(f);
                        }
                        else
                            printerr("error opening %s: %s\n", gzipfile, strerror(errno));
                    }
                }
            }
        }
        else
            rp_evbuffer_add_file(req->buffer_out, fd, beg, len);
    }
    else
        rp_evbuffer_add_file(req->buffer_out, fd, beg, len);

    snprintf(slen, 64, "%d", (int)len);
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Length", slen, 0, 1));

    sendresp(req, rescode, 0);
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
    sendresp(req, EVHTP_RES_FOUND, 0);
}

static void fileserver(evhtp_request_t *req, void *arg)
{
    DHMAP *map = (DHMAP *)arg;
    evhtp_path_t *path = req->uri->path;
    struct stat sb;
    /* take 2 off of key for the slash and * and add 1 for '\0' and one more for a potential '/' */
    char *s, fn[strlen(map->val) + strlen(path->full) + 4 - strlen(map->key)];
    mode_t mode;
    int i = 0, len = (int) strlen(path->full);

//    don't do that!  It will be stomped on.
//    dhs->req = req;

    strcpy(fn, map->val);
    s=duk_rp_url_decode( path->full, &len );

    /* redirect /mappeddir to /mappeddir/ */
    if ( !strcmp (s, map->key))
    {
        free(s);
        strcpy(fn, path->full);
        strcat(fn, "/");
        sendredir(req, fn);
        return;
    }
    /* don't look for /mappeddirEXTRAJUNK
       the next char after /reqdir must be a '/'
     */
    if( *(s + strlen(map->key)) != '/')
    {
        free(s);
        send404(req);
        return;
    }
    strcpy(&fn[strlen(map->val)], s + strlen(map->key) +1);

    free(s);
    if (stat(fn, &sb) == -1)
    {
        //need to send to rp_sendfile to remove any cached gz files
        if (compressibles)
            rp_sendfile(req, fn, 0, NULL);
        else
            send404(req);

        return;
    }
    mode = sb.st_mode & S_IFMT;

    if (mode == S_IFREG)
    {
        int i=0;

        for (;i<map->nheaders; i++)
        {
            evhtp_headers_add_header(req->headers_out, evhtp_header_new(map->hkeys[i], map->hvals[i], 0, 0));
        }

        rp_sendfile(req, fn, 0, &sb);
    }
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

/* attach a file to the end of req->buffer_out */
static int attachfile(evhtp_request_t *req, char *fn)
{
    int fd = -1;
    ev_off_t filesize;
    struct stat newstat, *sb=&newstat;
    mode_t mode;

    if (stat(fn, sb) == -1)
    {
        return 0;
    }

    mode = sb->st_mode & S_IFMT;
    if (mode == S_IFREG)
    {
        if ((fd = open(fn, O_RDONLY)) == -1)
        {
            return 0;
        }
    }
    else
    {
        return 0;
    }

    filesize=(ev_off_t) sb->st_size;

    rp_evbuffer_add_file(req->buffer_out, fd, 0, filesize);
    return 1;
}

/* no check done, idx must be a duktape buffer*/
static void attachbuf(DHS *dhs, duk_idx_t idx)
{
    duk_size_t sz;
    const char *s;
    duk_context *ctx = dhs->ctx;
    int variant;

    //is it an external buffer?
    duk_inspect_value(ctx, idx);
    duk_get_prop_string(ctx, -1, "variant");
    variant = duk_get_int_default(ctx, -1, 0);
    duk_pop_2(ctx);

    if(variant == 2)
    // indeed it is, no need to free.
    {
        s = duk_get_buffer_data(ctx, idx, &sz);
        evbuffer_add_reference(dhs->req->buffer_out, s, (size_t)sz, NULL, NULL);
    }
    else
    /* no it isn't, steal and free later */
    {
        CTXFREER *freeme = NULL;

        REMALLOC(freeme, sizeof(CTXFREER));
        duk_to_dynamic_buffer(ctx, idx, &sz);
        s = duk_steal_buffer(ctx, idx, &sz);
        freeme->ctx = ctx;
        freeme->threadno = (int)dhs->threadno;
        if( dhs->req->cb_has_websock)
            freeme->threadno += totnthreads;
        evbuffer_add_reference(dhs->req->buffer_out, s, (size_t)sz, refcb, freeme);
    }
}

/* send the shared memory buffer.
   if mmapSz is set, use it
   if 0, calculate it

   after, send auxbuf if exists
   in whatever form
*/
static int sendmem(DHS *dhs)
{
    if(!dhs->bufpos)
        return 0;
    evbuffer_add_reference(dhs->req->buffer_out, dhs->auxbuf, dhs->bufpos, frefcb, NULL);
    dhs->auxbuf=NULL;
    dhs->bufpos=0;
    dhs->bufsz=0;
    return 1;
}

/* send a buffer or a string
   will send content of req.printf first
   if there is nothing to be sent, returns 0
*/
static int sendbuf(DHS *dhs)
{
    const char *s;
    duk_context *ctx = dhs->ctx;
    duk_size_t sz;
    int ret = 0;

    ret = sendmem(dhs);

    /* null or empty string */
    if( duk_is_null(ctx, -1) || duk_is_undefined(ctx, -1) || 
        ( duk_is_string(ctx, -1) && !duk_get_length(ctx, -1) )
      )
    {
        return ret;
    }

    if ( duk_is_buffer_data(ctx, -1) )
    {
        attachbuf(dhs, -1);
    }
    else
    /* duk_to_buffer does a copy of the string, so we'll just skip adding by reference and copy directly */
    {
        if(duk_is_string(ctx, -1) )
            s = duk_get_lstring(ctx, -1, &sz);
        else if (duk_is_object(ctx, -1))
        {
            duk_json_encode(ctx, -1);
            s = duk_get_lstring(ctx, -1, &sz);
        }
        else
            s= duk_safe_to_lstring(ctx, -1, &sz);
        if(s)
        {
            /* allow string to start with '@' if escaped */
            if (*s == '\\' && *(s + 1) == '@')
            {
                s++;
                evbuffer_add(dhs->req->buffer_out,s,(size_t)sz-1);
            }
            else
                evbuffer_add(dhs->req->buffer_out,s,(size_t)sz);
        }
    }
    return 1;
}

static void sendws(DHS *dhs)
{
//    unsigned char      * outbuf;
    evhtp_request_t *req = dhs->req;
    uint8_t opcode = OP_TEXT;

    if(!req)
        return;

    if (duk_is_buffer_data(dhs->ctx, -1))
        opcode = OP_BIN;

    /* put everything into buffer_out */
    sendbuf(dhs);

    if(!evhtp_ws_add_header(req->buffer_out, opcode))
    {
        fprintf(stderr, "Error prepending header to websocket message\n");
        evbuffer_drain(req->buffer_out, evbuffer_get_length(req->buffer_out));
        return;
    }
//    outbuf = evbuffer_pullup(req->buffer_out, -1);
//printf("%x %x %x %x outbuf len=%d\n", outbuf[0], outbuf[1], outbuf[2], outbuf[3], (int)outlen);

    evhtp_send_reply_body(req, req->buffer_out);
}

/* send the object by mime type
   fill evbuffers with appropriate data
   for websockets, just send using object/data on top of the stack
   This function does not remove the object/data from the top of
   the stack
*/
static evhtp_res obj_to_buffer(DHS *dhs)
{
    RP_MTYPES m;
    RP_MTYPES *mres;
    int gotdata = 0, gotct = 0, gotdate=0;
    duk_context *ctx = dhs->ctx;
    evhtp_res res = 200;
    const char *setkey=NULL, *accept=NULL;
    int docompress = compress_scripts;
    int complev=1;
    duk_double_t delay=0.0;

    if(!dhs->req)
        return 0;
    /* for websockets */
    if( dhs->req->cb_has_websock)
    {
            if(duk_is_null(ctx, -1) || duk_is_undefined(ctx, -1) )
                return 0; //0 means don't send reply via evhtp

            sendws(dhs);
            return 0;
    }

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


                if (!strcasecmp(key, "content-type"))
                    gotct = 1;
                else if (!strcasecmp(key, "date"))
                    gotdate=1;

                if( duk_is_string(ctx, -1) )
                    val=duk_get_string(ctx, -1);
                else if (duk_is_array(ctx, -1))
                {
                    duk_uarridx_t i=0,
                        len = (duk_uarridx_t) duk_get_length(ctx, -1);

                    for (;i<len;i++)
                    {
                        duk_get_prop_index(ctx, -1, i);
                        if (duk_is_string(ctx, -1))
                            val=duk_get_string(ctx, -1);
                        else
                            val=duk_json_encode(ctx, -1);
                        evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));
                        duk_pop(ctx);
                    }
                    duk_pop_2(ctx);
                    continue;
                }
                else
                    val=duk_json_encode(ctx, -1);

                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(key, val, 1, 1));

                duk_pop_2(ctx);
            }

            duk_pop(ctx); /* pop enum and headers obj */
            duk_del_prop_string(ctx, -2, "headers");
        }
        else
        {
            duk_del_prop_string(ctx, -2, "headers");
            printerr("server.start: callback -- \"headers\" parameter in return value must be set to an object (headers:{...})\n");
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

    if (duk_get_prop_string(ctx, -1, "compress"))
    {
        if (duk_is_boolean(ctx, -1))
        {
            if(duk_get_boolean(ctx, -1))
                docompress=1;
            else
                docompress=0;
        }
        else if (duk_is_number(ctx, -1))
        {
            complev = duk_get_int(ctx, -1);
            docompress=1;
            if (complev == 0)
                docompress=0;
            if (complev>10)
            {
                printerr("server.start: callback -- \"compress\" must be a Boolean or an Integer 1-10 (compression level)\n");
                complev=10;
            }
            else if (complev<0)
            {
                printerr("server.start: callback -- \"compress\" must be a Boolean or an Integer 1-10 (compression level)\n");
                docompress=0;
            }
        }
        duk_del_prop_string(ctx, -2, "compress");
    }
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, -1, "chunk"))
    {
        if (duk_is_boolean(ctx, -1))
        {
            // this is going to be set anyway in evhtp_send_reply_chunk_start and it is a suitable flag
            dhs->req->flags |= EVHTP_REQ_FLAG_CHUNKED;
        }
        else
        {
            printerr("server.start: callback -- \"chunk\" must be a Boolean\n");
        }
        duk_del_prop_string(ctx, -2, "chunk");
        docompress=0; //todo: support compression and chunking together.
    }
    duk_pop(ctx);

    if (duk_get_prop_string(ctx, -1, "chunkDelay"))
    {
        if(duk_is_number(ctx, -1))
            delay=duk_get_number(ctx, -1);
        else
            printerr("server.start: callback -- \"chunkDelay\" must be a positive Number\n");

        if(delay < 0)
        {
             printerr("server.start: callback -- \"chunkDelay\" must be a positive Number\n");
             delay=0.0;
        }
        duk_del_prop_string(ctx, -2, "chunkDelay");
    }
    duk_pop(ctx);

    duk_enum(ctx, -1, 0);
    while (duk_next(ctx, -1, 1))
    {
        m.ext = (char *)duk_to_string(ctx, -2);

        if (gotdata)
            goto opterr;

        mres = bsearch(&m, allmimes, n_allmimes, sizeof(RP_MTYPES), compare_mtypes);
        if (mres)
        {
            const char *d;

            gotdata = 1;
            setkey=m.ext;
            if (!gotct)
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));

            if(!gotdate)
                setdate_header(dhs->req,0);
            /* if data is a string and starts with '@', its a filename */
            if (duk_is_string(ctx, -1) && ((d = duk_get_string(ctx, -1)) || 1) && *d == '@')
            {
                if(dhs->bufpos)
                {
                    sendmem(dhs);
                    attachfile(dhs->req, (char *)d+1);
                }
                else
                {
                    rp_sendfile(dhs->req, (char *)d+1, 1, NULL);
                    return (0);
                }
            }
            else if (dhs->req->flags & EVHTP_REQ_FLAG_CHUNKED && duk_is_function(ctx, -1) )
            {
                duk_remove(ctx, -3);//enum
                duk_push_number(ctx, delay);
                duk_replace(ctx, -3);//key
                //leave function (val) on top
                return res;
            }
            else
                sendbuf(dhs); /* send buffer or string contents at idx=-1 */

            duk_pop_2(ctx);
            continue;
        }
        else
        {
            const char *bad_option = duk_to_string(ctx, -2);
            //char estr[20 + strlen(bad_option)];
            printerr( "WARNING: Invalid key in return object '%s'\n", bad_option);
            //sprintf(estr, "unknown option '%s'", bad_option);
            //send500(dhs->req, estr);
            duk_pop_2(ctx);
            continue;
            //return (0);
        }

        opterr:
            printerr( "WARNING: Data already set from '%s', '%s=\"%s\"' is redundant.\n", setkey, m.ext, duk_to_string(ctx, -1));
            //RP_THROW(ctx, "Data already set from '%s', \"%s\" is redundant.\n", m.ext, duk_get_string(ctx, -2));
            duk_pop_2(ctx);
            return (res);
    }
    duk_pop(ctx);

    // allow script to return just status:404 without content
    if(!gotdata && res==404)
    {
        send404(dhs->req);
        return 0;
    }

    if(docompress)
    {
        accept = evhtp_kv_find(dhs->req->headers_in,"Accept-Encoding");
        if(accept && strcasestr(accept, "gzip"))
        {
            int len = compress_resp(dhs->req, complev, NULL);
            if(len)
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Encoding", "gzip", 0, 0));
        }
    }
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
        duk_enum(ctx, -2, DUK_ENUM_INCLUDE_HIDDEN|DUK_ENUM_INCLUDE_SYMBOLS);
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

/* ctx object and tctx object should be at top of respective stacks */
static int copy_obj(duk_context *ctx, duk_context *tctx, int objid)
{
    const char *s;

    /* for debugging *
    const char *lastvar="global";
    if(duk_is_string(ctx, -2) )
        lastvar=duk_get_string(ctx, -2);
    printf("copy_obj %s top=%d\n",lastvar, (int)duk_get_top(ctx));
    * end for debugging */


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
            // this really shouldn't happen either. If it does, we may infinitely recurse.
            duk_pop_3(tctx);
            cprintf("ref object not found in stash, going to enum_object\n");
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

        /* this is an internal flags and should not be copied to threads */
        if(!strcmp(s, "\xffobjRefId") )
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
        else if (duk_is_buffer_data(ctx, -1))
        {
            //no check for buffer type, just copy data into a plain buffer.
            duk_size_t sz;
            int variant=0;
            void *frombuf = duk_get_buffer_data(ctx, -1, &sz);
            void *tobuf;

            duk_inspect_value(ctx, -1);
            duk_get_prop_string(ctx, -1, "variant");
            variant = duk_get_int_default(ctx, -1, 0);
            duk_pop_2(ctx);
            if (variant == 2) //highly unlikely
                variant=0; //copy to fixed buffer
            
             tobuf = duk_push_buffer(tctx, sz, variant);
             memcpy(tobuf, frombuf, sz);
             
             duk_put_prop_string(tctx, -2, s);
        }
        else if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1) && !duk_is_c_function(ctx, -1))
        {
            /* check for date */
            if(duk_has_prop_string(ctx, -1, "getMilliseconds") && duk_has_prop_string(ctx, -1, "getUTCDay") )
            {
                duk_push_string(ctx, "getTime");
                //not a lot of error checking here.
                if(duk_pcall_prop(ctx, -2, 0)==DUK_EXEC_SUCCESS)
                {
                    duk_get_global_string(tctx, "Date");
                    duk_push_number(tctx, duk_get_number_default(ctx, -1, 0));
                    duk_new(tctx, 1);
                    duk_put_prop_string(tctx, -2, s);
                }
                duk_pop(ctx);
            } 
            else
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
                    // convert back to array if ctx object was an array
                    if(duk_is_array(ctx, -1))
                    {
                        duk_rp_values_from_object(tctx, -1);
                        duk_remove(tctx, -2);
                    }
                    duk_put_prop_string(tctx, -2, duk_get_string(ctx, -2));
                }
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

      http_callback
      |-> http_thread_callback
      |   |-> http_dothread -> Call js func. If there is a timeout set, this function will be in its own thread
*/

/* get the function, whether its a function or a (module) object containing the function */

static int getfunction(DHS *dhs){
    int ret=1;
    duk_context *ctx=dhs->ctx;
    duk_get_prop_index(ctx, 0, (duk_uarridx_t)(dhs->func_idx) );
    //printf("in getfunction() duk_get_prop_index(ctx, 0, %d)\n", (int) dhs->func_idx);
    //prettyprintstack(ctx);
    if( !duk_is_function(ctx, -1) ) {
    //printf("duk_get_prop_string(ctx, -1, '%s') -- at stack:\n", dhs->module_name);
    //prettyprintstack(ctx);
        duk_get_prop_string(ctx, -1, dhs->module_name);
        duk_remove(ctx, -2); /* the module object */
        if(!duk_is_function(ctx, -1)) {
            if(duk_is_object(ctx, -1)){
                //printf("path = %s, pathlen = %d, subpath = '%s'\n", dhs->reqpath, dhs->pathlen, dhs->reqpath + dhs->pathlen);
                char *s;
                /* if pathlen == 0, then the length is variable.  All we
                   can do is go backwards to the last '/'.  This means
                   for glob and regex, only '/file.html' can be matched.
                   Otherwise '/path/file.html' can also be matched.
                */
                if(dhs->pathlen)
                    s = dhs->reqpath + dhs->pathlen -1;
                else
                    s = strrchr(dhs->reqpath, '/');
                //printf("checking for key '%s'\n", s);
                if (!s)
                /* this will never happen */
                {
                    s = dhs->reqpath;
                    if(!duk_get_prop_string(ctx, -1, s))
                    {
                        duk_pop(ctx);
                        ret=0;
                    }
                }
                else if(duk_has_prop_string(ctx, -1, s))
                {
                    duk_get_prop_string(ctx, -1, s);
                    duk_remove(ctx, -2);
                    //printf("got key '%s'\n", s);
                }
                else if(duk_has_prop_string(ctx, -1, s+1))
                {
                    duk_get_prop_string(ctx, -1, s+1);
                    duk_remove(ctx, -2);
                    //printf("got key '%s'\n", s+1);
                }
                else
                {
                    duk_pop(ctx);
                    ret=0;
                    //printf("FAILED TO GET KEY %s\n", s);
                }
            }
            else
            {
                //printf("duk_is_object == false\n");
                duk_pop(ctx);
                ret=0;
            }
        }
        //else printf("getfunction: inner isfunction\n");
    }
    //else printf("getfunction: outer isfunction\n");
    return ret;
}

static void clean_reqobj(duk_context *ctx, int has_content, int keepreq)
{
    if(has_content == -1)
        return;

    duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("reqobj"));

    if(!keepreq)
    {
        if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("evreq")))
        {
            duk_pop(ctx);
            duk_push_pointer(ctx, (void*) NULL);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("evreq"));
        }
        else
            duk_pop(ctx);
    }

    if(duk_get_prop_string(ctx, -1, "body"))
    {
        if(duk_is_buffer_data(ctx, -1))
        {
            int variant;

            //is it an external buffer?
            duk_inspect_value(ctx, -1);
            duk_get_prop_string(ctx, -1, "variant");
            variant = duk_get_int_default(ctx, -1, 0);
            duk_pop_2(ctx);

            if(variant == 2)
                duk_config_buffer(ctx, -1, NULL, 0);
        }
    }
    duk_pop(ctx);//body

    if(has_content)
    {
        if(duk_get_prop_string(ctx, -1, "postData"))
        {
            if(duk_get_prop_string(ctx, -1, "content"))
            {
                if(duk_is_array(ctx,-1))
                {
                    int i=0, len= duk_get_length(ctx, -1);
                    while (i<len)
                    {
                        duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                        if(duk_is_object(ctx, -1) && duk_has_prop_string(ctx, -1, "content") )
                        {
                            duk_get_prop_string(ctx, -1, "content");
                            if(duk_is_buffer_data(ctx, -1))
                            {
                                int variant;

                                //is it an external buffer?
                                duk_inspect_value(ctx, -1);
                                duk_get_prop_string(ctx, -1, "variant");
                                variant = duk_get_int_default(ctx, -1, 0);
                                duk_pop_2(ctx);

                                if(variant == 2) //yes, so null out buffer
                                    duk_config_buffer(ctx, -1, NULL, 0);
                            }
                            duk_pop(ctx);//content
                        }
                        duk_pop(ctx);//array member
                        i++;
                    }
                }
                else if (duk_is_buffer_data(ctx, -1))
                {
                    int variant;

                    //is it an external buffer?
                    duk_inspect_value(ctx, -1);
                    duk_get_prop_string(ctx, -1, "variant");
                    variant = duk_get_int_default(ctx, -1, 0);
                    duk_pop_2(ctx);

                    if(variant == 2)
                        duk_config_buffer(ctx, -1, NULL, 0);
                }
            }
            duk_pop(ctx);//content or undefined
        }
        duk_pop(ctx);//postData
    }

    duk_pop(ctx);//reqobj
}

#define CHUNKPTR struct chunk_pointer_s

CHUNKPTR {
    duk_context *ctx;
    void *this_heap_ptr; //the JS request (this from req.chunkSend)
    DHS *dhs;
    unsigned int count;
    duk_double_t delay;
    struct timespec start_time;
};


static duk_ret_t send_chunk_chunkend(duk_context *ctx, int end) {
    struct evbuffer * buffer = evbuffer_new();
    evhtp_request_t *req=NULL;
    DHS *dhs;

    duk_push_this(ctx); //idx == 1

//printstack(ctx);

    if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs")))
    {
        printerr("server.start - req.chunkSend - internal error line %d\n",__LINE__);
        duk_push_false(ctx);
        return 1;
    }
    dhs=duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("evreq")) )
        req = duk_get_pointer(ctx, -1);

    duk_pop(ctx);

//printf("in send_chunk, req=%p\n", req);

    if(req)
    {
        if(!end || ( !duk_is_undefined(ctx,0) && !duk_is_null(ctx, 0)) )
        {
            const char *d;
            
            duk_pull(ctx, 0);

            if (duk_is_string(ctx, -1) && ((d = duk_get_string(ctx, -1)) || 1) && *d == '@')
            {
                if(dhs->bufpos)
                {
                    sendmem(dhs);
                    attachfile(dhs->req, (char *)d+1);
                }
                else
                    rp_sendfile(dhs->req, (char *)d+1, 1, NULL);

                evhtp_send_reply_chunk(req, dhs->req->buffer_out);
                dhs->freeme=1;
            }
            else
            {
                
                if(sendbuf(dhs))
                {
                    evhtp_send_reply_chunk(req, dhs->req->buffer_out);
                    dhs->freeme=1;
                }
                //else dhs->freeme=0;//this flags setTimeout to repeat.

            }
        }

        if(end)
        {
            evhtp_connection_unset_hook(req->conn, evhtp_hook_on_write);
            evhtp_send_reply_chunk_end(req);
            duk_del_prop_string(ctx, 1, DUK_HIDDEN_SYMBOL("evreq"));
            dhs->freeme=1;
        }

        duk_push_true(ctx);
    }
    else
        duk_push_false(ctx);

    evhtp_safe_free(buffer, evbuffer_free);
    return 1;
}

static duk_ret_t send_chunk(duk_context *ctx)
{
    return send_chunk_chunkend(ctx, 0);
}

static duk_ret_t send_chunk_end(duk_context *ctx)
{
    return send_chunk_chunkend(ctx, 1);
}

// invalidate this dhs in duktape, but possibly keep the struct around for the next chunk.
#define invalidatedhs do {\
    /* invalidate dhs so no other libevhtp event can use it */\
    duk_push_pointer(ctx, (void*)NULL);\
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));\
    /* free server buffer for next chunk */\
    if(dhs && dhs->auxbuf){\
        free(dhs->auxbuf);\
        dhs->bufsz = dhs->bufpos = 0;\
        dhs->auxbuf=NULL;\
    }\
} while (0)


/* this is called from within duk_rp_set_to.
   It is possible, if client disconnect, it will be
   called after chunkp, req & dhs have all been freed   */
int setdhs(void *arg, int is_post)
{
    CHUNKPTR *chunkp = (CHUNKPTR *)arg;
    DHS *dhs = NULL;
    duk_context *ctx = NULL;
    char reqobj_tempname[24];

    /* chunkp with dhs->ctx and dhs->req  may be invalid, if freed 
       in chunk_finalize so we need to get our ctx from the source   */
    ctx = thread_ctx[local_thread_number];
//printf("got ctx from tno = %d\n", local_thread_number);
    /* check that chunkp is valid.  If temp reference to
       saved js req is gone, then it was freed in chunk_finalize   */ 
    duk_push_global_object(ctx);
    sprintf(reqobj_tempname,"\xFFreq_%p", chunkp);
    if(duk_has_prop_string(ctx, -1, reqobj_tempname))
    {
        dhs = chunkp->dhs;
        ctx = dhs->ctx;
    }
    duk_pop(ctx);

    if(!dhs)
        return 0;

    if(is_post)//called after the JS callback
    {
        if(!dhs->freeme)
            return 1; // nothing was sent, do it again after another timeout.
                      // the libevhtp write callback will not be fired since nothing was sent. 
        invalidatedhs;
        return 0;//don't repeat.
    }
    else //called before the JS callback
    {
        if(dhs->req)
        {
            dhs->freeme=0;// this will be changed to 1 if data is sent;
            evbuffer_drain(dhs->req->buffer_out, -1);
        }
        else
        {
            invalidatedhs;
            return 0; //don't do JS callback
        }
        /* for req.printf, chunkSend, etc called from the JS chunk callback */
        duk_push_pointer(ctx, (void*)dhs);
        duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("dhs"));
    }
    return 1;
}

// insert chunk callback function into loop using setTimeout
static evhtp_res rp_chunk_callback(evhtp_connection_t * conn, void * arg)
{
    CHUNKPTR *chunkp = (CHUNKPTR *) arg;
    evhtp_request_t *req=NULL;
    duk_context *ctx = chunkp->ctx;
    DHS *dhs=NULL;
    struct timespec now;
    duk_double_t timediff_ms = 0.0;

    /* this is the JS req object */
    duk_push_heapptr(ctx, chunkp->this_heap_ptr);

    /* first chunk, set up a dhs struct */
    if(!chunkp->dhs)
    {
        /* minimum dhs required for req.printf et al */
        REMALLOC(dhs, sizeof(DHS));
        dhs->ctx = ctx;
        dhs->auxbuf = NULL;
        dhs->bufsz = 0;
        dhs->bufpos = 0;

        chunkp->dhs=dhs;

        if(duk_get_prop_string(chunkp->ctx, -1, DUK_HIDDEN_SYMBOL("evreq")) )
            dhs->req = req = duk_get_pointer(chunkp->ctx, -1);
        duk_pop(ctx);
        
    }
    else
    {
        dhs=chunkp->dhs;
        req=dhs->req;
    }

    if(!req)
    {
        duk_pop(ctx); // JS req object from heapptr
        return EVHTP_RES_OK;
    }

    chunkp->count++;

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("chunk_cb"));
    duk_insert(ctx, 0);

    if(chunkp->delay > 0.0)
    {
        if( chunkp->start_time.tv_sec)
        {
            clock_gettime(CLOCK_MONOTONIC, &now);

            //add next time to our clock.  That is the time we were aiming for.
            timespec_add_ms(&chunkp->start_time, chunkp->delay);

            //get the actual amount of time
            timediff_ms = chunkp->delay + timespec_diff_ms(&now, &chunkp->start_time);

            /* we may need to skip "frames", but will attempt to keep the timing */
            while( timediff_ms > chunkp->delay)
            {
                timespec_add_ms(&chunkp->start_time, chunkp->delay);
                timediff_ms -= chunkp->delay;
            }

        }
        else //set target time from clock once, then keep schedule by adding the delay.
            clock_gettime(CLOCK_MONOTONIC, &chunkp->start_time);

        duk_push_number(ctx, (chunkp->delay -( (timediff_ms>0.0)? timediff_ms : 0.0) ) );
    }
    else
        duk_push_number(ctx, chunkp->delay);

    duk_insert(ctx, 1);

    // keep JS req object at position 2
    // duk_pop(ctx); // JS req object from heapptr

    // put count into the req object
    duk_push_number(ctx, (double) chunkp->count);
    duk_put_prop_string(ctx, -2, "chunkIndex");

    duk_rp_set_to(ctx, 0, "server callback return value - chunking function", setdhs, chunkp);
    while(duk_get_top(ctx) > 0) duk_pop(ctx);
    return EVHTP_RES_OK;
}

// libevhtp finisher callback for chunking
static evhtp_res chunk_finalize(struct evhtp_connection *conn, void * arg)
{
    CHUNKPTR *chunkp = (CHUNKPTR *) arg;
    DHS *dhs=NULL;
    evhtp_request_t *req=NULL;
    duk_context *ctx=NULL;
    char reqobj_tempname[24];

    if(!chunkp || !chunkp->dhs || !chunkp->ctx)
        return EVHTP_RES_500;

    dhs = chunkp->dhs;
    ctx = chunkp->ctx;

    if(chunkp->dhs)
        req=chunkp->dhs->req;

    if(req)
    {
        evhtp_connection_unset_hook(req->conn, evhtp_hook_on_write);
        // make sure we do this only once
        evhtp_connection_unset_hook(req->conn, evhtp_hook_on_connection_fini);
        evhtp_connection_unset_hook(req->conn, evhtp_hook_on_request_fini);
    }

    duk_push_heapptr(ctx, chunkp->this_heap_ptr);
    duk_push_pointer(ctx, NULL);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("evreq"));
    duk_pop(ctx);
    
    invalidatedhs;
    free(dhs);
    // mark the request null in case of a delayed callback in rp_chunk_callback/setdhs
    chunkp->dhs=NULL;

    /* remove saved reference to JS req object */
    duk_push_global_object(ctx);
    sprintf(reqobj_tempname,"\xFFreq_%p", chunkp);
    duk_del_prop_string(ctx, -1, reqobj_tempname);
    duk_pop(ctx);

    free(chunkp);
    return EVHTP_RES_OK;
}

static duk_ret_t rp_post_req(duk_context *ctx)
{
//printf("in finalizer for req\n");
    evhtp_request_t *req=NULL;

    if(duk_get_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("evreq")) )
        req = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    if(req)
    {
        evhtp_send_reply_chunk_end(req);
    }
    return 0;
}

static void *http_dothread(void *arg)
{
    DHR *dhr = (DHR *)arg;
    evhtp_request_t *req = dhr->req;
    DHS *dhs = dhr->dhs;
    duk_context *ctx=dhs->ctx;
    evhtp_res res = 200;
    int eno, has_content=-1;

#ifdef RP_TIMEO_DEBUG
    pthread_t x = dhr->par;
#endif
    if (dhr->have_timeout)
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* copy function ref to top of stack */
    if(!getfunction(dhs))
    {
        if (dhr->have_timeout)
        {
            RP_MLOCK(&(dhr->lock));
            pthread_cond_signal(&(dhr->cond));
        }
        send404(req);
        if (dhr->have_timeout)
            RP_MUNLOCK(&(dhr->lock));
        return NULL;
    };

    /* populate object with details from client */
    if(req->cb_has_websock)
    {
        int saveobj=0;
        if(!duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("wsreq")))
        {   /* we've never done a ws request in this thread before */
            saveobj=1;
            duk_pop(ctx);
            duk_push_object(ctx);
        }
        //[ ..., function, {wsobj} ]
        duk_push_number(ctx, (double)req->ws_id);
        if(!duk_get_prop(ctx, -2))
        {
            /* first time this connection */
            duk_pop(ctx);
            duk_push_object(ctx); //[ ..., function, {wsobj}, {req} ]
            has_content = push_req_vars(dhs);
            (void)update_req_vars(dhs, 1);
            duk_push_number(ctx, (double)req->ws_id); //[ ..., function, {wsobj}, {req}, key ]
            duk_dup(ctx, -2); //[ ..., function, {wsobj}, {req}, key, val(reqcopy) ]
            duk_put_prop(ctx, -4); //[ ..., function, {wsobj}, {req} ]
        }
        else //[ ..., function, {wsobj}, {req} ]
        {
            has_content=update_req_vars(dhs, 0);
        }
        duk_pull(ctx, -2); //[ ..., function, {req}, {wsobj}]

        if(saveobj)
            duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("wsreq"));
        else
            duk_pop(ctx);
        //[ ..., function, {req} ]
    }
    else
    {	/* a normal http request */
        /* push an empty object for js callback "req" parameter*/
        duk_push_object(ctx);
        has_content = push_req_vars(dhs);
    }

    //put a copy out of the way for a moment
    duk_dup(ctx,-1);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("reqobj"));

    /* execute function "myfunc({object});" */
    if ((eno = duk_pcall(ctx, 1)))
    {
        if (dhr->have_timeout)
        {
            RP_MLOCK(&(dhr->lock));
            pthread_cond_signal(&(dhr->cond));
        }

        evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
        if (duk_is_error(ctx, -1) )
        {
            duk_get_prop_string(ctx, -1, "stack");
            printerr("error in callback:\n%s\n", duk_safe_to_string(ctx, -1));
            if (!req->cb_has_websock)
                send500(req, (char*)duk_safe_to_string(ctx, -1));
            //send500 calls http_callback, which now empties the stack
            //duk_pop(ctx);
        }
        else if (duk_is_string(ctx, -1))
        {
            printerr("error in callback: '%s'\n", duk_safe_to_string(ctx, -1));
            if (!req->cb_has_websock)
                send500(req, (char*)duk_safe_to_string(ctx, -1));
        }
        else
        {
            printerr("unknown error in callback");
            if (!req->cb_has_websock)
                send500(req, "unknown error in callback");
        }

        if (dhr->have_timeout)
            RP_MUNLOCK(&(dhr->lock));

        clean_reqobj(ctx, has_content, req->cb_has_websock);
        return NULL;
    }

    if (dhr->have_timeout)
    {
        debugf("0x%x, LOCKING in thread_cb\n", (int)x);
        fflush(stdout);
        RP_MLOCK(&(dhr->lock));
        pthread_cond_signal(&(dhr->cond));
    }
    /* stack now has return value from duk function call */

    if(!dhs->req) // wsEnd may have killed the connection
    {
        return NULL;
    }

    if(!req->cb_has_websock)
    {
        /* don't accept functions or arrays */
        if (duk_is_function(ctx, -1) || duk_is_array(ctx, -1))
        {
            RP_THROW(ctx, "Return value cannot be an array or a function");
        }

        /* object has reply data and options in it */
        if (!duk_is_object(ctx, -1))
        {
            duk_push_object(ctx);
            duk_pull(ctx, -2);
            duk_put_prop_string(ctx, -2, "text");
        }
    }

    res = obj_to_buffer(dhs);

    CHUNKPTR *chunkp = NULL;

    if(dhs->req->flags & EVHTP_REQ_FLAG_CHUNKED)
    {

        REMALLOC(chunkp, sizeof(CHUNKPTR));
        chunkp->ctx=ctx;
        chunkp->dhs=NULL;
        chunkp->count=-1;
        // the JS request object, save location to use as 'this' later
        duk_get_global_string(ctx, DUK_HIDDEN_SYMBOL("reqobj"));
        chunkp->this_heap_ptr = duk_get_heapptr(ctx, -1);
        /* check if return object included a callback */
        if(duk_is_function(ctx, -2))
        {
            //save it in JS request object
            duk_pull(ctx, -2);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("chunk_cb"));

            //we also have the timeout delay one below on the stack
            chunkp->delay = duk_get_number(ctx, -2);
            duk_remove(ctx, 2);
        }
        duk_push_c_function(ctx, rp_post_req, 1);
        duk_set_finalizer(ctx, -2);
        duk_push_c_function(ctx, send_chunk, 1);
        duk_put_prop_string(ctx, -2, "chunkSend");
        duk_push_c_function(ctx, send_chunk_end, 1);
        duk_put_prop_string(ctx, -2, "chunkEnd");
        duk_pop(ctx); //reqobj
//printf("finalizer should use req %p\n", dhs->req);

        /* when ready to send more data */
        evhtp_connection_set_hook(req->conn, evhtp_hook_on_write, rp_chunk_callback, chunkp);
        /* when client disconnects */
        evhtp_connection_set_hook(req->conn, evhtp_hook_on_connection_fini, chunk_finalize, chunkp);
        /* when we disconnect */
        evhtp_connection_set_hook(req->conn, evhtp_hook_on_request_fini, chunk_finalize, chunkp);

        if(ctimeout.tv_sec==RP_TIME_T_FOREVER)
            evhtp_connection_set_timeouts(req->conn, NULL, NULL);
        else
            evhtp_connection_set_timeouts(req->conn, NULL, &ctimeout);

        sendresp(req, res, 1); //1 for chunked

        chunkp->start_time.tv_sec=0; //first run, no time keeping

        if(dhs->bufsz)
        {
            sendmem(dhs);
            evhtp_send_reply_chunk(req, dhs->req->buffer_out);
        }
    }
    else
    if (res)
        sendresp(req, res, 0);

    duk_pop(ctx);//return from JS callback

    debugf("0x%x, UNLOCKING in thread_cb\n", (int)x);
//    fflush(stdout);
    if (dhr->have_timeout)
        RP_MUNLOCK(&(dhr->lock));

    clean_reqobj(ctx, has_content, req->cb_has_websock);

    if(dhs->req->flags & EVHTP_REQ_FLAG_CHUNKED)
    {
        char reqobj_tempname[24];
        duk_push_global_object(ctx);
        // we need the global "reqobj" gone. but we need to save the reference to it in a unique spot
        // until rp_chunk_callback, otherwise the finalizer will be called.  We can remove post transaction.
        sprintf(reqobj_tempname,"\xFFreq_%p", chunkp); //use the chunkp pointer as an index for the JS req
        duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("reqobj"));
        duk_put_prop_string(ctx, -2, reqobj_tempname);
        duk_del_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("reqobj"));
        duk_pop(ctx);
    }

    return NULL;
}

extern struct slisthead tohead;

static duk_context *redo_ctx(int thrno)
{
    duk_context *thr_ctx = duk_create_heap(NULL, NULL, NULL, NULL, duk_rp_fatal);
    duk_idx_t fno = 0;
    void *bc_ptr, *buf;
    duk_size_t bc_len;

    duk_init_context(thr_ctx);
    duk_push_array(thr_ctx);
    /* copy all the functions previously stored at bottom of stack */
    CTXLOCK;

    //    our array of functions have been stashed
    duk_push_global_stash(main_ctx);
    duk_get_prop_string(main_ctx, -1, "funcstash");
    duk_insert(main_ctx, 0);
    duk_pop(main_ctx);

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

    duk_remove(main_ctx,0);

    /* remove pending events from old context */
    {
        EVARGS *e, *tmp;
        SLISTLOCK;
        SLIST_FOREACH_SAFE(e, &tohead, entries, tmp)
        {
            if(thread_ctx[thrno] == e->ctx)
            {
                SLIST_REMOVE(&tohead, e, ev_args, entries);
                event_del(e->e);
                event_free(e->e);
                free(e);
            }
        }
        SLISTUNLOCK;
    }

    CTXUNLOCK;

    duk_push_global_stash(thr_ctx);
    duk_push_pointer(thr_ctx, thread_base[thrno]);
    duk_put_prop_string(thr_ctx, -2, "elbase");
    duk_pop(thr_ctx);
    duk_dup(thr_ctx,0);
    duk_put_global_string(thr_ctx, DUK_HIDDEN_SYMBOL("thread_funcstash"));

    duk_destroy_heap(thread_ctx[thrno]);
    thread_ctx[thrno] = thr_ctx;
    return thr_ctx;
}

static void
http_thread_callback(evhtp_request_t *req, void *arg, int thrno)
{
    DHS *dhs = arg;
    DHR dhr_s, *dhr = &dhr_s;
    pthread_t script_runner;
    pthread_attr_t attr;
    struct timespec ts;
    struct timeval now;
    int ret = 0;
	uint64_t nsec=0;

#ifdef RP_TIMEO_DEBUG
    pthread_t x = pthread_self();
    printf("%d, start\n", (int)x);
    fflush(stdout);
#endif

    gettimeofday(&now, NULL);

    dhr->dhs = dhs;
    dhr->req = req;
#ifdef RP_TIMEO_DEBUG
    dhr->par = x;
#endif
    /* if no timeout, not necessary to thread out the callback */
    if (req->websock || dhs->timeout.tv_sec == RP_TIME_T_FOREVER)
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

    RP_MINIT(&(dhr->lock));

    pthread_cond_init(&(dhr->cond), NULL);

    /* is this necessary? https://computing.llnl.gov/tutorials/pthreads/#Joining */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    debugf("0x%x, LOCKING in http_callback\n", (int)x);
    RP_MLOCK(&(dhr->lock));

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
        //fflush(stdout);
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

    debugf("exiting http_thread_callback\n");
}


/* call with 0.0 to start, use return from first call to get elapsed *
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
*/

extern struct event_base *elbase;

/* load module if not loaded, return position on stack
    if module file is newer than loaded version, reload
*/

/* kinda hidden, but visible from printstack */
/* DONE: replace RP_HIDDEN_SYMBOL with DUK_HIDDEN_SYMBOL */
//#define RP_HIDDEN_SYMBOL(s) " # " s
#define RP_HIDDEN_SYMBOL(s) DUK_HIDDEN_SYMBOL(s)

static int getmod(DHS *dhs)
{
    duk_idx_t idx=dhs->func_idx;
    duk_context *ctx=dhs->ctx;
    const char *modname = dhs->module_name;
    duk_get_prop_index(ctx, 0, (duk_uarridx_t) idx); // {module:...}
    int ret=0;
    /* is it already in the object? */
    if (duk_get_prop_string(ctx, -1, modname) )
    {
        struct stat sb;
        const char *filename;

        duk_get_prop_string(ctx, -1, RP_HIDDEN_SYMBOL("module_id"));
        filename=duk_get_string(ctx,-1);
        duk_pop(ctx);

        if (stat(filename, &sb) != -1)
        {
            time_t lmtime;

            duk_get_prop_string(ctx,-1,RP_HIDDEN_SYMBOL("mtime"));
            lmtime = (time_t)duk_get_number(ctx,-1);
            duk_pop(ctx); //mtime
            //printf("%d >= %d?",(int)lmtime,(int)sb.st_mtime);
            if(lmtime>=sb.st_mtime)
            {
                //printf("  Yes\n");
                duk_pop_2(ctx); //the module from duk_get_prop_string(ctx, -1, modname) and duk_get_prop_index(ctx, 0, (duk_uarridx_t) idx);
                return 1; // it is there and up to date
            }
            //printf("  No\n");
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
    ret = duk_rp_resolve(ctx, modname);
    if (!ret)
    {
        duk_pop_2(ctx);
        return 0;
    }
    else if(ret == -1)
    {
        if (duk_is_error(ctx, -1) )
        {
            duk_get_prop_string(ctx, -1, "stack");
            printerr("error loading module: '%s' - '%s'\n", modname, duk_safe_to_string(ctx, -1));
            duk_pop(ctx);
            send500(dhs->req, (char*)duk_safe_to_string(ctx, -1));
        }
        else if ( duk_is_string(ctx, -1))
        {
            printerr("error loading module: '%s' - '%s'\n", modname, duk_safe_to_string(ctx, -1));
            send500(dhs->req, (char*)duk_safe_to_string(ctx, -1));
        }
        else
        {
            printerr("error loading module '%s': Unknown error", modname);
            send500(dhs->req, "Unknown error");
        }
        //duk_pop_2(ctx);
        return -1;
    }

    // on stack is module.exports
    duk_get_prop_string(ctx,-1,"exports");
    /* take mtime & id from "module" and put it in module.exports,
        then remove modules */
    if(duk_is_function(ctx, -1))
    {
        /* give the function a name for error reporting */
        duk_push_sprintf(ctx,"module:%s",modname);
        duk_put_prop_string(ctx,-2,"fname");
    }
    else if (duk_is_object(ctx, -1))
    {
        /* do the same, but for each function in object */
        duk_enum(ctx, -1, 0);
        while (duk_next(ctx,-1,1))
        {
            if(duk_is_function(ctx, -1))
            {
                duk_push_sprintf(ctx,"module:%s",modname);
                duk_put_prop_string(ctx,-2,"fname");
            }
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    else
    {
        printerr("{module[Path]: _func}: module.exports must be set to a Function or Object with {key:function}\n");
        duk_pop_3(ctx);
        return 0;
    }
    duk_get_prop_string(ctx,-2,"mtime");
    duk_put_prop_string(ctx,-2,RP_HIDDEN_SYMBOL("mtime"));
    duk_get_prop_string(ctx,-2,"id");
    duk_put_prop_string(ctx,-2,RP_HIDDEN_SYMBOL("module_id"));
    duk_remove(ctx,-2); // now stack is [ func_array, ..., {module:xxx}, func_exports ]

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
    int mplen=0, ret=0;

    duk_get_prop_index(ctx, 0, (duk_uarridx_t)idx);
    duk_get_prop_string(dhs->ctx,-1,"modulePath");
    modpath=duk_get_string(dhs->ctx,-1);

    mplen=strlen(modpath);
    duk_pop(ctx);
    if(stat(modpath, &pathstat)== -1)
    {
        send404(dhs->req);
        ret=0;
    }
    else
    {
        char *s;
        char modname[ strlen(path->full) + strlen(modpath) + 1 ];
        char tm[ strlen(path->full) + strlen(modpath) + 1 ];
        char subpath[ strlen( (path->full) + dhs->pathlen -1 ) + 1 ];

        strcpy(modname,modpath);
//printf("1 modname=%s\n", modname);

        if( *(modname+strlen(modname)-1)=='/' )
            *(modname+strlen(modname)-1)='\0';
//printf("pathlen=%d modpath=%s modname=%s\n",dhs->pathlen,modpath,modname);


        strcpy(subpath, (path->full) + dhs->pathlen -1);

//printf("1 subpath=%s\n", (path->full) + dhs->pathlen -1);

        if(subpath[strlen(subpath)-1] == '/')
            subpath[strlen(subpath)-1] = '\0';
        else
        {
            // remove the extension:
            s=strrchr(subpath,'/');
            if (!s) s=subpath;
            s=strrchr(s,'.');
            if(s) *s='\0';
        }
//printf("2 subpath=%s\n", (path->full) + dhs->pathlen -1);
        strcat(modname, subpath);

//printf("3 modname=%s\n", modname);

        strcpy(tm, modname);

        /* search in object, will be found was found before in
           the mod search below                               */
//printf("looking for %s\n",tm);
        if(!duk_get_prop_string(ctx, -1, tm))
        {
            duk_pop(ctx);
            while (1)
            {
                s=strrchr(tm,'/');
                if(s) *s='\0';
                else {
                    *tm = '\0';
                    break;
                }
                if (strlen(tm)<mplen)
                {
                    *tm = '\0';
                    break;
                }

//printf("looking for %s\n",tm);
                if(duk_get_prop_string(ctx, -1, tm))
                {
                    duk_pop(ctx);
                    break;
                }
                duk_pop(ctx);
            }
        }
        else
        {
            duk_pop(ctx);
//printf("found it on first try\n");
        }

        if(*tm)
        {
//printf("short path! found %s\n", tm);
            duk_pop(ctx);
            dhs->module_name=strdup(tm);
            ret=getmod(dhs);
            dhs->pathlen += strlen(tm) - mplen + 1;
            return ret;
        }
        //search on fs

        duk_pop(ctx);

        strcpy(tm, modname);

        //search in object,
//printf("getmod: looking for %s\n",tm);
        dhs->module_name=tm;
        ret = getmod(dhs);

        if(!ret)
        {
            while (1)
            {
                s=strrchr(tm,'/');
                if(s) *s='\0';
                else {
                    *tm = '\0';
                    break;
                }
                if (strlen(tm)<mplen)
                {
                    *tm = '\0';
                    break;
                }

//printf("getmod: looking for %s\n",tm);
                ret = getmod(dhs);
                if(ret)
                {
                    break;
                }
            }
        }
        else
        {
//printf("found it on first try\n");
        }

        if(*tm)
        {
//printf("long path! found %s\n", tm);
            dhs->module_name=strdup(tm);
        //    ret=getmod(dhs);
            dhs->pathlen += strlen(tm) - mplen + 1;
            return ret;
        }
        dhs->module_name=NULL;
    }
    return ret;
}

static void http_callback(evhtp_request_t *req, void *arg)
{
    DHS newdhs={0}, *dhs = arg;
    int thrno = 0;
    duk_context *new_ctx;

    if (!gl_singlethreaded)
    {
        evthr_t *thread = get_request_thr(req);
        local_thread_number = thrno = *((int *)evthr_get_aux(thread));
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
    //newdhs.reqpath=strdup(req->uri->path->full);
    newdhs.reqpath=req->uri->path->full;
    newdhs.bufsz = 0;
    newdhs.bufpos = 0;
    newdhs.auxbuf= NULL;
    newdhs.freeme=0;
    dhs = &newdhs;


    /* if a ws callback has been set */
    if(req->cb_has_websock)
    {
        /* if this is not a websocket connection */
        if(!req->websock)
        {
            req->cb_has_websock=0; //otherwise send404 will come back here with infinite recursion.
            send404(req);
            return;
        }
        else
        {
            /* no timeout */
            evhtp_connection_set_timeouts(evhtp_request_get_connection(req), NULL, NULL);
            /* get context that is specifically for websockets (with no timeouts allowed) */
            dhs->ctx = thread_ctx[thrno+totnthreads];
            dhs->threadno = (uint16_t)thrno + (uint16_t)totnthreads;
        }
    }

    /* stack must start empty */
    while (duk_get_top(dhs->ctx) > 0) duk_pop(dhs->ctx);

    duk_get_global_string(dhs->ctx, DUK_HIDDEN_SYMBOL("thread_funcstash")); //this should be at index 0;
//printf("dhs req path = %s, module=%d\n",dhs->reqpath, dhs->module);
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
            dhs->module_name=NULL;
            goto http_req_end;
        }
        else if (res == 0)
        {
            send404(dhs->req);
            free(dhs->module_name);
            goto http_req_end;
        }
    }
    else if (dhs->module==MODULE_PATH)
    {
        int res=getmod_path(dhs);
        if(res==-1)
        {
            free(dhs->module_name);
            goto http_req_end;
        }
        else if (res==0)
        {
            //printf("sending 404 from http_callback\n");
            send404(dhs->req);
            free(dhs->module_name);
            goto http_req_end;
        }
    }

    if(duk_get_global_string(dhs->ctx, DUK_HIDDEN_SYMBOL("fdhs")))
    {
        //if not NULL, it is the old one from websock functions, if wsSend was never called
        DHS *fdhs = duk_get_pointer(dhs->ctx, -1);
        if(fdhs)
        {
            fdhs = free_dhs(fdhs);
        }
    }
    duk_pop(dhs->ctx);

    duk_push_pointer(dhs->ctx, (void*) dhs);
    duk_put_global_string(dhs->ctx, DUK_HIDDEN_SYMBOL("dhs"));

    /* do JS callback in a thread if there is a scriptTimeout
       otherwise, just do callback.  Then send output to client */
    http_thread_callback(req, dhs, thrno);

    /* cleanup */
    free(dhs->module_name);

    //dhs is gone at end of this function
    //so we mark it as such
    duk_push_pointer(dhs->ctx, (void*) NULL);
    duk_put_global_string(dhs->ctx, DUK_HIDDEN_SYMBOL("dhs"));

    http_req_end:

//    duk_push_undefined(dhs->ctx);
//    duk_put_global_string(dhs->ctx, DUK_HIDDEN_SYMBOL("clreq"));
    // we should only have thread_funcstash on top, but remove everything
    while (duk_get_top(dhs->ctx) > 0) duk_pop(dhs->ctx);
    return;
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
            evconnlistener_get_fd(htp->servers[htp->nservers-1]),
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

/*
void testcb(evhtp_request_t *req, void *arg)
{
    char rep[] = "hello world";
    evbuffer_add_printf(req->buffer_out, "%s", rep);
    evhtp_send_reply(req, EVHTP_RES_OK); // for pure speed in testing - no logs
}
*/

void initThread(evhtp_t *htp, evthr_t *thr, void *arg)
{
    int *thrno = NULL;
    duk_context *ctx;
    struct event_base *base = evthr_get_base(thr);

    CTXLOCK;

    REMALLOC(thrno, sizeof(int));

    /* drop privileges here, after binding port */
    if(unprivu && !gl_threadno)
    {
        if (setgid(unprivg) == -1)
        {
            CTXUNLOCK;
            RP_THROW(main_ctx, "error setting group, setgid() failed");
        }
        if (setuid(unprivu) == -1)
        {
            CTXUNLOCK;
            RP_THROW(main_ctx, "error setting user, setuid() failed");
        }
    }

    *thrno = gl_threadno++;
    add_exit_func(simplefree, thrno);
    evthr_set_aux(thr, thrno);

    /* for http requests, subject to timeout */
    ctx=thread_ctx[*thrno];
    duk_push_global_stash(ctx);
    duk_push_pointer(ctx, (void*)base);
    duk_put_prop_string(ctx, -2, "elbase");
    duk_pop(ctx);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("thread_funcstash"));

    //a special reset for sql module so that new thread/fork will have settings reapplied
    //in rampart-sql.c:reset_tx_default
    duk_push_int(ctx, -2);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_last_handle_no"));

    /* for websocket requests, no timeout */
    ctx=thread_ctx[*thrno+totnthreads];
    duk_push_global_stash(ctx);
    duk_push_pointer(ctx, (void*)base);
    duk_put_prop_string(ctx, -2, "elbase");
    duk_pop(ctx);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("thread_funcstash"));

    //a special reset for sql module so that new thread/fork will have settings reapplied
    //in rampart-sql.c:reset_tx_default
    duk_push_int(ctx, -2);
    duk_put_global_string(ctx, DUK_HIDDEN_SYMBOL("sql_last_handle_no"));


    thread_base[*thrno]=base;
    CTXUNLOCK;
}

/* just like duk_get_prop_string, except that the prop string compare is
   case insensitive */

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

const char *access_fn=NULL, *error_fn=NULL;

void reopen_logs(int sig)
{
    errno=0;
    if(access_fn)
    {
        fclose(access_fh);
        access_fh=fopen(access_fn,"a");
        if(!access_fh)
        {
            fprintf(stderr,"could not re-open %s for writing - %s\n", access_fn, strerror(errno));
            exit(1);
        }
    }

    if(error_fn)
    {
        fclose(error_fh);
        error_fh=fopen(error_fn,"a");
        if(!error_fh)
        {
            fprintf(stderr,"could not re-open %s for writing - %s\n", error_fn, strerror(errno));
            exit(1);
        }
    }
}

static inline void logging(duk_context *ctx, duk_idx_t ob_idx)
{
    if (duk_rp_GPS_icase(ctx, ob_idx, "log") && duk_get_boolean_default(ctx,-1,0) )
    {
        duk_rp_server_logging=1;
    }
    duk_pop(ctx);

    if (duk_rp_GPS_icase(ctx, ob_idx, "log") && duk_get_boolean_default(ctx,-1,0) )
    {
        duk_rp_server_logging=1;
    }
    duk_pop(ctx);

    if(duk_rp_server_logging)
    {
        int sig_reload=0;
        struct passwd  *pwd=NULL;

        if (duk_rp_GPS_icase(ctx, ob_idx, "user"))
        {
            if ( geteuid() == 0)
            {
                const char *user=REQUIRE_STRING(ctx, -1, "server.start: parameter \"user\" requires a string (username)");

                if(! (pwd = getpwnam(user)) )
                    RP_THROW(ctx, "server.start: error getting user '%s' in start()",user);
            }
        }
        duk_pop(ctx);

        if(duk_rp_GPS_icase(ctx, ob_idx, "accessLog") )
        {
            access_fn=REQUIRE_STRING(ctx,-1,  "server.start: parameter \"accessLog\" requires a string (filename)");
            access_fh=fopen(access_fn,"a");
            if(access_fh==NULL)
                RP_THROW(ctx, "server.start: error opening accessLog file '%s': %s", access_fn, strerror(errno));
            sig_reload=1;
            if(pwd)
            {
                if(chown(access_fn, pwd->pw_uid, -1))
                    RP_THROW(ctx, "server.start: could not chown access log - %s", strerror(errno));
            }
        }
        else
        {
            printf("no accessLog specified, logging to stdout\n");
        }
        duk_pop(ctx);

        if(duk_rp_GPS_icase(ctx, ob_idx, "errorLog") )
        {
            error_fn=REQUIRE_STRING(ctx,-1, "server.start: parameter \"errorLog\" requires a string (filename)");
            error_fh=fopen(error_fn,"a");
            if(error_fh==NULL)
                RP_THROW(ctx, "server.start: error opening errorLog file '%s': %s", error_fn, strerror(errno));
            sig_reload=1;
            if(pwd)
            {
                if(chown(error_fn, pwd->pw_uid, -1))
                    RP_THROW(ctx, "server.start: could not chown error log - %s", strerror(errno));
            }
        }
        else
        {
            printf("no errorLog specified, logging to stderr\n");
        }
        duk_pop(ctx);

        if(sig_reload)
        {
            struct sigaction sa = { {0} };

            sa.sa_handler = reopen_logs;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGUSR1, &sa, NULL);
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
     if(!sfn) RP_THROW(ctx, "Invalid ip/port specification\n");\
     char *p, *e;\
     char ibuf[INET6_ADDRSTRLEN]={0};\
     char *s=(char*)(sfn);\
     memset(ipany,0,INET6_ADDRSTRLEN + 5);\
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
         *(ipany + 5 + ( e - s ))='\0';\
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
     if(!strlen(ipany)) RP_THROW(ctx, "Error parsing ip address from %s", sfn);\
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



extern struct event_base *elbase;

DHS *main_dhs;
evhtp_t *main_htp;


static pid_t server_pid;

/* TODO: figure out how to clean up after a fork */
static void evexit(void *arg)
{
    free(main_dhs);

    if(getpid() != server_pid)
        return;

    evhtp_t *htp = arg;
    evhtp_free(htp);
    event_base_free(elbase);
}

/* The godzilla function.  Big, Scary and you just don't know what it's gonna
   step on next.
   TODO: turn this function from hell into something readable */

duk_ret_t duk_server_start(duk_context *ctx)
{
    DHS *dhs = new_dhs(ctx, -1);
    duk_idx_t ob_idx = -1;
    char *ipv6 = "ipv6:::1";
    char *ipv4 = "127.0.0.1";
    char ipany[INET6_ADDRSTRLEN + 5]={0};
    char *ip_addr = NULL;
    uint16_t port = 8088;
    int nthr=0, mapsort=1;
    evhtp_t *htp = NULL;
    evhtp_ssl_cfg_t *ssl_config = NULL;
    int i=0, confThreads = -1, mthread = 1, daemon=0;
    struct stat f_stat;
    duk_uarridx_t fpos =0;
    pid_t dpid=0;
    ctimeout.tv_sec = RP_TIME_T_FOREVER;
    ctimeout.tv_usec = 0;
    uint64_t max_body_size = 52428800;
    const char *cache_control="max-age=84600, public";

    main_dhs=dhs;
    server_pid = getpid();

    if(rampart_server_started)
        RP_THROW(ctx, "server.start - error- only one server per process may be running - use daemon:true to launch multiples");

    /*  an array at stack index 0 to hold all the callback function
        This gets it off of an otherwise growing and shrink stack and makes
        managment of their locations less error prone in the thread stacks.
    */
    duk_push_array(ctx);
    duk_insert(ctx,0);
    if (
        (duk_is_object(ctx, 1) && !duk_is_array(ctx, 1) && !duk_is_function(ctx, 1))
       )
            ob_idx = 1;
    else
        RP_THROW(ctx, "server.start - error - argument must be an object (options)");

    /* check if we are forking before doing any setup*/
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
            pid_t dpid2;

            close(child2par[0]);
            logging(ctx,ob_idx);
            if (setsid()==-1) {
                fprintf(error_fh, "server.start: failed to become a session leader while daemonising: %s",strerror(errno));
                exit(1);
            }
            /* need to double fork, and need to write back the pid of grandchild to parent. */
            dpid2=fork();

            if(dpid2==-1)
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
            server_pid=getpid();
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

            setproctitle("rampart server:%d", port);
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
    /* done with forking, now calloc */
    ssl_config = calloc(1, sizeof(evhtp_ssl_cfg_t));

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
                    fprintf(stderr,"\n******* WARNING: YOU ARE RUNNING SERVER AS ROOT. NOT A GOOD IDEA UNLESS YOU ARE DOING LIMITED TESTING. YOU'VE BEEN WARNED!!!! ********\n\n");
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

        /* cache-control header for fileserver */
        if (duk_rp_GPS_icase(ctx, ob_idx, "cacheControl"))
        {
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    cache_control=NULL;
            }
            else
                cache_control = REQUIRE_STRING(ctx, -1, "server.start: cacheControl requires a string");
        }
        duk_pop(ctx);

        /* fileserver compression */
        if (duk_rp_GPS_icase(ctx, ob_idx, "compressFiles"))
        {
            if(duk_is_boolean(ctx, -1))
            {
                if(!duk_get_boolean(ctx, -1))
                    compressibles=NULL;
            }
            else if (duk_is_array(ctx, -1))
            {
                size_t i=0, members = (size_t)duk_get_length(ctx, -1);

                compressibles=NULL;
                REMALLOC(compressibles, (members+1)*sizeof(char*));
                for (;i<members;i++)
                {
                    duk_get_prop_index(ctx, -1, (duk_uarridx_t)i);
                    compressibles[(int)i] = strdup(REQUIRE_STRING(ctx, -1, "server.start compressFiles - requires a Boolean or an array of strings (file extensions)"));
                    duk_pop(ctx);
                }
                compressibles[members]=NULL;
            }
            else
                RP_THROW(ctx, "server.start compressFiles - requires a Boolean or an array of strings (file extensions)");
        }
        else
        {   //turn off gzip compression by default
            compressibles=NULL;
        }
        duk_pop(ctx);

        /* compress scripts */
        if (duk_rp_GPS_icase(ctx, ob_idx, "compressScripts"))
        {
            compress_scripts = REQUIRE_BOOL(ctx, -1, "server.start compressScripts - requires a Boolean");
        }
        duk_pop(ctx);


        /* compress level */
        if (duk_rp_GPS_icase(ctx, ob_idx, "compressLevel"))
        {
            int cl = REQUIRE_INT(ctx, -1, "server.start compressLevel - requires an Integer 1-10");
            if (cl < 1 || cl > 10)
                RP_THROW(ctx, "server.start compressLevel - requires an Integer 1-10");
            compress_level=cl;
        }
        duk_pop(ctx);

        /* compress min size */
        if (duk_rp_GPS_icase(ctx, ob_idx, "compressMinSize"))
        {
            int cs = REQUIRE_INT(ctx, -1, "server.start compressMinSize - requires an Integer greater than 0");
            if (cs < 1)
                RP_THROW(ctx, "server.start compressMinSize - requires an Integer greater than 0");
            comp_min_size=cs;
        }
        duk_pop(ctx);

        /* max body size, for post and websockets */
        if (duk_rp_GPS_icase(ctx, ob_idx, "maxBodySize"))
        {
            max_body_size = (uint64_t) (REQUIRE_NUMBER(ctx, -1, "server.start: maxBodySize requires a number" ));
        }
        duk_pop(ctx);

        /* developer's mode throws errors to client as 500 Internal Server Error
           with the error message.  Otherwise a 404 is sent */
        if (duk_rp_GPS_icase(ctx, ob_idx, "developerMode"))
        {
            REQUIRE_BOOL(ctx, -1, "developerMode requires a boolean (true|false)");
            if(duk_get_boolean(ctx, -1))
                developer_mode=1;
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

    /* initialize two contexts for each thread
       the second one is used for websockets and will never
       be destroyed in a timeout.
    */
    REMALLOC(thread_ctx, (2 * totnthreads * sizeof(duk_context *)));
    for (i = 0; i <= totnthreads*2; i++)
    {
        duk_context *tctx;
        int tno = i<totnthreads ? i: i-totnthreads;
        if (i == totnthreads*2)
        {
            tctx = ctx;
        }
        else
        {
            thread_ctx[i] = duk_create_heap(NULL, NULL, NULL, NULL, duk_rp_fatal);
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
        duk_push_int(tctx, tno);
        duk_put_prop_string(tctx, -2, "thread_id");
        duk_push_int(tctx, totnthreads);
        duk_put_prop_string(tctx, -2, "total_threads");

        duk_put_global_string(tctx, "rampart");
    }

    //malloc here, set in initThread
    REMALLOC(thread_base, (totnthreads * sizeof(struct event_base *)));

    htp = evhtp_new(elbase, NULL);
    main_htp = htp;

    evhtp_set_max_keepalive_requests(htp, 128);
    evhtp_set_max_body_size(htp, max_body_size);

    /* testing for pure c benchmarking*
    evhtp_set_cb(htp, "/test", testcb, NULL);
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
                    for (i=0;i<totnthreads*2;i++)
                        copy_mod_func(ctx, thread_ctx[i], fpos);
                }
                else
                    copy_cb_func(dhs404, totnthreads*2);
                fpos++;
            }
            else
                RP_THROW(ctx, "server.start: Option for notFoundFunc must be a function or an object to load a module (e.g. {module:'mymodule'}");

        }
//        duk_pop(ctx);

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
                        duk_compile_string(ctx, DUK_COMPILE_FUNCTION, DIRLISTFUNC);
                        fname="defaultDirlist";
                        fprintf(access_fh, "mapping dir  list  to function   %-20s ->    function %s()\n", "Directory List", fname);
                    }
                    else
                    {
                        duk_pop(ctx);
                        goto dfunc_end;
                    }
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
                /* SO NO NEED TO POP OFF STACK */
                duk_put_prop_index(ctx, 0, fpos);

                dhs_dirlist = new_dhs(ctx, fpos);
                dhs_dirlist->module=mod;
                dhs_dirlist->timeout.tv_sec = dhs->timeout.tv_sec;
                dhs_dirlist->timeout.tv_usec = dhs->timeout.tv_usec;
                /* copy function to all the heaps/ctxs */
                if(mod)
                {
                    for (i=0;i<totnthreads*2;i++)
                        copy_mod_func(ctx, thread_ctx[i], fpos);
                }
                else
                    copy_cb_func(dhs_dirlist, totnthreads*2);

                fpos++;

            }
            else
                RP_THROW(ctx, "server.start: Option for directoryFunc must be a boolean, a function or an object to load a module (e.g. {module:'mymodule'}");
        }
        dfunc_end:

        if (duk_rp_GPS_icase(ctx, ob_idx, "mapSort"))
        {
            mapsort=REQUIRE_BOOL(ctx, -1, "server.start: parameter mapSort requires a boolean");
        }
        duk_pop(ctx);

        if (duk_rp_GPS_icase(ctx, ob_idx, "map"))
        {
            if (duk_is_object(ctx, -1) && !duk_is_function(ctx, -1) && !duk_is_array(ctx, -1))
            {
                int mlen=0,j=0;
                static char *pathtypes[]= { "exact", "glob ", "regex", "dir  " };

                if(mapsort)
                {
                    /* longer == more specific/match first */
                    //duk_push_string(ctx,"function(map) { return Object.keys(map).sort(function(a, b){  return b.length - a.length; }); }");
                    /* priority to exact, regex, path, then by length */
                    duk_push_string(ctx, "function(map){return Object.keys(map).sort(function(a, b){\n\
                                     a=a.replace(/^ws:/, ''); b=b.replace(/^ws:/, '');\n\
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
                /* Object.keys - sorted is on top of stack */
                duk_idx_t mapobj=duk_get_top_index(ctx);
                mlen=(int)duk_get_length(ctx, -1);
                for (j=0; j<mlen; j++)
                {
                    char *path, *fspath, *fs = (char *)NULL, *s = (char *)NULL;
                    int pathlen=0, isobject=0, isfsobject=0;

                    duk_get_prop_index(ctx,mapobj,j);
                    path = (char*)duk_get_string(ctx,-1);
                    duk_get_prop_string(ctx,-3,path);

                    if(duk_is_object(ctx, -1))
                    {
                        isobject=1;
                        if (!duk_is_function(ctx, -1) && duk_has_prop_string(ctx,-1, "path") )
                        {
                            isfsobject=1;
                        }
                    }

                    if (isobject && !isfsobject)
                    { /* map to function or module */
                        /* copy the function to array at stack pos 0 */
                        DHS *cb_dhs;
                        const char *fname;
                        char mod=MODULE_NONE;
                        int cbtype=0;
                        evhtp_callback_t  *req_callback;

                        //path = (char *)duk_get_string(ctx, -2);
                        REMALLOC(s, strlen(path) + 2);

                        if(*path == 'w' && *(path+1) =='s' && *(path+2) ==':' &&  *(path+3) =='/')
                        {
                            sprintf(s, "%s", path);
                            if  (*(s + strlen(s) - 1)=='/')
                                cbtype=3;
                        }
                        else if (*path == '~')
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
                                /* setting path length allows modules to return Objects with keys/urls
                                   greater than one directory.  Can always be used for modulePath: modules,
                                   but only for non glob/regex paths for module:
                                */
                                if(cbtype == 0 || cbtype == 3)
                                {
                                    if(*s=='w'  && *(s+1)=='s' && *(s+2)==':')
                                    {
                                        char *p=s+3;
                                        while(*p=='/') p++;
                                        pathlen=strlen(p)+1;
                                    }
                                    else
                                        pathlen=strlen(s);
                                }
                                goto copyfunction;

                            }
                            duk_pop(ctx);
                            if (duk_get_prop_string(ctx,-1, "modulePath") )
                            {
                                if( cbtype == 2 || cbtype==1 )
                                    RP_THROW(ctx, "server.start: parameter \"map:modulePath\" -- glob and regex not allowed in module path %s",s);

                                fname = duk_get_string(ctx, -1);
                                /* relative to script path */
                                if (*fname !='/')
                                {
                                    int l = strlen(RP_script_path) + strlen(fname) +2;
                                    char np[l];
                                    //printf("scriptpath='%s'\n",RP_script_path);
                                    strcpy(np, RP_script_path);
                                    strcat(np,"/");
                                    strcat(np,fname);
                                    duk_pop(ctx);
                                    duk_push_string(ctx, np);
                                    duk_put_prop_string(ctx, -2, "modulePath");
                                    duk_get_prop_string(ctx,-1, "modulePath");
                                    fname = duk_get_string(ctx, -1);
                                    //printf("should be '%s' at %d\n",np,fpos);
                                    //prettyprintstack(ctx);
                                }

                                /* must end with '/' */
                                if (fname[strlen(fname)-1] !='/')
                                {
                                    duk_push_string(ctx, "/");
                                    duk_concat(ctx, 2);
                                    duk_put_prop_string(ctx, -2, "modulePath");
                                    duk_get_prop_string(ctx,-1, "modulePath");
                                    fname = duk_get_string(ctx, -1);
                                    //printf("should be '%s' at %d\n",np,fpos);
                                    //prettyprintstack(ctx);
                                }

                                fprintf(access_fh, "mapping %s path to mod folder %-20s ->    module path:%s\n", pathtypes[cbtype], s, fname);
                                mod=MODULE_PATH;
                                duk_pop(ctx);

                                if(*s=='w'  && *(s+1)=='s' && *(s+2)==':')
                                {
                                    char *p=s+3;
                                    while(*p=='/') p++;
                                    pathlen=strlen(p)+1;
                                }
                                else
                                    pathlen=strlen(s);

                                goto copyfunction;
                            }
                            duk_pop(ctx);
                            RP_THROW(ctx, "server.start - map '%s' - Object must contain a key named 'module', 'modulePath' or 'path'", path);
                        }

                        duk_push_error_object(ctx, DUK_ERR_ERROR, "server.start: parameter \"map\" -- Option for path %s must be a function or an object to load a module (e.g. {module:'mymodule'}",s);
                        free(s);
                        (void)duk_throw(ctx);

                        copyfunction:
                        /* copy function into array at pos 0 in stack and into index fpos in array */
                        duk_put_prop_index(ctx,0,fpos);
                        cb_dhs = new_dhs(ctx, fpos);
                        add_exit_func(simplefree, cb_dhs);
                        cb_dhs->module=mod;
                        cb_dhs->pathlen=pathlen;
                        cb_dhs->timeout.tv_sec = dhs->timeout.tv_sec;
                        cb_dhs->timeout.tv_usec = dhs->timeout.tv_usec;
                        /* copy function to all the heaps/ctxs */
                        if(mod)
                        {
                            for (i=0;i<totnthreads*2;i++)
                                copy_mod_func(ctx, thread_ctx[i], fpos);
                        }
                        else
                            copy_cb_func(cb_dhs, totnthreads*2);

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
                        {
                            size_t slen = strlen(s);
                            //printf("setting exact path %s %d\n",s, cb_dhs->module);
                            evhtp_set_exact_cb(htp, s, http_callback, cb_dhs);

                            // if '*/index.html', then map '/' as well
                            if(slen > 10 && !strcmp ( s + (slen - 11), "/index.html") )
                            {
                                s[slen - 10] = '\0';
                                evhtp_set_exact_cb(htp, s, http_callback, cb_dhs);
                            }
                        }
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

                        REMALLOC(map, sizeof(DHMAP));
                        n_dhmaps++;
                        REMALLOC(all_dhmaps, n_dhmaps * sizeof(DHMAP*));
                        all_dhmaps[n_dhmaps-1] = map;
                        REMALLOC(s, strlen(path) + 4);

                        if (*path != '/')
                            sprintf(s, "/%s", path);
                        else
                            sprintf(s, "%s", path);

                        /* we need the libevhtp library to match /path as well as /path/
                           We'll take care of the redirect in fileserver()
                        */
                        if(s[strlen(s)-1] == '/')
                            s[strlen(s)-1] = '\0';

                        map->key = s;
                        map->dhs = dhs;
                        map->nheaders=0;
                        map->hkeys=NULL;
                        map->hvals=NULL;

                        if(cache_control)
                        {
                            map->nheaders=1;
                            REMALLOC(map->hkeys, sizeof(char*) *1 );
                            REMALLOC(map->hvals, sizeof(char*) *1 );
                            map->hkeys[0]="Cache-Control";
                            map->hvals[0]=(char *)cache_control;
                        }

                        if(isfsobject)
                        {
                            duk_get_prop_string(ctx, -1, "path");
                            fspath = (char *)REQUIRE_STRING(ctx, -1, "server.start: map '%s'- 'path' must be a String", path);
                            duk_pop(ctx);
                            if(duk_get_prop_string(ctx, -1, "headers")){
                                int len=0, ix;
                                REQUIRE_OBJECT(ctx, -1, "server.start: map '%s'- 'headers' must be an Object", path);
                                duk_enum(ctx,-1,DUK_ENUM_OWN_PROPERTIES_ONLY);
                                while (duk_next(ctx, -1, 1))
                                {
                                    if( cache_control && !strcasecmp(duk_get_string(ctx, -2), "cache-control") )
                                        map->nheaders=0; // overwrite the default cache-control
                                    len++;
                                    if (duk_is_array(ctx, -1))
                                    {
                                        len += (int)duk_get_length(ctx, -1) - 1;
                                    }
                                    duk_pop_2(ctx);
                                }
                                duk_pop(ctx);

                                ix=map->nheaders;
                                map->nheaders+=len;
                                REMALLOC(map->hkeys, sizeof(char*) * map->nheaders );
                                REMALLOC(map->hvals, sizeof(char*) * map->nheaders );
                                /* now do again, inserting values */
                                duk_enum(ctx,-1,DUK_ENUM_OWN_PROPERTIES_ONLY);
                                while (duk_next(ctx, -1, 1))
                                {
                                    if(!duk_is_array(ctx, -1))
                                    {
                                        map->hkeys[ix] = (char*) duk_get_string(ctx, -2);
                                        if(duk_is_string(ctx, -1))
                                            map->hvals[ix] = (char*) duk_get_string(ctx, -1);
                                        else
                                        {
                                            map->hvals[ix] = (char*) duk_json_encode(ctx, -1);
                                            /* save it so duktape doesn't free it */
                                            duk_dup(ctx, -1);
                                            duk_put_prop_string(ctx, -5, map->hkeys[ix]);
                                        }
                                        ix++;
                                    }
                                    else
                                    {
                                        duk_uarridx_t aidx=0,
                                            len = (duk_uarridx_t) duk_get_length(ctx, -1);

                                        for (;aidx<len;aidx++)
                                        {
                                            map->hkeys[ix] = (char*) duk_get_string(ctx, -2);
                                            duk_get_prop_index(ctx, -1, aidx);
                                            if (duk_is_string(ctx, -1))
                                            {
                                                map->hvals[ix] = (char*) duk_get_string(ctx, -1);
                                                duk_pop(ctx);
                                            }
                                            else
                                            {
                                                map->hvals[ix] = (char*) duk_json_encode(ctx, -1);
                                                /* save it so duktape doesn't free it */
                                                duk_put_prop_index(ctx, -2, aidx);
                                            }
                                            ix++;
                                        }
                                    }
                                    duk_pop_2(ctx);
                                }
                                duk_pop(ctx);
                            }
                            duk_pop(ctx);
                        }
                        else
                        {
                            fspath = (char *)REQUIRE_STRING(ctx, -1, "server.start: map '%s'- value must be a String, Object, Function", path);
                        }

                        if (stat(fspath, &sb) == -1)
                        {
                            RP_THROW(ctx, "server.start: parameter \"map\" -- Couldn't find fileserver path '%s'",fspath);
                        }
                        mode = sb.st_mode & S_IFMT;

                        if (mode != S_IFDIR)
                            RP_THROW(ctx, "server.start: parameter \"map\" -- Fileserver path '%s' requires a directory",fspath);

                        REMALLOC(fs, strlen(fspath) + 2)

                        if (*(fspath + strlen(fspath) - 1) != '/')
                        {
                            sprintf(fs, "%s/", fspath);
                        }
                        else
                            sprintf(fs, "%s", fspath);

                        map->val = fs;

                        if(!*s)
                        {
                            fprintf(access_fh, "mapping filesystem folder        %-20s ->    %s\n", "/", fs);
                            evhtp_set_cb(htp, "/", fileserver, map);
                        }
                        else
                        {
                            fprintf(access_fh, "mapping filesystem folder        %-20s ->    %s\n", s, fs);
                            evhtp_set_cb(htp, s, fileserver, map);
                        }
                        duk_pop_2(ctx);
                    }
                }
            }
            else
                RP_THROW(ctx, "server.start: value of parameter \"map\" requires an object");
        }
        else
            RP_THROW(ctx, "server.start: No \"map\" object provided, nothing to do");
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

    //done with options, get rid of ob_idx
    duk_remove(ctx, ob_idx);

    if(ctimeout.tv_sec != RP_TIME_T_FOREVER)
        evhtp_set_timeouts(htp, &ctimeout, &ctimeout);
    else
        evhtp_set_timeouts(htp, NULL, NULL);

    evhtp_set_pre_accept_cb(htp, pre_accept_callback, NULL);

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
        duk_push_global_stash(ctx);
        duk_pull(ctx,0);
        duk_put_prop_string(ctx, -2, "funcstash");
        // if forking as a daemon, run loop here in child and don't continue with rest of script
        // script will continue in parent process
        add_exit_func(evexit, htp);
        event_base_loop(elbase, 0);
    }
    else
        rampart_server_started=1;

    duk_push_global_stash(ctx);
    duk_pull(ctx,0);
    duk_put_prop_string(ctx, -2, "funcstash");
    duk_push_int(ctx, (int) getpid() );
    //add_exit_func(evexit, htp);
    return 1;
}

static const duk_function_list_entry utils_funcs[] = {
    {"start", duk_server_start, 1 /*nargs*/},
    {NULL, NULL, 0}};

static const duk_number_list_entry utils_consts[] = {
    {NULL, 0.0}};

duk_ret_t duk_open_module(duk_context *ctx)
{
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, utils_funcs);
    duk_put_number_list(ctx, -1, utils_consts);

    return 1;
}
