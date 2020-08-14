#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include "misc.h"
#include "../core/duktape.h"
#include "../../rp.h"
extern char **environ;

/* utility function for global object:
      var buf=toBuffer(val); //fixed if string, same type if already buffer
        or
      var buf=toBUffer(val,"[dynamic|fixed]"); //always converted to type
*/
#define BUFREADSZ 4096
#define homesubdir "/.rampart/"

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
/*  Find file searching standard directories */

RPPATH rp_find_path(char *file, char *subdir)
{
    int nlocs=2;
    /* look in these locations and in ./ */
    char *locs[nlocs];
    char *home=getenv("HOME");
    char *rampart_path=getenv("RAMPART_PATH");
    char homedir[strlen(home)+strlen(homesubdir)+1];
    char *loc="./";
    char *sd= (subdir)?subdir:"";
    RPPATH ret;
    char path[PATH_MAX];
    int i=0;
    struct stat sb;

    /* look for it as given before searching paths */
    if (stat(file, &sb) != -1)
    {
        ret.stat=sb;
        if(!realpath(file,ret.path))
            strcpy(ret.path,file);
        return ret;        
    }

//printf("looking for file %s%s\n",subdir,file);
    if(!home || access(home, R_OK)==-1) home="/tmp";

    strcpy(homedir,home);
    strcat(homedir,homesubdir);
    locs[0]=homedir;
    locs[1]=(rampart_path)?rampart_path:RP_INST_PATH;

    /* start with cur dir "./" */
    strcpy(path,loc);
    strcat(path,file);
//printf("first check\n");
    while(1) {
//printf("checking %s\n",path);
        if (stat(path, &sb) != -1)
{
//printf("break at i=%d\n",i);
            break;
}

//printf ("not found\n");
        if(i==nlocs)
        {
            i++;
            break;
        }
        strcpy(path,locs[i]);
        /* in case RAMPART_PATH doesn't have trailing '/' */
        if(locs[i][strlen(locs[i])-1] != '/')
            strcat(path,"/");
        strcat(path,sd);
        strcat(path,file);
//printf("new check(%d): %s\n",i,path);
        i++;
    }

    if(i<=nlocs){
        ret.stat=sb;
        if(!realpath(path,ret.path))
            strcpy(ret.path,path);
//printf("FOUND %s\n",ret.path);
    }
    else ret.path[0]='\0';

    return ret;
}

int rp_mkdir_parent(const char *path, mode_t mode)
{
    char _path[PATH_MAX], *p;
    mode_t old_umask=umask(0);
    errno=0;
    strcpy(_path, path);

    /* Move through the path string to recurisvely create directories */
    for (p = _path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(_path, mode) != 0)
            {
                if (errno != EEXIST)
                {
                    (void)umask(old_umask);
                    return 1;
                }
            }

            *p = '/';
        }
    }

    if (mkdir(path, mode) != 0)
    {
        if (errno != EEXIST)
        {
            (void)umask(old_umask);
            return -1;
        }
    }
    (void)umask(old_umask);
    return 0;
}

/* get path to a file to be written to the home in .rampart 
   if path/file exists, also return stat
*/
RPPATH rp_get_home_path(char *file, char *subdir)
{
    char *home=getenv("HOME");
    char homedir[strlen(home)+strlen(homesubdir)+1];
    char *sd= (subdir)?subdir:"";
    RPPATH ret;
    mode_t mode=0755;

    if( !home || access(home, W_OK)==-1 )
    {
         home="/tmp";
         mode=0777;
    }

    strcpy(homedir,home);
    strcat(homedir,homesubdir);
    strcat(homedir,sd);

    if( rp_mkdir_parent(homedir,mode)==-1)
    {
        ret.path[0]='\0';
        return ret;
    }

    strcpy(ret.path,homedir);
    strcat(ret.path,file);
    if (stat(ret.path, &ret.stat) == -1)
    {
        ret.stat=(struct stat){0};
    }

    return ret;
}

/* ********** some string functions *************************** */

/* return s+adds  s must be a malloced string or ->NULL*/
char *strcatdup(char *s, char *adds)
{
    int freeadds = 0, sl, al;
    if ((adds == (char *)NULL) || (*adds == '\0'))
    {
        if (s == (char *)NULL)
            s = strdup("");
        return (s);
    }

    if (s == (char *)NULL)
        sl = 0;
    else
        sl = strlen(s);

    /* if its the same string or a substring */
    if (adds >= s && adds < (s + sl))
    {
        adds = strdup(adds);
        freeadds = 1;
    }

    if (s != (char *)NULL)
    {
        al = strlen(adds);
        REMALLOC(s, al + sl + 1);
        strcpy(s + sl, adds);
    }
    else
        s = strdup(adds);

    if (freeadds)
        free(adds);

    return (s);
}

/* return s+c+adds  s must be a malloced string or ->NULL*/
char *strjoin(char *s, char *adds, char c)
{
    int freeadds = 0, sl, al=0;
    if ((adds == (char *)NULL) || (*adds == '\0'))
    {
        if (s == (char *)NULL)
            s = strdup("");
        return (s);
    }

    if (s == (char *)NULL)
        sl = 0;
    else
        sl = strlen(s);

    /* if its the same string or a substring */
    if (adds >= s && adds < (s + sl))
    {
        adds = strdup(adds);
        freeadds = 1;
    }

    al = strlen(adds);
    if (s != (char *)NULL)
    {
        REMALLOC(s, al + sl + 2);
        *(s + sl) = c;
        strcpy(s + sl + 1, adds);
    }
    else
    {
        REMALLOC(s, al + 2);
        *s = c;
        strcpy(s + 1, adds);
    }
    if (freeadds)
        free(adds);

    return (s);
}
/* ***************buffer to string and string to buffer ******************* */

duk_ret_t duk_rp_strToBuf(duk_context *ctx)
{
    duk_size_t sz;
    const char *opt = duk_to_string(ctx, 1);

    if (!strcmp(opt, "dynamic"))
        duk_to_dynamic_buffer(ctx, 0, &sz);
    else if (!strcmp(opt, "fixed"))
        duk_to_fixed_buffer(ctx, 0, &sz);
    else
        duk_to_buffer(ctx, 0, &sz);

    duk_pop(ctx);
    return 1;
}

duk_ret_t duk_rp_bufToStr(duk_context *ctx)
{

    duk_buffer_to_string(ctx, 0);

    return 1;
}


duk_ret_t duk_process_exit(duk_context *ctx)
{
    int exitval=duk_get_int_default(ctx,0,0);
    duk_destroy_heap(ctx);
    exit(exitval);
    return 0;
}

void duk_rp_toHex(duk_context *ctx, duk_idx_t idx, int ucase)
{
    unsigned char *buf,*end;
    char *out=NULL,*p;
    duk_size_t sz;
    if (ucase)
        ucase=7;
    else
        ucase=39;

    duk_to_buffer(ctx,idx,&sz);

    buf=(unsigned char *)duk_get_buffer_data(ctx,idx,&sz);

    end=buf+sz;
    DUKREMALLOC(ctx,out,sz*2);
    
    p=out;
    /* conver to lowercase hex */
    while(buf<end)
    {
        int nibval;
        
        nibval=*buf/16 + 48;
        if(nibval>57) nibval+=ucase;
        *p=nibval;
        p++;
        
        nibval=*buf%16 +48;
        if(nibval>57) nibval+=ucase;
        *p=nibval;
        p++;

        buf++;
    }
    
    duk_push_lstring(ctx,out,sz*2);
    duk_replace(ctx,idx);
    free(out);
}

duk_ret_t duk_rp_hexify(duk_context *ctx)
{
    if(duk_get_boolean(ctx,1))
    {
        duk_pop(ctx);
        duk_rp_toHex(ctx,0,1);
    }
    else
    {
        duk_pop(ctx);
        duk_rp_toHex(ctx,0,0);
    }
    return 1;
}

#define hextonib(bval) ({\
    int bv=(bval);\
    if(bv>96)bv-=32;\
    if(bv>64&&bv<71) bv-=55;\
    else if(bv>47&&bv<58)bv-=48;\
    else{duk_push_string(ctx,"hexToBuf(): invalid input");(void)duk_throw(ctx);}\
    (unsigned char) bv;\
})

void duk_rp_hexToBuf(duk_context *ctx, duk_idx_t idx)
{
    const char *s=duk_require_string(ctx,idx);
    size_t len=strlen(s);
    unsigned char *buf;

    len++; /* if have an extra nibble, round up */
    len/=2;
    
    buf=(unsigned char*)duk_push_fixed_buffer(ctx,(duk_size_t)len);
    
    while(*s)
    {
        unsigned char bval;

        bval=16*hextonib((int)*s);        
        s++;

        if(*s)
        {
            bval+=hextonib((int)*s);
            s++;
        }
        *buf=bval;
        buf++;
    }
}

duk_ret_t duk_rp_dehexify(duk_context *ctx)
{
    duk_rp_hexToBuf(ctx,0);
    return 1;
}



/* ********************* process.exit, process.env ********************** *
   process.args is in main
*/
void duk_process_init(duk_context *ctx)
{
    int i=0;
    char *env;

    duk_push_global_object(ctx);
    /* get global symbol "process" */
    if(!duk_get_prop_string(ctx,-1,"process"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    
    duk_push_object(ctx); /* process.env */

    while ( (env=environ[i]) != NULL )
    {
        int len;
        char *val=strchr(env,'=');
        
        if(val)
        {
            len=val-env;
            val++;
            duk_push_lstring(ctx,env,(duk_size_t)len);
            duk_push_string(ctx,val);
            duk_put_prop(ctx,-3);
        }
        i++;
    }
    duk_put_prop_string(ctx,-2,"env");

    duk_push_c_function(ctx,duk_process_exit,1);
    duk_put_prop_string(ctx,-2,"exit");

    duk_put_prop_string(ctx,-2,"process");
    duk_pop(ctx);
}


/* **************************************************************************
   This url(en|de)code is public domain from https://www.geekhideout.com/urlcode.shtml 
   ************************************************************************** */

/* Converts a hex character to its integer value */

static char from_hex(char ch)
{
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
static char to_hex(char code)
{
    static char hex[] = "0123456789abcdef";
    return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *duk_rp_url_encode(char *str, int len)
{
    char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;

    if (len < 0)
        len = strlen(str);

    while (len)
    {
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
char *duk_rp_url_decode(char *str, int len)
{
    char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;

    if (len < 0)
        len = strlen(str);

    while (len)
    {
        if (*pstr == '%' && len > 2)
        {
            *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
            pstr += 2;
            len -= 2;
        }
        else if (*pstr == '+')
        {
            *pbuf++ = ' ';
        }
        else
        {
            *pbuf++ = *pstr;
        }
        pstr++;
        len--;
    }
    *pbuf = '\0';
    return buf;
}
/* **************************************************************************
   END url(en|de)code, public domain from https://www.geekhideout.com/urlcode.shtml 
   ************************************************************************** */

/* **************** object to http query string ********************************* */

const char *duk_curl_to_strOrJSON(duk_context *ctx, duk_idx_t idx)
{
    if (duk_is_object(ctx, idx))
    {
        const char *s;
        duk_dup(ctx, idx);
        s = duk_json_encode(ctx, -1);
        return s;
    }
    /* expecting a translation (as above) or a copy on the stack */
    duk_dup(ctx, idx);
    return duk_to_string(ctx, -1);
}


#define ARRAYREPEAT 0
#define ARRAYBRACKETREPEAT 1
#define ARRAYCOMMA 2
#define ARRAYJSON 3
/* *****************************************************
   serialize object to query string 
   return val needs to be freed
   ***************************************************** */
char *duk_rp_object2querystring(duk_context *ctx, duk_idx_t qsidx)
{
    int i = 0, atype = ARRAYREPEAT, end = qsidx + 2;
    const char *arraytype;
    char *ret = (char *)NULL, *s;

    /* 
     look at next two stack items for a string,
     if i is -1, assume no string and don't wrap 
     around and go to 0 
  */
    if (qsidx != -1)
    {
        for (i = qsidx; i < end; i++)
        {
            if (duk_is_string(ctx, i))
            {
                arraytype = duk_get_string(ctx, i);

                if (!strncmp("bracket", arraytype, 7))
                    atype = ARRAYBRACKETREPEAT;
                else if (!strncmp("comma", arraytype, 5))
                    atype = ARRAYCOMMA;
                else if (!strcmp("json", arraytype))
                    atype = ARRAYJSON;

                duk_remove(ctx, i);
                /* consequence of counting backwards and removing string */
                if (qsidx < 0 && i != qsidx)
                    qsidx++;
            }
        }
    }
    i = 0;
    if (duk_is_object(ctx, qsidx) && !duk_is_array(ctx, qsidx))
    {

        duk_enum(ctx, qsidx, DUK_ENUM_SORT_ARRAY_INDICES);
        while (duk_next(ctx, -1, 1))
        {
            const char *stv; /* value from stack */
            stv = duk_to_string(ctx, -2);
            //s=curl_easy_escape(curl,stv,0);
            s = duk_rp_url_encode((char *)stv,-1);
            if (i)
                ret = strjoin(ret, s, '&');
            else
                ret = strdup(s);
            /* don't free s just yet */

            if (atype != ARRAYJSON && duk_is_array(ctx, -1))
            {
                int j = 0;
                char *v = (char *)NULL;
                while (duk_has_prop_index(ctx, -1, j))
                {

                    duk_get_prop_index(ctx, -1, j);
                    stv = duk_curl_to_strOrJSON(ctx, -1);
                    v = duk_rp_url_encode((char *)stv,-1);
                    duk_pop_2(ctx); /* get_prop_index and strOrJson both leave items on stack */
                    switch (atype)
                    {

                        /* var1[]=a&var1[]=b */
                    case ARRAYBRACKETREPEAT:
                    {
                        if (!j)
                        {
                            s = strcatdup(s, "%5B%5D");
                            ret = strcatdup(ret, "%5B%5D");
                        }
                        /* no break, fall through */
                    }

                    /* var1=a&var1=b */
                    case ARRAYREPEAT:
                    {
                        if (j)
                            ret = strjoin(ret, s, '&');

                        ret = strjoin(ret, v, '=');
                        break;
                    }

                    case ARRAYCOMMA:
                        /* var1=a,b */
                        {
                            if (j)
                                ret = strjoin(ret, v, ',');
                            else
                                ret = strjoin(ret, v, '=');

                            break;
                        }
                    }
                    free(v);
                    j++;
                }
                free(s);
            }
            else
            {
                free(s);
                //curl_free(s);
                stv = duk_curl_to_strOrJSON(ctx, -1);

                //s=curl_easy_escape(curl,stv,0);
                s = duk_rp_url_encode((char *)stv,-1);
                ret = strjoin(ret, s, '=');
                duk_pop(ctx); /* duk_curl_to_strOrJSON */
                //curl_free(s);
                free(s);
            }

            i = 1;
            duk_pop_2(ctx);
        } /* while */
    }
    //else error?
    duk_remove(ctx, qsidx);

    //  curl_easy_cleanup(curl);
    return (ret);
}

/* ************** query to object **************** */

void duk_rp_push_lstring_or_jsonob(duk_context *ctx, char *s, size_t l)
{

    if( 
        ( *s=='{' && *(s+l-1)=='}')
        ||
        ( *s=='[' && *(s+l-1)==']')
    )
    {
        duk_get_global_string(ctx,"JSON");
        duk_get_prop_string(ctx,-1,"parse");
        duk_remove(ctx,-2);
        duk_push_lstring(ctx,s,l);
        if(duk_pcall(ctx,1) != 0)
        {
            duk_pop(ctx);//remove error
            duk_push_lstring(ctx,s,l); //put string back as is
        }
        return;
    }
    duk_push_lstring(ctx,s,l);
}

static void pushqelem(duk_context *ctx, char *s, size_t l)
{
    char *eq=(char *)memmem(s,l,"=",1);

    if (eq)
    {
        size_t keyl=eq-s;
        size_t vall=l-(keyl+1);
        char *key=duk_rp_url_decode(s,keyl);
        char *val=duk_rp_url_decode(eq+1,vall);
        duk_size_t arrayi;

        keyl=strlen(key);
        vall=strlen(val);

        if( keyl > 3 && *(key+keyl-1)==']' && *(key+keyl-2)=='[')
        {   /* its an array with brackets */
            keyl-=2;
            duk_size_t arrayi;

            /* put in array from the beginning */
            if(!duk_get_prop_lstring(ctx,-1,key,keyl))
            {
                duk_pop(ctx);
                duk_push_array(ctx);
            }
            
            arrayi=duk_get_length(ctx,-1);
            duk_rp_push_lstring_or_jsonob(ctx,val,vall);
            duk_put_prop_index(ctx,-2,(duk_uarridx_t)arrayi);
            duk_put_prop_lstring(ctx,-2,key,keyl);
            //printf("array: '%.*s'='%.*s'\n", keyl,key,vall,val);
        }
        else
        {
            /* check if exists already, if so, make array or use array */
            if(!duk_get_prop_lstring(ctx,-1,s,keyl))
            {
                duk_pop(ctx);
                duk_rp_push_lstring_or_jsonob(ctx,val,vall);
            }
            else
            {
                if(!duk_is_array(ctx,-1))
                {   /* make array and push prev value to index 0 */
                    duk_push_array(ctx);
                    duk_pull(ctx,-2);
                    duk_put_prop_index(ctx,-2,0);
                    arrayi=1;
                }
                else
                    arrayi=duk_get_length(ctx,-1);
                
                duk_rp_push_lstring_or_jsonob(ctx,val,vall);
                duk_put_prop_index(ctx,-2,(duk_uarridx_t)arrayi);
            }
            duk_put_prop_lstring(ctx,-2,key,keyl);
            //printf("maybe array: '%.*s'='%.*s'\n", keyl,key,vall,val);
        }
        free(key);
        free(val);
    }
}

void duk_rp_querystring2object(duk_context *ctx, char *q)
{
    char *s=q,*e=q;
    
    duk_push_object(ctx);
    while (e)
    {
        e++;
        if(!*e || *e=='&')
        {
            size_t l=e-s;
            
            pushqelem(ctx,s,l);
            //printf("%.*s\n",(int)l,s);
            if(!*e)
                break;
            s=e+1;
        }
    }
}

duk_ret_t duk_rp_query2o(duk_context *ctx)
{
    char *s= (char *)duk_require_string(ctx,0);
    duk_rp_querystring2object(ctx,s);    
    return 1;
}

/* *****************************************
   for use directly in JS
   rampart.objectToQuery({...});
******************************************** */
duk_ret_t duk_rp_object2q(duk_context *ctx)
{
    char *s = duk_rp_object2querystring(ctx, 0);
    duk_push_string(ctx, s);
    free(s);
    return 1;
}

/* export functions in an object to global */
duk_ret_t duk_rp_globalize(duk_context *ctx)
{

    if( duk_is_array(ctx,1) )
    {
        duk_enum(ctx,1,DUK_ENUM_ARRAY_INDICES_ONLY);
        while (duk_next(ctx, -1, 1)) 
        {
            const char *pname=duk_get_string(ctx,-1);
            duk_get_prop_string(ctx,0,pname);
            duk_put_global_string(ctx,pname);
            duk_pop_2(ctx);
        }
    }
    else
    {
        duk_enum(ctx,0,0);
        while (duk_next(ctx, -1, 1)) 
        {
            const char *pname=duk_get_string(ctx,-2);
            duk_put_global_string(ctx,pname);
            duk_pop(ctx);
        }
    }
    return 0;
}

/* file utils */

/* rampart.utils.readFile({
       filename: "./filename", //required
       offset: -20,            //default 0.     Negative number is from end of file.
       length: 50,             //default 0.     If less than 1, length is calculated from position from end of file.
       retString: true         //default false. Normally returns a buffer. String may be truncated if file contains nulls.
   });
    ALSO may be called as:
    rampart.utils.readFile("./filename",-20,50,true);
*/
duk_ret_t duk_rp_read_file(duk_context *ctx)
{
    const char *filename=NULL;
    int64_t offset=0;
    int64_t length=0;
    duk_idx_t obj_idx=0, top=duk_get_top(ctx);
    int retstring=0;
    FILE *fp;
    void *buf;
    struct stat fstat;
    size_t off;
    size_t nbytes;

    /* get options as separate parameters: readfile("filename",0,0,true); */
    if(duk_is_string(ctx,0))
    {
        filename = duk_get_string(ctx, 0); 

        if (duk_is_number(ctx,1))
        {
            offset=(int64_t) duk_get_number(ctx,1);
            obj_idx++;
        }

        if (duk_is_number(ctx,2))
        {
            length=(int64_t) duk_get_number(ctx,2);
            obj_idx++;
        }
        retstring=duk_get_boolean_default(ctx,3,0); 
    }
    
    while (obj_idx < top)
    {
        if( duk_is_object(ctx,obj_idx))
            break;
        obj_idx++;
    }
    
    if ( obj_idx != top ) 
    {   

        if(duk_get_prop_string(ctx, obj_idx, "file"))
            filename = duk_require_string(ctx, -1);
        duk_pop(ctx);
        
        if(duk_get_prop_string(ctx, obj_idx, "offset"))
            offset=(int64_t) duk_get_number_default(ctx, -1, 0);
        duk_pop(ctx);

        if(duk_get_prop_string(ctx, obj_idx, "length"))
            length = (long)duk_get_number_default(ctx, -1, 0);
        duk_pop(ctx);
    
        if(duk_get_prop_string(ctx, obj_idx, "retString"))
            retstring=duk_require_boolean(ctx,-1);
    }

    if (!filename)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile() - error, no filename provided");
        return duk_throw(ctx);
    }

    if (stat(filename, &fstat) == -1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile(\"%s\") - error accessing: %s", filename, strerror(errno));
        return duk_throw(ctx);
    }
    if(offset < 0)
        offset = (int64_t)fstat.st_size + offset;

    if(length < 1)
        length = ((int64_t)fstat.st_size + length) - offset;

    if( length < 1 )
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile(\"%s\") - negative length puts end of read before offset or start of file", filename);
        return duk_throw(ctx);
    }
    
    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile(\"%s\") - error opening: %s", filename, strerror(errno));
        return duk_throw(ctx);
    }


    if(offset)
    {
        if (fseek(fp, offset, SEEK_SET))
        {
            fclose(fp);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile(\"%s\") - error seeking file: %s", filename, strerror(errno));
            return duk_throw(ctx);
        }
    }
    
    buf = duk_push_fixed_buffer(ctx, length);

    off = 0;
    while ((nbytes = fread(buf + off, 1, length - off, fp)) != 0)
    {
        off += nbytes;
    }

    if (ferror(fp))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readFile(\"%s\") - error reading file: %s", filename, strerror(errno));
        return duk_throw(ctx);
    }
    fclose(fp);

    if(retstring)
        duk_buffer_to_string(ctx,-1);    

    return 1;
}

duk_ret_t duk_rp_readln_finalizer(duk_context *ctx)
{
    duk_push_this(ctx);
    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer"));
    FILE *fp = duk_get_pointer(ctx, -1);
    duk_pop(ctx);
    if (fp)
    {
        fclose(fp);
    }
    return 0;
}


duk_ret_t duk_rp_readln_iter(duk_context *ctx)
{
    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filename"));
    const char *filename = duk_get_string(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer"));
    FILE *fp = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    // already at the end of the iterator
    if (fp == NULL)
    {
        // return object
        duk_push_object(ctx);

        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "value");

        duk_push_boolean(ctx, 0);
        duk_put_prop_string(ctx, -2, "done");
        return 1;
    }

    {
        char *line = NULL;
        size_t len = 0;
        int nread;

        errno = 0;
        nread = getline(&line, &len, fp);
        if (errno)
        {
            free(line);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "readln(): error reading file '%s': %s", filename, strerror(errno));
            return duk_throw(ctx);
        }
        // return object
        duk_push_object(ctx);

        if (nread != -1)
        {
            duk_push_string(ctx, line);
            duk_put_prop_string(ctx, -2, "value");
        }
        else
        {
            duk_push_undefined(ctx);
            duk_put_prop_string(ctx, -2, "value");
        }

        duk_push_boolean(ctx, nread == -1);
        duk_put_prop_string(ctx, -2, "done");

        if (nread == -1)
        {
            duk_push_pointer(ctx, NULL);
            duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));
            fclose(fp);
        }

        free(line);
        return 1;
    }
}

/**
 * Reads a file line by line using getline and javascript iterators.
 * @param {string} filename - the path to the file to be read.
 * @returns {Iterator} an object with a Symbol.iterator.
 */
duk_ret_t duk_rp_readln(duk_context *ctx)
{
    const char *filename = duk_require_string(ctx, -1);
    FILE *fp = fopen(filename, "r");

    if (fp == NULL)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readln(): error opening '%s': %s", filename, strerror(errno));
        return duk_throw(ctx);
    }

    // return object
    duk_push_object(ctx);

    // [Symbol.iterator] function
    duk_push_string(ctx, "(function() { return function getiter(iter) { return (function () { return iter; }); }})()");
    duk_eval(ctx);

    // iterator object
    duk_push_object(ctx);

    // add fp, filename and finalizer
    duk_push_pointer(ctx, fp);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));

    duk_push_string(ctx, filename);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filename"));

    duk_push_c_function(ctx, duk_rp_readln_finalizer, 1);
    duk_set_finalizer(ctx, -2);

    // next
    duk_push_c_function(ctx, duk_rp_readln_iter, 0);
    duk_put_prop_string(ctx, -2, "next");
    duk_call(ctx, 1);

    // iterator function is at top of stack
    duk_put_prop_string(ctx, -2, DUK_WELLKNOWN_SYMBOL("Symbol.iterator"));

    return 1;
}

static duk_ret_t readline_next(duk_context *ctx)
{
    FILE *fp;

    duk_push_this(ctx);

    duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filepointer"));
    fp = duk_get_pointer(ctx, -1);
    duk_pop(ctx);

    // already at the end of the iterator
    if (fp == NULL)
    {
        duk_push_null(ctx);
        return 1;
    }

    {
        char *line = NULL;
        size_t len = 0;
        int nread;

        errno = 0;
        nread = getline(&line, &len, fp);
        if (errno)
        {
            free(line);
            duk_get_prop_string(ctx, -1, DUK_HIDDEN_SYMBOL("filename"));
            duk_push_error_object(ctx, DUK_ERR_ERROR, "readln(): error reading file '%s': %s", duk_get_string(ctx, -1), strerror(errno));
            return duk_throw(ctx);
        }

        if (nread == -1)
        {
            duk_push_pointer(ctx, NULL);
            duk_put_prop_string(ctx, 0, DUK_HIDDEN_SYMBOL("filepointer"));
            fclose(fp);
            duk_push_null(ctx);
        }
        else
            duk_push_string(ctx, line);

        free(line);
        return 1;
    }
}


duk_ret_t duk_rp_readline(duk_context *ctx)
{
    const char *filename;
    FILE *fp;

    filename = duk_require_string(ctx, 0);

    fp = fopen(filename, "r");
    if (fp == NULL)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readLine(): error opening '%s': %s", filename, strerror(errno));
        return duk_throw(ctx);
    }

    duk_push_object(ctx);

    duk_push_string(ctx,filename);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filename"));

    duk_push_pointer(ctx,fp);
    duk_put_prop_string(ctx, -2, DUK_HIDDEN_SYMBOL("filepointer"));

    duk_push_c_function(ctx,readline_next,0);
    duk_put_prop_string(ctx, -2, "next");
    
    duk_push_c_function(ctx,duk_rp_readln_finalizer,0);
    duk_put_prop_string(ctx, -2, "finish");

    return 1;
}

#define DUK_PUT(ctx, type, key, value, idx) \
    {                                       \
        duk_push_##type(ctx, value);        \
        duk_put_prop_string(ctx, idx, key); \
    }

#define DUK_UTIL_STAT_TEST_MODE(mode, test)             \
    duk_ret_t duk_util_stat_is_##mode(duk_context *ctx) \
    {                                                   \
        duk_push_this(ctx);                             \
        duk_get_prop_string(ctx, -1, "mode");           \
        int mode = duk_require_int(ctx, -1);            \
        duk_push_boolean(ctx, test(mode));              \
        return 1;                                       \
    }

DUK_UTIL_STAT_TEST_MODE(block_device, S_ISBLK);
DUK_UTIL_STAT_TEST_MODE(character_device, S_ISCHR);
DUK_UTIL_STAT_TEST_MODE(directory, S_ISDIR);
DUK_UTIL_STAT_TEST_MODE(fifo, S_ISFIFO);
DUK_UTIL_STAT_TEST_MODE(file, S_ISREG);
DUK_UTIL_STAT_TEST_MODE(socket, S_ISSOCK);
DUK_UTIL_STAT_TEST_MODE(symbolic_link, S_ISLNK);

static const duk_function_list_entry stat_methods[] = {
    {"isBlockDevice", duk_util_stat_is_block_device, 0},
    {"isCharacterDevice", duk_util_stat_is_character_device, 0},
    {"isDirectory", duk_util_stat_is_directory, 0},
    {"isFIFO", duk_util_stat_is_fifo, 0},
    {"isFile", duk_util_stat_is_file, 0},
    {"isSocket", duk_util_stat_is_socket, 0},
//    {"isSymbolicLink", duk_util_stat_is_symbolic_link, 0},
    {NULL, NULL, 0}};
/**
 *  Filesystem stat
 *  @typedef {Object} StatObject
 *  @property {int} dev - id of device containing file
 *  @property {int} ino - inode number
 *  @property {int} mode - the file mode
 *  @property {int} nlink - the number of hard links
 *  @property {int} uid - the user id of the owner
 *  @property {int} gid - the group id of the owner
 *  @property {int} rdev - device id if special file
 *  @property {int} size - total size in bytes
 *  @property {int} blksize - the blocksize for the system I/O
 *  @property {int} blocks - the number of blocks
 *  @property {Date} atime - time of last access
 *  @property {Date} mtime - time of last modification
 *  @property {Date} ctime - time of last status
 * 
 *  @param {string} The path name
 *  @returns {StatObject} a javascript object of the following form:
 *  stat: {
 *    dev: int,
 *    ino: int,
 *    mode: int,
 *    nlink: int,
 *    uid: int,
 *    gid: int,
 *    rdev: int,
 *    size: int,
 *    blksize: int,
 *    blocks: int,
 *    atime: Date,
 *    mtime: Date,
 *    ctime: Date,
 *  }
 **/
duk_ret_t duk_rp_stat_lstat(duk_context *ctx, int islstat)
{
    const char *path = duk_get_string(ctx, 0);
    struct stat path_stat;
    int err,
        safestat = duk_get_boolean_default(ctx,1,0);

    if (islstat)
        err=lstat(path, &path_stat);
    else
        err=stat(path, &path_stat);

    if (err)
    {
        if(safestat)
        {
            duk_push_false(ctx);
            return 1;
        }
        duk_push_error_object(ctx, DUK_ERR_ERROR, "stat(): error getting status '%s': %s", path, strerror(errno));
        return duk_throw(ctx);
    }

    // stat
    duk_push_object(ctx);

    DUK_PUT(ctx, int, "dev", path_stat.st_dev, -2);
    DUK_PUT(ctx, int, "ino", path_stat.st_ino, -2);
    DUK_PUT(ctx, int, "mode", path_stat.st_mode, -2);
    DUK_PUT(ctx, int, "nlink", path_stat.st_nlink, -2);
    DUK_PUT(ctx, int, "uid", path_stat.st_uid, -2);
    DUK_PUT(ctx, int, "gid", path_stat.st_gid, -2);
    DUK_PUT(ctx, int, "rdev", path_stat.st_rdev, -2);
    DUK_PUT(ctx, int, "size", path_stat.st_size, -2);
    DUK_PUT(ctx, int, "blksize", path_stat.st_blksize, -2);
    DUK_PUT(ctx, int, "blocks", path_stat.st_blocks, -2);

    long long atime, mtime, ctime;
    atime = path_stat.st_atime * 1000;
    mtime = path_stat.st_mtime * 1000;
    ctime = path_stat.st_ctime * 1000;

    // atime
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, atime);
    duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "atime");

    // mtime
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, mtime);
    duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "mtime");

    // ctime
    (void)duk_get_global_string(ctx, "Date");
    duk_push_number(ctx, ctime);
    duk_new(ctx, 1);
    duk_put_prop_string(ctx, -2, "ctime");

    // add methods
    duk_put_function_list(ctx, -1, stat_methods);
    if(islstat)
    {
        duk_push_c_function(ctx,duk_util_stat_is_symbolic_link, 0);
        duk_put_prop_string(ctx,-2,"isSymbolicLink");
    }


    // see duktape function interface
    return 1;
}

duk_ret_t duk_rp_stat(duk_context *ctx)
{
    return duk_rp_stat_lstat(ctx, 0);
}

duk_ret_t duk_rp_lstat(duk_context *ctx)
{
    return duk_rp_stat_lstat(ctx, 1);
}

duk_ret_t duk_rp_trim(duk_context *ctx)
{
    if (duk_is_string(ctx,0))
    {
        duk_trim(ctx,0);
        return 1;
    }
    duk_push_error_object(ctx, DUK_ERR_ERROR, "trim(): string is required");
    return duk_throw(ctx);
}


struct exec_thread_waitpid_arg
{
    pid_t pid;
    unsigned int timeout;
    int signal;
    unsigned int killed;
};
void *duk_rp_exec_thread_waitpid(void *arg)
{
    struct exec_thread_waitpid_arg *arg_s = ((struct exec_thread_waitpid_arg *)arg);
    usleep(arg_s->timeout*1000);
    kill(arg_s->pid, arg_s->signal);
    arg_s->killed = 1;
    return NULL;
}

#define DUK_UTIL_EXEC_READ_FD(ctx, buf, fildes, nread)                                                      \
    {                                                                                                       \
        int size = BUFREADSZ;                                                                               \
        DUKREMALLOC(ctx,buf, size);                                                                                \
        int nbytes = 0;                                                                                     \
        nread = 0;                                                                                          \
        while ((nbytes = read(fildes, buf + nread, size - nread)) > 0)                                      \
        {                                                                                                   \
            size *= 2;                                                                                      \
            nread += nbytes;                                                                                \
            DUKREMALLOC(ctx,buf, size);                                                                            \
        }                                                                                                   \
        if (nbytes < 0)                                                                                     \
        {                                                                                                   \
            duk_push_error_object(ctx, DUK_ERR_ERROR, "exec(): could not read output buffer: %s", strerror(errno)); \
            return duk_throw(ctx);                                                                          \
        }                                                                                                   \
    }

/**
 * Executes a command where the arguments are the arguments to execv.
 * @typedef {Object} ExecOptions 
 * @property {string} path - The path to the program to execute.
 * @property {string[]} args - The arguments to provide to the program (including the program name).
 * @property {int} timeout - The optional timeout in milliseconds.
 * @property {int=} killSignal - The signal to use to kill a timed out process. Default is SIGKILL (9)
 * @property {int=} background - Whether to put the process in the background. stdout, stderr will be null in this case.
 * 
 * @typedef {Object} ExecReturnObject 
 * @property {string?} stdout - The stdout of the program as a string. Will be null if background is set in ExecOptions.
 * @property {string?} stderr - The stderr of the program as a string. Will be null if background is set in ExecOptions.
 * @property {int?} exitStatus - The exit status of the program. Will be null if background is set in ExecOptions.
 * @property {boolean} timedOut - whether the program timed out using after the specified timeout in ExecOptions.
 * @property {int} pid - the pid of the program.
 * 
 * @param {ExecOptions} options
 * @returns {ExecReturnObject}
 * Ex.
 * const { 
 *    stdout: string, 
 *    stderr: string, 
 *    exit_status: int,
 *    timed_out: bool,
 *    pid: int
 * } = utils.exec({ 
 *    path: "/bin/ls", 
 *    args: ["ls", "-1"], 
 *    timeout: 1000
 *    kill_signal: 9, background: false });
 */
duk_ret_t duk_rp_exec_raw(duk_context *ctx)
{

    // get options
    duk_get_prop_string(ctx, -1, "timeout");
    unsigned int timeout = duk_get_uint_default(ctx, -1, 0);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, "killSignal");
    int kill_signal = duk_get_int_default(ctx, -1, SIGKILL);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, "path");
    const char *path = duk_require_string(ctx, -1);
    duk_pop(ctx);

    duk_get_prop_string(ctx, -1, "background");
    int background = duk_get_boolean_default(ctx, -1, 0);
    duk_pop(ctx);

    // get arguments into null terminated buffer
    duk_get_prop_string(ctx, -1, "args");
    duk_size_t nargs = duk_get_length(ctx, -1);
    char **args = NULL;
    int i;
    DUKREMALLOC(ctx,args, (nargs + 1) * sizeof(char *));
    for (i = 0; i < nargs; i++)
    {
        duk_get_prop_index(ctx, -1, i);
        args[i] = (char *)duk_require_string(ctx, -1);
        duk_pop(ctx);
    }
    args[nargs] = NULL;
    duk_pop(ctx);

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (!background)
        if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1)
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "exec(): could not create pipe: %s", strerror(errno));
            return duk_throw(ctx);
        }
    pid_t pid;
    if ((pid = fork()) == -1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "exec(): could not fork: %s", strerror(errno));
        return duk_throw(ctx);
    }
    else if (pid == 0)
    {
        if (!background)
        {
            // make pipe equivalent to stdout and stderr
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);

            // close unused pipes
            close(stdout_pipe[0]);
            close(stderr_pipe[0]);
        }
        execv(path, args);
        fprintf(stderr, "exec(): could not execute %s\n", args[0]);
        exit(EXIT_FAILURE);
    }
    // create thread for timeout
    struct exec_thread_waitpid_arg arg;
    pthread_t thread;
    arg.signal = kill_signal;
    arg.pid = pid;
    arg.timeout = timeout;
    arg.killed = 0;
    if (timeout > 0)
    {
        pthread_create(&thread, NULL, duk_rp_exec_thread_waitpid, &arg);
    }

    if (background)
    {
        // return object
        duk_push_object(ctx);

        DUK_PUT(ctx, int, "pid", pid, -2);

        // set stderr and stdout to null
        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "stderr");
        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "stdout");
        duk_push_null(ctx);
        duk_put_prop_string(ctx, -2, "exitStatus");

        // set timed_out to false
        DUK_PUT(ctx, boolean, "timedOut", 0, -2);
    }
    else
    {
        int exit_status;
        waitpid(pid, &exit_status, 0);
        // cancel timeout thread in case it is still running
        if (timeout > 0)
        {
            pthread_cancel(thread);
            pthread_join(thread, NULL);
        }
        // close unused pipes
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        char *stdout_buf = NULL;
        char *stderr_buf = NULL;

        // read output
        int stdout_nread, stderr_nread;
        DUK_UTIL_EXEC_READ_FD(ctx, stdout_buf, stdout_pipe[0], stdout_nread);
        DUK_UTIL_EXEC_READ_FD(ctx, stderr_buf, stderr_pipe[0], stderr_nread);

        // push return object
        duk_push_object(ctx);

        duk_push_lstring(ctx, stdout_buf, stdout_nread);
        duk_put_prop_string(ctx, -2, "stdout");

        duk_push_lstring(ctx, stderr_buf, stderr_nread);
        duk_put_prop_string(ctx, -2, "stderr");

        DUK_PUT(ctx, boolean, "timedOut", arg.killed, -2);
        DUK_PUT(ctx, int, "exitStatus", exit_status, -2);
        DUK_PUT(ctx, int, "pid", pid, -2);
        free(stdout_buf);
        free(stderr_buf);
    }
    free(args);
    return 1;
}


duk_ret_t duk_rp_exec(duk_context *ctx)
{
    duk_idx_t i=0, obj_idx=-1, top=duk_get_top(ctx);
    char *comm=NULL, *s=NULL;
    duk_uarridx_t arrayi=0;
    duk_push_object(ctx); //object for exec_raw
    duk_push_array(ctx);  //array for args
    for (;i<top;i++)
    {
        if(duk_is_object(ctx,i) && !duk_is_function(ctx,i) && !duk_is_array(ctx,i))
        {
            obj_idx=i;
            continue;
        }

        if (!duk_is_string(ctx,i))
        {
            if ( !duk_is_function(ctx, i) )
                (void)duk_json_encode(ctx, i); 
            else
            {
                duk_push_string(ctx,"{_func:true}");
                duk_replace(ctx,i);
            }
        }    


        if(i==0)
            comm=strdup(duk_get_string(ctx,0));

        duk_dup(ctx,i);
        duk_put_prop_index(ctx,top+1,arrayi);

        arrayi++;
    }

    if(obj_idx!=-1)
    {
        duk_pull(ctx, obj_idx);
        //duk_del_prop_string(ctx,-1,"args");
        duk_replace(ctx, -3); 
    }
    duk_put_prop_string(ctx, -2, "args");

    s=strchr(comm,'/');
    if(!s)
    {
        char *path=strdup(getenv("PATH")), *p=path;
        char *end, *pfile=NULL;
        struct stat st;

        /* search for file in PATHs */
        end=strchr(p,':');
        while ( p != NULL )
        {
            if( end != NULL)
            {
                *end='\0';
                pfile=strdup(p);
                p=end+1;
                end=strchr(p,':');
            }
            else
            {
                pfile=strdup(p);
                p=NULL;
            }

            if(pfile[strlen(pfile)-1]!='/')
                pfile=strjoin(pfile,comm,'/');
            else
                pfile=strcatdup(pfile,comm);

            if ( stat(pfile,&st) != -1) 
                break;

            free(pfile);
            pfile=NULL;
        }

        free(path);
        if(pfile)
        {
            duk_push_string(ctx,pfile);
            free(pfile);
        }
        else
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "exec(): could not find '%s' in PATH", comm);
            free(comm);
            return duk_throw(ctx);
        }
    }
    else
        duk_push_string(ctx,comm);

    free(comm);
    duk_put_prop_string(ctx,-2,"path");
    duk_replace(ctx,0);

    top=duk_get_top(ctx);
    for (i=1;i<top;i++)
        duk_pop(ctx);

    return duk_rp_exec_raw(ctx);
}

duk_ret_t duk_rp_shell(duk_context *ctx)
{
    duk_idx_t sidx=0;
    const char *sh="/bin/bash";

    if(!duk_is_string(ctx,sidx) && !duk_is_string(ctx,++sidx) )
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "shell(): error, command must be a string");
        return duk_throw(ctx);
    }
    if (duk_is_undefined(ctx,1))
        duk_pop(ctx);

    if (getenv("SHELL"))
        sh=getenv("SHELL");
    duk_push_string(ctx,sh);
    duk_insert(ctx,0);
    duk_push_string(ctx,"-c");
    duk_insert(ctx,1);
    return duk_rp_exec(ctx);
}

/**
 * Kills a process with the process id given by the argument
 * @param {int} process id
 * @param {int} signal
 */
duk_ret_t duk_rp_kill(duk_context *ctx)
{
    pid_t pid = duk_require_int(ctx, 0);
    int signal = SIGTERM;
    
    if(duk_is_number(ctx,1))
        signal=duk_get_int(ctx, 1);

    if (kill(pid, signal))
        duk_push_int(ctx,0);
    else
        duk_push_int(ctx,1);

    return 1;
}

/**
 * Creates a directory with the name given as a path
 * @param {path} - the directory to be created
 * @param {mode=} - the mode of the newly created directory (default: 0777)
 * Ex.
 * utils.mkdir("new/directory")
 */
duk_ret_t duk_rp_mkdir(duk_context *ctx)
{
    const char *path;
    mode_t mode=0755;

    path = duk_require_string(ctx, 0);
    if(duk_is_string(ctx,1))
    {
        char *e;
        const char *s=duk_get_string(ctx,1);

        mode=(mode_t)strtol(s,&e,8);
        if(s==e)
            mode=0755;
    }
    else if (duk_is_number(ctx,1))
        mode=(mode_t)duk_get_int(ctx,1);

    if(rp_mkdir_parent(path,mode)==-1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "mkdir(): error creating directory: %s", strerror(errno));
        return duk_throw(ctx);
    }
    return 0;
}

/**
 * Removes an empty directory with the name given as a path. Allows recursively removing nested directories 
 * @param {string} path - The path to the directory to be deleted
 * @param {boolean=} recursive - whether to recursively delete. Set to false by default.
 * Ex.
 * utils.rmdir("directory/to/be/deleted")
 */
duk_ret_t duk_rp_rmdir(duk_context *ctx)
{
    const char *path;
    int recursive;
    
    path = duk_require_string(ctx, 0);
    recursive = duk_get_boolean_default(ctx, 1, 0);

    {
        int length=strlen(path);
        char _path[length+1];

        strcpy(_path, path);

        if (rmdir(path) != 0)
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "rmdir(): error removing directory: %s", strerror(errno));
            return duk_throw(ctx);
        }

        if (recursive)
        {
            char *p;
            for (p = _path + length - 1; p != _path; p--)
            { // Traverse the path backwards to delete nested directories

                if (*p == '/')
                {

                    *p = '\0';

                    if (rmdir(_path) != 0)
                    {
                        duk_push_error_object(ctx, DUK_ERR_ERROR, "rmdir(): error removing directories recursively: %s", strerror(errno));
                        return duk_throw(ctx);
                    }

                    *p = '/';
                }
            }
        }
    }
    return 0;
}
/**
 * Reads the directory given by path.
 * @param {path} the directory
 * @returns an array of file names
 */
duk_ret_t duk_rp_readdir(duk_context *ctx)
{
    const char *path = duk_require_string(ctx, 0);
    DIR *dir = opendir(path);
    struct dirent *entry=NULL;
    int i=0, 
        showhidden=duk_get_boolean_default(ctx,1,0);
    
    
    if (dir == NULL)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readdir(): could not open directory %s: %s", path, strerror(errno));
        return duk_throw(ctx);
    }

    errno = 0;
    duk_push_array(ctx);

    while ((entry = readdir(dir)) != NULL)
    {
        if( showhidden || *(entry->d_name) != '.')
        {
            duk_push_string(ctx, entry->d_name);
            duk_put_prop_index(ctx, -2, i++);
        }
    }
    if (errno)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "readdir(): error reading directory %s: %s", path, strerror(errno));
        return duk_throw(ctx);
    }
    closedir(dir);
    return 1;
}

#define DUK_UTIL_REMOVE_FILE(ctx, file)                                                                \
    if (remove(file))                                                                                  \
    {                                                                                                  \
        duk_push_error_object(ctx, DUK_ERR_ERROR, "could not remove '%s': %s", file, strerror(errno)); \
        return duk_throw(ctx);                                                                         \
    }

/**                                                                                                
 * Copies the file from src to dest. Passing overwrite will overwrite any file already present.    
 * It will try to preserve the file mode.
 * @typedef {Object} CopyFileOptions
 * @property {string} src - the path to the file source.
 * @property {string} dest - the path to where the file will be moved.
 * @property {string=} overwrite - whether to overwrite any existing file at dest. Set to false by default.                                                        
 * @param {{ src: string, dest: string, overwrite: boolean }} options - the options to be given                  
 */
duk_ret_t duk_rp_copyFile(duk_context *ctx, char *fname)
{
    duk_idx_t i=0, obj_idx=-1, top=duk_get_top(ctx);
    const char *src_filename=NULL, *dest_filename=NULL;
    int overwrite=0;

    for (;i<top;i++)
    {
        if( duk_is_string(ctx,i) )
        {
            if(src_filename == NULL)
                src_filename=duk_get_string(ctx,i);
            else if(dest_filename == NULL)
                dest_filename=duk_get_string(ctx,i);
        }

        else if (duk_is_boolean(ctx,i))
            overwrite=duk_get_boolean(ctx,i);   

        else if (duk_is_object(ctx,i) && !duk_is_array(ctx,i) && !duk_is_function(ctx,i) )
            obj_idx=i;
    }

    if (obj_idx != -1)
    {
        if( duk_get_prop_string(ctx, obj_idx, "src") )
            src_filename = duk_require_string(ctx, -1);
        duk_pop(ctx);

        if( duk_get_prop_string(ctx, obj_idx, "dest") )
            dest_filename = duk_require_string(ctx, -1);
        duk_pop(ctx);

        if( duk_get_prop_string(ctx, obj_idx, "overwrite") )
            overwrite = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);
    }

    if (!src_filename)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: source file not specified",fname);
        return duk_throw(ctx);
    }

    if (!dest_filename)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: destination file not specified",fname);
        return duk_throw(ctx);
    }

    /* test if they are the same file 
    if(! testlink(ctx, src_filename, dest_filename))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error getting status '%s': %s", fname, src_filename, strerror(errno));
        return duk_throw(ctx);
    }
    (void) testlink(ctx, dest_filename, src_filename);
    */
    
    {
        FILE *dest, *src = fopen(src_filename, "r");
        struct stat src_stat, dest_stat;
        int err;        

        if (src == NULL)
        {
            fclose(src);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: could not open file '%s': %s", fname, src_filename, strerror(errno));
            return duk_throw(ctx);
        }

        if (stat(src_filename, &src_stat))
        {
            fclose(src);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error getting status '%s': %s", fname, src_filename, strerror(errno));
            return duk_throw(ctx);
        } 
        else
        

        err = stat(dest_filename, &dest_stat);
        if(!err)
        {
            if(dest_stat.st_ino == src_stat.st_ino)
            {
                duk_push_error_object(ctx, DUK_ERR_ERROR, "copyFile(): same file: '%s' is a link to '%s'", src_filename, dest_filename);
                return duk_throw(ctx);
            }
        }

        if (!err && !overwrite)
        {
            // file exists and shouldn't be overwritten
            fclose(src);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error copying '%s': %s", fname, dest_filename, "file already exists");
            return duk_throw(ctx);
        }
        
        if (err && errno != ENOENT)
        {
            fclose(src);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error getting status '%s': %s", fname, dest_filename, strerror(errno));
            return duk_throw(ctx);
        }

        dest = fopen(dest_filename, "w");
        if (dest == NULL)
        {
            fclose(src);
            duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: could not open file '%s': %s", fname, dest_filename, strerror(errno));
            return duk_throw(ctx);
        }
        {
            char buf[BUFREADSZ];
            int nread;
            while ((nread = read(fileno(src), buf, BUFREADSZ)) > 0)
            {
                if (write(fileno(dest), buf, nread) != nread)
                {
                    fclose(src);
                    fclose(dest);
                    DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
                    duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: could not write to file '%s': %s", fname, dest_filename, strerror(errno));
                    return duk_throw(ctx);
                }
            }
            if (nread < 0)
            {
                fclose(src);
                fclose(dest);
                DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
                duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error reading file '%s': %s", fname, src_filename, strerror(errno));
                return duk_throw(ctx);
            }
            if (chmod(dest_filename, src_stat.st_mode))
            {
                //DUK_UTIL_REMOVE_FILE(ctx, dest_filename);
                fclose(src);
                fclose(dest);
                duk_push_error_object(ctx, DUK_ERR_ERROR, "%s: error setting file mode %o for '%s': %s", fname, src_stat.st_mode, dest_filename, strerror(errno));
                return duk_throw(ctx);
            }
        }
        fclose(src);
        fclose(dest);
    }
    return 0;
}

duk_ret_t duk_rp_copy_file(duk_context *ctx)
{
    return duk_rp_copyFile(ctx, "copyFile()");
}

/**
 * Deletes a file at the given path
 * @param {string} file - the file to be deleted
 */
duk_ret_t duk_rp_delete(duk_context *ctx)
{
    const char *file = duk_require_string(ctx, -1);

    if (remove(file) != 0)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "rmFile(): error deleting file: %s", strerror(errno));
        return duk_throw(ctx);
    }

    return 0;
}

/**
 * Creates a hard or symbolic link 
 * @typedef {Object} LinkOptions
 * @property {string} path - the path to the source file to link
 * @property {string} target - the path target file that will be created
 * @property {boolean=} hard - whether the link is hard. Set to false by default. 
 * @param {{src: string, target: string, hard: boolean }} options
 * Ex.
 * symlink({ src: "some_file", target: "some_link"});
 * symlink("some_file", "some_link");
 * link({ src: "some_file", target: "some_link"});
 * link("some_file", "some_link");
 */
duk_ret_t duk_rp_symHardLink(duk_context *ctx, int hard)
{
    const char *src=NULL, *target=NULL;
    duk_idx_t i=0, obj_idx=-1, top=duk_get_top(ctx);

    for (;i<top;i++)
    {
        if( duk_is_string(ctx,i) )
        {
            if(src == NULL)
                src=duk_get_string(ctx,i);
            else if(target == NULL)
                target=duk_get_string(ctx,i);
        }

        else if (duk_is_object(ctx,i) && !duk_is_array(ctx,i) && !duk_is_function(ctx,i) )
            obj_idx=i;
    }

    if (obj_idx != -1)
    {
        if( duk_get_prop_string(ctx, obj_idx, "src") )
            src = duk_require_string(ctx, -1);
        duk_pop(ctx);

        if( duk_get_prop_string(ctx, obj_idx, "target") )
            target = duk_require_string(ctx, -1);
        duk_pop(ctx);

    }

    if (!src)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "link(): source file not specified");
        return duk_throw(ctx);    
    }

    if (!target)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "link(): target name not specified");
        return duk_throw(ctx);    
    }

    if (!hard)
    {
        if (symlink(src, target))
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "link(): error creating symbolic link from '%s' to '%s': %s", src, target, strerror(errno));
            return duk_throw(ctx);
        }
    }
    else
    {
        if (link(src, target))
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "link(): error creating hard link from '%s' to '%s': %s", src, target, strerror(errno));
            return duk_throw(ctx);
        }
    }
    return 0;
}

duk_ret_t duk_rp_symlink(duk_context *ctx)
{
    return duk_rp_symHardLink(ctx, 0);
}

duk_ret_t duk_rp_link(duk_context *ctx)
{
    return duk_rp_symHardLink(ctx, 1);
}


/**
 * Changes the file permissions of a specified file
 * @param {path} - The path to the file
 * @param {mode} - The new permissions for the file
 */
duk_ret_t duk_rp_chmod(duk_context *ctx)
{
    const char *path = duk_require_string(ctx, 0);
    mode_t mode, old_umask=umask(0);

    if(duk_is_string(ctx,1))
    {
        char *e;
        const char *s=duk_get_string(ctx,1);

        mode=(mode_t)strtol(s,&e,8);
        if(s==e)
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "chmod(): invalid mode: %s", s);
            return duk_throw(ctx);
        }
    }
    else if (duk_is_number(ctx,1))
        mode=(mode_t)duk_get_int(ctx,1);
    else
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "chmod(): invalid or no mode specified");
        return duk_throw(ctx);
    }

    if (chmod(path, mode) == -1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "chmod(): error changing permissions: %s", strerror(errno));
        (void)umask(old_umask);
        return duk_throw(ctx);
    }

    (void)umask(old_umask);
    return 0;
}

/**
 * Updates last access time to now. Creates the file if it doesn't exist.
 * @typedef {Object} TouchOptions
 * @property {string} path - The path to the file to update/create
 * @property {boolean=} nocreate - Don't create the file if exist (defaults to false)
 * @property {string?} reference - A file to copy last access time from instead of current time
 * @property {boolean=} setaccess - Set the access time (defaults to setting both access and modified if neither specified)
 * @property {boolean=} setmodify - Set the modified time (defaults to setting both access and modified if neither specified)
 * @param {TouchOptions} options
 */
duk_ret_t duk_rp_touch(duk_context *ctx)
{
    int nocreate=0, setaccess=1, setmodify=1;
    const char *path=NULL, *reference=NULL;

    if( duk_is_object(ctx,0))
    {
        duk_get_prop_string(ctx, 0, "path");
        path = duk_require_string(ctx, -1);
        duk_pop(ctx);

        duk_get_prop_string(ctx, 0, "nocreate");
        nocreate = duk_get_boolean_default(ctx, -1, 0);
        duk_pop(ctx);

        duk_get_prop_string(ctx, 0, "reference");
        reference = duk_get_string_default(ctx, -1, NULL);
        duk_pop(ctx);

        duk_get_prop_string(ctx, 0, "setaccess");
        setaccess = duk_get_boolean_default(ctx, -1, 1);
        duk_pop(ctx);

        duk_get_prop_string(ctx, 0, "setmodify");
        setmodify = duk_get_boolean_default(ctx, -1, 1);
        duk_pop(ctx);
    }
    else if (duk_is_string(ctx, 0) )
        path=duk_get_string(ctx, 0);

    {
        struct stat filestat;
        time_t new_mtime, new_atime;
        struct stat refrence_stat;
        struct utimbuf new_times;

        if (stat(path, &filestat) != 0) // file doesn't exist
        {
            if (nocreate)
            {
                return 0;
            }
            else
            {
                FILE *fp = fopen(path, "w"); // create file
                fclose(fp);
            }
        }


        if (reference)
        {

            if (stat(reference, &refrence_stat) != 0) //reference file doesn't exist
            {
                duk_push_error_object(ctx, DUK_ERR_ERROR, "reference file does not exist");
                return duk_throw(ctx);
            }

            new_mtime = setmodify ? refrence_stat.st_mtime : filestat.st_mtime; // if setmodify, update m_time
            new_atime = setaccess ? refrence_stat.st_atime : filestat.st_atime; // if setacccess, update a_time
        }
        else
        {
            new_mtime = setmodify ? time(NULL) : filestat.st_mtime; //set to current time if set modify
            new_atime = setaccess ? time(NULL) : filestat.st_atime;
        }

        new_times.actime = new_atime;
        new_times.modtime = new_mtime;

        utime(path, &new_times);
    }

    return 0;
}

/**
 * Renames or moves a source file to a target path.
 * @param {string} old - the source file or directory
 * @param {string} new - the target path
 * Ex.
 * utils.rename("sample.txt", "sample-2.txt");
 */
duk_ret_t duk_rp_rename(duk_context *ctx)
{
    const char *old = duk_require_string(ctx, 0);
    const char *new = duk_require_string(ctx, 1);

    if (rename(old, new))
    {
        if(errno==EXDEV)
        {
            (void)duk_rp_copyFile(ctx,"rename()");
            if (remove(old) != 0)
            {
                duk_push_error_object(ctx, DUK_ERR_ERROR, "rename(): error deleting old file: %s", strerror(errno));
                return duk_throw(ctx);
            }
            return 0;            
        }
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error renaming '%s' to '%s': %s (%d)", old, new, strerror(errno),errno);
        return duk_throw(ctx);
    }

    return 0;
}

/** 
 * Changes ownership of a file to a given user or group.
 * @typedef {Object} ChownOptions
 * @property {string} path - the path to the file to change
 * @property {string} group - the name of the group to change ownership to
 * @property {string} use - the name of the user to change ownership to
 * @param {ChownOptions} options
 */
duk_ret_t duk_rp_chown(duk_context *ctx)
{
    duk_idx_t i=0, obj_idx=-1, top=duk_get_top(ctx);
    const char *path = NULL, *group_name = NULL, *user_name = NULL;
    int gid = -1, uid = -1;
    gid_t group_id = 0;
    uid_t user_id = 0;

    for (;i<top;i++)
    {
        if( duk_is_string(ctx,i) || duk_is_number(ctx,i) )
        {
            if(path == NULL)
            {
                path=duk_require_string(ctx,i);
            }
            else if(user_name == NULL && uid==-1)
            {
                if(duk_is_string(ctx,i))
                    user_name = duk_get_string(ctx,i);
                else
                {
                    uid=duk_get_int(ctx,i);
                    user_id=(uid_t)uid;
                }
            }
            else if(group_name == NULL && gid==-1)
            {
                if(duk_is_string(ctx,i))
                    group_name = duk_get_string(ctx,i);
                else
                {
                    gid=duk_get_int(ctx,i);
                    group_id=(gid_t)gid;
                }
            }
        }

        else if (duk_is_object(ctx,i) && !duk_is_array(ctx,i) && !duk_is_function(ctx,i) )
            obj_idx=i;
    }

    if(obj_idx !=-1 )
    {
        duk_get_prop_string(ctx, obj_idx, "path");
        path = duk_require_string(ctx, -1);
        duk_pop(ctx);

        duk_get_prop_string(ctx, obj_idx, "group");
        if(duk_is_string(ctx,-1))
        {
            gid=1;
            group_name = duk_get_string(ctx, -1);
        }
        else
        {
            gid=duk_get_int(ctx, -1);
            group_id=(gid_t)gid;
        }
        duk_pop(ctx);

        duk_get_prop_string(ctx, obj_idx, "user");
        if(duk_is_string(ctx,-1))
        {
            uid=1;
            user_name = duk_get_string(ctx, -1);
        }
        else
        {
            uid=duk_get_int(ctx,i);
            user_id=(uid_t)uid;
        }
        duk_pop(ctx);
    }

    {
        struct stat file_stat;

        if (user_name)
        {
            struct passwd *user = getpwnam(user_name);

            if (user == NULL)
            {
                duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing ownership (user not found): %s", strerror(errno));
                return duk_throw(ctx);
            }
            user_id = user->pw_uid;
            uid=1;
        }

        if (group_name)
        {
            struct group *grp = getgrnam(group_name);

            if (grp == NULL)
            {
                duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing ownership (group not found): %s", strerror(errno));
                return duk_throw(ctx);
            }
            group_id = grp->gr_gid;
            gid=1;
        }


        stat(path, &file_stat);
        if (uid == -1) // no specified user
        {
            user_id = file_stat.st_uid;
        }

        if (gid == -1) // no specified group
        {
            group_id = file_stat.st_gid;
        }

        if (chown(path, user_id, group_id) != 0)
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "error changing  ownership: %s", strerror(errno));
            return duk_throw(ctx);
        }
    }
    return 0;
}

void duk_rampart_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }

    if (!duk_get_prop_string(ctx, -1, "utils"))
    {
        duk_pop(ctx);
        duk_push_object(ctx); //new utils object
    }

    /* populate utils object with functions */
    duk_push_c_function(ctx, duk_rp_hexify, 2);
    duk_put_prop_string(ctx, -2, "hexify");
    duk_push_c_function(ctx, duk_rp_dehexify, 2);
    duk_put_prop_string(ctx, -2, "dehexify");
    duk_push_c_function(ctx, duk_rp_strToBuf, 2);
    duk_put_prop_string(ctx, -2, "stringToBuffer");
    duk_push_c_function(ctx, duk_rp_bufToStr, 1);
    duk_put_prop_string(ctx, -2, "bufferToString");
    duk_push_c_function(ctx, duk_rp_object2q, 2);
    duk_put_prop_string(ctx, -2, "objectToQuery");
    duk_push_c_function(ctx, duk_rp_query2o, 1);
    duk_put_prop_string(ctx, -2, "queryToObject");
    duk_push_c_function(ctx, duk_rp_read_file, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "readFile");
    duk_push_c_function(ctx, duk_rp_readline, 1);
    duk_put_prop_string(ctx, -2, "readLine");
    duk_push_c_function(ctx, duk_rp_readln, 1);
    duk_put_prop_string(ctx, -2, "readln");
    duk_push_c_function(ctx, duk_rp_stat, 2);
    duk_put_prop_string(ctx, -2, "stat");
    duk_push_c_function(ctx, duk_rp_lstat, 2);
    duk_put_prop_string(ctx, -2, "lstat");
    duk_push_c_function(ctx, duk_rp_trim, 1);
    duk_put_prop_string(ctx, -2, "trim");
    duk_push_c_function(ctx, duk_rp_exec_raw, 1);
    duk_put_prop_string(ctx, -2, "execRaw");
    duk_push_c_function(ctx, duk_rp_exec, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "exec");
    duk_push_c_function(ctx, duk_rp_shell, 2);
    duk_put_prop_string(ctx, -2, "shell");
    duk_push_c_function(ctx, duk_rp_kill, 2);
    duk_put_prop_string(ctx, -2, "kill");
    duk_push_c_function(ctx, duk_rp_mkdir, 2);
    duk_put_prop_string(ctx, -2, "mkdir");
    duk_push_c_function(ctx, duk_rp_rmdir, 2);
    duk_put_prop_string(ctx, -2, "rmdir");
    duk_push_c_function(ctx, duk_rp_readdir, 2);
    duk_put_prop_string(ctx, -2, "readdir");
    duk_push_c_function(ctx, duk_rp_copy_file, 4);
    duk_put_prop_string(ctx, -2, "copyFile");
    duk_push_c_function(ctx, duk_rp_delete, 1);
    duk_put_prop_string(ctx, -2, "rmFile");
    duk_push_c_function(ctx, duk_rp_link, 3);
    duk_put_prop_string(ctx, -2, "link");
    duk_push_c_function(ctx, duk_rp_symlink, 3);
    duk_put_prop_string(ctx, -2, "symlink");
    duk_push_c_function(ctx, duk_rp_chmod, 2);
    duk_put_prop_string(ctx, -2, "chmod");
    duk_push_c_function(ctx, duk_rp_touch, 1);
    duk_put_prop_string(ctx, -2, "touch");
    duk_push_c_function(ctx, duk_rp_rename, 2);
    duk_put_prop_string(ctx, -2, "rename");
    duk_push_c_function(ctx, duk_rp_chown, 4);
    duk_put_prop_string(ctx, -2, "chown");

    /* all above are rampart.utils.xyz() functions*/
    duk_put_prop_string(ctx, -2, "utils");

    /* globalize is rampart.globalize() */
    duk_push_c_function(ctx, duk_rp_globalize,2);
    duk_put_prop_string(ctx, -2, "globalize");
    
    duk_put_global_string(ctx, "rampart");
}




void duk_misc_init(duk_context *ctx)
{
    duk_rampart_init(ctx);
    duk_process_init(ctx);
}

#include "printf.c"

duk_ret_t duk_printf(duk_context *ctx)
{
    char buffer[1];
    int ret;
    if (pthread_mutex_lock(&pflock) == EINVAL)
    {
        fprintf(stderr, "could not obtain print lock\n");
        exit(1);
    }
    ret = _printf(_out_char, buffer, (size_t)-1, ctx,0);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;
}
#define getfh(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
    {\
        duk_push_sprintf(ctx,"%s: argument is not a file handle",func);\
        (void)duk_throw(ctx);\
    }\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    f;\
})
#define getfh_nonull(ctx,idx,func) ({\
    FILE *f;\
    if( !duk_get_prop_string(ctx,idx,DUK_HIDDEN_SYMBOL("filehandle")) )\
    {\
        duk_push_sprintf(ctx,"error %s: argument is not a file handle",func);\
        (void)duk_throw(ctx);\
    }\
    f=duk_get_pointer(ctx,-1);\
    duk_pop(ctx);\
    if(f==NULL){\
        duk_push_sprintf(ctx,"error %s: file handle was previously closed",func);\
        (void)duk_throw(ctx);\
    }\
    f;\
})
duk_ret_t duk_fseek(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fseek()");
    long offset=(long)duk_require_number(ctx,1);
    int whence=SEEK_SET;
    const char *wstr=duk_require_string(ctx,2);

    if(!strcmp(wstr,"SEEK_SET"))
        whence=SEEK_SET;
    else if(!strcmp(wstr,"SEEK_END"))
        whence=SEEK_END;
    else if(!strcmp(wstr,"SEEK_CUR"))
        whence=SEEK_CUR;
    else
    {
        duk_push_sprintf(ctx,"error fseek(): invalid argument '%s'",wstr);
        (void)duk_throw(ctx);
    }
    if(fseek(f, offset, whence))
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error fseek():'%s'", strerror(errno));
        (void)duk_throw(ctx);
    }
    return 0;
}
duk_ret_t duk_rewind(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"rewind()");
    rewind(f);
    return 0;
}
duk_ret_t duk_ftell(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"ftell()");
    long pos;

    pos=ftell(f);
    if(pos==-1)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error ftell():'%s'", strerror(errno));
        (void)duk_throw(ctx);
    }
    duk_push_number(ctx,(double)pos);
    return 1;
}
duk_ret_t duk_fread(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fread()");
    void *buf;
    size_t read, sz=(size_t)duk_require_number(ctx,1);
    buf=duk_push_dynamic_buffer(ctx, (duk_size_t)sz);

    read=fread(buf,1,sz,f);
    if(read != sz)
    {
        if(ferror(f))
        {
            duk_push_error_object(ctx, DUK_ERR_ERROR, "error fread(): error reading file");
            (void)duk_throw(ctx);
        }
        duk_resize_buffer(ctx, -1, (duk_size_t)read);
    }
    return(1);
}
duk_ret_t duk_fwrite(duk_context *ctx)
{
    FILE *f = getfh_nonull(ctx,0,"fwrite()");
    void *buf;
    size_t wrote, sz=(size_t)duk_get_number_default(ctx,2,-1);
    duk_size_t bsz;
    duk_to_buffer(ctx,1,&bsz);
    buf=duk_get_buffer_data(ctx, 1, &bsz);
    if(sz !=-1)
    {
        if((size_t)bsz < sz)
            sz=(size_t)bsz;
    }
    else sz=(size_t)bsz;

    wrote=fwrite(buf,1,sz,f);
    if(wrote != sz)
    {
        duk_push_error_object(ctx, DUK_ERR_ERROR, "error fwrite(): error writing file");
        (void)duk_throw(ctx);
    }
    duk_push_number(ctx,(double)wrote);
    return(1);
}
duk_ret_t duk_fopen(duk_context *ctx)
{
    FILE *f;
    const char *fn=duk_require_string(ctx,0);
    const char *mode=duk_require_string(ctx,1);

    f=fopen(fn,mode);
    if(f==NULL) goto err;

    duk_push_object(ctx);
    duk_push_pointer(ctx,(void *)f);
    duk_put_prop_string(ctx,-2,DUK_HIDDEN_SYMBOL("filehandle") );
    return 1;

    err:
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening file '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
}
duk_ret_t duk_fclose(duk_context *ctx)
{
    FILE *f = getfh(ctx,0,"fclose()");

    fclose(f);
    duk_push_pointer(ctx,NULL);
    duk_put_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle") );

    return 1;
}
duk_ret_t duk_fprintf(duk_context *ctx)
{
    int ret;
    const char *fn;
    FILE *out=NULL;
    int append=0;
    int closefh=1;

    if(duk_is_object(ctx,0))
    {
        if(duk_get_prop_string(ctx,0,"stream"))
        {
            const char *s=duk_require_string(ctx,-1);
            if (!strcmp(s,"stdout"))
            {
                out=stdout;
            }
            else if (!strcmp(s,"stderr"))
            {
                out=stderr;
            }
            else
            {
                duk_push_string(ctx,"error: fprintf({stream:""},...): stream must be stdout or stderr");
                (void)duk_throw(ctx);
            }
            closefh=0;
            duk_pop(ctx);
            goto startprint;
        }
        duk_pop(ctx);

        if ( duk_get_prop_string(ctx,0,DUK_HIDDEN_SYMBOL("filehandle")) )
        {
            out=duk_get_pointer(ctx,-1);
            duk_pop(ctx);
            closefh=0;
            if(out==NULL)
            {
                duk_push_string(ctx,"error: fprintf(handle,...): handle was previously closed");
                (void)duk_throw(ctx);
            }
            goto startprint;
        }
        duk_pop(ctx);

        duk_push_string(ctx,"error: fprintf({},...): invalid option");
        (void)duk_throw(ctx);
    }
    else
    {
        fn=duk_require_string(ctx,0);
        if( duk_is_boolean(ctx,1) )
        {
            append=duk_get_boolean(ctx,1);
            duk_remove(ctx,1);
        }

        if(append)
        {
            if( (out=fopen(fn,"a")) == NULL )
            {
                if( (out=fopen(fn,"w")) == NULL )
                    goto err;
            }
        }
        else
        {
            if( (out=fopen(fn,"w")) == NULL )
                goto err;
        }
    }
    startprint:
    ret = _printf(_fout_char, (void*)out, (size_t)-1, ctx,1);
    if (pthread_mutex_lock(&pflock) == EINVAL)
    {
        fprintf(stderr, "error: could not obtain lock in fprintf\n");
        exit(1);
    }
    if(closefh)
        fclose(out);
    pthread_mutex_unlock(&pflock);
    duk_push_int(ctx, ret);
    return 1;

    err:
    duk_push_error_object(ctx, DUK_ERR_ERROR, "error opening file '%s': %s", fn, strerror(errno));
    return duk_throw(ctx);
}
duk_ret_t duk_sprintf(duk_context *ctx)
{
    char *buffer;
    int size = _printf(_out_null, NULL, (size_t)-1, ctx,0);
    buffer = malloc((size_t)size + 1);
    if (!buffer)
    {
        duk_push_string(ctx, "malloc error in sprintf");
        (void)duk_throw(ctx);
    }
    (void)_printf(_out_buffer, buffer, (size_t)-1, ctx,0);
    duk_push_lstring(ctx, buffer,(duk_size_t)size);
    free(buffer);
    return 1;
}
duk_ret_t duk_bprintf(duk_context *ctx)
{
    char *buffer;
    int size = _printf(_out_null, NULL, (size_t)-1, ctx,0);
    buffer = (char *) duk_push_fixed_buffer(ctx, (duk_size_t)size);
    (void)_printf(_out_buffer, buffer, (size_t)-1, ctx,0);
    return 1;
}
void duk_printf_init(duk_context *ctx)
{
    if (!duk_get_global_string(ctx, "rampart"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    if(!duk_get_prop_string(ctx,-1,"utils"))
    {
        duk_pop(ctx);
        duk_push_object(ctx);
    }
    duk_push_c_function(ctx, duk_printf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "printf");
    duk_push_c_function(ctx, duk_sprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "sprintf");
    duk_push_c_function(ctx, duk_fprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "fprintf");
    duk_push_c_function(ctx, duk_bprintf, DUK_VARARGS);
    duk_put_prop_string(ctx, -2, "bprintf");
    duk_push_c_function(ctx, duk_fopen, 2);
    duk_put_prop_string(ctx, -2, "fopen");
    duk_push_c_function(ctx, duk_fclose, 1);
    duk_put_prop_string(ctx, -2, "fclose");
    duk_push_c_function(ctx, duk_fseek, 3);
    duk_put_prop_string(ctx, -2, "fseek");
    duk_push_c_function(ctx, duk_ftell, 1);
    duk_put_prop_string(ctx, -2, "ftell");
    duk_push_c_function(ctx, duk_rewind, 1);
    duk_put_prop_string(ctx, -2, "rewind");
    duk_push_c_function(ctx, duk_fread, 2);
    duk_put_prop_string(ctx, -2, "fread");
    duk_push_c_function(ctx, duk_fwrite, 3);
    duk_put_prop_string(ctx, -2, "fwrite");
    duk_push_object(ctx);
    duk_push_string(ctx,"stdout");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stdout");
    duk_push_object(ctx);
    duk_push_string(ctx,"stderr");
    duk_put_prop_string(ctx,-2,"stream");
    duk_put_prop_string(ctx, -2,"stderr");
    duk_put_prop_string(ctx, -2,"utils");
    duk_put_global_string(ctx,"rampart");
    if (pthread_mutex_init(&pflock, NULL) == EINVAL)
    {
        fprintf(stderr, "could not initialize context lock\n");
        exit(1);
    }
}
