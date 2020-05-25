#include <stdio.h>
#include <stdarg.h>  /* va_list etc */
#include <stddef.h>  /* size_t */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <curl/curl.h>
#include "duktape.h"
#include "rp.h"


#define RETTXT struct curl_resStr
RETTXT {
  char *text;
  size_t size;
};

#define CSOS struct curl_setopts_struct
#define MAXSLISTS 16
CSOS {
  long httpauth;       /* flags we need to save for successive calls*/
  long proxyauth;      /* ditto */
  long postredir;      /* ditto */
  long ssloptions;     /* ditto */
  long proxyssloptions;/* ditto */
  char *url;           /* url we will possibly append and will always need to free later */
  char *postdata;      /* postdata we will will need to free later, if used */
  curl_mime *mime;     /* from postform. Needs to be freed after curl_easy_perform() */
  struct curl_slist *slists[MAXSLISTS]; /* keep track of slists to free later. currently there are only 9 options with slist */
  int nslists;         /* number of lists actually used */
  int headerlist;      /* the index of the slists[] above that contains headers.  Headers set at the end so we can continually add during options */
};

#define CURLREQ struct curl_req

CURLREQ {
  CURL *curl;
  RETTXT body;
  RETTXT header;
  CSOS sopts;
  char *errbuf;
  char isclone;
};


/* return s+adds  s must be a malloced string or ->NULL*/
char *strcatdup(char *s, char *adds) 
{
  int freeadds=0,sl,al;
  if ( (adds==(char *)NULL) || (*adds=='\0') ) {
    if (s==(char *)NULL) s=strdup("");
    return(s);
  }

  if(s==(char *)NULL)
    sl=0;
  else
    sl=strlen(s);

  /* if its the same string or a substring */
  if (adds>=s && adds<(s+sl)) { adds=strdup(adds);freeadds=1; }

  if (s!=(char *)NULL) 
  {
    al=strlen(adds);
    REMALLOC(s,al+sl+1);
    strcpy(s+sl,adds);
  }
  else 
    s=strdup(adds);
  
  if (freeadds) free(adds);

  return(s);
}

/* return s+c+adds  s must be a malloced string or ->NULL*/
char *strjoin(char *s, char *adds, char c) 
{
  int freeadds=0,sl,al;
  if ( (adds==(char *)NULL) || (*adds=='\0') ) {
    if (s==(char *)NULL) s=strdup("");
    return(s);
  }

  if(s==(char *)NULL)
    sl=0;
  else
    sl=strlen(s);

  /* if its the same string or a substring */
  if (adds>=s && adds<(s+sl)) { adds=strdup(adds);freeadds=1; }

  if (s!=(char *)NULL) 
  {
    al=strlen(adds);
    REMALLOC(s,al+sl+2);
    *(s+sl)=c;
    strcpy(s+sl+1,adds);
  }
  else 
  {
    REMALLOC(s,al+2);
    *s=c;
    strcpy(s+1,adds);
  }  
  if (freeadds) free(adds);

  return(s);
}

/* copy this.errors to object at idx */
void duk_curl_copy_errors(duk_context *ctx,duk_idx_t idx)
{
  duk_push_this(ctx); /* [.., this] */
  duk_get_prop_string(ctx,-1,"errors");
  if (duk_is_array(ctx,-1)) {
    /* [...,this,array] */ 
    duk_put_prop_string(ctx,idx-2,"errMsg"); /* [...,this] */
  }
    /* [...,this,notAnArray] */
  else duk_pop(ctx); /* get rid of non array from duk_get_prop_string above */

  duk_pop(ctx); /* pop [..,this] */
}


/* push a blank return object on top of stack with 
  ok: false
  and 
  errMsg: [array of, collected, errors]
*/
void duk_curl_ret_blank(duk_context *ctx) {
  
  duk_push_object(ctx);
  duk_push_string(ctx,"");
  duk_put_prop_string(ctx,-2,"statusText");
  duk_push_int(ctx,-1);
  duk_put_prop_string(ctx,-2,"status");
  duk_push_string(ctx,"");
  duk_put_prop_string(ctx,-2,"text");

  /* copy errors to return.errMsg */
  duk_curl_copy_errors(ctx,-1) ;

}


void duk_curl_push_err(duk_context *ctx) 
{
  
  duk_idx_t top=duk_get_top(ctx)-1;  /* where our error string is at */

//printf("adding error %s\n",duk_to_string(ctx,-1));

  duk_push_this(ctx); /* [errmsg(idx=top), this ] */ 

  if(duk_get_prop_string(ctx,-1,"errors")  && duk_is_array(ctx,-1) )
  {
//printf("array exists\n");
    /* [errmsg(idx=top), this,array ] */
    duk_size_t l=duk_get_length(ctx,-1);
    duk_dup(ctx,top); /* [errmsg(idx=top), this,array,errmsg(dup) ] */
    duk_put_prop_index(ctx,-2,l); /* [errmsg(idx=top), this,array] */
    duk_put_prop_string(ctx,-2,"errors"); /* array popped [errmsg(idx=top), this] */
  }
  else
  {
//printf("creating array\n");
    duk_pop(ctx); /* undefined from get_prop_string */
    duk_push_array(ctx);
    duk_dup(ctx,top);
    duk_put_prop_index(ctx,-2,0);
    duk_put_prop_string(ctx,-2,"errors"); /* put array into this.errors */
    /* array is gone, have [errmsg(idx=top), this] */    
  }
  duk_pop_2(ctx); /* pop  [errmsg(idx=top), this] */
}


/* **************************************************************************
   curl_easy_escape requires a new curl object. That appears to be overkill.
   This one is public domain from https://www.geekhideout.com/urlcode.shtml 
   ************************************************************************** */

/* Converts a hex character to its integer value */

char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') 
      *pbuf++ = *pstr;
    else if (*pstr == ' ') 
      *pbuf++ = '+';
    else 
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_decode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') { 
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}

duk_ret_t duk_curl_encode(duk_context *ctx) {
  char *s=(char*)duk_to_string(ctx,0);
  s=url_encode(s);
  duk_push_string(ctx,s);
  free(s);
  return(1);
}

duk_ret_t duk_curl_decode(duk_context *ctx) {
  char *s=(char*)duk_to_string(ctx,0);
  s=url_decode(s);
  duk_push_string(ctx,s);
  free(s);
  return(1);
}

void duk_curl_check_global(duk_context *ctx) 
{
  duk_push_this(ctx);
  duk_get_prop_string(ctx,-1,"inited");
  if(! duk_get_boolean(ctx,-1) ) 
  {
    curl_global_init(CURL_GLOBAL_DEFAULT);  
    duk_push_boolean(ctx,1);
    duk_put_prop_string(ctx,-3,"inited");
  }
  duk_pop_2(ctx);
}

duk_ret_t duk_curl_global_cleanup(duk_context *ctx) 
{
  duk_push_this(ctx);
  duk_get_prop_string(ctx,-1,"inited");
  if( duk_get_boolean(ctx,-1) ) 
  {
    duk_pop(ctx);
    curl_global_cleanup();
    duk_push_boolean(ctx,0);
    duk_put_prop_string(ctx,-2,"inited");
  }
  duk_pop(ctx);
  return 0;
}

const char *duk_curl_to_strOrJSON(duk_context *ctx, duk_idx_t idx){
  if( duk_is_object(ctx,idx) )
  {
    const char *s;
    duk_dup(ctx,idx);
    s=duk_json_encode(ctx,-1);
    return s;
  }
  /* expecting a translation (as above) or a copy on the stack */
  duk_dup(ctx,idx);
  return duk_to_string(ctx,-1);
}

#define ARRAYREPEAT 0
#define ARRAYBRACKETREPEAT 1
#define ARRAYCOMMA 2
#define ARRAYJSON 3
/* *****************************************************
   serialize object to query string 
   return val needs to be freed
   ***************************************************** */
char * duk_curl_object2querystring(duk_context *ctx, duk_idx_t qsidx) {
  int i=0,atype=ARRAYREPEAT,end=qsidx+2;
  const char *arraytype;
  char *ret=(char*)NULL, *s;
  CURL *curl;
  
  //duk_curl_check_global(ctx);

  //curl = curl_easy_init();
  
  /* 
     look at next two stack items for a string,
     if i is -1, assume no string and don't wrap 
     around and go to 0 
  */
  if (qsidx!=-1)
  {
    for (i=qsidx; i<end; i++) 
    {
      if(duk_is_string(ctx,i)) 
      {
        arraytype=duk_get_string(ctx,i);

        if (!strncmp("bracket",arraytype,7))
          atype=ARRAYBRACKETREPEAT;
        else if (!strncmp("comma",arraytype,5))
          atype=ARRAYCOMMA;
        else if (!strcmp("json",arraytype))
          atype=ARRAYJSON;

        duk_remove(ctx,i);
        /* consequence of counting backwards and removing string */
        if (qsidx<0 && i!=qsidx) qsidx++;
      }
    }
  }
  i=0;
  if(duk_is_object(ctx,qsidx)  && !duk_is_array(ctx,qsidx) )
  {

    duk_enum(ctx, qsidx, DUK_ENUM_SORT_ARRAY_INDICES);
    while (duk_next(ctx, -1 , 1 ))
    {
      const char *stv; /* value from stack */
      stv=duk_to_string(ctx,-2);
      //s=curl_easy_escape(curl,stv,0);
      s=url_encode((char*)stv);
      if(i)
        ret=strjoin(ret,s,'&');
      else
        ret=strdup(s);
      /* don't free s just yet */

      if( atype!=ARRAYJSON && duk_is_array(ctx,-1) ) {
        int j=0;
        char *v=(char*)NULL;
        while(duk_has_prop_index(ctx,-1,j))        
        {

          duk_get_prop_index(ctx,-1,j);
          stv=duk_curl_to_strOrJSON(ctx,-1);
          //v=curl_easy_escape(curl,stv,0);              
          v=url_encode((char*)stv);
          duk_pop_2(ctx); /* get_prop_index and strOrJson both leave items on stack */
          switch(atype)
          {

             /* var1[]=a&var1[]=b */
            case ARRAYBRACKETREPEAT:
            {
              if(!j)
              {
                s=strcatdup(s,"%5B%5D");
                ret=strcatdup(ret,"%5B%5D");
              }
              /* no break, fall through */
            }

            /* var1=a&var1=b */
            case ARRAYREPEAT: 
            {
              if(j)
                ret=strjoin(ret,s,'&');
              
              ret=strjoin(ret,v,'=');
              break;
            }

            case ARRAYCOMMA: 
            /* var1=a,b */
            {
              if(j)
                ret=strjoin(ret,v,',');
              else
                ret=strjoin(ret,v,'=');
              
              break;
            }
          }
          curl_free(v);
          j++;
        }
        free(s);
      }
      else
      {
        free(s);
        //curl_free(s);
        stv=duk_curl_to_strOrJSON(ctx,-1);

        //s=curl_easy_escape(curl,stv,0);
        s=url_encode((char*)stv);
        ret=strjoin(ret,s,'=');
        duk_pop(ctx);/* duk_curl_to_strOrJSON */
        //curl_free(s);
        free(s);
      }

      i=1;
      duk_pop_2(ctx);
    }/* while */
  }
  //else error?
  duk_remove(ctx,qsidx);

//  curl_easy_cleanup(curl);
  return(ret);
}

/* *****************************************
   for use directly in JS
   var curl=require('./thismod');
   curl.objectToQuery({...});
******************************************** */
duk_ret_t duk_curl_object2q(duk_context *ctx) {
  char *s=duk_curl_object2querystring(ctx,0);
  duk_push_string(ctx,s);
  free(s);
  return 1;
}


void addheader(CSOS *sopts, char *h) 
{
  struct curl_slist *l;

  if(sopts->headerlist > -1)
    l=sopts->slists[sopts->headerlist];
  else
    l=NULL;

  l = curl_slist_append(l, h);
  if(sopts->headerlist == -1)
  {
    sopts->slists[sopts->nslists]=l;
    sopts->headerlist=sopts->nslists;
    sopts->nslists++;
  }
}

#define ISALNUM(c) (isalpha((c)) || isdigit((c)))
#define Curl_safefree(ptr) do { free((ptr)); (ptr) = NULL;} while(0)


/* from curl/src/tool_paramhlp.c */
long prototonum(long *val, const char *str)
{
  char *buffer;
  const char *sep = ",";
  char *token;

  static struct sprotos {
    const char *name;
    long bit;
  } const protos[] = {
    { "all", CURLPROTO_ALL },
    { "http", CURLPROTO_HTTP },
    { "https", CURLPROTO_HTTPS },
    { "ftp", CURLPROTO_FTP },
    { "ftps", CURLPROTO_FTPS },
    { "scp", CURLPROTO_SCP },
    { "sftp", CURLPROTO_SFTP },
    { "telnet", CURLPROTO_TELNET },
    { "ldap", CURLPROTO_LDAP },
    { "ldaps", CURLPROTO_LDAPS },
    { "dict", CURLPROTO_DICT },
    { "file", CURLPROTO_FILE },
    { "tftp", CURLPROTO_TFTP },
    { "imap", CURLPROTO_IMAP },
    { "imaps", CURLPROTO_IMAPS },
    { "pop3", CURLPROTO_POP3 },
    { "pop3s", CURLPROTO_POP3S },
    { "smtp", CURLPROTO_SMTP },
    { "smtps", CURLPROTO_SMTPS },
    { "rtsp", CURLPROTO_RTSP },
    { "gopher", CURLPROTO_GOPHER },
    { "smb", CURLPROTO_SMB },
    { "smbs", CURLPROTO_SMBS },
    { NULL, 0 }
  };

  if(!str)
    return 1;

  buffer = strdup(str); /* because strtok corrupts it */
  if(!buffer)
    return 1;

  /* Allow strtok() here since this isn't used threaded */
  /* !checksrc! disable BANNEDFUNC 2 */
  for(token = strtok(buffer, sep);
      token;
      token = strtok(NULL, sep)) {
    enum e_action { allow, deny, set } action = allow;

    struct sprotos const *pp;

    /* Process token modifiers */
    while(!ISALNUM(*token)) { /* may be NULL if token is all modifiers */
      switch (*token++) {
      case '=':
        action = set;
        break;
      case '-':
        action = deny;
        break;
      case '+':
        action = allow;
        break;
      default: /* Includes case of terminating NULL */
        Curl_safefree(buffer);
        return 1;
      }
    }

    for(pp = protos; pp->name; pp++) {
      if(curl_strequal(token, pp->name)) {
        switch(action) {
        case deny:
          *val &= ~(pp->bit);
          break;
        case allow:
          *val |= pp->bit;
          break;
        case set:
          *val = pp->bit;
          break;
        }
        break;
      }
    }

    if(!(pp->name)) { /* unknown protocol */
      /* If they have specified only this protocol, we say treat it as
         if no protocols are allowed */
      if(action == set)
        *val = 0;

    }
  }
  Curl_safefree(buffer);
  return 0;
}

typedef int (*copt_ptr)(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option);

int copt_get(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  duk_idx_t qsidx=-1;
  
  /* it's a string, add it to url => http://example.com/x.html?string */
  if (duk_is_string(ctx,-1))
    sopts->url=strjoin(sopts->url,(char*)duk_to_string(ctx,-1),'?');
  
  /* it's an object, check for prop "data" and "arrayType", then convert to querystring */
  else if(duk_is_object(ctx,-1) && !duk_is_array(ctx,-1) && duk_get_prop_string(ctx,-1,"data"))
  {
      char *s;

      if(duk_get_prop_string(ctx,-2,"arrayType"))
        qsidx=-2;

      else
        duk_pop(ctx); /* get rid of undefined */

      s=duk_curl_object2querystring(ctx,qsidx);
      sopts->url=strjoin(sopts->url,(char*)s,'?');
      free(s);
  }
  else
    return(1);

  duk_pop(ctx);
  return(0);
}

int copt_post(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  duk_idx_t qsidx=-1;
  char *postdata;  
  int freeme=0;
  
  /* it's a string, use as is */
  if (duk_is_string(ctx,-1))
    sopts->postdata=postdata=strdup( (char*)duk_to_string(ctx,-1) );


  /* it's an object, check for prop "data" and "arrayType", then convert to querystring */
  else if(duk_is_object(ctx,-1) && !duk_is_array(ctx,-1) && duk_get_prop_string(ctx,-1,"data"))
  {
      char *s;

      if(duk_get_prop_string(ctx,-2,"expect100") && duk_is_boolean(ctx,-1) && !duk_get_boolean(ctx,-1) )
          addheader(sopts, "EXPECT:");

      duk_pop(ctx);

      if(duk_get_prop_string(ctx,-2,"arrayType"))
        qsidx=-2;

      else
        duk_pop(ctx); /* get rid of undefined */



      /* save this to be freed after transaction is finished */
      postdata=sopts->postdata=duk_curl_object2querystring(ctx,qsidx);
      duk_pop(ctx);
  }
  else
     return(1);

  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, strlen(postdata));
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, postdata);
  return (0);
}


size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
  FILE *fh = (FILE *)userdata;
  curl_off_t nread;
 
  /* copy as much data as possible into the 'ptr' buffer, but no more than
     'size' * 'nmemb' bytes! */
  size_t retcode = fread(ptr, size, nmemb, fh);
 
  nread = (curl_off_t)retcode;
 
  printf("*** We read %" CURL_FORMAT_CURL_OFF_T
          " bytes from file\n", nread);
  return retcode;
}



int post_from_file(duk_context *ctx, CURL *handle, CSOS *sopts, char *fn)
{
  FILE *file = fopen(fn, "r");
  size_t sz;
  CURLcode result;
  char hbuf[64];

  if (file == NULL) {
    duk_push_sprintf(ctx, "could not open file \"%\".",fn);
    (void) duk_throw(ctx);
  }

  fseek(file, 0L, SEEK_END);
  sz = ftell(file);
  fseek(file, 0L, SEEK_SET); 

  sprintf(hbuf,"Content-Length: %llu", (long long unsigned int)sz);
  addheader(sopts, hbuf)  ;
  
  curl_easy_setopt(handle, CURLOPT_POST, 1L); 
  /* set callback to use */
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, NULL);
  curl_easy_setopt(handle, CURLOPT_READFUNCTION, read_callback);
 
  /* pass in suitable argument to callback */
  curl_easy_setopt(handle, CURLOPT_READDATA, (void *)file);
  return(0);
}

int copt_postbin(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) 
{
  duk_idx_t qsidx=-1;
  void *postdata=NULL;  
  int freeme=0;
  duk_size_t sz=0;

  if (duk_is_string(ctx,-1))
  {
    postdata=(void*)duk_to_string(ctx,-1);
    /* {postbin: "@filename"} */
    if (*((char*)postdata)=='@') {
      return( post_from_file(ctx, handle,sopts, postdata+1) );
    }
    else if ( *((char*)postdata)=='\\' && *((char*)postdata+1)=='@')
      postdata++;

    sz=strlen((char*)postdata);
  }
  else if (duk_is_buffer_data(ctx,-1))
  {
    postdata=duk_get_buffer_data(ctx,-1,&sz);
  }
  else
     return(1);

  /* when copt_* functions return, a pop_2 removes top two off the stack.  We want 
     this one to stay somewhere on the stack (no gc), and we also want our 
     duk_next loop to be in the right spot, so we'll move the data to 0 and add a blank string
     to be removed instead.  Agreed, it's not pretty.  But if postdata is large, this prevents
     an unnecessary and potentially large copy of the post data.
  */

  duk_insert(ctx,0);
  duk_push_string(ctx,"");

  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) sz);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, postdata);
  return(0);
}

#define SKIPARRAY 1
#define ENCODEARRAY 0

/* set the mimepart data from a file 
   Should we use the curl_mime_data_cb??  Or is this equivalent?
*/
void duk_curl_set_datafile(curl_mimepart *part, const char* fn)
{
  curl_mime_filedata(part, fn);
}

/* sets mimepart data if buffer_data, string, string="@filename", or object => json() 
   stack will remain the same
*/
int duk_curl_set_data(duk_context *ctx, curl_mimepart *part, int skiparrays) {
    if (duk_is_buffer_data(ctx,-1))
    {
      duk_size_t l=0;
      const char *data=duk_get_buffer_data(ctx,-1,&l);
      curl_mime_data(part, data, (size_t)l);
      return(1);
    }
    else if (duk_is_string(ctx,-1))
    {
      const char *s=duk_get_string(ctx,-1);
      if (*s=='@')
      {
        duk_curl_set_datafile(part,s+1);
        return(1);
      }
      else if (*s=='\\' && *(s+1)=='@')
        s++;
      curl_mime_data(part, s, CURL_ZERO_TERMINATED);
      return(1);
    }
    else if (duk_is_primitive(ctx,-1))
    /* otherwise a variable */
    {
      curl_mime_data(part, duk_to_string(ctx,-1), CURL_ZERO_TERMINATED);
      return(1);
    } 
    
    if (duk_is_array(ctx,-1) && skiparrays==SKIPARRAY)
      return(0);

    if(duk_is_object(ctx,-1))
    /* json encode all objects, including arrays if skiparrays=0 */
    {
      const char *s;
      duk_dup(ctx,-1);
      curl_mime_data(part, duk_json_encode(ctx,-1), CURL_ZERO_TERMINATED);
      duk_pop(ctx);
      return(1);
    }

    return(-1); /* shouldn't get here */
}


/* post multipart mime form data.
   Objects only   -   {"var1":"data1", "var2"="data2"}
   and for files  -   { "var1":"data1",
                        "FileFieldName": [
                          {"filename":"myfile1.txt" ,"type": "text/plain", "data":"..."},
                          {"filename":"myfile2.jpg" ,"type": "image/jpeg", "data":buffervar},
                          {"filename":"myfile3.jpg" ,"type": "image/jpeg", "data":"@filename"}
                        ]
                      }

    all files sent should have the same variable name.
    https://tools.ietf.org/html/rfc7578#section-4.3
    Therefore only one array of objects (as in "FileFieldName" above) should be present.
*/

char *operrors[]=
{"",
"invalid parameter",
"boolean (true or false) required",
"object required",
"array must contain objects with {data:...}"
};

int copt_postform(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  curl_mime *mime;
  curl_mimepart *part;

  if (!duk_is_object(ctx,-1))
    return(3); /* error: object required for postform */

  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  
  sopts->mime = curl_mime_init(handle);

  duk_enum(ctx, -1, DUK_ENUM_SORT_ARRAY_INDICES);
  while (duk_next(ctx, -1 /* index */, 1 /* get_value also */))
  {
    part = curl_mime_addpart(sopts->mime);
    if (duk_curl_set_data(ctx, part, SKIPARRAY))
    /*successfully added data, now set var name */
    {
       curl_mime_name(part, duk_to_string(ctx,-2)); 
    }
    else
    /* it's an array */
    {
      int i=0;
      /* should have array of objects, return error if not */
      while(duk_has_prop_index(ctx,-1,i))
      {
        duk_get_prop_index(ctx,-1,i);
        /* object must have a data prop and optionally a filename and/or type prop */
        if(duk_is_object(ctx,-1) && duk_has_prop_string(ctx,-1,"data"))
        {
           if(i) part = curl_mime_addpart(sopts->mime);

           curl_mime_name(part, duk_to_string(ctx,-3));

           duk_get_prop_string(ctx,-1,"data");
           (void) duk_curl_set_data(ctx, part, ENCODEARRAY);
           duk_pop(ctx); 
           
           if(duk_get_prop_string(ctx,-1,"filename"))
             curl_mime_filename(part,duk_get_string(ctx,-1));
           duk_pop(ctx);
           
           /* optional, default is text/plain */
           if(duk_get_prop_string(ctx,-1,"type"))
             curl_mime_type(part, duk_get_string(ctx,-1));
           duk_pop(ctx);
        }
        else
          return(4); /* error: array must be populated with objects={filename:...,data:...} */
        
        duk_pop(ctx);
        i++;
      }
    }
    printf("[%s, %s]\n",duk_to_string(ctx,-2),duk_to_string(ctx,-1));
    duk_pop_2(ctx);
  }    
  duk_pop(ctx);
  curl_easy_setopt(handle, CURLOPT_MIMEPOST, sopts->mime);
  return(0);    
}


#define getbool(b,idx) if(!duk_is_boolean(ctx,(idx))) return(2); (b)=(long)duk_get_boolean(ctx, (idx)) 

int copt_bool(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  curl_easy_setopt(handle,option,b);
  return(0);
}

/* set boolean, with some random extras or caveats */
int copt_boolplus(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  switch(subopt){
    case -1: {curl_easy_setopt(handle,option,(b)?0L:1L);break;} /* negate */
    case  1: {curl_easy_setopt(handle,CURLOPT_UNRESTRICTED_AUTH,1L);/* no break */}
    default: {curl_easy_setopt(handle,option,b);break;}
  }
  return(0);
}

int copt_fromsub(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  /* do nothing if not true */
  if(b)
    curl_easy_setopt(handle,option,(long)subopt);
  return(0);
}

int copt_compressed(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  if (b)
    curl_easy_setopt(handle,option,"br,gzip,deflate");
  /* default is null
  else
    curl_easy_setopt(handle,option,"");
  */
  return(0);
}

int copt_long(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b = (long) duk_get_int_default(ctx, -1, (duk_int_t)0);
  curl_easy_setopt(handle,option,b);
  return(0);
}

int copt_proto(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  long res=prototonum(&b,duk_to_string(ctx,-1));
  return(0);
}

int copt_64(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  curl_off_t b = (curl_off_t) duk_get_number_default(ctx, -1, (duk_double_t)0);
  curl_easy_setopt(handle,option,b);
  return(0);
}

int copt_1000(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b = (long) ( 1000.0 * duk_get_number_default(ctx, -1, (duk_int_t)0) );
  curl_easy_setopt(handle,option,b);
  return(0);
}

int copt_ftpmethod(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  const char *s = duk_to_string(ctx, -1);
  if(!strcmp("multicwd",s))
    curl_easy_setopt(handle,option,CURLFTPMETHOD_MULTICWD);

  else if(!strcmp("nocwd",s))
    curl_easy_setopt(handle,option,CURLFTPMETHOD_NOCWD);

  else if(!strcmp("singlecwd",s))
    curl_easy_setopt(handle,option,CURLFTPMETHOD_SINGLECWD);
  else return(1);
  
  return(0);
}

int copt_proxytype(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  const char *s = duk_to_string(ctx, -1);
  long flags[]={
    CURLPROXY_HTTP,              /* 0 */
    CURLPROXY_HTTPS,             /* 1 */
    CURLPROXY_HTTP_1_0,          /* 2 */
    CURLPROXY_SOCKS4,            /* 3 */
    CURLPROXY_SOCKS4A,           /* 4 */
    CURLPROXY_SOCKS5,            /* 5 */
    CURLPROXY_SOCKS5_HOSTNAME    /* 6 */
  };

  curl_easy_setopt(handle,option,s);
  curl_easy_setopt(handle,CURLOPT_PROXYTYPE,flags[subopt]);
  return(0);
}

int copt_string(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  const char *s = duk_to_string(ctx, -1);
  curl_easy_setopt(handle,option,s);
  return(0);
}

/* a string, or multiples strings in an array */
int copt_strings(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  if(duk_is_array(ctx,-1)) 
  {
    int i=0;
    while(duk_has_prop_index(ctx,-1,i))
    {
      duk_get_prop_index(ctx,-1,i);
      copt_string(ctx, handle, 0, sopts, option);
      duk_pop(ctx);
      i++;
    }
  }
  else
    return(copt_string(ctx, handle, 0, sopts, option));

  return(0);
}
/* turn JS array into curl slist, or if not an array, an slist with one member */
int copt_array_slist(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  struct curl_slist *list = NULL;

  if(subopt==1 && sopts->headerlist > -1){
    list=sopts->slists[sopts->headerlist];
    printf("using existing list at %d\n",sopts->headerlist);
  }
  if(duk_is_array(ctx,-1)) 
  {
    int i=0;
    while(duk_has_prop_index(ctx,-1,i))
    {
      duk_get_prop_index(ctx,-1,i);
      list=curl_slist_append(list, duk_to_string(ctx, -1));
      duk_pop(ctx);
      i++;
    }
  }
  else
    list=curl_slist_append(list, duk_to_string(ctx, -1));

  if(subopt==1) /* header list.  Keep it around in case we have more headers elsewhere */
  {
    //printf("subopt==1\n");
    if(sopts->headerlist==-1)
      sopts->headerlist=sopts->nslists;
    else
      return (0); /* we already have headerlist set and slists[sopts->headerlist] was appended */
  }
  else
    curl_easy_setopt(handle, option, list);

  sopts->slists[sopts->nslists]=list;
  sopts->nslists++;

  return(0);
}

int copt_ssl_ccc(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  if (!subopt)
  {
    long b;
    getbool(b,-1);
    b=(b)?CURLFTPSSL_CCC_PASSIVE:CURLFTPSSL_CCC_NONE;
    curl_easy_setopt(handle,option,b);
  } 
  else
  {
    const char *s = duk_to_string(ctx, -1);
    if(!strcmp("passive",s))
      curl_easy_setopt(handle,option,CURLFTPSSL_CCC_PASSIVE);

    else if(!strcmp("active",s))
      curl_easy_setopt(handle,option,CURLFTPSSL_CCC_ACTIVE);
  }
  return(0);
}

int copt_netrc(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);

  if (b)
  {
    if(subopt)
      curl_easy_setopt(handle,option,CURL_NETRC_OPTIONAL);
    else
      curl_easy_setopt(handle,option,CURL_NETRC_REQUIRED);
  } 
  else
  {
    curl_easy_setopt(handle,option,CURL_NETRC_IGNORED);
  }
  return(0);
}

int copt_auth(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  long flags[8]={CURLAUTH_BASIC,CURLAUTH_DIGEST,CURLAUTH_DIGEST_IE,3,CURLAUTH_NEGOTIATE,CURLAUTH_NTLM,CURLAUTH_NTLM_WB,CURLAUTH_ANY};
  long flag=flags[subopt];
  long *curflag = (option==CURLOPT_PROXYAUTH)? &(sopts->proxyauth): &(sopts->httpauth);
  
  if(b)
    *curflag |= flag;
  else
    *curflag &= ~flag;

  curl_easy_setopt(handle,option,*curflag);
  return(0);
}

int copt_socks5auth(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  long flag=0;
  if (!b) 
    flag |= CURLAUTH_BASIC|CURLAUTH_GSSAPI; /* the default */
  if(subopt==1)
    flag=CURLAUTH_BASIC;
  if(subopt==2)
    flag |= CURLAUTH_BASIC|CURLAUTH_GSSAPI;

  curl_easy_setopt(handle,option,flag);
  return(0);
}

int copt_sslopt(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  long *curflag = (option==CURLOPT_PROXY_SSL_OPTIONS)? &(sopts->proxyssloptions): &(sopts->ssloptions);
  /* long flags[4]={CURLSSLOPT_ALLOW_BEAST,CURLSSLOPT_NO_REVOKE,CURLSSLOPT_NO_PARTIALCHAIN,CURLSSLOPT_REVOKE_BEST_EFFORT}; */
  long flags[2]={CURLSSLOPT_ALLOW_BEAST,CURLSSLOPT_NO_REVOKE};
  long flag=flags[subopt];

  if(b)
    *curflag |= flag;
  else
    *curflag &= ~flag;

  curl_easy_setopt(handle,option,*curflag);
  return(0);
}

int copt_postr(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  long flags[4]={CURL_REDIR_POST_301,CURL_REDIR_POST_302,CURL_REDIR_POST_303,CURL_REDIR_POST_ALL};
  long flag=flags[subopt];

  if(b)
    sopts->postredir |= flag;
  else
    sopts->postredir &= ~flag;

  curl_easy_setopt(handle,CURLOPT_POSTREDIR,sopts->postredir);
  return(0);
}

int copt_sslver(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  long b;
  getbool(b,-1);
  long flags[]={
    CURL_SSLVERSION_DEFAULT,     /* 0  */
    CURL_SSLVERSION_TLSv1,       /* 1  */
    CURL_SSLVERSION_SSLv2,       /* 2  */
    CURL_SSLVERSION_SSLv3,       /* 3  */
    CURL_SSLVERSION_TLSv1_0,     /* 4  */
    CURL_SSLVERSION_TLSv1_1,     /* 5  */
    CURL_SSLVERSION_TLSv1_2,     /* 6  */
    CURL_SSLVERSION_TLSv1_3,     /* 7  */
    CURL_SSLVERSION_MAX_DEFAULT, /* 8  */
    CURL_SSLVERSION_MAX_TLSv1_0, /* 9  */
    CURL_SSLVERSION_MAX_TLSv1_1, /* 10 */
    CURL_SSLVERSION_MAX_TLSv1_2, /* 11 */
    CURL_SSLVERSION_MAX_TLSv1_3, /* 12 */
  };
  long flag=(b)?flags[subopt]:flags[0];
  
  curl_easy_setopt(handle,option,flag);
  return(0);
}

int copt_tlsmax(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  const char *s=duk_to_string(ctx, -1);
  
  if(!strcmp(s,"1.0"))
    return(copt_sslver(ctx, handle, 9, sopts, option));
  else if(!strcmp(s,"1.1"))
    return(copt_sslver(ctx, handle, 10, sopts, option));
  else if(!strcmp(s,"1.2"))
    return(copt_sslver(ctx, handle, 11, sopts, option));
  else if(!strcmp(s,"1.3"))
    return(copt_sslver(ctx, handle, 12, sopts, option));
  else if(!strcmp(s,"1"))
    return(copt_sslver(ctx, handle, 9, sopts, option));
    
  return(1); /* unknown parameter for option */
}

int copt_continue(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  const char *s;
  
  duk_push_string(ctx,"-");
  duk_concat(ctx,2);
  s = duk_to_string(ctx, -1);
  curl_easy_setopt(handle,option,s);
  return(0);
}

int copt_httpv(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
    /* assume true unless explicitly false.  If false, let curl choose version */
    long b;
    getbool(b,-1);
    if(!b) subopt=21;
    switch(subopt)
    {
      case 0: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_NONE); break;}
      case 10: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_1_0); break;}
      case 11: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_1_1); break;}
      case 20: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_2_0); break;}
      case 21: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_2TLS); break;}
      case 22: { curl_easy_setopt(handle,option,CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE); break;}
      /*case 30: { url_easy_setopt(handle,option,CURL_HTTP_VERSION_3); break;} */
    }
    return(0);
}

int copt_resolv(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
    /* assume true unless explicitly false.  If false, then use 'whatever' */
    long b;
    getbool(b,-1);
    long flags[3]={CURL_IPRESOLVE_WHATEVER,CURL_IPRESOLVE_V4,CURL_IPRESOLVE_V6};

    if(!b) subopt=0;
    
    curl_easy_setopt(handle,option,flags[subopt]);
    return(0);
}

int copt_insecure(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
    long b;
    getbool(b,-1);
    b=!b;
    CURLoption vh=(option==CURLOPT_PROXY_SSL_VERIFYPEER)?CURLOPT_PROXY_SSL_VERIFYHOST:CURLOPT_SSL_VERIFYHOST;
    /*
     * If you want to connect to a site who isn't using a certificate that is
     * signed by one of the certs in the CA bundle you have, you can skip the
     * verification of the server's certificate. This makes the connection
     * A LOT LESS SECURE.
     *
     * If you have a CA cert for the server stored someplace else than in the
     * default bundle, then the CURLOPT_CAPATH option might come handy for
     * you.
     */ 
    curl_easy_setopt(handle, option, b);
 
    /*
     * If the site you're connecting to uses a different host name that what
     * they have mentioned in their server certificate's commonName (or
     * subjectAltName) fields, libcurl will refuse to connect. You can skip
     * this check, but this will make the connection less secure.
     */ 
    curl_easy_setopt(handle, vh, b);
    return(0);
}
/* TODO: check for number */
int copt_limit(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  curl_off_t l = (curl_off_t) duk_get_number_default(ctx, -1, (duk_double_t)0);
  curl_easy_setopt(handle, CURLOPT_MAX_RECV_SPEED_LARGE,l);
  curl_easy_setopt(handle, CURLOPT_MAX_SEND_SPEED_LARGE,l);
  return(0);
}

/*
int copt_oauth(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  copt_string(ctx,handle,subopt,sopts,option);
  
  sopts->httpauth |= CURLAUTH_BEARER;
  curl_easy_setopt(handle,CURLOPT_HTTPAUTH,sopts->httpauth);
}
*/

/* TODO: accept string ddd-ddd */
int copt_lport(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
  if(duk_is_array(ctx,-1))
  {
    int i=0;
    int p=0;
    while(duk_has_prop_index(ctx,-1,i))
    {
      duk_get_prop_index(ctx,-1,i);
      if(duk_is_number(ctx,-1)) 
      {
        if(i)
          curl_easy_setopt(handle, CURLOPT_LOCALPORTRANGE,(long)( (int)duk_get_int(ctx,-1) - p ));
        else
          curl_easy_setopt( handle, CURLOPT_LOCALPORT,(long)(p=(int)duk_get_int(ctx,-1)) );
      }
      duk_pop(ctx);
      i++;
      if (i>1) break;
    }
  }
  else if( duk_is_number(ctx,-1))
  {
    curl_easy_setopt( handle, CURLOPT_LOCALPORT,(long)duk_get_int(ctx,-1) );
  }
  return(0);
}

int copt_cert(duk_context *ctx, CURL *handle, int subopt, CSOS *sopts, CURLoption option) {
    char *s, *p;

    CURLoption sslkey=(option==CURLOPT_SSLCERT)?CURLOPT_KEYPASSWD:CURLOPT_PROXY_KEYPASSWD;
    
    s=p=strdup((char*)duk_to_string(ctx,-1));
    while( *p && *p!=':') p++;
    
    if(*p)
    {
      *p='\0';
      p++;
      curl_easy_setopt(handle,sslkey, p);
    }
    curl_easy_setopt(handle, option, s);
    free(s);
    return(0);
}

#define CURL_OPTS struct curl_opts

CURL_OPTS {
  char *optionName;
  CURLoption option;
  int subopt;
  copt_ptr func;
};
/* these must be sorted.  If using shell sort, do LC_ALL=C sort to get the proper sort order */
CURL_OPTS curl_options[]={
  {"0",                            CURLOPT_HTTP_VERSION,               10,&copt_httpv}, 
  {"1",                            CURLOPT_SSLVERSION,                 1, &copt_sslver},
  {"2",                            CURLOPT_SSLVERSION,                 2, &copt_sslver},
  {"3",                            CURLOPT_SSLVERSION,                 3, &copt_sslver},
  {"4",                            CURLOPT_IPRESOLVE,                  1, &copt_resolv},
  {"6",                            CURLOPT_IPRESOLVE,                  2, &copt_resolv},
  {"A",                            CURLOPT_USERAGENT,                  0, &copt_string},
  {"C",                            CURLOPT_RANGE,                      0, &copt_continue},  
  {"E",                            CURLOPT_SSLCERT,                    0, &copt_cert},
  {"H",                            CURLOPT_HTTPHEADER,                 1, &copt_array_slist},
  {"L",                            CURLOPT_FOLLOWLOCATION,             0, &copt_bool},
  {"P",                            CURLOPT_FTPPORT,                    0, &copt_string},
  {"Q",                            CURLOPT_QUOTE,                      0, &copt_array_slist},
  {"X",                            CURLOPT_CUSTOMREQUEST,              0, &copt_string},
  {"Y",                            CURLOPT_LOW_SPEED_LIMIT,            0, &copt_long},
  {"anyauth",                      CURLOPT_HTTPAUTH,                   7, &copt_auth},
  {"b",                            CURLOPT_COOKIE,                     0, &copt_string},
  {"basic",                        CURLOPT_HTTPAUTH,                   0, &copt_auth},
  {"c",                            CURLOPT_COOKIEJAR,                  0, &copt_string},
  {"cacert",                       CURLOPT_CAINFO,                     0, &copt_string},
  {"capath",                       CURLOPT_CAPATH,                     0, &copt_string},
  {"cert",                         CURLOPT_SSLCERT,                    0, &copt_cert},
  {"cert-status",                  CURLOPT_SSL_VERIFYSTATUS,           0, &copt_bool},
  {"cert-type",                    CURLOPT_SSLCERTTYPE,                0, &copt_string},
  {"ciphers",                      CURLOPT_SSL_CIPHER_LIST,            0, &copt_string},
  {"compressed",                   CURLOPT_ACCEPT_ENCODING,            0, &copt_compressed},
  {"compressed-ssh",               CURLOPT_SSH_COMPRESSION,            0, &copt_bool},
  {"connect-timeout",              CURLOPT_CONNECTTIMEOUT,             0, &copt_long},
  {"connect-to",                   CURLOPT_CONNECT_TO,                 0, &copt_strings},
  {"continue-at",                  CURLOPT_RANGE,                      0, &copt_continue},  
  {"cookie",                       CURLOPT_COOKIE,                     0, &copt_string},
  {"cookie-jar",                   CURLOPT_COOKIEJAR,                  0, &copt_string},
  {"crlf",                         CURLOPT_CRLF,                       0, &copt_bool},
  {"crlfile",                      CURLOPT_CRLFILE,                    0, &copt_string},
  {"delegation",                   CURLOPT_GSSAPI_DELEGATION,          0, &copt_string},    
  {"digest",                       CURLOPT_HTTPAUTH,	               1, &copt_auth},
  {"digest-ie",                    CURLOPT_HTTPAUTH,	               2, &copt_auth},
  {"disable-eprt",                 CURLOPT_FTP_USE_EPRT,	       -1,&copt_boolplus},
  {"disable-epsv",                 CURLOPT_FTP_USE_EPSV,	       -1,&copt_boolplus},
  {"dns-interface",                CURLOPT_DNS_INTERFACE,              0, &copt_string},
  {"dns-ipv4-addr",                CURLOPT_DNS_LOCAL_IP4,              0, &copt_string},
  {"dns-ipv6-addr",                CURLOPT_DNS_LOCAL_IP6,              0, &copt_string},
  {"dns-servers",                  CURLOPT_DNS_SERVERS,                0, &copt_string},
  {"e",                            CURLOPT_REFERER,                    0, &copt_string},
  {"expect100_timeout",            CURLOPT_EXPECT_100_TIMEOUT_MS,      0, &copt_1000},
  {"false-start",                  CURLOPT_SSL_FALSESTART,             0, &copt_bool},
  {"ftp-account",                  CURLOPT_FTP_ACCOUNT,                0, &copt_string},
  {"ftp-alternative-to-user",      CURLOPT_FTP_ALTERNATIVE_TO_USER,    0, &copt_string},
  {"ftp-create-dirs",              CURLOPT_FTP_CREATE_MISSING_DIRS,    0, &copt_bool},
  {"ftp-method",                   CURLOPT_FTP_FILEMETHOD,             0, &copt_ftpmethod},
  {"ftp-port",                     CURLOPT_FTPPORT,                    0, &copt_string},
  {"ftp-pret",                     CURLOPT_FTP_USE_PRET,               0, &copt_bool},
  {"ftp-skip-pasv-ip",             CURLOPT_FTP_SKIP_PASV_IP,           0, &copt_bool},
  {"ftp-ssl-ccc",                  CURLOPT_FTP_SSL_CCC,                0, &copt_ssl_ccc},
  {"ftp-ssl-ccc-mode",             CURLOPT_FTP_SSL_CCC,                1, &copt_ssl_ccc},
  {"get",                          0, /* not used */                   0, &copt_get},
  {"header",                       CURLOPT_HTTPHEADER,                 1, &copt_array_slist},
  {"headers",                      CURLOPT_HTTPHEADER,                 1, &copt_array_slist},
  {"hostpubmd5",                   CURLOPT_SSH_HOST_PUBLIC_KEY_MD5,    0, &copt_string},
  {"http-any",                     CURLOPT_HTTP_VERSION,               0, &copt_httpv}, /* not a command line option (yet?)*/ 
  {"http1.0",                      CURLOPT_HTTP_VERSION,               10,&copt_httpv}, 
  {"http1.0",                      CURLOPT_HTTP_VERSION,               10,&copt_httpv}, 
  {"http1.1",                      CURLOPT_HTTP_VERSION,               11,&copt_httpv}, 
  {"http2",                        CURLOPT_HTTP_VERSION,               20,&copt_httpv}, 
  {"http2-prior-knowledge",        CURLOPT_HTTP_VERSION,               22,&copt_httpv}, 
  {"ignore-content-length",        CURLOPT_IGNORE_CONTENT_LENGTH,      0, &copt_bool},
  {"insecure",                     CURLOPT_SSL_VERIFYPEER,             0, &copt_insecure},
  {"interface",                    CURLOPT_INTERFACE,                  0, &copt_bool},
  {"ipv4",                         CURLOPT_IPRESOLVE,                  1, &copt_resolv},
  {"ipv6",                         CURLOPT_IPRESOLVE,                  2, &copt_resolv},
  {"j",                            CURLOPT_COOKIESESSION,              0, &copt_bool},
  {"junk-session-cookies",         CURLOPT_COOKIESESSION,              0, &copt_bool},
  {"k",                            CURLOPT_SSL_VERIFYPEER,             0, &copt_insecure},
  {"keepalive-time",               CURLOPT_TCP_KEEPALIVE,              0, &copt_long},
  {"key",                          CURLOPT_SSLKEY,                     0, &copt_string},
  {"key-type",                     CURLOPT_SSLKEYTYPE,                 0, &copt_string},
  {"krb",                          CURLOPT_KRBLEVEL,                   0, &copt_string},
  {"l",                            CURLOPT_DIRLISTONLY,                0, &copt_bool},
  {"limit-rate",                   CURLOPT_MAX_SEND_SPEED_LARGE,       0, &copt_limit},
  {"list-only",                    CURLOPT_DIRLISTONLY,                0, &copt_bool},
  {"local-port",                   CURLOPT_LOCALPORT,                  0, &copt_lport},
  {"location",                     CURLOPT_FOLLOWLOCATION,             0, &copt_bool},
  {"location-trusted",             CURLOPT_FOLLOWLOCATION,             1, &copt_boolplus},
  {"login-options",                CURLOPT_LOGIN_OPTIONS,              0, &copt_string},
  {"m",                            CURLOPT_TIMEOUT_MS,                 0, &copt_1000},
  {"mail-auth",                    CURLOPT_MAIL_AUTH,                  0, &copt_string},
  {"mail-from",                    CURLOPT_MAIL_FROM,                  0, &copt_string},
  {"mail-rcpt",                    CURLOPT_MAIL_RCPT,                  0, &copt_array_slist},
  {"max-filesize",                 CURLOPT_MAXFILESIZE_LARGE,          0, &copt_64},
  {"max-redirs",                   CURLOPT_MAXREDIRS,                  0, &copt_long},
  {"max-time",                     CURLOPT_TIMEOUT_MS,                 0, &copt_1000},
  {"n",                            CURLOPT_NETRC,                      0, &copt_netrc},
  {"negotiate",                    CURLOPT_HTTPAUTH,                   4, &copt_auth},
  {"netrc",                        CURLOPT_NETRC,                      0, &copt_netrc},
  {"netrc-file",                   CURLOPT_NETRC_FILE,                 0, &copt_string},
  {"netrc-optional",               CURLOPT_NETRC,                      1, &copt_netrc},
  {"no-alpn",                      CURLOPT_SSL_ENABLE_ALPN,            -1,&copt_boolplus},
  {"no-keepalive",                 CURLOPT_TCP_KEEPALIVE,              -1,&copt_boolplus},
  {"no-npn",                       CURLOPT_SSL_ENABLE_NPN,             -1,&copt_boolplus},
  {"no-sessionid",                 CURLOPT_SSL_SESSIONID_CACHE,        -1,&copt_boolplus},
  {"noproxy",                      CURLOPT_NOPROXY,                    0, &copt_string},
  {"ntlm",                         CURLOPT_HTTPAUTH,                   5, &copt_auth},
  {"ntlm-wb",                      CURLOPT_HTTPAUTH,                   6, &copt_auth},
  {"pass",                         CURLOPT_PASSWORD,                   0, &copt_string},
  {"path-as-is",                   CURLOPT_PATH_AS_IS,                 0, &copt_bool},
  {"pinnedpubkey",                 CURLOPT_PINNEDPUBLICKEY,            0, &copt_string},
  {"post",                         0, /* not used */                   0, &copt_post},
  {"post301",                      CURLOPT_POSTREDIR,                  0, &copt_postr},
  {"post302",                      CURLOPT_POSTREDIR,                  1, &copt_postr},
  {"post303",                      CURLOPT_POSTREDIR,                  2, &copt_postr},
  {"postbin",                      0, /* not used */                   0, &copt_postbin},
  {"postform",                     0, /* not used */                   0, &copt_postform},
  {"postredir",                    CURLOPT_POSTREDIR,                  3, &copt_postr},
  {"preproxy",                     CURLOPT_PRE_PROXY,                  0, &copt_string},
  {"proto",                        CURLOPT_PROTOCOLS,                  0, &copt_proto},
  {"proto-default",                CURLOPT_DEFAULT_PROTOCOL,           0, &copt_string},
  {"proto-redir",                  CURLOPT_REDIR_PROTOCOLS,            0, &copt_string},
  {"proxy",                        CURLOPT_PROXY,                      0, &copt_string},
  {"proxy-anyauth",                CURLOPT_PROXYAUTH,                  7, &copt_auth},
  {"proxy-basic",                  CURLOPT_PROXYAUTH,                  0, &copt_auth},
  {"proxy-cacert",                 CURLOPT_PROXY_CAINFO,               0, &copt_string},
  {"proxy-capath",                 CURLOPT_PROXY_CAPATH,               0, &copt_string},
  {"proxy-cert",                   CURLOPT_PROXY_SSLCERT,              0, &copt_cert},
  {"proxy-cert-type",              CURLOPT_PROXY_SSLCERTTYPE,          0, &copt_string},
  {"proxy-ciphers",                CURLOPT_PROXY_SSL_CIPHER_LIST,      0, &copt_string},
  {"proxy-crlfile",                CURLOPT_PROXY_CRLFILE,              0, &copt_string},
  {"proxy-digest",                 CURLOPT_PROXYAUTH,	               1, &copt_auth},
  {"proxy-digest-ie",              CURLOPT_PROXYAUTH,	               2, &copt_auth},
  {"proxy-header",                 CURLOPT_PROXYHEADER,                0, &copt_array_slist},
  {"proxy-insecure",               CURLOPT_PROXY_SSL_VERIFYPEER,       0, &copt_insecure},
  {"proxy-key",                    CURLOPT_PROXY_SSLKEY,               0, &copt_string},
  {"proxy-key-type",               CURLOPT_PROXY_SSLKEYTYPE,           0, &copt_string},
  {"proxy-negotiate",              CURLOPT_PROXYAUTH,                  4, &copt_auth},
  {"proxy-ntlm",                   CURLOPT_PROXYAUTH,                  5, &copt_auth},
  {"proxy-ntlm-wb",                CURLOPT_PROXYAUTH,                  6, &copt_auth},
  {"proxy-pass",                   CURLOPT_PROXYPASSWORD,              0, &copt_string},
  {"proxy-service-name",           CURLOPT_PROXY_SERVICE_NAME,         0, &copt_string},
  {"proxy-ssl-allow-beast",        CURLOPT_PROXY_SSL_OPTIONS,          0, &copt_sslopt},
  {"proxy-ssl-no-revoke",          CURLOPT_PROXY_SSL_OPTIONS,          1, &copt_sslopt},
  {"proxy-tlspassword",            CURLOPT_PROXY_TLSAUTH_PASSWORD,     0, &copt_string},
  {"proxy-tlsuser",                CURLOPT_PROXY_TLSAUTH_USERNAME,     0, &copt_string},
  {"proxy-tlsv1",                  CURLOPT_PROXY_SSLVERSION,           1, &copt_sslver},
  {"proxy-user",                   CURLOPT_PROXYUSERPWD,               0, &copt_string},
  {"proxy1.0",		           CURLOPT_PROXY,                      2, &copt_proxytype},
  {"proxytunnel",                  CURLOPT_HTTPPROXYTUNNEL,            0, &copt_bool},
  {"pubkey",                       CURLOPT_SSH_PUBLIC_KEYFILE,         0, &copt_string},
  {"quote",                        CURLOPT_QUOTE,                      0, &copt_array_slist},
  {"r",                            CURLOPT_RANGE,                      0, &copt_string},
  {"random-file",                  CURLOPT_RANDOM_FILE,                0, &copt_string},
  {"range",                        CURLOPT_RANGE,                      0, &copt_string},
  {"referer",                      CURLOPT_REFERER,                    0, &copt_string},
  {"request",                      CURLOPT_CUSTOMREQUEST,              0, &copt_string},
  {"request-target",               CURLOPT_REQUEST_TARGET,             0, &copt_string},
  {"resolve",                      CURLOPT_RESOLVE,                    0, &copt_array_slist},
  {"sasl-ir",                      CURLOPT_SASL_IR,                    0, &copt_bool},
  {"service-name",                 CURLOPT_SERVICE_NAME,               0, &copt_string},
  {"socks4",                       CURLOPT_PROXY,                      3, &copt_proxytype},
  {"socks4a",                      CURLOPT_PROXY,                      4, &copt_proxytype},
  {"socks5",                       CURLOPT_PROXY,                      5, &copt_proxytype},
  {"socks5-basic",                 CURLOPT_SOCKS5_AUTH,                1, &copt_socks5auth},
  {"socks5-gssapi",                CURLOPT_SOCKS5_AUTH,                2, &copt_socks5auth},
  {"socks5-gssapi-nec",            CURLOPT_SOCKS5_GSSAPI_NEC,          0, &copt_bool},
  {"socks5-gssapi-service",        CURLOPT_SOCKS5_GSSAPI_SERVICE,      0, &copt_string},
  {"socks5-hostname",              CURLOPT_PROXY,                      6, &copt_proxytype},
  {"speed-limit",                  CURLOPT_LOW_SPEED_LIMIT,            0, &copt_long},
  {"speed-time",                   CURLOPT_LOW_SPEED_TIME,             0, &copt_long},
  {"ssl",                          CURLOPT_USE_SSL,                    (int)CURLUSESSL_TRY, &copt_fromsub},
  {"ssl-allow-beast",              CURLOPT_SSL_OPTIONS,                0, &copt_sslopt},
  {"ssl-no-revoke",                CURLOPT_SSL_OPTIONS,                1, &copt_sslopt},
  {"ssl-reqd",                     CURLOPT_USE_SSL,                    (int)CURLUSESSL_ALL, &copt_fromsub},
  {"sslv2",                        CURLOPT_SSLVERSION,                 2, &copt_sslver},
  {"sslv3",                        CURLOPT_SSLVERSION,                 3, &copt_sslver},
  {"suppress-connect-headers",     CURLOPT_SUPPRESS_CONNECT_HEADERS,   0, &copt_bool},
  {"t",                            CURLOPT_TELNETOPTIONS,              0, &copt_array_slist},
  {"tcp-fastopen",                 CURLOPT_TCP_FASTOPEN,               0, &copt_bool},
  {"tcp-nodelay",                  CURLOPT_TCP_NODELAY,                0, &copt_bool},
  {"telnet-option",                CURLOPT_TELNETOPTIONS,              0, &copt_array_slist},
  {"tftp-blksize",                 CURLOPT_TFTP_BLKSIZE,               0, &copt_long},
  {"tftp-no-options",              CURLOPT_TFTP_NO_OPTIONS,            0, &copt_bool},
  {"time-cond",                    CURLOPT_TIMECONDITION,              0, &copt_long},
  {"tls-max",                      CURLOPT_SSLVERSION,                 0, &copt_tlsmax},
  {"tlspassword",                  CURLOPT_TLSAUTH_PASSWORD,           0, &copt_string},
  {"tlsuser",                      CURLOPT_TLSAUTH_USERNAME,           0, &copt_string},
  {"tlsv1",                        CURLOPT_SSLVERSION,                 1, &copt_sslver},
  {"tlsv1.0",                      CURLOPT_SSLVERSION,                 4, &copt_sslver},
  {"tlsv1.1",                      CURLOPT_SSLVERSION,                 5, &copt_sslver},
  {"tlsv1.2",                      CURLOPT_SSLVERSION,                 6, &copt_sslver},
  {"tlsv1.3",                      CURLOPT_SSLVERSION,                 7, &copt_sslver},
  {"tr-encoding",                  CURLOPT_TRANSFER_ENCODING,          0, &copt_bool},
  {"u",                            CURLOPT_USERPWD,                    0, &copt_string},
  {"unix-socket",                  CURLOPT_UNIX_SOCKET_PATH,           0, &copt_string},
  {"user",                         CURLOPT_USERPWD,                    0, &copt_string},
  {"user-agent",                   CURLOPT_USERAGENT,                  0, &copt_string},
  {"x",                            CURLOPT_PROXY,                      0, &copt_string},
  {"y",                            CURLOPT_LOW_SPEED_TIME,             0, &copt_long},
  {"z",                            CURLOPT_TIMECONDITION,              0, &copt_long}

/*{"oauth2-bearer",          CURLOPT_XOAUTH2_BEARER,         0, &copt_oauth}, */
/*{"http3",                  CURLOPT_HTTP_VERSION,           30,&copt_httpv}, */

};
#define nCurlOpts (sizeof(curl_options)/sizeof(curl_options[0]))

int compare_copts(void const *a, void const *b) { 
 char const *as = (char const *)((CURL_OPTS*)a)->optionName;
 char const *bs = (char const *)((CURL_OPTS*)b)->optionName;

 return strcmp(as, bs);
}


#define HTTP_CODE struct http_code

HTTP_CODE {
	int code;
	char *msg;
};
/* https://developer.mozilla.org/en-US/docs/Web/HTTP/Status */
HTTP_CODE http_codes[]={
{100, "Continue"},
{101, "Switching Protocol"},
{102, "Processing (WebDAV)"},
{103, "Early Hints"},
{200, "OK"},
{201, "Created"},
{202, "Accepted"},
{203, "Non-Authoritative Information"},
{204, "No Content"},
{205, "Reset Content"},
{206, "Partial Content"},
{207, "Multi-Status (WebDAV)"},
{208, "Already Reported (WebDAV)"},
{226, "IM Used (HTTP Delta encoding)"},
{300, "Multiple Choice"},
{301, "Moved Permanently"},
{302, "Found"},
{303, "See Other"},
{304, "Not Modified"},
{305, "Use Proxy"},
{306, "unused"},
{307, "Temporary Redirect"},
{308, "Permanent Redirect"},
{400, "Bad Request"},
{401, "Unauthorized"},
{402, "Payment Required"},
{403, "Forbidden"},
{404, "Not Found"},
{405, "Method Not Allowed"},
{406, "Not Acceptable"},
{407, "Proxy Authentication Required"},
{408, "Request Timeout"},
{409, "Conflict"},
{410, "Gone"},
{411, "Length Required"},
{412, "Precondition Failed"},
{413, "Payload Too Large"},
{414, "URI Too Long"},
{415, "Unsupported Media Type"},
{416, "Range Not Satisfiable"},
{417, "Expectation Failed"},
{418, "I'm a teapot"},  /* this is a joke, but it's real: https://en.wikipedia.org/wiki/Hyper_Text_Coffee_Pot_Control_Protocol */ 
{421, "Misdirected Request"},
{422, "Unprocessable Entity (WebDAV)"},
{423, "Locked (WebDAV)"},
{424, "Failed Dependency (WebDAV)"},
{425, "Too Early"},
{426, "Upgrade Required"},
{428, "Precondition Required"},
{429, "Too Many Requests"},
{431, "Request Header Fields Too Large"},
{451, "Unavailable For Legal Reasons"},
{500, "Internal Server Error"},
{501, "Not Implemented"},
{502, "Bad Gateway"},
{503, "Service Unavailable"},
{504, "Gateway Timeout"},
{505, "HTTP Version Not Supported"},
{506, "Variant Also Negotiates"},
{507, "Insufficient Storage (WebDAV)"},
{508, "Loop Detected (WebDAV)"},
{510, "Not Extended"},
{511, "Network Authentication Required"}
};
#define nHttpCodes (sizeof(http_codes)/sizeof(http_codes[0]))

int compare_hcode(const void *a, const void *b)
{
  HTTP_CODE *ac = (HTTP_CODE *) a;
  HTTP_CODE *bc = (HTTP_CODE *) b;

  return(ac->code - bc->code);
}



void duk_curl_parse_headers(duk_context *ctx,char *header)
{
  char *start=header, *key=(char*)NULL,  *end=header, buf[32];
  
  int inval=0, ukn=0, gotstatus=0;
  
  if(header==(char*)NULL)
    return;

  while ( *end != '\0')
  {
    if(inval) 
    {
      if(*end=='\n' && key !=(char*)NULL)
      {
        char *term=end;
        /* remove white space before the '\n' */
        while(term>start && isspace(*term)) term--;
        *(term+1)='\0';
        
        duk_push_string(ctx,start);
        duk_put_prop_string(ctx,-2,key);
        inval=0;
        end++;
        start=end;
        key=(char*)NULL;
      }    
    }
    else
    {
      /* the colon is the end of our header name */
      if(*end==':')
      {
        char *term=end-1;
        /* remove white space before the ':' */
        while(term>start && isspace(*term)) term--;
        *(term+1)='\0';

        key=start;
        end++;
        while(*end != '\0' && isspace(*end)) end++;
        start=end;
        inval=1;
      } 
      /* this is the first Status Line (HTTP/1.1 301 Moved Permanently) or some wierd error, or ftp headers */
      else if (*end =='\n')
      {
        char *term=end;

        if (start+1 == end) /* two \n -- end of headers */
          break;

        while(term>start && isspace(*term)) term--;
        *(term+1)='\0';
        duk_push_string(ctx,start);
        if (!gotstatus)
        {
          duk_put_prop_string(ctx,-2,"STATUS");
          gotstatus=1;
        }
        else
        {
          snprintf(buf,31,"HeaderLine_%d",gotstatus++);
          duk_put_prop_string(ctx,-2,buf);
        }
        end++;
        start=end;
      }
    }
    end++;
  }
}


int duk_curl_push_res(duk_context *ctx, CURLREQ *req) 
{
  HTTP_CODE code;
  HTTP_CODE *cres, *code_p=&code;
  CURLcode res;
  char *s;
  int d;
  struct curl_slist *cookies = NULL;
  double total;
  size_t bsz;
  
  duk_push_object(ctx);

  //duk_push_boolean(ctx,1);
  //duk_put_prop_string(ctx,-2,"ok");

  /* status text and status code */
  curl_easy_getinfo(req->curl, CURLINFO_RESPONSE_CODE, &d);
  code.code=d;
  cres=bsearch(code_p,http_codes,nHttpCodes,sizeof(HTTP_CODE),compare_hcode);

  if(cres != (HTTP_CODE*)NULL) 
    duk_push_string(ctx,cres->msg);
  else
    duk_push_string(ctx,"Unknown Status Code");

  duk_put_prop_string(ctx,-2,"statusText");
  duk_push_int(ctx,(int)d);
  duk_put_prop_string(ctx,-2,"status");

  /* add to errors if 400 or above */
  if(d>399) {
    char *s=(char*)duk_get_string(ctx,-1);
    duk_push_sprintf(ctx,"%d %s",d,(cres != (HTTP_CODE*)NULL)?cres->msg:"Unknown Status Code" );
    duk_curl_push_err(ctx);
  }


  /* the doc */
  bsz=strlen((req->body).text);
  if (bsz==(req->body).size) {
    duk_push_string(ctx,(req->body).text);
    duk_put_prop_string(ctx,-2,"text");
  }
  else
  /* it's binary */
  {
    unsigned char *b;
    duk_push_fixed_buffer(ctx,(req->body).size);
    b=(unsigned char *) duk_require_buffer_data(ctx, -1, &bsz);
    memcpy(b,(req->body).text,bsz);
    duk_put_prop_string(ctx,-2,"binary");
    duk_push_string(ctx,"binary");
    duk_put_prop_string(ctx,-2,"text");
  }
  /* the actual url, in case of redirects */
  curl_easy_getinfo(req->curl, CURLINFO_EFFECTIVE_URL, &s);
  duk_push_string(ctx,s);
  duk_put_prop_string(ctx,-2,"effectiveUrl");
  
  /* the request url, in case of redirects */
  duk_push_string(ctx,(req->sopts).url);
  duk_put_prop_string(ctx,-2,"url");
  
  /* local IP address */
  curl_easy_getinfo(req->curl, CURLINFO_LOCAL_IP, &s);
  duk_push_string(ctx,s);
  duk_put_prop_string(ctx,-2,"localIP");
  
  /* local port */
  curl_easy_getinfo(req->curl, CURLINFO_LOCAL_PORT, &d);
  duk_push_int(ctx,d);
  duk_put_prop_string(ctx,-2,"localPort");
  
  /* primary IP address */
  curl_easy_getinfo(req->curl, CURLINFO_PRIMARY_IP, &s);
  duk_push_string(ctx,s);
  duk_put_prop_string(ctx,-2,"primaryIP");
  
  /* primary port */
  curl_easy_getinfo(req->curl, CURLINFO_PRIMARY_PORT, &d);
  duk_push_int(ctx,d);
  duk_put_prop_string(ctx,-2,"primaryPort");
  
  /* headers, unparsed */
  if ((req->header).text==(char*)NULL)
    duk_push_string(ctx,"");
  else
    duk_push_string(ctx,(req->header).text);
  duk_put_prop_string(ctx,-2,"rawHeader");
  
  /* headers parsed into object */
  duk_push_object(ctx);
  duk_curl_parse_headers(ctx,(req->header).text);
  duk_put_prop_string(ctx,-2,"headers");

  /* http version used */
  curl_easy_getinfo(req->curl, CURLINFO_HTTP_VERSION, &d);
  switch(d){
    case CURL_HTTP_VERSION_1_0: {duk_push_string(ctx,"HTTP 1.0");break;}
    case CURL_HTTP_VERSION_1_1: {duk_push_string(ctx,"HTTP 1.1");break;}
    case CURL_HTTP_VERSION_2_0: {duk_push_string(ctx,"HTTP 2.0");break;}
    /* case CURL_HTTP_VERSION_3:   {duk_push_string(ctx,"HTTP 3.0");break;} */
    case 0: {duk_push_string(ctx,"");break;}
  }
  duk_put_prop_string(ctx,-2,"httpVersion");

  /* total time for request */
  curl_easy_getinfo(req->curl, CURLINFO_TOTAL_TIME, &total);
  duk_push_number(ctx,(duk_double_t)total);
  duk_put_prop_string(ctx,-2,"totalTime");

  /* only works when cookie jar is set? */
  res=curl_easy_getinfo(req->curl, CURLINFO_COOKIELIST, &cookies);
  if(!res && cookies) {
    int i=0;
    struct curl_slist *each = cookies;
    duk_push_array(ctx);
    while(each) {
      duk_push_string(ctx,each->data);
      duk_put_prop_index(ctx,-2,i++);
      each = each->next;
    }
    duk_put_prop_string(ctx,-2,"cookies");
    curl_slist_free_all(cookies);
  }
  /* return the http code */
  return(d);
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  RETTXT *mem = (RETTXT *)userp;
 
  char *ptr = realloc(mem->text, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */ 
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  mem->text = ptr;
  memcpy(&(mem->text[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->text[mem->size] = 0;
 
  return realsize;
}

static size_t
WriteHeaderCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  RETTXT *mem = (RETTXT *)userp;

  /* overwrite old headers if we get more due to redirect */
  if(!strncmp(contents,"HTTP/",5))
    mem->size=0;
 
  REMALLOC( mem->text, (mem->size + realsize + 1));

  memcpy(&(mem->text[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->text[mem->size] = '\0';

  return realsize;
}

void duk_curl_setopts(duk_context *ctx, CURL *curl, int idx, CSOS *sopts)
{
  CURL_OPTS opts;
  CURL_OPTS *ores, *opts_p=&opts;
  int funcerr=0;
  char *msg="";
  
  duk_enum(ctx, (duk_idx_t)idx, DUK_ENUM_SORT_ARRAY_INDICES);
  while (duk_next(ctx, -1 /* index */, 1 /* get_value also */))
  {
    const char *op=duk_to_string(ctx,-2);
    opts.optionName=(char*)op;
    ores=bsearch(opts_p,curl_options,nCurlOpts,sizeof(CURL_OPTS),compare_copts);

    /* if we find a function for the javascript option, call the appropriate function
       with the appropriate option from curl_options.  The value (if used) will be on 
       top of the ctx stack and accessed from within the function
    */
    if(ores != (CURL_OPTS*)NULL) 
    {
      funcerr=(ores->func)(ctx,curl,ores->subopt,sopts,ores->option);
      if(funcerr)
      {
        duk_push_sprintf(ctx,"option '%s': %s", op, operrors[funcerr]);
        duk_curl_push_err(ctx);
      }
    }
    else
    {
      duk_push_sprintf(ctx,"unknown option '%s'",op);
      duk_curl_push_err(ctx);
    }
    duk_pop_2(ctx);
  }
}




void clean_req( CURLREQ *req)
{
  int i;
  CSOS *sopts=&(req->sopts);
  /* where new_curlreq clones, don't free memory refs from shared pointers
     they will still be in use from the original req */
  if(!req->isclone)
  {
    for (i=0;i<sopts->nslists;i++) curl_slist_free_all(sopts->slists[i]);
    free(sopts->postdata);
    if(sopts->mime!=(curl_mime *)NULL) curl_mime_free(sopts->mime);
  }
  curl_easy_cleanup(req->curl);
  free(sopts->url);
  free((req->body).text);
  free((req->header).text);
  free(req->errbuf);
  free(req);
}

CURLREQ * new_curlreq(char *url){
  CURLREQ *ret=(CURLREQ*)NULL;
    
  REMALLOC(ret,sizeof(CURLREQ));
  ret->curl=(CURL*)NULL;
  ret->errbuf=(char*)NULL;
  ret->isclone=0;
  (ret->sopts).httpauth=0L;
  (ret->sopts).proxyauth=0L;
  (ret->sopts).postredir=0L;
  (ret->sopts).nslists=0;
  (ret->sopts).headerlist=-1;
  (ret->sopts).ssloptions=0L;
  (ret->sopts).proxyssloptions=0L;
  (ret->sopts).postdata=(char *)NULL;
  (ret->sopts).url=url;
  (ret->sopts).mime=(curl_mime *)NULL;

  
  (ret->body).size=(ret->header).size=0;
  (ret->body).text=(ret->header).text=(char*)NULL;
  
  REMALLOC(ret->errbuf,CURL_ERROR_SIZE);
  *(ret->errbuf)='\0';
  
  return(ret);
}


CURLREQ * new_request(char *url, CURLREQ *cloner, duk_context *ctx, int options_idx)
{
  CURLREQ * req=new_curlreq(url);
  
  if (cloner != (CURLREQ*)NULL)
  {
    req->curl=curl_easy_duphandle(cloner->curl);
    curl_easy_setopt(req->curl, CURLOPT_ERRORBUFFER, req->errbuf);
    
    curl_easy_setopt(req->curl, CURLOPT_WRITEDATA, (void *)&(req->body));
    curl_easy_setopt(req->curl, CURLOPT_HEADERDATA, (void *)&(req->header));
    curl_easy_setopt(req->curl, CURLOPT_URL, (req->sopts).url);

    req->sopts=cloner->sopts;    
    req->isclone=1;
    /* we just overwrote url, set it again */
    (req->sopts).url=url;
    return(req);    
  }

  req->curl = curl_easy_init();

  if(req->curl) {
 
    /* send all body data to this function  */ 
    curl_easy_setopt(req->curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
 
    /* we pass our RETTXT struct to the callback function */ 
    curl_easy_setopt(req->curl, CURLOPT_WRITEDATA, (void *)&(req->body));

    /* send all header data to this function  */ 
    curl_easy_setopt(req->curl, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
 
    /* we pass our RETTXT struct to the callback function */ 
    curl_easy_setopt(req->curl, CURLOPT_HEADERDATA, (void *)&(req->header));

    curl_easy_setopt(req->curl, CURLOPT_USERAGENT, "libcurl-duktape/0.1");

    /* provide a buffer to store errors in */
    curl_easy_setopt(req->curl, CURLOPT_ERRORBUFFER, req->errbuf);

    /* in /usr/bin/curl command line, this is normally on unless --no-keepalive is set */
    curl_easy_setopt(req->curl, CURLOPT_TCP_KEEPALIVE, 1L);

    if(options_idx >-1)
      duk_curl_setopts(ctx,req->curl,options_idx, &(req->sopts));
    
    /* set the url, which may have been altered in duk_curl_setopts to include a query string */
    curl_easy_setopt(req->curl, CURLOPT_URL, (req->sopts).url);

    if((req->sopts).headerlist !=-1)
      curl_easy_setopt(req->curl, CURLOPT_HTTPHEADER,(req->sopts).slists[(req->sopts).headerlist]);
  }

  return(req);
}

duk_ret_t addurl(duk_context *ctx) {
  CURLREQ *req, *preq;
  CURLM *cm;
  char *u=strdup(duk_to_string(ctx,0));

  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("req"));
  req=(CURLREQ *)duk_get_pointer(ctx, -1);
  duk_pop(ctx);
  duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("cm"));
  cm=(CURLM *)duk_get_pointer(ctx, -1);
  duk_pop(ctx);
  
  preq=new_request(u,req,ctx,0/*0 not used when cloning*/);

  if(!preq)
  {
    duk_push_sprintf(ctx,"Failed to get new curl handle while getting %s", u);
    duk_curl_push_err(ctx);
    duk_push_boolean(ctx,0);
    return 1;
  }

  curl_easy_setopt(preq->curl, CURLOPT_PRIVATE,preq);
  curl_multi_add_handle(cm, preq->curl);      
  
  duk_push_boolean(ctx,1);
  return 1;
}


duk_ret_t duk_curl_exe(duk_context *ctx) {
  int i, options_idx=-1, func_idx=-1, array_idx=-1;
  char *url=(char*)NULL;
  CURLREQ *req;
  CURLcode res;

  /* clear any previous errors */
  duk_push_this(ctx);
  duk_del_prop_string(ctx,-1,"errors");
  duk_pop(ctx);

  duk_curl_check_global(ctx);

  for (i=0; i<4; i++) 
  {
    int vtype=duk_get_type(ctx,i);
    switch(vtype) {
      case DUK_TYPE_STRING:
      {
        url=strdup(duk_get_string(ctx,i));
        break;
      }
      case DUK_TYPE_OBJECT:
      {
        /* array of parameters*/
        if (duk_is_array(ctx,i))
          array_idx=i;
                  
        /* argument is a function */
        else if(duk_is_function(ctx,i) )
          func_idx=i;

        /* object of settings */
        else
          options_idx=i;

        break;
      } /* case */
    } /* switch */
  } /* for */
 
  /* parallel requests */
  
  if(array_idx>-1)
  /* array of urls, do parallel search */
  {
    CURLM *cm = curl_multi_init();
    CURLMsg *msg;
    CURLREQ *preq;
    CURLMcode mc;
    int still_alive=1, msgs_left = -1;

    if(func_idx==-1)
    {
      duk_push_string(ctx,"Called with array (implying parallel fetch) but no callback function supplied");
      duk_curl_push_err(ctx);
      return 1;
    }
    

    i=0;
    while(duk_has_prop_index(ctx,array_idx,i))
    {
      char *u;
      CURLREQ *preq;
      duk_get_prop_index(ctx,array_idx,i);
      u=strdup(duk_to_string(ctx,-1));
      duk_pop(ctx);
      if(!i) 
      {
        preq=req=new_request(u,NULL,ctx,options_idx);

        /* set up for being able to add another url in the callback */
        duk_push_this(ctx);
        duk_push_pointer(ctx,(void*)cm);
        duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("cm"));
        duk_push_pointer(ctx,(void*)req);
        duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("req"));
        duk_push_c_function(ctx,addurl,1);
        duk_put_prop_string(ctx,-2,"addurl");
        duk_pop(ctx);
        /* end setup */
      }
      else
        preq=new_request(u,req,ctx,options_idx);
      
      if(!preq)
      {
        duk_push_sprintf(ctx,"Failed to get new curl handle while getting %s", u);
        duk_curl_push_err(ctx);
        duk_push_boolean(ctx,0);
        return 1;
      }

      curl_easy_setopt(preq->curl, CURLOPT_PRIVATE,preq);
      curl_multi_add_handle(cm, preq->curl);      
       
      i++;
    }/* while */

    do {
      CURLREQ *preq;
      curl_multi_perform(cm, &still_alive);
      
      while((msg = curl_multi_info_read(cm, &msgs_left))) 
      {
        if(msg->msg == CURLMSG_DONE) {
          CURL *eh = msg->easy_handle;
          curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &preq);
          res=msg->data.result;
          duk_dup(ctx,func_idx);
          duk_push_this(ctx);

          if(res != CURLE_OK)
          {
            duk_push_sprintf(ctx,"curl failed for '%s': %s", (preq->sopts).url,curl_easy_strerror(res));
            duk_curl_push_err(ctx);
            duk_curl_ret_blank(ctx);
          }
          else
          {
            /* assemble our response */
            duk_curl_push_res(ctx, preq);
            /* add error strings, if any */
            duk_push_string(ctx,preq->errbuf);
            duk_curl_push_err(ctx);
            duk_curl_copy_errors(ctx,-1);
                        
          }  /* if CURLE_OK */

          /* call the callback */
          duk_call_method(ctx,1);
          
          /* clean up */
          if(preq!=req) /* need to free original req last */
            clean_req(preq);       
        } /* CURLMSG_DONE */
      } /* while */
    } while (still_alive); /* do */

    clean_req(req);/* free the original */
    curl_multi_cleanup(cm);
    duk_push_boolean(ctx,1);
    return(1);
  }
  /* end parallel requests */
  else if (url==(char*)NULL)
  {
    duk_push_string(ctx,"No url provided");
    duk_curl_push_err(ctx);
    duk_curl_ret_blank(ctx);
    return 1;
  }

  /* single request */ 
  
  req=new_request(url,NULL,ctx,options_idx);
  if(req)
  {
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(req->curl);
    /* Check for errors */ 
    if(func_idx>-1) 
    {
      duk_dup(ctx,func_idx);
      duk_push_this(ctx);
    }
    if(res != CURLE_OK)
    {
      duk_push_sprintf(ctx,"curl failed: %s", curl_easy_strerror(res));
      duk_curl_push_err(ctx);
      duk_curl_ret_blank(ctx);

      /* cleanup */
      clean_req(req);
      if(func_idx>-1) 
      {
        duk_call_method(ctx,1);
        duk_push_boolean(ctx,0);
      }
      return (1);
    }
    else
    {
      /* if http res code 400 or greater, and using callback function, return false */
      i=(duk_curl_push_res(ctx, req) > 399)?0:1;
    }
    /* add error strings, if any */
    duk_push_string(ctx,req->errbuf);
    duk_curl_push_err(ctx);
    duk_curl_copy_errors(ctx,-1);
        
    /* cleanup */ 
    clean_req(req);

  }
  else 
  {
    i=0;/* return false */
    duk_push_string(ctx,"could not get curl handle");
    duk_curl_push_err(ctx);
    duk_curl_ret_blank(ctx);
  }

//  curl_global_cleanup();
  if(func_idx>-1) 
  {
    duk_call_method(ctx,1);
    duk_push_boolean(ctx,i);/* return bool */
  }

  return 1;

}


/* **************************************************
   Initialize Curl into global object. 
   ************************************************** */
//void duk_curl_init(duk_context *ctx) {
//  duk_push_c_function(ctx, duk_curl_exe, 4 /*nargs*/);  /* [ Sql proto fn_query ] */
//  duk_put_global_string(ctx, "curl");  /* -> stack: [ ] */
//}






static const duk_function_list_entry curl_funcs[] = {
    { "fetch",         duk_curl_exe, 4 /*nargs*/ },
    { "objectToQuery", duk_curl_object2q,2},
    { "encode",        duk_curl_encode,1},
    { "decode",        duk_curl_decode,1},
    { "cleanup",       duk_curl_global_cleanup,0},
    { NULL, NULL, 0 }
};

static const duk_number_list_entry curl_consts[] = {
    { NULL, 0.0 }
};

duk_ret_t dukopen_module(duk_context *ctx) {
    duk_push_object(ctx);
    duk_put_function_list(ctx, -1, curl_funcs);
    duk_put_number_list(ctx, -1, curl_consts);

    return 1;
}

