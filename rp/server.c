#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <sys/syscall.h> for syscall(SYS_gettid)
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "duktape.h"
#include "duk_cmdline.h"
#include "evhtp/evhtp.h"
#include "rp.h"
#include "mime.h"


#ifndef SINGLETHREADED
static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static int threadno=0;
#endif

duk_context **thread_ctx=NULL, *main_ctx;

int totnthreads=0;

#define printstack(ctx) duk_push_context_dump((ctx));\
printf("%s\n", duk_to_string((ctx), -1));\
duk_pop((ctx));

#define printenum(ctx,idx)  duk_enum((ctx),(idx),DUK_ENUM_INCLUDE_NONENUMERABLE|DUK_ENUM_INCLUDE_HIDDEN|DUK_ENUM_INCLUDE_SYMBOLS);\
    while(duk_next((ctx),-1,1)){\
      printf("%s -> %s\n",duk_get_string((ctx),-2),duk_safe_to_string((ctx),-1));\
      duk_pop_2((ctx));\
    }\
    duk_pop((ctx));

#define printat(ctx,idx) duk_dup(ctx,idx);printf("at %d: %s\n",(int)(idx),duk_safe_to_string((ctx),-1));duk_pop((ctx));


/* **************************************************************************
   This url(en|de)code is public domain from https://www.geekhideout.com/urlcode.shtml 
   ************************************************************************** */

/* Converts a hex character to its integer value */

static char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
static char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
static char *url_encode(char *str, int len) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  
  if(len<0)len=strlen(str);
  
  while (len) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') 
      *pbuf++ = *pstr;
    else if (*pstr == ' ') 
      *pbuf++ = '+';
    else 
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
    len--;
  }
  *pbuf = '\0';
  return buf;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
static char *url_decode(char *str, int len) {
  char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
  
  if(len<0)len=strlen(str);

  while (len) {
    if (*pstr == '%' && len>2) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
        len-=2;
    } else if (*pstr == '+') { 
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
    len--;
  }
  *pbuf = '\0';
  return buf;
}

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

#define DHS struct duk_http_s
DHS {
  duk_idx_t func_idx;
  duk_context *ctx;  
  evhtp_request_t *req;
  void *bytecode;
  size_t bytecode_sz;
};

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
//    evhtp_request_set_keepalive(req, 1);
//    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));

}

#define putval(key,val) if((char *)(val)!=(char*)NULL){duk_push_string(ctx,(const char *)(val));duk_put_prop_string(ctx,-2,(key));} 

int putkvs(evhtp_kv_t *kv, void *arg)
{
//    DHS *dhs=(DHS*)arg;
    char *v, *k;
    duk_context *ctx=(duk_context*)arg;

    v=url_decode(kv->val,(int)kv->vlen);
    k=url_decode(kv->key,(int)kv->klen);
    putval(k,v);

    free(k);
    free(v);

    return 0;
}

static int putheaders(evhtp_kv_t *kv, void *arg)
{
    char *v, *k;
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

void push_req_vars(DHS *dhs)
{
    duk_context *ctx=dhs->ctx;
    evhtp_path_t *path=dhs->req->uri->path;
    evhtp_authority_t *auth=dhs->req->uri->authority;
    int method = evhtp_request_get_method(dhs->req);
    evhtp_connection_t *conn = evhtp_request_get_connection(dhs->req);
    void *sa=(void *)conn->saddr;
    char address[INET6_ADDRSTRLEN], *q, *cl;
    int port=0,i,l;

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
                } /* else ????? */
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
    char *v, *k;
    char **range=arg;
    
    //printf("%.*s: %.*s\n",(int)kv->klen, kv->key, (int)kv->vlen,kv->val);
    
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
            if(*eptr!='\0');
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
<p><b>%s</b></p>\n\
</body>\n\
</html>",fn);
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

/* free the stolen duk buffer after evbuffer is done with it */
static void refcb(const void *data, size_t datalen, void *val)
{
    duk_context *ctx=val;
    duk_free(ctx,(void *)data);
    //duk_destroy_heap(ctx);
    /* remove everything but our functions */
//printf("removing %d (%d - %d)\n", (int)(duk_get_top_index(ctx)-top_idx),(int)duk_get_top(ctx),(int)top_idx);
//    duk_pop_n(ctx,duk_get_top_index(ctx)-top_idx);
// aparently it is autoremoved
}

static void sendbuf(DHS *dhs)
{
    size_t sz;
    const char *s;
    
    /* turn whatever the return val is into a buffer, 
       then steal the allocation and pass it to evbuffer
       without copying.  duktape will not free it
       when we duk_pop().
    */
    s=duk_get_string_default(dhs->ctx,-1,"  ");
    if(*s=='\\' && *(s+1)=='@')
    {
        s++;
        duk_push_string(dhs->ctx,s);
        duk_replace(dhs->ctx,-2);
    }
    duk_to_dynamic_buffer(dhs->ctx,-1,NULL);
    s=duk_steal_buffer(dhs->ctx, -1, &sz);

    /* add buffer to response, and specify callback to free s when done */
    evbuffer_add_reference(dhs->req->buffer_out,s,sz,refcb,dhs->ctx);
}

static evhtp_res sendobj(DHS *dhs)
{

    RP_MTYPES m;
    RP_MTYPES *mres, *m_p=&m;
    int gotdata=0;
    duk_context *ctx=dhs->ctx;
    evhtp_res res=200;

    if(duk_get_prop_string(ctx, -1, "headers"))
    {
        if (duk_is_object(ctx,-1)  && !duk_is_array(ctx,-1) && !duk_is_function(ctx,-1))
        {
            duk_enum(ctx, -1, DUK_ENUM_SORT_ARRAY_INDICES);
            while (duk_next(ctx, -1 , 1 ))
            {
// printf("got header %s=%s\n",duk_to_string(ctx,-2),duk_to_string(ctx,-1));
                evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new(duk_to_string(ctx,-2),duk_to_string(ctx,-1), 1, 1));            
                duk_pop_2(ctx);
            }
            duk_pop_2(ctx); /* pop enum and headers obj */
            duk_del_prop_string(ctx, -1, "headers");
        }
        else
        {
            duk_pop(ctx); /* get rid of 'headers' non object */
            duk_del_prop_string(ctx, -1, "headers");
            printf("Headers in reply must be set to an object");
            
        }
    }
    else
    {
        duk_pop(ctx); /* get rid of undefined from duk_get_prop_string(ctx, -1, "headers") */
    }
    

    if(duk_get_prop_string(ctx, -1, "status"))
    {
        if(!duk_is_number(ctx,-1) )
        {
            printf("status must be a number\n");
        }
        else
        {
            /* don't allow 0 */
            res=(evhtp_res)duk_get_number(ctx,-1);
            if(!res) res=200;
        }
        duk_del_prop_string(ctx, -2, "status");
    }
    duk_pop(ctx);

    duk_enum(ctx, -1, DUK_ENUM_SORT_ARRAY_INDICES);
    while (duk_next(ctx, -1 , 1 ))
    {
        if(gotdata) goto opterr;

        m.ext=(char*)duk_to_string(ctx,-2);
        mres=bsearch(m_p,rp_mimetypes,nRpMtypes,sizeof(RP_MTYPES),compare_mtypes);
        if(mres) 
        {
            const char *d;
            gotdata=1;
            evhtp_headers_add_header(dhs->req->headers_out, evhtp_header_new("Content-Type", mres->mime, 0, 0));
//printf("got data at %s\n",m.ext);
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

        opterr:
            printf("unknown option '%s', or bad option, or data already set from '%s'\n",duk_to_string(ctx,-2),m.ext);
            duk_pop_2(ctx);
    }
    duk_pop(ctx);
    return(res);
}

static evthr_t *get_request_thr(evhtp_request_t * request) 
{
    evhtp_connection_t * htpconn;
    evthr_t            * thread;

    htpconn = evhtp_request_get_connection(request);
    thread  = htpconn->thread;

    return thread;
}

static void
http_callback(evhtp_request_t * req, void * arg)
{
    DHS   *dhs = arg, newdhs;
    FILE  * file_desc;
    evhtp_res res=200;
    int eno;
    void *buf;
#ifdef SINGLETHREADED
    duk_context *new_ctx = thread_ctx[0];   
#else
    evthr_t *thread= get_request_thr(req);
    duk_context *new_ctx = (duk_context*) evthr_get_aux(thread);
#endif
    

    /* ****************************
      setup duk function callback 
       **************************** */

    newdhs.ctx=new_ctx;
    newdhs.req=req;
    newdhs.func_idx=dhs->func_idx;
    dhs=&newdhs;

    /* copy function ref to top of stack */
    duk_dup(dhs->ctx,dhs->func_idx);
    /* push an empty object */
    duk_push_object(dhs->ctx);
    /* populate object with details from client */
    push_req_vars(dhs);

    /* execute function "myfunc({object});" */
    if( (eno=duk_pcall(dhs->ctx,1)) ) 
    {
        if(duk_is_error(dhs->ctx,-1)){
            duk_get_prop_string(dhs->ctx, -1, "stack");
            evhtp_headers_add_header(req->headers_out, evhtp_header_new("Content-Type", "text/html", 0, 0));
            char msg[]="<html><head><title>500 Internal Server Error</title></head><body><h1>Internal Server Error</h1><p><pre>%s</pre></p></body></html>";
            evbuffer_add_printf(req->buffer_out,msg,duk_safe_to_string(dhs->ctx, -1));
            evhtp_send_reply(req, 500);
            printf("%s\n", duk_safe_to_string(dhs->ctx, -1));
            duk_pop(dhs->ctx);
            return;
        }
        else
            printf("error in function: %s\n",duk_safe_to_string(dhs->ctx, -1));

    }

    /* stack has return value from duk function call */    

    /* set some headers */
    setheaders(req);
    /* don't accept functions or arrays */
    if(duk_is_function(dhs->ctx,-1) || duk_is_array(dhs->ctx,-1) ){
        duk_push_string(dhs->ctx,"Return value cannot be an array or a function");
        duk_throw(dhs->ctx);
    }
    /* object has reply data and options in it */
    if(duk_is_object(dhs->ctx,-1))
    {
        
        res=sendobj(dhs);
    }
    else /* send string or buffer to client */
        sendbuf(dhs);
    
    if(res)evhtp_send_reply(req, res);

    duk_pop(dhs->ctx);
    /* logging goes here ? */
}

int bind_sock_port(evhtp_t *htp, const char *ip, uint16_t port, int backlog) 
{
    struct sockaddr_storage sin;
    socklen_t len = sizeof(struct sockaddr_storage);
    
    if( evhtp_bind_socket(htp, ip, port, backlog) != 0 )
        return(-1);
    
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
            char buf[INET6_ADDRSTRLEN];
            
            /* compress our ipv6 address to compare */
            if (inet_pton(AF_INET6, &ip[5], a) == 1){
                char buf[INET6_ADDRSTRLEN];
                if (inet_ntop(AF_INET6, a, buf, INET6_ADDRSTRLEN) == NULL) {
                   return(-1);
                }
                /* compare given compressed *ip with what we are actually bound to */
                if(strcmp(buf,addr)!=0)
                    return(-1);
            }
            else return(-1);
        }
        else
        {
            /*  ipv4 doesn't get compressed, it fails above if something like 127.0.0.001 */
            if(strcmp(ip,addr)!=0)
                return(-1);
        }
        printf("binding to %s port %d\n", addr, port);
    }

    return (0);
}

void testcb(evhtp_request_t * req, void * arg)
{
    char rep[]="hello world";
    /*  
    TODO: figure out keepalive problem
    evhtp_request_set_keepalive(req, 1);
    evhtp_headers_add_header(req->headers_out, evhtp_header_new("Connection","keep-alive", 0, 0));
    */
    evbuffer_add_printf(req->buffer_out,"%s",rep);
    evhtp_send_reply(req, EVHTP_RES_OK);
}

void exitcb(evhtp_request_t * req, void * arg)
{
    duk_context *ctx=arg;
    char rep[]="exit";
    int i=0;
    
    evbuffer_add_reference(req->buffer_out,rep,strlen(rep),NULL,NULL);
    evhtp_send_reply(req, EVHTP_RES_OK);
    for(;i<totnthreads;i++) 
    {
        duk_rp_sql_close(thread_ctx[i]);
        duk_destroy_heap(thread_ctx[i]);
    }
    duk_rp_sql_close(main_ctx);
    duk_destroy_heap(main_ctx);
    exit(0);
}

#define DHSA struct dhs_array
DHSA {
    int n;
    DHS **dhs_pa;
};

DHSA dhsa={0,NULL};

static DHS *newdhs(duk_context *ctx, int idx)
{
    DHS *dhs=NULL;
    DUKREMALLOC(ctx, dhs, sizeof(DHS));
    dhs->func_idx=idx;
    dhs->ctx=ctx;
    dhs->req=NULL;
    dhs->bytecode=NULL;
    dhs->bytecode_sz=0;
    dhsa.n++;
    DUKREMALLOC(ctx, dhsa.dhs_pa, sizeof(DHS*)*dhsa.n );
    dhsa.dhs_pa[dhsa.n-1]=dhs;
        
    return(dhs);
}

static void copy_func(DHS *dhs, int nt) {
    void *bc_ptr;
    duk_size_t bc_len;
    int i=0;

    duk_dup(dhs->ctx,dhs->func_idx);
    duk_dump_function(dhs->ctx);
    bc_ptr = duk_require_buffer_data(dhs->ctx, -1, &bc_len);
    /* load function into each of the thread contexts and record position */
    for(i=0;i<nt;i++){
        duk_context *ctx=thread_ctx[i];
        void *buf = duk_push_fixed_buffer(ctx, bc_len);
        memcpy(buf, (const void *) bc_ptr, bc_len);
        duk_load_function(ctx);
        if(!i)
            dhs->func_idx=duk_get_top_index(ctx);
    }
    duk_pop(dhs->ctx);
}

#ifndef SINGLETHREADED
/* this never happens?? */
void endThread ()
{
}

void initThread (evhtp_t *htp, evthr_t *thr, void *arg)
{
    evthr_set_aux(thr, thread_ctx[threadno++]);
}
#endif


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
    static int i=0;
    duk_dump_function(ctx); //dump function to bytecode
    bc_ptr = duk_require_buffer_data(ctx, -1, &bc_len);//get pointer to bytecode
    buf = duk_push_fixed_buffer(tctx, bc_len); //make a buffer in thread ctx
    memcpy(buf, (const void *) bc_ptr, bc_len); //copy bytecode to new buffer
    duk_load_function(tctx); //make copied bytecode a function
    duk_push_global_stash(tctx); //get global stash and save function there.
    duk_insert(tctx,-2);
    duk_put_prop_string(tctx,-2,name);
    duk_pop(tctx);

    duk_push_c_function(tctx,duk_rp_bytefunc,DUK_VARARGS); //put our wrapper function on stack
    duk_push_string(tctx,name);  // add desired function name (as set in global stash above)
    duk_put_prop_string(tctx,-2,"fname");
    duk_put_prop_string(tctx,-2,name);  //put it in object right above the call of this function
}

static void copy_prim(duk_context *ctx,duk_context *tctx)
{
    const char *name=duk_get_string(ctx,-2);

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

/* ctx object and tctx object should be at top of stack */
static void copy_obj(duk_context *ctx,duk_context *tctx)
{
    const char *s;
    duk_enum(ctx,-1,0);
    while(duk_next(ctx,-1,1)){

        if(duk_is_ecmascript_function(ctx,-1))
        {
            copy_bc_func(ctx,tctx);
            duk_pop_2(ctx);
        }

        else if (duk_check_type_mask(ctx, -1, DUK_TYPE_MASK_STRING|DUK_TYPE_MASK_NUMBER|DUK_TYPE_MASK_BOOLEAN|DUK_TYPE_MASK_NULL|DUK_TYPE_MASK_UNDEFINED)) 
        {
            copy_prim(ctx,tctx);
            duk_pop_2(ctx);
        }

        else if (duk_is_object(ctx,-1) && !duk_is_function(ctx,-1) && !duk_is_c_function(ctx,-1) )
        {

            s=duk_get_string(ctx,-2);
            if ( ! duk_has_prop_string( tctx,-1,s ) 
                 &&
                 strcmp(s, "console" )
                 &&
                 strcmp(s, "performance" )
               )
            {
                duk_push_object(tctx);
                copy_obj(ctx,tctx);

                duk_put_prop_string(tctx, -2, duk_get_string(ctx,-2) );
                duk_pop_2(ctx);
            }
            else
            {
                duk_pop_2(ctx);
            }
        }
        else 
        {
            duk_pop_2(ctx);
        }
    }
    duk_pop(ctx);//enum    
}

static void copy_all(duk_context *ctx,duk_context *tctx)
{
    duk_push_global_object(ctx);
    duk_push_global_object(tctx);
    copy_obj(ctx,tctx);
    duk_pop(ctx);
    duk_pop_2(tctx);
}

duk_ret_t duk_server_start(duk_context *ctx)
{
    /* TODO: make it so it can bind to any number of arbitary ipv4 or ipv6 addresses */
    evhtp_t           * htp4;
    evhtp_t           * htp6;
    evhtp_callback_t  * gencb;
    struct event_base * evbase;
    const char *fn;
    int i=0;
    DHS *dhs=newdhs(ctx,-1);
    duk_idx_t ob_idx=-1,script_idx;
    char ipv6[INET6_ADDRSTRLEN+5]="ipv6:::1";
    char ipv4[INET_ADDRSTRLEN]="127.0.0.1";
    uint16_t port=8080, ipv6port=8080;
    void *tptr;
    int nthr;
    duk_size_t sb_sz;

    main_ctx=ctx;

#ifndef SINGLETHREADED
    if(nthreads > 0) 
        nthr=nthreads;
     else
        nthr=sysconf(_SC_NPROCESSORS_ONLN);
    totnthreads=nthr*2;

    printf("HTTP server initializing with %d threads per server, %d total\n", nthr,totnthreads);
#else
    totnthreads=1;
#endif
    REMALLOC( thread_ctx, (totnthreads*sizeof(duk_context*)) );
//printf("%d?=%d, %lu\n",(int)getpid(),(int)syscall(SYS_gettid),(unsigned long int)pthread_self());


    /* initialize a context for each thread */
    for(i=0;i<totnthreads;i++){
        void *buf;

        thread_ctx[i]=NULL;
        REMALLOC( thread_ctx[i], sizeof(duk_context*) );

        thread_ctx[i] = duk_create_heap_default();
        /* do all the normal startup done in duk_cmdline but for each thread */
        duk_init_userfunc(thread_ctx[i]);

        copy_all(ctx,thread_ctx[i]);
    }

    i=0;
    if(
        (duk_is_object(ctx,i)  && !duk_is_array(ctx,i) && !duk_is_function(ctx,i))
        || 
        (duk_is_object(ctx,++i)&& !duk_is_array(ctx,i) && !duk_is_function(ctx,i)) 
    )
        ob_idx=i;
      
     i=!i; /* get the other option. There's only 2 */

     if(duk_is_function(ctx,i))
         dhs->func_idx=i;

    evbase = event_base_new();

    htp4 = evhtp_new(evbase, NULL);
    htp6 = evhtp_new(evbase, NULL);

    /* testing */
    evhtp_set_cb(htp4, "/test", testcb,NULL);
    evhtp_set_cb(htp6, "/test", testcb,NULL);
    /* testing, quick semi clean exit */
    evhtp_set_cb(htp4, "/exit", exitcb,ctx);
    evhtp_set_cb(htp6, "/exit", exitcb,ctx);

    if(ob_idx!=-1) {
        const char *s;

        /* ip addr */
        if(duk_get_prop_string(ctx,ob_idx,"ip"))
        {
            s=duk_get_string(ctx,-1);
            strcpy(ipv4,s);
        }
        duk_pop(ctx);

        /* ipv6 addr */
        if(duk_get_prop_string(ctx,ob_idx,"ipv6"))
        {
            s=duk_get_string(ctx,-1);
            strcpy(&ipv6[5],s);
        }
        duk_pop(ctx);

        /* port */
        if(duk_get_prop_string(ctx,ob_idx,"port"))
        {
            port=duk_get_int(ctx,-1);
        }
        duk_pop(ctx);

        /* port ipv6*/
        if(duk_get_prop_string(ctx,ob_idx,"ipv6port"))
        {
            ipv6port=(uint16_t)duk_get_int(ctx,-1);
        }
        else ipv6port=port;
        duk_pop(ctx);

        /* file system and function mapping */
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
                        //int tidx=0;
                        path=(char*)duk_to_string(ctx,-2);
                        DUKREMALLOC(ctx,s,strlen(path)+2);

                        if(*path!='/' && *path!='*')
                            sprintf(s,"/%s",path);
                        else 
                            sprintf(s,"%s",path);

                        duk_idx_t fi=duk_get_top(ctx) - 4;
                        duk_insert(ctx,fi);
                        cb_dhs=newdhs(ctx,fi);
                        /* copy function to all the heaps/ctxs */
                        copy_func(cb_dhs,totnthreads);
                        printf("setting %-20s ->    function\n",s);
                        evhtp_set_glob_cb(htp4,s,http_callback,cb_dhs);
                        evhtp_set_glob_cb(htp6,s,http_callback,cb_dhs);
                        //tidx=duk_get_top_index(cb_dhs->ctx);
                        //if(tidx>top_idx) top_idx=tidx;
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
                        printf ("setting %-20s ->    %s\n",s,fs);
                        map->val=fs;
                        evhtp_set_glob_cb(htp4,s,fileserver,map);
                        evhtp_set_glob_cb(htp6,s,fileserver,map);
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
    



    //evhtp_alloc_assert(htp);

    //gencb=evhtp_set_glob_cb(htp4, "/*", http_callback, &dhs);
    //evhtp_callback_set_hook(gencb,evhtp_hook_on_request_fini, onfinish, ctx);

    if( dhs->func_idx !=-1)
    {
        copy_func(dhs,totnthreads);
        evhtp_set_gencb(htp4, http_callback, dhs);
        evhtp_set_gencb(htp6, http_callback, dhs);
    }

    if(bind_sock_port(htp4,ipv4,port,2048))
    {
        duk_push_sprintf(ctx,"could not bind to %s port %d",ipv4,port);
        duk_throw(ctx);
    }
    /* TODO: don't fail on lack of ipv6 on the system (are there any left?) */
    if (bind_sock_port(htp6,ipv6,ipv6port,2048))
    {
        duk_push_sprintf(ctx,"could not bind to %s, %d",ipv6,ipv6port);
        duk_throw(ctx);
    }
#ifndef SINGLETHREADED
    evhtp_use_threads_wexit(htp4, initThread, NULL, nthr, NULL);
    evhtp_use_threads_wexit(htp6, initThread, NULL, nthr, NULL);
#else
    printf("in single threaded mode\n");
#endif
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
